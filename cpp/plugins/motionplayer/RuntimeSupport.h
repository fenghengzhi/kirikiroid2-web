//
// Internal helpers for motionplayer/emoteplayer runtime state.
//
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <spdlog/fmt/fmt.h>

#include "tjs.h"
#include "psbfile/PSBFile.h"
#include "MotionNode.h"

namespace motion {
    class Player;
    class SourceCache;
}

namespace motion::detail {

    struct VariableFrameInfo {
        std::string label;
        double value = 0.0;
    };

    struct VariableControllerBinding {
        int type = -1;
        int index = -1;
        std::string source;
        std::string role;
    };

    struct SelectorControlOption {
        std::string label;
        double offValue = 0.0;
        double onValue = 0.0;
    };

    struct SelectorControlBinding {
        std::string label;
        std::vector<SelectorControlOption> options;
    };

    struct FixedControllerOutputBinding {
        std::string label;
        int type = -1;
        int index = -1;
        std::string role;
    };

    struct ClampControlBinding {
        int type = 0;
        std::string varLr;
        std::string varUd;
        double minValue = 0.0;
        double maxValue = 0.0;
    };

    struct TimelineControlFrame {
        double time = 0.0;
        bool isTypeZero = true;
        float value = 0.0f;
        double easingWeight = 1.0;
    };

    struct TimelineControlTrack {
        std::string label;
        // Aligned to libkrkr2.so sub_66FC5C byte at track+8:
        // set when label is present in instantVariableList (player+0x4F8).
        bool instantVariable = false;
        std::vector<TimelineControlFrame> frames;
    };

    struct TimelineControlBinding {
        std::string label;
        double loopBegin = -1.0;
        double loopEnd = -1.0;
        double lastTime = -1.0;
        std::vector<TimelineControlTrack> tracks;
    };

    struct TimelineControlKeyframe {
        float value = 0.0f;
        float duration = 0.0f;
        float weight = 1.0f;
    };

    struct TimelineControlAnimatorState {
        std::deque<TimelineControlKeyframe> queue;
        bool active = false;
        float currentValue = 0.0f;
        float startValue = 0.0f;
        float targetValue = 0.0f;
        float progress = 1.0f;
        float duration = 0.0f;
        float weight = 1.0f;
    };

    // Runtime-owned parameter entry. Aligned to libkrkr2.so's 56-byte
    // Player+384 parameter table populated inside Player_initNonEmoteMotion
    // (0x6B365C) via sub_6B1718 / sub_6B202C.
    struct MotionParameterEntry {
        std::string id;
        bool discretization = false;
        double rangeBegin = 0.0;
        double rangeEnd = 0.0;
        double rangeScale = 1.0;
        double value = 0.0;
        int mode = 0;
    };

    struct MotionClip {
        std::string label;
        std::string owner;
        bool loop = false;
        double loopTime = -1.0;   // from PSB; >=0 means loop restart point
        double totalFrames = 0.0;
        // Primary layer storage — PSB array order, duplicates preserved.
        // Aligned to libkrkr2.so Player_buildNodeTree (0x6B51F0) reading
        // "layer" from Player+528 as a TJS Array iterated by index.
        std::vector<std::shared_ptr<const PSB::PSBDictionary>> layerList;
        std::vector<std::string> sourceCandidates;
        // Raw PSB objects retained for Player_initNonEmoteMotion (0x6B365C).
        // The parameter table is intentionally not cached here; it is rebuilt
        // on each player init to mirror libkrkr2.so ownership/lifetime.
        std::shared_ptr<const PSB::PSBDictionary> motionObject;
        std::shared_ptr<const PSB::PSBDictionary> contentObject;
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
        bool controlInitialized = false;
        double controlLastAppliedTime = 0.0;
        std::vector<int> controlFrameCursor;
        std::vector<float> controlTrackValues;
        std::vector<TimelineControlAnimatorState> controlTrackAnimators;
        TimelineControlAnimatorState blendAnimator;
        bool blendAutoStop = false;
    };

