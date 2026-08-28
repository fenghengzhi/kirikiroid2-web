//
// Created by LiDon on 2025/9/15.
// Reverse-engineered from the four current MotionPlayer reference binaries.
//
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <spdlog/spdlog.h>
#include "ComplexRect.h"
#include "tjs.h"
#include "ResourceManager.h"
#include "RuntimeSupport.h"
#include "internal/player_containers.h"

class iTVPTexture2D;
class tTJSNI_BaseLayer;
struct ncbPropAccessor;

namespace motion {
    class D3DAdaptor;
    class Player;
    class SeparateLayerAdaptor;
    class SourceCache;
}

namespace motion {
    namespace detail {
#if defined(KRKR2_WASMTIME_HEADLESS)
        struct LayerRenderState;
#endif
        struct MotionEvent;
        struct MotionNode;
        struct MotionParameterEntry;

        void buildNodeTree(motion::Player &player,
                           ncbPropAccessor &motionContent);
    }

    using D3DTargetTexturePair_guess =
        std::pair<iTVPTexture2D *, iTVPTexture2D *>;
    using D3DTargetTextureGetter_guess = std::function<
        D3DTargetTexturePair_guess(bool, const tTVPRect &)>;
    using D3DSourceTextureGetter_guess =
        std::function<iTVPTexture2D *(detail::PreparedRenderItem &)>;

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

    // All four current Motion root registrars publish these integer IDs. The
    // enum follows operation ID order; main.cpp preserves the native script
    // publication order Flip, Slant, Zoom, Angle.
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

    // Forward-declare helpers that require access to Player-owned runtime
    // containers. Definitions live inline in PlayerInternal.h.
    namespace internal {
        // Source-shaped non-chain ordinary-motion clock seed. Keeping the
        // literal zero as the first std::min operand is observable for NaN and
        // equal signed zeros on targets that retain the ordered comparison.
        double initialNonChainEvaluationTime_guess(double totalFrames);
        detail::MotionParameterEntry *selectParameterEntry_guess(
            Player &, const tTJSVariant &);
        bool evaluateTimeline_guess(
            detail::MotionNode &, double, bool);
        void initializeNodeTimelineSlots_guess(
            Player &, detail::MotionNode &);
        void seekParameterizedNodeFrames_guess(
            detail::MotionNode &, Player &);
        void stepVariableTrackSlot_guess(
            detail::VarTrackSlot &, const tTJSVariant &, std::uint32_t);
        void mergeVariableTrackSlot_guess(
            detail::VarTrackSlot &, const tTJSVariant &);
        void parseNodeFrame_guess(
            detail::MotionNode::ClipSlot &, const tTJSVariant &, int);
        void mergeNodeFrameContent_guess(
            detail::MotionNode::ClipSlot &, int, const tTJSVariant &);
    }

    class Player {
    public:
        // The four current constructors take one ResourceManager dispatch and
        // CopyRef it into three independent Variant owners: findSource,
        // render SourceCache, and the canonical script-visible owner. Player
        // never owns another native ResourceManager by value; nativeRM()
        // unwraps the retained dispatch. The same dispatch flows through
        // EmoteEngine and is copied into child Players. Parent/root links are
        // assigned after child construction, not passed as constructor inputs.
        explicit Player(const tTJSVariant &rmDispatch = tTJSVariant{});
        ~Player();

        // --- Properties (getter/setter) ---
        // Four-reference Player completionType is an unvalidated Int32. It is
        // adjacent to maskMode in every native layout; preview is a separate,
        // earlier Boolean byte.
        void setCompletionType(tjs_int v) { _completionType = v; }
        [[nodiscard]] tjs_int getCompletionType() const { return _completionType; }

        // Four-reference typed-property setter. The by-value NCB entry feeds a
        // shared flag-bearing coordinator: a real primary-chara change writes
        // both live chara slots, clears both live motion labels and clears
        // playing. An equal UTF-16 value is a complete no-op.
        void setChara(ttstr v);
        ttstr getChara() const { return _chara; }

        void setMotion(ttstr v);
        ttstr getMotion() const { return _motionKey; }

        // motionKey and project are literal read/write aliases over one
        // persistent Variant. NCB materializes the script argument by value;
        // the native-shaped inner setter then uses Variant copy assignment.
        void setMotionKey(tTJSVariant v) {
            setMotionContextVariant_guess(v);
        }
        void setMotionContextVariant_guess(const tTJSVariant &v) {
            _findMotionContextVariant = v;
        }
        tTJSVariant getMotionKey() const { return _findMotionContextVariant; }

        // Persistent render-style Variant. The setter preserves the incoming
        // TJS type and owner; the by-value getter returns an independent
        // CopyRef. Canvas outline primitives consume temporary copies.
        void setOutline(tTJSVariant v) { _outline = v; }
        tTJSVariant getOutline() const { return _outline; }

        // Player-level render gate, independent from node-level priorDraw.
        void setPriorDraw(bool v) { _priorDraw = v; }
        bool getPriorDraw() const { return _priorDraw; }

        // Raw frame-domain values loaded from motion["lastTime"] and
        // motion["loopTime"]. Their script properties are read-only.
        double getFrameLastTime() const { return _cachedTotalFrames; }
        double getFrameLoopTime() const { return _loopTime; }
        void setLoopTime(double v) { _loopTime = v; }

        // The read-only `variableKeys` member constructs a fresh TJS Array from
        // VariableLabelScope::cascadeKey values on every call. It preserves
        // deque order, duplicate keys and empty strings without normalization.

        // Local per-frame count plus the children reached by the shared
        // type-4/type-3 visitor. Its native particle-index-zero repetition is
        // preserved, so a type-4 count may add the first child more than once
        // rather than visiting every numeric element. Both accumulation
        // levels intentionally use uint32 wraparound.
        std::uint32_t getProcessedMeshVerticesNum() const;

        void setQueuing(bool v) { _queuing = v; }
        bool getQueuing() const { return _queuing; }

        void setDirectEdit(bool v) { _directEdit = v; }
        bool getDirectEdit() const { return _directEdit; }

        tTJSVariant getVariableKeys();

        void setAllplaying(bool v) { _allplaying = v; }
        bool getPlaying() const;
        bool getAllplaying() const;

        void setSyncWaiting(bool v) { _syncWaiting = v; }
        bool getSyncWaiting() const { return _syncWaiting; }

        void setSyncActive(bool v) { _syncActive = v; }
        bool getSyncActive() const { return _syncActive; }

        // Resource-shape query: scans the complete node deque for any type-5
        // node, regardless of its active state. This is intentionally distinct
        // from cameraAlive, which reads the per-frame CameraNode result byte.
        bool getHasCamera() const;

        // Enables CameraNode publication of FOV, position/target and angle.
        // It is independent from stereovisionActive, which gates only the
        // later prepared-item perspective pass.
        void setCameraActive(bool v) { _cameraActive = v; }
        bool getCameraActive() const { return _cameraActive; }

        void setStereovisionActive(bool v) { _stereovisionActive = v; }
        bool getStereovisionActive() const { return _stereovisionActive; }

        // Fresh Dictionary: unordered AABBs expose only isValid=false;
        // ordered AABBs also expose left/top/right/bottom/width/height.
        tTJSVariant getBounds() const;

        // All four Player member tables bind this to one per-Player double.
        // The EmotePlayer and D3DEmotePlayer properties traverse to this same
        // field; none of the three setters validates the value or updates the
        // EmoteEngine's independent metadata/controller scale pair.
        double getMeshDivisionRatio() const;
        void setMeshDivisionRatio(double v);

        // Millisecond-domain aliases. Positive frame values convert at 60 fps;
        // negative values, either zero, and NaN are returned unchanged.
        double getLastTime() const {
            return _cachedTotalFrames > 0.0
                       ? _cachedTotalFrames * 1000.0 / 60.0
                       : _cachedTotalFrames;
        }
        double getLoopTime() const {
            return _loopTime > 0.0 ? _loopTime * 1000.0 / 60.0 : _loopTime;
        }

        // Both public cursor setters commit the same complete state transition:
        // clamp negative frames to zero, retain the raw cursor, cap the
        // evaluation cursor only on an ordered `cursor > totalFrames`, and set
        // the adjacent queuing/first-frame bytes.  The ordered comparison is
        // significant for NaN and equal signed-zero operands.
        void setTickCount(double v) {
            setFrameCursorState_guess(v * 60.0 / 1000.0);
        }
        // Conversion is unconditional: negative zero, NaN and infinities flow
        // through the multiply-then-divide sequence without a positive guard.
        double getTickCount() const { return _frameTickCount * 1000.0 / 60.0; }

        // The script speed property is the double multiplier that already
        // drives `_deltaTime = _speedMul * dt`. It is not the independent
        // `_syncActive` align/sync gate; there is no separate speed-flag field.
        void setSpeed(double v) { _speedMul = v; }
        double getSpeed() const { return _speedMul; }

        void setFrameTickCount(double v) { setFrameCursorState_guess(v); }
        double getFrameTickCount() const { return _frameTickCount; }

        // The four references expose this as a raw per-Player int32 property.
        // It is distinct from D3DEmoteModule::pixelateDivision: writes to one
        // object never update the other. The native Player field has no direct
        // consumer beyond construction and these accessors.
        void setPixelateDivision(int v) { _pixelateDivision = v; }
        int getPixelateDivision() const { return _pixelateDivision; }

