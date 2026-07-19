//
// Created by lidong on 25-6-21.
//

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "LayerBitmapIntf.h"
#include "StorageIntf.h"
#include "motionplayer/MotionDispatch.h"
#include "ncbind.hpp"
#include "psbfile/PSBFile.h"
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
            if(TVPScriptEngine == nullptr) {
                TVPScriptEngine = new tTJS();
            }

            static bool indexed = false;
            if(!indexed) {
                ncbAutoRegister::AllRegist();
                indexed = true;
            }
            REQUIRE(ncbAutoRegister::LoadModule(TJS_W("PSBFile.dll")));
        }
    };

    PSB::PSBFile::OwnerFilter motionDecryptFilter(std::uint32_t seed) {
        // EmotePlayer_setEmotePSBDecryptSeed_callback @ 0x685D30 installs the
        // std::function whose call operator is sub_6863CC @ 0x6863CC.
        return [seed](PSB::PSBRawOwner &owner) {
            auto *header = owner.GetHeader();
            auto *cursor = header->encryptData;
            const auto length = static_cast<std::int32_t>(header->chunkOffsets -
                                                          header->encryptData);
            if(length <= 0) {
                return;
            }

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
} // namespace

TEST_CASE("read psbfile ezsave.pimg") {
    PSB::DecodedPSBFile f;
    REQUIRE(f.loadPSBFile(TEST_FILES_PATH "/emote/ezsave.pimg"));
    const PSB::PSBHeader &header = f.getPSBHeader();
    REQUIRE(f.getType() == PSB::PSBType::Pimg);
    CAPTURE(header.version, f.getType());

    auto objs = f.getObjects();

    SECTION("check width height") {
        int width = static_cast<int>(
            *std::dynamic_pointer_cast<PSB::PSBNumber>((*objs)["width"]));
        int height = static_cast<int>(
            *std::dynamic_pointer_cast<PSB::PSBNumber>((*objs)["height"]));
        REQUIRE(width == 1280);
        REQUIRE(height == 720);
    }

    SECTION("get layers") {
        auto layers =
            std::dynamic_pointer_cast<PSB::PSBList>((*objs)["layers"]);
        REQUIRE(layers->size() == 32);

        SECTION("check layer properties") {
            std::vector group_layer_ids = { 3093, 3093, 3093, 2174, 2174, 2174,
                                            2174, 2158, 2158, 2158, 2158, 2158,
                                            2158, 2158, 2158, 2158, 2158, 2158,
                                            2158, 2158, 0,    2142, 2142, 2142,
                                            2135, 2135, 0,    0,    0,    0,
                                            0,    0 };

            std::vector heights = { 42,  42,  54,  43, 49,  49, 51, 51,
                                    51,  51,  51,  51, 51,  51, 51, 51,
                                    51,  51,  51,  51, 612, 42, 42, 54,
                                    612, 720, 720, 0,  0,   0,  0,  0 };

            std::vector widths = { 27, 27, 36, 34, 41,   41, 40, 36, 36, 36, 36,
                                   36, 36, 36, 36, 36,   36, 36, 36, 36, 40, 27,
                                   27, 36, 72, 80, 1280, 0,  0,  0,  0,  0 };

            std::vector names = { "@pageup:over",
                                  "@pageup:off",
                                  "@pageup:rect",
                                  "@item:thumb:rect",
                                  "@item:over",
                                  "@item:off",
                                  "@item:rect",
                                  "@item0/cp:item",
                                  "@item1/cp:item",
                                  "@item2/cp:item",
                                  "@item3/cp:item",
                                  "@item4/cp:item",
                                  "@item5/cp:item",
                                  "@item6/cp:item",
                                  "@item7/cp:item",
                                  "@item8/cp:item",
                                  "@item9/cp:item",
                                  "@item10/cp:item",
                                  "@item11/cp:item",
                                  "@item12/cp:item",
                                  "@scroll/lay:rect",
                                  "@pagedown:over",
                                  "@pagedown:off",
                                  "@pagedown:rect",
                                  "@base:open:rect",
                                  "@base:rect",
                                  "レイヤー 1",
                                  "pageup",
                                  "item",
                                  "items",
                                  "pagedown",
                                  "範囲情報" };

            std::vector layer_ids = { 3092, 3087, -1,   -1,   2168, 2164, -1,
                                      2157, 2156, 2155, 2154, 2153, 2152, 2151,
                                      2150, 2149, 2148, 2147, 2146, 2145, -1,
                                      2139, 2138, -1,   -1,   -1,   2036, 3093,
                                      2174, 2158, 2142, 2135 };

            std::vector layer_types = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                        0, 0, 0, 0, 0, 2, 2, 2, 2, 2 };

            std::vector lefts = {
                1249, 1249, 1244, 1246, 1239, 1239, 1240, 1244,
                1244, 1244, 1244, 1244, 1244, 1244, 1244, 1244,
                1244, 1244, 1244, 1244, 1240, 1248, 1248, 1244,
                1208, 1200, 0,    0,    0,    0,    0,    0,
            };

            std::vector tops = {
                7,   7,   0,   58,  55,  55,  54,  54,  105, 156, 207,
                258, 309, 360, 411, 462, 513, 564, 615, 666, 54,  671,
                671, 666, 54,  0,   0,   0,   0,   0,   0,   0,
            };

            std::vector visibles = {
                1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1,
            };

            std::vector same_images = { 0,    0,    0,    0,    0,    0,
                                        0,    0,    2157, 2157, 2157, 2157,
                                        2157, 2157, 2157, 2157, 2157, 2157,
                                        2157, 2157, 0,    0,    0,    0,
                                        0,    0,    0,    0,    0,    0,
                                        0,    0 };

            for(int i = 0; i < layers->size(); i++) {
                auto layer =
                    std::dynamic_pointer_cast<PSB::PSBDictionary>((*layers)[i]);

                // height width opacity name layer_id layer_type left top type
                // visible  same_image
                {
                    auto group_layer_id =
                        std::dynamic_pointer_cast<PSB::PSBNumber>(
                            (*layer)["group_layer_id"]);
                    if(!(group_layer_id == nullptr &&
                         group_layer_ids[i] == 0)) {
                        REQUIRE(static_cast<int>(*group_layer_id) ==
                                group_layer_ids[i]);
                    }
                }
                {
                    auto height = std::dynamic_pointer_cast<PSB::PSBNumber>(
                        (*layer)["height"]);
                    REQUIRE(static_cast<int>(*height) == heights[i]);
                }
                {
                    auto width = std::dynamic_pointer_cast<PSB::PSBNumber>(
                        (*layer)["width"]);
                    REQUIRE(static_cast<int>(*width) == widths[i]);
                }
                {
                    auto opacity = std::dynamic_pointer_cast<PSB::PSBNumber>(
                        (*layer)["opacity"]);
                    REQUIRE(static_cast<int>(*opacity) == 255);
                }
                {
                    auto name = std::dynamic_pointer_cast<PSB::PSBString>(
                        (*layer)["name"]);
                    REQUIRE(name->value == names[i]);
                }
                {
                    auto layer_id = std::dynamic_pointer_cast<PSB::PSBNumber>(
                        (*layer)["layer_id"]);
                    REQUIRE(static_cast<int>(*layer_id) == layer_ids[i]);
                }
                {
                    auto layer_type = std::dynamic_pointer_cast<PSB::PSBNumber>(
                        (*layer)["layer_type"]);
                    REQUIRE(static_cast<int>(*layer_type) == layer_types[i]);
                }
                {
                    auto left = std::dynamic_pointer_cast<PSB::PSBNumber>(
                        (*layer)["left"]);
                    REQUIRE(static_cast<int>(*left) == lefts[i]);
                }
                {
                    auto top = std::dynamic_pointer_cast<PSB::PSBNumber>(
                        (*layer)["top"]);
                    REQUIRE(static_cast<int>(*top) == tops[i]);
                }
                {
                    auto type = std::dynamic_pointer_cast<PSB::PSBNumber>(
                        (*layer)["type"]);
                    REQUIRE(static_cast<int>(*type) == 13);
                }
                {
                    auto visible = std::dynamic_pointer_cast<PSB::PSBNumber>(
                        (*layer)["visible"]);
                    REQUIRE(static_cast<int>(*visible) == visibles[i]);
                }
                {
                    auto same_image = std::dynamic_pointer_cast<PSB::PSBNumber>(
                        (*layer)["same_image"]);
                    if(!(same_image == nullptr && same_images[i] == 0)) {
                        REQUIRE(static_cast<int>(*same_image) ==
                                same_images[i]);
                    }
                }
            }
        }
    }

    SECTION("collect resources") {
        auto resMetadata = f.getTypeHandler()->collectResources(f, true);
        REQUIRE(!resMetadata.empty());
        for(auto &res : resMetadata) {
            auto imgMetadata = dynamic_cast<PSB::ImageMetadata *>(res.get());
            REQUIRE(imgMetadata != nullptr);
        }
    }
}

