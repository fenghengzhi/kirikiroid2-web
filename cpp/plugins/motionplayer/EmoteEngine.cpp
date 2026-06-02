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

        // Delete deque#4 (eye) controllers. The binary's dtor frees each
        //   controller-deque's heap controllers (operator delete) before tearing
        //   down the deque header; the entry's ttstr label is released by the
        //   ttstr destructor. (M2 eye vertical: only deque#4 is populated so far.)
        for (EmoteEyeControlEntry_Deque4& entry : _stateMachineDeque4) {
            delete entry.ctl;
            entry.ctl = nullptr;
        }
        _stateMachineDeque4.clear();

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
    //       node->spring->collisionCurve(+168) = this->_matrixHeap1128(+1128);
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

            // node->spring->collisionCurve = this->_matrixHeap1128 (v12[141]). /*0x67bea4*/
            if (node.spring) {
                node.spring->collisionCurve = _matrixHeap1128;
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

            // Physics step pass — now ported (sub_67B970 anchor resolver +
            // EmotePhysics_springStep + EmoteBustChainSpring_step):
            //   stepHairParts(this, physDt);                               @0x67d458
            //   stepBust(this, _ctlBust1Target, &_bustChain1Nodes,
            //            _bustSpring1Const@+1184, physDt);                 @0x67d470
            //   stepBust(this, _ctlBust2Target, &_bustChain2Nodes,
            //            _bustSpring2Const@+1192, physDt);                 @0x67d488
            // The deques #1/#2/#3 are still empty until the (un-ported)
            // setVariable write path populates them with spring nodes, so this
            // pass has no observable effect today — but the structure now
            // matches the binary exactly.
            stepHairParts(physDt);                                      // @0x67d458
            stepBust(_ctlBust1Target, _bustChain1Nodes,
                     _bustSpring1Const, physDt);                        // @0x67d470
            stepBust(_ctlBust2Target, _bustChain2Nodes,
                     _bustSpring2Const, physDt);                        // @0x67d488
        }
    }

} // namespace motion
