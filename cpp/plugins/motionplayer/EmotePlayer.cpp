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

    // EmoteObject ctor/dtor — aligned with libkrkr2.so EmoteObject_init
    // @0x67DBAC. The binary, in order:
    //   1. operator new(0xE8) RM -> sub_6A88CC(rm,...) -> store EmoteObject+0
    //   2. sub_67E20C(rm,...) wraps RM in a TJS dispatch (2x AddRef)
    //   3. operator new(0x5D8) EmoteEngine -> EmoteEngine_ctor(engine, &wrapper)
    //      -> store EmoteObject+8 (Player+1064 gets the RM dispatch wrapper)
    //   4. VariantPtrVector_assign_67F0CC(EmoteObject+16, psbArgs)
    // G2-A: EmoteObject now self-owns the RM (member _rm, initialized first),
    //   then constructs the EmoteEngine from a copy of it — the binary owns RM
    //   at +0 and passes a wrapper down; the local ResourceManager value type
    //   models that ownership (shared_ptr<State>, cheap copy).
    EmoteObject::EmoteObject(ResourceManager rm) :
        _rm(std::move(rm)) {
        _engine = new EmoteEngine(_rm);
    }

    // Dtor — aligned with libkrkr2.so EmoteObject_destroy @0x67F420. Order:
    //   1. EmoteObject+8 EmoteEngine: sub_67F4B8 + operator delete
    //   2. EmoteObject+0 ResourceManager: sub_6A8B94 + operator delete
    //   3. EmoteObject+16 vector: per-element Release + delete buffer
    // Local: delete _engine FIRST, then RM (member, destroyed after body) and
    //   _modules (vector member) tear down in reverse-declaration order
    //   (_engine declared after _rm; vector member cleanup is implicit). The
    //   explicit `delete _engine` enforces the engine-before-RM order that the
    //   binary's dtor body performs (engine teardown reads RM-derived state).
    EmoteObject::~EmoteObject() {
        delete _engine;
        _engine = nullptr;
        // _modules (vector<tTJSVariant>) and _rm release after this body via
        //   member destruction — matches binary steps 2/3 (RM then vector).
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
        // G2-B: EmoteObject+16 is a vector<tTJSVariant*> (PSB reference array,
        //   D3DEmotePlayer_load @0x52FDD4 pushes each PSB; EmoteObject_init
        //   @0x67DBAC step4 assigns the whole array). setModule installs a
        //   single PSB reference -> size()==1 (equivalent to the old single
        //   variant). The vector teardown/Release happens in EmoteObject dtor.
        obj()._modules.assign(1, v);
        // Bridge loaded PSB snapshot into Player's animation pipeline.
        // Aligned to libkrkr2.so EmoteObject_init (sub_67DBAC):
        // After loading PSBs, the EmoteObject initializes its internal Player
        // with the loaded motion data.
        auto snapshot = detail::lookupModuleSnapshot(obj()._modules.front());
        if(snapshot) {
            player().loadFromSnapshot(snapshot);
        }
    }

    // getModule returns the representative (first) loaded PSB reference, or
    //   void when none loaded — single-PSB case mirrors the old single variant.
    tTJSVariant D3DEmotePlayer::getModule() const {
        return obj()._modules.empty() ? tTJSVariant() : obj()._modules.front();
    }

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
        // G2-B: assign the loaded PSB into the +16 vector (single PSB here).
        obj()._modules.assign(1, data);
        auto snapshot = detail::lookupModuleSnapshot(obj()._modules.front());
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
        // EmoteObject 层 — G2-B: copy the whole +16 PSB reference vector.
        copy->obj()._modules = obj()._modules;
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

        // Load the same snapshot into the cloned Player (G2-B: first PSB).
        auto snapshot = obj()._modules.empty()
                            ? nullptr
                            : detail::lookupModuleSnapshot(obj()._modules.front());
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
    // Aligned to libkrkr2.so sub_5305C8 → EmoteEngine_setVariable @0x671228. The
    //   0x671228 `this` is the EmoteEngine (HM6@+1384 / HM7@+1440 / controller
    //   deques@+256..+656), so the faithful dispatch is the single
    //   EmoteEngine::setVariable call. Binary arg mapping: (value=d0,
    //   easing=d1 [TJS "transition", the instant gate], durationFrames=d2 [TJS
    //   "ease", the transition-factor driver]).
    //
    //   engine().setVariable IS the keystone that drives the eye/eyebrow/mouth/
    //   transition/selector controllers (cases 4-8) and the type-0/1/2 HM7 write.
    //
    //   DISJOINT-MAP TOPOLOGY (fresh-decompile 2026-06-03, confirmed faithful):
    //   setVariable@0x671228 writes the EmoteEngine HM7 (+1440 = _labelToValueHM7;
    //   miss path @0x67135c: Player_HM2_upsert_labelToValue(this+1440,key)=value).
    //   getVariable@0x533E1C reads a DIFFERENT object: the inner Player at
    //   *(a1+1064) → scope scan (sub_6CD16C) → Player HM1 (+264) /
    //   HM4(+1240)→HM2; it NEVER reads EmoteEngine+1440. So in the binary,
    //   setVariable(x) then getVariable() with NO progress() in between does NOT
    //   return x — HM7 and Player HM1/HM2 are two disjoint maps. The ONLY bridge
    //   is the progress() bind-loop (EmoteEngine::progress post-loop, G2-C LIVE):
    //   HM7 → sub_67C560 (accumulate) → sub_67C6B0 (mirror) →
    //   Player_bindParameterValue @0x6C4668 → Player HM1/HM2. There is NO
    //   Player-side double-write at this site in the binary; the prior
    //   `player().setVariable(...)` shim was a non-faithful local invention that
    //   defeated the disjoint-map architecture, now removed.
    void D3DEmotePlayer::setVariable(ttstr label, double value, double transition,
                                  double ease) {
        engine().setVariable(label, value, transition, ease); // 0x671228 dispatch
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

    // Aligned to libkrkr2.so D3DEmotePlayer NCB "progress" member
    // (registration @0x52f76c) whose callback is EmoteEngine_progress @0x67D01C
    // with this=EmoteEngine. The binary D3DEmotePlayer.progress is LITERALLY
    // EmoteEngine_progress (no ms->frame conversion — dt arrives in frame units
    // from the TJS caller), which runs: preProgress -> 6 controller-deque steps
    // -> applyVarControllers -> wind gate -> G2-C bind-loop (HM7 -> Player
    // HM1/HM2) -> sub_67C8A8 -> sub_6D2A54(Player,0,frameDt) [step 7, Player
    // progress] -> bust/hair physics. Routing through engine().progress makes the
    // G2-C bind-loop RUNTIME-LIVE (previously this called
    // player().progressMsLike_0x6D2A54 directly, bypassing the engine entirely so
    // EmoteEngine::progress had no live caller and the bind-loop was dead code).
    // The Player-level progress is now driven exactly once, from inside
    // engine().progress step 7 — NOT here — so Player progress is not double-run.
    void D3DEmotePlayer::progress(double dt) {
        engine()._progress += dt; // local getProgress() accumulator (not in binary)
        engine().progress(static_cast<float>(dt)); // EmoteEngine_progress @0x67D01C
        engine()._modified = true;
    }

    // C++-side pass(double): NOT the binary NCB "pass" member (that name is bound
    // to addPlayCallback @0x52f730 — the play-callback setter, NOT a progress
    // driver). This pass() is a local progress-driver convenience (used by the
    // unit test as a frame stepper); route it through the same engine().progress
    // so it exercises the bind-loop identically to progress().
    void D3DEmotePlayer::pass(double dt) {
        progress(dt);
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

    // ============================================================
    // Motion.EmotePlayer — full NCB surface (68 members + 2 consts)
    //   Aligned with libkrkr2.so EmotePlayer_ncb_registerMembers @0x67FAC8.
    //   Binary EmotePlayer is a parallel NCB facade over the SAME Player/
    //   EmoteEngine machine as Motion.Player @0x6D69C8 and D3DEmotePlayer
    //   @0x52E504 (member callbacks are the same Player_*/sub_* fns operating
    //   on the underlying object — progress=sub_6818B4 -> Player_preProgress).
    //   Local EmotePlayer reaches that machine via the same EmoteObject chain
    //   D3DEmotePlayer uses (EmoteObject -> EmoteEngine -> Player). Each member
    //   delegates to player()/engine(), matching the binary's per-member fn.
    // ============================================================
#undef STUB_WARN
#define STUB_WARN(name) LOGGER->warn("EmotePlayer::" #name "() stub called")

    // dtor — lazy primary slot teardown (binary EmotePlayer native instance is a
    //   24B shell whose +8 EmoteEngine is destroyed by sub_67F4B8; local routes
    //   that teardown through the EmoteObject chain delete).
    EmotePlayer::~EmotePlayer() {
        delete _primaryObj;
        _primaryObj = nullptr;
    }

    // --- #1 progress (sub_6818B4). The binary EmotePlayer.progress NCB member is
    //   EmotePlayer_progress_sub_6818B4 @0x6818B4, which is an INLINED copy of
    //   EmoteEngine_progress @0x67D01C with ONE prologue difference: it converts
    //   the incoming dt from MILLISECONDS to FRAME units first
    //   (`a2 = a2 * 60.0 / 1000.0` @0x6818c8) and THEN runs the identical engine
    //   body (preProgress / 6 deque steps / applyVarControllers / wind / G2-C
    //   bind-loop / sub_67C8A8 / sub_6D2A54 / bust-hair physics). So faithfully:
    //   EmotePlayer.progress(dt_ms) == EmoteEngine::progress(dt_ms * 60/1000).
    //   Routing through engine().progress makes the G2-C bind-loop runtime-live
    //   (was bypassed via a direct player().progressMsLike call). Player progress
    //   is driven once, inside engine().progress step 7 — not here.
    void EmotePlayer::progress(double dt) {
        engine()._progress += dt; // local getProgress() accumulator (not in binary)
        // sub_6818B4 @0x6818c8: dt(ms) -> frame units, then engine body.
        const double frameDt = dt * 60.0 / 1000.0;
        engine().progress(static_cast<float>(frameDt)); // EmoteEngine_progress body
        engine()._modified = true;
    }

    // --- #2 frameProgress (sub_6817C0) ---
    void EmotePlayer::frameProgress(double dt) { player().frameProgress(dt); }

    // --- #3 draw (Player_draw_NCBWrapper) ---
    void EmotePlayer::draw(tTJSVariant target) {
        player().draw(target);
        engine()._modified = true;
    }

    // --- #4 initPhysics (sub_67D4D0) — open: physics builder not yet ported ---
    void EmotePlayer::initPhysics() { STUB_WARN(initPhysics); }

    // --- #5 startWind (Player_startWind) ---
    void EmotePlayer::startWind(double minAngle, double maxAngle, double amplitude,
                                double freqX, double freqY) {
        player().startWind(minAngle, maxAngle, amplitude, freqX, freqY);
    }

    // --- #6 stopWind (sub_681A38) ---
    void EmotePlayer::stopWind() { player().stopWind(); }

    // --- #7 play (Player_play_NCBWrapper) ---
    bool EmotePlayer::play(ttstr label, tjs_int flags) {
        const bool started = player().playMotionLike_0x6B2284(label, flags);
        engine()._modified = true;
        return started;
    }

    // --- #8 clear (sub_681A64) — tear down the primary slot (binary destroys the
    //   +8 EmoteEngine; local deletes the EmoteObject chain and re-lazy-builds) ---
    void EmotePlayer::clear() {
        delete _primaryObj;
        _primaryObj = nullptr;
    }

    // --- #9 getVariable (Player_getVariable_wrapper) ---
    double EmotePlayer::getVariable(ttstr label) {
        return player().getVariable(label);
    }

    // --- #10 contains (sub_681B0C -> hitTestLayer) ---
    bool EmotePlayer::contains(ttstr label, double x, double y) {
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
            *result = tTJSVariant(self->contains(
                ttstr(*param[0]), param[1]->AsReal(), param[2]->AsReal()));
            return TJS_S_OK;
        }
        return TJS_E_INVALIDPARAM;
    }

    // --- #11 serialize (sub_675E40 -> Player_serialize) ---
    tTJSVariant EmotePlayer::serialize() { return player().serialize(); }

    // --- #12 unserialize (sub_678044 -> Player_unserialize) ---
    void EmotePlayer::unserialize(tTJSVariant data) { player().unserialize(data); }

    // --- #13 pass (sub_681C48) — same progress driver as progress (binary
    //   shares the sub_6818B4 advance body) ---
    void EmotePlayer::pass(double dt) { progress(dt); }

    // --- #14 setVariable (sub_671DF0 -> EmoteEngine setVariable keystone) ---
    //   Single faithful dispatch into EmoteEngine::setVariable @0x671228. The
    //   former `player().setVariable(...)` double-write was a non-faithful local
    //   invention (the binary's HM7 and Player HM1/HM2 are disjoint maps bridged
    //   only by the progress() bind-loop — see D3DEmotePlayer::setVariable).
    void EmotePlayer::setVariable(ttstr label, double value, double transition,
                                  double ease) {
        engine().setVariable(label, value, transition, ease);
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
        self->setVariable(ttstr(*param[0]), param[1]->AsReal(), transition, ease);
        return TJS_S_OK;
    }

    // --- #15 setCoord (sub_672060) ---
    void EmotePlayer::setCoord(double x, double y, double, double) {
        player().setCoord(x, y);
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
        self->setCoord(param[0]->AsReal(), param[1]->AsReal(), transition, ease);
        return TJS_S_OK;
    }

    // --- #16 setScale (sub_67231C) ---
    void EmotePlayer::setScale(double s, double transition, double ease) {
        player().setEmoteScale(s, transition, ease);
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

    // --- #17 setRotate (sub_672568) ---
    void EmotePlayer::setRotate(double rot, double transition, double ease) {
        engine()._rot = rot;
        player().setRotate(rot, transition, ease);
        engine()._modified = true;
    }
    tjs_error EmotePlayer::setRotateCompat(tTJSVariant *, tjs_int numparams,
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
        self->setRotate(param[0]->AsReal(), transition, ease);
        return TJS_S_OK;
    }

    // --- #18 setColor (sub_67277C) ---
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

    // --- #19 setOuterForce (sub_672A78) ---
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

    // --- #26 meshDivisionRatio property setter (sub_681DDC) ---
    void EmotePlayer::setMeshDivisionRatio(double v) {
        engine()._meshDivisionRatio = v;
        player().setEmoteMeshDivisionRatio(v);
    }

    // --- #35 setDrawAffineTranslateMatrix (-> Player) ---
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

    // --- #36 getCameraOffset (sub_681EF0 -> Player_getCameraOffset) ---
    tTJSVariant EmotePlayer::getCameraOffset() { return player().getCameraOffset(); }

    // --- #35 setCameraOffset (sub_681EF8 -> raw float write player+144/+148) ---
    void EmotePlayer::setCameraOffset(double x, double y) {
        player().setCameraOffsetXY_0x681EF8(x, y);
    }

    // --- #38 modifyRoot (sub_681F0C): NO args — sets flag byte
    //   *(Player+1064 -> +200 -> +1584) = 1. Distinct from Motion.Player's
    //   modifyRoot(tTJSVariant). open: the +200/+1584 root-modify flag is a
    //   Player-internal field not yet surfaced as a named setter; faithful
    //   thin set deferred until that field is modelled. ---
    void EmotePlayer::modifyRoot() { STUB_WARN(modifyRoot); }

    // setHairScale/setPartsScale/setBustScale (#39-41, sub_681F20/28/30) are
    //   inline in the header (engine +1184/+1192/+1200 raw writes).

    // --- #49 setMirror (sub_671DB0) ---
    void EmotePlayer::setMirror(bool mirror) {
        engine()._mirrorRequested = mirror;
        engine()._mirrorChanged =
            (engine()._mirrorRequested != engine()._mirrorBase);
        player().setMirror(engine()._mirrorChanged);
        engine()._modified = true;
    }

    // --- #50 skip (sub_66EB8C) ---
    void EmotePlayer::skip() { player().stopTimeline(TJS_W("")); }

    // --- #51-57 timeline methods ---
    void EmotePlayer::playTimeline(ttstr label, tjs_int flags) {
        player().playTimeline(label, flags);
        engine()._modified = true;
    }
    void EmotePlayer::stopTimeline(ttstr label) { player().stopTimeline(label); }
    bool EmotePlayer::getTimelinePlaying(ttstr label) {
        return player().getTimelinePlaying(label);
    }
    void EmotePlayer::setTimelineBlendRatio(ttstr label, double ratio) {
        player().setTimelineBlendRatio(label, ratio);
    }
    void EmotePlayer::fadeInTimeline(ttstr label, double duration, tjs_int flags) {
        player().fadeInTimeline(label, duration, flags);
    }
    void EmotePlayer::fadeOutTimeline(ttstr label, double duration, tjs_int flags) {
        player().fadeOutTimeline(label, duration, flags);
    }
    double EmotePlayer::getTimelineBlendRatio(ttstr label) {
        return player().getTimelineBlendRatio(label);
    }

    // --- #58-64 variable/timeline query lists ---
    tTJSVariant EmotePlayer::getVariableRange(ttstr label) {
        return player().getVariableRange(label);
    }
    tTJSVariant EmotePlayer::getVariableFrameList(ttstr label) {
        return player().getVariableFrameList(label);
    }
    tTJSVariant EmotePlayer::getMainTimelineLabelList() {
        return player().getMainTimelineLabelList();
    }
    tTJSVariant EmotePlayer::getDiffTimelineLabelList() {
        return player().getDiffTimelineLabelList();
    }
    // #62 getLoopTimeline (sub_67522C). binary returns the loop-timeline label
    //   query result; local Player exposes a bool getLoopTimeline(label).
    tTJSVariant EmotePlayer::getLoopTimeline(ttstr label) {
        return tTJSVariant(player().getLoopTimeline(label));
    }
    tjs_int EmotePlayer::getTimelineTotalFrameCount(ttstr label) {
        return player().getTimelineTotalFrameCount(label);
    }
    tTJSVariant EmotePlayer::getPlayingTimelineInfoList() {
        return player().getPlayingTimelineInfoList();
    }

    // --- #65-67 selector methods ---
    bool EmotePlayer::isSelectorTarget(ttstr label) {
        return player().isSelectorTarget(label);
    }
    // #66 activateSelectorTarget (sub_67581C): scans the selector deque, finds
    //   the option matching `label`, snapshots its keyframe set into the active
    //   selector, then re-steps the selector + transition deques. Player has no
    //   activate entry yet (only deactivate @0x675BF4) — open: faithful selector
    //   activation needs the deque option-scan + applySelection re-step ported.
    void EmotePlayer::activateSelectorTarget(ttstr label) {
        STUB_WARN(activateSelectorTarget);
    }
    void EmotePlayer::deactivateSelectorTarget(ttstr label) {
        player().deactivateSelectorTarget(label);
    }

    // --- #68 getCommandList (sub_682520 -> Player_getCommandList) ---
    tTJSVariant EmotePlayer::getCommandList() { return player().getCommandList(); }

} // namespace motion
