# CLUSTER H Audit — Player updateLayers + geometry/transform + camera

> Date: 2026-05-30 · Method: IDA decompile (libkrkr2.so authoritative) vs cpp/plugins/motionplayer port
> Scope: per-node accum loop, transform/anchor math, camera/anchor node eval, bounds, root setters, event dispatch, translate offset.
> Protocol per function: decompile → <=10-line pseudocode → local counterpart → arch compare → P0/P1/P2.
> IDB: renamed 10 funcs + 2 layout comments, idb_save OK.

## Naming correction (IMPORTANT — caller's labels were swapped)
- `0x6BDA28` is **camera node** eval (nodeType==5) → renamed `Player_processCameraNode_type5`. Local: `updateLayersPhase3_CameraNode` (PlayerUpdateGeometry.cpp:636).
- `0x6C0528` is **ANCHOR node** eval (nodeType==10), NOT "evaluateCameraNodes" → renamed `Player_evaluateAnchorNodes_type10`. Local: `updateLayersPhase3_AnchorNode` (PlayerUpdateAnchor.cpp:7).

---

## Findings table

| id   | func @addr | local file:line | sev | one-line |
|------|-----------|-----------------|-----|----------|
| H-1  | Player_evaluateAnchorNodes_type10 @0x6C0528 | PlayerUpdateAnchor.cpp:36 | P0 | dampPow formula wrong: binary `dt*(v27*dt/v27)/v27/60/node+2432` with v27=`player592/player1168`; local uses `abs(frameLastTime)/60/anchorDamping` (wrong numerator + wrong divisor field). |
| H-2  | Player_evaluateAnchorNodes_type10 @0x6C0528 | PlayerUpdateAnchor.cpp:23-31 | P0 | width/height come from PSB **TJS dispatch** PropGet on `player+696` ("width"/"height"); local reads `interpolatedCache.width/height` (no dispatch). Also default 32 path differs: binary always materializes via dispatch, no `<=0 → 32` clamp on that path. |
| H-3  | Player_evaluateAnchorNodes_type10 @0x6C0528 | PlayerUpdateAnchor.cpp:14 | P1 | binary gate is `nodeType==10 && node+1505(active)`; sets `player+613=1` BEFORE the frameLastTime check (always, for every type10). Local sets `_needsInternalAssignImages` after the type check but skips it on the `==0` early-continue? — order/uncond mismatch. |
| H-4  | Player_evaluateAnchorNodes_type10 @0x6C0528 | PlayerUpdateAnchor.cpp:103-141 | P1 | color-damp loop count `v54 = v12?1:4` keyed on the 4-color-equal test at +100/104/108/112; local `iters=allEqual?1:4` matches intent but base is hardcoded 255 both branches (binary `qword_14D7C50[(blend&0xF0)==16]` selects between two consts — not both 255). |
| H-5  | Player_processCameraNode_type5 @0x6BDA28 | PlayerUpdateGeometry.cpp:642-681 | P0 | camera-target lookup via `Player_nodePathMap_find(a1+24, node+103*8...)` (clip-slot action path → node index) is MISSING; local uses previous-frame `_cameraTarget*` placeholder. Stereovision angle target therefore wrong. |
| H-6  | Player_processCameraNode_type5 @0x6BDA28 | PlayerUpdateGeometry.cpp:643 | P2 | binary finds **first** type5 with active(+1505) scanning index 2..N (starts v5=2, i.e. skips index1); local scans from i=1. Off-by-one on first-candidate; root(0) excluded both. |
| H-7  | Player_processCameraNode_type5 @0x6BDA28 | PlayerUpdateGeometry.cpp:648-651 | P2 | dy uses `posY*zFactor + posZ`; binary `v15[21]*player1112 + v15[20]` = posZ*coordinate + posY (field +21=posZ,+20=posY; player+1112=coordinate not zFactor). Verify zFactor==player+1112 mapping. |
| H-8  | Player_dispatchEvents @0x6C4490 | PlayerFrameProgress.cpp:963-983 | P1 | binary iterates `player+936..944` entry list (stride 44B, `i+=11` dwords), calls onAction/onSync via the **objthis dispatch vtbl[16]** (FuncCall) passing ttstr params copied by sub_A0F5E0; onSync uses cached hint `byte_1AB8450`. Local uses std::vector `_pendingEvents`+widen — container substitution (⚠ per CLAUDE.md) but event semantics match. |
| H-9  | Player_dispatchEvents @0x6C4490 | RuntimeSupport.cpp:1531 / PlayerFrameProgress.cpp:579 | P2 | onSync queued on playing→false edge (wasPlaying gate) — matches binary intent (entry type 1). onAction entries (type 0) queued at RuntimeSupport.cpp:1714 from action-string eval. Structurally OK. |
| H-10 | Player_calcBounds @0x6C3D04 | PlayerRenderItems.cpp:32 | P0 | binary is **recursive**: nodeType==4 (particle, 0x6C3F08) and nodeType==3 (sub-motion, 0x6C4048) each resolve a child Player via TJS dispatch and call `Player_calcBounds` recursively, merging child bounds into player+152..176. Local calcBounds is flat (no recursion); comment at :88-92 admits this. Bounds under-counted for nested motions/particles. |
| H-11 | Player_calcBounds @0x6C3D04 | PlayerRenderItems.cpp:93-98 | P0 | gate mask: binary `v31 = completionType?5193:5185` (0x1449/0x1441) AND `node+200`(renderTreeFlag200). Local uses `0x1449/0x1441` — values match. BUT binary ALSO skips when slot-done `node+536*slot+344` set (0x6C4028 outer `if(!...)`) and has the type4/type3 recursive paths BEFORE the mask path. Local mask path OK; missing slot-done gate + recursion ordering. |
| H-12 | Player_calcBounds @0x6C3D04 | PlayerRenderItems.cpp:121-134 | P1 | binary reads point arrays from node+2048 (mesh pts, stride8) / node+2072 (alt, stride8 from v34) / node+1856..1884 (8 floats=4 corners) selecting by which list is non-empty; floor/ceil applied. Local picks meshControlPoints else vertices[8] else (vertexPosX,Y). Field-source mapping plausible but +1856 corner-cache path (when both pt-lists empty) is the primary binary path and local maps it to `vertices[]` — verify offset identity. |
| H-13 | Player_calcBounds @0x6C3D04 | PlayerRenderItems.cpp:35-41 | P2 | binary inits player+152/160=DBL_MAX, +168/176=-DBL_MAX via vdupq; also AddRef/Release of player+636 motion var around the loop (sub_A0F5E0/vtbl). Local inits 1e308 (≈DBL_MAX OK) and skips the motion-var AddRef bracket. |
| H-14 | Player_setFlip @0x6C0F1C | (MISSING as root mutator) | P1 | binary writes root(player+200)+1587/+1588 and sets dirty +1584 only on change. Local `Player::setFlip` (PlayerLayerQuery.cpp:87) writes scalar `_flip` (viewport), NOT root node. No root-flipX/flipY mutator path found. |
| H-15 | Player_setSlant @0x6C0F54 | (MISSING) | P1 | binary writes root+1624(slantY)/+1632(zoomX)?? actually +1624/+1632 pair, dirty on change. Local `setSlant` writes `_slant` scalar only. Root mutator MISSING. |
| H-16 | Player_setAngleRad @0x6C0F84 | (partial) | P1 | binary: if emoteMode(player+482) wrap angle [0,360) → player+464 → `Player_initEmoteMotion(2)`; else write root+1616, dirty. Local has no equivalent Player root angle setter; angleRad lives only on LayerGetter (SourceCache.h:207). |
| H-17 | Player_setRootOpacity @0x6C1028 | (MISSING) | P1 | binary writes root+1656 (int opacity), dirty on change. Local `setOpacity` writes `_opacity` double scalar (PlayerLayerQuery.cpp:89). Root mutator MISSING. |
| H-18 | Player_setRootVisible @0x6C1048 | (MISSING) | P1 | binary writes root+1586 (bool&1), dirty on change. Local `setVisible` writes `_visible` scalar. Root mutator MISSING. |
| H-19 | Player_updateLayerAfterDraw_assignImages @0x6CE7D8 | PlayerRenderTargets.cpp:1434 | P2 | binary: `player+612 = player+613`; if(+613){ sub_6CE19C(...); materialize player+696 dispatch; FuncCall "assignImages"(target) }. Local gates on `_needsInternalAssignImages`(+613), calls assignImages on `_internalRenderLayer`. Missing the `+612 = +613` latch copy and the sub_6CE19C pre-step. |
| H-20 | Player_applyTranslateOffset @0x6D5264 | (MISSING) | P1 | binary: if `player+1095`(stereovisionActive) compute base offset from player+120/128/136/144/148; iterate render-list `a2[0..1]` (stride 8), translate each item's float vert arrays (item+136/144/152/160, item+184..212, item+344 ptr-list, item+400 ptr-list) by (player+144,+148); stereovision Z-divide projection per vertex. No local counterpart found (grep `applyTranslateOffset` only in comment PlayerDrawDispatch.cpp:292). |
| H-21 | Player_purgeNodeLabelMap_byParent_guess @0x6CDE18 | (MISSING) | P1 | binary walks linked node chain (a1[1]), for each entry iterates std::map at +51/+52 (Rb-tree), erases entries whose value+40 == parentKey, Release tTJSVariant, delete node, decrement +56. Local has no nodePathMap purge-by-parent; _nodeLabelMap is flat std::map without this GC. |
| H-22 | Player_updateLayers @0x6BB33C | PlayerUpdateLayers.cpp:13 | P2 | phase3 call ORDER per binary 0x6BBC60: 6BC000,6BC4F0,6BD8DC,6BDA28,6BDCC0,6BDE94,6BE0C0,6BEDD0,6BF0DC,6C0528. Local order: CameraConstraint,VertexComputation,Visibility,**CameraNode(6BDA28)**,ShapeAABB,ShapeGeometry,MotionSubNode,ParticleEmitter,ParticleSystem,**AnchorNode(6C0528)**. Order matches the documented chain. OK. |

