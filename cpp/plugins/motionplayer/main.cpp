//
// Created by LiDon on 2025/9/13.
// Reconstructed from the four current reference/binaries targets; the shared
// native image contains the emoteplayer.dll and motionplayer.dll NCB modules.
//
#include <spdlog/spdlog.h>
#include "tjs.h"
#include "ncbind.hpp"

#include "ResourceManager.h"
#include "EmotePlayer.h"
#include "Player.h"
#include "SeparateLayerAdaptor.h"
#include "SourceCache.h"
#include "D3DAdaptor.h"
#include "PlayerRenderInternal.h"
#include "MotionBezierPatch.h"
#include "MotionRenderBackend.h"
#include "RenderManager.h"
#include "MotionLayerExtensions.h"

using namespace motion;

// All four references emit these objects in one translation-unit global-init
// bundle, in this exact source order: the POD unit quad, the cubic-basis map,
// then the default 4x4 patch-point vector, followed by the NCB binding state
// below. __cxa_atexit therefore destroys the non-trivial objects in reverse
// registration order: later binding state, default points, then basis cache.
namespace motion::internal {
    std::array<float, 8> unitBezierPatchQuad_guess = {
        0.0f, 0.0f, 1.0f, 0.0f,
        1.0f, 1.0f, 0.0f, 1.0f,
    };
}

namespace motion::render_backend_guess {
    std::map<int, CubicBezierBasisTable_guess>
        cubicBezierBasisCache_guess;
}

namespace motion::internal {
    std::vector<detail::MeshPoint>
        defaultBezierPatchPoints_guess;
}

#define NCB_MODULE_NAME TJS_W("motionplayer.dll")
#define LOGGER spdlog::get("plugin")

// The references register one stateless native class named "BezierPatch"
// against Layer, then add these eight methods in this exact order.
NCB_ATTACH_CLASS(BezierPatch, Layer) {
    NCB_METHOD(affinePatch);
    NCB_METHOD(translatePatch);
    NCB_METHOD(affineTranslatePatch);
    NCB_METHOD(calcPatchBounds);
    NCB_METHOD(calcMeshBounds);
    NCB_METHOD(calcBezierPatch);
    NCB_METHOD(calcBezierPatchList);
    NCB_METHOD(reverseCalcBezierPatch);
}

NCB_GET_INSTANCE_HOOK(MotionLayerExtensions_guess) {
    NCB_INSTANCE_GETTER(objthis) {
        ClassT *object = GetNativeInstance(objthis);
        if(!object) {
            object = new ClassT(objthis);
            SetNativeInstance(objthis, object);
        }
        return object;
    }
};

NCB_ATTACH_CLASS_WITH_HOOK(MotionLayerExtensions_guess, Layer) {
    NCB_PROPERTY(debugMeshApp, getDebugMeshApp, setDebugMeshApp);
    NCB_PROPERTY(debugBezierApp, getDebugBezierApp, setDebugBezierApp);
    NCB_METHOD(meshCopy);
    NCB_METHOD(operateMesh);
    NCB_METHOD(drawMeshFrame);
    NCB_METHOD(bezierPatchCopy);
    NCB_METHOD(operateBezierPatch);
    NCB_METHOD(drawBezierPatchFrame);
    NCB_METHOD(drawBezierPatchMeshFrame);
}

// ============================================================
// Subclass registrations (used as Motion.XXX)
// ============================================================

NCB_REGISTER_SUBCLASS_DELAY(SourceCache) {
    // SourceCache has its own delayed ncbClassInfo<T>::InfoT tuple in all four
    // references. Setup publishes borrowed name/id/class-object state before
    // registering these descriptors and clears it without Release on unload.
    // The four current SourceCache registrars use the zero-argument typed
    // constructor.  The owner/cache-size constructor belongs only to the
    // ResourceManager base-construction path; it is not a script overload.
    NCB_CONSTRUCTOR(());
    NCB_METHOD(loadSource);
    NCB_METHOD(clearCache);
    NCB_PROPERTY_RO(bufLayer, getBufLayer);
}
NCB_REGISTER_SUBCLASS_DELAY(ObjSource) {
    // ObjSource owns an independent delayed ncbClassInfo<T>::InfoT tuple in
    // all four references. Setup publishes its non-owning name/id/class-object
    // state before member registration and clears it on unregister; this is
    // not shared with SourceCache or a physically adjacent subclass row.
    NCB_CONSTRUCTOR(());
    // The four ObjSource registrars expose the raw-node facade's four numeric
    // prop-ro members, clip prop-ro and drawLayer method in this order.
    NCB_PROPERTY_RO(originX, getOriginX);
    NCB_PROPERTY_RO(originY, getOriginY);
    NCB_PROPERTY_RO(width, getWidth);
    NCB_PROPERTY_RO(height, getHeight);
    NCB_PROPERTY_RO(clip, getClip);
    NCB_METHOD(drawLayer);
}

