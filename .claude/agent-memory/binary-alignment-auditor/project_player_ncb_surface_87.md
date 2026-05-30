---
name: player-ncb-surface-87
description: Player_ncb_registerMembers @0x6D69C8 registers exactly 87 members (not 92); RO/RW + method encoding rules; M15 over-removals and misbindings
metadata:
  type: project
---

Audited 0x6D69C8 (Player_ncb_registerMembers) directly 2026-05-31 (read-only).

**Count: 87 distinct members, NOT 92.** Prior clusterE doc / MASTER M15 over-counted
by treating the `L"Property"`/`L"Function"` descriptor TAG literals as members.
Authoritative count = `sub_6F6970(*a1, L"name", desc+32)` calls + direct
`sub_6D97B4`/`sub_6D993C(a1, L"name", cb, 0)` method calls.

**Binding encoding (decompiled):**
- Property: operator new(0x50u); desc+16=2; getter@+48; setter@+64. **+64==0 ⇒ RO**,
  non-zero / _OWORD get+set pair ⇒ RW. Tag `L"Property"`. Register via sub_6F6970.
- Method: operator new(0x40u), +16=1, one closure@+48, tag `L"Function"` (via sub_6F6970)
  OR direct sub_6D97B4/sub_6D993C(a1,L"name",cb,0).
- Hex-Rays SSA aliases getter/setter symbol names off-by-one — do NOT trust the
  printed getter name; trust the structural +64==0 literal in each member's own
  new(0x50) block for RO/RW.
- sub_6F6970 = NCB add-member; sub_6D97B4/sub_6D993C = direct method register.

**RO properties in binary (local binds several RW — DEVIATION):** loopTime,
variableKeys, syncWaiting, frameLastTime, frameLoopTime, hasCamera, cameraTarget,
cameraPosition, cameraFOV, cameraAlive, resourceManager, processedMeshVerticesNum,
lastTime, bounds, playing, allplaying, tags, defaultSyncActive. (local correctly
RO only for lastTime/bounds/playing/allplaying.)

**onAction/onSync/onGroundCorrection are METHODS** (Function tag, single closure),
not properties. Local main.cpp:145-147 wrongly binds them NCB_PROPERTY. onSync cb =
nullsub_88 (stub). 

**M15 over-removed 11 GENUINE binary members** (D-01 + M-colorWeight cleanups):
stealthChara, stealthMotion, tags(RO), project, meshline, useD3D,
independentLayerInherit (binary has BOTH colorWeight @0x6D7748 AND
independentLayerInherit @0x6D77C0 as distinct members — not aliases), setVariable,
getVariable, getCommandList, onFindMotion. These ARE at 0x6D69C8 and must be restored.
The ~22 timeline methods (countVariables/*Timeline*) were correctly removed (truly
D3DEmotePlayer-only, absent from 0x6D69C8).

**Port EXTRAS not at 0x6D69C8:** meshDivisionRatio (M15 added wrongly — it's
EmoteEngine/D3DEmotePlayer-level, not Motion.Player), tickCount, frameTickCount,
syncActive, cameraActive (scalar props, wrong-class hoist/invention), plus host
adapter methods (unload/findMotion/setSize/captureCanvas/loadSource/... — web-port,
no PLATFORM_BOUNDARY comment so not sanctioned).

M15 PASSES (correctly added/bound): x/y/left/top, angleDeg/angleRad, transformOrder/
coordinate, flipX/Y slantX/Y zoomX/Y visible opacity (RW props), pixelateDivision,
bounds(RO) lastTime(RO), setCoord/contains/clear (methods), colorWeight (RW, bound to
+1097 bool getter/setter per documented binary alias).
