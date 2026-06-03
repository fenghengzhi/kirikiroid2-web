// EmoteBlinkController — 0x170=368B controller for the emote "eye" category
//   (deque#4, TYPE 4). Aligned with libkrkr2.so:
//     ctor : EmoteBlinkController_ctor  @ 0x662968
//     step : sub_663BDC (EmoteVarController4_step) @ 0x663BDC
//
// This is the controller model + step for the eye/blink vertical slice. The
// object embeds TWO value-track deques (the EmoteAngleController-style 12B
// track @+0 and a 8B/512-block track @+80), an "edge" table (vector<{f,f}>
// @+160) and a "node" value-row pool (504-block deque @+184), PLUS the blink
// scalar state machine (+296..+360). The blink state machine IS the eye-blink
// behaviour and is fully ported in sub_663BDC.
//
// Field offset table (verified by decompiling EmoteBlinkController_ctor
// @0x662968 + step @0x663BDC; offsets are libkrkr2.so ARM64 byte offsets, kept
// as provenance comments only — wasm layout need not match per CLAUDE.md byte-
// layout methodology):
//   +0..+79    EmoteAngleController-style 12B-elem value track (ctor: 0x6629b8
//              EmoteAngleController_ctor_12Bdeque). Stepped via cursor @+16 in
//              sub_663BDC (reads {int@+0, float@+4, int@+8}; 504-block).
//   +80..+159  second value track, 8B-elem (ctor: 0x6629dc sub_6827A8).
//              Stepped via cursor @+96 (reads 2 floats; 512-block).
//   +160..+167 std::vector<std::pair<float,float>> edgeTable  (the "edge" PSB
//              array: each elem -> {sub_6637BC(idx,0), sub_6637BC(idx,1)} pair).
//   +168/+176  edgeTable cursor (vector end/cap second-cursor — modelled inside
//              the std::vector).
//   +184..+295 std::deque of node-value rows (504-block; ctor 0x662a04
//              sub_6828FC). Each row is a std::vector<float> built from the
//              "node" PSB sub-array via sub_6637BC.
//   +296       int32_t trackState   (0 idle / 1 pop-pending / 2 animating)
//   +300       float   trackValue   (eye-open position; ctor seeds = beginFrame)
//   +304       float   trackTarget
//   +308       float   trackDir     (+1/-1 ramp direction)
//   +312       float   trackSpan
//   +316       float   trackAccum
//   +320       float   trackInvDur
//   +324       float   trackPow (raw float bits from keyframe[+8], read *(float*))
//   +328       int32_t beginFrame   (PSB "beginFrame", int)
//   +332       int32_t endFrame     (PSB "endFrame", int)
//   +340       float   blinkIntervalMin (PSB "blinkIntervalMin", double->float)
//   +344       float   blinkIntervalMax (PSB "blinkIntervalMax", double->float)
//   +348       float   blinkFrameCount  (PSB "blinkFrameCount",  double->float)
//   +352       float   blinkTimer    (countdown; phase 0/10/11/12 timer)
//   +356       float   blinkPos      (current blink position; ctor = beginFrame)
//   +360       uint8_t blinkEnabled  (PSB "blinkEnabled", bool)
//   +336       int32_t blinkPhase    (state: 0 wait, 10 closing, 11 hold, 12 opening)
//
// PLATFORM_BOUNDARY: sizeof(EmoteBlinkController) on Web will not equal 368B
//   (libc++ deque/vector headers differ from libstdc++). Offsets above are for
//   traceability; the logical contract is field semantics + element types +
//   lifetime, not byte equality.
//
// SCOPE BOUNDARY (sub_661F7C / sub_660028): the value-track stepping in
//   sub_663BDC, when a 12B-track keyframe is popped, calls sub_661F7C(+160,+80,
//   trackValue,..) which dispatches into sub_660028 — a 1925-line edge-table-
//   driven node-value-row interpolation engine that resolves the eye mesh from
//   edgeTable + nodeRows. That mesh-row resolver is a SEPARATE large vertical
//   (its own controller-independent infrastructure) and is NOT ported here; the
//   call site is kept as a documented anchor. When the 12B value track is empty
//   (the common case until the resolver vertical is ported), the binary skips
//   that branch and trackValue retains its ctor value (= beginFrame), so the
//   blink state machine + final remap still run faithfully.
//
#pragma once