// Motion.Point/Circle/Rect/Quad share one full native geometry record and one
// contains implementation in all four current reference binaries.  Each
// delayed class also owns a distinct ncbClassInfo<T>::InfoT lookup tuple; the
// tuple is non-owning, is published name/id/class-object/initialized, and has
// no locking or rollback after publication.  These macros only instantiate
// that class state and its members.  The NCB_SUBCLASS rows in Motion's
// registrar below run Setup.  Their generated Unregist wrappers would clear
// ClassInfo before removing the corresponding member, but all four integrated
// loaders expose only the registration pipeline: there is no module erase or
// unload caller.  Successfully loaded Motion/subclass state is process-lived.
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
// LayerGetter owns a fifth, independent delayed-subclass ClassInfo tuple.  Its
// Setup transaction publishes borrowed name/class-object pointers plus the
// class ID without AddRef, locking or automatic rollback.  The generated
// zero-argument constructor attaches a one-pointer facade whose node starts
// null; only Player's native producers install a live borrowed MotionNode.
NCB_REGISTER_SUBCLASS_DELAY(LayerGetter) {
    NCB_CONSTRUCTOR(());
    NCB_PROPERTY_RO(type, getType);
    NCB_PROPERTY_RO(label, getLabel);
    NCB_PROPERTY_RO(src, getSrc);
    NCB_PROPERTY_RO(visible, getVisible);
    NCB_PROPERTY_RO(branchVisible, getBranchVisible);
    NCB_PROPERTY_RO(layerVisible, getLayerVisible);
    NCB_PROPERTY_RO(x, getX);
    NCB_PROPERTY_RO(y, getY);
    NCB_PROPERTY_RO(left, getLeft);
    NCB_PROPERTY_RO(top, getTop);
    NCB_PROPERTY_RO(coord, getCoord);
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
// SeparateLayerAdaptor owns an independent delayed-subclass ClassInfo tuple in
// all four references.  Setup publishes borrowed name/id/class-object state;
// unregister clears it without Release or resetting the one-time guard.
// Factory is ncbind's constructor descriptor registered under the dynamic
// class name, not a public member literally named "Factory".  It is the first
// of exactly five rows and marks the constructor-seen flag, so the retained
// dummy-constructor fallback is not published on the normal path.
//
// NativeClass creates an empty {native=null, sticky=false} instance adaptor
// before the generated factory bridge runs.  The bridge copies only arg0 (or
// Void), ignores surplus arguments, and attaches the new native only after it
// can recover that adaptor by this class's own ID.  Attach failure destroys and
// frees the new native.  The four references contain no existing-native/sticky
// SeparateLayerAdaptor producer; Player's persistent adaptor is a separate raw
// owner and Player's class-ID sites only consume script-supplied instances.
NCB_REGISTER_SUBCLASS_DELAY(SeparateLayerAdaptor) {
    Factory(&SeparateLayerAdaptor::factory);
    NCB_PROPERTY(absolute, getAbsolute, setAbsolute);
    NCB_PROPERTY(targetLayer, getTargetLayer, setTargetLayer);
    NCB_METHOD(clear);
    RawCallback(TJS_W("assign"), &SeparateLayerAdaptor::assignCompat, 0);
}
// D3DAdaptor has a second independent delayed-subclass ClassInfo tuple.  Its
// Factory descriptor is the dynamic class-name constructor row (the first of
// 16 rows), so it marks constructor-seen and suppresses ncbind's retained
// dummy fallback.  NativeClass first creates an empty non-sticky adaptor; the
// generated bridge's one-Void path leaves that shell empty, while an ordinary
// successful factory call writes only the native slot.  It does not set sticky
// or destroy an old native before an invalid repeated attach.
//
// All four ClassID xref sets contain only this factory/typed wrappers and the
// two Player receiver-unwrappers.  There is no plugin-side existing-native or
// sticky adaptor producer.  Player's process-global shared D3D renderer is a
// separate raw owner and is never published through this NCB class.
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
    NCB_PROPERTY(visible, getVisible, setVisible);
    NCB_PROPERTY(alphaOpAdd, getAlphaOpAdd, setAlphaOpAdd);
    NCB_METHOD(captureCanvas);
    NCB_PROPERTY(canvasCaptureEnabled, getCanvasCaptureEnabled, setCanvasCaptureEnabled);
    NCB_PROPERTY(clearEnabled, getClearEnabled, setClearEnabled);
}

