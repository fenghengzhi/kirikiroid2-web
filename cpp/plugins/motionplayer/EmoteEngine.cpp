// EmoteEngine implementation. Aligned with libkrkr2.so sub_67E38C (ctor),
// sub_67D01C (progress) and sub_6766E0 (applyVarControllers).
//
// CLAUDE.md rule satisfied: Player is held via raw pointer + manual new/delete,
// matching the binary's explicit `operator new(0x568); Player_ctor(...)` pattern.

#include "EmoteEngine.h"

#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

#include "EmotePlayer.h"  // Player + EmotePlayer + ResourceManager
#include "Player.h"

#define LOGGER spdlog::get("plugin")
#define STUB_WARN(name) LOGGER->warn("EmoteEngine::" #name "() stub called")

namespace motion {

    // Aligned with libkrkr2.so sub_67E38C EmoteEngine_ctor @ 0x67E38C.
    //
    // Binary behaviour summary (from EmoteEngine_controllers.md):
    //   1) memset + sub_xxx_init on 10 std::deque headers (offsets 0..720)
    //   2) zero scalar/state region (+800..+864) with float=1.0f@856
    //   3) 4 inline `vector reserve(10)` blocks (PB stubbed)
    //   4) v13 = operator new(0x568); Player_ctor(v13)
    //   5) a1[134..140] = 7 controllers, each operator new + ctor_zero
    //   6) zero matrix/state @+1128..+1167; a1[150]=double 1.0;
    //      a1[290]=int 1; *((BYTE*)a1+1162)=1 (_dirty seeded true)
    //   7) ...more vector reserve(10) blocks...
    //   8) reset 4 controllers (134, 135, 137, 136 — note order!) seeding
    //      default values (pos=0,0; scale=1.0; angle=0; color=identity).
    //
    // C++ member-init handles deque default construction (empty); we replicate
    // steps 4, 5, 8 explicitly.
    EmoteEngine::EmoteEngine(ResourceManager rm) {
        // Step 4: allocate and construct the Player heap object (+1064).
        // Binary: `v13 = operator new(0x568); Player_ctor(v13, a2)`.
        _player = new Player(std::move(rm));
        _player->_engineBack = this;

        // Step 5: allocate the 7 controllers (a1[134..140] = +1072..+1120).
        _ctlPosition         = new EmoteVarController();
        EmoteVarController_ctor(_ctlPosition,        2); // count=2 (x,y)

        _ctlScale            = new EmoteVarController();
        EmoteVarController_ctor(_ctlScale,           1); // count=1 (uniform)

        _ctlColor            = new EmoteVarController();
        EmoteVarController_ctor(_ctlColor,           4); // count=4 (RGBA)

        _ctlAngle            = new EmoteAngleController();
        EmoteAngleController_ctor(_ctlAngle,         0);

        _ctlHairPartsTarget  = new EmoteVarController();
        EmoteVarController_ctor(_ctlHairPartsTarget, 2);

        _ctlBust1Target      = new EmoteVarController();
        EmoteVarController_ctor(_ctlBust1Target,     2);

        _ctlBust2Target      = new EmoteVarController();
        EmoteVarController_ctor(_ctlBust2Target,     2);

        // Step 6 partial: +1162 _dirty defaults to true via in-class initializer.
        // Step 8: reset 4 direct controllers (pos/scale/angle/color) with
        //   identity default values. (Detailed reset pattern in binary not yet
        //   replicated — controller defaults are already zero-initialized via
        //   EmoteVarController_ctor, and scale default 1.0 is seeded below.)
        if (_ctlScale->currentValue && _ctlScale->count > 0) {
            // Scale uniform default 1.0 (seed of "no scaling").
            for (int i = 0; i < _ctlScale->count * 4; ++i) {
                _ctlScale->currentValue[i] = 1.0f;
            }
        }
        if (_ctlColor->currentValue && _ctlColor->count > 0) {
            // Color default white (xmmword_14D68D0 _guess = (1,1,1,1) RGBA).
            for (int i = 0; i < _ctlColor->count * 4; ++i) {
                _ctlColor->currentValue[i] = 1.0f;
            }
        }
    }

