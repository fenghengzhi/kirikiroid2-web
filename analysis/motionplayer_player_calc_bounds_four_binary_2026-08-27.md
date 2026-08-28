# Player calcBounds 四参考二进制联合恢复

日期：2026-08-27

## 1. 结论与 slice 边界

本报告闭合 `Player::calcBounds` 的完整递归 AABB 数据流和 owner 生命周期：入口 ResourceManager
dispatch snapshot、Player extrema reset、particle-child 递归、type-3 child 递归、普通 node 的三种点源、
node/Player bounds 发布和 unwind 边界。

prepared-item builder、projection 与 MotionNode/PreparedRenderItem 销毁已有独立报告；本 slice 不重复
宣称它们，但会关闭 calcBounds 中最后一个非原生 `MotionNode::index` 消费者。四端完整函数都没有
Web trace、motion-path 物化、label 转换、格式化或 node ordinal 字段。

## 2. 四端完整映射

| 平台 | 函数 | 完整指令 | 范围 |
|---|---:|---:|---:|
| Android arm64 | `0x6C10E4` | 480 | `0x6C10E4..0x6C186C` |
| Android armv7 | `0x58BE38` | 402 | `0x58BE38..0x58C336` |
| iOS arm64 | `0x100115C68` | 332 | `0x100115C68..0x1001161A0` |
| iOS armv7 | `0x11354C` | 433 | `0x11354C..0x113A80` |

四端均 fresh decompile，并从 offset 0 读取完整 disassembly；四个 cursor 都为 `done=true`。
Android 64 的较大指令数来自 libstdc++ deque count、展开的四角比较和 EH 形状；iOS 64 对多个
min/max loop 生成更紧凑的条件选择。共享控制流和 owner 边界一致。

## 3. 共同源码结构

```text
calcBounds():
    rmVariant = copy(Player.resourceManager)
    rmOwner = rmVariant.AsObject()       // independent retained dispatch
    destroy rmVariant

    player.minX = DBL_MAX
    player.minY = DBL_MAX
    player.maxX = -DBL_MAX
    player.maxY = -DBL_MAX

    for each nonroot node in physical deque order:
        if !preview && node.type == 4:
            particleArrayOwner = copy(node.particleArray).AsObject()
            count = particleArrayOwner.Count()       // once
            for particleIndex in [0, count):
                child = particleArrayOwner[index].nativePlayer
                child.calcBounds()
                merge child Player bounds into this Player
            release particleArrayOwner

        if node.activeSlot.done:
            continue

        if !preview && node.type == 3:
            child = node.child.nativePlayer
            child.calcBounds()
            node.bounds = float(child Player bounds)
            merge node.bounds into this Player
            continue

        mask = preview ? 0x1449 : 0x1441
        if !(mask & (1 << node.type)) || !node.source.valid:
            continue

        node.bounds = { FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX }
        if node.compositeMeshPoints is nonempty:
            scan every composite point
        else if node.transformedMeshControlPoints is nonempty:
            scan exactly 16 points
        else:
            scan exactly 4 vertex pairs

        node.bounds = { floor(minX), floor(minY), ceil(maxX), ceil(maxY) }
        merge node.bounds into this Player

    release rmOwner
```

## 4. 重要顺序和边界

### 4.1 ResourceManager owner

入口无条件从 Player canonical Variant 复制并 `AsObject`。所得 dispatch owner 在 reset extrema 之前
建立，并一直存活到全部节点、全部递归 child 和全部浮点发布完成；body 内没有使用它。这个看似
多余的 owner 仍可观察：AddRef/Release 时点、AsObject 失败、递归/re-entrant 期间 receiver 寿命以及
异常 cleanup 都依赖它。本地的 Variant copy + `RetainedDispatch_guess` 保留同一边界。

任一递归、Array dispatch、Variant 转换或内存异常都会沿编译器 cleanup 路径先销毁当前临时 owner，
最终释放 rmOwner 后继续 unwind。正常返回只 Release 一次。

### 4.2 particle 路径先于 done gate

非 preview 的 type-4 node 即使 active slot 已 done，仍先处理 particle child Array。Array dispatch
在 Count 之前 retain，Count 只读一次，并跨全部 indexed Get、child recursion 和 child bounds merge
保持同一 receiver；loop 结束后才 Release。Count/元素/native pointer 均按参考值信任，没有 null、
负 count、类型或 bounds recovery gate。

