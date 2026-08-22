// PlayerUpdateParticles.cpp — updateLayers particle emitter and particle system phases
// Split from PlayerUpdateLayers.cpp for maintainability.
//
#include "PlayerUpdateLayersInternal.h"
#include "MotionDispatch.h"

namespace motion::internal {
    void propagateParticleEvaluatedOpacityToChildRoot_guess(
        const detail::MotionNode &particleNode,
        detail::MotionNode &childRoot) {
        const int opacity = particleNode.accumulated.opacity;
        if(childRoot.delta.opacity != opacity) {
            childRoot.delta.dirty = true;
            childRoot.delta.opacity = opacity;
        }
    }

    std::array<float, 4> computeParticleOutsideRect_guess(
        const std::array<float, 4> &targetRect,
        double m11, double m12, double m21, double m22,
        float affineTranslateX, float affineTranslateY,
        float cameraOffsetX, float cameraOffsetY,
        double outsideFactor) {
        const double determinant = m11 * m22 - m21 * m12;
        const double inverseM11 = m22 / determinant;
        const double inverseM12 = -m12 / determinant;
        const double inverseM21 = -m21 / determinant;
        const double inverseM22 = m11 / determinant;

        // The four references promote the stored float translations and camera
        // offsets, finish each inverse-translation sum in double, and narrow
        // only that completed sum before the corner transforms.
        const double translatedX =
            inverseM11 * static_cast<double>(affineTranslateX) +
            inverseM12 * static_cast<double>(affineTranslateY);
        const double translatedY =
            inverseM21 * static_cast<double>(affineTranslateX) +
            inverseM22 * static_cast<double>(affineTranslateY);
        const float offsetX = static_cast<float>(
            translatedX + static_cast<double>(cameraOffsetX));
        const float offsetY = static_cast<float>(
            translatedY + static_cast<double>(cameraOffsetY));

        const auto transformPoint = [&](float x, float y) {
            return std::array<float, 2>{
                static_cast<float>(
                    inverseM11 * static_cast<double>(x) +
                    inverseM12 * static_cast<double>(y) +
                    static_cast<double>(offsetX)),
                static_cast<float>(
                    inverseM21 * static_cast<double>(x) +
                    inverseM22 * static_cast<double>(y) +
                    static_cast<double>(offsetY))
            };
        };
        const auto p0 = transformPoint(targetRect[0], targetRect[1]);
        const auto p1 = transformPoint(targetRect[2], targetRect[1]);
        const auto p2 = transformPoint(targetRect[2], targetRect[3]);
        const auto p3 = transformPoint(targetRect[0], targetRect[3]);

        // Ordered selects deliberately choose the right operand for equal
        // signed zeros and unordered operands, matching the SIMD compare/select
        // chains instead of std::min/std::max NaN behavior.
        const auto orderedMin = [](float lhs, float rhs) {
            return lhs < rhs ? lhs : rhs;
        };
        const auto orderedMax = [](float lhs, float rhs) {
            return lhs > rhs ? lhs : rhs;
        };
        const float left = orderedMin(
            orderedMin(orderedMin(p0[0], p1[0]), p2[0]), p3[0]);
        const float top = orderedMin(
            orderedMin(orderedMin(p0[1], p1[1]), p2[1]), p3[1]);
        const float right = orderedMax(
            orderedMax(orderedMax(p0[0], p1[0]), p2[0]), p3[0]);
        const float bottom = orderedMax(
            orderedMax(orderedMax(p0[1], p1[1]), p2[1]), p3[1]);

        // The midpoint addition happens in float before promotion to double.
        // Each final edge is narrowed independently after raw outsideFactor
        // arithmetic; negative, infinite and NaN factors are not normalized.
        const double centerX = static_cast<double>(
            static_cast<float>(left + right)) * 0.5;
        const double centerY = static_cast<double>(
            static_cast<float>(top + bottom)) * 0.5;
        return {
            static_cast<float>(centerX + outsideFactor *
                (static_cast<double>(left) - centerX)),
            static_cast<float>(centerY + outsideFactor *
                (static_cast<double>(top) - centerY)),
            static_cast<float>(centerX + outsideFactor *
                (static_cast<double>(right) - centerX)),
            static_cast<float>(centerY + outsideFactor *
                (static_cast<double>(bottom) - centerY))
        };
    }

