---
name: emoteengine-1496b-hashmap-layout
description: Verified EmoteEngine (1496B) full byte layout — 7 inline unordered_maps + 4 vectors, NOT raw byte blocks. From ctor 0x67E38C / dtor 0x67F4B8.
metadata:
  type: project
---

EmoteEngine = 1496B (0x5D8), no vtable, raw pointers. ctor EmoteEngine_ctor @0x67E38C, dtor EmoteEngine_dtor @0x67F4B8 (shared by EmotePlayer + D3DEmotePlayer). Decompiled & verified 2026-05-30.

**Why:** local EmoteEngine.h mis-modeled bytes 824..991 / 1272..1439 as `uint8_t[]` placeholder blocks (_inlineVectorBlocks_*, _scalarField_824..864, _residual_*). They are actually 7 libstdc++ inline `unordered_map<ttstr,V>` + 4 `std::vector<tTJSVariant*>`. The local `_bindListHead@1456` pseudo-field physically IS HM#7's `_M_before_begin._M_nxt`.

**How to apply:** when aligning EmoteEngine layout, model these as typed maps/vectors (mirroring the existing `detail::LabelValueMap` @+1440), delete `_bindListHead`, and DO NOT trust the old `class-layout-auditor/emoteengine_1496b_layout.md` "4 inline vector reserve(10)" note (it was wrong; review P0-2 overturned it).

Verified byte map (a1=_QWORD*, a1[N]=byte 8N):
- 0..799    : 10x KiriKiri deque (80B each, headers via sub_684A50/684BCC/684D58/684EAC/685000/685198/685314/6854AC/685628). Each block holds 10 elems (480 = 10*48 for #1).
- 800..823  : std::vector<tTJSVariant*> (begin/end/cap). dtor releases each elem + delete.
- 824..879  : HM#1 unordered_map<ttstr,V1> (56B libstdc++ _Hashtable). dtor releases node+8 (ttstr key) via tTJSVariant_Release.
- 880..935  : HM#2 same shape, value V2.
- 936..991  : HM#3 unordered_map<ttstr,V3> — dtor walks nodes with sub_683E40(node+1), NOT tTJSVariant_Release. Distinct value type.
- 992..1063 : 3x std::vector<tTJSVariant*> @992/1016/1040 (memset a1+124,0,0x48). dtor releases+deletes each.
- 1064      : Player* (a1[133]) new(0x568)=1384B. Player_ctor @0x6CED30, Player_dtor @0x6CFADC.
- 1072..1120: 7 controller ptrs — VarController(2)@1072 pos, VarController(1)@1080 scale, VarController(4)@1088 color, AngleController@1096, VarController(2)@1104 hairparts, VarController(2)@1112 bust1, VarController(2)@1120 bust2.
- 1128      : heap ptr (matrix/transform alloc). dtor: operator delete.
- 1159      : byte syncWaiting (progress gate: dt!=0 && !syncWaiting).
- 1160      : int32 =1.
- 1162      : byte _dirty (=1 in ctor, cleared per dt-slice in progress).
- 1168/1176 : double meshDivisionRatio + dup (=1.0).
- 1184/1192 : double bust spring constants (stepBust strength args).
- 1200      : double =1.0.
- 1208/1228/1248 : 3 small objects freed via sub_A0F778 (20B stride each).
- 1272..1327: HM#4 unordered_map<ttstr,V4>.
- 1328..1383: HM#5 — dtor sub_68577C(a1+1328) extra cleanup before node walk @1288. (note: 1288/1272 vector pair freed too — recheck which map owns which on next pass)
- 1384..1439: HM#6 unordered_map<ttstr,V6>.
- 1440..1495: HM#7 unordered_map<ttstr,double> = detail::LabelValueMap. _M_before_begin@1456 = node head (progress bind-loop + dtor walk it). This ends the 1496B struct.

HM node = operator new(0x20)=32B: {_M_nxt@0, ttstr_key@8, value@16, cached_hash@24}. Upsert helper ttstr_doubleMap_upsert @0x686944 (renamed from Player_HM2_upsert_labelToValue — it's a SHARED generic helper for both Player HM2@+320 and EmoteEngine HM7@+1440, hashes ttstr via c_str+68 cached hash, returns node+16). progress @0x67D01C writes deque step outputs (256/336/416/576/656/736) into HM7 as doubles.

OPEN: value types of HM#1/2/3/4/5/6 require reversing the setVariable dispatch write paths (not yet done). Only HM#7 value type (double) is fully confirmed.

**ABI note (PLATFORM_BOUNDARY):** libc++ unordered_map ~32-40B != libstdc++ 56B, so sizeof(EmoteEngine) on Web build cannot equal 1496B. Offset comments are for traceability; logical contract = K/V types + hash + lifetime, per CLAUDE.md accepted boundary (same posture as player_containers.h).
