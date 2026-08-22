// Spring physics used by the hair/parts and bust controller tracks.  The
// 72-byte simple-spring path is revalidated against all four current reference
// binaries; per-ABI addresses and disassembly evidence live in analysis/.
// Float literals are preserved exactly (no M_PI substitution).

#include "EmoteSpring.h"

#include <cmath>
#include <cstring>

#include "EmoteWindEmitter.h"
#include "MotionDispatch.h"
#include "ncbind.hpp"

namespace motion {

    // Runtime static initialization in every reference installs the rest/base
    // unit vector (0, 1, 0).  Only X/Y participate in the simple solver.
    static constexpr float kSpringRestUnitX = 0.0f;
    static constexpr float kSpringRestUnitY = 1.0f;

    void EmotePhysics_springStep_guess(EmoteSpringState* self,
                                       float* outX, float* outY,
                                       float inputX, float inputY,
                                       float forceX, float forceY,
                                       float dt, float outputScale,
                                       float angleRad) {
        float trackedX;
        float trackedY;

        if (self->firstFlag) {
            trackedX = self->storedX;
            trackedY = self->storedY;
            self->firstFlag = 0;
            self->prevDeltaX = trackedX - inputX;
            self->prevDeltaY = trackedY - inputY;
        } else {
            trackedX = self->prevDeltaX + inputX;
            trackedY = self->prevDeltaY + inputY;
            self->storedX = trackedX;
            self->storedY = trackedY;
        }

        const float sinAngle = sinf(-angleRad);
        const float cosAngle = cosf(angleRad);
        const float oldPosX = self->posX;
        const float springDt = self->k_b * dt;
        const float oldPosY = self->posY;
        const float oldPosZ = self->posZ;

        const float forcedSpringX =
            ((cosAngle * forceX) - (sinAngle * forceY)) * dt
            + (springDt * (trackedX - oldPosX));
        const float springY = springDt * (trackedY - oldPosY);
        const float restDt = self->k_a * dt;
        const float undampedVelZ =
            self->velZ + (springDt * (self->storedZ - oldPosZ));
        const float dampingDt = self->drag * dt;

        const float undampedVelX =
            ((cosAngle * kSpringRestUnitX) -
             (sinAngle * kSpringRestUnitY)) * restDt
            + (self->velX + forcedSpringX);
        const float undampedVelY =
            ((sinAngle * kSpringRestUnitX) +
             (cosAngle * kSpringRestUnitY)) * restDt
            + (self->velY +
               ((((sinAngle * forceX) + (cosAngle * forceY)) * dt) +
                springY));

        const float nextVelZ =
            undampedVelZ - (dampingDt * undampedVelZ);
        const float nextVelX =
            undampedVelX - (dampingDt * undampedVelX);
        const float nextVelY =
            undampedVelY - (dampingDt * undampedVelY);
        const float leverX = self->leverX;
        self->velX = nextVelX;
        self->velY = nextVelY;
        const float nextPosX = oldPosX + (nextVelX * dt);
        self->velZ = nextVelZ;
        self->posX = nextPosX;
        const float nextPosY = oldPosY + (nextVelY * dt);
        self->posY = nextPosY;
        self->posZ = oldPosZ + (nextVelZ * dt);

        const float remainingY = trackedY - nextPosY;
        const float xNumerator =
            -(((trackedX - nextPosX) * outputScale) * leverX) *
            0.0451603944f;
        const float normalizedX = atanf(xNumerator) / 0.0392699082f;
        *outX = normalizedX;

        // The X store precedes these state reads in every reference. Output
        // pointers are unchecked and may alias biasY/leverY, so keep this
        // sequence observable rather than caching both Y fields earlier.
        const float yNumerator =
            ((-(remainingY * outputScale) - self->biasY) * self->leverY) *
            0.0451603944f;
        *outY = atanf(yNumerator) / 0.0392699082f;
    }

    float EmoteWindEmitter_lookupForce_guess(
        const EmoteWindEmitter* emitter, float segmentX) {
        for (int i = 0; i < 128; ++i) {
            const EmoteWindParticle& particle = emitter->slots[i];
            if (particle.active) {
                const float halfWidth = particle.yPos * 0.5f + 4.0f;
                if (particle.lifePos - halfWidth < segmentX &&
                    particle.lifePos + halfWidth > segmentX) {
                    return particle.yPos * emitter->velocity;
                }
            }
        }
        return 0.0f;
    }

