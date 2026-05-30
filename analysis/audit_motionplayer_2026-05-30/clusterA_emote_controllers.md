# Cluster A — Emote low-level controllers & physics — Alignment Audit

> Date: 2026-05-30
> Authoritative source: libkrkr2.so (IDB libkrkr2.so.i64, via IDA MCP)
> Scope: EmotePhysics_springStep, EmoteAngleController_{step,ctor}, EmoteVarController_{step,ctor,deque-init},
>        plus cluster neighbors sub_662968 / sub_66480C / sub_6661A8 / sub_666830 / sub_666A14 / sub_66713C / sub_667490.
> Method: decompile each; line-by-line dataflow compare vs cpp/plugins/motionplayer/.
> Read-only on cpp/. IDB rename/fix allowed.
> Predecessor: analysis/MotionPlayer_Restoration_Review_2026-05-30.md (class-layout layer). This audit is the
>   dataflow/control-flow layer and supersedes that report's controller findings where noted.

---

## 1. EmotePhysics_springStep @ 0x662768  —  FINDING: MISSING (P1 gap, not a live bug)

### Binary pseudocode (real behavior)
```
float springStep(self, float* outA /*a2*/, float* outB /*a3*/,
                 float fx/*a4*/, float fy/*a5*/, float a6, float a7,
                 float dt/*a8*/, float a9, float ang/*a10*/):
  if (*(byte*)self) {                       // init flag @+0
     v18=+36; v19=+40; *(byte)self=0;
     *(self+28)=v18-fx; *(self+32)=v19-fy;  // store rest offset
  } else {
     v18=+28+fx; v19=+32+fy; *(self+36)=v18; *(self+40)=v19;
  }
  s = sinf(-ang); c = cosf(ang);
  pos=+48,+52,+56 ; k=+8*dt ; damp=+12*dt ; mass=+4*dt
  // accel = rotated(force)*mass + k*(targetPos - pos) + gravity terms(+60,+64,+68)
  acc.x = c*a6 - s*a7 ... ; acc.y = s*a6 + c*a7 ... ; acc.z uses +44,+68
  acc -= damp*acc                            // velocity damping
  *(+60)=acc.x; *(+64)=acc.y; *(+68)=acc.z
  vel/pos integrate: *(+48..56) += acc*dt
  *outA = atanf(-(dx*a9)*(+20) * 0.04516) / 0.03927   // +20 stiffnessA, magic deg-scale
  *outB = atanf((-(dy*a9) - (+16))*(+24) * 0.04516) / 0.03927  // +24 stiffnessB, +16 bias
  return *outB
```
Two-body damped spring with rotation-of-external-force, integrating a 3-component state (+48/+52/+56),
producing two `atanf(...)`-normalized angular outputs. Constants: 0.0451603944 and 0.0392699082 (= pi/80).
Magic offsets read: +4 mass, +8 k, +12 damp, +16 bias, +20 stiffnessA, +24 stiffnessB, +44 restZ.

### Local counterpart
NONE. `grep` for springStep/atanf/sinf/cosf/0.0451603944 across cpp/plugins/motionplayer/ returns nothing.
`PlayerCore.cpp:865` explicitly: "Full spring simulation not yet implemented for web port — store params only."
The MotionTraceWeb.cpp sinf/isnan hit is unrelated (NaN guard, not spring).

### Findings
- **A-1 / P1 / MISSING**: EmotePhysics_springStep has no port. The whole hair/bust/parts physics integration
  (called from progress @ sub_666BF8-adjacent loop, see EmotePlayer_Internal_Implementation.md:653-662) is absent.
  Classified P1 (structure gap) not P0: there is no *wrong* local dataflow to diverge — it is simply unimplemented,
  and PlayerCore.cpp:865 honestly marks it. To restore 1:1, port this function verbatim incl. the init-flag branch,
  the 3-state integration, both atanf outputs, and the two pi/80 magic constants. Do NOT collapse the two outputs.

---

## 2. EmoteAngleController_step @ 0x666634

