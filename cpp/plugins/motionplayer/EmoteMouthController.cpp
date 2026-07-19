// EmoteMouthController — ctor (0x665C98) + step (sub_666068) faithful port.
//
// The smallest of the three "control" leaf controllers (0x70 vs eye 0x170,
// eyebrow 0x150). See EmoteMouthController.h for the full structural analysis
// (value-track scalar ramp; no blink machine, no RNG, no mesh resolver, no audio
// despite the "mouth" name).

#include "EmoteMouthController.h"
#include "MotionDispatch.h"

#include <cmath>
#include <cstring> // std::memcpy for the raw-bits powField reinterpret

namespace motion {

    // Aligned with libkrkr2.so EmoteMouthController_ctor_guess @ 0x665C98.
    // Decompiled pseudocode (this conversation):
    //   memset(self, 0, 0x50);                          // +0..+79  /*0x665cc8*/
    //   EmoteAngleController_ctor_12Bdeque(self, 0);    // 12B deque /*0x665cd4*/
    //   *(int*)(self+80) = 0;                           // state     /*0x665cd8*/
    //   *(qword*)(self+84) = 0;                          // +84..+91 /*0x665cdc*/
    //   beginFrame(+108) = Motion_propGetInt("beginFrame", default 0); /*0x665d60*/
    // NOTE: NO endFrame / blinkInterval* / blinkFrameCount / blinkEnabled reads,
    //   NO RNG call, NO "edge" / "node" arrays. The mouth controller has no blink
    //   state and no mesh tables at all (the object is 0x70, smaller than the
    //   eye's 0x170 and the eyebrow's 0x150).
    void EmoteMouthController_ctor(EmoteMouthController* self,
                                   const tTJSVariant& dict) {
        // The 12B value track default-constructs empty (the binary's memset +
        //   EmoteAngleController_ctor_12Bdeque leaves it empty; std::deque's
        //   default ctor replicates this under the PLATFORM_BOUNDARY ABI note).
        EmoteAngleController_ctor(&self->valueTrack12B, 0); // 0x665cd4

        // state(+80)=0 and the +84..+91 clear are covered by the member
        //   initializers (state=0, currentValue=0, endVal=0). The binary's
        //   memset(0x50) + the explicit *(int*)(+80)=0 / *(qword*)(+84)=0 all
        //   zero these same fields.

        // beginFrame (the ONLY scalar field read; +108).               /*0x665d60*/
        self->beginFrame = detail::motionPropGetInt(
            dict, TJS_W("beginFrame"));                           // 0x665d60
    }

