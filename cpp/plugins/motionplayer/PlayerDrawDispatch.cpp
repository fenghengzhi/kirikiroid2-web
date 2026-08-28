// PlayerDrawDispatch.cpp — typed draw entry and draw target routing
// Split out for maintainability.
//
#include "PlayerRenderInternal.h"
#include "MotionTraceWeb.h"
#include "ncbind.hpp"

namespace motion {
    bool Player::setDrawAffineTranslateMatrix(double m11, double m21,
                                              double m12, double m22,
                                              double m14, double m24) {
        _drawAffineM11 = m11;
        _drawAffineM12 = m12;
        _drawAffineM21 = m21;
        _drawAffineM22 = m22;
        _drawAffineM14 = static_cast<float>(m14);
        _drawAffineM24 = static_cast<float>(m24);
        _drawAffineMatrixNonIdentity =
            m11 != 1.0 || m21 != 0.0 || m12 != 0.0 || m22 != 1.0 ||
            m14 != 0.0 || m24 != 0.0;
        return _drawAffineMatrixNonIdentity;
    }

    // The registered typed method owns one by-value Variant and contains the
    // target router directly.
    void Player::draw(tTJSVariant target) {
#if defined(KRKR2_WASMTIME_HEADLESS)
        // This scope is compiled only into the dedicated Wasmtime differential
        // guest.  Keep the production router unchanged while exposing the same
        // entry/branch/leave boundaries sampled by the Android render oracle.
        auto *const motionTraceTargetObject = target.AsObjectNoAddRef();
        detail::MotionTraceRenderDrawScope motionTraceDrawScope(
            this, &target, motionTraceTargetObject);
#endif
        // Both probes independently perform the strict Variant-to-Object
        // conversion emitted by ncbind. A non-object target therefore throws;
        // an Object containing a null dispatch simply misses both class IDs.
        auto *d3dAdaptor =
            ncbInstanceAdaptor<D3DAdaptor>::GetNativeInstance(
                target.AsObjectNoAddRef(), false);
#if defined(KRKR2_WASMTIME_HEADLESS)
        motionTraceDrawScope.recordTargetCheckD3D(d3dAdaptor != nullptr);
#endif
        if(d3dAdaptor) {
            // Direct D3D publication is sticky and precedes preparation.
            _d3dDrawMode = true;
#if defined(KRKR2_WASMTIME_HEADLESS)
            motionTraceDrawScope.setRoute("d3d_adaptor");
#endif
            renderToD3DAdaptor(d3dAdaptor);
            return;
        }

        auto *sla =
            ncbInstanceAdaptor<SeparateLayerAdaptor>::GetNativeInstance(
                target.AsObjectNoAddRef(), false);
#if defined(KRKR2_WASMTIME_HEADLESS)
        motionTraceDrawScope.recordTargetCheckSLA(sla != nullptr);
#endif
        if(sla) {
#if defined(KRKR2_WASMTIME_HEADLESS)
            motionTraceDrawScope.setRoute("separate_layer_adaptor");
#endif
            renderToSeparateLayerAdaptor(sla);
            return;
        }

        detail::PreparedRenderItemList mainList;
        detail::PreparedRenderItemList auxList;
        const bool prepareOk = prepareRenderItems(mainList, auxList);
#if defined(KRKR2_WASMTIME_HEADLESS)
        motionTraceDrawScope.recordPrepareResult(prepareOk);
#endif
        if(!prepareOk) {
            return;
        }

#if defined(KRKR2_WASMTIME_HEADLESS)
        motionTraceDrawScope.recordBranchAfterPrepare(_d3dDrawMode);
#endif
        if(_d3dDrawMode) {
            renderViaSharedD3DAdaptor(target, mainList);
            return;
        }

        applyPreparedRenderItemProjection_guess(mainList);
#if defined(KRKR2_WASMTIME_HEADLESS)
        motionTraceDrawScope.recordApplyPreparedProjection();
#endif
        renderToCanvas_guess(target, mainList, auxList);
#if defined(KRKR2_WASMTIME_HEADLESS)
        motionTraceDrawScope.recordRenderToCanvas(true);
        const bool internalAssignRequested = _needsInternalAssignImages;
        detail::motionTraceRenderImageCheckpoint(
            this, motionTraceTargetObject, "updateLayerAfterDraw_pre",
            "Player::draw.before-updateLayerAfterDraw");
#endif
        updateLayerAfterDrawRecovered_guess(target);
#if defined(KRKR2_WASMTIME_HEADLESS)
        detail::motionTraceRenderImageCheckpoint(
            this, motionTraceTargetObject, "updateLayerAfterDraw_post",
            "Player::draw.after-updateLayerAfterDraw");
        motionTraceDrawScope.recordUpdateLayerAfterDraw(
            internalAssignRequested, true);
#endif
    }

} // namespace motion
