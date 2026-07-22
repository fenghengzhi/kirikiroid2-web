// EmoteEngine implementation. Aligned with libkrkr2.so sub_67E38C (ctor),
// sub_67D01C (progress) and sub_6766E0 (applyVarControllers).
//
// CLAUDE.md rule satisfied: Player is held via raw pointer + manual new/delete,
// matching the binary's explicit `operator new(0x568); Player_ctor(...)` pattern.

#include "EmoteEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <spdlog/spdlog.h>

#include "EmotePlayer.h"  // Player + EmotePlayer + ResourceManager
#include "MsgIntf.h"
#include "MotionDispatch.h"
#include "Player.h"
#include "RuntimeSupport.h" // detail::narrow (G2-C bind-loop label conversion)
#include "tjsArray.h"
#include "tjsDictionary.h"

#define LOGGER spdlog::get("plugin")
#define STUB_WARN(name) LOGGER->warn("EmoteEngine::" #name "() stub called")

namespace motion {

    namespace {

        tTJSVariant createTJSDictionaryLike_0x9C8440() {
            iTJSDispatch2 *dispatch = TJSCreateDictionaryObject();
            tTJSVariant result(dispatch, dispatch);
            dispatch->Release();
            return result;
        }

        tTJSArrayNI *getTJSArrayNative(const tTJSVariant &value) {
            iTJSDispatch2 *dispatch = value.AsObjectNoAddRef();
            tTJSArrayNI *native = nullptr;
            dispatch->NativeInstanceSupport(
                TJS_NIS_GETINSTANCE, TJSGetArrayClassID(),
                reinterpret_cast<iTJSNativeInstance **>(&native));
            return native;
        }

        tTJSArrayNI *tryGetTJSArrayNative(const tTJSVariant &value) {
            if(value.Type() != tvtObject) {
                return nullptr;
            }
            iTJSDispatch2 *dispatch = value.AsObjectNoAddRef();
            if(!dispatch) {
                return nullptr;
            }
            tTJSArrayNI *native = nullptr;
            if(TJS_FAILED(dispatch->NativeInstanceSupport(
                    TJS_NIS_GETINSTANCE, TJSGetArrayClassID(),
                    reinterpret_cast<iTJSNativeInstance **>(&native)))) {
                return nullptr;
            }
            return native;
        }

        void setTJSProperty(tTJSVariant &dictionary, const tjs_char *name,
                            tTJSVariant value) {
            iTJSDispatch2 *dispatch = dictionary.AsObjectNoAddRef();
            dispatch->PropSet(TJS_MEMBERENSURE, name, nullptr, &value, dispatch);
        }

        bool tryGetTJSProperty(const tTJSVariant &dictionary,
                               const tjs_char *name, tTJSVariant &value) {
            if(dictionary.Type() != tvtObject) {
                return false;
            }
            iTJSDispatch2 *dispatch = dictionary.AsObjectNoAddRef();
            return dispatch && TJS_SUCCEEDED(dispatch->PropGet(
                TJS_MEMBERMUSTEXIST, name, nullptr, &value, dispatch));
        }

        void restoreIntIfPresent(const tTJSVariant &dictionary,
                                 const tjs_char *name, int32_t &field) {
            tTJSVariant value;
            if(tryGetTJSProperty(dictionary, name, value)) {
                field = static_cast<int32_t>(value.AsInteger());
            }
        }

        void restoreFloatIfPresent(const tTJSVariant &dictionary,
                                   const tjs_char *name, float &field) {
            tTJSVariant value;
            if(tryGetTJSProperty(dictionary, name, value)) {
                field = static_cast<float>(value.AsReal());
            }
        }

        void restoreDoubleIfPresent(const tTJSVariant &dictionary,
                                    const tjs_char *name, double &field) {
            tTJSVariant value;
            if(tryGetTJSProperty(dictionary, name, value)) {
                field = value.AsReal();
            }
        }

        // sub_67C094 @0x67C094: serialize the eye/eyebrow 8-byte request queue.
        tTJSVariant serializeRequestQueueLike_0x67C094(
            const std::deque<std::pair<float, float>> &queue) {
            detail::TJSArrayWithItems_guess result =
                detail::createTJSArrayWithItems_guess();
            for(const auto &[p0, p1] : queue) {
                tTJSVariant item = createTJSDictionaryLike_0x9C8440();
                setTJSProperty(item, TJS_W("p0"), tTJSVariant(p0));
                setTJSProperty(item, TJS_W("p1"), tTJSVariant(p1));
                result.items->push_back(item);
            }
            return result.value;
        }

        void restoreRequestQueueLike_0x663FC8(
            std::deque<std::pair<float, float>> &queue,
            const tTJSVariant &dictionary) {
            tTJSVariant requestQueue;
            if(!tryGetTJSProperty(dictionary, TJS_W("rq"), requestQueue)) {
                return;
            }
            tTJSArrayNI *native = tryGetTJSArrayNative(requestQueue);
            if(!native) {
                return;
            }
            queue.clear();
            for(const tTJSVariant &rawItem : native->Items) {
                tTJSVariant item(rawItem);
                item.ToObject();
                queue.emplace_back(
                    static_cast<float>(detail::motionPropGetDouble(
                        item, TJS_W("p0"))),
                    static_cast<float>(detail::motionPropGetDouble(
                        item, TJS_W("p1"))));
            }
        }

        // sub_66767C @0x66767C.
        tTJSVariant serializeVarControllerLike_0x66767C(
            const EmoteVarController *controller) {
            tTJSVariant result = createTJSDictionaryLike_0x9C8440();
            setTJSProperty(result, TJS_W("phase"),
                           tTJSVariant(controller->state));
            setTJSProperty(result, TJS_W("tick"),
                           tTJSVariant(controller->phase));
            setTJSProperty(result, TJS_W("speed"),
                           tTJSVariant(controller->invDuration));
            setTJSProperty(result, TJS_W("exponent"),
                           tTJSVariant(controller->powCount));

            const auto makeChannels = [controller](const float *values) {
                detail::TJSArrayWithItems_guess array =
                    detail::createTJSArrayWithItems_guess();
                for(int index = 0; index < controller->count; ++index) {
                    array.items->emplace_back(values[index]);
                }
                return array.value;
            };
            setTJSProperty(result, TJS_W("frame"),
                           makeChannels(controller->currentValue));
            setTJSProperty(result, TJS_W("prev"),
                           makeChannels(controller->targetValue));
            setTJSProperty(result, TJS_W("target"),
                           makeChannels(controller->startValue));
            return result;
        }

        // sub_667ADC @0x667ADC.
        void restoreVarControllerLike_0x667ADC(
            EmoteVarController *controller, const tTJSVariant &dictionary) {
            if(dictionary.Type() != tvtObject) {
                return;
            }
            restoreIntIfPresent(dictionary, TJS_W("phase"), controller->state);
            restoreFloatIfPresent(dictionary, TJS_W("tick"), controller->phase);
            restoreFloatIfPresent(dictionary, TJS_W("speed"),
                                  controller->invDuration);
            restoreFloatIfPresent(dictionary, TJS_W("exponent"),
                                  controller->powCount);

            const auto restoreChannels = [controller, &dictionary](
                const tjs_char *name, float *values) {
                tTJSVariant array;
                if(!tryGetTJSProperty(dictionary, name, array)) {
                    return;
                }
                tTJSVariant object(array);
                object.ToObject();
                iTJSDispatch2 *dispatch = object.AsObjectNoAddRef();
                for(int index = 0; index < controller->count; ++index) {
                    tTJSVariant value;
                    (void)dispatch->PropGetByNum(
                        0, index, &value, dispatch);
                    values[index] = static_cast<float>(value.AsReal());
                }
            };
            restoreChannels(TJS_W("frame"), controller->currentValue);
            restoreChannels(TJS_W("prev"), controller->targetValue);
            restoreChannels(TJS_W("target"), controller->startValue);
        }

        // EmoteVarController_registerMembers2_guess @0x666830.
        tTJSVariant serializeAngleControllerLike_0x666830(
            const EmoteAngleController *controller) {
            tTJSVariant result = createTJSDictionaryLike_0x9C8440();
            setTJSProperty(result, TJS_W("phase"),
                           tTJSVariant(controller->state));
            setTJSProperty(result, TJS_W("tick"),
                           tTJSVariant(controller->phase));
            setTJSProperty(result, TJS_W("speed"),
                           tTJSVariant(controller->invDuration));
            setTJSProperty(result, TJS_W("exponent"),
                           tTJSVariant(controller->powCount));
            setTJSProperty(result, TJS_W("frame"),
                           tTJSVariant(controller->currentRad));
            setTJSProperty(result, TJS_W("prev"),
                           tTJSVariant(controller->startRad));
            setTJSProperty(result, TJS_W("target"),
                           tTJSVariant(controller->targetRad));
            return result;
        }

        // EmoteVarController_registerMembers3_guess @0x666A14. The original
        // writes both "prev" and "target" to +92 (startRad); targetRad is not
        // restored. Preserve that shipped boundary quirk.
        void restoreAngleControllerLike_0x666A14(
            EmoteAngleController *controller,
            const tTJSVariant &dictionary) {
            if(dictionary.Type() != tvtObject) {
                return;
            }
            restoreIntIfPresent(dictionary, TJS_W("phase"), controller->state);
            restoreFloatIfPresent(dictionary, TJS_W("tick"), controller->phase);
            restoreFloatIfPresent(dictionary, TJS_W("speed"),
                                  controller->invDuration);
            restoreFloatIfPresent(dictionary, TJS_W("exponent"),
                                  controller->powCount);
            restoreFloatIfPresent(dictionary, TJS_W("frame"),
                                  controller->currentRad);
            restoreFloatIfPresent(dictionary, TJS_W("prev"),
                                  controller->startRad);
            restoreFloatIfPresent(dictionary, TJS_W("target"),
                                  controller->startRad);
        }

        tTJSVariant serializeEyeControllerState(
            const ttstr &label, const EmoteBlinkController *controller) {
            tTJSVariant result = createTJSDictionaryLike_0x9C8440();
            setTJSProperty(result, TJS_W("label"), tTJSVariant(label));
            setTJSProperty(result, TJS_W("phase"), tTJSVariant(controller->trackState));
            setTJSProperty(result, TJS_W("frame"), tTJSVariant(controller->trackValue));
            setTJSProperty(result, TJS_W("v"), tTJSVariant(controller->trackDir));
            setTJSProperty(result, TJS_W("target"), tTJSVariant(controller->trackTarget));
            setTJSProperty(result, TJS_W("length"), tTJSVariant(controller->trackSpan));
            setTJSProperty(result, TJS_W("lengthDone"), tTJSVariant(controller->trackAccum));
            setTJSProperty(result, TJS_W("exponent"), tTJSVariant(controller->trackPow));
            setTJSProperty(result, TJS_W("speed"), tTJSVariant(controller->trackInvDur));
            setTJSProperty(result, TJS_W("rq"),
                           serializeRequestQueueLike_0x67C094(controller->valueTrack8B));
            return result;
        }

        void restoreEyeControllerLike_0x663FC8(
            EmoteBlinkController *controller,
            const tTJSVariant &dictionary) {
            if(dictionary.Type() != tvtObject) {
                return;
            }
            restoreIntIfPresent(dictionary, TJS_W("phase"), controller->trackState);
            restoreFloatIfPresent(dictionary, TJS_W("frame"), controller->trackValue);
            restoreFloatIfPresent(dictionary, TJS_W("v"), controller->trackDir);
            restoreFloatIfPresent(dictionary, TJS_W("target"), controller->trackTarget);
            restoreFloatIfPresent(dictionary, TJS_W("length"), controller->trackSpan);
            restoreFloatIfPresent(dictionary, TJS_W("lengthDone"), controller->trackAccum);
            restoreFloatIfPresent(dictionary, TJS_W("exponent"), controller->trackPow);
            restoreFloatIfPresent(dictionary, TJS_W("speed"), controller->trackInvDur);
            restoreRequestQueueLike_0x663FC8(controller->valueTrack8B, dictionary);
        }

        tTJSVariant serializeEyebrowControllerState(
            const ttstr &label, const EmoteEyebrowController *controller) {
            tTJSVariant result = createTJSDictionaryLike_0x9C8440();
            setTJSProperty(result, TJS_W("label"), tTJSVariant(label));
            setTJSProperty(result, TJS_W("phase"), tTJSVariant(controller->trackState));
            setTJSProperty(result, TJS_W("frame"), tTJSVariant(controller->trackValue));
            setTJSProperty(result, TJS_W("v"), tTJSVariant(controller->trackDir));
            setTJSProperty(result, TJS_W("target"), tTJSVariant(controller->trackTarget));
            setTJSProperty(result, TJS_W("length"), tTJSVariant(controller->trackSpan));
            setTJSProperty(result, TJS_W("lengthDone"), tTJSVariant(controller->trackAccum));
            setTJSProperty(result, TJS_W("exponent"), tTJSVariant(controller->trackPow));
            setTJSProperty(result, TJS_W("speed"), tTJSVariant(controller->trackInvDur));
            setTJSProperty(result, TJS_W("rq"),
                           serializeRequestQueueLike_0x67C094(controller->valueTrack8B));
            return result;
        }

        void restoreEyebrowControllerLike_0x665844(
            EmoteEyebrowController *controller,
            const tTJSVariant &dictionary) {
            if(dictionary.Type() != tvtObject) {
                return;
            }
            restoreIntIfPresent(dictionary, TJS_W("phase"), controller->trackState);
            restoreFloatIfPresent(dictionary, TJS_W("frame"), controller->trackValue);
            restoreFloatIfPresent(dictionary, TJS_W("v"), controller->trackDir);
            restoreFloatIfPresent(dictionary, TJS_W("target"), controller->trackTarget);
            restoreFloatIfPresent(dictionary, TJS_W("length"), controller->trackSpan);
            restoreFloatIfPresent(dictionary, TJS_W("lengthDone"), controller->trackAccum);
            restoreFloatIfPresent(dictionary, TJS_W("exponent"), controller->trackPow);
            restoreFloatIfPresent(dictionary, TJS_W("speed"), controller->trackInvDur);
            restoreRequestQueueLike_0x663FC8(controller->valueTrack8B, dictionary);
        }

        tTJSVariant serializeMouthControllerState(
            const ttstr &label, const EmoteMouthController *controller) {
            tTJSVariant result = createTJSDictionaryLike_0x9C8440();
            setTJSProperty(result, TJS_W("label"), tTJSVariant(label));
            setTJSProperty(result, TJS_W("phase"), tTJSVariant(controller->state));
            setTJSProperty(result, TJS_W("mouth"), tTJSVariant(controller->beginFrame));
            setTJSProperty(result, TJS_W("frame"), tTJSVariant(controller->currentValue));
            setTJSProperty(result, TJS_W("prev"), tTJSVariant(controller->startVal));
            setTJSProperty(result, TJS_W("target"), tTJSVariant(controller->endVal));
            setTJSProperty(result, TJS_W("tick"), tTJSVariant(controller->accum));
            setTJSProperty(result, TJS_W("exponent"), tTJSVariant(controller->powField));
            setTJSProperty(result, TJS_W("speed"), tTJSVariant(controller->invDur));
            return result;
        }

        void restoreMouthControllerLike_0x6661A8(
            EmoteMouthController *controller,
            const tTJSVariant &dictionary) {
            if(dictionary.Type() != tvtObject) {
                return;
            }
            restoreIntIfPresent(dictionary, TJS_W("phase"), controller->state);
            restoreIntIfPresent(dictionary, TJS_W("mouth"), controller->beginFrame);
            restoreFloatIfPresent(dictionary, TJS_W("frame"), controller->currentValue);
            restoreFloatIfPresent(dictionary, TJS_W("prev"), controller->startVal);
            restoreFloatIfPresent(dictionary, TJS_W("target"), controller->endVal);
            restoreFloatIfPresent(dictionary, TJS_W("tick"), controller->accum);
            restoreFloatIfPresent(dictionary, TJS_W("exponent"), controller->powField);
            restoreFloatIfPresent(dictionary, TJS_W("speed"), controller->invDur);
        }

        tTJSVariant serializeSelectorControllerState(
            const ttstr &label, const EmoteSelectorController *controller) {
            tTJSVariant result = createTJSDictionaryLike_0x9C8440();
            setTJSProperty(result, TJS_W("label"), tTJSVariant(label));
            setTJSProperty(result, TJS_W("value"), tTJSVariant(controller->selectedIndex));
            setTJSProperty(result, TJS_W("phase"), tTJSVariant(controller->selState));
            setTJSProperty(result, TJS_W("speed"), tTJSVariant(controller->invDuration));
            setTJSProperty(result, TJS_W("tick"), tTJSVariant(controller->accum));
            return result;
        }

        void restoreSelectorControllerLike_0x668570(
            EmoteSelectorController *controller,
            const tTJSVariant &dictionary) {
            if(dictionary.Type() != tvtObject) {
                return;
            }
            restoreIntIfPresent(dictionary, TJS_W("value"), controller->selectedIndex);
            restoreIntIfPresent(dictionary, TJS_W("phase"), controller->selState);
            restoreFloatIfPresent(dictionary, TJS_W("speed"), controller->invDuration);
            restoreFloatIfPresent(dictionary, TJS_W("tick"), controller->accum);
        }

    } // namespace

    // Aligned with libkrkr2.so sub_67E38C EmoteEngine_ctor @ 0x67E38C.
    //
    // Binary behaviour summary (from EmoteEngine_controllers.md):
    //   1) memset + sub_xxx_init on 10 std::deque headers (offsets 0..720)
    //   2) zero scalar/state region (+800..+864) with float=1.0f@856
    //   3) 4 inline `vector reserve(10)` blocks (PB stubbed)
    //   4) v13 = operator new(0x568); Player_ctor(v13)
    //   5) a1[134..140] = 7 controllers, each operator new + ctor_zero
    //   6) zero matrix/state @+1128..+1167; a1[150]=double 1.0;
    //      a1[290]=int 1; *((BYTE*)a1+1162)=1 (_dirty seeded true)
    //   7) ...more vector reserve(10) blocks...
    //   8) reset 4 controllers (134, 135, 137, 136 — note order!) seeding
    //      default values (pos=0,0; scale=1.0; angle=0; color=identity).
    //
    // C++ member-init handles deque default construction (empty); we replicate
    // steps 4, 5, 8 explicitly.
    EmoteEngine::EmoteEngine(const tTJSVariant &rmDispatch) {
        // Step 4: allocate and construct the Player heap object (+1064).
        // Binary: `v13 = operator new(0x568); Player_ctor(v13, a2)` — a2 is the
        //   RM dispatch wrapper (P3-B single-param dispatch-in, @0x6CED30).
        _player = new Player(rmDispatch);
        _player->_engineBack = this;

        // Step 5: allocate the 7 controllers (a1[134..140] = +1072..+1120).
        _ctlPosition         = new EmoteVarController();
        EmoteVarController_ctor(_ctlPosition,        2); // count=2 (x,y)

        _ctlScale            = new EmoteVarController();
        EmoteVarController_ctor(_ctlScale,           1); // count=1 (uniform)

        _ctlColor            = new EmoteVarController();
        EmoteVarController_ctor(_ctlColor,           4); // count=4 (RGBA)

        _ctlAngle            = new EmoteAngleController();
        EmoteAngleController_ctor(_ctlAngle,         0);

        _ctlHairPartsTarget  = new EmoteVarController();
        EmoteVarController_ctor(_ctlHairPartsTarget, 2);

        _ctlBust1Target      = new EmoteVarController();
        EmoteVarController_ctor(_ctlBust1Target,     2);

        _ctlBust2Target      = new EmoteVarController();
        EmoteVarController_ctor(_ctlBust2Target,     2);

        // Step 6 partial: +1162 _dirty defaults to true via in-class initializer.
        //
        // Step 8: reset 4 direct controllers seeding their currentValue with a
        // default. The binary inlines, for each controller, a "clear deque
        // queue + memcpy(currentValue, &seed, 4*count)" block. The ORDER in the
        // binary is a1[134] -> a1[135] -> a1[137] -> a1[136], i.e.
        //   POSITION (134, seed 0.0f, count=2)
        //   SCALE    (135, seed 1.0f, count=1)   [v73 = 1065353216 = 1.0f]
        //   ANGLE    (137, no currentValue seed — angle controller has a
        //             different block shape; only its deque is cleared)
        //   COLOR    (136, seed = xmmword_14D68D0, count=4)
        // (The local order previously did scale then color and skipped pos.)
        //
        // Reset == clear the keyframe queue + broadcast `seed` into every
        // currentValue channel (matches the binary's deque-block free +
        // memcpy(*(ctl+88), &seed, 4*count)).
        auto resetVarController = [](EmoteVarController* c, float seed) {
            if (!c) return;
            c->queue.clear();
            c->state = 0;
            c->phase = 0.0f;
            c->invDuration = 0.0f;
            if (c->currentValue && c->count > 0) {
                for (int i = 0; i < c->count; ++i) {
                    c->currentValue[i] = seed;
                }
            }
        };

        // 134: POSITION, seed 0.0f.
        resetVarController(_ctlPosition, 0.0f);
        // 135: SCALE, seed 1.0f.
        resetVarController(_ctlScale, 1.0f);
        // 137: ANGLE — binary only clears the deque (no currentValue memcpy
        //   because the angle controller's 0x70 block has no currentValue
        //   array seeded here). Clear its queue to match.
        if (_ctlAngle) _ctlAngle->queue.clear();
        // 136: COLOR. EmoteEngine_ctor @0x67E9D8 copies xmmword_14D68D0
        // byte-for-byte into the four currentValue channels. The rodata bytes
        // decode to {128.0f, 128.0f, 128.0f, 255.0f}; this is a per-channel
        // seed, not a scalar broadcast.
        if (_ctlColor && _ctlColor->currentValue && _ctlColor->count >= 4) {
            static constexpr float colorSeed[4] = {
                128.0f, 128.0f, 128.0f, 255.0f
            };
            _ctlColor->queue.clear();
            _ctlColor->state = 0;
            std::memcpy(_ctlColor->currentValue, colorSeed,
                        sizeof(colorSeed));
        }
    }

