#include "EmoteMouthController.h"
#include "MotionDispatch.h"
#include "ncbind.hpp"

#include <cmath>
#include <cstring>

namespace motion {

    EmoteMouthController::EmoteMouthController(const tTJSVariant& dict)
        : state(0), currentValue(0.0f), endVal(0.0f) {
        ncbPropAccessor object{tTJSVariant(dict)};
        beginFrame = static_cast<int32_t>(object.GetValue(
            TJS_W("beginFrame"), ncbTypedefs::Tag<tjs_int>(), 0,
            &detail::emoteControllerBeginFrameHint_guess));
    }

    void EmoteMouthController_setTarget_guess(
        EmoteMouthController* self,
        float value,
        float duration,
        float power,
        bool append) {
        // Native branches to this path unless duration is ordered-positive;
        // unordered NaN therefore commits immediately too.
        if(!(duration > 0.0f)) {
            self->valueTrack12B.clear();
            self->currentValue = value;
            self->state = 0;
            return;
        }

        if(!append) {
            self->valueTrack12B.clear();
            self->state = 0;
        }

        EmoteAngleKeyValue12B keyframe;
        keyframe.endRad = value;
        keyframe.duration = duration;
        std::memcpy(&keyframe.powCount, &power, sizeof(power));
        self->valueTrack12B.push_back(keyframe);
    }

    float EmoteMouthController_step(EmoteMouthController* self,
                                    float* outBeginFrame,
                                    float* outCurrentValue,
                                    float dt) {
        if (self->state != 0) {
            if (self->state == 1) {
                const float phase = self->accum + self->invDur * dt;
                self->accum = phase;
                if (phase >= 1.0f) {
                    self->state = 0;
                    self->accum = 1.0f;
                    self->currentValue = self->endVal;
                } else {
                    self->currentValue =
                        std::pow(phase, self->powField) *
                            (self->endVal - self->startVal) +
                        self->startVal;
                }
            }
        } else if (!self->valueTrack12B.empty()) {
            const EmoteAngleKeyValue12B keyframe =
                self->valueTrack12B.front();

            self->state = 1;
            self->startVal = self->currentValue;
            self->endVal = keyframe.endRad;
            self->invDur = 1.0f / keyframe.duration;
            self->accum = 0.0f;
            std::memcpy(&self->powField, &keyframe.powCount,
                        sizeof(self->powField));
            self->valueTrack12B.pop_front();
        }

        const float result = static_cast<float>(self->beginFrame);
        *outBeginFrame = result;
        *outCurrentValue = self->currentValue;
        return result;
    }

} // namespace motion
