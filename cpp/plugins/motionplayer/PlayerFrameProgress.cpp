// PlayerFrameProgress.cpp — frameProgress timeline/control stepping
// Split from PlayerRender.cpp for maintainability.
//
#include <cmath> // std::floor — var-track interp interval quantize

#include "PlayerInternal.h"
#include "EmotePlayer.h" // for EmoteEngine (back-pointer deref)
#include "MotionDispatch.h"
#include "MotionTraceWeb.h"
#include "ncbind.hpp"
#include "tjsDebug.h"

using namespace motion::internal;

namespace {
    std::string shortTJSStackTrace(tjs_int limit = 8) {
        ttstr stack = TJSGetStackTraceString(limit, TJS_W(" <- "));
        return stack.AsStdString();
    }

} // anonymous namespace

namespace motion {
namespace {
    // Exact source-level helpers recovered at the two out-of-line Android
    // functions called by all three variable-track cursor paths.
    void stepVariableTrackSlotLike_0x6B786C(
        detail::VarTrackSlot &slot, const tTJSVariant &frameSource,
        std::uint32_t index) {
        slot.frameIndex = index;
        const tTJSVariant frame = detail::motionPropGetByNum(
            frameSource, static_cast<tjs_int>(index));
        slot.time = detail::motionPropGetDouble(frame, TJS_W("time"));
        slot.merged = false;
    }

    void mergeVariableTrackSlotLike_0x6B7A70(
        detail::VarTrackSlot &slot, const tTJSVariant &frameSource) {
        slot.merged = true;
        const tTJSVariant frame = detail::motionPropGetByNum(
            frameSource, static_cast<tjs_int>(slot.frameIndex));
        const tjs_int type = detail::motionPropGetInt(frame, TJS_W("type"));
        if(type == 0) {
            slot.typeZeroFlag = true;
            return;
        }
        slot.typeZeroFlag = false;
        if(type == 2) {
            slot.interpFlag = 0;
        } else if(type == 3) {
            slot.interpFlag = 1;
        }
        const tTJSVariant content = detail::motionPropGet(
            frame, TJS_W("content"));
        slot.interval = static_cast<std::uint32_t>(
            detail::motionPropGetInt(content, TJS_W("interval")));
        slot.value = detail::motionPropGetDouble(content, TJS_W("value"));
        // 0x6B7C90..0x6B7CD4 dispatches on the frame holder (v19), not
        // content (v15), then CopyRef's the resulting Variant into slot+32.
        slot.easing = detail::motionPropGet(frame, TJS_W("easing"));
    }

    int rawFrameCount(const tTJSVariant &frames) {
        return detail::motionPropGetCount(frames);
    }

    tTJSVariant rawFrameAt(const tTJSVariant &frames, int index) {
        return detail::motionPropGetByNum(frames, index);
    }

    double rawFrameTime(const tTJSVariant &frame) {
        return detail::motionPropGetDouble(frame, TJS_W("time"));
    }

    int rawFrameType(const tTJSVariant &frame) {
        return detail::motionPropGetInt(frame, TJS_W("type"));
    }

    tTJSVariant rawFrameContent(const tTJSVariant &frame) {
        return detail::motionPropGet(frame, TJS_W("content"));
    }
} // anonymous namespace


    void Player::advanceLayerEventStreamLike_0x6B6ADC(double targetTime) {
        const auto &frames = _tagFrameSourceVariant;
        const int count = rawFrameCount(frames);
        if(count >= 1) {
            while(_layerFrameCursor < count - 2) {
                if(targetTime < _layerNextTime) break;
                ++_layerFrameCursor;
                const auto frame = rawFrameAt(frames, _layerFrameCursor);
                _layerCurTime = rawFrameTime(frame);
                _layerNextTime = rawFrameTime(
                    rawFrameAt(frames, _layerFrameCursor + 1));
                if(rawFrameType(frame) != 1) continue;

                const auto content = rawFrameContent(frame);
                if(_syncActive) {
                    if(detail::motionPropGetBool(content, TJS_W("align"))) {
                        _motionCompleted = true;
                        _clampedEvalTime = _layerCurTime;
                        _frameTickCount = _layerCurTime;
                    }
                    if(_syncActive && detail::motionPropGetBool(
                           content, TJS_W("sync"))) {
                        _syncWaiting = true;
                        _clampedEvalTime = _layerCurTime;
                        _frameTickCount = _layerCurTime;
                        _pendingEvents.push_back({1, {}, {}});
                    }
                }
                const auto action = detail::motionPropGetString(
                    content, TJS_W("action"));
                if(!action.IsEmpty()) {
                    detail::MotionEvent event;
                    event.type = 0;
                    event.param2 = tTJSVariant(action);
                    _pendingEvents.push_back(event);
                }
            }
        }
    }

    void Player::rewindLayerEventStreamLike_0x6B9A3C(double targetTime) {
        const auto &frames = _tagFrameSourceVariant;
        const int count = rawFrameCount(frames);
        if(count != 0 && _layerCurTime > targetTime) {
            do {
                --_layerFrameCursor;
                const auto frame = rawFrameAt(frames, _layerFrameCursor);
                _layerCurTime = rawFrameTime(frame);
                _layerNextTime = rawFrameTime(
                    rawFrameAt(frames, _layerFrameCursor + 1));
                if(rawFrameType(frame) == 1) {
                    const auto content = rawFrameContent(frame);
                    if(_syncActive) {
                        if(detail::motionPropGetBool(content, TJS_W("align"))) {
                            _motionCompleted = true;
                            _clampedEvalTime = _layerCurTime;
                            _frameTickCount = _layerCurTime;
                        }
                        if(_syncActive && detail::motionPropGetBool(
                               content, TJS_W("sync"))) {
                            _syncWaiting = true;
                            _clampedEvalTime = _layerCurTime;
                            _frameTickCount = _layerCurTime;
                            _pendingEvents.push_back({1, {}, {}});
                        }
                    }
                    const auto action = detail::motionPropGetString(
                        content, TJS_W("action"));
                    if(!action.IsEmpty()) {
                        detail::MotionEvent event;
                        event.type = 0;
                        event.param2 = tTJSVariant(action);
                        _pendingEvents.push_back(event);
                    }
                }
            } while(_layerCurTime > targetTime);
        }
    }

    void Player::advanceRootContentStreamLike_0x6B6ADC(double targetTime) {
        const auto &frames = _priorityFrameSourceVariant;
        const int count = rawFrameCount(frames);
        while(_rootFrameCursor < count - 2) {
            if(targetTime < _rootNextTime) break;
            ++_rootFrameCursor;
            _rootContentVariant = rawFrameContent(
                rawFrameAt(frames, _rootFrameCursor));
            _rootCurTime = _rootNextTime;
            _rootNextTime = rawFrameTime(
                rawFrameAt(frames, _rootFrameCursor + 1));
        }
    }

    void Player::rewindRootContentStreamLike_0x6B9A3C(double targetTime) {
        const auto &frames = _priorityFrameSourceVariant;
        if(_rootCurTime > targetTime) {
            do {
                --_rootFrameCursor;
                const auto frame = rawFrameAt(frames, _rootFrameCursor);
                _rootContentVariant = rawFrameContent(frame);
                _rootNextTime = _rootCurTime;
                _rootCurTime = rawFrameTime(frame);
            } while(_rootCurTime > targetTime);
        }
    }

    void Player::advanceVariableTracksLike_0x6B6ADC(double clampedEvalTime) {
        // libkrkr2.so Player_advanceRootAndNodes (0x6B6ADC) var-track loop
        // (0x6B7124..0x6B71C8) — stream ③. For each VariableLabelScope
        // (Player+1296 deque), advance its two 56B slots so they bracket
        // clampedEvalTime (+456) via step@0x6B786C + merge@0x6B7A70.
        // frameSource remains the owned tTJSVariant copied by
        // Player_initVariables; every read goes through the Android dispatch
        // helpers rather than the decoded compatibility tree.

        for (auto &item : _variableLabelScopes) {
            const int count = detail::motionPropGetCount(item.frameSource);

            // active=slot[cursor], other=slot[!cursor] (0x6B7264).
            int cursor = item.activeSlotCursor & 1;
            detail::VarTrackSlot *active = &item.slot[cursor];
            detail::VarTrackSlot *other = &item.slot[cursor ^ 1];
            const int limit = count - 2;
            // step loop (0x6B7274): until active.frameIndex>=count-2 OR
            //   clampedEvalTime < other.time.
            while (static_cast<int>(active->frameIndex) < limit
                   && clampedEvalTime >= other->time) {
                const std::uint32_t nextIdx = other->frameIndex + 1;
                item.activeSlotCursor =
                    (item.activeSlotCursor & 1) == 0;   // toggle (0x6B7150)
                stepVariableTrackSlotLike_0x6B786C(
                    *active, item.frameSource, nextIdx);
                detail::VarTrackSlot *tmp = active;     // swap roles (0x6B7160)
                active = other;
                other = tmp;
            }
            // merge (0x6B7178, disasm-confirmed): merge slot[0] if !slot0.merged,
            // then slot[0] AGAIN if !slot1.merged — both BL 0x6B7A70(item+48,..).
            if (!item.slot[0].merged) {
                mergeVariableTrackSlotLike_0x6B7A70(
                    item.slot[0], item.frameSource);
            }
            if (!item.slot[1].merged) {
                mergeVariableTrackSlotLike_0x6B7A70(
                    item.slot[0], item.frameSource);
            }
        }
    }

    void Player::rewindVariableTracksLike_0x6B9A3C(double clampedEvalTime) {
        // Aligned with libkrkr2.so Player_rewindRootAndNodes var-track loop at
        // 0x6B9FCC (the for(i=0;...) over the Player+1296 deque, body
        // 0x6B9FEC..0x6BA038). Backward-play counterpart of
        // advanceVariableTracksLike_0x6B6ADC (0x6B7124). For each
        // VariableLabelScope walk its two 56B slots backward so they re-bracket
        // clampedEvalTime (+456), then merge slot[0] and slot[1]. step =
        // step@0x6B786C, merge@0x6B7A70; both are the same out-of-line
        // helpers as the forward path.

        for (auto &item : _variableLabelScopes) {
            // active=slot[cursor]=v14, other=slot[!cursor]=v13 (0x6BA0E8/0x6BA100).
            int cursor = item.activeSlotCursor & 1;
            detail::VarTrackSlot *active = &item.slot[cursor];
            detail::VarTrackSlot *other = &item.slot[cursor ^ 1];
            // backward step loop (0x6BA10C): until active.time <= target. Step
            //   the OTHER slot to (active.frameIndex - 1) using UNSIGNED arith
            //   (binary `(unsigned int)(*v14 - 1)`), toggle cursor, swap roles.
            while (active->time > clampedEvalTime) {
                const std::uint32_t prevIdx = active->frameIndex - 1u; // 0x6BA000
                item.activeSlotCursor =
                    (item.activeSlotCursor & 1) == 0;   // toggle (0x6B9FEC)
                stepVariableTrackSlotLike_0x6B786C(
                    *other, item.frameSource, prevIdx); // 0x6B786C(v13,...,*v14-1)
                detail::VarTrackSlot *tmp = active;     // swap (0x6BA004/0x6BA008)
                active = other;
                other = tmp;
            }
            // merge (0x6BA010/0x6BA024): merge slot[0] if !slot0.merged, then
            //   slot[1] if !slot1.merged — 0x6B7A70(item+48,..) then
            //   0x6B7A70(item+104,..). NOTE slot[1] target differs from the
            //   forward stepper (which merges slot[0] both times).
            if (!item.slot[0].merged) {
                mergeVariableTrackSlotLike_0x6B7A70(
                    item.slot[0], item.frameSource);
            }
            if (!item.slot[1].merged) {
                mergeVariableTrackSlotLike_0x6B7A70(
                    item.slot[1], item.frameSource);
            }
        }
    }

