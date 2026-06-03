---
name: wind-emitter-vertical
description: EmoteWindEmitter (sub_6687E8 step + 6709AC populate + 670AFC init) ported; engine+1128 IS the wind object NOT a matrix heap; spring collisionCurve = the wind particle pool
type: project
---

Wind/particle emitter subsystem fully ported 2026-06-04 (dev/motion).

**Address map (IDB renamed + saved):**
- sub_6687E8 -> EmoteWindEmitter_step : per-frame emitter. a1 = wind obj (NOT engine), a2 = clamped step.
- sub_6709AC -> Player_startWind_populate : a1 = the ENGINE (writes engine+1128 ptr, engine+1136..1152 cache, reads engine+1168 divisor). NOT the Player despite the Player_ prefix call site.
- sub_670AFC -> EmoteWindEmitter_init : (windObj, startPos, endPos). alloc size operator new(0x61C)=1564B.
- sub_6687D8 -> thunk (dead, no xrefs): if(ptr+1544) sub_6687E8(ptr,f).
- RNG REUSED: sub_9F1A08/9F17D0 = EmoteBlinkRng_get/next (already ported, global singleton qword_1AF7E80). Wind casts next() double->float, compares <0.0625f.

**KEY CORRECTION (was wrong in local code):**
engine+1128 was locally `_matrixHeap1128` ("transform/matrix alloc, semantics TODO"). It is the WIND EMITTER object. Renamed to EmoteEngine::_windEmitter (EmoteWindEmitter*). The bust/hair chain springs borrow it as collisionCurve (EmoteEngine_stepBust @0x67bea4: `spring->collisionCurve(+168) = *(engine+1128)`) — the spring reads the 128-slot particle field as a wind force. EmoteSpring.h already half-knew this ("128 entries stride 12B").

Local `Player::_windState` (minAngle/maxAngle/scaledAmplitude/counter) was a FUNCTIONAL-EQUIVALENT INVENTION — deleted. Replaced by faithful EmoteWindEmitter + engine-side float cache (_windMin/_windMax/_windAmp/_windFreqX/_windFreqY @ engine+1136..1152).

**Wind object layout (analysis only, not enforced):**
- +0..+1535: 128 slots x 12B {byte active@+0, float lifePos@+4, float yPos@+8}. 12B POD = data contract (spring reads via *(float*)(slot+4i)).
- +1536 startPos(f), +1540 endPos(f), +1544 gate(byte), +1548 yHi(f=freqX), +1552 yLo(f=freqY), +1556 velocity(f signed), +1560 emitAccumulator(f).
- init defaults: yHi=1.0f, yLo=0.0f (xmmword_14D68C0). threshold 0.0625f = dword_14D6788.

**Step engine:** accum += |vel|*dt; while(accum>=0){ if((float)rng()<0.0625) find first inactive slot[0..127] (give up if full), set active, lifePos=startPos bits, yPos=yLo+(yHi-yLo)*rng(); accum-=1 }. Then each active slot: lifePos+=vel*dt; kill if (vel>0&&pos>end)||(vel<0&&pos<end).

**startWind populate:** v6=|amp|, v9=amp>=0?min:max, v10=amp>=0?max:min. stop(delete+null) if v6==0||v10==v9||(fx==0&&fy==0). Rebuild emitter if null OR start/end(+1136/+1140) changed: new(0x61C)+init(v9/div, v10/div). Then cache fx/fy/amp, dir = endPos<startPos?-1:1, gate=1, velocity=dir*(v6/div), accum=0. NO _dirty write (binary doesn't set engine+1162 here — local invention had it, removed).

**Gate wiring (EmoteEngine::progress, EmoteEngine.cpp ~line 1907):**
disasm 0x67d384: LDR X0,[X19,#0x468](engine+1128); CBZ; LDRB W8,[X0,#0x608](+1544); CBZ; MOV V0=V9(=v5 clamped step); BL. => `if(_windEmitter && _windEmitter->gate) _windEmitter->step(step)` where step=fmin(dt,1.1).

**TRAP / boundary documented:** binary slot-find over-reads slot[128] (= +1536 control word) when all 128 full before bailing; if startPos low byte==0 it would corrupt control block (binary bug, non-deterministic). Port hoists the bound check (idx==128 bails directly) => observably identical when startPos low byte!=0 (common), avoids UB. Noted in EmoteWindEmitter.cpp comment.

**Verification:** web+wasmtime build clean. motion_playback wasmtime differential PASS bit-identical m2logo(93f)+yuzulogo(243f). ORACLE-INERT: logo motions never call startWind so gate stays clear and step never runs — non-regression guard, not engine exercise. No wind fixture exists (honest verification gap; do NOT fabricate one per CLAUDE.md). Files: EmoteWindEmitter.{h,cpp} (new), EmoteEngine.{h,cpp}, PlayerCore.cpp, Player.h, EmoteSpring.h. Added to both CMakeLists.
