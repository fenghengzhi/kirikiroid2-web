//
// Created to verify motionplayer/emoteplayer behavior aligned to libkrkr2.so.
//

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>

#include "motionplayer/D3DAdaptor.h"
#include "motionplayer/EmotePlayer.h"
#include "motionplayer/MotionNode.h"
#include "motionplayer/Player.h"
#include "motionplayer/PrivateMotionGLL.h"
#include "motionplayer/PlayerRenderInternal.h"
#include "motionplayer/ResourceManager.h"
#include "motionplayer/RuntimeSupport.h"
#include "motionplayer/SeparateLayerAdaptor.h"
#include "psbfile/PSBDispatch.h"
#include "LayerIntf.h"
#include "LayerTreeOwner.h"
#include "impl/LayerImpl.h"
#include "RenderManager.h"
#include "test_config.h"
#include "tjsArray.h"
#include "tjsError.h"
#include "tjsObject.h"
#include "ncbind.hpp"
#include "tvpgl.h"

extern tTJS *TVPScriptEngine;
extern unsigned int TVPMaxTextureSize;

namespace {

    constexpr tjs_int kEmoteSeed = 742877301;

    ttstr motionFixturePath() {
        return ttstr(TEST_FILES_PATH "/emote/e-mote3.0バニラパジャマa.psb");
    }

    void setEmoteSeed() {
        tTJSVariant seed{ kEmoteSeed };
        tTJSVariant *params[] = { &seed };
        REQUIRE(motion::ResourceManager::setEmotePSBDecryptSeed(
                    nullptr, 1, params, nullptr) == TJS_S_OK);
    }

    struct ScopedCoreScriptEngine {
        ScopedCoreScriptEngine() {
            if(TVPScriptEngine == nullptr) {
                TVPScriptEngine = new tTJS();
            }

            // Mirror application startup: index built-in NCB modules once,
            // then register the real motionplayer.dll class objects before
            // creating a ResourceManager adaptor.  NCB has no module-unload
            // path, so this runtime intentionally lasts for the test process.
            static bool indexed = false;
            if(!indexed) {
                ncbAutoRegister::AllRegist();
                indexed = true;
            }
            REQUIRE(ncbAutoRegister::LoadModule(TJS_W("motionplayer.dll")));

            // EmoteObject_init @0x67DBAC evaluates global.kag before it
            // constructs ResourceManager. The application always installs
            // this owner; make the unit-test script engine model that boundary
            // instead of relying on the old owner-less port constructor.
            iTJSDispatch2 *global = TVPScriptEngine->GetGlobalNoAddRef();
            tTJSVariant kag;
            if(TJS_FAILED(global->PropGet(
                   0, TJS_W("kag"), nullptr, &kag, global)) ||
               kag.Type() == tvtVoid) {
                iTJSDispatch2 *kagObject = TJSCreateDictionaryObject();
                REQUIRE(kagObject != nullptr);
                tTJSVariant kagValue(kagObject, kagObject);
                kagObject->Release();
                REQUIRE(TJS_SUCCEEDED(global->PropSet(
                    TJS_MEMBERENSURE | TJS_IGNOREPROP, TJS_W("kag"),
                    nullptr, &kagValue, global)));
            }
        }
    };

    struct EmoteDecryptCallback final : tTJSDispatch {
        explicit EmoteDecryptCallback(int *destructionCount) :
            destructionCount(destructionCount) {}

        ~EmoteDecryptCallback() override { ++*destructionCount; }

        tjs_error FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *,
                           tTJSVariant *, tjs_int numparams,
                           tTJSVariant **param,
                           iTJSDispatch2 *objthis) override {
            ++callCount;
            receivedObjThis = objthis;
            if(numparams != 2 || param[0]->Type() != tvtObject) {
                return TJS_E_INVALIDPARAM;
            }

            iTJSDispatch2 *accessor = param[0]->AsObjectNoAddRef();
            receivedSize = param[1]->AsInteger();
            tTJSVariant count;
            if(!accessor ||
               TJS_FAILED(accessor->PropGet(0, TJS_W("count"), nullptr,
                                            &count, accessor)) ||
               count.AsInteger() != receivedSize) {
                return TJS_E_INVALIDPARAM;
            }

            const auto readByte = [accessor](tjs_int index,
                                             std::uint8_t &value) {
                tTJSVariant item;
                const tjs_error error = accessor->PropGetByNum(
                    TJS_MEMBERMUSTEXIST, index, &item, accessor);
                if(TJS_FAILED(error))
                    return error;
                value = static_cast<std::uint8_t>(item.AsInteger());
                return TJS_S_OK;
            };
            const auto readU32 = [&readByte](tjs_int offset,
                                             std::uint32_t &value) {
                value = 0;
                for(tjs_int byte = 0; byte < 4; ++byte) {
                    std::uint8_t part{};
                    const tjs_error error = readByte(offset + byte, part);
                    if(TJS_FAILED(error))
                        return error;
                    value |= static_cast<std::uint32_t>(part) << (byte * 8);
                }
                return TJS_S_OK;
            };

            std::uint32_t begin{};
            std::uint32_t end{};
            if(TJS_FAILED(readU32(8, begin)) ||
               TJS_FAILED(readU32(24, end)) || begin > end ||
               end > static_cast<std::uint64_t>(receivedSize)) {
                return TJS_E_INVALIDPARAM;
            }

            std::uint32_t x = 123456789u;
            std::uint32_t y = 362436069u;
            std::uint32_t z = 521288629u;
            std::uint32_t w = kEmoteSeed;
            std::uint32_t bytes = 0;
            for(std::uint32_t offset = begin; offset < end; ++offset) {
                if(bytes == 0) {
                    const std::uint32_t t = x ^ (x << 11u);
                    x = y;
                    y = z;
                    z = w;
                    w = w ^ (w >> 19u) ^ t ^ (t >> 8u);
                    bytes = w;
                }
                std::uint8_t current{};
                if(TJS_FAILED(readByte(static_cast<tjs_int>(offset),
                                       current))) {
                    return TJS_E_INVALIDPARAM;
                }
                tTJSVariant decoded(static_cast<tjs_int32>(
                    current ^ static_cast<std::uint8_t>(bytes)));
                if(TJS_FAILED(accessor->PropSetByNum(
                       TJS_MEMBERMUSTEXIST, static_cast<tjs_int>(offset),
                       &decoded, accessor))) {
                    return TJS_E_INVALIDPARAM;
                }
                bytes >>= 8u;
            }
            valid = true;
            return TJS_S_OK;
        }

