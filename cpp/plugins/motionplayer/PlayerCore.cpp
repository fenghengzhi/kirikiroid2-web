// PlayerCore.cpp — Constructor, setMotion, serialize, core properties
// Split from Player.cpp for maintainability.
//
#include <algorithm>
#include <cctype>
#include <cmath>

#include "PlayerInternal.h"
#include "EmotePlayer.h" // for EmoteEngine (back-pointer deref)
#include "SourceCache.h"
#include "psbfile/PSBValue.h" // PSBDictionary / PSBList (metadata.eyeControl walk)
#include "ncbind.hpp"

using namespace motion::internal;

namespace {
    std::string lowerAscii(std::string value) {
        for(char &ch : value) {
            ch = static_cast<char>(
                std::tolower(static_cast<unsigned char>(ch)));
        }
        return value;
    }

    std::uint32_t swapPackedRbLike_0x6CD710(std::uint32_t packedColor) {
        return (packedColor & 0xFF00FF00u) |
            ((packedColor >> 16) & 0xFFu) |
            ((packedColor & 0xFFu) << 16);
    }
}

namespace motion {

    // Linear scan of a deque-backed controller bucket (binary EmoteEngine
    // +256/+336/+416/+576/+656 are std::deque<Animator>; lookup is O(N)).
    namespace {
        Player::VariableAnimatorState *findInDeque(
            std::deque<Player::VariableAnimatorState> &bucket,
            const std::string &label) {
            for(auto &entry : bucket) {
                if(entry.label == label) {
                    return &entry;
                }
            }
            return nullptr;
        }
        const Player::VariableAnimatorState *findInDeque(
            const std::deque<Player::VariableAnimatorState> &bucket,
            const std::string &label) {
            for(const auto &entry : bucket) {
                if(entry.label == label) {
                    return &entry;
                }
            }
            return nullptr;
        }
        void eraseInDeque(
            std::deque<Player::VariableAnimatorState> &bucket,
            const std::string &label) {
            for(auto it = bucket.begin(); it != bucket.end(); ++it) {
                if(it->label == label) {
                    bucket.erase(it);
                    return;
                }
            }
        }
    } // namespace

    std::deque<Player::VariableAnimatorState> *
    Player::controllerAnimatorBucketLike_0x671228(int type) {
        if(!_engineBack) {
            return nullptr;
        }
        switch(type) {
            case 4:
                return &_engineBack->_type4ControllerAnimators;
            case 5:
                return &_engineBack->_type5ControllerAnimators;
            case 6:
                return &_engineBack->_type6ControllerAnimators;
            case 7:
                return &_engineBack->_type7ControllerAnimators;
            case 8:
                return &_engineBack->_type8ControllerAnimators;
            default:
                return nullptr;
        }
    }

    const std::deque<Player::VariableAnimatorState> *
    Player::controllerAnimatorBucketLike_0x671228(int type) const {
        if(!_engineBack) {
            return nullptr;
        }
        switch(type) {
            case 4:
                return &_engineBack->_type4ControllerAnimators;
            case 5:
                return &_engineBack->_type5ControllerAnimators;
            case 6:
                return &_engineBack->_type6ControllerAnimators;
            case 7:
                return &_engineBack->_type7ControllerAnimators;
            case 8:
                return &_engineBack->_type8ControllerAnimators;
            default:
                return nullptr;
        }
    }

    Player::VariableAnimatorState *
    Player::findControllerAnimatorStateLike_0x671228(const std::string &label) {
        if(!_engineBack) {
            return nullptr;
        }
        if(auto *s = findInDeque(_engineBack->_type4ControllerAnimators, label)) return s;
        if(auto *s = findInDeque(_engineBack->_type5ControllerAnimators, label)) return s;
        if(auto *s = findInDeque(_engineBack->_type6ControllerAnimators, label)) return s;
        if(auto *s = findInDeque(_engineBack->_type8ControllerAnimators, label)) return s;
        return findInDeque(_engineBack->_type7ControllerAnimators, label);
    }

    const Player::VariableAnimatorState *
    Player::findControllerAnimatorStateLike_0x671228(
        const std::string &label) const {
        if(!_engineBack) {
            return nullptr;
        }
        if(auto *s = findInDeque(_engineBack->_type4ControllerAnimators, label)) return s;
        if(auto *s = findInDeque(_engineBack->_type5ControllerAnimators, label)) return s;
        if(auto *s = findInDeque(_engineBack->_type6ControllerAnimators, label)) return s;
        if(auto *s = findInDeque(_engineBack->_type8ControllerAnimators, label)) return s;
        return findInDeque(_engineBack->_type7ControllerAnimators, label);
    }

    void Player::eraseControllerAnimatorStateLike_0x671228(
        const std::string &label) {
        if(!_engineBack) {
            return;
        }
        eraseInDeque(_engineBack->_type4ControllerAnimators, label);
        eraseInDeque(_engineBack->_type5ControllerAnimators, label);
        eraseInDeque(_engineBack->_type6ControllerAnimators, label);
        eraseInDeque(_engineBack->_type7ControllerAnimators, label);
        eraseInDeque(_engineBack->_type8ControllerAnimators, label);
    }

    void Player::clearControllerAnimatorStateLike_0x671228() {
        if(!_engineBack) {
            return;
        }
        _engineBack->_type4ControllerAnimators.clear();
        _engineBack->_type5ControllerAnimators.clear();
        _engineBack->_type6ControllerAnimators.clear();
        _engineBack->_type7ControllerAnimators.clear();
        _engineBack->_type8ControllerAnimators.clear();
    }

    // findOrInsertControllerStateLike_0x671228 removed 2026-06-03: its only
    //   call sites were inside the non-faithful Player-side
    //   setVariableResolvedWeightLike_0x671228 shim (a local reimplementation of
    //   the EmoteEngine HM6->deque dispatch that the binary does only inside
    //   EmoteEngine_setVariable @0x671228). With that shim removed, this helper
    //   is dead.

    void Player::setSelectorEnabled(bool v) {
        if(_selectorEnabled == v) {
            return;
        }
        _selectorEnabled = v;
        syncSelectorControlsLike_0x670D1C();
    }

    tjs_int Player::getColorWeight() const {
        return static_cast<tjs_int>(
            swapPackedRbLike_0x6CD710(_colorWeightPacked));
    }

    void Player::setColorWeight(tjs_int v) {
        _colorWeightPacked = swapPackedRbLike_0x6CD710(
            static_cast<std::uint32_t>(v));
    }

    tjs_int Player::getMaskMode() const {
        return _maskMode;
    }

    void Player::setMaskMode(tjs_int v) {
        _maskMode = v;
    }

    void Player::setIndependentLayerInherit(bool v) {
        if(_independentLayerInherit == v) {
            return;
        }

        _independentLayerInherit = v;
        // libkrkr2.so 0x6CC9D4 compares player+1097 and marks node+1584 dirty.
        for(auto &node : _nodes) {
            node.accumulated.dirty = true;
        }
    }

    Player::Player(ResourceManager rm, Player *parentPlayer) :
        _resourceManagerNative(std::move(rm)),
        _parentPlayer(parentPlayer) {
        LOGGER->info("Motion.Player constructor called");
        // A10: makePlayerRuntime / ensureRootNodeLike previously ran inside
        // makePlayerRuntime; the call now lives here so the synthetic root
        // node lands on _nodes at index 0.
        _defaultParameterEntry.rangeScale = 1.0;
        _defaultParameterEntry.mode = 0;
        detail::ensureRootNodeLike_0x6CED30(*this);
        using ResourceManagerAdaptor = ncbInstanceAdaptor<ResourceManager>;
        if(auto *dispatch =
               ResourceManagerAdaptor::CreateAdaptor(
                   new ResourceManager(_resourceManagerNative))) {
            _resourceManager = tTJSVariant(dispatch, dispatch);
            dispatch->Release();
        }
        // Aligned to libkrkr2.so SourceCache constructor/owner lifetime
        // (0x6A78F4): Player stores a TJS SourceCache object and calls through
        // that dispatch for source resolution rather than owning a map directly.
        using SourceCacheAdaptor = ncbInstanceAdaptor<SourceCache>;
        auto *sourceCache = new SourceCache();
        sourceCache->bindPlayer(this, &_resourceManagerNative);
        if(auto *dispatch = SourceCacheAdaptor::CreateAdaptor(sourceCache)) {
            _sourceCacheNative = sourceCache;
            _sourceCacheObject = tTJSVariant(dispatch, dispatch);
            sourceCache->setSelfObject(_sourceCacheObject);
            dispatch->Release();
        } else {
            delete sourceCache;
        }
        // Aligned to sub_6A88CC (0x6A8988): create TJS Math.RandomGenerator
        // and store at player+992. Child Players inherit via sub_6CED30.
        try {
            TVPExecuteExpression(
                TJS_W("new Math.RandomGenerator()"),
                &_tjsRandomGenerator);
        } catch (...) {
            LOGGER->warn("Player: failed to create Math.RandomGenerator");
        }
    }

