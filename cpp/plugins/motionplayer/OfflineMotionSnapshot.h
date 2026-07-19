// Offline-only eager PSB snapshot and decoded resource helpers.
//
// Android libkrkr2.so Player ctor/dtor/load
// (0x6CED30/0x6CFADC/0x6B0F10) has no corresponding owner or call path.
// Keep this header out of the production motionplayer include graph.
#pragma once

#include "PlayerInternal.h"
#include "psbfile/PSBFile.h"

namespace motion::detail {

    struct MotionSnapshot {
        std::string path;
        std::shared_ptr<PSB::DecodedPSBFile> file;
        std::shared_ptr<const PSB::PSBDictionary> root;
        std::unordered_map<std::string, std::shared_ptr<const PSB::PSBResource>>
            resourcesByPath;
        tTJSVariant moduleValue;
    };

    std::shared_ptr<MotionSnapshot> loadMotionSnapshot(const ttstr &path,
                                                       tjs_int decryptSeed);

} // namespace motion::detail

namespace motion::internal {

    // Offline tools eagerly decode PSB curves into STL containers. Android's
    // live Player path does not own these mirrors: it retains raw TJS Objects
    // in ClipSlot and resolves their properties inside 0x69A754/0x698454.
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

    struct OfflineFrameContentState {
        bool visible = false;
        int frameType = 0;
        double x = 0.0, y = 0.0, z = 0.0;
        double ox = 0.0, oy = 0.0;
        double width = 0.0, height = 0.0;
        double opacity = 1.0;
        double angle = 0.0;
        double scaleX = 1.0, scaleY = 1.0;
        double slantX = 0.0, slantY = 0.0;
        bool flipX = false, flipY = false;
        int blendMode = 16;
        std::array<std::uint32_t, 4> packedColors{
            0xFF808080u, 0xFF808080u, 0xFF808080u, 0xFF808080u};
        int motionMask = 0;
        int motionFlags = 0;
        int motionDt = 0;
        bool motionDocmpl = false;
        double motionDofst = 0.0;
        double motionTimeOffset = 0.0;
        int prtTrigger = 0;
        double prtFmin = 10.0, prtF = 10.0;
        double prtVmin = 0.0, prtV = 0.0;
        double prtAmin = 0.0, prtA = 0.0;
        double prtZmin = 1.0, prtZ = 1.0;
        double prtRange = 0.0;
        double cameraFactor = 0.0;
        double modelTimeOffset = 0.0;
        bool modelLoop = false;
        int modelDt = 0;
        double feedbackTimespan = 0.0;
        int transformOrder[4] = {0, 1, 2, 3};
        bool hasTransformOrder = false;
        bool hasSync = false;
        double clipStartTime = 0.0;
        bool debugEvaluated = false;
        int debugActiveIndex = -1;
        int debugNextIndex = -1;
        int debugFrameAType = 0;
        int debugFrameBType = 0;
        bool debugFrameAInvisible = false;
        bool debugFrameBInvisible = false;
        bool debugInterpolated = false;
        double debugFrameATime = 0.0;
        double debugFrameBTime = 0.0;
        double debugInterpT = 0.0;
        double debugFrameAOpacity = 1.0;
        double debugFrameBOpacity = 1.0;
        double debugFrameAScaleX = 1.0;
        double debugFrameAScaleY = 1.0;
        double debugFrameBScaleX = 1.0;
        double debugFrameBScaleY = 1.0;
        std::string debugFrameASrc;
        std::string debugFrameBSrc;

        // Offline decoded-model owners only. Android live evaluation never
        // copies or interpolates source strings into its scalar result block.
        std::string icon;
        std::string src;
        BezierCurve ccc;
        BezierCurve acc;
        BezierCurve zcc;
        BezierCurve scc;
        BezierCurve occ;
        BezierCurve cc;
        ControlPointCurve cp;
    };

    template<typename CurveT>
    inline double evaluateOfflineBezierCurve(const CurveT &curve, double t) {
        if(curve.x.size() < 2 || curve.y.size() < 2) return t;
        if(curve.x.size() != curve.y.size()) return t;
        const size_t n = curve.x.size();
        if(curve.x[0] >= t) return curve.y[0];
        if(curve.x[n - 1] <= t) return curve.y[n - 1];
        size_t i = 0;
        while(i < n && curve.x[i] < t) i += 3;
        if(i < 3 || i >= n) return t;
        const double u = 1.0 - t;
        return u * u * u * curve.y[i - 3] +
               3.0 * u * u * t * curve.y[i - 2] +
               3.0 * u * t * t * curve.y[i - 1] +
               t * t * t * curve.y[i];
    }

    inline void evaluateOfflineControlPointCurve(
        double outXY[2], const ControlPointCurve &cp, double inputT) {
        if(cp.t.size() < 2 || cp.x.size() < 4 || cp.y.size() < 4) return;
        int segmentIndex = 0;
        int mainIndex = 0;
        for(size_t i = 1; i < cp.t.size(); ++i) {
            mainIndex += 3;
            if(cp.t[i] >= inputT) {
                segmentIndex = static_cast<int>(i) - 1;
                break;
            }
            segmentIndex = static_cast<int>(i) - 1;
        }
        if(segmentIndex < 0 ||
           segmentIndex >= static_cast<int>(cp.s.size())) return;
        const double t0 = cp.t[segmentIndex];
        const double t1 = segmentIndex + 1 < static_cast<int>(cp.t.size())
            ? cp.t[segmentIndex + 1] : t0;
        const double localT = t1 != t0 ? (inputT - t0) / (t1 - t0) : 0.0;
        double parameter = localT;
        const auto &segment = cp.s[segmentIndex];
        if(!segment.x.empty() && segment.x.size() == segment.y.size()) {
            if(segment.x.front() >= localT) {
                parameter = segment.y.front();
            } else if(segment.x.back() <= localT) {
                parameter = segment.y.back();
            } else {
                int index = 0;
                for(size_t i = 1; i < segment.x.size(); ++i) {
                    if(segment.x[i] >= localT) {
                        index = static_cast<int>(i) - 1;
                        break;
                    }
                    index = static_cast<int>(i) - 1;
                }
                if(index >= 0 && index + 1 < static_cast<int>(segment.x.size()) &&
                   index + 1 < static_cast<int>(segment.y.size())) {
                    const double x0 = segment.x[index];
                    const double x1 = segment.x[index + 1];
                    const double dx = x1 - x0;
                    if(dx != 0.0) {
                        const double u = (localT - x0) / dx;
                        const double inverse = 1.0 - u;
                        const double p0 = index < static_cast<int>(segment.p.size())
                            ? segment.p[index] : 0.0;
                        const double p1 = index + 1 < static_cast<int>(segment.p.size())
                            ? segment.p[index + 1] : 0.0;
                        parameter = dx * dx *
                                        ((u * u * u - u) * p1 +
                                         (inverse * inverse * inverse - inverse) * p0) /
                                        6.0 +
                                    u * segment.y[index + 1] +
                                    inverse * segment.y[index];
                    }
                }
            }
        }
        if(mainIndex < 3 || mainIndex >= static_cast<int>(cp.x.size()) ||
           mainIndex >= static_cast<int>(cp.y.size())) return;
        const double inverse = 1.0 - parameter;
        outXY[0] = inverse * inverse * inverse * cp.x[mainIndex - 3] +
                   3.0 * inverse * inverse * parameter * cp.x[mainIndex - 2] +
                   3.0 * inverse * parameter * parameter * cp.x[mainIndex - 1] +
                   parameter * parameter * parameter * cp.x[mainIndex];
        outXY[1] = inverse * inverse * inverse * cp.y[mainIndex - 3] +
                   3.0 * inverse * inverse * parameter * cp.y[mainIndex - 2] +
                   3.0 * inverse * parameter * parameter * cp.y[mainIndex - 1] +
                   parameter * parameter * parameter * cp.y[mainIndex];
    }

