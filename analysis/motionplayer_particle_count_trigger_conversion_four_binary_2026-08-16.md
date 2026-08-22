# MotionPlayer particle count-trigger signed-int32 转换（四参考二进制，2026-08-16）

## 结论

type-4 particle-system 的 count trigger 在 node flags 整字节非零时无条件消费一次共享
RNG，先以 double 完成区间插值，再转为 signed int32 `emitCount`：

```text
r = Player_random_guess()
sample = prtFmin + (prtF - prtFmin) * r
emitCount = signed_int32_toward_zero_saturated(sample)
```

四端使用和参数归一化相同的 `FCVTZS W,D` / `VCVT.S32.F64` 指令 profile：

- 有限且在 int32 范围内：向零截断；
- NaN：0；
- `value >= 2^31`：`INT32_MAX`；
- `value <= -2^31`：`INT32_MIN`。

本地旧实现直接 `static_cast<int>(sample)`，对 NaN、infinity 和超界有限值属于 C++
未定义行为。现在由显式 `_guess` helper 表达目标指令结果，且只在确认位于有限 int32
范围后执行 cast。

## 四端指令窗口

| 参考 | particle-system helper | 插值与转换 |
| --- | ---: | ---: |
| Android ARM64 | `0x6BC4BC` | `0x6BCB34 BL random`，`FSUB/FMUL/FADD`，`0x6BCB44 FCVTZS W20,D8` |
| Android ARMv7 | `0x588A48` | `0x5899F8 VSUB`，`VMUL/VADD`，`0x589A08 VCVT.S32.F64 S0,D10` |
| iOS ARM64 | `0x100111D08` | `0x100111FE4 BL random`，`FSUB/FMUL/FADD`，`0x100111FF4 FCVTZS W26,D8` |
| iOS ARMv7 | `0x10F51C` | `0x10F7C8 VSUB`，`VMUL/VADD`，`0x10F7D8 VCVT.S32.F64 S0,D10` |

两个 A64 和两个 ARMv7 的 operand/dataflow 一致：`prtFmin` 是加法初值，
`prtF-prtFmin` 先乘 RNG，再加回 minimum；没有 float 中间窄化、round-to-nearest、
unsigned conversion、范围检查或 conversion exception branch。

## 与 control-flow 的组合边界

- flags byte 为零时不调用 RNG、不做算术/转换，`emitCount` 保持零。
- flags 非零时，即使 `prtFmin == prtF`，仍先消费 RNG；退化区间不消除采样。
- sample 为 NaN 时转换为零，随后 `emitCount <= 0` 跳到 existing-child physics step，
  不进入 child creation。
- 正溢出得到 `INT32_MAX`，属于正 emit count；当前 pass 仍最多创建一个 child，余数
  由既有 decrement/worker 控制流消费，并不会变成一个内层 multi-spawn loop。
- 负溢出得到 `INT32_MIN`，立即走非正 count 路径。
- 转换不会修改 RNG 已经发生的消费；即使转换结果为零，随机序列仍前进一次。

## 端口、测试与 IDB

本轮修改：

- `PlayerUpdateLayersInternal.h` 增加
  `particleEmitCountFromDouble_guess`，显式实现 signed-int32 向零/饱和 profile；
- `PlayerUpdateParticles.cpp` 的 count-trigger producer 使用该 helper；
- `motionplayer-dll.cpp` 覆盖 NaN、正负 infinity、`±2^31` 和正负有限小数；
- 修正旧分析中“未恢复 invalid/out-of-range”的保守结论。

四个 recovery IDB 的转换指令旧注释已替换为确认后的边界说明，增加统一书签并保存。
