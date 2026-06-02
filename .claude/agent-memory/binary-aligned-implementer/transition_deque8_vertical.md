---
name: transition-deque8-vertical
description: M2 EmoteEngine transition/deque#8 (engine+576) vertical DONE - builder 0x66D4C4 + step sub_666BF8 + powCount raw-float-bits fix
metadata:
  type: project
---

# Transition / deque#8 (engine+576) vertical DONE 2026-06-03

EmoteVarController (0x80, ctor 0x667030 count param) is the transition controller. Builder
buildTransitionControl@0x66D4C4, step=EmoteVarController_step sub_666BF8 (progress loop @0x67d240).
Local: cpp/plugins/motionplayer/EmoteVarController.{h,cpp} + EmoteEngine.{h,cpp} + PlayerCore.cpp.

## powCount = RAW FLOAT BITS (the bug fixed this slice)
20B keyframe +16 (EmoteVarKeyValue20B::powCount) AND controller +112 (EmoteVarController::powCount)
are BOTH float, never int. Whole pipeline treats +16 as float:
- Animator_setKeyframes 0x667300 / pushback 0x667490: `*(DWORD*)(elem+16)=*a5` where a5 is a `float` arg (DWORD copy of float bits).
- step 0x666BF8: `*(DWORD*)(a1+112)=*(DWORD*)(elem+16)` (raw copy) then `LDR S1,[X20,#0x70]; powf(phase,S1)` @0x666df4 = NO SCVTF.
FIX: changed both fields uint32->float; step uses memcpy(&powCount,&kf.powCount,4) + std::pow(phase, powCount) (no static_cast). Selector EmoteSelectorController.cpp:67 memcpy sizeof now float. CORRECTS prior comment "field is uint32".
SAME-CLASS BUG STILL OPEN (out of scope): EmoteAngleController.cpp:85/105 still static_cast<int>(powCount) — separate AngleController, not transition.

## Builder 0x66D4C4 structure
enabled gate (0x66d62c, skip->LABEL_28, loop index still advances); new(0x80)+ctor(count=1); push 24B
{ctl@+0, ttstr label@+8, byte flag@+16=1} to deque a1[76]/end a1[78] (begin@+576); label written AFTER push
(0x66d770); HM6 findOrInsert@+1384 -> {type=7, index=loopIndex} (0x66d790). block 504.
Element type: EmoteTransitionControlEntry_Deque8 {EmoteVarController* ctl; ttstr label; uint8_t flag=1}.

## Step loop @0x67d240 (24B stride)
v45=begin@+576; while(cur!=begin){ EmoteVarController_step(*v45,out,dt); HM7[*(v45+1)=label]=out[0]; v45+=3 (24B); block node+63=504B }.
flag@+16 NOT read by step (only setVariable case7 Animator_setKeyframes gate). count=1 so out[0]=single channel.
ORDER: selector(@0x67d1e0, engine+656=Deque9) steps BEFORE transition(@0x67d240, engine+576=Deque8). Local progress() matches.

## Selector cross-reference NOW LIVE (was inert)
buildSelectorControl@0x66db0c scans THIS deque for option.label==transition.label -> borrows refCtl, sets matched flag@+16=0, break (first hit).
REQUIRES transition built before selector. applyMetadata 0x67D4D0 dispatches transitionControl BEFORE selectorControl.
PlayerCore.cpp dispatch now: eye->eyebrow->mouth->TRANSITION->selector (transition inserted before selector). Selector refCtl loop in EmoteEngine.cpp now real scan (was hardcoded null+empty-deque comment).
refCtl is BORROWED: transition deque owns the EmoteVarController; selector dtor must NOT delete refCtl. dtor deletes deque#8 controllers via EmoteVarController_dtor (3 heap arrays) + delete.

## Verification gap
No oracle/fixture/differential covers transition (deep emote physics). Unit tests + differential specs (geometry/position/curve) don't exercise EmoteVarController step or powCount. Acceptable per CLAUDE.md (証拠齐+build green). logo-inert.

## Remaining open (NOT this slice)
deque#10 inline curve lookup (progress @0x67d2a0, 16B elem, 12B sub-elem table); bust/hair/parts physics resolvers;
sub_661F7C anchor resolver; setVariable READER (0x671228, cases 4-8 dispatch); clamp/mirror/loop/instantVariableList builders;
EmoteAngleController powCount int-cast bug (sibling, same fix pattern).
