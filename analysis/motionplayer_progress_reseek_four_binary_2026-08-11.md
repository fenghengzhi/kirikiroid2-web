# MotionPlayer `progress` / full reseek / Join 快照尾链四参考二进制对照（2026-08-11）

> **2026-08-12 闭环说明：** 本文第 3.3、8、12.3 节记录的是修正前本地状态；
> frame-unit bridge 的 current-dispatch owner、无 event clear、无 wrapper 懒加载/钳位
> 以及最终源码/IDB 落地，已由
> `analysis/motionplayer_player_progress_bridge_four_binary_2026-08-12.md` 取代。
> 本文的 frame core、reseek、Join 与 HM1 证据仍有效。

## 1. 证据范围

本记录只使用 `reference/binaries/` 中四个参考产物及其 IDB，不把历史 `libkrkr2.so` 地址当作函数身份：

| 目标 | IDB 会话 |
| --- | --- |
| Android arm64-v8a | `motion_android_arm64` |
| Android armeabi-v7a（文件名为 `armabi-v7a`） | `motion_android_armv7` |
| iOS arm64 | `motion_ios_arm64` |
| iOS armv7 | `motion_ios_armv7` |

本轮从 `Motion.Player.progress` 的 UTF-16 成员名重新定位包装体，再沿调用图追到 frame stepping、绝对 reseek、Join 快照 restore/prune 和 HM1 每项重建。旧源码注释中的 `0x6D2A98`、`0x6C106C`、`0x6B86C8`、`0x6B826C`、`0x6997F0`、`0x6B9650` 等均属于旧目标；例如现行 Android arm64 的旧 `0x6B826C` 落在 ground-correction helper 内部，旧 `0x6B86C8` 也不是 reseek 入口，不能再用作当前四体映射。

## 2. `progress` 字符串定位

### 2.1 搜索方式与结果差异

普通 IDA 字符串列表搜索只返回若干 ASCII `progress`，没有给出 Motion.Player 的精确宽字符串。按 `ida-search-string` 流程继续用原始字节分别搜索 UTF-8、UTF-16LE、UTF-32LE；UTF-32LE 无命中，精确成员名由 UTF-16LE 命中。四处均读取前后边界，确认双零结尾及相邻成员名：Android 两份位于 `skip / pass / progress / modified` 邻域，iOS 两份直接位于 `play / progress / stop` 邻域。

| 目标 | `progress` UTF-16LE | Player registrar 中 xref |
| --- | ---: | ---: |
| Android arm64 | `0x14BEE6C` | `0x6D5D44`，registrar `0x6D3DA8` |
| Android armv7 | `0xD76C48` | `0x598684/0x598690`，registrar `0x597EC8` |
| iOS arm64 | `0x10195CE42` | `0x100125074`，registrar `0x1001244F8` |
| iOS armv7 | `0x174F1A6` | `0x1242CC/0x1242D2/0x1242DE`，registrar `0x123848` |

## 3. 包装体与 progress bridge

### 3.1 四文件映射

| 阶段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| TJS `progress` wrapper | `0x6CFE78` | `0x595598` | `0x100121204` | `0x11FFB4` |
| native progress bridge | `0x6CFE34` | `0x595570` | `0x1001211C0` | `0x11FF88` |
| frame progress core | `0x6BE44C` | `0x58A63A` | `0x100113B50` | `0x111556` |
| update layers | `0x6B871C` | `0x5856E0` | `0x10010E544` | `0x10BE5C` |
| recursive bounds/post-update | `0x6C10E4` | `0x58BE38` | `0x100115C68` | `0x11354C` |
| pending-event dispatch | `0x6C1870` | `0x58C3A8` | `0x10011622C` | `0x113B64` |

上述 wrapper、bridge 和 frame progress core 均在本轮重新反编译；recursive bounds 四体也重新反编译并由“重置 min/max、递归 child/particle Player、遍历节点累积 bounds”的共同函数体确认，不是仅凭调用顺序命名。

### 3.2 共同伪代码

四个 TJS wrapper 的源码级行为一致：

```text
self = NCB native instance(objthis)
if self == null: return invalid-object/native-class error
if argc < 1: return TJS_E_BADPARAMCOUNT (-1004)
dtFrames = argv[0].AsReal() * 60.0 / 1000.0
progressBridge(self, objthis, dtFrames)
return success
```

四个 progress bridge 的源码级行为一致：

```text
self.currentDispatch = objthis
frameProgress(self, dtFrames)
updateLayers(self)                    // 无 nodes.empty() 守卫
calcBoundsRecursively(self)
dispatchPendingEvents(self, self.currentDispatch)
self.currentDispatch = null
```

32/64 位的对象槽宽度、NCB native-instance 取法和 STL/TJS variant helper 不同；参数个数门控、毫秒到 60fps 帧的换算、五段调用顺序及 dispatch 所有权没有目标差异。

### 3.3 与本地实现的逐行对照（修改前）

本地 `cpp/plugins/motionplayer/PlayerFrameProgress.cpp`：

| 本地位置（修改前） | 四体结果 | 结论 |
| --- | --- | --- |
| `progressCompatMethod` 先取得 native instance | wrapper 同序 | 一致 |
| native instance 后直接 `ensureMotionLoaded()`，没有 `numparams < 1` 返回 | 四 wrapper 都在取实参前检查 `argc >= 1`，失败返回 `-1004` | **语义偏差**；应在任何加载/进度副作用前返回 `TJS_E_BADPARAMCOUNT` |
| `delta * 60 / 1000` | 四 wrapper 同式 | 一致 |
| `_pendingEvents.clear()` 后 `frameProgress()` | bridge 的 pending cursor 置零后进入 core | 本地 vector 是游标/队列的抽象，入口行为一致 |
| `if(!self->_nodes.empty()) self->updateLayers()` | 四 bridge 均无条件调用 update-layers | **语义偏差**；空节点树仍必须进入 update-layers 自身的边界逻辑 |
| 无条件 `calcBounds()` | 四体 recursive bounds/post-update | 一致 |
| 遍历 `_pendingEvents`，以本次 `objthis` 调 `onAction/onSync`，再清空 | 四体 dispatch helper 读取 bridge 暂存的 current dispatch，结束后 bridge 清空 owner | 对脚本 wrapper 路径的可观察行为一致；内部 EmoteEngine frame-unit bridge 的 dispatch owner 仍需单独审计 |

