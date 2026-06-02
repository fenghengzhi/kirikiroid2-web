// EmoteEyebrowController — 0x150=336B controller for the emote "eyebrow"
//   category (deque#5, TYPE 5). Aligned with libkrkr2.so:
//     ctor : EmoteBlinkController_ctor_slim_guess @ 0x66480C
//     step : sub_665600 (EmoteVarController5_step) @ 0x665600
//
// This is the "slim" sibling of EmoteBlinkController (eye, 0x170). It embeds
// the SAME value-track infrastructure as the eye controller (the 12B-elem
// EmoteAngleController-style track @+0, the 8B-elem track @+80, the "edge"
// table @+160 and the "node" value-row pool @+184) plus the value-track scalar
// state machine (+288..+324) and a SINGLE blink scalar field (+328 beginFrame).
//
// CRITICAL DIFFERENCE vs EmoteBlinkController (eye, 0x170) — verified by fresh
// decompile of both ctors/steps (this conversation):
//   * The slim ctor (0x66480C) reads ONLY "beginFrame" from the PSB dict (plus
//     the "edge" / "node" arrays). It does NOT read endFrame / blinkIntervalMin
//     / blinkIntervalMax / blinkFrameCount / blinkEnabled, and does NOT call the
//     RNG (sub_9F1A08/sub_9F17D0). So the eyebrow controller has NO blink state
//     machine and NO blink fields (+332..+360 simply do not exist; the object is
//     0x20=32B smaller than the eye's 0x170).
//   * The slim step (sub_665600) runs the value-track machine (states 0/1/2)
//     then writes `*out = trackValue` DIRECTLY — there is NO blink-phase switch
//     and NO final [beginFrame,endFrame] blink-remap (the eye step's LABEL_28
//     blink machine + remap are absent here). beginFrame (+328) is read by the
//     ctor only to seed trackValue (+300); the eyebrow step never reads +328.
//
//   Because the value-track region of the slim object uses DIFFERENT byte
//   offsets than the eye object (the binary swaps accum<->span at +312/+316 and
//   pow<->invDur at +320/+324 relative to the eye controller), and because the
//   blink fields genuinely do not exist, this is a SEPARATE class with its own
//   named fields rather than a shared base — faithful to the binary's actual
//   distinct controller object, not a forced merge nor a needless duplicate.
//
// Field offset table (verified by decompiling EmoteBlinkController_ctor_slim
// @0x66480C + step @0x665600; offsets are libkrkr2.so ARM64 byte offsets, kept
// as provenance comments only — wasm layout need not match per CLAUDE.md byte-
// layout methodology):
//   +0..+79    EmoteAngleController-style 12B-elem value track (ctor: 0x664858
//              EmoteAngleController_ctor_12Bdeque). Stepped via cursor @+16 in
//              sub_665600 (reads {int@+0, float@+4, int@+8}; 504-block).
//   +80..+159  second value track, 8B-elem (ctor: 0x66487c sub_6827A8).
//              Stepped via cursor @+96 (reads 2 floats; 512-block).
//   +160..+167 std::vector<std::pair<float,float>> edgeTable  (the "edge" PSB
//              array: each elem -> {sub_6637BC(idx,0), sub_6637BC(idx,1)} pair).
//   +168/+176  edgeTable cursor (vector end/cap second-cursor — modelled inside
//              the std::vector).
//   +184..+287 std::deque of node-value rows (504-block; ctor 0x6648a4
//              sub_6828FC). Each row is a std::vector<float> built from the
//              "node" PSB sub-array via sub_6637BC.
//   +264/+272/+280 zeroed by ctor (0x6648a8/0x6648ac) — node-pool cursors.
//   +288       float   trackResolvedSpan — WRITTEN by the mesh resolver
//              sub_661F7C (SCOPE BOUNDARY). Read at track-setup (0x665680) into
//              trackSpan(+316). Until the resolver vertical is ported this stays
//              0 (its ctor value).
//   +296       int32_t trackState   (0 idle / 1 pop-pending / 2 animating)
//   +300       float   trackValue   (ctor seeds = (float)beginFrame; step writes
//                                    this to *out unmodified)
//   +304       float   trackTarget
//   +308       float   trackDir     (+1/-1 ramp direction)
//   +312       float   trackAccum   (NOTE: eye uses +316 for accum — swapped)
//   +316       float   trackSpan    (NOTE: eye uses +312 for span — swapped)
//   +320       float   trackPow     (raw float bits; eye uses +324 for pow — swapped)
//   +324       float   trackInvDur  (NOTE: eye uses +320 for invDur — swapped)
//   +328       int32_t beginFrame   (PSB "beginFrame", int; ctor-only read)
//
// PLATFORM_BOUNDARY: sizeof(EmoteEyebrowController) on Web will not equal 336B
//   (libc++ deque/vector headers differ from libstdc++). Offsets above are for
//   traceability; the logical contract is field semantics + element types +
//   lifetime, not byte equality.
//
// SCOPE BOUNDARY (sub_661F7C / sub_660028): identical to the eye slice. When a
//   12B-track keyframe is popped, sub_665600 calls sub_661F7C(+160,+80,
//   trackValue, endVal) — the 1925-line edge-table-driven node-value-row mesh
//   resolver. That resolver is a SEPARATE large vertical (shared with the eye
//   controller) and is NOT ported here; the call site is kept as a documented
//   anchor. While the 12B track is empty (the common case until the resolver
//   vertical lands) the binary skips that branch and trackValue retains its
//   ctor value (= beginFrame), so the value-track machine + scalar output still
//   run faithfully. This call is INERT in the local port (same boundary the eye
//   slice already documents).
//
#pragma once

