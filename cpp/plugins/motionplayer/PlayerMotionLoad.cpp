// PlayerMotionLoad.cpp — motion load, variable init, and node tree build
// Split out for maintainability.
//
#include "PlayerInternal.h"
#include "MotionTraceWeb.h"

using namespace motion::internal;

namespace motion {
    // Aligned to libkrkr2.so Player_playImpl (0x6B21E8):
    // Called from sub_6BE0C0 at 0x6BE46C with flags = motionFlags | v12.
    // flags: PlayFlagForce(1)=force reload, PlayFlagStealth(16)=set stealth fields only.
    void Player::onFindMotion(ttstr name, int flags) {
        // PlayFlagStealth (0x10): store as stealth motion, don't load
        // Binary: if ((flags & 0x10) && !player->project) { player->motionKey = name; return; }
        if ((flags & PlayFlagStealth) && _project.Type() == tvtVoid) {
            _stealthMotion = name;
            return;
        }

        // Player_playImpl (0x6B2284) only enters Player_loadMotion /
        // Player_initNonEmoteMotion when force/as-can is set or the requested
        // motion key differs from the stored key.
        if(_runtime && _runtime->activeMotion && _motionKey == name &&
           (flags & (PlayFlagForce | PlayFlagAsCan)) == 0) {
            return;
        }

        // PlayFlagForce (0x01): force reload even if same motion is loaded.
        if ((flags & PlayFlagForce) && _motionKey == name) {
            _motionKey = ttstr();  // clear to bypass same-motion guard in findMotion
        }

        // Aligned to libkrkr2.so Player_playImpl (0x6B2284):
        // the requested motion/timeline label is part of the player state
        // throughout the load/init path (+976/+984 in the binary). Keep the
        // local request key after the force-guard so nested motion players do
        // not collapse back to the module's primary clip ordering.
        _motionKey = name;

        // Aligned to Player_loadMotion (0x6B0F10): the native path resolves
        // motion using the current chara first ("motion/<chara>/<motion>"),
        // then falls back to the raw motion string when needed.
        std::shared_ptr<detail::MotionSnapshot> snapshot;
        const auto motionRaw = detail::narrow(name);
        const auto charaRaw = detail::narrow(_chara);
        if(!_runtime || name.IsEmpty()) {
            snapshot.reset();
        } else {
            if(_project.Type() == tvtObject) {
                if(const auto projectSnapshot = detail::lookupModuleSnapshot(_project)) {
                    if(projectSnapshot->clipIndexByLabel.find(motionRaw) !=
                       projectSnapshot->clipIndexByLabel.end()) {
                        snapshot = projectSnapshot;
                    }
                }
            }
            if(!charaRaw.empty() &&
               !snapshot &&
               motionRaw.find('/') == std::string::npos &&
               motionRaw.find('\\') == std::string::npos) {
                const auto fullPath =
                    ttstr{ "motion/" + charaRaw + "/" + motionRaw };
                snapshot =
                    resolveMotion(*_runtime, fullPath, &_resourceManagerNative);
                if(snapshot) {
                    cacheMotion(*_runtime, motionRaw,
                                detail::narrow(fullPath), snapshot);
                }
            }
            if(!snapshot) {
                snapshot = resolveMotion(*_runtime, name, &_resourceManagerNative);
            }
        }
        if(snapshot) {
            activateMotion(*_runtime, snapshot);
            _motionKey = name;
            _project = snapshot->moduleValue;
            syncVariableKeysFromActiveMotion();
            initNonEmoteMotionLike_0x6B365C(
                static_cast<std::uint32_t>(flags));
        }

        // After loading, prime timelines and start playback.
        // Binary alignment:
        // - Player_playImpl (0x6B2284) stores the requested motion label
        // - Player_initNonEmoteMotion (0x6B365C) rebuilds state but does not
        //   auto-start every primary clip
        // - Player_playTimeline (0x672F70) starts the requested label only
        //   when it exists
        if (_runtime->activeMotion && _runtime->timelines.empty()) {
            detail::primeTimelineStates(_runtime->timelines,
                                        *_runtime->activeMotion);
        }

        if (_runtime->activeMotion && !_runtime->timelines.empty()) {
            const auto requestedKey = detail::narrow(name);
            bool startedRequested = false;
            if(!requestedKey.empty() &&
               _runtime->timelines.find(requestedKey) != _runtime->timelines.end()) {
                playTimeline(name, flags & ~PlayFlagStealth);
                startedRequested = true;
            }

            if(!startedRequested) {
                double maxTF = 0.0;
                _runtime->playingTimelineLabels.clear();
                const auto &primary =
                    !_runtime->activeMotion->mainTimelineLabels.empty()
                        ? _runtime->activeMotion->mainTimelineLabels
                        : _runtime->activeMotion->diffTimelineLabels;
                for (const auto &timelineLabel : primary) {
                    auto &state = _runtime->timelines[timelineLabel];
                    state.flags = flags & ~PlayFlagStealth;
                    state.playing = true;
                    state.blendRatio = 1.0;
                    state.controlInitialized = false;
                    state.controlLastAppliedTime = state.currentTime;
                    state.controlFrameCursor.clear();
                    state.controlTrackValues.clear();
                    state.controlTrackAnimators.clear();
                    _runtime->playingTimelineLabels.push_back(timelineLabel);
                    if (state.totalFrames > maxTF) maxTF = state.totalFrames;
                }
                _cachedTotalFrames = maxTF;
                _allplaying = !_runtime->playingTimelineLabels.empty();
            }
        }

        // Handle pending stealth motion (0x6B226C..0x6B2280)
        if (!_stealthMotion.IsEmpty()) {
            _stealthChara = _chara;
            // stealthMotion is consumed — binary nulls it after use
            _stealthMotion = ttstr();
        }
    }