// Player owns another independent ncbClassInfo<T>::InfoT tuple in all four
// current references (32 B on LP64, 16 B on ILP32). Setup publishes borrowed
// name/id/class-object fields before the member registrar; unregister clears
// them without Release, and exception cleanup closes only the already-visible
// registration prefix. Motion then publishes a separate vptr-only
// TJS_STATICMEMBER subclass item with no parent/cast/native-offset metadata.
//
// NativeClass first creates an empty {native=null, sticky=false} adaptor. The
// generated typed constructor treats one Void argument as that empty-shell
// sentinel; otherwise it copies only arg0, accepts surplus parameters, builds
// Player before receiver lookup, and writes only the native slot. A repeated
// constructor call on a populated receiver therefore overwrites and leaks the
// old native instead of running adaptor teardown.
//
// The complete plugin-side existing-native producer set is exactly the type-3
// node builder and type-4 particle spawner. Both call
// CreateAdaptor(child,false,false). Success creates a non-sticky adaptor that
// owns the child. CreateNew failure returns null and leaves a Void Variant; a
// rarer adaptor-lookup failure may instead return a non-null empty shell,
// because CreateAdaptor returns the new dispatch even when GetAdaptor fails.
// Neither failure deletes the supplied child, and there is no sticky Player
// producer in the four reference call graphs.
NCB_REGISTER_SUBCLASS(Player) {
    // Four-reference constructor boundary: one Variant source argument. The
    // generated native bridge requires an argument slot; one Void is ncbind's
    // empty-adaptor sentinel, otherwise only arg0 is copied and surplus args
    // are ignored.
    NCB_CONSTRUCTOR((tTJSVariant));

    // Four-reference invariant: exactly 92 members in this publication order.
    // Only setVariable/play/progress use native-instance raw callbacks. Clear
    // and draw use generated typed descriptors with explicit signatures, and
    // every other method/property uses the ordinary typed ncbind family.
    NCB_PROPERTY(defaultSyncActive, getDefaultSyncActive,
                 setDefaultSyncActive);                                     // #1
    NCB_PROPERTY(defaultTransformOrder, getDefaultTransformOrder,
                 setDefaultTransformOrder);                                  // #2
    NCB_PROPERTY_RO(resourceManager, getResourceManager);                    // #3
    NCB_PROPERTY_RO(lastTime, getLastTime);                                  // #4
    NCB_PROPERTY_RO(loopTime, getLoopTime);                                  // #5
    NCB_PROPERTY_RO(variableKeys, getVariableKeys);                          // #6
    NCB_PROPERTY(chara, getChara, setChara);                                 // #7
    NCB_PROPERTY(stealthChara, getStealthChara, setStealthChara);            // #8
    NCB_PROPERTY(motion, getMotion, setMotion);                              // #9
    NCB_PROPERTY(stealthMotion, getStealthMotion, setStealthMotion);         // #10
    NCB_PROPERTY_RO(tags, getTags);                                          // #11
    NCB_PROPERTY(motionKey, getMotionKey, setMotionKey);                     // #12
    NCB_PROPERTY(project, getProject, setProject);                           // #13
    NCB_PROPERTY(completionType, getCompletionType, setCompletionType);      // #14
    NCB_PROPERTY(preview, getPreview, setPreview);                           // #15
    NCB_PROPERTY(priorDraw, getPriorDraw, setPriorDraw);                     // #16
    NCB_PROPERTY(outsideFactor, getOutsideFactor, setOutsideFactor);         // #17
    NCB_PROPERTY(meshDivisionRatio, getMeshDivisionRatio,
                 setMeshDivisionRatio);                                      // #18
    NCB_PROPERTY(speed, getSpeed, setSpeed);                                 // #19
    NCB_PROPERTY(syncActive, getSyncActive, setSyncActive);                  // #20
    NCB_PROPERTY(tickCount, getTickCount, setTickCount);                     // #21
    NCB_PROPERTY(frameTickCount, getFrameTickCount, setFrameTickCount);      // #22
    NCB_PROPERTY(cameraActive, getCameraActive, setCameraActive);            // #23
    NCB_PROPERTY(stereovisionActive, getStereovisionActive,
                 setStereovisionActive);                                     // #24
    NCB_PROPERTY(outline, getOutline, setOutline);                           // #25
    NCB_PROPERTY(meshline, getMeshline, setMeshline);                        // #26
    NCB_PROPERTY(maskMode, getMaskMode, setMaskMode);                        // #27
    NCB_PROPERTY(colorWeight, getColorWeight, setColorWeight);               // #28
    NCB_PROPERTY(independentLayerInherit, getIndependentLayerInherit,
                 setIndependentLayerInherit);                                // #29
    NCB_PROPERTY(transformOrder, getTransformOrder, setTransformOrder);      // #30
    NCB_PROPERTY(coordinate, getCoordinate, setCoordinate);                  // #31
    NCB_PROPERTY(zFactor, getZFactor, setZFactor);                           // #32
    NCB_PROPERTY_RO(cameraTarget, getCameraTarget);                          // #33
    NCB_PROPERTY_RO(cameraPosition, getCameraPosition);                      // #34
    NCB_PROPERTY_RO(cameraFOV, getCameraFOV);                                // #35
    NCB_PROPERTY_RO(cameraAlive, getCameraAlive);                            // #36
    NCB_PROPERTY_RO(bounds, getBounds);                                      // #37
    NCB_PROPERTY_RO(playing, getPlaying);                                    // #38
    NCB_PROPERTY_RO(allplaying, getAllplaying);                              // #39
    NCB_PROPERTY_RO(syncWaiting, getSyncWaiting);                            // #40
    NCB_PROPERTY_RO(frameLastTime, getFrameLastTime);                        // #41
    NCB_PROPERTY_RO(frameLoopTime, getFrameLoopTime);                        // #42
    NCB_PROPERTY_RO(hasCamera, getHasCamera);                                // #43
    NCB_PROPERTY(angleDeg, getAngleDeg, setAngleDeg);                        // #44
    NCB_PROPERTY(angleRad, getAngleRad, setAngleRad);                        // #45
    NCB_METHOD(setCoord);                                                    // #46
    NCB_PROPERTY(x, getX, setX);                                             // #47
    NCB_PROPERTY(y, getY, setY);                                             // #48
    NCB_PROPERTY(left, getLeft, setLeft);                                    // #49
    NCB_PROPERTY(top, getTop, setTop);                                       // #50
    NCB_METHOD(setFlip);                                                     // #51
    NCB_PROPERTY(flipX, getFlipX, setFlipX);                                 // #52
    NCB_PROPERTY(flipY, getFlipY, setFlipY);                                 // #53
    NCB_METHOD(setOpacity);                                                  // #54
    NCB_PROPERTY(opacity, getOpacity, setOpacity);                           // #55
    NCB_METHOD(setVisible);                                                  // #56
    NCB_PROPERTY(visible, getVisible, setVisible);                           // #57
    NCB_METHOD(setSlant);                                                    // #58
    NCB_PROPERTY(slantX, getSlantX, setSlantX);                              // #59
    NCB_PROPERTY(slantY, getSlantY, setSlantY);                              // #60
    NCB_METHOD(setZoom);                                                     // #61
    NCB_PROPERTY(zoomX, getZoomX, setZoomX);                                 // #62
    NCB_PROPERTY(zoomY, getZoomY, setZoomY);                                 // #63
    NCB_PROPERTY(useD3D, getUseD3D, setUseD3D);                              // #64
    NCB_PROPERTY(pixelateDivision, getPixelateDivision,
                 setPixelateDivision);                                       // #65
    NCB_METHOD_RAW_CALLBACK(setVariable,
                            &Player::setVariableCompatMethod, 0);             // #66
    NCB_METHOD(getVariable);                                                 // #67
    NCB_METHOD(modifyRoot);                                                  // #68
    NCB_PROPERTY_RO(processedMeshVerticesNum,
                    getProcessedMeshVerticesNum);                            // #69
    NCB_METHOD(getLayerNames);                                               // #70
    NCB_METHOD_RAW_CALLBACK(play, &Player::playCompat, 0);                   // #71
    NCB_METHOD_RAW_CALLBACK(progress, &Player::progressCompatMethod, 0);     // #72
    // The historical script name `clear` is a typed two-Variant method whose
    // native body is the gated recursive draw-to-layer fill operation.
    NCB_METHOD_DETAIL(clear, Class, void, Class::drawToLayerRecursive_guess,
                      (tTJSVariant, tTJSVariant));                            // #73
    NCB_METHOD(stop);                                                        // #74
    NCB_METHOD(setCameraOffset);                                             // #75
    NCB_METHOD(getCameraOffset);                                             // #76
    NCB_METHOD(releaseSyncWait);                                             // #77
    NCB_METHOD_DETAIL(draw, Class, void, Class::draw,
                      (tTJSVariant));                                        // #78
    NCB_METHOD(setDrawAffineTranslateMatrix);                                // #79
    NCB_METHOD(contains);                                                    // #80
    NCB_METHOD(calcViewParam);                                               // #81
    NCB_METHOD(getCommandList);                                              // #82
    NCB_METHOD(getLayerMotion);                                              // #83
    NCB_METHOD(getLayerGetter);                                              // #84
    NCB_METHOD(getLayerGetterList);                                          // #85
    NCB_METHOD(skipToSync);                                                  // #86
    NCB_METHOD(onAction);                                                    // #87
    NCB_METHOD(onSync);                                                      // #88
    NCB_METHOD(onGroundCorrection);                                          // #89
    NCB_METHOD(onFindMotion);                                                // #90
    NCB_METHOD(isExistMotion);                                               // #91
    NCB_METHOD(setStereovisionCameraPosition);                               // #92

    // The native registrar owns this one-time 4x4 identity-grid population and
    // the ARM+NEON evaluator promotion after the Player member table is built.
    internal::initializeBezierPatchRuntime_guess();
}