        int *destructionCount{};
        int callCount{};
        tjs_int64 receivedSize{};
        iTJSDispatch2 *receivedObjThis{};
        bool valid{};
    };

    tTJSVariant getProp(const tTJSVariant &object, const tjs_char *name) {
        REQUIRE(object.Type() == tvtObject);
        auto *dispatch = object.AsObjectNoAddRef();
        REQUIRE(dispatch != nullptr);

        tTJSVariant result;
        REQUIRE(TJS_SUCCEEDED(
            dispatch->PropGet(0, name, nullptr, &result, dispatch)));
        return result;
    }

    tTJSVariant makeResourceManagerDispatch(motion::ResourceManager &manager) {
        using Adaptor = ncbInstanceAdaptor<motion::ResourceManager>;
        iTJSDispatch2 *dispatch = Adaptor::CreateAdaptor(&manager, true);
        REQUIRE(dispatch != nullptr);
        tTJSVariant result(dispatch, dispatch);
        dispatch->Release();
        return result;
    }

    tTJSVariant getIndex(const tTJSVariant &object, tjs_int index) {
        REQUIRE(object.Type() == tvtObject);
        auto *dispatch = object.AsObjectNoAddRef();
        REQUIRE(dispatch != nullptr);

        tTJSVariant result;
        REQUIRE(TJS_SUCCEEDED(
            dispatch->PropGetByNum(TJS_IGNOREPROP, index, &result, dispatch)));
        return result;
    }

    tjs_int variantCount(const tTJSVariant &object) {
        return static_cast<tjs_int>(
            getProp(object, TJS_W("count")).AsInteger());
    }

    std::vector<std::pair<ttstr, tTJSVariant>>
    dictionaryEntries(const tTJSVariant &object) {
        struct Enumerator : tTJSDispatch {
            std::vector<std::pair<ttstr, tTJSVariant>> entries;

            tjs_error FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *,
                               tTJSVariant *result, tjs_int numparams,
                               tTJSVariant **param, iTJSDispatch2 *) override {
                if(numparams >= 3) {
                    entries.emplace_back(ttstr(*param[0]), *param[2]);
                }
                if(result) {
                    *result = static_cast<tjs_int>(1);
                }
                return TJS_S_OK;
            }
        } enumerator;

        REQUIRE(object.Type() == tvtObject);
        auto *dispatch = object.AsObjectNoAddRef();
        REQUIRE(dispatch != nullptr);
        tTJSVariantClosure closure(&enumerator, nullptr);
        if(TJS_FAILED(
               dispatch->EnumMembers(TJS_IGNOREPROP, &closure, dispatch))) {
            return {};
        }
        return enumerator.entries;
    }

    tTJSArrayNI *requireArrayNI(const tTJSVariant &value) {
        REQUIRE(value.Type() == tvtObject);
        auto *dispatch = value.AsObjectNoAddRef();
        REQUIRE(dispatch != nullptr);

        tTJSArrayNI *native = nullptr;
        REQUIRE(dispatch->NativeInstanceSupport(
                    TJS_NIS_GETINSTANCE, TJSGetArrayClassID(),
                    reinterpret_cast<iTJSNativeInstance **>(&native)) ==
                TJS_S_OK);
        REQUIRE(native != nullptr);
        return native;
    }

    bool hasDictionaryKey(const tTJSVariant &object,
                          const tjs_char *name) {
        const ttstr expected(name);
        const auto entries = dictionaryEntries(object);
        return std::any_of(
            entries.begin(), entries.end(),
            [&](const auto &entry) { return entry.first == expected; });
    }

    void requireSameObject(const tTJSVariant &lhs,
                           const tTJSVariant &rhs) {
        REQUIRE(lhs.Type() == tvtObject);
        REQUIRE(rhs.Type() == tvtObject);
        REQUIRE(lhs.AsObjectNoAddRef() == rhs.AsObjectNoAddRef());
        REQUIRE(lhs.AsObjectThisNoAddRef() == rhs.AsObjectThisNoAddRef());
    }

    void discardNonRootNodes(motion::Player &player) {
        auto &nodes = player.nodesForBuild();
        while(nodes.size() > 1) {
            auto &node = nodes.back();
            if(node.layerId1 != 0) {
                player.releaseLayerId(node.layerId1);
            }
            if(node.layerId2 != 0) {
                player.releaseLayerId(node.layerId2);
            }
            if(node.preparedRenderItem &&
               node.preparedRenderItem->rawFlag20) {
                player.releaseLayerId(
                    node.preparedRenderItem->renderLayerId);
            }
            delete node.preparedRenderItem;
            node.preparedRenderItem = nullptr;
            nodes.pop_back();
        }
        player.nodeLabelMapForBuild().clear();
    }

    tTJSVariant firstPriorityContent(
        const tTJSVariant &module, const ttstr &chara,
        const ttstr &motionName) {
        const auto objectRoot = getProp(module, TJS_W("object"));
        const auto charaRoot = getProp(objectRoot, chara.c_str());
        const auto motionRoot = getProp(charaRoot, TJS_W("motion"));
        const auto motion = getProp(motionRoot, motionName.c_str());
        const auto priority = getProp(motion, TJS_W("priority"));
        return getProp(getIndex(priority, 0), TJS_W("content"));
    }

    std::vector<int> rebuildInactivePrioritySkeleton(
        motion::Player &player, const tTJSVariant &content) {
        discardNonRootNodes(player);
        auto &nodes = player.nodesForBuild();
        auto &root = nodes.front();
        root.accumulated.active = false;
        root.source.clear();
        root.stencilCompositeMaskNodes.clear();

        const tjs_int count = variantCount(content);
        REQUIRE(count >= 0);
        for(tjs_int index = 1; index <= count; ++index) {
            auto &node = nodes.emplace_back();
            node.index = index;
            node.parentIndex = 0;
            node.accumulated.active = false;
        }

        std::vector<int> nativeOrder;
        nativeOrder.reserve(static_cast<size_t>(count));
        for(tjs_int position = count; position > 0; --position) {
            const int nodeIndex = static_cast<int>(
                getIndex(content, position - 1).AsInteger()) + 1;
            REQUIRE(nodeIndex >= 1);
            REQUIRE(nodeIndex <= count);
            nativeOrder.push_back(nodeIndex);
        }
        return nativeOrder;
    }

    void dumpDictionary(const tTJSVariant &object, const std::string &prefix,
                        int depth = 0) {
        if(depth > 2 || object.Type() != tvtObject) {
            return;
        }

        for(const auto &[key, value] : dictionaryEntries(object)) {
            std::cerr << prefix << key.AsStdString()
                      << " type=" << static_cast<int>(value.Type());
            if(value.Type() == tvtString) {
                std::cerr << " value=" << ttstr(value).AsStdString();
            } else if(value.Type() == tvtInteger) {
                std::cerr << " value=" << value.AsInteger();
            } else if(value.Type() == tvtReal) {
                std::cerr << " value=" << value.AsReal();
            }
            std::cerr << "\n";

            if(value.Type() != tvtObject) {
                continue;
            }

            if(const auto count = variantCount(value); count > 0) {
                const auto limit = std::min<tjs_int>(count, 3);
                std::cerr << prefix << "  [count]=" << count << "\n";
                for(tjs_int index = 0; index < limit; ++index) {
                    const auto item = getIndex(value, index);
                    std::cerr << prefix << "  [" << index
                              << "] type=" << static_cast<int>(item.Type());
                    if(item.Type() == tvtString) {
                        std::cerr << " value=" << ttstr(item).AsStdString();
                    } else if(item.Type() == tvtInteger) {
                        std::cerr << " value=" << item.AsInteger();
                    } else if(item.Type() == tvtReal) {
                        std::cerr << " value=" << item.AsReal();
                    }
                    std::cerr << "\n";
                    if(item.Type() == tvtObject) {
                        dumpDictionary(item, prefix + "    ", depth + 1);
                    }
                }
            } else {
                dumpDictionary(value, prefix + "  ", depth + 1);
            }
        }
    }

    bool containsString(const tTJSVariant &object, const ttstr &expected) {
        const auto count = variantCount(object);
        for(tjs_int index = 0; index < count; ++index) {
            if(ttstr(getIndex(object, index)) == expected) {
                return true;
            }
        }
        return false;
    }

    struct FakeWindowDispatch : tTJSDispatch {
        tjs_error IsInstanceOf(tjs_uint32, const tjs_char *membername,
                               tjs_uint32 *, const tjs_char *classname,
                               iTJSDispatch2 *) override {
            if(!membername && classname &&
               !TJS_strcmp(classname, TJS_W("Window"))) {
                return TJS_S_TRUE;
            }
            return TJS_S_FALSE;
        }
    };

    struct FakeObjectDispatch : tTJSDispatch {
        tjs_error IsInstanceOf(tjs_uint32, const tjs_char *, tjs_uint32 *,
                               const tjs_char *, iTJSDispatch2 *) override {
            return TJS_S_FALSE;
        }
    };

    struct FakeLayerOwnerDispatch : tTJSDispatch {
        iTVPLayerTreeOwner *treeOwner = nullptr;

        tjs_error PropGet(tjs_uint32 flag, const tjs_char *membername,
                          tjs_uint32 *hint, tTJSVariant *result,
                          iTJSDispatch2 *objthis) override {
            if(membername &&
               !TJS_strcmp(membername, TJS_W("layerTreeOwnerInterface"))) {
                if(result) {
                    *result = static_cast<tjs_int64>(
                        reinterpret_cast<tjs_intptr_t>(treeOwner));
                }
                return TJS_S_OK;
            }
            return tTJSDispatch::PropGet(flag, membername, hint, result,
                                         objthis);
        }
    };

    struct FakeLayerTreeOwner : iTVPLayerTreeOwner {
        iTJSDispatch2 *owner = nullptr;

        void RegisterLayerManager(iTVPLayerManager *) override {}
        void UnregisterLayerManager(iTVPLayerManager *) override {}
        void StartBitmapCompletion(iTVPLayerManager *) override {}
        void NotifyBitmapCompleted(iTVPLayerManager *, tjs_int, tjs_int,
                                   tTVPBaseTexture *, const tTVPRect &,
                                   tTVPLayerType, tjs_int) override {}
        void EndBitmapCompletion(iTVPLayerManager *) override {}
        void SetMouseCursor(iTVPLayerManager *, tjs_int) override {}
        void GetCursorPos(iTVPLayerManager *, tjs_int &x, tjs_int &y) override {
            x = 0;
            y = 0;
        }
        void SetCursorPos(iTVPLayerManager *, tjs_int, tjs_int) override {}
        void ReleaseMouseCapture(iTVPLayerManager *) override {}
        void SetHint(iTVPLayerManager *, iTJSDispatch2 *,
                     const ttstr &) override {}
        void NotifyLayerResize(iTVPLayerManager *) override {}
        void NotifyLayerImageChange(iTVPLayerManager *) override {}
        void SetAttentionPoint(iTVPLayerManager *, tTJSNI_BaseLayer *, tjs_int,
                               tjs_int) override {}
        void DisableAttentionPoint(iTVPLayerManager *) override {}
        void SetImeMode(iTVPLayerManager *, tjs_int) override {}
        void ResetImeMode(iTVPLayerManager *) override {}
        iTJSDispatch2 *GetOwnerNoAddRef() const override { return owner; }
    };

    struct TestLayerHandle {
        iTJSDispatch2 *object = nullptr;
        tTJSNI_Layer *native = nullptr;
    };

    void ensureTVPGLInitialized() {
        static const bool initialized = [] {
            TVPInitTVPGL();
            return true;
        }();
        (void)initialized;
    }

    struct ScopedMaxTextureSize {
        explicit ScopedMaxTextureSize(unsigned int value) :
            previous(TVPMaxTextureSize) {
            TVPMaxTextureSize = value;
        }

        ~ScopedMaxTextureSize() { TVPMaxTextureSize = previous; }

        unsigned int previous;
    };

    TestLayerHandle
    createRegisteredTestLayer(iTVPLayerTreeOwner *treeOwner,
                              tTJSNI_BaseLayer *parent,
                              const tTJSVariantClosure &ownerClosure) {
        ensureTVPGLInitialized();

        if(tTJSNC_Layer::ClassID == static_cast<tjs_uint32>(-1)) {
            tTJSNC_Layer::ClassID = TJSRegisterNativeClass(TJS_W("Layer"));
        }

        auto *object = new tTJSCustomObject();
        auto *native = new tTJSNI_Layer();
        iTJSNativeInstance *nativeBase = native;
        REQUIRE(TJS_SUCCEEDED(object->NativeInstanceSupport(
            TJS_NIS_REGISTER, tTJSNC_Layer::ClassID, &nativeBase)));
        REQUIRE(TJS_SUCCEEDED(native->ConstructResolvedTreeOwnerLike_0x800438(
            treeOwner, parent, object, ownerClosure)));
        return { object, native };
    }

} // namespace

TEST_CASE("setEmotePSBDecryptSeed follows the Android raw callback boundary") {
    REQUIRE(motion::ResourceManager::setEmotePSBDecryptSeed(
                nullptr, 0, nullptr, nullptr) == TJS_E_BADPARAMCOUNT);

    tTJSVariant realSeed{ 42.75 };
    tTJSVariant ignored{ 99 };
    tTJSVariant *realParams[] = { &realSeed, &ignored };
    REQUIRE(motion::ResourceManager::setEmotePSBDecryptSeed(
                nullptr, 2, realParams, nullptr) == TJS_S_OK);
    REQUIRE(motion::ResourceManager::getEmotePSBDecryptSeed() == 42);

    tTJSVariant stringSeed{ TJS_W("314") };
    tTJSVariant *stringParams[] = { &stringSeed };
    REQUIRE(motion::ResourceManager::setEmotePSBDecryptSeed(
                nullptr, 1, stringParams, nullptr) == TJS_S_OK);
    REQUIRE(motion::ResourceManager::getEmotePSBDecryptSeed() == 314);

    setEmoteSeed();
}

