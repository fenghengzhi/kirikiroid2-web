# MotionPlayer `calcViewParam` mesh-chain `division` conversion（四参考二进制，2026-08-14）

## 1. 结论

`Player::calcViewParam` 为 `cmesh` 中每个有效 ancestor mesh 构造 Dictionary 时，
不会把节点字段或 prepared-render-item 字段直接写入 `division`。四份参考二进制
共同执行：

```text
ancestor.meshDivision raw low 32 bits
        -> interpret as uint32
        -> exact double conversion
        * Player.meshDivisionRatio raw double
        -> unsigned int32 conversion, round toward zero, native saturation
        -> unsigned integer min(converted, 50)
        -> zero-extend into TJS Integer property "division"
```

因此该 exporter 的稳定边界是：

- NaN、负有限数、`-Inf` 和 `Inf * 0` 的 NaN product 都得到 `0`；
- 正溢出和 `+Inf` 先饱和到 `UINT32_MAX`，再得到 `50`；
- `[0, 50)` 内的有限 product 向零截断；
- `>= 50` 得到 `50`。

旧 Web 源码对 product 直接执行 `static_cast<uint32_t>`，NaN、负数和越界输入都
落入 C++ 未定义行为；它还把当前以 signed `int` 暴露的 node 字段按 signed 值转
double，而原生指令明确把同一低 32 位解释为 unsigned。本次以
`calcViewMeshDivision_guess(double, uint32_t)` 显式恢复这两个边界。

## 2. 所在调用链与数据来源

完整 `calcViewParam(frame, viewParams)` 契约、absolute-frame evaluation preamble、
caller-owned 输出对象、`cmesh` separator 复用以及临时 TJS 对象生命周期已经在
[`motionplayer_calc_view_param_four_binary_2026-08-11.md`](motionplayer_calc_view_param_four_binary_2026-08-11.md)
闭合。本记录只缩放到每个 active ancestor mesh Dictionary 的 `division` producer。

共同调用链为：

```text
NCB Motion.Player.calcViewParam wrapper
  -> Player_calcViewParam_guess(frame, viewParams)
     -> absolute frameProgress/updateLayers
     -> for each non-root output layer
        -> walk node.meshAncestor chain
           -> when ancestor has active mesh data
              -> allocate mesh Dictionary
              -> compute/write division
              -> allocate/write invOffset, invMatrix and patch Arrays
              -> append Dictionary to fresh cmesh Array
```

ratio 直接来自 Player 自身的 raw double；它不是 `EmoteEngine` 的 metadata-scale
或 reciprocal-scale 字段。三个公开 property wrapper 的 receiver identity 和 raw
setter 行为见
[`motionplayer_mesh_division_ratio_four_binary_2026-08-13.md`](motionplayer_mesh_division_ratio_four_binary_2026-08-13.md)。

ancestor 字段通过 32-bit load 送入 unsigned floating conversion。即使 Web 结构体
目前把该 slot 声明成 `int`，raw `0xFFFFFFFF` 在这里也是 `4294967295.0`，不是
`-1.0`。本纵切面只在 call site 保留低 32 位，没有把尚未完整审计的 parser/结构体
全局类型一并改写。

## 3. 四端指令证据

| 目标 | `Player_calcViewParam_guess` | conversion / cap block |
|---|---:|---:|
| Android arm64 | `0x6CE908` | `0x6CF834..0x6CF89C` |
| Android armv7 | `0x594958` | `0x594DC0..0x594DF8` |
| iOS arm64 | `0x1001201CC` | `0x100120660..0x1001206A4` |
| iOS armv7 | `0x11EED4` | `0x11F3DC..0x11F422` |

### 3.1 Android arm64

```asm
0x6CF834  LDR     S0, [X8,#0x7D8]    ; ancestor meshDivision raw word
0x6CF83C  LDR     D1, [X9,#0x498]    ; Player.meshDivisionRatio
0x6CF840  UCVTF   D0, D0             ; uint32 -> double
0x6CF84C  FMUL    D0, D1, D0
0x6CF850  FCVTZU  W8, D0             ; double -> uint32, toward zero
0x6CF854  CMP     W8, #50
0x6CF858  CSEL    W8, W8, W9, CC     ; unsigned converted < 50 ? value : 50
0x6CF85C  STUR    X8, [X29,#...payload]
...
0x6CF89C  BL      <division Integer property setter>
```

`FCVTZU W,D` 确认 destination 是 32-bit unsigned integer。`CSEL ... CC`
消费 `CMP W8,#50` 的 carry clear 条件，所以 cap 也在 unsigned integer 域中执行；
它不是对原 product 做浮点比较。

### 3.2 Android armv7

```asm
0x594DC0  VLDR        S0, [R0,#0x6C0]
0x594DC4  VCVT.F64.U32 D0, S0
0x594DCA  VLDR        D1, [R0,#0x340]
0x594DCE  VMUL.F64    D0, D1, D0
0x594DD2  VCVT.U32.F64 S0, D0
0x594DD6  VMOV        R1, S0
0x594DDA  CMP         R1, #50
0x594DDC  IT          CS
0x594DDE  MOVCS       R1, #50
0x594DE0  STR         R1, [...payload]
0x594DF8  BL          <division Integer property setter>
```

这里 input/output 两次 VFP conversion 都显式带 `.U32`，而 `CS` 是 unsigned
`>=`。没有外部 runtime helper，conversion 边界包含在插件指令流内。

### 3.3 iOS arm64

