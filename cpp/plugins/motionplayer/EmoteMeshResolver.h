// EmoteMeshResolver — faithful port of the eye/eyebrow value-track mesh
//   resolver in libkrkr2.so:
//     sub_661F7C @0x661F7C  — selector / output wiring (EmoteMeshResolver_resolve)
//     sub_660028 @0x660028  — the 1925-line edge-table node-row path-search engine
//                             (EmoteMeshResolver_search)
//
// Both EmoteBlinkController (eye, 0x170) and EmoteEyebrowController (eyebrow,
// slim 0x150) embed the SAME mesh-resolver state at controller+160 (the edge
// table) and controller+184 (the node-row pool), plus an 88-byte output-row
// vector at controller+264 and a resolved-span float at controller+288. The
// binary calls sub_661F7C(controller+160, controller+80, currentValue, endValue)
// after popping a 12B value-track keyframe; the resolver rebuilds the 8B value
// track (controller+80) from the best path it finds through the value graph.
//
// libkrkr2.so layout that this models (controller+160 = the resolver "self"):
//   a1[0..2]  std::vector<{float lo, float hi}> edgeTable   (controller+160)
//   a1[5..12] std::deque<std::vector<float>>   nodeRows     (controller+184)
//   a1[13..15] std::vector<MeshPathRow>        outputRows   (controller+264)
//   *(a1+128)  float                           trackResolvedSpan (controller+288)
//
// The engine performs a bounded depth-first path enumeration through the value
// graph: each "edge" is a float interval [lo,hi]; each "node" is a vector<float>
// of neighbour values. Starting from `startValue` (a2) it explores paths to
// `endValue` (a3), and for every path it reaches it emits one MeshPathRow
// (the flattened (from,to) segment list as a deque<float>, plus the total
// |to-from| distance). sub_661F7C then picks the row with the minimum distance,
// writes that distance to trackResolvedSpan, and loads the row's segment values
// into the 8B value-track deque.
//
// Per CLAUDE.md byte-layout methodology this is a clean-container port: the
// binary's libstdc++ std::deque<float>/std::vector<float> byte arithmetic maps
// directly to std::deque<float>/std::vector<float> here; only element data
// formats (float values) are a cross-platform contract, never ABI offsets.
//
#pragma once

#include <deque>
#include <utility>
#include <vector>

namespace motion {

    // 88-byte output row in the binary: { std::deque<{float,float}> path; float
    //   dist; }. sub_686FEC appends one; sub_687234 copy-constructs the deque
    //   (8-byte = float-pair elements, 512-byte blocks); *(row+80) = dist. The
    //   selector (sub_661F7C else-branch @0x6620b4) copies this pair-deque
    //   element-for-element into the 8B value track. Here the same two fields,
    //   named by semantics.
    struct MeshPathRow {
        std::deque<std::pair<float, float>> path; // {from,to} segments (row+0..79)
        float dist = 0.0f;                        // sum |to-from| (row+80)
    };

    // Resolver "self" state embedded in the controller at +160/+184/+264/+288.
    // Modelled as a struct so the eye and eyebrow controllers can both embed it
    // (the binary embeds the same field cluster in both).
    struct EmoteMeshResolverState {
        std::vector<std::pair<float, float>> edgeTable; // a1[0..2]  (+160)
        std::deque<std::vector<float>>       nodeRows;  // a1[5..12] (+184)
        std::vector<MeshPathRow>             outputRows; // a1[13..15] (+264)
        float trackResolvedSpan = 0.0f;                  // *(a1+128) (+288)
    };

    // Aligned with libkrkr2.so sub_661F7C @0x661F7C.
    //   Clears outputRows, runs the path-search engine (sub_660028), then selects
    //   the min-distance row: writes its dist to trackResolvedSpan and copies its
    //   path segment values into `valueTrack8B`. If no row qualifies it pushes a
    //   single {endValue,endValue} pair and sets trackResolvedSpan = 0.
    void EmoteMeshResolver_resolve(EmoteMeshResolverState* self,
                                   std::deque<std::pair<float, float>>* valueTrack8B,
                                   float startValue, float endValue);

} // namespace motion
