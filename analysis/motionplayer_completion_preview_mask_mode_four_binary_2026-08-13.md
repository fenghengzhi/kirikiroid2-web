# Player `completionType` / `preview` / `maskMode` 四参考二进制复原

日期：2026-08-13

## 结论

这三个 Player NCB 属性是三个独立状态，不共享存储，也不存在 setter
联动：

- `preview` 是较早的单字节 Boolean，构造默认 `false`；
- `completionType` 是原样读写的 32 位整数，构造默认 `0`；
- `maskMode` 是紧随 `completionType` 的原样读写 32 位整数，构造默认
  `0`；
- 两个整数 setter 都不校验、不夹取、不映射枚举值；
- `completionType` 由 Layer copy 路径消费，并以 `== 0` 参加直绘门控；
- `maskMode` 原值作为 alpha-mask 调用的第十参数下传，Player 不负责把
  它归一化成 `0/1`；
- `preview` 被几何、可见性、子 motion、粒子、render-item、bounds 和
  camera-constraint 多条路径反复读取。

旧 `libkrkr2.so` 注释给出的 Android arm64 偏移本身并非全错，但不能平移
到其他 ABI；尤其 iOS armv7 的 `completionType/maskMode` 是
`+732/+736`，不是按相邻旧字段推出来的 `+712/+716`。源码中原先把
`_maskMode` 声明在 `_completionType` 之前且隔着其他字段，也不符合四端
共同显示的原生相邻顺序，现已修正为 `completionType` 后接 `maskMode`。

## NCB 注册与 accessor

| 目标 | `completionType` 注册 / getter / setter | `preview` 注册 / getter / setter | `maskMode` 注册 / getter / setter |
|---|---|---|---|
| Android arm64 | `0x6D4478` / `0x6D6A04` / `0x6D6A0C` | `0x6D44F8` / `0x6D6A14` / `0x6D6A1C` | `0x6D4AA0` / `0x6D6B38` / `0x6D6B40` |
| Android armv7 | `0x598056` / `0x598E5E` / `0x598E64` | `0x598074` / `0x598E6A` / `0x598E70` | `0x5981DC` / `0x598FB6` / `0x598FBC` |
| iOS arm64 | `0x100124738` / `0x100125550` / `0x100125558` | `0x100124764` / `0x100125560` / `0x100125568` | `0x100124974` / `0x100125670` / `0x100125678` |
| iOS armv7 | `0x123A4E` / `0x124756` / `0x12475C` | `0x123A78` / `0x124762` / `0x124768` | `0x123C70` / `0x12489A` / `0x1248A0` |

所有 `completionType` 和 `maskMode` getter/setter 都分别是一条 32 位
load/store 加 return。`preview` 则是一条 byte load/store 加 return；
Android arm64 的 typed Boolean setter 额外显式执行 `value & 1`，其他 ABI
由 NCB typed adapter 已经提供 Boolean 值，底层仍仅保存一个字节。

## 对象布局与构造默认值

| 目标 | `preview` | `completionType` | `maskMode` | 构造写入 |
|---|---:|---:|---:|---|
| Android arm64 | `+1092` | `+1144` | `+1148` | `0 / 0 / 0` |
| Android armv7 | `+744` | `+800` | `+804` | `0 / 0 / 0` |
| iOS arm64 | `+980` | `+1032` | `+1036` | `0 / 0 / 0` |
| iOS armv7 | `+680` | `+732` | `+736` | `0 / 0 / 0` |

构造证据：

| 目标 | `preview=false` | `completionType=0` | `maskMode=0` |
|---|---|---|---|
| Android arm64 | `0x6CC484` | `0x6CC4DC` | `0x6CC554` |
| Android armv7 | `0x5937B2` | `0x5937DC` | `0x59389A` |
| iOS arm64 | `0x10011EE0C` | `0x10011EE4C` | `0x10011EEEC` |
| iOS armv7 | `0x11D7D0` | `0x11D83C` | `0x11D96C` |

arm64 构造有时把相邻零值合并成更宽 store；这不改变 accessor 所证明的
逻辑字段宽度。

## 内部数据流和边界

### `completionType`

四端 render-command 构造和 `renderToCanvas` 都把该值符号扩展为 TJS
整数并放进 Layer affine/mesh/bezier copy 参数。代表性读取：

| 目标 | command 构造 | canvas 执行 |
|---|---|---|
| Android arm64 | `0x6C2684`, `0x6C2C60`, `0x6C2E48`, `0x6C301C` | `0x6C5204`, `0x6C53F0`, `0x6C55C8` |
| Android armv7 | `0x58CA44`, `0x58CD22`, `0x58CEB8`, `0x58D034` | `0x58E81E`, `0x58EA76`, `0x58EC72`, `0x58EE4E` |
| iOS arm64 | `0x100116A80`, `0x100116E30`, `0x100116FC8`, `0x1001171BC` | `0x100118E04`, `0x100118F90`, `0x100119168` |
| iOS armv7 | `0x1145B0`, `0x114990`, `0x114AF6`, `0x114CDE` | `0x117036`, `0x117322`, `0x117504`, `0x117758` |

canvas 直绘路径先测试 `completionType == 0`；非零值迫使 buffered 路径，
但之后仍把完整原值传给 copy 操作。负数和未知正数不会在 Player 层被
改写。

### `maskMode`

代表性 alpha-mask 调用前读取：Android arm64 `0x6C36EC` / `0x6C5728`，
Android armv7 `0x58D734` / `0x58EF78`，iOS arm64 `0x100117870` /
`0x100119314`，iOS armv7 `0x1152EE` / `0x1178E2`。读取值作为
`Motion_doAlphaMaskOperation` 的 mask-mode 参数原样下传；Player accessor
接受任意 32 位值。下游目前已知的 `0/1` 分支含义不能倒推成 Player setter
的合法值约束。

### `preview`

完整字段访问扫描（排除栈帧同立即数）在四端各得到约 20 个内部读取，
覆盖：timeline 初始化/推进/回退、camera constraint、vertex computation、
visibility、type-3 child motion、particle emitter/system、prepared render item、
bounds 和 view-parameter 计算。它不是只在加载时采样的配置：内部代码在
各阶段直接读取 live Player 字节。

## 本轮落地

- 修正 `Player.h` 中三属性的四参考说明；
- 把源码字段顺序改为原生共同顺序：`_completionType` 紧接
  `_maskMode`；
- 为 raw Int32/Boolean 默认值、整数极值、负值、属性独立性和 NCB 类型
  增加单元测试；
- 四个 IDB 共命名 24 个 accessor，并在注册、构造、主要 consumer 处写入
  注释后保存。

## 验证

- Web Debug 完整编译/链接成功，随后复查为 `ninja: no work to do`；
- Wasmtime Headless Debug 完整编译/链接成功，随后复查为
  `ninja: no work to do`；
- 复用 Web Debug 的真实 Emscripten defines/includes/ABI 参数，并加入
  `out/syntax-check` Catch2/test config 与 `out/syntax-stubs`，对完整
  `tests/unit-tests/plugins/motionplayer-dll.cpp` 执行 `-fsyntax-only` 成功；
  唯一诊断为仓库既有的 `_tss` literal-operator 弃用 warning；
- `git diff --check` 无 whitespace error；仅报告工作区既有的 LF/CRLF
  转换提示。
