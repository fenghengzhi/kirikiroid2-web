//
// Created by LiDon on 2025/9/15.
// Minimal runtime implementation aligned to libkrkr2.so MMotionPlayer surface.
//

#include "Player.h"

#include <algorithm>
#include <array>
#include <cctype>
#include "WindowIntf.h"
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif
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

#ifndef KRKR2_NO_OPENCV
#include "opencv2/opencv.hpp"
#endif

#define LOGGER spdlog::get("plugin")
#define STUB_WARN(name) LOGGER->warn("Player::" #name "() stub called")

namespace motion {

    namespace {

        bool isLogoMotionLike(const std::string &value) {
            const auto lowered = [] (std::string text) {
                std::transform(text.begin(), text.end(), text.begin(),
                               [](unsigned char ch) {
                                   return static_cast<char>(std::tolower(ch));
                               });
                return text;
            }(value);
            return lowered.find("yuzulogo") != std::string::npos ||
                lowered.find("m2logo") != std::string::npos;
        }

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
        std::vector<std::uint8_t> decompressPsbRL(
            const std::vector<std::uint8_t> &compressed,
            int width, int height) {
            const size_t totalPixels = static_cast<size_t>(width) * height;
            const size_t expectedBytes = totalPixels * 4u;
            std::vector<std::uint8_t> output(expectedBytes, 0);

            auto decompressChannel =
                [&](size_t srcStart, std::vector<std::uint8_t> &channelBuf)
                -> size_t {
                channelBuf.resize(totalPixels, 0);
                size_t src = srcStart;
                size_t dst = 0;
                while(src < compressed.size() && dst < totalPixels) {
                    const auto marker = compressed[src++];
                    if(marker & 0x80) {
                        const size_t count =
                            static_cast<size_t>(marker & 0x7F) + 1;
                        if(src >= compressed.size()) break;
                        const auto val = compressed[src++];
                        const size_t end = std::min(dst + count, totalPixels);
                        std::memset(channelBuf.data() + dst, val, end - dst);
                        dst = end;
                    } else {
                        const size_t count =
                            static_cast<size_t>(marker) + 1;
                        const size_t end = std::min(dst + count, totalPixels);
                        const size_t actual = end - dst;
                        if(src + actual > compressed.size()) break;
                        std::memcpy(channelBuf.data() + dst,
                                    compressed.data() + src, actual);
                        src += actual;
                        dst = end;
                    }
                }
                return src;
            };

            std::vector<std::uint8_t> channels[4];
            size_t pos = 0;
            for(int ch = 0; ch < 4; ++ch) {
                pos = decompressChannel(pos, channels[ch]);
            }

            // Interleave channels into RGBA pixel output
            for(size_t i = 0; i < totalPixels; ++i) {
                output[i * 4 + 0] = channels[0][i]; // R
                output[i * 4 + 1] = channels[1][i]; // G
                output[i * 4 + 2] = channels[2][i]; // B
                output[i * 4 + 3] = channels[3][i]; // A
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
#ifdef __EMSCRIPTEN__
            {
                bool hadPlaying = std::any_of(
                    runtime.timelines.begin(), runtime.timelines.end(),
                    [](const auto &e) { return e.second.playing; });
                if(hadPlaying && snapshot) {
                    EM_ASM({ console.warn('[activateMotion-CLEAR] path=' + UTF8ToString($0) + ' HAD PLAYING'); },
                           snapshot->path.c_str());
                }
            }
#endif
            runtime.activeMotion = snapshot;
            runtime.timelines.clear();
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

        struct FrameContentState {
            bool visible = false;
            std::string src;
            double x = 0.0;
            double y = 0.0;
            double ox = 0.0;
            double oy = 0.0;
            double width = 0.0;
            double height = 0.0;
            double opacity = 1.0;
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

            if(const auto src = psbDictionaryString(content, "src"); !src.empty()) {
                state.src = src;
            }
            if(const auto ox = psbDictionaryNumber(content, "ox")) {
                state.ox = *ox;
            }
            if(const auto oy = psbDictionaryNumber(content, "oy")) {
                state.oy = *oy;
            }
            if(const auto zx = psbDictionaryNumber(content, "zx")) {
                state.width = *zx;
            }
            if(const auto zy = psbDictionaryNumber(content, "zy")) {
                state.height = *zy;
            }
            if(const auto opacity = psbDictionaryNumber(content, "opacity")) {
                state.opacity = std::clamp(*opacity / 255.0, 0.0, 1.0);
            }
            if(const auto x = psbDictionaryNumber(content, "x")) {
                state.x = *x;
            }
            if(const auto y = psbDictionaryNumber(content, "y")) {
                state.y = *y;
            }

            if(const auto coord = psbDictionaryList(content, "coord")) {
                if(coord->size() > 0) {
                    if(const auto value = psbNumberValue((*coord)[0])) {
                        state.x = *value;
                    }
                }
                if(coord->size() > 1) {
                    if(const auto value = psbNumberValue((*coord)[1])) {
                        state.y = *value;
                    }
                }
            }
        }

        FrameContentState
        evaluateLayerContent(const std::shared_ptr<const PSB::PSBDictionary> &layer,
                             double time) {
            FrameContentState state;
            const auto frames = psbDictionaryList(layer, "frameList");
            if(!frames) {
                return state;
            }

            for(size_t index = 0; index < frames->size(); ++index) {
                const auto frame =
                    std::dynamic_pointer_cast<PSB::PSBDictionary>((*frames)[static_cast<int>(index)]);
                if(!frame) {
                    continue;
                }

                const auto frameTime =
                    psbDictionaryNumber(frame, "time").value_or(0.0);
                if(frameTime > time) {
                    break;
                }

                const auto type =
                    static_cast<int>(psbDictionaryNumber(frame, "type").value_or(0.0));
                if(type == 0) {
                    state.visible = false;
                    state.src.clear();
                    continue;
                }

                if(const auto content = psbDictionaryValue(frame, "content")) {
                    mergeFrameContent(content, state);
                }
                state.visible = true;
            }

            return state;
        }

#ifndef KRKR2_NO_OPENCV
        struct LoadedSourceImage {
            bool attempted = false;
            cv::Mat image;
        };

        cv::Matx33d translateMatrix(double x, double y) {
            return cv::Matx33d(1.0, 0.0, x, 0.0, 1.0, y, 0.0, 0.0, 1.0);
        }

        cv::Matx33d scaleMatrix(double x, double y) {
            return cv::Matx33d(x, 0.0, 0.0, 0.0, y, 0.0, 0.0, 0.0, 1.0);
        }

        bool copyLayerMainImage(tTJSNI_BaseLayer *layer, cv::Mat &image) {
            image.release();
            if(!layer) {
                return false;
            }

            auto *mainImage = layer->GetMainImage();
            const auto *srcPixels = reinterpret_cast<const std::uint8_t *>(
                layer->GetMainImagePixelBuffer());
            const auto pitch = layer->GetMainImagePixelBufferPitch();
            if(!mainImage || !srcPixels || pitch <= 0) {
                return false;
            }

            const auto width = static_cast<int>(mainImage->GetWidth());
            const auto height = static_cast<int>(mainImage->GetHeight());
            if(width <= 0 || height <= 0) {
                return false;
            }

            cv::Mat view(height, width, CV_8UC4,
                         const_cast<std::uint8_t *>(srcPixels), pitch);
            image = view.clone();
            return !image.empty();
        }

        bool loadMotionSourceImage(
            tTJSNI_BaseLayer *scratchLayer, const detail::MotionSnapshot &snapshot,
            const std::string &source,
            std::unordered_map<std::string, LoadedSourceImage> &cache,
            cv::Mat &image) {
            image.release();
            if(!scratchLayer || source.empty()) {
                return false;
            }

            if(const auto it = cache.find(source); it != cache.end()) {
                if(!it->second.image.empty()) {
                    image = it->second.image.clone();
                    return true;
                }
                return false;
            }

            auto &entry = cache[source];
            entry.attempted = true;
            const bool logoLike = isLogoMotionLike(snapshot.path);

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

            std::unordered_set<std::string> seen;
            for(const auto &candidate : candidates) {
                ttstr loadPath = candidate;
                const auto candidateKey = detail::narrow(candidate);
                if(!seen.insert(candidateKey).second || loadPath.IsEmpty()) {
                    continue;
                }

                if(candidateKey.rfind("psb://", 0) != 0) {
                    if(const auto placed = TVPGetPlacedPath(candidate);
                       !placed.IsEmpty()) {
                        loadPath = placed;
                    }
                }

                try {
                    if(auto *meta = scratchLayer->LoadImages(loadPath, TVP_clNone)) {
                        meta->Release();
                    }
                    if(copyLayerMainImage(scratchLayer, entry.image)) {
                        if(logoLike) {
                            LOGGER->warn(
                                "Motion logo source resolved: motion={} source={} candidate={}",
                                snapshot.path, source, candidateKey);
                        }
                        image = entry.image.clone();
                        return true;
                    }
                } catch(...) {
                }
            }

            if(logoLike) {
                LOGGER->warn("Motion logo source unresolved: motion={} source={}",
                             snapshot.path, source);
            }

            return false;
        }

        void alphaBlendImage(const cv::Mat &src, cv::Mat &dst, double opacity) {
            if(src.empty() || dst.empty() || src.size() != dst.size()) {
                return;
            }

            const auto clampedOpacity = std::clamp(opacity, 0.0, 1.0);
            for(int y = 0; y < dst.rows; ++y) {
                const auto *srcRow = src.ptr<cv::Vec4b>(y);
                auto *dstRow = dst.ptr<cv::Vec4b>(y);
                for(int x = 0; x < dst.cols; ++x) {
                    const double srcAlpha =
                        (static_cast<double>(srcRow[x][3]) / 255.0) * clampedOpacity;
                    if(srcAlpha <= 0.0) {
                        continue;
                    }

                    const double dstAlpha =
                        static_cast<double>(dstRow[x][3]) / 255.0;
                    const double outAlpha =
                        srcAlpha + dstAlpha * (1.0 - srcAlpha);
                    if(outAlpha <= 0.0) {
                        dstRow[x] = cv::Vec4b(0, 0, 0, 0);
                        continue;
                    }

                    for(int channel = 0; channel < 3; ++channel) {
                        const double srcValue = srcRow[x][channel] / 255.0;
                        const double dstValue = dstRow[x][channel] / 255.0;
                        const double outValue =
                            (srcValue * srcAlpha +
                             dstValue * dstAlpha * (1.0 - srcAlpha)) /
                            outAlpha;
                        dstRow[x][channel] = static_cast<std::uint8_t>(
                            std::clamp(outValue * 255.0, 0.0, 255.0));
                    }
                    dstRow[x][3] = static_cast<std::uint8_t>(
                        std::clamp(outAlpha * 255.0, 0.0, 255.0));
                }
            }
        }

        bool drawEvaluatedSource(cv::Mat &canvas,
                                 tTJSNI_BaseLayer *scratchLayer,
                                 const detail::MotionSnapshot &snapshot,
                                 std::unordered_map<std::string, LoadedSourceImage> &cache,
                                 const FrameContentState &state,
                                 const cv::Matx33d &transform) {
            if(!state.visible || state.src.empty() || state.src == "layout"
               || isMotionCrossReference(state.src)) {
                if(isLogoMotionLike(snapshot.path) && !state.src.empty()
                   && state.src != "layout" && !isMotionCrossReference(state.src)) {
                    LOGGER->warn("drawEvaluatedSource skip: visible={} src='{}' opacity={}",
                                 state.visible, state.src, state.opacity);
                }
                return false;
            }

            cv::Mat source;
            if(!loadMotionSourceImage(scratchLayer, snapshot, state.src, cache,
                                      source)) {
                return false;
            }

            const double drawWidth =
                state.width > 0.0 ? state.width : static_cast<double>(source.cols);
            const double drawHeight =
                state.height > 0.0 ? state.height : static_cast<double>(source.rows);
            const cv::Matx33d imageTransform =
                transform *
                scaleMatrix(drawWidth / static_cast<double>(source.cols),
                            drawHeight / static_cast<double>(source.rows));

            cv::Mat warped(canvas.rows, canvas.cols, CV_8UC4,
                           cv::Scalar(0, 0, 0, 0));
            const cv::Mat affine =
                (cv::Mat_<double>(2, 3) << imageTransform(0, 0),
                 imageTransform(0, 1), imageTransform(0, 2), imageTransform(1, 0),
                 imageTransform(1, 1), imageTransform(1, 2));
            cv::warpAffine(source, warped, affine, canvas.size(), cv::INTER_LINEAR,
                           cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0, 0));
            alphaBlendImage(warped, canvas, state.opacity);
            return true;
        }

        bool renderMotionLayer(cv::Mat &canvas,
                               tTJSNI_BaseLayer *scratchLayer,
                               const detail::MotionSnapshot &snapshot,
                               std::unordered_map<std::string, LoadedSourceImage> &cache,
                               const std::shared_ptr<const PSB::PSBDictionary> &layer,
                               double time, const cv::Matx33d &parentTransform) {
            if(!layer) {
                return false;
            }

            const auto state = evaluateLayerContent(layer, time);
            cv::Matx33d currentTransform =
                parentTransform * translateMatrix(state.x + state.ox,
                                                  state.y + state.oy);
            bool drewAny =
                drawEvaluatedSource(canvas, scratchLayer, snapshot, cache, state,
                                    currentTransform);

            if(const auto children = psbDictionaryList(layer, "children")) {
                cv::Matx33d childTransform = currentTransform;
                if(state.src == "layout" && state.width > 0.0 &&
                   state.height > 0.0) {
                    childTransform =
                        currentTransform * scaleMatrix(state.width, state.height);
                }

                for(size_t index = 0; index < children->size(); ++index) {
                    const auto child = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                        (*children)[static_cast<int>(index)]);
                    drewAny = renderMotionLayer(canvas, scratchLayer, snapshot,
                                                cache, child, time,
                                                childTransform) ||
                        drewAny;
                }
            }

            return drewAny;
        }
#endif

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
        // If the resource is RL-compressed, decompresses into decompressedOut.
        const PSB::PSBResource *findPSBResourceBySourceName(
            const detail::MotionSnapshot &snapshot,
            const std::string &source,
            int &outWidth, int &outHeight,
            std::vector<std::uint8_t> &decompressedOut) {
            outWidth = 0;
            outHeight = 0;
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
                    // For psb:// paths, check if the resource exists via the
                    // storage system (PSBMedia::CheckExistentStorage)
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
        struct FlatRenderNode {
            FrameContentState state;
            double x = 0.0, y = 0.0;     // computed position
            double scaleX = 1.0, scaleY = 1.0; // accumulated scale
        };

        void flattenLayerNodes(
            const std::shared_ptr<const PSB::PSBDictionary> &node,
            double time,
            double parentX, double parentY,
            double parentScaleX, double parentScaleY,
            std::vector<FlatRenderNode> &out) {
            if(!node) return;
            const auto state = evaluateLayerContent(node, time);
            const double curX = parentX + (state.x + state.ox) * parentScaleX;
            const double curY = parentY + (state.y + state.oy) * parentScaleY;
            double curScaleX = parentScaleX;
            double curScaleY = parentScaleY;

            if(state.visible && !state.src.empty() && state.src != "layout"
               && !isMotionCrossReference(state.src)) {
                out.push_back({state, curX, curY, curScaleX, curScaleY});
            }

            if(const auto children = psbDictionaryList(node, "children")) {
                double childScaleX = curScaleX;
                double childScaleY = curScaleY;
                if(state.src == "layout" && state.width > 0.0 && state.height > 0.0) {
                    childScaleX = curScaleX * state.width;
                    childScaleY = curScaleY * state.height;
                }
                for(size_t i = 0; i < children->size(); ++i) {
                    auto child = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                        (*children)[static_cast<int>(i)]);
                    flattenLayerNodes(child, time, curX, curY,
                                     childScaleX, childScaleY, out);
                }
            }
        }

    } // namespace

