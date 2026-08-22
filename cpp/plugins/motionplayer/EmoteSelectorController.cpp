// Emote selector controller implementation, cross-checked against
// the Android/iOS ARM64 and ARMv7 reference binaries.

#include "EmoteSelectorController.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include "EmoteVarController.h"

namespace motion {

    namespace {
        // The three native selector consumers encode signed-int32,
        // round-toward-zero conversion directly in ARM FP instructions.
        // Keep their NaN and overflow results explicit for WebAssembly.
        int selectorIndexFromFloat_guess(float value) {
            constexpr float lower = -2147483648.0f;
            constexpr float upper = 2147483648.0f;
            if(std::isnan(value)) {
                return 0;
            }
            if(value >= upper) {
                return std::numeric_limits<std::int32_t>::max();
            }
            if(value <= lower) {
                return std::numeric_limits<std::int32_t>::min();
            }
            return static_cast<int>(value);
        }
    }

    EmoteSelectorController::EmoteSelectorController(
        std::vector<EmoteSelectorOption_guess>&& options)
        : commandTrack12B{},
          selState(0),
          selectedIndex(0),
          invDuration(0.0f),
          accum(0.0f),
          optionList(std::move(options)) {
        // This call is part of the native C++ constructor. If it throws, the
        // option vector and command deque unwind before the new-expression
        // releases the controller allocation.
        EmoteSelectorController_applySelection(this, 0, 0.0f, 0.0f);
    }

    void EmoteSelectorController_enqueue_guess(
        EmoteSelectorController* self, float selection, float duration,
        float fade, bool append) {
        if (!(duration > 0.0f)) {
            self->commandTrack12B.queue.clear();
            self->selState = 0;
            EmoteSelectorController_applySelection(
                self, selectorIndexFromFloat_guess(selection), 0.0f, 0.0f);
            return;
        }

        if (!append) {
            self->commandTrack12B.queue.clear();
            self->selState = 0;
        }

        EmoteAngleKeyValue12B command{};
        command.endRad = selection;
        command.duration = duration;
        std::memcpy(&command.powCount, &fade, sizeof(float));
        self->commandTrack12B.queue.push_back(command);
    }

    float EmoteSelectorController_step(
        EmoteSelectorController* self, float* out, float dt) {
        const int state = self->selState;
        if (state != 0) {
            if (state == 1) {
                const float next = self->invDuration * dt + self->accum;
                self->accum = next;
                if (next >= 1.0f) {
                    self->accum = 1.0f;
                    self->selState = 0;
                }
            }
        } else if (!self->commandTrack12B.queue.empty()) {
            const EmoteAngleKeyValue12B command =
                self->commandTrack12B.queue.front();
            self->commandTrack12B.queue.pop_front();

            EmoteSelectorController_applySelection(
                self, selectorIndexFromFloat_guess(command.endRad),
                command.duration,
                command.powCount);
            self->invDuration = 1.0f / command.duration;
            self->selState = self->selState + 1;
            self->accum = 0.0f;
        }

        const float result = static_cast<float>(self->selectedIndex);
        *out = result;
        return result;
    }

    void EmoteSelectorController_applySelection(
        EmoteSelectorController* self, int index, float duration, float fade) {
        self->selectedIndex = index;

        for (std::size_t i = 0; i < self->optionList.size(); ++i) {
            EmoteSelectorOption_guess& option = self->optionList[i];
            if (!option.refCtl) {
                continue;
            }

            const float value =
                static_cast<int>(i) == self->selectedIndex
                    ? option.onValue
                    : option.offValue;

            float current = 0.0f;
            EmoteVarController_step(option.refCtl, &current, 0.0f);
            const float delta = current - value;
            const bool busy = option.refCtl->state != 0 ||
                              !option.refCtl->queue.empty();
            if (busy || std::fabs(delta) >= 0.0000001f) {
                const float span = option.onValue - option.offValue;
                const float scaledDuration =
                    std::fabs(delta / span) * duration;
                EmoteVarController_setTarget_guess(
                    option.refCtl, &value, scaledDuration, fade,
                    /*append=*/false);
            }
        }
    }

    void EmoteSelectorController_reset_guess(
        EmoteSelectorController* self) {
        if (!self->commandTrack12B.queue.empty()) {
            self->selState = 0;
            const int index = selectorIndexFromFloat_guess(
                self->commandTrack12B.queue.back().endRad);
            EmoteSelectorController_applySelection(
                self, index, 0.0f, 0.0f);
            self->commandTrack12B.queue.clear();
            return;
        }

        if (self->selState != 0) {
            self->selState = 0;
            EmoteSelectorController_applySelection(
                self, self->selectedIndex, 0.0f, 0.0f);
        }
    }

} // namespace motion
