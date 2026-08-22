# MotionPlayer `getCommandList` Bezier `division` conversion（四参考二进制，2026-08-14）

## 1. 结论

`Player::getCommandList` 在 `meshType <= 1` 的 `bezierPatch.division` 路径中执行
下面的数据流：

```text
PreparedRenderItem.commandPatchDivision : signed int32
        -> exact double conversion
        * Player.meshDivisionRatio : raw double
        = scaledDivision : double
        -> signed int64, round toward zero
        -> keep conversion only when scaledDivision is ordered and < 50
           otherwise select signed int64 50
        -> TJS Integer property "division"
```

这里有三个容易被伪代码掩盖的边界：

1. 四端都是 **signed 64-bit** 转换。两个 ARMv7 目标虽然用两个 32 位寄存器
   返回结果，但并没有先窄化到 `int32`。
2. 转换发生在浮点比较之前；64 位目标直接执行 `FCVTZS X,D`，32 位目标先
   调用外部 `double -> signed long long` ABI helper。
3. 比较后的机器选择不是 C++ `scaled >= 50 ? 50 : converted` 在 NaN 上的
   语义。AArch64 的 `MI` 和 AArch32 的 `PL` 都使 converted 只在有序 `< 50`
   时存活，因此任意 NaN 最终也发布 Integer `50`。

旧 Web 源码对 `< 50` 的 NaN/越界值直接执行
`static_cast<tjs_int64>(scaledDivision)`，属于宿主 C++ 未定义行为，而且 NaN
通常不会稳定得到参考二进制实际写出的 `50`。本次以
`serializeBezierPatchDivision_guess` 显式恢复可移植边界。

## 2. 四端函数与字段

| 目标 | `Player_getCommandList_guess` | `commandPatchDivision` | Player ratio |
|---|---:|---:|---:|
| Android arm64 | `0x6D0E2C` | item `+0x170`, signed 32-bit | `Player+0x498` |
| Android armv7 | `0x595FF0` | item `+0x128`, signed 32-bit | `Player+0x340` |
| iOS arm64 | `0x100121EB0` | item `+0x170`, signed 32-bit | `Player+0x428` |
| iOS armv7 | `0x120CF8` | item `+0x128`, signed 32-bit | `Player+0x2FC` |

ratio 的 receiver 身份、三个公开 wrapper 的转发链和 raw setter 行为已经在
[`motionplayer_mesh_division_ratio_four_binary_2026-08-13.md`](motionplayer_mesh_division_ratio_four_binary_2026-08-13.md)
闭合。本记录只审计该 raw double 到 command Dictionary 的最后一个消费者。
item 字段自身并非 raw node 值；其前一阶段 unsigned-input/signed-int32 生成规则在
[`motionplayer_prepared_bezier_division_conversion_four_binary_2026-08-14.md`](motionplayer_prepared_bezier_division_conversion_four_binary_2026-08-14.md)
闭合。两个阶段各自再次加载 ratio，所以 command query 会对 prepared integer 再乘
一次 ratio。

## 3. 指令级数据流

### 3.1 Android arm64

```asm
0x6D1718  LDR     S0, [X23,#0x170]    ; signed patch division bits
0x6D1720  LDR     D1, [X8,#0x498]     ; Player.meshDivisionRatio
0x6D1724  SSHLL   V0.2D, V0.2S, #0
0x6D1728  SCVTF   D0, D0
0x6D1730  FMUL    D0, D1, D0
0x6D1734  FCVTZS  X8, D0
0x6D1738  FCMP    D0, D8             ; D8 = 50.0
0x6D173C  CSEL    X8, X8, X9, MI     ; X9 = 50
0x6D1744  STR     W9, [...type]       ; TJS Integer tag 4
0x6D1748  STR     X8, [...payload]
```

`SSHLL` 对低 signed 32-bit lane 做符号扩展，随后 `SCVTF` 得到精确 double。
`FCVTZS X,D` 是 signed 64-bit、向零舍入转换。`MI` 只在 `FCMP` 的 less-than
结果设置 N 时成立；equal、greater 和 unordered 均选择常量 50。

### 3.2 Android armv7

```asm
0x596508  MOVS        R4, #0
0x596552  VLDR        S0, [R8,#0x128]
0x596556  VCVT.F64.S32 D0, S0
0x59655E  VLDR        D1, [R0,#0x340]
0x596562  VMUL.F64    D9, D1, D0
0x596566  VMOV        R0, R1, D9
0x59656A  BLX         __aeabi_d2lz
0x59656E  VCMPE.F64   D9, D8          ; D8 = 50.0
0x596572  VMRS        APSR_nzcv, FPSCR
0x596576  IT          PL
0x596578  MOVPL       R1, R4          ; high word = 0
0x59657A  STR         R1, [...high]
0x59657C  IT          PL
0x59657E  MOVPL       R0, #50         ; low word = 50
0x596580  STR         R0, [...low]
0x596598  BL          <Integer property setter>
```

`__aeabi_d2lz` 的返回值是 `{R0 low, R1 high}` 的 signed `long long`。浮点
unordered 比较把 N 清零，所以 `PL` 与 greater/equal 一样覆盖两个返回 word；
NaN 不会把 helper 的任意转换结果泄漏到 Dictionary。

### 3.3 iOS arm64

