// MotionNodeLayout.h — binary-exact 2632-byte MotionNode memory layout mirror.
//
// PURPOSE: authoritative compile-checked contract for the libkrkr2.so per-node
// structure stored in the Player node deque (Player+200, stride 2632 / 0xA48).
// Standalone layout struct: nothing consumes it yet. It is the geometry the
// container-POD migration converges on, so every later step (parse/merge → the
// live MotionNode → its ~20 consumers) checks against one static_assert grid
// instead of re-deriving offsets.
//
// AUTHORITY: analysis/MotionNode_2632B_layout.md — every named offset below is
// backed by an ARM64 instruction (disasm, NOT hex-rays, which mis-attributed
// node vs slot base). Key evidence:
//   stride 2632     MADD #0xA48 @0x6c06b8 (anchor) / @0x6b73cc (advRoot)
//   childTimeline+8 LDR X8,[X20,#8]; LDR D8,[X8,#0x28] @0x6b7e80 (advNodeFrames)
//   nodeType+28     LDR W8,[X19,#0x1C] @0x699c0c (evalTL)
//   forceFlag+44    STRB W9,[X20,#0x2C] @0x6b7fbc (advNodeFrames)
//   slot[0]+320     ADD X9,X20,#0x140; MADD ...,#0x218 @0x6b7ef8
//   slot[1]+856     slot[0]+536
//   activeSlot+1392 LDRSW X22,[X19,#0x570] @0x699b14
//   anchorGate+1505 LDRB W8,[X23,#0x5E1] @0x6c06c8 (anchor)
//   findSrc+1996    LDR W8,[X20,#0x7CC] @0x6b7fec (advNodeFrames)
//   posMode+2000    LDR W9,[X19,#0x7D0]; CMP#1 @0x699be4 (evalTL)
//
// DISCIPLINE: `_unverified_*` byte gaps are NOT claimed to be padding in the
// binary — only "not yet decompiled at instruction level". They must be
// resolved to named fields before this layout is treated as fully 1:1. The
// `#pragma pack(1)` is mandatory: the binary places 8-byte pointers / doubles
// at 4-byte-aligned node offsets (e.g. childTimeline@+8 is fine, but slot ptrs
// at +36 inside the embedded ClipSlot are ≡4 mod 8), which a natural-aligned
// C++ struct cannot reproduce. sizeof asserted == 2632 so any gap math error
// fails the build.
#pragma once

#include <cstddef>
#include <cstdint>

#include "ClipSlotLayout.h"

