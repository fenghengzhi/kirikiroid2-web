# CLUSTER I Audit — updateLayers eval + geometry + anchor

> Date: 2026-06-07 · Method: IDA decompile (libkrkr2.so authoritative) vs cpp/plugins/motionplayer port
> Scope (line-by-line, full coverage): PlayerUpdateLayers.cpp(121), PlayerUpdateLayersInternal.h(499),
>   PlayerUpdateLayerEval.cpp(1469), PlayerUpdateGeometry.cpp(811), PlayerUpdateAnchor.cpp(194).
> Supersedes 2026-05-30 cluster H for the anchor/geometry/setter functions — most H P0/P1 are now FIXED.
> IDB: renamed 5 funcs (+ offset suffix), 3 phase1/_deltaTime comments, idb_save OK.

## Verdict

**⚠️ PARTIAL DEVIATION**, one **🔧 architecture-blocked** prerequisite.

The phase2 accum loop, geometry phases (CameraConstraint/Vertex/Visibility/CameraNode/ShapeAABB/
ShapeGeometry), anchor damp math, calcBounds recursion, applyTranslateOffset and the root setters are
now well-aligned in branch structure, masks and field semantics — the 2026-05-30 H-cluster P0/P1 gaps
(H-1/H-2/H-3/H-4 anchor, H-10/H-11 calcBounds recursion, H-14..H-18 root setters, H-20 translateOffset)
have all been substantially closed since then. ONE cross-cluster prerequisite remains:

- **I-1 (🔧):** `player+592` (`_deltaTime` = `speedMul*dt`) is **never assigned in the port** and is used
  in three places (anchor gate, anchor dampPow, phase1 root velocity). Cannot be patched locally without
  first wiring the `_deltaTime = _speedMul * dt` entry-write that the binary does at progress_inner
  0x6C1094 (`*(a1+592) = speedMul*a2`).

## I-1 — player+592 (_deltaTime) assignment is MISSING (cross-cluster prerequisite) 🔧

DECOMPILE EVIDENCE:
- `Player_progress_inner` 0x6C1094 (entry, before any branch): `*(double*)(a1+592) = speedMul * a2`
  where `speedMul = *(a1+1168)` (read at 0x6C1080), `a2` = dt. This is the ONLY writer of player+592.
- Consumers of player+592:
  - 0x6BB38C / 0x6BB3B4 / 0x6BB3DC (phase1): `root.delta.posX/Y/Z += *(a1+592) * vel`
  - 0x6BB400 (phase1 damping): `pow(damp, *(a1+592)/60.0)`
  - 0x6C0884/0x6C0898 (anchor): `v27 = *(a1+592)/*(a1+1168)`, gate `a1[74]==0` (a1[74]==player+592)
  - 0x6C1108 firstFrame block, 0x6C1334 LABEL_48 clamp `+1120 += +592`

PORT STATE (grep cross-checked, not a single negative grep):
- `_deltaTime` (Player.h:1189 `// player+592 : _speedMul * dt`) is **read in 4 sites**
  (PlayerUpdateAnchor.cpp:23,66,67 ; PlayerUpdateChildMotion.cpp:186) and **assigned NOWHERE**
  (`grep "_deltaTime *="` → 0 hits). Stays at its initializer 0.0 forever.
- frameProgress (PlayerFrameProgress.cpp:2143-2144) sets only `_frameLastTime = dt` (unscaled).

CONSEQUENCES:
1. Anchor gate `_deltaTime == 0.0` (PlayerUpdateAnchor.cpp:23) is **perma-true** → every type-10 anchor
   takes the early-continue (clears anchorEnabled/renderTreeFlag200); the entire anchor physics
   (dampPow, angle/scale/slant/opacity/color damp, pos-lerp) is **dead**. The anchor code is otherwise
   byte-faithful (see I-OK below) — it just never runs because its time input is 0.
2. phase1 uses the WRONG field (see I-2) AND would still be 0 even if it read `_deltaTime`.

This is **architecture-blocked**: the fix is NOT in this cluster's 5 files. frameProgress (cluster G)
must add `_deltaTime = _speedMul * dt;` at the progress_inner entry (mirror 0x6C1094) BEFORE the existing
`_frameLastTime = dt`. Only then do anchor + phase1 have a non-zero, correctly-scaled time base.
IDB comment added @0x6C1094 / 0x6BB38C / 0x6BB400. (anchor type-10 absent from logo fixtures →
oracle-inert; still a real prerequisite, not a platform boundary.)

## I-2 — phase1 root velocity/damping uses _frameLastTime instead of _deltaTime ⚠️ (depends on I-1)

