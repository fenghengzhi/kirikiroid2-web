# MotionPlayer `updateLayers` unsigned divide-by-zero owner boundary（四参考二进制，2026-08-14）

## 1. 结论

`Player_updateLayersVertexComputation_guess` 的 mesh split/extent arithmetic 在四端
普通 nonzero divisor 输入上都是 uint32 truncating division，但 denominator 为零时
不是四插件共同内联的一套算法：

| 目标 | 实现 owner | plugin-contained zero-divisor result |
|---|---|---|
| Android arm64 | inline AArch64 `UDIV W` | `0` |
| iOS arm64 | inline AArch64 `UDIV W` | `0` |
| Android armv7 | imported `__aeabi_uidiv` | 无；由运行时及 `__aeabi_idiv0` policy 决定 |
| iOS armv7 | imported libSystem `___udivsi3` | 无；由目标 OS runtime 决定 |

因此旧分析“ARM32 runtime helper 也返回 0”超出了 reference plugin bytes。两份
AArch64 插件直接证明零，两份 ARMv7 插件只证明 call ABI 和 external owner，不能把
当时设备上可能的 runtime 行为冒充为 MotionPlayer 源码常量。

WebAssembly 的 `i32.div_u` 对零 divisor 会 trap。可移植 Web 端必须选择一个确定
profile；当前实现采用两份直接含 `UDIV` 的 AArch64 结果并返回 0，同时把 helper 从
`unsignedDivideOrZero_guess` 更名为 `unsignedDivideA64Profile_guess`，明确这是一项
有证据边界的移植选择，不是四端共同 helper 名/实现。

## 2. 四端 call-site map

同一 per-node vertex function 中有四类 division：

| 分支 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| own nonempty patch X split | `0x6BA32C` `UDIV` | `0x586E2E` `BLX __aeabi_uidiv` | `0x10010FF24` `UDIV` | `0x10D390` `BLX j____udivsi3` |
| inherited source extent rescale | `0x6BA3EC` `UDIV` | `0x586F1A` helper call | `0x10010FFE4` `UDIV` | `0x10D458` helper call |
| top-level own X split | `0x6BA47C` `UDIV` | `0x586F7A` helper call | `0x100110084` `UDIV` | `0x10D4B4` helper call |
| own empty-patch X split | `0x6BA4D8` `UDIV` | `0x586FDA` helper call | `0x1001100E4` `UDIV` | `0x10D50E` helper call |

所有 numerator 在 call/instruction 前已用 W-register/ARM 32-bit `MUL` 计算，先按
uint32 wrap。division result 也继续作为 raw 32-bit word 使用。

## 3. AArch64 plugin-contained 证据

Android arm64 own nonempty block：

```asm
0x6BA314  ADD     W8, W8, W9        ; denominator = width + height, wrap
0x6BA328  MUL     W9, W10, W9       ; numerator = division * width, wrap
0x6BA32C  UDIV    W8, W9, W8
0x6BA330  SUB     W9, W10, W8
0x6BA334  ADD     W0, W8, #1
0x6BA338  ADD     W1, W9, #1
```

iOS arm64 对应 block：

```asm
0x10010FF1C  MUL   W12, W11, W8
0x10010FF20  ADD   W8, W9, W8
0x10010FF24  UDIV  W8, W12, W8
```

AArch64 `UDIV` 对 zero divisor 把 destination 写成 zero；不会 trap，也不会调用
handler。其余三个 branch 仍是同一 instruction。由此两份插件直接证明：

```text
udiv_a64(numerator, 0) == 0    // 任意 numerator
```

这不仅是 compiler decompiler 的伪代码推断，division instruction 本身就在 plugin
function bytes 内。

## 4. Android armv7 external `__aeabi_uidiv`

四个 call site 都以 numerator=`R0`、denominator=`R1` 调用：

```asm
0x586E28  MUL   R0, R4, R1
0x586E2C  ADD   R1, R2
0x586E2E  BLX   __aeabi_uidiv
```

symbol resolution 是：

```text
call site
  -> .plt 0x2DA31C __aeabi_uidiv
     ADRL R12, ...
     LDR  PC, [__aeabi_uidiv_ptr]
  -> ELF .dynsym import 0x13013F4 "__aeabi_uidiv"
```

插件没有 `__aeabi_uidiv` arithmetic body，也没有可用于固定结果的本地
`__aeabi_idiv0` implementation。

