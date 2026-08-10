// Value structs for motion::Player's embedded std::unordered_map and std::deque
// instances. Each struct corresponds to a libkrkr2.so value type with its
// reverse-engineered field set and lifetime semantics; destructors match the
// binary's deallocation order so refcount and heap accounting stay aligned.
//
// PLATFORM_BOUNDARY: libkrkr2.so was built against Android libstdc++ where
// ttstr is 16B and std::vector is 24B. The Web build uses libc++ where ttstr
// is 8B and std::vector remains 24B; consequently sizeof(EvalCascadeState),
// sizeof(VariableLabelScope) etc. cannot match the binary's 72B / 160B byte
// counts. Logical 1:1 — same semantic fields, same dispatch refcount sequence,
// same heap ownership, same destructor order — is the contract we uphold.
//
// References:
//   .claude/agent-memory/ida-deep-analyzer/player_value_structs_spec.md
//   .claude/agent-memory/ida-deep-analyzer/player_containers_libstdcxx_spec.md
//
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "../MeshPoint.h"
#include "tjs.h"
#include "tjsInterface.h"
#include "tjsString.h"

namespace motion::detail {

    // RAII wrapper around an owned iTJSDispatch2* that releases on destruction
    // and nullifies the source on move. Composing a value struct out of these
    // gives us automatic destructor ordering (reverse declaration order, which
    // when fields are declared in ascending binary offset becomes descending
    // binary offset on destruction — matching the libkrkr2.so dtor sequence).
    struct DispatchRef {
        iTJSDispatch2 *p = nullptr;

        DispatchRef() = default;
        explicit DispatchRef(iTJSDispatch2 *raw) noexcept : p(raw) {}
        DispatchRef(const DispatchRef &) = delete;
        DispatchRef &operator=(const DispatchRef &) = delete;
        DispatchRef(DispatchRef &&other) noexcept : p(other.p) {
            other.p = nullptr;
        }
        DispatchRef &operator=(DispatchRef &&other) noexcept {
            if (this != &other) {
                if (p) {
                    p->Release();
                }
                p = other.p;
                other.p = nullptr;
            }
            return *this;
        }
        ~DispatchRef() {
            if (p) {
                p->Release();
                p = nullptr;
            }
        }

        [[nodiscard]] iTJSDispatch2 *get() const noexcept { return p; }
        [[nodiscard]] explicit operator bool() const noexcept {
            return p != nullptr;
        }

        void reset(iTJSDispatch2 *raw = nullptr) noexcept {
            if (p) {
                p->Release();
            }
            p = raw;
        }
    };

    // RAII wrapper around an owned raw heap pointer freed by ::operator delete.
    // libkrkr2.so allocates several PerNodeLayerState sub-blocks via
    // operator new and frees them in the value destructor; this mirrors that
    // ownership precisely without relying on placement-new + manual cleanup.
    struct HeapRef {
        void *p = nullptr;

        HeapRef() = default;
        explicit HeapRef(void *raw) noexcept : p(raw) {}
        HeapRef(const HeapRef &) = delete;
        HeapRef &operator=(const HeapRef &) = delete;
        HeapRef(HeapRef &&other) noexcept : p(other.p) { other.p = nullptr; }
        HeapRef &operator=(HeapRef &&other) noexcept {
            if (this != &other) {
                if (p) {
                    ::operator delete(p);
                }
                p = other.p;
                other.p = nullptr;
            }
            return *this;
        }
        ~HeapRef() {
            if (p) {
                ::operator delete(p);
                p = nullptr;
            }
        }

        [[nodiscard]] void *get() const noexcept { return p; }
        [[nodiscard]] explicit operator bool() const noexcept {
            return p != nullptr;
        }

        void reset(void *raw = nullptr) noexcept {
            if (p) {
                ::operator delete(p);
            }
            p = raw;
        }
    };

    // Forward decl — heapResult holds non-owning MotionNode* into Player's
    // _nodes deque (binary stride-2632 node pointers, see sub_6B9650).
    struct MotionNode;

