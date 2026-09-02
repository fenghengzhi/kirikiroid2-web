#include "MotionLayerExtensions.h"

#include "LayerIntf.h"
#include "MotionDispatch.h"
#include "MotionRenderBackend.h"
#include "RenderManager.h"
#include "RuntimeSupport.h"
#include "ncbind.hpp"
#include "tjsDictionary.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace motion {
    namespace {
        using PointList_guess = std::vector<tTVPPointD>;

        // The debug and public frame paths share these two process-lifetime
        // backing words.  They are a MotionLayerExtensions-local family, not
        // Player's distinct singular "drawLine" member hint.
        tjs_uint32 drawLinesMemberHint_guess = 0;
        tjs_uint32 drawBeziersMemberHint_guess = 0;
        tjs_uint32 motionLayerClipLeftMemberHint_guess = 0;
        tjs_uint32 motionLayerClipTopMemberHint_guess = 0;
        tjs_uint32 motionLayerClipWidthMemberHint_guess = 0;
        tjs_uint32 motionLayerClipHeightMemberHint_guess = 0;
        tjs_uint32 motionLayerHoldAlphaMemberHint_guess = 0;

        struct DispatchReference_guess {
            explicit DispatchReference_guess(iTJSDispatch2 *value) :
                value(value) {
                value->AddRef();
            }
            ~DispatchReference_guess() {
                value->Release();
            }

            iTJSDispatch2 *value;
        };

        std::int32_t signedInt32Bits_guess(std::uint32_t bits) {
            std::int32_t value;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }

        std::int32_t multipliedGridPointCount_guess(tjs_int divisionX,
                                                    tjs_int divisionY) {
            const std::uint32_t columns =
                static_cast<std::uint32_t>(divisionX) + 1u;
            const std::uint32_t rows =
                static_cast<std::uint32_t>(divisionY) + 1u;
            return signedInt32Bits_guess(columns * rows);
        }

        tjs_int addInt32_guess(tjs_int lhs, tjs_int rhs) {
            return signedInt32Bits_guess(
                static_cast<std::uint32_t>(lhs) +
                static_cast<std::uint32_t>(rhs));
        }

        tjs_real readMeshCoordinate_guess(ncbPropAccessor &points,
                                          tjs_int index) {
            // The first indexed access is only a MEMBERMUSTEXIST probe.  A
            // successful probe is followed by a second indexed access for the
            // real conversion; failed probes contribute zero.
            if(!points.HasValue(index)) {
                return 0.0;
            }
            return points.GetValue(
                index, ncbTypedefs::Tag<tjs_real>());
        }

        PointList_guess readMeshPoints_guess(ncbPropAccessor &points) {
            const tjs_int coordinateCount = points.GetArrayCount();
            PointList_guess result;

            // This unsigned gate is deliberately not expressed as
            // coordinateCount >= 2. Negative custom count-property results
            // enter the allocation path in the references as well.
            if(static_cast<std::uint32_t>(coordinateCount) + 1u >= 3u) {
                const tjs_int pointCount = coordinateCount / 2;
                result.reserve(static_cast<std::size_t>(pointCount));
                for(tjs_int pointIndex = 0; pointIndex < pointCount;
                    ++pointIndex) {
                    result.push_back({
                        readMeshCoordinate_guess(points, pointIndex * 2),
                        readMeshCoordinate_guess(points,
                                                 pointIndex * 2 + 1),
                    });
                }
            }
            return result;
        }

        void readBezierControlPoints_guess(ncbPropAccessor &points,
                                           PointList_guess &result,
                                           bool probeBeforeRead) {
            for(tjs_int controlIndex = 0; controlIndex != 16;
                ++controlIndex) {
                const tjs_int xIndex = controlIndex * 2;
                const tjs_int yIndex = xIndex + 1;
                if(probeBeforeRead) {
                    result.push_back({
                        readMeshCoordinate_guess(points, xIndex),
                        readMeshCoordinate_guess(points, yIndex),
                    });
                } else {
                    // The patch-mesh helper does not probe either the array
                    // count or MEMBERMUSTEXIST before these indexed reads.
                    result.push_back({
                        points.GetValue(xIndex,
                                        ncbTypedefs::Tag<tjs_real>()),
                        points.GetValue(yIndex,
                                        ncbTypedefs::Tag<tjs_real>()),
                    });
                }
            }
        }

        void parseAndTessellateBezierPatch_guess(
            tTJSVariant flatControlPoints,
            tjs_int divisionX,
            tjs_int divisionY,
            PointList_guess &controlPoints,
            PointList_guess &result) {
            controlPoints.reserve(16);

            // The native expression is a signed 32-bit multiplication before
            // vector::reserve widens it.  Rebuild its two's-complement result
            // explicitly so overflow does not become C++ undefined behavior.
            result.reserve(static_cast<std::size_t>(
                multipliedGridPointCount_guess(divisionX, divisionY)));

            ncbPropAccessor points(flatControlPoints);
            readBezierControlPoints_guess(points, controlPoints, false);

            const auto &basisX =
                render_backend_guess::cubicBezierBasisTable_guess(divisionX);
            const auto &basisY =
                render_backend_guess::cubicBezierBasisTable_guess(divisionY);
            if(divisionY < 0) {
                return;
            }

            for(std::int64_t y = 0;; ++y) {
                if(divisionX >= 0) {
                    for(std::int64_t x = 0;; ++x) {
                        tTVPPointD point{0.0, 0.0};
                        for(int controlIndex = 0; controlIndex != 16;
                            ++controlIndex) {
                            const double weight =
                                basisY[static_cast<std::size_t>(y)]
                                      [static_cast<std::size_t>(
                                          controlIndex / 4)] *
                                basisX[static_cast<std::size_t>(x)]
                                      [static_cast<std::size_t>(
                                          controlIndex % 4)];
                            point.x = point.x +
                                weight * controlPoints[controlIndex].x;
                            point.y = point.y +
                                weight * controlPoints[controlIndex].y;
                        }
                        result.push_back(point);
                        if(x == divisionX) {
                            break;
                        }
                    }
                }
                if(y == divisionY) {
                    break;
                }
            }
        }

        void appendPoint_guess(detail::TJSArrayWithItems_guess &points,
                               const tTVPPointD &point) {
            auto pair = detail::createTJSArrayWithItems_guess();
            pair.items->emplace_back(static_cast<tjs_real>(point.x));
            pair.items->emplace_back(static_cast<tjs_real>(point.y));
            points.items->push_back(pair.value);
        }

        void appendFlatPoint_guess(
            detail::TJSArrayWithItems_guess &points,
            const tTVPPointD &point) {
            points.items->emplace_back(static_cast<tjs_real>(point.x));
            points.items->emplace_back(static_cast<tjs_real>(point.y));
        }

        struct PatchBounds_guess {
            tjs_real left = std::numeric_limits<tjs_real>::max();
            tjs_real top = std::numeric_limits<tjs_real>::max();
            tjs_real right = -std::numeric_limits<tjs_real>::max();
            tjs_real bottom = -std::numeric_limits<tjs_real>::max();
        };

        void includePatchPoint_guess(PatchBounds_guess &bounds,
                                     tjs_real x,
                                     tjs_real y) {
            // Keep the four strict comparisons separate.  In particular,
            // NaNs do not update any extremum in the reference callbacks.
            if(x < bounds.left) {
                bounds.left = x;
            }
            if(y < bounds.top) {
                bounds.top = y;
            }
            if(x > bounds.right) {
                bounds.right = x;
            }
            if(y > bounds.bottom) {
                bounds.bottom = y;
            }
        }

        tTJSVariant makePatchBoundsDictionary_guess(
            const PatchBounds_guess &bounds,
            bool duplicateLeftWrite) {
            iTJSDispatch2 *dispatch = TJSCreateDictionaryObject();
            tTJSVariant result(dispatch, dispatch);
            dispatch->Release();

            ncbPropAccessor properties(result);
            properties.SetValue(
                TJS_W("left"), bounds.left,
                TJS_MEMBERENSURE, &detail::leftMemberHint_guess);
            if(duplicateLeftWrite) {
                // calcMeshBounds in every reference performs this identical
                // second PropSet, including the same member-hint storage.
                properties.SetValue(
                    TJS_W("left"), bounds.left,
                    TJS_MEMBERENSURE, &detail::leftMemberHint_guess);
            }
            properties.SetValue(
                TJS_W("top"), bounds.top,
                TJS_MEMBERENSURE, &detail::topMemberHint_guess);
            properties.SetValue(
                TJS_W("right"), bounds.right,
                TJS_MEMBERENSURE, &detail::rightMemberHint_guess);
            properties.SetValue(
                TJS_W("bottom"), bounds.bottom,
                TJS_MEMBERENSURE, &detail::bottomMemberHint_guess);
            properties.SetValue(
                TJS_W("width"), bounds.right - bounds.left,
                TJS_MEMBERENSURE, &detail::widthMemberHint_guess);
            properties.SetValue(
                TJS_W("height"), bounds.bottom - bounds.top,
                TJS_MEMBERENSURE, &detail::heightMemberHint_guess);
            return result;
        }

        using CubicBezierWeights_guess = std::array<tjs_real, 4>;

        CubicBezierWeights_guess
        cubicBezierSingleHorizontalWeights_guess(tjs_real t) {
            const tjs_real complement = 1.0 - t;
            return {
                complement * (complement * complement),
                complement * complement * t * 3.0,
                complement * t * t * 3.0,
                t * t * t,
            };
        }

        CubicBezierWeights_guess
        cubicBezierSingleVerticalWeights_guess(tjs_real t) {
            const tjs_real complement = 1.0 - t;
            return {
                complement * (complement * complement),
                complement * complement * t * 3.0,
                complement * 3.0 * t * t,
                t * t * t,
            };
        }

        CubicBezierWeights_guess
        cubicBezierListHorizontalWeights_guess(tjs_real t) {
            const tjs_real complement = 1.0 - t;
            return {
                complement * (complement * complement),
                t * (complement * complement) * 3.0,
                t * (t * complement) * 3.0,
                t * (t * t),
            };
        }

        CubicBezierWeights_guess
        cubicBezierListVerticalWeights_guess(tjs_real t) {
            const tjs_real complement = 1.0 - t;
            return {
                complement * (complement * complement),
                t * (complement * complement) * 3.0,
                t * (t * (complement * 3.0)),
                t * (t * t),
            };
        }

        tTVPPointD calculateBezierPatchPoint_guess(
            const PointList_guess &controlPoints,
            const CubicBezierWeights_guess &horizontal,
            const CubicBezierWeights_guess &vertical) {
            // This is intentionally not value-initialized.  All four native
            // callbacks start both += accumulators from indeterminate local
            // state; the exact residue differs by ABI and optimization.  It
            // is an original source-level undefined behavior, not a missing
            // zeroing operation in this reconstruction.
            tTVPPointD result;
            for(int controlIndex = 0; controlIndex != 16;
                ++controlIndex) {
                const tjs_real weight =
                    vertical[static_cast<std::size_t>(controlIndex / 4)] *
                    horizontal[static_cast<std::size_t>(controlIndex % 4)];
                result.x += weight * controlPoints[controlIndex].x;
                result.y += weight * controlPoints[controlIndex].y;
            }
            return result;
        }

        bool pointInTriangle_guess(const tTVPPointD &first,
                                   const tTVPPointD &second,
                                   const tTVPPointD &third,
                                   const tTVPPointD &target) {
            const tTVPPointD vertices[] = {first, second, third};
            const tjs_real orientation =
                (first.y - second.y) * third.x +
                (second.x - first.x) * third.y -
                ((first.y - second.y) * second.x +
                 second.y * (second.x - first.x));
            const tjs_real sign = orientation < 0.0 ? -1.0 : 1.0;

            for(int edge = 0; edge != 3; ++edge) {
                const auto &current = vertices[edge];
                const auto &next = vertices[(edge + 1) % 3];
                const tjs_real line =
                    (next.y - current.y) * target.x +
                    (current.x - next.x) * target.y -
                    (current.x * (next.y - current.y) +
                     current.y * (current.x - next.x));
                if(sign * line > 0.0) {
                    return false;
                }
            }
            return true;
        }

        bool reverseAffineTriangle_guess(
            tTJSVariant &result,
            const tTVPPointD &first,
            const tTVPPointD &second,
            const tTVPPointD &third,
            const tTVPPointD &firstParameter,
            const tTVPPointD &secondParameter,
            const tTVPPointD &thirdParameter,
            const tTVPPointD &target) {
            const tjs_real secondX = second.x - first.x;
            const tjs_real secondY = second.y - first.y;
            const tjs_real thirdX = third.x - first.x;
            const tjs_real thirdY = third.y - first.y;
            const tjs_real determinant =
                secondX * thirdY - secondY * thirdX;
            if(determinant == 0.0) {
                return false;
            }

            const tjs_real targetX = target.x - first.x;
            const tjs_real targetY = target.y - first.y;
            const auto mappedX = firstParameter.x +
                (secondX * (thirdParameter.x - firstParameter.x) -
                 thirdX * (secondParameter.x - firstParameter.x)) /
                    determinant * targetY +
                (thirdY * (secondParameter.x - firstParameter.x) -
                 secondY * (thirdParameter.x - firstParameter.x)) /
                    determinant * targetX;
            const auto mappedY = firstParameter.y +
                (secondX * (thirdParameter.y - firstParameter.y) -
                 thirdX * (secondParameter.y - firstParameter.y)) /
                    determinant * targetY +
                (thirdY * (secondParameter.y - firstParameter.y) -
                 secondY * (thirdParameter.y - firstParameter.y)) /
                    determinant * targetX;

            auto values = detail::createTJSArrayWithItems_guess();
            values.items->emplace_back(mappedX);
            values.items->emplace_back(mappedY);
            result = values.value;
            return true;
        }

        void callLayerDrawingMethod_guess(iTJSDispatch2 *layerClass,
                                          iTJSDispatch2 *owner,
                                          const tjs_char *method,
                                          tjs_uint32 *hint,
                                          const tTJSVariant &appearance,
                                          const tTJSVariant &points) {
            tTJSVariant appearanceArg(appearance);
            tTJSVariant pointsArg(points);
            tTJSVariant result;
            tTJSVariant *args[] = {&appearanceArg, &pointsArg};
            (void)layerClass->FuncCall(
                0, method, hint, &result, 2, args, owner);
        }

        void callLayerRectMethod_guess(iTJSDispatch2 *owner,
                                       const tjs_char *method,
                                       tjs_uint32 *hint,
                                       const tTVPRect &rect,
                                       tjs_int value,
                                       bool includeValue) {
            tTJSVariant left(static_cast<tjs_int64>(rect.left));
            tTJSVariant top(static_cast<tjs_int64>(rect.top));
            tTJSVariant width(
                static_cast<tjs_int64>(rect.right) - rect.left);
            tTJSVariant height(
                static_cast<tjs_int64>(rect.bottom) - rect.top);
            tTJSVariant valueArg(value);
            tTJSVariant result;
            tTJSVariant *args[] = {
                &left, &top, &width, &height, &valueArg,
            };
            (void)owner->FuncCall(
                0, method, hint, &result, includeValue ? 5 : 4,
                args, owner);
        }

        void clearWholeLayer_guess(iTJSDispatch2 *owner) {
            ncbPropAccessor ownerAccessor(owner);
            const tjs_int neutralColor = ownerAccessor.GetValue(
                TJS_W("neutralColor"), ncbTypedefs::Tag<tjs_int>(),
                0, &detail::neutralColorMemberHint_guess);
            const tjs_int height = ownerAccessor.GetValue(
                TJS_W("height"), ncbTypedefs::Tag<tjs_int>(),
                0, &detail::heightMemberHint_guess);
            const tjs_int width = ownerAccessor.GetValue(
                TJS_W("width"), ncbTypedefs::Tag<tjs_int>(),
                0, &detail::widthMemberHint_guess);
            callLayerRectMethod_guess(
                owner, TJS_W("fillRect"),
                &detail::fillRectMemberHint_guess,
                tTVPRect(0, 0, width, height), neutralColor, true);
        }

        tTJSNI_Layer *layerFromVariant_guess(const tTJSVariant &value) {
            return tTJSNI_Layer::FromVariant(value);
        }

        tTJSNI_Layer *layerFromObject_guess(iTJSDispatch2 *value) {
            return tTJSNI_Layer::FromObject(value);
        }

        void drawGridFrame_guess(iTJSDispatch2 *layer,
                                 iTJSDispatch2 *owner,
                                 const tTJSVariant &outline,
                                 const tTJSVariant &meshline,
                                 const PointList_guess &points,
                                 tjs_int divisionX,
                                 tjs_int divisionY) {
            if(divisionY >= 0) {
                for(std::int64_t y = 0;; ++y) {
                    const tTJSVariant &appearance =
                        y == 0 || y == divisionY ? outline : meshline;
                    if(appearance.Type() != tvtVoid) {
                        auto line = detail::createTJSArrayWithItems_guess();
                        if(divisionX >= 0) {
                            for(std::int64_t x = 0;; ++x) {
                                const auto pointIndex =
                                    y * (static_cast<std::int64_t>(divisionX) +
                                         1) +
                                    x;
                                appendPoint_guess(
                                    line,
                                    points[static_cast<std::size_t>(
                                        pointIndex)]);
                                if(x == divisionX) {
                                    break;
                                }
                            }
                        }
                        callLayerDrawingMethod_guess(
                            layer, owner, TJS_W("drawLines"),
                            &drawLinesMemberHint_guess,
                            appearance, line.value);
                    }
                    if(y == divisionY) {
                        break;
                    }
                }
            }

            if(divisionX >= 0) {
                for(std::int64_t x = 0;; ++x) {
                    const tTJSVariant &appearance =
                        x == 0 || x == divisionX ? outline : meshline;
                    if(appearance.Type() != tvtVoid) {
                        auto line = detail::createTJSArrayWithItems_guess();
                        if(divisionY >= 0) {
                            for(std::int64_t y = 0;; ++y) {
                                const auto pointIndex =
                                    y * (static_cast<std::int64_t>(divisionX) +
                                         1) +
                                    x;
                                appendPoint_guess(
                                    line,
                                    points[static_cast<std::size_t>(
                                        pointIndex)]);
                                if(y == divisionY) {
                                    break;
                                }
                            }
                        }
                        callLayerDrawingMethod_guess(
                            layer, owner, TJS_W("drawLines"),
                            &drawLinesMemberHint_guess,
                            appearance, line.value);
                    }
                    if(x == divisionX) {
                        break;
                    }
                }
            }
        }

        void drawGridDebug_guess(iTJSDispatch2 *owner,
                                 const tTJSVariant &appearance,
                                 const PointList_guess &points,
                                 tjs_int divisionX,
                                 tjs_int divisionY) {
            if(appearance.Type() == tvtVoid) {
                return;
            }

            ncbPropAccessor layerClass(TJS_W("Layer"));
            iTJSDispatch2 *layer = layerClass.GetDispatch();
            auto line = detail::createTJSArrayWithItems_guess();

            if(divisionY >= 0) {
                for(std::int64_t y = 0;; ++y) {
                    line.items->clear();
                    if(divisionX >= 0) {
                        for(std::int64_t x = 0;; ++x) {
                            const auto pointIndex =
                                y * (static_cast<std::int64_t>(divisionX) +
                                     1) +
                                x;
                            appendPoint_guess(
                                line,
                                points[static_cast<std::size_t>(pointIndex)]);
                            if(x == divisionX) {
                                break;
                            }
                        }
                    }
                    callLayerDrawingMethod_guess(
                        layer, owner, TJS_W("drawLines"),
                        &drawLinesMemberHint_guess,
                        appearance, line.value);
                    if(y == divisionY) {
                        break;
                    }
                }
            }

            if(divisionX >= 0) {
                for(std::int64_t x = 0;; ++x) {
                    line.items->clear();
                    if(divisionY >= 0) {
                        for(std::int64_t y = 0;; ++y) {
                            const auto pointIndex =
                                y * (static_cast<std::int64_t>(divisionX) +
                                     1) +
                                x;
                            appendPoint_guess(
                                line,
                                points[static_cast<std::size_t>(pointIndex)]);
                            if(y == divisionY) {
                                break;
                            }
                        }
                    }
                    callLayerDrawingMethod_guess(
                        layer, owner, TJS_W("drawLines"),
                        &drawLinesMemberHint_guess,
                        appearance, line.value);
                    if(x == divisionX) {
                        break;
                    }
                }
            }
        }

        tTVPPointD evaluateBezierRow_guess(
            const PointList_guess &controlPoints,
            const std::vector<double> &weights,
            int row) {
            tTVPPointD point{0.0, 0.0};
            for(int column = 0; column != 4; ++column) {
                const auto &control = controlPoints[row * 4 + column];
                point.x = point.x + weights[column] * control.x;
                point.y = point.y + weights[column] * control.y;
            }
            return point;
        }

        tTVPPointD evaluateBezierColumn_guess(
            const PointList_guess &controlPoints,
            const std::vector<double> &weights,
            int column) {
            tTVPPointD point{0.0, 0.0};
            for(int row = 0; row != 4; ++row) {
                const auto &control = controlPoints[row * 4 + column];
                point.x = point.x + weights[row] * control.x;
                point.y = point.y + weights[row] * control.y;
            }
            return point;
        }

        void drawBezierControlFrame_guess(
            iTJSDispatch2 *owner,
            const tTJSVariant &appearance,
            const PointList_guess &controlPoints) {
            if(appearance.Type() == tvtVoid) {
                return;
            }

            DispatchReference_guess ownerReference(owner);
            ncbPropAccessor layerClass(TJS_W("Layer"));
            iTJSDispatch2 *layer = layerClass.GetDispatch();
            const auto &basis =
                render_backend_guess::cubicBezierBasisTable_guess(3);

            for(int sample = 0; sample != 4; ++sample) {
                auto curve = detail::createTJSArrayWithItems_guess();
                for(int row = 0; row != 4; ++row) {
                    appendPoint_guess(
                        curve,
                        evaluateBezierRow_guess(
                            controlPoints, basis[sample], row));
                }
                callLayerDrawingMethod_guess(
                    layer, owner, TJS_W("drawBeziers"),
                    &drawBeziersMemberHint_guess,
                    appearance, curve.value);
            }

            for(int sample = 0; sample != 4; ++sample) {
                auto curve = detail::createTJSArrayWithItems_guess();
                for(int column = 0; column != 4; ++column) {
                    appendPoint_guess(
                        curve,
                        evaluateBezierColumn_guess(
                            controlPoints, basis[sample], column));
                }
                callLayerDrawingMethod_guess(
                    layer, owner, TJS_W("drawBeziers"),
                    &drawBeziersMemberHint_guess,
                    appearance, curve.value);
            }
        }

        bool submitLayerMesh_guess(
            iTVPRenderManager *submitManager,
            tTVPBaseTexture *targetBitmap,
            const tTVPRect &clipRect,
            iTVPTexture2D *sourceTexture,
            const tTVPRect &sourceRect,
            const PointList_guess &boundsPoints,
            const PointList_guess &meshPoints,
            tjs_int divisionX,
            tjs_int divisionY,
            iTVPRenderMethod *method,
            tjs_int stretchType) {
            // Native callers pass a null manager; the shared submit helper
            // resolves its own snapshot before touching the guarded static.
            if(!submitManager) submitManager = TVPGetRenderManager();
            static int stretchTypeParameterId_guess =
                submitManager->EnumParameterID("StretchType");
            submitManager->SetParameterInt(
                stretchTypeParameterId_guess,
                static_cast<unsigned short>(stretchType));

            tTVPRect computedBounds(clipRect);
            return render_backend_guess::buildAndSubmitMeshTriangles_guess(
                computedBounds, sourceTexture, sourceRect,
                boundsPoints, meshPoints, divisionX, divisionY,
                [&](iTVPTexture2D *submittedSourceTexture,
                    const PointList_guess &sourceVertices,
                    const PointList_guess &destinationVertices) {
                    auto *referenceTexture = targetBitmap->GetTexture();
                    auto *targetTexture =
                        targetBitmap->GetTextureForRender(
                            method->IsBlendTarget(), &clipRect);
                    tRenderTexQuadArray::Element textures[] = {
                        tRenderTexQuadArray::Element(
                            submittedSourceTexture,
                            sourceVertices.data())
                    };
                    submitManager->OperateTriangles(
                        method,
                        static_cast<int>(destinationVertices.size() / 3u),
                        targetTexture, referenceTexture, clipRect,
                        destinationVertices.data(),
                        tRenderTexQuadArray(textures));
                });
        }
    } // namespace

    tTJSVariant BezierPatch::affinePatch(
        tTJSVariant flatPoints,
        tjs_real m11,
        tjs_real m12,
        tjs_real m21,
        tjs_real m22) {
        auto result = detail::createTJSArrayWithItems_guess();
        ncbPropAccessor points(flatPoints);
        const std::uint32_t coordinateCount =
            static_cast<std::uint32_t>(points.GetCount());
        for(std::uint32_t index = 0; index < coordinateCount;
            index += 2u) {
            const tjs_real x = readMeshCoordinate_guess(
                points, signedInt32Bits_guess(index));
            const tjs_real y = readMeshCoordinate_guess(
                points, signedInt32Bits_guess(index + 1u));
            result.items->emplace_back(x * m11 + y * m21);
            result.items->emplace_back(x * m12 + y * m22);
        }
        return result.value;
    }

    tTJSVariant BezierPatch::translatePatch(
        tTJSVariant flatPoints,
        tjs_real offsetX,
        tjs_real offsetY) {
        auto result = detail::createTJSArrayWithItems_guess();
        ncbPropAccessor points(flatPoints);
        const std::uint32_t coordinateCount =
            static_cast<std::uint32_t>(points.GetCount());
        for(std::uint32_t index = 0; index < coordinateCount;
            index += 2u) {
            const tjs_real x = readMeshCoordinate_guess(
                points, signedInt32Bits_guess(index));
            const tjs_real y = readMeshCoordinate_guess(
                points, signedInt32Bits_guess(index + 1u));
            result.items->emplace_back(x + offsetX);
            result.items->emplace_back(y + offsetY);
        }
        return result.value;
    }

    tTJSVariant BezierPatch::affineTranslatePatch(
        tTJSVariant flatPoints,
        tjs_real m11,
        tjs_real m12,
        tjs_real m21,
        tjs_real m22,
        tjs_real offsetX,
        tjs_real offsetY) {
        auto result = detail::createTJSArrayWithItems_guess();
        ncbPropAccessor points(flatPoints);
        const std::uint32_t coordinateCount =
            static_cast<std::uint32_t>(points.GetCount());
        for(std::uint32_t index = 0; index < coordinateCount;
            index += 2u) {
            const tjs_real x = readMeshCoordinate_guess(
                points, signedInt32Bits_guess(index));
            const tjs_real y = readMeshCoordinate_guess(
                points, signedInt32Bits_guess(index + 1u));
            result.items->emplace_back(
                x * m11 + y * m21 + offsetX);
            result.items->emplace_back(
                x * m12 + y * m22 + offsetY);
        }
        return result.value;
    }

    tTJSVariant BezierPatch::calcPatchBounds(tTJSVariant flatPoints) {
        ncbPropAccessor points(flatPoints);
        const std::uint32_t coordinateCount =
            static_cast<std::uint32_t>(points.GetCount());
        PatchBounds_guess bounds;
        for(std::uint32_t index = 0; index < coordinateCount;
            index += 2u) {
            const tjs_real x = readMeshCoordinate_guess(
                points, signedInt32Bits_guess(index));
            const tjs_real y = readMeshCoordinate_guess(
                points, signedInt32Bits_guess(index + 1u));
            includePatchPoint_guess(bounds, x, y);
        }
        return makePatchBoundsDictionary_guess(bounds, false);
    }

    tTJSVariant BezierPatch::calcMeshBounds(
        tTJSVariant flatControlPoints) {
        PointList_guess controlPoints;
        PointList_guess points;
        parseAndTessellateBezierPatch_guess(
            flatControlPoints, 10, 10, controlPoints, points);

        PatchBounds_guess bounds;
        // The native callback constructs and retains this second input
        // accessor even though the following loop only reads the tessellated
        // vector.  Keep its conversion/AddRef/Release behavior observable.
        ncbPropAccessor unusedInputAccessor(flatControlPoints);
        for(const auto &point : points) {
            includePatchPoint_guess(bounds, point.x, point.y);
        }
        return makePatchBoundsDictionary_guess(bounds, true);
    }

    tTJSVariant BezierPatch::calcBezierPatch(
        tTJSVariant flatControlPoints,
        tjs_real u,
        tjs_real v) {
        ncbPropAccessor points(flatControlPoints);
        PointList_guess controlPoints;
        readBezierControlPoints_guess(points, controlPoints, true);

        const auto horizontal =
            cubicBezierSingleHorizontalWeights_guess(u);
        const auto vertical =
            cubicBezierSingleVerticalWeights_guess(v);
        const auto point = calculateBezierPatchPoint_guess(
            controlPoints, horizontal, vertical);
        auto result = detail::createTJSArrayWithItems_guess();
        appendFlatPoint_guess(result, point);
        return result.value;
    }

    tTJSVariant BezierPatch::calcBezierPatchList(
        tTJSVariant flatControlPoints,
        tTJSVariant flatParameters) {
        ncbPropAccessor points(flatControlPoints);
        PointList_guess controlPoints;
        readBezierControlPoints_guess(points, controlPoints, true);

        auto result = detail::createTJSArrayWithItems_guess();
        ncbPropAccessor parameters(flatParameters);
        const std::uint32_t parameterCount =
            static_cast<std::uint32_t>(parameters.GetCount());
        for(std::uint32_t index = 0; index < parameterCount;
            index += 2u) {
            const tjs_real u = readMeshCoordinate_guess(
                parameters, signedInt32Bits_guess(index));
            const tjs_real v = readMeshCoordinate_guess(
                parameters, signedInt32Bits_guess(index + 1u));
            const auto horizontal =
                cubicBezierListHorizontalWeights_guess(u);
            const auto vertical =
                cubicBezierListVerticalWeights_guess(v);
            appendFlatPoint_guess(
                result,
                calculateBezierPatchPoint_guess(
                    controlPoints, horizontal, vertical));
        }
        return result.value;
    }

    tTJSVariant BezierPatch::reverseCalcBezierPatch(
        tTJSVariant flatControlPoints,
        tjs_real targetX,
        tjs_real targetY) {
        tTJSVariant result;
        const tTJSVariant boundsValue =
            calcPatchBounds(flatControlPoints);
        ncbPropAccessor bounds(boundsValue);
        const tjs_real left = bounds.GetValue(
            TJS_W("left"), ncbTypedefs::Tag<tjs_real>(),
            0, &detail::leftMemberHint_guess);
        const tjs_real top = bounds.GetValue(
            TJS_W("top"), ncbTypedefs::Tag<tjs_real>(),
            0, &detail::topMemberHint_guess);
        const tjs_real right = bounds.GetValue(
            TJS_W("right"), ncbTypedefs::Tag<tjs_real>(),
            0, &detail::rightMemberHint_guess);
        const tjs_real bottom = bounds.GetValue(
            TJS_W("bottom"), ncbTypedefs::Tag<tjs_real>(),
            0, &detail::bottomMemberHint_guess);

        // Express the native positive gate directly: unlike a chain of
        // negated ordered comparisons, this rejects NaN targets/bounds.
        if(!(top <= targetY && left <= targetX &&
             right >= targetX && bottom >= targetY)) {
            return result;
        }

        PointList_guess controlPoints;
        PointList_guess meshPoints;
        parseAndTessellateBezierPatch_guess(
            flatControlPoints, 10, 10, controlPoints, meshPoints);
        const tTVPPointD target{targetX, targetY};

        for(int row = 9; row >= 0; --row) {
            for(int column = 9; column >= 0; --column) {
                const std::size_t topLeftIndex =
                    static_cast<std::size_t>(row * 11 + column);
                const auto &topLeft = meshPoints[topLeftIndex];
                const auto &topRight = meshPoints[topLeftIndex + 1u];
                const auto &bottomLeft = meshPoints[topLeftIndex + 11u];
                const auto &bottomRight = meshPoints[topLeftIndex + 12u];

                const tjs_real leftParameter =
                    static_cast<tjs_real>(column) / 10.0;
                const tjs_real rightParameter =
                    static_cast<tjs_real>(column + 1) / 10.0;
                const tjs_real topParameter =
                    static_cast<tjs_real>(row) / 10.0;
                const tjs_real bottomParameter =
                    static_cast<tjs_real>(row + 1) / 10.0;

                if(pointInTriangle_guess(
                       topRight, bottomLeft, bottomRight, target)) {
                    if(reverseAffineTriangle_guess(
                           result,
                           topRight, bottomLeft, bottomRight,
                           {rightParameter, topParameter},
                           {leftParameter, bottomParameter},
                           {rightParameter, bottomParameter},
                           target)) {
                        return result;
                    }
                    // The first triangle claimed the point.  If its affine
                    // inverse is degenerate, the reference skips the other
                    // triangle in this cell and resumes at the next cell.
                    continue;
                }

                if(pointInTriangle_guess(
                       topLeft, topRight, bottomLeft, target) &&
                   reverseAffineTriangle_guess(
                       result,
                       topLeft, topRight, bottomLeft,
                       {leftParameter, topParameter},
                       {rightParameter, topParameter},
                       {leftParameter, bottomParameter},
                       target)) {
                    return result;
                }
            }
        }
        return result;
    }

    MotionLayerExtensions_guess::MotionLayerExtensions_guess(
        iTJSDispatch2 *owner) noexcept :
        _owner(owner) {}

    tTJSVariant MotionLayerExtensions_guess::getDebugMeshApp() const {
        return _debugMeshApp;
    }

    void MotionLayerExtensions_guess::setDebugMeshApp(tTJSVariant value) {
        _debugMeshApp = value;
    }

    tTJSVariant MotionLayerExtensions_guess::getDebugBezierApp() const {
        return _debugBezierApp;
    }

    void MotionLayerExtensions_guess::setDebugBezierApp(tTJSVariant value) {
        _debugBezierApp = value;
    }

    void MotionLayerExtensions_guess::refreshFace_guess() {
        ncbPropAccessor ownerAccessor(_owner);
        // All four references deliberately use getIntValue here: a null-hint
        // MEMBERMUSTEXIST probe precedes the null-hint flags=0 value read.
        _faceCache_guess = ownerAccessor.getIntValue(TJS_W("face"), 0);
        if(_faceCache_guess != 128) {
            return;
        }

        const tjs_int type =
            ownerAccessor.getIntValue(TJS_W("type"), 0);
        if(type == 2 || (type >= 13 && type <= 28)) {
            _faceCache_guess = 0;
        } else if(type == 12) {
            _faceCache_guess = 4;
        } else {
            _faceCache_guess = 1;
        }
    }

    tjs_int MotionLayerExtensions_guess::resolveAutoMode_guess() const {
        ncbPropAccessor ownerAccessor(_owner);
        const tjs_int type =
            ownerAccessor.getIntValue(TJS_W("type"), 0);
        // The comparison is signed: negative script values survive unchanged.
        return type > 28 ? 1 : type;
    }

    bool MotionLayerExtensions_guess::resolveBitmapMethod_guess(
        tjs_int mode, tjs_int &bitmapMethod) {
        refreshFace_guess();
        switch(mode) {
            case 1:
                if(_faceCache_guess == 0) {
                    bitmapMethod = 1;
                    return true;
                }
                if(_faceCache_guess == 1) {
                    bitmapMethod = 0;
                    return true;
                }
                if(_faceCache_guess == 4) {
                    bitmapMethod = 15;
                    return true;
                }
                return false;
            case 2:
                if(_faceCache_guess == 0) {
                    bitmapMethod = 3;
                    return true;
                }
                if(_faceCache_guess == 1) {
                    bitmapMethod = 2;
                    return true;
                }
                if(_faceCache_guess == 4) {
                    bitmapMethod = 14;
                    return true;
                }
                return false;
            case 3: bitmapMethod = 4; return true;
            case 4: bitmapMethod = 5; return true;
            case 5: bitmapMethod = 6; return true;
            case 8: bitmapMethod = 7; return true;
            case 9: bitmapMethod = 8; return true;
            case 10: bitmapMethod = 9; return true;
            case 11: bitmapMethod = 10; return true;
            case 12:
                if(_faceCache_guess == 0) {
                    bitmapMethod = 13;
                    return true;
                }
                if(_faceCache_guess == 1) {
                    bitmapMethod = 11;
                    return true;
                }
                if(_faceCache_guess == 4) {
                    bitmapMethod = 12;
                    return true;
                }
                return false;
            default:
                if(mode >= 13 && mode <= 28) {
                    bitmapMethod = mode + 3;
                    return true;
                }
                return false;
        }
    }

    void MotionLayerExtensions_guess::renderMesh_guess(
        tTJSVariant source,
        const tTVPRect &sourceRect,
        tTJSVariant flatPoints,
        tjs_int divisionX,
        tjs_int divisionY,
        tjs_int bitmapMethod,
        bool holdAlpha,
        tjs_int opacity,
        tjs_int stretchType) {
        ncbPropAccessor pointAccessor(flatPoints);
        const PointList_guess points =
            readMeshPoints_guess(pointAccessor);

        ncbPropAccessor ownerAccessor(_owner);
        const tjs_int clipLeft = ownerAccessor.GetValue(
            TJS_W("clipLeft"), ncbTypedefs::Tag<tjs_int>(),
            0, &motionLayerClipLeftMemberHint_guess);
        const tjs_int clipTop = ownerAccessor.GetValue(
            TJS_W("clipTop"), ncbTypedefs::Tag<tjs_int>(),
            0, &motionLayerClipTopMemberHint_guess);
        const tjs_int clipRight = addInt32_guess(
            clipLeft, ownerAccessor.GetValue(
                TJS_W("clipWidth"), ncbTypedefs::Tag<tjs_int>(),
                0, &motionLayerClipWidthMemberHint_guess));
        const tjs_int clipBottom = addInt32_guess(
            clipTop, ownerAccessor.GetValue(
                TJS_W("clipHeight"), ncbTypedefs::Tag<tjs_int>(),
                0, &motionLayerClipHeightMemberHint_guess));
        const tTVPRect clipRect(
            clipLeft, clipTop, clipRight, clipBottom);

        auto *targetLayer = layerFromObject_guess(_owner);
        auto *sourceLayer = layerFromVariant_guess(source);
        auto *sourceTexture = sourceLayer->GetMainImage()->GetTexture();
        auto *methodManager = TVPGetRenderManager();
        auto *method = methodManager->GetRenderMethod(
            opacity, holdAlpha, bitmapMethod);
        if(method) {
            auto *targetBitmap = targetLayer->GetMainImage();
            if(submitLayerMesh_guess(
                   nullptr, targetBitmap, clipRect,
                   sourceTexture, sourceRect, points, points,
                   divisionX, divisionY, method, stretchType)) {
                callLayerRectMethod_guess(
                    _owner, TJS_W("update"),
                    &detail::updateMemberHint_guess,
                    clipRect, 0, false);
            }
            drawGridDebug_guess(
                _owner, _debugMeshApp,
                points, divisionX, divisionY);
        }
    }

    void MotionLayerExtensions_guess::renderBezierPatch_guess(
        tTJSVariant source,
        const tTVPRect &sourceRect,
        tTJSVariant flatControlPoints,
        tjs_int divisionX,
        tjs_int divisionY,
        tjs_int bitmapMethod,
        bool holdAlpha,
        tjs_int opacity,
        tjs_int stretchType) {
        ncbPropAccessor ownerAccessor(_owner);
        const tjs_int clipLeft = ownerAccessor.GetValue(
            TJS_W("clipLeft"), ncbTypedefs::Tag<tjs_int>(),
            0, &motionLayerClipLeftMemberHint_guess);
        const tjs_int clipTop = ownerAccessor.GetValue(
            TJS_W("clipTop"), ncbTypedefs::Tag<tjs_int>(),
            0, &motionLayerClipTopMemberHint_guess);
        const tjs_int clipRight = addInt32_guess(
            clipLeft, ownerAccessor.GetValue(
                TJS_W("clipWidth"), ncbTypedefs::Tag<tjs_int>(),
                0, &motionLayerClipWidthMemberHint_guess));
        const tjs_int clipBottom = addInt32_guess(
            clipTop, ownerAccessor.GetValue(
                TJS_W("clipHeight"), ncbTypedefs::Tag<tjs_int>(),
                0, &motionLayerClipHeightMemberHint_guess));
        const tTVPRect clipRect(
            clipLeft, clipTop, clipRight, clipBottom);

        PointList_guess controlPoints;
        PointList_guess points;
        parseAndTessellateBezierPatch_guess(
            flatControlPoints, divisionX, divisionY,
            controlPoints, points);
        auto *targetLayer = layerFromObject_guess(_owner);
        auto *sourceLayer = layerFromVariant_guess(source);
        auto *sourceTexture = sourceLayer->GetMainImage()->GetTexture();
        auto *methodManager = TVPGetRenderManager();
        auto *method = methodManager->GetRenderMethod(
            opacity, holdAlpha, bitmapMethod);
        if(method) {
            auto *targetBitmap = targetLayer->GetMainImage();
            if(submitLayerMesh_guess(
                   nullptr, targetBitmap, clipRect,
                   sourceTexture, sourceRect, controlPoints, points,
                   divisionX, divisionY, method, stretchType)) {
                callLayerRectMethod_guess(
                    _owner, TJS_W("update"),
                    &detail::updateMemberHint_guess,
                    clipRect, 0, false);
            }
            drawGridDebug_guess(
                _owner, _debugMeshApp,
                points, divisionX, divisionY);
            drawBezierControlFrame_guess(
                _owner, _debugBezierApp, controlPoints);
        }
    }

    void MotionLayerExtensions_guess::meshCopy(
        tTJSVariant source,
        tjs_int sourceLeft,
        tjs_int sourceTop,
        tjs_int sourceWidth,
        tjs_int sourceHeight,
        tTJSVariant flatPoints,
        tjs_int divisionX,
        tjs_int divisionY,
        tjs_int stretchType,
        bool clear) {
        ncbPropAccessor ownerAccessor(_owner);
        refreshFace_guess();
        bool holdAlpha = false;
        if(_faceCache_guess == 1) {
            holdAlpha = ownerAccessor.GetValue(
                TJS_W("holdAlpha"), ncbTypedefs::Tag<bool>(), 0,
                &motionLayerHoldAlphaMemberHint_guess);
        } else if(_faceCache_guess != 0 && _faceCache_guess != 4) {
            TVPThrowExceptionMessage(
                TJS_W("meshCopy: not drawable face type."));
        }
        if(clear) {
            clearWholeLayer_guess(_owner);
        }

        renderMesh_guess(
            source,
            tTVPRect(sourceLeft, sourceTop,
                     addInt32_guess(sourceLeft, sourceWidth),
                     addInt32_guess(sourceTop, sourceHeight)),
            flatPoints, divisionX, divisionY, 0, holdAlpha, 255,
            stretchType);
    }

    void MotionLayerExtensions_guess::operateMesh(
        tTJSVariant source,
        tjs_int sourceLeft,
        tjs_int sourceTop,
        tjs_int sourceWidth,
        tjs_int sourceHeight,
        tTJSVariant flatPoints,
        tjs_int divisionX,
        tjs_int divisionY,
        tjs_int mode,
        tjs_int opacity,
        tjs_int stretchType) {
        if(mode == 128) {
            mode = resolveAutoMode_guess();
        }
        tjs_int bitmapMethod = 0;
        if(!resolveBitmapMethod_guess(mode, bitmapMethod)) {
            TVPThrowExceptionMessage(
                TJS_W("operateMesh: not drawable face type."));
        }

        ncbPropAccessor ownerAccessor(_owner);
        const bool holdAlpha = ownerAccessor.GetValue(
            TJS_W("holdAlpha"), ncbTypedefs::Tag<bool>(), 0,
            &motionLayerHoldAlphaMemberHint_guess);
        renderMesh_guess(
            source,
            tTVPRect(sourceLeft, sourceTop,
                     addInt32_guess(sourceLeft, sourceWidth),
                     addInt32_guess(sourceTop, sourceHeight)),
            flatPoints, divisionX, divisionY, bitmapMethod,
            holdAlpha, opacity, stretchType);
    }

    void MotionLayerExtensions_guess::bezierPatchCopy(
        tTJSVariant source,
        tjs_int sourceLeft,
        tjs_int sourceTop,
        tjs_int sourceWidth,
        tjs_int sourceHeight,
        tTJSVariant flatControlPoints,
        tjs_int divisionX,
        tjs_int divisionY,
        tjs_int stretchType,
        bool clear) {
        ncbPropAccessor ownerAccessor(_owner);
        refreshFace_guess();
        bool holdAlpha = false;
        if(_faceCache_guess == 1) {
            holdAlpha = ownerAccessor.GetValue(
                TJS_W("holdAlpha"), ncbTypedefs::Tag<bool>(), 0,
                &motionLayerHoldAlphaMemberHint_guess);
        } else if(_faceCache_guess != 0 && _faceCache_guess != 4) {
            TVPThrowExceptionMessage(
                TJS_W("bezierPatchCopy: not drawable face type."));
        }
        if(clear) {
            clearWholeLayer_guess(_owner);
        }

        renderBezierPatch_guess(
            source,
            tTVPRect(sourceLeft, sourceTop,
                     addInt32_guess(sourceLeft, sourceWidth),
                     addInt32_guess(sourceTop, sourceHeight)),
            flatControlPoints, divisionX, divisionY, 0,
            holdAlpha, 255, stretchType);
    }

    void MotionLayerExtensions_guess::operateBezierPatch(
        tTJSVariant source,
        tjs_int sourceLeft,
        tjs_int sourceTop,
        tjs_int sourceWidth,
        tjs_int sourceHeight,
        tTJSVariant flatControlPoints,
        tjs_int divisionX,
        tjs_int divisionY,
        tjs_int mode,
        tjs_int opacity,
        tjs_int stretchType) {
        if(mode == 128) {
            mode = resolveAutoMode_guess();
        }
        tjs_int bitmapMethod = 0;
        if(!resolveBitmapMethod_guess(mode, bitmapMethod)) {
            TVPThrowExceptionMessage(
                TJS_W("operateMesh: not drawable face type."));
        }

        ncbPropAccessor ownerAccessor(_owner);
        const bool holdAlpha = ownerAccessor.GetValue(
            TJS_W("holdAlpha"), ncbTypedefs::Tag<bool>(), 0,
            &motionLayerHoldAlphaMemberHint_guess);
        renderBezierPatch_guess(
            source,
            tTVPRect(sourceLeft, sourceTop,
                     addInt32_guess(sourceLeft, sourceWidth),
                     addInt32_guess(sourceTop, sourceHeight)),
            flatControlPoints, divisionX, divisionY, bitmapMethod,
            holdAlpha, opacity, stretchType);
    }

    void MotionLayerExtensions_guess::drawMeshFrame(
        tTJSVariant outline,
        tTJSVariant meshline,
        tTJSVariant flatPoints,
        tjs_int divisionX,
        tjs_int divisionY) {
        ncbPropAccessor flatPointAccessor(flatPoints);
        const PointList_guess points =
            readMeshPoints_guess(flatPointAccessor);
        ncbPropAccessor layerClass(TJS_W("Layer"));
        drawGridFrame_guess(
            layerClass.GetDispatch(), _owner, outline, meshline, points,
            divisionX, divisionY);
    }

    void MotionLayerExtensions_guess::drawBezierPatchMeshFrame(
        tTJSVariant outline,
        tTJSVariant meshline,
        tTJSVariant flatControlPoints,
        tjs_int divisionX,
        tjs_int divisionY) {
        ncbPropAccessor layerClass(TJS_W("Layer"));
        PointList_guess controlPoints;
        PointList_guess points;
        parseAndTessellateBezierPatch_guess(
            flatControlPoints, divisionX, divisionY, controlPoints, points);
        drawGridFrame_guess(
            layerClass.GetDispatch(), _owner, outline, meshline, points,
            divisionX, divisionY);
    }

    void MotionLayerExtensions_guess::drawBezierPatchFrame(
        tTJSVariant outline,
        tTJSVariant meshline,
        tTJSVariant flatControlPoints) {
        ncbPropAccessor flatPointAccessor(flatControlPoints);
        PointList_guess controlPoints;
        ncbPropAccessor layerClass(TJS_W("Layer"));
        controlPoints.reserve(16);
        readBezierControlPoints_guess(
            flatPointAccessor, controlPoints, true);
        const auto &basis =
            render_backend_guess::cubicBezierBasisTable_guess(3);
        iTJSDispatch2 *layer = layerClass.GetDispatch();

        for(int sample = 0; sample != 4; ++sample) {
            const tTJSVariant &appearance =
                sample == 0 || sample == 3 ? outline : meshline;
            if(appearance.Type() != tvtVoid) {
                auto curve = detail::createTJSArrayWithItems_guess();
                for(int row = 0; row != 3; ++row) {
                    appendPoint_guess(
                        curve,
                        evaluateBezierRow_guess(
                            controlPoints, basis[sample], row));
                }
                if(sample == 0) {
                    std::reverse(curve.items->begin(), curve.items->end());
                }
                callLayerDrawingMethod_guess(
                    layer, _owner, TJS_W("drawBeziers"),
                    &drawBeziersMemberHint_guess,
                    appearance, curve.value);
            }
        }

        for(int sample = 0; sample != 4; ++sample) {
            const tTJSVariant &appearance =
                sample == 0 || sample == 3 ? outline : meshline;
            if(appearance.Type() != tvtVoid) {
                auto curve = detail::createTJSArrayWithItems_guess();
                for(int column = 0; column != 3; ++column) {
                    appendPoint_guess(
                        curve,
                        evaluateBezierColumn_guess(
                            controlPoints, basis[sample], column));
                }
                if(sample == 3) {
                    std::reverse(curve.items->begin(), curve.items->end());
                }
                callLayerDrawingMethod_guess(
                    layer, _owner, TJS_W("drawBeziers"),
                    &drawBeziersMemberHint_guess,
                    appearance, curve.value);
            }
        }
    }

} // namespace motion
