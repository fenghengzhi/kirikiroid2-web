# CLUSTER G — Player frame progress + timeline evaluation alignment ledger

> Date: 2026-05-30
> Method: IDA Pro MCP decompile of 18 binary functions vs cpp/plugins/motionplayer/ port.
> Authority: libkrkr2.so. Local code may be wrong. Reject functional equivalence.
> IDB: renamed 6 sub_* -> Player_*; 15 functions commented; idb_save done.

## VERDICT: ❌ SEVERE DIVERGENCE (architecture-level)

The binary's progress pipeline operates on a **node-deque frame-stepping machine**:
`Player_progress_inner` drives forward/back through PSB frame entries via
`Player_advance*/rewind*` -> `Player_advanceNodeFrames` -> `Player_parseFrame`
-> `Player_mergeFrameContent` (TJS PropGet dispatch on the motion object), with a
single scalar play-cursor (`player+1120` frameTickCount, `+456` clampedEvalTime,
`+1128` totalFrames, `+1136` loopTime, `+480` progressFlags, `+481` firstFrame).

The port's `frameProgress` is a **completely different machine**: STL timeline-state
maps (`_timelines`), per-track control-animator queues, blend animators,
`preProgressPlayingTimelinesLike_0x671764` looping, `_evalResultValues` clearing,
`applyEvalResultPostProcessLike_0x67CC9C`, controller-bucket stepping at dt<=1.1.
**None of the binary's per-node frame parse/merge stepping exists in the port's
progress path.** This is not a patchable branch/constant delta — it is a different
data-flow topology. Cannot be reconciled by local patching; requires a Phase-B/C
re-architecture of the whole progress core onto node-deque frame stepping.

---

## Findings table

| id | func @ addr | local file:line | sev | one-line |
|----|-------------|-----------------|-----|----------|
| G1 | Player_progress_inner @ 0x6c106c | PlayerFrameProgress.cpp:789 frameProgress | P0 | Binary steps node-deque frames via advance/rewind+parseFrame; port runs STL timeline/control-animator machine — different topology, no frame-index stepping. |
| G2 | Player_progress_inner @ 0x6c106c | PlayerFrameProgress.cpp:792 | P0 | `+480` progressFlags is a 16-bit gate (init 257, LSB tested at 0x6c1330/0x6c10f0). Port models `_queuing`(bit) + `_speed`(bool) only; binary's `+481` firstFrame one-shot + `+480` LSB dual gate not reproduced. |
| G3 | Player_progress_inner @ 0x6c106c | PlayerFrameProgress.cpp:809-811 | P0 | Binary: `+592 = speedMul(+1168)*dt`; advances `+1120` only when `!+480`; clamps `+456=min(+1120,+1128)`. Port unconditionally does `_frameLoopTime/_loopTime/_frameTickCount += dt` with no speedMul, no `+480` gate, no `min` clamp into eval time. |
| G4 | Player_progress_inner @ 0x6c106c | PlayerFrameProgress.cpp:850-853 | P0 | `+456` clampedEvalTime in binary is the frame-cursor min-clamp; port sets it to `activeClipTime(selectActiveClip())` (a timeline-map lookup). Wrong source value. |
| G5 | Player_progress_inner loop @ 0x6c10fc | PlayerFrameProgress.cpp:789 | P1 | Binary early-returns on `+1098 syncWaiting` / `+483 motionCompleted` between every advance step (re-checked ~10×). Port has no per-step sync/complete early-exit interleaving. |
| G6 | Player_advanceRootAndNodes_guess @ 0x6b6adc | PlayerFrameProgress.cpp (MISSING) | P0 | Forward motionList stepping (`+1072` dispatch, `+916`/`+928` cursor, type==1 content dispatch, `+1093` stop/sync gate, sub_6B638C action) absent locally. |
| G7 | Player_advanceNodeFrames_guess @ 0x6b7e44 | PlayerFrameProgress.cpp (MISSING) | P0 | Per-node bidirectional frame seek (node+1392 slot, 536B frame stride, parseFrame vs player+456, node+346/+882 merge gates) absent locally. |
| G8 | Player_rewindRootAndNodes_guess @ 0x6b9a3c | PlayerFrameProgress.cpp (MISSING) | P0 | Backward frame stepping (decrement cursors, parseFrame idx-1) absent locally. |
| G9 | Player_evaluateTimeline @ 0x699ae4 | PlayerUpdateChildMotion.cpp interpolatePosition69A4D4 (partial) | P1 | Two-slot transform interpolation (ratio mod +336, 180° shortest-path, type 4/5/10 channel copy, sub_69A754 easing) is the real per-node eval; port's frameProgress has no equivalent; only the child-motion crossfade path partially mirrors sub_69A4D4. |
| G10 | Player_evaluateTimelines_guess @ 0x6c72e4 | PlayerTimeline.cpp:204 stopTimeline (closest) | P1 | This is a teardown sweep (FuncCall vtbl+112 + delete + Release over playing-list, reset head a1[16..19]), NOT per-frame timeline eval. Port has no direct counterpart; name is misleading. Treat as list-clear/finalize. |
| G11 | Player_playTimeline @ 0x672f70 | PlayerTimeline.cpp:155 playTimeline | P1 | Binary keys playing list by ttstr-hash into inline hashtable (a1+117, mod a1[118]); dedupe by hashed compare; sub_670840+sub_671A50 reset; throws on miss. Port uses std::vector linear find + unordered_map; no throw, no hashtable. |
| G12 | Player_stopTimeline @ 0x67c2a0 | PlayerTimeline.cpp:204 stopTimeline | P1 | Binary: empty-label releases all playing-list TJS variant refs (a1[130..131]); non-empty sub_68C304 erase. Port mutates `_timelines` state (blend/control reset) AND clears std::vector — extra state mutation not in binary, and missing variant Release. |
| G13 | Player_isAnimating @ 0x673f98 | PlayerTimeline.cpp:561 isPlayingCompat | P0 | Binary scans 3 controller buckets + 5 inline hashtables for any animating controller matching a playing label. Port returns `!_playingTimelineLabels.empty()` — drastically simplified; ignores controller animation state. |
| G14 | Player_parseFrame @ 0x6926b4 | PlayerFrameProgress.cpp (MISSING) | P0 | Binary parses one PSB frame via TJS dispatch (FuncCall idx -> obj; PropGet time/type/content/mask; mask&0x40000 -> act var slot+288 AddRef). Port has no live frame-parse path in progress. |
| G15 | Player_mergeFrameContent @ 0x692ab0 | PlayerFrameProgress.cpp (MISSING) | P0 | Binary merges ~30 frame fields into node via TJS PropGet dispatch gated by slot+5 mask bits (exact bitmasks recorded in IDB comment). Port performs frame field application elsewhere via PSB structs, not via this mask-gated dispatch path. |
| G16 | Player_getLoopTime_array @ 0x6d139c | PlayerTimeline.cpp:99 getLoopTimeline / Player.h:156 | P0 | Binary builds a **TJS Array** (sub_704CB8) by iterating the inline node deque (a1[164..168], 160B stride), `new(0x1F4)` per entry, AddRef dispatch. Port exposes `getLoopTimeline(ttstr)->bool` reading `_activeMotion->loopTimelines` map + scalar `_loopTime`. Wrong return type (bool/scalar vs TJS Array), wrong container. |
| G17 | Player_play @ 0x6b21e8 | PlayerCore.cpp:522 initNonEmoteMotionLike / PlayerTimeline.cpp:288 | P1 | Binary play(): flags&0x10 && !+968 -> store stealth var +768; else playImpl + replay +768 with flag 16. Port `playMotionLike_0x6B2284`/`playCompat` collapse this into timeline-list playOne loops; stealth replay (+768 flag-16 second pass) and chara-gate not faithfully reproduced. |
| G18 | Player_playImpl @ 0x6b2284 | PlayerCore.cpp:522 initNonEmoteMotionLike_0x6B365C | P0 | Binary gate `(flags&5)||motionDiffers`; flags&4&&playing skip; Player_loadMotion(+968 chara,var)->content; content type==1 emote vs non-emote split; stores +976/+984; throws 'motion not found' on fail. Port has no loadMotion+content-type dispatch; emote/non-emote split and +976/+984 dual-store missing; replaced by snapshot/clip activation. |
| G19 | Player_play_NCBWrapper @ 0x67f40c | (MISSING) | P2 | Trampoline `Player_play(*(objthis+1064), flags, &var)`; objthis+1064 = native Player back-ptr via EmoteObject. Port routes play through ncbInstanceAdaptor directly; the +1064 indirection (EmotePlayer->Player) is a separate registration path. |

