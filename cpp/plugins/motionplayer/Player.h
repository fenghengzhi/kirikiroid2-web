//
// Created by LiDon on 2025/9/15.
// Reverse-engineered from libkrkr2.so MMotionPlayer API surface
//
#pragma once

#include <memory>
#include <unordered_map>
#include <spdlog/spdlog.h>
#include "tjs.h"
#include "ResourceManager.h"

namespace PSB {
    class PSBDictionary;
}

namespace motion {
    class D3DAdaptor;
}

namespace motion {
    namespace detail {
        struct MotionClip;
        struct PlayerRuntime;
    }

    // Motion class enums
    enum LayerType {
        LayerTypeObj = 0,
        LayerTypeShape = 1,
        LayerTypeLayout = 2,
        LayerTypeMotion = 3,
        LayerTypeParticle = 4,
        LayerTypeCamera = 5
    };

    enum ShapeType {
        ShapeTypePoint = 0,
        ShapeTypeCircle = 1,
        ShapeTypeRect = 2,
        ShapeTypeQuad = 3
    };

    enum PlayFlag {
        PlayFlagForce = 1,
        PlayFlagChain = 2,
        PlayFlagAsCan = 4,
        PlayFlagJoin = 8,
        PlayFlagStealth = 16
    };

    enum TransformOrder {
        TransformOrderFlip = 0,
        TransformOrderSlant = 1,
        TransformOrderZoom = 2,
        TransformOrderAngle = 3
    };

    enum CoordinateType {
        CoordinateRecutangularXY = 0,
        CoordinateRecutangularXZ = 1
    };

    class Player {
    public:
        explicit Player(ResourceManager rm = ResourceManager{});
        ~Player();

        // --- Properties (getter/setter) ---
        void setCompletionType(int v) { _completionType = v; }
        int getCompletionType() const { return _completionType; }

        void setMetadata(tTJSVariant v) { _metadata = v; }
        tTJSVariant getMetadata() const { return _metadata; }

        void setChara(ttstr v) { _chara = v; }
        ttstr getChara() const { return _chara; }

        void setMotion(ttstr v);
        ttstr getMotion() const { return _motionKey; }

        void setMotionKey(ttstr v) { setMotion(v); }
        ttstr getMotionKey() const { return _motionKey; }

        void setOutline(bool v) { _outline = v; }
        bool getOutline() const { return _outline; }

        void setPriorDraw(bool v) { _priorDraw = v; }
        bool getPriorDraw() const { return _priorDraw; }

        void setFrameLastTime(double v) { _frameLastTime = v; }
        double getFrameLastTime() const { return _frameLastTime; }

        void setProgressCompat(double v);
        double getProgressCompat() const;

        void setFrameLoopTime(double v) { _frameLoopTime = v; }
        double getFrameLoopTime() const { return _frameLoopTime; }

        void setLoopTime(double v) { _loopTime = v; }
        double getLoopTime() const { return _loopTime; }

        void setProcessedMeshVerticesNum(int v) { _processedMeshVerticesNum = v; }
        int getProcessedMeshVerticesNum() const { return _processedMeshVerticesNum; }

        void setQueuing(bool v) { _queuing = v; }
        bool getQueuing() const { return _queuing; }

        void setDirectEdit(bool v) { _directEdit = v; }
        bool getDirectEdit() const { return _directEdit; }

        void setSelectorEnabled(bool v) { _selectorEnabled = v; }
        bool getSelectorEnabled() const { return _selectorEnabled; }

        void setVariableKeys(tTJSVariant v) { _variableKeys = v; }
        tTJSVariant getVariableKeys();

        void setAllplaying(bool v) { _allplaying = v; }
        bool getAllplaying() const { return _allplaying; }

        void setSyncWaiting(bool v) { _syncWaiting = v; }
        bool getSyncWaiting() const { return _syncWaiting; }

        void setSyncActive(bool v) { _syncActive = v; }
        bool getSyncActive() const { return _syncActive; }

        void setHasCamera(bool v) { _hasCamera = v; }
        bool getHasCamera() const { return _hasCamera; }

        void setCameraActive(bool v) { _cameraActive = v; }
        bool getCameraActive() const { return _cameraActive; }

        void setStereovisionActive(bool v) { _stereovisionActive = v; }
        bool getStereovisionActive() const { return _stereovisionActive; }

        void setTickCount(double v) { _tickCount = v; }
        double getTickCount() const { return _tickCount; }

        void setSpeed(double v) { _speed = v; }
        double getSpeed() const { return _speed; }

        void setFrameTickCount(double v) { _frameTickCount = v; }
        double getFrameTickCount() const { return _frameTickCount; }

        void setColorWeight(double v) { _colorWeight = v; }
        double getColorWeight() const { return _colorWeight; }

