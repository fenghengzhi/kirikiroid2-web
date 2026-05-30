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
#include "D3DAdaptor.h"

using namespace motion;

#define NCB_MODULE_NAME TJS_W("motionplayer.dll")
#define LOGGER spdlog::get("plugin")

// ============================================================
// Subclass registrations (used as Motion.XXX)
// ============================================================

NCB_REGISTER_SUBCLASS_DELAY(SourceCache) {
    NCB_CONSTRUCTOR((tTJSVariant, tjs_int));
    NCB_METHOD(loadSource);
    NCB_METHOD(clearCache);
    NCB_PROPERTY_RO(bufLayer, getBufLayer);
}
NCB_REGISTER_SUBCLASS_DELAY(ObjSource) { NCB_CONSTRUCTOR(()); }

// Aligned to libkrkr2.so Motion.Point/Circle/Rect/Quad/LayerGetter (0x690FBC~0x69B350)
NCB_REGISTER_SUBCLASS_DELAY(Point) {
    NCB_CONSTRUCTOR(());
    NCB_PROPERTY_RO(type, getType);
    NCB_METHOD(contains);
    NCB_PROPERTY_RO(x, getX);
    NCB_PROPERTY_RO(y, getY);
}
NCB_REGISTER_SUBCLASS_DELAY(Circle) {
    NCB_CONSTRUCTOR(());
    NCB_PROPERTY_RO(type, getType);
    NCB_METHOD(contains);
    NCB_PROPERTY_RO(x, getX);
    NCB_PROPERTY_RO(y, getY);
    NCB_PROPERTY_RO(r, getR);
}
NCB_REGISTER_SUBCLASS_DELAY(Rect) {
    NCB_CONSTRUCTOR(());
    NCB_PROPERTY_RO(type, getType);
    NCB_METHOD(contains);
    NCB_PROPERTY_RO(l, getL);
    NCB_PROPERTY_RO(t, getT);
    NCB_PROPERTY_RO(w, getW);
    NCB_PROPERTY_RO(h, getH);
}
NCB_REGISTER_SUBCLASS_DELAY(Quad) {
    NCB_CONSTRUCTOR(());
    NCB_PROPERTY_RO(type, getType);
    NCB_METHOD(contains);
    NCB_PROPERTY_RO(p, getP);
}
NCB_REGISTER_SUBCLASS_DELAY(LayerGetter) {
    NCB_CONSTRUCTOR(());
    NCB_PROPERTY_RO(type, getType);
    NCB_PROPERTY_RO(label, getLabel);
    NCB_PROPERTY_RO(visible, getVisible);
    NCB_PROPERTY_RO(branchVisible, getBranchVisible);
    NCB_PROPERTY_RO(layerVisible, getLayerVisible);
    NCB_PROPERTY_RO(x, getX);
    NCB_PROPERTY_RO(y, getY);
    NCB_PROPERTY_RO(left, getLeft);
    NCB_PROPERTY_RO(top, getTop);
    NCB_PROPERTY_RO(flipX, getFlipX);
    NCB_PROPERTY_RO(flipY, getFlipY);
    NCB_PROPERTY_RO(zoomX, getZoomX);
    NCB_PROPERTY_RO(zoomY, getZoomY);
    NCB_PROPERTY_RO(angleDeg, getAngleDeg);
    NCB_PROPERTY_RO(angleRad, getAngleRad);
    NCB_PROPERTY_RO(slantX, getSlantX);
    NCB_PROPERTY_RO(slantY, getSlantY);
    NCB_PROPERTY_RO(originX, getOriginX);
    NCB_PROPERTY_RO(originY, getOriginY);
    NCB_PROPERTY_RO(opacity, getOpacity);
    NCB_PROPERTY_RO(mtx, getMtx);
    NCB_PROPERTY_RO(vtx, getVtx);
    NCB_PROPERTY_RO(color, getColor);
    NCB_PROPERTY_RO(bezierPatch, getBezierPatch);
    NCB_PROPERTY_RO(shape, getShape);
    NCB_PROPERTY_RO(motion, getMotion);
    NCB_PROPERTY_RO(particle, getParticle);
}
// Aligned to libkrkr2.so SeparateLayerAdaptor_ncb_registerMembers (0x6ABFAC)
NCB_REGISTER_SUBCLASS_DELAY(SeparateLayerAdaptor) {
    Factory(&SeparateLayerAdaptor::factory);
    NCB_PROPERTY(absolute, getAbsolute, setAbsolute);
    NCB_PROPERTY(targetLayer, getTargetLayer, setTargetLayer);
    NCB_METHOD(clear);
    RawCallback(TJS_W("assign"), &SeparateLayerAdaptor::assignCompat, 0);
    RawCallback(TJS_W("layerTreeOwnerInterface"),
                &SeparateLayerAdaptor::getLayerTreeOwnerInterfaceCompat,
                (int)0, TJS_HIDDENMEMBER);
}
NCB_REGISTER_SUBCLASS_DELAY(D3DAdaptor) {
    Factory(&D3DAdaptor::factory);
    NCB_METHOD(setPos);
    NCB_METHOD(setSize);
    NCB_METHOD(setClearColor);
    NCB_METHOD(setResizable);
    NCB_METHOD(removeAllTextures);
    NCB_METHOD(removeAllBg);
    NCB_METHOD(removeAllCaption);
    NCB_METHOD(registerBg);
    NCB_METHOD(registerCaption);
    NCB_METHOD(unloadUnusedTextures);
    RawCallback(TJS_W("captureCanvas"), &D3DAdaptor::captureCanvasStatic, 0);
    NCB_PROPERTY(visible, getVisible, setVisible);
    NCB_PROPERTY(alphaOpAdd, getAlphaOpAdd, setAlphaOpAdd);
    NCB_PROPERTY(canvasCaptureEnabled, getCanvasCaptureEnabled, setCanvasCaptureEnabled);
    NCB_PROPERTY(clearEnabled, getClearEnabled, setClearEnabled);
}

