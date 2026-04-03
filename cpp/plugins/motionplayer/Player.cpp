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
            double width = 0.0;    // "zx" from PSB (source display width)
            double height = 0.0;   // "zy" from PSB (source display height)
            double opacity = 1.0;  // 0.0-1.0 (from PSB "opa" uint8 0-255)
            double angle = 0.0;    // rotation angle in degrees
            bool flipX = false;    // "fx" from PSB content
            bool flipY = false;    // "fy" from PSB content
            std::string action;    // "content.action" from PSB frameList
            bool hasSync = false;  // "content.sync" from PSB frameList
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

        FrameContentState
        evaluateLayerContent(const std::shared_ptr<const PSB::PSBDictionary> &layer,
                             double time) {
            FrameContentState state;
            const auto frames = psbDictionaryList(layer, "frameList");
            if(!frames) {
                return state;
            }

            // Find the active keyframe index (last frame with time <= time).
            int activeIndex = -1;
            for(size_t index = 0; index < frames->size(); ++index) {
                const auto frame = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                    (*frames)[static_cast<int>(index)]);
                if(!frame) continue;
                const auto frameTime =
                    psbDictionaryNumber(frame, "time").value_or(0.0);
                if(frameTime > time) break;
                activeIndex = static_cast<int>(index);
            }

            if(activeIndex < 0) return state;

            // Evaluate all frames up to activeIndex (PSB frames are cumulative).
            for(int i = 0; i <= activeIndex; ++i) {
                const auto frame = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                    (*frames)[i]);
                if(!frame) continue;
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

            // Interpolate with next keyframe if available.
            // Aligned to libkrkr2.so sub_699AE4 (0x699AE4):
            // Linear interpolation between two clip states with ratio t.
            const int nextIndex = activeIndex + 1;
            if(nextIndex < static_cast<int>(frames->size()) && state.visible) {
                const auto curFrame = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                    (*frames)[activeIndex]);
                const auto nextFrame = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                    (*frames)[nextIndex]);
                if(curFrame && nextFrame) {
                    const double curTime =
                        psbDictionaryNumber(curFrame, "time").value_or(0.0);
                    const double nextTime =
                        psbDictionaryNumber(nextFrame, "time").value_or(0.0);
                    const double duration = nextTime - curTime;
                    if(duration > 0.0) {
                        const double t = std::clamp(
                            (time - curTime) / duration, 0.0, 1.0);
                        if(t > 0.0) {
                            // Aligned to libkrkr2.so sub_692AB0 (0x692AB0):
                            // Each clip slot is initialized with DEFAULTS
                            // before mask-gated properties are applied:
                            //   opacity = 255, color = 0xFF808080, blendMode = 16
                            // NOT copied from the current slot's values.
                            // This means: if the next keyframe's mask doesn't
                            // include 0x400 (opa), its opacity is 255 (default),
                            // NOT the current frame's opa value.
                            FrameContentState nextState;  // fresh defaults
                            nextState.src = state.src;    // inherit src
                            const auto nextType = static_cast<int>(
                                psbDictionaryNumber(nextFrame, "type").value_or(0.0));
                            if(nextType != 0) {
                                if(const auto nc = psbDictionaryValue(nextFrame, "content")) {
                                    mergeFrameContent(nc, nextState);
                                }
                                auto lerp = [](double a, double b, double r) {
                                    return a * (1.0 - r) + b * r;
                                };
                                state.x = lerp(state.x, nextState.x, t);
                                state.y = lerp(state.y, nextState.y, t);
                                state.ox = lerp(state.ox, nextState.ox, t);
                                state.oy = lerp(state.oy, nextState.oy, t);
                                state.opacity = lerp(state.opacity, nextState.opacity, t);
                                // Angle interpolation with 360° wrap-around.
                                // Aligned to sub_699AE4 at 0x699D94:
                                //   if curAngle >= nextAngle && diff > 180: nextAngle += 360
                                //   if curAngle <  nextAngle && diff > 180: nextAngle -= 360
                                //   result: if < 0 add 360, if >= 360 subtract 360
                                double curAngle = state.angle;
                                double nxtAngle = nextState.angle;
                                if(curAngle != nxtAngle) {
                                    if(curAngle >= nxtAngle) {
                                        if(curAngle - nxtAngle > 180.0) nxtAngle += 360.0;
                                    } else {
                                        if(nxtAngle - curAngle > 180.0) nxtAngle -= 360.0;
                                    }
                                    double interpAngle = lerp(curAngle, nxtAngle, t);
                                    if(interpAngle < 0.0) interpAngle += 360.0;
                                    else if(interpAngle >= 360.0) interpAngle -= 360.0;
                                    state.angle = interpAngle;
                                }
                                if(nextState.width > 0.0 && state.width > 0.0)
                                    state.width = lerp(state.width, nextState.width, t);
                                if(nextState.height > 0.0 && state.height > 0.0)
                                    state.height = lerp(state.height, nextState.height, t);
                            }
                        }
                    }
                }
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
                                 bool flipX, bool flipY,
                                 double angle) {
            // Build local 2x2 from identity via left-multiplication
            double l11 = 1.0, l12 = 0.0, l21 = 0.0, l22 = 1.0;

            // Case 0: Flip (left-multiply flip matrix)
            if(flipX) { l11 = -l11; l12 = -l12; }
            if(flipY) { l21 = -l21; l22 = -l22; }

            // Case 1: Angle (left-multiply rotation)
            if(angle != 0.0) {
                const double rad = angle * 2.0 * 3.14159265358979323846 / 360.0;
                const double c = std::cos(rad);
                const double s = std::sin(rad);
                // R × L where R = [c,-s; s,c]
                const double t11 = c*l11 - s*l21;
                const double t12 = c*l12 - s*l22;
                const double t21 = s*l11 + c*l21;
                const double t22 = s*l12 + c*l22;
                l11 = t11; l12 = t12; l21 = t21; l22 = t22;
            }

            // Case 2: Zoom — requires clip-level scaleX/scaleY (not yet parsed)
            // Case 3: Slant — requires clip-level slantX/slantY (not yet parsed)

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
            applyLocalTransform(curAffine, state.flipX, state.flipY,
                                state.angle);

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
                    // Step 1 (sub_6C4E28): Flatten PSB layer tree into
                    // a flat list with pre-computed positions.
                    std::vector<FlatRenderNode> renderNodes;
                    for(const auto &layerName : layerNamesList) {
                        const auto *layers = activeLayersByName();
                        if(!layers) break;
                        const auto it = layers->find(layerName);
                        if(it == layers->end()) continue;
                        flattenLayerNodes(it->second, renderTime,
                                          globalAffine, 1.0,
                                          false, false, renderNodes);
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
                                    const auto refAffine = affineTranslate(
                                        globalAffine, st.x + st.ox, st.y + st.oy);
                                    flattenLayerNodes(
                                        refIt->second, renderTime,
                                        refAffine, 1.0,
                                        false, false, renderNodes);
                                }
                            }
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