同文件 `progressMsLike_*` 与 `progressFramesLike_*` 也带有相同的 `_nodes.empty()` update 守卫。它们建模同一 native bridge 的毫秒入口和 frame-unit 内部入口，因此应一并移除守卫；但 frame-unit 路径当前只清队列而不持有/派发 current dispatch，属于后续对象生命周期缺口，本轮不凭 wrapper 路径擅自设计 owner 字段。

## 4. frame progress 到绝对 reseek

### 4.1 当前四体调用映射

| 阶段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| frame progress | `0x6BE44C` | `0x58A63A` | `0x100113B50` | `0x111556` |
| full reseek | `0x6B5AA8` | `0x583C8C` | `0x10010C3CC` | `0x109DAC` |
| forward incremental | `0x6B3EBC` | `0x582BE0` | `0x10010AA08` | `0x1083D8` |
| backward incremental | `0x6B6E1C` | `0x584838` | `0x10010D230` | `0x10AB68` |
| equal-time parameter-node loop | `0x6B7D30` | `0x5851BC` | `0x10010DF70` | `0x10B8A8` |
| parameterized node two-slot stepper | `0x6B5224` | `0x58387C` | `0x10010BA1C` | `0x1093A0` |

frame progress 四体都从 first-frame、wrap 或方向性重定位分支进入同一个 full-reseek 函数；正常单调时间变化进入独立 forward/backward 增量函数，等时参数路径另有小函数。不能把 full reseek 当作每帧唯一更新入口。

补充更名：fresh whole-function decompile 证明 forward/backward 两个增量入口各自都
内联处理 layer/tag、root/priority、variable-track、node timeline 四条流，旧 IDB 名
`Player_advanceVariableTracks_guess` / `Player_rewindVariableTracks_guess` 过窄。四库与
源码现统一为 `Player_advanceTimelineStreams_guess` /
`Player_rewindTimelineStreams_guess`；本地四个 phase helper 是从该原生大函数抽出的
source-level 区段，不声称是参考二进制中的独立函数。

同步清理了节点阶段中沿用自旧 Android `libkrkr2.so` 的地址式
辅助函数名。本地 `seekNodeFramesForwardPhase_guess` /
`seekNodeFramesBackwardPhase_guess` 明确表示两个原生四流函数的内联
node phase；`seekParameterizedNodeFrames_guess` 才对应 parameterized node 调用的
共享双向 stepper。

> **2026-08-14 node-slot helper 纠正：** 进一步 fresh decompile parser、merger、
> absolute initializer、parameterized stepper 及其所有 code xref 后，确认不只是语义
> 更名。共享 parameterized stepper 自己持有 Player 并在实际跨 slot 后执行 gated
> `findSource`；absolute initializer 也自己拥有两次 parse/merge、source 与 exact-frame
> action 尾部。等时/未播放 early-return 则只遍历 parameterEntry 非空节点，不推进普通
> 时间节点。本地已按这些独立边界修正，完整记录见
> `analysis/motionplayer_node_timeline_slot_helpers_four_binary_2026-08-14.md`。

本轮更名后的验证补充（2026-08-14）：Web Debug 从 31 个受影响对象重编到
`index.html`/Wasm 最终链接成功；完整 motionplayer Catch2 翻译单元以真实
Emscripten 参数执行 `-fsyntax-only` 成功，仅有既有 `_tss` warning。四份 recovery
IDB 当时应用了新函数名与四流顺序注释。其 `(Player *, double)` prototype 已由
2026-08-15 的 fresh 三调用点/四端 ABI 审计纠正为 this-only；详见下方 incremental
variable-track 补充。

### 4.2 full reseek 的共同数据流

四个 full-reseek 大函数均重新反编译。尽管 Android 使用 libstdc++ deque（大对象表现为一元素 block）、iOS 使用 libc++ 固定 16 元素 block，源码级顺序一致：

```text
1. 从头扫描 layer/tag 时间流，重建游标与 align/sync/action 状态
2. 从头扫描 root/priority 时间流，重建游标、当前/下一时间和 content snapshot
3. 遍历 variable-track deque，为每个 track 从绝对目标时间重播两个 slot
4. 从 node index 1 到 nodeCount-1，对每个非 root node 做绝对 timeline init
5. 恢复并清理 Join 的 HM4 variable snapshot 与 HM3 per-node snapshot
6. 遍历 HM1 unordered-map 的每个内部节点，重建该 entry 的 heapResult
7. 释放扫描中持有的临时 TJS dispatch/ttstr owner
```

尾链映射：

| helper | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| node absolute init | `0x6B388C` | `0x5827D8` | `0x10010A57C` | `0x107EE8` |
| HM3/HM4 restore-prune | `0x6B564C` | `0x583B0C` | `0x10010C1E8` | `0x109BDC` |
| HM1 per-entry rebuild | `0x6B6A30` | `0x58466C` | `0x10010D004` | `0x10A930` |

本地 `reseekTimelineCursors()` 的五段扫描/初始化、`restoreAndPruneJoinSnapshots_guess()` 调用及对 `_evalCascadeMap` 每项调用 rebuild 的顺序与四体一致。`PlayerUpdateLayerEval.cpp` 中仍称 STEP 5 “DEFERRED / m2logo never hits”的注释与现行代码以及四体调用链同时冲突，应删除该过时描述。

