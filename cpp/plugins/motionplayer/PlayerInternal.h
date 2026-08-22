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

namespace motion {
namespace internal {

        constexpr double kMotionFramesPerMillisecond = 60.0 / 1000.0;

        // Combine two render-native packed RGBA weights. The implementation
        // intentionally owns the reference implementation's process-global
        // one-entry cache; callers must not treat it as thread-safe state.
        std::uint32_t multiplyPackedColorWeights_guess(
            std::uint32_t lhs, std::uint32_t rhs);

        // Serialize getCommandList's Bezier-patch division after the native
        // double product.  This keeps the signed int64 narrowing and the
        // unordered floating-point selection out of host-language UB.
        tjs_int64 serializeBezierPatchDivision_guess(
            double scaledDivision);

        // Build the persistent prepared item's signed 32-bit Bezier division
        // from the node's raw unsigned 32-bit field and Player ratio.
        std::int32_t prepareBezierPatchDivision_guess(
            double ratio, std::uint32_t meshDivision);

        // calcViewParam's mesh-chain record uses unsigned 32-bit narrowing
        // before its unsigned cap, unlike the two prepared/command stages.
        std::uint32_t calcViewMeshDivision_guess(
            double ratio, std::uint32_t meshDivision);

        // Parameter ramps compare the two endpoints directly before computing
        // their difference. Discretization uses the four-reference signed-int32
        // FCVTZS/VCVT saturation profile, then ordered min/max/clamp selects.
        // The stripped source name is unavailable, hence the suffix.
        void normalizeParameterValue_guess(
            detail::MotionParameterEntry &entry, double rawValue) noexcept;

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
            return nullptr;
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

        // Resolve a real Layer dispatch through only the Variant's Object and
        // the public Layer ClassID. The four reference builds do not retry via
        // ObjThis, an adaptor owner, or a TJS property at this boundary.
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

        double evaluateVariableTrackEasing_guess(
            const tTJSVariant &curve, double t);

        // Shared by timeline color evaluation and source-clip corner
        // remapping. Equal endpoints return before the curve is inspected.
        std::uint32_t interpolatePackedColor_guess(
            const tTJSVariant &curve, std::uint32_t from,
            std::uint32_t to, double ratio);