    // Aligned to libkrkr2.so Player_dispatchEvents (0x6C4490):
    // type=0: onAction(param1, param2), type=1: onSync()
    struct MotionEvent {
        int type = 0;
        std::string param1;
        std::string param2;
        // 砖5/洞3: the layer (motion["tag"]) stream fires onAction(void, action)
        // — Player_advanceRootAndNodes 0x6B6E68 passes a released/void variant as
        // param1 (record.a) and content["action"] as param2 (record.b). When set,
        // dispatch passes a void tTJSVariant for param1 instead of widen(param1).
        // See analysis/Player_progress_frame_stepping_M1_plan.md §8.7.
        bool voidParam1 = false;
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
        std::unordered_map<std::string, VariableControllerBinding> controllerBindings;
        std::unordered_set<std::string> instantVariableLabels;
        std::unordered_map<std::string, SelectorControlBinding> selectorControls;
        std::vector<FixedControllerOutputBinding> fixedControllerOutputs;
        std::vector<ClampControlBinding> clampControls;
        std::vector<std::string> mirrorVariableMatchList;
        // Primary layer storage — PSB array order, duplicates preserved.
        // Aligned to libkrkr2.so Player_buildNodeTree (0x6B51F0) which reads
        // the "layer" TJS Array from Player+528 and iterates by index.
        std::vector<std::shared_ptr<const PSB::PSBDictionary>> layerList;
        std::vector<std::string> sourceCandidates;
        // Primary clip storage — PSB priority[] order preserved.
        // Aligned to libkrkr2.so Player+548 (motion.priority TJSArray stored at
        // 0x6B37D0) + Player+616 (priority[currentIndex].content at 0x6B38FC).
        // Duplicate clip labels are allowed (index-addressable) but the
        // auxiliary label→index map below resolves name-based lookups using
        // last-wins semantics to mirror Player+24 labelMap behaviour.
        std::vector<MotionClip> clipList;
        // Clip lookup keyed by (owner, label).
        //
        // Aligned to libkrkr2.so Player_loadMotion (0x6B0F10): the binary
        // navigates the PSB tree by the path segment "motion/<chara>/<motion>",
        // where the first segment after "motion/" is the owner (chara) name
        // (sub_A0CC68 "motion/"+chara @0x6B1308, then +"*/"+motion @0x6B1380).
        // Two objects in one PSB file that share a motion name (e.g. title.psb
        // char/show vs TITLE/show) resolve to DIFFERENT subtrees and therefore
        // DIFFERENT layer[] arrays — they are never merged. The port snapshot is
        // a flattened file-level cache, so the owner (chara) segment is folded
        // into the clip key to reproduce that per-chara isolation. Keying by
        // bare label would collide same-name motions across objects and merge
        // their layerLists (the DRACU title self-reference recursion bug).
        std::map<std::pair<std::string, std::string>, int> clipIndexByOwnerLabel;
        // Auxiliary label-only fallback index (last-wins), used ONLY after the
        // owner-scoped lookup fails — mirrors the binary's per-tree label->index
        // map (Player+24, built in Player_buildNodeTree_recursive @0x6B4CE4 with
        // last-wins lowerBoundInsert). Lets single-owner snapshots and consumers
        // that cannot determine the owner still resolve by motion name.
        std::unordered_map<std::string, int> clipIndexByLabel;

