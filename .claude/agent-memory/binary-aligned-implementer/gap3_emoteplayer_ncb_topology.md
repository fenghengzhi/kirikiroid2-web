---
name: gap3-emoteplayer-ncb-topology
description: Gap3 — three distinct binary NCB classes (EmotePlayer/Player/D3DEmotePlayer); Motion.EmotePlayer full 70-member surface restored
metadata:
  type: project
---

# Gap 3: Motion.EmotePlayer NCB surface — DONE 2026-06-03

## Binary NCB topology (fresh-decompiled, settled)
THREE distinct NCB classes, NOT a class-name mislabel:
1. **Motion.EmotePlayer** — class name str @0x14C1E9C="EmotePlayer". Registered by
   `emoteplayer_entry@0x682528` → `EmotePlayer_loadClass@0x685BC0` →
   `EmotePlayer_NCB_classInit@0x686148`(registers `finalize`) THEN
   `EmotePlayer_ncb_registerMembers@0x67FAC8`(**70 members + 2 consts**
   TimelinePlayFlagParallel/Difference). Native instance = 24B
   (`EmotePlayerNativeInstance_create@0x6862C8`, vtable off_1A18BB0).
2. **Motion.Player** — `Player_ncb_registerMembers@0x6D69C8` (92 members). The
   local `Player` class. Aliased into Motion namespace in PostRegistCallback.
3. **D3DEmotePlayer** — in **DrawDeviceD3D.dll** plugin (NOT Motion ns),
   registered by `sub_42C7F8` table @0x1A02C10 → `D3DEmotePlayer_ncb_register@0x541d98`
   → `D3DEmotePlayer_ncb_registerMembers@0x52E504` (**54 members**). class name
   str literally "D3DEmotePlayer".

## FALSIFIED prior conclusions (corrected in-place)
- EmotePlayer.h:36 said "二进制只注册一个 finalize 成员;无 script-facing API" —
  **WRONG**. 0x685BC0 registers the full 70-member API after finalize. Corrected.
- "整类暴露面缺失" (original review) = **partially TRUE**: local Motion.EmotePlayer
  registered ONLY ctor; binary has 70 members. NOT a class-name mislabel — the
  binary genuinely has a separate EmotePlayer class with full API. Now restored.

## Dispatch model (key insight)
Motion.EmotePlayer is a **parallel NCB facade over the SAME Player/EmoteEngine
machine** as Motion.Player. Member callbacks are mostly `Player_*`/`sub_*` that
operate on the underlying object: `progress`=sub_6818B4 calls Player_preProgress,
Player_HM2_upsert_labelToValue (+1440), bindParameterValue (+1064), the controller
step deques (+256..+736) — IDENTICAL to EmoteEngine_progress@0x67D01C. So the 70
members forward to the same logic D3DEmotePlayer/Player already implement.

## Local implementation (this session)
- EmotePlayer class (EmotePlayer.h, moved AFTER EmoteObject for inline accessor
  completeness) now holds an EmoteObject chain (`_primaryObj`) reaching player()/
  engine() — same pattern as D3DEmotePlayer. 70 members + 2 consts registered in
  main.cpp NCB_REGISTER_SUBCLASS_DELAY(EmotePlayer) in EXACT binary order.
- Two OPEN thin impls (registered, faithful stub, marked): #4 initPhysics
  (sub_67D4D0 physics builder not ported), #68 activateSelectorTarget (sub_67581C
  selector option-scan+applySelection re-step; Player has only deactivate@0x675BF4).

## Self-audit field corrections (caught after first build — fixed)
Decompiled the inferred members; several field mappings were WRONG and fixed:
- #39/40/41 setHairScale/setPartsScale/setBustScale (sub_681F20/28/30) write
  EmoteEngine +1184/+1192/+1200 (= _bustSpring1Const/_bustSpring2Const/
  _scalarField_1200_1d), NOT the _hairScale/_partsScale/_bustScale(+1080) fields.
- #42/43/44 hairScale/bustScale/partsScale PROPERTIES read the SAME +1184/+1200/
  +1192 fields (getters sub_681F38=+1184, sub_681F40=+1200, sub_681F48=+1192).
  So hairScale↔+1184, bustScale↔+1200, partsScale↔+1192 (note bust/parts swap).
- #45/46/47/48 debugPrint/queuing/directEdit/selectorEnabled are EmoteEngine
  BYTE flags +1163/+1161/+1159/+1160, NOT shell bools or Player. Setters are
  "set-always-1 trigger" (write field=1, IGNORE arg). selectorEnabled setter
  also calls sub_670D1C (open). Named +1163 = _debugPrintFlag in EmoteEngine.h.
- #38 modifyRoot (sub_681F0C) takes NO args — sets flag *(Player+1064→+200→
  +1584)=1. NOT Player_modifyRoot(data). Thin STUB (open, +200/+1584 not modelled).
- #49 variableKeys (sub_681FA0) copies EmoteEngine+1208 (_smallObj1208 20B blob,
  unmodelled) — local approximates via Player::getVariableKeys (DEVIATION, open).
- #33 bounds (sub_681EAC -> sub_6CCA84(Player,out)) = Player::getBounds OK.
- #37 setCameraOffset (sub_681EF8) = raw (x,y) write Player+144/+148 OK.
- Added Player::setCameraOffsetXY_0x681EF8 (raw float write +144/+148) — the
  EmotePlayer #37 setCameraOffset takes (x,y) doubles, distinct from Motion.Player
  setCameraOffset(tTJSVariant) @0x6D9A38.
- Member ORDER verified 70/70 exact match against literal-extraction of 0x67FAC8.

## M6 NON-regression
The 70 members are SUBCLASS members, NOT Motion-namespace free-functions, so they
do NOT touch the M6 namespace-attach crash path. wasmtime guest links clean
[8/8], motion_playback differential PASS (m2logo 93f, yuzulogo 243f).

## Oracle status
Logo TJS uses Motion.Player / D3DEmotePlayer, NOT Motion.EmotePlayer → this
surface is oracle-inert for existing differentials (per CLAUDE.md NOT a defer
reason; registration-surface restoration is the deliverable; logo PASS = non-
regression guard). D3DEmotePlayer block has NO duplicate registrations (the old
bodyScale/playCallback/setTimeline/addPlayCallback dups were already cleaned).
