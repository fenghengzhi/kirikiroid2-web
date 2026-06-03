---
name: render-dispatch-funccall-contract
description: sub_6C7440/0x6C4E28 route ALL draw primitives through iTJSDispatch2::FuncCall(vtbl+16) on the render-layer INSTANCE with UTF-16 keys; exact arg arrays for the 4 missing mesh/bezier methods
metadata:
  type: project
---

Binary render submit (0x6C7440 main, 0x6C4E28 SLA) dispatch EVERY draw primitive via
`(*(vtbl+16))(layerInstance, 0, L"<key>", &typehintCache, 0, argc, &argArray, layerInstance)`
= iTJSDispatch2::FuncCall on the render-layer INSTANCE itself, objthis = same instance
(NOT on the Layer class object). PropSet preamble uses vtbl+48 (PropSet, flag 512=TJS_MEMBERENSURE):
key, src, blendMode, neutralColor. setSize(2)/setClip(4 or 0).

**Why:** APPROVED migration 2026-06 — local motionplayer used native direct calls
(targetLayer->MeshCopy/AffineCopy etc.) for leaf/composed/mesh/bezier; only direct-affine
went through callLayerOperateAffineLike_0x6C7440. Faithful alignment = route through FuncCall.

**How to apply:** when porting the render hot path, build a TJS variant array matching the
binary's packed layout and FuncCall the layer instance. Type tags in decompile: variant first
dword 4=tvtInteger, 5=tvtReal, object copied via sub_A0F5E0. Mesh/bezier point arrays built by
sub_6C715C = TJS Array of interleaved doubles (x,y,x,y) each translated by (-0.5-clipOrigin) for
copy variants or (-0.5,-0.5) for direct operate variants.

Primitive keys reached by RENDERED cases (verified via sub_6C7440 packing):
- operateAffine(15), operateBezierPatch(11), operateMesh(11), operateRect(9)  [direct path]
- affineCopy(14), bezierPatchCopy(10), meshCopy(10)  [buffered leaf / SLA path]
- fillRect(4 main / 5 SLA), setSize(2), setClip(4|0)  [setup/clear]
NOT reached (debug-overlay gated by player+1048||player+1068):
drawMeshFrame(5), drawBezierPatchFrame(3), drawBezierPatchMeshFrame(5), drawLine(5) — DO NOT register.

EXACT packed arg arrays (from 0x6C7440, identical in 0x6C4E28):
- meshCopy(10):       [srcObj, srcx=0, srcy=0, srcw, srch, meshPtArray(+344, off -0.5-clip), divx(+272), divy(+276), stretchType(player+1144), clear=1]
- bezierPatchCopy(10):[srcObj, srcx=0, srcy=0, srcw, srch, bezPtArray(+400, off -0.5-clip), bdivx(computed +368/+256), bdivy, stretchType(player+1144), clear=1]
- operateMesh(11):    [srcObj, srcx=0, srcy=0, srcw, srch, meshPtArray(+344, off -0.5,-0.5), divx(+272), divy(+276), blendMode=2, opacity(v24), clear=0]
- operateBezierPatch(11):[srcObj, srcx=0, srcy=0, srcw, srch, bezPtArray(+400, off -0.5,-0.5), bdivx, bdivy, blendMode=2, opacity(v24), clear=0]

MIGRATION DONE 2026-06-03 (APPROVED). Registered 4 TJS methods on tTJSNC_Layer
(LayerIntf.cpp, after operateStretch): meshCopy/bezierPatchCopy(argc>=8)/operateMesh/
operateBezierPatch(argc>=8). Added TVPDecodeLayerMeshPointArray (LayerIntf.cpp, GetCount+
PropGetByNum, mirrors sub_6A0CF0). Added dispatch helpers in PlayerRenderInternal.{h,cpp}:
buildMeshPointTJSArrayLike_0x6C715C, callLayerAffineCopyLike/MeshCopyLike/BezierPatchCopyLike/
OperateMeshLike/OperateBezierPatchLike/OperateRectLike_0x6C7440 (all FuncCall on layer INSTANCE,
objthis=self). Converted PlayerRenderExecute.cpp sites: leaf renderItemSourceToLayer (now takes
source.object) affineCopy/meshCopy/bezierPatchCopy; direct operateMesh/operateBezierPatch;
accurate-SLA checkpoint; 2x operateRect (top-level submit + composed-child compose). TJS Array
built refcount 1, wrapped in variant (AddRef->2), Release after FuncCall (->1, variant holds),
freed on variant dtor. AUDIT: blendMode passed to operate* is the local resolveBlendOperation-
ModeLike value (NOT binary literal 2) — this is PRE-EXISTING local behavior (operateRect/
operateAffine already did it; logo common case resolves to omAlpha=2 == binary), preserved
verbatim, behavior-preserving. NOT converted (out of scope, documented): CopyRect (leaf->composed
blit, not in 12-primitive set), SetClip/ResetClip/Update (layer-state ops), PropSet key/src/
blendMode/neutralColor + setSize preamble (lives in aligned source-cache/prepareLayerForRender
subsystem; converting = materially larger restructure). RESULT: web+wasmtime built clean;
motion_playback differential m2logo(93)+yuzulogo(243) PASS bit-identical.

Local: native impls EXIST (tTJSNI_BaseLayer::MeshCopy/OperateMesh/BezierPatchCopy/OperateBezierPatch
take tTVPPointD* points, divx, divy, src, srcrect, [mode,opa,] type, clear). Only
operateAffine/affineCopy/operateRect/fillRect/setSize/setClip were registered as TJS methods in
LayerIntf.cpp. Mesh/bezier 4 methods registered 2026-06 to close the dispatch surface.

Real tTJSNC_Layer.meshCopy callback = sub_6A1D70, operateMesh = sub_6A1F24,
bezierPatchCopy = sub_6A299C, operateBezierPatch = sub_6A2B50 (registered by sub_6A3E24 onto
Layer class). Internal native worker sub_6A0CF0 reads the point array via Motion_propGetCount +
Motion_propGetIndexDouble (len/2 points). Authoritative arg order = 0x6C7440 packed array, NOT
the internal callback positional decode.
