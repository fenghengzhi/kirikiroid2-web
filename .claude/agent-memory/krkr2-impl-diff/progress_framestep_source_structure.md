---
name: progress-framestep-source-structure
description: SOURCE-STRUCTURE audit；2026-07-22 correction: decoded PlayerFrameStep/Stepping reference model was deleted; live raw path remains authoritative
metadata:
  type: project
---

**CURRENT CORRECTION 2026-07-22:** `PlayerFrameStep.*` 与
`PlayerFrameStepping.*` 的 decoded、test-only 重复端口及相应单测已删除。下文提到这些文件
时只表示历史状态；当前只有生产 raw `tTJSVariant`/`MotionNode` 路径。

2026-06-04 fresh-decompile audit (boundary/decomposition only, NOT value correctness). Binary addrs all verified this session.

**CURRENT CORRECTION 2026-07-19:** 下文的 `seekLayerEventStreamLike` /
`seekRootContentStreamLike` 双向 helper 与“活路径缺 reseek layer/root scan”描述已过时。
当前 `PlayerFrameProgress.cpp` 已按 `0x6B6ADC/0x6B9A3C` 拆成
`advanceLayerEventStreamLike_0x6B6ADC`、`rewindLayerEventStreamLike_0x6B9A3C`、
`advanceRootContentStreamLike_0x6B6ADC`、`rewindRootContentStreamLike_0x6B9A3C`；
`reseekTimelineCursors@0x6B86C8` 的 layer/root scan 也在 live path。下面保留的是
2026-06-04 历史审计，不得用旧符号或旧缺口描述当前源码。

CORE VERDICT: the port does NOT mirror the binary fn decomposition for frame-stepping. Three deviations:

1. SPLIT: binary has TWO monolithic single-fn stream-walkers, each handling all 4 streams (layer+1072 / root+548 / var-track+1312 / node-deque) inline:
   - Player_advanceRootAndNodes 0x6B6ADC (forward)
   - Player_rewindRootAndNodes  0x6B9A3C (backward)
   - Player_reseekTimelineCursors 0x6B86C8 (non-incremental reseek; tail = pruneHM3 0x6B9234 + sub_6B9650 aux-list)
   Live port SPLITS each into per-stream locals in PlayerFrameProgress.cpp, re-stitched by frameProgress branch ladder (lines ~1789-1925):
   seekLayerEventStreamLike_0x6B6ADC(:884, bidirectional) + seekRootContentStreamLike_0x6B6ADC(:1029) + advanceVariableTracksLike_0x6B6ADC(:1109)/rewindVariableTracksLike_0x6B9A3C(:1211)/reseedVariableTracksLike_0x6B86C8(:1312) + progressSeekNodeSlotsLike_0x6C106C (defined in PlayerUpdateLayerEval.cpp:548, NOT FrameProgress).

2. DEAD PARALLEL TRANSCRIPTION: PlayerFrameStepping.cpp ALSO has 1:1-faithful single-fn transcriptions advanceRootAndNodesLike_0x6B6ADC(:272)/rewindRootAndNodesLike_0x6B9A3C(:386)/reseekTimelineCursorsLike_0x6B86C8(:482)/advanceNodeFramesLike_0x6B7E44(:190). These have ZERO live callers — only tests/unit-tests/plugins/motionplayer-dll.cpp (lines 990-1132). Compiled (CMakeLists:23-24) but unreachable from progress entry. So each of 0x6B6ADC/0x6B9A3C/0x6B86C8/0x6B7E44 has TWO local counterparts: dead-faithful (FrameStepping) + live-split (FrameProgress+UpdateLayerEval). Invented redundancy.

3. MERGE: frameProgress (PlayerFrameProgress.cpp:1580) carries BOTH Player_progress_inner 0x6C106C AND the EmoteEngine controller bucket-loop 0x67D01C (while(remaining>0){fmin(1.1) type4/5/6/8/7 step}) — two distinct binary fns folded into one body (loop @:1678-1704). NOTE 0x530A5C=EmoteEngine_progress entry, 0x67D01C=its inlined body (same fn).

