//
// Created by LiDon on 2025/9/15.
// Aligned to libkrkr2.so D3DEmotePlayer architecture:
// EmotePlayer is a thin shell delegating all animation logic to an owned Player.
// Binary: D3DEmotePlayerNativeInstance(24b) → EmoteObject(40b) → Player(1496b)
//

#include <algorithm>

#include "EmotePlayer.h"
#include "RuntimeSupport.h"
#include "ncbind.hpp"

#define LOGGER spdlog::get("plugin")
#define STUB_WARN(name) LOGGER->warn("EmotePlayer::" #name "() stub called")

namespace motion {

    // Aligned to libkrkr2.so D3DEmotePlayer 对象链:壳持有 EmoteObject(+24),
    // EmoteObject 持有 EmoteEngine(+8),EmoteEngine 在 +1064 持有堆分配的 Player。
    // 二进制在 load() 时懒建此链;本地构造期即建(eager),功能等价。
    EmotePlayer::EmotePlayer(ResourceManager rm) :
        _emoteObj(std::make_unique<EmoteObject>(std::move(rm))) {}

    EmotePlayer::~EmotePlayer() = default;

    // --- Properties ---

    void EmotePlayer::setVisible(bool v) {
        _visible = v;
        player().setVisible(v);
    }

    void EmotePlayer::setMeshDivisionRatio(double v) {
        engine()._meshDivisionRatio = v;
        player().setEmoteMeshDivisionRatio(v);
    }

    bool EmotePlayer::getAnimating() const {
        return player().getAllplaying();
    }

    void EmotePlayer::setModule(tTJSVariant v) {
        obj()._module = v;
        // Bridge loaded PSB snapshot into Player's animation pipeline.
        // Aligned to libkrkr2.so EmoteObject_init (sub_67DBAC):
        // After loading PSBs, the EmoteObject initializes its internal Player
        // with the loaded motion data.
        auto snapshot = detail::lookupModuleSnapshot(obj()._module);
        if(snapshot) {
            player().loadFromSnapshot(snapshot);
        }
    }

    tTJSVariant EmotePlayer::getModule() const { return obj()._module; }

    // --- Methods ---

    // Aligned to libkrkr2.so sub_52FD84: create() is actually "destroy/reset"
    void EmotePlayer::create() {
        obj()._module.Clear();
        player().loadFromSnapshot(nullptr);
        engine()._modified = true;
    }

    void EmotePlayer::load(tTJSVariant data) {
        obj()._module = data;
        auto snapshot = detail::lookupModuleSnapshot(obj()._module);
        if(snapshot) {
            player().loadFromSnapshot(snapshot);
        }
        engine()._modified = true;
    }

    tTJSVariant EmotePlayer::clone() {
        typedef ncbInstanceAdaptor<EmotePlayer> AdaptorT;

        auto *copy = new EmotePlayer(ResourceManager{});
        // 壳层字段(EmotePlayer 自身)
        copy->_useD3D = _useD3D;
        copy->_smoothing = _smoothing;
        copy->_drawVisible = _drawVisible;
        copy->_drawOpacity = _drawOpacity;
        copy->_opengl = _opengl;
        copy->_visible = _visible;
        copy->_baseScale = _baseScale;
        copy->_userScale = _userScale;
        // EmoteObject 层
        copy->obj()._module = obj()._module;
        // EmoteEngine 层(引擎字段 + getScale/Rot/Color 缓存)
        copy->engine()._meshDivisionRatio = engine()._meshDivisionRatio;
        copy->engine()._queuing = engine()._queuing;
        copy->engine()._hairScale = engine()._hairScale;
        copy->engine()._partsScale = engine()._partsScale;
        copy->engine()._bustScale = engine()._bustScale;
        copy->engine()._bodyScale = engine()._bodyScale;
        copy->engine()._progress = engine()._progress;
        copy->engine()._modified = engine()._modified;
        copy->engine()._playCallback = engine()._playCallback;
        copy->engine()._rot = engine()._rot;
        copy->engine()._coordX = engine()._coordX;
        copy->engine()._coordY = engine()._coordY;
        copy->engine()._mirrorBase = engine()._mirrorBase;
        copy->engine()._mirrorRequested = engine()._mirrorRequested;
        copy->engine()._mirrorChanged = engine()._mirrorChanged;
        copy->engine()._color = engine()._color;

        // Load the same snapshot into the cloned Player
        auto snapshot = detail::lookupModuleSnapshot(obj()._module);
        if(snapshot) {
            copy->player().loadFromSnapshot(snapshot);
        }

        tTJSVariant result;
        if(iTJSDispatch2 *adaptor = AdaptorT::CreateAdaptor(copy)) {
            result = tTJSVariant(adaptor, adaptor);
            adaptor->Release();
        } else {
            delete copy;
        }
        return result;
    }