> **2026-08-15 root-priority cursor 边界补充：** fresh 四端 decompile/disasm
> 进一步确认 `priority["count"]` 是普通 signed 动态属性读取，不是容器
> `GetCount`。非零 count 分支无论是否 `count >= 1` 都先提交扫描 cursor；因此负值
> 必须从零开始再做 `min(cursor, count-2)`。本地原先只在 `count >= 1` 时提交，可能
> 保留更负的旧 cursor，现已修复。精确字段偏移、四端指令地址、`count==0/1/2`、
> negative/NaN、内容/时间提交顺序及回归见
> `analysis/motionplayer_root_priority_reseek_cursor_boundary_four_binary_2026-08-15.md`。

> **2026-08-15 variable-track absolute reseed 补充：** fresh 四端
> decompile/disasm 重新确认每个 `VariableLabelScope` 都先复制独立
> `frameSource` owner，再读取 signed dynamic `count`；owner 活过两组
> step/merge 与 cursor reset。`count-2` 是 32 位回绕减法，`INT_MIN`
> 必须选 seed 0，不能落入 C++ signed-overflow UB；每次 time getter 后仍需
> 重读 live Player 时间。字段/stride、四套 deque block、helper 提交顺序、
> root 共用边界与回归见
> `analysis/motionplayer_variable_track_absolute_reseed_four_binary_2026-08-15.md`。

> **2026-08-15 node absolute reseed 补充：** full-reseek 的下一相位已重新
> 对齐四端独立 Player-first helper。selection target 在动态 getter 前快照；
> retained frame-list owner 负责 scan 与尾部生命周期，但 parse/merge 故意重读
> node 持久字段。`count-2` 同样以 32 位回绕，且 active/dirty 只在两槽完整
> parse/merge 后提交。旧本地 owner 缺失、提前 cursor commit 和 source gate
> range guard 已修正，详见
> `analysis/motionplayer_node_absolute_reseed_four_binary_2026-08-15.md`。

> **2026-08-15 variable-track incremental forward/rewind 补充：** 两个四流
> member 的全部调用点都只传 Player，函数内每轮从 Player 重读 evaluation time；旧
> `(Player*, double)` type 与本地 target 快照均不成立。forward 每轨先复制一个
> 只供 dynamic count 使用的 source owner，step/merge 仍重读持久字段；`count-2`
> 32 位回绕后与 frameIndex 做 signed compare，active 使用 raw cursor，时间门以
> ordered `<` break 表达，故 NaN 会继续。cursor 在 step 前提交，step 再先提交
> frameIndex；两次 merge 故意都是 physical slot0。rewind 不读 count、没有局部
> owner，live ordered `>` 门在 time getter 重入后立即生效，index zero 无 guard
> 下溢到 `0xFFFFFFFF/-1`，最后 merge physical slot0/slot1。精确四端地址、异常与
> owner 生命周期回归见
> `analysis/motionplayer_variable_track_incremental_seek_four_binary_2026-08-15.md`。

> **2026-08-15 incremental tag/root forward/rewind 补充：** 四流成员入口先
> 构造 tag source owner，tag phase 完成后才构造 priority source owner；二者跨
> root/variable/node phase 活到 aggregate 尾部并按 priority、tag 逆序释放。forward
> tag 只在 signed `count >= 1` 时运行，root 却无 count gate；两者都使用 32 位回绕
> `count-2`、wrapping cursor increment 和 ordered `<` break，因此 NaN 继续，root
> 的 `INT_MIN-2` 还能从 cursor 0 进入。rewind tag 只测试 `count != 0`，root 完全不
> 读取 count；两者 cursor 零均回绕为数字索引 `-1`，ordered `>` 使 NaN 停止。
> align/sync/action、content/time 的提交顺序以及动态 getter 后的 live evaluation
> 重读已按四端修正，详见
> `analysis/motionplayer_incremental_tag_root_streams_four_binary_2026-08-15.md`。

> **2026-08-15 incremental non-root node forward/rewind 补充：** 四流成员末段走
> `1..<live nodeCount`，每轮重新计算跨 libstdc++/libc++ deque 的 size。parameterized
> node 两方向都调用同一共享 stepper；ordinary forward 快照 raw selector、建立仅供
> count 的 frame-list owner，以 wrapping `count-2`、live ordered-LT 推进，并按
> selector→parse old-active→crossed old-other action 顺序提交。rewind 不读 count、
> 不建 owner，以 live ordered-GT 和 wrapping decrement 进入 previous slot，零索引按
> signed `-1` 读取。两方向仅在 crossing 完整结束后 exact `flags=1`，再 physical
> slot0/slot1 merge 与 raw-nodeType source shift；旧 range guard 已删除。精确四端地址、
> owner/异常前缀和平台 shift 差异见
> `analysis/motionplayer_node_incremental_seek_four_binary_2026-08-15.md`。

## 5. Join HM4/HM3 restore-prune

### 5.1 四体容器布局差异

| 项目 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | --- | --- | --- | --- |
| node 大小/块形态 | `2632`，libstdc++ 大元素 deque | `2272`，libstdc++ 大元素 deque | `2648`，libc++ 16 元素 block | `2228`，libc++ 16 元素 block |
| 非 root 范围 | index `1..<count` | 同 | 同 | 同 |
| node joinTarget gate | `node+46` | `node+38` | `node+46` | `node+38` |
| snapshot nodeType | entry `+16` | entry `+16` | entry `+24` | entry `+12` |
| restore helper | `0x696BD0` | `0x572E52` | `0x1000F6904` | `0xF3588` |
| terminal clear | `0x6B54C4` | `0x583A54` | `0x10010BC60` | `0x109614` |

