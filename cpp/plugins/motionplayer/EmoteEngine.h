// EmoteEngine — libkrkr2.so 1496B (sub_67E38C) emote engine.
// Aligned with .claude/agent-memory/class-layout-auditor/emoteengine_1496b_layout.md
// + .claude/agent-memory/ida-deep-analyzer/EmoteEngine_controllers.md.
//
// Object chain in libkrkr2.so:
//   D3DEmotePlayer(>=56B, ncb)
//     +24 → EmoteObject(40B, sub_67DBAC)
//             +8  → EmoteEngine(1496B = 0x5D8, sub_67E38C)
//                     +1064 → Player(1384B = 0x568, Player_ctor @ 0x6CED30)
//
// CLAUDE.md hard rules satisfied here:
//   - controllers are raw pointer fields (no unique_ptr)
//   - Player is raw pointer field (no unique_ptr)
//   - 10 binary-typed deques have 10 distinct POD element types
//   - _dirty is at EmoteEngine+1162 (NOT on Player)
//   - _meshDivisionRatio* is at EmoteEngine+1168/+1176 (NOT on Player)
//   - the 7 inline unordered_map<ttstr,V> (@824/880/936/1272/1328/1384/1440)
//     and 4 std::vector<tTJSVariant*> (@800/992/1016/1040) are typed fields,
//     NOT raw byte blocks (P0-2, 2026-05-30). HM#7 (label→double) is @+1440.
//
// PLATFORM_BOUNDARY: sizeof(EmoteEngine) on Web build will not equal 1496B
// due to libc++ deque (~64B header) vs libstdc++ (80B header) and unordered_map
// ABI differences. Offset comments document intended binary offset for
// traceability; logical contract is element type + lifetime + count semantics.
//
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include "tjs.h"
#include "EmoteSpring.h"
#include "EmoteVarController.h"
#include "EmoteAngleController.h"
#include "EmoteBlinkController.h"
#include "internal/player_containers.h"
#include "internal/legacy_variable_state.h"
#include "internal/ttstr_hash.h"

namespace PSB {
    class PSBDictionary;
    class PSBList;
} // namespace PSB

namespace motion {

    class Player;
    class ResourceManager;

    // ========================================================================
    // EmoteEngine inline-container value typedefs (libkrkr2.so EmoteEngine_ctor
    // @0x67E38C / EmoteEngine_dtor @0x67F4B8).
    //
    // The 1496B engine embeds 7 libstdc++ unordered_map<ttstr,V> (offsets
    // 824/880/936/1272/1328/1384/1440) and 4 std::vector<tTJSVariant*>
    // (offsets 800/992/1016/1040). All 7 maps share the KiriKiri ttstr hash
    // (verified: ttstr_doubleMap_upsert @0x686944 uses the exact 1025/6/9/
    // 32769/11 mix in detail::ttstr_hash_utf16). Each map node is
    // operator new(0x20)=32B {next@0, ttstr key@8 (atomic-refcounted),
    // value@16, cached_hash@24}.
    //
    // VALUE TYPES (evidence status):
    //   HM#7 @1440 = <ttstr,double> — FULLY VERIFIED. ttstr_doubleMap_upsert
    //     returns node+16 and progress() writes a double there; bind-loop
    //     reads i[2] as double. dtor releases only the key ttstr.
    //   HM#1 @824, HM#2 @880, HM#4 @1272, HM#6 @1384 — dtor does
    //     tTJSVariant_Release(node[1]) i.e. releases ONLY the key ttstr; it
    //     never touches node+16. So the value is NOT an owned dispatch/variant.
    //     The exact value width is set by the (un-reversed) setVariable write
    //     path. TODO(P-B): reverse setVariable to confirm; placeholder = double.
    //   HM#5 @1328 — dtor runs sub_68577C(this+1328) first, then walks a
    //     SEPARATE node chain at +1288 releasing key ttstr. The +1272/+1288
    //     pairing means HM#4's footprint owns the +1288 chain; HM#5's own
    //     value width is likewise un-reversed. TODO(P-B): placeholder = double.
    //   HM#3 @936 — dtor walks nodes calling sub_683E40(node+1): a compound
    //     ~104B value object (ttstr@0 + dispatch@8 + sub-object@16 + heap@96).
    //     Distinct, owned value type. TODO(P-B): reverse the value struct;
    //     placeholder = an opaque 104B POD until then.
    //
    // PLATFORM_BOUNDARY: libc++ unordered_map header (~32-40B) != libstdc++ 56B
    // and libc++ vector (24B, matches). sizeof(EmoteEngine) on the Web build can
    // therefore no longer equal 1496B exactly. User-accepted trade-off (same
    // posture as player_containers.h): we align typed K/V semantics + shared
    // hash + lifetime, not byte-level 1496B. Offset comments are for trace only.
    // ========================================================================
    namespace detail {