namespace motion::detail::layout {

#pragma pack(push, 1)
    struct MotionNodeLayout {
        std::uint8_t  _unverified_0[8];      // +0    head (vtable? not decompiled)
        std::uint8_t  childTimeline[8];      // +8    ptr; *(child+0x28)=childEvalTime
        std::uint8_t  _unverified_16[12];    // +16   gap before nodeType
        std::int32_t  nodeType;              // +28   evalTL switch / 1<<nodeType
        std::uint8_t  _unverified_32[12];    // +32   gap before forceFlag
        std::uint8_t  forceFlag;             // +44   advance writes 1
        std::uint8_t  _unverified_45[7];     // +45   gap before stencil
        std::uint32_t stencilTypeFlag;       // +52   buildNodeTree &4 gate
        double        lastRatio;             // +56   crossfade ratio cache
        std::uint8_t  _unverified_64[36];    // +64   gap before color block
        std::uint32_t colorR;                // +100  eval result / anchor cursor
        std::uint32_t colorG;                // +104
        std::uint32_t colorB;                // +108
        std::uint32_t colorA;                // +112
        std::uint8_t  _unverified_116[4];    // +116  gap before matrix
        double        m00;                   // +120  transform (anchor compose)
        double        m01;                   // +128
        double        m10;                   // +136
        double        m11;                   // +144
        std::uint8_t  _unverified_152[48];   // +152  gap before imagesValid
        std::uint8_t  imagesValid;           // +200  anchor 0/1; findSource arg0
        std::uint8_t  _unverified_201[3];    // +201  gap before PSB cache
        std::uint8_t  psbDispatchCache[28];  // +204  sub_A0FB64(node+0xCC,...)
        double        sourceWidth;           // +232  anchor PSB "width"
        double        sourceHeight;          // +240  anchor PSB "height"
        double        halfHeight;            // +248  height*0.5
        double        halfWidth;             // +256  width*0.5
        std::uint8_t  cxBlock[8];            // +264  cleared by anchor
        std::uint8_t  cyBlock[8];            // +272
        std::uint8_t  identityBlock[16];     // +280  anchor writes {1.0,1.0}
        std::uint8_t  _unverified_296[24];   // +296  gap before slot[0]
        ClipSlotLayout slot0;                // +320  536B (node+346 = slot0+26)
        ClipSlotLayout slot1;                // +856  536B (node+882 = slot1+26)
        std::int32_t  activeSlotIndex;       // +1392 evalTL/advance
        std::uint8_t  _unverified_1396[109]; // +1396 gap before anchor gate
        std::uint8_t  anchorActiveGate;      // +1505 anchor: 0 -> skip node
        std::uint8_t  _unverified_1506[1];   // +1506 gap
        std::uint8_t  resultFlipX;           // +1507 <- slot fx (eval copy)
        std::uint8_t  resultFlipY;           // +1508 <- slot fy
        std::uint8_t  _unverified_1509[3];   // +1509 gap before pos
        double        resultPosA;            // +1512 <- slot+0x1A0 / anchor lerp
        double        resultPosB;            // +1520
        double        resultPosC;            // +1528
        double        resultAngle;           // +1536 eval result / anchor damp
        double        resultScaleX;          // +1544
        double        resultScaleY;          // +1552
        double        resultSlantX;          // +1560
        double        resultSlantY;          // +1568
        std::uint32_t resultOpacity;         // +1576 0..255
        std::uint8_t  _unverified_1580[381]; // +1580 gap before isLinkedChild
        std::uint8_t  isLinkedChild;         // +1961 buildNodeTree sets 1
        std::uint8_t  _unverified_1962[34];  // +1962 gap before findSource gate
        std::int32_t  findSourceGate;        // +1996 advance findSource branch
        std::uint32_t type1PosModeGate;      // +2000 ==1 -> pos interp path
        std::uint8_t  _unverified_2004[20];  // +2004 gap before pos-interp target
        std::uint8_t  posInterpTarget[200];  // +2024 sub_6996E8/sub_69AC4C dst
        double        type4Channels[8];      // +2224 type4 result channels
        double        type4ChannelLast;      // +2288 type4 last channel
        std::uint8_t  _unverified_2296[72];  // +2296 gap before type5
        double        type5Channel;          // +2368 type5 result
        std::uint8_t  _unverified_2376[56];  // +2376 gap before type10
        double        type10Channel;         // +2432 type10 / anchor damp divisor
        double        anchorOpaScale;        // +2440 anchor opacity pow scale
        std::uint8_t  _unverified_2448[24];  // +2448 gap before anchor color cache
        double        anchorColorCache;      // +2472 anchor per-channel cache base
        std::uint8_t  _unverified_tail[152]; // +2480 .. +2631
    };
#pragma pack(pop)

