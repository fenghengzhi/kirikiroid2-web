# WaveSoundBuffer JSPI 同步语义与 Promise-aware 主循环（2026-08-30）

## 目标

本轮用“方案二”替代 Web 端提前发布 `status` 的兼容方案：

1. 如果一个 Cocos 主循环 tick 因 VLFS/音频 continuation 发生 JSPI 暂停，Emscripten
   scheduler 不再请求下一帧；原 tick 的 Promise 完成后才请求新的 RAF，暂停期间的帧直接
   丢弃；
2. Web 底层仍使用 Promise-backed VLFS 和异步解码/首次填充，但 TJS
   `WaveSoundBuffer.open()` 与 `play()` 恢复四个原版平台一致的同步返回边界；
3. JSPI 暂停点不得持有 `BufferCS`、`L2BufferCS`、`L2BufferRemainCS` 或
   `AsyncDecodeCS`。

## 本轮四参考新证据

四份 `.i64` 均在本轮重新打开并完成 Hex-Rays 反编译；此前重建的 Android armv7
数据库可以正常打开。定位结果如下：

| 参考目标 | `Open` | `Play` | `StartPlay` |
|---|---:|---:|---:|
| Android arm64-v8a | `0x972034` | `0x971DE4` | `0x971A28` |
| Android armeabi-v7a | `0x705A10` | `0x70586C` | `0x7055FC` |
| iOS arm64 | `0x1002AE21C` | `0x1002AE034` | `0x1002ADD5C` |
| iOS armv7 | `0x2B3D30` | `0x2B3AAC` | `0x2B3688` |

四端共同控制流为：

```cpp
Open(storage) {
    Clear();
    Decoder = CreateWaveDecoder(storage);
    LoopManager = new WaveLoopManager(Decoder);
    RebuildFilterChain();
    InputFormat = FilterOutput->GetFormat();
    read_and_parse_optional_sli();
    SetStatus(ssStop);       // TJS 返回之前
}

Play() {
    if (Decoder && !BufferPlaying) {
        StopPlay();
        EnsurePrimaryBufferPlay();
        lock(BufferCS, L2BufferCS);
        StartPlay();         // 首次 L2 解码、四次 FillBuffer、设备 Play
        SetStatus(ssPlay);   // StartPlay 完成以后、TJS 返回之前
    }
}
```

对象偏移、ABI、锁 helper、设备状态字段和 label 调度细节在 armv7/arm64、Android/iOS
之间不同，但 `Open -> ssStop` 和 `StartPlay -> ssPlay -> TJS return` 的顺序没有平台
差异。当前非 Emscripten C++ 分支与该共同伪代码一致。

## Web 实现边界

VLFS 的文件内容只能异步取得，因此保留下列 continuation：

- `TVPCreateWaveDecoderAsync()`：异步取得完整音频文件，再在专用 FIFO worker 中探测
  decoder；
- `GetSizeAsync()` / `ReadBufferAsync()`：异步读取可选 `.sli`；
- `FillL2BufferAsync()`：在音频 continuation worker 解码，短暂进入
  `AsyncDecodeCS` / `L2BufferCS` 后提交结果；
- `ContinueStartPlayAsync()`：按原版的一次 L2 fill 对应一次 L1 `FillBuffer` 顺序完成
  四次首次填充。

新增的 wait/complete 桥以 heap context 地址作为 token：completion 早于 wait 时记录
`finished`，wait 早于 completion 时保存 Promise resolver，所以不存在“完成信号跑在等待
注册之前”造成的永久挂起。

### `open()`

`Open()` 在启动异步 decoder 后调用 JSPI wait。`FinishOpenAsync()` 不再提前调用
`SetStatus()`、`Play()` 或向日志吞掉异常，只提交 `Active/Error` 并唤醒原调用栈。
原 `Open()` 调用栈恢复以后：

- 成功：执行 `SetStatus(ssStop)` 后返回 TJS；
- 失败：执行 `Clear()`，再用保存的 `exception_ptr` 在原调用栈重新抛出；
- 取消：不发布伪造状态，直接返回。

额外的 owner 引用由栈上 guard 持有到恢复后的状态提交/异常清理结束，避免异步期间对象
失效，也保证异常路径释放引用。

### `play()`

`StartPlayAsync()` 只在短临界区内创建 sound buffer、reset filter chain 并标记
`BufferPlaying`，随后释放锁并启动四段 continuation。设备真正 `Play()`、label 重排和
decode thread 恢复完成后，completion context 才被标记为 `Started` 并唤醒原
`Play()` 调用栈。恢复以后：

- 成功：`SetStatus(ssPlay)`，然后返回 TJS；
- 首次填充未成立：`SetStatus(ssStop)`；
- 异步阶段抛错：在原 `Play()` 调用栈重新抛出；
- 被 `Stop()`/`Clear()` 取消：不覆盖取消方已经提交的状态。

