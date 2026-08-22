# MotionPlayer `updateLayers` mesh division compare-domain boundary（四参考二进制，2026-08-14）

## 1. 结论

`Player_updateLayersVertexComputation_guess` 中并不存在一套统一的
`min(uint32(ratio * rawDivision), 50u)`。四份参考二进制共同保留了两种只在
sign-bit 边界分叉的 cap：

```text
common conversion:
    raw node meshDivision low 32 bits
      -> interpret as uint32
      -> exact double
      * Player.meshDivisionRatio raw double
      -> saturated uint32, round toward zero

own type-1 mesh paths:
    compare converted word against 50 as signed int32
    signed(converted) >= 50 ? 50 : converted

inherited-source path:
    compare converted word against 50 as uint32
    converted >= 50u ? 50 : converted
```

在普通 `converted < 2^31` 输入上两者相同；在 `converted` 的 bit 31 为 1 时，
结果完全不同：

- own type-1 路径把 `0x80000000..0xFFFFFFFF` 视为 signed negative，保留原 raw
  `uint32` word；
- inherited-source 路径把同一范围视为 unsigned `>=50`，全部替换为 50。

因此 `+Inf`、`>=2^32` 和其他正溢出 product 在 own type-1 路径先饱和为
`UINT32_MAX`，随后**绕过** signed cap，而不是得到 50。旧 Web helper 对所有分支
统一使用 unsigned cap，抹掉了这个四端一致的边界。

本次把旧 `scaledMeshDivision_guess` 拆成：

- `scaledOwnMeshDivision_guess`：unsigned conversion + signed comparison-domain cap；
- `scaledInheritedMeshDivision_guess`：unsigned conversion + unsigned cap。

## 2. 所在函数与分支

