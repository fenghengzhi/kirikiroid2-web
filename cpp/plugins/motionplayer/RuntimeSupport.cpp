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
#include "psbfile/PSBMediaRegistry.h"
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

        void collectValueSources(const std::shared_ptr<PSB::IPSBValue> &value,
                                 std::vector<std::string> &sources);

        void collectDictionarySources(
            const std::shared_ptr<PSB::PSBDictionary> &dic,
            std::vector<std::string> &sources) {
            for(const auto &[key, child] : *dic) {
                const auto loweredKey = lowercase(key);
                if(const auto text = psbString(child)) {
                    if(looksLikeStoragePath(*text) ||
                       loweredKey.find("source") != std::string::npos ||
                       loweredKey == "path" || loweredKey == "file" ||
                       loweredKey == "src") {
                        appendUnique(sources, *text);
                    }
                }
                collectValueSources(child, sources);
            }
        }

        void collectListSources(const std::shared_ptr<PSB::PSBList> &list,
                                std::vector<std::string> &sources) {
            for(const auto &item : *list) {
                collectValueSources(item, sources);
            }
        }

        void collectValueSources(const std::shared_ptr<PSB::IPSBValue> &value,
                                 std::vector<std::string> &sources) {
            if(auto dic = std::dynamic_pointer_cast<PSB::PSBDictionary>(value)) {
                collectDictionarySources(dic, sources);
            } else if(auto list = std::dynamic_pointer_cast<PSB::PSBList>(value)) {
                collectListSources(list, sources);
            } else if(const auto text = psbString(value)) {
                if(looksLikeStoragePath(*text)) {
                    appendUnique(sources, *text);
                }
            }
        }

        void maybeRecordMotionClip(const std::vector<std::string> &path,
                                   const std::shared_ptr<PSB::PSBDictionary> &dic,
                                   MotionSnapshot &snapshot) {
            if(path.size() < 4 ||
               lowercase(path[path.size() - 2]) != "motion" ||
               lowercase(path[path.size() - 4]) != "object") {
                return;
            }

            const auto label = path.back();
            if(label.empty()) {
                return;
            }

            auto &clip = snapshot.clipsByLabel[label];
            clip.label = label;
            clip.owner = path[path.size() - 3];
            clip.totalFrames =
                dictionaryNumber(dic, { "lastTime", "frameCount", "frame_count",
                                        "totalFrameCount", "total_frame_count",
                                        "frames", "length", "end" })
                    .value_or(0.0);
            if(const auto loopTime = dictionaryNumber(dic, { "loopTime" })) {
                clip.loopTime = *loopTime;
                clip.loop = *loopTime >= 0.0;
            } else if(const auto loop = dictionaryBool(dic, { "loop", "repeat", "is_loop" })) {
                clip.loop = *loop;
                clip.loopTime = *loop ? 0.0 : -1.0;
            }

            if(const auto layers = dictionaryList(dic, { "layer" })) {
                for(const auto &item : *layers) {
                    const auto layer =
                        std::dynamic_pointer_cast<PSB::PSBDictionary>(item);
                    if(!layer) {
                        continue;
                    }

                    const auto layerLabel =
                        dictionaryString(layer, { "label", "name", "id" });
                    if(!layerLabel || layerLabel->empty()) {
                        continue;
                    }

                    if(clip.layersByName.find(*layerLabel) ==
                       clip.layersByName.end()) {
                        clip.layersByName[*layerLabel] = layer;
                        clip.layerNames.push_back(*layerLabel);
                    }
                    collectValueSources(layer, clip.sourceCandidates);
                }
            }

            collectValueSources(dic, clip.sourceCandidates);

            appendUnique(snapshot.mainTimelineLabels, clip.label);
            snapshot.loopTimelines[clip.label] = clip.loop;
            snapshot.timelineLoopTimes[clip.label] = clip.loopTime;
            snapshot.timelineTotalFrames[clip.label] = clip.totalFrames;
        }

        void scanValue(const std::shared_ptr<PSB::IPSBValue> &value,
                       std::vector<std::string> &path,
                       MotionSnapshot &snapshot);

        void scanDictionary(const std::shared_ptr<PSB::PSBDictionary> &dic,
                            std::vector<std::string> &path,
                            MotionSnapshot &snapshot) {
            maybeRecordMotionClip(path, dic, snapshot);
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

        bool looksLikeEmbeddedSourceKey(const std::string &value) {
            return looksLikeStoragePath(value) ||
                value.find('/') != std::string::npos ||
                value.find('\\') != std::string::npos;
        }

        void collectResourceMap(const std::shared_ptr<PSB::IPSBValue> &value,
                                std::vector<std::string> &path,
                                MotionSnapshot &snapshot);

        void collectDictionaryResourceMap(
            const std::shared_ptr<PSB::PSBDictionary> &dic,
            std::vector<std::string> &path, MotionSnapshot &snapshot) {
            for(const auto &[key, child] : *dic) {
                path.push_back(key);
                collectResourceMap(child, path, snapshot);
                path.pop_back();
            }
        }

        void collectListResourceMap(const std::shared_ptr<PSB::PSBList> &list,
                                    std::vector<std::string> &path,
                                    MotionSnapshot &snapshot) {
            for(size_t index = 0; index < list->size(); ++index) {
                path.push_back(std::to_string(index));
                collectResourceMap((*list)[static_cast<int>(index)], path,
                                   snapshot);
                path.pop_back();
            }
        }

        void collectResourceMap(const std::shared_ptr<PSB::IPSBValue> &value,
                                std::vector<std::string> &path,
                                MotionSnapshot &snapshot) {
            if(auto resource = std::dynamic_pointer_cast<PSB::PSBResource>(value)) {
                std::string joined;
                for(size_t index = 0; index < path.size(); ++index) {
                    if(index != 0) {
                        joined += '/';
                    }
                    joined += path[index];
                }
                if(!joined.empty()) {
                    snapshot.resourcesByPath.emplace(joined, resource);
                    if(looksLikeEmbeddedSourceKey(joined)) {
                        appendUnique(snapshot.sourceCandidates, joined);
                    }
                }
                return;
            }

            if(auto dic = std::dynamic_pointer_cast<PSB::PSBDictionary>(value)) {
                collectDictionaryResourceMap(dic, path, snapshot);
            } else if(auto list = std::dynamic_pointer_cast<PSB::PSBList>(value)) {
                collectListResourceMap(list, path, snapshot);
            }
        }

        void collectRootResources(const std::shared_ptr<const PSB::PSBDictionary> &root,
                                  MotionSnapshot &snapshot) {
            if(!root) {
                return;
            }

            std::vector<std::string> path;
            collectResourceMap(
                std::const_pointer_cast<PSB::PSBDictionary>(root), path, snapshot);

            for(const auto &[key, value] : *root) {
                const auto resource =
                    std::dynamic_pointer_cast<PSB::PSBResource>(value);
                if(!resource) {
                    continue;
                }
                if(looksLikeEmbeddedSourceKey(key)) {
                    appendUnique(snapshot.sourceCandidates, key);
                }
            }
        }

        void appendResourceAlias(MotionSnapshot &snapshot, const ttstr &alias) {
            const auto raw = narrow(alias);
            if(raw.empty()) {
                return;
            }
            appendUnique(snapshot.resourceAliases, raw);
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
            if(const auto placed = TVPGetPlacedPath(candidate);
               !placed.IsEmpty()) {
                resolved = placed;
                return true;
            }
        }
        return false;
    }

    void appendEmbeddedSourceCandidates(const MotionSnapshot &snapshot,
                                        const std::string &source,
                                        std::vector<ttstr> &candidates) {
        if(source.empty()) {
            return;
        }

        for(const auto &alias : snapshot.resourceAliases) {
            candidates.emplace_back(ttstr{ TJS_W("psb://") } + widen(alias) +
                                    TJS_W("/") + widen(source));
        }
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
        const auto loweredPath = lowercase(snapshot->path);
        if(loweredPath.find("yuzulogo.mtn") != std::string::npos ||
           loweredPath.find("m2logo.mtn") != std::string::npos) {
            LOGGER->warn("Motion logo snapshot loaded: path={} clips={} mainLabels={}",
                         snapshot->path, snapshot->clipsByLabel.size(),
                         snapshot->mainTimelineLabels.size());
        }
        appendResourceAlias(*snapshot, path);
        appendResourceAlias(*snapshot, TVPExtractStorageName(path));
        PSB::registerRootResources({ path, TVPExtractStorageName(path) }, *file);

        std::vector<std::string> pathParts;
        scanValue(std::const_pointer_cast<PSB::PSBDictionary>(root), pathParts,
                  *snapshot);
        collectRootResources(root, *snapshot);
        if(loweredPath.find("yuzulogo.mtn") != std::string::npos ||
           loweredPath.find("m2logo.mtn") != std::string::npos) {
            LOGGER->warn("Motion logo snapshot parsed: path={} clips={} mainLabels={} sources={}",
                         snapshot->path, snapshot->clipsByLabel.size(),
                         snapshot->mainTimelineLabels.size(),
                         snapshot->sourceCandidates.size());
        }
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
            state.loopTime =
                snapshot.timelineLoopTimes.find(label) != snapshot.timelineLoopTimes.end()
                ? snapshot.timelineLoopTimes.at(label)
                : -1.0;
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
                       const double dt,
                       std::vector<MotionEvent> *events) {
        if(dt <= 0.0) {
            return;
        }

        for(auto &[name, state] : states) {
            if(!state.playing) {
                state.wasPlaying = false;
                continue;
            }

            state.wasPlaying = true;
            state.currentTime += dt;
            if(state.totalFrames <= 0.0) {
                continue;
            }

            if(state.currentTime < state.totalFrames) {
                continue;
            }

            // Aligned to libkrkr2.so Player_progress_inner (0x6C106C):
            // loopTime >= 0: wrap using currentTime = currentTime + loopTime - lastTime
            // loopTime < 0: stop at end
            if(state.loopTime >= 0.0) {
                while(state.currentTime >= state.totalFrames) {
                    state.currentTime = state.currentTime + state.loopTime - state.totalFrames;
                }
            } else {
                state.currentTime = state.totalFrames;
                state.playing = false;
                // Aligned to libkrkr2.so Player_dispatchEvents (0x6C4490):
                // Queue onSync event when timeline stops (playing→false)
                if(events && state.wasPlaying) {
                    events->push_back({1, name, {}});
                    state.wasPlaying = false;
                }
            }
        }
    }

    // Scan PSB layer tree for action/sync events between prevTime and newTime.
    // Aligned to libkrkr2.so: updateLayers queues events when frame evaluation
    // crosses a frame boundary that has content.action or content.sync.
    void scanLayerActions(const MotionSnapshot &snapshot,
                          double prevTime, double newTime,
                          std::vector<MotionEvent> &events) {
        // Walk all layers in the snapshot
        for(const auto &[name, layerDict] : snapshot.layersByName) {
            if(!layerDict) continue;
            auto frameList = std::dynamic_pointer_cast<PSB::PSBList>(
                (*layerDict)["frameList"]);
            if(!frameList) continue;

            for(size_t i = 0; i < frameList->size(); ++i) {
                auto frame = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                    (*frameList)[static_cast<int>(i)]);
                if(!frame) continue;

                auto timeVal = std::dynamic_pointer_cast<PSB::PSBNumber>(
                    (*frame)["time"]);
                if(!timeVal) continue;
                double frameTime = 0.0;
                switch(timeVal->numberType) {
                    case PSB::PSBNumberType::Float:
                        frameTime = timeVal->getValue<float>(); break;
                    case PSB::PSBNumberType::Double:
                        frameTime = timeVal->getValue<double>(); break;
                    case PSB::PSBNumberType::Int:
                        frameTime = static_cast<double>(timeVal->getValue<int>()); break;
                    default:
                        frameTime = static_cast<double>(timeVal->getValue<tjs_int64>()); break;
                }

                // Only fire for frames crossed: prevTime < frameTime <= newTime
                if(frameTime <= prevTime || frameTime > newTime) continue;

                auto content = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                    (*frame)["content"]);
                if(!content) continue;

                // Check for action
                if(auto actionStr = std::dynamic_pointer_cast<PSB::PSBString>(
                    (*content)["action"])) {
                    if(!actionStr->value.empty()) {
                        events.push_back({0, actionStr->value, name});
                    }
                }

                // Check for sync
                if(auto syncVal = std::dynamic_pointer_cast<PSB::PSBNumber>(
                    (*content)["sync"])) {
                    double sv = 0.0;
                    switch(syncVal->numberType) {
                        case PSB::PSBNumberType::Float:
                            sv = syncVal->getValue<float>(); break;
                        case PSB::PSBNumberType::Int:
                            sv = static_cast<double>(syncVal->getValue<int>()); break;
                        default: break;
                    }
                    if(sv != 0.0) {
                        events.push_back({1, name, {}});
                    }
                }
            }
        }

        // Also scan clips' layers
        for(const auto &[clipLabel, clip] : snapshot.clipsByLabel) {
            for(const auto &[layerName, layerDict] : clip.layersByName) {
                if(!layerDict) continue;
                auto frameList = std::dynamic_pointer_cast<PSB::PSBList>(
                    (*layerDict)["frameList"]);
                if(!frameList) continue;

                for(size_t i = 0; i < frameList->size(); ++i) {
                    auto frame = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                        (*frameList)[static_cast<int>(i)]);
                    if(!frame) continue;

                    auto timeVal = std::dynamic_pointer_cast<PSB::PSBNumber>(
                        (*frame)["time"]);
                    if(!timeVal) continue;
                    double frameTime = static_cast<double>(
                        timeVal->numberType == PSB::PSBNumberType::Float
                            ? timeVal->getValue<float>()
                            : timeVal->numberType == PSB::PSBNumberType::Double
                                ? timeVal->getValue<double>()
                                : static_cast<double>(timeVal->getValue<int>()));

                    if(frameTime <= prevTime || frameTime > newTime) continue;

                    auto content = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                        (*frame)["content"]);
                    if(!content) continue;

                    if(auto actionStr = std::dynamic_pointer_cast<PSB::PSBString>(
                        (*content)["action"])) {
                        if(!actionStr->value.empty()) {
                            events.push_back({0, actionStr->value, layerName});
                        }
                    }
                }
            }
        }
    }

} // namespace motion::detail
