//
// Created by LiDon on 2025/9/13.
// Reverse-engineered from libkrkr2.so emoteplayer.dll + motionplayer.dll
//
#include <spdlog/spdlog.h>
#include "tjs.h"
#include "ncbind.hpp"
#include "psbfile/PSBFile.h"

#include "ResourceManager.h"
#include "EmotePlayer.h"
#include "Player.h"
#include "SeparateLayerAdaptor.h"
#include "D3DEmoteModule.h"
#include "SourceCache.h"

using namespace motion;

#define NCB_MODULE_NAME TJS_W("motionplayer.dll")
#define LOGGER spdlog::get("plugin")

// ============================================================
// Subclass registrations (used as Motion.XXX)
// ============================================================

NCB_REGISTER_SUBCLASS_DELAY(SourceCache) { NCB_CONSTRUCTOR(()); }
NCB_REGISTER_SUBCLASS_DELAY(ObjSource) { NCB_CONSTRUCTOR(()); }
NCB_REGISTER_SUBCLASS_DELAY(SeparateLayerAdaptor) { NCB_CONSTRUCTOR(()); }

NCB_REGISTER_CLASS(Player) {
    NCB_CONSTRUCTOR(());

    // Properties
    NCB_PROPERTY(completionType, getCompletionType, setCompletionType);
    NCB_PROPERTY(motionKey, getMotionKey, setMotionKey);
    NCB_PROPERTY(outline, getOutline, setOutline);
    NCB_PROPERTY(priorDraw, getPriorDraw, setPriorDraw);
    NCB_PROPERTY(frameLastTime, getFrameLastTime, setFrameLastTime);
    NCB_PROPERTY(frameLoopTime, getFrameLoopTime, setFrameLoopTime);
    NCB_PROPERTY(loopTime, getLoopTime, setLoopTime);
    NCB_PROPERTY(processedMeshVerticesNum, getProcessedMeshVerticesNum,
                 setProcessedMeshVerticesNum);
    NCB_PROPERTY(queuing, getQueuing, setQueuing);
    NCB_PROPERTY(directEdit, getDirectEdit, setDirectEdit);
    NCB_PROPERTY(selectorEnabled, getSelectorEnabled, setSelectorEnabled);
    NCB_PROPERTY(variableKeys, getVariableKeys, setVariableKeys);
    NCB_PROPERTY(allplaying, getAllplaying, setAllplaying);
    NCB_PROPERTY(syncWaiting, getSyncWaiting, setSyncWaiting);
    NCB_PROPERTY(syncActive, getSyncActive, setSyncActive);
    NCB_PROPERTY(hasCamera, getHasCamera, setHasCamera);
    NCB_PROPERTY(cameraActive, getCameraActive, setCameraActive);
    NCB_PROPERTY(stereovisionActive, getStereovisionActive,
                 setStereovisionActive);
    NCB_PROPERTY(tickCount, getTickCount, setTickCount);
    NCB_PROPERTY(frameTickCount, getFrameTickCount, setFrameTickCount);
    NCB_PROPERTY(colorWeight, getColorWeight, setColorWeight);
    NCB_PROPERTY(independentLayerInherit, getIndependentLayerInherit,
                 setIndependentLayerInherit);
    NCB_PROPERTY(zFactor, getZFactor, setZFactor);
    NCB_PROPERTY(cameraTarget, getCameraTarget, setCameraTarget);
    NCB_PROPERTY(cameraPosition, getCameraPosition, setCameraPosition);
    NCB_PROPERTY(cameraFOV, getCameraFOV, setCameraFOV);
    NCB_PROPERTY(cameraAlive, getCameraAlive, setCameraAlive);
    NCB_PROPERTY(canvasCaptureEnabled, getCanvasCaptureEnabled,
                 setCanvasCaptureEnabled);
    NCB_PROPERTY(clearEnabled, getClearEnabled, setClearEnabled);
    NCB_PROPERTY(hitThreshold, getHitThreshold, setHitThreshold);
    NCB_PROPERTY(preview, getPreview, setPreview);
    NCB_PROPERTY(outsideFactor, getOutsideFactor, setOutsideFactor);
    NCB_PROPERTY(resourceManager, getResourceManager, setResourceManager);
    NCB_PROPERTY(stealthChara, getStealthChara, setStealthChara);
    NCB_PROPERTY(stealthMotion, getStealthMotion, setStealthMotion);
    NCB_PROPERTY(tags, getTags, setTags);
    NCB_PROPERTY(project, getProject, setProject);
    NCB_PROPERTY(useD3D, getUseD3D, setUseD3D);
    NCB_PROPERTY(meshline, getMeshline, setMeshline);

    // Core methods
    NCB_METHOD(initPhysics);
    NCB_METHOD(unserialize);
    NCB_METHOD(setRotate);
    NCB_METHOD(setMirror);
    NCB_METHOD(setHairScale);
    NCB_METHOD(setPartsScale);
    NCB_METHOD(setBustScale);
    NCB_METHOD(setDrawAffineTranslateMatrix);
    NCB_METHOD(getCameraOffset);
    NCB_METHOD(setCameraOffset);
    NCB_METHOD(modifyRoot);
    NCB_METHOD(debugPrint);

    // Resource management
    NCB_METHOD(unload);
    NCB_METHOD(unloadAll);
    NCB_METHOD(isExistMotion);
    NCB_METHOD(findMotion);
    NCB_METHOD(requireLayerId);
    NCB_METHOD(releaseLayerId);

    // Drawing/rendering
    NCB_METHOD(setClearColor);
    NCB_METHOD(setResizable);
    NCB_METHOD(removeAllTextures);
    NCB_METHOD(removeAllBg);
    NCB_METHOD(removeAllCaption);
    NCB_METHOD(registerBg);
    NCB_METHOD(registerCaption);
    NCB_METHOD(unloadUnusedTextures);
    NCB_METHOD(alphaOpAdd);
    NCB_METHOD(captureCanvas);
    NCB_METHOD(findSource);
    NCB_METHOD(loadSource);
    NCB_METHOD(clearCache);
    NCB_METHOD(setSize);
    NCB_METHOD(copyRect);
    NCB_METHOD(adjustGamma);
    NCB_METHOD(draw);
    NCB_METHOD(frameProgress);

    // Viewport/display
    NCB_METHOD(setFlip);
    NCB_METHOD(setOpacity);
    NCB_METHOD(setVisible);
    NCB_METHOD(setSlant);
    NCB_METHOD(setZoom);
    NCB_METHOD(getLayerNames);
    NCB_METHOD(releaseSyncWait);
    NCB_METHOD(calcViewParam);
    NCB_METHOD(getLayerMotion);
    NCB_METHOD(getLayerGetter);
    NCB_METHOD(getLayerGetterList);
    NCB_METHOD(skipToSync);
    NCB_METHOD(setStereovisionCameraPosition);

    // Timeline/variable queries
    NCB_METHOD(getTimelinePlaying);
    NCB_METHOD(getVariableRange);
    NCB_METHOD(getVariableFrameList);
    NCB_METHOD(getMainTimelineLabelList);
    NCB_METHOD(getDiffTimelineLabelList);
    NCB_METHOD(getLoopTimeline);
    NCB_METHOD(getPlayingTimelineInfoList);

    // Selector
    NCB_METHOD(isSelectorTarget);
    NCB_METHOD(deactivateSelectorTarget);

    // Misc
    NCB_METHOD(getCommandList);
    NCB_METHOD(getD3DAvailable);
    NCB_METHOD(doAlphaMaskOperation);
    NCB_METHOD(onFindMotion);
    NCB_METHOD(motionList);
    NCB_METHOD(emoteEdit);
}