    // EmoteEngine dtor — manual cleanup of owned payload pointers. Aligned with
    // libkrkr2.so EmoteEngine_dtor @0x67F4B8; the typed STL members themselves
    // are destroyed automatically after this body.
    EmoteEngine::~EmoteEngine() {
        // libkrkr2.so dtor EmoteEngine_dtor @0x67F4B8 walks HM#7's
        // _M_before_begin._M_nxt node chain releasing each key ttstr, then
        // frees its buckets. With the typed std::unordered_map<ttstr,double>
        // _labelToValueHM7, the map's own destructor releases all key ttstrs
        // automatically (and likewise for the 6 other maps + 4 variant
        // vectors), so no manual bind-list free is needed here. The former
        // `_bindListHead` manual loop was an alias of the map internals and
        // has been removed.
        //
        // Delete deque#4 (eye) controllers. The binary's dtor frees each
        //   controller-deque's heap controllers (operator delete) before tearing
        //   down the deque header; the entry's ttstr label is released by the
        //   ttstr destructor. (M2 eye vertical: only deque#4 is populated so far.)
        for (EmoteEyeControlEntry_Deque4& entry : _stateMachineDeque4) {
            delete entry.ctl;
            entry.ctl = nullptr;
        }
        _stateMachineDeque4.clear();

        // Delete deque#5 (eyebrow) controllers (M2 eyebrow vertical). Same
        //   pattern as deque#4: the entry owns the operator new(0x150) slim
        //   controller; the ttstr label is released by its own destructor.
        for (EmoteEyebrowControlEntry_Deque5& entry : _stateMachineDeque5) {
            delete entry.ctl;
            entry.ctl = nullptr;
        }
        _stateMachineDeque5.clear();

        // Delete deque#6 (mouth) controllers (M2 mouth vertical). Same pattern as
        //   deque#4/#5: the entry owns the operator new(0x70) controller; the two
        //   ttstr keys (label + talkLabel) are released by their own destructors.
        for (EmoteMouthControlEntry_Deque6& entry : _compositeVarDeque6) {
            EmoteMouthController_dtor(entry.ctl);
            delete entry.ctl;
            entry.ctl = nullptr;
        }
        _compositeVarDeque6.clear();

        // Delete deque#8 (transition) controllers (M2 transition vertical). The
        //   entry owns the operator new(0x80) EmoteVarController; release its 3
        //   heap arrays then delete. These controllers are the SELECTOR's borrowed
        //   refCtl targets — the selector dtor (below) does NOT delete them, so we
        //   are the sole owner. The ttstr label is released by its own destructor.
        for (EmoteTransitionControlEntry_Deque8& entry : _auxVarDeque8) {
            EmoteVarController_dtor(entry.ctl);
            delete entry.ctl;
            entry.ctl = nullptr;
        }
        _auxVarDeque8.clear();

        // Delete deque#9 (selector) controllers (M2 selector vertical). Same
        //   pattern: the entry owns the operator new(0x80) controller; the ttstr
        //   label is released by its own destructor. The controller's optionList
        //   holds BORROWED refCtl pointers (owned by the transition deque), so
        //   EmoteSelectorController_dtor does NOT delete those — only this entry's
        //   own controller is deleted here.
        for (EmoteSelectorControlEntry_Deque9& entry : _vectorVarDeque9) {
            EmoteSelectorController_dtor(entry.ctl);
            delete entry.ctl;
            entry.ctl = nullptr;
        }
        _vectorVarDeque9.clear();

        // Deque#10 (loopControl) — each entry owns its operator new(0x20)
        //   EmoteLoopController. No special dtor (the keyframe vector frees its own
        //   buffer); just delete the controller.
        for (EmoteLoopControlEntry_Deque10& entry : _lookupCurvesDeque10) {
            delete entry.ctl;
            entry.ctl = nullptr;
        }
        _lookupCurvesDeque10.clear();

        // Delete deque#1 (bustControl -> hair/parts simple-spring) nodes. Each
        //   node owns its operator new(0x48) EmoteSpringState; the binary's dtor
        //   walks the deque freeing each spring (operator delete). The node ttstr
        //   keys (shapeLabel/keyX/keyY) are released by their own destructors.
        for (EmoteHairPartsNode48B& node : _hairPartsNodes) {
            delete node.spring;
            node.spring = nullptr;
        }
        _hairPartsNodes.clear();

        // Delete deque#2/#3 (hair/parts -> bust chain-spring) nodes. Each node
        //   owns its operator new(0xB0) EmoteBustChainSpring. collisionCurve is a
        //   BORROWED pointer (= EmoteEngine+1128 _windEmitter), not owned here.
        for (EmoteBustChain1Node56B& node : _bustChain1Nodes) {
            delete node.spring;
            node.spring = nullptr;
        }
        _bustChain1Nodes.clear();
        for (EmoteBustChain2Node56B& node : _bustChain2Nodes) {
            delete node.spring;
            node.spring = nullptr;
        }
        _bustChain2Nodes.clear();

        // Delete 7 controllers in reverse-of-ctor order.
        if (_ctlBust2Target)     { EmoteVarController_dtor(_ctlBust2Target);     delete _ctlBust2Target;     _ctlBust2Target = nullptr; }
        if (_ctlBust1Target)     { EmoteVarController_dtor(_ctlBust1Target);     delete _ctlBust1Target;     _ctlBust1Target = nullptr; }
        if (_ctlHairPartsTarget) { EmoteVarController_dtor(_ctlHairPartsTarget); delete _ctlHairPartsTarget; _ctlHairPartsTarget = nullptr; }
        if (_ctlAngle)           { EmoteAngleController_dtor(_ctlAngle);         delete _ctlAngle;           _ctlAngle = nullptr; }
        if (_ctlColor)           { EmoteVarController_dtor(_ctlColor);           delete _ctlColor;           _ctlColor = nullptr; }
        if (_ctlScale)           { EmoteVarController_dtor(_ctlScale);           delete _ctlScale;           _ctlScale = nullptr; }
        if (_ctlPosition)        { EmoteVarController_dtor(_ctlPosition);        delete _ctlPosition;        _ctlPosition = nullptr; }

        // Free the wind emitter heap object (+1128). The binary frees it in
        //   Player_startWind_populate/stopWind (operator delete + null); on engine
        //   teardown it must also be released since +1128 owns it. Any bust/hair
        //   spring still holding it as collisionCurve has already been deleted
        //   above, so no dangling borrow remains.
        delete _windEmitter;
        _windEmitter = nullptr;

        // Delete the Player heap object last (so _engineBack-using fields die first).
        delete _player;
        _player = nullptr;

        // EmoteEngine_dtor @0x67F4B8 destroys the four vector<ttstr> members in
        // reverse declaration order (+1040/+1016/+992/+800), releasing every
        // non-null string handle before freeing each buffer. The ordinary C++
        // member destructors below this body reproduce that exact lifetime; no
        // manual delete/Clear belongs here. sub_67F0CC is likewise the verified
        // vector<ttstr> copy-assignment helper, not an unknown pointer owner.
    }

    // sub_669928 @0x669928 + tail sub_669798 @0x669798.
    void EmoteEngine::resetMetadataState() {
        _scalarHM6_1384.clear();

        for(auto &node : _hairPartsNodes) {
            delete node.spring;
            node.spring = nullptr;
        }
        _hairPartsNodes.clear();
        for(auto &node : _bustChain1Nodes) {
            delete node.spring;
            node.spring = nullptr;
        }
        _bustChain1Nodes.clear();
        for(auto &node : _bustChain2Nodes) {
            delete node.spring;
            node.spring = nullptr;
        }
        _bustChain2Nodes.clear();

        for(auto &entry : _stateMachineDeque4) {
            delete entry.ctl;
            entry.ctl = nullptr;
        }
        _stateMachineDeque4.clear();
        for(auto &entry : _stateMachineDeque5) {
            delete entry.ctl;
            entry.ctl = nullptr;
        }
        _stateMachineDeque5.clear();
        for(auto &entry : _compositeVarDeque6) {
            EmoteMouthController_dtor(entry.ctl);
            delete entry.ctl;
            entry.ctl = nullptr;
        }
        _compositeVarDeque6.clear();
        _clampControlDeque7.clear();
        for(auto &entry : _auxVarDeque8) {
            EmoteVarController_dtor(entry.ctl);
            delete entry.ctl;
            entry.ctl = nullptr;
        }
        _auxVarDeque8.clear();
        for(auto &entry : _vectorVarDeque9) {
            EmoteSelectorController_dtor(entry.ctl);
            delete entry.ctl;
            entry.ctl = nullptr;
        }
        _vectorVarDeque9.clear();
        for(auto &entry : _lookupCurvesDeque10) {
            delete entry.ctl;
            entry.ctl = nullptr;
        }
        _lookupCurvesDeque10.clear();

        // sub_6696B8 clears vector<ttstr>@+800 first, releasing each element and
        // retaining capacity, then clears HM1/HM2. std::vector<ttstr>::clear()
        // reproduces the element Release/end=begin lifetime directly.
        _variableMatchList800.clear();
        _mirrorMatchSetHM1_824.clear();
        _mirrorMissSetHM2_880.clear();
        _compoundHM3_936.clear();

        detail::TJSArrayWithItems_guess baseLabels =
            detail::createTJSArrayWithItems_guess();
        _variableLabelsBase = baseLabels.value;
        _variableLabels = _variableLabelsBase;
        _variableFrameLists = createTJSDictionaryLike_0x9C8440();
        _instantVariableSetHM4_1272.clear();
        _variableRangesHM5_1328.clear();
    }

    // EmoteEngine_buildVariableList @0x66A530.
    void EmoteEngine::buildVariableList(const tTJSVariant &variableList) {
        detail::TJSArrayWithItems_guess labels =
            detail::createTJSArrayWithItems_guess();
        _variableLabels = labels.value;

        iTJSDispatch2 *frameDictionary =
            _variableFrameLists.AsObjectNoAddRef();
        const int count = detail::motionPropGetCount(variableList);
        for(int v11 = 0; v11 < count; ++v11) {
            const tTJSVariant item = detail::motionPropGetByNum(
                variableList, v11);
            const ttstr label = detail::motionPropGetString(
                item, TJS_W("label"));

            auto [rangeIt, inserted] =
                _variableRangesHM5_1328.try_emplace(label);
            (void)inserted;
            detail::EmoteVariableRange &range = rangeIt->second;

            tTJSVariant frameArrayValue;
            std::deque<tTJSVariant> *frameArray = nullptr;
            if(TJS_SUCCEEDED(frameDictionary->PropGet(
                    TJS_MEMBERMUSTEXIST, label.c_str(), nullptr,
                    &frameArrayValue, frameDictionary))) {
                frameArray = &getTJSArrayNative(frameArrayValue)->Items;
            } else {
                detail::TJSArrayWithItems_guess created =
                    detail::createTJSArrayWithItems_guess();
                frameArrayValue = created.value;
                frameArray = created.items;

                labels.items->emplace_back(label);
                frameDictionary->PropSet(
                    TJS_MEMBERENSURE, label.c_str(), nullptr,
                    &frameArrayValue, frameDictionary);
            }

            const tTJSVariant frameList = detail::motionPropGet(
                item, TJS_W("frameList"));
            const int frameCount = detail::motionPropGetCount(frameList);
            for(int v55 = 0; v55 < frameCount; ++v55) {
                const tTJSVariant frame = detail::motionPropGetByNum(
                    frameList, v55);
                const double frameValue = detail::motionPropGetDouble(
                    frame, TJS_W("frame"));

                range.frameMin = std::min(range.frameMin, frameValue);
                range.frameMax = std::max(range.frameMax, frameValue);
                frameArray->push_back(frame);
            }
        }
    }

    // sub_66E248 @0x66E248.
    void EmoteEngine::removeVariableLabel(const ttstr &label) {
        iTJSDispatch2 *labels = _variableLabels.AsObjectNoAddRef();
        tTJSVariant argument(label);
        tTJSVariant *arguments[] = { &argument };
        labels->FuncCall(0, TJS_W("remove"), nullptr, nullptr, 1, arguments,
                         labels);
    }

    // Aligned with libkrkr2.so sub_670D1C @0x670D1C.
    //
    // Binary pseudocode (fresh decompile in this conversation):
    //   base=new Array; +1208=base; base.Items=(+1228 Array).Items; dirty=1;
    //   for (entry : selectorDeque) { entry.flag=selectorEnabled;
    //     if (enabled) { entry.ctl.queue.clear(); selState=0; applySelection(0); }
    //     else std::remove(base.Items.begin(), base.Items.end(), entry.label);
    //     for (target : entry.targets) if (enabled) removeVariableLabel(target.label);
    //       else { target.ctl.queue.clear(); state=0; currentValue[0..count)=0; }
    //   }
    void EmoteEngine::syncSelectorControlsLike_0x670D1C() {
        detail::TJSArrayWithItems_guess baseLabels =
            detail::createTJSArrayWithItems_guess();               // 0x670d54
        _variableLabelsBase = baseLabels.value;                   // 0x670d60

        tTJSArrayNI *currentLabels = getTJSArrayNative(_variableLabels);
        *baseLabels.items = currentLabels->Items;                  // sub_670F6C

        _dirty = true;                                             // 0x670d98
        for (EmoteSelectorControlEntry_Deque9& entry : _vectorVarDeque9) {
            entry.flag = _selectorEnabled;                         // 0x670dc0..c4
            if (_selectorEnabled) {
                entry.ctl->commandTrack12B.queue.clear();          // 0x670dcc..e00
                entry.ctl->selState = 0;                           // 0x670e0c
                EmoteSelectorController_applySelection(
                    entry.ctl, 0, 0.0f, 0.0f);                     // 0x670e1c
            } else {
                const tTJSVariant label(entry.label);
                // sub_68B898 is std::remove over the Array Items deque. The
                // returned new-end iterator is deliberately ignored: the binary
                // does not erase/shrink the tail, so preserve that boundary quirk.
                (void)std::remove(baseLabels.items->begin(),
                                  baseLabels.items->end(), label); // 0x670e58
            }

            for (EmoteTransitionControlEntry_Deque8 *target : entry.targets) {
                if (_selectorEnabled) {
                    removeVariableLabel(target->label);            // 0x670e94
                } else {
                    EmoteVarController *ctl = target->ctl;
                    ctl->queue.clear();                             // 0x670ea0..ed4
                    ctl->state = 0;                                // 0x670ed8
                    std::fill_n(ctl->currentValue, ctl->count, 0.0f); // 0x670ee0..ef0
                }
            }
        }
    }

    // EmoteEngine_isSelectorTarget @0x6823FC.
    bool EmoteEngine::isSelectorTarget(const ttstr &label) {
        for (EmoteSelectorControlEntry_Deque9 &entry : _vectorVarDeque9) {
            entry.flag = _selectorEnabled;                       // 0x682478
            for (EmoteTransitionControlEntry_Deque8 *target : entry.targets) {
                if (target->label == label) {                     // 0x682484..e0
                    return true;
                }
            }
        }
        return false;
    }

    // EmoteEngine_activateSelectorTarget @0x67581C.
    void EmoteEngine::activateSelectorTarget(const ttstr &label) {
        for (EmoteSelectorControlEntry_Deque9 &entry : _vectorVarDeque9) {
            for (std::size_t index = 0; index < entry.targets.size(); ++index) {
                if (entry.targets[index]->label != label) {
                    continue;
                }

                entry.ctl->commandTrack12B.queue.clear();         // 0x675914..48
                entry.ctl->selState = 0;                          // 0x675924
                EmoteSelectorController_applySelection(
                    entry.ctl, static_cast<int>(index), 0.0f, 0.0f); // 0x67595c
                entry.flag = 0;                                  // 0x675960

                for (EmoteSelectorControlEntry_Deque9 &selector :
                     _vectorVarDeque9) {
                    float value;
                    EmoteSelectorController_step(
                        selector.ctl, &value, 0.0f);               // 0x67599c
                    _labelToValueHM7[selector.label] = value;      // 0x6759b0..bc
                }
                for (EmoteTransitionControlEntry_Deque8 &transition :
                     _auxVarDeque8) {
                    float value;
                    EmoteVarController_step(
                        transition.ctl, &value, 0.0f);             // 0x6759fc
                    _labelToValueHM7[transition.label] = value;    // 0x675a10..1c
                }
                return;
            }
        }
    }

    // EmoteEngine_deactivateSelectorTarget @0x675BF4.
    void EmoteEngine::deactivateSelectorTarget(const ttstr &label) {
        for (EmoteSelectorControlEntry_Deque9 &entry : _vectorVarDeque9) {
            for (std::size_t index = 0; index < entry.targets.size(); ++index) {
                if (entry.targets[index]->label != label) {
                    continue;
                }

                entry.ctl->commandTrack12B.queue.clear();         // 0x675CEC..0x675D20
                entry.ctl->selState = 0;                          // 0x675CFC
                EmoteSelectorController_applySelection(
                    entry.ctl, static_cast<int>(index), 0.0f, 0.0f); // 0x675D34
                entry.flag = 1;                                  // 0x675D3C

                for (EmoteSelectorControlEntry_Deque9 &selector :
                     _vectorVarDeque9) {
                    float value;
                    EmoteSelectorController_step(
                        selector.ctl, &value, 0.0f);               // 0x675D78
                    _labelToValueHM7[selector.label] = value;      // 0x675D8C..98
                }
                for (EmoteTransitionControlEntry_Deque8 &transition :
                     _auxVarDeque8) {
                    float value;
                    EmoteVarController_step(
                        transition.ctl, &value, 0.0f);             // 0x675DD8
                    _labelToValueHM7[transition.label] = value;    // 0x675DEC..F8
                }
                return;
            }
        }
    }

    // sub_66EB8C @0x66EB8C, including timeline helper sub_669D10 and direct
    // controller tail sub_66A42C. The order is observable because selector
    // applySelection may enqueue into transition controllers that are reset
    // immediately afterward.
    void EmoteEngine::resetControllersLike_0x66EB8C() {
        std::size_t activeIndex = 0;
        while(activeIndex < _activeTimelineLabels1040.size()) {
            detail::EmoteHM3Value &state =
                _compoundHM3_936.at(_activeTimelineLabels1040[activeIndex]);
            if(state.loopBegin >= 0.0) {
                EmoteVarController_resetLike_0x66713C(
                    state.blendController);
                ++activeIndex;
            } else {
                applyTimelineWindowLike_0x669E1C(
                    state, true, state.lastTime);
                _activeTimelineLabels1040.erase(
                    _activeTimelineLabels1040.begin() +
                    static_cast<std::ptrdiff_t>(activeIndex));
            }
        }

        EmoteVarController_resetLike_0x66713C(_ctlHairPartsTarget);
        EmoteVarController_resetLike_0x66713C(_ctlBust1Target);
        EmoteVarController_resetLike_0x66713C(_ctlBust2Target);

        for(EmoteHairPartsNode48B &node : _hairPartsNodes) {
            node.spring->firstFlag = 1;
            node.initFlag = 1;
        }
        for(EmoteBustChain1Node56B &node : _bustChain1Nodes) {
            node.spring->firstFlag = 1;
            node.initFlag = 1;
        }
        for(EmoteBustChain2Node56B &node : _bustChain2Nodes) {
            node.spring->firstFlag = 1;
            node.initFlag = 1;
        }

        for(EmoteEyeControlEntry_Deque4 &entry : _stateMachineDeque4) {
            EmoteBlinkController_resetLike_0x663AA0(entry.ctl);
        }
        for(EmoteEyebrowControlEntry_Deque5 &entry : _stateMachineDeque5) {
            EmoteEyebrowController_resetLike_0x6654C4(entry.ctl);
        }
        for(EmoteMouthControlEntry_Deque6 &entry : _compositeVarDeque6) {
            EmoteMouthController *ctl = entry.ctl;
            if(!ctl->valueTrack12B.queue.empty()) {
                ctl->state = 0;
                ctl->currentValue = ctl->valueTrack12B.queue.back().endRad;
                ctl->valueTrack12B.queue.clear();
            } else if(ctl->state != 0) {
                ctl->state = 0;
                ctl->currentValue = ctl->endVal;
            }
        }
        for(EmoteSelectorControlEntry_Deque9 &entry : _vectorVarDeque9) {
            EmoteSelectorController_resetLike_0x668394(entry.ctl);
        }
        for(EmoteTransitionControlEntry_Deque8 &entry : _auxVarDeque8) {
            EmoteVarController_resetLike_0x66713C(entry.ctl);
        }

        EmoteVarController_resetLike_0x66713C(_ctlPosition);
        EmoteVarController_resetLike_0x66713C(_ctlScale);
        if(!_ctlAngle->queue.empty()) {
            _ctlAngle->state = 0;
            _ctlAngle->currentRad = _ctlAngle->queue.back().endRad;
            _ctlAngle->queue.clear();
        } else if(_ctlAngle->state != 0) {
            float value = _ctlAngle->targetRad;
            _ctlAngle->state = 0;
            while(value < 0.0f) {
                value += 6.2832f;
            }
            while(value >= 6.2832f) {
                value -= 6.2832f;
            }
            _ctlAngle->currentRad = value;
        }
        EmoteVarController_resetLike_0x66713C(_ctlColor);
    }

    // sub_671DB0 @0x671DB0.
    void EmoteEngine::setMirrorLike_0x671DB0(bool mirror) {
        _mirrorRequested = mirror;
        _mirrorChanged = (_mirrorRequested != _mirrorBase);
        _player->setMirror(_mirrorChanged);
        resetControllersLike_0x66EB8C();
    }

    void EmoteEngine::applyMetadataLike_0x67D4D0(
        const tTJSVariant &metadata) {
        // EmoteEngine_applyMetadata_buildControllers @0x67D4D0 starts by
        // CopyRef'ing the raw metadata variant and unwrapping its dispatch.
        // Retain that source-level lifetime while all builders consume the raw
        // TJS dispatch in the same order as Android.
        const tTJSVariant metadataCopy = metadata;

        resetMetadataState();

        _mirrorBase = detail::motionPropGetBool(
            metadataCopy, TJS_W("mirror"));
        _mirrorChanged = (_mirrorRequested != _mirrorBase);
        _player->setMirror(_mirrorChanged);
        _player->progressFramesLike_0x6D2A54(0.0);

        _meshDivisionRatio = detail::motionPropGetDouble(
            metadataCopy, TJS_W("scale"));
        float controllerScale = 0.0f;
        EmoteVarController_step(_ctlScale, &controllerScale, 0.0f);
        _meshDivisionRatioDup =
            1.0 / (_meshDivisionRatio * controllerScale);

        tTJSVariant variableList;
        if(detail::motionTryPropGet(
                metadataCopy, TJS_W("variableList"), variableList)) {
            buildVariableList(variableList);
        }

        const tTJSVariant bustControl = detail::motionPropGet(
            metadataCopy, TJS_W("bustControl"));                  // 0x67d66c
        buildBustControl(bustControl);                              // 0x67d68c

        const tTJSVariant hairControl = detail::motionPropGet(
            metadataCopy, TJS_W("hairControl"));                  // 0x67d6c4
        buildChainControl(_bustChain1Nodes, 1, hairControl);        // 0x67d6e8

        const tTJSVariant partsControl = detail::motionPropGet(
            metadataCopy, TJS_W("partsControl"));                 // 0x67d720
        buildChainControl(_bustChain2Nodes, 2, partsControl);       // 0x67d744

        const tTJSVariant eyeControl = detail::motionPropGet(
            metadataCopy, TJS_W("eyeControl"));                  // 0x67d784
        buildEyeControl(eyeControl);                              // 0x67d7a4

        const tTJSVariant eyebrowControl = detail::motionPropGet(
            metadataCopy, TJS_W("eyebrowControl"));               // 0x67d7dc
        buildEyebrowControl(eyebrowControl);                       // 0x67d7fc

        const tTJSVariant mouthControl = detail::motionPropGet(
            metadataCopy, TJS_W("mouthControl"));                 // 0x67d834
        buildMouthControl(mouthControl);                           // 0x67d854

        const auto transitionControl = detail::motionPropGet(
            metadataCopy, TJS_W("transitionControl"));
        buildTransitionControl(transitionControl);

        tTJSVariant selectorControl;
        if(detail::motionTryPropGet(
                metadataCopy, TJS_W("selectorControl"), selectorControl)) {
            buildSelectorControl(selectorControl);
        }

        const auto loopControl = detail::motionPropGet(
            metadataCopy, TJS_W("loopControl"));
        buildLoopControl(loopControl);

        const auto clampControl = detail::motionPropGet(
            metadataCopy, TJS_W("clampControl"));                 // 0x67d93c
        buildClampControl(clampControl);                           // 0x67d95c

        const auto mirrorControl = detail::motionPropGet(
            metadataCopy, TJS_W("mirrorControl"));
        buildMirrorControl(mirrorControl);

        tTJSVariant instantVariableList;
        if(detail::motionTryPropGet(
                metadataCopy, TJS_W("instantVariableList"),
                instantVariableList)) {
            buildInstantVariableList(instantVariableList);
        }

        const auto timelineControl = detail::motionPropGet(
            metadataCopy, TJS_W("timelineControl"));
        buildTimelineControl(timelineControl);

        syncSelectorControlsLike_0x670D1C();                       // 0x67da8c
    }

