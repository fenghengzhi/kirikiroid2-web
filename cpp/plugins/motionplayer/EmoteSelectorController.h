// Emote selector controller reconstructed from the four current reference
// binaries. Native byte offsets and per-ABI container layouts are documented in
// analysis/; this header keeps the common source-level object model.
#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "EmoteAngleController.h"

namespace motion {

    struct EmoteVarController;

    // The selector reuses the native 12-byte command element, but it does not
    // embed the angle controller's scalar interpolation fields. Its base is only
    // a deque plus the otherwise-unused base state word.
    struct EmoteSelectorCommandTrack_guess {
        std::deque<EmoteAngleKeyValue12B> queue;
        int32_t state = 0;
    };

    // One selector option. refCtl is borrowed from the engine's transition
    // controller deque; the selector never destroys it. The element is 16 bytes
    // on the 64-bit references and 12 bytes on the 32-bit references.
    struct EmoteSelectorOption_guess {
        EmoteVarController* refCtl   = nullptr;
        float               offValue = 0.0f;
        float               onValue  = 0.0f;
    };

    struct EmoteSelectorController {
        explicit EmoteSelectorController(
            std::vector<EmoteSelectorOption_guess>&& options);

        EmoteSelectorCommandTrack_guess commandTrack12B;

        int32_t selState      = 0;    // 0: idle/setup, 1: duration ramp active
        int32_t selectedIndex = 0;
        float   invDuration   = 0.0f;
        float   accum         = 0.0f;

        // 64-bit ABIs insert four bytes of natural alignment here; 32-bit ABIs
        // place the vector immediately after accum. There is no source field.
        std::vector<EmoteSelectorOption_guess> optionList;
    };

    // Source-level argument order recovered by reconciling AArch64's split
    // integer/FP registers with the two ARMv7 call sites.
    void EmoteSelectorController_enqueue_guess(
        EmoteSelectorController* self, float selection, float duration,
        float fade, bool append);

    float EmoteSelectorController_step(
        EmoteSelectorController* self, float* out, float dt);

    // Commits the final queued selection, or re-applies the current selection
    // when a transition is active, then leaves the selector idle.
    void EmoteSelectorController_reset_guess(EmoteSelectorController* self);

    void EmoteSelectorController_applySelection(
        EmoteSelectorController* self, int index, float duration, float fade);

} // namespace motion
