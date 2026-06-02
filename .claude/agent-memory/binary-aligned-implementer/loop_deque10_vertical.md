---
name: loop-deque10-vertical
description: M2 EmoteEngine deque#10 (engine+736, loopControl, TYPE 3) complete vertical — builder sub_66E480, INLINE curve step @0x67d2a0, 16B elem, 12B keyframe POD
metadata:
  type: project
---

# loopControl / deque#10 (engine+736) vertical — DONE 2026-06-03

Last progress-stepped controller-deque. New files EmoteLoopController.{h,cpp}.

## Step has NO standalone fn — INLINED into EmoteEngine_progress @0x67d2a0..0x67d370
Curve sampler over 16B elems {EmoteLoopController* ctl@0, ttstr label@8}:
  idx=ctl[+0](int); accum=ctl[+4](float)+dt; ctl[+4]=accum;
  count=(ctl[+16]-ctl[+8])/12;  // keys.size()
  span=keys[idx].span(kf+8);
  if(span<=accum){ do{ idx=(idx+1)%count; accum-=span; span=keys[idx].span; }while(span<=accum); ctl[+4]=accum; ctl[+0]=idx; }
  t=accum/span; out= t*keys[idx].v1(kf+4) + (1-t)*keys[idx].v0(kf+0);  // LDP S3,S2,[kf]
  HM7[label]=(double)out (Player_HM2_upsert +1440). advance elem+=16 (v52+=2), block node+64=512B.

## Population: UNIQUE builder = sub_66E480 (loopControl), NOT applyMetadata-direct
Cross-checked the other late applyMetadata dispatches (all DIFFERENT targets):
  clampControl 0x66EE5C -> deque cur a1[66]=engine+528, 40B elem, block 0x1E0=480.
  mirrorControl 0x66F364 -> vector engine+800 (variableMatchList).
  instantVariableList 0x66F64C / timelineControl 0x66F80C -> neither +736.
sub_66E480 writes via finish.cur a1[96]=engine+768 / finish.node a1[99]=engine+792 = same deque as begin.cur@736 (start{cur736,first744,last752,node760} finish{cur768,..node792}). 16B elem, block 0x200=512.
Builder: per enabled elem (gate @0x66e5f0): read elem["transitionList"] (list of [v0,v1,span] triples), new(0x20) ctl, resize keys to kfCount, fill each kf via propGetIndexDouble(0/1/2) STORED AS FLOAT (STR S). push {ctl,label}. label=elem["var_loop"] (= deque label AND HM6 key). HM6 {type=3,index=v6} (skipped elem still ++v6).

## FLOAT-BITS verdict (audited): ALL raw float bits, no remap
Every numeric read is LDR S (no SCVTF): accum(+4), kf v0/v1/span. Builder stores via STR S after propGetDouble FCVT double->float. Local uses PSBNumber::getFloatValue() (NOT (float)(int)). 12B keyframe POD {float v0,v1,span} = platform-independent data contract (kept). Only integer = currentIndex (genuine LDR W). This category does NOT have the eye/eyebrow/angle int-cast powCount trap — there is no pow field here at all.

## Controller layout (0x20=32B)
+0 int currentIndex; +4 float accum (RAW bits); +8/+16/+24 std::vector<Keyframe12B>.
Element label/HM7-key/HM6-key are ALL the "var_loop" string value (single source).

## Wiring
PlayerCore.cpp loadFromSnapshot: drop+rebuild _lookupCurvesDeque10 after buildSelectorControl (binary order: loopControl @0x67d93c after selector @0x67d8ec; relative order immaterial — no cross-ctl dep). EmoteEngine dtor: delete entry.ctl (no special dtor). CMake: added EmoteLoopController.cpp to motionplayer/CMakeLists.txt + platforms/wasmtime/CMakeLists.txt. Both built green (web 101/101, wasmtime guest 35/35, grep error: 0).

## VERIFICATION GAP / oracle-inert (NOT a defer)
Logo differential motion has no enabled loopControl -> step inert vs current fixtures. Faithful per CLAUDE.md six-dim standard; build = non-regression guard.

## REMAINING open (deliberately out of scope)
bust/hair/parts physics (deque#1/2/3), sub_661F7C mesh resolver, setVariable cases 4-8 reader dispatch, clamp(0x66EE5C @+528)/mirror(0x66F364 @+800)/instantVariableList(0x66F64C)/timeline(0x66F80C) builders. deque#10 was the LAST progress-stepped controller-deque; all 6 controller-step categories (eye/eyebrow/mouth/selector/transition/loop) now ported.
