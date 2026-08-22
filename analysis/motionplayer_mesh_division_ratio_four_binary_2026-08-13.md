# `meshDivisionRatio` four-reference audit — 2026-08-13

## Result

`Motion.Player`, `Motion.EmotePlayer`, and `D3DEmotePlayer` expose the same
per-`Player` `double` through three independently generated NCB member tables.
Every setter is a raw store and every getter is a raw load. There is no clamp,
finite check, dirty flag, rebuild call, or propagation into `EmoteEngine`.

`EmoteEngine` owns a separate pair of doubles used by its metadata/controller
scale flow. On Android arm64 that pair happens to occupy `+1168/+1176`, while
the adjacent Player `speed/meshDivisionRatio` pair also occupies
`+1168/+1176`. An earlier analysis matched the numbers without preserving the
receiver identity and incorrectly routed wrapper writes and one geometry path
to the Engine pair. The current four binaries disprove that routing.

The source-level data flow is:

```text
Motion.Player.meshDivisionRatio --------------------+
Motion.EmotePlayer.meshDivisionRatio -> Player* ----+--> Player scalar
D3DEmotePlayer.meshDivisionRatio -> EmoteObject
  -> EmoteEngine -> Player* -------------------------+

EmoteEngine metadata "scale" -> Engine scale
scale controller -------------> Engine reciprocal scale
                                      (independent state)
```

## Registration and accessor evidence

### Android arm64

- Player string: `0x14BE9DA`; registration use: `0x6D4668`.
- Player getter/setter: `0x6D6A4C` / `0x6D6A54`.
  They load/store `Player+1176` (`0x498`).
- EmotePlayer registration use: `0x67D940`; getter/setter:
  `0x67F1BC` / `0x67F1C8`. The chain is
  `EmotePlayer+1064 -> Player+1176`.
- D3DEmotePlayer registration use: `0x52ED00`; getter/setter:
  `0x530484` / `0x530498`. The chain is
  `shell+24 -> EmoteObject+8 -> EmoteEngine+1064 -> Player+1176`.
- Player constructor writes exact double `1.0` at `0x6CC540`.

### Android armv7

- Player string: `0xD7680E`; registration use: `0x5980CE`.
- Player getter/setter: `0x598E96` / `0x598EA0`; field `Player+832`
  (`0x340`).
- EmotePlayer registration use: `0x561570`; getter/setter:
  `0x561F88` / `0x561F96`; chain `EmotePlayer+532 -> Player+832`.
- D3DEmotePlayer registration use: `0x494196`; getter/setter:
  `0x494A34` / `0x494A46`; chain
  `shell+16 -> EmoteObject+4 -> EmoteEngine+532 -> Player+832`.
- Player constructor writes exact double `1.0` at `0x593890`.

### iOS arm64

- Player string: `0x10195CAC4`; registration use: `0x1001247E8`.
- Player getter/setter: `0x100125590` / `0x100125598`; field
  `Player+1064` (`0x428`).
- EmotePlayer string/use: `0x10196046E` / `0x1001B54D0`;
  getter/setter `0x1001B6060` / `0x1001B606C`; chain
  `EmotePlayer+696 -> Player+1064`.
- D3DEmotePlayer string/use: `0x10196FD58` / `0x10023240C`;
  getter/setter `0x100232E5C` / `0x100232E70`; chain
  `shell+24 -> EmoteObject+8 -> EmoteEngine+696 -> Player+1064`.
- Player constructor writes exact double `1.0` at `0x10011EEE4`.

### iOS armv7

- Player string: `0x174EE28`; registration uses include `0x123AF6`.
- Player getter/setter: `0x12478E` / `0x124798`; field `Player+764`
  (`0x2FC`).
- EmotePlayer string/use: `0x17527D2` / `0x1B5148`; getter/setter
  `0x1B5DB8` / `0x1B5DC6`; chain `EmotePlayer+348 -> Player+764`.
- D3DEmotePlayer string/use: `0x1762104` / `0x2310B8`;
  getter/setter `0x231AC0` / `0x231AD2`; chain
  `shell+16 -> EmoteObject+4 -> EmoteEngine+348 -> Player+764`.
- Player constructor writes exact double `1.0` at `0x11D966`.

## Player field consumers

The public value is not merely an inert property. All four references use it
in the same Player-side mesh-division stages:

| Stage | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| updateLayers mesh tessellation | `0x6BA308` and three later loads | `0x586E00` and three later loads | `0x10010FF04` and three later loads | `0x10D362` and three later loads |
| prepared render item division | `0x6BFAC4` | `0x58B67A` | `0x100114F00` | `0x11291A` |
| calcViewParam division | `0x6CF83C` | `0x594DCA` | `0x10012066C` | `0x11F3E8` |
| getCommandList division | `0x6D1720` | `0x59655E` | `0x1001222D0` | `0x1211DC` |

