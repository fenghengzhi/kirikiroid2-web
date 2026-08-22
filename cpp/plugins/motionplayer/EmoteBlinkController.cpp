// Four-reference EmoteBlinkController constructor and per-slice step.

#include "EmoteBlinkController.h"
#include "EmoteBlinkRng.h"
#include "MotionDispatch.h"
#include "ncbind.hpp"

#include <cmath>
#include <cstring> // std::memcpy for the raw-bits trackPow reinterpret
#include <limits>

namespace motion {

    namespace {
        bool blinkTrackOvershoots_guess(float direction, float target,
                                        float nextValue) {
            // The positive half uses an ordered LS test. The negative half is
            // expressed as complements of GE and LT, so unordered direction or
            // target comparisons reach overshoot instead of falling through.
            if(direction > 0.0f) {
                return target <= nextValue;
            }
            if(!(direction >= 0.0f)) {
                return !(target < nextValue);
            }
            return false;
        }

        int blinkPositionToSignedInt32_guess(float value) noexcept {
            constexpr float lower = -0x1p31f;
            constexpr float upper = 0x1p31f;
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

    // The constructor reads the blink scalars, seeds the initial position and
    // shared-RNG timer, then copies the edge/node arrays into the embedded mesh
    // resolver. Exact ABI layouts and instruction mappings live in analysis/.
    EmoteBlinkController::EmoteBlinkController(const tTJSVariant& dict)
        : trackState(0), trackTarget(0.0f), trackDir(0.0f), blinkPhase(0) {
        // The first member is the naked 12-byte keyframe deque, not a complete
        // angle controller. It is already default-constructed empty.

        ncbPropAccessor object{tTJSVariant(dict)};

        // Blink scalar fields.
        beginFrame = static_cast<int32_t>(object.GetValue(
            TJS_W("beginFrame"), ncbTypedefs::Tag<tjs_int>(), 0,
            &detail::emoteControllerBeginFrameHint_guess));
        endFrame = static_cast<int32_t>(object.GetValue(
            TJS_W("endFrame"), ncbTypedefs::Tag<tjs_int>(), 0,
            &detail::emoteControllerEndFrameHint_guess));
        blinkIntervalMin = static_cast<float>(object.GetValue(
            TJS_W("blinkIntervalMin"), ncbTypedefs::Tag<tjs_real>(), 0,
            &detail::emoteControllerBlinkIntervalMinHint_guess));
        blinkIntervalMax = static_cast<float>(object.GetValue(
            TJS_W("blinkIntervalMax"), ncbTypedefs::Tag<tjs_real>(), 0,
            &detail::emoteControllerBlinkIntervalMaxHint_guess));
        blinkFrameCount = static_cast<float>(object.GetValue(
            TJS_W("blinkFrameCount"), ncbTypedefs::Tag<tjs_real>(), 0,
            &detail::emoteControllerBlinkFrameCountHint_guess));
        blinkEnabled = object.GetValue(
            TJS_W("blinkEnabled"), ncbTypedefs::Tag<bool>(), 0,
            &detail::emoteControllerBlinkEnabledHint_guess) ? 1u : 0u;

        const float intervalMin = blinkIntervalMin;
        const float intervalMax = blinkIntervalMax;
        const float initialPosition = static_cast<float>(beginFrame);
        trackValue = initialPosition;
        blinkPos = initialPosition;

        // nextBlink countdown = min + (max-min)*rand, rand in [0,1).
        const float random = static_cast<float>(
            EmoteBlinkRng_nextCanonical_guess(
                EmoteBlinkRng_getGlobal_guess()));
        blinkTimer = intervalMin + (intervalMax - intervalMin) * random;

        // "edge" array -> edgeTable of {x,y} pairs (each elem a 2-int sub-array).
        ncbPropAccessor edge{object.GetValue(
            TJS_W("edge"), ncbTypedefs::Tag<tTJSVariant>(), 0,
            &detail::emoteControllerEdgeHint_guess)};
        const int edgeCount = static_cast<int>(edge.GetArrayCount());
        mesh.edgeTable.reserve(static_cast<size_t>(edgeCount));
        for (int i = 0; i < edgeCount; ++i) {
            ncbPropAccessor pair{edge.GetValue(
                i, ncbTypedefs::Tag<tTJSVariant>())};
            const int x = static_cast<int>(pair.GetValue(
                0, ncbTypedefs::Tag<tjs_int>()));
            const int y = static_cast<int>(pair.GetValue(
                1, ncbTypedefs::Tag<tjs_int>()));
            mesh.edgeTable.emplace_back(static_cast<float>(x),
                                        static_cast<float>(y));
        }

        // "node" array -> nodeRows: each element becomes one int-to-float row.
        ncbPropAccessor node{object.GetValue(
            TJS_W("node"), ncbTypedefs::Tag<tTJSVariant>(), 0,
            &detail::emoteControllerNodeHint_guess)};
        const int nodeCount = static_cast<int>(node.GetArrayCount());
        for (int i = 0; i < nodeCount; ++i) {
            ncbPropAccessor sourceRow{node.GetValue(
                i, ncbTypedefs::Tag<tTJSVariant>())};
            std::vector<float> row;
            const int rowCount = static_cast<int>(
                sourceRow.GetArrayCount());
            row.reserve(static_cast<size_t>(rowCount));
            for (int j = 0; j < rowCount; ++j) {
                row.push_back(static_cast<float>(
                    sourceRow.GetValue(
                        j, ncbTypedefs::Tag<tjs_int>())));
            }
            mesh.nodeRows.push_back(std::move(row));
        }
    }

    void EmoteBlinkController_enqueueValue_guess(
        EmoteBlinkController* self, float value, float duration, float power,
        bool append) {
        // Native branches to this path unless duration is ordered-positive;
        // unordered NaN therefore commits immediately too.
        if(!(duration > 0.0f)) {
            self->valueTrack12B.clear();
            self->valueTrack8B.clear();
            self->trackValue = value;
            self->trackState = 0;
            return;
        }

        if(!append) {
            self->valueTrack12B.clear();
            self->valueTrack8B.clear();
            self->trackState = 0;
        }

        EmoteAngleKeyValue12B keyframe;
        keyframe.endRad = value;
        keyframe.duration = duration;
        std::memcpy(&keyframe.powCount, &power, sizeof(float));
        self->valueTrack12B.push_back(keyframe);
    }

    void EmoteBlinkController_step(EmoteBlinkController* self, float* out,
                                   float dt) {
        int state = self->trackState;

        // A completed segment immediately re-enters this loop. If the
        // secondary path is empty, state 1 becomes state 0 and the next primary
        // command is resolved in this same slice.
        for (;;) {
            while (state != 2) {
                if (state == 1) {
                    if (self->valueTrack8B.empty()) {
                        state = 0;
                        self->trackState = 0;
                    } else {
                        const std::pair<float, float> segment =
                            self->valueTrack8B.front();
                        self->valueTrack8B.pop_front();
                        const float startValue = segment.first;
                        const float endValue = segment.second;
                        self->trackValue = startValue;
                        if (startValue == endValue) {
                            // A zero-length segment is consumed, but state 1 is
                            // retained and no later segment is inspected now.
                            goto blink;
                        }
                        state = 2;
                        self->trackTarget = endValue;
                        self->trackDir =
                            ((endValue - startValue) < 0.0f) ? -1.0f : 1.0f;
                        self->trackState = 2;
                    }
                } else {
                    // Unknown nonzero states and an empty primary queue skip
                    // directly to the independent blink phase.
                    if (state != 0 || self->valueTrack12B.empty()) {
                        goto blink;
                    }

                    const EmoteAngleKeyValue12B keyframe =
                        self->valueTrack12B.front();
                    self->valueTrack12B.pop_front();
                    EmoteMeshResolver_resolve_guess(
                        &self->mesh, self->trackValue, keyframe.endRad,
                        &self->valueTrack8B);

                    self->trackAccum = 0.0f;
                    self->trackSpan = self->mesh.trackResolvedSpan;
                    self->trackInvDur = 1.0f / keyframe.duration;
                    // The command's last word is copied as float bits; there is
                    // no integer-to-float conversion.
                    std::memcpy(
                        &self->trackPow, &keyframe.powCount, sizeof(float));
                    state = self->trackState + 1;
                    self->trackState = state;
                }
            }

            const float span = self->trackSpan;
            const float previousAccum = self->trackAccum;
            const float power = self->trackPow;
            const float easedPhase =
                std::pow(previousAccum / span, 1.0f / power) +
                self->trackInvDur * dt;
            const float nextAccum = std::pow(easedPhase, power) * span;
            const float delta = nextAccum - previousAccum;
            const float direction = self->trackDir;
            const float nextValue = self->trackValue + direction * delta;
            self->trackValue = nextValue;
            const float target = self->trackTarget;
            const bool overshoot = blinkTrackOvershoots_guess(
                direction, target, nextValue);
            if (!overshoot) {
                self->trackAccum = previousAccum + delta;
                break;
            }

            state = 1;
            self->trackState = 1;
            self->trackValue = target;
        }

    blink:
        switch (self->blinkPhase) {
            case 0: { // wait for blink trigger
                if (self->blinkEnabled) {
                    if (self->beginFrame ==
                        blinkPositionToSignedInt32_guess(self->blinkPos)) {
                        const float timer = self->blinkTimer - dt;
                        self->blinkTimer = timer;
                        if (timer <= 0.0f) {
                            self->blinkPhase = 10;
                        }
                    }
                }
                break;
            }
            case 10: { // closing
                const int endFrame = self->endFrame;
                const float frameCount = self->blinkFrameCount;
                const float position = self->blinkPos +
                    (((dt * 2.5f) / frameCount) *
                     static_cast<float>(endFrame - self->beginFrame));
                self->blinkPos = position;
                if (position >= static_cast<float>(endFrame)) {
                    self->blinkPos = static_cast<float>(endFrame);
                    self->blinkPhase = 11;
                    self->blinkTimer = frameCount / 5.0f;
                }
                break;
            }
            case 11: { // hold (eyes closed)
                const float timer = self->blinkTimer - dt;
                self->blinkTimer = timer;
                if (timer <= 0.0f) {
                    const float intervalMin = self->blinkIntervalMin;
                    const float intervalMax = self->blinkIntervalMax;
                    self->blinkPhase = 12;
                    const float intervalSpan = intervalMax - intervalMin;
                    const float random = static_cast<float>(
                        EmoteBlinkRng_nextCanonical_guess(
                            EmoteBlinkRng_getGlobal_guess()));
                    self->blinkTimer = intervalMin + intervalSpan * random;
                }
                break;
            }
            case 12: { // opening
                const int beginFrame = self->beginFrame;
                const float position = self->blinkPos +
                    (((dt * -2.5f) / self->blinkFrameCount) *
                     static_cast<float>(self->endFrame - beginFrame));
                self->blinkPos = position;
                if (position <= static_cast<float>(beginFrame)) {
                    self->blinkPos = static_cast<float>(beginFrame);
                    self->blinkPhase = 0;
                }
                break;
            }
            default:
                break;
        }

        // The remap is inclusive and deliberately leaves a zero-width frame
        // range to the platform's floating-point divide-by-zero behaviour.
        const int beginFrame = self->beginFrame;
        float value = self->trackValue;
        if (value >= static_cast<float>(beginFrame)) {
            const int endFrame = self->endFrame;
            if (value <= static_cast<float>(endFrame)) {
                const float numerator =
                    (static_cast<float>(endFrame) - value) *
                    (self->blinkPos - static_cast<float>(beginFrame));
                value = static_cast<float>(
                    numerator / static_cast<double>(endFrame - beginFrame) +
                    value);
            }
        }
        *out = value;
    }

    // Commit the final pending value and return the controller to idle.
    void EmoteBlinkController_reset_guess(EmoteBlinkController* self) {
        if(!self->valueTrack12B.empty()) {
            self->trackState = 0;
            self->trackValue = self->valueTrack12B.back().endRad;
            self->valueTrack12B.clear();
            self->valueTrack8B.clear();
            return;
        }
        if(self->trackState != 0) {
            self->trackState = 0;
            self->trackValue = self->valueTrack8B.empty()
                ? self->trackTarget
                : self->valueTrack8B.back().first;
            self->valueTrack8B.clear();
        }
    }

} // namespace motion
