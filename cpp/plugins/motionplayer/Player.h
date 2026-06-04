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
#include "internal/legacy_variable_state.h"

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
                           int parentPreview);
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
        // completionType: binary +1144 int (full value); getter
        // Player_getCompletionType reads *(uint*)(this+1144). NOT the +1092 bool
        // (that is `preview`) — Player-table off-by-one had conflated them.
        void setCompletionType(tjs_int v) { _completionType = v; }
        [[nodiscard]] tjs_int getCompletionType() const { return _completionType; }

        void setMetadata(tTJSVariant v) { _metadata = v; }
        tTJSVariant getMetadata() const { return _metadata; }

        // Aligned to libkrkr2.so Player_setChara @0x6D94B0 (NCB "chara" setter).
        // Not a plain assignment: a chara change must invalidate the loaded
        // motion so the next play/update reloads against the new chara.
        void setChara(ttstr v);
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

        void setLoopTime(double v) { _loopTime = v; }
        double getLoopTime() const { return _loopTime; }

        // NOTE: a343ce9 added a getLoopTimeArrayLike_0x6D139C() here on the
        // misread that member "loopTime" binds to 0x6D139C. Verified false:
        // binary Player_ncb_registerMembers binds "loopTime" -> Player_getLastTime
        // (scalar) and 0x6D139C -> member "variableKeys". Function removed; the
        // var-track cascadeKey array for "variableKeys" is a separate alignment
        // item (local "variableKeys" currently reads _activeMotion->variableLabels
        // via getVariableKeys, NOT the Player+1296 cascadeKey deque 0x6D139C walks).

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
        // M15 missing transform `bounds` property (cluster E §3.1): binary
        // exposes bounds as TJS dict {left, top, right, bottom} from
        // _boundsMinX/MinY/MaxX/MaxY (binary +152/+160/+168/+176 floats).
        tTJSVariant getBounds() const;

        // M15 missing `meshDivisionRatio` (cluster E §3.1): binary Motion.Player
        // exposes as property delegating to EmoteEngine +1168. Defined in
        // PlayerCore.cpp where EmoteEngine.h is fully visible.
        double getMeshDivisionRatio() const;
        void setMeshDivisionRatio(double v);

        // M15 missing #3 (cluster E §3.1): binary `lastTime` NCB getter
        // sub_6D9448 reads +1136 (_loopTime):
        //   if (loopTime > 0) return loopTime * 1000/60   (frames → ms)
        //   else              return loopTime             (0 or negative)
        // RO property (no setter on binary).
        double getLastTime() const {
            return _loopTime > 0.0 ? _loopTime * 1000.0 / 60.0 : _loopTime;
        }

        // M16 P1 (cluster E §4): binary setTickCount_ms @0x6D96C0 does
        //   +1120 = fmax(v * 60/1000, 0)       (frameTickCount cursor)
        //   *(WORD*)(+480) = 257               (STRH 0x0101 — +480 = _queuing,
        //                                        +481 = _firstFrame both = 1)
        //   +456 = min(+1120, +1128)           (clampedEvalTime = min cursor,
        //                                        cachedTotalFrames)
        // Port previously only wrote _frameTickCount and missed all three
        // side effects.
        void setTickCount(double v) {
            double cursor = v * 60.0 / 1000.0;
            if (cursor < 0.0) cursor = 0.0;
            _frameTickCount = cursor;
            _queuing = true;
            _firstFrame = true;
            _clampedEvalTime = (cursor < _cachedTotalFrames)
                                   ? cursor
                                   : _cachedTotalFrames;
        }
        // M16 P2 (cluster E §4): binary getTickCount_ms @0x6D96A0 returns
        // +1120 * 1000/60 UNCONDITIONALLY (no >0 guard). Removed port's guard.
        double getTickCount() const { return _frameTickCount * 1000.0 / 60.0; }

        // NCB "speed" property = binary +1168 double speed multiplier (getter
        // sub_6D967C `return *(double*)(this+1168)`, bound to L"speed" @0x6d7308)
        // = local _speedMul, which already drives _deltaTime = _speedMul*dt. It
        // is NOT the +1093 bool gate _speed (separate play/pause flag). Was
        // mis-wired to the bool _speed; the IDB getter symbol is off-by-one
        // mislabeled (Player_getMeshDivisionRatio).
        void setSpeed(double v) { _speedMul = v; }
        double getSpeed() const { return _speedMul; }

        void setFrameTickCount(double v) { _frameTickCount = v; }
        double getFrameTickCount() const { return _frameTickCount; }

        // M15 missing #19 (cluster E §1 ctor +912 / §3.1 missing list):
        // binary Player_ctor stores 100 at +912 — pixelateDivision is an
        // INSTANCE field, not D3DEmoteModule static (port misplaces it).
        // R-pixelate phase 1: add Player instance field + NCB exposure here
        // additively; D3DEmoteModule static remains for compat in phase 1.
        void setPixelateDivision(int v) { _pixelateDivision = v; }
        int getPixelateDivision() const { return _pixelateDivision; }

        // M15 missing transform properties (cluster E §3.1 24 missing list):
        // binary Motion.Player exposes flipX/flipY/opacity/visible/slantX/
        // slantY/zoomX/zoomY as PROPERTIES backed by root node accumulated
        // transform state. Port previously exposed only setFlip/setOpacity/
        // setVisible/setSlant/setZoom METHODS (single-axis) without getters.
        // Adding NCB-bound X/Y-split property accessors that read/write the
        // root node delta. Existing setFlip/etc. preserved (port-extra).
        //
        // Defensive empty-deque check: returns default / no-op if _nodes is
        // empty (pre-motion-load state).
        bool getFlipX() const {
            return _nodes.empty() ? false : _nodes[0].delta.flipX;
        }
        void setFlipX(bool v) {
            if (_nodes.empty()) return;
            _nodes[0].delta.flipX = v;
            _nodes[0].delta.dirty = true;
        }
        bool getFlipY() const {
            return _nodes.empty() ? false : _nodes[0].delta.flipY;
        }
        void setFlipY(bool v) {
            if (_nodes.empty()) return;
            _nodes[0].delta.flipY = v;
            _nodes[0].delta.dirty = true;
        }
        double getSlantX() const {
            return _nodes.empty() ? 0.0 : _nodes[0].delta.slantX;
        }
        void setSlantX(double v) {
            if (_nodes.empty()) return;
            _nodes[0].delta.slantX = v;
            _nodes[0].delta.dirty = true;
        }
        double getSlantY() const {
            return _nodes.empty() ? 0.0 : _nodes[0].delta.slantY;
        }
        void setSlantY(double v) {
            if (_nodes.empty()) return;
            _nodes[0].delta.slantY = v;
            _nodes[0].delta.dirty = true;
        }
        double getZoomX() const {
            return _nodes.empty() ? 1.0 : _nodes[0].delta.scaleX;
        }
        void setZoomX(double v) {
            if (_nodes.empty()) return;
            _nodes[0].delta.scaleX = v;
            _nodes[0].delta.dirty = true;
        }
        double getZoomY() const {
            return _nodes.empty() ? 1.0 : _nodes[0].delta.scaleY;
        }
        void setZoomY(double v) {
            if (_nodes.empty()) return;
            _nodes[0].delta.scaleY = v;
            _nodes[0].delta.dirty = true;
        }
        // M15 missing visible/opacity (cluster E §3.1 24 missing): binary
        // exposes both as PROPERTIES. Port has setVisible/setOpacity METHODS
        // (no getters). Getters added here read from root node delta state
        // (opacity int 0-255 ↔ TJS double 0..1 conversion). Port C++
        // _visible/_opacity scalars still drive existing internal code paths.
        bool getVisible() const {
            return _nodes.empty() ? true : _nodes[0].delta.visibleOverride;
        }
        double getOpacity() const {
            // DeltaState.opacity is int 0..255; expose to TJS as double 0..1
            // matching binary semantics (binary 0..1 double float).
            return _nodes.empty() ? 1.0
                                  : static_cast<double>(_nodes[0].delta.opacity)
                                        / 255.0;
        }

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
        // Aligned with libkrkr2.so EmotePlayer setCameraOffset @0x681EF8:
        //   raw float write player+144/+148 (= _cameraOffsetX/Y). The
        //   Motion.EmotePlayer NCB #35 callback passes (x,y) doubles directly,
        //   not a TJS offset object (that is Motion.Player's setCameraOffset
        //   @0x6D9A38). Two distinct entry points → two setters.
        void setCameraOffsetXY_0x681EF8(double x, double y) {
            _cameraOffsetX = static_cast<float>(x);
            _cameraOffsetY = static_cast<float>(y);
        }
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
        // M5-2: core of getLayerNames @0x6D10E0 — in-order walk of the Player+24
        // node-index map emitting each raw-label key as a string variant. When
        // `filter` is non-null, only keys CONTAINING it are emitted (substring,
        // case-sensitive; an empty filter emits none — see getLayerNamesCompat).
        tTJSVariant collectLayerNames(const ttstr *filter);
        void releaseSyncWait();
        void calcViewParam();
        tTJSVariant getLayerMotion(ttstr name);
        tTJSVariant getLayerGetter(ttstr name);
        tTJSVariant getLayerGetterList();
        void skipToSync();
        void setStereovisionCameraPosition(double x, double y, double z);

        // Timeline/variable queries
        //   NOTE: there is no 4-arg Player::setVariable. The binary's
        //   Motion.Player.setVariable NCB member (callback @0x6D0E70) maps to
        //   Player_bindParameterValue @0x6C4668 (writes Player HM1/HM2), bound
        //   locally via setVariableCompatMethod -> writeEvalResultValueLike_0x6C4668.
        double getVariable(ttstr label);
        // libkrkr2.so Player_bindParameterValue_writesHM1_HM2 @0x6C4668: writes
        // a var value into HM2 (_evalResultValues, always) and, when the key
        // splits on "::"/"/", HM1 (_evalCascadeMap[joined].writeVal). Called by
        // interpolateVarTrackValues per item. Dispatch/animator side-effects
        // (sub_697D34, RenderItem updates) have no getVariable consumer → DEFERRED.
        void bindParameterValueLike_0x6C4668(const ttstr &key, double value);
        // libkrkr2.so Player_isLabelInBindScopeList @0x6CD16C: scans the
        // var-track deque (_variableLabelScopes), true if any item's cascadeKey
        // equals the lookup key. The scope gate of getVariable's 2-branch router.
        [[nodiscard]] bool isLabelInBindScopeListLike_0x6CD16C(
            const ttstr &key) const;
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
        // getD3DAvailable / doAlphaMaskOperation moved off Player: in
        // libkrkr2.so (motionplayer_ncb_register @0x6D9B08) they are Motion
        // namespace-level free functions, not Motion.Player methods. See
        // main.cpp motion_getD3DAvailable / motion_doAlphaMaskOperation.
        void onFindMotion(ttstr name, int flags = 0);
        bool playMotionLike_0x6B2284(ttstr label, tjs_int flags);
        void progressMsLike_0x6D2A54(double deltaMs);
        // Raw sub_6D2A54 @0x6D2A54: (player, 0, frameDt). Sets the pendingEvents
        // cursor (player+16) to 0, runs Player_progress_inner with frameDt
        // ALREADY in frame units (NO ms->frame *60/1000 conversion — that lives
        // in the NCB wrappers Player_progressCompat@0x6D2A98 / sub_6818B4),
        // updateLayers, calcBounds, dispatchEvents, then clears the cursor. This
        // is the step-7 Player progress called from inside EmoteEngine::progress
        // (EmoteEngine_progress @0x67d408 passes v12=ORIGINAL frame-dt). Distinct
        // from progressMsLike_0x6D2A54 which takes MILLISECONDS and converts.
        void progressFramesLike_0x6D2A54(double frameDt);
        void setParentPlayerLike_0x6B1ABC(Player *parentPlayer) {
            _parentPlayer = parentPlayer;
        }
        // Aligned to libkrkr2.so 0x681CAC: motion property as raw callback
        // so we have objthis to call onFindMotion TJS callback.
        static tjs_error setMotionCompat(tTJSVariant *result, tjs_int numparams,
                                         tTJSVariant **param, iTJSDispatch2 *objthis);
        static tjs_error getMotionCompat(tTJSVariant *result, tjs_int numparams,
                                         tTJSVariant **param, iTJSDispatch2 *objthis);
        // M5-2: raw NCB callback for getLayerNames @0x6D10E0 (registered as
        // "getLayerNames" @0x6D88C8). The binary takes an optional args[0]
        // substring filter — its absence (void type tag, `*a2==0`) emits all
        // keys; a string arg emits only keys containing it. Matches the raw
        // (this, args, result) calling convention of the binary callback.
        static tjs_error getLayerNamesCompat(tTJSVariant *result,
                                             tjs_int numparams,
                                             tTJSVariant **param,
                                             iTJSDispatch2 *objthis);
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

        // libkrkr2.so Player_getAngleRad @0x6CD0C0 (IDB symbol corrected
        // 2026-06-03; was formerly mislabeled "Player_getAngleDeg").
        // Returns the angle in RADIANS:
        //   if (_directEdit/+482) -> _emoteAngle/+464 else root node delta.angle
        //   times 0.0174532925 (deg->rad). Used by EmoteEngine_stepHairParts /
        //   stepBust as the rad provider.
        //
        // Per registration @0x6D69C8: 0x6CD0C0 backs the TJS **angleRad** member;
        // the TJS **angleDeg** member is backed by Player_getAngleDeg @0x6C1780
        // (raw value, no scale -> degrees). So:
        //   angleDeg member -> DEG (Player_getAngleDeg @0x6C1780)
        //   angleRad member -> RAD (this fn, Player_getAngleRad @0x6CD0C0)
        double emoteGetAngleRadLike_0x6CD0C0() const;

        // binary Motion.Player exposes angleDeg/angleRad as TJS properties.
        // Internal angle storage (root+1616 / _emoteAngle+464) is in DEGREES.
        //   angleDeg member: getter = Player_getAngleDeg @0x6C1780 (raw -> DEG);
        //                    setter = Player_setAngleDeg @0x6C0F84 (input DEG, direct)
        //   angleRad member: getter = Player_getAngleRad @0x6CD0C0 (raw*0.0174 -> RAD);
        //                    setter = Player_setAngleRad @0x6CD0EC (input RAD ->
        //                             * 57.2957795 -> DEG -> same store path)
        // Fixed 2026-06-03: bindings were previously swapped (getAngleDeg returned
        // rad, getAngleRad returned raw deg). The IDB symbols for these 4 fns were
        // also formerly mislabeled (swapped deg/rad) and have been corrected;
        // verified against registration @0x6D69C8 (sites 0x6d7db4 / 0x6d7e30).
        double getAngleDeg() const {            // sub_6C1780: raw stored -> deg
            if (_directEdit) return _emoteAngle;            // +464  /*0x6c178c*/
            return _nodes.empty() ? 0.0
                                  : _nodes[0].delta.angle;  // root+1616 /*0x6c179c*/
        }
        void setAngleDeg(double deg);  // sub_6C0F84 (deg-direct); impl in PlayerCore.cpp
        double getAngleRad() const {            // 0x6CD0C0: stored deg * pi/180 -> rad
            return emoteGetAngleRadLike_0x6CD0C0();
        }
        void setAngleRad(double rad); // Player_setAngleDeg@0x6CD0EC; impl in PlayerCore.cpp

        // Public accessor for EmotePlayer delegation
        double getActiveMotionWidth() const;
        double getActiveMotionHeight() const;
        bool hitTestLayer(ttstr name, double x, double y);
        // M15 missing `contains` (cluster E §3.1 24 missing): binary
        // Motion.Player exposes contains(label, x, y) — alias of binary's
        // sub_6B5AD8 layer-resolve + Player_hitTest. Port delegates to
        // hitTestLayer (same path).
        bool contains(ttstr label, double x, double y) {
            return hitTestLayer(label, x, y);
        }

        // M15 missing event callbacks (cluster E §3.1 24 missing):
        // binary Motion.Player exposes onAction/onSync/onGroundCorrection
        // as TJS callback PROPERTIES. binary invokes these at specific
        // points (action triggered / sync event / ground correction); port
        // adds storage now, invocation pending spike of binary call sites
        // (cluster E §3.1 lists these without call-site addresses).
        void setOnAction(tTJSVariant v) { _onAction = std::move(v); }
        [[nodiscard]] tTJSVariant getOnAction() const { return _onAction; }
        void setOnSync(tTJSVariant v) { _onSync = std::move(v); }
        [[nodiscard]] tTJSVariant getOnSync() const { return _onSync; }
        void setOnGroundCorrection(tTJSVariant v) {
            _onGroundCorrection = std::move(v);
        }
        [[nodiscard]] tTJSVariant getOnGroundCorrection() const {
            return _onGroundCorrection;
        }

        // libkrkr2.so registers onAction/onSync/onGroundCorrection as Motion.Player
        // *methods* (Function-kind descriptors @0x6D69C8), NOT properties.
        //   onAction           cb = nullsub_87 @0x6D9A50          -> empty no-op
        //   onSync             cb = nullsub_88 @0x6D9A54          -> empty no-op
        //                      (registered via sub_6D993C @0x6d8edc, X2=nullsub_88)
        //   onGroundCorrection cb = Player_onAction_ncb @0x6D9A58 -> sub_A0F5E0(out,a1):
        //       a tTJSVariant copy/AddRef helper; performs no Player state change,
        //       so the faithful native body is a no-op method.
        // The getOn*/setOn* property accessors above are port-invented storage and
        // are no longer NCB-exposed; kept only to avoid touching unrelated code.
        void onAction() {}            // nullsub_87 @0x6D9A50
        void onSync() {}              // nullsub_88 @0x6D9A54
        void onGroundCorrection() {}  // Player_onAction_ncb @0x6D9A58 (variant-copy, no state change)

        // M15 missing transformOrder/coordinate int properties (cluster E
        // §3.1 24 missing): scaffold port int fields with default 0; binary
        // semantics not yet spiked, port stores value but doesn't drive any
        // behavior off them.
        // transformOrder: binary property (getter sub_6CC188 / setter sub_6CC2C4,
        // bound to L"transformOrder" @0x6d7838) is a 4-int TJS Array — a
        // permutation of {0,1,2,3} (apply order of [0=Flip,1=Angle,2=Zoom,
        // 3=Slant]) read/written at node+84..96 — NOT a scalar. A scalar here
        // throws object->real if a script does arithmetic on it (same class as
        // the loopTime crash). Port stores the 4 ints on the Player (matching
        // the standalone-field choice for this NCB cluster; node-tree wiring is
        // a separate pre-existing divergence shared with coordinate).
        void setTransformOrder(tTJSVariant arr);
        [[nodiscard]] tTJSVariant getTransformOrder() const;
        void setCoordinate(tjs_int v) { _coordinate = v; }
        [[nodiscard]] tjs_int getCoordinate() const { return _coordinate; }

        // Root node position (x/y/left/top)
        // Aligned to libkrkr2.so:
        //   getter: Player_getRootX (0x6D98A8) reads root_node+1592
        //   setter: Player_setRootX (0x6CD028) writes root_node+1592, sets dirty
        double getX() const;
        double getY() const;
        void setX(double v);
        void setY(double v);
        // M15 missing #10 (cluster E §4): binary `setCoord` @0x6CCFF8 writes
        // root+1592=x, root+1600=y, dirty if changed. Atomic combined writer.
        void setCoord(double x, double y);
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
        // Aligned to libkrkr2.so Player_initVariables (0x6CD750). Builds the
        // Player+1296 std::deque<VariableLabelScope> (_variableLabelScopes)
        // from PSB root["variable"]: one var-track item per entry, cascadeKey =
        // scope+"::"+label. Snapshotted into HM4 by resetMotionState loop2.
        void initVariables();
        friend void detail::buildNodeTree(motion::Player &player,
                                          const detail::MotionSnapshot &snapshot,
                                          const std::string &clipLabel,
                                          motion::ResourceManager *resourceManager,
                                          int parentPreview);
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
        // M1/P7 step-1: progress-pass cursor-stepping driver.
        // Aligned to libkrkr2.so Player_progress_inner (0x6C106C), the ONLY
        // caller of the advance/rewind/reseek cursor machine. In the binary the
        // per-node frame seek (Player_advanceNodeFrames 0x6B7E44, reached at
        // 0x6C1264/0x6C130C) runs HERE — filling each node's two parsed-frame
        // slots (node+320/+856) — and the SEPARATE Player_updateLayers pass
        // (0x6BB33C) only reads those slots via Player_evaluateTimeline
        // (0x699AE4, eval-at-time). This method hoists the live per-node seek
        // (advanceNodeFrameSelectionLike_0x6926B4, which already operates on the
        // live MotionNode::ClipSlot = the binary's node+320/+856 slots) out of
        // updateLayers and into the progress pass, restoring the binary's
        // two-pass split. Forward-only for step-1; reverse rewind is a TODO.
        void progressSeekNodeSlotsLike_0x6C106C(double clampedEvalTime);
        // 砖5/洞1: Player_preProgressDirtyNodes (0x6B6878) — progress_inner's first
        // step (0x6C10AC): per-node "modified" emoteEdit-dict check -> timeline
        // rebuild. Inert in the web port (no modified-setter); ported for
        // call-chain restoration. See PlayerUpdateLayerEval.cpp.
        void preProgressDirtyNodesLike_0x6B6878();
        // 砖5/洞3: faithful layer (motion["tag"]) event stream — bidirectional
        // incremental cursor seek toward targetTime (= _clampedEvalTime), porting
        // the layer-stream loops of Player_advanceRootAndNodes (0x6B6ADC) +
        // Player_rewindRootAndNodes (0x6B9A3C): fires +1093(_speed)-gated
        // align/sync (with frameTickCount/clampedEvalTime snapping) and ungated
        // action -> onAction(void, actionName)/onSync(). Replaces the
        // port-invented per-timeline scanLayerActions.
        void seekLayerEventStreamLike_0x6B6ADC(double targetTime);
        // libkrkr2.so root content-snapshot stream ② (the 2nd of [layer → root →
        // var-track → node] inside Player_advanceRootAndNodes 0x6B6ADC, root loop
        // 0x6B6EE4..0x6B7124). Forward-only cursor advance over motion["priority"]
        // (Player+548): on each crossed frame snapshots priority[cursor]["content"]
        // into _rootContent (Player+616, sub_A0FB64 variant copy). NO event gate /
        // NO "type" read. curTime=_rootCurTime(+576), nextTime=_rootNextTime(+584).
        // Inert for logo (priority is single-clip → count<2 → loop never runs).
        void seekRootContentStreamLike_0x6B6ADC(double targetTime);
        // libkrkr2.so var-track stream ③ (the 3rd of [layer → root → var-track →
        // node] inside Player_advanceRootAndNodes 0x6B6ADC). Advances each
        // VariableLabelScope's two 56B slots so they bracket clampedEvalTime, via
        // the inlined step (sub_6B786C) + merge (sub_6B7A70). Inert for every
        // currently-available motion (none expose a populated "variable" list).
        void advanceVariableTracksLike_0x6B6ADC(double clampedEvalTime);
        // libkrkr2.so var-track stream ③ REVERSE form — the backward-play
        // counterpart inside Player_rewindRootAndNodes (0x6B9A3C, var-track loop
        // 0x6B9FCC..0x6BA038). Same two-slot bracketing as the forward stepper
        // but: (a) the inner loop steps while the ACTIVE slot's time > target
        // (vs forward's other.time < target), (b) it steps the OTHER slot to
        // (active.frameIndex - 1) — the decrement direction (vs forward's +1),
        // and (c) the post-loop merge writes slot[0] then slot[1] (vs forward's
        // slot[0] twice). Inert for every currently-available motion (none
        // expose a populated "variable" list).
        void rewindVariableTracksLike_0x6B9A3C(double clampedEvalTime);
        // libkrkr2.so var-track reseed inside Player_reseekTimelineCursors
        // (0x6B86C8, var-track block 0x6B8F30..0x6B8F60). The non-incremental
        // re-seed form: per track, scans its keyframe list to k, clamps
        // v41=min(k,count-2), then steps+merges slot[0] to v41 and slot[1] to
        // v41+1 (both slots fresh — NOT the active/other toggle), and resets the
        // activeSlotCursor (item+8) to 0. Called from the firstFrame seed and the
        // two loop-wrap reseek points. Inert for every currently-available motion.
        void reseedVariableTracksLike_0x6B86C8(double clampedEvalTime);
        // libkrkr2.so Player_reseekTimelineCursors @0x6B86C8 — the NON-incremental
        // full re-seek of all timeline cursors to targetTime (= player+456). Unlike
        // the incremental advance/rewindRootAndNodes seeks, this rescans each stream
        // FROM SCRATCH: (1) the layer coarse scan @0x6B8770 over motion["tag"] with
        // a DOUBLE-INCREMENT + int-truncated curTime/nextTime + the +1093-gated
        // align/sync + ungated action gate keyed on the CURSOR frame; (2) the root
        // single-step re-seek @0x6B8C1C over motion["priority"] (+616 content
        // snapshot, NON-truncated curTime); (3) the var-track reseed @0x6B8F30
        // (reuses reseedVariableTracksLike_0x6B86C8). The per-node init loop
        // @0x6B91B0 (step 4) is performed by the progressSeekNodeSlotsLike node
        // walk the caller keeps right after; the HM3-prune + aux-list tail
        // (0x6B9234/0x6B9248, step 5) is DEFERRED. Called at the firstFrame seed
        // (0x6C10E0/0x6C131C) + the two loop-wrap reseek points (0x6C1488/0x6C1428).
        void reseekTimelineCursors(double targetTime);
        // libkrkr2.so Player_interpolateVarTrackValues @0x6BBE20 — the var-track
        // item+16 writer (the value HM4 caches). Called at the start of
        // Player_resetMotionState_clearAndRebuild before its loop2 snapshots
        // item+16 → HM4. Per VariableLabelScope: HOLD or LERP between the
        // active(prev)/other(next) slot brackets, t interval-quantized + cubic-
        // bezier eased (Player_applyBezierEasing @0x69A754). Currently unwired
        // (the port has no resetMotionState yet); lands the value computation.
        void interpolateVarTrackValuesLike_0x6BBE20(double clampedEvalTime);
        // libkrkr2.so Player_resetMotionState_clearAndRebuild @0x6B2D3C, called
        // by playImpl when (flags & 8 == PlayFlagJoin). Body-gated on !_queuing
        // (+480). SLICE: clearHM3_HM4 + interpolateVarTrackValues + loop2
        // (item+16 → HM4 @ _variableSnapshotMap). loop1 (node evaluateTimeline)
        // and loop3 (HM3 perNodeLayerState via HM3_initValueFromNode @0x699510,
        // unported) are DEFERRED.
        void resetMotionStateLike_0x6B2D3C();
        // libkrkr2.so Player_HM3_initValueFromNode @0x699510 — snapshots node /
        // active-slot fields into a PerNodeLayerState (HM3 value). PARTIAL: only
        // nodeType maps to an existing MotionNode member; the other ~24 fields
        // read raw node byte offsets the port does not expose and are DEFERRED
        // (HM3 / _perNodeLayerStateMap has no consumer, so the snapshot is unread).
        void hm3InitValueFromNodeLike_0x699510(
            const detail::MotionNode &node, detail::PerNodeLayerState &v) const;
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
        // A10: read-only public accessors used by the differential test
        // harness (tests/differential/wasmtime/motion_playback_wasmtime.cpp).
        // Previously the harness reached these via the dropped runtime()
        // accessor and `_runtime->X` chains; now it goes through Player.
        [[nodiscard]] const std::shared_ptr<detail::MotionSnapshot> &
        activeMotion() const { return _activeMotion; }
        [[nodiscard]] const std::deque<detail::MotionNode> &nodes() const {
            return _nodes;
        }
        [[nodiscard]] const std::vector<detail::PreparedRenderItem> &
        preparedRenderItems() const { return _preparedRenderItems; }
        [[nodiscard]] const std::vector<detail::PreparedRenderItem *> &
        preparedRenderItemsTopLevel() const {
            return _preparedRenderItemsTopLevel;
        }
        [[nodiscard]] const std::vector<detail::PreparedRenderItem *> &
        preparedRenderItemsGroup() const {
            return _preparedRenderItemsGroup;
        }

        // A8 / A9: temporary mutable accessors for hoisted storage used by
        // anonymous-namespace helpers or by free functions in
        // motion::internal::render_detail:: which we can't friend across TU
        // boundaries cheaply. Marked for A10 cleanup review.
        std::deque<detail::MotionNode> &nodesForBuild() { return _nodes; }
        detail::NodeLabelMap &nodeLabelMapForBuild() {
            return _nodeLabelMap;
        }
        std::unordered_map<int, detail::RenderItemNativeFieldLifetime> &
        renderItemNativeFieldLifetimeByNode() {
            return _renderItemNativeFieldLifetimeByNode;
        }

    private:
        ResourceManager _resourceManagerNative;
        Player *_parentPlayer = nullptr; // non-owning, for 0x6B1ABC lookup
        // libkrkr2.so +1092: 1-byte bool = the "preview" NCB property (getter
        // Player_getPreview reads *(u8*)(this+1092)). NOT completionType — the
        // Player-table off-by-one IDB symbol had mislabeled +1092 as
        // completionType. +1092 gates calcBounds (0x6c4030) and buildNodeTree
        // (0x6B43A4) node-type visibility. The real completionType is the +1144
        // int (_completionType field below).
        bool _preview = false;
        tTJSVariant _metadata;
        ttstr _chara;
        ttstr _motionKey;
        ttstr _outline;  // Aligned to libkrkr2.so +1032: ttstr
        // libkrkr2.so +1160: double. ctor (0x6CED30) a1[145]=0x3FF8000000000000=1.5;
        // getter sub_6D965C reads *(player+1160).
        double _priorDraw = 1.5;
        double _frameLastTime = 0.0;
        double _clampedEvalTime = 0.0; // player+456: min(_frameTickCount, totalFrames)
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
        // Emote direct-edit angle (player+464). Aligned with libkrkr2.so
        // Player_getAngleRad @0x6CD0C0: when _directEdit (player+482) is set the
        // angle source is *(double*)(player+464); otherwise it is the root node
        // angle (_nodes[0].delta.angle = node+1616). Written by the emote
        // direct-edit init path (PlayerUpdateChildMotion / PlayerUpdateParticles
        // case at 0x6C0058 — "player+464 = emote angle", not yet wired in the
        // web port, so this stays 0.0 until that path is ported). Read by the
        // hair/parts + bust spring step (EmoteEngine_stepHairParts/stepBust).
        double _emoteAngle = 0.0;  // player+464
        // Camera angle for stereovision (a1+472, sub_6BDA28 at 0x6BDC50)
        double _cameraAngle = 0.0;
        double _cameraPosX = 0, _cameraPosY = 0, _cameraPosZ = 0;
        double _cameraTargetX = 0, _cameraTargetY = 0, _cameraTargetZ = 0;
        bool _speed = true;           // Aligned to libkrkr2.so +1093: bool flag
                                      //   (NOTE: +1093 is NOT the speed
                                      //   multiplier — that is _speedMul@+1168
                                      //   below. progress_inner @0x6C106C reads
                                      //   the multiplier from +1168, not +1093.)
        double _frameTickCount = 0.0;
        // Binary Player+912: pixelateDivision, default 100 (cluster E §1 ctor).
        // Port previously misplaced as D3DEmoteModule static with default 1.
        int _pixelateDivision = 100;

        // === M1 stage P1: progress / frame-stepping core state fields ===
        // All byte-verified from Player_progress_inner @0x6C106C (this
        // conversation). DECLARED ONLY — not yet read/written by the live
        // PlayerFrameProgress path (that wiring is M1 stages P5/P6); these are
        // groundwork so the frame-stepping machine can be ported incrementally
        // without disturbing the currently-green logo differential.
        //   v3 = *(double*)(a1+1168); *(double*)(a1+592) = v3 * dt;   // entry
        //   *(BYTE*)(a1+483) = 0;                                     // entry
        //   if (a1+481) { seed +1120/+456; a1+481=0; reseek; return } // firstFrame
        //   LABEL_48: if (!*(BYTE*)(a1+480)) { a1+1120 += a1+592;     // gated advance
        //                                      a1+456 = min(a1+1120,a1+1128); }
        //   ... +1099 loopArmed / +609 reverseSeekFlag gate loop-wrap ...
        double _speedMul = 1.0;        // player+1168: speed multiplier (dt scale)
        double _deltaTime = 0.0;       // player+592 : _speedMul * dt (per-frame)
        bool   _firstFrame = false;    // player+481 : one-shot seed of +1120/+456
        bool   _motionCompleted = false; // player+483: cleared each progress entry;
                                         //   set mid-step to abort cooperatively
        // R2: +1099 is _allplaying (declared above at line 655). Port previously
        // had a separate `_loopArmed = false` here, but R2 spike confirmed binary
        // treats +1099 as a single boolean isPlaying byte (STRH 0x100 set, STRB
        // WZR clear; no bit-level RMW). progress_inner @0x6C13F4/0x6C1384 STRB
        // WZR is "stop playing" semantics, not "disarm loop". Field merged.
        bool   _reverseSeekFlag = false; // player+609: one-shot reverse-seek request
        // --- 砖5/洞3: layer + root frame stream cursors (DECLARED ONLY) ---
        // The two player-level frame streams Player_advanceRootAndNodes
        // (0x6B6ADC) / Player_rewindRootAndNodes (0x6B9A3C) /
        // Player_reseekTimelineCursors (0x6B86C8) walk. Stream sources are
        // _activeMotion->tagFrames (layer = motion["tag"], Player+1072) and the
        // priority clip array (root = motion["priority"], Player+548); both
        // assigned by Player_initNonEmoteMotion @0x6B3778/0x6B37D0. The layer
        // stream is the global onAction/onSync source (type==1 frames, gated by
        // the +1093 stop-gate); the root stream only snapshots content into
        // _rootContent. NOT yet read by the live frameProgress path — that is
        // the next (behavioral) commit, which replaces scanLayerActions.
        // See analysis/Player_progress_frame_stepping_M1_plan.md §8.
        int    _layerFrameCursor = 0;  // player+916
        double _layerCurTime = 0.0;    // player+920: tag[cursor].time
        double _layerNextTime = 0.0;   // player+928: tag[cursor+1].time
        int    _rootFrameCursor = 0;   // player+568
        double _rootCurTime = 0.0;     // player+576: priority[cursor].time
        double _rootNextTime = 0.0;    // player+584: priority[cursor+1].time
        std::shared_ptr<const PSB::PSBDictionary> _rootContent; // player+616
        // 砖5/洞3: which tagFrames the layer cursor is currently valid for.
        // When _activeMotion->tagFrames changes (motion (re)loaded), the cursor
        // self-resets to 0 inside seekLayerEventStreamLike_0x6B6ADC. (The binary
        // resets via Player_reseekTimelineCursors on the firstFrame seed; this
        // pointer-identity guard reproduces that reset without coupling to the
        // motion-load site.)
        const void *_layerStreamSource = nullptr;
        // 砖G2: which priorityFrames the root cursor is currently valid for.
        // Same pointer-identity self-reset as _layerStreamSource (mirrors the
        // binary's Player_reseekTimelineCursors firstFrame seed resetting +568,
        // and the +616 = priority[0].content seed at 0x6B38FC).
        const void *_rootStreamSource = nullptr;
        // === end M1 P1 ===
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
        tjs_int _completionType = 0; // libkrkr2.so +1144: int (the real NCB
                                     // completionType value). Off-by-one had
                                     // mislabeled the +1092 bool (now _preview)
                                     // as completionType; +1144 is the int.
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
        // +612: post-draw snapshot of +613. The binary updateLayerAfterDraw
        // @0x6CE7F4 unconditionally copies +613 -> +612 each frame; anchor
        // type-10 (0x6C0528) gates on it ("internal render Layer was materialized
        // last frame") and reads its width/height as the per-player source size.
        bool _internalRenderLayerReady = false;

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
        // _variableLabelScopes: libkrkr2.so Player+1296 std::deque<VariableLabelScope>
        // (the var-track deque). Built by Player::initVariables (0x6CD750);
        // snapshotted into HM4 by resetMotionState loop2 (0x6B2D3C). Migrated
        // from the former std::vector<VariableLabelEntry> port model.
        // _nodeLabelMap: libkrkr2.so Player+24 std::map<ttstr,int>, keyed by the
        // RAW PSB "label" (NOT a hierarchical path). buildNodeTree_recursive
        // @0x6B4A6C inserts PropGet("label")'s raw return at 0x6B4CB0..0x6B4CB4
        // via Player_nodePathMap_lowerBoundInsert (no buildNodePathKey call;
        // xrefs_to(0x6B5C1C) shows the path builder feeds only HM3). Insert is
        // unconditional (no non-empty gate) → last-write-wins on label
        // collision. All reads (getLayerMotion/getLayerGetter @0x6B5AD8, dtgt
        // resolves @0x6F2228, stencil mask resolve @0x6B5454) feed the raw
        // query string verbatim, so the write key is raw to match. The
        // hierarchical "/top/.../self" path (Player_buildNodePathKey @0x6B5C1C)
        // is a SEPARATE key space used only by HM3 (_perNodeLayerStateMap).
        std::deque<detail::MotionNode> _nodes;
        detail::VariableLabelScopeDeque _variableLabelScopes;
        // Player+24 std::map<ttstr,int>; UTF-16 code-unit comparator sub_9B1ED0.
        detail::NodeLabelMap _nodeLabelMap;

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
        // libkrkr2.so player+760: persistent SeparateLayerAdaptor used by the
        // build-side requireLayerId materialization in sub_6C4E28 @ 0x6C5DBC.
        // Lazily created (Window.mainWindow.primaryLayer) the first time a
        // drawable render item reaches the LABEL_28 gate; owned by Player and
        // destroyed in ~Player (raw pointer + manual new/delete, matching the
        // binary's object lifetime). Aligned with sub_6C4E28 @0x6C5DBC.
        SeparateLayerAdaptor *_renderSeparateLayerAdaptor = nullptr;
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

        // M15 missing event callbacks (cluster E §3.1): port storage for
        // onAction/onSync/onGroundCorrection. Invocation pending binary
        // call-site spike.
        tTJSVariant _onAction;
        tTJSVariant _onSync;
        tTJSVariant _onGroundCorrection;
        // M15 missing transformOrder/coordinate (cluster E §3.1): port fields.
        // transformOrder spiked (2026-06-04): binary node+84..96 = 4-int
        // permutation of {0,1,2,3}; coordinate is still a scalar int (node+24).
        int _transformOrder[4] = {0, 1, 2, 3};
        tjs_int _coordinate = 0;
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
        // PLATFORM_BOUNDARY: legacy uniform record used by EmoteEngine's
        //   transitional 5 deques + 1 map. NOT a libkrkr2.so type — see
        //   internal/legacy_variable_state.h header note for the binary's
        //   actual 5 distinct POD element types.
        using VariableKeyframe       = detail::LegacyVariableKeyframe;
        using VariableAnimatorState  = detail::LegacyVariableAnimatorState;
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
        // === libkrkr2.so motion::Player hash maps (Phase B aliases) ===
        // NOTE (corrected 2026-06-03, fresh decompile of 0x686A4C/0x686C5C):
        // these are STANDARD libstdc++ std::unordered_map (literal
        // _Prime_rehash_policy::_M_need_rehash + textbook _M_rehash +
        // _M_before_begin single-chain + 1.0 load factor); only the hash
        // functor is custom (ttstr UTF-16 hash). The local std::unordered_map
        // selection is ALREADY the aligned container — there is NO inline /
        // open-addressing "KiriKiri HM" to migrate to. Do not rewrite these as
        // open-addressing maps. The only remaining container deviation is
        // key-type (std::string vs ttstr) on the two maps still keyed by
        // std::string; see _evalResultValues (HM2) and _nodeLabelMap.
        // HM1 (Player+264): cascaded PropGet result cache. Owns refcounts on
        // its embedded dispatch + chain via EvalCascadeState's destructor;
        // matches the binary's Player_HM1_value_destroy @0x6DD1A0 release
        // sequence. Empty until A8 wires it into the cascade evaluator.
        detail::EvalCascadeMap _evalCascadeMap;

        // HM3 (Player+1184): per-node-path layer state snapshot. KEY CONFIRMED
        // (Cluster F): the key is the node *path* ttstr produced by
        // Player_buildNodePathKey @0x6B5C1C. This is a SEPARATE key space from
        // the Player+24 node-index map (which is raw-label keyed); HM3 is the
        // ONLY consumer of the path builder (xrefs_to(0x6B5C1C) = 2 callers,
        // both HM3). Populated by Player_resetMotionState_clearAndRebuild
        // loop3 @0x6B2DF8: for each visible node whose type ∈ {0,2,3,7,8}
        //   key = Player_buildNodePathKey(player, nodeIndex)   // @0x6B2E08
        //   V   = Player_HM3_upsert_perNodeLayerState(player+1184, &key) //@0x6F2674
        //   Player_HM3_initValueFromNode(node, V)              // @0x699510
        // PerNodeLayerState owns 8 ttstr + 5 dispatch + 2 heap slots released in
        // binary dtor order (Player_HM3_value_destroy @0x6DD06C).
        //
        // DEFERRED (populate only — keying is already correct): the port has no
        // resetMotionState equivalent and no caller for it, and the value-fill
        // path Player_HM3_initValueFromNode @0x699510 is a 688-byte node→V
        // snapshot that is itself unported. Wiring HM3 populate here would mean
        // porting both functions wholesale, which is out of the node-tree
        // keying scope and has no consumer yet, so this map stays empty. When
        // resetMotionState is ported, build the key via
        // detail::buildNodePathKeyLike_0x6B5C1C(_nodes, nodeIndex) — that path
        // builder is HM3's own key generator (the Player+24 node-index map uses
        // the raw label, a different key space).
        detail::PerNodeLayerStateMap _perNodeLayerStateMap;

        // HM4 (Player+1240): ttstr -> double variable-snapshot cache.
        // Populated by Player_resetMotionState_clearAndRebuild's second loop
        // (binary 0x6B2D40: each active controller writes its raw 8B current
        // value keyed by controller-name ttstr). Read by Player::getVariable's
        // cascade as the first hit stop (HM4 → HM2 → HM1). Empty until that
        // resetMotionState path is wired in port.
        // (R-M4 spike-corrected: was annotated as `iTJSDispatch2*` alias map;
        // binary actually uses 8B raw double, identical layout to HM2.)
        detail::VariableSnapshotMap _variableSnapshotMap;

        // Aligned with libkrkr2.so motion::Player HM2 @ +320
        // (ttstr label -> double). Upsert helper: Player_HM2_upsert_labelToValue
        // @ 0x686944, which reads the key via ttstr_c_str() and hashes the
        // UTF-16 code units (1025*x ^ (1025*x>>6), then 9*acc, then
        // 32769*(h^(h>>11)), with the (uint32_t)-1 zero sentinel) — i.e. the
        // shared ttstr_hash. Cleared on motion change / reset alongside HM3/HM4.
        // Retyped (2026-06-03) from std::unordered_map<std::string,double> to
        // the ttstr-keyed detail::LabelValueMap so the key type, custom hash and
        // bucket distribution match the binary (was the only one of the four HMs
        // still keyed by std::string).
        detail::LabelValueMap _evalResultValues;
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
        // Per-frame flag cleared at end of updateLayers (player+608, 0x6BBDF8).
        // Set to true in constructor; checked by sub_6BE0C0 case 2 (0x6BE664)
        // and sub_6BEDD0 case 2 (0x6BEFF4). When true, case 2 falls through
        // to interpolated derivative path instead of using deltaPos.
        bool _noUpdateYet = true;  // player+608

        // NOTE: _emoteMeshDivisionRatio / _emoteMeshDivisionRatioDup MIGRATED
        // to EmoteEngine+1168/+1176 (per binary spec — these doubles live on
        // EmoteEngine, not Player). Callers now read/write via
        // _engineBack->_meshDivisionRatio[Dup].
        //
        // NOTE: hairScale/partsScale/bustScale are EmotePlayer/EmoteObject
        // properties (sub_681F20/28/30 write EmoteObject+1184/+1192/+1200),
        // NOT motion::Player fields.
        double _rotateAngle = 0.0;  // sub_672568 rotation parameter
        bool _physicsDisabled = false;   // player+1159
        bool _emoteAnimatorFlag = false; // player+1161
        // NOTE: _emoteDirty MIGRATED to EmoteEngine+1162 (per binary spec —
        // the byte lives on EmoteEngine, not Player). Callers now use
        // _engineBack->_dirty.
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

        // The wind emitter state was a functional-equivalent invention; it has
        //   been replaced by the faithful EmoteWindEmitter object (heap-owned at
        //   EmoteEngine+1128) plus the engine-side wind param cache
        //   (EmoteEngine::_windMin/_windMax/_windAmp/_windFreqX/_windFreqY).
        //   Player::startWind/stopWind populate them via _engineBack, matching
        //   Player_startWind_populate (sub_6709AC). See EmoteWindEmitter.{h,cpp}.

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
