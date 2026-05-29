//
// Created by LiDon on 2025/9/15.
// Reverse-engineered from libkrkr2.so MMotionPlayer API surface
//
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <limits>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <spdlog/spdlog.h>
#include "tjs.h"
#include "ResourceManager.h"
#include "RuntimeSupport.h"
#include "internal/player_containers.h"

namespace PSB {
    class PSBDictionary;
    class PSBList;
    class IPSBValue;
}

namespace motion {
    class D3DAdaptor;
    class Player;
    class SeparateLayerAdaptor;
    class SourceCache;
}

namespace motion {
    namespace detail {
        struct LayerRenderState;
        struct MotionClip;
        struct MotionEvent;
        struct MotionNode;
        struct MotionParameterEntry;
        struct MotionSnapshot;
        struct PlayerRuntime;
        struct TimelineControlBinding;
        struct TimelineState;

        void buildNodeTree(motion::Player &player,
                           const MotionSnapshot &snapshot,
                           const std::string &clipLabel,
                           motion::ResourceManager *resourceManager,
                           int parentCompletionType);
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

    // Aligned to libkrkr2.so Motion_namespace_ncb_register (0x6D9B08)
    enum TransformOrder {
        TransformOrderFlip = 0,
        TransformOrderAngle = 1,
        TransformOrderZoom = 2,
        TransformOrderSlant = 3
    };

    enum CoordinateType {
        CoordinateRecutangularXY = 0,
        CoordinateRecutangularXZ = 1
    };

    // A3 / A4: forward-declare motion / source / timeline helpers so Player
    // can grant them friend access to migrated members. Definitions live
    // inline in PlayerInternal.h.
    namespace internal {
        std::shared_ptr<detail::MotionSnapshot>
        cacheMotion(Player &, const std::string &, const std::string &,
                    const std::shared_ptr<detail::MotionSnapshot> &);
        std::shared_ptr<detail::MotionSnapshot>
        activateMotion(Player &,
                       const std::shared_ptr<detail::MotionSnapshot> &,
                       ResourceManager *);
        std::shared_ptr<detail::MotionSnapshot>
        resolveMotion(Player &, const ttstr &, const ResourceManager *);
        std::vector<ttstr> buildSourceCandidates(const Player &, const ttstr &);
        std::vector<tTJSVariant> timelineInfoVariants(const Player &);
        const detail::TimelineState *
        nthPlayingTimeline(const Player &, tjs_int);
        double activeClipTime(const Player &, const detail::MotionClip *);
        detail::MotionParameterEntry *
        resolveNodeParameterEntry(Player &, const detail::MotionNode &);
    }

    class Player {
    public:
        explicit Player(ResourceManager rm = ResourceManager{},
                        Player *parentPlayer = nullptr);
        ~Player();

        // --- Properties (getter/setter) ---
        // libkrkr2.so setter sub_6D963C: *(player+1092) = v & 1 (mask, not truncate)
        void setCompletionType(int v) { _completionType = (v & 1) != 0; }
        bool getCompletionType() const { return _completionType; }

        void setMetadata(tTJSVariant v) { _metadata = v; }
        tTJSVariant getMetadata() const { return _metadata; }

        void setChara(ttstr v) { _chara = v; }
        ttstr getChara() const { return _chara; }

        void setMotion(ttstr v);
        ttstr getMotion() const { return _motionKey; }

        void setMotionKey(ttstr v) { setMotion(v); }
        ttstr getMotionKey() const { return _motionKey; }

        // Aligned to libkrkr2.so +1032: ttstr, not bool
        void setOutline(ttstr v) { _outline = v; }
        ttstr getOutline() const { return _outline; }

        // Aligned to libkrkr2.so +1160: double, not int
        void setPriorDraw(double v) { _priorDraw = v; }
        double getPriorDraw() const { return _priorDraw; }

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

        void setSelectorEnabled(bool v);
        bool getSelectorEnabled() const { return _selectorEnabled; }

        void setVariableKeys(tTJSVariant v) { _variableKeys = v; }
        tTJSVariant getVariableKeys();

        void setAllplaying(bool v) { _allplaying = v; }
        bool getPlaying() const;
        bool getAllplaying() const;

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

        // Aligned to libkrkr2.so: tickCount uses ms↔frame conversion (60fps internal)
        // Getter: frameTickCount * 1000/60; Setter: value * 60/1000
        void setTickCount(double v) { _frameTickCount = v * 60.0 / 1000.0; }
        double getTickCount() const {
            return _frameTickCount > 0 ? _frameTickCount * 1000.0 / 60.0 : 0.0;
        }

