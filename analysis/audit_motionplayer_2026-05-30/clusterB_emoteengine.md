# Cluster B — EmoteEngine Core — Alignment Audit (2026-05-30)

Authoritative source = libkrkr2.so (IDB libkrkr2.so.i64). Decompiled this session:
EmoteEngine_progress @0x530a5c(=0x67D01C body), EmoteEngine_applyVarControllers @0x6766E0,
EmoteEngine_ctor @0x67E38C, EmoteEngine_dtor @0x67F4B8, EmoteEngine_stepHairParts @0x67B748,
EmoteEngine_stepBust @0x67BCE8, EmoteObject_init @0x67DBAC, EmoteObject_destroy @0x67F420,
ttstr_doubleMap_upsert @0x686944, VariantPtrVector_assign @0x67F0CC, EmoteObject_applyChara @0x67F370.

NOTE on owner-list addresses: progress real address is 0x67D01C (0x530a5c is a thunk/alt-entry
that tail-jumps into 0x67D01C; decompile of 0x530a5c yields the 0x67D01C body). EmoteObject_init
is sub_67DBAC (owner listed 0x67dbac ✓). sub_67e20c (= EmoteObject loadResource accessor) and
sub_67d4d0 are container/dispatch plumbing, classified non-logic (see neighbors section).

================================================================================
## P0 — dataflow bugs (read/write wrong data)
================================================================================

### P0-B1: progress() inner deque-step loops entirely absent (stubbed)
- func: EmoteEngine_progress @0x67D01C | local: EmoteEngine.cpp:235-315 (loop body :252-257)
- BINARY: inside the dt-slice loop the engine iterates SIX deques and writes each step
  output into HM#7 (this+1440) via ttstr_doubleMap_upsert. Order + element stride + writer:
  1. deque #4 @256  sub_663BDC(elem,&out,step) -> upsert(this+1440, elem+1)=out[0]; elem+=2
  2. deque #5 @336  sub_665600(elem,&out,step) -> upsert(elem+1)=out[0]; elem+=2
  3. deque #6 @416  sub_666068(elem,&out,&out2,step) -> upsert(elem+1)=out[0]; upsert(elem+2)=out2; elem+=3
  4. deque #9 @656  sub_668470(elem,&out,step) -> upsert(elem+1)=out[0]; elem+=6
  5. deque #8 @576  EmoteVarController_step(elem,&out,step) -> upsert(elem+1)=out[0]; elem+=3
  6. deque #10 @736 INLINE curve-LUT interp (no callee): advances cursor by step, wraps
     `(idx+1) % count`, lerps `v60/v61 * tab[+4] + (1-v60/v61)*tab[+0]`, upsert(elem+1); elem+=2
