// Offline-only eager PSB decoder used by mtndump, motionsim, and legacy
// compatibility tests. Android libkrkr2.so Player runtime has no equivalent
// owner; keep this translation unit out of the production motionplayer target.

#include "OfflineMotionSnapshot.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <optional>
#include <sstream>

#include <spdlog/spdlog.h>

#define LOGGER spdlog::get("plugin")

namespace motion::detail {
    namespace {
        std::string lowercase(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char ch) {
                               return static_cast<char>(std::tolower(ch));
                           });
            return value;
        }

        bool hasSuffix(const std::string &value, const char *suffix) {
            const auto suffixLen = std::strlen(suffix);
            return value.size() >= suffixLen &&
                value.compare(value.size() - suffixLen, suffixLen, suffix) == 0;
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

        std::shared_ptr<PSB::PSBList>
        dictionaryList(const std::shared_ptr<const PSB::PSBDictionary> &dic,
                       const std::vector<std::string> &keys) {
            for(const auto &key : keys) {
                if(auto value =
                       std::dynamic_pointer_cast<PSB::PSBList>((*dic)[key])) {
                    return value;
                }
            }
            return nullptr;
        }

        std::shared_ptr<const PSB::PSBDictionary> navigateDictionaryPath(
            const std::shared_ptr<const PSB::PSBDictionary> &root,
            const std::string &path) {
            if(!root || path.empty()) {
                return nullptr;
            }
            auto node = root;
            std::istringstream stream(path);
            std::string segment;
            while(std::getline(stream, segment, '/')) {
                if(segment.empty() || !node) {
                    continue;
                }
                node = std::dynamic_pointer_cast<const PSB::PSBDictionary>(
                    (*node)[segment]);
                if(!node) {
                    return nullptr;
                }
            }
            return node;
        }

        void collectResourceMap(const std::shared_ptr<PSB::IPSBValue> &value,
                                std::vector<std::string> &path,
                                MotionSnapshot &snapshot);

        void collectResourceMap(const std::shared_ptr<PSB::IPSBValue> &value,
                                std::vector<std::string> &path,
                                MotionSnapshot &snapshot) {
            if(auto resource =
                   std::dynamic_pointer_cast<PSB::PSBResource>(value)) {
                std::string joined;
                for(size_t index = 0; index < path.size(); ++index) {
                    if(index != 0) {
                        joined += '/';
                    }
                    joined += path[index];
                }
                if(!joined.empty()) {
                    snapshot.resourcesByPath.emplace(joined, resource);
                }
                return;
            }
            if(auto dic =
                   std::dynamic_pointer_cast<PSB::PSBDictionary>(value)) {
                for(const auto &[key, child] : *dic) {
                    path.push_back(key);
                    collectResourceMap(child, path, snapshot);
                    path.pop_back();
                }
            } else if(auto list =
                          std::dynamic_pointer_cast<PSB::PSBList>(value)) {
                for(size_t index = 0; index < list->size(); ++index) {
                    path.push_back(std::to_string(index));
                    collectResourceMap((*list)[static_cast<int>(index)], path,
                                       snapshot);
                    path.pop_back();
                }
            }
        }

        void collectRootResources(
            const std::shared_ptr<const PSB::PSBDictionary> &root,
            MotionSnapshot &snapshot) {
            if(!root) {
                return;
            }
            std::vector<std::string> path;
            collectResourceMap(
                std::const_pointer_cast<PSB::PSBDictionary>(root), path,
                snapshot);
        }
    } // namespace

    std::shared_ptr<MotionSnapshot> loadMotionSnapshot(
        const ttstr &path, const tjs_int decryptSeed) {
        auto file = std::make_shared<PSB::DecodedPSBFile>();
        file->setSeed(decryptSeed);
        if(!file->loadPSBFile(path)) {
            LOGGER->error("motion load file: {} failed", path.AsStdString());
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
        if(logoChainTraceEnabledForPath(snapshot->path)) {
            resetLogoChainTraceSession(snapshot->path);
            logoChainTraceLogf(snapshot->path, "snapshot.load", "PSB parse",
                               -1.0, "path={} phase=begin", snapshot->path);
        }
        collectRootResources(root, *snapshot);

        if(logoChainTraceEnabledForPath(snapshot->path)) {
            const auto rootParameterList =
                dictionaryList(snapshot->root, {"parameter"});
            const auto rootParameterizeValue =
                (*snapshot->root)["parameterize"];
            const auto contentNode =
                navigateDictionaryPath(snapshot->root, "content");
            const auto contentParameterList = contentNode
                ? dictionaryList(contentNode, {"parameter"})
                : nullptr;
            const auto contentParameterizeValue = contentNode
                ? (*contentNode)["parameterize"]
                : std::shared_ptr<PSB::IPSBValue>{};
            const auto describeValue =
                [](const std::shared_ptr<PSB::IPSBValue> &value) {
                    if(!value) return std::string("<none>");
                    if(std::dynamic_pointer_cast<PSB::PSBList>(value))
                        return std::string("list");
                    if(std::dynamic_pointer_cast<PSB::PSBDictionary>(value))
                        return std::string("dict");
                    if(std::dynamic_pointer_cast<PSB::PSBString>(value))
                        return std::string("string");
                    if(std::dynamic_pointer_cast<PSB::PSBNumber>(value))
                        return std::string("number");
                    if(std::dynamic_pointer_cast<PSB::PSBBool>(value))
                        return std::string("bool");
                    return std::string("other");
                };
            logoChainTraceLogf(
                snapshot->path, "snapshot.parsed", "PSB parse", -1.0,
                "path={} rootParameterCount={} rootParameterize={} contentParameterCount={} contentParameterize={}",
                snapshot->path,
                rootParameterList ? rootParameterList->size() : 0,
                describeValue(rootParameterizeValue),
                contentParameterList ? contentParameterList->size() : 0,
                describeValue(contentParameterizeValue));

            for(const auto &[resourcePath, resource] :
                snapshot->resourcesByPath) {
                if(!hasSuffix(resourcePath, "/pixel") &&
                   !hasSuffix(resourcePath, "/pal")) {
                    continue;
                }
                const auto iconPath = hasSuffix(resourcePath, "/pixel")
                    ? resourcePath.substr(0, resourcePath.size() - 6)
                    : resourcePath.substr(0, resourcePath.size() - 4);
                const auto iconNode =
                    navigateDictionaryPath(snapshot->root, iconPath);
                const auto width = iconNode
                    ? dictionaryNumber(iconNode, {"width", "truncated_width"})
                          .value_or(0.0)
                    : 0.0;
                const auto height = iconNode
                    ? dictionaryNumber(iconNode, {"height", "truncated_height"})
                          .value_or(0.0)
                    : 0.0;
                const auto originX = iconNode
                    ? dictionaryNumber(iconNode, {"originX"}).value_or(0.0)
                    : 0.0;
                const auto originY = iconNode
                    ? dictionaryNumber(iconNode, {"originY"}).value_or(0.0)
                    : 0.0;
                const auto compress = iconNode
                    ? dictionaryString(iconNode, {"compress"}).value_or("raw")
                    : std::string("raw");
                const bool hasPal =
                    snapshot->resourcesByPath.find(iconPath + "/pal") !=
                    snapshot->resourcesByPath.end();
                logoChainTraceLogf(
                    snapshot->path, "snapshot.resource", "PSB parse", -1.0,
                    "resource={} width={:.0f} height={:.0f} origin=({:.3f},{:.3f}) hasPal={} isRL={} bytes={}",
                    resourcePath, width, height, originX, originY,
                    hasPal ? 1 : 0, lowercase(compress) == "rl" ? 1 : 0,
                    resource ? resource->data.size() : 0);
            }
        }
        return snapshot;
    }
} // namespace motion::detail