#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

#include "tjs.h"
#include "EmoteAngleController.h"
#include "EmoteMeshResolver.h"

namespace PSB {
    class PSBDictionary;
} // namespace PSB

namespace motion {

    // 0x170=368B blink controller. Plain C++ object (no vtable: ctor +0 writes a
    // std::deque header, not a vptr — matches EmoteVarController/AngleController).
    struct EmoteBlinkController {
        // +0..+79  — 12B-elem value track (EmoteAngleController-style).
        EmoteAngleController valueTrack12B; // ctor 0x6629b8

        // +80..+159 — 8B-elem value track (sub_6827A8). Element = {float a, b}.
        // Stepped via cursor @+96 (2 floats/elem, 512-block).
        std::deque<std::pair<float, float>> valueTrack8B; // ctor 0x6629dc

        // +160..+295 — mesh-resolver state embedded in the controller:
        //   +160 edgeTable (vector<{lo,hi}>), +184 nodeRows (deque<vector<float>>),
        //   +264 outputRows (vector<MeshPathRow>), +288 trackResolvedSpan.
        //   Built in the ctor; consumed/written by EmoteMeshResolver_resolve
        //   (sub_661F7C -> sub_660028).
        EmoteMeshResolverState mesh;

        // +288 alias: trackResolvedSpan lives inside `mesh` (mesh.trackResolvedSpan
        //   is written by sub_661F7C; the step reads it into trackSpan(+312)).

        // +296..+324 — value-track animation state (mirrors EmoteVarController).
        int32_t trackState  = 0;   // +296
        float   trackValue  = 0.0f;// +300 (ctor = (float)beginFrame)
        float   trackTarget = 0.0f;// +304
        float   trackDir    = 0.0f;// +308
        float   trackSpan   = 0.0f;// +312
        float   trackAccum  = 0.0f;// +316
        float   trackInvDur = 0.0f;// +320
        float   trackPow    = 0.0f;// +324 — raw float bits (read *(float*), not int)

        // +328..+360 — blink state machine.
        int32_t beginFrame  = 0;   // +328  (PSB "beginFrame")
        int32_t endFrame    = 0;   // +332  (PSB "endFrame")
        int32_t blinkPhase  = 0;   // +336  (0 wait / 10 closing / 11 hold / 12 opening)
        float   blinkIntervalMin = 0.0f; // +340
        float   blinkIntervalMax = 0.0f; // +344
        float   blinkFrameCount  = 0.0f; // +348
        float   blinkTimer  = 0.0f;// +352
        float   blinkPos    = 0.0f;// +356  (ctor = (float)beginFrame)
        uint8_t blinkEnabled = 0;  // +360  (PSB "blinkEnabled")
    };

    // Aligned with libkrkr2.so EmoteBlinkController_ctor @ 0x662968.
    //   Reads the blink scalar fields from the PSB dict (beginFrame/endFrame/
    //   blinkIntervalMin/Max/blinkFrameCount/blinkEnabled), seeds
    //   trackValue=blinkPos=(float)beginFrame and nextBlink=min+(max-min)*rand,
    //   then populates the "edge" table and "node" value-rows.
    void EmoteBlinkController_ctor(EmoteBlinkController* self,
                                   const PSB::PSBDictionary* dict);

    // Aligned with libkrkr2.so sub_663BDC EmoteVarController4_step @ 0x663BDC.
    //   Advances the value track (when non-empty), runs the blink state machine
    //   (phases 0/10/11/12), then remaps the track value by blink progress and
    //   writes the scalar result to *out.
    void EmoteBlinkController_step(EmoteBlinkController* self, float* out,
                                   float dt);

} // namespace motion
