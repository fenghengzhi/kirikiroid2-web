// PlayerMotionLoad.cpp — motion load, variable init, and node tree build
// Split out for maintainability.
//
#include "PlayerInternal.h"
#include "MotionTraceWeb.h"

using namespace motion::internal;

namespace motion {
    namespace {
        bool shouldEmitMotionLoadDiag(std::uint32_t seq) {
            return seq <= 200 || (seq % 100) == 0;
        }

        const char *diagBool(bool v) {
            return v ? "true" : "false";
        }
    }

    // Aligned to libkrkr2.so Player_playImpl (0x6B21E8):
    // Called from sub_6BE0C0 at 0x6BE46C with flags = motionFlags | v12.
    // flags: PlayFlagForce(1)=force reload, PlayFlagStealth(16)=set stealth fields only.
    void Player::onFindMotion(ttstr name, int flags) {
        static std::uint32_t s_diagSeq = 0;
        const auto diagSeq = ++s_diagSeq;
        const bool emitDiag = shouldEmitMotionLoadDiag(diagSeq);
        if(emitDiag && LOGGER) {
            LOGGER->info(
                "PRTDIAG Player::onFindMotion enter seq={} this={} name='{}' flags=0x{:x} chara='{}' motionKey='{}' stealth='{}' active={} activePath='{}' timelines={} playingLabels={} allplaying={}",
                diagSeq, static_cast<const void *>(this),
                detail::narrow(name), static_cast<unsigned int>(flags),
                detail::narrow(_chara), detail::narrow(_motionKey),
                detail::narrow(_stealthMotion), _activeMotion != nullptr,
                _activeMotion ? _activeMotion->path : std::string("<none>"),
                _timelines.size(), _playingTimelineLabels.size(),
                diagBool(_allplaying));
        }

        // PlayFlagStealth (0x10): store as stealth motion, don't load
        // Binary: if ((flags & 0x10) && !player->project) { player->motionKey = name; return; }
        if ((flags & PlayFlagStealth) && _project.Type() == tvtVoid) {
            _stealthMotion = name;
            if(emitDiag && LOGGER) {
                LOGGER->info(
                    "PRTDIAG Player::onFindMotion stealth-store-return seq={} this={} name='{}'",
                    diagSeq, static_cast<const void *>(this),
                    detail::narrow(name));
            }
            return;
        }

        // Player_playImpl (0x6B2284) only enters Player_loadMotion /
        // Player_initNonEmoteMotion when force/as-can is set or the requested
        // motion key differs from the stored key.
        if(_activeMotion && _motionKey == name &&
           (flags & (PlayFlagForce | PlayFlagAsCan)) == 0) {
            if(emitDiag && LOGGER) {
                LOGGER->info(
                    "PRTDIAG Player::onFindMotion same-motion-return seq={} this={} name='{}' flags=0x{:x} activePath='{}'",
                    diagSeq, static_cast<const void *>(this),
                    detail::narrow(name), static_cast<unsigned int>(flags),
                    _activeMotion ? _activeMotion->path
                                  : std::string("<none>"));
            }
            return;
        }

        // PlayFlagForce (0x01): force reload even if same motion is loaded.
        if ((flags & PlayFlagForce) && _motionKey == name) {
            if(emitDiag && LOGGER) {
                LOGGER->info(
                    "PRTDIAG Player::onFindMotion force-clear-motionKey seq={} this={} name='{}'",
                    diagSeq, static_cast<const void *>(this),
                    detail::narrow(name));
            }
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
        if(name.IsEmpty()) {
            snapshot.reset();
        } else {
            if(_project.Type() == tvtObject) {
                if(const auto projectSnapshot = detail::lookupModuleSnapshot(_project)) {
                    // Presence check scoped to (chara, motion). Aligned to
                    // libkrkr2.so Player_loadMotion (0x6B0F10): a motion is
                    // resolved by "motion/<chara>/<motion>", so the project
                    // snapshot must contain THIS chara's same-named motion, not
                    // merely any object's. findClipIndex falls back to
                    // label-only for single-owner snapshots / empty chara.
                    if(projectSnapshot->findClipIndex(charaRaw, motionRaw) >= 0) {
                        snapshot = projectSnapshot;
                        if(emitDiag && LOGGER) {
                            LOGGER->info(
                                "PRTDIAG Player::onFindMotion resolved-project seq={} this={} name='{}' path='{}'",
                                diagSeq, static_cast<const void *>(this),
                                motionRaw, snapshot->path);
                        }
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
                    resolveMotion(*this, fullPath, nativeRM());
                if(snapshot) {
                    cacheMotion(*this, motionRaw,
                                detail::narrow(fullPath), snapshot);
                    if(emitDiag && LOGGER) {
                        LOGGER->info(
                            "PRTDIAG Player::onFindMotion resolved-chara-path seq={} this={} name='{}' fullPath='{}' path='{}'",
                            diagSeq, static_cast<const void *>(this),
                            motionRaw, detail::narrow(fullPath),
                            snapshot->path);
                    }
                }
            }
            if(!snapshot) {
                snapshot = resolveMotion(*this, name, nativeRM());
                if(snapshot && emitDiag && LOGGER) {
                    LOGGER->info(
                        "PRTDIAG Player::onFindMotion resolved-raw seq={} this={} name='{}' path='{}'",
                        diagSeq, static_cast<const void *>(this),
                        motionRaw, snapshot->path);
                }
            }
        }
        if(snapshot) {
            activateMotion(*this, snapshot);
            _motionKey = name;
            _project = snapshot->moduleValue;
            syncVariableKeysFromActiveMotion();
            if(emitDiag && LOGGER) {
                LOGGER->info(
                    "PRTDIAG Player::onFindMotion call-init seq={} this={} name='{}' flags=0x{:x} activePath='{}'",
                    diagSeq, static_cast<const void *>(this),
                    detail::narrow(name), static_cast<unsigned int>(flags),
                    _activeMotion ? _activeMotion->path
                                  : std::string("<none>"));
            }
            initNonEmoteMotionLike_0x6B365C(
                static_cast<std::uint32_t>(flags));
        } else if(emitDiag && LOGGER) {
            LOGGER->info(
                "PRTDIAG Player::onFindMotion unresolved seq={} this={} name='{}' flags=0x{:x}",
                diagSeq, static_cast<const void *>(this),
                detail::narrow(name), static_cast<unsigned int>(flags));
        }

        // After loading, prime timelines and start playback.
        // Binary alignment:
        // - Player_playImpl (0x6B2284) stores the requested motion label
        // - Player_initNonEmoteMotion (0x6B365C) rebuilds state but does not
        //   auto-start every primary clip
        // - Player_playTimeline (0x672F70) starts the requested label only
        //   when it exists
        if (_activeMotion && _timelines.empty()) {
            detail::primeTimelineStates(_timelines,
                                        *_activeMotion);
        }

        if (_activeMotion && !_timelines.empty()) {
            const auto requestedKey = detail::narrow(name);
            bool startedRequested = false;
            if(!requestedKey.empty() &&
               _timelines.find(requestedKey) != _timelines.end()) {
                playTimeline(name, flags & ~PlayFlagStealth);
                startedRequested = true;
            }

            if(!startedRequested) {
                // 移除 port-invented `_cachedTotalFrames = maxTF`（max(state.totalFrames)）
                // 覆盖：二进制无此逻辑。+1128(_cachedTotalFrames) 与 +1136(_loopTime)
                // 全二进制唯一成对写入点是 Player_initNonEmoteMotion @0x6B370C/@0x6B372C
                // （motion["loopTime"]/motion["lastTime"]，同源配对，见
                // player-totalframes-looptime-invariant note）。本端 onFindMotion 上方
                // 已调 initNonEmoteMotionLike_0x6B365C，由 PlayerCore.cpp:750-751
                // 成对设 `_loopTime=clip->loopTime; _cachedTotalFrames=clip->totalFrames`。
                // 此处再用 max(state.totalFrames) 单独覆盖 _cachedTotalFrames 而不动
                // _loopTime，会破坏 loopTime<lastTime 不变量（maxTF 可能为 0 < 残留
                // _loopTime），令 forward loop-wrap `v7 += loopTime - totalFrames` 在
                // while(totalFrames<=v7) 下空转 -> 千恋万花标题死循环。删除覆盖后
                // _cachedTotalFrames/_loopTime 保持 initNonEmoteMotion 的同源配对值。
                _playingTimelineLabels.clear();
                const auto &primary =
                    !_activeMotion->mainTimelineLabels.empty()
                        ? _activeMotion->mainTimelineLabels
                        : _activeMotion->diffTimelineLabels;
                for (const auto &timelineLabel : primary) {
                    auto &state = _timelines[timelineLabel];
                    state.flags = flags & ~PlayFlagStealth;
                    state.playing = true;
                    state.blendRatio = 1.0;
                    state.controlInitialized = false;
                    state.controlLastAppliedTime = state.currentTime;
                    state.controlFrameCursor.clear();
                    state.controlTrackValues.clear();
                    state.controlTrackAnimators.clear();
                    _playingTimelineLabels.push_back(timelineLabel);
                }
                _allplaying = !_playingTimelineLabels.empty();
            }
        }

        // Handle pending stealth motion (0x6B226C..0x6B2280)
        if (!_stealthMotion.IsEmpty()) {
            if(emitDiag && LOGGER) {
                LOGGER->info(
                    "PRTDIAG Player::onFindMotion consume-stealth seq={} this={} stealth='{}'",
                    diagSeq, static_cast<const void *>(this),
                    detail::narrow(_stealthMotion));
            }
            _stealthChara = _chara;
            // stealthMotion is consumed — binary nulls it after use
            _stealthMotion = ttstr();
        }

        if(emitDiag && LOGGER) {
            LOGGER->info(
                "PRTDIAG Player::onFindMotion exit seq={} this={} name='{}' activePath='{}' motionKey='{}' timelines={} playingLabels={} allplaying={}",
                diagSeq, static_cast<const void *>(this), detail::narrow(name),
                _activeMotion ? _activeMotion->path : std::string("<none>"),
                detail::narrow(_motionKey), _timelines.size(),
                _playingTimelineLabels.size(), diagBool(_allplaying));
        }
    }

    // Aligned to libkrkr2.so Player_initVariables (0x6CD750). Called
    // synchronously from the play path after Player_buildNodeTree (0x6B51F0)
    // and before the (flags & Chain) playback-state gate. Reads the PSB
    // "variable" array (from Player+528 == activeMotion->root) and pushes one
    // VariableLabelScope (the 160B var-track item) per dict entry onto the
    // Player+1296 deque:
    //   cascadeKey (item+0)  <- scope present ? scope+"::"+label : label
    //       (binary 0x6CDAEC..0x6CDBB4: v25 = sub_A1359C(scope, "::");
    //        item+0 = sub_A1359C(v25, label) — concat, NOT scope-suffix split)
    //   frameSource (item+24) <- entry["label"] raw value (sub_A0FB64 @0x6CDA98) —
    //       the keyframe list stream③ iterates; SAME value as item+0
    //   value (item+16)      <- 0  (interpolated later; HM4 reads it)
    //   cursor (item+8)      <- 0
    //   slot[0/1].typeZeroFlag <- 1  (binary item+68/+124 seeded =1 @0x6CD9C0)
    void Player::initVariables() {
        if(false) {
            return;
        }
        _variableLabelScopes.clear();
        if(!_activeMotion || !_activeMotion->root) {
            return;
        }

        const auto &root = _activeMotion->root;
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

            detail::VariableLabelScope entry;

            // frameSource (item+24) = entry["label"] raw value — the keyframe list
            // the var-track advance (stream③) iterates. The binary stores this same
            // entry["label"] at both item+0 (key) and item+24 (frames).
            const auto labelVal = (*entryDic)["label"];
            entry.frameSource = labelVal;

            // cascadeKey (item+0): ttstr_c_str(entry["label"]) joined with scope —
            // the binary's two sub_A1359C concats (NOT the scope suffix). The string
            // form is the label's text when it is a PSBString; a non-string label
            // yields an empty base (the binary's ttstr_c_str on a non-string
            // variant). Join gated on scope resolving (v38 != null), mirrored by
            // the PSBString cast succeeding.
            std::string label;
            if(const auto labelStr =
                   std::dynamic_pointer_cast<PSB::PSBString>(labelVal)) {
                label = labelStr->value;
            }
            std::string scope;
            bool scopePresent = false;
            if(const auto scopeVal = (*entryDic)["scope"]) {
                if(const auto scopeStr = std::dynamic_pointer_cast<
                       PSB::PSBString>(scopeVal)) {
                    scope = scopeStr->value;
                    scopePresent = true;
                }
            }
            entry.cascadeKey = detail::widen(
                scopePresent ? (scope + "::" + label) : label);

            // value/cursor default 0; slot gate flags default 1 (struct
            // in-class initialisers mirror the binary memset+seed).
            _variableLabelScopes.push_back(std::move(entry));
        }
    }

    void Player::resetNodeTreeForBuildLike_0x6B56F8() {
        if(false) {
            return;
        }
        detail::ensureRootNodeLike_0x6CED30(*this);
        for(size_t i = 1; i < _nodes.size(); ++i) {
            auto &node = _nodes[i];
            // P3-B (d): release via Player+992 dispatch FuncCall (binary
            //   resetAndReleaseNodes@0x6B56F8), not a native call.
            dispatchReleaseLayerId(node.layerId1);
            dispatchReleaseLayerId(node.layerId2);
        }
        detail::resetNodeTreeKeepRootLike_0x6B56F8(*this);
    }

    void Player::inheritChildPlayerStateLike_0x6B3C78(detail::MotionNode &node) {
        if(auto *child = node.getChildPlayer()) {
            // P3-B: the child already received the parent's RM dispatch at
            //   construction (binary 0x6b43cc: `Player_ctor(child, parent+992)`),
            //   so no native-RM copy is needed here. This site only sets the
            //   parent link (binary 0x6b43dc: `*(child+8) = parent`).
            child->setParentPlayerLike_0x6B1ABC(this);
            child->_tjsRandomGenerator = _tjsRandomGenerator;
            child->_project = _project.Type() == tvtObject
                ? _project
                : (_activeMotion
                       ? _activeMotion->moduleValue
                       : tTJSVariant{});
            if(true) {
                detail::ensureRootNodeLike_0x6CED30(*child);
                auto &root = child->_nodes.front();
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
        static std::uint32_t s_buildDiagSeq = 0;
        const auto diagSeq = ++s_buildDiagSeq;
        const bool emitDiag = shouldEmitMotionLoadDiag(diagSeq);
        if(!_activeMotion) {
            if(emitDiag && LOGGER) {
                LOGGER->info(
                    "PRTDIAG Player::buildNodeTree no-active-return seq={} this={} motionKey='{}' chara='{}'",
                    diagSeq, static_cast<const void *>(this),
                    detail::narrow(_motionKey), detail::narrow(_chara));
            }
            return;
        }

        const auto nodesBefore = _nodes.size();
        resetNodeTreeForBuildLike_0x6B56F8();

        std::string clipLabel;
        std::string clipOwner;
        const auto *clip =
            _activeClip != nullptr ? _activeClip
                                            : selectActiveClip();
        if(clip != nullptr) {
            clipLabel = clip->label;
            // owner = the chara/object that owns this clip. The clip was
            // already selected owner-scoped (selectActiveClip via _chara), so
            // pass its owner through to keep the free buildNodeTree resolving
            // the SAME (owner, label) clip — never a same-named clip from a
            // different object. Aligned to libkrkr2.so Player_loadMotion
            // (0x6B0F10) "motion/<chara>/<motion>" navigation.
            clipOwner = clip->owner;
        }
        if(clipOwner.empty()) {
            clipOwner = detail::narrow(_chara);
        }

        if(emitDiag && LOGGER) {
            LOGGER->info(
                "PRTDIAG Player::buildNodeTree enter seq={} this={} motionKey='{}' chara='{}' activePath='{}' clipLabel='{}' nodesBefore={} nodesAfterReset={} rootLayers={} timelines={} playingLabels={} allplaying={}",
                diagSeq, static_cast<const void *>(this),
                detail::narrow(_motionKey), detail::narrow(_chara),
                _activeMotion ? _activeMotion->path : std::string("<none>"),
                clipLabel.empty() ? std::string("<none>") : clipLabel,
                nodesBefore, _nodes.size(), _activeMotion->layerList.size(),
                _timelines.size(), _playingTimelineLabels.size(),
                diagBool(_allplaying));
        }

        if(_activeMotion &&
           detail::logoSnapshotMarkEnabledForPath(_activeMotion->path) &&
           _activeMotion->path.find("m2logo.mtn") != std::string::npos) {
            std::fprintf(
                stderr,
                "SNAPCLIP motion=%s motionKey=%s clipLabel=%s playing=%s clipCount=%zu\n",
                _activeMotion->path.c_str(),
                detail::narrow(_motionKey).c_str(),
                clipLabel.empty() ? "<none>" : clipLabel.c_str(),
                _playingTimelineLabels.empty()
                    ? "<none>"
                    : _playingTimelineLabels.front().c_str(),
                _activeMotion->clipList.size());
        }

        detail::buildNodeTree(
            *this, *_activeMotion, clipOwner, clipLabel,
            _preview);  // binary buildNodeTree (0x6B43A4) gates on +1092 (preview)

        if(emitDiag && LOGGER) {
            LOGGER->info(
                "PRTDIAG Player::buildNodeTree after-detail seq={} this={} activePath='{}' clipLabel='{}' nodeCount={} labelMap={} preview={}",
                diagSeq, static_cast<const void *>(this),
                _activeMotion ? _activeMotion->path : std::string("<none>"),
                clipLabel.empty() ? std::string("<none>") : clipLabel,
                _nodes.size(), _nodeLabelMap.size(), diagBool(_preview));
        }

        if(!_nodes.empty()) {
            auto &root = _nodes[0];
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

        if(detail::logoChainTraceEnabled(_activeMotion)) {
            const auto &motionPath = _activeMotion->path;
            detail::logoChainTraceLogf(
                motionPath, "buildNodeTree", "0x6B51F0", _clampedEvalTime,
                "clipLabel={} rootLayers={} nodeCount={}",
                clipLabel.empty() ? std::string("<root>") : clipLabel,
                _activeMotion->layerList.size(), _nodes.size());
            for(const auto &node : _nodes) {
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
