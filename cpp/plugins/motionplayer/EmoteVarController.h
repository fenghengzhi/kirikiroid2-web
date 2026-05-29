// EmoteVarController — 0x80=128B POD aligned with libkrkr2.so
//   sub_667030 EmoteVarController_ctor_20Bdeque  (constructor)
//   sub_666BF8 EmoteVarController_step           (step function)
//
// Layout (per .claude/agent-memory/ida-deep-analyzer/EmoteEngine_controllers.md):
//   +0..+79   std::deque<KeyValue20B>  (libstdc++ ABI, 25 elem/block × 20B)
//             element: { float endValue; float duration; uint32_t powCount; uint64_t pad; }
//   +80       int32_t  count          // # of float channels (2 = pos, 1 = scale, 4 = color)
//   +84       int32_t  state          // 0=idle, 1=animating
//   +88       float*   currentValue   // new[count*4] heap (zero-init)
//   +96       float*   targetValue    // new[count*4] heap
//   +104      float*   startValue     // new[count*4] heap (lerp src)
//   +112      int32_t  powCount       // curve degree
//   +116      float    phase          // 0..1 ramp
//   +120      float    invDuration    // 1/dur
//   +124      int32_t  pad
//
// Binary verification: see EmoteEngine_controllers.md "Variant A".
// IMPORTANT (CLAUDE.md hard rule): plain POD struct, no vtable, no inheritance,
//   no smart pointers. Step is a free function.
//
#pragma once

#include <cstdint>
#include <deque>

namespace motion {

    // Binary element layout for the deque @+0..+79.
    //   sizeof = 20 bytes (must match libstdc++ block math: 25 * 20 = 500)
    //   Power-curve keyframe: animate to endValue over duration seconds with
    //   pow(phase, powCount) easing.
    // PACKED to enforce 20B size — natural alignment of uint64_t would force
    //   24B padding. The libkrkr2.so element is a packed 4+4+4+8 record.
#pragma pack(push, 1)
    struct EmoteVarKeyValue20B {
        float    endValue;   // +0
        float    duration;   // +4
        uint32_t powCount;   // +8
        uint64_t pad;        // +12 — keep 20B size
    };
#pragma pack(pop)
    static_assert(sizeof(EmoteVarKeyValue20B) == 20,
                  "EmoteVarKeyValue20B must be 20 bytes (libkrkr2.so block math)");

    // PLATFORM_BOUNDARY: sizeof(std::deque<>) on libstdc++ (Android, 80B) vs
    //   libc++ (Emscripten/Web, ~64B) differs. The deque header occupies
    //   binary offsets 0..79; on Web the size will not be exactly 80 due to
    //   ABI differences. We use std::deque to preserve element semantics
    //   (front-pop, push-back) and node lifetime; byte-level offset equality
    //   inside the deque header is unreachable on libc++. EmoteVarController
    //   total size therefore may not equal 128B on Web.
    struct EmoteVarController {
        // +0..+79: deque queue
        std::deque<EmoteVarKeyValue20B> queue;

        // +80..+127: animation state
        int32_t count = 0;            // +80
        int32_t state = 0;            // +84
        float*  currentValue = nullptr; // +88 (heap, new[count*4])
        float*  targetValue  = nullptr; // +96 (heap, new[count*4])
        float*  startValue   = nullptr; // +104 (heap, new[count*4])
        int32_t powCount = 0;         // +112
        float   phase = 0.0f;         // +116
        float   invDuration = 0.0f;   // +120
        int32_t pad = 0;              // +124
    };

    // Aligned with libkrkr2.so sub_667030 EmoteVarController_ctor_20Bdeque @ 0x667030.
    //   Zero-inits the deque header, allocates currentValue/targetValue/startValue
    //   each as new[count*4] zero-filled float arrays.
    void EmoteVarController_ctor(EmoteVarController* self, int count);

    // Aligned with libkrkr2.so sub_666BF8 EmoteVarController_step @ 0x666BF8.
    //   Pops a keyframe from queue when state==0; advances phase, computes
    //   pow(phase, powCount) lerp current = start + f*(target-start);
    //   commits final value at phase>=1.
    //   Writes count floats into out[0..count-1] (current value).
    void EmoteVarController_step(EmoteVarController* self, float* out, float dt);

    // dtor helper — release the 3 heap arrays. (libkrkr2.so dtor not separately
    // reverse-engineered; this is the local conservative cleanup matching
    // ctor's heap acquisitions.)
    void EmoteVarController_dtor(EmoteVarController* self);

} // namespace motion
