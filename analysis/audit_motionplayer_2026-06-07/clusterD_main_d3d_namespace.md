# Cluster D — main.cpp NCB registration + D3DEmotePlayer + Motion namespace + D3D adaptor

Audit date: 2026-06-07. Authoritative = libkrkr2.so. Scope: cpp/plugins/motionplayer/
{main.cpp, D3DAdaptor.cpp/.h, D3DEmoteModule.h, MotionNodeBridge.cpp}.

This re-audits the 2026-05-30 Cluster D/K findings against the CURRENT main.cpp, which
has been substantially reworked since (D3DEmotePlayer 54-table rebuilt, M6 namespace fix
landed, namespace free-fns relocated). Most 2026-05-30 P0s are now RESOLVED.

Decompiled this session (evidence):
- D3DEmotePlayer_ncb_registerMembers @0x52E504 (the 54-entry member table)
- D3DEmotePlayer_ncb_register @0x541D98 (class registrar; default no-arg ctor)
- motionplayer_ncb_register @0x6D9B08 (Motion namespace registrar)
- D3DAdaptor_ncb_registerMembers @0x6ACE94 (16 members)
- D3DEmoteModule member registrar sub_52DFA8 @0x52DFA8 (8 members)

================================================================
## VERDICT: ✅ largely aligned, ⚠️ 2 minor deviations + 1 known platform stub

The member SETS, NAMES, ORDER, and NAME/callback aliasing for D3DEmotePlayer, D3DAdaptor,
D3DEmoteModule, and the Motion namespace registrar now match the binary 1:1. The 2026-05-30
"NEEDS ARCHITECTURAL REWORK" verdict for the D3DEmotePlayer facade is SUPERSEDED — the table
has been rebuilt to the exact binary shape. Remaining items are callback-identity / class-
placement nits, plus expected D3D platform stubs.

================================================================
## 1. D3DEmotePlayer member table (binary sub_52E504 vs main.cpp:870-1022)

Binary registration order (ncb_addMember / sub_52FC90 / sub_530328 / sub_53043C sequence):

