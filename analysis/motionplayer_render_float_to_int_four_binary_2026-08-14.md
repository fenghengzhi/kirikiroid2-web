# MotionPlayer 渲染浮点坐标到有符号整数边界（四参考二进制，2026-08-14）

## 1. 结论

`Player` 的 build/submit 渲染链并不是依赖宿主 C++ 对越界
`float -> int` 转换的未定义行为。四份当前参考二进制都把相关转换落实为
ARM 指令中自带的“向零取整 + 饱和”语义：

- 有限且可表示：去掉小数部分，向零取整；
- `value >= 2^31`（包括 `+Inf`）：`INT32_MAX`；
- `value <= -2^31`（包括 `-Inf`）：`INT32_MIN`；
- 任意 NaN：`0`；
- `+0.0` 与 `-0.0`：均为 `0`；
- 该取整方向由指令编码确定，不读取动态 FP rounding mode。

Android/iOS ARM64 使用 `FCVTZS Wd, Sn`；Android ARMv7 使用标量 VFP
`VCVT.S32.F32 Sd, Sm`；iOS ARMv7 既有标量/双 lane 形式，也把 paint-box
四元组合并为 `VCVT.S32.F32 Q8, Q8`。NEON 形式逐 lane 使用同一数值边界，
因此它只是编译器向量化差异，不是第二套 MotionPlayer 算法。

源码侧以 `floatToSignedIntTowardZeroSaturated_guess` 显式表达这个边界，避免
WebAssembly/宿主 C++ 在 NaN 或越界输入处进入未定义行为。`_guess` 表示它是
为了可移植复现而抽出的源码级 helper；参考二进制没有可证明的同名、非内联函数。

## 2. 四份函数定位

| 阶段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| build render commands | `0x6C2208` | `0x58C7C4` | `0x1001167BC` | `0x114118` |
| submit/render to canvas | `0x6C4820` | `0x58E2CC` | `0x1001186E0` | `0x11653C` |

这些地址是本次 recovery IDB 中的函数首址；旧注释中的 AArch64
`0x6C4E28` / `0x6C7440` 属于先前 `libkrkr2.so` 基线，不能继续当作当前四参考
二进制的地址证据。

## 3. 指令证据与数据流

### 3.1 build：group 子层 alpha-mask 参数

四个输入为：

1. `child.clip.left - groupUnion.left`；
2. `child.clip.top - groupUnion.top`；
3. `child.clip.right - child.clip.left`；
4. `child.clip.bottom - child.clip.top`。

转换后作为 alpha-mask 的 `dstX / dstY / width / height` 整数实参。对应指令：

| 目标 | 指令位置 |
|---|---|
| Android arm64 | `0x6C36FC`, `0x6C3700`, `0x6C370C`, `0x6C3710` — `FCVTZS W,S` |
| Android armv7 | `0x58D756`, `0x58D75A`, `0x58D76A`, `0x58D76E` — `VCVT.S32.F32 S,S` |
| iOS arm64 | `0x100117848`, `0x100117850`, `0x100117860`, `0x100117868` — `FCVTZS W,S` |
| iOS armv7 | `0x1152D8`, `0x1152E0`, `0x115310`, `0x115316` — `VCVT.S32.F32 D,D` |

### 3.2 submit：paint-box 累积到 draw region

每个通过 raw skip/opacity 门的 item 都把 `paintBox[0..3]` 转为
`tTVPRect`，随后并入 Player 的持久 draw region。转换发生在 source descriptor、
source object 和 direct/buffered 分支构造之前：

| 目标 | 指令位置 |
|---|---|
| Android arm64 | `0x6C4A1C`, `0x6C4A20`, `0x6C4A34`, `0x6C4A38` |
| Android armv7 | `0x58E4BE`, `0x58E4C2`, `0x58E4CA`, `0x58E4CE` |
| iOS arm64 | `0x1001188B8`, `0x1001188BC`, `0x1001188C4`, `0x1001188E0` |
| iOS armv7 | `0x116C66` — 单条四 lane `VCVT.S32.F32 Q8,Q8` |

### 3.3 submit：buffered ancestor alpha-mask

buffered 分支沿 `parentItem` 链行走时，再把祖先 clip 相对 buffer 原点的
`dstX/dstY` 以及祖先 clip 的 `width/height` 转成四个整数：

| 目标 | 指令位置 |
|---|---|
| Android arm64 | `0x6C5738`, `0x6C573C`, `0x6C5748`, `0x6C574C` |
| Android armv7 | `0x58EFA2`, `0x58EFA6`, `0x58EFB6`, `0x58EFBA` |
| iOS arm64 | `0x1001192F0`, `0x1001192F8`, `0x100119308`, `0x100119310` |
| iOS armv7 | `0x1178D2`, `0x1178DA`, `0x117908`, `0x11790E` |

转换结果与 `threshold=64`、Player mask mode、ancestor stencil flags 一起进入
alpha-mask 操作。不存在预先 clamp 到画布、非负或 `INT32` 范围的额外 MotionPlayer
分支；数值钳位完全来自转换指令本身。

## 4. 源码结构与非原生诊断字段

恢复源码让所有上述消费者共用显式 helper。`integralClipRect` 也改为使用同一
helper，因为它会处理由同一 native float clip 派生的 Web 诊断字段
`PreparedRenderItem::dirtyRect` / `builtRect`。这不表示参考二进制额外保存了一组
同构整数矩形；这些字段属于 Web 观测层。真正的原生可观察转换站点仍只有第 3 节
列出的指令组及同函数中与其他算法相关的独立转换。

测试覆盖：正负零、正负分数、最大可表示的正 `float` 整数、`+2^31`、
`-2^31`、负向越界、正负无穷和 NaN。这样既固定正常坐标的向零取整，也固定过去
只写成“AArch64 FCVTZS”而遗漏 ARMv7/iOS 编译形态的边界行为。

## 5. 验证

- 四份 recovery IDB 的两个函数入口与上述转换指令均已加入语义注释，并保存到
  原 recovery 数据库；
- `Web Debug Build` 增量重编受影响的 motionplayer 翻译单元，成功生成静态库并
  完成最终 `index.html` / Wasm 链接；
- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` 使用 Web Debug 的真实
  Emscripten defines/includes/ABI 参数及既有 Catch2/test config 执行
  `-fsyntax-only` 成功；唯一诊断是仓库既有 `_tss` literal-operator 弃用 warning；
- `git diff --check` 通过；工作树只报告既有 LF/CRLF 转换提示。
