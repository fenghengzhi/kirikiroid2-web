// EmoteAngleController free-function implementation.
// Aligned with libkrkr2.so sub_6867B0 + sub_666634.

#include "EmoteAngleController.h"

#include <cmath>
#include <cstring> // std::memcpy for the raw-bits powCount reinterpret

namespace motion {

    namespace {
        constexpr float kPi = 3.14159265358979323846f;  // shortest-path threshold (binary 3.14159265)
    }

    // Aligned with libkrkr2.so sub_6867B0 EmoteAngleController_ctor_12Bdeque
    //   @ 0x6867B0.
    // Caller does memset(self, 0, 0x50) before this call (deque header zero);
    //   here we ensure all animation-state fields are zero and queue empty.
    // count parameter is signaled as 0 by the EmoteEngine_ctor caller (the
    //   angle controller only ever drives a single scalar) but we keep the
    //   parameter for signature parity with the binary.
    void EmoteAngleController_ctor(EmoteAngleController* self, int /*count*/) {
        // queue default-constructed empty by C++ struct init.
        self->state = 0;
        self->currentRad = 0.0f;
        self->targetRad  = 0.0f;
        self->startRad   = 0.0f;
        self->invDuration = 0.0f;
        self->powCount = 0;
        self->phase = 0.0f;
        self->pad = 0;
    }

    // Aligned with libkrkr2.so sub_666634 EmoteAngleController_step @ 0x666634.
    // Decompiled pseudocode (this conversation, IDA @ 0x666634):
    //   v4 = state(+80);
    //   if (v4 == 1) {                         // ANIMATE — checked FIRST
    //     v11 = invDuration(+96)*dt + phase(+104);  phase(+104) = v11;
    //     if (v11 >= 1.0) {
    //       v13 = targetRad(+88);              // destination
    //       phase(+104) = 1.0;                 // NOT 0
    //       wrap v13 into [0,6.2832) by ±6.2832 loops;
    //       currentRad(+84) = v13;  state(+80) = 0;
    //     } else {
    //       i = pow(v11,powCount) * (targetRad(+88) - startRad(+92)) + startRad(+92);
    //       wrap i into [0,6.2832) by ±6.2832 loops;
    //       currentRad(+84) = i;
    //     }
    //   } else if (v4 == 0) {                  // SETUP — does NOT fall through to animate
    //     if (back(+48) != front(+16)) {       // queue non-empty
    //       v7 = currentRad(+84);  startRad(+92) = v7;            // source = old current
    //       v8 = elem.endRad;      targetRad(+88) = elem.endRad;  // destination
    //       if (v8 > v7) { if (v8-v7 > pi) targetRad(+88) = v8 - 6.28318531; }  // shortest path
    //       else         { if (v7-v8 > pi) targetRad(+88) = v8 + 6.28318531; }
    //       state(+80) = 1;
    //       invDuration(+96) = 1.0 / elem.duration(+4);           // no zero guard
    //       powCount(+100) = elem.powCount(+8);
    //       pop_front;                          // binary does NOT write phase(+104) here
    //     }
    //   }
    //   *out = currentRad(+84);
    //
    // FIDELITY NOTES vs the previous local paraphrase (all decompile-confirmed):
    //   1. Branches are mutually exclusive — SETUP does not advance phase in the
    //      same call; animation begins on the NEXT step.
    //   2. SETUP never resets phase; COMPLETION stores 1.0 (not 0.0). So a reused
    //      controller's subsequent keyframes start at phase>=1.0 and snap — this
    //      matches the binary and must be preserved (CLAUDE.md: replicate quirks).
    //   3. Result wrap uses the truncated literal 6.2832 via iterative add/sub
    //      and is stored back into currentRad(+84); the shortest-path adjust in
    //      SETUP uses the accurate 6.28318531 — two distinct constants.
    //   field roles: targetRad(+88) = destination (element, shortest-path wrapped);
    //      startRad(+92) = source (old current). Matches binary a1+88 / a1+92.
    void EmoteAngleController_step(EmoteAngleController* self, float* outRad, float dt) {
        if (self->state == 1) {
            const float p = self->invDuration * dt + self->phase;  // v11
            self->phase = p;                                       // *(a1+104) = v11
            if (p >= 1.0f) {
                float v = self->targetRad;                         // v13 = *(a1+88) destination
                self->phase = 1.0f;                                // *(a1+104) = 1.0 (1065353216)
                while (v < 0.0f)     v += 6.2832f;
                while (v >= 6.2832f) v -= 6.2832f;
                self->currentRad = v;                              // *(a1+84)
                self->state = 0;
            } else {
                float v = std::pow(p, self->powCount)
                          * (self->targetRad - self->startRad) + self->startRad;
                while (v < 0.0f)     v += 6.2832f;
                while (v >= 6.2832f) v -= 6.2832f;
                self->currentRad = v;                              // *(a1+84)
            }
        } else if (self->state == 0) {
            if (!self->queue.empty()) {                            // *(a1+48) != *(a1+16)
                const EmoteAngleKeyValue12B& elem = self->queue.front();
                const float cur = self->currentRad;                // v7 = *(a1+84)
                self->startRad = cur;                              // *(a1+92) = current (source)
                float dest = elem.endRad;                          // v8
                if (dest > cur) {
                    if (dest - cur > kPi) dest -= 6.28318531f;     // shortest path (accurate 2pi)
                } else {
                    if (cur - dest > kPi) dest += 6.28318531f;
                }
                self->targetRad = dest;                            // *(a1+88) = destination
                self->state = 1;                                   // *(a1+80) = 1
                self->invDuration = 1.0f / elem.duration;          // *(a1+96) = 1.0/elem.duration
                // powCount(+100) = keyframe[+8] RAW BITS: binary writes
                //   *(a1+100) = *(uint*)(v6+8) (DWORD copy) and reads it via
                //   pow(v11, *(float*)(a1+100)) at 0x6666f8 (no SCVTF). Raw float
                //   bit reinterpret, not int->float (same class as eye/eyebrow/
                //   mouth/transition, fixed 2316276/2870209).
                std::memcpy(&self->powCount, &elem.powCount, sizeof(float));
                self->queue.pop_front();                           // advance front / free block
                // NOTE: binary does NOT write phase (a1+104) in the setup branch.
            }
        }
        *outRad = self->currentRad;                                // *a2 = *(a1+84)
    }

    void EmoteAngleController_dtor(EmoteAngleController* /*self*/) {
        // No heap arrays to release.
    }

} // namespace motion
