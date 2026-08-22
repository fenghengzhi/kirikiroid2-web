#pragma once

#include <cstddef>

namespace motion::detail {

    // Four-reference element type for every mesh-control-point vector. Frame
    // merge pushes one element per x/y pair, and every consumer uses an exact
    // 8-byte stride.
    struct MeshPoint {
        float x;
        float y;
    };

    static_assert(sizeof(MeshPoint) == 8);

}