```asm
0x1001222C0  LDR     W8, [X20,#0x170]
0x1001222C4  SCVTF   D0, W8
0x1001222D0  LDR     D1, [X8,#0x428]
0x1001222D4  FMUL    D0, D0, D1
0x1001222D8  FCVTZS  X8, D0
0x1001222DC  FCMP    D0, D8           ; D8 = 50.0
0x1001222E0  MOV     W9, #50
0x1001222E4  CSEL    X8, X8, X9, MI
0x1001222E8  STR     X8, [...payload]
0x100122308  BL      <Integer property setter>
```

数值与 Android arm64 相同；差异只在对象布局、寄存器分配和 Dictionary helper。

### 3.4 iOS armv7

```asm
0x1211D0  VLDR        S0, [R0,#0x128]
0x1211D4  VCVT.F64.S32 D16, S0
0x1211DC  VLDR        D17, [R0,#0x2FC]
0x1211E0  VMUL.F64    D10, D16, D17
0x1211E4  VMOV        R0, R1, D10
0x1211E8  BLX         j____fixdfdi
0x1211EC  VCMPE.F64   D10, D9         ; D9 = 50.0
0x1211F0  MOVS        R2, #0
0x1211F2  VMRS        APSR_nzcv, FPSCR
0x1211F6  IT          PL
0x1211F8  MOVPL       R1, R2          ; high word = 0
0x1211FA  STR         R1, [...high]
0x1211FC  MOV.W       R1, #50
0x121200  IT          PL
0x121202  MOVPL       R0, R1          ; low word = 50
0x121204  STR         R0, [...low]
0x121228  BL          <Integer property setter>
```

`j____fixdfdi` 在 `0xE0482C` 跳到 `0x135DCA4` 的 Mach-O symbol stub，最后
解析为 `/usr/lib/libSystem.B.dylib` 的 `___fixdfdi`。它和 Android armv7 一样
返回完整的 signed 64-bit value，再由 `PL` 选择覆盖。

## 4. 可证明的边界表

对四个插件本体共同可证明的最终值如下：

| `scaledDivision` | 最终 `division` |
|---|---:|
| `-0.0`, `+0.0` | `0` |
| 有限、signed int64 可表示且 `< 50` | 向零截断 |
| `[50, +Inf]` | `50` |
| 任意 quiet/signaling NaN（helper 正常返回） | `50` |

两个 64 位插件还直接证明：

| 输入 | `FCVTZS X,D` 转换 | 最终选择 |
|---|---:|---:|
| `-2^63` | `INT64_MIN` | `INT64_MIN` |
| `< -2^63` 或 `-Inf` | 饱和为 `INT64_MIN` | `INT64_MIN` |
| `>= 2^63` 或 `+Inf` | 饱和为 `INT64_MAX` | `50` |
| NaN | 架构 conversion default | `50`，conversion 被丢弃 |

ARMv7 插件没有包含两个转换 helper 的实现字节：Android 通过 ELF dynsym/PLT
解析 `__aeabi_d2lz`，iOS 通过 libSystem symbol stub 解析 `___fixdfdi`。Arm RTABI
只把 `__aeabi_d2lz` 规定为向零舍入的 C-style conversion，并没有给源语言本就
越界的 `< -2^63` 输入规定一个跨 runtime 固定值。因此，**不能只凭这两份插件
文件把 ARMv7 负溢出伪装成已证明的内联算法**。这部分是设备 runtime 合同，不是
MotionPlayer 自身代码。

参考：

- [Arm Run-time ABI：standard floating-point to integer conversions](https://github.com/ARM-software/abi-aa/blob/main/rtabi32/rtabi32.rst#standard-floating-point-to-integer-conversions)
- [LLVM compiler-rt 当前 signed conversion 实现](https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/builtins/fp_fixint_impl.inc)

Web 端需要一个确定结果，且两份直接含转换指令的参考二进制一致，所以可移植
helper 采用 AArch64 的 signed saturation profile；它不会把未包含在 ARMv7 插件
中的外部 runtime 实现冒充为四端共同源码。

## 5. 源码恢复

新增 `motion::internal::serializeBezierPatchDivision_guess(double)`：

1. NaN 的临时 conversion value 设为 0；
2. `>= 2^63` 饱和到 `INT64_MAX`；
3. `<= -2^63` 饱和到 `INT64_MIN`；
4. 其余值执行定义良好的 signed int64 向零转换；
5. 只有 `scaledDivision < 50.0` 才返回 conversion，否则返回 50。

第 5 步使 NaN 稳定返回 50，也保留 reference compare 对 equal/greater 的 cap。
第 2 步的结果在当前消费者会被 cap 丢弃，但仍显式表达原生“先转换、后比较”的
数据流。helper 使用 `_guess`，因为参考二进制里没有可证明的同名、非内联源码函数。

测试固定了：正负零、正负分数、`49.999`、`50`、`50.999`、`-2^63`、下一个
更负的 double、`-Inf`、`+Inf` 和正负 NaN。

## 6. IDB 与验证

- 四份 recovery IDB 的 conversion、compare/select 站点已经加入上述语义注释并保存；
- 完整 `motionplayer-dll.cpp` 以 Web Debug 的真实 Emscripten defines/includes/ABI
  参数执行 `-fsyntax-only` 成功；唯一诊断仍是仓库既有 `_tss` literal-operator
  弃用 warning；
- `Web Debug Build` 成功增量重编受影响的 motionplayer 翻译单元、生成静态库并
  完成最终 Wasm/HTML 链接；
- `git diff --check` 退出码为 0；输出只有工作树既有 LF/CRLF 转换提示，没有
  whitespace error。