        // HM#1/2/4/5/6 value placeholder. dtor releases only the ttstr key, so
        // the value is a non-owned scalar. Width un-confirmed (setVariable write
        // path not yet reversed). TODO(P-B): replace `double` once confirmed.
        using EmoteScalarMap =
            std::unordered_map<ttstr, double, ttstr_hash, ttstr_equal>;

        // HM#6 @1384 value: VarRef {int32 type; int32 index} — VERIFIED by
        // EmoteEngine_buildEyeControl @0x66C77C (writes *ret=4 (type),
        // ret[1]=loopIndex) and the other category builders (type 5/6/7/8). The
        // setVariable READER @0x671228 reads type@+0/index@+4 and dispatches into
        // the matching controller deque by index. Corrects the header's prior
        // `double` placeholder for HM#6 (the dtor releases only the ttstr key, so
        // the value is a non-owned POD — an 8B {type,index} pair fits exactly).
        struct EmoteVarRef {
            int32_t type  = 0; // +0 — controller category tag (4 = eye)
            int32_t index = 0; // +4 — loop index into the category deque
        };
        using EmoteVarRefMap =
            std::unordered_map<ttstr, EmoteVarRef, ttstr_hash, ttstr_equal>;

        // HM#3 value: compound object destroyed by sub_683E40 (releases an
        // owned ttstr@0, dispatch@8, sub-object@16 via sub_683EB8, heap@96).
        // Opaque until reversed. TODO(P-B): unpack into a typed struct mirroring
        // sub_683E40's ascending-offset field destruction.
        struct EmoteHM3Value {
            // PLATFORM_BOUNDARY: 104B opaque. sub_683E40 frees in order:
            //   +96 heap (operator delete), +28 sub-object (sub_A0F778),
            //   +16 owned ptr (sub_683AA8 + delete), +8 owned ptr (sub_683EB8
            //   + delete), +0 ttstr (tTJSVariant_Release). Reverse-declaration
            //   destruction would be needed once typed. Held opaque for now so
            //   no partial/incorrect lifetime is introduced.
            unsigned char opaque[104] = {};
        };
        using EmoteHM3Map =
            std::unordered_map<ttstr, EmoteHM3Value, ttstr_hash, ttstr_equal>;

        // 4 std::vector<tTJSVariant*> (offsets 800/992/1016/1040). dtor walks
        // [begin,end) releasing each non-null element via tTJSVariant_Release,
        // then operator delete on the buffer. Non-owning of the variants beyond
        // the release (raw pointers, owner is the variant refcount).
        using VariantPtrVector = std::vector<tTJSVariant *>;

    } // namespace detail

    // ============================================================================
    // 10 deque element POD types (per binary spec).
    // Each type is distinct (CLAUDE.md hard rule: no uniform abstraction).
    // Internal field layouts are stubbed as char[N] until step functions are
    // ported in P2; sizes match the binary's per-element stride so libstdc++
    // block math parity holds logically.
    // ============================================================================

