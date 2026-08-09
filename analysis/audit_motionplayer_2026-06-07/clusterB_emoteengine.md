# Cluster B — EmoteEngine Core — Alignment Audit (2026-06-07)

Authoritative source = libkrkr2.so (IDB libkrkr2.so.i64). Scope: full coverage of
cpp/plugins/motionplayer/EmoteEngine.cpp (2090 lines) + EmoteEngine.h (743 lines).

Functions freshly decompiled THIS session (each claim below is backed by one of these):
- EmoteEngine_progress @0x67D01C (real body; 0x530A5C is a tail-jump thunk)
- EmoteEngine_applyVarControllers_pos_scale_color_angle @0x6766E0
- EmoteEngine_stepHairParts @0x67B748 (+ disasm 0x67b818/0x67b8b0)
- EmoteEngine_stepBust @0x67BCE8 (+ disasm 0x67beb8/0x67bf74)
- EmoteEngine_resolveShapeAnchor @0x67B970
- EmoteEngine_ctor @0x67E38C
- EmoteEngine_dtor @0x67F4B8
- Player_setVariable(=EmoteEngine::setVariable) @0x671228
- Player_getAngleDeg @0x6C1780 (returns DEGREES) / Player_getAngleRad @0x6CD0C0 (returns RADIANS)
- get_bytes(0x14D68D0,16) = xmmword color seed

CRITICAL: the 2026-05-30 report is OBSOLETE. It described progress() deque steps,
stepHairParts, stepBust, setVariable, and all build* as STUBs. The code has since
been fully ported. This audit re-verifies the CURRENT code against fresh decompiles.

================================================================================
## Six-dimension verdict per key function
================================================================================

| Function | 源码结构 | 数据流 | 调用链 | 生命周期 | 容器选型 | 边界行为 | Verdict |
|----------|---------|--------|--------|---------|---------|---------|---------|
| progress @0x67D01C | ✅ | ✅ | ⚠️(preProgress省略) | ✅ | ⚠️(HM7 bucket-order bind) | ✅ | ⚠️ partial |
| applyVarControllers @0x6766E0 | ✅ | ✅ | ✅ | ✅ | n/a | ✅ | ✅ |
| stepHairParts @0x67B748 | ✅ | ✅ | ✅(getAngleRad) | ✅ | ✅(deque) | ✅ | ✅ |
| stepBust @0x67BCE8 | ✅ | ✅ | ✅(getAngleRad) | ✅ | ✅(deque) | ✅ | ✅ |
| resolveShapeAnchor @0x67B970 | ✅ | ✅ | ✅ | ✅ | n/a | ✅ | ✅ |
| ctor @0x67E38C | ✅ | ⚠️(COLOR seed) | ✅ | ✅(raw new) | ⚠️(libc++ map) | ✅ | ⚠️ partial |
| dtor @0x67F4B8 | ✅ | ✅ | ✅ | ✅(raw delete) | ⚠️(order/comment) | ✅ | ⚠️ partial |
| setVariable @0x671228 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| build* (9 builders) | ✅ | ✅ | ✅ | ✅(raw new) | ✅(deque) | ✅ | ✅ |

================================================================================
## P0 — none confirmed this round
================================================================================
The four P0 items from 2026-05-30 (B1 missing deque loop, B2 dt!=0 guard, B3
residual-vs-original dt, B4 bind-loop) are ALL RESOLVED in current code:
- B1: all 6 deque step loops present (EmoteEngine.cpp:1854-1926), order #4,#5,#6,#9,#8,#10
  matches binary @0x67d0a4/d10c/d168/d1e0/d240/d2a0 EXACTLY (selector before transition).
- B2: `if (dt == 0.0f) return;` (EmoteEngine.cpp:1802) mirrors binary @0x530a60.
- B3: `originalDt` saved (cpp:1811 = v12) and fed to physics pass (cpp:2055/2058),
  drain loop uses working `dt` (= v14). Matches binary v12/v14 split.
- B4: bind-loop present (cpp:1980-2001), calls accumulate/mirror/bind — body LIVE.
  Order is bucket-order not insertion-order chain (carried as P1-B1 boundary below).

================================================================================
## P1 — structure / container / lifetime (residual)
================================================================================

