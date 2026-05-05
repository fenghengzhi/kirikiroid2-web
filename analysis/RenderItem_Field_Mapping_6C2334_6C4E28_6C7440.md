# Render Item Field Mapping (`0x6C2334 / 0x6C4E28 / 0x6C7440`)

## Scope
- Binary item builder: `sub_6C2334 @ 0x6C2334`
- Binary local-clip / layer-state stage: `sub_6C4E28 @ 0x6C4E28`
- Binary final execute / compose stage: `sub_6C7440 @ 0x6C7440`
- Binary child-list helpers: `sub_6C3B04 @ 0x6C3B04`, `sub_6C3C04 @ 0x6C3C04`
- Node-side source evidence used for item-byte tracing:
  - `sub_6B3C78 @ 0x6B3C78`
  - `sub_6BC4F0 @ 0x6BC4F0`

## Summary
- The binary render item is a `0x1B0` object allocated in `0x6C2334`.
- Several byte/dword fields on that object are still only semantically approximated in the Web port.
- The table below separates:
  - `Exact`: local field/step is already a direct match
  - `Folded`: local code represents the same role, but does not keep a dedicated one-byte field
  - `Gap`: local code does not yet keep a byte-for-byte equivalent

## Offset Table

| Binary item offset | Binary write / read evidence | Meaning from current evidence | Local mapping | Status |
|---|---|---|---|---|
| `+16` `BYTE` | Write: `0x6C2334` → `*(_BYTE *)(v299 + 16) = *(_BYTE *)(v288 + 201)` at `0x6C33A8`. Read: `0x6C7440` top-level gate `if (item+17 || item+16 || !item+232) skip` at `0x6C75C8`; child traversal also requires `!item+16` at `0x6C82F4`. | A dedicated per-item skip flag copied from `node+201`. It is **not** just “has render parent”. | Local runtime now carries it explicitly as `MotionNode::renderTreeFlag201 -> PreparedRenderItem::rawFlag16 -> RenderCommand::rawFlag16`, and the execute path consumes it in the same two places the native renderer does: top-level emit gating and child alpha-mask traversal gating. | `Captured` |
| `+17` `BYTE` | Write: `0x6C2334` → `*(_BYTE *)(v352 + 17) = ((preview ? 1097 : 1089) & (1 << nodeType)) == 0` at `0x6C33A0`. Read: `0x6C7440` top-level loop at `0x6C8FAC` (`LDRB [item,#0x11] ; CBNZ -> skip next item`). | “Skip this item in the top-level execute walk because this node type is masked out in the current preview/non-preview mode.” | `PreparedRenderItem::skipFlag0` stores the exact binary formula and is mirrored into `RenderCommand::rawFlag17`. The local execute loop now tests it before `buildCommandOutput()`, matching the native reader position instead of eagerly building a command that native would skip. | `Captured` |
| `+18` `BYTE` | Write: `0x6C2334` → `v298 = 1; if ((a6 & 1) == 0) v298 = node+48 != 0; *(_BYTE *)(item + 18) = v298` at `0x6C33B0..0x6C33C0`. Read: `0x6C7440` preview gate at `0x6C762C..0x6C7630` (`if (preview && !item+18) skip`). | Second execute gate bit. It depends on the recursive `a6` flag and, when that flag is clear, falls back to `node+48`. | Local runtime now carries this lineage into `PreparedRenderItem::skipFlag1` / `RenderCommand::rawFlag18`, and the top-level execute loop tests it before `buildCommandOutput()` when `_preview` is enabled, matching the native gate order. | `Captured` |
| `+19` `BYTE` | Zero-init on synthetic parent allocations (`0x6C25FC`, `0x6C2754`, `0x6C32F4`, `0x6C3774`, ...). Main write: `item+19 = node+1960 ? 1 : (a5 | node+1961) != 0` at `0x6C25D0..0x6C25D8` / `0x6C361C..0x6C3624`. Read: `0x6C4E28` first pass starts with `if (item+19)` at `0x6C5DC0`. | Main “item is eligible to enter the first render-command pass” byte. | `PreparedRenderItem::drawFlag` is the local equivalent. Synthetic/group-only entries also force this on when the binary allocates a synthetic parent. | `Exact-ish` |
| `+20` `BYTE` | Zero-init on synthetic parent allocations. Read in `0x6C4E28`: if layer-state arena exists and `!item+20`, go allocate `requireLayerId` path (`0x6C4F94..0x6C514C`). Write: `item+20 = 1` after `requireLayerId` succeeds at `0x6C5234..0x6C5240`. | “Layer id / getter state has already been resolved for this item.” | Local runtime now mirrors this into `RenderCommand::rawFlag20` when the layer object is actually allocated. | `Captured` |
| `+21` `BYTE` | Write: set `1` when clip intersection succeeds at `0x6C4F88`; set `0` when it fails at `0x6C5E6C`. `0x6C4E28` first pass skips item+19==0 entries at `0x6C5DC0` without writing this byte. Read: second pass child traversal in `0x6C7440` requires `child+21` at `0x6C82F4`; parent union in `0x6C4E28` also reads it in the second half. | Native partial-lifetime clip-valid byte: current-frame valid/invalid only when the 0x6C4E28 writer is reached; otherwise the previous item storage value survives. | `PreparedRenderItem::rawFlag21` now lives in `NativeRenderItemFields` and is restored from `renderItemNativeFieldLifetimeByNode` before the 0x6C4E28 pass. item+19==0 leaves it untouched; failed intersections write only `rawFlag21=0`; successful intersections write `rawFlag21=1` plus `clipRect`. | `Captured` |
| `+52` `DWORD` | Write: `item+52 = node+16` at `0x6C341C`. | First `requireLayerId` result copied from node. | `PreparedRenderItem::layerId` → `RenderCommand::layerId`. | `Exact` |
| `+56` `DWORD` | Write: `item+56 = node+20` at `0x6C3428`. | Second `requireLayerId` result copied from node. | `PreparedRenderItem::layerId2` → `RenderCommand::layerId2`. | `Exact` |
| `+184..196` `float[4]` | Write: from node bounds / child unions in `0x6C2334`. Read: `0x6C4E28` starts clip computation from `item+184..196`. | Paint box / current frame world AABB. | `PreparedRenderItem::paintBox` → `RenderCommand::clipRect` seed. | `Exact` |
| `+200..212` `float[4]` | Write: `item+200 = *node->1936 else invalid sentinel` at `0x6C2674`; later read in `0x6C4E28` as viewport clamp (`item+200..212`). | Viewport / clip rect inherited from `parentClipIndex` chain. | `PreparedRenderItem::viewport` + `hasViewport`. | `Exact` for normal source-backed items. Synthetic parent handling was recently corrected to stop inheriting this blindly. |
| `+232` `DWORD` | Write: `0x6C2334` loads `node+0x628` at `0x6C3608` and stores it to `item+0xE8` at `0x6C3610`. Read: `0x6C7440` top-level gate skips when zero at `0x6C75C8`; later the same value is used as the opacity argument, including the preview adjustment path at `0x6C7638..0x6C7668`. | Top-level opacity / nonzero draw gate, not a source-object gate. | `NativeRenderItemFields::opacity` / `PreparedRenderItem::opacity` is the local mapping. Source existence remains diagnostic/source-cache state, not item+232. | `Captured` |
| `+244` `DWORD` | Write: `item+244 = node+52` at `0x6C2A90` / `0x6C3618`. Read: `0x6C7440` alpha-mask path tests `(item+244 & 4)` and `(item+244 & 3) == 1`, and passes it to `Motion_doAlphaMaskOperation` at `0x6C8334..0x6C8398`. | Stencil / composite mode flags copied from runtime `node+52`. | `PreparedRenderItem::updateCount` → `RenderCommand::itemFlags`. | `Exact` |
| `+248` `tTJSVariant` | Write: `item+248 = player+1012` copy at `0x6C33CC..0x6C33FC`. Upstream source: `Player_playImpl (0x6B2284)` stores the second `Player_loadMotion` result into `player+1012`, `Player_loadMotion (0x6B0F10)` later feeds `player+1012` back into `"findMotion"` as arg0, and `0x6BF0DC` copies the same slot into child players. Read: `0x6C7440` uses it through TJS property calls (`"k"`, `"s"`, `"blendMode"`, etc.). | Per-player / per-item `findMotion` context variant carried into execute stage. | `PreparedRenderItem::contextVariant` → `RenderCommand::contextVariant`; local player slot has been renamed to `_findMotionContextVariant` to match the confirmed caller chain. | `Captured` |
| `+264` `QWORD` | Write: `item+264 = visibleAncestor->1904` or `0` at `0x6C2654`, `0x6C2B28`. Read: `0x6C7440` walks `item+264` as an ancestor chain (not a child list) around `0x6C82DC`. The child list itself lives in the `std::vector<item*>` at item `+24`, populated by `sub_6C3B04 / sub_6C3C04`. | Parent item pointer. Child list is a separate structure at `+24`. | Local runtime now mirrors this in two stages: `PreparedRenderItem::parentItem/childItems` is rebuilt after sort, then `RenderCommand::parentCommand/childCommandPtrs` is derived from those prepared-item pointers. `parentNodeIndex` remains only as a trace/debug aid. | `Captured (semantic)` |
| `+280` `DWORD` | Write: `item+280 = node+2000` at `0x6C2684`, then refined to `0/1/2` depending on mesh data. Read: `0x6C4E28` and `0x6C7440` branch on it for `affineCopy / bezierPatchCopy / meshCopy`. | Render geometry mode. | `PreparedRenderItem::meshType` → `RenderCommand::meshType`. | `Exact` |
| `+304` `tTJSVariant` | Write: `sub_A0FB64(item+304, layerVariant)` after `sub_6C6B48` at `0x6C533C`. Read: `0x6C7440` re-opens it as a layer object before `setSize/fillRect/copy`, and child alpha-mask branch chooses `+304` when `(item+244 & 4) == 0`. | First per-item layer-object variant (local / leaf output). | Local equivalent is `RenderCommand::leafLayer`, now allocated through the `requireLayerId -> sub_6C6B48` style leaf path instead of sharing the composed-layer allocation boundary. | `Captured` |
| `+320` `DWORD` | No independent business write found. In the binary layout, `+304` and `+324` are 20 bytes apart, matching the ARM64 `tTJSVariant` footprint (`union payload + type tag`). `0x6C7440` behavior is consistent with `+320` being the type/tag word for the variant stored at `+304`, not a separate render-mode field. | Internal tag/type for the `tTJSVariant` at `+304`. | Local equivalent is `command.leafLayer.Type()`, not a standalone field. | `Folded` |
| `+324` `tTJSVariant` | `0x6C7440` chooses `child+324` instead of `child+304` when `(child+244 & 4) != 0` in the alpha-mask traversal around `0x6C8334`, and later creates/uses a second layer object for composed output. | Second per-item layer-object variant (composed / post-child-composition output). | Local equivalent is `RenderCommand::composedLayer`, now kept on a separate allocation/reuse boundary from `leafLayer`; selection also follows `(item+244 & 4)` instead of `composedBuilt`. | `Captured` |
| `+340` `DWORD` | Same 20-byte spacing argument as `+320`: it sits 16 bytes after `+324`, consistent with the tag/type word inside a second `tTJSVariant`. Earlier decompiler snippets that checked `*(_DWORD *)(item+340)` before allocating a second layer therefore match “composed variant already initialized?” rather than a separate business flag. | Internal tag/type for the `tTJSVariant` at `+324`. | Local equivalent is `command.composedLayer.Type()`. | `Folded` |

