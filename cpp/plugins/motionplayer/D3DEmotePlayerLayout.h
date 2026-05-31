// D3DEmotePlayerLayout.h — binary-exact 56-byte D3DEmotePlayer native instance.
//
// PURPOSE: authoritative compile-checked contract for the libkrkr2.so
// D3DEmotePlayer native instance — the NCB-bound shell object that owns the
// EmoteObject chain. Standalone layout struct: nothing consumes it yet. Brick 4
// of the object-lifecycle spine (ClipSlot 536B → MotionNode 2632B →
// EmoteObject 40B → D3DEmotePlayer 56B → clear/load lifecycle → frame chain).
//
// AUTHORITY (decompile evidence, this session):
//   sub_52FFBC @ 0x52FFBC ("clone" callback = the true instance constructor):
//     v4 = operator new(0x38)           → instance is 56 bytes            0x52ffe8
//     v4+0  = off_19FE050 (vtable)                                        0x52fff4
//     v4+8  = a2 (parent dispatch)                                        0x52fff4
//     v4+16 = 0xBF00000000000008 (packed type-tag, low32=8)               0x52fff8
//     v4+24 = 0  (primary EmoteObject* slot, cleared)                     0x530024
//     v4+32 = 0  (secondary EmoteObject* slot, cleared)                   0x530024
//     v4+40 = 0x3F8000003F800000 (float 1.0f, 1.0f = scaleX/scaleY)       0x530028
//     v4+48 = 0 (word flags)                                              0x530030
//     v4+0  = off_19FE020 (vtable re-stamp to active)                     0x53002c
//     v4+24 = sub_67F978(...)  → primary slot = new EmoteObject (ONLY +24)0x53003c
//   sub_533C00 @ 0x533C00 (instance destructor, stamps vtable off_19FE050):
//     destroys a1[4] (+32) then a1[3] (+24), each EmoteObject_destroy+delete,
//     then nulls +24/+32 — proving BOTH slots are real EmoteObject* fields.
//   D3DEmotePlayer_load   @ 0x52FDD4: rebuilds ONLY +24 (operator new(0x28)).
//   D3DEmotePlayer_create @ 0x52FD84 ("clear"): tears down +24/+32, nulls both.
//
// +32 RESOLUTION (the open question that gated brick 4): the secondary slot is
// a REAL field (the destructor frees it) but across EVERY decompiled object-
// lifecycle path — construct (clone), load, reset (clear), destroy — it is
// only ever written ZERO. No decompiled create/load/clone path writes +32 a
// non-null EmoteObject. So for the logo cases it is a binary-reserved-but-
// inactive secondary slot (degenerate single-instance). This contract models
// it as a real slot defaulting to null, NOT as removed and NOT as eagerly
// allocated. (Scope honesty: proven for all lifecycle-spine entry points; a
// hidden writer off the spine is not excluded but would not be on this chain.)
//
// DISCIPLINE: all offsets decompile-backed. The two EmoteObject* are 8-byte
// aligned; scaleX/scaleY are 4-byte floats at +40/+44 (the 0x3F800000 halves);
// flags is a 2-byte word at +48. sizeof asserted == 56 so any drift fails the
// build. No #pragma pack needed — natural alignment matches (all fields land on
// their natural boundaries within the 56-byte object).
#pragma once

#include <cstddef>
#include <cstdint>

namespace motion::detail::layout {

    struct DispatchPtr { std::uint8_t raw[8]; };       // iTJSDispatch2* / vtable
    struct EmoteObjectPtr { std::uint8_t raw[8]; };    // EmoteObject* (40B target)

    struct D3DEmotePlayerLayout {
        DispatchPtr    vtable;          // +0  off_19FE020(active)/off_19FE050(base)
        DispatchPtr    parentDispatch;  // +8  a2 parent dispatch (sub_52FFBC)
        std::uint64_t  packedTypeTag;   // +16 0xBF00000000000008 (low32 = tag 8)
        EmoteObjectPtr primarySlot;     // +24 EmoteObject* — load/clone write, clear tears
        EmoteObjectPtr secondarySlot;   // +32 EmoteObject* — reserved, lifecycle-spine null
        float          scaleX;          // +40 0x3F800000 = 1.0f
        float          scaleY;          // +44 0x3F800000 = 1.0f
        std::uint16_t  flags;           // +48 word, init 0
        std::uint8_t   _unverified_50[6]; // +50 tail pad to 56 (not decompiled)
    };

    // ---- offset contract: every decompile-confirmed field ----
    static_assert(offsetof(D3DEmotePlayerLayout, vtable)         == 0,  "inst+0 vtable");
    static_assert(offsetof(D3DEmotePlayerLayout, parentDispatch) == 8,  "inst+8 parent");
    static_assert(offsetof(D3DEmotePlayerLayout, packedTypeTag)  == 16, "inst+16 type-tag");
    static_assert(offsetof(D3DEmotePlayerLayout, primarySlot)    == 24, "inst+24 EmoteObject* primary");
    static_assert(offsetof(D3DEmotePlayerLayout, secondarySlot)  == 32, "inst+32 EmoteObject* secondary");
    static_assert(offsetof(D3DEmotePlayerLayout, scaleX)         == 40, "inst+40 scaleX 1.0f");
    static_assert(offsetof(D3DEmotePlayerLayout, scaleY)         == 44, "inst+44 scaleY 1.0f");
    static_assert(offsetof(D3DEmotePlayerLayout, flags)          == 48, "inst+48 flags");
    static_assert(sizeof(D3DEmotePlayerLayout) == 56, "D3DEmotePlayer must be 56 bytes (operator new(0x38))");

    // ---- value contract: the packed constants the constructor stores, decoded
    // from the exact byte immediates in sub_52FFBC (documentation of intent). ----
    static_assert((std::uint32_t)(0xBF00000000000008ULL & 0xFFFFFFFF) == 8,
                  "packedTypeTag low32 == type tag 8 (sub_52FFBC 0x52fff8)");
    static_assert((std::uint32_t)(0x3F8000003F800000ULL & 0xFFFFFFFF) == 0x3F800000,
                  "scale halves == float 1.0f (sub_52FFBC 0x530028)");

} // namespace motion::detail::layout
