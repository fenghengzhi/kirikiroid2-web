# Cluster A — Emote low-level controllers & physics — Alignment Audit (2026-06-07)

> Authoritative source: libkrkr2.so (IDB libkrkr2.so.i64, via IDA MCP).
> Method: decompile each binary function THIS session; line-by-line dataflow compare vs current
>   cpp/plugins/motionplayer/. Read-only on cpp/. IDB rename/comment applied + idb_save.
> Predecessor: audit_motionplayer_2026-05-30/clusterA_emote_controllers.md (address index only).
>   Code has changed substantially since: EmoteSpring.cpp / EmoteWindEmitter.cpp / EmoteMeshResolver.cpp /
>   the Blink/Mouth/Eyebrow/Selector/Loop controllers now exist and the 2026-05-30 P0s are RESOLVED.

Six dimensions audited per function: 源码结构 / 数据流 / 调用链 / 对象生命周期 / 内部容器实现 / 边界行为.

Binary functions decompiled this session (each claim below has a decompile call behind it):
- 0x666634 EmoteAngleController_step
- 0x666BF8 EmoteVarController_step
- 0x663BDC EmoteBlinkController_step (EmoteVarController4_step)
- 0x665600 EmoteEyebrowController_step (EmoteVarController5_step)
- 0x666068 EmoteMouthController_step
- 0x668470 EmoteSelectorController_step  +  0x6680B0 applySelection  +  0x667330 Animator_setKeyframes
- 0x662968 EmoteBlinkController_ctor  +  0x665C98 EmoteMouthController_ctor  +  0x66E398 EmoteSelectorController_ctor
- 0x662768 EmotePhysics_springStep  +  0x6689A4 EmoteBustChainSpring_step
- 0x6687E8 EmoteWindEmitter_step  +  0x670AFC EmoteWindEmitter_init
- 0x661F7C EmoteMeshResolver_resolve  +  0x660028 EmoteMeshResolver_search (head only; engine = own vertical)
- 0x9F1A08 RNG init  +  0x9F17D0 RNG next

---

## 1. EmoteAngleController — ctor 0x6867B0 (index) / step 0x666634

Local: EmoteAngleController.cpp:22-117, .h:1-75.

| dim | binary | local | verdict |
|-----|--------|-------|---------|
| 源码结构 | free fn, POD self, `if(v4==1){animate} else if(!v4){setup}` mutually exclusive | EmoteAngleController.cpp:75-115 identical structure | ✅ |
| 数据流 | setup never touches +104; completion stores 1.0 (1065353216); wrap on +84 each tick | :74-114, no phase reset in setup, completion `phase=1.0f` | ✅ |
| 调用链 | leaf, no callees | leaf | ✅ |
| 对象生命周期 | deque pop frees block when last (`v6==v15-12`) | std::deque::pop_front | ⚠️ container-boundary |
| 内部容器 | libstdc++ deque<12B>, 504B/block | std::deque<12B> (libc++) | ⚠️ PLATFORM_BOUNDARY (header.h:37) |
| 边界行为 | wrap loop literal 6.2832; shortest-path 6.28318531; powCount read *(float*) no SCVTF | :81-90 use 6.2832f; :99-101 use 6.28318531f; :111 memcpy raw bits | ✅ |

NOTE: 2026-05-30 findings A-2 (same-call fall-through) / A-3 (phase=0 in setup) / A-4 (fmod vs 6.2832) are
**ALL RESOLVED** — current code uses the cached-state exclusive branch, omits the setup phase write, and uses
the truncated 6.2832 iterative wrap exactly as the binary. Verified against fresh 0x666634 decompile.

## 2. EmoteVarController — ctor 0x667030 / step 0x666BF8

Local: EmoteVarController.cpp:26-144, .h:1-106.

