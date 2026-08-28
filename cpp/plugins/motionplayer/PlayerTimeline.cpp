// PlayerTimeline.cpp — timeline queries and playback raw callbacks
// Split out for maintainability.
//
#include "PlayerInternal.h"
#include "D3DAdaptor.h"
#include "MotionDispatch.h"
#include "SeparateLayerAdaptor.h"
#include "LayerIntf.h"
#include "impl/LayerImpl.h"
#include "ncbind.hpp"

using namespace motion::internal;

namespace motion {
    namespace {
        struct TimelineDispatchReleaseGuard_guess {
            iTJSDispatch2 *dispatch = nullptr;

            ~TimelineDispatchReleaseGuard_guess() {
                if(dispatch) dispatch->Release();
            }
        };

    }

    // Despite the member name, this does not mutate an auxiliary timeline map.
    // It takes an independent owner of the persistent tag stream, preserves the
    // otherwise-dead property reads, then seeds the ordinary frame cursor and
    // its two one-shot flags.
    void Player::skipToSync() {
        if(_allplaying && _loopTime < 0.0) {
            const tTJSVariant tagFrames = _tagFrameSourceVariant;
            const tjs_int count = detail::motionPropGetCount(tagFrames);
            const double initialLastTime = _cachedTotalFrames;
            double upperLimit = initialLastTime;
            for(tjs_int index = 0; index < count; ++index) {
                const tTJSVariant frame =
                    detail::motionPropGetByNum(tagFrames, index);
                if(detail::motionPropGetInt(
                       frame, TJS_W("type"), 0,
                       &detail::typeMemberHint_guess) == 1) {
                    (void)detail::motionPropGetDouble(
                        frame, TJS_W("time"), 0,
                        &detail::timeMemberHint_guess);
                    const tTJSVariant content =
                        detail::motionPropGet(
                            frame, TJS_W("content"), 0,
                            &detail::contentMemberHint_guess);
                    (void)detail::motionPropGetBool(
                        content, TJS_W("sync"));
                }
            }
            if(count >= 1) {
                // The tag dispatch can re-enter Player while it is traversed.
                // Native therefore observes lastTime again after a non-empty
                // traversal instead of reusing the pre-loop snapshot.
                upperLimit = _cachedTotalFrames;
            }

            double cursor = initialLastTime;
            if(cursor < 0.0) {
                cursor = 0.0;
            }
            double clamped = cursor;
            if(cursor > upperLimit) {
                clamped = upperLimit;
            }
            _queuing = true;
            _firstFrame = true;
            _frameTickCount = cursor;
            _clampedEvalTime = clamped;
        }
    }

    void Player::playMotion_guess(tjs_int flags, const ttstr &label) {
        // A stealth request cannot enter playImpl until the live stealth-chara
        // slot exists. Retain it in the independent pending-motion owner.
        if((flags & PlayFlagStealth) != 0 && _stealthChara.IsEmpty()) {
            _pendingStealthMotion = label;
            return;
        }

        playMotionImpl_guess(label, flags);

        // Player and both motion setters flush the persistent pending owner by
        // invoking playImpl with PlayFlagStealth, then release/null it. The
        // field remains populated throughout the nested call.
        if(!_pendingStealthMotion.IsEmpty()) {
            playMotionImpl_guess(
                _pendingStealthMotion, PlayFlagStealth);
            _pendingStealthMotion.Clear();
        }
    }

