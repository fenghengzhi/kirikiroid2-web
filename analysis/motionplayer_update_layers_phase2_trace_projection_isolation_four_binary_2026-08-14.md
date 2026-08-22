# MotionPlayer `updateLayers` phase-2 trace projection isolation（四参考二进制，2026-08-14）

## 1. 结论

本地 `updateLayersPhase2_MainLoop` 里有一个明确标为 diagnostic-only 的
`TimelineTraceState`，但旧调用位置没有任何 gate：每个非根 node、每帧都会调用
`readNodeFrameSlotsForTrace`，复制 active/other slot 的标量，并把两个 `ttstr srcValue`
窄化成 `std::string`。结果只被三个 logo-chain log block 使用，不参与
`evaluateTimeline_guess`、transform/inheritance、ground correction 或 node publication。

四个当前参考二进制的完整 `Player_updateLayers_guess` 已 fresh 反编译/分页反汇编：全部
0 string references，direct call 集中只有 production evaluator、matrix/mesh/ground helpers
与 phase-3 workers；没有 trace-state projection、UTF narrow、motion-path、fmt 或 logger。

因此旧位置会在 trace 关闭时为每个 node 增加 native 不存在的 string conversion、潜在 heap
allocation、`bad_alloc`/UTF conversion 异常和大量标量复制。本轮把 motion-path
materialization 与整个 `TimelineTraceState` 构造都移入一次计算的 path-specific trace gate。
默认 phase 2 不再调用这条 diagnostic helper。

## 2. 四端 production call site

| 目标 | `Player_updateLayers_guess` | `Player_evaluateTimeline_guess` call site | string refs |
|---|---:|---:|---:|
| Android arm64 | `0x6B871C` | `0x6B89CC` | 0 |
| Android armv7 | `0x5856E0` | `0x58590E` | 0 |
| iOS arm64 | `0x10010E544` | `0x10010E858` | 0 |
| iOS armv7 | `0x10BE5C` | `0x10C2BA` | 0 |

四端 phase-2 共同顺序：

```text
for each non-root node:
    resolve effective parent through inherit-0x400000 chain
    timelineDirtyArg =
        previousCameraConstraintDirty || groundCorrection ||
        parent.accumulated.dirty || node.delta.dirty
    if !evaluateTimeline(node, currentTime, timelineDirtyArg):
        continue

    neutralize consumed delta transform overrides
    clear delta.dirty
    handle done branch or active transform branch
    optionally deform through parent mesh
    compose position/coordinate mode
    optionally apply ground correction
    compose opacity/inherit flags/matrix
```

`evaluateTimeline` 直接读取已经由 progress/reseek 定位的 node slots。native 没有在 call 前
复制它们到另一个 record，更不会把 `src` 转为 UTF-8。四端 full direct-call list 中对应
位置前后是 production memcpy/matrix/evaluator/deform/ground calls，不存在 diagnostic
callee。

## 3. 旧 `TimelineTraceState` 的真实成本

该 local struct 含：

- 5 个 Boolean；
- 5 个 int；
- position/opacity/scale、A/B frame time/opacity/scale、interpolation ratio 等多组 double；
- `debugFrameASrc` 与 `debugFrameBSrc` 两个 owning `std::string`。

`traceStateFromNodeSlots` 无条件执行：

```text
copy active slot payload
debugFrameASrc = narrow(active.srcValue)
if other frame exists:
    copy other payload
    debugFrameBSrc = narrow(other.srcValue)
compute diagnostic crossfade ratio
return owning state
```

即使 `logoChainTraceEnabledForPath` 随后返回 false，两次 narrow 和 state 构造已经发生。
这不只是“日志函数调用开销”：较长 source label 会分配，allocation/conversion failure 会在
native evaluator 前改变 control flow，而且每个 node 重复发生。

helper 注释此前把它写成“updateLayers reads slots here”，容易把 diagnostic projection 误解
为 production read half。本轮注释改为：它只复用 production selection-time rule 以让日志
标签一致，不是恢复出的 native helper，trace 关闭时绝不能运行。

## 4. 修复后的控制域

phase 2 入口缓存：

```cpp
const bool traceEnabled = logoChainTraceEnabled();
std::string motionPath;
bool traceForPath = false;
if(traceEnabled) {
    motionPath = matchedMotionPath();
    traceForPath = logoChainTraceEnabledForPath(motionPath);
}
```

每个 node 使用 disengaged optional：

```cpp
std::optional<TimelineTraceState> traceState;
if(traceForPath) {
    traceState.emplace(readNodeFrameSlotsForTrace(node, currentTime));
    if(traceState->debugEvaluated) {
        emit frame-selection trace
    }
}

run production evaluator and transforms

if(traceForPath) {
    emit final trace using *traceState
}
```

默认 trace false 时：

- motion context 不被转换；
- `readNodeFrameSlotsForTrace` 不被调用；
- 两个 debug strings 不构造/赋值；
- node label/source narrow 与三个 log formats 都不求值；
- production evaluator 和所有 `continue`/done/transform 分支保持原顺序。

当 evaluator 返回 false 或 done branch 提前 `continue` 时，final trace 与修复前一样不输出；
若 trace 开启，optional 在该 iteration 离开时按普通 C++ 生命周期销毁字符串。native 异常
边界没有被 try/catch 或 cleanup guard 改写。

## 5. IDB 回写

四份 recovery IDB 已在 `Player_updateLayers_guess` 函数与 production evaluator call site
追加：

- phase 2 没有 trace-state/src-string projection；
- evaluator call 前不构造 diagnostic snapshot；
- full routine 为 0 string refs；
- Web `TimelineTraceState` 必须只存在于 opt-in sidecar control domain。

四份 IDB 已原位保存。

## 6. 验证

本轮完成：

- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` Emscripten syntax check 通过；只有
  仓库既有 `_tss` literal-operator deprecated warning；
- `cmake --build --preset "Web Debug Build"` 重编 `PlayerUpdateLayerEval.cpp`，成功链接
  `libmotionplayer.a` 与最终 `index.html`/Wasm；
- `git diff --check` 通过；输出仅有工作树既有的 LF→CRLF 转换提示；
- 源码检索确认 `matchedMotionPath()` 位于 trace 总 gate 内，
  `readNodeFrameSlotsForTrace()` 唯一 caller 位于 path-specific gate 内，final log 也只在
  同一 engaged-optional 条件下解引用 state；
- 既有 parameter-mode regression 仍通过完整 TU 编译并调用完整 `updateLayers` 合法路径；
  phase-2 production evaluator/transform code没有为本轮诊断隔离而改写。

不为 Web URL trace 开关新增 native unit-test API；默认 sidecar isolation 由 caller 控制域、
四端 0-string/direct-call 证据、完整 TU 编译和 Web 链接共同固定。