    // EmoteEngine dtor — manual cleanup of 7 controllers + Player + bind list.
    // PLATFORM_BOUNDARY: libkrkr2.so dtor not yet separately reverse-engineered;
    //   this follows the standard "reverse of ctor" pattern.
    EmoteEngine::~EmoteEngine() {
        // Free bind linked list (PLATFORM_BOUNDARY: structure stubbed).
        for (EmoteBindListEntry* e = _bindListHead; e; ) {
            EmoteBindListEntry* next = e->next;
            delete e;
            e = next;
        }
        _bindListHead = nullptr;

        // Delete 7 controllers in reverse-of-ctor order.
        if (_ctlBust2Target)     { EmoteVarController_dtor(_ctlBust2Target);     delete _ctlBust2Target;     _ctlBust2Target = nullptr; }
        if (_ctlBust1Target)     { EmoteVarController_dtor(_ctlBust1Target);     delete _ctlBust1Target;     _ctlBust1Target = nullptr; }
        if (_ctlHairPartsTarget) { EmoteVarController_dtor(_ctlHairPartsTarget); delete _ctlHairPartsTarget; _ctlHairPartsTarget = nullptr; }
        if (_ctlAngle)           { EmoteAngleController_dtor(_ctlAngle);         delete _ctlAngle;           _ctlAngle = nullptr; }
        if (_ctlColor)           { EmoteVarController_dtor(_ctlColor);           delete _ctlColor;           _ctlColor = nullptr; }
        if (_ctlScale)           { EmoteVarController_dtor(_ctlScale);           delete _ctlScale;           _ctlScale = nullptr; }
        if (_ctlPosition)        { EmoteVarController_dtor(_ctlPosition);        delete _ctlPosition;        _ctlPosition = nullptr; }

        // Delete the Player heap object last (so _engineBack-using fields die first).
        delete _player;
        _player = nullptr;
    }

