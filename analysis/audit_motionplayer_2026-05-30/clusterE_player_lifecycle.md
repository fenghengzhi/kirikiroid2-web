# CLUSTER E — Player (MotionPlayer) lifecycle audit

> Date: 2026-05-30. Authoritative source: libkrkr2.so (IDB libkrkr2.so.i64).
> Scope: ctor/dtor, NCB registration, factory/dispatch chain, scalar accessors,
> initVariables, deque/HM init helpers. Read-only audit; IDB renamed + saved.
> Protocol per function: decompile -> <=10-line pseudocode -> local counterpart
> -> architecture compare -> P0/P1/P2.

## Verdict: PARTIAL DEVIATION (architecture-level on accessors + NCB member set)

Object layout (1384B), ctor default values, dtor reverse-order teardown, and
the factory/dispatch chain are **well aligned**. The two large open gaps are:
(1) the NCB member set diverges hard from the binary, and (2) several "scalar"
accessors are not scalar in the binary — they carry dispatch / TJS-Array / clamp
logic the local plain-field setters drop.

---

## 1. ctor — Player_ctor @ 0x6CED30  [renamed in IDB]

Pseudocode (key inits, all confirmed):
```
this[0]=this; this[1]=0; memset(+184,0,0x50); memset(+72,0,0x48)
Player_nodesDeque_init(+184,0)                       // node deque, stride 2632
HM1 init (+264, prime-bucket via sub_149EDF8(10), load 1.0f@+296)
HM2 init (+320, load 1.0f@+352)
sub_A0F5E0(+636, rmArg); sub_A0F5E0(+656, rmArg)     // 2 ttstr copies of rm arg
sub_7E2344(+864)                                      // inline container
+909=0; +984=0; (+912 dword)=100                      // pixelateDivision=100
sub_A0F5E0(+992, rmArg)                                // 3rd ttstr copy of rm arg
HM3 init (+1184, load 1.0f@+1216); HM4 init (+1240, load 1.0f@+1272)
memset(+1296,0,0x50); Player_controllerDeque_init(+1296,0)  // item stride 160
v17=sub_9C8440(0); sub_A0FCC0(+676,v17)               // RandomGenerator @ +676
v18=sub_9C8440(0); sub_A0FCC0(+716,v18); v17.vtbl[6](512,L"color",..) // color obj @ +716
+610=0;+482=0;+1120=0;+1092=0;+456..463=0
this[146]=1.0(+1168);+483=0; +1093=byte_1AB84A8(defaultSyncActive)
+480 word=1; +608 word=1; +1156 dword=0xFF808080
this[75]=1.0(+600 damping); +908=0
this[19]=0x7FEF..(+152 DBL_MAX); this[22]=0xFFEF..(+168 -DBL_MAX)
this[101]=1.0(+808); this[104]=1.0(+832); this[147]=1.0(+1176)
this[145]=1.5(+1160 priorDraw); +1148=0(maskMode)
push initial root node into +184 deque; copy dword_1AA40D8..E4 into root
+760=0; +904=0; release v18,v17 local refs
```

### ctor compare (default values, init order)
| Field | Binary value | Local | Status |
|---|---|---|---|
| +912 pixelateDivision | 100 | NOT a Player field (static on D3DEmoteModule, default 1) | DEVIATION (P1, pre-existing) |
| +1160 priorDraw | 1.5 | `_priorDraw=1.5` | OK |
| +1176 outsideFactor | 1.0 | `_outsideFactor=1.0` | OK |
| +1168 meshDivRatio | 1.0 | migrated to EmoteEngine | OK (EmoteEngine cluster) |
| +600 damping | 1.0 | `_cameraDamping=1.0` | OK |
| +152/+168 bounds | DBL_MAX/-DBL_MAX | numeric_limits<double>::max() | OK |
| +1156 parentColor | 0xFF808080 | `_colorWeightPacked=0xFF808080` | OK |
| +1092/+482/+483 | 0 | false | OK |
| +1093 speed | byte_1AB84A8 (defaultSyncActive) | `_speed=true` | OK (literal default; binary copies the class static) |
| RandomGen | sub_A0FCC0(**+676**) | created in ctor body via TVPExecuteExpression | OK content; **comment mislabels +992** -> P2-3 STILL OPEN |
| +716 "color" TJS obj | sub_9C8440 + set L"color" param | NO local equivalent | MISSING (P2) |
| +992 ttstr | 3rd sub_A0F5E0(rmArg) copy | NO local field | MISSING (P2) |

