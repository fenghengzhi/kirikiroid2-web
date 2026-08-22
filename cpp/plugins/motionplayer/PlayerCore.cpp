// PlayerCore.cpp — Constructor, setMotion, core properties
// Split from Player.cpp for maintainability.
//
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <iterator>

#include "PlayerInternal.h"
#include "EmotePlayer.h" // for EmoteEngine (back-pointer deref)
#include "MotionDispatch.h"
#include "SourceCache.h"
#include "DebugIntf.h"
#include "ncbind.hpp"

using namespace motion::internal;

namespace {
    struct DispatchReleaseGuard_guess {
        iTJSDispatch2 *dispatch = nullptr;

        ~DispatchReleaseGuard_guess() {
            if(dispatch) {
                dispatch->Release();
            }
        }
    };

    std::string lowerAscii(std::string value) {
        for(char &ch : value) {
            ch = static_cast<char>(
                std::tolower(static_cast<unsigned char>(ch)));
        }
        return value;
    }

    std::uint32_t swapPackedRedBlue_guess(std::uint32_t packedColor) {
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

    // Exact binary64 constants shared by all four current reference images.
    constexpr double kDegreesToRadians = 0x1.1df46a2529d39p-6;
    constexpr double kRadiansToDegrees = 0x1.ca5dc1a63c1f8p+5;

    bool boundsScalarIsValid_guess(double value) {
        // The native classifier accepts only non-negative finite binary64
        // values. In particular, negative finite values and -0.0 are rejected
        // alongside both infinities and every NaN payload.
        return std::isfinite(value) && !std::signbit(value);
    }

}

namespace motion::internal {

    double initialNonChainEvaluationTime_guess(double totalFrames) {
        return std::min(0.0, totalFrames);
    }

}

namespace motion {

    tjs_int Player::getColorWeight() const {
        return static_cast<tjs_int>(
            swapPackedRedBlue_guess(_colorWeightPacked));
    }

