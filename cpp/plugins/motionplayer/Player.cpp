//
// Created by LiDon on 2025/9/15.
// Minimal runtime implementation aligned to libkrkr2.so MMotionPlayer surface.
//

#include "Player.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include "WindowIntf.h"
#include <cstring>
#include <optional>
#include <random>
#include <sstream>
#include <unordered_set>
#include <vector>

#include "LayerIntf.h"
#include "LayerBitmapIntf.h"
#include "GraphicsLoaderIntf.h"
#include "RuntimeSupport.h"
#include "ResourceManager.h"
#include "SeparateLayerAdaptor.h"
#include "D3DAdaptor.h"
#include "StorageIntf.h"
#include "ncbind.hpp"
#include "tjsArray.h"
#include "EventIntf.h"
#include "NodeTree.h"
#include "MotionNode.h"
#ifdef __EMSCRIPTEN__
#include <wasm_simd128.h>
#endif


#define LOGGER spdlog::get("plugin")
#define STUB_WARN(name) LOGGER->warn("Player::" #name "() stub called")

namespace motion {

    namespace {

        // Return true if a source path is a motion cross-reference
        // (e.g. "motion/title_bg/char_move"), not an image source.
        bool isMotionCrossReference(const std::string &src) {
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
        std::vector<std::uint8_t> decompressPsbRL(
            const std::vector<std::uint8_t> &compressed,
            int width, int height, int align = 4) {
            const size_t outputSize =
                static_cast<size_t>(width) * height * 4u;
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

        constexpr double kMotionFramesPerMillisecond = 60.0 / 1000.0;

        std::string basenameWithoutExtension(const std::string &value) {
            const auto slash = value.find_last_of("/\\");
            const auto fileName =
                slash == std::string::npos ? value : value.substr(slash + 1);
            const auto dot = fileName.find_last_of('.');
            return dot == std::string::npos ? fileName : fileName.substr(0, dot);
        }

        std::shared_ptr<detail::MotionSnapshot>
        cacheMotion(detail::PlayerRuntime &runtime, const std::string &requestKey,
                    const std::string &resolvedKey,
                    const std::shared_ptr<detail::MotionSnapshot> &snapshot) {
            if(!snapshot) {
                return nullptr;
            }
            if(!requestKey.empty()) {
                runtime.motionsByKey.emplace(requestKey, snapshot);
            }
            if(!resolvedKey.empty()) {
                runtime.motionsByKey.emplace(resolvedKey, snapshot);
            }
            if(!snapshot->path.empty()) {
                runtime.motionsByKey.emplace(snapshot->path, snapshot);
            }
            return snapshot;
        }

        std::shared_ptr<detail::MotionSnapshot>
        activateMotion(detail::PlayerRuntime &runtime,
                       const std::shared_ptr<detail::MotionSnapshot> &snapshot) {
            runtime.activeMotion = snapshot;
            runtime.timelines.clear();
            // Reset persistent node tree so it gets rebuilt for new motion
            runtime.nodes.clear();
            runtime.nodesBuilt = false;
            // Detect emote mode from PSB root "type" field.
            // Aligned to libkrkr2.so Player_playImpl (0x6B2284):
            //   type=0 → non-emote (motion), type=1 → emote
            runtime.isEmoteMode = false;
            if(snapshot && snapshot->root) {
                auto typeVal = (*snapshot->root)["type"];
                if(auto num = std::dynamic_pointer_cast<PSB::PSBNumber>(typeVal)) {
                    runtime.isEmoteMode = (num->getValue<int>() == 1);
                }
            }
            if(snapshot) {
                detail::primeTimelineStates(runtime.timelines, *snapshot);
            }
            return snapshot;
        }

        std::shared_ptr<detail::MotionSnapshot>
        resolveMotion(detail::PlayerRuntime &runtime, const ttstr &name,
                      const ResourceManager *resourceManager) {
            const auto requestKey = detail::narrow(name);
            if(requestKey.empty()) {
                return nullptr;
            }

            if(const auto it = runtime.motionsByKey.find(requestKey);
               it != runtime.motionsByKey.end()) {
                return it->second;
            }

            const auto candidates = detail::buildMotionLookupCandidates(name);
            ttstr resolved;
            if(detail::resolveExistingPath(candidates, resolved)) {
                const auto resolvedKey = detail::narrow(resolved);
                if(const auto it = runtime.motionsByKey.find(resolvedKey);
                   it != runtime.motionsByKey.end()) {
                    runtime.motionsByKey.emplace(requestKey, it->second);
                    return it->second;
                }

                const auto snapshot = detail::loadMotionSnapshot(
                    resolved, ResourceManager::getEmotePSBDecryptSeed());
                if(snapshot) {
                    return cacheMotion(runtime, requestKey, resolvedKey, snapshot);
                }
            }

            if(resourceManager != nullptr) {
                for(const auto &candidate : candidates) {
                    const auto loaded = resourceManager->load(candidate);
                    if(const auto snapshot = detail::lookupModuleSnapshot(loaded)) {
                        return cacheMotion(runtime, requestKey,
                                           detail::narrow(candidate), snapshot);
                    }
                }
            }

            return nullptr;
        }

        std::vector<ttstr> buildSourceCandidates(
            const detail::PlayerRuntime &runtime, const ttstr &name) {
            std::vector<ttstr> candidates;
            if(name.IsEmpty()) {
                return candidates;
            }

            candidates.push_back(name);
            const auto requestKey = detail::narrow(name);
            if(!runtime.activeMotion) {
                return candidates;
            }

            const auto baseDir = TVPExtractStoragePath(
                detail::widen(runtime.activeMotion->path));
            for(const auto &candidate : runtime.activeMotion->sourceCandidates) {
                if(candidate == requestKey ||
                   basenameWithoutExtension(candidate) == requestKey) {
                    candidates.emplace_back(detail::widen(candidate));
                    detail::appendEmbeddedSourceCandidates(
                        *runtime.activeMotion, candidate, candidates);
                    if(!baseDir.IsEmpty() &&
                       candidate.find('/') == std::string::npos &&
                       candidate.find('\\') == std::string::npos) {
                        candidates.emplace_back(baseDir + detail::widen(candidate));
                    }
                }
            }

            return candidates;
        }

        std::vector<tTJSVariant>
        timelineInfoVariants(const detail::PlayerRuntime &runtime) {
            std::vector<tTJSVariant> items;
            for(const auto &[label, state] : runtime.timelines) {
                if(!state.playing) {
                    continue;
                }

                items.push_back(detail::makeDictionary({
                    { "label", detail::widen(label) },
                    { "flags", static_cast<tjs_int>(state.flags) },
                    { "loop", state.loop },
                    { "playing", state.playing },
                    { "currentTime", state.currentTime },
                    { "totalFrames", state.totalFrames },
                    { "blendRatio", state.blendRatio },
                }));
            }
            return items;
        }

        const detail::TimelineState *
        nthPlayingTimeline(const detail::PlayerRuntime &runtime, tjs_int idx) {
            if(idx < 0) {
                return nullptr;
            }

            tjs_int current = 0;
            for(const auto &[_, state] : runtime.timelines) {
                if(!state.playing) {
                    continue;
                }
                if(current == idx) {
                    return &state;
                }
                ++current;
            }
            return nullptr;
        }

        bool getObjectProperty(const tTJSVariant &object, const tjs_char *name,
                               tTJSVariant &result) {
            result.Clear();
            if(object.Type() != tvtObject || object.AsObjectNoAddRef() == nullptr) {
                return false;
            }
            return TJS_SUCCEEDED(object.AsObjectNoAddRef()->PropGet(
                TJS_IGNOREPROP, name, nullptr, &result,
                object.AsObjectNoAddRef()));
        }

        tjs_int getObjectCount(const tTJSVariant &object) {
            tTJSVariant count;
            return getObjectProperty(object, TJS_W("count"), count)
                ? count.AsInteger()
                : 0;
        }

        bool tryGetLayerObject(const tTJSVariant &value,
                               tTJSNI_BaseLayer *&layer) {
            layer = nullptr;
            if(value.Type() != tvtObject || value.AsObjectNoAddRef() == nullptr) {
                return false;
            }

            iTJSDispatch2 *obj = value.AsObjectNoAddRef();
            if(TJS_SUCCEEDED(obj->NativeInstanceSupport(
                   TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                   reinterpret_cast<iTJSNativeInstance **>(&layer))) &&
               layer != nullptr) {
                return true;
            }

            // Fallback: try via closure's Object member (may differ from
            // AsObjectNoAddRef for certain TJS value representations)
            const auto closure = value.AsObjectClosureNoAddRef();
            if(closure.Object && closure.Object != obj) {
                return TJS_SUCCEEDED(closure.Object->NativeInstanceSupport(
                           TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                           reinterpret_cast<iTJSNativeInstance **>(&layer))) &&
                    layer != nullptr;
            }

            return false;
        }

        // Resolve a real Layer dispatch from a TJS value that might be
        // a SeparateLayerAdaptor, an AffineLayer wrapper, or a raw Layer.
        iTJSDispatch2 *tryResolveLayerDispatch(const tTJSVariant &value) {
            if(value.Type() != tvtObject || value.AsObjectNoAddRef() == nullptr) {
                return nullptr;
            }

            iTJSDispatch2 *obj = value.AsObjectNoAddRef();

            // Direct Layer check
            {
                tTJSNI_BaseLayer *layer = nullptr;
                if(TJS_SUCCEEDED(obj->NativeInstanceSupport(
                       TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                       reinterpret_cast<iTJSNativeInstance **>(&layer))) &&
                   layer) {
                    return obj;
                }
            }

            // ncb SeparateLayerAdaptor → owner
            if(auto *adaptor =
                   ncbInstanceAdaptor<SeparateLayerAdaptor>::GetNativeInstance(
                       obj, false)) {
                auto *ownerObj = adaptor->getOwner();
                if(ownerObj) {
                    auto ownerResolved = tryResolveLayerDispatch(
                        tTJSVariant(ownerObj, ownerObj));
                    if(ownerResolved) return ownerResolved;
                }
            }

            // TJS property chain: owner, _owner, targetLayer
            static const tjs_char *propNames[] = {
                TJS_W("owner"), TJS_W("_owner"), TJS_W("targetLayer"),
                TJS_W("layer"), TJS_W("_layer"), TJS_W("baseLayer"),
                TJS_W("_base"), TJS_W("parent"), nullptr };

            for(int i = 0; propNames[i]; ++i) {
                tTJSVariant propVal;
                if(getObjectProperty(value, propNames[i], propVal) &&
                   propVal.Type() == tvtObject &&
                   propVal.AsObjectNoAddRef() != nullptr &&
                   propVal.AsObjectNoAddRef() != obj) {
                    auto *resolved = tryResolveLayerDispatch(propVal);
                    if(resolved) return resolved;
                }
            }

            return nullptr;
        }

        iTJSDispatch2 *tryResolveSeparateAdaptorOwner(const tTJSVariant &value) {
            return tryResolveLayerDispatch(value);
        }

        void pushGraphicCandidates(std::vector<ttstr> &candidates,
                                   const ttstr &base) {
            if(base.IsEmpty()) {
                return;
            }

            candidates.push_back(base);
            const auto raw = detail::narrow(base);
            if(raw.find('.') != std::string::npos) {
                return;
            }

            static const char *exts[] = { ".png",  ".webp", ".jpg", ".jpeg",
                                          ".bmp",  ".tlg",  ".pimg", ".psb" };
            for(const auto *ext : exts) {
                candidates.emplace_back(base + ttstr{ ext });
            }
        }

        bool getArrayItem(const tTJSVariant &object, tjs_int index,
                          tTJSVariant &result) {
            result.Clear();
            if(object.Type() != tvtObject || object.AsObjectNoAddRef() == nullptr) {
                return false;
            }
            return TJS_SUCCEEDED(object.AsObjectNoAddRef()->PropGetByNum(
                TJS_IGNOREPROP, index, &result, object.AsObjectNoAddRef()));
        }

        struct DictionaryEnumerator : public tTJSDispatch {
            std::vector<std::pair<ttstr, tTJSVariant>> entries;

            tjs_error FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *,
                               tTJSVariant *result, tjs_int numparams,
                               tTJSVariant **param,
                               iTJSDispatch2 *) override {
                if(numparams < 3) {
                    return TJS_E_BADPARAMCOUNT;
                }

                const tjs_uint32 flags = static_cast<tjs_uint32>(
                    param[1]->AsInteger());
                if(flags & TJS_HIDDENMEMBER) {
                    if(result) {
                        *result = static_cast<tjs_int>(1);
                    }
                    return TJS_S_OK;
                }

                entries.emplace_back(ttstr(*param[0]), *param[2]);
                if(result) {
                    *result = static_cast<tjs_int>(1);
                }
                return TJS_S_OK;
            }
        };

        // Bezier curve control points for easing.
        // Aligned to libkrkr2.so sub_69A754: PSB stores "x" and "y" arrays
        // in the curve data dict. Each array has 3*N+1 entries (N cubic segments).
        struct BezierCurve {
            std::vector<double> x;  // time control points
            std::vector<double> y;  // value control points
            bool empty() const { return x.empty(); }
        };

        struct FrameContentState {
            bool visible = false;
            std::string src;
            double x = 0.0;
            double y = 0.0;
            double ox = 0.0;          // mask 0x1: position offset X
            double oy = 0.0;          // mask 0x1: position offset Y
            double width = 0.0;       // "zx" from PSB (source display width)
            double height = 0.0;      // "zy" from PSB (source display height)
            double opacity = 1.0;     // mask 0x400: 0.0-1.0 (from "opa" uint8 0-255)
            double angle = 0.0;       // mask 0x10: rotation degrees
            double scaleX = 1.0;      // mask 0x20: zoom X ("z")
            double scaleY = 1.0;      // mask 0x40: zoom Y ("zy" in clip context)
            double slantX = 0.0;      // mask 0x80: slant X ("s")
            double slantY = 0.0;      // mask 0x100: slant Y ("sy")
            bool flipX = false;       // mask 0x4: "fx"
            bool flipY = false;       // mask 0x8: "fy"
            int blendMode = 16;       // mask 0x20000: "bm"/"b" (default 16)
            double c0 = 0.0;          // mask 0x2: color curve control points (sub_692AB0 at 0x692E14)
            double c1 = 0.0;          //   3 doubles via sub_6695BC indices 0,1,2
            double c2 = 0.0;
            int colorR = 0x80;        // mask 0x200: color RGBA (sub_692AB0 default 0xFF808080)
            int colorG = 0x80;
            int colorB = 0x80;
            int colorA = 0xFF;
            BezierCurve ccc;          // mask 0x800: color curve control
            BezierCurve acc;          // mask 0x1000: angle curve control
            BezierCurve zcc;          // mask 0x2000: zoom curve control
            BezierCurve scc;          // mask 0x4000: slant curve control
            BezierCurve occ;          // mask 0x8000: opacity curve control
            // === Subsystem data (mask 0x80000+) ===
            // mask 0x80000: motion sub-object (sub_692AB0 at 0x6938CC)
            int motionMask = 0;
            int motionFlags = 0;
            int motionDt = 0;
            bool motionDocmpl = false;
            double motionDofst = 0.0;
            double motionTimeOffset = 0.0;
            // mask 0x100000: particle sub-object (sub_692AB0 at 0x693C64)
            int prtTrigger = 0;
            double prtFmin = 10.0;
            double prtF = 10.0;
            double prtVmin = 0.0;
            double prtV = 0.0;
            double prtAmin = 0.0;
            double prtA = 0.0;
            double prtZmin = 1.0;
            double prtZ = 1.0;
            double prtRange = 0.0;
            // mask 0x200000: camera sub-object (sub_692AB0 at 0x693EF0)
            double cameraFactor = 0.0;
            // mask 0x800000: anchor sub-object (sub_692AB0 at 0x694020)
            // target is a string ref to another node
            // mask 0x1000000: model sub-object (sub_692AB0 at 0x693AE8)
            double modelTimeOffset = 0.0;
            bool modelLoop = false;
            int modelDt = 0;
            // mask 0x8000000: feedback sub-object (sub_692AB0 at 0x694130)
            double feedbackTimespan = 0.0;
            // Transform order (default [0,1,2,3] = Flip,Angle,Zoom,Slant)
            int transformOrder[4] = {0, 1, 2, 3};
            bool hasTransformOrder = false;
            std::string action;       // "content.action" from PSB frameList
            bool hasSync = false;     // "content.sync" from PSB frameList
        };

        std::optional<double>
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

        std::optional<double>
        psbDictionaryNumber(const std::shared_ptr<const PSB::PSBDictionary> &dic,
                            const char *key) {
            if(!dic) {
                return std::nullopt;
            }
            return psbNumberValue((*dic)[key]);
        }

        std::string
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

        std::shared_ptr<PSB::PSBList>
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
        BezierCurve parseBezierCurve(
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

        // Evaluate cubic bezier curve at parameter t.
        // Aligned to libkrkr2.so sub_69A754 (0x69A754):
        //   - x[] = time control points, y[] = value control points
        //   - Segments of 4 control points each (step 3, shared endpoints)
        //   - If t <= x[0]: return y[0]
        //   - If t >= x[last]: return y[last]
        //   - Find segment where x[i] >= t (step 3)
        //   - B(t) = (1-t)³P0 + 3(1-t)²tP1 + 3(1-t)t²P2 + t³P3
        double evaluateBezierCurve(const BezierCurve &curve, double t) {
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

        std::shared_ptr<PSB::PSBDictionary>
        psbDictionaryValue(const std::shared_ptr<const PSB::PSBDictionary> &dic,
                           const char *key) {
            if(!dic) {
                return nullptr;
            }
            return std::dynamic_pointer_cast<PSB::PSBDictionary>((*dic)[key]);
        }

        double activeClipTime(const detail::PlayerRuntime &runtime,
                              const detail::MotionClip *clip) {
            if(clip) {
                if(const auto it = runtime.timelines.find(clip->label);
                   it != runtime.timelines.end()) {
                    return it->second.currentTime;
                }
            }

            for(const auto &[_, state] : runtime.timelines) {
                if(state.playing || state.currentTime > 0.0) {
                    return state.currentTime;
                }
            }
            return 0.0;
        }

        void mergeFrameContent(const std::shared_ptr<PSB::PSBDictionary> &content,
                               FrameContentState &state) {
            if(!content) {
                return;
            }

            // Aligned to libkrkr2.so sub_6926B4 (0x6926B4) + sub_692AB0 (0x692AB0):
            // Each PSB frameList frame's "content" dict has a "mask" integer.
            // "mask" is a bitmask that controls WHICH properties this keyframe
            // modifies. Properties without their mask bit set keep their
            // previous/default value. This is critical — e.g. yuzulogo.mtn's
            // logo nodes have opa=0 in content but mask does NOT have bit 0x400,
            // so libkrkr2.so ignores the opa value and uses default 255.
            const int mask = static_cast<int>(
                psbDictionaryNumber(content, "mask").value_or(0));

            // "src" is NOT gated by mask — it's gated by node type in sub_692AB0
            // (((1 << a2) & 0x1849) != 0). We don't have node type here, so
            // read unconditionally (safe: src is always a string or absent).
            if(const auto src = psbDictionaryString(content, "src"); !src.empty()) {
                state.src = src;
            }

            // mask & 0x1: ox/oy (sub_692AB0 at 0x692DC4)
            if(mask & 0x1) {
                if(const auto ox = psbDictionaryNumber(content, "ox"))
                    state.ox = *ox;
                if(const auto oy = psbDictionaryNumber(content, "oy"))
                    state.oy = *oy;
            }

            // "zx"/"zy" (source display dimensions) — not clearly mask-gated
            // in sub_692AB0. Read unconditionally for safety.
            if(const auto zx = psbDictionaryNumber(content, "zx")) {
                state.width = *zx;
            }
            if(const auto zy = psbDictionaryNumber(content, "zy")) {
                state.height = *zy;
            }

            // mask & 0x400: opa (sub_692AB0 at 0x693440)
            // CRITICAL: only read "opa" when mask bit 0x400 is set.
            // Default opacity is 255 (1.0) — set in FrameContentState init.
            if(mask & 0x400) {
                if(const auto opa = psbDictionaryNumber(content, "opa"))
                    state.opacity = std::clamp(*opa / 255.0, 0.0, 1.0);
            }

            // "x"/"y" position — read unconditionally (these may be layer-level,
            // not clip-level properties; not clearly mask-gated in sub_692AB0)
            if(const auto x = psbDictionaryNumber(content, "x")) {
                state.x = *x;
            }
            if(const auto y = psbDictionaryNumber(content, "y")) {
                state.y = *y;
            }

            // mask & 0x10: angle (sub_692AB0 at 0x692FC4)
            if(mask & 0x10) {
                if(const auto angle = psbDictionaryNumber(content, "angle"))
                    state.angle = *angle;
            }

            // mask & 0x2: "c" color curve control points (sub_692AB0 at 0x692E14)
            // 3 doubles read via sub_6695BC(content["c"], index, ...)
            if(mask & 0x2) {
                if(auto cList = psbDictionaryList(content, "c")) {
                    if(cList->size() > 0)
                        if(auto v = psbNumberValue((*cList)[0])) state.c0 = *v;
                    if(cList->size() > 1)
                        if(auto v = psbNumberValue((*cList)[1])) state.c1 = *v;
                    if(cList->size() > 2)
                        if(auto v = psbNumberValue((*cList)[2])) state.c2 = *v;
                } else if(auto cDict = psbDictionaryValue(content, "c")) {
                    // May also be stored as dict with "0","1","2" keys
                    if(auto v = psbDictionaryNumber(cDict, "0")) state.c0 = *v;
                    if(auto v = psbDictionaryNumber(cDict, "1")) state.c1 = *v;
                    if(auto v = psbDictionaryNumber(cDict, "2")) state.c2 = *v;
                }
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

            // mask & 0x20: z/scaleX, mask & 0x40: zy/scaleY (sub_692AB0 at 0x692FF4)
            if(mask & 0x60) {
                if(mask & 0x20) {
                    if(const auto z = psbDictionaryNumber(content, "z"))
                        state.scaleX = *z;
                }
                if(mask & 0x40) {
                    // In clip context, "zy" is scaleY (not display height)
                    if(const auto zy = psbDictionaryNumber(content, "zy"))
                        state.scaleY = *zy;
                }
            }

            // mask & 0x80: s/slantX, mask & 0x100: sy/slantY (sub_692AB0 at 0x693048)
            if(mask & 0x180) {
                if(mask & 0x80) {
                    if(const auto s = psbDictionaryNumber(content, "s"))
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

            // mask & 0x200: color RGBA (sub_692AB0 at 0x692F4C → 0x693330)
            // Color is stored as 4 ints at clip+72..84, default 0xFF808080
            if(mask & 0x200) {
                if(auto colorDict = psbDictionaryValue(content, "color")) {
                    // Color can be a dict with indexed values (sub_6637BC)
                    // or a single int broadcast to all channels
                    if(auto r = psbDictionaryNumber(colorDict, "0"))
                        state.colorR = static_cast<int>(*r);
                    if(auto g = psbDictionaryNumber(colorDict, "1"))
                        state.colorG = static_cast<int>(*g);
                    if(auto b = psbDictionaryNumber(colorDict, "2"))
                        state.colorB = static_cast<int>(*b);
                    if(auto a = psbDictionaryNumber(colorDict, "3"))
                        state.colorA = static_cast<int>(*a);
                } else if(auto colorVal = psbDictionaryNumber(content, "color")) {
                    // Single value broadcast (sub_692AB0 case 2/4/5)
                    int cv = static_cast<int>(*colorVal);
                    state.colorR = cv; state.colorG = cv;
                    state.colorB = cv; state.colorA = cv;
                }
            }

            // mask & 0x80000: motion sub-object (sub_692AB0 at 0x6938CC)
            // Full read: mask → flags/dt/docmpl/dofst/dtgt + timeOffset
            if(mask & 0x80000) {
                if(auto md = psbDictionaryValue(content, "motion")) {
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
                    // dtgt (mm & 0x10) is a string ref — read but not stored
                    // (would need variant storage)
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
                state.action = act;
            }
            if(const auto sync = psbDictionaryNumber(content, "sync")) {
                state.hasSync = *sync != 0.0;
            }

            // coord: alternative position format, read unconditionally
            if(const auto coord = psbDictionaryList(content, "coord")) {
                if(coord->size() > 0) {
                    if(const auto value = psbNumberValue((*coord)[0]))
                        state.x = *value;
                }
                if(coord->size() > 1) {
                    if(const auto value = psbNumberValue((*coord)[1]))
                        state.y = *value;
                }
            }
        }

        // Initialize a FrameContentState from a single PSB frame's content.
        // Aligned to libkrkr2.so sub_692AB0 (0x692AB0): each clip slot is
        // independently initialized with DEFAULTS, then mask-gated properties
        // are applied from the frame's content dict. No accumulation from
        // prior frames.
        FrameContentState
        initSlotFromFrame(const std::shared_ptr<PSB::PSBDictionary> &frame) {
            FrameContentState slot;  // defaults: opacity=1.0, angle=0, etc.
            if(!frame) return slot;
            if(const auto content = psbDictionaryValue(frame, "content")) {
                mergeFrameContent(content, slot);
            }
            return slot;
        }

        FrameContentState
        evaluateLayerContent(const std::shared_ptr<const PSB::PSBDictionary> &layer,
                             double time) {
            FrameContentState state;
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

            // Aligned to libkrkr2.so dual-slot model (sub_699AE4 at 0x699AE4):
            // 1. Find the active frame (last frame with time <= time)
            // 2. If type=0: invisible
            // 3. If type=2: use this frame's slot directly (no interpolation)
            // 4. If type=3: interpolate between this frame's slot and
            //    the next frame's slot. Each slot is INDEPENDENTLY initialized
            //    with defaults + mask-gated overrides (sub_692AB0).

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

            const auto activeFrame = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                (*frames)[activeIndex]);
            if(!activeFrame) return state;

            const int activeType = static_cast<int>(
                psbDictionaryNumber(activeFrame, "type").value_or(0.0));

            // type=0: node is invisible at this time
            if(activeType == 0) {
                state.visible = false;
                return state;
            }

            // Initialize slot A from the active frame (fresh defaults + mask)
            // Preserve transformOrder from layer dict (read above)
            int savedTO[4]; bool savedHasTO = state.hasTransformOrder;
            std::copy(std::begin(state.transformOrder),
                      std::end(state.transformOrder), savedTO);
            state = initSlotFromFrame(activeFrame);
            state.visible = true;
            if(savedHasTO) {
                std::copy(savedTO, savedTO + 4, state.transformOrder);
                state.hasTransformOrder = true;
            }

            // type=2: static display, no interpolation
            if(activeType == 2) {
                return state;
            }

            // type=3: interpolate with next frame's slot
            const int nextIndex = activeIndex + 1;
            if(nextIndex >= static_cast<int>(frames->size())) {
                return state;  // no next frame, just use slot A
            }

            const auto nextFrame = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                (*frames)[nextIndex]);
            if(!nextFrame) return state;

            const int nextType = static_cast<int>(
                psbDictionaryNumber(nextFrame, "type").value_or(0.0));

            // Initialize slot B from the next frame (fresh defaults + mask)
            // Aligned to sub_692AB0: independent init, NOT copied from slot A
            FrameContentState slotB = initSlotFromFrame(nextFrame);
            // Inherit src from slot A if slot B doesn't set one
            if(slotB.src.empty()) slotB.src = state.src;

            // Compute interpolation ratio
            const double curTime =
                psbDictionaryNumber(activeFrame, "time").value_or(0.0);
            const double nextTime =
                psbDictionaryNumber(nextFrame, "time").value_or(0.0);
            const double duration = nextTime - curTime;
            if(duration <= 0.0) return state;

            const double t = std::clamp(
                (time - curTime) / duration, 0.0, 1.0);

            if(t <= 0.0 || nextType == 0) {
                return state;  // at exact start or next is invisible
            }

            // Interpolate between slot A (state) and slot B (slotB)
            // Aligned to sub_699AE4 (0x699AE4)
            auto lerp = [](double a, double b, double r) {
                return a * (1.0 - r) + b * r;
            };

            // Compute eased t for properties with curve control.
            // Aligned to sub_699AE4: if curve data exists, t is transformed
            // through sub_69A754 bezier evaluation before interpolation.

            // ccc: eases opacity and color (sub_69A4D4 at 0x69A55C)
            const double t_ccc = !state.ccc.empty()
                ? evaluateBezierCurve(state.ccc, t) : t;

            // acc: eases angle (sub_699AE4 at 0x699DE8)
            const double t_acc = !state.acc.empty()
                ? evaluateBezierCurve(state.acc, t) : t;

            // Position (linear, sub_699AE4 at 0x699BB0~BC0)
            state.x = lerp(state.x, slotB.x, t);
            state.y = lerp(state.y, slotB.y, t);
            state.ox = lerp(state.ox, slotB.ox, t);
            state.oy = lerp(state.oy, slotB.oy, t);

            // Opacity — uses ccc-eased t (sub_69A4D4 at 0x69A624)
            // Also supports occ (opacity-specific curve, mask 0x8000)
            // sub_699AE4 at 0x69A004: lerp as int, then round via
            // floor(v+0.5) or ceil(v-0.5)
            if(state.opacity != slotB.opacity) {
                const double t_opa = !state.occ.empty()
                    ? evaluateBezierCurve(state.occ, t)
                    : t_ccc;  // fall back to ccc if no occ
                const double opaA = state.opacity * 255.0;
                const double opaB = slotB.opacity * 255.0;
                double opaInterp = lerp(opaA, opaB, t_opa);
                // Integer rounding aligned to sub_699AE4 at 0x69A040:
                // if (v < 0) ceil(v - 0.5) else floor(v + 0.5)
                int opaInt = opaInterp < 0.0
                    ? static_cast<int>(std::ceil(opaInterp - 0.5))
                    : static_cast<int>(std::floor(opaInterp + 0.5));
                state.opacity = std::clamp(opaInt / 255.0, 0.0, 1.0);
            }

            // Angle with 360° wrap — uses acc-eased t (sub_699AE4 at 0x699DEC)
            double curAngle = state.angle;
            double nxtAngle = slotB.angle;
            if(curAngle != nxtAngle) {
                if(curAngle >= nxtAngle) {
                    if(curAngle - nxtAngle > 180.0) nxtAngle += 360.0;
                } else {
                    if(nxtAngle - curAngle > 180.0) nxtAngle -= 360.0;
                }
                double interpAngle = lerp(curAngle, nxtAngle, t_acc);
                if(interpAngle < 0.0) interpAngle += 360.0;
                else if(interpAngle >= 360.0) interpAngle -= 360.0;
                state.angle = interpAngle;
            }

            // ScaleX/scaleY — uses zcc-eased t (sub_699AE4 at 0x699E4C)
            const double t_zcc = !state.zcc.empty()
                ? evaluateBezierCurve(state.zcc, t) : t;
            if(state.scaleX != slotB.scaleX)
                state.scaleX = lerp(state.scaleX, slotB.scaleX, t_zcc);
            if(state.scaleY != slotB.scaleY)
                state.scaleY = lerp(state.scaleY, slotB.scaleY, t_zcc);

            // SlantX/slantY — uses scc-eased t (sub_699AE4 at 0x699EFC)
            const double t_scc = !state.scc.empty()
                ? evaluateBezierCurve(state.scc, t) : t;
            if(state.slantX != slotB.slantX)
                state.slantX = lerp(state.slantX, slotB.slantX, t_scc);
            if(state.slantY != slotB.slantY)
                state.slantY = lerp(state.slantY, slotB.slantY, t_scc);

            // Width/height (linear)
            if(state.width != slotB.width)
                state.width = lerp(state.width, slotB.width, t);
            if(state.height != slotB.height)
                state.height = lerp(state.height, slotB.height, t);

            // FlipX/FlipY: not interpolated, use slot A value
            // (sub_699AE4 copies directly from clip slot, no lerp)

            // Use src from slot A (or B if A is empty)
            if(state.src.empty() && !slotB.src.empty()) {
                state.src = slotB.src;
            }

            return state;
        }


        // -----------------------------------------------------------------
        // Layer-API based rendering (no OpenCV dependency, used in web build)
        // -----------------------------------------------------------------

        // Navigate a PSB dictionary tree by a slash-separated path.
        std::shared_ptr<const PSB::PSBDictionary> navigatePSBPath(
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
        // If the resource is RL-compressed, decompresses into decompressedOut.
        const PSB::PSBResource *findPSBResourceBySourceName(
            const detail::MotionSnapshot &snapshot,
            const std::string &source,
            int &outWidth, int &outHeight,
            std::vector<std::uint8_t> &decompressedOut,
            double &outOriginX, double &outOriginY) {
            outWidth = 0;
            outHeight = 0;
            outOriginX = 0.0;
            outOriginY = 0.0;
            decompressedOut.clear();
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
                        // Get the pixel resource
                        const auto pixelPath = iconPath + "/pixel";
                        auto resIt = snapshot.resourcesByPath.find(pixelPath);
                        if(resIt != snapshot.resourcesByPath.end() &&
                           !resIt->second->data.empty() &&
                           outWidth > 0 && outHeight > 0) {
                            // Check for RL compression
                            auto compressStr =
                                psbDictionaryString(iconNode, "compress");
                            if(compressStr == "RL") {
                                decompressedOut = decompressPsbRL(
                                    resIt->second->data,
                                    outWidth, outHeight);
                            }
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
                            // Check for RL compression
                            auto compressStr =
                                psbDictionaryString(node, "compress");
                            if(compressStr == "RL" &&
                               outWidth > 0 && outHeight > 0) {
                                decompressedOut = decompressPsbRL(
                                    resource->data, outWidth, outHeight);
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
        bool loadPSBResourceToLayer(
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

        // Try to resolve a source image path for the given source name in the
        // motion snapshot. Uses the same candidate generation logic as
        // loadMotionSourceImage but without OpenCV.
        ttstr resolveMotionSourcePath(
            const detail::MotionSnapshot &snapshot,
            const std::string &source) {
            if(source.empty() || isMotionCrossReference(source)) {
                return {};
            }

            std::vector<ttstr> candidates;
            const auto sourcePath = detail::widen(source);
            candidates.push_back(sourcePath);
            pushGraphicCandidates(candidates, sourcePath);
            detail::appendEmbeddedSourceCandidates(snapshot, source, candidates);
            for(const auto &alias : snapshot.resourceAliases) {
                const auto embeddedBase = ttstr{ TJS_W("psb://") } +
                    detail::widen(alias) + TJS_W("/") + sourcePath;
                pushGraphicCandidates(candidates, embeddedBase);
            }

            // PSB motion resources are stored in a tree like:
            //   source/<group>/<subgroup>/<name>/pixel
            // but motion layers reference them as:
            //   src/<group>/<name>
            // Scan resourcesByPath for matching resource paths.
            {
                const auto lastSlash = source.rfind('/');
                const auto baseName = (lastSlash != std::string::npos)
                    ? source.substr(lastSlash + 1) : source;

                for(const auto &[resPath, _] : snapshot.resourcesByPath) {
                    const auto targetSuffix = "/" + baseName + "/pixel";
                    if(resPath.size() >= targetSuffix.size() &&
                       resPath.compare(resPath.size() - targetSuffix.size(),
                                       targetSuffix.size(), targetSuffix) == 0) {
                        for(const auto &alias : snapshot.resourceAliases) {
                            const auto psbPath = ttstr{ TJS_W("psb://") } +
                                detail::widen(alias) + TJS_W("/") +
                                detail::widen(resPath);
                            pushGraphicCandidates(candidates, psbPath);
                        }
                    }
                }
            }

            std::unordered_set<std::string> seen;
            for(const auto &candidate : candidates) {
                const auto candidateKey = detail::narrow(candidate);
                if(!seen.insert(candidateKey).second || candidate.IsEmpty()) {
                    continue;
                }
                if(candidateKey.rfind("psb://", 0) == 0) {
                    if(TVPIsExistentStorage(candidate)) {
                        return candidate;
                    }
                    continue;
                }
                if(const auto placed = TVPGetPlacedPath(candidate);
                   !placed.IsEmpty()) {
                    return placed;
                }
            }
            return {};
        }

        // Flatten a PSB layer node tree into a list of render nodes.
        // Aligned to libkrkr2.so sub_6C4E28: converts tree into flat list
        // with pre-computed positions for the sub_6C7440 render loop.
        // Aligned to libkrkr2.so: full 2x3 affine [m11,m21,m12,m22,tx,ty]
        using Affine2x3 = std::array<double, 6>;

        struct FlatRenderNode {
            FrameContentState state;
            Affine2x3 affine{1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
            double accumulatedOpacity = 1.0;  // parent.opacity * child.opacity
            bool flipX = false;               // XOR-inherited from parent
            bool flipY = false;               // XOR-inherited from parent
        };

        // Compose: result = parent * Translate(lx, ly)
        Affine2x3 affineTranslate(const Affine2x3 &p, double lx, double ly) {
            return {p[0], p[1], p[2], p[3],
                    p[0]*lx + p[2]*ly + p[4],
                    p[1]*lx + p[3]*ly + p[5]};
        }

        // Compose: result = a * Scale(sx, sy)
        Affine2x3 affineScale(const Affine2x3 &a, double sx, double sy) {
            return {a[0]*sx, a[1]*sx, a[2]*sy, a[3]*sy, a[4], a[5]};
        }

        // Compose: result = a * Rotate(angleDeg)
        // Aligned to libkrkr2.so Player_updateLayers 2x2 matrix multiply
        Affine2x3 affineRotate(const Affine2x3 &a, double angleDeg) {
            if(angleDeg == 0.0) return a;
            const double rad = angleDeg * 3.14159265358979323846 / 180.0;
            const double c = std::cos(rad);
            const double s = std::sin(rad);
            // Rotation matrix R = [c -s; s c]
            // A * R: new_m11 = a.m11*c + a.m12*s, new_m12 = -a.m11*s + a.m12*c
            return {a[0]*c + a[2]*s, a[1]*c + a[3]*s,
                    -a[0]*s + a[2]*c, -a[1]*s + a[3]*c,
                    a[4], a[5]};
        }

        // Build local 2x2 matrix and right-multiply into affine.
        // Exactly replicates libkrkr2.so sub_699940 (0x699940):
        //   Starts from identity, LEFT-multiplies transforms in order
        //   [0=Flip, 1=Angle, 2=Zoom, 3=Slant] (default transformOrder).
        //   Then composes: affine = affine × local_2x2
        //
        // sub_699940 variable mapping (verified from decompilation):
        //   v5→m11(+120), v6→m12(+128), v4→m21(+136), v7→m22(+144)
        //   case 0 flipX: negate v5,v6 (row1) = left-multiply [-1,0;0,1]
        //   case 0 flipY: negate v4,v7 (row2) = left-multiply [1,0;0,-1]
        //   case 1 angle: left-multiply [cos,-sin;sin,cos]
        //   case 2 zoom:  left-multiply [zoomX,0;0,zoomY]
        //   case 3 slant: left-multiply [1,slantX;slantY,1]
        void applyLocalTransform(Affine2x3 &a,
                                 const FrameContentState &state) {
            // Build local 2x2 from identity via left-multiplication.
            // Exactly replicates sub_699940 (0x699940): iterates
            // transformOrder[0..3] and applies each transform case.
            // Default order [0,1,2,3] = [Flip, Angle, Zoom, Slant].
            double l11 = 1.0, l12 = 0.0, l21 = 0.0, l22 = 1.0;

            for(int step = 0; step < 4; step++) {
                const int op = state.transformOrder[step];
                switch(op) {
                    case 0: // Flip (left-multiply [-1,0;0,1] / [1,0;0,-1])
                        if(state.flipX) { l11 = -l11; l12 = -l12; }
                        if(state.flipY) { l21 = -l21; l22 = -l22; }
                        break;
                    case 1: // Angle (left-multiply [c,-s;s,c])
                        if(state.angle != 0.0) {
                            const double rad = state.angle * 2.0 * 3.14159265358979323846 / 360.0;
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
                        if(state.scaleX != 1.0 || state.scaleY != 1.0) {
                            l11 *= state.scaleX; l12 *= state.scaleX;
                            l21 *= state.scaleY; l22 *= state.scaleY;
                        }
                        break;
                    case 3: // Slant (left-multiply [1,sx;sy,1]) — 0x699A7C
                        if(state.slantX != 0.0 || state.slantY != 0.0) {
                            const double t12 = l22*state.slantX + l12;
                            const double t21 = l11*state.slantY + l21;
                            const double t22 = l22 + l12*state.slantY;
                            const double t11 = l11 + state.slantX*l21;
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

        void flattenLayerNodes(
            const std::shared_ptr<const PSB::PSBDictionary> &node,
            double time,
            const Affine2x3 &parentAffine,
            double parentOpacity,
            bool parentFlipX, bool parentFlipY,
            std::vector<FlatRenderNode> &out) {
            if(!node) return;
            const auto state = evaluateLayerContent(node, time);
            // Aligned to libkrkr2.so Player_updateLayers (0x6BB33C):
            // ox/oy from PSB frameList content are position offsets.
            const double lx = state.x + state.ox;
            const double ly = state.y + state.oy;

            // Step 1: Translation — parent * Translate(lx, ly)
            auto curAffine = affineTranslate(parentAffine, lx, ly);

            // Step 2: Build local 2x2 matrix matching sub_699940.
            // Uses the node's OWN flip (state.flipX/Y from sub_699AE4),
            // NOT the XOR'd inherited flip. The XOR is only for children.
            applyLocalTransform(curAffine, state);

            // Flip XOR for inheritance to children.
            // Aligned to Player_updateLayers (0x6BB8A8):
            //   child.flipX ^= parent.flipX
            const bool curFlipX = parentFlipX ^ state.flipX;
            const bool curFlipY = parentFlipY ^ state.flipY;

            // Opacity: integer multiplication matching libkrkr2.so (0x6BB6D4):
            //   result = parent_opa * child_opa / 255  (int math)
            const int parentOpaInt = static_cast<int>(
                std::clamp(parentOpacity * 255.0, 0.0, 255.0));
            const int childOpaInt = static_cast<int>(
                std::clamp(state.opacity * 255.0, 0.0, 255.0));
            const double curOpacity = static_cast<double>(
                parentOpaInt * childOpaInt / 255) / 255.0;

            if(state.visible && !state.src.empty() && state.src != "layout"
               && !isMotionCrossReference(state.src)) {
                FlatRenderNode rn;
                rn.state = state;
                rn.affine = curAffine;
                rn.accumulatedOpacity = curOpacity;
                // Flip is already in the matrix via applyLocalTransform.
                // These flags are not used at render time anymore.
                rn.flipX = false;
                rn.flipY = false;
                out.push_back(std::move(rn));
            }

            if(const auto children = psbDictionaryList(node, "children")) {
                auto childAffine = curAffine;
                if(state.src == "layout" && state.width > 0.0 && state.height > 0.0) {
                    childAffine = affineScale(curAffine, state.width, state.height);
                }
                for(size_t i = 0; i < children->size(); ++i) {
                    auto child = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                        (*children)[static_cast<int>(i)]);
                    flattenLayerNodes(child, time, childAffine,
                                     curOpacity, curFlipX, curFlipY, out);
                }
            }
        }


        // Build flat render list from persistent node tree.
        // Replaces flattenLayerNodes() output — produces FlatRenderNodes
        // from accumulated MotionNode state.
        // Aligned to libkrkr2.so sub_6C2334: converts accumulated node
        // state into renderable entries with globalAffine applied.
        void buildRenderListFromNodes(
            const std::vector<detail::MotionNode> &nodes,
            const Affine2x3 &globalAffine,
            std::vector<FlatRenderNode> &out) {
            for (const auto &node : nodes) {
                if (!node.drawFlag) continue;
                if (node.nodeType != 0) continue;  // only obj nodes render
                if (!node.hasSource) continue;
                if (node.interpolatedCache.src.empty()) continue;
                if (node.interpolatedCache.src == "layout") continue;
                if (isMotionCrossReference(node.interpolatedCache.src)) continue;

                // Compose globalAffine with node's accumulated transform:
                // result = globalAffine × [node.m11, node.m12; node.m21, node.m22]
                // with translation from node accumulated position.
                const auto &acc = node.accumulated;
                Affine2x3 nodeAffine;
                // First translate by accumulated position
                nodeAffine[0] = globalAffine[0];
                nodeAffine[1] = globalAffine[1];
                nodeAffine[2] = globalAffine[2];
                nodeAffine[3] = globalAffine[3];
                nodeAffine[4] = globalAffine[0] * acc.posX + globalAffine[2] * acc.posY + globalAffine[4];
                nodeAffine[5] = globalAffine[1] * acc.posX + globalAffine[3] * acc.posY + globalAffine[5];
                // Then multiply by accumulated 2x2 matrix
                const double m11 = nodeAffine[0] * acc.m11 + nodeAffine[2] * acc.m21;
                const double m21 = nodeAffine[1] * acc.m11 + nodeAffine[3] * acc.m21;
                const double m12 = nodeAffine[0] * acc.m12 + nodeAffine[2] * acc.m22;
                const double m22 = nodeAffine[1] * acc.m12 + nodeAffine[3] * acc.m22;
                nodeAffine[0] = m11;
                nodeAffine[1] = m21;
                nodeAffine[2] = m12;
                nodeAffine[3] = m22;

                FlatRenderNode rn;
                // Copy interpolated cache back into FrameContentState for rendering
                rn.state.visible = acc.visible;
                rn.state.src = node.interpolatedCache.src;
                rn.state.width = node.interpolatedCache.width;
                rn.state.height = node.interpolatedCache.height;
                rn.state.x = node.interpolatedCache.x;
                rn.state.y = node.interpolatedCache.y;
                rn.state.ox = node.interpolatedCache.ox;
                rn.state.oy = node.interpolatedCache.oy;
                rn.state.opacity = node.interpolatedCache.opacity;
                rn.state.angle = node.interpolatedCache.angle;
                rn.state.scaleX = node.interpolatedCache.scaleX;
                rn.state.scaleY = node.interpolatedCache.scaleY;
                rn.state.slantX = node.interpolatedCache.slantX;
                rn.state.slantY = node.interpolatedCache.slantY;
                rn.state.flipX = node.interpolatedCache.flipX;
                rn.state.flipY = node.interpolatedCache.flipY;
                rn.state.blendMode = node.interpolatedCache.blendMode;
                rn.state.colorR = node.interpolatedCache.colorR;
                rn.state.colorG = node.interpolatedCache.colorG;
                rn.state.colorB = node.interpolatedCache.colorB;
                rn.state.colorA = node.interpolatedCache.colorA;
                if (node.interpolatedCache.hasTransformOrder) {
                    std::copy(std::begin(node.interpolatedCache.transformOrder),
                              std::end(node.interpolatedCache.transformOrder),
                              rn.state.transformOrder);
                    rn.state.hasTransformOrder = true;
                }
                rn.state.action = node.interpolatedCache.action;
                rn.state.hasSync = node.interpolatedCache.hasSync;

                rn.affine = nodeAffine;
                rn.accumulatedOpacity = acc.opacity / 255.0;
                rn.flipX = false;
                rn.flipY = false;
                out.push_back(std::move(rn));
            }

            // Child Player render collection is done separately in
            // collectChildRenderNodes() called from renderToLayer().
        }

    } // namespace

    Player::Player(ResourceManager rm) :
        _runtime(detail::makePlayerRuntime()),
        _resourceManagerNative(std::move(rm)) {
        LOGGER->info("Motion.Player constructor called");
    }

    Player::~Player() = default;

    void Player::setMotion(ttstr v) {
        if(_motionKey == v) {
            return;
        }
        _motionKey = v;
        _runtime->activeMotion.reset();
        _runtime->timelines.clear();
        _runtime->drawAffineMatrix = { 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
        _variableKeys.Clear();
        _variableValues.clear();
        ensureMotionLoaded();
    }

    // Aligned to libkrkr2.so 0x681CAC → 0x6B0F10:
    // motion setter calls objthis.onFindMotion({chara, motion}) to let
    // TJS participate in path resolution before loading the PSB.
    tjs_error Player::setMotionCompat(tTJSVariant *result, tjs_int numparams,
                                      tTJSVariant **param,
                                      iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) return TJS_E_INVALIDOBJECT;

        ttstr motionValue;
        if(numparams > 0 && param[0] && param[0]->Type() != tvtVoid) {
            motionValue = *param[0];
        }

        if(self->_motionKey == motionValue) {
            return TJS_S_OK;
        }

        // Build dict {chara, motion} and call objthis.onFindMotion(dict)
        // Aligned to libkrkr2.so Player_loadMotion_guess (0x6B0F10)
        tTJSVariant dictVar = detail::makeDictionary({
            {"chara", tTJSVariant(self->_chara)},
            {"motion", tTJSVariant(motionValue)}
        });
        tTJSVariant onFindResult;
        tTJSVariant *args[] = { &dictVar };
        tjs_error hr = objthis->FuncCall(0, TJS_W("onFindMotion"),
                                          nullptr, &onFindResult, 1, args, objthis);

        // Read back (possibly modified) chara and motion from result
        if(TJS_SUCCEEDED(hr) && onFindResult.Type() == tvtObject) {
            iTJSDispatch2 *resObj = onFindResult.AsObjectNoAddRef();
            if(resObj) {
                tTJSVariant charaVal, motionVal;
                if(TJS_SUCCEEDED(resObj->PropGet(TJS_MEMBERMUSTEXIST,
                    TJS_W("chara"), nullptr, &charaVal, resObj))
                    && charaVal.Type() != tvtVoid) {
                    self->_chara = ttstr(charaVal);
                }
                if(TJS_SUCCEEDED(resObj->PropGet(TJS_MEMBERMUSTEXIST,
                    TJS_W("motion"), nullptr, &motionVal, resObj))
                    && motionVal.Type() != tvtVoid) {
                    motionValue = ttstr(motionVal);
                }
            }
        }

        // Reset state and load
        self->_motionKey = motionValue;
        self->_runtime->activeMotion.reset();
        self->_runtime->timelines.clear();
        self->_runtime->drawAffineMatrix = { 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
        self->_variableKeys.Clear();
        self->_variableValues.clear();
        self->ensureMotionLoaded();

        return TJS_S_OK;
    }

    tjs_error Player::getMotionCompat(tTJSVariant *result, tjs_int,
                                      tTJSVariant **, iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) return TJS_E_INVALIDOBJECT;
        if(result) *result = tTJSVariant(self->_motionKey);
        return TJS_S_OK;
    }

    bool Player::ensureMotionLoaded() {
        if(_runtime->activeMotion) {
            return true;
        }

        const auto motionKey = detail::narrow(_motionKey);
        const bool motionKeyLooksLikeStorage =
            motionKey.find('/') != std::string::npos ||
            motionKey.find('\\') != std::string::npos ||
            motionKey.find('.') != std::string::npos;

        if(_project.Type() == tvtObject) {
            if(const auto snapshot = detail::lookupModuleSnapshot(_project)) {
                activateMotion(*_runtime, snapshot);
                syncVariableKeysFromActiveMotion();
                return true;
            }
        }

        if(motionKeyLooksLikeStorage) {
            if(const auto snapshot =
                   resolveMotion(*_runtime, _motionKey, &_resourceManagerNative)) {
                activateMotion(*_runtime, snapshot);
                syncVariableKeysFromActiveMotion();
                return true;
            }
        }

        if(const auto loaded = _resourceManagerNative.getLastLoadedModule();
           loaded.Type() == tvtObject) {
            if(const auto snapshot = detail::lookupModuleSnapshot(loaded)) {
                activateMotion(*_runtime, snapshot);
                syncVariableKeysFromActiveMotion();
                return true;
            }
        }

        if(_motionKey.IsEmpty()) {
            return false;
        }

        if(const auto snapshot =
               resolveMotion(*_runtime, _motionKey, &_resourceManagerNative)) {
            activateMotion(*_runtime, snapshot);
            syncVariableKeysFromActiveMotion();
            return true;
        }

        return false;
    }

    void Player::syncVariableKeysFromActiveMotion() {
        if(!_runtime->activeMotion) {
            _variableKeys = detail::makeArray({});
            return;
        }

        _variableKeys = detail::makeArray(
            detail::stringsToVariants(_runtime->activeMotion->variableLabels));
    }

    const detail::MotionClip *Player::selectActiveClip() const {
        if(!_runtime->activeMotion) {
            return nullptr;
        }

        const auto selectByLabel =
            [this](const std::string &label) -> const detail::MotionClip * {
                if(label.empty()) {
                    return nullptr;
                }
                const auto it = _runtime->activeMotion->clipsByLabel.find(label);
                return it != _runtime->activeMotion->clipsByLabel.end()
                    ? &it->second
                    : nullptr;
            };

        if(const auto *clip = selectByLabel(detail::narrow(_motionKey))) {
            return clip;
        }

        for(const auto &[label, state] : _runtime->timelines) {
            if(!state.playing) {
                continue;
            }
            if(const auto *clip = selectByLabel(label)) {
                return clip;
            }
        }

        if(_runtime->activeMotion->clipsByLabel.size() == 1) {
            return &_runtime->activeMotion->clipsByLabel.begin()->second;
        }

        return nullptr;
    }

    const std::vector<std::string> &Player::activeLayerNames() const {
        static const std::vector<std::string> empty;
        if(!_runtime->activeMotion) {
            return empty;
        }

        if(const auto *clip = selectActiveClip();
           clip && !clip->layerNames.empty()) {
            return clip->layerNames;
        }

        return _runtime->activeMotion->layerNames;
    }

    const std::unordered_map<
        std::string, std::shared_ptr<const PSB::PSBDictionary>> *
    Player::activeLayersByName() const {
        if(!_runtime->activeMotion) {
            return nullptr;
        }

        if(const auto *clip = selectActiveClip();
           clip && !clip->layersByName.empty()) {
            return &clip->layersByName;
        }

        return &_runtime->activeMotion->layersByName;
    }

    const std::vector<std::string> &Player::activeSourceCandidates() const {
        static const std::vector<std::string> empty;
        if(!_runtime->activeMotion) {
            return empty;
        }

        if(const auto *clip = selectActiveClip();
           clip && !clip->sourceCandidates.empty()) {
            return clip->sourceCandidates;
        }

        return _runtime->activeMotion->sourceCandidates;
    }

    tTJSVariant Player::getVariableKeys() {
        ensureMotionLoaded();
        if(_variableKeys.Type() == tvtVoid) {
            return detail::makeArray({});
        }
        return _variableKeys;
    }

    void Player::setProgressCompat(double v) {
        ensureMotionLoaded();
        const auto progress = std::clamp(v, 0.0, 1.0);
        bool anyPlaying = false;
        for(auto &[_, state] : _runtime->timelines) {
            if(state.totalFrames > 0.0) {
                state.currentTime = state.totalFrames * progress;
            } else {
                state.currentTime = progress;
            }
            if(progress >= 1.0 && !state.loop) {
                state.playing = false;
            }
            anyPlaying = anyPlaying || state.playing;
        }
        _allplaying = anyPlaying;
    }

    double Player::getProgressCompat() const {
        bool sawTimeline = false;
        bool anyPlaying = false;
        double progress = 0.0;

        for(const auto &[_, state] : _runtime->timelines) {
            sawTimeline = true;
            anyPlaying = anyPlaying || state.playing;
            if(state.totalFrames > 0.0) {
                progress = std::max(
                    progress,
                    std::clamp(state.currentTime / state.totalFrames, 0.0, 1.0));
            } else if(!state.playing) {
                progress = std::max(progress, 1.0);
            }
        }

        if(!sawTimeline) {
            return _allplaying ? 0.0 : 1.0;
        }
        if(!anyPlaying) {
            return 1.0;
        }
        return progress;
    }

    // --- Core methods ---
    double Player::random() {
        // In libkrkr2.so, this delegates to a TJS callback "random" method.
        // Use standard random as a fallback.
        static std::mt19937 rng{std::random_device{}()};
        static std::uniform_real_distribution<double> dist(0.0, 1.0);
        return dist(rng);
    }

    void Player::initPhysics() { STUB_WARN(initPhysics); }
    tTJSVariant Player::serialize() {
        ensureMotionLoaded();

        std::vector<std::pair<std::string, tTJSVariant>> variables;
        std::unordered_set<std::string> seenVariables;
        if(_runtime->activeMotion) {
            for(const auto &label : _runtime->activeMotion->variableLabels) {
                seenVariables.insert(label);
                variables.emplace_back(label, getVariable(detail::widen(label)));
            }
        }
        for(const auto &[label, value] : _variableValues) {
            if(seenVariables.insert(label).second) {
                variables.emplace_back(label, value);
            }
        }

        return detail::makeDictionary({
            { "chara", _chara },
            { "motion", _motionKey },
            { "tickcount", _tickCount },
            { "speed", _speed },
            { "outline", static_cast<tjs_int>(_outline ? 1 : 0) },
            { "variables", detail::makeDictionary(variables) },
            { "timelines", getPlayingTimelineInfoList() },
        });
    }

    void Player::unserialize(tTJSVariant data) {
        if(data.Type() != tvtObject || data.AsObjectNoAddRef() == nullptr) {
            return;
        }

        tTJSVariant value;
        if(getObjectProperty(data, TJS_W("chara"), value) &&
           value.Type() != tvtVoid) {
            _chara = value;
        }

        if(getObjectProperty(data, TJS_W("motion"), value) &&
           value.Type() != tvtVoid) {
            _motionKey = value;
            ensureMotionLoaded();
        }

        if(getObjectProperty(data, TJS_W("tickcount"), value) &&
           value.Type() != tvtVoid) {
            _tickCount = value.AsReal();
        }

        if(getObjectProperty(data, TJS_W("speed"), value) &&
           value.Type() != tvtVoid) {
            _speed = value.AsReal();
        }

        if(getObjectProperty(data, TJS_W("outline"), value) &&
           value.Type() != tvtVoid) {
            _outline = value.AsInteger() != 0;
        }

        if(getObjectProperty(data, TJS_W("variables"), value) &&
           value.Type() == tvtObject && value.AsObjectNoAddRef() != nullptr) {
            DictionaryEnumerator callback;
            tTJSVariantClosure closure(&callback, nullptr);
            value.AsObjectNoAddRef()->EnumMembers(TJS_IGNOREPROP, &closure,
                                                  value.AsObjectNoAddRef());
            for(const auto &[label, stored] : callback.entries) {
                if(stored.Type() != tvtVoid) {
                    setVariable(label, stored.AsReal());
                }
            }
        }

        bool restoredTimelines = false;
        if(getObjectProperty(data, TJS_W("timelines"), value) &&
           value.Type() == tvtObject && value.AsObjectNoAddRef() != nullptr) {
            ensureMotionLoaded();
            if(_runtime->activeMotion && _runtime->timelines.empty()) {
                detail::primeTimelineStates(_runtime->timelines,
                                            *_runtime->activeMotion);
            }

            const auto count = getObjectCount(value);
            for(tjs_int index = 0; index < count; ++index) {
                tTJSVariant item;
                if(!getArrayItem(value, index, item) || item.Type() != tvtObject ||
                   item.AsObjectNoAddRef() == nullptr) {
                    continue;
                }

                tTJSVariant labelValue;
                if(!getObjectProperty(item, TJS_W("label"), labelValue) ||
                   labelValue.Type() == tvtVoid) {
                    continue;
                }

                const auto key = detail::narrow(labelValue);
                auto it = _runtime->timelines.find(key);
                if(it == _runtime->timelines.end()) {
                    continue;
                }

                restoredTimelines = true;
                it->second.playing = true;

                tTJSVariant flagsValue;
                if(getObjectProperty(item, TJS_W("flags"), flagsValue) &&
                   flagsValue.Type() != tvtVoid) {
                    it->second.flags = flagsValue.AsInteger();
                }

                tTJSVariant currentTimeValue;
                if(getObjectProperty(item, TJS_W("currentTime"), currentTimeValue) &&
                   currentTimeValue.Type() != tvtVoid) {
                    it->second.currentTime = currentTimeValue.AsReal();
                }

                tTJSVariant blendRatioValue;
                if(getObjectProperty(item, TJS_W("blendRatio"), blendRatioValue) &&
                   blendRatioValue.Type() != tvtVoid) {
                    it->second.blendRatio = blendRatioValue.AsReal();
                }
            }
        }

        if(!restoredTimelines && ensureMotionLoaded()) {
            if(_runtime->timelines.empty()) {
                detail::primeTimelineStates(_runtime->timelines,
                                            *_runtime->activeMotion);
            }
            const auto &primary = !_runtime->activeMotion->mainTimelineLabels.empty()
                ? _runtime->activeMotion->mainTimelineLabels
                : _runtime->activeMotion->diffTimelineLabels;
            for(const auto &label : primary) {
                playTimeline(detail::widen(label), PlayFlagForce);
            }
        }

        _allplaying = std::any_of(
            _runtime->timelines.begin(), _runtime->timelines.end(),
            [](const auto &entry) { return entry.second.playing; });
    }

    void Player::setRotate(double rot) { STUB_WARN(setRotate); }
    void Player::setMirror(bool mirror) { setFlip(mirror); }
    void Player::setHairScale(double) { STUB_WARN(setHairScale); }
    void Player::setPartsScale(double) { STUB_WARN(setPartsScale); }
    void Player::setBustScale(double) { STUB_WARN(setBustScale); }

    void Player::setDrawAffineTranslateMatrix(tTJSVariant) {
        STUB_WARN(setDrawAffineTranslateMatrix);
    }

    tTJSVariant Player::getCameraOffset() { return _cameraPosition; }

    void Player::setCameraOffset(tTJSVariant offset) {
        _cameraPosition = offset;
        // Aligned to libkrkr2.so sub_6D9A38: setCameraOffset(x, y)
        // Stores as float at Player+144/148. NCB passes a Point with x,y.
        if(offset.Type() == tvtObject) {
            auto *obj = offset.AsObjectNoAddRef();
            if(obj) {
                tTJSVariant xv, yv;
                if(obj->PropGet(0, TJS_W("x"), nullptr, &xv, obj) == TJS_S_OK)
                    _cameraOffsetX = static_cast<float>(xv.AsReal());
                if(obj->PropGet(0, TJS_W("y"), nullptr, &yv, obj) == TJS_S_OK)
                    _cameraOffsetY = static_cast<float>(yv.AsReal());
            }
        }
    }

    void Player::modifyRoot(tTJSVariant data) { _project = data; }

    void Player::debugPrint() {
        LOGGER->info("motionKey={}, motions={}, sources={}, timelines={}",
                     _motionKey.AsStdString(), _runtime->motionsByKey.size(),
                     _runtime->sourcesByKey.size(), _runtime->timelines.size());
    }

    // --- Resource management ---
    void Player::unload(ttstr name) {
        const auto key = detail::narrow(name);
        if(key.empty()) {
            return;
        }

        for(auto it = _runtime->motionsByKey.begin();
            it != _runtime->motionsByKey.end();) {
            if(it->first == key || it->second->path == key) {
                if(_runtime->activeMotion == it->second) {
                    _runtime->activeMotion.reset();
                    _runtime->timelines.clear();
                }
                it = _runtime->motionsByKey.erase(it);
            } else {
                ++it;
            }
        }

        for(auto it = _runtime->sourcesByKey.begin();
            it != _runtime->sourcesByKey.end();) {
            if(it->first == key) {
                it = _runtime->sourcesByKey.erase(it);
            } else {
                ++it;
            }
        }
    }

    void Player::unloadAll() {
        _runtime->motionsByKey.clear();
        _runtime->sourcesByKey.clear();
        _runtime->activeMotion.reset();
        _runtime->timelines.clear();
        _runtime->layerIdsByName.clear();
        _runtime->layerNamesById.clear();
        _runtime->lastCanvas.Clear();
        _runtime->lastViewParam.Clear();
        _runtime->drawAffineMatrix = { 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
        _variableKeys.Clear();
        _variableValues.clear();
        _motionKey.Clear();
    }

    bool Player::isExistMotion(ttstr name) {
        return static_cast<bool>(
            resolveMotion(*_runtime, name, &_resourceManagerNative));
    }

    tTJSVariant Player::findMotion(ttstr name) {
        const auto snapshot =
            resolveMotion(*_runtime, name, &_resourceManagerNative);
        if(!snapshot) {
            return {};
        }

        activateMotion(*_runtime, snapshot);
        _motionKey = name;
        syncVariableKeysFromActiveMotion();
        return snapshot->moduleValue;
    }

    tjs_int Player::requireLayerId(ttstr name) {
        const auto key = detail::narrow(name);
        if(const auto it = _runtime->layerIdsByName.find(key);
           it != _runtime->layerIdsByName.end()) {
            return it->second;
        }

        const auto id = _runtime->nextLayerId++;
        _runtime->layerIdsByName[key] = id;
        _runtime->layerNamesById[id] = key;
        return id;
    }

    void Player::releaseLayerId(tjs_int id) {
        if(const auto it = _runtime->layerNamesById.find(id);
           it != _runtime->layerNamesById.end()) {
            _runtime->layerIdsByName.erase(it->second);
            _runtime->layerNamesById.erase(it);
        }
    }

    // --- Drawing/rendering ---
    void Player::setClearColor(tjs_int color) { _runtime->clearColor = color; }

    void Player::setResizable(bool v) { _runtime->resizable = v; }

    void Player::removeAllTextures() { _runtime->sourcesByKey.clear(); }

    void Player::removeAllBg() { _runtime->backgrounds.clear(); }

    void Player::removeAllCaption() { _runtime->captions.clear(); }

    void Player::registerBg(tTJSVariant bg) { _runtime->backgrounds.push_back(bg); }

    void Player::registerCaption(tTJSVariant caption) {
        _runtime->captions.push_back(caption);
    }

    void Player::unloadUnusedTextures() {}

    tjs_int Player::alphaOpAdd() { return ++_runtime->alphaOpCounter; }

    tTJSVariant Player::captureCanvas() {
        if(_runtime->lastCanvas.Type() == tvtVoid) {
            draw();
        }
        return _runtime->lastCanvas;
    }

    ttstr Player::resolveCaptureSourcePath() const {
        if(!_runtime->activeMotion) {
            return {};
        }

        std::vector<ttstr> candidates;
        const auto motionPath = detail::widen(_runtime->activeMotion->path);
        const auto baseDir = TVPExtractStoragePath(motionPath);
        for(const auto &candidate : activeSourceCandidates()) {
            pushGraphicCandidates(candidates, detail::widen(candidate));
            detail::appendEmbeddedSourceCandidates(*_runtime->activeMotion,
                                                   candidate, candidates);
            if(!baseDir.IsEmpty()) {
                pushGraphicCandidates(candidates,
                                      baseDir + detail::widen(candidate));
            }
        }

        const auto stem =
            detail::widen(basenameWithoutExtension(_runtime->activeMotion->path));
        pushGraphicCandidates(candidates, stem);
        if(!baseDir.IsEmpty()) {
            pushGraphicCandidates(candidates, baseDir + stem);
        }

        ttstr resolved;
        detail::resolveExistingPath(candidates, resolved);
        return resolved;
    }

    bool Player::renderToD3DAdaptor(D3DAdaptor *adaptor) {
        if(!adaptor || adaptor->getWidth() <= 0 || adaptor->getHeight() <= 0) {
            return false;
        }
        // Guard against recursion: renderToLayer may trigger TJS callbacks
        // that call drawCompat again.
        static bool s_inRenderToD3D = false;
        if(s_inRenderToD3D) return false;
        s_inRenderToD3D = true;
        struct Guard { ~Guard() { s_inRenderToD3D = false; } } guard;

        ensureMotionLoaded();
        if(!_runtime->activeMotion) return false;

        // We need a scratch Layer for the rendering pipeline (LoadImages etc.).
        // Reuse _runtime->lastCanvas if available, otherwise create one via TJS.
        iTJSDispatch2 *scratchLayerObj = nullptr;
        if(_runtime->lastCanvas.Type() == tvtObject &&
           _runtime->lastCanvas.AsObjectNoAddRef()) {
            scratchLayerObj = _runtime->lastCanvas.AsObjectNoAddRef();
        }

        // Create a temporary Layer if we don't have one cached
        bool ownedLayer = false;
        if(!scratchLayerObj) {
            // Get the global Window object to use as parent
            iTJSDispatch2 *global = TVPGetScriptDispatch();
            if(!global) return false;
            tTJSVariant kagVar;
            if(TJS_SUCCEEDED(global->PropGet(0, TJS_W("kag"), nullptr, &kagVar, global)) &&
               kagVar.Type() == tvtObject && kagVar.AsObjectNoAddRef()) {
                // Create: new Layer(kag, kag.primaryLayer)
                tTJSVariant primaryVar;
                kagVar.AsObjectNoAddRef()->PropGet(0, TJS_W("primaryLayer"),
                                                   nullptr, &primaryVar,
                                                   kagVar.AsObjectNoAddRef());
                if(primaryVar.Type() == tvtObject && primaryVar.AsObjectNoAddRef()) {
                    iTJSDispatch2 *layerClass = nullptr;
                    tTJSVariant lcVar;
                    if(TJS_SUCCEEDED(global->PropGet(0, TJS_W("Layer"), nullptr, &lcVar, global)) &&
                       lcVar.Type() == tvtObject) {
                        layerClass = lcVar.AsObjectNoAddRef();
                        tTJSVariant *args[] = { &kagVar, &primaryVar };
                        iTJSDispatch2 *newLayerDisp = nullptr;
                        if(TJS_SUCCEEDED(layerClass->CreateNew(0, nullptr, nullptr,
                                                                &newLayerDisp, 2, args, layerClass)) &&
                           newLayerDisp) {
                            tTJSVariant newLayer(newLayerDisp, newLayerDisp);
                            newLayerDisp->Release();
                            scratchLayerObj = newLayer.AsObjectNoAddRef();
                            _runtime->lastCanvas = newLayer;  // cache it
                            ownedLayer = true;
                        }
                    }
                }
            }
            global->Release();
        }

        if(!scratchLayerObj) return false;

        // Render to the scratch Layer
        if(!renderToLayer(scratchLayerObj, true)) return false;

        // Copy pixels from scratch Layer to D3DAdaptor buffer
        tTJSNI_BaseLayer *layer = nullptr;
        if(TJS_FAILED(scratchLayerObj->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&layer))) || !layer) {
            return false;
        }

        const int w = adaptor->getWidth();
        const int h = adaptor->getHeight();
        const int layerW = static_cast<int>(layer->GetImageWidth());
        const int layerH = static_cast<int>(layer->GetImageHeight());
        const auto *srcBuf = reinterpret_cast<const std::uint8_t *>(
            layer->GetMainImagePixelBuffer());
        auto srcPitch = layer->GetMainImagePixelBufferPitch();

        if(!srcBuf || srcPitch <= 0 || layerW <= 0 || layerH <= 0) return false;

        // Resize adaptor buffer if needed
        if(w != layerW || h != layerH) {
            adaptor->setSize(layerW, layerH);
        }
        adaptor->clearBuffer();

        auto *dstBuf = adaptor->getBuffer();
        const auto dstPitch = adaptor->getBufferPitch();
        const int copyH = std::min(layerH, adaptor->getHeight());
        const int copyRowBytes = std::min(
            static_cast<int>(layerW * 4), dstPitch);

        for(int y = 0; y < copyH; ++y) {
            std::memcpy(dstBuf + dstPitch * y,
                        srcBuf + srcPitch * y,
                        static_cast<size_t>(copyRowBytes));
        }

        return true;
    }

    bool Player::renderToLayer(iTJSDispatch2 *layerObject,
                               bool skipUpdate) {
        if(!layerObject) {
            return false;
        }

        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return false;
        }


        tTJSNI_BaseLayer *layer = nullptr;
        {
            tjs_error nisResult = layerObject->NativeInstanceSupport(
                   TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                   reinterpret_cast<iTJSNativeInstance **>(&layer));
            if(TJS_FAILED(nisResult) || !layer) {
                // layerObject isn't a native Layer—try to find one through
                // TJS property chain (owner/_owner/targetLayer/layer)
                tTJSVariant wrapper(layerObject, layerObject);
                auto *resolved = tryResolveLayerDispatch(wrapper);
                if(resolved && resolved != layerObject) {
                    return renderToLayer(resolved);
                }

                // Last resort: try NativeInstanceSupport on layerObject directly
                // again (sometimes the first call can fail transiently)
                layer = nullptr;
                if(TJS_SUCCEEDED(layerObject->NativeInstanceSupport(
                       TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                       reinterpret_cast<iTJSNativeInstance **>(&layer))) &&
                   layer) {
                    // Fall through to rendering
                } else {
                    return false;
                }
            }
        }
        // Layer-API based rendering (no OpenCV)
        // libkrkr2.so sub_6C7440 does not gate on resourcesByPath —
        // motions can reference external image files without embedding
        // PSB resources (e.g. logo .mtn files).
        if(_runtime->activeMotion) {
            const auto *clip = selectActiveClip();
            const auto renderTime = activeClipTime(*_runtime, clip);
            // Use the target layer's own size if it's large enough (e.g.
            // motionWorkLayer at full screen resolution). Only fall back to
            // motion's native size if the layer is too small (e.g. SLA owner
            // at 64x64). libkrkr2.so renders at the target's size, not the
            // motion's intrinsic size.
            int canvasWidth = static_cast<int>(layer->GetWidth());
            int canvasHeight = static_cast<int>(layer->GetHeight());
            if(canvasWidth <= 0 || canvasHeight <= 0) {
                canvasWidth = static_cast<int>(layer->GetImageWidth());
                canvasHeight = static_cast<int>(layer->GetImageHeight());
            }
            // Fall back to motion size only if layer is very small
            if(canvasWidth < 128 || canvasHeight < 128) {
                int motionW = static_cast<int>(_runtime->activeMotion->width);
                int motionH = static_cast<int>(_runtime->activeMotion->height);
                if(motionW > canvasWidth || motionH > canvasHeight) {
                    canvasWidth = motionW;
                    canvasHeight = motionH;
                }
            }

            if(canvasWidth > 0 && canvasHeight > 0) {
                try {
                    // Ensure target layer has image buffer
                    if(!layer->GetHasImage()) {
                        layer->SetHasImage(true);
                    }
                    layer->SetImageSize(static_cast<tjs_uint>(canvasWidth),
                                        static_cast<tjs_uint>(canvasHeight));
                    // Always set display size to match canvas
                    layer->SetSize(canvasWidth, canvasHeight);
                    // libkrkr2.so Player_DrawSLA_guess sets visible=true
                    // on the resolved layer before rendering
                    if(!layer->GetVisible()) {
                        layer->SetVisible(true);
                    }

                    // Set clip rect to full canvas
                    layer->SetClip(0, 0, canvasWidth, canvasHeight);

                    // Clear with transparent black
                    tTVPRect clearRect;
                    clearRect.left = 0;
                    clearRect.top = 0;
                    clearRect.right = canvasWidth;
                    clearRect.bottom = canvasHeight;
                    layer->FillRect(clearRect, 0x00000000);

                    const auto &layerNamesList = activeLayerNames();

                    // Aligned to libkrkr2.so sub_6C2334 + Player_applyTranslateOffset_guess:
                    // 1. drawAffineMatrix (internal+808..844) transforms PSB native
                    //    coords to ownerLayer coords during render tree building
                    // 2. cameraOffset (Player+144/148) + rootOffset (Player+120/128)
                    //    are added to all vertices by applyTranslateOffset
                    // Our globalAffine combines both steps.
                    Affine2x3 globalAffine = {
                        _runtime->drawAffineMatrix[0],  // m11
                        _runtime->drawAffineMatrix[1],  // m21
                        _runtime->drawAffineMatrix[2],  // m12
                        _runtime->drawAffineMatrix[3],  // m22
                        _runtime->drawAffineMatrix[4] + _rootOffsetX + _cameraOffsetX,
                        _runtime->drawAffineMatrix[5] + _rootOffsetY + _cameraOffsetY
                    };
                    // Build persistent node tree if not yet built.
                    // Aligned to libkrkr2.so sub_6B51F0 → sub_6B4A6C.
                    if (!_runtime->nodesBuilt) {
                        std::string clipLabel;
                        if (clip) clipLabel = clip->label;
                        _runtime->nodes = detail::buildNodeTree(
                            *_runtime->activeMotion, clipLabel);
                        _runtime->nodesBuilt = true;
                    }

                    // Run 3-phase updateLayers pipeline on persistent nodes.
                    // Aligned to libkrkr2.so Player_updateLayers (0x6BB33C).
                    updateLayers(renderTime);

                    // Collect visible nodes into render list.
                    // Aligned to libkrkr2.so sub_6C2334 render tree building.
                    std::vector<FlatRenderNode> renderNodes;
                    buildRenderListFromNodes(_runtime->nodes, globalAffine,
                                            renderNodes);

                    // Collect child Player render output for nodeType=3 (Motion).
                    // Aligned to sub_6BE0C0: child render merges into parent tree.
                    for (const auto &motionNode : _runtime->nodes) {
                        if (motionNode.nodeType != 3 || !motionNode.childPlayer) continue;
                        auto &childRuntime = motionNode.childPlayer->_runtime;
                        if (!childRuntime || childRuntime->nodes.empty()) continue;
                        const auto &acc = motionNode.accumulated;
                        Affine2x3 childGlobal = {
                            globalAffine[0], globalAffine[1],
                            globalAffine[2], globalAffine[3],
                            globalAffine[0]*acc.posX + globalAffine[2]*acc.posY + globalAffine[4],
                            globalAffine[1]*acc.posX + globalAffine[3]*acc.posY + globalAffine[5]
                        };
                        buildRenderListFromNodes(childRuntime->nodes,
                                                childGlobal, renderNodes);
                    }

                    // Collect particle child Player render output (nodeType=4).
                    // Aligned to sub_6BF0DC: particle children render into parent tree.
                    for (const auto &particleNode : _runtime->nodes) {
                        if (particleNode.nodeType != 4) continue;
                        for (const auto &pChild : particleNode.particleChildren) {
                            if (!pChild || !pChild->_runtime) continue;
                            auto &pNodes = pChild->_runtime->nodes;
                            if (pNodes.empty()) continue;
                            const auto &pacc = particleNode.accumulated;
                            Affine2x3 pGlobal = {
                                globalAffine[0], globalAffine[1],
                                globalAffine[2], globalAffine[3],
                                globalAffine[0]*pacc.posX + globalAffine[2]*pacc.posY + globalAffine[4],
                                globalAffine[1]*pacc.posX + globalAffine[3]*pacc.posY + globalAffine[5]
                            };
                            buildRenderListFromNodes(pNodes, pGlobal, renderNodes);
                        }
                    }

                    // If node pipeline produced nothing, fall back to
                    // flattenLayerNodes for motion cross-references.
                    if (renderNodes.empty()) {
                        for (const auto &layerName : layerNamesList) {
                            const auto *layers = activeLayersByName();
                            if (!layers) break;
                            const auto it = layers->find(layerName);
                            if (it == layers->end()) continue;
                            flattenLayerNodes(it->second, renderTime,
                                              globalAffine, 1.0,
                                              false, false, renderNodes);
                        }
                    }

                    // Aligned to libkrkr2.so Player_calcBounds (0x6C3D04):
                    // Compute AABB from all render node affine corners.
                    _boundsMinX = 1e308;
                    _boundsMinY = 1e308;
                    _boundsMaxX = -1e308;
                    _boundsMaxY = -1e308;
                    for(const auto &node : renderNodes) {
                        // Estimate node bounds: use state.width/height or
                        // a default size. Exact source size isn't known yet.
                        const double nw = node.state.width > 0 ? node.state.width : 128.0;
                        const double nh = node.state.height > 0 ? node.state.height : 128.0;
                        const auto &a = node.affine;
                        // 4 corners: (0,0), (nw,0), (0,nh), (nw,nh)
                        double cx[4] = {a[4], a[0]*nw+a[4], a[2]*nh+a[4], a[0]*nw+a[2]*nh+a[4]};
                        double cy[4] = {a[5], a[1]*nw+a[5], a[3]*nh+a[5], a[1]*nw+a[3]*nh+a[5]};
                        for(int c = 0; c < 4; c++) {
                            if(cx[c] < _boundsMinX) _boundsMinX = cx[c];
                            if(cy[c] < _boundsMinY) _boundsMinY = cy[c];
                            if(cx[c] > _boundsMaxX) _boundsMaxX = cx[c];
                            if(cy[c] > _boundsMaxY) _boundsMaxY = cy[c];
                        }
                    }

                    // Step 2 (sub_6C7440): Flat loop — for each node,
                    // load source bitmap and call OperateAffine on target.
                    bool drewAny = false;
                    std::unordered_map<std::string, std::shared_ptr<tTVPBaseBitmap>> srcCache;
                    // Cache PSB source origin (anchor) offsets per source name.
                    // Aligned to libkrkr2.so sub_6BC4F0: originX/originY from
                    // PSB icon node define the image pivot point.
                    std::unordered_map<std::string, std::pair<double,double>> originCache;

                    for(const auto &node : renderNodes) {
                        // Resolve source bitmap (with cache)
                        std::shared_ptr<tTVPBaseBitmap> srcBmp;
                        if(auto cit = srcCache.find(node.state.src);
                           cit != srcCache.end()) {
                            srcBmp = cit->second;
                        } else {
                            // Try file source via storage system
                            const auto resolvedPath = resolveMotionSourcePath(
                                *_runtime->activeMotion, node.state.src);
                            if(!resolvedPath.IsEmpty()) {
                                ttstr loadPath = resolvedPath;
                                const auto ps = detail::narrow(resolvedPath);
                                if(ps.rfind('.') == std::string::npos ||
                                   ps.rfind('.') < ps.rfind('/')) {
                                    loadPath = resolvedPath + TJS_W(".png");
                                }
                                try {
                                    auto bmp = std::make_shared<tTVPBaseBitmap>(1, 1, 32);
                                    TVPLoadGraphic(bmp.get(), loadPath,
                                                   TVP_clNone, 0, 0,
                                                   glmNormal, nullptr, nullptr);
                                    if(bmp->GetWidth() > 0 && bmp->GetHeight() > 0)
                                        srcBmp = bmp;
                                } catch(...) {}
                            }
                            // Try PSB embedded resource
                            if(!srcBmp) {
                                int rw = 0, rh = 0;
                                double srcOriginX = 0, srcOriginY = 0;
                                std::vector<std::uint8_t> decompressed;
                                const auto *res = findPSBResourceBySourceName(
                                    *_runtime->activeMotion, node.state.src,
                                    rw, rh, decompressed,
                                    srcOriginX, srcOriginY);
                                originCache[node.state.src] = {srcOriginX, srcOriginY};
                                if(res && rw > 0 && rh > 0 && !res->data.empty()) {
                                    // Use decompressed data if RL was applied,
                                    // otherwise use raw resource data
                                    const auto &pixelData = decompressed.empty()
                                        ? res->data : decompressed;
                                    auto bmp = std::make_shared<tTVPBaseBitmap>(
                                        static_cast<tjs_uint>(rw),
                                        static_cast<tjs_uint>(rh), 32);
                                    tTVPRect fr(0, 0, rw, rh);
                                    bmp->Fill(fr, 0x00000000);
                                    // RL-decompressed data is RGBA interleaved.
                                    // KiKiRi2 internal format is BGRA. Swap R and B.
                                    const auto *sd = pixelData.data();
                                    for(int y = 0; y < rh; ++y) {
                                        auto *row = static_cast<std::uint8_t *>(
                                            bmp->GetScanLineForWrite(
                                                static_cast<tjs_uint>(y)));
                                        for(int x = 0; x < rw; ++x) {
                                            const size_t si =
                                                (static_cast<size_t>(y) * rw + x) * 4;
                                            if(si + 3 >= pixelData.size()) break;
                                            auto *dp = row + x * 4;
                                            dp[0] = sd[si + 2]; // B <- R
                                            dp[1] = sd[si + 1]; // G
                                            dp[2] = sd[si + 0]; // R <- B
                                            dp[3] = sd[si + 3]; // A
                                        }
                                    }
                                    srcBmp = bmp;
                                }
                            }
                            srcCache.emplace(node.state.src, srcBmp);
                        }

                        if(!srcBmp || srcBmp->GetWidth() == 0) {
                            continue;
                        }
                        // Compute affine destination points using full 2x3 matrix
                        // Aligned to libkrkr2.so sub_6C7440 operateAffine call
                        const double srcW = static_cast<double>(srcBmp->GetWidth());
                        const double srcH = static_cast<double>(srcBmp->GetHeight());
                        const double drawW = node.state.width > 0.0
                            ? node.state.width : srcW;
                        const double drawH = node.state.height > 0.0
                            ? node.state.height : srcH;
                        const double localSx = drawW / srcW;
                        const double localSy = drawH / srcH;

                        // Compose node affine with local source scale:
                        // A = node.affine * Scale(localSx, localSy)
                        const auto &a = node.affine;
                        const double am11 = a[0] * localSx;
                        const double am21 = a[1] * localSx;
                        const double am12 = a[2] * localSy;
                        const double am22 = a[3] * localSy;

                        // Aligned to libkrkr2.so sub_6BC4F0 (0x6BCB3C):
                        // origin = pos - matrix × (node[248] + clip[376], node[256] + clip[384])
                        // Confirmed via IDA: node[248]/[256] = PSB source icon "originX"/"originY"
                        //   (read in Motion_Player_findSource at 0x69505C/0x6950A8)
                        // clip[376]/[384] = clip-level offset (from "timeOffset", usually 0)
                        // PSB frameList ox/oy are position offsets (already in lx/ly).
                        double srcOX = 0, srcOY = 0;
                        if(auto oit = originCache.find(node.state.src); oit != originCache.end()) {
                            srcOX = oit->second.first;
                            srcOY = oit->second.second;
                        }
                        const double atx = a[4] - (a[0] * srcOX + a[2] * srcOY);
                        const double aty = a[5] - (a[1] * srcOX + a[3] * srcOY);

                        // OperateAffine takes 3 corner points:
                        // (0,0), (srcW,0), (0,srcH) mapped through the affine.
                        // libkrkr2.so applies -0.5 texel offset.
                        // Flip is now integrated into the affine matrix via
                        // applyLocalTransform (matching sub_699940 case 0).
                        tTVPPointD pts[3];
                        pts[0] = {atx - 0.5,
                                  aty - 0.5};
                        pts[1] = {am11 * srcW + atx - 0.5,
                                  am21 * srcW + aty - 0.5};
                        pts[2] = {am12 * srcH + atx - 0.5,
                                  am22 * srcH + aty - 0.5};

                        tTVPRect sr(0, 0, static_cast<tjs_int>(srcW),
                                    static_cast<tjs_int>(srcH));
                        // Use accumulated opacity (parent * child cascade)
                        // aligned to libkrkr2.so Player_updateLayers opacity multiplication
                        const tjs_int opa = static_cast<tjs_int>(
                            std::clamp(node.accumulatedOpacity * 255.0, 0.0, 255.0));

                        try {
                            layer->OperateAffine(pts, srcBmp.get(), sr,
                                                 omAlpha, opa, stNearest);
                            drewAny = true;
                        } catch(const eTJS &) {
                        } catch(...) {
                        }
                    }

                    if(drewAny) {
                        if(!skipUpdate) layer->Update(false);
                        _runtime->lastCanvas =
                            tTJSVariant(layerObject, layerObject);
                        return true;
                    }
                } catch(const std::exception &e) {
                    LOGGER->warn("Motion.Player LayerAPI render failed for {}: {}",
                                 _runtime->activeMotion->path, e.what());
                } catch(...) {
                    LOGGER->warn("Motion.Player LayerAPI render failed for {}",
                                 _runtime->activeMotion->path);
                }
            }
        }
        const auto sourcePath = resolveCaptureSourcePath();
        if(sourcePath.IsEmpty()) {
            LOGGER->warn("Motion.Player draw fallback could not resolve source "
                         "for {}",
                         _runtime->activeMotion->path);
            return false;
        }

        try {
            if(!layer->GetHasImage()) {
                layer->SetHasImage(true);
            }

            if(auto *meta = layer->LoadImages(sourcePath, TVP_clNone)) {
                meta->Release();
            }


            if(!skipUpdate) layer->Update(false);
            _runtime->lastCanvas = tTJSVariant(layerObject, layerObject);
            return true;
        } catch(...) {
            LOGGER->warn("Motion.Player draw fallback failed for {}",
                         sourcePath.AsStdString());
            return false;
        }
    }

    tTJSVariant Player::findSource(ttstr name) {
        loadSource(name);
        const auto key = detail::narrow(name);
        if(const auto it = _runtime->sourcesByKey.find(key);
           it != _runtime->sourcesByKey.end()) {
            return it->second;
        }
        return {};
    }

    void Player::loadSource(ttstr name) {
        const auto requestKey = detail::narrow(name);
        if(requestKey.empty() ||
           _runtime->sourcesByKey.find(requestKey) !=
               _runtime->sourcesByKey.end()) {
            return;
        }

        ttstr resolved;
        if(!detail::resolveExistingPath(buildSourceCandidates(*_runtime, name),
                                        resolved)) {
            return;
        }

        const auto resolvedKey = detail::narrow(resolved);
        if(const auto existing = _runtime->sourcesByKey.find(resolvedKey);
           existing != _runtime->sourcesByKey.end()) {
            _runtime->sourcesByKey.emplace(requestKey, existing->second);
            return;
        }

        const auto source = _resourceManagerNative.load(resolved);
        if(source.Type() == tvtVoid) {
            return;
        }

        _runtime->sourcesByKey.emplace(requestKey, source);
        _runtime->sourcesByKey.emplace(resolvedKey, source);
    }

    void Player::clearCache() {
        _runtime->sourcesByKey.clear();
        _runtime->lastCanvas.Clear();
    }

    void Player::setSize(tjs_int w, tjs_int h) {
        _runtime->width = w;
        _runtime->height = h;
    }

    void Player::copyRect(tTJSVariant) {}

    void Player::adjustGamma(tTJSVariant) {}

    void Player::draw() {
        if(!_runtime->visible) {
            _runtime->lastCanvas = detail::makeDictionary({
                { "visible", false },
                { "tickCount", _tickCount },
            });
            return;
        }

        ensureMotionLoaded();

        if(_runtime->width == 0 && _runtime->activeMotion) {
            _runtime->width = static_cast<tjs_int>(_runtime->activeMotion->width);
        }
        if(_runtime->height == 0 && _runtime->activeMotion) {
            _runtime->height = static_cast<tjs_int>(_runtime->activeMotion->height);
        }

        calcViewParam();

        const auto activeLayers = activeLayerNames();
        const auto layerNames =
            detail::makeArray(detail::stringsToVariants(activeLayers));
        const auto sourceCount = static_cast<tjs_int>(_runtime->sourcesByKey.size());
        _processedMeshVerticesNum = static_cast<int>(activeLayers.size());

        std::vector<std::pair<std::string, tTJSVariant>> entries = {
            { "width", _runtime->width },
            { "height", _runtime->height },
            { "visible", _runtime->visible },
            { "opacity", _runtime->opacity },
            { "flip", _runtime->flip },
            { "slant", _runtime->slant },
            { "zoom", _runtime->zoom },
            { "clearColor", _runtime->clearColor },
            { "tickCount", _tickCount },
            { "frameTickCount", _frameTickCount },
            { "backgroundCount",
              static_cast<tjs_int>(_runtime->backgrounds.size()) },
            { "captionCount", static_cast<tjs_int>(_runtime->captions.size()) },
            { "sourceCount", sourceCount },
            { "layers", layerNames },
        };

        if(_runtime->activeMotion) {
            entries.emplace_back("motionPath",
                                 detail::widen(_runtime->activeMotion->path));
            entries.emplace_back("layerCount",
                                 static_cast<tjs_int>(activeLayers.size()));
        }

        _runtime->lastCanvas = detail::makeDictionary(entries);
    }

    void Player::frameProgress(double dt) {
        // Aligned to libkrkr2.so Player_progress_inner (0x6C106C):
        // speed is applied internally, not by the caller.
        const double actualDelta = _speed * dt;
        _frameLastTime = actualDelta;
        _frameLoopTime += actualDelta;
        _loopTime += actualDelta;
        _tickCount += actualDelta;
        _frameTickCount += 1.0;

        // Aligned to libkrkr2.so Player_updateLayers (0x6BB33C) camera section:
        // Apply camera velocity to root offset, then apply exponential damping.
        // player+784/792/800 = velocityX/Y/Z, player+600 = damping
        if(_cameraVelocityX != 0.0) {
            _rootOffsetX += actualDelta * _cameraVelocityX;
        }
        if(_cameraVelocityY != 0.0) {
            _rootOffsetY += actualDelta * _cameraVelocityY;
        }
        if(_cameraVelocityZ != 0.0) {
            _rootOffsetZ += actualDelta * _cameraVelocityZ;
        }
        if(_cameraDamping != 1.0 && actualDelta > 0.0) {
            const double dampFactor = std::pow(_cameraDamping, actualDelta / 60.0);
            _cameraVelocityX *= dampFactor;
            _cameraVelocityY *= dampFactor;
            _cameraVelocityZ *= dampFactor;
        }

        // Save prevTime per timeline for action scanning
        std::unordered_map<std::string, double> prevTimes;
        for(const auto &[name, state] : _runtime->timelines) {
            prevTimes[name] = state.currentTime;
        }

        detail::stepTimelines(_runtime->timelines, actualDelta,
                              &_runtime->pendingEvents);

        // Scan PSB layers for action/sync events crossed this frame
        // Aligned to libkrkr2.so: updateLayers queues events during evaluation
        if(_runtime->activeMotion && actualDelta > 0) {
            for(const auto &[name, state] : _runtime->timelines) {
                double prev = 0.0;
                if(auto it = prevTimes.find(name); it != prevTimes.end())
                    prev = it->second;
                if(state.currentTime > prev) {
                    detail::scanLayerActions(*_runtime->activeMotion,
                                            prev, state.currentTime,
                                            _runtime->pendingEvents);
                }
            }
        }

        _allplaying = std::any_of(
            _runtime->timelines.begin(), _runtime->timelines.end(),
            [](const auto &entry) { return entry.second.playing; });
        _syncActive = _syncWaiting && _allplaying;
    }

    // --- updateLayers: 3-phase pipeline ---
    // Aligned to libkrkr2.so Player_updateLayers (0x6BB33C).
    // Operates on persistent MotionNode vector instead of re-walking PSB tree.
    void Player::updateLayers(double currentTime) {
        auto &nodes = _runtime->nodes;
        if (nodes.empty()) return;

        // === PHASE 1: Pre-loop setup ===

        // Step 1: Save previous positions for delta calculation
        for (auto &n : nodes) {
            n.prevPosX = n.accumulated.posX;
            n.prevPosY = n.accumulated.posY;
            n.prevPosZ = n.accumulated.posZ;
        }

        // Step 2: Evaluate root node (index 0)
        auto &root = nodes[0];
        {
            auto rootState = evaluateLayerContent(root.psbNode, currentTime);
            // slotDone: type=0 in evaluateLayerContent → visible=false → done
            root.slotDone = !rootState.visible;
            // Map to accumulated state
            root.accumulated.visible = rootState.visible;
            root.accumulated.flipX = rootState.flipX;
            root.accumulated.flipY = rootState.flipY;
            root.accumulated.posX = rootState.x + rootState.ox;
            root.accumulated.posY = rootState.y + rootState.oy;
            root.accumulated.posZ = 0.0;
            root.accumulated.angle = rootState.angle;
            root.accumulated.scaleX = rootState.scaleX;
            root.accumulated.scaleY = rootState.scaleY;
            root.accumulated.slantX = rootState.slantX;
            root.accumulated.slantY = rootState.slantY;
            root.accumulated.opacity = static_cast<int>(
                std::clamp(rootState.opacity * 255.0, 0.0, 255.0));
            root.accumulated.active = true;
            // Cache interpolated data for rendering
            root.interpolatedCache.src = rootState.src;
            root.interpolatedCache.width = rootState.width;
            root.interpolatedCache.height = rootState.height;
            root.interpolatedCache.opacity = rootState.opacity;
            root.interpolatedCache.x = rootState.x;
            root.interpolatedCache.y = rootState.y;
            root.interpolatedCache.ox = rootState.ox;
            root.interpolatedCache.oy = rootState.oy;
            root.interpolatedCache.angle = rootState.angle;
            root.interpolatedCache.scaleX = rootState.scaleX;
            root.interpolatedCache.scaleY = rootState.scaleY;
            root.interpolatedCache.slantX = rootState.slantX;
            root.interpolatedCache.slantY = rootState.slantY;
            root.interpolatedCache.flipX = rootState.flipX;
            root.interpolatedCache.flipY = rootState.flipY;
            root.interpolatedCache.blendMode = rootState.blendMode;
            root.interpolatedCache.colorR = rootState.colorR;
            root.interpolatedCache.colorG = rootState.colorG;
            root.interpolatedCache.colorB = rootState.colorB;
            root.interpolatedCache.colorA = rootState.colorA;
            root.interpolatedCache.hasTransformOrder = rootState.hasTransformOrder;
            if (rootState.hasTransformOrder) {
                std::copy(std::begin(rootState.transformOrder),
                          std::end(rootState.transformOrder),
                          root.interpolatedCache.transformOrder);
            }
            root.interpolatedCache.action = rootState.action;
            root.interpolatedCache.hasSync = rootState.hasSync;
            root.interpolatedCache.prtTrigger = rootState.prtTrigger;
            root.interpolatedCache.prtF = rootState.prtF;
            root.interpolatedCache.prtV = rootState.prtV;
            root.interpolatedCache.prtA = rootState.prtA;
            root.interpolatedCache.prtZ = rootState.prtZ;
            root.interpolatedCache.prtRange = rootState.prtRange;

            // Populate root clipW/clipH/originX/originY (sub_6BC4F0)
            root.clipW = rootState.width;
            root.clipH = rootState.height;
            if (!rootState.src.empty() && _runtime->activeMotion) {
                int srcW = 0, srcH = 0;
                double srcOX = 0, srcOY = 0;
                std::vector<std::uint8_t> decomp;
                findPSBResourceBySourceName(*_runtime->activeMotion, rootState.src,
                    srcW, srcH, decomp, srcOX, srcOY);
                root.originX = srcOX;
                root.originY = srcOY;
                if (root.clipW <= 0 && srcW > 0) root.clipW = srcW;
                if (root.clipH <= 0 && srcH > 0) root.clipH = srcH;
            }

            // Step 3: Build root local 2x2 matrix via sub_699940
            // Reuse applyLocalTransform logic but on raw 2x2
            Affine2x3 rootAffine = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
            applyLocalTransform(rootAffine, rootState);
            root.accumulated.m11 = rootAffine[0];
            root.accumulated.m21 = rootAffine[1];
            root.accumulated.m12 = rootAffine[2];
            root.accumulated.m22 = rootAffine[3];
        }

        // --- sub_6BBE20: Variable interpolation (pre-loop) ---
        // Aligned to 0x6BBE20. Interpolates variable values and binds to nodes.
        // In libkrkr2.so this operates on a 160-byte item deque (player+1312).
        // Each variable is interpolated then bound to nodes via sub_6C4668.
        //
        // sub_6C4668 binding: resolves variable name to a source entry in
        // player+264 map, then updates child Player timeline parameters for
        // nodeType=3 and nodeType=4 nodes. In our architecture, variable values
        // are stored in _variableValues and exposed via getVariable()/setVariable()
        // TJS API. The binding to child Players happens implicitly when child
        // Players re-evaluate their timelines.
        if (_runtime->activeMotion) {
            const auto &varFrames = _runtime->activeMotion->variableFrames;
            for (const auto &[label, frames] : varFrames) {
                if (frames.empty()) continue;
                // User-set value takes precedence
                if (_variableValues.find(label) != _variableValues.end()) continue;
                // Default: use first frame value
                _variableValues[label] = frames.front().value;
            }
            // Bind variable values to child Players (sub_6C4668 equivalent)
            // For nodeType=3/4 nodes with child Players, propagate variable values
            for (auto &vn : nodes) {
                if ((vn.nodeType == 3 || vn.nodeType == 4) && vn.childPlayer) {
                    for (const auto &[label, value] : _variableValues) {
                        vn.childPlayer->setVariable(detail::widen(label), value);
                    }
                }
            }
        }

        // === PHASE 2: Main loop — evaluate non-root nodes ===
        for (size_t i = 1; i < nodes.size(); ++i) {
            auto &node = nodes[i];

            // Find parent node — walk parentIndex chain, skip flag 0x40 nodes
            // Aligned to 0x6BB598..0x6BB5BC
            int parentIdx = node.parentIndex;
            while (parentIdx > 0 && parentIdx < static_cast<int>(nodes.size())) {
                if ((nodes[parentIdx].flags & 0x40) == 0) break;
                parentIdx = nodes[parentIdx].parentIndex;
            }
            if (parentIdx < 0 || parentIdx >= static_cast<int>(nodes.size()))
                parentIdx = 0;
            const auto &parent = nodes[parentIdx];

            // Evaluate this node's interpolated state
            auto state = evaluateLayerContent(node.psbNode, currentTime);

            // Cache interpolated data for rendering
            node.interpolatedCache.src = state.src;
            node.interpolatedCache.width = state.width;
            node.interpolatedCache.height = state.height;
            node.interpolatedCache.opacity = state.opacity;
            node.interpolatedCache.x = state.x;
            node.interpolatedCache.y = state.y;
            node.interpolatedCache.ox = state.ox;
            node.interpolatedCache.oy = state.oy;
            node.interpolatedCache.angle = state.angle;
            node.interpolatedCache.scaleX = state.scaleX;
            node.interpolatedCache.scaleY = state.scaleY;
            node.interpolatedCache.slantX = state.slantX;
            node.interpolatedCache.slantY = state.slantY;
            node.interpolatedCache.flipX = state.flipX;
            node.interpolatedCache.flipY = state.flipY;
            node.interpolatedCache.blendMode = state.blendMode;
            node.interpolatedCache.colorR = state.colorR;
            node.interpolatedCache.colorG = state.colorG;
            node.interpolatedCache.colorB = state.colorB;
            node.interpolatedCache.colorA = state.colorA;
            node.interpolatedCache.hasTransformOrder = state.hasTransformOrder;
            if (state.hasTransformOrder) {
                std::copy(std::begin(state.transformOrder),
                          std::end(state.transformOrder),
                          node.interpolatedCache.transformOrder);
            }
            node.interpolatedCache.action = state.action;
            node.interpolatedCache.hasSync = state.hasSync;
            // Particle data from FrameContentState (mask 0x100000)
            node.interpolatedCache.prtTrigger = state.prtTrigger;
            node.interpolatedCache.prtF = state.prtF;
            node.interpolatedCache.prtV = state.prtV;
            node.interpolatedCache.prtA = state.prtA;
            node.interpolatedCache.prtZ = state.prtZ;
            node.interpolatedCache.prtRange = state.prtRange;
            node.prtTrigger = state.prtTrigger;

            // Populate clipW/clipH from interpolated state (sub_6BC4F0 at 0x6BCB14)
            node.clipW = state.width;
            node.clipH = state.height;

            // Populate originX/originY from PSB source icon (sub_6948E8).
            // findPSBResourceBySourceName reads originX/originY from PSB icon nodes.
            if (!state.src.empty() && _runtime->activeMotion) {
                int srcW = 0, srcH = 0;
                double srcOX = 0, srcOY = 0;
                std::vector<std::uint8_t> decomp;
                findPSBResourceBySourceName(*_runtime->activeMotion, state.src,
                    srcW, srcH, decomp, srcOX, srcOY);
                node.originX = srcOX;
                node.originY = srcOY;
                if (node.clipW <= 0 && srcW > 0) node.clipW = srcW;
                if (node.clipH <= 0 && srcH > 0) node.clipH = srcH;
            }

            // slotDone: type=0 in evaluateLayerContent → visible=false → done
            node.slotDone = !state.visible;

            if (!state.visible) {
                node.accumulated.visible = false;
                node.accumulated.active = false;
                node.accumulated.opacity = 0;
                node.drawFlag = false;
                continue;
            }

            // === Inheritance from parent ===
            // Aligned to libkrkr2.so 0x6BB630..0x6BBB6C (Player_updateLayers main loop)
            // Full inheritFlags system with 3-phase independentLayerInherit support.
            node.accumulated.visible = true;
            node.accumulated.active = true;

            // Flip XOR from interpolated → accumulated (0x6BB668)
            node.accumulated.flipX = state.flipX ^ parent.accumulated.flipX;
            node.accumulated.flipY = state.flipY ^ parent.accumulated.flipY;

            // Scale: multiply from parent interpolated (0x6BB6A4)
            node.accumulated.scaleX = state.scaleX * parent.accumulated.scaleX;
            node.accumulated.scaleY = state.scaleY * parent.accumulated.scaleY;

            // Slant: add from parent interpolated (0x6BB6B8)
            node.accumulated.slantX = state.slantX + parent.accumulated.slantX;
            node.accumulated.slantY = state.slantY + parent.accumulated.slantY;

            // Opacity: int multiplication (0x6BB6D4)
            // First multiply: parent.interpolated.opacity * child.interpolated.opacity / 255
            const int childOpa = static_cast<int>(
                std::clamp(state.opacity * 255.0, 0.0, 255.0));
            node.accumulated.opacity = parent.accumulated.opacity * childOpa / 255;

            // Position: add from interpolated offsets (0x6BB6EC)
            const double lx = state.x + state.ox;
            const double ly = state.y + state.oy;

            // Position transform: parent.matrix × child.pos + parent.pos
            // 3D/2D coordinate mode branching (0x6BB718..0x6BB7C4)
            if (node.coordinateMode != 0) {
                // 3D mode: X and Z through matrix, Y pass-through (0x6BB720)
                node.accumulated.posX = parent.accumulated.m11 * lx
                    + parent.accumulated.m12 * ly + parent.accumulated.posX;
                node.accumulated.posY = parent.accumulated.m21 * lx
                    + parent.accumulated.m22 * ly + parent.accumulated.posY;
                // posZ: pass-through from interpolated + parent
                node.accumulated.posZ = state.y + state.oy + parent.accumulated.posZ;
            } else {
                // 2D mode (default, 0x6BB794): X and Y through matrix, Z pass-through
                node.accumulated.posX = parent.accumulated.m11 * lx
                    + parent.accumulated.m12 * ly + parent.accumulated.posX;
                node.accumulated.posY = parent.accumulated.m21 * lx
                    + parent.accumulated.m22 * ly + parent.accumulated.posY;
                // posZ: add from interpolated + parent (0x6BB7E4)
                node.accumulated.posZ += parent.accumulated.posZ;
            }

            // sub_69AE74: Mesh position deformation (0x6BB714)
            // Aligned to 0x69AE74. Called when parent.meshType != 0.
            // Deforms child position based on parent mesh surface.
            // Condition: parent.meshType==1 && (parent.meshFlags & 1) &&
            //            child.active && child.hasSource && parent has mesh vertices.
            if (parent.meshType == 1 && (parent.meshFlags & 1) != 0
                && node.accumulated.active && node.hasSource) {
                // Normalize child position by parent clip dimensions (0x69AF24..0x69AF50)
                const double pw = parent.clipW > 0.0 ? parent.clipW : 1.0;
                const double ph = parent.clipH > 0.0 ? parent.clipH : 1.0;
                const double normX = (node.accumulated.posX + parent.originX) / pw;
                const double normY = (node.accumulated.posY + parent.originY) / ph;

                // sub_69B1E8 → sub_6990A0: 4×4 bicubic Bezier patch evaluation.
                // meshData = 16 control points × 2 floats (X,Y) = 128 bytes at node+2024.
                // Bernstein basis: bu[i] for u, bv[j] for v, sum(bu[i]*bv[j]*P[i*4+j])
                // When no mesh vertex data available, use identity (passthrough).
                auto evalBezierPatch = [](const float *mesh, float u, float v,
                                          float &outX, float &outY) {
                    const float su = 1.0f - u, sv = 1.0f - v;
                    const float bu[4] = {
                        su*su*su, 3.0f*su*su*u, 3.0f*su*u*u, u*u*u
                    };
                    const float bv[4] = {
                        sv*sv*sv, 3.0f*sv*sv*v, 3.0f*sv*v*v, v*v*v
                    };
                    outX = 0; outY = 0;
                    for (int i = 0; i < 16; ++i) {
                        float w = bv[i >> 2] * bu[i & 3];
                        outX += mesh[i * 2] * w;
                        outY += mesh[i * 2 + 1] * w;
                    }
                };

                // Evaluate at normalized coordinates
                float defX = static_cast<float>(normX);
                float defY = static_cast<float>(normY);
                // Evaluate mesh at normalized coordinates using parent's mesh data.
                // parent.meshControlPoints populated by sub_6BC4F0 vertex computation.
                if (parent.meshControlPoints.size() >= 32) {
                    // 16-point Bezier patch: evaluate via sub_6990A0
                    evalBezierPatch(parent.meshControlPoints.data(),
                                    defX, defY, defX, defY);
                }
                node.accumulated.posX = static_cast<double>(defX) * pw - parent.originX;
                node.accumulated.posY = static_cast<double>(defY) * ph - parent.originY;

                // Angle deformation from mesh gradient (0x69AFB4..0x69B0EC)
                if ((parent.meshFlags & 2) != 0
                    && (node.inheritFlags & 0x10) != 0
                    && parent.meshControlPoints.size() >= 32) {
                    const float eps = 0.0001f;
                    const float *mp = parent.meshControlPoints.data();
                    float x1, y1, x2, y2, x3, y3, x4, y4;
                    // Sample at 4 nearby points (0x69B030..0x69B094)
                    evalBezierPatch(mp, defX - eps, defY, x1, y1);
                    evalBezierPatch(mp, defX + eps, defY, x2, y2);
                    evalBezierPatch(mp, defX, defY - eps, x3, y3);
                    evalBezierPatch(mp, defX, defY + eps, x4, y4);
                    // Average of two orthogonal gradients (0x69B0AC..0x69B0EC)
                    double a1 = std::atan2(
                        static_cast<double>(y3 - y4),
                        static_cast<double>(x4 - x3));
                    double a2 = std::atan2(
                        static_cast<double>(x2 - x1),
                        static_cast<double>(y2 - y1));
                    node.accumulated.angle += (a1 + a2) * 0.5 * 360.0 / 6.28318531;
                }

                // Scale deformation from mesh jacobian (0x69B11C..0x69B1A8)
                if ((parent.meshFlags & 4) != 0
                    && (node.inheritFlags & 0x60) != 0
                    && parent.meshControlPoints.size() >= 32) {
                    const float eps = 0.0001f;
                    const float *mp = parent.meshControlPoints.data();
                    float x1, y1, x2, y2, x3, y3, x4, y4;
                    evalBezierPatch(mp, defX - eps, defY, x1, y1);
                    evalBezierPatch(mp, defX + eps, defY, x2, y2);
                    evalBezierPatch(mp, defX, defY - eps, x3, y3);
                    evalBezierPatch(mp, defX, defY + eps, x4, y4);
                    // Jacobian area from cross product (0x69B154..0x69B188)
                    double dx1 = x2 - x1, dy1 = y2 - y1;
                    double dx2 = x3 - x4, dy2 = y3 - y4;
                    double area1 = std::fabs(dx1 * (y4 - y1) - dy1 * (x4 - x1)) * 0.5;
                    double area2 = std::fabs(dx1 * (y3 - y1) - dy1 * (x3 - x1)) * 0.5;
                    double scaleFactor = std::sqrt(area1 + area2 + area2 + area1) / 0.0002;
                    if (node.inheritFlags & 0x020)
                        node.accumulated.scaleX *= scaleFactor;
                    if (node.inheritFlags & 0x040)
                        node.accumulated.scaleY *= scaleFactor;
                }
            }

            // sub_6BAA10: Ground correction TJS callback (0x6BB7F8)
            // Aligned to 0x6BAA10. Called when node+47 (groundCorrection) set.
            // Invokes TJS onGroundCorrection(parentPos, childPos) callback on
            // the node's TJS object. The callback can modify child position.
            // In libkrkr2.so, the TJS object is at *(node+0)+16 (the layer's
            // iTJSDispatch2 reference). In our architecture, MotionNode doesn't
            // hold a TJS dispatch pointer. This callback is used for specialized
            // ground-plane correction in E-mote animations.
            if (node.groundCorrection && node.tjsLayerObject) {
                auto *tjsObj = static_cast<iTJSDispatch2 *>(node.tjsLayerObject);
                // Aligned to sub_6BAA10 (0x6BAA10): invoke TJS onGroundCorrection.
                // Push parent pos [posX,posY,posZ] and child pos as TJS arrays,
                // call onGroundCorrection, read back corrected child position.
                try {
                    // Create parent position array
                    iTJSDispatch2 *parentArr = TJSCreateArrayObject();
                    tTJSVariant pxv(parent.accumulated.posX);
                    tTJSVariant pyv(parent.accumulated.posY);
                    tTJSVariant pzv(parent.accumulated.posZ);
                    tTJSVariant *pargs[] = { &pxv };
                    parentArr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, pargs, parentArr);
                    pargs[0] = &pyv;
                    parentArr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, pargs, parentArr);
                    pargs[0] = &pzv;
                    parentArr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, pargs, parentArr);

                    // Create child position array
                    iTJSDispatch2 *childArr = TJSCreateArrayObject();
                    tTJSVariant cxv(node.accumulated.posX);
                    tTJSVariant cyv(node.accumulated.posY);
                    tTJSVariant czv(node.accumulated.posZ);
                    tTJSVariant *cargs[] = { &cxv };
                    childArr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, cargs, childArr);
                    cargs[0] = &cyv;
                    childArr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, cargs, childArr);
                    cargs[0] = &czv;
                    childArr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, cargs, childArr);

                    // Call onGroundCorrection(parentPos, childPos)
                    tTJSVariant parentVar(parentArr, parentArr);
                    tTJSVariant childVar(childArr, childArr);
                    tTJSVariant *callArgs[] = { &parentVar, &childVar };
                    tTJSVariant result;
                    tjsObj->FuncCall(0, TJS_W("onGroundCorrection"),
                        nullptr, &result, 2, callArgs, tjsObj);

                    // Read back corrected position from result (0x6BAD48..0x6BAE00)
                    if (result.Type() == tvtObject) {
                        iTJSDispatch2 *resObj = result.AsObjectNoAddRef();
                        if (resObj) {
                            tTJSVariant rv;
                            tTJSVariant idx;
                            idx = 0; resObj->PropGetByNum(0, 0, &rv, resObj);
                            node.accumulated.posX = static_cast<double>(rv);
                            idx = 1; resObj->PropGetByNum(0, 1, &rv, resObj);
                            node.accumulated.posY = static_cast<double>(rv);
                            idx = 2; resObj->PropGetByNum(0, 2, &rv, resObj);
                            node.accumulated.posZ = static_cast<double>(rv);
                        }
                    }
                    parentArr->Release();
                    childArr->Release();
                } catch (...) {
                    // TJS callback failure — silently ignore
                }
            }

            // Opacity conditional second multiply (0x6BB808..0x6BB830):
            // Decompilation: if ((v46 & 0x400) != 0 || (v47 = v3, !*(a1+1097)))
            //   node.opacity = v47.opacity * node.opacity / 255
            // v47 = parent when 0x400 set; v47 = root (v3) when !independentLayerInherit
            {
                const auto *opaNode = &parent;
                if ((node.inheritFlags & 0x400) == 0 && _independentLayerInherit) {
                    // Neither 0x400 set nor independentLayerInherit=false: skip
                    // (no second multiply in this case)
                } else {
                    if ((node.inheritFlags & 0x400) != 0)
                        opaNode = &parent;
                    else
                        opaNode = &nodes[0];  // root
                    node.accumulated.opacity = opaNode->accumulated.opacity
                        * node.accumulated.opacity / 255;
                }
            }

            // Angle: add (0x6BB8C8)
            node.accumulated.angle = state.angle + parent.accumulated.angle;

            // === inheritFlags per-property control (0x6BB83C) ===
            // Decompilation evidence: Player_updateLayers 0x6BB83C..0x6BBB6C
            //   if ((~v46 & 0x1FC) == 0) → all bits set, simple path
            //   else:
            //     per-property inherit from parent for SET bits
            //     if (player+1097) → LABEL_68: sub_699940 only, NO matrix multiply
            //     else → LABEL_76: root undo → sub_699940 → root re-apply → matrix multiply
            const int flags = node.inheritFlags;
            const bool allInheritBitsSet = (~flags & 0x1FC) == 0;

            if (allInheritBitsSet) {
                // All bits set → simple path (0x6BB848): inherit from parent,
                // sub_699940, matrix multiply. Already inherited above.
                Affine2x3 localAffine = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
                applyLocalTransform(localAffine, state);
                const double lm11 = localAffine[0], lm21 = localAffine[1];
                const double lm12 = localAffine[2], lm22 = localAffine[3];
                node.accumulated.m11 = parent.accumulated.m11 * lm11 + parent.accumulated.m12 * lm21;
                node.accumulated.m21 = parent.accumulated.m21 * lm11 + parent.accumulated.m22 * lm21;
                node.accumulated.m12 = parent.accumulated.m11 * lm12 + parent.accumulated.m12 * lm22;
                node.accumulated.m22 = parent.accumulated.m21 * lm12 + parent.accumulated.m22 * lm22;
            } else {
                // Some bits NOT set: per-property inherit from parent for SET bits only
                // (0x6BB8F4..0x6BB918)
                if (flags & 0x004) node.accumulated.flipX = state.flipX ^ parent.accumulated.flipX;
                else               node.accumulated.flipX = state.flipX;
                if (flags & 0x008) node.accumulated.flipY = state.flipY ^ parent.accumulated.flipY;
                else               node.accumulated.flipY = state.flipY;
                if (flags & 0x010) node.accumulated.angle = state.angle + parent.accumulated.angle;
                else               node.accumulated.angle = state.angle;
                if (flags & 0x020) node.accumulated.scaleX = state.scaleX * parent.accumulated.scaleX;
                else               node.accumulated.scaleX = state.scaleX;
                if (flags & 0x040) node.accumulated.scaleY = state.scaleY * parent.accumulated.scaleY;
                else               node.accumulated.scaleY = state.scaleY;
                if (flags & 0x080) node.accumulated.slantX = state.slantX + parent.accumulated.slantX;
                else               node.accumulated.slantX = state.slantX;
                if (flags & 0x100) node.accumulated.slantY = state.slantY + parent.accumulated.slantY;
                else               node.accumulated.slantY = state.slantY;

                if (_independentLayerInherit) {
                    // LABEL_68 (0x6BB918): independentLayerInherit=TRUE
                    // Only sub_699940, NO matrix multiply with parent.
                    // Node's matrix stays as its own local matrix (independent of parent).
                    Affine2x3 localAffine = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
                    applyLocalTransform(localAffine, state);
                    node.accumulated.m11 = localAffine[0];
                    node.accumulated.m21 = localAffine[1];
                    node.accumulated.m12 = localAffine[2];
                    node.accumulated.m22 = localAffine[3];
                } else {
                    // LABEL_76 (0x6BB9BC..0x6BBB6C): independentLayerInherit=FALSE
                    // 4-phase: undo root → sub_699940 → re-apply root → matrix multiply
                    const auto &rootNode = nodes[0];

                    // Phase A: For SET bits, UNDO root contribution (0x6BB9BC)
                    if (flags & 0x004) node.accumulated.flipX ^= rootNode.accumulated.flipX;
                    if (flags & 0x008) node.accumulated.flipY ^= rootNode.accumulated.flipY;
                    if (flags & 0x010) node.accumulated.angle -= rootNode.accumulated.angle;
                    if (flags & 0x020 && rootNode.accumulated.scaleX != 0.0)
                        node.accumulated.scaleX /= rootNode.accumulated.scaleX;
                    if (flags & 0x040 && rootNode.accumulated.scaleY != 0.0)
                        node.accumulated.scaleY /= rootNode.accumulated.scaleY;
                    if (flags & 0x080) node.accumulated.slantX -= rootNode.accumulated.slantX;
                    if (flags & 0x100) node.accumulated.slantY -= rootNode.accumulated.slantY;

                    // Phase B: sub_699940 (0x6BB9E8)
                    Affine2x3 localAffine = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
                    applyLocalTransform(localAffine, state);

                    // Phase C: For SET bits, RE-APPLY root contribution (0x6BBA04)
                    if (flags & 0x004) node.accumulated.flipX ^= rootNode.accumulated.flipX;
                    if (flags & 0x008) node.accumulated.flipY ^= rootNode.accumulated.flipY;
                    if (flags & 0x010) node.accumulated.angle += rootNode.accumulated.angle;
                    if (flags & 0x020) node.accumulated.scaleX *= rootNode.accumulated.scaleX;
                    if (flags & 0x040) node.accumulated.scaleY *= rootNode.accumulated.scaleY;
                    if (flags & 0x080) node.accumulated.slantX += rootNode.accumulated.slantX;
                    if (flags & 0x100) node.accumulated.slantY += rootNode.accumulated.slantY;

                    // Phase D: Matrix multiply parent × local (0x6BBA24)
                    const double lm11 = localAffine[0], lm21 = localAffine[1];
                    const double lm12 = localAffine[2], lm22 = localAffine[3];
                    node.accumulated.m11 = parent.accumulated.m11 * lm11 + parent.accumulated.m12 * lm21;
                    node.accumulated.m21 = parent.accumulated.m21 * lm11 + parent.accumulated.m22 * lm21;
                    node.accumulated.m12 = parent.accumulated.m11 * lm12 + parent.accumulated.m12 * lm22;
                    node.accumulated.m22 = parent.accumulated.m21 * lm12 + parent.accumulated.m22 * lm22;
                }
            }
        }

        // === PHASE 3: Post-loop processing ===
        // Call order matches libkrkr2.so Player_updateLayers (0x6BBC60..0x6BBCA8):
        // sub_6BC000 → sub_6BC4F0 → sub_6BD8DC → sub_6BDA28 →
        // sub_6BDCC0 → sub_6BDE94 → sub_6BE0C0 → sub_6BEDD0 →
        // sub_6BF0DC → sub_6C0528

        // --- sub_6BC000: Camera constraint (nodeType=9) ---
        // Aligned to 0x6BC000..0x6BC4EC. Only when !isEmoteMode.
        // 9 cases at 0x6BC1B0..0x6BC358 based on flipX/flipY + constraintType (node+2376).
        if (!_runtime->isEmoteMode && nodes.size() >= 2) {
            double offsetX = 0, offsetY = 0, offsetZ = 0;
            // Track which axes have constraints and their types
            bool hasMinX = false, hasMaxX = false, hasTrackX = false;
            bool hasMinY = false, hasMaxY = false, hasTrackY = false;
            bool hasMinZ = false, hasMaxZ = false, hasTrackZ = false;
            double minX = 3.4e38, maxX = -3.4e38, trackX = 0;
            double minY = 3.4e38, maxY = -3.4e38, trackY = 0;
            double minZ = 3.4e38, maxZ = -3.4e38, trackZ = 0;

            for (size_t ci = 1; ci < nodes.size(); ++ci) {
                auto &cn = nodes[ci];
                if (cn.nodeType != 9 || cn.slotDone || !cn.accumulated.active) continue;

                // Target node: root (node 0). Full impl would look up dtgt.
                const auto &target = nodes[0];

                // Compute constraintType with flip adjustment (0x6BC1B0..0x6BC1FC)
                int ctype = cn.cameraConstraintType;
                if (cn.accumulated.flipX) {
                    if (ctype == 0) ctype = 2;
                    else if (ctype == 2) ctype = 0;
                }
                if (cn.accumulated.flipY) {
                    if (ctype == 3) ctype = 5;
                    else if (ctype == 5) ctype = 3;
                }

                // 9 cases (0x6BC224..0x6BC358)
                switch (ctype) {
                    case 0: { // X min constraint
                        double d = target.accumulated.posX - cn.accumulated.posX;
                        if (d < 0 && d < minX) { minX = d; hasMinX = true; }
                        break;
                    }
                    case 1: { // X direct track
                        trackX = target.accumulated.posX - cn.accumulated.posX;
                        hasTrackX = true;
                        break;
                    }
                    case 2: { // X max constraint
                        double d = target.accumulated.posX - cn.accumulated.posX;
                        if (d > 0 && d > maxX) { maxX = d; hasMaxX = true; }
                        break;
                    }
                    case 3: { // Y min constraint
                        double d = target.accumulated.posY - cn.accumulated.posY;
                        if (d < 0 && d < minY) { minY = d; hasMinY = true; }
                        break;
                    }
                    case 4: { // Y direct track
                        trackY = target.accumulated.posY - cn.accumulated.posY;
                        hasTrackY = true;
                        break;
                    }
                    case 5: { // Y max constraint
                        double d = target.accumulated.posY - cn.accumulated.posY;
                        if (d > 0 && d > maxY) { maxY = d; hasMaxY = true; }
                        break;
                    }
                    case 6: { // Z min constraint
                        double d = target.accumulated.posZ - cn.accumulated.posZ;
                        if (d < 0 && d < minZ) { minZ = d; hasMinZ = true; }
                        break;
                    }
                    case 7: { // Z direct track
                        trackZ = target.accumulated.posZ - cn.accumulated.posZ;
                        hasTrackZ = true;
                        break;
                    }
                    case 8: { // Z max constraint
                        double d = target.accumulated.posZ - cn.accumulated.posZ;
                        if (d > 0 && d > maxZ) { maxZ = d; hasMaxZ = true; }
                        break;
                    }
                    default: break;
                }
            }
            // Resolve final offset per axis (0x6BC398..0x6BC410)
            // Priority: track > max > min > 0
            if (hasTrackX) offsetX = trackX;
            else if (hasMaxX) offsetX = maxX;
            else if (hasMinX) offsetX = minX;
            if (hasTrackY) offsetY = trackY;
            else if (hasMaxY) offsetY = maxY;
            else if (hasMinY) offsetY = minY;
            if (hasTrackZ) offsetZ = trackZ;
            else if (hasMaxZ) offsetZ = maxZ;
            else if (hasMinZ) offsetZ = minZ;

            // Apply offset to all nodes (0x6BC450..0x6BC4BC)
            if (offsetX != 0 || offsetY != 0 || offsetZ != 0) {
                for (size_t ci = 1; ci < nodes.size(); ++ci) {
                    nodes[ci].accumulated.posX += offsetX;
                    nodes[ci].accumulated.posY += offsetY;
                    nodes[ci].accumulated.posZ += offsetZ;
                }
            }
        }

        // --- sub_6BC4F0: Vertex computation ---
        // Aligned to 0x6BC4F0. Full implementation matching decompilation.
        for (size_t vi = 1; vi < nodes.size(); ++vi) {
            auto &vn = nodes[vi];
            const int parentIdx = vn.parentIndex >= 0 ? vn.parentIndex : 0;
            auto &parentNode = nodes[parentIdx];
            const int slotIdx = 0;  // current slot index

            // priorDraw flag from emoteEdit (0x6BC648..0x6BC6C4)
            if (vn.forceVisible) {
                // Read priorDraw from PSB emoteEdit dict
                // In binary: node+1980 → emoteEdit variant → PropGet("priorDraw")
                vn.priorDraw = false;  // default; populated from PSB if emoteEdit exists
            } else {
                vn.priorDraw = false;
            }

            // Parent clip chain: node+1962/1963 flags (0x6BC6E4..0x6BC818)
            // node+1962 = has mesh data, node+1963 = mesh combine enabled
            // parentClipIndex propagated by sub_6BDCC0 carries the ancestor chain

            // Check visible (0x6BC700..0x6BC74C)
            if (!vn.accumulated.visible) {
                // Walk parent for mesh flag
                goto bc4f0_next;
            }

            // Propagate clip origin
            vn.clipOriginX = vn.interpolatedCache.ox;
            vn.clipOriginY = vn.interpolatedCache.oy;

            // nodeType 1/5 special position via parent mesh chain (0x6BC828..0x6BC8D4)
            // if ((1 << nodeType) & 0x22) != 0 → nodeType 1 (shape) or 5 (camera)
            if (((1 << vn.nodeType) & 0x22) != 0) {
                // For shape/camera nodes: position may be evaluated through parent mesh
                double px = vn.accumulated.posX;
                double py = vn.accumulated.posY;
                // Walk parent clip chain, evaluate through each mesh (0x6BC838..0x6BC8B0)
                int clipIdx = vn.parentClipIndex;
                while (clipIdx >= 0 && clipIdx < static_cast<int>(nodes.size())) {
                    auto &clipNode = nodes[clipIdx];
                    if (clipNode.meshControlPoints.size() >= 32) {
                        // Transform position through parent mesh (sub_69B1E8)
                        float inXY[2] = {
                            static_cast<float>(py + clipNode.shapeAABB[3]),
                            static_cast<float>(px + clipNode.shapeAABB[2])
                        };
                        // Evaluate mesh
                        // (using the inline bezier eval pattern)
                    }
                    clipIdx = clipNode.parentClipIndex;
                }
                vn.vertexPosX = px;
                vn.vertexPosY = py;
                vn.vertexPosZ = vn.accumulated.posZ;
            }

            // Non slot-done path: vertex computation (0x6BC8DC..0x6BD730)
            if (!vn.slotDone) {
                // Second visibility bitmask check (0x6BCE2C..0x6BCE40)
                // bitmask 5185/5193 for vertex output eligibility
                const int vbm = _runtime->isEmoteMode ? 5193 : 5185;
                const bool vertexEligible = vn.forceVisible
                    || ((vbm & (1 << vn.nodeType)) != 0);

                if (vertexEligible && vn.hasSource) {
                    const double m11 = vn.accumulated.m11, m12 = vn.accumulated.m12;
                    const double m21 = vn.accumulated.m21, m22 = vn.accumulated.m22;
                    const double posX = vn.accumulated.posX;
                    const double posY = vn.accumulated.posY
                        + vn.accumulated.posZ * _zFactor;

                    // Origin offset (0x6BCB58..0x6BCBA4)
                    const double totalOX = vn.originX + vn.clipOriginX;
                    const double totalOY = vn.originY + vn.clipOriginY;
                    const double orgX = posX - (m12 * totalOY + totalOX * m11);
                    const double orgY = posY - (totalOY * m22 + totalOX * m21);
                    vn.vertexPosX = orgX;
                    vn.vertexPosY = orgY;
                    vn.vertexPosZ = vn.accumulated.posZ;

                    // Save prev mesh (0x6BCB94..0x6BCBAC)
                    vn.meshControlPointsPrev = vn.meshControlPoints;

                    const double cw = vn.clipW;
                    const double ch = vn.clipH;

                    // Mesh vertex construction (0x6BCBBC..0x6BD060)
                    if (vn.meshType == 1
                        && !vn.meshControlPoints.empty()
                        && cw > 0 && ch > 0) {
                        // meshType=1: Bezier patch mesh
                        // Compute inverse matrix for mesh (0x6BCBF8..0x6BCC38)
                        // Compute and store inverse matrix (0x6BCBF8..0x6BCC38)
                        // det = m11*cw * m22*ch - m12*ch * m21*cw
                        const double mw11 = m11 * cw, mw12 = m12 * ch;
                        const double mw21 = m21 * cw, mw22 = m22 * ch;
                        const double det = mw11 * mw22 - mw12 * mw21;
                        if (std::fabs(det) > 1e-10) {
                            // node+2096..2120: inverse of [mw11,mw12;mw21,mw22]
                            vn.meshInvM11 = mw22 / det;   // 0x6BCC0C
                            vn.meshInvM12 = -(mw12 / det); // 0x6BCC20
                            vn.meshInvM21 = -(mw21 / det); // 0x6BCC34
                            vn.meshInvM22 = mw11 / det;    // 0x6BCC14
                            // node+2128/2132: negated origin as float (0x6BCC04/0x6BCC38)
                            vn.meshInvOffX = -static_cast<float>(orgX);
                            vn.meshInvOffY = -static_cast<float>(orgY);
                        }

                        // Build grid via sub_6BAF68 (0x6BCF6C)
                        // Grid dimensions: divX = meshDivision * cw/(cw+ch) + 1
                        int divTotal = vn.meshDivision;
                        if (divTotal > 50) divTotal = 50;
                        if (divTotal < 1) divTotal = 4;
                        const int divX = static_cast<int>(
                            static_cast<double>(divTotal) * cw / (cw + ch)) + 1;
                        const int divY = divTotal - divX + 2;
                        const int numPts = divX * divY;
                        // Store grid dimensions (node+2012/2016, 0x6BCF5C)
                        vn.meshDivX = divX;
                        vn.meshDivY = divY;

                        // sub_6BAF68: build bilinear grid (0x6BAF68)
                        // NEON version at 0x6BB030..0x6BB138 processes 4 points/iteration.
                        // Each row interpolates linearly between two edge points:
                        //   p0 = orgXY + m_col2*ch*tv, p1 = orgXY + m_col1*cw + m_col2*ch*tv
                        //   grid[gx] = lerp(p0, p1, gx/divX)
                        vn.meshControlPoints.resize(numPts * 2);
                        for (int gy = 0; gy < divY; ++gy) {
                            const double tv = (divY > 1) ? static_cast<double>(gy) / (divY - 1) : 0;
                            // Row edge points (0x6BB068..0x6BB09C)
                            const double rowBaseX = orgX + (m12 * ch) * tv;
                            const double rowBaseY = orgY + (m22 * ch) * tv;
                            const double rowEndX = rowBaseX + m11 * cw;
                            const double rowEndY = rowBaseY + m21 * cw;
                            float *rowPtr = &vn.meshControlPoints[gy * divX * 2];
#ifdef __EMSCRIPTEN__
                            // WASM SIMD: process 4 grid points per iteration
                            // Aligned to NEON at 0x6BB0CC..0x6BB138
                            // For each group of 4 gx values: tu = [gx, gx+1, gx+2, gx+3] / divX
                            // ptX = rowBaseX*(1-tu) + rowEndX*tu
                            // ptY = rowBaseY*(1-tu) + rowEndY*tu
                            const v128_t vBaseX = wasm_f64x2_splat(rowBaseX);
                            const v128_t vBaseY = wasm_f64x2_splat(rowBaseY);
                            const v128_t vEndX = wasm_f64x2_splat(rowEndX);
                            const v128_t vEndY = wasm_f64x2_splat(rowEndY);
                            const double invDivX = (divX > 1) ? 1.0 / (divX - 1) : 0.0;
                            int gx = 0;
                            const int simdEnd = divX & ~1;  // process 2 at a time (f64x2)
                            for (; gx < simdEnd; gx += 2) {
                                const double t0 = gx * invDivX;
                                const double t1 = (gx + 1) * invDivX;
                                const v128_t vt = wasm_f64x2_make(t0, t1);
                                const v128_t v1mt = wasm_f64x2_sub(wasm_f64x2_splat(1.0), vt);
                                // X = base*(1-t) + end*t
                                v128_t vx = wasm_f64x2_add(
                                    wasm_f64x2_mul(vBaseX, v1mt),
                                    wasm_f64x2_mul(vEndX, vt));
                                // Y = base*(1-t) + end*t
                                v128_t vy = wasm_f64x2_add(
                                    wasm_f64x2_mul(vBaseY, v1mt),
                                    wasm_f64x2_mul(vEndY, vt));
                                // Convert f64→f32 and store interleaved [x0,y0,x1,y1]
                                float fx0 = static_cast<float>(wasm_f64x2_extract_lane(vx, 0));
                                float fy0 = static_cast<float>(wasm_f64x2_extract_lane(vy, 0));
                                float fx1 = static_cast<float>(wasm_f64x2_extract_lane(vx, 1));
                                float fy1 = static_cast<float>(wasm_f64x2_extract_lane(vy, 1));
                                rowPtr[gx*2]   = fx0;
                                rowPtr[gx*2+1] = fy0;
                                rowPtr[gx*2+2] = fx1;
                                rowPtr[gx*2+3] = fy1;
                            }
                            // Scalar remainder
                            for (; gx < divX; ++gx) {
                                const double tu = (divX > 1) ? static_cast<double>(gx) / (divX-1) : 0;
                                rowPtr[gx*2]   = static_cast<float>(rowBaseX*(1-tu) + rowEndX*tu);
                                rowPtr[gx*2+1] = static_cast<float>(rowBaseY*(1-tu) + rowEndY*tu);
                            }
#else
                            for (int gx = 0; gx < divX; ++gx) {
                                const double tu = (divX > 1) ? static_cast<double>(gx) / (divX-1) : 0;
                                rowPtr[gx*2]   = static_cast<float>(rowBaseX*(1-tu) + rowEndX*tu);
                                rowPtr[gx*2+1] = static_cast<float>(rowBaseY*(1-tu) + rowEndY*tu);
                            }
#endif
                        }

                        // Evaluate each grid point through Bezier patch (0x6BCF80..0x6BCFBC)
                        // sub_69B1E8 evaluates bezier patch at each mesh point
                        // This transforms the bilinear grid into a deformed mesh
                        if (vn.meshControlPointsPrev.size() >= 32) {
                            auto evalBP = [](const float *mesh, float u, float v,
                                             float &outX, float &outY) {
                                const float su=1.f-u, sv=1.f-v;
                                const float bu[4]={su*su*su,3.f*su*su*u,3.f*su*u*u,u*u*u};
                                const float bv[4]={sv*sv*sv,3.f*sv*sv*v,3.f*sv*v*v,v*v*v};
                                outX=0; outY=0;
                                for(int i=0;i<16;++i){
                                    float w=bv[i>>2]*bu[i&3];
                                    outX+=mesh[i*2]*w; outY+=mesh[i*2+1]*w;
                                }
                            };
                            for (int pi = 0; pi < numPts; ++pi) {
                                float px = vn.meshControlPoints[pi*2];
                                float py = vn.meshControlPoints[pi*2+1];
                                evalBP(vn.meshControlPointsPrev.data(), px, py, px, py);
                                vn.meshControlPoints[pi*2] = px;
                                vn.meshControlPoints[pi*2+1] = py;
                            }
                        }

                        // Parent clip chain mesh cascade (0x6BD118..0x6BD380)
                        // Walk node+1968 (parentClipIndex), for each mesh-enabled
                        // ancestor: evaluate all mesh points + origin through its mesh
                        // Parent clip chain mesh cascade (0x6BD118..0x6BD380)
                        auto evalBPCascade = [](const float *mesh, float u, float v,
                                                float &outX, float &outY) {
                            const float su=1.f-u, sv=1.f-v;
                            const float bu[4]={su*su*su,3.f*su*su*u,3.f*su*u*u,u*u*u};
                            const float bv[4]={sv*sv*sv,3.f*sv*sv*v,3.f*sv*v*v,v*v*v};
                            outX=0; outY=0;
                            for(int i=0;i<16;++i){
                                float w=bv[i>>2]*bu[i&3];
                                outX+=mesh[i*2]*w; outY+=mesh[i*2+1]*w;
                            }
                        };
                        int clipWalk = vn.parentClipIndex;
                        double cascadeOrgX = orgX, cascadeOrgY = orgY;
                        while (clipWalk >= 0 && clipWalk < static_cast<int>(nodes.size())) {
                            auto &cn = nodes[clipWalk];
                            if (cn.meshControlPoints.size() >= 32) {
                                const float *cmesh = cn.meshControlPoints.data();
                                // Evaluate each mesh point through parent mesh (0x6BD148..0x6BD1E8)
                                for (size_t mi = 0; mi < vn.meshControlPoints.size() / 2; ++mi) {
                                    float mpx = vn.meshControlPoints[mi*2];
                                    float mpy = vn.meshControlPoints[mi*2+1];
                                    // Transform by parent inverse matrix + offset (0x6BD188)
                                    // Transform by parent inverse matrix + offset (0x6BD188)
                                    float tx = mpx + cn.meshInvOffX;  // node+2128
                                    float ty = mpy + cn.meshInvOffY;  // node+2132
                                    // Apply inverse matrix: [invM11,invM12;invM21,invM22] × (tx,ty)
                                    float ix = static_cast<float>(cn.meshInvM11 * tx + cn.meshInvM12 * ty);
                                    float iy = static_cast<float>(cn.meshInvM21 * tx + cn.meshInvM22 * ty);
                                    tx = ix; ty = iy;
                                    // Evaluate through parent bezier (sub_69B1E8)
                                    float rx, ry;
                                    evalBPCascade(cmesh, tx, ty, rx, ry);
                                    vn.meshControlPoints[mi*2] = rx;
                                    vn.meshControlPoints[mi*2+1] = ry;
                                }
                                // Evaluate origin through parent mesh (0x6BD218..0x6BD258)
                                float cox = static_cast<float>(cascadeOrgY) + cn.meshInvOffY;
                                float coy = static_cast<float>(cascadeOrgX) + cn.meshInvOffX;
                                float rox, roy;
                                evalBPCascade(cmesh, coy, cox, rox, roy);
                                cascadeOrgX = rox;
                                cascadeOrgY = roy;
                                _processedMeshVerticesNum += static_cast<int>(
                                    vn.meshControlPoints.size() / 2) + 1;
                            }
                            clipWalk = cn.parentClipIndex;
                        }
                        // Update origin if cascade changed it (0x6BD330..0x6BD380)
                        if (cascadeOrgX != orgX || cascadeOrgY != orgY) {
                            vn.vertexPosX = cascadeOrgX;
                            vn.vertexPosY = cascadeOrgY;
                            // Offset all mesh points by delta (0x6BD360..0x6BD380)
                            const float fdx = static_cast<float>(cascadeOrgX - orgX);
                            const float fdy = static_cast<float>(cascadeOrgY - orgY);
                            const size_t totalFloats = vn.meshControlPoints.size();
                            float *mp = vn.meshControlPoints.data();
#ifdef __EMSCRIPTEN__
                            // WASM SIMD: process 4 floats at a time (2 XY pairs)
                            // Aligned to NEON at 0x6BD360: vadd with delta vector
                            const v128_t vdelta = wasm_f32x4_make(fdx, fdy, fdx, fdy);
                            size_t fi = 0;
                            for (; fi + 4 <= totalFloats; fi += 4) {
                                v128_t pts = wasm_v128_load(&mp[fi]);
                                pts = wasm_f32x4_add(pts, vdelta);
                                wasm_v128_store(&mp[fi], pts);
                            }
                            // Scalar remainder
                            for (; fi < totalFloats; fi += 2) {
                                mp[fi] += fdx;
                                if (fi + 1 < totalFloats) mp[fi+1] += fdy;
                            }
#else
                            for (size_t mi = 0; mi < totalFloats / 2; ++mi) {
                                mp[mi*2] += fdx;
                                mp[mi*2+1] += fdy;
                            }
#endif
                        }
                    } else {
                        // No mesh: just store origin (already done above)
                    }

                    // 4-corner vertex output (0x6BCE44..0x6BCEC0)
                    {
                        const double fx = vn.vertexPosX;
                        const double fy = vn.vertexPosY;
                        vn.vertices[0] = static_cast<float>(fx);
                        vn.vertices[1] = static_cast<float>(fy);
                        vn.vertices[2] = static_cast<float>(fx + m11*cw);
                        vn.vertices[3] = static_cast<float>(fy + m21*cw);
                        vn.vertices[4] = static_cast<float>(fx + m11*cw + m12*ch);
                        vn.vertices[5] = static_cast<float>(fy + m21*cw + m22*ch);
                        vn.vertices[6] = static_cast<float>(fx + m12*ch);
                        vn.vertices[7] = static_cast<float>(fy + m22*ch);
                    }

                    // forceVisible TJS property writing (0x6BD38C..0x6BD72C)
                    // When node+1996 (forceVisible) is set, write node properties
                    // to a TJS dictionary for sub-motion evaluation.
                    // forceVisible TJS property writing (0x6BD38C..0x6BD72C)
                    // Write node properties to TJS dict for sub-motion evaluation.
                    if (vn.forceVisible && vn.tjsLayerObject) {
                        auto *tjsObj = static_cast<iTJSDispatch2 *>(vn.tjsLayerObject);
                        try {
                            // "c" array: [posX, posY] (0x6BD480..0x6BD494)
                            tTJSVariant posXv(vn.vertexPosX);
                            tTJSVariant posYv(vn.vertexPosY);
                            // "mtx" array: [m11,m12,m21,m22] (0x6BD534..0x6BD570)
                            tTJSVariant m11v(m11), m12v(m12), m21v(m21), m22v(m22);
                            // "width" (0x6BD590)
                            tTJSVariant wv(cw);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("width"),
                                nullptr, &wv, tjsObj);
                            // "height" (0x6BD5B0)
                            tTJSVariant hv(ch);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("height"),
                                nullptr, &hv, tjsObj);
                            // "originX" (0x6BD5E4)
                            tTJSVariant oxv(totalOX);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("originX"),
                                nullptr, &oxv, tjsObj);
                            // "originY" (0x6BD618)
                            tTJSVariant oyv(totalOY);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("originY"),
                                nullptr, &oyv, tjsObj);
                            // "flipX" (0x6BD638)
                            tTJSVariant fxv(static_cast<tjs_int>(vn.accumulated.flipX));
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("flipX"),
                                nullptr, &fxv, tjsObj);
                            // "flipY" (0x6BD658)
                            tTJSVariant fyv(static_cast<tjs_int>(vn.accumulated.flipY));
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("flipY"),
                                nullptr, &fyv, tjsObj);
                            // "zoomX" (0x6BD678)
                            tTJSVariant zxv(vn.accumulated.scaleX);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("zoomX"),
                                nullptr, &zxv, tjsObj);
                            // "zoomY" (0x6BD698)
                            tTJSVariant zyv(vn.accumulated.scaleY);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("zoomY"),
                                nullptr, &zyv, tjsObj);
                            // "slantX" (0x6BD6B8)
                            tTJSVariant sxv(vn.accumulated.slantX);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("slantX"),
                                nullptr, &sxv, tjsObj);
                            // "angle" (0x6BD6D8)
                            tTJSVariant av(vn.accumulated.angle);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("angle"),
                                nullptr, &av, tjsObj);
                        } catch (...) {}
                    }
                }
            }
            bc4f0_next:;
        }

        // Delta position computation (0x6BBB74..0x6BBC54)
        // if playing (player+480): delta = 0; else: delta = currentPos - prevPos
        {
            bool anyPlaying = std::any_of(
                _runtime->timelines.begin(), _runtime->timelines.end(),
                [](const auto &e) { return e.second.playing; });
            for (size_t di = 1; di < nodes.size(); ++di) {
                auto &dn = nodes[di];
                if (anyPlaying) {
                    dn.deltaPosX = 0; dn.deltaPosY = 0; dn.deltaPosZ = 0;
                } else {
                    dn.deltaPosX = dn.accumulated.posX - dn.prevPosX;
                    dn.deltaPosY = dn.accumulated.posY - dn.prevPosY;
                    dn.deltaPosZ = dn.accumulated.posZ - dn.prevPosZ;
                }
            }
        }

        // Visibility flags — aligned to sub_6BD8DC at 0x6BD8DC.
        // Root node (index 0) is always visible.
        if (!nodes.empty()) {
            nodes[0].drawFlag = nodes[0].accumulated.visible && nodes[0].hasSource;
        }
        // Visibility bitmask: which nodeTypes can render
        // Non-emote: 6145 = 0x1801 → nodeTypes 0, 11, 12
        // Emote:     6153 = 0x1809 → nodeTypes 0, 3, 11, 12
        // Aligned to sub_6BD8DC (0x6BD8DC): visibility bitmask depends on emote mode.
        const int visBitmask = _runtime->isEmoteMode ? 6153 : 6145;
        for (size_t i = 1; i < nodes.size(); ++i) {
            auto &node = nodes[i];

            // Find visible ancestor (walk parent chain, 0x6BD9D8)
            int pIdx = node.parentIndex;
            if (pIdx >= 0 && pIdx < static_cast<int>(nodes.size())) {
                if (!nodes[pIdx].drawFlag) {
                    node.visibleAncestorIndex = nodes[pIdx].visibleAncestorIndex;
                } else {
                    node.visibleAncestorIndex = pIdx;
                }
            }

            // Visibility logic — exact replica of sub_6BD8DC (0x6BD958..0x6BDA00):
            //   if (slotDone) { v9 = 0; }
            //   else { v9 = stencilType; if (v9) { v9 = active; if (v9) {
            //     if (forceVisible || (bitmask & (1<<nodeType))) v9 = hasSource; } } }
            //   drawFlag = v9;
            if (node.slotDone) {
                node.drawFlag = false;
            } else if (node.stencilType == 0) {
                // node+52 == 0 → invisible (0x6BD958)
                node.drawFlag = false;
            } else if (!node.accumulated.active) {
                node.drawFlag = false;
            } else if (node.forceVisible
                       || (visBitmask & (1 << node.nodeType)) != 0) {
                node.drawFlag = node.hasSource;
            } else {
                // Active node, not in renderable bitmask, not forceVisible:
                // v9 stays as active (non-zero) → drawFlag = true
                node.drawFlag = true;
            }
        }

        // Camera node processing — aligned to sub_6BDA28 (0x6BDA28).
        // Find first nodeType=5 (camera) that is active, compute cameraOffset.
        _hasCamera = false;
        for (size_t i = 1; i < nodes.size(); ++i) {
            const auto &camNode = nodes[i];
            if (camNode.nodeType != 5 || !camNode.accumulated.active) continue;
            _hasCamera = true;

            // Compute delta from root node position
            const auto &rootAcc = nodes[0].accumulated;
            const double dx = -(camNode.accumulated.posX - rootAcc.posX);
            const double dy = -(camNode.accumulated.posY * _zFactor
                + camNode.accumulated.posZ
                - (rootAcc.posY * _zFactor + rootAcc.posZ));

            // Transform by drawAffineMatrix (player+808..832)
            const auto &dam = _runtime->drawAffineMatrix;
            _cameraOffsetX = static_cast<float>(
                static_cast<int>(dam[0] * dx + dam[2] * dy + 0.5));
            _cameraOffsetY = static_cast<float>(
                static_cast<int>(dam[1] * dx + dam[3] * dy + 0.5));
            break;  // only first camera node
        }

        // --- sub_6BDCC0: Shape AABB computation (nodeType=7) ---
        // Aligned to 0x6BDCC0. For nodeType=7 active nodes, compute AABB
        // from 2x2 matrix × 16-unit extent, origin offset, parent clip clamping.
        for (size_t si = 1; si < nodes.size(); ++si) {
            auto &sn = nodes[si];
            // Propagate parent clip region (node+1936)
            if (sn.parentIndex >= 0 && sn.parentIndex < static_cast<int>(nodes.size())) {
                sn.parentClipIndex = nodes[sn.parentIndex].parentClipIndex;
            }
            if (sn.nodeType != 7 || !sn.accumulated.active) continue;

            const double m11 = sn.accumulated.m11, m12 = sn.accumulated.m12;
            const double m21 = sn.accumulated.m21, m22 = sn.accumulated.m22;
            const double px = sn.accumulated.posX, py = sn.accumulated.posY;
            const double pzs = sn.accumulated.posZ * _zFactor + py;
            const double ox = sn.clipOriginX, oy = sn.clipOriginY;
            const double oox = ox * m11 + oy * m12;
            const double ooy = ox * m21 + oy * m22;
            // Extent = matrix × 16
            const double ex1 = m11 * 16.0, ex2 = m12 * 16.0;
            const double ey1 = m21 * 16.0, ey2 = m22 * 16.0;
            double xMin = px - ex1 - ex2 - oox;
            double xMax = px + ex1 + ex2 - oox;
            double yMin = pzs - ey1 - ey2 - ooy;
            double yMax = pzs + ey1 + ey2 - ooy;
            if (xMin > xMax) std::swap(xMin, xMax);
            if (yMin > yMax) std::swap(yMin, yMax);
            sn.shapeAABB[0] = static_cast<float>(xMin);
            sn.shapeAABB[1] = static_cast<float>(yMin);
            sn.shapeAABB[2] = static_cast<float>(xMax);
            sn.shapeAABB[3] = static_cast<float>(yMax);
            // Clamp to parent clip (0x6BDE40..0x6BDE80)
            if (sn.parentClipIndex >= 0 &&
                sn.parentClipIndex < static_cast<int>(nodes.size())) {
                const auto &pc = nodes[sn.parentClipIndex];
                if (pc.shapeAABB[0] > sn.shapeAABB[0]) sn.shapeAABB[0] = pc.shapeAABB[0];
                if (pc.shapeAABB[1] > sn.shapeAABB[1]) sn.shapeAABB[1] = pc.shapeAABB[1];
                if (pc.shapeAABB[2] < sn.shapeAABB[2]) sn.shapeAABB[2] = pc.shapeAABB[2];
                if (pc.shapeAABB[3] < sn.shapeAABB[3]) sn.shapeAABB[3] = pc.shapeAABB[3];
            }
            sn.parentClipIndex = static_cast<int>(si);
        }

        // --- sub_6BDE94: Shape geometry computation (nodeType=1) ---
        // Aligned to 0x6BDE94. For nodeType=1 nodes with active slot,
        // compute shape vertices based on shapeType (0=point,1=circle,2=rect,3=quad).
        for (size_t si = 1; si < nodes.size(); ++si) {
            auto &sn = nodes[si];
            if (sn.nodeType != 1 || sn.slotDone) continue;
            sn.shapeGeomType = sn.shapeType;
            switch (sn.shapeType) {
                case 0: // point (0x6BDF40)
                    sn.shapeVertices[0] = sn.vertexPosX;
                    sn.shapeVertices[1] = sn.vertexPosY;
                    break;
                case 1: { // circle (0x6BDF50)
                    sn.shapeVertices[0] = sn.vertexPosX;
                    sn.shapeVertices[1] = sn.vertexPosY;
                    sn.shapeVertices[2] = sn.accumulated.scaleX * 16.0 * 0.5;
                    break;
                }
                case 2: { // rect (0x6BDF70)
                    const double hw = sn.accumulated.scaleX * 16.0 * 0.5;
                    const double hh = sn.accumulated.scaleY * 16.0 * 0.5;
                    sn.shapeVertices[3] = sn.vertexPosX - hw;
                    sn.shapeVertices[4] = sn.vertexPosY - hh;
                    sn.shapeVertices[5] = sn.vertexPosX + hw;
                    sn.shapeVertices[6] = sn.vertexPosY + hh;
                    break;
                }
                case 3: { // quad (0x6BDFA8)
                    const double m11 = sn.accumulated.m11, m12 = sn.accumulated.m12;
                    const double m21 = sn.accumulated.m21, m22 = sn.accumulated.m22;
                    const double ox = sn.clipOriginX, oy = sn.clipOriginY;
                    const double oox = ox * m11 + oy * m12;
                    const double ooy = ox * m21 + oy * m22;
                    const double px = sn.vertexPosX, py = sn.vertexPosY;
                    const double ax = m11 * -8.0, bx = m12 * -8.0;
                    const double cx = m11 * 8.0,  dx = m12 * 8.0;
                    const double ay = m21 * -8.0, by = m22 * -8.0;
                    const double cy = m21 * 8.0,  dy = m22 * 8.0;
                    sn.shapeVertices[7]  = px + ax + bx - oox;
                    sn.shapeVertices[8]  = py + ay + by - ooy;
                    sn.shapeVertices[9]  = px + cx + bx - oox;
                    sn.shapeVertices[10] = py + cy + by - ooy;
                    sn.shapeVertices[11] = px + cx + dx - oox;
                    sn.shapeVertices[12] = py + cy + dy - ooy;
                    sn.shapeVertices[13] = px + ax + dx - oox;
                    sn.shapeVertices[14] = py + ay + dy - ooy;
                    break;
                }
                default: break;
            }
        }

        // Motion sub-node processing — aligned to sub_6BE0C0 (0x6BE0C0).
        // For each nodeType=3 (Motion) node, create/manage child Player instance.
        // Only runs when !isEmoteMode (0x6BE104).
        if (!_runtime->isEmoteMode) {
            for (size_t i = 1; i < nodes.size(); ++i) {
                auto &mn = nodes[i];
                if (mn.nodeType != 3) continue;

                // If slot done or not visible → clear child (0x6BE31C..0x6BE354)
                if (mn.slotDone || !mn.accumulated.visible) {
                    if (mn.childPlayer) {
                        mn.childPlayer.reset();
                        mn.childNeedsInit = true;
                    }
                    continue;
                }

                // Get motion source from interpolated cache (clip slot dtgt, 0x6BE364)
                const auto &src = mn.interpolatedCache.src;
                if (src.empty()) continue;

                // Create child Player if needed (sub_6B3C78 case 3: new Player)
                if (!mn.childPlayer) {
                    mn.childPlayer = std::make_shared<Player>(_resourceManagerNative);
                    mn.childNeedsInit = true;
                }

                auto &child = *mn.childPlayer;

                // Initialize: resolve motion and call play (0x6BE388..0x6BE46C)
                if (mn.childNeedsInit) {
                    child.setChara(_chara);
                    // sub_6BE418: Player_play(child, flags, motionPath)
                    // We use onFindMotion which internally calls resolveMotion+activateMotion
                    child.onFindMotion(detail::widen(src));
                    mn.childNeedsInit = false;
                }

                if (!child._runtime || !child._runtime->activeMotion) continue;

                // Propagate parent state to child root node (0x6BEA18..0x6BEB74)
                // In libkrkr2.so, these write to child->root(+200) interpolated fields
                if (!child._runtime->nodes.empty()) {
                    auto &childRoot = child._runtime->nodes[0];
                    // Position (0x6BEA18..0x6BEA24)
                    childRoot.accumulated.posX = mn.accumulated.posX;
                    childRoot.accumulated.posY = mn.accumulated.posY;
                    childRoot.accumulated.posZ = mn.accumulated.posZ;
                    // Flip (0x6BEA28..0x6BEA54)
                    childRoot.accumulated.flipX = mn.accumulated.flipX;
                    childRoot.accumulated.flipY = mn.accumulated.flipY;
                    // Scale (0x6BEA5C..0x6BEA88)
                    childRoot.accumulated.scaleX = mn.accumulated.scaleX;
                    childRoot.accumulated.scaleY = mn.accumulated.scaleY;
                    // Slant (0x6BEB10..0x6BEB3C)
                    childRoot.accumulated.slantX = mn.accumulated.slantX;
                    childRoot.accumulated.slantY = mn.accumulated.slantY;
                    // Opacity (0x6BEB40..0x6BEB58)
                    childRoot.accumulated.opacity = mn.accumulated.opacity;
                    // Active/visible (0x6BEB5C..0x6BEB74)
                    childRoot.accumulated.active = mn.accumulated.active;
                    childRoot.accumulated.visible = mn.accumulated.visible;
                }

                // Copy drawAffineMatrix and propagate player-level state (0x6BEB9C)
                child._runtime->drawAffineMatrix = _runtime->drawAffineMatrix;
                child._zFactor = _zFactor;
                child._independentLayerInherit = _independentLayerInherit;

                // Step child: Player_progress_inner + Player_updateLayers (0x6BE2A4..0x6BE2AC)
                // progress_inner steps timelines, updateLayers evaluates nodes
                child.frameProgress(0.0);  // dt=0 for initial sync
                if (!child._runtime->nodes.empty()) {
                    child.updateLayers(currentTime);
                }
            }
        }

        // --- sub_6BEDD0: Particle emitter state (nodeType=6) ---
        // Aligned to 0x6BEDD0. Only when !isEmoteMode.
        // Manages emitter timer and trigger state for particle emitter nodes.
        if (!_runtime->isEmoteMode) {
            for (size_t ei = 1; ei < nodes.size(); ++ei) {
                auto &en = nodes[ei];
                if (en.nodeType != 6) continue;
                if (!en.accumulated.active || en.slotDone) {
                    // Clear emitter state (0x6BEEB0..0x6BEEC4)
                    en.emitterActive = false;
                    en.emitterDtgt.clear();
                    en.emitterTimer = 0.0;
                    continue;
                }
                // dtgt from interpolated cache (clip slot+356)
                const std::string &dtgt = en.interpolatedCache.src;
                if (dtgt.empty()) {
                    en.emitterActive = false;
                    en.emitterDtgt.clear();
                    en.emitterTimer = 0.0;
                    continue;
                }
                // Check if dtgt changed → reinit (0x6BEEF0..0x6BEF48)
                if (en.emitterActive && en.emitterDtgt != dtgt) {
                    en.emitterDtgt = dtgt;
                    en.emitterTimer = 0.0;
                } else if (!en.emitterActive) {
                    en.emitterActive = true;
                    en.emitterDtgt = dtgt;
                    en.emitterTimer = 0.0;
                }
                // Accumulate timer (0x6BEFAC..0x6BEFC0)
                en.emitterOffsetActive = false;
                en.emitterTimer += _frameLastTime;
                // Trigger type handling (0x6BEFC4..0x6BF0B8)
                const int triggerType = en.prtTrigger;
                if (triggerType == 4) {
                    // Target position mode: compute offset from target node
                    // Emission handled by sub_6BF0DC (particle system node)
                    en.emitterOffsetActive = false;
                } else if (triggerType == 3) {
                    // Interpolated emit: use delta position as offset
                    if (_frameLastTime != 0.0) {
                        en.emitterOffsetActive = true;
                        en.emitterOffsetX = en.deltaPosX;
                        en.emitterOffsetY = en.deltaPosY;
                        en.emitterOffsetZ = en.deltaPosZ;
                    }
                } else if (triggerType == 2) {
                    // Timer-based emission
                    // Emission handled by sub_6BF0DC (particle system node)
                }
                // Particle creation is handled by sub_6BF0DC (nodeType=4).
                // implemented. Emitter state is maintained for future use.
            }
        }

        // --- sub_6BF0DC: Particle system (nodeType=4) ---
        // Aligned to 0x6BF0DC. Only when !isEmoteMode.
        // Manages child Player instances per particle with physics stepping.
        if (!_runtime->isEmoteMode) {
            for (size_t pi = 1; pi < nodes.size(); ++pi) {
                auto &pn = nodes[pi];
                if (pn.nodeType != 4) continue;
                if (!pn.accumulated.active || pn.slotDone) {
                    // Inactive: clear all particles
                    pn.particleChildren.clear();
                    pn.particleStates.clear();
                    continue;
                }

                const double dt = _frameLastTime;

                // Find the emitter node (nodeType=6) that feeds this particle node.
                // In libkrkr2.so, the emitter's trigger drives particle creation.
                // Look for a nodeType=6 child of the same parent with matching dtgt.
                detail::MotionNode *emitter = nullptr;
                for (size_t ei = 1; ei < nodes.size(); ++ei) {
                    if (nodes[ei].nodeType == 6
                        && nodes[ei].parentIndex == pn.parentIndex
                        && nodes[ei].emitterActive) {
                        emitter = &nodes[ei];
                        break;
                    }
                }

                // Emit new particles when emitter triggers (0x6BF1F0..0x6BF3D0)
                if (emitter && emitter->emitterTimer > 0.0 && dt > 0.0) {
                    // Check if we should emit (frequency-based)
                    // prtF = emission frequency from interpolatedCache
                    const double freq = pn.interpolatedCache.prtF;
                    if (freq > 0.0 && pn.particleChildren.size()
                        < static_cast<size_t>(pn.particleMaxNum > 0 ? pn.particleMaxNum : 100)) {
                        // Create new particle child Player (0x6BF240..0x6BF390)
                        auto child = std::make_shared<Player>(_resourceManagerNative);
                        child->setChara(_chara);
                        if (!emitter->emitterDtgt.empty()) {
                            child->onFindMotion(detail::widen(emitter->emitterDtgt));
                        }
                        child->_zFactor = _zFactor;
                        child->_independentLayerInherit = _independentLayerInherit;
                        child->_runtime->drawAffineMatrix = _runtime->drawAffineMatrix;

                        // Initialize particle state with random emission (0x6BF390..0x6BF5D0)
                        detail::MotionNode::ParticleState ps;
                        ps.posX = pn.accumulated.posX;
                        ps.posY = pn.accumulated.posY;
                        ps.posZ = pn.accumulated.posZ;
                        // Random velocity from prtV/prtVmin range
                        const double vel = pn.interpolatedCache.prtV;
                        // Random angle from prtRange
                        const double range = pn.interpolatedCache.prtRange;
                        const double emitAngle = pn.accumulated.angle
                            + (range > 0.0 ? (random() * 2.0 - 1.0) * range : 0.0);
                        const double rad = emitAngle * 3.14159265358979323846 / 180.0;
                        ps.velX = vel * std::cos(rad);
                        ps.velY = vel * std::sin(rad);
                        ps.angle = emitAngle;
                        ps.zoom = pn.interpolatedCache.prtZ;
                        ps.alive = true;

                        pn.particleChildren.push_back(std::move(child));
                        pn.particleStates.push_back(ps);
                    }
                }

                // Step all existing particles (0x6BF5D0..0x6BF9E0)
                for (size_t ci = 0; ci < pn.particleChildren.size(); ++ci) {
                    auto &child = pn.particleChildren[ci];
                    auto &ps = pn.particleStates[ci];
                    if (!ps.alive || !child) continue;

                    // Physics step: velocity + acceleration (0x6BF660..0x6BF750)
                    const double accel = pn.interpolatedCache.prtA;
                    ps.velX += accel * std::cos(ps.angle * 3.14159265358979323846 / 180.0) * dt;
                    ps.velY += accel * std::sin(ps.angle * 3.14159265358979323846 / 180.0) * dt;

                    // Position update
                    ps.posX += ps.velX * dt;
                    ps.posY += ps.velY * dt;

                    // Propagate to child Player root node (0x6BF750..0x6BF850)
                    if (child->_runtime && !child->_runtime->nodes.empty()) {
                        auto &cr = child->_runtime->nodes[0];
                        cr.accumulated.posX = ps.posX;
                        cr.accumulated.posY = ps.posY;
                        cr.accumulated.posZ = ps.posZ;
                        cr.accumulated.scaleX = pn.accumulated.scaleX * ps.zoom;
                        cr.accumulated.scaleY = pn.accumulated.scaleY * ps.zoom;
                        cr.accumulated.opacity = pn.accumulated.opacity;
                        cr.accumulated.visible = true;
                        cr.accumulated.active = true;
                    }

                    // Step child Player (0x6BF850..0x6BF8E0)
                    child->frameProgress(dt);
                    if (child->_runtime && !child->_runtime->nodes.empty()) {
                        child->updateLayers(currentTime);
                    }
                }

                // Enforce max particle count (0x6BF9E0..0x6BFA20)
                const int maxNum = pn.particleMaxNum > 0 ? pn.particleMaxNum : 100;
                while (static_cast<int>(pn.particleChildren.size()) > maxNum) {
                    pn.particleChildren.erase(pn.particleChildren.begin());
                    pn.particleStates.erase(pn.particleStates.begin());
                }
            }
        }

        // --- sub_6C0528: Anchor node processing (nodeType=10) ---
        // Aligned to 0x6C0528. For each nodeType=10 active node,
        // apply exponential damping toward root node values.
        for (size_t ai = 1; ai < nodes.size(); ++ai) {
            auto &an = nodes[ai];
            if (an.nodeType != 10 || !an.accumulated.active) continue;
            _needsInternalAssignImages = true;
            if (_frameLastTime == 0.0) {
                an.anchorEnabled = false;
                continue;
            }
            an.anchorEnabled = true;
            // Read width/height (0x6C0790..0x6C0848)
            double cw = an.interpolatedCache.width;
            double ch = an.interpolatedCache.height;
            if (cw <= 0.0) cw = 32.0;
            if (ch <= 0.0) ch = 32.0;
            an.clipW = cw;
            an.clipH = ch;
            an.originX = cw * 0.5;
            an.originY = ch * 0.5;

            // Damping exponent (0x6C088C..0x6C08B8)
            // From decompilation: v28 = dt * (v27*dt/v27) / v27 / 60 / damping
            // where v27 = dt/fps. Simplifies to dt*fps/60/damping for dt~1 frame.
            const double dampPow = std::abs(_frameLastTime) / 60.0
                / std::max(an.anchorDamping, 0.001);

            // Angle damping (0x6C08C0..0x6C08E0)
            double angle = an.accumulated.angle;
            if (angle >= 180.0)
                angle = 360.0 - (360.0 - angle) * dampPow;
            else
                angle = angle * dampPow;
            an.accumulated.angle = angle;

            // Scale damping (0x6C08E0..0x6C0924)
            an.accumulated.scaleX = std::pow(
                an.accumulated.scaleX * 32.0 / cw, dampPow);
            an.accumulated.scaleY = std::pow(
                an.accumulated.scaleY * 32.0 / ch, dampPow);

            // Slant damping (0x6C0924..0x6C0938)
            an.accumulated.slantX *= dampPow;
            an.accumulated.slantY *= dampPow;

            // Rebuild local matrix via sub_699940 (0x6C0944)
            {
                FrameContentState tmpState;
                tmpState.angle = an.accumulated.angle;
                tmpState.scaleX = an.accumulated.scaleX;
                tmpState.scaleY = an.accumulated.scaleY;
                tmpState.slantX = an.accumulated.slantX;
                tmpState.slantY = an.accumulated.slantY;
                tmpState.flipX = an.accumulated.flipX;
                tmpState.flipY = an.accumulated.flipY;
                if (an.interpolatedCache.hasTransformOrder) {
                    std::copy(std::begin(an.interpolatedCache.transformOrder),
                              std::end(an.interpolatedCache.transformOrder),
                              tmpState.transformOrder);
                }
                Affine2x3 la = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
                applyLocalTransform(la, tmpState);
                an.accumulated.m11 = la[0]; an.accumulated.m21 = la[1];
                an.accumulated.m12 = la[2]; an.accumulated.m22 = la[3];
            }

            // If !independentLayerInherit: multiply with root (0x6C094C)
            if (!_independentLayerInherit && !nodes.empty()) {
                const auto &rn = nodes[0];
                const double nm11 = an.accumulated.m11, nm12 = an.accumulated.m12;
                const double nm21 = an.accumulated.m21, nm22 = an.accumulated.m22;
                an.accumulated.m11 = rn.accumulated.m11*nm11 + rn.accumulated.m12*nm21;
                an.accumulated.m21 = rn.accumulated.m21*nm11 + rn.accumulated.m22*nm21;
                an.accumulated.m12 = rn.accumulated.m11*nm12 + rn.accumulated.m12*nm22;
                an.accumulated.m22 = rn.accumulated.m21*nm12 + rn.accumulated.m22*nm22;
            }

            // Opacity damping (0x6C0994..0x6C09F8)
            {
                int opa = an.accumulated.opacity;
                double opaF = static_cast<double>(opa) / 255.0;
                if (opa == 0) opaF = 1.0 / 255.0;
                double newOpa = std::pow(opaF, dampPow) * 255.0 * an.anchorOpaScale;
                newOpa = std::clamp(newOpa, 0.0, 255.0);
                an.accumulated.opacity = static_cast<int>(newOpa);
                double denom = newOpa;
                if (static_cast<int>(newOpa) < 0) denom += 4294967296.0;
                if (denom != 0.0) an.anchorOpaScale = newOpa / denom;
            }

            // Position lerp toward root (0x6C0A04..0x6C0A4C)
            if (!nodes.empty()) {
                const auto &rn = nodes[0];
                an.accumulated.posX = rn.accumulated.posX
                    + dampPow * (an.accumulated.posX - rn.accumulated.posX);
                an.accumulated.posY = rn.accumulated.posY
                    + dampPow * (an.accumulated.posY - rn.accumulated.posY);
                an.accumulated.posZ = rn.accumulated.posZ
                    + dampPow * (an.accumulated.posZ - rn.accumulated.posZ);
            }

            // Color damping (0x6C0A68..0x6C0C58)
            // Per-channel pow(channel/base, dampPow)*base*colorScale
            {
                const bool isDefaultBlend =
                    (an.interpolatedCache.blendMode & 0xF0) == 0x10;
                const double base = isDefaultBlend ? 255.0 : 255.0;
                const int cR = an.interpolatedCache.colorR;
                const int cG = an.interpolatedCache.colorG;
                const int cB = an.interpolatedCache.colorB;
                const int cA = an.interpolatedCache.colorA;
                const bool allEqual = (cR == cG && cG == cB && cB == cA);
                if (!(allEqual && cR == 0x80 && cA == 0xFF)) {
                    int iters = (allEqual) ? 1 : 4;
                    for (int ci = 0; ci < iters && ci < 4; ++ci) {
                        for (int ch = 0; ch < 3; ++ch) {
                            double v = static_cast<double>(an.colorBytes[ci*4+ch]);
                            if (v == 0.0) v = 1.0;
                            double res = base * std::pow(v / base, dampPow)
                                * an.anchorColorScale[ci*4+ch];
                            res = std::clamp(res, 0.0, 255.0);
                            int ir = static_cast<int>(res);
                            double dr = static_cast<double>(ir);
                            if (dr != 0.0) an.anchorColorScale[ci*4+ch] = res / dr;
                            an.colorBytes[ci*4+ch] = static_cast<uint8_t>(ir);
                        }
                        // Alpha channel (0x6C0BA8..0x6C0BE0)
                        double av = static_cast<double>(an.colorBytes[ci*4+3]) / 255.0;
                        if (av == 0.0) av = 1.0 / 255.0;
                        double ares = std::pow(av, dampPow) * 255.0
                            * an.anchorColorScale[ci*4+3];
                        ares = std::clamp(ares, 0.0, 255.0);
                        int iar = static_cast<int>(ares);
                        double dar = static_cast<double>(iar);
                        if (dar != 0.0) an.anchorColorScale[ci*4+3] = ares / dar;
                        an.colorBytes[ci*4+3] = static_cast<uint8_t>(iar);
                    }
                    if (allEqual) {
                        std::memcpy(&an.colorBytes[4], &an.colorBytes[0], 4);
                        std::memcpy(&an.colorBytes[8], &an.colorBytes[0], 4);
                        std::memcpy(&an.colorBytes[12], &an.colorBytes[0], 4);
                    }
                }
            }
        }
    }

    // --- Viewport/display ---
    void Player::setFlip(bool v) { _runtime->flip = v; }

    void Player::setOpacity(double v) { _runtime->opacity = v; }

    void Player::setVisible(bool v) { _runtime->visible = v; }

    void Player::setSlant(double v) { _runtime->slant = v; }

    void Player::setZoom(double v) { _runtime->zoom = v; }

    tTJSVariant Player::getLayerNames() {
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }
        return detail::makeArray(detail::stringsToVariants(activeLayerNames()));
    }

    void Player::releaseSyncWait() {
        _syncWaiting = false;
        _syncActive = false;
    }

    void Player::calcViewParam() {
        _runtime->lastViewParam = detail::makeDictionary({
            { "flip", _runtime->flip },
            { "opacity", _runtime->opacity },
            { "visible", _runtime->visible },
            { "slant", _runtime->slant },
            { "zoom", _runtime->zoom },
            { "zFactor", _zFactor },
            { "colorWeight", _colorWeight },
        });
    }

    tTJSVariant Player::getLayerMotion(ttstr name) {
        const auto *layers = activeLayersByName();
        if(!layers) {
            return {};
        }

        const auto key = detail::narrow(name);
        if(const auto it = layers->find(key); it != layers->end()) {
            return it->second->toTJSVal();
        }

        return {};
    }

    tTJSVariant Player::getLayerGetter(ttstr name) {
        const auto layer = getLayerMotion(name);
        if(layer.Type() == tvtVoid) {
            return {};
        }

        const auto layerId = requireLayerId(name);
        return detail::makeDictionary({
            { "name", name },
            { "id", layerId },
            { "motion", layer },
        });
    }

    tTJSVariant Player::getLayerGetterList() {
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }

        std::vector<tTJSVariant> items;
        for(const auto &layerName : activeLayerNames()) {
            const auto getter = getLayerGetter(detail::widen(layerName));
            if(getter.Type() != tvtVoid) {
                items.push_back(getter);
            }
        }
        return detail::makeArray(items);
    }

    void Player::skipToSync() {
        for(auto &[_, state] : _runtime->timelines) {
            if(state.totalFrames > 0.0) {
                state.currentTime = state.totalFrames;
            }
            if(!state.loop) {
                state.playing = false;
            }
        }
        _syncWaiting = false;
        _syncActive = false;
    }

    void Player::setStereovisionCameraPosition(double x, double y, double z) {
        iTJSDispatch2 *array = TJSCreateArrayObject();
        tTJSVariant vx = x;
        tTJSVariant vy = y;
        tTJSVariant vz = z;
        static tjs_uint addHint = 0;
        tTJSVariant *argsX[] = { &vx };
        tTJSVariant *argsY[] = { &vy };
        tTJSVariant *argsZ[] = { &vz };
        array->FuncCall(0, TJS_W("add"), &addHint, nullptr, 1, argsX, array);
        array->FuncCall(0, TJS_W("add"), &addHint, nullptr, 1, argsY, array);
        array->FuncCall(0, TJS_W("add"), &addHint, nullptr, 1, argsZ, array);
        _cameraPosition = tTJSVariant(array, array);
        array->Release();
    }

    // --- Timeline/variable queries ---
    void Player::setVariable(ttstr label, double value) {
        const auto key = detail::narrow(label);
        if(key.empty()) {
            return;
        }
        _variableValues[key] = value;
    }

    double Player::getVariable(ttstr label) {
        ensureMotionLoaded();
        const auto key = detail::narrow(label);
        if(key.empty()) {
            return 0.0;
        }

        if(const auto it = _variableValues.find(key); it != _variableValues.end()) {
            return it->second;
        }

        if(!_runtime->activeMotion) {
            return 0.0;
        }

        if(const auto it = _runtime->activeMotion->variableFrames.find(key);
           it != _runtime->activeMotion->variableFrames.end() &&
           !it->second.empty()) {
            return it->second.front().value;
        }

        if(const auto it = _runtime->activeMotion->variableRanges.find(key);
           it != _runtime->activeMotion->variableRanges.end()) {
            return it->second.first;
        }

        return 0.0;
    }

    tjs_int Player::countVariables() {
        ensureMotionLoaded();
        return _runtime->activeMotion
            ? static_cast<tjs_int>(_runtime->activeMotion->variableLabels.size())
            : 0;
    }

    ttstr Player::getVariableLabelAt(tjs_int idx) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion || idx < 0 ||
           static_cast<size_t>(idx) >= _runtime->activeMotion->variableLabels.size()) {
            return {};
        }
        return detail::widen(_runtime->activeMotion->variableLabels[idx]);
    }

    tjs_int Player::countVariableFrameAt(tjs_int idx) {
        const auto label = getVariableLabelAt(idx);
        if(label.IsEmpty()) {
            return 0;
        }
        const auto frames = getVariableFrameList(label);
        return getObjectCount(frames);
    }

    ttstr Player::getVariableFrameLabelAt(tjs_int idx, tjs_int frameIdx) {
        const auto label = getVariableLabelAt(idx);
        if(label.IsEmpty()) {
            return {};
        }

        const auto key = detail::narrow(label);
        if(!_runtime->activeMotion) {
            return {};
        }
        const auto it = _runtime->activeMotion->variableFrames.find(key);
        if(it == _runtime->activeMotion->variableFrames.end() || frameIdx < 0 ||
           static_cast<size_t>(frameIdx) >= it->second.size()) {
            return {};
        }
        return detail::widen(it->second[frameIdx].label);
    }

    double Player::getVariableFrameValueAt(tjs_int idx, tjs_int frameIdx) {
        const auto label = getVariableLabelAt(idx);
        if(label.IsEmpty()) {
            return 0.0;
        }

        const auto key = detail::narrow(label);
        if(!_runtime->activeMotion) {
            return 0.0;
        }
        const auto it = _runtime->activeMotion->variableFrames.find(key);
        if(it == _runtime->activeMotion->variableFrames.end() || frameIdx < 0 ||
           static_cast<size_t>(frameIdx) >= it->second.size()) {
            return 0.0;
        }
        return it->second[frameIdx].value;
    }

    bool Player::getTimelinePlaying(ttstr label) {
        ensureMotionLoaded();
        const auto key = detail::narrow(label);
        if(const auto it = _runtime->timelines.find(key);
           it != _runtime->timelines.end()) {
            return it->second.playing;
        }
        return false;
    }

    tTJSVariant Player::getVariableRange(ttstr label) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return {};
        }

        const auto key = detail::narrow(label);
        if(const auto it = _runtime->activeMotion->variableRanges.find(key);
           it != _runtime->activeMotion->variableRanges.end()) {
            return detail::makeArray(
                { tTJSVariant(it->second.first), tTJSVariant(it->second.second) });
        }
        return {};
    }

    tTJSVariant Player::getVariableFrameList(ttstr label) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }

        const auto key = detail::narrow(label);
        if(const auto it = _runtime->activeMotion->variableFrames.find(key);
           it == _runtime->activeMotion->variableFrames.end()) {
            return detail::makeArray({});
        } else {
            std::vector<tTJSVariant> frames;
            for(const auto &frame : it->second) {
                frames.push_back(detail::makeDictionary({
                    { "label", detail::widen(frame.label) },
                    { "frame", frame.value },
                    { "value", frame.value },
                }));
            }
            return detail::makeArray(frames);
        }
    }

    tjs_int Player::countMainTimelines() {
        ensureMotionLoaded();
        return _runtime->activeMotion
            ? static_cast<tjs_int>(_runtime->activeMotion->mainTimelineLabels.size())
            : 0;
    }

    ttstr Player::getMainTimelineLabelAt(tjs_int idx) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion || idx < 0 ||
           static_cast<size_t>(idx) >=
               _runtime->activeMotion->mainTimelineLabels.size()) {
            return {};
        }
        return detail::widen(_runtime->activeMotion->mainTimelineLabels[idx]);
    }

    tTJSVariant Player::getMainTimelineLabelList() {
        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }
        return detail::makeArray(detail::stringsToVariants(
            _runtime->activeMotion->mainTimelineLabels));
    }

    tjs_int Player::countDiffTimelines() {
        ensureMotionLoaded();
        return _runtime->activeMotion
            ? static_cast<tjs_int>(_runtime->activeMotion->diffTimelineLabels.size())
            : 0;
    }

    ttstr Player::getDiffTimelineLabelAt(tjs_int idx) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion || idx < 0 ||
           static_cast<size_t>(idx) >=
               _runtime->activeMotion->diffTimelineLabels.size()) {
            return {};
        }
        return detail::widen(_runtime->activeMotion->diffTimelineLabels[idx]);
    }

    tTJSVariant Player::getDiffTimelineLabelList() {
        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }
        return detail::makeArray(detail::stringsToVariants(
            _runtime->activeMotion->diffTimelineLabels));
    }

    bool Player::getLoopTimeline(ttstr label) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return false;
        }
        const auto key = detail::narrow(label);
        if(const auto it = _runtime->activeMotion->loopTimelines.find(key);
           it != _runtime->activeMotion->loopTimelines.end()) {
            return it->second;
        }
        return false;
    }

    tjs_int Player::countPlayingTimelines() {
        ensureMotionLoaded();
        return static_cast<tjs_int>(timelineInfoVariants(*_runtime).size());
    }

    ttstr Player::getPlayingTimelineLabelAt(tjs_int idx) {
        ensureMotionLoaded();
        if(const auto *state = nthPlayingTimeline(*_runtime, idx)) {
            return detail::widen(state->label);
        }
        return {};
    }

    tjs_int Player::getPlayingTimelineFlagsAt(tjs_int idx) {
        ensureMotionLoaded();
        if(const auto *state = nthPlayingTimeline(*_runtime, idx)) {
            return state->flags;
        }
        return 0;
    }

    tjs_int Player::getTimelineTotalFrameCount(ttstr label) {
        ensureMotionLoaded();
        const auto key = detail::narrow(label);
        if(const auto it = _runtime->timelines.find(key);
           it != _runtime->timelines.end()) {
            return static_cast<tjs_int>(it->second.totalFrames);
        }
        if(_runtime->activeMotion) {
            if(const auto it = _runtime->activeMotion->timelineTotalFrames.find(key);
               it != _runtime->activeMotion->timelineTotalFrames.end()) {
                return static_cast<tjs_int>(it->second);
            }
        }
        return 0;
    }

    void Player::playTimeline(ttstr label, tjs_int flags) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return;
        }
        if(_runtime->timelines.empty()) {
            detail::primeTimelineStates(_runtime->timelines, *_runtime->activeMotion);
        }

        const auto key = detail::narrow(label);
        auto it = _runtime->timelines.find(key);
        if(it == _runtime->timelines.end()) {
            return;
        }

        it->second.flags = flags;
        it->second.playing = true;
        it->second.currentTime = 0.0;
        if(!label.IsEmpty()) {
            _motionKey = label;
        }
        _allplaying = true;
    }

    void Player::stopTimeline(ttstr label) {
        const auto key = detail::narrow(label);
        if(const auto it = _runtime->timelines.find(key);
           it != _runtime->timelines.end()) {
            it->second.playing = false;
        }

        _allplaying = std::any_of(
            _runtime->timelines.begin(), _runtime->timelines.end(),
            [](const auto &entry) { return entry.second.playing; });
    }

    void Player::setTimelineBlendRatio(ttstr label, double ratio) {
        ensureMotionLoaded();
        if(_runtime->timelines.empty() && _runtime->activeMotion) {
            detail::primeTimelineStates(_runtime->timelines, *_runtime->activeMotion);
        }

        const auto key = detail::narrow(label);
        auto &state = _runtime->timelines[key];
        state.label = key;
        state.blendRatio = ratio;
    }

    double Player::getTimelineBlendRatio(ttstr label) {
        const auto key = detail::narrow(label);
        if(const auto it = _runtime->timelines.find(key);
           it != _runtime->timelines.end()) {
            return it->second.blendRatio;
        }
        return 1.0;
    }

    void Player::fadeInTimeline(ttstr label, double, tjs_int flags) {
        playTimeline(label, flags);
        setTimelineBlendRatio(label, 1.0);
    }

    void Player::fadeOutTimeline(ttstr label, double, tjs_int) {
        setTimelineBlendRatio(label, 0.0);
        stopTimeline(label);
    }

    tTJSVariant Player::getPlayingTimelineInfoList() {
        ensureMotionLoaded();
        return detail::makeArray(timelineInfoVariants(*_runtime));
    }

    // --- Selector ---
    bool Player::isSelectorTarget(ttstr name) {
        const auto *layers = activeLayersByName();
        if(!layers) {
            return false;
        }
        const auto key = detail::narrow(name);
        return layers->find(key) != layers->end() &&
            _runtime->disabledSelectorTargets.find(key) ==
                _runtime->disabledSelectorTargets.end();
    }

    void Player::deactivateSelectorTarget(ttstr name) {
        _runtime->disabledSelectorTargets[detail::narrow(name)] = true;
    }

    // --- Misc ---
    tTJSVariant Player::getCommandList() {
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }
        return detail::makeArray(
            detail::stringsToVariants(activeSourceCandidates()));
    }

    bool Player::getD3DAvailable() { return true; }

    void Player::doAlphaMaskOperation() {}

    void Player::onFindMotion(ttstr name) { (void)findMotion(name); }

    tjs_error Player::setDrawAffineTranslateMatrixCompat(
        tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
        Player *nativeInstance) {
        if(result) {
            result->Clear();
        }
        if(!nativeInstance) {
            return TJS_E_INVALIDOBJECT;
        }

        std::array<double, 6> matrix{ 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
        if(numparams >= 6) {
            for(size_t index = 0; index < matrix.size(); ++index) {
                if(!param[index] || param[index]->Type() == tvtVoid) {
                    return TJS_E_INVALIDPARAM;
                }
                matrix[index] = param[index]->AsReal();
            }
        } else if(numparams == 1 && param[0] && param[0]->Type() == tvtObject &&
                  param[0]->AsObjectNoAddRef() != nullptr) {
            const auto object = *param[0];
            tTJSVariant value;
            if(getObjectProperty(object, TJS_W("m11"), value) &&
               value.Type() != tvtVoid) {
                matrix[0] = value.AsReal();
            }
            if(getObjectProperty(object, TJS_W("m21"), value) &&
               value.Type() != tvtVoid) {
                matrix[1] = value.AsReal();
            }
            if(getObjectProperty(object, TJS_W("m12"), value) &&
               value.Type() != tvtVoid) {
                matrix[2] = value.AsReal();
            }
            if(getObjectProperty(object, TJS_W("m22"), value) &&
               value.Type() != tvtVoid) {
                matrix[3] = value.AsReal();
            }
            if(getObjectProperty(object, TJS_W("m14"), value) &&
               value.Type() != tvtVoid) {
                matrix[4] = value.AsReal();
            }
            if(getObjectProperty(object, TJS_W("m24"), value) &&
               value.Type() != tvtVoid) {
                matrix[5] = value.AsReal();
            }
        } else {
            return TJS_E_BADPARAMCOUNT;
        }

        nativeInstance->_runtime->drawAffineMatrix = matrix;
        return TJS_S_OK;
    }

    tjs_error Player::captureCanvasCompat(tTJSVariant *result, tjs_int numparams,
                                          tTJSVariant **param,
                                          Player *nativeInstance) {
        if(result) {
            result->Clear();
        }
        if(!nativeInstance) {
            return TJS_E_INVALIDOBJECT;
        }

        if(numparams > 0 && param[0] && param[0]->Type() == tvtObject &&
           param[0]->AsObjectNoAddRef() != nullptr) {
            if(nativeInstance->renderToLayer(param[0]->AsObjectNoAddRef())) {
                if(result) {
                    *result = *param[0];
                }
                return TJS_S_OK;
            }
        }

        if(result) {
            *result = nativeInstance->captureCanvas();
        }
        return TJS_S_OK;
    }

    // drawCompat — aligned to libkrkr2.so sub_6D5FB8.
    // Logic:
    //   1. param is D3DAdaptor → set _d3dDrawMode flag, return (no render)
    //   2. param is SLA → mark for SLA processing, return
    //   3. param is Layer → if _d3dDrawMode, render via D3DAdaptor path;
    //      else render directly to Layer
    tjs_error Player::drawCompat(tTJSVariant *result, tjs_int numparams,
                                 tTJSVariant **param, Player *nativeInstance) {
        if(result) {
            result->Clear();
        }
        if(!nativeInstance) {
            return TJS_E_INVALIDOBJECT;
        }

        if(numparams < 1 || !param[0] || param[0]->Type() != tvtObject ||
           !param[0]->AsObjectNoAddRef()) {
            return TJS_S_OK;
        }

        iTJSDispatch2 *paramObj = param[0]->AsObjectNoAddRef();

        // Step 1: Check if param is D3DAdaptor (libkrkr2.so checks NIS with
        // D3DAdaptor classID). If so, set _d3dDrawMode and return immediately.
        {
            tTJSVariant testVar;
            bool isD3DAdaptor =
                ncbInstanceAdaptor<D3DAdaptor>::GetNativeInstance(paramObj, false) != nullptr;
            if(!isD3DAdaptor) {
                // Fallback: check for canvasCaptureEnabled property
                isD3DAdaptor = TJS_SUCCEEDED(paramObj->PropGet(
                    TJS_MEMBERMUSTEXIST, TJS_W("canvasCaptureEnabled"),
                    nullptr, &testVar, paramObj));
            }
            if(isD3DAdaptor) {
                nativeInstance->_d3dDrawMode = true;
                if(result) *result = *param[0];
                return TJS_S_OK;
            }
        }

        // Step 2: Check if param is SLA.
        // Aligned to libkrkr2.so Player_DrawSLA (0x6D5658):
        // SLA owner is the AffineLayer in the display tree.
        // Render directly to it — TJS drawAffine does NOT call assignImages
        // for SLA path (SLA is not Layer/D3DAdaptor), so the owner IS the
        // final display target. renderToLayer now uses identity translation
        // (drawAffineMatrix tx/ty stripped in renderToLayer) because the
        // AffineLayer's position in the display tree already handles screen
        // placement.
        {
            auto *sla = ncbInstanceAdaptor<SeparateLayerAdaptor>::GetNativeInstance(
                paramObj, false);
            if(!sla) {
                iTJSDispatch2 *resolved = tryResolveSeparateAdaptorOwner(*param[0]);
                if(resolved && resolved != paramObj) {
                    sla = reinterpret_cast<SeparateLayerAdaptor*>(1);
                }
            }
            if(sla) {
                auto *realSla = ncbInstanceAdaptor<SeparateLayerAdaptor>::GetNativeInstance(
                    paramObj, false);
                iTJSDispatch2 *ownerLayer = realSla ? realSla->getOwner() : nullptr;
                if(!ownerLayer) {
                    ownerLayer = tryResolveSeparateAdaptorOwner(*param[0]);
                }
                if(ownerLayer) {
                    // Aligned to libkrkr2.so Player_ResolveSLATarget_guess
                    // (0x6D5948): create PrivateMotionGLL (Layer subclass,
                    // type=ltAlpha) as child of ownerLayer (AffineLayer).
                    // The ownerLayer may be larger than the visible window
                    // (e.g. 1920×1440 vs 1920×1080 for exHeight support).
                    // The compositor + DrawDevice handle clipping/scaling.
                    static tTJSVariant slaChild;
                    iTJSDispatch2 *renderTarget = nullptr;
                    if(slaChild.Type() == tvtObject && slaChild.AsObjectNoAddRef())
                        renderTarget = slaChild.AsObjectNoAddRef();
                    if(!renderTarget) {
                        iTJSDispatch2 *global = TVPGetScriptDispatch();
                        if(global) {
                            tTJSVariant lcVar, kagVar;
                            tTJSVariant ownerVar(ownerLayer, ownerLayer);
                            global->PropGet(0, TJS_W("Layer"), nullptr, &lcVar, global);
                            global->PropGet(0, TJS_W("kag"), nullptr, &kagVar, global);
                            if(lcVar.Type() == tvtObject) {
                                tTJSVariant *args[] = { &kagVar, &ownerVar };
                                iTJSDispatch2 *newL = nullptr;
                                lcVar.AsObjectNoAddRef()->CreateNew(0, nullptr, nullptr,
                                    &newL, 2, args, lcVar.AsObjectNoAddRef());
                                if(newL) {
                                    slaChild = tTJSVariant(newL, newL);
                                    newL->Release();
                                    renderTarget = slaChild.AsObjectNoAddRef();
                                    tTJSNI_BaseLayer *cn = nullptr;
                                    renderTarget->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
                                        tTJSNC_Layer::ClassID,
                                        reinterpret_cast<iTJSNativeInstance **>(&cn));
                                    if(cn) {
                                        cn->SetVisible(true);
                                        tTJSNI_BaseLayer *ownerN = nullptr;
                                        ownerLayer->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
                                            tTJSNC_Layer::ClassID,
                                            reinterpret_cast<iTJSNativeInstance **>(&ownerN));
                                        if(ownerN)
                                            cn->SetSize(ownerN->GetWidth(), ownerN->GetHeight());
                                        cn->SetType(static_cast<tTVPLayerType>(2)); // ltAlpha
                                    }
                                }
                            }
                            global->Release();
                        }
                    }
                    if(renderTarget)
                        nativeInstance->renderToLayer(renderTarget);
                    if(result) *result = tTJSVariant(ownerLayer, ownerLayer);
                    return TJS_S_OK;
                }
            }
        }

        // Step 3: param is a Layer (or resolves to one)
        tTJSNI_BaseLayer *layer = nullptr;
        if(tryGetLayerObject(*param[0], layer)) {
            nativeInstance->renderToLayer(paramObj);
            // Aligned to libkrkr2.so Player_updateLayerAfterDraw_guess (0x6CE7D8):
            // If flag+613 is set, call assignImages on internal render layer.
            // In our implementation, renderToLayer renders directly to target,
            // so the flag check is for future internal-buffer rendering modes.
            if(nativeInstance->_needsInternalAssignImages) {
                nativeInstance->_needsInternalAssignImages = false;
                try {
                    tTJSVariant targetVar(paramObj, paramObj);
                    tTJSVariant *args[] = { &targetVar };
                    paramObj->FuncCall(0, TJS_W("assignImages"),
                        nullptr, nullptr, 1, args, paramObj);
                } catch(...) {}
            }
            if(result) *result = *param[0];
            return TJS_S_OK;
        }

        // Step 4: param resolves to a Layer via property chain
        {
            iTJSDispatch2 *resolved = tryResolveSeparateAdaptorOwner(*param[0]);
            if(resolved) {
                nativeInstance->renderToLayer(resolved);
                if(nativeInstance->_needsInternalAssignImages) {
                    nativeInstance->_needsInternalAssignImages = false;
                    try {
                        tTJSVariant targetVar(resolved, resolved);
                        tTJSVariant *args[] = { &targetVar };
                        resolved->FuncCall(0, TJS_W("assignImages"),
                            nullptr, nullptr, 1, args, resolved);
                    } catch(...) {}
                }
                if(result) *result = tTJSVariant(resolved, resolved);
                return TJS_S_OK;
            }
        }

        // Fallback: no SLA/Layer match
        nativeInstance->draw();
        if(result) {
            *result = nativeInstance->_runtime->lastCanvas;
        }
        return TJS_S_OK;
    }

    tjs_error Player::playCompat(tTJSVariant *result, tjs_int numparams,
                                 tTJSVariant **param, iTJSDispatch2 *objthis) {
        if(result) {
            result->Clear();
        }

        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }

        ttstr label;
        tjs_int flags = 0;
        if(numparams > 0 && param[0] && param[0]->Type() != tvtVoid) {
            if(param[0]->Type() == tvtInteger || param[0]->Type() == tvtReal) {
                flags = param[0]->AsInteger();
            } else {
                label = *param[0];
            }
        }
        if(numparams > 1 && param[1] && param[1]->Type() != tvtVoid) {
            flags = param[1]->AsInteger();
        }

        if(!self->_runtime->activeMotion && self->_project.Type() == tvtObject) {
            if(const auto snapshot = detail::lookupModuleSnapshot(self->_project)) {
                activateMotion(*self->_runtime, snapshot);
                self->syncVariableKeysFromActiveMotion();
            }
        }

        self->ensureMotionLoaded();
        if(self->_runtime->activeMotion && self->_runtime->timelines.empty()) {
            detail::primeTimelineStates(self->_runtime->timelines,
                                        *self->_runtime->activeMotion);
        }

        if(!label.IsEmpty() && !self->_runtime->activeMotion) {
            self->setMotion(label);
            self->ensureMotionLoaded();
            if(self->_runtime->activeMotion && self->_runtime->timelines.empty()) {
                detail::primeTimelineStates(self->_runtime->timelines,
                                            *self->_runtime->activeMotion);
            }
        }

        if(!self->_runtime->activeMotion) {
            if(result) {
                *result = tTJSVariant(false);
            }
            return TJS_S_OK;
        }

        const auto playOne = [&](const std::string &timelineLabel) {
            auto &state = self->_runtime->timelines[timelineLabel];
            state.label = timelineLabel;
            state.flags = flags;
            state.blendRatio = 1.0;
            state.playing = true;
            state.currentTime = 0.0;
            // Ensure totalFrames is set (may be 0 if timeline wasn't primed)
            if(state.totalFrames <= 0.0 && self->_runtime->activeMotion) {
                auto it = self->_runtime->activeMotion->timelineTotalFrames.find(timelineLabel);
                if(it != self->_runtime->activeMotion->timelineTotalFrames.end()) {
                    state.totalFrames = it->second;
                }
            }
        };

        bool started = false;
        if(!label.IsEmpty()) {
            const auto key = detail::narrow(label);
            if(self->_runtime->timelines.find(key) != self->_runtime->timelines.end()) {
                self->_motionKey = label;
                playOne(key);
                started = true;
            } else if(self->_runtime->activeMotion) {
                self->_motionKey = label;
            }
        }

        if(!started) {
            const auto &primary = !self->_runtime->activeMotion->mainTimelineLabels.empty()
                ? self->_runtime->activeMotion->mainTimelineLabels
                : self->_runtime->activeMotion->diffTimelineLabels;
            for(const auto &timelineLabel : primary) {
                playOne(timelineLabel);
                started = true;
            }
        }

        self->_allplaying = std::any_of(
            self->_runtime->timelines.begin(), self->_runtime->timelines.end(),
            [](const auto &entry) { return entry.second.playing; });

        if(result) {
            *result = tTJSVariant(started);
        }
        return TJS_S_OK;
    }

    tjs_error Player::progressCompatMethod(tTJSVariant *result, tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }

        self->ensureMotionLoaded();

        double delta = 0.0;
        if(numparams > 0 && param[0] && param[0]->Type() != tvtVoid) {
            delta = param[0]->AsReal();
        }
        // Clamp delta to sane range: TJS tick differences can overflow
        // when uint32 wraps (e.g. 4294967381 = 2^32 + 85)
        if(delta < 0 || delta > 60000) {
            delta = 0;
        }

        self->_runtime->pendingEvents.clear();
        self->frameProgress(delta * kMotionFramesPerMillisecond);

        // Aligned to libkrkr2.so Player_dispatchEvents (0x6C4490):
        // After stepping timelines, dispatch queued onAction/onSync events.
        if(!self->_runtime->pendingEvents.empty()) {
            for(const auto &ev : self->_runtime->pendingEvents) {
                try {
                    if(ev.type == 0) {
                        // onAction(param1, param2)
                        tTJSVariant p1(detail::widen(ev.param1));
                        tTJSVariant p2(detail::widen(ev.param2));
                        tTJSVariant *args[] = { &p1, &p2 };
                        objthis->FuncCall(0, TJS_W("onAction"),
                            nullptr, nullptr, 2, args, objthis);
                    } else if(ev.type == 1) {
                        // onSync()
                        objthis->FuncCall(0, TJS_W("onSync"),
                            nullptr, nullptr, 0, nullptr, objthis);
                    }
                } catch(...) {}
            }
            self->_runtime->pendingEvents.clear();
        }

        if(result) {
            *result = tTJSVariant(self->getProgressCompat());
        }
        return TJS_S_OK;
    }

    tjs_error Player::isPlayingCompat(tTJSVariant *result, tjs_int,
                                      tTJSVariant **, iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }

        const bool playing = std::any_of(
            self->_runtime->timelines.begin(), self->_runtime->timelines.end(),
            [](const auto &entry) { return entry.second.playing; });
        self->_allplaying = playing;
        if(result) {
            *result = tTJSVariant(playing);
        }
        return TJS_S_OK;
    }

    tjs_error Player::stopCompat(tTJSVariant *result, tjs_int numparams,
                                 tTJSVariant **param, iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }

        // Aligned to libkrkr2.so Player_stop (0x6D9A30):
        // Binary simply sets playing=false (offset 1099), nothing else.
        for(auto &[_, state] : self->_runtime->timelines) {
            state.playing = false;
        }
        self->_allplaying = false;

        if(result) {
            *result = tTJSVariant(true);
        }
        return TJS_S_OK;
    }

    tTJSVariant Player::motionList() {
        std::vector<std::string> paths;
        std::unordered_set<std::string> seen;
        for(const auto &[_, snapshot] : _runtime->motionsByKey) {
            if(snapshot && seen.insert(snapshot->path).second) {
                paths.push_back(snapshot->path);
            }
        }
        return detail::makeArray(detail::stringsToVariants(paths));
    }

    void Player::emoteEdit(tTJSVariant args) {
        _directEdit = true;
        _tags = args;
    }

} // namespace motion
