// PlayerUpdateLayerEval.cpp — updateLayers phase1 and phase2 evaluation
// Split from PlayerUpdateLayers.cpp for maintainability.
//
#include "PlayerUpdateLayersInternal.h"

namespace motion::internal {

    namespace {
        FrameContentState frameStateFromClipSlot(
            const detail::MotionNode::ClipSlot &slot,
            bool visible,
            int frameType = 0) {
            FrameContentState state;
            state.visible = visible && !slot.done;
            state.frameType = slot.frameIndex >= 0 ? slot.frameType : frameType;
            state.src = slot.src;
            state.srcList = slot.srcList;
            state.x = slot.x; state.y = slot.y; state.z = slot.z;
            state.ox = slot.ox; state.oy = slot.oy;
            state.width = slot.width; state.height = slot.height;
            state.opacity = slot.opacity; state.angle = slot.angle;
            state.scaleX = slot.scaleX; state.scaleY = slot.scaleY;
            state.slantX = slot.slantX; state.slantY = slot.slantY;
            state.flipX = slot.flipX; state.flipY = slot.flipY;
            state.blendMode = slot.blendMode;
            state.packedColors = slot.packedColors;
            state.ccc.x = slot.ccc.x; state.ccc.y = slot.ccc.y;
            state.acc.x = slot.acc.x; state.acc.y = slot.acc.y;
            state.zcc.x = slot.zcc.x; state.zcc.y = slot.zcc.y;
            state.scc.x = slot.scc.x; state.scc.y = slot.scc.y;
            state.occ.x = slot.occ.x; state.occ.y = slot.occ.y;
            state.cc.x = slot.cc.x; state.cc.y = slot.cc.y;
            state.cp.x = slot.cp.x; state.cp.y = slot.cp.y;
            state.cp.t = slot.cp.t;
            state.clipStartTime = slot.clipStartTime;
            state.motionDt = slot.motionDt;
            state.motionFlags = slot.motionFlags;
            state.motionDofst = slot.motionDofst;
            state.motionDocmpl = slot.motionDocmpl;
            state.motionTimeOffset = slot.motionTimeOffset;
            state.motionDtgt = slot.motionDtgt;
            state.prtTrigger = slot.prtTrigger;
            state.prtFmin = slot.prtFmin; state.prtF = slot.prtF;
            state.prtVmin = slot.prtVmin; state.prtV = slot.prtV;
            state.prtAmin = slot.prtAmin; state.prtA = slot.prtA;
            state.prtZmin = slot.prtZmin; state.prtZ = slot.prtZ;
            state.prtRange = slot.prtRange;
            state.hasTransformOrder = slot.hasTransformOrder;
            std::copy(slot.transformOrder, slot.transformOrder + 4,
                      state.transformOrder);
            state.action = slot.action;
            state.hasSync = slot.hasSync;
            return state;
        }

        void populateInterpolatedCacheFromState(
            detail::MotionNode &node,
            const FrameContentState &state) {
            node.interpolatedCache.src = state.src;
            node.interpolatedCache.srcList = state.srcList;
            node.interpolatedCache.width = state.width;
            node.interpolatedCache.height = state.height;
            node.interpolatedCache.opacity = state.opacity;
            node.interpolatedCache.x = state.x;
            node.interpolatedCache.y = state.y;
            node.interpolatedCache.z = state.z;
            node.interpolatedCache.ox = state.ox;
            node.interpolatedCache.oy = state.oy;
            node.interpolatedCache.angle = state.angle;
            node.interpolatedCache.scaleX = state.scaleX;
            node.interpolatedCache.scaleY = state.scaleY;
            node.interpolatedCache.slantX = state.slantX;
            node.interpolatedCache.slantY = state.slantY;
            node.interpolatedCache.flipX = state.flipX;
            node.interpolatedCache.flipY = state.flipY;
            node.interpolatedCache.blendMode = state.blendMode;
            node.interpolatedCache.packedColors = state.packedColors;
            node.interpolatedCache.hasTransformOrder = state.hasTransformOrder;
            if (state.hasTransformOrder) {
                std::copy(std::begin(state.transformOrder),
                          std::end(state.transformOrder),
                          node.interpolatedCache.transformOrder);
            }
            node.interpolatedCache.action = state.action;
            node.interpolatedCache.hasSync = state.hasSync;
            node.interpolatedCache.motionDt = state.motionDt;
            node.interpolatedCache.motionFlags = state.motionFlags;
            node.interpolatedCache.motionDofst = state.motionDofst;
            node.interpolatedCache.motionDocmpl = state.motionDocmpl;
            node.interpolatedCache.motionTimeOffset = state.motionTimeOffset;
            node.interpolatedCache.clipStartTime = state.clipStartTime;
            node.interpolatedCache.motionDtgt = state.motionDtgt;
            node.interpolatedCache.prtTrigger = state.prtTrigger;
            node.interpolatedCache.prtF = state.prtF;
            node.interpolatedCache.prtV = state.prtV;
            node.interpolatedCache.prtA = state.prtA;
            node.interpolatedCache.prtZ = state.prtZ;
            node.interpolatedCache.prtRange = state.prtRange;
            node.prtTrigger = state.prtTrigger;
            node.interpolatedCache.ccc_x = state.ccc.x;
            node.interpolatedCache.ccc_y = state.ccc.y;
            node.interpolatedCache.cp_x = state.cp.x;
            node.interpolatedCache.cp_y = state.cp.y;
            node.interpolatedCache.cp_t = state.cp.t;
            node.interpolatedCache.hasCpRotation = !state.cp.empty();
            copyPackedColorsToBytes(node.colorBytes, state.packedColors);
        }

