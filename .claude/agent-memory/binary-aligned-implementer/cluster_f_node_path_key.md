---
name: cluster-f-node-path-key
description: CORRECTED 2026-06-02 — Player+24 node-index map is RAW-LABEL keyed, NOT path. buildNodePathKey @0x6B5C1C feeds ONLY HM3 (Player+1184). Earlier "path-keyed Player+24" claim (commit 98ac6e0) was wrong-direction; reverted in M5-1.
metadata:
  type: project
---

Cluster F / audit M5. **CORRECTION (2026-06-02, byte-verified by two independent
agents)**: the earlier conclusion below (commit 98ac6e0, 2026-05-30) that the
Player+24 node-index map is PATH-keyed was WRONG. Player+24 is keyed by the RAW
PSB "label". The hierarchical path is HM3's key space ONLY.

## DECISIVE EVIDENCE (why 98ac6e0 was wrong-direction)

**Player+24 insert (buildNodeTree_recursive @0x6B4A6C, disasm 0x6B4CA8..0x6B4CE4):**
```
6b4ca8  BL Motion_propGetByName     ; v30 = PropGet("label") raw ttstr
6b4cac  LDR X0, [SP,#var_140]       ; X0 = a1+0x18 = Player+24
6b4cb0  ADD X1, SP, #var_F8         ; X1 = &v30  ← insert KEY = RAW label
6b4cb4  BL Player_nodePathMap_lowerBoundInsert
6b4ce4  STR W26, [X0]               ; *slot = node deque-index
```
There is NO `BL Player_buildNodePathKey` before the insert. Key = raw label.

**xrefs_to(0x6B5C1C) = exactly 2 callers, BOTH HM3 — never Player+24:**
- 0x6B2E08 in Player_resetMotionState_clearAndRebuild → HM3_upsert (Player+1184)
- 0x6B84C4 in Player_pruneHM3_byNodeIdentity → HM3 find
Also `callees(0x6B4A6C)` does NOT include 0x6B5C1C. So buildNodeTree_recursive
never calls the path builder → Player+24 cannot be path-keyed.

**Empty-label gate:** NONE. PropGet→insert is a straight sequence with no
non-empty branch; nodes with missing/empty "label" still insert key "" (LWW).
The original port's `if(!node.layerName.empty())` guard was a divergence and was
removed when reverting to raw.

## CORRECT key-space map
| container | binary key | port |
|---|---|---|
| Player+24 node-index map | RAW PSB "label" (write @0x6B4CB0 + all reads @0x6F2228) | _nodeLabelMap, NodeTree.cpp:118 = `_nodeLabelMap[node.layerName]=index` |
| HM3 Player+1184 | PATH `/top/.../leaf` (buildNodePathKey) | _perNodeLayerStateMap, populated by resetMotionState loop3 (brick 6); key via buildNodePathKeyLike_0x6B5C1C |

**Reads (all RAW label, verbatim, were always correct):** getLayerMotion/
getLayerGetter sub_6B5AD8 @0x6B5B14; stencil mask resolve @0x6B5454; dtgt
resolves (angleMode=4 @0x6BE7B4, particle trigger=4 @0x6BF048) →
findNodeByLabel(_nodeLabelMap, dtgt); requireLayerId; hitTestLayer.

**buildNodePathKey @0x6B5C1C generator** (still accurate): usercall X0=player,
W1=nodeIndex, X8=ttstr_out. Walks parentIndex chain (node+36) leaf→root, each
segment = "/"+label(node+0) (sub_A0CC68), ancestor PREPENDED (sub_A1359C),
`while(a2)` stops at root index 0. Port: buildNodePathKeyLike_0x6B5C1C in
RuntimeSupport.cpp/.h — used ONLY for HM3.

**getLayerNames (M5-2 DONE 2026-06-03, premise corrected):** the REAL
getLayerNames is @0x6D10E0 (NCB name "getLayerNames" @0x6D88C8; IDA had MERGED
it into sub_6D1018, which is actually processedMeshVerticesNum; sub_6B601C is
that getter's mesh-count visitor — NOT layer names). getLayerNames does an
in-order walk of the Player+24 std::map<ttstr,int> emitting each KEY (raw label)
as a string variant, ascending; NO value, NO type3/4 descent, NO sub_6B601C.
Optional args[0] = substring filter: ttstr_indexOf(key,arg)>=0 → push (contains,
case-sensitive); void/absent arg → emit all; empty-string arg → emit none
(indexOf returns -1). Port (PlayerLayerQuery.cpp): collectLayerNames(filter) +
getLayerNamesCompat raw NCB callback; std::map iteration already key-ascending.
The earlier "re-port to sub_6B601C node-deque walk" task was based on the IDA
merge artifact and is VOID — local was already correct, only the filter was
missing.

**Why:** binary is authoritative (CLAUDE.md). **How to apply:** Player+24 =
_nodeLabelMap = RAW label; HM3 = _perNodeLayerStateMap = path. Do NOT cross the
two key spaces. M5-1 (2026-06-02) reverted the write side path→raw; logo
m2logo(93)+yuzulogo(243) 0-mismatch after revert (logo has no dup labels).
See also [[hm3-init-value-from-node]].
