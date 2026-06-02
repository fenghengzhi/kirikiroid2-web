// EmoteSelectorController — ctor (0x66E398) + step (sub_668470) + applySelection
//   (sub_6680B0) faithful port. See EmoteSelectorController.h for the full
//   structural analysis and the deque-numbering reconciliation (selector lives at
//   engine+656 = local _vectorVarDeque9; step = sub_668470, NOT sub_666BF8).

#include "EmoteSelectorController.h"

#include <cmath>   // std::fabs
#include <cstring> // std::memcpy
#include <utility> // std::move

#include "EmoteVarController.h" // option.refCtl is an EmoteVarController

namespace motion {

    namespace {

        // Aligned with libkrkr2.so Animator_setKeyframes @ 0x667330 (called by
        //   sub_6680B0 @0x668190). Pushes a power-curve keyframe onto an
        //   EmoteVarController's 20B-element deque, OR (when dur<=0) snaps the
        //   controller's currentValue directly.
        // Decompiled pseudocode:
        //   if (dur <= 0.0) {
        //     // snap: reset the keyframe deque to a single empty block, state=0,
        //     //   then copy `count` floats from values into currentValue(+88).
        //     <deque reset to one block>; state(+84)=0;
        //     for (i in [0,count)) currentValue[i] = values[i];
        //   } else {
        //     if (!clearFirst) { <deque reset to one block>; state(+84)=0; }
        //     EmoteVarController_deque20B_pushback({values, dur, pow});
        //   }
        // In the selector apply path the controller carries `count` channels; the
        //   keyframe channels are filled from `values` (here a single float, the
        //   selected on/off value — count is 1 for selector-driven transition
        //   controllers). We mirror the two branches faithfully.
        void Animator_setKeyframes(EmoteVarController* ctl, const float* values,
                                   bool clearFirst, float dur, float pow) {
            const int count = ctl->count; // *(a1+80)
            if (dur <= 0.0f) {                                         // /*0x667340*/
                // snap path: clear the keyframe deque and write currentValue.
                ctl->queue.clear();                                   // <deque reset>
                ctl->state = 0;                                        // *(a1+84)=0 /*0x6673cc*/
                if (ctl->currentValue) {                              // /*0x6673d4*/
                    for (int i = 0; i < count; ++i) {                 // /*0x667444*/
                        ctl->currentValue[i] = values[i];             // *(a1+88)[i]=values[i]
                    }
                }
                return;
            }
            // dur > 0 path.
            if (!clearFirst) {                                        // (a3 & 1) == 0 /*0x667344*/
                ctl->queue.clear();                                  // <deque reset>
                ctl->state = 0;                                       // *(a1+84)=0 /*0x667378*/
            }
            // EmoteVarController_deque20B_pushback({values[0..count), dur, pow}).
            //   /*0x667390*/
            EmoteVarKeyValue20B kf;
            for (int i = 0; i < count && i < 3; ++i) {
                kf.channel[i] = values[i];
            }
            kf.duration = dur;
            // pow is a float in the binary keyframe element's powCount slot
            //   (+16). The whole pipeline treats +16 as raw float bits: it is
            //   written by Animator_setKeyframes (0x667300) via a DWORD copy of a
            //   float arg, and read back with `LDR S1` (no SCVTF) in
            //   EmoteVarController_step (0x666df4). EmoteVarKeyValue20B::powCount
            //   is now a `float`, so this memcpy is a plain raw-bit store of the
            //   float `pow` (consistent with the mouth/eye/eyebrow powField rule).
            std::memcpy(&kf.powCount, &pow, sizeof(float));
            ctl->queue.push_back(kf);
        }

    } // namespace

