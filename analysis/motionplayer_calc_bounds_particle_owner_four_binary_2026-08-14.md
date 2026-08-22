# `calcBounds` type-4 粒子 Array owner 四参考恢复（2026-08-14）

## 1. 范围与结论

本纵切面只闭合 `Player::calcBounds` 递归 AABB pass 中 type-4 粒子节点的
Array dispatch 生命周期、读取顺序、递归合并和异常边界。其余 bounds getter、入口
ResourceManager owner、节点几何扫描和 IEEE-754 行为仍以
`motionplayer_bounds_four_binary_2026-08-12.md` 为主文档。

四端共同结论是：每遇到一个非 preview type-4 节点，函数从该节点的持久
Array `Variant` 只 CopyRef/AsObject 一次；临时 `Variant` 随即销毁，但所得 Object
dispatch 继续保留，覆盖一次 count 读取、所有升序数字元素读取，以及每个 child 的递归
`calcBounds`。旧 Web 源码分别调用 `getParticleCount()` 和 `getParticleChild()`，会在
每一步重新借用 node `Variant`，因此在 getter/child 重入清空或替换字段时会切换 receiver
或抛错，与四端不符。

## 2. 四端地址映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `Player_calcBoundsRecursive_guess` | `0x6C10E4` | `0x58BE38` | `0x100115C68` | `0x11354C` |
| non-preview type-4 分支 | `0x6C12E8` | `0x58BEEA` | `0x100116058` | `0x11363A` |
| CopyRef/AsObject 并保留 Array | `0x6C12F4` | `0x58BEF2` | `0x100116068` | `0x11364C` |
| 销毁 Array Variant 临时副本 | `0x6C1340` | `0x58BF04` | `0x100116084` | `0x113662` |
| 从同一 dispatch 读取 count | `0x6C134C` | `0x58BF0E` | `0x100116090` | `0x113670` |
| 从同一 dispatch 数字取 child | `0x6C1370` | `0x58BF22` | `0x1001160B4` | `0x113692` |
| child 递归 `calcBounds` | `0x6C1378` | `0x58BF26` | `0x1001160BC` | `0x113694` |
| 四个 double AABB 包含等号合并 | `0x6C137C..0x6C13C8` | `0x58BF2A..0x58BF80` | `0x1001160C0..0x10011610C` | `0x11369C..0x1136F4` |
| 释放该节点的 Array owner | `0x6C13E8..0x6C13F8` | `0x58BF8E..0x58BF96` | `0x100116128..0x10011613C` | `0x113700..0x11370E` |

这里的 Array owner 与函数入口从 Player 持久 ResourceManager `Variant` 建立的 owner
互相独立。进入 type-4 分支时，入口 owner 仍存活；随后再建立节点 Array owner；每个
child 递归又建立自己的入口 owner。正常返回时先释放本节点 Array owner，整个函数结束时
再释放入口 owner。四端异常展开表也包含相同两层清理职责。

## 3. 共同控制流

```text
calcBounds(player):
    resourceOwner = retainObject(CopyRef(player.resourceManagerVariant))
    reset player AABB to +/-DBL_MAX

    for nodeIndex in [1, nodeCount):
        node = nodes[nodeIndex]

        if !player.preview && node.type == 4:
            arrayOwner = retainObject(CopyRef(node.particleArrayVariant))
            count = getCount(arrayOwner.dispatch)  // once, signed int

            for index = 0; index < count; ++index:
                child = getNativePlayerByNum(arrayOwner.dispatch, index)
                child.calcBounds()
                mergeInclusive(player.AABB, child.AABB)

            release(arrayOwner)

        if node.activeSlot.done:
            continue
        ... type-3 / ordinary bounds path still follows ...
```

type-4 分支不是整个节点的无条件 `continue`。粒子循环结束后，函数仍检查该节点的 active
slot，并可能继续进入同一节点的普通 bounds 路径；这与 prepared render builder 的 type-4
分支完成后直接跳过 container 本身不同。