#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

#include "tjs.h"
#include "EmoteAngleController.h"

namespace PSB {
    class PSBDictionary;
} // namespace PSB

namespace motion {

    // 0x150=336B slim eyebrow controller. Plain C++ object (no vtable: ctor +0
    // writes a std::deque header, not a vptr — matches EmoteVarController/
    // AngleController/EmoteBlinkController).
    struct EmoteEyebrowController {
        // +0..+79  — 12B-elem value track (EmoteAngleController-style).
        EmoteAngleController valueTrack12B; // ctor 0x664858

        // +80..+159 — 8B-elem value track (sub_6827A8). Element = {float a, b}.
        // Stepped via cursor @+96 (2 floats/elem, 512-block).
        std::deque<std::pair<float, float>> valueTrack8B; // ctor 0x66487c

        // +160..+167 — "edge" table: each PSB "edge" elem -> {x,y} float pair.
        std::vector<std::pair<float, float>> edgeTable; // ctor edge loop 0x6649e4

        // +184..+287 — "node" value-row pool: each PSB "node" elem -> a row of
        // floats (504-block deque). Consumed by sub_660028 (mesh resolver).
        std::deque<std::vector<float>> nodeRows; // ctor 0x6648a4 (sub_6828FC)

        // +288 — resolved curve span, WRITTEN by the mesh resolver sub_661F7C
        //   (SCOPE BOUNDARY). Read at track-setup (0x665680) into trackSpan(+316).
        //   Until the resolver vertical is ported this stays 0 (its ctor value).
        float   trackResolvedSpan = 0.0f; // +288

        // +296..+324 — value-track animation state. NOTE the offset assignment
        //   differs from EmoteBlinkController (eye): the slim controller swaps
        //   accum<->span (+312/+316) and pow<->invDur (+320/+324). Semantics are
        //   identical; named fields here track semantics not offsets.
        int32_t trackState  = 0;   // +296
        float   trackValue  = 0.0f;// +300 (ctor = (float)beginFrame)
        float   trackTarget = 0.0f;// +304
        float   trackDir    = 0.0f;// +308
        float   trackAccum  = 0.0f;// +312 (eye: +316)
        float   trackSpan   = 0.0f;// +316 (eye: +312)
        float   trackPow    = 0.0f;// +320 (eye: +324) — raw float bits, read *(float*)
        float   trackInvDur = 0.0f;// +324 (eye: +320)

        // +328 — PSB "beginFrame" (int). Read by the ctor only (to seed
        //   trackValue); the eyebrow step never reads this. There are NO blink
        //   fields after this (the slim object ends at +336, 0x20 smaller than
        //   the eye controller).
        int32_t beginFrame  = 0;   // +328
    };

    // Aligned with libkrkr2.so EmoteBlinkController_ctor_slim_guess @ 0x66480C.
    //   Reads beginFrame from the PSB dict, seeds trackValue=(float)beginFrame,
    //   then populates the "edge" table and "node" value-rows. NO blink-field
    //   reads and NO RNG call (unlike the eye controller ctor).
    void EmoteEyebrowController_ctor(EmoteEyebrowController* self,
                                     const PSB::PSBDictionary* dict);

    // Aligned with libkrkr2.so sub_665600 EmoteVarController5_step @ 0x665600.
    //   Advances the value track (when non-empty) through states 0/1/2, then
    //   writes the track value to *out DIRECTLY (no blink machine, no remap).
    void EmoteEyebrowController_step(EmoteEyebrowController* self, float* out,
                                     float dt);

} // namespace motion
