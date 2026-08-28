# Player play / progress raw bridge（四参考二进制，2026-08-26）

## 1. endpoint 与函数形状

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `play` raw callback | `0x6CFFE8` | `0x59565C` | `0x1001212C0` | `0x120050` |
| `progress` raw callback | `0x6CFE78` | `0x595598` | `0x100121204` | `0x11FFB4` |
| progress bridge | inline in callback | `0x595570` | `0x1001211C0` | `0x11FF88` |

八个 callback endpoint 与三个独立 bridge 均已 fresh decompile + disassemble。
全部在对应 IDB 中命名、写入函数注释和 bookmark，并保存四份 IDB。Android
arm64 把 progress bridge 内联进 raw callback；另外三端保留相同的独立单基本块
helper。这是编译形状差异，不是源语义差异。

两个脚本成员都由 legacy `tTJSNativeClassMethodCallback` 注册，签名是：

```cpp
tjs_error callback(Variant *result, int argc,
                   Variant **argv, Dispatch *objthis);
```

它们与 `setVariable` 的 native-instance raw template 不同：`play` / `progress`
必须保留原始 `objthis`，因为后续脚本回调以它为 receiver。

## 2. legacy method object 与 callback 分层

外层 `tTJSNativeClassMethod::FuncCall` 和本报告中的 callback 是两层边界，不能把
两者合并描述：

1. 非空 `membername` 交还普通 Dispatch 处理；
2. `objthis == nullptr` 直接返回 `TJS_E_NATIVECLASSCRASH (-1008)`，不清 result；
3. receiver 非空时先 `result->Clear()`；
4. 再调用 raw callback；外层不做 argc gate，也不解析 Player。

raw callback 自身完全不读取/写入 `result`。因此直接调用 callback 时 result 保持
原值；通过真实脚本方法对象调用时，receiver 非空会先被外层清成 Void。错误
receiver 也会先清 result，然后 callback 的手工 native-instance resolution 返回
`-1008`。本地新增测试分别锁定两层行为。

## 3. 共同 receiver / argc 边界

两个 callback 均先通过 `objthis.NativeInstanceSupport(GETINSTANCE, PlayerClassId)`
取得 adaptor，再读 adaptor 中的 Player 指针。以下任一条件都返回 `-1008`：

- `objthis == nullptr`；
- NativeInstanceSupport status 失败；
- adaptor/closure output 为空；
- adaptor 内的 Player 指针为空。

只有解析成功后才检查 argc：`play` 要求 `argc >= 2`，`progress` 要求
`argc >= 1`，不足返回 `TJS_E_BADPARAMCOUNT (-1004)`；多余参数不访问。四端
没有额外的 `argv == nullptr` / `argv[i] == nullptr` 防护：argc 充足但数组或元素
指针畸形时继续原生解引用，属于 native crash boundary，不应被端口改成参数错误。

## 4. play callback

四端共同伪代码：

```cpp
Player *self = resolvePlayer(objthis);
if (!self) return TJS_E_NATIVECLASSCRASH;
if (argc < 2) return TJS_E_BADPARAMCOUNT;

self->currentDispatch = objthis;       // raw, no AddRef
int flags = argv[1]->AsInteger();      // first conversion
ttstr label = *argv[0];                // second conversion, by-value owner
self->play(flags, label);
self->currentDispatch = nullptr;       // normal return only
return TJS_S_OK;
```

关键顺序不是“等价可交换”的：

- raw dispatch 在两项转换之前安装；
- flags 必须先转 Integer，label 后构造 ttstr；
- flags 转换异常时尚无 label owner，raw slot 已残留；
- label 构造或 play 深层异常时，已构造的局部 label 按 ABI unwind 销毁，但 raw
  slot 不清；
- raw slot 不 AddRef，因此异常残留可能成为悬空指针；这是四端真实边界；
- `playImpl` 的加载/提交/初始化状态机不在本切片声称闭合，继续由专门播放状态机
  切片处理。

`currentDispatch` 字段坐标为 Android/iOS 64 位 `+0x10`，32 位 `+0x8`。

