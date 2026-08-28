// PlayerUpdateChildMotion.cpp — updateLayers motion sub-node phase
// Split from PlayerUpdateLayers.cpp for maintainability.
//
#include "PlayerUpdateLayersInternal.h"
#include "MotionDispatch.h"

namespace motion {
    // Prepend the child's pending callbacks to the parent's same event queue,
    // then destroy the child records without releasing vector capacity.
    void Player::aggregateChildPendingEvents_guess(Player &child) {
        detail::prependAndClearChildPendingEvents_guess(
            _pendingEvents, child._pendingEvents);
    }

    void Player::updateLayersPhase3_MotionSubNode() {
        auto &nodes = _nodes;
        if (_preview) return;

        for (size_t i = 1; i < nodes.size(); ++i) {
            auto &mn = nodes[i];
            if (mn.nodeType != 3) continue;

            // Replay mode prefers the node-bound parameter, then falls back to
            // the Player-level selected parameter. This fallback is unique to
            // the mode gate; later parent-time reads use only the node pointer.
            auto *modeParameterEntry = mn.parameterEntry;
            if(modeParameterEntry == nullptr) {
                modeParameterEntry = _selectedParameterEntry;
            }
            const int parameterMode =
                modeParameterEntry ? modeParameterEntry->mode : 0;

            // The native implementation resolves the retained child once and
            // immediately addresses root zero. It has no null/root-size guard.
            {
            Player &child = *mn.getChildPlayer();
            auto &childRoot = child._nodes[0];

            if (!parameterMode && !mn.accumulated.dirty) {
                goto label_18;
            }

            {
            const ttstr &src = mn.activeSlot().srcValue;
            // A completed slot and a null ttstr owner share the teardown path.
            // Teardown skips the shared frameProgress/updateLayers exit.
            if (motionSubNodeNeedsTeardown_guess(
                    mn.activeSlot().done, src)) {
                child._allplaying = false;
                if (true) {
                    child._variableLabelScopes.clear();
                    child.resetAndReleaseOldNodeTree_guess();
                }
                child._stealthMotion.Clear();
                child._motionKey.Clear();
                continue;
            }

            {
                    // The gate consumes the complete flags byte, then replaces
                    // it with 1; it is not a bitwise update.
                    if (motionSubNodeConsumeReplayFlag_guess(
                            parameterMode, mn.flags)) {

                        // The active-slot index is read once and remains unchanged
                        // throughout this path. Slot flipping belongs to the clip
                        // evaluation pipeline.

                        // Split src by "/" and dispatch the child motion.
                        // - 1 segment: setChara(segment[0]), then play the slot icon
                        // - otherwise: setChara(segment[1]), then Player_play(segment[2])
                        //
                        // This is important for paths like
                        // "motion/m2cheeseware_logo/icon25": the native code
                        // ignores the first "motion" prefix and uses
                        // chara="m2cheeseware_logo", motion="icon25".
                        const std::vector<ttstr> segments =
                            detail::splitTtstr_guess(
                                src, static_cast<tjs_char>('/'));

                        if (segments.size() == 1) {
                                // The one-element branch writes chara from src but
                                // plays the icon stored in the active slot.
                                child.setChara(src);
                                child.playMotion_guess(
                                    mn.activeSlot().motionFlags |
                                        parameterMode,
                                    mn.activeSlot().iconValue);
                        } else {
                                // The multi-element branch indexes [1] and [2]
                                // directly; there is no two-element fallback.
                                child.setChara(segments[1]);
                                child.playMotion_guess(
                                    mn.activeSlot().motionFlags |
                                        parameterMode,
                                    segments[2]);
                        }
                        // setChara() also flushes the pending stealth character;
                        // that field is not a second motion request.

                        // Synchronize child time only while the child is both
                        // playing and queued.
                        if (child._allplaying && child._queuing) {
                            // childTime = parent tick - clip start + motion offset.
                            double childTime = _frameTickCount
                                - mn.activeSlot().clipStartTime
                                + mn.activeSlot().motionTimeOffset;
                            // Direction follows the speed-scaled delta, not the raw
                            // frame delta.
                            if (_deltaTime < 0.0) {
                                // Backward play: handle loop wrapping
                                double loopEnd = child._loopTime;
                                if (loopEnd >= 0.0) {
                                    double totalFrames = child._cachedTotalFrames;
                                    while (childTime >= totalFrames)
                                        childTime = childTime - totalFrames + loopEnd;
                                }
                            }
                            double totalFrames = child._cachedTotalFrames;
                            childTime =
                                motionSubNodeClampTimeAtZero_guess(childTime);
                            child._frameTickCount = childTime;
                            if (childTime > totalFrames) childTime = totalFrames;
                            child._clampedEvalTime = childTime;
                            // The two adjacent state bytes are published together;
                            // this does not iterate child timelines.
                            child._queuing = true;
                            child._firstFrame = true;
                            // A parent that is not queued marks the child for a
                            // reverse seek.
                            if (!_queuing) {
                                child._reverseSeekFlag = true;
                            }
                        }
                    }

                // Retain the decompiler-visible dead branch token.
                if (!true) goto label_18;

                // === Angle interpolation ===
                int angleMode = mn.activeSlot().motionDt;
                bool hasAngle = false;
                double computedAngle = 0.0;
                const double dofst = mn.activeSlot().motionDofst;

                // During a dual-slot crossfade, blend the old and new angle
                // offsets using the parent time.
                double blendedAngleOffset = dofst;
                if (mn.activeSlot().motionDocmpl
                    && mn.activeSlot().crossfading
                    && !mn.otherSlot().done
                    && mn.otherSlot().motionDt != 0) {
                    // Prefer the node parameter time and fall back to the
                    // player's clamped evaluation time.
                    const double otherDofst = mn.otherSlot().motionDofst;
                    if (dofst != otherDofst) {
                        const auto *parameterEntry = mn.parameterEntry;
                        const double parentTime = parameterEntry
                            ? parameterEntry->value : _clampedEvalTime;
                        blendedAngleOffset = motionSubNodeBlendAngleOffset_guess(
                            dofst, otherDofst, parentTime,
                            mn.activeSlot().clipStartTime,
                            mn.otherSlot().clipStartTime,
                            mn.activeSlot().accVariant);
                    }
                }

                if (angleMode != 0) {
                    // Mode 2 falls through to mode 3 while the child has no
                    // previous-frame delta position.
                    int effectiveMode = angleMode;
                    if (angleMode == 2 && child._noUpdateYet) {
                        effectiveMode = 3;
                    }

                    switch (effectiveMode) {
                    case 1: // Direct angle
                        // Direct angles are not normalized to [0, 360).
                        computedAngle = dofst + mn.accumulated.angle;
                        hasAngle = true;
                        break;
                    case 2: { // atan2 from delta position
                        // Use the potentially interpolated offset, not raw dofst.
                        double dy_comp, dx_comp;
                        if (mn.coordinateMode == 1) {
                            dy_comp = mn.deltaPosZ;
                            dx_comp = mn.deltaPosX;
                        } else if (mn.coordinateMode == 0) {
                            dy_comp = mn.deltaPosY;
                            dx_comp = mn.deltaPosX;
                        } else {
                            // Unsupported coordinate modes preserve the default
                            // angle value but still mark the result present.
                            hasAngle = true;
                            break;
                        }
                        computedAngle = blendedAngleOffset +
                            std::atan2(dy_comp, dx_comp) * 360.0 / 6.28318531;
                        hasAngle = true;
                        break;
                    }
                    case 3: { // Interpolated atan2
                        // This mode requires an unfinished opposite slot. Sample
                        // the interpolated position at t and t+0.0001, then use
                        // the finite-difference direction for atan2.
                        if (!mn.activeSlot().crossfading
                            || mn.otherSlot().done) {
                            break;
                        }
                        // Prefer node parameter time; fall back to the player's
                        // clamped evaluation time.
                        const auto *parameterEntry = mn.parameterEntry;
                        double parentTime = parameterEntry
                            ? parameterEntry->value : _clampedEvalTime;
                        double currentStart = mn.activeSlot().clipStartTime;
                        double otherStart = mn.otherSlot().clipStartTime;
                        double denom = otherStart - currentStart;
                        // Preserve the direct division, including IEEE edge cases.
                        double ratio = (parentTime - currentStart) / denom;
                        const auto sampleTimes =
                            positionDerivativeSampleTimes_guess(ratio);
                        // Interpolate from the current evaluated position toward
                        // the position retained in the opposite slot.
                        const auto &slot = mn.activeSlot();
                        double src[3] = {slot.x, slot.y, mn.activeSlot().z};
                        double dst[3] = {mn.otherSlot().x, mn.otherSlot().y, mn.otherSlot().z};
                        double out1[3] = {}, out2[3] = {};
                        evaluatePositionInterpolation_guess(
                            slot.cccVariant, dst, src, out1,
                            mn.coordinateMode, slot.cpVariant,
                            sampleTimes.first);
                        evaluatePositionInterpolation_guess(
                            slot.cccVariant, dst, src, out2,
                            mn.coordinateMode, slot.cpVariant,
                            sampleTimes.second);
                        // Select the derivative plane from coordinateMode.
                        double dx_comp, dy_comp;
                        if (mn.coordinateMode == 1) {
                            dx_comp = out2[0] - out1[0]; dy_comp = out2[2] - out1[2];
                        } else if (mn.coordinateMode == 0) {
                            dx_comp = out2[0] - out1[0]; dy_comp = out2[1] - out1[1];
                        } else {
                            hasAngle = true;
                            break;
                        }
                        computedAngle = blendedAngleOffset +
                            std::atan2(dy_comp, dx_comp) * 360.0 / 6.28318531;
                        hasAngle = true;
                        break;
                    }
                    case 4: { // Target node lookup
                        // A missing label leaves the angle absent.
                        const ttstr &dtgt = mn.activeSlot().motionDtgtValue;
                        const auto *target =
                            findNodeByRawLabel_guess(dtgt, false);
                        if(target == nullptr) break;
                        double dy_comp, dx_comp;
                        if (mn.coordinateMode == 1) {
                            dy_comp = target->accumulated.posZ - mn.accumulated.posZ;
                            dx_comp = target->accumulated.posX - mn.accumulated.posX;
                        } else if (mn.coordinateMode == 0) {
                            dy_comp = target->accumulated.posY - mn.accumulated.posY;
                            dx_comp = target->accumulated.posX - mn.accumulated.posX;
                        } else {
                            hasAngle = true;
                            break;
                        }
                        computedAngle = blendedAngleOffset +
                            std::atan2(dy_comp, dx_comp) * 360.0 / 6.28318531;
                        hasAngle = true;
                        break;
                    }
                    default: break;
                    }
                    // Modes 2, 3, and 4 normalize; mode 1 does not.
                    if (effectiveMode != 1) {
                        while (computedAngle < 0.0) computedAngle += 360.0;
                        while (computedAngle >= 360.0) computedAngle -= 360.0;
                    }
                }

                // === Origin offset ===
                double posX = mn.accumulated.posX;
                double posY = mn.accumulated.posY;
                double posZ = mn.accumulated.posZ;

                motionSubNodeApplyOriginOffset_guess(
                    mn.coordinateMode,
                    mn.activeSlot().ox, mn.activeSlot().oy,
                    mn.matrix.m11, mn.matrix.m12,
                    mn.matrix.m21, mn.matrix.m22,
                    posX, posY, posZ);

                childRoot.delta.posX = posX;
                childRoot.delta.posY = posY;
                childRoot.delta.posZ = posZ;
                child.setFlip(mn.accumulated.flipX, mn.accumulated.flipY);
                child.setZoom(mn.accumulated.scaleX, mn.accumulated.scaleY);

                child._cameraAngle = _cameraAngle;
                if(child._directEdit) {
                    child.initEmoteMotion_guess(2u);
                }
                if (hasAngle) {
                    child.setAngleDeg(computedAngle);
                }

                child.setSlant(mn.accumulated.slantX,
                               mn.accumulated.slantY);
                child.setOpacity(mn.accumulated.opacity);
                // The parent node's accumulated active state drives the child
                // root's visibility override. Neither accumulated visibility
                // nor the child active override is copied here.
                child.setVisible(mn.accumulated.active);

                uint32_t packed;
                std::memcpy(&packed, &mn.colorBytes[0], sizeof(uint32_t));
                child._colorWeightPacked = packed;

                if (hasAngle || computedAngle == mn.accumulated.angle ||
                    child._directEdit) {
                    childRoot.matrix.m11 = mn.matrix.m11;
                    childRoot.matrix.m12 = mn.matrix.m12;
                    childRoot.matrix.m21 = mn.matrix.m21;
                    childRoot.matrix.m22 = mn.matrix.m22;
                } else {
                    const double angleDifference =
                        computedAngle - mn.accumulated.angle;
                    double delta =
                        (angleDifference * 3.14159265 +
                         angleDifference * 3.14159265) / 360.0;
                    if (mn.accumulated.flipX != mn.accumulated.flipY)
                        delta = -delta;
                    childRoot.matrix.m11 =
                        std::cos(delta) * mn.matrix.m11 +
                        std::sin(delta) * mn.matrix.m12;
                    childRoot.matrix.m12 =
                        std::cos(delta) * mn.matrix.m12 -
                        mn.matrix.m11 * std::sin(delta);
                    childRoot.matrix.m21 =
                        std::cos(delta) * mn.matrix.m21 +
                        std::sin(delta) * mn.matrix.m22;
                    childRoot.matrix.m22 =
                        std::cos(delta) * mn.matrix.m22 -
                        mn.matrix.m21 * std::sin(delta);
                }
                childRoot.delta.dirty = true;

            }
            }

        label_18:
                childRoot.clipAABB = mn.clipAABB;
                if (mn.meshInheritanceSeparator_guess) {
                    childRoot.meshAncestor = &mn;
                } else {
                    childRoot.meshAncestor = mn.meshAncestor;
                }
                childRoot.visibleAncestor = mn.visibleAncestor;

                child.frameProgress(_deltaTime);
                child.updateLayers();
                aggregateChildPendingEvents_guess(child);
            }
        }

    }


} // namespace motion