这些偏移差异来自指针宽度、variant/ttstr 布局和两套 STL，而非不同算法。

### 5.2 共同 prune 语义

```text
if HM4 非空:
  for each variable track:
    slot = track.slots[track.activeSlotCursor]
    if slot 处于 active/type-nonzero 状态 and HM4 contains track.cascadeKey:
      slot.value = HM4[key].value

if HM3 非空:
  for node index 1..<nodeCount:
    key = buildNodeIdentityPath(node)
    if HM3 contains key
       and node.joinTarget
       and snapshot.nodeType == node.nodeType:
      restoreNodeFromSnapshot(node, snapshot)
      if snapshot.nodeType == 0 and !snapshot.done:
        resolve/rebind snapshot source
      erase only this matched HM3 node

clear HM3 and HM4 unconditionally at the tail
```

未命中或 gate 失败的 HM3 entry 在扫描中不会提前 erase，但最终仍由 terminal clear 释放。Android arm64 把抵达 node end 的分支直接跳到 clear helper；本地循环结束后 clear 等价。iOS arm64 的 hash bucket/count fast gate 比其他产物多一个空表检查，不改变非空语义。

本地实现的 HM4 active-slot restore、HM3 identity/type/joinTarget gates、matched-only erase 和末尾双 map clear 均与四体一致。

## 6. per-node restore 的字段与所有权

四个 restore helper 均在本轮重新反编译。共同顺序：

```text
slot = node.slots[node.activeSlotIndex]
if node.meshType == 1:
  slot.meshControlPoints = snapshot.meshControlPoints

if snapshot.nodeType == 3:
  node.childPlayerVariant = snapshot.childPlayerVariant
  snapshot.childPlayerVariant.clear()

if snapshot.nodeType == 4:
  node.particleArrayVariant = snapshot.particleArrayVariant
  snapshot.particleArrayVariant.clear()
  if !snapshot.done:
    memcpy(slot.particleInterpolationBlock, snapshot.block, 72)

if !slot.done && !snapshot.done:
  restore contentMask, blendMode, origin, colors, opacity,
          xyz, flipXY, angle, scaleXY, slantY
```

边界行为：

- variant 操作不是单纯 `swap`：四体都先对目标做 copy-assign/ref acquire，再立即 clear snapshot/ref release。随后 prune 会 erase map node。当前本地赋值后 `Clear()` 与这个引用计数生命周期相符。
- 粒子 72 字节块与公共标量块使用不同 gate：粒子块只看 snapshot 未 done；公共块同时要求当前 active slot 未 done。
- 四体公共块都恢复 `slantY`，都跳过 `slantX`。这不是旧单库的反编译假象；本地故意不写 `slot.slantX` 是正确边界行为。
- mesh restore 位于 type 3/4 variant restore 之前，并仅由 node meshType gate；本地顺序一致。

## 7. HM1 每项 rebuild 的容器与边界行为

### 7.1 共同伪代码

```text
if entry.weight == 0.0:
  return

entry.weight = 0.0
entry.heapResult.clear()        // end=begin，保留 capacity
temporaryChain = []

for scanNodeIndex in 1..<nodeCount:
  scanNode = nodes[scanNodeIndex]
  if scanNode.nodeType not in {3,4}:
    continue

  idx = scanNodeIndex
  loop:
    temporaryChain.insert(begin, nodes[idx].layerName)
    if temporaryChain.size > entry.chainSegments.size:
      temporaryChain.pop_back()
    if sizes equal and all labels equal:
      entry.heapResult.push_back(&scanNode)
      break
    idx = nodes[idx].parentIndex
    if idx <= 0:
      break
```

四体的临时 `vector<ttstr>` 在不同候选节点之间复用，并不会在外层每次迭代时 clear；它通过“插首 + 超长弹尾”持续保持参考长度的窗口。本地把 `chain` 声明在外层、`insert(begin)`、`pop_back()`，与四体一致。Android helper `0x6B7994/0x584F9C` 和 iOS helper `0x10010DB14/0x10B430` 的 fresh decompile 都确认第二实参是 vector begin，因此不是 push_back。

### 7.2 STL/ABI 差异

- Android 64/32 的 MotionNode 分别为 `2632/2272` 字节，libstdc++ deque 的大小表达式出现模逆常量；反编译中的 `-1` 抵消实现细节偏置，实际上界仍是 `scan < nodeCount`，不存在尾哨兵。
- iOS 64/32 使用 libc++ deque 的 16 元素 block 索引；都显式以 `nodeCount < 2` 提前结束，否则从 1 扫到 `<nodeCount`。
- Android 的 ttstr 比较展开成 shared body/type-tag/wcscmp，iOS 保留 libc++/TJS helper；都是值相等比较。
- `heapResult.clear()` 只缩 end，push 时复用容量；本地 `std::vector::clear()` 保持同一生命周期性质。

### 7.3 2026-08-12 四体新鲜复核

本轮再次对当前四个 IDB 中的实现执行反编译，而不是复用旧
`libkrkr2.so` 注释。映射保持为 Android arm64 `0x6B6A30`、Android
armv7 `0x58466C`、iOS arm64 `0x10010D004`、iOS armv7 `0x10A930`；四体
共同支持第 7.1 节伪代码。

复核中特别锁定了以下容易被普通 C++ 重写改变的边界：

- 只有 `weight == 0.0` 才提前返回；NaN 会执行重建，随后被写成 `0.0`。
- `heapResult.clear()` 保留 capacity。
- 临时链在外层候选节点循环之前构造，**不会**在候选间清空；它只通过
  `insert(begin, label)` 与超长时 `pop_back()` 维持窗口。
