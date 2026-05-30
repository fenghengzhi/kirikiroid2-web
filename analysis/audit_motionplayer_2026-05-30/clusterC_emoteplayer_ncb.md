# Cluster C Audit — EmotePlayer NCB binding + native instance lifecycle

Date: 2026-05-30. Authoritative source: libkrkr2.so. Local: cpp/plugins/motionplayer/.
All binary claims below have a decompile call in this session.

## 0. Verdict

🔧 NEEDS ARCHITECTURAL REFACTOR (one P0 class-identity defect) + ✅ on lifecycle funcs.

The lifecycle / loader / static-init / decrypt-seed functions are all faithfully restorable
and mostly aligned. But the central NCB member-set claim from the prior review (P2-2:
"binary EmotePlayer only registers finalize") is FALSE. `EmotePlayer_loadClass` also calls
`EmotePlayer_ncb_registerMembers` (0x67FAC8), so the binary `Motion.EmotePlayer` class
exposes a full ~69-member script API. The local port instead leaves `Motion.EmotePlayer`
with only a constructor and puts an API on a *separate* local `D3DEmotePlayer` class. The two
EmotePlayer-family classes and their member sets are mismatched against the binary.

---

## 1. Binary call graph (decompiled this session)

```
emoteplayer_static_init (0x42EB00)            -> .init_array: registers "emoteplayer.dll" entry = emoteplayer_entry
emoteplayer_entry (0x682528) [on dll load]
  ├─ LoadModule("motionplayer.dll")           (sub_548A44)
  ├─ find/create namespace "Motion"
  ├─ EmotePlayer_loadClass(0x685BC0, mode=1)
  │     ├─ EmotePlayer_NCB_classInit (0x686148)            -> new(0xB0) class; native ctor=0x68629C; registers "finalize"->0x6862C8(noop ret 0)
  │     └─ EmotePlayer_ncb_registerMembers (0x67FAC8)      -> ~69 script members into SAME class object
  ├─ register class as Motion.EmotePlayer (flag 0x10000 = static? attr)
  ├─ namespace "ResourceManager"
  ├─ register setEmotePSBDecryptSeed (0x685D30) into Motion.ResourceManager (attr 66048=0x10200)
  └─ register setEmotePSBDecryptFunc (loc_685E60) into Motion.ResourceManager (attr 66048)
```

Native instance lifecycle (D3DEmotePlayerNativeInstance vtable off_1A18BB0 == EmotePlayer native):
```
EmotePlayerNativeInstance_create (0x68629C):  r=new(0x18); r[0]=off_1A18BB0; r[+8]=0; r[+16]=0; return r
EmotePlayerNativeInstance_destroy(0x6862D0):  p=this[+8]; if(p && !this[+16]){ EmoteEngine_dtor(0x67F4B8)(p); operator delete(p);} this[+8]=0; this[+16]=0
```

Confirmed: 24-byte shell = {vtable@+0, EmoteEngine* payload@+8, sticky/owned byte@+16}.
Destroy gate = `+8 != 0 && +16 == 0` (matches local EmotePlayer.h comment & P2-1).

---

## 2. P0 — CLASS IDENTITY / MEMBER-SET MISMATCH (refutes prior P2-2)

### Evidence
- xrefs_to(0x67FAC8) = ONLY 0x685C2C inside EmotePlayer_loadClass (0x685BC0).
- xrefs_to(0x686148) = ONLY 0x685C24 inside the SAME EmotePlayer_loadClass.
- In loadClass: `v11[0]=classKey; classInit(v11)` sets `v11[1]=classObj`; then
  `registerMembers(&v9)` where v9=v11, so registerMembers' `*a1`/`**a1` = the SAME classObj.
- => binary `Motion.EmotePlayer` gets `finalize` + the full 69-member API. NOT finalize-only.

### Binary `Motion.EmotePlayer` member set (0x67FAC8, source order)
First member: `finalize` (name passed dynamically as `**a1`, callback off_1A18BE8) — actually
the classInit-registered finalize; registerMembers re-emits a leading Function member too.
Then in order:
TimelinePlayFlagParallel(const=1), TimelinePlayFlagDifference(const=2), progress, frameProgress,
draw, initPhysics, startWind, stopWind, play, clear, getVariable, contains, serialize,
unserialize, pass, setVariable, setCoord, setScale, setRotate, setColor, setOuterForce,
completionType, chara, motion, motionKey, project, maskMode, meshDivisionRatio, outline,
priorDraw, frameLastTime, frameLoopTime, lastTime, loopTime, bounds, processedMeshVerticesNum,
setDrawAffineTranslateMatrix, getCameraOffset, setCameraOffset, modifyRoot, setHairScale,
setPartsScale, setBustScale, hairScale, bustScale, partsScale, debugPrint, queuing, directEdit,
selectorEnabled, variableKeys, animating, setMirror, skip, playTimeline, stopTimeline,
getTimelinePlaying, setTimelineBlendRatio, fadeInTimeline, fadeOutTimeline, getTimelineBlendRatio,
getVariableRange, getVariableFrameList, getMainTimelineLabelList, getDiffTimelineLabelList,
getLoopTimeline, getTimelineTotalFrameCount, getPlayingTimelineInfoList, isSelectorTarget,
deactivateSelectorTarget, getCommandList.  (= 71 named slots incl. 2 constants)

