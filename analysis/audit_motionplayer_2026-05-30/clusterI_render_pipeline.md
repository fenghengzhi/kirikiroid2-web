# CLUSTER I — Player render pipeline alignment audit (2026-05-30)

> Authoritative source: libkrkr2.so (IDB libkrkr2.so.i64). Read-only audit; IDB
> renames/comments applied + idb_save. No cpp/ edits.
> Method: per-function decompile -> pseudocode -> local counterpart -> compare.

## Draw-dispatch call graph (verified)

```
Player.draw (NCB)  = Player_draw_NCBWrapper @0x6818D0
    this = *(player+1064)            ; EmoteObject->Player chain
    -> Player_drawCompat @0x6D5FB8 (== sub_6D5FB8, the 3-way dispatcher)
         if arg is D3DAdaptor (cls dword_1AB8820): set player+909=1; Player_drawD3D@0x6D5B90
         elif arg is SLA (cls dword_1AB87F8):        Player_DrawSLA@0x6D5658
         else: sub_6D5164(build mainList v40 + boundsList p)
               if player+909 (wasD3DMode): D3DAdaptor path (setSize/visible/renderFromPlayer/captureCanvas)
               else: Player_applyTranslateOffset
                     Player_renderToCanvas_guess@0x6C7440(player, layerArg, mainList, boundsList)
                     Player_updateLayerAfterDraw_assignImages@0x6CE7D8
```

- **Player_drawD3D @0x6D5B90**: sub_6D5164 -> applyTranslateOffset -> D3DAdaptor_renderFromPlayer. Thin.
- **Player_DrawSLA @0x6D5658**: branches on `byte_1AB84F4` (config `ogl_accurate_render` OR `hasGPUAccel_guess`, cached via cxa_guard byte_1AB84F8).
  - accurate OFF: `Player_ResolveSLATarget_guess@0x6D5948` -> `Player_RenderMotionFrame_guess@0x6DE738(buf, w,h, ...)` -> Layer_UpdateRect.
  - accurate ON: `sub_6C9CA8` (which itself calls Player_emitRenderItem_requireLayer@0x6C4E28) + sub_6CE938.
- **Player_ResolveSLATarget_guess @0x6D5948**: resolves/creates PrivateMotionGLL child layer under ownerLayer (player+40 dispatch, g_PrivateMotionGLL_ClassID), SetSize(w,h). Lazy-creates via PrivateMotionGLL_CreateClass + cxa_guard byte_1AB8580.
- **Player_updateLayerAfterDraw_assignImages @0x6CE7D8**: gate `player+613` (copied to +612). If set: sub_6CE19C + dispatch `assignImages(arg)` on layer player+696.
- **Player_drawToLayerCompat @0x6D2D80**: gate `*(player+544)`. nodeType==3 recursive child-draw with fillRect + recursion over child Players (drawToLayerCompat self-call).

## CRITICAL re-mapping: sub_6C4E28 is NOT a build-tree fn

`sub_6C4E28` (renamed **Player_emitRenderItem_requireLayer**) is a PER-RENDER-ITEM
executor called from:
  - Player_renderToCanvas_guess @0x6c756c  (non-accurate layer path)
  - sub_6C9CA8 @0x6c9e74                    (accurate SLA path)

It runs DURING the execute/renderToCanvas phase (not the build phase). Two loops:
  - Loop A (over a2 mainList): per item, clip vs paintBox(+184)/viewport(+200),
    if drawable -> set item+21=1, write clipRect item+216..228, requireLayerId
    (LABEL_28) -> item+424=layerId, item+20=1, then emit affineCopy/meshCopy/
    bezierPatchCopy via TJS dispatch on the acquired Layer.
  - Loop B (over a3 boundsList): per group item, union child clip rects, acquire
    composed Layer (item+324 via Window.mainWindow.Layer ctor), setSize/fillRect,
    alpha-mask child loop (Motion_doAlphaMaskOperation), set item+21/16/216..228.

So the requireLayerId / item+20 materialization is an EXECUTE-phase action in the
binary. The local port moved it into the build loop (commit d51cce9) as a fidelity
trade; values match (trace 0-diff) but the phase placement is divergent. See P1-I3.