TEST_CASE("read psbfile e-mote3.0 psb") {
    int key = 742877301;
    PSB::DecodedPSBFile f;
    f.setSeed(key);
    REQUIRE(
        f.loadPSBFile(TEST_FILES_PATH "/emote/e-mote3.0バニラパジャマa.psb"));
    REQUIRE(f.getType() == PSB::PSBType::Motion);
}

TEST_CASE("raw psb storage load applies the Android motion decrypt filter") {
    PSB::PSBFile file;
    REQUIRE(file.LoadStorage(TEST_FILES_PATH
                             "/emote/e-mote3.0バニラパジャマa.psb",
                             motionDecryptFilter(742877301u)));

    const PSB::PSBRawNode root = file.GetRoot();
    REQUIRE(root.GetTypeCategory() == 7);
    REQUIRE(std::string(root.GetDictionaryValueStrict("id").GetString()) ==
            "motion");
    REQUIRE(root.GetDictionaryValueStrict("spec").GetString() != nullptr);
}

TEST_CASE("raw psb storage load unwraps an existing MDF scenario") {
    const ttstr path =
        REFERENCE_PATH
        "/xp3/dracu_boot/DRACU-RIOT/scn/★本編－その１５_２.ks.scn";

    PSB::PSBFile file;
    REQUIRE(file.LoadStorage(path));

    const PSB::PSBRawNode root = file.GetRoot();
    REQUIRE(root.GetTypeCategory() == 7);
    REQUIRE_FALSE(root.GetDictionaryKeys().empty());

    std::unique_ptr<tTJSBinaryStream> stream(
        TVPCreateStream(path, TJS_BS_READ));
    REQUIRE(stream != nullptr);
    std::vector<std::uint8_t> mdf(
        static_cast<std::size_t>(stream->GetSize()));
    stream->ReadBuffer(mdf.data(), mdf.size());

    PSB::PSBFile octetFile;
    REQUIRE(octetFile.LoadOctet(mdf.data(), mdf.size()));
    REQUIRE(octetFile.GetRoot().GetTypeCategory() == 7);
}

