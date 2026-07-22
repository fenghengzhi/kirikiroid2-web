# CLUSTER J — render pipeline part 1 (execute + internal + dispatch) alignment audit (2026-06-07)

> **2026-07-23 superseded correction：**本历史审计中出现的
> `player+1096 /*preview*/` 字段名已被 NCB 字面绑定证伪；raw offset
> +1096 是 Player `priorDraw` bool，`preview` 是 +1092。地址和分支本身保留。

> Authoritative source: libkrkr2.so (IDB libkrkr2.so.i64). Read-only audit of
> cpp/; IDB renames/comments applied + idb_save. No cpp/ edits.
> Method: per-function decompile -> pseudocode -> local counterpart -> compare.
> Scope files: PlayerRenderExecute.cpp (1305), PlayerRenderInternal.cpp (1297),
> PlayerRenderInternal.h (204), PlayerDrawDispatch.cpp (298), PlayerRender.cpp (84).

## Binary functions decompiled this pass (all confirmed in-conversation)

| addr | name (renamed/confirmed) | role |
|------|--------------------------|------|
| 0x6C4E28 | Player_emitRenderItem_requireLayer | per-item LEAF copy (Loop A) + group/composed compose (Loop B). Single pass; latches item+20/+21/+216..228 AND emits affineCopy/meshCopy/bezierPatchCopy on the per-item leaf layer (item+304) in the SAME pass |
| 0x6C7440 | Player_renderToCanvas (was _guess) | non-accurate top-level execute. Pre-walk calls 0x6C4E28; top-level loop submits each item to render-layer v370 via operateRect (buffered) or operateAffine/operateMesh/operateBezierPatch (direct) |
| 0x6C6B48 | Player_acquireLeafLayerById (was sub_) | Rb_tree get-or-create on SLA(player+760) keyed by item+424; new Layer absolute=node.x+y, ++node+164, hitThreshold=256 |
| 0x6CBCE4 | Player_acquireComposedLayerById (was Player_acquireLayerById) | Rb_tree get-or-create keyed by item+56(=node+20); new Layer absolute=node.x+y, hitThreshold=256. Used by accurate-SLA composed child |
| 0x6C9CA8 | Player_renderAccurateSLA (was sub_) | ogl_accurate_render path; builds persistent SLA child Layer tree (drawMeshFrame/drawLine/setPos), NOT a single-layer composite |

## CRITICAL re-confirmation of phase structure (binary ground truth)

The binary's draw execute is a **two-function pipeline** that runs inside the
SAME top-level draw call (`Player_renderToCanvas` 0x6C7440):

```
Player_renderToCanvas(player, a2, a3=mainList, a4=cameraOffset[4]):   // 0x6C7440
  Layer = global.Layer (sub_5CB08C)
  bufW = Layer.width(v9), bufH = Layer.height(v9)   // v9 = render-layer obj
  if (!player+1096 /*preview*/):
      DirtyRectList setup on player+864 (sub_7E2544)
      Player_emitRenderItem_requireLayer(player, a3, a4, &clip)   // 0x6C4E28 ★
  for item in *a3 (mainList):                                     // top-level walk
      if (item+17 || item+16 || !item+232) continue               // skip gate
      if (viewport valid: item+208>=item+200 && item+212>=item+204):
          clip = max(paintBox,floor(viewport)) ∩ min(paintBox,ceil(viewport))
          if (clip empty) continue
          v370.setClip(left,top,w,h)        // argc=4 FuncCall
      else:
          v370.setClip()                    // argc=0 reset FuncCall
      if (player+1096 /*preview*/ && !item+18) continue            // preview gate
      opa = item+232; if (preview) opa >>= 1                       // preview halves opacity
      DirtyRectList_AddRect(player+864, truncTowardZero(paintBox))
      ... PropSet key/src/blendMode on cache objects, sub_6C1B70 loadSource ...
      v48 = blendMode map: switch(item+48 & 0xF){1->14,2/5->15,3->16,4->17,
            0/default-> if(player+1144 clearEnabled) buffered(v48=2)
                        elif(item+264) buffered(v48=2) else DIRECT}
      if BUFFERED:    bufLayer=ctx(player+656).bufLayer; bufLayer.setSize(clip);
                      affineCopy/meshCopy/bezierPatchCopy onto bufLayer;
                      walk item+264 ancestor chain: Motion_doAlphaMaskOperation;
                      bufLayer.fillRect; v370.operateRect(clip.x,clip.y,bufLayer,...,v48,opa)
      if DIRECT:      v370.operateAffine / operateMesh / operateBezierPatch
                      (source pre-translated -0.5,-0.5 world; mode v48=2/14.., opa)
  v370.setClip()    // argc=0 final reset
  // NO Update() call. returns v370 (released).
```