NOTE: these member NAMES and callback addrs are mostly the *Player engine* callbacks
(sub_6818B4, loc_67D018, sub_675E40, sub_681C48, sub_681EF0, sub_681F0C, sub_681F20/28/30,
sub_66EB8C, sub_674F54, sub_6750C0, sub_6754C4, sub_682520, Player_setDrawAffineTranslateMatrix...)
i.e. Motion.EmotePlayer here is effectively the *Player-facing* emote API, NOT a thin shell.

### Local state
- main.cpp:299-301 `NCB_REGISTER_SUBCLASS_DELAY(EmotePlayer)` registers ONLY `NCB_CONSTRUCTOR(())`.
- The full emote API lives on a SEPARATE local class `D3DEmotePlayer` (main.cpp:496-583).
- main.cpp:298 comment asserts "Binary only registers finalize" — INCORRECT.

### Why this is P0 / architectural
The binary has TWO real classes, both with full APIs but DIFFERENT member sets and DIFFERENT
callback target layers:
  (a) `Motion.EmotePlayer`  via 0x67FAC8  — Player-engine-facing API (71 slots above).
  (b) `D3DEmotePlayer`      via 0x52E4xx (D3DEmotePlayer_ncb_registerMembers, register 0x541D98)
      — 54 D3D-shell members (module, clear, load, clone, show, hide, visible, smoothing,
      meshDivisionRatio, queing, hairScale, partsScale, bustScale, assignState, setCoord, setScale,
      getScale, setRot, getRot, setColor, getColor, count/getVariable*, setVariable, getVariable,
      startWind, stopWind, timeline*, animating, skip, pass, progress, modified, setOuterForce,
      getOuterForce, contains) + consts MaskModeStencil/MaskModeAlpha/TimelinePlayFlag*.
Local collapses these: empty `Motion.EmotePlayer` + a `D3DEmotePlayer` carrying an API that is a
hybrid of both binary sets (e.g. local D3DEmotePlayer has `bodyScale`, `create`, `addPlayCallback`,
`setMirror`, `setTimeline`, `isTimelinePlaying` — but binary D3DEmotePlayer uses `clear`(->create cb),
`pass`(->addPlayCallback cb), and has NO bodyScale/setMirror). Cannot be patched member-by-member;
the class-to-binary-function mapping must be fixed first:
  - `Motion.EmotePlayer` must expose the 0x67FAC8 set.
  - local `D3DEmotePlayer` must match the 0x52E4xx set exactly (names + order + const set).

### Severity: P0 (class identity wrong; both member sets diverge from their binary owners).

---

## 3. Lifecycle / loader function ledger

| id | binary func @ addr | local counterpart | severity | one-line |
|----|--------------------|-------------------|----------|----------|
| C1 | EmotePlayerNativeInstance_create @0x68629C | EmotePlayer.h:36-53 (24B {vtable,_payload,_owned}) | ✅ | 24B shell, +8=0,+16=0 init; layout & semantics match |
| C2 | EmotePlayerNativeInstance_destroy @0x6862D0 | (ncbind native dtor / EmotePlayer dtor) | ⚠️ | gate `+8 && !+16` then EmoteEngine_dtor+delete; local default ~EmotePlayer relies on unique semantics — verify sticky(+16) gate is reproduced in ncbind wrapper |
| C3 | EmotePlayer_NCB_classInit @0x686148 | part of NCB_REGISTER_SUBCLASS_DELAY(EmotePlayer) | ⚠️ | binary registers `finalize`->noop(0x6862C8); local registers ctor only, no explicit finalize member |
| C4 | EmotePlayer_finalize_noop @0x6862C8 | (none) | ⚠️ | binary `finalize` = `return 0;` noop; not represented locally |
| C5 | EmotePlayer_loadClass @0x685BC0 | implicit (NCB macro expansion) | 🔧 | drives BOTH classInit + registerMembers; local split loses this dual-registration into one class |
| C6 | emoteplayer_entry @0x682528 | EmotePlayerPreRegist main.cpp:464-468 | ⚠️ | local only LoadModule("motionplayer.dll"); binary also registers setEmotePSBDecryptSeed/Func into Motion.ResourceManager from here (local does it via ResourceManager subclass instead — acceptable if attrs match, see C8) |
| C7 | EmotePlayer_setEmotePSBDecryptSeed_callback @0x685D30 | ResourceManager::setEmotePSBDecryptSeed (main.cpp:316) | ⚠️ | binary: argc<1 -> ret 0xFFFFFC14; switch on variant type tag (*a3+16): case1/2 toInt path, case3/4 raw int, case5 double->int; new(8) box + dispatch sub_6A87D0. Verify local replicates type-switch + error code, not a plain int read |
| C8 | setEmotePSBDecryptFunc (loc_685E60) | ResourceManager::setEmotePSBDecryptFunc (main.cpp:319) | ❓ | not decompiled this session (loc_ inside 0x682528); both registered with attr 66048(0x10200) + TJS_STATICMEMBER — confirm attr bits match |
| C9 | emoteplayer_static_init @0x42EB00 | NCB module auto-register (emoteplayer.dll) | ✅ | registers "emoteplayer.dll" module entry=emoteplayer_entry; zero-init globals; framework-equivalent in ncbind |

