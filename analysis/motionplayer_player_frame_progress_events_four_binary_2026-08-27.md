# Player frameProgress / timeline streams / pending events（四参考二进制，2026-08-27）

## 1. 结论

本切片闭合 `Player::frameProgress` 根、它直接控制的绝对 reseek、正向/反向四流推进、
modified/parameterized node refresh、variable/node 双槽解析与合并、join snapshot 恢复/清理，
以及进度 bridge 尾部的 pending-event 派发和逐构造态异常清理。

四端共同证明当前本地结构已经恢复了以下关键边界：

- `frameProgress` 每次只消费一次传入 dt；EmoteEngine 的 substep、变量 binder 和 clamp
  不属于 Player 内层；
- 入口先清 `_processedMeshVerticesNum` 和 `_motionCompleted`，随后写
  `_deltaTime = _speedMul * dt`，再执行 direct-edit 与 modified-node refresh；
- selected Player parameter、idle parameter table、first-frame、queue、正反向终点和 loop
  wrap 是互相独立的控制层；
- 每个增量方向函数严格按 `tag/layer event -> root content -> variable tracks -> node
  slots` 执行，不能把 tag 扫描挪到统一的帧尾；
- absolute reseek 在 node 双槽初始化后继续恢复/裁剪 HM4/HM3，并重建每个 HM1 entry
  的 child-node cache；
- parameterized node 使用统一的 forward + corrective-backward 双槽 seek，且不发 node
  action；ordinary node 使用方向专属循环并在越过 action frame 时排队；
- pending event vector 只遍历、不消费；非空时对 dispatch 无条件 `AddRef`，因此空指针
  是原生崩溃边界；
- action 回调按事件复制两个 Variant，sync 回调传 0 参数；一个 callback-result Variant
  在整轮中复用；异常只销毁已经构造的临时对象并释放 retained dispatch，不回滚已经执行
  的回调、Player 字段或 vector 前缀。

本轮没有发现新的 C++ 语义差异，因而没有修改生产代码。旧 coverage 中
`MP-D11-PLAYER-PROGRESS-RAW` 的“深层 helper 未闭合”说明由本报告和既有
`updateLayers` / `calcBounds` companion slices 正式接管并闭合。

## 2. 根函数、bridge callee 与 caller closure

### 2.1 fresh body

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `Player::frameProgress` | `0x6BE44C`，278 | `0x58A63A`，240 | `0x100113B50`，197 | `0x111556`，238 |
| pending-event dispatcher | `0x6C1870`，118 | `0x58C3A8`，90 | `0x10011622C`，97 | `0x113B64`，145 |

四个 root 与四个 dispatcher 均已 fresh decompile；disassembly 使用分页读取到
`cursor.done=true`，上表为精确指令数。`frameProgress` 四端合计 953 条，dispatcher
主 body 合计 450 条。

### 2.2 caller closure

四端 xref 联合结果把 `frameProgress` 的真实入口限定为：

1. `progressFrames` bridge；
2. type-3 child motion 的递归进度；
3. type-4 particle child 的递归 stepping；
4. `calcViewParam` 的准备路径。

Android arm64 另显示一条来自合并函数 chunk 的 raw xref，控制流仍归入上述根；没有发现
第五种独立 owner。pending-event dispatcher 只由 progress bridge 调用；Android arm64
同样因函数合并形状多显示一个 raw chunk xref，不是额外源码调用点。

完整 bridge 顺序保持为：

```cpp
currentDispatch = rawDispatch;       // raw pointer, no AddRef
frameProgress(frameDt);
updateLayers();
calcBounds();
dispatchPendingEvents(currentDispatch); // reload live field
currentDispatch = nullptr;           // normal return only
```

四个 phase 任一抛出异常，raw dispatch slot 都保留本次值；dispatcher 自己也不清事件
vector。这里与 raw callback 的参数转换边界共同记录在
`motionplayer_player_play_progress_raw_four_binary_2026-08-26.md`。

## 3. 直接阶段函数的四端分母

