---
name: emoteplayer-dual-class-registration
description: Binary has TWO EmotePlayer-family NCB classes; Motion.EmotePlayer is NOT finalize-only (refutes prior review P2-2)
metadata:
  type: project
---

libkrkr2.so exposes TWO distinct EmotePlayer-family NCB classes, both with full APIs.

**Fact:** `EmotePlayer_loadClass` (0x685BC0) calls BOTH `EmotePlayer_NCB_classInit` (0x686148,
registers `finalize`->noop 0x6862C8) AND `EmotePlayer_ncb_registerMembers` (0x67FAC8, ~69-member
Player-engine-facing API) into the SAME class object, which is then registered as
`Motion.EmotePlayer`. xrefs_to confirm both 0x67FAC8 and 0x686148 are called only from 0x685BC0.

The second class is `D3DEmotePlayer`: `D3DEmotePlayer_ncb_register` (0x541D98) ->
`D3DEmotePlayer_ncb_registerMembers` (~0x52E4xx), 54 D3D-shell members + MaskMode/TimelineFlag consts.

**Why:** The prior analysis (analysis/MotionPlayer_Restoration_Review_2026-05-30.md P2-2 and
EmotePlayer_Internal_Implementation.md) claimed "binary EmotePlayer only registers finalize" — this
is WRONG; it missed loadClass's second call. Local main.cpp acted on the wrong claim: it leaves
Motion.EmotePlayer with only NCB_CONSTRUCTOR and puts a hybrid API on a separate D3DEmotePlayer class.

**How to apply:** When auditing/fixing EmotePlayer NCB membership, the API-bearing class is
Motion.EmotePlayer (0x67FAC8 set incl. setHairScale/setPartsScale/setBustScale via sub_681F20/28/30).
Do not trust "finalize-only" in the older analysis docs. Native instance shell = 24B
{vtable@0, EmoteEngine* payload@+8, sticky/owned byte@+16}; destroy gate (0x6862D0) = `+8 && !+16`.
Full ledger: analysis/audit_motionplayer_2026-05-30/clusterC_emoteplayer_ncb.md.