## MISSING (no local counterpart)
- Player root-node setters as root mutators: setFlip/setSlant/setAngleRad/setRootOpacity/setRootVisible (H-14..H-18). Local `Player::setFlip/setSlant/setOpacity/setVisible` are viewport scalar setters (`_flip/_slant/_opacity/_visible`), a DIFFERENT API surface.
- `Player_applyTranslateOffset` (0x6D5264) — H-20, only referenced in a comment.
- `Player_purgeNodeLabelMap_byParent` (0x6CDE18) — H-21, node-path-map GC by parent key.
- calcBounds recursion into type3/type4 children (H-10).
- camera stereovision target lookup via nodePathMap (H-5).

## Verdict
**🔧 NEEDS ARCHITECTURE REWORK (partial).** Geometry phases (VertexComputation/ShapeAABB/ShapeGeometry/Visibility/CameraConstraint) are well-aligned in math and branch structure. But the cluster has THREE architecture-level gaps that cannot be patched locally:
1. **Anchor damp math (H-1/H-2)** — port replaced the binary's `player+592 / player+1168` based exponent and PSB-dispatch width/height with simplified scalar reads; needs the real intermediate `v27`/`v28` dataflow.
2. **Root setters (H-14..H-18) MISSING as root mutators** — port models them as viewport scalars; the binary writes root node accum slots + dirty flag. Either a real root-mutator API must exist or this is a deliberate platform split (NOT annotated PLATFORM_BOUNDARY → counts as deviation).
3. **calcBounds non-recursive (H-10/H-11)** — binary recurses through type3 sub-motion and type4 particle children via TJS dispatch; port is flat.

Container substitutions (std::vector `_pendingEvents` for player+936 list, std::map `_nodeLabelMap`) are ⚠ per CLAUDE.md but not new findings here.

## PLATFORM_BOUNDARY notes
None of the deviating sections carry `// PLATFORM_BOUNDARY` annotations. The WASM-SIMD mesh-grid blocks in PlayerUpdateGeometry.cpp (#ifdef __EMSCRIPTEN__, lines 292/427) are legitimate ISA ports of NEON and are NOT counted as deviations.
