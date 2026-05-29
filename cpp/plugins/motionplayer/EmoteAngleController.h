// EmoteAngleController — 0x70=112B POD aligned with libkrkr2.so
//   sub_6867B0 EmoteAngleController_ctor_12Bdeque  (constructor)
//   sub_666634 EmoteAngleController_step           (step function, shortest-path)
//
// Layout (per .claude/agent-memory/ida-deep-analyzer/EmoteEngine_controllers.md):
//   +0..+79   std::deque<KeyValue12B>  (libstdc++ ABI, 42 elem/block × 12B)
//             element: { float endRad; float duration; uint32_t powCount; }
//   +80       int32_t  state         // 0=idle, 1=animating
//   +84       float    currentRad
//   +88       float    targetRad     // after shortest-path wrap
//   +92       float    startRad
//   +96       float    invDuration
//   +100      int32_t  powCount
//   +104      float    phase
//   +108      int32_t  pad
//
// Binary verification: see EmoteEngine_controllers.md "Variant B".
// IMPORTANT (CLAUDE.md hard rule): plain POD struct, no vtable, no inheritance,
//   no smart pointers. Step is a free function.
//
#pragma once

#include <cstdint>
#include <deque>

namespace motion {

    struct EmoteAngleKeyValue12B {
        float    endRad;     // +0
        float    duration;   // +4
        uint32_t powCount;   // +8
    };
    static_assert(sizeof(EmoteAngleKeyValue12B) == 12,
                  "EmoteAngleKeyValue12B must be 12 bytes (libkrkr2.so block math)");

    // PLATFORM_BOUNDARY: same caveat as EmoteVarController — std::deque header
    //   size differs between libstdc++ (80B) and libc++ (~64B). Local
    //   sizeof(EmoteAngleController) may not equal 112B on Web. We preserve
    //   logical semantics (front-pop / push-back, identical element type).
    struct EmoteAngleController {
        // +0..+79
        std::deque<EmoteAngleKeyValue12B> queue;

        // +80..+111
        int32_t state = 0;          // +80
        float   currentRad = 0.0f;  // +84
        float   targetRad  = 0.0f;  // +88
        float   startRad   = 0.0f;  // +92
        float   invDuration = 0.0f; // +96
        int32_t powCount = 0;       // +100
        float   phase = 0.0f;       // +104
        int32_t pad = 0;            // +108
    };

    // Aligned with libkrkr2.so sub_6867B0 EmoteAngleController_ctor_12Bdeque
    //   @ 0x6867B0.
    // The ctor leaves all animation state at 0 (no heap allocations — single
    //   scalar channel doesn't need 3 heap arrays like the Var variant).
    void EmoteAngleController_ctor(EmoteAngleController* self, int count);

    // Aligned with libkrkr2.so sub_666634 EmoteAngleController_step
    //   @ 0x666634.
    // Pops keyframe, applies shortest-path wrap:
    //   if |target - current| > pi: target ± 2pi to take the short way
    //   Power-curve interp on 1 scalar.
    //   Wraps result into [0, 2pi).
    void EmoteAngleController_step(EmoteAngleController* self, float* outRad, float dt);

    void EmoteAngleController_dtor(EmoteAngleController* self);

} // namespace motion
