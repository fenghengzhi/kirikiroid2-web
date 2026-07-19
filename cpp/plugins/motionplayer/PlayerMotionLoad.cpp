// PlayerMotionLoad.cpp — motion load, variable init, and node tree build
// Split out for maintainability.
//
#include "PlayerInternal.h"
#include "MotionDispatch.h"
#include "MotionTraceWeb.h"

using namespace motion::internal;

namespace motion {
    namespace {
        bool shouldEmitMotionLoadDiag(std::uint32_t seq) {
            return seq <= 200 || (seq % 100) == 0;
        }

        const char *diagBool(bool v) {
            return v ? "true" : "false";
        }
    }

    // Child-motion call sites use this native helper as the source-level
    // Player_play entry. Keep one state machine: Player_play @0x6B21E8 owns
    // pending +768, then Player_playImpl @0x6B2284 owns load/init and the live
    // +976/+984 slots.
    void Player::onFindMotion(ttstr name, int flags) {
        (void)playMotionLike_0x6B2284(std::move(name), flags);
    }

    // Aligned to libkrkr2.so Player_initVariables (0x6CD750). Called
    // synchronously from the play path after Player_buildNodeTree (0x6B51F0)
    // and before the (flags & Chain) playback-state gate. Reads the PSB
    // "variable" array (from Player+528) and pushes one
    // VariableLabelScope (the 160B var-track item) per dict entry onto the
    // Player+1296 deque:
    //   cascadeKey (item+0)  <- scope present ? scope+"::"+label : label
    //       (binary 0x6CDAEC..0x6CDBB4: v25 = sub_A1359C(scope, "::");
    //        item+0 = sub_A1359C(v25, label) — concat, NOT scope-suffix split)
    //   frameSource (item+24) <- entry["label"] raw value (sub_A0FB64 @0x6CDA98) —
    //       the keyframe list stream③ iterates; SAME value as item+0
    //   value (item+16)      <- 0  (interpolated later; HM4 reads it)
    //   cursor (item+8)      <- 0
    //   slot[0/1].typeZeroFlag <- 1  (binary item+68/+124 seeded =1 @0x6CD9C0)
    void Player::initVariables() {
        _variableLabelScopes.clear();
        if(_motionContentVariant.Type() != tvtObject) {
            return;
        }

        // Player_initVariables @0x6CD750 reads Player+528 directly through TJS
        // dispatch. Do not substitute MotionSnapshot::moduleValue here: that
        // compatibility value owns the decoded file root, not necessarily the
        // selected motion content returned by ResourceManager.findMotion.
        const tTJSVariant variableList = detail::motionPropGet(
            _motionContentVariant, TJS_W("variable"));
        if(variableList.Type() == tvtVoid) {
            return;
        }

        const tjs_int count = detail::motionPropGetCount(variableList);
        for(tjs_int i = 0; i < count; ++i) {
            const tTJSVariant item = detail::motionPropGetByNum(variableList, i);
            detail::VariableLabelScope entry;

            // 0x6CD9F0 stores the converted label string at item+0, while
            // 0x6CDA58..0x6CDA98 performs a second PropGet and CopyRef into
            // item+24. Preserve both accesses and the independent Variant owner.
            entry.cascadeKey = detail::motionPropGetString(
                item, TJS_W("label"));
            entry.frameSource = detail::motionPropGet(item, TJS_W("label"));

            const tTJSVariant scope = detail::motionPropGet(
                item, TJS_W("scope"));
            if(scope.Type() != tvtVoid) {
                entry.cascadeKey = ttstr(scope) + TJS_W("::") +
                                   entry.cascadeKey;
            }

            // value/cursor default 0; slot gate flags default 1 (struct
            // in-class initialisers mirror the binary memset+seed).
            _variableLabelScopes.push_back(std::move(entry));
        }
    }

    void Player::resetNodeTreeForBuildLike_0x6B56F8() {
        if(false) {
            return;
        }
        detail::ensureRootNodeLike_0x6CED30(*this);
        for(size_t i = 1; i < _nodes.size(); ++i) {
            auto &node = _nodes[i];
            // P3-B (d): release via Player+992 dispatch FuncCall (binary
            //   resetAndReleaseNodes@0x6B56F8), not a native call.
            dispatchReleaseLayerId(node.layerId1);
            dispatchReleaseLayerId(node.layerId2);
        }
        detail::resetNodeTreeKeepRootLike_0x6B56F8(*this);
    }

