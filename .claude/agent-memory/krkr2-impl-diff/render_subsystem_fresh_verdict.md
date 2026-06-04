---
name: render-subsystem-fresh-verdict
description: 2026-06-05 fresh-decompile 渲染子系统(draw/render items/targets/alpha-mask)全维裁决; 12原语全经FuncCall(vtbl+16)UTF-16派发✅; setClip半迁移; colorBytes经0x6A7518 CPU bake非draw路径(per-vertex边界再次证伪); alpha-mask shader→CPU平台边界
metadata:
  type: project
---

2026-06-05 对 motionplayer 渲染路径做 fresh-decompile 独立复核(0x6C7440/0x6C4E28/0x6C715C/0x6AF104/0x6A7518/0x6C6B48)。

**地址↔文件映射:**
- draw 主体 sub_6C7440 = renderToCanvasLike_0x6C7440 @PlayerRenderTargets.cpp:1087 (入口) + 执行主体 @PlayerRenderExecute.cpp:1020 + 12原语 dispatch helper @PlayerRenderInternal.cpp:48-289
- emitRenderItem_requireLayer sub_6C4E28 = build loop @PlayerRenderExecute.cpp:59-234 + SeparateLayerAdaptor.cpp resolveLayerNodeLike_0x6C6B48
- vertex builder sub_6C715C = buildMeshPointTJSArrayLike_0x6C715C @PlayerRenderInternal.cpp:171
- alpha-mask sub_6AF104 = applyMotionAlphaMaskLike_0x6AF104 @PlayerRenderInternal.cpp:822
- 4-corner color bake sub_6A7518 = applyPackedCornerTintLike_0x6A7518 @SourceCache.cpp:82 (消费点 :732)
- render target map sub_6C6B48 = NativeSLAOrderedMapLike_0x6C6B48 @SeparateLayerAdaptor.cpp

**核心裁决:**
- TJS-dispatch ✅: binary 12 draw 原语全经 `(*(...)(*_QWORD*vobj+16LL))(L"...")` 即 iTJSDispatch2::FuncCall(vtbl+16)+UTF-16LE 键。本地 callLayer*Like_0x6C7440 全部 FuncCall(0,TJS_W(...)) argc(15/14/11/10/9)逐项对齐。
- 12 原语: operateAffine/affineCopy/operateMesh/operateBezierPatch/meshCopy/bezierPatchCopy/operateRect/fillRect 全 ✅; setClip 🟡半迁移; drawLine/drawMeshFrame/drawBezierPatchFrame/drawBezierPatchMeshFrame ❌(debug overlay,a1+1048||1068门控,logo不走,oracle-inert)。
- colorBytes(node+100) 真实消费点 = sub_6A7518 CPU bilinear bake 进 source bitmap(非 draw 路径; 0x6C7440/0x6C4E28 全文无 node+96..112 读取)。divisor 128(GPU half-alpha)/255 本地对齐。**per-vertex color 平台边界论据再次证伪**(color 不走 GPU 顶点色,而是 draw 前 CPU bake)。
- sub_6C715C 仅 append 顶点位置(x+off,y+off),无颜色 ✅。

**Open 项(均 oracle-inert,非平台边界,应实装):**
- D-1(P2) setClip 半迁移: binary FuncCall(primaryLayer/v370,L"setClip",4或0 args); 本地 renderLayer->SetClip()/ResetClip() 原生 C++(PlayerRenderExecute.cpp:778/782/1293)。同一 TJS Layer 对象上 dispatch 与原生混用。renderLayer 是真实 TJS Layer 可派发,非平台边界。
- D-2(P3) alpha-mask 非GPU边缘提交: binary 非GPU分支边缘走 FuncCall("fillRect")+FuncCall("update"); 本地 FillMask 直写。CPU 像素核已对齐。
- D-3(P3) debug overlay 4 原语缺失(drawLine 等)。
- D-4(P3) mesh points 多一层 std::vector<float> 中转再转 TJS Array。

**平台边界(明确技术原因):** alpha-mask GPU shader 分支(0x6AF104, 5种 GLProgram: AddAlphaMask/AlphaMask/AlphaMaskRev/AlphaMaskThreshold{,Fill,Crop})——本地无 GLProgramObject 自定义 fragment shader 提交能力(binary 经 sub_84B160 编 GLSL+vtbl+160 draw)。CPU 像素核语义对齐非GPU分支,可接受。

render target sub_6C6B48 ✅: red-black tree map(ordinal)+active/retired 双链 swap+sub_6DCB2C(恒返1)+CreateNew"Layer"+PropSet absolute/hitThreshold=256。本地 try_emplace/find/erase/swapWith 对齐。
