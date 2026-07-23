---
name: project-clusterK-render-items-targets
description: 簇K render items/targets/layer-query 历史审计；2026-07-23 已证伪 P1-I3，当前 open 为 acquire payload/accurate-SLA
metadata:
  type: project
---

# 簇 K — render items build + render targets + layer query 审计 (2026-06-07)

**2026-07-23 当前结论（取代历史裁决）:** P1-I3 已证伪；本地
`buildRenderCommands` 是 `0x6C7440` 内 `!priorDraw` 调用的 `0x6C4E28`
pre-walk，不是 `0x6C2334` build phase。当前普通路径 open 是 acquire
caller payload 专用值类型；另有 accurate-SLA 持久树与 wrapper/executor 拆分。
报告: analysis/audit_motionplayer_2026-06-07/clusterK_render_items_targets.md

## 二进制地址 ↔ 本地映射(本轮反编译确认)
- 0x6C2334 build(item alloc 0x1B0=432B) — 内部不写 +424/item+20；该负证只区分 item construction 与 render pre-walk，不能证明本地 phase 偏差。
- 0x6C4E28 emitRenderItem_requireLayer — `0x6C7440` 的 pre-walk；LABEL_28 @0x6c5234 写 item+424=layerId，@0x6c5240 闩 item+20=1。Loop A(mainList)+Loop B(composed)。
- 0x6C7440 renderToCanvas — top-level skip gate @0x6c75c8 `item+17||item+16||!item+232`; priorDraw gate @0x6c7630 `player+1096 && !item+18` (`Player+1096` 的 NCB 名为 `priorDraw`，不是 preview)
- 0x6C9CA8 accurate SLA — leaf 用 item+52(sub_6C6B48); composite 用 item+56 调 acquireLayerById @0x6cab64; 三 layer-id 独立(item+52/+56/+424)
- 0x6CBCE4 = **Player_acquireLayerById**(已 rename, was buildRenderTree_guess) — std::map<int,Layer> Rb_tree on a1+120 key node+32; L"Layer"(2 args=a1,a1+20), absolute=node160+node164, hitThreshold=256, cache node+40。从 sub_6C9CA8 用 item+56 调用证实身份(起点文档 I8 落实)
- 0x6DCD0C = **SLA_orderedMap_insert_0x6DCD0C**(已 rename) — operator new(0xD0=208B), node+32=key, _Rb_tree_insert_and_rebalance = std::map<int,node>
- 0x6D5658 SLA dispatcher — sub_6D5164 build → applyTranslateOffset → byte_1AB84F4(ogl_accurate_render/hasGPUAccel) 双路: OFF=ResolveSLATarget+RenderMotionFrame+Layer_UpdateRect; ON=sub_6C9CA8+sub_6CE938
- 0x6D5948 ResolveSLATarget — SLA+56==1 复用 SLA+40, 否则 lazy PrivateMotionGLL(g_PrivateMotionGLL_ClassID) under ownerLayer
- 0x6CE7D8 updateLayerAfterDraw — 无条件 player+612=player+613 快照; gate +613 → sub_6CE19C + assignImages on layer player+696

## 架构决策(已确认对齐)
- **render target 容器选型对齐**: 本地 SeparateLayerAdaptor.h:41 `std::map<tjs_uint32, NativeSLANodeLike_0x6DCD0C>` == 二进制 0x6DCD0C 的 std::map<int,node> Rb_tree(0xD0 节点)。layer query 同样走 std::map Rb_tree。
- updateLayerAfterDraw +612/+613 + assignImages@696 本地精确对齐(_internalRenderLayerReady 无条件快照)

## 当前残留偏差
- ⚠ `0x6C5264..0x6C532C` 构造后传给 `0x6C6B48` 的 caller-local payload
  仍是 value-initialized placeholder，尚无同构专用值类型。
- 🔧 `0x6C9CA8` accurate-SLA 持久 Layer 树仍未复原，不能用 HEADLESS
  checkpoint 冒充。
- ⚠ `0x6C7440` 本地仍拆为 wrapper + executor。0x1B0 是 ARM64 ABI
  尺寸，不是要求 wasm32 用 padding 合并成 raw POD 的证据。