        // Resolve a clip index for (owner, label): owner-scoped first, then
        // label-only fallback. Returns -1 when no clip matches.
        int findClipIndex(const std::string &owner,
                          const std::string &label) const {
            if(label.empty()) {
                return -1;
            }
            if(!owner.empty()) {
                const auto it =
                    clipIndexByOwnerLabel.find(std::make_pair(owner, label));
                if(it != clipIndexByOwnerLabel.end()) {
                    return it->second;
                }
            }
            const auto fb = clipIndexByLabel.find(label);
            if(fb != clipIndexByLabel.end()) {
                return fb->second;
            }
            return -1;
        }
        // Layer event stream (global onAction/onSync source).
        // Aligned to libkrkr2.so Player+1072 = motion["tag"] (written by
        // Player_initNonEmoteMotion @0x6B3778). The binary's
        // Player_advanceRootAndNodes (0x6B6ADC) walks this array with the
        // cursor at Player+916, firing align/sync/action on type==1 frames
        // gated by the +1093 stop-gate. Each element is a frame dict
        // {time:double, type:int, content:{align,sync,action,...}}.
        // NOTE: the layout note's "+1072 = stealthMotionStr" is wrong — +1072
        // holds this tag array; the `stealthMotion` getter (sub_6D9618) merely
        // re-reads the same field. See analysis/Player_progress_frame_stepping_M1_plan.md §8.5.
        // (Brick-5 commit 1: additive storage only — no reader yet.)
        std::shared_ptr<PSB::PSBList> tagFrames;
        // Root content-snapshot stream (NO event gate; content snapshot only).
        // Aligned to libkrkr2.so Player+548 = motion["priority"] (written by
        // Player_initNonEmoteMotion @0x6B37D0). The binary's
        // Player_advanceRootAndNodes (0x6B6ADC) walks this array (root loop
        // 0x6B6EE4..0x6B7124) with the cursor at Player+568, snapshotting
        // priority[cursor]["content"] into Player+616 (sub_A0FB64 variant copy)
        // on each crossed frame. curTime=Player+576, nextTime=Player+584. The
        // initial snapshot is priority[0]["content"] (0x6B38FC). Each element is
        // a frame dict {time:double, content:{...}} — NO "type"/event read,
        // unlike the layer (tag) stream. This is the RAW priority frame array,
        // distinct from clipList (which decodes priority entries as clips for
        // the node-tree build path); the binary's +548 stream is the flat
        // frame array indexed priority[cursor]. (Stream ② of the 4-stream
        // advance unit — see analysis/Player_progress_frame_stepping_M1_plan.md §8.)
        std::shared_ptr<PSB::PSBList> priorityFrames;
        std::unordered_map<std::string, TimelineControlBinding>
            timelineControlByLabel;
        std::vector<std::string> resourceAliases;
        double width = 0.0;
        double height = 0.0;
    };

    // (The former VariableLabelEntry port model of Player+1296 has been
    // replaced by detail::VariableLabelScope / VariableLabelScopeDeque
    // (internal/value_structs.h + player_containers.h) — the byte-verified
    // 160B var-track item with cascadeKey/cursor/value/labelName/slot[2].
    // initVariables now builds the deque directly; see Player_initVariables
    // @0x6CD750 and analysis/Player_4_HashMaps_Container_Mapping.md §四之二.)

    // A5: lifted from PlayerRuntime's inner type to namespace scope so Player
    // can hold the renderLayerStates map without leaking the runtime's nested
    // structure outward.
    struct LayerRenderState {
        tjs_int layerId = 0;
        bool clipEnabled = true;
        bool initialized = false;
        bool isDirty = false;
        tjs_int absolute = 0;
        tjs_int hitThreshold = 256;
        tTJSVariant layerObject;
        tTJSVariant layerGetter;
        std::array<float, 4> clipRect{0.f, 0.f, 0.f, 0.f};
        std::array<float, 4> worldRect{0.f, 0.f, 0.f, 0.f};
        std::array<float, 4> localRect{0.f, 0.f, 0.f, 0.f};
        std::array<std::uint32_t, 4> packedColors{
            0xFF808080u, 0xFF808080u, 0xFF808080u, 0xFF808080u
        };
    };

    // A9: render-item structs lifted from PlayerRuntime's inner scope to
    // namespace detail:: so Player can hold the corresponding vectors / map
    // without leaking PlayerRuntime's nested type surface.

