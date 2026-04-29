// Standalone WASM harness for local transform matrix computation.
// Function copied from motionplayer/PlayerInternal.h (lines 1816-1876).
// Aligned to libkrkr2.so sub_699940 (0x699940).
//
// @exports: _run_local_transform,_get_affine_in_ptr
// @requires-lldb

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

using Affine2x3 = std::array<double, 6>;

// Verbatim copy of motion::internal::applyLocalTransform from PlayerInternal.h.
// Build local 2x2 from identity via left-multiplication.
// Exactly replicates sub_699940 (0x699940): iterates
// transformOrder[0..3] and applies each transform case.
// Default order [0,1,2,3] = [Flip, Angle, Zoom, Slant].
void applyLocalTransform(
    Affine2x3 &a,
    bool flipX,
    bool flipY,
    double angle,
    double scaleX,
    double scaleY,
    double slantX,
    double slantY,
    const int (&transformOrder)[4]) {
    double l11 = 1.0, l12 = 0.0, l21 = 0.0, l22 = 1.0;

    for(int step = 0; step < 4; step++) {
        const int op = transformOrder[step];
        switch(op) {
            case 0: // Flip (left-multiply [-1,0;0,1] / [1,0;0,-1])
                if(flipX) { l11 = -l11; l12 = -l12; }
                if(flipY) { l21 = -l21; l22 = -l22; }
                break;
            case 1: // Angle (left-multiply [c,-s;s,c])
                if(angle != 0.0) {
                    const double rad = angle * 2.0 * 3.14159265358979323846 / 360.0;
                    const double c = std::cos(rad);
                    const double s = std::sin(rad);
                    const double t11 = c*l11 - s*l21;
                    const double t12 = c*l12 - s*l22;
                    const double t21 = s*l11 + c*l21;
                    const double t22 = s*l12 + c*l22;
                    l11 = t11; l12 = t12; l21 = t21; l22 = t22;
                }
                break;
            case 2: // Zoom (left-multiply [zx,0;0,zy]) — 0x699A50
                if(scaleX != 1.0 || scaleY != 1.0) {
                    l11 *= scaleX; l12 *= scaleX;
                    l21 *= scaleY; l22 *= scaleY;
                }
                break;
            case 3: // Slant (left-multiply [1,sx;sy,1]) — 0x699A7C
                if(slantX != 0.0 || slantY != 0.0) {
                    const double t12 = l22*slantX + l12;
                    const double t21 = l11*slantY + l21;
                    const double t22 = l22 + l12*slantY;
                    const double t11 = l11 + slantX*l21;
                    l11 = t11; l12 = t12; l21 = t21; l22 = t22;
                }
                break;
        }
    }

    // Right-multiply local 2x2 into affine: A_new = A × L
    // (tx,ty unchanged; only 2x2 part is affected)
    const double m11 = a[0]*l11 + a[2]*l21;
    const double m21 = a[1]*l11 + a[3]*l21;
    const double m12 = a[0]*l12 + a[2]*l22;
    const double m22 = a[1]*l12 + a[3]*l22;
    a[0] = m11; a[1] = m21; a[2] = m12; a[3] = m22;
}

} // namespace

static double g_affine_in[6];
static double g_affine_out[6];
static std::int32_t g_call_index;

extern "C" __attribute__((noinline, used))
void krkr2_lldb_local_transform_sample(std::int32_t call_index,
                                       double v0,
                                       double v1,
                                       double v2,
                                       double v3,
                                       double v4,
                                       double v5) {
    (void)call_index;
    (void)v0;
    (void)v1;
    (void)v2;
    (void)v3;
    (void)v4;
    (void)v5;
}

extern "C" {

double *get_affine_in_ptr() { return g_affine_in; }

void run_local_transform(
    std::int32_t flipX, std::int32_t flipY,
    double angle, double scaleX, double scaleY,
    double slantX, double slantY,
    std::int32_t order0, std::int32_t order1,
    std::int32_t order2, std::int32_t order3) {
    Affine2x3 a;
    std::memcpy(a.data(), g_affine_in, sizeof(double) * 6);
    int transformOrder[4] = {order0, order1, order2, order3};
    applyLocalTransform(a, flipX != 0, flipY != 0, angle, scaleX, scaleY,
                        slantX, slantY, transformOrder);
    std::memcpy(g_affine_out, a.data(), sizeof(double) * 6);
    krkr2_lldb_local_transform_sample(
        g_call_index++, g_affine_out[0], g_affine_out[1], g_affine_out[2],
        g_affine_out[3], g_affine_out[4], g_affine_out[5]);
}

} // extern "C"
