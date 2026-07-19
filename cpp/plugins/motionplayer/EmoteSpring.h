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

#include <cstddef>
#include <cstdint>

#include "tjs.h"

namespace motion {

    // libkrkr2.so spring-state object (pointed to by deque-node +0).
    // PLATFORM_BOUNDARY: 72B POD; field meanings are derived from sub_662768
    //   access patterns only (no symbol names in the binary).
    // Fields accessed by NAME (a1->storedX etc.); binary byte offsets are kept
    // as provenance comments only. No _padN: wasm layout need not match the
    // ARM64 stride — only the field semantics/data flow must (the offset table
    // lives in analysis/, not in struct padding).
    struct EmoteSpringState {
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
        float   accZ;        // +68
    };

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

    // Aligned with libkrkr2.so sub_662448 @ 0x662448 — EmoteSpringState ctor.
    //   self = operator new(0x48). Seeds firstFlag=1, all stored/pos/vel = 0
    //   (from .bss constants qword_1AB7E68/dword_1AB7E70 = 0), then reads the
    //   per-node spring params from the raw TJS dispatch (narrow double->float, store
    //   raw bits = getFloatValue): gravity->k_a, spring->k_b, friction->drag,
    //   scale_x->leverX, scale_y->leverY. The vec3 fields (storedXYZ/posXYZ/
    //   velXYZ) and biasY are overwritten by the builder (op/p/pv/ofs) afterward.
    void EmoteSpringState_ctor(EmoteSpringState* self,
                               const tTJSVariant& dict);

    // ========================================================================
    // EmoteBustChainSpring — 2-segment chain spring, aligned with libkrkr2.so
    //   sub_6689A4 @ 0x6689A4. This is the spring object that a BUST deque
    //   node's +0 field POINTS TO (`a1 = *v15` in EmoteEngine_stepBust
    //   @0x67BCE8). It is NOT EmotePhysics_springStep — bust uses this separate
    //   chain solver with a per-segment loop (v28 in {0,1}) and an optional
    //   collision-depth curve lookup at +168.
    //
    // Field offsets are taken verbatim from the byte/float/int accesses in
    // sub_6689A4's decompile (no symbol names exist in the binary):
    //   +0   byte   firstFlag    (1 => init branch; cleared to 0)
    //   +4   float  velDampA     (*(a1+4)*a9 — seg drag-X numerator base)
    //   +8   float  dragX        (v34 = v31 - v31 * (*(a1+8)) * a9)
    //   +12  float  dragY        (v37 = v84 - v84 * (*(a1+12)) * a9)
    //   +16  float  forceScaleN  (*(a1+16)*a9 — spring force for seg != 1)
    //   +20  float  forceScale1  (*(a1+20)    — spring force for seg == 1)
    //   +24  int32  lastSegIndex (compared to v28: gates the +44/Y atan write)
    //   +36  float  restLen0     (seg0 rest length; initial v24)
    //   +40  float  restLen1     (seg1 rest length; v24 for v28==1)
    //   +44  float  restLenLastY (rest length in the last-seg Y atan)
    //   +56  float  angScaleX0   (*(a1+56) — seg0 X atan slope)
    //   +60  float  angScaleY0   (*(a1+60) — seg0 Y atan slope)
    //   +64  float  angScaleX1   (*(a1+64) — seg1 X atan slope, a1+8*1+56)
    //   +68  float  angScaleY1   (*(a1+68) — seg1 Y atan slope, a1+8*1+60)
    //   +72  float  prevDeltaX   (init branch: stored - input)
    //   +76  float  prevDeltaY
    //   +80  float  rootX        (chain root pos; v49 for seg0 = a1+80)
    //   +84  float  rootY
    //   +88  int32  rootFlag     (copied to +100 and +112)
    //   +92  float  rootCopyX    (*(a1+92) = *(a1+80) QWORD copy)
    //   +96  float  accumX       (a1+96 += *(a1+36))
    //   +100 int32  copyFlag1
    //   +104 q      rootCopy2    (*(a1+104) = *(a1+92) QWORD)
    //   +108 float  accumY       (a1+108 += *(a1+40))
    //   +116 float  seg0PosX     (working pos; v46 for seg0 = a1+116)
    //   +120 float  seg0PosY
    //   +124 float  seg0PosZ
    //   +128 float  seg1PosX     (v46 for seg1 = a1+128)
    //   +132 float  seg1PosY
    //   +136 float  seg1PosZ
    //   +140 float  seg0VelX     (v77 for seg0 = a1+140)
    //   +144 float  seg0VelY
    //   +148 float  seg0VelZ
    //   +152 float  seg1VelX     (v77 for seg1 = a1+152)
    //   +156 float  seg1VelY
    //   +160 float  seg1VelZ
    //   +168 ptr    collisionCurve (optional; *(a1+168). Points at the wind
    //               emitter's 128-slot particle pool (EmoteWindEmitter): 128
    //               entries, 12B stride, each {byte active@+0, float lifePos@+4,
    //               float yPos@+8} as established by EmoteWindEmitter_step
    //               (sub_6687E8). The control float at curve+1556 is the wind
    //               velocity. Set by stepBust from EmoteEngine+1128
    //               (_windEmitter). Null => skipped.)
    // PLATFORM_BOUNDARY: 176B POD; the collisionCurve table is held as an opaque
    //   pointer (its allocation path is not reversed; the lookup is null-guarded
    //   exactly as in the binary so an un-populated table is inert).
    // ========================================================================
    // Fields accessed by NAME; binary byte offsets kept as provenance comments
    // only. No _padN / no offset static_assert: wasm layout need not match the
    // ARM64 stride — the +28/+48 "untouched" gaps and the +168 QWORD/4B-ptr
    // divergence are ABI details, not source structure. Offset table: analysis/.
    //
    // OFFSET-EXACT LAYOUT (data-contract, CLAUDE.md element-internal exception):
    //   sub_6689A4 (step) and sub_668EF8 (ctor) BOTH access this object via raw
    //   `*(float*)(self + off)` / `*(int*)(self + off)` at the byte offsets in the
    //   comments above. The binary leaves genuine UNUSED slots at +28/+32 (used as
    //   the depth-ramp scratch sp[7]/sp[8] by stepBust!), +48/+52 (sp[12]/sp[13]),
    //   +112, +164. Those are part of the 176B data contract, NOT cosmetic ABI
    //   padding — so they are declared as named members below to PIN the offsets
    //   (C++ would otherwise pack the fields contiguously and the F(off) accesses
    //   in the step/ctor would read the wrong slots). Every member is 4B; the only
    //   ABI divergence is the final pointer (+168 QWORD on ARM64, 4B on wasm32),
    //   and nothing is read past +168 by byte offset, so wasm32 stays consistent.
    struct EmoteBustChainSpring {
        uint8_t firstFlag;        // +0   (byte; bytes +1..+3 are the natural gap)
        uint8_t _pin1[3];
        float   velDampA;         // +4   gravity
        float   dragX;            // +8   friction_x
        float   dragY;            // +12  friction_y
        float   forceScaleN;      // +16  b_rate
        float   forceScale1;      // +20  v_bound
        int32_t lastSegIndex;     // +24  ud_eft (int)
        float   bendSpd;          // +28  bend_spd   (= sp[7] depth-ramp speed)
        float   bendVol;          // +32  bend_vol   (= sp[8] depth-ramp volume)
        float   restLen0;         // +36  length[0]
        float   restLen1;         // +40  length[1]
        float   restLenLastY;     // +44
        float   rampPhase;        // +48  (= sp[12] fmod phase accumulator)
        float   rampDepth;        // +52  (= sp[13] collision depth)
        float   angScaleX0;       // +56  scale_x[0]
        float   angScaleY0;       // +60  scale_y[0]
        float   angScaleX1;       // +64  scale_x[1]
        float   angScaleY1;       // +68  scale_y[1]
        float   prevDeltaX;       // +72
        float   prevDeltaY;       // +76
        float   rootX;            // +80
        float   rootY;            // +84
        int32_t rootFlag;         // +88
        float   rootCopyX;        // +92   seg0 rest x
        float   accumX;           // +96   seg0 rest y
        int32_t copyFlag1;        // +100  seg0 rest z
        float   rootCopy2X;       // +104  seg1 rest x
        float   accumY;           // +108  seg1 rest y
        float   _pin112;          // +112  seg1 rest z
        float   seg0PosX;         // +116
        float   seg0PosY;         // +120
        float   seg0PosZ;         // +124
        float   seg1PosX;         // +128
        float   seg1PosY;         // +132
        float   seg1PosZ;         // +136
        float   seg0VelX;         // +140
        float   seg0VelY;         // +144
        float   seg0VelZ;         // +148
        float   seg1VelX;         // +152
        float   seg1VelY;         // +156
        float   seg1VelZ;         // +160
        float   _pin164;          // +164
        void*   collisionCurve;   // +168 (binary: QWORD; wasm32: 4B ptr)
    };