TEST_CASE("setEmotePSBDecryptFunc owns and invokes the Android closure shape") {
    REQUIRE(motion::ResourceManager::setEmotePSBDecryptFunc(
                nullptr, 0, nullptr, nullptr) == TJS_E_BADPARAMCOUNT);

    tTJSVariant notCallable{ 7 };
    tTJSVariant *invalidParams[] = { &notCallable };
    REQUIRE_THROWS(motion::ResourceManager::setEmotePSBDecryptFunc(
        nullptr, 1, invalidParams, nullptr));

    // Restore the numeric seed first, then replace the actual global raw
    // owner filter with the callable under test.
    setEmoteSeed();

    struct ScopedScriptEngine {
        bool owns = false;
        ScopedScriptEngine() {
            if(TVPScriptEngine == nullptr) {
                TVPScriptEngine = new tTJS();
                owns = true;
            }
        }
        ~ScopedScriptEngine() {
            if(owns) {
                TVPScriptEngine->Release();
                TVPScriptEngine = nullptr;
            }
        }
    } scriptEngine;

    int destructionCount = 0;
    auto *callback = new EmoteDecryptCallback(&destructionCount);
    tTJSVariant callable(callback);
    callback->Release();
    tTJSVariant ignored{ 99 };
    tTJSVariant *params[] = { &callable, &ignored };
    REQUIRE(motion::ResourceManager::setEmotePSBDecryptFunc(
                nullptr, 2, params, nullptr) == TJS_S_OK);
    callable.Clear();
    REQUIRE(destructionCount == 0);

    motion::ResourceManager manager;
    const auto module = manager.load(motionFixturePath());
    REQUIRE(module.Type() == tvtObject);
    REQUIRE(callback->callCount == 1);
    REQUIRE(callback->valid);
    REQUIRE(callback->receivedObjThis == callback);
    REQUIRE(callback->receivedSize == static_cast<tjs_int64>(
               std::filesystem::file_size(
                   std::filesystem::path(motionFixturePath().AsStdString()))));

    // Installing the seed lambda swaps the global std::function and destroys
    // the former tRefHolder control block, releasing Object exactly once.
    setEmoteSeed();
    REQUIRE(destructionCount == 1);
}

TEST_CASE("__Private_Motion_GLLayer uses private ClassID only") {
    FakeLayerTreeOwner treeOwner;
    FakeLayerOwnerDispatch ownerDispatch;
    ownerDispatch.treeOwner = &treeOwner;
    treeOwner.owner = &ownerDispatch;

    tTJSVariant ownerVariant(&ownerDispatch, &ownerDispatch);
    const auto ownerClosure = ownerVariant.AsObjectClosureNoAddRef();
    auto primaryLayer =
        createRegisteredTestLayer(&treeOwner, nullptr, ownerClosure);
    auto targetLayer = createRegisteredTestLayer(
        &treeOwner, primaryLayer.native, ownerClosure);
    tTJSVariant targetVariant(targetLayer.object, targetLayer.object);

    motion::SeparateLayerAdaptor adaptor(targetVariant);
    adaptor.setAbsolute(3);
    iTJSDispatch2 *privateObject = motion::ensurePrivateMotionGLLLike_0x6D5948(
        adaptor, ownerVariant, targetVariant, targetLayer.object, 64, 32);
    REQUIRE(privateObject != nullptr);

    auto *privateLayer =
        motion::resolvePrivateMotionGLLNativeLike_0x6DE24C(privateObject);
    REQUIRE(privateLayer != nullptr);
    REQUIRE(privateLayer->GetWidth() == 64);
    REQUIRE(privateLayer->GetHeight() == 32);
    REQUIRE(privateLayer->GetVisible());
    REQUIRE(privateLayer->GetAbsoluteOrderIndex() == 3);
    REQUIRE(motion::privateMotionGLLRenderQueueSizeLike_0x6DE738(
                privateObject) == 0);
    motion::clearPrivateMotionGLLRenderQueueLike_0x6DE738(privateObject);
    REQUIRE(motion::privateMotionGLLRenderQueueSizeLike_0x6DE738(
                privateObject) == 0);
    motion::PrivateMotionGLLRenderItemInputLike_0x6DE738 queueItem;
    queueItem.opacity = 255;
    queueItem.sourceRect = { 0, 0, 4, 4 };
    queueItem.affinePoints =
        std::array<motion::detail::MeshPoint, 3>{ {
            { 0.0f, 0.0f },
            { 4.0f, 0.0f },
            { 0.0f, 4.0f },
        } };
    motion::appendPrivateMotionGLLRenderItemLike_0x6DE738(privateObject,
                                                          queueItem,
                                                          nullptr);
    REQUIRE(motion::privateMotionGLLRenderQueueSizeLike_0x6DE738(
                privateObject) == 1);

    std::vector<motion::detail::MeshPoint> meshPoints{
        { 1.0f, 2.0f }, { 3.0f, 4.0f },
    };
    queueItem.geometryType = 2;
    motion::appendPrivateMotionGLLRenderItemLike_0x6DE738(
        privateObject, queueItem, &meshPoints);
    REQUIRE(meshPoints.empty());
    REQUIRE(motion::privateMotionGLLRenderQueueSizeLike_0x6DE738(
                privateObject) == 2);
    motion::clearPrivateMotionGLLRenderQueueLike_0x6DE738(privateObject);
    REQUIRE(motion::privateMotionGLLRenderQueueSizeLike_0x6DE738(
                privateObject) == 0);

    tTJSNI_BaseLayer *layerByPublicClass = nullptr;
    REQUIRE(TJS_FAILED(privateObject->NativeInstanceSupport(
        TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
        reinterpret_cast<iTJSNativeInstance **>(&layerByPublicClass))));
    REQUIRE(layerByPublicClass == nullptr);

    tTJSVariant width(17);
    tTJSVariant height(19);
    tTJSVariant *sizeArgs[] = { &width, &height };
    REQUIRE(TJS_SUCCEEDED(privateObject->FuncCall(
        0, TJS_W("setSize"), nullptr, nullptr, 2, sizeArgs, privateObject)));
    REQUIRE(privateLayer->GetWidth() == 17);
    REQUIRE(privateLayer->GetHeight() == 19);

    tTJSVariant visible(false);
    REQUIRE(TJS_SUCCEEDED(privateObject->PropSet(0, TJS_W("visible"), nullptr,
                                                 &visible, privateObject)));
    REQUIRE_FALSE(privateLayer->GetVisible());
    tTJSVariant visibleResult;
    REQUIRE(TJS_SUCCEEDED(privateObject->PropGet(
        0, TJS_W("visible"), nullptr, &visibleResult, privateObject)));
    REQUIRE(visibleResult.AsInteger() == 0);

    tTJSVariant absolute(3);
    REQUIRE(TJS_SUCCEEDED(privateObject->PropSet(0, TJS_W("absolute"), nullptr,
                                                 &absolute, privateObject)));
    REQUIRE(privateLayer->GetAbsoluteOrderIndex() == 3);

    targetLayer.object->Release();
    primaryLayer.object->Release();
}

TEST_CASE("Layer mesh divx and divy are cell counts") {
    FakeLayerTreeOwner treeOwner;
    FakeLayerOwnerDispatch ownerDispatch;
    ownerDispatch.treeOwner = &treeOwner;
    treeOwner.owner = &ownerDispatch;

    tTJSVariant ownerVariant(&ownerDispatch, &ownerDispatch);
    const auto ownerClosure = ownerVariant.AsObjectClosureNoAddRef();
    auto primaryLayer =
        createRegisteredTestLayer(&treeOwner, nullptr, ownerClosure);
    auto sourceLayer = createRegisteredTestLayer(
        &treeOwner, primaryLayer.native, ownerClosure);
    auto targetLayer = createRegisteredTestLayer(
        &treeOwner, primaryLayer.native, ownerClosure);

    const tTVPRect rect(0, 0, 4, 4);
    sourceLayer.native->SetType(ltAlpha);
    sourceLayer.native->SetSize(4, 4);
    sourceLayer.native->FillRect(rect, 0xffff0000u);
    targetLayer.native->SetType(ltAlpha);
    targetLayer.native->SetSize(4, 4);
    targetLayer.native->FillRect(rect, 0x00000000u);

    const std::array<tTVPPointD, 9> twoByTwoCells{{
        {0.0, 0.0}, {2.0, 0.0}, {4.0, 0.0},
        {0.0, 2.0}, {2.0, 2.0}, {4.0, 2.0},
        {0.0, 4.0}, {2.0, 4.0}, {4.0, 4.0},
    }};
    const auto twoByTwoBottomRightBefore =
        targetLayer.native->GetMainImage()->GetPoint(3, 3);
    targetLayer.native->MeshCopy(
        twoByTwoCells.data(), 2, 2, sourceLayer.native->GetMainImage(), rect,
        stNearest, false);
    REQUIRE(targetLayer.native->GetMainImage()->GetPoint(3, 3) !=
            twoByTwoBottomRightBefore);

    targetLayer.native->FillRect(rect, 0x00000000u);
    const std::array<tTVPPointD, 4> oneByOneCell{{
        {0.0, 0.0}, {4.0, 0.0}, {0.0, 4.0}, {4.0, 4.0},
    }};
    const auto oneByOneBottomRightBefore =
        targetLayer.native->GetMainImage()->GetPoint(3, 3);
    targetLayer.native->MeshCopy(
        oneByOneCell.data(), 1, 1, sourceLayer.native->GetMainImage(), rect,
        stNearest, false);
    REQUIRE(targetLayer.native->GetMainImage()->GetPoint(3, 3) !=
            oneByOneBottomRightBefore);

    const auto patchDivisions =
        motion::internal::render_detail::
            bezierPatchCellDivisionsLike_0x6C5C00(10, 8.0, 2.0);
    const std::array<tjs_int, 2> expectedPatchDivisions{9, 3};
    REQUIRE(patchDivisions == expectedPatchDivisions);
    const auto zeroExtentDivisions =
        motion::internal::render_detail::
            bezierPatchCellDivisionsLike_0x6C5C00(10, 0.0, 0.0);
    const std::array<tjs_int, 2> expectedZeroExtentDivisions{1, 11};
    REQUIRE(zeroExtentDivisions == expectedZeroExtentDivisions);

    const auto floatingNaNDivisions =
        motion::internal::render_detail::
            bezierPatchCellDivisionsLike_0x6C5C00(
                10, std::numeric_limits<double>::infinity(), 1.0);
    REQUIRE(floatingNaNDivisions == expectedZeroExtentDivisions);
    const auto floatingPositiveInfinityDivisions =
        motion::internal::render_detail::
            bezierPatchCellDivisionsLike_0x6C5C00(10, 2.0, -2.0);
    const std::array<tjs_int, 2> expectedFloatingPositiveInfinity{
        std::numeric_limits<tjs_int>::min(), -2147483636
    };
    REQUIRE(floatingPositiveInfinityDivisions ==
            expectedFloatingPositiveInfinity);

    const auto unsignedPatchDivisions =
        motion::internal::render_detail::
            bezierPatchCellDivisionsU32Like_0x6C8E5C(10, 8.0, 2.0);
    REQUIRE(unsignedPatchDivisions == expectedPatchDivisions);
    const auto unsignedNegativeWidthDivisions =
        motion::internal::render_detail::
            bezierPatchCellDivisionsU32Like_0x6C8E5C(10, -1.0, 2.0);
    REQUIRE(unsignedNegativeWidthDivisions == expectedZeroExtentDivisions);
    const auto unsignedNaNWidthDivisions =
        motion::internal::render_detail::
            bezierPatchCellDivisionsU32Like_0x6C8E5C(
                10, std::numeric_limits<double>::quiet_NaN(), 2.0);
    REQUIRE(unsignedNaNWidthDivisions == expectedZeroExtentDivisions);

    targetLayer.object->Release();
    sourceLayer.object->Release();
    primaryLayer.object->Release();
}