    Player::~Player() {
        // libkrkr2.so player+760 SeparateLayerAdaptor is owned by Player and
        // released on teardown (raw pointer + manual new/delete, matching the
        // binary object lifetime). Aligned with sub_6C4E28 @0x6C5DBC.
        delete _renderSeparateLayerAdaptor;
        _renderSeparateLayerAdaptor = nullptr;
    }

    bool Player::getPlaying() const {
        // Player_getPlaying @ 0x6D9794: return byte player+1099.
        static int traceCount = 0;
        if(detail::logoChainTraceEnabled(_activeMotion) &&
           traceCount < 80) {
            ++traceCount;
            detail::logoChainTraceLogf(
                _activeMotion->path, "getPlaying", "0x6D9794",
                _clampedEvalTime, "value={} timelineCount={} playingLabels={}",
                _allplaying ? 1 : 0, _timelines.size(),
                _playingTimelineLabels.size());
        }
        return _allplaying;
    }

    bool Player::getAllplaying() const {
        // Player_getAllplaying @ 0x6CCE34: child Motion players can keep the
        // aggregate playing state true after the owner-level flag is clear.
        static int traceCount = 0;
        if(true) {
            for(const auto &node : _nodes) {
                if(auto *child = node.getChildPlayer()) {
                    if(child->getAllplaying()) {
                        if(detail::logoChainTraceEnabled(_activeMotion) &&
                           traceCount < 80) {
                            ++traceCount;
                            detail::logoChainTraceLogf(
                                _activeMotion->path, "getAllplaying",
                                "0x6CCE34", _clampedEvalTime,
                                "value=1 reason=child nodeIndex={} localPlaying={} labels={}",
                                node.index, _allplaying ? 1 : 0,
                                _playingTimelineLabels.size());
                        }
                        return true;
                    }
                }
            }
        }
        if(detail::logoChainTraceEnabled(_activeMotion) &&
           traceCount < 80) {
            ++traceCount;
            detail::logoChainTraceLogf(
                _activeMotion->path, "getAllplaying", "0x6CCE34",
                _clampedEvalTime, "value={} reason=local labels={}",
                _allplaying ? 1 : 0,
                _playingTimelineLabels.size());
        }
        return _allplaying;
    }

    // Aligned to libkrkr2.so Player_getRootX (0x6D98A8) / Player_setRootX (0x6CD028):
    //   sub_6CD028: if (root.delta.posX != v) { root.delta.posX = v; root.delta.dirty = 1; }
    //   — writes node+1592 (delta.posX) and sets node+1584 (delta.dirty).
    double Player::getX() const {
        if (!_nodes.empty())
            return _nodes[0].delta.posX;
        return _hasPendingRootPos ? _pendingRootX : 0.0;
    }
    void Player::setX(double v) {
        _pendingRootX = v;
        _hasPendingRootPos = true;
        if (!_nodes.empty()) {
            auto &root = _nodes[0];
            if (root.delta.posX != v) {
                root.delta.posX = v;
                root.delta.dirty = true;
            }
        }
    }
    // Aligned to libkrkr2.so Player_getAngleRad @ 0x6CD0C0 (IDB symbol corrected
    // 2026-06-03; was formerly mislabeled "Player_getAngleDeg"):
    //   if (*(BYTE*)(player+482)) v1 = player+464;       (directEdit emote angle)
    //   else                      v1 = *(player+200)+1616; (root node delta.angle)
    //   return *v1 * 0.0174532925;                        (deg -> rad; pi/180)
    // player+482=_directEdit; player+464=_emoteAngle; (player+200)+1616 =
    // _nodes[0].delta.angle. Returns radians (used by emote spring step pass).
    double Player::emoteGetAngleRadLike_0x6CD0C0() const {
        double angleDeg;
        if (_directEdit) {                               // *(BYTE*)(player+482)  /*0x6cd0c0*/
            angleDeg = _emoteAngle;                      // player+464           /*0x6cd0c8*/
        } else {
            angleDeg = _nodes.empty() ? 0.0
                                      : _nodes[0].delta.angle; // node+1616      /*0x6cd0d4*/
        }
        return angleDeg * 0.0174532925;                  //                      /*0x6cd0e8*/
    }

    // Aligned to libkrkr2.so Player_getRootY (0x6D98B4) / Player_setRootY (0x6CD048):
    // same shape as setRootX but at node+1600 (delta.posY).
    double Player::getY() const {
        if (!_nodes.empty())
            return _nodes[0].delta.posY;
        return _hasPendingRootPos ? _pendingRootY : 0.0;
    }
    void Player::setY(double v) {
        _pendingRootY = v;
        _hasPendingRootPos = true;
        if (!_nodes.empty()) {
            auto &root = _nodes[0];
            if (root.delta.posY != v) {
                root.delta.posY = v;
                root.delta.dirty = true;
            }
        }
    }

    // angleDeg member setter = libkrkr2.so sub_6C0F84 @0x6C0F84. Input is in
    // DEGREES and stored directly (NO rad->deg conversion). If directEdit
    // (+482): normalize to [0,360), store _emoteAngle(+464), Player_initEmoteMotion(2).
    // Else: if root.delta.angle(+1616) != deg, set dirty(+1584) and store.
    //   if (*(BYTE*)(this+482)) { while(a2<0)a2+=360; while(a2>=360)a2-=360;
    //                             *(this+464)=a2; initEmoteMotion(this,2); }
    //   else { v=*(this+200); if(*(v+1616)!=a2){ *(v+1584)=1; *(v+1616)=a2; } }
    // Port stores deg in root.delta.angle (matching binary). initEmoteMotion(2)
    // omitted — port has no equivalent emote-mode re-init entry (pending spike).
    void Player::setAngleDeg(double deg) {
        if(_directEdit) {
            while(deg < 0.0) deg += 360.0;
            while(deg >= 360.0) deg -= 360.0;
            _emoteAngle = deg;
            // TODO M15: Player_initEmoteMotion(2) — port omits for now.
        } else if(!_nodes.empty()) {
            if(_nodes[0].delta.angle != deg) {
                _nodes[0].delta.angle = deg;
                _nodes[0].delta.dirty = true;
            }
        }
    }

    // angleRad member setter = libkrkr2.so Player_setAngleRad @0x6CD0EC (IDB
    // symbol corrected 2026-06-03; was formerly mislabeled "Player_setAngleDeg").
    // Input is RADIANS: deg = rad * 57.2957795, then the SAME store path as
    // setAngleDeg above (binary inlines an identical body).
    void Player::setAngleRad(double rad) {
        setAngleDeg(rad * 57.2957795);
    }

    // M15 missing `meshDivisionRatio` (cluster E §3.1): binary Motion.Player
    // exposes as property delegating to EmoteEngine +1168 (cluster E §1 ctor
    // confirmed +1168 lives on EmoteEngine, not Player; the NCB property on
    // Motion.Player is a delegate). Defaults to 1.0 if no engine attached.
    double Player::getMeshDivisionRatio() const {
        return _engineBack ? _engineBack->_meshDivisionRatio : 1.0;
    }

    void Player::setMeshDivisionRatio(double v) {
        if(_engineBack) {
            _engineBack->_meshDivisionRatio = v;
            _engineBack->_meshDivisionRatioDup = v;
        }
    }

