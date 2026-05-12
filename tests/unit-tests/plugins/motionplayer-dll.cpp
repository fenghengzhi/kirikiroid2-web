//
// Created to verify motionplayer/emoteplayer behavior aligned to libkrkr2.so.
//

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <memory>

#include "motionplayer/D3DAdaptor.h"
#include "motionplayer/EmotePlayer.h"
#include "motionplayer/Player.h"
#include "motionplayer/PrivateMotionGLL.h"
#include "motionplayer/ResourceManager.h"
#include "motionplayer/RuntimeSupport.h"
#include "motionplayer/SeparateLayerAdaptor.h"
#include "psbfile/PSBValue.h"
#include "LayerIntf.h"
#include "LayerTreeOwner.h"
#include "impl/LayerImpl.h"
#include "RenderManager.h"
#include "test_config.h"
#include "tjsError.h"
#include "tjsObject.h"
#include "tvpgl.h"

namespace {

    constexpr tjs_int kEmoteSeed = 742877301;

    ttstr motionFixturePath() {
        return ttstr(TEST_FILES_PATH "/emote/e-mote3.0バニラパジャマa.psb");
    }

    ttstr pimgFixturePath() {
        return ttstr(TEST_FILES_PATH "/emote/ezsave.pimg");
    }

    void setEmoteSeed() {
        tTJSVariant seed{kEmoteSeed};
        tTJSVariant *params[] = { &seed };
        REQUIRE(motion::ResourceManager::setEmotePSBDecryptSeed(
                    nullptr, 1, params, nullptr) == TJS_S_OK);
    }

