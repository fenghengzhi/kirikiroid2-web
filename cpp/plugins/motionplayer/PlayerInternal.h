// PlayerInternal.h — Shared internal helpers extracted from Player.cpp
// These were originally in an anonymous namespace. Now in motion::internal
// with inline linkage for use across multiple translation units.
//
#pragma once

#include "Player.h"

#include <algorithm>
#include <array>
#include <cmath>
#include "WindowIntf.h"
#include <cstring>
#include <limits>
#include <optional>
#include <random>
#include <stdexcept>
#include <unordered_set>
#include <vector>

#include "LayerIntf.h"
#include "LayerBitmapIntf.h"
#include "GraphicsLoaderIntf.h"
#include "tvpgl.h"
#include "RuntimeSupport.h"
#include "ResourceManager.h"
#include "SeparateLayerAdaptor.h"
#include "D3DAdaptor.h"
#include "StorageIntf.h"
#include "ncbind.hpp"
#include "tjsArray.h"
#include "EventIntf.h"
#include "ScriptMgnIntf.h"
#include "NodeTree.h"
#include "MotionNode.h"
#include "MotionDispatch.h"

#define LOGGER spdlog::get("plugin")
#define STUB_WARN(name) LOGGER->warn("Player::" #name "() stub called")

namespace motion {
namespace internal {

        constexpr double kMotionFramesPerMillisecond = 60.0 / 1000.0;

        inline detail::MotionParameterEntry *
        resolveNodeParameterEntry(Player &player,
                                  const detail::MotionNode &node) {
            if(node.parameterEntry != nullptr) {
                return node.parameterEntry;
            }
            if(node.parameterizeIndex >= 0 &&
               static_cast<size_t>(node.parameterizeIndex) <
                   player._parameterEntries.size()) {
                return &player._parameterEntries[static_cast<size_t>(
                    node.parameterizeIndex)];
            }
            if(node.parameterizeIndex >= 0) {
                throw std::out_of_range("parameter id out of range.");
            }
            if(player._defaultParameterEntryPtr != nullptr) {
                return player._defaultParameterEntryPtr;
            }
            return &player._defaultParameterEntry;
        }

        inline bool getObjectProperty(const tTJSVariant &object, const tjs_char *name,
                               tTJSVariant &result) {
            result.Clear();
            if(object.Type() != tvtObject || object.AsObjectNoAddRef() == nullptr) {
                return false;
            }
            const auto closure = object.AsObjectClosureNoAddRef();
            iTJSDispatch2 *dispatch =
                closure.Object ? closure.Object : object.AsObjectNoAddRef();
            iTJSDispatch2 *objthis =
                closure.ObjThis ? closure.ObjThis : dispatch;
            return TJS_SUCCEEDED(dispatch->PropGet(
                0, name, nullptr, &result, objthis));
        }

        inline tjs_int getObjectCount(const tTJSVariant &object) {
            tTJSVariant count;
            return getObjectProperty(object, TJS_W("count"), count)
                ? count.AsInteger()
                : 0;
        }

        inline bool tryGetLayerObject(const tTJSVariant &value,
                               tTJSNI_BaseLayer *&layer) {
            layer = nullptr;
            if(value.Type() != tvtObject || value.AsObjectNoAddRef() == nullptr) {
                return false;
            }

            iTJSDispatch2 *obj = value.AsObjectNoAddRef();
            if(TJS_SUCCEEDED(obj->NativeInstanceSupport(
                   TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                   reinterpret_cast<iTJSNativeInstance **>(&layer))) &&
               layer != nullptr) {
                return true;
            }

            // Fallback: try via closure's Object member (may differ from
            // AsObjectNoAddRef for certain TJS value representations)
            const auto closure = value.AsObjectClosureNoAddRef();
            if(closure.Object && closure.Object != obj) {
                return TJS_SUCCEEDED(closure.Object->NativeInstanceSupport(
                           TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                           reinterpret_cast<iTJSNativeInstance **>(&layer))) &&
                    layer != nullptr;
            }

            return false;
        }

        // Resolve a real Layer dispatch like libkrkr2.so sub_A7A050:
        // use only the variant's Object and ask it for the Layer native
        // instance. Do not chase ObjThis, SeparateLayerAdaptor owner, or TJS
        // properties here; Player_ResolveSLATarget @ 0x6D5948 only applies
        // this coercion to SLA+20 targetLayer.
        inline iTJSDispatch2 *tryResolveLayerDispatch(const tTJSVariant &value) {
            if(value.Type() != tvtObject || value.AsObjectNoAddRef() == nullptr) {
                return nullptr;
            }

            iTJSDispatch2 *obj = value.AsObjectNoAddRef();
            tTJSNI_BaseLayer *layer = nullptr;
            if(TJS_SUCCEEDED(obj->NativeInstanceSupport(
                   TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                   reinterpret_cast<iTJSNativeInstance **>(&layer))) &&
               layer) {
                return obj;
            }

            return nullptr;
        }