Arm Run-time ABI 规定 out-of-line division helper 遇零时把 quotient 取自
`__aeabi_idiv0`；但该 handler 可以返回传入值、返回 execution-environment 固定值
（例如 0），也可以发 signal/抛异常而不返回。对 unsigned nonzero numerator，helper
传给 handler 的建议值是该类型最大值。因此 RTABI 本身也不支持“所有 conforming
ARMv7 runtime 均返回 0”的结论。

Primary reference:
[Arm Run-time ABI — integer division and division by zero](https://github.com/ARM-software/abi-aa/blob/main/rtabi32/rtabi32.rst#division-by-zero).

## 5. iOS armv7 external `___udivsi3`

plugin call chain 是：

```text
0x10D390 BLX j____udivsi3
  -> 0xE04808 B ___udivsi3
  -> 0x135DE54 Mach-O __picsymbolstub4
  -> import 0x236E228 ___udivsi3
     owner: /usr/lib/libSystem.B.dylib
```

所以 iOS plugin 也只包含 call/stub，不包含 division loop 或 zero check。当时设备的
libSystem build 才是最终 owner；reference Mach-O 没有把那份 dylib 嵌入自身。

LLVM compiler-rt 的当前 `__udivsi3` 只是一个有用的 owner-shape 例子：它转发到
`__udivXi3`，而共享实现明确把 `d==0` 标成 unspecified。它不是参考 iOS 设备具体
libSystem 版本的替代证据，不能拿当前 upstream 结果倒推出旧 binary 的运行值。

Primary references:

- [LLVM compiler-rt `udivsi3.c`](https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/builtins/udivsi3.c)
- [LLVM compiler-rt `int_div_impl.inc`](https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/builtins/int_div_impl.inc)

## 6. zero denominator 的可达来源

zero divisor 并非只能由已损坏内存产生。public raw ratio 与 source dimensions 的异常
输入能进入这些路径，而且 width/height conversion 本身会饱和/归零。

### 6.1 own nonempty / top-level

```text
width       = sat_u32(cw)
height      = sat_u32(ch)
denominator = width + height       // uint32 wrap
numerator   = division * width     // uint32 wrap
splitX      = numerator / denominator
```

denominator 可因以下情况为 0：

- width 和 height 都是 0（例如非正、NaN 或实际零 dimensions）；
- 两个 conversion result 的 uint32 sum 恰好回绕到 0。

AArch64 profile 下 `splitX=0`，随后 stored word 为：

```text
meshDivXWord = 1
meshDivYWord = division + 1       // uint32 wrap
```

top-level branch 不存 counters，而把 `(division+2)*2` 的 wrap result 加入 processed
count。

### 6.2 inherited source rescale

```text
sourceExtent   = sat_u32(source.width + source.height)
currentExtent  = sat_u32(currentWidth + currentHeight)
scaledDivision = (sourceDivision * currentExtent) / sourceExtent
scaledDivision = unsigned_cap_50(scaledDivision)
```

sourceExtent 为 0 时，AArch64 quotient 为 0，第二个 cap 仍为 0。若 currentExtent
随后也为 0，最终 X split 的第二次 division 同样得到 0。ARMv7 则可能由 external
helper/handler 改变值或控制流；尤其返回 `UINT32_MAX` 时，紧随 inherited rescale
的 unsigned cap 会把它变成 50，但 own split 的 quotient 后没有同类 cap。

## 7. 源码、测试与 IDB

- Web helper 更名为 `unsignedDivideA64Profile_guess`，zero divisor 明确返回 0，
  nonzero divisor 仍执行普通 uint32 division；
- call sites 的 arithmetic 和分支没有改变；这次修正的是证据范围、名字和外部 owner
  建模，不伪造 ARMv7 runtime；
- unit test 固定 AArch64 profile 对 zero/nonzero numerator 与 zero divisor 的结果；
- 四份 recovery IDB 的四个 division site 均已注释；两个 ARMv7 import chain 与两个
  AArch64 inline-owner 边界已保存。

验证结果：

- 完整 `motionplayer-dll.cpp` 使用 Web Debug 的真实 Emscripten
  defines/includes/ABI 参数执行 `-fsyntax-only` 成功；唯一诊断是仓库既有 `_tss`
  literal-operator 弃用 warning；
- `Web Debug Build` 成功重编所有包含 update-layers internal header 的翻译单元、生成
  motionplayer 静态库，并完成最终 Wasm/HTML 链接与 shell-memory synchronization；
- `git diff --check` 退出码为 0；输出只有工作树既有 LF/CRLF 转换提示，没有
  whitespace error。
