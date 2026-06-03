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
#include "PlayerRenderInternal.h"

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
NCB_REGISTER_SUBCLASS_DELAY(ObjSource) {
    NCB_CONSTRUCTOR(());
    // M9 brick C: binary ObjSource dict-facade members (ncb_registerMembers
    // @0x69CCB8): 4 numeric prop-ro + clip prop-ro + drawLayer method.
    NCB_PROPERTY_RO(originX, getOriginX);
    NCB_PROPERTY_RO(originY, getOriginY);
    NCB_PROPERTY_RO(width, getWidth);
    NCB_PROPERTY_RO(height, getHeight);
    NCB_PROPERTY_RO(clip, getClip);
    NCB_METHOD(drawLayer);
}

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
    // M15 missing #10 (cluster E §4): binary `setCoord` @0x6CCFF8 — combined
    // root pos writer matching binary atomic write.
    NCB_METHOD(setCoord);
    // M15 missing `contains` (cluster E §3.1): label-based hit test, delegates
    // to hitTestLayer (port's existing sub_6B5AD8-aligned path).
    NCB_METHOD(contains);
    // onAction/onSync/onGroundCorrection are binary Motion.Player *methods*
    // (Function-kind descriptors @0x6D69C8), NOT properties. Verified via the
    // descriptor build sites:
    //   onAction           @0x6d8ed0  cb=nullsub_87 @0x6D9A50  (empty no-op)
    //   onSync             @0x6d8edc  cb=nullsub_88 @0x6D9A54  (empty no-op;
    //                                  registered via sub_6D993C, X2=nullsub_88)
    //   onGroundCorrection @0x6d8f6c  cb=Player_onAction_ncb @0x6D9A58
    //                                  -> sub_A0F5E0 (tTJSVariant copy/AddRef,
    //                                     no Player state change) => no-op method
    NCB_METHOD(onAction);
    NCB_METHOD(onSync);
    NCB_METHOD(onGroundCorrection);
    // M15 missing transformOrder/coordinate (cluster E §3.1): int property
    // scaffolding; semantics pending spike.
    NCB_PROPERTY(transformOrder, getTransformOrder, setTransformOrder);
    NCB_PROPERTY(coordinate, getCoordinate, setCoordinate);

    NCB_PROPERTY(completionType, getCompletionType, setCompletionType);
    // M15 D-01 (cluster E §3.1): removed metadata Motion.Player NCB — port
    // invention, no binary equivalent on 92-entry table.
    NCB_PROPERTY(chara, getChara, setChara);
    // stealthChara: binary RW property @0x6D69C8 (name xref @0x6d6d64);
    // binary reuses Player_getChara/Player_setChara as the accessor pair.
    NCB_PROPERTY(stealthChara, getStealthChara, setStealthChara);
    // Aligned to libkrkr2.so 0x681CAC: raw callback to access objthis
    // for onFindMotion TJS callback during motion loading
    NCB_PROPERTY_RAW_CALLBACK(motion, Player::getMotionCompat,
                              Player::setMotionCompat, 0);
    // stealthMotion: binary RW property; get=Player_getStealthMotion,
    // set=Player_setMotion_stealth (name xref @0x6D69C8).
    NCB_PROPERTY(stealthMotion, getStealthMotion, setStealthMotion);
    // tags: binary RO property; getter=Player_getStealthMotionStr @0x6D9768-
    // adjacent (name xref @0x6d... aTags), setter slot is null (RO). Local
    // getTags() returns _tags; do NOT expose a setter.
    NCB_PROPERTY_RO(tags, getTags);
    NCB_PROPERTY(motionKey, getMotionKey, setMotionKey);
    // project: binary RW property (name xref @0x6D69C8 aProject); shares the
    // generic accessor family with motionKey.
    NCB_PROPERTY(project, getProject, setProject);
    NCB_PROPERTY(outline, getOutline, setOutline);
    NCB_PROPERTY(priorDraw, getPriorDraw, setPriorDraw);
    // frameLastTime/frameLoopTime/loopTime: binary RO properties @0x6D69C8
    // (descriptor setter slot = XZR; verified `STP XZR,XZR,[X20,#0x40]`).
    NCB_PROPERTY_RO(frameLastTime, getFrameLastTime);
    // frameLoopTime getter == Player_getFrameLoopTime @0x6D97AC: scalar
    // `return *(double*)(this+1136)` == local _loopTime. (Disasm: NCB reg
    // @0x6d7d10 "frameLoopTime" -> Player_getFrameLoopTime.)
    NCB_PROPERTY_RO(frameLoopTime, getLoopTime);
    // loopTime getter == Player_getLoopTime_array @0x6D139C: builds a TJS Array
    // of the var-track deque (Player+1296) cascadeKey strings, NOT a scalar.
    // (Disasm: NCB reg @0x6d6c80 "loopTime" -> 0x6D139C, new(0x50)=property RO.)
    // Corrected 2026-06-03 (R0-3): was wrongly bound to the scalar getLoopTime
    // with a stale "getLastTime" comment.
    NCB_PROPERTY_RO(loopTime, getLoopTimeArrayLike_0x6D139C);
    // processedMeshVerticesNum: binary RO property; getter sub_6D1018, setter
    // slot null (verified `STP XZR,XZR,[X20,#0x40]` @0x6d883c).
    NCB_PROPERTY_RO(processedMeshVerticesNum, getProcessedMeshVerticesNum);
    NCB_PROPERTY_RO(playing, getPlaying);
    // M15 D-01 (cluster E §3.1): removed queuing/directEdit/selectorEnabled
    // Motion.Player NCB — port-invented properties not in 92-entry binary
    // table. _queuing/_directEdit/_selectorEnabled fields preserved for
    // internal use.
    // variableKeys: binary RO property (descriptor setter slot null @0x6D69C8).
    NCB_PROPERTY_RO(variableKeys, getVariableKeys);
    NCB_PROPERTY_RO(allplaying, getAllplaying);
    // syncWaiting: binary RO property (descriptor setter slot null @0x6D69C8).
    NCB_PROPERTY_RO(syncWaiting, getSyncWaiting);
    NCB_PROPERTY(syncActive, getSyncActive, setSyncActive);
    // hasCamera: binary RO property (descriptor setter slot null @0x6D69C8).
    NCB_PROPERTY_RO(hasCamera, getHasCamera);
    NCB_PROPERTY(cameraActive, getCameraActive, setCameraActive);
    NCB_PROPERTY(stereovisionActive, getStereovisionActive,
                 setStereovisionActive);
    NCB_PROPERTY(tickCount, getTickCount, setTickCount);
    // M15 missing #3 (cluster E §3.1): binary `lastTime` RO property reads
    // +1136 _loopTime with frames→ms conversion (sub_6D9448).
    NCB_PROPERTY_RO(lastTime, getLastTime);
    // M15 missing `bounds` (cluster E §3.1): RO dict {left,top,right,bottom}
    // from Player +152/+160/+168/+176.
    NCB_PROPERTY_RO(bounds, getBounds);
    // M15 missing `meshDivisionRatio` (cluster E §3.1): delegate to
    // EmoteEngine +1168/+1176 via _engineBack.
    NCB_PROPERTY(meshDivisionRatio, getMeshDivisionRatio, setMeshDivisionRatio);
    // angleDeg/angleRad — binary stores the angle in DEGREES internally
    // (root+1616, or +464 when override flag +482).
    //
    // CORRECTION (2026-06-03, fresh decompile of Player_ncb_registerMembers
    // @0x6D69C8, byte-verified at the registration sites 0x6d7db4 / 0x6d7e30):
    // IDA's auto symbol names are SWAPPED vs the actual TJS member names — this
    // misled the prior analysis. The real bindings are:
    //   angleDeg member: getter = sub_6C1780  (returns the raw value, NO scale
    //                    -> DEGREES); setter = Player_setAngleRad@0x6c0f84
    //   angleRad member: getter = 0x6CD0C0    (value * 0.0174532925 -> RADIANS;
    //                    IDA misnames this fn "Player_getAngleDeg");
    //                    setter = Player_setAngleDeg@0x6cd0ec
    // So both member names match their semantics: angleDeg->deg, angleRad->rad.
    // There is NO name/semantics mismatch in the binary.
    //
    // FIXED 2026-06-03: the port binding was itself swapped (getAngleDeg returned
    // rad, getAngleRad returned raw deg). The getter/setter bodies (Player.h /
    // PlayerCore.cpp) were corrected so angleDeg->deg, angleRad->rad now match
    // the binary. (Residual platform gap: directEdit path omits initEmoteMotion(2).)
    NCB_PROPERTY(angleDeg, getAngleDeg, setAngleDeg);
    NCB_PROPERTY(angleRad, getAngleRad, setAngleRad);
    NCB_PROPERTY(speed, getSpeed, setSpeed);
    NCB_PROPERTY(frameTickCount, getFrameTickCount, setFrameTickCount);
    // M15 missing #19: pixelateDivision is binary Player+912 instance field
    // (default 100), NOT D3DEmoteModule static. Cluster E §1 ctor + §3.1.
    NCB_PROPERTY(pixelateDivision, getPixelateDivision, setPixelateDivision);
    // M15 missing transform properties (cluster E §3.1): binary Motion.Player
    // exposes flipX/flipY/slantX/slantY/zoomX/zoomY as properties backed by
    // root-node delta. Port also has single-axis setFlip/setSlant/setZoom
    // methods (port-extras kept for compat).
    NCB_PROPERTY(flipX, getFlipX, setFlipX);
    NCB_PROPERTY(flipY, getFlipY, setFlipY);
    NCB_PROPERTY(slantX, getSlantX, setSlantX);
    NCB_PROPERTY(slantY, getSlantY, setSlantY);
    NCB_PROPERTY(zoomX, getZoomX, setZoomX);
    NCB_PROPERTY(zoomY, getZoomY, setZoomY);
    NCB_PROPERTY(visible, getVisible, setVisible);
    NCB_PROPERTY(opacity, getOpacity, setOpacity);
    NCB_PROPERTY(maskMode, getMaskMode, setMaskMode);
    // colorWeight: binary RW property @0x6D69C8 (name xref @0x6d7740).
    //   get = sub_6CD710: reads Player+1156 (_colorWeightPacked uint32) with
    //         R/B byte swap (b0<->b2).
    //   set = sub_6CD724: writes Player+1156 with the same R/B swap.
    // The prior claim (cb=+1097 bool / independentLayerInherit) was a
    // mis-attribution from commit f675202 and is disproven by the descriptor
    // build site. colorWeight maps to getColorWeight/setColorWeight which
    // already implement the +1156 R/B-swap accessor pair.
    NCB_PROPERTY(colorWeight, getColorWeight, setColorWeight);
    // independentLayerInherit: distinct binary RW property @0x6D69C8 (name
    // xref @0x6d77b8).
    //   get = Player_getColorWeightFlag (sub_6D9768): reads Player+1097 bool.
    //   set = sub_6CC9D4: writes +1097, marks each node+1584 dirty if changed.
    // Maps to getIndependentLayerInherit/setIndependentLayerInherit.
    NCB_PROPERTY(independentLayerInherit, getIndependentLayerInherit,
                 setIndependentLayerInherit);
    NCB_PROPERTY(zFactor, getZFactor, setZFactor);
    // cameraTarget/cameraPosition/cameraFOV/cameraAlive: binary RO properties
    // @0x6D69C8 (descriptor setter slots null; verified `STP XZR,XZR,[+0x40]`).
    NCB_PROPERTY_RO(cameraTarget, getCameraTarget);
    NCB_PROPERTY_RO(cameraPosition, getCameraPosition);
    NCB_PROPERTY_RO(cameraFOV, getCameraFOV);
    NCB_PROPERTY_RO(cameraAlive, getCameraAlive);
    // M15 D-01 (cluster E §3.1): removed canvasCaptureEnabled/clearEnabled/
    // hitThreshold Motion.Player NCB — port-invented properties not in
    // 92-entry binary table. C++ fields preserved.
    NCB_PROPERTY(preview, getPreview, setPreview);
    NCB_PROPERTY(outsideFactor, getOutsideFactor, setOutsideFactor);
    // resourceManager: binary RO property @0x6D69C8 (descriptor setter slot
    // null; verified `STP XZR,XZR,[X20,#0x40]`).
    NCB_PROPERTY_RO(resourceManager, getResourceManager);
    // stealthChara/stealthMotion/tags/project recovered above (their binary
    // name-xrefs are present in 0x6D69C8 — the earlier "port invention"
    // claim was disproven by the descriptor build sites).
    //
    // useD3D / meshline: binary RW properties @0x6D69C8.
    //   useD3D   get=sub_695DE0, set=Player_setUseD3DFlag (name xref @0x6d... aUsed3d)
    //   meshline get=Player_getMeshline, set=Player_setMeshline (name xref aMeshline)
    NCB_PROPERTY(useD3D, getUseD3D, setUseD3D);
    NCB_PROPERTY(meshline, getMeshline, setMeshline);

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
    // M15 D-01 (cluster E §3.1): removed alphaOpAdd — port draw-device helper,
    // not in binary 92-entry table. C++ method preserved.
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
    // M5-2: raw callback so the optional args[0] substring filter of the binary
    // getLayerNames @0x6D10E0 (NCB name @0x6D88C8) is honored; the no-arg path
    // still emits every key.
    NCB_METHOD_RAW_CALLBACK(getLayerNames, &Player::getLayerNamesCompat, 0);
    NCB_METHOD(releaseSyncWait);
    NCB_METHOD(calcViewParam);
    NCB_METHOD(getLayerMotion);
    NCB_METHOD(getLayerGetter);
    NCB_METHOD(getLayerGetterList);
    NCB_METHOD(skipToSync);
    NCB_METHOD(setStereovisionCameraPosition);

    // setVariable / getVariable: binary Motion.Player methods @0x6D69C8.
    //   setVariable cb = loc_6D0E70 (name xref @0x6d... aSetvariable) — bound
    //               via raw callback to setVariableCompatMethod.
    //   getVariable cb = HM1_cascadeJoinAndLookup (name xref aGetvariable).
    NCB_METHOD_RAW_CALLBACK(setVariable, &Player::setVariableCompatMethod, 0);
    NCB_METHOD(getVariable);

    // The 24 timeline-query methods below are NOT in the binary Motion.Player
    // table (verified absent from the full 0x6D69C8 name-xref enumeration);
    // they are D3DEmotePlayer-only (registered at main.cpp D3DEmotePlayer
    // block). Do NOT recover them here:
    //   countVariables, getVariableLabelAt, countVariableFrameAt,
    //   getVariableFrameLabelAt, getVariableFrameValueAt, getTimelinePlaying,
    //   getVariableRange, getVariableFrameList, countMainTimelines,
    //   getMainTimelineLabelAt, getMainTimelineLabelList, countDiffTimelines,
    //   getDiffTimelineLabelAt, getDiffTimelineLabelList, getLoopTimeline,
    //   countPlayingTimelines, getPlayingTimelineLabelAt,
    //   getPlayingTimelineFlagsAt, getTimelineTotalFrameCount, playTimeline,
    //   stopTimeline, setTimelineBlendRatio, getTimelineBlendRatio,
    //   fadeInTimeline, fadeOutTimeline, getPlayingTimelineInfoList
    // C++ Player methods preserved (D3DEmotePlayer wrapper forwards to Player).

    // Selector — also D3DEmotePlayer-only per cluster E §3.1
    // Removed: isSelectorTarget, deactivateSelectorTarget

    // Misc
    // getCommandList / onFindMotion: binary Motion.Player methods @0x6D69C8
    // (name xrefs aGetcommandlist / aOnfind[Motion]). The earlier "port
    // invention" claim was disproven by the descriptor build sites.
    //   getCommandList cb = loc_6D3A4C
    //   onFindMotion   present in the 0x6D69C8 method set
    // (motionList / emoteEdit are genuinely absent from the binary table and
    // stay unbound.)
    NCB_METHOD(getCommandList);
    NCB_METHOD(onFindMotion);
    // getD3DAvailable / doAlphaMaskOperation are NOT Motion.Player methods.
    // libkrkr2.so motionplayer_ncb_register @0x6D9B08 (0x6da1f0/0x6da260)
    // registers them as namespace-level free functions on the Motion namespace
    // object. Relocated to NCB_ATTACH_FUNCTION(..., Motion, ...) below the
    // NCB_REGISTER_CLASS(Motion) block. (was: wrong owner on Player)
    NCB_METHOD_RAW_CALLBACK(play, &Player::playCompat, 0);
    NCB_METHOD_RAW_CALLBACK(progress, &Player::progressCompatMethod, 0);
    NCB_METHOD_RAW_CALLBACK(isPlaying, &Player::isPlayingCompat, 0);
    NCB_METHOD_RAW_CALLBACK(stop, &Player::stopCompat, 0);
}