`0x6C4E28` (the pre-walk emitter) itself has TWO loops:

```
Player_emitRenderItem_requireLayer(player, a2=mainList, a3=groupList, a4=clip[4]):
  // Loop A over *a2 (mainList) — per leaf item:
  for item in *a2:
     if (!item+19 /*drawFlag*/) continue                  // ★ only drawFlag items
     clip = paintBox ∩ (floor/ceil viewport)
     if (clip.l<clip.r && clip.t<clip.b && !item+16):
        item+21 = 1; write item+216..228 = clip
        if (player+760 /*SLA*/): if(!item+20) LABEL_28
        else: create SLA from Window.mainWindow.primaryLayer, then LABEL_28
        LABEL_28: requireLayerId (numparams=0 FuncCall) -> item+424; item+20=1
        leaf = acquireLeafLayerById(SLA, item+424)  (sub_6C6B48, item+304)
        leaf.setSize(clip.w, clip.h)
        switch(item+280 meshType): 0->affineCopy 1->bezierPatchCopy 2->meshCopy
          (all onto LEAF layer, points pre-translated -0.5-clipOrigin)
     else: item+21 = 0
  // Loop B over *a3 (groupList) — per composite/group item:
  for grp in *a3:
     union child (child+21) clip rects into grp clip
     ∩ a4(cameraOffset/clip)
     if (empty): grp+21 = 0
     else: if(!grp+340): create composed Layer via Window.mainWindow + Layer ctor -> grp+324
           composed.setSize; composed.fillRect(0)
           for child in grp+24 vector: if(child+21 && child+320):
               Motion_doAlphaMaskOperation(grp+324, ..., child+304, child+244)
           grp+21=1; grp+16=0; write grp+216..228
```

## Render-item offset clarifications confirmed THIS pass (vs prior cluster-I)

- **item+424** = leaf layerId (requireLayerId result, latched with item+20). Distinct
  from item+52/+56 (= node+16/node+20, used as keys by accurate-SLA acquire fns).
  Prior cluster-I table said "+424 layerId = requireLayerId result" — confirmed.
- **item+304** = LEAF layer variant (acquireLeafLayerById/sub_6C6B48 result, keyed by item+424).
- **item+324** = COMPOSED layer variant (Loop B group, Window.mainWindow Layer ctor;
  in accurate-SLA keyed by item+56). item+340 gate = "composed already built".
- **item+264** = parent render item pointer (ancestor chain). In 0x6C7440 it drives
  BOTH (a) buffered-vs-direct routing (`item+264!=0 forces buffered`) AND
  (b) the alpha-mask ancestor walk. Local maps to `parentItem` / `visibleAncestorIndex`.
- **item+280** meshType 0/1/2 -> affineCopy/bezierPatchCopy/meshCopy. Confirmed in
  BOTH 0x6C4E28 (leaf) and 0x6C7440 (buffered bufLayer + direct operate*).
- **clipRect item+216..228 is float[4]** (binary writes `*(float*)(item+216)=v80`).
  **2026-07-23 correction:** local 与 harness 均已改为 `std::array<float,4>`；原 J5 类型偏差关闭。

## Findings

