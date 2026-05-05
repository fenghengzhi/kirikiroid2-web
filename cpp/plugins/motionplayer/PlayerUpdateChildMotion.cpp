// PlayerUpdateChildMotion.cpp — updateLayers motion sub-node phase
// Split from PlayerUpdateLayers.cpp for maintainability.
//
#include "PlayerUpdateLayersInternal.h"

namespace motion {
    void Player::updateLayersPhase3_MotionSubNode(double currentTime) {
        auto &nodes = _runtime->nodes;
        const auto motionPath =
            _runtime->activeMotion ? _runtime->activeMotion->path : std::string{};
        // Motion sub-node processing — aligned to sub_6BE0C0 (0x6BE0C0).
        // For each nodeType=3 (Motion) node, create/manage child Player instance.
        // Only runs when !isEmoteMode (0x6BE104).
        if (_runtime->isEmoteMode) return;

        for (size_t i = 1; i < nodes.size(); ++i) {
            auto &mn = nodes[i];
            if (mn.nodeType != 3) continue;

            // Aligned to libkrkr2.so sub_6BE0C0 (0x6BE204..0x6BE214):
            // v12 is parameterEntry->mode (entry+48), using the node entry
            // or the Player_initNonEmoteMotion default entry as fallback.
            auto *parameterEntry = resolveNodeParameterEntry(*_runtime, mn);
            int v12 = parameterEntry ? parameterEntry->mode : 0;

            // Get child Player via TJS dispatch (0x6BE220..0x6BE260)
            // Aligned to binary: node+1912 → NativeInstanceSupport → native Player*
            // Child Player is pre-created in buildNodeTree (sub_6B3C78 case 3).
            {
            Player *childPtr = mn.getChildPlayer();
            if (!childPtr) {
                goto label_18;
            }
            Player &child = *childPtr;

            // If no v12 flags and not dirty -> skip to LABEL_18 (0x6BE270).
            // sub_6BE0C0 tests node+1504 here; visible is node+1506.
            if (!v12 && !mn.accumulated.dirty) {
                goto label_18;
            }

            // Check slotDone → clear child (0x6BE31C..0x6BE354)
            // Binary: calls cleanup (sub_6C0DE8, sub_6B56F8), releases TJS variants,
            // then goes to LABEL_3 (next loop iteration), SKIPPING frameProgress/updateLayers.
            if (mn.activeSlot().done) {
                // Binary cleanup at 0x6BE328..0x6BE354:
                // 1. child._allplaying = false (player+1099)
                // 2. sub_6C0DE8(child+1296) — resets timeline keyframe cache
                // 3. sub_6B56F8(child) — releases layer IDs for all non-root nodes,
                //    clears nodes (except root), resets label map
                // 4. Release TJS variants at child+984 and child+976
                child._allplaying = false;
                if (child._runtime) {
                    // sub_6C0DE8: reset timeline keyframe cache
                    child._runtime->timelines.clear();
                    // sub_6B56F8: release layer IDs for non-root nodes, keep
                    // the constructor-created root, and clear the label map.
                    child.resetNodeTreeForBuildLike_0x6B56F8();
                }
                continue;  // skip to next iteration — binary goes to LABEL_3, not LABEL_18
            }

            {
                // Get motion source from clip slot (0x6BE364)
                const auto &src = mn.activeSlot().src;
                if (!src.empty()) {
                    // Re-init gate: (v12 & 5) != 0 || mn.flags (0x6BE37C)
                    if ((v12 & 5) != 0 || (mn.flags & 0x01)) {
                        mn.flags |= 0x01; // mark as initialized (0x6BE388)

                        // Binary does NOT flip activeSlotIndex here (0x6BE21C reads it
                        // once and uses it unchanged throughout). Slot flip is managed
                        // elsewhere in the clip evaluation pipeline.

                        // Resolve motion and play (0x6BE3B4..0x6BE46C)
                        // Aligned to libkrkr2.so sub_6BE0C0 + sub_697D34:
                        // split src by "/" into segments.
                        // - 1 segment: setChara(segment[0]), then Player_play(raw src)
                        // - otherwise: setChara(segment[1]), then Player_play(segment[2])
                        //
                        // This is important for paths like
                        // "motion/m2cheeseware_logo/icon25": the native code
                        // ignores the first "motion" prefix and uses
                        // chara="m2cheeseware_logo", motion="icon25".
                        {
                            std::vector<std::string> segments;
                            size_t start = 0;
                            while (start <= src.size()) {
                                const size_t slashPos = src.find('/', start);
                                if (slashPos == std::string::npos) {
                                    segments.push_back(src.substr(start));
                                    break;
                                }
                                segments.push_back(src.substr(start, slashPos - start));
                                start = slashPos + 1;
                            }

                            if (segments.size() <= 1) {
                                // Single segment: binary sets chara to src itself
                                // then Player_play with raw src (no "/" prefix)
                                child.setChara(detail::widen(src));
                                child.onFindMotion(detail::widen(src),
                                                   mn.activeSlot().motionFlags | v12);
                            } else if (segments.size() >= 3) {
                                child.setChara(detail::widen(segments[1]));
                                child.onFindMotion(detail::widen(segments[2]),
                                                   mn.activeSlot().motionFlags | v12);
                            } else {
                                // Defensive fallback for unexpected 2-segment paths.
                                child.setChara(detail::widen(segments[0]));
                                child.onFindMotion(detail::widen(segments[1]),
                                                   mn.activeSlot().motionFlags | v12);
                            }
                        }
                        // Stealth motion (0x6BE41C..0x6BE44C): binary reads from
                        // CHILD player+776, plays with flag 16, then clears child+776.
                        if (!child._stealthMotion.IsEmpty()) {
                            child.onFindMotion(child._stealthMotion, PlayFlagStealth);
                            child._stealthMotion.Clear();
                        }

                        if(detail::logoSnapshotMarkEnabledForPath(motionPath) &&
                           motionPath.find("m2logo.mtn") != std::string::npos &&
                           currentTime >= 0.0 && currentTime <= 50.0) {
                            std::fprintf(
                                stderr,
                                "SNAPPLAY frame=%.3f nodeIndex=%d src=%s childMotionKey=%s childActiveMotion=%s childNodesBuilt=%d childPlaying=%d\n",
                                currentTime,
                                mn.index,
                                src.c_str(),
                                detail::narrow(child.getMotion()).c_str(),
                                child._runtime && child._runtime->activeMotion
                                    ? child._runtime->activeMotion->path.c_str()
                                    : "<none>",
                                child._runtime && child._runtime->nodes.size() > 1 ? 1 : 0,
                                child._allplaying ? 1 : 0);
                        }


                        // Time sync from parent loop time (0x6BE478..0x6BE4E8)
                        // Binary checks both _allplaying && _queuing (0x6BE478)
                        if (child._allplaying && child._queuing) {
                            // Binary at 0x6BE49C: childTime = player+1120 - slot+8 + slot+376
                            // = _frameLoopTime - clipStartTime + motionTimeOffset
                            double childTime = _frameLoopTime
                                - mn.activeSlot().clipStartTime
                                + mn.activeSlot().motionTimeOffset;
                            if (_frameLastTime < 0.0) {
                                // Backward play: handle loop wrapping
                                // Binary reads child+1136 (_loopTime) and child+1128 (_cachedTotalFrames)
                                double loopEnd = child._loopTime;
                                if (loopEnd >= 0.0) {
                                    double totalFrames = child._cachedTotalFrames;
                                    while (childTime >= totalFrames)
                                        childTime = childTime - totalFrames + loopEnd;
                                }
                            }
                            // Binary reads player+1128 directly (0x6BE4CC)
                            double totalFrames = child._cachedTotalFrames;
                            childTime = std::max(childTime, 0.0);
                            // Binary: writes unclamped time to player+1120 (0x6BE4D4)
                            child._frameLoopTime = childTime;
                            if (childTime > totalFrames) childTime = totalFrames;
                            // Binary: writes clamped time to player+456 (0x6BE4E4)
                            child._clampedEvalTime = childTime;
                            // Binary at 0x6BE4E8: writes word 0x0101 to child+480,
                            // setting both _queuing (byte+480) and _allplaying (byte+481)
                            // simultaneously. Does NOT iterate timelines.
                            child._allplaying = true;
                            child._queuing = true;
                            // Binary: if (!*(byte*)(v4 + 480)) — checks _queuing (0x6BE4EC)
                            if (!_queuing) {
                                child._needsInternalAssignImages = true;
                            }
                        }
                    }
                }

                // Binary at 0x6BE534 unconditionally proceeds to angle/state
                // propagation (no activeMotion guard). Only guard for null runtime.
                if (!child._runtime) goto label_18;

                // === Angle interpolation (0x6BE534..0x6BEC9C) ===
                int angleMode = mn.activeSlot().motionDt;
                bool hasAngle = false;
                double computedAngle = 0.0;
                const double dofst = mn.activeSlot().motionDofst;

                // Dual-slot crossfade angle interpolation (0x6BE85C..0x6BEC9C)
                // When crossfading between two clip slots, blend dofst (v37) between
                // old and new slot values using time-based ratio.
                double v37 = dofst;
                if (mn.activeSlot().motionDocmpl
                    && mn.activeSlot().crossfading
                    && !mn.otherSlot().done
                    && mn.otherSlot().motionDt != 0) {
                    // Binary at 0x6BE864 reads node+8+40: parameterEntry->value.
                    // Falls back to player+456 (_clampedEvalTime) if node+8 is null.
                    double parentTime =
                        parameterEntry ? parameterEntry->value : _clampedEvalTime;
                    double currentStart = mn.activeSlot().clipStartTime;
                    double otherStart = mn.otherSlot().clipStartTime;
                    double denom = otherStart - currentStart;
                    // Binary divides directly without denom guard (0x6BEC6C)
                    double ratio = (parentTime - currentStart) / denom;
                    // Binary at 0x6BEC74: only checks hasEasing (slot+544).
                    if (mn.activeSlot().hasEasing) {
                        ratio = evaluateBezierCurve(mn.activeSlot().acc, ratio);
                    }
                    // Binary does NOT clamp ratio to [0,1] (0x6BEC9C).
                    double otherDofst = mn.otherSlot().motionDofst;
                    // Wrap angle difference > 180 degrees for shortest-path interpolation
                    if (dofst >= otherDofst) {
                        if (dofst - otherDofst > 180.0) otherDofst += 360.0;
                    } else {
                        if (otherDofst - dofst > 180.0) otherDofst -= 360.0;
                    }
                    v37 = otherDofst * ratio + dofst * (1.0 - ratio);
                    // Normalize to [0, 360)
                    if (v37 < 0.0) v37 += 360.0;
                    if (v37 >= 360.0) v37 -= 360.0;
                }

                if (angleMode != 0) {
                    // Case 2→3 fallthrough: binary at 0x6BE664 checks child player+608
                    // (_noUpdateYet). If set, case 2 falls through to LABEL_83 (case 3
                    // logic) because on the first frame there's no delta position yet.
                    int effectiveMode = angleMode;
                    if (angleMode == 2 && child._noUpdateYet) {
                        effectiveMode = 3;  // fallthrough to case 3 (0x6BE664→0x6BE668)
                    }

                    switch (effectiveMode) {
                    case 1: // Direct angle (0x6BE5BC)
                        // Binary does NOT normalize case 1 to [0,360).
                        computedAngle = dofst + mn.accumulated.angle;
                        hasAngle = true;
                        break;
                    case 2: { // atan2 from delta position (0x6BE8C4)
                        // Binary uses v37 (potentially interpolated) not raw dofst
                        double dy_comp, dx_comp;
                        if (mn.coordinateMode == 1) {
                            dy_comp = mn.deltaPosZ; // node+192
                            dx_comp = mn.deltaPosX; // node+176
                        } else if (mn.coordinateMode == 0) {
                            dy_comp = mn.deltaPosY; // node+184
                            dx_comp = mn.deltaPosX; // node+176
                        } else {
                            // Binary: non-0/non-1 coordinateMode → LABEL_129
                            hasAngle = true;
                            break;
                        }
                        computedAngle = v37 + std::atan2(dy_comp, dx_comp) * 360.0 / 6.28318531;
                        hasAngle = true;
                        break;
                    }
                    case 3: { // Interpolated atan2 (LABEL_83: 0x6BE668..0x6BE79C)
                        // Binary: guard: crossfading && !otherSlotDone (0x6BE680).
                        // If guard fails → hasAngle=false (LABEL_119).
                        // Otherwise: compute ratio from parent time, call sub_69A4D4
                        // twice (at t and t+0.0001) for finite-difference derivative,
                        // then atan2 on delta based on coordinateMode.
                        if (!mn.activeSlot().crossfading
                            || mn.otherSlot().done) {
                            // Guard fails → LABEL_119: hasAngle=false
                            break;
                        }
                        // Parent time (0x6BE688..0x6BE6B0): node+8+40 is
                        // parameterEntry->value; fallback is player+456.
                    double parentTime =
                        parameterEntry ? parameterEntry->value : _clampedEvalTime;
                        double currentStart = mn.activeSlot().clipStartTime;
                        double otherStart = mn.otherSlot().clipStartTime;
                        double denom = otherStart - currentStart;
                        // Binary divides directly without zero guard (0x6BE6D0)
                        double ratio = (parentTime - currentStart) / denom;
                        double t2 = ratio + 0.0001;
                        if (t2 >= 1.0) ratio = 0.9999;
                        t2 = std::min(t2, 1.0);
                        // sub_69A4D4: interpolate between slot positions.
                        // src = currentSlot+96 = current evaluated position
                        // dst = otherSlot+96 = position from before crossfade
                        const auto &slot = mn.activeSlot();
                        BezierCurve cccCurve;
                        cccCurve.x = slot.ccc.x; cccCurve.y = slot.ccc.y;
                        ControlPointCurve cpCurve;
                        if (slot.hasCpRotation) {
                            cpCurve.x = slot.cp.x; cpCurve.y = slot.cp.y;
                            cpCurve.t = slot.cp.t;
                        }
                        // Use crossfade slot positions: src=current, dst=other (saved at flip)
                        // Binary reads full {x,y,z} from active slot (a3+96..112).
                        double src[3] = {slot.x, slot.y, mn.activeSlot().z};
                        double dst[3] = {mn.otherSlot().x, mn.otherSlot().y, mn.otherSlot().z};
                        double out1[3] = {}, out2[3] = {};
                        interpolatePosition69A4D4(cccCurve, dst, src, out1, mn.coordinateMode, cpCurve, ratio);
                        interpolatePosition69A4D4(cccCurve, dst, src, out2, mn.coordinateMode, cpCurve, t2);
                        // Pick dx/dy based on coordinateMode (0x6BE72C..0x6BE740)
                        double dx_comp, dy_comp;
                        if (mn.coordinateMode == 1) {
                            dx_comp = out2[0] - out1[0]; dy_comp = out2[2] - out1[2];
                        } else if (mn.coordinateMode == 0) {
                            dx_comp = out2[0] - out1[0]; dy_comp = out2[1] - out1[1];
                        } else {
                            hasAngle = true;
                            break; // LABEL_129
                        }
                        computedAngle = v37 + std::atan2(dy_comp, dx_comp) * 360.0 / 6.28318531;
                        hasAngle = true;
                        break;
                    }
                    case 4: { // Target node lookup (0x6BE7B4)
                        // Binary: hasAngle is only set to true when target found
                        // and angle computed. LABEL_119 sets hasAngle=false.
                        const auto &dtgt = mn.activeSlot().motionDtgt;
                        if (dtgt.empty()) break; // LABEL_119: hasAngle=false
                        int targetIdx = findNodeByLabel(_runtime->nodeLabelMap, dtgt);
                        if (targetIdx < 0) break; // LABEL_119: hasAngle=false
                        const auto &target = nodes[targetIdx];
                        double dy_comp, dx_comp;
                        if (mn.coordinateMode == 1) {
                            dy_comp = target.accumulated.posZ - mn.accumulated.posZ;
                            dx_comp = target.accumulated.posX - mn.accumulated.posX;
                        } else if (mn.coordinateMode == 0) {
                            dy_comp = target.accumulated.posY - mn.accumulated.posY;
                            dx_comp = target.accumulated.posX - mn.accumulated.posX;
                        } else {
                            hasAngle = true; // LABEL_129
                            break;
                        }
                        computedAngle = v37 + std::atan2(dy_comp, dx_comp) * 360.0 / 6.28318531;
                        hasAngle = true;
                        break;
                    }
                    default: break; // LABEL_119: hasAngle=false
                    }
                    // Binary normalizes per-case (cases 2,3,4 each have inline loops).
                    // Case 1 does NOT normalize. Skip normalization for case 1.
                    if (effectiveMode != 1) {
                        while (computedAngle < 0.0) computedAngle += 360.0;
                        while (computedAngle >= 360.0) computedAngle -= 360.0;
                    }
                }

                // === Origin offset (0x6BE994..0x6BE9F4) ===
                double posX = mn.accumulated.posX;
                double posY = mn.accumulated.posY;
                double posZ = mn.accumulated.posZ;

                const double originX = mn.activeSlot().ox;
                const double originY = mn.activeSlot().oy;
                if (originX != 0.0 || originY != 0.0) {
                    const double negOY = -originY;
                    // v79 = m12*negOY - originX*m11 (0x6BE9E0)
                    const double vx = mn.accumulated.m12 * negOY - originX * mn.accumulated.m11;
                    // v80 = m22*negOY - originX*m21 (0x6BE9E4)
                    const double vy = mn.accumulated.m22 * negOY - originX * mn.accumulated.m21;
                    if (mn.coordinateMode == 1) {
                        posX += vx;
                        posZ += vy;
                    } else {
                        posX += vx;
                        posY += vy;
                    }
                }

                // === State propagation to child root node (0x6BEA18..0x6BEB74) ===
                if (child._runtime && !child._runtime->nodes.empty()) {
                    auto &cr = child._runtime->nodes[0];
                    cr.delta.posX = posX;
                    cr.delta.posY = posY;
                    cr.delta.posZ = posZ;
                    // Flip — child root delta block node+1587/+1588.
                    if (cr.delta.flipX != mn.accumulated.flipX ||
                        cr.delta.flipY != mn.accumulated.flipY) {
                        cr.delta.flipX = mn.accumulated.flipX;
                        cr.delta.flipY = mn.accumulated.flipY;
                        cr.delta.dirty = true;
                    }
                    // Scale — child root delta block node+1624/+1632.
                    if (cr.delta.scaleX != mn.accumulated.scaleX ||
                        cr.delta.scaleY != mn.accumulated.scaleY) {
                        cr.delta.scaleX = mn.accumulated.scaleX;
                        cr.delta.scaleY = mn.accumulated.scaleY;
                        cr.delta.dirty = true;
                    }
                    // Slant — child root delta block node+1640/+1648.
                    if (cr.delta.slantX != mn.accumulated.slantX ||
                        cr.delta.slantY != mn.accumulated.slantY) {
                        cr.delta.slantX = mn.accumulated.slantX;
                        cr.delta.slantY = mn.accumulated.slantY;
                        cr.delta.dirty = true;
                    }
                    // Opacity — child root delta block node+1656.
                    if (cr.delta.opacity != mn.accumulated.opacity) {
                        cr.delta.opacity = mn.accumulated.opacity;
                        cr.delta.dirty = true;
                    }
                    // Active/visible overrides are consumed by the next child
                    // Player_updateLayers root memcpy at 0x6BB4D4.
                    if (cr.delta.activeOverride != mn.accumulated.active) {
                        cr.delta.activeOverride = mn.accumulated.active;
                        cr.delta.dirty = true;
                    }
                    if (cr.delta.visibleOverride != mn.accumulated.visible) {
                        cr.delta.visibleOverride = mn.accumulated.visible;
                        cr.delta.dirty = true;
                    }
                    // Parent color propagation (0x6BEB7C)
                    // Binary: *(_DWORD *)(v16 + 1156) = *(_DWORD *)(v10 + 100)
                    // Reads node+100 (colorBytes[0..3] packed as uint32 RGBA), writes to
                    // child player+1156 (_parentColorPacked). NOT a blend mode field.
                    {
                        uint32_t packed;
                        std::memcpy(&packed, &mn.colorBytes[0], sizeof(uint32_t));
                        child._parentColorPacked = packed;
                    }

                    // isEmoteMode check + zFactor (0x6BEA90..0x6BEA94)
                    child._zFactor = _zFactor;
                    // Binary at 0x6BEA98: if isEmoteMode, call Player_initEmoteMotion(child, 2)
                    // This syncs emote bone state. Emote mode is not used in web port.

                    // === Angle → child (0x6BEAA8..0x6BEB08) ===
                    if (hasAngle) {
                        if (child._runtime->isEmoteMode) {
                            // Emote mode: normalize angle [0,360), set player+464, reinit
                            double k = computedAngle;
                            while (k < 0.0) k += 360.0;
                            while (k >= 360.0) k -= 360.0;
                            // player+464 = emote angle (not mapped in web port)
                            // Player_initEmoteMotion(child, 2) — N/A for web
                        } else {
                            if (cr.delta.angle != computedAngle) {
                                cr.delta.angle = computedAngle;
                                cr.delta.dirty = true;
                            }
                        }
                    }

                    // === Matrix propagation (0x6BEB9C..0x6BEC4C) ===
                    // Binary at 0x6BEB90: condition is hasAngle || angle==accAngle || child._directEdit
                    // (player+482). When _directEdit is true, direct-copy path is taken.
                    if (hasAngle || computedAngle == mn.accumulated.angle ||
                        child._directEdit) {
                        // Direct copy (0x6BEB9C)
                        cr.accumulated.m11 = mn.accumulated.m11;
                        cr.accumulated.m12 = mn.accumulated.m12;
                        cr.accumulated.m21 = mn.accumulated.m21;
                        cr.accumulated.m22 = mn.accumulated.m22;
                    } else {
                        // Rotate by (computedAngle - accumulated.angle) (0x6BEBC8..0x6BEC4C)
                        double delta = (computedAngle - mn.accumulated.angle)
                                       * 3.14159265 * 2.0 / 360.0;
                        if (mn.accumulated.flipX != mn.accumulated.flipY)
                            delta = -delta;
                        const double c = std::cos(delta);
                        const double s = std::sin(delta);
                        cr.accumulated.m11 = c * mn.accumulated.m11 + s * mn.accumulated.m12;
                        cr.accumulated.m12 = c * mn.accumulated.m12 - mn.accumulated.m11 * s;
                        cr.accumulated.m21 = c * mn.accumulated.m21 + s * mn.accumulated.m22;
                        cr.accumulated.m22 = c * mn.accumulated.m22 - mn.accumulated.m21 * s;
                    }
                    // Unconditional child-root delta dirty after matrix
                    // propagation (0x6BEBAC writes childRoot+1584).
                    cr.delta.dirty = true;
                    // Note: clip chain propagation is done in label_18 below,
                    // which ALL paths (active + inactive) fall through to.
                }

            }
            } // end childPtr scope — goto label_18 can jump here
            // Fall through to label_18 (matches binary: active path → LABEL_18)

        label_18:
            // LABEL_18: shared exit for ALL paths (0x6BE278..0x6BE2F8).
            // Binary always calls frameProgress + updateLayers on child,
            // even for inactive/non-visible nodes.
            if (auto *childP = mn.getChildPlayer()) {
                auto &child = *childP;
                if(detail::logoSnapshotMarkEnabledForPath(motionPath) &&
                   motionPath.find("m2logo.mtn") != std::string::npos &&
                   currentTime >= 30.0 && currentTime <= 50.0) {
                    const auto *activeClip = child.selectActiveClip();
                    std::fprintf(
                        stderr,
                        "SNAPCHILD phase=runtime frame=%.3f nodeIndex=%d src=%s parameterizeIndex=%d childActiveMotion=%s childMotionKey=%s childClip=%s childAllPlaying=%d childQueuing=%d childNodesBuilt=%d childNodeCount=%zu childNeedsAssignImages=%d\n",
                        currentTime,
                        mn.index,
                        mn.activeSlot().src.empty() ? "<none>"
                                                    : mn.activeSlot().src.c_str(),
                        mn.parameterizeIndex,
                        child._runtime && child._runtime->activeMotion
                            ? child._runtime->activeMotion->path.c_str()
                            : "<none>",
                        detail::narrow(child.getMotion()).c_str(),
                        activeClip ? activeClip->label.c_str() : "<none>",
                        child._allplaying ? 1 : 0,
                        child._queuing ? 1 : 0,
                        child._runtime && child._runtime->nodes.size() > 1 ? 1 : 0,
                        child._runtime ? child._runtime->nodes.size() : 0,
                        child._needsInternalAssignImages ? 1 : 0);
                }
                if (child._runtime && !child._runtime->nodes.empty()) {
                    auto &cr = child._runtime->nodes[0];
                    // Clip chain propagation (0x6BE278..0x6BE29C)
                    // Binary: v17+1936 = v10+1936 (parentClipIndex)
                    //         v18 = v10; if (!node+1963) v18 = *(v10+1968)
                    //         v17+1968 = v18 (visibleAncestor with conditional)
                    //         v17+1952 = v10+1952 (third field — not mapped in our arch)
                    cr.parentClipIndex = mn.parentClipIndex;
                    // Binary 0x6BE280: if meshCombineEnabled, current node is ancestor;
                    // otherwise, propagate stored ancestor.
                    if (mn.meshCombineEnabled) {
                        cr.visibleAncestorIndex = static_cast<int>(i);
                    } else {
                        cr.visibleAncestorIndex = mn.visibleAncestorIndex;
                    }
                    // Binary 0x6BE29C: propagates node+1952 (forceVisible) to child root
                    cr.forceVisible = mn.forceVisible;
                }
                // Step child: frameProgress + updateLayers (0x6BE2A4..0x6BE2AC)
                // Binary calls both unconditionally (no guard). Child's node
                // tree was eagerly built when its play/onFindMotion fired
                // earlier in this loop (see 0x6BE41C eager chain) — the
                // binary assumes nodes are already ready here.
                child.frameProgress(_frameLastTime);
                child.updateLayers();
            }
        }

    }


} // namespace motion