        void writeTimelineStateLike_0x699AE4(
            detail::MotionNode &node,
            const FrameContentState &state,
            bool dirtyArg) {
            // Player_evaluateTimeline (0x699AE4) updates the node+1507+
            // payload, but leaves node+1504/+1505/+1506
            // (dirty/active/visible) to Player_updateLayers.
            populateTimelinePayloadFromFrameState(node.accumulated, state);
            populateTransformStateFromFrameState(node.localState, state);
            node.localState.dirty = dirtyArg || node.flags != 0;
            populateInterpolatedCacheFromState(node, state);
        }

        void markNodePayloadDirtyFromState(
            detail::MotionNode &node,
            const FrameContentState &state) {
            if (!state.debugEvaluated) {
                return;
            }
            const bool payloadChanged =
                !node.hasLastActivePayload ||
                node.lastActiveFrameIndex != state.debugActiveIndex ||
                node.lastActiveSrc != state.src ||
                node.lastActiveMotionFlags != state.motionFlags ||
                node.lastActiveMotionDtgt != state.motionDtgt;
            if (payloadChanged) {
                node.flags |= 0x01;
            }
            node.hasLastActivePayload = true;
            node.lastActiveFrameIndex = state.debugActiveIndex;
            node.lastActiveSrc = state.src;
            node.lastActiveMotionFlags = state.motionFlags;
            node.lastActiveMotionDtgt = state.motionDtgt;
        }

        void markNodeNoActiveFrame(detail::MotionNode &node) {
            node.hasLastActivePayload = true;
            node.lastActiveFrameIndex = -1;
            node.lastActiveSrc.clear();
            node.lastActiveMotionFlags = 0;
            node.lastActiveMotionDtgt.clear();
        }

        struct NodeTransformOrder {
            int order[4] = {0, 1, 2, 3};
            bool has = false;
        };

        NodeTransformOrder readNodeTransformOrder(
            const std::shared_ptr<const PSB::PSBDictionary> &nodeDict) {
            NodeTransformOrder out;
            if(auto toList = psbDictionaryList(nodeDict, "transformOrder")) {
                for(int i = 0; i < 4 && i < static_cast<int>(toList->size()); ++i) {
                    if(auto v = psbNumberValue((*toList)[i])) {
                        out.order[i] = static_cast<int>(*v);
                    }
                }
                out.has = true;
            }
            return out;
        }

        void applyNodeTransformOrder(FrameContentState &state,
                                     const NodeTransformOrder &order) {
            if(!order.has) {
                return;
            }
            std::copy(order.order, order.order + 4, state.transformOrder);
            state.hasTransformOrder = true;
        }

        void resetClipSlot(detail::MotionNode::ClipSlot &slot) {
            slot = detail::MotionNode::ClipSlot{};
        }

        bool populateClipSlotFromFrameLike_0x6926B4(
            detail::MotionNode &node,
            const std::shared_ptr<PSB::PSBList> &frames,
            int frameIndex,
            const NodeTransformOrder &transformOrder,
            detail::MotionNode::ClipSlot &slot,
            FrameContentState *outState = nullptr) {
            if(!frames || frameIndex < 0 ||
               frameIndex >= static_cast<int>(frames->size())) {
                resetClipSlot(slot);
                return false;
            }

            const auto frame = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                (*frames)[frameIndex]);
            if(!frame) {
                resetClipSlot(slot);
                slot.frameIndex = frameIndex;
                return false;
            }

            ParsedFrame parsed = parseFrame(frame, node.nodeType);
            FrameContentState state;
            state.debugEvaluated = true;
            state.debugActiveIndex = frameIndex;
            state.debugFrameATime = parsed.time;
            state.debugFrameAType = parsed.type;
            state.debugFrameAInvisible = parsed.invisible;
            state.debugFrameAOpacity = parsed.slot.opacity;
            state.debugFrameAScaleX = parsed.slot.scaleX;
            state.debugFrameAScaleY = parsed.slot.scaleY;
            state.debugFrameASrc = parsed.slot.src;
            state.frameType = parsed.type;
            state.clipStartTime = parsed.time;

            if(!parsed.invisible) {
                state = parsed.slot;
                state.visible = true;
                state.frameType = parsed.type;
                state.clipStartTime = parsed.time;
                state.debugEvaluated = true;
                state.debugActiveIndex = frameIndex;
                state.debugFrameATime = parsed.time;
                state.debugFrameAType = parsed.type;
                state.debugFrameAInvisible = false;
                state.debugFrameAOpacity = parsed.slot.opacity;
                state.debugFrameAScaleX = parsed.slot.scaleX;
                state.debugFrameAScaleY = parsed.slot.scaleY;
                state.debugFrameASrc = parsed.slot.src;
                applyNodeTransformOrder(state, transformOrder);
            }

            populateSlotFromState(slot, state);
            slot.frameIndex = frameIndex;
            slot.frameType = parsed.type;
            slot.crossfading = !parsed.invisible && parsed.interpolate;
            if(outState) {
                *outState = state;
            }
            return true;
        }