| id | func@addr | local file:line | sev | one-line |
|----|-----------|-----------------|-----|----------|
| J1 | 0x6C4E28 leaf emit vs 0x6C7440 buffered/direct | PlayerRenderExecute.cpp:13-240 (buildRenderCommands) + 243-1303 (executeLayerRenderCommands) | 🔧 | Binary emits leaf copies in 0x6C4E28 (pre-walk) and FINAL submission in 0x6C7440 top-level loop — TWO passes, two layer tiers (leaf item+304 → operateRect into v370). Local folds leaf-copy into the SAME executeLayerRenderCommands top-level loop via buildItemOutput recursion; the 0x6C4E28 pre-walk leaf-emit is NOT replicated as a separate pass (build loop only latches +20/+21/clip, no affineCopy). Value-equiv on simple items, phase+structure divergent. |
| J2 | 0x6C7440 buffered/direct routing | PlayerRenderInternal.cpp:673-681 shouldUseDirectRenderPathLike | ✅ | Direct gate = (nibble==0 \|\| nibble>5) && !clearEnabled(player+1144) && !item+264. Local: `!clearEnabled && visibleAncestorIndex<0 && (lowNibble==0\|\|>5)` + caller also requires parentItem==null. ALIGNED (item+264==null ≡ parentItem==null ≡ visibleAncestorIndex<0). |
| J3 | 0x6C7440 blend map | PlayerRenderInternal.cpp:651-671 resolveBlendOperationModeLike | ✅ | switch 1->0xE,2/5->0xF,3->0x10,4->0x11,0/default->0x2. EXACT match. |
| J4 | 0x6C7440 tail | PlayerRenderExecute.cpp:1291-1298 | ❌ | Binary tail is ONLY setClip(argc0) reset on v370 + release; there is NO renderLayer.Update() in 0x6C7440. Local calls `renderLayer->Update(false)` when !skipUpdate (line 1293). Update has no binary counterpart in this fn (Update lives in updateLayerAfterDraw 0x6CE7D8 / SLA paths). Extra side-effect. |
| J5 | item+216..228 clipRect | PreparedRenderItem clipRect `std::array<float,4>` | ✅ 2026-07-23 | Binary/local/harness 均为 float[4]；原 int 截断偏差已关闭。 |
| J6 | 0x6C6B48/0x6CBCE4 absolute | PlayerRenderExecute.cpp:395-406 ensureLeafItemLayer | ❌ | Binary leaf/composed Layer.absolute = node.x(node+160)+node.y(node+164); leaf path then does ++node+164 (monotonic per-acquire bump). Local sets `state.absolute = _nextLayerAbsolute++` (pure global counter, ignores node x+y). Different absolute-order seed. |
| J7 | 0x6C4E28 leaf via acquireLeafLayerById Rb_tree (player+760 SLA) | PlayerRenderExecute.cpp:395 _renderLayerStates[id] | 🔧 | Binary leaf layers live in a std::map<int,LayerSlot> (Rb_tree) ON THE SLA object (player+760), keyed by item+424, cached at map-node+40. Local uses Player::_renderLayerStates (a map keyed by layerId) but NOT hung off a SeparateLayerAdaptor's internal tree; container topology approximated. |
| J8 | 0x6C9CA8 accurate-SLA | PlayerRenderExecute.cpp:587-703 (HEADLESS-only checkpoint) | 🔧 | Binary accurate path builds a persistent per-item SLA Layer tree (assignImages + drawMeshFrame/drawBezierPatchMeshFrame/drawLine/setPos/type/visible/opacity). Local only has a diagnostics-only `renderAccurateSlaPostDrawCandidateLike_0x6C9CA8` under KRKR2_WASMTIME_HEADLESS that re-runs affineCopy onto a candidate layer — NOT the persistent SLA-tree architecture. isAccurateSlaRenderEnabled() exists but the real accurate branch is unimplemented. |
| J9 | 0x6C7440 preview opacity | PlayerRenderExecute.cpp:1011-1013 | ⚠ | Binary @0x6c7668: opa = preview ? (item+232 >>1 sign-adj) : item+232. Local clamps item.opacity to [0,255] but does NOT halve under preview. Missing preview opacity-halving. (Low impact: preview path rarely active.) |
| J10 | 0x6C4E28 / 0x6C7440 mesh point arrays | PlayerRenderInternal.cpp:207-224 buildMeshPointTJSArrayLike | ✅ | sub_6C715C produces TJS Array(x,y,x,y..) interleaved doubles, translated by offset. Local builds TJSCreateArrayObject + PropSetByNum (TJS Array dispatch, NOT std::vector). ALIGNED container choice. |
| J11 | 0x6C4E28 leaf neutralColor | PlayerRenderInternal.cpp:595-613 prepareLayerForRender | ⚠ | Binary 0x6C4E28 sets leaf neutralColor=0 (PropSet "neutralColor") before setSize, NOT a fillRect. Local prepareLayerForRender does SetImageSize+SetSize+SetClip+FillRect(clearColor). The leaf path in binary relies on neutralColor + affineCopy(clear=1) to initialize; local fillRect is a coarser equivalent. |
| J12 | drawCompat dispatch 0x6D5FB8 | PlayerDrawDispatch.cpp:106-296 | ✅ | D3DAdaptor->drawD3D, SLA->DrawSLA, else build+branch(d3dDrawMode)+applyTranslateOffset+renderToCanvas+updateLayerAfterDraw. Routing structure matches cluster-I call graph. (D3D/SLA leaf paths are cluster-K/D scope.) |

