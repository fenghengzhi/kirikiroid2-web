# Motionplayer renderer primitive hint family — four-reference audit (V159)

Date: 2026-08-16

## Result

Fresh analysis of all four files under `reference/binaries/` proves that the
globals beginning at `meshCopy` are not a four-member group and are not byte
items or an aggregate. They are one contiguous family of twelve independent,
zero-initialized, 32-bit TJS member-hint slots:

```text
meshCopy
bezierPatchCopy
affineCopy
setClip
bufLayer
operateMesh
operateBezierPatch
operateAffine
drawMeshFrame
drawBezierPatchMeshFrame
drawBezierPatchFrame
drawLine
```

The immediately following slot is `visible`, whose consumer set belongs to a
different cross-render/calc/publication family. Therefore the exact V159 range
ends after `drawLine`. The pre-existing `size=1` IDB item on that next
`visible` address is old recovery metadata, not evidence about the native
object size; correcting that next family is intentionally left to V160.

This supersedes the four-render-hint portion of
`motionplayer_dispatch_member_hint_globals_four_binary_2026-08-15.md`. In
particular, that earlier document's iOS byte items and Android aggregate view
were recovery-IDB artifacts inherited from an older analysis stage.

## Reference functions

| role | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| build render commands | `0x6C2208` | `0x58C7C4` | `0x1001167BC` | `0x114118` |
| render to TJS Layer canvas | `0x6C4820` | `0x58E2CC` | `0x1001186E0` | `0x11653C` |
| accurate SeparateLayerAdaptor render | `0x6C7088` | `0x590468` | `0x10011A9E8` | `0x118D70` |

All three current recovery IDBs already name these stripped native functions
with `_guess`. No source-level name is claimed for a stripped symbol.

## Exact slot layout

Every row below is a distinct four-byte object. The address stride is exactly
four on all four targets, including both 64-bit targets.

| offset | member | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| ---: | --- | ---: | ---: | ---: | ---: |
| `+0x00` | `meshCopy` | `0x1AB5458` | `0x11118F4` | `0x101B69920` | `0x187D5C4` |
| `+0x04` | `bezierPatchCopy` | `0x1AB545C` | `0x11118F8` | `0x101B69924` | `0x187D5C8` |
| `+0x08` | `affineCopy` | `0x1AB5460` | `0x11118FC` | `0x101B69928` | `0x187D5CC` |
| `+0x0C` | `setClip` | `0x1AB5464` | `0x1111900` | `0x101B6992C` | `0x187D5D0` |
| `+0x10` | `bufLayer` | `0x1AB5468` | `0x1111904` | `0x101B69930` | `0x187D5D4` |
| `+0x14` | `operateMesh` | `0x1AB546C` | `0x1111908` | `0x101B69934` | `0x187D5D8` |
| `+0x18` | `operateBezierPatch` | `0x1AB5470` | `0x111190C` | `0x101B69938` | `0x187D5DC` |
| `+0x1C` | `operateAffine` | `0x1AB5474` | `0x1111910` | `0x101B6993C` | `0x187D5E0` |
| `+0x20` | `drawMeshFrame` | `0x1AB5478` | `0x1111914` | `0x101B69940` | `0x187D5E4` |
| `+0x24` | `drawBezierPatchMeshFrame` | `0x1AB547C` | `0x1111918` | `0x101B69944` | `0x187D5E8` |
| `+0x28` | `drawBezierPatchFrame` | `0x1AB5480` | `0x111191C` | `0x101B69948` | `0x187D5EC` |
| `+0x2C` | `drawLine` | `0x1AB5484` | `0x1111920` | `0x101B6994C` | `0x187D5F0` |
| next boundary | `visible` | `0x1AB5488` | `0x1111924` | `0x101B69950` | `0x187D5F4` |

After rebuilding the first 48 bytes as independent `unsigned int` items, a
fresh entity query returns exactly twelve named size-4 globals in the V159
range on every target. The next query result is `visible`, which independently
confirms the boundary.

## UTF-16LE member-string evidence

Normal IDA string rendering is unreliable for this region: several decompiler
views reduce a wide member name to its first character (`"b"`, `"o"`, or
`"d"`). Exact UTF-16LE byte-pattern searches, including the terminating NUL,
were therefore performed in every binary and paired with the renderer xrefs.