    bool particleBoundsStrictlyOverlapOutsideRect_guess(
        double boundsMinX, double boundsMinY,
        double boundsMaxX, double boundsMaxY,
        const std::array<float, 4> &outsideRect) {
        if(boundsMaxX < boundsMinX || boundsMaxY < boundsMinY) {
            return true;
        }
        return boundsMaxX > static_cast<double>(outsideRect[0]) &&
               boundsMinX < static_cast<double>(outsideRect[2]) &&
               boundsMaxY > static_cast<double>(outsideRect[1]) &&
               boundsMinY < static_cast<double>(outsideRect[3]);
    }
}

namespace motion {
    void Player::updateEmitterCrossfadeDelta_guess(
        detail::MotionNode &emitter) {
        const auto &active = emitter.activeSlot();
        const auto &other = emitter.otherSlot();
        if(!active.crossfading || other.done) {
            return;
        }

        // The native helper deliberately samples the position interpolator at
        // two nearby times and stores their difference as an unscaled delta.
        const double ratio =
            (_clampedEvalTime - active.clipStartTime) /
            (other.clipStartTime - active.clipStartTime);
        const auto sampleTimes = positionDerivativeSampleTimes_guess(ratio);

        const double src[3] = {active.x, active.y, active.z};
        const double dst[3] = {other.x, other.y, other.z};
        double first[3] = {};
        double second[3] = {};
        evaluatePositionInterpolation_guess(
            active.cccVariant, dst, src, first,
            emitter.coordinateMode, active.cpVariant, sampleTimes.first);
        evaluatePositionInterpolation_guess(
            active.cccVariant, dst, src, second,
            emitter.coordinateMode, active.cpVariant, sampleTimes.second);

        emitter.emitterOffsetActive = true;
        emitter.emitterOffsetX = second[0] - first[0];
        emitter.emitterOffsetY = second[1] - first[1];
        emitter.emitterOffsetZ = second[2] - first[2];
    }

    void Player::updateLayersPhase3_ParticleEmitter() {
        auto &nodes = _nodes;
        // Four-reference type-6 emitter pass. Preview disables the complete
        // pass. Each live emitter retains active `src` as its identity and uses
        // model.{dt,dtgt,timeOffset} for offset mode, target label, and timer
        // offset.
        if (_preview) return;

        for (size_t ei = 1; ei < nodes.size(); ++ei) {
            auto &en = nodes[ei];
            if (en.nodeType != 6) continue;

            // Inactive, done, or null-backed src clears the persistent
            // retained target and timer before advancing to the next node.
            if (!en.accumulated.active || en.activeSlot().done) {
                en.emitterActive = false;
                en.emitterDtgt.Clear();
                en.emitterTimer = 0.0;
                continue;
            }

            const ttstr &dtgt = en.activeSlot().srcValue;
            if (dtgt.IsEmpty()) {
                en.emitterActive = false;
                en.emitterDtgt.Clear();
                en.emitterTimer = 0.0;
                continue;
            }

            // With a zero node flags byte, native code always accumulates time.
            // Otherwise it initializes or re-resolves when src changed.
            bool doAccumulate;

            if (!en.flags) {
                doAccumulate = true;
            } else if (!en.emitterActive) {
                doAccumulate = false;
            } else if (en.emitterDtgt == dtgt) {
                doAccumulate = true;
            } else {
                doAccumulate = false;
            }

            if (doAccumulate) {
                en.emitterTimer += _deltaTime;
            } else {
                en.emitterActive = true;
                en.emitterDtgt = dtgt;
                // Timer = parent time - frame time + model.timeOffset.
                // Parameterized nodes read their bound value; ordinary nodes
                // read frameTick.
                auto *parameterEntry = resolveNodeParameterEntry(*this, en);
                double parentTime =
                    parameterEntry ? parameterEntry->value : _frameTickCount;
                double startTime = en.activeSlot().clipStartTime;
                double timeOffset = en.activeSlot().modelTimeOffset;
                en.emitterTimer = (parentTime - startTime) + timeOffset;
            }

            // Offset validity is cleared after both timer branches.
            en.emitterOffsetActive = false;

            const int triggerType = en.activeSlot().modelDt;

            switch (triggerType) {
            case 4: {
                // model.dt==4 resolves model.dtgt through the ordered raw-label
                // map and stores target.pos - emitter.pos. The shared Player
                // lookup consumes its mapped deque index without a bounds gate.
                const auto *target = findNodeByRawLabel_guess(
                    en.activeSlot().modelDtgt, false);
                if(target != nullptr) {
                    en.emitterOffsetActive = true;
                    en.emitterOffsetX = target->accumulated.posX - en.accumulated.posX;
                    en.emitterOffsetY = target->accumulated.posY - en.accumulated.posY;
                    en.emitterOffsetZ = target->accumulated.posZ - en.accumulated.posZ;
                }
                break;
            }
            case 3: {
                updateEmitterCrossfadeDelta_guess(en);
                break;
            }
            case 2: {
                // model.dt==2 uses the crossfade derivative while the Player is
                // in its initial-update state or the model timer equals zero.
                if (_noUpdateYet || en.emitterTimer == 0.0) {
                    updateEmitterCrossfadeDelta_guess(en);
                } else {
                    // A running timer uses this node's accumulated position delta.
                    en.emitterOffsetActive = true;
                    en.emitterOffsetX = en.deltaPosX;
                    en.emitterOffsetY = en.deltaPosY;
                    en.emitterOffsetZ = en.deltaPosZ;
                }
                break;
            }
            default:
                break;
            }
        }
    }

