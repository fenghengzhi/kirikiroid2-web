---
name: Render Executor Architecture (sub_6C7440 / sub_6C4E28 / sub_6C2334)
description: Complete architecture of libkrkr2.so render executor — mainList vs auxList, parent-child relationship via item+264 (ancestor chain, NOT child list), composed aggregation only for type12/nodeType=3, why moji_y letter children render as flat top-level items
type: project
---

# libkrkr2.so Render Executor — Complete Architecture

Function addresses:
- `sub_6C2334 @ 0x6C2334` — build render item tree (flat mainList + special auxList)
- `sub_6C4E28 @ 0x6C4E28` — render each item's source into its own leafLayer (item+304), then aux pass for type12 composite parents composes children into composedLayer (item+324)
- `sub_6C7440 @ 0x6C7440` — final composite loop over mainList: either direct affineCopy to renderLayer OR via scratch "bufLayer" with alpha-mask chain walk

## Top-Level Call Site — Player_drawCompat @ 0x6D5FB8

```asm
6d60c4  STP  XZR, XZR, [X29,#var_98]    // zero mainList struct (begin/end/cap=3 ptrs + pad)
6d60cc  STP  XZR, XZR, [SP,#var_B0]
6d60d0  STR  XZR, [SP,#p]
6d60d4  SUB  X1, X29, #-var_A0          // X1 = &mainList (3-ptr std::vector<item*>)
6d60d8  ADD  X2, SP, #p                 // X2 = &auxList (3-ptr std::vector<item*>)
6d60dc  MOV  X0, X19                    // X0 = player
6d60e0  BL   sub_6D5164                  // build + sort
...
6d61f8  SUB  X1, X29, #-var_A0
6d6200  BL   Player_applyTranslateOffset  // add cameraOffset to mainList ONLY
6d6214  SUB  X2, X29, #-var_A0
6d6218  ADD  X3, SP, #p
6d6220  BL   Player_renderToCanvas       // sub_6C7440(player, layerArg, &mainList, &auxList)
```

**sub_6C7440 signature**: `(a1=player, a2=layerArg, a3=&mainList, a4=&auxList)`.
- `a4` (auxList) is forwarded to `sub_6C4E28(a1, a3, a4, &clipRect)` and not otherwise used directly in sub_6C7440.
- Main iteration is `for v11 in *a3..a3[1]` — **only mainList**.

## sub_6D5164 Build + Sort

```c
sub_6D5164(player, &mainList):
    if (player+544 == 0) return 0;
    sub_6C2334();  // note: only X0 (player via X20), X3=0xFF808080, W4=W5=0 set
                    // X1..X2 come from caller (Player_drawCompat sets X1=&mainList, X2=&auxList)
    // then sort mainList (begin..end) via std::sort with comparator sub_6D4F00
```

## sub_6C2334 — Build Render Items (Flat + Recursive)

Signature: `(resultObj, a2=&mainList, a3=&auxList, color, preview_flag, a6)`.

**Main per-node loop** (line 408 `for i = 1; i < node_count; i++`):
For each non-root node in the deque:

### Branch A: nodeType==3 Motion sub-node
Entry condition at sub_6C2334: `node.type == 3` (node+28 == 3) AND `node.drawFlag` (node+1960) is set. The branch then splits at 0x6C38A0 via `CBZ player.completionType` (player+1092, byte): `completionType != 0` → alloc/push independent item (node+1904 auxList path); `completionType == 0` → normal nested-render path. Player+1092 is `completionType` (1-byte bool, TJS-settable) — NOT `isEmoteMode` or `indepLayerInherit`. See project_player_completionType.md for byte-verified semantics.
- Fetch child Player TJS obj from `node+1912`
- Get child's renderList pointer via PropGet
- If drawFlag (node+1960) set:
  - Get-or-allocate parent item at `node+1904` (0x1B0 bytes)
  - **Push parent item to auxList** (`sub_6C3B04(v18=a3, &v351)` at 0x6c2aa4)
  - Get/allocate visibleAncestor's item from `visibleAncestor+1904`
  - **Write `parentItem+264 = visibleAncestor_item`** at 0x6c2b28 (v351+264=v126)
  - Clear parent's children vector (`item+24..item+40`)
  - **Recurse with a2 = &parent.children (item+24)**: `sub_6C2334(childObj, &parent.children, auxList, ...)` at 0x6c2b5c
  - This pushes child items into parent.children vector (NOT mainList)
  - **Range-insert** parent.children back into mainList: `sub_6F3424(a2, a2_end, parent.children.begin, parent.children.end)` at 0x6c2b70

