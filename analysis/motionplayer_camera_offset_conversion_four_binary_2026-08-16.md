# MotionPlayer CameraNode offset conversion（四参考，2026-08-16）

## 1. 范围与共同语义

本轮只以 `reference/binaries/` 的四个当前参考二进制为证据，补齐
`Player_updateCameraNode_guess` 将二维 affine 结果发布为 camera offset 时的 invalid/overflow
边界。既有分析已经恢复了 float delta 窄化、double affine、`+0.5` 和最后的 float store；
fresh 指令证明中间整数化是明确的 signed-int32 saturation：

```text
weighted = matrixPrimary * double(deltaX)
         + matrixSecondary * double(deltaY)
adjusted = weighted + 0.5
word = signedInt32TowardZeroSaturated(adjusted)
cameraOffset = float(word)
```

两个 offset 分量都执行相同链。边界表为：

| `adjusted` | 中间 int32 | 最终 float |
|---|---:|---:|
| NaN | `0` | `+0.0f` |
| `+Inf` 或 `>= 2^31` | `INT32_MAX` | `float(INT32_MAX)`（通常为 `2147483648.0f`） |
| `-Inf` 或 `<= -2^31` | `INT32_MIN` | `-2147483648.0f` |
| 有限且在范围内 | 向零截断 | 该 int32 的 float 舍入结果 |

`+0.5` 在 conversion 前发生，所以普通负值仍保留既有非对称行为；这不是 `round`。

## 2. 四端 fresh 指令链

### 2.1 Android arm64-v8a

函数入口 `0x6BAE08`。矩阵乘加结束后：

```asm
0x6BAFBC  FMOV    D3, #0.5
0x6BAFC8  FADD    D1, D2, D3
0x6BAFCC  FADD    D0, D0, D3
0x6BAFD0  FCVTZS  W10, D1
0x6BAFD4  SCVTF   S1, W10
0x6BAFD8  FCVTZS  W10, D0
0x6BAFDC  SCVTF   S0, W10
0x6BAFE0  STP     S1, S0, [X19,#...]
```

`FCVTZS W,D` 将结果饱和到 signed 32-bit；随后的 `SCVTF S,W` 是 signed-int32 → float，
不是直接 double → float。

### 2.2 Android armeabi-v7a

函数入口 `0x587748`：

```asm
0x587862  VADD.F64      D2, D3, D2
0x587866  VADD.F64      D0, D0, D1
0x58786A  VMOV.F64      D3, #0.5
0x58786E  VADD.F64      D1, D2, D3
0x587872  VADD.F64      D0, D0, D3
0x587876  VCVT.S32.F64  S2, D1
0x58787A  VCVT.S32.F64  S0, D0
0x58787E  VCVT.F32.S32  S2, S2
0x587882  VCVT.F32.S32  S0, S0
```

### 2.3 iOS arm64

函数入口 `0x1001108C4`：

```asm
0x100110A18  FMOV    D1, #0.5
0x100110A1C  FADD    D2, D2, D1
0x100110A20  FCVTZS  W9, D2
0x100110A24  SCVTF   S2, W9
0x100110A28  STR     S2, [X19,#...]
0x100110A2C  FADD    D0, D0, D1
0x100110A30  FCVTZS  W9, D0
0x100110A34  SCVTF   S0, W9
0x100110A38  STR     S0, [X19,#...]
```

### 2.4 iOS armv7

函数入口 `0x10E048`：

```asm
0x10E15C  VADD.F64      D17, D18, D17
0x10E160  VADD.F64      D16, D16, D19
0x10E164  VMOV.F64      D18, #0.5
0x10E168  VADD.F64      D17, D17, D18
0x10E16C  VADD.F64      D16, D16, D18
0x10E170  VCVT.S32.F64  S0, D17
0x10E174  VCVT.S32.F64  S2, D16
0x10E178  VCVT.F32.S32  D0, D0
0x10E17C  VCVT.F32.S32  D1, D1
```

四端都先经过 32-bit signed integer，中间步骤不可化简成 `static_cast<float>(adjusted)`。

## 3. 可达性与旧端口偏差

delta 来自节点 vertex output 与 Z factor 的组合，先窄化为 float；矩阵来自 root/draw-affine
owner。任一上游值可携带 NaN/Infinity，有限大矩阵乘法也可溢出。因此 invalid/overflow 并非
只能由损坏寄存器产生，而是可沿已恢复的数据流到达 conversion。

旧端口用 `static_cast<float>(static_cast<int>(adjusted))`。NaN、Infinity 或 signed-int32
范围外 double 转 `int` 是 C++ 未定义行为；Wasm lowering 可能 trap。它也没有表达四端的
NaN→0 与正负饱和端点。

## 4. 便携恢复、回归与 IDB

`quantizeCameraNodeOffset_guess` 现在调用 update-layer 共用的
`signedInt32FromDoubleTowardZeroSaturated_guess`，再显式转 float。粒子 count/source-index
的语义 wrapper 同样复用这个 conversion core，但各生产者仍保留自己的乘加与控制流。

回归在既有 `+1.6/-1.6` 非对称量化上增加 NaN 与正负 Infinity，并比较最终 float 化的
`INT32_MAX/INT32_MIN`。四份 recovery IDB 的两个 conversion 站点均已加入语义注释；每端
第一个 conversion 添加书签，四份数据库已保存。