        int initialFrameIndexForTime(
            const std::shared_ptr<PSB::PSBList> &frames,
            double currentTime) {
            if(!frames || frames->size() == 0) {
                return -1;
            }

            int selected = 0;
            for(size_t index = 0; index < frames->size(); ++index) {
                const auto frame = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                    (*frames)[static_cast<int>(index)]);
                if(!frame) {
                    continue;
                }
                const double frameTime =
                    psbDictionaryNumber(frame, "time").value_or(0.0);
                if(currentTime < frameTime) {
                    selected = static_cast<int>(index == 0 ? 0 : index - 1);
                    break;
                }
                selected = static_cast<int>(index);
            }

            if(frames->size() > 1) {
                selected = std::min(
                    selected, static_cast<int>(frames->size()) - 2);
            }
            return std::max(selected, 0);
        }

        double frameSelectionTimeLike_0x6B7E44(
            const detail::MotionNode &node,
            double currentTime) {
            // sub_6B64AC/sub_6B7E44 read node+8->value when the node is
            // parameterized; otherwise they use the Player timeline time.
            if(node.parameterizeIndex >= 0 && node.parameterEntry != nullptr) {
                return node.parameterEntry->value;
            }
            return currentTime;
        }

        bool initializeNodeTimelineSlotsLike_0x6B64AC(
            detail::MotionNode &node,
            const std::shared_ptr<PSB::PSBList> &frames,
            double currentTime,
            const NodeTransformOrder &transformOrder) {
            if(!frames || frames->size() == 0) {
                resetClipSlot(node.slots[0]);
                resetClipSlot(node.slots[1]);
                node.activeSlotIndex = 0;
                markNodeNoActiveFrame(node);
                return false;
            }

            const double selectionTime =
                frameSelectionTimeLike_0x6B7E44(node, currentTime);
            const int activeIndex = initialFrameIndexForTime(frames, selectionTime);
            node.activeSlotIndex = 0;
            populateClipSlotFromFrameLike_0x6926B4(
                node, frames, activeIndex, transformOrder, node.slots[0]);
            populateClipSlotFromFrameLike_0x6926B4(
                node, frames, activeIndex + 1, transformOrder, node.slots[1]);
            node.flags |= 0x01;
            node.hasTimelineEvalRatio = false;
            return true;
        }

        FrameContentState frameStateFromNodeSlots(
            const detail::MotionNode &node,
            double currentTime) {
            const auto &active = node.activeSlot();
            const auto &other = node.otherSlot();
            FrameContentState state =
                frameStateFromClipSlot(active, !active.done, active.frameType);
            state.debugEvaluated = active.frameIndex >= 0;
            state.debugActiveIndex = active.frameIndex;
            state.debugFrameATime = active.clipStartTime;
            state.debugFrameAType = active.frameType;
            state.debugFrameAInvisible = active.done;
            state.debugFrameAOpacity = active.opacity;
            state.debugFrameAScaleX = active.scaleX;
            state.debugFrameAScaleY = active.scaleY;
            state.debugFrameASrc = active.src;
            state.clipStartTime = active.clipStartTime;

            if(other.frameIndex >= 0) {
                state.debugNextIndex = other.frameIndex;
                state.debugFrameBTime = other.clipStartTime;
                state.debugFrameBType = other.frameType;
                state.debugFrameBInvisible = other.done;
                state.debugFrameBOpacity = other.opacity;
                state.debugFrameBScaleX = other.scaleX;
                state.debugFrameBScaleY = other.scaleY;
                state.debugFrameBSrc = other.src;
            }

            if(active.crossfading && other.frameIndex >= 0) {
                const double duration = other.clipStartTime - active.clipStartTime;
                if(duration > 0.0) {
                    state.debugInterpT = std::clamp(
                        (currentTime - active.clipStartTime) / duration,
                        0.0, 1.0);
                    state.debugInterpolated =
                        state.debugInterpT > 0.0 && !other.done;
                }
            }
            return state;
        }
    }

    MOTIONPLAYER_NOINLINE FrameContentState
    advanceNodeFrameSelectionLike_0x6926B4(detail::MotionNode &node,
                                           double currentTime) {
        const auto frames = psbDictionaryList(node.psbNode, "frameList");
        if(!frames || frames->size() == 0) {
            node.activeSlot().done = true;
            node.activeSlot().crossfading = false;
            node.otherSlot().done = true;
            markNodeNoActiveFrame(node);
            return {};
        }

        const NodeTransformOrder transformOrder =
            readNodeTransformOrder(node.psbNode);
        const double selectionTime =
            frameSelectionTimeLike_0x6B7E44(node, currentTime);
        if(node.activeSlot().frameIndex < 0 && node.otherSlot().frameIndex < 0) {
            initializeNodeTimelineSlotsLike_0x6B64AC(
                node, frames, currentTime, transformOrder);
        }

        const int lastForwardFrameIndex =
            static_cast<int>(frames->size()) - 2;
        while(node.otherSlot().frameIndex >= 0 &&
              node.activeSlot().frameIndex < lastForwardFrameIndex &&
              selectionTime >= node.otherSlot().clipStartTime) {
            node.activeSlotIndex ^= 1;
            const int nextIndex = node.activeSlot().frameIndex + 1;
            populateClipSlotFromFrameLike_0x6926B4(
                node, frames, nextIndex, transformOrder, node.otherSlot());
            node.flags |= 0x01;
            node.hasTimelineEvalRatio = false;
        }

        while(node.activeSlot().frameIndex > 0 &&
              selectionTime < node.activeSlot().clipStartTime) {
            const int previousIndex = node.activeSlot().frameIndex - 1;
            node.activeSlotIndex ^= 1;
            populateClipSlotFromFrameLike_0x6926B4(
                node, frames, previousIndex, transformOrder, node.activeSlot());
            node.flags |= 0x01;
            node.hasTimelineEvalRatio = false;
        }

        FrameContentState state = frameStateFromNodeSlots(node, selectionTime);
        if(!state.debugEvaluated) {
            markNodeNoActiveFrame(node);
            return state;
        }
        node.currentFrameType = state.frameType;
        markNodePayloadDirtyFromState(node, state);
        return state;
    }

    MOTIONPLAYER_NOINLINE bool
    evaluateTimelineLike_0x699AE4(detail::MotionNode &node,
                                  bool dirtyArg,
                                  double currentTime) {
        const bool dirty = dirtyArg || node.flags != 0;
        auto &active = node.activeSlot();
        auto &other = node.otherSlot();

        if(active.done) {
            return dirty;
        }

        if(!active.crossfading || other.done) {
            if(!dirty) {
                return false;
            }
            FrameContentState state =
                frameStateFromClipSlot(active, true, node.currentFrameType);
            writeTimelineStateLike_0x699AE4(node, state, true);
            node.timelineEvalRatio = 0.0;
            node.hasTimelineEvalRatio = true;
            return true;
        }

        if(node.timelineParameterOverride) {
            currentTime = node.timelineParameterValue;
        }

        const double duration = other.clipStartTime - active.clipStartTime;
        double ratio = duration != 0.0
            ? (currentTime - active.clipStartTime) / duration
            : 0.0;
        ratio = std::clamp(ratio, 0.0, 1.0);

        const bool ratioChanged =
            !node.hasTimelineEvalRatio ||
            std::fabs(node.timelineEvalRatio - ratio) > 1.0e-12;
        if(!dirty && !ratioChanged) {
            return false;
        }
        node.timelineEvalRatio = ratio;
        node.hasTimelineEvalRatio = true;

        FrameContentState state =
            frameStateFromClipSlot(active, true, node.currentFrameType);
        if(ratio > 0.0) {
            FrameContentState next =
                frameStateFromClipSlot(other, !other.done, node.currentFrameType);
            if(next.src.empty()) {
                next.src = state.src;
            }
            state = interpolateSlots(state, next, node.coordinateMode, ratio);
            state.visible = true;
            state.frameType = node.currentFrameType;
        }

        writeTimelineStateLike_0x699AE4(node, state, true);
        return true;
    }
}

