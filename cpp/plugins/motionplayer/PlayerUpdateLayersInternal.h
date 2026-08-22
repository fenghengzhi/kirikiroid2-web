#pragma once

#include "PlayerInternal.h"
#include "MotionDispatch.h"
#include "MotionBezierPatch.h"
#include "MotionTraceWeb.h"
#include "ncbind.hpp"    // ncbInstanceAdaptor<Player>::CreateAdaptor for TJS bridge
#include "tjsArray.h"    // TJSCreateArrayObject, TJSGetArrayElementCount
#include <limits>
#ifdef __EMSCRIPTEN__
#include <wasm_simd128.h>
#endif

#if defined(__clang__) || defined(__GNUC__)
#define MOTIONPLAYER_NOINLINE __attribute__((noinline))
#else
#define MOTIONPLAYER_NOINLINE
#endif

using namespace motion::internal;

namespace {
    constexpr double kCameraConstraintExtent_guess =
        static_cast<double>(std::numeric_limits<float>::max());

    inline bool motionSubNodeNeedsTeardown_guess(
        bool slotDone, const ttstr &source) {
        return slotDone || source.IsEmpty();
    }

    inline bool motionSubNodeConsumeReplayFlag_guess(
        int parameterMode, std::uint8_t &flags) {
        if ((parameterMode & 5) == 0 && flags == 0) {
            return false;
        }
        flags = 1;
        return true;
    }

    inline double motionSubNodeClampTimeAtZero_guess(double time) {
        return std::fmax(time, 0.0);
    }

    // Several update-layer producers narrow through FCVTZS/VCVT.S32.F64 in
    // all four references. Keep their signed-int32 invalid/overflow result out
    // of C++'s undefined floating-to-integer conversion domain.
    inline int signedInt32FromDoubleTowardZeroSaturated_guess(
        double value) noexcept {
        constexpr double lower = -0x1p31;
        constexpr double upper = 0x1p31;
        if(std::isnan(value)) {
            return 0;
        }
        if(value >= upper) {
            return std::numeric_limits<std::int32_t>::max();
        }
        if(value <= lower) {
            return std::numeric_limits<std::int32_t>::min();
        }
        return static_cast<int>(value);
    }

    inline int particleEmitCountFromDouble_guess(double value) noexcept {
        return signedInt32FromDoubleTowardZeroSaturated_guess(value);
    }

    inline int particleSourceIndexFromDouble_guess(double value) noexcept {
        return signedInt32FromDoubleTowardZeroSaturated_guess(value);
    }

    // Timeline opacity is rounded half away from zero and then narrowed by
    // the same unsigned-saturating conversion used by timeline ti. Keep the
    // native 32-bit word separate from the port's signed storage field.
    inline std::uint32_t timelineOpacityWordFromDouble_guess(
        double value) noexcept {
        const double rounded = value < 0.0
            ? std::ceil(value - 0.5)
            : std::floor(value + 0.5);
        return doubleToUnsignedIntTowardZeroSaturated_guess(rounded);
    }

    struct PositionDerivativeSampleTimes_guess {
        double first;
        double second;
    };

    inline PositionDerivativeSampleTimes_guess
    positionDerivativeSampleTimes_guess(double ratio) {
        const double candidate = ratio + 0.0001;
        // Both MotionSub angle mode 3 and the type-6 emitter select the
        // endpoint fallback when this comparison is unordered.
        const double first = candidate < 1.0 ? ratio : 0.9999;
        // Keeping candidate first expresses the shared source shape and the
        // ARM64 unordered result. ARMv7 lowers this operation to an ordered
        // select and therefore chooses 1.0 for an unordered candidate.
        const double second = std::min(candidate, 1.0);
        return {first, second};
    }

    inline double motionSubNodeBlendAngleOffset_guess(
        double currentOffset, double otherOffset,
        double parentTime, double currentStart, double otherStart,
        const tTJSVariant &easing) {
        if (currentOffset == otherOffset) {
            return currentOffset;
        }
        if (currentOffset >= otherOffset) {
            if (currentOffset - otherOffset > 180.0) {
                otherOffset += 360.0;
            }
        } else if (otherOffset - currentOffset > 180.0) {
            otherOffset -= 360.0;
        }
        double ratio = (parentTime - currentStart) /
            (otherStart - currentStart);
        if (easing.Type() != tvtVoid) {
            ratio = evaluateVariableTrackEasing_guess(easing, ratio);
        }
        double result = otherOffset * ratio +
            currentOffset * (1.0 - ratio);
        if (result < 0.0) {
            result += 360.0;
        } else if (result >= 360.0) {
            result -= 360.0;
        }
        return result;
    }

