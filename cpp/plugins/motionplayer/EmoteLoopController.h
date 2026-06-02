// EmoteLoopController — 0x20=32B controller for the emote "loopControl" category
//   (the LAST progress-stepped controller-deque, engine+736 = local member
//   `_lookupCurvesDeque10`). Aligned with libkrkr2.so:
//     builder : EmoteEngine_buildLoopControl (sub_66E480) @ 0x66E480
//     step    : INLINED into EmoteEngine_progress @ 0x67d2a0..0x67d370
//               (there is NO separate step function for this category — the
//                curve sampler is open-coded in the progress dt-slice loop)
//     HM#6    : type tag = 3 (EmoteEngine_HM6_findOrInsertVarRef @0x6885CC)
//
// POPULATION SOURCE VERDICT (cross-checked, NOT a single negative grep).
//   The deque whose libstdc++ begin._M_cur is at engine+736 is populated by
//   EXACTLY ONE builder: sub_66E480 (loopControl). The other candidates that
//   applyMetadata @0x67D4D0 dispatches after the named controller categories
//   were each decompiled and target DIFFERENT structures:
//     - clampControl (sub_66EE5C) -> deque header a1[60..]/cur a1[66]=engine+528,
//       40B elements, block 0x1E0=480; reads var_lr/var_ud/min/max. NOT +736.
//     - mirrorControl (sub_66F364) -> vector at engine+800 (variableMatchList),
//       a plain std::vector<tTJSVariant*>. NOT +736.
//     - instantVariableList (sub_66F64C) / timelineControl (sub_66F80C) — neither
//       touches engine+736.
//   sub_66E480 writes the deque via finish._M_cur a1[96]=engine+768 and
//   finish._M_node a1[99]=engine+792, i.e. the SAME deque the progress step reads
//   from begin._M_cur engine+736 (start={cur@736,first@744,last@752,node@760},
//   finish={cur@768,first@776,last@784,node@792}). 16B elements, block 0x200=512
//   (32 elements/block). Unique populator confirmed.
//
// BUILDER pseudocode (fresh decompile of sub_66E480 @0x66E480, this conversation):
//   count = Motion_propGetCount(loopControl);                       // 0x66e514
//   for (v6 = 0; v6 < count; ++v6) {                                // 0x66e550
//     elem = loopControl[v6];                                       // PropGet(0,v6)
//     if ((Motion_propGetBool(elem,"enabled") & 1) == 0) continue;  // 0x66e5f0 gate
//     transitionList = elem["transitionList"];                      // 0x66e61c
//     kfCount = Motion_propGetCount(transitionList);                // 0x66e6a0
//     ctl = operator new(0x20); zero(ctl);                          // 0x66e688 (+0..+31=0)
//     ctl.keys.resize(kfCount);                                     // 0x66e6c4..0x66e6f4
//     for (v20 = 0; v20 < kfCount; ++v20) {                         // 0x66e810 do/while
//       kf = transitionList[v20];                                   // 0x66e728
//       ctl.keys[v20].v0   = (float)propGetIndexDouble(kf,0);       // 0x66e7a8 STR S
//       ctl.keys[v20].v1   = (float)propGetIndexDouble(kf,1);       // 0x66e7c8 STR S
//       ctl.keys[v20].span = (float)propGetIndexDouble(kf,2);       // 0x66e7e4 STR S
//     }
//     push_back deque#10 {ctl, label=0};                            // 0x66e828 (16B elem)
//     label = elem["var_loop"];                                     // 0x66e8b4 (ttstr value)
//     deque#10.back().label = label;                                // 0x66e944 (AddRef slot)
//     ref = HM6_findOrInsert(engine+1384, &back().label);           // 0x66e964
//     ref->type = 3; ref->index = v6;                               // 0x66e96c
//   }
//   NOTE: the HM#6 key AND the deque element label are BOTH the "var_loop" value
//   (the same ttstr fed by sub_A0BAF4 @0x66e90c -> v45 -> stored both at the
//   element +8 slot @0x66e944 and used as the HM6 key @0x66e964). The HM#6 index
//   is the LOOP index v6 (a skipped/disabled element still advances v6).
//
// INLINE STEP pseudocode (fresh decompile of EmoteEngine_progress @0x67d2a0):
//   for each 16B {ctl, label} entry in deque#10:                    // 0x67d2c0
//     idx   = ctl.currentIndex;        // *(int*)(ctl+0)            // 0x67d2d0 LDR W
//     accum = ctl.accum + dt;          // *(float*)(ctl+4) + dt     // 0x67d2d4 FADD S
//     ctl.accum = accum;                                            // 0x67d2d8 STR S
//     count = (ctl.keysFinish - ctl.keysStart) / 12;  // #keyframes // 0x67d300..0x67d304
//     span  = ctl.keys[idx].span;      // *(float*)(kf+8)           // 0x67d2e4 LDR S
//     if (span <= accum) {                                          // 0x67d2ec FCMP/B.LS
//       do {                                                        // 0x67d308 do/while
//         idx   = (idx + 1) % count;                                // 0x67d310 MSUB
//         accum = accum - span;                                     // 0x67d318 FSUB S
//         span  = ctl.keys[idx].span;                               // 0x67d31c LDR S
//       } while (span <= accum);
//       ctl.accum = accum; ctl.currentIndex = idx;                  // 0x67d32c/0x67d330
//     }
//     t   = accum / span;                                           // 0x67d340 FDIV S
//     out = t*ctl.keys[idx].v1 + (1-t)*ctl.keys[idx].v0;            // 0x67d33c..0x67d354
//          // LDP S3,S2,[kf]: S3=v0(kf+0), S2=v1(kf+4); FMUL t*v1, (1-t)*v0
//     HM7[label] = (double)out;        // Player_HM2_upsert(+1440)  // 0x67d35c/0x67d360/0x67d36c
//   advance entry += 16 (2 qwords); block boundary node+64 (512B). // 0x67d364
//
// FLOAT-BITS NOTE (the M2 alignment trap): every numeric field here is read by
//   the binary with `LDR S` (single-precision load, NO SCVTF int->float). The
//   accum (+4), and every keyframe field (v0/v1/span) are RAW float bits. The
//   builder stores them via `STR S` after the propGetDouble narrows double->single
//   (FCVT). So the keyframe is a 12B POD {float v0; float v1; float span} and the
//   controller's accum is a float. There is NO integer-to-float remap anywhere in
//   this category — the only integer is currentIndex (a genuine LDR W index).
//
// PLATFORM_BOUNDARY: sizeof(EmoteLoopController) on Web will not equal 32B (libc++
//   std::vector header differs from libstdc++). Offsets above are provenance only;
//   the logical contract is field semantics + 12B keyframe element type + the
//   raw-bits curve sampler, not byte equality (per CLAUDE.md byte-layout method).
//
// VERIFICATION GAP (documented, NOT a defer): this category has no oracle/fixture
//   coverage — the logo differential motion contains no enabled loopControl
//   element, so the step is inert against current differential tests. Per CLAUDE.md
//   the value standard is the six architecture dimensions, not observability; the
//   port is faithful (raw-bits sampler, wrapping index, HM7 upsert) with reverse-
//   engineering evidence and a non-regressing build. Build is the non-regression
//   guard.
//
#pragma once