| # | binary NAME | binary callback | kind | local main.cpp | status |
|---|---|---|---|---|---|
| C0 | <classname> | native create | ctor | Factory(D3DEmotePlayer::factory) | ✅ D3DImage type; see §1a |
| K0 | MaskModeStencil=0 | const | const | Variant :883 | ✅ |
| K1 | MaskModeAlpha=1 | const | const | Variant :884 | ✅ |
| K2 | TimelinePlayFlagParallel=1 | const | const | Variant :885 | ✅ |
| K3 | TimelinePlayFlagDifference=2 | const | const | Variant :887 | ✅ |
| 1 | module | D3DEmotePlayer_getModule | RO prop | NCB_PROPERTY_RO(module,getModule) :891 | ✅ |
| 2 | clear | D3DEmotePlayer_create | method | NCB_METHOD_DETAIL(clear,...create) :940 | ✅ alias kept |
| 3 | load | D3DEmotePlayer_load | method | NCB_METHOD(load) :941 | ✅ |
| 4 | clone | sub_52FFBC | method | NCB_METHOD(clone) :942 | ✅ |
| 5 | show | D3DEmotePlayer_show | method | NCB_METHOD(show) :943 | ✅ |
| 6 | hide | D3DEmotePlayer_hide | method | NCB_METHOD(hide) :944 | ✅ |
| 7 | visible | set/getVisible | prop | NCB_PROPERTY(visible) :899 | ✅ |
| 8 | smoothing | set/getSmoothing | prop | NCB_PROPERTY(smoothing) :900 | ✅ |
| 9 | meshDivisionRatio | set/getMeshDivisionRatio | prop | :901 | ✅ |
| 10 | queing | set/getQueing (byte@+1161 flag) | prop | NCB_PROPERTY(queing,getQueuing,setQueuing) :909 | ✅ |
| 11 | hairScale | set/getHairScale | prop | :910 | ✅ |
| 12 | partsScale | sub_530120 / getPartsScale | prop | :911 | ✅ |
| 13 | bustScale | set/getBustScale (double@+1200) | prop | :920 | ✅ |
| 14 | assignState | sub_530150 | method | NCB_METHOD(assignState) :945 | ✅ |
| 15 | setCoord | D3DEmotePlayer_setCoord | method | setCoordCompat :950 | ✅ |
| 16 | setScale | D3DEmotePlayer_setScale | method | setScaleCompat :951 | ✅ |
| 17 | getScale | getScale_stub | method | NCB_METHOD(getScale) :952 | ✅ |
| 18 | setRot | D3DEmotePlayer_setRot | method | setRotCompat :948 | ✅ (order: local emits setRot before getScale — see §1b) |
| 19 | getRot | getRot_stub | method | NCB_METHOD(getRot) :949 | ✅ |
| 20 | setColor | D3DEmotePlayer_setColor | method | setColorCompat :955 | ✅ |
| 21 | getColor | getColor_stub | method | NCB_METHOD(getColor) :956 | ✅ |
| 22 | countVariables | sub_53041C | method | :957 | ✅ |
| 23 | getVariableLabelAt | sub_530530 | method | :958 | ✅ |
| 24 | countVariableFrameAt | sub_530568 | method | :959 | ✅ |
| 25 | getVariableFrameLabelAt | sub_530588 | method | :960 | ✅ |
| 26 | getVariableFrameValueAt | sub_5305A8 | method | :961 | ✅ |
| 27 | setVariable | D3DEmotePlayer_setVariable | method | setVariableCompat :962 | ✅ |
| 28 | getVariable | D3DEmotePlayer_getVariable | method | :963 | ✅ |
| 29 | startWind | D3DEmotePlayer_startWind | method | startWindCompat :964 | ✅ |
| 30 | stopWind | D3DEmotePlayer_stopWind | method | stopWindCompat :965 | ✅ |
| 31 | countMainTimelines | D3DEmotePlayer_countMainTimelines | method | :966 | ✅ |
| 32 | getMainTimelineLabelAt | sub_5306C8 | method | :967 | ✅ |
| 33 | countDiffTimelines | D3DEmotePlayer_countDiffTimelines | method | :968 | ✅ |
| 34 | getDiffTimelineLabelAt | sub_5306F0 | method | :969 | ✅ |
| 35 | countPlayingTimelines | D3DEmotePlayer_countPlayingTimelines | method | :970 | ✅ |
| 36 | getPlayingTimelineLabelAt | sub_530718 | method | :971 | ✅ |
| 37 | getPlayingTimelineFlagsAt | sub_530724 | method | :972 | ✅ |
| 38 | isLoopTimeline | sub_530730 | method | :973 | ✅ |
| 39 | getTimelineTotalFrameCount | sub_5307D4 | method | :974 | ✅ |
| 40 | playTimeline | D3DEmotePlayer_playTimeline | method | :981 | ✅ |
| 41 | isTimelinePlaying | D3DEmotePlayer_isTimelinePlaying | method | :982 | ✅ |
| 42 | stopTimeline | D3DEmotePlayer_stopTimeline | method | :983 | ✅ |
| 43 | setTimelineBlendRatio | D3DEmotePlayer_setTimeline | method | NCB_METHOD_DETAIL(setTimelineBlendRatio,...setTimeline) :991 | ✅ alias kept |
| 44 | getTimelineBlendRatio | D3DEmotePlayer_getTimelineBlendRatio | method | :993 | ✅ |
| 45 | fadeInTimeline | D3DEmotePlayer_fadeInTimeline | method | :994 | ✅ |
| 46 | fadeOutTimeline | D3DEmotePlayer_fadeOutTimeline | method | :995 | ✅ |
| 47 | animating | D3DEmotePlayer_getAnimating | RO prop | NCB_PROPERTY_RO(animating,getAnimating) :930 | ✅ |
| 48 | skip | D3DEmotePlayer_skip | method | :996 | ✅ |
| 49 | pass | D3DEmotePlayer_addPlayCallback | method | NCB_METHOD_DETAIL(pass,...addPlayCallback) :1009 | ✅ alias kept |
| 50 | progress | **EmoteEngine_progress** | method | NCB_METHOD(progress)->D3DEmotePlayer::progress :1001 | ⚠️ D-A see §1c |
| 51 | modified | D3DEmotePlayer_getPlayCallback | RO prop | NCB_PROPERTY_RO(modified,getPlayCallback) :926 | ✅ alias kept |
| 52 | setOuterForce | D3DEmotePlayer_setOuterForce | method | setOuterForceCompat :1019 | ✅ |
| 53 | getOuterForce | sub_530B28 | method | NCB_METHOD(getOuterForce) :1020 | ✅ |
| 54 | contains | D3DEmotePlayer_contains | method | containsCompat :1021 | ✅ |