    inline void motionSubNodeApplyOriginOffset_guess(
        int coordinateMode, double originX, double originY,
        double m11, double m12, double m21, double m22,
        double &posX, double &posY, double &posZ) {
        if (originX == 0.0 && originY == 0.0) {
            return;
        }
        const double negOriginY = -originY;
        const double translatedX =
            m12 * negOriginY - originX * m11;
        const double translatedY =
            m22 * negOriginY - originX * m21;
        if (coordinateMode == 1) {
            posX += translatedX;
            posZ += translatedY;
        } else if (coordinateMode == 0) {
            posX += translatedX;
            posY += translatedY;
        }
    }

    inline void copyPackedColorsToBytes(
        uint8_t (&colorBytes)[16],
        const std::array<std::uint32_t, 4> &packedColors) {
        std::memcpy(colorBytes, packedColors.data(), sizeof(std::uint32_t) * 4u);
    }

    inline std::array<std::uint32_t, 4> copyPackedColorsFromBytes(
        const uint8_t (&colorBytes)[16]) {
        std::array<std::uint32_t, 4> packedColors{};
        std::memcpy(packedColors.data(), colorBytes,
                    sizeof(std::uint32_t) * packedColors.size());
        return packedColors;
    }

    // The two native clamp idioms deliberately disagree for NaN: opacity
    // falls back to zero, while a color channel falls back to 255.
    inline double clampAnchorOpacityResult_guess(double value) {
        double result = 0.0;
        if(value >= 0.0) {
            result = value;
            if(value > 255.0) {
                result = 255.0;
            }
        }
        return result;
    }

    inline double clampAnchorColorResult_guess(double value) {
        double result = 255.0;
        if(value <= 255.0) {
            result = value;
            if(value < 0.0) {
                result = 0.0;
            }
        }
        return result;
    }

    inline void dampAnchorOpacity_guess(
        int &opacity, double &scale, double dampPower) {
        const auto unsignedOpacity = static_cast<std::uint32_t>(opacity);
        double normalized = static_cast<double>(unsignedOpacity) / 255.0;
        if(unsignedOpacity == 0) {
            normalized = 1.0 / 255.0;
        }

        const double result = clampAnchorOpacityResult_guess(
            std::pow(normalized, dampPower) * 255.0 * scale);
        const int truncated = static_cast<int>(result);
        double denominator = result;
        if(truncated < 0) {
            denominator += 4294967296.0;
        }
        opacity = truncated;
        scale = result / denominator;
    }

    // The native channel path is intentionally asymmetric. It evaluates byte
    // order 2,1,0,3 against scale order 0,1,2,3, writes 0,2,1,3, and divides
    // scale 1 by channel 0's truncated result. These are observable source
    // bugs, including the unguarded zero divisions.
    inline void dampAnchorPackedColor_guess(
        std::uint8_t *bytes, double *scales,
        double rgbBase, double dampPower) {
        const auto dampRgb = [rgbBase, dampPower](
                                 std::uint8_t input,
                                 double scale) {
            double value = static_cast<double>(input);
            if(input == 0) {
                value = 1.0;
            }
            return clampAnchorColorResult_guess(
                rgbBase * std::pow(value / rgbBase, dampPower) * scale);
        };

        const double channel0 = dampRgb(bytes[2], scales[0]);
        const double channel1 = dampRgb(bytes[1], scales[1]);
        const double channel2 = dampRgb(bytes[0], scales[2]);

        double alpha = static_cast<double>(bytes[3]) / 255.0;
        if(bytes[3] == 0) {
            alpha = 1.0 / 255.0;
        }
        const double channel3 = clampAnchorColorResult_guess(
            std::pow(alpha, dampPower) * 255.0 * scales[3]);

        const int truncated0 = static_cast<int>(channel0);
        const int truncated1 = static_cast<int>(channel1);
        const int truncated2 = static_cast<int>(channel2);
        const int truncated3 = static_cast<int>(channel3);

        const double signedDenominator0 =
            static_cast<double>(truncated0);
        double unsignedDenominator0 = signedDenominator0;
        if(truncated0 < 0) {
            unsignedDenominator0 += 4294967296.0;
        }
        double unsignedDenominator2 =
            static_cast<double>(truncated2);
        if(truncated2 < 0) {
            unsignedDenominator2 += 4294967296.0;
        }
        double unsignedDenominator3 =
            static_cast<double>(truncated3);
        if(truncated3 < 0) {
            unsignedDenominator3 += 4294967296.0;
        }

        scales[0] = channel0 / signedDenominator0;
        scales[1] = channel1 / unsignedDenominator0;
        scales[2] = channel2 / unsignedDenominator2;
        scales[3] = channel3 / unsignedDenominator3;

        bytes[0] = static_cast<std::uint8_t>(truncated2);
        bytes[1] = static_cast<std::uint8_t>(truncated0);
        bytes[2] = static_cast<std::uint8_t>(truncated1);
        bytes[3] = static_cast<std::uint8_t>(truncated3);
    }

