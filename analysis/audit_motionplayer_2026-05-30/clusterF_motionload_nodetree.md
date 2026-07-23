# CLUSTER F Audit — Player motion load + node-tree construction

> 2026-05-30. Authoritative source: libkrkr2.so (IDB libkrkr2.so.i64). Read-only
> on cpp/. IDB improved + saved (renames + comments below).
> Protocol: decompile -> <=10-line pseudocode -> local counterpart -> arch compare -> P0/P1/P2.

## IDB changes made (idb_save done)
- 0x6B4A6C -> `Player_buildNodeTree_recursive`, 0x6B51F0 -> `Player_buildNodeTree`,
  0x6B3C78 -> `Player_initNodeFields`, 0x6B56F8 -> `Player_resetAndReleaseNodes`,
  0x6B2B7C -> `Player_resetMotionState_clearAndRebuild`,
  0x6B50B8 -> `Player_nodePathMap_lowerBoundInsert`, 0x6F2228 -> `Player_nodePathMap_find`.
- Comments @0x6B4CE4 (path-map insert), @0x6B4D24/@0x6B4DBC (per-node requireLayerId).

---

## Findings table

| id | func @addr | local file:line | sev | one-line |
|----|-----------|-----------------|-----|----------|
| F1 | Player_buildNodeTree_recursive @0x6B4A6C | NodeTree.cpp:88-263 | P0 | binary node-index map is keyed by HIERARCHICAL PATH ttstr (`parent/child`) via Player_buildNodePathKey -> map(player+24); port uses flat `std::map<std::string,int>` keyed by PSB "label" -> wrong key space, wrong values |
| F2 | Player_buildNodePathKey @0x6B5C1C | (none) | P0 | MISSING: path-key builder that walks parentIndex chain concatenating node names with "/" via TJS ttstr concat; required to key BOTH node-index map (build) and PerNodeLayerState map(player+1184). No local equivalent. |
| F3 | Player_nodePathMap_lowerBoundInsert @0x6B50B8 | NodeTree.cpp:109-111 | P1 | binary is std::map<ttstr,int> lower_bound+_Rb_tree insert via sub_6F1DC8 keyed by path; port `_nodeLabelMap[label]=index` is last-write-wins by flat label |
| F4 | Player_buildNodeTree_recursive index calc @0x6B4CE0 | NodeTree.cpp:99 | P1 | binary derives node index from DEQUE pointer arithmetic (`(end-begin)/8*248037625...-1`); port uses `nodes.size()-1`. Equivalent value only because deque is append-only; arch divergence in container model |
| F5 | requireLayerId per-node @0x6B4D24/@0x6B4DBC | NodeTree.cpp:102-105 | P2 | ALIGNED: 2x `requireLayerId` TJS FuncCall per node -> node+16/node+20 in tree build. Refutes review's "lazy render-build" claim for these two node-level IDs. Port domain/timing/count match. |
| F6 | Player_buildNodeTree post-pass @0x6B5388-0x6B55A8 | NodeTree.cpp:317-340 | P1 | stencilComposite(type12,bit2) mask resolve: binary looks up via `Player_nodePathMap_find` (PATH key) + appends node ptr to node+2600 vector + sets node+1961; port resolves by flat `_nodeLabelMap[label]` + bool flag. Same intent, divergent key + storage (ptr vector vs bool). |
| F7 | Player_initNodeFields @0x6B3C78 | NodeTree.cpp:107-209 | P1 | binary reads EVERY field via TJS dispatch PropGet on PSB-as-dispatch (v47->vtbl+32, keys L"label"/L"type"/...); port reads via PSB::PSBDictionary C++ helpers (nodeTreePsbNumber/String/List). Functional-equivalent PSB access, NOT TJS-dispatch arch. Field-by-field offset mapping is otherwise correct. |
| F8 | MotionNode_initFields @0x6F19B4 | MotionNode.h (member inits) | P2 | binary zeroes a fixed offset list inside the 2632B node block + sets node+1400=&empty-ttstr-rep + sub_699390; port uses C++ default member initializers. Same end-state per field; container-model divergence (manual offset memset vs RAII). |
| F9 | Player_nodesDeque_pushBlock @0x6F1914 | NodeTree.cpp:96-98 | P1 | binary appends a 0xA48-byte node block to KiriKiri deque (operator new(0xA48), MotionNode_initFields, advance map ptrs); port `nodes.emplace_back()` on std::deque<MotionNode>. STL deque substitutes KiriKiri inline deque (known Player container policy, PLATFORM_BOUNDARY-class). |
| F10 | Player_resetAndReleaseNodes @0x6B56F8 | NodeTree.cpp:274 (ensureRootNodeLike) | P1 | binary: per-node 2x `releaseLayerId` TJS call (node+16/+20) + cond node+1904->424 release, then deque tear-down via sub_6F3E0C + map destroy, reset deque to single root. Port `ensureRootNodeLike_0x6CED30` not co-located; releaseLayerId pairing for the two node IDs needs verification vs requireLayerId. |
| F11 | Player_nodesDeque_destroyAll @0x6CF9B4 / _destroy @0x6F436C | (Player dtor / clear) | P2 | binary manually iterates deque map-blocks calling MotionNode_destroy_guess per 2632B node then operator delete each 0xA48 block. Port relies on std::deque<MotionNode> RAII. Container-model divergence. |
| F12 | MotionNode_destroy_guess @0x6F4C8C | (~MotionNode) | P2 | binary: ordered manual teardown (node+1904 sub_6F4DFC+delete, node+2600 delete, A0F778 ttstr releases, tTJSVariant_Release node+2384/+312/+0, HM3_value_destroy node+856/+320, operator delete frameList blocks node+2024/+2048/+2072). Port ~MotionNode RAII. Verify all owned handles released in same order. |
| F13 | Player_resetMotionState_clearAndRebuild @0x6B2B7C | PlayerMotionLoad.cpp (reset path) | P1 | after rebuild: loop3 gate `nodeType<=8 && ((1<<type)&0x19D)` -> Player_buildNodePathKey -> HM3_upsert_perNodeLayerState(player+1184) keyed by PATH. Depends on F2 (MISSING path-key builder). Also loop1 node+44=1/evaluateTimeline, loop2 player+1240 label->value upsert. |
| F14 | Player_initNodeTimeline_guess @0x6B64AC | PlayerMotionLoad.cpp parseFrame | P2 | binary binary-searches frameList by L"time" to find frame index v12, then Player_parseFrame/mergeFrameContent on node+320 & node+856 (two clip slots), node+44=1, cond Motion_Player_findSource gate `(1<<type)&(preview(+1092)?6153:6145)`. Verify port replicates the (1<<type)&mask preview-gated findSource. |

