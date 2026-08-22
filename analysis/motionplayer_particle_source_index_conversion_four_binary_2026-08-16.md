# MotionPlayer particle source-index conversion（四参考，2026-08-16）

## 1. 结论

本轮只以 `reference/binaries/` 的四个当前参考二进制为证据，补齐
`Player_updateParticleSystems_guess` 选择 particle motion source 时的最后一个数值边界。
四端共同执行：

```text
signed sourceCount -> double
random() * double(sourceCount)
double -> signed int32, toward zero with saturation
numeric property getter(sourceIndex)
```

最后一步不是未约束的 C++ `static_cast<int>`。它在四端分别是 `FCVTZS W,D` 或
`VCVT.S32.F64`，共同数值结果为：

| product | sourceIndex |
|---|---:|
| NaN | `0` |
| `+Inf` 或 `product >= 2^31` | `INT32_MAX` |
| `-Inf` 或 `product <= -2^31` | `INT32_MIN` |
| 有限且在范围内 | 向零截断 |

这个 conversion 不做 `[0, sourceCount)` clamp。`random()==1.0` 仍得到
`sourceIndex==sourceCount`；负 count 或异常 random 仍可产生负索引或饱和端点。

## 2. 四端 fresh 指令链

### 2.1 Android arm64-v8a

函数入口 `0x6BC4BC`，source-list count 返回后：

```asm
0x6BCC58  MOV     W23, W0
0x6BCC5C  CBZ     W23, zero_count_loop
0x6BCC64  BL      Player_random_guess
0x6BCC68  SCVTF   D1, W23
0x6BCC6C  FMUL    D0, D0, D1
0x6BCC70  FCVTZS  W1, D0
0x6BCC84  BL      numeric_property_getter
```

`SCVTF` 锁定 sourceCount 的 signed 32-bit 解释；`FCVTZS W1,D0` 锁定最终
signed-int32 saturation profile。

### 2.2 Android armeabi-v7a

函数入口 `0x588A48`，非零 count 分支跳到：

```asm
0x588ADC  BL            Player_random_guess
0x588AE0  VMOV          S2, R4
0x588AE8  VCVT.F64.S32  D1, S2
0x588AEC  VMUL.F64      D0, D0, D1
0x588AF0  VCVT.S32.F64  S0, D0
0x588AF4  VMOV          R2, S0
0x588B02  BL            numeric_property_getter
```

### 2.3 iOS arm64

函数入口 `0x100111D08`：

```asm
0x100112100  MOV     X25, X0
0x100112104  CBZ     W25, zero_count_loop
0x10011210C  BL      Player_random_guess
0x100112110  SCVTF   D1, W25
0x100112114  FMUL    D0, D1, D0
0x100112118  FCVTZS  W1, D0
0x10011212C  BL      numeric_property_getter
```

虽然 count 暂存在 X25，消费者明确读取 W25，signed promotion 与最终 conversion 都是
32-bit。

### 2.4 iOS armv7

函数入口 `0x10F51C`：

```asm
0x10F916  BL            Player_random_guess
0x10F91C  VMOV          S0, R2
0x10F926  VCVT.F64.S32  D17, S0
0x10F92A  VMUL.F64      D16, D17, D16
0x10F92E  VCVT.S32.F64  S0, D16
0x10F934  VMOV          R2, S0
0x10F942  BL            numeric_property_getter
```

## 3. 可达性与旧端口偏差

`Player_random_guess` 调用脚本/ResourceManager 的 `random` member，并把返回 Variant
直接转换为 real；调用失败状态也不会提供安全的 0.0 fallback。因此它不是一个在 C++ 类型系统里
可假定为 `[0,1)` 的纯内部 RNG，NaN、Infinity、负数和任意大有限数都可进入乘法。

旧端口直接执行：

```cpp
static_cast<int>(random() * static_cast<double>(sourceCount))
```

有限范围外、NaN 或 Infinity 会进入 C++ floating-to-integer 未定义行为。Web/Wasm 编译器可以
trap 或产生与四端不同的值，且这种偏差发生在 numeric getter 之前，会改变被请求的脚本索引以及
随后异常清理路径。

## 4. 便携恢复、回归与 IDB

`PlayerUpdateLayersInternal.h` 现将四端共同的 signed-int32 conversion 收敛到
`signedInt32FromDoubleTowardZeroSaturated_guess`，并由 count-trigger 与 source-index 两个语义 wrapper
分别调用。source selection 仍在乘法之后转换，没有新增 index clamp。

回归覆盖 NaN、正负 Infinity、正负 `2^31`、正负普通分数；既有完整 spawn 回归继续固定
`random()==1.0` 时请求 `sourceCount` 本身。四份 recovery IDB 的 conversion 指令均已加入
语义注释与书签并保存。
