#pragma once

#include <utility>
#include <vector>

#include "tjs.h"

namespace motion::detail {

    // sub_697D34 @0x697D34. The input is a source-level ttstr value copied
    // into a mutable remainder; each separator-delimited prefix is pushed as
    // an independently owning ttstr and the final remainder is always pushed
    // (including an empty one). ResourceManager_findSource/findMotion and
    // Player_initEmoteMotion share this exact helper in libkrkr2.so.
    inline std::vector<ttstr> splitTtstrLike_0x697D34(
        ttstr remainder, const ttstr &separator) {
        std::vector<ttstr> pieces;
        for(;;) {
            const int index = remainder.IndexOf(separator, 0);
            if(index < 0) {
                break;
            }
            pieces.push_back(remainder.SubString(
                0, static_cast<unsigned int>(index)));
            const unsigned int next = static_cast<unsigned int>(index) +
                separator.GetLen();
            remainder = remainder.SubString(next, remainder.GetLen() - next);
        }
        pieces.push_back(remainder);
        return pieces;
    }

    inline std::vector<ttstr> splitTtstrLike_0x697D34(
        ttstr remainder, tjs_char separator) {
        return splitTtstrLike_0x697D34(
            std::move(remainder), ttstr(separator));
    }

    // Shared source reconstruction of the Motion_propGet* helpers used by the
    // Android motionplayer.  Each helper deliberately calls the holder's
    // dispatch with that same dispatch as objthis; this is the exact call shape
    // in 0x662668, 0x6635DC, 0x6636D4, 0x6695BC, 0x6637BC and 0x56C694.
    inline tTJSVariant motionPropGet(const tTJSVariant &holder,
                                     const tjs_char *member,
                                     tjs_uint32 flags = 0,
                                     tjs_uint32 *hint = nullptr) {
        tTJSVariant value;
        iTJSDispatch2 *dispatch = holder.AsObjectNoAddRef();
        (void)dispatch->PropGet(flags, member, hint, &value, dispatch);
        return value;
    }

    inline bool motionTryPropGet(const tTJSVariant &holder,
                                 const tjs_char *member,
                                 tTJSVariant &value,
                                 tjs_uint32 flags = TJS_MEMBERMUSTEXIST,
                                 tjs_uint32 *hint = nullptr) {
        iTJSDispatch2 *dispatch = holder.AsObjectNoAddRef();
        return TJS_SUCCEEDED(
            dispatch->PropGet(flags, member, hint, &value, dispatch));
    }

    inline tTJSVariant motionPropGetByNum(const tTJSVariant &holder,
                                          tjs_int index, tjs_uint32 flags = 0) {
        tTJSVariant value;
        iTJSDispatch2 *dispatch = holder.AsObjectNoAddRef();
        (void)dispatch->PropGetByNum(flags, index, &value, dispatch);
        return value;
    }

    inline tjs_real motionPropGetDouble(const tTJSVariant &holder,
                                        const tjs_char *member,
                                        tjs_uint32 flags = 0,
                                        tjs_uint32 *hint = nullptr) {
        // Motion_propGetDouble @ 0x662668.
        return motionPropGet(holder, member, flags, hint).AsReal();
    }

    inline tjs_int motionPropGetInt(const tTJSVariant &holder,
                                    const tjs_char *member,
                                    tjs_uint32 flags = 0,
                                    tjs_uint32 *hint = nullptr) {
        // Motion_propGetInt @ 0x6635DC.
        return static_cast<tjs_int>(
            motionPropGet(holder, member, flags, hint).AsInteger());
    }

    inline bool motionPropGetBool(const tTJSVariant &holder,
                                  const tjs_char *member, tjs_uint32 flags = 0,
                                  tjs_uint32 *hint = nullptr) {
        // sub_6636D4 @ 0x6636D4.
        return motionPropGet(holder, member, flags, hint).operator bool();
    }

    inline ttstr motionPropGetString(const tTJSVariant &holder,
                                     const tjs_char *member,
                                     tjs_uint32 flags = 0,
                                     tjs_uint32 *hint = nullptr) {
        // sub_529524 @ 0x529524 performs PropGet followed by the ordinary
        // variant-to-ttstr conversion into the caller's destination.
        return ttstr(motionPropGet(holder, member, flags, hint));
    }

    inline tjs_int motionPropGetCount(const tTJSVariant &holder) {
        // sub_56C694 @ 0x56C694 uses PropGet("count"), not GetCount().
        return static_cast<tjs_int>(
            motionPropGet(holder, TJS_W("count")).AsInteger());
    }

    inline tjs_real motionPropGetDoubleByNum(const tTJSVariant &holder,
                                             tjs_int index,
                                             tjs_uint32 flags = 0) {
        // sub_6695BC @ 0x6695BC.
        return motionPropGetByNum(holder, index, flags).AsReal();
    }

    inline tjs_int motionPropGetIntByNum(const tTJSVariant &holder,
                                         tjs_int index, tjs_uint32 flags = 0) {
        // sub_6637BC @ 0x6637BC.
        return static_cast<tjs_int>(
            motionPropGetByNum(holder, index, flags).AsInteger());
    }

} // namespace motion::detail