---

## Architectural verdict by theme

### THEME A — node-index keyed by PATH, not flat label  (P0, F1/F2/F3/F6/F13)
The single biggest divergence. In libkrkr2.so the node container is keyed for
lookup by a **hierarchical path ttstr** (`Player_buildNodePathKey` @0x6B5C1C walks
the parentIndex chain building `name0/name1/.../nameN` via TJS ttstr concat). That
path is the key for:
  - the build-time node-index map `map(player+24)` (insert @0x6B4CE4 via
    `Player_nodePathMap_lowerBoundInsert`),
  - the post-pass stencil-mask resolve (`Player_nodePathMap_find` @0x6F2228),
  - the per-node layer-state map `HM3(player+1184)` in resetMotionState.
The port instead keeps `_nodeLabelMap : std::map<std::string,int>` keyed by the
flat PSB `"label"` and resolves stencil masks by flat label. Two layers with the
same `label` under different parents collide in the port but are distinct paths in
the binary. `Player_buildNodePathKey` has NO local counterpart (F2 MISSING).
`PerNodeLayerStateMap _perNodeLayerStateMap` (Player.h:860) is declared but never
populated by a path key. This is a data-flow divergence (wrong key -> wrong/colliding
values), so P0, and requires building the path-key machinery first (cannot be
patched on the flat-label map).

