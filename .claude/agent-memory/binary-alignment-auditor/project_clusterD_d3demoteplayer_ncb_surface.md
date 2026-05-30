---
name: clusterD-d3demoteplayer-ncb-surface
description: D3DEmotePlayer NCB class-init 0x52E504 verified member table (54 entries), deliberate aliases, and the 4 port-invented EXTRA double-registrations blocking M11 closure
metadata:
  type: project
---

D3DEmotePlayer NCB class-init @ **0x52E504** = exactly 4 const + 50 members = **54 entries**. Verified by full decompile 2026-05-31.

Deliberate NAME/callback aliases (binary registers callback ONLY under the aliased name, never under its honest name):
- clear -> D3DEmotePlayer_create (0x52e680)
- queing -> set/getBustScale (0x52e9a0)
- bustScale -> set/getBodyScale (0x52eb08)
- setTimelineBlendRatio -> D3DEmotePlayer_setTimeline (0x52f53c)
- pass -> D3DEmotePlayer_addPlayCallback (0x52f730)
- modified -> D3DEmotePlayer_getPlayCallback RO prop (0x52f824)

Const: MaskModeStencil=0, MaskModeAlpha=1, TimelinePlayFlagParallel=1, **TimelinePlayFlagDifference=2** (NOT Sequential).

**CORRECTION to clusterD_d3demoteplayer.md line 78**: member `progress` (#50, 0x52f76c) binds **EmoteEngine_progress(this, long double dt)** — a dt-driven frame-stepping fn at ~0x67D000 — NOT "D3DEmotePlayer_progress". Local progress(double dt) is still the right surface target.

Binary has NO standalone members: setTimeline, addPlayCallback, bodyScale, playCallback. These callbacks are exposed ONLY via their alias members above.

**Why:** main.cpp:546-660 correctly replicated all 6 aliases (commits 5dfd5eb/9827e1f) but ALSO kept 4 "honest-named" duplicates -> the callback ends up registered TWICE (once aliased, once honest). This is the recurring failure pattern after an alias-rebind commit.

**How to apply:** When auditing an alias-heavy NCB table, after confirming an alias is replicated, grep for the honest callback name as a SECOND registration. EXTRA double-regs to remove for M11 surface closure: bodyScale(:569), playCallback(:580), setTimeline(:630), addPlayCallback(:644). Also member order setRot/getRot must follow getScale (binary: setCoord,setScale,getScale,setRot,getRot). Surface-only audit; logic-body deviations D-09..D-13 (contains AABB, setOuterForce unpack, setCoord/setScale Animator path, load lazy-ctor, getModule map) remain open in [[project_emoteengine_progress_dataflow]] / cluster G.