| 阶段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| refresh modified nodes | `0x6B3C58`，153 | `0x582A7C`，102 | `0x10010A88C`，74 | `0x10820C`，123 |
| full reseek | `0x6B5AA8`，982 | `0x583C8C`，662 | `0x10010C3CC`，526 | `0x109DAC`，758 |
| parameterized-node outer refresh / shared seek | `0x6B5224`，167（outer inline） | `0x5851BC`，40 | `0x10010DF70`，33 | `0x10B8A8`，28 |
| forward four-stream advance | `0x6B3EBC`，863 | `0x582BE0`，625 | `0x10010AA08`，553 | `0x1083D8`，730 |
| reverse four-stream rewind | `0x6B6E1C`，728 | `0x584838`，507 | `0x10010D230`，432 | `0x10AB68`，583 |
| 每端本表合计 | 2893 | 1936 | 1618 | 2222 |

这 20 个 fresh body 的完整 disassembly 共 8669 条，全部分页结束。Android arm64 的
`0x6B5224` 是单 node 的 `seekParameterizedNodeFrames`；outer node loop 被内联进
`frameProgress`。另外三端保留 40/33/28 条 outer wrapper，单 node helper 位于下一表。
该差异只反映编译内联决策。

## 4. 深层双槽、事件入队与 node 内容 helper

| helper | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| enqueue sync | `0x6B3674`，62 | `0x582674`，34 | `0x10010A3A4`，43 | `0x107C98`，76 |
| enqueue action | `0x6B376C`，72 | `0x582740`，48 | `0x10010A47C`，53 | `0x107DB0`，92 |
| initialize node slots | `0x6B388C`，242 | `0x5827D8`，199 | `0x10010A57C`，157 | `0x107EE8`，242 |
| step variable slot | `0x6B4C4C`，127 | `0x583518`，77 | `0x10010B604`，57 | `0x108EDC`，104 |
| merge variable slot | `0x6B4E50`，239 | `0x583648`，145 | `0x10010B76C`，113 | `0x109090`，192 |
| seek parameterized node | `0x6B5224`，167 | `0x58387C`，140 | `0x10010BA1C`，124 | `0x1093A0`，190 |
| restore/prune snapshots | `0x6B564C`，277 | `0x583B0C`，127 | `0x10010C1E8`，112 | `0x109BDC`，146 |
| parse node frame | `0x68FA94`，254 | `0x56EDE0`，172 | `0x1000F1464`，127 | `0xED638`，218 |
| merge node-frame content | `0x68FE90`，1924 | `0x56F06C`，1304 | `0x1000F1970`，983 | `0xEDD80`，1706 |

36 个 body 均已 fresh decompile，并读取精确的完整 disassembly。按 helper 横向合计分别为
215、265、840、365、689、621、662、771、5917 条；其中 Android arm64 的
`0x6B5224` 已在上一表出现，不能在 unique grand total 中重复计算。

## 5. frameProgress 共同控制流

四端去掉 ABI/container spelling 后的共同骨架如下：

```cpp
processedMeshVerticesNum = 0;
motionCompleted = false;
deltaTime = speedMul * dt;
if (directEdit) initEmoteMotion(2);
refreshModifiedNodeTimelines();

if (selectedParameterEntry) {
    t = selectedParameterEntry->value;
    if (firstFrame) { tick = eval = t; firstFrame = false; reseek(); return; }
    if (t > eval)   { tick = eval = t; advanceAllStreams(); return; }
    if (t < eval)   { tick = eval = t; rewindAllStreams(); return; }
    refreshParameterizedNodes();
    return;
}

if (!firstFrame && !allplaying) {
    if (parameterTable.empty()) return;
    refreshParameterizedNodes();
    return;
}
if (syncWaiting || motionCompleted) return;

if (firstFrame) {
    firstFrame = false;
    // negative-zero-origin seed, reverseSeekFlag corrective path,
    // absolute reseek, and sync/completion early exits
}

if (!queuing) frameTickCount += deltaTime;
if (!queuing) clampedEvalTime = min(frameTickCount, totalFrames);

if (deltaTime >= 0) {
    // normal forward, terminal stop, or
    // advance(end) -> reseek(loopTime) -> advance(wrappedTick)
} else {
    // normal reverse, terminal zero stop, or
    // rewind(loopTime) -> reseek(end) -> rewind(wrappedTick)
}
```