## Player-Side Gate Bits

### `player+1096`
- `0x6D9648` getter returns `*(BYTE*)(player+1096)`
- `0x6D9650` setter writes `*(BYTE*)(player+1096) = arg & 1`
- `0x6CED30` reset clears `*((WORD*)player + 548) = 0`, i.e. offsets `1096/1097`
- This matches the local `Player::_preview` field
- Therefore the `+18` gate in `0x6C7440` is preview-conditional, not unconditional

## Node-Side Inputs That Still Need Explicit Local Mapping

### `node+200`
- `0x6BC4F0` reads `node+200` at `0x6BC7F0`
- `0x6C2334` reads `node+200` at `0x6C32C8`
- Current local comment that labels this as only `anchorEnabled` is too narrow
- This byte is definitely part of the render-tree / mesh-combine path and still needs a dedicated local name

### `node+201`
- `0x6C2334` copies `node+201 -> item+16` at `0x6C33A8`
- `0x6BC4F0` also reads it directly at `0x6BD044` (`LDRB W9, [X23,#0xC9]`).
- `0x6F468C` copies `node+200/+201` together via
  `*(_WORD *)(dst + 200) = *(_WORD *)(src + 200)` at `0x6F4714`.
- Current-turn review of `Player_ctor (0x6CED30)`, `sub_6F4E90`, `sub_6F4F5C`,
  `sub_6F19B4`, `sub_699390`, `Player_initNodeFields (0x6B3C78)`, and
  `Player_buildNodeTree_recursive (0x6B4A6C)` still found no standalone initial
  writer for `node+201`.