    void EmotePlayer::show() {
        _visible = true;
        player().setVisible(true);
    }

    void EmotePlayer::hide() {
        _visible = false;
        player().setVisible(false);
    }

    void EmotePlayer::assignState() { STUB_WARN(assignState); }
    void EmotePlayer::initPhysics() { STUB_WARN(initPhysics); }

    // Aligned to libkrkr2.so sub_5302E4: delegates to Player's rotAnimator
    void EmotePlayer::setRot(double rot, double transition, double ease) {
        engine()._rot = rot;
        player().setRotate(rot, transition, ease);
        engine()._modified = true;
    }

    tjs_error EmotePlayer::setRotCompat(tTJSVariant *, tjs_int numparams,
                                        tTJSVariant **param,
                                        iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(numparams < 1 || !param[0]) {
            return TJS_E_INVALIDPARAM;
        }

        const double transition =
            (numparams >= 2 && param[1]) ? param[1]->AsReal() : 0.0;
        const double ease =
            (numparams >= 3 && param[2]) ? param[2]->AsReal() : 0.0;
        self->setRot(param[0]->AsReal(), transition, ease);
        return TJS_S_OK;
    }

    // Aligned to libkrkr2.so sub_53030C: binary returns hardcoded 0.0
    double EmotePlayer::getRot() { return 0.0; }

    // Aligned to libkrkr2.so sub_5301EC: delegates to Player's coordAnimator
    void EmotePlayer::setCoord(double x, double y, double transition,
                               double ease) {
        engine()._coordX = x;
        engine()._coordY = y;
        player().setEmoteCoord(x, y, transition, ease);
        engine()._modified = true;
    }

    tjs_error EmotePlayer::setCoordCompat(tTJSVariant *, tjs_int numparams,
                                          tTJSVariant **param,
                                          iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(numparams < 2 || !param[0] || !param[1]) {
            return TJS_E_INVALIDPARAM;
        }

        const double transition =
            (numparams >= 3 && param[2]) ? param[2]->AsReal() : 0.0;
        const double ease =
            (numparams >= 4 && param[3]) ? param[3]->AsReal() : 0.0;
        self->setCoord(param[0]->AsReal(), param[1]->AsReal(),
                       transition, ease);
        return TJS_S_OK;
    }

    // Aligned to libkrkr2.so sub_530260: finalScale = baseScale * userScale
    void EmotePlayer::setScale(double s, double transition, double ease) {
        _userScale = static_cast<float>(s);
        const double finalScale =
            static_cast<double>(_baseScale) * static_cast<double>(_userScale);
        player().setEmoteScale(finalScale, transition, ease);
        engine()._modified = true;
    }

    tjs_error EmotePlayer::setScaleCompat(tTJSVariant *, tjs_int numparams,
                                          tTJSVariant **param,
                                          iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(numparams < 1 || !param[0]) {
            return TJS_E_INVALIDPARAM;
        }

        const double transition =
            (numparams >= 2 && param[1]) ? param[1]->AsReal() : 0.0;
        const double ease =
            (numparams >= 3 && param[2]) ? param[2]->AsReal() : 0.0;
        self->setScale(param[0]->AsReal(), transition, ease);
        return TJS_S_OK;
    }

    // Aligned to libkrkr2.so sub_5302DC: binary returns hardcoded 1.0
    double EmotePlayer::getScale() { return 1.0; }

    void EmotePlayer::setMirror(bool mirror) {
        // Aligned to libkrkr2.so sub_671DB0:
        // wrapper stores requested mirror, derives a root-flip delta against a
        // baseline bit, forwards that effective flip to Player_setRootFlipX,
        // then triggers the large controller reset path.
        engine()._mirrorRequested = mirror;
        engine()._mirrorChanged = (engine()._mirrorRequested != engine()._mirrorBase);
        player().setMirror(engine()._mirrorChanged);
        engine()._modified = true;
    }

    void EmotePlayer::setColor(tjs_int color, double transition, double ease) {
        engine()._color = color;
        player().setEmoteColor(static_cast<tjs_uint32>(color), transition, ease);
        engine()._modified = true;
    }

    tjs_error EmotePlayer::setColorCompat(tTJSVariant *, tjs_int numparams,
                                          tTJSVariant **param,
                                          iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(numparams < 1 || !param[0]) {
            return TJS_E_INVALIDPARAM;
        }

        const double transition =
            (numparams >= 2 && param[1]) ? param[1]->AsReal() : 0.0;
        const double ease =
            (numparams >= 3 && param[2]) ? param[2]->AsReal() : 0.0;
        self->setColor(param[0]->AsInteger(), transition, ease);
        return TJS_S_OK;
    }
    // Aligned to libkrkr2.so sub_530320: binary returns hardcoded 0
    tjs_int EmotePlayer::getColor() { return 0; }

