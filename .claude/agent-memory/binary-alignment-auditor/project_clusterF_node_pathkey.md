---
name: clusterF-node-pathkey
description: CLUSTER F node-tree audit — Player+24 node-index map keyed by RAW PSB label; buildNodePathKey (path) feeds HM3 ONLY; requireLayerId timing review claim refuted
metadata:
  type: project
---

CLUSTER F (Player motion load + node tree), audited 2026-05-30; KEY-SPACE section
CORRECTED 2026-06-02 (independent re-derivation).

**Player+24 node-index map is keyed by RAW PSB "label", NOT hierarchical path (CORRECTED).**
Disasm of buildNodeTree_recursive @0x6B4A6C insert point (0x6b4ca8-0x6b4ce4):
`Motion_propGetByName(&v30, node, L"label")` -> v30 = raw "label" ttstr (void/empty if
absent); then `*(DWORD*)Player_nodePathMap_lowerBoundInsert(a1+3 /*Player+24*/, &v30) = v17`
(deque index). NO buildNodePathKey between PropGet and insert; straight-line, so NO
empty-label gate (every node inserted, empty-label nodes keyed by empty ttstr).
- callees(0x6B4A6C) = empty for 0x6B5C1C; xrefs_to(0x6B5C1C) = ONLY 0x6b2e08 + 0x6b84c4.
- Read sites use SAME raw key: Player_findNodeByRawLabel @0x6B5B14 does
  `Player_nodePathMap_find(a1+3, a2)` with caller's raw key (no path build); stencil/
  connectTarget post-pass @0x6B5454 does `sub_6F2228(a1+3, v43)` with raw array-element
  string. write=raw / read=raw, same space.
- Helper NAMES (`Player_nodePathMap_lowerBoundInsert`/`_find`/sub_6F2228) are misleading
  IDB labels — they are generic std::map ops on a1+3; key fed in is raw label.
- IDA inline comment at 0x6b4ce4 ("key=buildNodePathKey full path") is WRONG and
  contradicts the adjacent 0x6b4ca8 comment + the disasm — do not trust it.

**buildNodePathKey @0x6B5C1C builds path "/seg/.../leaf" (parent walk via *(node+36)),
consumed by HM3 (Player+1184) ONLY:** 0x6b2e08 -> HM3_upsert_perNodeLayerState (key=path),
0x6b84c4 -> pruneHM3 identity. NEVER reaches Player+24.

PRIOR CLAIM (now refuted): old version of this memory said Player+24 is keyed by path
and port's flat `_nodeLabelMap` collides on dup labels. WRONG — binary itself keys
Player+24 on raw label, so dup labels under different parents collide IN THE BINARY TOO;
that is binary-faithful, not a port bug. The actual port BUG is the REVERSE: port
NodeTree.cpp:118 was RE-KEYED flat-label -> path (buildNodePathKeyLike_0x6B5C1C) on the
WRITE side while readers use raw -> write/read mismatch, all lookups miss. FIX: revert
NodeTree.cpp:118 to raw label + drop the non-empty gate. `_perNodeLayerStateMap`
(Player.h:860, the HM3 analog) declared but never populated — that one IS the path-keyed map.

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