NCB_REGISTER_SUBCLASS_DELAY(EmotePlayer) {
    NCB_CONSTRUCTOR((ResourceManager));

    // Properties
    NCB_PROPERTY(useD3D, getUseD3D, setUseD3D);
    NCB_PROPERTY(smoothing, getSmoothing, setSmoothing);
    NCB_PROPERTY(meshDivisionRatio, getMeshDivisionRatio, setMeshDivisionRatio);
    NCB_PROPERTY(queing, getQueuing, setQueuing); // original typo
    NCB_PROPERTY(hairScale, getHairScale, setHairScale);
    NCB_PROPERTY(partsScale, getPartsScale, setPartsScale);
    NCB_PROPERTY(bustScale, getBustScale, setBustScale);
    NCB_PROPERTY_RO(animating, getAnimating);
    NCB_PROPERTY(progress, getProgress, setProgress);
    NCB_PROPERTY(modified, getModified, setModified);
    NCB_PROPERTY(drawvisible, getDrawVisible, setDrawVisible);
    NCB_PROPERTY(drawOpacity, getDrawOpacity, setDrawOpacity);
    NCB_PROPERTY(opengl, getOpengl, setOpengl);
    NCB_PROPERTY(module, getModule, setModule);

    // Methods
    NCB_METHOD(clone);
    NCB_METHOD(show);
    NCB_METHOD(hide);
    NCB_METHOD(assignState);
    NCB_METHOD(initPhysics);
    NCB_METHOD(setRot);
    NCB_METHOD(getRot);
    NCB_METHOD(setCoord);
    NCB_METHOD(setScale);
    NCB_METHOD(getScale);
    NCB_METHOD(setColor);
    NCB_METHOD(getColor);
    NCB_METHOD(countVariables);
    NCB_METHOD(getVariableLabelAt);
    NCB_METHOD(countVariableFrameAt);
    NCB_METHOD(getVariableFrameLabelAt);
    NCB_METHOD(getVariableFrameValueAt);
    NCB_METHOD(setVariable);
    NCB_METHOD(getVariable);
    NCB_METHOD(startWind);
    NCB_METHOD(stopWind);
    NCB_METHOD(countMainTimelines);
    NCB_METHOD(getMainTimelineLabelAt);
    NCB_METHOD(countDiffTimelines);
    NCB_METHOD(getDiffTimelineLabelAt);
    NCB_METHOD(countPlayingTimelines);
    NCB_METHOD(getPlayingTimelineLabelAt);
    NCB_METHOD(getPlayingTimelineFlagsAt);
    NCB_METHOD(isLoopTimeline);
    NCB_METHOD(getTimelineTotalFrameCount);
    NCB_METHOD(playTimeline);
    NCB_METHOD(isTimelinePlaying);
    NCB_METHOD(stopTimeline);
    NCB_METHOD(setTimelineBlendRatio);
    NCB_METHOD(getTimelineBlendRatio);
    NCB_METHOD(fadeInTimeline);
    NCB_METHOD(fadeOutTimeline);
    NCB_METHOD(skip);
    NCB_METHOD(pass);
    NCB_METHOD(setOuterForce);
    NCB_METHOD(getOuterForce);
    NCB_METHOD(contains);
}

