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
  - `Gap`: local code does not yet preserve the source-level member, dataflow, or lifecycle
- Offsets in this file are Android ARM64 evidence coordinates. They do not require
  the wasm32 C++ object to reproduce the same ABI byte layout.

## Offset Table

| Binary item offset | Binary write / read evidence | Meaning from current evidence | Local mapping | Status |
|---|---|---|---|---|
| `+0` `ttstr` | `sub_6C2334` copies the owning node label into ordinary items at `0x6C3348..0x6C3374` and type3 wrappers at `0x6C27B4..0x6C27DC` on every population; `sub_6F4DFC` releases this owner last. | Independent owner-label string; it is not a borrowed node pointer and is not initialized merely once at allocation. | `PreparedRenderItem::ownerLabel`, refreshed on every ordinary/wrapper population. Synthetic placeholders remain default-empty until their own population path runs. | `Captured` |
| `+8` `ttstr` | `0x6C35A8..0x6C35F8` copies the active clip-slot source string into the item; `sub_6F4DFC` releases it independently from `+0`. | Per-command source string owner. | `PreparedRenderItem::commandSrc`; the Web texture/source object remains a separate platform payload. | `Captured` |
| `+16` `BYTE` | Write: `0x6C2334` → `*(_BYTE *)(v299 + 16) = *(_BYTE *)(v288 + 201)` at `0x6C33A8`. Read: `0x6C7440` top-level gate `if (item+17 || item+16 || !item+232) skip` at `0x6C75C8`; child traversal also requires `!item+16` at `0x6C82F4`. | A dedicated per-item skip flag copied from `node+201`. It is **not** just “has render parent”. | `MotionNode::renderTreeFlag201 -> PreparedRenderItem::rawFlag16`; build 与 execute 都直接读写同一持久 item，没有中间 command 镜像。 | `Captured` |
| `+17` `BYTE` | Write: `0x6C2334` → `*(_BYTE *)(v352 + 17) = ((preview ? 1097 : 1089) & (1 << nodeType)) == 0` at `0x6C33A0`. Read: `0x6C7440` top-level loop at `0x6C8FAC` (`LDRB [item,#0x11] ; CBNZ -> skip next item`). | “Skip this item in the top-level execute walk because this node type is masked out in the current preview/non-preview mode.” | `PreparedRenderItem::skipFlag0` stores the exact formula；execute 在同一 item 上、进入提交分支前读取。 | `Captured` |
| `+18` `BYTE` | Write: `0x6C2334` → `v298 = 1; if ((a6 & 1) == 0) v298 = node+48 != 0; *(_BYTE *)(item + 18) = v298` at `0x6C33B0..0x6C33C0`. Read: `0x6C7440` priorDraw gate at `0x6C7624..0x6C7630` (`if (player+1096 && !item+18) skip`). | Second execute gate bit. It depends on the recursive `a6` flag and, when that flag is clear, falls back to `node+48`. | `PreparedRenderItem::skipFlag1` 直接保存原始极性并由 execute 在 `_priorDraw` 下读取；不存在第二份 `RenderCommand` 状态。 | `Captured` |
| `+19` `BYTE` | Zero-init on synthetic parent allocations (`0x6C25FC`, `0x6C2754`, `0x6C32F4`, `0x6C3774`, ...). Main write: `item+19 = node+1960 ? 1 : (a5 | node+1961) != 0` at `0x6C25D0..0x6C25D8` / `0x6C361C..0x6C3624`. Read: `0x6C4E28` first pass starts with `if (item+19)` at `0x6C5DC0`. | Main “item is eligible to enter the first render-command pass” byte. | `PreparedRenderItem::drawFlag` is the local equivalent. Synthetic/group-only entries also force this on when the binary allocates a synthetic parent. | `Exact-ish` |
| `+20` `BYTE` | Zero-init on synthetic parent allocations. Read in `0x6C4E28`: if layer-state arena exists and `!item+20`, go allocate `requireLayerId` path (`0x6C4F94..0x6C514C`). Write: `item+20 = 1` after `requireLayerId` succeeds at `0x6C5234..0x6C5240`. | “Layer id / getter state has already been resolved for this item.” | `PreparedRenderItem::rawFlag20` 在实际取得 layer id 时原地置位。 | `Captured` |
| `+21` `BYTE` | Write: set `1` when clip intersection succeeds at `0x6C4F88`; set `0` when it fails at `0x6C5E6C`. `0x6C4E28` first pass skips item+19==0 entries at `0x6C5DC0` without writing this byte. Read: second pass child traversal in `0x6C7440` requires `child+21` at `0x6C82F4`; parent union in `0x6C4E28` also reads it in the second half. | Native partial-lifetime clip-valid byte: current-frame valid/invalid only when the 0x6C4E28 writer is reached; otherwise the persistent item retains its previous value. | `PreparedRenderItem::rawFlag21` lives on the persistent item owned by `MotionNode::preparedRenderItem`. item+19==0 leaves it untouched; failed intersections write only `rawFlag21=0`; successful intersections write `rawFlag21=1` plus `clipRect`. The old `renderItemNativeFieldLifetimeByNode` / `nativeLifetime*` side map has been deleted. | `Captured` |
| `+52` `DWORD` | Write: `item+52 = node+16` at `0x6C341C`. | First `requireLayerId` result copied from node. | `PreparedRenderItem::layerId`. | `Exact` |
| `+56` `DWORD` | Write: `item+56 = node+20` at `0x6C3428`. | Second `requireLayerId` result copied from node. | `PreparedRenderItem::layerId2`. | `Exact` |
| `+184..196` `float[4]` | Write: from node bounds / child unions in `0x6C2334`. Read: `0x6C4E28` starts clip computation from `item+184..196`. | Paint box / current frame world AABB. | `PreparedRenderItem::paintBox`，build 由此原地计算 `clipRect`。 | `Exact` |
| `+200..212` `float[4]` | Write: `item+200 = *node->1936 else invalid sentinel` at `0x6C2674`; later read in `0x6C4E28` as viewport clamp (`item+200..212`). | Viewport / clip rect inherited from `parentClipIndex` chain. | `PreparedRenderItem::viewport` + `hasViewport`. | `Exact` for normal source-backed items. Synthetic parent handling was recently corrected to stop inheriting this blindly. |
| `+232` `DWORD` | Write: `0x6C2334` loads `node+0x628` at `0x6C3608` and stores it to `item+0xE8` at `0x6C3610`. Read: `0x6C7440` top-level gate skips when zero at `0x6C75C8`; later the same value is used as the opacity argument, including the priorDraw half-opacity path at `0x6C7638..0x6C7668`. | Top-level opacity / nonzero draw gate, not a source-object gate. | `NativePreparedRenderItemState::opacity` / `PreparedRenderItem::opacity`. Source existence remains platform/source-cache state, not item+232. | `Captured` |
| `+244` `DWORD` | Write: `item+244 = node+52` at `0x6C2A90` / `0x6C3618`. Read: `0x6C7440` alpha-mask path tests `(item+244 & 4)` and `(item+244 & 3) == 1`, and passes it to `Motion_doAlphaMaskOperation` at `0x6C8334..0x6C8398`. | Stencil / composite mode flags copied from runtime `node+52`. | `PreparedRenderItem::stencilComposite`; execute paths consume this field directly. The old `updateCount` name has been removed. | `Exact` |
| `+248` `ttstr` | `0x6C33CC..0x6C33FC` first coerces `player+1012` to string, then CopyRefs it into the item. Upstream, `Player_playImpl (0x6B2284)` and `Player_loadMotion (0x6B0F10)` maintain the `player+1012` findMotion context. `sub_6F4DFC` releases the resulting item string. | Per-item command key string derived from the findMotion context; it is not itself a `tTJSVariant`. | `PreparedRenderItem::commandKey`, sourced from `Player::_findMotionContextVariant`. | `Captured` |
| `+256` `QWORD` | `sub_6C4E28 @ 0x6C5BA8..0x6C5BB0` and `sub_6C7440 @ 0x6C8E08..0x6C8E10` dereference this pointer, then load the embedded source descriptor's `+0x20/+0x28` doubles. | Non-owning reachability to the node's persistent source descriptor; Bezier subdivision must use its logical width/height rather than the resolved Layer bitmap or atlas rectangle. | `PreparedRenderItem::nativeNode->source.width/height`. This is a semantic pointer relationship, not an attempt to preserve the ARM64 byte offset in wasm. | `Captured (semantic)` |
| `+264` `QWORD` | Write: `item+264 = visibleAncestor->1904` or `0` at `0x6C2654`, `0x6C2B28`. Read: `0x6C7440` walks `item+264` as an ancestor chain (not a child list) around `0x6C82DC`. The child list itself lives in the `std::vector<item*>` at item `+24`, populated by `sub_6C3B04 / sub_6C3C04`. | Parent item pointer. Child list is a separate structure at `+24`. | Local runtime writes `PreparedRenderItem::parentItem` directly while the main item is populated, allocating only the ancestor's persistent node-owned item when needed. The type12 post-pass modifies only its own `childItems`; it does not infer or rewrite child parent pointers. | `Captured (semantic)` |
| `+272/+276` `DWORD[2]` | `sub_6C2334 @ 0x6C2688..0x6C2694` copies `node+2012/+2016` unchanged. `sub_6C4E28 @ 0x6C5864/0x6C5874` and `sub_6C7440` pass the same values as `meshCopy/operateMesh` `divx/divy`. | Horizontal/vertical **cell counts**, not point counts. `sub_6BAF68 @ 0x6BAF94..0x6BAFA0` owns `(divx+1)*(divy+1)` points with row stride `divx+1`; `sub_69DC04 @ 0x69DD38..0x69DF3C` builds `divx+1`/`divy+1` boundaries and `divx*divy` cells. | `PreparedRenderItem::meshDivX/meshDivY`; all Layer, D3D and PrivateMotionGLL consumers keep the values unchanged and derive point dimensions with `+1`. | `Captured` |
| `+280` `DWORD` | Write: `item+280 = node+2000` at `0x6C2684`, then refined to `0/1/2` depending on mesh data. Read: `0x6C4E28` and `0x6C7440` branch on it for `affineCopy / bezierPatchCopy / meshCopy`. | Render geometry mode. | `PreparedRenderItem::meshType`. | `Exact` |
| `+304` `tTJSVariant` | Write: `sub_A0FB64(item+304, layerVariant)` after `sub_6C6B48` at `0x6C533C`. Read: `0x6C7440` re-opens it as a layer object before `setSize/fillRect/copy`, and child alpha-mask branch chooses `+304` when `(item+244 & 4) == 0`. | First per-item layer-object variant (local / leaf output). | `PreparedRenderItem::leafLayer`，由 build 原地写、execute 原地读。 | `Captured` |
| `+320` `DWORD` | No independent business write found. In the binary layout, `+304` and `+324` are 20 bytes apart, matching the ARM64 `tTJSVariant` footprint (`union payload + type tag`). `0x6C7440` behavior is consistent with `+320` being the type/tag word for the variant stored at `+304`, not a separate render-mode field. | Internal tag/type for the `tTJSVariant` at `+304`. | `PreparedRenderItem::leafLayer.Type()`；不是独立源码成员。 | `Captured as variant internals` |
| `+324` `tTJSVariant` | `0x6C7440` chooses `child+324` instead of `child+304` when `(child+244 & 4) != 0` in the alpha-mask traversal around `0x6C8334`, and later creates/uses a second layer object for composed output. | Second per-item layer-object variant (composed / post-child-composition output). | `PreparedRenderItem::composedLayer`，与 `leafLayer` 保持独立 owner，选择仍按 `(item+244 & 4)`。 | `Captured` |
| `+340` `DWORD` | Same 20-byte spacing argument as `+320`: it sits 16 bytes after `+324`, consistent with the tag/type word inside a second `tTJSVariant`. Earlier decompiler snippets that checked `*(_DWORD *)(item+340)` before allocating a second layer therefore match “composed variant already initialized?” rather than a separate business flag. | Internal tag/type for the `tTJSVariant` at `+324`. | `PreparedRenderItem::composedLayer.Type()`；不是独立源码成员。 | `Captured as variant internals` |
| `+344` `std::vector<MeshPoint>` | `sub_6C2334 @ 0x6C2684..0x6C2714` assigns `node+2048` on every ordinary population; `Player_applyTranslateOffset @ 0x6D52B8..0x6D533C` translates this vector for every item. | Composite/deformed grid owner used by geometry mode 2. Its cardinality is `(item+272+1)*(item+276+1)`. Assignment on every population also clears stale contents when the node vector is empty. | `PreparedRenderItem::commandCompositeMeshPoints`, copied from `MotionNode::compositeMeshPoints`. | `Captured` |
| `+368` `DWORD` | Type-1 branch derives the patch subdivision total from the Player mesh ratio and node division, then clamps it to 50. Leaf build `sub_6C4E28 @ 0x6C5C00..0x6C5C34` uses `SCVTF/FADD/FMUL/FDIV/FCVTZS` and dimensions through item+256. Direct `sub_6C7440 @ 0x6C8E5C..0x6C8EEC` and PrivateMotionGLL use a uint32 pipeline with the same persistent source dimensions. Accurate SLA `sub_6C9CA8 @ 0x6CA494..0x6CA4BC/0x6CA904..0x6CA97C` also uses uint32 arithmetic, but takes integer width/height from the resolved source Layer. | Bezier patch division total; not the composite `+272/+276` pair. The FP leaf-build and uint32 execution formulas agree for ordinary integral dimensions but intentionally retain different malformed/fractional/overflow boundaries; accurate SLA additionally has a distinct source-dimension owner. | `PreparedRenderItem::commandPatchDivision`; `bezierPatchCellDivisionsLike_0x6C5C00` is restricted to the `0x6C4E28` leaf path. `bezierPatchCellDivisionsU32Like_0x6C8E5C` serves direct/Private with `nativeNode->source` and accurate SLA with resolved source dimensions. | `Captured` |
| `+376` `std::vector<MeshPoint>` | In the type-1/raw-present branch, `sub_6C2334` assigns `node+2024`; the later camera-offset pass deliberately does not translate this vector. | Raw 4x4 Bezier control patch owner. | `PreparedRenderItem::commandBezierPatchPoints`, copied from `MotionNode::meshControlPoints`. | `Captured` |
| `+400` `std::vector<MeshPoint>` | In the same type-1 branch, `sub_6C2334` assigns `node+2072`; `Player_applyTranslateOffset @ 0x6D5348..0x6D5388` translates it only when `item+280==1`. | Own-affine-transformed patch owner used by the type-1 render path. | `PreparedRenderItem::meshPoints`, copied from `MotionNode::transformedMeshControlPoints`. | `Captured` |
| `+424` `DWORD` | `sub_6C4E28 @ 0x6C51CC..0x6C5240` calls zero-argument `requireLayerId`, stores the result here, then sets item+20. | Third per-item leaf layer id, latched in execute/pre-walk rather than item construction. | `PreparedRenderItem::renderLayerId`, written only when `rawFlag20` flips. | `Captured` |

