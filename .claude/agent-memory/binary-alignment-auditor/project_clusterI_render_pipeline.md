---
name: clusterI-render-pipeline
description: Player render pipeline (libkrkr2.so) verified call-graph + sub_6C4E28 re-mapping + skipFlag1/rawFlag20/node+48 verdicts
metadata:
  type: project
---

CLUSTER I render pipeline audit 2026-05-30 (Player draw path). Authoritative decompile facts:

**Draw dispatch**: Player_draw_NCBWrapper@0x6818D0 (this=*(player+1064)) -> Player_drawCompat@0x6D5FB8 (==sub_6D5FB8, 3-way: D3DAdaptor/SLA/layer). Layer path: sub_6D5164 builds mainList+boundsList -> if player+909(wasD3DMode) D3D capture else applyTranslateOffset+Player_renderToCanvas_guess@0x6C7440+Player_updateLayerAfterDraw@0x6CE7D8.

**KEY re-mapping (non-obvious)**: sub_6C4E28 (renamed Player_emitRenderItem_requireLayer) is NOT a build-tree fn — it is the PER-ITEM EXECUTOR called from Player_renderToCanvas_guess@0x6c756c AND sub_6C9CA8@0x6c9e74 (accurate SLA). It does clip+requireLayerId(LABEL_28: item+424=layerId, item+20=1)+TJS emit (setSize/fillRect/affineCopy/meshCopy/bezierPatchCopy). So requireLayerId/item+20 materialization is EXECUTE-phase in binary; local port moved it to build loop (commit d51cce9) — value-equiv (trace 0-diff) but phase-divergent.

**Player_buildRenderTree_guess@0x6CBCE4 is MIS-NAMED**: actually acquireLayerById — Rb_tree<int,LayerVariant> lookup-or-create, builds Layer TJS obj absolute=node.x+y hitThreshold=256.

**item+18 VERDICT = ALIGNED（2026-07-23 纠正）**: binary sub_6C2334@0x6c33c0 item+18 = `(a6&1)?1:(node+48!=0)` = inheritedFlag18||(node+48!=0). node+48 PROVEN=priorDraw: sub_6BC4F0@0x6bc6c4 `node+48=sub_6636D4(emoteEdit,"priorDraw")&1` only if node+1996(forceVisible) else 0@0x6bc67c (byte 0/1). Local `appendPreparedRenderItems(..., inheritedFlag18)` 直接携带 a6，子 Player 递归直接传 `inheritedFlag18 || ownerNode.priorDraw != 0`；item+18 按原极性存入历史遗留名称 `skipFlag1`，harness 直接输出该 bool，已无 `_renderItemInheritedFlag18` 侧挂或反相层。Residual m2logo items[1] frame12+ build_flow mismatch is NOT a formula bug — likely phase placement (I1) or per-frame priorDraw/forceVisible timing.

**rawFlag20 VERDICT = value-aligned, phase-divergent**: binary latches item+20=1 ONLY at LABEL_28 (drawFlag19 && drawable && item+20==0), execute phase.

**Item layout**: binary item = operator new(0x1B0)=432B raw block, alloc @0x6c2754. Key offsets: +16 rawFlag16=node+201, +17 skipFlag0=((preview?1097:1089)&(1<<nodeType))==0, +18 skipFlag1, +19 drawFlag=node+1960?1:node+1961, +20 layerId-latch, +21 drawable-clip, +136..164 corners=node+1856..1884, +184..196 paintBox=node+1888..1900, +200..212 viewport=node+1936 chain, +216..228 clipRect FLOAT, +232 opacity=node+1576, +244 stencilComposite=node stencilType, +248 context=player+1012, +280 meshType=node+2000, +304 leafLayer, +324 composedLayer, +424 layerId. **2026-07-23 correction:** local clipRect 已改为 float[4]；live item 由 MotionNode 持久拥有，caller-stack lists 借用其指针。0x1B0 仍只是 ARM64 ABI 尺寸，不是 wasm32 padding 目标。

**Player_DrawSLA@0x6D5658** branches on byte_1AB84F4 (config ogl_accurate_render OR hasGPUAccel): OFF->ResolveSLATarget@0x6D5948+RenderMotionFrame@0x6DE738; ON->sub_6C9CA8+sub_6CE938. Player_drawToLayerCompat@0x6D2D80 (gate player+544) = nodeType==3 recursive child fillRect+self-recursion.

Ledger: analysis/audit_motionplayer_2026-05-30/clusterI_render_pipeline.md