- Current-turn review of deque move/rebalance helpers `sub_6F426C`, `sub_6F436C`,
  `sub_6F4470`, and `sub_6F3E0C` found only `0x6F468C`-based copy propagation,
  not a standalone origin writer.
- A previous local reading conflated one nearby `0x6BC4F0` branch with `node+201`,
  but the current-turn disassembly at `0x6BCE2C` is actually `LDR W8, [X23,#0x7CC]`
  (`node+1996`, forceVisible).
- Current sweep over the motionplayer-relevant address range `0x6B0000..0x6E0000`
  found no direct standalone `#0xC9` byte writer, only these readers plus the
  `0x6F468C` word-copy. For the current sample and reachable node lifecycle,
  `node+201` should be treated as a default-zero bit that propagates through
  native node copies, not as a field with its own standalone init writer.
- Current local runtime now keeps a dedicated field for this byte:
  `MotionNode::renderTreeFlag201` -> `PreparedRenderItem::rawFlag16` ->
  `RenderCommand::rawFlag16`.

## Current Local Status
- Recently aligned:
  - `layerId/layerId2`
  - `paintBox`
  - `viewport`
  - `itemFlags(+244)`
  - `meshType(+280)`
  - `+16/+17/+18/+19/+20/+21` as explicit native fields
  - `+216..228` partial-lifetime build clip storage
  - synthetic parent item allocation / child list structure
