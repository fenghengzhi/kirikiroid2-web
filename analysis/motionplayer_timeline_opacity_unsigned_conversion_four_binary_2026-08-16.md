# MotionPlayer timeline opacity 无符号转换边界（四参考，2026-08-16）

## 1. 范围与结论

本轮只以 `reference/binaries/` 的四个当前参考二进制为依据，复核
`Player_evaluateTimeline_guess` crossfade 分支中 opacity 的完整数值链。旧分析已经锁定
half-away-from-zero 取整，但把最后一步写成了笼统的“最终整数”；fresh 四端指令证明这一步不是
signed `int` 转换，而是 **double → uint32 的 toward-zero saturation**：

```text
slot opacity raw 32 bits
    -> uint32 -> double
    -> endpoint equality shortcut or scalar interpolation
    -> ordered value < 0 ? ceil(value - 0.5) : floor(value + 0.5)
    -> FCVTZU / VCVT.U32.F64
    -> store the resulting 32-bit word
```

因此 conversion 的共同结果是：

| 取整后的 double | 写入的 opacity word |
|---|---:|
| NaN | `0` |
| `-Inf` 或负有限数 | `0` |
| `-0.0`、`+0.0` | `0` |
| `0 < x < 2^32` | 向零截断后的 `uint32` |
| `+Inf` 或 `x >= 2^32` | `UINT32_MAX` |

half-away 取整本身发生在 conversion 之前。例如 `0.5 -> 1`、`3.5 -> 4`；负结果随后又被
unsigned conversion 归零。没有 0..255 clamp。当前 Web 结构把同一字段暴露为 signed `int`，
所以 `UINT32_MAX` 必须按原始全一位模式写回，不能再做一次有符号数值转换。

## 2. 四端 fresh 指令链

### 2.1 Android arm64-v8a

求值器入口为 `0x696EC4`。两端在 `0x6973E4`、`0x6973E8` 用 `UCVTF` 从 32-bit
unsigned 值提升到 double；不等时的插值位于 `0x6973F4..0x697404`。最终链为：

```asm
0x697414  FCMP    D0, #0.0
0x697418  FADD    D1, D0, #-0.5
0x69741C  FADD    D0, D0, #0.5
0x697420  FRINTP  D1, D1
0x697424  FRINTM  D0, D0
0x697428  FCSEL   D0, D1, D0, MI
0x69742C  FCVTZU  W9, D0
0x697434  STR     W9, [X19,#...]
```

`MI` 只在 ordered negative 时选择 `ceil(value-0.5)`；unordered/NaN 的 N flag 不置位，
因此选择 `floor(value+0.5)`，NaN 保持到 `FCVTZU` 后得到零。

### 2.2 Android armeabi-v7a

求值器入口为 `0x573158`。端点 conversion 是
`VCVT.F64.U32 D10,S0` `0x5735E8` 与
`VCVT.F64.U32 D9,S2` `0x5735EC`，插值位于 `0x573608..0x573618`。取整和 conversion：

```asm
0x57361C..0x573628  ceil(D9 - 0.5)
0x57362C..0x57363C  floor(D9 + 0.5)
0x573640             VCMPE.F64 D9, #0.0
0x573644             VMRS      APSR_nzcv, FPSCR
0x573654             IT        MI
0x573656             VMOVMI.F64 D0, D1
0x57365A             VCVT.U32.F64 S0, D0
0x573664             VSTR      S0, [R1]
```

这里也只有 ordered negative 才覆盖默认的 floor 结果；unordered 时 `MI` 不成立。

### 2.3 iOS arm64

求值器入口为 `0x1000F6C34`。端点在 `0x1000F70C4`、`0x1000F70D0` 用 `UCVTF`
提升，不等端点的插值位于 `0x1000F70F0..0x1000F7100`。尾部为：

```asm
0x1000F7104  FCMP    D9, #0.0
0x1000F7108  B.PL    floor_path
0x1000F710C  FMOV    D0, #-0.5
0x1000F7110  FADD    D0, D9, D0
0x1000F7114  FRINTP  D0, D0
0x1000F7118  B       convert
0x1000F711C  FMOV    D0, #0.5
0x1000F7120  FADD    D0, D9, D0
0x1000F7124  FRINTM  D0, D0
0x1000F7128  FCVTZU  W8, D0
0x1000F712C  STR     W8, [X19,#...]
```

unordered 比较的 N flag 为零，所以 `B.PL` 同样把 NaN 送入 floor path。

### 2.4 iOS armv7

求值器入口为 `0xF3894`。端点 conversion 位于 `0xF3D88`、`0xF3D8C`，插值位于
`0xF3DB0..0xF3DC0`。尾部为：

```asm
0xF3DC4  VCMPE.F64 D9, #0.0
0xF3DC8  VMRS      APSR_nzcv, FPSCR
0xF3DCC  BPL       floor_path
0xF3DCE..0xF3DDA   ceil(D9 - 0.5)
0xF3DE0..0xF3DEC   floor(D9 + 0.5)
0xF3DF8  VCVT.U32.F64 S0, D16
0xF3DFC  VSTR      S0, [R0]
```

分支与 iOS arm64 相同：negative 走 ceil，nonnegative 或 unordered 走 floor，最后始终按
unsigned 32-bit saturation 转换。

## 3. 可达边界与旧端口偏差

端点本身来自 32-bit unsigned word，因而总是有限且非负；但 evaluator 不夹取 ratio。
外插可生成负值或超过 `2^32` 的正值，极端有限 ratio 还可让运算溢出。可观察例子包括：

- `a=0, b=1, ratio=-1` 得到负 opacity，native conversion 写零；
- `a=0, b=UINT32_MAX, ratio=2` 超出范围，native conversion 写 `UINT32_MAX`；
- unordered/invalid 浮点结果经 nonnegative/floor 选择后保持 NaN，并由 conversion 写零。

旧 Web 端口在 `std::ceil/std::floor` 后直接 `static_cast<int>`。负有限值会保留为负数；NaN、
Infinity 和 signed-int32 范围外转换则进入 C++ 未定义行为；它也无法表达四端明确存在的
`UINT32_MAX` saturation 边界。

## 4. 便携恢复与回归

本轮新增 `timelineOpacityWordFromDouble_guess(double)`：先保持四端的 ordered-negative
half-away 选择，再复用已经由 timeline `ti` 四端闭环的
`doubleToUnsignedIntTowardZeroSaturated_guess`。求值器把返回的 `uint32_t` 以 `memcpy`
复制到当前 signed opacity 字段，明确保留低 32 位而不依赖 implementation-defined
unsigned-to-signed 数值转换。

单元测试覆盖：NaN、正负 Infinity、负有限数、正负半数边界、普通 half-away 值、
`UINT32_MAX` 饱和，以及两条完整 evaluator 外插路径（negative 归零与 all-ones word 写回）。
四个 recovery IDB 的最终 conversion 与 ordered-negative 选择站点均已加入语义注释和书签并保存。