namespace motion {
    // Phase 1: Camera velocity, root evaluation, variable interpolation
    void Player::updateLayersPhase1_PreLoop(double currentTime) {
        auto &nodes = _runtime->nodes;
        // === PHASE 1: Pre-loop setup ===

        // Camera velocity → root delta block (0x6BB360..0x6BB3DC).
        // Writes node+1584 (delta.dirty) and node+1592/+1600/+1608 (delta pos).
        {
            auto &rootNode = nodes[0];
            if (_cameraVelocityX != 0.0) {
                rootNode.delta.dirty = true;
                rootNode.delta.posX += _frameLastTime * _cameraVelocityX;
            }
            if (_cameraVelocityY != 0.0) {
                rootNode.delta.dirty = true;
                rootNode.delta.posY += _frameLastTime * _cameraVelocityY;
            }
            if (_cameraVelocityZ != 0.0) {
                rootNode.delta.dirty = true;
                rootNode.delta.posZ += _frameLastTime * _cameraVelocityZ;
            }
            // Camera friction (0x6BB3E0..0x6BB428)
            if (_cameraDamping != 1.0 && _frameLastTime > 0.0) {
                const double dampFactor = std::pow(_cameraDamping,
                                                    _frameLastTime / 60.0);
                _cameraVelocityX *= dampFactor;
                _cameraVelocityY *= dampFactor;
                _cameraVelocityZ *= dampFactor;
            }
        }

        // Step 1: Save previous positions for delta calculation
        for (auto &n : nodes) {
            n.prevPosX = n.accumulated.posX;
            n.prevPosY = n.accumulated.posY;
            n.prevPosZ = n.accumulated.posZ;
        }

        // Step 2: Evaluate root node (index 0)
        auto &root = nodes[0];
        {
            FrameContentState rootState;
            const bool syntheticRoot = !root.psbNode;
            if (root.psbNode) {
                rootState = evaluateLayerContent(root.psbNode, currentTime,
                                                 root.nodeType);
            } else {
                // Aligned to Player_buildNodeTree (0x6B51F0): node 0 is a
                // synthetic root. Player_updateLayers @ 0x6BB4D4 copies its
                // existing delta block directly to accumulated state.
                root.delta.flipX = _rootFlipX;
                rootState.visible = root.delta.visibleOverride;
                rootState.opacity = std::clamp(
                    static_cast<double>(root.delta.opacity) / 255.0,
                    0.0, 1.0);
                rootState.x = root.delta.posX;
                rootState.y = root.delta.posY;
                rootState.z = root.delta.posZ;
                rootState.angle = root.delta.angle;
                rootState.scaleX = root.delta.scaleX;
                rootState.scaleY = root.delta.scaleY;
                rootState.slantX = root.delta.slantX;
                rootState.slantY = root.delta.slantY;
                rootState.flipX = root.delta.flipX;
                rootState.flipY = root.delta.flipY;
                rootState.blendMode = 16;
            }
            // Populate root active clip slot
            populateSlotFromState(root.activeSlot(), rootState);
            root.currentFrameType = rootState.frameType;
            populateTransformStateFromFrameState(root.localState, rootState);
            root.localState.dirty = root.delta.dirty;

            if (!syntheticRoot) {
                const bool deltaDirty = root.delta.dirty;
                const double deltaPosX = root.delta.posX;
                const double deltaPosY = root.delta.posY;
                const double deltaPosZ = root.delta.posZ;
                populateDeltaStateFromFrameState(root.delta, rootState);
                root.delta.posX = deltaPosX;
                root.delta.posY = deltaPosY;
                root.delta.posZ = deltaPosZ;
                root.delta.flipX = rootState.flipX ^ _rootFlipX;
                root.delta.dirty = deltaDirty;
            }

            // Aligned to libkrkr2.so 0x6BB4E0..0x6BB4E8:
            //   memcpy(root+1504, root+1584, 0x50); *(root+1584) = 0;
            copyDeltaBlockToAccum(root.accumulated, root.delta);
            root.accumulated.blendMode = root.localState.blendMode;
            root.delta.dirty = false;
            // Cache interpolated data for rendering
            root.interpolatedCache.src = rootState.src;
            root.interpolatedCache.width = rootState.width;
            root.interpolatedCache.height = rootState.height;
            root.interpolatedCache.opacity = rootState.opacity;
            root.interpolatedCache.x = rootState.x;
            root.interpolatedCache.y = rootState.y;
            root.interpolatedCache.z = rootState.z;
            root.interpolatedCache.ox = rootState.ox;
            root.interpolatedCache.oy = rootState.oy;
            root.interpolatedCache.angle = rootState.angle;
            root.interpolatedCache.scaleX = rootState.scaleX;
            root.interpolatedCache.scaleY = rootState.scaleY;
            root.interpolatedCache.slantX = rootState.slantX;
            root.interpolatedCache.slantY = rootState.slantY;
            root.interpolatedCache.flipX = root.delta.flipX;
            root.interpolatedCache.flipY = rootState.flipY;
            root.interpolatedCache.blendMode = rootState.blendMode;
            root.interpolatedCache.packedColors = rootState.packedColors;
            copyPackedColorsToBytes(root.colorBytes, rootState.packedColors);
            root.interpolatedCache.hasTransformOrder = rootState.hasTransformOrder;
            if (rootState.hasTransformOrder) {
                std::copy(std::begin(rootState.transformOrder),
                          std::end(rootState.transformOrder),
                          root.interpolatedCache.transformOrder);
            }
            root.interpolatedCache.action = rootState.action;
            root.interpolatedCache.hasSync = rootState.hasSync;
            root.interpolatedCache.prtTrigger = rootState.prtTrigger;
            root.interpolatedCache.prtF = rootState.prtF;
            root.interpolatedCache.prtV = rootState.prtV;
            root.interpolatedCache.prtA = rootState.prtA;
            root.interpolatedCache.prtZ = rootState.prtZ;
            root.interpolatedCache.prtRange = rootState.prtRange;

            refreshSourceGeometryFromSourceName(root, _runtime->activeMotion,
                                                rootState.src);

            // Step 3: Build root local 2x2 matrix via sub_699940
            // Reuse applyLocalTransform logic but on raw 2x2
            Affine2x3 rootAffine = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
            applyLocalTransform(rootAffine, root);
            root.accumulated.m11 = rootAffine[0];
            root.accumulated.m21 = rootAffine[1];
            root.accumulated.m12 = rootAffine[2];
            root.accumulated.m22 = rootAffine[3];
        }

        // --- sub_6BBE20: Variable interpolation (pre-loop) ---
        // Aligned to 0x6BBE20. Interpolates variable values and binds to nodes.
        // In libkrkr2.so this operates on a 160-byte item deque (player+1312).
        // Each variable is interpolated then bound to nodes via sub_6C4668.
        //
        // sub_6C4668 binding: resolves variable name to a source entry in
        // player+264 map, then updates child Player timeline parameters for
        // nodeType=3 and nodeType=4 nodes. In our architecture, variable values
        // are stored in _variableValues and exposed via getVariable()/setVariable()
        // TJS API. The binding to child Players happens implicitly when child
        // Players re-evaluate their timelines.
        if (_runtime->activeMotion) {
            const auto &varFrames = _runtime->activeMotion->variableFrames;
            for (const auto &[label, frames] : varFrames) {
                if (frames.empty()) continue;
                // User-set value takes precedence
                if (_variableValues.find(label) != _variableValues.end()) continue;
                // Default: use first frame value
                writeEvalResultValueLike_0x6C4668(label, 0,
                                                  frames.front().value);
            }
            // Aligned to sub_6C4668: refresh parameter entries directly. This
            // intentionally does not call public setVariable() on child players.
            for (const auto &[label, value] : _variableValues) {
                bindParameterValueLike_0x6C4668(label, 0, value);
            }
        }

    }