    void Player::reseedVariableTracksLike_0x6B86C8(double clampedEvalTime) {
        // Aligned with libkrkr2.so Player_reseekTimelineCursors var-track block
        // at 0x6B8F30 (inside the while(1) at 0x6B8F8C over the Player+1296
        // deque, reseed at 0x6B8F30..0x6B8F60). Non-incremental re-seed: per
        // track, scan its keyframe list to k (frames whose time<=target advance
        // k; first time>target stops, --k), clamp v41=min(k,count-2), then
        // step+merge slot[0] to v41 and slot[1] to v41+1, and reset
        // activeSlotCursor (item+8) to 0. Both slots are seeded fresh — there is
        // no active/other toggle here (that is the advance/rewind incremental
        // form). The same tTJSVariant source and step/merge helpers are reused.

        for (auto &item : _variableLabelScopes) {
            const int count = detail::motionPropGetCount(item.frameSource);

            // forward keyframe scan (0x6B90A0): k advances while frame[k].time
            //   compares against target. Mirror the reseek's per-element
            //   compare (v53): time==target → stop (k stays); time<target → ++k;
            //   time>target → --k, stop.
            const int v49 = count;
            int k = 0;
            if (v49 >= 1) {
                for (k = 0; k < v49; ++k) {
                    const tTJSVariant frame = detail::motionPropGetByNum(
                        item.frameSource, k);
                    const double t = detail::motionPropGetDouble(
                        frame, TJS_W("time"));
                    if (t == clampedEvalTime) {
                        break;                  // v53=0 (0x6B9150) → stop
                    } else if (t <= clampedEvalTime) {
                        continue;               // v53=1 (0x6B916C) → keep scanning
                    } else {
                        --k;                    // v53=0, --k (0x6B9164) → stop
                        break;
                    }
                }
            }
            // v41 = min(k, count-2) (0x6B8F1C).
            const int v41 = (k >= v49 - 2) ? (v49 - 2) : k;
            // reseed both slots fresh (0x6B8F30..0x6B8F60).
            stepVariableTrackSlotLike_0x6B786C(
                item.slot[0], item.frameSource,
                static_cast<std::uint32_t>(v41));                     // 0x6B8F30
            mergeVariableTrackSlotLike_0x6B7A70(
                item.slot[0], item.frameSource);                      // 0x6B8F3C
            stepVariableTrackSlotLike_0x6B786C(
                item.slot[1], item.frameSource,
                static_cast<std::uint32_t>(v41 + 1));                 // 0x6B8F50
            mergeVariableTrackSlotLike_0x6B7A70(
                item.slot[1], item.frameSource);                      // 0x6B8F5C
            item.activeSlotCursor = 0;                                 // 0x6B8F60
        }
    }

    // Aligned with libkrkr2.so Player_reseekTimelineCursors @0x6B86C8.
    // The binary's NON-incremental full re-seek of all timeline cursors to
    // targetTime (= player+456 _clampedEvalTime). progress_inner (0x6C106C)
    // calls this at the firstFrame seed (0x6C10E0/0x6C131C) and at the two
    // loop-wrap reseek points (forward 0x6C1488, reverse 0x6C1428). Unlike the
    // incremental advance/rewindRootAndNodes seeks (which step the existing
    // cursor toward the target), this rescans each stream FROM SCRATCH.
    //
    // Structure (verified against the 0x6B86C8 decompile):
    //   1. LAYER coarse scan @0x6B8770 over motion["tag"] (player+1072):
    //      a from-scratch linear scan with a DOUBLE-INCREMENT (loop ++i AND a
    //      body ++i when time<target) -> coarse overshoot; cursor=min(i,count-2);
    //      curTime/nextTime INT-TRUNCATED; then the +1093-gated align/sync +
    //      ungated action gate keyed on the CURSOR frame (NOT cursor+1).
    //   2. ROOT scan @0x6B8C1C over motion["priority"] (player+548): single-step
    //      (no double-increment); cursor=min(j,count-2); +616 content snapshot;
    //      curTime(+576) NOT int-truncated; nextTime(+584).
    //   3. VAR-TRACK reseed @0x6B8F30 -> reseedVariableTracksLike_0x6B86C8
    //      (REUSED verbatim — the binary's var-track block is identical).
    //   4. NODE init loop @0x6B91B0 (Player_initNodeTimeline @0x6B64E4 per node)
    //      -> the caller keeps progressSeekNodeSlotsLike_0x6C106C right after
    //      this call (the live per-node seek that fills node+320/+856), so the
    //      node-init is performed there; not duplicated here.
    //   5. TAIL @0x6B9234: Player_pruneHM3_byNodeIdentity (A, ported) + the
    //      player+280 HM1-entry walk (B) calling sub_6B9650 per entry (now
    //      ported — see STEP 5 below).
    void Player::reseekTimelineCursors(double targetTime) {
        // ---- STEP 1: LAYER coarse scan @0x6B8770 (motion["tag"], player+1072) ----
        {
            const auto &frames = _tagFrameSourceVariant;
            const int count = rawFrameCount(frames);

                if (count >= 1) {                       // 0x6B8768
                    // 0x6B8770: from-scratch linear scan i:0..count-1 with the
                    // DOUBLE-INCREMENT. v8 = continue flag.
                    int i = 0;
                    for (; i < count; ++i) {
                        const auto cf = rawFrameAt(frames, i);
                        const double v6 = rawFrameTime(cf); // 0x6B8810
                        const double v7 = targetTime;       // 0x6B8814: +456
                        int v8;
                        if (v6 <= v7) {                     // 0x6B881C
                            if (v6 < v7) {                  // 0x6B882C
                                ++i;                        // 0x6B8830 (DOUBLE-INC)
                                v8 = 1;
                            } else {
                                v8 = 0;                     // 0x6B883C
                            }
                        } else {
                            v8 = 0;                         // 0x6B8820
                            --i;                            // 0x6B8824
                        }
                        if (!v8) {                          // 0x6B885C
                            break;
                        }
                    }
                    // 0x6B8874: cursor = min(i, count-2).
                    _layerFrameCursor = (count - 2 >= i) ? i : (count - 2);
                    // 0x6B891C: curTime = (double)(int)tag[cursor].time
                    //   (INT-TRUNCATED).
                    const auto curF = rawFrameAt(frames, _layerFrameCursor);
                    _layerCurTime = static_cast<double>(
                        detail::motionPropGetInt(curF, TJS_W("time")));
                    // 0x6B89D0: nextTime = (double)(int)tag[cursor+1].time
                    //   (INT-TRUNCATED).
                    const auto nextF = rawFrameAt(
                        frames, _layerFrameCursor + 1);
                    _layerNextTime = static_cast<double>(
                        detail::motionPropGetInt(nextF, TJS_W("time")));
                    // 0x6B89D4..0x6B8B84: gate keyed on the CURSOR frame, fired
                    // only when curTime(+920) == target(+456) && type==1.
                    if(_layerCurTime == targetTime && rawFrameType(curF) == 1) {
                        const auto content = rawFrameContent(curF);
                        if(_syncActive) {              // 0x6B8A8C: +1093 gate
                            // 0x6B8AC0: align gate (re-tests +920==+456).
                            if(_layerCurTime == targetTime &&
                               detail::motionPropGetBool(
                                   content, TJS_W("align"))) {
                                _motionCompleted = true;       // +483 = 1
                                _clampedEvalTime = _layerCurTime; // +456
                                _frameTickCount = _layerCurTime;  // +1120
                            }
                            // 0x6B8AFC: sync gate (re-tests +1093).
                            if(_syncActive && detail::motionPropGetBool(
                                   content, TJS_W("sync"))) {
                                _syncWaiting = true;           // +1098 = 1
                                _clampedEvalTime = _layerCurTime; // +456
                                _frameTickCount = _layerCurTime;  // +1120
                                _pendingEvents.push_back({1, {}, {}});
                            }
                        }
                        // 0x6B8B38: content["action"] (ungated) ->
                        // Player_pushActionEvent_guess -> onAction(void, name).
                        const auto action = detail::motionPropGetString(
                            content, TJS_W("action"));
                        if(!action.IsEmpty()) {
                            detail::MotionEvent ev;
                            ev.type = 0;
                            ev.param2 = tTJSVariant(action);
                            _pendingEvents.push_back(ev);
                        }
                    }
            }
        }

        // ---- STEP 2: ROOT scan @0x6B8C1C (motion["priority"], player+548) ----
        {
            const auto &frames = _priorityFrameSourceVariant;
            const int count = rawFrameCount(frames);

                int j = 0;
                if (count) {                                // 0x6B8C20
                    if (count >= 1) {                       // 0x6B8C28
                        // 0x6B8C30: single-step scan (NO double-increment).
                        for (j = 0; j < count; ++j) {
                            const auto cf = rawFrameAt(frames, j);
                            const double v23 = rawFrameTime(cf); // 0x6B8CD0
                            const double v24 = targetTime;       // 0x6B8CD4: +456
                            int v25;
                            if (v24 == v23) {                    // 0x6B8CDC
                                v25 = 0;
                            } else if (v23 <= v24) {             // 0x6B8CEC
                                v25 = 1;
                            } else {
                                v25 = 0;                         // 0x6B8CF0
                                --j;
                            }
                            if (!v25) {                          // 0x6B8D1C
                                break;
                            }
                        }
                        _rootFrameCursor = j;                    // 0x6B8D44
                    }
                } else {
                    j = _rootFrameCursor;                        // 0x6B8D30
                }
                // 0x6B8D50: cursor = min(cursor, count-2).
                {
                    const int cur = _rootFrameCursor;
                    _rootFrameCursor =
                        (cur >= count - 2) ? (count - 2) : cur;
                }
                // 0x6B8E20: content snapshot = priority[cursor].content (+616,
                // sub_A0FB64 variant copy — the port copies the shared_ptr).
                {
                    const auto cf = rawFrameAt(frames, _rootFrameCursor);
                    _rootContentVariant = rawFrameContent(cf);
                    // 0x6B8E48: curTime(+576) = priority[cursor].time
                    //   (NOT int-truncated, unlike the layer stream).
                    _rootCurTime = rawFrameTime(cf);
                    // 0x6B8F08: nextTime(+584) = priority[cursor+1].time.
                    const auto nf = rawFrameAt(
                        frames, _rootFrameCursor + 1);
                    _rootNextTime = rawFrameTime(nf);
            }
        }

        // ---- STEP 3: VAR-TRACK reseed @0x6B8F30 ----
        // The binary's var-track block (0x6B8F8C while-loop over player+1296,
        // reseed at 0x6B8F30) is byte-identical to reseedVariableTracksLike;
        // reuse it verbatim (per brief — do not re-port).
        reseedVariableTracksLike_0x6B86C8(targetTime);

        // ---- STEP 4: NODE init loop @0x6B91B0 ----
        // Player_initNodeTimeline (0x6B64AC) per node — the ABSOLUTE two-slot
        // re-seed that repositions every node independent of its prior cursor.
        // Now ported (reseekNodeTimelineSlotsLike_0x6B91B0); the caller still runs
        // progressSeekNodeSlotsLike right after (matching the binary's
        // reseek-then-advanceRootAndNodes order), but with the node slots already
        // absolutely positioned the subsequent forward-only inline seek is a
        // ~no-op step rather than relying on a corrective-backward to undo a
        // loop-wrap time jump. (Previously DEFERRED to the caller's node walk; the
        // corrective-backward that stood in for this has been removed from the
        // forward inline seek now that this re-seed exists.)
        reseekNodeTimelineSlotsLike_0x6B91B0(targetTime);

        // ---- STEP 5: TAIL @0x6B9234 (re-confirmed against fresh decompile of
        //   Player_reseekTimelineCursors 0x6B86C8; runs UNGATED every reseek) ----
        //
        // The binary tail is two passes:
        //   (A) 0x6B9234 Player_pruneHM3_byNodeIdentity @0x6B826C
        //   (B) 0x6B923C..0x6B9248 the player+280 HM1-aux rebuild walk (sub_6B9650)
        //
        // (A) is now PORTED (pruneHM3ByNodeIdentityLike_0x6B826C) — the HM3/HM4
        // populate prerequisite it stood blocked on is in place (resetMotionState
        // loop2/loop3 @0x6B2D40/0x6B2DF8 + HM3_initValueFromNode @0x699510 were
        // landed in a5de9fd). The prior "both maps permanently empty → provable
        // no-op" framing is FALSIFIED for HM4 (it is populated by every
        // PlayFlagJoin play); corrected here. See the method body for the loop1
        // (HM4→active var-track slot.value) full port and the loop2 prune scope.
        pruneHM3ByNodeIdentityLike_0x6B826C();

        // (B) 0x6B923C..0x6B9248: `for (n = player+280; n; n = *n)
        //     sub_6B9650(a1, n+16)`. player+280 is HM1's (_evalCascadeMap,
        //     Player+264) internal before-begin all-entries chain (std::unord-
        //     ered_map's _M_before_begin._M_nxt). The walk calls sub_6B9650 with
        //     n+16 = the EvalCascadeState VALUE base for EVERY HM1 entry,
        //     rebuilding each entry's heapResult (entry+48 = vector<MotionNode*>).
        //   NOW PORTED (the prior "no port reader" DEFERRAL premise was a
        //   MISIDENTIFICATION, corrected this pass): heapResult's consumer is the
        //   loop at 0x6C4978 inside Player_bindParameterValue @0x6C4668 itself —
        //   for each listed type3/4 node it ramps the node's CHILD Player's +408
        //   map (= the child Player's _parameterRampMap, already modeled by
        //   Stage 1), keyed by the suffix. node+408 is NOT a field on MotionNode;
        //   it is Player+408 of the child reached THROUGH the node (sub_6C1678
        //   PropGet member 200 -> native child Player). That consumer is now wired
        //   in bindParameterValueLike_0x6C4668. The builder itself is
        //   rebuildEvalCascadeHeapResultLike_0x6B9650.
        //   The unordered_map's libstdc++ node-chain order differs from the port's
        //   iteration order, but each entry's heapResult is computed independently
        //   (reads _nodes + the entry's own chainSegments/weight, writes only that
        //   entry's heapResult), so iteration order does not affect the outcome.
        for(auto &kv : _evalCascadeMap) {
            rebuildEvalCascadeHeapResultLike_0x6B9650(kv.second);
        }
    }