        inline iTJSDispatch2 *tryResolveSeparateAdaptorOwner(const tTJSVariant &value) {
            return tryResolveLayerDispatch(value);
        }

        inline bool getArrayItem(const tTJSVariant &object, tjs_int index,
                          tTJSVariant &result) {
            result.Clear();
            if(object.Type() != tvtObject || object.AsObjectNoAddRef() == nullptr) {
                return false;
            }
            return TJS_SUCCEEDED(object.AsObjectNoAddRef()->PropGetByNum(
                TJS_IGNOREPROP, index, &result, object.AsObjectNoAddRef()));
        }

        struct DictionaryEnumerator : public tTJSDispatch {
            std::vector<std::pair<ttstr, tTJSVariant>> entries;

            tjs_error FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *,
                               tTJSVariant *result, tjs_int numparams,
                               tTJSVariant **param,
                               iTJSDispatch2 *) override {
                if(numparams < 3) {
                    return TJS_E_BADPARAMCOUNT;
                }

                const tjs_uint32 flags = static_cast<tjs_uint32>(
                    param[1]->AsInteger());
                if(flags & TJS_HIDDENMEMBER) {
                    if(result) {
                        *result = static_cast<tjs_int>(1);
                    }
                    return TJS_S_OK;
                }

                entries.emplace_back(ttstr(*param[0]), *param[2]);
                if(result) {
                    *result = static_cast<tjs_int>(1);
                }
                return TJS_S_OK;
            }
        };

        // Player_applyBezierEasing @0x69A754. Unlike the offline-only decoded
        // helper, the Android runtime resolves x/y and every numbered
        // element through TJS dispatch on each call.
        inline double evaluateBezierVariantLike_0x69A754(
            const tTJSVariant &curve, double t) {
            const tTJSVariant x = detail::motionPropGet(curve, TJS_W("x"));
            const tTJSVariant y = detail::motionPropGet(curve, TJS_W("y"));
            const int count = detail::motionPropGetCount(x);
            if(detail::motionPropGetDoubleByNum(x, 0) >= t) {
                return detail::motionPropGetDoubleByNum(y, 0);
            }
            const int last = count - 1;
            if(detail::motionPropGetDoubleByNum(x, last) <= t) {
                return detail::motionPropGetDoubleByNum(y, last);
            }
            int index = 0;
            do {
                index += 3;
            } while(detail::motionPropGetDoubleByNum(x, index) < t);
            const double y0 = detail::motionPropGetDoubleByNum(y, index - 3);
            const double y1 = detail::motionPropGetDoubleByNum(y, index - 2);
            const double y2 = detail::motionPropGetDoubleByNum(y, index - 1);
            const double y3 = detail::motionPropGetDoubleByNum(y, index);
            const double u = 1.0 - t;
            return u * u * u * y0 + u * u * 3.0 * t * y1 +
                   u * 3.0 * t * t * y2 + t * t * t * y3;
        }