    Player::Player(ResourceManager rm) :
        _runtime(detail::makePlayerRuntime()),
        _resourceManagerNative(std::move(rm)) {
        LOGGER->info("Motion.Player constructor called");
    }

    Player::~Player() {
        stopSelfDrive();
    }

    void Player::setMotion(ttstr v) {
#ifdef __EMSCRIPTEN__
        {
            bool hadPlaying = std::any_of(
                _runtime->timelines.begin(), _runtime->timelines.end(),
                [](const auto &e) { return e.second.playing; });
            if(hadPlaying) {
                EM_ASM({ console.warn('[setMotion-CLEAR] new=' + UTF8ToString($0) + ' old=' + UTF8ToString($1) + ' HAD PLAYING TIMELINES'); },
                       v.c_str(), _motionKey.c_str());
            }
        }
#endif
        if(isLogoMotionLike(detail::narrow(v))) {
            LOGGER->warn("Motion logo setMotion: request={} previous={}",
                         v.AsStdString(), _motionKey.AsStdString());
        }
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

    bool Player::ensureMotionLoaded() {
        if(_runtime->activeMotion) {
            return true;
        }

        const auto motionKey = detail::narrow(_motionKey);
        const bool logoLike = isLogoMotionLike(motionKey);
        const bool motionKeyLooksLikeStorage =
            motionKey.find('/') != std::string::npos ||
            motionKey.find('\\') != std::string::npos ||
            motionKey.find('.') != std::string::npos;

        if(_project.Type() == tvtObject) {
            if(const auto snapshot = detail::lookupModuleSnapshot(_project)) {
                if(logoLike) {
                    LOGGER->warn("Motion logo ensureMotionLoaded: using project snapshot path={}",
                                 snapshot->path);
                }
                activateMotion(*_runtime, snapshot);
                syncVariableKeysFromActiveMotion();
                return true;
            }
        }

        if(motionKeyLooksLikeStorage) {
            if(const auto snapshot =
                   resolveMotion(*_runtime, _motionKey, &_resourceManagerNative)) {
                if(logoLike) {
                    LOGGER->warn("Motion logo ensureMotionLoaded: resolved from storage key={} path={}",
                                 motionKey, snapshot->path);
                }
                activateMotion(*_runtime, snapshot);
                syncVariableKeysFromActiveMotion();
                return true;
            }
        }

        if(const auto loaded = _resourceManagerNative.getLastLoadedModule();
           loaded.Type() == tvtObject) {
            if(const auto snapshot = detail::lookupModuleSnapshot(loaded)) {
                if(logoLike) {
                    LOGGER->warn("Motion logo ensureMotionLoaded: using lastLoadedModule key={} path={}",
                                 motionKey, snapshot->path);
                }
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
            if(logoLike) {
                LOGGER->warn("Motion logo ensureMotionLoaded: resolved fallback key={} path={}",
                             motionKey, snapshot->path);
            }
            activateMotion(*_runtime, snapshot);
            syncVariableKeysFromActiveMotion();
            return true;
        }

        if(logoLike) {
            LOGGER->warn(
                "Motion logo ensureMotionLoaded: failed key={} lastLoadedType={} motionKeyLooksLikeStorage={}",
                motionKey,
                static_cast<int>(_resourceManagerNative.getLastLoadedModule().Type()),
                motionKeyLooksLikeStorage);
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
#ifdef __EMSCRIPTEN__
        if(_runtime->activeMotion) {
            EM_ASM({ console.warn('[setProgressCompat] v=' + $0 + ' path=' + UTF8ToString($1)); },
                   v, _runtime->activeMotion->path.c_str());
        }
#endif
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
        EM_ASM({ console.warn('[_allplaying] setProgressCompat=' + $0 + ' ptr=0x' + ($1 >>> 0).toString(16)); },
               (int)_allplaying, (int)(uintptr_t)this);
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
        EM_ASM({ console.warn('[_allplaying] stopTimeline=' + $0 + ' ptr=0x' + ($1 >>> 0).toString(16)); },
               (int)_allplaying, (int)(uintptr_t)this);
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

    void Player::setCameraOffset(tTJSVariant offset) { _cameraPosition = offset; }

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

#ifdef __EMSCRIPTEN__
        EM_ASM({ console.warn('[renderToLayer] obj=' + $0 + ' activeMotion=' + $1 + ' path=' + UTF8ToString($2)); },
               (int)(layerObject != nullptr),
               (int)(_runtime->activeMotion != nullptr),
               _runtime->activeMotion ? _runtime->activeMotion->path.c_str() : "null");
#endif

        const bool logoLike = isLogoMotionLike(_runtime->activeMotion->path);

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
                if(logoLike) {
                    LOGGER->warn("renderToLayer: NIS failed hr={} ptr={} obj={} tryResolve={} same={}",
                                 nisResult, (void*)layer, (void*)layerObject,
                                 resolved != nullptr,
                                 resolved == layerObject);
                }
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
                    if(logoLike) {
                        LOGGER->warn("renderToLayer: NIS retry SUCCEEDED ptr={}",
                                     (void*)layer);
                    }
                    // Fall through to rendering
                } else {
                    return false;
                }
            }
        }
        if(logoLike) {
            LOGGER->warn("renderToLayer: got native Layer ptr={} w={} h={} resources={}",
                         (void*)layer, layer->GetWidth(), layer->GetHeight(),
                         _runtime->activeMotion->resourcesByPath.size());
        }

#ifndef KRKR2_NO_OPENCV
        if(_runtime->activeMotion && !_runtime->activeMotion->resourcesByPath.empty()) {
            const auto *clip = selectActiveClip();
            const auto renderTime = activeClipTime(*_runtime, clip);

            int canvasWidth = static_cast<int>(layer->GetWidth());
            int canvasHeight = static_cast<int>(layer->GetHeight());
            if(canvasWidth <= 0 || canvasHeight <= 0) {
                canvasWidth = static_cast<int>(layer->GetImageWidth());
                canvasHeight = static_cast<int>(layer->GetImageHeight());
            }
            if(canvasWidth <= 0 || canvasHeight <= 0) {
                canvasWidth = static_cast<int>(_runtime->activeMotion->width);
                canvasHeight = static_cast<int>(_runtime->activeMotion->height);
            }
            if(canvasWidth > 0 && canvasHeight > 0) {
                try {
                    std::vector<std::uint8_t> canvasStorage(
                        static_cast<size_t>(canvasWidth) *
                        static_cast<size_t>(canvasHeight) * 4u, 0);
                    cv::Mat canvas(canvasHeight, canvasWidth, CV_8UC4,
                                   canvasStorage.data());
                    std::unordered_map<std::string, LoadedSourceImage>
                        sourceCache;

                    const auto &m = _runtime->drawAffineMatrix;
                    const cv::Matx33d globalTransform(
                        m[0], m[1], m[4], m[2], m[3], m[5], 0.0, 0.0, 1.0);

                    bool drewAny = false;
                    const auto &layerNamesList = activeLayerNames();
                    if(logoLike) {
                        LOGGER->warn("renderToLayer OpenCV: canvasSize={}x{} "
                                     "layerNames={} renderTime={} motionKey={}",
                                     canvasWidth, canvasHeight,
                                     layerNamesList.size(), renderTime,
                                     _motionKey.AsStdString());
                    }
                    for(const auto &layerName : layerNamesList) {
                        const auto *layers = activeLayersByName();
                        if(!layers) {
                            if(logoLike) {
                                LOGGER->warn("renderToLayer: activeLayersByName returned null");
                            }
                            break;
                        }
                        const auto it = layers->find(layerName);
                        if(it == layers->end()) {
                            if(logoLike) {
                                LOGGER->warn("renderToLayer: layer '{}' not found in map (map size={})",
                                             layerName, layers->size());
                            }
                            continue;
                        }
                        bool result = renderMotionLayer(
                                      canvas, layer, *_runtime->activeMotion,
                                      sourceCache, it->second, renderTime,
                                      globalTransform);
                        if(logoLike) {
                            LOGGER->warn("renderToLayer: renderMotionLayer '{}' returned {}",
                                         layerName, result);
                        }
                        drewAny = result || drewAny;
                    }

                    if(drewAny) {
                        if(!layer->GetHasImage()) {
                            layer->SetHasImage(true);
                        }
                        layer->SetImageSize(static_cast<tjs_uint>(canvasWidth),
                                            static_cast<tjs_uint>(canvasHeight));
                        auto *dstPixels = reinterpret_cast<std::uint8_t *>(
                            layer->GetMainImagePixelBufferForWrite());
                        const auto pitch =
                            layer->GetMainImagePixelBufferPitch();
                        if(dstPixels && pitch > 0) {
                            const auto *srcPixels = canvas.ptr<std::uint8_t>(0);
                            const auto srcPitch =
                                static_cast<size_t>(canvasWidth) * 4u;
                            for(int row = 0; row < canvasHeight; ++row) {
                                std::memcpy(
                                    dstPixels + static_cast<size_t>(pitch) * row,
                                    srcPixels + srcPitch * row, srcPitch);
                            }

                            {
                                // Quick pixel check
                                int nz = 0;
                                for(int r = 0; r < canvasHeight; r += canvasHeight/3+1)
                                    for(int c = 0; c < canvasWidth; c += canvasWidth/5+1)
                                        if(dstPixels[pitch*r + c*4 + 3]) nz++;
                                EM_ASM({ console.warn('[render] ' + $0 + 'x' + $1 + ' nz=' + $2 + ' skip=' + $3); },
                                       canvasWidth, canvasHeight, nz, (int)skipUpdate);
                            }
                            if(!skipUpdate) layer->Update(false);
                            _runtime->lastCanvas =
                                tTJSVariant(layerObject, layerObject);
                            return true;
                        }
                    }
                } catch(...) {
                    LOGGER->warn("Motion.Player tree render failed for {}",
                                 _runtime->activeMotion->path);
                }
            }
        }
#else
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
                    if(logoLike) {
                        LOGGER->warn("renderToLayer LayerAPI: canvasSize={}x{} "
                                     "layerNames={} renderTime={} motionKey={}",
                                     canvasWidth, canvasHeight,
                                     layerNamesList.size(), renderTime,
                                     _motionKey.AsStdString());
                    }

                    // Apply global affine transform
                    const auto &m = _runtime->drawAffineMatrix;
                    const double globalTx = m[4];
                    const double globalTy = m[5];
                    const double globalSx = m[0]; // scale X (m11)
                    const double globalSy = m[3]; // scale Y (m22)

                    // Step 1 (sub_6C4E28): Flatten PSB layer tree into
                    // a flat list with pre-computed positions.
                    std::vector<FlatRenderNode> renderNodes;
                    for(const auto &layerName : layerNamesList) {
                        const auto *layers = activeLayersByName();
                        if(!layers) break;
                        const auto it = layers->find(layerName);
                        if(it == layers->end()) continue;
                        flattenLayerNodes(it->second, renderTime,
                                          globalTx, globalTy,
                                          globalSx, globalSy,
                                          renderNodes);
                    }

                    // If we got 0 render nodes, check for motion cross-
                    // references in the active clip's layers and resolve
                    // them. A motion cross-reference has src like
                    // "motion/<owner>/<clipLabel>". We look up the
                    // referenced clip and flatten its layers instead.
                    if(renderNodes.empty()) {
                        const auto *layers = activeLayersByName();
                        if(layers) {
                            for(const auto &layerName : layerNamesList) {
                                const auto it = layers->find(layerName);
                                if(it == layers->end()) continue;
                                const auto st = evaluateLayerContent(
                                    it->second, renderTime);
                                if(!isMotionCrossReference(st.src)) continue;
                                // Parse "motion/<owner>/<clipLabel>"
                                const auto parts = st.src.substr(7); // skip "motion/"
                                const auto slash = parts.rfind('/');
                                if(slash == std::string::npos) continue;
                                const auto refLabel = parts.substr(slash + 1);
                                // Look up the referenced clip
                                const auto clipIt =
                                    _runtime->activeMotion->clipsByLabel
                                        .find(refLabel);
                                if(clipIt ==
                                   _runtime->activeMotion->clipsByLabel.end())
                                    continue;
                                // Flatten the referenced clip's layers
                                for(const auto &refLayerName :
                                    clipIt->second.layerNames) {
                                    const auto refIt =
                                        clipIt->second.layersByName
                                            .find(refLayerName);
                                    if(refIt ==
                                       clipIt->second.layersByName.end())
                                        continue;
                                    flattenLayerNodes(
                                        refIt->second, renderTime,
                                        globalTx + (st.x + st.ox),
                                        globalTy + (st.y + st.oy),
                                        globalSx, globalSy, renderNodes);
                                }
                            }
                        }
                    }

                    EM_ASM({ console.warn('[motion] nodes=' + $0 + ' layers=' + $1 + ' path=' + UTF8ToString($2)); },
                           (int)renderNodes.size(), (int)layerNamesList.size(),
                           _runtime->activeMotion->path.c_str());

                    // Step 2 (sub_6C7440): Flat loop — for each node,
                    // load source bitmap and call OperateAffine on target.
                    bool drewAny = false;
                    std::unordered_map<std::string, std::shared_ptr<tTVPBaseBitmap>> srcCache;

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
                                EM_ASM({ console.warn('[src-file] src=' + UTF8ToString($0) + ' path=' + UTF8ToString($1) + ' ok=' + $2); },
                                       node.state.src.c_str(), resolvedPath.c_str(), srcBmp ? 1 : 0);
                            }
                            // Try PSB embedded resource
                            if(!srcBmp) {
                                int rw = 0, rh = 0;
                                std::vector<std::uint8_t> decompressed;
                                const auto *res = findPSBResourceBySourceName(
                                    *_runtime->activeMotion, node.state.src,
                                    rw, rh, decompressed);
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
                                EM_ASM({ console.warn('[src-psb] src=' + UTF8ToString($0) + ' found=' + $1 + ' w=' + $2 + ' h=' + $3 + ' rl=' + $4); },
                                       node.state.src.c_str(), res ? 1 : 0, rw, rh,
                                       decompressed.empty() ? 0 : 1);
                            }
                            srcCache.emplace(node.state.src, srcBmp);
                        }

                        if(!srcBmp || srcBmp->GetWidth() == 0) {
                            EM_ASM({ console.warn('[src-fail] src=' + UTF8ToString($0)); },
                                   node.state.src.c_str());
                            continue;
                        }

                        // Compute affine destination points
                        // Aligned to sub_6C7440 operateAffine call
                        const double srcW = static_cast<double>(srcBmp->GetWidth());
                        const double srcH = static_cast<double>(srcBmp->GetHeight());
                        const double drawW = node.state.width > 0.0
                            ? node.state.width : srcW;
                        const double drawH = node.state.height > 0.0
                            ? node.state.height : srcH;
                        const double sx = (drawW / srcW) * node.scaleX;
                        const double sy = (drawH / srcH) * node.scaleY;

                        // libkrkr2.so applies -0.5 texel offset
                        tTVPPointD pts[3];
                        pts[0] = {node.x - 0.5,              node.y - 0.5};
                        pts[1] = {node.x - 0.5 + srcW * sx,  node.y - 0.5};
                        pts[2] = {node.x - 0.5,              node.y - 0.5 + srcH * sy};

                        tTVPRect sr(0, 0, static_cast<tjs_int>(srcW),
                                    static_cast<tjs_int>(srcH));
                        const tjs_int opa = static_cast<tjs_int>(
                            std::clamp(node.state.opacity * 255.0, 0.0, 255.0));

                        try {
                            layer->OperateAffine(pts, srcBmp.get(), sr,
                                                 omAlpha, opa, stNearest);
                            drewAny = true;
                            EM_ASM({ console.warn('[affine-ok] src=' + UTF8ToString($0) + ' ' + $1 + 'x' + $2 + ' at(' + $3 + ',' + $4 + ') opa=' + $5); },
                                   node.state.src.c_str(), (int)srcW, (int)srcH,
                                   (int)node.x, (int)node.y, opa);
                        } catch(const eTJS &e) {
                            EM_ASM({ console.warn('[affine-err] src=' + UTF8ToString($0) + ' err=' + UTF8ToString($1)); },
                                   node.state.src.c_str(), ttstr(e.GetMessage()).c_str());
                        } catch(...) {
                            EM_ASM({ console.warn('[affine-err] src=' + UTF8ToString($0) + ' unknown'); },
                                   node.state.src.c_str());
                        }
                    }

                    EM_ASM({ console.warn('[render-done] drewAny=' + $0 + ' nodes=' + $1 + ' canvas=' + $2 + 'x' + $3); },
                           (int)drewAny, (int)renderNodes.size(), canvasWidth, canvasHeight);

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
#endif
        const auto sourcePath = resolveCaptureSourcePath();
        if(sourcePath.IsEmpty()) {
            const auto loweredPath = _runtime->activeMotion
                ? detail::narrow(detail::widen(_runtime->activeMotion->path))
                : std::string{};
            const auto loweredMotionPath =
                loweredPath.empty() ? std::string{} : [] (std::string value) {
                    std::transform(value.begin(), value.end(), value.begin(),
                                   [](unsigned char ch) {
                                       return static_cast<char>(std::tolower(ch));
                                   });
                    return value;
                }(loweredPath);
            if(_runtime->activeMotion &&
               (loweredMotionPath.find("yuzulogo.mtn") != std::string::npos ||
                loweredMotionPath.find("m2logo.mtn") != std::string::npos)) {
                LOGGER->warn("Motion logo draw unresolved: path={} motionKey={} clipSources={} allSources={}",
                             _runtime->activeMotion->path, _motionKey.AsStdString(),
                             activeSourceCandidates().size(),
                             _runtime->activeMotion->sourceCandidates.size());
            }
            LOGGER->warn("Motion.Player draw fallback could not resolve source "
                         "for {}",
                         _runtime->activeMotion->path);
            return false;
        }
        const auto loweredMotionPath = _runtime->activeMotion
            ? [] (std::string value) {
                  std::transform(value.begin(), value.end(), value.begin(),
                                 [](unsigned char ch) {
                                     return static_cast<char>(std::tolower(ch));
                                 });
                  return value;
              }(_runtime->activeMotion->path)
            : std::string{};
        if(_runtime->activeMotion &&
           (loweredMotionPath.find("yuzulogo.mtn") != std::string::npos ||
            loweredMotionPath.find("m2logo.mtn") != std::string::npos)) {
            LOGGER->warn("Motion logo draw resolved: path={} motionKey={} source={}",
                         _runtime->activeMotion->path, _motionKey.AsStdString(),
                         sourcePath.AsStdString());
        }

        try {
            if(!layer->GetHasImage()) {
                layer->SetHasImage(true);
            }

            if(auto *meta = layer->LoadImages(sourcePath, TVP_clNone)) {
                meta->Release();
            }

#ifndef KRKR2_NO_OPENCV
            auto *mainImage = layer->GetMainImage();
            const auto *srcPixels = reinterpret_cast<const std::uint8_t *>(
                layer->GetMainImagePixelBuffer());
            auto *dstPixels = reinterpret_cast<std::uint8_t *>(
                layer->GetMainImagePixelBufferForWrite());
            const auto pitch = layer->GetMainImagePixelBufferPitch();
            if(mainImage && srcPixels && dstPixels && pitch > 0) {
                const auto width = static_cast<int>(mainImage->GetWidth());
                const auto height = static_cast<int>(mainImage->GetHeight());
                if(width > 0 && height > 0) {
                    std::vector<std::uint8_t> srcCopy(
                        static_cast<size_t>(pitch) * static_cast<size_t>(height));
                    std::memcpy(srcCopy.data(), srcPixels, srcCopy.size());

                    cv::Mat srcMat(height, width, CV_8UC4, srcCopy.data(), pitch);
                    cv::Mat dstMat(height, width, CV_8UC4, dstPixels, pitch);
                    dstMat.setTo(cv::Scalar(0, 0, 0, 0));

                    const auto &m = _runtime->drawAffineMatrix;
                    const cv::Mat affine =
                        (cv::Mat_<double>(2, 3) << m[0], m[1], m[4], m[2], m[3],
                         m[5]);
                    cv::warpAffine(srcMat, dstMat, affine, dstMat.size(),
                                   cv::INTER_LINEAR, cv::BORDER_CONSTANT,
                                   cv::Scalar(0, 0, 0, 0));
                }
            }
#endif

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
#ifdef __EMSCRIPTEN__
        if(_runtime->activeMotion && isLogoMotionLike(_runtime->activeMotion->path)) {
            static int fpLogCount = 0;
            if(fpLogCount++ < 30) {
                EM_ASM({ console.warn('[frameProgress] dt=' + $0 + ' tickCount=' + $1 + ' path=' + UTF8ToString($2)); },
                       dt, _tickCount, _runtime->activeMotion->path.c_str());
            }
        }
#endif
        _frameLastTime = dt;
        _frameLoopTime += dt;
        _loopTime += dt;
        _tickCount += dt;
        _frameTickCount += 1.0;
        detail::stepTimelines(_runtime->timelines, dt);

        _allplaying = std::any_of(
            _runtime->timelines.begin(), _runtime->timelines.end(),
            [](const auto &entry) { return entry.second.playing; });
        if(_runtime->activeMotion && isLogoMotionLike(_runtime->activeMotion->path)) {
            static int fpLogoCount = 0;
            if(fpLogoCount++ < 10) {
                std::string tlDetail;
                for(const auto &[k, v] : _runtime->timelines) {
                    tlDetail += k + "(p=" + std::to_string(v.playing) + ",t=" +
                        std::to_string(v.currentTime) + "/" + std::to_string(v.totalFrames) + ") ";
                }
                EM_ASM({ console.warn('[fp-logo] allplaying=' + $0 + ' dt=' + $1 + ' tl=' + UTF8ToString($2)); },
                       (int)_allplaying, dt, tlDetail.c_str());
            }
        }
        _syncActive = _syncWaiting && _allplaying;
    }

    // --- Self-driving animation loop ---
    // For the non-D3D web build, AffineLayer's continuous handler may not
    // fire.  When a timeline is playing, register a TJS continuous handler
    // that advances the animation and triggers onPaint on the owner's
    // target layer so the AffineLayer.onPaint → drawAffine chain fires.
    struct SelfDriveContinuousHandler : public tTJSDispatch {
        Player *player = nullptr;
        tTJSVariant ownerObj;       // prevents GC (AddRef'd)
        tTJSVariant affineSourceObj; // prevents GC (AddRef'd)
        tjs_int64 lastTick = 0;
        bool notifiedStop = false;
        bool disabled = false;

        iTJSDispatch2 *owner() const {
            return ownerObj.Type() == tvtObject
                ? ownerObj.AsObjectNoAddRef() : nullptr;
        }

        iTJSDispatch2 *affine() const {
            return affineSourceObj.Type() == tvtObject
                ? affineSourceObj.AsObjectNoAddRef() : nullptr;
        }

        tjs_error FuncCall(tjs_uint32 flag,
            const tjs_char *membername, tjs_uint32 *hint,
            tTJSVariant *result,
            tjs_int numparams, tTJSVariant **param,
            iTJSDispatch2 *objthis) override {
            if(result) *result = (tjs_int)1;
            if(disabled || !player || !player->_runtime) {
                return TJS_S_OK;
            }
#ifdef __EMSCRIPTEN__
            {
                static int selfDriveCallCount = 0;
                if(selfDriveCallCount++ < 40) {
                    std::string tlInfo;
                    for(const auto &[k, v] : player->_runtime->timelines) {
                        tlInfo += k + "(p=" + std::to_string(v.playing)
                               + ",t=" + std::to_string(v.currentTime)
                               + "/" + std::to_string(v.totalFrames)
                               + ",loop=" + std::to_string(v.loop) + ") ";
                    }
                    std::string motPath = player->_runtime->activeMotion
                        ? player->_runtime->activeMotion->path : "null";
                    EM_ASM({ console.warn('[SelfDrive] player=0x' + ($0 >>> 0).toString(16) + ' allplaying=' + $1 + ' path=' + UTF8ToString($2) + ' tl=' + UTF8ToString($3)); },
                           (int)(uintptr_t)player, (int)player->_allplaying, motPath.c_str(), tlInfo.c_str());
                }
            }
#endif
            if(!player->_allplaying) {
                // Animation finished — set _playing=0 directly on
                // AffineSourceMotion so canWaitMovie() returns 0.
                if(!notifiedStop) {
                    notifiedStop = true;
                    auto *as = affine();
                    if(as) {
                        try {
                            tTJSVariant zero((tjs_int)0);
                            as->PropSet(0, TJS_W("_playing"),
                                nullptr, &zero, as);
                            as->FuncCall(0, TJS_W("onMovieStop"),
                                nullptr, nullptr, 0, nullptr, as);
                        } catch(...) {}
                    }
                }
                return TJS_S_OK;
            }
            // Compute real time delta from tick parameter (ms)
            tjs_int64 tick = (numparams > 0 && param[0])
                ? param[0]->AsInteger() : 0;
            double deltaMs = 0.0;
            if(lastTick > 0 && tick > lastTick) {
                deltaMs = static_cast<double>(tick - lastTick);
            }
            lastTick = tick;
            if(deltaMs <= 0.0 || deltaMs > 200.0) {
                deltaMs = 16.0; // cap at ~60fps
            }
            constexpr double kFramesPerMs = 60.0 / 1000.0;
            const double deltaFrames = deltaMs * kFramesPerMs;
            player->frameProgress(deltaFrames * player->_speed);
            // Trigger repaint by setting redrawFlag on AffineSourceMotion.
            // Do NOT call onAction — that's dispatched by libkrkr2.so's
            // internal animation system with specific parameters we can't
            // replicate here.
            auto *as = affine();
            if(as) {
                try {
                    tTJSVariant flagVal((tjs_int)1);
                    as->PropSet(0, TJS_W("redrawFlag"),
                        nullptr, &flagVal, as);
                } catch(...) {}
            }
            return TJS_S_OK;
        }
    };

    void Player::startSelfDrive(iTJSDispatch2 *objthis) {
        if(_selfDriving) return;
#ifdef __EMSCRIPTEN__
        EM_ASM({ console.warn('[startSelfDrive] player=0x' + ($0 >>> 0).toString(16) + ' objthis=' + $1); },
               (int)(uintptr_t)this, (int)(objthis != nullptr));
#endif
        _selfDriving = true;
        _selfDriveObjThis = objthis;
        auto *handler = new SelfDriveContinuousHandler();
        handler->player = this;
        handler->ownerObj = tTJSVariant(objthis, objthis); // AddRef
        // Resolve AffineSourceMotion from Player's onAction closure
        {
            tTJSVariant onAction;
            if(TJS_SUCCEEDED(objthis->PropGet(0, TJS_W("onAction"),
                   nullptr, &onAction, objthis)) &&
               onAction.Type() == tvtObject) {
                auto clo = onAction.AsObjectClosureNoAddRef();
                iTJSDispatch2 *as =
                    clo.ObjThis ? clo.ObjThis : clo.Object;
                if(as) {
                    handler->affineSourceObj = tTJSVariant(as, as); // AddRef
                }
            }
        }
        // Store handler ref so we can remove it in stopSelfDrive
        _selfDriveHandler = tTJSVariant(handler, nullptr);
        tTJSVariantClosure clo(handler, nullptr);
        TVPAddContinuousHandler(clo);
        handler->Release(); // closure holds a ref
        // Set _playing=1 on AffineSourceMotion and override
        // canWaitMovie to return _playing for motion type too.
        auto *affineSource = handler->affine();
#ifdef __EMSCRIPTEN__
        if(affineSource) {
            tTJSVariant stVal;
            affineSource->PropGet(0, TJS_W("_storageType"), nullptr, &stVal, affineSource);
            auto stStr = ttstr(stVal).AsStdString();
            tTJSVariant cwmVal2;
            affineSource->PropGet(0, TJS_W("canWaitMovie"), nullptr, &cwmVal2, affineSource);
            int cwmType = cwmVal2.Type();
            // Call original canWaitMovie before override
            tTJSVariant cwmResult2;
            if(cwmVal2.Type() == tvtObject) {
                try { cwmVal2.AsObjectClosureNoAddRef().FuncCall(0, nullptr, nullptr, &cwmResult2, 0, nullptr, affineSource); } catch(...) {}
            }
            EM_ASM({ console.warn('[startSelfDrive] affineSource=1 _storageType=' + UTF8ToString($0) + ' canWaitMovie(before)=' + $1); },
                   stStr.c_str(), (int)cwmResult2.AsInteger());
        } else {
            EM_ASM({ console.warn('[startSelfDrive] affineSource=0'); });
        }
#endif
        if(affineSource) {
            tTJSVariant val((tjs_int)1);
            affineSource->PropSet(0, TJS_W("_playing"),
                                  nullptr, &val, affineSource);
            try {
                TVPExecuteExpression(
                    TJS_W("(function(obj){"
                          "obj.canWaitMovie = function(){"
                          "return this._playing;};})")
                    , &val);
                if(val.Type() == tvtObject) {
                    tTJSVariant asVar(affineSource, affineSource);
                    tTJSVariant *args[] = { &asVar };
                    val.AsObjectClosureNoAddRef().FuncCall(
                        0, nullptr, nullptr, nullptr, 1, args,
                        nullptr);
                }
            } catch(...) {}
#ifdef __EMSCRIPTEN__
            // Verify canWaitMovie override worked
            tTJSVariant cwmVal;
            if(TJS_SUCCEEDED(affineSource->PropGet(0, TJS_W("canWaitMovie"),
                             nullptr, &cwmVal, affineSource))) {
                // Call canWaitMovie() to get its return value
                tTJSVariant cwmResult;
                if(cwmVal.Type() == tvtObject) {
                    try {
                        cwmVal.AsObjectClosureNoAddRef().FuncCall(
                            0, nullptr, nullptr, &cwmResult, 0, nullptr,
                            affineSource);
                    } catch(...) {}
                }
                EM_ASM({ console.warn('[canWaitMovie-check] type=' + $0 + ' result=' + $1 + ' _playing=' + $2); },
                       (int)cwmVal.Type(), (int)cwmResult.AsInteger(),
                       0);
            } else {
                EM_ASM({ console.warn('[canWaitMovie-check] PropGet FAILED'); });
            }
#endif
        }
        LOGGER->info("Motion.Player self-drive started");
    }

    void Player::stopSelfDrive() {
        if(!_selfDriving) return;
        _selfDriving = false;
        // Remove the continuous handler and disable it
        if(_selfDriveHandler.Type() == tvtObject &&
           _selfDriveHandler.AsObjectNoAddRef()) {
            auto *handler = static_cast<SelfDriveContinuousHandler *>(
                static_cast<tTJSDispatch *>(
                    _selfDriveHandler.AsObjectNoAddRef()));
            handler->disabled = true;
            handler->player = nullptr;
            tTJSVariantClosure clo(handler, nullptr);
            TVPRemoveContinuousHandler(clo);
        }
        _selfDriveHandler.Clear();
        LOGGER->info("Motion.Player self-drive stopped");
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
        EM_ASM({ console.warn('[_allplaying] playTimeline=' + $0 + ' ptr=0x' + ($1 >>> 0).toString(16)); },
               (int)_allplaying, (int)(uintptr_t)this);
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
        EM_ASM({ console.warn('[_allplaying] playTimeline2=' + $0 + ' ptr=0x' + ($1 >>> 0).toString(16)); },
               (int)_allplaying, (int)(uintptr_t)this);
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

        const bool logoLike = nativeInstance->_runtime->activeMotion &&
            isLogoMotionLike(nativeInstance->_runtime->activeMotion->path);
        if(logoLike) {
            tTJSNI_BaseLayer *layer = nullptr;
            const bool hasLayer = numparams > 0 && param[0] &&
                tryGetLayerObject(*param[0], layer);
            const bool hasOwner = numparams > 0 && param[0] &&
                param[0]->Type() == tvtObject && param[0]->AsObjectNoAddRef() != nullptr &&
                tryResolveSeparateAdaptorOwner(*param[0]) != nullptr;
            LOGGER->warn(
                "Motion logo captureCanvasCompat: path={} hasParam={} hasLayer={} hasOwner={}",
                nativeInstance->_runtime->activeMotion->path, numparams > 0 && param[0],
                hasLayer, hasOwner);
        }

        if(numparams > 0 && param[0] && param[0]->Type() == tvtObject &&
           param[0]->AsObjectNoAddRef() != nullptr) {
            tTJSNI_BaseLayer *targetLayer = nullptr;
            bool isLayer = tryGetLayerObject(*param[0], targetLayer);
            if(logoLike) {
                LOGGER->warn("captureCanvasCompat: param isLayer={} w={} h={}",
                             isLayer,
                             targetLayer ? static_cast<int>(targetLayer->GetWidth()) : -1,
                             targetLayer ? static_cast<int>(targetLayer->GetHeight()) : -1);
            }
            if(nativeInstance->renderToLayer(param[0]->AsObjectNoAddRef())) {
                if(logoLike) {
                    LOGGER->warn("captureCanvasCompat: renderToLayer OK");
                }
                if(result) {
                    *result = *param[0];
                }
                return TJS_S_OK;
            }
            if(logoLike) {
                LOGGER->warn("captureCanvasCompat: renderToLayer FAILED");
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

#ifdef __EMSCRIPTEN__
        {
            static int drawCount = 0;
            if(drawCount++ < 30) {
                EM_ASM({ console.warn('[drawCompat] d3d=' + $0 + ' activeMotion=' + $1 + ' allplaying=' + $2 + ' numparams=' + $3); },
                       (int)nativeInstance->_d3dDrawMode,
                       nativeInstance->_runtime->activeMotion ? 1 : 0,
                       (int)nativeInstance->_allplaying,
                       (int)numparams);
            }
        }
#endif

        if(numparams < 1 || !param[0] || param[0]->Type() != tvtObject ||
           !param[0]->AsObjectNoAddRef()) {
            return TJS_S_OK;
        }

        iTJSDispatch2 *paramObj = param[0]->AsObjectNoAddRef();
        const bool logoLike = nativeInstance->_runtime->activeMotion &&
            isLogoMotionLike(nativeInstance->_runtime->activeMotion->path);

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
        // When TJS calls _player.draw(sla), the SLA path must render to a
        // visible Layer and trigger display update. In libkrkr2.so, this is
        // handled by Player_DrawSLA_guess which resolves internal targets.
        // We render to the motionWorkLayer (full-screen, in display tree).
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
                // libkrkr2.so CPU path (sub_6C9CA8): renders motion directly
                // onto the SLA's owner Layer by calling TJS methods on it
                // (affineCopy, setSize, setPos, visible=1, assignImages).
                // Get the motionWorkLayer from the window — a full-screen
                // Layer in the display tree.
                // TJS path: kag.motionWorkLayer (or _window.motionWorkLayer)
                iTJSDispatch2 *targetLayer = nullptr;
                {
                    // Get the window object from the first KAG window
                    tjs_int winCount = TVPGetWindowCount();
                    for(tjs_int wi = 0; wi < winCount && !targetLayer; wi++) {
                        auto *win = TVPGetWindowListAt(wi);
                        if(!win) continue;
                        iTJSDispatch2 *winObj = win->GetOwnerNoAddRef();
                        if(!winObj) continue;
                        // Try to get motionWorkLayer from window
                        tTJSVariant mwlVar;
                        if(TJS_SUCCEEDED(winObj->PropGet(0, TJS_W("motionWorkLayer"),
                                         nullptr, &mwlVar, winObj)) &&
                           mwlVar.Type() == tvtObject) {
                            targetLayer = mwlVar.AsObjectNoAddRef();
                        }
                    }
                }
                if(targetLayer) {
                    nativeInstance->renderToLayer(targetLayer);
                    if(result) *result = tTJSVariant(targetLayer, targetLayer);
                    return TJS_S_OK;
                }
            }
        }

        // Step 3: param is a Layer (or resolves to one)
        tTJSNI_BaseLayer *layer = nullptr;
        if(tryGetLayerObject(*param[0], layer)) {
            if(nativeInstance->_d3dDrawMode) {
                // D3D mode: render via renderToLayer (which does motion
                // rendering via LayerAPI), same as libkrkr2.so's D3D path
                // that calls sub_6ADE24 + sub_6AD92C.
                if(logoLike) {
                    LOGGER->warn("drawCompat: Layer + _d3dDrawMode → renderToLayer");
                }
                nativeInstance->renderToLayer(paramObj);
            } else {
                nativeInstance->renderToLayer(paramObj);
            }
            if(result) *result = *param[0];
            return TJS_S_OK;
        }

        // Step 4: param resolves to a Layer via property chain
        {
            iTJSDispatch2 *resolved = tryResolveSeparateAdaptorOwner(*param[0]);
            if(resolved) {
                static int s4cnt = 0;
                if(s4cnt < 3) { LOGGER->warn("drawCompat Step4: resolved via property chain"); s4cnt++; }
                nativeInstance->renderToLayer(resolved);
                if(result) *result = tTJSVariant(resolved, resolved);
                return TJS_S_OK;
            }
        }

        // Fallback
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

#ifdef __EMSCRIPTEN__
        EM_ASM({ console.warn('[playCompat] player=0x' + ($0 >>> 0).toString(16) + ' motionKey=' + UTF8ToString($1) + ' label=' + UTF8ToString($2) + ' allplaying=' + $3 + ' activeMotion=' + $4 + ' timelinesCount=' + $5); },
               (int)(uintptr_t)self,
               self->_motionKey.AsStdString().c_str(),
               detail::narrow(label).c_str(),
               (int)self->_allplaying,
               self->_runtime->activeMotion ? 1 : 0,
               (int)self->_runtime->timelines.size());
#endif

        const bool logoLike =
            isLogoMotionLike(detail::narrow(label)) || isLogoMotionLike(detail::narrow(self->_motionKey));
        if(logoLike) {
            LOGGER->warn(
                "Motion logo playCompat entry: label={} motionKey={} activeMotion={} lastLoadedType={}",
                label.AsStdString(), self->_motionKey.AsStdString(),
                self->_runtime->activeMotion ? self->_runtime->activeMotion->path : std::string{},
                static_cast<int>(self->_resourceManagerNative.getLastLoadedModule().Type()));
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
#ifdef __EMSCRIPTEN__
            if(isLogoMotionLike(timelineLabel) || (self->_runtime->activeMotion && isLogoMotionLike(self->_runtime->activeMotion->path))) {
                EM_ASM({ console.warn('[playOne] label=' + UTF8ToString($0) + ' BEFORE: playing=' + $1 + ' currentTime=' + $2 + ' totalFrames=' + $3); },
                       timelineLabel.c_str(), (int)state.playing, state.currentTime, state.totalFrames);
            }
#endif
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

        const auto loweredMotionPath = self->_runtime->activeMotion
            ? [] (std::string value) {
                  std::transform(value.begin(), value.end(), value.begin(),
                                 [](unsigned char ch) {
                                     return static_cast<char>(std::tolower(ch));
                                 });
                  return value;
              }(self->_runtime->activeMotion->path)
            : std::string{};
        if(self->_runtime->activeMotion &&
           (loweredMotionPath.find("yuzulogo.mtn") != std::string::npos ||
            loweredMotionPath.find("m2logo.mtn") != std::string::npos)) {
            LOGGER->warn(
                "Motion logo play: path={} label={} started={} mainLabels={} selectedClipLayers={}",
                self->_runtime->activeMotion->path, label.AsStdString(), started,
                self->_runtime->activeMotion->mainTimelineLabels.size(),
                self->activeLayerNames().size());
        }

        self->_allplaying = std::any_of(
            self->_runtime->timelines.begin(), self->_runtime->timelines.end(),
            [](const auto &entry) { return entry.second.playing; });
        EM_ASM({ console.warn('[_allplaying] progressCompat=' + $0 + ' ptr=0x' + ($1 >>> 0).toString(16)); },
               (int)self->_allplaying, (int)(uintptr_t)self);

        // Start self-driving animation loop for non-D3D web builds.
        // Stop any existing handler first to avoid having two handlers
        // for the same Player (the old one would see stale timeline
        // states and report allplaying=0, triggering premature onMovieStop).
        if(self->_allplaying) {
            self->stopSelfDrive();
            self->startSelfDrive(objthis);
        }

#ifdef __EMSCRIPTEN__
        {
            // Log final state after play
            std::string tlInfo;
            for(const auto &[k, v] : self->_runtime->timelines) {
                tlInfo += k + "(playing=" + std::to_string(v.playing)
                       + ",totalFrames=" + std::to_string(v.totalFrames)
                       + ",loop=" + std::to_string(v.loop) + ") ";
            }
            EM_ASM({ console.warn('[playCompat] EXIT: started=' + $0 + ' allplaying=' + $1 + ' selfDriving=' + $2 + ' timelines=' + UTF8ToString($3)); },
                   (int)started, (int)self->_allplaying, (int)self->_selfDriving, tlInfo.c_str());
        }
#endif

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

#ifdef __EMSCRIPTEN__
        {
            static int progressCount = 0;
            if(progressCount++ < 20) {
                double rawDelta = (numparams > 0 && param[0] && param[0]->Type() != tvtVoid) ? param[0]->AsReal() : -1;
                EM_ASM({ console.warn('[progressCompat] rawDelta=' + $0 + ' allplaying=' + $1 + ' activeMotion=' + $2); },
                       rawDelta, (int)self->_allplaying, self->_runtime->activeMotion ? 1 : 0);
            }
        }
#endif

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

        self->frameProgress(delta * kMotionFramesPerMillisecond * self->_speed);
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
        EM_ASM({ console.warn('[_allplaying] isPlayingCompat=' + $0 + ' ptr=0x' + ($1 >>> 0).toString(16)); },
               (int)self->_allplaying, (int)(uintptr_t)self);
#ifdef __EMSCRIPTEN__
        {
            static int isPlayingCount = 0;
            if(isPlayingCount++ < 20) {
                EM_ASM({ console.warn('[isPlayingCompat] result=' + $0 + ' timelinesCount=' + $1); },
                       (int)playing, (int)self->_runtime->timelines.size());
            }
        }
#endif
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
#ifdef __EMSCRIPTEN__
        if(self->_runtime->activeMotion && isLogoMotionLike(self->_runtime->activeMotion->path)) {
            EM_ASM({ console.warn('[stopCompat] path=' + UTF8ToString($0)); },
                   self->_runtime->activeMotion->path.c_str());
        }
#endif

        ttstr label;
        if(numparams > 0 && param[0] && param[0]->Type() != tvtVoid &&
           param[0]->Type() != tvtInteger && param[0]->Type() != tvtReal) {
            label = *param[0];
        }

        if(label.IsEmpty()) {
            for(auto &[_, state] : self->_runtime->timelines) {
                state.playing = false;
            }
        } else {
            if(const auto it = self->_runtime->timelines.find(detail::narrow(label));
               it != self->_runtime->timelines.end()) {
                it->second.playing = false;
            }
        }

        self->_allplaying = false;
        EM_ASM({ console.warn('[_allplaying] stopCompat=' + $0 + ' ptr=0x' + ($1 >>> 0).toString(16)); },
               (int)self->_allplaying, (int)(uintptr_t)self);
        self->_syncWaiting = false;
        self->_syncActive = false;
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
