---
name: sub6C7440-6C4E28-dispatch-contract
description: 0x6C7440 + 0x6C4E28 TJS dispatch contract；2026-07-23 已纠正 receiver 并完成 mesh/bezier 注册
metadata:
  type: project
---

**2026-07-23 correction (supersedes the June migration verdict):** Player render
uses `iTJSDispatch2::FuncCall`, but receiver and objthis are not universally the
same object. Target width/height, setClip, direct operate* and operateRect use the
global Layer class as receiver and target as objthis. Leaf/composed/RM.bufLayer
setSize/fillRect/copy methods use the instance as receiver and objthis. The four
mesh/bezier TJS methods are now registered and their call sites converted.

Two functions verified:

- 0x6C7440 (sub_6C7440): the main submit/draw function. Dispatch keys + argc:
  - operateRect(9), operateBezierPatch(11), operateMesh(11), operateAffine(15)
  - affineCopy(14), meshCopy(10), bezierPatchCopy(10), fillRect(4)
  - drawMeshFrame(5), drawBezierPatchMeshFrame(5), drawBezierPatchFrame(3), drawLine(5)
  - target property/setup: Layer-class PropGet width then height, Layer-class setClip(4|0); descriptor/color/neutralColor use their owning instances
- 0x6C4E28 (Player_emitRenderItem_requireLayer): SeparateLayerAdaptor (a1+760) path; NOT the per-item emit the prompt described. It dispatches bezierPatchCopy(10)/meshCopy(10)/affineCopy(14)/fillRect(5) and PropSet key/src/blendMode + setSize(2), plus requireLayerId via vtbl+16 and Motion_doAlphaMaskOperation for child alpha masks.

Current state: `meshCopy` / `bezierPatchCopy` / `operateMesh` /
`operateBezierPatch` are registered and use TJS Array point decoding. Production
`0x6C7440` has no native target/source precondition, no per-item exception catch,
and no tail Update. Remaining structural gaps are the `0x6C6B48` caller payload,
the accurate-SLA persistent tree, and the local wrapper/executor function split;
do not revive this historical “registration prerequisite” as an open blocker.