### 5.1 入口与 early-return 的可观察提交

- work counter 与 completion byte 在所有 early return 之前被覆盖；
- `_deltaTime` 在 direct-edit 之前提交，后续 anchor/damping/updateLayers 可见；
- modified-node refresh 位于 selected-parameter、idle 和 sync/completion gate 之前；
- `frameProgress` 不清 HM1/HM2/HM3/HM4，特别是 binder 写入的 HM2 跨帧保留；
- `_syncActive` 在本函数中只读，不会从 `_syncWaiting && _allplaying` 重新推导；
- large、negative、infinite、NaN dt 不在 Player wrapper 被额外 clamp 或 substep。

### 5.2 first-frame 与 queue

首次反向且 raw tick 为 0 时，tick/eval 从 `totalFrames` 开始。`reverseSeekFlag` 根据 delta
方向先把 absolute reseek 放在 0 或末帧，再向保存的 eval 做一次增量修正。每个 reseek/
advance/rewind 后都按原顺序检查 sync/completion。

first-frame 成功后不是无条件 return；它仍落入公共 cursor/wrap 机器。通常 play 设置的
queue gate 使增量数值不动，但公开 setter 可以提前清 gate，因此该 fallthrough 可观察。
queue 为真且未到正向终点时不执行方向流；queue 只阻止 tick 累加和普通方向派发，不会把
整个函数短路。

### 5.3 正向、反向与 loop wrap

- 正向到达/越过末帧：先把 eval clamp 到 `totalFrames`。有非负 loop target 时执行
  `advance(end) -> reseek(loop) -> wrap raw tick -> advance(wrapped)`；无 loop 时清
  `_allplaying`，gate 清时再执行终端 advance。
- 反向仍在 `[loopTime, +inf)` 内时走普通 rewind；越过零且 loopTime 为负时 clamp 到
  0、清 `_allplaying` 和 raw tick；有非负 loop target 时执行
  `rewind(loop) -> reseek(end) -> wrap raw tick -> rewind(wrapped)`。
- wrap 的 do/while 对 raw tick 做重复加减，不以一次 `fmod` 替代；字段提交点与异常时
  留下的部分结果因此也不同。

## 6. 四流、absolute reseek 与内部容器

### 6.1 增量四流的固定顺序

正向与反向 helper 都按以下顺序：

1. tag/layer event stream；
2. root priority/content stream；
3. variable-track list；
4. non-root node deque `[1, size())`。

tag phase 可以把 eval/tick snap 到 align/sync frame，并在 node walk 之前设置
`_syncWaiting` / `_motionCompleted` 或排队 sync/action。这也是 tag phase 不能被抽到帧尾的
原因。root phase 更新 cursor、content、current/next time。variable/node phase使用各自两槽
ping-pong 状态，供后续 `updateLayers` 只读插值。

Android 的 libstdc++ deque 与 iOS 的 libc++ deque 地址计算不同，但四端都证明 node 范围
是半开 `[1, nodeCount)`，不存在尾 sentinel；动态 getter/source lookup 可重入，因此循环
会重新读取 live end。

### 6.2 variable 双槽的 sharp edges

- step 先写 `frameIndex`，再按 index 取 frame、写 time，最后清 merged；中途异常保留
  已提交前缀；
- merge 先置 merged，再读 type/content/interval/value/easing；type 0 只置 type-zero
  状态并返回；
- forward phase 检查 slot0/slot1 的 merged flag，但两条 merge call 都传 slot0；这是
  四端共同的原生边界，不能“修正”为第二次传 slot1；
- reverse 与 absolute reseed 则分别合并真实的两个物理 slot；
- frame indices 使用 32 位 wrapping 算术；畸形 count/index 不增加本地防护。

### 6.3 absolute reseek 的完整阶段

full reseek 不是“把 cursor 设到目标”这一单步，而是：

1. 扫 tag frames；该 coarse scan 在 `time < target` 时出现 loop-body 加一和 for-update
   再加一的双步行为，缓存 time 还会经 int 截断；
