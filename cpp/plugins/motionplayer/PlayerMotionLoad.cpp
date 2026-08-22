// PlayerMotionLoad.cpp — motion load, variable init, and node tree build
// Split out for maintainability.
//
#include "PlayerInternal.h"
#include "MotionDispatch.h"
#include "MotionTraceWeb.h"

#include <memory>

using namespace motion::internal;

namespace motion {
    namespace {
        bool shouldEmitMotionLoadDiag(std::uint32_t seq) {
            return seq <= 200 || (seq % 100) == 0;
        }

        const char *diagBool(bool v) {
            return v ? "true" : "false";
        }

        struct DispatchRelease {
            void operator()(iTJSDispatch2 *dispatch) const {
                if(dispatch) {
                    dispatch->Release();
                }
            }
        };

        using RetainedDispatch =
            std::unique_ptr<iTJSDispatch2, DispatchRelease>;
    }

    // The script-visible default callback is an identity function in all four
    // references. Motion loading invokes it through the current TJS dispatch,
    // so scripts may replace the returned request dictionary.
    tTJSVariant Player::onFindMotion(tTJSVariant request) {
        // Returning through a const lvalue forces the copy performed by all
        // four native bodies instead of allowing an implicit move from the
        // by-value parameter.
        return static_cast<const tTJSVariant &>(request);
    }

    // Four-reference variable-track builder. It runs synchronously after the
    // node tree is built and before the chain-playback state gate. Each source
    // item is appended to the deque before any of its named properties are
    // read. That ordering is observable when a getter throws: the partially
    // initialized deque element remains present, with only the fields reached
    // before the exception written.
    void Player::initVariables() {
        _variableLabelScopes.clear();

        // The copied motion accessor is a full-expression temporary. It keeps
        // the selected content alive through the typed `variable` read, then
        // releases it before the Void gate and list traversal.
        const tTJSVariant variableList =
            ncbPropAccessor(tTJSVariant(_motionContentVariant)).GetValue(
                TJS_W("variable"),
                ncbTypedefs::Tag<tTJSVariant>(), 0);
        if(variableList.Type() == tvtVoid) {
            return;
        }

        ncbPropAccessor variableListObject{tTJSVariant(variableList)};
        const tjs_int count = variableListObject.GetArrayCount();
        for(tjs_int i = 0; i < count; ++i) {
            ncbPropAccessor itemObject{variableListObject.GetValue(
                i, ncbTypedefs::Tag<tTJSVariant>(), 0)};
            auto &entry = _variableLabelScopes.emplace_back();

            // The first access converts the label to the cascade key. Native
            // construction then seeds the two type-zero sentinels and cursor;
            // only after that does a second, independent access retain the raw
            // label value as the keyframe source.
            entry.cascadeKey = itemObject.GetValue(
                TJS_W("label"), ncbTypedefs::Tag<ttstr>(), 0);
            entry.value = 0.0;
            entry.slot[0].typeZeroFlag = true;
            entry.slot[1].typeZeroFlag = true;
            entry.activeSlotCursor = 0;
            entry.frameSource = itemObject.GetValue(
                TJS_W("label"), ncbTypedefs::Tag<tTJSVariant>(), 0);

            const ttstr scope(itemObject.GetValue(
                TJS_W("scope"), ncbTypedefs::Tag<tTJSVariant>(), 0));
            if(!scope.IsEmpty()) {
                entry.cascadeKey = scope + TJS_W("::") +
                                   entry.cascadeKey;
            }
        }
    }

    void Player::resetAndReleaseOldNodeTree_guess() {
        // Retain the canonical ResourceManager dispatch before invoking child
        // objects. Re-entrant callbacks therefore cannot redirect the release
        // phase by replacing the Player's Variant owner.
        tTJSVariant resourceManagerCopy(_resourceManager);
        RetainedDispatch resourceManager(
            resourceManagerCopy.AsObject());
        resourceManagerCopy.Clear();

        (void)detail::visitNodeOwnedPlayerVariants_guess(
            _nodes, [](const tTJSVariant &child) {
                iTJSDispatch2 *object = child.AsObjectNoAddRef();
                (void)object->Invalidate(
                    0, nullptr, nullptr, object);
                return true;
            });

        // Rebuild preserves HM1 entries but resets their live write value and
        // drops every cached, non-owning node pointer.
        for(auto &entry : _evalCascadeMap) {
            entry.second.writeVal = 1.0;
            entry.second.heapResult.clear();
        }

        const auto releaseLayerId = [&](tjs_int id) {
            tTJSVariant idValue(id);
            tTJSVariant *arguments[1] = {&idValue};
            (void)resourceManager->FuncCall(
                0, TJS_W("releaseLayerId"),
                &detail::releaseLayerIdMemberHint_guess,
                nullptr, 1, arguments, resourceManager.get());
        };

        for(size_t index = 1; index < _nodes.size(); ++index) {
            detail::MotionNode &node = _nodes[index];
            releaseLayerId(node.layerId1);
            releaseLayerId(node.layerId2);
            // This third release is gated only by the persistent item latch;
            // draw/rawFlag21 and the numeric value do not participate. Native
            // does not clear either field before the re-entrant dispatch, so an
            // exception stops before suffix erase and leaves the old item/tree
            // published for a later retry.
            if(node.preparedRenderItem &&
               node.preparedRenderItem->rawFlag20) {
                releaseLayerId(
                    node.preparedRenderItem->renderLayerId);
            }
        }

        detail::eraseNonRootNodesAndClearLabelMap_guess(*this);
    }