// Motion.EmotePlayer — full NCB surface (70 members + 2 constants). Registration
// order matches all four references. Its typed one-Variant Factory requires an
// ordinary arg0, reserves one Void for ncbind's empty-shell sentinel, ignores
// surplus arguments, and creates the Engine-sized payload only after the
// generated lower-bound gate. Members are subclass members, not Motion
// namespace free functions.
NCB_REGISTER_SUBCLASS_DELAY(EmotePlayer) {
    Factory(&EmotePlayer::factory);

    // 2 constants (binary registers these before the member loop body)
    Variant(TJS_W("TimelinePlayFlagParallel"),
            (tjs_int)TimelinePlayFlagParallel);
    Variant(TJS_W("TimelinePlayFlagDifference"),
            (tjs_int)TimelinePlayFlagDifference);

    // #1-19 Functions
    NCB_METHOD(progress);                       // #1
    Method(TJS_W("frameProgress"),
           &EmoteEngine::progress);             // #2 direct Engine member
    NCB_METHOD(draw);                           // #3
    Method(TJS_W("initPhysics"),
           &EmoteEngine::applyMetadata_guess); // #4 direct Engine member
    Method(TJS_W("startWind"),
           &EmoteEngine::setWind_guess);        // #5 direct Engine member
    NCB_METHOD(stopWind);                       // #6
    NCB_METHOD(play);                           // #7
    NCB_METHOD(clear);                         // #8
    Method(TJS_W("getVariable"),
           &EmoteEngine::getVariable);          // #9 direct Engine member
    NCB_METHOD(contains);                      // #10
    Method(TJS_W("serialize"),
           &EmoteEngine::serializeState_guess);   // #11 direct Engine member
    Method(TJS_W("unserialize"),
           &EmoteEngine::unserializeState_guess); // #12 direct Engine member
    NCB_METHOD(pass);                           // #13
    NCB_METHOD_RAW_CALLBACK(setVariable, &EmotePlayer::setVariableCompat, 0); // #14
    NCB_METHOD_RAW_CALLBACK(setCoord, &EmotePlayer::setCoordCompat, 0);       // #15
    NCB_METHOD_RAW_CALLBACK(setScale, &EmotePlayer::setScaleCompat, 0);       // #16
    NCB_METHOD_RAW_CALLBACK(setRotate, &EmotePlayer::setRotateCompat, 0);     // #17
    NCB_METHOD_RAW_CALLBACK(setColor, &EmotePlayer::setColorCompat, 0);       // #18
    NCB_METHOD_RAW_CALLBACK(setOuterForce, &EmotePlayer::setOuterForceCompat, 0); // #19

    // #20-34 Properties
    NCB_PROPERTY(completionType, getCompletionType, setCompletionType); // #20
    NCB_PROPERTY(chara, getChara, setChara);                           // #21
    NCB_PROPERTY(motion, getMotion, setMotion);                        // #22
    NCB_PROPERTY(motionKey, getMotionKey, setMotionKey);               // #23
    NCB_PROPERTY(project, getMotionKey, setMotionKey);                 // #24
    NCB_PROPERTY(maskMode, getMaskMode, setMaskMode);                  // #25
    NCB_PROPERTY(meshDivisionRatio, getMeshDivisionRatio, setMeshDivisionRatio); // #26
    NCB_PROPERTY(outline, getOutline, setOutline);                     // #27
    NCB_PROPERTY(priorDraw, getPriorDraw, setPriorDraw);               // #28
    NCB_PROPERTY_RO(frameLastTime, getFrameLastTime);                 // #29
    NCB_PROPERTY_RO(frameLoopTime, getFrameLoopTime);                // #30
    NCB_PROPERTY_RO(lastTime, getFrameLastTime);                     // #31
    NCB_PROPERTY_RO(loopTime, getFrameLoopTime);                     // #32
    NCB_PROPERTY_RO(bounds, getBounds);                              // #33
    NCB_PROPERTY_RO(processedMeshVerticesNum, getProcessedMeshVerticesNum); // #34

    NCB_METHOD(setDrawAffineTranslateMatrix);     // #35

    // #36-41 Functions
    NCB_METHOD(getCameraOffset);                // #36
    NCB_METHOD(setCameraOffset);                // #37
    NCB_METHOD(modifyRoot);                      // #38
    NCB_METHOD(setHairScale);                    // #39
    NCB_METHOD(setPartsScale);                   // #40
    NCB_METHOD(setBustScale);                    // #41

    // #42-50 Properties
    NCB_PROPERTY(hairScale, getHairScale, setHairScale);     // #42
    NCB_PROPERTY(bustScale, getBustScale, setBustScale);     // #43
    NCB_PROPERTY(partsScale, getPartsScale, setPartsScale);  // #44
    NCB_PROPERTY(debugPrint, getDebugPrint, setDebugPrint);      // #45
    NCB_PROPERTY(queuing, getQueuing, setQueuing);               // #46
    NCB_PROPERTY(directEdit, getDirectEdit, setDirectEdit);      // #47
    NCB_PROPERTY(selectorEnabled, getSelectorEnabled, setSelectorEnabled); // #48
    NCB_PROPERTY_RO(variableKeys, getVariableKeys);            // #49
    Property(TJS_W("animating"),
             &EmoteEngine::getAnimating_guess, (int)0);        // #50 direct Engine getter

    // #51-70 Functions
    Method(TJS_W("setMirror"),
           &EmoteEngine::setMirror_guess);       // #51 direct Engine member
    Method(TJS_W("skip"),
           &EmoteEngine::resetControllers_guess); // #52 direct Engine member
    NCB_METHOD_RAW_CALLBACK(
        playTimeline, &EmotePlayer::playTimelineRawCallback_guess, 0); // #53
    NCB_METHOD_RAW_CALLBACK(
        stopTimeline, &EmotePlayer::stopTimelineRawCallback_guess, 0); // #54
    NCB_METHOD_RAW_CALLBACK(
        getTimelinePlaying,
        &EmotePlayer::getTimelinePlayingRawCallback_guess, 0);         // #55
    NCB_METHOD_RAW_CALLBACK(
        setTimelineBlendRatio,
        &EmotePlayer::setTimelineBlendRatioRawCallback_guess, 0);      // #56
    NCB_METHOD_RAW_CALLBACK(
        fadeInTimeline, &EmotePlayer::fadeInTimelineRawCallback_guess, 0); // #57
    NCB_METHOD_RAW_CALLBACK(
        fadeOutTimeline, &EmotePlayer::fadeOutTimelineRawCallback_guess, 0); // #58
    Method(TJS_W("getTimelineBlendRatio"),
           &EmoteEngine::getTimelineBlendRatio_guess); // #59 direct Engine
    NCB_METHOD(getVariableRange);                // #60
    NCB_METHOD(getVariableFrameList);            // #61
    Method(TJS_W("getMainTimelineLabelList"),
           &EmoteEngine::getMainTimelineLabelList_guess); // #62 direct Engine
    Method(TJS_W("getDiffTimelineLabelList"),
           &EmoteEngine::getDiffTimelineLabelList_guess); // #63 direct Engine
    Method(TJS_W("getLoopTimeline"),
           &EmoteEngine::getLoopTimeline_guess);          // #64 direct Engine
    Method(TJS_W("getTimelineTotalFrameCount"),
           &EmoteEngine::getTimelineTotalFrameCount_guess); // #65 direct Engine
    Method(TJS_W("getPlayingTimelineInfoList"),
           &EmoteEngine::getPlayingTimelineInfoList_guess); // #66 direct Engine
    // All four reference binaries register these three methods in this order.
    Method(TJS_W("isSelectorTarget"),
           &EmoteEngine::isSelectorTarget);               // #67 direct Engine
    Method(TJS_W("activateSelectorTarget"),
           &EmoteEngine::activateSelectorTarget);         // #68 direct Engine
    Method(TJS_W("deactivateSelectorTarget"),
           &EmoteEngine::deactivateSelectorTarget);       // #69 direct Engine
    NCB_METHOD(getCommandList);                  // #70
}