| member | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| `meshCopy` | `0x14D55FC` | `0xD851D4` | `0x10195B9FC` | `0x174DD60` |
| `bezierPatchCopy` | `0x14D5642` | `0xD8521A` | `0x10195BA42` | `0x174DDA6` |
| `affineCopy` | `0x14D6188` | `0xD85B26` | `0x10195C726` | `0x174EA8A` |
| `setClip` | `0x14BE6FC` | `0x58F02C`, `0x590058` | `0x10195C73C` | `0x174EAA0` |
| `bufLayer` | `0x14D5866` | `0xD853D2` | `0x10195BCC4` | `0x174E028` |
| `operateMesh` | `0x14D560E` | `0xD851E6` | `0x10195BA0E` | `0x174DD72` |
| `operateBezierPatch` | `0x14D5662` | `0xD8523A` | `0x10195BA62` | `0x174DDC6` |
| `operateAffine` | `0x150733A` | `0x59006C` | `0x10195C74C` | `0x174EAB0` |
| `drawMeshFrame` | `0x14D5626` | `0xD851FE` | `0x10195BA26` | `0x174DD8A` |
| `drawBezierPatchMeshFrame` | `0x14D56B2` | `0xD8528A` | `0x10195BAB2` | `0x174DE16` |
| `drawBezierPatchFrame` | `0x14D5688` | `0xD85260` | `0x10195BA88` | `0x174DDEC` |
| `drawLine` | `0x14C1BCE` | `0xD783A0` | `0x10195C768` | `0x174EACC` |

The ARMv7 compiler places some `setClip` and `operateAffine` wide literals in
text-adjacent literal pools, hence the code-range addresses in that column.
They are exact UTF-16LE pattern hits and have direct xrefs from the expected
canvas call sites.

## Consumer matrix

| member group | build commands | canvas | accurate SLA |
| --- | :---: | :---: | :---: |
| `meshCopy`, `bezierPatchCopy`, `affineCopy` | yes | yes | yes |
| `setClip`, `bufLayer` | no | yes | no |
| `operateMesh`, `operateBezierPatch`, `operateAffine` | no | yes | no |
| four `draw*` members | no | yes | yes |

The exact xref counts differ by ISA because AArch64 and ARMv7 may materialize a
single address with two or three instructions, while iOS arm64 commonly emits
one relocatable data reference. The consumer set and member identity agree in
all four binaries; raw xref counts must not be mistaken for call counts.

## Dispatch ABI

The decompiler argument roles are consistent across all targets:

| member | operation | argc | receiver | `objthis` | result |
| --- | --- | ---: | --- | --- | --- |
| `meshCopy` | `FuncCall` | 10 | render Layer instance | same instance | null |
| `bezierPatchCopy` | `FuncCall` | 10 | render Layer instance | same instance | null |
| `affineCopy` | `FuncCall` | 14 | render Layer instance | same instance | null |
| `setClip` | `FuncCall` | 4 | `Layer` class | render Layer | null |
| `setClip` reset | `FuncCall` | 0 | `Layer` class | render Layer | null |
| `bufLayer` | `PropGet` | — | ResourceManager/SourceCache dispatch | same dispatch | non-null temporary |
| `operateMesh` | `FuncCall` | 11 | `Layer` class | render Layer | null |
| `operateBezierPatch` | `FuncCall` | 11 | `Layer` class | render Layer | null |
| `operateAffine` | `FuncCall` | 15 | `Layer` class | render Layer | null |
| `drawMeshFrame` | `FuncCall` | 5 | `Layer` class | render Layer | null |
| `drawBezierPatchMeshFrame` | `FuncCall` | 5 | `Layer` class | render Layer | null |
| `drawBezierPatchFrame` | `FuncCall` | 3 | `Layer` class | render Layer | null |
| `drawLine` | `FuncCall` | 5 | `Layer` class | render Layer | null |

All calls use flags zero. The two `setClip` shapes use the exact same pointer,
not two function-local caches. The canvas renderer also reuses that one slot at
all of its set/reset sites.

The copy and operate argument arrays match the already recovered primitive
packers:

```text
copy mesh family, argc 10:
  [src, sx, sy, sw, sh, points, divx, divy, type, clear]

operate mesh family, argc 11:
  [src, sx, sy, sw, sh, points, divx, divy, mode, opacity, clear]

affineCopy, argc 14:
  [src, sx, sy, sw, sh, useMatrix=false,
   x0, y0, x1, y1, x2, y2, type, clear]

operateAffine, argc 15:
  [src, sx, sy, sw, sh, useMatrix=false,
   x0, y0, x1, y1, x2, y2, mode, opacity, type=0]
```

## Result and argument lifetime

- Every primitive `FuncCall` passes a null result pointer, so no callback
  result Variant is constructed, reused, or destroyed for this family.
- Argument Variants are stack owners materialized before the call. Object
  arguments are ordinary Variant copies, not filtered or converted by the
  extracted helper. The surrounding renderer has already established the raw
  dispatch owners.
- Ordinary callback status values are ignored by the full render walks. In
  particular, one failed `drawLine` does not stop the remaining closing edges.
- `bufLayer` is the only result-producing member in this family. The canvas
  path owns the ResourceManager accessor, receives into a local result Variant,
  CopyRefs that result into the persistent local `bufLayer` Variant, destroys
  the property-result temporary, and only then constructs the buffer accessor.
  The three nested owners unwind in reverse order.
- The hint words themselves are process-lifetime mutable cache state. They are
  zero-initialized independently; sharing a member spelling elsewhere does not
  authorize aliasing these addresses.

## Recovery-IDB changes

For each recovery database, V159 performed the following operations:

1. undefine the exact 48-byte family range;
2. recreate twelve independent `unsigned int` items;
3. assign exact `g_motion_<member>MemberHint_guess` names;
4. attach per-item four-reference comments;
5. annotate the builder, canvas, and accurate-SLA consumer functions;
6. add the bookmark `V159 complete 12-slot renderer primitive member-hint family`;
7. invalidate and regenerate the three Hex-Rays functions;
8. verify twelve size-4 entities followed by the separate `visible` boundary;
9. save all four IDBs in place.

Saved databases:

- `out/ida-recovery/android-arm64/Kirikiroid2_1.3.9_Android_arm64-v8a.recovery.i64`
- `out/ida-recovery/android-armv7/Kirikiroid2_1.3.9_Android_armabi-v7a.recovery.i64`
- `out/ida-recovery/ios-arm64/Kirikiroid2_1.3.9_iOS_arm64.recovery.i64`
- `out/ida-recovery/ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.recovery.i64`

## Portable-source changes

- `MotionDispatch.h` now declares the twelve-slot family immediately after the
  V158 `onSync/onAction` pair and removes the old scattered four declarations.
- `RuntimeSupport.cpp` defines all twelve globals in the exact reference order.
- `PlayerRenderInternal.cpp` removes eight function-local static hint words and
  uses the matching process-global slot for every set/operate/draw call.
- Both `setClip(argc=4)` and `setClip(argc=0)` now pass
  `setClipMemberHint_guess`.
- Existing `meshCopy`, `bezierPatchCopy`, `affineCopy`, and `bufLayer` call
  sites retain their semantics but now sit in the correct source-level global
  family.

## Tests and build verification

The unit-test recorder now observes flags, member-hint address, result pointer,
receiver, `objthis`, and copied arguments. Tests cover:

- pairwise distinct identity for all twelve globals;
- exact copy/operate hint pointers;
- the shared set/reset `setClip` pointer;
- exact four draw-member pointers;
- null result pointers on the primitive calls;
- preserved receiver/`objthis` and argc contracts.

Both ordinary and `KRKR2_WASMTIME_HEADLESS=1` syntax checks pass. Both full
fresh configurations and binary-directory builds pass. Final products:

| product | bytes | imports | exports |
| --- | ---: | ---: | ---: |
| Web Debug `index.wasm` | `85,648,923` | 539 | 69 |
| Wasmtime Headless Debug `index.wasm` | `84,996,064` | 538 | 69 |

Both products increased by exactly 469 bytes from V158, as expected from
exposing eight additional process globals and the stronger test observations.
Node accepts both as valid WebAssembly modules, `llvm-objdump -h` parses all
sections, and both CTest directories return success with no registered tests.

## Next slice

V160 should begin at the adjacent `visible` slot. It must fresh-audit that
cross-consumer family rather than trust its current byte-sized recovery item or
the old source grouping. The first evidence already shows `visible` is consumed
across SeparateLayerAdaptor assignment, accurate SLA, calc-view, and draw paths.