    void Player::linkType3ChildPlayer_guess(Player &child) {
        // These non-owning links are installed before the first child-specific
        // property lookup.
        child._rootPlayer = _rootPlayer;
        child._parentPlayer = this;
    }

    void Player::initializeType3ChildState_guess(
        Player &child, detail::MotionNode &node,
        bool independentLayerInherit) {
        // This construction-only path writes the inherit flag directly after
        // dirtying the child's sole root; it does not use the public setter.
        auto &root = child._nodes.front();
        if(child._independentLayerInherit != independentLayerInherit) {
            root.delta.dirty = true;
            child._independentLayerInherit = independentLayerInherit;
        }
        child._type3RootTransformAlreadyPropagated = true;
        // Type-3 children inherit an independently owning context copy. Their
        // later successful/failed loads replace or clear only the child field.
        child._findMotionContextVariant = _findMotionContextVariant;
        root.coordinateMode = node.coordinateMode;
        child.setZFactor(_zFactor);
        for(int i = 0; i < 4; ++i) {
            root.transformOrder[i] = node.transformOrder[i];
        }
    }

    void Player::buildNodeTree_guess() {
        static std::uint32_t s_buildDiagSeq = 0;
        const bool logoTraceEnabled = detail::logoChainTraceEnabled();
        std::uint32_t diagSeq = 0;
        bool emitDiag = false;
        if(logoTraceEnabled && LOGGER) {
            diagSeq = ++s_buildDiagSeq;
            emitDiag = shouldEmitMotionLoadDiag(diagSeq);
        }

        // Construct the owning property accessor before old-tree teardown.
        // A non-object Variant throws here and leaves the existing tree intact.
        ncbPropAccessor motionContent{
            tTJSVariant(_motionContentVariant)};

        const auto nodesBefore = emitDiag ? _nodes.size() : 0;
        resetAndReleaseOldNodeTree_guess();

        if(emitDiag && LOGGER) {
            const auto entryMotionPath = matchedMotionPath();
            LOGGER->info(
                "PRTDIAG Player::buildNodeTree enter seq={} this={} motionKey='{}' chara='{}' activePath='{}' nodesBefore={} nodesAfterReset={} allplaying={}",
                diagSeq, static_cast<const void *>(this),
                detail::narrow(_motionKey), detail::narrow(_chara),
                entryMotionPath.empty()
                    ? std::string("<none>") : entryMotionPath,
                nodesBefore, _nodes.size(),
                diagBool(_allplaying));
        }

        detail::buildNodeTree(*this, motionContent);

        // Both the sampled PRTDIAG projection and the per-node trace consume
        // the same post-build path. Do not materialize it when trace is off.
        std::string motionPath;
        if(logoTraceEnabled) {
            motionPath = matchedMotionPath();
        }
        if(emitDiag && LOGGER) {
            LOGGER->info(
                "PRTDIAG Player::buildNodeTree after-detail seq={} this={} activePath='{}' nodeCount={} labelMap={} preview={}",
                diagSeq, static_cast<const void *>(this),
                motionPath.empty()
                    ? std::string("<none>") : motionPath,
                _nodes.size(), _nodeLabelMap.size(), diagBool(_preview));
        }

        if(logoTraceEnabled &&
           detail::logoChainTraceEnabledForPath(motionPath)) {
            detail::logoChainTraceLogf(
                motionPath, "buildNodeTree", "buildNodeTree", _clampedEvalTime,
                "nodeCount={}", _nodes.size());
            for(const auto &node : _nodes) {
                detail::logoChainTraceLogf(
                    motionPath, "buildNodeTree.node", "buildNodeTree",
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