    // EvalCascadeState — libkrkr2.so HM1 (Player+264) value type.
    //
    // Cached result of "::"-cascade-joined PropGet lookup chains. Reverse-
    // engineered from Player_HM1_upsert_evalCascade @0x6F52AC, the writer
    // Player_bindParameterValue_writesHM1_HM2 @0x6C4668 (v30 = value base) and
    // Player_HM1_value_destroy @0x6DD1A0. Binary V (value base = node+16) layout:
    //   V+0  ttstr key copy (the joinedKey)                     (store @0x6c4880)
    //   V+8/+16/+24 std::vector<tTJSVariant*> chainSegments     (set @0x6c48c8..)
    //              = the scope label split into "::"-segments
    //              @0x6c48bc; elements are tTJSVariant<string> (compared by
    //              type tag +0x3C + ttstr_c_str/wcscmp inside sub_6B9650).
    //   V+32 double writeVal  (= a4, store @0x6c4968 every bind)
    //   V+40 double weight     (= 1.0, seeded once @0x6c4964 on first insert)
    //   V+48/+56/+64 std::vector<MotionNode*> heapResult        (set @0x6b9778)
    //              = type3/4 nodes whose ancestor-label-chain == chainSegments;
    //              rebuilt by sub_6B9650 @0x6c4974 every bind.
    //
    // Binary dtor sequence (@0x6DD1A0): heapResult backing delete → chainSegments
    // element Release + backing delete → key release. heapResult holds NON-owning
    // node pointers (no per-element Release), so only the backing is freed.
    //
    // PORT NOTE: chainSegments is modeled as a vector<ttstr> of the scope split
    // segments (the binary stores tTJSVariant<string>; we keep the string value
    // which is all sub_6B9650's wcscmp dedup reads). heapResult is a
    // vector<MotionNode*> (faithful element type; non-owning into _nodes deque).
    struct EvalCascadeState {
        ttstr keyCopy;
        std::vector<ttstr> chainSegments;
        // Binary V+32 / V+40. writeVal := a4 on every bind; weight is seeded to
        // 1.0 only on first insert and left untouched on later upserts.
        double writeVal = 0.0;
        double weight = 0.0;
        std::vector<MotionNode *> heapResult;

        EvalCascadeState() = default;
        EvalCascadeState(const EvalCascadeState &) = delete;
        EvalCascadeState &operator=(const EvalCascadeState &) = delete;

        EvalCascadeState(EvalCascadeState &&other) noexcept = default;
        EvalCascadeState &operator=(EvalCascadeState &&other) noexcept = default;
        // ~EvalCascadeState: vector<ttstr> releases each ttstr (mirrors binary
        // chainSegments element Release); vector<MotionNode*> just frees backing
        // (non-owning, matches binary heapResult dtor — no per-node Release);
        // ~ttstr handles keyCopy. Declaration order = ascending binary offset, so
        // reverse-order member destruction matches the binary's V+48→V+8→V+0.
        ~EvalCascadeState() = default;
    };

