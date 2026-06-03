---
name: ncb-surface-verdict
description: Fresh-decompile member-by-member verdict for EmotePlayer/Player/D3DEmotePlayer/ResourceManager/ObjSource NCB registration tables — counts, order, the 4 D3DEmotePlayer port-invented duplicates
metadata:
  type: project
---

2026-06-03 fresh decompile of all NCB registration functions. Verdict per class:

- **EmotePlayer @0x67FAC8** (loadClass 0x685BC0 -> classInit 0x686148 `finalize` + this): 70 members + 2 consts (TimelinePlayFlagParallel/Difference). Local `NCB_REGISTER_SUBCLASS_DELAY(EmotePlayer)` is a **PERFECT match**: 70 members, exact names, exact order, 2 consts. The old "Motion.EmotePlayer is ctor-only / only finalize" claim was already corrected in code and is fully disproven. Member-add primitive = sub_68C664 (52x) + helpers sub_6817C0/sub_681944/sub_681B54/sub_681680(consts). Native instance 24B @0x68629C.

- **ResourceManager @0x6AB8BC** = 12 members in order: loadSource, clearCache, bufLayer(prop RO), load, unload, unloadAll, isExistMotion, findMotion, findSource, random, requireLayerId, releaseLayerId. Local matches exactly. setEmotePSBDecryptSeed/Func are local TJS_STATICMEMBER extras (flagged port extras).

- **ObjSource @0x69CCB8** = 6 members: originX, originY, width, height, clip (5 prop RO) + drawLayer (method). new(0x18)=24B. Local matches exactly.

- **D3DEmotePlayer @0x52E504** (DrawDeviceD3D.dll) = 54 members + 4 consts (MaskModeStencil/MaskModeAlpha/TimelinePlayFlagParallel/TimelinePlayFlagDifference). add primitive = ncb_addMember + helpers sub_52FC90/sub_530328/sub_53043C. **Binary uses NAME/callback mismatches by design**: member `clear`->create cb, `queing`->set/getBustScale, `bustScale`->set/getBodyScale, `setTimelineBlendRatio`->setTimeline cb, `pass`->addPlayCallback cb, `modified`->getPlayCallback getter.
  - **REAL GAP — 4 port-invented duplicate members NOT in binary** (cpp/plugins/motionplayer/main.cpp:842-956): `bodyScale`(865), `playCallback`(876), `setTimeline`(884 plain NCB_METHOD), `addPlayCallback`(940 plain NCB_METHOD). Local reproduces the binary's mismatched member AND adds a real-name alias -> table 54->58. These 4 should be removed for 1:1.
  - **Order mismatch (low sev)**: binary interleaves props+methods in registration sequence (module, clear, load, clone, show, hide, visible, smoothing, meshDivisionRatio, queing, hairScale, partsScale, bustScale, assignState, setCoord, setScale, getScale, setRot, getRot, setColor, getColor, countVariables...). Local groups all properties first then all methods. NCB end-state dict is order-insensitive for lookup, but registration order is a fidelity dimension.

Lifecycle (fresh decompile):
- EmotePlayer native inst 0x68629C: new(0x18), +8 EmoteEngine*=0 (lazy), +16 sticky=0. destroy 0x6862D0 gate `if(+8 && !+16)` -> sub_67F4B8(=EmoteEngine_dtor) DIRECTLY on +8. NO EmoteObject middle layer in binary. Local routes through EmoteObject* _primaryObj (ABI deviation, documented platform necessity). Local EmotePlayer ctor never creates _primaryObj -> player()/engine() would null-deref (open functional gap; binary also lazy but creates engine somewhere in finalize/sub_6863C4 path — not yet traced).
- EmoteObject 40B @0x67DBAC: +0 RM*(new 0xE8=232B, ctor sub_6A88CC), +8 EmoteEngine*(new 0x5D8=1496B), +16/+24/+32 vector<tTJSVariant*>. destroy 0x67F420 order: engine, then RM, then vector-release+free. Local member-destruction order aligns.
- D3DEmotePlayer dtor 0x533C00: a1[3]=primary(+24), a1[4]=secondary(+32). Order: destroy SECONDARY first then PRIMARY, each EmoteObject_destroy+delete, then base dtor +56 vfunc. Local dtor (EmotePlayer.cpp:67) `delete _secondaryObj; delete _primaryObj;` matches. load/create/clear slot teardown matches.