All 54 member NAMES + the 6 deliberate name/callback aliases (clear, queing, bustScale,
setTimelineBlendRatio, pass, modified) + 4 constants are present and 1:1. This is a complete
reversal of the 2026-05-30 P0 (D-01/02/03..08) — those are RESOLVED.

### 1a. ✅ D-B corrected: constructor requires D3DImage, not ResourceManager
Fresh tracing on 2026-07-18 reached native create `sub_542764` and unwrap
`sub_5428D8`. The first TJS argument is queried with class ID `dword_1AB2630`;
static initializer `sub_42C7F8` maps the owning registrar vtable `off_1A012E0`
to the binary literal `L"D3DImage"`. The native pointer is stored at shell base+8,
registered through owner vtable+48, and removed by destructor `sub_533C00` through
owner vtable+56. There is no ResourceManager field in the 56-byte shell.

Local now uses `D3DEmotePlayer::factory`, checks the exact `D3DImage` native class,
retains the corresponding TJS owner, and clone carries that owner instead of creating
an empty ResourceManager. Remaining gap: DrawDeviceD3D keeps its native D3DImage type
translation-unit-private, so the Web shell cannot yet call the native +48/+56 child
registration bridge; dispatch retention preserves owner availability and teardown order
but is not claimed as exact native-list ownership.

### 1b. Member ORDER note (NOT a deviation)
The binary order is module, clear, load, clone, show, hide, visible, smoothing,
meshDivisionRatio, queing, hairScale, partsScale, bustScale, assignState, setCoord, setScale,
getScale, setRot, getRot, setColor, getColor, ... Local main.cpp emits setRot/getRot (:948-949)
BEFORE getScale (:952) due to source grouping, but NCB member order is not script-observable
for distinct names (no aliasing collision), so this is cosmetic, not a behavior deviation.

### 1c. ⚠️ D-A (P2): progress callback identity
Binary member #50 'progress' binds **EmoteEngine_progress** (@0x52f76c), NOT a
D3DEmotePlayer_progress. The 'progress' member forwards straight into the EmoteEngine
progress routine. Local main.cpp:1001 binds NCB_METHOD(progress) -> D3DEmotePlayer::progress
(double dt). NAME matches; callback identity differs. To be 1:1, D3DEmotePlayer::progress
must tail-call the EmoteEngine progress (the engine-side tick), not a player-local routine.
Verify the wrapper body in EmotePlayer.cpp. (IDB comment added at 0x52f76c.)

================================================================
## 2. Motion namespace registrar (binary sub_6D9B08 vs main.cpp:607-654 + PostRegistCallback)

