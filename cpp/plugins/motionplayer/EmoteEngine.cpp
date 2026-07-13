// EmoteEngine implementation. Aligned with libkrkr2.so sub_67E38C (ctor),
// sub_67D01C (progress) and sub_6766E0 (applyVarControllers).
//
// CLAUDE.md rule satisfied: Player is held via raw pointer + manual new/delete,
// matching the binary's explicit `operator new(0x568); Player_ctor(...)` pattern.

#include "EmoteEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <spdlog/spdlog.h>

#include "EmotePlayer.h"  // Player + EmotePlayer + ResourceManager
#include "Player.h"
#include "RuntimeSupport.h" // detail::narrow (G2-C bind-loop label conversion)
#include "psbfile/PSBValue.h" // PSBList / PSBDictionary / PSBBool / PSBString

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
    EmoteEngine::EmoteEngine(const tTJSVariant &rmDispatch) {
        // Step 4: allocate and construct the Player heap object (+1064).
        // Binary: `v13 = operator new(0x568); Player_ctor(v13, a2)` — a2 is the
        //   RM dispatch wrapper (P3-B single-param dispatch-in, @0x6CED30).
        _player = new Player(rmDispatch);
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
        // 136: COLOR. EmoteEngine_ctor @0x67E9D8 copies xmmword_14D68D0
        // byte-for-byte into the four currentValue channels. The rodata bytes
        // decode to {128.0f, 128.0f, 128.0f, 255.0f}; this is a per-channel
        // seed, not a scalar broadcast.
        if (_ctlColor && _ctlColor->currentValue && _ctlColor->count >= 4) {
            static constexpr float colorSeed[4] = {
                128.0f, 128.0f, 128.0f, 255.0f
            };
            _ctlColor->queue.clear();
            _ctlColor->state = 0;
            std::memcpy(_ctlColor->currentValue, colorSeed,
                        sizeof(colorSeed));
        }
    }

    // EmoteEngine dtor — manual cleanup of owned payload pointers. Aligned with
    // libkrkr2.so EmoteEngine_dtor @0x67F4B8; the typed STL members themselves
    // are destroyed automatically after this body.
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
        // Delete deque#4 (eye) controllers. The binary's dtor frees each
        //   controller-deque's heap controllers (operator delete) before tearing
        //   down the deque header; the entry's ttstr label is released by the
        //   ttstr destructor. (M2 eye vertical: only deque#4 is populated so far.)
        for (EmoteEyeControlEntry_Deque4& entry : _stateMachineDeque4) {
            delete entry.ctl;
            entry.ctl = nullptr;
        }
        _stateMachineDeque4.clear();

        // Delete deque#5 (eyebrow) controllers (M2 eyebrow vertical). Same
        //   pattern as deque#4: the entry owns the operator new(0x150) slim
        //   controller; the ttstr label is released by its own destructor.
        for (EmoteEyebrowControlEntry_Deque5& entry : _stateMachineDeque5) {
            delete entry.ctl;
            entry.ctl = nullptr;
        }
        _stateMachineDeque5.clear();

        // Delete deque#6 (mouth) controllers (M2 mouth vertical). Same pattern as
        //   deque#4/#5: the entry owns the operator new(0x70) controller; the two
        //   ttstr keys (label + talkLabel) are released by their own destructors.
        for (EmoteMouthControlEntry_Deque6& entry : _compositeVarDeque6) {
            EmoteMouthController_dtor(entry.ctl);
            delete entry.ctl;
            entry.ctl = nullptr;
        }
        _compositeVarDeque6.clear();

        // Delete deque#8 (transition) controllers (M2 transition vertical). The
        //   entry owns the operator new(0x80) EmoteVarController; release its 3
        //   heap arrays then delete. These controllers are the SELECTOR's borrowed
        //   refCtl targets — the selector dtor (below) does NOT delete them, so we
        //   are the sole owner. The ttstr label is released by its own destructor.
        for (EmoteTransitionControlEntry_Deque8& entry : _auxVarDeque8) {
            EmoteVarController_dtor(entry.ctl);
            delete entry.ctl;
            entry.ctl = nullptr;
        }
        _auxVarDeque8.clear();

        // Delete deque#9 (selector) controllers (M2 selector vertical). Same
        //   pattern: the entry owns the operator new(0x80) controller; the ttstr
        //   label is released by its own destructor. The controller's optionList
        //   holds BORROWED refCtl pointers (owned by the transition deque), so
        //   EmoteSelectorController_dtor does NOT delete those — only this entry's
        //   own controller is deleted here.
        for (EmoteSelectorControlEntry_Deque9& entry : _vectorVarDeque9) {
            EmoteSelectorController_dtor(entry.ctl);
            delete entry.ctl;
            entry.ctl = nullptr;
        }
        _vectorVarDeque9.clear();

        // Deque#10 (loopControl) — each entry owns its operator new(0x20)
        //   EmoteLoopController. No special dtor (the keyframe vector frees its own
        //   buffer); just delete the controller.
        for (EmoteLoopControlEntry_Deque10& entry : _lookupCurvesDeque10) {
            delete entry.ctl;
            entry.ctl = nullptr;
        }
        _lookupCurvesDeque10.clear();

        // Delete deque#1 (bustControl -> hair/parts simple-spring) nodes. Each
        //   node owns its operator new(0x48) EmoteSpringState; the binary's dtor
        //   walks the deque freeing each spring (operator delete). The node ttstr
        //   keys (shapeLabel/keyX/keyY) are released by their own destructors.
        for (EmoteHairPartsNode48B& node : _hairPartsNodes) {
            delete node.spring;
            node.spring = nullptr;
        }
        _hairPartsNodes.clear();

        // Delete deque#2/#3 (hair/parts -> bust chain-spring) nodes. Each node
        //   owns its operator new(0xB0) EmoteBustChainSpring. collisionCurve is a
        //   BORROWED pointer (= EmoteEngine+1128 _windEmitter), not owned here.
        for (EmoteBustChain1Node56B& node : _bustChain1Nodes) {
            delete node.spring;
            node.spring = nullptr;
        }
        _bustChain1Nodes.clear();
        for (EmoteBustChain2Node56B& node : _bustChain2Nodes) {
            delete node.spring;
            node.spring = nullptr;
        }
        _bustChain2Nodes.clear();

        // Delete 7 controllers in reverse-of-ctor order.
        if (_ctlBust2Target)     { EmoteVarController_dtor(_ctlBust2Target);     delete _ctlBust2Target;     _ctlBust2Target = nullptr; }
        if (_ctlBust1Target)     { EmoteVarController_dtor(_ctlBust1Target);     delete _ctlBust1Target;     _ctlBust1Target = nullptr; }
        if (_ctlHairPartsTarget) { EmoteVarController_dtor(_ctlHairPartsTarget); delete _ctlHairPartsTarget; _ctlHairPartsTarget = nullptr; }
        if (_ctlAngle)           { EmoteAngleController_dtor(_ctlAngle);         delete _ctlAngle;           _ctlAngle = nullptr; }
        if (_ctlColor)           { EmoteVarController_dtor(_ctlColor);           delete _ctlColor;           _ctlColor = nullptr; }
        if (_ctlScale)           { EmoteVarController_dtor(_ctlScale);           delete _ctlScale;           _ctlScale = nullptr; }
        if (_ctlPosition)        { EmoteVarController_dtor(_ctlPosition);        delete _ctlPosition;        _ctlPosition = nullptr; }

        // Free the wind emitter heap object (+1128). The binary frees it in
        //   Player_startWind_populate/stopWind (operator delete + null); on engine
        //   teardown it must also be released since +1128 owns it. Any bust/hair
        //   spring still holding it as collisionCurve has already been deleted
        //   above, so no dangling borrow remains.
        delete _windEmitter;
        _windEmitter = nullptr;

        // Delete the Player heap object last (so _engineBack-using fields die first).
        delete _player;
        _player = nullptr;

        // The binary's four pointer vectors perform an element-level refcount
        // release before freeing their buffers. The current local
        // vector<tTJSVariant*> model has no matching element Release API, so
        // reproducing that step is blocked on reversing the actual pointer
        // element type/assign helper (0x67F0CC); do not guess with delete/Clear.
    }

    // Aligned with libkrkr2.so
    //   EmoteEngine_applyVarControllers_pos_scale_color_angle @ 0x6766E0
    //   (call site inside EmoteEngine_progress @0x67d380).
    //
    // Binary body (VERIFIED by fresh decompile of 0x6766E0 + all 4 sinks, 2026-06-06):
    //   step(ctlPosition@+1072, &v7, dt);  Player_setCoord(player, v7, v8);   // @0x6CCFF8
    //   step(ctlColor@+1088,    &v7, dt);  sub_6CD724(player, packARGB);       // @0x6CD724
    //       packARGB = (u8)(int)v7 | (u8)(int)v8<<8 | (u8)(int)v9<<16 | (u8)(int)v10<<24;
    //   step(ctlScale@+1080,    &v7, dt);  *(double*)(this+1176) =
    //                                          1.0 / (*(double*)(this+1168) * v7);
    //                                      Player_setSlant(player, v7, v7);    // @0x6C0F54
    //   step(ctlAngle@+1096,    &v7, dt);  Player_setAngleDeg(player, v7);     // @0x6C0F84
    //
    // ORDER IS pos -> color -> scale -> angle. Each apply happens IMMEDIATELY
    // after its own step, all reusing the same small output buffer (the binary
    // reuses stack slot &v7 for every step).
    //
    // Sink semantics confirmed against the binary:
    //   - Player_setCoord(0x6CCFF8): (x=v7, y=v8) -> root+1592/+1600. Local
    //     Player::setCoord matches.
    //   - sub_6CD724 = Player_setColorWeight(0x6CD724): takes the int packed by
    //     THIS caller; the sink's internal R/B swizzle into +1156 is replicated
    //     by Player::setColorWeight (swapPackedRbLike_0x6CD710). So we pass the
    //     caller pack verbatim.
    //   - Player_setSlant(0x6C0F54): two args (slantX=v7, slantY=v7) -> root
    //     +1624/+1632. Local Player::setSlant(v) writes both axes = v, matching
    //     setSlant(v7, v7).
    //   - Player_setAngleDeg(0x6C0F84): input is DEGREES (no rad conversion),
    //     fed directly from step output v7.
    //
    // Note: binary derefs the 4 controller ptrs unconditionally (no null guard);
    // ctor (lines 48-58) always `new`s them so they are non-null at runtime. The
    // local if(_ctlX) guards are a conservative no-op equivalent.
    void EmoteEngine::applyVarControllers_pos_scale_color_angle(float dt) {
        // Shared output buffer (mirrors the binary's single &v7 stack slot;
        // 4 floats covers the widest controller, color count=4).
        float out[4];

        // 1) POSITION (ctl@+1072, count=2) -> Player_setCoord(v7, v8) @0x6CCFF8.
        if (_ctlPosition) {
            out[0] = out[1] = 0.0f;
            EmoteVarController_step(_ctlPosition, out, dt);
            _player->setCoord(out[0], out[1]);
        }

        // 2) COLOR (ctl@+1088, count=4) -> sub_6CD724(packed ARGB32) @0x6CD724.
        //    Pack exactly as the binary caller does (out[0]=byte0 .. out[3]=byte3);
        //    Player::setColorWeight reproduces the sink's internal R/B swizzle.
        if (_ctlColor) {
            out[0] = out[1] = out[2] = out[3] = 1.0f;
            EmoteVarController_step(_ctlColor, out, dt);
            const std::uint32_t argb =
                  (std::uint32_t)(std::uint8_t)(int)out[0]
                | ((std::uint32_t)(std::uint8_t)(int)out[1] << 8)
                | ((std::uint32_t)(std::uint8_t)(int)out[2] << 16)
                | ((std::uint32_t)(std::uint8_t)(int)out[3] << 24);
            _player->setColorWeight((tjs_int)argb);
        }

        // 3) SCALE (ctl@+1080, count=1) -> +1176 denom + Player_setSlant @0x6C0F54.
        if (_ctlScale) {
            out[0] = 1.0f;
            EmoteVarController_step(_ctlScale, out, dt);
            // Binary: *(double*)(this+1176) = 1.0 / (*(double*)(this+1168) * out[0]);
            // (no guard in the binary; division by zero yields inf as in libc).
            _meshDivisionRatioDup = 1.0 / (_meshDivisionRatio * out[0]);
            // setSlant(v7, v7): both axes = out[0]; local setSlant writes slantX=slantY=v.
            _player->setSlant(out[0]);
        }

        // 4) ANGLE (ctl@+1096) -> Player_setAngleDeg(out[0]) @0x6C0F84 (DEGREES).
        if (_ctlAngle) {
            out[0] = 0.0f;
            EmoteAngleController_step(_ctlAngle, out, dt);
            _player->setAngleDeg(out[0]);
        }
    }

    // ------------------------------------------------------------------------
    // Physics-pass helpers (file-local). EmoteEngine is a friend of Player, so
    // these free helpers read Player's private state directly, matching the
    // binary's raw `*(player + off)` field reads.
    // ------------------------------------------------------------------------
    namespace {

        // Aligned with libkrkr2.so sub_67B970 @ 0x67B970 — per-node "shape"
        // anchor resolver shared by stepHairParts and stepBust.
        //
        // Binary pseudocode (condensed):
        //   v7 = *labelPtr; AddRef(v7);
        //   sub_6D38F4(player, &label, &resolved);     // label -> layer dispatch
        //   if (!resolvedValid) return 0;
        //   shape = resolved.PropGet("shape");          // vtable+32
        //   if (!shapeIsObject) return 0;
        //   if (sub_6635DC(shape,"type") != 0) return 0;// only type==0 proceeds
        //   x = sub_662668(shape,"x"); y = sub_662668(shape,"y");
        //   sub_6CD738(player, &rootX, &rootY);         // root node +1592/+1600
        //   r = *(player_owner + 1176);                 // meshDivisionRatioDup
        //   *outX = rootY + (y - rootY)*r;              // (binary's a3)
        //   *outY = rootX + (x - rootX)*r;              // (binary's a4)
        //   return 1;
        //
        // The local layer-dispatch resolver is Player::getLayerMotion (= the
        // sub_6D38F4 -> sub_6B5AD8 path: returns the resolved node's PSB dict
        // as a tTJSVariant). PropGet "shape"/"type"/"x"/"y" replicate
        // sub_6635DC (int) / sub_662668 (double) which both call dispatch
        // vtable+32 = PropGet. sub_6CD738 reads root node X(+1592)/Y(+1600)
        // = Player::getX()/getY(). meshDivisionRatioDup is EmoteEngine+1176.
        //
        // Returns 1 on success (outX/outY written), 0 on any miss (outputs left
        // unchanged — same as the binary, which only writes on the success path
        // and returns 0 otherwise).
        int resolveShapeAnchorLike_0x67B970(EmoteEngine* self, Player* player,
                                            const ttstr& label,
                                            float* outX, float* outY) {
            // sub_6D38F4(player, &label, &resolved): resolve label -> dispatch.
            tTJSVariant resolved = player->getLayerMotion(label); // /*0x67b9cc*/
            if (resolved.Type() != tvtObject) {
                return 0; // !v29 path -> v11 = 0                  /*0x67ba18*/
            }
            iTJSDispatch2* obj = resolved.AsObjectNoAddRef();
            if (!obj) {
                return 0;
            }

            // shape = obj.PropGet("shape") (vtable+32).               /*0x67ba64*/
            tTJSVariant shapeVar;
            if (TJS_FAILED(obj->PropGet(0, TJS_W("shape"), nullptr, &shapeVar, obj))
                || shapeVar.Type() != tvtObject) {
                return 0; // !v24 path -> v11 = 0                  /*0x67bac8*/
            }
            iTJSDispatch2* shape = shapeVar.AsObjectNoAddRef();
            if (!shape) {
                return 0;
            }

            // sub_6635DC(shape, "type"): int. Nonzero -> fail (v11=0).  /*0x67bb08*/
            tTJSVariant typeVar;
            tjs_int type = 0;
            if (shape->PropGet(0, TJS_W("type"), nullptr, &typeVar, shape) == TJS_S_OK) {
                type = static_cast<tjs_int>(typeVar.AsInteger());
            }
            if (type != 0) {
                return 0; // sub_6635DC nonzero -> v11 = 0          /*0x67bb10*/
            }

            // x = sub_662668(shape,"x"); y = sub_662668(shape,"y") (doubles).
            double x = 0.0, y = 0.0;                              // /*0x67bb38 / 0x67bb5c*/
            tTJSVariant xVar, yVar;
            if (shape->PropGet(0, TJS_W("x"), nullptr, &xVar, shape) == TJS_S_OK) {
                x = xVar.AsReal();
            }
            if (shape->PropGet(0, TJS_W("y"), nullptr, &yVar, shape) == TJS_S_OK) {
                y = yVar.AsReal();
            }

            // sub_6CD738(player, &rootX, &rootY): root node +1592 / +1600.
            const double rootX = player->getX();                 // (player+200)+1592 /*0x67bb6c*/
            const double rootY = player->getY();                 // (player+200)+1600
            const double r = self->_meshDivisionRatioDup;        // EmoteEngine+1176  /*0x67bb74*/

            // *a3 = rootY + (y - rootY)*r;  *a4 = rootX + (x - rootX)*r;
            // (binary keeps the X/Y crossover verbatim — v14=y pairs with rootY
            //  into the first output, v13=x pairs with rootX into the second.)
            *outX = static_cast<float>(rootY + (y - rootY) * r); // /*0x67bb84*/
            *outY = static_cast<float>(rootX + (x - rootX) * r); // /*0x67bb9c*/
            return 1;                                            // /*0x67bba4*/
        }

    } // namespace

    // Aligned with libkrkr2.so EmoteEngine_stepHairParts @ 0x67B748.
    //
    // Binary main loop (condensed):
    //   ctl = _ctlHairPartsTarget@+1104; n = ctl->count(+80);
    //   if (n>=1) memcpy(&cur, ctl->currentValue(+88), 4*n);  // cur[0..n)
    //   v13 = dt - 0.0001;
    //   for (node in deque#1) {
    //       anchor = node[36..40];                              // prev anchor
    //       resolveShapeAnchor(this, node+12, &anchor.x, &anchor.y);
    //       if (node->initFlag) {
    //           node->initFlag = 0; ang = getAngleDeg(player);
    //           springStep(node->spring, &oX,&oY, anchor.x,anchor.y,
    //                      cur[0],cur[1], dt, scalar1200, ang);
    //       } else if (v13 > 0) {
    //           acc=0;
    //           do { st=fminf(dt-acc,1.1); acc+=st; f=acc/dt; w=1-f;
    //                ax = w*node[36] + f*anchor.x; ay = w*node[40] + f*anchor.y;
    //                ang=getAngleDeg(player);
    //                springStep(node->spring,&oX,&oY, ax,ay, cur[0],cur[1],
    //                           st, scalar1200, ang);
    //           } while (v13 > acc);
    //       }
    //       node[36..40] = anchor;                              // write back
    //       HM7[node->keyX] = oX;  HM7[node->keyY] = oY;        // double slots
    //   }
    void EmoteEngine::stepHairParts(float dt) {
        Player* const player = _player;
        EmoteVarController* const ctl = _ctlHairPartsTarget; // *(this+1104) /*0x67b788*/

        // memcpy(&cur, ctl->currentValue, 4*count) — copy current controller out.
        // cur[0]=v32, cur[1]=v33 (count==2 for hair/parts target).         /*0x67b7a4*/
        float cur[8] = {};
        const int count = ctl ? ctl->count : 0;
        if (count >= 1 && ctl->currentValue) {
            for (int i = 0; i < count && i < 8; ++i) {
                cur[i] = ctl->currentValue[i];
            }
        }

        const float v13 = dt - 0.0001f;                       // /*0x67b7d0*/

        for (EmoteHairPartsNode48B& node : _hairPartsNodes) {
            // anchor = node[36..40] (previous), then resolve overwrites it.   /*0x67b800*/
            float anchorX = node.anchorX;
            float anchorY = node.anchorY;
            resolveShapeAnchorLike_0x67B970(this, player, node.shapeLabel,
                                            &anchorX, &anchorY);  //          /*0x67b804*/

            float oX = 0.0f, oY = 0.0f;
            if (node.initFlag) {                              //              /*0x67b808*/
                node.initFlag = 0;                            //              /*0x67b810*/
                const float ang = static_cast<float>(player->emoteGetAngleRadLike_0x6CD0C0()); //  /*0x67b818*/
                // springStep(spring,&oX,&oY, anchorX,anchorY, cur0,cur1,
                //            dt, scalar1200, ang)                            /*0x67b844*/
                EmotePhysics_springStep(node.spring, &oX, &oY,
                                        anchorX, anchorY, cur[0], cur[1],
                                        dt,
                                        static_cast<float>(_scalarField_1200_1d),
                                        ang);
            } else if (v13 > 0.0f) {                          //              /*0x67b850*/
                // sub-stepped integration toward the resolved anchor.
                const float prevX = node.anchorX; // *((float*)v9+9)  /*0x67b8a8*/
                const float prevY = node.anchorY; // *((float*)v9+10)
                float acc = 0.0f;                              //             /*0x67b858*/
                do {
                    const float st = std::fmin(dt - acc, 1.1f); //           /*0x67b880*/
                    acc = acc + st;                            //             /*0x67b88c*/
                    const float f = acc / dt;                  //             /*0x67b894*/
                    const float w = 1.0f - f;                  //             v22
                    const float ax = (w * prevX) + (f * anchorX); //         /*0x67b8a8*/
                    const float ay = (w * prevY) + (f * anchorY); //         /*0x67b8ac*/
                    const float ang = static_cast<float>(player->emoteGetAngleRadLike_0x6CD0C0()); /*0x67b8c0*/
                    EmotePhysics_springStep(node.spring, &oX, &oY,
                                            ax, ay, cur[0], cur[1],
                                            st,
                                            static_cast<float>(_scalarField_1200_1d),
                                            ang);              //             /*0x67b8dc*/
                } while (v13 > acc);                            //            /*0x67b8e4*/
            }

            node.anchorX = anchorX;                            // write back  /*0x67b8f4*/
            node.anchorY = anchorY;

            // HM#7 double outputs (Player_HM2_upsert_labelToValue(this+1440,..)).
            _labelToValueHM7[node.keyX] = oX;                  //             /*0x67b904*/
            _labelToValueHM7[node.keyY] = oY;                  //             /*0x67b918*/
        }
    }

    // Aligned with libkrkr2.so EmoteEngine_stepBust @ 0x67BCE8.
    //
    // Binary signature: stepBust(this, ctlTarget(a2), chainNodes(a3),
    //                            springConst(a4 double), dt(a5 float)).
    // Main loop (condensed):
    //   n = ctlTarget->count(+80);
    //   if (n>=1) memcpy(&cur, ctlTarget->currentValue(+88), 4*n);
    //   v50 = (float)springConst;            // strength fed to chain spring (a10)
    //   v47 = dt * 0.03125;  v49 = dt - 0.0001;
    //   for (node in chain deque) {
    //       anchor = node[44..48];
    //       resolveShapeAnchor(this, node+12, &anchor.x, &anchor.y);
    //       node->spring->collisionCurve(+168) = this->_windEmitter(+1128);
    //       if (node->initFlag) {
    //           node->initFlag = 0; ang=getAngleDeg(player);
    //           chainStep(spring,&oS0,&oS1,&oLast, anchor.x,anchor.y,
    //                     cur0,cur1, dt, springConst, ang);
    //           // depth ramp using |oLast|<=28 toward node->spring[13]:
    //           ... (see inline) ...
    //       } else if (v49>0) {
    //           acc=0;
    //           do { st=fminf(dt-acc,1.1); acc+=st; f=acc/dt; w=1-f;
    //                ax = w*node[44] + f*anchor.x; ay = w*node[48] + f*anchor.y;
    //                chainStep(...); depth ramp; } while (v49>acc);
    //       }
    //       node[44..48] = anchor;
    //       HM7[node->keyA]=v23; HM7[node->keyB]=v7; HM7[node->keyC]=v8(oLastY);
    //   }
    //
    // Output / jiggle mapping (verbatim from the binary):
    //   chainStep(spring, &oSeg0(=v55), &oSeg1(=v54), &oLastY(=v53), ...);
    //   v8 = oLastY (captured right after chainStep);
    //   depth ramp gates on |oLastY| <= 28 toward spring[13];
    //   spring[12] = fmod(spring[12] + depth*spring[7]*dt, 2*pi);
    //   j = sin(spring[12]) * spring[13] * spring[8];
    //   v23 = oSeg1 + j;   v7 = oSeg0 - j;   (oSeg0/oSeg1 also updated but dead)
    //   HM7[keyA(node+20)] = v23;  HM7[keyB(node+28)] = v7;  HM7[keyC(node+36)] = v8;
    // When neither branch runs (initFlag clear AND v49<=0): v23 = dt-0.0001 and
    //   v7/v8 keep their prior value (the binary reads them un-refreshed — the
    //   deques are empty at runtime so this path never executes; we seed v7/v8=0
    //   for defined behaviour, matching the binary's effective state on entry).
    void EmoteEngine::stepBust(EmoteVarController* ctlTarget,
                               std::deque<EmoteBustChain1Node56B>& chainNodes,
                               double springConst, float dt) {
        Player* const player = _player;

        // memcpy(&cur, ctlTarget->currentValue, 4*count).                  /*0x67bd4c*/
        float cur[8] = {};
        const int count = ctlTarget ? ctlTarget->count : 0;
        if (count >= 1 && ctlTarget->currentValue) {
            for (int i = 0; i < count && i < 8; ++i) {
                cur[i] = ctlTarget->currentValue[i];
            }
        }

        const float v50 = static_cast<float>(springConst);  // a4 -> chain a10 /*0x67bd6c*/
        const float v47 = dt * 0.03125f;                     // depth ramp dt   /*0x67bdac*/
        const float v49 = dt - 0.0001f;                      //                 /*0x67bdb0*/

        for (EmoteBustChain1Node56B& node : chainNodes) {
            float anchorX = node.anchorX;                    // node[44/48]     /*0x67be94*/
            float anchorY = node.anchorY;
            resolveShapeAnchorLike_0x67B970(this, player, node.shapeLabel,
                                            &anchorX, &anchorY); //            /*0x67be98*/

            // node->spring->collisionCurve = this->_windEmitter (v12[141]). /*0x67bea4*/
            //   The spring physics reads the wind emitter's 128-slot particle
            //   field as a collision/force curve. Borrowed pointer, not owned.
            if (node.spring) {
                node.spring->collisionCurve = _windEmitter;
            }

            // Spring float-array view for the depth-ramp fields [7]/[8]/[12]/[13].
            // (binary: v28 = (float*)*v15; reads v28[7],v28[8],v28[12],v28[13].)
            float* const sp = reinterpret_cast<float*>(node.spring);

            float oSeg0 = 0.0f;  // v55 (chainStep a2)
            float oSeg1 = 0.0f;  // v54 (chainStep a3)
            float oLastY = 0.0f; // v53 (chainStep a4)
            float v23 = dt - 0.0001f; // keyA value (default when no branch runs)
            float v7  = 0.0f;          // keyB value
            float v8  = 0.0f;          // keyC value (= oLastY)

            if (node.initFlag) {                             //                /*0x67bea8*/
                node.initFlag = 0;                           //                /*0x67beb0*/
                const float ang = static_cast<float>(player->emoteGetAngleRadLike_0x6CD0C0()); //   /*0x67beb8*/
                EmoteBustChainSpring_step(node.spring, &oSeg0, &oSeg1, &oLastY,
                                          anchorX, anchorY, cur[0], cur[1],
                                          dt, v50, ang);      //               /*0x67bee4*/
                v8 = oLastY;                                  // *(float*)&v8=v53 /*0x67bee8*/
                // depth ramp (|oLastY| vs 28).                                /*0x67befc*/
                float depth = sp ? sp[13] : 0.0f;             // v33           /*0x67beec*/
                if (std::fabs(oLastY) <= 28.0f) {
                    depth = depth - v47;                      //               /*0x67bdbc*/
                    if (depth < 0.0f) depth = 0.0f;           //               /*0x67bdc8*/
                } else {
                    depth = v47 + depth;                      //               /*0x67bf04*/
                    if (depth > 1.0f) depth = 1.0f;           //               /*0x67bf10*/
                }
                if (sp) {
                    const float spd = sp[7];                  //               /*0x67bdcc*/
                    sp[13] = depth;                           //               /*0x67bdd0*/
                    const float ph = std::fmod(sp[12] + ((depth * spd) * dt),
                                               6.28318531f);  //               /*0x67bdf4*/
                    sp[12] = ph;                              //               /*0x67bdf8*/
                    const float j = (std::sin(ph) * sp[13]) * sp[8]; //        /*0x67be10*/
                    v23 = oSeg1 + j;                          // v54 + v22     /*0x67be14*/
                    v7  = oSeg0 - j;                          // v55 - v22     /*0x67be18*/
                    oSeg1 = oSeg1 + j;                        // v54 += (dead) /*0x67be1c*/
                    oSeg0 = oSeg0 - j;                        // v55 -= (dead)
                }
            } else if (v49 > 0.0f) {                           //              /*0x67bf20*/
                const float prevX = node.anchorX;             // *((float*)v15+11) /*0x67bf30*/
                const float prevY = node.anchorY;             // *((float*)v15+12) /*0x67bf24*/
                float acc = 0.0f;                              //              /*0x67bf2c*/
                do {
                    const float st = std::fmin(dt - acc, 1.1f); //           /*0x67bf48*/
                    acc = acc + st;                           //               /*0x67bf50*/
                    const float f = acc / dt;                 //               /*0x67bf58*/
                    const float w = 1.0f - f;                 //               v36
                    const float ax = (w * prevX) + (f * anchorX); //          /*0x67bf6c*/
                    const float ay = (w * prevY) + (f * anchorY); //          /*0x67bf70*/
                    const float ang = static_cast<float>(player->emoteGetAngleRadLike_0x6CD0C0()); /*0x67bf74*/
                    EmoteBustChainSpring_step(node.spring, &oSeg0, &oSeg1, &oLastY,
                                              ax, ay, cur[0], cur[1],
                                              st, v50, ang);  //               /*0x67bfa8*/
                    v8 = oLastY;                              // *(float*)&v8=v53 /*0x67bfac*/
                    // depth ramp with per-substep dt (st).                    /*0x67bfc8*/
                    float depth = sp ? sp[13] : 0.0f;         // v40           /*0x67bfb4*/
                    const float v41 = st * 0.03125f;          //               /*0x67bfc4*/
                    if (std::fabs(oLastY) <= 28.0f) {
                        depth = depth - v41;                  //               /*0x67bfe0*/
                        if (depth < 0.0f) depth = 0.0f;       //               /*0x67bfe8*/
                    } else {
                        depth = v41 + depth;                  //               /*0x67bfcc*/
                        if (depth > 1.0f) depth = 1.0f;       //               /*0x67bfd4*/
                    }
                    if (sp) {
                        const float spd = sp[7];              //               /*0x67bff0*/
                        const float ph0 = sp[12];             //               /*0x67bff4*/
                        sp[13] = depth;                       //               /*0x67bff8*/
                        const float ph = std::fmod(ph0 + (st * (depth * spd)),
                                                   6.28318531f); //            /*0x67c014*/
                        sp[12] = ph;                          //               /*0x67c018*/
                        const float j = (std::sin(ph) * sp[13]) * sp[8]; //    /*0x67c034*/
                        v23 = oSeg1 + j;                      // v54 + v46     /*0x67c038*/
                        v7  = oSeg0 - j;                      // v55 - v46     /*0x67c040*/
                        oSeg1 = oSeg1 + j;                    // v54 += (dead) /*0x67c044*/
                        oSeg0 = oSeg0 - j;                    // v55 -= (dead)
                    }
                } while (v49 > acc);                          //               /*0x67c048*/
            }

            node.anchorX = anchorX;                           // write back     /*0x67be30*/
            node.anchorY = anchorY;

            // HM#7 outputs (Player_HM2_upsert_labelToValue(this+1440,..)).
            _labelToValueHM7[node.keyA] = v23;                //               /*0x67be38*/
            _labelToValueHM7[node.keyB] = v7;                 //               /*0x67be4c*/
            _labelToValueHM7[node.keyC] = v8;                 //               /*0x67be5c*/
        }
    }

    // Aligned with libkrkr2.so EmoteEngine_buildEyeControl @ 0x66C77C.
    // Decompiled pseudocode (this conversation):
    //   count = Motion_propGetCount(eyeControl);            // 0x66c810
    //   for (v5 = 0; v5 < count; ++v5) {                    // 0x66c844
    //     elem = eyeControl[v5];                            // PropGet(0, v5)
    //     if ((propGetBool(elem, "enabled") & 1) == 0) continue;  // 0x66c8e4 gate
    //     v7 = operator new(0x170);                         // 0x66c8f0
    //     EmoteBlinkController_ctor(v7, elem);              // 0x66c8fc
    //     push_back deque#4 {ctl=v7, label=0};              // 0x66c914 (label slot zeroed)
    //     label = propGet(elem, "label");                   // 0x66c9c4
    //     deque#4.back().label = label;                     // 0x66ca10 (AddRef into slot)
    //     ref = HM6_findOrInsert(this+1384, label);         // 0x66ca28
    //     ref->type = 4; ref->index = v5;                   // 0x66ca30
    //   }
    // The binary pushes {ctl, 0} first then writes the label into the slot
    //   (with ttstr AddRef); here the entry is constructed with the label
    //   directly — same end state {ctl, label}. The HM#6 index is the LOOP
    //   index v5 (NOT the deque size), matching the binary (an element skipped
    //   by the enabled gate still advances v5 but pushes nothing, so deque index
    //   and v5 can diverge — preserved verbatim).
    void EmoteEngine::buildEyeControl(const PSB::PSBList* eyeControl) {
        if (!eyeControl) {
            return;
        }
        const int count = static_cast<int>(eyeControl->size()); // Motion_propGetCount
        for (int v5 = 0; v5 < count; ++v5) {                     // 0x66c844
            const auto elem = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                (*eyeControl)[v5]);                             // PropGet(0, v5)
            // enabled gate (0x66c8e4): skip when "enabled" is not truthy.
            bool enabled = false;
            if (elem) {
                if (const auto b = std::dynamic_pointer_cast<PSB::PSBBool>(
                        (*elem)[std::string("enabled")])) {
                    enabled = b->value;
                } else if (const auto n = std::dynamic_pointer_cast<PSB::PSBNumber>(
                               (*elem)[std::string("enabled")])) {
                    enabled = n->getLongValue() != 0;
                }
            }
            if (!enabled) {
                continue;                                       // goto LABEL_28
            }

            // operator new(0x170) + ctor (raw pointer, manual lifetime — the
            //   deque entry owns the controller; dtor is responsible for delete).
            EmoteBlinkController* ctl = new EmoteBlinkController(); // 0x66c8f0
            EmoteBlinkController_ctor(ctl, elem.get());            // 0x66c8fc

            // label = elem["label"] as ttstr (the HM7 output key).
            ttstr label;
            if (const auto s = std::dynamic_pointer_cast<PSB::PSBString>(
                    (*elem)[std::string("label")])) {
                label = ttstr(s->value.c_str());
            }

            // push {ctl, label} onto deque#4 (binary pushes {ctl,0} then writes
            //   label into the slot at 0x66ca10; same end state).
            EmoteEyeControlEntry_Deque4 entry;
            entry.ctl   = ctl;
            entry.label = label;
            _stateMachineDeque4.push_back(std::move(entry));

            // HM#6 VarRef {type=4, index=v5} keyed by label (0x66ca28..0x66ca30).
            detail::EmoteVarRef& ref = _scalarHM6_1384[label];
            ref.type  = 4;   // *v17 = 4
            ref.index = v5;  // v17[1] = v5
        }
    }

    // Aligned with libkrkr2.so EmoteEngine_buildEyebrowControl @ 0x66CB9C.
    // Decompiled pseudocode (this conversation):
    //   count = Motion_propGetCount(eyebrowControl);        // 0x66cc30
    //   for (v5 = 0; v5 < count; ++v5) {                    // 0x66cc64
    //     elem = eyebrowControl[v5];                        // PropGet(0, v5)  0x66cc80
    //     if ((Motion_propGetBool(elem,"enabled") & 1) == 0) continue; // 0x66cd04 gate
    //     v7 = operator new(0x150);                         // 0x66cd10
    //     EmoteBlinkController_ctor_slim(v7, elem);         // 0x66cd1c
    //     push_back deque#5 {ctl=v7, label=0};              // 0x66cd34 (label slot zeroed)
    //     label = propGet(elem, "label");                   // 0x66cde4
    //     deque#5.back().label = label;                     // 0x66ce30 (AddRef into slot)
    //     ref = HM6_findOrInsert(this+1384, label);         // 0x66ce48 (a1+173)
    //     ref->type = 5; ref->index = v5;                   // 0x66ce50
    //   }
    // Same structure as buildEyeControl (0x66C77C) except: new(0x150) slim
    //   controller (not 0x170), pushes onto deque#5 (engine+320, a1[46..49]),
    //   and writes HM#6 type=5. The HM#6 index is the LOOP index v5 (NOT the
    //   deque size), matching the binary (a skipped element still advances v5).
    void EmoteEngine::buildEyebrowControl(const PSB::PSBList* eyebrowControl) {
        if (!eyebrowControl) {
            return;
        }
        const int count = static_cast<int>(eyebrowControl->size()); // Motion_propGetCount
        for (int v5 = 0; v5 < count; ++v5) {                        // 0x66cc64
            const auto elem = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                (*eyebrowControl)[v5]);                             // PropGet(0, v5)
            // enabled gate (0x66cd04): skip when "enabled" is not truthy.
            bool enabled = false;
            if (elem) {
                if (const auto b = std::dynamic_pointer_cast<PSB::PSBBool>(
                        (*elem)[std::string("enabled")])) {
                    enabled = b->value;
                } else if (const auto n = std::dynamic_pointer_cast<PSB::PSBNumber>(
                               (*elem)[std::string("enabled")])) {
                    enabled = n->getLongValue() != 0;
                }
            }
            if (!enabled) {
                continue;                                           // goto LABEL_28
            }

            // operator new(0x150) + slim ctor (raw pointer, manual lifetime —
            //   the deque entry owns the controller; dtor is responsible for
            //   delete).
            EmoteEyebrowController* ctl = new EmoteEyebrowController(); // 0x66cd10
            EmoteEyebrowController_ctor(ctl, elem.get());              // 0x66cd1c

            // label = elem["label"] as ttstr (the HM7 output key).
            ttstr label;
            if (const auto s = std::dynamic_pointer_cast<PSB::PSBString>(
                    (*elem)[std::string("label")])) {
                label = ttstr(s->value.c_str());
            }

            // push {ctl, label} onto deque#5 (binary pushes {ctl,0} then writes
            //   label into the slot at 0x66ce30; same end state).
            EmoteEyebrowControlEntry_Deque5 entry;
            entry.ctl   = ctl;
            entry.label = label;
            _stateMachineDeque5.push_back(std::move(entry));

            // HM#6 VarRef {type=5, index=v5} keyed by label (0x66ce48..0x66ce50).
            detail::EmoteVarRef& ref = _scalarHM6_1384[label];
            ref.type  = 5;   // *v17 = 5
            ref.index = v5;  // v17[1] = v5
        }
    }

    // Aligned with libkrkr2.so EmoteEngine_buildMouthControl @ 0x66CFBC.
    // Decompiled pseudocode (this conversation):
    //   count = Motion_propGetCount(mouthControl);          // 0x66d054
    //   for (v5 = 0; v5 < count; ++v5) {                    // 0x66d088
    //     elem = mouthControl[v5];                          // PropGet(0, v5)  0x66d0a4
    //     if ((Motion_propGetBool(elem,"enabled") & 1) == 0) continue; // 0x66d128 gate
    //     v9 = operator new(0x70);                          // 0x66d134
    //     EmoteMouthController_ctor(v9, elem);              // 0x66d140
    //     push_back deque#6 {ctl=v9, label=0, talkLabel=0}; // 0x66d158 (both slots zeroed)
    //     label     = propGet(elem, "label");     back().label     = label;     // 0x66d220..0x66d26c
    //     talkLabel = propGet(elem, "talkLabel");  back().talkLabel = talkLabel; // 0x66d2a8..0x66d2f4
    //     ref1 = HM6_findOrInsert(this+1384, &back().label);     // 0x66d30c (a1+173)
    //     ref1->type = 6; ref1->index = v5;                       // 0x66d314
    //     ref2 = HM6_findOrInsert(this+1384, &back().talkLabel);  // 0x66d320
    //     ref2->type = 6; ref2->index = v5;                       // 0x66d32c
    //   }
    // UNIQUE to the mouth category vs eye/eyebrow:
    //   * the deque#6 element is 24B {ctl, label, talkLabel} (a THIRD ttstr).
    //   * the builder inserts TWO HM#6 VarRef entries for a single controller
    //     (label AND talkLabel), both {type=6, index=v5}. The progress loop then
    //     stepping this controller writes *outBeginFrame into HM7[label] and
    //     *outCurrentValue into HM7[talkLabel].
    //   The HM#6 index is the LOOP index v5 (NOT the deque size), matching the
    //   binary (a skipped element still advances v5).
    void EmoteEngine::buildMouthControl(const PSB::PSBList* mouthControl) {
        if (!mouthControl) {
            return;
        }
        const int count = static_cast<int>(mouthControl->size()); // Motion_propGetCount
        for (int v5 = 0; v5 < count; ++v5) {                       // 0x66d088
            const auto elem = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                (*mouthControl)[v5]);                              // PropGet(0, v5)
            // enabled gate (0x66d128): skip when "enabled" is not truthy.
            bool enabled = false;
            if (elem) {
                if (const auto b = std::dynamic_pointer_cast<PSB::PSBBool>(
                        (*elem)[std::string("enabled")])) {
                    enabled = b->value;
                } else if (const auto n = std::dynamic_pointer_cast<PSB::PSBNumber>(
                               (*elem)[std::string("enabled")])) {
                    enabled = n->getLongValue() != 0;
                }
            }
            if (!enabled) {
                continue;                                          // goto LABEL_34
            }

            // operator new(0x70) + ctor (raw pointer, manual lifetime — the deque
            //   entry owns the controller; dtor is responsible for delete).
            EmoteMouthController* ctl = new EmoteMouthController(); // 0x66d134
            EmoteMouthController_ctor(ctl, elem.get());            // 0x66d140

            // label = elem["label"] (HM7 key for *outBeginFrame).          /*0x66d220*/
            ttstr label;
            if (const auto s = std::dynamic_pointer_cast<PSB::PSBString>(
                    (*elem)[std::string("label")])) {
                label = ttstr(s->value.c_str());
            }
            // talkLabel = elem["talkLabel"] (HM7 key for *outCurrentValue). /*0x66d2a8*/
            ttstr talkLabel;
            if (const auto s = std::dynamic_pointer_cast<PSB::PSBString>(
                    (*elem)[std::string("talkLabel")])) {
                talkLabel = ttstr(s->value.c_str());
            }

            // push {ctl, label, talkLabel} onto deque#6 (binary pushes {ctl,0,0}
            //   then writes label@+8 / talkLabel@+16 into the slot; same end state).
            EmoteMouthControlEntry_Deque6 entry;
            entry.ctl       = ctl;
            entry.label     = label;
            entry.talkLabel = talkLabel;
            _compositeVarDeque6.push_back(std::move(entry));

            // TWO HM#6 VarRef inserts {type=6, index=v5} — label AND talkLabel.
            //   (0x66d30c..0x66d314 and 0x66d320..0x66d32c.)
            detail::EmoteVarRef& ref1 = _scalarHM6_1384[label];     // 0x66d30c
            ref1.type  = 6;   // *v24 = 6
            ref1.index = v5;  // v24[1] = v5
            detail::EmoteVarRef& ref2 = _scalarHM6_1384[talkLabel]; // 0x66d320
            ref2.type  = 6;   // *v25 = 6
            ref2.index = v5;  // v25[1] = v5
        }
    }

    // Aligned with libkrkr2.so EmoteEngine_buildSelectorControl @ 0x66D8FC.
    // Decompiled pseudocode (this conversation):
    //   count = Motion_propGetCount(selectorControl);                 // 0x66d990
    //   for (v6 = 0; v6 < count; ++v6) {                              // 0x66de6c
    //     elem = selectorControl[v6];                                 // PropGet(0,v6) 0x66de88
    //     label = propGet(elem, "label"); -> v80 (HM7 key + HM6 key)  // 0x66df1c/0x66df30
    //     if ((propGetBool(elem,"enabled") & 1) == 0) {               // 0x66df5c gate
    //         sub_66E248(this, &label);  // remove var binding; skip  // 0x66dfe8
    //         continue;
    //     }
    //     optionList = empty vector<Option16B>;                       // 0x66df64
    //     options = propGet(elem, "optionList");                      // 0x66df94
    //     ocount = Motion_propGetCount(options);                      // 0x66d9f0
    //     for (v13 = 0; v13 < ocount; ++v13) {                        // 0x66da20
    //       opt = options[v13];                                       // 0x66da3c
    //       optLabel = propGet(opt, "label");                         // 0x66dacc
    //       // resolve optLabel against the TRANSITION deque (a1[72..]=engine+576):
    //       refCtl = 0;                                               // 0x66db98
    //       for (e in transitionDeque) if (e.label == optLabel) {     // 0x66db0c
    //           refCtl = e.ctl; e.flag@+16 = 0; sub_66E248(this,&optLabel); break; // 0x66db80
    //       }
    //       offValue = propGetFloat(opt, "offValue", 0);              // 0x66dbbc
    //       onValue  = propGetFloat(opt, "onValue",  0);              // 0x66dbdc
    //       optionList.push_back({refCtl, offValue, onValue});        // 0x66dbf0 (16B)
    //     }
    //     v47 = operator new(0x80);                                   // 0x66dcf4
    //     EmoteSelectorController_ctor(v47, optionList);              // 0x66dd08 (swaps optionList in)
    //     push_back deque#9 {ctl=v47, label=0,...zeroed}; back().label = label; // 0x66dd10..0x66ddec
    //     ref = HM6_findOrInsert(this+1384, &back().label);          // 0x66de30 (sub_689188)
    //     ref = {type=8, index=v6};   // payload (v6<<32)|8           // 0x66de20
    //   }
    //
    // INERT boundary (documented, not a defer): the option refCtl is resolved by
    //   searching the TRANSITION controller-deque (engine+576, local
    //   `_auxVarDeque8`), a SEPARATE category whose builder (0x66D4C4) is not yet
    //   ported. That deque is therefore empty here, so the search faithfully
    //   resolves every refCtl to nullptr (the binary's own search yields v26=0
    //   when the deque is empty). The selector's own state machine + HM7 index
    //   output are fully live; only the per-option cross-controller keyframe push
    //   (guarded by `if (option.refCtl)` in applySelection) is inert until the
    //   transition category lands. Same null-guard, same skip — 1:1 with binary.
    //   The HM#6 index is the LOOP index v6 (NOT the deque size); a skipped
    //   (disabled) element still advances v6.
    void EmoteEngine::buildSelectorControl(const PSB::PSBList* selectorControl) {
        if (!selectorControl) {
            return;
        }
        const int count = static_cast<int>(selectorControl->size()); // 0x66d990
        for (int v6 = 0; v6 < count; ++v6) {                          // 0x66de6c
            const auto elem = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                (*selectorControl)[v6]);                              // PropGet(0,v6)

            // label = elem["label"] (HM7 key for the step output + HM6 key).
            //   /*0x66df1c..0x66df30*/
            ttstr label;
            if (elem) {
                if (const auto s = std::dynamic_pointer_cast<PSB::PSBString>(
                        (*elem)[std::string("label")])) {
                    label = ttstr(s->value.c_str());
                }
            }

            // enabled gate (0x66df5c). Non-enabled -> remove var binding
            //   (sub_66E248) and skip. We mirror the skip (the remove dispatch is
            //   on the variable-list TJS object, not part of this vertical).
            bool enabled = false;
            if (elem) {
                if (const auto b = std::dynamic_pointer_cast<PSB::PSBBool>(
                        (*elem)[std::string("enabled")])) {
                    enabled = b->value;
                } else if (const auto n = std::dynamic_pointer_cast<PSB::PSBNumber>(
                               (*elem)[std::string("enabled")])) {
                    enabled = n->getLongValue() != 0;
                }
            }
            if (!enabled) {
                continue;                                             // goto LABEL_82
            }

            // Assemble optionList[] from elem["optionList"].          /*0x66df94*/
            std::vector<SelectorOption16B> optionList;
            const auto options = std::dynamic_pointer_cast<PSB::PSBList>(
                (*elem)[std::string("optionList")]);
            const int ocount =
                options ? static_cast<int>(options->size()) : 0;      // 0x66d9f0
            for (int v13 = 0; v13 < ocount; ++v13) {                  // 0x66da20
                const auto opt = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                    (*options)[v13]);                                 // 0x66da3c

                // optLabel = opt["label"].                            /*0x66dacc*/
                ttstr optLabel;
                if (opt) {
                    if (const auto s = std::dynamic_pointer_cast<PSB::PSBString>(
                            (*opt)[std::string("label")])) {
                        optLabel = ttstr(s->value.c_str());
                    }
                }

                // Resolve optLabel against the TRANSITION deque (engine+576 =
                //   local _auxVarDeque8). Linear scan, compare elem.label ==
                //   optLabel; on match borrow elem.ctl, clear the matched entry's
                //   flag byte@+16 to 0, remove the var binding (sub_66E248), and
                //   break. The match is the FIRST hit. /*0x66db0c..0x66db98*/
                //   This requires buildTransitionControl to have run already (it
                //   does — PlayerCore dispatches transition before selector,
                //   mirroring applyMetadata's per-key order @0x67D4D0). When the
                //   motion declares no transitionControl the deque is empty and
                //   refCtl stays null (faithful to the binary's v26=0 no-match).
                EmoteVarController* refCtl = nullptr;                 // v26 = 0
                for (EmoteTransitionControlEntry_Deque8& tentry : _auxVarDeque8) {
                    if (tentry.label == optLabel) {                  // 0x66db0c compare
                        refCtl       = tentry.ctl;                   // v26 = e.ctl
                        tentry.flag  = 0;                            // e.flag@+16 = 0
                        // sub_66E248(this, &optLabel) removes the var binding; that
                        //   dispatch is on the variable-list TJS object (not part
                        //   of this vertical) — skipped, same as the disabled-elem
                        //   path elsewhere in this builder.
                        break;
                    }
                }

                // offValue / onValue (default 0.0).  /*0x66dbbc / 0x66dbdc*/
                float offValue = 0.0f;
                float onValue  = 0.0f;
                if (opt) {
                    if (const auto n = std::dynamic_pointer_cast<PSB::PSBNumber>(
                            (*opt)[std::string("offValue")])) {
                        offValue = n->getFloatValue();
                    }
                    if (const auto n = std::dynamic_pointer_cast<PSB::PSBNumber>(
                            (*opt)[std::string("onValue")])) {
                        onValue = n->getFloatValue();
                    }
                }

                // push_back {refCtl, offValue, onValue} (16B option).  /*0x66dbf0*/
                SelectorOption16B option;
                option.refCtl   = refCtl;
                option.offValue = offValue;
                option.onValue  = onValue;
                optionList.push_back(option);
            }

            // operator new(0x80) + ctor (raw pointer, manual lifetime — the deque
            //   entry owns the controller; dtor delete). The ctor swaps optionList
            //   into the controller and applies selection index 0.   /*0x66dcf4/0x66dd08*/
            EmoteSelectorController* ctl = new EmoteSelectorController();
            EmoteSelectorController_ctor(ctl, std::move(optionList));

            // push {ctl, label} onto deque#9 (binary pushes {ctl,0,...} then writes
            //   label@+8; same end state).  /*0x66dd10..0x66ddec*/
            EmoteSelectorControlEntry_Deque9 entry;
            entry.ctl   = ctl;
            entry.label = label;
            _vectorVarDeque9.push_back(std::move(entry));

            // HM#6 VarRef insert {type=8, index=v6} keyed by label.   /*0x66de20/0x66de30*/
            //   (binary payload = (v6 << 32) | 8 -> type=8, index=v6.)
            detail::EmoteVarRef& ref = _scalarHM6_1384[label];
            ref.type  = 8;   // payload low 32 bits
            ref.index = v6;  // payload high 32 bits
        }
    }

    // Aligned with libkrkr2.so EmoteEngine_buildTransitionControl @ 0x66D4C4.
    // Decompiled pseudocode (this conversation):
    //   count = Motion_propGetCount(transitionControl);             // 0x66d558
    //   for (v5 = 0; v5 < count; ++v5) {                            // 0x66d58c
    //     elem = transitionControl[v5];                             // PropGet(0,v5) 0x66d5a8
    //     if ((Motion_propGetBool(elem,"enabled") & 1) == 0) continue; // 0x66d62c gate -> LABEL_28
    //     v7 = operator new(0x80);                                  // 0x66d638
    //     EmoteVarController_ctor_20Bdeque(v7, 1);                  // 0x66d644 (count=1)
    //     push_back deque#8 {ctl=v7, label=0, flag@+16=1};          // 0x66d660..0x66d664
    //     label = propGet(elem, "label");                           // 0x66d724
    //     deque#8.back().label = label;                             // 0x66d770 (AddRef into +8 slot)
    //     ref = HM6_findOrInsert(this+1384, &back().label);         // 0x66d788 (a1+173)
    //     ref->type = 7; ref->index = v5;                           // 0x66d790 (*v18=7; v18[1]=v5)
    //   }
    // Structure mirrors buildEyeControl/buildSelectorControl: enabled gate, new
    //   the controller, push {ctl,label} (here with the extra flag byte@+16=1),
    //   register HM#6 {type=7, index=loopIndex}. The flag byte is written by the
    //   builder (binary: *(_BYTE*)(v8+16)=1) and read only by setVariable case7's
    //   Animator_setKeyframes gate — the progress step (sub_666BF8) ignores it.
    //   HM#6 index is the LOOP index v5 (a skipped/disabled element still
    //   advances v5). MUST run before buildSelectorControl (the selector resolves
    //   each option's refCtl by scanning THIS deque @0x66db0c).
    void EmoteEngine::buildTransitionControl(const PSB::PSBList* transitionControl) {
        if (!transitionControl) {
            return;
        }
        const int count = static_cast<int>(transitionControl->size()); // Motion_propGetCount
        for (int v5 = 0; v5 < count; ++v5) {                            // 0x66d58c
            const auto elem = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                (*transitionControl)[v5]);                              // PropGet(0,v5)

            // enabled gate (0x66d62c): skip when "enabled" is not truthy.
            bool enabled = false;
            if (elem) {
                if (const auto b = std::dynamic_pointer_cast<PSB::PSBBool>(
                        (*elem)[std::string("enabled")])) {
                    enabled = b->value;
                } else if (const auto n = std::dynamic_pointer_cast<PSB::PSBNumber>(
                               (*elem)[std::string("enabled")])) {
                    enabled = n->getLongValue() != 0;
                }
            }
            if (!enabled) {
                continue;                                               // goto LABEL_28
            }

            // operator new(0x80) + ctor count=1 (raw pointer, manual lifetime —
            //   the deque entry owns the controller; dtor delete). /*0x66d638/0x66d644*/
            EmoteVarController* ctl = new EmoteVarController();
            EmoteVarController_ctor(ctl, 1);                            // count=1

            // label = elem["label"] as ttstr (the HM7 output key). /*0x66d724*/
            ttstr label;
            if (elem) {
                if (const auto s = std::dynamic_pointer_cast<PSB::PSBString>(
                        (*elem)[std::string("label")])) {
                    label = ttstr(s->value.c_str());
                }
            }

            // push {ctl, label, flag=1} onto deque#8 (binary pushes {ctl, 0,
            //   flag@+16=1} then writes label@+8; same end state). /*0x66d660..0x66d770*/
            EmoteTransitionControlEntry_Deque8 entry;
            entry.ctl   = ctl;
            entry.label = label;
            entry.flag  = 1;   // *(_BYTE*)(v8+16) = 1
            _auxVarDeque8.push_back(std::move(entry));

            // HM#6 VarRef {type=7, index=v5} keyed by label. /*0x66d788/0x66d790*/
            detail::EmoteVarRef& ref = _scalarHM6_1384[label];
            ref.type  = 7;   // *v18 = 7
            ref.index = v5;  // v18[1] = v5
        }
    }

    // Aligned with libkrkr2.so EmoteEngine_buildLoopControl (sub_66E480) @0x66E480.
    // Decompiled pseudocode (this conversation):
    //   count = Motion_propGetCount(loopControl);                    // 0x66e514
    //   for (v6 = 0; v6 < count; ++v6) {                             // 0x66e550
    //     elem = loopControl[v6];                                    // PropGet(0,v6)
    //     if ((Motion_propGetBool(elem,"enabled") & 1) == 0) continue;// 0x66e5f0 gate -> LABEL_49
    //     transitionList = elem["transitionList"];                   // 0x66e61c
    //     kfCount = Motion_propGetCount(transitionList);             // 0x66e6a0
    //     ctl = operator new(0x20); zero;                            // 0x66e688 (+0..+31=0)
    //     ctl.keys.resize(kfCount);                                  // 0x66e6c4..0x66e6f4
    //     for (v20 = 0; v20 < kfCount; ++v20) {                      // 0x66e810 do/while
    //       kf = transitionList[v20];                                // 0x66e728
    //       ctl.keys[v20].v0   = (float)propGetIndexDouble(kf,0);    // 0x66e7a8 STR S
    //       ctl.keys[v20].v1   = (float)propGetIndexDouble(kf,1);    // 0x66e7c8 STR S
    //       ctl.keys[v20].span = (float)propGetIndexDouble(kf,2);    // 0x66e7e4 STR S
    //     }
    //     push_back deque#10 {ctl, label=0};                         // 0x66e828 (16B elem)
    //     label = elem["var_loop"];                                  // 0x66e8b4 (ttstr value)
    //     deque#10.back().label = label;                             // 0x66e944
    //     ref = HM6_findOrInsert(engine+1384, &back().label);        // 0x66e964 (a1+173)
    //     ref->type = 3; ref->index = v6;                            // 0x66e96c
    //   }
    // Structure mirrors buildTransitionControl/buildSelectorControl: enabled gate,
    //   new the controller, fill its keyframe vector, push {ctl,label} (16B),
    //   register HM#6 {type=3, index=loopIndex}. The HM#6 index is the LOOP index
    //   v6 (a skipped/disabled element still advances v6). The element label AND
    //   the HM#6 key are BOTH the "var_loop" value (binary: sub_A0BAF4 @0x66e90c
    //   produces the ttstr stored at elem+8 @0x66e944 and used as the HM6 key
    //   @0x66e964). The step for this deque is INLINED into progress (no separate
    //   step fn) — see EmoteLoopController_step / progress @0x67d2a0.
    //
    // FLOAT-BITS: each keyframe field is stored via `STR S` (single-precision)
    //   after propGetDouble narrows double->single — i.e. raw float bits, no
    //   integer remap. getFloatValue() is the local equivalent (NOT a (float)(int)
    //   cast). The 12B keyframe POD {v0,v1,span} is a platform-independent data
    //   contract per the byte-layout methodology.
    void EmoteEngine::buildLoopControl(const PSB::PSBList* loopControl) {
        if (!loopControl) {
            return;
        }
        const int count = static_cast<int>(loopControl->size());     // Motion_propGetCount
        for (int v6 = 0; v6 < count; ++v6) {                         // 0x66e550
            const auto elem = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                (*loopControl)[v6]);                                 // PropGet(0,v6)

            // enabled gate (0x66e5f0): skip when "enabled" is not truthy. The
            //   skipped element still advances v6 (used as the HM#6 index).
            bool enabled = false;
            if (elem) {
                if (const auto b = std::dynamic_pointer_cast<PSB::PSBBool>(
                        (*elem)[std::string("enabled")])) {
                    enabled = b->value;
                } else if (const auto n = std::dynamic_pointer_cast<PSB::PSBNumber>(
                               (*elem)[std::string("enabled")])) {
                    enabled = n->getLongValue() != 0;
                }
            }
            if (!enabled) {
                continue;                                            // goto LABEL_49
            }

            // transitionList = elem["transitionList"] (the keyframe triples). /*0x66e61c*/
            const auto transitionList = elem
                ? std::dynamic_pointer_cast<PSB::PSBList>(
                      (*elem)[std::string("transitionList")])
                : nullptr;
            const int kfCount = transitionList
                ? static_cast<int>(transitionList->size())           // Motion_propGetCount /*0x66e6a0*/
                : 0;

            // operator new(0x20) + zero-init (raw pointer, manual lifetime — the
            //   deque entry owns the controller; dtor delete). /*0x66e688*/
            EmoteLoopController* ctl = new EmoteLoopController();
            ctl->keys.resize(static_cast<size_t>(kfCount));          // /*0x66e6c4..0x66e6f4*/

            // Fill each 12B keyframe {v0, v1, span} from the triple. Each field is
            //   propGetIndexDouble -> stored as float (STR S = raw float bits). /*0x66e810*/
            for (int v20 = 0; v20 < kfCount; ++v20) {
                const auto kf = std::dynamic_pointer_cast<PSB::PSBList>(
                    (*transitionList)[v20]);                         // PropGet(0,v20) /*0x66e728*/
                EmoteLoopKeyframe12B& dst = ctl->keys[static_cast<size_t>(v20)];
                if (kf && kf->size() >= 3) {
                    if (const auto n0 = std::dynamic_pointer_cast<PSB::PSBNumber>(
                            (*kf)[0])) {
                        dst.v0 = n0->getFloatValue();                // /*0x66e7a8 STR S*/
                    }
                    if (const auto n1 = std::dynamic_pointer_cast<PSB::PSBNumber>(
                            (*kf)[1])) {
                        dst.v1 = n1->getFloatValue();                // /*0x66e7c8 STR S*/
                    }
                    if (const auto n2 = std::dynamic_pointer_cast<PSB::PSBNumber>(
                            (*kf)[2])) {
                        dst.span = n2->getFloatValue();              // /*0x66e7e4 STR S*/
                    }
                }
            }

            // label = elem["var_loop"] as ttstr — the deque element label AND the
            //   HM#7 output key AND the HM#6 key. /*0x66e8b4 -> 0x66e90c*/
            ttstr label;
            if (elem) {
                if (const auto s = std::dynamic_pointer_cast<PSB::PSBString>(
                        (*elem)[std::string("var_loop")])) {
                    label = ttstr(s->value.c_str());
                }
            }

            // push {ctl, label} onto deque#10 (binary pushes {ctl, 0} then writes
            //   label@+8; same end state). /*0x66e828 -> 0x66e944*/
            EmoteLoopControlEntry_Deque10 entry;
            entry.ctl   = ctl;
            entry.label = label;
            _lookupCurvesDeque10.push_back(std::move(entry));

            // HM#6 VarRef {type=3, index=v6} keyed by the var_loop label. /*0x66e964/0x66e96c*/
            detail::EmoteVarRef& ref = _scalarHM6_1384[label];
            ref.type  = 3;   // *v37 = 3
            ref.index = v6;  // v37[1] = v6
        }
    }

    // ------------------------------------------------------------------------
    // Spring-physics deque builders (population path). File-local readers mirror
    // the Motion property accessors: propGetDouble -> double -> (float) raw bits,
    // propGetIndexDouble for list elements, sub_66B83C reads a dict's x/y/z.
    // ------------------------------------------------------------------------
    namespace {

        // narrow double->float (FCVT), like the binary's `*(float*)&v=*(double*)&v`.
        float springPropFloat(const PSB::PSBDictionary* d, const char* key) {
            if (!d) return 0.0f;
            const auto v = (*d)[std::string(key)];
            if (const auto n = std::dynamic_pointer_cast<PSB::PSBNumber>(v)) {
                switch (n->numberType) {
                    case PSB::PSBNumberType::Float:  return static_cast<float>(n->getFloatValue());
                    case PSB::PSBNumberType::Double: return static_cast<float>(n->getValue<double>());
                    default: return static_cast<float>(n->getLongValue());
                }
            }
            return 0.0f;
        }

        int springPropInt(const PSB::PSBDictionary* d, const char* key) {
            if (!d) return 0;
            const auto v = (*d)[std::string(key)];
            if (const auto n = std::dynamic_pointer_cast<PSB::PSBNumber>(v)) {
                return static_cast<int>(n->getLongValue());
            }
            return 0;
        }

        // These return shared_ptr by value; callers keep them alive for the scope
        //   in which they read fields (the PSB child is owned by its parent too,
        //   but returning the handle keeps lifetime obvious and leak-free).
        std::shared_ptr<PSB::PSBDictionary> springDict(
            const PSB::PSBDictionary* d, const char* key) {
            if (!d) return nullptr;
            return std::dynamic_pointer_cast<PSB::PSBDictionary>(
                (*d)[std::string(key)]);
        }

        std::shared_ptr<PSB::PSBList> springList(
            const PSB::PSBDictionary* d, const char* key) {
            if (!d) return nullptr;
            return std::dynamic_pointer_cast<PSB::PSBList>((*d)[std::string(key)]);
        }

        std::shared_ptr<PSB::PSBDictionary> springListDict(
            const PSB::PSBList* l, int idx) {
            if (!l || idx < 0 || idx >= static_cast<int>(l->size())) return nullptr;
            return std::dynamic_pointer_cast<PSB::PSBDictionary>((*l)[idx]);
        }

        // sub_66B83C(dict): reads x/y/z doubles, narrows each to float. The
        //   decompiler "returns x" but the caller stores all three components
        //   (the leftover y/z FP regs); we write all three into out[0..2].
        void springVec3(const PSB::PSBDictionary* d, float out[3]) {
            out[0] = springPropFloat(d, "x");
            out[1] = springPropFloat(d, "y");
            out[2] = springPropFloat(d, "z");
        }

        // enabled gate shared by both builders (PSBBool or PSBNumber truthiness).
        bool springEnabled(const PSB::PSBDictionary* elem) {
            if (!elem) return false;
            const auto v = (*elem)[std::string("enabled")];
            if (const auto b = std::dynamic_pointer_cast<PSB::PSBBool>(v)) {
                return b->value;
            }
            if (const auto n = std::dynamic_pointer_cast<PSB::PSBNumber>(v)) {
                return n->getLongValue() != 0;
            }
            return false;
        }

        ttstr springLabel(const PSB::PSBDictionary* d, const char* key) {
            ttstr label;
            if (!d) return label;
            if (const auto s = std::dynamic_pointer_cast<PSB::PSBString>(
                    (*d)[std::string(key)])) {
                label = ttstr(s->value.c_str());
            }
            return label;
        }

    } // namespace

    // Aligned with libkrkr2.so sub_66B018 @ 0x66B018 ("bustControl" -> deque#1,
    //   the SIMPLE spring consumed by stepHairParts).
    // Decompiled pseudocode (this conversation):
    //   count = Motion_propGetCount(bustControl);                  // 0x66b0ac
    //   for (v5 = 0; v5 < count; ++v5) {                           // 0x66b0e0
    //     elem = bustControl[v5];                                  // PropGet(0,v5)
    //     if ((propGetBool(elem,"enabled") & 1) == 0) continue;    // 0x66b180 gate
    //     param = elem["param"];                                   // 0x66b1ac
    //     spring = operator new(0x48); EmoteSpringState_ctor(spring, elem); // 0x66b220
    //     op = param["op"];  spring[+36/+40/+44] = vec3(op);       // 0x66b280
    //     p  = param["p"];   spring[+48/+52/+56] = vec3(p);        // 0x66b2dc
    //     pv = param["pv"];  spring[+60/+64/+68] = vec3(pv);       // 0x66b338
    //     spring[+16] = (float)propGetDouble(param,"ofs");         // 0x66b368
    //     node = deque#1.emplace(); node.spring=spring; node.initFlag=1;       // 0x66b388
    //     node.shapeLabel = elem["baseLayer"];                     // 0x66b498 (+12)
    //     node.keyX = elem["var_lr"];                              // 0x66b530 (+20)
    //     node.keyY = elem["var_ud"];                              // 0x66b5b8 (+28)
    //     HM6[var_lr] = {type=0, index=v5};                        // 0x66b5d4
    //     HM6[var_ud] = {type=0, index=v5};                        // 0x66b5e8
    //   }
    // The ctor's vec3 fields (storedXYZ/posXYZ/velXYZ) are seeded to 0 then
    //   OVERWRITTEN here by op/p/pv (the binary writes after the ctor returns).
    void EmoteEngine::buildBustControl(const PSB::PSBList* bustControl) {
        if (!bustControl) {
            return;
        }
        const int count = static_cast<int>(bustControl->size()); // Motion_propGetCount
        for (int v5 = 0; v5 < count; ++v5) {                     // 0x66b0e0
            const auto elem = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                (*bustControl)[v5]);                             // PropGet(0,v5)
            if (!springEnabled(elem.get())) {
                continue;                                        // 0x66b180 gate
            }

            const std::shared_ptr<PSB::PSBDictionary> param =
                springDict(elem.get(), "param"); // 0x66b1ac

            // operator new(0x48) + ctor over the ELEMENT dict (raw ptr, deque owns).
            EmoteSpringState* spring = new EmoteSpringState();   // 0x66b220
            EmoteSpringState_ctor(spring, elem.get());           // 0x66b22c

            // op/p/pv vec3 (dict x/y/z) overwrite stored/pos/vel.  /*0x66b280..0x66b338*/
            float v3[3];
            springVec3(springDict(param.get(), "op").get(), v3);
            spring->storedX = v3[0]; spring->storedY = v3[1]; spring->storedZ = v3[2]; // +36/+40/+44
            springVec3(springDict(param.get(), "p").get(), v3);
            spring->posX = v3[0]; spring->posY = v3[1]; spring->posZ = v3[2];          // +48/+52/+56
            springVec3(springDict(param.get(), "pv").get(), v3);
            spring->velX = v3[0]; spring->velY = v3[1]; spring->accZ = v3[2];          // +60/+64/+68
            spring->biasY = springPropFloat(param.get(), "ofs");                      // +16  0x66b368

            // push node onto deque#1; initFlag = 1 (binary 0x66b38c).
            EmoteHairPartsNode48B node;
            node.spring     = spring;
            node.initFlag   = 1;                                  // *(v19+8)=1
            node.shapeLabel = springLabel(elem.get(), "baseLayer"); // +12  0x66b498
            node.keyX       = springLabel(elem.get(), "var_lr");    // +20  0x66b530
            node.keyY       = springLabel(elem.get(), "var_ud");    // +28  0x66b5b8
            _hairPartsNodes.push_back(std::move(node));

            // HM#6 VarRef {type=0, index=v5} keyed by var_lr AND var_ud.
            detail::EmoteVarRef& refLr = _scalarHM6_1384[springLabel(elem.get(), "var_lr")];
            refLr.type = 0; refLr.index = v5;                     // 0x66b5d4
            detail::EmoteVarRef& refUd = _scalarHM6_1384[springLabel(elem.get(), "var_ud")];
            refUd.type = 0; refUd.index = v5;                     // 0x66b5e8
        }
    }

    // Aligned with libkrkr2.so sub_66B9D0 @ 0x66B9D0 ("hairControl"/"partsControl"
    //   -> deque#2/#3, the CHAIN spring consumed by stepBust). typeTag = 1 (hair)
    //   or 2 (parts), written into the HM#6 VarRef type.
    // Decompiled pseudocode (this conversation):
    //   count = Motion_propGetCount(chainControl);                 // 0x66ba70
    //   for (v10 = 0; v10 < count; ++v10) {                        // 0x66ba9c
    //     elem = chainControl[v10];                                // PropGet(0,v10)
    //     if ((propGetBool(elem,"enabled") & 1) == 0) continue;    // 0x66bb3c gate
    //     param = elem["param"];                                   // 0x66bb6c
    //     spring = operator new(0xB0); EmoteBustChainSpring_ctor(spring, elem);// 0x66bbdc
    //     op = param["op"];  spring[+80/+84/+88] = vec3(op);       // 0x66bc3c (root)
    //     spring[+44]=(float)propGetDouble(param,"ofs");           // 0x66bc6c
    //     spring[+48]=(float)propGetDouble(param,"bendR");         // 0x66bc94
    //     spring[+52]=(float)propGetDouble(param,"bendS");         // 0x66bcc0
    //     bp = elem["bp"]; (list[2])                               // 0x66bcec ("bp")
    //     p  = param["p"]; (list[2])                               // 0x66bd70
    //     pv = param["pv"];(list[2])                               // 0x66bdf4
    //     spring[+92..] = vec3(p[0]); spring[+104..] = vec3(p[1]); // 0x66be94/0x66bee4
    //     spring[+116..]= vec3(pv[0]);spring[+128..] = vec3(pv[1]);// 0x66bf34/0x66bf84
    //     spring[+140..]= vec3(bp[0]);spring[+152..] = vec3(bp[1]);// 0x66bfd4/0x66c024
    //     node = deque.emplace(); node.spring=spring;              // 0x66c04c (node+8 NOT set)
    //     node.shapeLabel=elem["baseLayer"]; node.keyA=elem["var_lr"];     // +12/+20
    //     node.keyB=elem["var_lrm"]; node.keyC=elem["var_ud"];     // +28/+36
    //     HM6[var_lr]=HM6[var_lrm]=HM6[var_ud]={type=tag, index=v10};      // 0x66c310..
    //   }
    void EmoteEngine::buildChainControl(
        std::deque<EmoteBustChain1Node56B>& chainNodes, int typeTag,
        const PSB::PSBList* chainControl) {
        if (!chainControl) {
            return;
        }
        const int count = static_cast<int>(chainControl->size()); // Motion_propGetCount
        for (int v10 = 0; v10 < count; ++v10) {                   // 0x66ba9c
            const auto elem = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                (*chainControl)[v10]);                            // PropGet(0,v10)
            if (!springEnabled(elem.get())) {
                continue;                                         // 0x66bb3c gate
            }

            const std::shared_ptr<PSB::PSBDictionary> param =
                springDict(elem.get(), "param"); // 0x66bb6c

            // operator new(0xB0) + chain ctor over the ELEMENT dict.
            EmoteBustChainSpring* spring = new EmoteBustChainSpring(); // 0x66bbdc
            EmoteBustChainSpring_ctor(spring, elem.get());            // 0x66bbe8

            uint8_t* const sp = reinterpret_cast<uint8_t*>(spring);
            auto SF = [sp](int off) -> float& { return *reinterpret_cast<float*>(sp + off); };

            // op = param["op"] (dict) -> root @+80/+84/+88.        /*0x66bc3c*/
            float v3[3];
            springVec3(springDict(param.get(), "op").get(), v3);
            SF(80) = v3[0]; SF(84) = v3[1]; SF(88) = v3[2];
            // ofs/bendR/bendS (param doubles) -> +44/+48/+52.       /*0x66bc6c..*/
            SF(44) = springPropFloat(param.get(), "ofs");
            SF(48) = springPropFloat(param.get(), "bendR");
            SF(52) = springPropFloat(param.get(), "bendS");

            // p / pv are 2-element lists under "param"; bp is a 2-element list
            //   directly under the element. Each entry is a dict x/y/z.
            const std::shared_ptr<PSB::PSBList> p  = springList(param.get(), "p");  // 0x66bd70
            const std::shared_ptr<PSB::PSBList> pv = springList(param.get(), "pv"); // 0x66bdf4
            const std::shared_ptr<PSB::PSBList> bp = springList(elem.get(), "bp");  // 0x66bcec
            springVec3(springListDict(p.get(),  0).get(), v3); SF(92)  = v3[0]; SF(96)  = v3[1]; SF(100) = v3[2];
            springVec3(springListDict(p.get(),  1).get(), v3); SF(104) = v3[0]; SF(108) = v3[1]; SF(112) = v3[2];
            springVec3(springListDict(pv.get(), 0).get(), v3); SF(116) = v3[0]; SF(120) = v3[1]; SF(124) = v3[2];
            springVec3(springListDict(pv.get(), 1).get(), v3); SF(128) = v3[0]; SF(132) = v3[1]; SF(136) = v3[2];
            springVec3(springListDict(bp.get(), 0).get(), v3); SF(140) = v3[0]; SF(144) = v3[1]; SF(148) = v3[2];
            springVec3(springListDict(bp.get(), 1).get(), v3); SF(152) = v3[0]; SF(156) = v3[1]; SF(160) = v3[2];

            // push node onto the chain deque. The binary does NOT write node+8
            //   (init byte): the deque block is raw operator-new, so it is
            //   indeterminate in libkrkr2.so. The local POD node value-inits it to
            //   0 (initFlag=0) deterministically — a documented divergence on an
            //   otherwise-untouched byte.
            EmoteBustChain1Node56B node;
            node.spring     = spring;
            node.shapeLabel = springLabel(elem.get(), "baseLayer"); // +12  0x66c150
            node.keyA       = springLabel(elem.get(), "var_lr");    // +20  0x66c1e8
            node.keyB       = springLabel(elem.get(), "var_lrm");   // +28  0x66c270
            node.keyC       = springLabel(elem.get(), "var_ud");    // +36  0x66c2f8
            chainNodes.push_back(std::move(node));

            // HM#6 VarRef {type=typeTag, index=v10} keyed by all three vars.
            detail::EmoteVarRef& refLr = _scalarHM6_1384[springLabel(elem.get(), "var_lr")];
            refLr.type = typeTag; refLr.index = v10;                // 0x66c318
            detail::EmoteVarRef& refLrm = _scalarHM6_1384[springLabel(elem.get(), "var_lrm")];
            refLrm.type = typeTag; refLrm.index = v10;              // 0x66c338
            detail::EmoteVarRef& refUd = _scalarHM6_1384[springLabel(elem.get(), "var_ud")];
            refUd.type = typeTag; refUd.index = v10;                // 0x66c354
        }
    }

    // ========================================================================
    // setVariable value-dispatch (libkrkr2.so Player_setVariable @0x671228) and
    // the 5 per-category enqueue functions it calls. Each enqueue pushes a
    // transition keyframe {value, easing, factor} into the controller's internal
    // keyframe std::deque (the libstdc++ deque the binary indexes at a1+16..+72),
    // or — on the instant path (easing <= 0) — clears the deque and snaps the
    // controller's scalar state. Element fields are the controllers' named
    // keyframe types (EmoteAngleKeyValue12B / EmoteVarKeyValue20B); the binary's
    // raw float-triple {a3,a4,a5} maps to {value, duration(=easing arg), powCount
    // (=factor arg)} stored as RAW FLOAT BITS (the step reads powCount with
    // `LDR S, no SCVTF`).
    // ========================================================================
    namespace {

        // v22 transition factor (0x671304..0x671328). durationFrames is the 3rd
        //   binary arg (TJS "ease"); == variableEaseWeightLike_0x671228.
        float emoteTransitionFactorLike_0x671228(double durationFrames) {
            if (durationFrames == 0.0) {
                return 1.0f;                                       // 0x671308
            }
            if (durationFrames > 0.0) {
                return static_cast<float>(durationFrames + 1.0);   // 0x671318
            }
            return static_cast<float>(1.0 / (1.0 - durationFrames)); // 0x671328
        }

        // Aligned with libkrkr2.so sub_6638B0 (case 4, eye enqueue).
        //   if (easing <= 0): clear BOTH value-track deques (a1+16 / a1+96), then
        //     trackValue(+300)=value, trackState(+296)=0.
        //   else: if (flag&1) append to valueTrack12B.queue; else clear it (and
        //     reset trackState(+296)=0) then append. Element {value, easing, factor}.
        void emoteEnqueueEye_sub_6638B0(EmoteBlinkController* ctl, bool flag,
                                        float value, float easing, float factor) {
            if (easing <= 0.0f) {                                  // 0x6638e8
                ctl->valueTrack12B.queue.clear();                  // a1+16 swap-clear
                ctl->valueTrack8B.clear();                         // a1+96 swap-clear
                ctl->trackValue = value;                           // 0x663978  (+300)
                ctl->trackState = 0;                               // 0x66397c  (+296)
                return;
            }
            if (!flag) {                                           // 0x6638ec else
                ctl->valueTrack12B.queue.clear();                  // a1+16 swap-clear
                ctl->trackState = 0;                               // 0x663a00  (+296)
            }
            // push {endRad=value, duration=easing, powCount=factor(raw bits)}.
            EmoteAngleKeyValue12B kf;                              // 0x663a50 new block
            kf.endRad   = value;                                   // *v39
            kf.duration = easing;                                  // v39[1]
            std::memcpy(&kf.powCount, &factor, sizeof(float));     // v39[2] raw bits
            ctl->valueTrack12B.queue.push_back(kf);
        }

        // Aligned with libkrkr2.so sub_6652D4 (case 5, eyebrow enqueue).
        //   STRUCTURALLY IDENTICAL to sub_6638B0 but on the slim eyebrow
        //   controller (same +296/+300 track-state offsets, two value-track
        //   deques cleared on the instant path).
        void emoteEnqueueEyebrow_sub_6652D4(EmoteEyebrowController* ctl, bool flag,
                                            float value, float easing,
                                            float factor) {
            if (easing <= 0.0f) {                                  // 0x66530c
                ctl->valueTrack12B.queue.clear();                  // a1+16
                ctl->valueTrack8B.clear();                         // a1+96
                ctl->trackValue = value;                           // 0x66539c  (+300)
                ctl->trackState = 0;                               // 0x6653a0  (+296)
                return;
            }
            if (!flag) {                                           // 0x665310 else
                ctl->valueTrack12B.queue.clear();
                ctl->trackState = 0;                               // 0x665424  (+296)
            }
            EmoteAngleKeyValue12B kf;                              // 0x665474 new block
            kf.endRad   = value;
            kf.duration = easing;
            std::memcpy(&kf.powCount, &factor, sizeof(float));
            ctl->valueTrack12B.queue.push_back(kf);
        }

        // Aligned with libkrkr2.so sub_665E34 (case 6, mouth talk-ramp enqueue).
        //   SINGLE value-track deque (a1+16 only). Instant path: clear queue,
        //   currentValue(+84)=value, state(+80)=0. Push path mirrors the eye one
        //   but writes state(+80) (not +296) when clearing.
        void emoteEnqueueMouth_sub_665E34(EmoteMouthController* ctl, bool flag,
                                          float value, float easing, float factor) {
            if (easing <= 0.0f) {                                  // 0x665e68 false
                ctl->valueTrack12B.queue.clear();                  // a1+16
                ctl->currentValue = value;                         // 0x665ec8  (+84)
                ctl->state = 0;                                    // 0x665ecc  (+80)
                return;
            }
            if (!flag) {                                           // 0x665e6c else
                ctl->valueTrack12B.queue.clear();
                ctl->state = 0;                                    // 0x665f0c  (+80)
            }
            EmoteAngleKeyValue12B kf;                              // 0x665f68 new block
            kf.endRad   = value;
            kf.duration = easing;
            std::memcpy(&kf.powCount, &factor, sizeof(float));
            ctl->valueTrack12B.queue.push_back(kf);
        }

        // Aligned with libkrkr2.so Animator_setKeyframes @0x667300 (case 7,
        //   transition controller). The "keyframe" pushed carries `count` float
        //   channels (count=1 for transition controllers). value is a single
        //   float (the binary passes &(float)value). Instant path: clear queue,
        //   state(+84)=0, copy `count` floats from value into currentValue(+88).
        void emoteAnimatorSetKeyframes_0x667300(EmoteVarController* ctl, bool flag,
                                                float value, float easing,
                                                float factor) {
            if (easing <= 0.0f) {                                  // 0x667340
                ctl->queue.clear();                                // a1+16
                ctl->state = 0;                                    // 0x6673cc  (+84)
                // copy count floats from the value-array into currentValue.
                //   (0x6673d4..0x66745c: memcpy count ints.) Here value is a
                //   single scalar broadcast to channel 0 (count==1 case).
                if (ctl->count >= 1 && ctl->currentValue) {
                    for (int i = 0; i < ctl->count; ++i) {
                        ctl->currentValue[i] = value;              // count==1 -> [0]
                    }
                }
                return;
            }
            if (!flag) {                                           // 0x667344 == 0
                ctl->queue.clear();
                ctl->state = 0;                                    // 0x667378  (+84)
            }
            // push a 20B keyframe {channel[0]=value, duration=easing, powCount=
            //   factor(raw bits)} (EmoteVarController_deque20B_pushback 0x667390).
            EmoteVarKeyValue20B kf;
            kf.channel[0] = value;
            kf.channel[1] = 0.0f;
            kf.channel[2] = 0.0f;
            kf.duration   = easing;
            std::memcpy(&kf.powCount, &factor, sizeof(float));     // raw bits
            ctl->queue.push_back(kf);
        }

        // Aligned with libkrkr2.so sub_6681E4 (case 8, selector command enqueue).
        //   SINGLE command-track deque (a1+16, the base 12B track). Instant path:
        //   clear queue, selState(+84)=0, then applySelection(ctl, (int)value,
        //   0, 0). Push path appends {selIdx=value, dur=easing, fade=factor}.
        void emoteEnqueueSelector_sub_6681E4(EmoteSelectorController* ctl, bool flag,
                                             float value, float easing,
                                             float factor) {
            if (easing <= 0.0f) {                                  // 0x668218 false
                ctl->commandTrack12B.queue.clear();                // a1+16
                ctl->selState = 0;                                 // 0x668274  (+84)
                EmoteSelectorController_applySelection(
                    ctl, static_cast<int>(value), 0.0f, 0.0f);     // 0x6682a4
                return;
            }
            if (!flag) {                                           // 0x66821c else
                ctl->commandTrack12B.queue.clear();
                ctl->selState = 0;                                 // 0x6682e0  (+84)
            }
            EmoteAngleKeyValue12B kf;                              // 0x66833c new block
            kf.endRad   = value;                                   // selIdx
            kf.duration = easing;                                  // dur
            std::memcpy(&kf.powCount, &factor, sizeof(float));     // fade raw bits
            ctl->commandTrack12B.queue.push_back(kf);
        }

    } // namespace

    // Aligned with libkrkr2.so Player_setVariable @ 0x671228 (see header for the
    //   arg-name mapping and the full step list). `this` IS the EmoteEngine.
    void EmoteEngine::setVariable(const ttstr& key, double value, double easing,
                                  double durationFrames) {
        // HM6 lookup (sub_6887F4 @0x6712f0). Empty key hashes to 0; the binary
        //   still performs the lookup. A miss => no entry => HM2 fallthrough.
        auto it = _scalarHM6_1384.find(key);                       // 0x6712f0
        if (it != _scalarHM6_1384.end()) {                         // result != 0  0x6712f4
            const detail::EmoteVarRef& ref = it->second;           // *(QWORD*)result  0x6712f8
            // v22 transition factor (0x671304..0x671328).
            const float factor = emoteTransitionFactorLike_0x671228(durationFrames);
            const float vEasing = static_cast<float>(easing);
            const float vValue  = static_cast<float>(value);
            const bool flag = _emoteAnimatorFlag;                  // *(BYTE*)(this+1161)

            _dirty = true;                                         // 0x671330  (+1162)

            switch (ref.type) {                                    // *(int*)(varref+16)  0x671350
                case 0:
                case 1:
                case 2:
                    // 0x671354: cases 0/1/2 fall through to the HM2 scalar write
                    //   ONLY when _syncWaiting(+1159) is set (`if(this+1159) break;
                    //   else return`). Otherwise they leave the value un-written
                    //   here (the spring target/const feed is a SEPARATE pass).
                    if (_syncWaiting) {                            // 0x671354
                        break;                                     // -> HM2 write
                    }
                    return;                                        // 0x671358
                case 4: {
                    // deque#4[ref.index] -> enqueue eye (sub_6638B0).  0x67139c
                    EmoteEyeControlEntry_Deque4& entry =
                        _stateMachineDeque4[ref.index];
                    emoteEnqueueEye_sub_6638B0(entry.ctl, flag, vValue,
                                               vEasing, factor); // 0x67155c
                    return;
                }
                case 5: {
                    // deque#5[ref.index] -> enqueue eyebrow (sub_6652D4). 0x6713c4
                    EmoteEyebrowControlEntry_Deque5& entry =
                        _stateMachineDeque5[ref.index];
                    emoteEnqueueEyebrow_sub_6652D4(entry.ctl, flag, vValue,
                                                   vEasing, factor); // 0x671588
                    return;
                }
                case 6: {
                    // deque#6[ref.index]. Dual-key controller: if `key` equals the
                    //   element's "label" -> write ctl->beginFrame(+108)=(int)value
                    //   directly (LABEL_68 @0x6716a8). If `key` equals the
                    //   element's "talkLabel" -> enqueue the talk ramp (sub_665E34
                    //   @0x6716a0). (Binary compares pointer-eq then wcscmp.)
                    EmoteMouthControlEntry_Deque6& entry =
                        _compositeVarDeque6[ref.index];           // 0x6713ec
                    if (entry.label == key) {                      // 0x6715d0 / LABEL_68
                        entry.ctl->beginFrame = static_cast<int>(value); // 0x6716a8 (+108)
                        return;
                    }
                    if (entry.talkLabel == key) {                  // 0x671688
                        emoteEnqueueMouth_sub_665E34(entry.ctl, flag, vValue,
                                                     vEasing, factor); // 0x6716a0
                    }
                    return;
                }
                case 7: {
                    // deque#8[ref.index] (transition). The element flag byte@+16
                    //   gates the enqueue (`if(!*(BYTE*)(v38+16)) return`).  0x671424
                    EmoteTransitionControlEntry_Deque8& entry =
                        _auxVarDeque8[ref.index];
                    if (!entry.flag) {                             // 0x67145c / 0x6716f0
                        return;
                    }
                    emoteAnimatorSetKeyframes_0x667300(entry.ctl, flag, vValue,
                                                       vEasing, factor); // 0x671710
                    return;
                }
                case 8: {
                    // deque#9[ref.index] (selector). Element flag byte@+16 gates
                    //   the enqueue (`if(!*(BYTE*)(v42+16)) return`).  0x671468
                    EmoteSelectorControlEntry_Deque9& entry =
                        _vectorVarDeque9[ref.index];
                    // case8 enqueue gate (`LDRB [elem+16]; CBNZ` @0x6714a0 /
                    //   0x671740). The builder leaves elem+16 un-initialised in
                    //   the binary (only +0/+8/+24/+32/+40 written); modelled as
                    //   entry.flag (default 1) — see the EmoteSelectorControlEntry
                    //   _Deque9 +16 note for the indeterminacy rationale.
                    if (!entry.flag) {                             // 0x6714a0 / 0x671740
                        return;
                    }
                    emoteEnqueueSelector_sub_6681E4(entry.ctl, flag, vValue,
                                                    vEasing, factor); // 0x671758
                    return;
                }
                default:
                    return;                                        // 0x671350 default
            }
        }

        // HM2 upsert fallthrough (0x67135c..0x671368): reached on HM6 miss, or a
        //   case 0/1/2 with _syncWaiting set. `*(double*)result = value`.
        _labelToValueHM7[key] = value;                             // 0x671368
    }

    // Aligned with libkrkr2.so sub_67D01C EmoteEngine_progress @ 0x67D01C.
    //
    // Binary main loop (from EmoteEngine_controllers.md):
    //
    //   EmoteEngine_preProgress_guess(this, false, dt); // 0x671764
    //   while (dt > 0 || _dirty@1162):
    //       step = fmin(dt, 1.1)           // physics step cap
    //       _dirty@1162 = false
    //       for each elem in deque#4..deque#10 (skip #7 pool): step_fn(elem, step)
    //       applyVarControllers_pos_scale_color_angle(step)
    //       if (player@1128 && player+1544 flag) sub_6687E8(step)
    //       dt -= step
    //   // post-loop (G2-C bind-loop, LIVE — bridges HM7 -> Player HM1/HM2):
    //   for (entry = HM7@+1456; entry; entry = entry->next):
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
    // Physics step functions (stepHairParts @0x67B748, stepBust @0x67BCE8) are
    // fully ported and their deques are now POPULATED by buildBustControl
    // (deque#1) / buildChainControl (deque#2/#3) from applyMetadata. They run on
    // real spring nodes; the only remaining un-wired inputs are the controller
    // TARGETS (_ctlHairPartsTarget/_ctlBust1/2Target @+1104/+1112/+1120) and the
    // spring CONSTANTS (_bustSpring1/2Const @+1184/+1192), set by the variableList/
    // setVariable resolution path (still open) — until then cur[]/springConst = 0.
    void EmoteEngine::preProgressLike_0x671764(bool force, double dt) {
        // EmoteEngine_preProgress_guess @0x671764 entry gate:
        //   if (dt != 0.0 || (force & 1) != 0) { ... }
        if(dt == 0.0 && !force) {
            return;
        }

        // The binary stores the timeline hashmap at EmoteEngine+936 and the
        // playing tTJSVariant* vector at +1040. Those typed containers exist on
        // EmoteEngine locally but their population path is not yet connected;
        // the live port timeline state is still hosted by the embedded Player.
        // Keep that pre-existing storage model behind the correctly-owned
        // EmoteEngine call boundary. This is an explicit remaining architecture
        // gap, not a platform boundary and not a behavioral guard.
        player().preProgressTimelineStateModelForEmoteEngine(dt, nullptr);
    }

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

        // 0x67D050 W1=WZR; 0x67D054 preserves the original V0 dt; X0 remains
        // this EmoteEngine through BL @0x67D060. Must run exactly once before
        // the dt-slice loop, never from Player_progress_inner @0x6C106C.
        preProgressLike_0x671764(false, originalDt); // BL 0x67D060

        // dt-slice main loop with physics step cap = 1.1f.
        // Binary: `if (dt>0) goto LABEL_6` enters the do-while body; the inner
        // `do{...; dt-=step;}while(dt>0)` drains dt; the outer `while(_dirty)`
        // re-runs while the dirty flag is still set. `while(dt>0 || _dirty)`
        // is the faithful flattening (first iteration guaranteed when dt>0;
        // subsequent iterations gated by remaining dt or the dirty flag).
        while (dt > 0.0f || _dirty) {
            const float step = std::fmin(dt, 1.1f);
            _dirty = false;

            // 6 active deques iterated by step functions (per binary
            //   EmoteEngine_progress @0x67D01C). Each is a deque OF POINTERS:
            //   element = { EmoteVarController* ctl; ttstr key } (e.g. #4 elem
            //   16B = ptr@0 + ttstr@8; #6/#8 24B add a 2nd/3rd ttstr key). The
            //   loop calls the per-controller step then upserts the result into
            //   HM7 keyed by elem's ttstr:
            //     #4 sub_663BDC, #5 sub_665600, #6 sub_666068, #8 EmoteVarController_step,
            //     #9 sub_668470, #10 inline curve lookup.
            // POPULATION (corrected 2026-06-03, was wrongly "setVariable fills
            //   these"): setVariable @0x671228 does NOT push elements — it hash-
            //   looks-up an EXISTING HM@+1384 entry, reads its type tag (+16) and
            //   pre-stored index (+20), and for type 4 indexes into this already-
            //   built deque to drive the controller (sub_6638B0). The initial
            //   builder (operator new controller + push {ctl,key} + register
            //   HM entry {type,index}) lives in the EmoteObject_init motion-load
            //   path and is NOT YET PORTED, so the deques stay empty (step inert).
            //   sub_663FC8 deserializes a controller from a PSB dict; sub_678044's
            //   per-category children (sub_678804 "eye" etc.) only RELOAD saved
            //   state into already-built controllers — also not the builder.
            //
            // Deque#4 (eye) step — PORTED (M2 eye vertical). Per binary
            //   EmoteEngine_progress @0x67d0a4..0x67d104: for each {ctl,label}
            //   entry, sub_663BDC(ctl, &out, step) then HM7[label] = out (the
            //   Player_HM2_upsert_labelToValue(+1440,..) call IS the HM#7
            //   double-map upsert keyed by elem.label).
            for (EmoteEyeControlEntry_Deque4& entry : _stateMachineDeque4) {
                float out = 0.0f;
                EmoteBlinkController_step(entry.ctl, &out, step); // sub_663BDC
                _labelToValueHM7[entry.label] = out;              // HM7 upsert @0x67d0f4
            }
            // Deque#5 (eyebrow) step — PORTED (M2 eyebrow vertical). Per binary
            //   EmoteEngine_progress @0x67d10c..0x67d160: for each {ctl,label}
            //   entry, sub_665600(ctl, &out, step) then HM7[label] = out (the
            //   Player_HM2_upsert_labelToValue(+1440, v23+1) call IS the HM#7
            //   double-map upsert keyed by elem.label; 16B stride, advance v23+=2).
            for (EmoteEyebrowControlEntry_Deque5& entry : _stateMachineDeque5) {
                float out = 0.0f;
                EmoteEyebrowController_step(entry.ctl, &out, step); // sub_665600
                _labelToValueHM7[entry.label] = out;                // HM7 upsert @0x67d150
            }
            // Deque#6 (mouth) step — PORTED (M2 mouth vertical). Per binary
            //   EmoteEngine_progress @0x67d168..0x67d1d8: for each 24B {ctl,label,
            //   talkLabel} entry, sub_666068(ctl, &outBeginFrame, &outCurrentValue,
            //   step) then HM7[label] = outBeginFrame (Player_HM2_upsert via v30+1)
            //   and HM7[talkLabel] = outCurrentValue (via v30+2); advance v30+=3
            //   (24B stride). This is the only deque whose step feeds TWO HM7 keys.
            for (EmoteMouthControlEntry_Deque6& entry : _compositeVarDeque6) {
                float outBeginFrame   = 0.0f;
                float outCurrentValue = 0.0f;
                EmoteMouthController_step(entry.ctl, &outBeginFrame,
                                          &outCurrentValue, step);   // sub_666068
                _labelToValueHM7[entry.label]     = outBeginFrame;   // HM7 upsert @0x67d1b4
                _labelToValueHM7[entry.talkLabel] = outCurrentValue; // HM7 upsert @0x67d1c8
            }
            // Deque#9 (selector) step — PORTED (M2 selector vertical). Binary
            //   order: the selector deque (engine+656) is stepped BEFORE the
            //   transition deque (engine+576) — see EmoteEngine_progress
            //   @0x67d1e0 (selector) then @0x67d240 (transition). Per
            //   @0x67d1e0..0x67d238: for each 48B {ctl,label} entry,
            //   sub_668470(ctl, &out, step) then HM7[label] = out (the
            //   Player_HM2_upsert_labelToValue(+1440, v38+1) call IS the HM#7
            //   double-map upsert keyed by elem.label; advance v38+=6 = 48B
            //   stride, block boundary node+60 = 480B). The step output is the
            //   selected option index (as float).
            for (EmoteSelectorControlEntry_Deque9& entry : _vectorVarDeque9) {
                float out = 0.0f;
                EmoteSelectorController_step(entry.ctl, &out, step); // sub_668470
                _labelToValueHM7[entry.label] = out;                // HM7 upsert @0x67d228
            }
            // Deque#8 (transition) step — PORTED (M2 transition vertical). Per
            //   binary EmoteEngine_progress @0x67d240..0x67d298: for each 24B
            //   {ctl,label,flag} entry, EmoteVarController_step(ctl, v71, step)
            //   then HM7[label] = v71[0] (the Player_HM2_upsert_labelToValue(
            //   v13+1440, v45+1) call IS the HM#7 double-map upsert keyed by
            //   elem.label; advance v45+=3 = 24B stride, block boundary node+63 =
            //   504B). The controller is count=1, so out[0] is the single channel.
            //   The flag byte@+16 is NOT read by this loop (only by setVariable
            //   case7). Stepped AFTER selector (binary @0x67d1e0 selector, then
            //   @0x67d240 transition).
            for (EmoteTransitionControlEntry_Deque8& entry : _auxVarDeque8) {
                float out = 0.0f;
                EmoteVarController_step(entry.ctl, &out, step);  // sub_666BF8
                _labelToValueHM7[entry.label] = out;             // HM7 upsert @0x67d288
            }
            // Deque#10 (loopControl) step — PORTED (M2 loopControl vertical).
            //   Per binary EmoteEngine_progress @0x67d2a0..0x67d370: for each 16B
            //   {ctl,label} entry, run the INLINE curve sampler (advance accum by
            //   step, wrap the keyframe index, blend v0/v1) then HM7[label] = out
            //   (the Player_HM2_upsert_labelToValue(+1440, v52+1) call IS the HM#7
            //   double-map upsert keyed by elem.label; advance v52+=2 = 16B stride,
            //   block boundary node+64 = 512B). There is NO standalone step fn in
            //   the binary — the sampler is open-coded here, factored into
            //   EmoteLoopController_step so this loop mirrors the per-entry body.
            //   The output is the curve blend cast float->double @0x67d35c.
            for (EmoteLoopControlEntry_Deque10& entry : _lookupCurvesDeque10) {
                const float out = EmoteLoopController_step(entry.ctl, step); // @0x67d2a0
                _labelToValueHM7[entry.label] = out;                         // HM7 upsert @0x67d360
            }

            // Apply the 4 direct controllers (pos/scale/color/angle).
            applyVarControllers_pos_scale_color_angle(step);

            // Wind emitter step (gated) — PORTED.
            //   Per binary EmoteEngine_progress @0x67d384..0x67d398:
            //       v65 = *(engine+1128);                       // wind emitter ptr
            //       if (v65 && *(byte*)(v65+1544))              // alloc'd AND gate on
            //           EmoteWindEmitter_step(v65, step);       // sub_6687E8(windObj, clampedStep)
            //   The X0 arg to sub_6687E8 is the emitter object (engine+1128), the
            //   float arg is `step` = fmin(dt,1.1) (the same clamped per-slice
            //   delta the deque steps use, V0 = V9 = v5 in the binary). The gate
            //   byte (+1544) is set by Player_startWind_populate; when wind is
            //   inactive the emitter is null/gate-clear and this is skipped.
            if (_windEmitter && _windEmitter->gate) {            /*0x67d384..0x67d390*/
                _windEmitter->step(step);                        /*0x67d394..0x67d398*/
            }

            dt -= step;
        }

        // Post-loop bind-loop (G2-C keystone): the binary
        // (EmoteEngine_progress @0x67D01C, body @0x67d3a4) walks HM#7's
        // _M_before_begin._M_nxt node chain (insertion order) at +1456:
        //   for (i = *(this+1456); i; i = *i) {
        //       sub_67C560(this, i+1, i+2);          // var-track weighted cascade,
        //                                            //   mutates i.value (node+16) in place
        //       v67 = i[2];                          // read accumulated value
        //       v68 = sub_67C6B0(this, i+1);         // negate-flag resolver
        //       v69 = (v68 & 1) ? -v67 : v67;
        //       Player_bindParameterValue(*(this+1064), i+1, 0, v69);  // write Player HM1/HM2
        //   }
        // i.key = node+8 (ttstr label), i.value = node+16 (double) — i.e. each
        // _labelToValueHM7 entry.
        //
        // The three callees are ported on the Player class (the port models the
        // engine's timeline/mirror/eval tables on Player; binary sub_67C560 reads
        // deque#10@+1040 + HM@+824/+880 + vector@+800, the port reads the
        // equivalent _playingTimelineLabels / mirrorVariableMatchList / HM1/HM2):
        //   sub_67C560            -> Player::accumulateTimelineContributionLike_0x67C560
        //   sub_67C6B0            -> Player::shouldMirrorEvalLabelLike_0x67C6B0
        //   Player_bindParameter  -> Player::bindParameterValueLike_0x6C4668 (ttstr,double)
        //                            which writes HM1 (_evalCascadeMap[joined].writeVal)
        //                            and HM2 (_evalResultValues[rawKey]) = the two maps
        //                            getVariable reads (R0-1).
        //
        // PLATFORM_BOUNDARY (insertion-order): libstdc++ chains HM7 nodes in
        // insertion order on _M_before_begin; libc++/this port has no
        // insertion-ordered chain, so we iterate the typed _labelToValueHM7 in
        // bucket order. Each bind writes a distinct label slot in Player HM1/HM2
        // (no inter-label ordering dependence in the binary's bind body), so the
        // observable HM1/HM2 result is order-independent; the boundary is benign.
        Player& p = player();                                       // *(this+1064)
        for (auto& kv : _labelToValueHM7) {
            const ttstr& label = kv.first;
            const std::string narrowLabel = detail::narrow(label);

            // sub_67C560(this, &label, &value): accumulate var-track timeline
            //   contribution into the HM7 node value in place (binary mutates
            //   i.value at node+16; we mutate the map value).
            double& value = kv.second;
            p.accumulateTimelineContributionLike_0x67C560(narrowLabel, value);

            // v67 = i[2] (read back the accumulated value).
            const double accumulated = value;

            // v68 = sub_67C6B0(this, &label); negate = v68 & 1.
            const bool negate =
                p.shouldMirrorEvalLabelLike_0x67C6B0(narrowLabel);

            // Player_bindParameterValue(player, &label, 0, negate ? -v67 : v67):
            //   write Player HM1/HM2 (the getVariable read surface).
            p.bindParameterValueLike_0x6C4668(label, negate ? -accumulated
                                                             : accumulated);
        }

        // sub_67C8A8(this) @0x67d3f8 — clampControl binder. Runs AFTER the HM7
        //   bind-loop (above) and BEFORE the Player-level progress sub_6D2A54
        //   (below). It strides the engine's 40B clampControl deque (deque#7
        //   @engine+496, populated by EmoteEngine_buildClampControl @0x66EE5C;
        //   element = {int type@+0, double min@+8, double max@+16, ttstr var_lr@+24,
        //   ttstr var_ud@+32}), and per entry: reads two ENGINE-HM7 values keyed by
        //   var_lr (X) / var_ud (Y) (sub_67C8A8 v6 = result+180 = engine+1440 = HM7,
        //   NOT player HM2), runs the var-track cascade sub_67C560 on each, normalizes
        //   to [-1,1] over [min,max], 2D disk-remaps by mode (0=squircle,
        //   1=clamp-circle), then writes both back via Player_bindParameterValue
        //   (engine+1064), the X result negated when sub_67C6B0 (mirror) is set.
        //   The faithful per-entry BODY is ported as
        //   Player::applyClampControlsLike_0x67C8A8 (reads engine HM7 via _engineBack
        //   + the clampControl snapshot MotionSnapshot::clampControls, writes player
        //   HM1/HM2).
        //
        //   TOPOLOGY (2026-06-03 approved migration): this clamp now runs HERE, in
        //   EmoteEngine::progress, exactly where the binary places it — @0x67d3f8,
        //   after the bind-loop @0x67d3a4 and before sub_6D2A54 @0x67d408. It was
        //   formerly run on the Player progress path (Player::frameProgress ->
        //   applyEvalResultPostProcessLike_0x67CC9C); that call has been REMOVED from
        //   frameProgress. Player_progress_inner @0x6C106C and the child-motion pass
        //   @0x6BE2A4 both run progress_inner WITHOUT any bind-loop or clamp (fresh-
        //   decompile confirmed this round), so the Player progress path must not
        //   carry it. Single invocation per frame here — no double-clamp.
        player().applyClampControlsLike_0x67C8A8();                 // @0x67d3f8

        // Step 7 — Player-level progress @0x67d408:
        //     sub_6D2A54(*(this+1064)=Player, 0, v12=originalDt);
        //   sub_6D2A54 (= local Player::progressFramesLike_0x6D2A54) sets
        //   player+16=0 (pendingEvents cursor -> _pendingEvents.clear()), runs
        //   progress_inner / updateLayers / calcBounds / dispatchEvents. Placed
        //   AFTER the G2-C bind-loop (so the bound HM1/HM2 values are already
        //   written before the Player frame seek/eval reads them) and BEFORE the
        //   bust/hair physics gate. The binary passes v12 (ORIGINAL FRAME dt), not
        //   the drained dt-slice copy and NOT a ms value — sub_6D2A54 forwards it
        //   straight to progress_inner with NO *60/1000 conversion (that lives in
        //   the NCB wrappers). Use progressFramesLike_0x6D2A54 (frame-units), NOT
        //   progressMsLike_0x6D2A54 (which would double-not-convert and re-scale a
        //   frame value by 0.06). The progress ENTRY (D3DEmotePlayer::progress /
        //   EmotePlayer::progress) routes through engine().progress, which calls
        //   this once here — the entry no longer calls a Player progress directly,
        //   so Player progress runs exactly once per frame (matches binary).
        player().progressFramesLike_0x6D2A54(originalDt);          // @0x67d408

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

            // Physics step pass — now ported (sub_67B970 anchor resolver +
            // EmotePhysics_springStep + EmoteBustChainSpring_step):
            //   stepHairParts(this, physDt);                               @0x67d458
            //   stepBust(this, _ctlBust1Target, &_bustChain1Nodes,
            //            _bustSpring1Const@+1184, physDt);                 @0x67d470
            //   stepBust(this, _ctlBust2Target, &_bustChain2Nodes,
            //            _bustSpring2Const@+1192, physDt);                 @0x67d488
            // The deques #1/#2/#3 are now POPULATED by buildBustControl /
            // buildChainControl (applyMetadata @0x67D4D0 dispatch) so this pass
            // runs on real spring nodes. Outputs are still inert on the logo
            // fixture (no bustControl/hairControl/partsControl metadata -> empty
            // deques) and driven by zero targets until the controller-target /
            // spring-const wiring lands; structure matches the binary exactly.
            stepHairParts(physDt);                                      // @0x67d458
            stepBust(_ctlBust1Target, _bustChain1Nodes,
                     _bustSpring1Const, physDt);                        // @0x67d470
            stepBust(_ctlBust2Target, _bustChain2Nodes,
                     _bustSpring2Const, physDt);                        // @0x67d488
        }
    }

} // namespace motion