### Branch B: Normal node (any type) with source (`node+200 != 0` and nodeType in bitmask)
- Get-or-allocate item at `node+1904`
- Set all fields (lines 786-1253): source variant, clip, viewport, bbox, matrix, vertices, colors, bounds, etc.
- **Push item to mainList** (`sub_6C3B04(a2, &v352)` at 0x6c30fc)
- **Does NOT touch item+264** — it keeps whatever value was written previously (or garbage if first alloc)

### Second Loop: Special Type12 Composite Parents (line 1258)

```c
for (k = 1; k < node_count; k++) {
  node = deque[k];
  if (node+28 == 12 && (node+52 & 4) && node+1944) {  // type12 with flag 0x4
    parent_item = get_or_alloc(node+1904);
    sub_6C3B04(a3, &parent_item);              // push to auxList
    clear parent_item.children (+24/+32);
    sub_6C3B04(parent_item+24, &parent_item);  // seed its own +24 list with itself
    
    // Iterate node.childNodes (2600..2608):
    for each childNode in node.childNodes:
      if (childNode+1944) {
        if (childNode.type == 3 && !emote) {
          // nodeType=3 child: range-insert its grandchildren (skip the type3 wrapper)
          sub_6F3424(parent_item+24_end, child_item+24_begin, child_item+24_end);
        } else if (childNode.type == 3 && emote) {
          // emote mode: append type3 child directly
          sub_6C3C04(parent_item+24, &child_item);
        } else if (childNode.type == 0) {
          // type0 child: append directly
          sub_6C3C04(parent_item+24, &child_item);
        }
      }
  }
}
```

## sub_6C4E28 — Two-Pass Renderer

### Pass 1: mainList leaf-layer rendering
For each item in mainList (`v21 = *a2; while v21 != a2[1]`):
- If `!item+19` (drawFlag): skip
- Compute intersection of item+184..196 (paintBox) with a4 (clipRect) AND item+200..212 (viewport)
- If result empty: `item+21 = 0`, skip
- Else: `item+21 = 1`, save clip to `item+216..228`
- If `player+760` (primaryLayer set) and `!item+20` (not yet initialized): call `requireLayerId` on windowMainWindow/primaryLayer, store result in `item+424`
- Mark `item+20 = 1`
- Initialize item+244=stencilType bits, item+248=source ref, item+184..196=bbox, item+200..212=viewport
- Apply mesh data from node+400 (bezier patch) or node+344 (mesh) if meshType != 0
- Call `sub_6C6B48(result_variant, player+760, item+424, ...)` to get leaf layer → store in `item+304`
- Get layer object from item+304
- Set neutralColor(0), setSize(w, h), affineCopy/meshCopy/bezierPatchCopy(source, ...) — renders source texture into leaf layer

### Pass 2: auxList composed parent rendering (line 0x6c5e7c onwards)
For each parent_item in auxList (`v93 = *v7; while v93 != v7[1]`):
- Walk `parent.children` (item+24..item+32): union their `item+184..196` bboxes into a single bound
- Intersect with `a4` clipRect
- Intersect with `parent.item+200..212` viewport if valid
- If empty: `parent+21 = 0`
- Else:
  - If `parent+340 == 0`: create new Layer via `Window.mainWindow.primaryLayer + new Layer(..., ...)`, store in `parent+324` (composedLayer)
  - Get composedLayer object from `parent+324`
  - `composedLayer.setSize(w, h)`
  - `composedLayer.fillRect(0, 0, w, h, 0)` — clear
  - For each child in parent.children:
    - If `child+21 && child+320 != 0` (leaf valid):
      - `Motion_doAlphaMaskOperation(parent+324, child.x-parent.x, child.y-parent.y, child+304, 0, 0, child_w, child_h, 64, player+1148, parent+244)`
  - Mark parent:
    - `parent+21 = 1`, `parent+16 = 0`
    - `parent+216..228 = composed bounds`