## Architecture-level (🔧) — standing divergences

- **J1/J7 (leaf-tier split)**: The binary materializes per-item LEAF layers
  (item+304) in 0x6C4E28's pre-walk and only SUBMITS them (operateRect) in
  0x6C7440's top-level loop. There are two distinct Layer tiers (leaf at item+304,
  composed at item+324) living in Rb_trees on the SLA. The local port collapses
  this into one `buildItemOutput` recursion inside executeLayerRenderCommands,
  with `_renderLayerStates` standing in for the SLA Rb_tree. This is the
  continuation of the cluster-I "build/execute phase + STL container" divergence,
  now seen to be even more structural: the binary's emit happens in a SEPARATE
  function pass, not just a separate loop. Reconciling requires (a) splitting
  the leaf-copy emit (0x6C4E28 Loop A) out of executeLayerRenderCommands into a
  dedicated pre-walk, (b) the group/composed Loop B as a second pass, (c) the
  final operateRect/operate* submission as the 0x6C7440 top-level loop. High
  regression risk; gate behind CI per prior guidance.

- **J8 (accurate SLA)**: 0x6C9CA8's persistent SLA-tree render is essentially
  unimplemented (only a headless diagnostics checkpoint). This is a whole
  alternate render architecture (drawMeshFrame/setPos/type on child layers)
  selected by ogl_accurate_render. Needs a from-design port if accurate render
  is to be reproduced; currently the non-accurate single-composite path
  (0x6C7440) is the only real local path.

## Local-only / port-invented elements (flagged, not binary-derived)

- `executeLayerRenderCommands` direct-vs-buffered `useDirectRenderPath` extra
  terms `!hasChildren && !skipFlag0 && !rawFlag16 && !(preview&&skipFlag1) &&
  opacity>0` (PlayerRenderExecute.cpp:801-805): the binary's per-item gate in
  0x6C7440 already filtered skipFlag0/16/232 at loop top and skipFlag1 at the
  preview gate, so re-testing them inside buildItemOutput is redundant but not
  wrong. The `!hasChildren` term is a LOCAL proxy for binary `item+264==null`
  driving the buffered route — but note binary routes on item+264 (PARENT), not
  on having children. A leaf item WITH children but no parent still goes DIRECT
  in the binary (item+264==0). Local `!hasChildren` would force it buffered →
  POTENTIAL divergence for parent-of-children-but-rootless items. (J2-adjacent;
  flagged for verification, low frequency.)

