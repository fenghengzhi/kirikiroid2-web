# Player motion-sub phase 四参考二进制联合恢复

日期：2026-08-27

## 1. 四端函数与范围

| 平台 | 函数 | 完整指令 |
|---|---:|---:|
| Android arm64 | `0x6BB4A0` | 833 |
| Android armv7 | `0x587E00` | 760 |
| iOS arm64 | `0x100110EEC` | 709 |
| iOS armv7 | `0x10E68C` | 921 |

四端均 fresh decompile，并完整读取 disassembly，cursor 全部 `done=true`。此前参数指针和
visible-ancestor 报告只闭合本函数中的局部数据流；本报告闭合整个 type-3 child-Player phase：
重播、销毁、时间同步、角度、根变换、递归推进、事件转移和所有 owner/partial-commit 边界。

函数在 phase3 中位于 shape-geometry 之后、particle-emitter 之前，每端只有 updateLayers root
一个直接调用者。

## 2. 外层遍历与 child 取得

preview 为真时整函数立即返回。普通路径按 physical deque order 遍历 `nodes[1..end)`，只处理
`nodeType==3`；root 永不处理。

每个 type-3 node 先取得 replay mode：node parameter pointer 优先，null 时回退 Player selected
pointer，再从 entry 读取 mode；这一数据流已经由 C14 独立闭合。随后从 node-owned Variant 取得
child Player，并立即索引 child root 0。这里没有 child-null、root-empty 或 native-instance 的保护；
本地 `*mn.getChildPlayer()` 与 `child._nodes[0]` 保留该 malformed-input 边界。

`mode==0 && !node.accumulated.dirty` 时跳过重播和变换发布，但不会跳过尾部的父子链接、递归推进和
event aggregation。

## 3. teardown 与 replay

active slot `done` 为真或 `src` backing pointer 为 null 时进入 teardown：

1. `child.allplaying=false`；
2. 清空 variable-label-scope deque；
3. reset/release child 旧 node tree；
4. Clear stealth motion；
5. Clear motion key；
6. 直接 continue 到下一 parent node。

因此 teardown 不执行本轮 child `frameProgress/updateLayers`，也不转移 child pending events。各步骤
存在 partial-commit；TJS/分配异常不会事务回滚。

非 teardown 路径只在 `(parameterMode & 5) != 0 || node.flags != 0` 时 replay，并把完整 flags byte
覆盖为 `1`。src 以 `/` split 为临时 `vector<ttstr>`：

```text
if segmentCount == 1:
    child.setChara(src)
    child.play(slot.motionFlags | parameterMode, slot.icon)
else:
    child.setChara(segments[1])
    child.play(slot.motionFlags | parameterMode, segments[2])
```

多段分支不检查 `segmentCount>=3`，两段字符串仍直接访问 `[2]`。setChara 内部的 pending-char
刷新、ttstr CopyRef 和 split-vector 析构都保留 native owner 顺序。

## 4. queued child 时间同步

只有 replay 后 `child.allplaying && child.queuing` 才同步 child 时间：

```text
childTime = parent.frameTickCount
          - slot.clipStartTime
          + slot.motionTimeOffset
```

parent `_deltaTime<0` 且 child loopTime 非负时，反复执行
`childTime = childTime - totalFrames + loopTime`，直到 `< totalFrames`。随后按 native clamp profile
在零处夹紧，先写 child frameTickCount，再在 totalFrames 处上夹紧写 child clampedEvalTime；相邻
`queuing/firstFrame` bytes 同时置 1。parent 自己不 queued 时额外置 child reverseSeekFlag。

负/零 totalFrames、loopTime 与 IEEE NaN/Inf 没有额外健壮性保护，可能形成原版循环/浮点边界。

## 5. angle offset crossfade

angle mode 取 active slot `motionDt`；初始 `hasAngle=false`、angle=0，offset 使用 active `motionDofst`。
只有以下条件全部满足才混合两 slot offset：

- active `motionDocmpl`；
- active `crossfading`；
- other slot 未 done；
- other `motionDt != 0`；
- 两个 dofst 不相等。

parent time 在此处重新读取 node parameter pointer，null 才用 Player clampedEvalTime；不复用函数入口
mode pointer。角度差跨 180 度时把 other offset 加/减 360，再以两个 clip start 求比值；active
`accVariant` 非 Void 时调用 easing。线性组合后只加/减一次 360，而不是循环完全归一化。

## 6. 四个 angle mode

