//
// Created to verify motionplayer/emoteplayer behavior aligned to libkrkr2.so.
//

#include <catch2/catch_test_macros.hpp>

#include "motionplayer/EmotePlayer.h"
#include "motionplayer/Player.h"
#include "motionplayer/ResourceManager.h"
#include "test_config.h"

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

} // namespace

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

    REQUIRE(player.countVariables() == 0);
    REQUIRE(ttstr(player.getVariableLabelAt(0)).IsEmpty());
    REQUIRE(player.countVariableFrameAt(0) == 0);
    REQUIRE(ttstr(player.getVariableFrameLabelAt(0, 0)).IsEmpty());
    REQUIRE(player.getVariableFrameValueAt(0, 0) == 0.0);
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
