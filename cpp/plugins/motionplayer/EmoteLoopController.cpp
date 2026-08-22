// Loop-curve sampler shared by the portable Engine progress implementation.

#include "EmoteLoopController.h"

namespace motion {

    void EmoteLoopController_step_guess(
        EmoteLoopController *ctl, float *outValue, float dt) {
        int idx = ctl->currentIndex;
        float accum = ctl->accum + dt;
        ctl->accum = accum;

        const int count = static_cast<int>(ctl->keys.size());

        float span = ctl->keys[idx].span;
        if(span <= accum) {
            do {
                idx = (idx + 1) % count;
                accum = accum - span;
                span = ctl->keys[idx].span;
            } while(span <= accum);
            ctl->accum = accum;
            ctl->currentIndex = idx;
        }

        const float t = accum / span;
        *outValue = (t * ctl->keys[idx].endValue_guess) +
            ((1.0f - t) * ctl->keys[idx].startValue_guess);
    }

} // namespace motion