        // Four-reference-audited direct root transform properties.
        bool getFlipX() const;
        void setFlipX(bool v);
        bool getFlipY() const;
        void setFlipY(bool v);
        double getSlantX() const;
        void setSlantX(double v);
        double getSlantY() const;
        void setSlantY(double v);
        double getZoomX() const;
        void setZoomX(double v);
        double getZoomY() const;
        void setZoomY(double v);
        bool getVisible() const;
        int getOpacity() const;

        // The script packed word crosses an R/B-swapping ABI boundary; the
        // retained field uses the renderer's native packed RGBA byte order.
        void setColorWeight(tjs_int v);
        tjs_int getColorWeight() const;

        // Four-reference raw Int32 property; Player forwards the complete value
        // to the downstream alpha-mask operation.
        void setMaskMode(tjs_int v);
        tjs_int getMaskMode() const;

        // The public typed-Boolean setter has deliberately asymmetric native
        // behavior: an unequal request dirties every node delta but does not
        // store the flag. Type-3 child initialization is the separate writer.
        void setIndependentLayerInherit(bool v);
        bool getIndependentLayerInherit() const { return _independentLayerInherit; }

        void setZFactor(double v);
        double getZFactor() const { return _zFactor; }

        // These two read-only properties construct a fresh three-real Array on
        // every query from the camera-node state retained by Player.
        tTJSVariant getCameraTarget() const;
        tTJSVariant getCameraPosition() const;

        double getCameraFOV() const { return _cameraFov; }
        bool getCameraAlive() const { return _hasCamera; }

        void setPreview(bool v) { _preview = v; }
        bool getPreview() const { return _preview; }

        void setOutsideFactor(double v) { _outsideFactor = v; }
        double getOutsideFactor() const { return _outsideFactor; }

        // Script-visible read-only owner. Returning by value CopyRefs the
        // canonical Variant, so a caller-retained alias survives Player
        // destruction without cloning or unwrapping the dispatch.
        tTJSVariant getResourceManager() const { return _resourceManager; }

        // The same coordinator receives the Stealth flag. While the live
        // stealth-chara slot is null it retains the value in an independent
        // pending owner; otherwise it updates only the live stealth slot and
        // then flushes that pending field by direct reference.
        void setStealthChara(ttstr v);
        ttstr getStealthChara() const { return _stealthChara; }

        // Routes through the same play wrapper as `motion`, with Stealth, or
        // queues an independent owner while the live stealth-chara is null.
        void setStealthMotion(ttstr v);
        ttstr getStealthMotion() const { return _stealthMotion; }

        // The read-only `tags` property returns an independent Variant owner
        // aliasing the persistent motion["tag"] frame stream. It does not
        // repeat the motion-property lookup or clone the frame container.
        tTJSVariant getTags() const { return _tagFrameSourceVariant; }

        void setProject(tTJSVariant v) { setMotionContextVariant_guess(v); }
        tTJSVariant getProject() const { return getMotionKey(); }

        // Four-reference typed-Boolean property over the persistent D3D mode
        // byte. A successful draw(D3DAdaptor) recognition also sets it true;
        // ordinary/SLA/empty-target draws never reset it. The value gates both
        // the shared-D3D draw route and the source-spec-1 atlas fast path.
        void setUseD3D(bool v) { _d3dDrawMode = v; }
        bool getUseD3D() const { return _d3dDrawMode; }

        // Second persistent render-style Variant used beside outline.
        void setMeshline(tTJSVariant v) { _meshline = v; }
        tTJSVariant getMeshline() const { return _meshline; }

        // --- Methods ---
        bool setDrawAffineTranslateMatrix(double m11, double m21,
                                          double m12, double m22,
                                          double m14, double m24);
        tTJSVariant getCameraOffset();
        void setCameraOffset(double x, double y);
        void modifyRoot();
        [[nodiscard]] bool getRootModified_guess() const;
        // Two unreferenced out-of-line bodies survive in each Android target:
        // a short copy that calls Player::random and a second optimizer-emitted
        // copy with that call inlined. Both iOS images dead-strip the range
        // helper while retaining Player::random for particle updates. It is not
        // in the Player NCB table.
        double randomInRange_guess(double minimum, double maximum);
        double random();

        // Resource management
        bool isExistMotion(ttstr name);
        // Layer-id allocation has no by-name Player route. Render admission
        // obtains a fresh id through dispatchRequireLayerId().
        void releaseLayerId(tjs_int id);

        // Drawing/rendering
        // Generic source lookup is a TJS dispatch boundary. Argument zero is
        // an independent copy of the persistent context Variant (without an
        // early string conversion); argument one is the requested path.
        tTJSVariant findSource(ttstr name);
        void draw(tTJSVariant target);
        // Direct D3DLayer listener route.
        void drawToTexture_guess(iTVPTexture2D *target, float x, float y);
        void frameProgress(double dt);

        // Viewport/display
        // Combined two-axis method; one difference rewrites both axes.
        void setFlip(bool x, bool y);
        void setOpacity(int v);
        void setVisible(bool v);
        // Combined two-axis method; one difference writes both axes.
        void setSlant(double x, double y);
        // Combined two-axis method; zero and negative values are accepted.
        void setZoom(double x, double y);
        // One required typed-NCB ttstr argument. A null ttstr handle (produced
        // by either Void or an empty String) disables filtering; otherwise the
        // ordered raw-label map contributes keys containing the case-sensitive
        // UTF-16 substring.
        tTJSVariant getLayerNames(ttstr filter);
        void releaseSyncWait();
        void calcViewParam(double frame, tTJSVariant viewParams);
        // NCB passes the Variant by value. The body converts it to one temporary
        // ttstr, recursively resolves the raw label, destroys that temporary,
        // then copies the resolved node's child-player Variant.
        tTJSVariant getLayerMotion(tTJSVariant name);
        // NCB passes the ttstr by value. The returned LayerGetter borrows the
        // resolved MotionNode and reads it live; it does not own the node.
        tTJSVariant getLayerGetter(ttstr name);
        // Returns a fresh Array containing one getter for every flat non-root
        // node, in deque order. Duplicate labels are not collapsed.
        tTJSVariant getLayerGetterList();
        void skipToSync();
        void setStereovisionCameraPosition(double x, double y, double z);

        // Motion.Player's script getter reads only the bound-parameter
        // cascade (HM1/HM2). The EmoteEngine facade owns the separate
        // scope/snapshot router used by EmotePlayer and D3DEmotePlayer.
        double getVariable(ttstr label);
        // Writes HM2 unconditionally and, for a splittable key, rebuilds and
        // applies the HM1 cascade before updating the own parameter ramps.
        void bindParameterValue_guess(const ttstr &key, int mode,
                                      double value);
        // Folds matching parameter intervals through this Player and every
        // child reached by the shared type-4/type-3 visitor. A Dictionary is
        // published only for ordered min < max; missing, single-point and
        // unordered final extrema are represented by Void.
        tTJSVariant getVariableRange_guess(ttstr label);
        // Misc
        tTJSVariant getCommandList();
        // getD3DAvailable / doAlphaMaskOperation are Motion namespace-level
        // free functions, not Motion.Player methods.
        // Script-overridable callback. Its native default copies the by-value
        // request Variant back unchanged.
        tTJSVariant onFindMotion(tTJSVariant request);
        // The public play layer and playImpl borrow the request label. The
        // native callers pass a ttstr slot address directly; ownership is
        // copied only when the request is queued or forwarded to the by-value
        // load helper.
        void playMotion_guess(tjs_int flags, const ttstr &label);
        // Native zero-argument void method. The typed NCB wrapper clears its
        // result slot and accepts surplus arguments before this body writes
        // only the Player-level playing byte.
        void stop();
        // The four progress bridges take (player,currentDispatch,frameDt),
        // store the raw dispatch pointer for the call, and run frameProgress ->
        // updateLayers -> calcBounds -> dispatchPendingEvents before clearing
        // the pointer. Engine progress passes nullptr and frame units; the raw
        // script callback converts milliseconds before entering the bridge.
        // Neither bridge consumes the event vector, and an exceptional phase
        // leaves the raw pointer installed.
        void progressFrames_guess(iTJSDispatch2 *currentDispatch,
                                  double frameDt);
        static tjs_error playCompat(tTJSVariant *result, tjs_int numparams,
                                    tTJSVariant **param, iTJSDispatch2 *objthis);
        static tjs_error progressCompatMethod(tTJSVariant *result,
                                              tjs_int numparams,
                                              tTJSVariant **param,
                                              iTJSDispatch2 *objthis);
        static tjs_error setVariableCompatMethod(tTJSVariant *result,
                                                 tjs_int numparams,
                                                 tTJSVariant **param,
                                                 Player *nativeInstance);
        // Script member "clear" binds this as a typed two-Variant method. It
        // supports D3DAdaptor and SeparateLayerAdaptor targets in addition to
        // an ordinary Layer. Values are owned per recursive call, matching the
        // native Variant copies made before descending into a type-3 child.
        void drawToLayerRecursive_guess(tTJSVariant targetLayer,
                                        tTJSVariant fillValue);
        // Motion.Player exposes degree and radian views of one degree-valued
        // state. Direct-edit selects `_emoteAngle`; ordinary mode selects root
        // zero. The radian setter converts and delegates to the degree setter.
        double getAngleDeg() const;
        void setAngleDeg(double deg);
        double getAngleRad() const;
        void setAngleRad(double rad);