// ============================================================
// ResourceManager (existing, unchanged)
// ============================================================

NCB_REGISTER_SUBCLASS(ResourceManager) {
    NCB_CONSTRUCTOR((iTJSDispatch2 *, tjs_int));
    NCB_METHOD(load);
    NCB_METHOD_RAW_CALLBACK(setEmotePSBDecryptSeed,
                            &ResourceManager::setEmotePSBDecryptSeed,
                            TJS_STATICMEMBER);
    NCB_METHOD_RAW_CALLBACK(setEmotePSBDecryptFunc,
                            &ResourceManager::setEmotePSBDecryptFunc,
                            TJS_STATICMEMBER);
}

// ============================================================
// Motion top-level class with constants and subclasses
// ============================================================

class Motion {
public:
    static tjs_error setEnableD3D(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                  iTJSDispatch2 *) {
        if(count == 1 && (*p)->Type() == tvtInteger) {
            _enableD3D = static_cast<bool>(**p);
            return TJS_S_OK;
        }
        return TJS_E_INVALIDPARAM;
    }

    static tjs_error getEnableD3D(tTJSVariant *r, tjs_int, tTJSVariant **,
                                  iTJSDispatch2 *) {
        *r = tTJSVariant{ _enableD3D };
        return TJS_S_OK;
    }

private:
    inline static bool _enableD3D;
};

