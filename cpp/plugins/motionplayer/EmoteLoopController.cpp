// EmoteLoopController — loop-curve sampler. The step is INLINED into
//   EmoteEngine_progress @0x67d2a0 in the binary; this file provides the same
//   sampler as a free function so EmoteEngine::progress mirrors the binary's
//   per-entry body 1:1 (and so the curve math lives next to the struct it reads).
//   See EmoteLoopController.h for the full builder + inline-step analysis and the
//   float-bits note.

#include "EmoteLoopController.h"

namespace motion {

    // Aligned with libkrkr2.so EmoteEngine_progress inline curve step
    //   @0x67d2a0..0x67d370. Advances the controller's accum by dt, wraps the
    //   keyframe index while the current segment's span is consumed, then returns
    //   the linear blend between the active keyframe's v0 and v1.
    //
    //   idx   = ctl.currentIndex;                               // 0x67d2d0 LDR W
    //   accum = ctl.accum + dt;                                 // 0x67d2d4 FADD S
    //   ctl.accum = accum;                                      // 0x67d2d8 STR S
    //   count = ctl.keys.size();    // (finish-start)/12        // 0x67d300..0x67d304
    //   span  = ctl.keys[idx].span;                             // 0x67d2e4 LDR S
    //   if (span <= accum) {                                    // 0x67d2ec B.LS
    //     do { idx=(idx+1)%count; accum-=span; span=keys[idx].span; }
    //       while (span <= accum);                              // 0x67d308..0x67d324
    //     ctl.accum = accum; ctl.currentIndex = idx;            // 0x67d32c/0x67d330
    //   }
    //   t   = accum / span;                                     // 0x67d340 FDIV S
    //   out = t*keys[idx].v1 + (1-t)*keys[idx].v0;              // 0x67d33c..0x67d354
    float EmoteLoopController_step(EmoteLoopController* ctl, float dt) {
        int   idx   = ctl->currentIndex;            // *(int*)(ctl+0)  /*0x67d2d0*/
        float accum = ctl->accum + dt;              // *(float*)(ctl+4) + dt /*0x67d2d4*/
        ctl->accum  = accum;                        // store back /*0x67d2d8*/

        // count = (keys.finish - keys.start) / sizeof(EmoteLoopKeyframe12B).
        //   The binary computes ((finish-start)>>2) * 0x55555555 (= /4 then /3)
        //   which is exactly the keyframe count; std::vector::size() is the
        //   structural equivalent. /*0x67d300..0x67d304*/
        const int count = static_cast<int>(ctl->keys.size());

        float span = ctl->keys[idx].span;           // *(float*)(kf+8) /*0x67d2e4*/
        if (span <= accum) {                        // FCMP / B.LS /*0x67d2ec*/
            do {                                    // /*0x67d308*/
                idx   = (idx + 1) % count;          // MSUB (wrap) /*0x67d310*/
                accum = accum - span;               // FSUB S /*0x67d318*/
                span  = ctl->keys[idx].span;        // *(float*)(kf+8) /*0x67d31c*/
            } while (span <= accum);                // /*0x67d324*/
            ctl->accum        = accum;              // STR S [ctl+4] /*0x67d32c*/
            ctl->currentIndex = idx;                // STR W [ctl+0] /*0x67d330*/
        }

        // LDP S3, S2, [kf]: S3 = keys[idx].v0 (kf+0), S2 = keys[idx].v1 (kf+4).
        const float t = accum / span;               // FDIV S /*0x67d340*/
        const float out =
            (t * ctl->keys[idx].v1)                  // FMUL t*v1 /*0x67d34c*/
            + ((1.0f - t) * ctl->keys[idx].v0);      // FMUL (1-t)*v0, FADD /*0x67d344/0x67d350/0x67d354*/
        return out;
    }

} // namespace motion