| | binary (0x6BB360..0x6BB428) | port (PlayerUpdateLayerEval.cpp:932-955) |
|---|---|---|
| velX integrate | `root+1592 += *(a1+592) * velX` | `delta.posX += _frameLastTime * vel` |
| damping pow | `pow(damp, *(a1+592)/60.0)` | `pow(_cameraDamping, _frameLastTime/60.0)` |
| damping gate | `if (damp != 1.0)` ONLY | `if (damp != 1.0 && _frameLastTime > 0.0)` ← extra subgate |

`*(a1+592)` = `_deltaTime` = `speedMul*dt` (scaled). Port uses `_frameLastTime` = raw `dt` (unscaled).
Differ whenever speed != 1. Port also adds a `_frameLastTime > 0.0` guard the binary does not have.
FIX: phase1 should read `_deltaTime` (not `_frameLastTime`) and drop the `>0` subgate — but blocked on
I-1 (with _deltaTime==0 today, switching the read would zero the velocity integrate). Fix I-1 first.
Inert for speed==1 logo fixtures (the two fields are equal there).

## I-OK — Anchor (0x6C0528) is byte-faithful (H-1/H-2/H-3/H-4 all FIXED)

PlayerUpdateAnchor.cpp now matches the decompile field-for-field:
- dampPow `dt*(v27*dt/v27)/v27/60.0/node+2432`, `v27 = *(player+592)/*(player+1168)` — line 66-69 ✅
  (H-1 fixed; the redundant `(v27*dt/v27)` preserved verbatim).
- w/h via PSB dispatch: binary `sub_A0F5E0(player+696)` then `PropGet(L"width"/L"height")` → node+232/+240;
  port reads `_internalRenderLayer.PropGet("width"/"height")` (line 40-52). No `<=0?32` clamp either side. ✅ (H-2 fixed)
- gate `a1[74]==0.0 || !*(player+612)` → `_deltaTime==0.0 || !_internalRenderLayerReady` (line 23). ✅ (H-3)
  player+612 = previous-frame snapshot of +613 (set by updateLayerAfterDraw 0x6CE7F4). Correct.
- COLOR base: binary `qword_14D7C50[(slot+44 & 0xF0)==0x10]`; **byte-verified qword_14D7C50 = {255.0, 128.0}**
  (0x14D7C50 = `00..00 E0 6F 40` =255.0, `00..00 60 40`=128.0) → index1(==0x10)=128.0. Port line 150-152
  `isDefaultBlend?128.0:255.0` reading `activeSlot().blendMode` (= slot+44). ✅ (H-4 fixed)
- allEqual sentinel `v50 == -8355712` = 0xFF808080; port line 158 `0xFF808080u`. ✅
- color iters `v54 = v12?1:4`; port line 159 `allEqual?1:4`. ✅
NOTE: the whole function is dead today via I-1 (gate perma-true). Faithful but un-exercised.

## I-OK — calcBounds (0x6C3D04) recursion present (H-10/H-11 FIXED, residual ordering ⚠️)

Binary is a SINGLE node-deque loop interleaving: type4 recurse (0x6C3F08, gate `!player+1092 && type==4`,
dispatches child via sub_56C694/sub_6C1678 list) → slot-done gate (0x6C4028 `!node+536*slot+344`) →
type3 recurse (0x6C4040, `!player+1092 && type==3`, dispatch node+1912 → child) → mask path (0x6C40B0,
`v31 = player+1092?5193:5185`, `&(1<<type)` AND `node+200`). Bounds source: node+2048 list (stride8) if
non-empty, else node+2072 list, else node+1856..1884 4-corner cache; floor/ceil; merged into player+152/160
(min, DBL_MAX init) /168/176 (max, -DBL_MAX init).

PlayerRenderItems.cpp:32 now recurses (line 185-203: type3 `child->calcBounds()`, type4 per-particle).
✅ recursion present (H-10/H-11 closed). Residual ⚠️ (local, non-blocking):
- Port splits into TWO loops (mask path 71-183, recursion 185-203); binary interleaves in one loop.
  min/max merge is commutative so result-equivalent — source-structure ⚠ only.
- Port type3/type4 recursion (line 187/193) **omits the `!_preview`(player+1092) gate** the binary has
  on BOTH recursive arms (0x6C3F08 `!player+1092 && type==4`, 0x6C4040 `!player+1092 && type==3`).
  In preview mode the binary does NOT recurse; port always does. → add `&& !_preview`.
- Port bounds-source order mesh→vertices[8]→vertexPosXY (line 121-134) vs binary node+2048→node+2072→
  node+1856 4-corner cache. Field-source mapping plausible; the 4-corner path (binary primary when both
  pt-lists empty) maps to port `vertices[]`. Offset identity not byte-re-verified this round (was H-12).