        void setIndependentLayerInherit(bool v) { _independentLayerInherit = v; }
        bool getIndependentLayerInherit() const { return _independentLayerInherit; }

        void setZFactor(double v) { _zFactor = v; }
        double getZFactor() const { return _zFactor; }

        void setCameraTarget(tTJSVariant v) { _cameraTarget = v; }
        tTJSVariant getCameraTarget() const { return _cameraTarget; }

        void setCameraPosition(tTJSVariant v) { _cameraPosition = v; }
        tTJSVariant getCameraPosition() const { return _cameraPosition; }

        void setCameraFOV(double v) { _cameraFOV = v; }
        double getCameraFOV() const { return _cameraFOV; }

        void setCameraAlive(bool v) { _cameraAlive = v; }
        bool getCameraAlive() const { return _cameraAlive; }

        void setCanvasCaptureEnabled(bool v) { _canvasCaptureEnabled = v; }
        bool getCanvasCaptureEnabled() const { return _canvasCaptureEnabled; }

        void setClearEnabled(bool v) { _clearEnabled = v; }
        bool getClearEnabled() const { return _clearEnabled; }

        void setHitThreshold(double v) { _hitThreshold = v; }
        double getHitThreshold() const { return _hitThreshold; }

        void setPreview(bool v) { _preview = v; }
        bool getPreview() const { return _preview; }

        void setOutsideFactor(double v) { _outsideFactor = v; }
        double getOutsideFactor() const { return _outsideFactor; }

        void setResourceManager(tTJSVariant v) { _resourceManager = v; }
        tTJSVariant getResourceManager() const { return _resourceManager; }

        void setStealthChara(ttstr v) { _stealthChara = v; }
        ttstr getStealthChara() const { return _stealthChara; }

        void setStealthMotion(ttstr v) { _stealthMotion = v; }
        ttstr getStealthMotion() const { return _stealthMotion; }

        void setTags(tTJSVariant v) { _tags = v; }
        tTJSVariant getTags() const { return _tags; }

        void setProject(tTJSVariant v) { _project = v; }
        tTJSVariant getProject() const { return _project; }

        void setUseD3D(bool v) { _useD3D = v; }
        bool getUseD3D() const { return _useD3D; }

        // Static accessors for class-level property access
        // (patch.tjs uses: with(Motion.Player) { .useD3D = 0; })
        static tjs_error setUseD3DStatic(tTJSVariant *, tjs_int count,
                                         tTJSVariant **p, iTJSDispatch2 *) {
            if (count == 1 && (*p)->Type() == tvtInteger) {
                bool old = _useD3D;
                _useD3D = static_cast<bool>(**p);
                auto logger = spdlog::get("plugin");
                if(logger) {
                    logger->warn("Motion.Player.useD3D: {} -> {}",
                                 old, _useD3D);
                }
                return TJS_S_OK;
            }
            return TJS_E_INVALIDPARAM;
        }

        static tjs_error getUseD3DStatic(tTJSVariant *r, tjs_int,
                                         tTJSVariant **, iTJSDispatch2 *) {
            *r = tTJSVariant{_useD3D};
            return TJS_S_OK;
        }

        void setMeshline(bool v) { _meshline = v; }
        bool getMeshline() const { return _meshline; }

        bool getBusy() const { return _busy; }

        // --- Methods ---
        void initPhysics();
        tTJSVariant serialize();
        void unserialize(tTJSVariant data);
        void setRotate(double rot);
        void setMirror(bool mirror);
        void setHairScale(double s);
        void setPartsScale(double s);
        void setBustScale(double s);
        void setDrawAffineTranslateMatrix(tTJSVariant m);
        tTJSVariant getCameraOffset();
        void setCameraOffset(tTJSVariant offset);
        void modifyRoot(tTJSVariant data);
        void debugPrint();

        double random();

        // Resource management
        void unload(ttstr name);
        void unloadAll();
        bool isExistMotion(ttstr name);
        tTJSVariant findMotion(ttstr name);
        tjs_int requireLayerId(ttstr name);
        void releaseLayerId(tjs_int id);

        // Drawing/rendering
        void setClearColor(tjs_int color);
        void setResizable(bool v);
        void removeAllTextures();
        void removeAllBg();
        void removeAllCaption();
        void registerBg(tTJSVariant bg);
        void registerCaption(tTJSVariant caption);
        void unloadUnusedTextures();
        tjs_int alphaOpAdd();
        tTJSVariant captureCanvas();
        tTJSVariant findSource(ttstr name);
        void loadSource(ttstr name);
        void clearCache();
        void setSize(tjs_int w, tjs_int h);
        void copyRect(tTJSVariant args);
        void adjustGamma(tTJSVariant args);
        void draw();
        void frameProgress(double dt);