    // Native render-item fields from the anonymous 0x1B0 item built by
    // libkrkr2.so 0x6C2334 and consumed in-place by 0x6C4E28 / 0x6C7440.
    // These fields intentionally keep the native write lifecycle: +21 and
    // +216..228 are not blanket-cleared every frame.
    struct NativeRenderItemFields {
        bool rawFlag16 = false; // original item +16 = node+201
        bool skipFlag0 = false; // original render item +17 (0x6C2334 / 0x6C7440)
        bool skipFlag1 = false; // original render item +18 (0x6C2334 / 0x6C7440)
        bool drawFlag = false;  // original render item +19
        bool rawFlag20 = false; // original item +20, set by sub_6C4E28 requireLayerId path
        bool rawFlag21 = false; // original item +21, drawable clip valid after sub_6C4E28
        std::uint8_t stencilMaskRef = 0; // original item +22
        std::uint8_t stencilWriteRef = 0; // original item +23
        std::array<float, 4> paintBox{0.f, 0.f, 0.f, 0.f}; // item+184..196
        std::array<float, 4> viewport{1.f, 1.f, -1.f, -1.f}; // item+200..212
        std::array<int, 4> clipRect{0, 0, 0, 0}; // item+216..228
        std::array<int, 4> dirtyRect{0, 0, 0, 0};
        int opacity = 255; // item+232
        // item+244 in libkrkr2.so sub_6C2334 @ 0x6C2A90 — stencil/composite
        // flags copied from node.stencilType; consumed by sub_6C7440 alpha
        // mask path `(item+244 & 4)` / `(item+244 & 3)==1`.
        int stencilComposite = 0;
    };

    struct RenderItemNativeFieldLifetime {
        bool rawFlag20 = false;
        bool rawFlag21 = false;
        std::array<int, 4> clipRect{0, 0, 0, 0};
        std::array<int, 4> dirtyRect{0, 0, 0, 0};
        std::array<float, 8> localCorners{};
        std::vector<float> localMeshPoints;
    };

    struct PreparedRenderItem : NativeRenderItemFields {
        int nodeIndex = 0;
        tTJSVariant srcRef;
        std::string sourceKey;
        bool hasOwnSource = false;
        bool groupOnly = false;
        bool topLevelList = true;
        bool groupList = false;
        bool selfSeedChildList = false;
        // A9: nativeLifetimeOwner moves from PlayerRuntime* to Player* so the
        // lifetime owner survives the eventual A10 PlayerRuntime removal.
        motion::Player *nativeLifetimeOwner = nullptr;
        int nativeLifetimeKey = 0;
        double sortKey = 0.0;
        int blendMode = 16;
        tTJSVariant contextVariant; // original item +248 (player+1012 copy)
        std::array<float, 8> corners{};
        std::array<float, 8> localCorners{};
        std::array<std::uint32_t, 4> packedColors{
            0xFF808080u, 0xFF808080u, 0xFF808080u, 0xFF808080u
        };
        bool hasViewport = false;
        int coordinateMode = 0;
        int objTriPriority = 0;
        int visibleAncestorIndex = -1;
        int meshDivX = 0;
        int meshDivY = 0;
        int meshType = 0;
        std::vector<float> meshPoints;
        std::vector<float> localMeshPoints;
        int layerId = 0;
        int layerId2 = 0;
        PreparedRenderItem *parentItem = nullptr; // semantic mapping of item +264
        std::vector<PreparedRenderItem *> childItems; // semantic mapping of item +24
        tTJSVariant leafLayer;      // item+304 variant
        tTJSVariant composedLayer;  // item+324 variant
        std::array<int, 4> builtRect{0, 0, 0, 0};
        bool leafBuilt = false;
        bool composedBuilt = false;
        bool executedDirect = false;
    };