    void EmoteBustChainSpring_step_guess(
        EmoteBustChainSpring* self,
        float anchorX, float anchorY,
        float* outSeg0, float* outSeg1, float* outLastY,
        float forceX, float forceY, float dt,
        float scale, float angleRad) {
        if (self->firstFlag) {
            const float rootX = self->op[0];
            const float rootY = self->op[1];
            self->firstFlag = 0;
            self->prevDelta[0] = rootX - anchorX;
            self->prevDelta[1] = rootY - anchorY;
        } else {
            self->op[0] = self->prevDelta[0] + anchorX;
            self->op[1] = self->prevDelta[1] + anchorY;
        }

        for (int segment = 0; segment < 2; ++segment) {
            const float* const parentTarget =
                segment == 0 ? self->op : self->p[segment - 1];
            self->p[segment][0] = parentTarget[0];
            self->p[segment][1] = parentTarget[1];
            self->p[segment][2] = parentTarget[2];
            self->p[segment][1] =
                self->length[segment] + self->p[segment][1];
        }

        const float sinAngle = sinf(-angleRad);
        const float cosAngle = cosf(angleRad);
        const float rotatedForceX =
            ((cosAngle * forceX) - (sinAngle * forceY)) * dt;
        const float rotatedForceY =
            ((sinAngle * forceX) + (cosAngle * forceY)) * dt;

        for (int segment = 0; segment < 2; ++segment) {
            float* const position = self->pv[segment];
            float* const velocity = self->bp[segment];
            const float* const parentPosition =
                segment == 0 ? self->op : self->pv[segment - 1];
            const float deltaX = parentPosition[0] - position[0];
            const float deltaY = parentPosition[1] - position[1];
            const float deltaZ = parentPosition[2] - position[2];
            const float restLength = self->length[segment];
            const float distanceSquared =
                ((deltaX * deltaX) + (deltaY * deltaY)) +
                (deltaZ * deltaZ);
            if (distanceSquared > (restLength * restLength)) {
                const float distance = sqrtf(distanceSquared);
                if (distance > 0.015625f) {
                    const float invDistance = 1.0f / distance;
                    const float directionX = deltaX * invDistance;
                    const float directionY = deltaY * invDistance;
                    const float directionZ = deltaZ * invDistance;
                    const float extension = distance - restLength;
                    if (segment == 1) {
                        const float oldVelocityX = velocity[0];
                        const float oldVelocityY = velocity[1];
                        const float oldVelocityZ = velocity[2];
                        position[0] =
                            (extension * directionX) + position[0];
                        position[1] =
                            (extension * directionY) + position[1];
                        position[2] =
                            (extension * directionZ) + position[2];
                        const float projectedVelocity =
                            (self->vBound *
                             (((directionX * oldVelocityX) +
                               (directionY * oldVelocityY)) +
                              (directionZ * oldVelocityZ))) * dt;
                        velocity[0] = oldVelocityX -
                                      (directionX * projectedVelocity);
                        velocity[1] = oldVelocityY -
                                      (directionY * projectedVelocity);
                        velocity[2] = oldVelocityZ -
                                      (directionZ * projectedVelocity);
                    } else {
                        const float constraintImpulse =
                            (extension * self->bRate) * dt;
                        velocity[0] = velocity[0] +
                                      (directionX * constraintImpulse);
                        velocity[1] = (directionY * constraintImpulse) +
                                      velocity[1];
                        velocity[2] = (directionZ * constraintImpulse) +
                                      velocity[2];
                    }
                }
            }

            const float velocityBeforeForceX = velocity[0];
            velocity[0] = rotatedForceX + velocityBeforeForceX;
            const float velocityWithForceY = rotatedForceY + velocity[1];
            velocity[1] = velocityWithForceY;
            // These zero-vector multiplies remain observable for NaN dt.
            const float velocityWithForceZ = (dt * 0.0f) + velocity[2];
            velocity[2] = velocityWithForceZ;
            const float gravityDt = self->gravity * dt;
            const float gravityY = cosAngle * gravityDt;
            const float gravityZ = gravityDt * 0.0f;
            float velocityWithGravityX =
                (rotatedForceX + velocityBeforeForceX) -
                (sinAngle * gravityDt);
            const float velocityWithGravityY =
                gravityY + velocityWithForceY;
            const float velocityWithGravityZ =
                gravityZ + velocityWithForceZ;
            velocity[0] = velocityWithGravityX;
            velocity[1] = velocityWithGravityY;
            velocity[2] = gravityZ + velocityWithForceZ;

            const float oldPositionX = position[0];
            if (self->collisionCurve) {
                const float windForce = EmoteWindEmitter_lookupForce_guess(
                    self->collisionCurve, oldPositionX);
                velocityWithGravityX = windForce + velocityWithGravityX;
                velocity[0] = velocityWithGravityX;
            }

            const float positionDeltaZ = velocityWithGravityZ * dt;
            const float dampedVelocityX =
                velocityWithGravityX -
                ((velocityWithGravityX * self->frictionX) * dt);
            velocity[0] = dampedVelocityX;
            const float nextPositionX =
                (dampedVelocityX * dt) + oldPositionX;
            float* const segmentOutput =
                segment == 0 ? outSeg0 : outSeg1;
            const float dampedVelocityY =
                velocityWithGravityY -
                ((velocityWithGravityY * self->frictionY) * dt);
            velocity[1] = dampedVelocityY;
            position[0] = nextPositionX;
            const float nextPositionY =
                (dampedVelocityY * dt) + position[1];
            position[1] = nextPositionY;
            position[2] = positionDeltaZ + position[2];

            const float targetY = self->p[segment][1];
            const float xNumerator =
                (((self->p[segment][0] - nextPositionX) *
                  self->scale[segment][0]) * scale) *
                -0.0451603944f;
            *segmentOutput = atanf(xNumerator) / 0.0392699082f;
            if (segment == self->udEft) {
                const float yNumerator =
                    (((self->ofs - (targetY - nextPositionY)) *
                      self->scale[segment][1]) * scale) *
                    0.0451603944f;
                *outLastY = atanf(yNumerator) / 0.0392699082f;
            }
        }
    }

