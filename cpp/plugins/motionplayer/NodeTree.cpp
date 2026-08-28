// Build the persistent node tree from the raw TJS layer hierarchy.

#include "NodeTree.h"

#include "MotionDispatch.h"
#include "MotionNode.h"
#include "Player.h"
#include "PlayerInternal.h"
#include "ncbind.hpp"
#include "tjsArray.h"

#include <memory>

namespace motion::detail {

    namespace {
        struct DispatchRelease {
            void operator()(iTJSDispatch2 *dispatch) const {
                if(dispatch) {
                    dispatch->Release();
                }
            }
        };

        using RetainedDispatch =
            std::unique_ptr<iTJSDispatch2, DispatchRelease>;

        RetainedDispatch retainObjectDispatch_guess(
            const tTJSVariant &source) {
            // The references first copy the Variant, then retain its Object and
            // destroy the temporary Variant. This leaves one independent
            // dispatch reference across callbacks and recursive work.
            tTJSVariant copy(source);
            iTJSDispatch2 *dispatch = copy.AsObject();
            copy.Clear();
            return RetainedDispatch(dispatch);
        }

        tjs_int requireLayerId_guess(iTJSDispatch2 *resourceManager,
                                     tTJSVariant &result) {
            (void)resourceManager->FuncCall(
                0, TJS_W("requireLayerId"),
                &nodeRequireLayerIdMemberHint_guess, &result,
                0, nullptr, resourceManager);
            return static_cast<tjs_int>(result.AsInteger());
        }

        void createType3ChildPlayer_guess(
            Player &player, MotionNode &node,
            ncbPropAccessor &layer) {
            using PlayerAdaptor = ncbInstanceAdaptor<Player>;

            auto *childNative = new Player(player.getResourceManager());
            player.linkType3ChildPlayer_guess(*childNative);

            bool independentLayerInherit = false;
            if(layer.HasValue(
                   TJS_W("motionIndependentLayerInherit"))) {
                independentLayerInherit = layer.GetValue(
                    TJS_W("motionIndependentLayerInherit"),
                    ncbTypedefs::Tag<bool>());
            }
            player.initializeType3ChildState_guess(
                *childNative, node, independentLayerInherit);

            // Both internal child producers use CreateAdaptor(child,false,false).
            // A null result leaves the Variant void and deliberately leaks the
            // newly allocated Player. If CreateNew succeeds but the fresh
            // adaptor lookup fails non-throwingly, ncbind instead returns a
            // non-null empty shell; the Variant is then Object but its native
            // Player slot is null, while childNative still leaks. Old-tree
            // teardown only exposes the Void form at its initial Object cast.
            tTJSVariant childVariant;
            if(auto *dispatch = PlayerAdaptor::CreateAdaptor(childNative)) {
                childVariant = tTJSVariant(dispatch, dispatch);
                dispatch->Release();
            }
            node.childPlayerVar = childVariant;
        }

