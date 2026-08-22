// EmoteAngleController operations, cross-checked against all four current
// reference binaries.

#include "EmoteAngleController.h"

#include <cmath>
#include <cstring> // std::memcpy for the raw-bits powCount reinterpret

namespace motion {

    namespace {
        constexpr float kPi = 3.14159265358979323846f;
    }

    // Common four-reference pseudocode:
    //   if (state == 1) {
    //     phase += invDuration * dt;
    //     if (phase >= 1) {
    //       phase = 1; currentRad = wrap(targetRad); state = 0;
    //     } else {
    //       currentRad = wrap(pow(phase,powCount)*(targetRad-startRad)+startRad);
    //     }
    //   } else if (state == 0 && !queue.empty()) {
    //       startRad = currentRad;
    //       targetRad = shortestPath(queue.front().endRad, currentRad);
    //       state = 1; invDuration = 1 / duration;
    //       powCount = queue.front().powCount; phase = 0;
    //       pop_front;
    //   }
    //   *out = currentRad;
    //
    // Fidelity notes:
    //   1. Branches are mutually exclusive — SETUP does not advance phase in the
    //      same call; animation begins on the NEXT step.
    //   2. Setup clears phase. Three targets do this as the high zero word of a
    //      64-bit powCount/phase store; iOS ARMv7 emits a separate zero store.
    //      Completion stores 1.0.
    //   3. Result wrap uses the truncated literal 6.2832 via iterative add/sub
    //      and is stored back into currentRad; the shortest-path adjust in
    //      SETUP uses the accurate 6.28318531 — two distinct constants.
    void EmoteAngleController_step(EmoteAngleController* self, float* outRad, float dt) {
        if (self->state == 1) {
            const float p = self->invDuration * dt + self->phase;
            self->phase = p;
            if (p >= 1.0f) {
                float v = self->targetRad;
                self->phase = 1.0f;
                while (v < 0.0f)     v += 6.2832f;
                while (v >= 6.2832f) v -= 6.2832f;
                self->currentRad = v;
                self->state = 0;
            } else {
                float v = std::pow(p, self->powCount)
                          * (self->targetRad - self->startRad) + self->startRad;
                while (v < 0.0f)     v += 6.2832f;
                while (v >= 6.2832f) v -= 6.2832f;
                self->currentRad = v;
            }
        } else if (self->state == 0) {
            if (!self->queue.empty()) {
                const EmoteAngleKeyValue12B& elem = self->queue.front();
                const float cur = self->currentRad;
                self->startRad = cur;
                float dest = elem.endRad;
                if (dest > cur) {
                    if (dest - cur > kPi) dest -= 6.28318531f;
                } else {
                    if (cur - dest > kPi) dest += 6.28318531f;
                }
                self->targetRad = dest;
                self->state = 1;
                self->invDuration = 1.0f / elem.duration;
                // The value is a float word copy, not an integer conversion.
                std::memcpy(&self->powCount, &elem.powCount, sizeof(float));
                self->phase = 0.0f;
                self->queue.pop_front();
            }
        }
        *outRad = self->currentRad;
    }

    void EmoteAngleController_setTarget_guess(
        EmoteAngleController* self, float endRad, float duration,
        float powCount, bool append) {
        while(endRad < 0.0f) {
            endRad += 6.2832f;
        }
        while(endRad >= 6.2832f) {
            endRad -= 6.2832f;
        }

        // Native branches to this path unless duration is ordered-positive;
        // unordered NaN therefore commits immediately too.
        if(!(duration > 0.0f)) {
            self->queue.clear();
            self->state = 0;
            self->currentRad = endRad;
            return;
        }

        if(!append) {
            self->queue.clear();
            self->state = 0;
        }

        self->queue.push_back({endRad, duration, powCount});
    }

} // namespace motion
