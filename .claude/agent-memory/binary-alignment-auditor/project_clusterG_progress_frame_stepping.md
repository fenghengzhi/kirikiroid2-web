---
name: clusterG-progress-frame-stepping
description: 2026-05-30 Cluster G audit — Player progress/timeline core is a node-deque frame-stepping machine in binary, replaced by STL timeline machine in port. Severe divergence.
metadata:
  type: project
---

Cluster G (Player frame progress + timeline) audited 2026-05-30. Verdict: ❌ SEVERE.

**Binary architecture (authority):** progress is a node-deque frame-stepping machine.
- Player_progress_inner @0x6c106c: dt=ms*60/1000; +592=speedMul(+1168)*dt; advances
  scalar cursor +1120(frameTickCount) gated by +480(progressFlags,init 257,LSB);
  +456 clampedEvalTime = min(+1120,+1128 totalFrames); +1136 loopTime; +481 firstFrame
  one-shot; +483 motionCompleted / +1098 syncWaiting checked between EVERY advance step.
- Forward: Player_advanceRootAndNodes_guess @0x6b6adc -> Player_advanceNodeFrames_guess
  @0x6b7e44 (node+1392 slot, 536B frame stride) -> Player_parseFrame @0x6926b4 (TJS
  PropGet time/type/content/mask) -> Player_mergeFrameContent @0x692ab0 (mask-gated
  field merge via PropGet, slot+5 bitmasks). Backward: Player_rewindRootAndNodes @0x6b9a3c.
- Player_isAnimating @0x673f98: scans 3 controller buckets + 5 inline hashtables (NOT
  just `!playingLabels.empty()`).
- Player_getLoopTime_array @0x6d139c: returns a TJS Array (sub_704CB8), iterates node
  deque a1[164..168] 160B stride, new(0x1F4) per entry. NOT bool/scalar.
- Player_playImpl @0x6b2284: Player_loadMotion(+968 chara,var)->content; content type==1
  emote vs non-emote; stores +976/+984; throws 'motion not found'.
- Player_play_NCBWrapper @0x67f40c: trampoline Player_play(*(objthis+1064),flags,&var).

**Port architecture:** STL timeline machine. PlayerFrameProgress.cpp frameProgress uses
_timelines map + per-track control-animator queues + blend animators +
preProgressPlayingTimelinesLike_0x671764 + stepQueuedAnimatorLike_0x67D01C. No node-deque,
no frame index arithmetic, no parseFrame/mergeFrameContent. _clampedEvalTime set from
activeClipTime(clip) lookup (wrong source vs binary min-clamp).

**Key takeaway:** advance/rewind/parseFrame/mergeFrameContent are MISSING (no live port
path). Not patchable — needs Phase-B/C re-architecture from Player_progress_inner down.
Ledger: analysis/audit_motionplayer_2026-05-30/clusterG_progress_timeline.md.
IDB: renamed 0x6b6adc/0x6b7e44/0x6b9a3c/0x699ae4/0x6926b4/0x692ab0; 15 funcs commented.
