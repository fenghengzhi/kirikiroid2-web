// EmoteSelectorController — 0x80=128B controller for the emote "selector"
//   category. Aligned with libkrkr2.so:
//     ctor          : EmoteSelectorController_ctor_guess @ 0x66E398
//     step          : sub_668470 (EmoteSelectorController_step) @ 0x668470
//     applySelection: sub_6680B0 (EmoteSelectorController_applySelection) @ 0x6680B0
//     setVariable   : sub_6681E4 (reader, case 8) @ 0x6681E4 — NOT ported here
//
// DEQUE-NUMBERING RECONCILIATION (read this before touching offsets).
//   The selector controller lives in the EmoteEngine controller-deque whose
//   libstdc++ `begin._M_cur` is at engine+656 (i.e. the deque header base is
//   engine+640). The progress loop @0x67d1e0 steps THIS deque with sub_668470,
//   48B stride (v38 += 6 qwords), 480-byte block (60 qwords). In the local
//   EmoteEngine struct that deque is the member historically named
//   `_vectorVarDeque9` (its comment already records `sub_668470, ARM64 48B`).
//   The task brief calls the same deque "deque#8"; that label refers to the
//   1-based controller-deque ordinal, NOT the engine member name. The binary
//   OFFSET engine+656 is the unambiguous ground truth and is what both names
//   point at. (The OTHER deque, engine+576 / header engine+560, member name
//   `_auxVarDeque8`, is the TRANSITION controller, 24B, stepped by
//   EmoteVarController_step a.k.a. sub_666BF8 — a SEPARATE, still-open category.
//   The brief's premise "step deque#8 = sub_666BF8" conflated the two; the
//   fresh decompile of EmoteEngine_progress @0x67D01C proves the selector's
//   step is sub_668470 @engine+656, while sub_666BF8 is the transition step
//   @engine+576.)
//
// Field offset table (fresh decompile of EmoteSelectorController_ctor @0x66E398,
// step @0x668470, applySelection @0x6680B0 this conversation; offsets are
// libkrkr2.so ARM64 byte offsets kept as provenance comments only — wasm layout
// need not match per CLAUDE.md byte-layout methodology):
//   +0..+79   EmoteAngleController-style 12B-elem command track (ctor:
//             EmoteAngleController_ctor_12Bdeque @0x66e3c4). The step's state-0
//             setup pops a 12B command {float selectIndex@+0, float dur@+4,
//             float fadeTime@+8} off the front; 504-block per sub_668470
//             @0x66851c (`*(a1+32)=block+504`).
//   +80       int32_t  baseState   (the base 12B-track's own state slot; the
//                                    selector step does NOT use it)
//   +84       int32_t  selState    (selector state: 0 setup/idle, 1 animating)
//   +88       int32_t  selectedIndex (the active option index; output -> *a2 as
//                                    (float)int)
//   +92       float    invDuration (= 1.0 / command.dur)
//   +96       float    accum       (0->1 progress; += invDuration*dt; clamps 1.0)
//   +100      int32_t  pad
//   +104/+112/+120  std::vector<SelectorOption16B>  optionList (begin/end/cap).
//             The ctor swaps in the optionList the BUILDER assembled (3-qword
//             vector move @0x66e3dc..0x66e404).
//
// SelectorOption16B (the vector element; assembled by the builder @0x66dbf0 and
// consumed by applySelection @0x668108):
//   +0   EmoteVarController* refCtl  (resolved from the TRANSITION deque@+576 by
//                                     matching the option's "label"; may be null
//                                     until the transition category is built —
//                                     see INERT note below)
//   +8   float offValue  (PSB "offValue", default 0.0 — applied when this
//                         option's index != selectedIndex)
//   +12  float onValue   (PSB "onValue",  default 0.0 — applied when this
//                         option's index == selectedIndex)
//
// PLATFORM_BOUNDARY: sizeof(EmoteSelectorController) on Web will not equal 128B
//   (libc++ deque/vector headers differ from libstdc++). Offsets above are for
//   traceability; the logical contract is field semantics + element types +
//   lifetime, not byte equality.
//
// INERT boundary (documented, NOT a defer): applySelection's per-option effect
//   (EmoteVarController_step + Animator_setKeyframes on option.refCtl) only fires
//   for options whose refCtl is non-null. refCtl is resolved by the builder out
//   of the TRANSITION controller-deque (engine+576), which is a separate, still-
//   open category — until it is built that deque is empty, so the builder
//   resolves every refCtl to null and applySelection skips them (the binary's
//   own `if (option.refCtl)` guard @0x66810c). The selector's OWN state machine
//   (state/selectedIndex/accum, and the +88 index output into HM7) is fully live
//   and faithful; only the cross-controller keyframe push is inert pending the
//   transition category. This mirrors the binary 1:1 (same null-guard, same
//   skip); it is a build-order dependency, not a missing computation.
//
#pragma once

