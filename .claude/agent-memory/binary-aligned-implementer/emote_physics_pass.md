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

## BLOCKERS (un-reversed dispatch deps; DEFER per CLAUDE.md)
- sub_67B970 @0x67B970 — per-node "shape" anchor resolution via TJS dispatch:
  resolves a label→dispatch (sub_6D38F4), PropGet "shape" (vtable+32), reads type/x/y
  (sub_6635DC/sub_662668), lerps with *(double*)(this+1176)=_meshDivisionRatioDup.
  Heavy TJS-dispatch path. Required by BOTH stepHairParts and stepBust.
- sub_6689A4 @0x6689A4 — bust 2-segment chain spring (separate from springStep).
- sub_6687E8, sub_67C560, sub_67C6B0, sub_67C8A8, sub_6D2A54, Player_bindParameterValue — bind-loop/post-loop deps.

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
