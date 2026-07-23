// PlayerTimeline.cpp — timeline queries and playback raw callbacks
// Split out for maintainability.
//
#include "PlayerInternal.h"
#include "MotionDispatch.h"
#include "ncbind.hpp"

using namespace motion::internal;

namespace motion {
    namespace {
        bool shouldEmitPlaybackDiag(std::uint32_t seq) {
            return seq <= 200 || (seq % 100) == 0;
        }

        const char *boolText(bool v) {
            return v ? "true" : "false";
        }

    }

    // Player_skipToSync @0x6D3504. Despite the member name, the binary does
    // not mutate an auxiliary timeline map. It preserves the raw tag-dispatch
    // traversal (including its otherwise-dead property reads), then seeds the
    // ordinary Player frame cursor and the two +480/+481 one-shot flags.
    void Player::skipToSync() {
        if(_allplaying && _loopTime < 0.0) {
            const tTJSVariant tagFrames = _tagFrameSourceVariant;
            const tjs_int count = detail::motionPropGetCount(tagFrames);
            for(tjs_int index = 0; index < count; ++index) {
                const tTJSVariant frame =
                    detail::motionPropGetByNum(tagFrames, index);
                if(detail::motionPropGetInt(frame, TJS_W("type")) == 1) {
                    (void)detail::motionPropGetDouble(
                        frame, TJS_W("time"));
                    const tTJSVariant content =
                        detail::motionPropGet(frame, TJS_W("content"));
                    (void)detail::motionPropGetBool(
                        content, TJS_W("sync"));
                }
            }

            const double cursor = std::fmax(_cachedTotalFrames, 0.0);
            const double clamped =
                std::fmin(_cachedTotalFrames, cursor);
            _queuing = true;
            _firstFrame = true;
            _frameTickCount = cursor;
            _clampedEvalTime = clamped;
        }
    }

    bool Player::playMotionLike_0x6B2284(ttstr label, tjs_int flags) {
        // Player_play @0x6B21E8: a stealth request cannot enter playImpl until
        // the live +968 stealth-chara slot exists. Retain it in the independent
        // +768 owner, exactly as Player_setMotion_stealth @0x6D9584 does.
        if((flags & PlayFlagStealth) != 0 && _stealthChara.IsEmpty()) {
            _pendingStealthMotion = std::move(label);
            return false;
        }

        bool started = playMotionImplLike_0x6B2284(std::move(label), flags);

        // Both Player_play and the two motion property setters flush +768 by
        // invoking playImpl with PlayFlagStealth, then release/null +768.
        if(!_pendingStealthMotion.IsEmpty()) {
            const ttstr pending = _pendingStealthMotion;
            _pendingStealthMotion.Clear();
            started = playMotionImplLike_0x6B2284(
                          pending, PlayFlagStealth) ||
                      started;
        }
        return started;
    }

