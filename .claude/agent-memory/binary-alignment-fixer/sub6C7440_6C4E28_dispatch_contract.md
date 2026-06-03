---
name: sub6C7440-6C4E28-dispatch-contract
description: Verified iTJSDispatch2::FuncCall draw-primitive dispatch contract in 0x6C7440 + 0x6C4E28; local Layer only registers 4 of the needed TJS methods (migration is LARGE not low-risk)
metadata:
  type: project
---

Player render path routes ALL draw primitives through iTJSDispatch2::FuncCall = `(*(vtbl+16))(layer, 0, L"<key>", &typehint, 0, argc, &args, objthis)`. Two functions verified fresh (2026-06-03):

- 0x6C7440 (sub_6C7440): the main submit/draw function. Dispatch keys + argc:
  - operateRect(9), operateBezierPatch(11), operateMesh(11), operateAffine(15)
  - affineCopy(14), meshCopy(10), bezierPatchCopy(10), fillRect(4)
  - drawMeshFrame(5), drawBezierPatchMeshFrame(5), drawBezierPatchFrame(3), drawLine(5)
  - property preamble via FuncCall: setSize(2), setClip(4 then 0), plus PropSet of width/height/blendMode/key/src/neutralColor
- 0x6C4E28 (Player_emitRenderItem_requireLayer): SeparateLayerAdaptor (a1+760) path; NOT the per-item emit the prompt described. It dispatches bezierPatchCopy(10)/meshCopy(10)/affineCopy(14)/fillRect(5) and PropSet key/src/blendMode + setSize(2), plus requireLayerId via vtbl+16 and Motion_doAlphaMaskOperation for child alpha masks.

Local feasibility (cpp/core/visual/LayerIntf.cpp): Layer registers as TJS native methods only: affineCopy, operateAffine, operateRect, fillRect (+ affinePile/affineBlend/stretch*). It does NOT register meshCopy / bezierPatchCopy / operateMesh / operateBezierPatch / drawMeshFrame / drawBezierPatchFrame / drawBezierPatchMeshFrame / drawLine. The native C++ impls (MeshCopy/BezierPatchCopy/OperateMesh/OperateBezierPatch) EXIST but are not exposed as dispatch entries. (`TJS_W("meshCopy")` at LayerIntf.cpp:4939 and `TJS_W("operateMesh")` at 5089/5094 are exception-message args inside the native bodies, not registrations.)

Local PlayerRenderExecute.cpp uses DIRECT native calls (renderLayer->AffineCopy/MeshCopy/BezierPatchCopy) for the leaf/composed/mesh/bezier draws; only the direct-affine path goes through callLayerOperateAffineLike_0x6C7440 (PlayerRenderInternal.cpp:47), which builds a 15-arg array and calls Layer-class-object FuncCall(L"operateAffine").

CONCLUSION: Full faithful migration is LARGE/RISKY, not low-risk. Prerequisite = register the 8 missing draw methods on tTJSNC_Layer first (each needs faithful arg-decode + face/drawable checks + exception semantics), THEN convert the 4 execute call sites + add per-primitive arg packing. Hot path feeds the only oracle (GREEN logo differential) with no local browser harness. Partial migration (4 registered prims only) would leave tree half-migrated = forbidden. Reported for approval rather than patched.
