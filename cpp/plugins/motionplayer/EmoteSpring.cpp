// EmoteSpring implementation — line-by-line port of libkrkr2.so
//   EmotePhysics_springStep @ 0x662768.
//
// Reverse-engineering evidence (this conversation):
//   - decompile of 0x662768 (full pseudocode)
//   - disasm 0x6627e0..0x662964 confirming `LDP S19,S20,[X8]` loads the two
//     floats of qword_1AB7E74.
//   - qword_1AB7E74 is a .bss symbol; emoteplayer_static_init @0x42eb28 writes
//     `qword_1AB7E74 = 0x3F80000000000000`, i.e. LE float[0]=0.0f, float[1]=1.0f.
//     So *(float*)&qword_1AB7E74 == 0.0f, *((float*)&qword_1AB7E74 + 1) == 1.0f.
//
// Float literals (0.0392699082 = pi/80, 0.0451603944) preserved exactly per
// CLAUDE.md (no M_PI substitution).

#include "EmoteSpring.h"

#include <cmath>

namespace motion {

    // The two floats of qword_1AB7E74 (rest/base 2D unit vector (0,1)).
    // Aligned with libkrkr2.so qword_1AB7E74 @ 0x1AB7E74 (.bss; runtime value
    // 0x3F80000000000000 set by emoteplayer_static_init @0x42eb28).
    static const float kQword1AB7E74_0 = 0.0f; // *(float*)&qword_1AB7E74
    static const float kQword1AB7E74_1 = 1.0f; // *((float*)&qword_1AB7E74 + 1)

    // Aligned with libkrkr2.so EmotePhysics_springStep at 0x662768.
    float EmotePhysics_springStep(EmoteSpringState* a1,
                                  float* a2, float* a3,
                                  float a4, float a5, float a6, float a7,
                                  float a8, float a9, float a10) {
        float v18, v19;

        // if (*(_BYTE *)a1) { first-frame init } else { accumulate }  /*0x66278c*/
        if (a1->firstFlag) {
            v18 = a1->storedX;                 // *(float*)(a1+36)  /*0x6627b0*/
            v19 = a1->storedY;                 // *(float*)(a1+40)
            a1->firstFlag = 0;                 // *(_BYTE*)a1 = 0   /*0x6627b4*/
            a1->prevDeltaX = v18 - a4;         // *(float*)(a1+28)  /*0x6627c0*/
            a1->prevDeltaY = v19 - a5;         // *(float*)(a1+32)
        } else {
            v18 = a1->prevDeltaX + a4;          // *(float*)(a1+28)+a4  /*0x6627cc*/
            v19 = a1->prevDeltaY + a5;          // *(float*)(a1+32)+a5  /*0x6627d0*/
            a1->storedX = v18;                  // *(float*)(a1+36)     /*0x6627d4*/
            a1->storedY = v19;                  // *(float*)(a1+40)
        }

        const float v20 = sinf(-a10);           // /*0x6627e0*/
        const float v21 = cosf(a10);            // /*0x6627e8*/
        const float v22 = a1->posX;             // *(float*)(a1+48)   /*0x6627f0*/
        const float v23 = a1->k_b * a8;         // *(float*)(a1+8)*a8 /*0x662814*/
        const float v24 = a1->posY;             // *(float*)(a1+52)   /*0x66281c*/
        const float v25 = a1->posZ;             // *(float*)(a1+56)

        // v26 = ((v21*a6 - v20*a7)*a8) + (v23*(v18 - v22))           /*0x66282c*/
        const float v26 = ((v21 * a6) - (v20 * a7)) * a8
                        + (v23 * (v18 - v22));
        // v27 = v23*(v19 - v24)                                      /*0x66284c*/
        const float v27 = v23 * (v19 - v24);
        const float v28 = a1->k_a * a8;          // *(float*)(a1+4)*a8 /*0x662870*/
        // v29 = *(float*)(a1+68) + v23*(*(float*)(a1+44) - v25)      /*0x662874*/
        const float v29 = a1->accZ + (v23 * (a1->storedZ - v25));
        const float v30 = a1->drag * a8;         // *(float*)(a1+12)*a8 /*0x662884*/

        // v31 = ((v21*Q0 - v20*Q1)*v28) + (*(a1+60) + v26)          /*0x662888*/
        const float v31 = ((v21 * kQword1AB7E74_0) - (v20 * kQword1AB7E74_1)) * v28
                        + (a1->velX + v26);
        // v32 = ((v20*Q0 + v21*Q1)*v28) + (*(a1+64) + (((v20*a6 + v21*a7)*a8) + v27)) /*0x662890*/
        const float v32 = ((v20 * kQword1AB7E74_0) + (v21 * kQword1AB7E74_1)) * v28
                        + (a1->velY + (((v20 * a6) + (v21 * a7)) * a8) + v27);

        const float v33 = v29 - (v30 * v29);     // /*0x662894*/
        const float v34 = v31 - (v30 * v31);     // /*0x6628a0*/
        const float v35 = v32 - (v30 * v32);     // /*0x6628a4*/
        const float v36 = a1->leverX;            // *(float*)(a1+20) /*0x6628a8*/
        a1->velX = v34;                          // *(float*)(a1+60) /*0x6628ac*/
        a1->velY = v35;                          // *(float*)(a1+64)
        const float v37 = v22 + (v34 * a8);      // /*0x6628b8*/
        a1->accZ = v33;                          // *(float*)(a1+68) /*0x6628c0*/
        a1->posX = v37;                          // *(float*)(a1+48) /*0x6628cc*/
        const float v38 = v24 + (v35 * a8);      // /*0x6628d8*/
        a1->posY = v38;                          // *(float*)(a1+52) /*0x6628e0*/
        a1->posZ = v25 + (v33 * a8);             // *(float*)(a1+56)

        const float v39 = v19 - v38;             // /*0x6628e4*/
        // v40 = -(((v18 - v37)*a9)*v36) * 0.0451603944              /*0x6628f4*/
        const float v40 = -(((v18 - v37) * a9) * v36) * 0.0451603944f;
        const float v41 = atanf(v40) / 0.0392699082f; // /*0x662910*/
        *a2 = v41;                               // /*0x662914*/

        // v42 = ((-(v39*a9) - *(a1+16)) * *(a1+24)) * 0.0451603944  /*0x662930*/
        const float v42 = ((-(v39 * a9) - a1->biasY) * a1->leverY) * 0.0451603944f;
        const float result = atanf(v42) / 0.0392699082f; // /*0x662940*/
        *a3 = result;                            // /*0x662944*/
        return result;                           // /*0x662964*/
    }

} // namespace motion