particle child 只把 child Player 四个 double bounds merge 到当前 Player，不写 type-4 node 的
`bounds[4]`；随后仍落入 active-done/ordinary gate。

### 4.3 type-3 child 路径

active slot done 会在 type-3 解析前跳过 node。非 preview type-3 直接取得 child native Player 指针，
递归 calcBounds，然后把 child 的四个 double extrema逐项缩窄为 node float bounds，再从这些 float
扩大 parent Player double extrema。这里存在刻意的 double -> float -> double 精度边界。路径最后
`continue`，不会再经过 ordinary source gate。

### 4.4 ordinary 三种点源

preview/non-preview mask 分别为 `0x1449`/`0x1441`，并与 `source.valid` 共同门控。mask 使用 native
整数 shift；超出正常 node-type 域属于未防御边界，不能添加 recoverable type check。

点源优先级严格为：非空 composite vector 的全部元素；否则非空 transformed-mesh vector 的前
16 个元素；否则固定四个普通 vertex pairs。第二分支只以“非空”门控，却无条件读取 16 项，因此
长度 1..15 是 native 越界 sharp boundary，不得改成 `min(size,16)`。

比较使用 `<=` 更新 minima、`>=` 更新 maxima；相等时会以较晚 point 覆盖同值，NaN 不更新当前
extrema。四个结果分别调用 floor/floor/ceil/ceil 后写回 float，再提升为 double merge 到 Player。
空点集不会在正常结构出现；若由破坏状态进入，sentinel 会被 floor/ceil 后发布。

## 5. 容器与 ABI 差异

Android/libstdc++ 的 MotionNode 大于 deque block threshold，每 block 一个 node；root 通过
iterator-difference 公式动态计算 size。iOS/libc++ 每 block 16 nodes，并使用保存 count/map block
解析。record stride 为 2632/2272/2648/2228，属于 ABI 差异，不进入 portable C++。

composite 和 transformed 点容器都是连续 `MeshPoint` vector；composite 以 end-begin 导出的实际
count 遍历，transformed 分支固定读取 128 bytes（16 * 两个 float）。普通 vertices 固定读取
32 bytes（4 * 两个 float）。node bounds 是内嵌四 float，Player bounds 是四 double。

## 6. 修改前本地差异

native 主体、三条递归/普通路径、mask、点源优先级、缩窄、floor/ceil 和两个 scoped dispatch owner
均已匹配。确认的不匹配是 calcBounds 独有诊断旁路：

1. rmOwner 建立后读取 Web trace 开关；
2. 可选调用 `matchedMotionPath` 并分配 string；
3. 每个普通 node 在 native bounds 已经写完后又复制 `actualBounds`、比较同源
   `expectedBounds`、转换 label、构造两个 `fmt::format` string 并调用 trace checker；
4. 读取参考 record 不存在的 `node.index`；
5. loop 后额外格式化并记录 Player 总 bounds。

这些操作会增加 allocation、Variant/string conversion、logger/re-entrancy 和异常前沿，可能让
函数在 native 已发布一部分 node/Player bounds 后提前退出。四端完整函数在 rmOwner 析构前均直接
结束节点循环，没有这些 call。因此必须删除整组旁路，而不是只删 node index 参数。

## 7. 证据后实施

完成上述四端完整证据后：

- 从 `PlayerRenderItems.cpp::calcBounds` 删除 trace 开关、motion-path 物化、per-node check 和最终
  Player log；
- 普通 node 直接把 floor/floor/ceil/ceil 写入 `node.bounds`；
- 在 motion-sub、updateLayers phase2/root、shape-AABB 和 calcBounds 所有诊断消费者都已分别完成
  fresh 四端审计后，删除 `MotionNode::index` 及 build/root 的两处赋值；
- 保留建树局部 `thisIndex`，因为 raw-label map value 和 children parentIndex 确实使用它；删除的只是
  node record 中从未存在的重复 ordinal。

四个 IDB 已统一命名 `Player_calcBounds_guess`，添加语义注释与 bookmark 并保存。

## 8. 验证限制

实施后执行 `rg` 确认 compiled motionplayer 不再存在 `MotionNode::index`/`node.index`，并执行
`git diff --check`、coverage 严格 12 列和 duplicate-ID 检查。当前环境缺 CMake/Ninja/Emscripten
正式工具链；单文件语法检查也受仓库依赖头限制，因此不能声称 unit/Web build 通过。

