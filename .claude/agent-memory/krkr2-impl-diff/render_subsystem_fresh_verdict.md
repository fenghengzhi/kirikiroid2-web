---
name: render-subsystem-fresh-verdict
description: Render/anchor/draw-dispatch audit, with 2026-07-23 SourceCache corrections for descriptor topology, std::list identity, and exact software/GPU tint branches
metadata:
  type: project
---

2026-06-07 READ-ONLY 全 fresh-decompile 复核 motionplayer 渲染/anchor/draw 子系统
(0x6C7440 draw / 0x6C715C vertex builder / 0x6C0528 anchor type10 / 0x6C1B70 color
resolver / 0x6A7518 bake)。本次 supersede 06-05 的 "setClip 半迁移 D-1" 裁决(已修复)。

**地址↔文件映射(本次确认仍有效):**
- draw 主体 sub_6C7440 = renderToCanvasLike_0x6C7440 (PlayerRenderTargets.cpp:1087 入口 + PlayerRenderExecute.cpp executeLayerRenderCommands 主体 + PlayerRenderInternal.cpp:48-325 的 12 原语 helper)
- vertex builder sub_6C715C = buildMeshPointTJSArrayLike_0x6C715C @PlayerRenderInternal.cpp:207
- anchor type10 sub_6C0528 = Player::updateLayersPhase3_AnchorNode @PlayerUpdateAnchor.cpp:7
- color resolver sub_6C1B70 = Player persistent descriptor/color Dictionaries →
  ResourceManager dispatch `loadSource(source, descriptor)` → inherited SourceCache
- bake sub_6A7518 = applyPackedCornerTintLike_0x6A7518 in SourceCache.cpp
- draw 入口路由 drawCompat @PlayerDrawDispatch.cpp:117 = sub_6D5FB8 (D3DAdaptor/SLA/ordinary 三路由 ✅)

**六维裁决 (2026-06-07):**
1. TJS-dispatch ✅: 12 draw 原语全经 `(*(vtbl+16))(L"...")` = iTJSDispatch2::FuncCall + UTF-16LE 键。本地 callLayer*Like_0x6C7440 全 FuncCall(0,TJS_W(...)). argc 逐项: operateAffine 15 / affineCopy 14 / operateRect 9 / operateMesh,operateBezierPatch 11 / meshCopy,bezierPatchCopy 10 / setClip 4(set)或0(reset). 全对齐。
2. **setClip 已全迁移 ✅ (06-05 D-1 半迁移 RESOLVED)**: callLayerSetClipLike_0x6C7440(PlayerRenderInternal.cpp:171)+callLayerResetClipLike(:193) 都用 renderLayerObject->FuncCall(0,TJS_W("setClip"),...). 调用点 PlayerRenderExecute.cpp:774/779/1291. binary 0x6c78dc(argc4)/0x6c7620(argc0)/0x6c8fcc(post-walk reset) 全对齐. PlayerRenderExecute.cpp:782 GetClip() 仅读回结果(非派发,可接受). PlayerRenderInternal.cpp:609 SetClip 是另一非 render-path 用途.
3. **2026-07-23 correction:** Player+676 is a persistent descriptor Dictionary,
   Player+716 its persistent numeric color Dictionary, and Player+656 the RM
   dispatch owner—not three work Layers. `0x6C1B70` writes descriptor/color and
   calls `RM.loadSource(source,descriptor)`. `sub_6A7518` returns for all neutral
   or bitwise-white colors. Its software branch does the BGRA bilinear multiply;
   RGB uses divisor 128/255 while alpha always uses 255, with literal
   width-1/height-1 divisors. Its GPU branch only queries the PrivateMotionGLL
   native instance and discards the result; the old “GPU bitmap bake / non-GPU
   GL operation” direction was inverted. Local now follows these exact branches.
4. anchor color base 255:128 ✅ byte-verified: get_bytes(0x14D7C50,16)= idx0 0x406FE0...=255.0, idx1 0x406000...=128.0. `qword_14D7C50[(blend&0xF0)==16]` TRUE→idx1→128.0. 本地 PlayerUpdateAnchor.cpp:150-152 `isDefaultBlend?128.0:255.0` 方向一致 ✅.
5. anchor blend 源 per-slot ✅: binary 0x6c0a80/0x6c0aac 读 `*(node+536*v19+364)`, v19=*(node+1392)=activeSlotIndex. node+536*idx+364 = slot0(node+320)+44=364(idx0) / slot1(node+856)+44=900(idx1) = ClipSlot::blendMode(+44). 本地 an.activeSlot().blendMode (MotionNode.h:236 slots[activeSlotIndex]) ✅. (注: 偏移是 +364 from node 即 slot+44, 早期注释 PlayerUpdateAnchor.cpp:145-146 写 "+44" 指 slot 相对偏移, 正确).
6. anchor w/h+gate+damp ✅: gate 0x6c06e8 = a1[74](Player+592 dt)==0 || !player+612; w/h 从 player+696 内部 render Layer PropGet(flag1024) ; dampPow=dt*(v27*dt/v27)/v27/60/node+2432, v27=(player+592)/(player+1168). 本地 PlayerUpdateAnchor.cpp:16-69 全对齐(冗余 (v27*dt/v27) FP 保留 ✅).

**Open 偏差 (按严重度, 均 oracle-inert 或平台边界):**
- ~~C-2 SourceCache cache topology~~ **CLOSED 2026-07-23**: both sides use
  `std::list<Entry>`, strict full-Variant `(key,src,blendMode)` identity, mutable
  colors, same-Layer rebake and `push_front(copy)+erase(old)` lifetime.
- MAJOR-DATAFLOW(架构,非平台边界但语义等价): binary 单 flat loop + per-player scratch(+716 color array / +656 bufLayer / +736 bake target) vs 本地 vector<PreparedRenderItem>+map+lambda recursion(buildItemOutput). TJS primitive dispatch 本身忠实. 这是 build-path 重组, 非 draw 原语偏离.
- D-2/D-3(P3, oracle-inert): debug overlay 4 原语缺失(drawLine/drawMeshFrame/drawBezierPatchFrame/drawBezierPatchMeshFrame, binary a1+1048||1068 门控, logo 不走). 本地 grep 0 hits 确认缺失. alpha-mask 非GPU边缘 FuncCall("fillRect"+"update") vs 本地 FillMask 直写(CPU 核已对齐).
- PropGet flag(P3,inert): anchor w/h binary flag1024 vs 本地 PlayerUpdateAnchor.cpp:44/48 flag0. width/height 在 Layer 上两边都成功, inert.

**平台边界(明确技术原因, 非缺口):**
- No SourceCache color platform boundary is claimed: local follows the original
  software bake and GPU native-query-only split directly.
- alpha-mask GPU shader 分支(0x6AF104 5种 GLProgram) → 本地无自定义 fragment shader 提交能力, CPU 像素核对齐非GPU分支.

**结论: 该子系统 draw 原语 dispatch / anchor 物理 / color bake 边界全部 1:1 忠实或正当平台边界。
06-05 的 setClip 半迁移已修复，SourceCache C-2 也由 fresh evidence 闭合。
剩余 open 是 build-path vector/map/lambda 重组和 debug overlay 等本文列出的独立项；
不能把这份局部审计外推成整个插件 100% 证明。**
