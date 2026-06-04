---
name: reseek-timeline-cursors-done
description: Player::reseekTimelineCursors (0x6B86C8) full re-seek ported to LIVE Player + wired into FrameProgress wrap-points
metadata:
  type: project
---

Player::reseekTimelineCursors(double targetTime) — full non-incremental re-seek of all timeline cursors, ported onto the LIVE Player @ PlayerFrameProgress.cpp (decl Player.h after reseedVariableTracksLike_0x6B86C8). Aligned with libkrkr2.so Player_reseekTimelineCursors @0x6B86C8.

**Why:** the two loop-wrap reseek points (FrameProgress fwd ~1797 / rev ~1862) previously modelled ONLY reseedVariableTracksLike_0x6B86C8 + node walk, SKIPPING the layer coarse re-scan (+916/+920/+928) and root re-seek (+568/+576/+584/+616) that 0x6B86C8 performs. Now route through the real function boundary.

**0x6B86C8 structure (verified fresh):**
1. LAYER coarse scan @0x6B8770 over motion["tag"] (player+1072): from-scratch linear scan with DOUBLE-INCREMENT (loop ++i AND body ++i when time<target) → coarse overshoot; cursor(+916)=min(i,count-2); curTime(+920)/nextTime(+928) INT-TRUNCATED ((double)(int)time). Gate keyed on CURSOR frame (not cursor+1): if curTime==target && type==1: +1093(_speed) align(+483=1, snap +456/+1120) / sync(+1098=1, snap, pushSync); ungated action→pushAction.
2. ROOT scan @0x6B8C1C over motion["priority"] (player+548): SINGLE-step (no double-inc); cursor(+568)=min(j,count-2); +616 content snapshot (sub_A0FB64); curTime(+576) NOT int-truncated; nextTime(+584). min-clamp + snapshot run UNCONDITIONALLY (outside the if(count) block); count==0 → j=*(+568).
3. VAR-TRACK reseed @0x6B8F30 → REUSED reseedVariableTracksLike_0x6B86C8 (byte-identical, don't re-port).
4. NODE init loop @0x6B91B0 (Player_initNodeTimeline @0x6B64E4) → caller keeps progressSeekNodeSlotsLike_0x6C106C right after (fills node+320/+856), not duplicated.
5. TAIL @0x6B9234 pruneHM3_byNodeIdentity + @0x6B9248 sub_6B9650 aux-list (player+280) → DEFERRED (no live consumer).

**Call sites confirmed via 0x6C106C decompile:** fwd wrap = 0x6C1488 (+456=+1136 then reseekTimelineCursors), rev wrap = 0x6C1428 (+456=+1128 then reseekTimelineCursors). Both call 0x6B86C8 (NOT just var-track reseed). firstFrame seed 0x6C10E0/0x6C131C also calls it (FrameProgress ~1624 still uses reseedVariableTracksLike only — that path is the firstFrame _queuing branch; left as-is per scope, only the 2 wrap-points rewired).

**How to apply:** layer coarse scan ≠ seekLayerEventStreamLike_0x6B6ADC (that's the incremental advance/rewind form). Modelled on B's PlayerFrameStepping.cpp:482-600 reference, ported to real Player fields (_layerFrameCursor/_layerCurTime/_layerNextTime/_rootFrameCursor/_rootCurTime/_rootNextTime/_rootContent). Gate reuses live _speed/_motionCompleted/_clampedEvalTime/_frameTickCount/_syncWaiting/_pendingEvents. int-truncation of layer curTime/nextTime is LOAD-BEARING (binary uses propGetInt, root uses propGetDouble).

**Verify:** web debug + krkr2_wasmtime_guest both build clean (PlayerFrameProgress.cpp already in platforms/wasmtime/CMakeLists.txt:22, no new file). 2026-06-04 verification note (TWICE-corrected): (1) the reseek agent's original "logo differential PASS m2logo(93f)" was a misreport — it did not run a real local differential. (2) A first correction here then WRONGLY claimed "differential RED / pre-session regression (d74f41e)" based on a LOCAL run reporting m2logo=100. That too was wrong: CI run 26944928172 (f4cdc66) is GREEN — `motion-playback-compare` reports port 93 == oracle 93, 0 mismatches. The 100-frame result is a LOCAL macOS-arm64 environment divergence ONLY (see [[local-motion-playback-differential-unreliable]]), NOT a code regression. So: committed code is correct, CI is the authoritative guard, and the local motion_playback frame count must NOT be used as a green/red oracle on this machine. This reseek change is logo-INERT by construction (logo doesn't loop → wrap-points 0x6C1488/0x6C1428 never reached); its real verification is CI's motion-playback-compare after push. No wrap/loop fixture (honest verification gap).
