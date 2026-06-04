---
name: advancenodeframes-0x6B7E44-convergence
description: P7 convergence step 1/3 — Player_advanceNodeFrames @0x6B7E44 — RE-LANDED & GREEN 2026-06-05 via fix A (advanceNodeFramesLike delegates to the proven shared seek+tail advanceNodeFrameSelectionLike_0x6926B4 with events off + clampedEvalTime forwarded). The earlier standalone literal transcription (9f2a112) hung m2logo; root cause + fix in [[advancenodeframes-hang-rootcause]]. D-A1 resolution below still valid.
metadata:
  type: project
---

✅ STATUS 2026-06-05: RE-LANDED GREEN via fix A. advanceNodeFramesLike_0x6B7E44(node,
currentTime) now DELEGATES to advanceNodeFrameSelectionLike_0x6926B4(node, currentTime,
nullptr) — the proven shared seek+state-establish tail with per-node onAction suppressed
(the only binary diff between 0x6B7E44 and the inline non-param path). m2logo PASS 93 /
yuzulogo PASS 243 (local per-case xp3). Full root-cause diagnosis + the negative-index /
0.0-currentTime traps in [[advancenodeframes-hang-rootcause]].

⚠️ CORRECTION (the "ORACLE STATUS" line near the bottom was WRONG): logo is NOT
all-non-parameterized. m2logo DOES route at least one parameterized node ("レイヤ1"),
PROVEN by a guest proc_exit(99) on the first parameterEntry!=null node. The change is
therefore NOT inert for logo — the parameterized branch IS exercised by m2logo, which is
exactly why the buggy standalone transcription hung. Do not trust the old "INERT for logo"
claim below.