    inline void interpolateOfflinePosition(
        const BezierCurve &easingCurve,
        const double dst[3], const double src[3], double out[3],
        int coordinateMode, const ControlPointCurve &rotationCurve, double t) {
        if(src[0] == dst[0] && src[1] == dst[1] && src[2] == dst[2]) {
            std::copy(src, src + 3, out);
            return;
        }
        const double eased = !easingCurve.empty()
            ? evaluateOfflineBezierCurve(easingCurve, t) : t;
        if(rotationCurve.empty()) {
            for(int i = 0; i < 3; ++i)
                out[i] = src[i] != dst[i]
                    ? src[i] * (1.0 - eased) + dst[i] * eased : src[i];
            return;
        }
        double rotation[2] = {1.0, 0.0};
        evaluateOfflineControlPointCurve(rotation, rotationCurve, eased);
        if(coordinateMode == 0) {
            const double dx = dst[0] - src[0];
            const double dy = dst[1] - src[1];
            out[0] = src[0] + dx * rotation[0] - dy * rotation[1];
            out[1] = src[1] + dx * rotation[1] + dy * rotation[0];
            out[2] = src[2] != dst[2]
                ? src[2] * (1.0 - eased) + dst[2] * eased : src[2];
        } else if(coordinateMode == 1) {
            const double dx = dst[0] - src[0];
            const double dz = dst[2] - src[2];
            out[0] = src[0] + dx * rotation[0] - dz * rotation[1];
            out[1] = src[1] != dst[1]
                ? src[1] * (1.0 - eased) + dst[1] * eased : src[1];
            out[2] = src[2] + dz * rotation[0] + dx * rotation[1];
        } else {
            std::copy(src, src + 3, out);
        }
    }

    inline OfflineFrameContentState interpolateOfflineSlots(
        const OfflineFrameContentState &slotA,
        const OfflineFrameContentState &slotB,
        int coordinateMode, double t) {
        OfflineFrameContentState state = slotA;
        const auto lerp = [](double a, double b, double ratio) {
            return a * (1.0 - ratio) + b * ratio;
        };
        const double angleT = !state.acc.empty()
            ? evaluateOfflineBezierCurve(state.acc, t) : t;
        const double src[3] = {state.x, state.y, state.z};
        const double dst[3] = {slotB.x, slotB.y, slotB.z};
        double out[3] = {};
        interpolateOfflinePosition(
            state.ccc, dst, src, out, coordinateMode, state.cp, t);
        state.x = out[0];
        state.y = out[1];
        state.z = out[2];
        state.ox = lerp(state.ox, slotB.ox, t);
        state.oy = lerp(state.oy, slotB.oy, t);
        if(state.opacity != slotB.opacity) {
            const double value = lerp(state.opacity * 255.0,
                                      slotB.opacity * 255.0, t);
            const int rounded = value < 0.0
                ? static_cast<int>(std::ceil(value - 0.5))
                : static_cast<int>(std::floor(value + 0.5));
            state.opacity = std::clamp(rounded / 255.0, 0.0, 1.0);
        }
        state.packedColors = slotA.packedColors;
        double angleA = state.angle;
        double angleB = slotB.angle;
        if(angleA != angleB) {
            if(angleA >= angleB) {
                if(angleA - angleB > 180.0) angleB += 360.0;
            } else if(angleB - angleA > 180.0) {
                angleB -= 360.0;
            }
            state.angle = lerp(angleA, angleB, angleT);
            if(state.angle < 0.0) state.angle += 360.0;
            else if(state.angle >= 360.0) state.angle -= 360.0;
        }
        const double zoomT = !state.zcc.empty()
            ? evaluateOfflineBezierCurve(state.zcc, t) : t;
        if(state.scaleX != slotB.scaleX)
            state.scaleX = lerp(state.scaleX, slotB.scaleX, zoomT);
        if(state.scaleY != slotB.scaleY)
            state.scaleY = lerp(state.scaleY, slotB.scaleY, zoomT);
        const double slantT = !state.scc.empty()
            ? evaluateOfflineBezierCurve(state.scc, t) : t;
        if(state.slantX != slotB.slantX)
            state.slantX = lerp(state.slantX, slotB.slantX, slantT);
        if(state.slantY != slotB.slantY)
            state.slantY = lerp(state.slantY, slotB.slantY, slantT);
        if(state.width != slotB.width)
            state.width = lerp(state.width, slotB.width, t);
        if(state.height != slotB.height)
            state.height = lerp(state.height, slotB.height, t);
        if(state.src.empty() && !slotB.src.empty()) {
            state.src = slotB.src;
            state.icon = slotB.icon;
        }
        return state;
    }

    inline std::optional<double>
    psbNumberValue(const std::shared_ptr<PSB::IPSBValue> &value) {
        if(auto number = std::dynamic_pointer_cast<PSB::PSBNumber>(value)) {
            switch(number->numberType) {
                case PSB::PSBNumberType::Float:
                    return number->getValue<float>();
                case PSB::PSBNumberType::Double:
                    return number->getValue<double>();
                case PSB::PSBNumberType::Int:
                    return static_cast<double>(number->getValue<int>());
                case PSB::PSBNumberType::Long:
                default:
                    return static_cast<double>(number->getValue<tjs_int64>());
            }
        }
        if(auto boolean = std::dynamic_pointer_cast<PSB::PSBBool>(value)) {
            return boolean->value ? 1.0 : 0.0;
        }
        return std::nullopt;
    }

    inline std::optional<double>
    psbDictionaryNumber(const std::shared_ptr<const PSB::PSBDictionary> &dic,
                        const char *key) {
        if(!dic) {
            return std::nullopt;
        }
        return psbNumberValue((*dic)[key]);
    }

    inline std::string
    psbDictionaryString(const std::shared_ptr<const PSB::PSBDictionary> &dic,
                        const char *key) {
        if(!dic) {
            return {};
        }
        if(auto text =
               std::dynamic_pointer_cast<PSB::PSBString>((*dic)[key])) {
            return text->value;
        }
        return {};
    }