    // VarTrackSlot — libkrkr2.so 56B per-track frame slot. Two of these live
    // inside each VariableLabelScope (binary item+48 / item+104, stride 56B) and
    // are the var-track analog of the node's ParsedFrameSlotLike_0x6926B4 (536B).
    // Filled by the var-track advance stream ③ inside Player_advanceRootAndNodes
    // @0x6B6ADC: Motion_VarTrackSlot_step_guess@0x6B786C then
    // Motion_VarTrackSlot_merge_guess@0x6B7A70 (merge —
    // type→flags + interval/value/easing). Field offsets byte-verified from those
    // two functions (slot base = a1):
    //   0x6B786C: a1[0]=frameIdx; a1+8=frame["time"]; a1+22=0.
    //   0x6B7A70: a1+22=1; type=frame["type"]; type==0→a1+20=1 (early return,
    //     loop2 HM4 gate); else a1+20=0, a1+21=(type==3), a1[4]=content["interval"],
    //     *((double*)a1+3)=content["value"], a1+32=frame["easing"].
    struct VarTrackSlot {
        std::uint32_t frameIndex = 0;  // +0  step: *(_DWORD*)a1 = frameIdx
        double time = 0.0;             // +8  step: frame["time"]
        std::uint32_t interval = 0;    // +16 merge: a1[4] = frame["interval"]
        // +20 typeZeroFlag — merge: type==0?1:0. == the loop2 HM4-write gate
        //   (`!*(BYTE)(item + 56*cursor + 68)` reads this byte). step does NOT
        //   touch it; Player_initVariables seeds it =1 (item+68/+124).
        bool typeZeroFlag = true;
        std::uint8_t interpFlag = 0;   // +21 merge: type==2→0, type==3→1
        bool merged = false;           // +22 step→0, merge→1 (advance merge-gate)
        double value = 0.0;            // +24 merge: *((double*)a1+3) = frame["value"]
        // +32 easing — merge: frame["easing"] copied as a tTJSVariant. A bezier
        // control-point dispatch {x:[...], y:[...]} consumed by
        // Player_applyBezierEasing @0x69A754. Its embedded variant type tag is
        // the binary's slot+48 presence gate.
        tTJSVariant easing;
    };

    // VariableLabelScope — libkrkr2.so Player+1296 std::deque element (160B).
    //
    // Populated by Player_initVariables @0x6CD750 while loading a motion, one
    // per entry of the motion's "variable" PSB array. Snapshotted into HM4
    // (Player+1240) by Player_resetMotionState_clearAndRebuild loop2 @0x6B2D3C:
    // key = cascadeKey (item+0), value = value (item+16), gated on the active
    // slot's gateFlag. Read on the lookup side by Player_evalKey_cascade
    // @0x6CD23C (HM4-first, keyed by the raw lookup ttstr == cascadeKey).
    //
    // Field offsets byte-verified from the deque push path 0x6CD940..0x6CDBB4
    // and the stream③ reader (advanceRootAndNodes 0x6B6ADC var-track loop):
    //   +0  cascadeKey   ttstr  (scope present ? scope+"::"+label : label;
    //                            ttstr_c_str(entry["label"]) form — HM4/HM1 key)
    //   +8  cursor       int    (active-slot parity, selects slot[0]/slot[1])
    //   +16 value        double (current variable value; HM4 reads it; interpolated)
    //   +24 frameSource  the entry["label"] value — the var's keyframe list, the
    //                    analog of node+64 "frameList". stream③ does
    //                    AsObject(item+24).PropGetByNum(i) on it; binary stores the
    //                    SAME entry["label"] at item+0 (key) and item+24 (frames).
    //   +48 slot[0]      56B    (VarTrackSlot)
    //   +104 slot[1]     56B    (VarTrackSlot)
    // (Local sizeof differs by ttstr/tTJSVariant sizeof — PLATFORM_BOUNDARY — but
    // field order / container selection match the binary.)
    //
    // NOT a controller animator — those are EmotePlayer's 5 deques at +256
    // / +336 / +416 / +576 / +656 with 16-48B elements; this is a lookup table
    // populated once per motion load and read by the cascade evaluator.
    struct VariableLabelScope {
        ttstr cascadeKey;            // item+0  — HM4/HM1 key
        int activeSlotCursor = 0;    // item+8  — parity cursor
        double value = 0.0;          // item+16 — HM4 value (interpolated)
        // item+24 — entry["label"] raw tTJSVariant, iterated through
        // PropGetByNum by the var-track advance (stream③).
        tTJSVariant frameSource;
        VarTrackSlot slot[2];        // item+48 / item+104
    };

