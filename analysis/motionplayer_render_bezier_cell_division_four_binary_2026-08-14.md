# MotionPlayer render Bezier cell-division pipeline（四参考二进制，2026-08-14）

## 1. 结论

四个 `reference/binaries/` 中，所有已定位的 render-time Bezier cell-count producer
都使用同一条 uint32 管线：

```text
widthWord   = sat_u32_toward_zero(sourceWidth)
heightWord  = sat_u32_toward_zero(sourceHeight)
divisionWord = low32(division)

denominator = widthWord + heightWord             // uint32 wrap
numerator   = divisionWord * widthWord            // uint32 wrap
split       = unsigned_divide(numerator, denominator)
divXWord    = split + 1                           // uint32 wrap
divYWord    = divisionWord - split + 1            // uint32 wrap
```

`divXWord`/`divYWord` 最终以 signed 32-bit cell count 解释；其 bit pattern 不会在
producer 内被 clamp。

这否定了项目中从旧 `libkrkr2.so` 地址推导出的第二套
“`double(division) * width / (width + height)`，再 `FCVTZS`”管线。特别是当前
Android arm64 reference 的 `0x6C5C00` 是 Variant/dispatch setup，`0x6C8E5C`
是 exception-cleanup landing block，二者都不是独立 cell-division helper。对应的
canvas leaf producer 实际使用 `FCVTZU/MUL/ADD/UDIV`，与其他 render family 完全同型。

源码因此删除了过时的浮点 helper 和以旧地址命名的 uint32 helper，统一为
`renderBezierPatchCellDivisions_guess`。

## 2. producer/caller map

同一算术模板在 native 中内联到以下四类 render family：

1. `Player_renderPreparedItemsToD3DTexture_guess`：Bezier tessellation 的 `divX/divY`；
2. `Player_renderToCanvas_guess`：函数内三处 Bezier dispatch producer；
3. `Player_renderAccurateSeparateLayerAdaptor_guess`：accurate separate-layer Bezier
   dispatch；
4. `Player_buildPrivateMotionGLLCommands_guess`：写入 queued command 的两个 cell-count
   words。

Web recovery 中的消费者覆盖：

- leaf `bezierPatchCopy` TJS dispatch；
- outline/meshline 的五参数 `drawBezierPatchMeshFrame`；
- canvas 与 accurate-SLA native image paths；
- D3D tessellation batch；
- PrivateMotionGLL queued command。

它们现在都调用同一个 helper，避免某个 backend 对 malformed extent 走不同的 C++
floating-to-integer UB 或不同的 arithmetic domain。

## 3. 四端指令位置

表中 conversion 列给出每组两个 width/height conversion 的首地址，division 列给出
相应 unsigned division instruction/call。

### 3.1 Android arm64

| family/site | conversion | division |
|---|---:|---:|
| D3D | `0x6AB6AC` | `0x6AB6BC` `UDIV W` |
| canvas A | `0x6C558C` | `0x6C55A0` `UDIV W` |
| canvas B / leaf | `0x6C5C7C` | `0x6C5C9C` `UDIV W` |
| canvas C | `0x6C623C` | `0x6C6250` `UDIV W` |
| accurate SLA | `0x6C856C` | `0x6C858C` `UDIV W` |
| PrivateMotionGLL | `0x6DC158` | `0x6DC178` `UDIV W` |

### 3.2 Android armv7

| family/site | conversion | division |
|---|---:|---:|
| D3D | `0x57D66E` | `0x57D696` `BLX __aeabi_uidiv` |
| canvas A | `0x58ED9C` | `0x58EDBE` helper call |
| canvas B / leaf | `0x58F478` | `0x58F48E` helper call |
| canvas C | `0x58F900` | `0x58F916` helper call |
| accurate SLA | `0x591614` | `0x59162A` helper call |
| PrivateMotionGLL | `0x59CE06` | `0x59CE44` helper call |

### 3.3 iOS arm64

| family/site | conversion | division |
|---|---:|---:|
| D3D | `0x10010484C` | `0x10010485C` `UDIV W` |
| canvas A | `0x100118F58` | `0x100118F68` `UDIV W` |
| canvas B / leaf | `0x100119700` | `0x100119710` `UDIV W` |
| canvas C | `0x100119B70` | `0x100119B84` `UDIV W` |
| accurate SLA | `0x10011BABC` | `0x10011BACC` `UDIV W` |
| PrivateMotionGLL | `0x10012BD78` | `0x10012BD8C` `UDIV W` |

### 3.4 iOS armv7

| family/site | conversion | division |
|---|---:|---:|
| D3D | `0x101F5C` | `0x101F82` `BLX j____udivsi3` |
| canvas A | `0x116A66` | `0x116A82` helper call |
| canvas B / leaf | `0x117434` | `0x11746E` helper call |
| canvas C | `0x117CEE` | `0x117D0C` helper call |
| accurate SLA | `0x11A018` | `0x11A036` helper call |
| PrivateMotionGLL | `0x12A966` | `0x12A99A` helper call |

四端每个 conversion pair 都是 `FCVTZU W,D` 或 `VCVT.U32.F64`；没有与这些
producer 相邻的 `FDIV`、`FCVTZS W,D` 或 `VCVT.S32.F64`。

## 4. arithmetic order 与 wrap

Android arm64 D3D producer 是最紧凑的共同模板：