    void Player::playMotionImpl_guess(const ttstr &label, tjs_int flags) {
        // Select the stealth label for Stealth and the primary label otherwise.
        // Force/AsCan enter unconditionally; other calls enter only when the
        // requested UTF-16 string differs from the selected live slot.
        const ttstr &selectedMotion =
            (flags & PlayFlagStealth) != 0 ? _stealthMotion : _motionKey;
        if((flags & (PlayFlagForce | PlayFlagAsCan)) == 0 &&
           selectedMotion == label) {
            return;
        }

        // AsCan suppresses reload only while the Player playing byte is set;
        // the compatibility snapshot is not part of this gate.
        if((flags & PlayFlagForce) == 0 &&
           (flags & PlayFlagAsCan) != 0 && _allplaying) {
            return;
        }

        // Join rebuilds the var-track snapshot from the current motion before
        // loading the new one. It is inert for an empty variable-list deque.
        if((flags & PlayFlagJoin) != 0) {
            resetMotionState_guess();
        }

        // The load helper always receives the live stealth-chara slot and the
        // current request, regardless of which motion-name slot will later be
        // committed. It has no loaded-state guard.
        const tTJSVariant loadResult =
            loadMotionResult_guess(_stealthChara, label);
        const bool loadSucceeded = loadResult.Type() != tvtVoid;
        if(!loadSucceeded) {
            // Failure logs the original request pair before clearing the two
            // persistent result owners and the playing byte. Labels are not
            // written on this branch.
            TVPAddLog(TJS_W("motion not found ") + _stealthChara +
                      TJS_W("/") + label);
            _motionContentVariant.Clear();
            _findMotionContextVariant.Clear();
            _allplaying = false;
            return;
        }

        // Native commit order is observable under exceptions: write both live
        // request labels before converting/indexing the non-Void load result.
        _stealthMotion = label;
        if((flags & PlayFlagStealth) == 0) {
            _motionKey = label;
        }

        // Keep the complete result container alive and force it through the
        // owning Object conversion before reading elements 0 and 1.
        tTJSVariant loadResultCopy(loadResult);
        TimelineDispatchReleaseGuard_guess loadResultObject{
            loadResultCopy.AsObject()};
        loadResultCopy.Clear();
        {
            tTJSVariant value;
            (void)loadResultObject.dispatch->PropGetByNum(
                0, 0, &value, loadResultObject.dispatch);
            _motionContentVariant = value;
        }
        {
            tTJSVariant value;
            (void)loadResultObject.dispatch->PropGetByNum(
                0, 1, &value, loadResultObject.dispatch);
            _findMotionContextVariant = value;
        }
        // Retain the just-committed motion Object across type/property reads
        // and the selected initializer. Re-entrant getters cannot redirect the
        // remainder of this branch by replacing the canonical field.
        tTJSVariant motionContentCopy(_motionContentVariant);
        TimelineDispatchReleaseGuard_guess motionContent{
            motionContentCopy.AsObject()};
        motionContentCopy.Clear();
        const auto readMotionProperty = [&](const tjs_char *member,
                                            tjs_uint32 *hint) {
            tTJSVariant value;
            (void)motionContent.dispatch->PropGet(
                0, member, hint, &value, motionContent.dispatch);
            return value;
        };
        const tjs_int motionType = static_cast<tjs_int>(
            readMotionProperty(TJS_W("type"),
                               &detail::typeMemberHint_guess).AsInteger());
        if(motionType == 1) {
            if(!_directEdit) {
                _emoteAngle = _nodes.front().delta.angle;
                _nodes.front().delta.angle = 0.0;
            }
            _directEdit = true;
            _emoteDivisionVariant = readMotionProperty(
                TJS_W("division"), &detail::divisionMemberHint_guess);
            _emoteMotionListVariant = readMotionProperty(
                TJS_W("motionList"), &detail::motionListMemberHint_guess);
            _emoteMotionIndex = -1;
            initEmoteMotion_guess(
                static_cast<std::uint32_t>(flags));
        } else if(motionType == 0) {
            if(_directEdit) {
                _nodes.front().delta.angle = _emoteAngle;
                _emoteAngle = 0.0;
            }
            _directEdit = false;
            initNonEmoteMotion_guess(
                static_cast<std::uint32_t>(flags));
        }
    }

    tjs_error Player::playCompat(tTJSVariant *result, tjs_int numparams,
                                 tTJSVariant **param, iTJSDispatch2 *objthis) {
        // The raw callback itself never writes the TJS result slot. The
        // legacy tTJSNativeClassMethod object clears a non-null result before
        // it invokes this callback.
        (void)result;

        auto *self =
            ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, false);
        if(!self) {
            return TJS_E_NATIVECLASSCRASH;
        }