        // Internal facade used by EmotePlayer/D3DEmotePlayer.  It consumes the
        // current geometry state; it does not load or advance the motion.
        bool hitTestLayerByRawLabel_guess(const ttstr &name, double x, double y);
        // Coordinate-only hit test: local non-root shape nodes first, followed
        // by child/particle Players in native visitor order.
        bool contains(double x, double y);

        // Function-kind methods in all four references. onAction/onSync are
        // empty defaults. onGroundCorrection is the identity fallback used by
        // layer evaluation: it returns the current-position Array in arg0 and
        // ignores the parent-position Array in arg1.
        void onAction() {}
        void onSync() {}
        tTJSVariant onGroundCorrection(tTJSVariant currentPosition) {
            return currentPosition;
        }

        // Both properties are direct views of the constructor-created root
        // node. transformOrder is a four-integer TJS Array permutation; its
        // setter writes incrementally and dirties the root when a value changes.
        // coordinate is the raw root coordinateMode integer and does not dirty
        // the transform delta block.
        void setTransformOrder(tTJSVariant arr);
        [[nodiscard]] tTJSVariant getTransformOrder() const;
        void setCoordinate(tjs_int v);
        [[nodiscard]] tjs_int getCoordinate() const;

        // Class-level RW properties backed by process-global state rather than
        // per-instance fields. All four references use a one-byte false default
        // for syncActive and the integer order {0,3,2,1}.
        [[nodiscard]] static bool getDefaultSyncActive() {
            return s_defaultSyncActive;
        }
        static void setDefaultSyncActive(bool v) { s_defaultSyncActive = v; }
        // The getter returns a new Array. The setter reads a four-element
        // permutation with required indexed lookups and writes incrementally.
        [[nodiscard]] static tTJSVariant getDefaultTransformOrder();
        static void setDefaultTransformOrder(tTJSVariant arr);

        // Root-node position. left aliases x and top aliases y in every
        // registrar; setters compare with ordinary `!=` and dirty on change.
        double getX() const;
        double getY() const;
        void setX(double v);
        void setY(double v);
        // If either axis differs, setCoord writes both axes before dirtying.
        void setCoord(double x, double y);
        double getLeft() const { return getX(); }
        double getTop() const { return getY(); }
        void setLeft(double v) { setX(v); }
        void setTop(double v) { setY(v); }

        // Internal node-construction hooks used by detail::buildNodeTree().
        // They are split at the PSB property read so C++ argument evaluation
        // cannot move that read before the two non-owning link stores performed
        // by the four-reference type-3 construction path.
        void linkType3ChildPlayer_guess(Player &child);
        void initializeType3ChildState_guess(
            Player &child, detail::MotionNode &node,
            bool independentLayerInherit);

        // Shared producers for the one persistent event vector. Layer timeline
        // events, node timeline events, and child aggregation all converge on
        // these records before the bridge dispatches them.
        void enqueueSyncEvent_guess();
        void enqueueActionEvent_guess(const tTJSVariant &param1,
                                      const ttstr &action);

    private:
        void setFrameCursorState_guess(double cursor) {
            if(cursor < 0.0) cursor = 0.0;
            _frameTickCount = cursor;

            double evaluationCursor = cursor;
            if(evaluationCursor > _cachedTotalFrames) {
                evaluationCursor = _cachedTotalFrames;
            }
            _clampedEvalTime = evaluationCursor;
            _queuing = true;
            _firstFrame = true;
        }