- LOCAL: replaces all six with `if(!deque.empty()){ STUB_WARN(...) }` and never writes HM#7.
  Iteration order in local comment (:248) is "#4,#5,#6,#8,#9,#10" — also WRONG vs binary
  (#9 precedes #8 in the binary). The whole physics->HM7 dataflow is missing.
- Severity: P0 (the engine's per-frame variable outputs are never produced). Gated behind
  empty deques today (deques unpopulated until setVariable porting), so inert at runtime NOW,
  but it is a real dataflow divergence and the deque iteration uses the libstdc++ block-walk
  (`if(cur==blockEnd){ cur=*(map+8); ... }`) that the local STL deque cannot reproduce 1:1.

### P0-B2: progress() top-level `if (a2 != 0.0)` guard missing
- @0x530a60 / 0x67D01C entry | local: EmoteEngine.cpp:243
- BINARY: entire body wrapped in `if (dt != 0.0) { ... }`. dt==0 => immediate return, NO
  preProgress, NO loop, NO bind-walk, NO physics pass.
- LOCAL: no such guard; enters `while(dt>0 || _dirty)` unconditionally — with dt==0 and
  _dirty seeded true the local runs one slice + the bind loop + physics gate. Divergent path
  for dt==0 callers.

### P0-B3: physics-pass + post-loop use ORIGINAL dt, local uses RESIDUAL dt
- @0x67D3A4..0x67D488 | local: EmoteEngine.cpp:290-314
- BINARY: after the slice loop, `v12` holds the ORIGINAL dt (saved @0x67D054 before the loop).
  - `sub_6D2A54(player,0,v12)` uses original dt.
  - physics gate `if (v12 != 0.0 && !syncWaiting@1159)` and the 3 controller steps + stepHairParts
    + 2×stepBust are all called with `v12` (original dt), NOT the post-loop residual (which is ~0).
- LOCAL: the slice loop mutates `dt` down to ~0 (`dt -= step`), then the physics gate
  `if (dt != 0.0f && !_syncWaiting)` and `EmoteVarController_step(..., dt)` use that RESIDUAL.
  => gate almost always false and, when taken, steps with wrong (near-zero) dt. Real dataflow bug.
  Local must save original dt in a separate variable (mirror v12) and feed it to the post-loop.

### P0-B4: bind-loop iterates HM#7 by std map order, not insertion-order node chain
- @0x67D3A4 | local: EmoteEngine.cpp:290-297
- BINARY: `for (i = *(this+1456); i; i = *i)` walks HM#7's _M_before_begin._M_nxt singly-linked
  INSERTION-ORDER node chain, reading key=i+1, value=i+2(double); calls sub_67C560 / sub_67C6B0,
  then Player_bindParameterValue(player, key, 0, (flag&1)?-val:val).
- LOCAL: `for (auto& kv : _labelToValueHM7)` — libc++ bucket order, and the body is fully
  commented out (no sub_67C560/67C6B0/bindParameterValue). Both the ORDER and the BODY diverge.
  Body currently inert (callees un-ported) so not live, but ordering boundary already documented.
- Severity: P0-latent (order + missing body). Tied to the Phase C decision in the restoration review.

================================================================================
## P1 — structure / type / container / lifetime
================================================================================

### P1-B1: D3DEmotePlayer holds EmoteObject via std::unique_ptr (binary = raw new/delete)
- EmotePlayer.h:310 `std::unique_ptr<EmoteObject> _emoteObj;` ; ctor EmotePlayer.cpp:37 make_unique
- BINARY: EmoteObject is `operator new(0x28)` (the parent D3DEmotePlayer shell stores raw ptr at
  +24/+8), freed by EmoteObject_destroy @0x67F420 via `sub_67F4B8(a1[1]); operator delete`.
  No smart-pointer ownership — manual new/delete, matching CLAUDE.md hard rule.
- LOCAL: unique_ptr. This is exactly the "shared_ptr/unique_ptr replacing manual new/delete"
  deviation CLAUDE.md forbids. EmoteObject's OWN internal _engine is correctly a raw pointer
  (EmotePlayer.h:83, EmotePlayer.cpp:24-31) — only the parent's hold of EmoteObject is wrong.

### P1-B2: EmoteObject_init body (PSB load + play pipeline) — partially restored 2026-07-13
- EmoteObject_init @0x67DBAC | local EmoteObject ctor now retains `vector<ttstr>`, loads every
  path through ResourceManager in order, and feeds the final loaded snapshot into Player.
- BINARY EmoteObject_init does MUCH more than allocate the engine. Verified call chain:
  1. ttstr "global.kag" -> sub_8E3F94 (resolve), operator new(0xE8) ResourceManager-like (sub_6A88CC)
  2. operator new(0x5D8)=EmoteEngine, EmoteEngine_ctor(engine, {accessor,accessor})
  3. ttstrVector_assign_67F0CC(this+2, modulePaths) — fills the +16 path vector
  4. loop: ResourceManager_loadResource per path
  5. dispatch PropGet "metadata"->"base", build sub_A0F5E0 wrapper (off_19FD968)
  6. PropGet "chara" (sub_6866F0->v33) and "motion" (->v32)
  7. AddRef + sub_A0FB64(player+1012, chara), EmoteObject_applyChara_67F370(engine, motion-ish)
  8. Player_play(player, 1, &charaVar); sub_67D4D0(engine, baseWrapper)
- LOCAL (corrected): the load loop and final snapshot→Player bridge now live in the
  EmoteObject constructor, and D3DEmotePlayer `load` is a raw vararg callback matching the
  binary producer. Remaining divergence is that local `loadFromSnapshot` still condenses the
  binary's explicit metadata/base/chara/motion dispatch sequence.
  Different call-chain topology. Marked P1 (structure), not P0, because behavior may converge,
  but it violates "复刻调用链/对象生命周期".

### P1-B3: applyVarControllers Player-side applies all commented out
- applyVarControllers @0x6766E0 | local EmoteEngine.cpp:167-207
- BINARY does, IN ORDER pos->color->scale->angle: Player_setCoord(out0,out1);
  sub_6CD724(player, packARGB); `*(this+1176)=1.0/(*(this+1168)*out0)`; Player_setSlant(out0,out0);
  Player_setAngleDeg(out0).
- LOCAL: ORDER is correct (pos,color,scale,angle ✓ — P2-4 ordering FIXED) and the +1176 write
  is present & correct (:197). BUT all four Player applies are commented out (TODO P1). So the
  controller outputs never reach the Player. Structural stub, flagged for completion.

### P1-B4: 6 EmoteEngine maps modeled as libc++ std::unordered_map (binary = libstdc++ 56B inline)
- EmoteEngine.h:224-325 (_scalarHM1_824.._labelToValueHM7)
- Already documented PLATFORM_BOUNDARY (libc++ header != libstdc++ 56B). Value types of
  HM#1/2/3/4/5/6 still placeholder. Accepted boundary per review; carried as ⚠️ structure.
  HM#3 value (EmoteHM3Value 104B opaque) is correct to keep opaque — dtor sub_683E40 confirmed
  (EmoteEngine_dtor @0x67F7CC walks +952 nodes calling sub_683E40(node+1)).

================================================================================
## P2 — comment / order / semantics
================================================================================

### P2-B1: progress() local deque iteration-order comment wrong
- EmoteEngine.cpp:248-249 lists "#4,#5,#6,#8,#9,#10"; binary order is #4,#5,#6,#9,#8,#10
  (deque@656 before deque@576). Cosmetic until P0-B1 ports the loop, but the comment misleads.

### P2-B2: ctor color seed (xmmword_14D68D0) still not read; _ctlColor left zero
- EmoteEngine.cpp:104-109. Binary ctor @0x67E9D8 does `v73 = xmmword_14D68D0` then memcpy into
  _ctlColor currentValue (4 floats). Local deliberately leaves it zero pending the rodata read.
  Honest TODO; flagged P2 (a real seed value is missing but not yet confirmable here).
- ctor reset ORDER 134/135/137/136 = pos/scale/angle/color is CORRECT in local (:95-109). P2-4
  ctor-order item FIXED.

### P2-B3: progress _dirty / fmin(dt,1.1) cap match
- Local :243-245 `while(dt>0||_dirty){ step=fmin(dt,1.1); _dirty=false; ... }` matches binary
  @0x67D0A0..0x67D0B8 (LABEL_6 / `while(_BYTE(v13+1162))` outer + `while(v14>0)` inner +
  `*(v13+1162)=0` + `fmin(v14,1.1)`). ✓ aligned (modulo P0-B2 guard).

================================================================================
## MISSING (no local counterpart at all)
================================================================================
- EmoteEngine::stepHairParts — DECLARED nowhere as a method; progress() only STUB_WARNs it
  (EmoteEngine.cpp:311). Binary EmoteEngine_stepHairParts @0x67B748 is a full physics integrator
  (deque #1 walk, sub_67B970 transform, sub_662768 spring, Player_getAngleDeg, writes 2 HM7 keys
  per node elem+20/elem+28). MISSING.
- EmoteEngine::stepBust — same: STUB_WARN only (EmoteEngine.cpp:312-313). Binary
  EmoteEngine_stepBust @0x67BCE8 is a full damped-oscillator integrator (deque walk, sub_67B970,
  sub_6689A4, sinf/fmod 2pi spring, writes 3 HM7 keys per node elem+20/28/36, reads spring const
  this+1184/1192, damping `a5*0.03125`, 28.0 threshold). MISSING.
- The 6 deque step fns (sub_663BDC/665600/666068/668470/666BF8 + inline LUT) — MISSING (P0-B1).
- sub_67C560 / sub_67C6B0 / Player_bindParameterValue body — MISSING (P0-B4).
- EmoteObject_init load/play pipeline — MISSING (P1-B2).

================================================================================
## Prior P0-2 / P2-4 status (review 2026-05-30)
================================================================================
- P0-2 (6 inline maps mis-modeled as raw byte blocks + _bindListHead pseudo-field): FIXED.
  EmoteEngine.h now has 7 typed unordered_map + 4 typed VariantPtrVector; _bindListHead removed.
  Carried forward only as P1-B4 (libc++/libstdc++ ABI boundary, accepted) + open value types.
- P2-4 applyVarControllers order pos->color->scale->angle: FIXED (EmoteEngine.cpp:172-206).
- P2-4 ctor reset order 134/135/137/136: FIXED (EmoteEngine.cpp:95-109).
- NEW vs review: review treated progress() physics+bind as "stubbed, structural only". This
  audit reclassifies the dt-residual reuse (P0-B3), the dt==0 guard (P0-B2), and the missing
  HM7-writing deque loop (P0-B1) as concrete dataflow divergences, not mere stubs.

================================================================================
## Neighbor scan classification
================================================================================
- sub_67E20C — EmoteObject loadResource/accessor helper (returns a refcounted dispatch from the
  ResourceManager built in init). Dispatch plumbing, not standalone logic. NOT deep-audited.
- sub_67D4D0 — apply-base wrapper invoked at end of EmoteObject_init (sub_67D4D0(engine, base)).
  Thin dispatch into Player. Plumbing. NOT deep-audited (no isolated local counterpart).
- sub_67F0CC = ttstrVector_assign_67F0CC — std::vector<ttstr>::assign with per-element
  string-handle AddRef/Release. Corrected 2026-07-13 from the old variant-pointer guess using
  D3DEmotePlayer_load@0x52FDD4 (variant→ttstr producer) and EmoteObject_init@0x67DBAC
  (ResourceManager_loadResource consumer); IDB rename updated and saved.
- sub_67F370 = EmoteObject_applyChara_67F370 — `sub_6B2AE8(player,0,&var)` thin wrapper
  (chara/motion apply). Plumbing. (renamed in IDB)
- ttstr_doubleMap_upsert @0x686944 — VERIFIED shared HM upsert (hash mix 1025/6/9/32769/11 over
  c_str+68 cached hash; node new(0x20){next,ttstr key+AddRef,value,hash}; returns node+16). Local
  detail::ttstr_hash (internal/ttstr_hash.h:35-40) reproduces the 1025/6/32769/11 mix ✓. The
  node-chain insertion order it builds is what P0-B4's bind-loop depends on.

================================================================================
## Cluster verdict: 🔧 NEEDS ARCHITECTURAL REWORK (progress dataflow) + ⚠️ partial elsewhere
================================================================================
Layout foundation (1496B map, 7 maps, 4 vectors, raw-ptr controllers/Player, ctor/dtor order,
applyVarControllers order, +1176 write) is ALIGNED — prior P0-2/P2-4 all fixed. BUT progress()
is not merely "stubbed": it has 4 real dataflow divergences (P0-B1 missing HM7-writing deque
loop + wrong order; P0-B2 missing dt!=0 guard; P0-B3 residual-vs-original dt; P0-B4 map-order
bind-loop). P0-B3 (original-dt) is a local-only fix (add a saved-original-dt variable). P0-B1/B4
require the libstdc++ deque block-walk + insertion-order node chain the STL containers can't mirror
1:1, so they are Phase-C structural work, not local patches. Plus P1-B1 (unique_ptr EmoteObject
→ raw new/delete) and P1-B2 (EmoteObject_init load/play pipeline topology) and P1-B3 (Player
applies commented out). stepHairParts/stepBust + 6 deque step fns are MISSING.
