#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "StorageIntf.h"
#include "motionplayer/MotionDispatch.h"
#include "ncbind.hpp"
#include "psbfile/PSBDispatch.h"
#include "psbfile/PSBRawFile.h"
#include "test_config.h"

extern tTJS *TVPScriptEngine;
extern void TVPGetListAt(const ttstr &name, iTVPStorageLister *lister);

namespace {
    class CollectingStorageLister final : public iTVPStorageLister {
    public:
        void Add(const ttstr &file) override { entries.push_back(file); }
        std::vector<ttstr> entries;
    };

    class AutoPathScope final {
    public:
        explicit AutoPathScope(const ttstr &path) : path_(path) {
            TVPAddAutoPath(path_);
        }
        ~AutoPathScope() { TVPRemoveAutoPath(path_); }
    private:
        ttstr path_;
    };

    class ScopedCoreScriptEngine final {
    public:
        ScopedCoreScriptEngine() {
            if(TVPScriptEngine == nullptr) TVPScriptEngine = new tTJS();
            static bool indexed = false;
            if(!indexed) {
                ncbAutoRegister::AllRegist();
                indexed = true;
            }
            REQUIRE(ncbAutoRegister::LoadModule(TJS_W("PSBFile.dll")));
        }
    };

    PSB::PSBFile::OwnerFilter motionDecryptFilter(std::uint32_t seed) {
        return [seed](PSB::PSBRawOwner &owner) {
            auto *header = owner.GetHeader();
            auto *cursor = header->encryptData;
            const auto length = static_cast<std::int32_t>(
                header->chunkOffsets - header->encryptData);
            if(length <= 0) return;
            auto *end = cursor + length;
            std::uint32_t x = 123456789u;
            std::uint32_t y = 362436069u;
            std::uint32_t z = 521288629u;
            std::uint32_t w = seed;
            std::uint32_t bytes = 0;
            do {
                if(bytes == 0) {
                    const std::uint32_t t = x ^ (x << 11u);
                    x = y;
                    y = z;
                    z = w;
                    w = w ^ (w >> 19u) ^ t ^ (t >> 8u);
                    bytes = w;
                }
                *cursor++ ^= static_cast<std::uint8_t>(bytes);
                bytes >>= 8u;
            } while(cursor < end);
        };
    }

    bool findFirstResource(const PSB::PSBRawNode &node,
                           const std::string &path,
                           std::string &resourcePath) {
        if(node.GetTypeCategory() == 5) {
            std::uint32_t size = 0;
            if(node.GetResource(size) && size > 0) {
                resourcePath = path;
                return true;
            }
        }
        if(node.GetTypeCategory() == 7) {
            for(const auto &key : node.GetDictionaryKeys()) {
                PSB::PSBRawNode child;
                if(node.GetDictionaryValue(key, child) &&
                   findFirstResource(child,
                       path.empty() ? key : path + "/" + key,
                       resourcePath)) return true;
            }
        } else if(node.GetTypeCategory() == 5) {
            return false;
        } else {
            std::uint32_t count = 0;
            if(node.GetArrayCount(count)) {
                for(std::uint32_t index = 0; index < count; ++index) {
                    PSB::PSBRawNode child;
                    if(node.GetArrayElement(index, child) &&
                       findFirstResource(child,
                           path.empty() ? std::to_string(index)
                                        : path + "/" + std::to_string(index),
                           resourcePath)) return true;
                }
            }
        }
        return false;
    }
} // namespace

TEST_CASE("raw psb storage load applies the Android motion decrypt filter") {
    PSB::PSBFile file;
    REQUIRE(file.LoadStorage(TEST_FILES_PATH
                             "/emote/e-mote3.0バニラパジャマa.psb",
                             motionDecryptFilter(742877301u)));
    const auto root = file.GetRoot();
    REQUIRE(root.GetTypeCategory() == 7);
    REQUIRE(std::string(root.GetDictionaryValueStrict("id").GetString()) ==
            "motion");
    REQUIRE(root.GetDictionaryValueStrict("spec").GetString() != nullptr);
}

TEST_CASE("raw psb owner and node views retain ezsave.pimg") {
    PSB::PSBRawNode retainedRoot;
    PSB::PSBRawNode retainedLayers;
    {
        PSB::PSBFile file;
        REQUIRE(file.LoadStorage(TEST_FILES_PATH "/emote/ezsave.pimg"));
        retainedRoot = file.GetRoot();
        REQUIRE(retainedRoot.GetType() == 0x21);
        REQUIRE(retainedRoot.GetDictionaryValueStrict("width").GetInt() == 1280);
        REQUIRE(retainedRoot.GetDictionaryValueStrict("height").GetInt() == 720);
        REQUIRE(retainedRoot.GetDictionaryValue("layers", retainedLayers));
        std::uint32_t count = 0;
        REQUIRE(retainedLayers.GetArrayCount(count));
        REQUIRE(count == 32);
        REQUIRE(file.LoadStorage(TEST_FILES_PATH "/emote/ezsave.pimg"));
    }
    REQUIRE(retainedRoot.GetDictionaryValueStrict("width").GetInt() == 1280);
    PSB::PSBRawNode firstLayer;
    REQUIRE(retainedLayers.GetArrayElement(0, firstLayer));
    REQUIRE(std::string(firstLayer.GetDictionaryValueStrict("name").GetString()) ==
            "@pageup:over");
}