    void Player::stepParticleChildren_guess(
        detail::MotionNode &particleNode) {
        // The native source keeps this as a separate two-pass worker. It owns
        // one Array receiver across both passes, independently of the outer
        // type-4 node pass. Count is a signed script result, not vector size.
        detail::ScopedParticleArrayDispatch_guess particleArray(
            particleNode.particleArrayVar);
        auto *array = particleArray.get();

        int childCount = static_cast<int>(
            detail::particleArrayCount_guess(array));
        // Pass 1 deletes stopped children. With deleteOutside, ordered
        // inverted bounds are retained before the viewport tests; strict edge
        // contact and unordered/NaN comparisons fail overlap and are erased.
        for(int childIndex = 0; childIndex < childCount; ++childIndex) {
            auto *child = detail::particleArrayGetNativePlayerAt_guess(
                array, childIndex);
            bool shouldErase = false;
            if(child->_allplaying) {
                if(particleNode.particleDeleteOutside) {
                    shouldErase = !internal::
                        particleBoundsStrictlyOverlapOutsideRect_guess(
                            child->_boundsMinX, child->_boundsMinY,
                            child->_boundsMaxX, child->_boundsMaxY,
                            _rootPlayer->_particleOutsideRect);
                }
            } else {
                shouldErase = true;
            }

            if(shouldErase) {
                // The numeric-get helper has already destroyed its own indexed
                // element Variant. Erase receives a separate default result
                // Variant, which remains alive through the post-delete count
                // read before this scope ends, exactly as in all four targets.
                tTJSVariant eraseResult;
                detail::particleArrayErase_guess(
                    array, childIndex, &eraseResult);
                // Only this erase edge refreshes count. The decremented index
                // and loop increment retry the same numeric slot. If script
                // erase reports success/failure without shrinking the Array,
                // native can therefore remain on that slot indefinitely.
                childCount = static_cast<int>(
                    detail::particleArrayCount_guess(array));
                --childIndex;
            }
        }

        // Pass 2 freezes the final first-pass count. Re-entrant Array mutation
        // by any child operation below does not refresh its numeric upper
        // bound. Mesh inheritance selects the particle node only for the
        // separator state; otherwise it forwards the existing ancestor.
        detail::MotionNode *meshParent = particleNode.meshInheritanceSeparator_guess
            ? &particleNode : particleNode.meshAncestor;
        for(int childIndex = 0; childIndex < childCount; ++childIndex) {
            auto *child = detail::particleArrayGetNativePlayerAt_guess(
                array, childIndex);
            // Per-child order is camera angle, optional direct-edit init,
            // three root-link stores, frame progress, layer update, then
            // prepend-and-clear of the child's pending event range.
            child->_cameraAngle = _cameraAngle;
            if(child->_directEdit) {
                child->initEmoteMotion_guess(2u);
            }

            auto &root = child->_nodes[0];
            root.clipAABB = particleNode.clipAABB;
            root.meshAncestor = meshParent;
            root.visibleAncestorIndex = particleNode.visibleAncestorIndex;

            child->frameProgress(_deltaTime);
            child->updateLayers();
            aggregateChildPendingEvents_guess(*child);
        }
    }