## sub_6C7440 — Final Composite Loop

**Outer loop** (lines 460-1487): `for v11 in *a3..a3[1]` (mainList only — auxList parent_items are NOT iterated here directly).

For each item:
1. **Filters (0x6c75c8)**:
   - `if (item+17 || item+16 || !item+232) skip`
2. **Clip and bbox** (0x6c75d0..0x6c7808):
   - Intersect item+184..196 with item+200..212 viewport
   - Compute `v31,v32,v36,v37 = effective bbox`
   - If empty: skip
3. **setClip** on `v370` (target renderLayer = layerArg)
4. **Preview gate** (0x6c7628): `if (player+1096 && !item+18) skip`
5. **Color setup**: item+168..180 copied as 4 color channels on `v27 = player+676`'s "stencil" object
6. **Source load** (0x6c7a90): `sub_6C1B70(v349, player, item+256+4)` → v349 = source texture Layer
7. **Select render path** (0x6c7b70, switch on `item+48 & 0xF`):
   - case 1: `v48=14`, goto LABEL_63 (composed)
   - case 2/5: `v48=15`, goto LABEL_63
   - case 3: `v48=16`, goto LABEL_63
   - case 4: `v48=17`, goto LABEL_63
   - case 0: 
     - If `player+1144` (stencilType): goto LABEL_60 (v48=2, composed)
     - Else goto LABEL_59:
       - If `item+264 != 0`: goto LABEL_60 (v48=2, composed)
       - Else fall through to direct path (v48=14)
   - default: If stencilType: goto LABEL_60. Else goto LABEL_59.
8. **LABEL_63 Composed Path** (0x6c7bb0-0x6c85bc):
   - `v49 = player+656` (findLayer) → returns layer with "bufLayer" sub-layer
   - `v342 = v49.bufLayer`  — a cached work/scratch layer
   - `v50 = v342` (object)
   - Compute setSize rect: `v57=max(item+184,0), v58=min(item+192, target.w), v61=max(item+188,0), v53=min(item+196, target.h)`
   - If empty: skip  
   - `v50.setSize(v58-v57, v53-v61)`
   - Dispatch on meshType:
     - `item+280 == 0`: `v50.operateAffine(v349, 0, 0, srcW, srcH, 0, vertex coords - [v57,v61] - 0.5, v48, blendMode, stNearest=1)` — renders source to bufLayer
     - `item+280 == 1`: operateBezierPatch
     - `item+280 == 2`: operateMesh
   - **LABEL_97 Alpha-mask chain walk** (0x6c82dc-0x6c83b0):
     ```c
     v87 = item+264;  // ancestor
     while (v87) {
       while (v87+21 && !v87+16) {  // ancestor is valid
         mask_src = (v87+244 & 4) ? v87+324 : v87+304;  // composed or leaf layer
         Motion_doAlphaMaskOperation(
           v342 /*bufLayer*/, v87+216-v57, v87+220-v61,
           mask_src, 0, 0,
           v87+224-v87+216, v87+228-v87+220,
           64, player+1148, v87+244);
         v87 = v87+264; if (!v87) break;
       }
       if (v87 && (v87+244 & 3) == 1) break;  // alpha-mask type
       v87 = v87+264;
     }
     // Then: fillRect (clear mask region) and final blit:
     v370.operateRect(v57, v61, v342, 0, 0, w, h, v48, blendMode)  // blit bufLayer to renderLayer
     ```
9. **Direct Path** (0x6c8bdc-0x6c8df0): Only meshType=0 case 0 with no stencil and no parent+264:
   - `v370.operateAffine(v349 /*source*/, 0, 0, srcW, srcH, 0, item+136..164 vertices - 0.5, v48=14, blendMode, stNearest=2)`
   - Draws source texture directly to target renderLayer with world-space vertices.
   - Similar for meshType=1 (operateBezierPatch) and meshType=2 (operateMesh) at 0x6c8e00/0x6c8a68.

## Key Field Semantics Summary (per verified evidence)