- 空 `chainSegments` 在第一次插首并弹尾后立即匹配，因此收集所有
  type 3/4 非根节点。
- 父索引只检查 `<= 0`；正数路径没有额外的本地范围保护。
- 写入的是最初的候选节点地址，而不是祖先遍历最终停留的节点地址。

本地实现语义无需改变；仅将旧单库地址式名称
`rebuildEvalCascadeHeapResultLike_0x6B9650` 统一为
`rebuildEvalCascadeEntry_guess`，并把源码中的旧地址叙述移回本文档。

## 8. 本轮可实施修正与继续项

有四体共同直接证据、可以立即修改的语义项：

1. `progressCompatMethod` 在 native instance 有效后、任何加载或进度副作用前，增加 `numparams < 1 -> TJS_E_BADPARAMCOUNT`。
2. 三个本地 progress bridge 入口都无条件调用 `updateLayers()`，移除 `_nodes.empty()` 的 port-only guard。

已证明无需修改的高风险项：

- Join restore 中 variant copy-assign 后 Clear；
- 粒子块/公共标量双层 done gate；
- 故意不恢复 `slantX`；
- prune 只 erase matched HM3 entry，末尾再 clear 两表；
- HM1 临时链跨候选复用、插首弹尾、真实 nodeCount 上界。

继续项：

- 为 frame-unit EmoteEngine→Player bridge 恢复 current-dispatch owner 的传递与 pending event dispatch；必须从四个 EmoteEngine 调用点另建映射，不能由脚本 wrapper 猜测。
- 从四个 `playImpl` 继续定位 load/emote-init/non-emote-init 的当前地址与资源 owner 交接；旧注释中的 `0x6B365C` 不再作为身份依据。
- 将本轮确认的 current-address 名称写入四个 IDB 并保存；编译源码仅保留语义名/四体证据说明，地址集中在本分析文件。

## 9. `play` wrapper → load helper 的临时 dispatch owner

### 9.1 当前四体映射

| 阶段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| `play` NCB wrapper | `0x6CFFE8` | `0x59565C` | `0x1001212C0` | `0x120050` |
| native `play` | `0x6AF5C8` | `0x5800EC` | `0x1001074A4` | `0x104A7C` |
| `playImpl` | `0x6AF664` | `0x580158` | `0x100107540` | `0x104AE8` |
| load / find-motion helper | `0x6AE2F0` | `0x57F654` | `0x1001067BC` | `0x103BBC` |
| emote wrapper init | `0x6B0270` | `0x5807E0` | `0x100107D38` | `0x105350` |
| ordinary motion init | `0x6B0A3C` | `0x580C28` | `0x100108258` | `0x1058F8` |

四份 wrapper、native play、playImpl 与 load helper 均在本轮重新反编译。load helper 的隐藏返回对象/寄存器约定使 64 位反编译形参显示发生一位错位，但从四份 caller、参数引用和 32 位显式签名可以共同恢复真实源码级签名：

```text
findMotionResult loadMotion(Player *self,
                            ttstr &lookupChara,
                            ttstr &lookupMotion)
```

### 9.2 wrapper 对临时 owner 的生命周期

```text
self = native Player from objthis
if argc < 2: return TJS_E_BADPARAMCOUNT

self.currentDispatch = objthis       // raw/non-owning，不 AddRef
motionCopy = argv[0]
flags = argv[1].AsInteger()
self.play(motionCopy, flags)
release motionCopy
self.currentDispatch = null
```

Android/iOS arm64 的字段偏移为 Player `+16`，两份 32 位为 `+8`。四体都只是普通指针写入，没有 AddRef/Release；真正的调用期 owner 是 NCB wrapper 的 `objthis` 实参。`progress` wrapper 也复用同一字段，供 pending-event dispatch 使用。

### 9.3 load helper 的共同数据流

```text
lookupChara  = copy(requested stealth/chara slot)
lookupMotion = copy(requested motion argument)

if self.currentDispatch != null:
  request = Dictionary{
    "chara":  lookupChara,
    "motion": lookupMotion
  }
  response = self.currentDispatch.onFindMotion(request)
  lookupChara  = response["chara"]  (属性读取失败则走 helper 的默认值路径)
  lookupMotion = response["motion"] (同上)
  release response/request 临时 dispatch owners

path = "motion/" + lookupChara + "/" + lookupMotion
return self.resourceManager.findMotion(self.findMotionContext, path)
```

load helper 本身不把回调调整后的字符串写回 Player 的 primary/stealth motion label。`playImpl` 在成功后仍从原始 `play` 实参复制到 stealth label，并在非 Stealth 请求时也复制到 primary label；回调调整值只决定本次查找路径。`findMotion` 返回对象的元素 0/1 随后分别 copy-assign 到 Player 的 motion-content variant 与 matched-key/context variant，临时返回对象再释放。

Android 产物的字面量有时被 IDA 误识别为窄 `"c"/"m"`，iOS 也会显示 `"m"`；宽字符串原始字节与同函数的 property helper 调用共同确认请求字典键为 `chara/motion`，路径前缀为 `motion/`。这属于 IDA 字符串类型识别差异，不是平台协议差异。

### 9.4 与本地实现的逐行对照（修改前）