Local ctor uses STL containers (deque/unordered_map/vector) instead of the
6 KiriKiri inline containers (4 HM + 2 deque). Per CLAUDE.md this is a permanent
container-choice DEVIATION (already tracked, PLATFORM_BOUNDARY-style tolerance).
The local ctor does NOT build the +716 color dispatch object nor the +992 ttstr.

## 2. dtor — Player_dtor @ 0x6CFADC  [renamed in IDB]

Reverse-order teardown confirmed (authoritative ordering):
```
1. sub_6CDE18(this)                       pre-cleanup
2. renderList +384 (stride 56, entry+0=tTJSVariant*) Release
3. sub_6C0DE8(+1296)                       controller deque
4. Player_resetAndReleaseNodes(this)
5. delete +760 d3dAdaptor (sub_6CFFB8 + operator delete)
6. sub_6F436C(+184) node deque destroy; sub_6CF678(+1296)
7. HM4(+1240) chain delete + bucket memset + cond delete
8. HM3(+1184) chain Player_HM3_entry_destroy + memset + cond delete
9. ttstr release: +1072,+1052,+1032,+1012,+992
10. variant release: +984,+976,+968,+960  (this+123/122/121/120)
11. variableList +936 (stride 44, 2 ttstr/item: +24,+4) release + delete
12. sub_7E24AC(+864)
13. variant release: +776,+768  (this+97/96)
14. ttstr release: +736,+716,+696,+676,+656,+636,+616,+548,+528,+508,+484
15. sub_6DD144(+408)
16. renderList +384 2nd pass Release + delete
17. HM2(+320) chain delete + memset + cond delete
18. HM1(+264) chain Player_HM1_value_destroy + memset + cond delete
19. sub_6CF9B4(+184); sub_6DD228(+24)
```
Local dtor (~Player) only `delete _renderSeparateLayerAdaptor`. Everything else
is RAII member-destruction in reverse declaration order. Release ORDER is NOT
guaranteed to match the binary chain. Status: DEVIATION (P3, architectural;
functional equivalence via tTJSVariant value semantics). The +760 SLA delete is
correctly mirrored.

## 3. NCB registration / factory / dispatch chain

| Binary fn | Addr | Role | Local counterpart | Status |
|---|---|---|---|---|
| Player_ncb_ctorDispatch | 0x6F6BD0 | ctor dispatch (param-count gate, a6==1 fast-path) | ncbind NCB_CONSTRUCTOR((ResourceManager)) | OK (framework) |
| Player_ncb_createInstance | 0x6F6CA8 | calls factory, PropSet(2,classid) into objthis+8 | ncbind adaptor | OK (framework) |
| Player_factory | 0x6F6DC0 | operator new(0x568); Player_ctor; sub_A0F778 | ncbind Factory | OK |
| Player_ncb_classInit | 0x6FDE74 | new(0xB0) class obj, vtbl off_19FD6C8, register only `finalize` | ncbind class init | OK (framework) |
| Player_ncb_registerMembers | 0x6D69C8 | registers 92 members | main.cpp NCB_REGISTER_CLASS(Player) | **DEVIATION (see 3.1)** |
| Motion_Player_ncb_register | 0x6FDD04 | top-level register/unregister gate | NCB_REGISTER_CLASS macro | OK |

### 3.1 NCB member-set diff — Player_ncb_registerMembers @ 0x6D69C8
Binary registers **92 distinct member names** (extracted from L"..." literals,
in registration order). Local registers 138. This is a hard divergence.

**24 binary members MISSING locally:**
`defaultSyncActive, defaultTransformOrder, lastTime, meshDivisionRatio,
transformOrder, coordinate, bounds, angleDeg, angleRad, setCoord, flipX, flipY,
opacity, visible, slantX, slantY, zoomX, zoomY, pixelateDivision, clear,
contains, onAction, onSync, onGroundCorrection`

Notes on the missing set:
- Root-node transform getters/setters exposed AS PROPERTIES in the binary
  (`flipX/flipY/opacity/visible/slantX/slantY/zoomX/zoomY`, `angleDeg/angleRad`,
  `coordinate`, `transformOrder`) — local exposes only the `setFlip/setOpacity/
  setVisible/setSlant/setZoom` METHOD forms, so script-facing property reads
  (`player.flipX`, `player.opacity`, `player.angleDeg`) are absent. P1.
