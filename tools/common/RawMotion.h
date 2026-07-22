#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "RawPsb.h"

namespace tools::rawmotion {
    using rawpsb::Value;

    struct BezierCurve final {
        std::vector<double> x;
        std::vector<double> y;
        [[nodiscard]] bool empty() const { return x.empty(); }
    };

    struct SplineSegment final {
        std::vector<double> x;
        std::vector<double> y;
        std::vector<double> p;
    };

    struct ControlPointCurve final {
        std::vector<double> x;
        std::vector<double> y;
        std::vector<double> t;
        std::vector<SplineSegment> s;
        [[nodiscard]] bool empty() const { return t.empty(); }
    };

    struct FrameState final {
        bool visible = false;
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        double opacity = 1.0;
        double angle = 0.0;
        double scaleX = 1.0;
        double scaleY = 1.0;
        double slantX = 0.0;
        double slantY = 0.0;
        bool flipX = false;
        bool flipY = false;
        std::string icon;
        std::string src;
        BezierCurve ccc;
        BezierCurve acc;
        BezierCurve zcc;
        BezierCurve scc;
        ControlPointCurve cp;
    };

    inline std::vector<double> numberArray(const Value &value) {
        std::vector<double> result;
        for(tjs_int index = 0; index < value.count(); ++index) {
            if(const auto number = value.at(index).number())
                result.push_back(*number);
        }
        return result;
    }

    inline BezierCurve parseBezier(const Value &value) {
        return {numberArray(value.property("x")),
                numberArray(value.property("y"))};
    }

    inline double evaluateBezier(const BezierCurve &curve, double t) {
        if(curve.x.size() < 2 || curve.x.size() != curve.y.size()) return t;
        const std::size_t count = curve.x.size();
        if(curve.x.front() >= t) return curve.y.front();
        if(curve.x.back() <= t) return curve.y.back();
        std::size_t index = 0;
        while(index < count && curve.x[index] < t) index += 3;
        if(index < 3 || index >= count) return t;
        const double inverse = 1.0 - t;
        return inverse * inverse * inverse * curve.y[index - 3] +
            3.0 * inverse * inverse * t * curve.y[index - 2] +
            3.0 * inverse * t * t * curve.y[index - 1] +
            t * t * t * curve.y[index];
    }

    inline ControlPointCurve parseControlPoints(const Value &value) {
        ControlPointCurve result;
        result.x = numberArray(value.property("x"));
        result.y = numberArray(value.property("y"));
        result.t = numberArray(value.property("t"));
        const Value segments = value.property("s");
        for(tjs_int index = 0; index < segments.count(); ++index) {
            const Value segment = segments.at(index);
            result.s.push_back({numberArray(segment.property("x")),
                                numberArray(segment.property("y")),
                                numberArray(segment.property("p"))});
        }
        return result;
    }