### THEME B — requireLayerId timing  (RESOLVED, F5)
The review's open build_flow item claimed port materializes layerId "early/per-node
in tree-build vs binary lazy per-drawable-item in render-build". For the TWO
node-level IDs (node+16/node+20) this is WRONG: `Player_buildNodeTree_recursive`
calls `requireLayerId` twice per node, in the tree build, exactly as
NodeTree.cpp:102-105. The lazy render-build `requireLayerId` (sub_6C4E28 LABEL_28)
is a SEPARATE, THIRD layer-id used for the per-drawable render item (node+424),
which is a distinct concern from layerId1/layerId2. So NodeTree.cpp:103-104 is
correctly aligned; the review conflated two different requireLayerId call sites.

### THEME C — TJS-dispatch PSB access vs C++ PSB helpers  (P1, F7)
`Player_initNodeFields` reads every field through `iTJSDispatch2::PropGet`/`FuncCall`
on the PSB node presented as a TJS dispatch object (vtbl+32 = PropGet, vtbl+40 =
FuncCallByNum). The port reads through PSB::PSBDictionary C++ casts. Field->offset
mapping is correct; the access architecture (TJS dispatch wrappers) is not. Per
CLAUDE.md this is a non-equivalence (always >= P1).

### THEME D — KiriKiri inline deque vs std::deque  (P1/P2, F4/F9/F11/F12)
Node container is a KiriKiri inline deque (80B header player+200..272 == a1[25..34],
2632B stride, 0xA48-byte blocks) with manual block alloc/free and per-node
MotionNode_initFields / MotionNode_destroy_guess. Port uses std::deque<MotionNode>
+ RAII. Consistent with the documented Player STL-container policy (PLATFORM_BOUNDARY
-class), but per CLAUDE.md stays >= P1 until byte-level KiriKiri deque is restored.

---

## MISSING (no local counterpart)
- **Player_buildNodePathKey @0x6B5C1C** (F2) — hierarchical path-key builder. Blocks
  Theme A. `_perNodeLayerStateMap` declared but never keyed/populated.
- **Path-keyed node-index map** — port has flat-label `_nodeLabelMap` instead of the
  binary `map<ttstr-path,int>` (F1/F3).
- **HM3_upsert_perNodeLayerState(player+1184) path-keyed population** in
  resetMotionState loop3 (F13) — absent because F2 is absent.

## Subfunction alignment status
- `Player_buildNodePathKey` 0x6B5C1C — decompiled, MISSING locally.
- `Player_nodePathMap_lowerBoundInsert` 0x6B50B8 / `_find` 0x6F2228 — decompiled,
  std::map<ttstr,int> by path; port flat-label map = divergent.
- `Player_initNodeFields` 0x6B3C78 — decompiled; field offsets aligned, TJS-dispatch
  access not (F7).
- `MotionNode_initFields` 0x6F19B4 / `_destroy_guess` 0x6F4C8C — decompiled; manual
  offset init/teardown vs RAII (F8/F12).
- `Player_nodesDeque_pushBlock` 0x6F1914 / `_destroyAll` 0x6CF9B4 / `_destroy`
  0x6F436C — decompiled; KiriKiri deque vs std::deque (F9/F11).
- `Player_resetAndReleaseNodes` 0x6B56F8 — decompiled; releaseLayerId pairing (F10).
- `Player_resetMotionState_clearAndRebuild` 0x6B2B7C — decompiled; loop3 path-key
  dependency (F13).
- `Player_initNodeTimeline_guess` 0x6B64AC — decompiled; frame binary-search +
  preview(+1092)-gated findSource (F14). TJS `completionType` is +1144 and does
  not select this mask.
- Player_loadMotion 0x6B0F10 / findMotion 0x6D004C / setMotion 0x6C1B20 /
  initEmoteMotion 0x6B2E90 / initNonEmoteMotion 0x6B365C — NOT decompiled this pass
  (entry-point wrappers; recommend a follow-up cluster for the load dispatch chain).

## Platform boundaries
- None tagged `// PLATFORM_BOUNDARY:` in NodeTree.cpp. The std::deque/std::map
  substitutions (Themes A/D) are STL-policy choices documented in Player.h:762-771
  but NOT marked PLATFORM_BOUNDARY — they remain counted deviations (Theme A is a
  true P0, not a boundary).