`Player_buildRenderTree_guess @0x6CBCE4` is mis-named: it is actually
**get/acquireLayerById** — a std::map<int,LayerVariant> (Rb_tree keyed by layerId)
lookup-or-create that builds a `Layer` TJS object with absolute=node.x+y,
hitThreshold=256, and caches at map node+40. Suggest rename
`Player_acquireLayerById`. (Not renamed yet — left as _guess pending local symbol.)

## skipFlag1 (item+18) VERDICT — ALIGNED

Binary primary item-write @0x6c3380-0x6c33c0 (leaf render item v352 in sub_6C2334):
```
v298 = 1;                                   // default
item+17 = ((preview?1097:1089) & (1<<node+28)) == 0;   // skipFlag0
item+16 = node+201;                                     // rawFlag16
if ((a6 & 1) == 0) v298 = (node+48 != 0);   // node+48 = priorDraw
item+18 = v298;                              // == inheritedFlag18 || (node+48!=0)
```
- `node+48` PROVEN = priorDraw: sub_6BC4F0 @0x6bc6c4 writes
  `node+48 = sub_6636D4(emoteEdit,"priorDraw") & 1` only when node+1996(forceVisible),
  else 0 @0x6bc67c. Byte 0/1.
- Consumed in Player_renderToCanvas_guess @0x6c7630: `if(!item+18) skip`, reached
  ONLY when player+1096(preview) set (LABEL_16 gated by a1+1096). item+18=1 means draw.
- Local PlayerRenderItems.cpp:477 `skipFlag1 = !(inheritedFlag18 || (node.priorDraw != 0))`
  — EXACT negation match (local stores skip-sense). priorDraw write gate
  (PlayerUpdateGeometry.cpp:144-154) matches forceVisible+emoteEdit gate. a6
  propagation `_renderItemInheritedFlag18 || nodePriorDraw` (line 259) matches
  binary recursive a6 `(a6&1)||node+48`.
- VERDICT: skipFlag1 formula + node+48 semantics + a6 propagation ALL aligned.
  The residual m2logo items[1] frame12+ build_flow mismatch is NOT a skipFlag1
  formula bug; it must originate from WHICH node/order the item is built for, or
  the build-vs-execute phase placement of layer materialization (P1-I3), or a
  node+1996/priorDraw value timing difference between frames. Needs runtime trace
  of node.priorDraw + inheritedFlag18 + forceVisible at m2logo items[1] frame12.

## rawFlag20 (item+20) VERDICT — ALIGNED (value), phase-divergent (P1-I3)

Binary: item+20 latched =1 ONLY at LABEL_28 in Player_emitRenderItem_requireLayer
(execute phase), gated `drawFlag19 && drawable(v80<v84 && v83<v85 && !item+16) &&
item+20==0`. item+424=layerId from requireLayerId dispatch. Other paths never
touch item+20.
Local: latched in build loop (commit d51cce9) when same gate holds; value matches
(trace 0-diff, build_flow yuzulogo 242->0). Phase moved execute->build = fidelity
trade, see P1-I3.

## Render-item offset table (binary item = operator new(0x1B0)=432B; sub_6C2334 alloc @0x6c2754)

