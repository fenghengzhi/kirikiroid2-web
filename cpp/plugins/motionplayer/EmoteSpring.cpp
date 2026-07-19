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
#include <cstring>

#include "MotionDispatch.h"

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

    // ========================================================================
    // EmoteBustChainSpring_step — line-by-line port of libkrkr2.so sub_6689A4
    //   @ 0x6689A4. The bust chain solver. Two segments (v28 in {0,1}).
    //
    // Faithful representation note (CLAUDE.md): the binary indexes the object
    // with `a1 + 12*v28` / `a1 + 8*v28` (per-segment field strides), so the
    // step is ported against a raw byte base with explicit `*(float*)(p+off)`
    // accesses mirroring the decompile 1:1, rather than re-deriving per-field
    // names (which would risk mislabeling the interleaved +92/+96 QWORD copy
    // and the +12*v28 segment arithmetic). Field meanings are documented in
    // EmoteBustChainSpring (EmoteSpring.h). All float literals preserved exactly
    // (0.03125, 6.28318531, 28.0, 0.015625, 0.0451603944, 0.0392699082, 4.0).
    //
    // Signature mirrors the binary:
    //   sub_6689A4(a1, a2/*outSeg0*/, a3/*outSeg1*/, a4/*outLastY*/,
    //              a5, a6, a7, a8, a9/*dt*/, a10, a11/*angleRad*/)
    // (a2/a3 are the two per-segment X-angle outputs selected by `v28 ? a3 : a2`;
    //  a4 is the last-segment Y-angle output.)
    // ========================================================================
    void EmoteBustChainSpring_step(EmoteBustChainSpring* self,
                                   float* a2, float* a3, float* a4,
                                   float a5, float a6, float a7, float a8,
                                   float a9, float a10, float a11) {
        // Raw byte base — `*(float*)(p+off)` reproduces the decompile verbatim.
        uint8_t* const a1 = reinterpret_cast<uint8_t*>(self);
        auto F  = [a1](int off) -> float&  { return *reinterpret_cast<float*>(a1 + off); };
        auto I  = [a1](int off) -> int32_t& { return *reinterpret_cast<int32_t*>(a1 + off); };

        float v20, v21, v22, v23, v24, v30, v31, v34, v35;

        // if (*(_BYTE *)a1) { init } else { accumulate }            /*0x6689d8*/
        if (self->firstFlag) {
            v20 = F(80);                       // *(a1+80)            /*0x668a00*/
            v21 = F(84);                       // *(a1+84)
            self->firstFlag = 0;               // *(_BYTE*)a1 = 0     /*0x668a04*/
            F(72) = v20 - a5;                  // *(a1+72)            /*0x668a10*/
            F(76) = v21 - a6;                  // *(a1+76)
        } else {
            v22 = F(76) + a6;                  // *(a1+76)+a6         /*0x668a20*/
            F(80) = F(72) + a5;                // *(a1+80)=*(a1+72)+a5 /*0x668a24*/
            F(84) = v22;                       // *(a1+84)
        }

        // *(_QWORD *)(a1 + 92) = *(_QWORD *)(a1 + 80);              /*0x668a30*/
        F(92) = F(80);
        F(96) = F(84);
        v24 = F(36);                            // restLen0           /*0x668a38*/
        v23 = F(40);                            // restLen1
        int32_t v25 = I(88);                    // rootFlag           /*0x668a3c*/
        F(96) = v24 + F(96);                    // accum +restLen0    /*0x668a44*/
        const float v26_lo = F(92);             // v26 QWORD lo       /*0x668a48*/
        const float v26_hi = F(96);             // v26 QWORD hi
        I(100) = v25;                           //                    /*0x668a4c*/
        I(112) = v25;                           //                    /*0x668a50*/
        F(104) = v26_lo;                        // *(a1+104)=v26      /*0x668a54*/
        F(108) = v26_hi;
        F(108) = v23 + F(108);                  // +restLen1          /*0x668a60*/

        const float v27 = std::sin(-a11);       // sinf(-a11)         /*0x668a6c*/
        const float v93 = std::cos(a11);        // cosf(a11)          /*0x668a80*/
        const float v94 = v27;
        // v92 = ((v93*a7) - (v27*a8)) * a9                          /*0x668acc*/
        const float v92 = ((v93 * a7) - (v27 * a8)) * a9;
        // v29 = ((v27*a7) + (v93*a8)) * a9                          /*0x668ad0*/
        const float v29 = ((v27 * a7) + (v93 * a8)) * a9;

        int64_t v28 = 0;                        // segment index      /*0x668ac0*/
        while (true) {
            // ---- spring constraint to the previous chain point ---- /*0x668bf8*/
            const int seg12 = 12 * static_cast<int>(v28);
            const float v48 = F(116 + seg12);    // v47 = *(a1+116+12*v28) /*0x668bfc*/
            // v49 = v46-3 floats (= +104+12*v28); for v28==0 -> a1+80.
            const int prevBase = (v28 == 0) ? 80 : (116 + seg12 - 12);
            const float v50 = F(prevBase + 0);   // *v49               /*0x668c10*/
            const float v51 = F(prevBase + 4);   // v49[1]
            const float v53 = F(prevBase + 8);   // v49[2]             /*0x668c18*/
            const float v55 = v50 - v48;         //                    /*0x668c24*/
            const float v56 = v51 - F(120 + seg12); // v51 - v46[1]    /*0x668c28*/
            const float v57 = v53 - F(124 + seg12); // v53 - v46[2]    /*0x668c30*/
            const float v58 = ((v55 * v55) + (v56 * v56)) + (v57 * v57); /*0x668c40*/
            if (v58 > (v24 * v24)) {             //                    /*0x668c4c*/
                const float v59 = std::sqrt(v58);// sqrtf              /*0x668c50*/
                if (v59 > 0.015625f) {           //                    /*0x668c5c*/
                    const float v60 = v55 * (1.0f / v59);              /*0x668c6c*/
                    const float v61 = v56 * (1.0f / v59);
                    const float v62 = v57 * (1.0f / v59);
                    const float v63 = v59 - v24;  //                    /*0x668c78*/
                    if (v28 == 1) {               //                    /*0x668c7c*/
                        const float v64 = v63 * v60;                   // /*0x668c88*/
                        const float v65 = (v63 * v61) + F(132);        // /*0x668c98*/
                        const float v66 = F(152);  //                    /*0x668c9c*/
                        const float v67 = F(156);
                        const float v68 = (v63 * v62) + F(136);        // /*0x668ca0*/
                        const float v69 = F(160);  //                    /*0x668ca4*/
                        F(128) = v64 + F(128);     //                    /*0x668ca8*/
                        F(132) = v65;
                        const float v70 = F(20);   // forceScale1        /*0x668cac*/
                        F(136) = v68;
                        // v71 = (v70 * ((v60*v66)+(v61*v67)+(v62*v69))) * a9  /*0x668ccc*/
                        const float v71 = (v70 * (((v60 * v66) + (v61 * v67)) + (v62 * v69))) * a9;
                        F(152) = v66 - (v60 * v71); //                   /*0x668ce8*/
                        F(156) = v67 - (v61 * v71);
                        F(160) = v69 - (v62 * v71); //                   /*0x668cec*/
                    } else {
                        // v72 = a1 + 12*v28 (=a1 for v28==0); v72[35..37] = +140..+148
                        const float v73 = (v63 * F(16)) * a9;          // /*0x668d0c*/
                        const float v74 = (v61 * v73) + F(144 + seg12); // v72[36] /*0x668d20*/
                        const float v75 = (v62 * v73) + F(148 + seg12); // v72[37] /*0x668d24*/
                        F(140 + seg12) = F(140 + seg12) + (v60 * v73);  // v72[35] /*0x668d28*/
                        F(144 + seg12) = v74;
                        F(148 + seg12) = v75;       //                   /*0x668d2c*/
                    }
                }
            }

            // ---- integrate this segment ---- (v76=a1+12*v28; v77=+140+12*v28) /*0x668d34*/
            const float v78 = F(140 + seg12);     // *(v76+140)         /*0x668d38*/
            F(140 + seg12) = v92 + v78;           // *v77               /*0x668d4c*/
            const float v79 = v29 + F(144 + seg12); // v77[1]           /*0x668d54*/
            F(144 + seg12) = v79;                 //                    /*0x668d58*/
            const float v80 = (a9 * 0.0f) + F(148 + seg12); // v77[2]   /*0x668d64*/
            F(148 + seg12) = v80;                 //                    /*0x668d68*/
            const float v81 = F(4) * a9;          // *(a1+4)*a9         /*0x668d74*/
            const float v82 = v93 * v81;          //                    /*0x668d7c*/
            const float v83 = v81 * 0.0f;         //                    /*0x668d80*/
            v31 = (v92 + v78) - (v94 * v81);      //                    /*0x668d84*/
            const float v84 = v82 + v79;          //                    /*0x668d88*/
            const float v85 = v83 + v80;          //                    /*0x668d8c*/
            F(140 + seg12) = v31;                 // *v77               /*0x668d90*/
            F(144 + seg12) = v84;                 // v77[1]             /*0x668d94*/
            F(148 + seg12) = v83 + v80;           // v77[2]             /*0x668d98*/

            // ---- optional collision-depth curve lookup at +168 ----  /*0x668d9c*/
            const float v87 = F(116 + seg12);     // *v46               /*0x668da0*/
            const uint8_t* v86 = *reinterpret_cast<uint8_t* const*>(a1 + 168);
            if (v86) {                            //                    /*0x668da4*/
                const float* v88 = reinterpret_cast<const float*>(v86 + 4); // +4 /*0x668da8*/
                v30 = 0.0f;                        // default (v88 exhausted)
                for (int64_t i = -1; i < 127; ++i) { //                 /*0x668dac*/
                    if (*reinterpret_cast<const uint8_t*>(
                            reinterpret_cast<const uint8_t*>(v88) - 4)) { // *((BYTE*)v88-4) /*0x668db0*/
                        const float v90 = v88[1];  //                   /*0x668db8*/
                        const float v91 = v90 * 0.5f + 4.0f;            // /*0x668dc8*/
                        if ((v88[0] - v91) < v87 && (v88[0] + v91) > v87) { // /*0x668de0*/
                            v30 = v90 * *reinterpret_cast<const float*>(v86 + 1556); // /*0x668af0*/
                            break;                  // goto LABEL_6
                        }
                    }
                    v88 += 3;                       //                   /*0x668dec*/
                }
                v31 = v30 + v31;                    //                   /*0x668af4*/
                F(140 + seg12) = v31;               // *v77              /*0x668af8*/
            }

            // ---- velocity damping + position/angle output ----       /*0x668b00*/
            const float v32 = v85 * a9;            //                   /*0x668b00*/
            // v33 = a1 + 8*v28; v34 = v31 - v31*(*(a1+8))*a9
            v34 = v31 - ((v31 * F(8)) * a9);       //                   /*0x668b14*/
            F(140 + seg12) = v34;                  // *v77              /*0x668b18*/
            v35 = (v34 * a9) + v87;                //                   /*0x668b24*/
            float* const v36 = (v28 ? a3 : a2);    // seg X-angle sink  /*0x668b2c*/
            const float v37 = v84 - ((v84 * F(12)) * a9);  //           /*0x668b34*/
            F(144 + seg12) = v37;                  // v77[1]            /*0x668b38*/
            F(116 + seg12) = v35;                  // *v46              /*0x668b3c*/
            const float v39 = (v37 * a9) + F(120 + seg12); // *v52      /*0x668b4c*/
            F(120 + seg12) = v39;                  //                   /*0x668b50*/
            F(124 + seg12) = v32 + F(124 + seg12); // *v54              /*0x668b5c*/
            const float v40 = F(96 + seg12);       // *(v38+96)         /*0x668b60*/
            // v41 = (((*(v38+92) - v35) * *(v33+56)) * a10) * -0.0451603944
            const int seg8 = 8 * static_cast<int>(v28);
            const float v41 = (((F(92 + seg12) - v35) * F(56 + seg8)) * a10) * -0.0451603944f; /*0x668b80*/
            const float v42 = std::atan(v41) / 0.0392699082f;  //       /*0x668b90*/
            *v36 = v42;                            //                   /*0x668b94*/
            if (v28 == I(24)) {                    // v28 == lastSeg    /*0x668ba0*/
                // v43 = (((*(a1+44) - (v40 - v39)) * *(v33+60)) * a10) * 0.0451603944
                const float v43 = (((F(44) - (v40 - v39)) * F(60 + seg8)) * a10) * 0.0451603944f; /*0x668bc8*/
                const float v44 = std::atan(v43) / 0.0392699082f;  //   /*0x668bd8*/
                *a4 = v44;                          //                  /*0x668bdc*/
            }
            if (++v28 == 2)                         //                  /*0x668be8*/
                break;
            v24 = F(36 + 4 * static_cast<int>(v28)); // next rest length /*0x668bf0*/
        }
    }

    // ========================================================================
    // Spring-state constructors (population path). These read per-node physics
    // params from raw TJS dispatches exactly as the binary's Motion_propGetDouble path
    // does: the value is fetched as a true double (type-correct conversion) and
    // narrowed to float (`*(float*)&v = *(double*)&v` in the binary = an FCVT
    // narrowing store, NOT a bit-reinterpret), then the raw float bits are stored.
    // Absent keys default to 0.0 (the .bss default-value constants 1AB7E8C.. are
    // all zero; only E68/E70/E74/E7C/E80/E88 are init'd by emoteplayer_static_init
    // @0x42eb28, and those feed the rest-pos init below, not the prop defaults).
    // ========================================================================
    // Aligned with libkrkr2.so sub_662448 @ 0x662448 (EmoteSpringState ctor).
    void EmoteSpringState_ctor(EmoteSpringState* self,
                               const tTJSVariant& dict) {
        // firstFlag = 1; stored/pos/vel seeded from .bss zeros (E68/E70 = 0). /*0x662478*/
        self->firstFlag  = 1;
        self->storedX = 0.0f; self->storedY = 0.0f; self->storedZ = 0.0f; // +36/+40/+44 = E68/E70
        self->posX    = 0.0f; self->posY    = 0.0f; self->posZ    = 0.0f; // +48/+52/+56
        self->velX    = 0.0f; self->velY    = 0.0f; self->accZ    = 0.0f; // +60/+64/+68

        // Per-node spring params (narrow double->float, raw bits).             /*0x662524..*/
        self->k_a = static_cast<float>(detail::motionPropGetDouble(
            dict, TJS_W("gravity")));                                  // +4  0x66252c
        self->k_b = static_cast<float>(detail::motionPropGetDouble(
            dict, TJS_W("spring")));                                   // +8  0x662554
        self->drag = static_cast<float>(detail::motionPropGetDouble(
            dict, TJS_W("friction")));                                // +12 0x66257c
        self->leverX = static_cast<float>(detail::motionPropGetDouble(
            dict, TJS_W("scale_x")));                                 // +20 0x6625a4
        self->leverY = static_cast<float>(detail::motionPropGetDouble(
            dict, TJS_W("scale_y")));                                 // +24 0x6625cc
    }

    // Aligned with libkrkr2.so sub_668EF8 @ 0x668EF8 (EmoteBustChainSpring ctor).
    void EmoteBustChainSpring_ctor(EmoteBustChainSpring* self,
                                   const tTJSVariant& dict) {
        uint8_t* const a1 = reinterpret_cast<uint8_t*>(self);
        auto F = [a1](int off) -> float&   { return *reinterpret_cast<float*>(a1 + off); };
        auto I = [a1](int off) -> int32_t& { return *reinterpret_cast<int32_t*>(a1 + off); };

        // firstFlag=1; +48=0; collisionCurve(+168)=0; rootX(+80)=0; memset(+92,0,0x48). /*0x668f2c..*/
        self->firstFlag = 1;
        F(48) = 0.0f;
        self->collisionCurve = nullptr;            // +168 = 0
        F(80) = 0.0f; F(84) = 0.0f;                // +80 (rootX/Y) QWORD = E68 = 0
        std::memset(a1 + 92, 0, 0x48u);            // +92..+163 zeroed

        // Scalar params (narrow double->float, raw bits).                       /*0x668fdc..*/
        F(4) = static_cast<float>(detail::motionPropGetDouble(
            dict, TJS_W("gravity")));              // +4   0x668fdc
        F(8) = static_cast<float>(detail::motionPropGetDouble(
            dict, TJS_W("friction_x")));           // +8   0x669004
        F(12) = static_cast<float>(detail::motionPropGetDouble(
            dict, TJS_W("friction_y")));           // +12  0x66902c
        F(16) = static_cast<float>(detail::motionPropGetDouble(
            dict, TJS_W("b_rate")));               // +16  0x669054
        F(20) = static_cast<float>(detail::motionPropGetDouble(
            dict, TJS_W("v_bound")));              // +20  0x66907c
        I(24) = detail::motionPropGetInt(
            dict, TJS_W("ud_eft"));                // +24  0x6690a8
        F(28) = static_cast<float>(detail::motionPropGetDouble(
            dict, TJS_W("bend_spd")));             // +28  0x6690c8
        F(32) = static_cast<float>(detail::motionPropGetDouble(
            dict, TJS_W("bend_vol")));             // +32  0x6690f0

        // 2-element lists: length / scale_x / scale_y via index 0/1.            /*0x669128..*/
        const tTJSVariant length = detail::motionPropGet(
            dict, TJS_W("length"));                // 0x669128
        F(36) = static_cast<float>(
            detail::motionPropGetDoubleByNum(length, 0)); // +36 0x66919c
        F(40) = static_cast<float>(
            detail::motionPropGetDoubleByNum(length, 1)); // +40 0x6691b8
        const tTJSVariant scaleX = detail::motionPropGet(
            dict, TJS_W("scale_x"));               // 0x66920c
        F(56) = static_cast<float>(
            detail::motionPropGetDoubleByNum(scaleX, 0)); // +56 0x669280
        F(64) = static_cast<float>(
            detail::motionPropGetDoubleByNum(scaleX, 1)); // +64 0x66929c
        const tTJSVariant scaleY = detail::motionPropGet(
            dict, TJS_W("scale_y"));               // 0x6692f0
        F(60) = static_cast<float>(
            detail::motionPropGetDoubleByNum(scaleY, 0)); // +60 0x669364
        F(68) = static_cast<float>(
            detail::motionPropGetDoubleByNum(scaleY, 1)); // +68 0x669380

        // Rest positions from the rest unit vector (0,1,0)
        //   (= floats of qword_1AB7E74 {0.0f,1.0f} and dword_1AB7E7C {0.0f}).   /*0x6693b0..*/
        const float restLen0 = F(36);
        const float restLen1 = F(40);
        const float ux = kQword1AB7E74_0; // 0.0f
        const float uy = kQword1AB7E74_1; // 1.0f
        const float uz = 0.0f;            // dword_1AB7E7C
        F(92)  = restLen0 * ux;            // +92  seg0 rest x
        F(96)  = restLen0 * uy;            // +96  seg0 rest y
        F(100) = restLen0 * uz;            // +100 seg0 rest z
        // +116/+120/+124 = copy of +92/+96/+100 (QWORD +116=+92, DWORD +124=+100).
        F(116) = F(92); F(120) = F(96); F(124) = F(100);
        F(104) = restLen1 * ux;            // +104 seg1 rest x
        F(108) = restLen1 * uy;            // +108 seg1 rest y
        F(112) = restLen1 * uz;            // +112 seg1 rest z
        // +128/+136 = copy(+104/+112): QWORD +128=+104(=x,y), DWORD +136=+112(z).
        F(128) = F(104); F(132) = F(108); F(136) = F(112);
        // +140/+148 = E68/E70 = 0 (seg0 vel); +152/+160 = E68/E70 = 0 (seg1 vel). /*0x669414..*/
        F(140) = 0.0f; F(144) = 0.0f; F(148) = 0.0f;
        F(152) = 0.0f; F(156) = 0.0f; F(160) = 0.0f;
    }

} // namespace motion
