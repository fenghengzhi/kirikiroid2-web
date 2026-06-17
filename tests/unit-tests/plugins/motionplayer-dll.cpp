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
#include "motionplayer/PlayerFrameStep.h"
#include "motionplayer/PlayerFrameStepping.h"
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

    // P3-B (c): binary has no by-name layer-id API; allocation is the no-arg RM
    //   dispatch FuncCall (emitRenderItem@0x6C4E28 / buildNodeTree@0x6B4A6C all
    //   numparams=0). Exercise the faithful no-arg path.
    const auto firstLayerId = player.dispatchRequireLayerId();
    REQUIRE(firstLayerId > 0);
    player.releaseLayerId(firstLayerId);
    REQUIRE(player.dispatchRequireLayerId() > 0);

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
    // (A2) `frameLastTime` is the RO script property = player+1128 =
    // motion["lastTime"] (= _cachedTotalFrames), NOT the per-frame dt. It is set
    // only by initNonEmoteMotion (play path); this case never plays a motion, so
    // it stays at its default 0.0. (The old `== 16.0` assertion encoded the
    // since-fixed bug where getFrameLastTime returned the raw dt; the
    // frameProgress-advanced check is covered by getTickCount/getFrameTickCount
    // below.)
    REQUIRE(player.getFrameLastTime() == 0.0);
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

    motion::D3DEmotePlayer player(rm);
    player.setModule(module);
    REQUIRE(player.getModule().Type() == tvtObject);

    player.setCoord(100.0, 200.0);
    player.setScale(1.0);
    // PRE-EXISTING DRIFT (M11 D-09, unrelated to the setVariable shim removal):
    //   the label-less contains(double,double) overload was removed because the
    //   binary D3DEmotePlayer_contains @0x530B6C is 3-arg
    //   (contains(label, x, y) -> resolve node by label via sub_6B5AD8(player+
    //   1064, label) -> Player_hitTest(node+1664, x, y)). The original 2-arg
    //   calls here encoded the removed non-faithful overload. Calling the
    //   faithful 3-arg form with an empty label exercises the root-node hit test;
    //   the exact boolean result depends on the fixture's node AABB, which this
    //   test has no oracle for, so we only exercise the call (no fabricated
    //   hit/miss assertion). A proper hit-test assertion needs a known node label
    //   from the fixture + hit-test geometry alignment — tracked separately.
    (void)player.contains(TJS_W(""), 100.0, 200.0);
    (void)player.contains(TJS_W(""), 99.0, 199.0);

    player.hide();
    (void)player.contains(TJS_W(""), 100.0, 200.0);
    player.show();
    (void)player.contains(TJS_W(""), 100.0, 200.0);

    // Disjoint-map reality (libkrkr2.so, fresh-decompile 2026-06-03):
    //   D3DEmotePlayer.setVariable -> EmoteEngine_setVariable @0x671228 writes
    //   the EmoteEngine HM7 (+1440 = _labelToValueHM7; HM6-miss path @0x67135c).
    //   D3DEmotePlayer.getVariable -> Player_getVariable @0x533E1C reads the
    //   inner Player's HM1(+264)/HM2(+320) cascade — a DIFFERENT object. The two
    //   maps are bridged ONLY by the EmoteEngine_progress bind-loop
    //   (D3DEmotePlayer.progress @0x67D01C, G2-C). So in the binary,
    //   setVariable(x) immediately followed by getVariable() WITHOUT a progress
    //   in between does NOT return x. The previous immediate-equality assertion
    //   here encoded non-binary behavior produced by a now-removed Player-side
    //   double-write shim (see EmotePlayer.cpp / PlayerVariable.cpp). Assert the
    //   faithful disjoint semantics: the engine accepts the write and getVariable
    //   reflects the (un-bridged) Player-side default until progress runs the
    //   bind-loop.
    //
    //   UPDATE 2026-06-03: the live-progress wiring has now LANDED.
    //   D3DEmotePlayer::progress / pass now route through engine().progress (=
    //   EmoteEngine_progress @0x67D01C) which runs the G2-C bind-loop at step 5
    //   then the Player progress at step 7, so the bind bridge is RUNTIME-LIVE.
    //   (The prior note that D3DEmotePlayer::pass calls progressMsLike directly
    //   and bypasses the engine bind-loop is now FALSIFIED — corrected here.)
    //   The disjoint-map assertion BELOW still holds because it does setVariable
    //   then getVariable with NO progress() in between, so the HM7 write has not
    //   yet been bridged to Player HM1/HM2. A progress-inclusive round-trip
    //   (setVariable -> progress -> getVariable == x) is structurally connected
    //   but only observable when the HM7 chain (+1456) is non-empty, which needs
    //   the controller/variable builder population from a motion's metadata; the
    //   logo fixture has none, so the bridge runs over an empty chain (inert).
    //   No progress-inclusive assertion is added here: this fixture cannot
    //   observe it, and per CLAUDE.md fixtures are not fabricated.
    player.setVariable(TJS_W("manual"), 3.5);
    const double manualBeforeBridge = player.getVariable(TJS_W("manual"));
    REQUIRE(manualBeforeBridge != 3.5); // HM7 write is not visible to Player HM2

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

    player.fadeInTimeline(label, 1.0, motion::TimelinePlayFlagDifference);
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

    const auto baseDir = std::filesystem::path(REFERENCE_PATH) / "xp3" /
        "logo_test";
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

