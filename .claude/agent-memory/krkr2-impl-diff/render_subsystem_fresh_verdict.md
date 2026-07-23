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
1. TJS-dispatch ✅: draw 原语均经 `iTJSDispatch2::FuncCall` + UTF-16LE 键，但 receiver 不是统一 instance。Target width/height、setClip、direct operate*、operateRect 走 Layer class receiver + target objthis；leaf/composed/buf copy/setup 走 instance/self。argc 逐项: operateAffine 15 / affineCopy 14 / operateRect 9 / operateMesh,operateBezierPatch 11 / meshCopy,bezierPatchCopy 10 / setClip 4(set)或0(reset)。
2. **setClip 已全迁移 ✅，且 2026-07-23 receiver 纠正**: callLayerSetClipLike/callLayerResetClipLike 通过 Layer class accessor dispatch，target 仅作 objthis；不存在旧文所称 native `GetClip()` 回读。
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
- 2026-07-23：旧 `buildItemOutput` recursion 与普通路径 `_renderLayerStates`
  代理已删除。普通 0x6C4E28→0x6C7440 主干、RM.bufLayer、ancestor mask、
  float/Real/FCVTZS、owner/异常边界已恢复；仍开放 acquire caller payload、
  accurate-SLA 持久树与 wrapper/executor 源码拆分。
- D-2/D-3(P3, oracle-inert): debug overlay 4 原语缺失(drawLine/drawMeshFrame/drawBezierPatchFrame/drawBezierPatchMeshFrame, binary a1+1048||1068 门控, logo 不走). 本地 grep 0 hits 确认缺失. alpha-mask 非GPU边缘 FuncCall("fillRect"+"update") vs 本地 FillMask 直写(CPU 核已对齐).
- PropGet flag(P3,inert): anchor w/h binary flag1024 vs 本地 PlayerUpdateAnchor.cpp:44/48 flag0. width/height 在 Layer 上两边都成功, inert.

**平台边界(明确技术原因, 非缺口):**
- No SourceCache color platform boundary is claimed: local follows the original
  software bake and GPU native-query-only split directly.
- alpha-mask GPU shader 分支(0x6AF104 5种 GLProgram) → 本地无自定义 fragment shader 提交能力, CPU 像素核对齐非GPU分支.

**结论: 该子系统 draw 原语 dispatch / anchor 物理 / color bake 的已审计站点已按 fresh evidence 修正。
06-05 的 setClip 半迁移已修复，SourceCache C-2 也由 fresh evidence 闭合。
剩余 open 是 acquire payload、accurate-SLA、函数拆分和 debug overlay 等独立项；
不能把这份局部审计外推成整个插件 100% 证明。**
