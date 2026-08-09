#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "StorageIntf.h"
#include "motionplayer/MotionDispatch.h"
#include "ncbind.hpp"
#include "psbfile/PSBDispatch.h"
#include "psbfile/PSBMedia.h"
#include "psbfile/PSBPackedInternal.h"
#include "psbfile/PSBRawFile.h"
#include "test_config.h"

extern tTJS *TVPScriptEngine;
extern void TVPGetListAt(const ttstr &name, iTVPStorageLister *lister);

static_assert(!noexcept(
    std::declval<PSB::PSBFile &>().Transfer_guess())); // 0x598A64 caller EH

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

    struct EnumRecord {
        tjs_int numparams = 0;
        ttstr name;
        tjs_int flags = 0;
        tTJSVariant value;
        iTJSDispatch2 *objthis = nullptr;
    };

    class EnumCapture final : public tTJSDispatch {
    public:
        tjs_error FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *,
                           tTJSVariant *result, tjs_int numparams,
                           tTJSVariant **param,
                           iTJSDispatch2 *objthis) override {
            EnumRecord record;
            record.numparams = numparams;
            record.objthis = objthis;
            if(numparams >= 1) record.name = ttstr(*param[0]);
            if(numparams >= 2) record.flags = param[1]->AsInteger();
            if(numparams >= 3) record.value = *param[2];
            records.push_back(record);
            if(result != nullptr) *result = static_cast<tjs_int>(1);
            return TJS_S_OK;
        }

        std::vector<EnumRecord> records;
    };

    tjs_error enumerate(iTJSDispatch2 *dispatch, tjs_uint32 flags,
                        EnumCapture &capture) {
        tTJSVariantClosure callback(&capture, nullptr);
        return dispatch->EnumMembers(flags, &callback, dispatch);
    }

    tTJSVariant getProperty(iTJSDispatch2 *dispatch, const tjs_char *name) {
        tTJSVariant value;
        REQUIRE(dispatch->PropGet(0, name, nullptr, &value, dispatch) ==
                TJS_S_OK);
        return value;
    }

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

    bool getArrayCount(const PSB::PSBRawNode &node, std::uint32_t &count) {
        if(node.GetType() != 0x20u) return false;
        count = PSB::detail::ReadPackedCount_guess(node.GetNode() + 1);
        return true;
    }

    bool getArrayElement(const PSB::PSBRawNode &node, std::uint32_t index,
                         PSB::PSBRawNode &value) {
        if(node.GetType() != 0x20u) return false;
        const std::uint8_t *packed = node.GetNode() + 1;
        const PSB::detail::PsbArray_guess offsets(packed);
        if(index >= offsets.nElementCount) return false;
        value = PSB::PSBRawNode(
            node.GetFile_guess(), packed + offsets.nBytes + offsets[index]);
        return true;
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
                if(node.GetDictionaryValue(key.c_str(), child) &&
                   findFirstResource(child,
                       path.empty() ? key : path + "/" + key,
                       resourcePath)) return true;
            }
        } else if(node.GetTypeCategory() == 5) {
            return false;
        } else {
            std::uint32_t count = 0;
            if(getArrayCount(node, count)) {
                for(std::uint32_t index = 0; index < count; ++index) {
                    PSB::PSBRawNode child;
                    if(getArrayElement(node, index, child) &&
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
    // PSBRawNode_ContainsDictionaryKey_guess @ 0x5995D8 constructs its
    // temporary before the category gate, delegates only category 7, and
    // returns false for known non-dictionary categories.
    REQUIRE(root.ContainsDictionaryKey("id"));
    REQUIRE_FALSE(
        root.GetDictionaryValueStrict("id").ContainsDictionaryKey("id"));
    // PSBRawNode_GetString_guess @ 0x598B58 returns null for every known
    // non-category-4 tag before touching the owner's strings table.
    REQUIRE(root.GetString() == nullptr);
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
        // Android motionplayer callers inline this single-argument root
        // constructor at seven sites; iOS keeps the shared boundary as
        // iOS arm64 @0x1001263B8.
        retainedRoot = PSB::PSBRawNode(file);
        REQUIRE(retainedRoot.GetType() == 0x21);
        REQUIRE(retainedRoot.GetDictionaryValueStrict("width").GetInt() == 1280);
        REQUIRE(retainedRoot.GetDictionaryValueStrict("height").GetInt() == 720);
        REQUIRE(retainedRoot.GetDictionaryValue("layers", retainedLayers));
        std::uint32_t count = 0;
        REQUIRE(getArrayCount(retainedLayers, count));
        REQUIRE(count == 32);
        REQUIRE(file.LoadStorage(TEST_FILES_PATH "/emote/ezsave.pimg"));
    }
    REQUIRE(retainedRoot.GetDictionaryValueStrict("width").GetInt() == 1280);
    PSB::PSBRawNode firstLayer;
    REQUIRE(getArrayElement(retainedLayers, 0, firstLayer));
    REQUIRE(std::string(firstLayer.GetDictionaryValueStrict("name").GetString()) ==
            "@pageup:over");
}

TEST_CASE("raw psb dictionary lookup accepts an aliased output node") {
    PSB::PSBFile file;
    REQUIRE(file.LoadStorage(TEST_FILES_PATH "/emote/ezsave.pimg"));
    PSB::PSBRawNode node = file.GetRoot();
    PSB::PSBRawOwner *const owner = node.GetOwner();
    const std::uint8_t *const rootNode = node.GetNode();

    // sub_695DE8 @ 0x696A84..0x696A90 passes the same raw-node address as
    // both this and out to sub_598D58.  The callee deliberately has no alias
    // guard: it releases out, reloads/retains this->owner, then writes child.
    // "name" exists in the shared trie for layer dictionaries but not in the
    // root dictionary, so this reaches the second lookup's alias-miss path.
    REQUIRE_FALSE(node.GetDictionaryValue("name", node));
    REQUIRE(node.GetOwner() == owner);
    REQUIRE(node.GetNode() == rootNode);
    REQUIRE(node.GetDictionaryValue("layers", node));
    REQUIRE(node.GetOwner() == owner);
    REQUIRE(node.GetType() == 0x20u);
    std::uint32_t count = 0;
    REQUIRE(getArrayCount(node, count));
    REQUIRE(count == 32u);
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

    const ttstr emptySegmentPath =
        TVPNormalizeStorageName(TJS_W("psb://ezsave.pimg/layers//0"));
    REQUIRE_FALSE(TVPIsExistentStorageNoSearchNoNormalize(emptySegmentPath));

    const ttstr missingContainer =
        TVPNormalizeStorageName(TJS_W("psb://missing.pimg/resource"));
    REQUIRE_THROWS(
        TVPIsExistentStorageNoSearchNoNormalize(missingContainer));
    REQUIRE(TVPIsExistentStorageNoSearchNoNormalize(resourcePath));
    REQUIRE(TVPGetLocallyAccessibleName(resourcePath).IsEmpty());
}

TEST_CASE("psb media replacement keeps old stream metadata destructible") {
    const ScopedCoreScriptEngine scriptEngine;
    PSB::PSBFile raw;
    REQUIRE(raw.LoadStorage(TEST_FILES_PATH "/emote/ezsave.pimg"));
    std::string resourceName;
    REQUIRE(findFirstResource(raw.GetRoot(), {}, resourceName));

    const AutoPathScope autoPath(TEST_FILES_PATH "/emote/");
    PSB::PSBFile unfilteredProbe;
    REQUIRE(unfilteredProbe.LoadStorage(
        TEST_FILES_PATH "/emote/e-mote3.0バニラパジャマa.psb"));
    REQUIRE(unfilteredProbe.GetRoot().GetType() == 0x1au);
    PSB::PSBMedia media;
    const ttstr firstName =
        ttstr(TJS_W("ezsave.pimg/")) + ttstr(resourceName);
    std::unique_ptr<tTJSBinaryStream> borrowed(
        media.Open(firstName, TJS_BS_READ));
    REQUIRE(borrowed != nullptr);
    const tjs_uint64 borrowedSize = borrowed->GetSize();
    REQUIRE(borrowedSize > 0);

    // Open reaches EnsureContainer @ 0x599E04 first.  The encrypted motion's
    // raw header is loadable without a filter, while its unfiltered root is a
    // resource node rather than the ezsave dictionary.  Looking up ezsave's
    // known resource path must therefore reach the post-replacement
    // cannot-open branch.  A stale cache would incorrectly return a stream;
    // a failed second load would return nullptr without throwing.
    const ttstr secondName =
        ttstr(TJS_W("e-mote3.0\u30d0\u30cb\u30e9\u30d1\u30b8\u30e3\u30dea.psb/")) +
        ttstr(resourceName);
    bool sawCannotOpen = false;
    try {
        std::unique_ptr<tTJSBinaryStream> unexpected(
            media.Open(secondName, TJS_BS_READ));
    } catch(const eTJSError &error) {
        REQUIRE(error.GetMessage().AsStdString().find("cannot open psbfile") !=
                std::string::npos);
        sawCannotOpen = true;
    }
    REQUIRE(sawCannotOpen);

    // Reverse-engineering proves the stream's block is borrowed.  This test
    // only guards that its own metadata and destructor remain usable after
    // replacement; do not read the now-invalid block.
    REQUIRE(borrowed->GetSize() == borrowedSize);
    borrowed.reset();

    std::unique_ptr<tTJSBinaryStream> reloaded(
        media.Open(firstName, TJS_BS_READ));
    REQUIRE(reloaded != nullptr);
    REQUIRE(reloaded->GetSize() == borrowedSize);
}

TEST_CASE("raw psb dispatch reads packed values and retains its owner") {
    tTJSVariant rootValue;
    {
        PSB::PSBFile file;
        REQUIRE(file.LoadStorage(TEST_FILES_PATH "/emote/ezsave.pimg"));
        iTJSDispatch2 *rootDispatch = file.GetRootDispatch();
        REQUIRE(rootDispatch != nullptr);
        rootValue = tTJSVariant(rootDispatch, rootDispatch);
        rootDispatch->Release();
    }
    auto *root = rootValue.AsObjectNoAddRef();
    REQUIRE(root != nullptr);
    REQUIRE(root->IsInstanceOf(0, nullptr, nullptr, TJS_W("Dictionary"), root) ==
            TJS_S_TRUE);
    REQUIRE(root->IsInstanceOf(0, nullptr, nullptr, TJS_W("Array"), root) ==
            TJS_S_FALSE);
    REQUIRE(root->IsInstanceOf(0, TJS_W("width"), nullptr,
                               TJS_W("Dictionary"), root) == TJS_E_NOTIMPL);
    tTJSVariant width;
    REQUIRE(root->PropGet(0, TJS_W("width"), nullptr, &width, root) == TJS_S_OK);
    REQUIRE(width.AsInteger() == 1280);
    REQUIRE(motion::detail::motionPropGetInt(rootValue, TJS_W("width")) == 1280);

    tTJSVariant layersValue;
    REQUIRE(root->PropGet(0, TJS_W("layers"), nullptr, &layersValue, root) ==
            TJS_S_OK);
    // PSBFile has already been destroyed. Drop the root closure's Object and
    // ObjThis references; the child Array dispatch must retain the raw owner.
    rootValue.Clear();
    auto *layers = layersValue.AsObjectNoAddRef();
    REQUIRE(layers != nullptr);
    REQUIRE(layers->IsInstanceOf(0, nullptr, nullptr, TJS_W("Array"), layers) ==
            TJS_S_TRUE);
    tjs_int count = 0;
    REQUIRE(layers->GetCount(&count, nullptr, nullptr, layers) == TJS_S_OK);
    REQUIRE(count == 32);
    tTJSVariant lastLayerValue;
    REQUIRE(layers->PropGetByNum(0, -1, &lastLayerValue, layers) == TJS_S_OK);
    // Drop the parent Array closure. The child Dictionary dispatch must
    // independently retain the same raw owner.
    layersValue.Clear();
    auto *lastLayer = lastLayerValue.AsObjectNoAddRef();
    REQUIRE(lastLayer != nullptr);
    tTJSVariant lastName;
    REQUIRE(lastLayer->PropGet(0, TJS_W("name"), nullptr, &lastName,
                               lastLayer) == TJS_S_OK);
    REQUIRE(ttstr(lastName).AsStdString() == "範囲情報");
}

TEST_CASE("typed PSBFile NCB wrappers load and expose the root property") {
    const ScopedCoreScriptEngine scriptEngine;
    const AutoPathScope autoPath(TEST_FILES_PATH "/emote/");

    tTJSVariant instance;
    TVPScriptEngine->EvalExpression(TJS_W("new PSBFile()"), &instance);
    REQUIRE(instance.Type() == tvtObject);
    auto *dispatch = instance.AsObjectNoAddRef();
    REQUIRE(dispatch != nullptr);

    tTJSVariant loadResult;
    REQUIRE(dispatch->FuncCall(0, TJS_W("load"), nullptr, &loadResult, 0,
                               nullptr, dispatch) == TJS_E_BADPARAMCOUNT);

    tTJSVariant path(TJS_W("ezsave.pimg"));
    tTJSVariant *params[] = { &path };
    REQUIRE(dispatch->FuncCall(0, TJS_W("load"), nullptr, &loadResult, 1,
                               params, dispatch) == TJS_S_OK);
    REQUIRE(loadResult.Type() == tvtInteger);
    REQUIRE(loadResult.AsInteger() == 1);

    tTJSVariant rootValue;
    REQUIRE(dispatch->PropGet(0, TJS_W("root"), nullptr, &rootValue,
                              dispatch) == TJS_S_OK);
    REQUIRE(rootValue.Type() == tvtObject);
    auto *root = rootValue.AsObjectNoAddRef();
    REQUIRE(root != nullptr);
    REQUIRE(root->IsInstanceOf(0, nullptr, nullptr, TJS_W("Dictionary"),
                               root) == TJS_S_TRUE);
    REQUIRE(dispatch->PropSet(0, TJS_W("root"), nullptr, &rootValue,
                              dispatch) == TJS_E_ACCESSDENYED);
}

TEST_CASE("PSB dispatch enumerates packed dictionary and array members") {
    PSB::PSBFile file;
    REQUIRE(file.LoadStorage(TEST_FILES_PATH "/emote/ezsave.pimg"));
    iTJSDispatch2 *rootDispatch = file.GetRootDispatch();
    REQUIRE(rootDispatch != nullptr);
    tTJSVariant rootValue(rootDispatch, rootDispatch);
    rootDispatch->Release();
    auto *root = rootValue.AsObjectNoAddRef();

    const std::vector<std::string> expectedNames = {
        "2036.tlg", "2138.tlg", "2139.tlg", "2157.tlg",
        "2164.tlg", "2168.tlg", "3087.tlg", "3092.tlg",
        "height", "layers", "width"
    };

    EnumCapture dictionary;
    REQUIRE(enumerate(root, 0, dictionary) == TJS_S_OK);
    REQUIRE(dictionary.records.size() == expectedNames.size());
    for(std::size_t index = 0; index < expectedNames.size(); ++index) {
        const auto &record = dictionary.records[index];
        REQUIRE(record.numparams == 3);
        REQUIRE(record.name.AsStdString() == expectedNames[index]);
        REQUIRE(record.flags == 0);
        REQUIRE(record.objthis == root);
        if(index < 8) REQUIRE(record.value.Type() == tvtOctet);
    }
    REQUIRE(dictionary.records[0].value.AsOctetNoAddRef()->GetLength() ==
            48265);
    REQUIRE(dictionary.records[8].value.Type() == tvtInteger);
    REQUIRE(dictionary.records[8].value.AsInteger() == 720);
    REQUIRE(dictionary.records[9].value.Type() == tvtObject);
    REQUIRE(dictionary.records[10].value.Type() == tvtInteger);
    REQUIRE(dictionary.records[10].value.AsInteger() == 1280);

    EnumCapture dictionaryNames;
    REQUIRE(enumerate(root, TJS_ENUM_NO_VALUE, dictionaryNames) == TJS_S_OK);
    REQUIRE(dictionaryNames.records.size() == expectedNames.size());
    for(std::size_t index = 0; index < expectedNames.size(); ++index) {
        REQUIRE(dictionaryNames.records[index].numparams == 2);
        REQUIRE(dictionaryNames.records[index].name.AsStdString() ==
                expectedNames[index]);
        REQUIRE(dictionaryNames.records[index].flags == 0);
        REQUIRE(dictionaryNames.records[index].objthis == root);
    }

    auto *layers = dictionary.records[9].value.AsObjectNoAddRef();
    REQUIRE(layers != nullptr);
    EnumCapture array;
    REQUIRE(enumerate(layers, 0, array) == TJS_S_OK);
    REQUIRE(array.records.size() == 32);
    for(std::size_t index = 0; index < array.records.size(); ++index) {
        const auto &record = array.records[index];
        REQUIRE(record.numparams == 3);
        REQUIRE(record.name.AsStdString() == std::to_string(index));
        REQUIRE(record.flags == 0);
        REQUIRE(record.value.Type() == tvtObject);
        REQUIRE(record.objthis == layers);
        auto *element = record.value.AsObjectNoAddRef();
        REQUIRE(element->IsInstanceOf(0, nullptr, nullptr,
                                      TJS_W("Dictionary"), element) ==
                TJS_S_TRUE);
    }

    EnumCapture arrayNames;
    REQUIRE(enumerate(layers, TJS_ENUM_NO_VALUE, arrayNames) == TJS_S_OK);
    REQUIRE(arrayNames.records.size() == 32);
    for(std::size_t index = 0; index < arrayNames.records.size(); ++index) {
        REQUIRE(arrayNames.records[index].numparams == 2);
        REQUIRE(arrayNames.records[index].name.AsStdString() ==
                std::to_string(index));
        REQUIRE(arrayNames.records[index].flags == 0);
        REQUIRE(arrayNames.records[index].objthis == layers);
    }
}

TEST_CASE("PSB Resource Variant owns copied bytes after owner release") {
    tTJSVariant copiedResource;
    std::vector<std::uint8_t> expected;
    {
        PSB::PSBFile file;
        REQUIRE(file.LoadStorage(TEST_FILES_PATH "/emote/ezsave.pimg"));

        const PSB::PSBRawNode rootNode = file.GetRoot();
        PSB::PSBRawNode resourceNode;
        REQUIRE(rootNode.GetDictionaryValue("2157.tlg", resourceNode));
        REQUIRE(resourceNode.GetType() == 0x19u);
        std::uint32_t resourceSize = 0;
        const std::uint8_t *resource =
            resourceNode.GetResource(resourceSize);
        REQUIRE(resource != nullptr);
        REQUIRE(resourceSize == 612u);
        expected.assign(resource, resource + resourceSize);

        iTJSDispatch2 *rootDispatch = file.GetRootDispatch();
        REQUIRE(rootDispatch != nullptr);
        tTJSVariant rootValue(rootDispatch, rootDispatch);
        rootDispatch->Release();
        auto *root = rootValue.AsObjectNoAddRef();
        REQUIRE(root != nullptr);
        copiedResource = getProperty(root, TJS_W("2157.tlg"));
        REQUIRE(copiedResource.Type() == tvtOctet);
        auto *octet = copiedResource.AsOctetNoAddRef();
        REQUIRE(octet != nullptr);
        REQUIRE(octet->GetLength() == resourceSize);
        REQUIRE(std::equal(expected.begin(), expected.end(), octet->GetData()));
    }

    // CreateVariant @0x596B50..0x596B74 owns a copied Octet allocation; the
    // source PSBFile/raw-node holders above are all gone at this point.
    auto *octet = copiedResource.AsOctetNoAddRef();
    REQUIRE(octet != nullptr);
    REQUIRE(octet->GetLength() == expected.size());
    REQUIRE(std::equal(expected.begin(), expected.end(), octet->GetData()));
}

TEST_CASE("PSB dispatch preserves native instance and invalidation boundaries") {
    PSB::PSBFile file;
    REQUIRE(file.LoadStorage(TEST_FILES_PATH "/emote/ezsave.pimg"));
    iTJSDispatch2 *rootDispatch = file.GetRootDispatch();
    REQUIRE(rootDispatch != nullptr);
    tTJSVariant rootValue(rootDispatch, rootDispatch);
    rootDispatch->Release();
    auto *root = rootValue.AsObjectNoAddRef();
    const tjs_int32 valueClassId =
        TJS::TJSRegisterNativeClass(TJS_W("PSBValueClass"));

    iTJSNativeInstance *native = nullptr;
    REQUIRE(root->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
                                        valueClassId,
                                        &native) == TJS_S_OK);
    REQUIRE(native != nullptr);

    auto *const sentinel = reinterpret_cast<iTJSNativeInstance *>(
        static_cast<std::uintptr_t>(1));
    native = sentinel;
    REQUIRE(root->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
                                        valueClassId + 1,
                                        &native) == TJS_E_FAIL);
    REQUIRE(native == sentinel);
    REQUIRE(root->NativeInstanceSupport(TJS_NIS_REGISTER,
                                        valueClassId,
                                        &native) == TJS_E_NOTIMPL);
    REQUIRE(native == sentinel);

    static_cast<PSB::PSBValueDispatch *>(root)->Invalidate();
    REQUIRE(root->IsValid(0, nullptr, nullptr, root) == TJS_S_TRUE);
    REQUIRE(root->Invalidate(0, TJS_W("member"), nullptr, root) ==
            TJS_E_NOTIMPL);
    REQUIRE(root->IsValid(0, nullptr, nullptr, root) == TJS_S_TRUE);
    REQUIRE(root->Invalidate(0, nullptr, nullptr, root) == TJS_S_OK);
    REQUIRE(root->IsValid(0, nullptr, nullptr, root) == TJS_S_FALSE);
    REQUIRE(root->Invalidate(0, nullptr, nullptr, root) ==
            TJS_E_INVALIDOBJECT);

    tTJSVariant value;
    REQUIRE(root->PropGet(0, TJS_W("width"), nullptr, &value, root) ==
            TJS_E_INVALIDOBJECT);
    tjs_int count = -1;
    REQUIRE(root->GetCount(&count, nullptr, nullptr, root) ==
            TJS_E_INVALIDOBJECT);
    EnumCapture capture;
    REQUIRE(enumerate(root, 0, capture) == TJS_E_INVALIDOBJECT);
    REQUIRE(capture.records.empty());
    REQUIRE(root->IsInstanceOf(0, nullptr, nullptr, TJS_W("Dictionary"),
                               root) == TJS_S_TRUE);
    native = nullptr;
    REQUIRE(root->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
                                        valueClassId,
                                        &native) == TJS_S_OK);
    REQUIRE(native != nullptr);
}