        // The native wrapper rejects fewer than two arguments, converts
        // param[1] to flags, and forwards param[0] to Player::play.
        if(numparams < 2) {
            return TJS_E_BADPARAMCOUNT;
        }
        // The native wrapper exposes objthis to loadMotion/onFindMotion through
        // a raw Player field. It deliberately does not AddRef. All four
        // wrappers install it before either argument conversion and clear it
        // only on normal return: if conversion/play throws, unwind destroys
        // any constructed local label but leaves this raw slot unchanged.
        self->_currentDispatch = objthis;
        const tjs_int flags = param[1]->AsInteger();
        const ttstr label = *param[0];
        self->playMotion_guess(flags, label);
        self->_currentDispatch = nullptr;
        return TJS_S_OK;
    }

    void Player::stop() {
        // The complete four-reference native body is a single byte store. In
        // particular it retains motion labels, loaded content/context,
        // timeline cursors, sync flags and pending owners.
        _allplaying = false;
    }

    // Despite the script member name "clear", this is a gated draw-to-target
    // routine. The typed NCB adapter supplies two owned Variant values. No
    // loaded motion means a successful no-op.
    void Player::drawToLayerRecursive_guess(tTJSVariant targetLayer,
                                            tTJSVariant fillValue) {
        if(!hasMotionContent()) {
            return;
        }

        // AsObjectNoAddRef deliberately preserves the native conversion error
        // for a non-object target before either adaptor check is attempted.
        iTJSDispatch2 *targetObject = targetLayer.AsObjectNoAddRef();

        if(auto *d3d = ncbInstanceAdaptor<D3DAdaptor>::GetNativeInstance(
               targetObject, false)) {
            const tjs_int color = fillValue.Type() == tvtObject
                                      ? 0
                                      : static_cast<tjs_int>(
                                            fillValue.AsInteger());
            d3d->clearTargetTexture(color);
            return;
        }

        if(auto *separate =
               ncbInstanceAdaptor<SeparateLayerAdaptor>::GetNativeInstance(
                   targetObject, false)) {
            targetLayer = separate->getTargetLayer();
            targetObject = targetLayer.AsObjectNoAddRef();
        }

        auto *nativeLayer = tTJSNI_Layer::FromVariant(targetLayer);
        if(nativeLayer && !nativeLayer->GetHasImage()) {
            return;
        }

        // The original constructs the name-based ncbind accessor even when the
        // fill argument is itself callable. Its constructor ignores PropGet
        // status, strictly converts the result to Object, retains only that
        // dispatch, and preserves ncbind's historical global-reference leak.
        ncbPropAccessor layerClass(TJS_W("Layer"));
        iTJSDispatch2 *layerClassObject = layerClass.GetDispatch();

        const tTVPRect &bound = _drawRegion.GetBound();
        tTJSVariant left(bound.left);
        tTJSVariant top(bound.top);
        tTJSVariant width(bound.get_width());
        tTJSVariant height(bound.get_height());
        tTJSVariant *rectArgs[] = {&left, &top, &width, &height};

        if(fillValue.Type() == tvtObject) {
            const auto fillClosure = fillValue.AsObjectClosureNoAddRef();
            fillClosure.Object->FuncCall(0, nullptr, nullptr, nullptr, 4,
                                         rectArgs, fillClosure.ObjThis);
        } else {
            tTJSVariant fill(fillValue);
            tTJSVariant *fillArgs[] = {
                &left, &top, &width, &height, &fill,
            };
            layerClassObject->FuncCall(
                0, TJS_W("fillRect"),
                &detail::fillRectMemberHint_guess, nullptr, 5, fillArgs,
                targetObject);
        }

        for(size_t ni = 1; ni < _nodes.size(); ++ni) {
            auto &node = _nodes[ni];
            if(node.nodeType == 3) {
                // Failed native lookup yields nullptr in all four products,
                // but the recursive call is still unconditional. A malformed
                // type-3 child is therefore a native null-dereference boundary,
                // not a silently skipped node.
                auto *child = node.getChildPlayer();
                child->drawToLayerRecursive_guess(
                    tTJSVariant(targetLayer), tTJSVariant(fillValue));
            }
        }
    }

} // namespace motion
