---
name: m2-setvariable-controller-subsystem
description: M2 EmoteEngine setVariable(0x671228)/builder(0x67D4D0)/progress(0x67D01C)/6 controllers/2 springs verdict. 2026-06-05 fresh re-decompile SUPERSEDES the old 06-03 "architecture ❌ controllerBindings parallel model" verdict — local is now binary-faithful HM6-index reader. One REAL dead-path residue remains.
metadata:
  type: project
---

2026-06-05 fresh re-decompile (read-only) of: 0x671228 setVariable, 0x67D4D0 builder, 0x67D01C progress, 0x67C560 spring bind-loop, 0x666068 mouth step, 0x668470 selector step, 0x665600 eyebrow step, 0x663BDC eye/blink step, EmoteEngine_stepBust, EmoteEngine_stepHairParts(0x67B748), EmotePhysics_springStep(0x662768).

## CORRECTION of stale 06-03 verdict (falsified-now-fixed)
The earlier "structure/call-chain/container ❌ — local routes setVariable via controllerBindings (unordered_map<string>) + Legacy deque BY-STRING, 5 enqueue handlers UNPORTED" is OBSOLETE. Verified this session: EmoteEngine::setVariable (EmoteEngine.cpp:1645) is now the binary-faithful HM6-index reader: `_scalarHM6_1384.find(key)` -> `EmoteVarRef{type,index}` -> `switch(type)` -> `typed_deque[index]` -> 5 enqueue handlers. controllerBindings is NO LONGER the dispatch source.

## VERDICT per audit dim (mostly ✅)
- ① setVariable cases 4-8 routing ✅ — all 5 routes present incl. case6 dual-key (key==label -> `ctl+108=(int)value`; key==talkLabel -> mouth enqueue) + case7/8 `elem+16` flag gate + easeWeight 3-branch(0x671304) + cases0/1/2 `_syncWaiting`(+1159) gate + HM2 fallthrough. EmoteEngine.cpp:1645-1743.
- ② 6 controller step order ✅ — deque#4@+256/#5@+336/#6@+416/#9@+656(selector BEFORE)/#8@+576(transition)/#10@+736(inline loop sampler). Local EmoteEngine.cpp:1835-1907 matches incl. non-obvious selector-before-transition + mouth dual-HM upsert.
- ③ float-bits ✅ — powField via `memcpy(raw bits)` not SCVTF (mouth step EmoteMouthController.cpp:130; 5 enqueue handlers 1528/1553/1576/1612/1637). eye final remap float-num/double-div mix aligned.
- ④ spring physics ✅ REAL (not stub) — EmotePhysics_springStep(0x662768) sinf/cosf/atanf damped spring rest-vec qword_1AB7E74={0,1} -> EmoteSpring.cpp:31; EmoteBustChainSpring_step(0x6689A4) -> :120; stepHairParts/stepBust sub-step integ + depth-ramp(|oLastY|<=28) + fmod jiggle + HM7 3-key -> EmoteEngine.cpp:425/530. NO STUB_WARN/TODO in EmoteSpring.cpp/stepBust/stepHairParts.
- ⑤ deque element layout ✅ — mouth#6=24B{ctl,label,talkLabel}, #4/#5=16B, #8=24B, #9=48B(480B block), #10=16B; springs hair48B/bust56B. typed std::deque<EntryN> EmoteEngine.h:514-558.
- ⑥ container choice ✅ — typed std::deque per controller + HM6=_scalarHM6_1384(unordered_map<ttstr,EmoteVarRef>) + HM7=_labelToValueHM7. ("container ❌" was stale.)

## REAL residue (the one ❌, low severity, oracle-inert)
PlayerFrameProgress.cpp:2001-2014 has a `stepControllerBucket` loop over `_engineBack->_type4..8ControllerAnimators` (legacy std::deque<LegacyVariableAnimatorState>, EmoteEngine.h:721-725) commented "Aligned to 0x67D01C container order" — but 0x67D01C steps the TYPED deques (_stateMachineDeque4 etc), NOT these legacy deques. grep confirms NO push/emplace into _typeNControllerAnimators anywhere -> they are PERMANENTLY EMPTY -> this loop + stepQueuedAnimatorLike_0x67D01C + PlayerCore.cpp:116-156 get/find/erase/clear helpers are a DEAD residue of the 06-03 parallel model (uncleaned after switch to HM6-index). Disguised as 0x67D01C-aligned (misleading comment). RECOMMEND (not done): delete legacy deques+bucket loop+PlayerCore helpers, or relabel as dead and fix the "Aligned to 0x67D01C" misattribution. 0x67D01C stepping is faithfully carried by EmoteEngine::progress typed-deque loop.

## 🟡 minor (carried from 06-04, not re-verified this session)
- sub_67C6B0 mirror-flag: local uses exact std::find vs binary ttstr_indexOf<1 substring + +824/+880 memo cache + +1158 gate. superstring-label semantic gap (MEDIUM, motion-data dependent).
- sub_660028 search() ~1925-line DFS structurally spot-checked only.
