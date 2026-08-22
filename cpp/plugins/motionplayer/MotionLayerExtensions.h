#pragma once

#include "ComplexRect.h"
#include "tjs.h"

namespace motion {

    // Stateless helper class attached to the script Layer class as the
    // reference-native "BezierPatch" registration.  Its methods operate on
    // flat [x, y, ...] TJS arrays and do not create per-Layer native state.
    class BezierPatch final {
    public:
        [[nodiscard]] static tTJSVariant affinePatch(
            tTJSVariant flatPoints,
            tjs_real m11,
            tjs_real m12,
            tjs_real m21,
            tjs_real m22);
        [[nodiscard]] static tTJSVariant translatePatch(
            tTJSVariant flatPoints,
            tjs_real offsetX,
            tjs_real offsetY);
        [[nodiscard]] static tTJSVariant affineTranslatePatch(
            tTJSVariant flatPoints,
            tjs_real m11,
            tjs_real m12,
            tjs_real m21,
            tjs_real m22,
            tjs_real offsetX,
            tjs_real offsetY);
        [[nodiscard]] static tTJSVariant calcPatchBounds(
            tTJSVariant flatPoints);
        [[nodiscard]] static tTJSVariant calcMeshBounds(
            tTJSVariant flatControlPoints);
        [[nodiscard]] static tTJSVariant calcBezierPatch(
            tTJSVariant flatControlPoints,
            tjs_real u,
            tjs_real v);
        [[nodiscard]] static tTJSVariant calcBezierPatchList(
            tTJSVariant flatControlPoints,
            tTJSVariant flatParameters);
        [[nodiscard]] static tTJSVariant reverseCalcBezierPatch(
            tTJSVariant flatControlPoints,
            tjs_real targetX,
            tjs_real targetY);
    };

    // Per-Layer native state installed lazily by the motionplayer module's
    // attached-class hook.  The owner pointer is non-owning: the TJS Layer
    // owns the attached native adaptor and invalidates it during its own
    // lifetime teardown.
    class MotionLayerExtensions_guess final {
    public:
        explicit MotionLayerExtensions_guess(iTJSDispatch2 *owner) noexcept;
        MotionLayerExtensions_guess(const MotionLayerExtensions_guess &) =
            delete;
        MotionLayerExtensions_guess &operator=(
            const MotionLayerExtensions_guess &) = delete;

        [[nodiscard]] tTJSVariant getDebugMeshApp() const;
        void setDebugMeshApp(tTJSVariant value);
        [[nodiscard]] tTJSVariant getDebugBezierApp() const;
        void setDebugBezierApp(tTJSVariant value);

        void meshCopy(tTJSVariant source,
                      tjs_int sourceLeft,
                      tjs_int sourceTop,
                      tjs_int sourceWidth,
                      tjs_int sourceHeight,
                      tTJSVariant flatPoints,
                      tjs_int divisionX,
                      tjs_int divisionY,
                      tjs_int stretchType,
                      bool clear);
        void operateMesh(tTJSVariant source,
                         tjs_int sourceLeft,
                         tjs_int sourceTop,
                         tjs_int sourceWidth,
                         tjs_int sourceHeight,
                         tTJSVariant flatPoints,
                         tjs_int divisionX,
                         tjs_int divisionY,
                         tjs_int mode,
                         tjs_int opacity,
                         tjs_int stretchType);
        void drawMeshFrame(tTJSVariant outline,
                           tTJSVariant meshline,
                           tTJSVariant flatPoints,
                           tjs_int divisionX,
                           tjs_int divisionY);
        void bezierPatchCopy(tTJSVariant source,
                             tjs_int sourceLeft,
                             tjs_int sourceTop,
                             tjs_int sourceWidth,
                             tjs_int sourceHeight,
                             tTJSVariant flatControlPoints,
                             tjs_int divisionX,
                             tjs_int divisionY,
                             tjs_int stretchType,
                             bool clear);
        void operateBezierPatch(tTJSVariant source,
                                tjs_int sourceLeft,
                                tjs_int sourceTop,
                                tjs_int sourceWidth,
                                tjs_int sourceHeight,
                                tTJSVariant flatControlPoints,
                                tjs_int divisionX,
                                tjs_int divisionY,
                                tjs_int mode,
                                tjs_int opacity,
                                tjs_int stretchType);
        void drawBezierPatchFrame(tTJSVariant outline,
                                  tTJSVariant meshline,
                                  tTJSVariant flatControlPoints);
        void drawBezierPatchMeshFrame(tTJSVariant outline,
                                      tTJSVariant meshline,
                                      tTJSVariant flatControlPoints,
                                      tjs_int divisionX,
                                      tjs_int divisionY);

    private:
        // The field order is load-bearing.  It gives the two Variant fields
        // the offsets shared by the 64-bit and 32-bit reference layouts.
        iTJSDispatch2 *_owner;
        tjs_int _faceCache_guess = 0;
        tTJSVariant _debugMeshApp;
        tTJSVariant _debugBezierApp;

        void refreshFace_guess();
        [[nodiscard]] tjs_int resolveAutoMode_guess() const;
        [[nodiscard]] bool resolveBitmapMethod_guess(
            tjs_int mode, tjs_int &bitmapMethod);
        void renderMesh_guess(tTJSVariant source,
                              const tTVPRect &sourceRect,
                              tTJSVariant flatPoints,
                              tjs_int divisionX,
                              tjs_int divisionY,
                              tjs_int bitmapMethod,
                              bool holdAlpha,
                              tjs_int opacity,
                              tjs_int stretchType);
        void renderBezierPatch_guess(tTJSVariant source,
                                     const tTVPRect &sourceRect,
                                     tTJSVariant flatControlPoints,
                                     tjs_int divisionX,
                                     tjs_int divisionY,
                                     tjs_int bitmapMethod,
                                     bool holdAlpha,
                                     tjs_int opacity,
                                     tjs_int stretchType);
    };

} // namespace motion
