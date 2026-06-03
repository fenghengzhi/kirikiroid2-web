---
name: setvariable-dispatch-keystone
description: EmoteEngine::setVariable (0x671228) HM6-lookup->switch->controller-enqueue keystone — object identity, enqueue semantics, HM2-map fork, selector +16 indeterminacy
metadata:
  type: project
---

setVariable value-dispatch keystone DONE 2026-06-03 (cpp/plugins/motionplayer/EmoteEngine.{h,cpp} + EmotePlayer.cpp).

**Object identity CORRECTION**: 0x671228 (IDA auto-name "Player_setVariable") `this` is the **EmoteEngine** (~1576B), NOT motion::Player. Proven by offsets: HM6@+1384 (=_scalarHM6_1384), HM2@+1440 (=_labelToValueHM7), controller deques @+256(eye)/+336(brow)/+416(mouth)/+576(transition=_auxVarDeque8)/+656(selector=_vectorVarDeque9), flag bytes +1159(_syncWaiting)/+1161(_emoteAnimatorFlag, MOVED from Player)/+1162(_dirty). Local pre-existing Player::setVariableResolvedWeightLike_0x671228 is a NON-faithful reimpl on wrong object (uses _activeMotion->controllerBindings, not engine HM6) — left in place for getVariable-cascade compat (see fork below).

**Dispatch structure**: HM6.find(key) -> if hit, ref={type@+0,index@+4} (inline map value, NOT pointer — buildEyeControl 0x66ca28 writes *ret=4,ret[1]=idx). _dirty=1. switch(ref.type): 0/1/2= if(_syncWaiting) HM2-write else return; 4=eye enqueue; 5=brow; 6=mouth dual-key; 7=transition; 8=selector. miss OR (0/1/2 w/ syncWaiting) -> _labelToValueHM7[key]=value.

**Arg mapping** (binary != TJS wrapper): Player_setVariable(this,key, value=d0, easing=d1, durationFrames=d2). TJS setVariable(label,value,transition,ease) -> value=value, easing=TJS"transition"(d1, the INSTANT GATE), durationFrames=TJS"ease"(d2 -> v22 factor). enqueue receives (value, easing, v22) all FCVT'd S0/S1/S2. v22 = df==0?1.0 : df>0?df+1.0 : 1/(1-df) (=variableEaseWeightLike_0x671228).

**5 enqueue fns** (6638B0 eye / 6652D4 brow / 665E34 mouth / Animator_setKeyframes 0x667300 transition / 6681E4 selector): all push a transition keyframe into the controller's INTERNAL std::deque (the libstdc++ deque IDA indexes at a1+16..+72). Pattern: if(easing<=0){clear queue(s)+snap scalar+state=0} elif(flag&1){append, NO clear} else{clear+state=0+append}. Element = float-triple {value, easing, v22} stored as {endRad/channel0, duration, powCount=RAW FLOAT BITS via memcpy — LDR S no SCVTF}. eye/brow clear TWO deques (+16 valueTrack12B, +96 valueTrack8B) on instant; mouth/transition/selector clear ONE. mouth instant writes currentValue+84/state+80; selector instant calls applySelection(ctl,(int)value,0,0).

**case6 mouth dual-key** (disasm 0x6715c4-0x6716b4): if key==elem.label -> ctl->beginFrame(+0x6C=108)=(int)value via FCVTZS (truncate, C++ static_cast<int> matches); elif key==elem.talkLabel -> sub_665E34 enqueue; else nothing. (ptr-eq then wcscmp sub_9B1ED0.)

**case7/case8 gate**: both read LDRB[elem+16];CBNZ. transition elem.flag explicitly =1 (builder). SELECTOR elem+16 INDETERMINATE — buildSelectorControl (0x66ddac-0x66de1c) writes only +0(ctl)/+8(label)/+24/+32/+40, leaves +16 from raw operator new(0x1E0). Modelled selector elem.flag default=1 (matches transition sibling + non-zero new-mem). logo has no selector vars => inert.

**HM2-MAP FORK (open, M3-scope)**: getVariable reads Player-side _evalResultValues/HM1/HM4 cascade (binary getVariable@0x6D69C8, M3/R0-1), NOT engine _labelToValueHM7(+1440) that 0x671228 writes. In binary these are the SAME HM2; locally TWO disjoint maps. Until unified, D3DEmotePlayer::setVariable calls BOTH engine().setVariable (controller activation + binary HM2) AND player().setVariable (getVariable compat). Unit test l.639 setVariable("manual",3.5)->getVariable==3.5 PASSES (relies on player path). NOT a faithful single-dispatch; documented boundary.

**bust/hair target/const feed NOT in 0x671228**: cases 0/1/2 only write HM2 (+1159 gated). _ctlBust1/2Target/_bustSpring1/2Const fed by a SEPARATE un-ported pass (bind-loop sub_67C560 etc.), NOT setVariable. Brief premise "setVariable sets bust/hair targets" is WRONG for 0x671228. REMAINING.

**Build**: web/debug + wasmtime krkr2_wasmtime_guest both clean (grep error:0), no new .cpp (only edits). macos motionplayer-dll test has PRE-EXISTING drift (contains 2-arg vs 3-arg, TimelinePlayFlagSequential missing) unrelated to this change — cannot fresh-run that harness.

REMAINING OPEN: HM2-map unification (M3); bust/hair target+const feed pass; sub_661F7C mesh resolver; clamp/mirror/instantVariable/timeline builders.
