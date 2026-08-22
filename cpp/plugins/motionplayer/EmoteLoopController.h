// Loop-curve controller recovered from all four current reference binaries.
// Android arm64 inlines its sampler into the Engine progress core; the other
// three targets retain an out-of-line controller step with an output pointer.
// Exact function maps and native ABI layouts live in analysis/.
#pragma once

#include <cstdint>
#include <vector>

namespace motion {

    // This 12-byte element is an internal serialized/strided data contract on
    // every target; unlike enclosing C++ object offsets, these offsets are
    // intentionally load-bearing.
    struct EmoteLoopKeyframe12B {
        // Exact original member spellings are unavailable. These semantic
        // names describe the two endpoints consumed by the sampler without
        // retaining Hex-Rays' temporary v0/v1 labels.
        float startValue_guess = 0.0f; // +0 — segment start value
        float endValue_guess   = 0.0f; // +4 — segment end value
        float span             = 0.0f; // +8 — duration (>=accum advances)
    };

    // Plain non-polymorphic controller. Native pointer/container widths change
    // its enclosing size, but the field order is common to all four targets.
    struct EmoteLoopController {
        int32_t currentIndex = 0;
        float accum = 0.0f;
        std::vector<EmoteLoopKeyframe12B> keys;
    };

    // Advances state and writes the current blend through outValue. The native
    // function has no guards for null pointers, empty keys, invalid indices or
    // non-positive spans; callers and this port preserve those boundaries.
    void EmoteLoopController_step_guess(
        EmoteLoopController *ctl, float *outValue, float dt);

} // namespace motion