    bool Player::playMotionImplLike_0x6B2284(ttstr label, tjs_int flags) {
        static std::uint32_t s_diagSeq = 0;
        const auto diagSeq = ++s_diagSeq;
        const bool emitDiag = shouldEmitPlaybackDiag(diagSeq);
        if(emitDiag && LOGGER) {
            LOGGER->info(
                "PRTDIAG Player::playMotionLike enter seq={} this={} label='{}' flags=0x{:x} chara='{}' motionKey='{}' stealth='{}' active={} activePath='{}' allplaying={}",
                diagSeq, static_cast<const void *>(this),
                detail::narrow(label), static_cast<unsigned int>(flags),
                detail::narrow(_chara), detail::narrow(_motionKey),
                detail::narrow(_stealthMotion), hasMotionContent(),
                matchedMotionPath().empty()
                    ? std::string("<none>") : matchedMotionPath(),
                boolText(_allplaying));
        }

        // 0x6B22B8..0x6B22D4: select +984 for stealth and +976 otherwise;
        // Force/AsCan enter unconditionally, all other calls enter only when
        // the requested UTF-16 string differs from the selected live slot.
        const ttstr &selectedMotion =
            (flags & PlayFlagStealth) != 0 ? _stealthMotion : _motionKey;
        if((flags & (PlayFlagForce | PlayFlagAsCan)) == 0 &&
           selectedMotion == label) {
            return true;
        }

        // 0x6B22D4: AsCan suppresses a reload only while Player+1099 is set.
        // `_allplaying` is the proven +1099 owner (Player_getPlaying
        // @0x6D9794); the compatibility snapshot is not part of this gate.
        if((flags & PlayFlagForce) == 0 &&
           (flags & PlayFlagAsCan) != 0 && _allplaying) {
            return true;
        }

        // Player_playImpl @0x6B22E4: when (flags & 8 == PlayFlagJoin), rebuild
        // the var-track HM4 snapshot from the CURRENT motion before loading the
        // new one. Inert for content without a "variable" list (empty deque).
        if((flags & PlayFlagJoin) != 0) {
            resetMotionStateLike_0x6B2D3C();
        }

        // Player_loadMotion @0x6B2330 always receives +968 stealthChara and
        // the current request, regardless of whether this is the primary or
        // stealth motion slot. The explicit overload always traverses that raw
        // lookup; it has no port-local loaded-state guard.
        const auto motionPathBeforeLoad = matchedMotionPath();
        const bool ensureOk = ensureMotionLoaded(_stealthChara, label);
        const auto motionPathAfterLoad = matchedMotionPath();
        if(emitDiag && LOGGER) {
            LOGGER->info(
                "PRTDIAG Player::playMotionLike after-ensure seq={} this={} ok={} activeChanged={} activePath='{}' motionKey='{}'",
                diagSeq, static_cast<const void *>(this), boolText(ensureOk),
                boolText(motionPathBeforeLoad != motionPathAfterLoad),
                motionPathAfterLoad.empty()
                    ? std::string("<none>") : motionPathAfterLoad,
                detail::narrow(_motionKey));
        }
        if(!ensureOk) {
            // 0x6B27E8..0x6B27F4: failed load releases +528/+1012 and clears
            // +1099. The two live motion labels are written only on success.
            _motionContentVariant.Clear();
            _findMotionContextVariant.Clear();
            _allplaying = false;
            return false;
        }

        // 0x6B2354..0x6B23AC: every successful load stores the request in
        // +984; a non-stealth load additionally owns the same ttstr in +976.
        _stealthMotion = label;
        if((flags & PlayFlagStealth) == 0) {
            _motionKey = label;
        }
        if(emitDiag && LOGGER) {
            LOGGER->info(
                "PRTDIAG Player::playMotionLike call-init seq={} this={} label='{}' flags=0x{:x} motionKey='{}' active={} sameLabelAfterCommit={}",
                diagSeq, static_cast<const void *>(this),
                detail::narrow(label), static_cast<unsigned int>(flags),
                detail::narrow(_motionKey), hasMotionContent(),
                boolText(_motionKey == label));
        }
        // 0x6B2560..0x6B26F0: the raw +528 motion dispatch owns the type
        // branch. Type 1 enters the emote wrapper/secondary-load path, type 0
        // enters the ordinary initializer, and every other non-zero value
        // performs neither initialization.
        const tjs_int motionType = detail::motionPropGetInt(
            _motionContentVariant, TJS_W("type"));
        if(motionType == 1) {
            if(!_directEdit) {
                _emoteAngle = _nodes.front().delta.angle;
                _nodes.front().delta.angle = 0.0;
            }
            _directEdit = true;
            _emoteDivisionVariant = detail::motionPropGet(
                _motionContentVariant, TJS_W("division"));
            _emoteMotionListVariant = detail::motionPropGet(
                _motionContentVariant, TJS_W("motionList"));
            _emoteMotionIndex = -1;
            initEmoteMotionLike_0x6B2E90(
                static_cast<std::uint32_t>(flags));
        } else if(motionType == 0) {
            if(_directEdit) {
                _nodes.front().delta.angle = _emoteAngle;
                _emoteAngle = 0.0;
            }
            _directEdit = false;
            initNonEmoteMotionLike_0x6B365C(
                static_cast<std::uint32_t>(flags));
        } else {
            return true;
        }
        if(emitDiag && LOGGER) {
            LOGGER->info(
                "PRTDIAG Player::playMotionLike exit seq={} this={} activePath='{}' motionKey='{}' allplaying={}",
                diagSeq, static_cast<const void *>(this),
                matchedMotionPath().empty()
                    ? std::string("<none>") : matchedMotionPath(),
                detail::narrow(_motionKey), boolText(_allplaying));
        }
        return true;
    }

    tjs_error Player::playCompat(tTJSVariant *result, tjs_int numparams,
                                 tTJSVariant **param, iTJSDispatch2 *objthis) {
        if(result) {
            result->Clear();
        }

        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }

