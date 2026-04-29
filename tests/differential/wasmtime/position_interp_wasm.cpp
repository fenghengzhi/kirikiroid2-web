// Standalone WASM harness for position interpolation with easing and rotation.
// Functions copied from motionplayer/PlayerInternal.h:
//   - evaluateBezierCurve (lines 708-725), aligned to sub_69A754
//   - evaluateControlPointCurve (lines 729-788), aligned to sub_698454
//   - interpolatePosition69A4D4 (lines 792-830), aligned to sub_69A4D4
//
// @exports: _run_position_interp,_get_easing_x_ptr,_get_easing_y_ptr,_get_cp_x_ptr,_get_cp_y_ptr,_get_cp_t_ptr,_get_cp_seg_data_ptr,_get_cp_seg_sizes_ptr,_get_src_pos_ptr,_get_dst_pos_ptr
// @requires-lldb

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

// --- Struct definitions (from PlayerInternal.h lines 510-533) ---

struct BezierCurve {
    std::vector<double> x;
    std::vector<double> y;
    bool empty() const { return x.empty(); }
};

struct SplineSegment {
    std::vector<double> x;
    std::vector<double> y;
    std::vector<double> p;
};

struct ControlPointCurve {
    std::vector<double> x;
    std::vector<double> y;
    std::vector<double> t;
    std::vector<SplineSegment> s;
    bool empty() const { return t.empty(); }
};

// --- Verbatim copy of evaluateBezierCurve (PlayerInternal.h:708-725) ---
// Aligned to libkrkr2.so sub_69A754 (0x69A754).
template<typename CurveT>
double evaluateBezierCurve(const CurveT &curve, double t) {
    if(curve.x.size() < 2 || curve.y.size() < 2) return t;
    if(curve.x.size() != curve.y.size()) return t;
    const size_t n = curve.x.size();
    if(curve.x[0] >= t) return curve.y[0];
    if(curve.x[n-1] <= t) return curve.y[n-1];
    size_t i = 0;
    while(i < n && curve.x[i] < t) i += 3;
    if(i < 3 || i >= n) return t;
    const double p0 = curve.y[i-3];
    const double p1 = curve.y[i-2];
    const double p2 = curve.y[i-1];
    const double p3 = curve.y[i];
    const double u = 1.0 - t;
    return u*u*u*p0 + 3.0*u*u*t*p1 + 3.0*u*t*t*p2 + t*t*t*p3;
}