        // Both string arguments are native by-value owners. onFindMotion may
        // replace these local handles without changing the caller's labels.
        tTJSVariant loadMotionResult_guess(ttstr chara, ttstr motion);
        // Zero-xref Android residual retained for source-shape recovery. It
        // uses the final indeterminate raw-dispatch slot and a callback-local
        // Variant, then creates a distinct return slot for findMotion. No live
        // Player path or registered bridge calls this method.
        tTJSVariant tailDispatchLoadMotionResidual_guess(
            ttstr chara, ttstr motion);
        void dispatchPendingEvents_guess(iTJSDispatch2 *dispatch);
        void playMotionImpl_guess(const ttstr &label, tjs_int flags);
        void setCharaLiveSlots_guess(tjs_int flags, const ttstr &value);
        void setCharaWithFlags_guess(tjs_int flags, const ttstr &value);
        // Direct raw-label-map lookup, optionally followed by the shared
        // child/particle visitor. It does not reject an empty key or validate
        // the mapped deque index. Exact four-target mappings live in analysis/.
        detail::MotionNode *findNodeByRawLabel_guess(
            const ttstr &name, bool recursive);
        // Calls visitor in flat node order while re-reading deque.end() on each
        // condition. Type 3 contributes its single child; type 4 retains the
        // Array once and visits numeric element zero `count` times (a
        // four-reference shipped bug). false stops the complete walk.
        void visitChildPlayerDispatches_guess(
            const std::function<bool(Player *)> &visitor) const;
        void foldVariableRangeRecursive_guess(
            const ttstr &label, double &minValue, double &maxValue);
        // Ordinary initializer and the emote wrapper's secondary selector.
        void initNonEmoteMotion_guess(std::uint32_t playFlags);
        void initEmoteMotion_guess(std::uint32_t playFlags);
        // Called eagerly from ordinary initialization; there is no lazy gate.
        void buildNodeTree_guess();
        void resetAndReleaseOldNodeTree_guess();
        // Builds one VariableLabelScope per selected-motion `variable` item.
        // The deque append precedes all named-property reads, so getter failures
        // retain the partially initialized element. Scope is converted to a
        // ttstr unconditionally; only a non-empty result prefixes the label.
        void initVariables();
        friend void detail::buildNodeTree(motion::Player &player,
                                          ncbPropAccessor &motionContent);
        friend void detail::ensureRootNode_guess(motion::Player &);
        friend void detail::eraseNonRootNodesAndClearLabelMap_guess(
            motion::Player &);
        void appendParameterEntry_guess(const tTJSVariant &parameter);
        bool parseParameterList_guess(const tTJSVariant &parameters);
        void finalizeParameterTable_guess();
        void purgeParameterRampMap_guess();
        double readInitialParameterValue_guess(const ttstr &id) const;
        // Rebuild one HM1 entry's cached type-3/type-4 node matches. The scratch
        // chain intentionally persists across candidates, as in all references.
        void rebuildEvalCascadeEntry_guess(
            detail::EvalCascadeState &entry);
        void renderToCanvas_guess(
            tTJSVariant target,
            detail::PreparedRenderItemList &mainList,
            detail::PreparedRenderItemList &auxList);
        void renderToSeparateLayerAdaptor(SeparateLayerAdaptor *sla);
        void renderToD3DAdaptor(D3DAdaptor *adaptor);
        void renderViaSharedD3DAdaptor(
            const tTJSVariant &target,
            detail::PreparedRenderItemList &mainList);
        int buildPrivateMotionGLLCommands_guess(
            tTJSNI_BaseLayer *renderLayer,
            tjs_int canvasWidth,
            tjs_int canvasHeight,
            detail::PreparedRenderItemList &mainList,
            detail::PreparedRenderItemList &auxList);
        void renderAccurateSeparateLayerAdaptor_guess(
            SeparateLayerAdaptor *sla,
            detail::PreparedRenderItemList &mainList,
            detail::PreparedRenderItemList &auxList);
        // Accurate SLA refreshes the root-owned particle deleteOutside
        // viewport from the target rectangle before command construction.
        // It is independent of public calcBounds and of the builder clip.
        void computeParticleOutsideRect_guess(
            const std::array<float, 4> &targetRect);
        void calcBounds();
        void updateLayers();
        bool prepareRenderItems(
            detail::PreparedRenderItemList &mainList,
            detail::PreparedRenderItemList &auxList);
        void appendPreparedRenderItems(
            std::vector<detail::PreparedRenderItem *> &mainList,
            std::vector<detail::PreparedRenderItem *> &auxList,
            std::uint32_t inheritedColor,
            bool inheritedDrawFlag19,
            bool inheritedFlag18);
        // Post-prepare pass over the sorted main list: always adds the two
        // float camera offsets, then optionally applies stereovision depth
        // projection and rebuilds each projected paint box.
        void applyPreparedRenderItemProjection_guess(
            detail::PreparedRenderItemList &mainList);
        // Prepend the child's pending-event range, then clear the child while
        // retaining its vector capacity.
        void aggregateChildPendingEvents_guess(Player &child);
        void buildRenderCommands(
            detail::PreparedRenderItemList &mainList,
            detail::PreparedRenderItemList &auxList,
            const std::array<float, 4> &targetClip);
        // Materialize a per-item leaf Layer in the SeparateLayerAdaptor active
        // map, size it to the clip, and copy the resolved source. This belongs
        // to command building; the later execute pass only submits it.
        void emitPreparedLeafLayerCopy_guess(
            detail::PreparedRenderItem &item);
        // For each group item, union visible child paint boxes, intersect the
        // result with the caller's four-edge target clip, create/reuse the
        // composed layer when non-empty, then apply every visible leaf as an
        // alpha mask.
        void composePreparedGroupLayers_guess(
            detail::PreparedRenderItemList &auxList,
            const std::array<float, 4> &targetClip);
        void executeLayerRenderCommands(iTJSDispatch2 *layerClassObject,
                                        iTJSDispatch2 *renderLayerObject,
                                        tjs_int canvasWidth,
                                        tjs_int canvasHeight,
                                        bool skipUpdate,
                                        detail::PreparedRenderItemList &mainList);
        tTJSVariant resolveRenderSource_guess(
            const tTJSVariant &sourceObject);
        void materializeInternalRenderLayers_guess(
            const tTJSVariant &target);
        void updateLayerAfterDrawRecovered_guess(
            const tTJSVariant &target);
        void updateAccurateSLAAfterDraw(const tTJSVariant &target);
        void renderPreparedItemsToD3DTexture_guess(
            D3DAdaptor *adaptor,
            detail::PreparedRenderItemList &mainList);
        void renderPreparedItemsToD3DTexture_guess(
            iTVPTexture2D *targetTexture,
            const D3DTargetTextureGetter_guess &targetTextureGetter,
            const tTVPRect &targetRect,
            const D3DSourceTextureGetter_guess &sourceTextureGetter,
            detail::PreparedRenderItemList &mainList,
            float xOffset,
            float yOffset);
        // Extracted node phase of the progress-pass cursor machine. The native
        // forward/reverse incremental functions inline this phase after the
        // layer, root and variable streams. It fills each node's two parsed
        // frame slots; the separate updateLayers pass only reads those slots
        // through the timeline evaluator. This method hoists the live two-slot
        // seek out of updateLayers and into the progress pass, restoring the
        // native two-pass split. Both forward and reverse node walks are
        // connected.
        // `forward` selects the non-parameterized node's single-direction
        // inline seek. Ordinary loops reload the live Player evaluation field;
        // parameterized nodes use their shared bidirectional stepper.
        void seekNodeTimelineSlotsIncrementalPhase_guess(bool forward = true);
        // Equal-time/non-playing route: visit only parameter-bound non-root
        // nodes through the independent bidirectional stepper.
        void refreshParameterizedNodeTimelines_guess();
        // Full-reseek step 4: absolute two-slot re-seed of every non-root node to
        // its target-bracketing frame. This is independent of the prior cursor,
        // so the loop-wrap path's subsequent forward-only phase needs no
        // corrective backward pass.
        void reseedNodeTimelineSlots_guess();
        // Forward four-stream walk: layer, root content, variable tracks, then
        // node timelines. The native member takes only `this` and reads the
        // current evaluation cursor from Player.
        void advanceTimelineStreams_guess();
        // Reverse counterpart with the same this-only ABI and dedicated
        // reverse loops for all four streams.
        void rewindTimelineStreams_guess();
        // Per-node "modified" emoteEdit-dict check followed by an absolute
        // timeline rebuild. This is the first out-of-line frame-progress phase.
        // The script-owned object can publish the flag; this pass reads and
        // clears it through one retained dispatch owner and a dedicated hint.
        void refreshModifiedNodeTimelines_guess();
        // Source-level extracted phases of the two four-stream native
        // functions. The native code inlines these phases in the exact order
        // layer/tag -> root/priority -> variable tracks -> node timelines.
        void advanceLayerEventStreamPhase_guess(
            const tTJSVariant &tagFrameSourceOwner);
        void rewindLayerEventStreamPhase_guess(
            const tTJSVariant &tagFrameSourceOwner);
        void advanceRootContentStreamPhase_guess(
            const tTJSVariant &priorityFrameSourceOwner);
        void rewindRootContentStreamPhase_guess(
            const tTJSVariant &priorityFrameSourceOwner);
        // Forward variable phase: both post-loop branches inspect their own
        // merged flag but intentionally merge physical slot[0].
        void advanceVariableTracksPhase_guess();
        // Reverse variable phase: unsigned decrement and physical slot[0],
        // slot[1] merge order are preserved.
        void rewindVariableTracksPhase_guess();
        // Absolute bracketing used by first-frame and wrap reseeks. Empty or
        // single-frame lists preserve the native count-2 unsigned-index edge.
        void reseedVariableTracks_guess();
        // Four-reference clear helper: pre-invalidate the child Player retained
        // by each unconsumed type-3 snapshot and every child object retained by
        // each unconsumed type-4 particle array, then clear HM4 followed by HM3.
        // A matched snapshot has transferred and cleared those Variants before
        // erase, so only unmatched snapshots are invalidated by this pass.
        void clearJoinSnapshotMaps_guess();
        // Four-reference full-reseek STEP5 tail. Two gated loops followed by an
        // unconditional call to clearJoinSnapshotMaps_guess:
        //   loop1 (HM4 bucket!=0): restore each active var-track slot.value from
        //     the HM4 snapshot keyed by item.cascadeKey — FULLY PORTED.
        //   loop2 (HM3 elem!=0): prune+restore HM3 by node identity — per-node
        //     restore uses the joinTarget+nodeType gate, restores the complete
        //     common/type-3/type-4/mesh payload, optionally refreshes type-0
        //     source state, and erases the matched entry.
        void restoreAndPruneJoinSnapshots_guess();
        // Non-incremental full reseek. It rebuilds the layer, root, variable and
        // node timeline cursors from the beginning, restores/prunes join
        // snapshots, then calls rebuildEvalCascadeEntry_guess for every HM1
        // entry. The exact four-target mapping and ABI notes live in analysis/.
        // Native member takes only `this`; each observable comparison reloads
        // the live evaluation cursor from Player after the preceding script
        // getter. This matters when a dynamic frame source re-enters Player.
        void reseekTimelineCursors();
        // Four-target parity: this Player member reads the current evaluation
        // time from the object, interpolates each variable track, writes the
        // value cached by the join snapshot, and binds the live value.
        void interpolateVarTrackValues_guess();
        // Exact source name is not retained. Android armv7 and both iOS targets
        // keep this aggregate helper out of line; Android arm64 inlines it into
        // reset. It interpolates variables and evaluates every non-root node.
        void evaluateTimelinesForJoinSnapshot_guess();
        // Called by playImpl for PlayFlagJoin and gated on !_queuing. All four
        // targets clear both snapshot maps, run the aggregate evaluation pass,
        // then rebuild the variable and per-node join snapshots.
        void resetMotionState_guess();
        // Select the active slot's separate src/icon values, then run the
        // native-shaped source resolver on the persistent node SourceState.
        // The resolver receives those strings as arguments rather than reading
        // Player slot state itself. ResourceManager owns atlas textures; nodes
        // keep borrowed texture pointers alongside their descriptor state.
        void findSourceForNode_guess(detail::MotionNode &node);
        // Shared KRKR atlas resolver used by both find-source and the
        // render-time texture getter. It mutates the supplied persistent
        // SourceState and borrows the ResourceManager/module key; it does not
        // create a second Player-owned atlas cache.
        static bool loadKrkrAtlasSource_guess(
            detail::MotionNode::SourceState &source,
            ResourceManager *resourceManager,
            const ttstr &moduleKey);
        // updateLayers sub-phases. Current four-reference address maps live in
        // analysis/ rather than portable declarations.
        void updateLayersPhase1_PreLoop(double currentTime);
        void updateLayersPhase2_MainLoop(double currentTime);
        void updateLayersPhase3_CameraConstraint();
        void updateLayersPhase3_VertexComputation();
        void updateLayersPhase3_Visibility();
        void updateLayersPhase3_CameraNode();
        void updateLayersPhase3_ShapeAABB();
        void updateLayersPhase3_ShapeGeometry();
        void updateLayersPhase3_MotionSubNode();
        void updateLayersPhase3_ParticleEmitter();
        void updateEmitterCrossfadeDelta_guess(detail::MotionNode &emitter);
        void updateLayersPhase3_ParticleSystem();
        void stepParticleChildren_guess(detail::MotionNode &particleNode);
        void updateLayersPhase3_AnchorNode();

