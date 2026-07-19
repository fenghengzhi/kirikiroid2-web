//
// Build the persistent node tree from the raw TJS layer hierarchy.
// Aligned to Player_buildNodeTree_recursive @0x6B4A6C,
// Player_initNodeFields @0x6B3C78 and Player_buildNodeTree @0x6B51F0.
//

#include "NodeTree.h"

#include "MotionDispatch.h"
#include "MotionNode.h"
#include "Player.h"
#include "PlayerInternal.h"
#include "ncbind.hpp"
#include "tjsArray.h"

#include <atomic>
#include <cstdio>
#include <spdlog/spdlog.h>

namespace motion::detail {

    namespace {

        bool rawTryGet(const tTJSVariant &holder, const tjs_char *member,
                       tTJSVariant &value) {
            return holder.Type() == tvtObject &&
                   motionTryPropGet(holder, member, value);
        }

        bool rawTryGetByNum(const tTJSVariant &holder, tjs_int index,
                            tTJSVariant &value) {
            if(holder.Type() != tvtObject) {
                return false;
            }
            iTJSDispatch2 *dispatch = holder.AsObjectNoAddRef();
            return TJS_SUCCEEDED(dispatch->PropGetByNum(
                TJS_MEMBERMUSTEXIST, index, &value, dispatch));
        }

        tTJSVariant rawGet(const tTJSVariant &holder,
                           const tjs_char *member) {
            return holder.Type() == tvtObject
                       ? motionPropGet(holder, member)
                       : tTJSVariant{};
        }

        int rawInt(const tTJSVariant &holder, const tjs_char *member,
                   int fallback = 0) {
            return holder.Type() == tvtObject
                       ? static_cast<int>(
                             motionPropGet(holder, member).AsInteger())
                       : fallback;
        }

        double rawReal(const tTJSVariant &holder, const tjs_char *member,
                       double fallback = 0.0) {
            return holder.Type() == tvtObject
                       ? motionPropGet(holder, member).AsReal()
                       : fallback;
        }

        bool rawBool(const tTJSVariant &holder, const tjs_char *member,
                     bool fallback = false) {
            return holder.Type() == tvtObject
                       ? motionPropGetBool(holder, member)
                       : fallback;
        }

        int rawCount(const tTJSVariant &holder) {
            return holder.Type() == tvtObject
                       ? motionPropGetCount(holder)
                       : 0;
        }

        void createChildPlayerLike_0x6B3C78(Player &player, MotionNode &node,
                                             const tTJSVariant &rawLayer) {
            using PlayerAdaptor = ncbInstanceAdaptor<Player>;
            {
                static std::atomic<long> createCount{0};
                const long count = ++createCount;
                if(count <= 80 || (count >= 10000 && count <= 10060) ||
                   count % 2000 == 0) {
                    std::string chain;
                    Player *root = &player;
                    int depth = 0;
                    for(Player *p = &player; p && depth < 4000; ++depth) {
                        if(depth < 10) {
                            if(depth) chain += " <- ";
                            chain += narrow(p->getChara());
                        }
                        root = p;
                        p = p->parentPlayerForDiag();
                    }
                    const std::string childLabel = narrow(node.layerName);
                    std::fprintf(
                        stderr,
                        "CREATESITE n=%ld childLabel='%s' nodeIdx=%d depth=%d "
                        "rootChara='%s' rootPtr=%p chain='%s'\n",
                        count,
                        node.layerName.IsEmpty() ? "<none>" : childLabel.c_str(),
                        node.index, depth, narrow(root->getChara()).c_str(),
                        static_cast<void *>(root), chain.c_str());
                }
            }

            auto *childNative = new Player(player.getResourceManager());
            if(auto *dispatch = PlayerAdaptor::CreateAdaptor(childNative)) {
                node.childPlayerVar = tTJSVariant(dispatch, dispatch);
                dispatch->Release();
            } else {
                delete childNative;
                return;
            }

            player.inheritChildPlayerStateLike_0x6B3C78(node);
            if(auto *child = node.getChildPlayer()) {
                // 0x6B4404..0x6B4654: optional bool defaults to false, then
                // inherit project context/coordinate/order and parent zFactor.
                child->setIndependentLayerInherit(rawBool(
                    rawLayer, TJS_W("motionIndependentLayerInherit"), false));
                child->setZFactor(player.getZFactor());
            }
        }

