// PlayerCore.cpp — Constructor, setMotion, core properties
// Split from Player.cpp for maintainability.
//
#include <algorithm>
#include <cctype>
#include <cmath>

#include "PlayerInternal.h"
#include "EmotePlayer.h" // for EmoteEngine (back-pointer deref)
#include "MotionDispatch.h"
#include "SourceCache.h"
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

    bool shouldEmitCoreDiag(std::uint32_t seq) {
        return seq <= 200 || (seq % 100) == 0;
    }

    const char *coreDiagBool(bool v) {
        return v ? "true" : "false";
    }

}

namespace motion {

    // (Removed 2026-06-05) controllerAnimatorBucketLike_0x671228 / find... /
    //   erase... / clearControllerAnimatorStateLike_0x671228 + the findInDeque/
    //   eraseInDeque helpers. They operated on a parallel per-Player animator
    //   bucket set (`_type4..8ControllerAnimators` + `_variableAnimators`) that
    //   was residue of a superseded stepping model — never written (zero
    //   push/emplace across cpp/). Fresh decompile of EmoteEngine_progress
    //   @0x67D01C / setVariable @0x671228 confirms controller stepping reads ONLY
    //   the EmoteEngine typed deques #4-#9 (engine +256/+336/+416/+576/+656/+736)
    //   and writes into HM7 (+1440); no independent Player-side bucket exists.
    //   Removal is byte-neutral (containers were perpetually empty).

    // findOrInsertControllerStateLike_0x671228 removed 2026-06-03: its only
    //   call sites were inside the non-faithful Player-side
    //   setVariableResolvedWeightLike_0x671228 shim (a local reimplementation of
    //   the EmoteEngine HM6->deque dispatch that the binary does only inside
    //   EmoteEngine_setVariable @0x671228). With that shim removed, this helper
    //   is dead.

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