- `setCoord` is a real method (0x6CCFF8) absent locally (local has separate x/y). P1.
- `bounds`, `contains`, `calcViewParam` group: `bounds`/`contains` missing. P1.
- `pixelateDivision` property missing (ties to +912=100 ctor field gap). P1.
- `clear` method missing. `onAction/onSync/onGroundCorrection` event callbacks
  missing (these copy a variant via sub_A0F5E0; 0x6D9A58/0x6D9A60). P1.
- `defaultSyncActive`/`defaultTransformOrder` are class-static-level members. P2.

**70 local-only members not on binary Motion.Player:** almost entirely the
timeline/variable query surface (`countVariables, getVariableLabelAt, *Timeline*,
playTimeline, fadeIn/OutTimeline, getPlayingTimelineInfoList, ...`) plus host/
resource methods (`unload, findMotion, requireLayerId, setClearColor, setSize,
captureCanvas, loadSource, ...`) and `metadata/queuing/directEdit/selectorEnabled/
canvasCaptureEnabled/clearEnabled/hitThreshold/busy/random/initPhysics/serialize/
unserialize/setRotate/setMirror/debugPrint/getD3DAvailable/doAlphaMaskOperation/
isPlaying/motionList/emoteEdit`.
- The timeline/variable query surface is a **D3DEmotePlayer** API in the binary
  (cf. D3DEmotePlayer NCB @ 0x67FAC8 / main.cpp:496), NOT Motion.Player. They
  were hoisted onto Player locally. This is a CLASS-BOUNDARY deviation: in the
  binary these live on the emote wrapper, here on Player. P2.
- Host methods (resource/draw-device) are Web-port host adaptations with no
  Motion.Player binary equivalent (binary routes via iTVPDrawDevice / Motion.
  ResourceManager). Tolerated extensions but they pollute the class surface. P2.

## 4. Scalar accessor deep-audit

| Accessor | Addr | Binary behavior | Local | Status |
|---|---|---|---|---|
| setChara | 0x6D94B0 | if +968(chara variant) set: re-dispatch `sub_6B29C0(this,16,arg)` (stealth re-play) then clear/Release +776; else AddRef arg, Release old +776, store arg@+776 | `setChara(ttstr v){_chara=v;}` plain field assign | **DEVIATION (P1/arch)**: misses variant mgmt + re-play dispatch; wrong type (ttstr vs tTJSVariant* at +968/+776) |
| setTickCount_ms | 0x6D96C0 | `+1120 = fmax(v*60/1000, 0)`; `+480 word=257`; `+456 = min(+1120, +1128)` | `_frameTickCount = v*60/1000` only | **DEVIATION (P1)**: missing fmax>=0 clamp, +480=257 progress-flag write, +456 clampedEvalTime update |
| getTickCount_ms | 0x6D96A0 | `return +1120 * 1000/60` (unconditional) | `_frameTickCount>0 ? *1000/60 : 0` | minor DEVIATION (P2): extra >0 guard not in binary |
| setAngleDeg | 0x6CD0EC | rad->deg(*57.2957795); if +482 emoteMode: normalize[0,360), `+464=ang`, `Player_initEmoteMotion(2)`; else root+1616=ang + dirty | no NCB `angleDeg` property locally; setRotate path differs | **MISSING property** (P1) |
| setCoord | 0x6CCFF8 | root+1592=x, root+1600=y, dirty if changed | no `setCoord` NCB method; local setX/setY split (each guards+dirty correctly) | **MISSING method** (P1); underlying x/y writes OK |
| getLoopTime (array) | 0x6D139C | builds **TJS Array** from +1312 deque (a1[164], stride 20, 60/block); per elem `operator new(0x1F4=500)`, `+16=2`, AddRef copy dispatch | `getLoopTime(){return _loopTime;}` returns plain double from `_loopTime` | **SEVERE DEVIATION (P1/arch)**: binary loopTime is an Array-valued property over a deque; local is a scalar double. Different value shape AND different backing (deque@+1312 vs `_loopTime` field) |
| colorWeight swap | 0x6CD710 | `(c & 0xFF00FF00) | byte2 | (byte0<<16)` R<->B swap on +1156 | `swapPackedRbLike_0x6CD710` matches | OK (but NCB `colorWeight` binary getter is sub_6D9768=+1097 bool, naming confusion P1 pre-existing) |
| getX/setX/getY/setY | 0x6D98A8/0x6CD028/.. | root+1592/+1600 read/write, dirty guard | `_nodes[0].delta.posX/posY` + dirty guard + pending-pos fallback | OK |

