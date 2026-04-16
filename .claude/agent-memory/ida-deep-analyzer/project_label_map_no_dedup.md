---
name: Layer labelMap semantics (no load-time dedup)
description: libkrkr2.so has NO loadMotionSnapshot-style hashmap. Player+24 is std::map<ttstr,int32_t> built during node tree construction; duplicate labels all get independent nodes (deque push_back unconditional), only labelMap entry is overwritten (operator[] semantics). 8 same-labeled layers → 8 nodes.
type: project
---

libkrkr2.so layer label handling — key findings for aligning web port:

### Is there a `layersByName`-style structure?
YES, but different from web port:
- `Player+24` (a1+3 in qwords) is a `std::map<ttstr, int32_t>` (red-black tree)
- Stores `label → nodeIndex` (int, not dict)
- Built during `Player_buildNodeTree_recursive` (0x6B4A6C), NOT during PSB load
- Insert-or-overwrite via std::map::operator[] at `sub_6B50B8` (0x6B50B8)
- Lookup via std::map::find at `sub_6F2228` (0x6F2228), used by deflector mask resolve, camera/anchor/joinTarget

### PSB load path does NOT preprocess layers:
- `Player_loadMotion` (0x6B0F10): just calls TJS onFindMotion/findMotion, stores dicts as-is
- `Player_initNonEmoteMotion` (0x6B365C): caches priority[0]["content"] to Player+616 as-is, no per-layer iteration

### Duplicate label handling in buildNodeTree_recursive:
- Every element of layer[] → UNCONDITIONAL deque push_back (2632-byte node, at 0x6B4BA4/0x6B4BBC)
- Then `labelMap[label] = nodeIndex` via operator[] — overwrites if key exists
- NO "if already exists skip" branch
- Result: N duplicates → N nodes in deque, only LAST nodeIndex in labelMap

### yuzulogo.mtn 8x "slide" labels:
- Binary: 8 independent nodes, all render/animate; labelMap points to the 8th
- Web port (RuntimeSupport.cpp:869-874): keeps only 1st, drops other 7 — ARCHITECTURAL MISALIGNMENT

### labelMap usage (6 call sites of sub_6F2228):
- Player_buildNodeTree (0x6B5454) — deflector stencilCompositeMaskLayerList resolve
- Player_processCameraNode (0x6BDB00)
- sub_6B5AD8 (0x6B5B14) — getNodeByLabel, returns node ptr from deque by label
- sub_6BC000, sub_6BE0C0, sub_6BEDD0 — per-frame property lookups by name

### To align web port:
1. Remove label-based dedup from PSB load path (keep all layer dicts)
2. Build label→index map during node tree construction instead
3. buildNodeTree must iterate ALL N original layer[] entries, not the unique subset

**Why:** yuzulogo-style motions with intentional duplicate labels (8 slides) lose 7/8 nodes in web port; each duplicate has its own frameList/children/coord config that gets dropped.
**How to apply:** When porting Motion loading, do NOT use `unordered_map<label, dict>` as the primary layer container; use `vector<dict>` preserving order and cardinality. Build the name→index map during tree walk with operator[]=overwrite semantics.