    public:
        // A10: read-only public accessors used by the differential test
        // harness (tests/differential/wasmtime/motion_playback_wasmtime.cpp).
        // Previously the harness reached these via the dropped runtime()
        // accessor and `_runtime->X` chains; now it goes through Player.
        // These fields are the binary's sole loaded-content and
        // matched-resource-key owners.
        [[nodiscard]] bool hasMotionContent() const {
            return _motionContentVariant.Type() != tvtVoid;
        }
        // Test-only entry for the native-shaped node source resolver. It is
        // not registered as a Motion.Player script member.
        void findSourceForDifferentialTest_guess(detail::MotionNode &node) {
            findSourceForNode_guess(node);
        }
        // Test-only observation of the internal evaluation cursor. This is not
        // registered as a Motion.Player script property.
        [[nodiscard]] double evaluationFrameForDifferentialTest_guess() const {
            return _clampedEvalTime;
        }
        // Test-only observation of the scaled frame delta committed at the
        // beginning of frameProgress. It distinguishes the raw progress
        // callback's native multiply-then-divide millisecond conversion from a
        // pre-folded 0.06 multiplication.
        [[nodiscard]] double deltaTimeForDifferentialTest_guess() const {
            return _deltaTime;
        }
        // Test-only injection for skipToSync's pre-loop snapshot, re-entrant
        // tag traversal, and IEEE-754 boundary tests. It is not registered as
        // a Motion.Player script member.
        void setSkipToSyncStateForDifferentialTest_guess(
            double lastTime, const tTJSVariant &tags) {
            _cachedTotalFrames = lastTime;
            _tagFrameSourceVariant = tags;
        }
        struct RootReseekObservationForDifferentialTest_guess {
            int cursor;
            double currentTime;
            double nextTime;
            tTJSVariant content;
        };
        // Test-only projection of full absolute reseek's root-priority phase.
        // Empty Player variable/node/maps make the later phases inert, while
        // the supplied tag owner keeps the preceding phase independently
        // controllable. This is not a script-visible method.
        RootReseekObservationForDifferentialTest_guess
        reseekRootStreamForDifferentialTest_guess(
            const tTJSVariant &tags, const tTJSVariant &priority,
            double targetTime, int priorCursor) {
            _tagFrameSourceVariant = tags;
            _priorityFrameSourceVariant = priority;
            _rootFrameCursor = priorCursor;
            _clampedEvalTime = targetTime;
            reseekTimelineCursors();
            return {_rootFrameCursor, _rootCurTime, _rootNextTime,
                    _rootContentVariant};
        }
        struct TagReseekObservationForDifferentialTest_guess {
            int cursor;
            double currentTime;
            double nextTime;
            double evaluationTime;
            double frameTickCount;
            bool motionCompleted;
            bool syncWaiting;
            std::vector<detail::MotionEvent> events;
        };
        // Test-only projection of full absolute reseek's tag phase. The source
        // clear/evaluation setters below permit a dynamic count getter to expose
        // the native local-owner and live-Player-cursor boundaries.
        TagReseekObservationForDifferentialTest_guess
        reseekTagStreamForDifferentialTest_guess(
            const tTJSVariant &tags, const tTJSVariant &priority,
            double targetTime, bool syncActive) {
            _tagFrameSourceVariant = tags;
            _priorityFrameSourceVariant = priority;
            _clampedEvalTime = targetTime;
            _frameTickCount = targetTime;
            _syncActive = syncActive;
            _syncWaiting = false;
            _motionCompleted = false;
            _pendingEvents.clear();
            reseekTimelineCursors();
            return {_layerFrameCursor, _layerCurTime, _layerNextTime,
                    _clampedEvalTime, _frameTickCount, _motionCompleted,
                    _syncWaiting, _pendingEvents};
        }
        void clearTagFrameSourceForDifferentialTest_guess() {
            _tagFrameSourceVariant.Clear();
        }
        void clearPriorityFrameSourceForDifferentialTest_guess() {
            _priorityFrameSourceVariant.Clear();
        }
        void setEvaluationFrameForDifferentialTest_guess(double value) {
            _clampedEvalTime = value;
        }
        struct IncrementalStreamObservationForDifferentialTest_guess {
            int layerCursor;
            double layerCurrentTime;
            double layerNextTime;
            int rootCursor;
            double rootCurrentTime;
            double rootNextTime;
            tTJSVariant rootContent;
            double evaluationTime;
            double frameTickCount;
            bool motionCompleted;
            bool syncWaiting;
            bool tagSourceCleared;
            bool prioritySourceCleared;
            std::vector<detail::MotionEvent> events;
        };
        // Test-only setup for the two complete incremental four-stream members.
        // Fresh Players have no variable tracks or nodes, isolating the leading
        // tag/root phases while retaining the aggregate owners and tail order.
        void configureIncrementalStreamsForDifferentialTest_guess(
            const tTJSVariant &tags, const tTJSVariant &priority,
            double evaluationTime, int layerCursor,
            double layerCurrentTime, double layerNextTime,
            int rootCursor, double rootCurrentTime, double rootNextTime,
            bool syncActive) {
            _tagFrameSourceVariant = tags;
            _priorityFrameSourceVariant = priority;
            _clampedEvalTime = evaluationTime;
            _frameTickCount = evaluationTime;
            _layerFrameCursor = layerCursor;
            _layerCurTime = layerCurrentTime;
            _layerNextTime = layerNextTime;
            _rootFrameCursor = rootCursor;
            _rootCurTime = rootCurrentTime;
            _rootNextTime = rootNextTime;
            _rootContentVariant.Clear();
            _syncActive = syncActive;
            _motionCompleted = false;
            _syncWaiting = false;
            _pendingEvents.clear();
            _variableLabelScopes.clear();
        }
        void advanceIncrementalStreamsForDifferentialTest_guess() {
            advanceTimelineStreams_guess();
        }
        void rewindIncrementalStreamsForDifferentialTest_guess() {
            rewindTimelineStreams_guess();
        }
        [[nodiscard]] IncrementalStreamObservationForDifferentialTest_guess
        observeIncrementalStreamsForDifferentialTest_guess() const {
            return {_layerFrameCursor, _layerCurTime, _layerNextTime,
                    _rootFrameCursor, _rootCurTime, _rootNextTime,
                    _rootContentVariant, _clampedEvalTime, _frameTickCount,
                    _motionCompleted, _syncWaiting,
                    _tagFrameSourceVariant.Type() == tvtVoid,
                    _priorityFrameSourceVariant.Type() == tvtVoid,
                    _pendingEvents};
        }
        // Test-only entry for the ordinary/parameterized node tail of the two
        // complete incremental four-stream members.
        void seekNodeTimelineSlotsIncrementalForDifferentialTest_guess(
            bool forward) {
            seekNodeTimelineSlotsIncrementalPhase_guess(forward);
        }
        struct VariableReseedObservationForDifferentialTest_guess {
            int cursor;
            std::uint32_t firstIndex;
            std::uint32_t secondIndex;
            double firstTime;
            double secondTime;
            bool firstMerged;
            bool secondMerged;
            bool firstTypeZero;
            bool secondTypeZero;
            bool persistentSourceCleared;
        };
        // Test-only projection of the inlined variable-track absolute phase.
        // Its dynamic source may clear both this track's persistent owner and
        // the caller's owner; the phase-local copy must remain valid through
        // both slot merges and the final cursor reset.
        VariableReseedObservationForDifferentialTest_guess
        reseedVariableTrackForDifferentialTest_guess(
            const tTJSVariant &frameSource, double targetTime) {
            _variableLabelScopes.clear();
            auto &item = _variableLabelScopes.emplace_back();
            item.frameSource = frameSource;
            _clampedEvalTime = targetTime;
            reseedVariableTracks_guess();
            return {item.activeSlotCursor,
                    item.slot[0].frameIndex, item.slot[1].frameIndex,
                    item.slot[0].time, item.slot[1].time,
                    item.slot[0].merged, item.slot[1].merged,
                    item.slot[0].typeZeroFlag,
                    item.slot[1].typeZeroFlag,
                    item.frameSource.Type() == tvtVoid};
        }
        void clearFirstVariableFrameSourceForDifferentialTest_guess() {
            if(!_variableLabelScopes.empty()) {
                _variableLabelScopes.front().frameSource.Clear();
            }
        }
        struct VariableIncrementalObservationForDifferentialTest_guess {
            int cursor;
            std::uint32_t firstIndex;
            std::uint32_t secondIndex;
            double firstTime;
            double secondTime;
            bool firstMerged;
            bool secondMerged;
            bool persistentSourceCleared;
            double evaluationTime;
        };
        // Test-only setup/entry points for the inlined forward and reverse
        // variable-track phases. Keeping setup separate from execution makes
        // cursor and slot prefixes observable after a re-entrant exception.
        void configureVariableTrackIncrementalForDifferentialTest_guess(
            const tTJSVariant &frameSource, double targetTime, int cursor,
            std::uint32_t firstIndex, double firstTime, bool firstMerged,
            std::uint32_t secondIndex, double secondTime, bool secondMerged) {
            _variableLabelScopes.clear();
            auto &item = _variableLabelScopes.emplace_back();
            item.frameSource = frameSource;
            item.activeSlotCursor = cursor;
            item.slot[0].frameIndex = firstIndex;
            item.slot[0].time = firstTime;
            item.slot[0].merged = firstMerged;
            item.slot[1].frameIndex = secondIndex;
            item.slot[1].time = secondTime;
            item.slot[1].merged = secondMerged;
            _clampedEvalTime = targetTime;
        }
        void advanceVariableTrackForDifferentialTest_guess() {
            advanceVariableTracksPhase_guess();
        }
        void rewindVariableTrackForDifferentialTest_guess() {
            rewindVariableTracksPhase_guess();
        }
        [[nodiscard]] VariableIncrementalObservationForDifferentialTest_guess
        observeVariableTrackIncrementalForDifferentialTest_guess() const {
            const auto &item = _variableLabelScopes.front();
            return {item.activeSlotCursor,
                    item.slot[0].frameIndex, item.slot[1].frameIndex,
                    item.slot[0].time, item.slot[1].time,
                    item.slot[0].merged, item.slot[1].merged,
                    item.frameSource.Type() == tvtVoid,
                    _clampedEvalTime};
        }
        // Test-only entry to the internal recursive AABB pass. It is never
        // registered as a Motion.Player script member.
        void calcBoundsForDifferentialTest_guess() { calcBounds(); }
        // Test-only replacement for the prepared-item builder's persistent
        // priority-content owner. It is not a Motion.Player script member.
        void setRootContentForDifferentialTest_guess(
            const tTJSVariant &content) {
            _rootContentVariant = content;
        }
        // Test-only entry to observe borrowed prepared-item topology without
        // running command generation. It is not a script-visible member.
        void appendPreparedRenderItemsForDifferentialTest_guess(
            std::vector<detail::PreparedRenderItem *> &mainList,
            std::vector<detail::PreparedRenderItem *> &auxList,
            std::uint32_t inheritedColor = 0xFF808080u,
            bool inheritedDrawFlag19 = false,
            bool inheritedFlag18 = false) {
            appendPreparedRenderItems(
                mainList, auxList, inheritedColor,
                inheritedDrawFlag19, inheritedFlag18);
        }
        void setMotionContentForDifferentialTest_guess(
            const tTJSVariant &content) {
            _motionContentVariant = content;
        }
        void initNonEmoteMotionForDifferentialTest_guess(
            std::uint32_t playFlags) {
            initNonEmoteMotion_guess(playFlags);
        }
        void initVariablesForDifferentialTest_guess() {
            initVariables();
        }
        [[nodiscard]] std::size_t
        variableLabelScopeCountForDifferentialTest_guess() const {
            return _variableLabelScopes.size();
        }
        [[nodiscard]] ttstr
        variableLabelScopeKeyForDifferentialTest_guess(
            std::size_t index) const {
            return _variableLabelScopes[index].cascadeKey;
        }
        [[nodiscard]] tTJSVariant
        variableLabelFrameSourceForDifferentialTest_guess(
            std::size_t index) const {
            return _variableLabelScopes[index].frameSource;
        }
        bool prepareRenderItemsForDifferentialTest_guess(
            detail::PreparedRenderItemList &mainList,
            detail::PreparedRenderItemList &auxList) {
            return prepareRenderItems(mainList, auxList);
        }
        // Test-only entry for the ordinary Canvas submitter. It permits
        // observing the buffered image-skip/debug-frame boundary without
        // registering another Motion.Player script member.
        void executeLayerRenderCommandsForDifferentialTest_guess(
            iTJSDispatch2 *layerClassObject, iTJSDispatch2 *renderLayerObject,
            tjs_int canvasWidth, tjs_int canvasHeight,
            detail::PreparedRenderItemList &mainList) {
            executeLayerRenderCommands(layerClassObject, renderLayerObject,
                                       canvasWidth, canvasHeight, false,
                                       mainList);
        }
        // Test-only entry for the common builder's auxiliary/group tail. It
        // preserves all four target-clip edges and is not script-visible.
        void composePreparedGroupLayersForDifferentialTest_guess(
            detail::PreparedRenderItemList &auxList,
            const std::array<float, 4> &targetClip) {
            composePreparedGroupLayers_guess(auxList, targetClip);
        }
        // Test-only entry to the CameraNode phase, used to keep the adjacent
        // cameraActive/stereovisionActive gates independently observable.
        void updateCameraNodeForDifferentialTest_guess() {
            updateLayersPhase3_CameraNode();
        }
        // Test-only entry to the type-4 particle pass. It exposes conditional
        // random-number consumption without registering another script member.
        void updateParticleSystemsForDifferentialTest_guess() {
            updateLayersPhase3_ParticleSystem();
        }
        [[nodiscard]] double
        cameraVelocityXForDifferentialTest_guess() const {
            return _cameraVelocityX;
        }
        [[nodiscard]] double
        cameraVelocityYForDifferentialTest_guess() const {
            return _cameraVelocityY;
        }
        [[nodiscard]] double
        cameraVelocityZForDifferentialTest_guess() const {
            return _cameraVelocityZ;
        }
        [[nodiscard]] double
        cameraDampingForDifferentialTest_guess() const {
            return _cameraDamping;
        }
        void updateParticleEmittersForDifferentialTest_guess() {
            updateLayersPhase3_ParticleEmitter();
        }
        // Test-only entry for the native load helper's shared result slot and
        // current-dispatch callback boundary. It is not registered as a
        // Motion.Player script member.
        tTJSVariant loadMotionResultForDifferentialTest_guess(
            ttstr chara, ttstr motion,
            iTJSDispatch2 *currentDispatch = nullptr) {
            iTJSDispatch2 *savedDispatch = _currentDispatch;
            _currentDispatch = currentDispatch;
            try {
                tTJSVariant result = loadMotionResult_guess(
                    std::move(chara), std::move(motion));
                _currentDispatch = savedDispatch;
                return result;
            } catch(...) {
                _currentDispatch = savedDispatch;
                throw;
            }
        }
        // Test-only observation of the raw callback bridge slot. Native play
        // and progress wrappers intentionally leave it populated when their
        // inner call unwinds through an exception.
        [[nodiscard]] iTJSDispatch2 *
        currentDispatchForDifferentialTest_guess() const {
            return _currentDispatch;
        }
        void clearCurrentDispatchForDifferentialTest_guess() {
            _currentDispatch = nullptr;
        }
        // Test-only entries for the native event-vector iteration and dispatch
        // lifetime boundaries. They are not registered as script members.
        void reservePendingEventsForDifferentialTest_guess(std::size_t count) {
            _pendingEvents.reserve(count);
        }
        void dispatchPendingEventsForDifferentialTest_guess(
            iTJSDispatch2 *dispatch) {
            dispatchPendingEvents_guess(dispatch);
        }
        void refreshParameterizedNodeTimelinesForDifferentialTest_guess() {
            refreshParameterizedNodeTimelines_guess();
        }
        void refreshModifiedNodeTimelinesForDifferentialTest_guess() {
            refreshModifiedNodeTimelines_guess();
        }
        // Test-only producer for the exact parameter-vector idle-progress gate.
        // The returned reference remains valid until another vector mutation.
        detail::MotionParameterEntry &
        appendParameterEntryForDifferentialTest_guess() {
            _parameterEntries.emplace_back();
            return _parameterEntries.back();
        }
        void selectParameterEntryForDifferentialTest_guess(
                std::size_t index) {
            _selectedParameterEntry = &_parameterEntries[index];
        }
        void updateMotionSubNodesForDifferentialTest_guess() {
            updateLayersPhase3_MotionSubNode();
        }
        void appendParameterVariantForDifferentialTest_guess(
                const tTJSVariant &parameter) {
            appendParameterEntry_guess(parameter);
        }
        bool parseParameterListForDifferentialTest_guess(
                const tTJSVariant &parameters) {
            return parseParameterList_guess(parameters);
        }
        [[nodiscard]] std::size_t
        parameterEntryCountForDifferentialTest_guess() const noexcept {
            return _parameterEntries.size();
        }
        [[nodiscard]] const detail::MotionParameterEntry &
        parameterEntryForDifferentialTest_guess(std::size_t index) const {
            return _parameterEntries[index];
        }
        [[nodiscard]] std::size_t
        parameterRampCountForDifferentialTest_guess() const noexcept {
            return _parameterRampMap.size();
        }
        void updateLayersForDifferentialTest_guess() {
            updateLayers();
        }
        // Test-only entry for the four-binary timeline evaluator. It is not
        // registered as a Motion.Player script member.
        bool evaluateTimelineForDifferentialTest_guess(
            detail::MotionNode &node, double currentTime, bool dirtyArg) {
            return internal::evaluateTimeline_guess(
                node, currentTime, dirtyArg);
        }
        // Test-only setup for the EmoteEngine getVariable router. These do not
        // expose script members or change the production lookup path.
        void setVariableSnapshotForDifferentialTest_guess(
            const ttstr &label, double value) {
            _variableSnapshotMap[label] = value;
        }
        void addVariableLabelScopeForDifferentialTest_guess(
            const ttstr &label) {
            detail::VariableLabelScope item;
            item.cascadeKey = label;
            _variableLabelScopes.push_back(std::move(item));
        }
        [[nodiscard]] std::string matchedMotionPath() const;
        [[nodiscard]] const std::deque<detail::MotionNode> &nodes() const {
            return _nodes;
        }
        // Mutable build-side views used by the split translation-unit helpers.
        // They expose the persistent deque/map themselves; callers must retain
        // the native stable-address and raw-label-index invariants below.
        std::deque<detail::MotionNode> &nodesForBuild() { return _nodes; }
        detail::NodeLabelMap &nodeLabelMapForBuild() {
            return _nodeLabelMap;
        }
        // Test-only entry for the complete native old-tree reset. It is not
        // registered as a Motion.Player script method.
        void resetAndReleaseOldNodeTreeForDifferentialTest_guess() {
            resetAndReleaseOldNodeTree_guess();
        }
        [[nodiscard]] double evalCascadeWeightForDifferentialTest_guess(
                const ttstr &key) const {
            const auto found = _evalCascadeMap.find(key);
            return found == _evalCascadeMap.end()
                ? 0.0 : found->second.weight;
        }
        // Test-only re-entrant owner mutation. Other constructor-owned
        // ResourceManager aliases are deliberately untouched; the reset probe
        // uses this to verify that its captured dispatch receiver stays fixed.
        void clearCanonicalResourceManagerForDifferentialTest_guess() {
            _resourceManager.Clear();
        }
        void setCanonicalResourceManagerForDifferentialTest_guess(
                const tTJSVariant &value) {
            _resourceManager = value;
        }
    public:
        // Reach the native ResourceManager through the first of the three
        // retained dispatch owners for atlas/cache-only fast paths. Generic
        // source fallback remains a TJS FuncCall on that retained dispatch.
        // Player does not own the native by value; invalid or Void input
        // returns null here without weakening the generic fallback boundary.
        ResourceManager *nativeRM() const;

