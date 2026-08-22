// EmoteAngleController — non-polymorphic owning class reconstructed from all
// four current reference binaries. It owns one 12-byte-element deque; the
// deque header and the object's trailing ABI alignment differ by STL/target.
//
#pragma once

#include <cstdint>
#include <deque>

namespace motion {

    // Angle keyframe element. Accessed by field name; native +0/+4/+8 offsets
    // are provenance comments only. Natural layout (3×4B) is 12B with no
    // padding; the assertion protects the fixed queue-element boundary.
    struct EmoteAngleKeyValue12B {
        float    endRad;     // +0
        float    duration;   // +4
        float    powCount;   // +8 — copied and consumed as float bits
    };

    static_assert(sizeof(EmoteAngleKeyValue12B) == 12,
                  "angle-controller keyframe must remain three float words");

    // The same naked deque type is the first member of the eye, eyebrow and
    // mouth controllers. Their native layouts do not embed the scalar tail of
    // EmoteAngleController.
    using EmoteAngleKeyframeQueue = std::deque<EmoteAngleKeyValue12B>;

    // PLATFORM_BOUNDARY: libstdc++ references use an 80-byte deque header and
    // libc++ references use 48/24 bytes on 64/32-bit targets. The logical
    // field order and ownership are common even though local byte offsets vary.
    struct EmoteAngleController {
        // The inlined native constructor initializes exactly these three scalar
        // fields. The remaining interpolation fields intentionally retain their
        // default-initialized (indeterminate) state until setup/restore writes
        // them; spelling initializers for them would diverge from all four
        // references.
        EmoteAngleController()
            : state(0), currentRad(0.0f), targetRad(0.0f) {}
        ~EmoteAngleController() = default;

        EmoteAngleController(const EmoteAngleController&) = delete;
        EmoteAngleController& operator=(const EmoteAngleController&) = delete;

        EmoteAngleKeyframeQueue queue;

        int32_t state;          // 0=idle, 1=animating
        float   currentRad;
        float   targetRad;      // after shortest-path wrap
        float   startRad;
        float   invDuration;
        float   powCount;       // raw float word copied from keyframe
        float   phase;
    };

    // State branches are mutually exclusive. Setup pops a keyframe, applies
    // shortest-path wrapping, writes phase=0, but does not animate until the
    // next call. Completion stores phase=1 and normalizes with 6.2832f.
    void EmoteAngleController_step(EmoteAngleController* self, float* outRad, float dt);

    // Shared direct angle-controller setter. It normalizes the requested value
    // with the native truncated turn constant before either snapping or
    // enqueuing it. The source name is unavailable, hence the suffix.
    void EmoteAngleController_setTarget_guess(
        EmoteAngleController* self, float endRad, float duration,
        float powCount, bool append);

} // namespace motion
