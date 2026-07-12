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
#include "EmoteEyebrowController.h"
#include "EmoteMouthController.h"
#include "EmoteSelectorController.h"
#include "EmoteLoopController.h"
#include "EmoteWindEmitter.h"
#include "internal/player_containers.h"
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
    //   HM#3 @936 — sub_687C80 allocates a 0x88-byte hash node: 16-byte
    //     {next,key} header + a 120-byte compound value. The dtor walks nodes
    //     calling sub_683E40(node+1): ttstr@0 + owned objects@8/@16 +
    //     tTJSVariant@28 + heap@96.
    //     Distinct, owned value type. TODO(P-B): reverse the value struct;
    //     placeholder = an opaque 120B POD until then.
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
            // REVERSE GAP: 120B opaque. sub_683E40 frees in order:
            //   +96 heap (operator delete), +28 sub-object (sub_A0F778),
            //   +16 owned ptr (sub_683AA8 + delete), +8 owned ptr (sub_683EB8
            //   + delete), +0 ttstr (tTJSVariant_Release). Reverse-declaration
            //   destruction would be needed once typed. Held opaque for now so
            //   no partial/incorrect lifetime is introduced.
            // sub_687C80 initializes bytes +0..+79, seeds double +72=1.0, then
            // zeroes +80..+111; the final 8 bytes are not explicitly written.
            unsigned char opaque[120] = {};
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
    // Deque #5 (eyebrow, TYPE 5) element — verified by
    //   EmoteBlinkController_ctor_slim builder @0x66CB9C (push: *elem=ctl_ptr;
    //   elem[1]=0/label; advances elem+=2 = 16B; block 512) and the slim step
    //   sub_665600. The element is {EmoteEyebrowController* ctl@+0; ttstr
    //   label@+8} (16B on ARM64), structurally identical to deque#4's element
    //   but holding the slim 0x150 controller. Corrects the prior `char raw[16]`
    //   placeholder.
    struct EmoteEyebrowControlEntry_Deque5 {
        EmoteEyebrowController* ctl = nullptr; // +0 — operator new(0x150) controller
        ttstr                   label;          // +8 — HM7 output key (PSB "label")
    };
    // Deque #6 (mouth, TYPE 6) element — verified by EmoteEngine_buildMouthControl
    //   @0x66CFBC (push 24B {ctl@+0, label@+8, talkLabel@+16}; advance elem+=24;
    //   block 504 = 21 elems) and the progress deque#6 step loop @0x67d168
    //   (reads *v30 as ctl, v30+1 as the "label" HM7 key, v30+2 as the "talkLabel"
    //   HM7 key, advances v30+=3 = 24B). This is the ONLY controller-deque whose
    //   element carries TWO ttstr keys, and the ONLY builder that inserts TWO HM#6
    //   VarRef entries (label + talkLabel, both {type=6, index=loopIndex}) for a
    //   single controller. Corrects the prior `char raw[24]` placeholder.
    struct EmoteMouthControlEntry_Deque6 {
        EmoteMouthController* ctl = nullptr; // +0 — operator new(0x70) controller
        ttstr                 label;          // +8  — HM7 key for *outBeginFrame
        ttstr                 talkLabel;      // +16 — HM7 key for *outCurrentValue
    };
    // Deque #7 (clampControl) element — 40B. Verified by the clampControl
    //   BUILDER EmoteEngine_buildClampControl @0x66EE5C and the per-entry binder
    //   sub_67C8A8 @0x67C8A8 (both stride the same 40B deque whose header base is
    //   engine+496; finish._M_cur at engine+528, block = 480B = 12 elems). The
    //   builder zeroes 40B then writes:
    //     +0  int32  type  (Motion_propGetInt "type",   default 0)  -> mode
    //     +4         (padding, zeroed by the builder)
    //     +8  double min   (Motion_propGetDouble "min",  default 0)
    //     +16 double max   (Motion_propGetDouble "max",  default 0)
    //     +24 ttstr  var_lr (Motion_propGetString "var_lr") -> X-axis HM2 key
    //     +32 ttstr  var_ud (Motion_propGetString "var_ud") -> Y-axis HM2 key
    //   Builder GATE: only entries whose "enabled" bool is set are pushed
    //   (0x66efbc). The binder sub_67C8A8 reads +0 mode, +8/+16 lo/hi, +24/+32 the
    //   two HM2-lookup keys, runs the var-track cascade (sub_67C560) on each,
    //   normalizes to [-1,1], 2D disk-remaps by mode, then writes both back via
    //   Player_bindParameterValue (engine+1064), the X-axis result negated when
    //   sub_67C6B0 sets the mirror flag.
    //   NOTE: the LIVE local model of this deque is the populated
    //   MotionSnapshot::clampControls (RuntimeSupport::collectClampControlMetadata
    //   mirrors the builder's reads at sub_66EE5C); this struct documents the
    //   binary's on-deque element layout. Corrects the prior `char raw[40]`
    //   placeholder (was mislabelled "no step fn / _guess").
    struct EmoteClampControlEntry_Deque7 {
        int    type     = 0;   // +0  — disk-remap mode (0 = squircle, 1 = clamp-circle)
        double minValue = 0.0; // +8  — lo bound
        double maxValue = 0.0; // +16 — hi bound
        ttstr  varLr;          // +24 — X-axis HM2 key (var_lr)
        ttstr  varUd;          // +32 — Y-axis HM2 key (var_ud)
    };
    // Deque #8 (transition, TYPE 7) element — 24B. Verified by
    //   EmoteEngine_buildTransitionControl @0x66D4C4 (push 24B
    //   {ctl@+0, ttstr label@+8, byte flag@+16 = 1}; advance elem+=24; block 504)
    //   and the progress deque#8 step loop @0x67d240 (reads *v45 as the ctl ptr,
    //   v45+1 (elem+8) as the HM7 "label" key, advances v45+=3 = 24B; block
    //   boundary v46=node+63 = 504B). The controller is EmoteVarController
    //   (operator new(0x80), ctor count=1). The flag byte@+16 is set to 1 by the
    //   builder and read only by setVariable case7 (the Animator_setKeyframes
    //   gate) — the progress step does NOT read it. Corrects the prior
    //   `char raw[24]` placeholder.
    struct EmoteTransitionControlEntry_Deque8 {
        EmoteVarController* ctl = nullptr; // +0 — operator new(0x80) controller (count=1)
        ttstr               label;          // +8 — HM7 output key (PSB "label")
        uint8_t             flag = 1;       // +16 — builder writes 1; setVariable case7 gate
    };
    // Deque #9 (selector, TYPE 8) element — 48B. Verified by
    //   EmoteEngine_buildSelectorControl @0x66D8FC: push to a1[86] (engine+688 =
    //   end._M_cur of the deque whose header base is engine+640 / begin._M_cur
    //   engine+656); the binary writes *v52=ctl (+0), then v52[1]/[3]/[4]/[5]=0
    //   (+8/+24/+32/+40), then *v57=label at elem+8 (LABEL_63 @0x66ddec). The
    //   progress step loop @0x67d1e0 reads *v38 as ctl, v38+1 (elem+8) as the
    //   HM7 "label" key, advances v38+=6 (48B), block boundary v39=node+60
    //   (60 qwords = 480B). So the element is {ctl, ttstr label, 32B unused}.
    //   This is the SELECTOR controller-deque (engine+656), stepped by
    //   sub_668470 — NOT the transition deque (engine+576, sub_666BF8). The
    //   member name "Deque9" is the engine-member ordinal; the brief calls the
    //   same deque "deque#8" (1-based controller-deque ordinal). engine+656 is
    //   the unambiguous ground truth shared by both names.
    struct EmoteSelectorControlEntry_Deque9 {
        EmoteSelectorController* ctl = nullptr; // +0 — operator new(0x80) controller
        ttstr                    label;          // +8 — HM7 output key (PSB "label")
        // +16 — gate byte read by setVariable case8 (`LDRB [elem+16]; CBNZ` @
        //   0x6714a0/0x671740: enqueue only when non-zero). The builder
        //   (buildSelectorControl @0x66ddac..0x66de1c) zeroes elem+8/+24/+32/+40
        //   and writes ctl@+0 / label@+8 but does NOT initialise elem+16 — it is
        //   left as raw operator-new(0x1E0) memory, so the binary's gate value is
        //   INDETERMINATE here. Modelled as an explicit flag defaulted to 1 to
        //   match the sibling transition deque (whose +16 flag is explicitly 1)
        //   and the typical non-zero new-memory case; this keeps case8 live when
        //   a selector var is actually driven. (logo motion has no selector vars,
        //   so this path is inert for the differential regardless.)
        uint8_t                  flag = 1;        // +16 — case8 enqueue gate
        // +24/+32/+40 zeroed by the binary; no further fields are read by the
        //   progress step. (The 48B stride is preserved as the element size; the
        //   trailing slots are unused dead space — kept implicit, not padded, per
        //   the byte-layout methodology.)
    };
    // Deque #10 (loopControl, TYPE 3) element — 16B. Verified by
    //   EmoteEngine_buildLoopControl @0x66E480 (push to finish._M_cur a1[96]=
    //   engine+768, block 0x200=512) and the inline progress step
    //   @0x67d2a0..0x67d370 (begin._M_cur engine+736, advance v52+=2 = 16B stride,
    //   block boundary node+64 = 512B). The binary writes *v27=ctl (+0), v27[1]=0
    //   (+8), then *v35=label (the "var_loop" value) at elem+8. So the element is
    //   {EmoteLoopController* ctl; ttstr label}. The step reads *v52 as ctl and
    //   v52+1 (elem+8) as the HM7 output key, identical to the other controller
    //   deques. The member is historically named `_lookupCurvesDeque10` (the
    //   engine-member ordinal); the brief calls it "deque#10". engine+736 is the
    //   unambiguous ground truth.
    struct EmoteLoopControlEntry_Deque10 {
        EmoteLoopController* ctl = nullptr; // +0 — operator new(0x20) controller
        ttstr                label;          // +8 — HM7 output key (PSB "var_loop")
    };

    // ============================================================================
    // EmoteEngine — 1496B (0x5D8), no vtable. Ctor sub_67E38C.
    // ============================================================================
    class EmoteEngine {
    public:
        // P3-B: RM dispatch-in. Binary EmoteEngine_ctor (sub_67E38C) receives the
        //   RM dispatch wrapper (sub_67E20C) and forwards it to Player_ctor
        //   @0x6CED30 (single-param dispatch). The engine does not own the native
        //   RM; it just passes the dispatch down.
        explicit EmoteEngine(const tTJSVariant &rmDispatch);
        ~EmoteEngine();

        EmoteEngine(const EmoteEngine&) = delete;
        EmoteEngine& operator=(const EmoteEngine&) = delete;

        Player& player();
        const Player& player() const;

        // Aligned with libkrkr2.so sub_67D01C EmoteEngine_progress @ 0x67D01C.
        // dt-sliced physics + animation main loop. Physics step functions are
        // STUB_WARN at present; control-flow skeleton matches the binary.
        void progress(float dt);

        // Aligned with libkrkr2.so EmoteEngine_preProgress_guess @0x671764.
        // EmoteEngine_progress calls this once with (force=false, original dt)
        // before entering its fmin(dt, 1.1) controller-slice loop.
        void preProgressLike_0x671764(bool force, double dt);

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

        // Aligned with libkrkr2.so EmoteBlinkController_ctor_slim builder
        //   @ 0x66CB9C (EmoteEngine_buildEyebrowControl). Same shape as
        //   buildEyeControl but: operator new(0x150) the SLIM EmoteEyebrow
        //   controller, push {ctl, label} onto deque#5, and register a HM#6
        //   VarRef {type=5, index=loopIndex} keyed by the element's "label".
        void buildEyebrowControl(const PSB::PSBList* eyebrowControl);

        // Aligned with libkrkr2.so EmoteEngine_buildMouthControl @ 0x66CFBC.
        //   For each enabled element in the metadata "mouthControl" PSB array:
        //   operator new(0x70) an EmoteMouthController, run its ctor over the
        //   element dict, push {ctl, label, talkLabel} (24B) onto deque#6, and
        //   register TWO HM#6 VarRefs {type=6, index=loopIndex} — one keyed by the
        //   element's "label", one keyed by the element's "talkLabel" (the unique
        //   double-HM-insert that distinguishes the mouth category).
        void buildMouthControl(const PSB::PSBList* mouthControl);

        // Aligned with libkrkr2.so EmoteEngine_buildSelectorControl @ 0x66D8FC.
        //   For each enabled element in the metadata "selectorControl" PSB array:
        //   assemble an optionList[] (each option resolves its "label" against the
        //   TRANSITION controller-deque to a borrowed EmoteVarController*, and
        //   reads "offValue"/"onValue"); operator new(0x80) an
        //   EmoteSelectorController (ctor swaps in the optionList + applies index
        //   0); push {ctl, label} (48B) onto deque#9 (engine+640); and register a
        //   HM#6 VarRef {type=8, index=loopIndex} keyed by the element's "label".
        //   A non-enabled element instead removes its var binding (sub_66E248) —
        //   here mirrored by skipping the element (no controller built).
        void buildSelectorControl(const PSB::PSBList* selectorControl);

        // Aligned with libkrkr2.so EmoteEngine_buildTransitionControl @ 0x66D4C4.
        //   For each enabled element in the metadata "transitionControl" PSB array:
        //   operator new(0x80) an EmoteVarController (ctor count=1), push
        //   {ctl, label, flag=1} (24B) onto deque#8 (engine+560), and register a
        //   HM#6 VarRef {type=7, index=loopIndex} keyed by the element's "label".
        //   A non-enabled element is skipped (the binary's `enabled` gate at
        //   0x66d62c skips straight to LABEL_28 without building a controller).
        //   The flag byte@+16 is set to 1 (read only by setVariable case7's
        //   Animator_setKeyframes gate, not by the progress step). This builder
        //   MUST run before buildSelectorControl: the selector's per-option
        //   refCtl is resolved by scanning THIS deque (engine+576) for a matching
        //   option label (buildSelectorControl @0x66db0c).
        void buildTransitionControl(const PSB::PSBList* transitionControl);

        // Aligned with libkrkr2.so EmoteEngine_buildLoopControl (sub_66E480)
        //   @ 0x66E480. For each enabled element in the metadata "loopControl" PSB
        //   array: read its "transitionList" sub-array of [v0,v1,span] triples,
        //   operator new(0x20) an EmoteLoopController whose keyframe vector is
        //   filled from those triples (each field stored as raw float bits — LDR S,
        //   no SCVTF), push {ctl, label} (16B) onto deque#10 (engine+736), and
        //   register a HM#6 VarRef {type=3, index=loopIndex} keyed by the element's
        //   "var_loop" value (which is ALSO the deque element's label / HM7 output
        //   key). A non-enabled element is skipped (binary `enabled` gate
        //   @0x66e5f0 -> LABEL_49) but still advances the loop index v6. This is the
        //   LAST progress-stepped controller-deque; its step is INLINED into
        //   EmoteEngine::progress (no separate step fn).
        void buildLoopControl(const PSB::PSBList* loopControl);

        // Aligned with libkrkr2.so sub_66B018 @ 0x66B018 (the "bustControl"
        //   builder). DESPITE the PSB key name "bustControl", this populates
        //   deque#1 (_hairPartsNodes) with 48B nodes whose +0 is a 72B
        //   EmoteSpringState — the SIMPLE spring consumed by stepHairParts (NOT
        //   the bust chain). For each enabled element: operator new(0x48) +
        //   EmoteSpringState_ctor, then overwrite the spring's vec3 fields from
        //   "op"/"p"/"pv" (each a dict x/y/z -> storedXYZ/posXYZ/velXYZ) and
        //   "ofs" -> biasY; node.initFlag = 1; node labels = baseLayer(shape),
        //   var_lr (X key), var_ud (Y key); register TWO HM#6 VarRefs {type=0,
        //   index=loopIndex} keyed by var_lr and var_ud.
        void buildBustControl(const PSB::PSBList* bustControl);

        // Aligned with libkrkr2.so sub_66B9D0 @ 0x66B9D0 (the "hairControl" /
        //   "partsControl" builder; tag=1 -> deque#2 _bustChain1Nodes, tag=2 ->
        //   deque#3 _bustChain2Nodes). DESPITE the key names, these populate the
        //   56B chain nodes whose +0 is a 176B EmoteBustChainSpring — the CHAIN
        //   spring consumed by stepBust. For each enabled element: operator
        //   new(0xB0) + EmoteBustChainSpring_ctor, then overwrite "op" (dict
        //   x/y/z -> +80/+92/.. root/accum) and the "p"/"pv" 2-segment lists
        //   (each a list of 2 dicts -> seg0/seg1 pos & vel); node labels =
        //   baseLayer(shape), var_lr (keyA), var_lrm (keyB), var_ud (keyC);
        //   register THREE HM#6 VarRefs {type=tag, index=loopIndex}. The 56B
        //   node's +8 init byte is NOT written by the binary (left indeterminate
        //   from raw operator-new); the local POD node zero-inits it (initFlag=0).
        void buildChainControl(std::deque<EmoteBustChain1Node56B>& chainNodes,
                               int typeTag, const PSB::PSBList* chainControl);

        // Aligned with libkrkr2.so Player_setVariable @ 0x671228.
        //   THIS is the EmoteEngine (the ~1576B object holding HM6@+1384,
        //   HM2/HM7@+1440 and the controller deques @+256/+336/+416/+576/+656);
        //   the IDA auto-name "Player_setVariable" is misleading — the offsets
        //   prove it is the engine, not the embedded 1384B motion::Player.
        //
        //   Binary signature: Player_setVariable(this, key, value, easing,
        //     durationFrames). Arg names per the binary (NOT the TJS wrapper):
        //     value         = d0 (the scalar to set),
        //     easing        = d1 (instant gate: <=0 => snap; the TJS "transition"),
        //     durationFrames= d2 (drives the transition factor v22; TJS "ease").
        //
        //   v22 (transition factor) = durationFrames==0 ? 1.0
        //                            : durationFrames>0  ? durationFrames+1.0
        //                            : 1.0/(1.0-durationFrames).
        //   (= variableEaseWeightLike_0x671228(durationFrames).)
        //
        //   Steps (verbatim):
        //     ref = HM6_lookup(_scalarHM6_1384, key);                  // sub_6887F4
        //     if (ref && ref-is-bound) {
        //       _dirty(+1162) = 1;                                      // 0x671330
        //       switch (ref.type@+16) {
        //         case 0/1/2: if (!_syncWaiting(+1159)) return; break;  // -> HM2 write
        //         case 4: enqueue eye   (sub_6638B0, deque#4[ref.index]);
        //         case 5: enqueue brow  (sub_6652D4, deque#5[ref.index]);
        //         case 6: mouth: if (key==elem.label) ctl.beginFrame=(int)value;
        //                        else if (key==elem.talkLabel) enqueue (sub_665E34);
        //         case 7: transition: if (elem.flag) Animator_setKeyframes (0x667300);
        //         case 8: enqueue selector (sub_6681E4, deque#9[ref.index]);
        //         default: return;
        //       }
        //     }
        //     _labelToValueHM7[key] = value;     // HM2 upsert (0x67135c) fallthrough
        void setVariable(const ttstr& key, double value, double easing,
                         double durationFrames);

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
        // +320..+399:deque #5 — Eyebrow controllers (TYPE 5). Element =
        //   {EmoteEyebrowController* ctl; ttstr label}. Populated by
        //   EmoteEngine::buildEyebrowControl (libkrkr2.so 0x66CB9C); stepped each
        //   frame by EmoteEyebrowController_step (sub_665600) writing the scalar
        //   result into HM#7 keyed by elem.label.
        std::deque<EmoteEyebrowControlEntry_Deque5> _stateMachineDeque5;
        // +400..+479:deque #6 — Mouth controllers (TYPE 6). Element =
        //   {EmoteMouthController* ctl; ttstr label; ttstr talkLabel} (24B).
        //   Populated by EmoteEngine::buildMouthControl (libkrkr2.so 0x66CFBC);
        //   stepped each frame by EmoteMouthController_step (sub_666068) writing
        //   beginFrame into HM#7 keyed by elem.label and currentValue into HM#7
        //   keyed by elem.talkLabel.
        std::deque<EmoteMouthControlEntry_Deque6> _compositeVarDeque6;
        // +480..+559:deque #7 — clampControl pool (40B elem, NO per-frame step
        //   fn; consumed by the binder sub_67C8A8 @0x67C8A8 once per progress,
        //   between the HM7 bind-loop and the Player-level progress sub_6D2A54).
        //   Populated by the clampControl builder EmoteEngine_buildClampControl
        //   @0x66EE5C (header base engine+496, finish._M_cur engine+528, 480B
        //   block). The LIVE local model of this deque is the populated
        //   MotionSnapshot::clampControls; this typed deque documents the binary's
        //   element layout and is reserved for a future on-deque port.
        std::deque<EmoteClampControlEntry_Deque7> _clampControlDeque7;
        // +560..+639:deque #8 — Transition controllers (TYPE 7). Element =
        //   {EmoteVarController* ctl; ttstr label; uint8_t flag=1} (24B).
        //   Populated by EmoteEngine::buildTransitionControl (libkrkr2.so
        //   0x66D4C4); stepped each frame by EmoteVarController_step (sub_666BF8)
        //   writing out[0] into HM#7 keyed by elem.label (progress @0x67d240).
        std::deque<EmoteTransitionControlEntry_Deque8> _auxVarDeque8;
        // +640..+719:deque #9 — Selector controllers (TYPE 8). Element =
        //   {EmoteSelectorController* ctl; ttstr label} (48B, begin._M_cur at
        //   engine+656). Populated by EmoteEngine::buildSelectorControl
        //   (libkrkr2.so 0x66D8FC); stepped each frame by
        //   EmoteSelectorController_step (sub_668470) writing the selected index
        //   (as float) into HM#7 keyed by elem.label. This is the deque the brief
        //   calls "deque#8 selector"; the engine-member ordinal name is "Deque9".
        std::deque<EmoteSelectorControlEntry_Deque9> _vectorVarDeque9;
        // +720..+799:deque #10 — Loop controllers (TYPE 3). Element =
        //   {EmoteLoopController* ctl; ttstr label} (16B, begin._M_cur at
        //   engine+736). Populated by EmoteEngine::buildLoopControl (libkrkr2.so
        //   0x66E480); stepped each frame by the INLINE curve sampler in
        //   EmoteEngine::progress (libkrkr2.so @0x67d2a0..0x67d370, no separate
        //   step fn) writing the curve blend (as float->double) into HM#7 keyed by
        //   elem.label. The label/HM6 key are both the PSB "var_loop" value.
        std::deque<EmoteLoopControlEntry_Deque10> _lookupCurvesDeque10;

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

        // +1128: EmoteWindEmitter* — the wind particle emitter heap object
        //   (operator new(0x61C), 1564B). ctor zeroes it; allocated lazily by
        //   Player_startWind_populate (sub_6709AC), freed there (and by
        //   stopWind) with operator delete. Advanced per frame-slice via the
        //   gated EmoteWindEmitter_step (sub_6687E8) in progress, and also fed
        //   to the bust/hair springs as their collisionCurve input
        //   (EmoteEngine_stepBust @0x67bea4: `spring->collisionCurve = *(+1128)`).
        //   (Was previously the untyped _matrixHeap1128 placeholder.)
        EmoteWindEmitter* _windEmitter = nullptr; // +1128

        // +1136..+1152: wind parameter cache, written by Player_startWind_populate
        //   (sub_6709AC). All floats. +1136 normalizedMin (v9), +1140
        //   normalizedMax (v10), +1144 |amplitude| (v6), +1148 freqX (a5), +1152
        //   freqY (a6). +1136/+1140 are also read back by startWind to decide
        //   whether to reuse the existing emitter (same start/end) or rebuild.
        float _windMin   = 0.f; // +1136
        float _windMax   = 0.f; // +1140
        float _windAmp   = 0.f; // +1144
        float _windFreqX = 0.f; // +1148
        float _windFreqY = 0.f; // +1152
        // +1156..+1158: remaining bytes of the original a1[143] OWORD region;
        //   zeroed in ctor, no reads observed. Kept as filler for documentation.
        uint8_t _stateRegion_1156_1158[1159 - 1156] = {}; // 3B

        // +1159: byte syncWaiting — read by progress physics-only pass
        //   (dt!=0 && !syncWaiting@1159).
        bool _syncWaiting = false; // +1159

        // +1160: int32_t = 1 (a1[290]). _guess: state seed.
        int32_t _scalarField_1160_1 = 1; // +1160

        // +1161: byte — the setVariable (0x671228) enqueue accumulate flag,
        //   passed as a2 to every case 4/5/6/7/8 enqueue function (sub_6638B0
        //   etc.). `(a2 & 1) != 0` => APPEND the new keyframe to the controller's
        //   existing transition queue; `== 0` => CLEAR the queue first then push.
        //   Read at 0x671340 (`*(BYTE*)(this+1161)`). Distinct from _dirty(+1162)
        //   and _syncWaiting(+1159). (Was incorrectly modelled on Player+1161 as
        //   Player::_emoteAnimatorFlag — the 0x671228 `this` is the EmoteEngine.)
        bool _emoteAnimatorFlag = false; // +1161

        // +1162: byte _dirty — progress main-loop dirty check.
        //   `*((BYTE*)a1+1162) = 1` in ctor, cleared at top of each dt-slice.
        //   Migrated from Player::_emoteDirty (was incorrectly on Player).
        bool _dirty = true; // +1162

        // +1163: byte — Motion.EmotePlayer `debugPrint` flag. NCB getter
        //   sub_681F50 reads +1163; setter sub_681F58 sets +1163=1 (set-always-1
        //   trigger, ignores arg). EmotePlayer-NCB-only field.
        bool _debugPrintFlag = false; // +1163

        // padding to +1168 (double-aligned)
        uint8_t _pad_1164_1167[1168 - 1164] = {}; // 4B

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
        // (Removed) Parallel controller-animator residue.
        // The former `_type4..8ControllerAnimators` deques + `_variableAnimators`
        //   map (type detail::LegacyVariableAnimatorState) modeled an earlier
        //   per-Player parallel controller-stepping model that was superseded by
        //   the EmoteEngine typed-deque model. Per fresh decompile of
        //   EmoteEngine_progress @0x67D01C and setVariable @0x671228: the binary's
        //   controller stepping reads ONLY the typed deques #4-#9 above (engine
        //   +256/+336/+416/+576/+656/+736) and writes outputs into HM7 (+1440);
        //   there is no independent Player-side animator bucket. The removed members
        //   were never written (zero push/emplace/insert across cpp/ — clear()/erase()
        //   only), so removal is byte-neutral. Removed 2026-06-05.
        // ============================================================================

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