    // Aligned to libkrkr2.so Player_initVariables (0x6CD750). Called
    // synchronously from the play path after Player_buildNodeTree (0x6B51F0)
    // and before the (flags & Chain) playback-state gate. Reads the PSB
    // "variable" array (from Player+528 == activeMotion->root) and appends
    // one VariableLabelEntry per dict entry:
    //   name  <- entry["scope"] split by "::" first, then ":" (right half),
    //            or empty
    //   label <- entry["label"]
    //   flag68/flag124 <- 1 (binary default; semantics not yet reversed)
    void Player::initVariables() {
        if(!_runtime) {
            return;
        }
        _runtime->variableLabelEntries.clear();
        if(!_runtime->activeMotion || !_runtime->activeMotion->root) {
            return;
        }

        const auto &root = _runtime->activeMotion->root;
        const auto variableList = std::dynamic_pointer_cast<PSB::PSBList>(
            (*root)["variable"]);
        if(!variableList) {
            return;
        }

        for(const auto &item : *variableList) {
            const auto entryDic =
                std::dynamic_pointer_cast<PSB::PSBDictionary>(item);
            if(!entryDic) {
                continue;
            }

            detail::VariableLabelEntry entry;

            if(const auto scopeVal = (*entryDic)["scope"]) {
                if(const auto scopeStr = std::dynamic_pointer_cast<
                       PSB::PSBString>(scopeVal)) {
                    const auto &scope = scopeStr->value;
                    auto sep = scope.find("::");
                    size_t sepLen = 2;
                    if(sep == std::string::npos) {
                        sep = scope.find(':');
                        sepLen = 1;
                    }
                    if(sep != std::string::npos) {
                        entry.name = detail::widen(scope.substr(sep + sepLen));
                    }
                }
            }
            if(const auto labelVal = (*entryDic)["label"]) {
                if(const auto labelStr = std::dynamic_pointer_cast<
                       PSB::PSBString>(labelVal)) {
                    entry.label = detail::widen(labelStr->value);
                }
            }

            _runtime->variableLabelEntries.push_back(std::move(entry));
        }
    }

    void Player::resetNodeTreeForBuildLike_0x6B56F8() {
        if(!_runtime) {
            return;
        }
        detail::ensureRootNodeLike_0x6CED30(*_runtime);
        for(size_t i = 1; i < _runtime->nodes.size(); ++i) {
            auto &node = _runtime->nodes[i];
            _resourceManagerNative.releaseLayerId(node.layerId1);
            _resourceManagerNative.releaseLayerId(node.layerId2);
        }
        detail::resetNodeTreeKeepRootLike_0x6B56F8(*_runtime);
    }