    // Deque #1 @+0 — Hair/Parts spring nodes. 48B per node (v9 += 6 QWORDs in
    //   EmoteEngine_stepHairParts @ 0x67B748). Field accesses (verbatim):
    //     *v9            (+0)  EmoteSpringState* — the 72B spring object
    //     *((BYTE*)v9+8) (+8)  init/dirty flag (1 => first-frame init branch)
    //     (char*)v9+12   (+12) shape-label ttstr (a2 to sub_67B970; *a2 AddRef'd)
    //     (char*)v9+20   (+20) HM7 key ttstr for the X output (springStep *a2)
    //     (char*)v9+28   (+28) HM7 key ttstr for the Y output (springStep *a3)
    //     *((float*)v9+9) (+36) anchorX  } resolved by sub_67B970, written back
    //     *((float*)v9+10)(+40) anchorY  } (v29 QWORD = {anchorX,anchorY})
    //   Fields accessed by NAME (node.anchorX / node.shapeLabel / node.spring);
    //   binary byte offsets are provenance comments only. No _padN: wasm layout
    //   need not match the ARM64 48B stride (ttstr/ptr are 8B on ARM64, 4B on
    //   wasm32 anyway — byte equality is unreachable and not required). Deques
    //   are empty until the (un-ported) setVariable write path populates them.
    struct EmoteHairPartsNode48B {
        EmoteSpringState* spring;     // +0
        uint8_t           initFlag;   // +8
        ttstr             shapeLabel; // +12 (8B handle)
        ttstr             keyX;       // +20 — HM7 key for X output
        ttstr             keyY;       // +28 — HM7 key for Y output
        float             anchorX;    // +36
        float             anchorY;    // +40
    };

    // Deque #2 @+80 — Bust chain #1 spring nodes. 56B per node (v15 += 7 QWORDs
    //   in EmoteEngine_stepBust @ 0x67BCE8). Field accesses (verbatim):
    //     *v15            (+0)  EmoteBustChainSpring* — the 176B chain spring
    //     *((BYTE*)v15+8) (+8)  init flag
    //     (char*)v15+12   (+12) shape-label ttstr (a2 to sub_67B970)
    //     (char*)v15+20   (+20) HM7 key ttstr — seg0 X output (v23)
    //     (char*)v15+28   (+28) HM7 key ttstr — seg1/last Y output (v7)
    //     (char*)v15+36   (+36) HM7 key ttstr — last-seg X output (v8)
    //     *((float*)v15+11)(+44) anchorX } resolved by sub_67B970, written back
    //     *((float*)v15+12)(+48) anchorY } (v52 QWORD)
    // Fields accessed by NAME; binary offsets are provenance comments only.
    // No _padN — wasm 56B stride need not match ARM64 (see note above).
    struct EmoteBustChain1Node56B {
        EmoteBustChainSpring* spring;     // +0
        uint8_t               initFlag;   // +8
        ttstr                 shapeLabel; // +12
        ttstr                 keyA;       // +20 — HM7 key (v23, seg0 X)
        ttstr                 keyB;       // +28 — HM7 key (v7, last Y)
        ttstr                 keyC;       // +36 — HM7 key (v8, last X)
        float                 anchorX;    // +44
        float                 anchorY;    // +48
    };

    // Deque #3 @+160 — Bust chain #2 spring nodes (same shape as #2).
    using EmoteBustChain2Node56B = EmoteBustChain1Node56B;