    // Aligned with libkrkr2.so
    //   EmoteEngine_applyVarControllers_pos_scale_color_angle @ 0x6766E0
    //   (call site inside EmoteEngine_progress @0x67d380).
    //
    // Binary body (VERIFIED by fresh decompile of 0x6766E0 + all 4 sinks, 2026-06-06):
    //   step(ctlPosition@+1072, &v7, dt);  Player_setCoord(player, v7, v8);   // @0x6CCFF8
    //   step(ctlColor@+1088,    &v7, dt);  sub_6CD724(player, packARGB);       // @0x6CD724
    //       packARGB = (u8)(int)v7 | (u8)(int)v8<<8 | (u8)(int)v9<<16 | (u8)(int)v10<<24;
    //   step(ctlScale@+1080,    &v7, dt);  *(double*)(this+1176) =
    //                                          1.0 / (*(double*)(this+1168) * v7);
    //                                      Player_setSlant(player, v7, v7);    // @0x6C0F54
    //   step(ctlAngle@+1096,    &v7, dt);  Player_setAngleDeg(player, v7);     // @0x6C0F84
    //
    // ORDER IS pos -> color -> scale -> angle. Each apply happens IMMEDIATELY
    // after its own step, all reusing the same small output buffer (the binary
    // reuses stack slot &v7 for every step).
    //
    // Sink semantics confirmed against the binary:
    //   - Player_setCoord(0x6CCFF8): (x=v7, y=v8) -> root+1592/+1600. Local
    //     Player::setCoord matches.
    //   - sub_6CD724 = Player_setColorWeight(0x6CD724): takes the int packed by
    //     THIS caller; the sink's internal R/B swizzle into +1156 is replicated
    //     by Player::setColorWeight (swapPackedRbLike_0x6CD710). So we pass the
    //     caller pack verbatim.
    //   - Player_setSlant(0x6C0F54): two args (slantX=v7, slantY=v7) -> root
    //     +1624/+1632. Local Player::setSlant(v) writes both axes = v, matching
    //     setSlant(v7, v7).
    //   - Player_setAngleDeg(0x6C0F84): input is DEGREES (no rad conversion),
    //     fed directly from step output v7.
    //
    // Note: binary derefs the 4 controller ptrs unconditionally (no null guard);
    // ctor (lines 48-58) always `new`s them so they are non-null at runtime. The
    // local if(_ctlX) guards are a conservative no-op equivalent.
    void EmoteEngine::applyVarControllers_pos_scale_color_angle(float dt) {
        // Shared output buffer (mirrors the binary's single &v7 stack slot;
        // 4 floats covers the widest controller, color count=4).
        float out[4];

        // 1) POSITION (ctl@+1072, count=2) -> Player_setCoord(v7, v8) @0x6CCFF8.
        if (_ctlPosition) {
            out[0] = out[1] = 0.0f;
            EmoteVarController_step(_ctlPosition, out, dt);
            _player->setCoord(out[0], out[1]);
        }

        // 2) COLOR (ctl@+1088, count=4) -> sub_6CD724(packed ARGB32) @0x6CD724.
        //    Pack exactly as the binary caller does (out[0]=byte0 .. out[3]=byte3);
        //    Player::setColorWeight reproduces the sink's internal R/B swizzle.
        if (_ctlColor) {
            out[0] = out[1] = out[2] = out[3] = 1.0f;
            EmoteVarController_step(_ctlColor, out, dt);
            const std::uint32_t argb =
                  (std::uint32_t)(std::uint8_t)(int)out[0]
                | ((std::uint32_t)(std::uint8_t)(int)out[1] << 8)
                | ((std::uint32_t)(std::uint8_t)(int)out[2] << 16)
                | ((std::uint32_t)(std::uint8_t)(int)out[3] << 24);
            _player->setColorWeight((tjs_int)argb);
        }

        // 3) SCALE (ctl@+1080, count=1) -> +1176 denom + Player_setSlant @0x6C0F54.
        if (_ctlScale) {
            out[0] = 1.0f;
            EmoteVarController_step(_ctlScale, out, dt);
            // Binary: *(double*)(this+1176) = 1.0 / (*(double*)(this+1168) * out[0]);
            // (no guard in the binary; division by zero yields inf as in libc).
            _meshDivisionRatioDup = 1.0 / (_meshDivisionRatio * out[0]);
            // setSlant(v7, v7): both axes = out[0]; local setSlant writes slantX=slantY=v.
            _player->setSlant(out[0]);
        }

        // 4) ANGLE (ctl@+1096) -> Player_setAngleDeg(out[0]) @0x6C0F84 (DEGREES).
        if (_ctlAngle) {
            out[0] = 0.0f;
            EmoteAngleController_step(_ctlAngle, out, dt);
            _player->setAngleDeg(out[0]);
        }
    }

    // ------------------------------------------------------------------------
    // Physics-pass helpers (file-local). EmoteEngine is a friend of Player, so
    // these free helpers read Player's private state directly, matching the
    // binary's raw `*(player + off)` field reads.
    // ------------------------------------------------------------------------
    namespace {

        // Aligned with libkrkr2.so sub_67B970 @ 0x67B970 — per-node "shape"
        // anchor resolver shared by stepHairParts and stepBust.
        //
        // Binary pseudocode (condensed):
        //   v7 = *labelPtr; AddRef(v7);
        //   sub_6D38F4(player, &label, &resolved);     // label -> layer dispatch
        //   if (!resolvedValid) return 0;
        //   shape = resolved.PropGet("shape");          // vtable+32
        //   if (!shapeIsObject) return 0;
        //   if (sub_6635DC(shape,"type") != 0) return 0;// only type==0 proceeds
        //   x = sub_662668(shape,"x"); y = sub_662668(shape,"y");
        //   sub_6CD738(player, &rootX, &rootY);         // root node +1592/+1600
        //   r = *(player_owner + 1176);                 // meshDivisionRatioDup
        //   *outX = rootY + (y - rootY)*r;              // (binary's a3)
        //   *outY = rootX + (x - rootX)*r;              // (binary's a4)
        //   return 1;
        //
        // The local layer-dispatch resolver is Player::getLayerMotion (= the
        // sub_6D38F4 -> sub_6B5AD8 path: returns the resolved node's PSB dict
        // as a tTJSVariant). PropGet "shape"/"type"/"x"/"y" replicate
        // sub_6635DC (int) / sub_662668 (double) which both call dispatch
        // vtable+32 = PropGet. sub_6CD738 reads root node X(+1592)/Y(+1600)
        // = Player::getX()/getY(). meshDivisionRatioDup is EmoteEngine+1176.
        //
        // Returns 1 on success (outX/outY written), 0 on any miss (outputs left
        // unchanged — same as the binary, which only writes on the success path
        // and returns 0 otherwise).
        int resolveShapeAnchorLike_0x67B970(EmoteEngine* self, Player* player,
                                            const ttstr& label,
                                            float* outX, float* outY) {
            // sub_6D38F4(player, &label, &resolved): resolve label -> dispatch.
            tTJSVariant resolved = player->getLayerMotion(label); // /*0x67b9cc*/
            if (resolved.Type() != tvtObject) {
                return 0; // !v29 path -> v11 = 0                  /*0x67ba18*/
            }
            iTJSDispatch2* obj = resolved.AsObjectNoAddRef();
            if (!obj) {
                return 0;
            }

            // shape = obj.PropGet("shape") (vtable+32).               /*0x67ba64*/
            tTJSVariant shapeVar;
            if (TJS_FAILED(obj->PropGet(0, TJS_W("shape"), nullptr, &shapeVar, obj))
                || shapeVar.Type() != tvtObject) {
                return 0; // !v24 path -> v11 = 0                  /*0x67bac8*/
            }
            iTJSDispatch2* shape = shapeVar.AsObjectNoAddRef();
            if (!shape) {
                return 0;
            }

            // sub_6635DC(shape, "type"): int. Nonzero -> fail (v11=0).  /*0x67bb08*/
            tTJSVariant typeVar;
            tjs_int type = 0;
            if (shape->PropGet(0, TJS_W("type"), nullptr, &typeVar, shape) == TJS_S_OK) {
                type = static_cast<tjs_int>(typeVar.AsInteger());
            }
            if (type != 0) {
                return 0; // sub_6635DC nonzero -> v11 = 0          /*0x67bb10*/
            }

            // x = sub_662668(shape,"x"); y = sub_662668(shape,"y") (doubles).
            double x = 0.0, y = 0.0;                              // /*0x67bb38 / 0x67bb5c*/
            tTJSVariant xVar, yVar;
            if (shape->PropGet(0, TJS_W("x"), nullptr, &xVar, shape) == TJS_S_OK) {
                x = xVar.AsReal();
            }
            if (shape->PropGet(0, TJS_W("y"), nullptr, &yVar, shape) == TJS_S_OK) {
                y = yVar.AsReal();
            }

            // sub_6CD738(player, &rootX, &rootY): root node +1592 / +1600.
            const double rootX = player->getX();                 // (player+200)+1592 /*0x67bb6c*/
            const double rootY = player->getY();                 // (player+200)+1600
            const double r = self->_meshDivisionRatioDup;        // EmoteEngine+1176  /*0x67bb74*/

            // *a3 = rootY + (y - rootY)*r;  *a4 = rootX + (x - rootX)*r;
            // (binary keeps the X/Y crossover verbatim — v14=y pairs with rootY
            //  into the first output, v13=x pairs with rootX into the second.)
            *outX = static_cast<float>(rootY + (y - rootY) * r); // /*0x67bb84*/
            *outY = static_cast<float>(rootX + (x - rootX) * r); // /*0x67bb9c*/
            return 1;                                            // /*0x67bba4*/
        }

    } // namespace

    // Aligned with libkrkr2.so EmoteEngine_stepHairParts @ 0x67B748.
    //
    // Binary main loop (condensed):
    //   ctl = _ctlHairPartsTarget@+1104; n = ctl->count(+80);
    //   if (n>=1) memcpy(&cur, ctl->currentValue(+88), 4*n);  // cur[0..n)
    //   v13 = dt - 0.0001;
    //   for (node in deque#1) {
    //       anchor = node[36..40];                              // prev anchor
    //       resolveShapeAnchor(this, node+12, &anchor.x, &anchor.y);
    //       if (node->initFlag) {
    //           node->initFlag = 0; ang = getAngleDeg(player);
    //           springStep(node->spring, &oX,&oY, anchor.x,anchor.y,
    //                      cur[0],cur[1], dt, scalar1200, ang);
    //       } else if (v13 > 0) {
    //           acc=0;
    //           do { st=fminf(dt-acc,1.1); acc+=st; f=acc/dt; w=1-f;
    //                ax = w*node[36] + f*anchor.x; ay = w*node[40] + f*anchor.y;
    //                ang=getAngleDeg(player);
    //                springStep(node->spring,&oX,&oY, ax,ay, cur[0],cur[1],
    //                           st, scalar1200, ang);
    //           } while (v13 > acc);
    //       }
    //       node[36..40] = anchor;                              // write back
    //       HM7[node->keyX] = oX;  HM7[node->keyY] = oY;        // double slots
    //   }
    void EmoteEngine::stepHairParts(float dt) {
        Player* const player = _player;
        EmoteVarController* const ctl = _ctlHairPartsTarget; // *(this+1104) /*0x67b788*/

        // memcpy(&cur, ctl->currentValue, 4*count) — copy current controller out.
        // cur[0]=v32, cur[1]=v33 (count==2 for hair/parts target).         /*0x67b7a4*/
        float cur[8] = {};
        const int count = ctl ? ctl->count : 0;
        if (count >= 1 && ctl->currentValue) {
            for (int i = 0; i < count && i < 8; ++i) {
                cur[i] = ctl->currentValue[i];
            }
        }

        const float v13 = dt - 0.0001f;                       // /*0x67b7d0*/

        for (EmoteHairPartsNode48B& node : _hairPartsNodes) {
            // anchor = node[36..40] (previous), then resolve overwrites it.   /*0x67b800*/
            float anchorX = node.anchorX;
            float anchorY = node.anchorY;
            resolveShapeAnchorLike_0x67B970(this, player, node.shapeLabel,
                                            &anchorX, &anchorY);  //          /*0x67b804*/

            float oX = 0.0f, oY = 0.0f;
            if (node.initFlag) {                              //              /*0x67b808*/
                node.initFlag = 0;                            //              /*0x67b810*/
                const float ang = static_cast<float>(player->emoteGetAngleRadLike_0x6CD0C0()); //  /*0x67b818*/
                // springStep(spring,&oX,&oY, anchorX,anchorY, cur0,cur1,
                //            dt, scalar1200, ang)                            /*0x67b844*/
                EmotePhysics_springStep(node.spring, &oX, &oY,
                                        anchorX, anchorY, cur[0], cur[1],
                                        dt,
                                        static_cast<float>(_scalarField_1200_1d),
                                        ang);
            } else if (v13 > 0.0f) {                          //              /*0x67b850*/
                // sub-stepped integration toward the resolved anchor.
                const float prevX = node.anchorX; // *((float*)v9+9)  /*0x67b8a8*/
                const float prevY = node.anchorY; // *((float*)v9+10)
                float acc = 0.0f;                              //             /*0x67b858*/
                do {
                    const float st = std::fmin(dt - acc, 1.1f); //           /*0x67b880*/
                    acc = acc + st;                            //             /*0x67b88c*/
                    const float f = acc / dt;                  //             /*0x67b894*/
                    const float w = 1.0f - f;                  //             v22
                    const float ax = (w * prevX) + (f * anchorX); //         /*0x67b8a8*/
                    const float ay = (w * prevY) + (f * anchorY); //         /*0x67b8ac*/
                    const float ang = static_cast<float>(player->emoteGetAngleRadLike_0x6CD0C0()); /*0x67b8c0*/
                    EmotePhysics_springStep(node.spring, &oX, &oY,
                                            ax, ay, cur[0], cur[1],
                                            st,
                                            static_cast<float>(_scalarField_1200_1d),
                                            ang);              //             /*0x67b8dc*/
                } while (v13 > acc);                            //            /*0x67b8e4*/
            }

            node.anchorX = anchorX;                            // write back  /*0x67b8f4*/
            node.anchorY = anchorY;

            // HM#7 double outputs (Player_HM2_upsert_labelToValue(this+1440,..)).
            _labelToValueHM7[node.keyX] = oX;                  //             /*0x67b904*/
            _labelToValueHM7[node.keyY] = oY;                  //             /*0x67b918*/
        }
    }

    // Aligned with libkrkr2.so EmoteEngine_stepBust @ 0x67BCE8.
    //
    // Binary signature: stepBust(this, ctlTarget(a2), chainNodes(a3),
    //                            springConst(a4 double), dt(a5 float)).
    // Main loop (condensed):
    //   n = ctlTarget->count(+80);
    //   if (n>=1) memcpy(&cur, ctlTarget->currentValue(+88), 4*n);
    //   v50 = (float)springConst;            // strength fed to chain spring (a10)
    //   v47 = dt * 0.03125;  v49 = dt - 0.0001;
    //   for (node in chain deque) {
    //       anchor = node[44..48];
    //       resolveShapeAnchor(this, node+12, &anchor.x, &anchor.y);
    //       node->spring->collisionCurve(+168) = this->_windEmitter(+1128);
    //       if (node->initFlag) {
    //           node->initFlag = 0; ang=getAngleDeg(player);
    //           chainStep(spring,&oS0,&oS1,&oLast, anchor.x,anchor.y,
    //                     cur0,cur1, dt, springConst, ang);
    //           // depth ramp using |oLast|<=28 toward node->spring[13]:
    //           ... (see inline) ...
    //       } else if (v49>0) {
    //           acc=0;
    //           do { st=fminf(dt-acc,1.1); acc+=st; f=acc/dt; w=1-f;
    //                ax = w*node[44] + f*anchor.x; ay = w*node[48] + f*anchor.y;
    //                chainStep(...); depth ramp; } while (v49>acc);
    //       }
    //       node[44..48] = anchor;
    //       HM7[node->keyA]=v23; HM7[node->keyB]=v7; HM7[node->keyC]=v8(oLastY);
    //   }
    //
    // Output / jiggle mapping (verbatim from the binary):
    //   chainStep(spring, &oSeg0(=v55), &oSeg1(=v54), &oLastY(=v53), ...);
    //   v8 = oLastY (captured right after chainStep);
    //   depth ramp gates on |oLastY| <= 28 toward spring[13];
    //   spring[12] = fmod(spring[12] + depth*spring[7]*dt, 2*pi);
    //   j = sin(spring[12]) * spring[13] * spring[8];
    //   v23 = oSeg1 + j;   v7 = oSeg0 - j;   (oSeg0/oSeg1 also updated but dead)
    //   HM7[keyA(node+20)] = v23;  HM7[keyB(node+28)] = v7;  HM7[keyC(node+36)] = v8;
    // When neither branch runs (initFlag clear AND v49<=0): v23 = dt-0.0001 and
    //   v7/v8 keep their prior value (the binary reads them un-refreshed — the
    //   deques are empty at runtime so this path never executes; we seed v7/v8=0
    //   for defined behaviour, matching the binary's effective state on entry).
    void EmoteEngine::stepBust(EmoteVarController* ctlTarget,
                               std::deque<EmoteBustChain1Node56B>& chainNodes,
                               double springConst, float dt) {
        Player* const player = _player;

        // memcpy(&cur, ctlTarget->currentValue, 4*count).                  /*0x67bd4c*/
        float cur[8] = {};
        const int count = ctlTarget ? ctlTarget->count : 0;
        if (count >= 1 && ctlTarget->currentValue) {
            for (int i = 0; i < count && i < 8; ++i) {
                cur[i] = ctlTarget->currentValue[i];
            }
        }

        const float v50 = static_cast<float>(springConst);  // a4 -> chain a10 /*0x67bd6c*/
        const float v47 = dt * 0.03125f;                     // depth ramp dt   /*0x67bdac*/
        const float v49 = dt - 0.0001f;                      //                 /*0x67bdb0*/

        for (EmoteBustChain1Node56B& node : chainNodes) {
            float anchorX = node.anchorX;                    // node[44/48]     /*0x67be94*/
            float anchorY = node.anchorY;
            resolveShapeAnchorLike_0x67B970(this, player, node.shapeLabel,
                                            &anchorX, &anchorY); //            /*0x67be98*/

            // node->spring->collisionCurve = this->_windEmitter (v12[141]). /*0x67bea4*/
            //   The spring physics reads the wind emitter's 128-slot particle
            //   field as a collision/force curve. Borrowed pointer, not owned.
            if (node.spring) {
                node.spring->collisionCurve = _windEmitter;
            }

            // Spring float-array view for the depth-ramp fields [7]/[8]/[12]/[13].
            // (binary: v28 = (float*)*v15; reads v28[7],v28[8],v28[12],v28[13].)
            float* const sp = reinterpret_cast<float*>(node.spring);

            float oSeg0 = 0.0f;  // v55 (chainStep a2)
            float oSeg1 = 0.0f;  // v54 (chainStep a3)
            float oLastY = 0.0f; // v53 (chainStep a4)
            float v23 = dt - 0.0001f; // keyA value (default when no branch runs)
            float v7  = 0.0f;          // keyB value
            float v8  = 0.0f;          // keyC value (= oLastY)

            if (node.initFlag) {                             //                /*0x67bea8*/
                node.initFlag = 0;                           //                /*0x67beb0*/
                const float ang = static_cast<float>(player->emoteGetAngleRadLike_0x6CD0C0()); //   /*0x67beb8*/
                EmoteBustChainSpring_step(node.spring, &oSeg0, &oSeg1, &oLastY,
                                          anchorX, anchorY, cur[0], cur[1],
                                          dt, v50, ang);      //               /*0x67bee4*/
                v8 = oLastY;                                  // *(float*)&v8=v53 /*0x67bee8*/
                // depth ramp (|oLastY| vs 28).                                /*0x67befc*/
                float depth = sp ? sp[13] : 0.0f;             // v33           /*0x67beec*/
                if (std::fabs(oLastY) <= 28.0f) {
                    depth = depth - v47;                      //               /*0x67bdbc*/
                    if (depth < 0.0f) depth = 0.0f;           //               /*0x67bdc8*/
                } else {
                    depth = v47 + depth;                      //               /*0x67bf04*/
                    if (depth > 1.0f) depth = 1.0f;           //               /*0x67bf10*/
                }
                if (sp) {
                    const float spd = sp[7];                  //               /*0x67bdcc*/
                    sp[13] = depth;                           //               /*0x67bdd0*/
                    const float ph = std::fmod(sp[12] + ((depth * spd) * dt),
                                               6.28318531f);  //               /*0x67bdf4*/
                    sp[12] = ph;                              //               /*0x67bdf8*/
                    const float j = (std::sin(ph) * sp[13]) * sp[8]; //        /*0x67be10*/
                    v23 = oSeg1 + j;                          // v54 + v22     /*0x67be14*/
                    v7  = oSeg0 - j;                          // v55 - v22     /*0x67be18*/
                    oSeg1 = oSeg1 + j;                        // v54 += (dead) /*0x67be1c*/
                    oSeg0 = oSeg0 - j;                        // v55 -= (dead)
                }
            } else if (v49 > 0.0f) {                           //              /*0x67bf20*/
                const float prevX = node.anchorX;             // *((float*)v15+11) /*0x67bf30*/
                const float prevY = node.anchorY;             // *((float*)v15+12) /*0x67bf24*/
                float acc = 0.0f;                              //              /*0x67bf2c*/
                do {
                    const float st = std::fmin(dt - acc, 1.1f); //           /*0x67bf48*/
                    acc = acc + st;                           //               /*0x67bf50*/
                    const float f = acc / dt;                 //               /*0x67bf58*/
                    const float w = 1.0f - f;                 //               v36
                    const float ax = (w * prevX) + (f * anchorX); //          /*0x67bf6c*/
                    const float ay = (w * prevY) + (f * anchorY); //          /*0x67bf70*/
                    const float ang = static_cast<float>(player->emoteGetAngleRadLike_0x6CD0C0()); /*0x67bf74*/
                    EmoteBustChainSpring_step(node.spring, &oSeg0, &oSeg1, &oLastY,
                                              ax, ay, cur[0], cur[1],
                                              st, v50, ang);  //               /*0x67bfa8*/
                    v8 = oLastY;                              // *(float*)&v8=v53 /*0x67bfac*/
                    // depth ramp with per-substep dt (st).                    /*0x67bfc8*/
                    float depth = sp ? sp[13] : 0.0f;         // v40           /*0x67bfb4*/
                    const float v41 = st * 0.03125f;          //               /*0x67bfc4*/
                    if (std::fabs(oLastY) <= 28.0f) {
                        depth = depth - v41;                  //               /*0x67bfe0*/
                        if (depth < 0.0f) depth = 0.0f;       //               /*0x67bfe8*/
                    } else {
                        depth = v41 + depth;                  //               /*0x67bfcc*/
                        if (depth > 1.0f) depth = 1.0f;       //               /*0x67bfd4*/
                    }
                    if (sp) {
                        const float spd = sp[7];              //               /*0x67bff0*/
                        const float ph0 = sp[12];             //               /*0x67bff4*/
                        sp[13] = depth;                       //               /*0x67bff8*/
                        const float ph = std::fmod(ph0 + (st * (depth * spd)),
                                                   6.28318531f); //            /*0x67c014*/
                        sp[12] = ph;                          //               /*0x67c018*/
                        const float j = (std::sin(ph) * sp[13]) * sp[8]; //    /*0x67c034*/
                        v23 = oSeg1 + j;                      // v54 + v46     /*0x67c038*/
                        v7  = oSeg0 - j;                      // v55 - v46     /*0x67c040*/
                        oSeg1 = oSeg1 + j;                    // v54 += (dead) /*0x67c044*/
                        oSeg0 = oSeg0 - j;                    // v55 -= (dead)
                    }
                } while (v49 > acc);                          //               /*0x67c048*/
            }

            node.anchorX = anchorX;                           // write back     /*0x67be30*/
            node.anchorY = anchorY;

            // HM#7 outputs (Player_HM2_upsert_labelToValue(this+1440,..)).
            _labelToValueHM7[node.keyA] = v23;                //               /*0x67be38*/
            _labelToValueHM7[node.keyB] = v7;                 //               /*0x67be4c*/
            _labelToValueHM7[node.keyC] = v8;                 //               /*0x67be5c*/
        }
    }