// ============================================================
// ResourceManager
// ============================================================

NCB_REGISTER_SUBCLASS(ResourceManager) {
    // ResourceManager owns a ClassInfo tuple distinct from SourceCache in all
    // four current references. Motion publishes its class dispatch through an
    // independent TJS_STATICMEMBER subclass item; the constant IsSubClass
    // trait selects this NCB publication path and carries no SourceCache parent
    // metadata or native-pointer adjustment. Setup publishes borrowed
    // name/id/class-object fields before this table and unregister clears them
    // without releasing the class dispatch.
    NCB_CONSTRUCTOR((tTJSVariant, tjs_int));
    // All four current references expose the same 12 typed descriptors in
    // this exact order. bufLayer is the sole read-only property; every other
    // row is an ordinary typed method, so this table has no raw callback.
    // The two setEmotePSBDecrypt* methods are injected later by the separate
    // emoteplayer registrars, rather than belonging to this member table.
    NCB_METHOD(loadSource);
    NCB_METHOD(clearCache);
    NCB_PROPERTY_RO(bufLayer, getBufLayer);
    NCB_METHOD(load);
    NCB_METHOD(unload);
    NCB_METHOD(unloadAll);
    NCB_METHOD(isExistMotion);
    NCB_METHOD(findMotion);
    NCB_METHOD(findSource);
    NCB_METHOD(random);
    NCB_METHOD(requireLayerId);
    NCB_METHOD(releaseLayerId);
}

