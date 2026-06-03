#include "EmoteWindEmitter.h"
#include "EmoteBlinkRng.h"

#include <cstring>

namespace motion {

    // Aligned with libkrkr2.so EmoteWindEmitter_init (sub_670AFC @0x670AFC).
    //   void __fastcall (windObj a1, float startPos a2, float endPos a3):
    //     *(float*)(a1+1536) = a2;            // startPos
    //     for i in 0..127: *(byte*)(a1 + 12*i) = 0;  // slot.active
    //     *(float*)(a1+1540) = a3;            // endPos
    //     *(byte*)(a1+1544) = 0;              // gate
    //     *(OWORD*)(a1+1548) = {1.0f, 0.0f};  // yHi, yLo defaults (xmmword_14D68C0)
    //   (startWind overwrites yHi/yLo + gate immediately after.)
    void EmoteWindEmitter::init(float startPos_, float endPos_) {
        startPos = startPos_;                  /*0x670b00*/
        for (int i = 0; i < 128; ++i) {        /*0x670b0c..0x670d08 (128 STRB)*/
            slots[i].active = 0;
        }
        endPos = endPos_;                      /*0x670d0c*/
        gate = 0;                              /*0x670d10*/
        yHi = 1.0f;                            /*0x670d14 xmmword_14D68C0 = {1.0f,0.0f}*/
        yLo = 0.0f;
    }

    // Aligned with libkrkr2.so EmoteWindEmitter_step (sub_6687E8 @0x6687E8).
    //   void __fastcall (windObj a1, float dt a2):
    //     v4 = |*(float*)(a1+1556)|;                       // |velocity|
    //     v5 = v4*a2 + *(float*)(a1+1560);  *(a1+1560)=v5;  // emit accumulator
    //     if (v5 >= 0.0) do {
    //         if ((float)rng() < 0.0625) {                  // emission probability
    //             walk slots 0..127 for first inactive (v8>126 -> give up);
    //             slot.active=1;
    //             slot.lifePos(+4) = *(DWORD*)(a1+1536);    // startPos bits
    //             slot.yPos(+8)    = yLo + (yHi-yLo)*rng(); // +1552 + (+1548-+1552)*r
    //         }
    //         v14 = *(a1+1560) - 1.0; *(a1+1560)=v14;
    //     } while (v14 >= 0.0);
    //     for (i=0; i!=1536; i+=12) if (slot.active) {       // advance + kill
    //         v16 = slot.lifePos + (*(float*)(a1+1556) * a2); slot.lifePos = v16;
    //         v17 = *(float*)(a1+1556);
    //         if ((v17 > 0.0 && v16 > *(float*)(a1+1540))
    //          || (v17 < 0.0 && v16 < *(float*)(a1+1540))) slot.active = 0;
    //     }
    void EmoteWindEmitter::step(float dt) {
        float v4 = velocity;                          /*0x66880c*/
        if (v4 < 0.0f)                                /*0x66881c*/
            v4 = -v4;
        float v5 = (v4 * dt) + emitAccumulator;       /*0x668824*/
        emitAccumulator = v5;                         /*0x66882c*/
        if (v5 >= 0.0f) {                             /*0x668830*/
            do {                                      /*0x6688bc*/
                const float v7 =
                    static_cast<float>(EmoteBlinkRng_next(EmoteBlinkRng_get())); /*0x668844-0x66884c*/
                if (v7 < 0.0625f) {                   /*0x668854 dword_14D6788 = 0.0625f*/
                    // Find first inactive slot. Binary pointer-walk @0x668858:
                    //   v8 = -1; v9 = &slots[0];
                    //   while (*v9) { ++v8; v9 += 12; if (v8 > 126) goto skip; }
                    //   *v9 = 1; ...
                    // The give-up triggers once v8 reaches 127 (idx == 128); i.e.
                    // when all 128 slots are active. The binary's last iteration
                    // over-reads one element past the pool (slot[128] = the +1536
                    // control word) before bailing — a benign read; we hoist the
                    // bound check ahead of the access so idx==128 bails directly,
                    // producing the identical observable result (use first
                    // inactive slot in [0,127]; otherwise skip emission this draw).
                    long v8 = -1;                     /*0x668858*/
                    int idx = 0;
                    while (idx != 128 && slots[idx].active) { /*0x668864/0x668874 walk+guard*/
                        ++v8;                         /*0x668868*/
                        ++idx;                        /*0x668870 v9 += 12*/
                    }
                    if (idx != 128) {                 /*0x668874 v8>126 => give up*/
                        (void)v8;
                        slots[idx].active = 1;        /*0x66887c STRB W20(=1)*/
                        // slot.lifePos = *(DWORD*)(a1+1536) — copy startPos bits.
                        std::memcpy(&slots[idx].lifePos, &startPos, sizeof(float)); /*0x668884*/
                        const float v10 = yLo;        /*0x668888 *(a1+1552)*/
                        const float v11 = yHi - v10;  /*0x668890 *(a1+1548) - v10*/
                        const float v13 =
                            static_cast<float>(EmoteBlinkRng_next(EmoteBlinkRng_get())); /*0x668894-0x66889c*/
                        slots[idx].yPos = v10 + (v11 * v13); /*0x6688a8*/
                    }
                }
                // LABEL_10 (0x6688ac): both the emit-branch and the skip path
                //   fall through here to decrement the accumulator.
                const float v14 = emitAccumulator + -1.0f; /*0x6688ac*/
                emitAccumulator = v14;                     /*0x6688b8*/
                if (!(v14 >= 0.0f))                         /*0x6688bc B.GE*/
                    break;
            } while (true);
        }
        for (int i = 0; i != 128; ++i) {              /*0x6688c0 i != 1536, i += 12*/
            if (slots[i].active) {                    /*0x6688c4*/
                const float v16 = slots[i].lifePos + (velocity * dt); /*0x66890c*/
                slots[i].lifePos = v16;
                const float v17 = velocity;
                if ((v17 > 0.0f && v16 > endPos)      /*0x668... compound predicate*/
                    || (v17 < 0.0f && v16 < endPos)) {
                    slots[i].active = 0;              /*0x668910*/
                }
            }
        }
    }

} // namespace motion
