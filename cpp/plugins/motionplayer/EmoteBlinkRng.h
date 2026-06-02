// EmoteBlinkRng — the global blink random source used by EmoteBlinkController.
//   Aligned with libkrkr2.so:
//     sub_9F1A08 — lazy-init of a process-global std::mt19937 (operator new
//                  0x1398; seeded by sub_A2BDBC = steady_clock::now()/1000000)
//     sub_9F17D0 — generate a double in [0,1) (libstdc++
//                  std::generate_canonical-style 32+20-bit mantissa build,
//                  returns (constructed_double_in_[1,2)) - 1.0)
//
// CLAUDE.md hard rule: faithfully port the binary's RNG algorithm (MT19937 +
//   the exact canonical-real construction), NOT std::rand / a different MT
//   seeding. The repo's tTJSMersenneTwister (cpp/core/tjs2) is a DIFFERENT,
//   TJS-script-facing generator with different seeding/output and is NOT used
//   here — the blink path uses this dedicated global per the binary.
//
// PLATFORM_BOUNDARY: the seed is wall-clock derived (steady_clock::now() /
//   1000000), so the blink sequence is inherently non-deterministic across
//   runs — this matches the binary and is why the eye/blink subsystem has no
//   deterministic oracle/fixture (documented verification gap).
//
#pragma once

#include <cstdint>

namespace motion {

    // Process-global MT19937 state, lazily initialised on first use, matching
    //   sub_9F1A08 (one shared generator for all blink controllers). The binary
    //   stores 624 words as QWORDs (libstdc++ uint_fast32_t = 64-bit on ARM64);
    //   we keep them as uint32_t since every value is masked to 32 bits — the
    //   produced sequence is bit-identical.
    struct EmoteBlinkMt19937 {
        uint32_t mt[624] = {};
        int      left = 0;     // *(state+8): words remaining before regen
        int      pos  = 0;     // index into mt of the next word
        bool     initialized = false;
    };

    // Aligned with libkrkr2.so sub_9F1A08. Returns the lazily-initialised global
    //   generator (seeds it on first call).
    EmoteBlinkMt19937* EmoteBlinkRng_get();

    // Aligned with libkrkr2.so sub_9F17D0. Advances the generator and returns a
    //   double in [0,1).
    double EmoteBlinkRng_next(EmoteBlinkMt19937* self);

} // namespace motion