    // Deque #4-#10 — variable/state-machine element types whose step functions
    // are not yet ported (see STUB_WARN in EmoteEngine.cpp). The `char raw[N]`
    // is an honest "element stride known, internal fields not yet reversed"
    // placeholder; the binary's ARM64 stride (N) is the provenance comment, not
    // a wasm constraint, so no size assert. When a step fn is ported, replace
    // the blob with the real named-field element type.
    // Deque #4 (eye, TYPE 4) element — verified by EmoteEngine_buildEyeControl
    //   @0x66C77C (push: *elem=ctl_ptr; elem[1]=0/label) and the progress
    //   deque#4 step loop @0x67d0a4 (reads *v15 as the ctl ptr, v15+1 as the
    //   HM7 key ttstr, advances v15+=2 = 16B). So the element is
    //   {EmoteBlinkController* ctl@+0; ttstr label@+8} (16B on ARM64).
    //   Corrects the prior `char raw[16]` placeholder (EmoteEngine.h:179).
    struct EmoteEyeControlEntry_Deque4 {
        EmoteBlinkController* ctl = nullptr; // +0 — operator new(0x170) controller
        ttstr                 label;          // +8 — HM7 output key (PSB "label")
    };
    struct EmoteStateMachine16B_Deque5 { char raw[16]; };  // sub_665600, ARM64 16B
    struct EmoteCompositeVar24B_Deque6 { char raw[24]; };  // sub_666068, ARM64 24B
    struct EmoteSetupEntry40B_Deque7   { char raw[40]; };  // no step fn, ARM64 40B (_guess)
    struct EmoteAuxVar24B_Deque8       { char raw[24]; };  // sub_666BF8, ARM64 24B
    struct EmoteVectorVar48B_Deque9    { char raw[48]; };  // sub_668470, ARM64 48B
    struct EmoteLookupCurve16B_Deque10 { char raw[16]; };  // lookup table, ARM64 16B

    // ============================================================================
    // EmoteEngine — 1496B (0x5D8), no vtable. Ctor sub_67E38C.
    // ============================================================================
    class EmoteEngine {
    public:
        explicit EmoteEngine(ResourceManager rm);
        ~EmoteEngine();

        EmoteEngine(const EmoteEngine&) = delete;
        EmoteEngine& operator=(const EmoteEngine&) = delete;

        Player& player();
        const Player& player() const;

        // Aligned with libkrkr2.so sub_67D01C EmoteEngine_progress @ 0x67D01C.
        // dt-sliced physics + animation main loop. Physics step functions are
        // STUB_WARN at present; control-flow skeleton matches the binary.
        void progress(float dt);

        // Aligned with libkrkr2.so sub_6766E0
        //   EmoteEngine_applyVarControllers_pos_scale_color_angle @ 0x6766E0.
        // Steps the 4 direct controllers (pos/scale/color/angle) and applies
        // their outputs to the embedded Player. Currently the Player-side
        // apply (setCoord/setSlant/setColor/setAngleDeg) is STUB_WARN (P1).
        void applyVarControllers_pos_scale_color_angle(float dt);

        // Aligned with libkrkr2.so EmoteEngine_stepHairParts @ 0x67B748.
        //   Iterates deque #1 (_hairPartsNodes); per node resolves the "shape"
        //   anchor (sub_67B970) then drives the 72B EmoteSpringState via
        //   EmotePhysics_springStep, writing the X/Y angle outputs into HM#7.
        void stepHairParts(float dt);

        // Aligned with libkrkr2.so EmoteEngine_stepBust @ 0x67BCE8.
        //   stepBust(ctlTarget, chainNodes, springConst, dt): iterates a bust
        //   chain deque (#2 or #3); per node resolves the "shape" anchor then
        //   drives the 176B EmoteBustChainSpring via EmoteBustChainSpring_step,
        //   writing 3 angle outputs into HM#7.
        void stepBust(EmoteVarController* ctlTarget,
                      std::deque<EmoteBustChain1Node56B>& chainNodes,
                      double springConst, float dt);

        // Aligned with libkrkr2.so EmoteEngine_buildEyeControl @ 0x66C77C.
        //   For each enabled element in the metadata-base "eyeControl" PSB array:
        //   operator new(0x170) an EmoteBlinkController, run its ctor over the
        //   element dict, push {ctl, label} onto deque#4, and register a HM#6
        //   VarRef {type=4, index=loopIndex} keyed by the element's "label".
        //   `eyeControl` is the PSB list (= the binary's L"eyeControl" value
        //   passed by applyMetadata @0x67d7a4; iterated by index, count via
        //   Motion_propGetCount).
        void buildEyeControl(const PSB::PSBList* eyeControl);

