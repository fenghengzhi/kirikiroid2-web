---
name: m5-node-index-map-raw-label
description: Player+24 node-index map is keyed by RAW PSB "label", NOT a node-path; buildNodePathKey is HM3-only. getLayerNames substring filter semantics.
metadata:
  type: project
---

Player+24 node-index map (the std::map<ttstr,int> getLayerNames / getLayerMotion / getLayerGetter / hitTestLayer / stencil-composite-mask resolve all share) is keyed by the **RAW PSB "label" ttstr**, not a hierarchical node-path.

**Why / evidence (fresh decompile 2026-06-03):**
- `Player_buildNodeTree_recursive @0x6B4A6C`, insert site disasm 0x6b4ca8..0x6b4ce4:
  `6b4ca8 BL Motion_propGetByName(L"label") -> v30`; `6b4cac LDR X0,[var_140]=Player+24`;
  `6b4cb0 ADD X1,&v30`; `6b4cb4 BL lowerBoundInsert(Player+24, &v30)`; `6b4ce4 STR W26,[X0]` (= node deque index).
  There is **NO** call to a path-builder between PropGet and the insert. Key = raw label verbatim.
- `Player_buildNodePathKey @0x6B5C1C` (builds "/seg/.../leaf") has exactly 2 callers
  (`xrefs_to` = 0x6b2e08 resetMotionState, 0x6b84c4 pruneHM3) — **both HM3 (Player+1184)**. Never feeds Player+24.
- Lookup `Player_nodePathMap_find @0x6F2228` callers (xrefs) = buildNodeTree stencil-mask resolve, `Player_findNodeByRawLabel @0x6B5AD8` (IDA's own name), updateLayers passes — all pass raw caller-supplied label. Read keyspace == write keyspace.

**Misleading IDA annotations to ignore:** the function names `Player_nodePathMap_lowerBoundInsert` / `Player_nodePathMap_find` and inline comments saying "key=buildNodePathKey full path" are STALE WRONG (likely left by the earlier wrong-direction re-key, commit 98ac6e0). The disasm contradicts them. Verdict rests on instructions, not names.

**Verdict on commits:** 3ecd554 (revert Player+24 node index back to RAW label key) is CORRECT and faithful. The 06-02 review's claim "buildNodePathKey completely MISSING -> nodes flat-label indexed -> name collision" was wrong framing: buildNodePathKey is NOT missing, it exists at 0x6B5C1C but is HM3-only by design; the binary itself uses raw label for the node-index map. CLAUDE.md's recorded "M5 path-key wrongly judged Player+24 as path-keyed" lesson is consistent with this.

**getLayerNames @0x6D10E0 (NCB "getLayerNames" @0x6D88C8) substring filter:**
- void/absent args[0] (`*a2==0`, disasm 6d1134 CBZ) -> emit ALL keys (in-order RB-tree walk, leftmost @+48 then _Rb_tree_increment sub_1485230).
- present args[0] -> emit key iff `ttstr_indexOf(keyLabel, filter, 0) >= 0` (TBNZ #0x1F gates out -1). = key CONTAINS filter, case-SENSITIVE (ttstr_indexOf @0xA0CC00 -> sub_9B1FF8 plain UTF-16 substring search, no case fold).
- present-but-EMPTY filter: ttstr_indexOf returns -1 for every key (`if(*a2 && ...)` fails) -> emit NOTHING. Distinct from void (emit all).
- Port collectLayerNames (PlayerLayerQuery.cpp:174 `needle.empty() || label.find(needle)==npos -> continue`) matches exactly. 73cc3ac CORRECT.