    inline int remapCameraConstraintType_guess(
        int constraintType, bool flipX, bool flipY) {
        if(flipX) {
            if(constraintType == 0) {
                constraintType = 2;
            } else if(constraintType == 2) {
                // All four references carry this asymmetric branch. It is not
                // the intuitive 2 -> 0 mirror and must remain observable.
                constraintType = 3;
            }
        }
        if(flipY) {
            if(constraintType == 3) {
                constraintType = 5;
            } else if(constraintType == 5) {
                constraintType = 3;
            }
        }
        return constraintType;
    }

    inline double selectCameraConstraintOffset_guess(
        bool hasMinimum, double minimum,
        bool hasDirect, double direct,
        bool hasMaximum, double maximum) {
        if(hasDirect) {
            return direct;
        }
        if(hasMaximum) {
            return maximum;
        }
        if(hasMinimum) {
            return minimum;
        }
        return 0.0;
    }

    inline float narrowAndNegateCameraNodeDelta_guess(double value) {
        return -static_cast<float>(value);
    }

    inline float quantizeCameraNodeOffset_guess(
        double primary, double secondary,
        float deltaX, float deltaY) {
        return static_cast<float>(
            signedInt32FromDoubleTowardZeroSaturated_guess(
                primary * static_cast<double>(deltaX)
                + secondary * static_cast<double>(deltaY) + 0.5));
    }

    inline double cameraNodeAngleDegrees_guess(
        double cameraX, double cameraZ,
        double targetX, double targetZ) {
        const double radians = std::atan2(
            cameraZ - targetZ, cameraX - targetX);
        double degrees = radians * -57.2957795 + 90.0;
        while(degrees < 0.0) {
            degrees += 360.0;
        }
        while(degrees >= 360.0) {
            degrees -= 360.0;
        }
        return degrees;
    }

    inline int selectVisibleAncestorIndex_guess(
        int parentIndex, bool parentDrawFlag,
        int parentVisibleAncestorIndex) {
        return parentDrawFlag ? parentIndex : parentVisibleAncestorIndex;
    }

    inline bool selectNodeDrawFlag_guess(
        bool slotDone, int stencilType, bool accumulatedActive,
        int forceVisible, int nodeType, bool preview,
        bool sourceValid) {
        if(slotDone || stencilType == 0 || !accumulatedActive) {
            return false;
        }
        const int visibilityMask = preview ? 6153 : 6145;
        if(forceVisible != 0
           || (visibilityMask & (1 << nodeType)) != 0) {
            return sourceValid;
        }
        return true;
    }

    inline bool selectVertexQuadMaterialization_guess(
        int forceVisible, int nodeType, bool preview, bool sourceBlank) {
        if(forceVisible != 0) {
            return true;
        }
        const int mask = preview ? 5193 : 5185;
        return (mask & (1 << nodeType)) != 0 && !sourceBlank;
    }

    class RetainedVariantDispatch_guess {
        iTJSDispatch2 *dispatch_ = nullptr;

        explicit RetainedVariantDispatch_guess(
            iTJSDispatch2 *dispatch) : dispatch_(dispatch) {}

    public:
        RetainedVariantDispatch_guess() = default;
        RetainedVariantDispatch_guess(
            const RetainedVariantDispatch_guess &) = delete;
        RetainedVariantDispatch_guess &operator=(
            const RetainedVariantDispatch_guess &) = delete;

        RetainedVariantDispatch_guess(
            RetainedVariantDispatch_guess &&other) noexcept
            : dispatch_(other.dispatch_) {
            other.dispatch_ = nullptr;
        }

        RetainedVariantDispatch_guess &operator=(
            RetainedVariantDispatch_guess &&other) noexcept {
            if(this != &other) {
                if(dispatch_) {
                    dispatch_->Release();
                }
                dispatch_ = other.dispatch_;
                other.dispatch_ = nullptr;
            }
            return *this;
        }

        ~RetainedVariantDispatch_guess() {
            if(dispatch_) {
                dispatch_->Release();
            }
        }

        static RetainedVariantDispatch_guess fromRawDispatch(
            iTJSDispatch2 *dispatch) {
            dispatch->AddRef();
            return RetainedVariantDispatch_guess(dispatch);
        }

        iTJSDispatch2 *get() const { return dispatch_; }
    };

