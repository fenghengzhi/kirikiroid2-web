---
name: progress-routing-engine-g2c-live
description: Capstone DONE 2026-06-03 — EmotePlayer/D3DEmotePlayer progress now route through engine().progress (EmoteEngine_progress 0x67D01C); step-7 Player progress added; G2-C bind-loop runtime-live. Units trap (ms vs frames) resolved.
metadata:
  type: project
---

# Progress routing through engine().progress — G2-C bind-loop made LIVE (2026-06-03)

## What changed (capstone: shim-removal disjoint-map now FUNCTIONALLY works)
- **EmoteEngine::progress (EmoteEngine.cpp ~1982)**: ADDED step 7
  `player().progressFramesLike_0x6D2A54(originalDt)` AFTER the G2-C bind-loop,
  BEFORE the bust/hair physics gate — exactly @0x67d408. Was a comment-only stub.
- **D3DEmotePlayer::progress (EmotePlayer.cpp ~589)**: was
  `player().progressMsLike_0x6D2A54(dt)` (bypassed engine) → now
  `engine().progress((float)dt)`. D3DEmotePlayer::pass now `{ progress(dt); }`.
- **EmotePlayer::progress (EmotePlayer.cpp ~706)**: was direct progressMsLike →
  now `engine().progress((float)(dt * 60.0/1000.0))` (sub_6818B4 ms→frame prologue).
- **NEW Player::progressFramesLike_0x6D2A54 (PlayerFrameProgress.cpp + Player.h)**:
  faithful RAW sub_6D2A54 (frame-units, NO *60/1000). _pendingEvents.clear() +
  frameProgress(frameDt) + updateLayers(node-gated) + calcBounds + clear.

## DECISIVE UNITS TRAP (resolved — the whole reason for a new method)
Three progress entry shapes, DIFFERENT unit conventions, all confirmed by decompile:
- **Motion.Player.progress** = Player_progressCompat@0x6D2A98: does
  `Player_progress_inner(v8, v10 * 60/1000)` INLINE (ms→frame), then
  updateLayers/calcBounds/dispatchEvents. Does NOT call sub_6D2A54. UNTOUCHED — logo path.
- **D3DEmotePlayer.progress** NCB callback = **raw EmoteEngine_progress@0x67D01C**
  (registration @0x52f76c, this=EmoteEngine). NO ms→frame conversion — dt arrives
  in FRAME units. Passes same frame-dt to sub_6D2A54.
- **EmotePlayer.progress** NCB = EmotePlayer_progress_sub_6818B4@0x6818B4: inlined
  copy of 0x67D01C with ONE prologue diff `a2 = a2*60/1000` @0x6818c8 (ms→frame),
  then identical engine body.
- **sub_6D2A54(player,0,frameDt)@0x6D2A54** (the step-7 callee): tiny 6-liner.
  `player+16=0; Player_progress_inner(player,frameDt); updateLayers; calcBounds;
  dispatchEvents; player+16=0`. frameDt is ALREADY frames (progress_inner does
  `+592 = speedMul * a2`, no further *60/1000).
- TRAP: local `progressMsLike_0x6D2A54(deltaMs)` is MISLABELED — it does
  `frameProgress(deltaMs * kMotionFramesPerMillisecond)`, i.e. it's the
  ms-CONVERTING wrapper body, NOT raw sub_6D2A54. Calling it from step-7 with a
  frame-dt would re-scale by 0.06 (double-not-convert). Hence the new
  progressFramesLike_0x6D2A54 (frame-units) for step 7. progressMsLike kept for
  any genuine ms callers (none remain in cpp/ after this change).

## NCB "pass" is NOT progress (confirmed)
D3DEmotePlayer NCB "pass" → D3DEmotePlayer_addPlayCallback@0x52f730 (play-callback
setter). Local NCB already binds pass→addPlayCallback (main.cpp:943, faithful). The
C++ D3DEmotePlayer::pass(double) method is a LOCAL convenience (only unit-test caller
l700); routed through progress() so it exercises the bind-loop.

## Verification
- web/debug + wasmtime krkr2_wasmtime_guest BOTH GREEN.
- logo motion_playback wasmtime: m2logo(93f)+yuzulogo(243f) BOTH PASS bit-identical
  (logo uses Motion.Player.progress = progressCompat, untouched → non-regression).
- ROUND-TRIP: structurally connected (set→engine.progress→bind-loop→get) but the
  macos unit-test fixture SIGSEGVs PRE-EXISTING @l622 (rm.load/module, fixture-load,
  BEFORE any progress code) so the round-trip is runtime-UNOBSERVABLE here. Also HM7
  chain(+1456) is empty on logo (no controller/variable metadata) → bind-loop inert.
  Per CLAUDE.md: structurally faithful, fixture-inert, NOT fabricated. The disjoint
  assertion (set then get WITHOUT progress != value) still holds + kept.
- 3 OTHER macos test failures (l519 resource-chain, l572 findSource, l824
  layerNames==0) are PRE-EXISTING in the uncommitted Gap-tree: all driven by
  motion::Player resource/layer-build paths I never touched, + harness warning
  "failed to create Math.RandomGenerator" (env/fixture gap). NOT my change.

## FALSIFIED + corrected in-place
- setvariable_shim_removal_done.md "NEW GAP" (D3DEmotePlayer.progress bypasses
  engine, bind-loop runtime-dead) — NOW RESOLVED by this task. The gap is closed.
- motionplayer-dll.cpp:664-670 comment ("pass calls progressMsLike directly,
  bypassing engine bind-loop, separate task") — FALSIFIED, corrected to reflect
  the live wiring.

## Still OPEN (unchanged by this task)
- sub_67C8A8 (step 6) + sub_6687E8/player+1544 (step 4 wind) still PLATFORM_BOUNDARY
  stubs (not reversed). Player_preProgress (step 1) documented stub.
- HM2-clear-timing question: local frameProgress clears _evalResultValues(HM2) at
  entry; binary Player_progress_inner@0x6C106C does NOT clear +320. Since step-7
  progress runs AFTER the bind-loop, the local HM2 clear could wipe bound values
  before getVariable. PRE-EXISTING, orthogonal to routing, NOT patched (CLAUDE.md).