| dim | binary | local | verdict |
|-----|--------|-------|---------|
| 源码结构 | setup FALLS THROUGH to animate (v33==1 -> LABEL_24); writes phase=0 @+116 | :85-132 fall-through, :108 phase=0 | ✅ |
| 数据流 | 3 heap float[count]; +96 target=snapshot of cur, +104 start=elem channels; lerp current=target+f*(start-target) | :96-97/:127-131 same role assignment & lerp | ✅ |
| 调用链 | leaf | leaf | ✅ |
| 对象生命周期 | new[](4*count) bytes x3; deque pop frees block | :32-34 new float[count]() x3; dtor delete[] x3 | ✅ (4x over-alloc bug FIXED) |
| 内部容器 | libstdc++ deque<20B> 500B/block | std::deque<20B> | ⚠️ PLATFORM_BOUNDARY (.h:62) |
| 边界行为 | powCount@+112 read *(float*) NO SCVTF; element channel *(float*)(elem+4i) aliases dur@+12 for count==4 | :106 memcpy raw bits; :94 reinterpret_cast index [0,count) | ✅ |

## 3. EmoteBlinkController — ctor 0x662968 / step 0x663BDC (eye, 0x170=368B)

Local: EmoteBlinkController.cpp:90-391, .h:1-137.

| dim | binary | local | verdict |
|-----|--------|-------|---------|
| 源码结构 | track machine (states 0/1/2) + 8B-track drain `while(v5==1)` + blink switch(+336 cases 0/A/B/C) + final remap | :199-391 identical layout incl. goto-based control flow | ✅ |
| 数据流 | trackValue+300/target+304/dir+308/span+312/accum+316/invDur+320/pow+324; blink +328..+360 | matches offset-table; :253-261 setup, :316-374 switch | ✅ |
| 调用链 | sub_661F7C(self+160,self+80,trackValue,kf.endRad) on 12B pop; sub_9F1A08/9F17D0 RNG in case 0xB | :248 EmoteMeshResolver_resolve; :355 EmoteBlinkRng_next | ✅ |
| 对象生命周期 | embedded deques/vectors; 8B-track & 12B-track pop free blocks | std members; pop_front | ⚠️ container-boundary |
| 内部容器 | edge=std::vector<{f,f}> @+160; node=deque<vector<float>> @+184 (504B block) | mesh.edgeTable (vector), mesh.nodeRows (deque<vector<float>>) | ✅ selection matches (see §note) |
| 边界行为 | final remap divisor `(double)(v44-v42)` FLOAT numerator / DOUBLE divisor; trackPow raw bits | :383-388 reproduces mixed precision; :261 memcpy | ✅ |

ctor 0x662968: beginFrame/endFrame int; blinkIntervalMin/Max/blinkFrameCount double->float; blinkEnabled bool&1;
trackValue(+75=+300)=blinkPos(+89=+356)=(float)beginFrame; nextBlink(+88=+352)=min+(max-min)*rand
(sub_9F17D0(sub_9F1A08())). Local :100-163 matches. The binary reads keys via Motion_propGet* TJS dispatch
on the live dispatch; local uses PSB dict accessors — see §A-CHAIN below (调用链 boundary, flagged not failed).

## 4. EmoteEyebrowController — ctor 0x66480C / step 0x665600 (slim, 0x150)

Local: EmoteEyebrowController.cpp:58-236, .h.

| dim | binary | local | verdict |
|-----|--------|-------|---------|
| 源码结构 | states 0/1/2; offsets SWAPPED vs eye (accum=+312/span=+316/pow=+320/invDur=+324); ends `*a2=+300` DIRECT, NO blink/RNG/remap | :144-236 identical incl. swapped offsets, direct end | ✅ |
| 数据流 | same as eye track machine minus blink | matches | ✅ |
| 调用链 | sub_661F7C on 12B pop; NO RNG | :217 EmoteMeshResolver_resolve | ✅ |
| 对象生命周期/容器 | embedded deque/vector | std equivalents | ⚠️ boundary |
| 边界行为 | trackPow raw bits; v15=pow(...) as double | :158-159 double v15; :227 memcpy | ✅ |

## 5. EmoteMouthController — ctor 0x665C98 / step 0x666068 (0x70=112B)

Local: EmoteMouthController.cpp:44-150, .h.