    // Aligned with libkrkr2.so EmoteSelectorController_ctor_guess @ 0x66E398.
    // Decompiled pseudocode (this conversation):
    //   memset(self, 0, 0x50);                              // +0..+79 /*0x66e3b8*/
    //   EmoteAngleController_ctor_12Bdeque(self, 0);        // 12B deque /*0x66e3c4*/
    //   *(qword*)(self+104) = 0;                            // optionList.begin /*0x66e3cc*/
    //   *(qword*)(self+112) = 0; *(qword*)(self+120) = 0;   // .end/.cap /*0x66e3d0*/
    //   *(qword*)(self+92) = 0;                             // invDur+accum /*0x66e3d4*/
    //   *(qword*)(self+84) = 0;                             // selState+selectedIndex /*0x66e3d8*/
    //   // swap the builder's optionList (3 qwords) into +104/+112/+120:
    //   self+104 = a2[0]; a2[0] = 0;                        // /*0x66e3dc..0x66e3e4*/
    //   swap(self+112, a2[1]); swap(self+120, a2[2]);       // /*0x66e3e8..0x66e404*/
    //   sub_6680B0(self, 0, 0.0, 0.0);                      // applySelection(0) /*0x66e418*/
    void EmoteSelectorController_ctor(EmoteSelectorController* self,
                                      std::vector<SelectorOption16B>&& optionList) {
        // The 12B command track default-constructs empty (binary's memset(0x50) +
        //   EmoteAngleController_ctor_12Bdeque leaves it empty).
        EmoteAngleController_ctor(&self->commandTrack12B, 0); // 0x66e3c4

        // +84..+100 cleared by member initializers (selState=0, selectedIndex=0,
        //   invDuration=0, accum=0, pad=0) — mirrors *(qword*)(self+84)=0 and
        //   *(qword*)(self+92)=0.

        // Move-swap the builder's assembled optionList into the controller
        //   (binary swaps the 3-qword vector; std::move replicates the ownership
        //   transfer — a2 is left empty, self takes the buffer).
        self->optionList = std::move(optionList);             // /*0x66e3dc..0x66e404*/

        // applySelection(self, 0, 0.0, 0.0): select index 0 initially.   /*0x66e418*/
        EmoteSelectorController_applySelection(self, 0, 0.0f, 0.0f);
    }

    // Aligned with libkrkr2.so sub_668470 EmoteSelectorController_step @ 0x668470.
    // Decompiled pseudocode (this conversation):
    //   v4 = selState(+84);
    //   if (v4) {
    //     if (v4 == 1) {                                     // animating
    //       v6 = invDur(+92)*dt + accum(+96); accum(+96) = v6;
    //       if (v6 >= 1.0) { accum(+96)=1.0; selState(+84)=0; }
    //     }
    //   } else {                                             // state 0: setup
    //     if (commandTrack @+16 non-empty) {                 // *(+48) != *(+16)
    //       v9 = elem[+0]; v8 = elem[+4]; v10 = elem[+8];    // {selIdx, dur, fade}
    //       pop_front commandTrack;
    //       applySelection(self, (int)v9, v8, v10);
    //       invDur(+92) = 1.0 / v8;
    //       selState(+84) = selState + 1;                    // 0 -> 1
    //       accum(+96) = 0;
    //     }
    //   }
    //   result = (float)selectedIndex(+88);  *out = result;  return result;
    float EmoteSelectorController_step(EmoteSelectorController* self,
                                       float* out, float dt) {
        const int v4 = self->selState; // *(a1+84)  /*0x668488*/

        if (v4) {                                             // /*0x668490*/
            if (v4 == 1) {                                    // /*0x668498*/ animating
                // v6 = invDur(+92)*dt + accum(+96).                   /*0x6684a4*/
                const float v6 = (self->invDuration * dt) + self->accum;
                self->accum = v6;                             // *(a1+96)=v6 /*0x6684b0*/
                if (v6 >= 1.0f) {                             // /*0x6684b4*/
                    self->accum    = 1.0f;                    // 0x3F800000 /*0x6684bc*/
                    self->selState = 0;                       // *(a1+84)=0 /*0x6684c0*/
                }
            }
        } else {                                              // /*0x6684cc*/ state 0: setup
            if (!self->commandTrack12B.queue.empty()) {       // *(+48)!=*(+16) /*0x6684d4*/
                const EmoteAngleKeyValue12B kf =
                    self->commandTrack12B.queue.front();      // {selIdx,dur,fade} /*0x6684dc*/
                const float selIdx = kf.endRad;               // elem[+0] /*0x6684dc*/
                const float dur    = kf.duration;             // elem[+4] /*0x6684dc*/
                float fade;                                   // elem[+8] /*0x6684e0*/
                std::memcpy(&fade, &kf.powCount, sizeof(float)); // raw float bits

                self->commandTrack12B.queue.pop_front();      // advance +16 / free block /*0x668500*/

                // applySelection(self, (int)selIdx, dur, fade).        /*0x668530*/
                EmoteSelectorController_applySelection(
                    self, static_cast<int>(selIdx), dur, fade);

                self->invDuration = 1.0f / dur;               // *(a1+92)=1/v8 /*0x668540*/
                self->selState    = self->selState + 1;       // *(a1+84)=v13+1 /*0x668548*/
                self->accum       = 0.0f;                     // *(a1+96)=0 /*0x66854c*/
            }
        }

        // result = (float)selectedIndex; *out = result.                /*0x668554*/
        const float result = static_cast<float>(self->selectedIndex);
        *out = result;                                        // *a2 = result /*0x668558*/
        return result;                                        // /*0x66856c*/
    }