    inline std::shared_ptr<PSB::PSBList>
    psbDictionaryList(const std::shared_ptr<const PSB::PSBDictionary> &dic,
                      const char *key) {
        if(!dic) {
            return nullptr;
        }
        return std::dynamic_pointer_cast<PSB::PSBList>((*dic)[key]);
    }

    // Parse a BezierCurve from a PSB dict that has "x" and "y" list children.
    // Aligned to libkrkr2.so sub_69A754 (0x69A754): reads curve_data["x"]
    // and curve_data["y"] as arrays of doubles.
    inline BezierCurve parseBezierCurve(
        const std::shared_ptr<const PSB::PSBDictionary> &dic) {
        BezierCurve curve;
        if(!dic) return curve;
        auto xList = std::dynamic_pointer_cast<PSB::PSBList>((*dic)["x"]);
        auto yList = std::dynamic_pointer_cast<PSB::PSBList>((*dic)["y"]);
        if(!xList || !yList) return curve;
        for(int i = 0; i < static_cast<int>(xList->size()); i++) {
            if(auto v = psbNumberValue((*xList)[i])) curve.x.push_back(*v);
        }
        for(int i = 0; i < static_cast<int>(yList->size()); i++) {
            if(auto v = psbNumberValue((*yList)[i])) curve.y.push_back(*v);
        }
        return curve;
    }

    inline std::shared_ptr<PSB::PSBDictionary>
    psbDictionaryValue(const std::shared_ptr<const PSB::PSBDictionary> &dic,
                       const char *key) {
        if(!dic) {
            return nullptr;
        }
        return std::dynamic_pointer_cast<PSB::PSBDictionary>((*dic)[key]);
    }