## 5. initVariables — Player_initVariables @ 0x6CD750  [renamed in IDB]
```
sub_6C0DE8(+1296)                                  // clear controller deque
dispatch = ttstr(+528)->asObject; dispatch.PropGet(L"variable") -> varlist
n = varlist.PropGet(L"label" count via sub_56C694)
for i in 0..n:
  elem = varlist[i]
  push 160B item into +1296 deque (operator new(0x1E0)=3x160 block; memset 0xA0)
  set item: +16(dword)=0, +(-92)=1, +(-36)=1
  item.label  <- elem.PropGet(L"label") ttstr
  item.scope  <- elem.PropGet(L"scope") ; split by "::"; store both halves
  AddRef/Release dispatch refs throughout
```
Local `Player::initVariables` (PlayerMotionLoad.cpp:147):
- Reads `_activeMotion->root["variable"]` PSBList (NOT via TJS dispatch PropGet
  on a +528 ttstr->object). DEVIATION: binary drives off Player+528 ttstr-as-
  dispatch + PropGet(L"variable"/L"label"/L"scope"); local reads PSB structs.
- Pushes into `std::vector<VariableLabelEntry>` (`_variableLabelEntries`), NOT the
  +1296 KiriKiri deque of 160B items. **WRONG CONTAINER + WRONG TARGET**: the
  +1296 deque (Player_controllerDeque_init, stride 160) is the binary's
  initVariables sink; the local +936 variable list (stride 44) is a different
  structure. Local conflates them into one std::vector.
- scope split: local does "::" then ":" ; binary does "::" only (sub_A1359C
  L"::"). Minor logic DEVIATION (P2).
Status: **DEVIATION (P1/arch)** — container + dispatch-source mismatch.

## 6. deque/HM init helpers
| Binary fn | Addr | Finding |
|---|---|---|
| Player_nodesDeque_init | 0x6F4E90 | KiriKiri deque, node stride **2632**, map-block of 8-ptr slots. Local std::deque<MotionNode> (PLATFORM_BOUNDARY container choice). |
| Player_controllerDeque_init | 0x6F4FD8 | KiriKiri deque, item stride **160** (480/3 per block, +480 block span). This is the +1296 deque used by initVariables. Local std::vector<VariableLabelEntry> (stride mismatch + wrong container). |

## 7. MISSING (no local counterpart)
- ctor +716 "color" TJS dispatch object; ctor +992 ttstr (3rd rm-arg copy).
- NCB properties: flipX, flipY, opacity, visible, slantX, slantY, zoomX, zoomY,
  angleDeg, angleRad, coordinate, transformOrder, bounds, pixelateDivision,
  lastTime, meshDivisionRatio, defaultSyncActive, defaultTransformOrder.
- NCB methods: setCoord, clear, contains, onAction, onSync, onGroundCorrection.
- +1296 160B controller deque (initVariables sink) — local has no aligned field.

## 8. Status of carried-over items
- **P1-2** (Player _evalResultValues key type std::string vs binary ttstr HM2
  @+320): STILL OPEN. Player.h:872 still `unordered_map<std::string,double>` with
  the `TODO(A8): retype to detail::LabelValueMap (ttstr key)`. Not addressed.
- **P1-3** (Player HM 6->4 mapping): STILL OPEN. ctor confirms exactly 4 inline
  HMs (+264/+320/+1184/+1240, all prime-bucket via sub_149EDF8, load factor
  1.0f). Local still has 6 unordered_map. Mapping undefined.
- **P2-3** (_tjsRandomGenerator comment mislabels player+992): STILL OPEN.
  ctor re-confirms RandomGen object is `sub_A0FCC0(+676)`; +992 is the 3rd
  sub_A0F5E0(rmArg) ttstr copy. Player.h:954 + ctor comment (PlayerCore.cpp:243)
  both still say "player+992". Comment-only fix; not done.

## 9. IDB changes (saved)
Renamed (idb_save OK): Player_ctor 0x6CED30, Player_dtor 0x6CFADC,
Player_factory 0x6F6DC0, Player_ncb_createInstance 0x6F6CA8,
Player_ncb_ctorDispatch 0x6F6BD0, Player_ncb_registerMembers 0x6D69C8,
Player_initVariables 0x6CD750, Player_setChara 0x6D94B0,
Player_setTickCount_ms 0x6D96C0, Player_getTickCount_ms 0x6D96A0,
Player_setAngleDeg 0x6CD0EC, Player_setCoord 0x6CCFF8,
Player_getLoopTime_array 0x6D139C.
