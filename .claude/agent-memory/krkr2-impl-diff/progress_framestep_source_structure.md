---
name: progress-framestep-source-structure
description: SOURCE-STRUCTURE (fn boundary/decomposition) audit of progress/frame-stepping — binary monolithic stream-walkers SPLIT in live port + DEAD parallel transcription in PlayerFrameStepping.cpp
metadata:
  type: project
---

2026-06-04 fresh-decompile audit (boundary/decomposition only, NOT value correctness). Binary addrs all verified this session.

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
- sub_6B786C (var-track step, time->slot+8) & sub_6B7A70 (var-track merge type/interval/value/easing): standalone binary helper fns, NO standalone local fn — inlined into the 3 var-track locals (merge).
- sub_6D2A54 (6-line orchestrator: progress_inner->updateLayers->calcBounds->dispatchEvents): SPLIT into progressFramesLike_0x6D2A54(:1993, frame-unit, faithful) + progressMsLike_0x6D2A54(:1954, ms-unit INVENTED — fuses ms->frame clamp/convert that binary keeps in separate wrapper 0x6D2A98).
- Player_advanceNodeFrames 0x6B7E44 (per-node 2-slot ping-pong seek toward CHILD evalTime *(node+8)+40, calls parseFrame 0x6926B4): live counterpart is advanceNodeFrameSelectionLike_0x6926B4 in PlayerUpdateLayerEval.cpp (keyed to wrong addr 0x6926B4 = parseFrame, not 0x6B7E44). progressSeekNodeSlotsLike_0x6C106C also mis-suffixed (0x6C106C has no standalone node-seek fn).
- 1:1 clean: parseFrameLike_0x6926B4 (PlayerFrameStep.cpp:401 <-0x6926B4), mergeFrameContentLike_0x692AB0 (:140 <-0x692AB0), interpolateVarTrackValuesLike_0x6BBE20 (PlayerFrameProgress.cpp:1417 <-0x6BBE20, called from resetMotionState not per-frame), Player::getVariable (PlayerVariable.cpp:457 <-0x533E1C 2-branch router).

MISSING (binary fn, no live counterpart): 0x6B86C8 tail pruneHM3_byNodeIdentity(0x6B9234) + sub_6B9650 aux-list + Player_initNodeTimeline(0x6B9228) — reseedVariableTracksLike ports only the var-track reseed block (0x6B8F30). Event-gate placement (pushSync/pushAction) relocated out of the binary's in-loop position (port comments :879/:1781).

setVariable 0x671228 = EmotePlayer (NOT motion::Player) -> EmoteEngine.cpp:1645 / EmotePlayer.cpp:376,802. Outside the 8-file Player frame core.