OTHER:
- Motion_VarTrackSlot_step_guess@0x6B786C 与 Motion_VarTrackSlot_merge_guess@0x6B7A70 是独立二进制 helper；2026-07-18 已纠正本地“3 份 lambda 内联”偏差，PlayerFrameProgress.cpp 现在也只有一份共享 step/merge helper，三条游标路径复用。
- sub_6D2A54 (6-line orchestrator: progress_inner->updateLayers->calcBounds->dispatchEvents): SPLIT into progressFramesLike_0x6D2A54(:1993, frame-unit, faithful) + progressMsLike_0x6D2A54(:1954, ms-unit INVENTED — fuses ms->frame clamp/convert that binary keeps in separate wrapper 0x6D2A98).
- Player_advanceNodeFrames 0x6B7E44 (per-node 2-slot ping-pong seek toward CHILD evalTime *(node+8)+40, calls parseFrame 0x6926B4): live counterpart is advanceNodeFrameSelectionLike_0x6926B4 in PlayerUpdateLayerEval.cpp (keyed to wrong addr 0x6926B4 = parseFrame, not 0x6B7E44). progressSeekNodeSlotsLike_0x6C106C also mis-suffixed (0x6C106C has no standalone node-seek fn).
- 1:1 clean: parseFrameLike_0x6926B4 (PlayerFrameStep.cpp:401 <-0x6926B4), mergeFrameContentLike_0x692AB0 (:140 <-0x692AB0), interpolateVarTrackValuesLike_0x6BBE20 (PlayerFrameProgress.cpp:1417 <-0x6BBE20, called from resetMotionState not per-frame), Player::getVariable (PlayerVariable.cpp:457 <-0x533E1C 2-branch router).

MISSING (binary fn, no live counterpart): 0x6B86C8 tail pruneHM3_byNodeIdentity(0x6B9234) + sub_6B9650 aux-list + Player_initNodeTimeline(0x6B9228) — reseedVariableTracksLike ports only the var-track reseed block (0x6B8F30). Event-gate placement (pushSync/pushAction) relocated out of the binary's in-loop position (port comments :879/:1781).

2026-06-04 B-vs-A convergence refinements (this session, all 6 addrs re-decompiled):
- RESEEK LAYER/ROOT SCAN IS MISSING FROM A (not just the tail). Binary 0x6B86C8 layer scan @0x6B8770 is a COARSE DOUBLE-INCREMENT (for-loop ++i AND body ++i @0x6B8830 on time<target — confirmed real) + INT-TRUNCATED +920/+928 (`(double)(int)`), intentional overshoot. A has NO such scan: at both reseek wrap points (FrameProgress.cpp:1797,:1862) A reuses the INCREMENTAL seekLayerEventStreamLike — different cursor trajectory, no overshoot/no int-trunc. Only B (FrameStepping.cpp:488-535) reproduces it. reseedVariableTracksLike_0x6B86C8 only covers the var-track block.
- advanceNodeFrames target = *(node+8)+40 (CHILD object eval time, [0x6B7E90]), NOT player+456 and NOT directly parameterEntry->value. A's frameSelectionTimeLike_0x6B7E44 (UpdateLayerEval.cpp:281) returns parameterEntry->value for param nodes — equivalence to child+40 UNVERIFIED (needs child-obj decompile). B models it as explicit childEvalTime field.
- rewind var-track merge targets DIFFER by direction: fwd (0x6B7178) merges slot[0] BOTH times; rev (0x6BA024) merges item+48 then item+104(slot[1]). A faithfully reflects this (advanceVariableTracksLike:1206-1207 both slot[0]; rewindVariableTracksLike:1307-1308 slot[0]+slot[1]).
- TESTABILITY: B's 4 fns ARE isolatable on lightweight mock structs (cursor-walk core depends only on scalar player fields + frame time/type/content). Test motionplayer-dll.cpp:983-1142 binds B's FREE-FUNCTION symbols — cannot re-point to A by rename (A = Player members on real _nodes/_clampedEvalTime). DELETING B BEFORE A grows (a) a standalone reseek coarse-scan member + (b) Player-constructible fixtures LOSES coverage of: reseek double-inc scan, int-trunc, empty-stream signed-limit trap, no-op early-out. Var-track + incremental layer/root could be re-covered via Player-level tests (A implements them live). Recommend: add reseek scan member (port B:488-578) + Player fixture FIRST, parity-check, THEN delete B.

setVariable 0x671228 = EmotePlayer (NOT motion::Player) -> EmoteEngine.cpp:1645 / EmotePlayer.cpp:376,802. Outside the 8-file Player frame core.
