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
| 0x6C6B48 | Player_acquireLeafLayerById (was sub_) | Rb_tree get-or-create on SLA(player+760) keyed by item+424; new Layer absolute=SLA+160 base + SLA+164 sequence, then ++sequence, hitThreshold=256 |
| 0x6CBCE4 | Player_acquireComposedLayerById (was Player_acquireLayerById) | Rb_tree get-or-create keyed by item+56(=node+20); new Layer absolute=node.x+y, hitThreshold=256. Used by accurate-SLA composed child |
| 0x6C9CA8 | Player_renderAccurateSLA (was sub_) | ogl_accurate_render path; builds persistent SLA child Layer tree (drawMeshFrame/drawLine/setPos), NOT a single-layer composite |

## CRITICAL re-confirmation of phase structure (binary ground truth)

The binary's draw execute is a **two-function pipeline** that runs inside the
SAME top-level draw call (`Player_renderToCanvas` 0x6C7440):

```
Player_renderToCanvas(player, a2, a3=mainList, a4=cameraOffset[4]):   // 0x6C7440
  Layer = global.Layer (sub_5CB08C)
  bufW = Layer.width(v9), bufH = Layer.height(v9)   // v9 = render-layer obj
  if (!player+1096 /*priorDraw*/):
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
      if (player+1096 /*priorDraw*/ && !item+18) continue          // priorDraw gate
      opa = item+232; if (priorDraw) opa = signed_divide_by_2(opa)
      DirtyRectList_AddRect(player+864, truncTowardZero(paintBox))
      ... PropSet key/src/blendMode on cache objects, sub_6C1B70 loadSource ...
      v48 = blendMode map: switch(item+48 & 0xF){1->14,2/5->15,3->16,4->17,
            0/default-> if(player+1144 completionType!=0) buffered(v48=2)
                        elif(item+264) buffered(v48=2) else DIRECT}
      if BUFFERED:    bufLayer=ctx(player+656).bufLayer;
                      L/T=max(paintBox,0), R/B=ordered-min(paintBox,target size);
                      if(R<L) continue; bufLayer.setSize(Real W,Real H);
                      affineCopy/meshCopy/bezierPatchCopy(type=completionType,
                                                         clear=1) onto bufLayer;
                      walk item+264 ancestor chain: Motion_doAlphaMaskOperation;
                      invalid ancestor flags&3==1: fillRect(argc=4) fails, ignore+break;
                      v370.operateRect(Real L,T,bufLayer,0,0,Real W,H,v48,opa)
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
     clip = a4(camera) ∩ paintBox; valid viewport alone is floor/ceil-narrowed
     if (clip.l<clip.r && clip.t<clip.b && !item+16):
        item+21 = 1; write item+216..228 = clip
        if (player+760 /*SLA*/): if(!item+20) LABEL_28
        else: create SLA from Window.mainWindow.primaryLayer, then LABEL_28
        LABEL_28: requireLayerId (numparams=0 FuncCall) -> item+424; item+20=1
        leaf = acquireLeafLayerById(SLA, item+424, payload, &refresh)
        if (!refresh) continue;                    // item+304 still owns leaf
        leaf.neutralColor=0; leaf.setSize(Real clip.w, Real clip.h)
        switch(item+280 meshType): 0->affineCopy 1->bezierPatchCopy 2->meshCopy
          (all onto LEAF layer, points pre-translated -0.5-clipOrigin)
     else: item+21 = 0
  // Loop B over *a3 (groupList) — per composite/group item:
  for grp in *a3:
     seed with grp paintBox; union child (child+21) paintBox values
     cameraTuple = union ∩ a4; only this tuple controls empty
     finalTuple = valid-viewport narrowing of cameraTuple
     if (cameraTuple empty): grp+21 = 0
     else: if(!grp+340): create composed Layer via Window.mainWindow + Layer ctor -> grp+324
           composed.setSize(Real W,Real H); composed.fillRect(0,0,Real W,Real H,0)
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
  (b) the alpha-mask ancestor walk. Local maps to `parentItem`; `visibleAncestorIndex`
  is diagnostics-only and does not participate in this route.
- **item+280** meshType 0/1/2 -> affineCopy/bezierPatchCopy/meshCopy. Confirmed in
  BOTH 0x6C4E28 (leaf) and 0x6C7440 (buffered bufLayer + direct operate*).
- **clipRect item+216..228 is float[4]** (binary writes `*(float*)(item+216)=v80`).
  **2026-07-23 correction:** local 与 harness 均已改为 `std::array<float,4>`；原 J5 类型偏差关闭。

## Findings

| id | func@addr | local file:line | sev | one-line |
|----|-----------|-----------------|-----|----------|
| J1 | 0x6C4E28 leaf emit vs 0x6C7440 buffered/direct | `buildRenderCommands` + `executeLayerRenderCommands` | ✅ 2026-07-23 | 双函数管线已拆开。0x6C4E28 生成 leaf/composed；0x6C7440 direct 直接提交 current source，而 buffered 不借用 leaf/composed 作为最终 source，而是每项把同一次 resolver 结果复制到持久 RM.bufLayer，再做 ancestor mask + operateRect。旧 `buildItemOutput` 递归与普通路径对 `Player::_renderLayerStates` 的使用已删除；该容器仍只残留于 HEADLESS accurate-SLA 诊断路径，不能宣称字段本身已删除。 |
| J2 | 0x6C7440 buffered/direct routing | `shouldUseDirectRenderPathLike_0x6C7440` | ✅ 2026-07-23 纠错 | Direct gate = `(nibble==0 || nibble>5) && completionType(+1144)==0 && item+264==null`。+1144 由 NCB 字面 `completionType` 绑定确认，不是 clearEnabled；本地只读 `_completionType` 和 `parentItem`，不再混入 childItems/visibleAncestorIndex。 |
| J3 | 0x6C7440 blend map | PlayerRenderInternal.cpp:651-671 resolveBlendOperationModeLike | ✅ | switch 1->0xE,2/5->0xF,3->0x10,4->0x11,0/default->0x2. EXACT match. |
| J4 | 0x6C7440 tail | `executeLayerRenderCommands` tail | ✅ 2026-07-23 | Binary tail is ONLY setClip(argc0) reset on v370 + release；本地已删除额外 `renderLayer->Update(false)`，Update 仍归属 updateLayerAfterDraw 0x6CE7D8 / SLA 路径。 |
| J5 | item+216..228 clipRect | PreparedRenderItem clipRect `std::array<float,4>` | ✅ 2026-07-23 | Binary/local/harness 均为 float[4]；camera clamp 后无 viewport 时保留分数，leaf/composed `setSize` 接收 Real，child mask 只在最终 float 差值上 FCVTZS；原“仅存储 float、消费仍提前整化”的偏差已关闭。 |
| J6 | 0x6C6B48/0x6CBCE4 absolute | `SeparateLayerAdaptor::resolveRenderLayerNodeLike_0x6C6B48` | ✅ 2026-07-23 | Binary 是 SLA base(+160)+sequence(+164)，leaf acquire 后 sequence++；本地 SLA `_absolute + _assignSequence` 后自增，保留同一 owner 与计数生命周期。旧报告把 +160/+164 误读为 node x/y。 |
| J7 | 0x6C4E28 leaf via acquireLeafLayerById Rb_tree (player+760 SLA) | `SeparateLayerAdaptor::{begin,end}RenderLayerPassLike_0x6C4E28` + `resolveRenderLayerNodeLike_0x6C6B48` | ✅ 2026-07-23 | 普通 0x6C4E28 入口按 `0x6C4E74..0x6C4F14` swap active/retired 两棵树并清零 sequence；acquire 从 retired 同 key 搬回复用，否则创建。只有两个 loop 正常结束才按 `0x6C63B8 → 0x6C72E4` Invalidate 并清理未复用 retired；异常 unwind 不执行尾部清理，故本地使用显式 begin/end 而非 RAII。`Player::_renderLayerStates` 仅残留于 HEADLESS accurate-SLA 诊断（J8）。 |
| J8 | 0x6C9CA8 accurate-SLA | PlayerRenderExecute.cpp:587-703 (HEADLESS-only checkpoint) | 🔧 | Binary accurate path builds a persistent per-item SLA Layer tree (assignImages + drawMeshFrame/drawBezierPatchMeshFrame/drawLine/setPos/type/visible/opacity). Local only has a diagnostics-only `renderAccurateSlaPostDrawCandidateLike_0x6C9CA8` under KRKR2_WASMTIME_HEADLESS that re-runs affineCopy onto a candidate layer — NOT the persistent SLA-tree architecture. isAccurateSlaRenderEnabled() exists but the real accurate branch is unimplemented. |
| J9 | 0x6C7440 priorDraw opacity | `executeLayerRenderCommands` | ✅ 2026-07-23 | 入口只以 raw `opacity==0` 跳过；priorDraw 用 source-level signed `/2`（AArch64 add-sign+ASR 展开），之后不再 gate，也不 clamp。负值、>255、折半后为 0 均原样封成 Integer Variant。 |
| J10 | 0x6C4E28 / 0x6C7440 mesh point arrays | PlayerRenderInternal.cpp:207-224 buildMeshPointTJSArrayLike | ✅ | sub_6C715C produces TJS Array(x,y,x,y..) interleaved doubles, translated by offset. Local builds TJSCreateArrayObject + PropSetByNum (TJS Array dispatch, NOT std::vector). ALIGNED container choice. |
| J11 | 0x6C4E28 leaf neutralColor | `emitLeafLayerCopyLike_0x6C4E28` | ✅ 2026-07-23 | Leaf 先以 `TJS_MEMBERENSURE` 写 Integer `neutralColor=0`，再以 Real W/H 调 `setSize`，最后 copy(clear=1)；三次返回值均不再作为本地恢复 gate。 |
| J12 | drawCompat dispatch 0x6D5FB8 | PlayerDrawDispatch.cpp:106-296 | ✅ | D3DAdaptor->drawD3D, SLA->DrawSLA, else build+branch(d3dDrawMode)+applyTranslateOffset+renderToCanvas+updateLayerAfterDraw. Routing structure matches cluster-I call graph. (D3D/SLA leaf paths are cluster-K/D scope.) |

## Architecture-level (🔧) — standing divergences

- **J1/J6/J7 普通路径已关闭**：0x6C4E28 现在独立执行 leaf emitter 与
  group compose，并通过 SLA 自身的 ordered-map/reuse 池持有 item+304；
  0x6C7440 则保持 flat top-level submit，buffered 使用 RM.bufLayer。旧的
  `buildItemOutput` 递归已删除。`Player::_renderLayerStates` 字段尚未删除，
  但只被下述 HEADLESS accurate-SLA 诊断消费，不能再归因给普通路径。

- **J8 (accurate SLA)**: 0x6C9CA8's persistent SLA-tree render is essentially
  unimplemented (only a headless diagnostics checkpoint). This is a whole
  alternate render architecture (drawMeshFrame/setPos/type on child layers)
  selected by ogl_accurate_render. Needs a from-design port if accurate render
  is to be reproduced; currently the non-accurate single-composite path
  (0x6C7440) is the only real local path.

- **0x6C6B48 caller payload type仍开放**：本地已经删除从旧 Layer 读取
  type/visible/left/top/width/height 的错误副作用，并恢复 acquire 返回 byte 对 leaf
  refresh body 的 gate；但 `0x6C5264..0x6C532C` 构造的 command/completion/geometry
  caller-local payload尚未形成一只与二进制源码同构的专用值类型，当前仅传值初始化占位。

- **普通 SLA pass 生命周期已闭合**：`buildRenderCommands` 在既有 SLA 或 loop 内 lazy-create
  SLA 两种入口都执行同一 active/retired swap + sequence reset；正常尾部才 Invalidate/clear
  retired。该清理没有包成 scope guard，因为 `0x6C4E28` 的 landing pad 不调用
  `0x6C72E4`。这只闭合容器/对象退休生命周期，不消除上面的 caller payload 缺口。

## Local-only / port-invented elements (flagged, not binary-derived)

- **2026-07-23 证伪并删除**：旧 `buildItemOutput` 内的 `hasChildren`、
  `visibleAncestorIndex`、重复 skip/opacity gate 都不是 0x6C7440 的结构分流字段。
  它们已从生产路径删除；root item 即便有 children，只要 parent null、
  completionType 0、blend 低位为 0 或 >5，仍走 direct。

- **2026-07-23 证伪并纠正**：Player+1144 是 `completionType` int，不是
  `_clearEnabled`。它控制 direct/buffered，并作为 leaf/buffered
  affine/mesh/Bezier copy 的 `type` 参数；direct `operateAffine` 的末参是字面
  Integer 0，direct mesh/Bezier 的 `clear` 也是字面 0。

## Sub-function alignment status

| sub | role | status |
|-----|------|--------|
| 0x6C4E28 Player_emitRenderItem_requireLayer | leaf emit + group compose | ⚠ 主调用链、float 边界、Variant tag/refresh gate已复刻；acquire caller payload值类型仍开放 |
| 0x6C7440 Player_renderToCanvas | top-level submit | ⚠ 主循环/边界已对齐；本地仍拆成入口 wrapper + executor 两个源码函数 |
| 0x6C6B48 Player_acquireLeafLayerById | leaf Rb_tree | ✅ SLA ordered-map/reuse owner + absolute sequence（J6/J7） |
| 0x6CBCE4 Player_acquireComposedLayerById | composed Rb_tree | 🔧 (only reached via accurate SLA, J8) |
| 0x6C9CA8 Player_renderAccurateSLA | accurate path | 🔧 unimplemented (J8) |
| 0x6C1B70 loadSource | source Layer | ✅ Player-owned identity fast path + ResourceManager fallback；execute 现场保持 descriptor→color→result→source-accessor owner 链 |
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

## Audit verdict: ⚠ PARTIAL DEVIATION（2026-07-23 当前）

普通 Layer 路径的 routing、float clip、direct/buffered dispatch、RM.bufLayer、
ancestor mask、TJS receiver/objthis、owner 逆序析构与异常传播已经按 fresh
`0x6C4E28/0x6C7440` 证据落地；J4/J5/J6/J9/J11 的旧偏差均已纠正。仍不能称
100%：J8 accurate-SLA 持久树尚未实现，`0x6C6B48` caller-local command payload
尚无同构值类型，且二进制单体 `0x6C7440` 在本地仍拆为 wrapper + executor。