| Offset | Meaning | Written By | Read By |
|---|---|---|---|
| +16 | Tree-filter flag (copy of node+201) | sub_6C2334 @ 0x6c33a8 | sub_6C7440 @ 0x6c75c8, 0x6c82f4 |
| +17 | Node-type bitmask filter | sub_6C2334 @ 0x6c33a0 | sub_6C7440 @ 0x6c75c8 |
| +18 | Preview-mode filter | sub_6C2334 @ 0x6c33c0 | sub_6C7440 @ 0x6c7630 |
| +19 | First-pass entry (drawFlag) | sub_6C2334 @ 0x6c2a8c | sub_6C4E28 @ 0x6c5dc0 |
| +20 | Layer-id resolved flag | sub_6C4E28 @ 0x6c5240 | sub_6C4E28 @ 0x6c5144 |
| +21 | Clip-valid flag (1=renderable, 0=degenerate/empty) | sub_6C4E28 @ 0x6c4f88/0x6c5e6c | sub_6C7440 @ 0x6c82f4, sub_6C4E28 aux @ 0x6c5eb0 |
| +24/+32 | std::vector<item*> own children (populated by special composite loops) | sub_6C2334 branch A/type12 | sub_6C4E28 aux pass @ 0x6c5ea8 |
| +184..196 | paintBox (world AABB) | sub_6C2334 @ 0x6c27e8 | sub_6C4E28 clip, sub_6C7440 setClip |
| +200..212 | viewport AABB (inherited from Shape AABB chain) | sub_6C2334 | sub_6C4E28, sub_6C7440 |
| +216..228 | clipped render rect (after clip intersection) | sub_6C4E28 @ 0x6c4f8c | sub_6C7440 LABEL_97 |
| +232 | sourceW int flag (nonzero = drawable) | sub_6C2334 | sub_6C7440 @ 0x6c75c8 |
| +244 | stencil / composite flags (from node+52) | sub_6C2334 @ 0x6c2a90 | sub_6C7440 alpha-mask, sub_6C4E28 |
| +248 | tTJSVariant k context | sub_6C2334 | sub_6C7440 "k" propset |
| +256 | source key/data pointer | sub_6C2334 | sub_6C1B70 |
| +264 | **PARENT-ancestor item pointer (for alpha-mask chain walk)** — **ONLY set for nodeType=3 sub-player parents and type12 composite structures**. **NOT set for normal type0 leaves under type0 transform parents (like slide→moji_y).** Possibly uninitialized garbage otherwise. | sub_6C2334 @ 0x6c2b28 (branch A), 0x6c2654 (branch similar) | sub_6C7440 LABEL_59 gate and LABEL_97 chain walk |
| +280 | meshType (0=none, 1=bezier, 2=mesh) | sub_6C2334 | sub_6C7440 switch |
| +304 | leafLayer tTJSVariant (rendered source texture) | sub_6C4E28 @ 0x6c5348 | sub_6C7440 alpha-mask |
| +320 | type tag for leafLayer variant (tag field inside tTJSVariant at +304) | sub_6C4E28 | sub_6C4E28 aux "child+320 != 0" |
| +324 | composedLayer tTJSVariant (child aggregation target) | sub_6C4E28 aux @ 0x6c6114 | sub_6C7440 alpha-mask if flag&4, sub_6C4E28 aux setSize/fillRect |
| +340 | type tag for composedLayer variant | sub_6C4E28 aux | sub_6C4E28 aux "parent+340 == 0" guard |
| +424 | layerId from requireLayerId | sub_6C4E28 @ 0x6c5234 | sub_6C6B48 |

## Architectural Truth

1. **mainList is flat.** All renderable items (type0 with source, type3 motions, type12) sit in mainList as sequential top-level entries. Child-parent relationships are NOT implicit in list structure.

2. **Children render as INDEPENDENT top-level items.** moji_y / moji_f / etc. are type0 leaf nodes whose vertices (node+1856..1884) are already in WORLD space after `Player_updateLayers` bakes in parent transforms. They appear in mainList and are rendered via direct affineCopy (case 0, no stencil, item+264==0 → LABEL_59 falls through to direct path).

