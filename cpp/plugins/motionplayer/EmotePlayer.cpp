//
// Created by LiDon on 2025/9/15.
// Aligned to libkrkr2.so D3DEmotePlayer architecture:
// D3DEmotePlayer is a standalone NCB class (≥56B, D3DEmotePlayer_ncb_register
// @ 0x541D98) — NOT a subclass of EmotePlayer. The two are independent NCB
// classes in the binary, registered separately.
// Object chain: D3DEmotePlayer(≥56B) → EmoteObject(40B) → EmoteEngine(1496B) → Player
//

#include <algorithm>

#include "EmotePlayer.h"
#include "RuntimeSupport.h"
#include "ncbind.hpp"

#define LOGGER spdlog::get("plugin")
#define STUB_WARN(name) LOGGER->warn("D3DEmotePlayer::" #name "() stub called")

namespace motion {

    // EmoteObject ctor/dtor: raw EmoteEngine* with manual new/delete (aligned
    // with libkrkr2.so EmoteObject_init sub_67DBAC which does
    // `operator new(0x5D8); EmoteEngine_ctor(...)`).
    EmoteObject::EmoteObject(ResourceManager rm) {
        _engine = new EmoteEngine(std::move(rm));
    }

    EmoteObject::~EmoteObject() {
        delete _engine;
        _engine = nullptr;
    }

    // Aligned to libkrkr2.so D3DEmotePlayer 对象链:壳持有【两个】EmoteObject 槽
    // (主 instance+24 / 次 instance+32),EmoteObject 持有 EmoteEngine(+8),
    // EmoteEngine 在 +1064 持有堆分配的 Player。
    // COMMIT-2:懒建。二进制 plain 构造(TJS `new Motion.D3DEmotePlayer`)留主槽
    // null,只在 load(0x52FDD4)/clone(sub_52FFBC via sub_67F978)时才
    // operator new(0x28) 建主槽。证据:全部已反编译路径中,仅 load 与 clone 建
    // 主槽 EmoteObject;plain ctor 不建。_rm 保存供 load/clone 重建。
    // 访问器无 null 守卫(与二进制 EmoteEngine_progress 一致),靠调用时序保证
    // construct 后必先 load 再访问主槽。
    D3DEmotePlayer::D3DEmotePlayer(ResourceManager rm) :
        _rm(rm) {}

    // 析构 = 二进制 sub_533C00:依次拆次槽 +32、主槽 +24(各 EmoteObject_destroy
    // + operator delete)。EmoteObject* 裸指针手动 delete,无智能指针。
    D3DEmotePlayer::~D3DEmotePlayer() {
        delete _secondaryObj;
        _secondaryObj = nullptr;
        delete _primaryObj;
        _primaryObj = nullptr;
    }

    // --- Properties ---

    void D3DEmotePlayer::setVisible(bool v) {
        _visible = v;
        player().setVisible(v);
    }

    void D3DEmotePlayer::setMeshDivisionRatio(double v) {
        engine()._meshDivisionRatio = v;
        player().setEmoteMeshDivisionRatio(v);
    }

    bool D3DEmotePlayer::getAnimating() const {
        return player().getAllplaying();
    }

