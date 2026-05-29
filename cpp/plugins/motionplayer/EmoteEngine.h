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
//   - HM2 (label→value) is at EmoteEngine+1440 (NOT +1384)
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

#include "tjs.h"
#include "EmoteVarController.h"
#include "EmoteAngleController.h"
#include "internal/player_containers.h"
#include "internal/legacy_variable_state.h"

namespace motion {

    class Player;
    class ResourceManager;

    // ============================================================================
    // 10 deque element POD types (per binary spec).
    // Each type is distinct (CLAUDE.md hard rule: no uniform abstraction).
    // Internal field layouts are stubbed as char[N] until step functions are
    // ported in P2; sizes match the binary's per-element stride so libstdc++
    // block math parity holds logically.
    // ============================================================================

    // Deque #1 @+0 — Hair/Parts spring nodes. 48B per node.
    //   Fields (EmoteEngine_stepHairParts @ 0x67B748):
    //   +0 spring_obj_ptr, +8 byte init flag, +12 anchor, +20 label1,
    //   +28 label2, +36 int state. PLATFORM_BOUNDARY: stubbed as raw.
    struct EmoteHairPartsNode48B { char raw[48]; };
    static_assert(sizeof(EmoteHairPartsNode48B) == 48, "");

    // Deque #2 @+80 — Bust chain #1 spring nodes. 56B per node.
    //   Per EmoteEngine_stepBust @ 0x67BCE8: +0 node_ptr, +8 init_flag,
    //   +12 anchor, +20 label1, +28 label2, +36 label3, +44 int state.
    struct EmoteBustChain1Node56B { char raw[56]; };
    static_assert(sizeof(EmoteBustChain1Node56B) == 56, "");

    // Deque #3 @+160 — Bust chain #2 spring nodes (same shape as #2).
    struct EmoteBustChain2Node56B { char raw[56]; };
    static_assert(sizeof(EmoteBustChain2Node56B) == 56, "");

    // Deque #4 @+240 — Eye/mouth state-machine variable (sub_663BDC).
    //   Writes 1 output to HM2. 16B element.
    struct EmoteStateMachine16B_Deque4 { char raw[16]; };
    static_assert(sizeof(EmoteStateMachine16B_Deque4) == 16, "");

    // Deque #5 @+320 — Variable #2 single-value (sub_665600). 16B.
    struct EmoteStateMachine16B_Deque5 { char raw[16]; };
    static_assert(sizeof(EmoteStateMachine16B_Deque5) == 16, "");

    // Deque #6 @+400 — Composite variable (sub_666068). 24B, 2 outputs.
    struct EmoteCompositeVar24B_Deque6 { char raw[24]; };
    static_assert(sizeof(EmoteCompositeVar24B_Deque6) == 24, "");

    // Deque #7 @+480 — Setup/keyframe pool (no step function). 40B. _guess.
    struct EmoteSetupEntry40B_Deque7 { char raw[40]; };
    static_assert(sizeof(EmoteSetupEntry40B_Deque7) == 40, "");

    // Deque #8 @+560 — Auxiliary single-value var (sub_666BF8). 24B.
    struct EmoteAuxVar24B_Deque8 { char raw[24]; };
    static_assert(sizeof(EmoteAuxVar24B_Deque8) == 24, "");

    // Deque #9 @+640 — Vector variable (sub_668470, 6×QWORD step). 48B.
    struct EmoteVectorVar48B_Deque9 { char raw[48]; };
    static_assert(sizeof(EmoteVectorVar48B_Deque9) == 48, "");

    // Deque #10 @+720 — Pre-baked curve lookup table. 16B element.
    struct EmoteLookupCurve16B_Deque10 { char raw[16]; };
    static_assert(sizeof(EmoteLookupCurve16B_Deque10) == 16, "");

    // Linked-list node @ +1456 — bind pending evals tail-loop in progress.
    //   PLATFORM_BOUNDARY: only the head pointer + `next` traversal is reversed
    //   (progress 0x67D01C tail loop).
    struct EmoteBindListEntry {
        EmoteBindListEntry* next = nullptr;
    };

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