| bin off | type | binary write site / meaning | local field (NativeRenderItemFields / PreparedRenderItem) | status |
|---------|------|------------------------------|-----------------------------------------------------------|--------|
| +0  | _OWORD | =*(node+0) variant/source ref (0x6c3374, memset 0 @0x6c27a0) | srcRef (semantic) | ⚠ STL |
| +8  | tTJSVariant* | source key, AddRef'd (0x6c52a0) | srcRef/sourceKey | ⚠ STL |
| +16 | byte | rawFlag16 = node+201 (0x6c33a8) | rawFlag16 | ✅ |
| +17 | byte | skipFlag0 = ((preview?1097:1089)&(1<<nodeType))==0 (0x6c33a0) | skipFlag0 | ✅ |
| +18 | byte | skipFlag1 = inheritedFlag18||(node+48!=0) (0x6c33c0) | skipFlag1 (negated sense) | ✅ |
| +19 | byte | drawFlag = node+1960?1:node+1961 (build path) | drawFlag | ✅ (local adds needsGroupEntry term) |
| +20 | byte | rawFlag20 latch=1 at requireLayerId LABEL_28 (0x6c5240) | rawFlag20 | ✅ value / ⚠ phase (P1-I3) |
| +21 | byte | rawFlag21 drawable-clip-valid (0x6c4f88 set / 0x6c5e6c clear) | rawFlag21 | ✅ |
| +24/+32 | ptr/ptr | childItems vector begin/end (0x6c2768 memset) | childItems std::vector<PreparedRenderItem*> | ⚠ STL |
| +48 | int | blendMode = clip+364 (node mesh/composite) | blendMode | ✅ (default 16) |
| +52 | int | = node+16 (0x6c341c) | (objTriPriority region) | ❓ verify |
| +56 | int | = node+20 (0x6c3428) | — | ❓ |
| +136..164 | float[8] | corner vertices = node+1856..1884 (0x6c530c via v250[3..4] -> +136) | corners / localCorners | ✅ |
| +168..180 | int[4] | RGBA packed colors | packedColors | ✅ |
| +184..196 | float[4] | paintBox = node+1888..1900 (v248 0x6c52bc) | paintBox | ✅ |
| +200..212 | float[4] | viewport = node+1936 chain (v249 0x6c52c8) default (1,1,-1,-1) | viewport | ✅ |
| +216..228 | float[4] | clipRect (drawable: v80/v83/v84/v85 0x6c4f8c) | clipRect (int!) | ⚠ type float vs int |
| +232 | int | opacity = node+1576 (read @0x6c7634) | opacity | ✅ |
| +244 | int | stencilComposite = node+52/stencilType (0x6c2a90); consumed (item+244&4),(item+244&3)==1 | stencilComposite | ✅ |
| +248 | tTJSVariant | context = player+1012 (0x6c33fc) | contextVariant | ✅ |
| +256 | ptr | -> node+200 source deque (loadSource arg @0x6c5664 uses +256+4) | (sourceKey path) | ⚠ STL |
| +264 | ptr | parent render node (semantic item +264) | parentItem | ⚠ STL ptr |
| +272/+276 | int | meshDivX/meshDivY (0x6c5864/0x6c5874) | meshDivX/meshDivY | ✅ |
| +280 | int | meshType = node+2000 (0x6c2684); switch 0/1/2 | meshType | ✅ |
| +300 | int | (memset 0 @0x6c2770) | — | ❓ |
| +304 | tTJSVariant | leaf layer (sub_A0FB64 v79+304 @0x6c533c) | leafLayer | ✅ |
| +320 | int | composed flag/count (memset @0x6c2774; tested @0x6c62e0 item+320) | composedBuilt-ish | ❓ verify |
| +324 | tTJSVariant | composed Layer (Loud B sub_A0FB64 v94+324 @0x6c6114) | composedLayer | ✅ |
| +340 | int/_OWORD | composed-built flag (tested !item+340 @0x6c5f94) | composedBuilt | ❓ verify int vs bool |
| +344..  | deque | mesh vertices (meshCopy src node+344) | meshPoints | ⚠ STL |
| +364 | int | (memset region) | — | — |
| +368 | int | bezier precision = node+368 (0x6c5bac) | meshPoints precision | ❓ |
| +376/+392/+408 | _OWORD | mesh control point deques (memset @0x6c2778..) | localMeshPoints | ⚠ STL |
| +400 | deque | mesh base points (bezier src node+400) | meshPoints | ⚠ STL |
| +424 | int | layerId = requireLayerId result (0x6c5234) | layerId | ✅ |

(❓ = offset present in binary, local mapping not yet positively confirmed this pass.)

## Findings