### P1-B1: bind-loop walks HM7 by libc++ bucket order, not _M_before_begin chain
- progress @0x67d3a4: `for (i = *(this+1456); i; i = *i)` walks libstdc++
  _M_before_begin._M_nxt INSERTION-ORDER node chain (key=i+1, value=i+2 double).
- LOCAL (EmoteEngine.cpp:1980 `for (auto& kv : _labelToValueHM7)`) iterates a libc++
  std::unordered_map in bucket order. Already documented PLATFORM_BOUNDARY
  (cpp:1973-1978): bind body writes distinct per-label slots in Player HM1/HM2 with
  no inter-label ordering dependence, so the observable result is order-independent.
- Severity: P1 benign boundary (accepted). Same as 2026-05-30 P0-B4 ordering note,
  now downgraded because the BODY is live and verified order-independent.

### P1-B2: 7 EmoteEngine maps modeled as libc++ std::unordered_map (binary=libstdc++ 56B)
- EmoteEngine.h HM#1.._labelToValueHM7. dtor @0x67F4B8 confirms libstdc++ inline
  hashtable (node-chain walk: HM7@+1456, HM6@+1400, HM4@+1288, HM3@+952 via
  sub_683E40, HM2@+896, HM1@+840; each 56B header, 32B node {next,ttstr key,value,hash}).
- Accepted PLATFORM_BOUNDARY (libc++ header layout != libstdc++). HM#3 value type
  distinct (dtor uses sub_683E40, not tTJSVariant_Release) — correctly kept opaque.
- Source-structure dim (std::unordered_map selection) is aligned; ABI offset diff is
  the platform-mandated ABI difference per CLAUDE.md byte-layout methodology.

### P1-B3: 4 variant vectors don't per-element Release (binary dtor does)
- dtor @0x67f70c/74c/780/8c0: each std::vector<tTJSVariant*> (+1040/+1016/+992/+800)
  iterates elements calling tTJSVariant_Release before operator delete.