    void Player::pruneHM3ByNodeIdentityLike_0x6B826C() {
        // libkrkr2.so Player_pruneHM3_byNodeIdentity @0x6B826C, the reseek STEP5
        // tail (called from Player_reseekTimelineCursors @0x6B9234, UNGATED). Two
        // gated loops then a terminal Player_clearHM3_HM4 @0x6B80E4.
        //
        // === loop1 (gate HM4 bucket-count != 0, binary a1[158]=Player+1264) ===
        //   for each var-track item in the Player+1312 deque (_variableLabelScopes):
        //     cursor = item.activeSlotCursor                         // item+8
        //     if (!item.slot[cursor].typeZeroFlag) {                 // !*(item+56*cursor+68)
        //       node = HM4.find(item.cascadeKey)                     // key = *item (item+0)
        //       if (node) item.slot[cursor].value = node.value       // *(item+56*cursor+72) = node[2]
        //     }
        //   item+56*cursor+72 == slot[cursor]+24 == VarTrackSlot.value; item+...+68
        //   == slot[cursor]+20 == VarTrackSlot.typeZeroFlag. This restores the
        //   active slot's value from the HM4 snapshot so the loop-wrap reseed does
        //   not lose a variable that resetMotionState cached. FULLY PORTED — every
        //   field exists; HM4 (_variableSnapshotMap) is populated by every
        //   PlayFlagJoin play (resetMotionState loop2 @0x6B2D40).
        if (!_variableSnapshotMap.empty()) {
            for (auto &item : _variableLabelScopes) {
                const int cursor = item.activeSlotCursor & 1;
                if (item.slot[cursor].typeZeroFlag) {
                    continue;          // 0x6B8378 gate: type==0 slot → no value
                }
                if (const auto it = _variableSnapshotMap.find(item.cascadeKey);
                    it != _variableSnapshotMap.end()) {
                    item.slot[cursor].value = it->second;   // 0x6B8420
                }
            }
        }

        // === loop2 (gate HM3 element-count != 0, binary a1[151]=Player+1208) ===
        // Binary (0x6B843C..0x6B869C): for j=1..nodeCount, build the node's
        // path-key, look it up in HM3 (_perNodeLayerStateMap). If found AND
        //   node+46 (joinTarget) != 0 AND HM3.value.nodeType == node.nodeType
        // (0x6b855c gate + 0x6b8574 match), restore the snapshot into the node's
        // active ClipSlot (Player_HM3_restoreValueToNode @0x6997F0, 0x6b857c),
        // then — when node.nodeType==0 && slot+344==0 — call Motion_Player_findSource
        // (0x6b85a0), then ERASE that HM3 entry (0x6b8644, --HM3.count). The
        // terminal Player_clearHM3_HM4 @0x6B80E4 wipes whatever entries remain.
        //
        // PORTED: the per-node restore (joinTarget+nodeType gate → restore common
        // scalar block) + gated findSource + matched-entry erase. node+46 = joinTarget is now
        // modeled (the prior "visible byte / DEFERRED" was falsified by the
        // 0x6b3ef0/0x6b2dcc/0x6b855c decompiles this pass). The restore itself is
        // ported for the common scalar block plus the type-3/type-4 child and
        // particle variants. Player_HM3_initValueFromNode @0x699510 also owns a
        // ttstr copy of activeSlot.src at value+44; Player_HM3_restoreValueToNode
        // @0x6997F0 deliberately does not write that owner back to the slot, so
        // its role is snapshot lifetime retention rather than restore payload.
        //
        if(!_perNodeLayerStateMap.empty()) {
            for(size_t k = 1; k < _nodes.size(); ++k) {
                detail::MotionNode &node = _nodes[k];
                const ttstr key = detail::buildNodePathKeyLike_0x6B5C1C(
                    _nodes, static_cast<int>(k));
                const auto it = _perNodeLayerStateMap.find(key);
                if(it == _perNodeLayerStateMap.end()) {
                    continue;                       // 0x6b8550 HM3 miss
                }
                const detail::PerNodeLayerState &v = it->second;
                // 0x6b855c joinTarget gate + 0x6b8574 nodeType match.
                if(!node.joinTarget || v.nodeType != node.nodeType) {
                    continue;
                }
                // 0x6b857c restore V -> active ClipSlot (partial).
                hm3RestoreValueToNodeLike_0x6997F0(node, v);
                // 0x6b8588 findSource gate (nodeType==0 && active slot.done==0).
                if(node.nodeType == 0 && !node.activeSlot().done) {
                    findSourceForNode_guess(node);
                }
                // 0x6b8644 erase the matched HM3 entry (--HM3.count).
                _perNodeLayerStateMap.erase(it);
            }
        }
        _perNodeLayerStateMap.clear();   // clearHM3_HM4 @0x6B80E4 (HM3 @+1184)
        _variableSnapshotMap.clear();    // clearHM3_HM4 @0x6B80E4 (HM4 @+1240)
    }

    void Player::interpolateVarTrackValuesLike_0x6BBE20(double clampedEvalTime) {
        // libkrkr2.so Player_interpolateVarTrackValues @0x6BBE20. Writes item+16
        // (the value HM4 caches) for each VariableLabelScope. active=slot[cursor]
        // is prev (lower frame), other=slot[!cursor] is next. Gate (0x6BBF14):
        // skip if active.typeZeroFlag (type==0 → no value). Then HOLD vs LERP.
        // Player_applyBezierEasing @0x69A754: easing = {x:[...], y:[...]} control
        // points (count multiple of 3). Clamp t to [x[0], x[n-1]] → y end; else
        // stride-3 scan locates the segment and the curve uses t DIRECTLY:
        //   B(t) = (1-t)³y0 + 3(1-t)²t·y1 + 3(1-t)t²·y2 + t³y3
        // (the x values only locate the segment; they do not re-parameterise t —
        // faithful to the binary's discard of the xs reads).
        const auto applyBezierEasing =
            [](const tTJSVariant &easing, double t) -> double {
            const tTJSVariant xs = detail::motionPropGet(
                easing, TJS_W("x"));
            const tTJSVariant ys = detail::motionPropGet(
                easing, TJS_W("y"));
            const int count = detail::motionPropGetCount(xs);
            const auto xAt = [&](int i) {
                return detail::motionPropGetDoubleByNum(xs, i);
            };
            const auto yAt = [&](int i) {
                return detail::motionPropGetDoubleByNum(ys, i);
            };
            if (xAt(0) >= t) return yAt(0);            // 0x69A938
            if (xAt(count - 1) <= t) return yAt(count - 1);  // 0x69A958
            int s = 0;
            do { s += 3; } while (s < count && xAt(s) < t);  // 0x69A960 stride-3
            const double y0 = yAt(s - 3), y1 = yAt(s - 2),
                         y2 = yAt(s - 1), y3 = yAt(s);
            const double u = 1.0 - t;                  // 0x69AA80 cubic bezier
            return u * u * u * y0 + 3.0 * u * u * t * y1
                 + 3.0 * u * t * t * y2 + t * t * t * y3;
        };

        for (auto &item : _variableLabelScopes) {
            const int cursor = item.activeSlotCursor & 1;
            detail::VarTrackSlot &active = item.slot[cursor];     // prev
            detail::VarTrackSlot &other = item.slot[cursor ^ 1];  // next
            if (active.typeZeroFlag) {
                continue;                       // 0x6BBF14 gate: type==0 → no value
            }
            double v;
            if (active.interpFlag == 0 || other.typeZeroFlag) {
                v = active.value;               // HOLD (0x6BBF40)
            } else {
                double d = clampedEvalTime - active.time;   // evalTime - prevTime
                if (active.interval != 0) {                 // 0x6BBF74 quantize
                    d = std::floor(d / active.interval) * active.interval;
                }
                const double Vp = active.value, Vo = other.value;
                if (Vo == Vp) {
                    v = Vp;                     // degenerate → hold
                } else {
                    double t = d / (other.time - active.time);  // 0x6BBFBC
                    if (active.easing.Type() != tvtVoid) { // slot+48 variant tag
                        t = applyBezierEasing(active.easing, t); // 0x6BBFC8
                    }
                    v = Vo * t + Vp * (1.0 - t); // LERP (0x6BBFD8)
                }
            }
            item.value = v;                     // item+16 (0x6BBF54)
            // 0x6BBF58: Player_bindParameterValue_writesHM1_HM2(player, item, 0,
            // v) — populate HM1 (_evalCascadeMap, scope keys) + HM2
            // (_evalResultValues) so getVariable's HM1-join branch resolves.
            bindParameterValueLike_0x6C4668(item.cascadeKey, v);
        }
    }