| 本地位置 | 四体结果 | 结论 |
| --- | --- | --- |
| `playCompat` 解析 native instance、要求两个参数 | wrapper 同序 | 一致 |
| `playCompat` 直接调用旧 `playMotionLike_*`，没有保存 `objthis` | wrapper 在两次参数转换前写 currentDispatch，正常返回后清零；异常时不清 | **生命周期与数据流缺口** |
| `ensureMotionLoaded(chara,motion)` 直接拼路径并调 ResourceManager | load helper 在 currentDispatch 非空时先调用 `onFindMotion` 并使用回写字符串 | **脚本 `Player.play` 可观察偏差** |
| `setMotionCompat` 自己手写 callback、改写 chara/motion，再调用 play | 四端 motion 属性是普通 typed setter，只调用 `play(0,label)`；setter 不保存 objthis，load 看不到 callback dispatch | **确认的旧补偿逻辑，已删除** |
| `setCharaSlotLike_0x6B29C0` 以旧单体地址命名，pending 用局部 move/clear 近似 | 四端是 live writer + pending coordinator；值相等完全 no-op，stealth-first 按 copy last-write-wins，flush 直接借用 pending 字段并在返回后 clear；真实变化只清两条 label 与 playing | **确认的旧地址与 owner/边界偏差，已替换** |
| `stopCompat` 作为 raw callback 清 playing 后把 result 写成 true | 四端 `stop` 均为 typed zero-arg void method；wrapper 先 clear result、只拒绝负 argc、接受 surplus，native body仅清 playing | **确认的 wrapper ABI 偏差，已替换** |
| `getAllplaying` 从 root 开始且对所有节点调用 child unwrap，并对 null child 静默 skip | 四端从 deque index 1 开始、每轮重读 size、仅递归 nodeType 3；非 object 抛出，null/wrong-native pointer 仍无 guard 递归；type 4 不参与 | **确认的容器遍历与 malformed 边界偏差，已替换** |
| 成功查找后 `_motionContentVariant=result[0]`、`_findMotionContextVariant=result[1]` | playImpl 同序 copy-assign | 一致 |
| 成功后 label 槽写原始 `label` | playImpl 明确从原始实参写 label，而非回调局部副本 | 一致，不能改成 callback-adjusted motion |

实施方式应保留原版 owner 语义：在 Player 中增加非 owning `_currentDispatch`；`playCompat` 在参数转换前设置 raw 字段，只在正常返回尾部清零，异常时保留原指针；load helper 对入参先做局部副本，仅在字段非空时执行 callback，再以副本查找。字段不能 `AddRef`，也不能把 callback 调整值提交到 `_motionKey/_stealthMotion`。`play`/`playImpl` 的完整提交与异常边界见 `motionplayer_player_play_commit_state_four_binary_2026-08-14.md`；角色 live/pending 槽见 `motionplayer_player_chara_pending_four_binary_2026-08-14.md`。

## 10. emote 二次选片与 ordinary-init 的分流

### 10.1 当前四体映射

| 阶段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| emote init | `0x6B0270` | `0x5807E0` | `0x100107D38` | `0x105350` |
| ordinary/non-emote init | `0x6B0A3C` | `0x580C28` | `0x100108258` | `0x1058F8` |
| build-node-tree outer | `0x6B25D0` | `0x581CC8` | `0x1001097C8` | `0x107060` |
| reset/release old tree | `0x6B2AD8` | `0x581F3C` | `0x100109ACC` | `0x107358` |
| init variables | `0x6CAB30` | `0x592944` | `0x10011D540` | `0x11BF04` |

上述 emote init、ordinary init、build-node-tree outer 和 reset/release helper 均在本轮对四个当前 IDB 重新反编译。四体的 STL 展开差异仍分别是 Android libstdc++ 大元素 deque 与 iOS libc++ 16 元素 block，不改变源码级调用边界。

### 10.2 emote init 的共同边界行为

```text
angle = cameraAngle + emoteAngle
while angle < 0:    angle += 360
while angle >= 360: angle -= 360

selected = 1
for each division interval (previous, current]:
  if previous < angle && angle <= current: break
selected %= divisionCount

if selected == priorSelection:
  return
priorSelection = selected

path = motionList[selected].toString()
parts = split(path, '/')
secondaryMotion = parts[2]              // 无 size guard

result = loadMotion(liveStealthChara, secondaryMotion)
if result exists:
  motionContent = result[0]
  findMotionContext = result[1]
  initNonEmoteMotion(flags)
else:
  TVPAddLog("motion not found <chara>/<motion>")
  motionContent.clear()
  findMotionContext.clear()
```

四体均不为 `divisionCount == 0` 增加保护，也不检查 split 后是否至少有三个元素；这些看似危险的行为是参考插件的真实边界。二次查找复用第 9 节的同一个 load helper，因此 `play` wrapper 的 current-dispatch owner 在 emote 二次选片期间仍然有效，`onFindMotion` 同样可调整二次查找路径。

2026-08-12 的独立 emote-init 纵向进一步确认：失败调用是 important=false 的普通
`TVPAddLog`，不是 throw；日志发生在双 Variant clear 之前；成功路径只检查返回 Variant
非 Void，并让完整 load-result owner 存活到 ordinary init 返回之后。本地因此把
`loadMotionResult_guess` 与字段提交拆开，同时修正 primary play 的同类 owner 生命周期。
完整证据见 `motionplayer_emote_init_four_binary_2026-08-12.md`。

同日完成的 load-caller 全审计进一步证明四端 `Player_loadMotion_guess` 都只有 primary
play 与 emote 二次选片两个直接调用语义。draw/render/progress/getVariable 不做隐式
load，本地旧 `ensureMotionLoaded` 因而已整体删除。完整映射和边界见
`motionplayer_load_callers_four_binary_2026-08-12.md`。

## 11. ordinary init、旧树释放时点与容器生命周期

### 11.1 ordinary init 的共同数据流

