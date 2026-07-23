---
name: render-dispatch-funccall-contract
description: 0x6C7440/0x6C4E28 的 TJS receiver/objthis 合约；target 操作走 Layer class receiver，leaf/composed/buf copy/setup 走实例 receiver
metadata:
  type: project
---

**2026-07-23 fresh correction（取代旧 “ALL instance receiver” 结论）：**

- `0x6C7440` 的 target `width/height` PropGet、`setClip(4|0)`、direct
  `operateAffine/operateMesh/operateBezierPatch` 与最终 `operateRect` 都在同一
  global `Layer` class accessor 上 dispatch，target instance 只作为 `objthis`。
- leaf、composed、RM.bufLayer 上的 `neutralColor`、`setSize`、`fillRect`、
  `affineCopy/meshCopy/bezierPatchCopy` 则以各自 Layer instance 同时作为
  receiver 与 `objthis`。
- source descriptor/color 的 `PropSet(TJS_MEMBERENSURE)` 走各自 Dictionary
  accessor；source width 后 height走 source instance accessor。

因此不能再把 `(*(vtbl+16))(receiver, ..., objthis)` 中的两者一概写成同一
instance；receiver 选择本身就是可观察的 TJS 调用链。

**Why:** APPROVED migration 2026-06 — local motionplayer used native direct calls
(targetLayer->MeshCopy/AffineCopy etc.) for leaf/composed/mesh/bezier; only direct-affine
went through callLayerOperateAffineLike_0x6C7440. Faithful alignment requires FuncCall
with the receiver/objthis pair selected at each binary call site, not a generic
“always use the instance as receiver” rule.

**How to apply:** when porting the render hot path, build a TJS variant array matching the
binary's packed layout. FuncCall leaf/composed/RM.bufLayer setup and copy methods on the
Layer instance itself; FuncCall target width/height/setClip/direct operate*/operateRect
on the global Layer class accessor with the target instance as objthis. Type tags in
decompile: variant first dword 4=tvtInteger, 5=tvtReal, object copied via sub_A0F5E0.
Mesh/bezier point arrays built by sub_6C715C = TJS Array of interleaved doubles
(x,y,x,y) each translated by (-0.5-clipOrigin) for copy variants or (-0.5,-0.5)
for direct operate variants.

Primitive keys reached by rendered cases (verified via sub_6C7440 packing):
- operateAffine(15), operateBezierPatch(11), operateMesh(11), operateRect(9)  [direct path]
- affineCopy(14), bezierPatchCopy(10), meshCopy(10)  [buffered leaf / SLA path]
- fillRect(4 buffered ancestor / 5 compose), setSize(2), setClip(4|0) [setup/clear]
OPEN debug-overlay surface (gated by player+1048||player+1068 in the binary):
drawMeshFrame(5), drawBezierPatchFrame(3), drawBezierPatchMeshFrame(5), drawLine(5).
These calls may be absent from ordinary fixtures, but that does not make them unreachable
or forbidden. Their Layer registration/implementation and the common overlay tail are
still missing locally and must be reconstructed from the binary before closing 0x6C7440.

EXACT packed arg arrays (copy forms occur in both paths; operate forms in 0x6C7440):
- meshCopy(10):       [srcObj, srcx=0, srcy=0, srcw, srch, meshPtArray(+344, off -0.5-clip), divx(+272), divy(+276), completionType(player+1144), clear=1]
- bezierPatchCopy(10):[srcObj, srcx=0, srcy=0, srcw, srch, bezPtArray(+400, off -0.5-clip), bdivx(computed +368/+256), bdivy, completionType(player+1144), clear=1]
- operateMesh(11):    [srcObj, srcx=0, srcy=0, srcw, srch, meshPtArray(+344, off -0.5,-0.5), divx(+272), divy(+276), blendMode=2, opacity(v24), clear=0]
- operateBezierPatch(11):[srcObj, srcx=0, srcy=0, srcw, srch, bezPtArray(+400, off -0.5,-0.5), bdivx, bdivy, blendMode=2, opacity(v24), clear=0]

MIGRATION DONE 2026-06-03 (APPROVED). Registered 4 TJS methods on tTJSNC_Layer
(LayerIntf.cpp, after operateStretch): meshCopy/bezierPatchCopy(argc>=8)/operateMesh/
operateBezierPatch(argc>=8). Added TVPDecodeLayerMeshPointArray (LayerIntf.cpp, GetCount+
PropGetByNum, mirrors sub_6A0CF0). Added dispatch helpers in PlayerRenderInternal.{h,cpp}:
buildMeshPointTJSArrayLike_0x6C715C, callLayerAffineCopyLike/MeshCopyLike/BezierPatchCopyLike/
OperateMeshLike/OperateBezierPatchLike/OperateRectLike_0x6C7440. Copy helpers use
instance/self；operate helpers and operateRect use Layer-class/target-objthis. Converted
PlayerRenderExecute.cpp sites: `emitLeafLayerCopyLike_0x6C4E28` performs the
leaf affineCopy/meshCopy/bezierPatchCopy from source.object; execute performs direct
operateMesh/operateBezierPatch, the accurate-SLA checkpoint and two operateRect sites
(top-level submit + composed-child compose). TJS Array
built refcount 1, wrapped in variant (AddRef->2), Release after FuncCall (->1, variant holds),
freed on variant dtor. Current `setClip` is also class/target-objthis；target tail has no
`Update()`。Leaf neutralColor/Real setSize、compose argc5 fillRect、buffer argc4 fillRect
and descriptor/color owner scopes have since been converted. The June validation snapshot
below is historical and does not prove later worktree changes; use the current clusterJ report.

Local: native impls EXIST (tTJSNI_BaseLayer::MeshCopy/OperateMesh/BezierPatchCopy/OperateBezierPatch
take tTVPPointD* points, divx, divy, src, srcrect, [mode,opa,] type, clear). Only
operateAffine/affineCopy/operateRect/fillRect/setSize/setClip were registered as TJS methods in
LayerIntf.cpp. Mesh/bezier 4 methods registered 2026-06 to close the dispatch surface.

Real tTJSNC_Layer.meshCopy callback = sub_6A1D70, operateMesh = sub_6A1F24,
bezierPatchCopy = sub_6A299C, operateBezierPatch = sub_6A2B50 (registered by sub_6A3E24 onto
Layer class). Internal native worker sub_6A0CF0 reads the point array via Motion_propGetCount +
Motion_propGetIndexDouble (len/2 points). Authoritative arg order = 0x6C7440 packed array, NOT
the internal callback positional decode.
