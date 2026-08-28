// Standalone WASM harness for Bezier curve evaluation.
// Function copied from motionplayer/PlayerInternal.h (lines 708-725).
// Aligned to libkrkr2.so sub_69A754 (0x69A754).
//
// @exports: _run_bezier_curve,_get_curve_x_ptr,_get_curve_y_ptr,_get_bezier_result,_run_bezier_curve_direct

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

struct LocalBezierCurve {
    std::vector<double> x;
    std::vector<double> y;
};

// Verbatim copy of motion::internal::evaluateBezierCurve from PlayerInternal.h.
// Evaluate cubic bezier curve at parameter t.
// Aligned to libkrkr2.so sub_69A754 (0x69A754):
//   - x[] = time control points, y[] = value control points
//   - Segments of 4 control points each (step 3, shared endpoints)
//   - If t <= x[0]: return y[0]
//   - If t >= x[last]: return y[last]
//   - Find segment where x[i] >= t (step 3)
//   - B(t) = (1-t)^3*P0 + 3*(1-t)^2*t*P1 + 3*(1-t)*t^2*P2 + t^3*P3
double evaluateBezierCurve(const LocalBezierCurve &curve, double t) {
    if(curve.x.size() < 2 || curve.y.size() < 2) return t;
    if(curve.x.size() != curve.y.size()) return t;
    const size_t n = curve.x.size();
    if(curve.x[0] >= t) return curve.y[0];
    if(curve.x[n-1] <= t) return curve.y[n-1];
    // Find segment (step 3, aligned to sub_69A754 at 0x69A960)
    size_t i = 0;
    while(i < n && curve.x[i] < t) i += 3;
    if(i < 3 || i >= n) return t;
    // Cubic bezier: P0=y[i-3], P1=y[i-2], P2=y[i-1], P3=y[i]
    const double p0 = curve.y[i-3];
    const double p1 = curve.y[i-2];
    const double p2 = curve.y[i-1];
    const double p3 = curve.y[i];
    const double u = 1.0 - t;
    return u*u*u*p0 + 3.0*u*u*t*p1 + 3.0*u*t*t*p2 + t*t*t*p3;
}

} // namespace

static double g_curve_x[64];
static double g_curve_y[64];
static double g_result;

extern "C" {

double *get_curve_x_ptr() { return g_curve_x; }
double *get_curve_y_ptr() { return g_curve_y; }
double get_bezier_result() { return g_result; }

double run_bezier_curve_direct(
    std::int32_t n,
    double t,
    double x0, double x1, double x2, double x3,
    double x4, double x5, double x6,
    double y0, double y1, double y2, double y3,
    double y4, double y5, double y6) {
    const double xs[7] = {x0, x1, x2, x3, x4, x5, x6};
    const double ys[7] = {y0, y1, y2, y3, y4, y5, y6};
    LocalBezierCurve curve;
    curve.x.assign(xs, xs + n);
    curve.y.assign(ys, ys + n);
    return evaluateBezierCurve(curve, t);
}

void run_bezier_curve(std::int32_t n, double t) {
    LocalBezierCurve curve;
    curve.x.assign(g_curve_x, g_curve_x + n);
    curve.y.assign(g_curve_y, g_curve_y + n);
    g_result = evaluateBezierCurve(curve, t);
}

} // extern "C"