        // Player_playCompat @0x6D2C08 rejects fewer than two arguments, then
        // converts param[1] to integer flags and forwards param[0] unchanged to
        // Player_play @0x6B21E8. It has no independent motion/timeline state.
        if(numparams < 2 || !param || !param[0] || !param[1]) {
            return TJS_E_BADPARAMCOUNT;
        }
        const ttstr label = *param[0];
        const tjs_int flags = param[1]->AsInteger();
        (void)self->playMotionLike_0x6B2284(label, flags);
        return TJS_S_OK;
    }

    tjs_error Player::stopCompat(tTJSVariant *result, tjs_int numparams,
                                 tTJSVariant **param, iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }

        // Aligned to libkrkr2.so Player_stop (0x6D9A30):
        // Binary simply clears the Player-level playing flag (player+1099).
        self->_allplaying = false;

        if(result) {
            *result = tTJSVariant(true);
        }
        return TJS_S_OK;
    }

    // clear #72 — raw-callback entry, aligned with libkrkr2.so
    // Player_drawToLayerCompat @0x6D2D80. Despite the member name "clear", the
    // binary callback is a gated draw-to-layer routine: param[0] is the target
    // Layer object, param[1] is the fill value (a3 in the binary, switched on
    // its variant type at *(a3+16)). The whole body is gated on the player+544
    // dword, which is the type tag of the motion tTJSVariant at player+528, not
    // Player+1144 `completionType`. If no motion is loaded, the binary returns
    // immediately doing nothing. (Corrected after the 0x6D9624/0x6D962C NCB
    // literal binding disproved the former clearEnabled label.)
    tjs_error Player::clearCompat(tTJSVariant *result, tjs_int numparams,
                                  tTJSVariant **param, iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        // Binary: `if (*(player+544))` — gate directly on player+528 motion
        // variant's non-void type tag.
        if(self->hasMotionContent() && numparams >= 1 && param[0]) {
            tTJSVariant fill =
                (numparams >= 2 && param[1]) ? *param[1] : tTJSVariant();
            self->drawToLayerCompat(*param[0], fill);
        }
        if(result) {
            *result = tTJSVariant();
        }
        return TJS_S_OK;
    }

    // Instance worker for clear — aligned with libkrkr2.so
    // Player_drawToLayerCompat @0x6D2D80 (recursive body).
    //
    // Binary structure (post-gate):
    //   1. FAST PATH: PropGetByNum(targetLayer, flags=2, num=dword_1AB8820) — a
    //      runtime-resolved member index that, when present with a non-null
    //      +8 sub-object, routes the fill through sub_6ADCAC(subObj, fillInt).
    //      dword_1AB8820 is a member-index cache (0xffffffff placeholder in the
    //      static .so), so the exact UTF-16 member name cannot be resolved
    //      statically. DOCUMENTED GAP: this fast path is not ported; the port
    //      always takes the general fillRect path below (observably equivalent
    //      for a plain Layer target, which is the common case).
    //   2. GENERAL PATH: builds a "Layer" class wrapper, lazily recomputes the
    //      tTVPComplexRect bound via sub_7E3ECC(player+864), reads it as
    //      {left=+884, top=+888, w=+892-+884, h=+896-+888}, then calls
    //      fillRect(left, top, w, h, fillValue) on the target layer instance.
    //   3. RECURSION: iterates _nodes[1..]; for each node with nodeType==3,
    //      resolves the child Player and recurses drawToLayerCompat with the
    //      same target layer + fill value.
    void Player::drawToLayerCompat(const tTJSVariant &targetLayer,
                                   const tTJSVariant &fillValue) {
        iTJSDispatch2 *layer = targetLayer.Type() == tvtObject
                                   ? targetLayer.AsObjectNoAddRef()
                                   : nullptr;
        if(layer) {
            // General path: FuncCall(L"fillRect", left, top, w, h, fillValue).
            const tTVPRect &bound = _drawRegion.GetBound();
            tTJSVariant left(bound.left);
            tTJSVariant top(bound.top);
            tTJSVariant w(bound.get_width());
            tTJSVariant h(bound.get_height());
            tTJSVariant fill(fillValue);
            tTJSVariant *args[] = {&left, &top, &w, &h, &fill};
            tjs_uint32 hint = 0;
            layer->FuncCall(0, TJS_W("fillRect"), &hint, nullptr, 5, args,
                            layer);
        }
        // Recurse over nodeType==3 children (binary loop i=1.. over _nodes).
        for(size_t ni = 1; ni < _nodes.size(); ++ni) {
            auto &node = _nodes[ni];
            if(node.nodeType == 3) {
                if(auto *child = node.getChildPlayer()) {
                    child->drawToLayerCompat(targetLayer, fillValue);
                }
            }
        }
    }

} // namespace motion
