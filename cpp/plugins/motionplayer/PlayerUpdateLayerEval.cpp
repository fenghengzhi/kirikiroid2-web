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
            const NodeTransformOrder &transformOrder,
            std::vector<detail::MotionEvent> *pendingEvents = nullptr) {
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

            // 砖5/洞2 (initNodeTimeline tail): Aligned with libkrkr2.so
            // Player_initNodeTimeline tail @0x6B674C. After parsing slot[0]
            // (node+320) and slot[1], the binary fires a per-node onAction when the
            // seed landed EXACTLY on slot[0]'s frame AND that frame carries an
            // action:
            //   if ( v8 == *(double*)(node+328)            // slot[0].time
            //        && (*(_BYTE*)(node+342) & 4) != 0 )   // slot[0].mask & 0x40000
            //     Player_pushActionEvent_guess(player, &node_label, node+608);
            // where v8 = frameSelectionTimeLike_0x6B7E44(node) (the seed target,
            // node+8 ? *(node+8)+40 : player+456), node+328 = slot[0]+8 = time
            // (= ClipSlot.clipStartTime), the (node+342 & 4) byte = slot[0].mask
            // & 0x40000 (parseFrame @0x6928EC sets slot+288='act' ONLY under that
            // bit), and node+608 = slot[0]+288 = the action variant. The pushed
            // event is {type=0(ACTION), param1=node label, param2=action} (same
            // contract as Player_pushActionEvent_guess @0x6B63C0 -> onAction). The
            // 0x40000 mask gate is mirrored here by !action.empty(): slot.action is
            // populated exactly when the active frame carries one, matching the
            // existing per-node fireNodeAction gate used by the advance/rewind
            // inline seeks (advanceNodeFrameSelectionLike_0x6926B4). Fires for
            // slot[0] (node.activeSlotIndex == 0 after the seed above).
            if(pendingEvents) {
                const detail::MotionNode::ClipSlot &slot0 = node.slots[0];
                if(selectionTime == slot0.clipStartTime &&
                   !slot0.action.empty()) {
                    pendingEvents->push_back(
                        {0, node.layerName, slot0.action, false});
                }
            }
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

    // The shared per-node seek primitive behind THREE binary functions that all
    // run an identical forward / corrective-backward seek over node.slots[0/1]
    // and an identical state-establish tail, differing only in WHICH direction
    // loops run and whether per-node onAction fires:
    //   • Player_advanceNodeFrames  @0x6B7E44 (parameterized): forward + corrective
    //       backward, NO events  → doForward=true,  doBackward=true,  events=null
    //   • forward inline seek       @0x6B73DC (advanceRootAndNodes, non-param):
    //       forward only, WITH events → doForward=true, doBackward=false
    //   • backward inline seek      @0x6BA1CC (rewindRootAndNodes, non-param):
    //       backward only, WITH events → doForward=false, doBackward=true
    // The forward/backward loops below are gated by doForward/doBackward so each
    // binary variant gets exactly its loop set; the frames-empty handling, the
    // init seed, and the tail are shared (byte-identical across all three).
    // (doBackward off for the forward inline is the binary's reality — 0x6B73DC
    // has NO corrective-backward; its loop-wrap repositioning is done earlier by
    // Player_reseekTimelineCursors' node re-seed @0x6B91B0, ported as
    // reseekNodeTimelineSlotsLike_0x6B91B0. Empirically the backward loop never
    // fires for the logo cases, so the split is logo-identical.)
    MOTIONPLAYER_NOINLINE FrameContentState
    advanceNodeFrameSelectionLike_0x6926B4(
        detail::MotionNode &node, double currentTime,
        std::vector<detail::MotionEvent> *pendingEvents,
        bool doForward, bool doBackward) {
        // 砖5/洞2: fire a per-node onAction(label, action) for each crossed
        // action frame (slot mask bit 0x40000 / content["act"], stored in
        // ClipSlot.action). Aligned to the per-node sub_6B638C (0x6B638C) push
        // in Player_advanceRootAndNodes (0x6B6ADC, push @0x6B74E4, fires the
        // CROSSED `other` slot) and Player_rewindRootAndNodes (0x6B9A3C, push
        // @0x6BA26C, fires the just-entered prev slot).
        //
        // param1 = *(node+0). VERIFIED via Player_initNodeFields (0x6B3C78):
        // node+0 is seeded at 0x6B3DF4 with the object pointer of the
        // PropGet("label") result (sub_529524 @0x6B3DC8) — i.e. node+0 IS the
        // node's "label" string variant. The inline push (advance 0x6B74BC:
        // `LDR X8,[X20]; STUR X8,[var_70]` then AddRef) copies exactly that
        // label variant as record.a. node.layerName is this same PSB "label"
        // (NodeTree.cpp:108). So param1 = node.layerName is the faithful port.
        // NOTE: param1 is NOT the layer dispatch object (node.tjsLayerObject /
        // *(node+0)+16); that misreading would diverge from the binary.
        // param2 = slot+288 (advance push arg `node+0x120`) = ClipSlot.action.
        // Residual ABI-level nuance (the binary's record.a variant tag vs a
        // tvtString of the same text) is below the source-structure tier per
        // CLAUDE.md (no byte/representation match required).
        const auto fireNodeAction =
            [&](const detail::MotionNode::ClipSlot &slot) {
            if(pendingEvents && !slot.action.empty()) {
                pendingEvents->push_back(
                    {0, node.layerName, slot.action, false});
            }
        };

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
            // No pendingEvents here: this is a port-local lazy first-seed safety
            // net for the inline advance/rewind seeks (0x6B73DC / 0x6BA1CC), which
            // do their OWN inline parse and have their OWN action push in the seek
            // loop (fireNodeAction below). The binary's inline seeks never invoke
            // Player_initNodeTimeline (0x6B64AC) — that is reseekTimelineCursors'
            // node re-seed (0x6B91B0) — so its tail onAction must NOT fire here, or
            // it would double-push with the seek loop's own action.
            initializeNodeTimelineSlotsLike_0x6B64AC(
                node, frames, currentTime, transformOrder);
        }

        const int lastForwardFrameIndex =
            static_cast<int>(frames->size()) - 2;
        while(doForward &&
              node.otherSlot().frameIndex >= 0 &&
              node.activeSlot().frameIndex < lastForwardFrameIndex &&
              selectionTime >= node.otherSlot().clipStartTime) {
            // 0x6B6ADC: fire the crossed frame (`other`) before the swap.
            fireNodeAction(node.otherSlot());
            node.activeSlotIndex ^= 1;
            const int nextIndex = node.activeSlot().frameIndex + 1;
            populateClipSlotFromFrameLike_0x6926B4(
                node, frames, nextIndex, transformOrder, node.otherSlot());
            node.flags |= 0x01;
            node.hasTimelineEvalRatio = false;
        }

        while(doBackward &&
              node.activeSlot().frameIndex > 0 &&
              selectionTime < node.activeSlot().clipStartTime) {
            const int previousIndex = node.activeSlot().frameIndex - 1;
            node.activeSlotIndex ^= 1;
            populateClipSlotFromFrameLike_0x6926B4(
                node, frames, previousIndex, transformOrder, node.activeSlot());
            // 0x6B9A3C: rewind fires the just-entered (previous) frame.
            fireNodeAction(node.activeSlot());
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

    // ==================================================================
    // Player_advanceNodeFrames @ 0x6B7E44  (P7 convergence step 1 of 3)
    // ------------------------------------------------------------------
    // The binary's per-node 2-slot ping-pong frame seek for PARAMETERIZED
    // nodes (the caller branch `if (*(node+8)) advanceNodeFrames(node,player)`
    // at Player_advanceRootAndNodes 0x6B73B4/0x6B73D4, LABEL_104). It forward-
    // seeks (with corrective backward seek) the node's active parsed-frame slot
    // toward the node's CHILD eval time, then merges both slots + gated
    // findSource.
    //
    // D-A1 RESOLUTION (decompiled, NOT trusted from stale comments):
    //   The binary reads the seek target at 0x6B7E90 as
    //       v6 = *(double *)(*(node+8) + 40)
    //   node+8 is seeded by Player_initNodeFields (0x6B3EA0): when the PSB
    //   "parameterize" value has variant type 4 (integer), node+8 =
    //   player_paramTable[idx] (56-byte stride entry, 0x6B3E90/0x6B3EA0);
    //   otherwise node+8 = 0. So node+8 is a *parameter-table entry*, NOT a
    //   child Player. Offset +40 of that 56-byte entry is the interpolated
    //   parameter VALUE: sub_6B1718 (the param-entry builder) writes the
    //   entry's fields at base..base+48 and stores the eased value at
    //   `*(double*)(v6-16)` = entry+40 (0x6B19E4). Cross-checked against
    //   Player_initNodeTimeline (0x6B64AC, 0x6B6500):
    //       v7 = (*(node+8)) ? (double*)(*(node+8)+40) : (double*)(player+456)
    //   i.e. parameterized -> entry+40 value, else player+456 (clampedEvalTime).
    //   The live MotionParameterEntry maps entry+40 -> ::value (RuntimeSupport.h
    //   builds the 56-byte entry with `value` as the eased field). Therefore
    //   *(node+8)+40 == node.parameterEntry->value == exactly what
    //   frameSelectionTimeLike_0x6B7E44 returns for a parameterized node
    //   (PlayerUpdateLayerEval.cpp). CONFIRMED EQUAL: child+40 == the live
    //   parameterEntry->value. No behavior change for parameterized nodes.
    //
    // FAITHFUL BODY = the shared seek + state-establish tail, events suppressed.
    // ------------------------------------------------------------------
    // 0x6B7E44 and the NON-parameterized inline sibling path inside
    // Player_advanceRootAndNodes (0x6B73B0, LABEL_88 @0x6B72BC..0x6B7338) execute
    // the SAME source-level node-frame-advance: identical forward seek
    // (0x6B7F14 / break `cur.fi >= count-2 || target < other.time`), identical
    // corrective backward seek, and a field-for-field identical tail —
    // `node+44 = 1` (content-established) → 2× gated Player_mergeFrameContent
    // (node+346/+882) → gated Motion_Player_findSource(node+200, player,
    // activeSlot+356/src, +348/icon) which writes the active slot's
    // done(node+200 +0) / src(texture, +24). The ONLY binary difference: the
    // parameterized path (0x6B7E44) fires NO per-node onAction events; the
    // inline path pushes them in its seek loop (slot mask 0x40000). Verified by
    // decompile (0x6B7E44 full body + 0x6B72BC..0x6B7338 disasm) 2026-06-05.
    //
    // The live faithful reproduction of that shared seek+tail is
    // advanceNodeFrameSelectionLike_0x6926B4 (proven correct: logo 93/243).
    // So the faithful 0x6B7E44 = that shared helper with pendingEvents = nullptr.
    //
    // WHY NOT a literal standalone transcription of 0x6B7E44's loops?  The binary
    // backward seek (0x6B7FA4) calls Player_parseFrame(other, cur.fi - 1) with NO
    // lower-bound guard, relying on the data invariant target >= frame0.time so
    // it never actually parses a negative index. The live frame parser
    // populateClipSlotFromFrameLike_0x6926B4 instead RESETS the slot to a default
    // {done=true, src=empty} on a negative index — so a literal transcription
    // diverged: for m2logo's parameterized "レイヤ1" node it produced active-slot
    // {done=true, src=empty} where the shared helper produces {done=false,
    // src=non-empty} (seek landed activeSlotIndex=0/frame0 vs 1/frame1). That
    // {done=true,src=empty} flipped the child-motion play gates in sub_6BE0C0
    // (0x6BE31C done / 0x6BE364 src), leaving a child Player playing-but-motionless
    // (_speed=1 / null motion / totalFrames=0) which spun the progress_inner
    // loop-wrap (PlayerFrameProgress.cpp:2100) forever → m2logo CI hang. The
    // shared helper carries the data-invariant guards (active.fi > 0 backward,
    // other.fi >= 0 forward) that faithfully encode the binary invariant the live
    // parser requires, so it lands the seek identically and avoids the reset.
    // (Differential pinned via per-case xp3 + proc_exit bitmask probes 2026-06-05.)
    // ------------------------------------------------------------------
    MOTIONPLAYER_NOINLINE void
    advanceNodeFramesLike_0x6B7E44(detail::MotionNode &node, double currentTime) {
        // 0x6B7E90 seek target = *(node+8)+40 = parameterEntry->value. The shared
        // helper recomputes the selection time internally via
        // frameSelectionTimeLike_0x6B7E44, which returns parameterEntry->value
        // ONLY when (parameterizeIndex >= 0 && parameterEntry != nullptr); for a
        // node that is parameterEntry-routed but has parameterizeIndex < 0 it
        // falls back to `currentTime`. The caller gates on parameterEntry != null
        // (the binary node+8 split) but NOT on parameterizeIndex, so we MUST
        // forward the real clampedEvalTime — passing 0.0 here seeks such a node to
        // t=0 and reintroduces the m2logo hang. Matches the green baseline routing
        // exactly: advanceNodeFrameSelectionLike(node, clampedEvalTime, nullptr).
        // pendingEvents = nullptr: the parameterized path fires no onAction.
        advanceNodeFrameSelectionLike_0x6926B4(node, currentTime, nullptr);
    }

    // M1/P7 step-1: read-only slot consumer for the updateLayers pass.
    // After progressSeekNodeSlotsLike_0x6C106C has positioned node.slots[0/1],
    // updateLayers reads them here — no seek. Mirrors the read half of the old
    // inline seek: uses the same per-node selection time
    // (frameSelectionTimeLike_0x6B7E44) so the debug interpolation ratio matches.
    // Aligned to libkrkr2.so: Player_updateLayers (0x6BB33C) feeds the slots to
    // Player_evaluateTimeline (0x699AE4) without cursor-stepping.
    MOTIONPLAYER_NOINLINE FrameContentState
    readNodeFrameSlotsLike_0x699AE4(detail::MotionNode &node,
                                    double currentTime) {
        const double selectionTime =
            frameSelectionTimeLike_0x6B7E44(node, currentTime);
        return frameStateFromNodeSlots(node, selectionTime);
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
    // M1/P7 step-1: progress-pass per-node frame seek (cursor stepping).
    //
    // Aligned to libkrkr2.so Player_progress_inner (0x6C106C). In the binary the
    // progress core walks the node-deque (player+200, 2632B stride) and, for
    // each node whose child-timeline pointer node+8 is non-null, calls
    // Player_advanceNodeFrames (0x6B7E44) at 0x6C1264 / 0x6C130C; the inline
    // loop bodies start at deque index 1 (`for(j=1; ...; ++j)`), i.e. the root
    // node (index 0) is NOT seeked here (it is reseeded by
    // Player_advanceRootAndNodes / the root-content snapshot path). The seek
    // FILLS each node's two parsed-frame slots (node+320 / node+856) that the
    // SEPARATE Player_updateLayers pass (0x6BB33C) then reads via
    // Player_evaluateTimeline (0x699AE4) at 0x6BB5F0.
    //
    // The live MotionNode::ClipSlot slots[2] ARE the binary's node+320/+856
    // slots, and advanceNodeFrameSelectionLike_0x6926B4 already seeks them using
    // the same per-node selection time (frameSelectionTimeLike_0x6B7E44 picks
    // node.parameterEntry->value for parameterized nodes, else clampedEvalTime =
    // player+456). So this driver is a faithful hoist: it runs the existing live
    // seek in the progress pass (before updateLayers), restoring the binary's
    // two-pass data flow with NO second slot copy.
    //
    // STEP-1 SCOPE: forward-only. The binary's reverse path (deltaTime<0 ->
    // Player_rewindRootAndNodes 0x6B9A3C) and the full reseek
    // (Player_reseekTimelineCursors 0x6B86C8 firstFrame/loop-wrap) are not yet
    // wired; advanceNodeFrameSelectionLike already contains a corrective
    // backward sub-loop (PlayerUpdateLayerEval.cpp:390) that covers small
    // rewinds, so forward + corrective-backward is the step-1 coverage. Full
    // reverse rewind = TODO (P7 step-2).
    void Player::progressSeekNodeSlotsLike_0x6C106C(double clampedEvalTime,
                                                    bool forward) {
        auto &nodes = _nodes;
        if (nodes.empty()) {
            return;
        }
        // Player_progress_inner node-deque loop starts at index 1 (0x6C1288:
        // `for(j=1; ...)`). Root node (index 0) takes the root-stream path, not
        // the per-node advanceNodeFrames seek.
        //
        // UPPER BOUND = `i < nodes.size()` — DO NOT change to `i+1 < size()`.
        // The binary exit is `dequeSize - 1 <= idx` (0x6C12D8 / disasm 0x6B7390
        // SUB X9,X9,#1 / 0x6B7398 B.LS), the term being the libstdc++
        // std::deque::size() expansion. It LOOKS like `idx < size-1` (末节点排
        // 除), but the binary deque carries ONE EXTRA trailing past-the-end slot
        // beyond the real node count, so `dequeSize - 1 == realNodeCount ==
        // _nodes.size()`. The `-1` cancels that trailing slot; the walk covers
        // ALL real non-root nodes [1, realNodeCount-1], identical to the local
        // `i < _nodes.size()`. Cross-checked vs Player_buildNodeTree @0x6B51F0
        // (0x6B531C): its INDEPENDENT deque-size term ALSO subtracts 1 and its
        // `v9=1; while(++v9 >= dequeSize-1)` loop reads real node fields (node+28
        // type==12), confirming dequeSize = realNodeCount + 1, NOT 1:1.
        // RUNTIME-PROVEN (2026-06-06): `i+1 < size()` regressed yuzulogo by 468
        // mismatches (last real node, layer_index 24, stopped being seeked);
        // baseline `i < size()` is byte-exact vs the libkrkr2.so oracle. The
        // 2026-06-06 audit's "_nodes is 1:1 with the binary deque" claim is WRONG
        // — the deque has a trailing sentinel the local container lacks.
        for (size_t i = 1; i < nodes.size(); ++i) {
            detail::MotionNode &node = nodes[i];
            // Player_advanceNodeFrames (0x6B7E44) seeks this node's two slots to
            // the node's selection time. The live seek writes node.slots[0/1],
            // node.activeSlotIndex, node.flags |= 1 and clears
            // node.hasTimelineEvalRatio — exactly the state Player_evaluateTimeline
            // (0x699AE4) consumes in the updateLayers pass. Return value is only
            // used for tracing in the collapsed model; here we discard it (the
            // slots are the real output, mirroring the binary).
            // 砖5/洞2: per-node onAction(label, action) on crossed action frames
            // (slot mask bit 0x40000 -> ClipSlot.action), matching the inline
            // sub_6B638C push inside Player_advanceRootAndNodes (0x6B6ADC,
            // LABEL_86 / push @0x6B74E4) and Player_rewindRootAndNodes (0x6B9A3C,
            // LABEL_76 / push @0x6BA26C).
            //
            // node+8 GATING (0x6B73B0/0x6B73D4 advance, 0x6BA1A8/0x6BA1C4 rewind):
            // both node loops branch
            //     if (*(node+8)) { Player_advanceNodeFrames(node, player); continue; }
            // i.e. PARAMETERIZED nodes (node+8 = parameterEntry != 0) take the
            // child-advance path (Player_advanceNodeFrames 0x6B7E44), which seeks
            // its two slots but contains NO action-mask check and NO
            // pushActionEvent call — so parameterized nodes fire NO per-node
            // onAction. Only NON-parameterized nodes (node+8 == 0) run the inline
            // seek that pushes the event. node+8 == parameterEntry (MotionNode.h:71,
            // node init 0x6B3EA0). Gate accordingly: parameterized -> no events.
            //
            // P7 convergence step 1: route the PARAMETERIZED branch through the
            // dedicated faithful reproduction of Player_advanceNodeFrames
            // (0x6B7E44) — the exact function the binary caller branches to at
            // LABEL_104 (`if (*(node+8)) { Player_advanceNodeFrames(node,player);
            // continue; }`). Non-parameterized nodes (node+8 == 0) keep the
            // inline 2-slot seek (0x6B73D0..0x6B7338) modelled by
            // advanceNodeFrameSelectionLike_0x6926B4, which fires the per-node
            // onAction events the inline path pushes. This mirrors the binary's
            // node+8 split precisely instead of conflating both into one helper
            // gated only by the selection time.
            if(node.parameterEntry != nullptr) {
                // node+8 != 0 -> Player_advanceNodeFrames (0x6B7E44). NO events.
                // Same forward+corrective-backward seek in both play directions.
                advanceNodeFramesLike_0x6B7E44(node, clampedEvalTime);
                continue;
            }
            // node+8 == 0 -> single-direction inline seek WITH events: forward
            // inline (0x6B73DC, advanceRootAndNodes) for forward playback,
            // backward inline (0x6BA1CC, rewindRootAndNodes) for reverse. The
            // binary runs exactly one direction's loop here; the forward inline
            // has NO corrective-backward (loop-wrap repositioning is done by
            // reseekNodeTimelineSlotsLike_0x6B91B0 in reseekTimelineCursors).
            if(forward) {
                advanceNodeFrameForwardInlineSeekLike_0x6B73DC(
                    node, clampedEvalTime, &_pendingEvents);
            } else {
                advanceNodeFrameBackwardInlineSeekLike_0x6BA1CC(
                    node, clampedEvalTime, &_pendingEvents);
            }
        }
    }

    // Player_reseekTimelineCursors node-deque re-seed loop @0x6B91B0 (STEP 4).
    // ABSOLUTE two-slot re-seed of every non-root node to its target-bracketing
    // frame, independent of the prior cursor:
    //   for(m=1; m<dequeSize-1; ++m) Player_initNodeTimeline(player, node[m]);
    // Player_initNodeTimeline (0x6B64AC) parses slot[0]=frame(v19) +
    // slot[1]=frame(v19+1) (v19=min(scan(target),count-2)), merges both, sets
    // activeSlotIndex(+1392)=0 and seeded(+44)=1; selection target per-node is
    // (*(node+8)) ? *(node+8)+40 : player+456 — computed inside
    // initializeNodeTimelineSlotsLike_0x6B64AC via frameSelectionTimeLike_0x6B7E44.
    // This is what makes the loop-wrap path's subsequent FORWARD-ONLY inline seek
    // sufficient (no corrective-backward needed). The binary breaks at
    // m == dequeSize-1; but the binary deque carries one trailing past-the-end
    // slot beyond the real node count, so `dequeSize - 1 == realNodeCount ==
    // _nodes.size()` and the proven node-walk range is `i < nodes.size()`
    // (matches progressSeekNodeSlotsLike). UPPER BOUND verified vs fresh-decompile
    // 0x6B9200 (`... - 1 <= m` -> break) — the same libstdc++ deque::size()-1 exit
    // term as all node-walks; the `-1` cancels the trailing sentinel, NOT the last
    // real node. (See progressSeekNodeSlotsLike_0x6C106C for the full
    // dequeSize=realNodeCount+1 cross-check; do NOT change to `i+1 < size()`.)
    // The 0x6B9234 pruneHM3 / 0x6B9650 aux-list tail is housekeeping (inert on
    // node slots) and stays DEFERRED. (Only reached at loop-wrap, which the logo
    // cases never hit — empirically reseekTimelineCursors is never called for
    // m2logo.)
    void Player::reseekNodeTimelineSlotsLike_0x6B91B0(double targetTime) {
        auto &nodes = _nodes;
        for (size_t i = 1; i < nodes.size(); ++i) {
            detail::MotionNode &node = nodes[i];
            const auto frames = psbDictionaryList(node.psbNode, "frameList");
            const auto transformOrder = readNodeTransformOrder(node.psbNode);
            // 砖5/洞2: Player_initNodeTimeline (0x6B64AC) fires a per-node onAction
            // in its tail @0x6B674C when the re-seed lands exactly on an action
            // frame; pass _pendingEvents so reseekTimelineCursors' node re-seed
            // (the binary's @0x6B91B0 loop) reproduces those onAction pushes.
            initializeNodeTimelineSlotsLike_0x6B64AC(
                node, frames, targetTime, transformOrder, &_pendingEvents);
        }
    }

    // 砖5/洞1: Player_preProgressDirtyNodes (0x6B6878) — progress_inner's first
    // step (called at 0x6C10AC, before the firstFrame/cursor logic). For each
    // node (deque idx >= 1) whose forceVisible (node+1996) != 0 and whose
    // emoteEdit dict (node+1980) has "modified" set: clear the flag and rebuild
    // the node's two timeline slots via initializeNodeTimelineSlotsLike_0x6B64AC
    // (= Player_initNodeTimeline_guess 0x6B64AC at 0x6B6A1C).
    //
    // INERT IN THE WEB PORT: no emote direct-edit path sets "modified" on the
    // emoteEdit dict, so no node is ever rebuilt here. Ported for call-chain
    // restoration (CLAUDE.md): this is progress_inner's documented first call,
    // previously absent from the live path. The binary's clear via
    // PropSet(512,"modified",0) (0x6B6A08) is unreachable while "modified" is
    // never set, and the live PSB dicts are read-only, so the clear is omitted;
    // if a modified-setter is later ported, the clear MUST be added here or the
    // node would be rebuilt every frame.
    void Player::preProgressDirtyNodesLike_0x6B6878() {
        // UPPER BOUND = `i < _nodes.size()` — do NOT change to `i+1 < size()`.
        // Fresh-decompile 0x6B6920 (`... - 1 <= v2` -> return; v2 starts at 1) is
        // the SAME libstdc++ std::deque::size()-1 exit term as the other 3
        // node-walks (progress_inner 0x6C12D8 / advanceRootAndNodes 0x6B7398 /
        // reseek 0x6B9200). The `-1` cancels the binary deque's trailing
        // past-the-end sentinel (dequeSize = realNodeCount + 1), so the walk
        // covers all real nodes [1, realNodeCount-1] == `i < _nodes.size()`. (Full
        // cross-check + runtime proof in progressSeekNodeSlotsLike_0x6C106C.)
        for (size_t i = 1; i < _nodes.size(); ++i) {
            detail::MotionNode &node = _nodes[i];
            if (node.forceVisible == 0 || !node.emoteEditDict) { // node+1996/+1980
                continue;
            }
            // sub_6636D4(emoteEdit, "modified") — 0x6B69C0.
            const bool modified =
                psbDictionaryNumber(node.emoteEditDict, "modified").value_or(0.0)
                    != 0.0;
            if (!modified) {
                continue;
            }
            // Player_initNodeTimeline_guess(player, node) — 0x6B6A1C.
            const auto frames = psbDictionaryList(node.psbNode, "frameList");
            const NodeTransformOrder transformOrder =
                readNodeTransformOrder(node.psbNode);
            // 砖5/洞2: the dirty-node rebuild is a direct Player_initNodeTimeline
            // (0x6B64AC) call, so its tail @0x6B674C onAction push fires here too;
            // pass _pendingEvents to reproduce it.
            initializeNodeTimelineSlotsLike_0x6B64AC(
                node, frames, _clampedEvalTime, transformOrder, &_pendingEvents);
        }
    }

    // Phase 1: Camera velocity, root evaluation, variable interpolation
    void Player::updateLayersPhase1_PreLoop(double currentTime) {
        auto &nodes = _nodes;
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

            refreshSourceGeometryFromSourceName(root, _activeMotion,
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
        // are stored in _evalResultValues and exposed via getVariable()/setVariable()
        // TJS API. The binding to child Players happens implicitly when child
        // Players re-evaluate their timelines.
        if (_activeMotion) {
            const auto &varFrames = _activeMotion->variableFrames;
            for (const auto &[label, frames] : varFrames) {
                if (frames.empty()) continue;
                // User-set value takes precedence (HM2 is ttstr-keyed; widen).
                if (_evalResultValues.find(detail::widen(label)) != _evalResultValues.end()) continue;
                // Default: use first frame value
                writeEvalResultValueLike_0x6C4668(label, 0,
                                                  frames.front().value);
            }
            // Aligned to sub_6C4668: refresh parameter entries directly. This
            // intentionally does not call public setVariable() on child players.
            for (const auto &[label, value] : _evalResultValues) {
                // HM2 keys are ttstr; the std::string bindParameterValue
                // overload takes a narrow label.
                bindParameterValueLike_0x6C4668(detail::narrow(label), 0, value);
            }
        }

    }

    // Phase 2: Main node evaluation loop (non-root nodes)
    void Player::updateLayersPhase2_MainLoop(double currentTime) {
        auto &nodes = _nodes;
        const std::string motionPath = _activeMotion
            ? _activeMotion->path
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

            if (detail::logoChainTraceEnabled(_activeMotion)) {
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

            // M1/P7 step-1: the per-node frame seek no longer runs here. In
            // libkrkr2.so Player_updateLayers (0x6BB33C) does NOT cursor-step;
            // the node's two parsed-frame slots (node+320/+856) were already
            // filled by the progress pass (Player_progress_inner 0x6C106C ->
            // Player_advanceNodeFrames 0x6B7E44, hoisted to
            // Player::progressSeekNodeSlotsLike_0x6C106C). Here we only READ the
            // already-positioned live ClipSlots — exactly what the binary's
            // Player_evaluateTimeline (0x699AE4) consumes. frameStateFromNodeSlots
            // builds the same FrameContentState (and trace fields) from the live
            // slots without re-seeking.
            auto state = readNodeFrameSlotsLike_0x699AE4(node, currentTime);
            if (detail::logoChainTraceEnabled(_activeMotion)
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
                auto *parameterEntry = resolveNodeParameterEntry(*this, node);
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
                node, _activeMotion, node.interpolatedCache.src);

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

            if (detail::logoChainTraceEnabled(_activeMotion)) {
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
