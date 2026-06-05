---
name: reseek-step5-tail-prereq-gap
description: reseekTimelineCursors@0x6B86C8 STEP5 tail (0x6B9234 pruneHM3 + 0x6B9248 player+280 HM1-walk) — what the two containers are and why DEFERRED is architecture-prerequisite-missing, not oracle-inert
metadata:
  type: project
---

`Player_reseekTimelineCursors@0x6B86C8` STEP5 tail (UNGATED, runs every reseek; after the Player_initNodeTimeline node-loop @0x6B91B0). Two passes, both legitimately DEFERRED in port for **architecture-prerequisite-missing** (the container has no port writer) — NOT for "oracle-inert / no consumer". Corrected the old DEFERRED comment in PlayerFrameProgress.cpp STEP5 (was "no consumer", which violates CLAUDE.md oracle-inert rule).

**(A) Player_pruneHM3_byNodeIdentity@0x6B9234** — three sub-passes:
- loop1: node-track hashmap @player+1240 (a1[155]/a1[164..171] = node-track deque player+1312..1368), recompute path-key hash -> write node+72. Port: no node-track hashmap populate.
- loop2: HM3 @player+1184 (a1[148..151]) = `_perNodeLayerStateMap`. Prune entries whose Player_buildNodePathKey no longer matches a live NODE-deque (player+200..256, a1[25..32]) node. Player_HM3_entry_destroy + --count.
- tail `Player_clearHM3_HM4@0x6B80E8`: clears HM3 (player+1184=`_perNodeLayerStateMap`) AND HM4 (player+1240=`_variableSnapshotMap`); also list-head @player+1256 = HM4-node release (value=OWNING tTJSVariant*).
- PREREQ MISSING: both HM3 and HM4 have NO port writer. HM3 populate = resetMotionState loop3 @0x6B2DF8 + Player_HM3_initValueFromNode @0x699510 (688B node->V snapshot), unported. HM4 populate = resetMotionState loop2 @0x6B2D40, unported. Both maps permanently empty in port -> prune+clear is a provable no-op detached from its source.

**(B) 0x6B923C: `for(n=player+280; n; n=*n) sub_6B9650(a1, n+16)`** — KEY FINDING: player+280 is HM1's (`_evalCascadeMap`, Player+264) internal std::unordered_map before-begin all-entries chain (`a1[2]`=`_M_before_begin._M_nxt`; head-insert at Player_HM1_insert_node@0x6F541C `*a4=a1[2]; a1[2]=a4`). NOT a separately-addressable port field. n+16 (n+2) = HM1 entry value region (EvalCascadeState; node = operator new(0x60)).
- `sub_6B9650(Player, HM1-entry)`: gate entry+40(weight, =1.0 on first insert via 0x6c4964)==0.0 -> return; clear entry+48 vector (=`EvalCascadeState::heapResult`); scan NODE-deque, push nodeType in {3,4} nodes (deduped via sub_6BA5B4 + temp set). Rebuilds each HM1 entry's affected-node list.
- entry+48 list IS truly consumed in binary: bindParameterValue@0x6C4978 ramp-write loop (`*(slot+40)=ramp` per node, nodeType 3/4). BUT that consumer is ALSO DEFERRED in port's bindParameterValueLike_0x6C4668 (node+408 controller RB-trees unpopulated).
- PREREQ MISSING: heapResult has neither port writer (sub_6B9650) nor reader (consumer loop). Faithful order = port sub_6B9650 + bindParameter consumer loop FIRST, then this loop-wrap rebuild hook.

sub_6B9650 has 2 callers: reseek-tail (0x6b9248, walks ALL HM1 entries) + bindParameterValue (0x6c4974, single new entry).

**Why:** 2026-06-06 frame-stepping audit flagged STEP5 tail as G-Prune DEFERRED claiming "no live consumer". Cross-check showed the real stop is prerequisite-missing (resetMotionState + HM3_initValueFromNode + bindParameter ramp-consumer all unported), which is a CLAUDE.md legitimate stop; "no consumer" is not.
**How to apply:** When porting resetMotionState (HM3/HM4 populate) or the bindParameter ramp-consumer (entry+48 reader), THIS reseek tail must be wired at the same time — pruneHM3 needs a populated HM3, and the HM1-walk needs entry+48 to have a reader. Don't port either tail pass standalone (would walk an empty/unread container).
