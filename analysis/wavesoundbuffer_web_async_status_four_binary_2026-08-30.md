# WaveSoundBuffer Web 异步播放状态四参考联合分析（2026-08-30）

> 后续修订：本文记录的是 `320623a5` 的“提前发布状态”修复及其回归结果。当前实现已按
> 方案二改为 Promise-aware 主循环，并恢复 TJS `open()` / `play()` 的同步返回语义；
> 详见 `wavesoundbuffer_jspi_sync_semantics_four_binary_2026-08-30.md`。

## 现象与脚本调用链

游戏 `KRkr高压_千恋万花.zip` 的年龄提示流程在 `main/custom.ks` 中依次执行三次：

```text
[sysvoice name=att1 chara=@ wait]
[wait time=1000]
[sysvoice name=att2 chara=@ wait]
[wait time=1000]
[sysvoice name=att3 chara=@ wait]
[wait time=1000]
```

`main/sysvoice.tjs` 的 `sysvoice` KAG handler 只在下面的同步检查成立时进入等待：

```tjs
PlaySystemVoice(elm.name, elm.chara);
if (elm.wait && SystemVoice.status == "play") {
    kag.conductor.wait(%["sysvoice_stop" => ..., "click" => ...]);
    return -2;
}
```

原版会等待每段语音结束。当前 Web 版中，Playwright 冷启动 Web Audio 时间线测得：

- 第一批音频源在页面时间约 `24756 ms` 启动；
- 约 `25769 ms` 停止旧源，`25785 ms` 启动下一批，相隔约 `1.01 s`；
- 约 `26835 ms` 再次停止旧源，`26847 ms` 启动下一批，相隔约 `1.05 s`；
- Web Audio 以约 `125 ms` 的 buffer source 分块排队，切换时仍有已排队分块短暂存活。

两个约一秒的间隔与脚本显式的两个 `[wait time=1000]` 一致，证明三个
`[sysvoice ... wait]` 都没有进入等待。过早执行下一次 `play()` 又会停止前一个语音，
因此年龄提示很快结束，并出现语音开头重叠/重复的听感。

`SystemVoiceTrackBase.start()` 的调用顺序是同步的 `open(file); play();`，handler 紧接着
读取 `SystemVoice.status`。问题边界因此落在 `WaveSoundBuffer::Play()` 返回脚本时的状态。

## 本轮 IDB 状态与宽字符串锚点

四份 `.i64` 均由本轮新的 IDALib 会话打开并完成 Hex-Rays 初始化。此前重建的 Android
armv7 `.i64` 本轮可正常打开和反编译。普通字符串索引找不到 `WaveSoundBuffer`，补做
UTF-8、UTF-16LE、UTF-32LE 原始字节搜索后，四端均只在 UTF-16LE 命中；边界读取确认了
完整字符串与 NUL 终止符。用于定位 NCB 注册的锚点为：

| 参考目标 | UTF-16LE `WaveSoundBuffer` | NCB 注册函数 |
|---|---:|---:|
| Android arm64-v8a | `0x14C7B08` | `0x9605C8` |
| Android armeabi-v7a | `0x6FA648`（函数内字面量池） | `0x6FA160` |
| iOS arm64 | `0x10195F768` | `0x100198F0C` |
| iOS armv7 | `0x1751ACC` | `0x1986DC` |

## 四参考新定位

| 参考目标 | NCB `open` wrapper | `Open` | NCB `play` wrapper | `Play` | `StartPlay` | `SetStatus` | NCB `stop` wrapper | `Stop` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Android arm64-v8a | `0x960DF8` | `0x972034` | `0x960EDC` | `0x971DE4` | `0x971A28` | `0x95B960` | `0x960F5C` | `0x9713B0` |
| Android armeabi-v7a | `0x6FA89C` | `0x705A10` | `0x6FA938` | `0x70586C` | `0x7055FC` | `0x6F7A0C` | `0x6FA998` | `0x705168` |
| iOS arm64 | `0x1001996EC` | `0x1002AE21C` | `0x100199794` | `0x1002AE034` | `0x1002ADD5C` | `0x100185A20` | `0x1001997F4` | `0x1002AD84C` |
| iOS armv7 | `0x19909C` | `0x2B3D30` | `0x199190` | `0x2B3AAC` | `0x2B3688` | `0x183150` | `0x1991D0` | `0x2B30E4` |

## 共同伪代码与状态契约

四端 `Open` 都在解码器、filter chain、格式和可选 `.sli` 初始化全部成功以后同步执行：

```cpp
SetStatus(ssStop); // 1
```

四端 `Play` 的对象布局和后端细节不同，但共同控制流一致：