    void Player::resetMotionStateLike_0x6B2D3C() {
        // libkrkr2.so Player_resetMotionState_clearAndRebuild @0x6B2D3C. Called
        // from playImpl (0x6B22E4) when (flags & 8 == PlayFlagJoin), before
        // loadMotion. Body-gated on !*(BYTE)(player+480) == !_queuing.
        if (_queuing) {
            return;
        }
        // clearHM3_HM4 (0x6B80E4): HM3(+1184) + HM4(+1240).
        _perNodeLayerStateMap.clear();
        _variableSnapshotMap.clear();
        // interpolateVarTrackValues (0x6BBE20): compute item+16 per var-track.
        interpolateVarTrackValuesLike_0x6BBE20(_clampedEvalTime);
        // loop1 (0x6B2BDC: node-deque evaluateTimeline) — DEFERRED.
        // loop2 (0x6B2C64): snapshot each var-track item+16 → HM4
        // (_variableSnapshotMap), gated on the active slot's typeZeroFlag
        // (binary `!*(BYTE)(item + 56*cursor + 68)`); key = item+0 cascadeKey.
        for (const auto &item : _variableLabelScopes) {
            const int cursor = item.activeSlotCursor & 1;
            if (!item.slot[cursor].typeZeroFlag) {
                _variableSnapshotMap[item.cascadeKey] = item.value;
            }
        }
        // loop3 (0x6B2D68): HM3 per-node-path layer state. Structure and HM3
        // payload ownership are ported (see hm3InitValueFromNodeLike); the
        // preceding loop1 evaluateTimeline call remains deferred, so the values
        // presented to this snapshot can still differ in this reset path. Gate
        // order matches the binary @0x6b2dcc/0x6b2df8 EXACTLY:
        //   (1) if (!node+46) continue;            // joinTarget gate FIRST
        //   (2) v30 = node+28;
        //       if (v30 <= 8 && ((1<<v30) & 0x19D)) // nodeType mask SECOND
        // The joinTarget gate (node+46) was previously MISSING here — a real
        // divergence. node+46 = joinTarget (PSB "joinTarget" bool, writer
        // Player_initNodeFields @0x6b3ef0; the prior "visible byte / DEFERRED"
        // annotation was falsified by fresh decompile this pass). Now restored:
        // only joinTarget nodes are snapshotted into HM3. key = buildNodePathKey
        // (HM3 path-key space, distinct from raw-label Player+24);
        // HM3.upsert → _perNodeLayerStateMap. The map
        // is read on the maintenance side by pruneHM3ByNodeIdentityLike_0x6B826C
        // (reseek STEP5), whose per-node restore payload is ported.
        for(size_t k = 1; k < _nodes.size(); ++k) {
            const auto &node = _nodes[k];
            if(!node.joinTarget) {
                continue;                          // 0x6b2dcc joinTarget gate
            }
            const int t = node.nodeType;
            if(t >= 0 && t <= 8 && ((1 << t) & 0x19D) != 0) {  // 0x6b2df8 mask
                const ttstr key = detail::buildNodePathKeyLike_0x6B5C1C(
                    _nodes, static_cast<int>(k));
                hm3InitValueFromNodeLike_0x699510(
                    node, _perNodeLayerStateMap[key]);
            }
        }
    }

    void Player::hm3InitValueFromNodeLike_0x699510(
        const detail::MotionNode &node, detail::PerNodeLayerState &v) const {
        // libkrkr2.so Player_HM3_initValueFromNode @0x699510 (a1=node, a2=V).
        // The binary snapshots the node's ALREADY-INTERPOLATED state — written by
        // Player_evaluateTimeline @0x699AE4 in resetMotionState loop1, into the
        // node+1512.. byte-mirror — plus the active ClipSlot's raw fields. The
        // port holds the same node runtime block in node.accumulated and the
        // slot fields in node.activeSlot(), so we copy them semantically (the
        // ARM64 byte offsets are an ABI detail the port does not need).
        v.nodeType = node.nodeType;              // V+0   ← node+28
        const auto &c = node.accumulated;
        // The init side reads the ACTIVE slot and may CLEAR node fields (type-3),
        // so it needs a mutable node ref; the caller passes a const node, so cast
        // here matching the binary which takes a1=node and writes node+1912.
        detail::MotionNode &mnode = const_cast<detail::MotionNode &>(node);
        auto &slot = mnode.activeSlot();
        // mesh control points (meshType==1; sub_6996E8 copies V+568 <- node+2024).
        // BINARY ORDER: the mesh snapshot happens FIRST, before the type-3/4 and
        // common blocks (@0x699588), gated on node+2000==1.
        if(mnode.meshType == 1) {                 // node+2000 == 1
            v.meshControlPoints = mnode.meshControlPoints; // V+568 ← node+2024
        }
        // type-3 child-Player dispatch snapshot (@0x699598): copy node+1912 into
        // V+544 (tTJSVariant copy-assign sub_A0FB64) then clear node+1912
        // (sub_A0F790). The node's childPlayerVar ownership moves into the snapshot.
        if(mnode.nodeType == 3) {
            v.childPlayerSnapshot = mnode.childPlayerVar;  // V+544 ← node+1912
            mnode.childPlayerVar.Clear();                  // sub_A0F790(node+1912)
        }
        // type-4 particle snapshot (@0x699550, LABEL_4). The binary does this
        // BEFORE the doneFlag common-block gate:
        //   sub_A0FB64(V+672, node+2296); sub_A0F790(node+2296)  -- move Array
        //   if (slot+344 done): V+32 = done; return (skip interp block + common)
        //   else: V+600..664 <- node+2224..2288; V+32 = 0; fall into common block
        if(mnode.nodeType == 4) {
            // V+672 ← node+2296 (tTJSVariant copy-assign), then clear node.
            v.particleArraySnapshot = mnode.particleArrayVar; // V+672 ← node+2296
            mnode.particleArrayVar.Clear();                   // sub_A0F790(node+2296)
            if(slot.done) {                       // 0x699570 if(slot+344)
                v.doneFlag = 1;                   // V+32 ← slot+344
                return;                           // 0x69957c early-return
            }
            // 0x6995dc: V+600..664 (V+150 int*) <- node+2224..2288
            // (MotionNode::particleInterp, the evaluateTimeline eval-output mirror).
            for(int i = 0; i < 9; ++i) {
                v.particleInterp[i] = mnode.particleInterp[i];
            }
            v.doneFlag = 0;                       // 0x699608 *((BYTE*)a2+32)=0
            // fall through to the common block (LABEL_11) — NOT early-returned.
        } else {
            // V+32 doneFlag ← slot+344. The binary sets it (@0x6995c8 / @0x699608),
            // and the COMMON BLOCK (V+28 contentMask + V+52.. transform) is only
            // reached at LABEL_11 when doneFlag==0 (early-return @0x6995cc). type-3/4
            // snapshots above run regardless of doneFlag.
            v.doneFlag = slot.done ? 1 : 0;          // V+32  ← slot+344
            if(v.doneFlag) {
                return;                              // 0x6995cc early-return (skip common block)
            }
        }
        // active ClipSlot fields (slot = node+320+536*activeSlotIndex):
        // 0x699610..0x69964C CopyRefs slot+36 into V+44. This is a lifetime
        // owner only: Player_HM3_restoreValueToNode @0x6997F0 never writes it
        // back to the slot, and the HM3 value destructor releases it.
        v.srcValue_44 = slot.srcValue;              // V+44 ← slot+36
        v.contentMask = slot.contentMask;        // V+28  ← slot+340 (frame "mask")
        v.blendMode = slot.blendMode;            // V+52  ← slot+364
        v.ox = slot.ox;                          // V+64  ← slot+376
        v.oy = slot.oy;                          // V+72
        // interpolated transform state (evaluateTimeline outputs):
        std::memcpy(v.packedColors.data(), node.colorBytes,
                    sizeof(node.colorBytes));    // V+80  ← node+100..112
        v.opacity = c.opacity;                   // V+96  ← node+1576
        v.coordX = c.posX;                       // V+104 ← node+1512
        v.coordY = c.posY;                       // V+112 ← node+1520
        v.coordZ = c.posZ;                       // V+120 ← node+1528
        v.flipX = c.flipX ? 1 : 0;               // V+128 ← node+1507
        v.flipY = c.flipY ? 1 : 0;               // V+129 ← node+1508
        v.angle = c.angle;                       // V+136 ← node+1536
        v.scaleX = c.scaleX;                     // V+144 ← node+1544
        v.scaleY = c.scaleY;                     // V+152 ← node+1552
        v.slantX = c.slantX;                     // V+160 ← node+1560
        v.slantY = c.slantY;                     // V+168 ← node+1568
        // type-4 (V+672 particle-array snapshot ← node+2296, V+600..664 ←
        // node+2224..2288) is ported above. V+44 source ownership is also
        // complete: it is the ttstr CopyRef taken before this scalar block.
    }

    void Player::hm3RestoreValueToNodeLike_0x6997F0(
        detail::MotionNode &node, const detail::PerNodeLayerState &v) const {
        // libkrkr2.so Player_HM3_restoreValueToNode @0x6997F0 (a1=node, a2=V).
        // The reverse of HM3_initValueFromNode: writes the HM3 snapshot back into
        // the node's active ClipSlot's merged-content region. Binary slot base =
        // node + 536*activeSlotIndex (== node+320 + 536*idx == the active
        // ClipSlot); the restore offsets node+536*idx+{340,364,376,...} resolve to
        // slot-relative +{20,44,56,...} (= the merged parse fields, byte-verified
        // against ClipSlot_536B_layout.md). The next evaluateTimeline copy-branch
        // reads these SAME slot fields (slot+392..480 == slot+72.. == the merged
        // fields) into the node mirror, so restoring them is the faithful inverse.
        //
        // Binary structure (slot = node+536*idx, slot-relative offsets):
        //   if (node+2000 == 1) copyVector_meshControlPts(slot+640, V+71)  -- mesh
        //   if (V.nodeType == 3) restore node+1912 <- V+136(dispatch); reset V
        //   if (V.nodeType == 4) restore node+2296 <- V+168(dispatch); reset V;
        //                        if (!V+32) memcpy(slot+744 <- V+150, 0x48)
        //   V+44 source is intentionally not restored; it is a lifetime owner.
        //   COMMON BLOCK gate (0x6998a4): if (!slot+344 && !V+32):
        //     slot+20  = V+28  contentMask
        //     slot+44  = V+52  blendMode
        //     slot+56  = V+64  ox ; slot+64 = V+72 oy
        //     slot+72/76/80/84 = V+80/84/88/92  color[0..3]
        //     slot+88  = V+96  opacity
        //     slot+96  = V+104 coordX ; slot+104 = V+112 coordY
        //     slot+112 = V+120 coordZ
        //     slot+120 = V+128 flipX ; slot+121 = V+129 flipY
        //     slot+128 = V+136 angle ; slot+136 = V+144 scaleX
        //     slot+144 = V+152 scaleY
        //     slot+160 = V+168 slantY     (NOTE: slot+152 / slantX NOT restored
        //                                  by the binary — faithfully skipped)
        detail::MotionNode::ClipSlot &slot = node.activeSlot();

        // mesh control-point vector restore (@0x699828, FIRST, gate node+2000==1):
        // copyVector_meshControlPts(slot+640, V+71) == slot+640 <- V+568. The
        // per-slot mesh source (slot+640) is the value evaluateTimeline @0x699c08
        // copies into node+2024; restoring V's snapshot of node+2024 into slot+640
        // re-seeds that source for the next eval pass. Asymmetric vs init (init
        // reads node-base node+2024 -> V+568; restore writes slot-base slot+640).
        if(node.meshType == 1) {                       // node+2000 == 1
            slot.meshControlPoints = v.meshControlPoints; // slot+640 <- V+568
        }

        // type-3 child-Player dispatch restore (@0x699844, gate V.nodeType==3):
        // sub_A0FB64(node+1912, V+544) (tTJSVariant copy-assign) then
        // sub_A0F790(V+544) (clear the snapshot). Moves the child-Player dispatch
        // ownership back from the V+544 snapshot into node.childPlayerVar.
        if(v.nodeType == 3) {
            node.childPlayerVar = v.childPlayerSnapshot;  // node+1912 <- V+544
            // sub_A0F790(V+544): clear the snapshot. v is const here (the snapshot
            // is consumed/transferred), so cast to clear matching the binary which
            // resets V+544's type tag to 0 after the move.
            const_cast<detail::PerNodeLayerState &>(v).childPlayerSnapshot.Clear();
        }

        // type-4 particle-array dispatch restore (@0x699868, gate V.nodeType==4):
        //   sub_A0FB64(node+2296, V+672); sub_A0F790(V+672)  -- move Array back
        //   if (!V+32): memcpy(slot+744 <- V+600, 0x48)       -- restore prt block
        // The Array (node+2296 == node.particleArrayVar) ownership moves back from
        // the V+672 snapshot; the particle block is reseeded from V+600..664 (the
        // snapshot of node+2224..2288 taken by HM3 init @0x6995dc), gated on the
        // SNAPSHOT having been of a live slot (V+32==0).
        // ALIAS (self-disassembled @0x699880-0x699890): dest = node+536*idx+744
        // (X8=X20+536*idx, ADD X0,X8,#0x2E8). node+536*idx+744 == slot+424, so the
        // memcpy target IS the prt block prtFmin..prtRange (the same bytes
        // mergeFrameContent writes each frame). The next evaluateTimeline COPY
        // branch @0x699c2c then reads this same prt block into node+2224..2288.
        if(v.nodeType == 4) {
            node.particleArrayVar = v.particleArraySnapshot;  // node+2296 <- V+672
            // sub_A0F790(V+672): clear the snapshot (consumed/transferred).
            const_cast<detail::PerNodeLayerState &>(v).particleArraySnapshot.Clear();
            if(v.doneFlag == 0) {                  // 0x699874 if(!V+32)
                // memcpy(slot+744 <- V+600, 0x48): write the 9-double prt block
                // (slot+424..488 == slot+744..808) from V's particleInterp snapshot.
                slot.prtFmin  = v.particleInterp[0];   // slot+424
                slot.prtF     = v.particleInterp[1];   // slot+432
                slot.prtVmin  = v.particleInterp[2];   // slot+440
                slot.prtV     = v.particleInterp[3];   // slot+448
                slot.prtAmin  = v.particleInterp[4];   // slot+456
                slot.prtA     = v.particleInterp[5];   // slot+464
                slot.prtZmin  = v.particleInterp[6];   // slot+472
                slot.prtZ     = v.particleInterp[7];   // slot+480
                slot.prtRange = v.particleInterp[8];   // slot+488
            }
        }

        // COMMON SCALAR BLOCK (0x6998a4) — gate `!slot+344 && !V+32`:
        //   slot+344 = ClipSlot.done (active slot's done/invisible flag);
        //   V+32     = PerNodeLayerState.doneFlag (snapshotted slot.done at
        //              populate time). Restore ONLY when both are 0 (active slot
        //              has live content AND the snapshot was of a live slot).
        if(slot.done || v.doneFlag != 0) {
            return;                                // 0x6998a4 gate
        }
        slot.contentMask = v.contentMask;          // slot+340 (slot+20) <- V+28 (V[7])
        slot.blendMode = v.blendMode;              // slot+44  <- V+52
        slot.ox = v.ox;                            // slot+56  <- V+64
        slot.oy = v.oy;                            // slot+64  <- V+72
        slot.packedColors = v.packedColors;        // slot+72.. <- V+80..92
        slot.opacity = v.opacity;  // slot+88 <- V+96, both are integer 0..255
        slot.x = v.coordX;                         // slot+96  <- V+104
        slot.y = v.coordY;                         // slot+104 <- V+112
        slot.z = v.coordZ;                         // slot+112 <- V+120
        slot.flipX = v.flipX != 0;                 // slot+120 <- V+128
        slot.flipY = v.flipY != 0;                 // slot+121 <- V+129
        slot.angle = v.angle;                      // slot+128 <- V+136
        slot.scaleX = v.scaleX;                    // slot+136 <- V+144
        slot.scaleY = v.scaleY;                    // slot+144 <- V+152
        // slot+152 (slantX/sx) intentionally NOT restored — the binary's restore
        // common block writes slot+160 (slantY) but skips slot+152 (slantX).
        slot.slantY = v.slantY;                    // slot+160 <- V+168
    }