    // Aligned with libkrkr2.so sub_6680B0
    //   EmoteSelectorController_applySelection @ 0x6680B0.
    // Decompiled pseudocode (this conversation):
    //   selectedIndex(+88) = index;                          // *(a1+88)=a2 /*0x6680ec*/
    //   for (i = 0; i < optionList.size(); ++i) {            // /*0x668108..0x6681ac*/
    //     opt = optionList[i];
    //     if (opt.refCtl) {                                  // /*0x66810c*/
    //       value = (i == selectedIndex) ? opt.onValue : opt.offValue;  /*0x668124*/
    //       EmoteVarController_step(opt.refCtl, &cur, 0.0);   // read current /*0x668138*/
    //       delta = cur - value;                             // /*0x66814c*/
    //       if (opt.refCtl.state(+84) != 0                   // /*0x668150*/
    //           || opt.refCtl.queue non-empty (+48 != +16)   // /*0x668154*/
    //           || fabsf(delta) >= 1e-7) {                   // /*0x66816c*/
    //         Animator_setKeyframes(opt.refCtl, &value, 0,
    //             fabsf(delta / (onValue - offValue)) * dur, fade);     /*0x668190*/
    //       }
    //     }
    //   }
    // NOTE on value-vs-target: the binary loads value into `v17` (the keyframe
    //   target) and current into `v16` (from EmoteVarController_step). It compares
    //   `value - current`'s magnitude against the on/off span; the duration scale
    //   = |delta| / |onValue - offValue| * dur. The keyframe target passed to
    //   Animator_setKeyframes is `&value` (v17).
    void EmoteSelectorController_applySelection(EmoteSelectorController* self,
                                                int index, float dur,
                                                float fade) {
        self->selectedIndex = index; // *(a1+88)=a2  /*0x6680ec*/

        const std::size_t n = self->optionList.size(); // (+112 - +104) >> 4
        for (std::size_t i = 0; i < n; ++i) {                // /*0x668108..0x6681ac*/
            SelectorOption16B& opt = self->optionList[i];
            if (!opt.refCtl) {                               // /*0x66810c*/
                continue;
            }
            // value = (i == selectedIndex) ? onValue(+12) : offValue(+8). /*0x668124*/
            const float value = (static_cast<int>(i) == self->selectedIndex)
                                    ? opt.onValue
                                    : opt.offValue;

            // EmoteVarController_step(refCtl, &cur, 0.0) — read current value.
            //   /*0x668138*/
            float cur = 0.0f;
            EmoteVarController_step(opt.refCtl, &cur, 0.0f);

            // delta = current - value. The binary loads S0=current (the
            //   EmoteVarController_step output), S3=value, then FSUB S0,S0,S3
            //   (@0x66814c). The sign matters only structurally — both downstream
            //   uses (the guard and the duration scale) take fabs — but we keep
            //   the binary's operand order for faithful alignment.
            const float delta = cur - value;                 // FSUB S0,S0,S3 /*0x66814c*/

            // Guard: only push a keyframe if the controller is mid-anim, has
            //   queued frames, or the delta is non-trivial (|delta| >= 1e-7).
            //   /*0x668150..0x668170*/
            const bool busy = (opt.refCtl->state != 0) ||
                              (!opt.refCtl->queue.empty());
            if (busy || std::fabs(delta) >= 0.0000001f) {    // 1e-7 (qword_14CF408)
                // Animator_setKeyframes(refCtl, &value, 0,
                //   |delta / (onValue - offValue)| * dur, fade).        /*0x668190*/
                const float span = opt.onValue - opt.offValue; // v12[3]-v12[2]
                const float scaledDur = std::fabs(delta / span) * dur;
                Animator_setKeyframes(opt.refCtl, &value, /*clearFirst=*/false,
                                      scaledDur, fade);
            }
        }
    }

    void EmoteSelectorController_dtor(EmoteSelectorController* self) {
        // The 12B command track owns no heap beyond its deque buffer (freed by
        //   std::deque). The optionList holds NON-owning refCtl pointers (those
        //   EmoteVarControllers are owned by the transition controller-deque), so
        //   the selector dtor does NOT delete them — it only releases its own
        //   vector buffer (handled by std::vector's destructor). Mirrors the
        //   binary: the option vector stores borrowed controller pointers.
        (void)self;
    }

} // namespace motion
