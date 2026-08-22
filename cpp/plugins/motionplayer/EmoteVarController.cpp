// EmoteVarController free-function implementation, cross-checked against the
// four current reference binaries.

#include "EmoteVarController.h"

#include <cmath>
#include <cstring>

namespace motion {

    // All four constructors allocate exactly `channelCount` floats for each of
    // the current/start/target owners and zero-initialize those arrays. Their
    // deque headers differ by STL ABI, but no extra portable member exists.
    EmoteVarController::EmoteVarController(int channelCount) {
        // queue is already default-constructed empty by C++ struct init
        // under the PLATFORM_BOUNDARY ABI note.
        count = channelCount;
        state = 0;
        currentValue = new float[channelCount]();
        startValue   = new float[channelCount]();
        targetValue  = new float[channelCount]();
        // powCount/phase/invDuration are deliberately untouched.
    }

    // State 0 starts at most one queued keyframe and falls through to its first
    // state-1 update. Completing that keyframe does not start a second queued
    // command in the same call. Native SIMD/scalar loop choices are ABI/codegen
    // details; every logical loop is bounded by the stored signed count.
    void EmoteVarController_step(EmoteVarController* self, float* out, float dt) {
        const int count = self->count;
        if (self->state == 0 && !self->queue.empty()) {
            const EmoteVarKeyValue20B& keyframe = self->queue.front();
            // count==4 deliberately reads word 3 as both alpha and duration.
            const float* channels = keyframe.channelAndDuration;
            for (int i = 0; i < count; ++i) {
                self->startValue[i] = self->currentValue[i];
                self->targetValue[i] = channels[i];
            }
            self->state = 1;
            self->invDuration =
                1.0f / keyframe.channelAndDuration[3];
            std::memcpy(
                &self->powCount, &keyframe.powCount, sizeof(float));
            self->queue.pop_front();
            self->phase = 0.0f;
        }

        if (self->state == 1) {
            self->phase += self->invDuration * dt;
            if (self->phase >= 1.0f) {
                self->phase = 1.0f;
                for (int i = 0; i < count; ++i) {
                    self->currentValue[i] = self->targetValue[i];
                }
                self->state = 0;
            } else {
                const float weight =
                    std::pow(self->phase, self->powCount);
                for (int i = 0; i < count; ++i) {
                    self->currentValue[i] = self->startValue[i] +
                        weight *
                            (self->targetValue[i] - self->startValue[i]);
                }
            }
        }

        for (int i = 0; i < count; ++i) {
            out[i] = self->currentValue[i];
        }
    }

    void EmoteVarController_setTarget_guess(
        EmoteVarController* self, const float* values, float duration,
        float powCount, bool append) {
        // Native uses the unsigned LS condition after FCMP/VCMPE: non-positive
        // ordered values commit here, while unordered NaN remains queueable.
        if(duration <= 0.0f) {
            self->queue.clear();
            self->state = 0;
            for(int i = 0; i < self->count; ++i) {
                self->currentValue[i] = values[i];
            }
            return;
        }

        if(!append) {
            self->queue.clear();
            self->state = 0;
        }

        self->queue.emplace_back(
            values, self->count, duration, powCount);
    }

    void EmoteVarController_reset_guess(EmoteVarController* self) {
        if(!self) {
            return;
        }
        if(!self->queue.empty()) {
            self->state = 0;
            const EmoteVarKeyValue20B &last = self->queue.back();
            const float *channels = last.channelAndDuration;
            for(int i = 0; i < self->count; ++i) {
                self->currentValue[i] = channels[i];
            }
            self->queue.clear();
            return;
        }
        if(self->state != 0) {
            self->state = 0;
            for(int i = 0; i < self->count; ++i) {
                self->currentValue[i] = self->targetValue[i];
            }
        }
    }

    EmoteVarController::~EmoteVarController() {
        // Four-reference body order is current -> start -> target. The queue is
        // then destroyed automatically after this body, before operator delete.
        delete[] currentValue;
        delete[] startValue;
        delete[] targetValue;
    }

} // namespace motion