        // Player_setIndependentLayerInherit@0x6CC9D4 compares Player+1097 and
        // marks node+1584 dirty across the deque, but its complete 24-
        // instruction body never writes Player+1097.  Preserve that observable
        // boundary behavior; the type-3 construction path has its own direct
        // flag write at 0x6B4614.
        for(auto &node : _nodes) {
            node.delta.dirty = true;
        }
    }

    // P3-B (2026-06-05): RM dispatch-in. Aligned to Player_ctor @0x6CED30 —
    //   single-param `(this, iTJSDispatch2* rm_dispatch)`. The RM dispatch
    //   arrives from above (EmoteObject wraps the native RM via sub_67E20C and
    //   flows it down; child Players inherit the canonical RM owner). The
    //   binary retains the SAME dispatch pointer in three independent Variants
    //   (sub_A0F5E0, each AddRef'd); local Player does the same. It also creates
    //   one persistent source descriptor Dictionary, two initially-void
    //   internal Layer Variant slots and one persistent color Dictionary
    //   between the second and third RM owners. Player no longer creates its own
    //   RM nor owns one by value; the native is reached via nativeRM().
    //   root owner and parent are set post-construct
    //   (binary child+0=parent+0, child+8=parent @0x6B43DC).
    Player::Player(const tTJSVariant &rmDispatch) :
        _rootPlayer(this),
        _findSourceResourceManager(rmDispatch),
        _sourceCacheObject(rmDispatch),
        _resourceManager(rmDispatch) {
        LOGGER->info("Motion.Player constructor called");
        LOGGER->info("PRTDIAG Player::ctor this={} rmType={}",
                     static_cast<const void *>(this),
                     static_cast<int>(_resourceManager.Type()));
        // A10: makePlayerRuntime / ensureRootNodeLike previously ran inside
        // makePlayerRuntime; the call now lives here so the synthetic root
        // node lands on _nodes at index 0.
        _defaultParameterEntry.rangeScale = 1.0;
        _defaultParameterEntry.mode = 0;
        detail::ensureRootNodeLike_0x6CED30(*this);
        // Player_ctor @0x6CED30 copies the same ResourceManager dispatch into
        // three independent owners. ResourceManager_ctor @0x6A88CC constructs
        // its SourceCache base; no standalone SourceCache exists.
        _sourceCacheNative = nativeRM();
        // Player_ctor @0x6CED30 creates the render descriptor/color objects at
        // 0x6CF014..0x6CF080, then assigns descriptor.color = colors. Every
        // 0x6C1B70 caller mutates these same objects before entering the
        // Player-owned resolver. The resolver either uses its internal-Layer
        // fast path or dispatches ResourceManager.loadSource as fallback.
        iTJSDispatch2 *descriptor = TJSCreateDictionaryObject();
        _sourceDescriptor = tTJSVariant(descriptor, descriptor);
        descriptor->Release();

        iTJSDispatch2 *colors = TJSCreateDictionaryObject();
        _sourceColors = tTJSVariant(colors, colors);
        colors->Release();

        descriptor = _sourceDescriptor.AsObjectNoAddRef();
        (void)descriptor->PropSet(
            TJS_MEMBERENSURE, TJS_W("color"),
            &detail::colorMemberHint_guess, &_sourceColors, descriptor);
        // The random generator is owned by ResourceManager_ctor @0x6A88CC;
        // Player does not create or own another generator.
    }

    // P3-B: dispatch->native unpack. Binary findSource @0x694928 takes the +636
    //   RM dispatch, PropGet(hint, membername=NULL) returns the NCB instance
    //   dispatch, then reads `*(instance+8)` = native ResourceManager pointer
    //   (NCB tTJSNI_* layout). GetNativeInstance is the local equivalent of that
    //   +8 unpack. The native is owned by the dispatch refcount (created at the
    //   RM owner EmoteObject), so it outlives the Player as long as
    //   `_resourceManager` holds the dispatch ref.
    ResourceManager *Player::nativeRM() const {
        iTJSDispatch2 *dispatch =
            _findSourceResourceManager.Type() == tvtObject
                ? _findSourceResourceManager.AsObjectNoAddRef()
                : nullptr;
        if(!dispatch) {
            return nullptr;
        }
        return ncbInstanceAdaptor<ResourceManager>::GetNativeInstance(dispatch);
    }

    Player::~Player() {
        // Player_dtor@0x6CFADC calls 0x6CDE18 before releasing the +384
        // parameter vector. Values in every ancestor's +408 multimap can point
        // into this vector, so they must be removed while the entries are alive.
        purgeParameterRampMapLike_0x6CDE18();

        // Player_dtor@0x6CFADC releases and destroys every non-root node before
        // destroying the SeparateLayerAdaptor. The remaining root is destroyed
        // later by the deque member destructor.
        if(_nodes.size() > 1) {
            for(size_t i = 1; i < _nodes.size(); ++i) {
                dispatchReleaseLayerId(_nodes[i].layerId1);
                dispatchReleaseLayerId(_nodes[i].layerId2);
            }
        }
        detail::resetNodeTreeKeepRootLike_0x6B56F8(*this);

        delete _renderSeparateLayerAdaptor;
        _renderSeparateLayerAdaptor = nullptr;
    }

    std::string Player::matchedMotionPath() const {
        if(_findMotionContextVariant.Type() == tvtVoid) {
            return {};
        }
        return detail::narrow(ttstr(_findMotionContextVariant));
    }

    bool Player::getPlaying() const {
        // Player_getPlaying @ 0x6D9794: return byte player+1099.
        const auto motionPath = matchedMotionPath();
        if((detail::logoChainTraceEnabled() ||
            detail::logoChainTraceEnabledForPath(motionPath)) && LOGGER) {
            const auto path = motionPath.empty()
                ? std::string("<none>") : motionPath;
            LOGGER->info(
                "PRTDIAG Player::getPlaying this={} path='{}' value={}",
                static_cast<const void *>(this), path,
                _allplaying ? 1 : 0);
        }
        return _allplaying;
    }

    bool Player::getAllplaying() const {
        // Player_getAllplaying @ 0x6CCE34: child Motion players can keep the
        // aggregate playing state true after the owner-level flag is clear.
        if(true) {
            for(const auto &node : _nodes) {
                if(auto *child = node.getChildPlayer()) {
                    if(child->getAllplaying()) {
                        const auto motionPath = matchedMotionPath();
                        if((detail::logoChainTraceEnabled() ||
                            detail::logoChainTraceEnabledForPath(motionPath)) &&
                           LOGGER) {
                            const auto path = motionPath.empty()
                                ? std::string("<none>") : motionPath;
                            LOGGER->info(
                                "PRTDIAG Player::getAllplaying this={} path='{}' value=1 reason=child nodeIndex={} localPlaying={}",
                                static_cast<const void *>(this),
                                path, node.index,
                                _allplaying ? 1 : 0);
                        }
                        return true;
                    }
                }
            }
        }
        const auto motionPath = matchedMotionPath();
        if((detail::logoChainTraceEnabled() ||
            detail::logoChainTraceEnabledForPath(motionPath)) && LOGGER) {
            const auto path = motionPath.empty()
                ? std::string("<none>") : motionPath;
            LOGGER->info(
                "PRTDIAG Player::getAllplaying this={} path='{}' value={} reason=local",
                static_cast<const void *>(this), path,
                _allplaying ? 1 : 0);
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
    // Port stores deg in root.delta.angle and routes the direct-edit branch
    // through the same Player_initEmoteMotion boundary as the binary.
    void Player::setAngleDeg(double deg) {
        if(_directEdit) {
            while(deg < 0.0) deg += 360.0;
            while(deg >= 360.0) deg -= 360.0;
            _emoteAngle = deg;
            initEmoteMotionLike_0x6B2E90(2u);
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
    // setAngleDeg above. Keep the body explicit because the binary has this
    // complete branch structure at 0x6CD0EC rather than a setter tail-call.
    void Player::setAngleRad(double rad) {
        double deg = rad * 57.2957795;
        if(_directEdit) {
            while(deg < 0.0) deg += 360.0;
            while(deg >= 360.0) deg -= 360.0;
            _emoteAngle = deg;
            initEmoteMotionLike_0x6B2E90(2u);
        } else if(!_nodes.empty()) {
            if(_nodes[0].delta.angle != deg) {
                _nodes[0].delta.angle = deg;
                _nodes[0].delta.dirty = true;
            }
        }
    }

    // Player_ncb_registerMembers@0x6D7250..0x6D7290 binds the literal
    // "meshDivisionRatio" to accessors that directly read/write Player+1176
    // (0x6D9670/0x6D9674).  This is independent from the EmoteEngine ratios.
    double Player::getMeshDivisionRatio() const {
        return _meshDivisionRatio;
    }

    // Player_updateLayers@0x6BCF28..0x6BCF3C reaches the owning EmoteEngine
    // and reads its separate +1176 ratio while constructing node+2048.
    double Player::meshDivisionRatioDupLike_0x6BCF3C() const {
        return _engineBack ? _engineBack->_meshDivisionRatioDup : 1.0;
    }

    void Player::setMeshDivisionRatio(double v) {
        _meshDivisionRatio = v;
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

    // Aligned with libkrkr2.so Player_setChara @0x6C0E9C (NCB "chara" setter).
    //
    // Binary structure:
    //   if (*(this+968) /* live stealthChara string-value slot */) {
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
    //      (+976 motion, +984 stealthMotion) and the playing byte (+1099),
    //      i.e. a chara change invalidates the currently loaded motion so the
    //      next play()/update reloads the PSB against the new chara.
    //
    // chara value storage: the binary keeps refcounted TJS string-value object
    // pointers at +960/+968 and compares them through ttstr_c_str. Source-level
    // ttstr values are the platform-independent representation of those same
    // owners; the manual ARM64 AddRef/Release is an inlined ABI detail.
    //
    // The architecturally load-bearing piece that the previous plain
    // `_chara = v;` was missing is the chara-change -> motion-invalidation
    // side effect. Without it, a chara change that keeps the same motion key
    // would skip reload because the local same-motion guards in
    // findMotion (PlayerMotionLoad.cpp:24,30) key only on the motion name,
    // never on chara.
    //
    // +776 is an independent pending stealthChara string owner. The public
    // setters and child creation paths flush it through the slot-16 writer and
    // immediately release/clear it, matching 0x6C0EBC..0x6C0EE4 and
    // 0x6D94DC..0x6D9504.
    bool Player::setCharaSlotLike_0x6B29C0(const ttstr &value,
                                           bool stealthOnly) {
        // Player_setCharaOrKeySlot_dedup @0x6B29C0 chooses +968 for the
        // stealth-only path and +960 for the primary path. Pointer identity is
        // merely its first fast path; the following type/string comparison
        // makes a value-equality test the source-level operation.
        const ttstr &comparisonSlot = stealthOnly ? _stealthChara : _chara;
        if(comparisonSlot == value) {
            return false;
        }

        // Both branches write +968. The primary branch additionally writes
        // +960, preserving the two independent ttstr owners.
        _stealthChara = value;
        if(!stealthOnly) {
            _chara = value;
        }

        // 0x6B2AB0..0x6B2AD0: a real chara change releases both motion-name
        // slots and clears Player+1099. It does not clear +528/+1012; the next
        // play reload is forced because +976/+984 are now empty.
        _stealthMotion.Clear();
        _motionKey.Clear();
        _allplaying = false;
        return true;
    }

    void Player::setChara(ttstr v) {
        // Player_setChara @0x6C0E9C: primary slot write first, then flush the
        // independently owned pending stealth-chara slot at +776 through the
        // same helper's stealth-only branch.
        setCharaSlotLike_0x6B29C0(v, false);
        if(!_pendingStealthChara.IsEmpty()) {
            const ttstr pending = _pendingStealthChara;
            setCharaSlotLike_0x6B29C0(pending, true);
            _pendingStealthChara.Clear();
        }
    }

    void Player::setMotion(ttstr v) {
        // Player_setMotion @0x6C1B20 is a thin Player_play wrapper; it does
        // not maintain a second setter-specific load state machine.
        playMotionLike_0x6B2284(std::move(v), 0);
    }

    void Player::setStealthChara(ttstr v) {
        // Player_setStealthChara @0x6D94B0.
        if(!_stealthChara.IsEmpty()) {
            setCharaSlotLike_0x6B29C0(v, true);
            if(!_pendingStealthChara.IsEmpty()) {
                const ttstr pending = _pendingStealthChara;
                setCharaSlotLike_0x6B29C0(pending, true);
                _pendingStealthChara.Clear();
            }
            return;
        }
        _pendingStealthChara = std::move(v);
    }

    void Player::setStealthMotion(ttstr v) {
        // Player_setMotion_stealth @0x6D9584 is the PlayFlagStealth form of
        // Player_play @0x6B21E8, including the +768 pending owner.
        playMotionLike_0x6B2284(std::move(v), PlayFlagStealth);
    }

    // Aligned to libkrkr2.so 0x681CAC → 0x6B0F10:
    // motion setter calls objthis.onFindMotion({chara, motion}) to let
    // TJS participate in path resolution before loading the PSB.
    tjs_error Player::setMotionCompat(tTJSVariant *result, tjs_int numparams,
                                      tTJSVariant **param,
                                      iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) return TJS_E_INVALIDOBJECT;

        ttstr charaValue = self->_chara;
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
                    charaValue = ttstr(charaVal);
                }
                if(TJS_SUCCEEDED(resObj->PropGet(TJS_MEMBERMUSTEXIST,
                    TJS_W("motion"), nullptr, &motionVal, resObj))
                    && motionVal.Type() != tvtVoid) {
                    motionValue = ttstr(motionVal);
                }
            }
        }

        // Player_loadMotion @0x6B0F10 feeds the callback-adjusted pair back
        // into Player_playImpl. Route through the single play state machine so
        // +976/+984, pending flush and the AsCan gate cannot diverge.
        self->setChara(charaValue);
        self->playMotionLike_0x6B2284(motionValue, 0);

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
        if(hasMotionContent()) {
            return true;
        }
        return ensureMotionLoaded(_stealthChara, _motionKey);
    }

    bool Player::ensureMotionLoaded(const ttstr &chara,
                                    const ttstr &motion) {
        // Player_loadMotion @0x6B0F10 calls ResourceManager.findMotion with
        // Player+1012 and "motion/<chara>/<motion>". ResourceManager_findMotion
        // @0x6A9ED4 returns [raw motion dispatch, matched HashMap-A key]. Keep
        // the raw result as the authoritative lookup. Android has no parallel
        // loaded-state object: result[0]/result[1] are the two sole owners.
        iTJSDispatch2 *rm = _resourceManager.Type() == tvtObject
            ? _resourceManager.AsObjectNoAddRef()
            : nullptr;
        // Player_loadMotion @ 0x6B142C..0x6B1478 copies Player+1012 as a
        // complete Variant and calls findMotion even when it is void; the
        // ResourceManager body owns the direct-vs-fallback gate.
        if(rm != nullptr) {
            tTJSVariant project = _findMotionContextVariant;
            tTJSVariant path(detail::widen(
                "motion/" + detail::narrow(chara) + "/" +
                detail::narrow(motion)));
            tTJSVariant *args[] = { &project, &path };
            tTJSVariant result;
            static tjs_uint32 hint = 0;
            const tjs_error hr = rm->FuncCall(
                0, TJS_W("findMotion"), &hint, &result, 2, args, rm);
            if(TJS_FAILED(hr)) {
                return false;
            }
            if(result.Type() == tvtObject) {
                const auto motionValue =
                    detail::motionPropGetByNum(result, 0);
                const auto matchedKey =
                    detail::motionPropGetByNum(result, 1);
                // Player_playImpl @0x6B24F4: CopyRef result[0] -> Player+528,
                // then CopyRef result[1] -> Player+1012.
                _motionContentVariant = motionValue;
                _findMotionContextVariant = matchedKey;
                return true;
            }
        }

        return false;
    }

    void Player::initEmoteMotionLike_0x6B2E90(
        std::uint32_t playFlags) {
        // 0x6B2ED0..0x6B2F00: normalize cameraAngle(+472) plus the retained
        // direct-edit angle(+464) with the binary's two explicit loops.
        double angle = _cameraAngle + _emoteAngle;
        while(angle < 0.0) {
            angle += 360.0;
        }
        while(angle >= 360.0) {
            angle -= 360.0;
        }

        // 0x6B2F14..0x6B2FD0: locate the first adjacent division pair whose
        // interval is (previous,current], then wrap the terminal index by the
        // raw count. The binary has no count==0 guard.
        const tjs_int divisionCount =
            detail::motionPropGetCount(_emoteDivisionVariant);
        tjs_int divisionIndex = 1;
        if(divisionCount >= 2) {
            do {
                if(detail::motionPropGetDoubleByNum(
                       _emoteDivisionVariant, divisionIndex - 1) < angle &&
                   detail::motionPropGetDoubleByNum(
                       _emoteDivisionVariant, divisionIndex) >= angle) {
                    break;
                }
                ++divisionIndex;
            } while(divisionIndex < divisionCount);
        }
        const tjs_int selected = divisionIndex % divisionCount;
        if(selected == _emoteMotionIndex) {
            return;
        }
        _emoteMotionIndex = selected;

        // 0x6B2FE8..0x6B30A8: motionList[selected] is converted to ttstr,
        // split by the common sub_697D34 helper, and element 2 is consumed
        // without a size check.
        const ttstr path(detail::motionPropGetByNum(
            _emoteMotionListVariant, selected));
        const std::vector<ttstr> parts =
            detail::splitTtstrLike_0x697D34(path, TJS_W('/'));
        const ttstr secondaryMotion = parts[2];

        // 0x6B30C0..0x6B3264: loadMotion receives the live stealthChara
        // (+968) and the selected third path component. A successful result
        // overwrites +528/+1012 and enters the ordinary non-emote initializer.
        if(ensureMotionLoaded(_stealthChara, secondaryMotion)) {
            initNonEmoteMotionLike_0x6B365C(playFlags);
        } else {
            // 0x6B333C/0x6B3344: failed secondary load clears both owners.
            _motionContentVariant.Clear();
            _findMotionContextVariant.Clear();
        }
    }

    void Player::initNonEmoteMotionLike_0x6B365C(std::uint32_t playFlags) {
        static std::uint32_t s_initDiagSeq = 0;
        const auto diagSeq = ++s_initDiagSeq;
        const bool emitDiag = shouldEmitCoreDiag(diagSeq);
        if(emitDiag && LOGGER) {
            LOGGER->info(
                "PRTDIAG Player::initNonEmoteMotion enter seq={} this={} flags=0x{:x} active={} isEmote={} motionKey='{}' chara='{}' activePath='{}' nodes={} allplaying={} queuing={} firstFrame={}",
                diagSeq, static_cast<const void *>(this), playFlags,
                hasMotionContent(), coreDiagBool(_preview),
                detail::narrow(_motionKey), detail::narrow(_chara),
                matchedMotionPath().empty()
                    ? std::string("<none>") : matchedMotionPath(),
                _nodes.size(),
                coreDiagBool(_allplaying), coreDiagBool(_queuing),
                coreDiagBool(_firstFrame));
        }
        resetNodeTreeForBuildLike_0x6B56F8();
        _parameterEntries.clear();
        _defaultParameterEntry = {};
        _defaultParameterEntry.rangeScale = 1.0;
        _defaultParameterEntry.mode = 0;

        // Player_initNonEmoteMotion @0x6B3698..0x6B398C keeps one dispatch
        // holder rooted in Player+528 and performs every PSB read through it.
        // There is no decoded snapshot/PSBDictionary fallback.
        _loopTime = detail::motionPropGetDouble(
            _motionContentVariant, TJS_W("loopTime"));
        _cachedTotalFrames = detail::motionPropGetDouble(
            _motionContentVariant, TJS_W("lastTime"));
        _tagFrameSourceVariant = detail::motionPropGet(
            _motionContentVariant, TJS_W("tag"));
        _priorityFrameSourceVariant = detail::motionPropGet(
            _motionContentVariant, TJS_W("priority"));
        const tTJSVariant firstPriority = detail::motionPropGetByNum(
            _priorityFrameSourceVariant, 0);
        _rootContentVariant = detail::motionPropGet(
            firstPriority, TJS_W("content"));

        const tTJSVariant parameterize = detail::motionPropGet(
            _motionContentVariant, TJS_W("parameterize"));
        if(parameterize.Type() == tvtObject) {
            appendParameterEntryLike_0x6B1718(parameterize);
            finalizeParameterTableLike_0x6B1ECC();
            // 0x6B39B0..0x6B3A70 writes +376 only when begin != end. Preserve
            // the prior pointer on the empty-object boundary.
            if(!_parameterEntries.empty()) {
                _defaultParameterEntryIndex = 0;
                _defaultParameterEntryPtr = &_parameterEntries.front();
            }
        } else {
            const tTJSVariant parameters = detail::motionPropGet(
                _motionContentVariant, TJS_W("parameter"));
            (void)parseParameterListLike_0x6B202C(parameters);
            if(parameterize.Type() == tvtInteger) {
                const tjs_int index = parameterize.AsInteger();
                if(index < 0 || static_cast<size_t>(index) >=
                                    _parameterEntries.size()) {
                    throw std::out_of_range("parameter id out of range.");
                }
                _defaultParameterEntryIndex = index;
                _defaultParameterEntryPtr =
                    &_parameterEntries[static_cast<size_t>(index)];
            } else {
                _defaultParameterEntryIndex = -1;
                _defaultParameterEntryPtr = nullptr;
            }
        }

        if(emitDiag && LOGGER) {
            LOGGER->info(
                "PRTDIAG Player::initNonEmoteMotion before-build seq={} this={} defaultParamIndex={} paramEntries={} loopTime={} totalFrames={}",
                diagSeq, static_cast<const void *>(this),
                _defaultParameterEntryIndex, _parameterEntries.size(),
                _loopTime, _cachedTotalFrames);
        }
        buildNodeTree();
        if(emitDiag && LOGGER) {
            LOGGER->info(
                "PRTDIAG Player::initNonEmoteMotion after-build seq={} this={} nodeCount={} labelMap={}",
                diagSeq, static_cast<const void *>(this), _nodes.size(),
                _nodeLabelMap.size());
        }
        initVariables();

        // Player_initNonEmoteMotion @0x6B3A8C: TBNZ playFlags&2(Chain) branch.
        //   non-chain (0x6B3A90..0x6B3AAC): +1120=0; +456=min(+1128,0);
        //     STRH 0x0101 @0x6B3AAC -> +480(_queuing)=1 AND +481(_firstFrame)=1.
        //   chain    (0x6B3AB4..0x6B3AC0): STRB 1 @0x6B3AC0 -> +481(_firstFrame)=1 only.
        // Port previously set ONLY _queuing in the non-chain branch and nothing in
        // the chain branch — _firstFrame(+481) was never seeded here, so the
        // progress_inner firstFrame block (0x6C1104, gated on +481) never ran on the
        // play() path. Restored to the exact STRH 0x0101 / STRB 1 writes.
        if((playFlags & PlayFlagChain) == 0) {
            _frameTickCount = 0.0;                              // +1120 = 0 (0x6B3AA4)
            _clampedEvalTime = std::min(_cachedTotalFrames, 0.0); // +456 (0x6B3AA8)
            _queuing = true;                                   // +480 = 1 (0x6B3AAC STRH lo)
            _firstFrame = true;                                // +481 = 1 (0x6B3AAC STRH hi)
        } else {
            _firstFrame = true;                                // +481 = 1 (0x6B3AC0 STRB)
        }
        // R2: binary Player_initNonEmoteMotion @ 0x6B3A78 writes STRH 0x100
        // unconditionally (no chain branch). Previously port set _allplaying
        // twice (inside chain-skip branch + unconditional below); R2 spike
        // confirmed only one write is needed. Single unconditional write.
        _allplaying = true;
        if(emitDiag && LOGGER) {
            LOGGER->info(
                "PRTDIAG Player::initNonEmoteMotion exit seq={} this={} nodeCount={} variables={} loopTime={} totalFrames={} clampedEvalTime={} allplaying={} queuing={} firstFrame={}",
                diagSeq, static_cast<const void *>(this), _nodes.size(),
                _variableLabelScopes.size(), _loopTime, _cachedTotalFrames,
                _clampedEvalTime, coreDiagBool(_allplaying),
                coreDiagBool(_queuing), coreDiagBool(_firstFrame));
        }
    }

    tTJSVariant Player::getVariableKeys() {
        // Player_getVariableKeys @0x6D139C creates the Array first through
        // sub_704CB8, then writes String Variants straight into the native
        // tTJSArrayNI::Items deque. There is no intermediate vector or TJS
        // `add` dispatch.
        auto result = detail::createTJSArrayWithItems_guess();
        for(const auto &scope : _variableLabelScopes) {
            result.items->emplace_back(scope.cascadeKey);
        }
        return result.value;
    }

    // transformOrder getter: libkrkr2.so sub_6CC188 (bound to L"transformOrder")
    // builds a TJS Array of the 4 ints at node+84..96 (type-4 int variants), in
    // order, by writing tTJSArrayNI::Items directly after sub_704CB8.
    tTJSVariant Player::getTransformOrder() const {
        auto result = detail::createTJSArrayWithItems_guess();
        for(const int value : _transformOrder) {
            result.items->emplace_back(static_cast<tjs_int>(value));
        }
        return result.value;
    }

    // transformOrder setter: libkrkr2.so sub_6CC2C4 reads 4 elements [0..3] of
    // the assigned Array, coerces each to an int, rejects any value >3 or any
    // duplicate with TVPThrowExceptionMessage(L"illegul variable for transform
    // order") (binary sub_95440C, typo preserved), then stores to node+84..96.
    void Player::setTransformOrder(tTJSVariant arr) {
        iTJSDispatch2 *a =
            arr.Type() == tvtObject ? arr.AsObjectNoAddRef() : nullptr;
        if(!a)
            return;
        int order[4];
        bool used[4] = {false, false, false, false};
        for(int i = 0; i < 4; i++) {
            tTJSVariant elem;
            if(TJS_FAILED(a->PropGetByNum(0, i, &elem, a)))
                return;
            const int v = static_cast<int>((tjs_int)elem);
            if(v < 0 || v > 3 || used[v])
                TVPThrowExceptionMessage(
                    TJS_W("illegul variable for transform order"));
            used[v] = true;
            order[i] = v;
        }
        for(int i = 0; i < 4; i++)
            _transformOrder[i] = order[i];
    }

    // M16 (92-set alignment): class-level (process-global) state backing the
    // binary defaultSyncActive / defaultTransformOrder properties. Defaults are
    // taken verbatim from the libkrkr2.so module globals:
    //   byte_1AB84A8 == 0xff  -> s_defaultSyncActive = true
    //   dword_1AA40D8..E4 == {0,3,2,1} -> s_defaultTransformOrder
    bool Player::s_defaultSyncActive = true;
    int Player::s_defaultTransformOrder[4] = {0, 3, 2, 1};

    // defaultTransformOrder getter — aligned with libkrkr2.so sub_6B097C
    // @0x6B097C. The binary builds a 4-element TJS Array, pushing each
    // dword_1AA40D8[i] (i=0..3) as an int variant (type=4) directly into the
    // native Items deque returned alongside the Array by sub_704CB8.
    tTJSVariant Player::getDefaultTransformOrder() const {
        auto result = detail::createTJSArrayWithItems_guess();
        for(const int value : s_defaultTransformOrder) {
            result.items->emplace_back(static_cast<tjs_int>(value));
        }
        return result.value;
    }

    // defaultTransformOrder setter — aligned with libkrkr2.so sub_6B0AB4
    // @0x6B0AB4. The binary fetches 4 elements via PropGet(flag=1024, index
    // 0..3); a PropGet failure (high bit set) throws L"illegul size of transform
    // order"; each value is coerced to int and must be a unique value in [0,3]
    // (tracked via a per-value used flag), else throws L"illegul variable for
    // transform order" (binary sub_95440C, typos preserved). Values are written
    // in order to dword_1AA40D8 / DC / E0 / E4.
    void Player::setDefaultTransformOrder(tTJSVariant arr) {
        iTJSDispatch2 *a =
            arr.Type() == tvtObject ? arr.AsObjectNoAddRef() : nullptr;
        if(!a)
            TVPThrowExceptionMessage(
                TJS_W("illegul size of transform order"));
        // Binary writes each global immediately inside the loop (interleaved
        // with the PropGet/validate of the next index), so a mid-loop throw
        // leaves the earlier indices already written. Mirror that incremental
        // write rather than deferring, to match the partial-write-on-error
        // behavior of sub_6B0AB4.
        bool used[4] = {false, false, false, false};
        for(int i = 0; i < 4; i++) {
            tTJSVariant elem;
            // PropGetByNum(flags=TJS_MEMBERMUSTEXIST(0x400=1024), num=i) —
            // binary (*(vtbl+40))(obj, 1024, i, &elem, obj). The must-exist flag
            // is load-bearing: it makes a too-short array fail here so the
            // L"illegul size of transform order" throw fires (flag 0 would let a
            // missing index succeed-with-void and skip the error path).
            if(TJS_FAILED(a->PropGetByNum(TJS_MEMBERMUSTEXIST, i, &elem, a)))
                TVPThrowExceptionMessage(
                    TJS_W("illegul size of transform order"));
            const int v = static_cast<int>((tjs_int)elem);
            if((unsigned)v > 3 || used[v])
                TVPThrowExceptionMessage(
                    TJS_W("illegul variable for transform order"));
            // Binary: dword_1AA40D8[i] = v; *used_flag = 1; (in that order).
            s_defaultTransformOrder[i] = v;
            used[v] = true;
        }
    }

    // --- Core methods ---
    // Aligned to libkrkr2.so sub_6BA7B8 at 0x6BA7B8:
    // 1. copy Player's canonical ResourceManager dispatch;
    // 2. FuncCall(obj, 0, L"random", ...) on that ResourceManager;
    // 3. convert the result variant to double. ResourceManager_ctor @0x6A88CC
    //    owns the actual Math.RandomGenerator.
    double Player::random() {
        if (_resourceManager.Type() == tvtObject) {
            iTJSDispatch2 *obj = _resourceManager.AsObjectNoAddRef();
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
        // compare/write only root delta.flipX and mark that delta dirty.
        if(_rootFlipX == mirror) {
            return;
        }

        _rootFlipX = mirror;
        if(!_nodes.empty()) {
            _nodes.front().delta.flipX = mirror;
            _nodes.front().delta.dirty = true;
        }
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
    // Player_setOuterForce (0x672D58): CASE-SENSITIVE label dispatch (binary
    // uses sub_9B1ED0 = plain wcscmp, NOT a case-folding compare) for "bust",
    // "hair", and "parts", routing to the bust/hair/parts sinks (binary
    // targets a1+1104/+1112/+1120) and carrying transition/ease through.
    // FIX 2026-06-04: label was "h" (wrong — binary is L"hair" @0x672dca) and
    // compare was lowerAscii (wrong — binary wcscmp is case-sensitive).
    void Player::setOuterForce(ttstr label, double x, double y,
                               double transition, double ease) {
        const auto key = detail::narrow(label);
        OuterForceState *target = nullptr;
        if(key == "bust") {
            target = &_bustOuterForce;
        } else if(key == "hair") {
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

    void Player::modifyRoot() {
        // Player_modifyRoot @0x6CD0B0:
        //   *(_BYTE *)(*(_QWORD *)(player + 200) + 1584) = 1;
        if(!_nodes.empty()) {
            _nodes[0].delta.dirty = true;
        }
    }

} // namespace motion
