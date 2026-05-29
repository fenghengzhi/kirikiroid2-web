// EmoteVarController free-function implementation.
// Aligned with libkrkr2.so sub_667030 + sub_666BF8.

#include "EmoteVarController.h"

#include <cmath>
#include <cstring>

namespace motion {

    // Aligned with libkrkr2.so sub_667030 EmoteVarController_ctor_20Bdeque
    //   @ 0x667030.
    // Behavior summary (from EmoteEngine_controllers.md "Variant A"):
    //   - The caller has already done memset(self, 0, 0x50) before calling
    //     this for the in-place deque header zero (we replicate by ensuring
    //     the std::deque is default-constructed empty + count/state zeroed).
    //   - Allocates currentValue/targetValue/startValue as new[count*4]
    //     float arrays, zero-init.
    //   - Stores count at +80.
    //   - All other state fields (state, powCount, phase, invDuration, pad)
    //     are zero.
    void EmoteVarController_ctor(EmoteVarController* self, int count) {
        // queue is already default-constructed empty by C++ struct init.
        self->count = count;
        self->state = 0;
        const int channelCount = count * 4;
        self->currentValue = new float[channelCount]();
        self->targetValue  = new float[channelCount]();
        self->startValue   = new float[channelCount]();
        self->powCount = 0;
        self->phase = 0.0f;
        self->invDuration = 0.0f;
        self->pad = 0;
    }

    // Aligned with libkrkr2.so sub_666BF8 EmoteVarController_step
    //   @ 0x666BF8.
    // Step function (per Variant A spec):
    //   if state == 0:
    //     pop deque head as elem
    //     targetValue[i] = elem.endValue (broadcast count*4)
    //     invDuration = 1/elem.duration
    //     powCount = elem.powCount
    //     copy currentValue → startValue
    //     state = 1
    //   if state == 1:
    //     phase += invDuration * dt
    //     if phase >= 1: commit final (current = target), state = 0
    //     else: f = powf(phase, powCount);
    //           current[i] = start[i] + f*(target[i]-start[i])
    //   copy currentValue → out[0..count-1] (only count floats, not count*4)
    //
    // PLATFORM_BOUNDARY: the binary uses SIMD vector intrinsics to update 4
    //   floats at a time; here we use scalar loops. Numerical results match.
    void EmoteVarController_step(EmoteVarController* self, float* out, float dt) {
        const int channelCount = self->count * 4;
        if (self->state == 0) {
            if (self->queue.empty()) {
                // No keyframe pending — emit current as-is.
                for (int i = 0; i < self->count; ++i) {
                    out[i] = self->currentValue ? self->currentValue[i] : 0.0f;
                }
                return;
            }
            const EmoteVarKeyValue20B elem = self->queue.front();
            self->queue.pop_front();
            for (int i = 0; i < channelCount; ++i) {
                self->targetValue[i] = elem.endValue;
                self->startValue[i]  = self->currentValue[i];
            }
            self->invDuration = (elem.duration != 0.0f) ? (1.0f / elem.duration) : 0.0f;
            self->powCount = static_cast<int32_t>(elem.powCount);
            self->phase = 0.0f;
            self->state = 1;
        }
        if (self->state == 1) {
            self->phase += self->invDuration * dt;
            if (self->phase >= 1.0f) {
                for (int i = 0; i < channelCount; ++i) {
                    self->currentValue[i] = self->targetValue[i];
                }
                self->state = 0;
                self->phase = 0.0f;
            } else {
                const float f = std::pow(self->phase, static_cast<float>(self->powCount));
                for (int i = 0; i < channelCount; ++i) {
                    self->currentValue[i] = self->startValue[i] +
                        f * (self->targetValue[i] - self->startValue[i]);
                }
            }
        }
        // Write count floats to out (channel 0 of each of count groups).
        for (int i = 0; i < self->count; ++i) {
            out[i] = self->currentValue[i];
        }
    }

    void EmoteVarController_dtor(EmoteVarController* self) {
        delete[] self->currentValue;  self->currentValue = nullptr;
        delete[] self->targetValue;   self->targetValue  = nullptr;
        delete[] self->startValue;    self->startValue   = nullptr;
    }

} // namespace motion