NCB_REGISTER_CLASS(Player) {
    NCB_CONSTRUCTOR((ResourceManager));

    // Properties
    // Root node position — aligned to libkrkr2.so NCB registration (0x6D69C8)
    NCB_PROPERTY(x, getX, setX);
    NCB_PROPERTY(y, getY, setY);
    NCB_PROPERTY(left, getLeft, setLeft);
    NCB_PROPERTY(top, getTop, setTop);

    NCB_PROPERTY(completionType, getCompletionType, setCompletionType);
    // M15 D-01 (cluster E §3.1): removed metadata Motion.Player NCB — port
    // invention, no binary equivalent on 92-entry table.
    NCB_PROPERTY(chara, getChara, setChara);
    // Aligned to libkrkr2.so 0x681CAC: raw callback to access objthis
    // for onFindMotion TJS callback during motion loading
    NCB_PROPERTY_RAW_CALLBACK(motion, Player::getMotionCompat,
                              Player::setMotionCompat, 0);
    NCB_PROPERTY(motionKey, getMotionKey, setMotionKey);
    NCB_PROPERTY(outline, getOutline, setOutline);
    NCB_PROPERTY(priorDraw, getPriorDraw, setPriorDraw);
    NCB_PROPERTY(frameLastTime, getFrameLastTime, setFrameLastTime);
    NCB_PROPERTY(frameLoopTime, getLoopTime, setLoopTime); // R1.H2: binary 0x6D97AC frameLoopTime getter reads +1136 (_loopTime), not +1120
    NCB_PROPERTY(loopTime, getLoopTime, setLoopTime);
    NCB_PROPERTY(processedMeshVerticesNum, getProcessedMeshVerticesNum,
                 setProcessedMeshVerticesNum);
    NCB_PROPERTY_RO(playing, getPlaying);
    // M15 D-01 (cluster E §3.1): removed queuing/directEdit/selectorEnabled
    // Motion.Player NCB — port-invented properties not in 92-entry binary
    // table. _queuing/_directEdit/_selectorEnabled fields preserved for
    // internal use.
    NCB_PROPERTY(variableKeys, getVariableKeys, setVariableKeys);
    NCB_PROPERTY_RO(allplaying, getAllplaying);
    NCB_PROPERTY(syncWaiting, getSyncWaiting, setSyncWaiting);
    NCB_PROPERTY(syncActive, getSyncActive, setSyncActive);
    NCB_PROPERTY(hasCamera, getHasCamera, setHasCamera);
    NCB_PROPERTY(cameraActive, getCameraActive, setCameraActive);
    NCB_PROPERTY(stereovisionActive, getStereovisionActive,
                 setStereovisionActive);
    NCB_PROPERTY(tickCount, getTickCount, setTickCount);
    NCB_PROPERTY(speed, getSpeed, setSpeed);
    NCB_PROPERTY(frameTickCount, getFrameTickCount, setFrameTickCount);
    // M15 missing #19: pixelateDivision is binary Player+912 instance field
    // (default 100), NOT D3DEmoteModule static. Cluster E §1 ctor + §3.1.
    NCB_PROPERTY(pixelateDivision, getPixelateDivision, setPixelateDivision);
    NCB_PROPERTY(maskMode, getMaskMode, setMaskMode);
    // M-colorWeight P1 (cluster E §4): binary Player NCB `colorWeight` getter
    // sub_6D9768 returns +1097 bool (independentLayerInherit), NOT the
    // _colorWeightPacked uint32 (+1156). The TJS name suggests packed color
    // but the binary cb is the +1097 bool — apparent binary alias mis-naming.
    // 1:1 with libkrkr2.so per CLAUDE.md.
    NCB_PROPERTY(colorWeight, getIndependentLayerInherit,
                 setIndependentLayerInherit);
    // M-colorWeight: `independentLayerInherit` removed as port-extra (it was
    // an alias of the correct binary `colorWeight` binding).
    NCB_PROPERTY(zFactor, getZFactor, setZFactor);
    NCB_PROPERTY(cameraTarget, getCameraTarget, setCameraTarget);
    NCB_PROPERTY(cameraPosition, getCameraPosition, setCameraPosition);
    NCB_PROPERTY(cameraFOV, getCameraFOV, setCameraFOV);
    NCB_PROPERTY(cameraAlive, getCameraAlive, setCameraAlive);
    // M15 D-01 (cluster E §3.1): removed canvasCaptureEnabled/clearEnabled/
    // hitThreshold Motion.Player NCB — port-invented properties not in
    // 92-entry binary table. C++ fields preserved.
    NCB_PROPERTY(preview, getPreview, setPreview);
    NCB_PROPERTY(outsideFactor, getOutsideFactor, setOutsideFactor);
    NCB_PROPERTY(resourceManager, getResourceManager, setResourceManager);
    NCB_PROPERTY(stealthChara, getStealthChara, setStealthChara);
    NCB_PROPERTY(stealthMotion, getStealthMotion, setStealthMotion);
    NCB_PROPERTY(tags, getTags, setTags);
    NCB_PROPERTY(project, getProject, setProject);
    // M15 D-01 (cluster E §3.1): removed useD3D/meshline/busy Motion.Player
    // NCB — port-invented Web-port properties without binary equivalent in
    // 92-entry Motion.Player table (sub_6D69C8).

    // Core methods
    // M15 D-01 (cluster E §3.1): removed random/initPhysics/setRotate/setMirror
    // Motion.Player NCB — port-invented helpers (random is Motion namespace,
    // initPhysics is wrong class hoist, setRotate/setMirror are web-port). No
    // binary equivalent on Motion.Player.
    // (serialize/unserialize already removed above.)
    // hairScale/partsScale/bustScale removed: not Motion.Player members in
    // libkrkr2.so (sub_681F20/28/30 are EmotePlayer-only NCB accessors).
    NCB_METHOD_RAW_CALLBACK(setDrawAffineTranslateMatrix,
                            &Player::setDrawAffineTranslateMatrixCompat, 0);
    NCB_METHOD(getCameraOffset);
    NCB_METHOD(setCameraOffset);
    NCB_METHOD(modifyRoot);
    // M15 D-01 (cluster E §3.1): removed debugPrint — port debug helper, no
    // binary Motion.Player equivalent.

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
    // M15 D-01 (cluster E §3.1): removed 5 draw-device internal NCB methods
    // — not in binary Motion.Player 92-entry set (sub_6D69C8). These are
    // web-port draw-device adapters with no binary Motion.Player equivalent
    // and unlikely to be called from TJS scenarios; binary routes via
    // iTVPDrawDevice/Motion.ResourceManager:
    //   removeAllTextures, removeAllBg, removeAllCaption,
    //   registerBg, registerCaption.
    // C++ methods preserved (host adapter still calls them internally).
    NCB_METHOD(unloadUnusedTextures);
    NCB_METHOD(alphaOpAdd);
    NCB_METHOD_RAW_CALLBACK(captureCanvas, &Player::captureCanvasCompat, 0);
    NCB_METHOD(findSource);
    NCB_METHOD(loadSource);
    NCB_METHOD(clearCache);
    NCB_METHOD(setSize);
    NCB_METHOD(copyRect);
    NCB_METHOD(adjustGamma);
    NCB_METHOD_DETAIL(draw, Class, void, Class::draw, (tTJSVariant));
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

    // M15 D-01 (cluster E §3.1): removed 22 timeline/variable-query NCB
    // methods from Motion.Player — these are D3DEmotePlayer 54-entry table
    // members (cluster D §1 #22-#26, #29-#47), not Motion.Player 92-entry
    // table. Port hoisted them onto Motion.Player as convenience. 1:1
    // reproduction places them only on D3DEmotePlayer (where they remain
    // registered at main.cpp:553-590).
    //
    // Removed: setVariable, getVariable, countVariables, getVariableLabelAt,
    //   countVariableFrameAt, getVariableFrameLabelAt, getVariableFrameValueAt,
    //   getTimelinePlaying, getVariableRange, getVariableFrameList,
    //   countMainTimelines, getMainTimelineLabelAt, getMainTimelineLabelList,
    //   countDiffTimelines, getDiffTimelineLabelAt, getDiffTimelineLabelList,
    //   getLoopTimeline, countPlayingTimelines, getPlayingTimelineLabelAt,
    //   getPlayingTimelineFlagsAt, getTimelineTotalFrameCount,
    //   playTimeline, stopTimeline, setTimelineBlendRatio, getTimelineBlendRatio,
    //   fadeInTimeline, fadeOutTimeline, getPlayingTimelineInfoList
    //
    // C++ Player methods preserved (D3DEmotePlayer wrapper forwards to Player).

    // Selector — also D3DEmotePlayer-only per cluster E §3.1
    // Removed: isSelectorTarget, deactivateSelectorTarget

    // Misc
    // M15 D-01 (cluster E §3.1): removed 4 port-invented Motion.Player NCB
    // methods (getCommandList/motionList/emoteEdit/onFindMotion) — debug/
    // internal helpers without binary equivalent in the 92-entry Motion.Player
    // table (sub_6D69C8). C++ methods kept; only TJS exposure removed.
    NCB_METHOD(getD3DAvailable);
    NCB_METHOD(doAlphaMaskOperation);
    NCB_METHOD_RAW_CALLBACK(play, &Player::playCompat, 0);
    NCB_METHOD_RAW_CALLBACK(progress, &Player::progressCompatMethod, 0);
    NCB_METHOD_RAW_CALLBACK(isPlaying, &Player::isPlayingCompat, 0);
    NCB_METHOD_RAW_CALLBACK(stop, &Player::stopCompat, 0);
}

