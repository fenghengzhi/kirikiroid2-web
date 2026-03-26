//
// Created by LiDon on 2025/9/11.
//

#include <spdlog/spdlog.h>

#include "PSBMedia.h"

#include "UtilStreams.h"

namespace PSB {
#define LOGGER spdlog::get("plugin")

    void PSBMedia::NormalizeDomainName(ttstr &name) {
        tjs_int dotIndex = name.IndexOf(TJS_W('.'));
        if(dotIndex == -1)
            return;
        name = name.SubString(0, dotIndex) +
            name.SubString(dotIndex, name.GetLen()).AsLowerCase();
    }

    void PSBMedia::NormalizePathName(ttstr &name) {
        // province(_p), mask(_m)
    }

    namespace {
        // Try exact match first, then strip extension for embedded resources
        // that are stored without extensions in the PSB dictionary tree.
        std::string resolveResourceKey(
            const std::unordered_map<std::string, PSBResource> &resources,
            const std::string &name) {
            if(resources.find(name) != resources.end()) {
                return name;
            }
            // Strip extension and retry
            const auto dotPos = name.rfind('.');
            const auto slashPos = name.rfind('/');
            if(dotPos != std::string::npos &&
               (slashPos == std::string::npos || dotPos > slashPos)) {
                const auto base = name.substr(0, dotPos);
                if(resources.find(base) != resources.end()) {
                    return base;
                }
            }
            return {};
        }
    }

    bool PSBMedia::CheckExistentStorage(const ttstr &name) {
        return !resolveResourceKey(_resources, name.AsStdString()).empty();
    }

    tTJSBinaryStream *PSBMedia::Open(const ttstr &name, tjs_uint32 flags) {
        auto key = resolveResourceKey(_resources, name.AsStdString());
        if(key.empty()) {
            key = name.AsStdString(); // fallback to exact match (will create empty entry)
        }
        auto memoryStream = new tTVPMemoryStream();
        auto res = _resources[key];
        memoryStream->WriteBuffer(res.data.data(), res.data.size());
        memoryStream->Seek(0, TJS_BS_SEEK_SET);
        return memoryStream;
    }

    void PSBMedia::GetListAt(const ttstr &name, iTVPStorageLister *lister) {
        LOGGER->error("TODO: PSBMedia GetListAt");
    }

    void PSBMedia::GetLocallyAccessibleName(ttstr &name) {
        LOGGER->error("can't get GetLocallyAccessibleName from {}!",
                      name.AsStdString());
    }

    void PSBMedia::add(const std::string &name,
                       const std::shared_ptr<PSBResource> &resource) {
        this->_resources[name] = *resource;
    }
} // namespace PSB