    inline void mergeFrameContent(const std::shared_ptr<PSB::PSBDictionary> &content,
                           OfflineFrameContentState &state,
                           int nodeType) {
        if(!content) {
            return;
        }

        // Aligned to libkrkr2.so sub_692AB0 (0x692AB0):
        // the clip slot is already reset to defaults by sub_69260C/ParsedFrame,
        // then this routine applies only the fields selected by mask bits.
        const int mask = static_cast<int>(
            psbDictionaryNumber(content, "mask").value_or(0));

        // sub_692AB0 gates the "src"/"icon" lookup on ((1 << nodeType) & 0x1849).
        // This covers the initial source-handle block only; ox/oy/coord are handled
        // below by their own mask bits.
        const bool sourceGateEnabled =
            nodeType >= 0 && nodeType < 63 &&
            ((((std::uint64_t)1) << static_cast<unsigned>(nodeType)) &
             0x1849u) != 0;
        if(sourceGateEnabled) {
            state.icon = psbDictionaryString(content, "icon");
            if(const auto src = psbDictionaryString(content, "src"); !src.empty()) {
                state.src = src;
            } else if(auto srcList = psbDictionaryList(content, "src")) {
                for(size_t si = 0; si < srcList->size(); ++si) {
                    if(auto s = std::dynamic_pointer_cast<PSB::PSBString>((*srcList)[si])) {
                        state.src = s->value;
                        break;
                    }
                }
            }
        }

        // mask & 0x1: ox/oy (sub_692AB0 at 0x692DC4)
        if(mask & 0x1) {
            if(const auto ox = psbDictionaryNumber(content, "ox"))
                state.ox = *ox;
            if(const auto oy = psbDictionaryNumber(content, "oy"))
                state.oy = *oy;
        }

        // mask & 0x2: coord[x,y,z] (sub_692AB0 at 0x692E14).
        // Binary fetches content["coord"] then reads indices 0/1/2 via sub_6695BC.
        if(mask & 0x2) {
            if(const auto coord = psbDictionaryList(content, "coord")) {
                if(coord->size() > 0) {
                    if(const auto value = psbNumberValue((*coord)[0]))
                        state.x = *value;
                }
                if(coord->size() > 1) {
                    if(const auto value = psbNumberValue((*coord)[1]))
                        state.y = *value;
                }
                if(coord->size() > 2) {
                    if(const auto value = psbNumberValue((*coord)[2]))
                        state.z = *value;
                }
            } else if(auto coordDict = psbDictionaryValue(content, "coord")) {
                if(auto value = psbDictionaryNumber(coordDict, "0")) state.x = *value;
                if(auto value = psbDictionaryNumber(coordDict, "1")) state.y = *value;
                if(auto value = psbDictionaryNumber(coordDict, "2")) state.z = *value;
            }
        }

        // mask & 0x400: opa (sub_692AB0 at 0x693440)
        // CRITICAL: only read "opa" when mask bit 0x400 is set.
        // Offline normalized opacity defaults to 1.0.
        if(mask & 0x400) {
            if(const auto opa = psbDictionaryNumber(content, "opa"))
                state.opacity = std::clamp(*opa / 255.0, 0.0, 1.0);
        }

        // mask & 0x10: angle (sub_692AB0 at 0x692FC4)
        if(mask & 0x10) {
            if(const auto angle = psbDictionaryNumber(content, "angle"))
                state.angle = *angle;
        }

        // mask & 0x4: fx, mask & 0x8: fy (sub_692AB0 at 0x692F6C)
        if(mask & 0xC) {
            if(mask & 0x4) {
                if(const auto fx = psbDictionaryNumber(content, "fx"))
                    state.flipX = *fx != 0.0;
            }
            if(mask & 0x8) {
                if(const auto fy = psbDictionaryNumber(content, "fy"))
                    state.flipY = *fy != 0.0;
            }
        }

        // mask & 0x60: scaleX ("zx") / scaleY ("zy")
        // Aligned to libkrkr2.so sub_692AB0 at 0x693018:
        // both keys are read directly from the PSB content dict and stored
        // in the current clip slot. This is required by logo motions whose
        // opening backdrop uses zx/zy magnification on a tiny source image.
        if(mask & 0x60) {
            if(const auto zx = psbDictionaryNumber(content, "zx"))
                state.scaleX = *zx;
            if(const auto zy = psbDictionaryNumber(content, "zy"))
                state.scaleY = *zy;
        }

        // mask & 0x80: slantX ("sx") / mask & 0x100: slantY ("sy")
        // Aligned to sub_692AB0 at 0x69306C.
        // 0x14D869A: bytes 73 00 78 00 00 00 = UTF-16LE "sx" (IDA showed "s")
        if(mask & 0x180) {
            if(mask & 0x80) {
                if(const auto s = psbDictionaryNumber(content, "sx"))
                    state.slantX = *s;
            }
            if(mask & 0x100) {
                if(const auto sy = psbDictionaryNumber(content, "sy"))
                    state.slantY = *sy;
            }
        }

        // mask & 0x20000: bm/blend mode (sub_692AB0 at 0x692F20)
        if(mask & 0x20000) {
            if(const auto bm = psbDictionaryNumber(content, "bm"))
                state.blendMode = static_cast<int>(*bm);
        }

        // mask & 0x800: ccc/color curve control (sub_692AB0 at 0x6930DC)
        if(mask & 0x800) {
            if(auto cccDict = psbDictionaryValue(content, "ccc"))
                state.ccc = parseBezierCurve(cccDict);
        }

        // mask & 0x1000: acc/angle curve control (sub_692AB0 at 0x69319C)
        if(mask & 0x1000) {
            if(auto accDict = psbDictionaryValue(content, "acc"))
                state.acc = parseBezierCurve(accDict);
        }

        // mask & 0x2000: zcc/zoom curve control (sub_692AB0 at 0x6931FC)
        if(mask & 0x2000) {
            if(auto zccDict = psbDictionaryValue(content, "zcc"))
                state.zcc = parseBezierCurve(zccDict);
        }

        // mask & 0x4000: scc/slant curve control (sub_692AB0 at 0x69325C)
        if(mask & 0x4000) {
            if(auto sccDict = psbDictionaryValue(content, "scc"))
                state.scc = parseBezierCurve(sccDict);
        }

        // mask & 0x8000: occ/opacity curve control (sub_692AB0 at 0x69313C)
        if(mask & 0x8000) {
            if(auto occDict = psbDictionaryValue(content, "occ"))
                state.occ = parseBezierCurve(occDict);
        }

        // "cc" position curve (sub_692AB0 at 0x693580, slot+296)
        // Used by sub_69A4D4 to ease position interpolation t value.
        // Check done AFTER per-property curves, gated by slot+25 (crossfading)
        // and slot+22 (flipX) at 0x6932B8..0x6932C4.
        if(auto ccDict = psbDictionaryValue(content, "cc"))
            state.cc = parseBezierCurve(ccDict);

        // "cp" rotation control points (sub_692AB0 at 0x6932D8, slot+268)
        // Used by sub_698454 for spline rotation interpolation in sub_69A4D4.
        if(auto cpDict = psbDictionaryValue(content, "cp")) {
            auto cpxList = psbDictionaryList(cpDict, "x");
            auto cpyList = psbDictionaryList(cpDict, "y");
            auto cptList = psbDictionaryList(cpDict, "t");
            auto cpsList = psbDictionaryList(cpDict, "s");
            if (cpxList && cpyList && cptList) {
                for (size_t ci = 0; ci < cpxList->size(); ++ci)
                    if (auto v = psbNumberValue((*cpxList)[ci])) state.cp.x.push_back(*v);
                for (size_t ci = 0; ci < cpyList->size(); ++ci)
                    if (auto v = psbNumberValue((*cpyList)[ci])) state.cp.y.push_back(*v);
                for (size_t ci = 0; ci < cptList->size(); ++ci)
                    if (auto v = psbNumberValue((*cptList)[ci])) state.cp.t.push_back(*v);
                if (cpsList) {
                    for (size_t ci = 0; ci < cpsList->size(); ++ci) {
                        SplineSegment seg;
                        if (auto segDict = std::dynamic_pointer_cast<PSB::PSBDictionary>((*cpsList)[ci])) {
                            if (auto sx = psbDictionaryList(segDict, "x"))
                                for (size_t si = 0; si < sx->size(); ++si)
                                    if (auto v = psbNumberValue((*sx)[si])) seg.x.push_back(*v);
                            if (auto sy = psbDictionaryList(segDict, "y"))
                                for (size_t si = 0; si < sy->size(); ++si)
                                    if (auto v = psbNumberValue((*sy)[si])) seg.y.push_back(*v);
                            if (auto sp = psbDictionaryList(segDict, "p"))
                                for (size_t si = 0; si < sp->size(); ++si)
                                    if (auto v = psbNumberValue((*sp)[si])) seg.p.push_back(*v);
                        }
                        state.cp.s.push_back(std::move(seg));
                    }
                }
            }
        }

        // mask & 0x200: packed color payload (sub_692AB0 at
        // 0x692F4C..0x693428). Binary stores four packed RGBA DWORDs.
        if(mask & 0x200) {
            if(auto colorDict = psbDictionaryValue(content, "color")) {
                for(int ci = 0; ci < 4; ++ci) {
                    const auto key = std::to_string(ci);
                    if(auto value = psbDictionaryNumber(colorDict, key.c_str())) {
                        state.packedColors[ci] =
                            static_cast<std::uint32_t>(static_cast<std::int64_t>(*value));
                    }
                }
            } else if(auto colorVal = psbDictionaryNumber(content, "color")) {
                // Scalar color is broadcast to all four packed slots.
                const auto packed = static_cast<std::uint32_t>(
                    static_cast<std::int64_t>(*colorVal));
                state.packedColors = { packed, packed, packed, packed };
            }
        }

        // mask & 0x80000: motion sub-object (sub_692AB0 at 0x6938CC)
        // Full read: mask → flags/dt/docmpl/dofst/dtgt + timeOffset
        if(mask & 0x80000) {
            if(auto md = psbDictionaryValue(content, "motion")) {
                // sub_692AB0 @ 0x693988 initializes the motion block with
                // flags=0, dt=1, docmpl=false, dofst=0 before applying the
                // motion sub-mask overrides.
                state.motionDt = 1;
                int mm = static_cast<int>(
                    psbDictionaryNumber(md, "mask").value_or(0));
                state.motionMask = mm;
                if(mm & 0x1) {
                    if(auto v = psbDictionaryNumber(md, "flags"))
                        state.motionFlags = static_cast<int>(*v);
                }
                if(mm & 0x2) {
                    if(auto v = psbDictionaryNumber(md, "dt"))
                        state.motionDt = static_cast<int>(*v);
                }
                if(mm & 0x4) {
                    if(auto v = psbDictionaryNumber(md, "docmpl"))
                        state.motionDocmpl = *v != 0.0;
                }
                if(mm & 0x8) {
                    if(auto v = psbDictionaryNumber(md, "dofst"))
                        state.motionDofst = *v;
                }
                // dtgt (mm & 0x10): target node name string (sub_692AB0 at 0x693A48)
                if(mm & 0x10) {
                    auto v = psbDictionaryString(md, "dtgt");
                }
                if(auto v = psbDictionaryNumber(md, "timeOffset"))
                    state.motionTimeOffset = *v;
            }
        }

        // mask & 0x100000: particle sub-object (sub_692AB0 at 0x693C64)
        if(mask & 0x100000) {
            if(auto pd = psbDictionaryValue(content, "prt")) {
                int pm = static_cast<int>(
                    psbDictionaryNumber(pd, "mask").value_or(0));
                if(pm & 0x1) {
                    if(auto v = psbDictionaryNumber(pd, "trigger"))
                        state.prtTrigger = static_cast<int>(*v);
                }
                if(pm & 0x2) {
                    if(auto v = psbDictionaryNumber(pd, "fmin"))
                        state.prtFmin = *v;
                    if(auto v = psbDictionaryNumber(pd, "f"))
                        state.prtF = *v;
                }
                if(pm & 0x4) {
                    if(auto v = psbDictionaryNumber(pd, "vmin"))
                        state.prtVmin = *v;
                    if(auto v = psbDictionaryNumber(pd, "v"))
                        state.prtV = *v;
                }
                if(pm & 0x8) {
                    if(auto v = psbDictionaryNumber(pd, "amin"))
                        state.prtAmin = *v;
                    if(auto v = psbDictionaryNumber(pd, "a"))
                        state.prtA = *v;
                }
                if(pm & 0x10) {
                    if(auto v = psbDictionaryNumber(pd, "zmin"))
                        state.prtZmin = *v;
                    if(auto v = psbDictionaryNumber(pd, "z"))
                        state.prtZ = *v;
                }
                if(pm & 0x20) {
                    if(auto v = psbDictionaryNumber(pd, "range"))
                        state.prtRange = *v;
                }
            }
        }

        // mask & 0x200000: camera (sub_692AB0 at 0x693EF0)
        if(mask & 0x200000) {
            if(auto cd = psbDictionaryValue(content, "camera")) {
                if(auto v = psbDictionaryNumber(cd, "f"))
                    state.cameraFactor = *v;
                // camera.target is a string ref (sub_529524)
            }
        }

        // mask & 0x800000: anchor (sub_692AB0 at 0x694020)
        if(mask & 0x800000) {
            // anchor.target is a string ref — read via sub_529524
            // No numeric properties to store; the target ref links
            // to another node for position constraint.
        }

        // mask & 0x1000000: model (sub_692AB0 at 0x693AE8)
        if(mask & 0x1000000) {
            if(auto md = psbDictionaryValue(content, "model")) {
                if(auto v = psbDictionaryNumber(md, "timeOffset"))
                    state.modelTimeOffset = *v;
                if(auto v = psbDictionaryNumber(md, "loop"))
                    state.modelLoop = *v != 0.0;
                if(auto v = psbDictionaryNumber(md, "dt"))
                    state.modelDt = static_cast<int>(*v);
                // model.dtgt is a string ref
            }
        }

        // mask & 0x8000000: feedback (sub_692AB0 at 0x694130)
        if(mask & 0x8000000) {
            if(auto fd = psbDictionaryValue(content, "feedback")) {
                if(auto v = psbDictionaryNumber(fd, "timespan"))
                    state.feedbackTimespan = *v;
            }
        }

        // action/sync: not mask-gated (separate mechanism via mask & 0x40000
        // in sub_6926B4 at 0x6928EC)
        if(const auto act = psbDictionaryString(content, "action"); !act.empty()) {
        }
        if(const auto sync = psbDictionaryNumber(content, "sync")) {
            state.hasSync = *sync != 0.0;
        }
    }