```asm
0x100120660  LDR     S0, [X8,#0x7E8]
0x100120664  UCVTF   D0, D0
0x10012066C  LDR     D1, [X9,#0x428]
0x100120670  FMUL    D0, D0, D1
0x100120674  FCVTZU  W8, D0
0x100120678  CMP     W8, #50
0x100120680  CSEL    W8, W8, W9, CC
0x100120684  STR     X8, [...payload]
0x1001206A4  BL      <division Integer property setter>
```

该序列与 Android arm64 的转换域、comparison 和 zero-extended TJS payload
完全一致，仅对象布局与寄存器分配不同。

### 3.4 iOS armv7

```asm
0x11F3DC  VLDR        S0, [R0,#0x6CC]
0x11F3E0  VCVT.F64.U32 D16, S0
0x11F3E8  VLDR        D17, [R0,#0x2FC]
0x11F3EC  VMUL.F64    D16, D16, D17
0x11F3F0  VCVT.U32.F64 S0, D16
0x11F3F4  VMOV        R0, S0
0x11F3F8  CMP         R0, #50
0x11F3FA  IT          CS
0x11F3FC  MOVCS       R0, #50
0x11F3FE  STR         R0, [...payload]
0x11F422  BL          <division Integer property setter>
```

两个 ARMv7 目标和两个 AArch64 目标都在插件体内给出同一 unsigned conversion
和 unsigned cap；这里不存在 `getCommandList` 那种 32-bit external ABI helper
证明边界。

## 4. 精确数值边界

原生 unsigned conversion 先执行，随后才比较 conversion result：

| ratio × uint32(raw division) product | uint32 conversion | exported `division` |
|---|---:|---:|
| NaN（含 `0 * ±Inf`） | `0` | `0` |
| `-Inf` 或任意负有限数 | `0` | `0` |
| `-0.0`, `+0.0` | `0` | `0` |
| finite，`0 < x < 2^32` | 向零截断 | converted `<50` 时原值，否则 `50` |
| `+Inf` 或 `x >= 2^32` | `UINT32_MAX` | `50` |

几个容易暴露错误重构的例子：

| ratio / raw bits | product | result |
|---|---:|---:|
| `0.5 / 7` | `3.5` | `3` |
| `49.999 / 1` | `49.999` | `49` |
| `50.0 / 1` | `50` | `50` |
| `-0.5 / 7` | `-3.5` | `0` |
| `1 / 0xFFFFFFFF` | `4294967295` | `50` |
| `NaN / 4` | NaN | `0` |
| `+Inf / 0` | NaN | `0` |
| `+Inf / 4` | `+Inf` | `50` |
| `-Inf / 4` | `-Inf` | `0` |

特别是 NaN：这里先由 unsigned conversion 产生 0，再由 integer cap 保留 0；
不能把它改写成 `scaled < 50 ? converted : 50`，后者会在 unordered compare 上
错误地产生 50。

## 5. 三种相邻 division stage 不能合并

同一个 Player ratio 附近存在三套不同机器语义：

| Stage | input integer | float→integer | cap/selection 域 | NaN | 负 product |
|---|---|---|---|---:|---:|
| `calcViewParam.cmesh[].division` | node raw `uint32` | saturating `uint32` | unsigned integer `>=50` cap | `0` | `0` |
| prepared render item | node raw `uint32` | saturating `int32` | signed integer `>=50` cap | `0` | negative value/saturation retained |
| `getCommandList.bezierPatch.division` | prepared `int32` | signed `int64` | floating ordered-`<50` select | `50` | negative value/saturation retained |

prepared producer 的详细分支与 persistent-item publication 时序见
[`motionplayer_prepared_bezier_division_conversion_four_binary_2026-08-14.md`](motionplayer_prepared_bezier_division_conversion_four_binary_2026-08-14.md)；
command Dictionary 的 signed-int64/external-helper 边界见
[`motionplayer_get_command_list_division_conversion_four_binary_2026-08-14.md`](motionplayer_get_command_list_division_conversion_four_binary_2026-08-14.md)。

`calcViewParam` 直接遍历 node ancestor chain，并不经过 prepared item，因此它只
乘一次当前 ratio。`getCommandList` 则读取已经乘过一次 ratio 的 persistent prepared
field，再乘第二次 ratio。三条路径即使在普通默认值 `ratio=1` 下看起来相同，也不能
抽成一个共享的“normalized division”。

## 6. 源码、测试与 IDB

- 新增 `motion::internal::calcViewMeshDivision_guess(double, uint32_t)`，显式实现
  NaN/负数归零、正溢出饱和、向零截断和 conversion 后 unsigned cap；
- `calcViewParam` call site 将 node slot 的低 32 位按 `uint32_t` 传入；
- unit test 固定普通小数、49/50、negative zero、raw `UINT32_MAX`、NaN、正负无穷
  和 `Inf * 0`；
- 四份 recovery IDB 的 unsigned input conversion、unsigned output conversion 和
  integer-cap 站点均已加入语义注释并保存。

验证结果：

- 完整 `motionplayer-dll.cpp` 使用 Web Debug 的真实 Emscripten
  defines/includes/ABI 参数执行 `-fsyntax-only` 成功；唯一诊断是仓库既有 `_tss`
  literal-operator 弃用 warning；
- `Web Debug Build` 成功重编受影响的 motionplayer 翻译单元、生成静态库，并完成
  最终 Wasm/HTML 链接与 shell-memory synchronization；
- `git diff --check` 退出码为 0；输出只有工作树既有 LF/CRLF 转换提示，没有
  whitespace error。