旧的 `AsyncPlayPending` 路径已移除。方案二把正常 TJS 入口串行化以后，脚本不可能在
尚未返回的 `open()` 之后进入同一条 `play()` 语句；浏览器事件的异常重入也不再提前
发布 `ssPlay`。

## 不跨 JSPI 暂停持锁

两个 JSPI wait 的位置均可由局部控制流验证：

| 暂停点 | 暂停前最后一个可能持锁的操作 | wait 时状态 |
|---|---|---|
| `Open()` | `Clear()` 内部的 stop/decoder 清理已完整返回 | 无 sound critical section |
| `Play()` | `StartPlayAsync()` 的 `BufferCS + L2BufferCS` scope 已退出 | 无 sound critical section |

异步 worker 和主线程 completion 仍会进入短临界区，但都在 signal Promise 之前退出。
因此，原 TJS 栈暂停时 completion 可以重入 Wasm 完成 I/O/解码，却不会等待一个由暂停
栈持有的互斥锁。

## Promise-aware RAF scheduler

Emscripten 原始 `MainLoop.runIter()` 会丢弃 callback 返回值，并在调用后立即执行
`MainLoop.scheduler()`。即使 tick 已由 `WebAssembly.promising()` 包装，原实现也不会
等待 Promise。

`jspi_jsc_mainloop_fix.js` 现在保存每个实际 engine tick 返回的 Promise，并把
`MainLoop.scheduler` 变为一个稳定的 gate：

```text
RAF -> wrappedTick -> Promise pending
                    -> scheduler gate 不请求下一帧
Promise fulfilled  -> 使用当前底层 scheduler 请求一个新 RAF
Promise rejected   -> handleException，不再排帧
```

gate 通过 `MainLoop.scheduler` 的 getter/setter 保存最新底层 target，所以
`emscripten_set_main_loop_timing()`、pause/resume 后替换 RAF/timeout scheduler 时不会
绕过 Promise 等待。FPS limiter 在 stall 后只消费一个 due frame，并对累计时间取余，
不会补跑暂停期间的旧帧。Cocos 的 `s_inTick` 继续作为 RAF 之外异常入口的防御性保护。

## 验证

- `cmake --preset "Web Debug Config"` 配置成功；overlay `cocos2dx` 因补丁输入变化重新
  构建成功；
- `cmake --build out/web/debug` 全量及后续增量构建/链接成功，生成
  `index.html`、`index.js`、`index.wasm`、`vlfs.js`、`assets.zip`；
- 生成的 `index.js` 同时包含 Promise-aware scheduler、JSPI async wait import 和
  completion signal import；
- `tests/web/jspi_mainloop_scheduler.test.mjs` 验证：
  1. tick Promise pending 时重复调用 scheduler 不产生下一帧；
  2. Promise fulfilled 后只排一个新帧；
  3. timing scheduler 被替换后仍受 gate 约束；
  4. Promise rejected 时走 `handleException` 且不再排帧。

使用用户指定的 `playwright-cli`、独立 headless Chromium 会话和完整
`KRkr高压_千恋万花.zip` 对最终增量构建做了冷启动回归。导航前探针同时记录 RAF 请求、
tick Promise、Web Audio source 和过滤后的控制台日志：

- 共观测 73 个 tick Promise 窗口；`rafRequestsWhilePending == 0`；
- 最大 VLFS 暂停约 `3707.22 ms`，窗口内没有 RAF 请求，Promise fulfilled 后只增加
  1 次下一帧调度；其余大于 10ms 的窗口也全部是 `rafDelta == 1`；
- 年龄提示三段语音窗口依次为：

| 段 | source start 调用窗口 | 分块数 | 强制 `stop()` |
|---|---:|---:|---:|
| `att1` | `5.908–35.419 s` | 146 | 0 |
| `att2` | `36.806–76.594 s` | 195 | 0 |
| `att3` | `78.288–88.094 s` | 50 | 0 |

相邻语音窗口间隔约 `1.39 s` / `1.69 s`，包含脚本显式的 1 秒 wait 和少量 archive/
调度开销。之后厂商流程约 `91.575 s` 启动短音频，标题 BGM 约 `104.045 s` 开始；
约 `131 s` 时画面正常显示完整标题菜单。整个会话记录 542 个 audio source start，
`AudioBufferSourceNode.stop()` 为 0，证明三段语音没有被后续 `play()` 截断或重启。
最终 `pending == false`、`waiting == false`。过滤后的唯一 error 是既有 Fontconfig 默认
配置缺失提示，没有 JSPI、WaveSoundBuffer、TJS 或 Wasm 异常。

## 与上一版修复的关系

`wavesoundbuffer_web_async_status_four_binary_2026-08-30.md` 记录的“异步操作继续、TJS
立即返回并提前发布 `ssPlay`”是上一版兼容修复。它解决了年龄提示不等待，但改变了
`open()`/`play()` 的返回边界，并可能让底层尚未完成时脚本继续执行。本轮方案二取代该
策略：状态值仍保持四参考顺序，但现在由原始 TJS 调用栈在真正完成底层初始化后同步
提交。