#include <cstdint>
#include <vector>

#include "EmoteAngleController.h"

namespace motion {

    struct EmoteVarController; // option.refCtl target (transition-pool controller)

    // The vector element assembled by the builder and consumed by applySelection.
    //   16B in the binary: {ptr@+0, float@+8, float@+12}. Accessed by field name;
    //   the +0/+8/+12 offsets are provenance comments only.
    struct SelectorOption16B {
        EmoteVarController* refCtl   = nullptr; // +0
        float               offValue = 0.0f;    // +8
        float               onValue  = 0.0f;    // +12
    };

    // 0x80=128B selector controller. Plain C++ object (no vtable: ctor +0 writes a
    // std::deque header, not a vptr — matches EmoteAngleController /
    // EmoteVarController / EmoteMouthController).
    struct EmoteSelectorController {
        // +0..+79 — 12B-elem command track (EmoteAngleController-style). The step
        //   pops selection commands {selectIndex, dur, fadeTime} off the front of
        //   this deque (state-0 setup). The ctor leaves it empty; it is fed by the
        //   value-write path (setVariable case 8 / sub_6681E4), not this vertical.
        EmoteAngleController commandTrack12B; // ctor 0x66e3c4

        // +80 — the base 12B-track's own state slot. The selector step uses +84
        //   instead; +80 is left as the base controller's field (the ctor's
        //   memset(0x50) + EmoteAngleController_ctor zero it).
        int32_t baseState = 0; // +80

        // +84..+100 — selector animation state (compact ramp machine).
        int32_t selState      = 0;    // +84 (0 setup/idle, 1 animating)
        int32_t selectedIndex = 0;    // +88 (active option index; out -> *a2)
        float   invDuration   = 0.0f; // +92 (1/command.dur)
        float   accum         = 0.0f; // +96 (0->1 progress)
        int32_t pad           = 0;    // +100

        // +104/+112/+120 — optionList vector (begin/end/cap in the binary; here a
        //   std::vector). Built by EmoteEngine::buildSelectorControl, swapped into
        //   the controller by the ctor.
        std::vector<SelectorOption16B> optionList; // +104
    };

    // Aligned with libkrkr2.so EmoteSelectorController_ctor_guess @ 0x66E398.
    //   memset(self,0,0x50); EmoteAngleController_ctor_12Bdeque(self,0);
    //   clear optionList(+104/+112/+120); clear +84..+91 and +92/+96;
    //   move-swap the builder's optionList vector into +104/+112/+120;
    //   applySelection(self, 0, 0.0, 0.0)   // select index 0 initially.
    void EmoteSelectorController_ctor(EmoteSelectorController* self,
                                      std::vector<SelectorOption16B>&& optionList);

    // Aligned with libkrkr2.so sub_668470 EmoteSelectorController_step @ 0x668470.
    //   state(+84)==1: accum(+96) += invDur(+92)*dt; if accum>=1 -> accum=1,state=0.
    //   state(+84)==0: if commandTrack non-empty: pop {selIdx,dur,fade};
    //     applySelection(self, (int)selIdx, dur, fade); invDur(+92)=1/dur;
    //     state(+84)++; accum(+96)=0.
    //   *out = (float)selectedIndex(+88); returns the same.
    float EmoteSelectorController_step(EmoteSelectorController* self,
                                       float* out, float dt);

    // sub_668394 @0x668394. Commits the last queued selection (or the current
    // selected index when mid-transition), clears the command track and idles.
    void EmoteSelectorController_resetLike_0x668394(
        EmoteSelectorController* self);

    // Aligned with libkrkr2.so sub_6680B0
    //   EmoteSelectorController_applySelection @ 0x6680B0.
    //   selectedIndex(+88) = index; for each option i in optionList:
    //     if (option.refCtl) {
    //       value = (i == selectedIndex) ? option.onValue : option.offValue;
    //       EmoteVarController_step(option.refCtl, &cur, 0.0);   // read current
    //       delta = cur - value;
    //       if (refCtl.state!=0 || refCtl-queue-nonEmpty || |delta|>=1e-7)
    //         Animator_setKeyframes(refCtl, &value, 0,
    //             |delta/(onValue-offValue)| * dur, fadeTime);
    //     }
    void EmoteSelectorController_applySelection(EmoteSelectorController* self,
                                                int index, float dur,
                                                float fadeTime);

    void EmoteSelectorController_dtor(EmoteSelectorController* self);

} // namespace motion
