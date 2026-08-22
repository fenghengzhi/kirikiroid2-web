// EmoteSpring — damped-spring physics used by the current four reference
// binaries.  Per-ABI addresses and pseudocode comparisons live in analysis/.
//
// The spring-state struct is the object that a hair/parts deque node's +0
// field points to. It is a
// separate ~72B heap object, NOT the 48B deque node itself.
//
// Field offsets are taken verbatim from the byte/float accesses in
// the four-reference decompile/disassembly comparison:
//   +0  byte   firstFlag      (1 => "first frame" init branch, cleared to 0)
//   +4  float  k_a            (gravity * dt scales the rest-vector term)
//   +8  float  k_b            (spring * dt scales the delta-to-pos term)
//   +12 float  drag           (friction * dt is the damping factor)
//   +16 float  biasY          (subtracted in the Y atan numerator)
//   +20 float  leverX         (multiplies the X atan numerator)
//   +24 float  leverY         (multiplies the Y atan numerator)
//   +28 float  prevDeltaX     (stored cross-frame; init: storedX - inputX)
//   +32 float  prevDeltaY
//   +36 float  storedX        (non-init: prevDeltaX + inputX, written back)
//   +40 float  storedY
//   +44 float  storedZ        (accumulator paired with +56)
//   +48 float  posX           (integrated position X)
//   +52 float  posY
//   +56 float  posZ
//   +60 float  velX           (integrated velocity X)
//   +64 float  velY
//   +68 float  velZ           (integrated velocity Z)
//
#pragma once

#include <cstddef>
#include <cstdint>

#include "tjs.h"

namespace motion {

    struct EmoteWindEmitter;

    // Reference spring-state object (pointed to by deque-node +0).
    // PLATFORM_BOUNDARY: 72B state; field meanings are derived from the solver
    //   access patterns only (no symbol names in the binary).
    // Fields are accessed by semantic names; binary byte offsets are kept
    // as provenance comments only. No _padN: wasm layout need not match the
    // ARM64 stride — only the field semantics/data flow must (the offset table
    // lives in analysis/, not in struct padding).
    struct EmoteSpringState {
        // The metadata builder uses the argument-taking constructor. Keeping a
        // defaulted default constructor is useful for pure-math callers and
        // preserves value-initialization without changing the native path.
        EmoteSpringState() = default;
        explicit EmoteSpringState(const tTJSVariant& dict);

        uint8_t firstFlag;   // +0
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
        float   velZ;        // +68
    };

    // Pure math leaf — no TJS dispatch and no allocation.  All four references
    // consume only the two output pointers.  The apparent exit-register value
    // differs between ARM32 and ARM64, proving that it is not a source-level
    // float return value; the faithful signature is void.
    void EmotePhysics_springStep_guess(EmoteSpringState* self,
                                       float* outX, float* outY,
                                       float inputX, float inputY,
                                       float forceX, float forceY,
                                       float dt, float outputScale,
                                       float angleRad);

    // Two-segment chain state used by the bust/hair/parts control paths. The
    // four references agree on the source-level field sequence below, including
    // a final borrowed wind-emitter pointer. Per-ABI layout evidence lives in
    // analysis/motionplayer_bust_chain_spring_four_binary_2026-08-11.md.
    //
    // The surviving runtime names distinguish serialized snapshot roles from
    // solver roles: `op` restores `op`, while `bp`, `p`, and `pv` restore the
    // internal target `p`, current-position `pv`, and velocity `bp` arrays.
    // `scale[segment][0/1]` is filled from scale_x/scale_y.
    struct EmoteBustChainSpring {
        EmoteBustChainSpring() = default;
        explicit EmoteBustChainSpring(const tTJSVariant& dict);

        uint8_t firstFlag;
        float gravity;
        float frictionX;
        float frictionY;
        float bRate;
        float vBound;
        int32_t udEft;
        float bendSpd;
        float bendVol;
        float length[2];
        float ofs;
        float bendR;
        float bendS;
        float scale[2][2];
        float prevDelta[2];
        float op[3];
        float p[2][3];
        float pv[2][3];
        float bp[2][3];
        EmoteWindEmitter* collisionCurve;
    };
    static_assert(sizeof(EmoteBustChainSpring) ==
                      (sizeof(void*) == 8 ? 176u : 168u),
                  "chain spring must keep the reference ABI-sized state");

    // Three references retain this leaf while Android ARM64 inlines the exact
    // 128-slot scan into the chain solver.
    float EmoteWindEmitter_lookupForce_guess(
        const EmoteWindEmitter* emitter, float segmentX);

    // Pure two-segment chain solver. It writes one X-angle per segment and
    // writes the selected `ud_eft` segment's Y-angle through outLastY.
    void EmoteBustChainSpring_step_guess(
        EmoteBustChainSpring* self,
        float anchorX, float anchorY,
        float* outSeg0, float* outSeg1, float* outLastY,
        float forceX, float forceY, float dt,
        float scale, float angleRad);

    // Bend-depth/phase postprocessor. ARM32 and both iOS references retain it
    // as a helper; Android ARM64 inlines it at both call sites. The callers
    // ignore the ABI-specific exit-register residue, so its source return is void.
    void EmoteBustChainSpring_postBend_guess(
        EmoteBustChainSpring* self, float lastY,
        float* outSeg0, float* outSeg1, float dt);

} // namespace motion
