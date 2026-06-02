---
name: emote-physics-population
description: EmoteEngine hair/parts/bust spring deque POPULATION builders (sub_66B018/sub_66B9D0) + spring ctors. CORRECTS bustControl<->stepHairParts naming.
metadata:
  type: project
---

# EmoteEngine spring-physics deque POPULATION (libkrkr2.so)

Reversed 2026-06-03. Step fns (stepHairParts@0x67B748/stepBust@0x67BCE8) were
ALREADY ported 2026-05-30 and re-verified bit-for-bit against fresh decompile
this pass (match). The gap was DEQUE POPULATION — see [[emote-physics-pass]].

## CORRECTION to EmoteEngine_controller_deque_builder.md naming
The PSB key names are misleading vs which step fn consumes them. AUTHORITATIVE
from applyMetadata@0x67D4D0 dispatch + the two builders:
- **bustControl** -> sub_66B018(engine, dict) -> deque#1 @engine+0 (48B node,
  72B EmoteSpringState) -> consumed by **stepHairParts**. HM6 type=**0**.
- **hairControl** -> sub_66B9D0(engine, engine+80, dict, 1) -> deque#2 @+80
  (56B node, 176B EmoteBustChainSpring) -> **stepBust** chain1. HM6 type=**1**.
- **partsControl** -> sub_66B9D0(engine, engine+160, dict, 2) -> deque#3 @+160
  -> **stepBust** chain2. HM6 type=**2**.
