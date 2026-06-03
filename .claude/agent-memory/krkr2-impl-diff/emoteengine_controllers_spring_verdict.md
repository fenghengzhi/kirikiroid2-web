---
name: emoteengine-controllers-spring-verdict
description: 2026-06-03 fresh-decompile audit of EmoteEngine progress + 7 controllers + spring/rng vs libkrkr2.so. Two REAL open gaps (sub_67C8A8 mislabeled as boundary; sub_661F7C mesh resolver unported). Everything else faithful.
type: project
---

EmoteEngine + Emote controller/spring subsystem audit (fresh decompile 2026-06-03).

**Faithfully ported (verified line-by-line this round):**
- EmoteEngine_progress 0x67D01C — 6 deque step loops + var-track HM7 bind-loop + Player progress + physics gate. ALIGNED.
- setVariable 0x671228 (=EmotePlayer::setVariable; disjoint HM@+1384/+1392/+1440 + 5 handlers case4-8). ALIGNED.
- Blink/eye step sub_663BDC (IDA name EmoteVarController4_step_guess is misleading — body IS the blink controller, switch+336 phases 0/A/B/C + RNG + final remap). ALIGNED, incl float-num/double-div remap mix.
- Eyebrow step 0x665600, Mouth step 0x666068, Selector step 0x668470 + applySelection 0x6680B0 + Animator_setKeyframes 0x667330. ALIGNED (selector guard order matches: state||queue||fabs>=1e-7).
- Loop sampler inline 0x67d2a0 -> EmoteLoopController_step. ALIGNED.
- Angle 0x666634, Var 0x666BF8/EmoteVarController_step 0x666c0c. ALIGNED (count floats not count*4; raw-bit powCount).
- EmotePhysics_springStep 0x662768, EmoteBustChainSpring_step 0x6689A4, EmoteEngine_stepBust 0x67BCE8, stepHairParts 0x67B748 — all CALLED from local progress (not stub). ALIGNED.
- EmoteBlinkRng sub_9F1A08/sub_9F17D0. ALIGNED.

**REAL OPEN GAPS (not platform boundaries):**
1. **sub_67C8A8 @0x67C8A8 MISLABELED as "PLATFORM_BOUNDARY: stub" in EmoteEngine.cpp:1963-1964.** It is a REAL ~150-line paired-parameter binder: walks deque@+62 (40B-stride entries = local opaque EmoteSetupEntry40B_Deque7 placeholder, EmoteEngine.h:237), reads 2 HM2 values/entry, runs sub_67C560 on both, does a 2D circular-disc remap (atan2/sin/cos, modes *(int)v2==0/1), then 2x Player_bindParameterValue(+24,+32). Called in binary progress @0x67d3f8 BETWEEN bind-loop and sub_6D2A54. Local progress does NOT call it. The "stub, no live consumer" comment is WRONG. Fix needs: parse/populate the 40B deque#7 entries + port the remap+bind.
2. **sub_661F7C @0x661F7C (mesh resolver, dispatches sub_660028 ~1925 lines) NOT called** in EmoteBlinkController.cpp:255 and EmoteEyebrowController.cpp:221 (commented-out anchor). Consequence: valueTrack8B never repopulated, trackResolvedSpan(+288) stays 0, eye/eyebrow track interp inert. Labeled "SCOPE BOUNDARY" in headers — defensible as a separate large vertical, but it IS a functional gap, not a platform boundary.

**Architecture wins:** controllers use std::deque/std::vector matching binary container SELECTION (deque-of-blocks); raw-bit powCount memcpy faithfully reproduced everywhere; spring uses raw byte-offset F(off)/I(off) accessors mirroring +12*seg / +8*seg strides 1:1.
