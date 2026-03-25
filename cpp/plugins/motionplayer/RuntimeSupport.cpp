//
// Internal helpers for motionplayer/emoteplayer runtime state.
//

#include "RuntimeSupport.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <mutex>
#include <optional>
#include <unordered_set>

#include <spdlog/spdlog.h>

#include "StorageIntf.h"
#include "tjsArray.h"
#include "tjsDictionary.h"

#define LOGGER spdlog::get("plugin")

namespace motion::detail {

    namespace {

        std::mutex &snapshotRegistryMutex() {
            static std::mutex mutex;
            return mutex;
        }

        std::unordered_map<const iTJSDispatch2 *, std::shared_ptr<MotionSnapshot>>
        &snapshotRegistry() {
            static std::unordered_map<const iTJSDispatch2 *,
                                      std::shared_ptr<MotionSnapshot>>
                registry;
            return registry;
        }

        std::string lowercase(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char ch) {
                               return static_cast<char>(std::tolower(ch));
                           });
            return value;
        }

        bool pathContainsToken(const std::vector<std::string> &path,
                               const std::string &token) {
            const auto loweredToken = lowercase(token);
            return std::any_of(path.begin(), path.end(),
                               [&loweredToken](const std::string &part) {
                                   return lowercase(part).find(loweredToken) !=
                                       std::string::npos;
                               });
        }

        bool hasExtension(const std::string &value) {
            return value.find('.') != std::string::npos;
        }

        bool hasSuffix(const std::string &value, const char *suffix) {
            const auto suffixLen = std::strlen(suffix);
            return value.size() >= suffixLen &&
                value.compare(value.size() - suffixLen, suffixLen, suffix) == 0;
        }

        bool looksLikeStoragePath(const std::string &value) {
            const auto lowered = lowercase(value);
            static const char *exts[] = { ".psb", ".pimg", ".png", ".jpg",
                                          ".jpeg", ".bmp", ".tlg", ".webp" };
            return std::any_of(std::begin(exts), std::end(exts),
                               [&lowered](const char *ext) {
                                   return hasSuffix(lowered, ext);
                               });
        }

        std::optional<std::string>
        psbString(const std::shared_ptr<PSB::IPSBValue> &value) {
            if(auto str = std::dynamic_pointer_cast<PSB::PSBString>(value)) {
                return str->value;
            }
            return std::nullopt;
        }

        std::optional<double>
        psbNumber(const std::shared_ptr<PSB::IPSBValue> &value) {
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

        std::optional<bool>
        psbBool(const std::shared_ptr<PSB::IPSBValue> &value) {
            if(auto boolean = std::dynamic_pointer_cast<PSB::PSBBool>(value)) {
                return boolean->value;
            }
            if(auto number = psbNumber(value)) {
                return *number != 0.0;
            }
            return std::nullopt;
        }

        std::optional<std::string>
        dictionaryString(const std::shared_ptr<const PSB::PSBDictionary> &dic,
                         const std::vector<std::string> &keys) {
            for(const auto &key : keys) {
                if(const auto value = (*dic)[key]) {
                    if(const auto result = psbString(value)) {
                        return result;
                    }
                }
            }
            return std::nullopt;
        }

        std::optional<double>
        dictionaryNumber(const std::shared_ptr<const PSB::PSBDictionary> &dic,
                         const std::vector<std::string> &keys) {
            for(const auto &key : keys) {
                if(const auto value = (*dic)[key]) {
                    if(const auto result = psbNumber(value)) {
                        return result;
                    }
                }
            }
            return std::nullopt;
        }

        std::optional<bool>
        dictionaryBool(const std::shared_ptr<const PSB::PSBDictionary> &dic,
                       const std::vector<std::string> &keys) {
            for(const auto &key : keys) {
                if(const auto value = (*dic)[key]) {
                    if(const auto result = psbBool(value)) {
                        return result;
                    }
                }
            }
            return std::nullopt;
        }

        std::shared_ptr<PSB::PSBList>
        dictionaryList(const std::shared_ptr<const PSB::PSBDictionary> &dic,
                       const std::vector<std::string> &keys) {
            for(const auto &key : keys) {
                if(auto value = std::dynamic_pointer_cast<PSB::PSBList>((*dic)[key])) {
                    return value;
                }
            }
            return nullptr;
        }

        bool dictionaryHasKey(const std::shared_ptr<const PSB::PSBDictionary> &dic,
                              const std::string &key) {
            return (*dic)[key] != nullptr;
        }

        bool dictionaryKeyContains(
            const std::shared_ptr<const PSB::PSBDictionary> &dic,
            const std::string &token) {
            const auto loweredToken = lowercase(token);
            return std::any_of(dic->begin(), dic->end(),
                               [&loweredToken](const auto &entry) {
                                   return lowercase(entry.first)
                                              .find(loweredToken) !=
                                       std::string::npos;
                               });
        }

        void appendUnique(std::vector<std::string> &values,
                          const std::string &value) {
            if(value.empty()) {
                return;
            }
            if(std::find(values.begin(), values.end(), value) == values.end()) {
                values.push_back(value);
            }
        }

        std::string basenameWithoutExtension(const std::string &value) {
            const auto slash = value.find_last_of("/\\");
            const auto fileName =
                slash == std::string::npos ? value : value.substr(slash + 1);
            const auto dot = fileName.find_last_of('.');
            return dot == std::string::npos ? fileName : fileName.substr(0, dot);
        }

        void collectFrameList(const std::string &label,
                              const std::shared_ptr<PSB::PSBList> &list,
                              MotionSnapshot &snapshot) {
            if(!list) {
                return;
            }

            std::vector<VariableFrameInfo> frames;
            int index = 0;
            for(const auto &item : *list) {
                if(auto dic = std::dynamic_pointer_cast<PSB::PSBDictionary>(item)) {
                    const auto frameLabel = dictionaryString(
                        dic, { "label", "name", "id" })
                                                .value_or(std::to_string(index));
                    const auto value = dictionaryNumber(
                        dic, { "value", "val", "frame", "position", "time" })
                                           .value_or(static_cast<double>(index));
                    frames.push_back({ frameLabel, value });
                } else if(const auto value = psbNumber(item)) {
                    frames.push_back({ std::to_string(index), *value });
                }
                ++index;
            }

            if(!frames.empty()) {
                snapshot.variableFrames[label] = std::move(frames);
            }
        }

        void maybeRecordVariable(
            const std::vector<std::string> &path,
            const std::shared_ptr<PSB::PSBDictionary> &dic,
            MotionSnapshot &snapshot) {
            if(!pathContainsToken(path, "variable") &&
               !pathContainsToken(path, "vars")) {
                return;
            }

            const auto label =
                dictionaryString(dic, { "label", "name", "id" });
            if(!label || label->empty()) {
                return;
            }

            appendUnique(snapshot.variableLabels, *label);

            const auto minValue =
                dictionaryNumber(dic, { "min", "minimum", "from" });
            const auto maxValue =
                dictionaryNumber(dic, { "max", "maximum", "to" });
            if(minValue && maxValue) {
                snapshot.variableRanges[*label] = { *minValue, *maxValue };
            }

            collectFrameList(*label,
                             dictionaryList(dic, { "frames", "frameList",
                                                   "frame_list", "values",
                                                   "valueList" }),
                             snapshot);
        }

        void maybeRecordLayer(const std::vector<std::string> &path,
                              const std::shared_ptr<PSB::PSBDictionary> &dic,
                              MotionSnapshot &snapshot) {
            const auto label =
                dictionaryString(dic, { "name", "label", "id" });
            if(!label || label->empty()) {
                return;
            }

            const bool layerLike = pathContainsToken(path, "layer") ||
                dictionaryHasKey(dic, "layer_id") ||
                dictionaryHasKey(dic, "layer_type") ||
                (dictionaryHasKey(dic, "width") &&
                 dictionaryHasKey(dic, "height") &&
                 (dictionaryHasKey(dic, "left") || dictionaryHasKey(dic, "top")));
            if(!layerLike) {
                return;
            }

            if(snapshot.layersByName.find(*label) ==
               snapshot.layersByName.end()) {
                snapshot.layersByName[*label] = dic;
                snapshot.layerNames.push_back(*label);
            }
        }

        void maybeRecordTimeline(const std::vector<std::string> &path,
                                 const std::shared_ptr<PSB::PSBDictionary> &dic,
                                 MotionSnapshot &snapshot) {
            const bool timelineLike = pathContainsToken(path, "timeline") ||
                dictionaryKeyContains(dic, "timeline") ||
                dictionaryHasKey(dic, "loop") ||
                dictionaryHasKey(dic, "frame_count") ||
                dictionaryHasKey(dic, "frameCount");
            if(!timelineLike) {
                return;
            }

            const auto label =
                dictionaryString(dic, { "label", "name", "id" });
            if(!label || label->empty()) {
                return;
            }

            const bool isDiff = pathContainsToken(path, "diff");
            appendUnique(isDiff ? snapshot.diffTimelineLabels
                                : snapshot.mainTimelineLabels,
                         *label);

            snapshot.loopTimelines[*label] =
                dictionaryBool(dic, { "loop", "repeat", "is_loop" }).value_or(
                    false);
            snapshot.timelineTotalFrames[*label] =
                dictionaryNumber(dic, { "frameCount", "frame_count",
                                        "totalFrameCount", "total_frame_count",
                                        "frames", "length", "end" })
                    .value_or(0.0);
        }

        void scanValue(const std::shared_ptr<PSB::IPSBValue> &value,
                       std::vector<std::string> &path,
                       MotionSnapshot &snapshot);

        void scanDictionary(const std::shared_ptr<PSB::PSBDictionary> &dic,
                            std::vector<std::string> &path,
                            MotionSnapshot &snapshot) {
            maybeRecordLayer(path, dic, snapshot);
            maybeRecordTimeline(path, dic, snapshot);
            maybeRecordVariable(path, dic, snapshot);

            if(const auto width = dictionaryNumber(dic, { "width" });
               width && snapshot.width == 0.0) {
                snapshot.width = *width;
            }
            if(const auto height = dictionaryNumber(dic, { "height" });
               height && snapshot.height == 0.0) {
                snapshot.height = *height;
            }

            for(const auto &[key, child] : *dic) {
                const auto loweredKey = lowercase(key);
                if(const auto text = psbString(child)) {
                    if(looksLikeStoragePath(*text) ||
                       loweredKey.find("source") != std::string::npos ||
                       loweredKey == "path" || loweredKey == "file" ||
                       loweredKey == "src") {
                        appendUnique(snapshot.sourceCandidates, *text);
                    }
                }

                path.push_back(key);
                scanValue(child, path, snapshot);
                path.pop_back();
            }
        }

        void scanList(const std::shared_ptr<PSB::PSBList> &list,
                      std::vector<std::string> &path, MotionSnapshot &snapshot) {
            for(size_t index = 0; index < list->size(); ++index) {
                path.push_back(std::to_string(index));
                scanValue((*list)[static_cast<int>(index)], path, snapshot);
                path.pop_back();
            }
        }

        void scanValue(const std::shared_ptr<PSB::IPSBValue> &value,
                       std::vector<std::string> &path,
                       MotionSnapshot &snapshot) {
            if(auto dic = std::dynamic_pointer_cast<PSB::PSBDictionary>(value)) {
                scanDictionary(dic, path, snapshot);
            } else if(auto list = std::dynamic_pointer_cast<PSB::PSBList>(value)) {
                scanList(list, path, snapshot);
            } else if(const auto text = psbString(value)) {
                if(looksLikeStoragePath(*text)) {
                    appendUnique(snapshot.sourceCandidates, *text);
                }
            }
        }

        std::shared_ptr<PSB::PSBFile> loadPSBFile(const ttstr &path,
                                                  const tjs_int decryptSeed) {
            auto file = std::make_shared<PSB::PSBFile>();
            file->setSeed(decryptSeed);
            if(!file->loadPSBFile(path)) {
                LOGGER->error("motion load file: {} failed", path.AsStdString());
                return nullptr;
            }
            return file;
        }

    } // namespace

    std::shared_ptr<PlayerRuntime> makePlayerRuntime() {
        return std::make_shared<PlayerRuntime>();
    }

    std::shared_ptr<EmotePlayerRuntime> makeEmotePlayerRuntime() {
        return std::make_shared<EmotePlayerRuntime>();
    }

    std::string narrow(const ttstr &value) { return value.AsStdString(); }

    ttstr widen(const std::string &value) { return ttstr{ value }; }

    std::vector<ttstr> buildMotionLookupCandidates(const ttstr &name) {
        std::vector<ttstr> candidates;
        if(name.IsEmpty()) {
            return candidates;
        }

        const auto raw = narrow(name);
        const bool hasPathSeparator =
            raw.find('/') != std::string::npos || raw.find('\\') != std::string::npos;
        const bool hasKnownExtension = hasExtension(raw);
        if(hasPathSeparator || hasKnownExtension) {
            candidates.push_back(name);
        } else {
            candidates.emplace_back(ttstr{ raw + ".mtn" });
            candidates.emplace_back(ttstr{ raw + ".psb" });
            candidates.emplace_back(ttstr{ "motion/" + raw + ".mtn" });
            candidates.emplace_back(ttstr{ "motion/" + raw + ".psb" });
        }

        return candidates;
    }

    bool resolveExistingPath(const std::vector<ttstr> &candidates,
                             ttstr &resolved) {
        for(const auto &candidate : candidates) {
            if(TVPIsExistentStorageNoSearch(candidate)) {
                resolved = candidate;
                return true;
            }
        }
        return false;
    }

    std::shared_ptr<MotionSnapshot> loadMotionSnapshot(const ttstr &path,
                                                       const tjs_int decryptSeed) {
        const auto file = loadPSBFile(path, decryptSeed);
        if(!file) {
            return nullptr;
        }
        if(file->getType() != PSB::PSBType::Motion) {
            LOGGER->error("this psb file is not motion file: {}",
                          path.AsStdString());
            return nullptr;
        }

        const auto root = file->getObjects();
        if(!root) {
            return nullptr;
        }

        auto snapshot = std::make_shared<MotionSnapshot>();
        snapshot->path = narrow(path);
        snapshot->file = file;
        snapshot->root = root;
        snapshot->moduleValue = root->toTJSVal();

        std::vector<std::string> pathParts;
        scanValue(std::const_pointer_cast<PSB::PSBDictionary>(root), pathParts,
                  *snapshot);
        registerModuleSnapshot(snapshot->moduleValue, snapshot);
        return snapshot;
    }

    tTJSVariant loadPSBVariant(const ttstr &path, const tjs_int decryptSeed) {
        if(const auto snapshot = loadMotionSnapshot(path, decryptSeed)) {
            return snapshot->moduleValue;
        }

        const auto file = loadPSBFile(path, decryptSeed);
        if(!file || !file->getObjects()) {
            return {};
        }

        return file->getObjects()->toTJSVal();
    }

    void registerModuleSnapshot(const tTJSVariant &module,
                                const std::shared_ptr<MotionSnapshot> &snapshot) {
        if(module.Type() != tvtObject || module.AsObjectNoAddRef() == nullptr ||
           !snapshot) {
            return;
        }

        std::lock_guard lock(snapshotRegistryMutex());
        snapshotRegistry()[module.AsObjectNoAddRef()] = snapshot;
    }

    std::shared_ptr<MotionSnapshot> lookupModuleSnapshot(const tTJSVariant &module) {
        if(module.Type() != tvtObject || module.AsObjectNoAddRef() == nullptr) {
            return nullptr;
        }

        std::lock_guard lock(snapshotRegistryMutex());
        const auto it = snapshotRegistry().find(module.AsObjectNoAddRef());
        return it != snapshotRegistry().end() ? it->second : nullptr;
    }

    tTJSVariant makeArray(const std::vector<tTJSVariant> &items) {
        iTJSDispatch2 *array = TJSCreateArrayObject();
        static tjs_uint addHint = 0;
        for(const auto &item : items) {
            tTJSVariant value = item;
            tTJSVariant *args[] = { &value };
            array->FuncCall(0, TJS_W("add"), &addHint, nullptr, 1, args, array);
        }
        tTJSVariant result(array, array);
        array->Release();
        return result;
    }

    tTJSVariant makeDictionary(
        const std::vector<std::pair<std::string, tTJSVariant>> &entries) {
        iTJSDispatch2 *dic = TJSCreateDictionaryObject();
        for(const auto &[key, value] : entries) {
            tTJSVariant tmp = value;
            dic->PropSet(TJS_MEMBERENSURE, widen(key).c_str(), nullptr, &tmp,
                         dic);
        }
        tTJSVariant result(dic, dic);
        dic->Release();
        return result;
    }

    std::vector<tTJSVariant>
    stringsToVariants(const std::vector<std::string> &values) {
        std::vector<tTJSVariant> result;
        result.reserve(values.size());
        for(const auto &value : values) {
            result.emplace_back(widen(value));
        }
        return result;
    }

    void primeTimelineStates(std::unordered_map<std::string, TimelineState> &states,
                             const MotionSnapshot &snapshot) {
        const auto primeOne = [&](const std::string &label) {
            auto &state = states[label];
            state.label = label;
            state.loop =
                snapshot.loopTimelines.find(label) != snapshot.loopTimelines.end()
                ? snapshot.loopTimelines.at(label)
                : false;
            state.totalFrames =
                snapshot.timelineTotalFrames.find(label) !=
                    snapshot.timelineTotalFrames.end()
                ? snapshot.timelineTotalFrames.at(label)
                : 0.0;
        };

        for(const auto &label : snapshot.mainTimelineLabels) {
            primeOne(label);
        }
        for(const auto &label : snapshot.diffTimelineLabels) {
            primeOne(label);
        }
    }

    void stepTimelines(std::unordered_map<std::string, TimelineState> &states,
                       const double dt) {
        if(dt <= 0.0) {
            return;
        }

        for(auto &[_, state] : states) {
            if(!state.playing) {
                continue;
            }

            state.currentTime += dt;
            if(state.totalFrames <= 0.0) {
                continue;
            }

            if(state.currentTime < state.totalFrames) {
                continue;
            }

            if(state.loop) {
                state.currentTime = std::fmod(state.currentTime, state.totalFrames);
            } else {
                state.currentTime = state.totalFrames;
                state.playing = false;
            }
        }
    }

} // namespace motion::detail
