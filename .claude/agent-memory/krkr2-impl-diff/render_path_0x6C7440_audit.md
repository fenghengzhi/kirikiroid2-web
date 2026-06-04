---
name: render-path-0x6C7440-audit
description: 2026-06-04 fresh-decompile audit of Player render path (sub_6C7440 draw, sub_6C715C builder, anchor 0x6C0528) vs local PlayerRender*/PlayerUpdate*; architecture verdicts + open gaps
metadata:
  type: project
---

2026-06-04 READ-ONLY audit, fresh decompile. Authoritative addrs confirmed:

**STRUCTURE-CORRECTION (2026-06-04, second pass):** sub_6C4E28
(Player_emitRenderItem_requireLayer, size 0x1bac) is NOT an "identical sibling"
of sub_6C7440 — that loose wording (still in ResourceManager.h:25 comments) is
structurally FALSE. 6C4E28 is a DISTINCT binary function = the SLA-build path
(per-item affineCopy/meshCopy/bezierPatchCopy with requireLayerId +
SeparateLayerAdaptor materialization, then per-group setSize/fillRect/
Motion_doAlphaMaskOperation, ends in Player_evaluateTimelines_guess). Called by
BOTH sub_6C7440 and sub_6C9CA8. It shares only the LEAF helpers (sub_6C715C
vertex builder, sub_6C1B70 source resolver, sub_6AF104 alpha mask) with 6C7440.
Local has NO single fn mirroring 6C4E28 (logic scattered in PlayerRenderExecute
clip/latch + persistNativeRenderItemFieldLifetimeLike_0x6C4E28) → MISSING/
mis-modeled.

**Render-path decomposition map (binary ground truth):**
Player_drawCompat 0x6D5FB8 dispatches 3 ways: Player_drawD3D 0x6D5B90 →
6ADE24→6ADFBC; Player_DrawSLA 0x6D5658 → sub_6C9CA8; default sub_6D5164
(calls sub_6C2334 build + SORT via comparator sub_6D4F00) → Player_renderToCanvas
0x6C7440 → updateLayerAfterDraw 0x6CE7D8.
- 1:1 (separate fn both sides): calcBounds 0x6C3D04 (called by Player_progressCompat,
  NOT by 6C2334; 0x6C4030 is just an internal loc of it), sub_6C9CA8, sub_6C715C,
  sub_6C1B70+6A7518, sub_6AF104, sub_6ADFBC, sub_6ADE24, sub_6CE7D8, the 10
  Phase3 callees (6BC000/6BC4F0/6BD8DC/6BDA28/6BDCC0/6BDE94/6BE0C0/6BEDD0/6BF0DC/
  6C0528 — binary genuinely has 10 separate phase-3 fns, local split FAITHFUL).
- SPLIT (1 binary fn → many local): sub_6C7440 → buildRenderCommands +
  executeLayerRenderCommands + renderToCanvasLike (+invented PreparedRenderItem
  vector / _renderLayerStates map / lambda recursion); sub_6C2334 (monolithic
  recursive tree-build) → appendPreparedRenderItems + prepareRenderItems +
  applyPreparedRenderItemTranslateOffsets; Player_updateLayers 0x6BB33C (P1+P2
  INLINE in one fn) → updateLayersPhase1_PreLoop + updateLayersPhase2_MainLoop.
- Possibly missing: sub_6D5164 wrapper + its render-item SORT (comparator
  sub_6D4F00) — verify a local sort step exists.

Authoritative addrs confirmed:

**sub_6C7440 = Player_renderToCanvas_guess (the real draw).** Single monolithic
loop over render-item list `a3` (item stride drives `*a3..a3[1]`). For EACH item
it directly dispatches TJS FuncCall on the render-layer INSTANCE (v370): setClip →
loadSource(sub_6C1B70) → operateAffine/operateMesh/operateBezierPatch (item+280
selects: 0=affine,1=bezier,2=mesh) OR the buffered bufLayer path (when blend&0xF
or item+264 children) using setSize/affineCopy/meshCopy/bezierPatchCopy/fillRect
then operateRect. Also drawMeshFrame/drawBezierPatchFrame/drawLine debug overlays
gated on a1+1048/a1+1068. Final setClip(reset).

**4-corner color consumption (M9 phase-D question — RESOLVED, no GPU per-vertex):**
Per item, binary sets a 4-element color-array object at player+716 via vtable+56
(PropSetByNum idx 0..3) from item+168/172/176/180 (the 4 packed corner colors),
then sub_6C1B70/sub_6A7518 BAKES that 4-corner tint into the source bitmap
(bilinear lerp, /128 if (blend&0xF0)==0x10 else /255) and caches by (name,color,
defaultBlend). sub_6C715C builder appends ONLY (x,y) position pairs (stride 20,
type tag 5) — NO color. So per-vertex GPU color is FALSE; the boundary is a real
CPU bake. Local mirror = SourceCache::applyPackedCornerTintLike_0x6A7518
(SourceCache.cpp:82) keyed by (key,blendMode,packedColors). FAITHFUL.

**Anchor color base index: CONFIRMED CORRECT.** qword_14D7C50 bytes verified =
{255.0(idx0), 128.0(idx1)} @0x14D7C50. `qword_14D7C50[(blend&0xF0)==0x10]` so
default-blend(==0x10)→TRUE→128.0. Local PlayerUpdateAnchor.cpp:144-146
`isDefaultBlend ? 128.0 : 255.0` MATCHES.

**Anchor blend source = per-slot CONFIRMED.** Binary reads
`*(v17 + 536*v19 + 364)` where v19 = node+1392 (slot index). Local uses single
`an.interpolatedCache.blendMode` (PlayerUpdateAnchor.cpp:145) — STILL a per-slot
vs single-cache gap (06-03 item stands, severity LOW: only affects 128-vs-255 base
choice for multi-slot anchor nodes; type-10 absent from logo fixtures).

**PropGet flag 1024 CONFIRMED in binary** (0x6c0790/0x6c07f8 width/height PropGet
use flag 1024). Local PlayerUpdateAnchor.cpp:44/48 passes flag 0. Real gap but
inert (PropGet flag 1024 = TJS_IGNOREPROP-ish; width/height succeed either way on
Layer). Severity LOW.

**MAJOR ARCHITECTURE DIVERGENCE (the headline):** Binary sub_6C7440 is a single
in-place loop that dispatches drawing primitives through the render-LAYER instance
via TJS FuncCall, with a per-player color-array object (+716) and bufLayer pulled
from +656. Local splits into buildRenderCommands (PlayerRenderExecute.cpp:13) +
executeLayerRenderCommands (:249) using std::vector<PreparedRenderItem>,
per-item reusable leaf/composed Layer slots, _renderLayerStates map, lambdas
(buildItemOutput recursion). The local "direct vs buffered" split and the
recursion over childItems is a reorganization, not the binary's flat loop. The
TJS dispatch primitives themselves (operateAffine/operateRect/affineCopy via
FuncCall on the right instance) ARE preserved via callLayer*Like_0x6C7440 helpers.
Verdict: data-flow/容器 NOT 1:1 (vector+map+lambda recursion vs flat loop +
per-player scratch objects); call-chain to TJS primitives IS faithful.
