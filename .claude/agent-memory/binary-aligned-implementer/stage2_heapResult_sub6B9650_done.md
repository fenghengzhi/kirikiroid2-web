---
name: stage2-heapResult-sub6B9650-done
description: Stage 2 sub_6B9650 heapResult builder + chainSegments + bindParameter HM1 consumer + reseek STEP5(B) all PORTED; corrects prior "node+408 unported -> DEFERRED" misID
metadata:
  type: project
---

Stage 2 (sub_6B9650 @0x6B9650 heapResult builder + downstream) PORTED on dev/motion.

**Why:** prior session DEFERRED this on premise "entry+48 node list has no port reader; node+408 ramp consumer unported (node+408 not modeled on MotionNode)". That premise is FALSIFIED — node+408 is NOT a MotionNode field; it is **Player+408 of the CHILD player** reached THROUGH the node (sub_6C1678 @0x6C1678 = PropGet member 200 -> native child Player; child+408 = child's _parameterRampMap, already modeled Stage 1). Builder + consumer are both inside bindParameter @0x6C4668 (consumer loop @0x6c4978 is offset-within-0x6C4668, NOT a separate fn — the "@0x6C4978" in old comments confused this).

**How to apply (contracts, all fresh-decompiled this session):**
- **heapResult (EvalCascadeState V+48..+64) = std::vector<MotionNode*>** (NOT tTJSVariant*/RenderItem*). Stores node ptrs (stride 2632, or via deque index-redirect a1[28]). NON-owning (dtor frees backing only, no per-elem Release). Pushed value = `v6+2632*scanNodeIndex` (the node).
- **chainSegments (V+8/+16/+24) = std::vector<tTJSVariant<string>>** = scope label split by "::" via sub_697D34 @0x697D34. Built once on first HM1 insert, FROZEN. The dedup reference. (value_structs.h old comment mislabeled V+16 "iTJSDispatch2* chainDispatches" + spurious "mainDispatch V+8" — CORRECTED: V+0 key, V+8 chainSegments vec, V+32 writeVal, V+40 weight, V+48 heapResult vec.)
- sub_6B9650: gate entry.weight==0->return; **clears weight to 0** (one-shot dirty flag, 0x6b96ac); clears heapResult (end=begin); scan node-deque idx 1..size()-1 (NOTE: binary `dequeSize-1` is libstdc++ size() inline +1-bias-cancel for 2632B 1-elem/block deque == real _nodes.size(); author wrote `< size()`, NOT size()-1, NO sentinel — same as advanceNodeFrames 0x6B7E44). Per type3/4 node: ancestor-walk via node+36 parentIndex collecting layer LABELS into shared local `chain`, truncate chain to <= ref.size (pop_back newest when over), compare when ==ref.size; match (or both empty) -> push node; climb parentIndex<=0 -> stop. CHAIN PERSISTS across scan nodes (not reset) — binary artifact: once a matching window freezes at ref.size, subsequent type3/4 nodes re-compare frozen window (reproduced faithfully).
- **node+36 = parentNodeIndex** (int, 0-based deque idx; root's children=0). Writer buildNodeTree_recursive @0x6B4A6C STR @0x6b4bf8 (a2=parent's own index). Reader sub_6B9650 climb @0x6b9958. Local MotionNode.parentIndex models it.
- Consumer @0x6c4978 (inside bindParameter): for each heapResult node ramp CHILD Player +408 (_parameterRampMap) by SUFFIX (&v101=parts.suffix) via applyParameterRampsLike_0x6C4C0C — NOT by re-running HM1/HM2. type4: per-particle-child (node+2296); type3: own child (node+1912). REPLACED prior non-faithful _nodes full-recursion-by-full-label approximation.
- STEP5(B) reseek tail @0x6B923C-48: `for(n=player+280; n; n=*n) sub_6B9650(a1, n+16)` = walk ALL HM1 entries, rebuild each. Ported as `for(kv:_evalCascadeMap) rebuildEvalCascadeHeapResultLike_0x6B9650(kv.second)`. unordered_map iter order irrelevant (each entry independent). By reseek time weight already 0 (bind cleared it) -> effectively no-op for bound entries, faithful.

**Local symbols:** Player::rebuildEvalCascadeHeapResultLike_0x6B9650 + splitScopeSegmentsLike_0x697D34 (anon-ns, PlayerVariable.cpp); EvalCascadeState{keyCopy,chainSegments,writeVal,weight,heapResult} (value_structs.h, MotionNode fwd-decl added). Files: PlayerVariable.cpp, PlayerFrameProgress.cpp STEP5(B), Player.h, value_structs.h.

**SELF-AUDIT caught bug:** first impl used `scanNodeIndex < nodeCount-1` (off-by-one, dropped last node) — fixed to `< _nodes.size()` per the deque size()-inline rule. Auditor sub-agents NOT available in this env; audited inline against decompile.

**Verification:** web debug + krkr2_wasmtime_guest clean; m2logo 93f + yuzulogo 243f PASS bit-identical. ORACLE-INERT for both logos (no "::"/"/"-scoped var binds -> HM1 cascade never fires -> heapResult never built) = non-regression guard; NO scoped-bind fixture (honest gap). NOT committed (Stage 1 was be77533).

**OPEN sub-gaps (genuine, not invented):** sub_697D34 also builds via TJS dispatch resolution in binary (sub_A0CBEC/sub_A0CA58 = TJS string ops) — port uses plain std::string "::"-split of the scope, value-equivalent for the wcscmp dedup (the only reader). node+0 label: binary compares `*(node+0)` tTJSVariant (layer-object first qword); port uses MotionNode.layerName (the "label") — asserted equal, not byte-verified the layer-object internal. pruneHM3 loop2 per-node restore (sub_6997F0) still DEFERRED (separate, node+46 visible byte unmodeled).