        // Viewport/display
        void setFlip(bool v);
        void setOpacity(double v);
        void setVisible(bool v);
        void setSlant(double v);
        void setZoom(double v);
        tTJSVariant getLayerNames();
        void releaseSyncWait();
        void calcViewParam();
        tTJSVariant getLayerMotion(ttstr name);
        tTJSVariant getLayerGetter(ttstr name);
        tTJSVariant getLayerGetterList();
        void skipToSync();
        void setStereovisionCameraPosition(double x, double y, double z);

        // Timeline/variable queries
        void setVariable(ttstr label, double value);
        double getVariable(ttstr label);
        tjs_int countVariables();
        ttstr getVariableLabelAt(tjs_int idx);
        tjs_int countVariableFrameAt(tjs_int idx);
        ttstr getVariableFrameLabelAt(tjs_int idx, tjs_int frameIdx);
        double getVariableFrameValueAt(tjs_int idx, tjs_int frameIdx);
        bool getTimelinePlaying(ttstr label);
        tTJSVariant getVariableRange(ttstr label);
        tTJSVariant getVariableFrameList(ttstr label);
        tjs_int countMainTimelines();
        ttstr getMainTimelineLabelAt(tjs_int idx);
        tTJSVariant getMainTimelineLabelList();
        tjs_int countDiffTimelines();
        ttstr getDiffTimelineLabelAt(tjs_int idx);
        tTJSVariant getDiffTimelineLabelList();
        bool getLoopTimeline(ttstr label);
        tjs_int countPlayingTimelines();
        ttstr getPlayingTimelineLabelAt(tjs_int idx);
        tjs_int getPlayingTimelineFlagsAt(tjs_int idx);
        tjs_int getTimelineTotalFrameCount(ttstr label);
        void playTimeline(ttstr label, tjs_int flags);
        void stopTimeline(ttstr label);
        void setTimelineBlendRatio(ttstr label, double ratio);
        double getTimelineBlendRatio(ttstr label);
        void fadeInTimeline(ttstr label, double duration, tjs_int flags);
        void fadeOutTimeline(ttstr label, double duration, tjs_int flags);
        tTJSVariant getPlayingTimelineInfoList();

        // Selector
        bool isSelectorTarget(ttstr name);
        void deactivateSelectorTarget(ttstr name);

        // Misc
        tTJSVariant getCommandList();
        bool getD3DAvailable();
        void doAlphaMaskOperation();
        void onFindMotion(ttstr name);
        // Aligned to libkrkr2.so 0x681CAC: motion property as raw callback
        // so we have objthis to call onFindMotion TJS callback.
        static tjs_error setMotionCompat(tTJSVariant *result, tjs_int numparams,
                                         tTJSVariant **param, iTJSDispatch2 *objthis);
        static tjs_error getMotionCompat(tTJSVariant *result, tjs_int numparams,
                                         tTJSVariant **param, iTJSDispatch2 *objthis);
        static tjs_error setDrawAffineTranslateMatrixCompat(
            tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
            Player *nativeInstance);
        static tjs_error captureCanvasCompat(tTJSVariant *result,
                                             tjs_int numparams,
                                             tTJSVariant **param,
                                             Player *nativeInstance);
        static tjs_error drawCompat(tTJSVariant *result, tjs_int numparams,
                                    tTJSVariant **param,
                                    Player *nativeInstance);
        static tjs_error playCompat(tTJSVariant *result, tjs_int numparams,
                                    tTJSVariant **param, iTJSDispatch2 *objthis);
        static tjs_error progressCompatMethod(tTJSVariant *result,
                                              tjs_int numparams,
                                              tTJSVariant **param,
                                              iTJSDispatch2 *objthis);
        static tjs_error isPlayingCompat(tTJSVariant *result, tjs_int numparams,
                                         tTJSVariant **param,
                                         iTJSDispatch2 *objthis);
        static tjs_error stopCompat(tTJSVariant *result, tjs_int numparams,
                                    tTJSVariant **param, iTJSDispatch2 *objthis);
        tTJSVariant motionList();
        void emoteEdit(tTJSVariant args);