mode 1 直接产生 `active.dofst + node.accumulated.angle`，置 `hasAngle=true`，且不做 `[0,360)`
归一化。

mode 2 通常从 parent node delta position 求 atan2；coordinateMode 0 取 X/Y，1 取 X/Z。child
`noUpdateYet` 为真时按 native fallthrough 改走 mode 3。unsupported coordinate mode 仍把
`hasAngle=true`，保留初始 angle。

mode 3 要求 active 正在 crossfade 且 other 未 done；否则 angle absent。它再次独立重读 node
parameter pointer，以 parent time 和两个 clip start 求 ratio，分别在 ratio 与 ratio+0.0001 附近调用
position interpolation helper，再在 X/Y 或 X/Z 平面以 finite difference 求方向。端点使用
`0.9999/1.0` profile；除零、NaN 和 easing 异常均直接暴露。unsupported coordinate mode同样把
angle 标为 present。

mode 4 用 active `motionDtgt` 做 raw-label lookup；miss 时 angle absent。命中后从 target 与当前 node
accumulated position差求 X/Y 或 X/Z atan2；unsupported coordinate mode把 angle 标为 present。

mode 2/3/4 的结果以 while 循环归一化到 `[0,360)`；NaN 不进入循环，Inf 可能不终止。mode 0 和
未知 mode 保持 angle absent。

## 7. child root publication

先从 node accumulated position复制 X/Y/Z；active slot origin 非零时，用 accumulated 2x2 matrix
把负 origin 投影，并只在 coordinateMode 0/1 对 X/Y 或 X/Z 修正。随后按 native setter/直接 store
次序发布：

1. child root delta position；
2. flip；
3. zoom；
4. parent cameraAngle 到 child；
5. direct-edit 时先 `initEmoteMotion(2)`；
6. angle present 时 setAngleDeg；
7. slant；
8. opacity；
9. visible 使用 parent node `accumulated.active`，不是 `visible`；
10. 从 parent node color bytes 的首 32 位复制 child colorWeight。

各 setter 只在值改变时标 child root delta dirty，direct-edit angle 会循环归一化并再次选择 emote
motion。

最后恢复 child root accumulated 2x2。原版条件故意反直觉：`hasAngle`、computed angle恰好等于
parent accumulated angle、或 child directEdit 任一成立时直接复制 parent matrix；只有 angle absent、
默认 angle 与 parent angle不同、且非 directEdit 时才按角差和 flip parity旋转矩阵。完成后无条件
把 child root accumulated dirty 置真。

## 8. 父子拓扑、递归与事件 owner

无论前面的 dirty/mode 快捷路径是否执行，正常非 teardown 尾部都按顺序：

```text
childRoot.clipAABB = parentNode.clipAABB
childRoot.meshAncestor = parentNode.meshSeparator
    ? &parentNode : parentNode.meshAncestor
childRoot.visibleAncestor = parentNode.visibleAncestor
child.frameProgress(parent.deltaTime)
child.updateLayers()
parent.pendingEvents.insert(parent.begin,
                            child.pendingEvents.begin,
                            child.pendingEvents.end)
child.pendingEvents.clear()
```

三个指针都是 borrowed raw pointer，不 AddRef、不 range-check；visibleAncestor 可跨 Player。事件 insert
保持 child 顺序并整体前插，所以多个 child 依 parent traversal 次序会形成后处理 child 更靠前的
队列。insert 成功后 clear 析构 child 的 event 元素但保留 capacity；若 insert 分配/复制抛异常，
child 队列尚未 clear，parent 可能已处于标准库允许的部分状态。

## 9. 本地对照与结论

`PlayerUpdateChildMotion.cpp` 的控制流、字段选择、setter 顺序、直接索引边界、角度存在位、浮点
结合、跨 Player 指针、递归次序和 event vector owner 逐项匹配。此前已依据同一组完整函数证据：

- 恢复三个独立 parameter pointer load frontier；
- 删除 parameter index resolver；
- 恢复 visibleAncestor raw pointer转发；
- 删除非参考 snapshot/logging 和 node ordinal sidecars。

完整 phase 复核未发现额外差异，本轮无需修改编译语义。四库已追加全函数注释、bookmark并保存。

## 10. 验证限制

已执行 coverage 严格 12 列、duplicate-ID 检查和 `git diff --check`。当前环境缺少正式
CMake/Ninja/Emscripten 依赖工具链，不能声称 unit/Web build 通过。