### 2a. Constants — 23 total. ✅ all present
Binary: LayerType{Obj0..Camera5}(6), ShapeType{Point0..Quad3}(4), PlayFlag{Force1,Chain2,
AsCan4,Join8,Stealth16}(5), TransformOrder{Flip0,Slant3,Zoom2,Angle1}(4),
CoordinateRecutangular{XY0,XZ1}(2), **MaskModeStencil0, MaskModeAlpha1**(2) = 23.
Local NCB_REGISTER_CLASS(Motion) main.cpp:623-653 registers all 21 LayerType/ShapeType/
PlayFlag/TransformOrder/Coordinate constants 1:1.
- ⚠️ D-C (P3): **MaskModeStencil / MaskModeAlpha are NOT on the local Motion namespace.**
  Binary registers them on the Motion namespace (sub_6D9B08 @0x6d9d24/0x6d9d3c) AS WELL AS on
  D3DEmotePlayer (sub_52E504). Local has them on D3DEmotePlayer (main.cpp:883-884) but the
  Motion namespace block (:623-653) omits them. Binary has them in BOTH places. Add two
  Variant(TJS_W("MaskModeStencil"/"MaskModeAlpha"), ...) to NCB_REGISTER_CLASS(Motion).
  Scalar ints, additive-safe.

### 2b. Subclass registration ORDER. ✅ aligned (Player via PostRegist alias)
Binary sub_6D9B08 order: Point, Circle, Rect, Quad, LayerGetter, **Player**(key @MEMORY
[0x14C1E9C][5]), SourceCache, ObjSource, ResourceManager, SeparateLayerAdaptor, D3DAdaptor.
Each via sub_6FCAAC(*a1, name, descriptor) — in-flow member-add on the Motion dispatch.
Local: NCB_SUBCLASS order in main.cpp:609-620 is ResourceManager, EmotePlayer, SLA, D3DAdaptor,
SourceCache, ObjSource, Point, Circle, Rect, Quad, LayerGetter. Player is aliased into Motion
via PostRegistCallback (:789). NCB_SUBCLASS source order differs from the binary in-flow order,
but ncbind defers subclass attach and the end-state object graph (Motion namespace owns the
same 11 subclasses + Player) is identical; NCB member order on a namespace dispatch is not
script-observable across distinct names. EmotePlayer as an extra Motion subclass is the
emoteplayer.dll module's own (separate module, acceptable). NOT a deviation.

### 2c. Namespace free-fns doAlphaMaskOperation + getD3DAvailable. ✅ owner + timing aligned
Binary sub_6D9B08 registers BOTH on the Motion namespace dispatch IN-FLOW, after the last
subclass (D3DAdaptor), via the same sub_6FCAAC member-add primitive:
  - doAlphaMaskOperation @0x6da1f0 (cb Motion_doAlphaMaskOperation)
  - getD3DAvailable @0x6da260 (cb Motion_getD3DAvailable)
Local main.cpp:751-755 registers both via MotionFreeFnRegistrar::RegistFunction(..., "Motion")
INSIDE PostRegistCallback (after subclasses), order doAlphaMaskOperation then getD3DAvailable.
This is the M6 fix (commit f50f197) — owner (Motion namespace, not Player) and timing (after
subclasses) both now match sub_6D9B08. The 2026-05-30 K-7 wrong-owner finding is RESOLVED.
Residual mechanism diff (RegistFunction re-looks-up Motion dispatch via GetDispatch vs binary's
in-hand *a1) is benign; end-state object graph identical.

### 2d. M6 namespace-attach regression — RESOLVED (memory cross-check)
project_m6_motion_namespace_attach_regression confirms the wasmtime regression (render
pipeline silently 0 events when the two free-fns were registered via standalone
NCB_ATTACH_FUNCTION auto-register units, too early) was root-caused and fixed by relocating
the same member-add into PostRegistCallback. The binary registration topology (sub_6D9B08
in-flow after subclasses) is what the current code now mirrors. No registration-topology
deviation remains; this is the binary-aligned shape, not a workaround.

================================================================
## 3. D3DAdaptor (binary sub_6ACE94 vs D3DAdaptor.h/.cpp + main.cpp:118-135)