    // Parse a single PSB frame entry: read time, type, content.
    // Aligned to libkrkr2.so sub_6926B4 (0x6926B4):
    //   - Reads frame["time"] (double), frame["type"] (int: 0=invisible, 2=static, 3=interpolate)
    //   - Reads frame["content"]["mask"] and calls sub_692AB0 to populate slot properties
    //   - Reads frame["content"]["act"] (action string, mask & 0x40000)
    struct ParsedFrame {
        double time = 0.0;
        int type = 0;        // 0=invisible, 2=static, 3=interpolate
        bool invisible = true;  // type==0
        bool interpolate = false; // type==3
        OfflineFrameContentState slot;  // offline decoded merge result
    };

    inline ParsedFrame
    parseFrame(const std::shared_ptr<PSB::PSBDictionary> &frame,
               int nodeType) {
        ParsedFrame result;
        if(!frame) return result;
        result.time = psbDictionaryNumber(frame, "time").value_or(0.0);
        result.type = static_cast<int>(
            psbDictionaryNumber(frame, "type").value_or(0.0));
        result.invisible = (result.type == 0);
        result.interpolate = (result.type == 3);
        result.slot.frameType = result.type;
        if(!result.invisible) {
            // sub_6926B4 at 0x692838: read content dict, then call sub_692AB0
            if(const auto content = psbDictionaryValue(frame, "content")) {
                mergeFrameContent(content, result.slot, nodeType);
            }
        }
        return result;
    }

    // Initialize an OfflineFrameContentState from one decoded PSB frame.
    // Convenience wrapper: calls parseFrame (sub_6926B4) and returns
    // the populated slot. Used by flattenLayerNodes (PSB-tree path).
    inline OfflineFrameContentState
    initSlotFromFrame(const std::shared_ptr<PSB::PSBDictionary> &frame,
                      int nodeType) {
        return parseFrame(frame, nodeType).slot;
    }

    // Evaluate layer content at a given time.
    // Orchestrator that calls:
    //   1. parseFrame (sub_6926B4) — parse each frame in frameList
    //   2. mergeFrameContent (sub_692AB0) — read mask-gated properties (called by parseFrame)
    //   3. interpolateOfflineSlots — offline dual-slot interpolation
    inline OfflineFrameContentState
    evaluateLayerContent(const std::shared_ptr<const PSB::PSBDictionary> &layer,
                         double time,
                         int nodeType) {
        OfflineFrameContentState state;
        const auto frames = psbDictionaryList(layer, "frameList");
        if(!frames || frames->size() == 0) {
            return state;
        }

        // Read transformOrder from layer dict (stored at node+84..96 in libkrkr2.so).
        // sub_699940 uses this to determine the order of Flip/Angle/Zoom/Slant.
        if(auto toList = psbDictionaryList(
               std::const_pointer_cast<PSB::PSBDictionary>(layer),
               "transformOrder")) {
            for(int i = 0; i < 4 && i < static_cast<int>(toList->size()); i++) {
                if(auto v = psbNumberValue((*toList)[i]))
                    state.transformOrder[i] = static_cast<int>(*v);
            }
            state.hasTransformOrder = true;
        }

        // Step 1: Find active frame (last frame with time <= time)
        int activeIndex = -1;
        for(size_t index = 0; index < frames->size(); ++index) {
            const auto frame = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                (*frames)[static_cast<int>(index)]);
            if(!frame) continue;
            const double frameTime =
                psbDictionaryNumber(frame, "time").value_or(0.0);
            if(frameTime > time) break;
            activeIndex = static_cast<int>(index);
        }

        if(activeIndex < 0) return state;
        state.debugEvaluated = true;
        state.debugActiveIndex = activeIndex;

        const auto activeFrame = std::dynamic_pointer_cast<PSB::PSBDictionary>(
            (*frames)[activeIndex]);
        if(!activeFrame) return state;

        // Step 2: Parse active frame via sub_6926B4
        ParsedFrame frameA = parseFrame(activeFrame, nodeType);
        state.debugFrameATime = frameA.time;
        state.debugFrameAType = frameA.type;
        state.debugFrameAInvisible = frameA.invisible;
        state.debugFrameAOpacity = frameA.slot.opacity;
        state.debugFrameAScaleX = frameA.slot.scaleX;
        state.debugFrameAScaleY = frameA.slot.scaleY;
        state.debugFrameASrc = frameA.slot.src;

        // type=0: node is invisible at this time
        if(frameA.invisible) {
            state.visible = false;
            return state;
        }