        // sub_698454 @0x698454. All x/y/t/s and nested segment x/y/p values
        // remain raw TJS dispatch owners and are read only at evaluation time.
        inline void evaluateControlPointVariantLike_0x698454(
            double outXY[2], const tTJSVariant &curve, double inputT) {
            const tTJSVariant mainX =
                detail::motionPropGet(curve, TJS_W("x"));
            const tTJSVariant mainY =
                detail::motionPropGet(curve, TJS_W("y"));
            const tTJSVariant knots =
                detail::motionPropGet(curve, TJS_W("t"));
            const tTJSVariant segments =
                detail::motionPropGet(curve, TJS_W("s"));

            int mainIndex = 0;
            int segmentIndex = -1;
            do {
                const double next = detail::motionPropGetDoubleByNum(
                    knots, segmentIndex + 2);
                ++segmentIndex;
                mainIndex += 3;
                if(next >= inputT) {
                    break;
                }
            } while(true);

            const double knotStart = detail::motionPropGetDoubleByNum(
                knots, segmentIndex);
            const double knotEnd = detail::motionPropGetDoubleByNum(
                knots, segmentIndex + 1);
            const tTJSVariant segment = detail::motionPropGetByNum(
                segments, segmentIndex);
            const tTJSVariant sx =
                detail::motionPropGet(segment, TJS_W("x"));
            const tTJSVariant sy =
                detail::motionPropGet(segment, TJS_W("y"));
            const tTJSVariant sp =
                detail::motionPropGet(segment, TJS_W("p"));
            const double localT =
                (inputT - knotStart) / (knotEnd - knotStart);

            double parameter;
            const int splineCount = detail::motionPropGetCount(sx);
            if(detail::motionPropGetDoubleByNum(sx, 0) >= localT) {
                parameter = detail::motionPropGetDoubleByNum(sy, 0);
            } else if(detail::motionPropGetDoubleByNum(
                          sx, splineCount - 1) <= localT) {
                parameter = detail::motionPropGetDoubleByNum(
                    sy, splineCount - 1);
            } else {
                int splineIndex = -1;
                do {
                    const double next = detail::motionPropGetDoubleByNum(
                        sx, splineIndex + 2);
                    ++splineIndex;
                    if(next >= localT) {
                        break;
                    }
                } while(true);
                const double x1 = detail::motionPropGetDoubleByNum(
                    sx, splineIndex + 1);
                const double x0 = detail::motionPropGetDoubleByNum(
                    sx, splineIndex);
                const double p1 = detail::motionPropGetDoubleByNum(
                    sp, splineIndex + 1);
                const double p0 = detail::motionPropGetDoubleByNum(
                    sp, splineIndex);
                const double y1 = detail::motionPropGetDoubleByNum(
                    sy, splineIndex + 1);
                const double y0 = detail::motionPropGetDoubleByNum(
                    sy, splineIndex);
                const double ratio = (localT - x0) / (x1 - x0);
                const double inverse = 1.0 - ratio;
                parameter = (x1 - x0) * (x1 - x0) *
                                ((ratio * ratio * ratio - ratio) * p1 +
                                 (inverse * inverse * inverse - inverse) * p0) /
                                6.0 +
                            ratio * y1 + inverse * y0;
            }

            const double x0 = detail::motionPropGetDoubleByNum(
                mainX, mainIndex - 3);
            const double y0 = detail::motionPropGetDoubleByNum(
                mainY, mainIndex - 3);
            const double x1 = detail::motionPropGetDoubleByNum(
                mainX, mainIndex - 2);
            const double y1 = detail::motionPropGetDoubleByNum(
                mainY, mainIndex - 2);
            const double x2 = detail::motionPropGetDoubleByNum(
                mainX, mainIndex - 1);
            const double y2 = detail::motionPropGetDoubleByNum(
                mainY, mainIndex - 1);
            const double x3 = detail::motionPropGetDoubleByNum(
                mainX, mainIndex);
            const double y3 = detail::motionPropGetDoubleByNum(
                mainY, mainIndex);
            const double inverse = 1.0 - parameter;
            const double w0 = inverse * inverse * inverse;
            const double w1 = parameter * inverse * inverse * 3.0;
            const double w2 = parameter * parameter * inverse * 3.0;
            const double w3 = parameter * parameter * parameter;
            outXY[0] = w0 * x0 + w1 * x1 + w2 * x2 + w3 * x3;
            outXY[1] = w0 * y0 + w1 * y1 + w2 * y2 + w3 * y3;
        }

        // sub_69A4D4 @0x69A4D4 with raw slot variants. The caller supplies
        // slot+168 ccc and slot+268 cp directly.
        inline void interpolatePositionVariantLike_0x69A4D4(
            const tTJSVariant &easingCurve,
            const double dstPos[3], const double srcPos[3], double outPos[3],
            int coordinateMode, const tTJSVariant &rotationCurve, double t) {
            if(srcPos[0] == dstPos[0] && srcPos[1] == dstPos[1] &&
               srcPos[2] == dstPos[2]) {
                std::copy(srcPos, srcPos + 3, outPos);
                return;
            }
            const double eased = easingCurve.Type() != tvtVoid
                ? evaluateBezierVariantLike_0x69A754(easingCurve, t) : t;
            if(rotationCurve.Type() == tvtVoid) {
                for(int i = 0; i < 3; ++i) {
                    outPos[i] = srcPos[i] == dstPos[i]
                        ? srcPos[i]
                        : srcPos[i] * (1.0 - eased) + dstPos[i] * eased;
                }
                return;
            }
            double rotation[2];
            evaluateControlPointVariantLike_0x698454(
                rotation, rotationCurve, eased);
            if(coordinateMode == 0) {
                const double dx = dstPos[0] - srcPos[0];
                const double dy = dstPos[1] - srcPos[1];
                outPos[0] = srcPos[0] + dx * rotation[0] - dy * rotation[1];
                outPos[1] = srcPos[1] + dx * rotation[1] + dy * rotation[0];
                outPos[2] = srcPos[2] == dstPos[2]
                    ? srcPos[2]
                    : srcPos[2] * (1.0 - eased) + dstPos[2] * eased;
            } else if(coordinateMode == 1) {
                const double dx = dstPos[0] - srcPos[0];
                const double dz = dstPos[2] - srcPos[2];
                outPos[0] = srcPos[0] + dx * rotation[0] - dz * rotation[1];
                outPos[1] = srcPos[1] == dstPos[1]
                    ? srcPos[1]
                    : srcPos[1] * (1.0 - eased) + dstPos[1] * eased;
                outPos[2] = srcPos[2] + dz * rotation[0] + dx * rotation[1];
            }
        }