    // Aligned with libkrkr2.so EmoteEngine_buildEyeControl @ 0x66C77C.
    // Decompiled pseudocode (this conversation):
    //   count = Motion_propGetCount(eyeControl);            // 0x66c810
    //   for (v5 = 0; v5 < count; ++v5) {                    // 0x66c844
    //     elem = eyeControl[v5];                            // PropGet(0, v5)
    //     if ((propGetBool(elem, "enabled") & 1) == 0) continue;  // 0x66c8e4 gate
    //     v7 = operator new(0x170);                         // 0x66c8f0
    //     EmoteBlinkController_ctor(v7, elem);              // 0x66c8fc
    //     push_back deque#4 {ctl=v7, label=0};              // 0x66c914 (label slot zeroed)
    //     label = propGet(elem, "label");                   // 0x66c9c4
    //     deque#4.back().label = label;                     // 0x66ca10 (AddRef into slot)
    //     ref = HM6_findOrInsert(this+1384, label);         // 0x66ca28
    //     ref->type = 4; ref->index = v5;                   // 0x66ca30
    //   }
    // The HM#6 index is the LOOP index v5 (NOT the deque size), matching the
    // binary: an element skipped by the enabled gate still advances v5.
    void EmoteEngine::buildEyeControl(const tTJSVariant& eyeControl) {
        const int count = detail::motionPropGetCount(eyeControl);
        for (int v5 = 0; v5 < count; ++v5) {                     // 0x66c844
            const tTJSVariant elem =
                detail::motionPropGetByNum(eyeControl, v5);     // 0x66c860
            if (!detail::motionPropGetBool(elem, TJS_W("enabled"))) {
                continue;                                       // goto LABEL_28
            }

            // operator new(0x170) + ctor (raw pointer, manual lifetime — the
            //   deque entry owns the controller; dtor is responsible for delete).
            EmoteBlinkController* ctl = new EmoteBlinkController(); // 0x66c8f0
            EmoteBlinkController_ctor(ctl, elem);                  // 0x66c8fc

            // Push {ctl, empty label}, then CopyRef the property into back().label.
            EmoteEyeControlEntry_Deque4 entry;
            entry.ctl = ctl;
            _stateMachineDeque4.push_back(std::move(entry));
            _stateMachineDeque4.back().label = detail::motionPropGetString(
                elem, TJS_W("label"));                            // 0x66c9c4..0x66ca10

            // HM#6 VarRef {type=4, index=v5} keyed by label (0x66ca28..0x66ca30).
            detail::EmoteVarRef& ref =
                _scalarHM6_1384[_stateMachineDeque4.back().label];
            ref.type  = 4;   // *v17 = 4
            ref.index = v5;  // v17[1] = v5
        }
    }

    // Aligned with libkrkr2.so EmoteEngine_buildEyebrowControl @ 0x66CB9C.
    // Decompiled pseudocode (this conversation):
    //   count = Motion_propGetCount(eyebrowControl);        // 0x66cc30
    //   for (v5 = 0; v5 < count; ++v5) {                    // 0x66cc64
    //     elem = eyebrowControl[v5];                        // PropGet(0, v5)  0x66cc80
    //     if ((Motion_propGetBool(elem,"enabled") & 1) == 0) continue; // 0x66cd04 gate
    //     v7 = operator new(0x150);                         // 0x66cd10
    //     EmoteBlinkController_ctor_slim(v7, elem);         // 0x66cd1c
    //     push_back deque#5 {ctl=v7, label=0};              // 0x66cd34 (label slot zeroed)
    //     label = propGet(elem, "label");                   // 0x66cde4
    //     deque#5.back().label = label;                     // 0x66ce30 (AddRef into slot)
    //     ref = HM6_findOrInsert(this+1384, label);         // 0x66ce48 (a1+173)
    //     ref->type = 5; ref->index = v5;                   // 0x66ce50
    //   }
    // Same structure as buildEyeControl (0x66C77C) except: new(0x150) slim
    //   controller (not 0x170), pushes onto deque#5 (engine+320, a1[46..49]),
    //   and writes HM#6 type=5. The HM#6 index is the LOOP index v5 (NOT the
    //   deque size), matching the binary (a skipped element still advances v5).
    void EmoteEngine::buildEyebrowControl(const tTJSVariant& eyebrowControl) {
        const int count = detail::motionPropGetCount(eyebrowControl);
        for (int v5 = 0; v5 < count; ++v5) {                        // 0x66cc64
            const tTJSVariant elem =
                detail::motionPropGetByNum(eyebrowControl, v5);     // 0x66cc80
            if (!detail::motionPropGetBool(elem, TJS_W("enabled"))) {
                continue;                                           // goto LABEL_28
            }

            // operator new(0x150) + slim ctor (raw pointer, manual lifetime —
            //   the deque entry owns the controller; dtor is responsible for
            //   delete).
            EmoteEyebrowController* ctl = new EmoteEyebrowController(); // 0x66cd10
            EmoteEyebrowController_ctor(ctl, elem);                    // 0x66cd1c

            // Push {ctl, empty label}, then CopyRef the property into back().label.
            EmoteEyebrowControlEntry_Deque5 entry;
            entry.ctl = ctl;
            _stateMachineDeque5.push_back(std::move(entry));
            _stateMachineDeque5.back().label = detail::motionPropGetString(
                elem, TJS_W("label"));                              // 0x66cde4..0x66ce30

            // HM#6 VarRef {type=5, index=v5} keyed by label (0x66ce48..0x66ce50).
            detail::EmoteVarRef& ref =
                _scalarHM6_1384[_stateMachineDeque5.back().label];
            ref.type  = 5;   // *v17 = 5
            ref.index = v5;  // v17[1] = v5
        }
    }

    // Aligned with libkrkr2.so EmoteEngine_buildMouthControl @ 0x66CFBC.
    // Decompiled pseudocode (this conversation):
    //   count = Motion_propGetCount(mouthControl);          // 0x66d054
    //   for (v5 = 0; v5 < count; ++v5) {                    // 0x66d088
    //     elem = mouthControl[v5];                          // PropGet(0, v5)  0x66d0a4
    //     if ((Motion_propGetBool(elem,"enabled") & 1) == 0) continue; // 0x66d128 gate
    //     v9 = operator new(0x70);                          // 0x66d134
    //     EmoteMouthController_ctor(v9, elem);              // 0x66d140
    //     push_back deque#6 {ctl=v9, label=0, talkLabel=0}; // 0x66d158 (both slots zeroed)
    //     label     = propGet(elem, "label");     back().label     = label;     // 0x66d220..0x66d26c
    //     talkLabel = propGet(elem, "talkLabel");  back().talkLabel = talkLabel; // 0x66d2a8..0x66d2f4
    //     ref1 = HM6_findOrInsert(this+1384, &back().label);     // 0x66d30c (a1+173)
    //     ref1->type = 6; ref1->index = v5;                       // 0x66d314
    //     ref2 = HM6_findOrInsert(this+1384, &back().talkLabel);  // 0x66d320
    //     ref2->type = 6; ref2->index = v5;                       // 0x66d32c
    //   }
    // UNIQUE to the mouth category vs eye/eyebrow:
    //   * the deque#6 element is 24B {ctl, label, talkLabel} (a THIRD ttstr).
    //   * the builder inserts TWO HM#6 VarRef entries for a single controller
    //     (label AND talkLabel), both {type=6, index=v5}. The progress loop then
    //     stepping this controller writes *outBeginFrame into HM7[label] and
    //     *outCurrentValue into HM7[talkLabel].
    //   The HM#6 index is the LOOP index v5 (NOT the deque size), matching the
    //   binary (a skipped element still advances v5).
    void EmoteEngine::buildMouthControl(const tTJSVariant& mouthControl) {
        const int count = detail::motionPropGetCount(mouthControl);
        for (int v5 = 0; v5 < count; ++v5) {                       // 0x66d088
            const tTJSVariant elem =
                detail::motionPropGetByNum(mouthControl, v5);      // 0x66d0a4
            if (!detail::motionPropGetBool(elem, TJS_W("enabled"))) {
                continue;                                          // goto LABEL_34
            }

            // operator new(0x70) + ctor (raw pointer, manual lifetime — the deque
            //   entry owns the controller; dtor is responsible for delete).
            EmoteMouthController* ctl = new EmoteMouthController(); // 0x66d134
            EmoteMouthController_ctor(ctl, elem);                  // 0x66d140

            // The binary first pushes {ctl,0,0}, then CopyRefs both strings into
            // the newly appended slot. Preserve that construction/data-flow order.
            EmoteMouthControlEntry_Deque6 entry;
            entry.ctl       = ctl;
            _compositeVarDeque6.push_back(std::move(entry));
            EmoteMouthControlEntry_Deque6& back = _compositeVarDeque6.back();

            // label = elem["label"] (HM7 key for *outBeginFrame).          /*0x66d220*/
            back.label = detail::motionPropGetString(
                elem, TJS_W("label"));                            // 0x66d220
            // talkLabel = elem["talkLabel"] (HM7 key for *outCurrentValue). /*0x66d2a8*/
            back.talkLabel = detail::motionPropGetString(
                elem, TJS_W("talkLabel"));                        // 0x66d2a8

            // TWO HM#6 VarRef inserts {type=6, index=v5} — label AND talkLabel.
            //   (0x66d30c..0x66d314 and 0x66d320..0x66d32c.)
            detail::EmoteVarRef& ref1 = _scalarHM6_1384[back.label]; // 0x66d30c
            ref1.type  = 6;   // *v24 = 6
            ref1.index = v5;  // v24[1] = v5
            detail::EmoteVarRef& ref2 = _scalarHM6_1384[back.talkLabel]; // 0x66d320
            ref2.type  = 6;   // *v25 = 6
            ref2.index = v5;  // v25[1] = v5
        }
    }

    // Aligned with libkrkr2.so EmoteEngine_buildSelectorControl @ 0x66D8FC.
    // Decompiled pseudocode (this conversation):
    //   count = Motion_propGetCount(selectorControl);                 // 0x66d990
    //   for (v6 = 0; v6 < count; ++v6) {                              // 0x66de6c
    //     elem = selectorControl[v6];                                 // PropGet(0,v6) 0x66de88
    //     label = propGet(elem, "label"); -> v80 (HM7 key + HM6 key)  // 0x66df1c/0x66df30
    //     if ((propGetBool(elem,"enabled") & 1) == 0) {               // 0x66df5c gate
    //         sub_66E248(this, &label);  // remove var binding; skip  // 0x66dfe8
    //         continue;
    //     }
    //     optionList = empty vector<Option16B>;                       // 0x66df64
    //     options = propGet(elem, "optionList");                      // 0x66df94
    //     ocount = Motion_propGetCount(options);                      // 0x66d9f0
    //     for (v13 = 0; v13 < ocount; ++v13) {                        // 0x66da20
    //       opt = options[v13];                                       // 0x66da3c
    //       optLabel = propGet(opt, "label");                         // 0x66dacc
    //       // resolve optLabel against the TRANSITION deque (a1[72..]=engine+576):
    //       refCtl = 0;                                               // 0x66db98
    //       for (e in transitionDeque) if (e.label == optLabel) {     // 0x66db0c
    //           refCtl = e.ctl; e.flag@+16 = 0; sub_66E248(this,&optLabel); break; // 0x66db80
    //       }
    //       offValue = propGetFloat(opt, "offValue", 0);              // 0x66dbbc
    //       onValue  = propGetFloat(opt, "onValue",  0);              // 0x66dbdc
    //       optionList.push_back({refCtl, offValue, onValue});        // 0x66dbf0 (16B)
    //     }
    //     v47 = operator new(0x80);                                   // 0x66dcf4
    //     EmoteSelectorController_ctor(v47, optionList);              // 0x66dd08 (swaps optionList in)
    //     push_back deque#9 {ctl=v47, label=0,...zeroed}; back().label = label; // 0x66dd10..0x66ddec
    //     ref = HM6_findOrInsert(this+1384, &back().label);          // 0x66de30 (sub_689188)
    //     ref = {type=8, index=v6};   // payload (v6<<32)|8           // 0x66de20
    //   }
    //
    // buildTransitionControl @0x66D4C4 populates `_auxVarDeque8` first;
    // buildVariableList @0x66A530 owns the +1228 label Array, and both binary
    // sub_66E248 remove calls below dispatch to that exact Array. The HM#6 index
    // is the LOOP index v6 (NOT deque size); a disabled item still advances v6.
    void EmoteEngine::buildSelectorControl(const tTJSVariant &selectorControl) {
        const int count = detail::motionPropGetCount(selectorControl); // 0x66d990
        for (int v6 = 0; v6 < count; ++v6) {                          // 0x66de6c
            const tTJSVariant elem = detail::motionPropGetByNum(
                selectorControl, v6);                                // PropGet(0,v6)

            // label = elem["label"] (HM7 key for the step output + HM6 key).
            //   /*0x66df1c..0x66df30*/
            const ttstr label = detail::motionPropGetString(
                elem, TJS_W("label"));

            // enabled gate (0x66df5c). Non-enabled -> remove through the
            // +1228 TJS Array (sub_66E248) and skip.
            if (!detail::motionPropGetBool(elem, TJS_W("enabled"))) {
                removeVariableLabel(label);                           // sub_66E248
                continue;                                             // goto LABEL_82
            }

            // Assemble optionList[] from elem["optionList"].          /*0x66df94*/
            std::vector<SelectorOption16B> optionList;
            const tTJSVariant options = detail::motionPropGet(
                elem, TJS_W("optionList"));
            const int ocount = detail::motionPropGetCount(options);   // 0x66d9f0
            for (int v13 = 0; v13 < ocount; ++v13) {                  // 0x66da20
                const tTJSVariant opt = detail::motionPropGetByNum(
                    options, v13);                                   // 0x66da3c

                // optLabel = opt["label"].                            /*0x66dacc*/
                const ttstr optLabel = detail::motionPropGetString(
                    opt, TJS_W("label"));

                // Resolve optLabel against the TRANSITION deque (engine+576 =
                //   local _auxVarDeque8). Linear scan, compare elem.label ==
                //   optLabel; on match borrow elem.ctl, clear the matched entry's
                //   flag byte@+16 to 0, remove the var binding (sub_66E248), and
                //   break. The match is the FIRST hit. /*0x66db0c..0x66db98*/
                //   This requires buildTransitionControl to have run already (it
                //   does — EmoteEngine dispatches the raw transition list after
                //   the decoded middle builders and before this raw selector,
                //   mirroring applyMetadata's per-key order @0x67D4D0). When the
                //   motion declares no transitionControl the deque is empty and
                //   refCtl stays null (faithful to the binary's v26=0 no-match).
                EmoteVarController* refCtl = nullptr;                 // v26 = 0
                for (EmoteTransitionControlEntry_Deque8& tentry : _auxVarDeque8) {
                    if (tentry.label == optLabel) {                  // 0x66db0c compare
                        refCtl       = tentry.ctl;                   // v26 = e.ctl
                        tentry.flag  = 0;                            // e.flag@+16 = 0
                        removeVariableLabel(optLabel);               // sub_66E248
                        break;
                    }
                }

                // offValue / onValue (default 0.0).  /*0x66dbbc / 0x66dbdc*/
                const float offValue = static_cast<float>(
                    detail::motionPropGetDouble(opt, TJS_W("offValue")));
                const float onValue = static_cast<float>(
                    detail::motionPropGetDouble(opt, TJS_W("onValue")));

                // push_back {refCtl, offValue, onValue} (16B option).  /*0x66dbf0*/
                SelectorOption16B option;
                option.refCtl   = refCtl;
                option.offValue = offValue;
                option.onValue  = onValue;
                optionList.push_back(option);
            }

            // operator new(0x80) + ctor (raw pointer, manual lifetime — the deque
            //   entry owns the controller; dtor delete). The ctor swaps optionList
            //   into the controller and applies selection index 0.   /*0x66dcf4/0x66dd08*/
            EmoteSelectorController* ctl = new EmoteSelectorController();
            EmoteSelectorController_ctor(ctl, std::move(optionList));

            // Push {ctl, empty label, trailing zeros/indeterminate flag} first,
            // then CopyRef the label into back().label. /*0x66dd10..0x66ddec*/
            EmoteSelectorControlEntry_Deque9 entry;
            entry.ctl = ctl;
            _vectorVarDeque9.push_back(std::move(entry));
            _vectorVarDeque9.back().label = label;

            // HM#6 VarRef insert {type=8, index=v6} keyed by label.   /*0x66de20/0x66de30*/
            //   (binary payload = (v6 << 32) | 8 -> type=8, index=v6.)
            detail::EmoteVarRef& ref =
                _scalarHM6_1384[_vectorVarDeque9.back().label];
            ref.type  = 8;   // payload low 32 bits
            ref.index = v6;  // payload high 32 bits
        }
    }

    // Aligned with libkrkr2.so EmoteEngine_buildTransitionControl @ 0x66D4C4.
    // Decompiled pseudocode (this conversation):
    //   count = Motion_propGetCount(transitionControl);             // 0x66d558
    //   for (v5 = 0; v5 < count; ++v5) {                            // 0x66d58c
    //     elem = transitionControl[v5];                             // PropGet(0,v5) 0x66d5a8
    //     if ((Motion_propGetBool(elem,"enabled") & 1) == 0) continue; // 0x66d62c gate -> LABEL_28
    //     v7 = operator new(0x80);                                  // 0x66d638
    //     EmoteVarController_ctor_20Bdeque(v7, 1);                  // 0x66d644 (count=1)
    //     push_back deque#8 {ctl=v7, label=0, flag@+16=1};          // 0x66d660..0x66d664
    //     label = propGet(elem, "label");                           // 0x66d724
    //     deque#8.back().label = label;                             // 0x66d770 (AddRef into +8 slot)
    //     ref = HM6_findOrInsert(this+1384, &back().label);         // 0x66d788 (a1+173)
    //     ref->type = 7; ref->index = v5;                           // 0x66d790 (*v18=7; v18[1]=v5)
    //   }
    // Structure mirrors buildEyeControl/buildSelectorControl: enabled gate, new
    //   the controller, push {ctl,label} (here with the extra flag byte@+16=1),
    //   register HM#6 {type=7, index=loopIndex}. The flag byte is written by the
    //   builder (binary: *(_BYTE*)(v8+16)=1) and read only by setVariable case7's
    //   Animator_setKeyframes gate — the progress step (sub_666BF8) ignores it.
    //   HM#6 index is the LOOP index v5 (a skipped/disabled element still
    //   advances v5). MUST run before buildSelectorControl (the selector resolves
    //   each option's refCtl by scanning THIS deque @0x66db0c).
    void EmoteEngine::buildTransitionControl(const tTJSVariant &transitionControl) {
        const int count = detail::motionPropGetCount(transitionControl);
        for (int v5 = 0; v5 < count; ++v5) {                            // 0x66d58c
            const auto elem = detail::motionPropGetByNum(
                transitionControl, v5);                                // PropGet(0,v5)

            // enabled gate (0x66d62c): skip when "enabled" is not truthy.
            if (!detail::motionPropGetBool(elem, TJS_W("enabled"))) {
                continue;                                               // goto LABEL_28
            }

            // operator new(0x80) + ctor count=1 (raw pointer, manual lifetime —
            //   the deque entry owns the controller; dtor delete). /*0x66d638/0x66d644*/
            EmoteVarController* ctl = new EmoteVarController();
            EmoteVarController_ctor(ctl, 1);                            // count=1

            // Push {ctl, null-label, flag=1} first, then assign label to the
            // pushed entry, preserving the binary's source order. /*0x66d660..0x66d770*/
            EmoteTransitionControlEntry_Deque8 entry;
            entry.ctl   = ctl;
            entry.flag  = 1;   // *(_BYTE*)(v8+16) = 1
            _auxVarDeque8.push_back(std::move(entry));
            _auxVarDeque8.back().label = detail::motionPropGetString(
                elem, TJS_W("label"));

            // HM#6 VarRef {type=7, index=v5} keyed by label. /*0x66d788/0x66d790*/
            detail::EmoteVarRef& ref =
                _scalarHM6_1384[_auxVarDeque8.back().label];
            ref.type  = 7;   // *v18 = 7
            ref.index = v5;  // v18[1] = v5
        }
    }

    // Aligned with libkrkr2.so EmoteEngine_buildLoopControl (sub_66E480) @0x66E480.
    // Decompiled pseudocode (this conversation):
    //   count = Motion_propGetCount(loopControl);                    // 0x66e514
    //   for (v6 = 0; v6 < count; ++v6) {                             // 0x66e550
    //     elem = loopControl[v6];                                    // PropGet(0,v6)
    //     if ((Motion_propGetBool(elem,"enabled") & 1) == 0) continue;// 0x66e5f0 gate -> LABEL_49
    //     transitionList = elem["transitionList"];                   // 0x66e61c
    //     kfCount = Motion_propGetCount(transitionList);             // 0x66e6a0
    //     ctl = operator new(0x20); zero;                            // 0x66e688 (+0..+31=0)
    //     ctl.keys.resize(kfCount);                                  // 0x66e6c4..0x66e6f4
    //     for (v20 = 0; v20 < kfCount; ++v20) {                      // 0x66e810 do/while
    //       kf = transitionList[v20];                                // 0x66e728
    //       ctl.keys[v20].v0   = (float)propGetIndexDouble(kf,0);    // 0x66e7a8 STR S
    //       ctl.keys[v20].v1   = (float)propGetIndexDouble(kf,1);    // 0x66e7c8 STR S
    //       ctl.keys[v20].span = (float)propGetIndexDouble(kf,2);    // 0x66e7e4 STR S
    //     }
    //     push_back deque#10 {ctl, label=0};                         // 0x66e828 (16B elem)
    //     label = elem["var_loop"];                                  // 0x66e8b4 (ttstr value)
    //     deque#10.back().label = label;                             // 0x66e944
    //     ref = HM6_findOrInsert(engine+1384, &back().label);        // 0x66e964 (a1+173)
    //     ref->type = 3; ref->index = v6;                            // 0x66e96c
    //   }
    // Structure mirrors buildTransitionControl/buildSelectorControl: enabled gate,
    //   new the controller, fill its keyframe vector, push {ctl,label} (16B),
    //   register HM#6 {type=3, index=loopIndex}. The HM#6 index is the LOOP index
    //   v6 (a skipped/disabled element still advances v6). The element label AND
    //   the HM#6 key are BOTH the "var_loop" value (binary: sub_A0BAF4 @0x66e90c
    //   produces the ttstr stored at elem+8 @0x66e944 and used as the HM6 key
    //   @0x66e964). The step for this deque is INLINED into progress (no separate
    //   step fn) — see EmoteLoopController_step / progress @0x67d2a0.
    //
    // FLOAT-BITS: each keyframe field is stored via `STR S` (single-precision)
    //   after propGetDouble narrows double->single — i.e. raw float bits, no
    //   integer remap. motionPropGetDoubleByNum followed by the explicit float
    //   narrowing is the local equivalent. The 12B keyframe POD
    //   {v0,v1,span} is a platform-independent data contract per the byte-layout
    //   methodology.
    void EmoteEngine::buildLoopControl(const tTJSVariant &loopControl) {
        const int count = detail::motionPropGetCount(loopControl);
        for (int v6 = 0; v6 < count; ++v6) {
            const auto elem = detail::motionPropGetByNum(loopControl, v6);
            if(!detail::motionPropGetBool(elem, TJS_W("enabled"))) {
                continue;
            }

            const auto transitionList = detail::motionPropGet(
                elem, TJS_W("transitionList"));
            const int kfCount = detail::motionPropGetCount(transitionList);

            auto *ctl = new EmoteLoopController();
            ctl->keys.resize(static_cast<size_t>(kfCount));

            for(int v20 = 0; v20 < kfCount; ++v20) {
                const auto frame = detail::motionPropGetByNum(
                    transitionList, v20);
                auto &dst = ctl->keys[static_cast<size_t>(v20)];
                dst.v0 = static_cast<float>(
                    detail::motionPropGetDoubleByNum(frame, 0));
                dst.v1 = static_cast<float>(
                    detail::motionPropGetDoubleByNum(frame, 1));
                dst.span = static_cast<float>(
                    detail::motionPropGetDoubleByNum(frame, 2));
            }

            // Push {ctl, empty label}, then CopyRef var_loop into back().label.
            // /*0x66e828 -> 0x66e944*/
            EmoteLoopControlEntry_Deque10 entry;
            entry.ctl = ctl;
            _lookupCurvesDeque10.push_back(std::move(entry));
            _lookupCurvesDeque10.back().label = detail::motionPropGetString(
                elem, TJS_W("var_loop"));

            // HM#6 VarRef {type=3, index=v6} keyed by the var_loop label. /*0x66e964/0x66e96c*/
            detail::EmoteVarRef& ref =
                _scalarHM6_1384[_lookupCurvesDeque10.back().label];
            ref.type  = 3;   // *v37 = 3
            ref.index = v6;  // v37[1] = v6
        }
    }