    public:
        // ====== Binary field layout (ascending offset order) ======

        // +0..+79:   deque #1 — Hair/Parts spring nodes
        std::deque<EmoteHairPartsNode48B>     _hairPartsNodes;
        // +80..+159: deque #2 — Bust chain #1 spring nodes
        std::deque<EmoteBustChain1Node56B>    _bustChain1Nodes;
        // +160..+239:deque #3 — Bust chain #2 spring nodes
        std::deque<EmoteBustChain2Node56B>    _bustChain2Nodes;
        // +240..+319:deque #4 — Eye blink controllers (TYPE 4). Element =
        //   {EmoteBlinkController* ctl; ttstr label}. Populated by
        //   EmoteEngine::buildEyeControl (libkrkr2.so 0x66C77C); stepped each
        //   frame by EmoteBlinkController_step (sub_663BDC) writing the scalar
        //   result into HM#7 keyed by elem.label.
        std::deque<EmoteEyeControlEntry_Deque4> _stateMachineDeque4;
        // +320..+399:deque #5 — Variable #2
        std::deque<EmoteStateMachine16B_Deque5> _stateMachineDeque5;
        // +400..+479:deque #6 — Composite variable
        std::deque<EmoteCompositeVar24B_Deque6> _compositeVarDeque6;
        // +480..+559:deque #7 — Setup/keyframe pool (no step)
        std::deque<EmoteSetupEntry40B_Deque7>   _setupPoolDeque7;
        // +560..+639:deque #8 — Auxiliary single-value var
        std::deque<EmoteAuxVar24B_Deque8>       _auxVarDeque8;
        // +640..+719:deque #9 — Vector variable
        std::deque<EmoteVectorVar48B_Deque9>    _vectorVarDeque9;
        // +720..+799:deque #10 — Pre-baked curve lookup
        std::deque<EmoteLookupCurve16B_Deque10> _lookupCurvesDeque10;

        // +800..+823: std::vector<tTJSVariant*> (a1[100..102]).
        //   ctor zeroes begin/end/cap; dtor releases each elem + delete buffer.
        detail::VariantPtrVector _variantVector800; // +800

        // +824..+879: HM#1 unordered_map<ttstr,V> (libkrkr2.so +824).
        //   ctor: M_next_bkt(this+107,10); dtor releases key ttstr only.
        detail::EmoteScalarMap _scalarHM1_824; // +824

        // +880..+935: HM#2 unordered_map<ttstr,V> (libkrkr2.so +880).
        detail::EmoteScalarMap _scalarHM2_880; // +880

        // +936..+991: HM#3 unordered_map<ttstr,EmoteHM3Value> (libkrkr2.so +936).
        //   dtor walks nodes via sub_683E40(node+1): distinct compound value.
        detail::EmoteHM3Map _compoundHM3_936; // +936

        // +992..+1015 / +1016..+1039 / +1040..+1063: 3 std::vector<tTJSVariant*>
        //   (a1[124..]). ctor memset(this+124,0,0x48); dtor releases+deletes.
        detail::VariantPtrVector _variantVector992;  // +992
        detail::VariantPtrVector _variantVector1016; // +1016
        detail::VariantPtrVector _variantVector1040; // +1040

        // +1064: Player* (a1[133]) — independent 1384B heap object.
        //   `v13 = operator new(0x568); Player_ctor(v13)`.
        //   Raw pointer + manual new/delete (NOT unique_ptr — CLAUDE.md rule).
        Player* _player = nullptr;