So "bustControl" feeds the SIMPLE spring (hair/parts step), and "hair/parts
Control" feed the CHAIN spring (bust step). Do NOT trust the key name to infer
the step fn. (Old builder-map memory said hairControl->66B9D0, bustControl->
"writes scalars" — the scalar claim was wrong; 66B018 populates deque#1.)

## .bss default constants (emoteplayer_static_init @0x42eb28, VERIFIED bytes)
- qword_1AB7E68 = 0  -> storedXY / posXY / velXY init = 0,0
- dword_1AB7E70 = 0  -> storedZ / posZ / accZ = 0
- qword_1AB7E74 = 0x3F80000000000000 -> LE floats (0.0f, 1.0f) rest unit vec
- dword_1AB7E7C = 0  -> 0.0f (z of unit vec)
- All Motion_propGetDouble default-value ptrs (1AB7E8C gravity, E90 spring,
  E94 friction, E98 scale_x, E9C scale_y, EFC friction_x, F00 friction_y,
  F04 b_rate, F08 v_bound, F0C ud_eft, F10 bend_spd, F14 bend_vol, ED0 length,
  ...) are .bss = **0.0** (only E68/E70/E74/E7C/E80/E88 are init'd nonzero by
  static_init; E80=1065353216=1.0f, E88=0). So absent-key default = 0.0.

## EmoteSpringState ctor (sub_662448, new 0x48=72B) field writes
firstFlag(+0)=1; +44/+36 = E70/E68 (=0); +56/+48 = E70/E68; +68/+60 = E70/E68.
Then prop reads (each narrow double->float, store raw bits = getFloatValue):
gravity->+4(k_a), spring->+8(k_b), friction->+12(drag), scale_x->+20(leverX),
scale_y->+24(leverY). biasY(+16) NOT set by ctor — set later by builder (ofs).
Builder sub_66B018 then overwrites vec3s: op(dict x/y/z)->+36/+40/+44,
p(dict x/y/z)->+48/+52/+56, pv(dict x/y/z)->+60/+64/+68, ofs(double)->+16.

## EmoteBustChainSpring ctor (sub_668EF8, new 0xB0=176B) field writes
firstFlag(+0)=1; +48=0; +88=E70(0); +168=0 (collisionCurve); +80=E68(0);
memset(+92,0,0x48). Reads: gravity->+4, friction_x->+8, friction_y->+12,
b_rate->+16, v_bound->+20, ud_eft(INT propGetInt)->+24, bend_spd->+28,
bend_vol->+32. length list[0]->+36, list[1]->+40. scale_x list[0]->+56,[1]->+64.
scale_y list[0]->+60,[1]->+68. Then rest-pos: unit={0,1,0}(E74 lo/hi,E7C);
+92/+96/+100 = length0*unit; +116/+120/+124 = copy of +92..; +104/+108/+112 =
length1*unit; +128/+136 = copy(+104/+112); +148/+140 = E70/E68; +160/+152=E70/E68.
NOTE: step fn's struct field NAMES (forceScaleN@+16 etc.) were a math-only guess;
ctor gives true semantics but step offsets unchanged so EmoteBustChainSpring
struct layout stays valid — just add ctor writing by the same offsets.

## 48B/56B deque NODE writes (builders)
sub_66B018 (48B, deque#1): node+0=spring; node+8=**1** (initFlag SET);
OWORD@+12=0, OWORD@+28=0; node+12=baseLayer(shapeLabel), node+20=var_lr(keyX),
node+28=var_ud(keyY). HM6(var_lr)=type0/idx, HM6(var_ud)=type0/idx.
sub_66B9D0 (56B, deque#2/#3): node+0=spring; node+8 **NOT written** (binary
leaves it indeterminate — raw operator-new block, no memset). OWORD@+12=0,
OWORD@+28=0, +44=0; node+12=baseLayer, node+20=var_lr(keyA), node+28=var_lrm
(keyB), node+36=var_ud(keyC). HM6 3 refs type=a4(1|2)/idx. Local port zero-inits
node (POD) -> bust initFlag=0 deterministically; documented divergence (binary
indeterminate). enabled gate (PSBBool/PSBNumber) skips but advances loop idx.

## vec3 reader sub_66B83C(dict): reads x/y/z doubles, narrows each to float;
decompiler "returns x" but caller stores x->+0,y->+4,z->+8 (leftover s1/s2 regs).
In builder p/pv for sub_66B9D0 are LISTS of 2 dicts (PropGet idx 0/1 then 66B83C).

## DONE 2026-06-03 (this slice)
Ported: EmoteSpringState_ctor + EmoteBustChainSpring_ctor (EmoteSpring.cpp),
buildBustControl (deque#1) + buildChainControl(tag1/2) (EmoteEngine.cpp), wired
into PlayerCore applyMetadata dispatch BEFORE eyeControl (binary order), dtor +
PlayerCore rebuild free node.spring for all 3 deques. Built green web+wasmtime.
- LATENT BUG FIXED: EmoteBustChainSpring struct had OFFSET-COMMENTED fields but
  C++ packed them contiguously -> the pre-existing EmoteBustChainSpring_step
  F(off)/sp[idx] byte accesses were reading WRONG slots (never exercised: deques
  were empty). Pinned the struct to exact byte offsets via named reserved members
  (_pin1/_pin112/_pin164 + bendSpd@28/bendVol@32/rampPhase@48/rampDepth@52);
  offsetof now matches binary exactly, sizeof==176==0xB0. Verified via /tmp probe.
  This is a data-contract (CLAUDE.md element-internal exception), NOT cosmetic pad.
- prop reads: springPropFloat = double->float FCVT narrow (NOT getFloatValue raw
  bits) — spring params are value conversions. op/p/pv vec3 dicts read x/y/z.
  chain p/pv = 2-elem lists under param; bp = 2-elem list under ELEMENT (key "bp"
  = UTF-16 at 0x15069A0, IDA truncates to "b"). ofs/bendR/bendS from param.

## STILL OPEN (out of this slice)
Controller targets engine+1104(_ctlHairPartsTarget)/+1112/+1120 and spring
consts +1184/+1192 are NOT written by these builders — set by variableList/
setVariable resolution (un-ported). So with these builders, deques fill but
cur[0]/cur[1]=0 and springConst=0 until that path lands. Springs run, outputs
to HM7, but driven by zero target. No oracle (logo inert). NON-regression only.