### Binary pseudocode (real behavior)
```
void step(self, int* out /*a2*/, float dt):
  v4 = state(+80)
  if (v4 == 1) {                            // ANIMATE branch
     phase(+104) = invDur(+96)*dt + phase
     if (phase >= 1.0) { phase=1.0; r=cur(+88); wrap r into[0,2pi); cur(+84)=r; state=0; }
     else { r = pow(phase, powCount(+100)) * (target(+88) - start(+92)) + start(+92);
            wrap r into [0,2pi); cur(+84)=r; }
  } else if (v4 == 0) {                      // SETUP branch — MUTUALLY EXCLUSIVE with v4==1
     front = deque.front (+16); if (deque empty) skip;
     start(+92) = cur(+84)
     t = front.endRad ; v9 = (t > cur)
     target(+88) = t                          // raw first
     if (v9) { if (t-cur > pi)  target(+88)=t-2pi; }   // shortest-path
     else    { if (cur-t > pi)  target(+88)=t+2pi; }
     state(+80)=1
     invDur(+96)=1.0/front.duration(+4); powCount(+100)=front.powCount(+8)
     deque.pop_front (free block if last)
     // NO phase reset here; NO fall-through into ANIMATE this call
  }
  *out = cur(+84)                            // single int/float scalar
```
Element 12B: {endRad@0, duration@4, powCount@8}. Deque block stride 12, 504-byte data span (=42 elem).
KEY: setup and animate are exclusive on the OLD v4. After setup, the function writes `*out=cur` and returns;
the first phase advance happens on the NEXT call. Also note: the WRAP into [0,2pi) is applied to the *interpolated
current* (cur+84) on every animate tick — wrap lives inside the animate branch, not after.

### Local counterpart: cpp/plugins/motionplayer/EmoteAngleController.cpp:39-81
```
if (state==0) { ... setup ...; phase=0.0f; state=1; }   // :40-64
if (state==1) { phase += invDuration*dt; ... }          // :65-76   <-- re-reads state, runs SAME call
... fmod wrap applied to cur AFTER both blocks ...       // :77-80
```

### Findings
- **A-2 / P0 / dataflow+control-flow**: Local uses two **sequential** `if(state==0)` then `if(state==1)` so a
  freshly-popped keyframe is **interpolated in the same step()** (phase advances on the setup call). Binary uses
  `if(v4==1) … else if(!v4) …` on the cached OLD state → after popping, it returns with `*out = cur` (un-advanced)
  and only advances on the next call. This is a one-frame phase-timing divergence on every keyframe boundary.
  Fix: branch on a cached `int v4 = state;` and make the two arms mutually exclusive (no fall-through).
- **A-3 / P0 / extra computation step**: Binary resets phase to 0 *implicitly* by never touching +104 in the setup
  arm (it was left wherever the prior animate left it — but since state only reaches 0 via the phase>=1 path which
  sets phase=1.0, the next setup starts from phase=1.0, NOT 0.0). Local explicitly sets `phase=0.0f` at :62.
  Re-examine: in binary, after a completed animation phase(+104)=1.0 (1065353216) and state=0; the setup arm does
  not reset it; next animate does `phase = invDur*dt + 1.0` → immediately >=1.0 → snaps to target in one tick.
  Local's `phase=0.0f` makes it ramp from 0 instead. **This is a genuine interpolation-shape divergence.** Verify
  against EmoteVarController (which DOES write +116=0 at 0x666dcc) — the Angle variant deliberately omits that
  write. Fix: remove the `self->phase = 0.0f;` at :62 to match 0x666634 (no +104 store in setup arm).
- **A-4 / P2 / wrap placement**: Local applies fmod-wrap once after the merged block (:77-80); binary applies the
  `for(;r<0;r+=2pi) for(;r>=2pi;r-=2pi)` normalization *inside* each animate sub-branch (two copies). Numerically
  equivalent for finite r; semantic/structure-only. Note binary uses iterative add/sub of 6.2832 (a truncated 2pi
  literal = 6.2832, NOT 6.28318531), local uses std::fmod with kTwoPi=6.28318530718. **Minor numeric drift**: the
  loop constant 6.2832 differs from true 2pi at the 5th digit → results diverge slightly. Reclassify borderline
  P2/P1. Recommend matching the 6.2832 literal and the iterative wrap for byte-fidelity.