────────────────────────────────────────────────────────────
HISTORICAL (the standalone transcription attempt, REVERTED 2026-06-04):
⚠️ STATUS: REVERTED 2026-06-04 (commit 9e1b607). The advanceNodeFrames implementation
(committed in 9f2a112) caused m2logo to enter a non-terminating loop in CI → wasmtime job
timed out at 1230s on the m2logo case (yuzulogo passed, 243 frames). Bisect (reseek-only vs
+advanceNodeFrames, both with the CORRECT per-case xp3 reference/xp3/logo_test_oracle_m2logo.xp3)
confirmed advanceNodeFrames is the culprit: reseek-only → m2logo PASS 93 frames; +advanceNodeFrames
→ hang. ROOT CAUSE of the miss: the implementer verified locally with the runner's DEFAULT
--startup-xp3 (logo_test_oracle.xp3) which never exercises m2logo's real per-case path, so the
hang was invisible locally (see [[local-motion-playback-differential-per-case-xp3]]). The bug
itself (non-termination in the new advanceNodeFramesLike_0x6B7E44 forward/backward seek on
m2logo's parameterized node) is NOT yet diagnosed. The decompile facts + D-A1 resolution below
are still correct and reusable; only the implementation must be redone (and verified with the
per-case xp3 on BOTH cases) before re-landing.

P7 convergence step 1 of 3 (advanceNodeFrames → advanceRootAndNodes → rewindRootAndNodes) — implementation attempted 2026-06-04, REVERTED (see status above).

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

**ORACLE STATUS** [❌ PROVEN WRONG 2026-06-05 — see CORRECTION at top]: ~~logo (m2/yuzu) all NON-parameterized → parameterized branch never taken → change INERT for logo~~. FALSE: m2logo routes a parameterized node ("レイヤ1"), confirmed by guest proc_exit(99) on the first parameterEntry!=null node. The parameterized branch IS exercised by m2logo and the standalone transcription hung there. The non-param branch IS byte-identical to green, but the PARAM branch is not inert.
**PRE-EXISTING REGRESSION (NOT MINE)**: m2logo differential FAILs `has 100 frames; spec requires exactly 93` (oracle=93). REPRODUCED ON CLEAN HEAD f4cdc66 (stashed ALL wip incl. mine → still 100). So the 93→100 frame-count drift predates this session — almost certainly d74f41e (completionType +1144 / preview +1092 untangle, directly governs motion-completion → frame count) or f4cdc66. My change produces the SAME 100-frame trace as clean baseline = does NOT move the count. Fixing the completion-gating regression is OUT OF SCOPE for this convergence; flagged to user.

**STEP 2 DONE & GREEN (2026-06-05, commit 27a3b08)**: advanceRootAndNodes @0x6B6ADC +
rewindRootAndNodes @0x6B9A3C extracted as real function boundaries
(Player::advanceRootAndNodes_0x6B6ADC / rewindRootAndNodes_0x6B9A3C in
PlayerFrameProgress.cpp). The 4-stream sequence [layer ① seekLayerEventStreamLike →
root ② seekRootContentStreamLike → var-track ③ advance|rewindVariableTracksLike →
node ④ progressSeekNodeSlotsLike] was inlined at 5 advance/rewind-equivalent call points
(fwd 0x6C13D4/0x6C13F8/0x6C1468, rev 0x6C117C/0x6C138C/0x6C1408); now one call per point +
deltaTime-sign dispatch in the common tail. Pure behavior-preserving extraction (streams
already ported separately); m2logo 93 / yuzulogo 243 green. Blueprint: agent a027b3f7.

**STEP 3 DONE & GREEN (2026-06-05, commit pending)**: 3a + 3b both landed.
EMPIRICAL BASIS (proc_exit probes, m2logo + yuzulogo): (i) logo NEVER loop-wraps —
reseekTimelineCursors is never called (exit(88) in it never fired for m2logo); (ii) the
conflated seek's corrective-backward loop NEVER fires for either logo case (exit(77) in the
loop body never fired). So removing the corrective-backward from the forward path (3b) is
PROVABLY logo-identical (the removed loop never executed — empirical, not assumed, unlike the
step-1 "inert" miss), and the @0x6B91B0 re-seed (3a) is logo-inert (reseek uncalled). Both
implemented faithfully per CLAUDE.md (decompile evidence + non-regressing build); m2logo 93 /
yuzulogo 243 green.
— 3a: reseekNodeTimelineSlotsLike_0x6B91B0 (PlayerUpdateLayerEval.cpp) added + called in
reseekTimelineCursors STEP 4 (PlayerFrameProgress.cpp), closing the documented gap.
— 3b: advanceNodeFrameSelectionLike_0x6926B4 took (bool doForward, bool doBackward); two
inline-seek boundaries advanceNodeFrameForwardInlineSeekLike_0x6B73DC (true,false) /
advanceNodeFrameBackwardInlineSeekLike_0x6BA1CC (false,true) (PlayerInternal.h);
progressSeekNodeSlotsLike_0x6C106C took (bool forward); advanceRootAndNodes → forward,
rewindRootAndNodes → backward. Param nodes still advanceNodeFramesLike (both dirs, no events)
— preserves the step-1 fix (m2logo's param "レイヤ1" passed 93). CAVEAT kept: reseek node
range uses `i < _nodes.size()` (proven walk range) vs binary `m < dequeSize-1`; logo-inert
(reseek uncalled), documented in code.

**STEP 4 DECISION (2026-06-05): KEEP the PlayerFrameStepping.cpp mock — do NOT retire.**
The unit tests (tests/unit-tests/plugins/motionplayer-dll.cpp) exercise the MOCK
(motion::detail::advanceNodeFramesLike_0x6B7E44 / advanceRootAndNodesLike_0x6B6ADC /
rewindRootAndNodesLike_0x6B9A3C / reseekTimelineCursorsLike_0x6B86C8) on SYNTHETIC
NodeFrameStreamsLike seeds with NO motion file. The live functions need a full Player + loaded
motion (PSB) fixtures, which do NOT exist for these synthetic stepping scenarios. Per CLAUDE.md
(no fabricating fixtures), retiring the mock would LOSE real synthetic unit coverage with no
ready replacement → net negative. The live functions now supersede the mock's role IN THE LIVE
PATH (all 3 convergences + 3a/3b are live), but the mock stays as standalone synthetic unit
coverage. Genuine fixture-backed migration = a separate task only if motion fixtures for these
cases materialize.

HISTORICAL REMAINING (now resolved above):
— **3a: port reseekTimelineCursors node-init loop @0x6B91B0** (PREREQUISITE for 3b). Local
reseekTimelineCursors (PlayerFrameProgress.cpp STEP 4 ~line 1673) DEFERS @0x6B91B0,
relying on the corrective-backward sub-loop in advanceNodeFrameSelectionLike to reposition
node slots after loop-wrap. Binary @0x6B91B0 (agent af0adcbd) =
`for(m=1; m<dequeSize-1; ++m) Player_initNodeTimeline(player, node[m])` — ABSOLUTE two-slot
re-seed (= local initializeNodeTimelineSlotsLike_0x6B64AC): parseFrame slot[0]=frame(v19) +
slot[1]=frame(v19+1) (v19=min(scan(target),count-2)), merge both, activeSlotIndex(+1392)=0,
seeded(+44)=1. selection target = (*(node+8)) ? *(node+8)+40 : player+456 (per-node param
rule, inside 0x6B64AC @0x6B6500). Tail 0x6B9234 pruneHM3 / 0x6B9650 aux-list = housekeeping,
inert on node slots → keep DEFERRED. CAVEATS: (a) binary loop range `m < dequeSize-1` may
skip the last node vs the proven progressSeekNodeSlots `i < nodes.size()` — resolve the
off-by-one (libstdc++ deque trailing-iterator) before trusting; (b) adding absolute re-seed
SUPPRESSES the corrective-backward's per-frame onAction fires at loop-wrap (oracle-inert per
[[project-motion-event-path-ci-blindspot]] but a real behavioral delta); (c) logo may not
even loop-wrap (then 3a is logo-inert/unverifiable — implement faithfully + note gap, do NOT
fabricate a looping fixture).