        // Aligned to libkrkr2.so +1093: bool flag (defaultSyncActive), not double
        void setSpeed(bool v) { _speed = v; }
        bool getSpeed() const { return _speed; }

        void setFrameTickCount(double v) { _frameTickCount = v; }
        double getFrameTickCount() const { return _frameTickCount; }

        // Aligned to libkrkr2.so 0x6CD724 / 0x6CD710: packed color int at +1156
        void setColorWeight(tjs_int v);
        tjs_int getColorWeight() const;

        // Aligned to libkrkr2.so 0x6D9760 / 0x6D9758: raw int at +1148
        void setMaskMode(tjs_int v);
        tjs_int getMaskMode() const;

        // Aligned to libkrkr2.so 0x6CC9D4 / 0x6D9768: bool flag at +1097
        void setIndependentLayerInherit(bool v);
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

        // libkrkr2.so Player_setUseD3DFlag @ 0x6D9920 and getter
        // sub_695DE0 read/write player+909, the same byte draw(D3DAdaptor)
        // sets before entering Player_drawD3D @ 0x6D5B90.
        void setUseD3D(bool v) { _d3dDrawMode = v; }
        bool getUseD3D() const { return _d3dDrawMode; }

        // Aligned to libkrkr2.so +1052: ttstr, not bool
        void setMeshline(ttstr v) { _meshline = v; }
        ttstr getMeshline() const { return _meshline; }

        bool getBusy() const { return _busy; }

        // --- Methods ---
        void initPhysics();
        tTJSVariant serialize();
        void unserialize(tTJSVariant data);
        void setEmoteCoord(double x, double y, double transition = 0.0,
                           double ease = 0.0);
        void setEmoteScale(double scale, double transition = 0.0,
                           double ease = 0.0);
        void setRotate(double rot, double transition = 0.0,
                       double ease = 0.0);
        void setEmoteColor(tjs_uint32 color, double transition = 0.0,
                           double ease = 0.0);
        void setMirror(bool mirror);
        void setEmoteMeshDivisionRatio(double v);
        void startWind(double minAngle, double maxAngle, double amplitude,
                       double freqX, double freqY);
        void stopWind();
        void setOuterForce(ttstr label, double x, double y,
                           double transition = 0.0, double ease = 0.0);
        void setDrawAffineTranslateMatrix(tTJSVariant m);
        tTJSVariant getCameraOffset();
        void setCameraOffset(tTJSVariant offset);
        void modifyRoot(tTJSVariant data);
        void debugPrint();

        double random();