    void Player::inheritChildPlayerStateLike_0x6B3C78(detail::MotionNode &node) {
        if(auto *child = node.getChildPlayer()) {
            child->_resourceManagerNative = _resourceManagerNative;
            child->_tjsRandomGenerator = _tjsRandomGenerator;
            child->_project = _project.Type() == tvtObject
                ? _project
                : (_runtime && _runtime->activeMotion
                       ? _runtime->activeMotion->moduleValue
                       : tTJSVariant{});
            if(child->_runtime) {
                detail::ensureRootNodeLike_0x6CED30(*child->_runtime);
                auto &root = child->_runtime->nodes.front();
                root.coordinateMode = node.coordinateMode;
                for(int i = 0; i < 4; ++i) {
                    root.transformOrder[i] = node.transformOrder[i];
                }
                root.delta.dirty = true;
            }
        }
    }

    // Aligned to libkrkr2.so Player_buildNodeTree (0x6B51F0). The binary calls
    // this unconditionally from Player_initNonEmoteMotion (0x6B365C) after
    // Player_loadMotion succeeds — there is no lazy gate. The caller is
    // responsible for having loaded the motion first; we keep a minimal null
    // check so calls on a Player without a loaded motion become a no-op
    // instead of crashing, but we do NOT call ensureMotionLoaded here.
    void Player::buildNodeTree() {
        if(!_runtime || !_runtime->activeMotion) {
            return;
        }

        resetNodeTreeForBuildLike_0x6B56F8();

        std::string clipLabel;
        const auto *clip =
            _runtime->activeClip != nullptr ? _runtime->activeClip
                                            : selectActiveClip();
        if(clip != nullptr) {
            clipLabel = clip->label;
        }

        if(_runtime->activeMotion &&
           detail::logoSnapshotMarkEnabledForPath(_runtime->activeMotion->path) &&
           _runtime->activeMotion->path.find("m2logo.mtn") != std::string::npos) {
            std::fprintf(
                stderr,
                "SNAPCLIP motion=%s motionKey=%s clipLabel=%s playing=%s clipCount=%zu\n",
                _runtime->activeMotion->path.c_str(),
                detail::narrow(_motionKey).c_str(),
                clipLabel.empty() ? "<none>" : clipLabel.c_str(),
                _runtime->playingTimelineLabels.empty()
                    ? "<none>"
                    : _runtime->playingTimelineLabels.front().c_str(),
                _runtime->activeMotion->clipList.size());
        }

        detail::buildNodeTree(
            *_runtime, *_runtime->activeMotion, clipLabel, &_resourceManagerNative, this,
            _completionType);

        if(!_runtime->nodes.empty()) {
            auto &root = _runtime->nodes[0];
            // Aligned to libkrkr2.so Player_setRootFlipX/X/Y
            // (0x6CD028/0x6CD048/0x6CD068): these setters write the delta block
            // at node+1584..+1660, not the local post-interpolation mirror.
            root.delta.flipX = _rootFlipX;
            if(_hasPendingRootPos) {
                root.delta.posX = _pendingRootX;
                root.delta.posY = _pendingRootY;
            }
            root.delta.dirty = true;
        }

        if(detail::logoChainTraceEnabled(_runtime->activeMotion)) {
            const auto &motionPath = _runtime->activeMotion->path;
            detail::logoChainTraceLogf(
                motionPath, "buildNodeTree", "0x6B51F0", _clampedEvalTime,
                "clipLabel={} rootLayers={} nodeCount={}",
                clipLabel.empty() ? std::string("<root>") : clipLabel,
                _runtime->activeMotion->layerList.size(), _runtime->nodes.size());
            for(const auto &node : _runtime->nodes) {
                const bool hasStencilTypeKey =
                    node.psbNode && static_cast<bool>((*node.psbNode)["stencilType"]);
                detail::logoChainTraceLogf(
                    motionPath, "buildNodeTree.node", "0x6B51F0",
                    _clampedEvalTime,
                    "nodeIndex={} label={} type={} parent={} hasSource={} meshType={} inheritFlags=0x{:x} parameterizeIndex={} objTriPriority={} parentClipIndex={} stencilType={} hasStencilTypeKey={}",
                    node.index,
                    node.layerName.empty() ? std::string("<root>")
                                           : node.layerName,
                    node.nodeType, node.parentIndex, node.hasSource ? 1 : 0,
                    node.meshType, node.inheritFlags, node.parameterizeIndex,
                    node.objTriPriority,
                    node.parentClipIndex,
                    node.stencilType, hasStencilTypeKey ? 1 : 0);
            }
        }
    }

} // namespace motion
