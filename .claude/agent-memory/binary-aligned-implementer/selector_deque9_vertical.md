---
name: selector-deque9-vertical
description: M2 EmoteEngine selector vertical DONE — EmoteSelectorController(0x80) ctor(0x66E398)+step(0x668470)+applySelection(0x6680B0)+builder(0x66D8FC). CORRECTS the brief's "deque#8 step=sub_666BF8" — selector lives @engine+656 (local _vectorVarDeque9), step=sub_668470; sub_666BF8 is the TRANSITION step @engine+576.
metadata:
  type: project
---

# Selector vertical (M2) — DONE 2026-06-03

Files added: `cpp/plugins/motionplayer/EmoteSelectorController.{h,cpp}`.
Touched: EmoteEngine.{h,cpp} (deque#9 element type EmoteSelectorControlEntry_Deque9,
buildSelectorControl, progress step loop, dtor), PlayerCore.cpp loadFromSnapshot,
both CMakeLists (motionplayer + platforms/wasmtime). Built green web/debug +
krkr2_wasmtime_guest, 0 errors (grep). No oracle/fixture (logo motion_playback
does not exercise selectorControl) — evidence + build + line-by-line disasm audit.

## BIG CORRECTION — deque numbering & which step is the selector
The brief said "selector = deque#8 @engine+656, progress step = sub_666BF8". The
"@engine+656" is RIGHT but "step=sub_666BF8" is WRONG. Fresh decompile of
EmoteEngine_progress @0x67D01C proves:
- engine+656 (header base +640) -> step **sub_668470** (48B stride v38+=6, block
  480 = node+60). THIS is the selector. = local member `_vectorVarDeque9`.
- engine+576 (header base +560) -> step **sub_666BF8** = EmoteVarController_step,
  24B stride, block 504. THIS is the TRANSITION (#7). = local `_auxVarDeque8`.
Binary steps SELECTOR(+656) BEFORE TRANSITION(+576) in progress; local loop order
was swapped to match. The local member NAMES (Deque8/Deque9) are engine-member
ordinals; the brief's "deque#8" is the 1-based controller-deque ordinal. engine
OFFSET is the only unambiguous ID. (sub_6681E4 = setVariable case8 reader = the
keyframe pusher onto the selector's 12B command track + applySelection on a4<=0.)

## EmoteSelectorController (0x80=128B), ctor 0x66E398 (memsets 0x50 then deque ctor)
Shares the EmoteAngleController 12B-deque base (+0..+79) as a COMMAND track, plus:
- +80 baseState (the base track's slot; selector step does NOT use it)
- +84 selState (0 setup/idle, 1 animating)
- +88 selectedIndex (int; output -> *out as (float)int via SCVTF)
- +92 invDuration  +96 accum  +100 pad
- +104/+112/+120 std::vector<SelectorOption16B> optionList (ctor swaps it in)
SelectorOption16B = {EmoteVarController* refCtl@+0; float offValue@+8; float onValue@+12}.

## step sub_668470 (disasm-verified 0x6684dc)
state0 setup pops 12B cmd {selIdx@+0,dur@+4,fade@+8}: `LDP S10,S8` + `LDR S9` —
fade is **RAW float bits** of elem[+8] (LDR S, NO SCVTF) -> memcpy(&fade,&kf.powCount).
`FCVTZS W1,S10` => selIdx=(int)float. applySelection(self,(int)selIdx,dur,fade);
invDur(+92)=1/dur; selState(+84)++; accum(+96)=0. state1: accum += invDur*dt (store
BEFORE compare); if accum>=1 -> accum=1.0, selState=0. out = (float)selectedIndex(+88).

## applySelection sub_6680B0 (disasm-verified)
selectedIndex(+88)=index; per option (16B): if(refCtl){ value = (i==selectedIndex)?
onValue:offValue; EmoteVarController_step(refCtl,&cur,0); delta = cur - value
(FSUB S0,S0,S3 — current minus value; sign only matters structurally, both uses
fabs); if(refCtl.state!=0 || refCtl.queue nonempty || |delta|>=1e-7)
Animator_setKeyframes(refCtl,&value,clearFirst=0, |delta/(onValue-offValue)|*dur, fade) }.

## Animator_setKeyframes @0x667330 (ported as file-local helper in the .cpp)
dur<=0: queue.clear()+state=0+copy count floats values->currentValue(+88).
dur>0: if(!clearFirst){queue.clear();state=0;} push 20B kf {values, dur, pow-bits}.
pow stored via memcpy(&kf.powCount,&pow) = RAW float bits (downstream
EmoteVarController_step reads +112 with LDR S, raw).

## INERT boundary (documented, NOT a defer)
option.refCtl is resolved by the BUILDER searching the TRANSITION deque (engine+576,
local _auxVarDeque8) for an element whose label matches the option's "label". That
category is still open (builder 0x66D4C4 not ported) => deque empty => every refCtl
resolves null (binary's own v26=0) => applySelection skips all options (its
`if(option.refCtl)` guard). Selector's OWN state machine + HM7 index output are
fully LIVE. Only the cross-controller keyframe push is inert pending transition.
Same null-guard, same skip = 1:1 with binary. Build-order dep, not missing compute.

## PRE-EXISTING latent bug found (OUT OF SCOPE, flagged): EmoteVarController.cpp
sub_666BF8 reads powCount as RAW float bits (binary: STR raw + LDR S no SCVTF), but
local EmoteVarController.cpp:101/118 uses static_cast<float>(int). Same class of bug
as eye/eyebrow (mouth memory). Transition is open; my Animator_setKeyframes feeds
raw bits correctly, but the downstream reader would misconvert. Left for the
transition vertical to fix (path is inert: refCtl always null here).

## REMAINING OPEN (list-only, unchanged): transition(deque#7/+576,type7,0x66D4C4,
sub_666BF8 + EmoteVarController powCount raw-bits fix), deque#10 inline curve(+736,
16B), bust/hair/parts(deque#1/2/3 sub_66B9D0), timelineControl(HM3), variableList,
clamp/mirror/loop/instantVariable, setVariable READER dispatch (0x671228 case8
sub_6681E4 = selector value-write path), and the shared sub_661F7C/660028 mesh
resolver (eye+eyebrow).
