// Global blink/wind random source reconstructed from all four 1.3.9
// references. Platform addresses and native object layouts live in analysis/.
//
// CLAUDE.md hard rule: faithfully port the binary's RNG algorithm (MT19937 +
//   the exact canonical-real construction), NOT std::rand / a different MT
//   seeding. The repo's tTJSMersenneTwister (cpp/core/tjs2) is a DIFFERENT,
//   TJS-script-facing generator with different seeding/output and is NOT used
//   here — the blink path uses this dedicated global per the binary.
//
// PLATFORM_BOUNDARY: the seed is clock-derived (steady_clock::now() /
//   1000000), so the blink sequence is inherently non-deterministic across
//   runs — this matches the binary and is why the eye/blink subsystem has no
//   deterministic oracle/fixture (documented verification gap).
//
#pragma once

#include <cstdint>

namespace motion {

    // The native class has a virtual destructor followed by left, a cursor,
    // and 624 pointer-width state slots. uintptr_t reproduces both observed
    // ABI sizes while the algorithm deliberately uses each slot's low 32 bits.
    struct EmoteBlinkMt19937 {
        explicit EmoteBlinkMt19937(uint32_t seed);
        virtual ~EmoteBlinkMt19937() = default;

        int left;
        uintptr_t *cursor;
        uintptr_t mt[624];
    };
    static_assert(sizeof(EmoteBlinkMt19937) ==
                  (sizeof(void *) == 8 ? 0x1398 : 0x9CC));

    // Returns the lazily initialized process-global generator.
    EmoteBlinkMt19937* EmoteBlinkRng_getGlobal_guess();

    // Consumes two MT words and returns the reference canonical double in
    // [0,1).
    double EmoteBlinkRng_nextCanonical_guess(EmoteBlinkMt19937* self);

} // namespace motion
