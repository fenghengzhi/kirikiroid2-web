// EmoteWindEmitter — the four-reference wind particle emitter owned by Engine.
//
//   Allocated lazily by EmoteEngine::setWind_guess as a 1564-byte object with
//   the travel endpoints passed to its constructor, and advanced once per
//   Engine frame slice by EmoteWindEmitter::step while gate is set.
//
//   The same pointer is also handed to the bust/hair chain springs as their
//   `collisionCurve` input (`node->spring->collisionCurve = engine->wind`) —
//   i.e. the spring
//   physics reads this 128-slot particle field as a wind force. EmoteSpring.h
//   already documents collisionCurve as "128 entries stride 12B".
//
// Layout (binary byte offsets, enforced below where platform-independent):
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

#include <cstddef>
#include <cstdint>

namespace motion {

    // One emitted wind particle. 12-byte POD, read by the spring physics via
    //   raw `*(float*)(slot+4*i)` indexing (collisionCurve contract).
    struct EmoteWindParticle {
        uint8_t active;   // +0
        uint8_t _pad[3];  // +1..+3 (lifePos is float-aligned at +4)
        float   lifePos;  // +4: position along the travel axis
        float   yPos;     // +8: spawn-time random y in [yLo, yHi]
    };
    static_assert(sizeof(EmoteWindParticle) == 12);
    static_assert(offsetof(EmoteWindParticle, lifePos) == 4);
    static_assert(offsetof(EmoteWindParticle, yPos) == 8);

    // 1564-byte wind emitter object (operator new(0x61C)).
    struct EmoteWindEmitter {
        EmoteWindParticle slots[128]; // +0..+1535
        float startPos;               // +1536
        float endPos;                 // +1540
        uint8_t gate;                 // +1544
        uint8_t _pad1544[3];          // +1545..+1547 (yHi float-aligned at +1548)
        float yHi;                    // +1548 (constructor default 1.0f)
        float yLo;                    // +1552 (constructor default 0.0f)
        float velocity;               // +1556
        float emitAccumulator;        // +1560

        // Four-reference constructor:
        //   +1536 = startPos; all 128 slot.active = 0; +1540 = endPos;
        //   +1544 = 0; tail floats = {1.0f, 0.0f, 0.0f, 0.0f}.
        // Slot padding/lifePos/yPos and the three bytes after gate are left
        // untouched until a slot is emitted.
        EmoteWindEmitter(float startPos_, float endPos_);

        // Four-reference per-slice step:
        //   advance emission accumulator, MT-RNG-triggered spawn into the free
        //   slot, then advance + sign-aware-kill every active slot.
        void step(float dt);
    };
    static_assert(sizeof(EmoteWindEmitter) == 1564);
    static_assert(offsetof(EmoteWindEmitter, startPos) == 1536);
    static_assert(offsetof(EmoteWindEmitter, emitAccumulator) == 1560);

} // namespace motion
