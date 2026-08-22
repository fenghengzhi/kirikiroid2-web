# MotionPlayer 参数归一化的 IEEE 与转换边界（四参考二进制，2026-08-16）

## 结论

参数记录的归一化并不是“先算 range，再以 `range == 0` 归零”，也不是任意 C++
`double -> int` cast。四个参考二进制共同实现了如下数据流：

```text
if rangeBegin == rangeEnd or division <= 0.0:
    entry.value = +0.0
    return

value = rawValue
if discretization:
    value = double(signed_int32_toward_zero_saturated(rawValue))

lo = rangeEnd < rangeBegin ? rangeEnd : rangeBegin
hi = rangeBegin < rangeEnd ? rangeEnd : rangeBegin
if value < lo:
    value = lo
else if hi < value:
    value = hi

entry.value = division * (value - rangeBegin) /
              (rangeEnd - rangeBegin)
```

这使三类过去未锁定的边界可观察：

- `rangeBegin`、`rangeEnd` 是相同符号的 infinity 时，端点直接相等，结果为
  `+0.0`。若先做减法，`Inf - Inf` 会错误地变成 NaN 并绕过归零路径。
- discretization 的转换目标是 signed int32，向零舍入并饱和：NaN -> 0，
  `value >= 2^31` -> `INT32_MAX`，`value <= -2^31` -> `INT32_MIN`。
- min/max/clamp 全是有序比较。raw NaN 逐级保留；在 `[+0,+1]` 内输入
  `-0.0` 时，最终结果仍是 `-0.0`。division 的 NaN 同样不会被 `<= 0` 拒绝。

## 四端函数与调用结构

| 参考 | 归一化位置 | ramp 调用结构 |
| --- | --- | --- |
| Android ARM64 | append 内联 `0x6AED58..0x6AEDC4`；binder 内联 `0x6C1E58..0x6C1EB8`、`0x6C1F30..0x6C1F94`、`0x6C2020..0x6C2084` | binder 为三份同构内联尾部 |
| Android ARM32 | `normalizeParameterValue_guess` `0x57FC38` | `Player_applyParameterRamps_guess` `0x585058` 调用 helper |
| iOS ARM64 | `normalizeParameterValue_guess` `0x100106F78` | `Player_applyParameterRamps_guess` `0x10010DDE0` 调用 helper |
| iOS ARM32 | `normalizeParameterValue_guess` `0x10446C` | `Player_applyParameterRamps_guess` `0x10B708` 调用 helper |

这说明 A64 Android 的内联不是不同算法。四端共享源级归一化语义，只是优化器在
Android A64 的 append/binder 调用点把函数体展开了。

## 入口 guard：直接端点相等

四端都在任何减法之前直接比较 `rangeBegin` 与 `rangeEnd`：

- Android ARM64：`0x6AED60 FCMP D1,D2`，`0x6AED64 B.EQ`；binder 的一份
  对应序列为 `0x6C202C FCMP D0,D1`、`0x6C2030 B.EQ`。
- Android ARM32：`0x57FC44 VCMP.F64 D0,D1`，随后 `0x57FC4C BEQ`。
- iOS ARM64：`0x100106F7C FCMP D0,D1`，随后 `0x100106F80 B.EQ`。
- iOS ARM32：`0x104478 VCMP.F64 D0,D1`，随后相等分支。

division gate 则是有序的非正比较：A64 使用 `FCMP division,#0` 后 `B.LS`，A32
使用 `VCMPE division,#0` 后 `BLS`。浮点 unordered 时这些分支不成立，所以
division=NaN 会继续进入乘除式；没有额外 `isfinite` 或 `isnan` 保护。

## discretization：signed-int32 饱和转换

指令族在四端一致：

- Android ARM64：`0x6AED78 FCVTZS W9,D0`，`0x6AED7C SCVTF D4,W9`。
- Android ARM32：`0x57FC6C VCVT.S32.F64 S8,D3`，`0x57FC74 VCVT.F64.S32 D4,S8`。
- iOS ARM64：`0x100106F90 FCVTZS W8,D3`，`0x100106F94 SCVTF D3,W8`。
- iOS ARM32：`0x10449C VCVT.S32.F64 S8,D3`，`0x1044A4 VCVT.F64.S32 D4,S8`。

因此端口使用显式 helper 表达该指令 profile，只在已证明落入 int32 有限范围之后
执行 C++ cast，消除了 `static_cast<int>(NaN/Inf/out-of-range)` 的未定义行为。

## min/max/clamp 的 operand identity

Android A64 append 的连续选择最清楚：

```text
FCMP  end, begin
FCSEL lo, end, begin, MI
FCMP  begin, end
FCSEL hi, end, begin, MI

FCMP  value, lo
FCSEL tmp, lo, value, MI
FCMP  tmp, hi
FCSEL ...                         // value > hi 时取 hi
FCCMP value, lo, ..., LE
FCSEL value, selected, value, MI
```

Android ARM32 `0x57FC5C..0x57FCCA`、iOS ARM64
`0x100106FA4..0x100106FC8`、iOS ARM32 `0x10448C..0x1044FA` 给出相同的有序
比较/选择结果。源代码没有改写成会改变 NaN 或 signed-zero operand identity 的
`fmin`/`fmax`，而是显式写出对应 ternary 与两段 clamp 分支。

## 端口修正与回归

本次修正：

- `cpp/plugins/motionplayer/PlayerVariable.cpp`
  - 入口改为直接比较两个 endpoint；
  - 增加 signed-int32 向零饱和转换 helper；
  - 显式实现四端 operand order 的 lo/hi/clamp；
  - 公开 `_guess` 内部归一化 helper，供差分边界测试直接调用。
- `cpp/plugins/motionplayer/PlayerInternal.h`
  - 声明 stripped-name 不可恢复的 `_guess` helper。
- `tests/unit-tests/plugins/motionplayer-dll.cpp`
  - 覆盖同号 infinity endpoint、NaN/Inf/int32 溢出 discretization、向零截断、
    raw NaN、`-0.0`、上下 infinity clamp 和 NaN division。

IDB 中在四端归一化入口、转换指令和 min/max/clamp 选择处补充了交叉验证注释与
书签；四个 recovery IDB 均已保存。构建验证结果记录在本轮工作日志中。
