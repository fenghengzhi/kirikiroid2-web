---
name: type4-particle-interp-chain-done
description: type-4 particle interp chain (mergeFrameContent prt / eval node+2224..2288 mirror / HM3 init+restore / emitter redirect) ported 2026-06-06; slot+744≡slot+424 SAME-BYTES alias corrected 2026-06-06 (phantom prtResult deleted)
metadata:
  type: project
---

type-4 particle interpolation chain PORTED 2026-06-06; **regression corrected 2026-06-06** (slot+744 alias).

**Why:** brief claimed two "前置缺口" (eval type-4 branch unported; slot+744 no consumer) — BOTH FALSIFIED. Then a FIRST port introduced a phantom field that caused a real regression (caught by binary-alignment-auditor + self-disassembly).

== THE ALIAS (self-disassembled, decisive — this is the corrected understanding) ==
**slot+744 ≡ slot+424 — the SAME 9-double physical block, ONE prt block, not two.**
- Slot base = node+320+536*idx (INTERP branch `ADD X20,X19,#0x140` @0x69a0f8; merge X21 base; advanceNodeFrames `ADD X9,X20,#0x140` @0x6b7ef8).
- COPY branch @0x699c2c: `v11=node+536*idx` (NO +320), reads `v11[93..101]` = node+536*idx+744..808.
- INTERP branch @0x69a0f8: reads node+320+536*idx+424..488.
- restore @0x699890: dest = node+536*idx+744 (`X8=X20+536*idx`, `ADD X0,X8,#0x2E8`).
- ARITHMETIC: node+536*idx+744 == node+320+536*idx+424. **COPY's 744 = INTERP's 424 = prt block.**

== Field contracts (fresh-decompiled) ==
- prt block slot+424..488 = ClipSlot {prtFmin,prtF,prtVmin,prtV,prtAmin,prtA,prtZmin,prtZ,prtRange} (9 doubles). Field map [0]424=fmin [1]432=fmax [2]440=vmin [3]448=vmax [4]456=amin [5]464=amax [6]472=zmin [7]480=zmax [8]488=range. (slot+416 trigger int separate.)
  - **TWO writers:** (1) mergeFrameContent @0x693d98 — main per-frame writer (gate mask&0x100000, reset defaults @0x693d20 {trigger0,fmin/fmax10.0,v/a0,z1.0,range0}); (2) HM3 restore @0x699890 — HM3 path only (memcpy V+600..664 -> slot+744 == slot+424, gate nodeType4&&V+32==0).
  - **TWO readers:** COPY branch @0x699c2c (single-slot) + INTERP branch @0x69a0f8 (crossfade lerp). Both read the SAME bytes.
- node+2224..2288 (MotionNode::particleInterp[9]) = eval-output mirror, written by COPY/INTERP, READ by emitter @0x6BF0DC (node4[139..143]). AUTHORITATIVE emitter read surface, NOT slot prt-block direct.
- V+600..664 (PerNodeLayerState::particleInterp[9]) = snapshot of node+2224..2288 (eval mirror), init @0x6995dc (Q0<-node+2224 etc). NOT a snapshot of prt block.
- V+672 = particleArraySnapshot tTJSVariant (init V+672<-node+2296; restore node+2296<-V+672). NOT released by value_destroy @0x6DD06C (transient like V+544 childPlayerSnapshot).

== REGRESSION + FIX (2026-06-06) ==
FIRST port invented `ClipSlot.prtResult[9]` as a SEPARATE slot+744..808 region (claimed "SOLE WRITER=restore, SOLE READER=COPY"). FALSE: slot+744≡slot+424 means COPY reads the prt block that merge writes EVERY frame. The phantom split the one physical block into two: COPY read prtResult (zero in normal playback → emitter inert REGRESSION) instead of prtFmin..prtRange (merge's per-frame values).
FIX: (1) deleted ClipSlot.prtResult[9]; (2) COPY branch reads prtFmin..prtRange (== INTERP srcA); (3) restore writes prtFmin..prtRange from v.particleInterp[0..8].
**Corrected data flow:** normal playback = merge writes prt block / COPY+INTERP read prt block / mirror node+2224 / emitter → particles ACTIVE (no longer falsely inert). HM3 restore = restore writes prt block (V+600..664 = old mirror snapshot) / eval reads it / mirror / emitter. Both paths write+read the SAME prt bytes.

== Falsified claims corrected ==
- "slot+744 separate region / prtResult phantom" → deleted, IS slot+424 prt block.
- "slot+744 SOLE WRITER=restore" → TWO writers (merge per-frame + restore HM3).
- "normal non-crossfade COPY reads slot+744=0 → emitter inert = IDENTICAL to binary" → WRONG; COPY reads merge's live prt values, particles active.
- Comments fixed in MotionNode.h, PlayerUpdateLayerEval.cpp, PlayerFrameProgress.cpp, value_structs.h; IDB @0x699c30 + @0x699890.

== Verification ==
web debug 248/248 + krkr2_wasmtime_guest clean. motion_playback --only-structural m2logo(93)+yuzulogo(243) PASS bit-identical. ORACLE-INERT for logo (logo type-4 nodes don't exercise crossfade/HM3-restore; structural diff unaffected) — honest gap, no prt unit test, no fixture fabricated.