    public:
        // ====== Binary field layout (ascending offset order) ======

        // +0..+79:   deque #1 — Hair/Parts spring nodes
        std::deque<EmoteHairPartsNode48B>     _hairPartsNodes;
        // +80..+159: deque #2 — Bust chain #1 spring nodes
        std::deque<EmoteBustChain1Node56B>    _bustChain1Nodes;
        // +160..+239:deque #3 — Bust chain #2 spring nodes
        std::deque<EmoteBustChain2Node56B>    _bustChain2Nodes;
        // +240..+319:deque #4 — Eye/mouth state machine
        std::deque<EmoteStateMachine16B_Deque4> _stateMachineDeque4;
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

        // +800..+815: OWORD zero block
        // +816: int scalar; +840/+848: int scalars; +856: float=1.0 (a1[214]);
        // +864: int (a1[108]). PLATFORM_BOUNDARY: semantics not detailed; _guess.
        uint8_t  _scalarRegion_800_OWORD[16] = {}; // +800..+815
        int32_t  _scalarField_816 = 0;             // +816
        int32_t  _scalarField_820_guess = 0;       // +820 _guess
        int32_t  _scalarField_824_guess = 0;       // +824 _guess
        int32_t  _scalarField_828_guess = 0;       // +828 _guess
        int32_t  _scalarField_832_guess = 0;       // +832 _guess
        int32_t  _scalarField_836_guess = 0;       // +836 _guess
        int32_t  _scalarField_840 = 0;             // +840
        int32_t  _scalarField_844_guess = 0;       // +844 _guess
        int32_t  _scalarField_848 = 0;             // +848
        int32_t  _scalarField_852_guess = 0;       // +852 _guess
        float    _scalarField_856_1f = 1.0f;       // +856 (a1[214]=1.0f)
        int32_t  _scalarField_860_guess = 0;       // +860 _guess
        int32_t  _scalarField_864 = 0;             // +864 (a1[108])

        // +868..+1023: 3 KiriKiri inline `vector reserve(10)` blocks.
        // PLATFORM_BOUNDARY: not detailed-reverse-engineered. 24B ptr/cap/size
        //   vector control headers populated inside setVariable type-dispatch
        //   (offsets 856/888/952). P2 TODO: reverse and unpack into typed fields.
        uint8_t _inlineVectorBlocks_868_1023[1024 - 868] = {}; // 156B

        // +1024..+1063: residual unreversed scalars.
        // PLATFORM_BOUNDARY: ctor body not exhaustively analyzed here.
        uint8_t _residual_1024_1063[1064 - 1024] = {}; // 40B _guess

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

        // +1128..+1158: 2× OWORD matrix-ish block (zeroed by ctor).
        // PLATFORM_BOUNDARY: matrix semantics not reversed.
        uint8_t _matrixRegion_1128_1158[1159 - 1128] = {}; // 31B

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

        // +1208..+1271: residual.
        // PLATFORM_BOUNDARY: not detailed-reversed.
        uint8_t _residual_1208_1271[1272 - 1208] = {}; // 64B

        // +1272..+1439: 2 more inline vector blocks (~+1272, +1328).
        // PLATFORM_BOUNDARY: not detailed-reverse-engineered. P2 TODO.
        uint8_t _inlineVectorBlocks_1272_1439[1440 - 1272] = {}; // 168B

        // +1440: HM2 — libstdc++ unordered_map<ttstr, double> label-to-value.
        //   Aligned with libkrkr2.so EmoteEngine+1440. Written by physics step
        //   functions in progress(), read by Player_bindParameterValue.
        // PLATFORM_BOUNDARY: sizeof(std::unordered_map) on libc++ ~32B vs
        //   libstdc++ 56B. Binary occupies offsets 1440..1495 (56B).
        detail::LabelValueMap _labelToValueHM2; // +1440

        // +1456 (within HM2's binary footprint): linked-list head pointer for
        //   pending bind evaluations. PLATFORM_BOUNDARY: positioned adjacent
        //   on local build.
        EmoteBindListEntry* _bindListHead = nullptr; // +1456

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
        bool _queuing = false;
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
