#pragma once

#include "MotionNode.h"
#include "DebugIntf.h"
#include "cpu_types.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

extern "C" tjs_uint32 TVPCPUType;

namespace motion::internal {

    using BezierPatchEvaluator_guess = detail::MeshPoint (*)(
        const detail::MeshPoint *, float, float);

    inline std::array<float, 4> cubicBezierBasis_guess(float t) {
        const float oneMinus = 1.0f - t;
        return {
            oneMinus * (oneMinus * oneMinus),
            (t * (oneMinus * oneMinus)) * 3.0f,
            (t * (t * oneMinus)) * 3.0f,
            t * (t * t)
        };
    }

    // Portable form of the original scalar fallback. It deliberately keeps
    // the two float accumulators and the native row-major 4x4 traversal.
    inline detail::MeshPoint evaluateBezierPatch4x4Scalar_guess(
        const detail::MeshPoint *points, float u, float v) {
        const auto basisU = cubicBezierBasis_guess(u);
        const auto basisV = cubicBezierBasis_guess(v);
        detail::MeshPoint result{0.0f, 0.0f};
        for(std::size_t i = 0; i < 16; ++i) {
            const float weight = basisV[i >> 2] * basisU[i & 3];
            result.x = result.x + points[i].x * weight;
            result.y = result.y + points[i].y * weight;
        }
        return result;
    }

    // The ARM references replace the scalar function pointer with a NEON
    // implementation when TVPCPUType identifies ARM+NEON. AArch64 emits fused
    // FMLA while ARMv7 emits VMLA; the pointer-width split preserves those two
    // lane-wise accumulation forms without depending on ARM intrinsics here.
    inline detail::MeshPoint evaluateBezierPatch4x4Neon_guess(
        const detail::MeshPoint *points, float u, float v) {
        const auto basisU = cubicBezierBasis_guess(u);
        const auto basisV = cubicBezierBasis_guess(v);
        detail::MeshPoint result{0.0f, 0.0f};
        for(std::size_t i = 0; i < 16; ++i) {
            const float weight = basisV[i >> 2] * basisU[i & 3];
#if INTPTR_MAX == INT64_MAX
            result.x = std::fma(points[i].x, weight, result.x);
            result.y = std::fma(points[i].y, weight, result.y);
#else
            result.x = result.x + points[i].x * weight;
            result.y = result.y + points[i].y * weight;
#endif
        }
        return result;
    }

    // The unit quad and default point vector are process-global native
    // objects. Their definitions live in main.cpp so the four-reference
    // construction/destruction order relative to the shared basis map and NCB
    // registration state is explicit. The vector is populated at the end of
    // Motion.Player NCB registration; the evaluator pointer starts on the
    // scalar kernel and is only promoted.
    extern std::array<float, 8> unitBezierPatchQuad_guess;
    extern std::vector<detail::MeshPoint>
        defaultBezierPatchPoints_guess;
    inline BezierPatchEvaluator_guess bezierPatchEvaluator_guess =
        &evaluateBezierPatch4x4Scalar_guess;

    inline void initializeBezierPatchRuntime_guess() {
        if(defaultBezierPatchPoints_guess.empty()) {
            defaultBezierPatchPoints_guess.reserve(16);
            for(int i = 0; i < 16; ++i) {
                defaultBezierPatchPoints_guess.push_back({
                    static_cast<float>(
                        static_cast<double>(i & 3) / 3.0),
                    static_cast<float>(
                        static_cast<double>(i >> 2) / 3.0)
                });
            }
        }

        constexpr tjs_uint32 armNeon =
            TVP_CPU_FAMILY_ARM | TVP_CPU_HAS_NEON;
        constexpr tjs_uint32 armNeonMask =
            TVP_CPU_FAMILY_MASK | TVP_CPU_HAS_NEON;
        if((TVPCPUType & armNeonMask) == armNeon) {
            bezierPatchEvaluator_guess =
                &evaluateBezierPatch4x4Neon_guess;
        }
    }

    // The wrapper diagnoses every non-16-point vector but intentionally calls
    // the selected fixed-size kernel anyway. A nonempty short vector therefore
    // retains the reference implementation's out-of-bounds boundary behavior.
    inline detail::MeshPoint evaluateBezierPatchVector_guess(
        const std::vector<detail::MeshPoint> &points, float u, float v) {
        if(points.size() != 16) {
            TVPAddLog(TJS_W("invalid size of bezier patch."));
        }
        return bezierPatchEvaluator_guess(points.data(), u, v);
    }

} // namespace motion::internal
