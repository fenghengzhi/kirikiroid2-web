#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace motion::detail {

    // Point/Circle/Rect/Quad and the node-owned geometry all share this exact
    // record.  Padding before values is supplied by the target ABI: Android
    // ARM32 aligns double to 8, while iOS ARMv7 aligns it to 4.
    struct HitData {
        std::int32_t type;
        std::array<double, 15> values;
    };

    static_assert(offsetof(HitData, values) ==
                  (alignof(double) == 4 ? 4u : 8u));
    static_assert(sizeof(HitData) ==
                  (alignof(double) == 4 ? 0x7cu : 0x80u));

    inline bool hitTestHitData(const HitData &hit, double x, double y) {
        switch(hit.type) {
            case 1: {
                const double cx = hit.values[0];
                const double cy = hit.values[1];
                const double r = hit.values[2];
                const double dx = x - cx;
                const double dy = y - cy;
                return dx * dx + dy * dy <= r * r;
            }
            case 2:
                // Each gate requires an ordered comparison.  Writing the
                // tempting opposite predicates (`left > x`, etc.) would let a
                // NaN bound pass even though all four native condition-code
                // paths reject it.
                if(!(hit.values[3] <= x) || !(x < hit.values[5]) ||
                   !(hit.values[4] <= y)) {
                    return false;
                }
                return y < hit.values[6];
            case 3: {
                const double x0 = hit.values[7];
                const double y0 = hit.values[8];
                const double x1 = hit.values[9];
                const double y1 = hit.values[10];
                const double x2 = hit.values[11];
                const double y2 = hit.values[12];
                const double orientation =
                    (y2 - y0) * x1 + (x0 - x2) * y1 -
                    ((y2 - y0) * x0 + y0 * (x0 - x2));
                // ARM LT is selected for the -1.0 arm in all four references;
                // unordered FCMP flags therefore also select -1.0.
                const double direction = orientation >= 0.0 ? 1.0 : -1.0;

                for(std::size_t i = 0; i < 4; ++i) {
                    const std::size_t current = 7 + i * 2;
                    const std::size_t next = 7 + ((i + 1) & 3) * 2;
                    const double currentX = hit.values[current];
                    const double currentY = hit.values[current + 1];
                    const double nextX = hit.values[next];
                    const double nextY = hit.values[next + 1];
                    const double deltaY = nextY - currentY;
                    const double deltaX = currentX - nextX;
                    const double edge = deltaY * x + deltaX * y -
                        (currentX * deltaY + currentY * deltaX);
                    if(direction * edge > 0.0) {
                        return false;
                    }
                }
                return true;
            }
            default:
                return false;
        }
    }

} // namespace motion::detail