    // --- Variable system: delegates to Player ---
    // Aligned to libkrkr2.so sub_5305C8 → sub_671228:
    // wrapper forwards label/value/transition/ease into Player_setVariable.
    void EmotePlayer::setVariable(ttstr label, double value, double transition,
                                  double ease) {
        player().setVariable(label, value, transition, ease);
        engine()._modified = true;
    }

    tjs_error EmotePlayer::setVariableCompat(tTJSVariant *, tjs_int numparams,
                                             tTJSVariant **param,
                                             iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(numparams < 2 || !param[0] || !param[1]) {
            return TJS_E_INVALIDPARAM;
        }

        const double transition =
            (numparams >= 3 && param[2]) ? param[2]->AsReal() : 0.0;
        const double ease =
            (numparams >= 4 && param[3]) ? param[3]->AsReal() : 0.0;
        self->setVariable(ttstr(*param[0]), param[1]->AsReal(), transition,
                          ease);
        return TJS_S_OK;
    }

    double EmotePlayer::getVariable(ttstr label) {
        return player().getVariable(label);
    }

    tjs_int EmotePlayer::countVariables() {
        return player().countVariables();
    }

    ttstr EmotePlayer::getVariableLabelAt(tjs_int idx) {
        return player().getVariableLabelAt(idx);
    }

    tjs_int EmotePlayer::countVariableFrameAt(tjs_int idx) {
        return player().countVariableFrameAt(idx);
    }

    ttstr EmotePlayer::getVariableFrameLabelAt(tjs_int idx, tjs_int frameIdx) {
        return player().getVariableFrameLabelAt(idx, frameIdx);
    }

    double EmotePlayer::getVariableFrameValueAt(tjs_int idx, tjs_int frameIdx) {
        return player().getVariableFrameValueAt(idx, frameIdx);
    }

    // --- Wind/Force ---
    void EmotePlayer::startWind(double minAngle, double maxAngle,
                                double amplitude, double freqX,
                                double freqY) {
        player().startWind(minAngle, maxAngle, amplitude, freqX, freqY);
        engine()._modified = true;
    }

    tjs_error EmotePlayer::startWindCompat(tTJSVariant *, tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(numparams < 5 || !param[0] || !param[1] || !param[2] ||
           !param[3] || !param[4]) {
            return TJS_E_INVALIDPARAM;
        }

        self->startWind(param[0]->AsReal(), param[1]->AsReal(),
                        param[2]->AsReal(), param[3]->AsReal(),
                        param[4]->AsReal());
        return TJS_S_OK;
    }

    void EmotePlayer::stopWind() {
        player().stopWind();
        engine()._modified = true;
    }

    tjs_error EmotePlayer::stopWindCompat(tTJSVariant *, tjs_int,
                                          tTJSVariant **,
                                          iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        self->stopWind();
        return TJS_S_OK;
    }

    // --- Timeline methods: delegate to Player ---

    tjs_int EmotePlayer::countMainTimelines() {
        return player().countMainTimelines();
    }

    ttstr EmotePlayer::getMainTimelineLabelAt(tjs_int idx) {
        return player().getMainTimelineLabelAt(idx);
    }

    tjs_int EmotePlayer::countDiffTimelines() {
        return player().countDiffTimelines();
    }

    ttstr EmotePlayer::getDiffTimelineLabelAt(tjs_int idx) {
        return player().getDiffTimelineLabelAt(idx);
    }

    tjs_int EmotePlayer::countPlayingTimelines() {
        return player().countPlayingTimelines();
    }

    ttstr EmotePlayer::getPlayingTimelineLabelAt(tjs_int idx) {
        return player().getPlayingTimelineLabelAt(idx);
    }

    tjs_int EmotePlayer::getPlayingTimelineFlagsAt(tjs_int idx) {
        return player().getPlayingTimelineFlagsAt(idx);
    }

    bool EmotePlayer::isLoopTimeline(ttstr label) {
        return player().getLoopTimeline(label);
    }

    tjs_int EmotePlayer::getTimelineTotalFrameCount(ttstr label) {
        return player().getTimelineTotalFrameCount(label);
    }

    void EmotePlayer::playTimeline(ttstr label, tjs_int flags) {
        player().playTimeline(label, flags);
        engine()._modified = true;
    }

    bool EmotePlayer::isTimelinePlaying(ttstr label) {
        return player().getTimelinePlaying(label);
    }

    void EmotePlayer::stopTimeline(ttstr label) {
        player().stopTimeline(label);
    }

    void EmotePlayer::setTimelineBlendRatio(ttstr label, double ratio) {
        player().setTimelineBlendRatio(label, ratio);
    }

    double EmotePlayer::getTimelineBlendRatio(ttstr label) {
        return player().getTimelineBlendRatio(label);
    }