        // Layer-id allocation/release intentionally goes through the retained
        // ResourceManager dispatch via TJS FuncCall, rather than shortcutting
        // to its native instance. A malformed ResourceManager Variant keeps
        // the native conversion failure instead of becoming a silent no-op.
        tjs_int dispatchRequireLayerId(tjs_uint32 *hint = nullptr) const;
        void dispatchReleaseLayerId(tjs_int id,
                                    tjs_uint32 *hint = nullptr) const;

    private:
        // Dispatch the generic ResourceManager.findSource fallback and return
        // its raw TJS status. The caller retains the receiver and the context
        // snapshot from resolver entry; this helper owns the path argument for
        // the complete call.
        tjs_error dispatchFindSource_guess(
            iTJSDispatch2 *resourceManager,
            tTJSVariant &contextArgument,
            ttstr name, tTJSVariant &result);

        friend class D3DAdaptor;
        // Engine-only variable routing helpers. Their by-value ttstr
        // parameters preserve the native caller-owned temporary boundaries.
        [[nodiscard]] bool hasVariableLabelScope_guess(ttstr key) const;
        [[nodiscard]] double
        readSnapshotOrBoundParameterValue_guess(ttstr label);

        // Two independent, non-owning links. Construction seeds root=this and
        // parent=null. Both type-3 and particle paths then overwrite the child
        // pair before any child-specific property read or adaptor creation:
        // root inherits the parent's canonical root, while parent records the
        // immediate owner. Root-scoped render/update work follows the first;
        // parameter publication and lookup walk the second toward null.
        Player *_rootPlayer = nullptr;
        Player *_parentPlayer = nullptr;
        // Non-owning dispatch for the currently executing NCB wrapper. It is
        // the third machine-word field in every native Player. play/progress
        // publish objthis here and clear it only on their normal tail; the
        // pointer itself never AddRefs or Releases.
        iTJSDispatch2 *_currentDispatch = nullptr;
        // Ordered raw-label-to-flat-node-index map with UTF-16 comparison. Its
        // ABI-specific tree header follows currentDispatch at the start of
        // Player and ends exactly where the nine-double camera block begins.
        detail::NodeLabelMap _nodeLabelMap;
        // The native object prefix continues from the raw-label map into nine
        // doubles: camera position, camera target, then the stereovision
        // camera. All nine start at exact +0.0. CameraNode writes the first two
        // triples under cameraActive; the stereovision setter writes the final
        // triple independently.
        double _cameraPosX = 0.0;
        double _cameraPosY = 0.0;
        double _cameraPosZ = 0.0;
        double _cameraTargetX = 0.0;
        double _cameraTargetY = 0.0;
        double _cameraTargetZ = 0.0;
        double _stereovisionCameraX_guess = 0.0;
        double _stereovisionCameraY_guess = 0.0;
        double _stereovisionCameraZ_guess = 0.0;
        // Two separately zeroed floats immediately follow the nine doubles.
        // CameraNode and setCameraOffset both overwrite this same pair.
        float _cameraOffsetX = 0.0f;
        float _cameraOffsetY = 0.0f;
        // The constructor and every calcBounds entry publish the exact
        // +DBL_MAX,+DBL_MAX,-DBL_MAX,-DBL_MAX quartet. The MotionNode deque
        // starts immediately after maxY in every reference ABI.
        double _boundsMinX = std::numeric_limits<double>::max();
        double _boundsMinY = std::numeric_limits<double>::max();
        double _boundsMaxX = -std::numeric_limits<double>::max();
        double _boundsMaxY = -std::numeric_limits<double>::max();
        // Flat MotionNode storage. Index zero is the constructor-created root;
        // loaded layer trees append at [1,end). The deque's stable element
        // addresses are also cached by the immediately following cascade map.
        std::deque<detail::MotionNode> _nodes;
        // The four native targets place two standard-library unordered maps
        // directly after the node deque. HM1 stores cascade writes plus a
        // non-owning cache of matching deque children. EvalCascadeState owns
        // its key/chain/vector backing, but never the MotionNode objects in
        // heapResult. HM2 stores the raw ttstr label -> double result written
        // after any HM1 propagation. Both use the custom UTF-16 ttstr hash;
        // they are not open-addressing containers.
        detail::EvalCascadeMap _evalCascadeMap;
        detail::LabelValueMap _evalResultValues;
        // Player-level parameter selected by motion.content.parameterize. It
        // is a non-owning alias into the following vector and is therefore
        // trivially cleared rather than destroyed.
        detail::MotionParameterEntry *_selectedParameterEntry = nullptr;
        // Parsed parameter table and exact idle frameProgress refresh gate. In
        // the !firstFrame && !allplaying path an empty vector returns before the
        // parameterized-node scan; nonempty does not imply that any node points
        // into it, so the subsequent scan may still be a no-op.
        std::vector<detail::MotionParameterEntry> _parameterEntries;
        // Parameter-ramp multimap<ttstr id, MotionParameterEntry*>. Built by
        // finalizeParameterTable_guess and consumed by the binder's equal-range
        // loop. Its values alias the preceding vector. Normal destruction and
        // constructor rollback therefore destroy the map before the vector,
        // then HM2, HM1 and the node deque in exact reverse declaration order.
        detail::ParameterRampMap _parameterRampMap;
        // The first three doubles after the ramp tree are three independent
        // live values: the clamped evaluation cursor, Player-side emote angle,
        // and camera-to-target angle. The public lastTime/loopTime pair lives
        // much later in the native object and must not be folded into this
        // block merely because all three constructor values are zero.
        double _clampedEvalTime = 0.0;
        double _emoteAngle = 0.0;
        double _cameraAngle = 0.0;
        // Four adjacent frame-state bytes follow the scalar triple.
        bool _queuing = true;
        bool _firstFrame = false;
        bool _directEdit = false;
        bool _motionCompleted = false;
        // Type-1 wrapper state then continues as division Variant, deliberately
        // uninitialized motion index, motion-list Variant and loaded motion
        // content. playImpl commits the two property owners separately and
        // writes -1 to the index only immediately before initEmoteMotion.
        tTJSVariant _emoteDivisionVariant;
        int _emoteMotionIndex;
        tTJSVariant _emoteMotionListVariant;
        tTJSVariant _motionContentVariant;
        // Root/priority stream state is physically split from the much later
        // tag stream. The priority owner precedes its cursor/time scalars.
        tTJSVariant _priorityFrameSourceVariant;
        int _rootFrameCursor = 0;
        double _rootCurTime = 0.0;
        double _rootNextTime = 0.0;
        // Per-frame scaled delta and damping are followed by four independent
        // control bytes. The two render-layer producer/consumer bytes come
        // next; post-draw snapshots needsInternalAssignImages into the earlier
        // internalRenderLayerReady byte.
        double _deltaTime = 0.0;
        double _cameraDamping = 1.0;
        bool _noUpdateYet = true;
        bool _reverseSeekFlag = false;
        bool _cameraConstraintDirty_guess = false;
        bool _drawAffineMatrixNonIdentity = false;
        bool _internalRenderLayerReady = false;
        bool _needsInternalAssignImages = false;
        // priority[0].content ends the root-stream portion and is destroyed
        // before the priority/motion/type-1 owners.
        tTJSVariant _rootContentVariant;
        // Six consecutive Variants form the persistent render-source workspace.
        // The constructor CopyRefs the same ResourceManager into the first two
        // independent owners. Descriptor and colors become persistent
        // Dictionaries; primary and work Layers start Void and are lazily
        // materialized. descriptor.color adds another reference to colors.
        tTJSVariant _findSourceResourceManager;
        tTJSVariant _sourceCacheObject;
        tTJSVariant _sourceDescriptor;
        tTJSVariant _internalRenderLayer;
        tTJSVariant _sourceColors;
        tTJSVariant _internalSourceWorkLayer_guess;
        // A single pointer-sized raw owner directly follows the workspace.
        // Lazy construction publishes only after the adaptor constructor
        // succeeds. Player destruction runs the pointee destructor, frees it,
        // and only then clears the slot; this is not unique_ptr reset timing.
        SeparateLayerAdaptor *_renderSeparateLayerAdaptor = nullptr;
        // Two independent pending ttstr owners follow the adaptor pointer.
        // A queued value remains in its field throughout the nested flush and
        // is cleared only after that call returns normally.
        ttstr _pendingStealthMotion;
        ttstr _pendingStealthChara;
        // The three persistent camera-velocity doubles immediately follow the
        // pending pair and directly precede drawAffine in all four native
        // layouts. Damping is the earlier frame-delta neighbor, not a fourth
        // member of this contiguous block.
        double _cameraVelocityX = 0.0;
        double _cameraVelocityY = 0.0;
        double _cameraVelocityZ = 0.0;
        // drawAffineMatrix is the 2x3 affine applied during draw dispatch.
        // Four double linear components are followed by two float translations.
        // The setter argument order is m11, m21, m12, m22, m14, m24, while the
        // stored linear-component order is m11, m12, m21, m22.
        double _drawAffineM11 = 1.0;
        double _drawAffineM12 = 0.0;
        double _drawAffineM21 = 0.0;
        double _drawAffineM22 = 1.0;
        float _drawAffineM14 = 0.0f;
        float _drawAffineM24 = 0.0f;
        // Persistent root-owned viewport consumed only by the first pass of
        // particle-child stepping when a particle node enables deleteOutside.
        // Accurate SLA rewrites it from the current target, the inverse root
        // draw-affine transform, camera offset and outsideFactor. The next
        // direct nontrivial member after this four-float array remains a
        // separate layout boundary.
        std::array<float, 4> _particleOutsideRect = {
            0.0f, 0.0f, 0.0f, 0.0f
        };
        // Complex region containing rectangles submitted by the latest
        // non-preview renderToCanvas pass. Its ABI-sized destructor runs after
        // the event vector and before the earlier pending stealth owners.
        tTVPComplexRect _drawRegion;
        // Four references expose one ctor-zeroed 32-bit slot here and no other
        // access in the complete Player code cluster. The stripped private
        // source identity is therefore intentionally left as a guess.
        std::uint32_t _postDrawRegionDword_guess = 0;
        // One-shot type-3 root-matrix marker followed immediately by sticky
        // public D3D mode; natural alignment then places pixelateDivision.
        bool _type3RootTransformAlreadyPropagated = false;
        bool _d3dDrawMode = false;
        int _pixelateDivision = 100;
        // Tag-stream cursor/time POD. The Player constructor deliberately does
        // not initialize these three members; ordinary motion initialization
        // commits them before forward/rewind/reseek traversal.
        int _layerFrameCursor;
        double _layerCurTime;     // tag[cursor].time
        double _layerNextTime;    // tag[cursor+1].time
        // The sole persistent event vector is followed by four independent
        // live ttstr owners in primary/stealth chara then primary/stealth
        // motion order. Vector teardown precedes drawRegion destruction.
        std::vector<detail::MotionEvent> _pendingEvents;
        ttstr _chara;
        ttstr _stealthChara;
        ttstr _motionKey;
        ttstr _stealthMotion;
        // Third independent CopyRef of the constructor ResourceManager. It is
        // the first Variant owner after the four live string slots.
        tTJSVariant _resourceManager;
        // Successful loads incrementally replace result[1] here. Consumers and
        // child Players take their own CopyRefs; later play failures do not
        // roll back an already completed assignment.
        tTJSVariant _findMotionContextVariant;
        // Persistent rendering style owners. Both begin Void and preserve the
        // incoming Variant type exactly through CopyRef/copy assignment.
        tTJSVariant _outline;
        tTJSVariant _meshline;
        // Ordinary motion initialization commits tag before priority/root.
        // The read-only tags property CopyRefs this final cluster owner.
        tTJSVariant _tagFrameSourceVariant;
        // Nine Boolean bytes immediately follow the tag Variant in every
        // current reference. Keep this exact declaration order: several
        // constructors combine adjacent pairs into halfword stores.
        // Preview gates node-type masks, child motion/particle updates,
        // visibility, render-item preparation, bounds and camera constraints.
        bool _preview = false;
        // Construct-time snapshot of the process-global class default. The
        // public setter is the only later direct writer; timeline steppers only
        // read it as their align/sync gate.
        bool _syncActive = s_defaultSyncActive;
        // CameraNode reads cameraActive; the post-prepare projection pass reads
        // stereovisionActive. They are independent public gates.
        bool _cameraActive = false;
        bool _stereovisionActive = false;
        // Player priorDraw is independent of per-node priorDraw propagation.
        bool _priorDraw = false;
        // The public setter intentionally marks every node dirty without
        // committing this byte. Type-3 child initialization is its real
        // non-constructor writer.
        bool _independentLayerInherit = false;
        bool _syncWaiting = false;
        bool _allplaying = false;
        // Per-frame CameraNode result exposed as cameraAlive. The CameraNode
        // phase clears it before scanning and sets it only for the first active
        // type-5 node; the read-only hasCamera property does not read this byte.
        bool _hasCamera = false;
        // The following doubles use each target ABI's natural alignment. An
        // active CameraNode overwrites FOV only while cameraActive is true.
        double _cameraFov = 0.2;
        // Native construction stores exact +0.0 here. setZFactor uses ordered
        // equality, dirties root element zero, then recursively visits children.
        double _zFactor = 0.0;
        // Raw frame-domain cursor shared by frameTickCount and tickCount.
        double _frameTickCount = 0.0;
        // Raw motion metadata returned by frameLastTime and frameLoopTime.
        // Player construction deliberately leaves both POD slots untouched;
        // ordinary motion initialization commits lastTime before loop-driven
        // frame progression can consume them. There is no separate raw-dt
        // frameLastTime member.
        double _cachedTotalFrames;
        double _loopTime;
        // Native declaration/layout order is completionType immediately
        // followed by maskMode. Both default to zero, preserve the complete
        // signed 32-bit value, and perform no setter-side normalization.
        tjs_int _completionType = 0;
        tjs_int _maskMode = 0;
        // Reset by frameProgress before any early return; updateLayers then
        // counts mesh-point transformations performed by this Player.
        std::uint32_t _processedMeshVerticesNum = 0;
        // Render-native RGBA weight: RGB 128 and alpha 255 are neutral.
        std::uint32_t _colorWeightPacked = 0xFF808080u;
        // The three adjacent raw-double properties are independently
        // registered. A prior single-binary map incorrectly invented a speed
        // Boolean at syncActive; speed is this middle multiplier instead.
        double _outsideFactor = 1.5;
        double _speedMul = 1.0;        // speed multiplier (dt scale)
        double _meshDivisionRatio = 1.0;
        // HM3: per-node-path join snapshot. The node path is distinct from the
        // early raw-label node-index map. Only eligible join targets are
        // inserted; each value owns an all-zero embedded ClipSlot plus the
        // specialized Variants, mesh vector and particle interpolation block.
        detail::PerNodeLayerStateMap _perNodeLayerStateMap;
        // HM4: short-lived cascade-key -> raw-double bridge. Join reset writes
        // live variable-track values; full reseek restores hits and then clears
        // HM4 before HM3. The mapped double has no reference-count ownership.
        detail::VariableSnapshotMap _variableSnapshotMap;
        // The variable-track deque follows both maps directly in all four
        // native layouts. Stable element addresses are relied on by cursor and
        // snapshot paths; clear destroys elements but retains implementation-
        // defined block/map capacity.
        detail::VariableLabelScopeDeque _variableLabelScopes;
        // Final native Player slot. Only the two Android products retain an
        // otherwise-unreferenced legacy load helper that reads this raw
        // dispatch and calls onFindMotion through it. No product initializes,
        // publishes, AddRefs, Releases or destroys the slot; the live load
        // path instead follows rootPlayer->_currentDispatch. Leave the pointer
        // deliberately indeterminate to preserve that dead residual boundary.
        iTJSDispatch2 *_tailDispatchLoadMotionResidual_guess;
        // === end native Player instance layout ===
        // Headless differential-only per-layer render snapshots. Ordinary
        // plugin builds use the recovered prepared-list/SLA render paths and
        // must not carry this diagnostic map in the Player object layout.
#if defined(KRKR2_WASMTIME_HEADLESS)
        // Headless-only observable render snapshots. Native Player state has
        // no by-name layer-id maps; production layer ids come from the
        // ResourceManager's no-argument allocator.
        std::unordered_map<tjs_int, detail::LayerRenderState> _renderLayerStates;
#endif

