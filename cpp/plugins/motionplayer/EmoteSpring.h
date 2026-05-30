// EmoteSpring — damped-spring single-step physics, aligned with libkrkr2.so
//   EmotePhysics_springStep @ 0x662768.
//
// CLAUDE.md hard rules satisfied:
//   - plain POD struct, no vtable / inheritance / smart pointers
//   - free function step (matches the binary's standalone sub_662768)
//   - all float literals preserved exactly (0.0392699082, 0.0451603944)
//
// The spring-state struct is the object that a hair/parts deque node's +0
// field POINTS TO (`a1 = *v9` in EmoteEngine_stepHairParts @0x67B748). It is a
// separate ~72B heap object, NOT the 48B deque node itself.
//
// Field offsets are taken verbatim from the byte/float accesses in
// sub_662768's decompile/disasm:
//   +0  byte   firstFlag      (1 => "first frame" init branch, cleared to 0)
//   +4  float  k_a            (*(a1+4) * dt scales the rest-vector term)
//   +8  float  k_b            (*(a1+8) * dt scales the delta-to-pos term)
//   +12 float  drag           (*(a1+12) * dt is the velocity damping factor)
//   +16 float  biasY          (subtracted in the Y atan term: -(...) - a1+16)
//   +20 float  leverX         (multiplies the X atan numerator)
//   +24 float  leverY         (multiplies the Y atan numerator)
//   +28 float  prevDeltaX     (stored cross-frame; init branch: storedX - a4)
//   +32 float  prevDeltaY
//   +36 float  storedX        (non-init branch: prevDeltaX + a4, written back)
//   +40 float  storedY
//   +44 float  storedZ        (accumulator paired with +56)
//   +48 float  posX           (integrated position X)
//   +52 float  posY
//   +56 float  posZ
//   +60 float  velX           (integrated velocity X)
//   +64 float  velY
//   +68 float  accZ           (+68 = velZ accumulator, read into v29)
//
#pragma once

#include <cstdint>

namespace motion {

    // libkrkr2.so spring-state object (pointed to by deque-node +0).
    // PLATFORM_BOUNDARY: 72B POD; field meanings are derived from sub_662768
    //   access patterns only (no symbol names in the binary).
    struct EmoteSpringState {
        uint8_t firstFlag;   // +0
        uint8_t _pad1[3];    // +1..+3 (align to +4)
        float   k_a;         // +4
        float   k_b;         // +8
        float   drag;        // +12
        float   biasY;       // +16
        float   leverX;      // +20
        float   leverY;      // +24
        float   prevDeltaX;  // +28
        float   prevDeltaY;  // +32
        float   storedX;     // +36
        float   storedY;     // +40
        float   storedZ;     // +44
        float   posX;        // +48
        float   posY;        // +52
        float   posZ;        // +56
        float   velX;        // +60
        float   velY;        // +64
        float   accZ;        // +68
    };
    static_assert(sizeof(EmoteSpringState) == 72,
                  "EmoteSpringState must be 72 bytes (libkrkr2.so sub_662768 layout)");

    // Aligned with libkrkr2.so EmotePhysics_springStep @ 0x662768.
    //
    // Signature mirrors the binary:
    //   float springStep(self, float* outX, float* outY,
    //                     a4, a5, a6, a7, a8/*dt*/, a9, a10/*angleRad*/);
    // Writes *outX/*outY = atanf(...)/0.0392699082; returns *outY.
    //
    // Pure math leaf — no TJS dispatch, no allocation. Safe to port verbatim.
    float EmotePhysics_springStep(EmoteSpringState* self,
                                  float* outX, float* outY,
                                  float a4, float a5, float a6, float a7,
                                  float a8, float a9, float a10);

} // namespace motion