// --- Verbatim copy of evaluateControlPointCurve (PlayerInternal.h:729-788) ---
// Aligned to libkrkr2.so sub_698454 (0x698454).
void evaluateControlPointCurve(
    double outXY[2], const ControlPointCurve &cp, double inputT) {
    if (cp.t.size() < 2 || cp.x.size() < 4 || cp.y.size() < 4) return;
    int segIdx = 0;
    int mainIdx = 0;
    for (size_t i = 1; i < cp.t.size(); ++i) {
        mainIdx += 3;
        if (cp.t[i] >= inputT) { segIdx = static_cast<int>(i) - 1; break; }
        segIdx = static_cast<int>(i) - 1;
    }
    if (segIdx < 0 || segIdx >= static_cast<int>(cp.s.size())) return;
    double tStart = cp.t[segIdx];
    double tEnd = (segIdx + 1 < static_cast<int>(cp.t.size())) ? cp.t[segIdx + 1] : tStart;
    double localT = (tEnd != tStart) ? (inputT - tStart) / (tEnd - tStart) : 0.0;
    double param = localT;
    const auto &seg = cp.s[segIdx];
    if (!seg.x.empty() && seg.x.size() == seg.y.size()) {
        double sx0 = seg.x[0];
        if (sx0 >= localT) {
            param = seg.y[0];
        } else if (seg.x.back() <= localT) {
            param = seg.y.back();
        } else {
            int subIdx = 0;
            for (size_t i = 1; i < seg.x.size(); ++i) {
                if (seg.x[i] >= localT) { subIdx = static_cast<int>(i) - 1; break; }
                subIdx = static_cast<int>(i) - 1;
            }
            if (subIdx >= 0 && subIdx + 1 < static_cast<int>(seg.x.size()) &&
                subIdx + 1 < static_cast<int>(seg.y.size())) {
                double x0 = seg.x[subIdx], x1 = seg.x[subIdx + 1];
                double y0 = seg.y[subIdx], y1 = seg.y[subIdx + 1];
                double dx = x1 - x0;
                if (dx != 0.0) {
                    double u = (localT - x0) / dx;
                    double p0 = (subIdx < static_cast<int>(seg.p.size())) ? seg.p[subIdx] : 0.0;
                    double p1 = (subIdx + 1 < static_cast<int>(seg.p.size())) ? seg.p[subIdx + 1] : 0.0;
                    param = dx * dx * ((u*u*u - u) * p1 + ((1-u)*(1-u)*(1-u) - (1-u)) * p0) / 6.0
                          + u * y1 + (1 - u) * y0;
                }
            }
        }
    }
    if (mainIdx >= 3 && mainIdx < static_cast<int>(cp.x.size()) &&
        mainIdx < static_cast<int>(cp.y.size())) {
        double px0 = cp.x[mainIdx-3], py0 = cp.y[mainIdx-3];
        double px1 = cp.x[mainIdx-2], py1 = cp.y[mainIdx-2];
        double px2 = cp.x[mainIdx-1], py2 = cp.y[mainIdx-1];
        double px3 = cp.x[mainIdx],   py3 = cp.y[mainIdx];
        double u = 1.0 - param;
        outXY[0] = u*u*u*px0 + 3*u*u*param*px1 + 3*u*param*param*px2 + param*param*param*px3;
        outXY[1] = u*u*u*py0 + 3*u*u*param*py1 + 3*u*param*param*py2 + param*param*param*py3;
    }
}

// --- Verbatim copy of interpolatePosition69A4D4 (PlayerInternal.h:792-830) ---
// Aligned to libkrkr2.so sub_69A4D4 (0x69A4D4).
void interpolatePosition69A4D4(
    const BezierCurve &easingCurve,
    const double dstPos[3],
    const double srcPos[3],
    double outPos[3],
    int coordinateMode,
    const ControlPointCurve &rotationCurve,
    double t) {
    if (srcPos[0]==dstPos[0] && srcPos[1]==dstPos[1] && srcPos[2]==dstPos[2]) {
        outPos[0]=srcPos[0]; outPos[1]=srcPos[1]; outPos[2]=srcPos[2];
        return;
    }
    double et = !easingCurve.empty() ? evaluateBezierCurve(easingCurve, t) : t;
    if (rotationCurve.empty()) {
        for (int i = 0; i < 3; ++i)
            outPos[i] = (srcPos[i]!=dstPos[i]) ? srcPos[i]*(1-et)+dstPos[i]*et : srcPos[i];
        return;
    }
    double rot[2] = {1.0, 0.0};
    evaluateControlPointCurve(rot, rotationCurve, et);
    double cosA = rot[0], sinA = rot[1];
    if (coordinateMode == 0) {
        double dx = dstPos[0]-srcPos[0], dy = dstPos[1]-srcPos[1];
        outPos[0] = srcPos[0] + dx*cosA - dy*sinA;
        outPos[1] = srcPos[1] + dx*sinA + dy*cosA;
        outPos[2] = (srcPos[2]!=dstPos[2]) ? srcPos[2]*(1-et)+dstPos[2]*et : srcPos[2];
    } else if (coordinateMode == 1) {
        double dx = dstPos[0]-srcPos[0], dz = dstPos[2]-srcPos[2];
        outPos[0] = srcPos[0] + dx*cosA - dz*sinA;
        outPos[1] = (srcPos[1]!=dstPos[1]) ? srcPos[1]*(1-et)+dstPos[1]*et : srcPos[1];
        outPos[2] = srcPos[2] + dz*cosA + dx*sinA;
    } else {
        outPos[0]=srcPos[0]; outPos[1]=srcPos[1]; outPos[2]=srcPos[2];
    }
}

} // namespace