TEST_CASE("D3DAdaptor constructor follows libkrkr2 parameter boundary") {
    motion::D3DAdaptor *badCountAdaptor = nullptr;
    REQUIRE(motion::D3DAdaptor::factory(&badCountAdaptor, 4, nullptr,
                                        nullptr) == TJS_E_BADPARAMCOUNT);
    REQUIRE(badCountAdaptor == nullptr);

    FakeObjectDispatch object;
    tTJSVariant objectArg(&object, &object);
    tTJSVariant width(640);
    tTJSVariant height(480);
    tTJSVariant centerX(320);
    tTJSVariant centerY(240);
    tTJSVariant *nonWindowParams[] = {
        &objectArg, &width, &height, &centerX, &centerY,
    };

    motion::D3DAdaptor *nonWindowAdaptor = nullptr;
    bool threwWindowError = false;
    try {
        (void)motion::D3DAdaptor::factory(&nonWindowAdaptor, 5, nonWindowParams,
                                          nullptr);
    } catch(const eTJSError &e) {
        threwWindowError = true;
        REQUIRE(e.GetMessage() == ttstr(TJS_W("must set Window object")));
    }
    REQUIRE(threwWindowError);
    REQUIRE(nonWindowAdaptor == nullptr);

    FakeWindowDispatch windowObject;
    tTJSVariant windowArg(&windowObject, &windowObject);
    tTJSVariant validWidth(1024);
    tTJSVariant validHeight(768);
    tTJSVariant validCenterX(512);
    tTJSVariant validCenterY(384);
    tTJSVariant *validParams[] = {
        &windowArg, &validWidth, &validHeight, &validCenterX, &validCenterY,
    };

    motion::D3DAdaptor *rawAdaptor = nullptr;
    REQUIRE(motion::D3DAdaptor::factory(&rawAdaptor, 5, validParams, nullptr) ==
            TJS_S_OK);
    REQUIRE(rawAdaptor != nullptr);
    std::unique_ptr<motion::D3DAdaptor> adaptor(rawAdaptor);
    REQUIRE(adaptor->getWindowObject() == &windowObject);
    REQUIRE(adaptor->getWidth() == 1024);
    REQUIRE(adaptor->getHeight() == 768);
    REQUIRE(adaptor->getCenterX() == 512);
    REQUIRE(adaptor->getCenterY() == 384);
    REQUIRE(adaptor->getBufferSize() == 1024u * 768u * 4u);
    REQUIRE_FALSE(adaptor->getVisible());
    REQUIRE_FALSE(adaptor->getCanvasCaptureEnabled());
    REQUIRE(adaptor->getClearEnabled());
    REQUIRE_FALSE(adaptor->getAlphaOpAdd());
    REQUIRE(adaptor->hasTargetTexture());
    REQUIRE(adaptor->targetTexture()->GetWidth() == 1024);
    REQUIRE(adaptor->targetTexture()->GetHeight() == 768);

    adaptor->setSize(320, 200);
    REQUIRE(adaptor->getWidth() == 320);
    REQUIRE(adaptor->getHeight() == 200);
    REQUIRE(adaptor->getCenterX() == 512);
    REQUIRE(adaptor->getCenterY() == 384);
    REQUIRE(adaptor->getBufferSize() == 320u * 200u * 4u);
    REQUIRE(adaptor->hasTargetTexture());
    REQUIRE(adaptor->targetTexture()->GetWidth() == 320);
    REQUIRE(adaptor->targetTexture()->GetHeight() == 200);
}