    // Aligned with libkrkr2.so sub_6689A4 @ 0x6689A4.
    //   void chainStep(self, float* outSeg0, float* outSeg1, float* outLastY,
    //                  a5, a6, a7, a8, a9/*dt*/, a10, a11/*angleRad*/);
    //   Writes seg0/seg1 X-angle into *outSeg0/*outSeg1 and the last segment's
    //   Y-angle into *outLastY. No allocation; the collision-curve lookup is the
    //   only pointer deref and is null-guarded.
    void EmoteBustChainSpring_step(EmoteBustChainSpring* self,
                                   float* outSeg0, float* outSeg1, float* outLastY,
                                   float a5, float a6, float a7, float a8,
                                   float a9, float a10, float a11);

    // Aligned with libkrkr2.so sub_668EF8 @ 0x668EF8 — EmoteBustChainSpring ctor.
    //   self = operator new(0xB0=176B). firstFlag=1, +48=0, collisionCurve=0,
    //   rootX/Y(+80)=0, memset(+92,0,0x48). Reads (narrow double->float raw bits):
    //   gravity->+4, friction_x->+8, friction_y->+12, b_rate->+16, v_bound->+20,
    //   ud_eft->+24 (INT, propGetInt), bend_spd->+28, bend_vol->+32. The "length",
    //   "scale_x", "scale_y" props are 2-element dispatch arrays -> [0]/[1] each.
    //   length[0]->+36(restLen0), length[1]->+40(restLen1); scale_x[0]->+56,[1]->
    //   +64; scale_y[0]->+60,[1]->+68. Then the rest positions are derived from the
    //   rest unit vector (0,1,0) (= floats of qword_1AB7E74/dword_1AB7E7C):
    //     seg0 rest @+92/+96/+100 = restLen0 * (0,1,0); copy -> +116/+120/+124;
    //     seg1 rest @+104/+108/+112 = restLen1 * (0,1,0); copy -> +128/+136(.., low
    //     half of QWORD copy); +140/+148 = 0; +152/+160 = 0.
    void EmoteBustChainSpring_ctor(EmoteBustChainSpring* self,
                                   const tTJSVariant& dict);

} // namespace motion