    // Aligned with libkrkr2.so EmoteEngine_buildClampControl @0x66EE5C.
    // Decompiled pseudocode (this conversation):
    //   count = Motion_propGetCount(clampControl);
    //   for (v5 = 0; v5 < count; ++v5) {
    //     elem = clampControl[v5];
    //     if (!propGetBool(elem, "enabled", false)) continue;
    //     deque#7.push_back({0,0.0,0.0,empty,empty});
    //     back.type = propGetInt(elem,"type",0);
    //     back.varLr = propGetString(elem,"var_lr",empty);
    //     back.varUd = propGetString(elem,"var_ud",empty);
    //     back.minValue = propGetDouble(elem,"min",0.0);
    //     back.maxValue = propGetDouble(elem,"max",0.0);
    //   }
    void EmoteEngine::buildClampControl(const tTJSVariant &clampControl) {
        const int count = detail::motionPropGetCount(clampControl);     // 0x66eef0
        for(int v5 = 0; v5 < count; ++v5) {                            // 0x66f210
            const tTJSVariant elem =
                detail::motionPropGetByNum(clampControl, v5);          // 0x66ef38
            if(!detail::motionPropGetBool(elem, TJS_W("enabled"))) {  // 0x66efbc
                continue;
            }

            // Binary zeroes all 40 bytes before advancing finish._M_cur.
            _clampControlDeque7.emplace_back();                        // 0x66efd8..0x66eff0
            EmoteClampControlEntry_Deque7 &back =
                _clampControlDeque7.back();

            back.type = detail::motionPropGetInt(
                elem, TJS_W("type"));                                // 0x66f070
            back.varLr = detail::motionPropGetString(
                elem, TJS_W("var_lr"));                              // 0x66f0c0..0x66f108
            back.varUd = detail::motionPropGetString(
                elem, TJS_W("var_ud"));                              // 0x66f144..0x66f18c
            back.minValue = detail::motionPropGetDouble(
                elem, TJS_W("min"));                                 // 0x66f1b8
            back.maxValue = detail::motionPropGetDouble(
                elem, TJS_W("max"));                                 // 0x66f1dc
        }
    }

    // Aligned with libkrkr2.so EmoteEngine_buildMirrorControl @0x66F364.
    // Decompiled pseudocode (this conversation):
    //   list = propGet(mirrorControl, "variableMatchList");
    //   count = Motion_propGetCount(list);
    //   for (v6 = 0; v6 < count; ++v6) {
    //     value = list[v6];
    //     text = Motion_propGetString(value); // missing/void -> empty ttstr
    //     variableMatchList800.push_back(text);
    //   }
    // No enabled gate, empty-string filter, deduplication, or builder-local clear.
    void EmoteEngine::buildMirrorControl(const tTJSVariant &mirrorControl) {
        const tTJSVariant variableMatchList = detail::motionPropGet(
            mirrorControl, TJS_W("variableMatchList"));             // 0x66f404
        const int count = detail::motionPropGetCount(variableMatchList); // 0x66f478
        for(int v6 = 0; v6 < count; ++v6) {                          // 0x66f530
            const tTJSVariant value = detail::motionPropGetByNum(
                variableMatchList, v6);                              // 0x66f4ac
            _variableMatchList800.push_back(
                ttstr(value));                                       // 0x66f4c0..0x66f500
        }
    }

    // Aligned with libkrkr2.so sub_67C6B0 @0x67C6B0.
    bool EmoteEngine::shouldMirrorEvalLabelLike_0x67C6B0(
        const ttstr &label) {
        if(!_mirrorChanged) {                                       // 0x67c6e0
            return false;
        }
        if(_mirrorMatchSetHM1_824.find(label) !=
           _mirrorMatchSetHM1_824.end()) {                          // 0x67c76c
            return true;
        }
        if(_mirrorMissSetHM2_880.find(label) !=
           _mirrorMissSetHM2_880.end()) {                           // 0x67c804
            return false;
        }

        for(const ttstr &pattern : _variableMatchList800) {         // 0x67c814
            if(label.IndexOf(pattern, 0) >= 1) {                    // 0x67c838
                _mirrorMatchSetHM1_824.insert(label);               // 0x67c874
                return true;
            }
        }
        _mirrorMissSetHM2_880.insert(label);                        // 0x67c858
        return false;
    }

    // sub_67C8A8 @0x67C8A8.
    void EmoteEngine::applyClampControlsLike_0x67C8A8() {
        Player &embeddedPlayer = player();
        for(const EmoteClampControlEntry_Deque7 &entry : _clampControlDeque7) {
            double lrValue = 0.0;
            double udValue = 0.0;
            if(const auto it = _labelToValueHM7.find(entry.varLr);
               it != _labelToValueHM7.end()) {
                lrValue = it->second;                               // 0x67c9b4..cc
            }
            if(const auto it = _labelToValueHM7.find(entry.varUd);
               it != _labelToValueHM7.end()) {
                udValue = it->second;                               // 0x67ca68..7c
            }

            accumulateTimelineContributionLike_0x67C560(
                entry.varLr, lrValue);                              // 0x67ca8c
            accumulateTimelineContributionLike_0x67C560(
                entry.varUd, udValue);                              // 0x67ca9c

            const double range = entry.maxValue - entry.minValue;
            double lrNorm = ((lrValue - entry.minValue) / range) * 2.0 - 1.0;
            double udNorm = ((udValue - entry.minValue) / range) * 2.0 - 1.0;
            if(lrNorm != 0.0 && udNorm != 0.0) {                    // 0x67cadc
                if(entry.type != 0) {
                    if(entry.type == 1 &&
                       std::sqrt(lrNorm * lrNorm + udNorm * udNorm) > 1.0) {
                        const double angle = std::atan2(udNorm, lrNorm);
                        lrNorm = std::cos(angle);
                        udNorm = std::sin(angle);                    // 0x67cbc4..e0
                    }
                } else {
                    const double rawRatio = std::abs(lrNorm / udNorm);
                    const double ratio = rawRatio <= 1.0
                        ? rawRatio : 1.0 / rawRatio;
                    const double invLen = 1.0 / std::sqrt(ratio * ratio + 1.0);
                    const double projectedX = lrNorm * invLen;
                    const double projectedY = invLen * udNorm;
                    const double projectedLength = std::sqrt(
                        projectedX * projectedX + projectedY * projectedY);
                    const double scale =
                        (1.0 - std::cos(ratio * 1.57079633)) *
                            (std::sin(projectedLength * 1.57079633) /
                                 projectedLength - 1.0) +
                        1.0;
                    lrNorm = projectedX * scale;
                    udNorm = scale * projectedY;                     // 0x67cb18..a0
                }
            }

            const double lrFinal = entry.minValue +
                range * (lrNorm + 1.0) * 0.5;
            const double udFinal = entry.minValue +
                range * (udNorm + 1.0) * 0.5;
            embeddedPlayer.bindParameterValueLike_0x6C4668(
                entry.varLr,
                shouldMirrorEvalLabelLike_0x67C6B0(entry.varLr)
                    ? -lrFinal : lrFinal);                           // 0x67cc10..30
            embeddedPlayer.bindParameterValueLike_0x6C4668(
                entry.varUd, udFinal);                               // 0x67cc44
        }
    }

    // Aligned with libkrkr2.so EmoteEngine_buildInstantVariableList @0x66F64C.
    // Decompiled pseudocode (this conversation):
    //   count = Motion_propGetCount(instantVariableList);
    //   for (v5 = 0; v5 < count; ++v5) {
    //     value = instantVariableList[v5];
    //     key = Motion_propGetString(value); // void -> empty ttstr
    //     instantVariableSetHM4_1272.insert(key);
    //   }
    // The optional property gate is in applyMetadata@0x67D4D0, not this helper.
    void EmoteEngine::buildInstantVariableList(
        const tTJSVariant &instantVariableList) {
        const int count = detail::motionPropGetCount(instantVariableList); // 0x66f6d8
        for(int v5 = 0; v5 < count; ++v5) {                              // 0x66f74c
            const tTJSVariant value = detail::motionPropGetByNum(
                instantVariableList, v5);                                // 0x66f70c
            _instantVariableSetHM4_1272.insert(ttstr(value));             // 0x66f720..0x66f734
        }
    }

    // Aligned with libkrkr2.so EmoteEngine_buildTimelineControl @0x66F80C.
    // Decompiled pseudocode (this conversation):
    //   clear normalLabels; clear diffLabels;
    //   for (v12 = 0; v12 < count; ++v12) {
    //     elem = timelineControl[v12];
    //     hasDiff = PropGet(TJS_MEMBERMUSTEXIST,"diff",probe); destroy probe;
    //     target = hasDiff && propGetBool(elem,"diff") ? diff : normal;
    //     label = propGetString(elem,"label"); target.push_back(label);
    //     HM3.findOrInsert(label).rawElement = elem;
    //   }
    void EmoteEngine::buildTimelineControl(const tTJSVariant &timelineControl) {
        // Binary releases all existing strings and resets end=begin for only
        // these two vectors before unwrapping the raw argument. +1040 is untouched.
        _timelineLabels992.clear();                                  // 0x66f840..0x66f874
        _timelineDiffLabels1016.clear();                              // 0x66f86c..0x66f8a4

        const int count = detail::motionPropGetCount(timelineControl); // 0x66f908
        for(int v12 = 0; v12 < count; ++v12) {                       // 0x66fb10
            const tTJSVariant elem = detail::motionPropGetByNum(
                timelineControl, v12);                               // 0x66f95c

            bool hasDiff = false;
            {
                // Keep the MEMBERMUSTEXIST probe result's lifetime separate:
                // the binary destroys it before optionally reading "diff" again.
                tTJSVariant probe;
                hasDiff = detail::motionTryPropGet(
                    elem, TJS_W("diff"), probe);                     // 0x66f9f0
            }

            std::vector<ttstr> &target =
                hasDiff && detail::motionPropGetBool(elem, TJS_W("diff"))
                    ? _timelineDiffLabels1016                        // 0x66fa24
                    : _timelineLabels992;

            const ttstr label = detail::motionPropGetString(
                elem, TJS_W("label"));                              // 0x66fa54..0x66fa68
            target.push_back(label);                                 // 0x66fa74..0x66fab4

            // Duplicate labels remain duplicated in the vector; findOrInsert
            // returns the existing HM3 value and CopyRef replaces its raw element.
            _compoundHM3_936[label].rawElement = elem;               // 0x66fac0..0x66fad4
        }
    }

    // ------------------------------------------------------------------------
    // Spring-physics deque builders (population path). sub_66B83C reads a raw
    // dictionary dispatch's x/y/z values and narrows them to floats.
    // ------------------------------------------------------------------------
    namespace {

        // sub_66B83C @0x66B83C: retain the raw dictionary variant for the full
        // x -> y -> z property-read sequence and narrow each result to float.
        void springVec3Raw(const tTJSVariant& dict, float out[3]) {
            out[0] = static_cast<float>(detail::motionPropGetDouble(
                dict, TJS_W("x")));
            out[1] = static_cast<float>(detail::motionPropGetDouble(
                dict, TJS_W("y")));
            out[2] = static_cast<float>(detail::motionPropGetDouble(
                dict, TJS_W("z")));
        }

    } // namespace

    // Aligned with libkrkr2.so sub_66B018 @ 0x66B018 ("bustControl" -> deque#1,
    //   the SIMPLE spring consumed by stepHairParts).
    // Decompiled pseudocode (this conversation):
    //   count = Motion_propGetCount(bustControl);                  // 0x66b0ac
    //   for (v5 = 0; v5 < count; ++v5) {                           // 0x66b0e0
    //     elem = bustControl[v5];                                  // PropGet(0,v5)
    //     if ((propGetBool(elem,"enabled") & 1) == 0) continue;    // 0x66b180 gate
    //     param = elem["param"];                                   // 0x66b1ac
    //     spring = operator new(0x48); EmoteSpringState_ctor(spring, elem); // 0x66b220
    //     op = param["op"];  spring[+36/+40/+44] = vec3(op);       // 0x66b280
    //     p  = param["p"];   spring[+48/+52/+56] = vec3(p);        // 0x66b2dc
    //     pv = param["pv"];  spring[+60/+64/+68] = vec3(pv);       // 0x66b338
    //     spring[+16] = (float)propGetDouble(param,"ofs");         // 0x66b368
    //     node = deque#1.emplace(); node.spring=spring; node.initFlag=1;       // 0x66b388
    //     node.shapeLabel = elem["baseLayer"];                     // 0x66b498 (+12)
    //     node.keyX = elem["var_lr"];                              // 0x66b530 (+20)
    //     node.keyY = elem["var_ud"];                              // 0x66b5b8 (+28)
    //     HM6[var_lr] = {type=0, index=v5};                        // 0x66b5d4
    //     HM6[var_ud] = {type=0, index=v5};                        // 0x66b5e8
    //   }
    // The ctor's vec3 fields (storedXYZ/posXYZ/velXYZ) are seeded to 0 then
    //   OVERWRITTEN here by op/p/pv (the binary writes after the ctor returns).
    void EmoteEngine::buildBustControl(const tTJSVariant& bustControl) {
        const int count = detail::motionPropGetCount(bustControl); // 0x66b0ac
        for (int v5 = 0; v5 < count; ++v5) {                     // 0x66b0e0
            const tTJSVariant elem =
                detail::motionPropGetByNum(bustControl, v5);     // 0x66b0fc
            if (!detail::motionPropGetBool(elem, TJS_W("enabled"))) {
                continue;                                        // 0x66b180 gate
            }

            const tTJSVariant param = detail::motionPropGet(
                elem, TJS_W("param"));                           // 0x66b1ac

            // `new T(elem)` does not zero the fields the ctor leaves untouched.
            EmoteSpringState* spring = new EmoteSpringState;     // 0x66b220
            EmoteSpringState_ctor(spring, elem);                  // 0x66b22c

            // op/p/pv vec3 (dict x/y/z) overwrite stored/pos/vel.  /*0x66b280..0x66b338*/
            float v3[3];
            springVec3Raw(detail::motionPropGet(param, TJS_W("op")), v3);
            spring->storedX = v3[0]; spring->storedY = v3[1]; spring->storedZ = v3[2]; // +36/+40/+44
            springVec3Raw(detail::motionPropGet(param, TJS_W("p")), v3);
            spring->posX = v3[0]; spring->posY = v3[1]; spring->posZ = v3[2];          // +48/+52/+56
            springVec3Raw(detail::motionPropGet(param, TJS_W("pv")), v3);
            spring->velX = v3[0]; spring->velY = v3[1]; spring->accZ = v3[2];          // +60/+64/+68
            spring->biasY = static_cast<float>(detail::motionPropGetDouble(
                param, TJS_W("ofs")));                                                // +16 0x66b368

            // Push pointer/init with the remaining node storage zeroed, then
            // assign the three ttstr slots in source order.
            EmoteHairPartsNode48B node;
            node.spring     = spring;
            node.initFlag   = 1;                                  // *(v19+8)=1
            node.anchorX    = 0.0f;
            node.anchorY    = 0.0f;
            _hairPartsNodes.push_back(std::move(node));
            EmoteHairPartsNode48B& back = _hairPartsNodes.back();
            back.shapeLabel = detail::motionPropGetString(
                elem, TJS_W("baseLayer"));                         // +12 0x66b498
            back.keyX = detail::motionPropGetString(
                elem, TJS_W("var_lr"));                            // +20 0x66b530
            back.keyY = detail::motionPropGetString(
                elem, TJS_W("var_ud"));                            // +28 0x66b5b8

            // HM#6 VarRef {type=0, index=v5} keyed by var_lr AND var_ud.
            detail::EmoteVarRef& refLr = _scalarHM6_1384[back.keyX];
            refLr.type = 0; refLr.index = v5;                     // 0x66b5d4
            detail::EmoteVarRef& refUd = _scalarHM6_1384[back.keyY];
            refUd.type = 0; refUd.index = v5;                     // 0x66b5e8
        }
    }

    // Aligned with libkrkr2.so sub_66B9D0 @ 0x66B9D0 ("hairControl"/"partsControl"
    //   -> deque#2/#3, the CHAIN spring consumed by stepBust). typeTag = 1 (hair)
    //   or 2 (parts), written into the HM#6 VarRef type.
    // Decompiled pseudocode (this conversation):
    //   count = Motion_propGetCount(chainControl);                 // 0x66ba70
    //   for (v10 = 0; v10 < count; ++v10) {                        // 0x66ba9c
    //     elem = chainControl[v10];                                // PropGet(0,v10)
    //     if ((propGetBool(elem,"enabled") & 1) == 0) continue;    // 0x66bb3c gate
    //     param = elem["param"];                                   // 0x66bb6c
    //     spring = operator new(0xB0); EmoteBustChainSpring_ctor(spring, elem);// 0x66bbdc
    //     op = param["op"];  spring[+80/+84/+88] = vec3(op);       // 0x66bc3c (root)
    //     spring[+44]=(float)propGetDouble(param,"ofs");           // 0x66bc6c
    //     spring[+48]=(float)propGetDouble(param,"bendR");         // 0x66bc94
    //     spring[+52]=(float)propGetDouble(param,"bendS");         // 0x66bcc0
    //     bp = param["bp"]; (list[2])                              // 0x66bcec ("bp")
    //     p  = param["p"]; (list[2])                               // 0x66bd70
    //     pv = param["pv"];(list[2])                               // 0x66bdf4
    //     spring[+92..] = vec3(p[0]); spring[+104..] = vec3(p[1]); // 0x66be94/0x66bee4
    //     spring[+116..]= vec3(pv[0]);spring[+128..] = vec3(pv[1]);// 0x66bf34/0x66bf84
    //     spring[+140..]= vec3(bp[0]);spring[+152..] = vec3(bp[1]);// 0x66bfd4/0x66c024
    //     node = deque.emplace(); node.spring=spring;              // 0x66c04c (node+8 NOT set)
    //     node.shapeLabel=elem["baseLayer"]; node.keyA=elem["var_lr"];     // +12/+20
    //     node.keyB=elem["var_lrm"]; node.keyC=elem["var_ud"];     // +28/+36
    //     HM6[var_lr]=HM6[var_lrm]=HM6[var_ud]={type=tag, index=v10};      // 0x66c310..
    //   }
    void EmoteEngine::buildChainControl(
        std::deque<EmoteBustChain1Node56B>& chainNodes, int typeTag,
        const tTJSVariant& chainControl) {
        const int count = detail::motionPropGetCount(chainControl); // 0x66ba70
        for (int v10 = 0; v10 < count; ++v10) {                   // 0x66ba9c
            const tTJSVariant elem =
                detail::motionPropGetByNum(chainControl, v10);    // 0x66bab8
            if (!detail::motionPropGetBool(elem, TJS_W("enabled"))) {
                continue;                                         // 0x66bb3c gate
            }

            const tTJSVariant param = detail::motionPropGet(
                elem, TJS_W("param"));                            // 0x66bb6c

            // operator new(0xB0) + chain ctor over the ELEMENT dict.
            EmoteBustChainSpring* spring = new EmoteBustChainSpring; // 0x66bbdc
            EmoteBustChainSpring_ctor(spring, elem);                 // 0x66bbe8

            uint8_t* const sp = reinterpret_cast<uint8_t*>(spring);
            auto SF = [sp](int off) -> float& { return *reinterpret_cast<float*>(sp + off); };

            // op = param["op"] (dict) -> root @+80/+84/+88.        /*0x66bc3c*/
            float v3[3];
            springVec3Raw(detail::motionPropGet(param, TJS_W("op")), v3);
            SF(80) = v3[0]; SF(84) = v3[1]; SF(88) = v3[2];
            // ofs/bendR/bendS (param doubles) -> +44/+48/+52.       /*0x66bc6c..*/
            SF(44) = static_cast<float>(detail::motionPropGetDouble(
                param, TJS_W("ofs")));
            SF(48) = static_cast<float>(detail::motionPropGetDouble(
                param, TJS_W("bendR")));
            SF(52) = static_cast<float>(detail::motionPropGetDouble(
                param, TJS_W("bendS")));

            // p / pv / bp are all 2-element lists under `param`.
            const tTJSVariant bp = detail::motionPropGet(
                param, TJS_W("bp"));                              // 0x66bcec
            const tTJSVariant p = detail::motionPropGet(
                param, TJS_W("p"));                               // 0x66bd70
            const tTJSVariant pv = detail::motionPropGet(
                param, TJS_W("pv"));                              // 0x66bdf4
            springVec3Raw(detail::motionPropGetByNum(p, 0), v3);  SF(92)  = v3[0]; SF(96)  = v3[1]; SF(100) = v3[2];
            springVec3Raw(detail::motionPropGetByNum(p, 1), v3);  SF(104) = v3[0]; SF(108) = v3[1]; SF(112) = v3[2];
            springVec3Raw(detail::motionPropGetByNum(pv, 0), v3); SF(116) = v3[0]; SF(120) = v3[1]; SF(124) = v3[2];
            springVec3Raw(detail::motionPropGetByNum(pv, 1), v3); SF(128) = v3[0]; SF(132) = v3[1]; SF(136) = v3[2];
            springVec3Raw(detail::motionPropGetByNum(bp, 0), v3); SF(140) = v3[0]; SF(144) = v3[1]; SF(148) = v3[2];
            springVec3Raw(detail::motionPropGetByNum(bp, 1), v3); SF(152) = v3[0]; SF(156) = v3[1]; SF(160) = v3[2];

            // emplace leaves node+8 untouched; the binary explicitly zeroes
            // labels/anchors before assigning the four ttstr fields.
            chainNodes.emplace_back();                              // 0x66c04c
            EmoteBustChain1Node56B& back = chainNodes.back();
            back.spring = spring;
            back.anchorX = 0.0f;
            back.anchorY = 0.0f;
            back.shapeLabel = detail::motionPropGetString(
                elem, TJS_W("baseLayer"));                         // 0x66c150
            back.keyA = detail::motionPropGetString(
                elem, TJS_W("var_lr"));                            // 0x66c1e8
            back.keyB = detail::motionPropGetString(
                elem, TJS_W("var_lrm"));                           // 0x66c270
            back.keyC = detail::motionPropGetString(
                elem, TJS_W("var_ud"));                            // 0x66c2f8

            // HM#6 VarRef {type=typeTag, index=v10} keyed by all three vars.
            detail::EmoteVarRef& refLr = _scalarHM6_1384[back.keyA];
            refLr.type = typeTag; refLr.index = v10;                // 0x66c318
            detail::EmoteVarRef& refLrm = _scalarHM6_1384[back.keyB];
            refLrm.type = typeTag; refLrm.index = v10;              // 0x66c338
            detail::EmoteVarRef& refUd = _scalarHM6_1384[back.keyC];
            refUd.type = typeTag; refUd.index = v10;                // 0x66c354
        }
    }