    // ---- offset contract: every disasm-confirmed node field ----
    static_assert(offsetof(MotionNodeLayout, childTimeline)    == 8,    "node+8");
    static_assert(offsetof(MotionNodeLayout, nodeType)         == 28,   "node+28");
    static_assert(offsetof(MotionNodeLayout, forceFlag)        == 44,   "node+44");
    static_assert(offsetof(MotionNodeLayout, stencilTypeFlag)  == 52,   "node+52");
    static_assert(offsetof(MotionNodeLayout, lastRatio)        == 56,   "node+56");
    static_assert(offsetof(MotionNodeLayout, colorR)           == 100,  "node+100");
    static_assert(offsetof(MotionNodeLayout, colorA)           == 112,  "node+112");
    static_assert(offsetof(MotionNodeLayout, m00)              == 120,  "node+120");
    static_assert(offsetof(MotionNodeLayout, m11)              == 144,  "node+144");
    static_assert(offsetof(MotionNodeLayout, imagesValid)      == 200,  "node+200");
    static_assert(offsetof(MotionNodeLayout, sourceWidth)      == 232,  "node+232");
    static_assert(offsetof(MotionNodeLayout, sourceHeight)     == 240,  "node+240");
    static_assert(offsetof(MotionNodeLayout, halfHeight)       == 248,  "node+248");
    static_assert(offsetof(MotionNodeLayout, halfWidth)        == 256,  "node+256");
    static_assert(offsetof(MotionNodeLayout, identityBlock)    == 280,  "node+280");
    static_assert(offsetof(MotionNodeLayout, slot0)            == 320,  "node+320 slot[0]");
    static_assert(offsetof(MotionNodeLayout, slot1)            == 856,  "node+856 slot[1]");
    static_assert(offsetof(MotionNodeLayout, activeSlotIndex)  == 1392, "node+1392");
    static_assert(offsetof(MotionNodeLayout, anchorActiveGate) == 1505, "node+1505");
    static_assert(offsetof(MotionNodeLayout, resultFlipX)      == 1507, "node+1507");
    static_assert(offsetof(MotionNodeLayout, resultFlipY)      == 1508, "node+1508");
    static_assert(offsetof(MotionNodeLayout, resultPosA)       == 1512, "node+1512");
    static_assert(offsetof(MotionNodeLayout, resultAngle)      == 1536, "node+1536");
    static_assert(offsetof(MotionNodeLayout, resultScaleX)     == 1544, "node+1544");
    static_assert(offsetof(MotionNodeLayout, resultScaleY)     == 1552, "node+1552");
    static_assert(offsetof(MotionNodeLayout, resultSlantX)     == 1560, "node+1560");
    static_assert(offsetof(MotionNodeLayout, resultSlantY)     == 1568, "node+1568");
    static_assert(offsetof(MotionNodeLayout, resultOpacity)    == 1576, "node+1576");
    static_assert(offsetof(MotionNodeLayout, isLinkedChild)    == 1961, "node+1961");
    static_assert(offsetof(MotionNodeLayout, findSourceGate)   == 1996, "node+1996");
    static_assert(offsetof(MotionNodeLayout, type1PosModeGate) == 2000, "node+2000");
    static_assert(offsetof(MotionNodeLayout, posInterpTarget)  == 2024, "node+2024");
    static_assert(offsetof(MotionNodeLayout, type4Channels)    == 2224, "node+2224");
    static_assert(offsetof(MotionNodeLayout, type4ChannelLast) == 2288, "node+2288");
    static_assert(offsetof(MotionNodeLayout, type5Channel)     == 2368, "node+2368");
    static_assert(offsetof(MotionNodeLayout, type10Channel)    == 2432, "node+2432");
    static_assert(offsetof(MotionNodeLayout, anchorOpaScale)   == 2440, "node+2440");
    static_assert(offsetof(MotionNodeLayout, anchorColorCache) == 2472, "node+2472");

    // ---- cross-check: the two mergedFlag bytes the binary addresses by NODE
    // absolute offset (advanceNodeFrames LDRB [node,#0x15A]/[node,#0x372]) must
    // coincide with slotN+26 inside the embedded ClipSlot. This proves the slot
    // embedding is byte-consistent with the node-level addressing. ----
    static_assert(offsetof(MotionNodeLayout, slot0) +
                  offsetof(ClipSlotLayout, mergedFlag) == 346, "node+346 = slot0+26");
    static_assert(offsetof(MotionNodeLayout, slot1) +
                  offsetof(ClipSlotLayout, mergedFlag) == 882, "node+882 = slot1+26");
    // clipStartTime the rewind cursor reads at node+slotN+328:
    static_assert(offsetof(MotionNodeLayout, slot0) +
                  offsetof(ClipSlotLayout, u320) + 8 == 648, "node+648 = slot0 clipStartTime");

    static_assert(sizeof(MotionNodeLayout) == 2632, "MotionNode must be 2632 bytes");

} // namespace motion::detail::layout