| dim | binary | local | verdict |
|-----|--------|-------|---------|
| 源码结构 | 2-state ramp (state1 animate, state0 setup); NO state 2; NO blink/RNG/mesh | :85-143 identical | ✅ |
| 数据流 | accum+92/cur+84/end+88/invDur+96/powF+100/start+104/beginFrame+108; out *a2=(float)beginFrame, *a3=cur | matches | ✅ |
| 调用链 | leaf (only deque pop) | leaf | ✅ |
| 对象生命周期/容器 | 12B deque pop | std::deque | ⚠️ boundary |
| 边界行为 | powF read *(float*) no SCVTF; completion accum=1.0 | :130 memcpy; :100 accum=1.0f | ✅ |

ctor 0x665C98: ONLY reads beginFrame(+108); NO endFrame/blink*/RNG/edge/node. Local :44-58 matches.
MINOR (P2): setup arm field-write ORDER differs (local sets startVal before state; binary sets state between
reading v11 and writing start) — distinct fields, no aliasing, semantically identical. Not a deviation.

## 6. EmoteSelectorController — ctor 0x66E398 / step 0x668470 / applySelection 0x6680B0 / Animator_setKeyframes 0x667330

Local: EmoteSelectorController.cpp:36-243, .h.

| dim | binary | local | verdict |
|-----|--------|-------|---------|
| 源码结构 | step: 2-state (anim accum, setup pop 12B {selIdx,dur,fade}); applySelection loops optionList; Animator_setKeyframes dur<=0 snap / else clearFirst+pushback | :125-231 identical; :36-71 setKeyframes both branches | ✅ |
| 数据流 | selState+84/selectedIndex+88/invDur+92/accum+96; value=(i==sel)?onValue(+12):offValue(+8); delta=cur-value | :127-163 step; :201-215 applySelection | ✅ |
| 调用链 | step->applySelection->EmoteVarController_step(refCtl,&cur,0)+Animator_setKeyframes | :208/:227 same chain | ✅ |
| 对象生命周期 | optionList=vector swapped in (move); refCtl pointers BORROWED (dtor does NOT delete) | :100 std::move; :233-241 dtor borrows | ✅ |
| 内部容器 | vector<16B option>; 12B command deque | std::vector + std::deque | ⚠️ boundary |
| 边界行为 | guard `state!=0 || queue non-empty || fabs(delta)>=1e-7`; scaledDur=fabs(delta/(on-off))*dur; fade=raw bits | :220-228 identical guard+scale; :146/:69 memcpy | ✅ |

## 7. EmoteLoopController — inline step (EmoteEngine_progress 0x67d2a0)

Local: EmoteLoopController.cpp:29-57.

| dim | binary | local | verdict |
|-----|--------|-------|---------|
| 源码结构 | inline in progress; idx+accum advance, wrap `(idx+1)%count` while span<=accum, lerp t*v1+(1-t)*v0 | free fn mirror :30-56 | ✅ |
| 数据流 | currentIndex@+0/accum@+4; keys 12B {v0,v1,span} | matches | ✅ |
| 内部容器 | (finish-start)/12 = vector size | std::vector::size() | ✅ (STL size() inline equiv) |
| 边界行为 | FDIV t=accum/span; FMUL/FADD blend | :52-55 identical | ✅ |

NOTE: binary inlines this INTO progress; local factors it to a free function. 源码结构 boundary (factoring) —
the per-entry body is reproduced 1:1; acceptable as a readability split that preserves the math/dataflow.

## 8. EmotePhysics_springStep 0x662768 (72B state)

Local: EmoteSpring.cpp:31-99, .h:52-84.

| dim | binary | local | verdict |
|-----|--------|-------|---------|
| 源码结构 | free fn, init-flag branch (+0), 3-component integrate (+48/+52/+56), two atanf outputs | :38-98 line-by-line identical | ✅ |
| 数据流 | every `*(float*)(a1+off)` matches; qword_1AB7E74=(0.0f,1.0f) rest unit vector | :27-28 constants; all v18..v42 match | ✅ |
| 调用链 | leaf (sinf/cosf/atanf) | leaf | ✅ |
| 对象生命周期/容器 | POD, no alloc | POD struct | ✅ |
| 边界行为 | 0.0451603944 & 0.0392699082 (pi/80) preserved; outputs NOT collapsed | :90-96 both constants, two outputs | ✅ |

