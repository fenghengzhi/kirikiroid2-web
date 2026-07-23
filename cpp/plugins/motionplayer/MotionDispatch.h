#pragma once

#include <utility>
#include <vector>

#include "tjs.h"

namespace motion::detail {

    // Process-wide mutable TJS member-hint slots.  These are individual
    // plugin globals in libkrkr2.so, shared by every dispatch call site that
    // references the same address; they are not per-function caches.
    extern tjs_uint32 widthMemberHint_guess;   // 0x1AB820C, "width"
    extern tjs_uint32 heightMemberHint_guess;  // 0x1AB8210, "height"
    extern tjs_uint32 originXMemberHint_guess; // 0x1AB8214, "originX"
    extern tjs_uint32 originYMemberHint_guess; // 0x1AB8218, "originY"
    extern tjs_uint32 blankMemberHint_guess;   // 0x1AB821C, "blank"
    extern tjs_uint32 clipMemberHint_guess;    // 0x1AB8220, "clip"
    extern tjs_uint32 leftMemberHint_guess;    // 0x1AB8224, "left"
    extern tjs_uint32 topMemberHint_guess;     // 0x1AB8228, "top"
    extern tjs_uint32 rightMemberHint_guess;   // 0x1AB822C, "right"
    extern tjs_uint32 bottomMemberHint_guess;  // 0x1AB8230, "bottom"
    extern tjs_uint32 xMemberHint_guess;       // 0x1AB8234, "x"
    extern tjs_uint32 yMemberHint_guess;       // 0x1AB8238, "y"

    // getCommandList @0x6D3A4C command/dictionary member-hint slots.
    extern tjs_uint32 commandKeyMemberHint_guess;          // 0x1AB82D8, "key"
    extern tjs_uint32 commandIdMemberHint_guess;           // 0x1AB83DC, "id"
    extern tjs_uint32 commandSrcMemberHint_guess;          // 0x1AB8134, "src"
    extern tjs_uint32 coordinateMemberHint_guess;          // 0x1AB8400, "coordinate"
    extern tjs_uint32 opacityMemberHint_guess;             // 0x1AB8490, "opacity"
    extern tjs_uint32 blendModeMemberHint_guess;           // 0x1AB8448, "blendMode"
    extern tjs_uint32 coordMemberHint_guess;               // 0x1AB8140, "coord"
    extern tjs_uint32 mtxMemberHint_guess;                 // 0x1AB84D0, "mtx"
    extern tjs_uint32 colorMemberHint_guess;               // 0x1AB8148, "color"
    extern tjs_uint32 triPriorityMemberHint_guess;         // 0x1AB84D4, "triPriority"
    extern tjs_uint32 clipRectMemberHint_guess;            // 0x1AB84D8, "clipRect"
    extern tjs_uint32 meshTransformMemberHint_guess;       // 0x1AB84DC, "meshTransform"
    extern tjs_uint32 bezierPatchMemberHint_guess;         // 0x1AB819C, "bezierPatch"
    extern tjs_uint32 compositeMeshMemberHint_guess;       // 0x1AB84EC, "compositeMesh"
    extern tjs_uint32 stencilChainMemberHint_guess;        // 0x1AB84F0, "stencilChain"
    extern tjs_uint32 patchMemberHint_guess;               // 0x1AB84C0, "patch"
    extern tjs_uint32 divisionMemberHint_guess;            // 0x1AB83EC, "division"
    extern tjs_uint32 vtxMemberHint_guess;                 // 0x1AB84E0, "vtx"
    extern tjs_uint32 divxMemberHint_guess;                // 0x1AB84E4, "divx"
    extern tjs_uint32 divyMemberHint_guess;                // 0x1AB84E8, "divy"
    extern tjs_uint32 typeMemberHint_guess;                // 0x1AB8124, "type"
    extern tjs_uint32 meshMemberHint_guess;                // 0x1AB8188, "mesh"

    // SourceCache_loadSource/bake @0x6A7BA8/@0x6A6BE0 and Player's
    // descriptor bridge @0x6C1B70. These are distinct process-wide mutable
    // hint slots in libkrkr2.so even where another call site uses the same
    // literal member name.
    extern tjs_uint32 loadSourceMemberHint_guess;  // 0x1AB8444, "loadSource"
    extern tjs_uint32 drawLayerMemberHint_guess;   // 0x1AB82C0, "drawLayer"
    extern tjs_uint32 setSizeMemberHint_guess;     // 0x1AB82C4, "setSize"
    extern tjs_uint32 copyRectMemberHint_guess;    // 0x1AB82C8, "copyRect"
    extern tjs_uint32 operateRectMemberHint_guess; // 0x1AB82CC, "operateRect"
    extern tjs_uint32 adjustGammaMemberHint_guess; // 0x1AB82D0, "adjustGamma"
    extern tjs_uint32 fillRectMemberHint_guess;    // 0x1AB8270, "fillRect"
    extern tjs_uint32 neutralColorMemberHint_guess; // 0x1AB826C, "neutralColor"
    extern tjs_uint32 windowMemberHint_guess;      // 0x1AB84A0, "window"
    extern tjs_uint32 piledCopyMemberHint_guess;   // 0x1AB84A4, "piledCopy"
    extern tjs_uint32 assignImagesMemberHint_guess; // 0x1AB844C, "assignImages"
    extern tjs_uint32 layerClassMemberHint_guess;  // 0x1AB853C, "Layer"
    extern tjs_uint32 meshCopyMemberHint_guess;    // 0x1AB8458, "meshCopy"
    extern tjs_uint32 bezierPatchCopyMemberHint_guess; // 0x1AB845C, "bezierPatchCopy"
    extern tjs_uint32 affineCopyMemberHint_guess;  // 0x1AB8460, "affineCopy"
    extern tjs_uint32 bufLayerMemberHint_guess;    // 0x1AB8468, "bufLayer"

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