    // Aligned with libkrkr2.so sub_666068 EmoteMouthController_step @ 0x666068.
    // Decompiled pseudocode (this conversation):
    //   v5 = state(+80);
    //   if (v5) {
    //     if (v5 == 1) {                                  // animating
    //       v8 = accum(+92) + invDur(+96)*dt;  accum(+92) = v8;
    //       if (v8 >= 1.0) { current(+84)=endVal(+88); state(+80)=0; accum=1.0; }
    //       else current(+84) = pow(v8, powF(+100))*(endVal(+88)-start(+104))
    //                            + start(+104);
    //     }
    //   } else {                                          // state 0: setup
    //     if (12B-track @+16 non-empty) {                 // *(+48) != *(+16)
    //       start(+104)=current(+84); state(+80)=1; endVal(+88)=elem[+0];
    //       invDur(+96)=1.0/elem[+4]; accum(+92)=0; powF(+100)=elem[+8] (raw bits);
    //       pop_front 12B-track;
    //     }
    //   }
    //   result = (float)beginFrame(+108);
    //   *a2 = result;  *a3 = current(+84);  return result;
    //
    // NOTE: the binary clamps with `if (v8 >= 1.0)` and then writes the literal
    //   1.0 (0x3F800000) back into accum(+92) on completion. State 2 does NOT
    //   exist for the mouth controller (unlike the eyebrow step which has a
    //   separate state==2 animating branch); the mouth ramp animates inline in
    //   state 1 and snaps to state 0 on completion.
    float EmoteMouthController_step(EmoteMouthController* self,
                                    float* outBeginFrame, float* outCurrentValue,
                                    float dt) {
        const float a4 = dt;
        const int v5 = self->state; // *(a1+80)  /*0x66607c*/

        if (v5) {                                           // /*0x666088*/
            if (v5 == 1) {                                  // /*0x666090*/ animating
                // v8 = accum(+92) + invDur(+96)*dt.                   /*0x66609c*/
                const float v8 = self->accum + (self->invDur * a4);
                self->accum = v8;                            // *(a1+92)=v8 /*0x6660a8*/
                if (v8 >= 1.0f) {                            // /*0x6660ac*/
                    // current(+84)=endVal(+88); state(+80)=0; accum(+92)=1.0.
                    const float v14 = self->endVal;          // *(a1+88) /*0x666148*/
                    self->state = 0;                         // *(a1+80)=0 /*0x666150*/
                    self->accum = 1.0f;                      // 0x3F800000 /*0x666154*/
                    self->currentValue = v14;                // *(a1+84)=endVal /*0x666158*/
                } else {
                    // current(+84) = pow(v8, powF(+100))
                    //                * (endVal(+88) - start(+104)) + start(+104).
                    //   The binary reads +100 with `LDR S1` (NO SCVTF) — the float
                    //   exponent is the raw bits stored at setup, not an int->float
                    //   conversion. powField already holds those raw bits.  /*0x6660b0..0x6660dc*/
                    const float v9 =
                        std::pow(v8, self->powField) *
                            (self->endVal - self->startVal) +
                        self->startVal;
                    self->currentValue = v9;                 // *(a1+84)=v9 /*0x6660e0*/
                }
            }
        } else {                                            // /*0x6660ec*/ state 0: setup
            if (!self->valueTrack12B.queue.empty()) {        // *(+48)!=*(+16) /*0x6660f4*/
                const EmoteAngleKeyValue12B kf =
                    self->valueTrack12B.queue.front();        // {endVal,dur,pow} /*0x6660f8*/

                self->startVal = self->currentValue;          // *(a1+104)=*(a1+84) /*0x666108*/
                self->state    = 1;                           // *(a1+80)=1 /*0x666100*/
                self->endVal   = kf.endRad;                   // *(a1+88)=elem[+0] /*0x666110*/
                self->invDur   = 1.0f / kf.duration;          // *(a1+96)=1/elem[+4] /*0x666120*/
                self->accum    = 0.0f;                        // *(a1+92)=0 /*0x666130*/
                // powField(+100) = elem[+8] RAW BITS. The binary stores the
                //   keyframe's powCount uint32 with `STR W10` and later reads it
                //   with `LDR S1` as a float — a bit reinterpretation. Mirror that
                //   exactly (memcpy the uint32 bits into the float slot), NOT a
                //   numeric cast.                                        /*0x666124/0x666134*/
                std::memcpy(&self->powField, &kf.powCount, sizeof(float));

                self->valueTrack12B.queue.pop_front();         // advance +16 / free block /*0x666140/0x666164*/
            }
        }

        // result = (float)beginFrame; *a2 = result; *a3 = currentValue.
        //   The binary writes *a3 = *(_DWORD*)(a1+84) (the float bits of
        //   currentValue, which the progress loop reads back as a float).
        const float result = static_cast<float>(self->beginFrame); // /*0x666188*/
        *outBeginFrame   = result;             // *a2 = (float)beginFrame /*0x66618c*/
        *outCurrentValue = self->currentValue; // *a3 = currentValue      /*0x666194*/
        return result;                          // /*0x6661a4*/
    }

    void EmoteMouthController_dtor(EmoteMouthController* self) {
        // The 12B value track owns no heap beyond its deque buffer (freed by the
        //   std::deque destructor). Mirrors EmoteAngleController_dtor — no manual
        //   per-element release.
        (void)self;
    }

} // namespace motion