// Motion.EmotePlayer — port currently registers only the ctor; the full
// script-facing API lives on D3DEmotePlayer below.
//
// CORRECTION (2026-06-03, fresh decompile of EmotePlayer_loadClass @0x685BC0):
// the earlier claim that "binary only registers `finalize`" is WRONG.
// 0x685BC0 calls EmotePlayer_NCB_classInit @0x686148 (registers `finalize`)
// and THEN EmotePlayer_ncb_registerMembers @0x67FAC8, which registers the
// full ~69-member API (progress/draw/play/setVariable/playTimeline/... +
// TimelinePlayFlag* constants + chara/motion/bounds/... properties) into the
// SAME class object. So Motion.EmotePlayer DOES expose the full API in the
// binary. Putting that API on D3DEmotePlayer and leaving Motion.EmotePlayer
// ctor-only is an architecture-P0 mismatch (audit cluster C). Open complication:
// the D3DEmotePlayer<->EmotePlayer binary relationship (sub_52E504) is not yet
// decompiled, so "API fully missing" is not yet a settled verdict — verify
// sub_52E504 before relocating the member table here.
NCB_REGISTER_SUBCLASS_DELAY(EmotePlayer) {
    NCB_CONSTRUCTOR(());
}

// ============================================================
// ResourceManager (existing, unchanged)
// ============================================================