The critical Android-arm64 updateLayers sequence first loads `[Player+0]` and
then loads `[that+0x498]`. `Player+0` is the constructor-initialized self
pointer, not an `EmoteEngine*`. The corresponding Android-armv7 helper at
`0x585434` has the same receiver shape: dereference `[arg0]`, multiply the
`Player+832` double by a `uint32`, and convert back to `uint32`. Its following
cap compares that word as **signed**, so sign-bit-set results bypass 50; the
helper is now named `Player_scaleOwnMeshDivisionSignedCap_guess`.

The exact downstream floating-to-integer instruction varies by branch
(`FCVTZU`, `FCVTZS`, or ARMv7 VFP equivalents). This audit changes only the
source scalar and receiver identity. It does not add setter-side normalization;
NaN, infinities, negative zero, and negative finite values remain stored
unchanged until a particular consumer performs its native conversion.

The `getCommandList` consumer is now closed separately in
[`motionplayer_get_command_list_division_conversion_four_binary_2026-08-14.md`](motionplayer_get_command_list_division_conversion_four_binary_2026-08-14.md).
It proves signed-int64 conversion followed by an ordered-`< 50` select, including
the otherwise easy-to-miss rule that unordered NaN serializes Integer `50`.
The two ARMv7 plugins call external ABI helpers, so their negative-overflow
result is explicitly kept separate from plugin-contained MotionPlayer code.

The immediately preceding prepared-item producer is closed in
[`motionplayer_prepared_bezier_division_conversion_four_binary_2026-08-14.md`](motionplayer_prepared_bezier_division_conversion_four_binary_2026-08-14.md).
It proves unsigned interpretation of the node's raw 32-bit division, an
in-plugin signed-int32 saturating conversion, and an integer cap. Together the
two records show that command serialization loads and multiplies the Player
ratio a second time rather than consuming a once-normalized public scalar.

The independent `calcViewParam` mesh-chain exporter is closed in
[`motionplayer_calc_view_division_conversion_four_binary_2026-08-14.md`](motionplayer_calc_view_division_conversion_four_binary_2026-08-14.md).
It also interprets the node slot as unsigned, but converts the product to
**unsigned** int32 and caps that converted integer. Thus NaN and every negative
product export zero, unlike both prepared-item signed output and command-list
ordered-select output. It walks nodes directly and multiplies the ratio once;
it does not consume the prepared field.

The geometry consumer inside `updateLayers` is closed in
[`motionplayer_update_layers_mesh_division_compare_domain_four_binary_2026-08-14.md`](motionplayer_update_layers_mesh_division_compare_domain_four_binary_2026-08-14.md).
It proves another branch-local distinction: own type-1 paths perform unsigned
conversion followed by a **signed** cap comparison, while inherited-source
paths use unsigned comparison. Thus positive overflow is retained as
`UINT32_MAX` only for own meshes and can wrap into negative signed grid counts.

## Independent Engine pair

The separate `EmoteEngine` values retain their existing roles:

- metadata application loads the first from metadata `base.scale`;
- the scale-controller step recomputes the second as
  `1 / (metadataScale * controllerScale)` without a zero/finite guard;
- wind setup divides its geometry/amplitude by the first;
- shape-anchor resolution uses the second.

No public `meshDivisionRatio` accessor in any of the three member tables
reaches either Engine value. In particular, the native setters do not write
both Engine doubles and do not force them equal.

## Port corrections

- `EmotePlayer::{get,set}MeshDivisionRatio` now forwards only to its embedded
  Player.
- `D3DEmotePlayer::{get,set}MeshDivisionRatio` now traverses its owned object
  chain and forwards only to that Player.
- updateLayers geometry now consumes `Player::getMeshDivisionRatio()` instead
  of the Engine reciprocal-scale value.
- Removed the invented `Player::setEmoteMeshDivisionRatio` and
  `Player::meshDivisionRatioDupLike_0x6BCF3C` bridges.
- Replaced stale compiled-source comments containing Android-arm64-only
  addresses with cross-reference semantic descriptions.
- Renamed and commented all 24 property accessor functions in the four IDBs;
  the two Android inline-like cap helpers were also named. All four IDBs were
  saved.

## Regression coverage

The unit-test translation unit now verifies:

- the Player and EmotePlayer views share one raw double;
- typed NCB round-trips preserve negative infinity, negative finite values,
  negative zero, positive zero, finite maximum, positive infinity, and NaN;
- wrapper writes leave both Engine scale values unchanged;
- writes through the Player view are immediately visible through EmotePlayer;
- a loaded D3DEmotePlayer's typed property and direct embedded-Player view are
  bidirectionally identical.

## Validation

- Full `motionplayer-dll.cpp` Catch2 translation-unit Emscripten
  `-fsyntax-only`: passed. The only diagnostic was the existing deprecated
  whitespace before the `_tss` literal-operator suffix.
- `cmake --build out/web/debug -- -j 6`: passed through final link and
  `sync_prealloc_memory`.
- `cmake --build out/wasmtime/debug -- -j 6`: passed through final link and
  `sync_prealloc_memory`.
- Immediate reruns of both build trees reported `ninja: no work to do.`
- `git diff --check`: exit code 0. Its output contained only the repository's
  existing LF-to-CRLF working-copy warnings and no whitespace errors.
