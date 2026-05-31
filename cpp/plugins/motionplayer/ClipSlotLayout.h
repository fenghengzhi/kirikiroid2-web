// ClipSlotLayout.h — binary-exact 536-byte ClipSlot memory layout mirror.
//
// PURPOSE: This is the authoritative compile-checked contract for the
// libkrkr2.so per-node parsed-frame slot (MotionNode+320 = slot[0],
// MotionNode+856 = slot[1], stride 536 / 0x218). It is a STANDALONE layout
// struct: nothing consumes it yet. It exists so every subsequent step of the
// container-POD migration (parse/merge/eval → MotionNode 2632B → consumers)
// can be checked against a single static_assert-verified offset table instead
// of re-deriving offsets ad hoc.
//
// AUTHORITY (decompile evidence, this session):
//   Player_parseFrame        @ 0x6926B4  — slot+0 frameIndex, +8 time, +16 ti,
//                                          +20 mask, +24 typeZeroFlag,
//                                          +25 interpFlag
//   Player_mergeFrameContent @ 0x692AB0  — slot+44 blendMode (STR W,#0x2C),
//                                          +72 packedColors[4] (STR Q,#0x48),
//                                          +88 opacity (STR W,#0x58)
//   Frame_resetSlot          @ 0x69260C  — variant ptrs (LDR X): +36 src(#0x24),
//                                          +168 ccc(#0xA8), +288 act(#0x120),
//                                          +296 cc(#0x128); bezier-vec begin/end
//                                          +320(#0x140)/+328(#0x148)
//   ClipSlot_536B_layout.md / MotionNode_2632B_layout.md (offset tables)
//
// DISCIPLINE: every field whose offset is backed by the decompile above is
// guarded by a static_assert(offsetof(...) == N). Regions NOT yet identified at
// instruction level are explicit `_unverified_*` byte padding — they are NOT
// claimed to be padding in the binary, only "not yet decompiled". They MUST be
// resolved (and converted to named fields) before this layout is treated as
// fully 1:1. sizeof is asserted == 536 so any gap error fails the build.
#pragma once

#include <cstddef>
#include <cstdint>

namespace motion::detail::layout {

    // 8-byte slot for a heap tTJSVariant pointer (binary stores these as raw
    // pointers released by Frame_resetSlot via tTJSVariant_Release). Modeled as
    // an opaque pointer-width slot here; the migration replaces it with the real
    // tTJSVariant* once parse/merge are converted off PSBDictionary.
    //
    // ALIGNMENT NOTE (decompile-driven): the binary places 8-byte pointers at
    // 4-byte-aligned slot offsets (src@+36, act@+288, curve ptrs@+168.. — all
    // ≡4 mod 8). ARM64 LDR/STR tolerate this; a natural C++ `void*` cannot
    // (the compiler would pad to 8-align). The struct is therefore #pragma
    // pack(1) and VariantPtr is a raw 8-byte blob accessed by memcpy, so the
    // 536-byte field grid matches node+320/+856 byte-for-byte.
    struct VariantPtr { std::uint8_t raw[8]; };

#pragma pack(push, 1)
    struct ClipSlotLayout {
        // ---- leading scalar region (+0 .. +167): all decompile-confirmed ----
        std::uint32_t frameIndex;            // +0   PF 0x6926ec
        std::uint32_t _unverified_4;         // +4   gap (not decompiled)
        double        time;                  // +8   PF 0x69277c
        std::uint32_t ti;                    // +16  PF 0x6927dc / MF 0x6930a0
        std::uint32_t mask;                  // +20  PF 0x6928e8 / MF v3[5]
        std::uint8_t  typeZeroFlag;          // +24  PF 0x6927c0
        std::uint8_t  interpFlag;            // +25  PF 0x6927c8/0x6927d4
        std::uint8_t  mergedFlag;            // +26  MF 0x692af0 / reset 0x692624
        std::uint8_t  _unverified_27;        // +27  gap
        std::uint8_t  _unverified_28[8];     // +28  gap (8B before src ptr)
        VariantPtr    src;                   // +36  reset LDR X,#0x24
        std::uint32_t blendMode;             // +44  MF STR W,#0x2C (dflt 16)
        std::uint8_t  _unverified_48[8];     // +48  gap (memset region start)
        double        ox;                    // +56  MF 0x692dc8 (mask 0x1)
        double        oy;                    // +64  MF 0x692df0 (mask 0x1)
        std::uint32_t packedColors[4];       // +72  MF STR Q,#0x48 (0xFF808080)
        std::uint32_t opacity;               // +88  MF STR W,#0x58 (dflt 255)
        std::uint32_t _unverified_92;        // +92  gap (pad before coord)
        double        coordX;                // +96  MF 0x692e3c (mask 0x2)
        double        coordY;                // +104 MF 0x692e58
        double        coordZ;                // +112 MF 0x692e74
        std::uint8_t  flipX;                 // +120 MF 0x692f7c (mask 0xC)
        std::uint8_t  flipY;                 // +121 MF 0x692f90
        std::uint8_t  _unverified_122[6];    // +122 gap before angle
        double        angle;                 // +128 MF 0x692fcc (mask 0x10)
        double        zx;                    // +136 MF 0x692ffc (mask 0x60)
        double        zy;                    // +144 MF 0x693020
        double        sx;                    // +152 MF 0x693050 (mask 0x180)
        double        sy;                    // +160 MF 0x693074

