// PlayerFrameStep.h — M1/P2 binary-aligned parsed-frame slot + parse/merge.
//
// SCOPE (M1 staged re-architecture, phase P2 — see
//   analysis/Player_progress_frame_stepping_M1_plan.md):
// This header ports libkrkr2.so's per-node "parsed frame slot" data structure
// and the two routines that populate it:
//   - Player_parseFrame       @ 0x6926B4  -> parseFrameLike_0x6926B4
//   - Player_mergeFrameContent @ 0x692AB0 -> mergeFrameContentLike_0x692AB0
//
// These are INDEPENDENT, UNIT-TESTABLE free functions. They are NOT wired into
// the live frame-progress path (PlayerFrameProgress.cpp / PlayerTimeline.cpp /
// PlayerUpdateLayerEval.cpp remain untouched). The goal here is a structurally
// faithful copy of the binary's raw 536-byte node slot buffer (node+320 /
// node+856 in libkrkr2.so, 536-byte stride) plus byte-exact mask gating and
// field placement, so that later phases (P4/P6) can wire it in.
//
// Field offsets in the comments below are taken verbatim from the decompiled
// field accesses in 0x6926B4 / 0x692AB0 (slot base = a1 in parseFrame, = v3 in
// mergeFrameContent, where v3 is unsigned int* so v3[N] == byte 4N and
// (double*)v3 + N == byte 8N). They are PROVENANCE annotations only — the wasm
// layout need not match the ARM64 byte stride; only field semantics/data flow
// must. The authoritative offset table lives in analysis/ClipSlot_536B_layout.md.
//
// DATA SOURCE NOTE: the binary reads motion data through iTJSDispatch2 PropGet
// dispatch wrappers (sub_662668=double, sub_6635DC=int, sub_6636D4=bool,
// sub_6695BC=array-index, sub_529524=variant-ref, sub_56C694=array count). The
// local port has no live iTJSDispatch2 motion tree; the equivalent decoded data
// lives in PSB::PSBDictionary. These Like_ functions therefore source values
// from a PSB::PSBDictionary frame/content, while preserving the binary's mask
// gates, default values, and slot offsets exactly. Variant blobs the binary
// keeps as raw tTJSVariant (act/src/dtgt/target/curve blocks) are decoded into
// their logical local form at the matching offset; see per-field comments.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace PSB {
    class PSBDictionary;
}

namespace motion {
    namespace detail {