— **3b: split inline seek to its 0x6B73D0 boundary** (needs 3a first). Binary forward inline
(0x6B73DC, in advanceRootAndNodes) = forward-only + shared tail; reverse inline (0x6BA1CC, in
rewindRootAndNodes) = backward-only + shared tail; both call Player_parseFrame@0x6926B4
(= populateClipSlotFromFrameLike, NOT advanceNodeFrameSelectionLike — naming was confused).
advanceNodeFrames@0x6B7E44 (param, both dirs) = forward+corrective-backward. Local
advanceNodeFrameSelectionLike conflates forward+corrective-backward+tail+events and is shared
by both directions; its corrective-backward STANDS IN for @0x6B91B0 — so the literal split
(forward-only fwd / backward-only rev, removing corrective-backward) is only safe AFTER 3a
re-seeds node slots at loop-wrap. HIGH exposure (most logo nodes are non-param). The three
binary seeks share a FIELD-IDENTICAL tail (0x6B7FB4 / 0x6B72BC / 0x6BA288: node+44=1 →
2×mergeFrameContent(node+346/+882 gates) → gated findSource(node+200, slot+356/+348)) →
extract a shared establishNodeStateAfterSeekLike when splitting.

— **4: retire PlayerFrameStepping.cpp mock + migrate unit tests** — the explicit "lead
handles after all 3 convergences" follow-up. Needs the live functions
(advanceNodeFramesLike / advanceRootAndNodes / rewindRootAndNodes) unit-testable so
PlayerFrameStep.cpp tests can target them instead of the mock.

HISTORICAL (pre step-2): ~~advanceRootAndNodes @0x6B6ADC ... rewindRootAndNodes @0x6B9A3C~~
(DONE). The non-parameterized inline per-node seek still lives in
advanceNodeFrameSelectionLike_0x6926B4 (not yet split to its 0x6B73D0 boundary — see 3b).
B's PlayerFrameStepping.cpp mock is the 1:1 reference for unit-test migration (see 4).