        // ---- curve-pointer region (+168 .. +287): 20-byte blocks ----
        // Each curve is a tTJSVariant ptr at the block base; the 12 trailing
        // bytes per 20B block are NOT yet decompiled (easing params?).
        VariantPtr    ccc;                   // +168 reset LDR X,#0xA8 (mask 0x800)
        std::uint8_t  _unverified_176[12];   // +176 (20B block tail)
        VariantPtr    occ;                   // +188 (mask 0x8000)
        std::uint8_t  _unverified_196[12];   // +196
        VariantPtr    acc;                   // +208 (mask 0x1000)
        std::uint8_t  _unverified_216[12];   // +216
        VariantPtr    zcc;                   // +228 (mask 0x2000)
        std::uint8_t  _unverified_236[12];   // +236
        VariantPtr    scc;                   // +248 (mask 0x4000)
        std::uint8_t  _unverified_256[12];   // +256
        VariantPtr    cp;                    // +268 (mask 0x10000 "cp")
        std::uint8_t  _unverified_276[12];   // +276

        // ---- act / cc variant ptrs ----
        VariantPtr    act;                   // +288 reset LDR X,#0x120 (mask 0x40000)
        VariantPtr    cc;                    // +296 reset LDR X,#0x128 (7th curve)
        std::uint8_t  _unverified_304[16];   // +304 gap before bezier-vec

        // ---- +320 union: bezier float-vec (begin/end/cap) OR clipStartTime ----
        // Frame_resetSlot frees a float[2] vector at +320(begin)/+328(end)/
        // +336(cap); evaluateTimeline reads +328 as double clipStartTime and
        // +336 as u32 timeModulo. A slot uses ONE interpretation at a time, so
        // these MUST overlay (union), not coexist.
        union {
            struct {
                VariantPtr bezierBegin;      // +320 reset LDR X,#0x140
                VariantPtr bezierEnd;        // +328 reset LDR X,#0x148
                VariantPtr bezierCap;        // +336
            } mesh;
            struct {
                std::uint8_t _pad320[8];     // +320
                double clipStartTime;        // +328 EV 0x699c90 / rewind +328
                std::uint32_t timeModulo;    // +336 EV
                std::uint32_t _pad340;       // +340
            } timeline;
        } u320;
        std::uint8_t  hasContentFlag;        // +344 EV 0x699b38 (!slot+344 gate)
        std::uint8_t  crossfadeFlag;         // +345 EV 0x699b60
        std::uint8_t  _unverified_346[6];    // +346 gap before result block

