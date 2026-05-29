// EmoteAngleController free-function implementation.
// Aligned with libkrkr2.so sub_6867B0 + sub_666634.

#include "EmoteAngleController.h"

#include <cmath>

namespace motion {

    namespace {
        constexpr float kPi  = 3.14159265358979323846f;
        constexpr float kTwoPi = 2.0f * kPi;
    }

    // Aligned with libkrkr2.so sub_6867B0 EmoteAngleController_ctor_12Bdeque
    //   @ 0x6867B0.
    // Caller does memset(self, 0, 0x50) before this call (deque header zero);
    //   here we ensure all animation-state fields are zero and queue empty.
    // count parameter is signaled as 0 by the EmoteEngine_ctor caller (the
    //   angle controller only ever drives a single scalar) but we keep the
    //   parameter for signature parity with the binary.
    void EmoteAngleController_ctor(EmoteAngleController* self, int /*count*/) {
        // queue default-constructed empty by C++ struct init.
        self->state = 0;
        self->currentRad = 0.0f;
        self->targetRad  = 0.0f;
        self->startRad   = 0.0f;
        self->invDuration = 0.0f;
        self->powCount = 0;
        self->phase = 0.0f;
        self->pad = 0;
    }

    // Aligned with libkrkr2.so sub_666634 EmoteAngleController_step
    //   @ 0x666634.
    // Pops a keyframe (when idle), applies shortest-path target wrap, then
    //   power-curve interpolates a single scalar. Result is wrapped into
    //   [0, 2pi).
    void EmoteAngleController_step(EmoteAngleController* self, float* outRad, float dt) {
        if (self->state == 0) {
            if (self->queue.empty()) {
                *outRad = self->currentRad;
                return;
            }
            const EmoteAngleKeyValue12B elem = self->queue.front();
            self->queue.pop_front();

            // Shortest-path wrap: if |target - current| > pi, adjust target
            // by ±2pi so interpolation takes the short way around.
            float target = elem.endRad;
            const float delta = target - self->currentRad;
            if (delta > kPi) {
                target -= kTwoPi;
            } else if (delta < -kPi) {
                target += kTwoPi;
            }

            self->startRad   = self->currentRad;
            self->targetRad  = target;
            self->invDuration = (elem.duration != 0.0f) ? (1.0f / elem.duration) : 0.0f;
            self->powCount = static_cast<int32_t>(elem.powCount);
            self->phase = 0.0f;
            self->state = 1;
        }
        if (self->state == 1) {
            self->phase += self->invDuration * dt;
            if (self->phase >= 1.0f) {
                self->currentRad = self->targetRad;
                self->state = 0;
                self->phase = 0.0f;
            } else {
                const float f = std::pow(self->phase, static_cast<float>(self->powCount));
                self->currentRad = self->startRad +
                    f * (self->targetRad - self->startRad);
            }
        }
        // Wrap result into [0, 2pi).
        float r = std::fmod(self->currentRad, kTwoPi);
        if (r < 0.0f) r += kTwoPi;
        *outRad = r;
    }

    void EmoteAngleController_dtor(EmoteAngleController* /*self*/) {
        // No heap arrays to release.
    }

} // namespace motion