    inline void mirrorForceVisibleGeometry_guess(
        const tTJSVariant &emoteEditVariant,
        double coordX, double coordY,
        double m11, double m12, double m21, double m22,
        double width, double height,
        double originX, double originY,
        bool flipX, bool flipY,
        double zoomX, double zoomY,
        double slantX, double angle) {
        // Native uses ncbind's polymorphic two-pointer accessor throughout:
        // the base comes from a copied Variant, while each nested accessor is
        // constructed from the temporary Variant returned by GetValue.
        ncbPropAccessor object{tTJSVariant(emoteEditVariant)};
        ncbPropAccessor coord{object.GetValue(
            TJS_W("coord"), ncbTypedefs::Tag<tTJSVariant>(), 0, nullptr)};
        (void)coord.SetValue(
            static_cast<tjs_int32>(0), coordX, TJS_MEMBERENSURE);
        (void)coord.SetValue(1, coordY, TJS_MEMBERENSURE);

        ncbPropAccessor matrix{object.GetValue(
            TJS_W("mtx"), ncbTypedefs::Tag<tTJSVariant>(), 0, nullptr)};
        (void)matrix.SetValue(
            static_cast<tjs_int32>(0), m11, TJS_MEMBERENSURE);
        (void)matrix.SetValue(1, m12, TJS_MEMBERENSURE);
        (void)matrix.SetValue(2, m21, TJS_MEMBERENSURE);
        (void)matrix.SetValue(3, m22, TJS_MEMBERENSURE);

        (void)object.SetValue(
            TJS_W("width"), width, TJS_MEMBERENSURE,
            &motion::detail::widthMemberHint_guess);
        (void)object.SetValue(
            TJS_W("height"), height, TJS_MEMBERENSURE,
            &motion::detail::heightMemberHint_guess);
        (void)object.SetValue(
            TJS_W("originX"), originX, TJS_MEMBERENSURE,
            &motion::detail::originXMemberHint_guess);
        (void)object.SetValue(
            TJS_W("originY"), originY, TJS_MEMBERENSURE,
            &motion::detail::originYMemberHint_guess);
        (void)object.SetValue(
            TJS_W("flipX"), flipX, TJS_MEMBERENSURE,
            &motion::detail::emoteEditFlipXMemberHint_guess);
        (void)object.SetValue(
            TJS_W("flipY"), flipY, TJS_MEMBERENSURE,
            &motion::detail::emoteEditFlipYMemberHint_guess);
        (void)object.SetValue(
            TJS_W("zoomX"), zoomX, TJS_MEMBERENSURE,
            &motion::detail::emoteEditZoomXMemberHint_guess);
        (void)object.SetValue(
            TJS_W("zoomY"), zoomY, TJS_MEMBERENSURE,
            &motion::detail::emoteEditZoomYMemberHint_guess);
        (void)object.SetValue(
            TJS_W("slantX"), slantX, TJS_MEMBERENSURE,
            &motion::detail::emoteEditSlantXMemberHint_guess);
        (void)object.SetValue(
            TJS_W("angle"), angle, TJS_MEMBERENSURE,
            &motion::detail::angleMemberHint_guess);
    }

    struct OrderedShapeAxis_guess {
        double minimum;
        double maximum;
    };

    inline OrderedShapeAxis_guess orderShapeAxis_guess(
        double first, double second) {
        return {
            first < second ? first : second,
            first <= second ? second : first
        };
    }

    inline float clampShapeMinimumToParent_guess(
        float childMinimum, float parentMinimum) {
        return parentMinimum < childMinimum
            ? childMinimum
            : parentMinimum;
    }

    inline float clampShapeMaximumToParent_guess(
        float childMaximum, float parentMaximum) {
        return parentMaximum > childMaximum
            ? childMaximum
            : parentMaximum;
    }

