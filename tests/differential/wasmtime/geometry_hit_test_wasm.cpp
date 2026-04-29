// @exports: _krkr2_hit_test_run
// @plugin-include
// @requires-lldb

#include <cstdint>

#include "motionplayer/HitTestInternal.h"

namespace {

int g_call_index = 0;

} // namespace

extern "C" __attribute__((used))
volatile std::int32_t krkr2_lldb_hit_test_last_call_index = -1;

extern "C" __attribute__((used))
volatile std::int32_t krkr2_lldb_hit_test_last_hit = 0;

extern "C" __attribute__((noinline, used))
void krkr2_lldb_hit_test_sample(std::int32_t call_index,
                                std::int32_t hit) {
    asm volatile("" : : "r"(call_index), "r"(hit) : "memory");
}

extern "C" int krkr2_hit_test_run(std::int32_t type,
                                   double point_x,
                                   double point_y,
                                   double v0,
                                   double v1,
                                   double v2,
                                   double v3,
                                   double v4,
                                   double v5,
                                   double v6,
                                   double v7,
                                   double v8,
                                   double v9,
                                   double v10,
                                   double v11,
                                   double v12,
                                   double v13,
                                   double v14) {
    motion::detail::HitData hit{};
    hit.type = type;
    hit.values[0] = v0;
    hit.values[1] = v1;
    hit.values[2] = v2;
    hit.values[3] = v3;
    hit.values[4] = v4;
    hit.values[5] = v5;
    hit.values[6] = v6;
    hit.values[7] = v7;
    hit.values[8] = v8;
    hit.values[9] = v9;
    hit.values[10] = v10;
    hit.values[11] = v11;
    hit.values[12] = v12;
    hit.values[13] = v13;
    hit.values[14] = v14;
    const int result =
        motion::detail::hitTestHitData(hit, point_x, point_y) ? 1 : 0;
    const int call_index = g_call_index++;
    krkr2_lldb_hit_test_last_call_index = call_index;
    krkr2_lldb_hit_test_last_hit = result;
    krkr2_lldb_hit_test_sample(call_index, result);
    return result;
}