- Still not byte-for-byte aligned:
  - `+264` (semantic pointer-chain mapping only)
  - `+304/+320`
  - `+324/+340`

## Structural Implication
- The `0x6C4E28`/`0x6C7440` gates around `+16/+17/+18/+19/+21` make it clear the
  native renderer is not just “one flat command array rendered top-to-bottom”.
- In particular:
  - `+19` decides whether an item enters the first pass at all
  - `+17` and `+18` further gate which items may be emitted as direct top-level outputs
  - `+21` then controls whether child items are eligible to participate in later
    composed/alpha-mask traversal
- Special type12 composite parents have an extra structure rule:
  - `0x6C37D0` pushes the parent item to the auxiliary list, not the main top-level walk
  - `0x6C37E4` seeds that same item into its own `item+24` child vector
  - `0x6C3898` appends active `nodeType==0` child items directly into that vector
  - `0x6C3924` does the same for active `nodeType==3` child items only when preview is enabled
  - `0x6F3424` range-inserts the child item's own `item+24` vector when preview is disabled
- This means the current Web port's single `renderCommands` vector is still an
  architectural simplification over the native split-pass behavior, even after the
  recent synthetic-parent fixes.

## Next Concrete Alignment Steps
1. Continue replacing semantic pointer-chain shortcuts around `+264` with the native item+24 / item+264 container boundaries.
2. Trace and name the `+320` / `+340` variant tag reads before changing any remaining alpha-mask/group behavior.
3. Keep render-stage diagnostics reporting `+21` and `+216..228` as native lifecycle fields, not derived per-frame booleans.