        // +1072: EmoteVarController* count=2 — Position (x,y)
        EmoteVarController*   _ctlPosition = nullptr;
        // +1080: EmoteVarController* count=1 — Scale (uniform)
        EmoteVarController*   _ctlScale = nullptr;
        // +1088: EmoteVarController* count=4 — Color RGBA
        EmoteVarController*   _ctlColor = nullptr;
        // +1096: EmoteAngleController* — Angle/Rotation (shortest-path wrap)
        EmoteAngleController* _ctlAngle = nullptr;
        // +1104: EmoteVarController* count=2 — Hair/Parts physics target
        EmoteVarController*   _ctlHairPartsTarget = nullptr;
        // +1112: EmoteVarController* count=2 — Bust #1 physics target
        EmoteVarController*   _ctlBust1Target = nullptr;
        // +1120: EmoteVarController* count=2 — Bust #2 physics target
        EmoteVarController*   _ctlBust2Target = nullptr;

        // +1128: heap pointer (transform/matrix alloc). ctor zeroes it; dtor
        //   does `if (p) operator delete(p)`. Allocation site is in a setup
        //   path not reversed here. PLATFORM_BOUNDARY: payload semantics TODO.
        void* _matrixHeap1128 = nullptr; // +1128

        // +1136..+1158: zeroed scalar/state region (a1[141..143] OWORDs in ctor).
        // PLATFORM_BOUNDARY: semantics not reversed; kept as raw filler so the
        //   typed fields below land at their documented offsets logically.
        uint8_t _stateRegion_1136_1158[1159 - 1136] = {}; // 23B

        // +1159: byte syncWaiting — read by progress physics-only pass
        //   (dt!=0 && !syncWaiting@1159).
        bool _syncWaiting = false; // +1159

        // +1160: int32_t = 1 (a1[290]). _guess: state seed.
        int32_t _scalarField_1160_1 = 1; // +1160

        // +1162: byte _dirty — progress main-loop dirty check.
        //   `*((BYTE*)a1+1162) = 1` in ctor, cleared at top of each dt-slice.
        //   Migrated from Player::_emoteDirty (was incorrectly on Player).
        bool _dirty = true; // +1162

        // padding to +1168 (double-aligned)
        uint8_t _pad_1163_1167[1168 - 1163] = {}; // 5B

        // +1168: double — mesh division ratio (scale denominator).
        //   sub_6709AC (startWind) reads +1168/+1176 paired.
        //   Migrated from Player::_emoteMeshDivisionRatio.
        double _meshDivisionRatio = 1.0;     // +1168
        // +1176: double — duplicate of _meshDivisionRatio.
        //   Migrated from Player::_emoteMeshDivisionRatioDup.
        double _meshDivisionRatioDup = 1.0;  // +1176

        // +1184: double — bust chain #1 spring constant (stepBust strength).
        double _bustSpring1Const = 0.0;   // +1184 _guess
        // +1192: double — bust chain #2 spring constant.
        double _bustSpring2Const = 0.0;   // +1192 _guess
        // +1200: double = 1.0 (a1[150]).
        double _scalarField_1200_1d = 1.0; // +1200

        // +1208 / +1228 / +1248: 3 small objects, each freed by sub_A0F778
        //   (20B stride). PLATFORM_BOUNDARY: payload semantics not reversed;
        //   kept as raw filler. TODO(P-B): identify the 20B object type.
        uint8_t _smallObj1208[20] = {}; // +1208
        uint8_t _smallObj1228[20] = {}; // +1228
        uint8_t _smallObj1248[24] = {}; // +1248 (pad to +1272 map boundary)

        // +1272..+1327: HM#4 unordered_map<ttstr,V> (libkrkr2.so +1272).
        //   dtor walks the +1288 node chain releasing key ttstr.
        detail::EmoteScalarMap _scalarHM4_1272; // +1272

        // +1328..+1383: HM#5 unordered_map<ttstr,V> (libkrkr2.so +1328).
        //   dtor runs sub_68577C(this+1328) before walking its node chain.
        detail::EmoteScalarMap _scalarHM5_1328; // +1328

