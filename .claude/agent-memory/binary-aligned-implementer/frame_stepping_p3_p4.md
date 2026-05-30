---
name: frame-stepping-p3-p4
description: M1/P3+P4 node-deque frame cursor stepping (advance/rewind/reseek @0x6B6ADC/0x6B9A3C/0x6B86C8 + advanceNode @0x6B7E44) -> PlayerFrameStepping.{h,cpp}; 3 frame-stream cursors, 2-slot ping-pong seek, signed-compare trap, reseek double-increment
metadata:
  type: project
---

M1/P3+P4 (2026-05-30): ported the binary node-deque frame-stream cursor model +
forward/back/reseek cursor stepping as INDEPENDENT free functions, NOT wired to
live frame-progress path (logo differential untouched, 0-mismatch preserved).

**Files:** cpp/plugins/motionplayer/PlayerFrameStepping.{h,cpp} (namespace
motion::detail). Builds on P2's ParsedFrameSlotLike_0x6926B4 (PlayerFrameStep.h)
+ parseFrameLike/mergeFrameContentLike. Added clipStartTime (logical +328) to
that P2 slot struct. Test: tests/unit-tests/plugins/motionplayer-dll.cpp "frame
cursor stepping is binary-aligned (P3+P4)" (15 assertions, all pass; P2 still 18).
Added to motionplayer/CMakeLists.txt + platforms/wasmtime/CMakeLists.txt.

**Function map:**
- 0x6B7E44 Player_advanceNodeFrames -> advanceNodeFramesLike_0x6B7E44
- 0x6B6ADC Player_advanceRootAndNodes -> advanceRootAndNodesLike_0x6B6ADC
- 0x6B9A3C Player_rewindRootAndNodes -> rewindRootAndNodesLike_0x6B9A3C
- 0x6B86C8 Player_reseekTimelineCursors -> reseekTimelineCursorsLike_0x6B86C8
(IDB: renamed all 4 from _guess, set_comments + idb_save done.)

**3 frame-stream cursors (player-level, FrameStreamCursorLike):**
- LAYER stream: source +1072 (TJS Array dispatch), cursor=+916, curTime=+920,
  nextTime=+928. type==1 frame triggers +1093 motionStopGate action/align/sync.
- ROOT stream: source +548, cursor=+568, curTime=+576, nextTime=+584, current
  frame content snapshot copied to +616 (sub_A0FB64).
- VARIABLE-TRACK deque: +1312..1368 libstdc++ deque, 160B/track, 3-per-chunk,
  per-track 2-slot seed via sub_6B786C/sub_6B7A70. **DEFERRED** (opaque records,
  not reproducible from PSB stand-in).

**Per-node (NodeFrameStreamsLike, node-deque @+200, 2632B stride):**
- node+8 = CHILD timeline ptr (non-null => advanceNodeFrames, seeks to
  (node+8)+40 child eval time; null => inline seek to player+456). Modelled as
  hasChild + childEvalTime.
- node+28 nodeType, node+64 frameList, node+1996 timelineDirty.
- node+320/+856 = the two 536B parsed slots; node+1392 activeSlotIndex (toggled
  `(x&1)==0`). node+346 / node+882 = slots[0/1] +26 mergedFlag merge gates.

**Seek algorithm (2-slot ping-pong, advanceNodeFrames is cleanest):**
cur=slots[idx], other=slots[idx^1], limit=count-2. Forward: while
cur.frameIndex<limit && t>=other.time { activeSlotIndex^=1; parseFrame(cur,
frameList, other.frameIndex+1); swap cur<->other; seeked=1 }. Then if cur.time>t:
corrective backward loop (parseFrame other at cur.frameIndex-1 until time<=t).
elif !seeked: early-return (NO merge). Merge: both slots if !mergedFlag, then
findSource gate. action gate during seek = (slot.mask & 0x40000) -> sub_6B638C
(DEFERRED).

**findSource node-type mask:** !player+1092 ? 6145(0x1801) : 6153(0x1809);
gate = node+1996 || (mask & (1<<nodeType)). Body sub_6B638C/findSource DEFERRED.

**reseek layer scan TRAP (0x6B8770):** for(i=0;...;++i) DOUBLE-increments — the
for-clause ++i AND the body ++i both fire when frame.time<target. So
target=22/[0,10,20,30,40] gives cursor=3 (not 2): coarse overshoot by design,
advance/rewind corrects later. curTime/nextTime are (int)-truncated. Root scan is
single-step. align/sync gate runs on frames[cursor+1] when curTime==target &&
its type==1.

**ALIGNMENT TRAP (cost a debug cycle):** slot+0 frameIndex is SIGNED int in the
binary's seek comparisons (`*(_DWORD*)v45 < v46` where v46=count-2). Local
frameIndex is std::uint32_t; comparing `frameIndex < limit` with limit=count-2=-2
(empty stream) promotes -2 to 4294967294u and INFINITE-LOOPS. Must cast to int.
Fixed at the inline-node-seek `frameIndex < limit` sites in advanceRoot.

**DEFERRED (PLATFORM_BOUNDARY / deep dispatch):** variable-track deque seed;
+280 aux singly-linked list (sub_6B9650); Motion_Player_findSource layer-source
resolution (slot+348/+356, no live layer registry in PSB stand-in); sub_6B638C
action runner + sub_6B6294 (TJS dispatch — gate reproduced, body no-op);
slot+328 clipStartTime WRITER (read by rewind gate, populated by an init/evaluate
pass outside P2-P4 scope); reseek per-node Player_initNodeTimeline_guess inline
init (child nodes use advanceNodeFrames; non-child init deferred).

**Frame-stream stand-in:** like P2, a frame stream = PSB::PSBList of PSBDictionary
frames {time,type,content}; cursor/seek/gate/merge byte-faithful, only leaf
"frame[i].time" reads go through PSB instead of iTJSDispatch2 PropGet. NOTE
PSBList has NO default ctor (use PSBList(0)) and operator[] takes int not size_t.

**Why:** P3+P4 of analysis/Player_progress_frame_stepping_M1_plan.md (P5/P6
wire-in is high-risk, still deferred). **How to apply:** when wiring the live
node-deque frame stepping (P6), reuse these 4 routines; replace the PSB
stand-in frame streams with the real iTJSDispatch2 motion arrays, and resolve the
DEFERRED items (findSource, action runner, variable-track deque, slot+328 writer).
