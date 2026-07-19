#pragma once

#include <cstddef>

namespace motion::detail {

    // Element type of every mesh-control-point std::vector in libkrkr2.so.
    // Player_mergeFrameContent @0x692AB0 pushes one element per x/y pair;
    // vector copy @0x6996E8 and all consumers use an exact 8-byte stride.
    struct MeshPoint {
        float x;
        float y;
    };

    static_assert(sizeof(MeshPoint) == 8);

}