        // Phase-2 frame selection is split to match libkrkr2.so:
        // sub_6926B4/sub_692AB0 advance PSB frameList data into node clip slots,
        // then Player_evaluateTimeline (0x699AE4) consumes those slots and writes
        // node runtime state. These are intentionally non-inline in
        // PlayerUpdateLayers.cpp so native LLDB can hook the 0x699AE4 boundary.
        // 砖5/洞2: optional pendingEvents sink — when non-null, per-node
        // onAction events (node mask 0x40000 / content["act"], fired on each
        // crossed action frame) are pushed as MotionEvent{type=0,
        // param1=node label, param2=action}. Aligned to the per-node sub_6B638C
        // calls in Player_advanceRootAndNodes/rewindRootAndNodes (node[0]=label,
        // see analysis §9). Default null = no firing (read/test callers).
        void
        advanceNodeFrameSelectionLike_0x6926B4(
            detail::MotionNode &node, double currentTime,
            std::vector<detail::MotionEvent> *pendingEvents = nullptr,
            bool doForward = true, bool doBackward = true);

        // The two single-direction inline-seek boundaries the binary runs for
        // NON-parameterized nodes (node+8 == 0) inside the node-deque walk:
        //   • forward inline  @0x6B73DC (in Player_advanceRootAndNodes) — forward
        //     seek only + onAction; reached for forward playback.
        //   • backward inline @0x6BA1CC (in Player_rewindRootAndNodes) — backward
        //     seek only + onAction; reached for reverse playback.
        // Both delegate to advanceNodeFrameSelectionLike_0x6926B4 with the matching
        // direction flag set and events enabled. (The parameterized node+8 != 0
        // path uses advanceNodeFramesLike_0x6B7E44 = forward + corrective-backward,
        // no events.)
        inline void advanceNodeFrameForwardInlineSeekLike_0x6B73DC(
            detail::MotionNode &node, double currentTime,
            std::vector<detail::MotionEvent> *pendingEvents) {
            advanceNodeFrameSelectionLike_0x6926B4(
                node, currentTime, pendingEvents, /*doForward=*/true,
                /*doBackward=*/false);
        }
        inline void advanceNodeFrameBackwardInlineSeekLike_0x6BA1CC(
            detail::MotionNode &node, double currentTime,
            std::vector<detail::MotionEvent> *pendingEvents) {
            advanceNodeFrameSelectionLike_0x6926B4(
                node, currentTime, pendingEvents, /*doForward=*/false,
                /*doBackward=*/true);
        }

        // Player_advanceNodeFrames @ 0x6B7E44 — the binary's per-node 2-slot
        // ping-pong frame seek for PARAMETERIZED nodes (caller branch at
        // Player_advanceRootAndNodes 0x6B73B4 LABEL_104:
        // `if (*(node+8)) Player_advanceNodeFrames(node,player)`). Forward
        // (with corrective backward) seek of the active parsed-frame slot toward
        // the node's CHILD eval time *(node+8)+40 == parameterEntry->value
        // (D-A1 resolved: == frameSelectionTimeLike_0x6B7E44 for parameterized
        // nodes), then re-merge both slots + gated findSource (both subsumed by
        // the live ClipSlot parse + read-pass source refresh). Fires NO per-node
        // onAction (the binary's parameterized path has no action push). Operates
        // on the real MotionNode/ClipSlot state. See PlayerUpdateLayerEval.cpp.
        void advanceNodeFramesLike_0x6B7E44(detail::MotionNode &node,
                                            double currentTime);

        bool evaluateTimelineLike_0x699AE4(detail::MotionNode &node,
                                           bool dirtyArg,
                                           double currentTime);

} // namespace internal
} // namespace motion