    // Player_advanceRootAndNodes @0x6B6ADC — forward 4-stream walk.
    // Faithful function boundary for the [layer ① → root ② → var-track ③ →
    // node-deque ④] sequence the binary runs at each forward advance point. The
    // four stream seeks are already ported as separate methods; this groups them
    // at the binary's function boundary so the call graph matches (one call per
    // advance point instead of five inlined copies). All four are keyed on the
    // SAME clampedEvalTime (player+456). progressSeekNodeSlotsLike (④) carries
    // the node+8 split (parameterized → advanceNodeFrames 0x6B7E44 / non-param →
    // inline forward seek) and self-guards on an empty node deque.
    void Player::advanceRootAndNodes_0x6B6ADC(double clampedEvalTime) {
        advanceLayerEventStreamLike_0x6B6ADC(clampedEvalTime);  // ① layer 0x6B6B8C
        advanceRootContentStreamLike_0x6B6ADC(clampedEvalTime); // ② root 0x6B6F48
        advanceVariableTracksLike_0x6B6ADC(clampedEvalTime); // ③ var-track 0x6B7124 (forward)
        progressSeekNodeSlotsLike_0x6C106C(clampedEvalTime); // ④ node deque 0x6B7358
    }

    // Player_rewindRootAndNodes @0x6B9A3C — reverse 4-stream walk. Same boundary
    // as advanceRootAndNodes but with dedicated layer/root decrement loops and
    // the REVERSE var-track stepper (rewindVariableTracksLike 0x6B9FCC).
    // The node deque driver is shared, but dispatches to distinct single-direction
    // inline boundaries: forward @0x6B73DC and reverse @0x6BA1CC. Parameterized
    // nodes take Player_advanceNodeFrames @0x6B7E44 in either direction.
    void Player::rewindRootAndNodes_0x6B9A3C(double clampedEvalTime) {
        rewindLayerEventStreamLike_0x6B9A3C(clampedEvalTime);  // ① layer 0x6B9AE8
        rewindRootContentStreamLike_0x6B9A3C(clampedEvalTime); // ② root 0x6B9E84
        rewindVariableTracksLike_0x6B9A3C(clampedEvalTime);  // ③ var-track 0x6B9FCC (reverse)
        progressSeekNodeSlotsLike_0x6C106C(clampedEvalTime, /*forward=*/false); // ④ node deque 0x6BA158 (backward inline seek)
    }

