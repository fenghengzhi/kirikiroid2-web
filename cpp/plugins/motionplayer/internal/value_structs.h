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

#include <cstddef>
#include <cstdint>
#include <vector>

#include "tjs.h"
#include "tjsInterface.h"
#include "tjsString.h"

namespace motion::detail {

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
    struct EvalCascadeState {
        ttstr keyCopy;
        iTJSDispatch2 *mainDispatch = nullptr;
        std::vector<iTJSDispatch2 *> chainDispatches;
        void *heapResult = nullptr;

        EvalCascadeState() = default;
        EvalCascadeState(const EvalCascadeState &) = delete;
        EvalCascadeState &operator=(const EvalCascadeState &) = delete;

        EvalCascadeState(EvalCascadeState &&other) noexcept
            : keyCopy(std::move(other.keyCopy)),
              mainDispatch(other.mainDispatch),
              chainDispatches(std::move(other.chainDispatches)),
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

    // PerNodeLayerState — libkrkr2.so HM3 (Player+1184) value type (688B).
    //
    // Per-node-path layer state snapshot. Reverse-engineered partially in
    // player_value_structs_spec.md; first ~170B is a current-frame MotionNode
    // snapshot, +188..+688 contains 8+ ttstr / 5+ iTJSDispatch2* slots plus a
    // 56B-strided per-frame skip array. Implementing the full 688B with all
    // dtor-referenced refcount sites is deferred to a focused follow-up; until
    // then HM3 is not used at the container-alias layer (see
    // player_containers.h — no HM3Map alias yet).
    //
    // Risk noted here so future maintainers don't accidentally add an HM3Map
    // alias without first defining this struct: any value type used by
    // unordered_map must release the binary's 8+ ttstr and 5+ dispatch fields
    // in the right order or the Web build will leak.
    struct PerNodeLayerState; // forward-declare only — full definition pending

} // namespace motion::detail
