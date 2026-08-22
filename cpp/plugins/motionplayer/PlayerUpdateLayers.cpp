// PlayerUpdateLayers.cpp — updateLayers dispatcher and native phase order
// Split from Player.cpp for maintainability.
//
#include "PlayerInternal.h"
#include "MotionTraceWeb.h"

using namespace motion::internal;

namespace motion {
    // --- updateLayers: four-reference 3-phase pipeline ---
    // Operates on persistent MotionNode deque instead of re-walking PSB tree.
    void Player::updateLayers() {
        detail::motionTraceRecordUpdatePlayer(this);
        // Every reference clears the producer flag before any phase can set it
        // again. Post-draw only snapshots it; post-draw never clears it.
        _needsInternalAssignImages = false;
        auto &nodes = _nodes;
        // Player construction establishes the root deque entry. All four
        // references dereference it directly; an empty deque is not a
        // recoverable native state.
        std::string motionPath;
        if(detail::logoChainTraceEnabled()) {
            // Keep the optional Web diagnostic outside the native/default
            // data path: materializing this Variant as text may allocate.
            motionPath = matchedMotionPath();
        }
        const double currentTime = _clampedEvalTime;

        updateLayersPhase1_PreLoop(currentTime);
        updateLayersPhase2_MainLoop(currentTime);
        if(detail::logoChainTraceEnabledForPath(motionPath)) {
            const auto &root = nodes[0];
            detail::logoChainTraceLogf(
                motionPath, "updateLayers.phase1", "Player::updateLayers",
                currentTime,
                "rootPos=({:.3f},{:.3f},{:.3f}) cameraVel=({:.3f},{:.3f},{:.3f}) damping={:.6f} variableCount={}",
                root.accumulated.posX, root.accumulated.posY,
                root.accumulated.posZ, _cameraVelocityX, _cameraVelocityY,
                _cameraVelocityZ, _cameraDamping, _evalResultValues.size());
            for(const auto &[label, value] : _evalResultValues) {
                detail::logoChainTraceLogf(
                    motionPath, "updateLayers.phase1.var",
                    "Player::updateLayers",
                    currentTime, "label={} value={:.6f}",
                    detail::narrow(label), value);
            }
            for(const auto &node : nodes) {
                const auto &ac = node.accumulated;
                const auto &ls = node.accumulated;
                const auto &slot = node.activeSlot();
                const bool hasParent = node.parentIndex >= 0
                    && node.parentIndex < static_cast<int>(nodes.size());
                const auto &pc = hasParent ? nodes[node.parentIndex].accumulated
                                           : nodes[0].accumulated;
                detail::logoChainTraceLogf(
                    motionPath, "updateLayers.phase2.node",
                    "Player::updateLayers",
                    currentTime,
                    "nodeIndex={} label={} type={} parent={} src={} inherit=0x{:x} indep={} interp[x={:.3f},y={:.3f},ox={:.3f},oy={:.3f},opacity={:.6f},angle={:.3f},scale=({:.6f},{:.6f}),slant=({:.6f},{:.6f}),flip=({},{}) blend={}] local[pos=({:.3f},{:.3f},{:.3f}),angle={:.3f},scale=({:.6f},{:.6f}),slant=({:.6f},{:.6f}),flip=({},{}) opacity={}] parentAccum[pos=({:.3f},{:.3f},{:.3f}),scale=({:.6f},{:.6f}),slant=({:.6f},{:.6f}),matrix=({:.6f},{:.6f},{:.6f},{:.6f}),opacity={}] accum[pos=({:.3f},{:.3f},{:.3f}),scale=({:.6f},{:.6f}),slant=({:.6f},{:.6f}),matrix=({:.6f},{:.6f},{:.6f},{:.6f}),opacity={},active={},visible={}]",
                    node.index,
                    node.layerName.IsEmpty() ? std::string("<root>")
                                             : detail::narrow(node.layerName),
                    node.nodeType, node.parentIndex,
                    node.activeSlot().srcValue.IsEmpty()
                        ? std::string("<none>")
                        : detail::narrow(node.activeSlot().srcValue),
                    node.inheritFlags,
                    _independentLayerInherit ? 1 : 0,
                    ls.posX, ls.posY, slot.ox, slot.oy,
                    static_cast<double>(ls.opacity) / 255.0,
                    ls.angle, ls.scaleX, ls.scaleY, ls.slantX, ls.slantY,
                    ls.flipX ? 1 : 0, ls.flipY ? 1 : 0, slot.blendMode,
                    ls.posX, ls.posY, ls.posZ, ls.angle, ls.scaleX, ls.scaleY,
                    ls.slantX, ls.slantY, ls.flipX ? 1 : 0, ls.flipY ? 1 : 0,
                    ls.opacity,
                    pc.posX, pc.posY, pc.posZ, pc.scaleX, pc.scaleY,
                    pc.slantX, pc.slantY, pc.m11, pc.m12, pc.m21, pc.m22,
                    pc.opacity,
                    ac.posX, ac.posY, ac.posZ, ac.scaleX, ac.scaleY,
                    ac.slantX, ac.slantY, ac.m11, ac.m12,
                    ac.m21, ac.m22, ac.opacity,
                    ac.active ? 1 : 0, ac.visible ? 1 : 0);
            }
        }

        // The camera-constraint dirty state has now been consumed by every
        // non-root node. Clear it before the constraint pass publishes the
        // next frame's state.
        _cameraConstraintDirty_guess = false;

        // === PHASE 3: Post-loop processing ===
        // The order is shared by all four current reference binaries.
        updateLayersPhase3_CameraConstraint();
        updateLayersPhase3_VertexComputation();
        updateLayersPhase3_Visibility();
        updateLayersPhase3_CameraNode();
        updateLayersPhase3_ShapeAABB();
        updateLayersPhase3_ShapeGeometry();
        updateLayersPhase3_MotionSubNode();
        updateLayersPhase3_ParticleEmitter();
        updateLayersPhase3_ParticleSystem();
        updateLayersPhase3_AnchorNode();

        // === Post-loop cleanup ===
        // All four references clear the same player and per-node state here.

        // Clear the complete flags byte and accumulated dirty state for every
        // non-root node.
        for (size_t ci = 1; ci < nodes.size(); ++ci) {
            nodes[ci].flags = 0;
            nodes[ci].accumulated.dirty = false;
        }

        // Parameter mode is a one-update trigger consumed by the type-3 child
        // pass above. The value and every other parameter field remain live.
        for (auto &parameter : _parameterEntries) {
            parameter.mode = 0;
        }

        // Clear the first-update and parent-queue state only after the two
        // record ranges have been consumed and reset.
        _noUpdateYet = false;
        _queuing = false;
    }

} // namespace motion