```cpp
if (Decoder != nullptr && !BufferPlaying) {
    StopPlay();
    EnsurePrimaryBufferPlay();
    RaiseDecodeThreadPriorityIfNeeded();
    lock(BufferCS, L2BufferCS);
    StartPlay();
    SetStatus(ssPlay); // 2，返回 TJS 前同步可见
}
```

四端 `Stop` 也共同在停止实际播放/解码线程后执行：

```cpp
if (Status != ssUnload)
    SetStatus(ssStop); // 1
rewind_to_zero_if_loaded();
```

因此 `Play()` 成功接受播放请求后，`status == "play"` 在函数返回脚本前成立，是四个原版
平台一致的脚本可观察契约。它不是“音频设备已经输出第一帧”的异步通知。

## 平台差异

- Android 与 iOS 的音频 buffer 实现、对象字段偏移、线程/锁 helper 地址不同；
- armv7 使用 32 位对象布局和 Thumb/SJLJ 异常处理，arm64 使用 64 位布局和 AArch64 异常处理；
- Android arm64 在确保主 buffer 运行处有额外的全局初始化分支；
- 上述差异不改变 `Open -> stop`、`Play -> play`、`Stop -> stop` 的同步状态顺序。

## 本地 Web 异步实现比较

`cpp/core/sound/win32/WaveImpl.cpp` 的 Emscripten 路径把文件打开和首次四块填充改成
continuation：

1. `Play()` 遇到 `AsyncOpenPending` 时只设置 `AsyncPlayPending = true` 就返回；
2. 解码器已经打开时，`Play()` 调 `StartPlayAsync()` 后也不设置状态；
3. 只有四块初始 buffer 全部填好、`FinishStartPlayAsync()` 执行时才
   `SetStatus(ssPlay)`；
4. `FinishOpenAsync()` 成功时无条件先 `SetStatus(ssStop)`，再在有排队播放时调用
   `Play()`。

所以 `open(); play(); status` 的同步脚本序列读到 `unload`/`stop`，而不是四参考共同的
`play`。如果只简单地提前发布 `play`，第 4 点又会产生一次人为的 `play -> stop`，
`SystemVoiceTrackBase.onStatusChanged()` 会把它当成自然播放结束并触发 `sysvoice_stop`。

## 修复决策

Web continuation 只应推迟 I/O 和首次填充，不应推迟原版在 API 返回前已经发布的逻辑播放
状态。最小修复为：

1. `Play()` 接受异步打开期间的排队播放时，立即 `SetStatus(ssPlay)`；
2. 解码器已就绪并启动 `StartPlayAsync()` 后，也立即 `SetStatus(ssPlay)`；
3. `FinishOpenAsync()` 有排队播放时保持 `play`，清除 pending 标志后直接继续 `Play()`；
   只有没有排队播放时才发布 `ssStop`；
4. 首次异步填充失败时发布 `ssStop`，确保已经等待 `play -> stop` 的 TJS conductor 不会
   永久挂起；
5. `FinishStartPlayAsync()` 末尾保留幂等的 `SetStatus(ssPlay)`，作为实际启动完成后的状态
   收敛，不额外产生事件。

打开失败仍走 `Clear()`，按现有路径从已发布的 `play` 进入 `stop`/`unload`，能释放等待并
保留失败语义；显式 `Stop()` 也会清除所有 pending 标志并发布 `stop`。原生非 Emscripten
路径不改。

## 修复后 Web 回归

Web Debug 增量构建成功。使用同一份 ZIP、持久浏览器 profile 和导航前注入的 Web Audio
探针重新冷启动，得到三段年龄提示语音：

| 段 | 首个 buffer source | 最后一个 buffer source | 分块数 |
|---|---:|---:|---:|
| `att1` | `8583 ms` | `48887 ms`（末块约 `106.7 ms`） | 197 |
| `att2` | `50615 ms` | `65211 ms`（末块约 `115.7 ms`） | 72 |
| `att3` | `68559 ms` | `71694 ms`（末块约 `91.3 ms`） | 11 |

从 `8583 ms` 到 `72000 ms` 的三段语音窗口内没有任何
`AudioBufferSourceNode.stop()`，证明后续语音没有再强制截断前一段。第一段末块自然结束于
约 `49315 ms`，第二段在 `50615 ms` 开始，约 `1.30 s` 的间隔对应脚本显式 1 秒等待加
少量 archive/autopath 开销。第二段与第三段之间还有异步 archive 打开和堆扩容开销，
但控制流保持顺序。

页面截图在约 24 秒和 42 秒仍显示年龄提示；旧构建约 30 秒前已经离开该页面。第三段
自然结束后，页面继续完成资源加载，并在约 118 秒的观察点正常显示完整标题菜单，证明
等待能够由真实 `play -> stop` 释放而非永久卡住。控制台没有与本修复相关的异常；仅有
既有的 Fontconfig 默认配置提示和 `favicon.ico` 404。
