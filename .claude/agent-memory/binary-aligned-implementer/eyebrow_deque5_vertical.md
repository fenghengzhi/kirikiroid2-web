---
name: eyebrow-deque5-vertical
description: EmoteEngine M2 eyebrow/deque#5 slim controller vertical — slim ctor 0x66480C, step 0x665600, builder 0x66CB9C. Records slim(0x150) vs eye(0x170) field/step differences and the offset-swap trap.
metadata:
  type: project
---

# Eyebrow / deque#5 slim vertical — DONE 2026-06-03

Files: `cpp/plugins/motionplayer/EmoteEyebrowController.{h,cpp}` (new);
`EmoteEngine.{h,cpp}` (deque#5 element type + buildEyebrowControl + progress step + dtor);
`PlayerCore.cpp` loadFromSnapshot (metadata["eyebrowControl"] wire); both CMakeLists.

## Slim controller (eyebrow, 0x150) vs full (eye, 0x170) — the key rulings

- **Slim ctor 0x66480C reads ONLY "beginFrame" (+328) + "edge"/"node" arrays.**
  NO endFrame/blinkIntervalMin/Max/blinkFrameCount/blinkEnabled, NO RNG call.
  So eyebrow has NO blink state machine and NO blink fields — object is 0x20
  smaller than eye. trackValue(+300) seeded = (float)beginFrame at LABEL_90
  (always runs, even when edge/node empty).
- **Slim step sub_665600**: value-track machine states 0/1/2 then `*out = trackValue`
  DIRECTLY — NO blink-phase switch, NO [beginFrame,endFrame] remap (eye's LABEL_28
  is absent). eyebrow step NEVER reads +328.
- **State==1 in slim is a SINGLE `else if`, NOT a `while` loop** (eye's LABEL_24
  loops + gotos). After setting state=2, slim does NOT animate same-call — falls
  straight to `*out`. pop_front happens after the if/else, unconditionally (both
  v9==v8 and v9!=v8 branches advance the cursor).

## TRAP: value-track offset swap (slim vs eye) — do NOT share a base class

Slim and eye use the SAME 4 anim fields with **swapped offsets**:
| field   | eye (663BDC) | slim (665600) |
|---------|--------------|----------------|
| accum   | +316         | +312           |
| span    | +312         | +316           |
| pow     | +324         | +320           |
| invDur  | +320         | +324           |

Semantics identical (`pow(accum/span,1/pow)+invDur*dt`). Because offsets differ
AND blink fields genuinely don't exist, EmoteEyebrowController is a SEPARATE
class with named fields (not a shared base, not a forced merge). Named fields
make the offset swap a non-issue.

## Builder 0x66CB9C — same shape as buildEyeControl (0x66C77C) except:
new(0x150) slim ctl (not 0x170); push deque#5 (engine+320, a1[46..49], 16B
{ctl,label}, block 512); HM6 type=**5**, index=loop v5. enabled-gate identical.

## Progress step loop (EmoteEngine_progress 0x67D01C @ 0x67d10c-0x67d160)
deque#5 begin=*(engine+336); `sub_665600(*v23,&out,step)`; HM7[label]=out via
Player_HM2_upsert_labelToValue(+1440, v23+1); advance v23+=2 (16B). Parallel to
deque#4 (0x67d0a4).

## SCOPE BOUNDARY (shared with eye, INERT): sub_661F7C @0x661F7C -> sub_660028
slim step state-0 setup calls sub_661F7C(self+160,self+80,trackValue,endVal) =
1925-line edge-table mesh resolver. NOT ported (same open boundary as eye). 12B
track empty at runtime -> branch never taken; left as documented anchor.

## Verification gap: NO oracle/fixture. logo differential (motion_playback) does
NOT exercise eyebrowControl; no fixture triggers it. Evidence + build (web +
wasmtime guest both green) + structural alignment is the bar (CLAUDE.md:
oracle-inert is not a defer reason). Build grep: 0 error/undefined/unsupported.

## REMAINING (still open, list-only): mouth(deque#6,sub_666068,0x70,type6,
2x HM6 insert label+talkLabel), transition(deque#7,type7), selector(deque#8,
0x80,sub_6681E4,type8,48B elem), deque#9(sub_668470 vector var), deque#10
(inline curve lookup), bust/hair/parts(deque#1/2/3 node builders sub_66B9D0),
timelineControl(HM3), variableList, clamp/mirror/loop/instantVariable. And the
shared sub_661F7C/660028 mesh resolver (feeds both eye+eyebrow 8B tracks).