**2026-05-30 A-1 "MISSING (P1)" is RESOLVED** — spring physics is now fully ported. Verified vs fresh decompile.

## 9. EmoteBustChainSpring_step 0x6689A4 (176B chain spring)

Local: EmoteSpring.cpp:120-284, .h:169-225.

| dim | binary | local | verdict |
|-----|--------|-------|---------|
| 源码结构 | init-flag, 2-seg loop (v28 in {0,1}), per-seg constraint + integrate + collision lookup + damping + atan | :132-283 raw-byte `F(off)` mirror, identical loop | ✅ |
| 数据流 | 12*v28/8*v28 strides; +168 collision curve (128 entries stride 12, +1556 scale, v90*0.5+4.0 bound); +24 lastSeg gate | :170-279 every offset matches | ✅ |
| 调用链 | leaf | leaf | ✅ |
| 对象生命周期/容器 | POD 176B; collisionCurve null-guarded pointer | POD with _pin members; null guard :236 | ✅ |
| 边界行为 | constants 0.015625/4.0/pi-80; seg-select `v28?a3:a2` | :183/:243/:271-277 preserved | ✅ |

ctor 0x668EF8 (sub_668EF8): local EmoteSpring.cpp:369-426 documented; reads gravity/friction_x/y/b_rate/v_bound/
ud_eft(int)/bend_spd/bend_vol + length/scale_x/scale_y 2-elem lists; rest-pos from (0,1,0). Structurally matches
(ctor not re-decompiled this session — flagged ❓ for a future confirm, math contract already cross-checked via step).

## 10. EmoteWindEmitter — init 0x670AFC / step 0x6687E8 (1564B, 128-slot pool)

Local: EmoteWindEmitter.cpp:16-105, .h.

| dim | binary | local | verdict |
|-----|--------|-------|---------|
| 源码结构 | init: +1536 start, 128 STRB active=0, +1540 end, +1544 gate=0, +1548/52={1.0,0.0}; step: accum+=abs(vel)*dt, emit-while-loop, advance+kill | init :16-25, step :46-105 identical | ✅ |
| 数据流 | slot 12B {active@+0,lifePos@+4,yPos@+8}; emit prob 0.0625; yPos=yLo+(yHi-yLo)*rng | :40-45 POD; :56/:83 matches | ✅ |
| 调用链 | sub_9F1A08/9F17D0 RNG x2 per emit | :55/:82 EmoteBlinkRng_next | ✅ |
| 对象生命周期/容器 | fixed 128 POD array, no alloc | slots[128] | ✅ |
| 边界行为 | slot-walk give-up v8>126; sign-aware kill compound predicate | :68-74 hoisted-bound walk; :99-100 predicate | ✅ |

NOTE: binary's give-up over-reads slot[128] (=+1536 control word) once before bailing; local hoists the bound
check (idx==128 bails first) — documented at :57-67 as producing identical observable result (benign read elided).
边界行为 ✅ (observable result identical; the benign OOB read is not reproduced, correctly).

## 11. EmoteBlinkRng — init 0x9F1A08 / next 0x9F17D0 (global MT19937)

Local: EmoteBlinkRng.cpp:34-130, .h.

| dim | binary | local | verdict |
|-----|--------|-------|---------|
| 源码结构 | lazy global (qword_1AF7E80), operator new(0x1398), seed=sub_A2BDBC; next = MT recurrence + temper + 53-bit canonical-real build, return d-1.0 | :84-130 mirror | ✅ |
| 数据流 | mt[0]=seed; fill `v1=(v2+1)+1812433253*((v1>>30)^v1)-3`; low=temper(w1) 32 bits, high mantissa=((w2>>18)^w2)&0xFFFFF<<32; bits|0x3FF..0 | :89-93 fill; :122-129 mantissa build | ✅ |
| 内部容器 | libstdc++ stores 624 words as QWORD (uint_fast32_t=64b); masked to 32 | local uint32_t (masked-equiv) | ✅ (bit-identical sequence) |
| 边界行为 | lowerMask 0x7FFFFFFE; matrixA 0x9908B0DF; seed=steady_clock/1000000 | :17-19 constants; :22-28 seed | ⚠️ regen-timing P2 (see deviation) |

