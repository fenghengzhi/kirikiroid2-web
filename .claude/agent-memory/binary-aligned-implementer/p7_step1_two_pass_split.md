---
name: p7-step1-two-pass-split
description: M1/P7 step-1 DONE — hoisted live per-node frame seek out of updateLayers into the progress pass via Player::progressSeekNodeSlotsLike_0x6C106C, restoring the binary's two-pass split (progress fills slots / updateLayers reads slots) using Choice B (live ClipSlot IS the binary slot, no parallel-struct copy)
metadata:
  type: project
---

M1/P7 step-1 (2026-05-30, decompile-verified, built green web+wasmtime). Restored
the binary's two-pass data flow without adding a slot-copy bridge.

**DECISION — Choice B (live seek hoist), NOT Choice A (parallel-struct feed):**
The binary's per-node frame seek (Player_advanceNodeFrames 0x6B7E44) and the
interp read (Player_evaluateTimeline 0x699AE4) operate on the SAME node slots
(node+320/+856). In the port, the live `MotionNode::ClipSlot slots[2]` ARE those
slots, and the live `advanceNodeFrameSelectionLike_0x6926B4` already seeks them /
`evaluateTimelineLike_0x699AE4` already reads them. So the minimal AND most
binary-faithful move is to HOIST the live seek into the progress pass — one set
of slots, seek fills, interp reads. Choice A (feed PSB-sourced P3/P4
`NodeFrameStreamsLike` parallel structs then copy `ParsedFrameSlotLike`→ClipSlot)
would introduce a SECOND slot set + a copy bridge the binary does NOT have =
strictly less aligned. **The P2/P3/P4 parallel structs stay as the unit-tested
reference (richer cursor algo); they are NOT on the live path.**

**Decompile evidence (this turn, first-hand):**
- Player_progress_inner @0x6C106C: node-deque walk `for(j=1;...)` at 0x6C1288
  calls Player_advanceNodeFrames at 0x6C1264/0x6C130C. Root (idx 0) NOT seeked
  here (root-stream path). Plus advance/rewind/reseek at LABEL_48.
- Player_updateLayers @0x6BB33C main loop: ONLY `Player_evaluateTimeline(v22,
  dirty, *(a1+456))` at 0x6BB5F0. ZERO advance/rewind/reseek. node+44 cleared at
  END (0x6BBD2C `*(v86+44)=0`). => progress sets node.flags|=1, updateLayers
  reads, updateLayers clears. Lifecycle preserved by the hoist (nothing clears
  flags between progress and updateLayers).

**Implementation (touched files):**
- Player.h: decl `void progressSeekNodeSlotsLike_0x6C106C(double clampedEvalTime)`.
- PlayerUpdateLayerEval.cpp: def of that driver — `for(i=1;i<nodes.size();++i)
  advanceNodeFrameSelectionLike_0x6926B4(node, clampedEvalTime);` (idx 1+, root
  excluded, matches phase2 range + binary 0x6C1288). Also added read-only
  `readNodeFrameSlotsLike_0x699AE4(node,t)` = `frameStateFromNodeSlots(node,
  frameSelectionTimeLike_0x6B7E44(node,t))` (NOINLINE, decl in PlayerInternal.h)
  because the anon-ns helper isn't visible from the `namespace motion{}` block.
- PlayerFrameProgress.cpp::frameProgress: after `_clampedEvalTime=min(...)`
  (P5 site) -> `if(!_nodes.empty()) progressSeekNodeSlotsLike_0x6C106C(_clampedEvalTime);`
- PlayerUpdateLayerEval.cpp::updateLayersPhase2_MainLoop line ~736: replaced
  `advanceNodeFrameSelectionLike_0x6926B4(node,currentTime)` (SEEK) with
  `readNodeFrameSlotsLike_0x699AE4(node,currentTime)` (READ-only). evaluateTimeline
  call below unchanged.

**ParsedFrameSlot→ClipSlot mapping: IDENTITY, no copy.** Under Choice B the live
ClipSlot IS the binary slot, so every layer-state field
(done/crossfading/frameType/src/x/y/z/ox/oy/opacity/angle/scaleX/scaleY/slantX/
slantY/flipX/flipY/blendMode/packedColors/clipStartTime/curves ccc..cp/transform
Order/motion*/prt*) is written by the live seek into ClipSlot and read in-place.
The drift surface is therefore NOT a mapping gap but whatever the live seek
(advanceNodeFrameSelectionLike + populateClipSlotFromFrameLike_0x6926B4) already
diverges from the binary advance/merge — unchanged by this step.

**STEP-1 SCOPE / first-round CI drift self-assessment:**
- Forward-only. Reverse rewind (deltaTime<0 -> Player_rewindRootAndNodes
  0x6B9A3C) + full reseek (firstFrame/loop-wrap Player_reseekTimelineCursors
  0x6B86C8) NOT wired = TODO P7 step-2. advanceNodeFrameSelectionLike has a
  corrective-backward sub-loop so small rewinds are covered; large reverse seeks
  / loop wraps will drift on reverse-playing or looping motions.
- hitTestLayer (PlayerLayerQuery.cpp:223) calls updateLayers() with NO preceding
  frameProgress; old model re-seeked inline, new model reads last-seeked slots.
  Same _clampedEvalTime => slots current => no drift in normal flow; only a
  no-progress standalone hit-test on a never-progressed player reads default
  slots (edge, low risk).
- clipStartTime writer / color-decode deferred items: NOT pre-filled this step;
  they were already DEFERRED in P2/P3/P4 and are unchanged. If the logo
  differential was green under the collapsed model it should stay green (forward
  monotonic time), since the seek is bit-identical, just moved earlier.
- Likely-red-first: looping motions (loop wrap), reverse playback, and any motion
  whose firstFrame seed relied on the inline-seek-in-updateLayers timing.

**Why this is safe vs the prior P6 blocker:** the p6_*.md blocker was "task forbids
touching P2/P3/P4 but binary progress IS the P2/P3/P4 machine". Choice B sidesteps
it: we do NOT wire P2/P3/P4 into live; we hoist the EXISTING live seek (already on
ClipSlots) into progress. P2/P3/P4 untouched, two-pass split achieved.