---

## 3. EmoteAngleController_ctor_12Bdeque @ 0x6867B0  —  ALIGNED (deque-init helper)

### Binary
Pure libstdc++ `std::deque<12B>` constructor: computes block count `a2/0x2A+3` (42 elem/block, min 8),
`operator new(8*blocks)` for the map, places start/finish iterators, element data block stride 504 (=42*12),
calls sub_6868C8 to allocate the node block(s). NO animation-state writes (those are the caller's memset+0 job).

### Local: EmoteAngleController_ctor (EmoteAngleController.cpp:22-32)
Zeroes all animation-state scalars (state/cur/target/start/invDur/powCount/phase/pad) and relies on std::deque
default-ctor for the queue.

### Findings
- **A-5 / P1 / container**: Local models the deque as `std::deque<EmoteAngleKeyValue12B>` (libc++ ABI). The binary
  is libstdc++ (80B header, 42-elem/504B blocks, explicit map+8 block-pointer growth visible in pop_front at
  0x666804). This is a flagged PLATFORM_BOUNDARY (header.h:36-39) — element semantics (front-pop / push-back,
  12B element) preserved; byte-offset equality unreachable on libc++. Accept as boundary, NOT counted as deviation.
- **A-6 / P2**: Binary ctor does the deque init only; the caller (sub_662968 / sub_66480C) does the `memset(self,0,0x50)`
  BEFORE the call and zeroes animation state AFTER. Local merges the animation-state zeroing into the ctor. Harmless
  (eliminates UB), already noted in predecessor review row "仅 ctor 多做无害清零". P2.

---

## 4. EmoteVarController_step @ 0x666BF8

### Binary pseudocode (real behavior)
```
void step(self, float* out /*a2*/, float dt):
  v4 = state(+84)
  if (v4 == 0) {
     front = +16; if (+48==front) goto WRITE_OUT;     // empty
     count = +80
     for i in [0,count): target(+96)[i]=cur(+88)[i]; start(+104)[i]=front[i] (*(float)(front+4i))
     state(+84)=1
     invDur(+120)=1.0/front.duration(+12); powCount(+112)=front.powCount(+16)
     pop_front (+16+=20, or free block & advance map)
     phase(+116)=0.0
     if (v33 /*=state-after-pop, ==1 in normal path*/ != 1) goto WRITE_OUT; else goto ANIMATE
  } else if (v4 != 1) goto WRITE_OUT;
ANIMATE:
  phase(+116) += invDur(+120)*dt
  if (phase>=1.0){ phase=1.0; for i: cur(+88)[i]=start(+104)[i]; state=0; }
  else { f=powf(phase,powCount); for i: cur(+88)[i]=target(+96)[i]+f*(start(+104)[i]-target(+96)[i]); }
WRITE_OUT:
  for i in [0,count): out[i]=cur(+88)[i]
```
Element 20B: {channel[0..count)@0/4/8…, duration@+12, powCount@+16}. count = +80. 3 heap arrays of `count` floats.
KEY: binary DOES fall through setup→animate in the same call (v33==1 branch at 0x666dd4 → LABEL_24).
Array roles: +96 ("target") = lerp source (snapshot of old cur); +104 ("start") = lerp dest (element channels).

### Local counterpart: cpp/plugins/motionplayer/EmoteVarController.cpp:83-129
```
count = self->count;                                   // :84
if (state==0){ ... setup; state=1; invDur=1/dur; pow; pop; phase=0; }   // :85-104, falls through
else if (state!=1) goto write_out;                     // :105-107
// state==1 update :109-123
write_out: for i out[i]=cur[i];                         // :125-128
```

### Findings
- **A-7 / P0-RESOLVED (was P0-1 in predecessor)**: The 4x over-allocation bug (`new float[count*4]`) flagged in
  MotionPlayer_Restoration_Review_2026-05-30.md §P0-1 is **FIXED** in current code: ctor :32-34 now `new float[count]()`,
  step loops :95/:113/:119/:126 all bound by `count` = +80. Matches ctor 0x667030 (`operator new[](4*count)` bytes =
  count floats) and step 0x666BF8 (all loops bound by +80). Dataflow now aligned. (Recorded for traceability.)
- **A-8 / P2 / control-flow shape — ALIGNED**: Local's fall-through (setup → state==1 update same call) MATCHES
  binary (v33==1 path). Unlike the Angle variant, the Var binary genuinely falls through, AND it writes phase=0 at
  +116 (0x666dcc) before the animate. Local :103 `phase=0.0f` + fall-through is correct here. Good — confirms the
  Angle-variant divergence (A-2/A-3) is real and variant-specific, not a copy error.
- **A-9 / P2 / element channel aliasing**: Binary reads element channels as `*(float*)(front+4*i)` for i in [0,count).
  For count==4 (color RGBA) i=3 aliases duration@+12; binary is fine because color keyframes presumably store 4
  channels and read duration from a different element slot? — No: for count==4 the read at front+12 IS duration.
  Local replicates byte-for-byte via `reinterpret_cast<const float*>(&elem)` indexing [0,count) (:94-97), so it
  aliases identically. Faithful. The header struct `channel[3]` (header :48) only declares 3 but local indexes by
  raw pointer so count==4 still reads +12. P2 (comment/struct-decl clarity only).
- **A-10 / P1 / container**: same std::deque libc++ vs libstdc++ PLATFORM_BOUNDARY as A-5 (20B element, 25/block,
  500B span). Flagged boundary in header :56-62. Not counted.
- **A-11 / P1 / heap-array dtor**: Local dtor (:131-135) `delete[]` x3. Binary's EmoteVarController dtor was not
  separately decompiled here; ctor uses `operator new[]` x3 so `delete[]` x3 is the matching ABI cleanup. Acceptable;
  flag for a future dedicated dtor decompile to confirm there is no per-element teardown (there isn't — POD floats).

---

## 5. EmoteVarController_ctor_20Bdeque @ 0x667030  —  ALIGNED

memset(self,0,0x50) → deque-init (0x6878D8) → v5 = is_mul_ok(count,4)?4*count:-1 → +80=count,+84=0 →
3× operator new[](v5) at +88/+96/+104 → 3× memset(.,0,4*count). Local ctor :26-39 replicates exactly
(modulo the std::deque-default-ctor standing in for memset+0x6878D8 under the boundary note). Aligned. P2 only:
local relies on `new float[count]()` value-init instead of explicit memset — equivalent for floats.

## 6. EmoteVarController_deque20B_init_guess @ 0x6878D8  —  container helper, ALIGNED-by-boundary
libstdc++ deque<20B> first-block init: blocks=`a2/0x19+3`(25 elem/block) min 8, map=operator new(8*blocks),
data span 500 (=25*20), element stride 20, growth via map+8. Pure container internals subsumed by std::deque
under PLATFORM_BOUNDARY. No local 1:1 (nor should there be on libc++).

---

## 7. Cluster neighbors — classification

| addr | role | deep-audit? | note |
|------|------|-------------|------|
| sub_662968 @ 0x662968 | **EmotePlayer blink-controller ctor** (logic) | scanned | Builds EmoteAngleController(+0) + a Var-like ctrl(+80) + a 3rd deque(+184); reads TJS dispatch keys `beginFrame/endFrame/blinkIntervalMin/blinkIntervalMax/blinkFrameCount/blinkEnabled/edge/node`. Random blink interval via sub_9F17D0. Populates std::vector<pair> (+168/+176) and inline deque-of-vector (+184). **Not a Cluster-A controller-step**; it is the *owner* ctor. Out of owned scope but mapped. |
| sub_66480C @ 0x66480C | **sibling ctor** (logic, slimmer) | scanned | Same shape as 662968 minus blink fields (only `beginFrame/edge/node`). A second controller-owner variant. Out of owned scope. |
| sub_6661A8 @ 0x6661A8 | **TJS member-registrar** (trampoline-ish) | scanned | Registers props phase/mouth/frame/prev/target/tick/exponent/speed at self+80..+108 via sub_66441C/sub_664524. NCB property binding, no math. |
| sub_666830 @ 0x666830 | **TJS member-registrar** (variant) | scanned | Same family: phase/tick/speed/exponent/frame/prev/target via sub_5A6020/sub_5A614C, flags 512. |
| sub_666A14 @ 0x666A14 | **TJS member-registrar** (variant) | scanned | phase/tick/speed/exponent/frame/prev/target; note prev & target BOTH bind to a1+92 (v5). Registrar only. |
| sub_66713C @ 0x66713C | **VarController reset/rewind** (logic) | scanned | Restores cur(+88) from start(+104) or deque-front-prev(+...-20), clears state(+84)=0, then frees all deque overflow blocks and resets deque iterators to the single first block (a re-init/clear). A "reset to first keyframe" helper. Logic but a state-reset, not a per-frame step. |
| sub_667490 @ 0x667490 | **VarController deque push_back** (container) | scanned | `_M_push_back`-style: writes element {channels[0..count), duration@+12, powCount@+16} into deque finish slot, grows a new 0x1F4(500)-byte block + map entry when full. Pure container insert. |

None of the neighbors is a second copy of a Cluster-A *physics/interpolation* logic function; the two true logic
functions owned (AngleController_step, VarController_step) plus the absent springStep are the substance.

---

## Severity roll-up

| id | binary func @addr | local file:line | sev | one-line |
|----|-------------------|-----------------|-----|----------|
| A-1 | EmotePhysics_springStep @0x662768 | (none) PlayerCore.cpp:865 | P1 | MISSING — spring physics unimplemented, honest stub |
| A-2 | EmoteAngleController_step @0x666634 | EmoteAngleController.cpp:40-76 | P0 | sequential if/if runs setup+animate same call; binary's exclusive if/else-if advances phase one frame later |
| A-3 | EmoteAngleController_step @0x666634 | EmoteAngleController.cpp:62 | P0 | local writes phase=0 in setup; binary never resets +104 → keyframe snaps from phase=1.0, different interp shape |
| A-4 | EmoteAngleController_step @0x666634 | EmoteAngleController.cpp:78 | P1 | wrap uses true 2pi via fmod; binary iterates 6.2832 literal twice in-branch → numeric drift |
| A-5 | EmoteAngleController_ctor @0x6867B0 | EmoteAngleController.h:42 | P1(bnd) | std::deque libc++ vs libstdc++ — flagged PLATFORM_BOUNDARY |
| A-6 | EmoteAngleController_ctor @0x6867B0 | EmoteAngleController.cpp:22-32 | P2 | ctor folds anim-state zeroing caller did separately (harmless) |
| A-7 | EmoteVarController_ctor/step @0x667030/0x666BF8 | EmoteVarController.cpp:32 | P0-RESOLVED | prior 4x over-alloc bug now fixed (new float[count]) |
| A-8 | EmoteVarController_step @0x666BF8 | EmoteVarController.cpp:85-107 | P2 | fall-through setup→animate correctly MATCHES binary (variant-specific) |
| A-9 | EmoteVarController_step @0x666BF8 | EmoteVarController.cpp:94 | P2 | element channel aliasing (count==4 reads +12) replicated faithfully |
| A-10 | EmoteVarController_ctor @0x667030 | EmoteVarController.h:65 | P1(bnd) | std::deque boundary, same as A-5 |
| A-11 | EmoteVarController_dtor | EmoteVarController.cpp:131 | P1 | delete[] x3 plausible; binary dtor not decompiled — confirm later |

### Cluster verdict
**minor drift** — Two true Cluster-A logic ports exist and are structurally faithful (POD, deque, free-function,
manual heap). The predecessor's P0 4x over-alloc is fixed. Remaining real issues are localized to
EmoteAngleController_step's control-flow (A-2 same-call fall-through, A-3 phase-reset, A-4 wrap constant) — direct
patches on the existing dataflow, no architectural rework needed. The one structural gap is the unimplemented
EmotePhysics_springStep (A-1), which is a MISSING port rather than a divergence.