## Player-Side Gate Bits

### `player+1092` (`preview`)
- NCB member literal `"preview"` binds getter `0x6D9634` and setter `0x6D963C`
- Getter reads `*(BYTE*)(player+1092)`; setter writes `arg & 1`
- This maps to local `Player::_preview`
- It controls preview-specific node/build behavior, but it is **not** the
  `0x6C7440` item `+18` gate

### `player+1096` (`priorDraw`)
- NCB member literal `"priorDraw"` binds getter `0x6D9648` and setter `0x6D9650`
- Getter/setter read/write the independent one-byte bool at `player+1096`
- `0x6C74E8` clears the player+864 draw region only when this byte is zero
- `0x6C7440 @ 0x6C7624..0x6C7630` gates on this byte and skips when
  `item+18==0`; `0x6C764C..0x6C7668` also halves submitted opacity when set
- `Player_RenderMotionFrame @ 0x6DE738` also consumes this Player-level flag when filtering/adjusting PrivateMotionGLL queue items
- This field is distinct from per-node `node+48 priorDraw`

### `player+1144` (`completionType`)
- NCB member literal `"completionType"` binds getter `0x6D9624` and setter `0x6D962C`
- It is a 32-bit int, independent from the `player+1092 preview` render/timeline gate

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
  `MotionNode::renderTreeFlag201` -> `PreparedRenderItem::rawFlag16`；build 与
  execute 直接消费同一持久 item。

