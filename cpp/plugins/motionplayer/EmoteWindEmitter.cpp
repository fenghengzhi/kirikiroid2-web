#include "EmoteWindEmitter.h"
#include "EmoteBlinkRng.h"

namespace motion {

    // A64 retains this constructor as a standalone function; A32/i64/i32
    // inline the same stores into setWind. Only active bytes are cleared: the
    // inactive slot payload and both padding regions deliberately keep their
    // allocation contents until later writes.
    EmoteWindEmitter::EmoteWindEmitter(float startPos_, float endPos_) {
        startPos = startPos_;
        for (int i = 0; i < 128; ++i) {
            slots[i].active = 0;
        }
        endPos = endPos_;
        gate = 0;
        yHi = 1.0f;
        yLo = 0.0f;
        velocity = 0.0f;
        emitAccumulator = 0.0f;
    }

    // Every nonnegative accumulator unit performs one chance roll. A passing
    // roll claims the first inactive slot and consumes a second roll for y.
    // The later full-pool walk still consumes the chance roll, but not the y
    // roll. All active slots, including a just-created one, advance afterward.
    void EmoteWindEmitter::step(float dt) {
        float absoluteVelocity = velocity;
        if (absoluteVelocity < 0.0f) {
            absoluteVelocity = -absoluteVelocity;
        }

        const float accumulatedEmission =
            (absoluteVelocity * dt) + emitAccumulator;
        emitAccumulator = accumulatedEmission;
        if (accumulatedEmission >= 0.0f) {
            do {
                const float chanceRoll =
                    static_cast<float>(EmoteBlinkRng_nextCanonical_guess(
                        EmoteBlinkRng_getGlobal_guess()));
                if (chanceRoll < 0.0625f) {
                    int slotIndex = 0;
                    while (slotIndex != 128 && slots[slotIndex].active) {
                        ++slotIndex;
                    }
                    if (slotIndex != 128) {
                        EmoteWindParticle &particle = slots[slotIndex];
                        particle.active = 1;
                        particle.lifePos = startPos;
                        const float spawnLow = yLo;
                        const float spawnSpan = yHi - spawnLow;
                        const float positionRoll =
                            static_cast<float>(EmoteBlinkRng_nextCanonical_guess(
                                EmoteBlinkRng_getGlobal_guess()));
                        particle.yPos =
                            spawnLow + (spawnSpan * positionRoll);
                    }
                }

                const float nextAccumulator = emitAccumulator + -1.0f;
                emitAccumulator = nextAccumulator;
                if (!(nextAccumulator >= 0.0f)) {
                    break;
                }
            } while (true);
        }

        for (int i = 0; i != 128; ++i) {
            EmoteWindParticle &particle = slots[i];
            if (particle.active) {
                const float nextPosition =
                    particle.lifePos + (velocity * dt);
                particle.lifePos = nextPosition;
                const float currentVelocity = velocity;
                if ((currentVelocity > 0.0f && nextPosition > endPos)
                    || (currentVelocity < 0.0f && nextPosition < endPos)) {
                    particle.active = 0;
                }
            }
        }
    }

} // namespace motion
