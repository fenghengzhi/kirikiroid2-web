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
#include "MotionDispatch.h"
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
    //   4. ttstrVector_assign_67F0CC(EmoteObject+16, modulePaths)
    EmoteObject::EmoteObject(const std::vector<ttstr> &modulePaths) {
        // EmoteObject_init @0x67DBAC: operator new(0xE8), construct the sole
        // ResourceManager from global.kag/default spec, and store its pointer at
        // EmoteObject+0. It is not copied from the D3DEmotePlayer shell.
        _rm = new ResourceManager();
        // P3-B (2026-06-05): step 2 of EmoteObject_init @0x67DBAC —
        //   `sub_67E20C(rm, 1, 0)` wraps that exact native RM and sets the adaptor
        //   sticky flag. The dispatch therefore never deletes RM; EmoteObject
        //   remains its sole owner while the same pointer flows through
        //   EmoteEngine -> Player -> child Players.
        using RMAdaptor = ncbInstanceAdaptor<ResourceManager>;
        if(auto *dispatch = RMAdaptor::CreateAdaptor(_rm, true)) {
            _rmDispatch = tTJSVariant(dispatch, dispatch);
            dispatch->Release();
        }
        _engine = new EmoteEngine(_rmDispatch);
        // ttstrVector_assign_67F0CC is called only after both heap objects and
        // the RM dispatch are established; preserve that refcount/throw order.
        _modulePaths = modulePaths;

        // EmoteObject_init @0x67DCB0..0x67DFA0: load all paths, use the last raw
        // module as metadata source, seed Player+1012 from the last INPUT path,
        // apply chara, force-play motion, then apply the full metadata dict.
        tTJSVariant loaded;
        for(const auto &path : _modulePaths) {
            loaded = _rm->load(path);
        }
        const auto metadata =
            detail::motionPropGet(loaded, TJS_W("metadata"));
        const auto base =
            detail::motionPropGet(metadata, TJS_W("base"));
        const ttstr chara(
            detail::motionPropGet(base, TJS_W("chara")));
        const ttstr motionName(
            detail::motionPropGet(base, TJS_W("motion")));

        auto &player = _engine->player();
        player.setProject(tTJSVariant(_modulePaths.back()));
        player.setChara(chara);
        player.playMotionLike_0x6B2284(motionName, PlayFlagForce);
        _engine->applyMetadataLike_0x67D4D0(metadata);
    }

    // Dtor — aligned with libkrkr2.so EmoteObject_destroy @0x67F420. Order:
    //   1. EmoteObject+8 EmoteEngine: sub_67F4B8 + operator delete
    //   2. EmoteObject+0 ResourceManager: sub_6A8B94 + operator delete
    //   3. EmoteObject+16 vector: per-element Release + delete buffer
    // Local follows the same manual order. Clearing the sticky facade does not
    // delete RM; it only invalidates the remaining TJS wrapper before the sole
    // native owner is destroyed. _modulePaths is destroyed after this body.
    EmoteObject::~EmoteObject() {
        delete _engine;
        _engine = nullptr;
        _rmDispatch.Clear();
        delete _rm;
        _rm = nullptr;
    }

    // Aligned to libkrkr2.so D3DEmotePlayer 对象链:壳持有【两个】EmoteObject 槽
    // (主 instance+24 / 次 instance+32),EmoteObject 持有 EmoteEngine(+8),
    // EmoteEngine 在 +1064 持有堆分配的 Player。
    // COMMIT-2:懒建。二进制 plain 构造(TJS `new Motion.D3DEmotePlayer`)留主槽
    // null,只在 load(0x52FDD4)/clone(sub_52FFBC via sub_67F978)时才
    // operator new(0x28) 建主槽。证据:全部已反编译路径中,仅 load 与 clone 建
    // 主槽 EmoteObject;plain ctor 不建。load/clone 让 EmoteObject_init 自建其
    // 唯一 ResourceManager。D3DEmotePlayer native-create sub_542764 / unwrap
    // sub_5428D8 instead require a D3DImage owner; the shell has no RM field.
    // 访问器无 null 守卫(与二进制 EmoteEngine_progress 一致),靠调用时序保证
    // construct 后必先 load 再访问主槽。
    D3DEmotePlayer::D3DEmotePlayer(D3DLayerObject *d3dImageOwner) :
        _d3dImageOwner(d3dImageOwner) {
        // D3DEmotePlayer native-create @0x542764: store the raw D3DImage
        // pointer, then register this listener through owner vtable +48 before
        // either EmoteObject slot is initialized.
        if(_d3dImageOwner)
            _d3dImageOwner->AddListener(this);
    }

    // sub_5428D8: arg0 must expose the NCB class ID whose descriptor is mapped
    // to binary literal L"D3DImage" by sub_42C7F8. Missing/null objects and a
    // different native class follow the binary's two exception boundaries.
    tjs_error D3DEmotePlayer::factory(D3DEmotePlayer **result,
                                      tjs_int numparams,
                                      tTJSVariant **param,
                                      iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        if(param[0]->Type() != tvtObject)
            TVPThrowExceptionMessage(TJS_W("No instance."));
        iTJSDispatch2 *owner = param[0]->AsObjectNoAddRef();
        if(!owner)
            TVPThrowExceptionMessage(TJS_W("No instance."));

        D3DLayerObject *nativeOwner = TVPGetD3DImageNative(owner);
        if(!nativeOwner) {
            TVPThrowExceptionMessage(TJS_W("Invalid instance type."));
        }

        *result = new D3DEmotePlayer(nativeOwner);
        return TJS_S_OK;
    }

    // 析构 = 二进制 sub_533C00:依次拆次槽 +32、主槽 +24(各 EmoteObject_destroy
    // + operator delete)。EmoteObject* 裸指针手动 delete,无智能指针。
    D3DEmotePlayer::~D3DEmotePlayer() {
        delete _secondaryObj;
        _secondaryObj = nullptr;
        delete _primaryObj;
        _primaryObj = nullptr;
        // Binary sub_533C00 invokes owner vtable +56 only after both slots are
        // gone. The owner is non-owning; there is no script-object Release.
        if(_d3dImageOwner)
            _d3dImageOwner->RemoveListener(this);
        _d3dImageOwner = nullptr;
    }

    // D3DEmotePlayer listener slot +16 @0x533CBC. D3DImage invokes this from
    // matrix-change and OnUpdate fan-out. The binary compares exact floats and
    // only dereferences the primary EmoteObject when the owner scale changed.
    bool D3DEmotePlayer::IsVisible() {
        const float ownerScale = TVPGetD3DImageScaleX(_d3dImageOwner);
        if(_baseScale != ownerScale) {
            _baseScale = ownerScale;
            const float finalScale = _baseScale * _userScale;
            player().setEmoteScale(static_cast<double>(finalScale), 0.0, 1.0);
        }
        return true;
    }

    // D3DEmotePlayer listener slot +24 @0x533D4C: transform the zero origin
    // through D3DImage, then enter Player_drawToTexture @0x6D5C68 with the
    // compositor's native target texture.
    void D3DEmotePlayer::Draw(iTVPTexture2D *target) {
        float x = 0.0f;
        float y = 0.0f;
        _d3dImageOwner->TransformPoint(x, y);
        player().drawToD3DImageLike_0x6D5C68(target, x, y);
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
        std::vector<ttstr> paths{ttstr(v)};
        load(paths);
    }

    // PORT GAP: getModule@0x52FB98 is backed by a separate global-id-keyed
    // ordered map. Returning the first retained path here preserves the former
    // local API shape; it is not claimed as the binary getModule implementation.
    tTJSVariant D3DEmotePlayer::getModule() const {
        return obj().modulePaths().empty()
                   ? tTJSVariant()
                   : tTJSVariant(obj().modulePaths().front());
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
    void D3DEmotePlayer::load(const std::vector<ttstr> &modulePaths) {
        // teardown 双槽(== create)
        delete _secondaryObj;
        _secondaryObj = nullptr;
        delete _primaryObj;
        _primaryObj = nullptr;
        // 重建主槽(二进制 operator new(0x28) + EmoteObject_init(args))
        _primaryObj = new EmoteObject(modulePaths);
        engine()._modified = true;
    }

    tjs_error D3DEmotePlayer::loadCompat(tTJSVariant *result,
                                         tjs_int numparams,
                                         tTJSVariant **param,
                                         iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<D3DEmotePlayer>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }

        std::vector<ttstr> modulePaths;
        modulePaths.reserve(static_cast<size_t>(numparams));
        for(tjs_int i = 0; i < numparams; ++i) {
            modulePaths.emplace_back(*param[i]);
        }
        self->load(modulePaths);
        if(result) {
            result->Clear();
        }
        return TJS_S_OK;
    }

    tTJSVariant D3DEmotePlayer::clone() {
        typedef ncbInstanceAdaptor<D3DEmotePlayer> AdaptorT;

        auto *copy = new D3DEmotePlayer(_d3dImageOwner);
        // 懒建后 copy 主槽为 null;clone 需显式建主槽 —— 对齐二进制 sub_52FFBC
        // clone 回调内 `+24 = sub_67F978(...)`(operator new(0x28)+EmoteObject_init)。
        copy->_primaryObj = new EmoteObject(obj().modulePaths());
        // 壳层字段(EmotePlayer 自身)
        copy->_useD3D = _useD3D;
        copy->_smoothing = _smoothing;
        copy->_drawVisible = _drawVisible;
        copy->_drawOpacity = _drawOpacity;
        copy->_opengl = _opengl;
        copy->_visible = _visible;
        copy->_baseScale = _baseScale;
        copy->_userScale = _userScale;
        // EmoteEngine 层(引擎字段 + getScale/Rot/Color 缓存)
        copy->engine()._meshDivisionRatio = engine()._meshDivisionRatio;
        // R3 phantom: _queuing is Player+480 (Player class), not EmoteEngine.
        copy->player().setQueuing(player().getQueuing());
        copy->engine()._hairScale = engine()._hairScale;
        copy->engine()._partsScale = engine()._partsScale;
        copy->engine()._bustScale = engine()._bustScale;
        // bustScale member now backs onto the +1200 field (was _bodyScale);
        // copy it so the cloned D3DEmotePlayer preserves its bust scale.
        copy->engine()._scalarField_1200_1d = engine()._scalarField_1200_1d;
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

    // D3DEmotePlayer::assignState @0x530150 is an intentionally unimplemented
    // Android boundary: validate the argument as Object, probe its native
    // D3DEmotePlayer instance without raising a type error, then always throw
    // the exact TODO eTJSError emitted through sub_95440C @0x95440C.
    void D3DEmotePlayer::assignState(tTJSVariant state) {
        iTJSDispatch2 *object = state.AsObjectNoAddRef();
        if(object) {
            (void)ncbInstanceAdaptor<D3DEmotePlayer>::GetNativeInstance(
                object, false);
        }
        TVPThrowExceptionMessage(
            TJS_W("TODO: implement D3DEmotePlayer::assignState()"));
    }

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
        engine().setMirrorLike_0x671DB0(mirror);
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
        // D3DEmotePlayer::countVariables @0x53041C is deliberately unimplemented.
        TVPThrowExceptionMessage(
            TJS_W("TODO: implement D3DEmotePlayer::countVariables()"));
        return 0;
    }

    ttstr D3DEmotePlayer::getVariableLabelAt(tjs_int) {
        // D3DEmotePlayer::getVariableLabelAt @0x530530.
        TVPThrowExceptionMessage(
            TJS_W("TODO: implement D3DEmotePlayer::getVariableLabelAt()"));
        return {};
    }

    tjs_int D3DEmotePlayer::countVariableFrameAt(tjs_int) {
        // D3DEmotePlayer::countVariableFrameAt @0x530568.
        TVPThrowExceptionMessage(
            TJS_W("TODO: implement D3DEmotePlayer::countVariableFrameAt()"));
        return 0;
    }

    ttstr D3DEmotePlayer::getVariableFrameLabelAt(tjs_int, tjs_int) {
        // D3DEmotePlayer::getVariableFrameLabelAt @0x530588.
        TVPThrowExceptionMessage(
            TJS_W("TODO: implement D3DEmotePlayer::getVariableFrameLabelAt()"));
        return {};
    }

    double D3DEmotePlayer::getVariableFrameValueAt(tjs_int, tjs_int) {
        // D3DEmotePlayer::getVariableFrameValueAt @0x5305A8.
        TVPThrowExceptionMessage(
            TJS_W("TODO: implement D3DEmotePlayer::getVariableFrameValueAt()"));
        return 0.0;
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

    // --- Timeline methods ---

    tjs_int D3DEmotePlayer::countMainTimelines() {
        return engine().countMainTimelinesLike_0x5306AC();
    }

    ttstr D3DEmotePlayer::getMainTimelineLabelAt(tjs_int idx) {
        return engine().getMainTimelineLabelAtLike_0x674C84(
            static_cast<tjs_uint32>(idx));
    }

    tjs_int D3DEmotePlayer::countDiffTimelines() {
        return engine().countDiffTimelinesLike_0x5306D4();
    }

    ttstr D3DEmotePlayer::getDiffTimelineLabelAt(tjs_int idx) {
        return engine().getDiffTimelineLabelAtLike_0x674CEC(
            static_cast<tjs_uint32>(idx));
    }

    tjs_int D3DEmotePlayer::countPlayingTimelines() {
        return engine().countPlayingTimelinesLike_0x5306FC();
    }

    ttstr D3DEmotePlayer::getPlayingTimelineLabelAt(tjs_int idx) {
        return engine().getPlayingTimelineLabelAtLike_0x674D54(
            static_cast<tjs_uint32>(idx));
    }

    tjs_int D3DEmotePlayer::getPlayingTimelineFlagsAt(tjs_int idx) {
        return engine().getPlayingTimelineFlagsAtLike_0x674DC8(
            static_cast<tjs_uint32>(idx));
    }

    bool D3DEmotePlayer::isLoopTimeline(ttstr label) {
        return engine().getLoopTimelineLike_0x67522C(label);
    }

    tjs_int D3DEmotePlayer::getTimelineTotalFrameCount(ttstr label) {
        return static_cast<tjs_int>(
            engine().getTimelineTotalFrameCountLike_0x6753F0(label));
    }

    void D3DEmotePlayer::playTimeline(ttstr label, tjs_int flags) {
        engine().playTimelineLike_0x672F70(
            label, static_cast<tjs_uint32>(flags));
        engine()._modified = true;
    }

    bool D3DEmotePlayer::isTimelinePlaying(ttstr label) {
        return engine().isTimelinePlayingLike_0x673558(label);
    }

    void D3DEmotePlayer::stopTimeline(ttstr label) {
        engine().stopTimelineLike_0x67C2A0(label);
    }

    void D3DEmotePlayer::setTimelineBlendRatio(ttstr label, double ratio) {
        engine().setTimelineBlendLike_0x6735AC(
            label, false, static_cast<float>(ratio), 0.0f, 1.0f);
    }

    double D3DEmotePlayer::getTimelineBlendRatio(ttstr label) {
        return engine().getTimelineBlendLike_0x6821C8(label);
    }

    void D3DEmotePlayer::fadeInTimeline(ttstr label, double duration,
                                     tjs_int flags) {
        engine().fadeInTimelineLike_0x6736EC(
            label, duration, static_cast<double>(flags));
    }

    void D3DEmotePlayer::fadeOutTimeline(ttstr label, double duration,
                                      tjs_int flags) {
        engine().fadeOutTimelineLike_0x6739F4(
            label, duration, static_cast<double>(flags));
    }

    // D3DEmotePlayer_setTimeline @0x5308A4. The binary thunk only rewrites the
    // receiver and masks autoStop before tail-calling sub_6735AC; all three
    // floating arguments remain in their incoming FP registers.
    void D3DEmotePlayer::setTimeline(ttstr label, bool autoStop, float value,
                                    float transition, float easingWeight) {
        engine().setTimelineBlendLike_0x6735AC(
            label, autoStop, value, transition, easingWeight);
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
        engine().resetControllersLike_0x66EB8C();
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

    [[noreturn]] tTJSVariant D3DEmotePlayer::getOuterForce() {
        // D3DEmotePlayer::getOuterForce @0x530B28 is an intentional Android
        // TODO boundary and unconditionally throws through sub_95440C.
        throw eTJSError(
            ttstr(TJS_W("TODO: implement D3DEmotePlayer::getOuterForce()")));
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
    //   on the underlying object — progress=sub_6818B4 ->
    //   EmoteEngine_preProgress_guess @0x671764).
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

    // --- #4 initPhysics — the shipped NCB name maps directly to
    // EmoteEngine_applyMetadata_buildControllers @0x67D4D0. Despite its public
    // name, this consumes the raw metadata dictionary and rebuilds the complete
    // Engine controller/container graph; it is not a five-scalar wind helper.
    void EmotePlayer::initPhysics(tTJSVariant metadata) {
        engine().applyMetadataLike_0x67D4D0(metadata);
    }

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

    // --- #11 serialize (EmoteEngine state save @0x675E40) ---
    tTJSVariant EmotePlayer::serialize() {
        return engine().serializeLike_0x675E40();
    }

    // --- #12 unserialize (EmoteEngine state restore @0x678044) ---
    void EmotePlayer::unserialize(tTJSVariant data) {
        engine().unserializeLike_0x678044(data);
    }

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

    // EmotePlayer_modifyRoot @0x681F0C follows engine+1064 -> player+200 and
    // sets the same root-node delta dirty byte (+1584) as
    // Player_modifyRoot @0x6CD0B0.
    void EmotePlayer::modifyRoot() { player().modifyRoot(); }

    // setHairScale/setPartsScale/setBustScale (#39-41, sub_681F20/28/30) are
    //   inline in the header (engine +1184/+1192/+1200 raw writes).

    // --- #49 setMirror (sub_671DB0) ---
    void EmotePlayer::setMirror(bool mirror) {
        engine().setMirrorLike_0x671DB0(mirror);
    }

    // --- #50 skip (sub_66EB8C) ---
    void EmotePlayer::skip() { engine().resetControllersLike_0x66EB8C(); }

    // --- #51-57 timeline methods ---
    void EmotePlayer::playTimeline(ttstr label, tjs_int flags) {
        engine().playTimelineLike_0x672F70(
            label, static_cast<tjs_uint32>(flags));
        engine()._modified = true;
    }
    void EmotePlayer::stopTimeline(ttstr label) {
        engine().stopTimelineLike_0x67C2A0(label);
    }
    bool EmotePlayer::getTimelinePlaying(ttstr label) {
        return engine().isTimelinePlayingLike_0x673558(label);
    }
    void EmotePlayer::setTimelineBlendRatio(ttstr label, double ratio) {
        engine().setTimelineBlendLike_0x6735AC(
            label, false, static_cast<float>(ratio), 0.0f, 1.0f);
    }
    void EmotePlayer::fadeInTimeline(ttstr label, double duration, tjs_int flags) {
        engine().fadeInTimelineLike_0x6736EC(
            label, duration, static_cast<double>(flags));
    }
    void EmotePlayer::fadeOutTimeline(ttstr label, double duration, tjs_int flags) {
        engine().fadeOutTimelineLike_0x6739F4(
            label, duration, static_cast<double>(flags));
    }
    double EmotePlayer::getTimelineBlendRatio(ttstr label) {
        return engine().getTimelineBlendLike_0x6821C8(label);
    }

    // --- #58-64 variable/timeline query lists ---
    tTJSVariant EmotePlayer::getVariableRange(ttstr label) {
        // EmotePlayer::getVariableRange @0x673BEC: HM5 hit returns a fresh
        // Dictionary from value+40/+48; miss delegates to Player @0x6D6590.
        if(const auto it = engine()._variableRangesHM5_1328.find(label);
           it != engine()._variableRangesHM5_1328.end()) {
            return detail::makeDictionary({
                { "min", it->second.frameMin },
                { "max", it->second.frameMax },
            });
        }
        return player().getParameterRangeLike_0x6D6590(label);
    }
    tTJSVariant EmotePlayer::getVariableFrameList(ttstr label) {
        // EmotePlayer::getVariableFrameList @0x68229C: CopyRef engine+1248,
        // PropGet(label), CopyRef the result, then release the local dispatch.
        tTJSVariant result;
        tTJSVariant frameLists = engine()._variableFrameLists;
        iTJSDispatch2 *frames = frameLists.AsObjectNoAddRef();
        frames->PropGet(0, label.c_str(), nullptr, &result, frames);
        return result;
    }
    tTJSVariant EmotePlayer::getMainTimelineLabelList() {
        return engine().getMainTimelineLabelListLike_0x674F54();
    }
    tTJSVariant EmotePlayer::getDiffTimelineLabelList() {
        return engine().getDiffTimelineLabelListLike_0x6750C0();
    }
    tTJSVariant EmotePlayer::getLoopTimeline(ttstr label) {
        return tTJSVariant(engine().getLoopTimelineLike_0x67522C(label));
    }
    double EmotePlayer::getTimelineTotalFrameCount(ttstr label) {
        return engine().getTimelineTotalFrameCountLike_0x6753F0(label);
    }
    tTJSVariant EmotePlayer::getPlayingTimelineInfoList() {
        return engine().getPlayingTimelineInfoListLike_0x6754C4();
    }

    // --- #65-67 selector methods ---
    bool EmotePlayer::isSelectorTarget(ttstr label) {
        return engine().isSelectorTarget(label);
    }
    void EmotePlayer::activateSelectorTarget(ttstr label) {
        engine().activateSelectorTarget(label);
    }
    void EmotePlayer::deactivateSelectorTarget(ttstr label) {
        engine().deactivateSelectorTarget(label);
    }

    // --- #68 getCommandList (sub_682520 -> Player_getCommandList) ---
    tTJSVariant EmotePlayer::getCommandList() { return player().getCommandList(); }

} // namespace motion