    void D3DEmotePlayer::setModule(tTJSVariant v) {
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

    tTJSVariant D3DEmotePlayer::getModule() const { return obj()._module; }

    // --- Methods ---

    // Aligned to libkrkr2.so D3DEmotePlayer_create @0x52FD84 (注册名 "clear"):
    //   if(+32){ EmoteObject_destroy(+32); delete; }
    //   if(+24){ EmoteObject_destroy(+24); delete; }
    //   +24 = 0; +32 = 0;
    // 纯对象拆除 —— 销毁两个 EmoteObject 槽并置 null,不碰任何 motion cursor/
    // 帧推进状态(in-place reset 猜测版曾导致帧推进 hang, 见 revert 9587e2c)。
    // 拆除后下一次 load 重建主槽。
    void D3DEmotePlayer::create() {
        delete _secondaryObj;
        _secondaryObj = nullptr;
        delete _primaryObj;
        _primaryObj = nullptr;
    }

    // Aligned to libkrkr2.so D3DEmotePlayer_load @0x52FDD4:
    //   拆除前半 == create(destroy +32/+24, 置 null), 再重建主槽:
    //   v16 = operator new(0x28); EmoteObject_init(v16, &args); +24 = v16;
    // 只重建主槽(+24), 次槽(+32)保持 null。
    void D3DEmotePlayer::load(tTJSVariant data) {
        // teardown 双槽(== create)
        delete _secondaryObj;
        _secondaryObj = nullptr;
        delete _primaryObj;
        _primaryObj = nullptr;
        // 重建主槽(二进制 operator new(0x28) + EmoteObject_init(args))
        _primaryObj = new EmoteObject(_rm);
        obj()._module = data;
        auto snapshot = detail::lookupModuleSnapshot(obj()._module);
        if(snapshot) {
            player().loadFromSnapshot(snapshot);
        }
        engine()._modified = true;
    }

    tTJSVariant D3DEmotePlayer::clone() {
        typedef ncbInstanceAdaptor<D3DEmotePlayer> AdaptorT;

        auto *copy = new D3DEmotePlayer(ResourceManager{});
        // 懒建后 copy 主槽为 null;clone 需显式建主槽 —— 对齐二进制 sub_52FFBC
        // clone 回调内 `+24 = sub_67F978(...)`(operator new(0x28)+EmoteObject_init)。
        copy->_primaryObj = new EmoteObject(copy->_rm);
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
        // R3 phantom: _queuing is Player+480 (Player class), not EmoteEngine.
        copy->player().setQueuing(player().getQueuing());
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

    void D3DEmotePlayer::show() {
        _visible = true;
        player().setVisible(true);
    }

    void D3DEmotePlayer::hide() {
        _visible = false;
        player().setVisible(false);
    }

    void D3DEmotePlayer::assignState() { STUB_WARN(assignState); }
    void D3DEmotePlayer::initPhysics() { STUB_WARN(initPhysics); }

    // Aligned to libkrkr2.so sub_5302E4: delegates to Player's rotAnimator
    void D3DEmotePlayer::setRot(double rot, double transition, double ease) {
        engine()._rot = rot;
        player().setRotate(rot, transition, ease);
        engine()._modified = true;
    }

    tjs_error D3DEmotePlayer::setRotCompat(tTJSVariant *, tjs_int numparams,
                                        tTJSVariant **param,
                                        iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<D3DEmotePlayer>::GetNativeInstance(objthis, true);
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
    double D3DEmotePlayer::getRot() { return 0.0; }

    // Aligned to libkrkr2.so sub_5301EC: delegates to Player's coordAnimator
    void D3DEmotePlayer::setCoord(double x, double y, double transition,
                               double ease) {
        engine()._coordX = x;
        engine()._coordY = y;
        player().setEmoteCoord(x, y, transition, ease);
        engine()._modified = true;
    }

    tjs_error D3DEmotePlayer::setCoordCompat(tTJSVariant *, tjs_int numparams,
                                          tTJSVariant **param,
                                          iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<D3DEmotePlayer>::GetNativeInstance(objthis, true);
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
    void D3DEmotePlayer::setScale(double s, double transition, double ease) {
        _userScale = static_cast<float>(s);
        const double finalScale =
            static_cast<double>(_baseScale) * static_cast<double>(_userScale);
        player().setEmoteScale(finalScale, transition, ease);
        engine()._modified = true;
    }

    tjs_error D3DEmotePlayer::setScaleCompat(tTJSVariant *, tjs_int numparams,
                                          tTJSVariant **param,
                                          iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<D3DEmotePlayer>::GetNativeInstance(objthis, true);
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
    double D3DEmotePlayer::getScale() { return 1.0; }

    void D3DEmotePlayer::setMirror(bool mirror) {
        // Aligned to libkrkr2.so sub_671DB0:
        // wrapper stores requested mirror, derives a root-flip delta against a
        // baseline bit, forwards that effective flip to Player_setRootFlipX,
        // then triggers the large controller reset path.
        engine()._mirrorRequested = mirror;
        engine()._mirrorChanged = (engine()._mirrorRequested != engine()._mirrorBase);
        player().setMirror(engine()._mirrorChanged);
        engine()._modified = true;
    }

    void D3DEmotePlayer::setColor(tjs_int color, double transition, double ease) {
        engine()._color = color;
        player().setEmoteColor(static_cast<tjs_uint32>(color), transition, ease);
        engine()._modified = true;
    }

    tjs_error D3DEmotePlayer::setColorCompat(tTJSVariant *, tjs_int numparams,
                                          tTJSVariant **param,
                                          iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<D3DEmotePlayer>::GetNativeInstance(objthis, true);
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
    tjs_int D3DEmotePlayer::getColor() { return 0; }

    // --- Variable system ---
    // Aligned to libkrkr2.so sub_5305C8 → Player_setVariable @0x671228. The
    //   0x671228 `this` is the EmoteEngine (HM6@+1384 / HM2@+1440 / controller
    //   deques@+256..+656), so the faithful dispatch is EmoteEngine::setVariable.
    //   Binary arg mapping: (value=d0, easing=d1 [TJS "transition", the instant
    //   gate], durationFrames=d2 [TJS "ease", the transition-factor driver]).
    //
    //   engine().setVariable IS the keystone that drives the eye/eyebrow/mouth/
    //   transition/selector controllers (cases 4-8) and the type-0/1/2 HM2 write.
    //
    //   PORT FORK (documented, in-scope boundary): local getVariable reads the
    //   Player-side cascade map (_evalResultValues / HM1 / HM4 — the M3/R0-1
    //   subsystem modelling binary getVariable @0x6D69C8), NOT the engine's
    //   HM2 (+1440 = _labelToValueHM7) that 0x671228 actually writes. In the
    //   binary these are the SAME HM2; locally they are two disjoint maps. Until
    //   that map unification (a separate M3-scope refactor, out of this slice's
    //   "setVariable cases 4-8" boundary) is done, the Player-side
    //   setVariableResolvedWeightLike_0x671228 call is retained so the existing
    //   getVariable round-trip (and its unit test) keep working. The engine call
    //   added here is what actually activates the controller deques; its HM2
    //   fallthrough writes _labelToValueHM7 (read by the controller-step HM7
    //   path, inert for scalar getVariable). No double-corruption: the two HM2
    //   surfaces are read by disjoint consumers.
    void D3DEmotePlayer::setVariable(ttstr label, double value, double transition,
                                  double ease) {
        engine().setVariable(label, value, transition, ease); // 0x671228 dispatch
        player().setVariable(label, value, transition, ease); // getVariable cascade (M3 fork)
        engine()._modified = true;
    }

    tjs_error D3DEmotePlayer::setVariableCompat(tTJSVariant *, tjs_int numparams,
                                             tTJSVariant **param,
                                             iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<D3DEmotePlayer>::GetNativeInstance(objthis, true);
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

    double D3DEmotePlayer::getVariable(ttstr label) {
        return player().getVariable(label);
    }

    tjs_int D3DEmotePlayer::countVariables() {
        return player().countVariables();
    }

    ttstr D3DEmotePlayer::getVariableLabelAt(tjs_int idx) {
        return player().getVariableLabelAt(idx);
    }

    tjs_int D3DEmotePlayer::countVariableFrameAt(tjs_int idx) {
        return player().countVariableFrameAt(idx);
    }

    ttstr D3DEmotePlayer::getVariableFrameLabelAt(tjs_int idx, tjs_int frameIdx) {
        return player().getVariableFrameLabelAt(idx, frameIdx);
    }

    double D3DEmotePlayer::getVariableFrameValueAt(tjs_int idx, tjs_int frameIdx) {
        return player().getVariableFrameValueAt(idx, frameIdx);
    }

    // --- Wind/Force ---
    void D3DEmotePlayer::startWind(double minAngle, double maxAngle,
                                double amplitude, double freqX,
                                double freqY) {
        player().startWind(minAngle, maxAngle, amplitude, freqX, freqY);
        engine()._modified = true;
    }

    tjs_error D3DEmotePlayer::startWindCompat(tTJSVariant *, tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<D3DEmotePlayer>::GetNativeInstance(objthis, true);
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

    void D3DEmotePlayer::stopWind() {
        player().stopWind();
        engine()._modified = true;
    }

    tjs_error D3DEmotePlayer::stopWindCompat(tTJSVariant *, tjs_int,
                                          tTJSVariant **,
                                          iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<D3DEmotePlayer>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        self->stopWind();
        return TJS_S_OK;
    }

    // --- Timeline methods: delegate to Player ---

    tjs_int D3DEmotePlayer::countMainTimelines() {
        return player().countMainTimelines();
    }

    ttstr D3DEmotePlayer::getMainTimelineLabelAt(tjs_int idx) {
        return player().getMainTimelineLabelAt(idx);
    }

    tjs_int D3DEmotePlayer::countDiffTimelines() {
        return player().countDiffTimelines();
    }

    ttstr D3DEmotePlayer::getDiffTimelineLabelAt(tjs_int idx) {
        return player().getDiffTimelineLabelAt(idx);
    }

    tjs_int D3DEmotePlayer::countPlayingTimelines() {
        return player().countPlayingTimelines();
    }

    ttstr D3DEmotePlayer::getPlayingTimelineLabelAt(tjs_int idx) {
        return player().getPlayingTimelineLabelAt(idx);
    }

    tjs_int D3DEmotePlayer::getPlayingTimelineFlagsAt(tjs_int idx) {
        return player().getPlayingTimelineFlagsAt(idx);
    }

    bool D3DEmotePlayer::isLoopTimeline(ttstr label) {
        return player().getLoopTimeline(label);
    }

    tjs_int D3DEmotePlayer::getTimelineTotalFrameCount(ttstr label) {
        return player().getTimelineTotalFrameCount(label);
    }

    void D3DEmotePlayer::playTimeline(ttstr label, tjs_int flags) {
        player().playTimeline(label, flags);
        engine()._modified = true;
    }

    bool D3DEmotePlayer::isTimelinePlaying(ttstr label) {
        return player().getTimelinePlaying(label);
    }

    void D3DEmotePlayer::stopTimeline(ttstr label) {
        player().stopTimeline(label);
    }

    void D3DEmotePlayer::setTimelineBlendRatio(ttstr label, double ratio) {
        player().setTimelineBlendRatio(label, ratio);
    }

    double D3DEmotePlayer::getTimelineBlendRatio(ttstr label) {
        return player().getTimelineBlendRatio(label);
    }

    void D3DEmotePlayer::fadeInTimeline(ttstr label, double duration,
                                     tjs_int flags) {
        player().fadeInTimeline(label, duration, flags);
    }

    void D3DEmotePlayer::fadeOutTimeline(ttstr label, double duration,
                                      tjs_int flags) {
        player().fadeOutTimeline(label, duration, flags);
    }

    void D3DEmotePlayer::setTimeline(ttstr label, bool loop) {
        // Player doesn't have an exact equivalent; use playTimeline + loop flag
        player().playTimeline(label, 0);
    }

    bool D3DEmotePlayer::play(ttstr label, tjs_int flags) {
        const bool started = player().playMotionLike_0x6B2284(label, flags);
        engine()._modified = true;
        return started;
    }

    void D3DEmotePlayer::draw(tTJSVariant target) {
        player().draw(target);
        engine()._modified = true;
    }

    tjs_error D3DEmotePlayer::setDrawAffineTranslateMatrixCompat(
        tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
        iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<D3DEmotePlayer>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        return Player::setDrawAffineTranslateMatrixCompat(
            result, numparams, param, &self->player());
    }

    void D3DEmotePlayer::addPlayCallback() {
        engine()._playCallback = true;
    }

    void D3DEmotePlayer::skip() {
        // Aligned to libkrkr2.so sub_66EB8C: skip to end of all timelines
        // Player doesn't expose skip() directly, stop all timelines
        player().stopTimeline(TJS_W(""));
    }

    // Aligned to libkrkr2.so sub_6818B4 -> sub_6D2A54:
    // after wrapper-side animators, EmotePlayer advances its owned Player and
    // immediately updates layers/calcBounds.
    void D3DEmotePlayer::pass(double dt) {
        engine()._progress += dt;
        player().progressMsLike_0x6D2A54(dt);
        engine()._modified = true;
    }

    void D3DEmotePlayer::progress(double dt) {
        pass(dt);
    }

    // Aligned to libkrkr2.so sub_672D58: routes by label to bust/h/parts
    void D3DEmotePlayer::setOuterForce(double x, double y) {
        setOuterForce(TJS_W("bust"), x, y, 0.0, 0.0);
    }

    void D3DEmotePlayer::setOuterForce(ttstr label, double x, double y,
                                    double transition, double ease) {
        player().setOuterForce(label, x, y, transition, ease);
        engine()._modified = true;
    }

    tjs_error D3DEmotePlayer::setOuterForceCompat(tTJSVariant *, tjs_int numparams,
                                               tTJSVariant **param,
                                               iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<D3DEmotePlayer>::GetNativeInstance(objthis, true);
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

    tTJSVariant D3DEmotePlayer::getOuterForce() {
        STUB_WARN(getOuterForce);
        return tTJSVariant();
    }

    // M11 D-09 P0: removed `contains(double x, double y)` AABB overload — this
    // was a port invention. libkrkr2.so sub_530b5c (D3DEmotePlayer::contains)
    // has EXACTLY one code path: AddRef label-variant -> sub_6B5AD8 resolve
    // layer node -> if node: Player_hitTest(node+1664, x, y) else 0 -> Release.
    // No AABB branch, no _visible guard, no IsEmpty guard. CLAUDE.md mandates
    // 1:1 reproduction.

    bool D3DEmotePlayer::contains(ttstr label, double x, double y) {
        // M11 D-09 P1: removed _visible / label.IsEmpty() guards — binary
        // unconditionally resolves layer + hitTests (returns 0 if resolve
        // fails). Visibility and empty-label cases are handled by the layer
        // resolver returning null, which hitTestLayer then maps to false.
        return player().hitTestLayer(label, x, y);
    }

    tjs_error D3DEmotePlayer::containsCompat(tTJSVariant *result, tjs_int numparams,
                                          tTJSVariant **param,
                                          iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<D3DEmotePlayer>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(!result) {
            return TJS_E_INVALIDPARAM;
        }

        // M11 D-09: binary contains is (label, x, y) only. 2-arg AABB form
        // is removed — TJS callers passing 2 args now get INVALIDPARAM.
        if(numparams >= 3 && param[0] && param[1] && param[2]) {
            *result = tTJSVariant(
                self->contains(ttstr(*param[0]),
                               param[1]->AsReal(),
                               param[2]->AsReal()));
            return TJS_S_OK;
        }
        return TJS_E_INVALIDPARAM;
    }

} // namespace motion