    // Faithful 1:1 element of libkrkr2.so player+936/944's
    // std::vector<DeadChildMotionRenderItem> (44-byte stride element).
    //
    // Binary layout (sub_6F363C @0x6F363C, the vector::_M_range_insert that
    // operates on this element type, copies each element as):
    //   elem+0  : int32              (*v6 = *(_DWORD *)v5)
    //   elem+4  : tTJSVariant        (sub_A0FB64(v6+1,  v5+4),  20 bytes)
    //   elem+24 : tTJSVariant        (sub_A0FB64(v6+6,  v5+24), 20 bytes)
    // total 44 bytes. Destroy path (sub_6BE2C0 / 0x6C1A00 / sub_6F363C
    // shrink) destroys the two variants via sub_A0F778(elem+24)+sub_A0F778(elem+4).
    //
    // This is the DEAD residual render-item buffer: in this libkrkr2.so build
    // it has NO producer (no leaf item is ever pushed in; sub_6C2334 writes
    // caller-stack temporaries instead) and NO consumer (nothing reads it).
    // It is only ever fed by aggregating child players' equally-empty +936
    // buffers (Player_updateLayers_childMotionPass @0x6BE2C0 and
    // Player_particleStepChildren @0x6C1A00), so it stays empty -> empty and
    // is observably inert. It is reproduced here purely for 1:1 structural
    // fidelity (the live draw-time render list is the SEPARATE
    // _preparedRenderItems, which corresponds to the binary's per-draw temp
    // vector built by sub_6C2334, NOT this buffer).
    //
    // Plain C++ POD with two tTJSVariant fields: a default std::vector of this
    // type gives the binary's ctor (player+936 zero-init at 0x6CEF1C, OWORD
    // store = empty vector) and dtor (per-element variant destroy + free)
    // for free.
    struct DeadChildMotionRenderItem {
        int kind = 0;          // elem+0
        tTJSVariant a;         // elem+4
        tTJSVariant b;         // elem+24
    };

    struct PerNodeEvalData {
        double padding[5] = {};   // offsets 0-39 (unused in our current scope)
        double evalTime = 0.0;
        int dirtyFlag = 0;
    };

    // A10: PlayerRuntime struct deleted after Phase A1-A9 hoisted every field
    // onto motion::Player. Forward declarations of the legacy type may still
    // appear in unrelated headers but the type no longer has any members
    // and is not instantiated anywhere.
    void ensureRootNodeLike_0x6CED30(motion::Player &player);
    void resetNodeTreeKeepRootLike_0x6B56F8(motion::Player &player);

    // Aligned with libkrkr2.so Player_buildNodePathKey @0x6B5C1C.
    // Walks the parentIndex chain from `nodeIndex` toward the synthetic root
    // (index 0), accumulating each node's "label" (node+0) as a "/"-prefixed
    // segment. Ancestors are prepended, so the result is the slash-joined
    // hierarchical path "/topLevelLabel/.../selfLabel". The loop terminates
    // when a node's parentIndex reaches 0 (the synthetic root is NOT emitted),
    // matching the binary `while ( a2 )` on `*(node+36)` (parentIndex).
    // This path is the key for HM3 (Player+1184, _perNodeLayerStateMap) ONLY —
    // it is the path builder's sole consumer (xrefs_to(0x6B5C1C) = 2 callers,
    // both HM3). The Player+24 node-index map (_nodeLabelMap) is keyed by the
    // RAW label, a separate key space — do NOT use this for that map.
    std::string buildNodePathKeyLike_0x6B5C1C(
        const std::deque<motion::detail::MotionNode> &nodes, int nodeIndex);

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

    bool logoChainTraceEnabled();
    bool logoChainTraceEnabledForPath(const std::string &motionPath);
    bool logoChainTraceEnabled(const std::shared_ptr<MotionSnapshot> &snapshot);
    bool logoSnapshotMarkEnabled();
    bool logoSnapshotMarkEnabledForPath(const std::string &motionPath);
    void resetLogoChainTraceSession(const std::string &motionPath);
    void logoChainTraceLog(const std::string &motionPath,
                           const char *stage,
                           const char *func,
                           double frameTime,
                           const std::string &message);
    void logoChainTraceCheck(const std::string &motionPath,
                             const char *stage,
                             const char *func,
                             double frameTime,
                             const std::string &expected,
                             const std::string &actual,
                             bool ok,
                             const std::string &likelyRootCause = {});
    void logoChainTraceSummary(const std::string &motionPath,
                               const char *func,
                               double frameTime,
                               const std::string &note = {});

    template <typename... Args>
    inline void logoChainTraceLogf(const std::string &motionPath,
                                   const char *stage,
                                   const char *func,
                                   double frameTime,
                                   fmt::format_string<Args...> format,
                                   Args &&...args) {
        if(!logoChainTraceEnabledForPath(motionPath)) {
            return;
        }
        logoChainTraceLog(motionPath, stage, func, frameTime,
                          fmt::format(format, std::forward<Args>(args)...));
    }

} // namespace motion::detail