    // ========================================================================
    // setVariable value-dispatch (libkrkr2.so Player_setVariable @0x671228) and
    // the 5 per-category enqueue functions it calls. Each enqueue pushes a
    // transition keyframe {value, easing, factor} into the controller's internal
    // keyframe std::deque (the libstdc++ deque the binary indexes at a1+16..+72),
    // or — on the instant path (easing <= 0) — clears the deque and snaps the
    // controller's scalar state. Element fields are the controllers' named
    // keyframe types (EmoteAngleKeyValue12B / EmoteVarKeyValue20B); the binary's
    // raw float-triple {a3,a4,a5} maps to {value, duration(=easing arg), powCount
    // (=factor arg)} stored as RAW FLOAT BITS (the step reads powCount with
    // `LDR S, no SCVTF`).
    // ========================================================================
    namespace {

        // v22 transition factor (0x671304..0x671328). durationFrames is the 3rd
        //   binary arg (TJS "ease"); == variableEaseWeightLike_0x671228.
        float emoteTransitionFactorLike_0x671228(double durationFrames) {
            if (durationFrames == 0.0) {
                return 1.0f;                                       // 0x671308
            }
            if (durationFrames > 0.0) {
                return static_cast<float>(durationFrames + 1.0);   // 0x671318
            }
            return static_cast<float>(1.0 / (1.0 - durationFrames)); // 0x671328
        }

        // Aligned with libkrkr2.so sub_6638B0 (case 4, eye enqueue).
        //   if (easing <= 0): clear BOTH value-track deques (a1+16 / a1+96), then
        //     trackValue(+300)=value, trackState(+296)=0.
        //   else: if (flag&1) append to valueTrack12B.queue; else clear it (and
        //     reset trackState(+296)=0) then append. Element {value, easing, factor}.
        void emoteEnqueueEye_sub_6638B0(EmoteBlinkController* ctl, bool flag,
                                        float value, float easing, float factor) {
            if (easing <= 0.0f) {                                  // 0x6638e8
                ctl->valueTrack12B.queue.clear();                  // a1+16 swap-clear
                ctl->valueTrack8B.clear();                         // a1+96 swap-clear
                ctl->trackValue = value;                           // 0x663978  (+300)
                ctl->trackState = 0;                               // 0x66397c  (+296)
                return;
            }
            if (!flag) {                                           // 0x6638ec else
                ctl->valueTrack12B.queue.clear();                  // a1+16 swap-clear
                ctl->trackState = 0;                               // 0x663a00  (+296)
            }
            // push {endRad=value, duration=easing, powCount=factor(raw bits)}.
            EmoteAngleKeyValue12B kf;                              // 0x663a50 new block
            kf.endRad   = value;                                   // *v39
            kf.duration = easing;                                  // v39[1]
            std::memcpy(&kf.powCount, &factor, sizeof(float));     // v39[2] raw bits
            ctl->valueTrack12B.queue.push_back(kf);
        }

        // Aligned with libkrkr2.so sub_6652D4 (case 5, eyebrow enqueue).
        //   STRUCTURALLY IDENTICAL to sub_6638B0 but on the slim eyebrow
        //   controller (same +296/+300 track-state offsets, two value-track
        //   deques cleared on the instant path).
        void emoteEnqueueEyebrow_sub_6652D4(EmoteEyebrowController* ctl, bool flag,
                                            float value, float easing,
                                            float factor) {
            if (easing <= 0.0f) {                                  // 0x66530c
                ctl->valueTrack12B.queue.clear();                  // a1+16
                ctl->valueTrack8B.clear();                         // a1+96
                ctl->trackValue = value;                           // 0x66539c  (+300)
                ctl->trackState = 0;                               // 0x6653a0  (+296)
                return;
            }
            if (!flag) {                                           // 0x665310 else
                ctl->valueTrack12B.queue.clear();
                ctl->trackState = 0;                               // 0x665424  (+296)
            }
            EmoteAngleKeyValue12B kf;                              // 0x665474 new block
            kf.endRad   = value;
            kf.duration = easing;
            std::memcpy(&kf.powCount, &factor, sizeof(float));
            ctl->valueTrack12B.queue.push_back(kf);
        }

        // Aligned with libkrkr2.so sub_665E34 (case 6, mouth talk-ramp enqueue).
        //   SINGLE value-track deque (a1+16 only). Instant path: clear queue,
        //   currentValue(+84)=value, state(+80)=0. Push path mirrors the eye one
        //   but writes state(+80) (not +296) when clearing.
        void emoteEnqueueMouth_sub_665E34(EmoteMouthController* ctl, bool flag,
                                          float value, float easing, float factor) {
            if (easing <= 0.0f) {                                  // 0x665e68 false
                ctl->valueTrack12B.queue.clear();                  // a1+16
                ctl->currentValue = value;                         // 0x665ec8  (+84)
                ctl->state = 0;                                    // 0x665ecc  (+80)
                return;
            }
            if (!flag) {                                           // 0x665e6c else
                ctl->valueTrack12B.queue.clear();
                ctl->state = 0;                                    // 0x665f0c  (+80)
            }
            EmoteAngleKeyValue12B kf;                              // 0x665f68 new block
            kf.endRad   = value;
            kf.duration = easing;
            std::memcpy(&kf.powCount, &factor, sizeof(float));
            ctl->valueTrack12B.queue.push_back(kf);
        }

        // Aligned with libkrkr2.so Animator_setKeyframes @0x667300 (case 7,
        //   transition controller). The "keyframe" pushed carries `count` float
        //   channels (count=1 for transition controllers). value is a single
        //   float (the binary passes &(float)value). Instant path: clear queue,
        //   state(+84)=0, copy `count` floats from value into currentValue(+88).
        void emoteAnimatorSetKeyframes_0x667300(EmoteVarController* ctl, bool flag,
                                                float value, float easing,
                                                float factor) {
            if (easing <= 0.0f) {                                  // 0x667340
                ctl->queue.clear();                                // a1+16
                ctl->state = 0;                                    // 0x6673cc  (+84)
                // copy count floats from the value-array into currentValue.
                //   (0x6673d4..0x66745c: memcpy count ints.) Here value is a
                //   single scalar broadcast to channel 0 (count==1 case).
                if (ctl->count >= 1 && ctl->currentValue) {
                    for (int i = 0; i < ctl->count; ++i) {
                        ctl->currentValue[i] = value;              // count==1 -> [0]
                    }
                }
                return;
            }
            if (!flag) {                                           // 0x667344 == 0
                ctl->queue.clear();
                ctl->state = 0;                                    // 0x667378  (+84)
            }
            // push a 20B keyframe {channel[0]=value, duration=easing, powCount=
            //   factor(raw bits)} (EmoteVarController_deque20B_pushback 0x667390).
            EmoteVarKeyValue20B kf;
            kf.channel[0] = value;
            kf.channel[1] = 0.0f;
            kf.channel[2] = 0.0f;
            kf.duration   = easing;
            std::memcpy(&kf.powCount, &factor, sizeof(float));     // raw bits
            ctl->queue.push_back(kf);
        }

        // Aligned with libkrkr2.so sub_6681E4 (case 8, selector command enqueue).
        //   SINGLE command-track deque (a1+16, the base 12B track). Instant path:
        //   clear queue, selState(+84)=0, then applySelection(ctl, (int)value,
        //   0, 0). Push path appends {selIdx=value, dur=easing, fade=factor}.
        void emoteEnqueueSelector_sub_6681E4(EmoteSelectorController* ctl, bool flag,
                                             float value, float easing,
                                             float factor) {
            if (easing <= 0.0f) {                                  // 0x668218 false
                ctl->commandTrack12B.queue.clear();                // a1+16
                ctl->selState = 0;                                 // 0x668274  (+84)
                EmoteSelectorController_applySelection(
                    ctl, static_cast<int>(value), 0.0f, 0.0f);     // 0x6682a4
                return;
            }
            if (!flag) {                                           // 0x66821c else
                ctl->commandTrack12B.queue.clear();
                ctl->selState = 0;                                 // 0x6682e0  (+84)
            }
            EmoteAngleKeyValue12B kf;                              // 0x66833c new block
            kf.endRad   = value;                                   // selIdx
            kf.duration = easing;                                  // dur
            std::memcpy(&kf.powCount, &factor, sizeof(float));     // fade raw bits
            ctl->commandTrack12B.queue.push_back(kf);
        }

    } // namespace

    // Aligned with libkrkr2.so Player_setVariable @ 0x671228 (see header for the
    //   arg-name mapping and the full step list). `this` IS the EmoteEngine.
    void EmoteEngine::setVariable(const ttstr& key, double value, double easing,
                                  double durationFrames) {
        // HM6 lookup (sub_6887F4 @0x6712f0). Empty key hashes to 0; the binary
        //   still performs the lookup. A miss => no entry => HM2 fallthrough.
        auto it = _scalarHM6_1384.find(key);                       // 0x6712f0
        if (it != _scalarHM6_1384.end()) {                         // result != 0  0x6712f4
            const detail::EmoteVarRef& ref = it->second;           // *(QWORD*)result  0x6712f8
            // v22 transition factor (0x671304..0x671328).
            const float factor = emoteTransitionFactorLike_0x671228(durationFrames);
            const float vEasing = static_cast<float>(easing);
            const float vValue  = static_cast<float>(value);
            const bool flag = _emoteAnimatorFlag;                  // *(BYTE*)(this+1161)

            _dirty = true;                                         // 0x671330  (+1162)

            switch (ref.type) {                                    // *(int*)(varref+16)  0x671350
                case 0:
                case 1:
                case 2:
                    // 0x671354: cases 0/1/2 fall through to the HM2 scalar write
                    //   ONLY when _syncWaiting(+1159) is set (`if(this+1159) break;
                    //   else return`). Otherwise they leave the value un-written
                    //   here (the spring target/const feed is a SEPARATE pass).
                    if (_syncWaiting) {                            // 0x671354
                        break;                                     // -> HM2 write
                    }
                    return;                                        // 0x671358
                case 4: {
                    // deque#4[ref.index] -> enqueue eye (sub_6638B0).  0x67139c
                    EmoteEyeControlEntry_Deque4& entry =
                        _stateMachineDeque4[ref.index];
                    emoteEnqueueEye_sub_6638B0(entry.ctl, flag, vValue,
                                               vEasing, factor); // 0x67155c
                    return;
                }
                case 5: {
                    // deque#5[ref.index] -> enqueue eyebrow (sub_6652D4). 0x6713c4
                    EmoteEyebrowControlEntry_Deque5& entry =
                        _stateMachineDeque5[ref.index];
                    emoteEnqueueEyebrow_sub_6652D4(entry.ctl, flag, vValue,
                                                   vEasing, factor); // 0x671588
                    return;
                }
                case 6: {
                    // deque#6[ref.index]. Dual-key controller: if `key` equals the
                    //   element's "label" -> write ctl->beginFrame(+108)=(int)value
                    //   directly (LABEL_68 @0x6716a8). If `key` equals the
                    //   element's "talkLabel" -> enqueue the talk ramp (sub_665E34
                    //   @0x6716a0). (Binary compares pointer-eq then wcscmp.)
                    EmoteMouthControlEntry_Deque6& entry =
                        _compositeVarDeque6[ref.index];           // 0x6713ec
                    if (entry.label == key) {                      // 0x6715d0 / LABEL_68
                        entry.ctl->beginFrame = static_cast<int>(value); // 0x6716a8 (+108)
                        return;
                    }
                    if (entry.talkLabel == key) {                  // 0x671688
                        emoteEnqueueMouth_sub_665E34(entry.ctl, flag, vValue,
                                                     vEasing, factor); // 0x6716a0
                    }
                    return;
                }
                case 7: {
                    // deque#8[ref.index] (transition). The element flag byte@+16
                    //   gates the enqueue (`if(!*(BYTE*)(v38+16)) return`).  0x671424
                    EmoteTransitionControlEntry_Deque8& entry =
                        _auxVarDeque8[ref.index];
                    if (!entry.flag) {                             // 0x67145c / 0x6716f0
                        return;
                    }
                    emoteAnimatorSetKeyframes_0x667300(entry.ctl, flag, vValue,
                                                       vEasing, factor); // 0x671710
                    return;
                }
                case 8: {
                    // deque#9[ref.index] (selector). Element flag byte@+16 gates
                    //   the enqueue (`if(!*(BYTE*)(v42+16)) return`).  0x671468
                    EmoteSelectorControlEntry_Deque9& entry =
                        _vectorVarDeque9[ref.index];
                    // case8 enqueue gate (`LDRB [elem+16]; CBNZ` @0x6714a0 /
                    //   0x671740). The builder leaves elem+16 un-initialised in
                    //   the binary (only +0/+8/+24/+32/+40 written); modelled as
                    //   entry.flag (default 1) — see the EmoteSelectorControlEntry
                    //   _Deque9 +16 note for the indeterminacy rationale.
                    if (!entry.flag) {                             // 0x6714a0 / 0x671740
                        return;
                    }
                    emoteEnqueueSelector_sub_6681E4(entry.ctl, flag, vValue,
                                                    vEasing, factor); // 0x671758
                    return;
                }
                default:
                    return;                                        // 0x671350 default
            }
        }

        // HM2 upsert fallthrough (0x67135c..0x671368): reached on HM6 miss, or a
        //   case 0/1/2 with _syncWaiting set. `*(double*)result = value`.
        _labelToValueHM7[key] = value;                             // 0x671368
    }

    // sub_66FC5C @0x66FC5C — lazily materialize the timeline state stored in
    // HM3. The raw element remains owned by the HM3 mapped value; this function
    // creates the two nested heap objects and their internal containers.
    void EmoteEngine::initializeTimelineStateLike_0x66FC5C(
        detail::EmoteHM3Value &state) {
        detail::EmoteTimelineData80B *timelineData =
            new detail::EmoteTimelineData80B();                    // 0x66fca4..b4
        delete state.timelineData;
        state.timelineData = timelineData;                         // 0x66fcb8..d0

        state.loopBegin = detail::motionPropGetDouble(
            state.rawElement, TJS_W("loopBegin"));                 // 0x66fd50
        state.loopEnd = detail::motionPropGetDouble(
            state.rawElement, TJS_W("loopEnd"));                   // 0x66fd74
        state.lastTime = detail::motionPropGetDouble(
            state.rawElement, TJS_W("lastTime"));                  // 0x66fd98
        state.blendWeight = 1.0f;                                  // 0x66fda4
        state.autoStop = 0.0;                                      // 0x66fda8

        EmoteVarController *blendController = new EmoteVarController();
        EmoteVarController_ctor(blendController, 1);               // 0x66fdb4..c0
        if(state.blendController) {
            EmoteVarController_dtor(state.blendController);
            delete state.blendController;
        }
        state.blendController = blendController;                   // 0x66fdc4..dc
        emoteAnimatorSetKeyframes_0x667300(
            blendController, false, state.blendWeight, 0.0f, 0.0f); // 0x66fde4..0x66fe80

        const tTJSVariant variableList = detail::motionPropGet(
            state.rawElement, TJS_W("variableList"));              // 0x66feb0
        const int variableCount = detail::motionPropGetCount(variableList);
        double maxFrameTime = 0.0;
        for(int variableIndex = 0; variableIndex < variableCount;
            ++variableIndex) {
            const tTJSVariant variable = detail::motionPropGetByNum(
                variableList, variableIndex);                      // 0x66ff70
            const tTJSVariant frameList = detail::motionPropGet(
                variable, TJS_W("frameList"));                     // 0x670000

            timelineData->variableList.emplace_back();             // 0x670070..0x67010c
            detail::EmoteTimelineTrack56B &track =
                timelineData->variableList.back();
            track.label = detail::motionPropGetString(
                variable, TJS_W("label"));                         // 0x670130..0x6701a4
            track.instantVariable =
                _instantVariableSetHM4_1272.find(track.label) !=
                _instantVariableSetHM4_1272.end();                 // 0x6701b8..0x670268

            const int frameCount = detail::motionPropGetCount(frameList);
            for(int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
                const tTJSVariant rawFrame = detail::motionPropGetByNum(
                    frameList, frameIndex);                         // 0x670298
                track.frameList.emplace_back();                    // 0x6702b0..0x6702dc
                detail::EmoteTimelineFrame24B &frame =
                    track.frameList.back();
                frame.time = detail::motionPropGetDouble(
                    rawFrame, TJS_W("time"));                      // 0x670354
                const int type = detail::motionPropGetInt(
                    rawFrame, TJS_W("type"));                      // 0x670378
                if(frame.time > maxFrameTime) {
                    maxFrameTime = frame.time;                     // 0x670384
                }
                frame.typeZero = type == 0;                        // 0x670390
                if(type != 0) {
                    const tTJSVariant content = detail::motionPropGet(
                        rawFrame, TJS_W("content"));               // 0x67039c..0x670424
                    frame.value = static_cast<float>(
                        detail::motionPropGetDouble(
                            content, TJS_W("value")));             // 0x670444..0x67044c
                    const double easing = detail::motionPropGetDouble(
                        content, TJS_W("easing"));                 // 0x67046c
                    frame.easingWeight = easing == 0.0
                        ? 1.0
                        : easing > 0.0 ? easing + 1.0
                                       : 1.0 / (1.0 - easing);     // 0x670474..0x670490
                }
            }
        }
        if(state.lastTime < 0.0) {
            state.lastTime = maxFrameTime;                         // 0x670550..0x670554
        }
    }

    // sub_670840 @0x670840.
    void EmoteEngine::initializeTimelineControllersLike_0x670840(
        detail::EmoteHM3Value &state, tjs_uint32 flags) {
        state.flags = flags;                                       // 0x670870
        if((flags & 2u) == 0) {
            return;
        }
        for(detail::EmoteTimelineTrack56B &track :
            state.timelineData->variableList) {
            if(track.frameList.empty() || track.instantVariable) { // 0x6708b8
                continue;
            }
            if(!track.controller) {
                track.controller = new EmoteVarController();
                EmoteVarController_ctor(track.controller, 1);      // 0x670934..0x670944
            } else {
                emoteAnimatorSetKeyframes_0x667300(
                    track.controller, false, 0.0f, 0.0f, 0.0f);    // 0x6708cc..0x670924
            }
        }
    }

    // sub_671A50 @0x671A50.
    void EmoteEngine::seekTimelineLike_0x671A50(
        detail::EmoteHM3Value &state, double time) {
        state.frameCursors.clear();                                // 0x671a90
        for(detail::EmoteTimelineTrack56B &track :
            state.timelineData->variableList) {
            if((state.flags & 4u) != 0 && track.instantVariable) { // 0x671c60
                continue;
            }
            const bool internalRoute =
                (state.flags & 2u) != 0 && !track.instantVariable; // 0x671c7c
            std::size_t cursor = 0;
            int lastActionFrame = -1;
            if(track.frameList.size() >= 2) {
                const std::size_t scanCount = track.frameList.size() - 1;
                for(cursor = 0; cursor < scanCount; ++cursor) {
                    const detail::EmoteTimelineFrame24B &frame =
                        track.frameList[cursor];
                    if(!frame.typeZero) {
                        lastActionFrame = static_cast<int>(cursor); // 0x671ca8..b4
                    }
                    if(frame.time <= time &&
                       track.frameList[cursor + 1].time > time) {
                        break;                                     // 0x671cc8
                    }
                }
            }
            state.frameCursors.push_back(static_cast<int32_t>(cursor)); // 0x671ae0..0x671bb0
            if(lastActionFrame < 0) {
                continue;
            }

            const detail::EmoteTimelineFrame24B &frame =
                track.frameList[static_cast<std::size_t>(lastActionFrame)];
            const double transition = std::max(
                track.frameList[static_cast<std::size_t>(lastActionFrame) + 1].time -
                    time - 1.0,
                0.0);                                              // 0x671bb8..0x671bd0
            if(internalRoute) {
                emoteAnimatorSetKeyframes_0x667300(
                    track.controller, _emoteAnimatorFlag, frame.value,
                    static_cast<float>(transition),
                    static_cast<float>(frame.easingWeight));       // 0x671bd4..0x671c00
            } else {
                setVariable(track.label, frame.value, transition,
                            frame.easingWeight);                    // 0x671c08..0x671c28
            }
        }
        state.currentTime = time;                                  // 0x671ce0
    }

    // sub_669E1C @0x669E1C.
    void EmoteEngine::applyTimelineWindowLike_0x669E1C(
        detail::EmoteHM3Value &state, bool inclusive,
        double targetTime) {
        std::size_t trackIndex = 0;
        for(detail::EmoteTimelineTrack56B &track :
            state.timelineData->variableList) {
            if((state.flags & 4u) != 0 && track.instantVariable) { // 0x669f78..0x669f80
                ++trackIndex;
                continue;
            }
            const bool internalRoute =
                (state.flags & 2u) != 0 && !track.instantVariable; // 0x669fcc
            int32_t cursor = state.frameCursors[trackIndex];
            const std::size_t frameCount = track.frameList.size();
            if(cursor < static_cast<int32_t>(frameCount) - 1) {
                auto crossed = [inclusive, targetTime](double frameTime) {
                    return inclusive ? frameTime <= targetTime
                                     : frameTime < targetTime;
                };
                while(crossed(track.frameList[
                                  static_cast<std::size_t>(cursor) + 1].time)) {
                    const std::size_t nextIndex =
                        static_cast<std::size_t>(cursor) + 1;
                    const detail::EmoteTimelineFrame24B &next =
                        track.frameList[nextIndex];
                    if(!next.typeZero && nextIndex + 1 < frameCount) {
                        const double transition = std::max(
                            track.frameList[nextIndex + 1].time - targetTime - 1.0,
                            0.0);                                  // 0x66a048..0x66a058
                        if(internalRoute) {
                            emoteAnimatorSetKeyframes_0x667300(
                                track.controller, _emoteAnimatorFlag, next.value,
                                static_cast<float>(transition),
                                static_cast<float>(next.easingWeight)); // 0x66a078..0x66a080
                        } else {
                            setVariable(track.label, next.value, transition,
                                        next.easingWeight);        // 0x669ebc
                        }
                    }
                    cursor = static_cast<int32_t>(nextIndex);
                    if(nextIndex >= frameCount - 1) {
                        break;
                    }
                }
            }
            state.frameCursors[trackIndex] = cursor;               // 0x669f0c..0x669f14
            ++trackIndex;
        }
        state.currentTime = targetTime;                             // 0x66a0b8
    }

    // EmoteEngine_playTimeline @0x672F70 (the old IDB Player_ owner was wrong;
    // all +936/+1040 accesses and NCB native a4 prove the receiver is Engine).
    void EmoteEngine::playTimelineLike_0x672F70(
        const ttstr &label, tjs_uint32 flags) {
        if((flags & 1u) != 0) {
            stopTimelineLike_0x67C2A0(ttstr());                     // 0x672fa8..0x672fd0
        }
        const auto found = _compoundHM3_936.find(label);           // 0x672fd4..0x67305c
        if(found == _compoundHM3_936.end()) {
            TVPThrowExceptionMessage(
                TJS_W("timeline label not found '%1'."), label);   // 0x673168..0x6731c8
        }
        if(std::find(_activeTimelineLabels1040.begin(),
                     _activeTimelineLabels1040.end(), label) ==
           _activeTimelineLabels1040.end()) {
            _activeTimelineLabels1040.push_back(label);            // 0x67306c..0x673130
        }
        detail::EmoteHM3Value &state = found->second;
        if(!state.timelineData) {
            initializeTimelineStateLike_0x66FC5C(state);           // 0x673134..0x673144
        }
        initializeTimelineControllersLike_0x670840(state, flags);  // 0x673148
        seekTimelineLike_0x671A50(state, 0.0);                     // 0x673154..0x673160
    }

