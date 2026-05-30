---
name: clusterF-node-pathkey
description: CLUSTER F node-tree audit — binary keys nodes by hierarchical PATH ttstr, not flat label; requireLayerId timing review claim refuted
metadata:
  type: project
---

CLUSTER F (Player motion load + node tree), audited 2026-05-30 against libkrkr2.so.

**Node lookup is keyed by HIERARCHICAL PATH, not flat label (P0).**
`Player_buildNodePathKey` @0x6B5C1C walks parentIndex chain concatenating node
names with "/" (TJS ttstr concat) -> `parent/child/...`. That path is the key for:
- build-time node-index map `map(player+24)` (insert @0x6B4CE4, sub_6B50B8 renamed
  `Player_nodePathMap_lowerBoundInsert`; find = sub_6F2228 = `Player_nodePathMap_find`)
- post-pass stencil-mask resolve
- per-node layer-state map `HM3(player+1184)` in resetMotionState loop3.
Port uses flat `_nodeLabelMap : std::map<std::string,int>` keyed by PSB "label" ->
collides on duplicate labels under different parents. `Player_buildNodePathKey` has
NO local counterpart (MISSING). `_perNodeLayerStateMap` (Player.h:860) declared but
never populated.

**Refuted review claim:** MotionPlayer_Restoration_Review_2026-05-30.md said
NodeTree.cpp:103-104 requireLayerId is "early per-node tree-build vs binary lazy
render-build". WRONG for node+16/node+20: `Player_buildNodeTree_recursive` @0x6B4A6C
calls requireLayerId 2x PER NODE in tree build (@0x6B4D24/@0x6B4DBC), exactly like
NodeTree.cpp:102-105 -> ALIGNED. The lazy render-build requireLayerId (sub_6C4E28
LABEL_28) is a THIRD, separate layer-id for the render item (node+424). Review
conflated two distinct call sites.

**Container model:** node container is KiriKiri inline deque (80B header
player+200..272 = a1[25..34], 2632B stride, 0xA48-byte blocks). pushBlock=0x6F1914,
destroyAll=0x6CF9B4, destroy=0x6F436C, node init=MotionNode_initFields 0x6F19B4,
node teardown=MotionNode_destroy_guess 0x6F4C8C. Port substitutes std::deque<MotionNode>.

**TJS-dispatch PSB access:** Player_initNodeFields 0x6B3C78 reads every field via
iTJSDispatch2 PropGet/FuncCall on PSB-as-dispatch (vtbl+32/+40); port uses PSBDictionary
C++ helpers. Field offsets correct, access arch not.

Ledger: analysis/audit_motionplayer_2026-05-30/clusterF_motionload_nodetree.md
