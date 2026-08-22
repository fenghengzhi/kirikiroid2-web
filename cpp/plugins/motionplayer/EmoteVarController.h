// EmoteVarController — non-polymorphic owning controller reconstructed from the
// four current reference binaries. Its common source declaration is a keyframe
// deque, channel count/state, three owned float arrays, and three curve scalars.
// The four ABIs preserve that order and ownership while assigning different
// deque-header sizes, pointer offsets and tail alignment; their physical layout
// tables live in analysis/.
//
// Four-reference behavior:
//   ctor: operator new[](4*count) x3 = count floats EACH; zero-fill each array.
//   step: every loop bound is the stored count; out receives count floats.
//     state==0: startValue[i]=currentValue[i]; targetValue[i]=element[i] (i in [0,count));
//               invDuration=1/element.channelAndDuration[3];
//               powCount=element.powCount.
//     state==1: phase+=invDuration*dt; if phase>=1 -> currentValue[i]=targetValue[i], state=0;
//               else f=powf(phase,powCount), currentValue[i]=startValue[i]+f*(targetValue[i]-startValue[i]).
//     tail: out[i]=currentValue[i] for i in [0,count).
// IMPORTANT: non-polymorphic owning class, no vtable, no inheritance and no
// smart pointers. Construction/destruction own the three arrays and deque;
// the animation operations remain free functions.
//
#pragma once

#include <cstdint>
#include <deque>

namespace motion {

    // Power-curve keyframe element. Its constructor writes duration into word 3
    // and power into word 4, then copies only `count` channel floats starting at
    // word 0. Consequently a four-channel color keyframe overwrites duration
    // with alpha. Words between the copied channels and word 3 deliberately
    // remain indeterminate; all four references construct directly in deque
    // storage and never value-initialize the full 20 bytes.
    struct EmoteVarKeyValue20B {
        float channelAndDuration[4]; // +0..+12; [3] is duration or channel 3
        float powCount;             // +16; stored/loaded as raw float bits

        EmoteVarKeyValue20B(const float *values, int count,
                            float duration, float power) {
            channelAndDuration[3] = duration;
            powCount = power;
            for(int i = 0; i < count; ++i) {
                channelAndDuration[i] = values[i];
            }
        }
    };

    static_assert(sizeof(EmoteVarKeyValue20B) == 20,
                  "variable-controller keyframe must remain five float words");

    // std::deque preserves front-pop, push-back and element lifetime. Its
    // implementation-specific header and block map are intentionally left to
    // the target standard library rather than represented as source fields.
    struct EmoteVarController {
        explicit EmoteVarController(int channelCount);
        ~EmoteVarController();

        EmoteVarController(const EmoteVarController &) = delete;
        EmoteVarController &operator=(const EmoteVarController &) = delete;

        std::deque<EmoteVarKeyValue20B> queue;

        int32_t count = 0;
        int32_t state = 0;
        float*  currentValue = nullptr; // owned new[count] output array
        float*  startValue   = nullptr; // owned new[count] origin snapshot
        float*  targetValue  = nullptr; // owned new[count] destination array
        // Curve exponent: step copies the keyframe word as raw float bits and
        // passes it directly to powf without integer conversion.
        // The four constructors do not write these three fields. State gates ensure the
        // live paths initialize the relevant fields before reading them; keep
        // the source fields deliberately indeterminate at construction.
        float   powCount;             // raw float bits
        float   phase;
        float   invDuration;
    };

    // Four-reference animation step.
    //   When idle, pops at most one keyframe and immediately advances it by dt;
    //   completion never starts the next queued keyframe in the same call.
    //   Active interpolation uses pow(phase,powCount), then commits target at
    //   phase>=1.
    //   Writes count floats into out[0..count-1] (current value).
    void EmoteVarController_step(EmoteVarController* self, float* out, float dt);

    // Shared direct-controller setter. For ordered duration<=0 it clears the
    // queue, idles the controller and copies the values immediately. Unordered
    // NaN follows the queue path. Otherwise it optionally replaces the queue
    // and pushes one fixed-size keyframe. The source name is unavailable, hence
    // the suffix.
    void EmoteVarController_setTarget_guess(
        EmoteVarController* self, const float* values, float duration,
        float powCount, bool append);

    // If queued keyframes exist, commits the last queued destination into
    // currentValue and clears the queue; otherwise an active interpolation
    // commits targetValue. In both cases state becomes idle.
    void EmoteVarController_reset_guess(EmoteVarController* self);

} // namespace motion