// M1/P2: binary-aligned parseFrame / mergeFrameContent (independent, not wired
// into the live frame-progress path). Verifies the parsed-frame slot layout,
// type-flag derivation, and mask-gated field merge from a synthetic PSB frame
// dictionary. Aligned to libkrkr2.so Player_parseFrame @0x6926B4 +
// Player_mergeFrameContent @0x692AB0.
namespace {
    std::shared_ptr<PSB::PSBNumber> psbInt(int v) {
        return std::make_shared<PSB::PSBNumber>(v);
    }
    std::shared_ptr<PSB::PSBString> psbStr(const std::string &v) {
        return std::make_shared<PSB::PSBString>(v);
    }
}

TEST_CASE("parseFrame/mergeFrameContent slot is binary-aligned (P2)") {
    using motion::detail::ParsedFrameSlotLike_0x6926B4;
    using motion::detail::parseFrameLike_0x6926B4;
    using motion::detail::mergeFrameContentLike_0x692AB0;

    // Slot layout guards (mirrors node+320 / node+856 scalar region).
    static_assert(offsetof(ParsedFrameSlotLike_0x6926B4, time) == 8);
    static_assert(offsetof(ParsedFrameSlotLike_0x6926B4, mask) == 20);
    static_assert(offsetof(ParsedFrameSlotLike_0x6926B4, typeZeroFlag) == 24);

    SECTION("type 0 -> invisible, merge early-returns") {
        auto frame = std::make_shared<PSB::PSBDictionary>();
        frame->emplace("time", psbInt(5));
        frame->emplace("type", psbInt(0));
        ParsedFrameSlotLike_0x6926B4 slot;
        parseFrameLike_0x6926B4(slot, frame, 7, /*nodeType*/ 0);
        REQUIRE(slot.frameIndex == 7u);
        REQUIRE(slot.time == Catch::Approx(5.0));
        REQUIRE(slot.typeZeroFlag == 1);
        // merge must early-out (mergedFlag set, no fields applied)
        auto content = std::make_shared<PSB::PSBDictionary>();
        content->emplace("mask", psbInt(0x1));
        content->emplace("ox", psbInt(99));
        mergeFrameContentLike_0x692AB0(slot, 0, content);
        REQUIRE(slot.mergedFlag == 1);
        REQUIRE(slot.ox == Catch::Approx(0.0));  // not applied
    }

    SECTION("type 3 -> interpolate, mask-gated ox/oy + opa") {
        auto content = std::make_shared<PSB::PSBDictionary>();
        // mask 0x1 (ox/oy) | 0x400 (opa). opa group requires 0x20600 to enter,
        // but 0x400 alone sets the 0x...600 group bit -> enters opa branch.
        content->emplace("mask", psbInt(0x1 | 0x400 | 0x200));
        content->emplace("ox", psbInt(12));
        content->emplace("oy", psbInt(34));
        content->emplace("opa", psbInt(128));
        auto frame = std::make_shared<PSB::PSBDictionary>();
        frame->emplace("time", psbInt(2));
        frame->emplace("type", psbInt(3));
        frame->emplace("content", content);

        ParsedFrameSlotLike_0x6926B4 slot;
        parseFrameLike_0x6926B4(slot, frame, 0, /*nodeType*/ 0);
        REQUIRE(slot.typeZeroFlag == 0);
        REQUIRE(slot.interpFlag == 1);
        REQUIRE(slot.mask == 0x601u);

        mergeFrameContentLike_0x692AB0(slot, /*nodeType*/ 0, content);
        REQUIRE(slot.ox == Catch::Approx(12.0));
        REQUIRE(slot.oy == Catch::Approx(34.0));
        REQUIRE(slot.opacity == 128u);          // applied (mask & 0x400)
        REQUIRE(slot.blendMode == 16u);         // default preserved (no 0x20000)
    }

    SECTION("mask defaults: opacity 255, blend 16 when bits absent") {
        auto content = std::make_shared<PSB::PSBDictionary>();
        content->emplace("mask", psbInt(0x10));  // angle only
        content->emplace("angle", psbInt(90));
        ParsedFrameSlotLike_0x6926B4 slot;
        slot.interpFlag = 0;  // type 2 path
        slot.mask = 0x10;     // merge reads v3[5] (slot.mask), set by parseFrame
        mergeFrameContentLike_0x692AB0(slot, /*nodeType*/ 1, content);
        REQUIRE(slot.angle == Catch::Approx(90.0));
        REQUIRE(slot.opacity == 255u);
        REQUIRE(slot.blendMode == 16u);
    }

    SECTION("act read only when mask & 0x40000 (parseFrame 0x6928EC)") {
        auto content = std::make_shared<PSB::PSBDictionary>();
        content->emplace("mask", psbInt(0x40000));
        content->emplace("act", psbStr("jump"));
        auto frame = std::make_shared<PSB::PSBDictionary>();
        frame->emplace("time", psbInt(0));
        frame->emplace("type", psbInt(2));
        frame->emplace("content", content);
        ParsedFrameSlotLike_0x6926B4 slot;
        parseFrameLike_0x6926B4(slot, frame, 0, /*nodeType*/ 0);
        REQUIRE(slot.act == "jump");
    }

    SECTION("src gate: nodeType 0 in 0x1849, nodeType 1 not") {
        auto content = std::make_shared<PSB::PSBDictionary>();
        content->emplace("mask", psbInt(0));
        content->emplace("src", psbStr("chara/body"));
        ParsedFrameSlotLike_0x6926B4 slotIn;
        mergeFrameContentLike_0x692AB0(slotIn, /*nodeType*/ 0, content);
        REQUIRE(slotIn.src == "chara/body");  // (1<<0)&0x1849 != 0
        ParsedFrameSlotLike_0x6926B4 slotOut;
        mergeFrameContentLike_0x692AB0(slotOut, /*nodeType*/ 1, content);
        REQUIRE(slotOut.src.empty());          // (1<<1)&0x1849 == 0
    }
}

