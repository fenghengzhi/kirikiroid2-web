// EmoteMeshResolver — eye/eyebrow value-track graph resolver reconstructed from
// the four current Android/iOS 1.3.9 references.
//
// Both controllers embed the same logical state: an interval vector, a deque of
// neighbour rows, a vector of candidate path rows, and the selected path span.
// Native offsets and row sizes differ with pointer width and the platform STL;
// the portable structure below preserves the common field order, ownership,
// element types, and lifetime. Exact layouts and function mappings are recorded
// in analysis/ rather than compiled-source comments.
//
// The search is a bounded depth-first enumeration. Each edge is a closed float
// interval [lo, hi], each node row contains adjacent values, and each candidate
// owns a deque of {from,to} segments plus the accumulated |to-from| distance.
// The wrapper clears its previous candidates and destination track, runs the
// search, selects the first strict minimum whose distance is not -1, and moves
// that candidate into the controller's secondary value track from back to
// front. The selected output row remains present but its path is consumed;
// non-selected rows retain their owned paths. If no distance is below the
// native 99999 ceiling, it instead produces {endValue,endValue} with a zero
// resolved span.
//
#pragma once

#include <deque>
#include <utility>
#include <vector>

namespace motion {

    // Logical output row. Native row sizes are 88/44 bytes with libstdc++ and
    // 56/28 bytes with libc++; all four store pair elements, not a flattened
    // deque<float>.
    struct MeshPathRow {
        std::deque<std::pair<float, float>> path; // ordered {from,to} segments
        float dist = 0.0f;                        // accumulated sum |to-from|
    };

    // Embedded identically by logical field order in both controller classes.
    struct EmoteMeshResolverState {
        std::vector<std::pair<float, float>> edgeTable;
        std::deque<std::vector<float>> nodeRows;
        std::vector<MeshPathRow> outputRows;
        // Deliberately constructor-uninitialized. Every resolver return path
        // writes this before either controller copies it into its active track.
        float trackResolvedSpan;
    };

    // Clears both output containers, runs the graph search, then performs the
    // strict-minimum selection and fallback described above. The source name
    // is unavailable in the stripped references, hence the suffix.
    void EmoteMeshResolver_resolve_guess(
        EmoteMeshResolverState* self, float startValue, float endValue,
        std::deque<std::pair<float, float>>* valueTrack8B);

} // namespace motion