        // Preserve transformOrder from layer dict
        int savedTO[4]; bool savedHasTO = state.hasTransformOrder;
        std::copy(std::begin(state.transformOrder),
                  std::end(state.transformOrder), savedTO);
        state = frameA.slot;
        state.visible = true;
        state.clipStartTime = frameA.time;  // slot+328: frame start time
        state.debugEvaluated = true;
        state.debugActiveIndex = activeIndex;
        state.debugFrameATime = frameA.time;
        state.debugFrameAType = frameA.type;
        state.debugFrameAInvisible = frameA.invisible;
        state.debugFrameAOpacity = frameA.slot.opacity;
        state.debugFrameAScaleX = frameA.slot.scaleX;
        state.debugFrameAScaleY = frameA.slot.scaleY;
        state.debugFrameASrc = frameA.slot.src;
        if(savedHasTO) {
            std::copy(savedTO, savedTO + 4, state.transformOrder);
            state.hasTransformOrder = true;
        }

        // type=2: static display, no interpolation
        if(!frameA.interpolate) {
            return state;
        }

        // type=3: interpolate with next frame's slot
        const int nextIndex = activeIndex + 1;
        if(nextIndex >= static_cast<int>(frames->size())) {
            return state;  // no next frame, just use slot A
        }
        state.debugNextIndex = nextIndex;

        const auto nextFrame = std::dynamic_pointer_cast<PSB::PSBDictionary>(
            (*frames)[nextIndex]);
        if(!nextFrame) return state;

        // Step 3: Parse next frame via sub_6926B4
        ParsedFrame frameB = parseFrame(nextFrame, nodeType);
        state.debugFrameBTime = frameB.time;
        state.debugFrameBType = frameB.type;
        state.debugFrameBInvisible = frameB.invisible;
        state.debugFrameBOpacity = frameB.slot.opacity;
        state.debugFrameBScaleX = frameB.slot.scaleX;
        state.debugFrameBScaleY = frameB.slot.scaleY;
        state.debugFrameBSrc = frameB.slot.src;
        // Inherit src from slot A if slot B doesn't set one
        if(frameB.slot.src.empty()) frameB.slot.src = state.src;

        // Compute interpolation ratio
        const double duration = frameB.time - frameA.time;
        if(duration <= 0.0) return state;

        const double t = std::clamp(
            (time - frameA.time) / duration, 0.0, 1.0);
        state.debugInterpT = t;

        if(t <= 0.0 || frameB.invisible) {
            return state;  // at exact start or next is invisible
        }

        // Step 4: Interpolate via sub_699AE4
        state = interpolateOfflineSlots(state, frameB.slot, 0, t);
        state.visible = true;
        state.debugEvaluated = true;
        state.debugActiveIndex = activeIndex;
        state.debugNextIndex = nextIndex;
        state.debugFrameATime = frameA.time;
        state.debugFrameAType = frameA.type;
        state.debugFrameAInvisible = frameA.invisible;
        state.debugFrameAOpacity = frameA.slot.opacity;
        state.debugFrameAScaleX = frameA.slot.scaleX;
        state.debugFrameAScaleY = frameA.slot.scaleY;
        state.debugFrameASrc = frameA.slot.src;
        state.debugFrameBTime = frameB.time;
        state.debugFrameBType = frameB.type;
        state.debugFrameBInvisible = frameB.invisible;
        state.debugFrameBOpacity = frameB.slot.opacity;
        state.debugFrameBScaleX = frameB.slot.scaleX;
        state.debugFrameBScaleY = frameB.slot.scaleY;
        state.debugFrameBSrc = frameB.slot.src;
        state.debugInterpT = t;
        state.debugInterpolated = true;

        if(savedHasTO) {
            std::copy(savedTO, savedTO + 4, state.transformOrder);
            state.hasTransformOrder = true;
        }