    void Player::updateLayersPhase3_ParticleSystem() {
        // Current four-reference type-4 particle-system pass.  Velocity lives
        // on each child Player; frameProgress applies velocity and damping.
        if (_preview) return;
        auto &nodes = _nodes;
        // All four current references consume the same speed-scaled frame
        // delta here that was prepared by the Player progress path.  There is
        // no second raw-delta input in this particle-system pass.
        const double dt = _deltaTime;
        constexpr double PI = 3.14159265358979323846;

        for (size_t pi = 1; pi < nodes.size(); ++pi) {
            auto &pn = nodes[pi];
            if (pn.nodeType != 4) continue;

            // Native snapshots the active-slot index at node entry, before
            // retaining the child Array or invoking its count getter. Keep a
            // reference to that exact slot across every later re-entrant
            // dispatch in this node pass.
            const auto &particleSlot = pn.activeSlot();
            detail::ScopedParticleArrayDispatch_guess particleArray(
                pn.particleArrayVar);
            auto *array = particleArray.get();

            // Existing children inherit the parent transform before the
            // activity/emission gates. Inactive or completed emitters therefore
            // still update already-created children.

            const int childCount = static_cast<int>(
                detail::particleArrayCount_guess(array));

            if (pn.particleInheritVelocity == 2) {
                bool addTranslationOnly = true;
                if(!particleSlot.done && pn.particleInheritAngle) {
                    const double curM11 = pn.accumulated.m11;
                    const double curM21 = pn.accumulated.m21;
                    const double curM12 = pn.accumulated.m12;
                    const double curM22 = pn.accumulated.m22;
                    const bool matrixChanged =
                        curM11 != pn.prevM11 || curM21 != pn.prevM21 ||
                        curM12 != pn.prevM12 || curM22 != pn.prevM22;

                    if(matrixChanged) {
                        addTranslationOnly = false;

                        // Native commits both snapshots before testing the
                        // child count or unwrapping Array elements. Empty or
                        // negative counts still advance them, and an indexed
                        // getter exception leaves the committed prefix intact.
                        const double oldM11 = pn.prevM11;
                        const double oldM21 = pn.prevM21;
                        const double oldM12 = pn.prevM12;
                        const double oldM22 = pn.prevM22;
                        pn.prevM11 = curM11;
                        pn.prevM21 = curM21;
                        pn.prevM12 = curM12;
                        pn.prevM22 = curM22;

                        const double curAngle = pn.accumulated.angle;
                        const double rawAngleDelta =
                            curAngle - pn.prevParticleAngle;
                        const double angleDelta =
                            pn.accumulated.flipX == pn.accumulated.flipY
                                ? rawAngleDelta
                                : -rawAngleDelta;
                        pn.prevParticleAngle = curAngle;

                        if(childCount >= 1) {
                            // The subtraction-pair form is inv(old) * current.
                            // The two off-diagonal old-matrix terms are divided
                            // without pre-negation.
                            const double det =
                                oldM11 * oldM22 - oldM12 * oldM21;
                            const double id = 1.0 / det;
                            const double idM22 = oldM22 * id;
                            const double idM21 = oldM21 * id;
                            const double idM12 = oldM12 * id;
                            const double idM11 = oldM11 * id;
                            const double t11 =
                                curM11 * idM22 - curM21 * idM12;
                            const double t12 =
                                curM21 * idM11 - curM11 * idM21;
                            const double t21 =
                                curM12 * idM22 - curM22 * idM12;
                            const double t22 =
                                curM22 * idM11 - curM12 * idM21;

                            const double posXref = pn.accumulated.posX;
                            const double posYref = pn.accumulated.posY;
                            const double posZref = pn.accumulated.posZ;
                            const double dPosX = pn.deltaPosX;
                            const double dPosY = pn.deltaPosY;
                            const double dPosZ = pn.deltaPosZ;

                            for(int ci = 0; ci < childCount; ++ci) {
                                auto *child = detail::
                                    particleArrayGetNativePlayerAt_guess(
                                        array, ci);
                                auto &cr = child->_nodes[0];

                                if(child->_directEdit) {
                                    double childAngle =
                                        child->_emoteAngle + angleDelta;
                                    while(childAngle < 0.0) childAngle += 360.0;
                                    while(childAngle >= 360.0)
                                        childAngle -= 360.0;
                                    child->_emoteAngle = childAngle;
                                    child->initEmoteMotion_guess(2u);
                                } else {
                                    double childAngle =
                                        cr.accumulated.angle + angleDelta;
                                    while(childAngle < 0.0) childAngle += 360.0;
                                    while(childAngle >= 360.0)
                                        childAngle -= 360.0;
                                    cr.accumulated.angle = childAngle;
                                }

                                auto *transformedRoot = &child->_nodes[0];
                                const int coordinateMode = pn.coordinateMode;
                                if(coordinateMode == 1) {
                                    const double x =
                                        transformedRoot->accumulated.posX -
                                        posXref + dPosX;
                                    const double z =
                                        transformedRoot->accumulated.posZ -
                                        posZref + dPosZ;
                                    transformedRoot->accumulated.posX =
                                        posXref + t11 * x + t12 * z;
                                    transformedRoot->accumulated.posZ =
                                        posZref + t21 * x + t22 * z;
                                    transformedRoot->accumulated.posY += dPosY;
                                } else {
                                    const double x =
                                        transformedRoot->accumulated.posX -
                                        posXref + dPosX;
                                    const double y =
                                        transformedRoot->accumulated.posY -
                                        posYref + dPosY;
                                    transformedRoot->accumulated.posX =
                                        transformedRoot->accumulated.posZ +
                                        dPosZ;
                                    transformedRoot->accumulated.posY =
                                        posYref + t21 * x + t22 * y;
                                    transformedRoot->accumulated.posZ =
                                        posXref + t11 * x + t12 * y;
                                }

                                const double velocityX =
                                    child->_cameraVelocityX;
                                const double velocityOther =
                                    coordinateMode == 1
                                        ? child->_cameraVelocityZ
                                        : child->_cameraVelocityY;
                                child->_cameraVelocityX =
                                    t11 * velocityX + t12 * velocityOther;
                                const double transformedOther =
                                    t21 * velocityX + t22 * velocityOther;
                                if(coordinateMode == 1) {
                                    child->_cameraVelocityZ = transformedOther;
                                } else {
                                    child->_cameraVelocityY = transformedOther;
                                }
                            }
                        }
                    }
                }

                if(addTranslationOnly && childCount >= 1) {
                    for(int ci = 0; ci < childCount; ++ci) {
                        auto *child = detail::
                            particleArrayGetNativePlayerAt_guess(
                                array, ci);
                        auto &root = child->_nodes[0];
                        root.accumulated.posX += pn.deltaPosX;
                        root.accumulated.posY += pn.deltaPosY;
                        root.accumulated.posZ += pn.deltaPosZ;
                    }
                }
            }

            // Only accumulated inactivity clears the persistent emitter-active
            // byte. A completed slot merely skips creation for this frame.
            {
            int emitCount = 0;
            if (!pn.accumulated.active) {
                pn.particleEmitterFlagActive = false;
                goto physics_step;
            }

            if (particleSlot.done) goto physics_step;
            {
                // The system consumes the evaluator-output mirror, not the
                // slot's prt block directly. HM3 restore first targets a real
                // slot; the subsequent evaluator copy publishes this mirror.
                const double prtFmin = pn.particleInterp[0];
                const double prtF = pn.particleInterp[1];
                const int prtTrigger = particleSlot.prtTrigger;

                if (prtTrigger == 0 && prtFmin == 0.0) goto physics_step;

                const bool wasActive = pn.particleEmitterFlagActive;
                pn.particleEmitterFlagActive = true;

                // Frequency/count dispatch reads the active slot directly;
                // there is no persistent node-level trigger mirror.
                const int triggerType = particleSlot.prtTrigger;

                if (triggerType == 0) {
                    if (!wasActive) {
                        // First activation interpolates in frequency domain.
                        double freq0 = 60.0 / prtFmin;
                        double freq1 = 60.0 / prtF;
                        if (freq0 != freq1)
                            freq0 = freq0 + (freq1 - freq0) * random();
                        pn.emitterTimerAccum = freq0;
                    }
                    pn.emitterTimerAccum -= dt;
                    while (pn.emitterTimerAccum <= 0.0) {
                        double freq0 = 60.0 / prtFmin;
                        double freq1 = 60.0 / prtF;
                        if (freq0 != freq1)
                            freq0 = freq0 + (freq1 - freq0) * random();
                        pn.emitterTimerAccum += freq0;
                        ++emitCount;
                    }
                    // Frequency mode alone caps the retained timer to the
                    // smaller of its current value and 60/prtFmin.
                    if (prtFmin > 0.0) {
                        double maxTimer = 60.0 / prtFmin;
                        if (maxTimer > pn.emitterTimerAccum)
                            maxTimer = pn.emitterTimerAccum;
                        pn.emitterTimerAccum = maxTimer;
                        if (emitCount <= 0) goto physics_step;
                    }
                } else if (triggerType == 1) {
                    // Count mode samples whenever the complete node flags byte
                    // is nonzero, even when both endpoints compare equal.
                    if (pn.flags) {
                        double r = random();
                        emitCount = particleEmitCountFromDouble_guess(
                            prtFmin + (prtF - prtFmin) * r);
                    }
                    if (emitCount <= 0) goto physics_step;
                }
            }

            // ====== BLOCK 3: particle creation ======
            // At most one child is constructed for this node in this pass.
            // emitCount controls only the later worker call; excess count does
            // not cause an inner multi-spawn loop.
            if (emitCount > 0) {
                // Retain the raw source-list dispatch independently across its
                // count and numeric getter. Re-entrant replacement of the node
                // Variant must not switch or destroy the receiver mid-spawn.
                detail::ScopedVariantObjectDispatch_guess particleSources(
                    pn.particleMotionListVariant);
                auto *const sourceList = particleSources.get();
                const int sourceCount = static_cast<int>(
                    detail::motionPropGetCount(sourceList));

                // All four references retain this native bug: a zero source
                // count drains positive emitCount, steps existing children,
                // then branches back to the decrement/step block forever.
                if (sourceCount == 0) {
                    for(;;) {
                        do {
                            --emitCount;
                        } while(emitCount > 0);
                        stepParticleChildren_guess(pn);
                    }
                }

                {

                // Selection has no index clamp. After direct Variant-to-ttstr
                // conversion, the shared splitter preserves every empty piece;
                // native then reads pieces 1 and 2 with no size check and
                // ignores piece 0.
                const int sourceIndex = particleSourceIndexFromDouble_guess(
                    random() * static_cast<double>(sourceCount));
                const ttstr selectedSrc =
                    detail::motionPropGetStringByNum(
                        sourceList, sourceIndex);
                const auto sourceParts = detail::splitTtstr_guess(
                    selectedSrc, TJS_W('/'));
                const ttstr particleChara(sourceParts[1]);
                const ttstr motionPath(sourceParts[2]);

                // Create the child from the parent's retained RM dispatch. All
                // four references install the canonical-root and immediate-
                // parent links after construction but before adaptor creation;
                // the adaptor Variant is then appended to the particle Array.
                using PlayerAdaptor = ncbInstanceAdaptor<Player>;
                auto *childRaw = new Player(getResourceManager());
                childRaw->_rootPlayer = _rootPlayer;
                childRaw->_parentPlayer = this;
                iTJSDispatch2 *childDisp = PlayerAdaptor::CreateAdaptor(childRaw);
                tTJSVariant childVar;
                if(childDisp) {
                    childVar = tTJSVariant(childDisp, childDisp);
                    childDisp->Release();
                }
                auto *child = childRaw;  // native pointer for subsequent use
                // A null non-throwing adaptor result leaves childVar void but
                // does not delete the native child or skip initialization. If
                // CreateNew succeeds while GetAdaptor fails, ncbind can instead
                // return a non-null empty shell: childVar becomes Object, its
                // native slot stays null, and childRaw still leaks. The caller
                // tests only the dispatch, so it does not distinguish that
                // malformed object form from a successfully attached child.
                // Render-native color-weight propagation precedes context,
                // zFactor, chara and play. Parent and child share the same
                // packed representation; the old port duplicated this field.
                {
                    uint32_t packed;
                    std::memcpy(&packed, &pn.colorBytes[0], sizeof(uint32_t));
                    child->_colorWeightPacked = packed;
                }
                // Give the child an independently owning copy of the current
                // matched-module context before its first motion lookup.
                child->_findMotionContextVariant = _findMotionContextVariant;
                child->setZFactor(_zFactor);
                child->setChara(particleChara);
                child->playMotion_guess(0, motionPath);
                // The new child flushes only its own pending stealth character
                // after the primary character write; it does not copy either
                // live stealth slot from the parent. The subsequent play entry
                // also flushes the child's pending stealth-motion request
                // through the shared play state machine.

                // Propagate the particle node's evaluated opacity into the
                // child root delta block. A changed value dirties that root so
                // its next update copies the complete delta transform block.
                {
                    auto &cr = child->_nodes[0];
                    internal::propagateParticleEvaluatedOpacityToChildRoot_guess(
                        pn, cr);
                }

                // Position distribution is selected by the particle subtype,
                // while direction/decay below uses particleFlyDirection.
                double offX = 0, offY = 0, offZ = 0;
                const int flyDir = pn.particleType;
                const bool has3D = pn.particleTriVolume;

                if (flyDir == 2) {
                    // Uniform box. RNG order is X, Y, then optional Z.
                    double r1 = random();
                    offY = random() * 32.0 - 16.0;
                    offX = r1 * 32.0 - 16.0;
                    if (has3D) offZ = random() * 32.0 - 16.0;
                } else if (flyDir == 1) {
                    if (has3D) {
                        double r1 = random(), r2 = random(), r3 = random();
                        double phi = r2 * 2.0 * PI;
                        double theta = r1 * 2.0 * PI;
                        // Native calls pow with the single-precision 1/3
                        // constant promoted to double; it does not call cbrt.
                        double radius = std::pow(
                            r3 + 0.0,
                            static_cast<double>(1.0f / 3.0f)) * 16.0;
                        double cosPhi = std::cos(phi);
                        offX = cosPhi * (radius * std::cos(theta));
                        offY = radius * (cosPhi * std::sin(theta));
                        offZ = radius * std::sin(phi);
                    } else {
                        // Disk RNG order is angle, then area-uniform radius.
                        double angle2d = random() * 2.0 * PI;
                        double radius = std::sqrt(random()) * 16.0;
                        offX = std::cos(angle2d) * radius;
                        offY = radius * std::sin(angle2d);
                    }
                } else {
                    offX = 0.0;
                    offY = 0.0;
                }

                // A nonzero Z component scales by sqrt(det(matrix)) without an
                // absolute value or a determinant guard.
                if (offZ != 0.0) {
                    const double det = pn.accumulated.m11 * pn.accumulated.m22
                                     - pn.accumulated.m12 * pn.accumulated.m21;
                    offZ *= std::sqrt(det);
                }

                // Transform the sampled XY offset around ox/oy read directly
                // from the selected slot. There is no separately propagated
                // node-level clip-origin cache in any current reference.
                const double m11 = pn.accumulated.m11, m21 = pn.accumulated.m21;
                const double m12 = pn.accumulated.m12, m22 = pn.accumulated.m22;
                const double clipOX = particleSlot.ox;
                const double clipOY = particleSlot.oy;
                const double txOff = m11 * (offX - clipOX) + m12 * (offY - clipOY);
                const double tyOff = m21 * (offX - clipOX) + m22 * (offY - clipOY);

                // Speed interval uses evaluator outputs [2]/[3] and samples
                // only when its endpoints compare unequal.
                double speed = pn.particleInterp[2];
                if (speed != pn.particleInterp[3])
                    speed = speed + (pn.particleInterp[3] - speed) * random();

                // Direction and optional distance-fitting decay are selected by
                // particleFlyDirection, not particleInheritVelocity.
                double direction = 0.0;
                const int inhVel = pn.particleFlyDirection;

                if (inhVel == 2) {
                    // Distance-fitting decay deliberately has no duration,
                    // sign, zero-denominator or finite-value guards.
                    double dist = std::sqrt(txOff * txOff + tyOff * tyOff + offZ * offZ);
                    double dirAngle = std::atan2(tyOff, txOff) * 360.0;
                    double decay = pn.particleAccelRatio;
                    double childTotalTime = child->_cachedTotalFrames;
                    double dtNorm = childTotalTime / 60.0;
                    if (decay == 1.0) {
                        speed = dist / dtNorm;
                    } else {
                        speed = dist * std::log(decay) / (std::pow(decay, dtNorm) - 1.0);
                    }
                    direction = dirAngle / (2.0 * PI) + 180.0;
                    direction = direction * PI / 180.0; // convert to radians
                    speed /= 60.0;
                } else if (inhVel == 1) {
                    direction = std::atan2(tyOff, txOff) * 360.0 / (2.0 * PI) + 180.0;
                    direction = direction * PI / 180.0;
                } else {
                    direction = std::atan2(pn.accumulated.m12, pn.accumulated.m11) * 360.0 / (2.0 * PI);
                    direction = direction * PI / 180.0;
                }

                // Symmetric direction spread uses evaluator output [8]. Signed
                // zero compares equal to its negation and consumes no RNG.
                double range = pn.particleInterp[8];
                double spreadRandom = -range;
                if (range != -range) spreadRandom = (range + range) * random() - range;
                double totalAngle = direction + spreadRandom * PI / 180.0;
                double dirRad = totalAngle;

                // Project 3D direction length into XY for fly modes 1 and 2.
                double zoomScale = 1.0;
                if (inhVel >= 1 && inhVel <= 2) {
                    if (txOff != 0.0 || tyOff != 0.0) {
                        if (offZ != 0.0) {
                            double xyLen = std::sqrt(txOff * txOff + tyOff * tyOff);
                            zoomScale = xyLen / std::sqrt(offZ * offZ + xyLen * xyLen);
                        }
                    }
                }

                // Position/velocity axes are selected by coordinateMode.
                double velX = 0.0, velY = 0.0, velZ = 0.0;

                {
                    auto &cr = child->_nodes[0];
                    if (pn.coordinateMode == 1) {
                        cr.accumulated.posX = txOff + pn.accumulated.posX;
                        cr.accumulated.posY = offZ + pn.accumulated.posY;
                        cr.accumulated.posZ = tyOff + pn.accumulated.posZ;
                        velX = zoomScale * speed * std::cos(dirRad);
                        velY = speed * 0.0;
                        velZ = zoomScale * speed * std::sin(dirRad);
                    } else if (pn.coordinateMode == 0) {
                        cr.accumulated.posX = txOff + pn.accumulated.posX;
                        cr.accumulated.posY = tyOff + pn.accumulated.posY;
                        cr.accumulated.posZ = offZ + pn.accumulated.posZ;
                        velX = zoomScale * speed * std::cos(dirRad);
                        velY = zoomScale * speed * std::sin(dirRad);
                        velZ = speed * 0.0;
                    }

                    // Flip and ordinary root transform writes dirty only on a
                    // changed value.
                    if (cr.accumulated.flipX != pn.accumulated.flipX ||
                        cr.accumulated.flipY != pn.accumulated.flipY) {
                        cr.accumulated.flipX = pn.accumulated.flipX;
                        cr.accumulated.flipY = pn.accumulated.flipY;
                        cr.accumulated.dirty = true;
                    }

                    // Particle-angle sampling precedes zoom sampling, which is
                    // observable in the shared RNG sequence.
                    double aMin = pn.particleInterp[4];
                    double aMax = pn.particleInterp[5];
                    double prtAngle = aMin;
                    if (aMin != aMax) prtAngle = aMin + (aMax - aMin) * random();
                    // Parent flip parity controls the sampled-angle sign.
                    double childAngle = -prtAngle;
                    if (pn.accumulated.flipX == pn.accumulated.flipY) childAngle = prtAngle;

                    if (pn.particleInheritAngle) {
                        double inheritedDirectionRad = dirRad + PI;
                        if (!pn.accumulated.flipX) {
                            inheritedDirectionRad = dirRad;
                        }
                        childAngle +=
                            inheritedDirectionRad * 360.0 / (2.0 * PI);
                    }
                    while (childAngle < 0.0) childAngle += 360.0;
                    while (childAngle >= 360.0) childAngle -= 360.0;

                    if (child->_directEdit) {
                        double k = childAngle;
                        while (k < 0.0) k += 360.0;
                        while (k >= 360.0) k -= 360.0;
                        child->_emoteAngle = k;
                        child->initEmoteMotion_guess(2u);
                    } else {
                        if (cr.accumulated.angle != childAngle) {
                            cr.accumulated.dirty = true;
                            cr.accumulated.angle = childAngle;
                        }
                    }
                    auto *postAngleRoot = &child->_nodes[0];

                    double zoom = pn.particleInterp[6];
                    if (zoom != pn.particleInterp[7])
                        zoom = zoom + (pn.particleInterp[7] - zoom) * random();
                    if(postAngleRoot->accumulated.scaleX != zoom ||
                       postAngleRoot->accumulated.scaleY != zoom) {
                        postAngleRoot->accumulated.dirty = true;
                        postAngleRoot->accumulated.scaleX = zoom;
                        postAngleRoot->accumulated.scaleY = zoom;
                    }

                    // Distance-fitted fly mode 2 bypasses this later zoom
                    // adjustment. Mode 2 divides unconditionally, including
                    // zero, negative, infinite and NaN zoom values.
                    if (pn.particleFlyDirection != 2) {
                        if (pn.particleApplyZoomToVelocity == 1) {
                            velX *= zoom; velY *= zoom; velZ *= zoom;
                        } else if (pn.particleApplyZoomToVelocity == 2) {
                            velX /= zoom; velY /= zoom; velZ /= zoom;
                        }
                    }
                }

                child->_cameraVelocityX = velX;
                child->_cameraVelocityY = velY;
                child->_cameraVelocityZ = velZ;

                // Translation-velocity inheritance is an independent mode and
                // tests delta time for inequality with zero, not positivity.
                if (pn.particleInheritVelocity == 1 && dt != 0.0) {
                    child->_cameraVelocityX += pn.deltaPosX / dt;
                    child->_cameraVelocityY += pn.deltaPosY / dt;
                    child->_cameraVelocityZ += pn.deltaPosZ / dt;
                }

                // The same acceleration-ratio field becomes child damping.
                child->_cameraDamping = pn.particleAccelRatio;

                detail::particleArrayAdd_guess(array, childVar);

                // Signed count > maxNum erases index zero once. A zero maximum
                // therefore removes the just-retained nonempty child once.
                if(detail::particleArrayCount_guess(array) >
                   pn.particleMaxNum) {
                    detail::particleArrayErase_guess(array, 0);
                }

                // Keep the independently retained source-list receiver and all
                // spawn temporaries alive through the worker. Excess emitCount
                // skips only that worker for this node/frame.
                if (emitCount <= 1) {
                    stepParticleChildren_guess(pn);
                }
                continue;
                } // end creation block
            }
            } // end outer emitCount scope

        physics_step:
            stepParticleChildren_guess(pn);
        } // for each nodeType==4
    }


} // namespace motion