    // Phase 2: Main node evaluation loop (non-root nodes)
    void Player::updateLayersPhase2_MainLoop(double currentTime) {
        auto &nodes = _runtime->nodes;
        const std::string motionPath = _runtime->activeMotion
            ? _runtime->activeMotion->path
            : std::string();
        for (size_t i = 1; i < nodes.size(); ++i) {
            auto &node = nodes[i];

            const int origParentIdx = node.parentIndex;
            int parentIdx = node.parentIndex;
            int walkSteps = 0;
            while (parentIdx > 0 && parentIdx < static_cast<int>(nodes.size())) {
                if ((nodes[parentIdx].inheritFlags & 0x00400000) == 0) {
                    break;
                }
                parentIdx = nodes[parentIdx].parentIndex;
                ++walkSteps;
            }
            if (parentIdx < 0 || parentIdx >= static_cast<int>(nodes.size())) {
                parentIdx = 0;
            }
            const auto &parent = nodes[parentIdx];

            if (detail::logoChainTraceEnabled(_runtime->activeMotion)) {
                const auto &parentNode = nodes[parentIdx];
                detail::logoChainTraceLogf(
                    motionPath, "updateLayers.phase2.parent_lookup", "0x6BB598",
                    currentTime,
                    "nodeIndex={} label={} type={} inheritFlags=0x{:x} origParentIdx={} resolvedParentIdx={} parentLabel={} parentType={} parentInheritFlags=0x{:x} walkSteps={} independentLayerInherit={}",
                    node.index,
                    node.layerName.empty() ? std::string("<root>")
                                           : node.layerName,
                    node.nodeType,
                    static_cast<unsigned>(node.inheritFlags),
                    origParentIdx,
                    parentIdx,
                    parentNode.layerName.empty() ? std::string("<root>")
                                                 : parentNode.layerName,
                    parentNode.nodeType,
                    static_cast<unsigned>(parentNode.inheritFlags),
                    walkSteps,
                    _independentLayerInherit ? 1 : 0);
            }

            auto state = advanceNodeFrameSelectionLike_0x6926B4(node,
                                                                 currentTime);
            if (detail::logoChainTraceEnabled(_runtime->activeMotion)
                && state.debugEvaluated) {
                detail::logoChainTraceLogf(
                    motionPath, "updateLayers.phase2.framesel",
                    "0x6926B4", currentTime,
                    "nodeIndex={} label={} type={} activeIndex={} nextIndex={} frameA[time={:.3f},type={},invisible={},src={},opacity={:.6f},scale=({:.6f},{:.6f})] frameB[time={:.3f},type={},invisible={},src={},opacity={:.6f},scale=({:.6f},{:.6f})] t={:.6f} interpolated={} final[src={},opacity={:.6f},scale=({:.6f},{:.6f})]",
                    node.index,
                    node.layerName.empty() ? std::string("<root>")
                                           : node.layerName,
                    node.nodeType,
                    state.debugActiveIndex,
                    state.debugNextIndex,
                    state.debugFrameATime,
                    state.debugFrameAType,
                    state.debugFrameAInvisible ? 1 : 0,
                    state.debugFrameASrc.empty() ? std::string("<none>")
                                                : state.debugFrameASrc,
                    state.debugFrameAOpacity,
                    state.debugFrameAScaleX,
                    state.debugFrameAScaleY,
                    state.debugFrameBTime,
                    state.debugFrameBType,
                    state.debugFrameBInvisible ? 1 : 0,
                    state.debugFrameBSrc.empty() ? std::string("<none>")
                                                : state.debugFrameBSrc,
                    state.debugFrameBOpacity,
                    state.debugFrameBScaleX,
                    state.debugFrameBScaleY,
                    state.debugInterpT,
                    state.debugInterpolated ? 1 : 0,
                    state.src.empty() ? std::string("<none>") : state.src,
                    state.opacity,
                    state.scaleX,
                    state.scaleY);
            }

            const bool forceDirty = false;
            const bool needGround = node.groundCorrection;
            const bool parentDirty = parent.accumulated.dirty;
            const bool deltaDirty = node.delta.dirty;
            // Player_updateLayers @ 0x6BB5E0 passes only the explicit
            // dirty sources as a2; node+44 is folded in inside
            // Player_evaluateTimeline itself.
            const bool timelineDirtyArg =
                forceDirty || needGround || parentDirty || deltaDirty;

            node.timelineParameterOverride = false;
            node.timelineParameterValue = 0.0;
            if (node.parameterizeIndex >= 0) {
                auto *parameterEntry = resolveNodeParameterEntry(*_runtime, node);
                if (parameterEntry != nullptr && parameterEntry->mode != 0) {
                    node.timelineParameterOverride = true;
                    node.timelineParameterValue = parameterEntry->value;
                }
            }

            if (!evaluateTimelineLike_0x699AE4(
                    node, timelineDirtyArg, currentTime)) {
                continue;
            }

            refreshSourceGeometryFromSourceName(
                node, _runtime->activeMotion, node.interpolatedCache.src);

            // Player_updateLayers clears node+1584 after evaluateTimeline but
            // keeps the active/visible override bytes intact.
            neutralizeDeltaTransformOverrides(node.delta);
            node.delta.dirty = false;

            if (node.activeSlot().done) {
                node.accumulated = parent.accumulated;
                const bool copiedDirty = node.accumulated.dirty;
                node.accumulated.active = false;
                node.accumulated.dirty = copiedDirty ? true : (node.flags != 0);
                node.accumulated.visible =
                    node.accumulated.visible && node.delta.visibleOverride;
                node.accumulated.m11 = parent.accumulated.m11;
                node.accumulated.m21 = parent.accumulated.m21;
                node.accumulated.m12 = parent.accumulated.m12;
                node.accumulated.m22 = parent.accumulated.m22;
                continue;
            }

            if (node.activeSlot().hasSync) {
                node.accumulated.dirty = parent.accumulated.dirty;
                node.accumulated.active = parent.accumulated.active;
                node.accumulated.visible = parent.accumulated.visible;
                node.accumulated.flipX = parent.accumulated.flipX;
                node.accumulated.flipY = parent.accumulated.flipY;
                node.accumulated.posX = parent.accumulated.posX;
                node.accumulated.posY = parent.accumulated.posY;
                node.accumulated.posZ = parent.accumulated.posZ;
                node.accumulated.angle = parent.accumulated.angle;
                node.accumulated.scaleX = parent.accumulated.scaleX;
                node.accumulated.scaleY = parent.accumulated.scaleY;
                node.accumulated.slantX = parent.accumulated.slantX;
                node.accumulated.slantY = parent.accumulated.slantY;
                node.accumulated.opacity = parent.accumulated.opacity;
                const bool postDirty = node.accumulated.dirty;
                const bool postVisible = node.accumulated.visible;
                node.accumulated.active = false;
                node.accumulated.dirty = postDirty ? true : (node.flags != 0);
                node.accumulated.visible =
                    postVisible ? node.delta.visibleOverride : false;
                node.accumulated.m11 = parent.accumulated.m11;
                node.accumulated.m21 = parent.accumulated.m21;
                node.accumulated.m12 = parent.accumulated.m12;
                node.accumulated.m22 = parent.accumulated.m22;
                continue;
            }

            {
                const bool visResolved = node.delta.visibleOverride
                    ? parent.accumulated.visible
                    : false;
                node.accumulated.dirty = true;
                node.accumulated.flipX ^= node.delta.flipX;
                node.accumulated.flipY ^= node.delta.flipY;
                node.accumulated.visible = visResolved;
                node.accumulated.active =
                    visResolved && node.delta.activeOverride;
            }
            node.accumulated.scaleX *= node.delta.scaleX;
            node.accumulated.scaleY *= node.delta.scaleY;
            node.accumulated.slantX += node.delta.slantX;
            node.accumulated.slantY += node.delta.slantY;
            node.accumulated.opacity =
                node.delta.opacity * node.accumulated.opacity / 255;
            node.accumulated.posX += node.delta.posX;
            node.accumulated.posY += node.delta.posY;
            node.accumulated.posZ += node.delta.posZ;
            node.accumulated.angle += node.delta.angle;

            if (parent.meshType != 0) {
                sub_69AE74_meshDeform(parent, node);
            }

            {
                const double localX = node.accumulated.posX;
                const double localY = node.accumulated.posY;
                const double localZ = node.accumulated.posZ;
                if (parent.coordinateMode != 0) {
                    const double worldX = parent.accumulated.m11 * localX
                        + parent.accumulated.m12 * localZ;
                    const double worldZ = parent.accumulated.m21 * localX
                        + parent.accumulated.m22 * localZ;
                    node.accumulated.posX = worldX + parent.accumulated.posX;
                    node.accumulated.posY = localY + parent.accumulated.posY;
                    node.accumulated.posZ = worldZ + parent.accumulated.posZ;
                } else {
                    const double worldX = parent.accumulated.m11 * localX
                        + parent.accumulated.m12 * localY;
                    const double worldY = parent.accumulated.m21 * localX
                        + parent.accumulated.m22 * localY;
                    node.accumulated.posX = worldX + parent.accumulated.posX;
                    node.accumulated.posY = worldY + parent.accumulated.posY;
                    node.accumulated.posZ = localZ + parent.accumulated.posZ;
                }
            }

            sub_6BAA10_groundCorrection(node, parent);

            {
                const int v46 = node.inheritFlags;
                if ((v46 & 0x400) != 0) {
                    node.accumulated.opacity =
                        parent.accumulated.opacity * node.accumulated.opacity / 255;
                } else if (!_independentLayerInherit) {
                    const auto &rootNode = nodes[0];
                    node.accumulated.opacity =
                        rootNode.accumulated.opacity
                        * node.accumulated.opacity / 255;
                }
            }

            const int flags = node.inheritFlags;
            if ((~flags & 0x1FC) == 0) {
                Affine2x3 localAffine = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
                applyLocalTransform(localAffine, node);
                const double lm11 = localAffine[0];
                const double lm21 = localAffine[1];
                const double lm12 = localAffine[2];
                const double lm22 = localAffine[3];
                node.accumulated.m11 =
                    parent.accumulated.m11 * lm11
                    + parent.accumulated.m12 * lm21;
                node.accumulated.m21 =
                    parent.accumulated.m21 * lm11
                    + parent.accumulated.m22 * lm21;
                node.accumulated.m12 =
                    parent.accumulated.m11 * lm12
                    + parent.accumulated.m12 * lm22;
                node.accumulated.m22 =
                    parent.accumulated.m21 * lm12
                    + parent.accumulated.m22 * lm22;
                node.accumulated.flipX ^= parent.accumulated.flipX;
                node.accumulated.flipY ^= parent.accumulated.flipY;
                node.accumulated.angle += parent.accumulated.angle;
                node.accumulated.scaleX *= parent.accumulated.scaleX;
                node.accumulated.scaleY *= parent.accumulated.scaleY;
                node.accumulated.slantX += parent.accumulated.slantX;
                node.accumulated.slantY += parent.accumulated.slantY;
            } else {
                if (flags & 0x004)
                    node.accumulated.flipX ^= parent.accumulated.flipX;
                if (flags & 0x008)
                    node.accumulated.flipY ^= parent.accumulated.flipY;
                if (flags & 0x010)
                    node.accumulated.angle += parent.accumulated.angle;
                if (flags & 0x020)
                    node.accumulated.scaleX *= parent.accumulated.scaleX;
                if (flags & 0x040)
                    node.accumulated.scaleY *= parent.accumulated.scaleY;
                if (flags & 0x080)
                    node.accumulated.slantX += parent.accumulated.slantX;
                if (flags & 0x100)
                    node.accumulated.slantY += parent.accumulated.slantY;

                if (_independentLayerInherit) {
                    Affine2x3 localAffine = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
                    applyLocalTransform(localAffine, node);
                    node.accumulated.m11 = localAffine[0];
                    node.accumulated.m21 = localAffine[1];
                    node.accumulated.m12 = localAffine[2];
                    node.accumulated.m22 = localAffine[3];
                } else {
                    const auto &rootNode = nodes[0];
                    if (flags & 0x004)
                        node.accumulated.flipX ^= rootNode.accumulated.flipX;
                    if (flags & 0x008)
                        node.accumulated.flipY ^= rootNode.accumulated.flipY;
                    if (flags & 0x010)
                        node.accumulated.angle -= rootNode.accumulated.angle;
                    if (flags & 0x020)
                        node.accumulated.scaleX /= rootNode.accumulated.scaleX;
                    if (flags & 0x040)
                        node.accumulated.scaleY /= rootNode.accumulated.scaleY;
                    if (flags & 0x080)
                        node.accumulated.slantX -= rootNode.accumulated.slantX;
                    if (flags & 0x100)
                        node.accumulated.slantY -= rootNode.accumulated.slantY;

                    Affine2x3 localAffine = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
                    applyLocalTransform(localAffine, node);

                    if (flags & 0x004)
                        node.accumulated.flipX ^= rootNode.accumulated.flipX;
                    if (flags & 0x008)
                        node.accumulated.flipY ^= rootNode.accumulated.flipY;
                    if (flags & 0x010)
                        node.accumulated.angle += rootNode.accumulated.angle;
                    if (flags & 0x020)
                        node.accumulated.scaleX *= rootNode.accumulated.scaleX;
                    if (flags & 0x040)
                        node.accumulated.scaleY *= rootNode.accumulated.scaleY;
                    if (flags & 0x080)
                        node.accumulated.slantX += rootNode.accumulated.slantX;
                    if (flags & 0x100)
                        node.accumulated.slantY += rootNode.accumulated.slantY;

                    const double lm11 = localAffine[0];
                    const double lm21 = localAffine[1];
                    const double lm12 = localAffine[2];
                    const double lm22 = localAffine[3];
                    node.accumulated.m11 =
                        rootNode.accumulated.m11 * lm11
                        + rootNode.accumulated.m12 * lm21;
                    node.accumulated.m21 =
                        rootNode.accumulated.m21 * lm11
                        + rootNode.accumulated.m22 * lm21;
                    node.accumulated.m12 =
                        rootNode.accumulated.m11 * lm12
                        + rootNode.accumulated.m12 * lm22;
                    node.accumulated.m22 =
                        rootNode.accumulated.m21 * lm12
                        + rootNode.accumulated.m22 * lm22;
                }
            }

            if (detail::logoChainTraceEnabled(_runtime->activeMotion)) {
                detail::logoChainTraceLogf(
                    motionPath, "updateLayers.phase2.accum_final", "0x6BBB6C",
                    currentTime,
                    "nodeIndex={} label={} type={} parentIdx={} parentLabel={} state[visible={},evaluated={},opacity={:.3f},scale=({:.3f},{:.3f}),localPos=({:.3f},{:.3f},{:.3f})] parentAccum[pos=({:.3f},{:.3f},{:.3f}),m=({:.3f},{:.3f},{:.3f},{:.3f}),opacity={},visible={}] accum[pos=({:.3f},{:.3f},{:.3f}),m=({:.3f},{:.3f},{:.3f},{:.3f}),scale=({:.3f},{:.3f}),opacity={},visible={},active={}]",
                    node.index,
                    node.layerName.empty() ? std::string("<root>")
                                           : node.layerName,
                    node.nodeType,
                    parentIdx,
                    parent.layerName.empty() ? std::string("<root>")
                                             : parent.layerName,
                    state.visible ? 1 : 0,
                    state.debugEvaluated ? 1 : 0,
                    state.opacity,
                    state.scaleX, state.scaleY,
                    state.x, state.y, state.z,
                    parent.accumulated.posX, parent.accumulated.posY,
                    parent.accumulated.posZ,
                    parent.accumulated.m11, parent.accumulated.m21,
                    parent.accumulated.m12, parent.accumulated.m22,
                    parent.accumulated.opacity,
                    parent.accumulated.visible ? 1 : 0,
                    node.accumulated.posX, node.accumulated.posY,
                    node.accumulated.posZ,
                    node.accumulated.m11, node.accumulated.m21,
                    node.accumulated.m12, node.accumulated.m22,
                    node.accumulated.scaleX, node.accumulated.scaleY,
                    node.accumulated.opacity,
                    node.accumulated.visible ? 1 : 0,
                    node.accumulated.active ? 1 : 0);
            }
        }
    }


} // namespace motion
