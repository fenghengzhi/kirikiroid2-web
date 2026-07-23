# CLUSTER G — Player frame progress + timeline evaluation alignment ledger

> Date: 2026-05-30
> Method: IDA Pro MCP decompile of 18 binary functions vs cpp/plugins/motionplayer/ port.
> Authority: libkrkr2.so. Local code may be wrong. Reject functional equivalence.
> IDB: renamed 6 sub_* -> Player_*; 15 functions commented; idb_save done.

## 当前状态（2026-07-19，取代旧 verdict）

raw node-deque/frameList 两槽 parse/merge、advance/rewind/reseek 与主标量游标链已接入；
错误的 Player `_timelines/_playingTimelineLabels`、control/blend animator 和 decoded
timeline 表已删除。公开 timeline API 归属 EmoteEngine HM3/+1040。当前仍 open 的是
原版 per-frame renderList owner 与本地 `_nodes` 门控的容器差异。

## 历史基线：❌ SEVERE DIVERGENCE（不再描述当前代码）

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

> 2026-07-19 owner 勘误：下表 G11/G12/G13 的 `Player_*Timeline` 命名与本地对照已被
> NCB 注册证据证伪。`0x672F70/0x67C2A0/0x673F98` 属于 EmoteEngine；Motion.Player
> 注册表无 timeline API。本地 Player timeline map/vector/API 已删除，真实 Engine
> HM3/+1040 与 typed controller deques 已接线。下列旧行保留为历史审计输入，不能再
> 作为当前实现结论。

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
| G10 | SeparateLayerAdaptor_ClearRetiredLayerMap_guess @ 0x6c72e4 | `SeparateLayerAdaptor::endRenderLayerPassLike_0x6C4E28` | CLOSED 2026-07-23 | 旧名 `Player_evaluateTimelines_guess` 已证伪并在 IDB 纠正。它由 0x6C4E28 正常尾部以 player+760 SLA 调用，遍历 SLA+112 retired Rb_tree，对 node+40 Object Variant 调 Invalidate、析构 payload、删节点并 reset tree；异常 unwind 不调用。不是 timeline/playing-list。 |
| G11 | EmoteEngine_playTimeline @ 0x672f70 | EmoteEngine.cpp | CLOSED | HM3 lookup、+1040 active-label vector、controller reset/seek 与 miss throw 已归回 Engine；错误 Player API 已删除。 |
| G12 | EmoteEngine_stopTimeline @ 0x67c2a0 | EmoteEngine.cpp | CLOSED | named erase 与 empty-label 全量 ttstr Release/clear 已由 Engine owner 承担；错误 Player map mutation 已删除。 |
| G13 | EmoteEngine_isAnimating @ 0x673f98 | EmoteEngine.cpp | CLOSED | controller buckets/HM 与 active labels 的查询属于 Engine；错误 Player `isPlayingCompat` 已删除。 |
| G14 | Player_parseFrame @ 0x6926b4 | PlayerFrameProgress.cpp (MISSING) | P0 | Binary parses one PSB frame via TJS dispatch (FuncCall idx -> obj; PropGet time/type/content/mask; mask&0x40000 -> act var slot+288 AddRef). Port has no live frame-parse path in progress. |
| G15 | Player_mergeFrameContent @ 0x692ab0 | PlayerFrameProgress.cpp (MISSING) | P0 | Binary merges ~30 frame fields into node via TJS PropGet dispatch gated by slot+5 mask bits (exact bitmasks recorded in IDB comment). Port performs frame field application elsewhere via PSB structs, not via this mask-gated dispatch path. |
| G16 | **已纠正：0x6D139C 是 `Player_getVariableKeys`，不是 loop timeline** | `PlayerVariable.cpp` / `Player.h` | CLOSED | `Player_ncb_registerMembers@0x6D69C8` 在 `0x6D6CEC` 将该函数只读绑定到二进制字面量 `variableKeys`；函数遍历 Player+1296 的 var-track deque 并新建 TJS Array。旧记录把它错接到 `getLoopTimeline`，现删除该错误地址与错误返回类型推论。Motion.Player timeline API 必须重新从注册函数逐项取证。 |
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