    void EmoteBustChainSpring_postBend_guess(
        EmoteBustChainSpring* self, float lastY,
        float* outSeg0, float* outSeg1, float dt) {
        const float amount = dt * 0.03125f;
        if (std::fabs(lastY) <= 28.0f) {
            self->bendS = self->bendS - amount;
            if (self->bendS < 0.0f) {
                self->bendS = 0.0f;
            }
        } else {
            self->bendS = amount + self->bendS;
            if (self->bendS > 1.0f) {
                self->bendS = 1.0f;
            }
        }

        self->bendR = std::fmod(
            self->bendR + ((self->bendS * self->bendSpd) * dt),
            6.28318531f);
        const float bend = (std::sin(self->bendR) * self->bendS) * self->bendVol;
        *outSeg1 = *outSeg1 + bend;
        *outSeg0 = *outSeg0 - bend;
    }

    // ========================================================================
    // Spring-state constructors (population path). Both use real
    // ncbPropAccessor owners. Values are fetched as TJS reals and then narrowed
    // to float; this is a numeric conversion, not a bit reinterpretation.
    // ========================================================================
    EmoteSpringState::EmoteSpringState(const tTJSVariant& dict) {
        // firstFlag = 1; stored/pos/vel are seeded from the shared zero vector.
        firstFlag = 1;
        storedX = 0.0f; storedY = 0.0f; storedZ = 0.0f;
        posX    = 0.0f; posY    = 0.0f; posZ    = 0.0f;
        velX    = 0.0f; velY    = 0.0f; velZ    = 0.0f;

        ncbPropAccessor object{tTJSVariant(dict)};

        // Per-node spring params (TJS real -> float).
        k_a = static_cast<float>(object.GetValue(
            TJS_W("gravity"), ncbTypedefs::Tag<tjs_real>(), 0,
            &detail::emoteSpringGravityHint_guess));
        k_b = static_cast<float>(object.GetValue(
            TJS_W("spring"), ncbTypedefs::Tag<tjs_real>(), 0,
            &detail::emoteSpringCoefficientHint_guess));
        drag = static_cast<float>(object.GetValue(
            TJS_W("friction"), ncbTypedefs::Tag<tjs_real>(), 0,
            &detail::emoteSpringFrictionHint_guess));
        leverX = static_cast<float>(object.GetValue(
            TJS_W("scale_x"), ncbTypedefs::Tag<tjs_real>(), 0,
            &detail::emoteSpringScaleXHint_guess));
        leverY = static_cast<float>(object.GetValue(
            TJS_W("scale_y"), ncbTypedefs::Tag<tjs_real>(), 0,
            &detail::emoteSpringScaleYHint_guess));
    }

