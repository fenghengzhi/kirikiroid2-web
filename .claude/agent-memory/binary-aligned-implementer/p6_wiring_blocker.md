---
name: p6-wiring-blocker
description: M1/P6 cursor-stepping wire-in — binary data flow is two SEPARATE passes (progress_inner cursor-steps then updateLayers eval-at-time reads slots), NOT eval-vs-cursor alternatives. Wiring belongs in progress pass, not updateLayers.
metadata:
  type: project
---

M1/P6 STRUCTURAL FINDING (2026-05-30, decompile-verified). The P6 task as
framed ("replace eval-at-time interpolation in updateLayers with cursor-stepping")
is INVERTED vs the binary. Ground truth from decompilation:

**Two SEPARATE passes, sequential, not alternatives:**
1. `Player_progress_inner` @ 0x6C106C — the ONLY caller of advance/rewind/reseek
   (0x6B6ADC / 0x6B9A3C / 0x6B86C8; xrefs_to confirms zero other callers). This
   pass cursor-steps each node and parseFrame/mergeFrameContent into the two
   536B node slots (node+320 / node+856).
2. `Player_updateLayers` @ 0x6BB33C — reads those slots via
   `Player_evaluateTimeline` @ 0x699AE4 (single call at 0x6bb5f0, time arg =
   player+456 = _clampedEvalTime). updateLayers does NOT call advance/rewind/
   reseek at all.

**`Player_evaluateTimeline` @ 0x699AE4 IS eval-at-time interpolation** (decompiled):
ratio v3 = (a3 - slot[active]+328) / (slot[other]+328 - slot[active]+328) @0x699ccc,
then lerp `b*ratio + a*(1-ratio)` on angle(+128, 180-wrap), zoom(+136/144),
scale(+152/160), pos(sub_69A4D4), opacity(+88->+1576 round). So the binary's
updateLayers stage is eval-at-time BY DESIGN. The cursor-stepping (advance/rewind)
FILLS the slots that eval-at-time then reads; they are sequential stages, NOT
competing models.

**Local divergence (real, but NOT what P6 says to fix):** local
updateLayersPhase2_MainLoop (PlayerUpdateLayerEval.cpp) COLLAPSES the two passes:
`advanceNodeFrameSelectionLike_0x6926B4` does an inline seek at top of the loop +
`evaluateTimelineLike_0x699AE4` interpolates — both in the SAME pass. The binary
splits them: progress_inner seeks, updateLayers interpolates.

**Correct P6 (binary-aligned) = wire P3/P4 cursor-stepping into the PROGRESS pass
(PlayerFrameProgress.cpp, models 0x6C106C), filling node slots, and LEAVE
updateLayers' evaluateTimelineLike (eval-at-time) as the slot CONSUMER.** P5's
PlayerFrameProgress.cpp currently advances _clampedEvalTime only and does NOT yet
call advanceRootAndNodesLike etc. — that call site (LABEL_48 region, ~line 811/860)
is where cursor-stepping belongs.

**Blocker for the inline-seek removal:** to move the seek out of updateLayers into
progress, the live MotionNode slots (MotionNode::ClipSlot) must be filled by the
progress-pass cursor-stepping BEFORE updateLayers runs. But the P3/P4 routines
operate on the PARALLEL NodeFrameStreamsLike / ParsedFrameSlotLike_0x6926B4 model,
which is a SEPARATE struct from the live MotionNode::ClipSlot. Unifying them is the
prerequisite, and it touches the live frame path (high red risk). Recommend NOT
pushing the task-as-framed; instead either (a) unify slot structs + wire
progress-pass seek as a dedicated phase, or (b) keep current collapsed model
(already differential-green) and only document the two-pass split.
</content>
</invoke>