// --- Global buffers ---

static double g_easing_x[64];
static double g_easing_y[64];
static double g_cp_x[64];
static double g_cp_y[64];
static double g_cp_t[16];
// Segment data: packed as [seg0.x..., seg0.y..., seg0.p..., seg1.x..., ...]
static double g_cp_seg_data[96];
// Segment sizes: [seg0.x_len, seg0.y_len, seg0.p_len, seg1.x_len, ...]
static std::int32_t g_cp_seg_sizes[12];

static double g_src_pos[3];
static double g_dst_pos[3];
static double g_out_pos[3];
static std::int32_t g_call_index;

extern "C" __attribute__((noinline, used))
void krkr2_lldb_position_interp_sample(std::int32_t call_index,
                                       double x,
                                       double y,
                                       double z) {
    (void)call_index;
    (void)x;
    (void)y;
    (void)z;
}

extern "C" {

double *get_easing_x_ptr() { return g_easing_x; }
double *get_easing_y_ptr() { return g_easing_y; }
double *get_cp_x_ptr() { return g_cp_x; }
double *get_cp_y_ptr() { return g_cp_y; }
double *get_cp_t_ptr() { return g_cp_t; }
double *get_cp_seg_data_ptr() { return g_cp_seg_data; }
std::int32_t *get_cp_seg_sizes_ptr() { return g_cp_seg_sizes; }
double *get_src_pos_ptr() { return g_src_pos; }
double *get_dst_pos_ptr() { return g_dst_pos; }

void run_position_interp(
    std::int32_t easing_n,
    std::int32_t cp_n,
    std::int32_t cp_t_n,
    std::int32_t cp_seg_count,
    std::int32_t coord_mode,
    double t) {

    // Reconstruct BezierCurve
    BezierCurve easing;
    easing.x.assign(g_easing_x, g_easing_x + easing_n);
    easing.y.assign(g_easing_y, g_easing_y + easing_n);

    // Reconstruct ControlPointCurve
    ControlPointCurve cp;
    cp.x.assign(g_cp_x, g_cp_x + cp_n);
    cp.y.assign(g_cp_y, g_cp_y + cp_n);
    cp.t.assign(g_cp_t, g_cp_t + cp_t_n);

    // Reconstruct segments from packed data
    int data_offset = 0;
    for (int seg = 0; seg < cp_seg_count; ++seg) {
        SplineSegment s;
        int x_len = g_cp_seg_sizes[seg * 3 + 0];
        int y_len = g_cp_seg_sizes[seg * 3 + 1];
        int p_len = g_cp_seg_sizes[seg * 3 + 2];
        s.x.assign(g_cp_seg_data + data_offset, g_cp_seg_data + data_offset + x_len);
        data_offset += x_len;
        s.y.assign(g_cp_seg_data + data_offset, g_cp_seg_data + data_offset + y_len);
        data_offset += y_len;
        s.p.assign(g_cp_seg_data + data_offset, g_cp_seg_data + data_offset + p_len);
        data_offset += p_len;
        cp.s.push_back(s);
    }

    // Run interpolation
    g_out_pos[0] = 0.0;
    g_out_pos[1] = 0.0;
    g_out_pos[2] = 0.0;
    interpolatePosition69A4D4(easing, g_dst_pos, g_src_pos, g_out_pos,
                              coord_mode, cp, t);
    krkr2_lldb_position_interp_sample(
        g_call_index++, g_out_pos[0], g_out_pos[1], g_out_pos[2]);
}

} // extern "C"