// ============================================================
// Motion top-level class with constants and subclasses
// ============================================================

static void motion_doAlphaMaskOperation(tTJSVariant dstLayer,
                                        int dstX,
                                        int dstY,
                                        tTJSVariant srcLayer,
                                        int srcX,
                                        int srcY,
                                        int width,
                                        int height,
                                        int threshold,
                                        int maskMode,
                                        int op);
static bool motion_getD3DAvailable();

class Motion {
};

NCB_REGISTER_CLASS(Motion) {
    // There is deliberately no NCB_CONSTRUCTOR row. After this registrar body
    // finishes, ncbind installs a native method named "Motion" whose callback
    // returns TJS_E_NOTIMPL, then publishes the completed class globally.
    // A generated Unregist(false) virtual body can delete the same surface and
    // clear ClassInfo, but it is dormant in all four integrated loaders: their
    // only module entry calls Regist and inserts a registered-set marker.
    // All four references emit these 23 constants first, in this exact order.
    Variant(TJS_W("LayerTypeObj"), (tjs_int)LayerTypeObj);
    Variant(TJS_W("LayerTypeShape"), (tjs_int)LayerTypeShape);
    Variant(TJS_W("LayerTypeLayout"), (tjs_int)LayerTypeLayout);
    Variant(TJS_W("LayerTypeMotion"), (tjs_int)LayerTypeMotion);
    Variant(TJS_W("LayerTypeParticle"), (tjs_int)LayerTypeParticle);
    Variant(TJS_W("LayerTypeCamera"), (tjs_int)LayerTypeCamera);
    Variant(TJS_W("ShapeTypePoint"), (tjs_int)ShapeTypePoint);
    Variant(TJS_W("ShapeTypeCircle"), (tjs_int)ShapeTypeCircle);
    Variant(TJS_W("ShapeTypeRect"), (tjs_int)ShapeTypeRect);
    Variant(TJS_W("ShapeTypeQuad"), (tjs_int)ShapeTypeQuad);
    Variant(TJS_W("PlayFlagForce"), (tjs_int)PlayFlagForce);
    Variant(TJS_W("PlayFlagChain"), (tjs_int)PlayFlagChain);
    Variant(TJS_W("PlayFlagAsCan"), (tjs_int)PlayFlagAsCan);
    Variant(TJS_W("PlayFlagJoin"), (tjs_int)PlayFlagJoin);
    Variant(TJS_W("PlayFlagStealth"), (tjs_int)PlayFlagStealth);
    Variant(TJS_W("TransformOrderFlip"), (tjs_int)TransformOrderFlip);
    Variant(TJS_W("TransformOrderSlant"), (tjs_int)TransformOrderSlant);
    Variant(TJS_W("TransformOrderZoom"), (tjs_int)TransformOrderZoom);
    Variant(TJS_W("TransformOrderAngle"), (tjs_int)TransformOrderAngle);
    Variant(TJS_W("CoordinateRecutangularXY"),
            (tjs_int)CoordinateRecutangularXY);
    Variant(TJS_W("CoordinateRecutangularXZ"),
            (tjs_int)CoordinateRecutangularXZ);
    Variant(TJS_W("MaskModeStencil"), (tjs_int)MaskModeStencil);
    Variant(TJS_W("MaskModeAlpha"), (tjs_int)MaskModeAlpha);

    // The exact eleven in-flow subclass rows follow the constants. Player is
    // the sixth row; it is not a top-level class plus a post-registration alias.
    NCB_SUBCLASS(Point, Point);
    NCB_SUBCLASS(Circle, Circle);
    NCB_SUBCLASS(Rect, Rect);
    NCB_SUBCLASS(Quad, Quad);
    NCB_SUBCLASS(LayerGetter, LayerGetter);
    NCB_SUBCLASS(Player, Player);
    NCB_SUBCLASS(SourceCache, SourceCache);
    NCB_SUBCLASS(ObjSource, ObjSource);
    NCB_SUBCLASS(ResourceManager, ResourceManager);
    // A vptr-only static subclass item exposes SLA's own class dispatch.  It
    // carries no parent class ID, cast thunk, native offset or Player raw owner.
    NCB_SUBCLASS(SeparateLayerAdaptor, SeparateLayerAdaptor);
    // D3DAdaptor uses the same vptr-only publication shape, but returns its own
    // independent class object.  The item does not expose the shared raw
    // renderer and carries no inheritance/cast/native-offset metadata.
    NCB_SUBCLASS(D3DAdaptor, D3DAdaptor);

    // Both namespace functions follow D3DAdaptor in the same registrar.
    Method(TJS_W("doAlphaMaskOperation"), &motion_doAlphaMaskOperation);
    Method(TJS_W("getD3DAvailable"), &motion_getD3DAvailable);
}

