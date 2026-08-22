//
// Created by LiDon on 2025/9/15.
// D3DEmotePlayer architecture cross-checked against all four current reference
// binaries. It is a standalone NCB class, not an EmotePlayer subclass; the two
// classes are registered independently. Exact object sizes and field offsets
// are ABI-specific. Current per-target mappings live under analysis/.
//

#include <algorithm>
#include <cstdint>
#include <utility>

#include "D3DEmoteModule.h"
#include "EmotePlayer.h"
#include "MotionDispatch.h"
#include "RuntimeSupport.h"
#include "ScriptMgnIntf.h"
#include "ncbind.hpp"

namespace motion {

    namespace {

        tjs_uint32 emoteObjectBaseCharaHint_guess = 0;
        tjs_uint32 emoteObjectBaseMotionHint_guess = 0;

        double scriptEaseToPowerDouble_guess(double ease) {
            if(ease > 0.0) {
                return ease + 1.0;
            }
            if(ease < 0.0) {
                return 1.0 / (1.0 - ease);
            }
            return 1.0;
        }

        float scriptEaseToPower_guess(double ease) {
            return static_cast<float>(scriptEaseToPowerDouble_guess(ease));
        }

    } // namespace

    // Four-reference EmoteObject construction order:
    //   1. allocate the ABI-sized ResourceManager and store the owning pointer
    //   2. wrap it in a temporary sticky TJS dispatch Variant
    //   3. allocate the ABI-sized EmoteEngine and pass that temporary owner down
    //   4. destroy the temporary Variant, then copy modulePaths
    // Once either pointer is stored, later ctor failure does not release it;
    // retaining raw fields preserves that four-reference unwind boundary.
    EmoteObject::EmoteObject(const std::vector<ttstr> &modulePaths) {
        // All four initializers evaluate global.kag before allocating the sole
        // ResourceManager and pass the literal 20 MiB source-cache byte budget.
        tTJSVariant kag;
        TVPExecuteExpression(TJS_W("global.kag"), &kag);
        _rm = new ResourceManager(kag, 0x01400000);

        // The dispatch is a constructor-stack owner, not an EmoteObject field.
        // All four producers call CreateAdaptor with sticky=true and the
        // default error=false. The adaptor therefore borrows _rm; CreateNew or
        // type failure does not reclaim it, and a successfully created shell
        // may still be returned when its ResourceManager adaptor lookup yields
        // null. _rm remains the sole native owner across every such boundary.
        // The Variant scope ends immediately after Engine construction, before
        // the path-vector copy, matching all four native refcount timelines.
        // Player and child Players retain independent Variant owners.
        {
            tTJSVariant rmDispatch;
            using RMAdaptor = ncbInstanceAdaptor<ResourceManager>;
            if(auto *dispatch = RMAdaptor::CreateAdaptor(_rm, true)) {
                rmDispatch = tTJSVariant(dispatch, dispatch);
                dispatch->Release();
            }
            _engine = new EmoteEngine(rmDispatch);
        }

        // Path-vector copy begins only after both heap objects are established
        // and the temporary dispatch Variant has been released.
        _modulePaths = modulePaths;

        // The four initializers load all paths, use the last raw module as the
        // metadata source, seed Player project from the last input path, apply
        // chara, force-play motion, then apply the full metadata dictionary.
        tTJSVariant loaded;
        for(const auto &path : modulePaths) {
            loaded = _rm->load(path);
        }
        const tTJSVariant metadata =
            detail::motionPropGet(loaded, TJS_W("metadata"));
        // The final load-result slot is the base working Variant from this
        // point onward. This releases the last module dispatch before Player
        // seeding instead of retaining an otherwise-dead third owner.
        loaded =
            detail::motionPropGet(metadata, TJS_W("base"));
        ncbPropAccessor baseObject{tTJSVariant(loaded)};
        const ttstr chara = baseObject.GetValue(
            TJS_W("chara"), ncbTypedefs::Tag<ttstr>(), 0,
            &emoteObjectBaseCharaHint_guess);
        const ttstr motionName = baseObject.GetValue(
            TJS_W("motion"), ncbTypedefs::Tag<ttstr>(), 0,
            &emoteObjectBaseMotionHint_guess);

        auto &player = _engine->player();
        player.setProject(tTJSVariant(modulePaths.back()));
        player.setChara(chara);
        player.playMotion_guess(PlayFlagForce, motionName);
        _engine->applyMetadata_guess(metadata);
    }