        // Evaluate the two-dimensional control curve used by curved position
        // interpolation. Dispatch order and temporary Variant lifetimes are
        // observable and are intentionally expressed directly.
        inline void evaluatePositionControlCurve_guess(
            double outXY[2], const tTJSVariant &curve, double inputT) {
            const tTJSVariant mainX = detail::motionPropGet(
                curve, TJS_W("x"), 0, &detail::xMemberHint_guess);
            const tTJSVariant mainY = detail::motionPropGet(
                curve, TJS_W("y"), 0, &detail::yMemberHint_guess);
            const tTJSVariant knots = detail::motionPropGet(
                curve, TJS_W("t"), 0,
                &detail::positionControlTMemberHint_guess);
            const tTJSVariant segments = detail::motionPropGet(
                curve, TJS_W("s"), 0,
                &detail::positionControlSMemberHint_guess);

            int mainIndex = -3;
            int segmentIndex = -1;
            double nextKnot;
            do {
                nextKnot = detail::motionPropGetDoubleByNum(
                    knots, segmentIndex + 2);
                mainIndex += 3;
                ++segmentIndex;
            } while(nextKnot < inputT);

            const double knotStart = detail::motionPropGetDoubleByNum(
                knots, segmentIndex);
            const double knotEnd = detail::motionPropGetDoubleByNum(
                knots, segmentIndex + 1);
            const double knotStartForDenominator =
                detail::motionPropGetDoubleByNum(knots, segmentIndex);

            double parameter;
            {
                const tTJSVariant segment = detail::motionPropGetByNum(
                    segments, segmentIndex);
                const tTJSVariant splineX = detail::motionPropGet(
                    segment, TJS_W("x"), 0,
                    &detail::xMemberHint_guess);
                const tTJSVariant splineY = detail::motionPropGet(
                    segment, TJS_W("y"), 0,
                    &detail::yMemberHint_guess);
                const tTJSVariant splineP = detail::motionPropGet(
                    segment, TJS_W("p"), 0,
                    &detail::positionControlPMemberHint_guess);

                const double firstX =
                    detail::motionPropGetDoubleByNum(splineX, 0);
                const int splineCount = detail::motionPropGetCount(splineX);
                const double localT = (inputT - knotStart) /
                    (knotEnd - knotStartForDenominator);
                if(firstX >= localT) {
                    parameter = detail::motionPropGetDoubleByNum(splineY, 0);
                } else {
                    const int last = splineCount - 1;
                    if(detail::motionPropGetDoubleByNum(
                           splineX, last) <= localT) {
                        parameter = detail::motionPropGetDoubleByNum(
                            splineY, last);
                    } else {
                        int splineIndex = -1;
                        double nextX;
                        do {
                            nextX = detail::motionPropGetDoubleByNum(
                                splineX, splineIndex + 2);
                            ++splineIndex;
                        } while(nextX < localT);

                        const double x1 = detail::motionPropGetDoubleByNum(
                            splineX, splineIndex + 1);
                        const double x0ForDenominator =
                            detail::motionPropGetDoubleByNum(
                                splineX, splineIndex);
                        const double x0ForRatio =
                            detail::motionPropGetDoubleByNum(
                                splineX, splineIndex);
                        const double p1 = detail::motionPropGetDoubleByNum(
                            splineP, splineIndex + 1);
                        const double p0 = detail::motionPropGetDoubleByNum(
                            splineP, splineIndex);
                        const double y1 = detail::motionPropGetDoubleByNum(
                            splineY, splineIndex + 1);
                        const double y0 = detail::motionPropGetDoubleByNum(
                            splineY, splineIndex);
                        const double ratio = (localT - x0ForRatio) /
                            (x1 - x0ForDenominator);
                        const double deltaX = x1 - x0ForDenominator;
                        parameter = deltaX *
                                (deltaX *
                                 ((ratio * (ratio * ratio) - ratio) * p1 +
                                  ((1.0 - ratio) *
                                       ((1.0 - ratio) * (1.0 - ratio)) -
                                   (1.0 - ratio)) * p0)) /
                                6.0 +
                            ratio * y1 + (1.0 - ratio) * y0;
                    }
                }
            }

            double x[4];
            double y[4];
            for(int i = 0; i < 4; ++i) {
                x[i] = detail::motionPropGetDoubleByNum(mainX, mainIndex);
                y[i] = detail::motionPropGetDoubleByNum(mainY, mainIndex);
                ++mainIndex;
            }

            const double oneMinus = 1.0 - parameter;
            const double oneMinusTimesThree = oneMinus * 3.0;
            const double w0 = oneMinus * (oneMinus * oneMinus);
            const double w1 = parameter * (oneMinus * oneMinusTimesThree);
            const double w2 = parameter * (parameter * oneMinusTimesThree);
            const double w3 = parameter * (parameter * parameter);
            outXY[0] = w0 * x[0] + w1 * x[1] +
                w2 * x[2] + w3 * x[3];
            outXY[1] = w0 * y[0] + w1 * y[1] +
                w2 * y[2] + w3 * y[3];
        }