TEST_CASE("PSB dispatch converts an encrypted motion float to Real") {
    PSB::PSBFile file;
    REQUIRE(file.LoadStorage(TEST_FILES_PATH
                             "/emote/e-mote3.0バニラパジャマa.psb",
                             motionDecryptFilter(742877301u)));
    iTJSDispatch2 *rootDispatch = file.GetRootDispatch();
    REQUIRE(rootDispatch != nullptr);
    tTJSVariant rootValue(rootDispatch, rootDispatch);
    rootDispatch->Release();
    auto *root = rootValue.AsObjectNoAddRef();

    tTJSVariant metadataValue = getProperty(root, TJS_W("metadata"));
    auto *metadata = metadataValue.AsObjectNoAddRef();
    REQUIRE(metadata != nullptr);
    tTJSVariant bustControlValue =
        getProperty(metadata, TJS_W("bustControl"));
    auto *bustControl = bustControlValue.AsObjectNoAddRef();
    REQUIRE(bustControl != nullptr);
    tTJSVariant firstValue;
    REQUIRE(bustControl->PropGetByNum(0, 0, &firstValue, bustControl) ==
            TJS_S_OK);
    auto *first = firstValue.AsObjectNoAddRef();
    REQUIRE(first != nullptr);
    tTJSVariant friction = getProperty(first, TJS_W("friction"));
    REQUIRE(friction.Type() == tvtReal);
    REQUIRE(friction.AsReal() == 0.125);
}
