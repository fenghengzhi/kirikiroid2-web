// PlayerRenderTargets.cpp — Layer/SLA/D3D render targets and post-draw update
// Split from PlayerRender.cpp for maintainability.
//
#include "PlayerRenderInternal.h"
#include "MotionTraceWeb.h"
#include "ncbind.hpp"

using namespace motion::internal;
using namespace motion::internal::render_detail;

namespace motion {
    bool Player::renderViaSharedD3DAdaptor(iTJSDispatch2 *targetLayerObject) {
        if(!targetLayerObject) {
            return false;
        }

        auto *resolvedTarget = targetLayerObject;
        tTJSVariant wrapper(targetLayerObject, targetLayerObject);
        if(auto *resolved = tryResolveLayerDispatch(wrapper)) {
            resolvedTarget = resolved;
        }

        auto *targetLayer = resolveNativeLayer(resolvedTarget);
        if(!targetLayer) {
            return false;
        }

        auto *sharedAdaptor = ensureSharedD3DAdaptor(resolvedTarget);
        if(!sharedAdaptor) {
            return false;
        }

        if(!renderToD3DAdaptor(sharedAdaptor)) {
            return false;
        }

        if(sharedAdaptor->getWidth() > 0 && sharedAdaptor->getHeight() > 0) {
            targetLayer->SetSize(sharedAdaptor->getWidth(),
                                 sharedAdaptor->getHeight());
        }
        targetLayer->SetVisible(true);

        tTJSVariant targetVar(resolvedTarget, resolvedTarget);
        tTJSVariant *args[] = { &targetVar };
        if(TJS_FAILED(sharedAdaptor->captureCanvas(nullptr, 1, args, nullptr))) {
            return false;
        }

        targetLayer->Update(false);
        _runtime->lastCanvas = tTJSVariant(resolvedTarget, resolvedTarget);
        return true;
    }


    iTJSDispatch2 *Player::resolveSeparateLayerRenderTarget(
        SeparateLayerAdaptor *sla,
        iTJSDispatch2 *fallbackOwner,
        int &canvasWidth,
        int &canvasHeight) {
        canvasWidth = 0;
        canvasHeight = 0;
        if(!sla) {
            return nullptr;
        }

        iTJSDispatch2 *targetLayerObject = nullptr;
        if(auto *resolved = tryResolveLayerDispatch(sla->getTargetLayer())) {
            targetLayerObject = resolved;
        }
        if(!targetLayerObject) {
            targetLayerObject = fallbackOwner;
        }
        if(!targetLayerObject) {
            return nullptr;
        }

        sla->setTargetLayer(tTJSVariant(targetLayerObject, targetLayerObject));
        if(!queryLayerCanvasSize(targetLayerObject, canvasWidth, canvasHeight)) {
            return nullptr;
        }

        iTJSDispatch2 *renderTarget = ensureReusableLayerObject(
            sla->privateRenderTargetSlot(),
            resolveLayerTreeOwnerObject(targetLayerObject),
            targetLayerObject,
            static_cast<tTVPLayerType>(ltAlpha),
            true,
            sla->getAbsolute());
        if(!renderTarget) {
            return nullptr;
        }

        sla->setPrivateRenderTarget(tTJSVariant(renderTarget, renderTarget));
        if(auto *renderLayer = resolveNativeLayer(renderTarget)) {
            renderLayer->SetSize(canvasWidth, canvasHeight);
            renderLayer->SetVisible(true);
        }
        return renderTarget;
    }

    bool Player::renderMotionFrameToTarget(iTJSDispatch2 *renderTargetObject,
                                           tjs_int canvasWidth,
                                           tjs_int canvasHeight,
                                           const char *traceFunc) {
        if(!renderTargetObject || canvasWidth <= 0 || canvasHeight <= 0) {
            return false;
        }
        if(!prepareLayerForRender(renderTargetObject, canvasWidth, canvasHeight,
                                  0x00000000)) {
            return false;
        }

        const auto motionPath =
            _runtime && _runtime->activeMotion ? _runtime->activeMotion->path
                                               : std::string{};
        detail::logoChainTraceLogf(
            motionPath, "sla.renderMotionFrame", "0x6DE738",
            _clampedEvalTime,
            "target={} canvas={}x{} route={}",
            static_cast<const void *>(renderTargetObject),
            canvasWidth, canvasHeight,
            traceFunc ? traceFunc : "0x6DE738");

        buildRenderCommands(canvasWidth, canvasHeight);
        return executeLayerRenderCommands(renderTargetObject, true);
    }

