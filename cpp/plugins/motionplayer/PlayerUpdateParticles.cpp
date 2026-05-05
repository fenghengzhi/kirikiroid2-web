// PlayerUpdateParticles.cpp — updateLayers particle emitter and particle system phases
// Split from PlayerUpdateLayers.cpp for maintainability.
//
#include "PlayerUpdateLayersInternal.h"

namespace motion {
    void Player::updateLayersPhase3_ParticleEmitter() {
        auto &nodes = _runtime->nodes;
        // --- sub_6BEDD0: Particle emitter state (nodeType=6) ---
        // Aligned to 0x6BEDD0. Only when !isEmoteMode.
        if (_runtime->isEmoteMode) return;

        for (size_t ei = 1; ei < nodes.size(); ++ei) {
            auto &en = nodes[ei];
            if (en.nodeType != 6) continue;

            // Active/slotDone guard (0x6BEE90..0x6BEEC4)
            if (!en.accumulated.active || en.activeSlot().done) {
                en.emitterActive = false;
                en.emitterDtgt.clear();
                en.emitterTimer = 0.0;
                continue;
            }

            // dtgt from clip slot (node+536*slot+356, our activeSlot().src)
            const std::string &dtgt = en.activeSlot().src;
            if (dtgt.empty()) {
                en.emitterActive = false;
                en.emitterDtgt.clear();
                en.emitterTimer = 0.0;
                continue;
            }

            // Flags gate + re-resolve logic (0x6BEED8..0x6BEF9C)
            // Binary checks whole byte at node+44: LDRB W9,[X21,#0x2C]; CBZ W9
            // If flags==0: always LABEL_27 (continue, just accumulate timer).
            // If flags!=0: check emitterActive + dtgt comparison.
            bool doAccumulate; // true=LABEL_27 (timer += dt), false=LABEL_21 (re-resolve)

            if (!en.flags) {
                // node+44 flags == 0: skip re-resolve → LABEL_27 (0x6BEEE0)
                doAccumulate = true;
            } else if (!en.emitterActive) {
                // First init → LABEL_21 (0x6BEEFC)
                doAccumulate = false;
            } else if (en.emitterDtgt == dtgt) {
                // Same dtgt (pointer or string compare) → LABEL_27 (0x6BEEF8)
                doAccumulate = true;
            } else {
                // dtgt changed → LABEL_21
                doAccumulate = false;
            }

            if (doAccumulate) {
                // LABEL_27 (0x6BEF88): emitterTimer = _frameLastTime + emitterTimer
                en.emitterTimer += _frameLastTime;
            } else {
                // LABEL_21 (0x6BEF48): re-resolve dtgt, compute time offset.
                // Binary does NOT flip activeSlotIndex here (v10 is read once at
                // 0x6BEE9C and never modified). No crossfading flag is set either.
                en.emitterActive = true;
                en.emitterDtgt = dtgt;
                // Timer = (parentTime - clipSlot.startTime) + clipSlot.timeOffset
                // Aligned to 0x6BEF74..0x6BEFA8:
                //   parentTime = node+8 ? parameterEntry->value : player+1120
                // Falls back to _frameLoopTime if node+8 is null.
                auto *parameterEntry = resolveNodeParameterEntry(*_runtime, en);
                double parentTime =
                    parameterEntry ? parameterEntry->value : _frameLoopTime;
                double startTime = en.activeSlot().clipStartTime;
                double timeOffset = en.activeSlot().motionTimeOffset;
                en.emitterTimer = (parentTime - startTime) + timeOffset;
            }

            // Binary: emitterOffsetActive = false AFTER branch convergence (0x6BEFB0)
            en.emitterOffsetActive = false;

            // Trigger type handling (0x6BEFC4..0x6BF0B8)
            // triggerType from clipSlot (node+536*slot+708)
            const int triggerType = en.activeSlot().prtTrigger;

            switch (triggerType) {
            case 4: {
                // Target position offset (0x6BF048..0x6BF0B8)
                // sub_6F2228 resolves target node by name from slot+712 (motionDtgt).
                // Compute position difference: target.pos - emitter.pos
                int targetIdx = findNodeByLabel(_runtime->nodeLabelMap, en.activeSlot().motionDtgt);
                if (targetIdx >= 0 && targetIdx < static_cast<int>(nodes.size())) {
                    auto &target = nodes[targetIdx];
                    en.emitterOffsetActive = true;
                    en.emitterOffsetX = target.accumulated.posX - en.accumulated.posX;
                    en.emitterOffsetY = target.accumulated.posY - en.accumulated.posY;
                    en.emitterOffsetZ = target.accumulated.posZ - en.accumulated.posZ;
                }
                break;
            }
            case 3: {
                // LABEL_36 (0x6BF028): sub_6C1540 equivalent.
                // sub_6C1540 guard at 0x6C1574: *(a3+25) [crossfading] && !*(a4+24) [otherSlotDone].
                // ratio at 0x6C15A8: (player+456 - currentSlot.startTime) / (otherSlot.startTime - currentSlot.startTime)
                // src = currentSlot+96 (current evaluated position), dst = otherSlot+96 (saved at crossfade start).
                if (en.activeSlot().crossfading && !en.otherSlot().done) {
                    const auto &slot = en.activeSlot();
                    double currentStart = slot.clipStartTime;
                    double otherStart = en.otherSlot().clipStartTime;
                    double denom = otherStart - currentStart;
                    // Binary divides directly without denom!=0 guard (0x6C15A8)
                    constexpr double epsilon = 0.0001;
                    double ratio = (_clampedEvalTime - currentStart) / denom;
                    double t2 = ratio + epsilon;
                    if (t2 >= 1.0) ratio = 0.9999;
                    t2 = std::min(t2, 1.0);
                    BezierCurve cccCurve;
                    cccCurve.x = slot.ccc.x; cccCurve.y = slot.ccc.y;
                    ControlPointCurve cpCurve;
                    if (slot.hasCpRotation) {
                        cpCurve.x = slot.cp.x; cpCurve.y = slot.cp.y;
                        cpCurve.t = slot.cp.t;
                    }
                    // src = current slot position, dst = other slot position (saved at flip)
                    // Binary reads full {x,y,z} from active slot (a3+96..112).
                    double src[3] = {slot.x, slot.y, en.activeSlot().z};
                    double dst[3] = {en.otherSlot().x, en.otherSlot().y, en.otherSlot().z};
                    double out1[3] = {}, out2[3] = {};
                    interpolatePosition69A4D4(cccCurve, dst, src, out1,
                        en.coordinateMode, cpCurve, ratio);
                    interpolatePosition69A4D4(cccCurve, dst, src, out2,
                        en.coordinateMode, cpCurve, t2);
                    en.emitterOffsetActive = true;
                    en.emitterOffsetX = out2[0] - out1[0];
                    en.emitterOffsetY = out2[1] - out1[1];
                    en.emitterOffsetZ = out2[2] - out1[2];
                }
                break;
            }
            case 2: {
                // (0x6BEFF0..0x6BF020)
                // Binary checks player+608 (_noUpdateYet) OR emitterTimer==0 (0x6BEFF4)
                if (_noUpdateYet || en.emitterTimer == 0.0) {
                    // Queuing or zero timer → same as case 3: sub_6C1540
                    // sub_6C1540 guard: crossfading && !otherSlotDone (0x6C1574)
                    if (en.activeSlot().crossfading && !en.otherSlot().done) {
                        const auto &slot = en.activeSlot();
                        double currentStart = slot.clipStartTime;
                        double otherStart = en.otherSlot().clipStartTime;
                        double denom = otherStart - currentStart;
                        // Binary divides directly without denom!=0 guard (0x6C15A8)
                        constexpr double epsilon = 0.0001;
                        double ratio = (_clampedEvalTime - currentStart) / denom;
                        double t2 = ratio + epsilon;
                        if (t2 >= 1.0) ratio = 0.9999;
                        t2 = std::min(t2, 1.0);
                        BezierCurve cccCurve;
                        cccCurve.x = slot.ccc.x; cccCurve.y = slot.ccc.y;
                        ControlPointCurve cpCurve;
                        if (slot.hasCpRotation) {
                            cpCurve.x = slot.cp.x; cpCurve.y = slot.cp.y;
                            cpCurve.t = slot.cp.t;
                        }
                        double src[3] = {slot.x, slot.y, en.activeSlot().z};
                        double dst[3] = {en.otherSlot().x, en.otherSlot().y, en.otherSlot().z};
                        double out1[3] = {}, out2[3] = {};
                        interpolatePosition69A4D4(cccCurve, dst, src, out1,
                            en.coordinateMode, cpCurve, ratio);
                        interpolatePosition69A4D4(cccCurve, dst, src, out2,
                            en.coordinateMode, cpCurve, t2);
                        en.emitterOffsetActive = true;
                        en.emitterOffsetX = out2[0] - out1[0];
                        en.emitterOffsetY = out2[1] - out1[1];
                        en.emitterOffsetZ = out2[2] - out1[2];
                    }
                } else {
                    // Non-queuing, timer running: binary reads node+176/184/192
                    // directly (0x6BF004..0x6BF020), which ARE deltaPosX/Y/Z.
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

    void Player::updateLayersPhase3_ParticleSystem(double currentTime) {
        // --- sub_6BF0DC: Particle system (nodeType=4) ---
        // Fully aligned to libkrkr2.so 0x6BF0DC (~800 lines decompiled).
        // Velocity stored on child Player _cameraVelocityX/Y/Z (player+784/792/800).
        // frameProgress + updateLayersPhase1_PreLoop auto-applies velocity+damping.
        if (_runtime->isEmoteMode) return;
        auto &nodes = _runtime->nodes;
        const double dt = _frameLastTime;
        constexpr double PI = 3.14159265358979323846;

        for (size_t pi = 1; pi < nodes.size(); ++pi) {
            auto &pn = nodes[pi];
            if (pn.nodeType != 4) continue;

            // Binary flow: BLOCK 1 (child position update) runs BEFORE the LABEL_64
            // activity check. The activity check only gates BLOCK 2 (emission control).
            // Existing particles ALWAYS get position updates even when inactive/done.

            const int childCount = pn.getParticleCount();

            // ====== BLOCK 1: Existing particle update (0x6BF310..0x6BF668) ======
            // Binary guard: particleInheritVelocity==2 gates ALL child position updates (0x6BF304).
            // If != 2: goto LABEL_64 (skip ALL child position updates).
            // If == 2: check !slotDone && particleInheritAngle for full matrix update;
            // otherwise just add deltaPos to existing children (0x6BF32C..0x6BF384).
            if (pn.particleInheritVelocity == 2 && childCount >= 1 && !pn.activeSlot().done && pn.particleInheritAngle) {
                const double curM11 = pn.accumulated.m11, curM21 = pn.accumulated.m21;
                const double curM12 = pn.accumulated.m12, curM22 = pn.accumulated.m22;

                const bool matrixChanged =
                    (curM11 != pn.prevM11 || curM21 != pn.prevM21 ||
                     curM12 != pn.prevM12 || curM22 != pn.prevM22);

                if (matrixChanged) {
                    // Compute inv(prev) * cur (0x6BF458..0x6BF49C)
                    // Binary divides each element by det WITHOUT negation,
                    // then computes the product as subtraction pairs.
                    // This is inv(prev) * cur, NOT cur * inv(prev).
                    const double det = pn.prevM11 * pn.prevM22 - pn.prevM12 * pn.prevM21;
                    {
                        const double id = 1.0 / det;
                        const double id_m22 = pn.prevM22 * id;  // v34
                        const double id_m21 = pn.prevM21 * id;  // v35 (no negation)
                        const double id_m12 = pn.prevM12 * id;  // v36 (no negation)
                        const double id_m11 = pn.prevM11 * id;  // v37
                        // inv(prev) * cur coefficients (0x6BF490..0x6BF49C)
                        const double t11 = curM11 * id_m22 - curM21 * id_m12;  // v39
                        const double t12 = curM21 * id_m11 - curM11 * id_m21;  // v40
                        const double t21 = curM12 * id_m22 - curM22 * id_m12;  // v41
                        const double t22 = curM22 * id_m11 - curM12 * id_m21;  // v42

                        // Angle delta (0x6BF404..0x6BF43C)
                        // Binary reads node+1536 = accumulated.angle, not interpolated
                        const double curAngle = pn.accumulated.angle;
                        double angleDelta = curAngle - pn.prevParticleAngle;
                        if (pn.accumulated.flipX == pn.accumulated.flipY)
                            angleDelta = curAngle - pn.prevParticleAngle;
                        else
                            angleDelta = -(curAngle - pn.prevParticleAngle);
                        pn.prevParticleAngle = curAngle;

                        const double posXref = pn.accumulated.posX;
                        const double posYref = pn.accumulated.posY;
                        const double posZref = pn.accumulated.posZ;
                        const double dPosX = pn.deltaPosX, dPosY = pn.deltaPosY;
                        const double dPosZ = pn.deltaPosZ;

                        for (int ci = 0; ci < childCount; ++ci) {
                            auto *child = pn.getParticleChild(ci);
                            if (!child || !child->_runtime || child->_runtime->nodes.empty()) continue;
                            auto &cr = child->_runtime->nodes[0];

                            // Rotate child angle (0x6BF4C4..0x6BF528)
                            // Binary checks child._directEdit (player+482) for emote path.
                            // If _directEdit: writes to player+464 and calls initEmoteMotion.
                            // If not: writes to root node accumulated.angle.
                            if (child->_directEdit) {
                                // Emote angle path — not applicable in web port
                                // player+464 = emote angle, Player_initEmoteMotion(child, 2)
                            } else {
                                double cAngle = cr.accumulated.angle + angleDelta;
                                while (cAngle < 0.0) cAngle += 360.0;
                                while (cAngle >= 360.0) cAngle -= 360.0;
                                cr.accumulated.angle = cAngle;
                            }

                            // Transform position (0x6BF54C..0x6BF620)
                            const int coordMode = pn.coordinateMode;
                            if (coordMode == 1) {
                                // 3D: X/Z through matrix, Y pass-through
                                double px = cr.accumulated.posX - posXref + dPosX;
                                double pz = cr.accumulated.posZ - posZref + dPosZ;
                                cr.accumulated.posX = posXref + t11 * px + t12 * pz;
                                cr.accumulated.posZ = posZref + t21 * px + t22 * pz;
                                cr.accumulated.posY += dPosY;
                            } else {
                                // 2D: Binary swaps X↔Z (0x6BF5D0..0x6BF620):
                                //   newPosX = oldPosZ + deltaPosZ
                                //   newPosY = posYref + t21*px + t22*py
                                //   newPosZ = posXref + t11*px + t12*py
                                double px = cr.accumulated.posX - posXref + dPosX;
                                double py = cr.accumulated.posY - posYref + dPosY;
                                cr.accumulated.posX = cr.accumulated.posZ + dPosZ;
                                cr.accumulated.posY = posYref + t21 * px + t22 * py;
                                cr.accumulated.posZ = posXref + t11 * px + t12 * py;
                            }

                            // Transform velocity (0x6BF628..0x6BF64C)
                            double vx = child->_cameraVelocityX;
                            double vy = (coordMode == 1) ? child->_cameraVelocityZ
                                                         : child->_cameraVelocityY;
                            double nvx = t11 * vx + t12 * vy;
                            double nvy = t21 * vx + t22 * vy;
                            child->_cameraVelocityX = nvx;
                            if (coordMode == 1) child->_cameraVelocityZ = nvy;
                            else child->_cameraVelocityY = nvy;
                        }
                    }
                    pn.prevM11 = curM11; pn.prevM21 = curM21;
                    pn.prevM12 = curM12; pn.prevM22 = curM22;
                } else {
                    // Matrix unchanged: just add delta position (0x6BF348..0x6BF384)
                    for (int ci = 0; ci < childCount; ++ci) {
                        auto *child = pn.getParticleChild(ci);
                        if (!child || !child->_runtime || child->_runtime->nodes.empty()) continue;
                        auto &cr = child->_runtime->nodes[0];
                        cr.accumulated.posX += pn.deltaPosX;
                        cr.accumulated.posY += pn.deltaPosY;
                        cr.accumulated.posZ += pn.deltaPosZ;
                    }
                }
            } else if (pn.particleInheritVelocity == 2 && childCount >= 1) {
                // Missing path from binary (0x6BF32C..0x6BF384):
                // When particleInheritVelocity==2 but (slotDone || !particleInheritAngle),
                // still add deltaPos to existing children's positions.
                for (int ci = 0; ci < childCount; ++ci) {
                    auto *child = pn.getParticleChild(ci);
                    if (!child || !child->_runtime || child->_runtime->nodes.empty()) continue;
                    auto &cr = child->_runtime->nodes[0];
                    cr.accumulated.posX += pn.deltaPosX;
                    cr.accumulated.posY += pn.deltaPosY;
                    cr.accumulated.posZ += pn.deltaPosZ;
                }
            }
            // Binary: when particleInheritVelocity != 2, goto LABEL_64 (0x6BF314)
            // skips ALL child position updates — no deltaPos addition.

            // ====== LABEL_64: Activity check (0x6BF668..0x6BF710) ======
            // Binary: only !accumulated.active sets particleEmitterFlagActive=false.
            // slotDone alone does NOT reset the flag — it just skips emission.
            // emitCount declared here so goto doesn't cross initialization.
            {
            int emitCount = 0;
            if (!pn.accumulated.active) {
                pn.particleEmitterFlagActive = false;
                goto physics_step;
            }

            // ====== BLOCK 2: Emission control (0x6BF668..0x6BF810) ======
            // Binary: slotDone skips emission but does NOT reset particleEmitterFlagActive.
            if (pn.activeSlot().done) goto physics_step;
            {
                const double prtFmin = pn.activeSlot().prtFmin;
                const double prtF = pn.activeSlot().prtF;
                const int prtTrigger = pn.activeSlot().prtTrigger;

                if (prtTrigger == 0 && prtFmin == 0.0) goto physics_step;

                const bool wasActive = pn.particleEmitterFlagActive;
                pn.particleEmitterFlagActive = true;

                // Read trigger type from slot (0x6BF680..0x6BF690)
                const int triggerType = pn.prtTrigger;

                if (triggerType == 0) {
                    // Frequency mode (0x6BF690..0x6BF7F4)
                    if (!wasActive) {
                        // First frame: initialize timer (0x6BF7BC..0x6BF7EC)
                        // Binary interpolates in frequency domain: lerp(60/prtFmin, 60/prtF, r)
                        double freq0 = 60.0 / prtFmin;
                        double freq1 = 60.0 / prtF;
                        if (freq0 != freq1)
                            freq0 = freq0 + (freq1 - freq0) * random();
                        pn.emitterTimerAccum = freq0;
                    }
                    // Timer loop (0x6BF698..0x6BF6F8)
                    pn.emitterTimerAccum -= dt;
                    while (pn.emitterTimerAccum <= 0.0) {
                        double freq0 = 60.0 / prtFmin;
                        double freq1 = 60.0 / prtF;
                        if (freq0 != freq1)
                            freq0 = freq0 + (freq1 - freq0) * random();
                        pn.emitterTimerAccum += freq0;
                        ++emitCount;
                    }
                    // LABEL_85 timer clamp (0x6BF780..0x6BF7B8)
                    // Only for frequency mode (triggerType==0).
                    // Clamps timer to min(60/prtFmin, currentTimer).
                    if (prtFmin > 0.0) {
                        double maxTimer = 60.0 / prtFmin;
                        if (maxTimer > pn.emitterTimerAccum)
                            maxTimer = pn.emitterTimerAccum;
                        pn.emitterTimerAccum = maxTimer;
                        if (emitCount <= 0) goto physics_step;
                    }
                } else if (triggerType == 1) {
                    // Count mode (0x6BF734..0x6BF804)
                    // Binary checks node+44 (flags byte, v173) not particleInheritAngle.
                    if (pn.flags) {
                        double r = random();
                        emitCount = static_cast<int>(prtFmin + (prtF - prtFmin) * r);
                    }
                    // Timer clamp is NOT applied for triggerType==1 in binary.
                    // LABEL_85 (0x6BF780) is only reachable from the frequency mode path.
                    if (emitCount <= 0) goto physics_step;
                }
            }

            // ====== BLOCK 3: Particle creation (0x6BF810..0x6C02DC) ======
            // Binary creates exactly 1 particle per frame per node.
            // When srcList is empty (v85==0), skip creation and run ONE physics step (0x6C02D0).
            // When emitCount > 1, physics is skipped; next frame creates another particle.
            if (emitCount > 0) {
                // 3a. Resolve srcList count (0x6BF810..0x6BF87C)
                const auto &srcList = pn.activeSlot().srcList;
                const int srcListCount = static_cast<int>(srcList.size());

                // Binary: if srcList count==0, skip creation entirely (0x6C02D0).
                // The binary decrements emitCount to 0 (a no-op loop), then runs
                // ONE physics step. No particles are created.
                if (srcListCount == 0) {
                    goto physics_step;
                }

                // Binary creates exactly 1 particle per frame per node (0x6BF810..0x6C02DC).
                // emitCount > 1 just means "skip physics this frame" (0x6C0270).
                {

                // Random selection from srcList (0x6BF87C: v86 = random() * v85)
                int idx = static_cast<int>(random() * srcListCount);
                if (idx >= srcListCount) idx = srcListCount - 1;
                const std::string &selectedSrc = srcList[idx];
                if (selectedSrc.empty()) goto physics_step;

                // Handle "chara/motion" format (binary: sub_697D34 splits by "/")
                std::string particleChara;
                std::string motionPath;
                auto slashPos = selectedSrc.find('/');
                if (slashPos != std::string::npos) {
                    particleChara = selectedSrc.substr(0, slashPos);
                    motionPath = selectedSrc.substr(slashPos + 1);
                } else {
                    motionPath = selectedSrc;
                }

                // 3b. Create child Player via TJS dispatch (0x6BF93C..0x6BFA00)
                // Aligned to binary: new Player → CreateAdaptor → Array.add
                using PlayerAdaptor = ncbInstanceAdaptor<Player>;
                auto *childRaw = new Player(_resourceManagerNative, this);
                childRaw->_tjsRandomGenerator = _tjsRandomGenerator;
                iTJSDispatch2 *childDisp = PlayerAdaptor::CreateAdaptor(childRaw);
                if (!childDisp) { delete childRaw; goto physics_step; }
                tTJSVariant childVar(childDisp, childDisp);
                childDisp->Release();
                auto *child = childRaw;  // native pointer for subsequent use
                // Binary: chara comes from the split path, not parent chara
                child->setChara(particleChara.empty() ? _chara : detail::widen(particleChara));
                child->onFindMotion(detail::widen(motionPath));
                // Stealth motion (0x6BFA08..0x6BFA40): binary checks child+776
                // (stealth motion path) and plays it with flag 0x10. In our arch,
                // propagate parent stealth fields so child resolves stealth if set.
                child->_stealthChara = _stealthChara;
                child->_stealthMotion = _stealthMotion;
                if (!_stealthMotion.IsEmpty()) {
                    child->onFindMotion(_stealthMotion, PlayFlagStealth);
                }
                // _parentColorPacked propagation (0x6BF9B4)
                {
                    uint32_t packed;
                    std::memcpy(&packed, &pn.colorBytes[0], sizeof(uint32_t));
                    child->_parentColorPacked = packed;
                }
                // emoteEdit propagation (0x6BF9C0..0x6BF9D4)
                child->_findMotionContextVariant = _findMotionContextVariant;
                child->_zFactor = _zFactor;
                child->_independentLayerInherit = _independentLayerInherit;

                // Set blendMode on child root node accumulated state (0x6BFAA8..0x6BFAC4)
                // Binary writes to *(v99+1656) = root node accumulated blendMode, not activeSlot.
                if (child->_runtime && !child->_runtime->nodes.empty()) {
                    auto &cr = child->_runtime->nodes[0];
                    auto blendVal = pn.activeSlot().blendMode;
                    if (cr.accumulated.blendMode != blendVal) {
                        cr.accumulated.dirty = true;
                        cr.accumulated.blendMode = blendVal;
                    }
                }

                // Stealth motion play (0x6BFA08..0x6BFA40)
                // Binary: if child+776 (stealth path) exists, play it with flags=16
                // In our architecture, stealth is stored at player level.
                // The binary copies the stealth path from the resource manager state.
                // For web port, stealth motion is rarely used; skip for now.

                // 3c. Position based on flyDirection (0x6BFAC8..0x6BFC88)
                double offX = 0, offY = 0, offZ = 0;
                // Binary uses "particle" field (node+2164, PSB key "particle") for fly
                // direction, NOT particleFlyDirection (node+2180). 0x6BFAC8.
                const int flyDir = pn.particleType;
                // Binary uses node+2189 (particleTriVolume, PSB key), not coordinateMode.
                const bool has3D = pn.particleTriVolume;

                if (flyDir == 2) {
                    // Uniform box (0x6BFB88..0x6BFBCC)
                    // Binary RNG order: r1→offX (v110→v168), r2→offY (v167) (0x6BFB88)
                    double r1 = random();
                    offY = random() * 32.0 - 16.0;  // r2→offY (v167)
                    offX = r1 * 32.0 - 16.0;         // r1→offX (v168)
                    if (has3D) offZ = random() * 32.0 - 16.0;
                } else if (flyDir == 1) {
                    // 3D sphere (0x6BFAE4..0x6BFB78)
                    if (has3D) {
                        double r1 = random(), r2 = random(), r3 = random();
                        double phi = r2 * 2.0 * PI;
                        double theta = r1 * 2.0 * PI;
                        double radius = std::cbrt(r3) * 16.0;
                        double cosPhi = std::cos(phi);
                        offX = cosPhi * (radius * std::cos(theta));
                        offY = radius * (cosPhi * std::sin(theta));
                        offZ = radius * std::sin(phi);
                    } else {
                        // 2D disk (0x6BFC14..0x6BFC48)
                        double angle2d = random() * 2.0 * PI;
                        double radius = std::sqrt(random()) * 16.0;
                        offX = std::cos(angle2d) * radius;
                        offY = radius * std::sin(angle2d);
                    }
                } else {
                    // flyDir == 0 or other: offX=offY=0 (0x6BFBD8)
                    offX = 0.0;
                    offY = 0.0;
                }

                // Z component scale by sqrt(det(matrix)) (0x6BFC64..0x6BFC88)
                // Binary does sqrt(det) without abs — NaN for negative det.
                if (offZ != 0.0) {
                    const double det = pn.accumulated.m11 * pn.accumulated.m22
                                     - pn.accumulated.m12 * pn.accumulated.m21;
                    offZ *= std::sqrt(det);
                }

                // Transform offset through parent matrix (0x6BFCE0..0x6BFCE8)
                const double m11 = pn.accumulated.m11, m21 = pn.accumulated.m21;
                const double m12 = pn.accumulated.m12, m22 = pn.accumulated.m22;
                const double clipOX = pn.clipOriginX, clipOY = pn.clipOriginY;
                const double txOff = m11 * (offX - clipOX) + m12 * (offY - clipOY);
                const double tyOff = m21 * (offX - clipOX) + m22 * (offY - clipOY);

                // 3d. Speed = lerp(prtVmin, prtV, random()) (0x6BFC94..0x6BFCBC)
                // Binary only calls random() when min != max to preserve RNG sequence.
                double speed = pn.activeSlot().prtVmin;
                if (speed != pn.activeSlot().prtV)
                    speed = speed + (pn.activeSlot().prtV - speed) * random();

                // 3e. Direction based on particleFlyDirection (0x6BFCEC..0x6BFDE8)
                // Binary uses node+2180 (particleFlyDirection) for direction mode,
                // NOT node+2176 (particleInheritVelocity). 0x6BFCC4.
                double direction = 0.0;
                const int inhVel = pn.particleFlyDirection;

                if (inhVel == 2) {
                    // Exponential decay (0x6BFD58..0x6BFDE8)
                    double dist = std::sqrt(txOff * txOff + tyOff * tyOff + offZ * offZ);
                    double dirAngle = std::atan2(tyOff, txOff) * 360.0;
                    double decay = pn.particleAccelRatio;
                    // Binary reads cached player+1128 directly (0x6BFD88)
                    double childTotalTime = child->_cachedTotalFrames;
                    double dtNorm = childTotalTime / 60.0;
                    if (decay == 1.0) {
                        speed = (dtNorm > 0) ? dist / dtNorm : 0.0;
                    } else if (decay > 0.0 && dtNorm > 0.0) {
                        speed = dist * std::log(decay) / (std::pow(decay, dtNorm) - 1.0);
                    }
                    direction = dirAngle / (2.0 * PI) + 180.0;
                    direction = direction * PI / 180.0; // convert to radians
                    speed /= 60.0;
                } else if (inhVel == 1) {
                    // Offset direction (0x6BFCF4..0x6BFD18)
                    direction = std::atan2(tyOff, txOff) * 360.0 / (2.0 * PI) + 180.0;
                    direction = direction * PI / 180.0;
                } else {
                    // Matrix angle (0x6BFDAC): atan2(m12, m11) — node+136, node+120.
                    direction = std::atan2(pn.accumulated.m12, pn.accumulated.m11) * 360.0 / (2.0 * PI);
                    direction = direction * PI / 180.0;
                }

                // Angle spread (0x6BFDEC..0x6BFE34)
                double range = pn.activeSlot().prtRange;
                double spreadRandom = -range;
                if (range != -range) spreadRandom = (range + range) * random() - range;
                double totalAngle = direction + spreadRandom * PI / 180.0;
                double dirRad = totalAngle;

                // 3f. particleApplyZoomToVelocity (0x6BFE38..0x6BFEA0)
                double zoomScale = 1.0;
                if (inhVel >= 1 && inhVel <= 2) {
                    if (txOff != 0.0 || tyOff != 0.0) {
                        if (offZ != 0.0) {
                            double xyLen = std::sqrt(txOff * txOff + tyOff * tyOff);
                            zoomScale = xyLen / std::sqrt(offZ * offZ + xyLen * xyLen);
                        }
                    }
                }

                // Compute velocity + set position (0x6BFEC0..0x6BFF70)
                // Binary branches on coordinateMode (node+24), not inhVel.
                double velX = 0.0, velY = 0.0, velZ = 0.0;

                if (child->_runtime && !child->_runtime->nodes.empty()) {
                    auto &cr = child->_runtime->nodes[0];
                    if (pn.coordinateMode == 1) {
                        // 3D mode (0x6BFEB4..0x6BFEDC)
                        cr.accumulated.posX = txOff + pn.accumulated.posX;
                        cr.accumulated.posY = offZ + pn.accumulated.posY;
                        cr.accumulated.posZ = tyOff + pn.accumulated.posZ;
                        velX = zoomScale * speed * std::cos(dirRad);
                        velY = speed * 0.0;
                        velZ = zoomScale * speed * std::sin(dirRad);
                    } else if (pn.coordinateMode == 0) {
                        // 2D mode (0x6BFF14..0x6BFF3C)
                        cr.accumulated.posX = txOff + pn.accumulated.posX;
                        cr.accumulated.posY = tyOff + pn.accumulated.posY;
                        cr.accumulated.posZ = offZ + pn.accumulated.posZ;
                        velX = zoomScale * speed * std::cos(dirRad);
                        velY = zoomScale * speed * std::sin(dirRad);
                        velZ = speed * 0.0;
                    }

                    // 3h. Set flipX/Y (0x6BFF74..0x6BFFA4)
                    // Binary only writes + sets dirty when values differ.
                    if (cr.accumulated.flipX != pn.accumulated.flipX ||
                        cr.accumulated.flipY != pn.accumulated.flipY) {
                        cr.accumulated.flipX = pn.accumulated.flipX;
                        cr.accumulated.flipY = pn.accumulated.flipY;
                        cr.accumulated.dirty = true;
                    }

                    // 3i. Angle from prtA lerp — BEFORE zoom (0x6BFFA8..0x6C00AC)
                    // Binary order: angle lerp → angle computation → zoom lerp.
                    // Both call random(), so order matters for RNG sequence.
                    double aMin = pn.activeSlot().prtAmin;
                    double aMax = pn.activeSlot().prtA;
                    double prtAngle = aMin;
                    if (aMin != aMax) prtAngle = aMin + (aMax - aMin) * random();
                    // Binary uses PARENT flipX/Y for sign (0x6BFFD8..0x6BFFE0)
                    double childAngle = -prtAngle;
                    if (pn.accumulated.flipX == pn.accumulated.flipY) childAngle = prtAngle;

                    if (pn.particleInheritAngle) {
                        // Binary: v154 = dirRad + PI; if(!flipX) v154 = dirRad;
                        // then childAngle += v154 * 360 / (2*PI) (0x6BFFEC..0x6C0008)
                        double v154 = dirRad + PI;
                        if (!pn.accumulated.flipX) v154 = dirRad;
                        childAngle += v154 * 360.0 / (2.0 * PI);
                    }
                    while (childAngle < 0.0) childAngle += 360.0;
                    while (childAngle >= 360.0) childAngle -= 360.0;

                    // _directEdit check (0x6C0058): binary writes to player+464 and
                    // calls Player_initEmoteMotion if child._directEdit is true
                    if (child->_directEdit) {
                        // Emote mode angle path (0x6C0088..0x6C00AC)
                        double k = childAngle;
                        while (k < 0.0) k += 360.0;
                        while (k >= 360.0) k -= 360.0;
                        // player+464 = emote angle — not mapped in web port
                        // Player_initEmoteMotion(child, 2) — N/A for web
                    } else {
                        // Normal angle path (0x6C0060..0x6C0078)
                        if (cr.accumulated.angle != childAngle) {
                            cr.accumulated.dirty = true;
                            cr.accumulated.angle = childAngle;
                        }
                    }

                    // 3j. Zoom lerp — AFTER angle (0x6C00B0..0x6C00D8)
                    double zoom = pn.activeSlot().prtZmin;
                    if (zoom != pn.activeSlot().prtZ)
                        zoom = zoom + (pn.activeSlot().prtZ - zoom) * random();
                    if (cr.accumulated.scaleX != zoom || cr.accumulated.scaleY != zoom) {
                        cr.accumulated.dirty = true;
                        cr.accumulated.scaleX = zoom;
                        cr.accumulated.scaleY = zoom;
                    }

                    // 3j. particleApplyZoomToVelocity on child velocity (0x6C0110..0x6C0168)
                    // Binary gate: particleFlyDirection != 2 (0x6C0110)
                    if (pn.particleFlyDirection != 2) {
                        if (pn.particleApplyZoomToVelocity == 1) {
                            velX *= zoom; velY *= zoom; velZ *= zoom;
                        } else if (pn.particleApplyZoomToVelocity == 2 && zoom != 0.0) {
                            velX /= zoom; velY /= zoom; velZ /= zoom;
                        }
                    }
                }

                // 3k. Store velocity on child (0x6BFEF8..0x6BFF70)
                child->_cameraVelocityX = velX;
                child->_cameraVelocityY = velY;
                child->_cameraVelocityZ = velZ;

                // 3l. particleInheritVelocity==1: add parent delta/dt (0x6C0174..0x6C01AC)
                // Binary checks node+2176 (particleInheritVelocity), not particleFlyDirection.
                // Binary at 0x6C0178: checks dt != 0.0 (not dt > 0.0)
                if (pn.particleInheritVelocity == 1 && dt != 0.0) {
                    child->_cameraVelocityX += pn.deltaPosX / dt;
                    child->_cameraVelocityY += pn.deltaPosY / dt;
                    child->_cameraVelocityZ += pn.deltaPosZ / dt;
                }

                // 3m. Set cameraDamping (0x6C01B4)
                // Binary: node+2192 is one field for both decay and damping
                child->_cameraDamping = pn.particleAccelRatio;

                pn.addParticleChild(childVar);

                // Enforce maxNum per-particle (0x6C0218..0x6C0268)
                // Binary: signed comparison count > maxNum. When maxNum==0, ALL particles
                // are removed (size > 0 is always true). Only removes ONE per emission.
                if (pn.getParticleCount() > pn.particleMaxNum) {
                    pn.eraseParticleChild(0);
                }

                // Physics only when emitCount <= 1 (0x6C026C: CMP W20, #1; B.GT)
                if (emitCount <= 1) goto physics_step;
                // emitCount > 1: skip physics this frame, advance to next node.
                // Next frame will create another particle.
                continue;
                } // end creation block
            }
            } // end outer emitCount scope

        physics_step:
            // ====== sub_6C17A4: Physics stepping ======
            // Pass 1: Delete particles (0x6C1858..0x6C1950)
            // Binary uses TJS Array.erase with index-based iteration.
            // When erasing, count decreases and index stays (--i after erase).
            {
                int pCount = pn.getParticleCount();
                for (int ci = 0; ci < pCount; ++ci) {
                    auto *child = pn.getParticleChild(ci);
                    bool shouldErase = false;
                    if (!child || !child->_runtime || child->_runtime->nodes.empty()) {
                        shouldErase = true;
                    } else if (child->_allplaying) {
                        // Playing: only check bounds if particleDeleteOutside (0x6C1888)
                        if (pn.particleDeleteOutside) {
                            const double bMinX = child->_boundsMinX;
                            const double bMinY = child->_boundsMinY;
                            const double bMaxX = child->_boundsMaxX;
                            const double bMaxY = child->_boundsMaxY;
                            if (bMaxX >= bMinX && bMaxY >= bMinY) {
                                const double sw = static_cast<double>(_runtime->width);
                                const double sh = static_cast<double>(_runtime->height);
                                if (!(bMaxY > 0.0 && bMinX < sw && bMaxX > 0.0 && bMinY < sh)) {
                                    shouldErase = true;
                                }
                            }
                        }
                    } else {
                        // Not playing: always delete (0x6C1880)
                        shouldErase = true;
                    }
                    if (shouldErase) {
                        // Aligned to sub_6C17A4 (0x6C1930): TJS Array.erase(index)
                        pn.eraseParticleChild(ci);
                        --ci;
                        pCount = pn.getParticleCount();
                    }
                }
            }

            // Pass 2: Step each remaining child (0x6C1984..0x6C1A3C)
            // Binary at 0x6C1960: mesh combine parent propagation.
            {
                const int meshParentIdx = pn.meshCombineEnabled
                    ? static_cast<int>(pi) : pn.visibleAncestorIndex;
                const int pCount2 = pn.getParticleCount();
                for (int ci = 0; ci < pCount2; ++ci) {
                    auto *child = pn.getParticleChild(ci);
                    if (!child || !child->_runtime) continue;
                    child->_zFactor = _zFactor;
                    if (!child->_runtime->nodes.empty()) {
                        auto &cr = child->_runtime->nodes[0];
                        cr.parentClipIndex = pn.parentClipIndex;
                        cr.visibleAncestorIndex = meshParentIdx;
                        cr.forceVisible = pn.forceVisible;
                    }
                    // Aligned to libkrkr2.so particle-child step at 0x6BEF58+:
                    // binary does not lazy-build the child tree here; the tree
                    // was already built when the child's play/onFindMotion ran.
                    child->frameProgress(_frameLastTime);
                    if (!child->_runtime->nodes.empty()) {
                        child->updateLayers();
                    }
                }
            }
        } // for each nodeType==4
    }


} // namespace motion