        void initializeNodeFromLayer_guess(
            Player &player, MotionNode &node,
            const tTJSVariant &rawLayer) {
            ncbPropAccessor layer{tTJSVariant(rawLayer)};

            if(layer.HasValue(
                   TJS_W("emoteEdit"),
                   &nodeEmoteEditMemberHint_guess)) {
                node.emoteEditVariant = layer.GetValue(
                    TJS_W("emoteEdit"),
                    ncbTypedefs::Tag<tTJSVariant>(), 0,
                    &nodeEmoteEditMemberHint_guess);
            } else {
                node.emoteEditVariant.Clear();
            }

            node.layerName = layer.GetValue(
                TJS_W("label"), ncbTypedefs::Tag<ttstr>(), 0,
                &nodeLabelMemberHint_guess);

            const tTJSVariant parameterize = layer.GetValue(
                TJS_W("parameterize"),
                ncbTypedefs::Tag<tTJSVariant>(), 0,
                &parameterizeMemberHint_guess);
            node.parameterEntry =
                internal::selectParameterEntry_guess(player, parameterize);

            node.coordinateMode = static_cast<int>(layer.GetValue(
                TJS_W("coordinate"), ncbTypedefs::Tag<tjs_int>(), 0,
                &coordinateMemberHint_guess));
            node.joinTarget = layer.GetValue(
                TJS_W("joinTarget"), ncbTypedefs::Tag<bool>(), 0,
                &nodeJoinTargetMemberHint_guess);
            node.groundCorrection = layer.GetValue(
                TJS_W("groundCorrection"), ncbTypedefs::Tag<bool>(), 0,
                &nodeGroundCorrectionMemberHint_guess);
            node.frameListVariant = layer.GetValue(
                TJS_W("frameList"), ncbTypedefs::Tag<tTJSVariant>(), 0,
                &nodeFrameListMemberHint_guess);
            node.inheritFlags = static_cast<int>(layer.GetValue(
                TJS_W("inheritMask"), ncbTypedefs::Tag<tjs_int>(), 0,
                &nodeInheritMaskMemberHint_guess));

            ncbPropAccessor transform{layer.GetValue(
                TJS_W("transformOrder"),
                ncbTypedefs::Tag<tTJSVariant>(), 0,
                &nodeTransformOrderMemberHint_guess)};
            for(int index = 0; index < 4; ++index) {
                node.transformOrder[index] = static_cast<int>(
                    transform.getIntValue(index, 0));
            }

            node.meshType = static_cast<int>(layer.GetValue(
                TJS_W("meshTransform"), ncbTypedefs::Tag<tjs_int>()));
            if(node.meshType != 0) {
                node.meshFlags = static_cast<int>(layer.GetValue(
                    TJS_W("meshSyncChildMask"),
                    ncbTypedefs::Tag<tjs_int>()));
                node.meshDivision = static_cast<int>(layer.GetValue(
                    TJS_W("meshDivision"),
                    ncbTypedefs::Tag<tjs_int>()));
                if(layer.HasValue(TJS_W("meshCombine"))) {
                    node.meshCombine = layer.GetValue(
                        TJS_W("meshCombine"),
                        ncbTypedefs::Tag<bool>());
                }
            }

            node.nodeType = static_cast<int>(layer.GetValue(
                TJS_W("type"), ncbTypedefs::Tag<tjs_int>()));
            if(layer.HasValue(TJS_W("stencilType"))) {
                node.stencilType = static_cast<int>(layer.GetValue(
                    TJS_W("stencilType"),
                    ncbTypedefs::Tag<tjs_int>()));
            } else {
                node.stencilType = 0;
            }

            switch(node.nodeType) {
                case 0:
                    node.objTriPriority = static_cast<int>(layer.GetValue(
                        TJS_W("objTriPriority"),
                        ncbTypedefs::Tag<tjs_int>()));
                    break;
                case 1:
                    node.shapeType = static_cast<int>(layer.GetValue(
                        TJS_W("shape"), ncbTypedefs::Tag<tjs_int>()));
                    break;
                case 3:
                    // The native initializer reads the Player byte here, after
                    // every preceding property callback. It is not a snapshot
                    // captured when tree construction began.
                    if(player.getPreview()) {
                        node.stencilType &= ~4;
                    }
                    node.meshType = 0;
                    createType3ChildPlayer_guess(
                        player, node, layer);
                    break;
                case 4:
                    node.prevM11 = 1.0;
                    node.prevM21 = 0.0;
                    node.prevM12 = 0.0;
                    node.prevM22 = 1.0;
                    node.prevParticleAngle = 0.0;
                    node.emitterTimerAccum = 0.0;
                    node.particleEmitterFlagActive = false;
                    node.particleType = static_cast<int>(layer.GetValue(
                        TJS_W("particle"),
                        ncbTypedefs::Tag<tjs_int>()));
                    node.particleMaxNum = static_cast<int>(layer.GetValue(
                        TJS_W("particleMaxNum"),
                        ncbTypedefs::Tag<tjs_int>()));
                    node.particleAccelRatio = layer.GetValue(
                        TJS_W("particleAccelRatio"),
                        ncbTypedefs::Tag<tjs_real>());
                    node.particleInheritAngle = layer.GetValue(
                        TJS_W("particleInheritAngle"),
                        ncbTypedefs::Tag<bool>());
                    node.particleInheritVelocity = static_cast<int>(
                        layer.GetValue(
                            TJS_W("particleInheritVelocity"),
                            ncbTypedefs::Tag<tjs_int>()));
                    node.particleFlyDirection = static_cast<int>(
                        layer.GetValue(
                            TJS_W("particleFlyDirection"),
                            ncbTypedefs::Tag<tjs_int>()));
                    node.particleApplyZoomToVelocity = static_cast<int>(
                        layer.GetValue(
                            TJS_W("particleApplyZoomToVelocity"),
                            ncbTypedefs::Tag<tjs_int>()));
                    node.particleDeleteOutside = layer.GetValue(
                        TJS_W("particleDeleteOutsideScreen"),
                        ncbTypedefs::Tag<bool>());
                    node.particleTriVolume = layer.GetValue(
                        TJS_W("particleTriVolume"),
                        ncbTypedefs::Tag<bool>());
                    node.particleMotionListVariant = layer.GetValue(
                        TJS_W("particleMotionList"),
                        ncbTypedefs::Tag<tTJSVariant>());
                    if(auto *array = TJSCreateArrayObject()) {
                        node.particleArrayVar =
                            tTJSVariant(array, array);
                        array->Release();
                    }
                    break;
                case 6:
                    node.emitterActive = false;
                    break;
                case 9:
                    node.anchorType_guess = static_cast<int>(layer.GetValue(
                        TJS_W("anchor"), ncbTypedefs::Tag<tjs_int>()));
                    break;
                case 12:
                    node.stencilCompositeMaskLayerListVariant =
                        layer.GetValue(
                            TJS_W("stencilCompositeMaskLayerList"),
                            ncbTypedefs::Tag<tTJSVariant>());
                    break;
                default:
                    break;
            }
        }

