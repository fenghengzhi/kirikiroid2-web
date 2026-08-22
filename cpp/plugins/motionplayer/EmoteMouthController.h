// Scalar mouth/talk controller.
//
// The controller owns one naked 12-byte keyframe deque.  Its fixed
// beginFrame value is published under the mouth label, while currentValue is
// published under the talk label.  It has no vtable, blink state, mesh table,
// RNG state, or audio input.
#pragma once

#include <cstdint>

#include "tjs.h"
#include "EmoteAngleController.h"

namespace motion {

    struct EmoteMouthController {
        explicit EmoteMouthController(const tTJSVariant& dict);
        ~EmoteMouthController() = default;

        EmoteMouthController(const EmoteMouthController&) = delete;
        EmoteMouthController& operator=(const EmoteMouthController&) = delete;

        // Popped keyframes are {target value, duration, raw float power bits}.
        EmoteAngleKeyframeQueue valueTrack12B;

        int32_t state;
        float   currentValue;
        float   endVal;

        // The native constructor deliberately leaves these four fields
        // untouched.  The first normal state-0 setup writes all of them before
        // the state-1 animation path can read them.
        float   accum;
        float   invDur;
        float   powField;
        float   startVal;

        int32_t beginFrame;
    };

    // Queue or apply a talk target.  The source-level argument order is
    // value/duration/power/append.  On AArch64 the three floats use S0..S2
    // while append independently uses W1; the register layout must not be
    // mistaken for a source signature with append first.
    void EmoteMouthController_setTarget_guess(
        EmoteMouthController* self,
        float value,
        float duration,
        float power,
        bool append);

    // Advances at most one state-machine phase.  A state-0 call consumes one
    // keyframe and performs setup only; interpolation starts on a later call.
    float EmoteMouthController_step(EmoteMouthController* self,
                                    float* outBeginFrame,
                                    float* outCurrentValue,
                                    float dt);

} // namespace motion