NCB_REGISTER_SUBCLASS(ResourceManager) {
    NCB_CONSTRUCTOR((iTJSDispatch2 *, tjs_int));
    // M9 brick B: expose the binary ResourceManager's 12 members in the
    // ncb_registerMembers @0x6AB8BC registration order. (setEmotePSBDecrypt*
    // below are port extras, not part of the binary's 12.)
    NCB_METHOD(loadSource);
    NCB_METHOD(clearCache);
    NCB_PROPERTY_RO(bufLayer, getBufLayer);
    NCB_METHOD(load);
    NCB_METHOD(unload);
    NCB_METHOD(unloadAll);
    NCB_METHOD(isExistMotion);
    NCB_METHOD(findMotion);
    NCB_METHOD(findSource);
    NCB_METHOD_RAW_CALLBACK(random, &ResourceManager::random, 0);
    NCB_METHOD(requireLayerId);
    NCB_METHOD(releaseLayerId);
    // port extras (not in the binary RM 12-member table):
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
// Motion namespace-level free functions
// Aligned with libkrkr2.so motionplayer_ncb_register @0x6D9B08
// (0x6da154..0x6da260): after the 10 subclasses, two functions are
// registered directly on the Motion namespace dispatch object via
// sub_6FCAAC(*Motion, name, descriptor) — they are namespace-level free
// functions, NOT Motion.Player methods. Earlier the port mis-attached
// these to Player (NCB_METHOD on the Player subclass) — wrong owner.
// ============================================================

// Aligned with libkrkr2.so Motion_doAlphaMaskOperation @0x6AF104.
// The binary function (xref'd from both motionplayer_ncb_register @0x6d9b08
// AND the render path @0x6c4e28/0x6c7440/0x6c9ca8) is a single 11-arg free
// function: (dstLayer,dstX,dstY,srcLayer,srcX,srcY,w,h,threshold,maskMode,op).
// Its body (clip-intersect against dst PropGet("clip*"), per-pixel alpha
// composite switching on op/maskMode, border fillRect, then update()) is
// already faithfully ported in render_detail::applyMotionAlphaMaskLike_0x6AF104
// (PlayerRenderInternal.cpp:627), which the live render path uses. The NCB
// namespace entry is the very same address in the binary, so it delegates to
// the same compositor here rather than duplicating the pixel loops.
//   a9  = threshold        -> threshold
//   a10 = maskMode (0/1/2) -> playerStencilType (thresholdMaskMode = a10==0)
//   a11 = op (1/2/5/6)     -> itemFlags
static void motion_doAlphaMaskOperation(iTJSDispatch2 *dstLayer,
                                        int dstX,
                                        int dstY,
                                        iTJSDispatch2 *srcLayer,
                                        int srcX,
                                        int srcY,
                                        int width,
                                        int height,
                                        int threshold,
                                        int maskMode,
                                        int op) {
    motion::internal::render_detail::applyMotionAlphaMaskLike_0x6AF104(
        dstLayer, dstX, dstY, srcLayer, srcX, srcY, width, height, threshold,
        maskMode, op,
        // motionPath/frameTime/dstNode/srcNode are port-only diagnostics on the
        // compositor; the NCB script entry has no node/time context.
        std::string(), 0.0, -1, -1);
}

// Aligned with libkrkr2.so Motion_getD3DAvailable @0x6B0960:
//   return (hasGPUAccel_guess() & 1) == 0;   // D3D available iff NOT GLES-accel
// Namespace-level free function (registered on Motion @0x6da260), NOT a
// Motion.Player method. Platform boundary: the web port has no GLES-GPU-accel
// vs D3D split (single software-affine path), so there is no hasGPUAccel probe
// to invert; the port reports D3D as available. The wrong-owner relocation is
// the binary-aligned change; the constant value is a pre-existing platform
// boundary.
static bool motion_getD3DAvailable() { return true; }

// Motion namespace-level free functions doAlphaMaskOperation / getD3DAvailable:
// in-flow Motion-dispatch registration aligning libkrkr2.so motionplayer_ncb_register
// (sub_6D9B08 @0x6D9B08), which registers both on the Motion namespace dispatch
// *in-flow*, right after the last subclass (D3DAdaptor), via the same member-add
// primitive as the subclasses (sub_6FCAAC -> ncb_registerMember @0x6da1f0 / 0x6da260)
// — NOT as a separate deferred attach.
//
// Root-cause fix for the M6 motion-namespace regression: the port previously
// registered these via two standalone NCB_ATTACH_FUNCTION auto-register units.
// Those units run their Regist() at function-auto-register time (too early in the
// NCB registration sequence); their presence silently disabled the entire motion
// render pipeline (guest produced 0 render events, no trap/exception — see
// project_m6_motion_namespace_attach_regression). Relocating the *same* member-add
// (GetDispatch("Motion") -> PropSet TJS_MEMBERENSURE) into PostRegistCallback — the
// local hook that already registers Motion-dispatch members (the Player alias),
// running after the module's subclasses are registered, matching sub_6D9B08's
// "after subclasses" timing — both aligns and fixes. Verified locally (wasmtime
// guest, yuzulogo + m2logo): both functions present on Motion (PropGet rc=0,
// tvtObject) AND render pipeline restored (306 / 346 events, 51 execute_post PNGs).
// Residual minor mechanism difference vs binary: RegistFunction re-looks-up the
// Motion dispatch (GetDispatch) rather than reusing the in-hand *a1; end-state
// object graph is identical. The Player alias below uses the same re-lookup.
//
// Accessor to reach the protected static RegistFunction without constructing an
// auto-register unit (which would re-introduce the early-timed standalone unit).
struct MotionFreeFnRegistrar : ncbNativeFunctionAutoRegister {
    MotionFreeFnRegistrar() : ncbNativeFunctionAutoRegister(NCB_MODULE_NAME) {}
    using ncbNativeFunctionAutoRegister::RegistFunction;
};

// ============================================================
// Callbacks (must be under motionplayer.dll module)
// ============================================================

static void PostRegistCallback() {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if (!global) return;

    // In-flow Motion-dispatch registration of the two namespace free functions
    // (see sub_6D9B08 alignment / M6-fix note above). Same member-add path
    // (GetDispatch("Motion") -> PropSet TJS_MEMBERENSURE) the Player alias uses
    // below. Order matches binary: doAlphaMaskOperation then getD3DAvailable
    // (0x6da1f0 then 0x6da260).
    MotionFreeFnRegistrar::RegistFunction(
        TJS_W("doAlphaMaskOperation"), TJS_W("Motion"),
        &motion_doAlphaMaskOperation);
    MotionFreeFnRegistrar::RegistFunction(
        TJS_W("getD3DAvailable"), TJS_W("Motion"), &motion_getD3DAvailable);

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
    // R-pixelate phase 2: removed D3DEmoteModule.pixelateDivision NCB binding.
    // binary has pixelateDivision as Motion.Player instance property (+912),
    // not on D3DEmoteModule. Phase 1 commit 15e5ddc added Player NCB; phase 2
    // here removes D3DEmoteModule static + NCB exposure.

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
    // M11 D-01 (cluster D §2a): removed `play` — port invention, not in
    // binary's 54-entry D3DEmotePlayer NCB table. binary playback starts via
    // load(#3) + progress(#50). Motion.Player still exposes `play` via
    // playCompat at main.cpp:~275, so TJS using `player.play(...)` (Player
    // instance) continues to work. Only the D3DEmotePlayer wrapper alias
    // removed. CI will confirm if logo TJS uses d3dEmotePlayer.play instead.
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