---

## MISSING (no live counterpart in port progress path)

- Player_advanceRootAndNodes_guess @ 0x6b6adc  (forward root+node frame stepping)
- Player_advanceNodeFrames_guess @ 0x6b7e44     (per-node bidirectional frame seek)
- Player_rewindRootAndNodes_guess @ 0x6b9a3c     (backward frame stepping)
- Player_parseFrame @ 0x6926b4                   (PSB frame TJS-dispatch parse)
- Player_mergeFrameContent @ 0x692ab0            (mask-gated frame field merge via PropGet)
- Player_play_NCBWrapper @ 0x67f40c +1064 indirection

The port replaces this entire forward/back frame-stepping subsystem with an
STL timeline-state + control-animator-queue machine (PlayerFrameProgress.cpp
preProgressPlayingTimelinesLike_0x671764 + stepQueuedAnimatorLike_0x67D01C +
applyTimelineControlWindowLike_0x669E1C). That machine has no node-deque,
no frame index arithmetic, no parseFrame/mergeFrameContent, and a different
play-cursor model.

---

## Architecture-level note (why this can't be patched incrementally)

Binary play-cursor state:  +1120 frameTickCount | +1128 totalFrames |
+1136 loopTime | +456 clampedEvalTime=min-clamp | +480 progressFlags(257) |
+481 firstFrame | +483 motionCompleted | +1098 syncWaiting | +592 deltaTime=+1168*dt.

Port play-cursor state:    _frameTickCount | _cachedTotalFrames | _loopTime |
_clampedEvalTime=activeClipTime(clip) | _queuing(bit) | _speed(bool) |
+ per-timeline TimelineState{currentTime,totalFrames,loopTime,...}.

The mapping is many-to-many and the intermediate variables (frame index cursors
node+1392, +916, +568; merge mask gates slot+5; per-step sync/complete early
exits) have **no port equivalents**. Reaching 1:1 requires re-introducing the
node-deque frame-stepping core (advance/rewind + parseFrame + mergeFrameContent)
and demoting the STL timeline machine to whatever (if anything) the binary
actually uses. Recommend escalation to module-alignment-driver to stage the
re-architecture from Player_progress_inner downward; do not local-patch.
