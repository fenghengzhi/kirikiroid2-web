// EmoteBlinkRng — faithful port of libkrkr2.so sub_9F1A08 / sub_9F17D0.

#include "EmoteBlinkRng.h"

#include <chrono>

namespace motion {

    namespace {

        // The single process-global generator pointer (libkrkr2.so qword_1AF7E80).
        // Raw owning pointer + lazy `new`, matching the binary's
        //   `if (!qword_1AF7E80) { qword_1AF7E80 = operator new(...); init; }`.
        EmoteBlinkMt19937* g_blinkRng = nullptr;

        // MT19937 constants.
        constexpr uint32_t kMatrixA   = 2567483615u; // 0x9908B0DF
        constexpr uint32_t kUpperMask = 0x80000000u;
        constexpr uint32_t kLowerMask = 0x7FFFFFFEu; // binary uses 0x7FFFFFFE (note: not 0x7FFFFFFF)

        // Aligned with libkrkr2.so sub_A2BDBC: seed = steady_clock::now()/1000000.
        uint32_t blinkSeed() {
            const auto now = std::chrono::steady_clock::now()
                                 .time_since_epoch()
                                 .count();
            // Binary truncates to (int) then /1000000 then takes the low 32 bits.
            return static_cast<uint32_t>(static_cast<int>(now) / 1000000);
        }

        // Aligned with libkrkr2.so sub_9F17D0 state regeneration (the
        //   `if (left==1 || left==0)` batch-recompute of all 624 words via the
        //   MT recurrence). The binary unrolls this into three loops; the
        //   canonical MT _M_gen_rand below is value-identical.
        void regenerate(EmoteBlinkMt19937* s) {
            uint32_t* mt = s->mt;
            for (int i = 0; i < 624; ++i) {
                const uint32_t y =
                    (mt[i] & kUpperMask) | (mt[(i + 1) % 624] & kLowerMask);
                const uint32_t mag = (y & 1u) ? kMatrixA : 0u;
                mt[i] = mt[(i + 397) % 624] ^ (y >> 1) ^ mag;
            }
            s->pos  = 0;
            s->left = 624;
        }

        // The shared tempering applied to a raw state word (binary inline
        //   expression in sub_9F17D0: y ^= y>>11; y ^= (y<<7)&0x9D2C5680;
        //   y ^= (y<<15)&0xEFC60000; y ^= y>>18).
        uint32_t temper(uint32_t y) {
            y ^= (y >> 11);
            y ^= (y << 7) & 0x9D2C5680u;
            y ^= (y << 15) & 0xEFC60000u;
            y ^= (y >> 18);
            return y;
        }

        // Draw one tempered 32-bit word from the engine (regenerate the 624-word
        //   block when exhausted), mirroring the per-word consume + `if (left==1)
        //   regenerate` bookkeeping in sub_9F17D0. `left` counts words remaining
        //   in the current block (binary *(state+8)); when it reaches the
        //   exhaustion boundary the block is recomputed and the cursor reset.
        uint32_t nextWord(EmoteBlinkMt19937* s) {
            const int left0 = s->left; // v1 = *(a1+8)
            s->left = left0 - 1;        // *(a1+8) = v1 - 1
            if (left0 == 1) {           // exhausted (or first draw post-init) -> regen
                regenerate(s);
            }
            const uint32_t y = s->mt[s->pos];
            s->pos += 1;
            return temper(y);
        }

    } // namespace

    // Aligned with libkrkr2.so sub_9F1A08.
    //   v0 = operator new(0x1398); v1 = sub_A2BDBC(v0);  // seed
    //   *(v0+8)=1; *(v0+24)=v1;                          // left=1; mt[0]=seed
    //   for (v2=4; v2!=627; v2++)                        // mt[1..623]
    //       v1 = (v2+1) + 1812433253*((v1>>30)^v1) - 3;  // = 1812433253*(...) + (v2-2)
    //       *(v0+8*v2) = v1;
    //   *(v0+16)=v0+24; *(v0+8)=1;                       // pos=0; left=1
    // The mt[] words live at QWORD indices 3..626 (v0+24 = index 3), so
    //   mt[i] is at v2=i+3 and the additive term v2-2 == i+1.
    EmoteBlinkMt19937* EmoteBlinkRng_get() {
        if (!g_blinkRng) {                       // if (!qword_1AF7E80)
            EmoteBlinkMt19937* s = new EmoteBlinkMt19937(); // operator new(0x1398)
            uint32_t v1 = blinkSeed();           // v1 = sub_A2BDBC(v0)
            s->mt[0] = v1;                        // *(v0+24) = v1  (index 3 = mt[0])
            for (int v2 = 4; v2 != 627; ++v2) {   // fill mt[1..623]
                // v1 = (v2+1) + 1812433253*((v1>>30)^v1) - 3  ==  *_M_x + (v2-2)
                v1 = static_cast<uint32_t>(v2 + 1) +
                     1812433253u * ((v1 >> 30) ^ v1) - 3u;
                s->mt[v2 - 3] = v1;               // *(v0 + 8*v2)
            }
            s->pos  = 0;                          // *(v0+16) = v0+24
            s->left = 1;                          // *(v0+8) = 1
            s->initialized = true;
            g_blinkRng = s;                       // qword_1AF7E80 = v0
        }
        return g_blinkRng;
    }

    // Aligned with libkrkr2.so sub_9F17D0.
    //   left = *(state+8); *(state+8) = left - 1;
    //   if (left == 1) regenerate();             // first word after init/exhaust
    //   word1 = mt[pos]; advance; if (--left == 0) regenerate(); word2 = mt[pos];
    //   advance;
    //   low  = temper(word1);                    // 32 mantissa bits
    //   high = temper(word2);                    // top 20 mantissa bits
    //   bits = (uint32)low | (((high>>18 ^ high) & 0xFFFFF) << 32)
    //          | 0x3FF0000000000000;             // double in [1,2)
    //   return *(double*)&bits - 1.0;
    double EmoteBlinkRng_next(EmoteBlinkMt19937* self) {
        // Two consecutive engine words: word1 -> low 32 mantissa bits, word2 ->
        //   top 20 mantissa bits (the binary inlines this 53-bit canonical-real
        //   build at 0x9f19d0). Both come from the same MT stream via nextWord.
        const uint32_t low  = nextWord(self);      // v26 -> v27
        const uint32_t high = nextWord(self);      // v44

        // Build a double in [1,2): mantissa = low (32 bits) | top 20 bits of
        //   high, exponent = 0x3FF, then subtract 1.0 for [0,1).
        const uint64_t mantHi =
            static_cast<uint64_t>(((high >> 18) ^ high) & 0xFFFFFu) << 32;
        const uint64_t bits = static_cast<uint64_t>(low) | mantHi |
                              0x3FF0000000000000ull;
        double v45;
        static_assert(sizeof(double) == sizeof(uint64_t), "");
        __builtin_memcpy(&v45, &bits, sizeof(double));
        return v45 - 1.0;                          // [1,2) - 1 = [0,1)
    }

} // namespace motion
