// EmoteVarController free-function implementation.
// Aligned with libkrkr2.so sub_667030 + sub_666BF8.

#include "EmoteVarController.h"

#include <cmath>
#include <cstring>

namespace motion {

    // Aligned with libkrkr2.so sub_667030 EmoteVarController_ctor_20Bdeque
    //   @ 0x667030.
    // Decompiled pseudocode (this conversation):
    //   memset(self, 0, 0x50);              // zero 80-byte deque header
    //   sub_6878D8(self, 0);                // init empty deque (one reserved block)
    //   v5 = is_mul_ok(count,4) ? 4*count : -1;   // BYTES
    //   *(self+80) = count; *(self+84) = 0;
    //   currentValue = operator new[](v5);  // 4*count BYTES = count floats
    //   targetValue  = operator new[](v5);
    //   startValue   = operator new[](v5);
    //   memset(currentValue, 0, 4*count);   // count floats
    //   memset(targetValue,  0, 4*count);
    //   memset(startValue,   0, 4*count);
    // NOTE: binary allocates `count` floats per array (4*count BYTES), NOT
    //   count*4 floats. The previous local code over-allocated 4x.
    void EmoteVarController_ctor(EmoteVarController* self, int count) {
        // queue is already default-constructed empty by C++ struct init
        // (the deque header memset + sub_6878D8 init are replicated by the
        //  std::deque default ctor under the PLATFORM_BOUNDARY ABI note).
        self->count = count;        // *(self+80) = count
        self->state = 0;            // *(self+84) = 0
        self->currentValue = new float[count]();  // operator new[](4*count) + memset
        self->targetValue  = new float[count]();
        self->startValue   = new float[count]();
        self->powCount = 0;
        self->phase = 0.0f;
        self->invDuration = 0.0f;
        self->pad = 0;
    }