// ============================================================
// Motion namespace-level free functions
// Four-reference registrar parity: after the 11 subclasses, both functions are
// registered directly on the Motion namespace dispatch object. They are not
// Motion.Player methods; an earlier port attached them to the wrong owner.
// ============================================================

// Registrar ownership, the 11-argument shape, clipping, mode/op branches and
// update behavior are confirmed in all four references. This entry delegates
// to the shared render path rather than duplicating its pixel loops.
// `threshold` is forwarded unchanged. `maskMode` 0/1 selects the native
// stencil mode (`thresholdMaskMode` is true for zero), and `op` 1/2/5/6 maps
// to the compositor item-operation flags.
static void motion_doAlphaMaskOperation(tTJSVariant dstLayer,
                                        int dstX,
                                        int dstY,
                                        tTJSVariant srcLayer,
                                        int srcX,
                                        int srcY,
                                        int width,
                                        int height,
                                        int threshold,
                                        int maskMode,
                                        int op) {
    motion::internal::render_detail::applyMotionAlphaMask_guess(
        dstLayer, dstX, dstY, srcLayer, srcX, srcY, width, height, threshold,
        maskMode, op);
}

// All four references invert the process-cached software-renderer flag. The
// local helper has the same function-static cache and RenderManager source.
static bool motion_getD3DAvailable() {
    return !TVPIsSoftwareRenderManager();
}