    // PerNodeLayerState — libkrkr2.so HM3 (Player+1184) value type.
    //
    // Per-node-path layer state snapshot keyed by the slash-joined node path
    // built by Player_buildNodePathKey @0x6B5C1C. Reverse-engineered in
    // player_value_structs_spec.md from:
    //   * Player_HM3_initValueFromNode @0x699510 (snapshot Node→V)
    //   * sub_6997F0                  @0x6997F0 (restore   V→Node)
    //   * Player_HM3_value_destroy    @0x6DD06C (release order)
    //   * Player_pruneHM3_byNodeIdentity @0x6B826C (frame skip read)
    //
    // Binary V layout is 688B; with ttstr=8B on Web vs 16B on Android the
    // local sizeof necessarily differs. PLATFORM_BOUNDARY governs the size
    // mismatch — what we replicate is field semantics, ownership, and the
    // destruction sequence.
    //
    // Field declaration order is ASCENDING binary offset. Members are
    // automatically destructed in REVERSE declaration order, which is
    // DESCENDING binary offset — matching the libkrkr2.so dtor sequence
    // (V+688 → V+584 heap → V+560 ttstr → V+516 → V+504 dispatch → … → V+8
    // dispatch). DispatchRef / HeapRef handle release; ~ttstr handles its
    // Ptr. No explicit destructor body is needed.
    struct PerNodeLayerState {
        // --- current-frame MotionNode snapshot ---
        // (init: Player_HM3_initValueFromNode @0x699510, reverse:
        //  sub_6997F0 @0x6997F0)
        // Semantic field set (HM3_initValueFromNode @0x699510 byte-verified):
        // the snapshot copies the node's already-interpolated state (written by
        // evaluateTimeline in resetMotionState loop1) + active ClipSlot fields.
        // (Port models these by value — node.accumulated/node.colorBytes plus
        // node.activeSlot(); offsets remain reverse-engineering documentation.)
        int nodeType = 0;                  // V+0   ← node+28
        DispatchRef dispatch_8;            // V+8   (dtor-released; not init-written)
        int contentMask = 0;              // V+28  ← active ClipSlot "mask" (slot+340)
        uint8_t doneFlag = 0;             // V+32  ← active ClipSlot done (slot+344)
        // Player_HM3_initValueFromNode @0x699610..0x69964C CopyRefs the
        // active slot's ttstr at node+356 into V+44. Restore @0x6997F0 does
        // not write it back; this owner only extends source lifetime until
        // the HM3 value is destroyed.
        ttstr srcValue_44;                 // V+44  ← active ClipSlot src ttstr
        int blendMode = 16;               // V+52  ← active ClipSlot "bm" (slot+364)
        double ox = 0.0;                  // V+64  ← active ClipSlot ox (slot+376)
        double oy = 0.0;                  // V+72  ← active ClipSlot oy
        std::array<std::uint32_t, 4> packedColors{   // V+80 ← interp RGBA (node+100..112)
            0xFF808080u, 0xFF808080u, 0xFF808080u, 0xFF808080u};
        int opacity = 255;                // V+96  ← interp opacity 0-255 (node+1576)
        double coordX = 0.0;              // V+104 ← interp x (node+1512)
        double coordY = 0.0;              // V+112 ← interp y (node+1520)
        double coordZ = 0.0;              // V+120 ← interp z (node+1528)
        uint8_t flipX = 0;                // V+128 ← interp flipX (node+1507)
        uint8_t flipY = 0;                // V+129 ← interp flipY (node+1508)
        double angle = 0.0;               // V+136 ← interp angle (node+1536)
        double scaleX = 1.0;              // V+144 ← interp scaleX (node+1544)
        double scaleY = 1.0;              // V+152 ← interp scaleY (node+1552)
        double slantX = 0.0;              // V+160 ← interp slantX (node+1560)
        double slantY = 0.0;              // V+168 ← interp slantY (node+1568)
        std::vector<MeshPoint> meshControlPoints; // V+568 ← node+2024 (meshType==1)
        // V+544 — tTJSVariant snapshot of node+1912 (child Player dispatch),
        // taken when nodeType==3. init @0x699598 does sub_A0FB64(V+544, node+1912)
        // (variant copy-assign) then sub_A0F790(node+1912) (variant clear); the
        // node's childPlayerVar ownership moves into this snapshot. restore
        // @0x699844 does the reverse: sub_A0FB64(node+1912, V+544) then
        // sub_A0F790(V+544). sub_A0FB64 (0xA0FB64) is the tTJSVariant copy ctor
        // (switches on the value type tag at +16, AddRefs object dispatch); it is
        // NOT a ttstr copy — V+544 is a full tTJSVariant, corrected from the prior
        // (falsified) ttstr modeling.
        tTJSVariant childPlayerSnapshot;       // V+544 (nodeType==3, node+1912)
        // V+600..664 — type-4 particle interpolation block snapshot (9 doubles).
        // init @0x6995dc (nodeType==4 && active slot done==0): V+600..664 <-
        //   node+2224..2288 (the evaluateTimeline particle eval-output mirror,
        //   MotionNode::particleInterp). 4 OWORD + 1 QWORD = 0x48 bytes.
        // restore @0x699890 (nodeType==4 && V+32==0): memcpy(slot+744 <- V+600,
        //   0x48) — writes the active ClipSlot prt block. ALIAS (self-disassembled):
        //   slot+744 == slot+424 (node+536*idx+744 == node+320+536*idx+424), so the
        //   destination is the prtFmin..prtRange fields, the SAME bytes
        //   mergeFrameContent writes each frame. We model V+600..664 as a flat
        //   9-double array (the binary's V+150..164 int* view == V+600..664 byte
        //   view) and restore writes it field-by-field into the prt block.
        double particleInterp[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};  // V+600..664