        // ---- +352 .. +535: type4/5/10 eval result ∪ prt/camera/anchor/feedback
        // merge sub-objects (union by nodeType). 184 bytes. Field-level breakdown
        // is partially in ClipSlot_536B_layout.md but NOT yet instruction-verified
        // here, so kept as one opaque block to lock total size at 536.
        std::uint8_t  _unverified_resultBlock[184]; // +352 .. +535
    };
#pragma pack(pop)

    // ---- offset contract: every decompile-confirmed field ----
    static_assert(offsetof(ClipSlotLayout, frameIndex)   == 0,   "slot+0");
    static_assert(offsetof(ClipSlotLayout, time)         == 8,   "slot+8");
    static_assert(offsetof(ClipSlotLayout, ti)           == 16,  "slot+16");
    static_assert(offsetof(ClipSlotLayout, mask)         == 20,  "slot+20");
    static_assert(offsetof(ClipSlotLayout, typeZeroFlag) == 24,  "slot+24");
    static_assert(offsetof(ClipSlotLayout, interpFlag)   == 25,  "slot+25");
    static_assert(offsetof(ClipSlotLayout, mergedFlag)   == 26,  "slot+26");
    static_assert(offsetof(ClipSlotLayout, src)          == 36,  "slot+36 src");
    static_assert(offsetof(ClipSlotLayout, blendMode)    == 44,  "slot+44");
    static_assert(offsetof(ClipSlotLayout, ox)           == 56,  "slot+56");
    static_assert(offsetof(ClipSlotLayout, oy)           == 64,  "slot+64");
    static_assert(offsetof(ClipSlotLayout, packedColors) == 72,  "slot+72");
    static_assert(offsetof(ClipSlotLayout, opacity)      == 88,  "slot+88");
    static_assert(offsetof(ClipSlotLayout, coordX)       == 96,  "slot+96");
    static_assert(offsetof(ClipSlotLayout, coordY)       == 104, "slot+104");
    static_assert(offsetof(ClipSlotLayout, coordZ)       == 112, "slot+112");
    static_assert(offsetof(ClipSlotLayout, flipX)        == 120, "slot+120");
    static_assert(offsetof(ClipSlotLayout, flipY)        == 121, "slot+121");
    static_assert(offsetof(ClipSlotLayout, angle)        == 128, "slot+128");
    static_assert(offsetof(ClipSlotLayout, zx)           == 136, "slot+136");
    static_assert(offsetof(ClipSlotLayout, zy)           == 144, "slot+144");
    static_assert(offsetof(ClipSlotLayout, sx)           == 152, "slot+152");
    static_assert(offsetof(ClipSlotLayout, sy)           == 160, "slot+160");
    static_assert(offsetof(ClipSlotLayout, ccc)          == 168, "slot+168");
    static_assert(offsetof(ClipSlotLayout, occ)          == 188, "slot+188");
    static_assert(offsetof(ClipSlotLayout, acc)          == 208, "slot+208");
    static_assert(offsetof(ClipSlotLayout, zcc)          == 228, "slot+228");
    static_assert(offsetof(ClipSlotLayout, scc)          == 248, "slot+248");
    static_assert(offsetof(ClipSlotLayout, cp)           == 268, "slot+268");
    static_assert(offsetof(ClipSlotLayout, act)          == 288, "slot+288");
    static_assert(offsetof(ClipSlotLayout, cc)           == 296, "slot+296");
    static_assert(offsetof(ClipSlotLayout, u320)         == 320, "slot+320 union");
    static_assert(offsetof(ClipSlotLayout, hasContentFlag) == 344, "slot+344");
    static_assert(offsetof(ClipSlotLayout, crossfadeFlag)  == 345, "slot+345");
    static_assert(sizeof(ClipSlotLayout)  == 536, "ClipSlot must be 536 bytes");

} // namespace motion::detail::layout