TEST_CASE("psb media caches and exposes real ezsave.pimg nodes") {
    const ScopedCoreScriptEngine scriptEngine;
    tTJSVariant scriptInstance;
    TVPScriptEngine->EvalExpression(TJS_W("new PSBFile()"), &scriptInstance);
    REQUIRE(scriptInstance.Type() == tvtObject);
    REQUIRE(ncbInstanceAdaptor<PSB::PSBFile>::GetNativeInstance(
                scriptInstance.AsObjectNoAddRef()) != nullptr);

    PSB::PSBFile raw;
    REQUIRE(raw.LoadStorage(TEST_FILES_PATH "/emote/ezsave.pimg"));
    std::string resourceName;
    REQUIRE(findFirstResource(raw.GetRoot(), {}, resourceName));

    const AutoPathScope autoPath(TEST_FILES_PATH "/emote/");
    const ttstr resourcePath = TVPNormalizeStorageName(
        ttstr(TJS_W("psb://ezsave.pimg/")) + ttstr(resourceName));
    REQUIRE(TVPIsExistentStorageNoSearchNoNormalize(resourcePath));
    std::unique_ptr<tTJSBinaryStream> stream(
        TVPCreateStream(resourcePath, TJS_BS_READ));
    REQUIRE(stream != nullptr);
    REQUIRE(stream->GetSize() > 0);

    CollectingStorageLister layers;
    TVPGetListAt(TVPNormalizeStorageName(TJS_W("psb://ezsave.pimg/layers")),
                 &layers);
    REQUIRE(layers.entries.size() == 32);
    for(tjs_int index = 0; index < 32; ++index)
        REQUIRE(layers.entries[static_cast<std::size_t>(index)] == ttstr(index));

    const ttstr missingPath =
        TVPNormalizeStorageName(TJS_W("psb://ezsave.pimg/missing"));
    CollectingStorageLister missing;
    TVPGetListAt(missingPath, &missing);
    REQUIRE(missing.entries.empty());
    REQUIRE_FALSE(TVPIsExistentStorageNoSearchNoNormalize(missingPath));
    REQUIRE_THROWS(TVPCreateStream(missingPath, TJS_BS_READ));
    REQUIRE(TVPIsExistentStorageNoSearchNoNormalize(resourcePath));
    REQUIRE(TVPGetLocallyAccessibleName(resourcePath).IsEmpty());
}

TEST_CASE("raw psb dispatch reads packed values and retains its owner") {
    tTJSVariant rootValue;
    {
        PSB::PSBFile file;
        REQUIRE(file.LoadStorage(TEST_FILES_PATH "/emote/ezsave.pimg"));
        rootValue = PSB::CreatePSBValueVariant(file.GetRoot());
    }
    auto *root = rootValue.AsObjectNoAddRef();
    REQUIRE(root != nullptr);
    REQUIRE(root->IsInstanceOf(0, nullptr, nullptr, TJS_W("Dictionary"), root) ==
            TJS_S_TRUE);
    tTJSVariant width;
    REQUIRE(root->PropGet(0, TJS_W("width"), nullptr, &width, root) == TJS_S_OK);
    REQUIRE(width.AsInteger() == 1280);
    REQUIRE(motion::detail::motionPropGetInt(rootValue, TJS_W("width")) == 1280);

    tTJSVariant layersValue;
    REQUIRE(root->PropGet(0, TJS_W("layers"), nullptr, &layersValue, root) ==
            TJS_S_OK);
    auto *layers = layersValue.AsObjectNoAddRef();
    tjs_int count = 0;
    REQUIRE(layers->GetCount(&count, nullptr, nullptr, layers) == TJS_S_OK);
    REQUIRE(count == 32);
    tTJSVariant lastLayerValue;
    REQUIRE(layers->PropGetByNum(0, -1, &lastLayerValue, layers) == TJS_S_OK);
    auto *lastLayer = lastLayerValue.AsObjectNoAddRef();
    tTJSVariant lastName;
    REQUIRE(lastLayer->PropGet(0, TJS_W("name"), nullptr, &lastName,
                               lastLayer) == TJS_S_OK);
    REQUIRE(ttstr(lastName).AsStdString() == "範囲情報");
}