    // Four-reference normal destruction order: Engine, ResourceManager, then
    // the modulePaths vector as an ordinary member destructor. There is no
    // persistent dispatch field between Engine and ResourceManager. EmoteEngine
    // has no virtual/deleting destructor of its own: each raw-owner delete below
    // lowers to the ordinary pointee destructor followed by scalar operator
    // delete. EmoteObject itself is deallocated later by its D3D outer owner.
    EmoteObject::~EmoteObject() {
        delete _engine;
        delete _rm;
    }

    // The copy remains a raw local throughout state transfer. Its new-expression
    // cleanup deletes only pending storage when the EmoteObject constructor
    // throws. Once construction completes, serialize failure leaks the complete
    // copy; unserialize failure destroys the live state Variant but still leaks
    // the copy. Do not add a guard that repairs those four-reference frontiers.
    EmoteObject *EmoteObject::clone_guess() {
        auto *copy = new EmoteObject(_modulePaths);
        tTJSVariant state = _engine->serializeState_guess();
        copy->_engine->unserializeState_guess(state);
        return copy;
    }

    D3DEmotePlayer::D3DEmotePlayer(D3DLayer *d3dLayerOwner)
        : D3DLayerListener(d3dLayerOwner) {}

    // The wrapper supplies objthis separately, unboxes arg0 as D3DLayer, and
    // owns failure cleanup after this listener shell has registered itself.
    D3DEmotePlayer *D3DEmotePlayer::factory(iTJSDispatch2 *,
                                             D3DLayer *d3dLayerOwner) {
        return new D3DEmotePlayer(d3dLayerOwner);
    }

    // The complete destructor clears both EmoteObject owners before listener
    // base teardown. The compiler's distinct deleting-destructor entry performs
    // scalar operator delete only after this complete destructor returns.
    D3DEmotePlayer::~D3DEmotePlayer() {
        clear();
    }

    // D3DLayer invokes this from matrix-change and update fan-out. The native
    // path compares exact floats and only dereferences primary on scale change.
    // It always returns true and never reads the shell's script-visible
    // `_visible` byte; that byte is dormant compatibility state.
    bool D3DEmotePlayer::IsVisible() {
        const float ownerScale = TVPGetD3DLayerScaleX(GetD3DLayerOwner());
        if(_baseScale != ownerScale) {
            _baseScale = ownerScale;
            const float finalScale = _baseScale * _userScale;
            EmoteEngine &target = engine();
            target._dirty = true;
            EmoteVarController_setTarget_guess(
                target._ctlScale.get(), &finalScale, 0.0f, 1.0f,
                target._queuing);
        }
        return true;
    }

    // Transform the zero origin through D3DLayer, then enter the Player's
    // direct-texture route with the compositor's native target texture.
    void D3DEmotePlayer::Draw(iTVPTexture2D *target) {
        float x = 0.0f;
        float y = 0.0f;
        GetD3DLayerOwner()->TransformPoint(x, y);
        player().drawToTexture_guess(target, x, y);
    }

    // --- Properties ---

    void D3DEmotePlayer::setVisible(bool v) {
        _visible = v;
    }

    void D3DEmotePlayer::setMeshDivisionRatio(double v) {
        player().setMeshDivisionRatio(v);
    }

    bool D3DEmotePlayer::getAnimating() const {
        return engine().getAnimating_guess();
    }

    D3DEmoteModule *D3DEmotePlayer::getModule() const {
        // The native shell itself does not own this object, but pointer-result
        // boxing uses sticky=false: the returned non-sticky TJS adaptor and the
        // DrawDevice/root class-id map both delete the same pointer.  The module
        // has no map back-pointer, so wrapper-first teardown leaves a dangling
        // non-null cache value and root-first teardown leaves a live wrapper
        // owning freed storage.  Preserve this four-reference double-owner bug.
        const auto classId = static_cast<tjs_uint32>(
            ncbClassInfo<D3DEmoteModule>::GetID());
        auto *module = static_cast<D3DEmoteModule *>(
            GetD3DLayerOwner()->FindParentModule_guess(classId));
        if(!module) {
            module = new D3DEmoteModule();
            GetD3DLayerOwner()->SetParentModule_guess(classId, module);
        }
        return module;
    }

    // --- Methods ---

    // Four-reference slot protocol: secondary is destroyed/deallocated first,
    // primary second, and neither member is cleared until both deletes finish.
    // The paired null stores therefore occur after all nested Engine/RM/path
    // teardown, rather than as two independent reset-before-delete operations.
    void D3DEmotePlayer::clear() {
        delete _secondaryObj;
        delete _primaryObj;
        _primaryObj = nullptr;
        _secondaryObj = nullptr;
    }

    void D3DEmotePlayer::load(const std::vector<ttstr> &modulePaths) {
        clear();
        _primaryObj = new EmoteObject(modulePaths);
    }

