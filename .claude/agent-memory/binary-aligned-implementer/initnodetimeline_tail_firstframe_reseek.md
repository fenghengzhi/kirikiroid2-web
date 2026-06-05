---
name: initnodetimeline-tail-firstframe-reseek
description: 缺口#2 initNodeTimeline tail onAction push + 缺口#1c firstFrame full reseek — both DONE 2026-06-05
metadata:
  type: project
---

缺口#2 + 缺口#1c CLOSED 2026-06-05 (PlayerUpdateLayerEval.cpp + PlayerFrameProgress.cpp), both oracle-inert for logo.

**缺口#2 — initNodeTimeline tail per-node onAction (Player_initNodeTimeline tail @0x6B674C):**
Binary tail after parsing slot[0]/slot[1]: `if (v8 == *(double*)(node+328) && (*(_BYTE*)(node+342) & 4)) pushActionEvent(player, &node+0, node+608)`.
- v8 = frameSelectionTimeLike_0x6B7E44 (node+8 ? *(node+8)+40 : player+456) — the seed target.
- node+328 = slot[0]+8 = time field (= ClipSlot.clipStartTime). slot base for slot[0] IS node+320.
- node+342 = slot[0]+22 = mask byte 2; &4 == mask & 0x40000 (parseFrame @0x6928EC sets slot+288='act' ONLY under that bit). Local proxy = `!slots[0].action.empty()` (faithful — action stored exactly when frame carries one; same gate as existing fireNodeAction).
- node+608 = slot[0]+288 = action variant. push args = {type=0 ACTION, param1=node.layerName(node+0='label' per initNodeFields 0x6B3DF4), param2=slot0.action}.
KEY: tail fires for slot[0] (activeSlotIndex==0 after seed). Local: added optional `std::vector<MotionEvent>* pendingEvents=nullptr` param to initializeNodeTimelineSlotsLike_0x6B64AC; push when selectionTime==slot[0].clipStartTime && !action.empty().
WIRING (3 call sites): pass &_pendingEvents at (a) reseekNodeTimelineSlotsLike_0x6B91B0 (reseek STEP 4 = genuine 0x6B64AC) (b) preProgressDirtyNodesLike_0x6B6878 (dirty rebuild = genuine 0x6B64AC). DO NOT pass at (c) the lazy-init inside advanceNodeFrameSelectionLike_0x6926B4 — binary inline seeks (0x6B73DC/0x6BA1CC) never call 0x6B64AC and have their OWN action push (fireNodeAction in seek loop); passing events there = double-fire.

**缺口#1c — firstFrame _queuing branch full reseek (progress_inner @0x6C106C firstFrame):**
Binary firstFrame calls FULL Player_reseekTimelineCursors at BOTH seeds: activeTimeline path 0x6C10E0 (`return reseekTimelineCursors`), queuing path 0x6C131C (reverseSeekFlag+609-clear branch). Local _queuing branch (PlayerFrameProgress.cpp ~1950) previously ran ONLY reseedVariableTracksLike_0x6B86C8 (the 0x6B8F30 var-track sub-piece) + progressSeekNodeSlotsLike — OMITTED reseek's LAYER scan (0x6B8770→+916/+920/+928+align/sync/action), ROOT step (0x6B8C1C→+568/+616/+576/+584), absolute node re-seed (0x6B91B0). Fix: replaced with `reseekTimelineCursors(_clampedEvalTime)` (full). STEP 4 of reseek does absolute node re-seed → dropped now-redundant progressSeekNodeSlotsLike (binary RETURNS after reseek on activeTimeline path). The firstFrame root-seed gap was formerly masked by an entry-side per-tick recompute removed in commit ea808b8.

VERIFY: web debug + krkr2_wasmtime_guest build clean. motion_playback differential PASS bit-identical (yuzulogo 243f, m2logo 93f). Both gaps oracle-inert (logo non-loop, no populated action frames at seed) → 0-mismatch is non-regression guard, not exercise. No new .cpp files → no CMake source-list change.