        // ------------------------------------------------------------------
        // Binary-aligned parsed-frame slot.
        // Mirrors the 536-byte node slot at node+320 / node+856 in libkrkr2.so.
        // Frame_resetSlot @ 0x69260C zero-initialises this (defaults below match
        // the post-reset + merge-default state from 0x692C70..0x692C90).
        // ------------------------------------------------------------------
        struct ParsedFrameSlotLike_0x6926B4 {
            // +0   frameIndex      parseFrame: *(_DWORD*)a1 = a3
            std::uint32_t frameIndex = 0;
            // +8   time            parseFrame: *(double*)(a1+8) = PropGet("time")
            double time = 0.0;
            // +16  ti              merge: v3[4] (mask bit 0x...; node+25 gated)
            std::uint32_t ti = 0;
            // +20  mask            parseFrame: *(a1+20)=PropGet("mask");
            //                      merge reads v3[5]
            std::uint32_t mask = 0;
            // +24  typeZeroFlag    parseFrame: type==0 -> 1 (frame invisible).
            //                      merge early-returns when set.
            std::uint8_t typeZeroFlag = 0;
            // +25  interpFlag      parseFrame: type==2 -> 0, type==3 -> 1.
            std::uint8_t interpFlag = 0;
            // +26  mergedFlag      merge: *(result+26) = 1 (content merged).
            std::uint8_t mergedFlag = 0;
            // +28  icon            merge: independent icon key variant.
            std::string icon;
            // +36  src             merge: tTJSVariant at *(v3+9). Decoded as the
            //                      "src" string (mask via (1<<nodeType)&0x1849).
            std::string src;        // logical view of the +36 tTJSVariant
            std::vector<std::string> srcList;  // particle-node "src" array form
            // +44  blendMode       merge: v3[11], default 16, mask 0x20000 "bm".
            std::uint32_t blendMode = 16;
            // +56  ox              semantic field written by mask&1.
            double ox = 0.0;
            // +64  oy              merge: (double*)v3+8 (mask 0x1 "oy").
            double oy = 0.0;
            // +72  packedColors    merge: int32x4 at v3+18, default 0xFF808080.
            std::array<std::uint32_t, 4> packedColors{
                0xFF808080u, 0xFF808080u, 0xFF808080u, 0xFF808080u};
            // +88  opacity         merge: v3[22], default 255, mask 0x400 "opa".
            std::uint32_t opacity = 255;
            // +96/104/112 coord    merge: (double*)v3+12/13/14, mask 0x2 "coord".
            double coordX = 0.0;
            double coordY = 0.0;
            double coordZ = 0.0;
            // +120/121 flip        merge: (byte*)v3+120/121, mask 0xC "fx"/"fy".
            std::uint8_t flipX = 0;
            std::uint8_t flipY = 0;
            // +128 angle           merge: (double*)v3+16, mask 0x10 "angle".
            double angle = 0.0;
            // +136/144 zx/zy       merge: (double*)v3+17/18, mask 0x60 "zx"/"zy".
            double zx = 0.0;
            double zy = 0.0;
            // +152/160 sx/sy       merge: (double*)v3+19/20, mask 0x180 "sx"/"sy".
            double sx = 0.0;
            double sy = 0.0;
            // Curve blocks: binary stores raw 20-byte tTJSVariant at
            //   +168 ccc (v3+42, mask 0x800), +188 occ (v3+47, mask 0x8000),
            //   +208 acc (v3+52, mask 0x1000), +228 zcc (v3+57, mask 0x2000),
            //   +248 scc (v3+62, mask 0x4000), +268 cp  (v3+67, "cp").
            // We keep them as decoded double-lists (logical view) at the matching
            // logical slot; offsets are documented, not asserted (the local
            // std::vector cannot occupy the exact 20-byte variant footprint).
            std::vector<double> ccc;   // +168
            std::vector<double> occ;   // +188
            std::vector<double> acc;   // +208
            std::vector<double> zcc;   // +228
            std::vector<double> scc;   // +248
            std::vector<double> cp;    // +268
            // +288 act             parseFrame: tTJSVariant at *(a1+288),
            //                      gated by mask & 0x40000 ("act").
            std::string act;
            // +296 cc / mesh       binary keeps a tTJSVariant + raw float-pair
            //                      vector (v3+80/82/84) for mesh/bezierPatch
            //                      (mask 0x2000000). DEFERRED — see .cpp note.
            std::vector<std::pair<float, float>> bezierPatch;  // +296.. (mesh)

            // +328 clipStartTime — node-slot clip start time. NOT written by
            //   parseFrame (0x6926B4) or mergeFrameContent (0x692AB0); the binary
            //   populates it elsewhere (an init / evaluate pass outside P2-P4
            //   scope). Read by Player_rewindRootAndNodes (0x6B9A3C) per-node gate
            //   `*(double*)(node + 536*activeSlot + 328) > player+456` and by the
            //   evaluate path. Kept here so the rewind cursor logic mirrors the
            //   binary's +328 read exactly; population is DEFERRED (see
            //   PlayerFrameStepping.cpp). Defaults to slot.time so a freshly
            //   parsed slot has a sane start time for isolated unit driving.
            //   LOGICAL offset: like every member past `src` (+36), the local
            //   C++ representation does NOT occupy the binary's exact +328 byte
            //   (the std::string/std::vector members above shift the layout — see
            //   the header note); the offset is documented, not static_asserted.
            double clipStartTime = 0.0;  // node-slot +328 (externally populated)