2. 扫 root priority frames，按 `cursor -> content -> current time -> next time` 提交；
3. 绝对 reseed 每个 variable track 的两个 slot，并把 active cursor 置 0；
4. 对全部 non-root node 绝对初始化两个 ClipSlot；
5. 先恢复 HM4 variable snapshot，再按 path/type/joinTarget 恢复并 erase 匹配的 HM3；
   最后 invalidates 未匹配 child/particle owner，清 HM4 后清 HM3；
6. 遍历 HM1，为每个 cascade entry 重建 child-node cache。

tag、priority、current/next frame 的 owner 在 reseek 尾部之前一直存活。各动态 getter 之后
有意重读 Player live eval 或 persistent Variant，因此 re-entrant mutation 不应被过度
snapshot 隐藏。

### 6.4 node parse/merge 与 parameterized/ordinary 分叉

`parseNodeFrame` 先 reset slot、写 raw index/time/type；type 0 置 done 并返回；type 2/3
选择 crossfade；随后读取 content mask，`0x40000` 才读取 action。`mergeNodeFrameContent`
承担 nodeType 专属的完整 content payload 写入、Variant/string/vector owner 与 partial commit。

absolute initializer 顺序为 slot0 parse/merge、slot1 parse/merge、active=0、dirty=1、可选
source refresh、最后 exact-frame action。parser/merger 抛出时，不提前提交 active/dirty。

parameterized node 从 `parameterEntry->value` 取 selection time，执行统一的 forward seek 加
corrective backward seek；若发生 crossing 才 refresh source，且不产生 node action。
ordinary node 则按方向 crossing：forward action 观察 crossed old-other slot；rewind 先翻槽、
parse 新进入的 previous frame，再发该 frame action。完成 crossing loop 后才覆盖 dirty byte、
补 merge 尚未 merged 的两槽，并执行 source lookup。

modified-node refresh 对每个非 root、有 emoteEdit 的 node 读取 `modified`。真时先把脚本属性
写 0，再调用完整 absolute initializer；setter 的 Integer temporary 在 initializer 前销毁。

## 7. pending events 的数据结构、派发与异常边界

### 7.1 入队

pending vector 元素共同结构为：

```text
type (sync=1/action=0) + param1 Variant + param2 Variant
```

sync push 构造两个 Void Variant；action push 复制 param1，并由 action string 构造 param2。
vector growth、元素复制和 Variant 析构都遵循已有 `MotionEvent` owner；异常只保留已经完成的
vector 前缀。

### 7.2 派发伪代码

```cpp
if (events.empty()) return;
dispatch->AddRef();                  // unconditional: null crashes
Variant result;
Event *cursor = events.data();
while (cursor != events.data() + events.size()) { // live end reload
    if (cursor->type == 0) {
        Variant a(cursor->param1);
        Variant b(cursor->param2);
        dispatch->FuncCall("onAction", result, 2, {&a, &b}, dispatch);
    } else if (cursor->type == 1) {
        dispatch->FuncCall("onSync", result, 0, nullptr, dispatch);
    }
    ++cursor;
}
destroy(result);
dispatch->Release();
```

未知 type 只递增 cursor。vector 在返回后不清。循环条件重读 live end，因此回调 append 且
未 realloc 时，新元素可能在同轮被访问；一旦 realloc，旧 raw cursor 与参考实现一样失效，
本地不加入“安全索引化”修正。

### 7.3 四端 EH disposition

| 目标 | cleanup 形状 | 证据 |
|---|---|---|
| Android arm64 | 主函数尾部 landing pad | `0x6C19F8..0x6C1A44`：依构造态销毁 action 两临时和 result，Release retained dispatch，`_Unwind_Resume`；cleanup throw 到 terminate helper |
| Android armv7 | 无本帧 landing pad | 90 条 body 在栈保护检查后直接返回；没有函数内异常清理块 |
| iOS arm64 | 独立 cold cleanup + 2 terminate thunks | `0x1001163C4`，18；thunks `0x1001163C0`、`0x10011640C` |
| iOS armv7 | 独立 6-state SjLj switch | `0x113CEA`，37；按 state 销毁 0/1/2 个 action Variant 和 result，再 Release/Resume；cleanup throw 调 terminate |