- `_clearEnabled` (player+1144) drives both the clear flag in copy calls AND the
  buffered route (binary case-0 `if(player+1144) buffered`). Local
  shouldUseDirectRenderPath checks `!clearEnabled` — matches.

## Sub-function alignment status

| sub | role | status |
|-----|------|--------|
| 0x6C4E28 Player_emitRenderItem_requireLayer | leaf emit + group compose | 🔧 phase/structure folded into execute (J1) |
| 0x6C7440 Player_renderToCanvas | top-level submit | ⚠ aligned routing, ❌ extra Update (J4), ⚠ preview opa (J9) |
| 0x6C6B48 Player_acquireLeafLayerById | leaf Rb_tree | 🔧 container + ❌ absolute seed (J6/J7) |
| 0x6CBCE4 Player_acquireComposedLayerById | composed Rb_tree | 🔧 (only reached via accurate SLA, J8) |
| 0x6C9CA8 Player_renderAccurateSLA | accurate path | 🔧 unimplemented (J8) |
| 0x6C1B70 loadSource | source texture | ✅ resolveSourceObjectLike_0x6C1B70 (cluster-K scope; signature sig(buf,player,key) confirmed) |
| Motion_doAlphaMaskOperation 0x6AF104 | alpha mask | ✅ applyMotionAlphaMaskLike_0x6AF104 (per-pixel, itemFlags 1/2/5/6; matches) |
| 0x6C715C buildMeshArray | TJS Array | ✅ buildMeshPointTJSArrayLike (TJS dispatch, J10) |

## Platform-boundary segments (skipped, listed for reviewer)

- PlayerRenderExecute.cpp:248-326, 533-704, 1052-1126, 1207-1282 — all under
  `#if defined(KRKR2_WASMTIME_HEADLESS)`: trace scopes, SNAP* fprintf snapshots,
  emitDirectExecuteDiagnostics, renderAccurateSlaPostDrawCandidate. These are
  DIAGNOSTIC instrumentation, not render logic. No explicit `// PLATFORM_BOUNDARY`
  comment but the `KRKR2_WASMTIME_HEADLESS` guard is the equivalent gate.
  NOTE: J8's accurate-SLA helper lives inside this guard — it is the only place
  the accurate path is even gestured at, which is why J8 is 🔧 (the real
  architecture is absent, not platform-bounded).
- PlayerRenderInternal.cpp:1009-1295 — FirstPixelProbe / emitDirectExecuteDiagnostics
  (HEADLESS diff probes). Skipped.
- No `// PLATFORM_BOUNDARY:`-tagged segments found in any of the 5 scope files.

## IDB changes applied (idb_save done)

- rename 0x6C7440 Player_renderToCanvas_guess -> Player_renderToCanvas
- rename 0x6C6B48 -> Player_acquireLeafLayerById
- rename 0x6CBCE4 Player_acquireLayerById -> Player_acquireComposedLayerById
- rename 0x6C9CA8 -> Player_renderAccurateSLA
- comments @0x6C7440 (full execute structure), @0x6C6B48 (leaf Rb_tree absolute=x+y +
  ++y), @0x6CBCE4 (composed Rb_tree), @0x6C9CA8 (accurate SLA architecture)

## Audit verdict: ⚠ PARTIAL DEVIATION (with 🔧 architectural items)

Routing/blend/mesh-dispatch/alpha-mask logic is well aligned and uses TJS
dispatch (not STL shortcuts) where it counts. The standing divergences are
architectural (J1/J7/J8 — leaf/composed two-tier Rb_tree split across two
functions, accurate-SLA tree absent) plus three local fixable points:
**J4 (extra Update() call), J9 (missing preview opacity halving), J6 (absolute
seed = x+y not a counter)**. J4/J9/J6 can be patched on the existing data flow;
J1/J7/J8 require the structural refactor flagged since cluster-I (do under CI).