3. **item+264 is a special ancestor-chain pointer**, not a general parent pointer. It is ONLY set when:
   - Branch A (nodeType=3 sub-player parent wrapper): parent.item+264 = visibleAncestor.item
   - Branch similar at 0x6c2654 (also synthetic parent scenarios)
   - **Not set for a normal type0 leaf under a type0 transform parent.**

4. **auxList holds type12 composite parents** (and nodeType=3 wrappers). These parents need children aggregated into a composedLayer before being used as alpha-mask sources in sub_6C7440's LABEL_97 chain walk.

5. **No scratch-layer aggregation for normal slide→letters.** Slide itself has node+200==0 (no source), so it's skipped at sub_6C2334 line 762 and creates NO item. Its letter children render flat with their world-space vertices.

6. **bufLayer is a PER-ITEM scratch at `player+656["bufLayer"]`** (accessed via TJS findLayer on player's layer property). Reused across items with setSize. Blitted to renderLayer via operateRect in composed path. This is the ONLY scratch layer path — used when item+264 != 0 OR stencil != 0 OR meshType != 0.

## Local Web Port Misalignments (what to fix)

**Key local code bug at `PlayerRenderPrepare.cpp:820-837`:**
```cpp
for(auto &item : _runtime->preparedRenderItems) {
    const int parentNodeIndex = item.visibleAncestorIndex;
    // ... unconditionally sets item.parentItem for ANY visibleAncestor
    item.parentItem = it->second;
    it->second->childItems.push_back(&item);
}
```

This sets `parentItem` for EVERY item whose visibleAncestorIndex matches a prepared item. That is NOT aligned with binary's item+264 write rules (binary only writes item+264 for nodeType=3 sub-player parents and type12 composites).

**Downstream effect** (in `PlayerRender.cpp:1151-1165`):
- All child items get `hasRenderParent = true`
- At line 1851 `if(command.hasRenderParent) continue;` — they're SKIPPED from the top-level execute loop
- The parent (slide) is invisible (no source) so it never enters mainList either in our local logic (or it's wrongly marked something)
- Result: NOTHING renders the letter children, because:
  - They're filtered out as "children" in execute loop
  - Their parent (slide) has no source bitmap to render them through the composed path
  - The `buildCommandOutput` composed branch never triggers because the would-be "parent" slide never enters the top-level walk

## Minimal Local Fix Set

1. **`PlayerRenderPrepare.cpp:820-837`**: Only set `parentItem` following the binary's item+264 write conditions:
   - Set for nodeType=3 sub-player parent wrappers (the type3 node's parent_item → visibleAncestor's item). These correspond to Branch A in sub_6C2334 at 0x6c2aa4.
   - Set for type12 composite aggregation. These correspond to the secondary loop at 0x6c3754.
   - **Do NOT set for ordinary type0 leaves under type0 transforms.**

2. **`PlayerRender.cpp:1851`**: The `if(command.hasRenderParent) continue;` filter becomes almost never triggered once parentItem is correctly scoped. Children (moji_y) will go through to `buildCommandOutput`, which for them — with meshType=0, stencilType=0, no item+264 set — takes the **direct path** (`executedDirect = true`), doing `renderLayer.OperateAffine(worldCorners, source, ...)`. This is already present at `PlayerRender.cpp:1672-1679`. 

3. **Verify `shouldUseDirectRenderPathLike_0x6C7440`** at `PlayerRender.cpp:1672`: Must return true for: `meshType==0 && stencilType==0 && item+264==0`. That's the binary's case 0 + LABEL_59 fall-through condition.

4. **The `buildCommandOutput` composed branch is correctly architected** for type3/type12 cases — it's just never triggered for the yuzulogo letter scene because those don't involve type3/type12 composition. Leave it as-is.

## Verification Steps

After fix, expected behavior for yuzulogo:
- `renderCommands` contains: bg (1 item) + moji_y/f/o/s/t/u1/u2/z (~8 items) + potentially a few more = ~9-10 items
- `renderCommandsTopLevel` should be the SAME as `renderCommands` (all are top-level, none have parentItem)
- Execute loop processes all ~10 items
- Each takes `executedDirect=true` path → `renderLayer.OperateAffine(...)` 
- Expected ~10 `execute.copy` entries in log per frame (vs current 1)
- `buffered.operateRect.composed` should remain 0 for yuzulogo (no type12/type3 in play)
