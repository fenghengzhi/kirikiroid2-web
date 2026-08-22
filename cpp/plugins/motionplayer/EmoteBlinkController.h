// EmoteBlinkController — controller for the emote "eye" category (deque #4,
// type 4), reconstructed from the four current 1.3.9 references.
//
// The source declaration consists, in order, of a primary 12-byte-keyframe
// deque, a secondary pair deque, the mesh-resolver graph, the value-track
// scalars, and the blink metadata/state tail. The four references agree on that
// order and ownership. Their physical offsets differ because Android uses
// libstdc++ deque/vector headers while iOS uses libc++; exact per-ABI layouts
// therefore live in analysis/ rather than in this portable declaration.
//
// This controller is a distinct non-polymorphic class, not a shared scalar base
// with EmoteEyebrowController. In particular its curve fields are declared as
// span/accum/inverse-duration/power, and it owns the independent blink phase
// machine which remaps the value-track output over [beginFrame,endFrame].
//
// When a 12-byte-track keyframe is popped, the value-track step invokes the
// shared mesh resolver with the embedded graph, current value, requested end
// value, and secondary track. The active resolver enumerates candidate segment
// paths, copies its selected pair-deque into valueTrack8B, and exposes the path
// span consumed by the power-curve state machine below.
//
#pragma once

#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

#include "tjs.h"
#include "EmoteAngleController.h"
#include "EmoteMeshResolver.h"

namespace motion {

    // Plain owning C++ object: its first source member is the primary deque and
    // there is no vptr, matching EmoteVarController and EmoteAngleController.
    struct EmoteBlinkController {
        explicit EmoteBlinkController(const tTJSVariant& dict);

        // Primary commands: {target, duration, power}.
        EmoteAngleKeyframeQueue valueTrack12B;

        // Resolver-produced path segments, each represented by two floats.
        std::deque<std::pair<float, float>> valueTrack8B;

        // Embedded graph built from the PSB "edge" and "node" arrays. The
        // resolver rebuilds valueTrack8B and publishes trackResolvedSpan here.
        EmoteMeshResolverState mesh;

        // Value-track animation state. The declaration order is significant and
        // differs from the eyebrow controller's curve-field order.
        // The constructor initializes only state/target/direction/value. The
        // resolver and state-0 setup write span/accum/inv-duration/power before
        // the animating state can read them.
        int32_t trackState;
        float   trackValue;   // constructor seeds from beginFrame
        float   trackTarget;
        float   trackDir;
        float   trackSpan;
        float   trackAccum;
        float   trackInvDur;
        float   trackPow;     // raw float bits, never integer-converted

        // Blink metadata and phase-machine state. All fields except blinkPhase
        // are populated by metadata reads or the constructor's RNG/seed path.
        int32_t beginFrame;
        int32_t endFrame;
        int32_t blinkPhase;        // 0 wait / 10 closing / 11 hold / 12 opening
        float   blinkIntervalMin;
        float   blinkIntervalMax;
        float   blinkFrameCount;
        float   blinkTimer;
        float   blinkPos;          // constructor seeds from beginFrame
        uint8_t blinkEnabled;
    };

    // Replaces or appends one primary value-track keyframe. Both the instant
    // path and the animated append=false path discard the resolver-generated
    // secondary track; append=true preserves both existing tracks.
    void EmoteBlinkController_enqueueValue_guess(
        EmoteBlinkController* self, float value, float duration, float power,
        bool append);

    // Common four-reference per-slice step semantics. Completed nonzero
    // segments re-enter the track loop immediately, so one call can consume
    // the next primary command; an equal-endpoint segment instead stops this
    // slice in state 1. The independent blink phase then runs one case and the
    // frame-window remap writes the scalar result to *out.
    void EmoteBlinkController_step(EmoteBlinkController* self, float* out,
                                   float dt);

    // Commits the final pending track value and clears the primary/secondary
    // tracks while returning the controller to idle.
    void EmoteBlinkController_reset_guess(EmoteBlinkController* self);

} // namespace motion