### 3a. Member set — 16 members. ✅ 1:1 (set, names, order)
Binary order: constructor, setPos(nullsub_81), setSize(sub_6AD7A8), setClearColor(sub_6AD7B0),
setResizable(sub_6AD7B8), removeAllTextures(sub_6AD8B8), removeAllBg(nullsub_82),
removeAllCaption(nullsub_83), registerBg(nullsub_84), registerCaption(nullsub_85),
unloadUnusedTextures(nullsub_86), visible(prop), alphaOpAdd(prop), captureCanvas,
canvasCaptureEnabled(prop), clearEnabled(prop).
Local main.cpp:118-135: factory, setPos, setSize, setClearColor, setResizable, removeAllTextures,
removeAllBg, removeAllCaption, registerBg, registerCaption, unloadUnusedTextures, captureCanvas,
visible, alphaOpAdd, canvasCaptureEnabled, clearEnabled. Same 16, same order. ✅
(Cluster K's "19 members" was a miscount; the binary ncb table is exactly 16.)

### 3b. PLATFORM_BOUNDARY (D3D stubs) — binary itself stubs these
Binary callbacks setPos=nullsub_81, removeAllBg=nullsub_82, removeAllCaption=nullsub_83,
registerBg=nullsub_84, registerCaption=nullsub_85, unloadUnusedTextures=nullsub_86 are
no-op nullsubs IN THE BINARY. Local D3DAdaptor.h:39-48 stubs (empty `{}` bodies) faithfully
reproduce these. removeAllTextures binds sub_6AD8B8 in binary (real) but local stubs it `{}`
(D3DAdaptor.h:43) — minor: binary removeAllTextures is a real texture-list clear, local is empty.
Flag D-D (P3): removeAllTextures has a real binary body (sub_6AD8B8); local empties it. Likely
acceptable since the web port has no D3D bg/caption texture list, but the comment does NOT
carry an explicit `// PLATFORM_BOUNDARY: ...` justification, so it is listed, not waived.

### 3c. Implementation architecture (impl, not member-set)
D3DAdaptor.cpp uses std::vector<uint8_t> _buffer + iTVPTexture2D* _targetTexture +
TVPGetRenderManager()->CreateTexture2D. The binary D3DAdaptor_init @0x6ADB10 creates an RGBA
render texture at adaptor+48 from ctor width/height. This is the draw-device/texture-target
path; the std::vector readback buffer is a web-port impl choice (captureCanvas readback into a
TJS Layer). The setSize/captureCanvas/ensureTargetTexture flow is structurally aligned to the
adaptor+48 texture model. The internal RGBA capture path has no per-member NCB script surface
beyond the 16 above; impl-level container choice (std::vector readback) is a web stub detail,
not part of the NCB facade. Acceptable for this cluster's scope (NCB facade + namespace).

================================================================
## 4. D3DEmoteModule (binary sub_52DFA8 vs D3DEmoteModule.h + main.cpp:839-868)

### 4a. Member set — 8 members. ✅ 1:1 (set, names, order)
Binary order: constructor(off_1A02790,cb=0 default ctor), maskMode(sub_52E3F8/sub_52E3F0),
maskRegionClipping(sub_52E408/sub_52E400), mipMapEnabled(sub_52E41C/sub_52E414),
alphaOp(sub_52E430/sub_52E428), protectTranslucentTextureColor(sub_52E440/sub_52E438),
pixelateDivision(sub_52E454/sub_52E44C), setMaxTextureSize(sub_52E45C).
Local main.cpp:839-868: ctor, maskMode, maskRegionClipping, mipMapEnabled, alphaOp,
protectTranslucentTextureColor, pixelateDivision, setMaxTextureSize. Same 8, same order. ✅

### 4b. Constants placement
The 4 constants (MaskModeStencil/Alpha + TimelinePlayFlagParallel/Difference) were previously
on D3DEmoteModule; they have been MOVED to D3DEmotePlayer (main.cpp:883-888) per sub_52E504,
which is correct — sub_52DFA8 (D3DEmoteModule registrar) registers NO constants. ✅

### 4c. pixelateDivision — correctly on BOTH classes
Binary registers pixelateDivision on D3DEmoteModule (sub_52DFA8 @0x52e318, module+20) AND on
Motion.Player (Player_ncb_registerMembers, Player+912 default 100). D3DEmoteModule.h:34-48 and
Player both expose it with distinct backing fields. ✅ (2026-05-30 over-deletion restored.)

================================================================
## 5. MotionNodeBridge.cpp

Thin TJS<->native bridge for MotionNode child/particle access. Aligned to sub_6BE0C0
(getChildPlayer: dispatch from node+1912, NIS -> native Player* at +8), sub_56C694
(getParticleCount: Array.count via PropGet L"count"), sub_6C1678 (getParticleChild:
PropGetByNum + NIS + *(+8)), TJS Array add/erase via FuncCall (sub_6C17A4). These use TJS
dispatch (PropGetByNum/FuncCall("add"/"erase")) on the particle Array, NOT std::vector — the
container choice is the binary's TJS Array dispatch, faithfully reproduced. ✅ Out of the
NCB-registration scope but verified architecturally consistent; no deviation.

================================================================
## FINDINGS TABLE (current, 2026-06-07)

| id | func@addr | local | sev | one-line |
|----|-----------|-------|-----|----------|
| D-A | progress cb @0x52f76c | main.cpp:1001 | P2 ⚠️ | binary 'progress' cb=EmoteEngine_progress, not D3DEmotePlayer_progress; verify wrapper tail-calls engine progress |
| D-B | ctor @0x542764/0x5428D8 | D3DEmotePlayer::factory | P2 ⚠️ | D3DImage type and shell owner data flow corrected; native owner child add/remove bridge still missing |
| D-C | MaskMode* @0x6d9d24/0x6d9d3c | main.cpp:623-653 | P3 ⚠️ | binary registers MaskModeStencil/Alpha on Motion namespace too; local Motion block omits them (only on D3DEmotePlayer) |
| D-D | removeAllTextures @0x6AD8B8 | D3DAdaptor.h:43 | P3 ⚠️ | binary has real body (sub_6AD8B8); local empties it w/o explicit PLATFORM_BOUNDARY note |

## RESOLVED since 2026-05-30 (was P0/P1, now ✅)
- D-01/02/03..08 (D3DEmotePlayer member set + 6 name aliases + TimelinePlayFlagDifference):
  table rebuilt to exact 54-entry binary shape with all aliases.
- K-7 (doAlphaMaskOperation/getD3DAvailable wrong owner): relocated to Motion namespace,
  in-flow timing via PostRegistCallback (M6 fix f50f197).
- D3DEmoteModule 8-member set + constants moved to D3DEmotePlayer.
- D3DAdaptor 16-member set 1:1.

## PLATFORM_BOUNDARY (listed, not counted)
- D3DAdaptor setPos/removeAllBg/removeAllCaption/registerBg/registerCaption/
  unloadUnusedTextures: binary nullsubs; local empty stubs are FAITHFUL (binary stubs them).
  These do NOT need a PLATFORM_BOUNDARY note since they mirror the binary's own nullsubs.
- D3DAdaptor.cpp std::vector readback buffer + CreateTexture2D capture path: web draw-device
  impl detail; the NCB facade (16 members) is what this cluster audits. No explicit boundary
  comment present; impl-level, not facade-level.
- motion_getD3DAvailable returns true (main.cpp:706): annotated platform boundary (web port
  has no GLES-GPU-accel vs D3D split to invert). Owner relocation is binary-aligned; the value
  is a pre-existing boundary with a stated reason — legal.

## SUBFUNCTION alignment status
- ✅ sub_6D9B08, sub_52E504, sub_541D98, sub_6ACE94, sub_52DFA8 fully enumerated this session.
- ✅ D3DEmotePlayer native create `sub_542764`, D3DImage unwrap `sub_5428D8`, clone `sub_52FFBC`, destructor `sub_533C00` traced.
- ❓ EmoteEngine_progress vs D3DEmotePlayer::progress body equivalence — not traced (D-A).

## IDB improvements (saved)
- Comment at 0x52f76c documenting progress cb = EmoteEngine_progress + verify directive.
- idb_save OK (C:\Users\fenghengzhi\libkrkr2\libkrkr2\libkrkr2.so.i64).