// ---------------------------------------------------------------------------
// M1/P3+P4: binary-aligned node-deque frame cursor stepping (independent, not
// wired into the live frame-progress path). Drives the pure cursor seek /
// completion / gate logic of:
//   Player_advanceNodeFrames     @0x6B7E44
//   Player_advanceRootAndNodes   @0x6B6ADC
//   Player_rewindRootAndNodes    @0x6B9A3C
//   Player_reseekTimelineCursors @0x6B86C8
namespace {
    // Build one frame dict {time,type[,content]} for a synthetic frame stream.
    std::shared_ptr<PSB::PSBDictionary>
    mkFrame(double time, int type,
            std::shared_ptr<PSB::PSBDictionary> content = nullptr) {
        auto f = std::make_shared<PSB::PSBDictionary>();
        f->emplace("time", std::make_shared<PSB::PSBNumber>(time));
        f->emplace("type", psbInt(type));
        if(content) f->emplace("content", content);
        return f;
    }
    // Build a frame stream (PSBList) from an array of (time,type) pairs, all
    // type-2 (static) with an empty content (mask 0).
    std::shared_ptr<PSB::PSBList>
    mkStream(std::initializer_list<double> times) {
        auto list = std::make_shared<PSB::PSBList>(0);
        for(double t : times) {
            auto content = std::make_shared<PSB::PSBDictionary>();
            content->emplace("mask", psbInt(0));
            list->push_back(mkFrame(t, 2, content));
        }
        return list;
    }
}