    tjs_error D3DEmotePlayer::loadCompat(tTJSVariant *,
                                         tjs_int numparams,
                                         tTJSVariant **param,
                                         D3DEmotePlayer *nativeInstance) {
        std::vector<ttstr> modulePaths;
        modulePaths.reserve(static_cast<size_t>(numparams));
        for(tjs_int i = 0; i < numparams; ++i) {
            modulePaths.emplace_back(*param[i]);
        }
        nativeInstance->load(modulePaths);
        return TJS_S_OK;
    }

    // Shell construction immediately registers a listener on the borrowed
    // D3DLayer. The new-expression cleanup owns pending shell storage only until
    // that constructor returns. A later EmoteObject clone exception therefore
    // leaks both the completed shell and its live listener registration.
    D3DEmotePlayer *D3DEmotePlayer::clone(D3DLayer *d3dLayerOwner) {
        auto *copy = new D3DEmotePlayer(d3dLayerOwner);
        copy->_primaryObj = obj().clone_guess();
        return copy;
    }

    void D3DEmotePlayer::show() {
        _visible = true;
    }

    void D3DEmotePlayer::hide() {
        _visible = false;
    }

    // All four current references first require an Object, probe its native
    // D3DEmotePlayer instance without raising on a class mismatch, and then
    // unconditionally throw the same TODO eTJSError.
    void D3DEmotePlayer::assignState(tTJSVariant state) {
        iTJSDispatch2 *object = state.AsObjectNoAddRef();
        if(object) {
            (void)ncbInstanceAdaptor<D3DEmotePlayer>::GetNativeInstance(
                object, false);
        }
        TVPThrowExceptionMessage(
            TJS_W("TODO: implement D3DEmotePlayer::assignState()"));
    }

    void D3DEmotePlayer::setRot(double rot, double transition, double ease) {
        EmoteEngine &target = engine();
        target._dirty = true;
        EmoteAngleController_setTarget_guess(
            target._ctlAngle.get(), static_cast<float>(rot),
            static_cast<float>(transition), static_cast<float>(ease),
            target._queuing);
    }

    // All four current references return a hardcoded 0.0.
    double D3DEmotePlayer::getRot() { return 0.0; }

    void D3DEmotePlayer::setCoord(double x, double y, double transition,
                               double ease) {
        const float values[2] = {
            static_cast<float>(x), static_cast<float>(y)
        };
        EmoteEngine &target = engine();
        target._dirty = true;
        EmoteVarController_setTarget_guess(
            target._ctlPosition.get(), values, static_cast<float>(transition),
            static_cast<float>(ease), target._queuing);
    }

    void D3DEmotePlayer::setScale(double s, double transition, double ease) {
        _userScale = static_cast<float>(s);
        const float finalScale = _baseScale * _userScale;
        EmoteEngine &target = engine();
        target._dirty = true;
        EmoteVarController_setTarget_guess(
            target._ctlScale.get(), &finalScale, static_cast<float>(transition),
            static_cast<float>(ease), target._queuing);
    }

    // All four current references return a hardcoded 1.0.
    double D3DEmotePlayer::getScale() { return 1.0; }

    void D3DEmotePlayer::setColor(tjs_int color, double transition, double ease) {
        const auto packed = static_cast<tjs_uint32>(color);
        const float values[4] = {
            static_cast<float>(static_cast<std::uint8_t>(packed)),
            static_cast<float>(static_cast<std::uint8_t>(packed >> 8)),
            static_cast<float>(static_cast<std::uint8_t>(packed >> 16)),
            static_cast<float>(static_cast<std::uint8_t>(packed >> 24))
        };
        EmoteEngine &target = engine();
        target._dirty = true;
        EmoteVarController_setTarget_guess(
            target._ctlColor.get(), values, static_cast<float>(transition),
            static_cast<float>(ease), target._queuing);
    }

    // All four current references return a hardcoded 0.
    tjs_int D3DEmotePlayer::getColor() { return 0; }

    // --- Variable system ---
    // All four D3D shells forward the label and three binary64 values directly
    // to the primary EmoteObject's Engine. The generated NCB adapter requires
    // all four arguments; it supplies no defaults. setVariable writes the
    // Engine-side variable map. getVariable uses the Engine facade router:
    // scope-owned labels read Player HM1/HM2 directly, while other labels first
    // try the Player's short-lived HM4 join snapshot. Engine::progress bridges
    // the Engine variable-value map into the bound Player maps. See analysis/
    // for the four-target flow.
    void D3DEmotePlayer::setVariable(ttstr label, double value, double transition,
                                  double ease) {
        engine().setVariable(label, value, transition, ease);
    }

    double D3DEmotePlayer::getVariable(ttstr label) {
        return engine().getVariable(label);
    }