    inline void evaluateControlPoint(double output[2],
                                     const ControlPointCurve &curve,
                                     double input) {
        if(curve.t.size() < 2 || curve.x.size() < 4 || curve.y.size() < 4)
            return;
        int segmentIndex = 0;
        int mainIndex = 0;
        for(std::size_t index = 1; index < curve.t.size(); ++index) {
            mainIndex += 3;
            if(curve.t[index] >= input) {
                segmentIndex = static_cast<int>(index) - 1;
                break;
            }
            segmentIndex = static_cast<int>(index) - 1;
        }
        if(segmentIndex < 0 ||
           segmentIndex >= static_cast<int>(curve.s.size())) return;
        const double t0 = curve.t[segmentIndex];
        const double t1 = segmentIndex + 1 < static_cast<int>(curve.t.size())
            ? curve.t[segmentIndex + 1] : t0;
        const double local = t1 != t0 ? (input - t0) / (t1 - t0) : 0.0;
        double parameter = local;
        const auto &segment = curve.s[segmentIndex];
        if(!segment.x.empty() && segment.x.size() == segment.y.size()) {
            if(segment.x.front() >= local) {
                parameter = segment.y.front();
            } else if(segment.x.back() <= local) {
                parameter = segment.y.back();
            } else {
                int index = 0;
                for(std::size_t i = 1; i < segment.x.size(); ++i) {
                    if(segment.x[i] >= local) {
                        index = static_cast<int>(i) - 1;
                        break;
                    }
                    index = static_cast<int>(i) - 1;
                }
                if(index >= 0 && index + 1 < static_cast<int>(segment.x.size())) {
                    const double x0 = segment.x[index];
                    const double x1 = segment.x[index + 1];
                    const double dx = x1 - x0;
                    if(dx != 0.0) {
                        const double u = (local - x0) / dx;
                        const double inverse = 1.0 - u;
                        const double p0 = index < static_cast<int>(segment.p.size())
                            ? segment.p[index] : 0.0;
                        const double p1 = index + 1 < static_cast<int>(segment.p.size())
                            ? segment.p[index + 1] : 0.0;
                        parameter = dx * dx *
                            ((u * u * u - u) * p1 +
                             (inverse * inverse * inverse - inverse) * p0) /
                            6.0 + u * segment.y[index + 1] +
                            inverse * segment.y[index];
                    }
                }
            }
        }
        if(mainIndex < 3 || mainIndex >= static_cast<int>(curve.x.size()) ||
           mainIndex >= static_cast<int>(curve.y.size())) return;
        const double inverse = 1.0 - parameter;
        output[0] = inverse * inverse * inverse * curve.x[mainIndex - 3] +
            3.0 * inverse * inverse * parameter * curve.x[mainIndex - 2] +
            3.0 * inverse * parameter * parameter * curve.x[mainIndex - 1] +
            parameter * parameter * parameter * curve.x[mainIndex];
        output[1] = inverse * inverse * inverse * curve.y[mainIndex - 3] +
            3.0 * inverse * inverse * parameter * curve.y[mainIndex - 2] +
            3.0 * inverse * parameter * parameter * curve.y[mainIndex - 1] +
            parameter * parameter * parameter * curve.y[mainIndex];
    }

    inline void mergeFrameContent(const Value &content, FrameState &state,
                                  int nodeType) {
        if(!content.isDictionary()) return;
        // Player_mergeFrameContent @ 0x692AB0: all scalar reads are mask-gated.
        const int mask = static_cast<int>(
            content.property("mask").numberOr(0.0));
        const bool sourceGate = nodeType >= 0 && nodeType < 63 &&
            (((std::uint64_t{1} << static_cast<unsigned>(nodeType)) &
              0x1849u) != 0);
        if(sourceGate) {
            state.icon = content.property("icon").stringOr();
            const Value source = content.property("src");
            state.src = source.stringOr();
            if(state.src.empty() && source.isArray()) {
                for(tjs_int index = 0; index < source.count(); ++index) {
                    state.src = source.at(index).stringOr();
                    if(!state.src.empty()) break;
                }
            }
        }
        if(mask & 0x2) {
            const Value coordinate = content.property("coord");
            state.x = coordinate.at(0).numberOr(state.x);
            state.y = coordinate.at(1).numberOr(state.y);
            state.z = coordinate.at(2).numberOr(state.z);
        }
        if(mask & 0x400)
            state.opacity = std::clamp(
                content.property("opa").numberOr(state.opacity * 255.0) /
                    255.0,
                0.0, 1.0);
        if(mask & 0x10)
            state.angle = content.property("angle").numberOr(state.angle);
        if(mask & 0x4)
            state.flipX = content.property("fx").numberOr(state.flipX) != 0.0;
        if(mask & 0x8)
            state.flipY = content.property("fy").numberOr(state.flipY) != 0.0;
        if(mask & 0x20)
            state.scaleX = content.property("zx").numberOr(state.scaleX);
        if(mask & 0x40)
            state.scaleY = content.property("zy").numberOr(state.scaleY);
        if(mask & 0x80)
            state.slantX = content.property("sx").numberOr(state.slantX);
        if(mask & 0x100)
            state.slantY = content.property("sy").numberOr(state.slantY);
        if(mask & 0x800)
            state.ccc = parseBezier(content.property("ccc"));
        if(mask & 0x1000)
            state.acc = parseBezier(content.property("acc"));
        if(mask & 0x2000)
            state.zcc = parseBezier(content.property("zcc"));
        if(mask & 0x4000)
            state.scc = parseBezier(content.property("scc"));
        const Value controlPoints = content.property("cp");
        if(controlPoints.isDictionary())
            state.cp = parseControlPoints(controlPoints);
    }

    struct ParsedFrame final {
        double time = 0.0;
        int type = 0;
        FrameState slot;
    };