```text
loopTime  = motionContent["loopTime"]
lastTime  = motionContent["lastTime"]
tag       = owning copy of motionContent["tag"]
priority  = owning copy of motionContent["priority"]
root      = owning copy of priority[0]["content"]

rawNodeLabelMap.clear()
parameterEntries.clear()                 // capacity/storage retained

parameterize = motionContent["parameterize"]
if parameterize is object:
  append one parameter entry
  finalize parameter table
  if parameterEntries is nonempty:
    defaultParameterPtr = begin           // empty 时不写旧 pointer
else:
  parse motionContent["parameter"]
  if parameterize is integer:
    if index < 0 or index >= count:
      throw "parameter id out of range."
    defaultParameterPtr = &entries[index]
  else:
    defaultParameterPtr = null

allPlaying = true
adjacentStateByte = false
buildNodeTree()
initVariables()

if !(flags & Chain):
  frameTick = 0
  clampedEvalTime = min(lastTime, 0)
  queuing = true
  firstFrame = true
else:
  firstFrame = true
```

`tag`、`priority`、root `content` 都是保存在 Player 字段中的 TJS owner；它们不是解析后的 Web-only 快照。`parameterize` 的 object/integer/other 三分支、越界异常文本以及 object 为空时不覆写默认指针，在四体完全一致。两份 64 位产物通过相邻半字写入、两份 32 位产物通过等价 byte 写入表达 `allPlaying=1` 与相邻状态字节清零。

### 11.2 build-node-tree 外层的共同结构

```text
rootContentOwner = retain(Player.rootContent)
resetAndReleaseOldTree(Player)
build root node from rootContentOwner
layers = rootContentOwner["layer"]
build descendants recursively(parent = root)

for node index 1..<nodeCount:
  if node.type == 12 && (node.flags & 4):
    for each referenced raw label:
      target = rawNodeLabelMap[label]
      if target exists && target.type in {0,3}:
        node.maskTargets.push_back(target)
        target.isMaskTarget = true
```

type-12 引用解析在整棵树和 raw-label map 完成后才执行。引用缺失或 target 类型不是 0/3 时静默跳过；mask target vector 只存原始 node pointer。节点本体在四体中位于稳定地址的 deque block 内，因此 push vector 不会使这些指针失效。本地以 `std::deque<MotionNode>` 和 `std::vector<MotionNode*>` 表达相同稳定性。

### 11.3 reset/release helper 的共同对象生命周期

四端首 helper 不是“只创建根节点”，而是完整的旧树拆除：

```text
resourceManagerOwner = retain(Player.resourceManager)
destroy every cached/prepared render item through callback walk
reset per-entry evaluation/ramp cursors

for node index 1..<oldNodeCount:
  resourceManager.releaseLayerId(node.layerId1)
  resourceManager.releaseLayerId(node.layerId2)
  if node has live prepared render layer:
    resourceManager.releaseLayerId(prepared.renderLayerId)

erase/destroy non-root deque suffix
rawNodeLabelMap.clear()
release resourceManagerOwner
```

Android 反编译把 deque range-erase 展开为迭代器/块算术；iOS 显式出现 `releaseLayerId` 宽字符串。三类 layer ID 的释放、非根节点析构和保留 synthetic root 的结果一致。

### 11.4 与本地实现的逐行对照（修改前）

| 本地位置 | 四体结果 | 结论 |
| --- | --- | --- |
| `initNonEmoteMotionLike_*` 入口立刻调用 `resetNodeTreeForBuildLike_*` | ordinary init 先读取/持有五个 motion 字段，再只清 label map/parameter vector；旧节点直到 `buildNodeTree` 内才释放 | **释放时点过早**；属性读取失败或 parameter index 越界时可观察 |
| 入口 reset 同时清 `_nodeLabelMap`，随后清 `_parameterEntries` | 四体在取得 root content 后按此顺序清两容器 | 容器内容结果相同，但异常边界顺序不同 |
| `buildNodeTree()` 内再次调用 reset helper | 四体 build outer 也在创建新根前调用 reset/release helper | 这个 reset 属于正确函数边界，应保留 |
| `detail::buildNodeTree` 建 root/descendant 后解析 type-12 引用 | 四体同序 | 一致 |
| build 后调用 `initVariables()` | 四体同序 | 一致 |

实施修正：删除 ordinary-init 入口的完整 tree reset；在 root content owner 写入后显式清 `_nodeLabelMap`，紧接着清 parameter vector/default fallback；保留 `buildNodeTree()` 内唯一一次完整 reset。这样既维持最终容器内容，也恢复“先解析新 motion，成功抵达建树阶段后才释放旧层/旧子 Player”的异常安全和 owner 生命周期。

## 12. progress bridge 的 dispatch owner 与事件 vector 持久性

### 12.1 四端 bridge 与调用点

| 项目 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| Player progress bridge | `0x6CFE34` | `0x595570` | `0x1001211C0` | `0x11FF88` |
| script `progress` wrapper | `0x6CFE78` | `0x595598` | `0x100121204` | `0x11FFB4` |
| Emote progress call site | `0x67A7E8`（IDA tail chunk） | `0x55FFA8` in `0x55FEF0` | `0x1001B43DC` in `0x1001B4304` | `0x1B3ED2` in `0x1B3E10` |
| metadata/init zero-step call site | `0x67A990` in `0x67A8B0` | `0x5600A4` in `0x560020` | `0x1001B4504` in `0x1001B4468` | `0x1B403C` in `0x1B3F58` |
| event dispatch helper | `0x6C1870` | `0x58C3A8` | `0x10011622C` | `0x113B64` |

四份 bridge、event dispatch、Emote progress caller 和 metadata/init caller 均在本轮重新反编译。Android arm64 的 Emote progress 主体被 IDA 归入一个异常的 shared/tail chunk（显示 owner `sub_530E3C`），但实际 call site、实参寄存器和其余三端一致，不能把该 IDA 函数边界误当成源码差异。

bridge 的共同伪代码为：