TEST_CASE("raw psb owner and node views retain ezsave.pimg") {
    PSB::PSBRawNode retainedRoot;
    PSB::PSBRawNode retainedLayers;

    {
        PSB::PSBFile file;
        REQUIRE(file.LoadStorage(TEST_FILES_PATH "/emote/ezsave.pimg"));

        retainedRoot = file.GetRoot();
        REQUIRE(retainedRoot.GetType() == 0x21);

        PSB::PSBRawNode width;
        PSB::PSBRawNode height;
        REQUIRE(retainedRoot.GetDictionaryValue("width", width));
        REQUIRE(retainedRoot.GetDictionaryValue("height", height));
        REQUIRE(width.GetInt() == 1280);
        REQUIRE(height.GetInt() == 720);
        REQUIRE(retainedRoot.GetTypeCategory() == 7);
        REQUIRE(width.GetTypeCategory() == 2);
        REQUIRE(width.GetInt() == 1280);
        REQUIRE(width.GetDouble() == 1280.0);

        const auto keys = retainedRoot.GetDictionaryKeys();
        REQUIRE(!keys.empty());
        REQUIRE(std::find(keys.begin(), keys.end(), "width") != keys.end());
        REQUIRE(std::find(keys.begin(), keys.end(), "height") != keys.end());
        REQUIRE(width.GetDictionaryKeys().empty());
        REQUIRE(retainedRoot.ContainsDictionaryKey("width"));
        REQUIRE_FALSE(retainedRoot.ContainsDictionaryKey("missing"));
        REQUIRE_FALSE(width.ContainsDictionaryKey("missing"));
        REQUIRE(retainedRoot.GetDictionaryValueStrict("width").GetInt() ==
                1280);
        REQUIRE_THROWS(retainedRoot.GetDictionaryValueStrict("missing"));

        REQUIRE(retainedRoot.GetDictionaryValue("layers", retainedLayers));
        std::uint32_t count{};
        REQUIRE(retainedLayers.GetArrayCount(count));
        REQUIRE(count == 32);

        // Replacing the holder releases only its reference.  Existing node
        // views must continue to retain the old raw allocation.
        REQUIRE(file.LoadStorage(TEST_FILES_PATH "/emote/ezsave.pimg"));
    }

    PSB::PSBRawNode widthAfterHolderDestruction;
    REQUIRE(
        retainedRoot.GetDictionaryValue("width", widthAfterHolderDestruction));
    REQUIRE(widthAfterHolderDestruction.GetInt() == 1280);

    PSB::PSBRawNode firstLayer;
    REQUIRE(retainedLayers.GetArrayElement(0, firstLayer));
    PSB::PSBRawNode layerName;
    REQUIRE(firstLayer.GetDictionaryValue("name", layerName));
    REQUIRE(std::string(layerName.GetString()) == "@pageup:over");
    REQUIRE(firstLayer.GetString() == nullptr);
}