    // All five query functions are intentional TODO leaves in every current
    // reference binary. They do not read their index arguments before throwing.
    tjs_int D3DEmotePlayer::countVariables() {
        TVPThrowExceptionMessage(
            TJS_W("TODO: implement D3DEmotePlayer::countVariables()"));
        return 0;
    }

    ttstr D3DEmotePlayer::getVariableLabelAt(tjs_int) {
        TVPThrowExceptionMessage(
            TJS_W("TODO: implement D3DEmotePlayer::getVariableLabelAt()"));
        return {};
    }

    tjs_int D3DEmotePlayer::countVariableFrameAt(tjs_int) {
        TVPThrowExceptionMessage(
            TJS_W("TODO: implement D3DEmotePlayer::countVariableFrameAt()"));
        return 0;
    }

    ttstr D3DEmotePlayer::getVariableFrameLabelAt(tjs_int, tjs_int) {
        TVPThrowExceptionMessage(
            TJS_W("TODO: implement D3DEmotePlayer::getVariableFrameLabelAt()"));
        return {};
    }

    double D3DEmotePlayer::getVariableFrameValueAt(tjs_int, tjs_int) {
        TVPThrowExceptionMessage(
            TJS_W("TODO: implement D3DEmotePlayer::getVariableFrameValueAt()"));
        return 0.0;
    }

    // --- Wind/Force ---
    void D3DEmotePlayer::startWind(float minAngle, float maxAngle,
                                   float amplitude, float freqX,
                                   float freqY) {
        engine().setWind_guess(minAngle, maxAngle, amplitude, freqX, freqY);
    }

