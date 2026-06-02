---
name: eye-deque4-vertical
description: M2 EmoteEngine eye/deque#4 vertical DONE — EmoteBlinkController(0x170) model+ctor(0x662968)+step(sub_663BDC)+builder(0x66C77C)+MT19937 RNG(9F1A08/9F17D0). Corrects 2 memory errors. Wired via PlayerCore loadFromSnapshot.
metadata:
  type: project
---

# Eye / deque#4 vertical (M2) — IMPLEMENTED 2026-06-03

Files added: `cpp/plugins/motionplayer/EmoteBlinkController.{h,cpp}`,
`EmoteBlinkRng.{h,cpp}`. Touched: EmoteEngine.{h,cpp}, PlayerCore.cpp,
both CMakeLists (motionplayer + platforms/wasmtime). Built green web/debug +
krkr2_wasmtime_guest. No oracle/fixture (blink RNG is wall-clock seeded →
non-deterministic by design); verification = build + line-by-line decompile audit.

## Function → local map
- `EmoteBlinkController_ctor @0x662968` → `EmoteBlinkController_ctor` (EmoteBlinkController.cpp)
- `sub_663BDC` (eye step) → `EmoteBlinkController_step`
- `EmoteEngine_buildEyeControl @0x66C77C` → `EmoteEngine::buildEyeControl(const PSB::PSBList*)`
- `EmoteEngine_applyMetadata @0x67D4D0` (eye branch only) → inlined in PlayerCore.cpp loadFromSnapshot
- `sub_9F1A08`/`sub_9F17D0` (blink MT19937 + canonical-real) → `EmoteBlinkRng_get`/`EmoteBlinkRng_next`
- progress deque#4 step loop @0x67d0a4 → EmoteEngine.cpp progress() (replaced STUB_WARN)

## TWO MEMORY CORRECTIONS (prior notes were WRONG)
1. **eyeControl lives under `metadata`, NOT `metadata["base"]`.** EmoteObject_init
   @0x67DBAC: `base = metadata["base"]` is read ONLY for chara/motion (0x67dd6c).
   The builder dispatch `applyMetadata(engine, v28)` is called with v28 = COPY of
   the FULL `metadata` dict (0x67dfa0), and applyMetadata reads
   eyeControl/variableList/mirror/scale straight off it. The
   EmoteEngine_controller_deque_builder.md "base metadata" framing is misleading.
2. **HM#6 @+1384 value is `{int32 type; int32 index}` (EmoteVarRef), NOT a
   `double`/scalar.** buildEyeControl writes `*ret=4; ret[1]=loopIndex`
   (0x66ca30). Retyped `_scalarHM6_1384` from EmoteScalarMap to EmoteVarRefMap.
   The EmoteEngine.h TODO(P-B) `double` placeholder for HM#6 was a guess; now
   verified by the builder write. (HM#1/2/4/5 value width still un-reversed.)

## EmoteBlinkController field table (0x170=368B, decompile-verified)
- +0..79 valueTrack12B (EmoteAngleController 12B-elem deque; ctor 0x6629b8)
- +80..159 valueTrack8B (8B {float,float} deque; ctor sub_6827A8 @0x6629dc)
- +160 edgeTable vector<pair<float,float>> (PSB "edge" array; ctor 0x662cac)
- +184 nodeRows deque<vector<float>> (PSB "node" array; ctor sub_6828FC @0x662a04)
- +288 trackResolvedSpan (WRITTEN by sub_661F7C; read into +312 at track-setup)
- +296 trackState(0/1/2) +300 trackValue(=beginFrame) +304 target +308 dir
  +312 span +316 accum +320 invDur +324 pow
- +328 beginFrame(int) +332 endFrame(int) +336 blinkPhase(0/10/11/12)
- +340 blinkIntervalMin +344 blinkIntervalMax +348 blinkFrameCount
- +352 blinkTimer(=min+(max-min)*rand) +356 blinkPos(=beginFrame) +360 blinkEnabled(byte)

## deque#4 element = {EmoteBlinkController* ctl; ttstr label} (16B)
progress loop @0x67d0a4: `sub_663BDC(*v15,&out,step); HM7[v15+1]=out; v15+=2`.
Confirms 16B stride, ctl@+0, label@+8. (Corrected EmoteEngine.h:179 `char raw[16]`.)

## SCOPE BOUNDARY (open, documented in code)
`sub_661F7C @0x661F7C → sub_660028` is a **1925-line** edge-table node-value-row
mesh resolver that rebuilds valueTrack8B from edgeTable+nodeRows. NOT ported —
call site kept as commented anchor in EmoteBlinkController_step. Without it the
value-track interpolation is inert (trackValue holds the popped position) but the
blink state machine + final remap are fully faithful. This is the eye's mesh-row
evaluation engine — its own future vertical.

## RNG (sub_9F1A08 / sub_9F17D0)
Process-global MT19937 (libstdc++ flavor), seed = steady_clock::now()/1000000
(sub_A2BDBC). Init recurrence quirk: `mt[i]=1812433253*(mt[i-1]^(mt[i-1]>>30)) +
(i+1)` (NOT standard `+i`; the +(i+1) is from the v2-2 additive with mt[] at
QWORD index 3..626). next() draws TWO words → double in [1,2) via mantissa
(low=word1 32b | high top-20b) then -1.0 → [0,1). NOT the repo's
tTJSMersenneTwister (different seeding). Faithfully hand-ported in EmoteBlinkRng.cpp.

## Wiring point
PlayerCore.cpp `Player::loadFromSnapshot` after activateMotion (mirrors
EmoteObject_init calling applyMetadata after Player_play). Reads
root["metadata"]["eyeControl"] PSBList → buildEyeControl. Clears+deletes prior
deque#4 controllers first (fresh-build semantics; binary always runs on a new engine).

## Remaining OPEN (this vertical only did eye)
eyebrow(type5/0x66CB9C)/mouth(type6)/transition(type7)/selector(type8)/timeline
builders; bust/hair/parts physics; sub_661F7C/sub_660028 mesh resolver;
deque#5/6/8/9/10 step fns still STUB_WARN; setVariable@0x671228 reader-side
dispatch into deque#4 by HM6 index (sub_6638B0) not yet wired to new model.