    // Aligned with libkrkr2.so sub_6766E0
    //   EmoteEngine_applyVarControllers_pos_scale_color_angle @ 0x6766E0.
    //
    // Binary call shape (verified):
    //   EmoteVarController_step(ctlPosition,    posOut[2],   dt);
    //   EmoteVarController_step(ctlScale,       scaleOut[1], dt);
    //   EmoteVarController_step(ctlColor,       colorOut[4], dt);
    //   EmoteAngleController_step(ctlAngle,     &angleOut,   dt);
    //   Player_setCoord(player, posOut[0], posOut[1]);
    //   Player_setSlant(player, scaleOut[0], scaleOut[0]);
    //   *(double*)(this+1176) = 1.0 / (this+1168 * scaleOut[0]);  // scale denom
    //   sub_6CD724(player, packBytes(colorOut));                    // color apply
    //   Player_setAngleDeg(player, angleOut);
    //
    // PLATFORM_BOUNDARY: Player_setCoord/setSlant/setColor/setAngleDeg are
    //   referenced by binary name but the local equivalents are STUB_WARN at
    //   present (P1 will wire them up). Controllers run their step fns here.
    void EmoteEngine::applyVarControllers_pos_scale_color_angle(float dt) {
        float posOut[2]   = {0.0f, 0.0f};
        float scaleOut[1] = {1.0f};
        float colorOut[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float angleOut    = 0.0f;

        if (_ctlPosition) EmoteVarController_step(_ctlPosition,   posOut,   dt);
        if (_ctlScale)    EmoteVarController_step(_ctlScale,      scaleOut, dt);
        if (_ctlColor)    EmoteVarController_step(_ctlColor,      colorOut, dt);
        if (_ctlAngle)    EmoteAngleController_step(_ctlAngle, &angleOut,   dt);

        // Scale denominator at +1176 (per spec):
        //   *(double*)(this+1176) = 1.0 / (this+1168 * scaleOut[0])
        if (_meshDivisionRatio != 0.0 && scaleOut[0] != 0.0f) {
            _meshDivisionRatioDup = 1.0 / (_meshDivisionRatio * scaleOut[0]);
        }

        // Apply to player — P1 stubs (binary calls Player_setCoord/setSlant/
        // setColor/setAngleDeg). Avoid noisy logging here; caller progress()
        // already STUB_WARN's.
        (void)posOut; (void)scaleOut; (void)colorOut; (void)angleOut;
    }

    // Aligned with libkrkr2.so sub_67D01C EmoteEngine_progress @ 0x67D01C.
    //
    // Binary main loop (from EmoteEngine_controllers.md):
    //
    //   Player_preProgress();
    //   while (dt > 0 || _dirty@1162):
    //       step = fmin(dt, 1.1)           // physics step cap
    //       _dirty@1162 = false
    //       for each elem in deque#4..deque#10 (skip #7 pool): step_fn(elem, step)
    //       applyVarControllers_pos_scale_color_angle(step)
    //       if (player@1128 && player+1544 flag) sub_6687E8(step)
    //       dt -= step
    //   // post-loop:
    //   for (entry = _bindListHead; entry; entry = entry->next):
    //       sub_67C560 / sub_67C6B0 / Player_bindParameterValue
    //   sub_67C8A8(this); sub_6D2A54(player, 0, dt);
    //   if (dt != 0 && !syncWaiting@1159):
    //       EmoteVarController_step(_ctlHairPartsTarget, v71, dt);
    //       EmoteVarController_step(_ctlBust1Target,     v71, dt);
    //       EmoteVarController_step(_ctlBust2Target,     v71, dt);
    //       stepHairParts(dt);
    //       stepBust(_ctlBust1Target, _bustChain1Nodes, _bustSpring1Const, dt);
    //       stepBust(_ctlBust2Target, _bustChain2Nodes, _bustSpring2Const, dt);
    //
    // Physics step functions (stepHairParts, stepBust, 6 deque step fns) are
    // STUB_WARN here — structural alignment only. P1 will port them.
    void EmoteEngine::progress(float dt) {
        // Player_preProgress() stub — Player already has its own progress
        // pipeline (PlayerFrameProgress.cpp) that the EmotePlayer calls
        // directly. Keeping the call point as a documented anchor:
        // PLATFORM_BOUNDARY: Player_preProgress not isolated as a separate
        //   call yet.

        // dt-slice main loop with physics step cap = 1.1f.
        while (dt > 0.0f || _dirty) {
            const float step = std::fmin(dt, 1.1f);
            _dirty = false;

            // 6 active deques iterated by step functions (per binary):
            //   #4 sub_663BDC, #5 sub_665600, #6 sub_666068, #8 sub_666BF8,
            //   #9 sub_668470, #10 inline table lookup.
            // PLATFORM_BOUNDARY: step functions stubbed; deques are empty until
            //   setVariable populates them with binary-typed POD elements (P2).
            if (!_stateMachineDeque4.empty())   { STUB_WARN(stepDeque4_sub_663BDC); }
            if (!_stateMachineDeque5.empty())   { STUB_WARN(stepDeque5_sub_665600); }
            if (!_compositeVarDeque6.empty())   { STUB_WARN(stepDeque6_sub_666068); }
            if (!_auxVarDeque8.empty())         { STUB_WARN(stepDeque8_sub_666BF8); }
            if (!_vectorVarDeque9.empty())      { STUB_WARN(stepDeque9_sub_668470); }
            if (!_lookupCurvesDeque10.empty())  { STUB_WARN(stepDeque10_lookup); }

            // Apply the 4 direct controllers (pos/scale/color/angle).
            applyVarControllers_pos_scale_color_angle(step);

            // if (player@1128 && player+1544 flag) sub_6687E8(step)
            //   PLATFORM_BOUNDARY: sub_6687E8 and player+1544 not reversed.

            dt -= step;
        }

        // Post-loop: iterate linked list of pending bind evaluations at +1456.
        // PLATFORM_BOUNDARY: sub_67C560/67C6B0/Player_bindParameterValue stubs.
        for (EmoteBindListEntry* entry = _bindListHead; entry; entry = entry->next) {
            // sub_67C560(this, ...);
            // sub_67C6B0(this, ...);
            // Player_bindParameterValue_writesHM1_HM2(...);
        }

        // sub_67C8A8(this); sub_6D2A54(player, 0, dt);
        //   PLATFORM_BOUNDARY: stubs.

        // Physics-only pass when (dt != 0 && !_syncWaiting): step the 3
        // physics-target controllers, then run stepHairParts + stepBust×2.
        if (dt != 0.0f && !_syncWaiting) {
            float discardOut[8] = {};
            if (_ctlHairPartsTarget) EmoteVarController_step(_ctlHairPartsTarget, discardOut, dt);
            if (_ctlBust1Target)     EmoteVarController_step(_ctlBust1Target,     discardOut, dt);
            if (_ctlBust2Target)     EmoteVarController_step(_ctlBust2Target,     discardOut, dt);

            // PLATFORM_BOUNDARY: physics step functions stubbed (P1 scope).
            STUB_WARN(stepHairParts);
            STUB_WARN(stepBust_chain1);
            STUB_WARN(stepBust_chain2);
        }
    }

} // namespace motion