    // EmoteEngine_stopTimeline @0x67C2A0.
    void EmoteEngine::stopTimelineLike_0x67C2A0(const ttstr &label) {
        if(label.IsEmpty()) {
            _activeTimelineLabels1040.clear();                     // 0x67c2f4..0x67c320
            return;
        }
        const auto found = std::find(_activeTimelineLabels1040.begin(),
                                     _activeTimelineLabels1040.end(), label);
        if(found != _activeTimelineLabels1040.end()) {
            _activeTimelineLabels1040.erase(found);                // tail @0x68C200
        }
    }

    bool EmoteEngine::isTimelinePlayingLike_0x673558(
        const ttstr &label) const {
        return std::find(_activeTimelineLabels1040.begin(),
                         _activeTimelineLabels1040.end(), label) !=
               _activeTimelineLabels1040.end();                    // 0x673568..0x6735a8
    }

    // sub_6735AC @0x6735AC.
    void EmoteEngine::setTimelineBlendLike_0x6735AC(
        const ttstr &label, bool autoStop, float value,
        float transition, float easingWeight) {
        const auto found = _compoundHM3_936.find(label);
        if(found == _compoundHM3_936.end()) {
            return;                                                 // 0x673674..0x673678
        }
        detail::EmoteHM3Value &state = found->second;
        if(!state.timelineData) {
            initializeTimelineStateLike_0x66FC5C(state);            // 0x673688..0x673694
        }
        emoteAnimatorSetKeyframes_0x667300(
            state.blendController, _emoteAnimatorFlag, value,
            transition, easingWeight);                             // 0x6736ac
        state.autoStop = autoStop ? 1.0 : 0.0;                     // 0x6736b8
    }

    // sub_6736EC @0x6736EC.
    void EmoteEngine::fadeInTimelineLike_0x6736EC(
        const ttstr &label, double duration, double easing) {
        const float transition = static_cast<float>(duration);
        const float easingWeight = static_cast<float>(
            easing == 0.0 ? 1.0
                          : easing > 0.0 ? easing + 1.0
                                         : 1.0 / (1.0 - easing));  // 0x6737c0..0x673860
        if(!isTimelinePlayingLike_0x673558(label)) {
            playTimelineLike_0x672F70(label, 3u);                  // 0x673878..0x6738a4
            setTimelineBlendLike_0x6735AC(
                label, false, 0.0f, 0.0f, 1.0f);                  // 0x6738c0
        }
        setTimelineBlendLike_0x6735AC(
            label, false, 1.0f, transition, easingWeight);         // 0x6738dc
    }

    // sub_6739F4 @0x6739F4.
    void EmoteEngine::fadeOutTimelineLike_0x6739F4(
        const ttstr &label, double duration, double easing) {
        const float transition = static_cast<float>(duration);
        const float easingWeight = static_cast<float>(
            easing == 0.0 ? 1.0
                          : easing > 0.0 ? easing + 1.0
                                         : 1.0 / (1.0 - easing));  // 0x673ac8..0x673b68
        setTimelineBlendLike_0x6735AC(
            label, true, 0.0f, transition, easingWeight);          // 0x673b84
    }

    // sub_6821C8 @0x6821C8.
    double EmoteEngine::getTimelineBlendLike_0x6821C8(
        const ttstr &label) const {
        const auto found = _compoundHM3_936.find(label);
        if(found != _compoundHM3_936.end() && found->second.timelineData) {
            return found->second.blendWeight;                      // 0x682268..0x682288
        }
        return 0.0;                                                // 0x68226c
    }

    tjs_int EmoteEngine::countMainTimelinesLike_0x5306AC() const {
        return static_cast<tjs_int>(_timelineLabels992.size());
    }

    ttstr EmoteEngine::getMainTimelineLabelAtLike_0x674C84(
        tjs_uint32 index) const {
        if(index >= _timelineLabels992.size()) {
            return ttstr();                                       // 0x674cd0
        }
        return _timelineLabels992[index];                          // 0x674cb0
    }

    tjs_int EmoteEngine::countDiffTimelinesLike_0x5306D4() const {
        return static_cast<tjs_int>(_timelineDiffLabels1016.size());
    }

    ttstr EmoteEngine::getDiffTimelineLabelAtLike_0x674CEC(
        tjs_uint32 index) const {
        if(index >= _timelineDiffLabels1016.size()) {
            return ttstr();                                       // 0x674d40
        }
        return _timelineDiffLabels1016[index];                     // 0x674d18
    }

    tjs_int EmoteEngine::countPlayingTimelinesLike_0x5306FC() const {
        return static_cast<tjs_int>(_activeTimelineLabels1040.size());
    }

    ttstr EmoteEngine::getPlayingTimelineLabelAtLike_0x674D54(
        tjs_uint32 index) const {
        if(index >= _activeTimelineLabels1040.size()) {
            return ttstr();                                       // 0x674da8
        }
        return _activeTimelineLabels1040[index];                   // 0x674d80
    }

    tjs_int EmoteEngine::getPlayingTimelineFlagsAtLike_0x674DC8(
        tjs_uint32 index) const {
        const ttstr label = getPlayingTimelineLabelAtLike_0x674D54(index);
        const auto found = _compoundHM3_936.find(label);
        if(found == _compoundHM3_936.end()) {
            return 0;                                             // 0x674ef4
        }
        return static_cast<tjs_int>(found->second.flags);          // 0x674ef8
    }

    bool EmoteEngine::getLoopTimelineLike_0x67522C(
        const ttstr &label) const {
        const auto found = _compoundHM3_936.find(label);
        if(found != _compoundHM3_936.end()) {
            return found->second.loopBegin >= 0.0;                 // 0x6752e8
        }
        TVPThrowExceptionMessage(
            TJS_W("timeline label not found '%1'."), label);       // 0x675310..0x675360
        return false;
    }

    double EmoteEngine::getTimelineTotalFrameCountLike_0x6753F0(
        const ttstr &label) const {
        const auto found = _compoundHM3_936.find(label);
        if(found != _compoundHM3_936.end() &&
           found->second.loopBegin >= 0.0) {
            return found->second.lastTime;                         // 0x6754ac..0x6754b0
        }
        return 0.0;                                                // 0x675494
    }

    tTJSVariant EmoteEngine::getMainTimelineLabelListLike_0x674F54() const {
        detail::TJSArrayWithItems_guess result =
            detail::createTJSArrayWithItems_guess();
        for(const ttstr &label : _timelineLabels992) {
            result.items->emplace_back(label);                     // 0x674f94..0x675058
        }
        return result.value;
    }

    tTJSVariant EmoteEngine::getDiffTimelineLabelListLike_0x6750C0() const {
        detail::TJSArrayWithItems_guess result =
            detail::createTJSArrayWithItems_guess();
        for(const ttstr &label : _timelineDiffLabels1016) {
            result.items->emplace_back(label);                     // 0x675100..0x6751c4
        }
        return result.value;
    }

    tTJSVariant EmoteEngine::getPlayingTimelineInfoListLike_0x6754C4() const {
        detail::TJSArrayWithItems_guess result =
            detail::createTJSArrayWithItems_guess();
        for(const ttstr &label : _activeTimelineLabels1040) {
            const auto found = _compoundHM3_936.find(label);
            if(found == _compoundHM3_936.end()) {
                continue;                                         // 0x6755ac
            }

            const detail::EmoteHM3Value &state = found->second;
            tTJSVariant dictionary = createTJSDictionaryLike_0x9C8440();
            iTJSDispatch2 *dispatch = dictionary.AsObjectNoAddRef();
            tTJSVariant labelValue(label);
            tTJSVariant flagsValue(static_cast<tjs_int>(state.flags));
            tTJSVariant blendValue(static_cast<double>(state.blendWeight));
            dispatch->PropSet(TJS_MEMBERENSURE, TJS_W("label"), nullptr,
                              &labelValue, dispatch);               // 0x675664..0x675690
            dispatch->PropSet(TJS_MEMBERENSURE, TJS_W("flags"), nullptr,
                              &flagsValue, dispatch);               // 0x6756b8
            dispatch->PropSet(TJS_MEMBERENSURE, TJS_W("blendRatio"), nullptr,
                              &blendValue, dispatch);               // 0x6756d8
            result.items->push_back(dictionary);                   // 0x6756e0..0x675710
        }
        return result.value;
    }

    // sub_67C560 @0x67C560.
    void EmoteEngine::accumulateTimelineContributionLike_0x67C560(
        const ttstr &label, double &value) {
        for(const ttstr &timelineLabel : _activeTimelineLabels1040) {
            detail::EmoteHM3Value &state = _compoundHM3_936[timelineLabel];
            if((state.flags & 2u) == 0) {                           // 0x67c5b4
                continue;
            }
            for(const detail::EmoteTimelineTrack56B &track :
                state.timelineData->variableList) {
                if(track.instantVariable || track.frameList.empty()) {
                    continue;                                      // 0x67c5fc
                }
                if(track.label == label) {
                    value += static_cast<float>(
                        track.output * state.blendWeight);          // 0x67c67c
                }
            }
        }
    }

    // Aligned with libkrkr2.so sub_67D01C EmoteEngine_progress @ 0x67D01C.
    //
    // Binary main loop (from EmoteEngine_controllers.md):
    //
    //   EmoteEngine_preProgress_guess(this, false, dt); // 0x671764
    //   while (dt > 0 || _dirty@1162):
    //       step = fmin(dt, 1.1)           // physics step cap
    //       _dirty@1162 = false
    //       for each elem in deque#4..deque#10 (skip #7 pool): step_fn(elem, step)
    //       applyVarControllers_pos_scale_color_angle(step)
    //       if (player@1128 && player+1544 flag) sub_6687E8(step)
    //       dt -= step
    //   // post-loop (G2-C bind-loop, LIVE — bridges HM7 -> Player HM1/HM2):
    //   for (entry = HM7@+1456; entry; entry = entry->next):
    //       sub_67C560 / sub_67C6B0 / Player_bindParameterValue
    //   sub_67C8A8(this); sub_6D2A54(player, 0, dt);
    //   if (dt != 0 && !syncWaiting@1159):
    //       EmoteVarController_step(_ctlHairPartsTarget, v71, dt);
    //       EmoteVarController_step(_ctlBust1Target,     v71, dt);
    //       EmoteVarController_step(_ctlBust2Target,     v71, dt);
    //       stepHairParts(dt);
    //       stepBust(_ctlBust1Target, _bustChain1Nodes, _bustSpring1Const, dt);
    //       stepBust(_ctlBust2Target, _bustChain2Nodes, _bustSpring2Const, dt);
    //
    // Physics step functions (stepHairParts @0x67B748, stepBust @0x67BCE8) are
    // fully ported and their deques are now POPULATED by buildBustControl
    // (deque#1) / buildChainControl (deque#2/#3) from applyMetadata. They run on
    // real spring nodes; the only remaining un-wired inputs are the controller
    // TARGETS (_ctlHairPartsTarget/_ctlBust1/2Target @+1104/+1112/+1120) and the
    // spring CONSTANTS (_bustSpring1/2Const @+1184/+1192), set by the variableList/
    // setVariable resolution path (still open) — until then cur[]/springConst = 0.
    void EmoteEngine::preProgressLike_0x671764(bool force, double dt) {
        // EmoteEngine_preProgress_guess @0x671764 entry gate:
        //   if (dt != 0.0 || (force & 1) != 0) { ... }
        if(dt == 0.0 && !force) {
            return;
        }

        std::size_t activeIndex = 0;
        while(activeIndex < _activeTimelineLabels1040.size()) {
            detail::EmoteHM3Value &state =
                _compoundHM3_936[_activeTimelineLabels1040[activeIndex]]; // 0x6717b4..bc
            double remaining = dt;
            if(state.loopBegin < 0.0) {                            // 0x6717c4..d4
                applyTimelineWindowLike_0x669E1C(
                    state, true, state.currentTime + remaining);   // 0x6717d8..e8
            } else {
                while(state.currentTime + remaining >= state.loopEnd) {
                    remaining -= state.loopEnd - state.currentTime;
                    applyTimelineWindowLike_0x669E1C(
                        state, false, state.loopEnd);               // 0x6718b0..c4
                    seekTimelineLike_0x671A50(
                        state, state.loopBegin);                    // 0x6718c8..d4
                }
                applyTimelineWindowLike_0x669E1C(
                    state, true,
                    state.currentTime + std::max(remaining, 0.0)); // 0x6718ec..0x671900
            }

            if((state.flags & 2u) != 0) {
                const float step = static_cast<float>(remaining);
                EmoteVarController_step(
                    state.blendController, &state.blendWeight, step); // 0x6717f4..0x671800 / 0x67190c..1c
                for(detail::EmoteTimelineTrack56B &track :
                    state.timelineData->variableList) {
                    if(!track.frameList.empty() && !track.instantVariable) {
                        EmoteVarController_step(
                            track.controller, &track.output, step); // 0x671828..0x671848
                    }
                }
            }

            const bool blendFinished = state.autoStop != 0.0 &&
                state.blendController->state == 0 &&
                state.blendController->queue.empty();              // 0x671868..0x67188c
            if(state.lastTime <= state.currentTime || blendFinished) {
                _activeTimelineLabels1040.erase(
                    _activeTimelineLabels1040.begin() +
                    static_cast<std::ptrdiff_t>(activeIndex));      // 0x6719a8..0x671a1c
            } else {
                ++activeIndex;
            }
        }
    }

    // sub_6767E4 @0x6767E4.
    tTJSVariant EmoteEngine::serializeTimelineLike_0x6767E4() const {
        detail::TJSArrayWithItems_guess result =
            detail::createTJSArrayWithItems_guess();
        for(const ttstr &label : _activeTimelineLabels1040) {
            const auto found = _compoundHM3_936.find(label);
            if(found == _compoundHM3_936.end()) {
                continue;
            }
            const detail::EmoteHM3Value &state = found->second;
            tTJSVariant item = createTJSDictionaryLike_0x9C8440();
            setTJSProperty(item, TJS_W("label"), tTJSVariant(label));
            setTJSProperty(item, TJS_W("flags"),
                           tTJSVariant(static_cast<tjs_int>(state.flags)));
            setTJSProperty(item, TJS_W("curTime"),
                           tTJSVariant(state.currentTime));
            setTJSProperty(item, TJS_W("blendRatioCtrl"),
                           serializeVarControllerLike_0x66767C(
                               state.blendController));
            setTJSProperty(item, TJS_W("stopWhenBlendDone"),
                           tTJSVariant(state.autoStop));
            result.items->push_back(item);
        }
        return result.value;
    }

    // sub_676B0C @0x676B0C.
    tTJSVariant EmoteEngine::serializeEyeLike_0x676B0C() const {
        detail::TJSArrayWithItems_guess result =
            detail::createTJSArrayWithItems_guess();
        for(const EmoteEyeControlEntry_Deque4 &entry :
            _stateMachineDeque4) {
            result.items->push_back(
                serializeEyeControllerState(entry.label, entry.ctl));
        }
        return result.value;
    }

    // sub_676F48 @0x676F48.
    tTJSVariant EmoteEngine::serializeEyebrowLike_0x676F48() const {
        detail::TJSArrayWithItems_guess result =
            detail::createTJSArrayWithItems_guess();
        for(const EmoteEyebrowControlEntry_Deque5 &entry :
            _stateMachineDeque5) {
            result.items->push_back(
                serializeEyebrowControllerState(entry.label, entry.ctl));
        }
        return result.value;
    }

    // sub_677384 @0x677384.
    tTJSVariant EmoteEngine::serializeMouthLike_0x677384() const {
        detail::TJSArrayWithItems_guess result =
            detail::createTJSArrayWithItems_guess();
        for(const EmoteMouthControlEntry_Deque6 &entry :
            _compositeVarDeque6) {
            result.items->push_back(
                serializeMouthControllerState(entry.label, entry.ctl));
        }
        return result.value;
    }

    // sub_6776BC @0x6776BC.
    tTJSVariant EmoteEngine::serializeTransitionLike_0x6776BC() const {
        detail::TJSArrayWithItems_guess result =
            detail::createTJSArrayWithItems_guess();
        for(const EmoteTransitionControlEntry_Deque8 &entry : _auxVarDeque8) {
            tTJSVariant item =
                serializeVarControllerLike_0x66767C(entry.ctl);
            setTJSProperty(item, TJS_W("label"), tTJSVariant(entry.label));
            result.items->push_back(item);
        }
        return result.value;
    }

    // sub_6778F0 @0x6778F0.
    tTJSVariant EmoteEngine::serializeSelectorLike_0x6778F0() const {
        detail::TJSArrayWithItems_guess result =
            detail::createTJSArrayWithItems_guess();
        for(const EmoteSelectorControlEntry_Deque9 &entry :
            _vectorVarDeque9) {
            result.items->push_back(
                serializeSelectorControllerState(entry.label, entry.ctl));
        }
        return result.value;
    }

    // sub_677BA8 @0x677BA8.
    tTJSVariant EmoteEngine::serializeBaseLike_0x677BA8() const {
        tTJSVariant result = createTJSDictionaryLike_0x9C8440();
        setTJSProperty(result, TJS_W("coord"),
                       serializeVarControllerLike_0x66767C(_ctlPosition));
        setTJSProperty(result, TJS_W("scale"),
                       serializeVarControllerLike_0x66767C(_ctlScale));
        setTJSProperty(result, TJS_W("color"),
                       serializeVarControllerLike_0x66767C(_ctlColor));
        setTJSProperty(result, TJS_W("rotate"),
                       serializeAngleControllerLike_0x666830(_ctlAngle));
        return result;
    }

    // sub_677E28 @0x677E28. Literal key order and controller slots are exact:
    // bust=Engine[138](+1104), hair=[139](+1112), parts=[140](+1120).
    tTJSVariant EmoteEngine::serializeOuterForceLike_0x677E28() const {
        tTJSVariant result = createTJSDictionaryLike_0x9C8440();
        setTJSProperty(result, TJS_W("bust"),
                       serializeVarControllerLike_0x66767C(
                           _ctlHairPartsTarget));
        setTJSProperty(result, TJS_W("hair"),
                       serializeVarControllerLike_0x66767C(_ctlBust1Target));
        setTJSProperty(result, TJS_W("parts"),
                       serializeVarControllerLike_0x66767C(_ctlBust2Target));
        return result;
    }

    // sub_675E40 @0x675E40.
    tTJSVariant EmoteEngine::serializeLike_0x675E40() {
        preProgressLike_0x671764(true, 0.0);

        for(EmoteEyeControlEntry_Deque4 &entry : _stateMachineDeque4) {
            float value;
            EmoteBlinkController_step(entry.ctl, &value, 0.0f);
            _labelToValueHM7[entry.label] = value;
        }
        for(EmoteEyebrowControlEntry_Deque5 &entry : _stateMachineDeque5) {
            float value;
            EmoteEyebrowController_step(entry.ctl, &value, 0.0f);
            _labelToValueHM7[entry.label] = value;
        }
        for(EmoteMouthControlEntry_Deque6 &entry : _compositeVarDeque6) {
            float mouth;
            float talk;
            EmoteMouthController_step(entry.ctl, &mouth, &talk, 0.0f);
            _labelToValueHM7[entry.label] = mouth;
            _labelToValueHM7[entry.talkLabel] = talk;
        }
        for(EmoteSelectorControlEntry_Deque9 &entry : _vectorVarDeque9) {
            float value;
            EmoteSelectorController_step(entry.ctl, &value, 0.0f);
            _labelToValueHM7[entry.label] = value;
        }
        for(EmoteTransitionControlEntry_Deque8 &entry : _auxVarDeque8) {
            float value;
            EmoteVarController_step(entry.ctl, &value, 0.0f);
            _labelToValueHM7[entry.label] = value;
        }
        applyVarControllers_pos_scale_color_angle(0.0f);

        tTJSVariant result = createTJSDictionaryLike_0x9C8440();
        setTJSProperty(result, TJS_W("timeline"),
                       serializeTimelineLike_0x6767E4());
        setTJSProperty(result, TJS_W("eye"), serializeEyeLike_0x676B0C());
        setTJSProperty(result, TJS_W("eyebrow"),
                       serializeEyebrowLike_0x676F48());
        setTJSProperty(result, TJS_W("mouth"),
                       serializeMouthLike_0x677384());
        setTJSProperty(result, TJS_W("transition"),
                       serializeTransitionLike_0x6776BC());
        setTJSProperty(result, TJS_W("selector"),
                       serializeSelectorLike_0x6778F0());
        setTJSProperty(result, TJS_W("base"), serializeBaseLike_0x677BA8());
        setTJSProperty(result, TJS_W("outerforce"),
                       serializeOuterForceLike_0x677E28());
        return result;
    }

    // sub_678454 @0x678454.
    void EmoteEngine::restoreTimelineLike_0x678454(
        const tTJSVariant &value) {
        stopTimelineLike_0x67C2A0(ttstr());
        tTJSArrayNI *array = tryGetTJSArrayNative(value);
        if(!array) {
            return;
        }
        for(const tTJSVariant &item : array->Items) {
            if(item.Type() != tvtObject) {
                continue;
            }
            tTJSVariant labelValue;
            if(!tryGetTJSProperty(item, TJS_W("label"), labelValue)) {
                continue;
            }
            const ttstr label(labelValue);
            auto found = _compoundHM3_936.find(label);
            if(found == _compoundHM3_936.end()) {
                continue;
            }

            tjs_uint32 flags = 0;
            double curTime = 0.0;
            tTJSVariant field;
            if(tryGetTJSProperty(item, TJS_W("flags"), field)) {
                flags = static_cast<tjs_uint32>(field.AsInteger());
            }
            if(tryGetTJSProperty(item, TJS_W("curTime"), field)) {
                curTime = field.AsReal();
            }
            playTimelineLike_0x672F70(label, flags);
            detail::EmoteHM3Value &state = found->second;
            applyTimelineWindowLike_0x669E1C(state, true, curTime);
            restoreDoubleIfPresent(item, TJS_W("stopWhenBlendDone"),
                                   state.autoStop);
            if(tryGetTJSProperty(item, TJS_W("blendRatioCtrl"), field)) {
                restoreVarControllerLike_0x667ADC(
                    state.blendController, field);
            }
        }
    }

    // sub_678804 @0x678804.
    void EmoteEngine::restoreEyeLike_0x678804(const tTJSVariant &value) {
        tTJSArrayNI *array = tryGetTJSArrayNative(value);
        if(!array) {
            return;
        }
        for(const tTJSVariant &item : array->Items) {
            if(item.Type() != tvtObject) {
                continue;
            }
            tTJSVariant labelValue;
            if(!tryGetTJSProperty(item, TJS_W("label"), labelValue)) {
                continue;
            }
            const ttstr label(labelValue);
            const auto found = std::find_if(
                _stateMachineDeque4.begin(), _stateMachineDeque4.end(),
                [&label](const EmoteEyeControlEntry_Deque4 &entry) {
                    return entry.label == label;
                });
            if(found != _stateMachineDeque4.end()) {
                restoreEyeControllerLike_0x663FC8(found->ctl, item);
            }
        }
    }

    // sub_678FF0 @0x678FF0.
    void EmoteEngine::restoreEyebrowLike_0x678FF0(
        const tTJSVariant &value) {
        tTJSArrayNI *array = tryGetTJSArrayNative(value);
        if(!array) {
            return;
        }
        for(const tTJSVariant &item : array->Items) {
            if(item.Type() != tvtObject) {
                continue;
            }
            tTJSVariant labelValue;
            if(!tryGetTJSProperty(item, TJS_W("label"), labelValue)) {
                continue;
            }
            const ttstr label(labelValue);
            const auto found = std::find_if(
                _stateMachineDeque5.begin(), _stateMachineDeque5.end(),
                [&label](const EmoteEyebrowControlEntry_Deque5 &entry) {
                    return entry.label == label;
                });
            if(found != _stateMachineDeque5.end()) {
                restoreEyebrowControllerLike_0x665844(found->ctl, item);
            }
        }
    }

