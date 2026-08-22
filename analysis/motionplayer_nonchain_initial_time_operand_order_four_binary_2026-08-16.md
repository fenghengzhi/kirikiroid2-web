# 普通 motion 非 Chain 初始时间的操作数顺序（四参考二进制，2026-08-16）

## 结论

`Player_initNonEmoteMotion_guess` 的非 Chain 尾块不是当前源码的
`std::min(lastTime, 0.0)`。四份参考共同把 `lastTime=NaN` 写成精确 `+0.0`；当前表达式
却因 `std::min` 在比较为 false 时保留第一操作数而返回 NaN。

最能同时解释旧源码家族和两个 32 位目标操作数身份的共享源码形状是：

```cpp
clampedEvalTime = std::min(0.0, lastTime);
```

它保留负的普通值，并把正值、正零、NaN 和相等的负零映射到第一操作数 `+0.0`。两个
64 位目标把同一最小值优化成 numeric-min 指令，因而只在异号零这一点产生目标代码生成
分化。

## 四端新鲜指令证据

| 参考目标 | 函数 | Chain gate 与初始时间块 |
|---|---:|---:|
| Android ARM64 | `0x6B0A3C` | `0x6B0E6C..0x6B0E8C` |
| Android ARMv7 | `0x580C28` | `0x580DF4..0x580E16` |
| iOS ARM64 | `0x100108258` | `0x1001084EC..0x100108508` |
| iOS ARMv7 | `0x1058F8` | `0x105BB8..0x105BE8` |

Android ARM64 在 bit 1 未置位时装载 `lastTime` 与零，随后于 `0x6B0E80` 执行
`FMINNM D0,D0,D1`，于 `0x6B0E84/0x6B0E88` 依次写零 frame tick 和最小值。iOS
ARM64 的对应序列是 `0x1001084F4` 装载 `lastTime`、`0x1001084F8` 建立零、
`0x1001084FC` 执行 `FMINNM`、`0x100108500` 写初始 evaluation time。

Android ARMv7 在 `0x580DFA` 装载 `lastTime`，预置结果 `D0=+0`，于
`0x580E04` 比较 `lastTime` 与零，并只在 MI/ordered-less 时于 `0x580E0E` 把
`lastTime` 移入结果。iOS ARMv7 在 `0x105BD2..0x105BE4` 使用同样的
`VCMPE`、`IT MI`、`VMOVMI`、store 序列。因此两个 32 位目标的高层条件严格是：

```text
result = lastTime < +0.0 ? lastTime : +0.0
```

这不是 `std::min(lastTime, 0.0)`；后者的第一操作数身份会令 unordered 比较保留
`lastTime`。

## IEEE-754 边界与目标分化

四端共同结果：

| 输入 | 结果 |
|---|---|
| 负的有限值或 `-Inf` | 输入值 |
| 正的有限值或 `+Inf` | `+0.0` |
| NaN | `+0.0` |
| `+0.0` | `+0.0` |

`-0.0` 不能列成四端共同位模式。两个 ARMv7 ordered-select 因 equality 不满足 MI 而
保留预置的 `+0.0`；两个 ARM64 `FMINNM` 按 numeric-min 的 signed-zero 规则产生
`-0.0`。这是参考二进制本身的 32/64 位分化，不应通过含糊的“min”描述抹平。

本地 Web/Wasm 源码使用 `std::min(0.0, totalFrames)`，选择共享源码操作数身份及两个
ARMv7 的精确 unordered/equality 行为；分析文档保留 ARM64 后端的 signed-zero 差异。

## 源码与回归

- `cpp/plugins/motionplayer/PlayerCore.cpp`：生产尾块改为调用
  `initialNonChainEvaluationTime_guess`；helper 内固定
  `std::min(0.0, totalFrames)`，避免以后再次无意交换操作数。
- `cpp/plugins/motionplayer/Player.h`：只声明内部 helper，不把它注册为脚本 API。
- `tests/unit-tests/plugins/motionplayer-dll.cpp`：直接覆盖 NaN、`-0.0`、负值和正值，锁定
  当前共享源码/ARMv7 的第一操作数规则；ARM64 的 `-0.0` 分化只作为二进制事实记录，
  不伪装成单一 Web 运行时能够同时满足的断言。

## IDB 改进

四份 recovery IDB 的初始化器入口、Chain gate、数值最小值/ordered-select 和最终 clock
store 均补充了语义注释，并加入统一 bookmark：
`non-chain init clock operand order and signed-zero target split (2026-08-16)`。

## 验证

- 普通 Web 与 `KRKR2_WASMTIME_HEADLESS=1` 两种宏环境下，完整
  `tests/unit-tests/plugins/motionplayer-dll.cpp` 均以真实 Emscripten 参数通过
  `-fsyntax-only`；
- `Web Debug Build` 完整编译并链接 `index.html` 成功；
- `Wasmtime Headless Debug Build --target motionplayer` 编译并链接静态库成功；
- 输出只有仓库既有的 `_tss`、`nodiscard` 与 Emscripten 链接警告；
- 当前预设没有生成可直接运行该 Catch 翻译单元的 executable，因此不把语法检查描述成
  运行时测试。
