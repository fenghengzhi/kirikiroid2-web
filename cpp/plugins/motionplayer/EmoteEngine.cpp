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
        //
        // Step 8: reset 4 direct controllers seeding their currentValue with a
        // default. The binary inlines, for each controller, a "clear deque
        // queue + memcpy(currentValue, &seed, 4*count)" block. The ORDER in the
        // binary is a1[134] -> a1[135] -> a1[137] -> a1[136], i.e.
        //   POSITION (134, seed 0.0f, count=2)
        //   SCALE    (135, seed 1.0f, count=1)   [v73 = 1065353216 = 1.0f]
        //   ANGLE    (137, no currentValue seed — angle controller has a
        //             different block shape; only its deque is cleared)
        //   COLOR    (136, seed = xmmword_14D68D0, count=4)
        // (The local order previously did scale then color and skipped pos.)
        //
        // Reset == clear the keyframe queue + broadcast `seed` into every
        // currentValue channel (matches the binary's deque-block free +
        // memcpy(*(ctl+88), &seed, 4*count)).
        auto resetVarController = [](EmoteVarController* c, float seed) {
            if (!c) return;
            c->queue.clear();
            c->state = 0;
            c->phase = 0.0f;
            c->invDuration = 0.0f;
            if (c->currentValue && c->count > 0) {
                for (int i = 0; i < c->count; ++i) {
                    c->currentValue[i] = seed;
                }
            }
        };

        // 134: POSITION, seed 0.0f.
        resetVarController(_ctlPosition, 0.0f);
        // 135: SCALE, seed 1.0f.
        resetVarController(_ctlScale, 1.0f);
        // 137: ANGLE — binary only clears the deque (no currentValue memcpy
        //   because the angle controller's 0x70 block has no currentValue
        //   array seeded here). Clear its queue to match.
        if (_ctlAngle) _ctlAngle->queue.clear();
        // 136: COLOR, seed = xmmword_14D68D0 (a 4-float constant). The exact
        //   bytes are NOT yet read out of libkrkr2.so (rodata @0x14D68D0,
        //   referenced ONLY here). Per CLAUDE.md we do not guess the value;
        //   the color controller's currentValue stays zero-initialized from
        //   EmoteVarController_ctor until the constant is confirmed.
        //   TODO(P-C): read xmmword_14D68D0 (4 floats) and seed _ctlColor here;
        //   most likely identity white (1,1,1,1) but UNCONFIRMED.
        //   resetVarController(_ctlColor, <xmmword_14D68D0 channels>);
    }

    // EmoteEngine dtor — manual cleanup of 7 controllers + Player + bind list.
    // PLATFORM_BOUNDARY: libkrkr2.so dtor not yet separately reverse-engineered;
    //   this follows the standard "reverse of ctor" pattern.
    EmoteEngine::~EmoteEngine() {
        // libkrkr2.so dtor EmoteEngine_dtor @0x67F4B8 walks HM#7's
        // _M_before_begin._M_nxt node chain releasing each key ttstr, then
        // frees its buckets. With the typed std::unordered_map<ttstr,double>
        // _labelToValueHM7, the map's own destructor releases all key ttstrs
        // automatically (and likewise for the 6 other maps + 4 variant
        // vectors), so no manual bind-list free is needed here. The former
        // `_bindListHead` manual loop was an alias of the map internals and
        // has been removed.
        //
        // NOTE: the binary's dtor ALSO calls sub_67C8A8-adjacent cleanup and,
        // for the 4 variant vectors, tTJSVariant_Release on each element before
        // delete. The typed std::vector<tTJSVariant*> does NOT release the
        // referenced variants (it only frees the pointer buffer). TODO(P-B):
        // if/when those vectors are populated, add an explicit per-element
        // tTJSVariant_Release pass mirroring EmoteEngine_dtor @0x67F8C0/+992/
        // +1016/+1040 before the vector clears. Currently the vectors are
        // never populated (setVariable write path un-ported), so this is inert.

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
    // Binary call shape (VERIFIED by decompile of sub_6766E0):
    //   step(ctlPosition@+1072, &v);  Player_setCoord(player, v[0], v[1]);
    //   step(ctlColor@+1088,    &v);  sub_6CD724(player, packARGB(v[0..3]));
    //   step(ctlScale@+1080,    &v);  *(double*)(this+1176) =
    //                                     1.0 / (*(double*)(this+1168) * v[0]);
    //                                 Player_setSlant(player, v[0], v[0]);
    //   step(ctlAngle@+1096,    &v);  Player_setAngleDeg(player, v[0]);
    //
    // ORDER IS pos -> color -> scale -> angle (NOT pos/scale/color/angle).
    // Each apply happens IMMEDIATELY after its own step, all reusing the same
    // small output buffer (the binary reuses stack slot &v7 for every step).
    //
    // PLATFORM_BOUNDARY: Player_setCoord/setSlant/setAngleDeg and the color
    //   pack sink (sub_6CD724) are referenced by binary name; the local
    //   equivalents are not yet wired (P1). The controller steps + the +1176
    //   scale-denominator write are real here.
    void EmoteEngine::applyVarControllers_pos_scale_color_angle(float dt) {
        // Shared output buffer (mirrors the binary's single &v7 stack slot;
        // 4 floats covers the widest controller, color count=4).
        float out[4];

        // 1) POSITION (ctl@+1072, count=2) -> Player_setCoord(out[0], out[1]).
        if (_ctlPosition) {
            out[0] = out[1] = 0.0f;
            EmoteVarController_step(_ctlPosition, out, dt);
            // Player_setCoord(_player, out[0], out[1]);  // TODO(P1)
        }

        // 2) COLOR (ctl@+1088, count=4) -> sub_6CD724(packed ARGB32).
        if (_ctlColor) {
            out[0] = out[1] = out[2] = out[3] = 1.0f;
            EmoteVarController_step(_ctlColor, out, dt);
            // const uint32_t argb =
            //     (uint8_t)(int)out[0]
            //   | ((uint8_t)(int)out[1] << 8)
            //   | ((uint8_t)(int)out[2] << 16)
            //   | ((uint8_t)(int)out[3] << 24);
            // sub_6CD724(_player, argb);                 // TODO(P1)
        }

        // 3) SCALE (ctl@+1080, count=1) -> +1176 denom + Player_setSlant.
        if (_ctlScale) {
            out[0] = 1.0f;
            EmoteVarController_step(_ctlScale, out, dt);
            // Binary: *(double*)(this+1176) = 1.0 / (*(double*)(this+1168) * out[0]);
            // (no guard in the binary; division by zero yields inf as in libc).
            _meshDivisionRatioDup = 1.0 / (_meshDivisionRatio * out[0]);
            // Player_setSlant(_player, out[0], out[0]);  // TODO(P1)
        }

        // 4) ANGLE (ctl@+1096) -> Player_setAngleDeg(out[0]).
        if (_ctlAngle) {
            out[0] = 0.0f;
            EmoteAngleController_step(_ctlAngle, out, dt);
            // Player_setAngleDeg(_player, out[0]);       // TODO(P1)
        }
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
        // P0-B2 FIX: top-level gate. The binary (EmoteEngine_progress @0x67D01C,
        // thunk 0x530a5c) opens with `if (*(double*)&a2 != 0.0)` at 0x530a60 and
        // does NOTHING when dt == 0.0 — not even the _dirty drain loop nor the
        // bind-loop / physics pass. The previous local code entered the
        // `while(dt>0 || _dirty)` loop unconditionally, so a dt==0 frame with
        // _dirty set incorrectly ran one slice. Gate the whole body on dt!=0.
        if (dt == 0.0f) {
            return; // /*0x530a60 false branch*/
        }

        // P0-B3 SETUP: the binary keeps the ORIGINAL dt in v12 across the whole
        // function (set at 0x67d054 from a2) and reuses it for the final
        // physics pass gate/argument (0x67d414/0x67d420). The dt-slice loop
        // drains a SEPARATE copy (v14, from 0x67d080). Mirror that split here:
        // `dt` is the drained working copy; `originalDt` is v12.
        const float originalDt = dt; // v12 @0x67d054

        // Player_preProgress() stub — Player already has its own progress
        // pipeline (PlayerFrameProgress.cpp) that the EmotePlayer calls
        // directly. Keeping the call point as a documented anchor:
        // PLATFORM_BOUNDARY: Player_preProgress not isolated as a separate
        //   call yet.

        // dt-slice main loop with physics step cap = 1.1f.
        // Binary: `if (dt>0) goto LABEL_6` enters the do-while body; the inner
        // `do{...; dt-=step;}while(dt>0)` drains dt; the outer `while(_dirty)`
        // re-runs while the dirty flag is still set. `while(dt>0 || _dirty)`
        // is the faithful flattening (first iteration guaranteed when dt>0;
        // subsequent iterations gated by remaining dt or the dirty flag).
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

        // Post-loop: the binary (EmoteEngine_progress @0x67D01C) walks HM#7's
        // _M_before_begin._M_nxt node chain (insertion order) at +1456:
        //   for (i = *(this+1456); i; i = *i) {
        //       sub_67C560(this, &i.key, &i.value);
        //       v68 = sub_67C6B0(this, &i.key);
        //       Player_bindParameterValue(player, &i.key, 0, v68&1 ? -i.value : i.value);
        //   }
        // i.key = node+8 (ttstr), i.value = node+16 (double) — i.e. each
        // _labelToValueHM7 entry. sub_67C560 / sub_67C6B0 /
        // Player_bindParameterValue are not yet ported (stubs), so the loop
        // body has no observable effect today.
        //
        // PLATFORM_BOUNDARY (insertion-order): libstdc++ chains nodes in
        // insertion order on _M_before_begin; libc++ does NOT expose an
        // insertion-ordered chain, so iterating _labelToValueHM7 here would
        // use libc++'s bucket order. This only matters once the bind callbacks
        // above are ported AND a script observes ordering. Since the body is
        // inert, we iterate the typed map directly and accept the order
        // boundary. TODO(P-C): if a future port needs insertion order,
        // reconsider a KiriKiri inline hashtable (decision deferred — see the
        // module-alignment report; not done because no observable consumer
        // exists yet and the cost/risk was judged too high to do blindly).
        for (auto& kv : _labelToValueHM7) {
            const ttstr& label = kv.first;
            const double value  = kv.second;
            (void)label; (void)value;
            // sub_67C560(this, &label, &value);
            // const bool negate = (sub_67C6B0(this, &label) & 1) != 0;
            // Player_bindParameterValue(player, &label, 0, negate ? -value : value);
        }

        // sub_67C8A8(this); sub_6D2A54(player, 0, originalDt);
        //   PLATFORM_BOUNDARY: stubs. Note the binary passes v12 (ORIGINAL dt)
        //   to sub_6D2A54 @0x67d408, not the drained copy.

        // Physics-only pass. P0-B3 FIX: the binary gates on the ORIGINAL dt
        // (v12) and the syncWaiting byte @+1159:
        //     if (*(double*)&v12 != 0.0 && !*(_BYTE*)(this+1159))   /*0x67d414*/
        // and feeds v12 (cast to float @0x67d420) into every step call. The
        // previous local code used the post-loop `dt`, which the dt-slice loop
        // has already drained to <= 0 — so this pass almost never ran. Use
        // `originalDt`.
        if (originalDt != 0.0f && !_syncWaiting) {
            // The binary casts the double v12 to float once (0x67d420) and
            // reuses that float for all six calls.
            const float physDt = originalDt;

            // Step the 3 physics-target controllers (no output sink in the
            // binary — &v71 is a scratch buffer whose result is unused here;
            // the controllers' purpose is to advance their internal state and
            // feed the spring targets read by stepHairParts/stepBust).
            float scratch[8] = {};
            EmoteVarController_step(_ctlHairPartsTarget, scratch, physDt); // *(this+1104) @0x67d42c
            EmoteVarController_step(_ctlBust1Target,     scratch, physDt); // *(this+1112) @0x67d43c
            EmoteVarController_step(_ctlBust2Target,     scratch, physDt); // *(this+1120) @0x67d44c

            // stepHairParts(this, physDt);                                 // @0x67d458
            // stepBust(this, _ctlBust1Target, &_bustChain1Nodes,
            //          _bustSpring1Const, physDt);                         // @0x67d470 (spring const @+1184)
            // stepBust(this, _ctlBust2Target, &_bustChain2Nodes,
            //          _bustSpring2Const, physDt);                         // @0x67d488 (spring const @+1192)
            //
            // DEFER (blocked, NOT done — see returned blocker report):
            //   stepHairParts (sub_67B748) and stepBust (sub_67BCE8) both call
            //   the per-node anchor resolver sub_67B970 @0x67B970, which
            //   resolves a label -> dispatch (sub_6D38F4), PropGet "shape"
            //   (vtable+32), and reads type/x/y via sub_6635DC/sub_662668 —
            //   a deep TJS-dispatch path that is NOT yet reversed. stepBust
            //   additionally calls sub_6689A4 (a SEPARATE 2-segment chain
            //   spring, NOT EmotePhysics_springStep). Porting the spring math
            //   alone without sub_67B970 would feed the springs garbage anchors
            //   (CLAUDE.md: no patching on an architecturally-incomplete base).
            //   The leaf EmotePhysics_springStep @0x662768 IS ported
            //   (EmoteSpring.{h,cpp}) so stepHairParts can be completed once
            //   sub_67B970 is reversed. The deques #1/#2/#3 are also empty until
            //   the (un-ported) setVariable write path populates them, so this
            //   pass has no observable effect today regardless.
            STUB_WARN(stepHairParts);    // sub_67B748 — blocked on sub_67B970
            STUB_WARN(stepBust_chain1);  // sub_67BCE8 — blocked on sub_67B970 + sub_6689A4
            STUB_WARN(stepBust_chain2);  // sub_67BCE8 — blocked on sub_67B970 + sub_6689A4
        }
    }

} // namespace motion