    bool Player::renderToD3DAdaptor(D3DAdaptor *adaptor) {
        if(!adaptor || adaptor->getWidth() <= 0 || adaptor->getHeight() <= 0) {
            return false;
        }
        // Guard against recursion: D3D capture can re-enter drawCompat.
        static bool s_inRenderToD3D = false;
        if(s_inRenderToD3D) return false;
        s_inRenderToD3D = true;
        struct Guard { ~Guard() { s_inRenderToD3D = false; } } guard;

        ensureMotionLoaded();
        if(!_runtime->activeMotion) return false;
        const auto motionPath = _runtime->activeMotion->path;
        detail::logoChainTraceLogf(
            motionPath, "draw.d3d", "0x6D5B90", _clampedEvalTime,
            "adaptorSize={}x{} route=D3DAdaptor_renderFromPlayer",
            adaptor->getWidth(), adaptor->getHeight());

        prepareRenderItems();
        applyPreparedRenderItemTranslateOffsets();

        iTJSDispatch2 *renderLayerObject =
            ensureReusableLayerObject(_runtime->internalRenderLayer,
                                      adaptor->getWindowObject(),
                                      nullptr,
                                      static_cast<tTVPLayerType>(ltAlpha),
                                      false);
        if(!renderLayerObject) {
            return false;
        }
        if(!prepareLayerForRender(renderLayerObject, adaptor->getWidth(),
                                  adaptor->getHeight(), 0x00000000)) {
            return false;
        }

        buildRenderCommands(adaptor->getWidth(), adaptor->getHeight());
        executeLayerRenderCommands(renderLayerObject, true);

        // D3D backend still ends with copying pixels into the adaptor buffer,
        // but it now consumes prepared items directly instead of recursing into
        // renderToLayer().
        tTJSNI_BaseLayer *layer = nullptr;
        if(TJS_FAILED(renderLayerObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&layer))) || !layer) {
            return false;
        }

        const int w = adaptor->getWidth();
        const int h = adaptor->getHeight();
        const int layerW = static_cast<int>(layer->GetImageWidth());
        const int layerH = static_cast<int>(layer->GetImageHeight());
        const auto *srcBuf = reinterpret_cast<const std::uint8_t *>(
            layer->GetMainImagePixelBuffer());
        auto srcPitch = layer->GetMainImagePixelBufferPitch();

        if(!srcBuf || srcPitch <= 0 || layerW <= 0 || layerH <= 0) return false;

        // Resize adaptor buffer if needed
        if(w != layerW || h != layerH) {
            adaptor->setSize(layerW, layerH);
        }
        adaptor->clearBuffer();

        auto *dstBuf = adaptor->getBuffer();
        const auto dstPitch = adaptor->getBufferPitch();
        const int copyH = std::min(layerH, adaptor->getHeight());
        const int copyRowBytes = std::min(
            static_cast<int>(layerW * 4), dstPitch);

        for(int y = 0; y < copyH; ++y) {
            std::memcpy(dstBuf + dstPitch * y,
                        srcBuf + srcPitch * y,
                        static_cast<size_t>(copyRowBytes));
        }

        return true;
    }

    bool Player::renderToCanvasLike_0x6C7440(
        tTJSVariant *target, bool willCallUpdateLayerAfterDraw) {
        if(!target) {
            return false;
        }

        ensureMotionLoaded();
        if(!_runtime || !_runtime->activeMotion) {
            return false;
        }
        const auto motionPath = _runtime->activeMotion->path;

        iTJSDispatch2 *resolvedLayerObject = tryResolveLayerDispatch(*target);
        if(!resolvedLayerObject && target->Type() == tvtObject) {
            resolvedLayerObject = target->AsObjectNoAddRef();
        }
        if(!resolvedLayerObject) {
            detail::logoChainTraceCheck(
                motionPath, "draw.renderToCanvas", "0x6C7440",
                _clampedEvalTime,
                "target variant should resolve to a Layer object",
                "target did not resolve", false,
                "Player_renderToCanvas_guess could not resolve target variant");
            return false;
        }

        int canvasWidth = 0;
        int canvasHeight = 0;
        if(!queryLayerCanvasSize(resolvedLayerObject, canvasWidth, canvasHeight) &&
           _runtime->activeMotion) {
            canvasWidth = static_cast<int>(_runtime->activeMotion->width);
            canvasHeight = static_cast<int>(_runtime->activeMotion->height);
        }
        if(canvasWidth <= 0 || canvasHeight <= 0) {
            return false;
        }

        const bool useInternalRenderLayer =
            _needsInternalAssignImages && willCallUpdateLayerAfterDraw;
        detail::logoChainTraceLogf(
            motionPath, "draw.renderToCanvas", "0x6C7440", _clampedEvalTime,
            "targetLayerCanvas={}x{} willCallUpdateLayerAfterDraw={} needsInternalAssignImages={} useInternalRenderLayer={}",
            canvasWidth, canvasHeight,
            willCallUpdateLayerAfterDraw ? 1 : 0,
            _needsInternalAssignImages ? 1 : 0,
            useInternalRenderLayer ? 1 : 0);

        iTJSDispatch2 *renderLayerObject = resolvedLayerObject;
        if(useInternalRenderLayer) {
            renderLayerObject = ensureReusableLayerObject(
                _runtime->internalRenderLayer,
                resolveLayerTreeOwnerObject(resolvedLayerObject),
                resolvedLayerObject,
                static_cast<tTVPLayerType>(ltAlpha),
                false);
        }
        if(renderLayerObject != resolvedLayerObject) {
            if(!prepareLayerForRender(renderLayerObject, canvasWidth, canvasHeight,
                                      0x00000000)) {
                return false;
            }
        } else if(auto *targetLayer = resolveNativeLayer(resolvedLayerObject)) {
            if(targetLayer->GetWidth() != canvasWidth ||
               targetLayer->GetHeight() != canvasHeight) {
                targetLayer->SetSize(canvasWidth, canvasHeight);
            }
        } else {
            return false;
        }

        buildRenderCommands(canvasWidth, canvasHeight);
        if(!executeLayerRenderCommands(renderLayerObject, true)) {
            return false;
        }

        _runtime->lastCanvas =
            tTJSVariant(resolvedLayerObject, resolvedLayerObject);
        detail::logoChainTraceSummary(
            motionPath, "renderToCanvasLike_0x6C7440", _clampedEvalTime,
            useInternalRenderLayer ? "internalRenderLayer=1"
                                   : "internalRenderLayer=0");
        return true;
    }

    bool Player::renderToLayer(iTJSDispatch2 *layerObject, bool skipUpdate) {
        if(!layerObject) {
            return false;
        }

        ensureMotionLoaded();
        if(!_runtime || !_runtime->activeMotion) {
            return false;
        }
        const auto motionPath = _runtime->activeMotion->path;

        tTJSVariant target(layerObject, layerObject);
        iTJSDispatch2 *resolvedLayerObject = layerObject;
        if(auto *resolved = tryResolveLayerDispatch(target)) {
            resolvedLayerObject = resolved;
        }

        int canvasWidth = 0;
        int canvasHeight = 0;
        if(!queryLayerCanvasSize(resolvedLayerObject, canvasWidth, canvasHeight) &&
            _runtime->activeMotion) {
            canvasWidth = static_cast<int>(_runtime->activeMotion->width);
            canvasHeight = static_cast<int>(_runtime->activeMotion->height);
        }
        if(canvasWidth <= 0 || canvasHeight <= 0) {
            return false;
        }
        detail::logoChainTraceLogf(
            motionPath, "draw.layer", "0x6C7440/0x6CE7D8", _clampedEvalTime,
            "targetLayerCanvas={}x{} skipUpdate={} needsInternalAssignImages={}",
            canvasWidth, canvasHeight, skipUpdate ? 1 : 0,
            _needsInternalAssignImages ? 1 : 0);

        prepareRenderItems();
        applyPreparedRenderItemTranslateOffsets();

        const bool needsInternalAssignBeforeRender =
            _needsInternalAssignImages && !skipUpdate;
        if(!renderToCanvasLike_0x6C7440(&target, !skipUpdate)) {
            return false;
        }

        if(!skipUpdate) {
            if(needsInternalAssignBeforeRender) {
                updateLayerAfterDrawLike_0x6CE7D8(&target);
            } else if(auto *layer = resolveNativeLayer(resolvedLayerObject)) {
                if(detail::logoSnapshotMarkEnabledForPath(motionPath) &&
                   motionPath.find("m2logo.mtn") != std::string::npos &&
                   _clampedEvalTime >= 30.0 && _clampedEvalTime <= 50.0) {
                    std::fprintf(stderr,
                                 "SNAPLAYER phase=beforeUpdate frame=%.3f %s\n",
                                 _clampedEvalTime,
                                 summarizeLayerChildren(layer).c_str());
                }
                layer->Update(false);
                detail::logoChainTraceLogf(
                    motionPath, "post.layer", "0x6CE7D8", _clampedEvalTime,
                    "targetLayer.Update(false) size={}x{}",
                    layer->GetWidth(), layer->GetHeight());
                if(detail::logoSnapshotMarkEnabledForPath(motionPath) &&
                   motionPath.find("m2logo.mtn") != std::string::npos &&
                   _clampedEvalTime >= 30.0 && _clampedEvalTime <= 50.0) {
                    std::fprintf(stderr,
                                 "SNAPLAYER phase=afterUpdate frame=%.3f %s\n",
                                 _clampedEvalTime,
                                 summarizeLayerChildren(layer).c_str());
                }
            }
        }

        detail::logoChainTraceSummary(
            motionPath, "renderToLayer", _clampedEvalTime,
            skipUpdate ? "skipUpdate=1" : "skipUpdate=0");
        return true;
    }

    bool Player::renderToSeparateLayerAdaptor(iTJSDispatch2 *slaObject) {
        if(!slaObject || !_runtime) {
            return false;
        }

        SeparateLayerAdaptor *sla =
            ncbInstanceAdaptor<SeparateLayerAdaptor>::GetNativeInstance(
                slaObject, false);
        iTJSDispatch2 *ownerLayer = sla ? sla->getOwner() : nullptr;
        if(!ownerLayer) {
            ownerLayer = tryResolveSeparateAdaptorOwner(tTJSVariant(slaObject, slaObject));
        }
        if(!ownerLayer) {
            return false;
        }

        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return false;
        }
        const auto motionPath = _runtime->activeMotion->path;

        int canvasWidth = 0;
        int canvasHeight = 0;
        iTJSDispatch2 *renderTarget =
            resolveSeparateLayerRenderTarget(sla, ownerLayer, canvasWidth,
                                             canvasHeight);
        if(!renderTarget) {
            detail::logoChainTraceSummary(
                motionPath, "renderToSeparateLayerAdaptor", _clampedEvalTime,
                "fail=resolveSeparateLayerRenderTarget");
            return false;
        }
        detail::logoChainTraceLogf(
            motionPath, "draw.sla", "0x6D5658", _clampedEvalTime,
            "ownerLayer={} targetCanvas={}x{} accurate={} route={}",
            static_cast<const void *>(ownerLayer),
            canvasWidth, canvasHeight,
            isAccurateSlaRenderEnabled() ? 1 : 0,
            isAccurateSlaRenderEnabled()
                ? "0x6C9CA8 -> 0x6CE938"
                : "Player_RenderMotionFrame -> Layer_UpdateRect");
        detail::logoChainTraceLogf(
            motionPath, "sla.resolveTarget", "0x6D5948",
            _clampedEvalTime,
            "targetLayer={} privateTarget={} absolute={} canvas={}x{}",
            static_cast<const void *>(tryResolveLayerDispatch(sla->getTargetLayer())),
            static_cast<const void *>(renderTarget),
            sla->getAbsolute() ? 1 : 0,
            canvasWidth, canvasHeight);

        prepareRenderItems();
        applyPreparedRenderItemTranslateOffsets();

        if(!renderMotionFrameToTarget(renderTarget, canvasWidth, canvasHeight,
                                      isAccurateSlaRenderEnabled()
                                          ? "0x6C9CA8"
                                          : "0x6DE738")) {
            detail::logoChainTraceSummary(
                motionPath, "renderToSeparateLayerAdaptor", _clampedEvalTime,
                "fail=renderMotionFrameToTarget");
            return false;
        }

        if(isAccurateSlaRenderEnabled()) {
            detail::logoChainTraceLogf(
                motionPath, "sla.accurate.begin", "0x6C9CA8",
                _clampedEvalTime,
                "target={} canvas={}x{}",
                static_cast<const void *>(renderTarget),
                canvasWidth, canvasHeight);
            updateAccurateSLAAfterDraw(renderTarget);
            detail::logoChainTraceLogf(
                motionPath, "sla.accurate.end", "0x6CE938",
                _clampedEvalTime,
                "target={}", static_cast<const void *>(renderTarget));
        } else if(auto *renderLayer = resolveNativeLayer(renderTarget)) {
            renderLayer->Update(false);
            detail::logoChainTraceLogf(
                motionPath, "sla.updateRect", "0x800F4C", _clampedEvalTime,
                "renderTarget.Update(false) size={}x{} ownerLayer={}",
                renderLayer->GetWidth(), renderLayer->GetHeight(),
                static_cast<const void *>(ownerLayer));
        } else {
            detail::logoChainTraceCheck(
                motionPath, "sla.updateRect", "0x800F4C", _clampedEvalTime,
                "renderTarget should expose a native layer for Update(false)",
                "renderTarget native layer missing", false,
                "Player_RenderMotionFrame finished but SLA target lacked a native layer");
        }

        _runtime->lastCanvas = tTJSVariant(ownerLayer, ownerLayer);
        detail::logoChainTraceSummary(
            motionPath, "renderToSeparateLayerAdaptor", _clampedEvalTime,
            isAccurateSlaRenderEnabled() ? "accurate=1" : "accurate=0");
        return true;
    }

    bool Player::updateLayerAfterDrawLike_0x6CE7D8(tTJSVariant *target) {
#if defined(KRKR2_WASMTIME_HEADLESS)
        iTJSDispatch2 *rawProbeLayerObject =
            target ? tryResolveLayerDispatch(*target) : nullptr;
        if(!rawProbeLayerObject && target && target->Type() == tvtObject) {
            rawProbeLayerObject = target->AsObjectNoAddRef();
        }
        detail::motionTraceRenderImageCheckpoint(
            this, rawProbeLayerObject, "updateLayerAfterDraw_pre",
            "Player::updateLayerAfterDraw_0x6CE7D8.enter.after-target-resolve");
        detail::motionTraceLayerRawProbe(
            this, rawProbeLayerObject, "updateLayerAfterDraw_0x6CE7D8.enter");
        struct UpdateLayerAfterDrawTraceLeave {
            Player *player;
            iTJSDispatch2 *layerObject;
            ~UpdateLayerAfterDrawTraceLeave() {
                detail::motionTraceRenderImageCheckpoint(
                    player, layerObject, "updateLayerAfterDraw_post",
                    "Player::updateLayerAfterDraw_0x6CE7D8.leave.before-return");
                detail::motionTraceLayerRawProbe(
                    player, layerObject,
                    "updateLayerAfterDraw_0x6CE7D8.leave");
            }
        } updateLayerAfterDrawTraceLeave{this, rawProbeLayerObject};
#endif
        if(!_needsInternalAssignImages) {
            return true;
        }
        const auto motionPath =
            _runtime && _runtime->activeMotion ? _runtime->activeMotion->path
                                               : std::string{};

        _needsInternalAssignImages = false;
        if(!target || !_runtime) {
            return false;
        }

        iTJSDispatch2 *renderLayerObject =
            _runtime->internalRenderLayer.Type() == tvtObject
                ? _runtime->internalRenderLayer.AsObjectNoAddRef()
                : nullptr;
        if(!renderLayerObject) {
            return false;
        }

        try {
            tTJSVariant targetVar;
            targetVar = *target;
            tTJSVariant *args[] = { &targetVar };
            const bool ok = TJS_SUCCEEDED(renderLayerObject->FuncCall(
                0, TJS_W("assignImages"), nullptr, nullptr, 1, args,
                renderLayerObject));
            detail::logoChainTraceCheck(
                motionPath, "post.assignImages", "0x6CE7D8",
                _clampedEvalTime,
                "internal render layer assignImages(original target variant)",
                ok ? "assignImages(target)" : "assignImages(failed)",
                ok,
                "sub_6CE7D8 failed to assign internal render layer to target");
            return ok;
        } catch(...) {
            detail::logoChainTraceCheck(
                motionPath, "post.assignImages", "0x6CE7D8",
                _clampedEvalTime,
                "internal render layer assignImages(original target variant)",
                "assignImages(threw)", false,
                "sub_6CE7D8 threw while assigning internal render layer");
            return false;
        }
    }

    bool Player::updateLayerAfterDraw(iTJSDispatch2 *targetLayerObject) {
        if(!targetLayerObject) {
            return !_needsInternalAssignImages;
        }
        tTJSVariant target(targetLayerObject, targetLayerObject);
        return updateLayerAfterDrawLike_0x6CE7D8(&target);
    }

    bool Player::updateAccurateSLAAfterDraw(iTJSDispatch2 *targetLayerObject) {
        if(!targetLayerObject) {
            return false;
        }
        const auto motionPath =
            _runtime && _runtime->activeMotion ? _runtime->activeMotion->path
                                               : std::string{};

        if(auto *layer = resolveNativeLayer(targetLayerObject)) {
            layer->Update(false);
            detail::logoChainTraceLogf(
                motionPath, "post.sla.accurate", "0x6C9CA8/0x6CE938",
                _clampedEvalTime,
                "route=renderTarget.Update(false) size={}x{}",
                layer->GetWidth(), layer->GetHeight());
            return true;
        }
        detail::logoChainTraceCheck(
            motionPath, "post.sla.accurate", "0x6C9CA8/0x6CE938",
            _clampedEvalTime,
            "accurate SLA should update the resolved render target",
            "no post-update target", false,
            "accurate SLA render finished without a target update");
        return false;
    }

} // namespace motion
