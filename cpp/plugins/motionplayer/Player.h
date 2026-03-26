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
                _useD3D = static_cast<bool>(**p);
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
        bool renderToLayer(iTJSDispatch2 *layerObject);
        ttstr resolveCaptureSourcePath() const;
        const detail::MotionClip *selectActiveClip() const;
        const std::vector<std::string> &activeLayerNames() const;
        const std::unordered_map<
            std::string, std::shared_ptr<const PSB::PSBDictionary>> *
        activeLayersByName() const;
        const std::vector<std::string> &activeSourceCandidates() const;

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
        std::unordered_map<std::string, double> _variableValues;
    };

} // namespace motion