        // --- multi-slot region (dtor-referenced) ---
        ttstr ttstr_188;                            // binary V+188 dtor 0x6DD0F8
        ttstr ttstr_228;                            // binary V+228 dtor 0x6DD0E8
        ttstr ttstr_268;                            // binary V+268 dtor 0x6DD0D8
        DispatchRef dispatch_288;                   // binary V+288 dtor 0x6DD0CC
        ttstr ttstr_296;                            // binary V+296 dtor 0x6DD0C4
        HeapRef heap_320;                           // binary V+320 dtor 0x6DD0B4
        ttstr ttstr_364;                            // binary V+364 dtor 0x6DD0A8
        DispatchRef dispatch_392;                   // binary V+392 dtor 0x6DD098
        DispatchRef dispatch_504;                   // binary V+504 dtor 0x6DD08C
        ttstr ttstr_516;                            // binary V+516 dtor 0x6DD080
        // (V+544 = childPlayerSnapshot tTJSVariant, declared above with the
        //  current-frame snapshot block since it is init/restore-written, not
        //  merely dtor-referenced.)
        ttstr ttstr_560;                            // binary V+560 dtor 0x6DD040
        // V+672 — type-4 particle Array dispatch snapshot (a tTJSVariant holding
        // the TJS Array at node+2296, NOT a ttstr — corrected from prior falsified
        // ttstr modeling; the writer sub_A0FB64 @0x699550 is the tTJSVariant
        // copy-assign, same as V+544 childPlayerSnapshot). init @0x699550
        // (nodeType==4): sub_A0FB64(V+672, node+2296) then sub_A0F790(node+2296)
        // — moves node.particleArrayVar into the snapshot. restore @0x699868:
        // sub_A0FB64(node+2296, V+672) then sub_A0F790(V+672) — moves it back.
        // NOTE: V+672 is NOT released by Player_HM3_value_destroy @0x6DD06C (it is
        // transient, consumed/transferred by restore — same as V+544); tTJSVariant's
        // own dtor handles any un-restored snapshot, matching the binary leaving the
        // value tag to its move-out / consume lifecycle.
        tTJSVariant particleArraySnapshot;          // binary V+672 (nodeType==4, node+2296)
        HeapRef heap_584;                           // binary V+584 dtor 0x6DD030
        ttstr ttstr_688;                            // binary V+688 dtor 0x6DD02C

        PerNodeLayerState() = default;
        PerNodeLayerState(const PerNodeLayerState &) = delete;
        PerNodeLayerState &operator=(const PerNodeLayerState &) = delete;
        PerNodeLayerState(PerNodeLayerState &&) noexcept = default;
        PerNodeLayerState &operator=(PerNodeLayerState &&) noexcept = default;
        ~PerNodeLayerState() = default;
    };

} // namespace motion::detail
