# MotionPlayer `Player.progress` diagnostic isolation（四参考二进制，2026-08-14）

## 1. 结论

四个当前参考二进制的脚本 `Motion.Player.progress(deltaMs)` wrapper 在 native instance
与 argc 检查后，只做以下事情：

```text
dt = argv[0].AsReal()
dtFrames = dt * 60.0 / 1000.0
progress bridge / equivalent inlined sequence
return TJS_S_OK
```

它不读取 Player 的 motion context，不构造 path/string，不调用 logger，不格式化诊断
expected/actual，不采集 TJS stack，也没有 snapshot 输出。本地 Web sidecar 虽然对
`shortTJSStackTrace()` 已有 path gate，但此前仍然：

- 每次调用无条件执行 `matchedMotionPath()`；
- 每次 `frameProgress` 后无条件求值两个 `fmt::format(...)` 实参，再进入内部才门控的
  `logoChainTraceCheck`；
- 多次重复 lower-case/path filter。

因此 trace/snapshot 默认关闭时也有 native 不存在的 Variant-to-text、UTF narrow、format
allocation 和 `bad_alloc`/转换异常点。本轮把 path materialization、eager formatting、stack
trace、日志与 snapshot 全部放到显式 sidecar gate 后；native/default wrapper 只保留四端
共同数据流。

## 2. fresh 四端映射

| 目标 | TJS wrapper | native bridge | `AsReal` | bridge/内联 core handoff |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6CFE78` | `0x6CFE34` | 从 `0x6CFEF4` 开始的 inline type switch | `0x6CFFB8`，直接 `frameProgress` |
| Android armv7 | `0x595598` | `0x595570` | `0x5955F4` | `0x595614`，调用 bridge |
| iOS arm64 | `0x100121204` | `0x1001211C0` | `0x100121270` | `0x100121294`，调用 bridge |
| iOS armv7 | `0x11FFB4` | `0x11FF88` | `0x12000A` | `0x12002A`，调用 bridge |

Android arm64 编译器把 bridge 的五段 body 内联进 script wrapper，即在
`0x6CFF98` 写 raw `currentDispatch` 后依次直接调用 frame/update/bounds/event，并清字段；
独立 bridge 仍供其他调用点使用。其余三端保留 wrapper→bridge call。这个 inlining 差异
不改变共同源码职责。

## 3. wrapper 与 bridge 的直接证据

四份 fresh Hex-Rays 共同显示 wrapper：

1. `objthis == null` 或 NCB native instance 解析失败返回 invalid object；
2. native Player 指针有效后才检查 `numparams < 1`，失败返回 `-1004`；
3. 第一个 Variant 立即 `AsReal`；
4. 对结果乘 `60.0 / 1000.0`；
5. 调 bridge，或执行完全相同的 inlined bridge body；
6. 返回 0，不写 result。

bridge 仍为：

```text
player.currentDispatch = objthis        // raw borrow, no AddRef
player.frameProgress(dtFrames)
player.updateLayers()
player.calcBoundsRecursive()
player.dispatchPendingEvents(player.currentDispatch)
player.currentDispatch = null
```

wrapper/bridge direct calls 没有任何 `TJSObjectToString`、Variant string conversion、
`ttstr`、UTF narrow、`fmt`、logger、stack trace、`fprintf` 或 motion-path helper；这些函数
也没有对应诊断字符串引用。故 sidecar 的 argument evaluation 不能在默认路径上发生。

## 4. 修复前的隐蔽 eager evaluation

`logoChainTraceLogf` 是 template，内部先调用 `logoChainTraceEnabledForPath`，之后才执行
`fmt::format`，所以普通标量实参本身不会在 trace 关闭时构造日志字符串。但
`logoChainTraceCheck` 的 `expected` 与 `actual` 已经是 `const std::string &`：

```cpp
logoChainTraceCheck(
    motionPath, ...,
    fmt::format("speedMul...", expectedValue),
    fmt::format("deltaTime...", actualValue),
    ok, ...);
```

C++ 必须在进入 helper 前求值这两个 `fmt::format`。因此 helper 内部再早的 enable check
也无法避免两次 format/allocation。无条件 `matchedMotionPath()` 同理：后续
`EnabledForPath` 返回 false 不会撤销已发生的 Variant string conversion。

## 5. 修复后的 sidecar 边界

wrapper 先缓存两个无参数开关：

```cpp
const bool traceEnabled = logoChainTraceEnabled();
const bool snapshotEnabled = logoSnapshotMarkEnabled();

std::string motionPath;
if(traceEnabled || snapshotEnabled) {
    motionPath = matchedMotionPath();
}

const bool traceForPath =
    traceEnabled && logoChainTraceEnabledForPath(motionPath);
const bool snapshotForPath =
    snapshotEnabled && logoSnapshotMarkEnabledForPath(motionPath);
```

之后：

- `shortTJSStackTrace()` 只在 `traceForPath` 为 true 时求值；
- 两个 `fmt::format` 与 `logoChainTraceCheck` 一起处于同一 `traceForPath` block；
- update/exit log 也复用已经计算好的 path gate；
- 两个 `fprintf` snapshot block 复用 `snapshotForPath`；
- trace/snapshot 都关闭时，不读取 `_findMotionContextVariant`，不做 path lower-case/filter，
  不格式化诊断字符串。

`MotionTraceProgressScope` 属于另一套 Wasmtime/Web trace sidecar；其构造/析构首先检查
自己的 `traceRequested()`，本轮没有把 logo-path 语义混入其中。

显式开启诊断仍可能分配、格式化和输出，这是 opt-in instrumentation 的刻意行为；这些
字段不进入 Player/node/parameter 的 native 容器，也不改变 bridge 阶段顺序。native 阶段
若抛出，原生 raw currentDispatch 不执行尾部清零的异常边界仍保持不变。

## 6. IDB 回写

四份 recovery IDB 已在 wrapper 函数、`AsReal` 位点和 bridge/inline handoff 位点写入：

- native wrapper 无 motion path/string/format/logging 数据流；
- `AsReal` 后直接进行 60/1000 scale 与 native handoff；
- Android arm64 为 bridge body inline，其余三端为 out-of-line bridge call；
- Web diagnostics 必须在构造其参数前完成 gate。

四份 IDB 已原位保存。

## 7. 验证

本轮完成：

- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` Emscripten syntax check 通过；只有
  仓库既有 `_tss` literal-operator deprecated warning；
- `cmake --build --preset "Web Debug Build"` 重编 `PlayerFrameProgress.cpp`，成功链接
  `libmotionplayer.a` 与最终 `index.html`/Wasm；
- `git diff --check` 通过；输出仅有工作树既有的 LF→CRLF 转换提示；
- 源码检索确认 `matchedMotionPath()` 位于 trace/snapshot 总 gate 内，两个
  `fmt::format` 位于 path-specific trace block 内，snapshot 输出复用缓存 gate。

本轮不把诊断 URL 开关写成单元测试输入：它由 Emscripten `window.location`/全局 JS 标记
驱动，而非 native Player API。对默认路径的关键保证由源码结构、完整 TU 编译和四端
wrapper direct-call 负证据共同固定。