```text
Player.currentDispatch = dispatchArg       // 64 位 Player+16，32 位 Player+8
Player.frameProgress(frameDt)
Player.updateLayers()                      // 无条件
Player.calcBounds()
Player.dispatchPendingEvents(Player.currentDispatch)
Player.currentDispatch = null
```

script wrapper 传 `objthis`；Emote progress 与 metadata/init 的 zero-step 调用都明确传 `null`。因此 Engine 内部帧推进不会把外层 EmotePlayer 的 dispatch 偷渡给 Player；它依赖“这条内部路径不会生成 Player action/sync event”的前置条件。

### 12.2 event vector 的真实布局与 dispatch 行为

| 项目 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| event vector begin/end/cap | `+936/+944/+952` | `+656/+660/+664` | `+824/+832/+840` | `+592/+596/+600` |
| event 元素大小 | `44` | `28` | `44` | `28` |
| payload | `int type + 2×tTJSVariant` | 同 | 同 | 同 |

type `0` 是 `onAction(param1,param2)`，type `1` 是 `onSync()`；其他 type 被遍历但不调用。enqueue helpers 只做 `vector::push_back`/capacity growth；bridge 和 dispatch helper 都不 clear、erase 或缩短 vector。dispatch helper 的共同生命周期是：

```text
if events.empty(): return
dispatch.AddRef()                           // 无 null guard
resultVariant = void
for event in events:
  if type == 0:
    p1 = owning copy(event.param1)
    p2 = owning copy(event.param2)
    dispatch.onAction(p1,p2,&resultVariant)
  else if type == 1:
    dispatch.onSync(&resultVariant)
destroy resultVariant
dispatch.Release()
```

这带来两个不寻常但四端一致的边界：

1. 已 enqueue 的事件在 dispatch 后仍留在 Player vector 中，后续 bridge 会再次遍历；原版没有“派发即消费”语义。
2. 当 vector 非空而 `dispatchArg == null` 时，helper 在进入循环前就无条件虚调用 `AddRef`，属于原版的 null-dereference/crash 前置条件；只有空 vector 的 Engine 内部路径安全。

IDA 对短宽字符串仍会误显示为 `"o"`，但四体交叉对照确认两个成员名为 `onAction` 与 `onSync`。

### 12.3 与本地实现的逐行对照（修改前）

| 本地位置 | 四体结果 | 结论 |
| --- | --- | --- |
| `progressFramesLike_*` 开头 `_pendingEvents.clear()` | bridge 首写 currentDispatch，不碰 event vector | **错误丢弃既有事件并掩盖 null-dispatch 边界** |
| `progressFramesLike_*` 末尾 `_pendingEvents.clear()` | dispatch 后 vector 保持不变 | **错误消费事件** |
| `progressCompatMethod` 手动派发后 clear vector | dispatch helper 不 clear | **重复派发边界不一致** |
| 手动派发直接用 `objthis`，未写 `_currentDispatch` | bridge 先保存 raw pointer，dispatch 时从字段重新读取，末尾清零 | **对象字段生命周期缺口** |
| 手动派发不 AddRef/Release、callback result 传 null、逐事件 catch-all | helper 整批 AddRef/Release，同一个局部 result variant 跨事件复用，无逐事件吞异常层 | **引用计数与回调结果 owner 不一致** |

实施方向：提取一个 member dispatch helper，严格保留“空 vector 才允许 null dispatch”、整批 AddRef/Release、复用 callback result variant、vector 不消费；script wrapper 在本地 lazy-load 适配完成后写 `_currentDispatch=objthis`，按 bridge 顺序派发并清字段；Engine frame-unit 路径显式写 null，不再用 clear 模拟 Player 指针字段。

## 13. 本轮构建验证

- `out/web/debug`：在 current-dispatch/load callback 与 ordinary-init 释放时点修正后增量编译、静态库链接和 `index.html/index.wasm` 链接成功。
- `out/wasmtime/debug --target krkr2_wasmtime_guest`：30 个受 `Player.h` 影响的目标完整重编，guest wasm 链接及 exnref 转换成功。
- 编译输出只有仓库既有的 `_tss` literal-operator deprecation、Emscripten pthread+memory-growth 与 JSPI experimental 警告；无新增 error。
- Web build tree 的 CTest 配置仍为 0 个测试。当前可见 Python runtime 都缺少 `wasmtime` 包，故未在全局环境安装新依赖，也未伪报 differential runtime 已执行。

## 14. 2026-08-15 tag/root full-reseek ABI 与 owner 尾部复核

四端 full-reseek 的六处 caller 均只传 Player 指针，函数本身也在每个动态 `time`
getter 后重新读取 Player 当前 evaluation time；旧端口的 `double targetTime` 参数是抽取
helper 时引入的快照，并非原始成员 ABI。现已恢复为无显式参数的
`reseekTimelineCursors()`，tag/root 以及 variable-track absolute scan 都在四端对应的
getter 后读取 live Player 字段。node absolute initializer 原本就直接读取 Player。

函数 epilogue 还证明四个 owner 跨过 variable/node/join/HM1 尾部继续存活，构造顺序为
tag source、priority source、current root frame、next root frame，析构严格反序。旧端口的
phase-local 引用既没有独立 CopyRef，又会过早结束作用域；现在改为函数作用域的 owning
Variant。tag phase 自身的 content/next/current frame 临时量仍在进入 priority phase 前逆序
释放。

tag coarse scan 的双增量、signed `count<1` 全 phase skip、real 扫描但 integer→double
cache、align→sync→action 顺序、iOS UTF-16 `type/time/content/align/sync` 误显示修正、字段
偏移、释放地址和重入回归详见
`motionplayer_tag_absolute_reseek_four_binary_2026-08-15.md`；root 的 negative-count
cursor 提交与 `count-2` 边界详见
`motionplayer_root_priority_reseek_cursor_boundary_four_binary_2026-08-15.md`。
