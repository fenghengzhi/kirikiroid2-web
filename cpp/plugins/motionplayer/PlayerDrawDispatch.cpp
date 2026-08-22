// PlayerDrawDispatch.cpp — typed draw entry and draw target routing
// Split out for maintainability.
//
#include "PlayerRenderInternal.h"
#include "MotionTraceWeb.h"
#include "ncbind.hpp"
#include "tjsDebug.h"

using namespace motion::internal;

namespace {
    std::string shortTJSStackTrace(tjs_int limit = 8) {
        ttstr stack = TJSGetStackTraceString(limit, TJS_W(" <- "));
        return stack.AsStdString();
    }
}

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

        if(detail::logoChainTraceEnabled()) {
            const auto motionPath = matchedMotionPath();
            detail::logoChainTraceLogf(
                motionPath, "setDrawAffine", "setDrawAffineTranslateMatrix",
                _clampedEvalTime,
                "matrix=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}] nonIdentityFlag={}",
                m11, m21, m12, m22, m14, m24,
                _drawAffineMatrixNonIdentity ? 1 : 0);
        }
        return _drawAffineMatrixNonIdentity;
    }

    // The registered typed method owns one by-value Variant and contains the
    // renderer directly. The trace event spelling remains stable for existing
    // differential captures; it does not denote a second native member.
    void Player::draw(tTJSVariant target) {
        const bool logoTraceEnabled = detail::logoChainTraceEnabled();
        std::string motionPath;
        bool logoTraceEnabledForPath = false;
        if(logoTraceEnabled) {
            // Native draw dispatch has no motion-path or stack-string work.
            // Materialize both only for the opt-in Web trace sidecar.
            motionPath = matchedMotionPath();
            logoTraceEnabledForPath =
                detail::logoChainTraceEnabledForPath(motionPath);
        }
        iTJSDispatch2 *paramObj =
            target.Type() == tvtObject ? target.AsObjectNoAddRef() : nullptr;
        if(logoTraceEnabledForPath) {
            detail::logoChainTraceLogf(
                motionPath, "drawCompat.enter", "drawCompat", _clampedEvalTime,
                "argType={} targetObj={} d3dMode={} allplaying={} nodes={} stack={}",
                static_cast<int>(target.Type()),
                static_cast<const void *>(paramObj), _d3dDrawMode ? 1 : 0,
                _allplaying ? 1 : 0, _nodes.size(), shortTJSStackTrace());
        }
#if defined(KRKR2_WASMTIME_HEADLESS)
        detail::MotionTraceRenderDrawScope renderTrace(this, &target, paramObj);
#endif

        if(!paramObj) {
#if defined(KRKR2_WASMTIME_HEADLESS)
            renderTrace.setRoute("no_target");
#endif
            if(logoTraceEnabledForPath) {
                detail::logoChainTraceLogf(
                    motionPath, "drawCompat.dispatch", "drawCompat",
                    _clampedEvalTime,
                    "route=no-param");
            }
            return;
        }

        // Step 1: all four references check the D3DAdaptor NCB class ID first.
        // A hit forces the persistent mode byte true and renders immediately;
        // no later draw route implicitly clears that byte.
        {
            auto *d3dAdaptor =
                ncbInstanceAdaptor<D3DAdaptor>::GetNativeInstance(paramObj, false);
            if(d3dAdaptor) {
#if defined(KRKR2_WASMTIME_HEADLESS)
                renderTrace.recordTargetCheckD3D(true);
                renderTrace.setRoute("d3d_adaptor");
#endif
                if(logoTraceEnabledForPath) {
                    detail::logoChainTraceCheck(
                        motionPath, "drawCompat.dispatch", "drawCompat",
                        _clampedEvalTime,
                        "D3DAdaptor -> Player_drawD3D",
                        "D3DAdaptor -> Player_drawD3D", true,
                        "drawCompat D3D routing mismatch");
                }
                _d3dDrawMode = true;
                renderToD3DAdaptor(d3dAdaptor);
                return;
            }
#if defined(KRKR2_WASMTIME_HEADLESS)
            renderTrace.recordTargetCheckD3D(false);
#endif
        }

        // Step 2: Check if param is SLA.
        // The native code only checks the SeparateLayerAdaptor class ID here.
        // It does not route plain Layer objects through the SLA backend just
        // because they resolve to an owner/target layer.
        {
            auto *sla =
                ncbInstanceAdaptor<SeparateLayerAdaptor>::GetNativeInstance(
                    paramObj, false);
            if(sla) {
#if defined(KRKR2_WASMTIME_HEADLESS)
                renderTrace.recordTargetCheckSLA(true);
                renderTrace.setRoute("separate_layer_adaptor");
#endif
                if(logoTraceEnabledForPath) {
                    detail::logoChainTraceCheck(
                        motionPath, "drawCompat.dispatch", "drawCompat",
                        _clampedEvalTime,
                        "SeparateLayerAdaptor -> Player_DrawSLA",
                        "SeparateLayerAdaptor -> Player_DrawSLA", true,
                        "drawCompat SLA routing mismatch");
                }
                renderToSeparateLayerAdaptor(sla);
                return;
            }
#if defined(KRKR2_WASMTIME_HEADLESS)
            renderTrace.recordTargetCheckSLA(false);
#endif
        }

        // Step 3: ordinary render-list path. The four references do not
        // implicitly load here: an empty motion owner exits before preparation.
        if(!hasMotionContent()) {
#if defined(KRKR2_WASMTIME_HEADLESS)
            renderTrace.setRoute("no_motion");
#endif
            if(logoTraceEnabledForPath) {
                detail::logoChainTraceLogf(
                    motionPath, "drawCompat.dispatch", "drawCompat",
                    _clampedEvalTime,
                    "route=no-motion");
            }
            return;
        }

        detail::PreparedRenderItemList mainList;
        detail::PreparedRenderItemList auxList;
        const bool prepareOk = prepareRenderItems(mainList, auxList);
#if defined(KRKR2_WASMTIME_HEADLESS)
        renderTrace.recordPrepareResult(prepareOk);
#endif
        if(!prepareOk) {
#if defined(KRKR2_WASMTIME_HEADLESS)
            renderTrace.setRoute("prepare_empty");
#endif
            if(logoTraceEnabledForPath) {
                detail::logoChainTraceCheck(
                    motionPath, "drawCompat.dispatch", "drawCompat",
                    _clampedEvalTime,
                    "prepareRenderItems should produce a render list",
                    "prepareRenderItems returned false", false,
                    "drawCompat ordinary path stopped before renderToCanvas");
            }
            return;
        }

#if defined(KRKR2_WASMTIME_HEADLESS)
        renderTrace.recordBranchAfterPrepare(_d3dDrawMode);
#endif
        if(_d3dDrawMode) {
            renderViaSharedD3DAdaptor(target, mainList);
#if defined(KRKR2_WASMTIME_HEADLESS)
            renderTrace.setRoute("shared_d3d_after_prepare");
#endif
            if(logoTraceEnabledForPath) {
                detail::logoChainTraceCheck(
                    motionPath, "drawCompat.dispatch", "drawCompat",
                    _clampedEvalTime,
                    "prepareRenderItems -> shared D3D render path",
                    "shared_d3d", true,
                    "drawCompat shared D3D path failed");
            }
            return;
        }

        applyPreparedRenderItemProjection_guess(mainList);
#if defined(KRKR2_WASMTIME_HEADLESS)
        renderTrace.recordApplyPreparedProjection();
#endif
        renderToCanvas_guess(target, mainList, auxList);
#if defined(KRKR2_WASMTIME_HEADLESS)
        renderTrace.recordRenderToCanvas(true);
#endif
#if defined(KRKR2_WASMTIME_HEADLESS)
        const bool internalAssignRequested = _needsInternalAssignImages;
#endif
        updateLayerAfterDrawRecovered_guess(target);
#if defined(KRKR2_WASMTIME_HEADLESS)
        renderTrace.recordUpdateLayerAfterDraw(
            internalAssignRequested, true);
        renderTrace.setRoute("ordinary_layer");
#endif
        if(logoTraceEnabledForPath) {
            detail::logoChainTraceCheck(
                motionPath, "drawCompat.dispatch", "drawCompat",
                _clampedEvalTime,
                "prepareRenderItems -> applyPreparedProjection -> renderToCanvas(copy(target)) -> updateLayerAfterDraw(target)",
                "render_to_canvas", true,
                "drawCompat ordinary render path failed");
        }
    }

} // namespace motion