        void buildNodeTreeRecursive_guess(
            const tTJSVariant &rawLayers, int parentIndex,
            Player &player,
            const tTJSVariant &resourceManagerVariant) {
            ncbPropAccessor layers{tTJSVariant(rawLayers)};
            const int count = static_cast<int>(layers.GetArrayCount());
            const RetainedDispatch resourceManager =
                retainObjectDispatch_guess(resourceManagerVariant);

            for(int arrayIndex = 0; arrayIndex < count; ++arrayIndex) {
                auto &nodes = player.nodesForBuild();
                const int thisIndex = static_cast<int>(nodes.size());

                // Append and establish the native partial-node state before
                // the indexed layer lookup. Any later exception leaves this
                // element in the deque.
                nodes.emplace_back();
                MotionNode &node = nodes.back();
                node.parentIndex = parentIndex;
                node.slots[0].done = true;
                node.slots[1].done = true;

                const tTJSVariant rawLayer = layers.GetValue(
                    arrayIndex, ncbTypedefs::Tag<tTJSVariant>());
                ncbPropAccessor layer{tTJSVariant(rawLayer)};

                // The map key and node.layerName are two independent property
                // reads. A side-effecting getter may therefore make them differ.
                const ttstr rawMapLabel = layer.GetValue(
                    TJS_W("label"), ncbTypedefs::Tag<ttstr>());
                player.nodeLabelMapForBuild()[rawMapLabel] = thisIndex;

                tTJSVariant layerIdResult;
                node.layerId1 = requireLayerId_guess(
                    resourceManager.get(), layerIdResult);
                node.layerId2 = requireLayerId_guess(
                    resourceManager.get(), layerIdResult);

                initializeNodeFromLayer_guess(player, node, rawLayer);

                const tTJSVariant rawChildren = layer.GetValue(
                    TJS_W("children"),
                    ncbTypedefs::Tag<tTJSVariant>());
                buildNodeTreeRecursive_guess(
                    rawChildren, thisIndex, player,
                    resourceManagerVariant);
            }
        }

    } // namespace

    void buildNodeTree(Player &player,
                       ncbPropAccessor &motionContent) {
        const tTJSVariant rawLayers = motionContent.GetValue(
            TJS_W("layer"), ncbTypedefs::Tag<tTJSVariant>());
        buildNodeTreeRecursive_guess(
            rawLayers, 0, player,
            player._resourceManager);

        const int nodeCount = static_cast<int>(player._nodes.size());
        for(int index = 1; index < nodeCount; ++index) {
            auto &node = player._nodes[static_cast<size_t>(index)];
            if(node.nodeType != 12 || (node.stencilType & 4) == 0) {
                continue;
            }

            ncbPropAccessor maskList{tTJSVariant(
                node.stencilCompositeMaskLayerListVariant)};
            const int maskCount =
                static_cast<int>(maskList.GetArrayCount());
            for(int maskIndex = 0; maskIndex < maskCount; ++maskIndex) {
                const ttstr label = maskList.GetValue(
                    maskIndex, ncbTypedefs::Tag<ttstr>());
                MotionNode *const target =
                    player.findNodeByRawLabel_guess(label, false);
                if(target == nullptr) {
                    continue;
                }

                if(target->nodeType == 0 || target->nodeType == 3) {
                    node.stencilCompositeMaskNodes.push_back(target);
                    target->stencilCompositeMaskReferenced = true;
                }
            }
        }
    }

} // namespace motion::detail
