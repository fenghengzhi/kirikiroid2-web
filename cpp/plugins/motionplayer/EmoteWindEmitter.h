// EmoteWindEmitter — libkrkr2.so wind particle emitter (the heap object at
//   EmoteEngine+1128).
//
//   Allocated lazily by Player_startWind_populate (sub_6709AC @0x6709AC) with
//   `operator new(0x61C)` (1564 bytes) and initialised by EmoteWindEmitter_init
//   (sub_670AFC @0x670AFC). Advanced per frame-slice by EmoteWindEmitter_step
//   (sub_6687E8 @0x6687E8), gated in EmoteEngine_progress @0x67d384:
//       v65 = *(engine+1128);
//       if (v65 && *(byte*)(v65+1544)) sub_6687E8(v65, clampedStep);
//
//   The same pointer is also handed to the bust/hair chain springs as their
//   `collisionCurve` input (EmoteEngine_stepBust @0x67bea4:
//   `node->spring->collisionCurve(+168) = *(engine+1128)`) — i.e. the spring
//   physics reads this 128-slot particle field as a wind force. EmoteSpring.h
//   already documents collisionCurve as "128 entries stride 12B".
//
// Layout (binary byte offsets, for analysis traceability only — NOT enforced):
//   +0..+1535   : 128 particle slots, 12 bytes each:
//                   +0 active (byte), +4 lifePos (float), +8 yPos (float)
//   +1536       : startPos (float)   — slot.lifePos seed on emit
//   +1540       : endPos   (float)   — kill bound (sign-aware)
//   +1544       : gate     (byte)    — set 1 by startWind, the progress gate
//   +1548       : yHi      (float)   — y spawn range high (= freqX)
//   +1552       : yLo      (float)   — y spawn range low  (= freqY)
//   +1556       : velocity (float)   — signed per-step lifePos velocity
//   +1560       : emitAccumulator (float) — fractional emission carry
//
// The 12B slot internal format IS a platform-independent data contract (the
//   spring reads it via `*(float*)(slot+4*i)`), so the slot POD keeps its
//   field layout; the object's ABI offset on the heap is irrelevant.
//
#pragma once

#include <cstdint>

namespace motion {

    // One emitted wind particle. 12-byte POD, read by the spring physics via
    //   raw `*(float*)(slot+4*i)` indexing (collisionCurve contract).
    struct EmoteWindParticle {
        uint8_t active = 0;   // +0
        uint8_t _pad[3] = {}; // +1..+3 (lifePos is float-aligned at +4)
        float   lifePos = 0.f; // +4: position along travel axis (sub_6687E8 init/step)
        float   yPos = 0.f;    // +8: spawn-time random y in [yLo, yHi]
    };

    // 1564-byte wind emitter object (operator new(0x61C)).
    struct EmoteWindEmitter {
        EmoteWindParticle slots[128] = {}; // +0..+1535
        float startPos = 0.f;              // +1536
        float endPos   = 0.f;              // +1540
        uint8_t gate    = 0;               // +1544
        uint8_t _pad1544[3] = {};          // +1545..+1547 (yHi float-aligned at +1548)
        float yHi = 1.0f;                  // +1548 (init default 1.0f, set to freqX)
        float yLo = 0.0f;                  // +1552 (init default 0.0f, set to freqY)
        float velocity = 0.f;              // +1556
        float emitAccumulator = 0.f;       // +1560

        // Aligned with libkrkr2.so EmoteWindEmitter_init (sub_670AFC @0x670AFC):
        //   +1536 = startPos; all 128 slot.active = 0; +1540 = endPos;
        //   +1544 = 0 (gate); {+1548,+1552} = {1.0f, 0.0f} default.
        void init(float startPos_, float endPos_);

        // Aligned with libkrkr2.so EmoteWindEmitter_step (sub_6687E8 @0x6687E8):
        //   advance emission accumulator, MT-RNG-triggered spawn into the free
        //   slot, then advance + sign-aware-kill every active slot.
        void step(float dt);
    };

} // namespace motion