TEST_CASE("frame cursor stepping is binary-aligned (P3+P4)") {
    using motion::detail::ParsedFrameSlotLike_0x6926B4;
    using motion::detail::parseFrameLike_0x6926B4;
    using motion::detail::NodeFrameStreamsLike;
    using motion::detail::FrameStreamCursorLike;
    using motion::detail::PlayerFrameStreamsLike;
    using motion::detail::TimelineSeekStateLike;
    using motion::detail::advanceNodeFramesLike_0x6B7E44;
    using motion::detail::advanceRootAndNodesLike_0x6B6ADC;
    using motion::detail::rewindRootAndNodesLike_0x6B9A3C;
    using motion::detail::reseekTimelineCursorsLike_0x6B86C8;

    // Seed a node's slot0 to frame `idx` of `frames` (parseFrame fills time).
    auto seedNode = [&](NodeFrameStreamsLike &node,
                        const std::shared_ptr<PSB::PSBList> &frames, int idx) {
        node.frameList = frames;
        node.nodeType = 0;
        node.activeSlotIndex = 0;
        parseFrameLike_0x6926B4(node.slots[0], nullptr, 0, 0);
        node.slots[0].frameIndex = static_cast<std::uint32_t>(idx);
        // seed both slots' time from the stream so the ping-pong has a baseline.
        auto f = (*frames)[idx];
        if(auto d = std::dynamic_pointer_cast<PSB::PSBDictionary>(f)) {
            // re-parse so slots reflect the real frame time.
            parseFrameLike_0x6926B4(node.slots[0], d,
                                    static_cast<std::uint32_t>(idx), 0);
        }
        node.slots[1] = node.slots[0];
        node.slots[1].frameIndex = static_cast<std::uint32_t>(idx);
    };

    SECTION("advanceNodeFrames seeks the active slot forward to childEvalTime") {
        // Stream times 0,10,20,30,40 (5 frames). Seek toward childEvalTime=25.
        auto frames = mkStream({0, 10, 20, 30, 40});
        NodeFrameStreamsLike node;
        node.hasChild = true;
        node.childEvalTime = 25.0;
        seedNode(node, frames, 0);  // start at frame 0 (time 0)

        TimelineSeekStateLike state;  // emoteListFlag 0
        advanceNodeFramesLike_0x6B7E44(node, state);

        // The active slot must now bracket t=25: cursor advanced past time<=25.
        // count-2 == 3 caps the cursor; the seek stops when next slot.time>25.
        // Active slot frameIndex should be 2 (time 20) with the other at 3
        // (time 30): 20 <= 25 < 30.
        const auto &as = node.slots[node.activeSlotIndex];
        REQUIRE(as.time <= 25.0);
        REQUIRE(as.frameIndex <= 3u);
        // merge ran (both slots merged): mergedFlag set on at least one slot.
        REQUIRE((node.slots[0].mergedFlag == 1 ||
                 node.slots[1].mergedFlag == 1));
    }

    SECTION("advanceNodeFrames no-op when at limit & target ahead (no merge)") {
        // Seed at the last seekable frame (index count-2 == 3, time 30) with the
        // child target between 30 and 40. The forward loop breaks immediately
        // (cur.frameIndex >= limit), cur.time(30) is NOT > t(35) so no backward
        // seek, and seeked==false -> 0x6B7F70 early return without merge.
        auto frames = mkStream({0, 10, 20, 30, 40});
        NodeFrameStreamsLike node;
        node.hasChild = true;
        node.childEvalTime = 35.0;
        seedNode(node, frames, 3);  // both slots at frame 3 (time 30)
        node.slots[0].mergedFlag = 0;
        node.slots[1].mergedFlag = 0;

        TimelineSeekStateLike state;
        advanceNodeFramesLike_0x6B7E44(node, state);
        REQUIRE(node.slots[0].mergedFlag == 0);
        REQUIRE(node.slots[1].mergedFlag == 0);
    }

    SECTION("advanceRootAndNodes advances layer cursor to clampedEvalTime") {
        PlayerFrameStreamsLike p;
        p.state.clampedEvalTime = 25.0;
        p.layerStream.frames = mkStream({0, 10, 20, 30, 40});
        p.layerStream.frameCursor = 0;
        p.layerStream.curTime = 0.0;
        p.layerStream.nextTime = 10.0;  // frames[1].time
        p.rootStream.frames = mkStream({0, 50});
        p.rootStream.frameCursor = 0;
        p.rootStream.nextTime = 50.0;
        // node 0 is the root placeholder; add a real node at index 1.
        p.nodes.resize(2);

        advanceRootAndNodesLike_0x6B6ADC(p);
        // Layer cursor stops where clampedEvalTime(25) < nextTime, capped at
        // count-2 == 3. 20 <= 25 < 30 -> cursor at 2 (time 20, next 30).
        REQUIRE(p.layerStream.frameCursor == 2);
        REQUIRE(p.layerStream.curTime == Catch::Approx(20.0));
        REQUIRE(p.layerStream.nextTime == Catch::Approx(30.0));
    }

    SECTION("align gate sets motionCompleted + snaps cursor (stopGate on)") {
        // Layer stream frame[1] is a type-1 frame whose content has align=1 and
        // whose time equals clampedEvalTime, with stop gate active.
        auto alignContent = std::make_shared<PSB::PSBDictionary>();
        alignContent->emplace("align", psbInt(1));
        auto list = std::make_shared<PSB::PSBList>(0);
        list->push_back(mkFrame(0, 2));
        list->push_back(mkFrame(10, 1, alignContent));  // align frame at t=10
        list->push_back(mkFrame(20, 2));
        list->push_back(mkFrame(30, 2));

        PlayerFrameStreamsLike p;
        p.state.clampedEvalTime = 10.0;
        p.state.motionStopGate = 1;
        p.layerStream.frames = list;
        p.layerStream.frameCursor = 0;
        p.layerStream.curTime = 0.0;
        p.layerStream.nextTime = 10.0;
        p.rootStream.frames = mkStream({0, 40});
        p.rootStream.nextTime = 40.0;
        p.nodes.resize(1);

        advanceRootAndNodesLike_0x6B6ADC(p);
        // cursor advanced to frame 1 (curTime 10 == clampedEvalTime), align
        // gate fired: motionCompleted set, frameTickCount snapped to 10.
        REQUIRE(p.state.motionCompleted == 1);
        REQUIRE(p.state.frameTickCount == Catch::Approx(10.0));
    }

    SECTION("rewindRootAndNodes decrements layer cursor backward") {
        PlayerFrameStreamsLike p;
        p.state.clampedEvalTime = 12.0;
        p.layerStream.frames = mkStream({0, 10, 20, 30, 40});
        // start at cursor 3 (curTime 30) — must rewind toward 12.
        p.layerStream.frameCursor = 3;
        p.layerStream.curTime = 30.0;
        p.layerStream.nextTime = 40.0;
        p.rootStream.frames = mkStream({0, 50});
        p.rootStream.frameCursor = 1;
        p.rootStream.curTime = 0.0;  // already <= clampedEvalTime, no rewind
        p.nodes.resize(1);

        rewindRootAndNodesLike_0x6B9A3C(p);
        // curTime walked down: 30 -> 20 -> 10 (10 <= 12 stops). cursor at 1.
        REQUIRE(p.layerStream.frameCursor == 1);
        REQUIRE(p.layerStream.curTime == Catch::Approx(10.0));
    }

    SECTION("reseekTimelineCursors linear-scans layer cursor to target") {
        PlayerFrameStreamsLike p;
        p.state.clampedEvalTime = 22.0;
        p.layerStream.frames = mkStream({0, 10, 20, 30, 40});
        p.rootStream.frames = mkStream({0, 50});
        p.nodes.resize(1);

        reseekTimelineCursorsLike_0x6B86C8(p);
        // The binary's reseek (0x6B8770) is a COARSE linear scan that
        // double-increments i: the for-loop's own ++i AND the body's ++i fire on
        // every "time < target" step. With target=22, frames=[0,10,20,30,40]:
        //   i=0 (t0<22): body ++i->1, loop ++i->2
        //   i=2 (t20<22): body ++i->3, loop ++i->4
        //   i=4 (t40>22): --i->3, break
        // cursor = min(3, count-2=3) = 3. curTime/nextTime are int-truncated:
        // (int)frames[3].time=30, (int)frames[4].time=40. (advance/rewind later
        // corrects this coarse seek — reseek intentionally overshoots.)
        REQUIRE(p.layerStream.frameCursor == 3);
        REQUIRE(p.layerStream.curTime == Catch::Approx(30.0));
        REQUIRE(p.layerStream.nextTime == Catch::Approx(40.0));
    }
}