// ============================================================
// emoteplayer.dll is separate from motionplayer.dll. Its sole pre-registration
// callback loads motionplayer, then publishes EmotePlayer and the two PSB
// decrypt setters into the Motion namespace. The two D3D classes belong to
// DrawDeviceD3D.dll and are registered in that module's translation unit.
// ============================================================
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("emoteplayer.dll")

static void EmotePlayerPreRegist() {
    // The four emoteplayer registrars above form the separate registration
    // path. The corresponding motionplayer namespace registrars deliberately
    // do not contain EmotePlayer; it is attached only when this module loads.
    ncbAutoRegister::LoadModule(TJS_W("motionplayer.dll"));

    iTJSDispatch2 *global = TVPGetScriptDispatch();
    tTJSVariant value;
    global->PropGet(0, TJS_W("Motion"), nullptr, &value, global);
    iTJSDispatch2 *motion = value.AsObjectNoAddRef();

    // Each current registrar creates the complete EmotePlayer class before
    // registering its class object on Motion with flags 0x10000; the class
    // setup result is not used by the surrounding registration path.
    ncbSubClassItem<EmotePlayer>::Setup(TJS_W("EmotePlayer"), true);
    TJSNativeClassRegisterNCM(
        static_cast<tTJSNativeClass *>(motion), TJS_W("EmotePlayer"),
        ncbClassInfo<EmotePlayer>::GetClassObject(), TJS_W("EmotePlayer"),
        nitClass, TJS_STATICMEMBER);

    // The binary reuses the Motion variant for ResourceManager, constructs the
    // first method variant before converting that value to Object, then reuses
    // the same method variant for the second callback.
    motion->PropGet(0, TJS_W("ResourceManager"), nullptr, &value, motion);
    iTJSDispatch2 *method = TJSCreateNativeClassMethod(
        &ResourceManager::setEmotePSBDecryptSeed);
    tTJSVariant methodValue(method, method);
    method->Release();

    iTJSDispatch2 *manager = value.AsObjectNoAddRef();
    manager->PropSet(TJS_MEMBERENSURE | TJS_STATICMEMBER,
                     TJS_W("setEmotePSBDecryptSeed"), nullptr,
                     &methodValue, manager);

    method = TJSCreateNativeClassMethod(
        &ResourceManager::setEmotePSBDecryptFunc);
    methodValue.SetObject(method, method);
    method->Release();
    manager->PropSet(TJS_MEMBERENSURE | TJS_STATICMEMBER,
                     TJS_W("setEmotePSBDecryptFunc"), nullptr,
                     &methodValue, manager);

    // All four registrar bodies have no explicit Release for the AddRef
    // returned by TVPGetScriptDispatch; only methodValue and value are
    // destructed.
}
NCB_PRE_REGIST_CALLBACK(EmotePlayerPreRegist);