        // Class-level process-global property state shared by all Player
        // instances. Initial values are common to all four references.
        //   s_defaultSyncActive default = false
        //   s_defaultTransformOrder default = {0,3,2,1}
        static bool s_defaultSyncActive;
        static int s_defaultTransformOrder[4];
#if defined(KRKR2_WASMTIME_HEADLESS)
        // Test-side sequence used only by the headless render snapshot map.
        // It is not the ResourceManager layer-id allocator or an ordinary
        // plugin-build Player member.
        tjs_int _nextLayerAbsolute = 1;
#endif
        // Child motion players receive the evaluated node color directly in
        // the renderer's native packed RGBA byte order.
        // EmoteEngine also owns a distinct metadata/controller scale pair.
        // It drives Emote coordinate and wind scaling and must not be
        // conflated with Player::_meshDivisionRatio; matching Android-arm64
        // numeric offsets were the source of an earlier receiver mix-up.
        //
        // The script-visible hairScale/partsScale/bustScale triplet is also
        // Engine-owned. Motion.EmotePlayer and D3DEmotePlayer both forward
        // those properties to the same Engine values; Player has no duplicate
        // scale or directEdit/physics-disable byte.
        //
        // The progress main-loop dirty byte and the wind emitter/cache likewise
        // live on EmoteEngine. Player owns no duplicate state or Engine
        // back-pointer for those facilities.

        friend class EmoteEngine;
        // SourceCache does not retain a Player back-pointer. Its Web-only
        // render-item helper borrows a Player reference for the duration of one
        // lookup; friendship permits that helper to read the persistent motion
        // context and unwrap the native ResourceManager.
        friend class SourceCache;
        friend detail::MotionParameterEntry *internal::selectParameterEntry_guess(
            Player &, const tTJSVariant &);
        friend void internal::initializeNodeTimelineSlots_guess(
            Player &, detail::MotionNode &);
        friend void internal::seekParameterizedNodeFrames_guess(
            detail::MotionNode &, Player &);
    };

} // namespace motion