## I-OK — applyTranslateOffset (0x6D5264) now implemented (H-20 FIXED)

Binary: if `player+1095`(stereovision) compute base from player+120/128/136/144/148; iterate render-list
`a2[0..1]` (stride8) translating item+136/144/152/160 (4 verts), item+184..212 (paintBox), item+344 ptr-list,
item+400 ptr-list (when item+280==1) by (player+144,+148); stereovision Z-divide projection per vertex
(`v - z*(v-base)/(z-z0)` with floor/ceil AABB rebuild). Port: PlayerRenderItems.cpp:1040+ adds
_cameraOffsetX/Y to prepared render items. Present (was MISSING). Stereovision Z-projection branch (the
`if(v2)` block) coverage not deep-re-verified this round; flagged for a future stereovision-specific pass.

## I-OK — root setters now write root-node delta (H-14..H-18 FIXED as root mutators)

| setter @addr | binary writes | port |
|---|---|---|
| setFlip 0x6C0F1C | root+1587(flipX)/+1588(flipY), dirty+1584, both axes 1 call | Player.h:289 setFlipX→delta.flipX; setFlipY→delta.flipY (X/Y SPLIT, port-extra) |
| setSlant 0x6C0F54 | root+1624/+1632, dirty, both axes 1 call | Player.h:305 setSlantX→delta.slantX (split) |
| setRootOpacity 0x6C1028 | root+1656 (int), dirty | getOpacity reads delta.opacity (Player.h:342) |
| setRootVisible 0x6C1048 | root+1586 (bool&1), dirty | getVisible reads delta.visibleOverride (Player.h:339) |

Port now writes the root node delta block (`_nodes[0].delta.*` + `delta.dirty=true`), matching the binary's
root+158x/162x/165x/1586 + dirty+1584 semantics (the field *names* map; byte offsets are ABI, not a deviation).
⚠ ARCH NOTE: binary setFlip/setSlant write BOTH axes in ONE call (NCB-bound single property each); port
splits into setFlipX/setFlipY/setSlantX/setSlantY (X/Y-split NCB properties, self-annotated "port-extra").
The legacy `Player::setFlip/setSlant` viewport-scalar methods (PlayerLayerQuery.cpp:91/123 → `_flip`/`_slant`)
ALSO still exist — a DIFFERENT (port-invented) API surface. Not a phase2/geometry deviation; flagged for
cluster-E (NCB registration) to confirm the binary's registered property name/arity.
NB: setSlant 0x6C0F54 writes +1624/+1632 — note `analysis/player_updateLayers_accum.md` labels +1624 as
`scaleX_delta`. The delta-block offset labels in that note may be off; port uses NAMED fields (delta.slantX
vs delta.scaleX) so setter↔consumer stay self-consistent. ABI offset conflict not counted as a deviation.

## phase2 main loop (0x6BB524..0x6BBB6C) — ALIGNED ✅

Verified field-for-field against the full 0x6BB33C decompile:
- parent skip-walk `(parent+42 & 0x40)` = inheritMask bit22 joinTarget (0x6BB5AC) → port line 1110-1116 ✅
- v25 dirty `a1+610 || node+47 || parent+1504 || node+1584` → port line 1190-1198 ✅
- slot-done(+344) sync memcpy parent→node + matrix copy → port hasSync path line 1237-1263 ✅
- pre-matrix delta merge (vmulq/vaddq 1624/1640/1656/1592/1608) → port line 1276-1285 ✅
  (port uses named delta.scaleX/slantX/opacity/posX — same ops, FP order preserved)
- mesh deform `if(parent+2000) sub_69AE74` → port line 1287-1289 (gate node+2000=meshType) ✅
- pos transform by parent matrix, coordinateMode(+24) 3D/2D split → port line 1291-1312 ✅
- groundCorrection(+47) sub_6BAA10 → port line 1314 ✅
- opacity 2nd-mul `(v46&0x400) || !player+1097` → port line 1316-1327 ✅
- SIMPLE `(~v46 & 0x1FC)==0` / COMPLEX bit-set / INDEPENDENT(1097) / DEPENDENT 4-phase undo/redo-root
  → port line 1330-1431 ✅ (matrix matmul parent×local both SIMPLE & DEPENDENT-phaseD)
- post-loop: player+480 ? clear node+176..192 : lastPos=accum-prev (0x6BBB74) → port PlayerUpdateGeometry.cpp:567-583 ✅
- cleanup node+44=0/node+1504=0, +608=0, +480=0 (0x6BBCFC) → PlayerUpdateLayers.cpp:99-117 ✅
- phase3 call ORDER 0x6BBC60: CameraConstraint→Vertex→Visibility→CameraNode→ShapeAABB→ShapeGeometry→
  childMotion→particleEmitter→particleSystem→anchor → PlayerUpdateLayers.cpp:87-96 ✅ (H-22 OK)