    void Player::frameProgress(double dt) {
        // === Player_progress_inner @0x6C106C 入口 ===
        // 移除 port-invented `if(!_syncActive) return;`：二进制 progress_inner 入口
        // (0x6C1080..0x6C10AC) 全是无条件副作用，**无** play/pause/null 守卫。
        // +1093(_syncActive) 只是 advance/rewindRootAndNodes 内部的 align/sync/action
        // 事件 gate，不门控整个 progress（progress_core_M1 note 勘误，本轮 IDA
        // 复核 0x6C106C 入口拓扑确认）。
        const double actualDelta = dt;
        // (A2) No `_frameLastTime = dt` here: +904 was a dead port-invented field.
        // The binary stores no raw dt at entry — only +592=_deltaTime below.

        // §1 入口无条件副作用 (0x6C108C): +483 motionCompleted 每帧清零。二进制在
        // 任何分支/return 之前 STRB WZR,[X19,#0x1E3]；Player.h:1148 已注明
        // "cleared each progress entry" 但此前漏实现 —— 此处补上。因此入口门控的
        // `if(_motionCompleted) return`(0x6C1100) 读到的恒为 0（dead-but-faithful，
        // 二进制为 emote 路径保留该检查）；帧内 loop-wrap 守卫(0x6C1474/0x6C1498 等)读
        // 到的是 advanceRootAndNodes 在本帧内重新设置的值，不受入口清零影响（本端
        // 这两处读取是 _motionCompleted 仅有的引用点，无跨帧依赖）。
        // 二进制入口另清 +1152 DWORD（0x6C1088 `*(_DWORD*)(a1+1152)=0`，用途待定）；
        // 本端 Player 类无 +1152 字段（grep 确认所有 +1152 引用均为 EmoteEngine 的
        // _windFreqY=engine+1152 风缓存，不同对象），故该 DWORD 清零无可建模目标，属
        // "未建模字段"缺口而非遗漏 —— 不臆造字段。
        _motionCompleted = false;

        // player+592(_deltaTime) = speedMul*dt @0x6C1094。二进制入口写入顺序为
        // 0x6C1088(+1152=0) → 0x6C108C(+483=0) → 0x6C1094(+592=write)，故 _deltaTime
        // 写入必须排在 _motionCompleted=false 之后以与二进制计算顺序一致。该写入须在
        // firstFrame 块(0x6C1108 读 v8=+592)与 LABEL_48(0x6C1334 读 +592)之前完成，
        // 否则两处读到上一帧的陈旧 _deltaTime（IDA 0x6C1108 注释标注的 PORT BUG），
        // PlayerUpdateAnchor 的 _deltaTime==0 gate 与阻尼计算同样依赖本帧值。
        _deltaTime = _speedMul * actualDelta;       // player+592 @0x6C1094
        if(_directEdit) {                           // player+482 @0x6C1098
            initEmoteMotionLike_0x6B2E90(2u);       // 0x6C10A4
        }

        // HM2 (_evalResultValues @+320) is NOT cleared per-frame. Byte-verified
        //   against Player_progress_inner @0x6C106C (2026-06-03): its entry clears
        //   ONLY +1152 (`*(_DWORD*)(a1+1152)=0` @0x6c1088) and +483
        //   (motionCompleted, @0x6c108c) — the full body never writes any of the
        //   four hashmaps (+264 HM1 / +320 HM2 / +1184 HM3 / +1240 HM4). The binary
        //   HM2 is persistent across frames (written by-overwrite via the bind-loop
        //   / Player_bindParameterValue @0x6C4668 LABEL_132, read by getVariable
        //   @0x533E1C, cleared only on motion reset/load — mirrored locally by
        //   PlayerCore/PlayerResource resets). The previous per-frame `.clear()`
        //   here was port-invented; it wiped the EmoteEngine_progress bind-loop's
        //   step-5 HM2 writes before getVariable (called after the step-7
        //   sub_6D2A54→frameProgress) could read them. Removed for 1:1 alignment.

        // 砖5/洞1: progress_inner's first step is Player_preProgressDirtyNodes
        // (0x6C10AC), before the firstFrame/cursor logic. Inert in the web port
        // (no "modified"-setter) but ported for call-chain restoration.
        preProgressDirtyNodesLike_0x6B6878();

        // Player_progress_inner @0x6C10B0: Player+376 is the active/default
        // MotionParameterEntry selected while loading the motion.  Its value
        // drives the player's cursor directly; ordinary dt progression is not
        // entered while this pointer is present.  The local semantic match for
        // Player+376 is _defaultParameterEntryPtr (assigned by the same
        // parameterize dictionary/index load paths as 0x6B365C).
        if(_defaultParameterEntryPtr != nullptr) {
            const double parameterTime = _defaultParameterEntryPtr->value;
            if(_firstFrame) {                         // 0x6C10C0..0x6C10DC
                _frameTickCount = parameterTime;
                _firstFrame = false;
                _clampedEvalTime = parameterTime;
                reseekTimelineCursors(_clampedEvalTime);
                return;
            }
            if(parameterTime > _clampedEvalTime) {   // 0x6C11F4..0x6C1220
                _frameTickCount = parameterTime;
                _clampedEvalTime = parameterTime;
                advanceRootAndNodes_0x6B6ADC(_clampedEvalTime);
                return;
            }
            if(parameterTime < _clampedEvalTime) {   // 0x6C1224..0x6C1250
                _frameTickCount = parameterTime;
                _clampedEvalTime = parameterTime;
                rewindRootAndNodes_0x6B9A3C(_clampedEvalTime);
                return;
            }
            // 0x6C1254..0x6C126C: equal cursor — refresh only nodes whose
            // binary node+8 parameter pointer is present.  parameterEntry can
            // also contain the local fallback entry, so retain the integer
            // parameterize gate that models node+8 itself.
            for(size_t i = 1; i < _nodes.size(); ++i) {
                auto &node = _nodes[i];
                if(node.parameterizeIndex >= 0 &&
                   node.parameterEntry != nullptr) {
                    advanceNodeFramesLike_0x6B7E44(
                        node, _clampedEvalTime);
                }
            }
            return;
        }

        // === loc_6C10E4 入口门控 (二进制真实拓扑, 本轮 IDA 复核 0x6C10E4..0x6C1278) ===
        // +376==0 的普通时间轴路径从 loc_6C10E4 开始；非空分支已在上方
        // 由 _defaultParameterEntryPtr 一比一承接。
        // 0x6C10F0: if(+481 firstFrame==0 && +1099 loopArmed==0) goto loc_6C1270。
        // 字段映射（IDA 确认）：+481=_firstFrame、+1099=_allplaying(loopArmed)。
        if(!_firstFrame && !_allplaying) {
            // loc_6C1270 (0x6C1270/0x6C1278): renderList(+384/+392) 空检查。
            // renderList = 二进制每帧 framesel(parse 0x6926B4 / merge 0x692AB0 /
            // lerp 0x699AE4) 产出、updateLayers(0x6BBD44) 消费、initNonEmoteMotion
            // (0x6B3914) Release+清空的 56B 内容条目 vector(begin/end/cap, 首字段
            // tTJSVariant*)。本端当前在这个门控点检查承载节点帧步进的 _nodes；
            // 原版门控对象仍是上述 renderList，二者的剩余容器归属差异需单独迁移。
            // 一个从未 play 过的 child（无 motion：firstFrame=0 / loopArmed=0 /
            // _cachedTotalFrames=_loopTime=0 / _nodes 空）在此 0x6C1278 return，
            // **永不到达 LABEL_48 forward loop-wrap do-while**（v7 += loopTime-totalFrames
            // = 0-0，while(0<=v7) 永真 -> 千恋万花标题死循环）。这（非 loopTime<lastTime
            // 不变量、非 +1136<0 默认）才是二进制避免全 0 child 空转的真正机制。
            if(_nodes.empty()) {              // 0x6C1278 renderList 空 -> return result
                return;
            }
            // 非空 (0x6C127C..0x6C130C): 遍历 node-deque，每个 node+8 有效者调
            // Player_advanceNodeFrames(node, this)。本端等价 = 一次 seek-to-+456 的
            // node 槽刷新（progressSeekNodeSlotsLike 内含 forward + corrective-backward
            // 子环，方向无关），随后 return（二进制非空分支走完循环即 return result）。
            progressSeekNodeSlotsLike_0x6C106C(_clampedEvalTime);
            return;
        }

        // 到此 (firstFrame || loopArmed)，二进制 0x6C10F4 起的短路检查：
        if(_syncWaiting) {                    // 0x6C10F8 syncWaiting!=0 -> return result
            return;
        }
        if(_motionCompleted) {                // 0x6C1100 motionCompleted!=0 -> return
            // 上方入口已清 _motionCompleted=0，故此处恒不成立（dead-but-faithful，
            // 复刻二进制为 emote/initEmoteMotion 路径保留的检查点）。
            return;
        }
        // 0x6C1104: if(firstFrame==0) goto LABEL_48（仅 loopArmed，跳过 firstFrame 块）。
        // 本端 firstFrame 种子标志由 _firstFrame(+481) 承载（二进制此块 0x6C1104 读 +481、
        // 0x6C110C 清 +481；STRH 0x0101 在 setTickCount/child-motion 写入时同置 +480/+481）。
        //
        // === firstFrame 块 (0x6C1108..0x6C132C) — 精确复刻 progress_inner ===
        // 此前本端把整块坍缩成 blanket `reseekTimelineCursors(_clampedEvalTime); return`，
        // 丢失了二进制的两段方向性逻辑（2026-06-06 fresh-decompile 审计缺口#4）：
        //   (b) 0x6C1120 reverse-from-end 种子：v8(+592 deltaTime)<0 && +1120==0 时
        //       +456 = +1128(lastTime); +1120 = +1128。
        //   (a) 0x6C1130 +609 reverseSeekFlag 方向性 seek：
        //         forward(v8>=0): save +456 → +456=0 → reseek → advanceRoot → restore
        //         reverse(v8<0) : (+1128>+1120 才) save +456 → +456=+1128 → reseek →
        //                         rewind → restore  (+1128<=+1120 直接 goto LABEL_48)
        //       else(+609==0): plain reseek(@0x6C131C)。
        // 二进制每步带 +1098(syncWaiting)/+483(motionCompleted) 早退；全块走完后
        // **fall-through 到 LABEL_48**（不是 return），LABEL_48 的 cursor advance 被
        // +480 gate(=_queuing，本帧仍为 1) 跳过。本端忠实复刻：用 reseekNodes 之外的
        // 直接调用，块末不 return，自然落到下方 LABEL_48。
        // 字段交叉核实（全部 disasm 直读，非命名推断）：+456=_clampedEvalTime(0x1C8)、
        // +592=_deltaTime(0x250)、+609=_reverseSeekFlag(0x261，writer 0x6BE4F8 同字段)、
        // +1120=_frameTickCount(0x460)、+1128=_cachedTotalFrames(0x468)。
        if(_firstFrame) {                              // 0x6C1104: firstFrame!=0
            // (B) progress_inner 不写 syncActive(+1093)。该字段全二进制仅两个
            // writer：Player_ctor@0x6CF11C 与 Player_setSyncActive@0x6D9698(脚本
            // setter)，由 advance/rewind/reseek 游标推进函数只读作 gate。原先此处
            // `_syncActive = _syncWaiting && _allplaying` 是杜撰，已删。
            const double deltaTime = _deltaTime;       // v8 = *(double*)(+592)  (0x6C1108)
            _firstFrame = false;                       // *(BYTE*)(+481) = 0     (0x6C110C)

            // (b) 0x6C1120: reverse-from-end seed
            if(deltaTime < 0.0 && _frameTickCount == 0.0) {   // 0x6C1110/0x6C111C/0x6C1120
                _clampedEvalTime = _cachedTotalFrames;        // +456 = +1128 (0x6C1128)
                _frameTickCount  = _cachedTotalFrames;        // +1120 = +1128 (0x6C112C)
            }

            // (a) 0x6C1130: +609 reverseSeekFlag directional seek
            if(_reverseSeekFlag) {                     // 0x6C1130
                _reverseSeekFlag = false;              // *(BYTE*)(+609) = 0 (0x6C113C)
                if(deltaTime >= 0.0) {                 // 0x6C1140 -> 0x6C13AC: FORWARD
                    const double saved = _clampedEvalTime; // v30 = *(+456) (0x6C13AC)
                    _clampedEvalTime = 0.0;            // +456 = 0 (0x6C13B4) — seek-to-0
                    reseekTimelineCursors(_clampedEvalTime);          // 0x6C13B8
                    if(_syncWaiting)     return;       // 0x6C13BC
                    if(_motionCompleted) return;       // 0x6C13C4
                    _clampedEvalTime = saved;          // +456 = v30 (0x6C13D0) — restore
                    advanceRootAndNodes_0x6B6ADC(_clampedEvalTime);   // 0x6C13D4
                    if(_syncWaiting)     return;       // 0x6C13D8
                } else {                               // 0x6C1144: REVERSE
                    const double lastTime = _cachedTotalFrames; // v10 = *(+1128) (0x6C1144)
                    if(lastTime > _frameTickCount) {   // 0x6C114C: only seek if +1128 > +1120
                        const double saved = _clampedEvalTime; // v11 = *(+456) (0x6C1154)
                        _clampedEvalTime = lastTime;   // +456 = +1128 (0x6C115C) — seek-to-end
                        reseekTimelineCursors(_clampedEvalTime);      // 0x6C1160
                        if(_syncWaiting)     return;   // 0x6C1164
                        if(_motionCompleted) return;   // 0x6C116C
                        _clampedEvalTime = saved;      // +456 = v11 (0x6C1178) — restore
                        rewindRootAndNodes_0x6B9A3C(_clampedEvalTime);// 0x6C117C
                        if(_syncWaiting)     return;   // 0x6C1180
                    }
                    // else (+1128 <= +1120): 0x6C1150 -> goto LABEL_48 directly (no seek)
                }
            } else {                                   // 0x6C1318: +609==0, plain reseek
                reseekTimelineCursors(_clampedEvalTime);              // 0x6C131C
                if(_syncWaiting) return;               // 0x6C1320
            }
            if(_motionCompleted) return;               // 0x6C1328
            // 二进制 0x6C1328 fall-through 到 LABEL_48(0x6C1330)。此帧 gate(+480=_queuing)
            // 仍为 1，故 LABEL_48：(1) gated clamp(0x6C1338 CBNZ +480)被跳过；(2) forward
            // 分支 not-at-end(tick<total) 落 `else if(!gate)`，gate=1 → 不调 advanceRoot →
            // **return result(0x6C13A4)**。即二进制 firstFrame 路径的净效果 = 仅 reseek 后
            // 返回，LABEL_48 不产生任何可观察副作用。
            //
            // 本端 `_deltaTime=...; if(!_queuing)+1120+=...` 已经是 LABEL_48
            // gated advance 本身，gate=1 时为 no-op。此前位于此后的
            // preProgressPlayingTimelinesLike_0x671764 错位调用已经迁出：0x671764
            // 的真实 this 是 EmoteEngine，且只由 EmoteEngine_progress @0x67D060
            // 调用。故此处 `return` 现在只表达二进制 firstFrame 路径经 gate=1
            // 落到 0x6C13A4 返回的净效果，不再用于绕开任何 port-invented 调用。
            return;                                    // = 0x6C13A4 LABEL_48 gated forward return
        }

        // R1.H2: removed `_frameLoopTime += actualDelta;` — port-invented
        // duplicate accumulator that collided with _frameTickCount on +1120.
        // Binary's progress_inner @0x6C106C maintains a single cursor at +1120
        // (= _frameTickCount); the previous _loopTime corruption stemmed from
        // this twin-accumulator pattern (commit e11ecef formally removed the
        // _loopTime side; R1.H2 deletes the field declaration itself).
        //
        // M1 P5/G3: Player_progress_inner @0x6C106C LABEL_48 advances the frame
        //   cursor by deltaTime(+592 = speedMul(+1168)*dt), gated by _queuing
        //   (+480) — when the gate's LSB is set the cursor is frozen. Mirror that:
        //     if (!_queuing) _frameTickCount += _deltaTime;
        //   At P1 defaults (_speedMul=1.0, _queuing=false) this is exactly the
        //   previous `_frameTickCount += actualDelta`, so behaviour is preserved.
        if(!_queuing) {                              // player+480 LSB gate (LABEL_48)
            _frameTickCount += _deltaTime;           // player+1120 += player+592
        }

        // 砖6/Stage C — 0x671764 call removed from this function.
        // Fresh disassembly of EmoteEngine_progress @0x67D01C proves the call at
        // 0x67D060 keeps X0=EmoteEngine*, sets W1=0 and passes the original dt in
        // V0. Player_progress_inner @0x6C106C never calls it; its own pre-progress
        // step is Player_preProgressDirtyNodes @0x6B6878, already invoked above.
        // The former call here advanced EmoteEngine timeline state on every plain
        // MotionPlayer frame and could enter its loop-window while loop for DRACU's
        // main/mono_loop motions. The binary call now lives solely in
        // EmoteEngine::progress before the controller-slice loop.
        //
        // NOTE on _evalResultValues (= PORT MIRROR OF HM2 @+320, a real binary
        // container — written by Player_bindParameterValue_writesHM1_HM2 @0x6C4668
        // LABEL_132 `HM2_upsert(player+320,label)=value`, read by getVariable
        // @0x533E1C): the former frameProgress-entry `.clear()` has been REMOVED
        // (see the entry comment above) to align with the binary, where HM2 is
        // persistent across frames and progress_inner @0x6C106C never touches +320.
        // The fixed-controller eval refresh below overwrites its own labels in a
        // persistent HM2 rather than repopulating a cleared one — matching the
        // by-overwrite semantics of the binary bind-loop.
        // 子步进 controller 循环 REMOVED (2026-06-05 — 误植拓扑修正)。
        //
        // 此处曾有一个 `while(dt>0){ slice=fmin(dt,1.1); refreshFixed(); dt-=slice }`
        // 子步进循环，自承"对齐 EmoteEngine_progress @0x67D01C step loops
        // @0x67d0a4..0x67d370"。本轮 fresh-decompile（0x6C106C / 0x67D01C）证实这是
        // 错误的拓扑放置：
        //   - frameProgress 复刻的是 Player_progress_inner @0x6C106C（MotionPlayer 类）。
        //     该函数对入参 dt 是一次性使用 —— @0x6c1090 `+592 = speedMul*frameDt`，此后
        //     全程只读 +592，**没有任何 fmin(dt,1.1)、没有 while(dt>0) 子步进**。dt 经
        //     LABEL_48 前进/后退分支一次性推进帧游标 +1120。
        //   - fmin(dt,1.1) 子步进循环只属于 EmoteEngine_progress @0x67D01C（EmotePlayer
        //     类）。二进制里每次迭代把 slice 作为 dt 传给 7 个 controller deque 的 step
        //     （物理相位 @0x67d2d4 `phase += slice`，各 *_step(slice) 按 slice 推进时间
        //     状态）——是有意义的物理积分细分。这段已正确实现于 EmoteEngine::progress
        //     （EmoteEngine.cpp 子步进循环，每个 step 都传 `step`），循环结束后 bind-loop
        //     写 Player HM1/HM2，再调 progressFramesLike_0x6D2A54 → progress_inner（=本
        //     函数）恰好一次。
        //   - 调用链证据：非 emote 的 `Player.progress → progressCompat @0x6D2A98 →
        //     sub_6D2A54 → progress_inner` 全程不触及 @0x67D01C；progress_inner 的 callee
        //     里没有任何 controller step。子步进与 progress_inner 是兄弟（EmoteEngine 调
        //     progress_inner），不是父子。
        //
        // 误植后果（本轮定位的标题卡死元凶）：非 emote 的 Player.progress 路径在大 dt 时
        // （progressCompat 把 ms clamp 到 [0,60000] → ×60/1000 → 最大 3600 帧）会空转
        // ⌈dt/1.1⌉≈3273 次，且 refreshFixed() 不接收 controllerDt、每次迭代做完全相同的
        // 工作（既无累积也无意义）；再经 updateLayersPhase3_MotionSubNode 对每个 Motion
        // 子节点递归 child.frameProgress(同一 dt)，迭代数 × 节点数 → 单帧卡死。移除后
        // frameProgress 与 progress_inner @0x6C106C 一一对应（一次性消费 dt）。

        // EMOTE POST-PROCESS MIGRATED OUT (2026-06-03 approved topology refactor).
        // The binary Player_progress_inner @0x6C106C (which frameProgress models)
        // contains NO HM7 bind-loop and NO clampControl binder — both live ONLY in
        // EmoteEngine_progress @0x6818B4 (bind-loop @0x67d3a4, sub_67C8A8 clamp
        // @0x67d3f8), BEFORE the step-7 Player progress sub_6D2A54 @0x67d408. The
        // child-motion pass @0x6BE2A4 also calls progress_inner directly, with no
        // post-process. Re-confirmed by fresh decompile of 0x6C106C / 0x6BE2A4 /
        // 0x6818B4 / 0x67C8A8 this round. The former Player-side duplicate put
        // the emote-only clamp on this generic path and double-ran the Engine
        // bind loop. It and its eager snapshot clamp table have been deleted;
        // the sole live owner is now EmoteEngine::progress (HM7 bind loop at
        // 0x67D3A4, raw Engine clamp deque at 0x67D3F8).

        // Camera velocity/friction moved to updateLayers pre-loop (0x6BB360..0x6BB42C)

        // M1 P5/G4 + P7 step-2: Player_progress_inner @0x6C106C LABEL_48.
        // After the gated cursor advance above (+1120 += +592; +456 =
        // min(+1120,+1128) when the +480 gate is clear), the binary branches on
        // the SIGN of deltaTime(+592) and on whether the cursor reached the end
        // / start, into forward-normal / forward-to-end(loop|stop) /
        // reverse-rewind / loop-wrap. Each terminal branch then re-seeks the
        // node slots through direction-specific binary boundaries:
        // Player_advanceRootAndNodes 0x6B6ADC selects the non-parameterized
        // forward inline @0x6B73DC, while Player_rewindRootAndNodes 0x6B9A3C
        // selects the backward inline @0x6BA1CC. Parameterized nodes alone call
        // Player_advanceNodeFrames @0x6B7E44, whose implementation contains a
        // forward pass plus corrective backward pass in either play direction.
        // The local progressSeekNodeSlotsLike_0x6C106C(+456, forward) preserves
        // that node+8 split and dispatches the matching direction helper.
        //
        // The clamp `+456 = min(+1120,+1128)` was already applied above (the
        // gated-advance block). LABEL_48 below re-derives the SAME value for the
        // forward-not-at-end (= logo) path, so that path stays bit-identical to
        // P7 step-1; only the at-end / reverse / loop-wrap paths add behavior.
        // gate v23 = +480 snapshot (was sampled at the gated-advance block);
        // deltaTime v24 = +592.
        const bool gate = _queuing;              // v23 = (BYTE)player+480
        const double deltaTime = _deltaTime;     // v24 = player+592

        // Gated clamp (0x6C1340..0x6C1354): when the +480 gate is clear,
        //   v26 = +592 + +1120; +1120 = v26; if (v26 > +1128) v26 = +1128;
        //   +456 = v26;  (= min(advanced cursor, totalFrames))
        // The +1120 advance was already applied above (line ~831, the same
        // `!_queuing` gate). Here we mirror only the +456 clamp half so the
        // forward-not-at-end path keeps the P7 step-1 value bit-for-bit
        // (+456 = min(+1120,+1128); for not-at-end that is +1120 = _frameTickCount).
        if(!gate) {                              // 0x6C1330 !(BYTE)player+480
            double v26 = _frameTickCount;        // +1120 (already += +592 above)
            if(v26 > _cachedTotalFrames) {       // 0x6C1350
                v26 = _cachedTotalFrames;
            }
            _clampedEvalTime = v26;              // +456 = min(+1120,+1128) (0x6C1354)
        }

        bool reseekNodes = false;                // whether to seek slots this frame

        if(deltaTime >= 0.0) {                   // 0x6C135C: FORWARD
            const double totalFrames = _cachedTotalFrames; // v29 = player+1128
            if(totalFrames <= _frameTickCount) { // 0x6C13A0: reached/past end
                // M1 P7 step-2 (5553dd2): binary reads +1136 as fixed loop-wrap
                // target (set once at setMotion from clip->loopTime, see
                // PlayerCore.cpp:~557) and NEVER mutates it inside progress_inner.
                // R-M9 spike Q1/Q2 confirmed 5 reads + 0 writes of +1136 in
                // progress_inner. The earlier `_loopTime += actualDelta` port-
                // invented accumulator was removed in commit e11ecef so this
                // path is now numerically aligned with binary's loop-wrap math.
                const double loopTime = _loopTime; // v31 = player+1136
                _clampedEvalTime = totalFrames;    // +456 = v29 (= totalFrames)
                if(loopTime >= 0.0) {              // 0x6C13F0: LOOP
                    // advanceRootAndNodes at the clamped end (+456 = totalFrames)
                    // 砖6/Stage A (洞3 调用点重定位): Player_advanceRootAndNodes
                    // (0x6B6ADC) runs the layer event stream (+916 cursor) BEFORE
                    // the node-deque walk (LABEL_86), both keyed on the SAME +456.
                    // The layer pass may align/sync-snap +456/+1120 (0x6B6DD8,
                    // +1093-only gate), and that snap must reach this SAME advance
                    // point's node walk. So seek the layer stream here, immediately
                    // before progressSeekNodeSlotsLike (= the node walk), at each
                    // advanceRoot/rewind equivalent point — not once at end-of-frame.
                    advanceRootAndNodes_0x6B6ADC(_clampedEvalTime); // 0x6C1468 (1st forward advance)
                    if(!_syncWaiting && !_motionCompleted) {              // 0x6C1474
                        _clampedEvalTime = _loopTime; // +456 = player+1136 (0x6C1484)
                        // reseekTimelineCursors (0x6C1488) — the FULL non-
                        // incremental re-seek: layer coarse scan (+916) + root
                        // re-seek (+568/+616) + var-track reseed (0x6B8F30) before
                        // the node walk. Previously this modelled ONLY the var-
                        // track reseed, skipping the layer/root coarse re-scan.
                        reseekTimelineCursors(_clampedEvalTime);             // 0x6C1488
                        progressSeekNodeSlotsLike_0x6C106C(_clampedEvalTime); // node walk (step 4)
                        if(!_syncWaiting && !_motionCompleted) {          // 0x6C1498
                            // LABEL_22/23 loop-wrap (0x6C14AC..0x6C14CC):
                            //   v7 = +1120; if (+1128 > v7) goto LABEL_23;
                            //   do v7 += +1136 - +1128; while (+1128 <= v7);
                            //   +1120 = v7;  (LABEL_22)  +456 = v7; (LABEL_23)
                            double v7 = _frameTickCount;            // 0x6C14B0
                            if(_cachedTotalFrames > v7) {           // 0x6C14B8 -> LABEL_23
                                _clampedEvalTime = v7;              // LABEL_23 (0x6C119C)
                            } else {
                                do {
                                    v7 = v7 + _loopTime - _cachedTotalFrames; // 0x6C14C4
                                } while(_cachedTotalFrames <= v7);  // 0x6C14CC
                                _frameTickCount = v7;               // LABEL_22 (0x6C1198)
                                _clampedEvalTime = v7;              // LABEL_23 (0x6C119C)
                            }
                            // 2nd forward advance (0x6C11B0, +456 = wrapped tick).
                            advanceRootAndNodes_0x6B6ADC(_clampedEvalTime); // 0x6C11B0
                        }
                    }
                } else {                          // 0x6C13F4: NON-LOOP, hit end -> stop
                    _allplaying = false;          // player+1099 = 0 (0x6C13F4)
                    if(!gate) {                   // 0x6C13F8
                        reseekNodes = true;       // advanceRootAndNodes
                    }
                }
            } else if(!gate) {                    // 0x6C13A4: NOT at end (= logo path)
                reseekNodes = true;               // advanceRootAndNodes
            }
            // else: gate set & not-at-end -> no seek (binary returns result)
        } else {                                  // 0x6C1360: REVERSE (deltaTime < 0)
            const double frameTickCount = _frameTickCount; // v27 = player+1120
            const double loopTime = _loopTime;             // v28 = player+1136
            if(frameTickCount >= 0.0 && loopTime <= frameTickCount) { // 0x6C1374 -> LABEL_57
                if(!gate) {                       // 0x6C138C
                    reseekNodes = true;           // rewindRootAndNodes (LABEL_57)
                }
            } else if(loopTime < 0.0) {           // 0x6C137C: non-loop, hit start
                _clampedEvalTime = 0.0;           // player+456 = 0 (0x6C1380)
                _allplaying = false;              // player+1099 = 0 (0x6C1384)
                _frameTickCount = 0.0;            // player+1120 = 0 (0x6C1388)
                if(!gate) {                       // LABEL_57 (0x6C138C)
                    reseekNodes = true;           // rewindRootAndNodes
                }
            } else {                              // 0x6C1404: reverse loop-wrap
                _clampedEvalTime = loopTime;      // player+456 = v28 (0x6C1404)
                rewindRootAndNodes_0x6B9A3C(_clampedEvalTime); // 0x6C1408 (1st reverse rewind)
                if(!_syncWaiting && !_motionCompleted) {              // 0x6C1414
                    _clampedEvalTime = _cachedTotalFrames; // +456 = player+1128 (0x6C1424)
                    // reseekTimelineCursors (0x6C1428) — the FULL non-incremental
                    // re-seek: layer coarse scan (+916) + root re-seek (+568/+616)
                    // + var-track reseed (0x6B8F30) before the node walk. Same
                    // 0x6B86C8 boundary as the forward wrap-point (0x6C1488);
                    // previously this modelled ONLY the var-track reseed.
                    reseekTimelineCursors(_clampedEvalTime);             // 0x6C1428
                    progressSeekNodeSlotsLike_0x6C106C(_clampedEvalTime); // node walk (step 4)
                    if(!_syncWaiting && !_motionCompleted) {          // 0x6C1434
                        // LABEL_27/28 reverse loop-wrap (0x6C143C..0x6C145C):
                        //   v7 = +1120; if (+1136 <= v7) goto LABEL_28;
                        //   do v7 += +1128 - +1136; while (+1136 > v7);
                        //   +1120 = v7;  (LABEL_27)  +456 = v7; (LABEL_28)
                        double v7 = _frameTickCount;            // 0x6C1440
                        if(_loopTime <= v7) {                   // 0x6C1448 -> LABEL_28
                            _clampedEvalTime = v7;              // LABEL_28 (0x6C11BC)
                        } else {
                            do {
                                v7 = v7 - _loopTime + _cachedTotalFrames; // 0x6C1454
                            } while(_loopTime > v7);            // 0x6C145C
                            _frameTickCount = v7;               // LABEL_27 (0x6C11B8)
                            _clampedEvalTime = v7;              // LABEL_28 (0x6C11BC)
                        }
                        // 2nd reverse rewind (0x6C11C0, +456 = reverse-wrapped tick).
                        rewindRootAndNodes_0x6B9A3C(_clampedEvalTime); // 0x6C11C0
                    }
                }
            }
        }

        // M1/P7 step-1: progress-pass cursor stepping (forward-normal /
        // non-loop-end / reverse-normal terminal: advance|rewindRootAndNodes).
        // Aligned to Player_progress_inner (0x6C106C) LABEL_48 / the node-deque
        // walk at 0x6C1288: once +456 (clampedEvalTime) is established, the
        // binary advances every node's frame cursor HERE (Player_advanceNodeFrames
        // 0x6B7E44, reached at 0x6C1264/0x6C130C), filling the two parsed-frame
        // slots (node+320/+856). The SEPARATE Player_updateLayers pass (0x6BB33C)
        // then only interpolates those slots (Player_evaluateTimeline 0x699AE4).
        // The loop-wrap / reverse-wrap branches above already re-seeked inline
        // (matching the binary's terminal advance/rewindRootAndNodes calls); this
        // covers the forward-normal / reverse-normal / non-loop-end cases.
        if(reseekNodes) {
            // The forward-not-at-end advanceRoot (0x6C13A4) OR the reverse LABEL_57
            // rewind (0x6C138C) — the normal-playback (logo) path. Dispatch on the
            // SAME deltaTime sign LABEL_48 used to pick the branch: forward reaches
            // here via Player_advanceRootAndNodes (0x6B6ADC), reverse via
            // Player_rewindRootAndNodes (0x6B9A3C). Each function owns its exact
            // single-direction layer/root/var-track loops.
            // The empty-node-deque guard now lives inside progressSeekNodeSlotsLike.
            if(deltaTime >= 0.0) {
                advanceRootAndNodes_0x6B6ADC(_clampedEvalTime); // 0x6C13A4 / 0x6C13F8
            } else {
                rewindRootAndNodes_0x6B9A3C(_clampedEvalTime);  // 0x6C138C (LABEL_57)
            }
        }

        // 砖6/Stage A (洞3 调用点重定位 — DONE): the faithful layer
        // (motion["tag"]) event stream is now driven INSIDE each advanceRoot /
        // rewind equivalent point above (each progressSeekNodeSlotsLike is now
        // preceded by the matching directional layer loop on the SAME +456), matching the
        // binary where Player_advanceRootAndNodes (0x6B6ADC) / rewindRootAndNodes
        // (0x6B9A3C) run [layer stream -> root -> var-track -> node walk] as one
        // unit. This fixes the three defects of the old single end-of-frame call:
        //   (1) loop-wrap segments (totalFrames then loopTime) each now scan the
        //       layer stream, so align/sync/action inside a wrapped segment fire;
        //   (2) align/sync snaps of +456/+1120 now propagate to the SAME advance's
        //       node walk (was a 1-frame lag);
        //   (3) the gate-set not-at-end / firstFrame-queuing paths correctly do
        //       NOT scan the layer stream (binary returns without advanceRoot).
        // (Player_reseekTimelineCursors 0x6B86C8 — the firstFrame seed + the two
        // loop-wrap reseek points 0x6C1488/0x6C1428 — carries its OWN layer scan
        // with a DIFFERENT gate (+920==+456 precise-frame, 0x6B8AC0); that is
        // Stage B and intentionally NOT covered by these advance-form seeks.)
        // (Per-node frame actions — node mask 0x40000 from the node seek — remain
        // 洞2, already handled inside progressSeekNodeSlotsLike's _pendingEvents.)

        // Player_progress_inner @0x6C106C does not derive +1099(loopArmed) from
        // the playing-list each frame; it only clears +1099 in terminal non-loop
        // branches above (0x6C13F4 / 0x6C1384). Keeping _allplaying independent is
        // required for image-side players whose script-visible motionPlaying is
        // +1099-backed even when no local timeline label was started.
        // (B) Removed fabricated `_syncActive = _syncWaiting && _allplaying`:
        // progress_inner never writes syncActive(+1093); it is a script-set gate
        // (writers = ctor 0x6CF11C + setSyncActive 0x6D9698 only).
    }


