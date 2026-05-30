---
name: player-ncb-0x6D69C8-registration
description: Authoritative Motion.Player NCB member table from Player_ncb_registerMembers @0x6D69C8 — 58 props (41 RW + 17 RO) + 32 methods; how to enumerate kind/RO from disasm
type: project
---

`Player_ncb_registerMembers` @0x6D69C8 registers exactly **90** Motion.Player members: **58 properties** (41 RW + 17 RO) + **32 methods**. (NOT "92-entry table" — that count was wrong in old main.cpp comments. The first two name-xrefs `defaultSyncActive`/`defaultTransformOrder` are constants, not members.)

**Why:** main.cpp had drifted — M15 commits (9fb64d4/bece216/7753cc7/9dc72a4) deleted 11 real members as "port invention"; f675202 entangled colorWeight/independentLayerInherit; 12 RO props were bound RW; onAction/onSync/onGroundCorrection were bound as properties but are methods.

**How to apply / how to re-enumerate the table:**
- Each member = a descriptor built via `new(W0)` → fields → `ADRL X1,<name>; BL sub_6F6970`. `W0=0x50` ⇒ property, `W0=0x40` (or 0x18 for play/progress, "Function" marker) ⇒ method.
- ~8 methods register via `sub_6D993C(X0=this,X1=name,X2=cb)` instead of sub_6F6970 (e.g. onSync, setCameraOffset, releaseSyncWait, skipToSync) — so BL-count (84) < member-count (90). Use the name-xref enumeration, not BL count.
- **RO vs RW**: RO property descriptor emits `STP XZR,XZR,[X20,#0x40]` + `STR XZR,[X20,#0x38]` (null setter slot). RW emits `STP Q1,Q0,[X20,#0x30]` (getter+setter in V0/V1). Scan ~20 lines before the `ADRL X1,<name>` anchor.

**The 17 RO properties:** resourceManager, lastTime, loopTime, variableKeys, tags, cameraTarget, cameraPosition, cameraFOV, cameraAlive, bounds, playing, allplaying, syncWaiting, frameLastTime, frameLoopTime, hasCamera, processedMeshVerticesNum.

**Disentangled accessors (f675202 fix):**
- `colorWeight` (RW, name@0x6d7740): get=sub_6CD710 / set=sub_6CD724 — read/write Player+1156 (_colorWeightPacked uint32) with R/B byte swap (b0<->b2). Local: getColorWeight/setColorWeight (already correct impl).
- `independentLayerInherit` (RW, name@0x6d77b8): get=Player_getColorWeightFlag(sub_6D9768)=Player+1097 bool / set=sub_6CC9D4 (writes +1097, marks each node+1584 dirty if changed). Local: getIndependentLayerInherit/setIndependentLayerInherit.
- These are TWO DISTINCT members. The old comment "colorWeight cb = +1097 bool" was a mis-attribution.

**onAction/onSync/onGroundCorrection are METHODS (Function-kind), not properties:**
- onAction @0x6d8ed0 cb=nullsub_87 @0x6D9A50 (empty no-op)
- onSync @0x6d8edc cb=nullsub_88 @0x6D9A54 (empty no-op; via sub_6D993C)
- onGroundCorrection @0x6d8f6c cb=Player_onAction_ncb @0x6D9A58 → sub_A0F5E0 (tTJSVariant copy/AddRef helper, NO Player state change) ⇒ faithful port = no-op method.
- Port: added no-op `void onAction()/onSync()/onGroundCorrection() {}` in Player.h; the old getOn*/setOn* property storage is port-invented and no longer NCB-exposed.

**Recovered members (binary-confirmed, were wrongly deleted):** stealthChara (RW, reuses getChara/setChara), stealthMotion (RW, getStealthMotion/setMotion_stealth), tags (**RO**, getter=Player_getStealthMotionStr→local getTags), project (RW), meshline (RW), independentLayerInherit (RW), useD3D (RW, set=Player_setUseD3DFlag), setVariable (method, cb=loc_6D0E70→setVariableCompatMethod), getVariable (method), getCommandList (method, cb=loc_6D3A4C), onFindMotion (method).

**The 24 timeline methods (countVariables, getVariableLabelAt, playTimeline, ...) are correctly D3DEmotePlayer-only — verified ABSENT from 0x6D69C8.** Do NOT recover onto Motion.Player.

**Remaining 1:1 gap (NOT yet fixed):** 19 port-extra resource/draw-device methods still bound on Motion.Player but absent from binary 0x6D69C8: adjustGamma, captureCanvas, clearCache, copyRect, doAlphaMaskOperation, findMotion, findSource, frameProgress, getD3DAvailable, isPlaying, loadSource, releaseLayerId, requireLayerId, setClearColor, setResizable, setSize, unload, unloadAll, unloadUnusedTextures. Left bound because host adapter calls them internally; removing needs a separate scoped pass.
