---
name: project-clusterK-render-items-targets
description: 簇K render items/targets/layer-query 审计(2026-06-07): acquireLayerById 0x6CBCE4 确证, requireLayerId phase P1-I3, SLA std::map 容器对齐, accurate composite 子路径缺口
metadata:
  type: project
---

# 簇 K — render items build + render targets + layer query 审计 (2026-06-07)

**结论: ⚠️ 部分偏差** — 容器/逻辑/字段映射均对齐, 唯一未消除 = standing P1-I3(跨簇)。
报告: analysis/audit_motionplayer_2026-06-07/clusterK_render_items_targets.md

## 二进制地址 ↔ 本地映射(本轮反编译确认)
- 0x6C2334 build(item alloc 0x1B0=432B) — grep 负证: 内部 `+424`/`requireLayer`/`sub_6C4E28` 全 0 hits → item+424/item+20 **不在 build 写**(证实 P1-I3)
- 0x6C4E28 emitRenderItem_requireLayer — EXECUTE 阶段; LABEL_28 @0x6c5234 写 item+424=layerId, @0x6c5240 闩 item+20=1。Loop A(mainList)+Loop B(composed)
- 0x6C7440 renderToCanvas — top-level skip gate @0x6c75c8 `item+17||item+16||!item+232`; preview gate @0x6c7630 `player+1096 && !item+18`
- 0x6C9CA8 accurate SLA — leaf 用 item+52(sub_6C6B48); composite 用 item+56 调 acquireLayerById @0x6cab64; 三 layer-id 独立(item+52/+56/+424)
- 0x6CBCE4 = **Player_acquireLayerById**(已 rename, was buildRenderTree_guess) — std::map<int,Layer> Rb_tree on a1+120 key node+32; L"Layer"(2 args=a1,a1+20), absolute=node160+node164, hitThreshold=256, cache node+40。从 sub_6C9CA8 用 item+56 调用证实身份(起点文档 I8 落实)
- 0x6DCD0C = **SLA_orderedMap_insert_0x6DCD0C**(已 rename) — operator new(0xD0=208B), node+32=key, _Rb_tree_insert_and_rebalance = std::map<int,node>
- 0x6D5658 SLA dispatcher — sub_6D5164 build → applyTranslateOffset → byte_1AB84F4(ogl_accurate_render/hasGPUAccel) 双路: OFF=ResolveSLATarget+RenderMotionFrame+Layer_UpdateRect; ON=sub_6C9CA8+sub_6CE938
- 0x6D5948 ResolveSLATarget — SLA+56==1 复用 SLA+40, 否则 lazy PrivateMotionGLL(g_PrivateMotionGLL_ClassID) under ownerLayer
- 0x6CE7D8 updateLayerAfterDraw — 无条件 player+612=player+613 快照; gate +613 → sub_6CE19C + assignImages on layer player+696

## 架构决策(已确认对齐)
- **render target 容器选型对齐**: 本地 SeparateLayerAdaptor.h:41 `std::map<tjs_uint32, NativeSLANodeLike_0x6DCD0C>` == 二进制 0x6DCD0C 的 std::map<int,node> Rb_tree(0xD0 节点)。layer query 同样走 std::map Rb_tree。
- updateLayerAfterDraw +612/+613 + assignImages@696 本地精确对齐(_internalRenderLayerReady 无条件快照)

## 残留偏差(下结论前已交叉核实)
- 🔧 **P1-I3(standing 跨簇, 与 cluster I 同一条)**: requireLayerId(item+424)+item+20 闩二进制只在 execute(0x6C4E28 LABEL_28), 本地移到 build loop(PlayerRenderExecute.cpp:83+, commit d51cce9)。值 0-diff 但 phase 不 1:1。架构性, 需收单块 POD + 还回 execute, 高回归, CI 下做。**不连续两轮恶化, 不触发停滞警告。**
- ⚠ **accurate composite 子路径缺口**: sub_6C9CA8 LABEL_85 后对 visible-gated composite(item+264==0||blend6)走 acquireLayerById+assignImages+child-alphamask, 按 item+280 用 drawMeshFrame(0x6cb0d8)/drawBezierPatchMeshFrame(0x6cb200)/drawBezierPatchFrame(0x6cb298)/drawLine(0x6cafe8)。本地 renderAccurateSlaLike(PlayerRenderTargets.cpp:893-915)只实现 leaf affineCopy/BezierPatchCopy/MeshCopy, composite 分支缺。logo fixture oracle-inert, 标 MISSING。
- ⚠ I4(container-class): item 单块 raw 432B vs 本地 NativeRenderItemFields+PreparedRenderItem 两 STL struct。trace 0-diff, 保留。
