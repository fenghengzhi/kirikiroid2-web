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
#include <utility>
#include <vector>

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

    // VariableLabelScope — libkrkr2.so Player+1296 std::deque element.
    //
    // Populated by Player_initVariables @0x6CD750 while loading a motion. Each
    // entry binds a (variable, label, scope) triple resolved from the motion's
    // "variable" PSB array. Reverse-engineered from the deque push path at
    // 0x6CD9C0..0x6CDBB4.
    //
    // NOT a controller animator — those are EmotePlayer's 5 deques at +256
    // / +336 / +416 / +576 / +656 with 16-48B elements; this is a lookup
    // table populated once per motion load and read by the cascade evaluator.
    //
    // Binary element is 160B with most interior bytes memset(0); the only
    // populated regions are 3 ttstr fields and 3 byte flags. Local sizeof is
    // dictated by ttstr's actual sizeof (8B on Web, 16B on Android).
    struct VariableLabelScope {
        ttstr cascadeKey;       // binary +0  — "scope::variable" joined key
        ttstr labelName;        // binary +16 — from PropGet("label")
        ttstr scope;            // binary +64 — from sub_A0BAF4 resolution
        bool flagActive = true;     // binary +68
        bool flagValidated = true;  // binary +108
        bool flagField124 = true;   // binary +124

        VariableLabelScope() = default;
        VariableLabelScope(const VariableLabelScope &) = default;
        VariableLabelScope &operator=(const VariableLabelScope &) = default;
        VariableLabelScope(VariableLabelScope &&) noexcept = default;
        VariableLabelScope &operator=(VariableLabelScope &&) noexcept = default;
        ~VariableLabelScope() = default;
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