    EmoteBustChainSpring::EmoteBustChainSpring(const tTJSVariant& dict) {
        firstFlag = 1;
        bendR = 0.0f;
        bendS = 0.0f;
        op[0] = 0.0f;
        op[1] = 0.0f;
        op[2] = 0.0f;
        collisionCurve = nullptr;
        std::memset(p, 0, sizeof(p));
        std::memset(pv, 0, sizeof(pv));
        std::memset(bp, 0, sizeof(bp));

        ncbPropAccessor object{tTJSVariant(dict)};

        gravity = static_cast<float>(object.GetValue(
            TJS_W("gravity"), ncbTypedefs::Tag<tjs_real>(), 0,
            &detail::emoteSpringGravityHint_guess));
        frictionX = static_cast<float>(object.GetValue(
            TJS_W("friction_x"), ncbTypedefs::Tag<tjs_real>(), 0,
            &detail::emoteBustChainFrictionXHint_guess));
        frictionY = static_cast<float>(object.GetValue(
            TJS_W("friction_y"), ncbTypedefs::Tag<tjs_real>(), 0,
            &detail::emoteBustChainFrictionYHint_guess));
        bRate = static_cast<float>(object.GetValue(
            TJS_W("b_rate"), ncbTypedefs::Tag<tjs_real>(), 0,
            &detail::emoteBustChainBRateHint_guess));
        vBound = static_cast<float>(object.GetValue(
            TJS_W("v_bound"), ncbTypedefs::Tag<tjs_real>(), 0,
            &detail::emoteBustChainVBoundHint_guess));
        udEft = static_cast<int32_t>(object.GetValue(
            TJS_W("ud_eft"), ncbTypedefs::Tag<tjs_int>(), 0,
            &detail::emoteBustChainUdEftHint_guess));
        bendSpd = static_cast<float>(object.GetValue(
            TJS_W("bend_spd"), ncbTypedefs::Tag<tjs_real>(), 0,
            &detail::emoteBustChainBendSpdHint_guess));
        bendVol = static_cast<float>(object.GetValue(
            TJS_W("bend_vol"), ncbTypedefs::Tag<tjs_real>(), 0,
            &detail::emoteBustChainBendVolHint_guess));

        {
            ncbPropAccessor lengthAccessor{object.GetValue(
                TJS_W("length"), ncbTypedefs::Tag<tTJSVariant>(), 0,
                &detail::controllerLengthHint_guess)};
            this->length[0] = static_cast<float>(lengthAccessor.GetValue(
                0, ncbTypedefs::Tag<tjs_real>()));
            this->length[1] = static_cast<float>(lengthAccessor.GetValue(
                1, ncbTypedefs::Tag<tjs_real>()));
        }
        {
            ncbPropAccessor scaleXAccessor{object.GetValue(
                TJS_W("scale_x"), ncbTypedefs::Tag<tTJSVariant>(), 0,
                &detail::emoteSpringScaleXHint_guess)};
            scale[0][0] = static_cast<float>(scaleXAccessor.GetValue(
                0, ncbTypedefs::Tag<tjs_real>()));
            scale[1][0] = static_cast<float>(scaleXAccessor.GetValue(
                1, ncbTypedefs::Tag<tjs_real>()));
        }
        {
            ncbPropAccessor scaleYAccessor{object.GetValue(
                TJS_W("scale_y"), ncbTypedefs::Tag<tTJSVariant>(), 0,
                &detail::emoteSpringScaleYHint_guess)};
            scale[0][1] = static_cast<float>(scaleYAccessor.GetValue(
                0, ncbTypedefs::Tag<tjs_real>()));
            scale[1][1] = static_cast<float>(scaleYAccessor.GetValue(
                1, ncbTypedefs::Tag<tjs_real>()));
        }

        const float restLen0 = this->length[0];
        const float restLen1 = this->length[1];
        const float ux = kSpringRestUnitX;
        const float uy = kSpringRestUnitY;
        const float uz = 0.0f;
        p[0][0] = restLen0 * ux;
        p[0][1] = restLen0 * uy;
        p[0][2] = restLen0 * uz;
        pv[0][0] = p[0][0];
        pv[0][1] = p[0][1];
        pv[0][2] = p[0][2];
        p[1][0] = restLen1 * ux;
        p[1][1] = restLen1 * uy;
        p[1][2] = restLen1 * uz;
        pv[1][0] = p[1][0];
        pv[1][1] = p[1][1];
        pv[1][2] = p[1][2];
        bp[0][0] = 0.0f;
        bp[0][1] = 0.0f;
        bp[0][2] = 0.0f;
        bp[1][0] = 0.0f;
        bp[1][1] = 0.0f;
        bp[1][2] = 0.0f;
    }

} // namespace motion