## Current Local Status
- Recently aligned:
  - independent `ownerLabel(+0)` and `commandSrc(+8)` ttstr owners
  - `layerId/layerId2`
  - `paintBox`
  - `viewport`
  - `stencilComposite(+244)`
  - `meshType(+280)`
  - three independent mesh vectors at `+344/+376/+400` and division at `+368`
  - execute-time `renderLayerId(+424)` latch
  - `+16/+17/+18/+19/+20/+21` as explicit native fields
  - `+216..228` partial-lifetime build clip storage
  - persistent node-owned item allocation, type3 Branch A `+264`, and type12 child-list structure
- Source-structure/lifecycle closure:
  - `+304/+320` 与 `+324/+340` 已分别还原为两只 `tTJSVariant` owner 及其内部 type tag，不另造源码字段
  - `NativePreparedRenderItemState` 的声明顺序对应 `sub_6F4DFC` 的逆序析构链
  - Web 平台附加状态放在派生 `PreparedRenderItem`，先于 native semantic base 析构
  - main/aux 指针表是每个 draw caller 的栈局部量，不属于 `Player`

## Structural Implication
- The `0x6C4E28`/`0x6C7440` gates around `+16/+17/+18/+19/+21` make it clear the
  native renderer is not just “one flat command array rendered top-to-bottom”.