NCB_REGISTER_CLASS(Motion) {
    // Subclasses (Player registered as top-level class, aliased in PostRegistCallback)
    NCB_SUBCLASS(ResourceManager, ResourceManager);
    NCB_SUBCLASS(EmotePlayer, EmotePlayer);
    NCB_SUBCLASS(SeparateLayerAdaptor, SeparateLayerAdaptor);
    NCB_SUBCLASS(SourceCache, SourceCache);
    NCB_SUBCLASS(ObjSource, ObjSource);

    NCB_PROPERTY_RAW_CALLBACK(enableD3D, Motion::getEnableD3D,
                              Motion::setEnableD3D, TJS_STATICMEMBER);

    // Layer types
    Variant(TJS_W("LayerTypeObj"), (tjs_int)LayerTypeObj);
    Variant(TJS_W("LayerTypeShape"), (tjs_int)LayerTypeShape);
    Variant(TJS_W("LayerTypeLayout"), (tjs_int)LayerTypeLayout);
    Variant(TJS_W("LayerTypeMotion"), (tjs_int)LayerTypeMotion);
    Variant(TJS_W("LayerTypeParticle"), (tjs_int)LayerTypeParticle);
    Variant(TJS_W("LayerTypeCamera"), (tjs_int)LayerTypeCamera);

    // Shape types
    Variant(TJS_W("ShapeTypePoint"), (tjs_int)ShapeTypePoint);
    Variant(TJS_W("ShapeTypeCircle"), (tjs_int)ShapeTypeCircle);
    Variant(TJS_W("ShapeTypeRect"), (tjs_int)ShapeTypeRect);
    Variant(TJS_W("ShapeTypeQuad"), (tjs_int)ShapeTypeQuad);

    // Play flags
    Variant(TJS_W("PlayFlagForce"), (tjs_int)PlayFlagForce);
    Variant(TJS_W("PlayFlagChain"), (tjs_int)PlayFlagChain);
    Variant(TJS_W("PlayFlagAsCan"), (tjs_int)PlayFlagAsCan);
    Variant(TJS_W("PlayFlagJoin"), (tjs_int)PlayFlagJoin);
    Variant(TJS_W("PlayFlagStealth"), (tjs_int)PlayFlagStealth);

    // Transform orders
    Variant(TJS_W("TransformOrderFlip"), (tjs_int)TransformOrderFlip);
    Variant(TJS_W("TransformOrderSlant"), (tjs_int)TransformOrderSlant);
    Variant(TJS_W("TransformOrderZoom"), (tjs_int)TransformOrderZoom);
    Variant(TJS_W("TransformOrderAngle"), (tjs_int)TransformOrderAngle);

    // Coordinate types
    Variant(TJS_W("CoordinateRecutangularXY"),
            (tjs_int)CoordinateRecutangularXY);
    Variant(TJS_W("CoordinateRecutangularXZ"),
            (tjs_int)CoordinateRecutangularXZ);
}

// ============================================================
// Top-level emoteplayer.dll classes (D3DEmoteModule, D3DEmotePlayer)
// ============================================================

NCB_REGISTER_CLASS(D3DEmoteModule) {
    NCB_CONSTRUCTOR(());

    // Constants
    Variant(TJS_W("MaskModeStencil"), (tjs_int)MaskModeStencil);
    Variant(TJS_W("MaskModeAlpha"), (tjs_int)MaskModeAlpha);
    Variant(TJS_W("TimelinePlayFlagParallel"),
            (tjs_int)TimelinePlayFlagParallel);
    Variant(TJS_W("TimelinePlayFlagDifference"),
            (tjs_int)TimelinePlayFlagDifference);

    // Properties
    NCB_PROPERTY(maskMode, getMaskMode, setMaskMode);
    NCB_PROPERTY(maskRegionClipping, getMaskRegionClipping,
                 setMaskRegionClipping);
    NCB_PROPERTY(mipMapEnabled, getMipMapEnabled, setMipMapEnabled);
    NCB_PROPERTY(alphaOp, getAlphaOp, setAlphaOp);
    NCB_PROPERTY(protectTranslucentTextureColor,
                 getProtectTranslucentTextureColor,
                 setProtectTranslucentTextureColor);
    NCB_PROPERTY(pixelateDivision, getPixelateDivision, setPixelateDivision);

    // Methods
    NCB_METHOD(setMaxTextureSize);
}

