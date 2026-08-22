// EmoteEyebrowController — slim controller for the emote "eyebrow" category
// (controller deque #5 / type 5), reconstructed from all four current 1.3.9
// references.
//
// This is the slim sibling of EmoteBlinkController. It embeds the same primary
// command deque, secondary pair deque and mesh-resolver graph, followed by its
// own value-track scalars and one beginFrame metadata value. The four references
// agree on this source order; their physical offsets and total sizes differ with
// pointer width and the libstdc++/libc++ container ABI and are kept in analysis/.
//
// CRITICAL DIFFERENCE vs EmoteBlinkController (eye):
//   * The slim ctor reads ONLY "beginFrame" from the PSB dict (plus
//     the "edge" / "node" arrays). It does NOT read endFrame / blinkIntervalMin
//     / blinkIntervalMax / blinkFrameCount / blinkEnabled, and does NOT call the
//     shared RNG. The eyebrow controller therefore has no blink state machine
//     or blink fields.
//   * The slim step runs the value-track machine (states 0/1/2)
//     then writes `*out = trackValue` DIRECTLY — there is NO blink-phase switch
//     and NO final [beginFrame,endFrame] blink remap. beginFrame is read by the
//     ctor only to seed trackValue; the eyebrow step never reads it again.
//
//   Its curve declaration order is accum/span/power/inverse-duration, whereas
//   Eye declares span/accum/inverse-duration/power. Together with the genuinely
//   absent blink tail, that makes this a separate class rather than a shared
//   scalar base or an inheritance relationship.
//
// When a 12-byte-track keyframe is popped, the step runs the same active mesh
// resolver used by the eye controller. It rebuilds valueTrack8B from edgeTable
// and nodeRows, then the eyebrow state machine consumes the selected segments
// with its independently laid-out scalar fields.
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

    // Plain owning C++ object whose first source member is a deque, not a vptr.
    struct EmoteEyebrowController {
        explicit EmoteEyebrowController(const tTJSVariant& dict);

        // Primary commands: {target, duration, power}.
        EmoteAngleKeyframeQueue valueTrack12B;

        // Resolver-produced path segments, each represented by two floats.
        std::deque<std::pair<float, float>> valueTrack8B;

        // Embedded graph built from the PSB "edge" and "node" arrays. The
        // resolver rebuilds valueTrack8B and publishes trackResolvedSpan here.
        EmoteMeshResolverState mesh;

        // Value-track animation state. The field order differs from Eye even
        // though the roles match. The eyebrow step advances only one state stage
        // per call. Only
        //   state/target/direction/value are initialized by the constructor;
        //   setup writes the remaining curve fields before state 2 reads them.
        int32_t trackState;
        float   trackValue;   // constructor seeds from beginFrame
        float   trackTarget;
        float   trackDir;
        float   trackAccum;
        float   trackSpan;
        float   trackPow;     // raw float bits, never integer-converted
        float   trackInvDur;

        // Read by the constructor only to seed trackValue; step never reads it.
        // No blink state-machine fields follow it.
        int32_t beginFrame;
    };

    // Replaces or appends one primary value-track keyframe. Replacing an
    // animated command clears both the 12B primary track and the resolver's
    // 8B secondary track before the new keyframe is appended.
    void EmoteEyebrowController_enqueueValue_guess(
        EmoteEyebrowController* self, float value, float duration, float power,
        bool append);

    // Advances at most one value-track stage per call: state 0 resolves one
    // primary command, state 1 consumes one secondary segment, and state 2
    // advances one ramp. It then writes trackValue directly (no blink/remap).
    void EmoteEyebrowController_step(EmoteEyebrowController* self, float* out,
                                     float dt);

    // Same two-track commit/reset topology as the eye controller.
    void EmoteEyebrowController_reset_guess(EmoteEyebrowController* self);

} // namespace motion