    inline ParsedFrame parseFrame(const Value &frame, int nodeType) {
        // Player_parseFrame @ 0x6926B4: reset slot, then time/type/content.
        ParsedFrame result;
        if(!frame.isDictionary()) return result;
        result.time = frame.property("time").numberOr(0.0);
        result.type = static_cast<int>(frame.property("type").numberOr(0.0));
        if(result.type != 0)
            mergeFrameContent(frame.property("content"), result.slot, nodeType);
        return result;
    }

    inline FrameState interpolate(const FrameState &from,
                                  const FrameState &to, double ratio) {
        // Player evaluator @ 0x699AE4. Motionsim is a 2D phase-2 oracle.
        FrameState result = from;
        const auto lerp = [](double a, double b, double t) {
            return a * (1.0 - t) + b * t;
        };
        const double positionRatio = !from.ccc.empty()
            ? evaluateBezier(from.ccc, ratio) : ratio;
        if(from.cp.empty()) {
            result.x = lerp(from.x, to.x, positionRatio);
            result.y = lerp(from.y, to.y, positionRatio);
            result.z = lerp(from.z, to.z, positionRatio);
        } else {
            double rotation[2] = {1.0, 0.0};
            evaluateControlPoint(rotation, from.cp, positionRatio);
            const double dx = to.x - from.x;
            const double dy = to.y - from.y;
            result.x = from.x + dx * rotation[0] - dy * rotation[1];
            result.y = from.y + dx * rotation[1] + dy * rotation[0];
            result.z = lerp(from.z, to.z, positionRatio);
        }
        if(from.opacity != to.opacity) {
            const double value = lerp(from.opacity * 255.0,
                                      to.opacity * 255.0, ratio);
            const int rounded = value < 0.0
                ? static_cast<int>(std::ceil(value - 0.5))
                : static_cast<int>(std::floor(value + 0.5));
            result.opacity = std::clamp(rounded / 255.0, 0.0, 1.0);
        }
        double angleTo = to.angle;
        if(result.angle >= angleTo) {
            if(result.angle - angleTo > 180.0) angleTo += 360.0;
        } else if(angleTo - result.angle > 180.0) {
            angleTo -= 360.0;
        }
        result.angle = lerp(result.angle, angleTo,
            !from.acc.empty() ? evaluateBezier(from.acc, ratio) : ratio);
        if(result.angle < 0.0) result.angle += 360.0;
        else if(result.angle >= 360.0) result.angle -= 360.0;
        const double zoomRatio = !from.zcc.empty()
            ? evaluateBezier(from.zcc, ratio) : ratio;
        result.scaleX = lerp(from.scaleX, to.scaleX, zoomRatio);
        result.scaleY = lerp(from.scaleY, to.scaleY, zoomRatio);
        const double slantRatio = !from.scc.empty()
            ? evaluateBezier(from.scc, ratio) : ratio;
        result.slantX = lerp(from.slantX, to.slantX, slantRatio);
        result.slantY = lerp(from.slantY, to.slantY, slantRatio);
        if(result.src.empty() && !to.src.empty()) {
            result.src = to.src;
            result.icon = to.icon;
        }
        return result;
    }

    inline FrameState evaluateLayer(const Value &layer, double time,
                                    int nodeType) {
        FrameState result;
        const Value frames = layer.property("frameList");
        if(!frames.isArray() || frames.count() == 0) return result;
        int activeIndex = -1;
        for(tjs_int index = 0; index < frames.count(); ++index) {
            const Value frame = frames.at(index);
            if(frame.property("time").numberOr(0.0) > time) break;
            activeIndex = index;
        }
        if(activeIndex < 0) return result;
        ParsedFrame active = parseFrame(frames.at(activeIndex), nodeType);
        if(active.type == 0) return result;
        result = active.slot;
        result.visible = true;
        if(active.type != 3 || activeIndex + 1 >= frames.count()) return result;
        ParsedFrame next = parseFrame(frames.at(activeIndex + 1), nodeType);
        if(next.slot.src.empty()) next.slot.src = result.src;
        const double duration = next.time - active.time;
        if(duration <= 0.0 || next.type == 0) return result;
        const double ratio = std::clamp(
            (time - active.time) / duration, 0.0, 1.0);
        if(ratio <= 0.0) return result;
        result = interpolate(result, next.slot, ratio);
        result.visible = true;
        return result;
    }
} // namespace tools::rawmotion