    inline void updateShapeGeometryRecord_guess(
        motion::detail::HitData &geometry,
        int shapeType,
        double vertexX,
        double vertexY,
        double scaleX,
        double scaleY,
        double m11,
        double m12,
        double m21,
        double m22,
        double slotOx,
        double slotOy) {
        geometry.type = shapeType;
        switch(shapeType) {
            case 0:
                geometry.values[0] = vertexX;
                geometry.values[1] = vertexY;
                break;
            case 1:
                geometry.values[0] = vertexX;
                geometry.values[1] = vertexY;
                geometry.values[2] = scaleX * 16.0 * 0.5;
                break;
            case 2: {
                const double halfWidth = scaleX * 16.0 * 0.5;
                const double halfHeight = scaleY * 16.0 * 0.5;
                geometry.values[3] = vertexX - halfWidth;
                geometry.values[4] = vertexY - halfHeight;
                geometry.values[5] = vertexX + halfWidth;
                geometry.values[6] = vertexY + halfHeight;
                break;
            }
            case 3: {
                const double originX = slotOx * m11 + slotOy * m12;
                const double originY = slotOx * m21 + slotOy * m22;
                const double negativeM11 = m11 * -8.0;
                const double negativeM12 = m12 * -8.0;
                const double negativeM21 = m21 * -8.0;
                const double negativeM22 = m22 * -8.0;
                const double positiveM11 = m11 * 8.0;
                const double positiveM12 = m12 * 8.0;
                const double positiveM21 = m21 * 8.0;
                const double positiveM22 = m22 * 8.0;

                // The four optimized references share these operation groups.
                // In particular, the fourth corner associates the position
                // with the positive term before adding the negative term.
                const double x0Offset =
                    (negativeM11 + negativeM12) - originX;
                const double y0Offset =
                    (negativeM21 + negativeM22) - originY;
                const double x1Offset =
                    (positiveM11 + negativeM12) - originX;
                const double y1Offset =
                    (positiveM21 + negativeM22) - originY;
                const double x2Offset =
                    (positiveM11 + positiveM12) - originX;
                const double y2Offset =
                    (positiveM21 + positiveM22) - originY;
                const double x3WithPosition = vertexX + positiveM12;
                const double y3WithPosition = vertexY + positiveM22;

                geometry.values[7] = vertexX + x0Offset;
                geometry.values[8] = vertexY + y0Offset;
                geometry.values[9] = vertexX + x1Offset;
                geometry.values[10] = vertexY + y1Offset;
                geometry.values[11] = vertexX + x2Offset;
                geometry.values[12] = vertexY + y2Offset;
                geometry.values[13] =
                    (negativeM11 + x3WithPosition) - originX;
                geometry.values[14] =
                    (negativeM21 + y3WithPosition) - originY;
                break;
            }
            default:
                break;
        }
    }

    inline std::array<int, 4> unpackPackedRgba(std::uint32_t packedColor) {
        return {
            static_cast<int>(packedColor & 0xFFu),
            static_cast<int>((packedColor >> 8) & 0xFFu),
            static_cast<int>((packedColor >> 16) & 0xFFu),
            static_cast<int>((packedColor >> 24) & 0xFFu),
        };
    }

    inline void neutralizeDeltaTransformOverrides(
        motion::detail::MotionNode::DeltaState &delta) {
        delta.flipX = false;
        delta.flipY = false;
        delta.posX = 0.0;
        delta.posY = 0.0;
        delta.posZ = 0.0;
        delta.angle = 0.0;
        delta.scaleX = 1.0;
        delta.scaleY = 1.0;
        delta.slantX = 0.0;
        delta.slantY = 0.0;
        delta.opacity = 255;
    }

    inline void copyDeltaBlockToAccum(
        motion::detail::MotionNode::AccumulatedState &accum,
        const motion::detail::MotionNode::DeltaState &delta) {
        accum.dirty = delta.dirty;
        accum.active = delta.activeOverride;
        accum.visible = delta.visibleOverride;
        accum.flipX = delta.flipX;
        accum.flipY = delta.flipY;
        accum.posX = delta.posX;
        accum.posY = delta.posY;
        accum.posZ = delta.posZ;
        accum.angle = delta.angle;
        accum.scaleX = delta.scaleX;
        accum.scaleY = delta.scaleY;
        accum.slantX = delta.slantX;
        accum.slantY = delta.slantY;
        accum.opacity = delta.opacity;
    }

    // Flatten a PSB layer node tree into the render-item list with precomputed
    // positions. The four-reference affine form is the full 2x3
    // [m11,m21,m12,m22,tx,ty].
    using Affine2x3 = std::array<double, 6>;

    // Build the local 2x2 matrix in transform-order sequence, then
    // right-multiply it into the caller's affine matrix.
    inline void applyLocalTransform(
        Affine2x3 &a,
        bool flipX,
        bool flipY,
        double angle,
        double scaleX,
        double scaleY,
        double slantX,
        double slantY,
        const int (&transformOrder)[4]) {
        // Build local 2x2 from identity via left-multiplication.
        double l11 = 1.0, l12 = 0.0, l21 = 0.0, l22 = 1.0;

        for(int step = 0; step < 4; step++) {
            const int op = transformOrder[step];
            switch(op) {
                case 0: // Flip
                    if(flipX) { l11 = -l11; l12 = -l12; }
                    if(flipY) { l21 = -l21; l22 = -l22; }
                    break;
                case 1: // Angle
                    if(angle != 0.0) {
                        const double rad =
                            (angle * 3.14159265 + angle * 3.14159265)
                            / 360.0;
                        const double c = std::cos(rad);
                        const double s = std::sin(rad);
                        const double t11 = c*l11 - s*l21;
                        const double t12 = c*l12 - s*l22;
                        const double t21 = s*l11 + c*l21;
                        const double t22 = s*l12 + c*l22;
                        l11 = t11; l12 = t12; l21 = t21; l22 = t22;
                    }
                    break;
                case 2: // Zoom
                    if(scaleX != 1.0 || scaleY != 1.0) {
                        l11 *= scaleX; l12 *= scaleX;
                        l21 *= scaleY; l22 *= scaleY;
                    }
                    break;
                case 3: // Slant
                    if(slantX != 0.0 || slantY != 0.0) {
                        const double t12 = l22*slantX + l12;
                        const double t21 = l11*slantY + l21;
                        const double t22 = l22 + l12*slantY;
                        const double t11 = l11 + slantX*l21;
                        l11 = t11; l12 = t12; l21 = t21; l22 = t22;
                    }
                    break;
            }
        }

        // Translation is unchanged; only the 2x2 part is multiplied.
        const double m11 = a[0]*l11 + a[2]*l21;
        const double m21 = a[1]*l11 + a[3]*l21;
        const double m12 = a[0]*l12 + a[2]*l22;
        const double m22 = a[1]*l12 + a[3]*l22;
        a[0] = m11; a[1] = m21; a[2] = m12; a[3] = m22;
    }