- In particular:
  - `+19` decides whether an item enters the first pass at all
  - `+17` and `+18` further gate which items may be emitted as direct top-level outputs
  - `+21` then controls whether child items are eligible to participate in later
    composed/alpha-mask traversal
- Special type12 composite parents have an extra structure rule:
  - a node reaching this pass already set `drawnThisFrame` and pushed the same item into the main list at `0x6C30FC`
  - `0x6C37D0` additionally pushes that same persistent parent item into the auxiliary list
  - `0x6C37E4` seeds that same item into its own `item+24` child vector
  - `0x6C3898` appends active `nodeType==0` child items directly into that vector
  - `0x6C3924` does the same for active `nodeType==3` child items only when preview is enabled
  - `0x6F3424` range-inserts the child item's own `item+24` vector when preview is disabled
- The current port creates separate `PreparedRenderItemList mainList/auxList` in each
  draw caller. `prepareRenderItems(main, aux)` populates both and sorts only main;
  translate walks only main; `buildRenderCommands(..., main, aux)` consumes both;
  final execute receives main. Both vectors die when the caller returns, matching the
  native call-scoped container lifetime.

## Next Concrete Alignment Steps
1. Continue tracing the remaining `0x6C7440` direct/composed gates and D3D-specific boundaries without converting ARM64 offsets into wasm object-layout requirements.
2. Keep `+21` and `+216..228` on the persistent item itself; do not reintroduce `nativeLifetime*` side maps.