#include <cstdint>
#include <vector>

namespace motion {

    // The 12B keyframe element of the loop curve (a per-element internal data
    //   format the binary reads by byte via *(float*)(base + 12*i + {0,4,8}) — the
    //   element POD layout IS a platform-independent data contract per CLAUDE.md,
    //   so the field offsets here are load-bearing, unlike object ABI offsets).
    struct EmoteLoopKeyframe12B {
        float v0   = 0.0f; // +0  — value at the START of this keyframe segment
        float v1   = 0.0f; // +4  — value at the END of this keyframe segment
        float span = 0.0f; // +8  — segment duration (>=accum advances to next)
    };

    // 0x20=32B loop controller. Plain C++ object (no vtable; the binary's
    //   `operator new(0x20)` + 16B/16B zero-init covers {int,float,vector}).
    struct EmoteLoopController {
        int32_t                            currentIndex = 0;   // +0  — active kf index
        float                              accum        = 0.0f;// +4  — RAW float bits
        std::vector<EmoteLoopKeyframe12B>  keys;               // +8/+16/+24 (begin/finish/cap)
    };

    // Aligned with libkrkr2.so EmoteEngine_progress inline curve step
    //   @0x67d2a0..0x67d370 (this category has NO standalone step in the binary;
    //   the body is open-coded inside progress). Returns the curve blend (float)
    //   for the current frame, advancing ctl.accum / ctl.currentIndex.
    float EmoteLoopController_step(EmoteLoopController* ctl, float dt);

} // namespace motion
