//
// Internal helpers for motionplayer/emoteplayer runtime state.
//
#pragma once

#include <array>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "tjs.h"
#include "psbfile/PSBFile.h"
#include "MotionNode.h"

namespace motion::detail {

    struct VariableFrameInfo {
        std::string label;
        double value = 0.0;
    };

    struct MotionClip {
        std::string label;
        std::string owner;
        bool loop = false;
        double loopTime = -1.0;   // from PSB; >=0 means loop restart point
        double totalFrames = 0.0;
        std::vector<std::string> layerNames;
        std::unordered_map<std::string, std::shared_ptr<const PSB::PSBDictionary>>
            layersByName;
        std::vector<std::string> sourceCandidates;
    };

    struct TimelineState {
        std::string label;
        int flags = 0;
        bool playing = false;
        bool loop = false;
        double loopTime = -1.0;   // from PSB; >=0 means loop, <0 means stop at end
        double totalFrames = 0.0;
        double currentTime = 0.0;
        double blendRatio = 1.0;
        bool wasPlaying = false;  // for edge detection in dispatchEvents
    };

    // Aligned to libkrkr2.so Player_dispatchEvents (0x6C4490):
    // type=0: onAction(param1, param2), type=1: onSync()
    struct MotionEvent {
        int type = 0;
        std::string param1;
        std::string param2;
    };

    struct MotionSnapshot {
        std::string path;
        std::shared_ptr<PSB::PSBFile> file;
        std::shared_ptr<const PSB::PSBDictionary> root;
        std::unordered_map<std::string, std::shared_ptr<const PSB::PSBResource>>
            resourcesByPath;
        tTJSVariant moduleValue;
        std::vector<std::string> mainTimelineLabels;
        std::vector<std::string> diffTimelineLabels;
        std::vector<std::string> variableLabels;
        std::unordered_map<std::string, bool> loopTimelines;
        std::unordered_map<std::string, double> timelineLoopTimes;
        std::unordered_map<std::string, double> timelineTotalFrames;
        std::unordered_map<std::string, std::pair<double, double>> variableRanges;
        std::unordered_map<std::string, std::vector<VariableFrameInfo>> variableFrames;
        std::vector<std::string> layerNames;
        std::unordered_map<std::string, std::shared_ptr<const PSB::PSBDictionary>> layersByName;
        std::vector<std::string> sourceCandidates;
        std::unordered_map<std::string, MotionClip> clipsByLabel;
        std::vector<std::string> resourceAliases;
        double width = 0.0;
        double height = 0.0;
    };

    struct PlayerRuntime {
        std::unordered_map<std::string, std::shared_ptr<MotionSnapshot>> motionsByKey;
        std::unordered_map<std::string, tTJSVariant> sourcesByKey;
        std::shared_ptr<MotionSnapshot> activeMotion;
        std::unordered_map<std::string, TimelineState> timelines;
        std::unordered_map<std::string, tjs_int> layerIdsByName;
        std::unordered_map<tjs_int, std::string> layerNamesById;
        std::vector<tTJSVariant> backgrounds;
        std::vector<tTJSVariant> captions;
        std::unordered_map<std::string, bool> disabledSelectorTargets;
        tTJSVariant lastCanvas;
        tTJSVariant lastViewParam;
        std::array<double, 6> drawAffineMatrix{ 1.0, 0.0, 0.0,
                                                1.0, 0.0, 0.0 };
        tjs_int nextLayerId = 1;
        tjs_int clearColor = 0;
        tjs_int width = 0;
        tjs_int height = 0;
        int alphaOpCounter = 0;
        bool resizable = false;
        bool flip = false;
        bool visible = true;
        double opacity = 1.0;
        double slant = 0.0;
        double zoom = 1.0;
        std::vector<MotionEvent> pendingEvents;
        // Persistent node tree for updateLayers pipeline
        std::vector<MotionNode> nodes;
        bool nodesBuilt = false;
        // Node label → index map. Aligned to binary's std::map<ttstr,int> at player+24.
        // Populated after buildNodeTree, queried by sub_6F2228 equivalent.
        std::map<std::string, int> nodeLabelMap;

        // Render entry list for cross-Player merge during updateLayers.
        // Aligned to libkrkr2.so player+936/944: vector of 44-byte entries.
        // Each entry: int nodeIndex + tTJSVariant srcRef + tTJSVariant renderData.
        // Populated by sub_6C2334 (draw phase), merged from child→parent by
        // sub_6F363C during sub_6BE0C0/sub_6C17A4 (updateLayers phase3).
        // Render entry list for cross-Player merge during updateLayers.
        // Aligned to libkrkr2.so player+936/944: vector of 44-byte entries.
        struct RenderEntry {
            int nodeIndex = 0;        // offset 0: node reference
            tTJSVariant srcRef;       // offset 4: source image variant
            tTJSVariant renderData;   // offset 24: render parameters variant
        };
        std::vector<RenderEntry> renderEntries;  // player+936/944

        // Per-node evaluation time array.
        // Aligned to libkrkr2.so player+384: 56-byte-per-node entries.
        // Each node's node+8 pointer points into this array.
        // Offset 40 within each entry stores the per-node eval time.
        // Offset 48 stores the per-node dirty flag (cleared in post-loop).
        struct PerNodeEvalData {
            double padding[5] = {};   // offsets 0-39 (unused in our current scope)
            double evalTime = 0.0;    // offset 40: per-node evaluation time
            int dirtyFlag = 0;        // offset 48: cleared in post-loop
        };
        std::vector<PerNodeEvalData> perNodeEvalData;  // player+384/392
        // Aligned to libkrkr2.so Player_playImpl (0x6B2284):
        // PSB root "type" field: 0=non-emote (motion), 1=emote
        bool isEmoteMode = false;
    };

    std::shared_ptr<PlayerRuntime> makePlayerRuntime();

    std::string narrow(const ttstr &value);
    ttstr widen(const std::string &value);

    std::vector<ttstr> buildMotionLookupCandidates(const ttstr &name);
    bool resolveExistingPath(const std::vector<ttstr> &candidates, ttstr &resolved);
    void appendEmbeddedSourceCandidates(const MotionSnapshot &snapshot,
                                        const std::string &source,
                                        std::vector<ttstr> &candidates);

    std::shared_ptr<MotionSnapshot> loadMotionSnapshot(const ttstr &path,
                                                       tjs_int decryptSeed);
    tTJSVariant loadPSBVariant(const ttstr &path, tjs_int decryptSeed);

    void registerModuleSnapshot(const tTJSVariant &module,
                                const std::shared_ptr<MotionSnapshot> &snapshot);
    std::shared_ptr<MotionSnapshot> lookupModuleSnapshot(const tTJSVariant &module);

    tTJSVariant makeArray(const std::vector<tTJSVariant> &items);
    tTJSVariant makeDictionary(
        const std::vector<std::pair<std::string, tTJSVariant>> &entries);
    std::vector<tTJSVariant> stringsToVariants(
        const std::vector<std::string> &values);

    void primeTimelineStates(std::unordered_map<std::string, TimelineState> &states,
                             const MotionSnapshot &snapshot);
    void stepTimelines(std::unordered_map<std::string, TimelineState> &states,
                       double dt,
                       std::vector<MotionEvent> *events = nullptr);

    // Scan PSB layer tree for action/sync events between prevTime and newTime.
    // Aligned to libkrkr2.so: updateLayers queues events during tree evaluation.
    void scanLayerActions(const MotionSnapshot &snapshot,
                          double prevTime, double newTime,
                          std::vector<MotionEvent> &events);

} // namespace motion::detail