    void D3DEmotePlayer::stopWind() {
        engine().setWind_guess(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    }

    // --- Timeline methods ---

    tjs_int D3DEmotePlayer::countMainTimelines() {
        return engine().countMainTimelines_guess();
    }

    ttstr D3DEmotePlayer::getMainTimelineLabelAt(tjs_int idx) {
        return engine().getMainTimelineLabelAt_guess(
            static_cast<tjs_uint32>(idx));
    }

    tjs_int D3DEmotePlayer::countDiffTimelines() {
        return engine().countDiffTimelines_guess();
    }

    ttstr D3DEmotePlayer::getDiffTimelineLabelAt(tjs_int idx) {
        return engine().getDiffTimelineLabelAt_guess(
            static_cast<tjs_uint32>(idx));
    }

    tjs_int D3DEmotePlayer::countPlayingTimelines() {
        return engine().countPlayingTimelines_guess();
    }

    ttstr D3DEmotePlayer::getPlayingTimelineLabelAt(tjs_int idx) {
        return engine().getPlayingTimelineLabelAt_guess(
            static_cast<tjs_uint32>(idx));
    }

    tjs_int D3DEmotePlayer::getPlayingTimelineFlagsAt(tjs_int idx) {
        return engine().getPlayingTimelineFlagsAt_guess(
            static_cast<tjs_uint32>(idx));
    }

    bool D3DEmotePlayer::isLoopTimeline(ttstr label) {
        return engine().getLoopTimeline_guess(label);
    }

    tjs_int D3DEmotePlayer::getTimelineTotalFrameCount(ttstr label) {
        return static_cast<tjs_int>(
            engine().getTimelineTotalFrameCount_guess(label));
    }

    void D3DEmotePlayer::playTimeline(ttstr label, tjs_int flags) {
        engine().playTimeline_guess(
            label, static_cast<tjs_uint32>(flags));
    }

    bool D3DEmotePlayer::isTimelinePlaying(ttstr label) {
        return engine().isTimelinePlaying_guess(label);
    }

    void D3DEmotePlayer::stopTimeline(ttstr label) {
        engine().stopTimeline_guess(label);
    }

    double D3DEmotePlayer::getTimelineBlendRatio(ttstr label) {
        return engine().getTimelineBlendRatio_guess(label);
    }

    void D3DEmotePlayer::fadeInTimeline(ttstr label, float duration,
                                        float easingWeight) {
        engine().fadeInTimeline_guess(label, duration, easingWeight);
    }

    void D3DEmotePlayer::fadeOutTimeline(ttstr label, float duration,
                                         float easingWeight) {
        engine().setTimelineBlendController_guess(
            label, 0.0f, duration, easingWeight, true);
    }

    void D3DEmotePlayer::setTimeline(ttstr label, float value,
                                     float transition, float easingWeight,
                                     bool autoStop) {
        engine().setTimelineBlendController_guess(
            label, value, transition, easingWeight, autoStop);
    }

    void D3DEmotePlayer::passTimelines_guess() {
        engine().passTimelines_guess();
    }

    void D3DEmotePlayer::skip() {
        engine().resetControllers_guess();
    }

    // D3D progress consumes frame units. Its shell is the only entry with a
    // zero-dt gate; a nonzero call performs one pointer hop to the shared Engine
    // core without writing any shell/Engine progress accumulator.
    void D3DEmotePlayer::progress(double dt) {
        if(dt != 0.0) {
            engine().progress(dt);
        }
    }

    void D3DEmotePlayer::setOuterForce(ttstr label, double x, double y,
                                       double duration, double power) {
        engine().setOuterForceTarget_guess(
            label, x, y, duration, power);
    }

    [[noreturn]] tTJSVariant D3DEmotePlayer::getOuterForce() {
        // All four current references intentionally expose this TODO boundary.
        throw eTJSError(
            ttstr(TJS_W("TODO: implement D3DEmotePlayer::getOuterForce()")));
    }

    bool D3DEmotePlayer::contains(ttstr label, double x, double y) {
        // The four facades resolve the raw label recursively and consume the
        // current geometry without visibility, empty-label or update gates.
        return player().hitTestLayerByRawLabel_guess(label, x, y);
    }

    // ============================================================
    // Motion.EmotePlayer — full NCB surface (70 members + 2 constants).
    // It is a parallel NCB facade over the same Player/EmoteEngine machine used
    // by Motion.Player and D3DEmotePlayer. Unlike D3DEmotePlayer, its adaptor
    // owns the Engine-sized payload directly, with no EmoteObject middle layer.
    // Each member therefore delegates straight to player()/engine().
    // ============================================================
    // The four references instantiate the typed one-Variant Factory family,
    // not ncbNativeClassFactory's raw callback overload. The generated wrapper
    // owns this by-value arg0, rejects an ordinary zero-argument call, accepts
    // and ignores surplus arguments, and reserves exactly one Void for its
    // empty-adaptor sentinel before this function is reached.
    EmotePlayer *EmotePlayer::factory(tTJSVariant rmDispatch) {
        return new EmotePlayer(rmDispatch);
    }

    // Motion.EmotePlayer progress consumes milliseconds, converts them to frame
    // units, and enters the shared Engine core even when the result is zero.
    void EmotePlayer::progress(double milliseconds) {
        const double frameDt = milliseconds * 60.0 / 1000.0;
        engine().progress(frameDt);
    }

    // The registrar stores this Primary wrapper, not Player::draw directly.
    // NCBind has already materialized its by-value argument; forwarding through
    // Player::draw creates the wrapper-owned local Variant consumed by the
    // native draw dispatcher. The caller's Variant remains borrowed/unchanged.
    void EmotePlayer::draw(tTJSVariant target) {
        player().draw(target);
    }

    // Unlike the D3D shell's five-zero setWind call, this member is a dedicated
    // delete-and-null body. Cached wind parameters deliberately survive.
    void EmotePlayer::stopWind() {
        delete engine()._windEmitter;
        engine()._windEmitter = nullptr;
    }

    // --- #7 play (Player_play_NCBWrapper) ---
    void EmotePlayer::play(ttstr label, tjs_int flags) {
        player().playMotion_guess(flags, label);
    }

    // --- #8 clear ---
    // Despite its script-visible name, all four native wrappers forward two
    // owned Variants to Player's gated recursive draw-to-layer body through a
    // typed NCB member. It is not a lifecycle operation and never destroys or
    // replaces the Engine payload.
    void EmotePlayer::clear(tTJSVariant target, tTJSVariant fill) {
        player().drawToLayerRecursive_guess(target, fill);
    }

    // --- #10 contains ---
    bool EmotePlayer::contains(ttstr label, double x, double y) {
        return player().hitTestLayerByRawLabel_guess(label, x, y);
    }

    // pass is a zero-argument timeline flush. It does not advance elapsed time
    // and does not share the progress/frameProgress call path.
    void EmotePlayer::pass() { engine().passTimelines_guess(); }

    // --- #14 setVariable ---
    //   The primary raw callback performs the script ease-to-power transform
    //   once before dispatching to EmoteEngine::setVariable; that common router
    //   applies the same transform again. The former Player-side value-map
    //   double-write was unrelated and remains removed: Engine HM7 and Player
    //   HM1/HM2 are bridged only by the progress bind-loop.
    void EmotePlayer::setVariable(ttstr label, double value, double transition,
                                  double ease) {
        // The primary EmotePlayer raw callback pre-transforms script ease as a
        // double; the general Engine router applies its own transform again.
        // D3DEmotePlayer is a different API surface and forwards ease directly.
        engine().setVariable(
            label, value, transition,
            scriptEaseToPowerDouble_guess(ease));
    }
    tjs_error EmotePlayer::setVariableCompat(tTJSVariant *, tjs_int numparams,
                                             tTJSVariant **param,
                                             EmotePlayer *nativeInstance) {
        if(numparams < 2) {
            return TJS_E_BADPARAMCOUNT;
        }
        // The four raw bodies convert in label -> value -> optional transition
        // -> optional ease order and trust every argv slot covered by argc.
        ttstr label(*param[0]);
        const double value = param[1]->AsReal();
        const double transition =
            numparams >= 3 ? param[2]->AsReal() : 0.0;
        const double ease =
            numparams >= 4 ? param[3]->AsReal() : 0.0;
        nativeInstance->setVariable(
            std::move(label), value, transition, ease);
        return TJS_S_OK;
    }

    // --- #15 setCoord ---
    void EmotePlayer::setCoord(double x, double y, double transition,
                               double ease) {
        const double power = scriptEaseToPowerDouble_guess(ease);
        const float values[2] = {
            static_cast<float>(x), static_cast<float>(y)
        };
        const float durationValue = static_cast<float>(transition);
        const float powerValue = static_cast<float>(power);
        auto *const controller = _ctlPosition.get();
        const bool append = _queuing;
        _dirty = true;
        EmoteVarController_setTarget_guess(
            controller, values, durationValue, powerValue, append);
    }
    tjs_error EmotePlayer::setCoordCompat(tTJSVariant *, tjs_int numparams,
                                          tTJSVariant **param,
                                          EmotePlayer *nativeInstance) {
        if(numparams < 2) {
            return TJS_E_BADPARAMCOUNT;
        }
        const double x = param[0]->AsReal();
        const double y = param[1]->AsReal();
        const double transition =
            numparams >= 3 ? param[2]->AsReal() : 0.0;
        const double ease =
            numparams >= 4 ? param[3]->AsReal() : 0.0;
        nativeInstance->setCoord(x, y, transition, ease);
        return TJS_S_OK;
    }

    // --- #16 setScale ---
    void EmotePlayer::setScale(double s, double transition, double ease) {
        const double power = scriptEaseToPowerDouble_guess(ease);
        const float value = static_cast<float>(s);
        const float durationValue = static_cast<float>(transition);
        const float powerValue = static_cast<float>(power);
        auto *const controller = _ctlScale.get();
        const bool append = _queuing;
        _dirty = true;
        EmoteVarController_setTarget_guess(
            controller, &value, durationValue, powerValue, append);
    }
    tjs_error EmotePlayer::setScaleCompat(tTJSVariant *, tjs_int numparams,
                                          tTJSVariant **param,
                                          EmotePlayer *nativeInstance) {
        if(numparams < 1) {
            return TJS_E_BADPARAMCOUNT;
        }
        const double scale = param[0]->AsReal();
        const double transition =
            numparams >= 2 ? param[1]->AsReal() : 0.0;
        const double ease =
            numparams >= 3 ? param[2]->AsReal() : 0.0;
        nativeInstance->setScale(scale, transition, ease);
        return TJS_S_OK;
    }

    // --- #17 setRotate ---
    void EmotePlayer::setRotate(double rot, double transition, double ease) {
        const double power = scriptEaseToPowerDouble_guess(ease);
        const float angle = static_cast<float>(rot);
        const float durationValue = static_cast<float>(transition);
        const float powerValue = static_cast<float>(power);
        auto *const controller = _ctlAngle.get();
        const bool append = _queuing;
        _dirty = true;
        EmoteAngleController_setTarget_guess(
            controller, angle, durationValue, powerValue, append);
    }
    tjs_error EmotePlayer::setRotateCompat(tTJSVariant *, tjs_int numparams,
                                           tTJSVariant **param,
                                           EmotePlayer *nativeInstance) {
        if(numparams < 1) {
            return TJS_E_BADPARAMCOUNT;
        }
        const double rotation = param[0]->AsReal();
        const double transition =
            numparams >= 2 ? param[1]->AsReal() : 0.0;
        const double ease =
            numparams >= 3 ? param[2]->AsReal() : 0.0;
        nativeInstance->setRotate(rotation, transition, ease);
        return TJS_S_OK;
    }

    // --- #18 setColor ---
    void EmotePlayer::setColor(tjs_int color, double transition, double ease) {
        const double power = scriptEaseToPowerDouble_guess(ease);
        const auto packed = static_cast<tjs_uint32>(color);
        const float values[4] = {
            static_cast<float>(static_cast<std::uint8_t>(packed)),
            static_cast<float>(static_cast<std::uint8_t>(packed >> 8)),
            static_cast<float>(static_cast<std::uint8_t>(packed >> 16)),
            static_cast<float>(static_cast<std::uint8_t>(packed >> 24))
        };
        const float durationValue = static_cast<float>(transition);
        const float powerValue = static_cast<float>(power);
        auto *const controller = _ctlColor.get();
        const bool append = _queuing;
        _dirty = true;
        EmoteVarController_setTarget_guess(
            controller, values, durationValue, powerValue, append);
    }
    tjs_error EmotePlayer::setColorCompat(tTJSVariant *, tjs_int numparams,
                                          tTJSVariant **param,
                                          EmotePlayer *nativeInstance) {
        if(numparams < 1) {
            return TJS_E_BADPARAMCOUNT;
        }
        const tjs_int color = param[0]->AsInteger();
        const double transition =
            numparams >= 2 ? param[1]->AsReal() : 0.0;
        const double ease =
            numparams >= 3 ? param[2]->AsReal() : 0.0;
        nativeInstance->setColor(color, transition, ease);
        return TJS_S_OK;
    }

    // --- #19 setOuterForce ---
    void EmotePlayer::setOuterForce(ttstr label, double x, double y,
                                    double transition, double ease) {
        setOuterForceTarget_guess(
            label, x, y, transition,
            scriptEaseToPowerDouble_guess(ease));
    }
    tjs_error EmotePlayer::setOuterForceCompat(tTJSVariant *, tjs_int numparams,
                                               tTJSVariant **param,
                                               EmotePlayer *nativeInstance) {
        if(numparams < 3) {
            return TJS_E_BADPARAMCOUNT;
        }
        ttstr label(*param[0]);
        const double x = param[1]->AsReal();
        const double y = param[2]->AsReal();
        const double transition =
            numparams >= 4 ? param[3]->AsReal() : 0.0;
        const double ease =
            numparams >= 5 ? param[4]->AsReal() : 0.0;
        nativeInstance->setOuterForce(
            std::move(label), x, y, transition, ease);
        return TJS_S_OK;
    }

    // --- #26 meshDivisionRatio property setter ---
    void EmotePlayer::setMeshDivisionRatio(double v) {
        player().setMeshDivisionRatio(v);
    }

    bool EmotePlayer::setDrawAffineTranslateMatrix(
        double m11, double m21, double m12, double m22,
        double m14, double m24) {
        return player().setDrawAffineTranslateMatrix(
            m11, m21, m12, m22, m14, m24);
    }

    tTJSVariant EmotePlayer::getCameraOffset() { return player().getCameraOffset(); }

    void EmotePlayer::setCameraOffset(double x, double y) {
        player().setCameraOffset(x, y);
    }

    void EmotePlayer::modifyRoot() { player().modifyRoot(); }

    // setHairScale/setPartsScale/setBustScale (#39-41) are inline in the header
    // and write the three consecutive Engine-owned scale values directly.

    // --- #53-58 native-instance raw timeline callbacks ---
    tjs_error EmotePlayer::playTimelineRawCallback_guess(
        tTJSVariant *, tjs_int numparams, tTJSVariant **param,
        EmotePlayer *nativeInstance) {
        if(numparams < 1) {
            return TJS_E_BADPARAMCOUNT;
        }
        const ttstr label(*param[0]);
        const tjs_uint32 flags = numparams >= 2
            ? static_cast<tjs_uint32>(param[1]->AsInteger())
            : 0u;
        nativeInstance->engine().playTimeline_guess(label, flags);
        return TJS_S_OK;
    }

    tjs_error EmotePlayer::stopTimelineRawCallback_guess(
        tTJSVariant *, tjs_int numparams, tTJSVariant **param,
        EmotePlayer *nativeInstance) {
        ttstr label;
        if(numparams >= 1) {
            label = ttstr(*param[0]);
        }
        nativeInstance->engine().stopTimeline_guess(label);
        return TJS_S_OK;
    }

    tjs_error EmotePlayer::getTimelinePlayingRawCallback_guess(
        tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
        EmotePlayer *nativeInstance) {
        ttstr label;
        if(numparams >= 1) {
            label = ttstr(*param[0]);
        }
        // The four raw bodies publish through the supplied result slot
        // unconditionally; the callback itself has no null-result branch.
        *result = nativeInstance->engine().isTimelinePlaying_guess(label);
        return TJS_S_OK;
    }

    tjs_error EmotePlayer::setTimelineBlendRatioRawCallback_guess(
        tTJSVariant *, tjs_int numparams, tTJSVariant **param,
        EmotePlayer *nativeInstance) {
        if(numparams < 1) {
            return TJS_E_BADPARAMCOUNT;
        }
        const ttstr label(*param[0]);
        EmoteEngine &engine = nativeInstance->engine();
        if(!engine.isTimelinePlaying_guess(label)) {
            // This compatibility entry intentionally stops here. Unlike the
            // adjacent fade-in callback, it does not enqueue the final 1.0
            // target on the first/non-playing call and does not inspect any
            // optional arguments on this branch.
            engine.playTimeline_guess(label, 3u);
            engine.setTimelineBlendController_guess(
                label, 0.0f, 0.0f, 1.0f, false);
            return TJS_S_OK;
        }
        const float duration = numparams >= 2
            ? static_cast<float>(param[1]->AsReal())
            : 0.0f;
        const float easingWeight = numparams >= 3
            ? scriptEaseToPower_guess(param[2]->AsReal())
            : 1.0f;
        const bool autoStop = numparams >= 4
            ? param[3]->operator bool()
            : false;
        engine.setTimelineBlendController_guess(
            label, 1.0f, duration, easingWeight, autoStop);
        return TJS_S_OK;
    }

    tjs_error EmotePlayer::fadeInTimelineRawCallback_guess(
        tTJSVariant *, tjs_int numparams, tTJSVariant **param,
        EmotePlayer *nativeInstance) {
        if(numparams < 1) {
            return TJS_E_BADPARAMCOUNT;
        }
        // The four native callbacks finish each conversion before advancing
        // to the next argv slot: label owner, narrowed duration, then the
        // double-domain script-ease mapping narrowed to controller precision.
        const ttstr label(*param[0]);
        const float duration = numparams >= 2
            ? static_cast<float>(param[1]->AsReal())
            : 0.0f;
        const float easingWeight = numparams >= 3
            ? scriptEaseToPower_guess(param[2]->AsReal())
            : 1.0f;
        nativeInstance->engine().fadeInTimeline_guess(
            label, duration, easingWeight);
        return TJS_S_OK;
    }

    tjs_error EmotePlayer::fadeOutTimelineRawCallback_guess(
        tTJSVariant *, tjs_int numparams, tTJSVariant **param,
        EmotePlayer *nativeInstance) {
        if(numparams < 1) {
            return TJS_E_BADPARAMCOUNT;
        }
        const ttstr label(*param[0]);
        const float duration = numparams >= 2
            ? static_cast<float>(param[1]->AsReal())
            : 0.0f;
        const float easingWeight = numparams >= 3
            ? scriptEaseToPower_guess(param[2]->AsReal())
            : 1.0f;
        nativeInstance->engine().setTimelineBlendController_guess(
            label, 0.0f, duration, easingWeight, true);
        return TJS_S_OK;
    }

    // --- #60-61 variable query lists ---
    tTJSVariant EmotePlayer::getVariableRange(ttstr label) {
        // An Engine variable-range hit returns a fresh Dictionary from the
        // stored frame extrema; miss delegates to the recursive Player
        // parameter-range query.
        if(const auto it = engine()._variableRanges.find(label);
           it != engine()._variableRanges.end()) {
            iTJSDispatch2 *dispatch = TJSCreateDictionaryObject();
            tTJSVariant dictionary(dispatch, dispatch);
            dispatch->Release();

            tTJSVariant objectValue(dictionary);
            objectValue.ToObject();
            ncbPropAccessor object(objectValue);
            objectValue.Clear();

            (void)object.SetValue(
                TJS_W("min"), it->second.frameMin, TJS_MEMBERENSURE,
                &detail::emoteVariableRangeMinHint_guess);
            (void)object.SetValue(
                TJS_W("max"), it->second.frameMax, TJS_MEMBERENSURE,
                &detail::emoteVariableRangeMaxHint_guess);

            tTJSVariant returned(dictionary);
            return returned;
        }
        return player().getVariableRange_guess(label);
    }
    tTJSVariant EmotePlayer::getVariableFrameList(ttstr label) {
        // Copy/force the Engine Dictionary, transfer its lifetime to one
        // retained dispatch owner, and release the copied Object closure before
        // the property getter can re-enter script code.
        tTJSVariant frameListsValue(engine()._variableFrameLists);
        frameListsValue.ToObject();
        ncbPropAccessor frameLists(frameListsValue);
        frameListsValue.Clear();

        tTJSVariant result;
        iTJSDispatch2 *dispatch = frameLists.GetDispatch();
        (void)dispatch->PropGet(
            0, label.c_str(), label.GetHint(), &result, dispatch);

        // The four native bodies CopyRef the getter output into the hidden
        // return object, destroy the output temporary, then release dispatch.
        tTJSVariant returned(result);
        return returned;
    }
    // --- #70 getCommandList (delegates to the embedded Player) ---
    tTJSVariant EmotePlayer::getCommandList() { return player().getCommandList(); }

} // namespace motion