    void Player::setColorWeight(tjs_int v) {
        _colorWeightPacked = swapPackedRedBlue_guess(
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

        // All four public setters compare the typed Boolean with the retained
        // flag and dirty every node delta on mismatch, but never store the
        // flag. Preserve that observable boundary; type-3 construction has a
        // separate compare/dirty/store path.
        for(auto &node : _nodes) {
            node.delta.dirty = true;
        }
    }

    // The four-reference Player constructor takes one RM dispatch. It arrives
    // from the EmoteObject owner and flows through EmoteEngine; child Players
    // inherit another CopyRef of the canonical owner. The same dispatch is
    // retained in three independent Variants. Between the second and third RM
    // owners are the persistent source descriptor Dictionary, two initially
    // Void internal Layer slots, and the persistent color Dictionary. Player
    // does not create or own a native RM by value. Root and parent links are
    // assigned after child construction.
    Player::Player(const tTJSVariant &rmDispatch) :
        _rootPlayer(this),
        _findSourceResourceManager(rmDispatch),
        _sourceCacheObject(rmDispatch),
        _resourceManager(rmDispatch) {
        LOGGER->info("Motion.Player constructor called");
        LOGGER->info("PRTDIAG Player::ctor this={} rmType={}",
                     static_cast<const void *>(this),
                     static_cast<int>(_resourceManager.Type()));
        // All four current constructors copy the same ResourceManager dispatch
        // into three independent owners. ResourceManager construction includes
        // its SourceCache base; render-side native helpers unwrap that stable
        // owner on demand instead of adding a cached Player pointer. The final
        // native Player pointer slot belongs to an uninitialized Android-only
        // load residual and is not a SourceCache/ResourceManager back-pointer.
        // All four current constructors create the persistent render
        // descriptor/color objects, then assign descriptor.color = colors.
        // Render-source callers mutate these same objects before entering the
        // Player-owned resolver. The resolver either uses its internal-Layer
        // fast path or dispatches ResourceManager.loadSource as fallback.
        iTJSDispatch2 *descriptor = TJSCreateDictionaryObject();
        DispatchReleaseGuard_guess descriptorGuard{descriptor};
        _sourceDescriptor = tTJSVariant(descriptor, descriptor);

        iTJSDispatch2 *colors = TJSCreateDictionaryObject();
        DispatchReleaseGuard_guess colorsGuard{colors};
        _sourceColors = tTJSVariant(colors, colors);

        descriptor = _sourceDescriptor.AsObjectNoAddRef();
        (void)descriptor->PropSet(
            TJS_MEMBERENSURE, TJS_W("color"),
            &detail::colorMemberHint_guess, &_sourceColors, descriptor);

        // Only after descriptor.color succeeds do all four constructors append
        // the sole synthetic root. MotionNode's ordinary value-constructor
        // leaves transformOrder zeroed; only this root receives the process-
        // wide class default. Keeping this after the Dictionary publications
        // also preserves their constructor-unwind frontier if root allocation
        // throws.
        detail::ensureRootNode_guess(*this);
        std::copy(std::begin(s_defaultTransformOrder),
                  std::end(s_defaultTransformOrder),
                  std::begin(_nodes.front().transformOrder));

        // The creation-return owners stay live through descriptor.color and
        // root setup, then release in colors -> descriptor order. The guards
        // preserve the same order when Dictionary assignment, PropSet or root
        // construction unwinds.
        // The random generator belongs to ResourceManager in all four current
        // layouts; Player does not create or own another generator.
    }

    // Dispatch-to-native unpack for the findSource owner. The native is owned
    // by the ResourceManager adaptor dispatch, so the non-owning fast pointer
    // remains valid while any of Player's three persistent Variant owners is
    // alive. Invalid or Void input produces no native fast pointer.
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
        // All four current destructors purge references from self/ancestor ramp
        // maps while the pointed-to parameter entries are still alive.
        purgeParameterRampMap_guess();

        // The native destructor then explicitly destroys/empties this vector
        // before node-tree teardown. This order matters because child teardown
        // and ordinary member destruction occur in later lifetime phases.
        _parameterEntries.clear();

        // Destruction reuses the complete old-tree teardown, including the
        // explicit child-object invalidation pre-pass. The synthetic root is
        // destroyed later by the deque member destructor.
        resetAndReleaseOldNodeTree_guess();

        // Keep the raw-owner ordering: destroy/free first and clear the member
        // slot only afterward. This is part of Player's explicit destructor
        // body, before automatic member destruction begins.
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
        // The script-visible `playing` property is only this Player's local
        // playback byte. It does not inspect retained child Players.
        if(detail::logoChainTraceEnabled() && LOGGER) {
            const auto motionPath = matchedMotionPath();
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
        // The aggregate property skips the synthetic root, then recursively
        // scans only nested-motion nodes. Particle children (type 4) are not
        // part of this query. The source-shaped loop tests nodes.size(); three
        // targets visibly reload it after child recursion, while iOS armv7
        // hoists the invariant bound because this const traversal has no
        // supported mutation path.
        for(std::size_t nodeIndex = 1; nodeIndex < _nodes.size(); ++nodeIndex) {
            const auto &node = _nodes[nodeIndex];
            if(node.nodeType != 3) {
                continue;
            }

            // Invalid non-object Variants throw during conversion. A null or
            // wrong-native object resolves to nullptr, and the references
            // still recurse without a null guard; callers therefore retain
            // the same malformed-tree crash boundary.
            Player *const child = node.getChildPlayer();
            if(child->getAllplaying()) {
                if(detail::logoChainTraceEnabled() && LOGGER) {
                    const auto motionPath = matchedMotionPath();
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
        if(detail::logoChainTraceEnabled() && LOGGER) {
            const auto motionPath = matchedMotionPath();
            const auto path = motionPath.empty()
                ? std::string("<none>") : motionPath;
            LOGGER->info(
                "PRTDIAG Player::getAllplaying this={} path='{}' value={} reason=local",
                static_cast<const void *>(this), path,
                _allplaying ? 1 : 0);
        }
        return _allplaying;
    }

    // All four references address the constructor-created root directly. The
    // setter writes and dirties only when IEEE-754 `!=` reports a change.
    double Player::getX() const {
        return _nodes[0].delta.posX;
    }
    void Player::setX(double v) {
        auto &root = _nodes[0];
        if (root.delta.posX != v) {
            root.delta.posX = v;
            root.delta.dirty = true;
        }
    }
    // Direct-edit selects the Player-resident degree value; ordinary mode
    // selects the constructor-owned root node directly. All four references
    // use the same selection for both public angle getters.
    double Player::getAngleDeg() const {
        return _directEdit ? _emoteAngle : _nodes[0].delta.angle;
    }

    double Player::getAngleRad() const {
        return getAngleDeg() * kDegreesToRadians;
    }

    // The y pair has the same direct-root and dirty-on-change behavior.
    double Player::getY() const {
        return _nodes[0].delta.posY;
    }
    void Player::setY(double v) {
        auto &root = _nodes[0];
        if (root.delta.posY != v) {
            root.delta.posY = v;
            root.delta.dirty = true;
        }
    }

    // Direct-edit always normalizes, stores, and reselects the emote motion.
    // Ordinary mode uses IEEE-754 `!=` and the native dirty-before-angle order.
    void Player::setAngleDeg(double deg) {
        if(_directEdit) {
            while(deg < 0.0) deg += 360.0;
            while(deg >= 360.0) deg -= 360.0;
            _emoteAngle = deg;
            initEmoteMotion_guess(2u);
        } else {
            auto &root = _nodes[0];
            if(root.delta.angle != deg) {
                root.delta.dirty = true;
                root.delta.angle = deg;
            }
        }
    }

    // Three targets preserve this shared call as a tail branch; Android ARM64
    // inlines the same degree-setter body.
    void Player::setAngleRad(double rad) {
        setAngleDeg(rad * kRadiansToDegrees);
    }

    // Player, EmotePlayer and D3DEmotePlayer all expose this same raw Player
    // scalar. The generated accessors perform only a double load/store.
    double Player::getMeshDivisionRatio() const {
        return _meshDivisionRatio;
    }

    void Player::setMeshDivisionRatio(double v) {
        _meshDivisionRatio = v;
    }

    // The accessor directly adopts the fresh Dictionary factory reference.
    // An unordered AABB emits only isValid=false. An ordered AABB emits its six
    // Real-valued geometry members first, then the classifier result as a typed
    // Boolean. The returned two-pointer closure is built while the accessor is
    // still alive, after which the accessor releases the factory reference.
    tTJSVariant Player::getBounds() const {
        ncbPropAccessor result(TJSCreateDictionaryObject(), false);
        // All four references perform the Y ordering test before X.
        if(_boundsMaxY >= _boundsMinY &&
           _boundsMaxX >= _boundsMinX) {
            (void)result.SetValue(
                TJS_W("left"), _boundsMinX, TJS_MEMBERENSURE,
                &detail::leftMemberHint_guess);
            (void)result.SetValue(
                TJS_W("top"), _boundsMinY, TJS_MEMBERENSURE,
                &detail::topMemberHint_guess);
            (void)result.SetValue(
                TJS_W("right"), _boundsMaxX, TJS_MEMBERENSURE,
                &detail::rightMemberHint_guess);
            (void)result.SetValue(
                TJS_W("bottom"), _boundsMaxY, TJS_MEMBERENSURE,
                &detail::bottomMemberHint_guess);
            (void)result.SetValue(
                TJS_W("width"), _boundsMaxX - _boundsMinX,
                TJS_MEMBERENSURE, &detail::widthMemberHint_guess);
            (void)result.SetValue(
                TJS_W("height"), _boundsMaxY - _boundsMinY,
                TJS_MEMBERENSURE, &detail::heightMemberHint_guess);

            const bool valid =
                boundsScalarIsValid_guess(_boundsMinX) &&
                boundsScalarIsValid_guess(_boundsMaxX) &&
                boundsScalarIsValid_guess(_boundsMinY) &&
                boundsScalarIsValid_guess(_boundsMaxY);
            (void)result.SetValue(
                TJS_W("isValid"), valid, TJS_MEMBERENSURE,
                &detail::isValidMemberHint_guess);
        } else {
            (void)result.SetValue(
                TJS_W("isValid"), false, TJS_MEMBERENSURE,
                &detail::isValidMemberHint_guess);
        }
        iTJSDispatch2 *const dispatch = result.GetDispatch();
        return tTJSVariant(dispatch, dispatch);
    }

    // All four references use one combined comparison. Once either axis differs
    // they write both doubles unconditionally, then set the dirty byte.
    void Player::setCoord(double x, double y) {
        auto &root = _nodes[0];
        if (root.delta.posX != x || root.delta.posY != y) {
            root.delta.posX = x;
            root.delta.posY = y;
            root.delta.dirty = true;
        }
    }

    // Four-reference inner live-slot writer. Equality is tested by string
    // value, not only owner identity. A real chara change writes the live
    // stealth chara (and primary chara for the non-stealth path), clears both
    // live motion labels, and clears playing.
    void Player::setCharaLiveSlots_guess(tjs_int flags,
                                         const ttstr &value) {
        // Pointer identity is merely a native fast path; the subsequent string
        // comparison makes value equality the source-level operation.
        const bool stealth = (flags & PlayFlagStealth) != 0;
        const ttstr &comparisonSlot = stealth ? _stealthChara : _chara;
        if(comparisonSlot == value) {
            return;
        }

        // Both branches write the live stealth slot. The primary branch also
        // writes the independent primary slot.
        _stealthChara = value;
        if(!stealth) {
            _chara = value;
        }

        // A real chara change does not clear the loaded content/context pair;
        // emptying both labels is enough to force the next play to reload.
        _stealthMotion.Clear();
        _motionKey.Clear();
        _allplaying = false;
    }

    // Four-reference outer coordinator shared by the primary and Stealth
    // typed-property setters. The pending field is passed to the inner writer
    // in place; its owner is released and nulled only after that call returns.
    void Player::setCharaWithFlags_guess(tjs_int flags,
                                         const ttstr &value) {
        const bool stealth = (flags & PlayFlagStealth) != 0;
        if(stealth && _stealthChara.IsEmpty()) {
            _pendingStealthChara = value;
            return;
        }

        setCharaLiveSlots_guess(flags, value);
        if(!_pendingStealthChara.IsEmpty()) {
            setCharaLiveSlots_guess(
                PlayFlagStealth, _pendingStealthChara);
            _pendingStealthChara.Clear();
        }
    }

    void Player::setChara(ttstr v) {
        setCharaWithFlags_guess(0, v);
    }

    void Player::setMotion(ttstr v) {
        // All four current references route the by-value setter-local label
        // through the same borrowed-label play entry. There is no second
        // setter-specific load state machine.
        playMotion_guess(0, v);
    }

    void Player::setStealthChara(ttstr v) {
        setCharaWithFlags_guess(PlayFlagStealth, v);
    }

    void Player::setStealthMotion(ttstr v) {
        // The stealth setter uses the same borrowed-label play entry with the
        // Stealth flag, including its persistent pending-owner path.
        playMotion_guess(PlayFlagStealth, v);
    }

    tTJSVariant Player::loadMotionResult_guess(
        ttstr chara, ttstr motion) {
        // The hidden-sret Variant is initialized once and then reused by both
        // calls. In particular, findMotion failure/no-write leaves a preceding
        // onFindMotion object in this same slot.
        tTJSVariant result;

        // All four load helpers route their by-value lookup pair through the
        // current NCB dispatch when one exists. The callback-adjusted handles
        // select the resource only; playImpl still commits the caller's label.
        if(_currentDispatch != nullptr) {
            iTJSDispatch2 *requestObject = TJSCreateDictionaryObject();
            DispatchReleaseGuard_guess requestOwner{requestObject};
            {
                tTJSVariant value(chara);
                (void)requestObject->PropSet(
                    TJS_MEMBERENSURE, TJS_W("chara"),
                    &detail::requestCharaMemberHint_guess, &value,
                    requestObject);
            }
            {
                tTJSVariant value(motion);
                (void)requestObject->PropSet(
                    TJS_MEMBERENSURE, TJS_W("motion"),
                    &detail::motionMemberHint_guess, &value, requestObject);
            }
            _currentDispatch->AddRef();
            DispatchReleaseGuard_guess currentDispatch{
                _currentDispatch};
            tTJSVariant request(requestObject, requestObject);
            tTJSVariant *callbackArguments[] = {&request};
            (void)currentDispatch.dispatch->FuncCall(
                0, TJS_W("onFindMotion"),
                &detail::onFindMotionMemberHint_guess, &result,
                1, callbackArguments, currentDispatch.dispatch);
            // The callback argument closure is destroyed immediately. A raw
            // Dictionary owner remains until the callback-result properties
            // have been consumed, even if the callee replaced param[0].
            request.Clear();

            // Force the callback result through the owning Object conversion,
            // then release the temporary Variant owner. Property status alone
            // chooses the empty-string default; a failed getter's output is
            // never converted or retained as the adjusted label.
            tTJSVariant responseCopy(result);
            DispatchReleaseGuard_guess response{
                responseCopy.AsObject()};
            responseCopy.Clear();

            const auto adjustedString = [&](const tjs_char *member) {
                tTJSVariant value;
                const tjs_error status = response.dispatch->PropGet(
                    TJS_MEMBERMUSTEXIST, member, nullptr, &value,
                    response.dispatch);
                return TJS_FAILED(status) ? ttstr() : ttstr(value);
            };
            chara = adjustedString(TJS_W("chara"));
            motion = adjustedString(TJS_W("motion"));
        }

        // Copy the canonical ResourceManager Variant, force it to Object, and
        // retain the dispatch independently for the complete call. Context and
        // path are separate argument Variants; neither aliases Player storage.
        tTJSVariant resourceManagerCopy(_resourceManager);
        DispatchReleaseGuard_guess resourceManager{
            resourceManagerCopy.AsObject()};
        resourceManagerCopy.Clear();

        tTJSVariant path(
            TJS_W("motion/") + chara + TJS_W("/") + motion);
        tTJSVariant projectArgument(_findMotionContextVariant);
        tTJSVariant pathArgument(path);
        tTJSVariant *arguments[] = {&projectArgument, &pathArgument};
        (void)resourceManager.dispatch->FuncCall(
            0, TJS_W("findMotion"), &detail::findMotionMemberHint_guess,
            &result, 2, arguments, resourceManager.dispatch);
        return result;
    }

    tTJSVariant Player::tailDispatchLoadMotionResidual_guess(
        ttstr chara, ttstr motion) {
        // Android retains this source-shaped companion with no code/data
        // caller. It borrows the final raw dispatch directly: there is no
        // AddRef guard and the Player constructor deliberately never seeds the
        // slot. Keep it separate from the live root-currentDispatch path.
        if(_tailDispatchLoadMotionResidual_guess != nullptr) {
            iTJSDispatch2 *requestObject = TJSCreateDictionaryObject();
            DispatchReleaseGuard_guess requestOwner{requestObject};
            {
                tTJSVariant value(chara);
                (void)requestObject->PropSet(
                    TJS_MEMBERENSURE, TJS_W("chara"),
                    &detail::requestCharaMemberHint_guess, &value,
                    requestObject);
            }
            {
                tTJSVariant value(motion);
                (void)requestObject->PropSet(
                    TJS_MEMBERENSURE, TJS_W("motion"),
                    &detail::motionMemberHint_guess, &value, requestObject);
            }

            tTJSVariant callbackResult;
            tTJSVariant request(requestObject, requestObject);
            tTJSVariant *callbackArguments[] = {&request};
            (void)_tailDispatchLoadMotionResidual_guess->FuncCall(
                0, TJS_W("onFindMotion"),
                &detail::onFindMotionMemberHint_guess, &callbackResult,
                1, callbackArguments,
                _tailDispatchLoadMotionResidual_guess);
            request.Clear();

            tTJSVariant responseCopy(callbackResult);
            DispatchReleaseGuard_guess response{responseCopy.AsObject()};
            responseCopy.Clear();
            const auto adjustedString = [&](const tjs_char *member) {
                tTJSVariant value;
                const tjs_error status = response.dispatch->PropGet(
                    TJS_MEMBERMUSTEXIST, member, nullptr, &value,
                    response.dispatch);
                return TJS_FAILED(status) ? ttstr() : ttstr(value);
            };
            chara = adjustedString(TJS_W("chara"));
            motion = adjustedString(TJS_W("motion"));
        }

        // The residual creates/clears its actual return slot only after the
        // callback phase, so a failed/no-write findMotion cannot leak the
        // callback response through as the return value.
        tTJSVariant result;
        tTJSVariant resourceManagerCopy(_resourceManager);
        DispatchReleaseGuard_guess resourceManager{
            resourceManagerCopy.AsObject()};
        resourceManagerCopy.Clear();

        tTJSVariant path(
            TJS_W("motion/") + chara + TJS_W("/") + motion);
        tTJSVariant projectArgument(_findMotionContextVariant);
        tTJSVariant pathArgument(path);
        tTJSVariant *arguments[] = {&projectArgument, &pathArgument};
        (void)resourceManager.dispatch->FuncCall(
            0, TJS_W("findMotion"), &detail::findMotionMemberHint_guess,
            &result, 2, arguments, resourceManager.dispatch);
        return result;
    }

    void Player::initEmoteMotion_guess(std::uint32_t playFlags) {
        double angle = _cameraAngle + _emoteAngle;
        while(angle < 0.0) {
            angle += 360.0;
        }
        while(angle >= 360.0) {
            angle -= 360.0;
        }

        // Locate the first adjacent division pair whose interval is
        // (previous,current], then wrap the terminal index by the raw count.
        // The reference implementation has no count==0 guard.
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

        // motionList[selected] is converted to ttstr, split by the common
        // helper, and element 2 is consumed without a size check.
        const ttstr path(detail::motionPropGetByNum(
            _emoteMotionListVariant, selected));
        const std::vector<ttstr> parts =
            detail::splitTtstr_guess(path, TJS_W('/'));
        const ttstr secondaryMotion = parts[2];

        // Keep the complete load result alive through the ordinary initializer.
        // The native branch tests only Void versus non-Void; it does not add a
        // protective object-type check before indexing elements 0 and 1.
        const tTJSVariant loadResult =
            loadMotionResult_guess(_stealthChara, secondaryMotion);
        if(loadResult.Type() != tvtVoid) {
            _motionContentVariant =
                detail::motionPropGetByNum(loadResult, 0);
            _findMotionContextVariant =
                detail::motionPropGetByNum(loadResult, 1);
            initNonEmoteMotion_guess(playFlags);
        } else {
            TVPAddLog(TJS_W("motion not found ") + _stealthChara +
                      TJS_W("/") + secondaryMotion);
            _motionContentVariant.Clear();
            _findMotionContextVariant.Clear();
        }
    }

    void Player::initNonEmoteMotion_guess(std::uint32_t playFlags) {
        static std::uint32_t s_initDiagSeq = 0;
        std::uint32_t diagSeq = 0;
        bool emitDiag = false;
        if(detail::logoChainTraceEnabled() && LOGGER) {
            diagSeq = ++s_initDiagSeq;
            emitDiag = shouldEmitCoreDiag(diagSeq);
        }
        if(emitDiag && LOGGER) {
            const auto motionPath = matchedMotionPath();
            LOGGER->info(
                "PRTDIAG Player::initNonEmoteMotion enter seq={} this={} flags=0x{:x} active={} isEmote={} motionKey='{}' chara='{}' activePath='{}' nodes={} allplaying={} queuing={} firstFrame={}",
                diagSeq, static_cast<const void *>(this), playFlags,
                hasMotionContent(), coreDiagBool(_preview),
                detail::narrow(_motionKey), detail::narrow(_chara),
                motionPath.empty()
                    ? std::string("<none>") : motionPath,
                _nodes.size(),
                coreDiagBool(_allplaying), coreDiagBool(_queuing),
                coreDiagBool(_firstFrame));
        }
        // Keep the copied motion receiver alive across every property read,
        // parameter build, node build and variable initialization.
        ncbPropAccessor motionContent{tTJSVariant(_motionContentVariant)};
        _loopTime = motionContent.GetValue(
            TJS_W("loopTime"), ncbTypedefs::Tag<tjs_real>(), 0);
        _cachedTotalFrames = motionContent.GetValue(
            TJS_W("lastTime"), ncbTypedefs::Tag<tjs_real>(), 0);
        _tagFrameSourceVariant = motionContent.GetValue(
            TJS_W("tag"), ncbTypedefs::Tag<tTJSVariant>(), 0);
        _priorityFrameSourceVariant = motionContent.GetValue(
            TJS_W("priority"), ncbTypedefs::Tag<tTJSVariant>(), 0);

        // The priority accessor is a full-expression temporary. The root item
        // accessor outlives it and remains retained through the function tail.
        ncbPropAccessor rootItem{
            ncbPropAccessor(tTJSVariant(_priorityFrameSourceVariant)).GetValue(
                0, ncbTypedefs::Tag<tTJSVariant>(), 0)};
        _rootContentVariant = rootItem.GetValue(
            TJS_W("content"), ncbTypedefs::Tag<tTJSVariant>(), 0);

        // All four reference builds clear these two indexes only after the
        // five motion owners above have been acquired.  The old node deque and
        // its layer IDs remain live until buildNodeTree() enters its own reset
        // helper; that ordering is observable if a later property read or
        // parameter selection throws.
        _nodeLabelMap.clear();
        _parameterEntries.clear();

        const tTJSVariant parameterize = motionContent.GetValue(
            TJS_W("parameterize"), ncbTypedefs::Tag<tTJSVariant>(), 0,
            &detail::parameterizeMemberHint_guess);
        if(parameterize.Type() == tvtObject) {
            appendParameterEntry_guess(parameterize);
            finalizeParameterTable_guess();
            // The object branch writes the selected pointer only when append
            // actually produced an entry, preserving its previous value on the
            // empty/non-object boundary.
            if(!_parameterEntries.empty()) {
                _selectedParameterEntry = &_parameterEntries.front();
            }
        } else {
            const tTJSVariant parameters = motionContent.GetValue(
                TJS_W("parameter"), ncbTypedefs::Tag<tTJSVariant>(), 0,
                &detail::parameterMemberHint_guess);
            (void)parseParameterList_guess(parameters);
            if(parameterize.Type() == tvtInteger) {
                const tjs_int index = parameterize.AsInteger();
                if(index < 0 || static_cast<size_t>(index) >=
                                    _parameterEntries.size()) {
                    throw std::out_of_range("parameter id out of range.");
                }
                _selectedParameterEntry =
                    &_parameterEntries[static_cast<size_t>(index)];
            } else {
                _selectedParameterEntry = nullptr;
            }
        }

        if(emitDiag && LOGGER) {
            LOGGER->info(
                "PRTDIAG Player::initNonEmoteMotion before-build seq={} this={} selectedParam={} paramEntries={} loopTime={} totalFrames={}",
                diagSeq, static_cast<const void *>(this),
                static_cast<const void *>(_selectedParameterEntry),
                _parameterEntries.size(),
                _loopTime, _cachedTotalFrames);
        }

        // All four references commit this adjacent state pair before either
        // node-tree construction or variable initialization. Consequently a
        // later exception leaves sync waiting cleared and playback enabled.
        _syncWaiting = false;
        _allplaying = true;
        buildNodeTree_guess();
        if(emitDiag && LOGGER) {
            LOGGER->info(
                "PRTDIAG Player::initNonEmoteMotion after-build seq={} this={} nodeCount={} labelMap={}",
                diagSeq, static_cast<const void *>(this), _nodes.size(),
                _nodeLabelMap.size());
        }
        initVariables();

        // Non-chain play resets the frame clock, clamps the initial evaluation
        // time against zero, and marks the player queued. Chain play preserves
        // those three fields. Both paths seed the first-frame transition.
        if((playFlags & PlayFlagChain) == 0) {
            _frameTickCount = 0.0;
            _clampedEvalTime =
                initialNonChainEvaluationTime_guess(_cachedTotalFrames);
            _queuing = true;
        }
        _firstFrame = true;
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
        // All four references create a new Array before walking the scope
        // deque, then construct String Variants directly in its native Items
        // deque. Physical scope order and duplicates are preserved; there is
        // no intermediate vector, filtering, sorting, or script `add` call.
        auto result = detail::createTJSArrayWithItems_guess();
        for(const auto &scope : _variableLabelScopes) {
            result.items->emplace_back(scope.cascadeKey);
        }
        return result.value;
    }

    // Both instance properties address the constructor-created root node
    // directly.  The native implementations have no empty-deque guard.
    void Player::setCoordinate(tjs_int value) {
        _nodes[0].coordinateMode = static_cast<int>(value);
    }

    tjs_int Player::getCoordinate() const {
        return static_cast<tjs_int>(_nodes[0].coordinateMode);
    }

    // The getter creates a TJS Array and writes the four root transform-order
    // integers straight into its native Items deque.
    tTJSVariant Player::getTransformOrder() const {
        auto result = detail::createTJSArrayWithItems_guess();
        for(const int value : _nodes[0].transformOrder) {
            result.items->emplace_back(static_cast<tjs_int>(value));
        }
        return result.value;
    }

    // The setter is intentionally non-transactional: each valid element is
    // written before the next indexed read.  A later conversion/validation
    // failure therefore leaves earlier writes and their dirty side effect in
    // place, matching all four reference binaries.
    void Player::setTransformOrder(tTJSVariant arr) {
        iTJSDispatch2 *a = arr.AsObjectNoAddRef();
        auto &root = _nodes[0];
        bool used[4] = {false, false, false, false};
        for(int i = 0; i < 4; i++) {
            tTJSVariant elem;
            int v = 0;
            if(TJS_SUCCEEDED(a->PropGetByNum(TJS_MEMBERMUSTEXIST, i, &elem, a)))
                v = static_cast<int>((tjs_int)elem);
            if(static_cast<unsigned int>(v) > 3u || used[v])
                TVPThrowExceptionMessage(
                    TJS_W("illegul variable for transform order"));
            if(root.transformOrder[i] != v) {
                root.transformOrder[i] = v;
                root.delta.dirty = true;
            }
            used[v] = true;
        }
    }

    // Class-level (process-global) state backing the defaultSyncActive and
    // defaultTransformOrder properties. All four references initialize the
    // byte to false and the four integers to {0,3,2,1}.
    bool Player::s_defaultSyncActive = false;
    int Player::s_defaultTransformOrder[4] = {0, 3, 2, 1};

    // All four references build a fresh 4-element TJS Array by pushing each
    // process-global order value as an integer Variant into its native Items
    // container.
    tTJSVariant Player::getDefaultTransformOrder() const {
        auto result = detail::createTJSArrayWithItems_guess();
        for(const int value : s_defaultTransformOrder) {
            result.items->emplace_back(static_cast<tjs_int>(value));
        }
        return result.value;
    }

    // The four setters first use the native Variant-to-object conversion, then
    // fetch indices 0..3 with flag 1024. A failed fetch throws the typo-bearing
    // size error; each integer must be a unique value in [0,3], otherwise the
    // typo-bearing variable error is thrown.
    void Player::setDefaultTransformOrder(tTJSVariant arr) {
        iTJSDispatch2 *a = arr.AsObjectNoAddRef();
        // Binary writes each global immediately inside the loop (interleaved
        // with the PropGet/validate of the next index), so a mid-loop throw
        // leaves the earlier indices already written. Mirror that incremental
        // write rather than deferring, to match the partial-write-on-error
        // behavior of all four native setters.
        bool used[4] = {false, false, false, false};
        for(int i = 0; i < 4; i++) {
            tTJSVariant elem;
            // PropGetByNum(flags=TJS_MEMBERMUSTEXIST, num=i) uses the Array
            // itself as ObjThis. The must-exist flag is load-bearing: it makes
            // a too-short array fail here so the
            // L"illegul size of transform order" throw fires (flag 0 would let a
            // missing index succeed-with-void and skip the error path).
            if(TJS_FAILED(a->PropGetByNum(TJS_MEMBERMUSTEXIST, i, &elem, a)))
                TVPThrowExceptionMessage(
                    TJS_W("illegul size of transform order"));
            const int v = static_cast<int>((tjs_int)elem);
            if((unsigned)v > 3 || used[v])
                TVPThrowExceptionMessage(
                    TJS_W("illegul variable for transform order"));
            // Native order: global[i] = v; used[v] = true.
            s_defaultTransformOrder[i] = v;
            used[v] = true;
        }
    }

    // --- Core methods ---
    double Player::randomInRange_guess(double minimum, double maximum) {
        double value = minimum;
        if(minimum != maximum) {
            value = minimum + (maximum - minimum) * random();
        }
        return value;
    }

    // The Player retains an independent reference to its canonical
    // ResourceManager dispatch across the call.  The call status is ignored;
    // conversion of the result Variant is the observable success/failure
    // boundary.  The actual Math.RandomGenerator belongs to ResourceManager.
    double Player::random() {
        tTJSVariant resourceManagerCopy(_resourceManager);
        DispatchReleaseGuard_guess resourceManager{
            resourceManagerCopy.AsObject()};
        resourceManagerCopy.Clear();

        tTJSVariant result;
        (void)resourceManager.dispatch->FuncCall(
            0, TJS_W("random"), &detail::randomMemberHint_guess,
            &result, 0, nullptr,
            resourceManager.dispatch);
        return result.AsReal();
    }

    // Motion.Player has no hair/parts/bust scale triplet. Motion.EmotePlayer's
    // methods and properties target three consecutive doubles in its direct
    // Engine payload; the D3D facade reaches the same Engine-owned fields
    // through its primary EmoteObject. See EmotePlayer::setHairScale.

    // The four current references share the emitter allocation, cache and
    // update flow, but their stop predicates split by pointer width. Both
    // 64-bit builds stop for a zero amplitude, a collapsed angle interval, or
    // two zero frequencies. Both 32-bit builds stop for a zero amplitude or a
    // zero freqX, without consulting the interval or freqY. This is a genuine
    // two-versus-two binary difference, not a decompiler simplification; keep
    // it explicit. Full mappings and pseudocode are in
    // analysis/motionplayer_d3d_variable_wind_four_binary_2026-08-11.md.
    void EmoteEngine::setWind_guess(float minAngle, float maxAngle,
                                    float amplitude, float freqX,
                                    float freqY) {
        const float normalizedAmplitude = std::abs(amplitude);
        const float normalizedMin = amplitude >= 0.0f ? minAngle : maxAngle;
        const float normalizedMax = amplitude >= 0.0f ? maxAngle : minAngle;

        bool shouldStop = false;
#if INTPTR_MAX == INT64_MAX
        shouldStop = normalizedAmplitude == 0.0f ||
                     normalizedMax == normalizedMin ||
                     (freqX == 0.0f && freqY == 0.0f);
#else
        shouldStop = normalizedAmplitude == 0.0f || freqX == 0.0f;
#endif
        if (shouldStop) {
            // stop: delete + null the emitter, leave caches as-is.
            if (_windEmitter) {
                delete _windEmitter;
                _windEmitter = nullptr;
            }
            return;
        }

        const double divisionRatio = _metadataScale;

        EmoteWindEmitter *emitter = _windEmitter;
        if(!emitter || _windMin != normalizedMin ||
           _windMax != normalizedMax) {
            // Preserve the native raw-owner replacement order. In particular,
            // do not clear the member before new: allocation failure leaves the
            // old address in the member even though that allocation was freed.
            if(emitter) {
                delete emitter;
            }
            emitter = new EmoteWindEmitter(
                static_cast<float>(normalizedMin / divisionRatio),
                static_cast<float>(normalizedMax / divisionRatio));
            _windEmitter = emitter;
        }

        _windMin = normalizedMin;
        _windMax = normalizedMax;
        _windAmp = normalizedAmplitude;
        _windFreqX = freqX;
        _windFreqY = freqY;

        const float direction =
            emitter->endPos < emitter->startPos ? -1.0f : 1.0f;
        emitter->yHi = freqX;
        emitter->yLo = freqY;
        emitter->gate = 1;
        emitter->velocity = direction *
            static_cast<float>(normalizedAmplitude / divisionRatio);
        emitter->emitAccumulator = 0.0f;
    }

    tTJSVariant Player::getCameraOffset() {
        ncbDictionaryAccessor dictionary;
        dictionary.SetValue(TJS_W("x"), _cameraOffsetX,
                            TJS_MEMBERENSURE,
                            &detail::xMemberHint_guess);
        dictionary.SetValue(TJS_W("y"), _cameraOffsetY,
                            TJS_MEMBERENSURE,
                            &detail::yMemberHint_guess);
        return tTJSVariant(dictionary.GetDispatch(), dictionary.GetDispatch());
    }

    void Player::setCameraOffset(double x, double y) {
        _cameraOffsetX = static_cast<float>(x);
        _cameraOffsetY = static_cast<float>(y);
    }

    void Player::modifyRoot() {
        _nodes[0].delta.dirty = true;
    }

    bool Player::getRootModified_guess() const {
        // The native getter performs an unchecked root-node dereference.
        return _nodes[0].delta.dirty;
    }

} // namespace motion