        void initNodeFieldsLike_0x6B3C78(Player &player, MotionNode &node,
                                         const tTJSVariant &rawLayer,
                                         int parentPreview) {
            // Four independent CopyRef owners in the Android node.
            node.emoteEditVariant = rawGet(rawLayer, TJS_W("emoteEdit"));
            node.frameListVariant = rawGet(rawLayer, TJS_W("frameList"));

            node.layerName =
                motionPropGetString(rawLayer, TJS_W("label"));
            const tTJSVariant parameterize =
                rawGet(rawLayer, TJS_W("parameterize"));
            if(parameterize.Type() == tvtInteger) {
                node.parameterizeIndex =
                    static_cast<int>(parameterize.AsInteger());
                node.parameterEntry =
                    internal::resolveNodeParameterEntry(player, node);
            } else {
                // Player_initNodeFields @0x6B3EA0 writes node+8=null for every
                // non-integer `parameterize` variant. The default parameter
                // entry is used by other binary consumers, not stored here.
                node.parameterizeIndex = -1;
                node.parameterEntry = nullptr;
            }

            node.coordinateMode = rawInt(rawLayer, TJS_W("coordinate"));
            node.joinTarget = rawBool(rawLayer, TJS_W("joinTarget"));
            node.groundCorrection =
                rawBool(rawLayer, TJS_W("groundCorrection"));
            node.inheritFlags =
                rawInt(rawLayer, TJS_W("inheritMask"), node.inheritFlags);

            const tTJSVariant transformOrder =
                rawGet(rawLayer, TJS_W("transformOrder"));
            for(int index = 0; index < 4; ++index) {
                tTJSVariant value;
                if(rawTryGetByNum(transformOrder, index, value)) {
                    node.transformOrder[index] =
                        static_cast<int>(value.AsInteger());
                }
            }

            node.meshType = rawInt(rawLayer, TJS_W("meshTransform"));
            if(node.meshType != 0) {
                node.meshFlags =
                    rawInt(rawLayer, TJS_W("meshSyncChildMask"));
                node.meshDivision = rawInt(rawLayer, TJS_W("meshDivision"));
                tTJSVariant meshCombine;
                if(rawTryGet(rawLayer, TJS_W("meshCombine"), meshCombine)) {
                    node.meshCombineEnabled = motionPropGetBool(
                        rawLayer, TJS_W("meshCombine"));
                }
            }

            node.nodeType = rawInt(rawLayer, TJS_W("type"));
            node.stencilTypeBase =
                rawInt(rawLayer, TJS_W("stencilType"));
            node.stencilType = node.stencilTypeBase;

            switch(node.nodeType) {
                case 0:
                    node.objTriPriority =
                        rawInt(rawLayer, TJS_W("objTriPriority"));
                    break;
                case 1:
                    node.shapeType = rawInt(rawLayer, TJS_W("shape"));
                    break;
                case 3:
                    if(parentPreview != 0) {
                        node.stencilType &= ~4;
                    }
                    node.meshType = 0;
                    createChildPlayerLike_0x6B3C78(player, node, rawLayer);
                    break;
                case 4:
                    node.particleType = rawInt(rawLayer, TJS_W("particle"));
                    node.particleMaxNum =
                        rawInt(rawLayer, TJS_W("particleMaxNum"));
                    node.particleAccelRatio =
                        rawReal(rawLayer, TJS_W("particleAccelRatio"));
                    node.particleInheritAngle =
                        rawBool(rawLayer, TJS_W("particleInheritAngle"));
                    node.particleInheritVelocity =
                        rawInt(rawLayer, TJS_W("particleInheritVelocity"));
                    node.particleFlyDirection =
                        rawInt(rawLayer, TJS_W("particleFlyDirection"));
                    node.particleApplyZoomToVelocity = rawInt(
                        rawLayer, TJS_W("particleApplyZoomToVelocity"));
                    node.particleDeleteOutside = rawBool(
                        rawLayer, TJS_W("particleDeleteOutsideScreen"));
                    node.particleTriVolume =
                        rawBool(rawLayer, TJS_W("particleTriVolume"));
                    node.particleMotionListVariant =
                        rawGet(rawLayer, TJS_W("particleMotionList"));
                    if(auto *array = TJSCreateArrayObject()) {
                        node.particleArrayVar = tTJSVariant(array, array);
                        array->Release();
                    }
                    break;
                case 6:
                    // node+2380 is already zero from MotionNode construction.
                    break;
                case 9:
                    node.anchorType = node.cameraConstraintType =
                        rawInt(rawLayer, TJS_W("anchor"));
                    break;
                case 12:
                    node.stencilCompositeMaskLayerListVariant = rawGet(
                        rawLayer, TJS_W("stencilCompositeMaskLayerList"));
                    break;
                default:
                    break;
            }
        }