## phase3 geometry (PlayerUpdateGeometry.cpp) — ALIGNED ✅ (one residual)

- CameraConstraint 0x6BC000 9-case (flip-adjusted ctype, track>max>min priority) → line 7-110 ✅
  RESIDUAL (was H-5): camera-target lookup is root(node0) placeholder, not the binary's nodePathMap
  resolve of clip-slot action path. CameraNode 0x6BDA28 stereovision angle target (line 667-679) likewise
  uses prev-frame `_cameraTarget*`. Stereovision-only; inert for non-stereo logo. ⚠
- VertexComputation 0x6BC4F0: priorDraw bool (node+48 = sub_6636D4&1) line 144-154 ✅; mesh inverse/grid/
  bezier-cascade (WASM-SIMD = faithful NEON port @0x6BB0CC/0x6BD360, NOT a deviation) line 244-449 ✅;
  4-corner output line 451-462 ✅; forceVisible TJS dict PropSet line 512-560 ✅
- Visibility 0x6BD8DC: bitmask emote?6153:6145, slot-done/stencil(+52)/active/forceVisible chain → line 587-632 ✅
- ShapeAABB 0x6BDCC0 (matrix×16 extent, origin offset, parent-clip clamp) → line 686-752 ✅
- ShapeGeometry 0x6BDE94 (shapeType 0=point/1=circle/2=rect/3=quad) → line 754-808 ✅

## Sub-function alignment

| sub | addr | status |
|---|---|---|
| Player_evaluateTimeline | 0x699AE4 | ✅ (evaluateTimelineLike_0x699AE4 + type4 COPY/INTERP particle mirror) |
| sub_699940 local matrix | 0x699940 | ✅ (applyLocalTransform, transformOrder 0/1/2/3, left-mul) |
| sub_69AE74 mesh deform | 0x69AE74 | ✅ (sub_69AE74_meshDeform, meshFlags 1/2/4 gates) |
| sub_6BAA10 groundCorrection | 0x6BAA10 | ✅ (TJS Array add + onGroundCorrection FuncCall, AddRef/Release) |
| Player_advanceNodeFrames | 0x6B7E44 | ✅ (advanceNodeFramesLike, parameterized no-events) |
| Player_progress_inner | 0x6C106C | ⚠ entry +592 write MISSING (I-1); rest cluster-G scope |
| Player_calcBounds | 0x6C3D04 | ✅ recursion; ⚠ split loops + missing !_preview recurse gate |
| Player_applyTranslateOffset | 0x6D5264 | ✅ present; stereovision Z-branch not deep-verified |
| root setters | 0x6C0F1C/54/0x6C1028/48 | ✅ root-delta write; ⚠ X/Y split + legacy scalar API |
| purgeNodeLabelMap_byParent | 0x6CDE18 | ❓ not in scope this round (cluster-F nodePathMap GC) |

## PLATFORM_BOUNDARY notes

- WASM-SIMD blocks (PlayerUpdateGeometry.cpp #ifdef __EMSCRIPTEN__ 292/427) are faithful wasm32 ports of
  ARM NEON (0x6BB0CC mesh grid / 0x6BD360 delta add) — legitimate ISA ports, NOT deviations, NOT counted.
- No `// PLATFORM_BOUNDARY:` annotations in the 5 audited files; none of the I-* deviations qualify as one
  (I-1/I-2 are real missing dataflow; "oracle-inert for logo" is NOT a platform boundary).

## Fix recommendations (priority)

1. **I-1 (🔧, do first):** in frameProgress (PlayerFrameProgress.cpp ~2144) add `_deltaTime = _speedMul * dt;`
   mirroring progress_inner 0x6C1094, BEFORE/with `_frameLastTime = dt`. Unblocks anchor + I-2.
2. **I-2 (⚠, after I-1):** phase1 (PlayerUpdateLayerEval.cpp:938/942/946/949-951) read `_deltaTime` not
   `_frameLastTime`; drop the extra `&& _frameLastTime > 0.0` subgate (binary gate is only `damp != 1.0`).
3. **calcBounds (⚠):** add `&& !_preview` to the type3/type4 recursion gates (PlayerRenderItems.cpp:187/193)
   to match binary `!player+1092` on both recursive arms.
4. Cross-cluster handoffs (not this cluster): H-5 camera nodePathMap target (cluster F), root-setter NCB
   arity (cluster E), stereovision Z-projection (applyTranslateOffset + camera) deep pass.