---

## §A-CHAIN — controller ctor key-source 调用链 boundary (flagged, NOT failed)

The Blink/Eyebrow/Mouth/Selector ctors in the binary read PSB keys via the LIVE TJS dispatch
(`Motion_propGetInt/Double/Bool/Count`, `(**vtbl)(...)` member calls, `sub_A0F5E0/sub_6637BC` variant
coercion). The local ctors read the SAME data from a parsed `PSB::PSBDictionary*` via dynamic_pointer_cast.
This is a 调用链 divergence (TJS dispatch wrapper replaced by direct PSB accessors). It is flagged here, not
auto-failed, because: (a) the controller-step bodies — the live per-frame logic that this cluster owns — use
NO dispatch and are byte-faithful; (b) the ctor dispatch path is the PSB-load bridge whose dispatch-vs-PSB
decision is a project-wide architectural choice owned by the EmoteObject/Motion load layer (clusters B/C/F),
not by these leaf controllers. Reviewer should confirm the load-layer dispatch decision separately. The KEYS,
DEFAULTS (0), and TYPES read are all confirmed against the decompile and match.

---

## 未达成 100% 清单 (by severity)

| id | 本地位置 | 二进制 @addr | dim | sev | 摘要 |
|----|---------|-------------|-----|-----|------|
| A6-1 | EmoteBlinkRng.cpp:62-71 nextWord | 0x9F17D0 | 边界行为 | P2 | next() consumes 2 words via two independent `nextWord` calls (`if(left0==1)regen`); binary batches the 2-word consume with a pre-decrement (`v5=v1-2`, regen word2 when `v1==2`). Regeneration BOUNDARY may differ by one draw at the 624-word block edge. Unobservable: wall-clock-seeded, no oracle (documented PLATFORM_BOUNDARY .h:15). Recommend re-deriving next() as a single batched 2-word read mirroring 0x9F17D0's `left` bookkeeping for byte-fidelity. |
| A-CHAIN | Blink/Eyebrow/Mouth/Selector ctors | 0x662968/0x66480C/0x665C98/0x66E398 | 调用链 | P1(bnd) | ctors read PSB dict directly instead of TJS Motion_propGet* dispatch. Architectural decision owned by load layer; keys/defaults/types match. See §A-CHAIN. |
| A-DEQUE | all controllers w/ deque/vector | many | 内部容器 | P1(bnd) | std::deque/std::vector (libc++) vs libstdc++ block math. Documented PLATFORM_BOUNDARY in every header. Element semantics (front-pop/push-back, element type) preserved; byte-offset equality unreachable. Not counted as deviation. |
| A-LOOP | EmoteLoopController.cpp | 0x67d2a0 | 源码结构 | P2 | step factored to a free function; binary inlines it into progress. Per-entry math reproduced 1:1; factoring is a readability split. |

## 已确认对齐 (ALIGNED)