    // Rebuild the local 2x2 from the node's accumulated fields.
    inline void applyLocalTransform(Affine2x3 &a,
                                    const motion::detail::MotionNode &node) {
        applyLocalTransform(a,
                            node.accumulated.flipX,
                            node.accumulated.flipY,
                            node.accumulated.angle,
                            node.accumulated.scaleX,
                            node.accumulated.scaleY,
                            node.accumulated.slantX,
                            node.accumulated.slantY,
                            node.transformOrder);
    }

    // Deform a child through the parent's row-major 4x4 Bezier patch. Position
    // is always updated for a live type-1 sync mesh; angle and scale sample one
    // shared four-point finite-difference stencil at the original normalized
    // input coordinates.
    inline void deformChildByParentBezierPatch_guess(
        const motion::detail::MotionNode &parent,
        motion::detail::MotionNode &node) {
        if (parent.meshType != 1 || (parent.meshFlags & 1) == 0
            || !parent.accumulated.active || !parent.source.valid
            || parent.meshControlPoints.empty())
            return;
        const double slotOX = parent.activeSlot().ox;
        const double slotOY = parent.activeSlot().oy;
        const float totalOX = static_cast<float>(
            slotOX + parent.source.originX);
        const float totalOY = static_cast<float>(
            slotOY + parent.source.originY);
        const double width = parent.source.width;
        const double height = parent.source.height;
        const float childSecondary = static_cast<float>(
            parent.coordinateMode != 0
                ? node.accumulated.posZ
                : node.accumulated.posY);
        const float secondaryWithOrigin = totalOY + childSecondary;
        const float u = static_cast<float>(
            (node.accumulated.posX + static_cast<double>(totalOX)) / width);
        const float v = static_cast<float>(
            static_cast<double>(secondaryWithOrigin) / height);

        const auto deformed = evaluateBezierPatchVector_guess(
            parent.meshControlPoints, u, v);
        node.accumulated.posX = static_cast<double>(static_cast<float>(
            static_cast<double>(deformed.x) * width
            - static_cast<double>(totalOX)));
        if (parent.coordinateMode != 0) {
            node.accumulated.posZ = static_cast<double>(static_cast<float>(
                static_cast<double>(deformed.y) * height
                - static_cast<double>(totalOY)));
        } else {
            node.accumulated.posY = static_cast<double>(static_cast<float>(
                static_cast<double>(deformed.y) * height
                - static_cast<double>(totalOY)));
        }

        const bool inheritAngle = (parent.meshFlags & 2) != 0
            && (node.inheritFlags & 0x10) != 0;
        const bool inheritScale = (parent.meshFlags & 4) != 0
            && (node.inheritFlags & 0x60) != 0;
        if(!inheritAngle && !inheritScale) {
            return;
        }

        constexpr float negativeEpsilon = -0.0001f;
        constexpr float positiveEpsilon = 0.0001f;
        const auto uMinus = evaluateBezierPatchVector_guess(
            parent.meshControlPoints, u + negativeEpsilon, v);
        const auto uPlus = evaluateBezierPatchVector_guess(
            parent.meshControlPoints, u + positiveEpsilon, v);
        const auto vMinus = evaluateBezierPatchVector_guess(
            parent.meshControlPoints, u, v + negativeEpsilon);
        const auto vPlus = evaluateBezierPatchVector_guess(
            parent.meshControlPoints, u, v + positiveEpsilon);

        if(inheritAngle) {
            const float angleFromV = ::atan2f(
                vMinus.x - vPlus.x, vPlus.y - vMinus.y);
            const float angleFromU = ::atan2f(
                uPlus.y - uMinus.y, uPlus.x - uMinus.x);
            const double angleSum = static_cast<double>(angleFromV)
                + static_cast<double>(angleFromU);
            node.accumulated.angle +=
                ((angleSum * 0.5) * 360.0) / 6.28318531;
        }

        if(inheritScale) {
            const double xLeft = static_cast<double>(uMinus.x);
            const double yLeft = static_cast<double>(uMinus.y);
            const double dx = static_cast<double>(uPlus.x) - xLeft;
            const double dy = static_cast<double>(uPlus.y) - yLeft;
            const double areaPlus = std::fabs(
                dx * (static_cast<double>(vPlus.y) - yLeft)
                - dy * (static_cast<double>(vPlus.x) - xLeft)) * 0.5;
            const double areaMinus = std::fabs(
                dx * (static_cast<double>(vMinus.y) - yLeft)
                - dy * (static_cast<double>(vMinus.x) - xLeft)) * 0.5;
            const double doubledArea =
                ((areaPlus + areaMinus) + areaMinus) + areaPlus;
            const double scaleFactor = std::sqrt(doubledArea) / 0.0002;
            if (node.inheritFlags & 0x020)
                node.accumulated.scaleX *= scaleFactor;
            if (node.inheritFlags & 0x040)
                node.accumulated.scaleY *= scaleFactor;
        }
    }