        void walkRawTree(const tTJSVariant &rawLayers,
                         int parentIndex, Player &player, int parentPreview) {
            const int count = rawCount(rawLayers);
            for(int arrayIndex = 0; arrayIndex < count; ++arrayIndex) {
                tTJSVariant rawLayer;
                if(!rawTryGetByNum(rawLayers, arrayIndex, rawLayer) ||
                   rawLayer.Type() != tvtObject) {
                    continue;
                }

                auto &nodes = player.nodesForBuild();
                nodes.emplace_back();
                MotionNode &node = nodes.back();
                node.index = static_cast<int>(nodes.size() - 1);
                node.parentIndex = parentIndex;
                node.layerId1 = player.dispatchRequireLayerId();
                node.layerId2 = player.dispatchRequireLayerId();

                initNodeFieldsLike_0x6B3C78(
                    player, node, rawLayer, parentPreview);
                player.nodeLabelMapForBuild()[node.layerName] = node.index;

                const int thisIndex = node.index;
                const tTJSVariant rawChildren =
                    rawGet(rawLayer, TJS_W("children"));
                walkRawTree(rawChildren, thisIndex, player, parentPreview);
            }
        }

    } // namespace

    void buildNodeTree(Player &player, const tTJSVariant &motionContent,
                       int parentPreview) {
        ensureRootNodeLike_0x6CED30(player);
        player._nodes.front().index = 0;
        player._nodes.front().parentIndex = -1;

        const tTJSVariant rawLayers =
            rawGet(motionContent, TJS_W("layer"));
        walkRawTree(rawLayers, 0, player, parentPreview);

        // 0x6B531C..0x6B55A8: raw type-12 mask list -> Player+24 map;
        // append the referenced type-0/type-3 node pointer and mark the target.
        for(size_t index = 1; index < player._nodes.size(); ++index) {
            auto &node = player._nodes[index];
            if(node.nodeType != 12 || (node.stencilType & 4) == 0) {
                continue;
            }
            const int maskCount =
                rawCount(node.stencilCompositeMaskLayerListVariant);
            for(int maskIndex = 0; maskIndex < maskCount; ++maskIndex) {
                tTJSVariant rawLabel;
                if(!rawTryGetByNum(node.stencilCompositeMaskLayerListVariant,
                                   maskIndex, rawLabel)) {
                    continue;
                }
                const ttstr label(rawLabel);
                const auto found = player._nodeLabelMap.find(label);
                if(found == player._nodeLabelMap.end()) {
                    continue;
                }
                auto &target = player._nodes[
                    static_cast<size_t>(found->second)];
                if(target.nodeType == 0 || target.nodeType == 3) {
                    node.stencilCompositeMaskNodes.push_back(&target);
                    target.stencilCompositeMaskReferenced = true;
                }
            }
        }

        if(auto logger = spdlog::get("plugin")) {
            logger->debug(
                "buildNodeTree(raw): rawLayers={}, {} nodes built",
                rawCount(rawLayers), player._nodes.size());
        }
    }

} // namespace motion::detail
