// EmoteObjectLayout.h — binary-exact 40-byte EmoteObject memory layout mirror.
//
// PURPOSE: authoritative compile-checked contract for the libkrkr2.so
// EmoteObject — the heap object the D3DEmotePlayer holds at player+24 / +32 and
// (re)creates via D3DEmotePlayer_load / tears down via D3DEmotePlayer_create
// (the member registered under the NAME "clear"). Standalone layout struct:
// nothing consumes it yet. It is brick 3 of the object-lifecycle spine the
// frame-progression alignment converges on (ClipSlot 536B → MotionNode 2632B →
// EmoteObject 40B → clear/load lifecycle → frame-progression chain).
//
// AUTHORITY (decompile evidence, this session):
//   EmoteObject_init    @ 0x67DBE0 — allocation size & field writes:
//     *(_OWORD*)a1   = 0           → zero +0/+8     (0x67dbec)
//     *((_OWORD*)a1+1)= 0          → zero +16/+24   (0x67dbe4)
//     a1[4]          = 0           → zero +32       (0x67dbe8)
//       => object spans exactly 40 bytes (operator new(0x28) in load @0x52fec0)
//     *a1   = operator new(0xE8) via sub_6A88CC     → +0  script/KAG object
//     a1[1] = operator new(0x5D8) via EmoteEngine_ctor → +8 EmoteEngine* (1496B)
//     sub_67F0CC(a1+2, a2)         → +16.. tTJSVariant* vector populated
//   EmoteObject_destroy @ 0x67F434 — teardown confirms the same field roles:
//     a1[1] (+8):  sub_67F4B8 + operator delete     → EmoteEngine teardown
//     a1[0] (+0):  sub_6A8B94 + operator delete      → script object teardown
//     a1[2]/a1[3] (+16/+24): release each tTJSVariant*, operator delete(a1[2])
//                                                    → std::vector<tTJSVariant*>
//   D3DEmotePlayer_create @ 0x52FD84 ("clear"): destroys player+24/+32 chain.
//   D3DEmotePlayer_load   @ 0x52FDD4: same teardown front-half, then rebuilds
//                                     via operator new(0x28)+EmoteObject_init.
//
// DISCIPLINE: every offset is decompile-backed and 8-byte aligned (all pointer
// width), so unlike ClipSlot/MotionNode this struct needs NO #pragma pack — the
// natural layout already matches. sizeof asserted == 40 so any drift fails the
// build. The +16/+24/+32 triple is modeled as an explicit begin/end/cap mirror
// of the libc++ std::vector<tTJSVariant*> control block the binary uses.
#pragma once

#include <cstddef>
#include <cstdint>

namespace motion::detail::layout {

    // Opaque 8-byte pointer-width slot (raw, like ClipSlotLayout::VariantPtr).
    // The migration replaces these with the real typed pointers once the
    // EmoteObject chain is converted off the current EmotePlayer representation.
    struct EmotePtr { std::uint8_t raw[8]; };

    struct EmoteObjectLayout {
        EmotePtr scriptObject;   // +0  operator new(0xE8), sub_6A88CC / sub_6A8B94
        EmotePtr engine;         // +8  EmoteEngine* (operator new(0x5D8)=1496B)
        EmotePtr varVecBegin;    // +16 std::vector<tTJSVariant*> begin (a1[2])
        EmotePtr varVecEnd;      // +24 std::vector<tTJSVariant*> end   (a1[3])
        EmotePtr varVecCap;      // +32 std::vector<tTJSVariant*> cap   (a1[4])
    };

    // ---- offset contract: every decompile-confirmed field ----
    static_assert(offsetof(EmoteObjectLayout, scriptObject) == 0,  "EmoteObject+0 script obj");
    static_assert(offsetof(EmoteObjectLayout, engine)       == 8,  "EmoteObject+8 EmoteEngine*");
    static_assert(offsetof(EmoteObjectLayout, varVecBegin)  == 16, "EmoteObject+16 vec.begin");
    static_assert(offsetof(EmoteObjectLayout, varVecEnd)    == 24, "EmoteObject+24 vec.end");
    static_assert(offsetof(EmoteObjectLayout, varVecCap)    == 32, "EmoteObject+32 vec.cap");
    static_assert(sizeof(EmoteObjectLayout) == 40, "EmoteObject must be 40 bytes (operator new(0x28))");

    // ---- cross-check vs the engine brick: EmoteObject+8 points at the 1496B
    // EmoteEngine (operator new(0x5D8)). Asserted here as documentation of the
    // spine; the EmoteEngine layout itself is a later brick. ----
    static_assert(0x5D8 == 1496, "EmoteEngine allocation 0x5D8 == 1496 bytes");

} // namespace motion::detail::layout