    // M15 missing `bounds` property (cluster E §3.1): binary returns a TJS
    // dict {left, top, right, bottom} from _boundsMinX/MinY/MaxX/MaxY (binary
    // +152/+160/+168/+176 floats, init DBL_MAX/-DBL_MAX per ctor).
    tTJSVariant Player::getBounds() const {
        iTJSDispatch2 *dict = TJSCreateDictionaryObject();
        if(!dict) {
            return {};
        }
        tTJSVariant lx(_boundsMinX);
        tTJSVariant ty(_boundsMinY);
        tTJSVariant rx(_boundsMaxX);
        tTJSVariant by(_boundsMaxY);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("left"), nullptr, &lx, dict);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("top"), nullptr, &ty, dict);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("right"), nullptr, &rx, dict);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("bottom"), nullptr, &by, dict);
        tTJSVariant result(dict, dict);
        dict->Release();
        return result;
    }

    // M15 missing #10 (cluster E §4): binary `Player::setCoord` @0x6CCFF8
    // writes root+1592=x, root+1600=y, with a single combined dirty flag if
    // either changed. Atomic combined writer 1:1 with binary semantics.
    void Player::setCoord(double x, double y) {
        _pendingRootX = x;
        _pendingRootY = y;
        _hasPendingRootPos = true;
        if (!_nodes.empty()) {
            auto &root = _nodes[0];
            bool changed = false;
            if (root.delta.posX != x) {
                root.delta.posX = x;
                changed = true;
            }
            if (root.delta.posY != y) {
                root.delta.posY = y;
                changed = true;
            }
            if (changed)
                root.delta.dirty = true;
        }
    }

    // Aligned to libkrkr2.so EmoteObject_init (sub_67DBAC):
    // Sets activeMotion directly from a pre-loaded snapshot, bypassing file I/O.
    // Used by EmotePlayer.setModule() to bridge loaded PSB data into the Player pipeline.
    void Player::loadFromSnapshot(
        std::shared_ptr<detail::MotionSnapshot> snapshot) {
        _activeMotion.reset();
        _timelines.clear();
        _playingTimelineLabels.clear();
        _drawAffineMatrix = { 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
        _variableKeys.Clear();
        if(_engineBack) _engineBack->_variableAnimators.clear();
        clearControllerAnimatorStateLike_0x671228();
        _evalResultValues.clear();
        _evalResultList.clear();
        _evalResultListIndex.clear();

        if(snapshot) {
            activateMotion(*this, snapshot, &_resourceManagerNative);
            syncVariableKeysFromActiveMotion();

            // Aligned with libkrkr2.so EmoteObject_init @0x67DBAC: AFTER
            //   Player_play it calls EmoteEngine_applyMetadata_buildControllers
            //   (0x67D4D0) with the motion "metadata" dict (the FULL metadata,
            //   NOT metadata["base"] — corrects the earlier "base metadata"
            //   note: base @0x67dd6c is read only for chara/motion; the builder
            //   reads eyeControl/variableList/... straight off the metadata dict
            //   passed at 0x67dfa0). Here we wire the EYE category only (M2 eye
            //   vertical): metadata["eyeControl"] -> EmoteEngine::buildEyeControl.
            //   Remaining categories (eyebrow/mouth/transition/selector/timeline/
            //   bust/hair/parts/...) stay open.
            if(_engineBack && _activeMotion && _activeMotion->root) {
                const auto metadata = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                    (*_activeMotion->root)["metadata"]);
                if(metadata) {
                    // M2 spring-physics population: applyMetadata @0x67D4D0
                    //   dispatches bustControl/hairControl/partsControl BEFORE
                    //   eyeControl. DESPITE the key names, "bustControl" feeds the
                    //   SIMPLE spring (deque#1, stepHairParts) via sub_66B018, and
                    //   "hairControl"/"partsControl" feed the CHAIN spring
                    //   (deque#2/#3, stepBust) via sub_66B9D0(.,1)/(.,2). Same
                    //   fresh-build/re-load semantics (drop prior nodes + free
                    //   their springs first so we never double-populate / leak).
                    const auto bustControl = std::dynamic_pointer_cast<PSB::PSBList>(
                        (*metadata)["bustControl"]);
                    for(auto& node : _engineBack->_hairPartsNodes) {
                        delete node.spring;
                        node.spring = nullptr;
                    }
                    _engineBack->_hairPartsNodes.clear();
                    _engineBack->buildBustControl(bustControl.get());

                    const auto hairControl = std::dynamic_pointer_cast<PSB::PSBList>(
                        (*metadata)["hairControl"]);
                    for(auto& node : _engineBack->_bustChain1Nodes) {
                        delete node.spring;
                        node.spring = nullptr;
                    }
                    _engineBack->_bustChain1Nodes.clear();
                    _engineBack->buildChainControl(
                        _engineBack->_bustChain1Nodes, 1, hairControl.get());

                    const auto partsControl = std::dynamic_pointer_cast<PSB::PSBList>(
                        (*metadata)["partsControl"]);
                    for(auto& node : _engineBack->_bustChain2Nodes) {
                        delete node.spring;
                        node.spring = nullptr;
                    }
                    _engineBack->_bustChain2Nodes.clear();
                    _engineBack->buildChainControl(
                        _engineBack->_bustChain2Nodes, 2, partsControl.get());

                    const auto eyeControl = std::dynamic_pointer_cast<PSB::PSBList>(
                        (*metadata)["eyeControl"]);
                    // Fresh-build semantics: EmoteObject_init runs on a newly
                    //   ctor'd engine (deque#4 empty). On a re-load into an
                    //   existing engine, drop the prior eye controllers first so
                    //   we never double-populate.
                    for(auto& entry : _engineBack->_stateMachineDeque4) {
                        delete entry.ctl;
                        entry.ctl = nullptr;
                    }
                    _engineBack->_stateMachineDeque4.clear();
                    _engineBack->buildEyeControl(eyeControl.get());

                    // M2 eyebrow vertical: metadata["eyebrowControl"] ->
                    //   EmoteEngine::buildEyebrowControl (libkrkr2.so 0x66CB9C).
                    //   Same fresh-build/re-load semantics as the eye category
                    //   (drop prior controllers first so we never double-populate).
                    const auto eyebrowControl = std::dynamic_pointer_cast<PSB::PSBList>(
                        (*metadata)["eyebrowControl"]);
                    for(auto& entry : _engineBack->_stateMachineDeque5) {
                        delete entry.ctl;
                        entry.ctl = nullptr;
                    }
                    _engineBack->_stateMachineDeque5.clear();
                    _engineBack->buildEyebrowControl(eyebrowControl.get());

                    // M2 mouth vertical: metadata["mouthControl"] ->
                    //   EmoteEngine::buildMouthControl (libkrkr2.so 0x66CFBC).
                    //   Same fresh-build/re-load semantics (drop prior controllers
                    //   first so we never double-populate). The mouth builder
                    //   registers TWO HM#6 keys per controller (label+talkLabel).
                    const auto mouthControl = std::dynamic_pointer_cast<PSB::PSBList>(
                        (*metadata)["mouthControl"]);
                    for(auto& entry : _engineBack->_compositeVarDeque6) {
                        motion::EmoteMouthController_dtor(entry.ctl);
                        delete entry.ctl;
                        entry.ctl = nullptr;
                    }
                    _engineBack->_compositeVarDeque6.clear();
                    _engineBack->buildMouthControl(mouthControl.get());

                    // M2 transition vertical: metadata["transitionControl"] ->
                    //   EmoteEngine::buildTransitionControl (libkrkr2.so 0x66D4C4).
                    //   Same fresh-build/re-load semantics (drop prior controllers
                    //   first so we never double-populate). Each controller is
                    //   operator new(0x80); the deque entry owns it. MUST run
                    //   BEFORE buildSelectorControl below: the selector resolves
                    //   each option's borrowed refCtl by scanning THIS deque
                    //   (engine+576), so transition must be populated first
                    //   (mirrors applyMetadata's per-key order @0x67D4D0:
                    //   transitionControl is dispatched before selectorControl).
                    const auto transitionControl = std::dynamic_pointer_cast<PSB::PSBList>(
                        (*metadata)["transitionControl"]);
                    for(auto& entry : _engineBack->_auxVarDeque8) {
                        motion::EmoteVarController_dtor(entry.ctl);
                        delete entry.ctl;
                        entry.ctl = nullptr;
                    }
                    _engineBack->_auxVarDeque8.clear();
                    _engineBack->buildTransitionControl(transitionControl.get());

                    // M2 selector vertical: metadata["selectorControl"] ->
                    //   EmoteEngine::buildSelectorControl (libkrkr2.so 0x66D8FC).
                    //   Same fresh-build/re-load semantics (drop prior controllers
                    //   first so we never double-populate). Each controller is
                    //   operator new(0x80); the deque entry owns it.
                    const auto selectorControl = std::dynamic_pointer_cast<PSB::PSBList>(
                        (*metadata)["selectorControl"]);
                    for(auto& entry : _engineBack->_vectorVarDeque9) {
                        motion::EmoteSelectorController_dtor(entry.ctl);
                        delete entry.ctl;
                        entry.ctl = nullptr;
                    }
                    _engineBack->_vectorVarDeque9.clear();
                    _engineBack->buildSelectorControl(selectorControl.get());

                    // M2 loopControl vertical: metadata["loopControl"] ->
                    //   EmoteEngine::buildLoopControl (libkrkr2.so 0x66E480). This
                    //   is the LAST progress-stepped controller-deque (engine+736);
                    //   its step is inlined into progress @0x67d2a0. Same fresh-
                    //   build/re-load semantics (drop prior controllers first so we
                    //   never double-populate). Each controller is operator
                    //   new(0x20); the deque entry owns it. applyMetadata dispatches
                    //   loopControl AFTER selectorControl (@0x67d93c, well after
                    //   selector @0x67d8ec); the relative order is immaterial here
                    //   (loopControl has no cross-controller dependency), but we
                    //   keep the binary's per-key order for traceability.
                    const auto loopControl = std::dynamic_pointer_cast<PSB::PSBList>(
                        (*metadata)["loopControl"]);
                    for(auto& entry : _engineBack->_lookupCurvesDeque10) {
                        delete entry.ctl;
                        entry.ctl = nullptr;
                    }
                    _engineBack->_lookupCurvesDeque10.clear();
                    _engineBack->buildLoopControl(loopControl.get());
                }
            }
        }
    }

    double Player::getActiveMotionWidth() const {
        return _activeMotion ? _activeMotion->width : 0.0;
    }

    double Player::getActiveMotionHeight() const {
        return _activeMotion ? _activeMotion->height : 0.0;
    }

    // Aligned with libkrkr2.so Player_setChara @0x6D94B0 (NCB "chara" setter).
    //
    // Binary structure:
    //   if (*(this+968) /* current chara variant slot */) {
    //       sub_6B29C0(this, 16, &v);              // write chara -> +968 (dedup)
    //       if (*(this+776)) {                     // pending chara override slot
    //           sub_6B29C0(this, 16, this+776);    // re-apply pending into +968
    //           Release(*(this+776)); *(this+776)=0;
    //       }
    //   } else {                                   // first-ever set: just stash raw
    //       AddRef(v); Release(old +776); *(this+776) = v;
    //   }
    //
    // sub_6B29C0 @0x6B29C0 (the chara/key slot writer) does two things that
    // matter to the source-level architecture:
    //   1. dedup via wcscmp (sub_9B1ED0): if the new chara equals the stored
    //      chara it returns WITHOUT side effects.
    //   2. on an actual change it clears the loaded-motion slots
    //      (+976 motion, +984 motion2) and the motion-loaded byte (+1099),
    //      i.e. a chara change invalidates the currently loaded motion so the
    //      next play()/update reloads the PSB against the new chara.
    //
    // chara value storage: the binary keeps a tTJSVariant* at +968 with manual
    // ldaxr/stlxr AddRef + Release of the old value. The local ttstr _chara is
    // the platform-independent value-semantics equivalent of that refcounted
    // slot (per the byte-layout methodology in CLAUDE.md) - covering +968's
    // AddRef/Release is NOT a deviation, it is the same object lifetime.
    //
    // The architecturally load-bearing piece that the previous plain
    // `_chara = v;` was missing is the chara-change -> motion-invalidation
    // side effect. Without it, a chara change that keeps the same motion key
    // would skip reload because the local same-motion guards in
    // findMotion (PlayerMotionLoad.cpp:24,30) key only on the motion name,
    // never on chara.
    //
    // The +776 "pending chara override" slot has no setChara-path producer in
    // the local data flow (it is written only by the child-motion / stealth
    // passes, e.g. Player_updateLayers_childMotionPass @0x6BE0C0), so there is
    // no pending value to flush here; the first-set vs subsequent-set branch
    // collapses to "dedup, then on change store + invalidate motion".
    void Player::setChara(ttstr v) {
        // sub_6B29C0 dedup (sub_9B1ED0 wcscmp): unchanged chara is a no-op.
        if(_chara == v) {
            return;
        }
        // *(this+968) = v  (refcount-equivalent value store).
        _chara = v;
        // sub_6B29C0 motion-slot invalidation: clear +976/+984 motion slots and
        // the +1099 motion-loaded flag so the next play()/ensureMotionLoaded()
        // reloads the motion against the new chara. _activeMotion is the local
        // analog of the loaded-motion slots; clearing _motionKey forces
        // ensureMotionLoaded/findMotion past their same-motion guards.
        _activeMotion.reset();
        _motionKey = ttstr();
    }

    void Player::setMotion(ttstr v) {
        if(_motionKey == v) {
            return;
        }
        _motionKey = v;
        _activeMotion.reset();
        _timelines.clear();
        _playingTimelineLabels.clear();
        _drawAffineMatrix = { 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
        _variableKeys.Clear();
        if(_engineBack) _engineBack->_variableAnimators.clear();
        clearControllerAnimatorStateLike_0x671228();
        _evalResultValues.clear();
        _evalResultList.clear();
        _evalResultListIndex.clear();
        if(ensureMotionLoaded()) {
            initNonEmoteMotionLike_0x6B365C(0);
        }
    }

    // Aligned to libkrkr2.so 0x681CAC → 0x6B0F10:
    // motion setter calls objthis.onFindMotion({chara, motion}) to let
    // TJS participate in path resolution before loading the PSB.
    tjs_error Player::setMotionCompat(tTJSVariant *result, tjs_int numparams,
                                      tTJSVariant **param,
                                      iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) return TJS_E_INVALIDOBJECT;

        ttstr motionValue;
        if(numparams > 0 && param[0] && param[0]->Type() != tvtVoid) {
            motionValue = *param[0];
        }

        if(self->_motionKey == motionValue) {
            return TJS_S_OK;
        }

        // Build dict {chara, motion} and call objthis.onFindMotion(dict)
        // Aligned to libkrkr2.so Player_loadMotion_guess (0x6B0F10)
        tTJSVariant dictVar = detail::makeDictionary({
            {"chara", tTJSVariant(self->_chara)},
            {"motion", tTJSVariant(motionValue)}
        });
        tTJSVariant onFindResult;
        tTJSVariant *args[] = { &dictVar };
        tjs_error hr = objthis->FuncCall(0, TJS_W("onFindMotion"),
                                          nullptr, &onFindResult, 1, args, objthis);

        // Read back (possibly modified) chara and motion from result
        if(TJS_SUCCEEDED(hr) && onFindResult.Type() == tvtObject) {
            iTJSDispatch2 *resObj = onFindResult.AsObjectNoAddRef();
            if(resObj) {
                tTJSVariant charaVal, motionVal;
                if(TJS_SUCCEEDED(resObj->PropGet(TJS_MEMBERMUSTEXIST,
                    TJS_W("chara"), nullptr, &charaVal, resObj))
                    && charaVal.Type() != tvtVoid) {
                    self->_chara = ttstr(charaVal);
                }
                if(TJS_SUCCEEDED(resObj->PropGet(TJS_MEMBERMUSTEXIST,
                    TJS_W("motion"), nullptr, &motionVal, resObj))
                    && motionVal.Type() != tvtVoid) {
                    motionValue = ttstr(motionVal);
                }
            }
        }

        // Reset state and load
        self->_motionKey = motionValue;
        self->_activeMotion.reset();
        self->_timelines.clear();
        self->_playingTimelineLabels.clear();
        self->_drawAffineMatrix = { 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
        self->_variableKeys.Clear();
        self->_evalResultValues.clear();
        if(self->ensureMotionLoaded()) {
            self->initNonEmoteMotionLike_0x6B365C(0);
        }

        return TJS_S_OK;
    }

    tjs_error Player::getMotionCompat(tTJSVariant *result, tjs_int,
                                      tTJSVariant **, iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) return TJS_E_INVALIDOBJECT;
        // Player_getMotion_ncb @ 0x6D9544 returns native player+976.
        // _motionKey is the local mirror of that getter-visible slot.
        if(result) *result = tTJSVariant(self->_motionKey);
        return TJS_S_OK;
    }

    bool Player::ensureMotionLoaded() {
        if(_activeMotion) {
            return true;
        }

        const auto motionKey = detail::narrow(_motionKey);
        const bool motionKeyLooksLikeStorage =
            motionKey.find('/') != std::string::npos ||
            motionKey.find('\\') != std::string::npos ||
            motionKey.find('.') != std::string::npos;

        if(_project.Type() == tvtObject) {
            if(const auto snapshot = detail::lookupModuleSnapshot(_project)) {
                activateMotion(*this, snapshot, &_resourceManagerNative);
                syncVariableKeysFromActiveMotion();
                return true;
            }
        }

        if(motionKeyLooksLikeStorage) {
            if(const auto snapshot =
                   resolveMotion(*this, _motionKey, &_resourceManagerNative)) {
                activateMotion(*this, snapshot, &_resourceManagerNative);
                syncVariableKeysFromActiveMotion();
                return true;
            }
        }

        if(const auto loaded = _resourceManagerNative.getLastLoadedModule();
           loaded.Type() == tvtObject) {
            if(const auto snapshot = detail::lookupModuleSnapshot(loaded)) {
                activateMotion(*this, snapshot, &_resourceManagerNative);
                syncVariableKeysFromActiveMotion();
                return true;
            }
        }

        if(_motionKey.IsEmpty()) {
            return false;
        }

        if(const auto snapshot =
               resolveMotion(*this, _motionKey, &_resourceManagerNative)) {
            activateMotion(*this, snapshot, &_resourceManagerNative);
            syncVariableKeysFromActiveMotion();
            return true;
        }

        return false;
    }

    void Player::initNonEmoteMotionLike_0x6B365C(std::uint32_t playFlags) {
        if(!_activeMotion || _isEmoteMode) {
            return;
        }

        const auto *clip = selectActiveClip();
        _activeClip = clip;

        resetNodeTreeForBuildLike_0x6B56F8();
        _parameterEntries.clear();
        _parameterEntryById.clear();
        _defaultParameterEntry = {};
        _defaultParameterEntry.rangeScale = 1.0;
        _defaultParameterEntry.mode = 0;
        _defaultParameterEntryPtr = nullptr;
        _defaultParameterEntryIndex = -1;

        if(clip != nullptr) {
            _loopTime = clip->loopTime;
            _cachedTotalFrames = clip->totalFrames;
        }

        const auto motionObject = clip ? clip->motionObject : nullptr;
        if(motionObject) {
            const auto parameterizeValue = (*motionObject)["parameterize"];
            if(auto parameterizeObject =
                   std::dynamic_pointer_cast<const PSB::PSBDictionary>(
                       parameterizeValue)) {
                appendParameterEntryLike_0x6B1718(parameterizeObject);
                finalizeParameterTableLike_0x6B1ECC();
                if(!_parameterEntries.empty()) {
                    _defaultParameterEntryIndex = 0;
                    _defaultParameterEntryPtr =
                        &_parameterEntries.front();
                }
            } else {
                parseParameterListLike_0x6B202C((*motionObject)["parameter"]);
                if(auto numeric =
                       std::dynamic_pointer_cast<PSB::PSBNumber>(
                           parameterizeValue)) {
                    int index = 0;
                    switch(numeric->numberType) {
                        case PSB::PSBNumberType::Float:
                            index = static_cast<int>(
                                numeric->getValue<float>());
                            break;
                        case PSB::PSBNumberType::Double:
                            index = static_cast<int>(
                                numeric->getValue<double>());
                            break;
                        case PSB::PSBNumberType::Int:
                            index = numeric->getValue<int>();
                            break;
                        case PSB::PSBNumberType::Long:
                        default:
                            index = static_cast<int>(
                                numeric->getValue<tjs_int64>());
                            break;
                    }
                    if(index < 0 ||
                       static_cast<size_t>(index) >=
                           _parameterEntries.size()) {
                        throw std::out_of_range("parameter id out of range.");
                    }
                    _defaultParameterEntryIndex = index;
                    _defaultParameterEntryPtr =
                        &_parameterEntries[static_cast<size_t>(index)];
                }
            }
        }

        buildNodeTree();
        initVariables();

        if((playFlags & PlayFlagChain) == 0) {
            _frameTickCount = 0.0;
            _clampedEvalTime = std::min(_cachedTotalFrames, 0.0);
            _queuing = true;
        }
        // R2: binary Player_initNonEmoteMotion @ 0x6B3A78 writes STRH 0x100
        // unconditionally (no chain branch). Previously port set _allplaying
        // twice (inside chain-skip branch + unconditional below); R2 spike
        // confirmed only one write is needed. Single unconditional write.
        _allplaying = true;
    }

    void Player::syncVariableKeysFromActiveMotion() {
        if(!_activeMotion) {
            _variableKeys = detail::makeArray({});
            return;
        }

        _variableKeys = detail::makeArray(
            detail::stringsToVariants(_activeMotion->variableLabels));
        syncSelectorControlsLike_0x670D1C();
    }

    void Player::syncSelectorControlsLike_0x670D1C() {
        const auto *activeMotion = _activeMotion.get();
        if(!activeMotion) {
            return;
        }

        const auto removeRuntimeState =
            [this](const std::string &label) {
                if(label.empty()) {
                    return;
                }
                if(_engineBack) _engineBack->_variableAnimators.erase(label);
                eraseControllerAnimatorStateLike_0x671228(label);
                // HM2 (Player+320) is ttstr-keyed; widen the std::string label.
                _evalResultValues.erase(detail::widen(label));
                removeEvalResultSlotLike_Reset(label);
            };

        for(const auto &[selectorLabel, binding] : activeMotion->selectorControls) {
            removeRuntimeState(selectorLabel);
            for(const auto &option : binding.options) {
                removeRuntimeState(option.label);
            }
        }

        if(!_selectorEnabled) {
            if(_engineBack) _engineBack->_dirty = true;
            return;
        }

        // Aligned to libkrkr2.so sub_670D1C @0x670d98..0x670e1c: the
        //   selector-enabled path iterates the EmoteEngine selector deque #9
        //   (engine+656 = _vectorVarDeque9) and, for each entry whose gate byte
        //   (+1160) is set, resets the controller's selection state
        //   (*(ctl+84)=0) and calls EmoteSelectorController_applySelection(ctl,
        //   0, 0.0, 0.0) DIRECTLY @0x670e1c. It does NOT route through
        //   setVariable/0x671228 — the prior local setVariable(...) call was a
        //   non-faithful approximation that depended on the removed Player-side
        //   shim. Drive the selector controllers in-place to match the binary.
        if(_engineBack) {
            for(auto &entry : _engineBack->_vectorVarDeque9) {
                if(!entry.ctl) {
                    continue;
                }
                entry.ctl->selectedIndex = 0; // *(ctl+84)=0 @0x670e0c
                EmoteSelectorController_applySelection(entry.ctl, 0, 0.0f,
                                                       0.0f); // @0x670e1c
            }
            _engineBack->_dirty = true;
        }
    }

    const detail::TimelineState *Player::primaryTimelineStateLike_0x66F80C() const {
        if(!_activeMotion) {
            return nullptr;
        }

        const auto &primaryLabels =
            !_activeMotion->mainTimelineLabels.empty()
                ? _activeMotion->mainTimelineLabels
                : _activeMotion->diffTimelineLabels;
        for(const auto &label : primaryLabels) {
            if(const auto it = _timelines.find(label);
               it != _timelines.end()) {
                return &it->second;
            }
        }

        if(!_motionKey.IsEmpty()) {
            if(const auto it = _timelines.find(detail::narrow(_motionKey));
               it != _timelines.end()) {
                return &it->second;
            }
        }

        return !_timelines.empty()
            ? &(_timelines.begin()->second)
            : nullptr;
    }

    void Player::resetControllerStateLike_0x66EB8C() {
        // Aligned to libkrkr2.so sub_66EB8C:
        // the binary performs a broad controller/reset sweep after wrapper-side
        // setMirror(). Keep the local reset focused on runtime controller state,
        // eval sinks, and root-node dirty propagation.
        if(_engineBack) _engineBack->_variableAnimators.clear();
        clearControllerAnimatorStateLike_0x671228();
        _evalResultValues.clear();
        _evalResultList.clear();
        _evalResultListIndex.clear();

        if(!_nodes.empty()) {
            auto &root = _nodes.front();
            // Aligned to libkrkr2.so Player_setRootFlipX (0x6CD068):
            // writes node+1587 (delta.flipX), sets node+1584 (delta.dirty).
            root.delta.flipX = _rootFlipX;
            root.delta.dirty = true;
            root.interpolatedCache.flipX = _rootFlipX;
        }

        if(_selectorEnabled) {
            syncSelectorControlsLike_0x670D1C();
        }
        if (_engineBack) _engineBack->_dirty = true;
    }

    const detail::MotionClip *Player::selectActiveClip() const {
        if(!_activeMotion) {
            return nullptr;
        }

        const auto &motion = *_activeMotion;
        const auto selectByLabel =
            [&motion](const std::string &label) -> const detail::MotionClip * {
                if(label.empty()) {
                    return nullptr;
                }
                const auto it = motion.clipIndexByLabel.find(label);
                if(it == motion.clipIndexByLabel.end()) return nullptr;
                const int idx = it->second;
                if(idx < 0 || idx >= static_cast<int>(motion.clipList.size()))
                    return nullptr;
                return &motion.clipList[idx];
            };

        // Aligned to libkrkr2.so Player_playImpl (0x6B2284):
        // the requested motion/timeline label is stored on the player before
        // the non-emote init path rebuilds content/node state. In the local
        // architecture, this is the closest equivalent to the binary's
        // selected content object, so prefer _motionKey before falling back to
        // the playing-timeline list or primary label ordering.
        if(!_motionKey.IsEmpty()) {
            if(const auto *clip = selectByLabel(detail::narrow(_motionKey))) {
                return clip;
            }
        }

        for(const auto &label : _playingTimelineLabels) {
            if(const auto *clip = selectByLabel(label)) {
                return clip;
            }
        }

        const auto &primaryLabels =
            !motion.mainTimelineLabels.empty()
                ? motion.mainTimelineLabels
                : motion.diffTimelineLabels;
        for(const auto &label : primaryLabels) {
            if(const auto *clip = selectByLabel(label)) {
                return clip;
            }
        }

        // Fallback — aligned to libkrkr2.so Player_initNonEmoteMotion reading
        // priority[0].content at 0x6B38FC when no explicit selection exists.
        if(motion.clipList.size() == 1) {
            return &motion.clipList.front();
        }
        if(!motion.clipList.empty()) {
            return &motion.clipList.front();
        }

        return nullptr;
    }

    const std::vector<std::string> &Player::activeSourceCandidates() const {
        static const std::vector<std::string> empty;
        if(!_activeMotion) {
            return empty;
        }

        if(const auto *clip = selectActiveClip();
           clip && !clip->sourceCandidates.empty()) {
            return clip->sourceCandidates;
        }

        return _activeMotion->sourceCandidates;
    }

    tTJSVariant Player::getVariableKeys() {
        ensureMotionLoaded();
        if(_variableKeys.Type() == tvtVoid) {
            return detail::makeArray({});
        }
        return _variableKeys;
    }

    void Player::setProgressCompat(double v) {
        ensureMotionLoaded();
        const auto progress = std::clamp(v, 0.0, 1.0);
        _playingTimelineLabels.clear();
        for(auto &[_, state] : _timelines) {
            if(state.totalFrames > 0.0) {
                state.currentTime = state.totalFrames * progress;
            } else {
                state.currentTime = progress;
            }
            if(progress >= 1.0 && !state.loop) {
                state.playing = false;
            }
            state.controlInitialized = false;
            state.controlLastAppliedTime = state.currentTime;
            state.controlFrameCursor.clear();
            state.controlTrackValues.clear();
            state.controlTrackAnimators.clear();
            if(state.playing) {
                _playingTimelineLabels.push_back(state.label);
            }
        }
        _allplaying = !_playingTimelineLabels.empty();
    }

    double Player::getProgressCompat() const {
        bool sawTimeline = false;
        bool anyPlaying = false;
        double progress = 0.0;

        for(const auto &[_, state] : _timelines) {
            sawTimeline = true;
            anyPlaying = anyPlaying || state.playing;
            if(state.totalFrames > 0.0) {
                progress = std::max(
                    progress,
                    std::clamp(state.currentTime / state.totalFrames, 0.0, 1.0));
            } else if(!state.playing) {
                progress = std::max(progress, 1.0);
            }
        }

        if(!sawTimeline) {
            return _allplaying ? 0.0 : 1.0;
        }
        if(!anyPlaying) {
            return 1.0;
        }
        return progress;
    }

    // --- Core methods ---
    // Aligned to libkrkr2.so sub_6BA7B8 at 0x6BA7B8:
    // 1. sub_A0F5E0(v9, a1+992) — read TJS dispatch from player+992
    // 2. FuncCall(obj, 0, L"random", ...) — call "random" method
    // 3. Convert result variant to double (case 2→real, case 4→int→double, case 5→raw)
    //
    // player+992 is initialized once via "new Math.RandomGenerator()" (sub_6A88CC at 0x6A8988).
    // Child Players inherit the same object from parent (sub_6CED30 at 0x6CED30: a1+992 = a2).
    double Player::random() {
        if (_tjsRandomGenerator.Type() == tvtObject) {
            iTJSDispatch2 *obj = _tjsRandomGenerator.AsObjectNoAddRef();
            if (obj) {
                tTJSVariant result;
                static tjs_uint32 hint = 0;
                tjs_error hr = obj->FuncCall(0, TJS_W("random"), &hint,
                                             &result, 0, nullptr, obj);
                if (TJS_SUCCEEDED(hr))
                    return static_cast<double>(result);
            }
        }
        return 0.0;
    }

    // Aligned to libkrkr2.so sub_6709AC at 0x6709AC:
    // initPhysics(min, max, amp, freq1, freq2) creates a Spring physics object
    // at player+1128 (1564 bytes, sub_670AFC). Stores params at player+1136..1152.
    // The Spring physics system drives emote hair/bust/parts oscillation.
    // Full spring simulation not yet implemented for web port — store params only.
    void Player::initPhysics() {
        // Parameters come from NCB: initPhysics(min, max, amp, freq1, freq2)
        // In the NCB binding this is a raw callback. The actual parameters
        // are parsed by the NCB wrapper. Since we store the scale values
        // (hairScale/partsScale/bustScale) and these physics params would
        // drive them, we log but accept the call.
        // player+1128 = physics object (not created)
        // player+1136..1152 = min, max, amplitude, freq1, freq2
    }
    tTJSVariant Player::serialize() {
        ensureMotionLoaded();

        std::vector<std::pair<std::string, tTJSVariant>> variables;
        std::unordered_set<std::string> seenVariables;
        if(_activeMotion) {
            for(const auto &label : _activeMotion->variableLabels) {
                seenVariables.insert(label);
                variables.emplace_back(label, getVariable(detail::widen(label)));
            }
        }
        for(const auto &[label, value] : _evalResultValues) {
            // HM2 keys are ttstr (Player+320); narrow for the std::string-keyed
            // seen-set / output dictionary.
            const auto narrowLabel = detail::narrow(label);
            if(seenVariables.insert(narrowLabel).second) {
                variables.emplace_back(narrowLabel, value);
            }
        }

        return detail::makeDictionary({
            { "chara", _chara },
            { "motion", _motionKey },
            { "tickcount", getTickCount() },
            { "speed", _speed },
            { "outline", tTJSVariant(_outline) },
            { "variables", detail::makeDictionary(variables) },
            { "timelines", getPlayingTimelineInfoList() },
        });
    }

    void Player::unserialize(tTJSVariant data) {
        if(data.Type() != tvtObject || data.AsObjectNoAddRef() == nullptr) {
            return;
        }

        tTJSVariant value;
        if(getObjectProperty(data, TJS_W("chara"), value) &&
           value.Type() != tvtVoid) {
            _chara = value;
        }

        if(getObjectProperty(data, TJS_W("motion"), value) &&
           value.Type() != tvtVoid) {
            _motionKey = value;
            ensureMotionLoaded();
        }

        if(getObjectProperty(data, TJS_W("tickcount"), value) &&
           value.Type() != tvtVoid) {
            setTickCount(value.AsReal());
        }

        if(getObjectProperty(data, TJS_W("speed"), value) &&
           value.Type() != tvtVoid) {
            _speed = value.AsReal();
        }

        if(getObjectProperty(data, TJS_W("outline"), value) &&
           value.Type() != tvtVoid) {
            _outline = ttstr(value);
        }

        if(getObjectProperty(data, TJS_W("variables"), value) &&
           value.Type() == tvtObject && value.AsObjectNoAddRef() != nullptr) {
            DictionaryEnumerator callback;
            tTJSVariantClosure closure(&callback, nullptr);
            value.AsObjectNoAddRef()->EnumMembers(TJS_IGNOREPROP, &closure,
                                                  value.AsObjectNoAddRef());
            for(const auto &[label, stored] : callback.entries) {
                if(stored.Type() != tvtVoid) {
                    // Restore each saved variable straight into Player HM1/HM2
                    //   (the map Player::serialize read via getVariable). This is
                    //   the faithful Player-side write = Player_bindParameterValue
                    //   @0x6C4668 (= the Motion.Player.setVariable NCB member,
                    //   callback @0x6D0E70 -> 0x6C4668). The removed 4-arg
                    //   Player::setVariable shim used to wrap this same write.
                    writeEvalResultValueLike_0x6C4668(detail::narrow(label),
                                                      stored.AsReal());
                }
            }
        }

        bool restoredTimelines = false;
        if(getObjectProperty(data, TJS_W("timelines"), value) &&
           value.Type() == tvtObject && value.AsObjectNoAddRef() != nullptr) {
            ensureMotionLoaded();
            if(_activeMotion && _timelines.empty()) {
                detail::primeTimelineStates(_timelines,
                                            *_activeMotion);
            }
            _playingTimelineLabels.clear();

            const auto count = getObjectCount(value);
            for(tjs_int index = 0; index < count; ++index) {
                tTJSVariant item;
                if(!getArrayItem(value, index, item) || item.Type() != tvtObject ||
                   item.AsObjectNoAddRef() == nullptr) {
                    continue;
                }

                tTJSVariant labelValue;
                if(!getObjectProperty(item, TJS_W("label"), labelValue) ||
                   labelValue.Type() == tvtVoid) {
                    continue;
                }

                const auto key = detail::narrow(labelValue);
                auto it = _timelines.find(key);
                if(it == _timelines.end()) {
                    continue;
                }

                restoredTimelines = true;
                it->second.playing = true;
                _playingTimelineLabels.push_back(key);
                it->second.controlInitialized = false;
                it->second.controlLastAppliedTime = it->second.currentTime;
                it->second.controlFrameCursor.clear();
                it->second.controlTrackValues.clear();
                it->second.controlTrackAnimators.clear();

                tTJSVariant flagsValue;
                if(getObjectProperty(item, TJS_W("flags"), flagsValue) &&
                   flagsValue.Type() != tvtVoid) {
                    it->second.flags = flagsValue.AsInteger();
                }

                tTJSVariant currentTimeValue;
                if(getObjectProperty(item, TJS_W("currentTime"), currentTimeValue) &&
                   currentTimeValue.Type() != tvtVoid) {
                    it->second.currentTime = currentTimeValue.AsReal();
                }

                tTJSVariant blendRatioValue;
                if(getObjectProperty(item, TJS_W("blendRatio"), blendRatioValue) &&
                   blendRatioValue.Type() != tvtVoid) {
                    it->second.blendRatio = blendRatioValue.AsReal();
                }
            }
        }

        if(!restoredTimelines && ensureMotionLoaded()) {
            if(_timelines.empty()) {
                detail::primeTimelineStates(_timelines,
                                            *_activeMotion);
            }
            const auto &primary = !_activeMotion->mainTimelineLabels.empty()
                ? _activeMotion->mainTimelineLabels
                : _activeMotion->diffTimelineLabels;
            for(const auto &label : primary) {
                playTimeline(detail::widen(label), PlayFlagForce);
            }
        }

        _allplaying = !_playingTimelineLabels.empty();
    }

    // Aligned to libkrkr2.so D3DEmotePlayer_setCoord (0x5301EC):
    // store the coord animator payload on Player and keep root x/y in sync.
    void Player::setEmoteCoord(double x, double y, double transition,
                               double ease) {
        _emoteCoordState.x = x;
        _emoteCoordState.y = y;
        _emoteCoordState.transition = transition;
        _emoteCoordState.ease = ease;
        setX(x);
        setY(y);
        if (_engineBack) _engineBack->_dirty = true;
    }

    // Aligned to libkrkr2.so D3DEmotePlayer_setScale (0x530260):
    // the wrapper multiplies baseScale * userScale, then forwards the final
    // scalar plus transition/ease to the inner Player scale animator.
    void Player::setEmoteScale(double scale, double transition, double ease) {
        _emoteScaleState.value = scale;
        _emoteScaleState.transition = transition;
        _emoteScaleState.ease = ease;
        if (_engineBack) _engineBack->_dirty = true;
    }

    // Aligned to libkrkr2.so D3DEmotePlayer_setRot (0x5302E4):
    // read player+1161, set player+1162=1, then forward rot/transition/ease
    // to the Player rot animator sink.
    void Player::setRotate(double rot, double transition, double ease) {
        _rotateAngle = rot;
        _emoteRotState.value = rot;
        _emoteRotState.transition = transition;
        _emoteRotState.ease = ease;
        if (_engineBack) _engineBack->_dirty = true;
    }

    // Aligned to libkrkr2.so D3DEmotePlayer_setColor (0x530314):
    // unpack AARRGGBB into four float byte values and forward them to the
    // Player color animator sink together with transition/ease.
    void Player::setEmoteColor(tjs_uint32 color, double transition,
                               double ease) {
        _emoteColorState.packed = color;
        _emoteColorState.rgbaBytes[0] =
            static_cast<float>(static_cast<std::uint8_t>(color));
        _emoteColorState.rgbaBytes[1] =
            static_cast<float>(static_cast<std::uint8_t>(color >> 8));
        _emoteColorState.rgbaBytes[2] =
            static_cast<float>(static_cast<std::uint8_t>(color >> 16));
        _emoteColorState.rgbaBytes[3] =
            static_cast<float>(static_cast<std::uint8_t>(color >> 24));
        _emoteColorState.transition = transition;
        _emoteColorState.ease = ease;
        if (_engineBack) _engineBack->_dirty = true;
    }

    void Player::setMirror(bool mirror) {
        // Aligned to libkrkr2.so Player_setRootFlipX (0x6CD068):
        // update the synthetic root node's flipX flag and mark it dirty.
        if(_rootFlipX == mirror && _mirrorEvalEnabled == mirror) {
            return;
        }

        _rootFlipX = mirror;
        _mirrorEvalEnabled = mirror;
        resetControllerStateLike_0x66EB8C();
    }

    void Player::setEmoteMeshDivisionRatio(double v) {
        // Migrated to EmoteEngine+1168/+1176 (per binary spec).
        if (_engineBack) {
            _engineBack->_meshDivisionRatio = v;
            _engineBack->_meshDivisionRatioDup = v;
        }
    }

    // hairScale/partsScale/bustScale removed from motion::Player: sub_681F20/28/30
    // are EmotePlayer NCB accessors writing EmoteObject+1184/+1192/+1200, not
    // Player fields (the 1384B Player has hash table HM3 at those offsets).
    // See EmotePlayer::setHairScale.

    // Aligned with libkrkr2.so Player_startWind_populate (sub_6709AC @0x6709AC).
    //   NOTE: the binary's `a1` is the EmoteEngine (it writes engine+1128 =
    //   wind emitter ptr, engine+1136..1152 = wind param cache, reads
    //   engine+1168 = mesh division ratio). The arg names follow the NCB
    //   `startWind(min, max, amplitude, freqX, freqY)` order.
    //
    //   void __fastcall (engine a1, float min a2, max a3, amp a4, fx a5, fy a6):
    //     v6  = |a4|;                                      // |amplitude|
    //     v9  = (a4 >= 0) ? a2 : a3;                       // normalized min
    //     v10 = (a4 >= 0) ? a3 : a2;                       // normalized max
    //     if (v6 == 0 || v10 == v9 || (a5 == 0 && a6 == 0)) {
    //         delete *(a1+1128); *(a1+1128) = 0; return;   // stop
    //     }
    //     v13 = *(a1+1128);
    //     if (!v13) goto ALLOC;
    //     if (*(float*)(a1+1136) != v9 || *(float*)(a1+1140) != v10) {
    //         delete *(a1+1128);
    //     ALLOC:
    //         v13 = operator new(0x61C);
    //         div = *(double*)(a1+1168);
    //         EmoteWindEmitter_init(v13, v9/div, v10/div);
    //         *(a1+1128) = v13;
    //     }
    //     *(float*)(a1+1136) = v9; *(a1+1140) = v10; *(a1+1144) = v6;
    //     *(float*)(a1+1148) = a5; *(a1+1152) = a6;
    //     dir = (*(float*)(v13+1540) < *(float*)(v13+1536)) ? -1 : 1;  // endPos<startPos
    //     *(float*)(v13+1548) = a5; *(v13+1552) = a6;                  // yHi, yLo
    //     *(byte*)(v13+1544) = 1;                                      // gate on
    //     *(float*)(v13+1556) = dir * (v6 / div);                      // velocity
    //     *(DWORD*)(v13+1560) = 0;                                     // emit accumulator
    void Player::startWind(double minAngle, double maxAngle, double amplitude,
                           double freqX, double freqY) {
        if (!_engineBack) {
            return;
        }
        EmoteEngine* const eng = _engineBack;

        const float v6  = static_cast<float>(std::abs(amplitude));        /*0x6709d8 |a4|*/
        const float v9  = static_cast<float>(amplitude >= 0.0 ? minAngle : maxAngle); /*0x6709e4*/
        const float v10 = static_cast<float>(amplitude >= 0.0 ? maxAngle : minAngle); /*0x6709e8*/
        const float a5  = static_cast<float>(freqX);
        const float a6  = static_cast<float>(freqY);

        if (v6 == 0.0f || v10 == v9 || (a5 == 0.0f && a6 == 0.0f)) {       /*0x670a14*/
            // stop: delete + null the emitter, leave caches as-is.
            if (eng->_windEmitter) {                                       /*0x670a18*/
                delete eng->_windEmitter;                                  /*0x670a20*/
                eng->_windEmitter = nullptr;                               /*0x670a24*/
            }
            return;                                                        /*0x670a28*/
        }

        const double div = eng->_meshDivisionRatio;                       /*0x670a5c *(a1+1168)*/

        EmoteWindEmitter* v13 = eng->_windEmitter;                        /*0x670a2c*/
        if (!v13 ||                                                        /*0x670a30 !v13 -> ALLOC*/
            eng->_windMin != v9 || eng->_windMax != v10) {                /*0x670a48 start/end changed*/
            if (v13) {                                                     /*0x670a50 delete old when rebuilding*/
                delete v13;
            }
            v13 = new EmoteWindEmitter();                                  /*0x670a54 operator new(0x61C)*/
            v13->init(static_cast<float>(v9 / div),                        /*0x670a7c init(startPos, endPos)*/
                      static_cast<float>(v10 / div));
            eng->_windEmitter = v13;                                       /*0x670a80*/
        }

        eng->_windMin   = v9;                                             /*0x670a84 *(a1+1136)*/
        eng->_windMax   = v10;                                            /*0x670a8c *(a1+1140)*/
        eng->_windAmp   = v6;                                             /*0x670a90 *(a1+1144)*/
        eng->_windFreqX = a5;                                             /*0x670a94 *(a1+1148)*/
        eng->_windFreqY = a6;                                             /*0x670a98 *(a1+1152)*/

        // v18 = endPos(+1540) < startPos(+1536) ; direction = v18 ? -1 : 1.
        const bool v18 = v13->endPos < v13->startPos;                     /*0x670ab0*/
        const float v20 = static_cast<float>(v6 / div);                  /*0x670ac8 v6/div*/
        const float v21 = v18 ? -1.0f : 1.0f;                            /*0x670acc*/
        v13->yHi = a5;                                                    /*0x670abc *(v13+1548)=a5*/
        v13->yLo = a6;                                                    /*0x670ac0 *(v13+1552)=a6*/
        v13->gate = 1;                                                    /*0x670ad4 *(v13+1544)=1*/
        v13->velocity = v21 * v20;                                       /*0x670ad8 *(v13+1556)*/
        v13->emitAccumulator = 0.0f;                                     /*0x670adc *(v13+1560)=0*/
    }

    // Aligned with libkrkr2.so D3DEmotePlayer_stopWind (0x53068C), which calls
    //   Player_startWind_populate with all-zero amplitude/freq -> hits the stop
    //   branch (delete + null the emitter at engine+1128).
    void Player::stopWind() {
        if (_engineBack && _engineBack->_windEmitter) {
            delete _engineBack->_windEmitter;
            _engineBack->_windEmitter = nullptr;
        }
    }

    // Aligned to D3DEmotePlayer_setOuterForce (0x530A8C) ->
    // Player_setOuterForce (0x672D58): case-insensitive label dispatch for
    // "bust", "h", and "parts", carrying transition/ease through the sink.
    void Player::setOuterForce(ttstr label, double x, double y,
                               double transition, double ease) {
        const auto key = lowerAscii(detail::narrow(label));
        OuterForceState *target = nullptr;
        if(key == "bust") {
            target = &_bustOuterForce;
        } else if(key == "h") {
            target = &_hairOuterForce;
        } else if(key == "parts") {
            target = &_partsOuterForce;
        } else {
            return;
        }

        target->active = true;
        target->x = x;
        target->y = y;
        target->transition = transition;
        target->ease = ease;
        if (_engineBack) _engineBack->_dirty = true;
    }

    // Aligned to libkrkr2.so sub_681EF8 at 0x681EF8:
    // Stores translate (x,y) to runtime+144/148 (cameraOffsetX/Y).
    // The full 6-param matrix version is handled by setDrawAffineTranslateMatrixCompat.
    void Player::setDrawAffineTranslateMatrix(tTJSVariant) {
        // Single-param variant: compat handler does the real work via NCB_METHOD_RAW
    }

    tTJSVariant Player::getCameraOffset() { return _cameraPosition; }

    void Player::setCameraOffset(tTJSVariant offset) {
        _cameraPosition = offset;
        // Aligned to libkrkr2.so sub_6D9A38: setCameraOffset(x, y)
        // Stores as float at Player+144/148. NCB passes a Point with x,y.
        if(offset.Type() == tvtObject) {
            auto *obj = offset.AsObjectNoAddRef();
            if(obj) {
                tTJSVariant xv, yv;
                if(obj->PropGet(0, TJS_W("x"), nullptr, &xv, obj) == TJS_S_OK)
                    _cameraOffsetX = static_cast<float>(xv.AsReal());
                if(obj->PropGet(0, TJS_W("y"), nullptr, &yv, obj) == TJS_S_OK)
                    _cameraOffsetY = static_cast<float>(yv.AsReal());
            }
        }
    }

    void Player::modifyRoot(tTJSVariant data) { _project = data; }

    void Player::debugPrint() {
        LOGGER->info("motionKey={}, motions={}, sources={}, timelines={}",
                     _motionKey.AsStdString(), _motionsByKey.size(),
                     _sourceCacheNative ? _sourceCacheNative->size()
                                                 : 0,
                     _timelines.size());
    }


} // namespace motion