---

## 4. Member-set diff highlights (binary Motion.EmotePlayer 0x67FAC8 vs local Motion.EmotePlayer)

| member | binary Motion.EmotePlayer | local Motion.EmotePlayer | status |
|--------|---------------------------|--------------------------|--------|
| finalize | present (classInit) | absent (only ctor) | ABSENT |
| progress/play/draw/contains/setCoord/setScale/setColor/setRotate/setOuterForce | present | absent | ABSENT (all 69) |
| setHairScale/setPartsScale/setBustScale | present (methods, cb sub_681F20/28/30) | absent | ABSENT |
| hairScale/bustScale/partsScale | present (RO-ish props) | absent | ABSENT |
| all timeline/* + getVariable* + serialize/unserialize | present | absent | ABSENT |
| (constructor) | NOT a named member (native ctor=0x68629C bound via classInit v2[21]) | NCB_CONSTRUCTOR(()) | EXTRA(framework) |

Every script-facing member of binary Motion.EmotePlayer is ABSENT locally. This is the P0.

NOTE: binary uses sub_681F20/28/30 (setHairScale/Parts/Bust) as **Motion.EmotePlayer** members
(callbacks live in 0x67FAC8). This CONTRADICTS analysis/EmotePlayer_Internal_Implementation.md
§2.4 note which said sub_681F20/28/30 are "EmotePlayer-only" — true, but it concluded they belong
to the local `EmotePlayer` class which is currently empty. They belong on the API-bearing
Motion.EmotePlayer.

---

## 5. Subfunction alignment status

- EmotePlayer_NCB_classInit (0x686148): ✅ decompiled — new(0xB0), native ctor slot v2[21]=0x68629C,
  vtable off_19FD6C8, registers finalize. Singleton guard byte_1AB8060 ("Already registerd class.").
- EmotePlayer_ncb_registerMembers (0x67FAC8): ✅ decompiled & enumerated (71 slots).
- D3DEmotePlayer_ncb_registerMembers (0x52E4xx) / _ncb_register (0x541D98): ✅ decompiled — 54 members.
- EmoteEngine_dtor (0x67F4B8): ❓ not re-decompiled (referenced by destroy gate; trusted from prior).
- setEmotePSBDecryptFunc (loc_685E60): ❓ not decompiled (loc_ inside emoteplayer_entry).
- sub_6A87D0 / sub_A0E48C / sub_A13294 (decrypt-seed dispatch helpers): ❓ not decompiled.

---

## 6. Platform boundary notes

None encountered in cluster C local code carrying `// PLATFORM_BOUNDARY:`. The `NCB_CONSTRUCTOR(())`
on EmotePlayer could be a legitimate ncbind requirement (native instance allocation) but is NOT
labelled as a platform boundary and is paired with a wrong claim — treat as part of P0 until the
class identity is fixed and, if the ctor must stay for ncbind, add an explicit PLATFORM_BOUNDARY note.

---

## 7. Fix guidance (do NOT apply here — code edits out of scope)

1. (P0) Re-map classes: make local `Motion.EmotePlayer` expose the 0x67FAC8 member set in order;
   make local `D3DEmotePlayer` match the 0x52E4xx set (drop bodyScale/setMirror/setTimeline/create/
   addPlayCallback unless they map to binary clear/pass cbs; add module-const set MaskMode*/TimelineFlag*).
2. (P0) Move setHairScale/setPartsScale/setBustScale + hairScale/bustScale/partsScale onto the
   API-bearing Motion.EmotePlayer (callbacks sub_681F20/28/30), per 0x67FAC8.
3. (P1) Add explicit `finalize` member to Motion.EmotePlayer mapping to a noop (0x6862C8).
4. (P1) Verify destroy gate: native dtor must skip delete when sticky(+16) set; ensure ncbind
   `_sticky` path is wired, else clone()/assignState ownership transfer will double-free.
5. (P1) Audit setEmotePSBDecryptSeed local impl vs 0x685D30 type-switch (5 cases) + error 0xFFFFFC14.
6. Correct analysis/MotionPlayer_Restoration_Review_2026-05-30.md P2-2 and
   analysis/EmotePlayer_Internal_Implementation.md §2.4 ("only finalize" / class ownership of
   sub_681F20/28/30).

## 8. IDB changes made
- renamed sub_67FAC8 -> EmotePlayer_ncb_registerMembers (confirmed)
- renamed sub_6862C8 -> EmotePlayer_finalize_noop
- renamed 0x42EB00 -> emoteplayer_static_init
- comments added at 0x685BC0 (dual-registration), 0x67FAC8 (member helpers + gate). idb_save done.
