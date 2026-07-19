---
name: setchara-field-map
description: Player_setChara@0x6D94B0 + sub_6B29C0 field map; corrects the "+968 gate" misread (+968 IS the chara slot, not a boolean gate)
metadata:
  type: project
---

R0-2 setChara architecture. Fresh-decompiled again 2026-07-18; the property
table off-by-one and source-level value types below supersede the 2026-06-03
wording.

**CORRECTION of the lead's premise:** "+968 gate" is WRONG. +968 is NOT a boolean gate.
`Player_getStealthChara`@0x6D9490 reads `*(this+968)` and returns it -> **+968
is the live stealthChara refcounted string-value slot**. The `if(*(+968))` in
its setter tests whether that live slot exists.

**Player field map (definitive, from Player_playImpl@0x6B2284 + getters):**
- +768 = pending stealthMotion string-value owner
- +776 = pending stealthChara string-value owner
- +960 = primary chara (NCB `chara`, getter 0x6D9470)
- +968 = live stealthChara (getter 0x6D9490)
- +976 = primary motion; +984 = stealthMotion
- +1099 = public `playing` byte (getter 0x6D9794)

**sub_6B29C0@0x6B29C0** (renamed Player_setCharaOrKeySlot_dedup): chara/key slot writer.
- a2&0x10 -> dedup-read target = +968(stealthChara); else +960(chara).
- dedup via wcscmp sub_9B1ED0: returns early if unchanged (after type-tag check *((u32*)slot+15)==*(u32*)(new+60)).
- on change: AddRef new, Release old, ALWAYS writes +968; if (a2&0x10)==0 ALSO writes +960.
- THEN clears +976/+984 motion slots + +1099 playing => **chara change invalidates loaded motion**.

**Player_setChara@0x6C0E9C:** writer flags=0, then flush +776 through
flags=16. **Player_setStealthChara@0x6D94B0:** if +968 exists, writer flags=16
then flush +776; otherwise queue the request in +776.

**Local impl (PlayerCore.cpp/PlayerTimeline.cpp, 2026-07-18):** four live
`ttstr` values and two pending `ttstr` owners are independent; primary chara
writes both chara slots, both setters flush +776 exactly, play flushes +768,
and the same-motion gate selects +976/+984. Native build and existing tests
show no new regression.

Family: Player_setChara@0x6C0E9C = writer(flags=0)+flush+776;
Player_setStealthChara@0x6D94B0 = writer(flags=16)/queue+776;
Player_play@0x6B21E8 = playImpl+flush+768.
