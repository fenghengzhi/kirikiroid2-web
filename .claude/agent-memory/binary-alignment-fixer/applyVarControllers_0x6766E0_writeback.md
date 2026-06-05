---
name: applyvarcontrollers-0x6766E0-writeback
description: EmoteEngine_applyVarControllers_pos_scale_color_angle @0x6766E0 — 4 direct-controller writebacks (pos/color/scale/angle), sink addrs + semantics, the 0x67D380 address-is-callsite trap
metadata:
  type: project
---

`EmoteEngine::applyVarControllers_pos_scale_color_angle` (EmoteEngine.cpp ~266) aligned to
libkrkr2.so **`0x6766E0`** (the function BODY). Wired 2026-06-06 (audit P1 gap #1) — controller
steps + the +1176 denom were already aligned; only the 4 Player writebacks were TODO(P1) stubs.

**Address trap (important):** audit referenced `0x67D380` but that is NOT the function body — it
is the *call site* inside `EmoteEngine_progress` @0x67D01C (decompiler labels the call instruction
`/*0x67d380*/ EmoteEngine_applyVarControllers_pos_scale_color_angle(v13, v5)`). The real body is
`0x6766E0`. When an audit address lands mid-progress, decompile it AND grep the decompile for the
call to find the actual callee body.

**Binary body (0x6766E0), order pos->color->scale->angle, all reuse one &v7 stack slot:**
1. `step(ctlPosition@+1072,&v7,dt); Player_setCoord(player, v7, v8)` @0x6CCFF8 -> root+1592/+1600
2. `step(ctlColor@+1088,&v7,dt); sub_6CD724(player, pack)` @0x6CD724
   pack = `(u8)(int)v7 | (u8)(int)v8<<8 | (u8)(int)v9<<16 | (u8)(int)v10<<24`
3. `step(ctlScale@+1080,&v7,dt); *(this+1176)=1.0/(*(this+1168)*v7); Player_setSlant(player,v7,v7)` @0x6C0F54
4. `step(ctlAngle@+1096,&v7,dt); Player_setAngleDeg(player, v7)` @0x6C0F84

**Sink semantics (all 4 fresh-decompiled 2026-06-06):**
- setCoord 0x6CCFF8: (x,y)=(v7,v8) -> root+1592/+1600, dirty+1584 if changed. NO cnt gate.
- setColorWeight 0x6CD724 (=sub_6CD724): takes int packed by CALLER; internally does R/B swizzle
  `+1156 = a2&0xFF00FF00 | BYTE2(a2) | ((u8)a2<<16)`. getter 0x6CD710 does the SAME swizzle (symmetric).
  Local Player::setColorWeight + swapPackedRbLike_0x6CD710 replicate it -> pass caller pack verbatim.
- setSlant 0x6C0F54: TWO args (slantX=v7, slantY=v7) -> root+1624/+1632. Binary always passes (v7,v7).
  Local Player::setSlant(v) writes slantX=slantY=v, so setSlant(out[0]) matches the 2-arg same-value call.
- setAngleDeg 0x6C0F84: input is DEGREES (no rad conversion); fed directly from v7. (directEdit branch
  normalizes [0,360)+initEmoteMotion(2); else root+1616 with !=guard.)

**Null-guard divergence (conservative, kept):** binary derefs all 4 ctl ptrs UNCONDITIONALLY (no null
check); EmoteEngine ctor (EmoteEngine.cpp:48-58) always `new`s them so non-null at runtime. Local
`if(_ctlX)` guards are a no-op-equivalent and are kept.

**Verification:** web debug build OK + wasmtime guest rebuild OK. motion_playback differential
m2logo(93f)/yuzulogo(243f) both PASS bitwise — this path is INERT on logo fixtures (logo motions do
not drive these 4 direct-controllers), so the writebacks are a non-regression guard, not a positive
test. No fixture exercises the controllers -> writeback semantics positively (verification gap noted).
