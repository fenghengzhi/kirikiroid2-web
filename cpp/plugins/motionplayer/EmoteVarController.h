// EmoteVarController — 0x80=128B POD aligned with libkrkr2.so
//   sub_667030 EmoteVarController_ctor_20Bdeque  (constructor)
//   sub_666BF8 EmoteVarController_step           (step function)
//
// Layout (verified by reverse-engineering sub_667030 / sub_666BF8):
//   +0..+79   std::deque<KeyValue20B>  (libstdc++ ABI, 25 elem/block × 20B)
//             element (20B): float channel[0..count); float duration@+12; uint32_t powCount@+16
//   +80       int32_t  count          // # of float channels (2 = pos, 1 = scale, 4 = color)
//   +84       int32_t  state          // 0=idle, 1=animating
//   +88       float*   currentValue   // new[count] heap (zero-init)  -- output value
//   +96       float*   targetValue    // new[count] heap  -- lerp "from" (= old current at keyframe start)
//   +104      float*   startValue     // new[count] heap  -- lerp "to"   (= keyframe element channels)
//   +112      int32_t  powCount       // curve degree
//   +116      float    phase          // 0..1 ramp
//   +120      float    invDuration    // 1/dur
//   +124      int32_t  pad
//
// Binary verification (this conversation):
//   ctor 0x667030: v5 = 4*count BYTES; operator new[](v5) x3 = count floats EACH;
//                  memset(.,0,4*count) x3. *(self+80)=count. NOT count*4.
//   step 0x666BF8: every loop bound = count = *(int*)(self+80). out (a2) gets count floats.
//     state==0: targetValue[i]=currentValue[i]; startValue[i]=element[i] (i in [0,count));
//               invDuration=1/element.duration(@+12); powCount=element.powCount(@+16).
//     state==1: phase+=invDuration*dt; if phase>=1 -> currentValue[i]=targetValue[i], state=0;
//               else f=powf(phase,powCount), currentValue[i]=targetValue[i]+f*(startValue[i]-targetValue[i]).
//     tail: out[i]=currentValue[i] for i in [0,count).
// IMPORTANT (CLAUDE.md hard rule): plain POD struct, no vtable, no inheritance,
//   no smart pointers. Step is a free function.
//
#pragma once

#include <cstdint>
#include <deque>

namespace motion {

    // Binary element layout for the deque @+0..+79.
    //   sizeof = 20 bytes (must match libstdc++ block math: 25 * 20 = 500,
    //   confirmed by sub_6878D8: a1[4]=v11+500, element stride 20).
    //   Power-curve keyframe: the leading `count` floats are the per-channel
    //   destination values (read as element[i], i in [0,count)); duration is at
    //   element offset +12 and powCount (curve degree) at +16. The binary reads
    //   element channels via *(float*)(elem + 4*i), so channels occupy +0,+4,+8
    //   and (for count<=3) coexist with duration@+12 / powCount@+16 in 20 bytes.
    // PACKED to enforce 20B size — natural alignment would otherwise pad.
#pragma pack(push, 1)
    struct EmoteVarKeyValue20B {
        float    channel[3]; // +0,+4,+8 — per-channel destination values (element[i])
        float    duration;   // +12  (step reads *(float*)(elem+12))
        uint32_t powCount;   // +16  (step reads *(uint32_t*)(elem+16))
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
        float*  currentValue = nullptr; // +88 (heap, new[count]) — output value
        float*  targetValue  = nullptr; // +96 (heap, new[count]) — lerp "from"
        float*  startValue   = nullptr; // +104 (heap, new[count]) — lerp "to"
        int32_t powCount = 0;         // +112
        float   phase = 0.0f;         // +116
        float   invDuration = 0.0f;   // +120
        int32_t pad = 0;              // +124
    };

    // Aligned with libkrkr2.so sub_667030 EmoteVarController_ctor_20Bdeque @ 0x667030.
    //   Zero-inits the deque header, allocates currentValue/targetValue/startValue
    //   each as new[count] zero-filled float arrays (binary: new[](4*count) bytes).
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