## 4. 读取、重入与 native 转换边界

- count 只读取一次并保存为有符号 `int`；`count <= 0` 不进入循环；
- index 严格为 `0,1,...,count-1`，元素在每一轮临时读取，不预先快照；
- 因而 count 是快照，但元素 property 是 live 的：先前 child 的递归回调可以修改尚未读取
  的同一个 Array dispatch，后续轮次会看到修改后的元素；
- count helper 忽略 property getter 的 HRESULT，再转换输出 Variant；
- 数字 getter 的 HRESULT 同样由共享 helper 忽略，然后无条件 Object 转换和 Player native
  query；
- native query 成功但 native Player 指针为 null 时，没有额外 null guard，下一步直接成员
  调用；
- count getter、数字 getter或 child 递归若替换/清空 `node.particleArrayVariant`，当前循环
  仍使用进入分支前保留的 dispatch，不重新读取字段；
- 若这些调用抛异常，Array owner 由展开路径释放；已经完成的 child AABB 合并不回滚，且
  后续 active/type-3/ordinary 路径不执行。

每个 child 返回后，父 Player 的四个 double 分别做独立的 `<=,<=,>=,>=` 比较。这会保留
旧 bounds 主文档记录的 NaN、无序 sentinel 和 signed-zero 后写行为；没有 `haveBounds`，也
没有 child AABB ordered 预检查。

## 5. 源码恢复

`PlayerRenderItems.cpp` 的 type-4 bounds 分支现在显式构造一个
`detail::ScopedParticleArrayDispatch_guess`，缓存其 dispatch，并直接调用共享的
`particleArrayCount_guess` / `particleArrayGetNativePlayerAt_guess`。该 RAII owner 的作用域
覆盖完整粒子循环和全部 child 递归，随后才落入同一节点的 active-slot gate。

没有把这一规则塞回 `MotionNode::getParticleCount/getParticleChild`：这两个单次公共 helper
各自独立借用仍是合理语义；需要跨多次操作保持 receiver 身份的是调用方的 type-4 pass。

## 6. 确定性回归

`tests/unit-tests/plugins/motionplayer-dll.cpp` 复用了自定义
`PreparedParticleArrayDispatch`：

1. parent Player 建立一个 type-4、active-slot done 的非 root 节点；
2. 自定义 Array 内持有两个有效 Player NCB adaptor；
3. `count` getter 第一次调用时重入清空 `node.particleArrayVar`；
4. `calcBoundsForDifferentialTest_guess()` 必须仍在原 dispatch 上依次读取 `{0,1}`；
5. persistent node Variant 已为 Void，但外部测试 owner 清除前 Array 不析构；
6. 清除最终外部 owner 后恰好析构一次。

旧逐次 `node.getParticleChild()` 实现会在第一次数字读取前对已清空的 Variant 再做 Object
转换，因而该用例可确定性地区分两种生命周期，而不是只验证普通输入的最终 AABB。

## 7. IDB 回写与验证

四份 recovery IDB 已在 Array acquisition、count、indexed lookup 和 release 点写入相同语义
注释；本轮重新反编译确认注释落点仍处于同一 `Player_calcBoundsRecursive_guess` 控制流。

- 完整 motionplayer 单测 TU 使用真实 Emscripten response file 执行
  `-fsyntax-only`：通过；仅有仓库既有 `_tss` literal-operator 弃用警告；
- `cmake --build out/web/debug --parallel 8`：通过；重编
  `PlayerRenderItems.cpp`、重建 motionplayer 静态库并成功链接最终 `index.html`；
- 对本纵切面涉及的源码、测试、分析和计划文件执行 `git diff --check`：通过；仅输出
  工作树既有 LF/CRLF 转换提示；
- Android ARM64/ARMv7、iOS ARM64/ARMv7 四份 recovery IDB 均成功原位保存。