```asm
0x6AB6AC  FCVTZU W8, D11        ; widthWord
0x6AB6B0  FCVTZU W9, D10        ; heightWord
0x6AB6B4  MUL    W10, W19, W8   ; divisionWord * widthWord, wrap
0x6AB6B8  ADD    W8, W9, W8     ; widthWord + heightWord, wrap
0x6AB6BC  UDIV   W8, W10, W8    ; split
0x6AB6C0  ADD    W2, W8, #1     ; divXWord
0x6AB6C4  SUB    W8, W19, W8
0x6AB6C8  ADD    W23, W8, #1    ; divYWord
```

canvas leaf、accurate SLA 与 PrivateMotionGLL 只是 register allocation 和 Variant
materialization 顺序不同。PrivateMotionGLL 的尾部例如：

```asm
0x6DC178  UDIV W9, W9, W13
0x6DC17C  MOV  W13, #1
0x6DC180  SUB  W13, W13, W9
0x6DC184  ADD  W12, W13, W12   ; divisionWord - split + 1
0x6DC188  ADD  W9, W9, #1
0x6DC194  STP  W9, W12, [...]  ; raw output words
```

所以以下顺序差异是可观察的，不能重写成代数上“正常输入等价”的 floating formula：

- width/height 是分别转换，之后才相加；不是先计算 double `width+height`；
- numerator 和 denominator 都在 32-bit word domain 回绕；
- division 使用回绕后的两个 words；
- 两个输出也先回绕，再以 signed word 解释。

## 5. conversion boundary

`FCVTZU W,D` / `VCVT.U32.F64` 的 value profile 在四端一致：

| double 输入 | word 结果 |
|---|---:|
| NaN、负有限值、`-Infinity`、`-0.0` | `0` |
| `[0, 2^32)` | toward-zero truncation |
| `>= 2^32`、`+Infinity` | `UINT32_MAX` |

Web helper 复用已由 timeline `ti` 等四端路径闭合的
`doubleToUnsignedIntTowardZeroSaturated_guess`，不再依赖越界
`static_cast<uint32_t>(double)` 的 C++ undefined behavior。

## 6. zero-divisor owner

这条 render pipeline 与 `updateLayers` mesh split 具有相同的 owner 边界：

- 两个 AArch64 plugin 内联 `UDIV W`，denominator 为零时直接得到 `split=0`；
- Android armv7 调用 imported `__aeabi_uidiv`；
- iOS armv7 调用 libSystem imported `___udivsi3`；
- 两个 ARMv7 plugin 自身不包含 zero handler，不能从 plugin bytes 声称它们必然返回 0。

Wasm `i32.div_u` 会在零 denominator 上 trap，因此 Web 明确采用两个 AArch64 plugin
直接证明的 profile：zero denominator 返回 split 0。外部 owner 的完整证据见
`motionplayer_update_layers_unsigned_divide_zero_owner_four_binary_2026-08-14.md`。

zero denominator 可由普通零 extent、conversion 归零，或 converted words 相加恰好
回绕到零产生。例如：

```text
sourceWidth  = +Infinity -> UINT32_MAX
sourceHeight = 1         -> 1
denominator               -> 0 (wrap)
split                     -> 0 (AArch64 profile)
result                    -> {1, divisionWord + 1}
```

## 7. malformed-input examples

| division | width | height | 结果 words / signed counts |
|---:|---:|---:|---:|
| `10` | `8` | `2` | `{9, 3}` |
| `10` | `0` | `0` | `{1, 11}` |
| `10` | `-1` | `2` | `{1, 11}` |
| `10` | NaN | `2` | `{1, 11}` |
| `10` | `2` | `-2` | `{11, 1}` |
| `10` | `+Infinity` | `1` | `{1, 11}`（denominator wrap） |
| `2` | `2^31` | `0` | `{1, 3}`（numerator wrap to zero） |
| `-1` | `1` | `0` | `{0, 1}` |
| `INT32_MIN` | `1` | `0` | `{-2147483647, 1}` |

旧 floating helper 对 `(division=10,width=2,height=-2)` 先制造 `+Infinity`，再依赖
signed floating conversion，得到完全不同的极值结果；当前四份 reference 的真实
uint32 extent pipeline 会先把 negative height 饱和为 0，结果是 `{11,1}`。

## 8. 源码、测试与 IDB

- 删除 `bezierPatchCellDivisionsLike_0x6C5C00`；
- 删除 `bezierPatchCellDivisionsU32Like_0x6C8E5C`；
- 新增单一语义名 `renderBezierPatchCellDivisions_guess`；
- 所有 render call site 改用同一 helper；
- output word 使用 `memcpy` bit interpretation，避免 uint32-to-signed 的
  implementation-defined value；
- unit tests 固定 normal、zero、negative、NaN、Infinity、denominator wrap、numerator
  wrap 和 negative division bit patterns；
- 四份 recovery IDB 的四个 render family、六组 conversion/division site 已注释并
  保存；Android arm64 的两个旧地址误认点也已明确标注。

验证结果：

- 完整 `motionplayer-dll.cpp` 已使用 Web Debug 的真实 Emscripten 参数执行 syntax
  check 成功；唯一诊断是仓库既有 `_tss` literal-operator 弃用 warning；
- `Web Debug Build` 成功重编受影响的 render 翻译单元与 motionplayer 静态库，并完成
  最终 Wasm/HTML 链接和 shell-memory synchronization；
- `git diff --check` 退出码为 0；输出只有工作树既有 LF/CRLF 转换提示，没有 whitespace
  error。
