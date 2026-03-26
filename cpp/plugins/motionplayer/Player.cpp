//
// Created by LiDon on 2025/9/15.
// Minimal runtime implementation aligned to libkrkr2.so MMotionPlayer surface.
//

#include "Player.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <optional>
#include <unordered_set>
#include <vector>

#include "LayerIntf.h"
#include "RuntimeSupport.h"
#include "ResourceManager.h"
#include "SeparateLayerAdaptor.h"
#include "StorageIntf.h"
#include "ncbind.hpp"
#include "tjsArray.h"

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
                TJS_W("layer"), nullptr };
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

#ifndef KRKR2_NO_OPENCV
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

        struct LoadedSourceImage {
            bool attempted = false;
            cv::Mat image;
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

        cv::Matx33d translateMatrix(double x, double y) {
            return cv::Matx33d(1.0, 0.0, x, 0.0, 1.0, y, 0.0, 0.0, 1.0);
        }

        cv::Matx33d scaleMatrix(double x, double y) {
            return cv::Matx33d(x, 0.0, 0.0, 0.0, y, 0.0, 0.0, 0.0, 1.0);
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
            if(!state.visible || state.src.empty() || state.src == "layout") {
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

    } // namespace

    Player::Player(ResourceManager rm) :
        _runtime(detail::makePlayerRuntime()),
        _resourceManagerNative(std::move(rm)) {
        LOGGER->info("Motion.Player constructor called");
    }

    Player::~Player() = default;

    void Player::setMotion(ttstr v) {
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

    bool Player::renderToLayer(iTJSDispatch2 *layerObject) {
        if(!layerObject) {
            return false;
        }

        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return false;
        }

        tTJSNI_BaseLayer *layer = nullptr;
        if(TJS_FAILED(layerObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&layer))) ||
           !layer) {
            // layerObject isn't a native Layer—try to find one through
            // TJS property chain (owner/_owner/targetLayer/layer)
            tTJSVariant wrapper(layerObject, layerObject);
            auto *resolved = tryResolveLayerDispatch(wrapper);
            if(resolved && resolved != layerObject) {
                return renderToLayer(resolved);
            }
            return false;
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
                    for(const auto &layerName : activeLayerNames()) {
                        const auto *layers = activeLayersByName();
                        if(!layers) {
                            break;
                        }
                        const auto it = layers->find(layerName);
                        if(it == layers->end()) {
                            continue;
                        }
                        drewAny = renderMotionLayer(
                                      canvas, layer, *_runtime->activeMotion,
                                      sourceCache, it->second, renderTime,
                                      globalTransform) ||
                            drewAny;
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

                            layer->Update(false);
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

            layer->Update(false);
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
        _frameLastTime = dt;
        _frameLoopTime += dt;
        _loopTime += dt;
        _tickCount += dt;
        _frameTickCount += 1.0;
        detail::stepTimelines(_runtime->timelines, dt);

        _allplaying = std::any_of(
            _runtime->timelines.begin(), _runtime->timelines.end(),
            [](const auto &entry) { return entry.second.playing; });
        _syncActive = _syncWaiting && _allplaying;
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

    bool Player::getD3DAvailable() { return _useD3D; }

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
           param[0]->AsObjectNoAddRef() != nullptr &&
           nativeInstance->renderToLayer(param[0]->AsObjectNoAddRef())) {
            if(result) {
                *result = *param[0];
            }
            return TJS_S_OK;
        }

        if(result) {
            *result = nativeInstance->captureCanvas();
        }
        return TJS_S_OK;
    }

    tjs_error Player::drawCompat(tTJSVariant *result, tjs_int numparams,
                                 tTJSVariant **param, Player *nativeInstance) {
        if(result) {
            result->Clear();
        }
        if(!nativeInstance) {
            return TJS_E_INVALIDOBJECT;
        }

        tTJSNI_BaseLayer *layer = nullptr;
        const bool logoLike = nativeInstance->_runtime->activeMotion &&
            isLogoMotionLike(nativeInstance->_runtime->activeMotion->path);
        iTJSDispatch2 *owner = nullptr;
        if(numparams > 0 && param[0] && param[0]->Type() == tvtObject &&
           param[0]->AsObjectNoAddRef() != nullptr) {
            owner = tryResolveSeparateAdaptorOwner(*param[0]);
        }
        if(logoLike) {
            LOGGER->warn(
                "Motion logo drawCompat: path={} hasParam={} hasLayer={} hasOwner={}",
                nativeInstance->_runtime->activeMotion->path, numparams > 0 && param[0],
                numparams > 0 && param[0] && tryGetLayerObject(*param[0], layer),
                owner != nullptr);
            layer = nullptr;
        }
        if(numparams > 0 && param[0] && tryGetLayerObject(*param[0], layer) &&
           nativeInstance->renderToLayer(param[0]->AsObjectNoAddRef())) {
            if(result) {
                *result = *param[0];
            }
            return TJS_S_OK;
        }

        if(numparams > 0 && param[0] && param[0]->Type() == tvtObject &&
           param[0]->AsObjectNoAddRef() != nullptr) {
            if(owner && nativeInstance->renderToLayer(owner)) {
                if(result) {
                    *result = tTJSVariant(owner, owner);
                }
                return TJS_S_OK;
            }
        }

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
            state.label = timelineLabel;
            state.flags = flags;
            state.blendRatio = 1.0;
            state.playing = true;
            state.currentTime = 0.0;
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