    inline void addBezierPatchDelta_guess(
        std::vector<motion::detail::MeshPoint> &working,
        const std::vector<motion::detail::MeshPoint> &ancestorPatch) {
        if(working.size() != ancestorPatch.size()) {
            TVPAddLog(TJS_W("mesh size is different."));
        }
        for(size_t index = 0; index < working.size(); ++index) {
            working[index].x = working[index].x
                + (ancestorPatch[index].x
                   - defaultBezierPatchPoints_guess[index].x);
            working[index].y = working[index].y
                + (ancestorPatch[index].y
                   - defaultBezierPatchPoints_guess[index].y);
        }
    }

    inline motion::detail::MeshPoint mapMeshPointThroughAncestor_guess(
        motion::detail::MeshPoint point,
        const motion::detail::MotionNode &ancestor) {
        const float translatedX = point.x + ancestor.meshInvOffX;
        const float translatedY = point.y + ancestor.meshInvOffY;
        const float u = static_cast<float>(
            ancestor.meshInvM11 * static_cast<double>(translatedX)
            + ancestor.meshInvM12 * static_cast<double>(translatedY));
        const float v = static_cast<float>(
            ancestor.meshInvM21 * static_cast<double>(translatedX)
            + ancestor.meshInvM22 * static_cast<double>(translatedY));
        return evaluateBezierPatchVector_guess(
            ancestor.transformedMeshControlPoints, u, v);
    }

    inline motion::detail::MeshPoint mapMeshPositionThroughAncestor_guess(
        double x, double y, const motion::detail::MotionNode &ancestor) {
        const double translatedX = x
            + static_cast<double>(ancestor.meshInvOffX);
        const double translatedY = y
            + static_cast<double>(ancestor.meshInvOffY);
        const float u = static_cast<float>(
            ancestor.meshInvM11 * translatedX
            + ancestor.meshInvM12 * translatedY);
        const float v = static_cast<float>(
            ancestor.meshInvM21 * translatedX
            + ancestor.meshInvM22 * translatedY);
        return evaluateBezierPatchVector_guess(
            ancestor.transformedMeshControlPoints, u, v);
    }

    inline void buildBilinearMeshGrid_guess(
        int cellsX, int cellsY,
        std::vector<motion::detail::MeshPoint> &output,
        const float *corners) {
        const std::uint32_t sizeWord =
            (static_cast<std::uint32_t>(cellsX) + 1u)
            * (static_cast<std::uint32_t>(cellsY) + 1u);
        output.resize(static_cast<size_t>(
            static_cast<std::int32_t>(sizeWord)));
        size_t outputIndex = 0;
        // The references use wrapping 32-bit counters and execute exactly
        // cells+1 iterations for every nonnegative dimension.  An int64 loop
        // expresses the same count without introducing C++ signed-overflow UB
        // at INT_MAX.
        for(std::int64_t y = 0; y <= static_cast<std::int64_t>(cellsY); ++y) {
            const double yRatio = static_cast<double>(y)
                / static_cast<double>(cellsY);
            const double inverseY = 1.0 - yRatio;
            const double leftX = yRatio * static_cast<double>(corners[6])
                + inverseY * static_cast<double>(corners[0]);
            const double leftY = yRatio * static_cast<double>(corners[7])
                + inverseY * static_cast<double>(corners[1]);
            const double rightX = yRatio * static_cast<double>(corners[4])
                + inverseY * static_cast<double>(corners[2]);
            const double rightY = yRatio * static_cast<double>(corners[5])
                + inverseY * static_cast<double>(corners[3]);
            for(std::int64_t x = 0;
                x <= static_cast<std::int64_t>(cellsX); ++x) {
                const double xRatio = static_cast<double>(x)
                    / static_cast<double>(cellsX);
                const double inverseX = 1.0 - xRatio;
                output[outputIndex++] = {
                    static_cast<float>(
                        rightX * xRatio + leftX * inverseX),
                    static_cast<float>(
                        rightY * xRatio + leftY * inverseX),
                };
            }
        }
    }

