// Four-reference EmoteEyebrowController constructor, step, and reset path.
//
// The "slim" sibling of EmoteBlinkController. See EmoteEyebrowController.h for
// the full structural-difference analysis (no blink machine, no RNG, no remap;
// scalar field positions differ from the eye controller, and its value-track
// state machine advances at most one stage per call).

#include "EmoteEyebrowController.h"
#include "MotionDispatch.h"
#include "ncbind.hpp"

#include <cmath>
#include <cstring> // std::memcpy for the raw-bits trackPow reinterpret

namespace motion {

    namespace {
        bool eyebrowTrackOvershoots_guess(float direction, float target,
                                          float nextValue) {
            // Native routes unordered direction through the positive-side
            // block. Both target tests include unordered via condition-code
            // complements, unlike ordinary portable <=/>= expressions.
            if(!(direction <= 0.0f)) {
                return !(target > nextValue);
            }
            if(direction >= 0.0f) {
                return false;
            }
            return !(target < nextValue);
        }
    }

    // All four references construct the same source-level member sequence:
    // two ABI-specific deque implementations, the embedded edge/node/output
    // resolver, scalar track state, and beginFrame. Android's libstdc++ deques
    // eagerly allocate their map/block while iOS libc++ leaves its deques lazy;
    // that library difference does not add a portable source member.
    //
    // The metadata data flow is also uniform: read only beginFrame, convert the
    // two-int entries in "edge" to float pairs, convert each row in "node" to a
    // vector<float>, then seed trackValue from beginFrame. There are no Blink
    // fields, inheritance/vptr setup, or RNG calls in this constructor.
    EmoteEyebrowController::EmoteEyebrowController(
        const tTJSVariant& dict)
        : trackState(0), trackTarget(0.0f), trackDir(0.0f) {
        // The first member is the naked 12-byte keyframe deque. It is already
        // default-constructed empty with the owning object.

        ncbPropAccessor object{tTJSVariant(dict)};

        // beginFrame is the only scalar field read by the slim controller.
        beginFrame = static_cast<int32_t>(object.GetValue(
            TJS_W("beginFrame"), ncbTypedefs::Tag<tjs_int>(), 0,
            &detail::emoteControllerBeginFrameHint_guess));

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

        // "node" array -> nodeRows: each elem is a sub-array; push a row of its
        //   int->float values.
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

        // The initial output is the metadata's begin frame.
        trackValue = static_cast<float>(beginFrame);
    }

    void EmoteEyebrowController_enqueueValue_guess(
        EmoteEyebrowController* self, float value, float duration, float power,
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

    void EmoteEyebrowController_step(EmoteEyebrowController* self, float* out,
                                     float dt) {
        const int state = self->trackState;

        if (state != 0) {
            if (state == 2) {
                const float easedPhase =
                    std::pow(self->trackAccum / self->trackSpan,
                             1.0f / self->trackPow) +
                    self->trackInvDur * dt;
                const double powered =
                    std::pow(easedPhase, self->trackPow);
                const float previousAccum = self->trackAccum;
                const float direction = self->trackDir;
                float delta = static_cast<float>(
                    powered * self->trackSpan - previousAccum);
                const float nextValue =
                    self->trackValue + direction * delta;
                self->trackValue = nextValue;

                const float target = self->trackTarget;
                const bool overshoot = eyebrowTrackOvershoots_guess(
                    direction, target, nextValue);
                if (overshoot) {
                    // Eyebrow commits only the correction term here; it does
                    // not re-enter state 1 until the next step call.
                    delta = (target - nextValue) * direction;
                    self->trackValue = target;
                    self->trackState = 1;
                }
                self->trackAccum = previousAccum + delta;
            } else if (state == 1) {
                if (self->valueTrack8B.empty()) {
                    self->trackState = 0;
                } else {
                    const std::pair<float, float> segment =
                        self->valueTrack8B.front();
                    const float startValue = segment.first;
                    const float endValue = segment.second;
                    if (startValue == endValue) {
                        self->trackValue = endValue;
                    } else {
                        self->trackTarget = endValue;
                        self->trackValue = startValue;
                        self->trackDir =
                            ((endValue - startValue) < 0.0f) ? -1.0f : 1.0f;
                        self->trackState = 2;
                    }
                    self->valueTrack8B.pop_front();
                }
            }
        } else {
            if (!self->valueTrack12B.empty()) {
                const EmoteAngleKeyValue12B keyframe =
                    self->valueTrack12B.front();

                // Unlike Eye/Blink, the primary command remains in its deque if
                // resolver allocation/search throws; pop_front happens after
                // all nonthrowing active-curve stores.
                EmoteMeshResolver_resolve_guess(
                    &self->mesh, self->trackValue, keyframe.endRad,
                    &self->valueTrack8B);

                self->trackAccum = 0.0f;
                self->trackSpan = self->mesh.trackResolvedSpan;
                self->trackInvDur = 1.0f / keyframe.duration;
                std::memcpy(
                    &self->trackPow, &keyframe.powCount, sizeof(float));

                self->valueTrack12B.pop_front();
                self->trackState = self->trackState + 1;
            }
        }

        // No blink machine or frame-window remap exists for eyebrow output.
        *out = self->trackValue;
    }

    void EmoteEyebrowController_reset_guess(EmoteEyebrowController* self) {
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
