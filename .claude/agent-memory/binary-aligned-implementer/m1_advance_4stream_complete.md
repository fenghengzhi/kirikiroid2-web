---
name: m1-advance-4stream-complete
description: M1 advance-unit now runs all 4 streams [layer→root→var→node] at every advanceRoot point in PlayerFrameProgress.cpp frameProgress. Stream② root content-snapshot (+616) added 2026-06-03; stream③ var-track was ALREADY wired (06-02 review was stale).
metadata:
  type: project
---

**CORRECTED 2026-07-19:** 本文记录 2026-06-04 的旧实现。当前
`seekRootContentStreamLike_0x6B6ADC` 与 `_rootStreamSource` 已删除，改为
`advanceRootContentStreamLike_0x6B6ADC` / `rewindRootContentStreamLike_0x6B9A3C`；
layer 亦按方向拆分，且不存在 pointer-identity reseed owner。

M1 advance-unit completion (2026-06-03, fresh decompile 0x6B6ADC). The binary
Player_advanceRootAndNodes runs 4 streams in fixed order at each advanceRoot
terminal point: [① layer → ② root → ③ var-track → ④ node]. The live port
(PlayerFrameProgress.cpp::frameProgress, models progress_inner @0x6C106C) now
drives all 4 at every advanceRoot/rewind equivalent point.

**STALE-DOC CORRECTION:** analysis/MotionPlayer_Restoration_Review_2026-06-02.md
§1.2 claimed stream③ (var-track) was "无 / DEFERRED" at the advance point. That
was already FALSE by 2026-06-03: `advanceVariableTracksLike_0x6B6ADC` (live
Player method, NOT the PlayerFrameStepping.cpp reference) was already called at
all 5 advance sites. Only stream② (root) was genuinely missing. Verified by
double grep (all root-stream fields had zero .cpp usages).

**Stream② (root content-snapshot) — what was added:**
- Source: motion["priority"] (Player+548), a FLAT frame-dict array [{time,content}]
  exactly parallel to motion["tag"] (+1072). NOT the same as the port's
  clipList decode (clipList treats priority entries as clips w/ layer[]; the
  binary's +548 stream is the raw frame array indexed priority[cursor]).
- Init seed (0x6B38FC): +616 = priority[0].content; cursor +568 starts 0.
- Root loop (0x6B6EE4..0x6B7124): forward-ONLY (rewindRootAndNodes 0x6B9A3C also
  only forward-advances +568 — NO reverse root scan). Per crossed frame:
  `if(+456 < +584) break; ++cursor; +616 = priority[cursor].content (sub_A0FB64
  variant copy); +576 = +584; +584 = priority[cursor+1].time`. NO event gate,
  NO "type" read (unlike layer stream).
- sub_A0FB64 = tTJSVariant copy-assign (type switch; case 1 object = OWORD copy +
  addref). Port copies the shared_ptr<PSBDictionary> = semantically equivalent.

**Files touched:**
- RuntimeSupport.h: added `std::shared_ptr<PSB::PSBList> priorityFrames` to
  MotionSnapshot (next to tagFrames).
- RuntimeSupport.cpp: ~line 1346 `snapshot->priorityFrames =
  dictionaryList(root, {"priority"})` (same site as tagFrames).
- Player.h: decl `seekRootContentStreamLike_0x6B6ADC(double)` + field
  `_rootStreamSource` (pointer-identity reseed guard, like _layerStreamSource).
  Root-stream state fields _rootFrameCursor(+568)/_rootCurTime(+576)/_rootNextTime
  (+584)/_rootContent(+616) ALREADY existed (declared-only since 砖5/洞3).
- PlayerFrameProgress.cpp: impl of seekRootContentStreamLike (mirrors
  seekLayerEventStreamLike minus the gate), + inserted the call at all 5
  advance points between seekLayerEventStreamLike and advanceVariableTracksLike.

**Inert for logo (non-regression guard):** logo priority arrays are single-clip
(count<2 → loop body never runs); root stream only snapshots +616 which no live
consumer reads yet (dead-data, like HM3). Pure data-flow restoration.

**Verification gap:** local LLDB motion tracer is BROKEN on this Mac
(debugserver can't attach: "com.apple.linkd.autoShortcut" errors,
breakpoint krkr2_lldb_motion_frame_begin never hit, motionTraceFrames:0). FAILS
IDENTICALLY at HEAD baseline with changes stashed — pre-existing env issue, NOT
caused by the change. So logo mismatch count could not be produced locally; CI
differential.yml (push to web/dev/*) validates. Both web/debug (248/248) +
wasmtime guest (krkr2_wasmtime_guest, 31/31) build green.

**STILL OPEN (not this task):** Stage B reseek three-stream
(Player_reseekTimelineCursors 0x6B86C8 carries its own scans w/ +920==+456
precise-frame gate, at the 2 loop-wrap reseek points 0x6C1488/0x6C1428); +616
content consumer (who reads root content snapshot — likely a render/eval path);
emote/non-emote progress dispatch split.