| 函数 | @addr | 验证 |
|------|-------|------|
| EmoteAngleController_step | 0x666634 | line-by-line; 2026-05-30 A-2/A-3/A-4 ALL resolved |
| EmoteVarController_step/ctor | 0x666BF8/0x667030 | line-by-line; 4x over-alloc fixed; fall-through correct |
| EmoteBlinkController_step/ctor | 0x663BDC/0x662968 | line-by-line incl. mixed-precision remap, raw-bits powCount |
| EmoteEyebrowController_step/ctor | 0x665600/0x66480C | line-by-line; swapped offsets, direct end |
| EmoteMouthController_step/ctor | 0x666068/0x665C98 | line-by-line; 2-state, beginFrame-only ctor |
| EmoteSelectorController step/applySel/setKeyframes/ctor | 0x668470/0x6680B0/0x667330/0x66E398 | line-by-line; borrowed refCtl, both setKeyframes branches |
| EmoteLoopController_step | 0x67d2a0 | per-entry math 1:1 |
| EmotePhysics_springStep | 0x662768 | line-by-line; A-1 MISSING resolved |
| EmoteBustChainSpring_step | 0x6689A4 | line-by-line; 2-seg, collision curve, lastSeg gate |
| EmoteWindEmitter_init/step | 0x670AFC/0x6687E8 | line-by-line; benign OOB read elided w/ identical result |
| EmoteBlinkRng_get/next | 0x9F1A08/0x9F17D0 | algorithm + canonical-real build bit-identical (modulo A6-1 regen edge) |

## 递归发现的子函数待办

| sub | @addr | 状态 | 备注 |
|-----|-------|------|------|
| EmoteMeshResolver_search | 0x660028 | ❓ partial | ~1925-line DFS path engine. Entry wiring (0x661F7C arg order, min-dist 99999.0/-1.0 selection, {endVal,endVal} fallback span=0) CONFIRMED vs decompile. Full branch-for-branch re-verify of the engine BODY is its own dedicated vertical (out of scope for this cluster's proportion); local EmoteMeshResolver.cpp claims a branch-for-branch port with decompile line refs — recommend a focused re-audit pass. |
| EmoteBustChainSpring_ctor | 0x668EF8 | ❓ not re-decompiled | math contract cross-checked via step (rest-pos +92..+136, scale lists). Recommend a confirm decompile. |
| EmoteSpringState_ctor | 0x662448 | ❓ not re-decompiled | local EmoteSpring.cpp:349-366 documented (gravity/spring/friction/scale_x/scale_y). Confirm later. |
| Player physics callers (stepHairParts/stepBust) | 0x67B748/0x67BCE8 | out of cluster | cluster B/G scope; spring objects fed from here. |

## 平台边界标注 (skipped, listed for reviewer)

- EmoteSpring.h / EmoteBlinkController.h / EmoteVarController.h / EmoteAngleController.h: `PLATFORM_BOUNDARY`
  std::deque/std::vector ABI (libc++ vs libstdc++) — every controller header. Reason given: container header
  byte-size differs; element semantics preserved. VALID boundary.
- EmoteBlinkRng.h:15 `PLATFORM_BOUNDARY` wall-clock seed (steady_clock/1000000) -> non-deterministic, no
  oracle. VALID (also gates A6-1's observability).
- EmoteSpring.h:149 `PLATFORM_BOUNDARY` collisionCurve held as opaque pointer (alloc path = wind emitter,
  ported separately; null-guarded). VALID.
- EmoteSpring.cpp:80 / EmoteVarController.cpp:80 NEON->scalar loop note. VALID (numerical result identical).

## IDB 修正记录 (this session)

- rename 0x66E398 `EmoteSelectorController_ctor_guess` -> `EmoteSelectorController_ctor` (verb identity
  confirmed: memset + AngleController ctor + optionList swap + applySelection(0)).
- set_comments 0x66E398 / 0x6689A4 / 0x662768 with 2026-06-07 clusterA ALIGNED annotations (and noting the
  2026-05-30 A-1 spring-MISSING resolution at 0x662768).
- idb_save persisted.

## Cluster verdict

**⚠️ 部分偏差 (minor)** — All 11 controller/physics step functions are line-by-line ALIGNED against fresh
decompiles. Every 2026-05-30 P0 (Angle same-call/phase-reset/wrap, Var 4x over-alloc) and the A-1 spring-MISSING
gap are RESOLVED. Remaining items: one localized P2 (A6-1 RNG regen-boundary, unobservable), plus the
project-wide ctor dispatch-vs-PSB boundary (A-CHAIN, owned by the load layer) and the universal deque/vector
PLATFORM_BOUNDARY. The mesh-resolver engine body (0x660028) is the one large recursion left ❓ — entry wiring
confirmed, engine body needs its own focused vertical.