    void Player::progressMsLike_0x6D2A54(double deltaMs) {
        ensureMotionLoaded();
        if(deltaMs < 0 || deltaMs > 60000) {
            deltaMs = 0;
        }

        if(true) {
            _pendingEvents.clear();
        }
        frameProgress(deltaMs * kMotionFramesPerMillisecond);
        if(!_nodes.empty()) {
            updateLayers();
        }
        calcBounds();
        if(true) {
            _pendingEvents.clear();
        }
    }

    // Aligned with libkrkr2.so sub_6D2A54 @0x6D2A54 (raw, frame-units):
    //     *(player+16) = 0;                 // pendingEvents cursor
    //     Player_progress_inner(player, frameDt);   // frameDt ALREADY frames
    //     Player_updateLayers(player);
    //     Player_calcBounds(player);
    //     Player_dispatchEvents(player, *(player+16));
    //     *(player+16) = 0;
    // The middle arg in the binary call (sub_6D2A54(player, 0, frameDt)) is the
    // pendingEvents cursor seed (always 0 at the EmoteEngine_progress @0x67d408
    // call site) — modelled by the _pendingEvents.clear() bracket. NO ms->frame
    // conversion here: that is the wrapper's job (Player_progressCompat@0x6D2A98
    // does v10*60/1000 BEFORE calling progress_inner; sub_6818B4 does dt*60/1000
    // before the engine body). EmoteEngine::progress receives frame-dt already,
    // so it forwards frame-dt straight through. This is the step-7 Player progress
    // run AFTER the G2-C bind-loop so the bound HM1/HM2 values are in place when
    // the frame seek/eval reads them. NOTE: frameProgress (= progress_inner) wipes
    // _evalResultValues(HM2) at entry; the binary progress_inner @0x6C106C does
    // NOT clear +320, so the local HM2 clear vs the bind-loop write ordering is a
    // PRE-EXISTING question independent of this routing (affects round-trip
    // observability, see report) — not patched here per CLAUDE.md.
    void Player::progressFramesLike_0x6D2A54(double frameDt) {
        _pendingEvents.clear();                 // *(player+16)=0 @0x6d2a64
        frameProgress(frameDt);                 // Player_progress_inner @0x6d2a68
        if(!_nodes.empty()) {                   // updateLayers guard (port)
            updateLayers();                     // Player_updateLayers @0x6d2a70
        }
        calcBounds();                           // Player_calcBounds @0x6d2a78
        _pendingEvents.clear();                 // dispatchEvents + clear @0x6d2a84/88
    }