    private:
        bool ensureMotionLoaded();
        void syncVariableKeysFromActiveMotion();
        bool renderToLayer(iTJSDispatch2 *layerObject,
                           bool skipUpdate = false);
        bool renderToD3DAdaptor(D3DAdaptor *adaptor);
        ttstr resolveCaptureSourcePath() const;
        const detail::MotionClip *selectActiveClip() const;
        const std::vector<std::string> &activeLayerNames() const;
        const std::unordered_map<
            std::string, std::shared_ptr<const PSB::PSBDictionary>> *
        activeLayersByName() const;
        const std::vector<std::string> &activeSourceCandidates() const;
        void updateLayers(double currentTime);
        // updateLayers sub-phases (aligned to libkrkr2.so sub-functions)
        void updateLayersPhase1_PreLoop(double currentTime);
        void updateLayersPhase2_MainLoop(double currentTime);
        void updateLayersPhase3_CameraConstraint();          // sub_6BC000
        void updateLayersPhase3_VertexComputation();          // sub_6BC4F0
        void updateLayersPhase3_Visibility();                 // sub_6BD8DC
        void updateLayersPhase3_CameraNode();                 // sub_6BDA28
        void updateLayersPhase3_ShapeAABB();                  // sub_6BDCC0
        void updateLayersPhase3_ShapeGeometry();              // sub_6BDE94
        void updateLayersPhase3_MotionSubNode(double currentTime);  // sub_6BE0C0
        void updateLayersPhase3_ParticleEmitter();            // sub_6BEDD0
        void updateLayersPhase3_ParticleSystem(double currentTime); // sub_6BF0DC
        void updateLayersPhase3_AnchorNode();                 // sub_6C0528

        std::shared_ptr<detail::PlayerRuntime> _runtime;
        ResourceManager _resourceManagerNative;
        int _completionType = 0;
        tTJSVariant _metadata;
        ttstr _chara;
        ttstr _motionKey;
        bool _outline = false;
        bool _priorDraw = false;
        double _frameLastTime = 0.0;
        double _frameLoopTime = 0.0;
        double _loopTime = 0.0;
        int _processedMeshVerticesNum = 0;
        bool _queuing = false;
        bool _directEdit = false;
        bool _selectorEnabled = false;
        tTJSVariant _variableKeys;
        bool _allplaying = false;
        bool _syncWaiting = false;
        bool _syncActive = false;
        bool _hasCamera = false;
        bool _cameraActive = false;
        bool _stereovisionActive = false;
        // Camera angle for stereovision (a1+472, sub_6BDA28 at 0x6BDC50)
        double _cameraAngle = 0.0;
        double _cameraPosX = 0, _cameraPosY = 0, _cameraPosZ = 0;
        double _cameraTargetX = 0, _cameraTargetY = 0, _cameraTargetZ = 0;
        double _tickCount = 0.0;
        double _speed = 1.0;
        double _frameTickCount = 0.0;
        double _colorWeight = 1.0;
        bool _independentLayerInherit = false;
        double _zFactor = 1.0;
        tTJSVariant _cameraTarget;
        tTJSVariant _cameraPosition;
        double _cameraFOV = 60.0;
        bool _cameraAlive = false;
        bool _canvasCaptureEnabled = false;
        bool _clearEnabled = false;
        bool _d3dDrawMode = false; // libkrkr2.so byte_909: set when draw(D3DAdaptor) called
        double _hitThreshold = 0.0;
        bool _preview = false;
        double _outsideFactor = 0.0;
        tTJSVariant _resourceManager;
        ttstr _stealthChara;
        ttstr _stealthMotion;
        tTJSVariant _tags;
        tTJSVariant _project;
        inline static bool _useD3D;
        bool _meshline = false;
        bool _busy = false;

        // Aligned to libkrkr2.so Player_updateLayers (0x6BB33C):
        // Camera velocity at player+784/792/800, damping at player+600
        double _cameraVelocityX = 0.0;   // player+784
        double _cameraVelocityY = 0.0;   // player+792
        double _cameraVelocityZ = 0.0;   // player+800
        double _cameraDamping = 1.0;     // player+600 (1.0 = no damping)
        double _rootOffsetX = 0.0;       // player+120, root layer position offset
        double _rootOffsetY = 0.0;       // player+128
        double _rootOffsetZ = 0.0;
        float _cameraOffsetX = 0.0f;    // player+144, set by setCameraOffset (0x6D9A38)
        float _cameraOffsetY = 0.0f;    // player+148

        // Aligned to libkrkr2.so Player_calcBounds (0x6C3D04):
        // AABB stored at player+152~176
        double _boundsMinX = 1e308;
        double _boundsMinY = 1e308;
        double _boundsMaxX = -1e308;
        double _boundsMaxY = -1e308;
        bool _needsInternalAssignImages = false; // flag +613 for updateLayerAfterDraw
        std::unordered_map<std::string, double> _variableValues;

        // Aligned to libkrkr2.so emote scale/rotate fields:
        // sub_681F20: player+1184, sub_681F28: player+1192, sub_681F30: player+1200
        double _hairScale = 1.0;    // player+1184
        double _partsScale = 1.0;   // player+1192
        double _bustScale = 1.0;    // player+1200
        double _rotateAngle = 0.0;  // sub_672568 rotation parameter

        // Aligned to libkrkr2.so player+992: TJS Math.RandomGenerator object.
        // sub_6BA7B8 calls its "random" method to get [0.0, 1.0) doubles.
        // Created via TJS eval "new Math.RandomGenerator()" during init.
        tTJSVariant _tjsRandomGenerator;  // player+992
    };

} // namespace motion
