---
name: clusterF-nodetree-pathkey-resolved
description: NodeTree 簇 F：Player+24 node-index map 用 RAW label（非 path-key）；buildNodePathKey 仅喂 HM3；2026-05-30 报告 F1/F2/F3 已证伪
metadata:
  type: project
---

# Cluster F (NodeTree/MotionLoad) — path-key 争议终结

**事实（指令级反编译确认 2026-06-07，IDB libkrkr2.so.i64）：**
- `Player_buildNodeTree_recursive @0x6B4A6C`：node-index map(Player+24) insert @0x6b4ce4，
  key = `Motion_propGetByName(L"label")` @0x6b4ca8 = **RAW PSB label ttstr**。PropGet 与 insert
  之间 **无 BL Player_buildNodePathKey**。value = 0-based deque index。
- `Player_nodePathMap_lowerBoundInsert @0x6B50B8`：Player+24 = std::map<ttstr,int> RB-tree，cmp sub_9B1ED0（UTF-16 lex）。
- `Player_buildNodePathKey @0x6B5C1C`：xrefs_to = **仅 2 caller**：0x6b2e08(resetMotionState loop3)、
  0x6b84c4(pruneHM3)。**都喂 HM3(Player+1184)，从不喂 Player+24**。path key = "/seg/.../leaf"，仅 HM3 key space。
- HM3 populate gate（resetMotionState loop3 @0x6b2dcc/0x6b2df8）：`joinTarget(node+46)!=0` AND `nodeType<=8 && (1<<type)&0x19D`。

**证伪：** 2026-05-30 报告 `clusterF_motionload_nodetree.md` 的 F1/F2/F3（P0「Player+24 应用 path-key、
buildNodePathKey 缺失」）**全错**。本地当前代码已正确：`_nodeLabelMap[widen(label)]=index`（RAW label），
`buildNodePathKeyLike_0x6B5C1C`（RuntimeSupport.cpp:1241）存在且仅 HM3 消费（PlayerFrameProgress.cpp HM3 populate@1897 / restore@1749）。

**Why:** 与 [[project_hm3_hm4_field_misjudgments_corrected]] 的「M5 path-key 旧 memory 曾误判 Player+24 为 path-keyed」
同源；本轮给出确凿反编译终结证据。错误 path-key memory 会再次诱导把正确的 RAW-label map 错改成 path（commit 98ac6e0 已踩过此坑并被回退）。

**How to apply:** 任何后续 session 若再听到「Player+24 应该用层级 path key」或「path-builder 缺失」，
直接引此条 + 0x6b4ca8/0x6b4ce4/xrefs_to(0x6B5C1C) 反驳，不要重新打开。IDB 内 0x6b4ce4/0x6b4ce0 注释已就地纠正+idb_save。

**残留偏差（本簇，非 P0，可局部修）：** initNodeFields(0x6B3C78) 两处：
(D1) NodeTree.cpp:185-190 mesh 子键读缺 `if(meshType!=0)` 门控（二进制 0x6b4198）；
(D2) meshCombine(node+1964 @0x6b4238) 本地未读。
容器策略 C1(std::deque vs KiriKiri inline deque)/C2(PSB helper vs TJS dispatch)/C3(default init vs MotionNode_initFields) 属全模块政策，≥P1 非 boundary。
