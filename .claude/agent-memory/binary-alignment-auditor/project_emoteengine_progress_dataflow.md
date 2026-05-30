---
name: emoteengine-progress-dataflow
description: EmoteEngine cluster B audit (2026-05-30) — progress() real dataflow divergences vs binary, plus what's already fixed. For future motionplayer EmoteEngine audits.
metadata:
  type: project
---

EmoteEngine cluster B audit 2026-05-30. Full ledger: analysis/audit_motionplayer_2026-05-30/clusterB_emoteengine.md.

**Already FIXED (do not re-flag):** P0-2 (7 typed unordered_map + 4 VariantPtrVector, _bindListHead deleted), P2-4 applyVarControllers order pos->color->scale->angle, P2-4 ctor reset order 134/135/137/136. EmoteEngine.h layout + EmoteEngine.cpp ctor/dtor/applyVarControllers are aligned.

**Addresses:** progress real body = 0x67D01C (0x530a5c is a thunk into it). applyVarControllers 0x6766E0, ctor 0x67E38C, dtor 0x67F4B8, stepHairParts 0x67B748, stepBust 0x67BCE8, EmoteObject_init 0x67DBAC, EmoteObject_destroy 0x67F420, upsert ttstr_doubleMap_upsert 0x686944.

**Open P0 in EmoteEngine.cpp progress() (235-315):**
- P0-B1: binary's dt-slice loop iterates 6 deques (#4@256,#5@336,#6@416,#9@656,#8@576,#10@736-inline-LUT) writing each output into HM7@1440 via upsert; local STUB_WARNs all 6 and writes nothing. Order in binary is #9 BEFORE #8 (local comment says #8,#9 — wrong). Inert now (deques empty) but real divergence + needs libstdc++ deque block-walk (Phase C).
- P0-B2: binary wraps whole body in `if (dt != 0.0)`; local has no guard.
- P0-B3: binary post-loop (sub_6D2A54 + physics gate + stepHairParts/stepBust) uses ORIGINAL dt (saved v12), local uses RESIDUAL dt after the slice loop drained it to ~0. LOCAL-ONLY FIX: save original dt to a separate var.
- P0-B4: bind-loop walks HM7 _M_before_begin._M_nxt insertion-order chain (`for(i=*(this+1456);i;i=*i)`), body sub_67C560/67C6B0/Player_bindParameterValue; local iterates std map (bucket order) with empty body.

**P1:** EmotePlayer.h:310 `unique_ptr<EmoteObject> _emoteObj` — binary uses raw new/delete (EmoteObject_destroy 0x67F420). EmoteObject's own _engine is correctly raw ptr; only parent hold is wrong. EmoteObject_init front-loads PSB load+Player_play INTO the ctor (eager); local splits across setModule/setChara/setMotion (different call-chain topology). applyVarControllers Player applies (setCoord/setSlant/setAngleDeg/sub_6CD724) all commented out.

**MISSING entirely:** stepHairParts, stepBust (full physics integrators, only STUB_WARN in progress), 6 deque step fns, the bind callbacks.

**IDB renames done this session:** EmoteObject_init@0x67DBAC, VariantPtrVector_assign_67F0CC, EmoteObject_applyChara_67F370.