            // ---- mask 0x80000 "motion" sub-object (merge @ 0x6938CC) ----
            int  motionFlags = 0;     // sub-mask 0x1  "flags"  (v3[86])
            int  motionDt = 0;        // sub-mask 0x2  "dt"     (v3[87])
            bool motionDocmpl = false;// sub-mask 0x4  "docmpl" (byte v3+360)
            double motionDofst = 0.0; // sub-mask 0x8  "dofst"  ((double*)v3+44)
            std::string motionDtgt;   // sub-mask 0x10 "dtgt"   (variant v3+91)
            double motionTimeOffset = 0.0; // "timeOffset"      ((double*)v3+47)

            // ---- mask 0x1000000 "model" sub-object (merge @ 0x693AE8) ----
            double modelTimeOffset = 0.0;  // (double*)v3+51 "timeOffset"
            bool   modelLoop = false;      // byte v3+384  "loop"
            int    modelDt = 0;            // v3[97]       "dt"
            std::string modelDtgt;         // variant (qword)v3+49 "dtgt"

            // ---- mask 0x100000 "prt" sub-object (merge @ 0x693C64) ----
            int    prtTrigger = 0;    // sub-mask 0x1  "trigger" (v3[104])
            double prtFmin = 10.0;    // sub-mask 0x2  "fmin"  ((double*)v3+53)
            double prtFmax = 10.0;    //               "fmax"  ((double*)v3+54)
            double prtVmin = 0.0;     // sub-mask 0x4  "vmin"  ((double*)v3+55)
            double prtVmax = 0.0;     //               "vmax"  ((double*)v3+56)
            double prtAmin = 0.0;     // sub-mask 0x8  "amin"  ((double*)v3+57)
            double prtAmax = 0.0;     //               "amax"  ((double*)v3+58)
            double prtZmin = 0.0;     // sub-mask 0x10 "zmin"  ((double*)v3+59)
            double prtZmax = 0.0;     //               "zmax"  ((double*)v3+60)
            double prtRange = 0.0;    // sub-mask 0x20 "range" ((double*)v3+61)

            // ---- mask 0x200000 "camera" sub-object (merge @ 0x693EF0) ----
            double cameraFov = 0.0;   // (double*)v3+62 "fov"
            std::string cameraTarget; // variant (qword)v3+63 "target"

            // ---- mask 0x800000 "anchor" sub-object (merge @ 0x694020) ----
            std::string anchorTarget; // variant (qword)v3+129 "target"

            // ---- mask 0x8000000 "feedback" sub-object (merge @ 0x694130) ----
            double feedbackTimespan = 0.0;  // (double*)v3+66 "timespan"
        };

        // Reset slot to defaults (Frame_resetSlot @ 0x69260C). Because the local
        // struct is a normal C++ object, assignment from a fresh instance
        // reproduces the post-reset state (all variant refs released, scalars 0,
        // curve vectors emptied).
        inline void resetSlotLike_0x69260C(ParsedFrameSlotLike_0x6926B4 &slot) {
            slot = ParsedFrameSlotLike_0x6926B4{};
        }

        // Player_parseFrame @ 0x6926B4.
        // Parses one PSB frame dict into `slot`:
        //   slot.frameIndex = frameIndex
        //   slot.time       = frame["time"]            (double)
        //   frame["type"]:  0 -> typeZeroFlag=1 (invisible), else typeZeroFlag=0
        //                   2 -> interpFlag=0, 3 -> interpFlag=1
        //   frame["content"]["mask"] -> slot.mask, then mergeFrameContent.
        //   if (mask & 0x40000): frame["content"]["act"] -> slot.act.
        void parseFrameLike_0x6926B4(ParsedFrameSlotLike_0x6926B4 &slot,
                                     const std::shared_ptr<PSB::PSBDictionary> &frame,
                                     std::uint32_t frameIndex,
                                     int nodeType);

        // Player_mergeFrameContent @ 0x692AB0.
        // Applies the content dict's mask-gated fields onto `slot`.
        // a2 == nodeType (the source-gate test uses (1<<nodeType)&0x1849).
        void mergeFrameContentLike_0x692AB0(ParsedFrameSlotLike_0x6926B4 &slot,
                                            int nodeType,
                                            const std::shared_ptr<PSB::PSBDictionary> &content);

    } // namespace detail
} // namespace motion
