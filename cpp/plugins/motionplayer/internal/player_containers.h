// Container aliases for motion::Player's four-reference native hash maps.
// Android uses libstdc++ and iOS/Web use different libc++ ABIs, so portable
// object sizes and offsets cannot be identical. The contract here is the same
// key/value semantics, custom UTF-16 hash/equality, ownership and observable
// boundary behavior. Exact per-target layouts belong in analysis/.
//
// HM3's portable PerNodeLayerState now lives in value_structs.h. All four
// references place a complete ClipSlot inside that mapped value, followed by
// the child Variant, outer mesh vector, particle block and particle Variant.
// Map clear first performs the explicit invalidation pass over unconsumed
// type-3/type-4 retained child objects, then clears HM4 and finally destroys
// the ordinary HM3 value owners.
//
#pragma once

#include <deque>
#include <map>
#include <unordered_map>

#include "ttstr_hash.h"
#include "value_structs.h"

namespace motion::detail {

    // HM1: ttstr → EvalCascadeState. The mapped value owns a copied key and
    // scope-label chain, stores the latest write/weight, and caches non-owning
    // MotionNode pointers. Rebuild clears only the vector's logical length, so
    // its capacity is retained.
    using EvalCascadeMap =
        std::unordered_map<ttstr, EvalCascadeState, ttstr_hash, ttstr_equal>;

    // HM2: ttstr -> raw double (not tTJSVariant). Used by the Player
    // setVariable/getVariable path and, as the identical specialization, by the
    // EmoteEngine variable-value map. A missing operator[] entry value-initializes
    // the mapped double to +0.0 before returning it; an existing entry is not
    // relinked.
    using LabelValueMap =
        std::unordered_map<ttstr, double, ttstr_hash, ttstr_equal>;

    // HM3: node-path ttstr -> PerNodeLayerState. The value type owns its
    // embedded ClipSlot plus the specialized Variants and mesh vector.
    // Unconsumed child objects receive the separate pre-clear invalidation
    // pass described above; that behavior is intentionally not a destructor
    // side effect because matched entries have already transferred the owners.
    using PerNodeLayerStateMap =
        std::unordered_map<ttstr, PerNodeLayerState, ttstr_hash, ttstr_equal>;

    // HM4: ttstr -> raw double.
    // Same value-slot semantics as HM2: clear releases the ttstr key and deletes
    // the node; the raw double is never AddRef'd or Release'd. The former
    // `iTJSDispatch2 *` annotation was an audit misread: the writer stores a raw
    // variable-track value and the reader loads it directly as double.
    //
    // Short-lived join snapshot populated by resetMotionState_guess. Each live
    // variable track writes its interpolated value under cascadeKey. Full reseek
    // may restore a hit to the active slot, then clears the map. EmoteEngine's
    // facade getter checks it before falling back to Player's HM1/HM2 reader.
    using VariableSnapshotMap =
        std::unordered_map<ttstr, double, ttstr_hash, ttstr_equal>;

    // Variable-track deque. initVariables clears it, appends one element per
    // motion `variable` item and builds cascadeKey as scope+"::"+label when
    // scope is present. Join snapshot production stores current values in HM4
    // under that raw cascadeKey. This is a lookup/interpolation table, distinct
    // from EmotePlayer's controller state-machine deques.
    using VariableLabelScopeDeque = std::deque<VariableLabelScope>;

    // Node-index map keyed by the raw PSB `label`. Null-backed keys sort before
    // every non-null backing; remaining order is UTF-16 code-unit lexicographic.
    // Duplicate construction writes replace only the mapped flat-node index,
    // retaining the key object from the first insertion. Iteration is the
    // tree's in-order walk.
    using NodeLabelMap = std::map<ttstr, int, ttstr_utf16_less>;

    struct MotionParameterEntry;

    // Parameter controller ramp table. It is built after the parameter-entry
    // vector is populated from PSB `parameterList`. Each mapped value borrows a
    // pointer to its vector entry; the tree key owns a copy of entry.id. Entries
    // are inserted unconditionally, so duplicate ids are kept. This
    // matches the binder's equal_range walk over every duplicate. Comparator is
    // UTF-16 lexicographic (`ttstr_utf16_less`, shared with NodeLabelMap). The
    // own-player lookup uses the raw label; descendant lookups use the split
    // suffix. Exact four-target addresses, node sizes and Player offsets live
    // in analysis/. Destruction erases nodes by exact mapped pointer before the
    // parameter vector releases its storage.
    using ParameterRampMap =
        std::multimap<ttstr, MotionParameterEntry *, ttstr_utf16_less>;

    // Player's other deque carries motion::MotionNode. The alias is not
    // declared here to keep this header free of the MotionNode dependency
    // surface; consumers spell it directly as std::deque<motion::MotionNode>.

} // namespace motion::detail