        return state;
    }


    // Return true if a source path is a motion cross-reference
    // (e.g. "motion/title_bg/char_move"), not an image source.
    inline bool isMotionCrossReference(const std::string &src) {
        return src.rfind("motion/", 0) == 0;
    }

    // PSB RL decompression: each RGBA channel is separately RL-compressed.
    // Format per channel: stream of [marker] entries where
    //   marker & 0x80 → repeat (marker & 0x7F + 1) copies of next byte
    //   otherwise      → (marker + 1) literal bytes follow
    // Aligned to libkrkr2.so via FreeMote PSB RL spec.
    // PSB RL decompression — two variants based on libkrkr2.so sub_695DE8:
    //
    // align=1 (with palette): single-byte RLE, used with 8-bit indexed data
    //   RLE run:  count = (marker & 0x7F) + 3, repeat 1 byte
    //   Literal:  count = marker + 1, copy count bytes
    //
    // align=4 (no palette, RGBA8): 4-byte RLE, used with 32-bit pixel data
    //   RLE run:  count = (marker & 0x7F) + 3, repeat 4 bytes
    //   Literal:  count = marker + 1, copy count*4 bytes
    //   (0x696D00-0x696D98 in libkrkr2.so)
    inline std::vector<std::uint8_t> decompressPsbRL(
        const std::vector<std::uint8_t> &compressed,
        size_t elementCount, int align = 4) {
        const size_t outputSize = elementCount * static_cast<size_t>(align);
        std::vector<std::uint8_t> output(outputSize, 0);

        const auto *src = compressed.data();
        const auto *srcEnd = src + compressed.size();
        auto *dst = output.data();
        const auto *dstEnd = dst + outputSize;

        while(src < srcEnd && dst < dstEnd) {
            const auto marker = *src++;
            if(marker & 0x80) {
                // RLE run: repeat `align` bytes (count) times
                const size_t count = (marker & 0x7F) + 3;
                if(src + align > srcEnd) break;
                for(size_t i = 0; i < count && dst + align <= dstEnd; i++) {
                    std::memcpy(dst, src, align);
                    dst += align;
                }
                src += align;
            } else {
                // Literal: copy (marker+1)*align bytes verbatim
                const size_t count = (marker + 1) * static_cast<size_t>(align);
                if(src + count > srcEnd) break;
                const size_t n = std::min(count,
                    static_cast<size_t>(dstEnd - dst));
                std::memcpy(dst, src, n);
                src += count;
                dst += n;
            }
        }
        return output;
    }

    inline bool decodePsbPixelResource(
        const detail::MotionSnapshot &snapshot,
        const std::string &iconPath,
        const PSB::PSBResource &pixelResource,
        int width,
        int height,
        bool isRLCompressed,
        std::vector<std::uint8_t> &decodedOut,
        bool *outDecodedIsBgra = nullptr) {
        decodedOut.clear();
        if(outDecodedIsBgra) {
            *outDecodedIsBgra = false;
        }

        if(width <= 0 || height <= 0 || pixelResource.data.empty()) {
            return false;
        }

        const size_t pixelCount =
            static_cast<size_t>(width) * static_cast<size_t>(height);
        const auto palPath = iconPath + "/pal";
        const auto palIt = snapshot.resourcesByPath.find(palPath);
        const bool hasPalette = palIt != snapshot.resourcesByPath.end() &&
            palIt->second && !palIt->second->data.empty();

        if(hasPalette) {
            std::vector<std::uint8_t> indexBuffer;
            if(isRLCompressed) {
                indexBuffer = decompressPsbRL(pixelResource.data,
                                              pixelCount, 1);
            } else {
                indexBuffer.resize(pixelCount, 0);
                const size_t copyCount = std::min(pixelCount,
                                                  pixelResource.data.size());
                std::memcpy(indexBuffer.data(), pixelResource.data.data(),
                            copyCount);
            }

            const size_t paletteEntryCount =
                palIt->second->data.size() / sizeof(tjs_uint32);
            if(paletteEntryCount == 0) {
                return false;
            }

            std::vector<tjs_uint32> rawPalette(paletteEntryCount, 0);
            std::memcpy(rawPalette.data(), palIt->second->data.data(),
                        paletteEntryCount * sizeof(tjs_uint32));
            std::vector<tjs_uint32> bgraPalette(paletteEntryCount, 0);
            TVPReverseRGB(bgraPalette.data(), rawPalette.data(),
                          static_cast<tjs_int>(paletteEntryCount));

            std::vector<tjs_uint32> expandedPixels(pixelCount, 0);
            TVPBLExpand8BitTo32BitPal(
                expandedPixels.data(), indexBuffer.data(),
                static_cast<tjs_int>(pixelCount), bgraPalette.data());

            decodedOut.resize(pixelCount * sizeof(tjs_uint32));
            std::memcpy(decodedOut.data(), expandedPixels.data(),
                        decodedOut.size());
            if(outDecodedIsBgra) {
                *outDecodedIsBgra = true;
            }
            return true;
        }

        if(isRLCompressed) {
            decodedOut = decompressPsbRL(pixelResource.data, pixelCount, 4);
            return !decodedOut.empty();
        }

        return false;
    }

    inline std::string basenameWithoutExtension(const std::string &value) {
        const auto slash = value.find_last_of("/\\");
        const auto fileName =
            slash == std::string::npos ? value : value.substr(slash + 1);
        const auto dot = fileName.find_last_of('.');
        return dot == std::string::npos ? fileName : fileName.substr(0, dot);
    }

    // -----------------------------------------------------------------
    // Layer-API based rendering (no OpenCV dependency, used in web build)
    // -----------------------------------------------------------------

    // Navigate a PSB dictionary tree by a slash-separated path.
    inline std::shared_ptr<const PSB::PSBDictionary> navigatePSBPath(
        const std::shared_ptr<const PSB::PSBDictionary> &root,
        const std::string &path) {
        if(!root || path.empty()) return nullptr;
        auto node = root;
        std::istringstream pathStream(path);
        std::string segment;
        while(std::getline(pathStream, segment, '/')) {
            if(segment.empty() || !node) continue;
            auto child = std::dynamic_pointer_cast<const PSB::PSBDictionary>(
                (*node)[segment]);
            if(!child) return nullptr;
            node = child;
        }
        return node;
    }

    // Find a PSB resource node by source name. The motion layer `src`
    // field uses paths like "src/title/bg" and the PSB tree stores
    // resources under "source/title/icon/bg/pixel".
    // Aligned to libkrkr2.so sub_6948E8: navigates source/<group>/icon/<name>.
    // Also reads originX/originY from the icon node (image anchor point,
    // used in sub_6BC4F0: origin = pos - matrix × (originX, originY)).
    // If the resource is RL-compressed or palettized, decodes into
    // decompressedOut. Palettized output matches libkrkr2.so's BGRA path.
    inline const PSB::PSBResource *findPSBResourceBySourceName(
        const detail::MotionSnapshot &snapshot,
        const std::string &source,
        int &outWidth, int &outHeight,
        std::vector<std::uint8_t> &decompressedOut,
        double &outOriginX, double &outOriginY,
        bool *outDecodedIsBgra = nullptr) {
        outWidth = 0;
        outHeight = 0;
        outOriginX = 0.0;
        outOriginY = 0.0;
        decompressedOut.clear();
        if(outDecodedIsBgra) {
            *outDecodedIsBgra = false;
        }
        if(source.empty() || isMotionCrossReference(source)) {
            return nullptr;
        }

        // Strategy 1: Parse src/<group>/<name> and navigate directly
        // to source/<group>/icon/<name> in the PSB tree.
        // This is the primary path aligned to libkrkr2.so sub_6948E8.
        if(source.rfind("src/", 0) == 0 && snapshot.root) {
            // Parse "src/<group>/<name>" → group, name
            const auto afterSrc = source.substr(4); // skip "src/"
            const auto slash = afterSrc.find('/');
            if(slash != std::string::npos) {
                const auto group = afterSrc.substr(0, slash);
                const auto name = afterSrc.substr(slash + 1);
                // Navigate: source/<group>/icon/<name>
                const auto iconPath =
                    "source/" + group + "/icon/" + name;
                auto iconNode = navigatePSBPath(snapshot.root, iconPath);
                if(iconNode) {
                    // Read width/height from the icon node
                    if(auto w = psbDictionaryNumber(iconNode, "width"))
                        outWidth = static_cast<int>(*w);
                    if(auto h = psbDictionaryNumber(iconNode, "height"))
                        outHeight = static_cast<int>(*h);
                    if(outWidth <= 0) {
                        if(auto tw = psbDictionaryNumber(iconNode,
                                         "truncated_width"))
                            outWidth = static_cast<int>(*tw);
                    }
                    if(outHeight <= 0) {
                        if(auto th = psbDictionaryNumber(iconNode,
                                         "truncated_height"))
                            outHeight = static_cast<int>(*th);
                    }
                    // Read origin (anchor point) from icon node
                    // Aligned to libkrkr2.so sub_6BC4F0: used as
                    // origin = pos - matrix × (originX, originY)
                    if(auto ox = psbDictionaryNumber(iconNode, "originX"))
                        outOriginX = *ox;
                    if(auto oy = psbDictionaryNumber(iconNode, "originY"))
                        outOriginY = *oy;

                    const auto iconLeft =
                        static_cast<int>(
                            psbDictionaryNumber(iconNode, "left")
                                .value_or(0.0));
                    const auto iconTop =
                        static_cast<int>(
                            psbDictionaryNumber(iconNode, "top")
                                .value_or(0.0));
                    const auto texturePath =
                        "source/" + group + "/texture";
                    auto textureNode =
                        navigatePSBPath(snapshot.root, texturePath);
                    if(textureNode && outWidth > 0 && outHeight > 0) {
                        int textureWidth = 0;
                        int textureHeight = 0;
                        if(auto w =
                               psbDictionaryNumber(textureNode, "width"))
                            textureWidth = static_cast<int>(*w);
                        if(auto h =
                               psbDictionaryNumber(textureNode, "height"))
                            textureHeight = static_cast<int>(*h);
                        if(textureWidth <= 0) {
                            if(auto tw = psbDictionaryNumber(
                                   textureNode, "truncated_width"))
                                textureWidth = static_cast<int>(*tw);
                        }
                        if(textureHeight <= 0) {
                            if(auto th = psbDictionaryNumber(
                                   textureNode, "truncated_height"))
                                textureHeight = static_cast<int>(*th);
                        }

                        const auto texturePixelPath =
                            texturePath + "/pixel";
                        auto textureResIt =
                            snapshot.resourcesByPath.find(texturePixelPath);
                        if(textureResIt != snapshot.resourcesByPath.end() &&
                           textureResIt->second &&
                           !textureResIt->second->data.empty() &&
                           textureWidth > 0 && textureHeight > 0 &&
                           iconLeft >= 0 && iconTop >= 0 &&
                           iconLeft + outWidth <= textureWidth &&
                           iconTop + outHeight <= textureHeight) {
                            bool texturePixelsAreBgra = false;
                            std::vector<std::uint8_t> texturePixels;
                            const auto textureCompress =
                                psbDictionaryString(textureNode,
                                                    "compress");
                            decodePsbPixelResource(
                                snapshot, texturePath,
                                *textureResIt->second,
                                textureWidth, textureHeight,
                                textureCompress == "RL",
                                texturePixels, &texturePixelsAreBgra);
                            const auto &pixelData =
                                texturePixels.empty()
                                    ? textureResIt->second->data
                                    : texturePixels;
                            const size_t requiredSize =
                                static_cast<size_t>(textureWidth) *
                                static_cast<size_t>(textureHeight) * 4u;
                            if(pixelData.size() >= requiredSize) {
                                decompressedOut.assign(
                                    static_cast<size_t>(outWidth) *
                                        static_cast<size_t>(outHeight) * 4u,
                                    0);
                                const size_t targetStride =
                                    static_cast<size_t>(outWidth) * 4u;
                                for(int y = 0; y < outHeight; ++y) {
                                    const size_t sourceOffset =
                                        (static_cast<size_t>(iconTop + y) *
                                             static_cast<size_t>(
                                                 textureWidth) +
                                         static_cast<size_t>(iconLeft)) *
                                        4u;
                                    std::memcpy(
                                        decompressedOut.data() +
                                            static_cast<size_t>(y) *
                                                targetStride,
                                        pixelData.data() + sourceOffset,
                                        targetStride);
                                }
                                if(outDecodedIsBgra) {
                                    *outDecodedIsBgra =
                                        !texturePixels.empty() &&
                                        texturePixelsAreBgra;
                                }
                                return textureResIt->second.get();
                            }
                        }
                    }

                    // Fallback: older local assets may carry per-icon pixels.
                    const auto pixelPath = iconPath + "/pixel";
                    auto resIt = snapshot.resourcesByPath.find(pixelPath);
                    if(resIt != snapshot.resourcesByPath.end() &&
                       !resIt->second->data.empty() &&
                       outWidth > 0 && outHeight > 0) {
                        auto compressStr =
                            psbDictionaryString(iconNode, "compress");
                        decodePsbPixelResource(
                            snapshot, iconPath, *resIt->second,
                            outWidth, outHeight, compressStr == "RL",
                            decompressedOut, outDecodedIsBgra);
                        return resIt->second.get();
                    }
                }
            }
        }

        // Strategy 2 (fallback): Search resourcesByPath for a key
        // ending with /<baseName>/pixel.
        const auto lastSlash = source.rfind('/');
        const auto baseName = (lastSlash != std::string::npos)
            ? source.substr(lastSlash + 1) : source;

        for(const auto &[resPath, resource] : snapshot.resourcesByPath) {
            const auto targetSuffix = "/" + baseName + "/pixel";
            if(resPath.size() >= targetSuffix.size() &&
               resPath.compare(resPath.size() - targetSuffix.size(),
                               targetSuffix.size(), targetSuffix) == 0) {
                // Found the pixel resource — read dims from parent node
                const auto parentPath =
                    resPath.substr(0, resPath.size() - 6); // strip "/pixel"
                if(snapshot.root) {
                    auto node = navigatePSBPath(snapshot.root, parentPath);
                    if(node) {
                        if(auto w = psbDictionaryNumber(node, "width"))
                            outWidth = static_cast<int>(*w);
                        if(auto h = psbDictionaryNumber(node, "height"))
                            outHeight = static_cast<int>(*h);
                        if(outWidth <= 0) {
                            if(auto tw = psbDictionaryNumber(node,
                                             "truncated_width"))
                                outWidth = static_cast<int>(*tw);
                        }
                        if(outHeight <= 0) {
                            if(auto th = psbDictionaryNumber(node,
                                             "truncated_height"))
                                outHeight = static_cast<int>(*th);
                        }
                        auto compressStr =
                            psbDictionaryString(node, "compress");
                        if(outWidth > 0 && outHeight > 0) {
                            decodePsbPixelResource(
                                snapshot, parentPath, *resource,
                                outWidth, outHeight, compressStr == "RL",
                                decompressedOut, outDecodedIsBgra);
                        }
                    }
                }
                if(outWidth > 0 && outHeight > 0 &&
                   !resource->data.empty()) {
                    return resource.get();
                }
            }
        }
        return nullptr;
    }

    // Write raw BGRA pixel data from a PSB resource to a Layer
    // Load PSB resource pixel data into a layer.
    // Based on libkrkr2.so sub_6948E8: PSB texture data is raw RGBA8
    // pixels (not RL-compressed). The data size may be smaller than
    // width*height*4 — only data.size()/4 pixels are valid, the rest
    // should be zero (transparent). RGB order is swapped to BGRA for
    // TJS layers (TVPReverseRGB in libkrkr2.so).
    inline bool loadPSBResourceToLayer(
        tTJSNI_BaseLayer *layer,
        const PSB::PSBResource &resource,
        int width, int height) {
        if(!layer || width <= 0 || height <= 0 || resource.data.empty()) {
            return false;
        }

        if(!layer->GetHasImage()) {
            layer->SetHasImage(true);
        }
        layer->SetImageSize(static_cast<tjs_uint>(width),
                            static_cast<tjs_uint>(height));

        auto *dstPixels = reinterpret_cast<std::uint8_t *>(
            layer->GetMainImagePixelBufferForWrite());
        const auto pitch = layer->GetMainImagePixelBufferPitch();
        if(!dstPixels || pitch <= 0) {
            return false;
        }

        // Zero-fill the entire layer buffer first
        const auto totalRows = static_cast<size_t>(height);
        for(size_t row = 0; row < totalRows; ++row) {
            std::memset(dstPixels + pitch * row, 0,
                        static_cast<size_t>(width) * 4u);
        }

        // Copy raw RGBA8 data, swapping R↔B → BGRA (TJS format)
        const size_t pixelCount = resource.data.size() / 4u;
        const auto *src = resource.data.data();
        for(size_t i = 0; i < pixelCount; ++i) {
            const size_t px = i % static_cast<size_t>(width);
            const size_t py = i / static_cast<size_t>(width);
            if(py >= totalRows) break;
            auto *dst = dstPixels + pitch * py + px * 4;
            dst[0] = src[i * 4 + 2]; // B ← src R
            dst[1] = src[i * 4 + 1]; // G ← src G
            dst[2] = src[i * 4 + 0]; // R ← src B
            dst[3] = src[i * 4 + 3]; // A ← src A
        }
        return true;
    }

} // namespace motion::internal