| id | func@addr | local file:line | sev | one-line |
|----|-----------|-----------------|-----|----------|
| I1 | Player_emitRenderItem_requireLayer@0x6C4E28 | PlayerRenderItems/Execute/Targets | P1 | requireLayerId+item+20 live in EXECUTE phase (called from renderToCanvas/sub_6C9CA8), local moved to build loop — value-equiv, phase-divergent |
| I2 | sub_6C2334@0x6c33c0 | PlayerRenderItems.cpp:477 | OK | skipFlag1 = !(inherited18||priorDraw) EXACT; node+48=priorDraw PROVEN @0x6bc6c4 |
| I3 | sub_6C2334@0x6c5240/0x6c5e6c | RuntimeSupport.h:257 | OK | rawFlag20 latch gate matches; value-aligned (trace 0-diff) |
| I4 | sub_6C2334 item alloc 0x1B0 | RuntimeSupport.h:252-321 | P2 | item is raw 432B new; local NativeRenderItemFields+PreparedRenderItem = STL structs (vector/variant/string) — PLATFORM-class container divergence, always ⚠ per CLAUDE.md |
| I5 | item+216..228 clipRect | RuntimeSupport.h:263 | P2 | binary stores float[4]; local std::array<int,4> clipRect — TYPE mismatch (truncation risk in dirty/setClip math) |
| I6 | Player_drawCompat@0x6D5FB8 D3D path | PlayerDrawDispatch.cpp | P1? | player+909(wasD3DMode) re-render-to-D3DAdaptor + captureCanvas path — verify local has this branch (Web D3D stub) |
| I7 | Player_DrawSLA@0x6D5658 | SeparateLayerAdaptor/PlayerRenderTargets | P1 | two SLA sub-paths gated by ogl_accurate_render (byte_1AB84F4): RenderMotionFrame vs sub_6C9CA8+sub_6CE938 — verify local distinguishes accurate vs non-accurate |
| I8 | Player_buildRenderTree_guess@0x6CBCE4 | (n/a IDB) | P3 | mis-named: actually acquireLayerById (Rb_tree<int,LayerVariant>, absolute=x+y, hitThreshold=256) — rename pending local symbol |
| I9 | Player_renderToCanvas@0x6c7440 first skip | PlayerRenderExecute.cpp:~462-528 | OK→verify | skip = item+17||item+16||!item+232; item+18 skip preview-gated @a1+1096 — confirm local replicates preview gate |
| I10| Player_emitRenderItem mesh switch | PlayerRenderExecute.cpp | P2 | meshType 0/1/2 -> affineCopy/bezierPatchCopy/meshCopy via TJS dispatch; verify local emits all three (analysis notes only affineCopy historically) |

## MISSING (binary present, local absent/unverified)

- Player_drawToLayerCompat @0x6D2D80 nodeType==3 recursive child fillRect path
  (gate player+544) — confirm local has a drawToLayer/nodeType-3 recursion.
- Player_DrawSLA accurate-render sub_6C9CA8 + sub_6CE938 branch (ogl_accurate_render).
- Player_ResolveSLATarget PrivateMotionGLL lazy-create + SetSize.
- Player_renderToCanvas alpha-mask child loop (Motion_doAlphaMaskOperation, item+244&...)
  in Loop B of Player_emitRenderItem (composed/group items).
- item internal offsets +52/+56/+300/+320/+340/+368 local mapping unconfirmed.

## Architecture-level (🔧)

- I1/I4: The binary render item is a single 432B raw `new(0x1B0)` block consumed
  in-place across build(sub_6C2334) and execute(sub_6C4E28/6C7440). The local port
  splits it across two STL structs (NativeRenderItemFields + PreparedRenderItem)
  with std::vector/std::string/tTJSVariant members and moves requireLayerId from
  execute->build. This is the standing "container + phase" divergence; trace is
  0-diff so behaviour is preserved, but it is not byte/phase 1:1. Reconciling
  requires (a) a single raw-ish item POD, (b) restoring requireLayerId to the
  execute pass. High regression risk; do under CI per prior review guidance.

## IDB changes applied (idb_save done)

- rename 0x6C4E28 -> Player_emitRenderItem_requireLayer
- comments @0x6c33c0 (item+18), @0x6bc6c4 (node+48=priorDraw), @0x6c5240 (item+20),
  @0x6c756c (emitRenderItem call-site role)
