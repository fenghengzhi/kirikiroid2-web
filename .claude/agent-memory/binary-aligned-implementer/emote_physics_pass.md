---
name: emote-physics-pass
description: EmoteEngine physics subsystem (springStep/stepHairParts/stepBust/progress) binary facts, struct layouts, blockers
metadata:
  type: project
---

# EmoteEngine physics + progress pass (libkrkr2.so)

Reversed 2026-05-30. Functions and addresses:
- EmotePhysics_springStep @ 0x662768 — leaf damped-spring single step. Pure math (sinf/cosf/atanf). Operates on a spring-state struct `a1` POINTED TO by the deque node (node+0), NOT the node itself.
- EmoteEngine_stepHairParts @ 0x67B748 — iterates hair/parts deque (#1 @+0, node stride 48B = v9+=6). Calls sub_662768 on `*v9`.
- EmoteEngine_stepBust @ 0x67BCE8 — iterates bust chain deque (#2 @+80 / #3 @+160, node stride 56B = v15+=7). **Calls sub_6689A4 (NOT springStep)** — a separate 2-segment chain solver.
- EmoteEngine_progress @ 0x67D01C (thunk 0x530a5c) — dt-slice loop + bind-loop + physics pass.

## qword_1AB7E74 actual runtime value
.bss symbol (IDA shows 0xFFFFFFFF because .bss not loaded). Written by emoteplayer_static_init @0x42eb28: `qword_1AB7E74 = 0x3F80000000000000`.
As two LE floats: float[0]@0x1AB7E74 = 0.0f, float[1]@0x1AB7E78 = 1.0f. A rest/base 2D unit vector (0,1).

## springStep struct (a1, ~72B) — field offsets (all float unless noted)
- +0 byte firstFlag (1=init branch); +4 dampX; +8 dampY/k; +12 drag;
- +16 bias; +20 lever; +24 gain2; +28/+32 prevDeltaXY; +36/+40 storedXY;
- +44 storedZ; +48/+52/+56 posXYZ; +60/+64 velXY; +68 velZ.
Output: *a2 = atanf(...)/0.0392699082 (X angle), *a3 = same for Y.
Constants: 0.0392699082 (=pi/80), 0.0451603944. Keep literally.

## Deque NODE layout (stepHairParts #1, 48B)
- +0 spring-state ptr (=springStep a1); +8 byte init flag; +12 anchorXY input to sub_67B970;
- +20 ttstr label1 (HM7 key for X out); +28 ttstr label2 (HM7 key for Y out);
- +36 prevAnchor (qword=2 floats), cached/restored each frame.
- (float*)v9+9 = +36, +10 = +40 used as lerp targets in dt-slice subloop.

## Deque NODE layout (stepBust #2/#3, 56B)
- +0 spring ptr (sub_6689A4 a1); +8 byte init; +12 anchorXY; +20/+28/+36 three ttstr labels;
- +44 prevAnchor qword. (float*)v15+11/+12 = +44/+48 lerp targets.

## RESOLVED 2026-05-30 (was BLOCKERS) — stepHairParts/stepBust now ported
- EmoteEngine_resolveShapeAnchor @0x67B970 (renamed) — per-node "shape" anchor.
  Flow: resolved=getLayerMotion(label) [=sub_6D38F4→sub_6B5AD8, returns PSB dict
  dispatch]; if not object → return 0. shape=resolved.PropGet("shape") [vtable+32];
  if not object → 0. type=sub_6635DC(shape,"type") [int]; if type!=0 → 0.
  x=sub_662668(shape,"x"); y=sub_662668(shape,"y") [doubles]. rootX=getX()(node+1592),
  rootY=getY()(node+1600) via sub_6CD738. r=*(EmoteEngine+1176)=_meshDivisionRatioDup.
  **X/Y CROSSOVER (verbatim):** *outX(a3)=rootY+(y-rootY)*r; *outY(a4)=rootX+(x-rootX)*r.
  return 1 only on success (outputs untouched on any miss). Local resolver =
  Player::getLayerMotion(ttstr) (PlayerLayerQuery.cpp). Local impl =
  resolveShapeAnchorLike_0x67B970 (anon ns in EmoteEngine.cpp).
- EmoteBustChainSpring_step @0x6689A4 (renamed) — 176B 2-segment chain spring,
  NOT springStep. Loop v28∈{0,1}: constraint to previous chain point (seg0 prev =
  +80 root, seg1 prev = seg0 @+116), integrate, OPTIONAL collision-depth curve
  lookup at +168 (128 entries from +4, stride 12B {byte enabled@-4,float center,
  float halfW}; match val = halfW * *(curve+1556); null-guarded), velocity damping
  (+8/+12), atan angle outputs (slope *(a1+8*v28+56)/+60). Constants 0.03125 (in
  stepBust caller), 6.28318531, 28.0, 0.015625, 4.0, 0.0451603944, 0.0392699082.
  Struct in EmoteSpring.h; step in EmoteSpring.cpp (raw `*(float*)(p+off)` accesses
  to mirror +12*v28 / +8*v28 segment arithmetic faithfully).
- Player_getAngleDeg @0x6CD0C0 — returns RADIANS despite the name. if(_directEdit
  /player+482) angle=*(player+464)=_emoteAngle else angle=root node delta.angle
  (node+1616); return angle*0.0174532925. Ported as Player::emoteGetAngleRadLike_
  0x6CD0C0 (PlayerCore.cpp); added Player+464 _emoteAngle field (un-written until
  emote direct-edit path ported). stepHairParts/stepBust call it per node/substep.
- stepBust collisionCurve: node->spring->collisionCurve(+168) = EmoteEngine+1128
  (_matrixHeap1128). stepBust output mapping: keyA(node+20)=oSeg1+jiggle,
  keyB(node+28)=oSeg0-jiggle, keyC(node+36)=oLastY. depth ramp on spring[13],
  phase spring[12]=fmod(...,2pi), jiggle=sin(phase)*spring[13]*spring[8].
- DEFER still: sub_6687E8, sub_67C560/67C6B0/67C8A8, sub_6D2A54,
  Player_bindParameterValue (bind-loop/post-loop). setVariable write path that
  POPULATES deques #1/#2/#3 is also un-ported → these spring fns run on empty
  deques at runtime (no observable effect yet, but structure now binary-aligned).

## ALIGNMENT TRAPS hit this pass
- `friend class EmoteEngine` does NOT grant access to anon-namespace free fns in
  EmoteEngine.cpp — only to EmoteEngine member fns. Port Player-private reads as
  Player methods (emoteGetAngleRadLike_0x6CD0C0), not free helpers.
- EmoteBustChainSpring +168 is a QWORD (8B) on ARM64 but `void*` is 4B on wasm32;
  static_assert sizeof==176 fails on web/wasmtime. Pin offsetof(...collisionCurve)
  ==168 and sizeof==168+sizeof(void*) instead.
- getLayerMotion path: returns the PSB dict variant; do tvtObject checks before
  AsObjectNoAddRef / PropGet (binary's v29/v24 null gates).

## progress @0x67D01C control flow (VERIFIED)
1. `if (a2 /*dt*/ != 0.0)` TOP-LEVEL GATE (P0-B2: local was missing this).
2. Player_preProgress(); v14 = dt.
3. if (dt>0) enter dt-slice loop; else `while(_dirty@1162)` wrapper. Loop:
   - v19 = fmin(v14, 1.1); v5=v19(step); _dirty=0;
   - deque#4(sub_663BDC,stride2) → HM7; #5(sub_665600,stride2); #6(sub_666068,stride3,2 out);
     #9(sub_668470,stride6) [note: @+656 NOT +640]; #8(EmoteVarController_step,@+576,stride3);
     #10(@+736 inline curve lerp,stride2).
   - applyVarControllers(v5); if(player@1128 && [+1544]) sub_6687E8(v5); v14 -= v19;
   - loop while v14>0; outer while _dirty.
4. bind-loop: `for(i=*(this+1456); i; i=*i)` walks HM7 insertion chain.
5. sub_67C8A8(this); sub_6D2A54(player,0, v12/*ORIGINAL dt*/).
6. **physics pass** gated `if (v12 /*ORIGINAL dt*/ != 0.0 && !*(this+1159)/*syncWaiting*/)`:
   (P0-B3: uses ORIGINAL dt v12, NOT drained dt v14.)
   - cast dt to float; step _ctlHairPartsTarget@+1104, _ctlBust1Target@+1112, _ctlBust2Target@+1120;
   - stepHairParts(this, dt);
   - stepBust(this, *(this+1112), this+80,  *(double*)(this+1184), dt);  // chain1, spring const +1184
   - stepBust(this, *(this+1120), this+160, *(double*)(this+1192), dt);  // chain2, spring const +1192

## Local mapping
- _syncWaiting @ EmoteEngine.h +1159; _dirty @ +1162; _bustSpring1Const @+1184; _bustSpring2Const @+1192.
- _meshDivisionRatioDup @+1176. Controllers _ctlHairPartsTarget/_ctlBust1Target/_ctlBust2Target.
- progress() in EmoteEngine.cpp. applyVarControllers order = pos→color→scale→angle (already correct).