        // +1384..+1439: HM#6 unordered_map<ttstr,V> (libkrkr2.so +1384).
        detail::EmoteVarRefMap _scalarHM6_1384; // +1384 — {type,index} VarRef map

        // +1440..+1495: HM#7 unordered_map<ttstr,double> — VERIFIED.
        //   Written by progress() deque-step loop via ttstr_doubleMap_upsert
        //   @0x686944 (returns node+16), read by the bind-loop. Its
        //   _M_before_begin._M_nxt single-linked node chain (insertion order)
        //   IS what the binary's bind-loop and dtor walk; on libc++ this chain
        //   is not exposed (see progress() / dtor notes for the consequence).
        // PLATFORM_BOUNDARY: libc++ map header != libstdc++ 56B; binary
        //   occupies 1440..1495 (the end of the 1496B struct).
        detail::LabelValueMap _labelToValueHM7; // +1440

        // (The former `_bindListHead@1456` pseudo-field was deleted: it was a
        //  physical alias of HM#7's _M_before_begin._M_nxt, not a real member.)

        // ===== End binary-layout fields =====

        // ============================================================================
        // Legacy / local-only transitional storage.
        // PLATFORM_BOUNDARY: NOT at any binary offset. The 5 deques + 1
        //   unordered_map below hold Player::VariableAnimatorState records used
        //   by PlayerVariable.cpp / PlayerCore.cpp until the binary's typed
        //   step functions (sub_663BDC/665600/666068/666BF8/668470) are ported
        //   in P2. The binary's equivalent state lives inside the typed deques
        //   above (#4-#9) and HM2.
        //
        // Declared via forward-declaration handles to avoid pulling Player.h
        // into this header (Player.h includes EmoteEngine.h via friend hooks).
        // ============================================================================

        // Defined inline in EmotePlayer.h (after Player.h include) so the
        // VariableAnimatorState type is complete. Stored here as opaque
        // storage members initialized in EmoteEngine ctor body via placement.

        // Backward-compat legacy deques (consumed by PlayerCore.cpp
        // controllerAnimatorBucketLike_0x671228 dispatch).
        std::deque<detail::LegacyVariableAnimatorState> _type4ControllerAnimators;
        std::deque<detail::LegacyVariableAnimatorState> _type5ControllerAnimators;
        std::deque<detail::LegacyVariableAnimatorState> _type6ControllerAnimators;
        std::deque<detail::LegacyVariableAnimatorState> _type7ControllerAnimators;
        std::deque<detail::LegacyVariableAnimatorState> _type8ControllerAnimators;

        // Backward-compat legacy label→state map (consumed by PlayerVariable.cpp
        // / PlayerCore.cpp setVariable cleanup).
        std::unordered_map<std::string, detail::LegacyVariableAnimatorState>
            _variableAnimators;

        // Engine scalar fields (callers reference these by name; binary offset
        // homes pending audit). Used by EmotePlayer.cpp clone() and various
        // getters/setters as engine-side state.
        double _hairScale = 1.0;
        double _partsScale = 1.0;
        double _bustScale = 1.0;
        double _bodyScale = 1.0;
        double _progress = 0.0;
        // R3 phantom (class-layout-auditor): removed EmoteEngine `_queuing`
        // shadow field — binary Player+480 _queuing is the authoritative byte;
        // EmoteEngine has no equivalent +480 field. Port previously kept a
        // duplicate here that EmotePlayer delegate routed to via engine().
        // 1:1 fix: delegate now routes via player()._queuing (Player.h:159).
        bool _modified = false;
        bool _playCallback = false;
        double _rot = 0.0;
        double _coordX = 0.0;
        double _coordY = 0.0;
        bool _mirrorBase = false;
        bool _mirrorRequested = false;
        bool _mirrorChanged = false;
        tjs_int _color = 0xFFFFFF;
    };

} // namespace motion
