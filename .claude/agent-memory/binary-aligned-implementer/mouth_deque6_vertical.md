---
name: mouth-deque6-vertical
description: M2 EmoteEngine mouth/deque#6 vertical DONE — EmoteMouthController(0x70) ctor(0x665C98)+step(0x666068)+builder(0x66CFBC). Records the 24B/double-HM6 mouth specifics AND the powField RAW-FLOAT-BITS trap (which the eye/eyebrow siblings get WRONG).
metadata:
  type: project
---

# Mouth / deque#6 vertical (M2) — DONE 2026-06-03

Files added: `cpp/plugins/motionplayer/EmoteMouthController.{h,cpp}`.
Touched: EmoteEngine.{h,cpp} (deque#6 element type EmoteMouthControlEntry_Deque6,
buildMouthControl, progress step loop, dtor), PlayerCore.cpp loadFromSnapshot,
both CMakeLists (motionplayer + platforms/wasmtime). Built green web/debug +
krkr2_wasmtime_guest, 0 errors. No oracle/fixture (logo differential does not
exercise mouthControl) — evidence + build + line-by-line decompile/disasm audit.

## Function -> local map
- `EmoteMouthController_ctor @0x665C98` -> `EmoteMouthController_ctor`
- `EmoteMouthController_step @0x666068` (was sub_666068) -> `EmoteMouthController_step`
- `EmoteEngine_buildMouthControl @0x66CFBC` -> `EmoteEngine::buildMouthControl`
- progress deque#6 loop @0x67d168 -> EmoteEngine.cpp progress() (replaced STUB_WARN)

## EmoteMouthController (0x70=112B) — SMALLEST control leaf (vs eye 0x170, eyebrow 0x150)
Same SHAPE as EmoteAngleController (0x70: 12B-elem deque @+0..+79 + scalar machine
@+80..+108) but DIFFERENT field semantics + DIFFERENT step, so it is a SEPARATE
named-field class. Despite the name "mouth" there is NO lipsync/audio/volume — the
step is a plain value-track scalar ramp. NO blink machine, NO RNG, NO 8B secondary
track, NO edge/node mesh tables, **NO sub_661F7C/660028 mesh resolver call** (so —
unlike eye/eyebrow — mouth has NO open mesh boundary; it is structurally complete).

Field table (disasm-verified at 0x666068):
- +0..+79 valueTrack12B (EmoteAngleController_ctor_12Bdeque; elem {float endVal@+0,
  float dur@+4, u32 powCount@+8}; 504-block = 21 elems/block)
- +80 state(0 idle/setup, 1 animating)  +84 currentValue(out *a3)  +88 endVal(target)
- +92 accum(0->1, +=invDur*dt, clamps 1.0)  +96 invDur(=1/dur)
- +100 powField(RAW float bits of kf.powCount — see TRAP)  +104 startVal  +108 beginFrame(int, out *a2)

ctor reads ONLY "beginFrame"(+108). state==0 setup snapshots start=current,
endVal=kf[+0], invDur=1/kf[+4], accum=0, powField=kf[+8]bits, state=1, pop_front.
state==1 inline animate; clamp >=1.0 -> current=endVal, state=0, accum=1.0.
NO state 2 (eyebrow has state 2; mouth animates inline in state 1).

## TRAP: powField is RAW FLOAT BITS, not int->float convert
Setup `STR W10,[X20,#0x64]` stores kf.powCount(u32) raw; animate `LDR S1,[X20,#0x64]`
reads it as a **float with NO SCVTF**. So +100 = bit_cast<float>(kf.powCount), used
directly as the pow() exponent. Local mouth uses `std::memcpy(&powField,&kf.powCount,4)`
NOT `(float)powCount`. **The eye(663BDC)/eyebrow(665600) ports do this WRONG** — they
use `static_cast<float>(trackPow)`. Disasm 0x6656cc confirms eyebrow ALSO does
`LDR S2,[X19,#0x140]` (raw float, no SCVTF). Eye/eyebrow trackPow is therefore a
latent deviation (out of scope here; flag for a future correction pass). Mouth is correct.

## deque#6 element = 24B {EmoteMouthController* ctl; ttstr label; ttstr talkLabel}
The ONLY controller-deque with a THIRD ttstr. Builder 0x66CFBC inserts HM6 TWICE for
ONE controller: label->{type6,index=v5} AND talkLabel->{type6,index=v5}. Progress
loop @0x67d168: step(*v30, &outBeginFrame, &outCurrentValue, dt); HM7[label]=outBeginFrame
(=(float)beginFrame); HM7[talkLabel]=outCurrentValue(=current+84); advance v30+=3 (24B);
block boundary v32=block+63 (63 qwords=504B). enabled-gate + index=loop-v5 same as eye/eyebrow.

## Step output mapping (the mouth-unique double feed)
*a2/outBeginFrame = (float)beginFrame(+108) every step (never animated) -> HM7[label].
*a3/outCurrentValue = currentValue(+84) (the ramp output) -> HM7[talkLabel]. So "label"
key gets the static beginFrame scalar; "talkLabel" key gets the animated value.

## Wiring point
PlayerCore.cpp loadFromSnapshot after eyebrow: metadata["mouthControl"] PSBList ->
buildMouthControl, fresh-build (drop+delete prior deque#6 ctls first), same as eye/eyebrow.

## REMAINING OPEN (list-only): transition(deque#7,type7,0x66D4C4),
selector(deque#8,0x80,sub_6681E4,type8,48B), deque#9(sub_668470 48B vector var),
deque#10(inline curve lookup 16B), bust/hair/parts(deque#1/2/3 sub_66B9D0),
timelineControl(HM3), variableList, clamp/mirror/loop/instantVariable, and the shared
sub_661F7C/660028 mesh resolver (feeds eye+eyebrow 8B tracks; mouth does not use it).
