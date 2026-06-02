---
name: setchara-field-map
description: Player_setChara@0x6D94B0 + sub_6B29C0 field map; corrects the "+968 gate" misread (+968 IS the chara slot, not a boolean gate)
metadata:
  type: project
---

R0-2 setChara architecture. Fresh-decompiled 2026-06-03. Corrects the task lead's "+968 gate" framing.

**CORRECTION of the lead's premise:** "+968 gate" is WRONG. +968 is NOT a boolean gate.
`Player_getChara`@0x6D9490 reads `*(this+968)` and returns it -> **+968 IS the chara tTJSVariant* value slot**. The `if(*(+968))` in setChara just tests "has chara ever been set" (first-set vs subsequent-set branch).

**Player field map (definitive, from Player_playImpl@0x6B2284 + getters):**
- +768 = stealth motion slot (Player_play stores stealth var here when flags&0x10 && !+968)
- +776 = pending chara override slot (only producer: childMotionPass@0x6BE0C0 / stealth pass; NO setChara-path producer)
- +960 = variableKeys cache (Player_getVariableKeys@0x6D9470 reads it) — NOT a chara mirror
- +968 = chara value (Player_getChara reads it)
- +976 = motion key slot; +984 = motion2/stealth motion slot
- +1099 = motion-loaded byte flag (cleared on load fail and on chara change)

**sub_6B29C0@0x6B29C0** (renamed Player_setCharaOrKeySlot_dedup): chara/key slot writer.
- a2&0x10 -> dedup-read target = +968(chara); else +960(variableKeys).
- dedup via wcscmp sub_9B1ED0: returns early if unchanged (after type-tag check *((u32*)slot+15)==*(u32*)(new+60)).
- on change: AddRef new, Release old, ALWAYS writes +968; if (a2&0x10)==0 ALSO writes +960.
- THEN clears +976/+984 motion slots + +1099 flag => **chara change invalidates loaded motion**.

**Player_setChara@0x6D94B0:** if *(+968) set -> sub_6B29C0(this,16,&v) then flush pending +776 (re-apply as chara). else first-set -> AddRef v, store into +776 (NOT +968).

**Local impl (Player::setChara, PlayerCore.cpp):** was plain `_chara=v`. The missing architectural piece = chara-change->motion-invalidation side effect (NOT a "replay dispatch"; lead's "double replay" framing also wrong). Implemented: dedup `if(_chara==v) return;` then `_chara=v; _activeMotion.reset(); _motionKey=ttstr();`. ttstr `_chara` value-semantics = platform-independent equiv of +968 refcounted slot (NOT a deviation). +776 pending-flush branch collapses (no local producer). Verification: oracle-inert on existing fixtures (no fixture toggles chara on a loaded player); build green web/debug.

Family: sub_6C0E9C@0x6C0E9C (renamed Player_setCharaAndFlushPending_guess) = setChara(this,0,a2)+flush+776; sub_6B2AE8@0x6B2AE8 = setMotion-with-stealth variant.