    void EmotePlayer::fadeInTimeline(ttstr label, double duration,
                                     tjs_int flags) {
        player().fadeInTimeline(label, duration, flags);
    }

    void EmotePlayer::fadeOutTimeline(ttstr label, double duration,
                                      tjs_int flags) {
        player().fadeOutTimeline(label, duration, flags);
    }

    void EmotePlayer::setTimeline(ttstr label, bool loop) {
        // Player doesn't have an exact equivalent; use playTimeline + loop flag
        player().playTimeline(label, 0);
    }

    bool EmotePlayer::play(ttstr label, tjs_int flags) {
        const bool started = player().playMotionLike_0x6B2284(label, flags);
        engine()._modified = true;
        return started;
    }

    void EmotePlayer::draw(tTJSVariant target) {
        player().draw(target);
        engine()._modified = true;
    }

    tjs_error EmotePlayer::setDrawAffineTranslateMatrixCompat(
        tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
        iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        return Player::setDrawAffineTranslateMatrixCompat(
            result, numparams, param, &self->player());
    }

    void EmotePlayer::addPlayCallback() {
        engine()._playCallback = true;
    }

    void EmotePlayer::skip() {
        // Aligned to libkrkr2.so sub_66EB8C: skip to end of all timelines
        // Player doesn't expose skip() directly, stop all timelines
        player().stopTimeline(TJS_W(""));
    }

    // Aligned to libkrkr2.so sub_6818B4 -> sub_6D2A54:
    // after wrapper-side animators, EmotePlayer advances its owned Player and
    // immediately updates layers/calcBounds.
    void EmotePlayer::pass(double dt) {
        engine()._progress += dt;
        player().progressMsLike_0x6D2A54(dt);
        engine()._modified = true;
    }

    void EmotePlayer::progress(double dt) {
        pass(dt);
    }

    // Aligned to libkrkr2.so sub_672D58: routes by label to bust/h/parts
    void EmotePlayer::setOuterForce(double x, double y) {
        setOuterForce(TJS_W("bust"), x, y, 0.0, 0.0);
    }

    void EmotePlayer::setOuterForce(ttstr label, double x, double y,
                                    double transition, double ease) {
        player().setOuterForce(label, x, y, transition, ease);
        engine()._modified = true;
    }

    tjs_error EmotePlayer::setOuterForceCompat(tTJSVariant *, tjs_int numparams,
                                               tTJSVariant **param,
                                               iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(numparams < 3 || !param[0] || !param[1] || !param[2]) {
            return TJS_E_INVALIDPARAM;
        }

        const ttstr label(*param[0]);
        const double transition =
            (numparams >= 4 && param[3]) ? param[3]->AsReal() : 0.0;
        const double ease =
            (numparams >= 5 && param[4]) ? param[4]->AsReal() : 0.0;
        self->setOuterForce(label, param[1]->AsReal(), param[2]->AsReal(),
                            transition, ease);
        return TJS_S_OK;
    }

    tTJSVariant EmotePlayer::getOuterForce() {
        STUB_WARN(getOuterForce);
        return tTJSVariant();
    }

    bool EmotePlayer::contains(double x, double y) {
        if(!_visible) {
            return false;
        }

        // Use local coordinate state for AABB test.
        // Aligned to libkrkr2.so sub_690DF0: supports circle/rect/quad;
        // we use AABB approximation for now.
        const double scale = static_cast<double>(_baseScale * _userScale);
        const double width = player().getActiveMotionWidth();
        const double height = player().getActiveMotionHeight();
        if(width <= 0.0 || height <= 0.0) {
            return false;
        }

        const auto scaledWidth = width * scale;
        const auto scaledHeight = height * scale;
        return x >= engine()._coordX && x <= (engine()._coordX + scaledWidth) &&
               y >= engine()._coordY && y <= (engine()._coordY + scaledHeight);
    }

    bool EmotePlayer::contains(ttstr label, double x, double y) {
        if(!_visible || label.IsEmpty()) {
            return false;
        }
        return player().hitTestLayer(label, x, y);
    }

    tjs_error EmotePlayer::containsCompat(tTJSVariant *result, tjs_int numparams,
                                          tTJSVariant **param,
                                          iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<EmotePlayer>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(!result) {
            return TJS_E_INVALIDPARAM;
        }

        if(numparams >= 3 && param[0] && param[1] && param[2]) {
            *result = tTJSVariant(
                self->contains(ttstr(*param[0]),
                               param[1]->AsReal(),
                               param[2]->AsReal()));
            return TJS_S_OK;
        }
        if(numparams >= 2 && param[0] && param[1]) {
            *result = tTJSVariant(
                self->contains(param[0]->AsReal(), param[1]->AsReal()));
            return TJS_S_OK;
        }
        return TJS_E_INVALIDPARAM;
    }

} // namespace motion