TEST_CASE("D3DAdaptor captureCanvas reads back target texture rows") {
    FakeWindowDispatch windowObject;
    tTJSVariant windowArg(&windowObject, &windowObject);
    tTJSVariant width(2);
    tTJSVariant height(2);
    tTJSVariant centerX(1);
    tTJSVariant centerY(1);
    tTJSVariant *validParams[] = {
        &windowArg, &width, &height, &centerX, &centerY,
    };

    motion::D3DAdaptor *rawAdaptor = nullptr;
    REQUIRE(motion::D3DAdaptor::factory(&rawAdaptor, 5, validParams, nullptr) ==
            TJS_S_OK);
    REQUIRE(rawAdaptor != nullptr);
    std::unique_ptr<motion::D3DAdaptor> adaptor(rawAdaptor);
    REQUIRE(adaptor->hasTargetTexture());

    auto *texture = adaptor->targetTexture();
    REQUIRE(texture != nullptr);
    auto *row0 = static_cast<std::uint8_t *>(texture->GetScanLineForWrite(0));
    auto *row1 = static_cast<std::uint8_t *>(texture->GetScanLineForWrite(1));
    REQUIRE(row0 != nullptr);
    REQUIRE(row1 != nullptr);
    REQUIRE(texture->GetPitch() >= 8);
    const std::uint8_t expectedRow0[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    const std::uint8_t expectedRow1[] = { 9, 10, 11, 12, 13, 14, 15, 16 };
    std::copy(std::begin(expectedRow0), std::end(expectedRow0), row0);
    std::copy(std::begin(expectedRow1), std::end(expectedRow1), row1);

    std::array<std::uint8_t, 32> captured{};
    constexpr tjs_int dstPitch = 16;
    REQUIRE(adaptor->copyTargetTextureRowsForCaptureLike_0x6AD92C(
        captured.data(), dstPitch));
    const auto *dstRow0 = captured.data();
    REQUIRE(
        std::equal(std::begin(expectedRow0), std::end(expectedRow0), dstRow0));
    REQUIRE(std::equal(std::begin(expectedRow1), std::end(expectedRow1),
                       dstRow0 + dstPitch));
}

TEST_CASE("software texture updates packed atlas sub-rects in place") {
    auto *texture = TVPGetRenderManager()->CreateTexture2D(
        nullptr, 4 * 4, 4, 4, TVPTextureFormat::RGBA,
        RENDER_CREATE_TEXTURE_FLAG_ANY);
    REQUIRE(texture != nullptr);

    constexpr tjs_uint32 untouched = 0xA5A5A5A5u;
    for(int y = 0; y < 4; ++y) {
        for(int x = 0; x < 4; ++x)
            texture->SetPoint(x, y, untouched);
    }

    const std::array<tjs_uint32, 6> pixelsWithRowPadding = {
        0x01020304u, 0x11121314u, 0xDEADBEEFu,
        0x21222324u, 0x31323334u, 0xBAADF00Du,
    };
    // sub_695DE8 @ 0x696BE0..0x696C0C passes the packed atlas x/y as
    // tTVPRect left/top while the source pointer starts at its own (0,0).
    texture->Update(pixelsWithRowPadding.data(), TVPTextureFormat::RGBA,
                    3 * 4,
                    tTVPRect(1, 1, 3, 3));

    for(int y = 0; y < 4; ++y) {
        for(int x = 0; x < 4; ++x) {
            tjs_uint32 expected = untouched;
            if(x == 1 && y == 1)
                expected = pixelsWithRowPadding[0];
            else if(x == 2 && y == 1)
                expected = pixelsWithRowPadding[1];
            else if(x == 1 && y == 2)
                expected = pixelsWithRowPadding[3];
            else if(x == 2 && y == 2)
                expected = pixelsWithRowPadding[4];
            REQUIRE(texture->GetPoint(x, y) == expected);
        }
    }
    texture->Release();
}

TEST_CASE("KRKR D3D source path builds and uploads the production atlas") {
    ScopedCoreScriptEngine scriptEngine;
    ScopedMaxTextureSize maxTextureSize(
        std::max(TVPMaxTextureSize, 4096u));
    ensureTVPGLInitialized();
    setEmoteSeed();

    const auto motionPath = motionFixturePath();
    motion::ResourceManager manager;
    (void)manager.load(motionPath);

    const auto managerDispatch = makeResourceManagerDispatch(manager);
    motion::Player player(managerDispatch);
    player.setProject(tTJSVariant(motionPath));
    player.setChara(TJS_W("body_parts"));
    player.setUseD3D(true);
    REQUIRE(player.playMotionLike_0x6B2284(
        TJS_W("下半身変形基礎"), motion::PlayFlagForce));

    // The KRKR atlas miss in sub_695DE8 enumerates and decodes every source
    // icon before publishing the requested entry.  This fixture contains 42
    // fully-transparent icons and 72 mixed-alpha icons, so one ordinary source
    // resolution executes both the transparent early-free path and the packed
    // sub-rect Update/free path without manufacturing test data.
    player.progressMsLike_0x6D2A54(16.0);

    const motion::detail::MotionNode::SourceState *mixedAlpha = nullptr;
    for(const auto &node : player.nodes()) {
        const auto &source = node.source;
        if(!source.valid || source.texture == nullptr) {
            continue;
        }

        const int left = source.textureRect[0];
        const int top = source.textureRect[1];
        const int right = source.textureRect[2];
        const int bottom = source.textureRect[3];
        const int width = right - left;
        const int height = bottom - top;
        REQUIRE(width > 0);
        REQUIRE(height > 0);
        REQUIRE(left >= 0);
        REQUIRE(top >= 0);
        REQUIRE(right <= static_cast<int>(source.texture->GetWidth()));
        REQUIRE(bottom <= static_cast<int>(source.texture->GetHeight()));

        bool hasTransparentPixel = false;
        bool hasVisiblePixel = false;
        for(int y = 0; y < height; ++y) {
            for(int x = 0; x < width; ++x) {
                const auto pixel =
                    source.texture->GetPoint(left + x, top + y);
                const auto alpha =
                    static_cast<std::uint8_t>(pixel >> 24u);
                hasTransparentPixel |= alpha == 0;
                hasVisiblePixel |= alpha != 0;
            }
        }
        if(hasTransparentPixel && hasVisiblePixel) {
            mixedAlpha = &source;
            break;
        }
    }

    REQUIRE(mixedAlpha != nullptr);
    REQUIRE(mixedAlpha->path.rfind("src/", 0) == 0);
    REQUIRE(mixedAlpha->texture != nullptr);
}

TEST_CASE("Player variableKeys returns a fresh var-track array") {
    motion::Player player;

    const auto first = player.getVariableKeys();
    const auto second = player.getVariableKeys();
    REQUIRE(first.Type() == tvtObject);
    REQUIRE(second.Type() == tvtObject);
    // Player_getVariableKeys @0x6D139C creates a fresh empty Array even when
    // Player+1296 has no VariableLabelScope entries.
    REQUIRE(first.AsObjectNoAddRef() != second.AsObjectNoAddRef());
    REQUIRE(variantCount(first) == 0);
    REQUIRE(variantCount(second) == 0);
}

TEST_CASE("LayerGetter is a live non-owning MotionNode facade") {
    ScopedCoreScriptEngine scriptEngine;
    motion::Player player;
    auto &node = player.nodesForBuild().emplace_back();

    const auto getters = player.getLayerGetterList();
    REQUIRE(variantCount(getters) ==
            static_cast<tjs_int>(player.nodes().size() - 1));
    const auto getter = getIndex(getters, 0);
    REQUIRE(getter.Type() == tvtObject);
    REQUIRE(getProp(getter, TJS_W("x")).AsReal() == Catch::Approx(0.0));

    // Every property reads the borrowed node at access time; none of these
    // values existed when the LayerGetter adaptor above was created.
    node.activeSlotIndex = 1;
    node.slots[0].srcValue = TJS_W("inactive");
    node.slots[1].srcValue = TJS_W("active");
    node.slots[1].ox = 12.5;
    node.slots[1].oy = -7.25;
    node.source.originX = 900.0;
    node.source.originY = 901.0;
    node.accumulated.posX = 3.0;
    node.accumulated.posY = 4.0;
    node.accumulated.posZ = 5.0;
    node.accumulated.m11 = 1.25;
    node.accumulated.m12 = 2.25;
    node.accumulated.m21 = 3.25;
    node.accumulated.m22 = 4.25;
    node.accumulated.angle = 90.0;

    REQUIRE(ttstr(getProp(getter, TJS_W("src"))) == TJS_W("active"));
    REQUIRE(getProp(getter, TJS_W("x")).AsReal() == Catch::Approx(3.0));
    REQUIRE(getProp(getter, TJS_W("originX")).AsReal() ==
            Catch::Approx(12.5));
    REQUIRE(getProp(getter, TJS_W("originY")).AsReal() ==
            Catch::Approx(-7.25));
    REQUIRE(getProp(getter, TJS_W("angleDeg")).AsReal() ==
            Catch::Approx(90.0));
    REQUIRE(getProp(getter, TJS_W("angleRad")).AsReal() ==
            Catch::Approx(3.14159265358979323846 / 2.0));

    const auto coord = getProp(getter, TJS_W("coord"));
    REQUIRE(variantCount(coord) == 3);
    REQUIRE(getIndex(coord, 0).AsReal() == Catch::Approx(3.0));
    REQUIRE(getIndex(coord, 1).AsReal() == Catch::Approx(4.0));
    REQUIRE(getIndex(coord, 2).AsReal() == Catch::Approx(5.0));

    const auto mtx = getProp(getter, TJS_W("mtx"));
    REQUIRE(variantCount(mtx) == 4);
    REQUIRE(getIndex(mtx, 0).AsReal() == Catch::Approx(1.25));
    REQUIRE(getIndex(mtx, 1).AsReal() == Catch::Approx(2.25));
    REQUIRE(getIndex(mtx, 2).AsReal() == Catch::Approx(3.25));
    REQUIRE(getIndex(mtx, 3).AsReal() == Catch::Approx(4.25));

    const float vertices[8] = {1.0f, 2.0f, 3.0f, 4.0f,
                               5.0f, 6.0f, 7.0f, 8.0f};
    std::memcpy(node.vertices, vertices, sizeof(vertices));
    const auto vtx = getProp(getter, TJS_W("vtx"));
    REQUIRE(variantCount(vtx) == 4);
    for(tjs_int i = 0; i < 4; ++i) {
        const auto point = getIndex(vtx, i);
        REQUIRE(getProp(point, TJS_W("x")).AsReal() ==
                Catch::Approx(vertices[i * 2]));
        REQUIRE(getProp(point, TJS_W("y")).AsReal() ==
                Catch::Approx(vertices[i * 2 + 1]));
    }

    const std::uint32_t colors[4] = {
        0xFF808080u, 0x01020304u, 0x89ABCDEFu, 0x7F102030u
    };
    std::memcpy(node.colorBytes, colors, sizeof(colors));
    const auto color = getProp(getter, TJS_W("color"));
    REQUIRE(variantCount(color) == 4);
    for(tjs_int i = 0; i < 4; ++i) {
        REQUIRE(getIndex(color, i).AsInteger() ==
                static_cast<tjs_int64>(colors[i]));
    }

    REQUIRE(getProp(getter, TJS_W("bezierPatch")).Type() == tvtVoid);
    node.meshType = 1;
    node.meshControlPoints = {{1.25f, -2.5f}, {3.75f, 4.5f}};
    const auto patch = getProp(getter, TJS_W("bezierPatch"));
    REQUIRE(variantCount(patch) == 4);
    REQUIRE(getIndex(patch, 0).AsReal() == Catch::Approx(1.25));
    REQUIRE(getIndex(patch, 1).AsReal() == Catch::Approx(-2.5));
    REQUIRE(getIndex(patch, 2).AsReal() == Catch::Approx(3.75));
    REQUIRE(getIndex(patch, 3).AsReal() == Catch::Approx(4.5));

    node.accumulated.visible = true;
    node.accumulated.active = false;
    node.drawFlag = true;
    REQUIRE_FALSE(getProp(getter, TJS_W("layerVisible")).operator bool());
    node.accumulated.active = true;
    REQUIRE(getProp(getter, TJS_W("layerVisible")).operator bool());
}

TEST_CASE("getCommandList keeps persistent aliases and native array order") {
    ScopedCoreScriptEngine scriptEngine;
    setEmoteSeed();

    const auto motionPath = motionFixturePath();
    motion::ResourceManager manager;
    const auto module = manager.load(motionPath);
    const auto metadata = getProp(module, TJS_W("metadata"));
    const auto base = getProp(metadata, TJS_W("base"));
    const ttstr chara(getProp(base, TJS_W("chara")));
    const ttstr motionName(getProp(base, TJS_W("motion")));
    const auto rootContent =
        firstPriorityContent(module, chara, motionName);

    const auto managerDispatch = makeResourceManagerDispatch(manager);
    motion::Player player(managerDispatch);
    player.setProject(tTJSVariant(motionPath));
    player.setChara(chara);
    REQUIRE(player.playMotionLike_0x6B2284(
        motionName, motion::PlayFlagForce));
    REQUIRE(player.hasMotionContent());

    const auto nativeOrder =
        rebuildInactivePrioritySkeleton(player, rootContent);
    REQUIRE(nativeOrder.size() >= 3);
    auto &nodes = player.nodesForBuild();
    auto &root = nodes.front();
    REQUIRE(root.preparedRenderItem == nullptr);
    auto &parent = nodes[static_cast<size_t>(nativeOrder[0])];
    auto &childA = nodes[static_cast<size_t>(nativeOrder[1])];
    auto &childB = nodes[static_cast<size_t>(nativeOrder[2])];

    const auto configureRenderable =
        [](motion::detail::MotionNode &node, int index,
           const ttstr &src, double sortKey, int meshType) {
            node.index = index;
            node.parentIndex = 0;
            node.nodeType = 0;
            node.accumulated.active = true;
            node.accumulated.visible = true;
            node.accumulated.posX = static_cast<double>(index);
            node.accumulated.posY = static_cast<double>(index * 2);
            node.accumulated.posZ = sortKey;
            node.accumulated.opacity = 255;
            node.accumulated.blendMode = 16;
            node.drawFlag = true;
            node.visibleAncestorIndex = -1;

            node.source.valid = true;
            node.source.blank = false;
            // Deliberately not a String: command.src must come from the
            // active clip-slot ttstr, never this Web boundary payload.
            node.source.object =
                tTJSVariant(static_cast<tjs_int>(9000 + index));
            node.source.path = "decoy-source-" + std::to_string(index);
            node.source.width = 8.0;
            node.source.height = 8.0;
            node.activeSlot().srcValue = src;
            node.meshType = meshType;

            const float vertices[8] = {
                0.0f, 0.0f, 8.0f, 0.0f,
                8.0f, 8.0f, 0.0f, 8.0f
            };
            std::memcpy(node.vertices, vertices, sizeof(vertices));
        };

    configureRenderable(parent, 1, TJS_W("親/合成.png"), 0.0, 0);
    parent.nodeType = 12;
    parent.stencilTypeBase = 4;
    parent.stencilType = 4;

    configureRenderable(childA, 2, TJS_W("子A/α.png"), 20.0, 0);
    configureRenderable(childB, 3, TJS_W("子B/β.png"), -20.0, 3);
    // sub_698188 @0x698188: a non-unit source clip bilinearly remaps the
    // accumulated four corner colors with 8-bit fixed-point W32 arithmetic.
    const std::uint32_t childAColors[4] = {
        0x00000000u, 0x40404040u, 0x80808080u, 0xFFFFFFFFu
    };
    std::memcpy(childA.colorBytes, childAColors, sizeof(childAColors));
    childA.source.clipLeft = 0.25;
    childA.source.clipTop = 0.25;
    childA.source.clipRight = 0.75;
    childA.source.clipBottom = 0.75;
    childA.stencilCompositeMaskReferenced = true;
    childB.stencilCompositeMaskReferenced = true;
    childA.visibleAncestorIndex = parent.index;
    childB.visibleAncestorIndex = parent.index;

    // Reverse raw node-generation order. The native type12 post-pass must
    // consume node+2600 directly; scanning main entries would produce A,B.
    parent.stencilCompositeMaskNodes = { &childB, &childA };
    player.setMotionKey(tTJSVariant(static_cast<tjs_int>(12345)));

    const auto firstResult = player.getCommandList();
    const auto &firstItems = requireArrayNI(firstResult)->Items;
    REQUIRE(firstItems.size() == 2);

    auto *const parentItem = parent.preparedRenderItem;
    auto *const itemA = childA.preparedRenderItem;
    auto *const itemB = childB.preparedRenderItem;
    REQUIRE(parentItem != nullptr);
    REQUIRE(itemA != nullptr);
    REQUIRE(itemB != nullptr);

    REQUIRE(parentItem->skipFlag0);
    REQUIRE(parentItem->childItems.size() == 3);
    REQUIRE(parentItem->childItems[0] == parentItem);
    REQUIRE(parentItem->childItems[1] == itemB);
    REQUIRE(parentItem->childItems[2] == itemA);
    REQUIRE(itemA->parentItem == parentItem);
    REQUIRE(itemB->parentItem == parentItem);

    const tTJSVariant parentCommand = parentItem->commandVariant;
    const tTJSVariant commandA = itemA->commandVariant;
    const tTJSVariant commandB = itemB->commandVariant;
    requireSameObject(firstItems[0], commandB);
    requireSameObject(firstItems[1], commandA);

    // item+17 filters the type12 parent only after first-pass command build.
    REQUIRE(parentCommand.Type() == tvtObject);
    REQUIRE(ttstr(getProp(parentCommand, TJS_W("src"))) ==
            TJS_W("親/合成.png"));

    const auto key = getProp(commandA, TJS_W("key"));
    const auto srcA = getProp(commandA, TJS_W("src"));
    const auto srcB = getProp(commandB, TJS_W("src"));
    REQUIRE(key.Type() == tvtString);
    REQUIRE(ttstr(key) == TJS_W("12345"));
    REQUIRE(srcA.Type() == tvtString);
    REQUIRE(srcB.Type() == tvtString);
    REQUIRE(ttstr(srcA) == TJS_W("子A/α.png"));
    REQUIRE(ttstr(srcB) == TJS_W("子B/β.png"));

    REQUIRE(requireArrayNI(getProp(commandA, TJS_W("coord")))->
                Items.size() == 3);
    REQUIRE(requireArrayNI(getProp(commandA, TJS_W("mtx")))->
                Items.size() == 4);
    const auto commandAColor = getProp(commandA, TJS_W("color"));
    REQUIRE(requireArrayNI(commandAColor)->Items.size() == 4);
    const std::uint32_t clippedColors[4] = {
        0x33333333u, 0x5B5B5B5Bu, 0x7B7B7B7Bu, 0xB3B3B3B3u
    };
    for(tjs_int i = 0; i < 4; ++i) {
        REQUIRE(getIndex(commandAColor, i).AsInteger() ==
                static_cast<tjs_int64>(clippedColors[i]));
    }

    const auto chain = getProp(commandB, TJS_W("stencilChain"));
    const auto &chainItems = requireArrayNI(chain)->Items;
    REQUIRE(chainItems.size() == 1);
    const auto link = chainItems.front();
    REQUIRE(getProp(link, TJS_W("type")).AsInteger() == 4);
    const auto mesh = getProp(link, TJS_W("mesh"));
    const auto &meshItems = requireArrayNI(mesh)->Items;
    REQUIRE(meshItems.size() == 3);
    requireSameObject(meshItems[0], parentCommand);
    requireSameObject(meshItems[1], commandB);
    requireSameObject(meshItems[2], commandA);

    // meshType > 2 emits neither mesh payload branch.
    REQUIRE(getProp(commandB, TJS_W("meshTransform")).AsInteger() == 3);
    REQUIRE_FALSE(hasDictionaryKey(commandB, TJS_W("bezierPatch")));
    REQUIRE_FALSE(hasDictionaryKey(commandB, TJS_W("compositeMesh")));

    // A second query reuses each node-owned native item, but replaces its
    // persistent item+284 Variant in place and rebuilds aliases to the new set.
    const auto secondResult = player.getCommandList();
    REQUIRE(parent.preparedRenderItem == parentItem);
    REQUIRE(childA.preparedRenderItem == itemA);
    REQUIRE(childB.preparedRenderItem == itemB);
    REQUIRE(parentItem->commandVariant.AsObjectNoAddRef() !=
            parentCommand.AsObjectNoAddRef());
    REQUIRE(itemA->commandVariant.AsObjectNoAddRef() !=
            commandA.AsObjectNoAddRef());
    REQUIRE(itemB->commandVariant.AsObjectNoAddRef() !=
            commandB.AsObjectNoAddRef());

    const auto &secondItems = requireArrayNI(secondResult)->Items;
    REQUIRE(secondItems.size() == 2);
    requireSameObject(secondItems[0], itemB->commandVariant);
    requireSameObject(secondItems[1], itemA->commandVariant);
    const auto secondChain =
        getProp(secondItems[0], TJS_W("stencilChain"));
    const auto secondLink = requireArrayNI(secondChain)->Items.front();
    const auto secondMesh = getProp(secondLink, TJS_W("mesh"));
    const auto &secondMeshItems = requireArrayNI(secondMesh)->Items;
    REQUIRE(secondMeshItems.size() == 3);
    requireSameObject(secondMeshItems[0], parentItem->commandVariant);
    requireSameObject(secondMeshItems[1], itemB->commandVariant);
    requireSameObject(secondMeshItems[2], itemA->commandVariant);
}

TEST_CASE("type-3 render builder preserves native wrapper topology") {
    ScopedCoreScriptEngine scriptEngine;
    setEmoteSeed();

    const auto motionPath = motionFixturePath();
    motion::ResourceManager manager;
    const auto module = manager.load(motionPath);
    const auto metadata = getProp(module, TJS_W("metadata"));
    const auto base = getProp(metadata, TJS_W("base"));
    const ttstr chara(getProp(base, TJS_W("chara")));
    const ttstr motionName(getProp(base, TJS_W("motion")));
    const auto rootContent =
        firstPriorityContent(module, chara, motionName);
    const auto managerDispatch = makeResourceManagerDispatch(manager);

    const auto loadPlayer = [&](motion::Player &player) {
        player.setProject(tTJSVariant(motionPath));
        player.setChara(chara);
        REQUIRE(player.playMotionLike_0x6B2284(
            motionName, motion::PlayFlagForce));
        REQUIRE(player.hasMotionContent());
    };

    motion::Player parent(managerDispatch);
    loadPlayer(parent);
    const auto parentOrder =
        rebuildInactivePrioritySkeleton(parent, rootContent);
    REQUIRE(parentOrder.size() >= 2);

    auto *child = new motion::Player(managerDispatch);
    loadPlayer(*child);
    const auto childOrder =
        rebuildInactivePrioritySkeleton(*child, rootContent);
    REQUIRE_FALSE(childOrder.empty());
    using PlayerAdaptor = ncbInstanceAdaptor<motion::Player>;
    auto *childDispatch = PlayerAdaptor::CreateAdaptor(child);
    REQUIRE(childDispatch != nullptr);

    auto &parentNodes = parent.nodesForBuild();
    auto &ancestor = parentNodes[
        static_cast<size_t>(parentOrder[0])];
    auto &motionNode = parentNodes[
        static_cast<size_t>(parentOrder[1])];
    ancestor.parentIndex = 0;
    ancestor.nodeType = 0;
    ancestor.accumulated.active = false;

    motionNode.parentIndex = 0;
    motionNode.nodeType = 3;
    motionNode.accumulated.active = true;
    motionNode.bounds[0] = 10.0f;
    motionNode.bounds[1] = 20.0f;
    motionNode.bounds[2] = 30.0f;
    motionNode.bounds[3] = 40.0f;
    motionNode.childPlayerVar =
        tTJSVariant(childDispatch, childDispatch);
    childDispatch->Release();

    auto &childNodes = child->nodesForBuild();
    auto &leaf = childNodes[
        static_cast<size_t>(childOrder[0])];
    leaf.parentIndex = 0;
    leaf.nodeType = 0;
    leaf.accumulated.active = true;
    leaf.accumulated.visible = true;
    leaf.accumulated.opacity = 255;
    leaf.accumulated.blendMode = 16;
    leaf.drawFlag = true;
    leaf.source.valid = true;
    leaf.source.path = "type3-child";
    leaf.source.width = 4.0;
    leaf.source.height = 4.0;
    leaf.activeSlot().srcValue = TJS_W("type3-child.png");
    const float leafVertices[8] = {
        0.0f, 0.0f, 4.0f, 0.0f,
        4.0f, 4.0f, 0.0f, 4.0f
    };
    std::memcpy(leaf.vertices, leafVertices, sizeof(leafVertices));

    // 0x6C272C..0x6C3124: no drawFlag/maskRef means direct recursion;
    // there is no wrapper allocation or auxiliary-list entry.
    motionNode.drawFlag = false;
    motionNode.stencilCompositeMaskReferenced = false;
    (void)parent.getCommandList();
    REQUIRE(motionNode.preparedRenderItem == nullptr);
    REQUIRE_FALSE(motionNode.drawnThisFrame);
    REQUIRE(leaf.preparedRenderItem != nullptr);

    // 0x6C273C..0x6C2B74: maskRef alone builds a persistent wrapper and
    // routes the child build through wrapper.childItems with a5=1, but does
    // not append the wrapper to aux and leaves parentItem null.
    motionNode.stencilCompositeMaskReferenced = true;
    (void)parent.getCommandList();
    auto *const wrapper = motionNode.preparedRenderItem;
    REQUIRE(wrapper != nullptr);
    REQUIRE(motionNode.drawnThisFrame);
    REQUIRE(wrapper->childItems.size() == 1);
    REQUIRE(wrapper->childItems[0] == leaf.preparedRenderItem);
    REQUIRE(wrapper->parentItem == nullptr);
    REQUIRE(leaf.preparedRenderItem->drawFlag);

    // drawFlag additionally pushes that SAME wrapper pointer into aux and
    // resolves item+264 from the visible ancestor's persistent item.
    motionNode.stencilCompositeMaskReferenced = false;
    motionNode.drawFlag = true;
    motionNode.visibleAncestorIndex = ancestor.index;
    (void)parent.getCommandList();
    REQUIRE(motionNode.preparedRenderItem == wrapper);
    REQUIRE(ancestor.preparedRenderItem != nullptr);
    REQUIRE(wrapper->parentItem == ancestor.preparedRenderItem);
    REQUIRE(wrapper->childItems.size() == 1);
    REQUIRE(wrapper->childItems[0] == leaf.preparedRenderItem);
}

TEST_CASE("motionplayer resource chain and query surface") {
    ScopedCoreScriptEngine scriptEngine;
    setEmoteSeed();

    const auto motionPath = motionFixturePath();
    motion::ResourceManager manager;
    const auto module = manager.load(motionPath);
    const auto metadata = getProp(module, TJS_W("metadata"));
    const auto base = getProp(metadata, TJS_W("base"));
    const ttstr chara(getProp(base, TJS_W("chara")));
    const ttstr motionName(getProp(base, TJS_W("motion")));

    const auto managerDispatch = makeResourceManagerDispatch(manager);
    motion::Player player(managerDispatch);
    player.setProject(tTJSVariant(motionPath));
    player.setChara(chara);

    REQUIRE(player.isExistMotion(motionName));
    REQUIRE_FALSE(player.isExistMotion(TJS_W("__missing_motion__")));
    REQUIRE(player.playMotionLike_0x6B2284(
        motionName, motion::PlayFlagForce));

    const auto layerNames = player.getLayerNames();
    REQUIRE(variantCount(layerNames) > 0);

    const auto firstLayer = ttstr(getIndex(layerNames, 0));
    REQUIRE_FALSE(firstLayer.IsEmpty());
    REQUIRE(player.getLayerGetter(firstLayer).Type() == tvtObject);
    // getLayerNames is label-map based and collapses duplicates, while
    // getLayerGetterList@0x6D4F88 walks every non-root flat node.
    const auto layerGetters = player.getLayerGetterList();
    REQUIRE(variantCount(layerGetters) ==
            static_cast<tjs_int>(player.nodes().size() - 1));
    REQUIRE(variantCount(layerGetters) >= variantCount(layerNames));

    bool foundMotionLayer = false;
    for(tjs_int i = 0; i < variantCount(layerNames); ++i) {
        const ttstr layerName(getIndex(layerNames, i));
        if(player.getLayerMotion(layerName).Type() == tvtObject) {
            foundMotionLayer = true;
            break;
        }
    }
    REQUIRE(foundMotionLayer);

    // P3-B (c): binary has no by-name layer-id API; allocation is the no-arg RM
    //   dispatch FuncCall (emitRenderItem@0x6C4E28 / buildNodeTree@0x6B4A6C all
    //   numparams=0). Exercise the faithful no-arg path.
    const auto firstLayerId = player.dispatchRequireLayerId();
    REQUIRE(firstLayerId > 0);
    player.releaseLayerId(firstLayerId);
    REQUIRE(player.dispatchRequireLayerId() > 0);

    REQUIRE(player.getVariableKeys().Type() == tvtObject);
}

TEST_CASE("motionplayer draw cache and playback state") {
    ScopedCoreScriptEngine scriptEngine;
    setEmoteSeed();

    const auto motionPath = motionFixturePath();
    motion::ResourceManager manager;
    const auto module = manager.load(motionPath);
    const auto metadata = getProp(module, TJS_W("metadata"));
    const auto base = getProp(metadata, TJS_W("base"));
    const ttstr chara(getProp(base, TJS_W("chara")));
    const ttstr motionName(getProp(base, TJS_W("motion")));
    const auto managerDispatch = makeResourceManagerDispatch(manager);
    motion::Player player(managerDispatch);
    player.setProject(tTJSVariant(motionPath));
    player.setChara(chara);
    REQUIRE(player.playMotionLike_0x6B2284(
        motionName, motion::PlayFlagForce));
    REQUIRE(player.findSource(TJS_W("__missing_source__")).Type() == tvtVoid);

    player.setFlip(true);
    player.setOpacity(0.5);
    player.setVisible(true);
    player.setSlant(1.25);
    player.setZoom(1.5);
    player.setClearColor(0x102030);
    player.registerBg(ttstr(TJS_W("bg")));
    player.registerCaption(ttstr(TJS_W("caption")));

    player.draw();

    player.frameProgress(16.0);
    // `frameLastTime` is Player+1128 = motion["lastTime"], not the latest dt.
    REQUIRE(player.getFrameLastTime() > 0.0);
    // Non-chain play seeds queuing+firstFrame at 0x6B3AAC. The first progress
    // reseeks and the queuing gate keeps Player+1120 frozen.
    REQUIRE(player.getTickCount() == 0.0);
    REQUIRE(player.getFrameTickCount() == 0.0);
    player.draw();

    player.clearCache();
    player.draw();
}

TEST_CASE("ResourceManager caches raw holders and returns fresh dispatches") {
    // Android constructs ResourceManager only after the global TJS engine is
    // initialized; its ctor evaluates `new Math.RandomGenerator()`.  The unit
    // host has no application command-line/data-path environment, so install
    // only the TJS core for this scoped lifecycle.
    struct ScopedScriptEngine {
        bool owns = false;
        ScopedScriptEngine() {
            if(TVPScriptEngine == nullptr) {
                TVPScriptEngine = new tTJS();
                owns = true;
            }
        }
        ~ScopedScriptEngine() {
            if(owns) {
                TVPScriptEngine->Release();
                TVPScriptEngine = nullptr;
            }
        }
    } scriptEngine;
    setEmoteSeed();

    motion::ResourceManager rm;
    const ttstr path = motionFixturePath();
    const auto first = rm.load(path);
    const auto second = rm.load(path);

    REQUIRE(first.Type() == tvtObject);
    REQUIRE(second.Type() == tvtObject);
    REQUIRE(first.AsObjectNoAddRef() != second.AsObjectNoAddRef());
    REQUIRE(ttstr(getProp(first, TJS_W("id"))) == ttstr(TJS_W("motion")));
    REQUIRE(ttstr(getProp(second, TJS_W("spec"))) == ttstr(TJS_W("krkr")));

    const tjs_int32 valueClassId =
        TJS::TJSRegisterNativeClass(TJS_W("PSBValueClass"));
    for(const tTJSVariant *module : { &first, &second }) {
        iTJSNativeInstance *native{};
        REQUIRE(module->AsObjectNoAddRef()->NativeInstanceSupport(
                    TJS_NIS_GETINSTANCE, valueClassId,
                    &native) == TJS_S_OK);
        REQUIRE(native != nullptr);
    }

    rm.unload(path);
    // The two externally retained raw dispatches own the PSBRawOwner after the
    // map holder is erased, matching the intrusive lifetime at 0x6A959C.
    REQUIRE(ttstr(getProp(first, TJS_W("id"))) == ttstr(TJS_W("motion")));
}

TEST_CASE("D3DEmotePlayer methods keep Android TODO boundaries") {
    // These six methods do not dereference the lazy EmoteObject slot in the
    // binary, so no fixture or script engine is required to test their exact
    // exception contract.
    motion::D3DEmotePlayer player(nullptr);
    const auto requireTodo = [](const auto &call, const tjs_char *message) {
        bool threw = false;
        try {
            call();
        } catch(const eTJSError &e) {
            threw = true;
            REQUIRE(e.GetMessage() == ttstr(message));
        }
        REQUIRE(threw);
    };

    requireTodo([&] { (void)player.countVariables(); },
                TJS_W("TODO: implement D3DEmotePlayer::countVariables()"));
    requireTodo([&] { (void)player.getVariableLabelAt(0); },
                TJS_W("TODO: implement D3DEmotePlayer::getVariableLabelAt()"));
    requireTodo([&] { (void)player.countVariableFrameAt(0); },
                TJS_W("TODO: implement D3DEmotePlayer::countVariableFrameAt()"));
    requireTodo(
        [&] { (void)player.getVariableFrameLabelAt(0, 0); },
        TJS_W("TODO: implement D3DEmotePlayer::getVariableFrameLabelAt()"));
    requireTodo(
        [&] { (void)player.getVariableFrameValueAt(0, 0); },
        TJS_W("TODO: implement D3DEmotePlayer::getVariableFrameValueAt()"));
    requireTodo([&] { (void)player.getOuterForce(); },
                TJS_W("TODO: implement D3DEmotePlayer::getOuterForce()"));
}

TEST_CASE("EmoteEngine serialize uses Android controller state schema") {
    motion::EmoteEngine engine{ tTJSVariant() };

    auto *eye = new motion::EmoteBlinkController();
    eye->trackValue = 12.5f;
    eye->trackTarget = 8.25f;
    eye->valueTrack8B.emplace_back(1.5f, 2.5f);
    engine._stateMachineDeque4.push_back({ eye, TJS_W("eye0") });

    engine._ctlPosition->state = 0;
    engine._ctlPosition->phase = 0.25f;
    engine._ctlPosition->currentValue[0] = 10.0f;
    engine._ctlPosition->currentValue[1] = 20.0f;
    engine._ctlPosition->targetValue[0] = 30.0f;
    engine._ctlPosition->targetValue[1] = 40.0f;
    engine._ctlPosition->startValue[0] = 50.0f;
    engine._ctlPosition->startValue[1] = 60.0f;

    engine._ctlAngle->currentRad = 0.5f;
    engine._ctlAngle->startRad = 1.5f;
    engine._ctlAngle->targetRad = 2.5f;

    const tTJSVariant saved = engine.serializeLike_0x675E40();
    REQUIRE(dictionaryEntries(saved).size() == 8);
    REQUIRE(variantCount(getProp(saved, TJS_W("timeline"))) == 0);
    REQUIRE(variantCount(getProp(saved, TJS_W("eye"))) == 1);
    REQUIRE(variantCount(getProp(saved, TJS_W("eyebrow"))) == 0);
    REQUIRE(variantCount(getProp(saved, TJS_W("mouth"))) == 0);
    REQUIRE(variantCount(getProp(saved, TJS_W("transition"))) == 0);
    REQUIRE(variantCount(getProp(saved, TJS_W("selector"))) == 0);
    REQUIRE(dictionaryEntries(getProp(saved, TJS_W("base"))).size() == 4);
    REQUIRE(dictionaryEntries(getProp(saved, TJS_W("outerforce"))).size() == 3);

    const tTJSVariant savedEye = getIndex(
        getProp(saved, TJS_W("eye")), 0);
    REQUIRE(ttstr(getProp(savedEye, TJS_W("label"))) == TJS_W("eye0"));
    REQUIRE(variantCount(getProp(savedEye, TJS_W("rq"))) == 1);

    eye->trackValue = -1.0f;
    eye->trackTarget = -2.0f;
    eye->valueTrack8B.clear();
    engine._ctlPosition->state = 1;
    engine._ctlPosition->phase = 0.0f;
    std::fill_n(engine._ctlPosition->currentValue, 2, 0.0f);
    std::fill_n(engine._ctlPosition->targetValue, 2, 0.0f);
    std::fill_n(engine._ctlPosition->startValue, 2, 0.0f);
    engine._ctlAngle->currentRad = 9.0f;
    engine._ctlAngle->startRad = 9.0f;
    engine._ctlAngle->targetRad = 9.0f;

    engine.unserializeLike_0x678044(saved);
    REQUIRE(eye->trackValue == Catch::Approx(12.5f));
    REQUIRE(eye->trackTarget == Catch::Approx(8.25f));
    REQUIRE(eye->valueTrack8B.size() == 1);
    REQUIRE(eye->valueTrack8B.front().first == Catch::Approx(1.5f));
    REQUIRE(eye->valueTrack8B.front().second == Catch::Approx(2.5f));
    REQUIRE(engine._ctlPosition->state == 0);
    REQUIRE(engine._ctlPosition->phase == Catch::Approx(0.25f));
    REQUIRE(engine._ctlPosition->currentValue[0] == Catch::Approx(10.0f));
    REQUIRE(engine._ctlPosition->currentValue[1] == Catch::Approx(20.0f));
    REQUIRE(engine._ctlPosition->targetValue[0] == Catch::Approx(30.0f));
    REQUIRE(engine._ctlPosition->startValue[1] == Catch::Approx(60.0f));
    REQUIRE(engine._ctlAngle->currentRad == Catch::Approx(0.5f));
    // sub_666A14 writes both "prev" and "target" into startRad (+92), so the
    // later target value wins and targetRad (+88) keeps its pre-restore value.
    REQUIRE(engine._ctlAngle->startRad == Catch::Approx(2.5f));
    REQUIRE(engine._ctlAngle->targetRad == Catch::Approx(9.0f));
}

TEST_CASE("emoteplayer timeline state and todo stubs") {
    ScopedCoreScriptEngine scriptEngine;
    setEmoteSeed();

    const auto motionPath = motionFixturePath();
    motion::ResourceManager rm;
    const auto module = rm.load(motionPath);
    REQUIRE(module.Type() == tvtObject);

    // Direct C++ construction bypasses the script-facing factory's D3DImage
    // type check. ScopedCoreScriptEngine has already installed global.kag for
    // EmoteObject's ResourceManager construction; nullptr here is only the
    // outer D3DImage shell argument.
    motion::D3DEmotePlayer player(nullptr);
    player.setModule(tTJSVariant(motionPath));
    const auto retainedModule = player.getModule();
    REQUIRE(retainedModule.Type() == tvtString);
    REQUIRE(TJS_strcmp(
                retainedModule.AsStringNoAddRef()->operator const tjs_char *(),
                motionPath.c_str()) == 0);

    player.setCoord(100.0, 200.0);
    player.setScale(1.0);
    player.hide();
    player.show();

    // Disjoint-map reality (libkrkr2.so, fresh-decompile 2026-06-03):
    //   D3DEmotePlayer.setVariable -> EmoteEngine_setVariable @0x671228 writes
    //   the EmoteEngine HM7 (+1440 = _labelToValueHM7; HM6-miss path
    //   @0x67135c). D3DEmotePlayer.getVariable -> Player_getVariable @0x533E1C
    //   reads the inner Player's HM1(+264)/HM2(+320) cascade — a DIFFERENT
    //   object. The two maps are bridged ONLY by the EmoteEngine_progress
    //   bind-loop (D3DEmotePlayer.progress @0x67D01C, G2-C). So in the binary,
    //   setVariable(x) immediately followed by getVariable() WITHOUT a progress
    //   in between does NOT return x. The previous immediate-equality assertion
    //   here encoded non-binary behavior produced by a now-removed Player-side
    //   double-write shim (see EmotePlayer.cpp / PlayerVariable.cpp). Assert
    //   the faithful disjoint semantics: the engine accepts the write and
    //   getVariable reflects the (un-bridged) Player-side default until
    //   progress runs the bind-loop.
    //
    //   UPDATE 2026-06-03: the live-progress wiring has now LANDED.
    //   D3DEmotePlayer::progress / pass now route through engine().progress (=
    //   EmoteEngine_progress @0x67D01C) which runs the G2-C bind-loop at step 5
    //   then the Player progress at step 7, so the bind bridge is RUNTIME-LIVE.
    //   (The prior note that D3DEmotePlayer::pass calls progressMsLike directly
    //   and bypasses the engine bind-loop is now FALSIFIED — corrected here.)
    //   The disjoint-map assertion BELOW still holds because it does
    //   setVariable then getVariable with NO progress() in between, so the HM7
    //   write has not yet been bridged to Player HM1/HM2. A progress-inclusive
    //   round-trip (setVariable -> progress -> getVariable == x) is
    //   structurally connected but only observable when the HM7 chain (+1456)
    //   is non-empty, which needs the controller/variable builder population
    //   from a motion's metadata; the logo fixture has none, so the bridge runs
    //   over an empty chain (inert). No progress-inclusive assertion is added
    //   here: this fixture cannot observe it, and per CLAUDE.md fixtures are
    //   not fabricated.
    player.setVariable(TJS_W("manual"), 3.5);
    const double manualBeforeBridge = player.getVariable(TJS_W("manual"));
    REQUIRE(manualBeforeBridge !=
            3.5); // HM7 write is not visible to Player HM2

    const auto mainCount = player.countMainTimelines();
    const auto diffCount = player.countDiffTimelines();
    REQUIRE((mainCount + diffCount) > 0);

    const auto label = mainCount > 0 ? player.getMainTimelineLabelAt(0)
                                     : player.getDiffTimelineLabelAt(0);
    REQUIRE_FALSE(label.IsEmpty());
    REQUIRE(player.getTimelineTotalFrameCount(label) >= 0);

    player.playTimeline(label, motion::TimelinePlayFlagDifference);
    REQUIRE(player.isTimelinePlaying(label));
    REQUIRE(player.getAnimating());
    REQUIRE(player.countPlayingTimelines() >= 1);
    REQUIRE(player.getPlayingTimelineLabelAt(0) == label);

    player.pass(10.0);
    REQUIRE(player.getProgress() == 10.0);

    player.fadeOutTimeline(label, 1.0, 0);
    REQUIRE(player.isTimelinePlaying(label));
    player.pass(1.0);
    REQUIRE_FALSE(player.isTimelinePlaying(label));

    player.fadeInTimeline(label, 1.0, motion::TimelinePlayFlagDifference);
    REQUIRE(player.isTimelinePlaying(label));
    player.pass(1.0);
    REQUIRE(player.getTimelineBlendRatio(label) == 1.0);

    player.skip();
    if(!player.isLoopTimeline(label)) {
        REQUIRE_FALSE(player.isTimelinePlaying(label));
    }

    player.playTimeline(label, motion::TimelinePlayFlagParallel);
    player.stopTimeline(TJS_W(""));
    REQUIRE(player.countPlayingTimelines() == 0);
    REQUIRE(player.getAnimating());

    FakeObjectDispatch stateObject;
    tTJSVariant state(&stateObject, &stateObject);
    bool assignStateThrew = false;
    try {
        player.assignState(state);
    } catch(const eTJSError &e) {
        assignStateThrew = true;
        REQUIRE(e.GetMessage() ==
                ttstr(TJS_W("TODO: implement D3DEmotePlayer::assignState()")));
    }
    REQUIRE(assignStateThrew);
    player.setOuterForce(1.0, 2.0);
}
