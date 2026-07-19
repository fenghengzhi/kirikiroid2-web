// EmoteMouthController — 0x70=112B controller for the emote "mouth" category
//   (deque#6, TYPE 6). Aligned with libkrkr2.so:
//     ctor : EmoteMouthController_ctor_guess @ 0x665C98
//     step : sub_666068 (EmoteMouthController_step) @ 0x666068
//
// This is the SMALLEST of the three "control" leaf controllers ported so far
// (0x70 vs the eye's 0x170 and the eyebrow's 0x150). Despite the name "mouth"
// (which would suggest lipsync / audio-volume driving), the binary's step is
// purely a value-track scalar ramp: it has NO blink machine, NO RNG, NO 8B
// secondary track, NO edge/node mesh tables, and does NOT read any audio. It is
// the same shape as EmoteAngleController (0x70, the same 12B-elem deque base
// @+0..+79 followed by a compact scalar machine @+80..+108), but the scalar
// machine has DIFFERENT field offsets/semantics than EmoteAngleController and a
// DIFFERENT step (sub_666068 vs the angle step sub_666634), so it is a SEPARATE
// class with named fields — faithful to the binary's distinct controller object,
// not a forced merge with EmoteAngleController nor a needless duplicate.
//
// Field offset table (verified by fresh decompile of EmoteMouthController_ctor
// @0x665C98 + step @0x666068 this conversation; offsets are libkrkr2.so ARM64
// byte offsets kept as provenance comments only — wasm layout need not match per
// CLAUDE.md byte-layout methodology):
//   +0..+79   EmoteAngleController-style 12B-elem value track (ctor:
//             EmoteAngleController_ctor_12Bdeque @0x665cd4). Stepped via cursor
//             @+16 in sub_666068, which reads {float endVal@+0, float dur@+4,
//             int powCount@+8}; 504-block (63 qwords / 21 elems per block, per
//             the progress loop @0x67d17c `v32 = block + 63`).
//   +80       int32_t  state        (0 idle/setup, 1 animating)
//   +84       float    currentValue (output -> *a3; also the start-of-ramp seed)
//   +88       float    endVal       (= popped keyframe elem[+0], the ramp target)
//   +92       float    accum        (0->1 progress; += invDur*dt; clamps at 1.0)
//   +96       float    invDur       (= 1.0 / popped keyframe dur elem[+4])
//   +100      float    powField     (the popped keyframe's elem[+8] BITS, stored
//                                    raw (STR W10) and read back as a float (LDR
//                                    S1, no SCVTF) for the pow() exponent — a
//                                    raw bit reinterpretation, NOT an int->float
//                                    conversion; see ctor/step notes)
//   +104      float    startVal     (snapshot of currentValue at ramp setup)
//   +108      int32_t  beginFrame   (PSB "beginFrame", int; ctor-read; output
//                                    -> *a2 unmodified every step)
//
// PLATFORM_BOUNDARY: sizeof(EmoteMouthController) on Web will not equal 112B
//   (libc++ deque header differs from libstdc++). Offsets above are for
//   traceability; the logical contract is field semantics + element types +
//   lifetime, not byte equality.
//
// NOTE (no mesh resolver): unlike the eye/eyebrow controllers, sub_666068 does
//   NOT call the sub_661F7C/sub_660028 mesh resolver. Its state-0 setup pops a
//   12B keyframe straight off the +0 deque (which the ctor leaves empty; it is
//   populated by the value-write path that is not part of this vertical), with
//   no edge/node table involvement. So the mouth controller has no open mesh
//   boundary — it is structurally complete here. The 12B track is empty at
//   runtime until the value-write path lands, so the step output holds
//   currentValue (0) and beginFrame; this is the same INERT-until-fed status the
//   eye/eyebrow tracks have, NOT a missing computation.
//
#pragma once

#include <cstdint>

#include "tjs.h"
#include "EmoteAngleController.h"

namespace motion {

    // 0x70=112B mouth controller. Plain C++ object (no vtable: ctor +0 writes a
    // std::deque header, not a vptr — matches EmoteAngleController /
    // EmoteVarController / EmoteBlinkController).
    struct EmoteMouthController {
        // +0..+79 — 12B-elem value track (EmoteAngleController-style). The step
        //   pops keyframes {endVal, dur, powCount} off the front of this deque.
        EmoteAngleController valueTrack12B; // ctor 0x665cd4

        // +80..+108 — value-track animation state (compact ramp machine).
        int32_t state        = 0;    // +80
        float   currentValue = 0.0f; // +84 (output -> *a3)
        float   endVal       = 0.0f; // +88 (ramp target)
        float   accum        = 0.0f; // +92 (0->1 progress)
        float   invDur       = 0.0f; // +96 (1/dur)
        float   powField     = 0.0f; // +100 (ramp exponent; raw bits of elem[+8])
        float   startVal     = 0.0f; // +104 (ramp start)
        int32_t beginFrame   = 0;    // +108 (PSB "beginFrame"; output -> *a2)
    };

    // Aligned with libkrkr2.so EmoteMouthController_ctor_guess @ 0x665C98.
    //   memset(self,0,0x50); EmoteAngleController_ctor_12Bdeque(self,0);
    //   state(+80)=0; clear(+84..+91);
    //   beginFrame(+108) = propGetInt(dict, "beginFrame", default 0).
    //   NO blink fields, NO RNG, NO edge/node arrays (unlike eye/eyebrow ctors).
    void EmoteMouthController_ctor(EmoteMouthController* self,
                                   const tTJSVariant& dict);

    // Aligned with libkrkr2.so sub_666068 EmoteMouthController_step @ 0x666068.
    //   Advances the value-track ramp through states 1 (animating) and 0 (setup),
    //   then writes:
    //     *outBeginFrame = (float)beginFrame   (the PSB scalar, never animated)
    //     *outCurrentValue = currentValue      (the ramp output)
    //   and returns (float)beginFrame. The progress loop stores *outBeginFrame
    //   into HM7 keyed by the element's "label" and *outCurrentValue into HM7
    //   keyed by the element's "talkLabel" (the two HM6 keys this controller
    //   registers).
    float EmoteMouthController_step(EmoteMouthController* self,
                                    float* outBeginFrame, float* outCurrentValue,
                                    float dt);

    void EmoteMouthController_dtor(EmoteMouthController* self);

} // namespace motion