TEST_CASE("psb media caches and exposes real ezsave.pimg nodes") {
    const ScopedCoreScriptEngine scriptEngine;

    tTJSVariant scriptInstance;
    TVPScriptEngine->EvalExpression(TJS_W("new PSBFile()"), &scriptInstance);
    REQUIRE(scriptInstance.Type() == tvtObject);
    REQUIRE(ncbInstanceAdaptor<PSB::PSBFile>::GetNativeInstance(
                scriptInstance.AsObjectNoAddRef()) != nullptr);

    PSB::DecodedPSBFile decoded;
    REQUIRE(decoded.loadPSBFile(TEST_FILES_PATH "/emote/ezsave.pimg"));
    auto resources = decoded.getTypeHandler()->collectResources(decoded, true);
    REQUIRE_FALSE(resources.empty());
    const std::string resourceName = resources.front()->getName();
    REQUIRE_FALSE(resourceName.empty());

    const AutoPathScope autoPath(TEST_FILES_PATH "/emote/");
    const ttstr resourcePath = TVPNormalizeStorageName(
        ttstr(TJS_W("psb://ezsave.pimg/")) + ttstr(resourceName));
    REQUIRE(TVPIsExistentStorageNoSearchNoNormalize(resourcePath));

    std::unique_ptr<tTJSBinaryStream> stream(
        TVPCreateStream(resourcePath, TJS_BS_READ));
    REQUIRE(stream != nullptr);
    REQUIRE(stream->GetSize() > 0);

    CollectingStorageLister layers;
    TVPGetListAt(TVPNormalizeStorageName(
                     TJS_W("psb://ezsave.pimg/layers")),
                 &layers);
    REQUIRE(layers.entries.size() == 32);
    for(tjs_int index = 0; index < 32; ++index) {
        REQUIRE(layers.entries[static_cast<std::size_t>(index)] ==
                ttstr(index));
    }

    CollectingStorageLister missingList;
    const ttstr missingPath =
        TVPNormalizeStorageName(TJS_W("psb://ezsave.pimg/missing"));
    TVPGetListAt(missingPath, &missingList);
    REQUIRE(missingList.entries.empty());
    REQUIRE_FALSE(TVPIsExistentStorageNoSearchNoNormalize(missingPath));
    REQUIRE_THROWS(TVPCreateStream(missingPath, TJS_BS_READ));

    // A miss below the same cached container must not invalidate that owner.
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

    iTJSDispatch2 *root = rootValue.AsObjectNoAddRef();
    REQUIRE(root != nullptr);
    REQUIRE(root->AddRef() == 3);
    REQUIRE(root->Release() == 2);
    REQUIRE(root->FuncCall(0, TJS_W("missing"), nullptr, nullptr, 0, nullptr,
                           root) == TJS_E_NOTIMPL);
    const tTJSVariant ignored;
    REQUIRE(root->PropSet(0, TJS_W("missing"), nullptr, &ignored, root) ==
            TJS_E_NOTIMPL);
    REQUIRE(root->Reserved1() == TJS_E_NOTIMPL);
    REQUIRE(root->IsInstanceOf(0, nullptr, nullptr, TJS_W("Dictionary"),
                               root) == TJS_S_TRUE);

    iTJSNativeInstance *native{};
    REQUIRE(root->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
                                        PSB::GetPSBValueClassID(),
                                        &native) == TJS_S_OK);
    REQUIRE(native != nullptr);
    REQUIRE(native->Construct(0, nullptr, root) == TJS_S_OK);
    native->Invalidate();
    native->Destruct();

    tTJSVariant width;
    REQUIRE(root->PropGet(0, TJS_W("width"), nullptr, &width, root) ==
            TJS_S_OK);
    REQUIRE(width.AsInteger() == 1280);
    REQUIRE(motion::detail::motionPropGetInt(rootValue, TJS_W("width")) ==
            1280);

    tTJSVariant layersValue;
    REQUIRE(root->PropGet(0, TJS_W("layers"), nullptr, &layersValue, root) ==
            TJS_S_OK);
    iTJSDispatch2 *layers = layersValue.AsObjectNoAddRef();
    tjs_int count{};
    REQUIRE(layers->GetCount(&count, nullptr, nullptr, layers) == TJS_S_OK);
    REQUIRE(count == 32);
    REQUIRE(motion::detail::motionPropGetCount(layersValue) == 32);

    tTJSVariant lastLayerValue;
    REQUIRE(layers->PropGetByNum(0, -1, &lastLayerValue, layers) == TJS_S_OK);
    iTJSDispatch2 *lastLayer = lastLayerValue.AsObjectNoAddRef();
    tTJSVariant lastName;
    REQUIRE(lastLayer->PropGet(0, TJS_W("name"), nullptr, &lastName,
                               lastLayer) == TJS_S_OK);
    REQUIRE(ttstr(lastName).AsStdString() == "範囲情報");
    REQUIRE(ttstr(motion::detail::motionPropGet(
                      motion::detail::motionPropGetByNum(layersValue, -1),
                      TJS_W("name")))
                .AsStdString() == "範囲情報");

    REQUIRE(root->Invalidate(0, nullptr, nullptr, root) == TJS_S_OK);
    REQUIRE(root->IsValid(0, nullptr, nullptr, root) == TJS_S_FALSE);
    REQUIRE(root->PropGet(0, TJS_W("width"), nullptr, &width, root) ==
            TJS_E_INVALIDOBJECT);

    // Invalidation belongs only to this dispatch; a child dispatch has its own
    // valid byte and owner retain.
    REQUIRE(layers->GetCount(&count, nullptr, nullptr, layers) == TJS_S_OK);
    REQUIRE(count == 32);
}
