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

#include "tjs.h"
#include "tjsInterface.h"
#include "tjsString.h"

// Forward declaration — VariableLabelScope::frameSource holds a PSB keyframe
// list (item+24) without this header pulling in the full psbfile dependency.
// shared_ptr<incomplete> as a member is fine; it is constructed/destroyed where
// the complete type is visible (PlayerMotionLoad.cpp / PlayerFrameProgress.cpp).
namespace PSB {
    class IPSBValue;
}

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

    // EvalCascadeState — libkrkr2.so HM1 (Player+264) value type.
    //
    // Cached result of "::"-cascade-joined PropGet lookup chains. Reverse-
    // engineered from Player_HM1_upsert_evalCascade @0x6F52AC and
    // Player_HM1_value_destroy @0x6DD1A0. Binary V layout (72B):
    //   V+0  ttstr key copy
    //   V+8  iTJSDispatch2 *mainDispatch
    //   V+16 std::vector<iTJSDispatch2*> chainDispatches  (3 pointers V+16..+32)
    //   V+40 padding
    //   V+48 void *heapResult  (operator new / delete)
    //   V+56..+72 padding
    //
    // Binary dtor sequence: heap → vector elements release + backing delete →
    // main dispatch release → key release. Local destructor body mirrors that
    // order, then member destructors run in reverse declaration order.
    //
    // Binary node is operator new(0x60u) @0x6F5368. The fields the port's
    // evidence-grounded write subset populates are:
    //   V+32 double writeVal  (= a4, store at 0x6c4968)
    //   V+40 double weight     (= 1.0, seeded once at 0x6c4964 on first insert)
    // DEFERRED (no port consumer / unported input): chainDispatches (built by
    // sub_697D34 @0x6c48bc — pure TJS-dispatch scope resolution), the V+48 aux
    // node vector (sub_6B9650 @0x6c4974), and the V+408-keyed controller ramps.
    struct EvalCascadeState {
        ttstr keyCopy;
        iTJSDispatch2 *mainDispatch = nullptr;
        std::vector<iTJSDispatch2 *> chainDispatches;
        // Binary V+32 / V+40. writeVal := a4 on every bind; weight is seeded to
        // 1.0 only on first insert and left untouched on later upserts.
        double writeVal = 0.0;
        double weight = 0.0;
        void *heapResult = nullptr;

        EvalCascadeState() = default;
        EvalCascadeState(const EvalCascadeState &) = delete;
        EvalCascadeState &operator=(const EvalCascadeState &) = delete;

        EvalCascadeState(EvalCascadeState &&other) noexcept
            : keyCopy(std::move(other.keyCopy)),
              mainDispatch(other.mainDispatch),
              chainDispatches(std::move(other.chainDispatches)),
              writeVal(other.writeVal),
              weight(other.weight),
              heapResult(other.heapResult) {
            other.mainDispatch = nullptr;
            other.heapResult = nullptr;
        }

        EvalCascadeState &operator=(EvalCascadeState &&other) noexcept {
            if (this != &other) {
                this->~EvalCascadeState();
                ::new (this) EvalCascadeState(std::move(other));
            }
            return *this;
        }

        ~EvalCascadeState() {
            // Binary dtor order: V+48 → V+16..+24 → V+8 → V+0.
            if (heapResult) {
                ::operator delete(heapResult);
                heapResult = nullptr;
            }
            for (auto *d : chainDispatches) {
                if (d) {
                    d->Release();
                }
            }
            chainDispatches.clear();
            if (mainDispatch) {
                mainDispatch->Release();
                mainDispatch = nullptr;
            }
            // ~ttstr handles keyCopy (Release on its Ptr).
        }
    };

    // VarTrackSlot — libkrkr2.so 56B per-track frame slot. Two of these live
    // inside each VariableLabelScope (binary item+48 / item+104, stride 56B) and
    // are the var-track analog of the node's ParsedFrameSlotLike_0x6926B4 (536B).
    // Filled by the var-track advance stream ③ inside Player_advanceRootAndNodes
    // @0x6B6ADC: sub_6B786C (step — frame index + time) then sub_6B7A70 (merge —
    // type→flags + interval/value/easing). Field offsets byte-verified from those
    // two functions (slot base = a1):
    //   sub_6B786C: a1[0]=frameIdx; a1+8=frame["time"]; a1+22=0.
    //   sub_6B7A70: a1+22=1; type=frame["type"]; type==0→a1+20=1 (early return,
    //     loop2 HM4 gate); else a1+20=0, a1+21=(type==3), a1[4]=frame["interval"],
    //     *((double*)a1+3)=frame["value"], a1+32=frame["content"]["easing"].
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
        ttstr easing;                  // +32 merge: frame["content"]["easing"]
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
    // (Local sizeof differs by ttstr/shared_ptr sizeof — PLATFORM_BOUNDARY — but
    // field order / container selection match the binary.)
    //
    // NOT a controller animator — those are EmotePlayer's 5 deques at +256
    // / +336 / +416 / +576 / +656 with 16-48B elements; this is a lookup table
    // populated once per motion load and read by the cascade evaluator.
    struct VariableLabelScope {
        ttstr cascadeKey;            // item+0  — HM4/HM1 key
        int activeSlotCursor = 0;    // item+8  — parity cursor
        double value = 0.0;          // item+16 — HM4 value (interpolated)
        // item+24 — entry["label"] raw value, iterated as a keyframe list by the
        // var-track advance (stream③); no-op when it is not a list (mirrors the
        // binary's PropGetCount ~0 on a non-array variant).
        std::shared_ptr<PSB::IPSBValue> frameSource;
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
        int nodeType = 0;                           // binary V+0   from Node+28
        uint32_t _pad_4 = 0;                        // align
        DispatchRef dispatch_8;                     // binary V+8   release in dtor
        std::array<uint8_t, 16> _opaque_16_31 = {}; // binary V+16  unknown gap

        int field_28 = 0;                           // binary V+28  from Node+340
        std::array<uint8_t, 12> _opaque_32_43 = {}; // binary V+32  unknown gap

        DispatchRef dispatch_44;                    // binary V+44  Node+356, refcount++, release in dtor
        int field_52 = 0;                           // binary V+52  from Node+364
        std::array<uint8_t, 8> _opaque_56_63 = {};  // align

        std::array<uint8_t, 16> oword_64 = {};      // binary V+64  Node+376
        int sourceRect_x = 0;                       // binary V+80  Player+100
        int sourceRect_y = 0;                       // binary V+84  Player+104
        int sourceRect_w = 0;                       // binary V+88  Player+108
        int sourceRect_h = 0;                       // binary V+92  Player+112
        int field_96 = 0;                           // binary V+96  Node+408
        std::array<uint8_t, 4> _pad_100 = {};       // align

        std::array<uint8_t, 16> oword_104 = {};     // binary V+104 Player+1512
        int64_t qword_120 = 0;                      // binary V+120 Player+1528
        uint8_t skipFlag_128 = 0;                   // binary V+128 Node+(536·frame)+344
        uint8_t flag_129 = 0;                       // binary V+129 Player+1508
        std::array<uint8_t, 6> _pad_130 = {};       // align
        std::array<uint8_t, 16> oword_136 = {};     // binary V+136 Player+1544

        // long double — 16B on ARM64 libstdc++, 8B on Emscripten libc++.
        // Kept opaque to avoid platform-dependent slot sizing in the struct.
        std::array<uint8_t, 16> ldouble_152 = {};   // binary V+152 Player+1560
        int64_t qword_168 = 0;                      // binary V+168 Player+1536

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
        ttstr ttstr_544;                            // binary V+544 init nodeType==3 (Node+1912)
        ttstr ttstr_560;                            // binary V+560 dtor 0x6DD040
        ttstr ttstr_672;                            // binary V+672 init nodeType==4 (Node+2296)
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