    tTJSVariant getProp(const tTJSVariant &object, const tjs_char *name) {
        REQUIRE(object.Type() == tvtObject);
        auto *dispatch = object.AsObjectNoAddRef();
        REQUIRE(dispatch != nullptr);

        tTJSVariant result;
        REQUIRE(TJS_SUCCEEDED(dispatch->PropGet(0, name, nullptr, &result,
                                               dispatch)));
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
        return static_cast<tjs_int>(getProp(object, TJS_W("count")).AsInteger());
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

    void dumpPsbValue(const std::shared_ptr<PSB::IPSBValue> &value,
                      const std::string &prefix, int depth = 0) {
        if(!value || depth > 3) {
            return;
        }

        if(auto text = std::dynamic_pointer_cast<PSB::PSBString>(value)) {
            std::cerr << prefix << "string=" << text->value << "\n";
            return;
        }
        if(auto number = std::dynamic_pointer_cast<PSB::PSBNumber>(value)) {
            std::cerr << prefix << "number=" << number->toString() << "\n";
            return;
        }
        if(auto boolean = std::dynamic_pointer_cast<PSB::PSBBool>(value)) {
            std::cerr << prefix << "bool=" << (boolean->value ? "true" : "false")
                      << "\n";
            return;
        }
        if(auto resource = std::dynamic_pointer_cast<PSB::PSBResource>(value)) {
            std::cerr << prefix << "resource index="
                      << resource->index.value_or(UINT32_MAX)
                      << " size=" << resource->data.size() << "\n";
            return;
        }
        if(auto list = std::dynamic_pointer_cast<PSB::PSBList>(value)) {
            std::cerr << prefix << "list size=" << list->size() << "\n";
            const auto limit = std::min<size_t>(list->size(), 3);
            for(size_t index = 0; index < limit; ++index) {
                std::cerr << prefix << "  [" << index << "]\n";
                dumpPsbValue((*list)[static_cast<int>(index)], prefix + "    ",
                             depth + 1);
            }
            return;
        }
        if(auto dic = std::dynamic_pointer_cast<PSB::PSBDictionary>(value)) {
            std::cerr << prefix << "dict size="
                      << std::distance(dic->begin(), dic->end()) << "\n";
            int count = 0;
            for(const auto &[key, child] : *dic) {
                std::cerr << prefix << "  " << key << "\n";
                dumpPsbValue(child, prefix + "    ", depth + 1);
                if(++count >= 12) {
                    break;
                }
            }
            return;
        }

        std::cerr << prefix << "type=" << static_cast<int>(value->getType())
                  << " text=" << value->toString() << "\n";
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
        tjs_error IsInstanceOf(tjs_uint32, const tjs_char *,
                               tjs_uint32 *, const tjs_char *,
                               iTJSDispatch2 *) override {
            return TJS_S_FALSE;
        }
    };

    struct FakeLayerOwnerDispatch : tTJSDispatch {
        iTVPLayerTreeOwner *treeOwner = nullptr;

        tjs_error PropGet(tjs_uint32 flag,
                          const tjs_char *membername,
                          tjs_uint32 *hint,
                          tTJSVariant *result,
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
        void NotifyBitmapCompleted(iTVPLayerManager *,
                                   tjs_int,
                                   tjs_int,
                                   tTVPBaseTexture *,
                                   const tTVPRect &,
                                   tTVPLayerType,
                                   tjs_int) override {}
        void EndBitmapCompletion(iTVPLayerManager *) override {}
        void SetMouseCursor(iTVPLayerManager *, tjs_int) override {}
        void GetCursorPos(iTVPLayerManager *, tjs_int &x, tjs_int &y) override {
            x = 0;
            y = 0;
        }
        void SetCursorPos(iTVPLayerManager *, tjs_int, tjs_int) override {}
        void ReleaseMouseCapture(iTVPLayerManager *) override {}
        void SetHint(iTVPLayerManager *, iTJSDispatch2 *, const ttstr &) override {}
        void NotifyLayerResize(iTVPLayerManager *) override {}
        void NotifyLayerImageChange(iTVPLayerManager *) override {}
        void SetAttentionPoint(iTVPLayerManager *,
                               tTJSNI_BaseLayer *,
                               tjs_int,
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

    TestLayerHandle createRegisteredTestLayer(
        iTVPLayerTreeOwner *treeOwner,
        tTJSNI_BaseLayer *parent,
        const tTJSVariantClosure &ownerClosure) {
        static const bool graphicsInitialized = [] {
            TVPInitTVPGL();
            return true;
        }();
        (void)graphicsInitialized;

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
    iTJSDispatch2 *privateObject = motion::ensurePrivateMotionGLLLike_0x6D5948(
        adaptor, ownerVariant, targetVariant, targetLayer.object, 64, 32);
    REQUIRE(privateObject != nullptr);

    auto *privateLayer =
        motion::resolvePrivateMotionGLLNativeLike_0x6DE24C(privateObject);
    REQUIRE(privateLayer != nullptr);
    REQUIRE(privateLayer->GetWidth() == 64);
    REQUIRE(privateLayer->GetHeight() == 32);
    REQUIRE(privateLayer->GetVisible());
    REQUIRE(motion::privateMotionGLLRenderQueueSizeLike_0x6DE738(
                privateObject) == 0);
    motion::clearPrivateMotionGLLRenderQueueLike_0x6DE738(privateObject);
    REQUIRE(motion::privateMotionGLLRenderQueueSizeLike_0x6DE738(
                privateObject) == 0);
    motion::PrivateMotionGLLRenderItemInputLike_0x6DE738 queueItem;
    queueItem.opacity = 255;
    queueItem.sourceRect = { 0, 0, 4, 4 };
    queueItem.points = {
        { 0.0f, 0.0f },
        { 4.0f, 0.0f },
        { 0.0f, 4.0f },
    };
    motion::appendPrivateMotionGLLRenderItemLike_0x6DE738(privateObject,
                                                          queueItem);
    REQUIRE(motion::privateMotionGLLRenderQueueSizeLike_0x6DE738(
                privateObject) == 1);
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
    REQUIRE(TJS_SUCCEEDED(privateObject->PropSet(
        0, TJS_W("visible"), nullptr, &visible, privateObject)));
    REQUIRE_FALSE(privateLayer->GetVisible());
    tTJSVariant visibleResult;
    REQUIRE(TJS_SUCCEEDED(privateObject->PropGet(
        0, TJS_W("visible"), nullptr, &visibleResult, privateObject)));
    REQUIRE(visibleResult.AsInteger() == 0);

    tTJSVariant absolute(3);
    REQUIRE(TJS_SUCCEEDED(privateObject->PropSet(
        0, TJS_W("absolute"), nullptr, &absolute, privateObject)));
    REQUIRE(privateLayer->GetAbsoluteOrderIndex() == 3);

    targetLayer.object->Release();
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
        (void)motion::D3DAdaptor::factory(&nonWindowAdaptor, 5,
                                          nonWindowParams, nullptr);
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
    REQUIRE(motion::D3DAdaptor::factory(&rawAdaptor, 5, validParams,
                                        nullptr) == TJS_S_OK);
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
    REQUIRE(motion::D3DAdaptor::factory(&rawAdaptor, 5, validParams,
                                        nullptr) == TJS_S_OK);
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

    std::array<std::uint8_t, 32> captured {};
    constexpr tjs_int dstPitch = 16;
    REQUIRE(adaptor->copyTargetTextureRowsForCaptureLike_0x6AD92C(
        captured.data(), dstPitch));
    const auto *dstRow0 = captured.data();
    REQUIRE(std::equal(std::begin(expectedRow0), std::end(expectedRow0),
                       dstRow0));
    REQUIRE(std::equal(std::begin(expectedRow1), std::end(expectedRow1),
                       dstRow0 + dstPitch));
}

TEST_CASE("motionplayer resource chain and query surface") {
    setEmoteSeed();

    motion::Player player;
    const auto motionPath = motionFixturePath();
    const auto pimgPath = pimgFixturePath();

    REQUIRE_FALSE(player.isExistMotion(ttstr(TEST_FILES_PATH "/emote/missing.psb")));
    REQUIRE_FALSE(player.isExistMotion(pimgPath));
    REQUIRE(player.findMotion(pimgPath).Type() == tvtVoid);

    const auto motion = player.findMotion(motionPath);
    REQUIRE(motion.Type() == tvtObject);
    REQUIRE(player.isExistMotion(motionPath));

    const auto motions = player.motionList();
    REQUIRE(variantCount(motions) == 1);

    const auto layerNames = player.getLayerNames();
    REQUIRE(variantCount(layerNames) > 0);

    const auto firstLayer = ttstr(getIndex(layerNames, 0));
    REQUIRE_FALSE(firstLayer.IsEmpty());
    REQUIRE(player.getLayerMotion(firstLayer).Type() == tvtObject);
    REQUIRE(player.getLayerGetter(firstLayer).Type() == tvtObject);
    REQUIRE(variantCount(player.getLayerGetterList()) == variantCount(layerNames));

    const auto firstLayerId = player.requireLayerId(firstLayer);
    REQUIRE(firstLayerId > 0);
    player.releaseLayerId(firstLayerId);
    REQUIRE(player.requireLayerId(firstLayer) > 0);

    const auto mainTimelineLabels = player.getMainTimelineLabelList();
    const auto diffTimelineLabels = player.getDiffTimelineLabelList();
    REQUIRE(mainTimelineLabels.Type() == tvtObject);
    REQUIRE(diffTimelineLabels.Type() == tvtObject);

    if(variantCount(mainTimelineLabels) > 0) {
        const auto label = ttstr(getIndex(mainTimelineLabels, 0));
        REQUIRE_FALSE(label.IsEmpty());
        REQUIRE_FALSE(player.getTimelinePlaying(label));
        REQUIRE(player.getVariableFrameList(label).Type() == tvtObject);
    }

    const auto variableKeys = player.getVariableKeys();
    REQUIRE(variableKeys.Type() == tvtObject);
    if(variantCount(variableKeys) > 0) {
        const auto variableLabel = ttstr(getIndex(variableKeys, 0));
        REQUIRE(player.getVariableFrameList(variableLabel).Type() == tvtObject);
    }
}

TEST_CASE("motionplayer draw cache and playback state") {
    setEmoteSeed();

    motion::Player player;
    const auto motionPath = motionFixturePath();
    const auto pimgPath = pimgFixturePath();

    REQUIRE(player.findMotion(motionPath).Type() == tvtObject);
    REQUIRE(player.findSource(pimgPath).Type() == tvtObject);

    player.setFlip(true);
    player.setOpacity(0.5);
    player.setVisible(true);
    player.setSlant(1.25);
    player.setZoom(1.5);
    player.setClearColor(0x102030);
    player.registerBg(ttstr(TJS_W("bg")));
    player.registerCaption(ttstr(TJS_W("caption")));

    player.draw();
    const auto canvas = player.captureCanvas();
    REQUIRE(canvas.Type() == tvtObject);
    REQUIRE(getProp(canvas, TJS_W("width")).AsInteger() > 0);
    REQUIRE(getProp(canvas, TJS_W("height")).AsInteger() > 0);
    REQUIRE(getProp(canvas, TJS_W("sourceCount")).AsInteger() == 1);
    REQUIRE(getProp(canvas, TJS_W("backgroundCount")).AsInteger() == 1);
    REQUIRE(getProp(canvas, TJS_W("captionCount")).AsInteger() == 1);
    REQUIRE(getProp(canvas, TJS_W("flip")).AsInteger() == 1);
    REQUIRE(getProp(canvas, TJS_W("opacity")).AsReal() == 0.5);

    player.frameProgress(16.0);
    REQUIRE(player.getFrameLastTime() == 16.0);
    REQUIRE(player.getTickCount() == 16.0);
    REQUIRE(player.getFrameTickCount() == 1.0);
    player.draw();
    REQUIRE(getProp(player.captureCanvas(), TJS_W("sourceCount")).AsInteger() ==
            1);

    player.clearCache();
    player.draw();
    REQUIRE(getProp(player.captureCanvas(), TJS_W("sourceCount")).AsInteger() ==
            0);

    REQUIRE(player.findSource(pimgPath).Type() == tvtObject);
    player.unload(pimgPath);
    player.draw();
    REQUIRE(getProp(player.captureCanvas(), TJS_W("sourceCount")).AsInteger() ==
            0);

    player.unloadAll();
    REQUIRE(variantCount(player.motionList()) == 0);
}

TEST_CASE("emoteplayer timeline state and todo stubs") {
    setEmoteSeed();

    motion::ResourceManager rm;
    const auto module = rm.load(motionFixturePath());
    REQUIRE(module.Type() == tvtObject);

    motion::EmotePlayer player(rm);
    player.setModule(module);
    REQUIRE(player.getModule().Type() == tvtObject);

    player.setCoord(100.0, 200.0);
    player.setScale(1.0);
    REQUIRE(player.contains(100.0, 200.0));
    REQUIRE_FALSE(player.contains(99.0, 199.0));

    player.hide();
    REQUIRE_FALSE(player.contains(100.0, 200.0));
    player.show();
    REQUIRE(player.contains(100.0, 200.0));

    player.setVariable(TJS_W("manual"), 3.5);
    REQUIRE(player.getVariable(TJS_W("manual")) == 3.5);

    // After delegation to Player, countVariables returns real count from PSB.
    // The loaded PSB may or may not have variables.
    const auto varCount = player.countVariables();
    REQUIRE(varCount >= 0);
    if(varCount > 0) {
        REQUIRE_FALSE(ttstr(player.getVariableLabelAt(0)).IsEmpty());
    }
    REQUIRE(player.getOuterForce().Type() == tvtVoid);

    const auto mainCount = player.countMainTimelines();
    const auto diffCount = player.countDiffTimelines();
    REQUIRE((mainCount + diffCount) > 0);

    const auto label =
        mainCount > 0 ? player.getMainTimelineLabelAt(0)
                      : player.getDiffTimelineLabelAt(0);
    REQUIRE_FALSE(label.IsEmpty());
    REQUIRE(player.getTimelineTotalFrameCount(label) >= 0);

    player.playTimeline(label, motion::TimelinePlayFlagParallel);
    REQUIRE(player.isTimelinePlaying(label));
    REQUIRE(player.getAnimating());
    REQUIRE(player.countPlayingTimelines() >= 1);
    REQUIRE(player.getPlayingTimelineLabelAt(0) == label);

    player.pass(10.0);
    REQUIRE(player.getProgress() == 10.0);

    player.fadeOutTimeline(label, 1.0, 0);
    REQUIRE_FALSE(player.isTimelinePlaying(label));
    REQUIRE(player.getTimelineBlendRatio(label) == 0.0);

    player.fadeInTimeline(label, 1.0, motion::TimelinePlayFlagSequential);
    REQUIRE(player.isTimelinePlaying(label));
    REQUIRE(player.getTimelineBlendRatio(label) == 1.0);

    player.skip();
    if(!player.isLoopTimeline(label)) {
        REQUIRE_FALSE(player.isTimelinePlaying(label));
    }

    player.playTimeline(label, motion::TimelinePlayFlagParallel);
    player.stopTimeline(TJS_W(""));
    REQUIRE_FALSE(player.getAnimating());

    player.assignState();
    player.setOuterForce(1.0, 2.0);
}

TEST_CASE("motionplayer can play internal logo motion clips") {
    setEmoteSeed();

    const auto baseDir = std::filesystem::path(".debugtmp") / "titleprobe_hd" /
        "data1080";
    if(!std::filesystem::exists(baseDir / "yuzulogo.mtn") ||
       !std::filesystem::exists(baseDir / "m2logo.mtn")) {
        return;
    }

    motion::Player player;
    const auto yuzuPath =
        ttstr(std::filesystem::absolute(baseDir / "yuzulogo.mtn").string());
    const auto m2Path =
        ttstr(std::filesystem::absolute(baseDir / "m2logo.mtn").string());

    const auto verifyOne = [&](const ttstr &path, const ttstr &label,
                               const tjs_int expectedLayers,
                               const tjs_int expectedFrames) {
        INFO("path=" << path.AsStdString() << " label=" << label.AsStdString());
        REQUIRE(player.findMotion(path).Type() == tvtObject);
        const auto snapshot = motion::detail::lookupModuleSnapshot(
            player.findMotion(path));
        REQUIRE(snapshot != nullptr);

        const auto mainLabels = player.getMainTimelineLabelList();
        const auto diffLabels = player.getDiffTimelineLabelList();
        REQUIRE(containsString(mainLabels, label));
        REQUIRE(variantCount(diffLabels) == 0);
        REQUIRE(player.getTimelineTotalFrameCount(label) == expectedFrames);

        player.playTimeline(label, motion::PlayFlagForce);
        REQUIRE(player.getTimelinePlaying(label));
        const auto layerNames = player.getLayerNames();
        const auto getterList = player.getLayerGetterList();
        const auto commands = player.getCommandList();
        std::cerr << "logo test path=" << path.AsStdString()
                  << " label=" << label.AsStdString()
                  << " layers=" << variantCount(layerNames)
                  << " commands=" << variantCount(commands) << "\n";
        for(tjs_int index = 0; index < variantCount(commands); ++index) {
            const auto command = ttstr(getIndex(commands, index));
            int sourceType = -1;
            try {
                sourceType = static_cast<int>(player.findSource(command).Type());
            } catch(...) {
                std::cerr << "  command[" << index << "]=" << command.AsStdString()
                          << " sourceError=<non-std-exception>\n";
                continue;
            }
            std::cerr << "  command[" << index << "]=" << command.AsStdString()
                      << " sourceType=" << sourceType << "\n";
        }
        for(tjs_int index = 0; index < variantCount(layerNames) && index < 2; ++index) {
            const auto layerName = ttstr(getIndex(layerNames, index));
            const auto layerNameStd = layerName.AsStdString();
            std::cerr << "  layer[" << index << "]=" << layerName.AsStdString()
                      << "\n";
            const auto clipIt =
                snapshot->clipIndexByLabel.find(label.AsStdString());
            REQUIRE(clipIt != snapshot->clipIndexByLabel.end());
            REQUIRE(clipIt->second >= 0);
            REQUIRE(static_cast<size_t>(clipIt->second) < snapshot->clipList.size());
            const auto &clip = snapshot->clipList[static_cast<size_t>(clipIt->second)];
            const auto layerIt = std::find_if(
                clip.layerList.begin(), clip.layerList.end(),
                [&](const auto &candidate) {
                    if(!candidate) {
                        return false;
                    }
                    if(const auto labelValue = (*candidate)["label"]) {
                        if(const auto text =
                               std::dynamic_pointer_cast<PSB::PSBString>(labelValue)) {
                            return text->value == layerNameStd;
                        }
                    }
                    return false;
                });
            if(layerIt == clip.layerList.end()) {
                std::cerr << "    native layer lookup skipped\n";
            } else {
                const auto &layer = *layerIt;
                if(const auto frameList = (*layer)["frameList"]) {
                    std::cerr << "    native frameList\n";
                    dumpPsbValue(frameList, "      ");
                }
                if(const auto children = (*layer)["children"]) {
                    std::cerr << "    native children\n";
                    dumpPsbValue(children, "      ");
                }
            }
        }
        REQUIRE(variantCount(layerNames) == expectedLayers);
        REQUIRE(getterList.Type() == tvtObject);
        REQUIRE(player.getLayerMotion(ttstr(getIndex(player.getLayerNames(), 0)))
                    .Type() == tvtObject);
        REQUIRE(player.getProgressCompat() == Catch::Approx(0.0));

        player.frameProgress(static_cast<double>(expectedFrames - 1));
        REQUIRE(player.getTimelinePlaying(label));
        REQUIRE(player.getProgressCompat() < 1.0);

        player.frameProgress(1.0);
        REQUIRE_FALSE(player.getTimelinePlaying(label));
        REQUIRE(player.getProgressCompat() == Catch::Approx(1.0));
    };

    verifyOne(yuzuPath, TJS_W("yuzulogo"), 15, 241);
    verifyOne(m2Path, TJS_W("back_white"), 23, 91);
}
