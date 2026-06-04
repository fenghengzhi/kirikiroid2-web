---
name: advancenodeframes-0x6B7E44-convergence
description: P7 convergence step 1/3 — faithful Player_advanceNodeFrames @0x6B7E44 (parameterized-node 2-slot ping-pong seek) ported onto real MotionNode, routed via progressSeekNodeSlotsLike node+8 split
metadata:
  type: project
---

P7 convergence step 1 of 3 (advanceNodeFrames → advanceRootAndNodes → rewindRootAndNodes) DONE 2026-06-04.

**What 0x6B7E44 IS**: the binary's per-node 2-slot ping-pong frame seek for PARAMETERIZED nodes only. In Player_advanceRootAndNodes (0x6B73B0, LABEL_86 node loop) the caller branches: `if (*(node+8)) { Player_advanceNodeFrames(node,player); continue; }` (LABEL_104 @0x6B73B4/0x6B73D4) else inline 2-slot seek WITH per-node onAction push (0x6B73D0..0x6B7338, push @0x6B74E4). So advanceNodeFrames = parameterized path (NO events); inline = non-parameterized path (fires onAction).

**D-A1 RESOLVED (child+40 == parameterEntry->value, CONFIRMED EQUAL)**:
- node+8 is NOT a child Player. Player_initNodeFields (0x6B3EA0): reads PSB "parameterize"; if variant type==4 (int) → node+8 = playerParamTable[idx] (56-byte stride, `v11=v10+56*idx` @0x6B3E90, `*(node+8)=v11` @0x6B3EA0); else node+8=0.
- offset +40 of the 56-byte param entry = the eased VALUE. sub_6B1718 (param-entry builder) field map: +0 id, +8 discretization, +16 rangeBegin, +24 rangeEnd, +32 rangeScale/division, **+40 value** (`*(double*)(v6-16)=v16` @0x6B19E4), +48 mode(=0).
- Cross-check Player_initNodeTimeline (0x6B64AC @0x6B6500): `v7 = (*(node+8)) ? (double*)(*(node+8)+40) : (double*)(player+456)` — parameterized→entry+40, else player+456 (clampedEvalTime). IDENTICAL selection-time rule to frameSelectionTimeLike_0x6B7E44.
- Local MotionParameterEntry maps entry+40 → ::value (RuntimeSupport.h). So `*(node+8)+40 == node.parameterEntry->value == frameSelectionTimeLike_0x6B7E44(parameterized)`. No behavior change.

**What I built**: `motion::internal::advanceNodeFramesLike_0x6B7E44(MotionNode&)` in PlayerUpdateLayerEval.cpp (~line 482), declared PlayerInternal.h (~line 1480, next to advanceNodeFrameSelectionLike_0x6926B4). Reproduces 0x6B7E44 1:1 via curIdx/otherIdx int swap (= binary cur/other pointer swap) operating on node.slots[2] (=node+320/+856), driving populateClipSlotFromFrameLike_0x6926B4 (live ClipSlot parse = binary parseFrame+auto-merge). ClipSlot.clipStartTime == binary slot+8 "time" (the seek comparison field). limit=count-2 SIGNED (negative→inert for empty). Merge gate (node+346/+882) + findSource subsumed by live parse + read-pass refreshSourceGeometryFromSourceName (the established two-pass hoist). NO events (binary parameterized path has none).
- INIT GUARD added at top: binary advanceNodeFrames has NO init (relies on prior Player_initNodeTimeline 0x6B64AC seed via reseekTimelineCursors node-init loop @0x6B91B0). Live reseek defers that seed to the node walk, so when both slots frameIndex<0, call initializeNodeTimelineSlotsLike_0x6B64AC first (same selection time).

**Routing**: progressSeekNodeSlotsLike_0x6C106C (PlayerUpdateLayerEval.cpp) now mirrors the binary node+8 split: `if(node.parameterEntry != nullptr){ advanceNodeFramesLike_0x6B7E44(node); continue; } advanceNodeFrameSelectionLike_0x6926B4(node, t, &_pendingEvents);`. Previously ALL nodes rode the conflated split helper gated only by selection time + event-nullness.

**Build**: web/debug + krkr2_wasmtime_guest both clean. No .cpp added/removed (edits only) → wasmtime source list untouched.

**ORACLE STATUS**: logo (m2/yuzu) all NON-parameterized → parameterized branch never taken → change INERT for logo (provable: non-param branch is byte-identical to old `advanceNodeFrameSelectionLike_0x6926B4(node, t, &_pendingEvents)`). NO parameterized-node fixture exists (honest verification gap; do NOT fabricate).
**PRE-EXISTING REGRESSION (NOT MINE)**: m2logo differential FAILs `has 100 frames; spec requires exactly 93` (oracle=93). REPRODUCED ON CLEAN HEAD f4cdc66 (stashed ALL wip incl. mine → still 100). So the 93→100 frame-count drift predates this session — almost certainly d74f41e (completionType +1144 / preview +1092 untangle, directly governs motion-completion → frame count) or f4cdc66. My change produces the SAME 100-frame trace as clean baseline = does NOT move the count. Fixing the completion-gating regression is OUT OF SCOPE for this convergence; flagged to user.

**STILL OPEN (steps 2/3)**: advanceRootAndNodes @0x6B6ADC (the 4-stream forward walk wrapping advanceNodeFrames; layer/root/var-track streams already separately ported as seek*StreamLike) and rewindRootAndNodes @0x6B9A3C (reverse). The non-parameterized inline per-node seek still lives in advanceNodeFrameSelectionLike_0x6926B4 (not yet split to its own 0x6B73D0 boundary). B's PlayerFrameStepping.cpp mock advanceNodeFramesLike_0x6B7E44 (on NodeFrameStreamsLike) is the 1:1 reference; lead handles its deletion + unit-test migration after all 3 convergences.