共同源语义是“只销毁已经构造的 locals，result 在 dispatch Release 之前销毁，cleanup
析构/Release 再抛则 terminate/abort”。没有事务回滚，也不撤销已经执行的 callback。
Android armv7 的无本帧 landing 是该产物的异常表/编译策略边界，不能仅据它否定另外三端
一致可见的 source owner 顺序。

## 8. 目标差异

- 64/32 位 Player、deque、vector、Variant/accessor 布局与寄存器传参不同；共同字段顺序和
  逻辑提交点一致。
- Android 使用 libstdc++ deque/map walk，iOS 使用 libc++；HM1/HM3/HM4 的节点遍历形状
  不同，但相同 helper 只读取当前 entry/node，因此没有需要恢复的排序语义。
- Android arm64 把 parameterized outer loop 内联；其他三端保留 wrapper。
- arm64 ELF 的 dispatcher landing 在主 body，iOS arm64 采用冷区，iOS armv7 使用 SjLj，
  Android armv7 无本帧 landing；这四种形态映射到同一 RAII owner 边界。
- 浮点比较必须保留 ordered/unordered 分支结果；尤其 NaN 下 forward/rewind break、ratio gate
  和 terminal route 不能用表面等价但 NaN 语义不同的 C++ 条件重写。

没有发现 Android/iOS 产品级的功能开关差异；本切片不存在新的 `PLATFORM_BOUNDARY`。

## 9. 本地逐行映射

| 联合证据 | 本地位置 | 结果 |
|---|---|---|
| frame root、first-frame、queue、wrap | `cpp/plugins/motionplayer/PlayerFrameProgress.cpp:915` | 匹配 |
| full reseek 与 owner/提交顺序 | `cpp/plugins/motionplayer/PlayerFrameProgress.cpp:481` | 匹配 |
| HM4/HM3 restore/prune | `cpp/plugins/motionplayer/PlayerFrameProgress.cpp:620` | 匹配 |
| forward/reverse 四流总边界 | `cpp/plugins/motionplayer/PlayerFrameProgress.cpp:894`、`:905` | 匹配 |
| variable step/merge | `cpp/plugins/motionplayer/PlayerFrameProgress.cpp:114`、`:127` | 匹配 |
| node parse/merge | `cpp/plugins/motionplayer/PlayerUpdateLayerEval.cpp:907`、`:319` | 匹配 |
| node absolute init/parameter seek | `cpp/plugins/motionplayer/PlayerUpdateLayerEval.cpp:952`、`:997` | 匹配 |
| parameterized/modified outer refresh | `cpp/plugins/motionplayer/PlayerUpdateLayerEval.cpp:1240`、`:1271` | 匹配 |
| enqueue/dispatch | `cpp/plugins/motionplayer/PlayerFrameProgress.cpp:1229`、`:1234`、`:1240` | 匹配 |
| progress bridge | `cpp/plugins/motionplayer/PlayerFrameProgress.cpp:1292` | 匹配 |

本轮只新增分析记录和 coverage 对账，没有作 semantic C++ edit。相关差分测试已经覆盖
first-frame、direction/loop、variable 双槽、node parse/merge、parameterized refresh、事件
append/live-end 和异常 owner 等边界；正式 unit/Web build 仍因当前环境缺少
CMake/Ninja/Emscripten/Catch2 而不可执行。

## 10. IDB 改进与状态

四份 IDB 已对本报告中的 root、所有直接/深层 helper、dispatcher cold/SjLj cleanup 和
terminate thunks 统一命名，补充函数/行注释与 bookmarks，并原位保存。命名中特别区分：

- Android arm64 `0x6B5224` 是 `Player_seekParameterizedNodeFrames_guess`；
- Android armv7/iOS arm64/iOS armv7 的 40/33/28 条 wrapper 才是
  `Player_refreshParameterizedNodeTimelines_guess`；
- `Player_frameProgress_guess` 只用于四个真正 root，不用于 bridge 或合并 chunk。

状态：`IMPLEMENTED`。剩余工作是 coverage 的 root-reachable/function-pointer/vtable/static
lifetime 总分母对账和正式工具链验证，不再是本切片的 frame-progress 深层语义缺口。