NCB_REGISTER_CLASS(D3DEmotePlayer) {
    NCB_CONSTRUCTOR((ResourceManager));

    // Properties (same as EmotePlayer subclass)
    NCB_PROPERTY(useD3D, getUseD3D, setUseD3D);
    NCB_PROPERTY(smoothing, getSmoothing, setSmoothing);
    NCB_PROPERTY(meshDivisionRatio, getMeshDivisionRatio, setMeshDivisionRatio);
    NCB_PROPERTY(queing, getQueuing, setQueuing);
    NCB_PROPERTY(hairScale, getHairScale, setHairScale);
    NCB_PROPERTY(partsScale, getPartsScale, setPartsScale);
    NCB_PROPERTY(bustScale, getBustScale, setBustScale);
    NCB_PROPERTY_RO(animating, getAnimating);
    NCB_PROPERTY(progress, getProgress, setProgress);
    NCB_PROPERTY(modified, getModified, setModified);
    NCB_PROPERTY(drawvisible, getDrawVisible, setDrawVisible);
    NCB_PROPERTY(drawOpacity, getDrawOpacity, setDrawOpacity);
    NCB_PROPERTY(opengl, getOpengl, setOpengl);
    NCB_PROPERTY(module, getModule, setModule);

    // Methods
    NCB_METHOD(clone);
    NCB_METHOD(show);
    NCB_METHOD(hide);
    NCB_METHOD(assignState);
    NCB_METHOD(initPhysics);
    NCB_METHOD(setRot);
    NCB_METHOD(getRot);
    NCB_METHOD(setCoord);
    NCB_METHOD(setScale);
    NCB_METHOD(getScale);
    NCB_METHOD(setColor);
    NCB_METHOD(getColor);
    NCB_METHOD(countVariables);
    NCB_METHOD(getVariableLabelAt);
    NCB_METHOD(countVariableFrameAt);
    NCB_METHOD(getVariableFrameLabelAt);
    NCB_METHOD(getVariableFrameValueAt);
    NCB_METHOD(setVariable);
    NCB_METHOD(getVariable);
    NCB_METHOD(startWind);
    NCB_METHOD(stopWind);
    NCB_METHOD(countMainTimelines);
    NCB_METHOD(getMainTimelineLabelAt);
    NCB_METHOD(countDiffTimelines);
    NCB_METHOD(getDiffTimelineLabelAt);
    NCB_METHOD(countPlayingTimelines);
    NCB_METHOD(getPlayingTimelineLabelAt);
    NCB_METHOD(getPlayingTimelineFlagsAt);
    NCB_METHOD(isLoopTimeline);
    NCB_METHOD(getTimelineTotalFrameCount);
    NCB_METHOD(playTimeline);
    NCB_METHOD(isTimelinePlaying);
    NCB_METHOD(stopTimeline);
    NCB_METHOD(setTimelineBlendRatio);
    NCB_METHOD(getTimelineBlendRatio);
    NCB_METHOD(fadeInTimeline);
    NCB_METHOD(fadeOutTimeline);
    NCB_METHOD(skip);
    NCB_METHOD(pass);
    NCB_METHOD(setOuterForce);
    NCB_METHOD(getOuterForce);
    NCB_METHOD(contains);
}

// ============================================================
// Callbacks
// ============================================================

static void PostRegistCallback() {
    // Manually alias top-level Player class into Motion namespace
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if (!global) return;

    // Get Motion class object
    tTJSVariant motionVar;
    if (TJS_SUCCEEDED(global->PropGet(0, TJS_W("Motion"), nullptr, &motionVar, global))) {
        iTJSDispatch2 *motion = motionVar.AsObjectNoAddRef();
        if (motion) {
            // Get Player class dispatch
            tTJSVariant playerVar;
            if (TJS_SUCCEEDED(global->PropGet(0, TJS_W("Player"), nullptr, &playerVar, global))) {
                iTJSDispatch2 *playerDsp = playerVar.AsObjectNoAddRef();
                if (playerDsp) {
                    // Create variant with Player as both object and context
                    // so with(Motion.Player) { .useD3D = 0; } works
                    tTJSVariant aliasVar(playerDsp, playerDsp);
                    motion->PropSet(TJS_MEMBERENSURE, TJS_W("Player"),
                                    nullptr, &aliasVar, motion);
                }
            }
        }
    }
    global->Release();
}

static void PreRegistCallback() {}
static void PostUnregistCallback() {}

NCB_PRE_REGIST_CALLBACK(PreRegistCallback);
NCB_POST_REGIST_CALLBACK(PostRegistCallback);
NCB_POST_UNREGIST_CALLBACK(PostUnregistCallback);