// Motion.EmotePlayer — minimal NCB class matching libkrkr2.so EmotePlayer_NCB_classInit
// @ 0x686148. Binary only registers `finalize` (no script-facing API). The constructor
// is bound for native instance allocation; D3DEmotePlayer is the class with the real API.
NCB_REGISTER_SUBCLASS_DELAY(EmotePlayer) {
    NCB_CONSTRUCTOR(());
}

// ============================================================
// ResourceManager (existing, unchanged)
// ============================================================

NCB_REGISTER_SUBCLASS(ResourceManager) {
    NCB_CONSTRUCTOR((iTJSDispatch2 *, tjs_int));
    NCB_METHOD(load);
    NCB_METHOD(loadSource);
    NCB_METHOD(unload);
    NCB_METHOD(clearCache);
    NCB_METHOD(findSource);
    NCB_METHOD(requireLayerId);
    NCB_METHOD(releaseLayerId);
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
};

NCB_REGISTER_CLASS(Motion) {
    // Subclasses (Player registered as top-level class, aliased in PostRegistCallback)
    NCB_SUBCLASS(ResourceManager, ResourceManager);
    NCB_SUBCLASS(EmotePlayer, EmotePlayer);
    NCB_SUBCLASS(SeparateLayerAdaptor, SeparateLayerAdaptor);
    NCB_SUBCLASS(D3DAdaptor, D3DAdaptor);
    NCB_SUBCLASS(SourceCache, SourceCache);
    NCB_SUBCLASS(ObjSource, ObjSource);
    // Aligned to libkrkr2.so Motion_namespace_ncb_register (0x6D9B08)
    NCB_SUBCLASS(Point, Point);
    NCB_SUBCLASS(Circle, Circle);
    NCB_SUBCLASS(Rect, Rect);
    NCB_SUBCLASS(Quad, Quad);
    NCB_SUBCLASS(LayerGetter, LayerGetter);

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
// Callbacks (must be under motionplayer.dll module)
// ============================================================

static void PostRegistCallback() {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if (!global) return;

    auto ensurePlayerClassUseD3DProbe = [](iTJSDispatch2 *playerClass) {
        if(!playerClass) {
            return;
        }
        tTJSVariant marker;
        try {
            TVPExecuteExpression(TJS_W("%[]"), &marker);
        } catch(...) {
            return;
        }
        if(marker.Type() != tvtObject) {
            return;
        }

        // Player_ncb_registerMembers @ 0x6D69C8 registers useD3D as a
        // property object on the Player class; game scripts probe that with
        // typeof Motion.Player.useD3D. This restores the class-level NCB shape
        // without adding a mutable static useD3D state.
        playerClass->PropSet(TJS_MEMBERENSURE | TJS_STATICMEMBER,
                             TJS_W("useD3D"), nullptr, &marker, playerClass);
    };

    // Alias Player class into Motion namespace
    tTJSVariant motionVar;
    if (TJS_SUCCEEDED(global->PropGet(0, TJS_W("Motion"), nullptr, &motionVar, global))) {
        iTJSDispatch2 *motion = motionVar.AsObjectNoAddRef();
        if (motion) {
            tTJSVariant playerVar;
            if (TJS_SUCCEEDED(global->PropGet(0, TJS_W("Player"), nullptr, &playerVar, global))) {
                if (playerVar.Type() == tvtObject &&
                    playerVar.AsObjectNoAddRef() != nullptr) {
                    ensurePlayerClassUseD3DProbe(playerVar.AsObjectNoAddRef());
                    motion->PropSet(TJS_MEMBERENSURE, TJS_W("Player"),
                                    nullptr, &playerVar, motion);
                }
            }

        }
    }

    // Define ShortCutInitialPadKeyMap and related members as empty dictionaries.
    // These are referenced by encrypted keybinder.tjs but may not be defined
    // if the gamepad initialization script hasn't run yet.
    {
        tTJSVariant r;
        try {
            TVPExecuteExpression(
                TJS_W("global.ShortCutInitialPadKeyMap === void "
                      "? (global.ShortCutInitialPadKeyMap = %[]) : void"),
                &r);
            TVPExecuteExpression(
                TJS_W("global.ShortCutInitialGamePadKeyMap === void "
                      "? (global.ShortCutInitialGamePadKeyMap = %[]) : void"),
                &r);
        } catch(...) {}
    }

    global->Release();
}

static void PreRegistCallback() {}
static void PostUnregistCallback() {}

NCB_PRE_REGIST_CALLBACK(PreRegistCallback);
NCB_POST_REGIST_CALLBACK(PostRegistCallback);
NCB_POST_UNREGIST_CALLBACK(PostUnregistCallback);

// ============================================================
// emoteplayer.dll module — separate from motionplayer.dll
// In libkrkr2.so, emoteplayer.dll is an independent module whose
// entry callback (sub_682528) loads motionplayer.dll as a dependency,
// then registers EmotePlayer into the Motion namespace.
// ============================================================
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("emoteplayer.dll")

static void EmotePlayerPreRegist() {
    // Load motionplayer.dll as dependency (matches libkrkr2.so sub_682528)
    ncbAutoRegister::LoadModule(TJS_W("motionplayer.dll"));
}
NCB_PRE_REGIST_CALLBACK(EmotePlayerPreRegist);

NCB_REGISTER_CLASS(D3DEmoteModule) {
    NCB_CONSTRUCTOR(());

    // Constants
    Variant(TJS_W("MaskModeStencil"), (tjs_int)MaskModeStencil);
    Variant(TJS_W("MaskModeAlpha"), (tjs_int)MaskModeAlpha);
    Variant(TJS_W("TimelinePlayFlagParallel"),
            (tjs_int)TimelinePlayFlagParallel);
    // M11 D-02: 1:1 with libkrkr2.so D3DEmoteModule sub_52E504 constant table.
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

    // Properties (binary 54-entry NCB table @ libkrkr2.so sub_52E504)
    NCB_PROPERTY_RO(module, getModule);
    // M11 D-01 R-M11.4b: removed 10 EmotePlayer-style props that don't belong
    // on D3DEmotePlayer per binary (completionType/chara/motion/motionKey/
    // maskMode/outline/priorDraw/frameLastTime/frameLoopTime/loopTime/
    // processedMeshVerticesNum). They are Player-class properties hoisted
    // locally to D3DEmotePlayer — binary sub_52E504 exposes them on Motion.Player
    // (sub_6D69C8), not on D3DEmotePlayer (which is the visible/smoothing/scale
    // wrapper). 1:1 reproduction strips the hoist.
    NCB_PROPERTY(visible, getVisible, setVisible);
    NCB_PROPERTY(smoothing, getSmoothing, setSmoothing);
    NCB_PROPERTY(meshDivisionRatio, getMeshDivisionRatio, setMeshDivisionRatio);
    // M11 D-04: binary `queing` member is bound to set/getBustScale cb (float),
    // not get/setQueuing(bool). 1:1 with libkrkr2.so sub_52E504.
    NCB_PROPERTY(queing, getBustScale, setBustScale);
    NCB_PROPERTY(hairScale, getHairScale, setHairScale);
    NCB_PROPERTY(partsScale, getPartsScale, setPartsScale);
    // M11 D-05: binary `bustScale` member is bound to set/getBodyScale cb,
    // not set/getBustScale. 1:1 with libkrkr2.so sub_52E504.
    NCB_PROPERTY(bustScale, getBodyScale, setBodyScale);
    NCB_PROPERTY(bodyScale, getBodyScale, setBodyScale);
    // M11 D-01: removed `useD3D` — not in binary's 54-entry D3DEmotePlayer
    // NCB table (sub_52E504). Web-port draw-mode flag, no binary equivalent.
    // (progress NCB binding moved to method section per cluster D §1 #50.)
    // M11 D-08: binary `modified` is RO prop bound to getPlayCallback getter
    // (not a bool modified prop). 1:1 with libkrkr2.so sub_52E504.
    NCB_PROPERTY_RO(modified, getPlayCallback);
    // M11 D-01: removed `drawvisible/drawOpacity/opengl` — not in binary's
    // 54-entry D3DEmotePlayer NCB table. Web-port rendering controls,
    // no binary equivalent.
    NCB_PROPERTY_RO(animating, getAnimating);
    NCB_PROPERTY_RO(playCallback, getPlayCallback);

    // Methods
    // M11 D-03: binary registers member NAME "clear" bound to the create
    // callback (which tears down the EmoteObject chain — naming is binary's
    // apparent bug; CLAUDE.md mandates 1:1 reproduction).
    NCB_METHOD_DETAIL(clear, Class, void, Class::create, ());
    NCB_METHOD(load);
    NCB_METHOD(clone);
    NCB_METHOD(show);
    NCB_METHOD(hide);
    NCB_METHOD(assignState);
    // M11 D-01 (cluster D §2a): removed `initPhysics` — port-invented, not
    // in binary's 54-entry D3DEmotePlayer NCB table.
    NCB_METHOD_RAW_CALLBACK(setRot, &D3DEmotePlayer::setRotCompat, 0);
    NCB_METHOD(getRot);
    NCB_METHOD_RAW_CALLBACK(setCoord, &D3DEmotePlayer::setCoordCompat, 0);
    NCB_METHOD_RAW_CALLBACK(setScale, &D3DEmotePlayer::setScaleCompat, 0);
    NCB_METHOD(getScale);
    // M11 D-01: removed `setMirror` — not in binary's 54-entry D3DEmotePlayer
    // NCB table. Web-port method without binary equivalent.
    NCB_METHOD_RAW_CALLBACK(setColor, &D3DEmotePlayer::setColorCompat, 0);
    NCB_METHOD(getColor);
    NCB_METHOD(countVariables);
    NCB_METHOD(getVariableLabelAt);
    NCB_METHOD(countVariableFrameAt);
    NCB_METHOD(getVariableFrameLabelAt);
    NCB_METHOD(getVariableFrameValueAt);
    NCB_METHOD_RAW_CALLBACK(setVariable, &D3DEmotePlayer::setVariableCompat, 0);
    NCB_METHOD(getVariable);
    NCB_METHOD_RAW_CALLBACK(startWind, &D3DEmotePlayer::startWindCompat, 0);
    NCB_METHOD_RAW_CALLBACK(stopWind, &D3DEmotePlayer::stopWindCompat, 0);
    NCB_METHOD(countMainTimelines);
    NCB_METHOD(getMainTimelineLabelAt);
    NCB_METHOD(countDiffTimelines);
    NCB_METHOD(getDiffTimelineLabelAt);
    NCB_METHOD(countPlayingTimelines);
    NCB_METHOD(getPlayingTimelineLabelAt);
    NCB_METHOD(getPlayingTimelineFlagsAt);
    NCB_METHOD(isLoopTimeline);
    NCB_METHOD(getTimelineTotalFrameCount);
    // M11 D-01 (cluster D §2a): `play` is port-invented (not in binary's
    // 54-entry D3DEmotePlayer NCB table; binary playback starts via load(#3)
    // + progress(#50)). KEPT FOR LOGO CI: removing `play` likely breaks logo
    // TJS which uses d3dEmotePlayer.play(motion). Re-evaluate after wider
    // logo TJS audit. Continue tracking as TODO M11 D-01 残留.
    NCB_METHOD(play);
    NCB_METHOD(playTimeline);
    NCB_METHOD(isTimelinePlaying);
    NCB_METHOD(stopTimeline);
    NCB_METHOD(setTimeline);
    // M11 D-06: binary `setTimelineBlendRatio` member is bound to setTimeline
    // cb (signature (ttstr label, bool loop)), not a real blend-ratio method.
    NCB_METHOD_DETAIL(setTimelineBlendRatio, Class, void, Class::setTimeline,
                      (ttstr, bool));
    NCB_METHOD(getTimelineBlendRatio);
    NCB_METHOD(fadeInTimeline);
    NCB_METHOD(fadeOutTimeline);
    NCB_METHOD(skip);
    // M11 D-01 (cluster D §1 #50): binary registers `progress` as a method,
    // not a property. 港口之前在 property 区有 NCB_PROPERTY(progress, ...).
    // Moved to method section here. C++ D3DEmotePlayer::progress(double dt)
    // 签名匹配 binary D3DEmotePlayer_progress cb.
    NCB_METHOD(progress);
    NCB_METHOD(addPlayCallback);
    // M11 D-07: binary `pass` member is bound to addPlayCallback cb (void()),
    // not the real pass(double dt) method.
    NCB_METHOD_DETAIL(pass, Class, void, Class::addPlayCallback, ());
    // M11 D-01 (cluster D §2a): removed duplicate progress + draw NCB methods.
    // - progress already registered above (line ~597) per cluster D §1 #50;
    //   the second NCB_METHOD(progress) was a leftover from the property
    //   removal commit and would have caused dup-name NCB registration.
    // - draw is in cluster D §2a EXTRA list (not in binary 54-entry table);
    //   binary routes drawing via D3DEmotePlayer_setDrawAffineTranslateMatrix
    //   path which is also removed.
    // M11 D-01: removed `setDrawAffineTranslateMatrix` — not in binary's
    // 54-entry D3DEmotePlayer NCB table. Web-port draw-device helper.
    NCB_METHOD_RAW_CALLBACK(setOuterForce, &D3DEmotePlayer::setOuterForceCompat, 0);
    NCB_METHOD(getOuterForce);
    NCB_METHOD_RAW_CALLBACK(contains, &D3DEmotePlayer::containsCompat, 0);
}