    void Player::inheritChildPlayerStateLike_0x6B3C78(detail::MotionNode &node) {
        if(auto *child = node.getChildPlayer()) {
            // P3-B: the child already received the parent's RM dispatch at
            //   construction (binary 0x6b43cc: `Player_ctor(child, parent+992)`),
            //   so no native-RM copy is needed here. This site only sets the
            //   parent link (binary 0x6b43dc: `*(child+8) = parent`).
            child->setParentPlayerLike_0x6B1ABC(this);
            child->_tjsRandomGenerator = _tjsRandomGenerator;
            child->_findMotionContextVariant = _findMotionContextVariant;
            if(true) {
                detail::ensureRootNodeLike_0x6CED30(*child);
                auto &root = child->_nodes.front();
                root.coordinateMode = node.coordinateMode;
                for(int i = 0; i < 4; ++i) {
                    root.transformOrder[i] = node.transformOrder[i];
                }
                root.delta.dirty = true;
            }
        }
    }

    // Aligned to libkrkr2.so Player_buildNodeTree (0x6B51F0). The binary calls
    // this unconditionally from Player_initNonEmoteMotion (0x6B365C) after
    // Player_loadMotion succeeds — there is no lazy gate. The caller is
    // responsible for having loaded the motion first; we keep a minimal null
    // check so calls on a Player without a loaded motion become a no-op
    // instead of crashing, but we do NOT call ensureMotionLoaded here.
    void Player::buildNodeTree() {
        static std::uint32_t s_buildDiagSeq = 0;
        const auto diagSeq = ++s_buildDiagSeq;
        const bool emitDiag = shouldEmitMotionLoadDiag(diagSeq);
        if(_motionContentVariant.Type() != tvtObject) {
            if(emitDiag && LOGGER) {
                LOGGER->info(
                    "PRTDIAG Player::buildNodeTree no-raw-content-return seq={} this={} motionKey='{}' chara='{}'",
                    diagSeq, static_cast<const void *>(this),
                    detail::narrow(_motionKey), detail::narrow(_chara));
            }
            return;
        }

        const auto nodesBefore = _nodes.size();
        resetNodeTreeForBuildLike_0x6B56F8();

        if(emitDiag && LOGGER) {
            LOGGER->info(
                "PRTDIAG Player::buildNodeTree enter seq={} this={} motionKey='{}' chara='{}' activePath='{}' nodesBefore={} nodesAfterReset={} allplaying={}",
                diagSeq, static_cast<const void *>(this),
                detail::narrow(_motionKey), detail::narrow(_chara),
                matchedMotionPath().empty()
                    ? std::string("<none>") : matchedMotionPath(),
                nodesBefore, _nodes.size(),
                diagBool(_allplaying));
        }

        detail::buildNodeTree(
            *this, _motionContentVariant,
            _preview);  // binary buildNodeTree (0x6B43A4) gates on +1092 (preview)

        if(emitDiag && LOGGER) {
            LOGGER->info(
                "PRTDIAG Player::buildNodeTree after-detail seq={} this={} activePath='{}' nodeCount={} labelMap={} preview={}",
                diagSeq, static_cast<const void *>(this),
                matchedMotionPath().empty()
                    ? std::string("<none>") : matchedMotionPath(),
                _nodes.size(), _nodeLabelMap.size(), diagBool(_preview));
        }

        if(!_nodes.empty()) {
            auto &root = _nodes[0];
            // Aligned to libkrkr2.so Player_setRootFlipX/X/Y
            // (0x6CD028/0x6CD048/0x6CD068): these setters write the delta block
            // at node+1584..+1660, not the local post-interpolation mirror.
            root.delta.flipX = _rootFlipX;
            if(_hasPendingRootPos) {
                root.delta.posX = _pendingRootX;
                root.delta.posY = _pendingRootY;
            }
            root.delta.dirty = true;
        }

        const auto motionPath = matchedMotionPath();
        if(detail::logoChainTraceEnabledForPath(motionPath)) {
            detail::logoChainTraceLogf(
                motionPath, "buildNodeTree", "0x6B51F0", _clampedEvalTime,
                "nodeCount={}", _nodes.size());
            for(const auto &node : _nodes) {
                detail::logoChainTraceLogf(
                    motionPath, "buildNodeTree.node", "0x6B51F0",
                    _clampedEvalTime,
                    "nodeIndex={} label={} type={} parent={} hasSource={} meshType={} inheritFlags=0x{:x} parameterizeIndex={} objTriPriority={} clipAABB={} meshAncestor={} stencilType={}",
                    node.index,
                    node.layerName.IsEmpty() ? std::string("<root>")
                                             : detail::narrow(node.layerName),
                    node.nodeType, node.parentIndex,
                    node.source.valid ? 1 : 0,
                    node.meshType, node.inheritFlags, node.parameterizeIndex,
                    node.objTriPriority,
                    static_cast<const void *>(node.clipAABB),
                    static_cast<const void *>(node.meshAncestor),
                    node.stencilType);
            }
        }
    }

} // namespace motion