    inline std::uint32_t meshDoubleToUnsignedTowardZeroSaturated_guess(
        double value) {
        constexpr double twoToThe32 = 4294967296.0;
        if(std::isnan(value) || value <= 0.0) {
            return 0;
        }
        if(value >= twoToThe32) {
            return std::numeric_limits<std::uint32_t>::max();
        }
        return static_cast<std::uint32_t>(value);
    }

    // Own type-1 mesh paths convert to uint32 but compare that word against
    // 50 as signed int32.  Values with the sign bit set therefore bypass the
    // cap and continue through the following uint32 arithmetic unchanged.
    inline std::uint32_t scaledOwnMeshDivision_guess(
        double ratio, std::uint32_t meshDivision) {
        const std::uint32_t division =
            meshDoubleToUnsignedTowardZeroSaturated_guess(
                ratio * static_cast<double>(meshDivision));
        return division >= 50u && division < 0x80000000u
            ? 50u
            : division;
    }

    // The inherited-source branch uses an unsigned CS/CC comparison after
    // the same uint32 conversion and therefore caps every value >= 50.
    inline std::uint32_t scaledInheritedMeshDivision_guess(
        double ratio, std::uint32_t meshDivision) {
        const std::uint32_t division =
            meshDoubleToUnsignedTowardZeroSaturated_guess(
                ratio * static_cast<double>(meshDivision));
        return division >= 50u ? 50u : division;
    }

    // The split counters are formed with W-register arithmetic, stored as raw
    // words, then consumed by signed int loop conditions in the grid builder.
    inline int meshDivisionCounterWordToInt_guess(std::uint32_t word) {
        static_assert(sizeof(std::int32_t) == sizeof(word));
        std::int32_t signedWord;
        std::memcpy(&signedWord, &word, sizeof(signedWord));
        return static_cast<int>(signedWord);
    }

    // AArch64 UDIV returns zero for a zero divisor.  The ARMv7 plugins call
    // external runtime helpers whose zero-divisor policy is not in their
    // binary bytes; Web adopts the directly proven AArch64 profile.
    inline std::uint32_t unsignedDivideA64Profile_guess(
        std::uint32_t numerator, std::uint32_t denominator) {
        return denominator != 0u ? numerator / denominator : 0u;
    }

    // Ground-correction TJS callback. Player_updateLayers gates the call on the
    // node flag and supplies rootPlayer.currentDispatch; the worker separately
    // returns when that borrowed wrapper dispatch is null.
    inline void applyGroundCorrection_guess(
        iTJSDispatch2 *currentDispatch,
        motion::detail::MotionNode &node,
        const motion::detail::MotionNode &parent) {
        if(!currentDispatch) {
            return;
        }

        auto current = motion::detail::createTJSArrayWithItems_guess();
        current.items->emplace_back(node.accumulated.posX);
        current.items->emplace_back(node.accumulated.posY);
        current.items->emplace_back(node.accumulated.posZ);

        auto parentPosition = motion::detail::createTJSArrayWithItems_guess();
        parentPosition.items->emplace_back(parent.accumulated.posX);
        parentPosition.items->emplace_back(parent.accumulated.posY);
        parentPosition.items->emplace_back(parent.accumulated.posZ);

        tTJSVariant result;
        auto callbackOwner =
            RetainedVariantDispatch_guess::fromRawDispatch(currentDispatch);
        {
            // The two independently owning argument copies die immediately
            // after FuncCall, parent first and current second.
            tTJSVariant currentArgument(current.value);
            tTJSVariant parentArgument(parentPosition.value);
            tTJSVariant *callArgs[] = {
                &currentArgument, &parentArgument
            };
            (void)callbackOwner.get()->FuncCall(
                0, TJS_W("onGroundCorrection"),
                &motion::detail::onGroundCorrectionMemberHint_guess,
                &result, 2, callArgs, callbackOwner.get());
        }

        // getRealValue first probes with MEMBERMUSTEXIST, then performs a
        // second flags-0 read when the probe returns any nonnegative status.
        // Missing coordinates independently use 0.0; conversion/callback
        // exceptions propagate and writes remain incremental in x/y/z order.
        ncbPropAccessor corrected{tTJSVariant(result)};
        node.accumulated.posX = corrected.getRealValue(
            static_cast<tjs_int32>(0), 0.0);
        node.accumulated.posY = corrected.getRealValue(
            static_cast<tjs_int32>(1), 0.0);
        node.accumulated.posZ = corrected.getRealValue(
            static_cast<tjs_int32>(2), 0.0);
    }
} // anonymous namespace