- LOCAL std::vector<tTJSVariant*> frees only the pointer buffer. Documented inert TODO
  (cpp:130-137): the vectors are never populated (setVariable write path doesn't push),
  so no live leak. P1-latent.

### P1-B4: dtor teardown order diverges + stale PLATFORM_BOUNDARY comment
- EmoteEngine.cpp:118-119 still claims "libkrkr2.so dtor not yet separately reverse-
  engineered; this follows the standard reverse-of-ctor pattern." NOW DECOMPILED
  (@0x67F4B8). Binary order: windEmitter(+1128) -> HM7 chain -> HM6 -> HM5(sub_68577C)
  -> HM4 -> vec(+1248/1228/1208) -> ctl 1120..1072 (reverse) -> Player(+1064) ->
  vec(+1040/1016/992) -> HM3 chain(sub_683E40) -> HM2 -> HM1 -> vec(+800) ->
  deque 720,640,560,480,400,320,240,160,80 (sub_684320..sub_68498C).
- LOCAL dtor deletes deque controllers first, then 7 ctls, then windEmitter, then
  Player. Different ORDER. Objects are independent (no cross-refs at teardown except
  springs borrow windEmitter, which local deletes springs BEFORE windEmitter — safe),
  so order divergence is behavior-neutral. Fix: update the stale comment to cite
  @0x67F4B8 and optionally reorder to mirror the binary. P2-cosmetic + P1-comment.

================================================================================
## P2 — data-value / comment
================================================================================

### P2-B1: ctor COLOR seed left zero (binary seeds 128/128/128/255) — NOW CONFIRMABLE
- ctor @0x67e9d8: `v73 = xmmword_14D68D0` then memcpy 4 floats into _ctlColor
  currentValue. get_bytes(0x14D68D0,16) = `00 00 00 43 / 00 00 00 43 / 00 00 00 43 /
  00 00 7F 43` = {128.0f, 128.0f, 128.0f, 255.0f} (NOT identity white 1,1,1,1).
- LOCAL (EmoteEngine.cpp:107-114) leaves _ctlColor zero with an UNCONFIRMED TODO
  guessing white. The constant is now READ from the binary (not guessed), so this is
  a fixable data-value deviation:
    resetVarController(_ctlColor, ...) must seed currentValue[0..3] = {128,128,255 on
    channel 3}. NOTE channel order: memcpy copies the 16 bytes verbatim into
    currentValue[0..3] -> [0]=128,[1]=128,[2]=128,[3]=255. Apply as a 4-float seed,
    NOT the single-scalar resetVarController helper (which broadcasts one value).
- Severity: P2 (real seed value missing; inert until a motion drives the color
  controller without an explicit setVariable color write — but it is the documented
  initial state and now has confirmed bytes, so should be applied).

### P2-B2: stale "STUB_WARN"/"P1" header comments on progress/applyVarControllers
- EmoteEngine.h:347-357 still says progress physics "STUB_WARN at present" and
  applyVarControllers Player-side apply "is STUB_WARN (P1)". Both are now fully
  ported (cpp:281-324 applies all 4 sinks live; cpp:2082-2086 runs real physics).
  Cosmetic: update header doc-comments.

================================================================================
## Already-aligned (verified this round)
================================================================================
- progress dt!=0 gate, originalDt/dt split, fmin(dt,1.1) cap, _dirty drain — ✅
- 6 deque step loops: order #4,#5,#6,#9,#8,#10; strides 16/16/24/48/24/16B; mouth
  dual-HM7 write (label=outBeginFrame, talkLabel=outCurrentValue); deque#10 inline
  curve LUT factored into EmoteLoopController_step — ✅
- applyVarControllers: pos->color->scale->angle order, packARGB byte pack, +1176
  write `1.0/(+1168 * out0)`, setSlant(v,v), setAngleDeg(degrees from step) — ✅
- stepHairParts/stepBust: anchor resolve, initFlag branch + substep integration,
  spring const +1200(hair)/+1184/+1192(bust), depth ramp |oLastY|<=28, fmod 2pi
  jiggle, HM7 key writes via ttstr_doubleMap_upsert(engine+1440) — ✅
  CRITICAL CHECK PASSED: binary `BL Player_getAngleRad` (0x6CD0C0, returns RAD) — the
  progress/step decomp's "Player_getAngleDeg" label is IDA residue; the actual BL
  target is 0x6CD0C0=getAngleRad. Local emoteGetAngleRadLike_0x6CD0C0() is CORRECT.
- setVariable: HM6 lookup, durationFrames->factor (1.0 / df+1 / 1/(1-df)), _dirty=1,
  cases 0/1/2 (syncWaiting gate), 4/5 enqueue, 6 dual-key (label->beginFrame,
  talkLabel->enqueue), 7 (flag gate), 8 (flag gate), HM2 fallthrough — ✅
- 9 builders (eye/eyebrow/mouth/selector/transition/loop/bust/chain x2): enabled
  gate, raw operator new controllers, deque push, HM6 VarRef {type,loopIndex} with
  loop-index NOT deque-index semantics, mouth/bust dual+triple HM6 inserts — ✅
- ctor: 7 controllers raw new(0x80/0x80/0x80/0x70/0x80/0x80/0x80), Player raw
  new(0x568), reset order POS(0)/SCALE(1.0)/ANGLE(deque-only)/COLOR(seed),
  _dirty=1 — ✅ (except COLOR seed value, P2-B1)
- EmoteObject holds engine via raw ptr (EmotePlayer.h _engine raw) — note: the
  PARENT D3DEmotePlayer's hold of EmoteObject is checked in cluster A/EmotePlayer,
  out of THIS file's scope.

================================================================================
## Recursive sub-function status
================================================================================
- ttstr_doubleMap_upsert @0x686944 — ✅ shared HM7 upsert. **2026-07-26 correction (supersedes the
  old complete-alignment implication):** the local 1025/6/9/32769/11 arithmetic mix was correct, but
  the old functor collapsed null `Ptr` into the empty payload and skipped backing-rep `Hint@+68`.
  Fresh decompiles at `0x6F52AC`, `0x686944`, `0x6F2674`, `0x689760`, `0x6885CC`, and
  `0x6E2060/0x6E2150/0x6E2484/0x6E2574` prove the full behavior now implemented: null→0, reuse a
  nonzero Hint, compute and write Hint otherwise, and map a zero result for non-null input to
  `0xFFFFFFFF`.
- resolveShapeAnchor @0x67B970 — ✅ decompiled, local matches (getLayerMotion dispatch,
  PropGet shape/type/x/y, rootX/Y crossover, *r meshDivisionRatioDup).
- Player_getAngleRad @0x6CD0C0 / Player_getAngleDeg @0x6C1780 — ✅ disambiguated.
- sub_67C560 / sub_67C6B0 / Player_bindParameterValue — ❓ NOT re-decompiled this
  round (bind-loop callees; ported as Player:: methods per memory setvar-progress-
  resolved; live). Recommend cluster-A/Player audit owns these.
- sub_67C8A8 (clampControl binder) — ❓ NOT re-decompiled; ported as
  Player::applyClampControlsLike_0x67C8A8 per memory (live, reconstruction-only).
- 6 step fns (sub_663BDC/665600/666068/668470/666BF8 + inline LUT) — ❓ existence
  confirmed by progress call sites; internal logic NOT line-audited this round
  (they live in EmoteSpring.cpp / controller files, out of EmoteEngine.cpp scope).
  Recommend a controller-step sub-audit.
- EmotePhysics_springStep / EmoteBustChainSpring_step — ❓ called by step fns; in
  EmoteSpring.cpp (out of scope). Disasm confirms call sites + arg order match.
- EmoteEngine_preProgress_guess @0x67d060 — see C&CB-1 below.

================================================================================
## Platform-boundary annotations in EmoteEngine.cpp (listed, not counted)
================================================================================
- cpp:118-119 dtor "not yet reverse-engineered" — STALE (now @0x67F4B8). See P1-B4.
- CORRECTION 2026-07-12: the old "Player_preProgress / no args" reading was false.
  Disasm at 0x67d050..0x67d060 proves X0 remains EmoteEngine*, W1=0 and V0 is the
  original dt. The local call is now restored at the top of every non-zero-dt progress,
  BEFORE the drain loop. LOCAL omits it entirely (comment says "Player has its own
  progress pipeline ... calls directly"). This is a real CALL-CHAIN omission (CB-1
  below) dressed as a boundary; the reason given ("not isolated yet") is a porting
  gap, not an inherent platform limit. Flag for resolution.
- cpp:1973-1978 PLATFORM_BOUNDARY: HM7 insertion-order chain (P1-B1). Legitimate
  (libc++ has no _M_before_begin chain), reason is concrete + order-independence
  argued. Accept.

================================================================================
## CB-1 (call-chain): EmoteEngine_preProgress_guess @0x67d060 restored
================================================================================
- BINARY: `if (dt != 0.0) EmoteEngine_preProgress_guess(this, false, dt)` once
  before the slice loop. LOCAL: restored in `EmoteEngine::progress`; the prior call
  from plain `Player::frameProgress` was removed because Player_progress_inner
  @0x6C106C has no such edge. Remaining gap: the engine HM3@+936 / playing vector
  @+1040 population is still represented by the embedded Player timeline model.

================================================================================
## IDB corrections applied / proposed
================================================================================
- 0x67E38C ctor: IDB comment already documents COLOR seed = {128,128,128,255}
  (confirmed get_bytes this session). No new rename needed.
- 0x6CD0C0/0x6C1780 angle getters already correctly renamed in IDB
  (getAngleRad/getAngleDeg) — the progress/step decomp body still shows the stale
  inline label "Player_getAngleDeg" but disasm BL target resolves to getAngleRad.
  Cosmetic IDB residue; not corrected (would require re-propagating types).
- No idb_save needed: no rename/set_type performed (read-only verification round).

================================================================================
## Cluster verdict: ⚠️ PARTIAL DEVIATION (near-complete; small residual)
================================================================================
Massive improvement vs 2026-05-30: progress dataflow, 6 deque steps, stepHairParts,
stepBust, setVariable, ctor, dtor, and all 9 builders are now ported and LINE-ALIGNED
with the binary. No P0 remains. Residual: P2-B1 (COLOR seed value now confirmable =
128/128/128/255, should be applied), CB-1 (EmoteEngine preProgress call restored;
equivalence or restore), P1-B4 (stale dtor comment now decompiled), plus accepted
boundaries P1-B1 (HM7 bucket-order bind) / P1-B2 (libc++ vs libstdc++ map ABI) /
P1-B3 (variant-vector Release, inert). Recursive ❓: bind-loop callees (sub_67C560/
67C6B0/67C8A8), 6 controller step fns, spring steppers — all out of THIS file's
scope, recommend controller-step + Player sub-audits.