## 5. progress callback 与 bridge

callback 共同伪代码：

```cpp
Player *self = resolvePlayer(objthis);
if (!self) return TJS_E_NATIVECLASSCRASH;
if (argc < 1) return TJS_E_BADPARAMCOUNT;

double deltaMs = argv[0]->AsReal();
progressBridge(self, objthis, (deltaMs * 60.0) / 1000.0);
return TJS_S_OK;
```

bridge 共同伪代码：

```cpp
self->currentDispatch = objthis;             // raw, no AddRef
self->frameProgress(frameDelta);
self->updateLayers();
self->calcBounds();
self->dispatchPendingEvents(self->currentDispatch); // reload live field
self->currentDispatch = nullptr;             // normal return only
```

与 play 的重要差异是 `AsReal()` 在安装 raw dispatch 之前发生：转换异常必须保留
进入 callback 前 slot 的旧值，而不能写成本次 objthis。进入 bridge 后，四个 phase
中的任一异常都会留下本次 objthis。事件派发参数从 Player 字段重新读取，并非一直
使用 helper 的入参快照，因此 re-entrant 修改 slot 会影响最终 dispatch receiver。
bridge 和事件 helper 都不消费 pending-event vector。

四端指令都明确执行 binary64 `delta * 60.0`，随后 `/ 1000.0`；不是先折叠
`60/1000` 再乘。该差异在极小有限值上可达一 ulp，本地用
`0x1.d2f346baa9455p-858` 锁定期望结果 `0x1.c045b48a3c19ap-862`。

## 6. 本地逐项对照与修改

| 联合证据 | 修改前 | 当前实现 |
|---|---|---|
| resolve 失败为 `-1008` | 返回 `TJS_E_INVALIDOBJECT (-1006)` | 改为 `TJS_E_NATIVECLASSCRASH` |
| 仅 argc 下界检查 | play 额外检查 `param/param[0]/param[1]` | 删除非参考 null guards |
| progress 先 AsReal 后装 raw slot | 先写 slot，再转换 | callback 转换后调用 bridge |
| progress 只有四 phase | wrapper 插入 trace scope、路径构造、栈回溯、日志与 snapshot stderr | 删除全部非参考 sidecar |
| bridge 统一顺序 | wrapper 手工复制 bridge 且夹杂 side effect | 复用 `progressFrames_guess` |
| `*60` 后 `/1000` | 乘预折叠 `0.06` 常量 | 恢复 multiply-then-divide |
| callback/result 与 method/result 分层 | 测试只覆盖 callback 直调 | 新增真实 legacy method object result/receiver 测试 |

对应实现位于 `PlayerTimeline.cpp:271`、`PlayerFrameProgress.cpp:1292` 和
`PlayerFrameProgress.cpp:1302`；签名与边界回归位于
`tests/unit-tests/plugins/motionplayer-dll.cpp:90`、`:23640` 起。

## 7. 验证状态与剩余范围

本切片记为 `IMPLEMENTED`：四端 callback/bridge fresh 证据、共同伪代码、本地逐项
对照和静态/边界测试均已补齐；`git diff --check` 通过。当前机器缺少项目要求的
CMake/Ninja/Emscripten 正式工具链，因此尚未执行正式 unit/Web build。

本切片自身只负责 raw script bridge；它原先列出的深层依赖现已由后续独立切片全部
闭合：`playMotionImpl_guess` 和两类 initializer 见
`motionplayer_player_playback_state_machine_four_binary_2026-08-27.md`，`frameProgress`、
正反向四流、node/variable 双槽、事件 helper 与异常 ledger 见
`motionplayer_player_frame_progress_events_four_binary_2026-08-27.md`，`updateLayers` 的
dispatcher/phase1/phase2/十个 phase3 见 MP-C15、MP-C20 至 MP-C29，`calcBounds` 见
`motionplayer_player_calc_bounds_four_binary_2026-08-27.md`。因此 coverage 中 raw play 与
raw progress 的旧 remaining-gap 已完成回填；当前只剩正式工具链验证和最终全局分母审计。
