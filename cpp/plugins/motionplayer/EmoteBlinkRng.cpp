// Shared MT19937 source used by blink controllers and wind emitters.

#include "EmoteBlinkRng.h"

#include <chrono>

namespace motion {

    namespace {

        // All four references use one process-global, lazily allocated owner.
        EmoteBlinkMt19937* g_blinkRng = nullptr;

        // MT19937 constants.
        constexpr uint32_t kMatrixA   = 2567483615u; // 0x9908B0DF
        constexpr uint32_t kUpperMask = 0x80000000u;
        constexpr uint32_t kLowerMaskWithoutBit0 = 0x7FFFFFFEu;

        // Divide the full steady-clock representation first. The references
        // truncate the quotient to the low 32 bits, not the pre-division tick.
        uint32_t blinkSeed() {
            const auto now = std::chrono::steady_clock::now()
                                 .time_since_epoch()
                                 .count();
            return static_cast<uint32_t>(now / 1000000);
        }

        // Four-reference state regeneration. The native implementation splits
        // the wrap into three loops; this in-place modulo form has the same
        // deliberate dependency on already-regenerated low-index slots.
        void regenerate(EmoteBlinkMt19937* s) {
            uintptr_t* mt = s->mt;
            for (int i = 0; i < 624; ++i) {
                const uint32_t current = static_cast<uint32_t>(mt[i]);
                const uint32_t next =
                    static_cast<uint32_t>(mt[(i + 1) % 624]);
                const uint32_t y =
                    (current & kUpperMask) |
                    (next & kLowerMaskWithoutBit0);
                const uint32_t mag = (next & 1u) ? kMatrixA : 0u;
                const uint32_t distant =
                    static_cast<uint32_t>(mt[(i + 397) % 624]);
                mt[i] = static_cast<uintptr_t>(
                    distant ^ (y >> 1) ^ mag);
            }
            s->cursor = s->mt;
            s->left = 624;
        }

        // Shared native tempering applied to a raw state word.
        uint32_t temper(uint32_t y) {
            y ^= (y >> 11);
            y ^= (y << 7) & 0x9D2C5680u;
            y ^= (y << 15) & 0xEFC60000u;
            y ^= (y >> 18);
            return y;
        }

        // Draw one tempered low-32 word. The pre-decrement/left==1 convention
        // is important because one canonical draw can regenerate between its
        // first and second words.
        uint32_t nextWord(EmoteBlinkMt19937* s) {
            const int previousLeft = s->left;
            s->left = previousLeft - 1;
            if (previousLeft == 1) {
                regenerate(s);
            }
            const uint32_t y = static_cast<uint32_t>(*s->cursor);
            ++s->cursor;
            return temper(y);
        }

    } // namespace

    // The seed expression is evaluated after operator new and before this
    // constructor in every reference. The virtual destructor supplies the
    // polymorphic object prefix seen by the native allocation path.
    EmoteBlinkMt19937::EmoteBlinkMt19937(uint32_t seed) {
        left = 1;
        uint32_t word = seed;
        mt[0] = static_cast<uintptr_t>(word);
        for (uint32_t index = 1; index < 624; ++index) {
            word = 1812433253u * (word ^ (word >> 30)) + index;
            mt[index] = static_cast<uintptr_t>(word);
        }
        cursor = mt;
        left = 1;
    }

    // One unsynchronized raw process-global owner. It is published only after
    // the complete clock-seeded constructor and is never released normally.
    EmoteBlinkMt19937* EmoteBlinkRng_getGlobal_guess() {
        if (!g_blinkRng) {
            g_blinkRng = new EmoteBlinkMt19937(blinkSeed());
        }
        return g_blinkRng;
    }

    // Two consecutive tempered words fill a synthetic [1,2) double's 52-bit
    // mantissa, low word first and the second word's low 20 bits above it.
    double EmoteBlinkRng_nextCanonical_guess(EmoteBlinkMt19937* self) {
        const uint32_t low = nextWord(self);
        const uint32_t high = nextWord(self);
        const uint64_t mantHi =
            static_cast<uint64_t>(high & 0xFFFFFu) << 32;
        const uint64_t bits = static_cast<uint64_t>(low) | mantHi |
                              0x3FF0000000000000ull;
        double canonicalPlusOne;
        static_assert(sizeof(double) == sizeof(uint64_t), "");
        __builtin_memcpy(&canonicalPlusOne, &bits, sizeof(double));
        return canonicalPlusOne - 1.0;
    }

} // namespace motion