        // Interpolate one position sample from the two clip slots. Unknown
        // coordinate modes with a control curve deliberately leave outPos
        // untouched.
        inline void evaluatePositionInterpolation_guess(
            const tTJSVariant &easingCurve,
            const double dstPos[3], const double srcPos[3], double outPos[3],
            int coordinateMode, const tTJSVariant &rotationCurve, double t) {
            if(srcPos[0] == dstPos[0] && srcPos[1] == dstPos[1] &&
               srcPos[2] == dstPos[2]) {
                outPos[0] = srcPos[0];
                outPos[1] = srcPos[1];
                outPos[2] = srcPos[2];
                return;
            }
            const double eased = easingCurve.Type() != tvtVoid
                ? evaluateVariableTrackEasing_guess(easingCurve, t) : t;

            // This default-constructed Variant and its type test are present in
            // all four references. It remains Void, but its dead branch and
            // destructor are part of the recovered source shape.
            const tTJSVariant secondaryEasing_guess;
            if(rotationCurve.Type() == tvtVoid) {
                outPos[0] = srcPos[0] == dstPos[0]
                    ? srcPos[0]
                    : (1.0 - eased) * srcPos[0] + eased * dstPos[0];
                outPos[1] = srcPos[1] == dstPos[1]
                    ? srcPos[1]
                    : dstPos[1] * eased + srcPos[1] * (1.0 - eased);
                outPos[2] = srcPos[2] == dstPos[2]
                    ? srcPos[2]
                    : dstPos[2] * eased + srcPos[2] * (1.0 - eased);
                return;
            }
            double rotation[2];
            evaluatePositionControlCurve_guess(
                rotation, rotationCurve, eased);
            if(coordinateMode == 0) {
                const double dx = dstPos[0] - srcPos[0];
                const double dy = dstPos[1] - srcPos[1];
                outPos[0] = srcPos[0] + dx * rotation[0] - dy * rotation[1];
                outPos[1] = srcPos[1] +
                    (dx * rotation[1] + dy * rotation[0]);
                if(srcPos[2] == dstPos[2]) {
                    outPos[2] = srcPos[2];
                } else {
                    double axisEasing = eased;
                    if(secondaryEasing_guess.Type() != tvtVoid) {
                        axisEasing = evaluateVariableTrackEasing_guess(
                            secondaryEasing_guess, axisEasing);
                    }
                    outPos[2] = dstPos[2] * axisEasing +
                        srcPos[2] * (1.0 - axisEasing);
                }
            } else if(coordinateMode == 1) {
                const double dx = dstPos[0] - srcPos[0];
                const double dz = dstPos[2] - srcPos[2];
                outPos[0] = srcPos[0] + dx * rotation[0] - dz * rotation[1];
                if(srcPos[1] == dstPos[1]) {
                    outPos[1] = srcPos[1];
                } else {
                    double axisEasing = eased;
                    if(secondaryEasing_guess.Type() != tvtVoid) {
                        axisEasing = evaluateVariableTrackEasing_guess(
                            secondaryEasing_guess, axisEasing);
                    }
                    outPos[1] = dstPos[1] * axisEasing +
                        srcPos[1] * (1.0 - axisEasing);
                }
                outPos[2] = dz * rotation[0] + srcPos[2] +
                    dx * rotation[1];
            }
        }

        // Shared node-frame selection primitive. It advances PSB frame-list data
        // into the node's two clip slots; the later timeline-evaluation pass
        // consumes those slots and writes runtime state. When eventOwner is
        // non-null, each crossed action frame queues
        // MotionEvent{type=ACTION, param1=node label, param2=action}.
        bool
        seekNodeFrameSelection_guess(
            detail::MotionNode &node, double currentTime,
            Player *eventOwner = nullptr,
            bool doForward = true, bool doBackward = true);

        // Absolute node timeline initializer. The native helper owns selection,
        // both parse/merge calls, source refresh, and the exact-frame action tail.
        void initializeNodeTimelineSlots_guess(
            Player &player, detail::MotionNode &node);

        // Shared two-slot ping-pong stepper for parameterized nodes. It seeks
        // toward parameterEntry->value with a forward pass plus corrective
        // backward pass and emits no per-node action events.
        void seekParameterizedNodeFrames_guess(detail::MotionNode &node,
                                                Player &player);

        // Value result of the ARM FCVTZU/VCVT.U32.F64 instruction boundary
        // shared by timeline `ti` quantization and rounded opacity. Floating-
        // point exception flags are not observable in the Web port.
        std::uint32_t doubleToUnsignedIntTowardZeroSaturated_guess(
            double value);

        bool evaluateTimeline_guess(detail::MotionNode &node,
                                    double currentTime,
                                    bool dirtyArg);

        void propagateParticleEvaluatedOpacityToChildRoot_guess(
            const detail::MotionNode &particleNode,
            detail::MotionNode &childRoot);

        // Numeric core of the four-reference particle deleteOutside viewport.
        // `targetRect` is [left, top, right, bottom] in target coordinates;
        // affine translations are the root Player's float draw-affine pair plus
        // its float camera-offset pair. The inverse is deliberately unguarded.
        std::array<float, 4> computeParticleOutsideRect_guess(
            const std::array<float, 4> &targetRect,
            double m11, double m12, double m21, double m22,
            float affineTranslateX, float affineTranslateY,
            float cameraOffsetX, float cameraOffsetY,
            double outsideFactor);

        // Ordered native predicate used by the first pass of the particle
        // worker. Valid child bounds must strictly overlap the cached outside
        // rectangle; touching an edge is outside. Ordered inverted bounds are
        // retained, while unordered values fail the strict-overlap chain.
        bool particleBoundsStrictlyOverlapOutsideRect_guess(
            double boundsMinX, double boundsMinY,
            double boundsMaxX, double boundsMaxY,
            const std::array<float, 4> &outsideRect);

} // namespace internal
} // namespace motion