    tjs_error Player::progressCompatMethod(tTJSVariant *result, tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }

        self->ensureMotionLoaded();
        detail::MotionTraceProgressScope motionTraceScope(self, objthis);

        double delta = 0.0;
        if(numparams > 0 && param[0] && param[0]->Type() != tvtVoid) {
            delta = param[0]->AsReal();
        }
        // Clamp delta to sane range: TJS tick differences can overflow
        // when uint32 wraps (e.g. 4294967381 = 2^32 + 85)
        if(delta < 0 || delta > 60000) {
            delta = 0;
        }

        const auto motionPath = self->matchedMotionPath();
        // 二进制 progressCompat(0x6D2A98) 入口无任何栈遍历/字符串构建。shortTJSStackTrace()
        // 会走一遍 TJS 调用栈——必须先过路径门控
        // (logoChainTraceLogf 内部的同一判定) 再求值，否则每帧每个 player 在所有构建上都
        // 付出二进制不存在的开销，违背运行时行为复刻。
        if(detail::logoChainTraceEnabledForPath(motionPath)) {
            detail::logoChainTraceLogf(
                motionPath, "progressCompat.enter", "0x6D2A98",
                self->_clampedEvalTime,
                "deltaMs={:.3f} frameDt={:.6f} allplayingBefore={} nodesBefore={} stack={}",
                delta, delta * kMotionFramesPerMillisecond,
                self->_allplaying ? 1 : 0,
                self->_nodes.size(), shortTJSStackTrace());
        }

        self->_pendingEvents.clear();
        self->frameProgress(delta * kMotionFramesPerMillisecond);
        detail::logoChainTraceCheck(
            motionPath, "progressCompat.dt", "0x6D2A98",
            self->_clampedEvalTime,
            fmt::format("speedMul*dt_ms*60/1000={:.6f}",
                        self->_speedMul * delta * kMotionFramesPerMillisecond),
            fmt::format("deltaTime={:.6f}", self->_deltaTime),
            std::fabs(self->_deltaTime -
                      self->_speedMul * delta * kMotionFramesPerMillisecond) <
                0.000001,
            "progressCompat dt->_deltaTime(+592=speedMul*dt) diverged from 0x6C1094");

        // Aligned to libkrkr2.so Player_progressCompat (0x6D2A98):
        // progress_inner -> updateLayers -> calcBounds -> dispatchEvents.
        // The binary assumes the node tree is already built (it was built
        // eagerly inside play()/setMotion()), so there is no lazy build here.
        if(!self->_nodes.empty()) {
            detail::logoChainTraceLogf(
                motionPath, "progressCompat.update", "0x6D2A98",
                self->_clampedEvalTime,
                "timelineCurrentTime={:.3f} pendingEvents={} nodes={}",
                self->_clampedEvalTime, self->_pendingEvents.size(),
                self->_nodes.size());
            self->updateLayers();
        }
        self->calcBounds();
        if(detail::logoChainTraceEnabledForPath(motionPath)) {
            detail::logoChainTraceLogf(
                motionPath, "progressCompat.exit", "0x6D2A98",
                self->_clampedEvalTime,
                "allplayingAfter={} nodesAfter={} bounds=({:.3f},{:.3f},{:.3f},{:.3f})",
                self->_allplaying ? 1 : 0,
                self->_nodes.size(), self->_boundsMinX, self->_boundsMinY,
                self->_boundsMaxX, self->_boundsMaxY);
        }

        if(detail::logoSnapshotMarkEnabledForPath(motionPath) &&
           motionPath.find("m2logo.mtn") != std::string::npos &&
           self->_clampedEvalTime >= 0.0 && self->_clampedEvalTime <= 60.0) {
            std::fprintf(stderr,
                         "SNAPTIME motion=%s frame=%.3f playing=%d nodes=%zu\n",
                         motionPath.c_str(), self->_clampedEvalTime,
                         self->_allplaying ? 1 : 0,
                         self->_nodes.size());
        }

        if(detail::logoSnapshotMarkEnabledForPath(motionPath) &&
           motionPath.find("m2logo.mtn") != std::string::npos &&
           self->_clampedEvalTime >= 30.0 && self->_clampedEvalTime <= 50.0) {
            std::fprintf(stderr, "SHOTMARK motion=%s frame=%.3f\n",
                         motionPath.c_str(), self->_clampedEvalTime);
        }

        // Aligned to libkrkr2.so Player_dispatchEvents (0x6C4490):
        // After stepping timelines, dispatch queued onAction/onSync events.
        if(!self->_pendingEvents.empty()) {
            for(const auto &ev : self->_pendingEvents) {
                try {
                    if(ev.type == 0) {
                        // Player_dispatchEvents @0x6C4490 copy-constructs both
                        // queued variants before invoking onAction.
                        tTJSVariant p1(ev.param1);
                        tTJSVariant p2(ev.param2);
                        tTJSVariant *args[] = { &p1, &p2 };
                        objthis->FuncCall(0, TJS_W("onAction"),
                            nullptr, nullptr, 2, args, objthis);
                    } else if(ev.type == 1) {
                        // onSync()
                        objthis->FuncCall(0, TJS_W("onSync"),
                            nullptr, nullptr, 0, nullptr, objthis);
                    }
                } catch(...) {}
            }
            self->_pendingEvents.clear();
        }

        return TJS_S_OK;
    }

} // namespace motion