    // Aligned with libkrkr2.so sub_666BF8 EmoteVarController_step @ 0x666BF8.
    // Decompiled pseudocode (this conversation). All loops bounded by
    //   count = *(int*)(self+80). out (a2) receives count floats.
    //
    //   v4 = state;
    //   if (state == 0) {
    //     if (deque empty) goto WRITE_OUT;          // (a1+16)==(a1+48): emit current
    //     for (i=0; i<count; ++i) {                 // SIMD x8 + scalar tail in binary
    //       targetValue[i] = currentValue[i];        // (a1+96)[i] = (a1+88)[i]
    //       startValue[i]  = element[i];             // (a1+104)[i] = *(float*)(elem + 4*i)
    //     }
    //     state = 1;                                 // *(a1+84) = 1
    //     invDuration = 1.0 / element.duration;      // *(float*)(elem+12)
    //     powCount    = element.powCount;            // *(uint32_t*)(elem+16)
    //     pop_front(deque);                          // advance (a1+16), free block if last
    //     phase = 0.0f;
    //     // falls through into state==1 update (v33==1)
    //   } else if (state != 1) goto WRITE_OUT;
    //
    //   // state == 1 update:
    //   phase += invDuration * dt;
    //   if (phase >= 1.0f) {
    //     phase = 1.0f;
    //     for (i=0;i<count;++i) currentValue[i] = startValue[i];  // (a1+88)[i]=(a1+104)[i]
    //     state = 0;
    //   } else {
    //     f = powf(phase, powCount);   // powCount@+112 read as float (raw bits), NO SCVTF
    //     for (i=0;i<count;++i)                      // current = target + f*(start-target)
    //       currentValue[i] = targetValue[i] + f*(startValue[i] - targetValue[i]);
    //   }
    //   WRITE_OUT:
    //   for (i=0;i<count;++i) out[i] = currentValue[i];
    //
    // NOTE: the binary's array roles are: +96 ("targetValue") holds the lerp
    //   SOURCE (snapshot of currentValue at keyframe start); +104
    //   ("startValue") holds the lerp DESTINATION (element channel values).
    //   The lerp is current = target + f*(start-target), so it ramps from the
    //   old current toward the element values. Names kept for layout fidelity.
    //
    // PLATFORM_BOUNDARY: the binary updates 8 floats at a time via NEON
    //   float32x4_t intrinsics with a scalar tail; here we use a scalar loop
    //   over [0,count). Numerical results match.
    void EmoteVarController_step(EmoteVarController* self, float* out, float dt) {
        const int count = self->count;  // *(int*)(self+80) — drives every loop
        if (self->state == 0) {
            if (self->queue.empty()) {
                // (a1+16)==(a1+48): no keyframe pending — fall to WRITE_OUT.
                goto write_out;
            }
            const EmoteVarKeyValue20B& elem = self->queue.front();
            // Binary reads element channels as *(float*)(elem + 4*i) for
            //   i in [0,count). For count==4 index 3 aliases duration@+12,
            //   matching the binary byte-for-byte; index from the element base.
            const float* elemChannels = reinterpret_cast<const float*>(&elem);
            for (int i = 0; i < count; ++i) {
                self->targetValue[i] = self->currentValue[i];  // +96[i] = +88[i]
                self->startValue[i]  = elemChannels[i];        // +104[i] = *(float*)(elem+4*i)
            }
            self->state = 1;
            self->invDuration = 1.0f / elem.duration;          // 1.0 / *(float*)(elem+12)
            // *(_DWORD *)(a1 + 112) = *(_DWORD *)(elem + 16) — RAW 32-bit copy.
            //   The binary moves the word verbatim (no int<->float conversion).
            //   Both fields are `float`, so a plain assignment IS the bit copy;
            //   memcpy makes the raw-bit intent explicit and immune to any future
            //   field-type drift.
            std::memcpy(&self->powCount, &elem.powCount, sizeof(float));
            self->queue.pop_front();
            self->phase = 0.0f;
            // fall through into the state==1 update (binary: v33==1 -> LABEL_24)
        } else if (self->state != 1) {
            goto write_out;
        }

        // state == 1 update
        self->phase += self->invDuration * dt;
        if (self->phase >= 1.0f) {
            self->phase = 1.0f;
            for (int i = 0; i < count; ++i) {
                self->currentValue[i] = self->startValue[i];   // +88[i] = +104[i]
            }
            self->state = 0;
        } else {
            // powf(phase, *(float*)(a1+112)) — the exponent is read as a float
            //   directly (LDR S1, [X20,#0x70] @0x666df4; NO SCVTF). self->powCount
            //   IS already a float (raw bits from the keyframe), so it is passed
            //   straight in — no static_cast<float>(int) round-trip.
            const float f = std::pow(self->phase, self->powCount);
            for (int i = 0; i < count; ++i) {
                self->currentValue[i] = self->targetValue[i] +
                    f * (self->startValue[i] - self->targetValue[i]);
            }
        }

    write_out:
        for (int i = 0; i < count; ++i) {
            out[i] = self->currentValue[i];
        }
    }

    // sub_66713C @0x66713C.
    void EmoteVarController_resetLike_0x66713C(EmoteVarController* self) {
        if(!self) {
            return;
        }
        if(!self->queue.empty()) {
            self->state = 0;
            const EmoteVarKeyValue20B &last = self->queue.back();
            const float *channels = reinterpret_cast<const float *>(&last);
            for(int i = 0; i < self->count; ++i) {
                self->currentValue[i] = channels[i];
            }
            self->queue.clear();
            return;
        }
        if(self->state != 0) {
            self->state = 0;
            for(int i = 0; i < self->count; ++i) {
                self->currentValue[i] = self->startValue[i];
            }
        }
    }

    void EmoteVarController_dtor(EmoteVarController* self) {
        delete[] self->currentValue;  self->currentValue = nullptr;
        delete[] self->targetValue;   self->targetValue  = nullptr;
        delete[] self->startValue;    self->startValue   = nullptr;
    }

} // namespace motion
