---
name: player-plus408-param-ramp-multimap
description: Player+408 is std::multimap<ttstr id, MotionParameterEntry*> (NOT node+408, NOT unpopulated) — controller ramp table; full chain decompiled + Stage1 ported
metadata:
  type: project
---

The brief's "node+408 controller RB-tree" is a MISLABEL — it is **Player+408**, not a
per-node field. Fresh-decompiled the whole chain 2026-06-06.

**Container contract (Player+408):** `std::multimap<ttstr id, MotionParameterEntry*>`.
- ctor Player_ctor@0x6CED30 (0x6cee8c): inits empty header (this+416 _M_header,
  +432/+440 left/right = &header, +448 node_count=0).
- VALUE = `MotionParameterEntry*` pointing INTO the Player+384
  `std::vector<MotionParameterEntry>` (56B entries). Map node = operator new(0x30):
  32B _Rb_tree_node_base + pair{ttstr key@+32 = entry.id (AddRef'd), entry*@+40}.
- MULTIMAP (dup keys kept): builder descends right on equal keys + inserts
  unconditionally; consumer uses equal_range. Comparator = sub_9B1ED0 = UTF-16
  lexicographic = ttstr_utf16_less.

**Chain (leaf→root):**
- ramp ELEMENT = MotionParameterEntry (the +384 vector entry, 56B): +0 id(ttstr),
  +8 discretization(byte=intcast), +16 rangeBegin, +24 rangeEnd, +32 rangeScale
  (=raw "division", NOT division/range), +40 value(out), +48 mode. Built by
  sub_6B1718@0x6B1718 (= appendParameterEntryLike) which pushes to +384 vector.
- parser sub_6B202C@0x6B202C (= parseParameterListLike): loops PSB "parameter" list,
  calls sub_6B1718 per entry, THEN calls sub_6B1ECC.
- map BUILDER = **sub_6B1ECC@0x6B1ECC** (= finalizeParameterTableLike): iterates +384
  vector (v1[48]..v1[49] step 56B), inserts each into +408 keyed by entry.id via
  sub_6F16AC@0x6F16AC (_Rb_tree insert). **纠正 2026-07-13**：outer
  `v3=v3[1]` 是从当前 Player 沿 Player+8 parent 链上行，不是 sibling chain；
  因而祖先 map 会持有子 Player 参数项指针。
- CONSUMER = Player_bindParameterValue@0x6C4668 (func start; 0x6C4978 is a mid-line).
  THREE equal_range ramp loops via sub_6F2F98@0x6F2F98 (lower_bound walk; sub_1485230
  = _Rb_tree_increment): (a) HM1-block type4 0x6C4B30 child's+408 by SUFFIX; (b) HM1
  type3 0x6C4A54 by suffix; (c) HM2 tail 0x6C4C24 OWN+408 by RAW label. Each match:
  entry.mode=a3; entry.value = rangeScale*clamp(disc?int(a4):a4, lo,hi)/range when
  range!=0 && rangeScale>0 else 0 (= normalizeParameterValueLike_0x6B1718).
- key split sub_6D0BF4@0x6D0BF4: FIRST "::" (else FIRST "/") → scope + suffix.
  WARNING: local splitParameterLabelLike uses rfind (LAST) — pre-existing divergence
  for nested scopes, tangential.
- erase **Player_purgeParameterRampMapByParent_guess@0x6CDE18**: 对当前 Player
  每个参数项，沿当前对象到 parent 的整条链，在相同 key 的 equal_range 中删除
  value==&param_entry 的 +408 节点（header@+416/count@+448）。唯一 xref 是
  Player_dtor@0x6CFADC 的首个业务调用，且发生在 +384 vector 释放之前。本地
  `purgeParameterRampMapLike_0x6CDE18` 已按相同调用时序复刻。

**CONFIG UAF 运行时证据（Web Debug ASan，2026-07-13）：** 点击标题页 CONFIG
后，ASan 报 `heap-use-after-free`，写地址位于已释放的 56B
`MotionParameterEntry` vector allocation 内；Wasm PC `0xB8A5EA` 符号化为
`applyParameterRampsLike_0x6C4C0C`，具体写入 entry+48 `mode`。根因正是此前
漏掉 0x6CDE18，使祖先 +408 map 在子 Player 析构后仍保留 entry 指针。

FALSIFIES prior memory "+408 controller RB-trees unpopulated / no consumer
(getVariable reads HM2 not HM1)": +408 IS populated by sub_6B1ECC and IS read by
bindParameter (3 loops). The DEFERRED claim was about heapResult-dispatch, not +408.

**Stage 1 PORTED (2026-06-06):** replaced dead `_parameterEntryById`
(unordered_map<string,size_t>, built-but-never-read invention) with faithful
`_parameterRampMap` = `std::multimap<ttstr,MotionParameterEntry*,ttstr_utf16_less>`
(player_containers.h). finalizeParameterTableLike_0x6B1ECC builds it; bindParameter
HM2 tail now consumes via equal_range(rawLabel) (applyParameterRampsLike_0x6C4C0C),
replacing the non-faithful full-vector scan that matched id==full||id==suffix on the
own player (suffix-match belonged to the descendant path). Descendant propagation
still via live node recursion (type3/4 children) — approximation of the
heapResult-driven suffix dispatch.

**Stage 2 STILL OPEN (DEFERRED, evidence-gap on inner loop):**
- sub_6B9650@0x6B9650 builds HM1 entry+48 heapResult = vector<MotionNode*> (gate
  V+40 weight==0→skip; clears weight + vector; scans node-deque idx>=1 for
  nodeType∈{3,4} via (nodeType-3)<=1; inner v26/v27 sub-loop uses node+36 child-count
  to descend + dedups via sub_6BA5B4 temp vector + key compare node+60/ttstr). The
  v26/v27 inner dedup loop + node+36 semantics need more disasm before faithful port.
- HM1-block consumer = TJS child dispatch (sub_6C1678@0x6C1678 PropGet child native
  ptr) + child's +408 equal_range by suffix. Couples to heapResult.
- EvalCascadeState.heapResult is still `void*` locally — change to
  vector<MotionNode*> when sub_6B9650 ported.
- reseek STEP5(B) (PlayerFrameProgress.cpp pruneHM3 tail) + pruneHM3 loop2 per-node
  restore remain DEFERRED until heapResult builder+consumer land.