| 目标 | vertex-computation body | own nonempty patch | inherited source | top-level own | own empty patch |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x6B98D0` | `0x6BA300..0x6BA324` | `0x6BA3B8..0x6BA3F4` | `0x6BA44C..0x6BA470` | `0x6BA4A8..0x6BA4C8` |
| Android armv7 | `0x5866F8` | `0x586DF4..0x586E26` | `0x586EDA..0x586F24` | `0x586F40..0x586F72` | `0x586FA2..0x586FCC` |
| iOS arm64 | `0x10010F6AC` | `0x10010FEF8..0x10010FF18` | `0x10010FFB8..0x10010FFEC` | `0x100110058..0x100110078` | `0x1001100AC..0x1001100CC` |
| iOS armv7 | `0x10CE30` | `0x10D358..0x10D388` | `0x10D41A..0x10D462` | `0x10D47C..0x10D4AC` | `0x10D4D8..0x10D500` |

这些 block 都位于同一个 per-node vertex pass 中。更宽的 parent→meshAncestor
选链、raw patch combine、grid vector、separator 两阶段映射和 processed-count
生命周期见
[`motionplayer_vertex_mesh_chain_composition_four_binary_2026-08-14.md`](motionplayer_vertex_mesh_chain_composition_four_binary_2026-08-14.md)。

分支语义是：

1. 当前 node 有 mesh ancestor、`meshType==1` 且 effective own patch nonempty：
   用 own signed-domain cap，先建 unit-quad grid，再逐点过 own 4×4 Bezier patch；
2. 当前 node 有 mesh ancestor、`meshType==1` 但 effective patch empty：
   仍用 own signed-domain cap，直接在 affine four corners 上建格；
3. 当前 node 有 mesh ancestor、但不是 type 1：沿 meshAncestor 找第一个
   `hasMeshData` source，无 null guard；该 source raw division 使用 inherited unsigned
   cap，再按 source/current extent 缩放并进行第二次 unsigned cap；
4. 当前 node 没有 mesh ancestor、`meshType==1`：仍用 own signed-domain cap；不
   materialize composite vector，只向 Player 的 uint32 processed count 加上网格点数。

## 3. own type-1 的 signed comparison 证据

### 3.1 Android arm64

nonempty own-patch block：

```asm
0x6BA300  LDR     S0, [X23,#0x7D8]   ; raw division word
0x6BA308  LDR     D2, [X8,#0x498]    ; Player ratio
0x6BA310  UCVTF   D0, D0
0x6BA318  FMUL    D0, D2, D0
0x6BA31C  FCVTZU  W10, D0
0x6BA320  CMP     W10, #50
0x6BA324  CSEL    W10, W10, W11, LT ; signed LT, W11=50
```

`LT` 是 `N != V` 的 signed less-than，不是 unsigned `CC`。相同的
`FCVTZU + CMP + CSEL LT` 在 top-level block `0x6BA468..0x6BA470` 和 empty-patch
block `0x6BA4C0..0x6BA4C8` 重复。

### 3.2 Android armv7

```asm
0x586DF4  VLDR        S2, [R0]
0x586DF8  VCVT.F64.U32 D1, S2
0x586E00  VLDR        D2, [R0,#0x340]
0x586E06  VMUL.F64    D1, D2, D1
0x586E0A  VCVT.U32.F64 S2, D1
0x586E16  VMOV        R4, S2
0x586E22  CMP         R4, #50
0x586E24  IT          GE             ; signed GE
0x586E26  MOVGE       R4, #50
```

`GE` 是 signed `N == V`。top-level `0x586F56..0x586F72` 与 empty-patch
`0x586FB8..0x586FCC` 保持同一比较域。数据库中独立存在的 11-instruction helper
`0x585434` 也执行 `VCVT.U32.F64` 后的 signed `IT GE`；它已更名为
`Player_scaleOwnMeshDivisionSignedCap_guess`，而不是继续使用误导性的无条件 U32-cap
名称。

### 3.3 iOS arm64

```asm
0x10010FEF8  LDR     S0, [X10,#0x7E8]
0x10010FEFC  UCVTF   D0, D0
0x10010FF04  LDR     D1, [X11,#0x428]
0x10010FF08  FMUL    D0, D0, D1
0x10010FF0C  FCVTZU  W11, D0
0x10010FF10  CMP     W11, #50
0x10010FF18  CSEL    W11, W11, W12, LT
```

top-level `0x10011006C..0x100110078` 和 empty-patch
`0x1001100C0..0x1001100CC` 也使用 signed `LT`。

### 3.4 iOS armv7

```asm
0x10D358  VLDR        S0, [R0]
0x10D35C  VCVT.F64.U32 D17, S0
0x10D362  VLDR        D18, [R0,#0x2FC]
0x10D368  VMUL.F64    D17, D17, D18
0x10D36C  VCVT.U32.F64 S0, D17
0x10D378  VMOV        R4, S0
0x10D384  CMP         R4, #50
0x10D386  IT          GE
0x10D388  MOVGE       R4, #50
```

top-level `0x10D490..0x10D4AC` 和 empty-patch `0x10D4EC..0x10D500`
重复 signed `GE`。

## 4. inherited-source 的 unsigned comparison 证据

相邻 source 分支不是编译器对同一比较的另一种写法；四端都明确切换到 unsigned
condition code。

### Android arm64

```asm
0x6BA3B8  LDR     S4, [X8,#0x7D8]
0x6BA3C0  LDR     D5, [X9,#0x498]
0x6BA3CC  UCVTF   D2, D4
0x6BA3D0  FMUL    D2, D5, D2
0x6BA3D4  FCVTZU  W9, D2
0x6BA3D8  CMP     W9, #50
0x6BA3E0  CSEL    W9, W9, W11, CC   ; unsigned below
...
0x6BA3F0  CMP     W9, #50
0x6BA3F4  CSEL    W9, W9, W11, CC   ; second unsigned cap
```

### Android armv7

```asm
0x586EF6  VCVT.U32.F64 S2, D1
0x586F12  CMP         R0, #50
0x586F14  IT          CS             ; unsigned >=
0x586F16  MOVCS       R0, #50
...
0x586F20  CMP         R5, #50
0x586F22  IT          CS
0x586F24  MOVCS       R5, #50
```

### iOS arm64

```asm
0x10010FFCC  FCVTZU  W9, D1
0x10010FFD0  CMP     W9, #50
0x10010FFD8  CSEL    W9, W9, W12, CC
...
0x10010FFE8  CMP     W9, #50
0x10010FFEC  CSEL    W9, W9, W12, CC
```

### iOS armv7

```asm
0x10D434  VCVT.U32.F64 S0, D17
0x10D450  CMP         R0, #50
0x10D452  IT          CS
0x10D454  MOVCS       R0, #50
...
0x10D45E  CMP         R5, #50
0x10D460  IT          CS
0x10D462  MOVCS       R5, #50
```

`CC`/`CS` 是 carry-based unsigned conditions。它们把 sign-bit-set word 正确视为
大 unsigned value，因此 inherited path 不会泄漏大 division。

## 5. conversion 与 cap 的完整边界表

共同 `FCVTZU` / `VCVT.U32.F64` conversion：

| product | converted uint32 |
|---|---:|
| NaN（含 `0 * ±Inf`） | `0` |
| `-Inf` 或负有限数 | `0` |
| `±0` | `0` |
| finite，`0 < x < 2^32` | 向零截断 |
| `+Inf` 或 `x >= 2^32` | `UINT32_MAX` |

conversion 后：

| converted range | own type-1 signed cap | inherited unsigned cap |
|---|---:|---:|
| `0..49` | 原值 | 原值 |
| `50..0x7FFFFFFF` | `50` | `50` |
| `0x80000000..0xFFFFFFFF` | **原 raw word** | `50` |

所以代表性输入为：

| ratio / raw division | converted | own | inherited |
|---|---:|---:|---:|
| `0.5 / 7` | `3` | `3` | `3` |
| `49.999 / 1` | `49` | `49` | `49` |
| `50 / 1` | `50` | `50` | `50` |
| `(2^31-1) / 1` | `0x7FFFFFFF` | `50` | `50` |
| `2^31 / 1` | `0x80000000` | `0x80000000` | `50` |
| `+Inf / 1` | `0xFFFFFFFF` | `0xFFFFFFFF` | `50` |
| `+Inf / 0` | `0`（NaN product） | `0` | `0` |
| negative / nonzero | `0` | `0` | `0` |

## 6. 大 own division 的下游 word/signed 生命周期

own paths 继续用 32-bit unsigned arithmetic 计算 split：

```text
denominator = u32(width) + u32(height)             // wrap
splitX      = (division * u32(width)) / denominator // product wraps
meshDivXWord = splitX + 1                           // wrap
meshDivYWord = division - splitX + 1                // wrap
```

两个 result word 直接以 `STR W`/`STR` 写入 node，随后作为 signed `int` 参数传给
bilinear grid builder。也就是说，cap comparison 先按 signed 看 raw word，split
期间按 unsigned 运算，建格循环又按 signed counter 消费，三个域不能被一种 C++
类型概括。

例如 `ratio=+Inf, rawDivision=1, width=height=1`：

```text
division    = 0xFFFFFFFF
splitX      = 0xFFFFFFFF / 2 = 0x7FFFFFFF
meshDivXWord = 0x80000000 -> signed INT32_MIN
meshDivYWord = 0x80000001 -> signed INT32_MIN + 1
```

grid builder 的 signed inclusive loops 因负 cell counts 不输出点。这解释了为何
“division 总在 0..50，所以 stored divisions 至少为 1”只对普通资产/ratio 成立，
不是 raw setter 可达的完整边界。

top-level own type-1 branch不保存 X/Y，也不建 vector；它用相同 division/split，
把 `(division-splitX+2)*(splitX+2)` 以 uint32 wrap 加进 processed count。因此同一
异常 ratio 在有 ancestor 与无 ancestor 分支产生不同的持久副作用。

Web 端新增 `meshDivisionCounterWordToInt_guess`，以 bit-preserving copy 把 W-register
word 显式重解释成 signed int，避免依赖 out-of-range `uint32_t -> int` 的宿主实现
定义行为。

上述 `/` 在 AArch64 是零 divisor 返回 0 的 inline `UDIV`，在 ARMv7 则分别是
external `__aeabi_uidiv` / libSystem `___udivsi3`。完整 external-owner 与 Web profile
选择见
[`motionplayer_update_layers_unsigned_divide_zero_owner_four_binary_2026-08-14.md`](motionplayer_update_layers_unsigned_divide_zero_owner_four_binary_2026-08-14.md)。

## 7. 源码、测试与 IDB

- `PlayerUpdateLayersInternal.h` 拆分 own signed-domain 与 inherited unsigned-domain
  helpers，并加入 counter-word 的显式 signed bit interpretation；
- `PlayerUpdateGeometry.cpp` 的 own nonempty、own empty、top-level 三个 call sites
  使用前者，ancestor source 使用后者；
- unit test 固定 NaN/negative/49/50/`2^31-1`/`2^31`/`+Inf`/`Inf*0`，并验证
  `UINT32_MAX` division 在 `width=height=1` 时形成两个 negative grid counters；
- 四份 recovery IDB 的 conversion、signed/unsigned cap 与第二次 inherited cap
  站点都已写入语义注释并保存；Android armv7 的 outlined helper 也已按新语义更名。

验证结果：

- 完整 `motionplayer-dll.cpp` 使用 Web Debug 的真实 Emscripten
  defines/includes/ABI 参数执行 `-fsyntax-only` 成功；唯一诊断是仓库既有 `_tss`
  literal-operator 弃用 warning；
- `Web Debug Build` 成功重编所有包含 update-layers internal header 的翻译单元、生成
  motionplayer 静态库，并完成最终 Wasm/HTML 链接与 shell-memory synchronization；
- `git diff --check` 退出码为 0；输出只有工作树既有 LF/CRLF 转换提示，没有
  whitespace error。