        // Load from a pre-loaded snapshot (used by EmotePlayer.setModule)
        // Aligned to libkrkr2.so: EmoteObject_init (sub_67DBAC) sets Player's
        // activeMotion directly from loaded PSB data without file I/O.
        void loadFromSnapshot(std::shared_ptr<detail::MotionSnapshot> snapshot);

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
        void draw(tTJSVariant target);
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
        void setVariable(ttstr label, double value, double transition = 0.0,
                         double ease = 0.0);
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
        void onFindMotion(ttstr name, int flags = 0);
        bool playMotionLike_0x6B2284(ttstr label, tjs_int flags);
        void progressMsLike_0x6D2A54(double deltaMs);
        void setParentPlayerLike_0x6B1ABC(Player *parentPlayer) {
            _parentPlayer = parentPlayer;
        }
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
        void drawCompat(tTJSVariant *target);
        static tjs_error playCompat(tTJSVariant *result, tjs_int numparams,
                                    tTJSVariant **param, iTJSDispatch2 *objthis);
        static tjs_error progressCompatMethod(tTJSVariant *result,
                                              tjs_int numparams,
                                              tTJSVariant **param,
                                              iTJSDispatch2 *objthis);
        static tjs_error setVariableCompatMethod(tTJSVariant *result,
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

        // Public accessor for EmotePlayer delegation
        double getActiveMotionWidth() const;
        double getActiveMotionHeight() const;
        bool hitTestLayer(ttstr name, double x, double y);

        // Root node position (x/y/left/top)
        // Aligned to libkrkr2.so:
        //   getter: Player_getRootX (0x6D98A8) reads root_node+1592
        //   setter: Player_setRootX (0x6CD028) writes root_node+1592, sets dirty
        double getX() const;
        double getY() const;
        void setX(double v);
        void setY(double v);
        double getLeft() const { return getX(); }
        double getTop() const { return getY(); }
        void setLeft(double v) { setX(v); }
        void setTop(double v) { setY(v); }

        // Internal node-construction hook used by detail::buildNodeTree().
        // Not registered to TJS; keeps child Player init ordering aligned with
        // Player_initNodeFields case 3 (0x6B3C78).
        void inheritChildPlayerStateLike_0x6B3C78(detail::MotionNode &node);

    private:
        bool ensureMotionLoaded();
        // Aligned to libkrkr2.so Player_initNonEmoteMotion (0x6B365C).
        // This is the native/LLDB init_motion stage boundary.
        void initNonEmoteMotionLike_0x6B365C(std::uint32_t playFlags);
        // Aligned to libkrkr2.so Player_buildNodeTree (0x6B51F0). Called
        // eagerly from play/onFindMotion paths; the binary has no lazy gate.
        void buildNodeTree();
        void resetNodeTreeForBuildLike_0x6B56F8();
        // Aligned to libkrkr2.so Player_initVariables (0x6CD750). Writes the
        // Player+1296 std::vector<LabelEntry> from PSB content["variable"].
        // Currently a placeholder; real implementation lands with the
        // std::vector<VariableLabelEntry> field (see RuntimeSupport.h).
        void initVariables();
        friend void detail::buildNodeTree(motion::Player &player,
                                          const detail::MotionSnapshot &snapshot,
                                          const std::string &clipLabel,
                                          motion::ResourceManager *resourceManager,
                                          int parentCompletionType);
        friend void detail::ensureRootNodeLike_0x6CED30(motion::Player &);
        friend void detail::resetNodeTreeKeepRootLike_0x6B56F8(motion::Player &);
        void syncVariableKeysFromActiveMotion();
        void syncSelectorControlsLike_0x670D1C();
        const detail::TimelineState *primaryTimelineStateLike_0x66F80C() const;
        void preProgressPlayingTimelinesLike_0x671764(
            double dt, std::unordered_map<std::string, double> *prevTimes);
        void resetTimelineControlStateLike_0x671A50(
            detail::TimelineState &state,
            const detail::TimelineControlBinding &binding,
            double time);
        void scheduleTimelineControlAnimatorLike_0x671A50(
            detail::TimelineState &state,
            size_t trackIndex,
            float value,
            double transition,
            double easeWeight);
        void applyTimelineControlWindowLike_0x669E1C(
            detail::TimelineState &state,
            const detail::TimelineControlBinding &binding,
            double targetTime,
            bool inclusiveEnd);
        void applyTimelineControlFrameCrossingLike_0x67CD20(
            const std::unordered_map<std::string, double> &prevTimes);
        void stepTimelineControlAnimatorsLike_0x67D01C(double dt);
        void stepTimelineBlendAnimatorsLike_0x67D01C(double dt);
        void setTimelineBlendLike_0x6735AC(
            const std::string &label,
            bool autoStop,
            double value,
            double transition,
            double ease);
        void refreshFixedControllerEvalOutputsLike_0x67D01C();
        void accumulateTimelineContributionLike_0x67C560(
            const std::string &label,
            double &value);
        void setVariableResolvedWeightLike_0x671228(
            const std::string &key,
            double value,
            double transition,
            double easeWeight);
        void resetControllerStateLike_0x66EB8C();
        void applyEvalResultPostProcessLike_0x67CC9C();
        void applyClampControlsLike_0x67C8A8();
        bool shouldMirrorEvalLabelLike_0x67C6B0(const std::string &label);
        double &ensureEvalResultSlotLike_0x686944(const std::string &label);
        void removeEvalResultSlotLike_Reset(const std::string &label);
        detail::MotionParameterEntry *appendParameterEntryLike_0x6B1718(
            const std::shared_ptr<const PSB::PSBDictionary> &dic);
        bool parseParameterListLike_0x6B202C(
            const std::shared_ptr<PSB::IPSBValue> &value);
        void finalizeParameterTableLike_0x6B1ECC();
        double initialParameterRawValueLike_0x6B1ABC(
            const std::string &id) const;
        void bindParameterValueLike_0x6C4668(const std::string &label,
                                             int mode,
                                             double value);
        void writeEvalResultValueLike_0x6C4668(const std::string &label,
                                              double value);
        void writeEvalResultValueLike_0x6C4668(const std::string &label,
                                              int mode,
                                              double value);
        bool renderToLayer(iTJSDispatch2 *layerObject,
                           bool skipUpdate = false);
        bool renderToCanvasLike_0x6C7440(
            tTJSVariant *target,
            bool willCallUpdateLayerAfterDraw);
        bool renderToSeparateLayerAdaptor(iTJSDispatch2 *slaObject);
        bool renderToD3DAdaptor(D3DAdaptor *adaptor);
        bool renderViaSharedD3DAdaptor(iTJSDispatch2 *targetLayerObject);
        iTJSDispatch2 *resolveSeparateLayerRenderTarget(SeparateLayerAdaptor *sla,
                                                        int &canvasWidth,
                                                        int &canvasHeight);
        bool renderMotionFrameToTarget(iTJSDispatch2 *renderTargetObject,
                                       tjs_int canvasWidth,
                                       tjs_int canvasHeight,
                                       const char *traceFunc);
        bool renderAccurateSlaLike_0x6C9CA8(SeparateLayerAdaptor *sla,
                                            iTJSDispatch2 *slaObject,
                                            iTJSDispatch2 *targetLayerObject,
                                            tjs_int canvasWidth,
                                            tjs_int canvasHeight);
        const detail::MotionClip *selectActiveClip() const;
        const std::vector<std::string> &activeSourceCandidates() const;
        void calcBounds();
        void updateLayers();
        bool prepareRenderItems(bool inheritedFlag18 = false);
        void appendPreparedRenderItems();
        void applyPreparedRenderItemTranslateOffsets();
        bool buildRenderCommands(tjs_int canvasWidth, tjs_int canvasHeight);
        bool executeLayerRenderCommands(iTJSDispatch2 *renderLayerObject,
                                        bool skipUpdate);
        bool updateLayerAfterDrawLike_0x6CE7D8(tTJSVariant *target);
        bool updateLayerAfterDraw(iTJSDispatch2 *targetLayerObject);
        bool updateAccurateSLAAfterDraw(iTJSDispatch2 *targetLayerObject);
        bool renderFromPlayerLike_0x6ADE24(D3DAdaptor *adaptor);
        bool renderItemsToD3DTextureLike_0x6ADFBC(D3DAdaptor *adaptor);
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

    public:
        // A8 / A9: temporary mutable accessors for hoisted storage used by
        // anonymous-namespace helpers or by free functions in
        // motion::internal::render_detail:: which we can't friend across TU
        // boundaries cheaply. Marked for A10 cleanup review.
        std::deque<detail::MotionNode> &nodesForBuild() { return _nodes; }
        std::map<std::string, int> &nodeLabelMapForBuild() {
            return _nodeLabelMap;
        }
        std::unordered_map<int, detail::RenderItemNativeFieldLifetime> &
        renderItemNativeFieldLifetimeByNode() {
            return _renderItemNativeFieldLifetimeByNode;
        }

    private:
        ResourceManager _resourceManagerNative;
        Player *_parentPlayer = nullptr; // non-owning, for 0x6B1ABC lookup
        // libkrkr2.so +1092: 1-byte bool. ctor (0x6CED30) sets byte=0;
        // setter sub_6D963C does *(player+1092)=v&1; getter sub_6D9634 reads byte.
        bool _completionType = false;
        tTJSVariant _metadata;
        ttstr _chara;
        ttstr _motionKey;
        ttstr _outline;  // Aligned to libkrkr2.so +1032: ttstr
        // libkrkr2.so +1160: double. ctor (0x6CED30) a1[145]=0x3FF8000000000000=1.5;
        // getter sub_6D965C reads *(player+1160).
        double _priorDraw = 1.5;
        double _frameLastTime = 0.0;
        double _frameLoopTime = 0.0;
        double _clampedEvalTime = 0.0; // player+456: min(_frameLoopTime, totalFrames)
        double _loopTime = 0.0;    // player+1136
        double _cachedTotalFrames = 0.0; // player+1128: cached max totalFrames across timelines
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
        bool _speed = true;           // Aligned to libkrkr2.so +1093: bool flag
        double _frameTickCount = 0.0;
        tjs_int _maskMode = 0;                         // libkrkr2.so +1148
        std::uint32_t _colorWeightPacked = 0xFF808080u; // libkrkr2.so +1156
        bool _independentLayerInherit = false;          // libkrkr2.so +1097
        double _zFactor = 1.0;
        tTJSVariant _cameraTarget;
        tTJSVariant _cameraPosition;
        double _cameraFOV = 60.0;
        bool _cameraAlive = false;
        bool _canvasCaptureEnabled = false;
        bool _clearEnabled = false;
        bool _d3dDrawMode = false; // libkrkr2.so player+909
        double _hitThreshold = 0.0;
        bool _preview = false; // libkrkr2.so +1096
        bool _renderItemInheritedFlag18 = false; // sub_6C2334 arg6 low-bit lineage
        // libkrkr2.so +1176: double. ctor (0x6CED30) a1[147]=0x3FF0000000000000=1.0;
        // getter sub_6D966C reads *(player+1176).
        double _outsideFactor = 1.0;
        tTJSVariant _resourceManager;
        ttstr _stealthChara;
        ttstr _stealthMotion;
        tTJSVariant _tags;
        tTJSVariant _project;
        ttstr _meshline;  // Aligned to libkrkr2.so +1052: ttstr
        bool _busy = false;

        // Aligned to libkrkr2.so Player_updateLayers (0x6BB33C):
        // Camera velocity at player+784/792/800, damping at player+600
        double _cameraVelocityX = 0.0;   // player+784
        double _cameraVelocityY = 0.0;   // player+792
        double _cameraVelocityZ = 0.0;   // player+800
        double _cameraDamping = 1.0;     // player+600 (1.0 = no damping)
        double _rootOffsetX = 0.0;       // player+120, root layer position offset
        double _rootOffsetY = 0.0;       // player+128
        // Pending root position from TJS setter (player.x/y).
        // Applied to root node when nodes are built (deferred because
        // setter may be called before node tree exists).
        double _pendingRootX = 0.0;
        double _pendingRootY = 0.0;
        bool _hasPendingRootPos = false;
        double _rootOffsetZ = 0.0;
        float _cameraOffsetX = 0.0f;    // player+144, set by setCameraOffset (0x6D9A38)
        float _cameraOffsetY = 0.0f;    // player+148

        // Aligned to libkrkr2.so Player_calcBounds (0x6C3D04):
        // AABB stored at player+152~176. ctor (0x6CED30) inits
        // a1[19]=0x7FEFFFFFFFFFFFFF (DBL_MAX), a1[22]=0xFFEFFFFFFFFFFFFF (-DBL_MAX).
        double _boundsMinX = std::numeric_limits<double>::max();
        double _boundsMinY = std::numeric_limits<double>::max();
        double _boundsMaxX = -std::numeric_limits<double>::max();
        double _boundsMaxY = -std::numeric_limits<double>::max();
        bool _needsInternalAssignImages = false; // flag +613 for updateLayerAfterDraw

        // === Motion / source caching state (Phase A3) ===
        // motionsByKey caches MotionSnapshot loads by motion key; activeMotion
        // points at the currently-loaded snapshot. sourceCacheNative +
        // sourceCacheObject form the libkrkr2.so Player+656 SourceCache pair
        // (raw pointer for fast C++ access, TJS variant for script reach).
        std::unordered_map<std::string, std::shared_ptr<detail::MotionSnapshot>>
            _motionsByKey;
        // libkrkr2.so player+656: SourceCache object variant.
        SourceCache *_sourceCacheNative = nullptr;
        tTJSVariant _sourceCacheObject;
        std::shared_ptr<detail::MotionSnapshot> _activeMotion;

        // === Timeline state (Phase A4) ===
        // Per-label timeline runtime + the list of labels currently playing
        // (driven by Player_playTimeline / Player_stopTimeline @ 0x66E000 ish).
        std::unordered_map<std::string, detail::TimelineState> _timelines;
        std::vector<std::string> _playingTimelineLabels;

        // === Render-layer state (Phase A5) ===
        // Web-only host adaptation: tracks Cocos2d / DOM-side layer ids,
        // per-layer render snapshot used by the draw dispatch, plus the
        // background / caption variant lists exposed to script.
        std::unordered_map<std::string, tjs_int> _layerIdsByName;
        std::unordered_map<tjs_int, std::string> _layerNamesById;
        std::unordered_map<tjs_int, detail::LayerRenderState> _renderLayerStates;
        std::vector<tTJSVariant> _backgrounds;
        std::vector<tTJSVariant> _captions;

        // === TJS variant slots (Phase A6) ===
        // lastCanvas / lastViewParam cache the most recent draw target / view
        // parameters. internalRenderLayer mirrors libkrkr2.so player+696 and
        // is consumed by sub_6CE7D8 / sub_6CE938 post-draw paths.
        // scratchWorkLayer is reused per-frame for sub_6C4E28-style clipping.
        // drawAffineMatrix is the 2x3 affine applied during draw dispatch.
        tTJSVariant _lastCanvas;
        tTJSVariant _lastViewParam;
        tTJSVariant _internalRenderLayer;
        tTJSVariant _scratchWorkLayer;
        std::array<double, 6> _drawAffineMatrix{1.0, 0.0, 0.0,
                                                1.0, 0.0, 0.0};

        // === Extension fields (Phase A7) ===
        // disabledSelectorTargets / pendingEvents are Web port extensions
        // without libkrkr2.so equivalents (binary handles selector and
        // event flow differently). The parameter system + activeClip +
        // isEmoteMode map directly to binary state used by the eval loop.
        std::unordered_map<std::string, bool> _disabledSelectorTargets;
        std::vector<detail::MotionEvent> _pendingEvents;
        std::vector<detail::MotionParameterEntry> _parameterEntries;
        std::unordered_map<std::string, size_t> _parameterEntryById;
        detail::MotionParameterEntry _defaultParameterEntry;
        detail::MotionParameterEntry *_defaultParameterEntryPtr = nullptr;
        int _defaultParameterEntryIndex = -1;
        const detail::MotionClip *_activeClip = nullptr;
        // Aligned to libkrkr2.so Player_playImpl (0x6B2284):
        // PSB root "type" field: 0=non-emote (motion), 1=emote.
        bool _isEmoteMode = false;

        // === Node tree + variable label storage (Phase A8) ===
        // _nodes: libkrkr2.so Player+184 (std::deque of MotionNode). Index 0
        // is the constructor-created root; loaded layer trees append at
        // indices [1,end) during Player_buildNodeTree (0x6B51F0).
        // _variableLabelEntries: libkrkr2.so Player+1296 std::vector pending
        // retype to VariableLabelScopeDeque (Phase B alias) in a follow-up.
        // _nodeLabelMap: libkrkr2.so Player+24 std::map<ttstr,int>
        // populated during recursive build with last-write-wins assignment.
        std::deque<detail::MotionNode> _nodes;
        std::vector<detail::VariableLabelEntry> _variableLabelEntries;
        std::map<std::string, int> _nodeLabelMap;

        // === Render-item scratch state (Phase A9) ===
        // preparedRenderItems is the per-frame scratch array of native-shape
        // render items (libkrkr2.so player+936/944). preparedRenderItemsTopLevel
        // and preparedRenderItemsGroup are the a2/a3 lists threaded through
        // sub_6C2334 → sub_6C4E28 → sub_6C7440; both alias into
        // preparedRenderItems. renderItemNativeFieldLifetimeByNode preserves
        // the +21 / +216..228 cross-frame fields. perNodeEvalData is local
        // diagnostic scratch (no binary equivalent).
        std::vector<detail::PreparedRenderItem> _preparedRenderItems;
        std::vector<detail::PreparedRenderItem *> _preparedRenderItemsTopLevel;
        std::vector<detail::PreparedRenderItem *> _preparedRenderItemsGroup;
        std::unordered_map<int, detail::RenderItemNativeFieldLifetime>
            _renderItemNativeFieldLifetimeByNode;
        std::vector<detail::PerNodeEvalData> _perNodeEvalData;
        // _variableValues removed: it duplicated HM2 @ Player+320; merged into _evalResultValues.

        // === Web port render-host state (no libkrkr2.so offset alignment) ===
        // libkrkr2.so encapsulates this state in iTVPDrawDevice subclasses
        // rather than on motion::Player itself, so these scalars have no binary
        // offset to align against. They drive the Cocos2d-x / DOM host's
        // drawDevice API: visibility / opacity / Y-axis flip / slant / zoom /
        // resize policy / clear colour / framebuffer dimensions / alpha-op
        // counter / layer-id allocation counters.
        bool _resizable = false;
        bool _flip = false;
        bool _visible = true;
        double _opacity = 1.0;
        double _slant = 0.0;
        double _zoom = 1.0;
        tjs_int _clearColor = 0;
        tjs_int _width = 0;
        tjs_int _height = 0;
        int _alphaOpCounter = 0;
        tjs_int _nextLayerId = 1;
        tjs_int _nextLayerAbsolute = 1;
    public:
        struct VariableKeyframe {
            float value = 0.0f;
            float duration = 0.0f;
            float weight = 1.0f;
        };
        struct VariableAnimatorState {
            std::string label; // for deque linear lookup (EmoteEngine +256..+656 deques)
            std::deque<VariableKeyframe> queue;
            bool active = false;
            float currentValue = 0.0f;
            float startValue = 0.0f;
            float targetValue = 0.0f;
            float progress = 1.0f;
            float duration = 0.0f;
            float weight = 1.0f;
        };
    private:
        // The 7 animator containers below live on EmoteEngine, not Player.
        // Per libkrkr2.so analysis (Player_setVariable @ 0x671228):
        //   EmoteEngine+256  std::deque<Animator> type-4 controller
        //   EmoteEngine+336  std::deque<Animator> type-5 controller
        //   EmoteEngine+416  std::deque<Animator> type-6 controller
        //   EmoteEngine+576  std::deque<Animator> type-7 controller
        //   EmoteEngine+656  std::deque<Animator> type-8 controller
        //   EmoteEngine+1384 std::unordered_map<ttstr, Animator> _variableAnimators
        // Player accesses them through _engineBack (set by EmoteEngine ctor).
        std::deque<VariableAnimatorState> *
        controllerAnimatorBucketLike_0x671228(int type);
        const std::deque<VariableAnimatorState> *
        controllerAnimatorBucketLike_0x671228(int type) const;
        VariableAnimatorState *
        findControllerAnimatorStateLike_0x671228(const std::string &label);
        const VariableAnimatorState *
        findControllerAnimatorStateLike_0x671228(const std::string &label) const;
        void eraseControllerAnimatorStateLike_0x671228(const std::string &label);
        void clearControllerAnimatorStateLike_0x671228();
        // find-or-emplace helper for (*bucket)[label] / _typeNControllerAnimators[label]
        // call sites (deque has no operator[] keyed by label).
        static VariableAnimatorState &
        findOrInsertControllerStateLike_0x671228(
            std::deque<VariableAnimatorState> &bucket,
            const std::string &label);
        // === libkrkr2.so motion::Player inlined hash maps (Phase B aliases) ===
        // HM1 (Player+264): cascaded PropGet result cache. Owns refcounts on
        // its embedded dispatch + chain via EvalCascadeState's destructor;
        // matches the binary's Player_HM1_value_destroy @0x6DD1A0 release
        // sequence. Empty until A8 wires it into the cascade evaluator.
        detail::EvalCascadeMap _evalCascadeMap;

        // HM3 (Player+1184): per-node-path layer state snapshot keyed by
        // Player_buildNodePathKey @0x6B5C1C output. PerNodeLayerState owns 8
        // ttstr + 5 dispatch + 2 heap slots released in binary dtor order
        // (Player_HM3_value_destroy @0x6DD06C). Empty until A8 wires it into
        // the per-node update path.
        detail::PerNodeLayerStateMap _perNodeLayerStateMap;

        // HM4 (Player+1240): ttstr -> iTJSDispatch2* alias resolution map.
        // Non-owning pointers (alias to a dispatch held elsewhere). Empty
        // until A8 wires it into the alias resolver.
        detail::DispatchAliasMap _dispatchAliasMap;

        // Aligned with libkrkr2.so motion::Player HM2 @ +320
        // (raw label -> double). Upsert helper: Player_HM2_upsert_labelToValue
        // @ 0x686944. Cleared on motion change / reset alongside HM3/HM4.
        // TODO(A8): retype to detail::LabelValueMap (ttstr key + ttstr_hash)
        // so bucket distribution and iteration order match the binary.
        std::unordered_map<std::string, double> _evalResultValues;
        struct EvalResultEntry {
            std::string label;
            double value = 0.0;
        };
        std::list<EvalResultEntry> _evalResultList;
        std::unordered_map<std::string, std::list<EvalResultEntry>::iterator>
            _evalResultListIndex;
        bool _rootFlipX = false;
        bool _mirrorEvalEnabled = false;

        // Parent color propagated from parent motion node (sub_6BE0C0 at 0x6BEB7C).
        // Binary: *(_DWORD *)(childPlayer + 1156) = *(_DWORD *)(node + 100)
        // Stores colorBytes[0..3] packed as RGBA uint32 (default 0xFF808080).
        uint32_t _parentColorPacked = 0xFF808080u;  // player+1156

        // Per-frame flag cleared at end of updateLayers (player+608, 0x6BBDF8).
        // Set to true in constructor; checked by sub_6BE0C0 case 2 (0x6BE664)
        // and sub_6BEDD0 case 2 (0x6BEFF4). When true, case 2 falls through
        // to interpolated derivative path instead of using deltaPos.
        bool _noUpdateYet = true;  // player+608

        // Aligned to libkrkr2.so emote scale/rotate fields:
        // player+1168/+1176 are the duplicated meshDivisionRatio doubles read by
        // Player_startWind (0x6709AC).
        double _emoteMeshDivisionRatio = 1.0;
        double _emoteMeshDivisionRatioDup = 1.0;
        // NOTE: hairScale/partsScale/bustScale are EmotePlayer/EmoteObject
        // properties (sub_681F20/28/30 write EmoteObject+1184/+1192/+1200),
        // NOT motion::Player fields — on the 1384B Player those offsets are
        // hash table HM3. They live on EmotePlayer (EmotePlayer::_hairScale).
        double _rotateAngle = 0.0;  // sub_672568 rotation parameter
        bool _physicsDisabled = false;   // player+1159
        bool _emoteAnimatorFlag = false; // player+1161
        bool _emoteDirty = false;        // player+1162
        struct EmoteCoordState {
            double x = 0.0;
            double y = 0.0;
            double transition = 0.0;
            double ease = 0.0;
        } _emoteCoordState;
        struct EmoteScalarAnimatorState {
            double value = 0.0;
            double transition = 0.0;
            double ease = 0.0;
        } _emoteScaleState, _emoteRotState;
        struct EmoteColorState {
            tjs_uint32 packed = 0;
            std::array<float, 4> rgbaBytes{0.f, 0.f, 0.f, 0.f};
            double transition = 0.0;
            double ease = 0.0;
        } _emoteColorState;

        struct WindState {
            bool active = false;
            double minAngle = 0.0;
            double maxAngle = 0.0;
            double amplitude = 0.0;
            double freqX = 0.0;
            double freqY = 0.0;
            double phase = 0.0;
            double prevPhase = 0.0;
            double scaledAmplitude = 0.0;
            int counter = 0;
        } _windState;

        struct OuterForceState {
            bool active = false;
            double x = 0.0;
            double y = 0.0;
            double transition = 0.0;
            double ease = 0.0;
        };

        OuterForceState _bustOuterForce;
        OuterForceState _hairOuterForce;
        OuterForceState _partsOuterForce;

        // Aligned to libkrkr2.so player+992: TJS Math.RandomGenerator object.
        // sub_6BA7B8 calls its "random" method to get [0.0, 1.0) doubles.
        // Created via TJS eval "new Math.RandomGenerator()" during init.
        tTJSVariant _tjsRandomGenerator;  // player+992

        // Aligned to libkrkr2.so player+1012:
        // - written from Player_playImpl / load-motion result path
        // - propagated to child particle players (sub_6BF0DC at 0x6BF9C0)
        // - copied into render item +248 by sub_6C2334
        // Its exact semantic name is still under investigation, so keep the
        // local field neutral instead of claiming it is emoteEdit-specific.
        // libkrkr2.so player+1012:
        // second result returned by Player_loadMotion / Player_playImpl and
        // then fed back into Player_loadMotion as the first argument to
        // "findMotion" (0x6B0F10 / 0x6B2284), including child-player copies.
        tTJSVariant _findMotionContextVariant;    // player+1012

        // Web-port back-pointer: in libkrkr2.so, Player_setVariable (0x671228)
        // and friends actually run with EmoteEngine `this` (the function is
        // mis-named in the binary). Locally we keep methods on Player but route
        // engine-resident state through this pointer. EmoteEngine ctor body sets
        // it after Player is constructed; non-owning, always valid for the
        // lifetime of the owning EmoteEngine.
        class EmoteEngine *_engineBack = nullptr;
        friend class EmoteEngine;
        // A3: SourceCache holds a back-pointer to Player so it can reach the
        // migrated _activeMotion (and PlayerRuntime via _runtime until later
        // sub-steps migrate the rest).
        friend class SourceCache;

        // A3: motion / source helpers in motion::internal:: access
        // _motionsByKey / _activeMotion / _sourceCacheNative /
        // _sourceCacheObject directly. Declared here so PlayerInternal.h's
        // inline helpers can keep their non-member shape during the
        // PlayerRuntime tear-down.
        friend std::shared_ptr<detail::MotionSnapshot> internal::cacheMotion(
            Player &, const std::string &, const std::string &,
            const std::shared_ptr<detail::MotionSnapshot> &);
        friend std::shared_ptr<detail::MotionSnapshot> internal::activateMotion(
            Player &, const std::shared_ptr<detail::MotionSnapshot> &,
            ResourceManager *);
        friend std::shared_ptr<detail::MotionSnapshot> internal::resolveMotion(
            Player &, const ttstr &, const ResourceManager *);
        friend std::vector<ttstr> internal::buildSourceCandidates(
            const Player &, const ttstr &);
        // A4: timeline helpers — read _timelines / _playingTimelineLabels.
        friend std::vector<tTJSVariant> internal::timelineInfoVariants(
            const Player &);
        friend const detail::TimelineState *
        internal::nthPlayingTimeline(const Player &, tjs_int);
        // Defined in PlayerFrameProgress.cpp under motion::internal:: to
        // keep its single-caller scope; granting friend access here lets
        // it reach _timelines / _playingTimelineLabels.
        friend double internal::activeClipTime(
            const Player &, const detail::MotionClip *);
        // A7: parameter resolver needs _parameterEntries /
        // _defaultParameterEntry{,Ptr,Index}.
        friend detail::MotionParameterEntry *internal::resolveNodeParameterEntry(
            Player &, const detail::MotionNode &);
    };

} // namespace motion