    // sub_679804 @0x679804.
    void EmoteEngine::restoreMouthLike_0x679804(const tTJSVariant &value) {
        tTJSArrayNI *array = tryGetTJSArrayNative(value);
        if(!array) {
            return;
        }
        for(const tTJSVariant &item : array->Items) {
            if(item.Type() != tvtObject) {
                continue;
            }
            tTJSVariant labelValue;
            if(!tryGetTJSProperty(item, TJS_W("label"), labelValue)) {
                continue;
            }
            const ttstr label(labelValue);
            const auto found = std::find_if(
                _compositeVarDeque6.begin(), _compositeVarDeque6.end(),
                [&label](const EmoteMouthControlEntry_Deque6 &entry) {
                    return entry.label == label;
                });
            if(found != _compositeVarDeque6.end()) {
                restoreMouthControllerLike_0x6661A8(found->ctl, item);
            }
        }
    }

    // sub_67A020 @0x67A020.
    void EmoteEngine::restoreTransitionLike_0x67A020(
        const tTJSVariant &value) {
        tTJSArrayNI *array = tryGetTJSArrayNative(value);
        if(!array) {
            return;
        }
        for(const tTJSVariant &item : array->Items) {
            if(item.Type() != tvtObject) {
                continue;
            }
            tTJSVariant labelValue;
            if(!tryGetTJSProperty(item, TJS_W("label"), labelValue)) {
                continue;
            }
            const ttstr label(labelValue);
            const auto found = std::find_if(
                _auxVarDeque8.begin(), _auxVarDeque8.end(),
                [&label](const EmoteTransitionControlEntry_Deque8 &entry) {
                    return entry.label == label;
                });
            if(found != _auxVarDeque8.end()) {
                restoreVarControllerLike_0x667ADC(found->ctl, item);
            }
        }
    }

    // sub_67A868 @0x67A868.
    void EmoteEngine::restoreSelectorLike_0x67A868(
        const tTJSVariant &value) {
        tTJSArrayNI *array = tryGetTJSArrayNative(value);
        if(!array) {
            return;
        }
        for(const tTJSVariant &item : array->Items) {
            if(item.Type() != tvtObject) {
                continue;
            }
            tTJSVariant labelValue;
            if(!tryGetTJSProperty(item, TJS_W("label"), labelValue)) {
                continue;
            }
            const ttstr label(labelValue);
            const auto found = std::find_if(
                _vectorVarDeque9.begin(), _vectorVarDeque9.end(),
                [&label](const EmoteSelectorControlEntry_Deque9 &entry) {
                    return entry.label == label;
                });
            if(found != _vectorVarDeque9.end()) {
                restoreSelectorControllerLike_0x668570(found->ctl, item);
            }
        }
    }

    // sub_67B08C @0x67B08C.
    void EmoteEngine::restoreBaseLike_0x67B08C(const tTJSVariant &value) {
        if(value.Type() != tvtObject) {
            return;
        }
        restoreVarControllerLike_0x667ADC(
            _ctlPosition, detail::motionPropGet(value, TJS_W("coord")));
        restoreVarControllerLike_0x667ADC(
            _ctlScale, detail::motionPropGet(value, TJS_W("scale")));
        restoreVarControllerLike_0x667ADC(
            _ctlColor, detail::motionPropGet(value, TJS_W("color")));
        restoreAngleControllerLike_0x666A14(
            _ctlAngle, detail::motionPropGet(value, TJS_W("rotate")));
    }

    // sub_67B34C @0x67B34C.
    void EmoteEngine::restoreOuterForceLike_0x67B34C(
        const tTJSVariant &value) {
        if(value.Type() != tvtObject) {
            return;
        }
        restoreVarControllerLike_0x667ADC(
            _ctlHairPartsTarget,
            detail::motionPropGet(value, TJS_W("bust")));
        restoreVarControllerLike_0x667ADC(
            _ctlBust1Target,
            detail::motionPropGet(value, TJS_W("hair")));
        restoreVarControllerLike_0x667ADC(
            _ctlBust2Target,
            detail::motionPropGet(value, TJS_W("parts")));
    }

    // EmoteEngine unserialize entry @0x678044.
    void EmoteEngine::unserializeLike_0x678044(tTJSVariant data) {
        data.ToObject();
        iTJSDispatch2 *dispatch = data.AsObject();
        data.Clear();

        const auto getChild = [dispatch](const tjs_char *name) {
            tTJSVariant value;
            (void)dispatch->PropGet(0, name, nullptr, &value, dispatch);
            return value;
        };
        try {
            restoreTimelineLike_0x678454(getChild(TJS_W("timeline")));
            restoreEyeLike_0x678804(getChild(TJS_W("eye")));
            restoreEyebrowLike_0x678FF0(getChild(TJS_W("eyebrow")));
            restoreMouthLike_0x679804(getChild(TJS_W("mouth")));
            restoreTransitionLike_0x67A020(getChild(TJS_W("transition")));
            restoreSelectorLike_0x67A868(getChild(TJS_W("selector")));
            restoreBaseLike_0x67B08C(getChild(TJS_W("base")));
            restoreOuterForceLike_0x67B34C(getChild(TJS_W("outerforce")));
        } catch(...) {
            dispatch->Release();
            throw;
        }
        dispatch->Release();
    }

    void EmoteEngine::progress(float dt) {
        // P0-B2 FIX: top-level gate. The binary (EmoteEngine_progress @0x67D01C,
        // thunk 0x530a5c) opens with `if (*(double*)&a2 != 0.0)` at 0x530a60 and
        // does NOTHING when dt == 0.0 — not even the _dirty drain loop nor the
        // bind-loop / physics pass. The previous local code entered the
        // `while(dt>0 || _dirty)` loop unconditionally, so a dt==0 frame with
        // _dirty set incorrectly ran one slice. Gate the whole body on dt!=0.
        if (dt == 0.0f) {
            return; // /*0x530a60 false branch*/
        }

        // P0-B3 SETUP: the binary keeps the ORIGINAL dt in v12 across the whole
        // function (set at 0x67d054 from a2) and reuses it for the final
        // physics pass gate/argument (0x67d414/0x67d420). The dt-slice loop
        // drains a SEPARATE copy (v14, from 0x67d080). Mirror that split here:
        // `dt` is the drained working copy; `originalDt` is v12.
        const float originalDt = dt; // v12 @0x67d054

        // 0x67D050 W1=WZR; 0x67D054 preserves the original V0 dt; X0 remains
        // this EmoteEngine through BL @0x67D060. Must run exactly once before
        // the dt-slice loop, never from Player_progress_inner @0x6C106C.
        preProgressLike_0x671764(false, originalDt); // BL 0x67D060

        // dt-slice main loop with physics step cap = 1.1f.
        // Binary: `if (dt>0) goto LABEL_6` enters the do-while body; the inner
        // `do{...; dt-=step;}while(dt>0)` drains dt; the outer `while(_dirty)`
        // re-runs while the dirty flag is still set. `while(dt>0 || _dirty)`
        // is the faithful flattening (first iteration guaranteed when dt>0;
        // subsequent iterations gated by remaining dt or the dirty flag).
        while (dt > 0.0f || _dirty) {
            const float step = std::fmin(dt, 1.1f);
            _dirty = false;

            // 6 active deques iterated by step functions (per binary
            //   EmoteEngine_progress @0x67D01C). Each is a deque OF POINTERS:
            //   element = { EmoteVarController* ctl; ttstr key } (e.g. #4 elem
            //   16B = ptr@0 + ttstr@8; #6/#8 24B add a 2nd/3rd ttstr key). The
            //   loop calls the per-controller step then upserts the result into
            //   HM7 keyed by elem's ttstr:
            //     #4 sub_663BDC, #5 sub_665600, #6 sub_666068, #8 EmoteVarController_step,
            //     #9 sub_668470, #10 inline curve lookup.
            // POPULATION (corrected 2026-06-03, was wrongly "setVariable fills
            //   these"): setVariable @0x671228 does NOT push elements — it hash-
            //   looks-up an EXISTING HM@+1384 entry, reads its type tag (+16) and
            //   pre-stored index (+20), and for type 4 indexes into this already-
            //   built deque to drive the controller (sub_6638B0). The initial
            //   builder (operator new controller + push {ctl,key} + register
            //   HM entry {type,index}) lives in the EmoteObject_init motion-load
            //   path and is NOT YET PORTED, so the deques stay empty (step inert).
            //   sub_663FC8 deserializes a controller from a PSB dict; sub_678044's
            //   per-category children (sub_678804 "eye" etc.) only RELOAD saved
            //   state into already-built controllers — also not the builder.
            //
            // Deque#4 (eye) step — PORTED (M2 eye vertical). Per binary
            //   EmoteEngine_progress @0x67d0a4..0x67d104: for each {ctl,label}
            //   entry, sub_663BDC(ctl, &out, step) then HM7[label] = out (the
            //   Player_HM2_upsert_labelToValue(+1440,..) call IS the HM#7
            //   double-map upsert keyed by elem.label).
            for (EmoteEyeControlEntry_Deque4& entry : _stateMachineDeque4) {
                float out = 0.0f;
                EmoteBlinkController_step(entry.ctl, &out, step); // sub_663BDC
                _labelToValueHM7[entry.label] = out;              // HM7 upsert @0x67d0f4
            }
            // Deque#5 (eyebrow) step — PORTED (M2 eyebrow vertical). Per binary
            //   EmoteEngine_progress @0x67d10c..0x67d160: for each {ctl,label}
            //   entry, sub_665600(ctl, &out, step) then HM7[label] = out (the
            //   Player_HM2_upsert_labelToValue(+1440, v23+1) call IS the HM#7
            //   double-map upsert keyed by elem.label; 16B stride, advance v23+=2).
            for (EmoteEyebrowControlEntry_Deque5& entry : _stateMachineDeque5) {
                float out = 0.0f;
                EmoteEyebrowController_step(entry.ctl, &out, step); // sub_665600
                _labelToValueHM7[entry.label] = out;                // HM7 upsert @0x67d150
            }
            // Deque#6 (mouth) step — PORTED (M2 mouth vertical). Per binary
            //   EmoteEngine_progress @0x67d168..0x67d1d8: for each 24B {ctl,label,
            //   talkLabel} entry, sub_666068(ctl, &outBeginFrame, &outCurrentValue,
            //   step) then HM7[label] = outBeginFrame (Player_HM2_upsert via v30+1)
            //   and HM7[talkLabel] = outCurrentValue (via v30+2); advance v30+=3
            //   (24B stride). This is the only deque whose step feeds TWO HM7 keys.
            for (EmoteMouthControlEntry_Deque6& entry : _compositeVarDeque6) {
                float outBeginFrame   = 0.0f;
                float outCurrentValue = 0.0f;
                EmoteMouthController_step(entry.ctl, &outBeginFrame,
                                          &outCurrentValue, step);   // sub_666068
                _labelToValueHM7[entry.label]     = outBeginFrame;   // HM7 upsert @0x67d1b4
                _labelToValueHM7[entry.talkLabel] = outCurrentValue; // HM7 upsert @0x67d1c8
            }
            // Deque#9 (selector) step — PORTED (M2 selector vertical). Binary
            //   order: the selector deque (engine+656) is stepped BEFORE the
            //   transition deque (engine+576) — see EmoteEngine_progress
            //   @0x67d1e0 (selector) then @0x67d240 (transition). Per
            //   @0x67d1e0..0x67d238: for each 48B {ctl,label} entry,
            //   sub_668470(ctl, &out, step) then HM7[label] = out (the
            //   Player_HM2_upsert_labelToValue(+1440, v38+1) call IS the HM#7
            //   double-map upsert keyed by elem.label; advance v38+=6 = 48B
            //   stride, block boundary node+60 = 480B). The step output is the
            //   selected option index (as float).
            for (EmoteSelectorControlEntry_Deque9& entry : _vectorVarDeque9) {
                float out = 0.0f;
                EmoteSelectorController_step(entry.ctl, &out, step); // sub_668470
                _labelToValueHM7[entry.label] = out;                // HM7 upsert @0x67d228
            }
            // Deque#8 (transition) step — PORTED (M2 transition vertical). Per
            //   binary EmoteEngine_progress @0x67d240..0x67d298: for each 24B
            //   {ctl,label,flag} entry, EmoteVarController_step(ctl, v71, step)
            //   then HM7[label] = v71[0] (the Player_HM2_upsert_labelToValue(
            //   v13+1440, v45+1) call IS the HM#7 double-map upsert keyed by
            //   elem.label; advance v45+=3 = 24B stride, block boundary node+63 =
            //   504B). The controller is count=1, so out[0] is the single channel.
            //   The flag byte@+16 is NOT read by this loop (only by setVariable
            //   case7). Stepped AFTER selector (binary @0x67d1e0 selector, then
            //   @0x67d240 transition).
            for (EmoteTransitionControlEntry_Deque8& entry : _auxVarDeque8) {
                float out = 0.0f;
                EmoteVarController_step(entry.ctl, &out, step);  // sub_666BF8
                _labelToValueHM7[entry.label] = out;             // HM7 upsert @0x67d288
            }
            // Deque#10 (loopControl) step — PORTED (M2 loopControl vertical).
            //   Per binary EmoteEngine_progress @0x67d2a0..0x67d370: for each 16B
            //   {ctl,label} entry, run the INLINE curve sampler (advance accum by
            //   step, wrap the keyframe index, blend v0/v1) then HM7[label] = out
            //   (the Player_HM2_upsert_labelToValue(+1440, v52+1) call IS the HM#7
            //   double-map upsert keyed by elem.label; advance v52+=2 = 16B stride,
            //   block boundary node+64 = 512B). There is NO standalone step fn in
            //   the binary — the sampler is open-coded here, factored into
            //   EmoteLoopController_step so this loop mirrors the per-entry body.
            //   The output is the curve blend cast float->double @0x67d35c.
            for (EmoteLoopControlEntry_Deque10& entry : _lookupCurvesDeque10) {
                const float out = EmoteLoopController_step(entry.ctl, step); // @0x67d2a0
                _labelToValueHM7[entry.label] = out;                         // HM7 upsert @0x67d360
            }

            // Apply the 4 direct controllers (pos/scale/color/angle).
            applyVarControllers_pos_scale_color_angle(step);

            // Wind emitter step (gated) — PORTED.
            //   Per binary EmoteEngine_progress @0x67d384..0x67d398:
            //       v65 = *(engine+1128);                       // wind emitter ptr
            //       if (v65 && *(byte*)(v65+1544))              // alloc'd AND gate on
            //           EmoteWindEmitter_step(v65, step);       // sub_6687E8(windObj, clampedStep)
            //   The X0 arg to sub_6687E8 is the emitter object (engine+1128), the
            //   float arg is `step` = fmin(dt,1.1) (the same clamped per-slice
            //   delta the deque steps use, V0 = V9 = v5 in the binary). The gate
            //   byte (+1544) is set by Player_startWind_populate; when wind is
            //   inactive the emitter is null/gate-clear and this is skipped.
            if (_windEmitter && _windEmitter->gate) {            /*0x67d384..0x67d390*/
                _windEmitter->step(step);                        /*0x67d394..0x67d398*/
            }

            dt -= step;
        }

        // Post-loop bind-loop (G2-C keystone): the binary
        // (EmoteEngine_progress @0x67D01C, body @0x67d3a4) walks HM#7's
        // _M_before_begin._M_nxt node chain (insertion order) at +1456:
        //   for (i = *(this+1456); i; i = *i) {
        //       sub_67C560(this, i+1, i+2);          // var-track weighted cascade,
        //                                            //   mutates i.value (node+16) in place
        //       v67 = i[2];                          // read accumulated value
        //       v68 = sub_67C6B0(this, i+1);         // negate-flag resolver
        //       v69 = (v68 & 1) ? -v67 : v67;
        //       Player_bindParameterValue(*(this+1064), i+1, 0, v69);  // write Player HM1/HM2
        //   }
        // i.key = node+8 (ttstr label), i.value = node+16 (double) — i.e. each
        // _labelToValueHM7 entry.
        //
        // The three callees now use the binary owners directly:
        //   sub_67C560            -> this Engine's HM3/+1040/nested track deque
        //   sub_67C6B0            -> this Engine's +800/+824/+880 mirror state
        //   Player_bindParameter  -> embedded Player::bindParameterValueLike_0x6C4668
        //                            which writes HM1 (_evalCascadeMap[joined].writeVal)
        //                            and HM2 (_evalResultValues[rawKey]) = the two maps
        //                            getVariable reads (R0-1).
        //
        // PLATFORM_BOUNDARY (insertion-order): libstdc++ chains HM7 nodes in
        // insertion order on _M_before_begin; libc++/this port has no
        // insertion-ordered chain, so we iterate the typed _labelToValueHM7 in
        // bucket order. Each bind writes a distinct label slot in Player HM1/HM2
        // (no inter-label ordering dependence in the binary's bind body), so the
        // observable HM1/HM2 result is order-independent; the boundary is benign.
        Player& p = player();                                       // *(this+1064)
        for (auto& kv : _labelToValueHM7) {
            const ttstr& label = kv.first;
            // sub_67C560(this, &label, &value): accumulate var-track timeline
            //   contribution into the HM7 node value in place (binary mutates
            //   i.value at node+16; we mutate the map value).
            double& value = kv.second;
            accumulateTimelineContributionLike_0x67C560(label, value);

            // v67 = i[2] (read back the accumulated value).
            const double accumulated = value;

            // v68 = sub_67C6B0(this, &label); negate = v68 & 1.
            const bool negate = shouldMirrorEvalLabelLike_0x67C6B0(label);

            // Player_bindParameterValue(player, &label, 0, negate ? -v67 : v67):
            //   write Player HM1/HM2 (the getVariable read surface).
            p.bindParameterValueLike_0x6C4668(label, negate ? -accumulated
                                                             : accumulated);
        }

        // sub_67C8A8(this) @0x67d3f8 — clampControl binder. Runs AFTER the HM7
        //   bind-loop (above) and BEFORE the Player-level progress sub_6D2A54
        //   (below). It strides the engine's 40B clampControl deque (deque#7
        //   @engine+496, populated by EmoteEngine_buildClampControl @0x66EE5C;
        //   element = {int type@+0, double min@+8, double max@+16, ttstr var_lr@+24,
        //   ttstr var_ud@+32}), and per entry: reads two ENGINE-HM7 values keyed by
        //   var_lr (X) / var_ud (Y) (sub_67C8A8 v6 = result+180 = engine+1440 = HM7,
        //   NOT player HM2), runs the var-track cascade sub_67C560 on each, normalizes
        //   to [-1,1] over [min,max], 2D disk-remaps by mode (0=squircle,
        //   1=clamp-circle), then writes both back via Player_bindParameterValue
        //   (engine+1064), the X result negated when sub_67C6B0 (mirror) is set.
        //   The per-entry body and sub_67C560 both consume this Engine's raw
        //   deque/HM7/HM3/+1040 containers directly.
        //
        //   TOPOLOGY (2026-06-03 approved migration): this clamp now runs HERE, in
        //   EmoteEngine::progress, exactly where the binary places it — @0x67d3f8,
        //   after the bind-loop @0x67d3a4 and before sub_6D2A54 @0x67d408. It was
        //   formerly duplicated on the Player progress path through a local model
        //   of caller-less binary sub_67CC9C; that entire dead local model and its
        //   snapshot clamp table have now been removed. Player_progress_inner
        //   @0x6C106C and the child-motion pass
        //   @0x6BE2A4 both run progress_inner WITHOUT any bind-loop or clamp (fresh-
        //   decompile confirmed this round), so the Player progress path must not
        //   carry it. Single invocation per frame here — no double-clamp.
        applyClampControlsLike_0x67C8A8();                          // @0x67d3f8

        // Step 7 — Player-level progress @0x67d408:
        //     sub_6D2A54(*(this+1064)=Player, 0, v12=originalDt);
        //   sub_6D2A54 (= local Player::progressFramesLike_0x6D2A54) sets
        //   player+16=0 (pendingEvents cursor -> _pendingEvents.clear()), runs
        //   progress_inner / updateLayers / calcBounds / dispatchEvents. Placed
        //   AFTER the G2-C bind-loop (so the bound HM1/HM2 values are already
        //   written before the Player frame seek/eval reads them) and BEFORE the
        //   bust/hair physics gate. The binary passes v12 (ORIGINAL FRAME dt), not
        //   the drained dt-slice copy and NOT a ms value — sub_6D2A54 forwards it
        //   straight to progress_inner with NO *60/1000 conversion (that lives in
        //   the NCB wrappers). Use progressFramesLike_0x6D2A54 (frame-units), NOT
        //   progressMsLike_0x6D2A54 (which would double-not-convert and re-scale a
        //   frame value by 0.06). The progress ENTRY (D3DEmotePlayer::progress /
        //   EmotePlayer::progress) routes through engine().progress, which calls
        //   this once here — the entry no longer calls a Player progress directly,
        //   so Player progress runs exactly once per frame (matches binary).
        player().progressFramesLike_0x6D2A54(originalDt);          // @0x67d408

        // Physics-only pass. P0-B3 FIX: the binary gates on the ORIGINAL dt
        // (v12) and the syncWaiting byte @+1159:
        //     if (*(double*)&v12 != 0.0 && !*(_BYTE*)(this+1159))   /*0x67d414*/
        // and feeds v12 (cast to float @0x67d420) into every step call. The
        // previous local code used the post-loop `dt`, which the dt-slice loop
        // has already drained to <= 0 — so this pass almost never ran. Use
        // `originalDt`.
        if (originalDt != 0.0f && !_syncWaiting) {
            // The binary casts the double v12 to float once (0x67d420) and
            // reuses that float for all six calls.
            const float physDt = originalDt;

            // Step the 3 physics-target controllers (no output sink in the
            // binary — &v71 is a scratch buffer whose result is unused here;
            // the controllers' purpose is to advance their internal state and
            // feed the spring targets read by stepHairParts/stepBust).
            float scratch[8] = {};
            EmoteVarController_step(_ctlHairPartsTarget, scratch, physDt); // *(this+1104) @0x67d42c
            EmoteVarController_step(_ctlBust1Target,     scratch, physDt); // *(this+1112) @0x67d43c
            EmoteVarController_step(_ctlBust2Target,     scratch, physDt); // *(this+1120) @0x67d44c

            // Physics step pass — now ported (sub_67B970 anchor resolver +
            // EmotePhysics_springStep + EmoteBustChainSpring_step):
            //   stepHairParts(this, physDt);                               @0x67d458
            //   stepBust(this, _ctlBust1Target, &_bustChain1Nodes,
            //            _bustSpring1Const@+1184, physDt);                 @0x67d470
            //   stepBust(this, _ctlBust2Target, &_bustChain2Nodes,
            //            _bustSpring2Const@+1192, physDt);                 @0x67d488
            // The deques #1/#2/#3 are now POPULATED by buildBustControl /
            // buildChainControl (applyMetadata @0x67D4D0 dispatch) so this pass
            // runs on real spring nodes. Outputs are still inert on the logo
            // fixture (no bustControl/hairControl/partsControl metadata -> empty
            // deques) and driven by zero targets until the controller-target /
            // spring-const wiring lands; structure matches the binary exactly.
            stepHairParts(physDt);                                      // @0x67d458
            stepBust(_ctlBust1Target, _bustChain1Nodes,
                     _bustSpring1Const, physDt);                        // @0x67d470
            stepBust(_ctlBust2Target, _bustChain2Nodes,
                     _bustSpring2Const, physDt);                        // @0x67d488
        }
    }

} // namespace motion
