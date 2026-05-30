---
name: p7-step2-label48-branches
description: M1/P7 step-2 DONE — implemented Player_progress_inner 0x6C106C LABEL_48 forward-to-end(loop|stop) + reverse rewind(3 branches) + fwd/rev loop-wrap do-while in PlayerFrameProgress.cpp frameProgress, gated so forward-not-at-end (logo) stays bit-identical to P7 step-1
metadata:
  type: project
---

M1/P7 step-2 (2026-05-30, decompile-verified 0x6C106C, built green web+wasmtime).
Completed the non-forward LABEL_48 paths left as TODO by P7 step-1.

**LABEL_48 (0x6C1330) full structure (verified):**
- Gated clamp (0x6C1340..1354): if(!+480){ +1120+=+592; +456=min(+1120,+1128) }.
- FORWARD +592>=0 (0x6C135C): if +1128<=+1120 (at-end): +456=+1128; if +1136>=0
  LOOP{ advanceRootAndNodes; if(!sync&&!completed){ +456=+1136; reseek; if(...){
  LABEL_22/23 wrap: v7=+1120; if(+1128>v7) only +456=v7; else do v7+=+1136-+1128
  while(+1128<=v7); +1120=v7; +456=v7; advanceRootAndNodes } } } else (+1136<0)
  STOP{ +1099=0; if(!gate) advanceRootAndNodes }. else-if !gate (not-at-end) ->
  advanceRootAndNodes (= logo path).
- REVERSE +592<0 (0x6C1360): if(+1120>=0 && +1136<=+1120)->LABEL_57{ if(!gate)
  rewindRootAndNodes }. elif +1136<0 { +456=0; +1099=0; +1120=0; LABEL_57 }. else
  loop-wrap{ +456=+1136; rewindRootAndNodes; if(!sync&&!completed){ +456=+1128;
  reseek; if(...){ LABEL_27/28: v7=+1120; if(+1136<=v7) only +456=v7; else do
  v7+=+1128-+1136 while(+1136>v7); +1120=v7; +456=v7; rewindRootAndNodes } } }.

**KEY ALIGNMENT INSIGHT (why no separate reverse-seek helper):** the live
per-node seek advanceNodeFrameSelectionLike_0x6926B4 (PlayerUpdateLayerEval.cpp:357)
is DIRECTION-AGNOSTIC — it has BOTH a forward slot loop (lines 379-388) AND a
corrective-backward loop (390-398). So a single
progressSeekNodeSlotsLike_0x6C106C(+456) reproduces the binary's
advanceRootAndNodes (0x6B6ADC) AND rewindRootAndNodes (0x6B9A3C). Reverse/loop-wrap
is wired purely by setting +456 (_clampedEvalTime) / +1120 (_frameTickCount) per
the branch, then seeking. The root-level advance/rewind/reseek free functions in
PlayerFrameStepping.cpp stay UNWIRED (unit-test reference only).

**Forward/logo bit-identity (CI-green prerequisite, self-assessed OK):** logo =
_progressFlags=false, forward, not-at-end. Path: line 831 advance (unchanged) ->
gated clamp _clampedEvalTime=min=_frameTickCount (same value as removed step-1
line) -> forward `else if(!gate)` reseekNodes=true, +456 NOT touched -> final
`if(reseekNodes&&!nodes.empty()) seek(_clampedEvalTime)` = same call/arg as step-1.
New code is actually MORE binary-faithful than step-1 (step-1 always seeked;
binary gates the not-at-end seek on !gate — never triggers for logo).

**DEFER (documented in-code at the loop branch):** PlayerFrameProgress.cpp:821
`_loopTime += actualDelta` CORRUPTS +1136 (the loop-wrap target set from
clip->loopTime at PlayerCore.cpp:557; binary NEVER mutates +1136 in
progress_inner). The loop-wrap math is STRUCTURALLY aligned but only numerically
correct once that accumulation is removed/relocated. Does NOT affect logo (loop
branches need _frameTickCount>=_cachedTotalFrames). Needs its own decompile pass
on where the +1136 accumulator value is actually consumed (PlayerUpdateChildMotion
:151 reads _loopTime as `loopEnd`).

**Touched:** cpp/plugins/motionplayer/PlayerFrameProgress.cpp only (frameProgress,
replaced the step-1 single-clamp+forward-seek block with full LABEL_48 branch).
IDB: renamed 0x6B6ADC/0x6B9A3C/0x6B7E44 off _guess (already clean), set_comments
on LABEL_48 region (0x6C1330/135C/1360/14C4/1454), idb_save.

**NO runtime verification done locally** (differential not run per task). CI
differential.yml on push validates forward/logo green.
