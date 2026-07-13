# CLUSTER D — D3DEmotePlayer NCB facade audit (2026-05-30)

Authoritative source: libkrkr2.so `D3DEmotePlayer_ncb_registerMembers @ 0x52e504`.
Local: `cpp/plugins/motionplayer/main.cpp` lines 496-583 (NCB_REGISTER_CLASS(D3DEmotePlayer)),
`EmotePlayer.h`/`EmotePlayer.cpp` (D3DEmotePlayer methods).

NOTE: `D3DAdaptor.cpp/.h` and `SeparateLayerAdaptor.cpp` are a DIFFERENT binary class
(Motion.D3DAdaptor, a draw-device adaptor; ctor 0x6AD518). They are NOT the D3DEmotePlayer
facade and are out of scope for the member-set diff.

## 1. Authoritative binary member set (0x52e504), in registration order

Helper semantics (verified):
- `ncb_addConstant(a1, NAME, val, 0x10000)` -> const
- `sub_52FC90 / sub_530328 / sub_53043C` -> register a Function member NAME with given callback
- inline `operator new`+ncb_addMember -> Property or Function member
- KEY FINDING: member NAME and callback FUNCTION are independent. The binary deliberately
  registers several members under names that do NOT match the callback semantics.

Constants (4):
1. MaskModeStencil = 0
2. MaskModeAlpha = 1
3. TimelinePlayFlagParallel = 1
4. **TimelinePlayFlagDifference = 2**   (NOT "TimelinePlayFlagSequential")

Members (50 ncb_addMember calls):
| # | binary member NAME | binary callback(s) | kind |
|---|---|---|---|
| 1 | module | D3DEmotePlayer_getModule | RO prop |
| 2 | **clear** | D3DEmotePlayer_create | method |
| 3 | load | D3DEmotePlayer_load | method |
| 4 | clone | sub_52FFBC | method |
| 5 | show | D3DEmotePlayer_show | method |
| 6 | hide | D3DEmotePlayer_hide | method |
| 7 | visible | set/getVisible | prop |
| 8 | smoothing | set/getSmoothing | prop |
| 9 | meshDivisionRatio | set/getMeshDivisionRatio | prop |
| 10 | **queing** | setBustScale / getBustScale | prop |
| 11 | hairScale | set/getHairScale | prop |
| 12 | partsScale | sub_530120 / getPartsScale | prop |
| 13 | **bustScale** | setBodyScale / getBodyScale | prop |
| 14 | assignState | sub_530150 | method |
| 15 | setCoord | D3DEmotePlayer_setCoord | method |
| 16 | setScale | D3DEmotePlayer_setScale | method |
| 17 | getScale | getScale_stub | method |
| 18 | setRot | D3DEmotePlayer_setRot | method |
| 19 | getRot | getRot_stub | method |
| 20 | setColor | D3DEmotePlayer_setColor | method |
| 21 | getColor | getColor_stub | method |
| 22 | countVariables | sub_53041C | method |
| 23 | getVariableLabelAt | sub_530530 | method |
| 24 | countVariableFrameAt | sub_530568 | method |
| 25 | getVariableFrameLabelAt | sub_530588 | method |
| 26 | getVariableFrameValueAt | sub_5305A8 | method |
| 27 | setVariable | D3DEmotePlayer_setVariable | method |
| 28 | getVariable | D3DEmotePlayer_getVariable | method |
| 29 | startWind | D3DEmotePlayer_startWind | method |
| 30 | stopWind | D3DEmotePlayer_stopWind | method |
| 31 | countMainTimelines | D3DEmotePlayer_countMainTimelines | method |
| 32 | getMainTimelineLabelAt | sub_5306C8 | method |
| 33 | countDiffTimelines | D3DEmotePlayer_countDiffTimelines | method |
| 34 | getDiffTimelineLabelAt | sub_5306F0 | method |
| 35 | countPlayingTimelines | D3DEmotePlayer_countPlayingTimelines | method |
| 36 | getPlayingTimelineLabelAt | sub_530718 | method |
| 37 | getPlayingTimelineFlagsAt | sub_530724 | method |
| 38 | isLoopTimeline | sub_530730 | method |
| 39 | getTimelineTotalFrameCount | sub_5307D4 | method |
| 40 | playTimeline | D3DEmotePlayer_playTimeline | method |
| 41 | isTimelinePlaying | D3DEmotePlayer_isTimelinePlaying | method |
| 42 | stopTimeline | D3DEmotePlayer_stopTimeline | method |
| 43 | **setTimelineBlendRatio** | D3DEmotePlayer_setTimeline | method |
| 44 | getTimelineBlendRatio | D3DEmotePlayer_getTimelineBlendRatio | method |
| 45 | fadeInTimeline | D3DEmotePlayer_fadeInTimeline | method |
| 46 | fadeOutTimeline | D3DEmotePlayer_fadeOutTimeline | method |
| 47 | animating | D3DEmotePlayer_getAnimating | RO prop |
| 48 | skip | D3DEmotePlayer_skip | method |
| 49 | **pass** | D3DEmotePlayer_addPlayCallback | method |
| 50 | progress | D3DEmotePlayer_progress | method |
| 51 | **modified** | D3DEmotePlayer_getPlayCallback | RO prop |
| 52 | setOuterForce | D3DEmotePlayer_setOuterForce | method |
| 53 | getOuterForce | sub_530B28 | method |
| 54 | contains | D3DEmotePlayer_contains | method |

Total = 4 const + 50 members = **54 registered NCB entries** (matches Cluster C's "54").

## 2. Member-set diff vs local main.cpp (lines 496-583)

### 2a. Members local registers that the binary does NOT (EXTRA — must remove for 1:1):
completionType, chara, motion, motionKey, maskMode, outline, priorDraw, frameLastTime,
frameLoopTime, loopTime, processedMeshVerticesNum, queing(as bool queuing prop — see note),
hairScale(dup ok), partsScale, bodyScale, useD3D, progress(as prop), modified(as bool prop),
drawvisible, drawOpacity, opengl, playCallback(RO), create, getVariableLabelAt(ok),
setMirror, setTimeline, setTimelineBlendRatio(as real method), play, draw,
setDrawAffineTranslateMatrix, addPlayCallback, getOuterForce(ok name).

The binary D3DEmotePlayer member set is MUCH smaller and uses the deliberate name aliasing
(clear/queing/bustScale/setTimelineBlendRatio/pass/modified). Local main.cpp instead
registers the FULL "EmotePlayer-subclass" property surface (completionType, chara, motion,
maskMode, outline, priorDraw, frame*, loop*, useD3D, opengl, drawOpacity, etc.) which the
binary D3DEmotePlayer class does NOT expose at 0x52e504.

### 2b. Members the binary registers that local is MISSING or MIS-NAMED:
| binary NAME | local status |
|---|---|
| clear (->create cb) | MISSING (local exposes `create`, not `clear`) |
| queing (->BustScale cb) | MIS-MAPPED (local `queing`->getQueuing bool; binary `queing`->bustScale float) |
| bustScale (->BodyScale cb) | MIS-MAPPED (local `bustScale`->bustScale; binary->bodyScale) |
| setTimelineBlendRatio (->setTimeline cb) | MIS-MAPPED (local `setTimelineBlendRatio`->real blend method; binary->setTimeline) |
| pass (->addPlayCallback cb) | MIS-MAPPED (local `pass`->pass(dt); binary `pass`->addPlayCallback) |
| modified (->getPlayCallback prop) | MIS-MAPPED (local `modified`->bool modified prop; binary->getPlayCallback RO) |
| TimelinePlayFlagDifference=2 | MISSING (local registers TimelinePlayFlagSequential instead) |

## 3. Logic-bearing callback architecture audit

### setCoord @ 0x5301ec  (local setCoordCompat + setCoord)
Binary pseudocode:
```
v7 = *(player+8)            ; Animator host
v13 = {float x, float y}
ease = *(u8)(v7+1161)
*(u8)(v7+1162) = 1          ; modified flag
Animator_setKeyframes(*(v7+1072), &v13[x,y], ease, transition, easeArg)
```
Local: setCoordCompat reads param[0..3] as Real, calls self->setCoord(x,y,transition,ease).
The binary passes a *2-float packed keyframe* directly to Animator_setKeyframes on the
Player's animator at +1072, sets modified byte +1162. Local delegates through a C++
`setCoord` -> player API. ARCH DEVIATION (P1): binary uses a single Animator_setKeyframes
call with packed float[2]; the ease/transition arg ordering (ease = preexisting +1161 byte,
not a param) differs from local's param-derived ease. **Verify local setCoord body** maps to
Animator at +1072 with same modified-flag write.

### setScale @ 0x530260  (local setScale lines 212-218 — ALIGNED on core math)
Binary: `v13 = (*(float)(a1+40)) * userScale_param; *(a1+44)=userScale; *(u8)(host+1162)=1;
Animator_setKeyframes(*(host+1080), &v13, ease@+1161, transition, easeArg)`.
Local setScale: `_userScale=s; finalScale=_baseScale*_userScale;
player().setEmoteScale(finalScale,transition,ease); modified=true`. Core math (baseScale*userScale,
store userScale@+44) MATCHES. P1: binary writes finalScale into a keyframe and calls
Animator_setKeyframes(+1080) directly; local funnels through setEmoteScale wrapper.

### getVariable @ 0x5305d4  (local getVariable line 312)
Binary: AddRef param ttstr, Player_getVariable_wrapper(player, &ttstr), Release. Returns double.
Local D3DEmotePlayer::getVariable(ttstr) -> player().getVariable. Structurally aligned;
the binary's manual AddRef/Release on the variant arg is the NCB dispatch boundary handled
by ncb adaptor in local. OK.

### getTimelineBlendRatio @ 0x5308b4
Binary: AddRef ttstr; compute a HASH of the label (FNV-like: 1025*x ^ (>>6), then 9*..,
then 32769*(v^v>>11), 0->-1); hashmap lookup sub_533F84 at player+936 bucket (hash %
*(player+944)); if node->[16] set, ratio = *(float)(node+88) else 0.0; Release. Returns double.
Local getTimelineBlendRatio -> player().getTimelineBlendRatio(label). The binary performs an
inline hash + open-addressing hashmap probe on the timeline table (player+936/+944) with the
label-hash caching into ttstr+68 (the *v7 cache slot). P1: confirm Player::getTimelineBlendRatio
replicates the +936 hashmap structure and the +88 float read gated by node+16.

### setOuterForce @ 0x530a8c  (local setOuterForceCompat + setOuterForce)
Binary: AddRef ttstr arg; Player_setOuterForce(player, &ttstr); Release. SINGLE arg (the
packed variant). Local setOuterForceCompat requires numparams>=3 (label,x,y) and calls
Player::setOuterForce(label,x,y,transition,ease). DEVIATION (P1): binary setOuterForce takes
ONE variant arg and forwards it whole to Player_setOuterForce; local unpacks label+x+y+
transition+ease. The binary's Player_setOuterForce does the unpacking internally. Local has
restructured the arg boundary.

### contains @ 0x530b5c  (local containsCompat + contains)
Binary: AddRef label-variant; node = sub_6B5AD8(*(player+1064), &variant, 1) [resolve layer by
label]; if node: result = Player_hitTest(node+1664, x, y); else 0. Release. Returns bool.
Local contains(label,x,y): `if(!_visible||label.IsEmpty()) return false;
return player().hitTestLayer(label,x,y)`. Local contains(x,y): AABB approximation.
DEVIATIONS:
- P1: binary has NO `_visible` guard and NO IsEmpty guard — it always resolves via
  sub_6B5AD8 then Player_hitTest(node+1664). Local adds `!_visible` / IsEmpty early returns
  that the binary does not have.
- P0: binary contains takes (label-variant, x, y) and ALWAYS goes through the
  sub_6B5AD8 layer-resolve + Player_hitTest path. Local containsCompat has a SECOND overload
  `contains(x,y)` doing a hand-rolled AABB (engine._coordX..+scaledWidth). The binary has NO
  such AABB branch — there is exactly one code path (label resolve + node+1664 hitTest).
  Local's AABB overload is invented architecture not present in binary.

### load @ 0x52fdd4 / create @ 0x52fd84
Binary create (registered as "clear"): destroys EmoteObject at +32 and +24 (EmoteObject_destroy
+ operator delete each), zeroes +24/+32. So binary "clear"/create = tear down the object chain.
Binary load: same teardown, then converts every a2/a3 TJS param to ttstr and builds a
`vector<ttstr>` (string-handle AddRef, sub_533AB4 push), `operator new(0x28)`
EmoteObject_init(obj,&vec), store @+24, release vec. Corrected 2026-07-13 from the old
tTJSVariant-vector guess by following the producer into EmoteObject's loadResource consumer.
P0/ARCH: binary lazily (re)creates the EmoteObject chain in load via operator new(0x28) +
EmoteObject_init; local builds the chain eagerly in the ctor (unique_ptr<EmoteObject>) and load
does not new() it. Confirmed lazy-vs-eager deviation already noted in EmotePlayer.h comment.

### getModule @ 0x52fb98
Binary: walks a red-black/ordered map at obj+224 keyed by dword_1AB26A8 (a global id); if found
returns the stored dispatch at node+40; else operator new(0x20) a fresh tTJSVariant (vtable
off_19FE000, type 256/void-ish) and inserts via sub_533644. Local getModule returns
tTJSVariant from obj()._module. P1: binary uses a global-id-keyed map insert-or-default, not a
single stored _module field.

## 4. Findings (severity)

- D-01 | ncb_registerMembers@0x52e504 | main.cpp 496-583 | P0 | Local member set != binary:
  local registers the full EmotePlayer-style property surface (~28 props + methods); binary
  exposes 54 entries with deliberate name aliasing. Member set diverges structurally.
- D-02 | const TimelinePlayFlagDifference=2 | main.cpp 478 | P0 | Local registers
  `TimelinePlayFlagSequential`; binary string is `TimelinePlayFlagDifference` (value 2).
- D-03 | name 'clear'->create | main.cpp 531 | P0 | Binary registers member NAME "clear"
  bound to create-callback; local registers "create". Missing "clear", extra "create".
- D-04 | name 'queing'->BustScale | n/a | P1 | Binary 'queing' bound to set/getBustScale
  (float); local 'queing'->getQueuing(bool). Callback semantics differ.
- D-05 | name 'bustScale'->BodyScale | main.cpp 519 | P1 | Binary 'bustScale' bound to
  set/getBodyScale; local 'bustScale'->bustScale. Mis-mapped.
- D-06 | name 'setTimelineBlendRatio'->setTimeline | main.cpp 569 | P1 | Binary
  'setTimelineBlendRatio' bound to setTimeline cb; local binds a real setTimelineBlendRatio.
- D-07 | name 'pass'->addPlayCallback | main.cpp 575 | P1 | Binary 'pass' bound to
  addPlayCallback cb; local 'pass'->pass(dt).
- D-08 | name 'modified'->getPlayCallback | main.cpp 523 | P1 | Binary 'modified' is RO prop
  bound to getPlayCallback getter; local 'modified'->bool modified prop.
- D-09 | contains@0x530b5c | EmotePlayer.cpp 538-564 | P0 | Local invents an AABB overload
  (contains(x,y)) + `_visible`/IsEmpty guards not present in binary. Binary has a single path:
  sub_6B5AD8 layer resolve -> Player_hitTest(node+1664).
- D-10 | setOuterForce@0x530a8c | EmotePlayer.cpp 511-531 | P1 | Binary forwards ONE variant
  whole to Player_setOuterForce; local unpacks label+x+y+transition+ease at the NCB boundary.
- D-11 | setCoord@0x5301ec / setScale@0x530260 | EmotePlayer.cpp 190-238 | P1 | Binary calls
  Animator_setKeyframes(host+1072/+1080) directly with packed float[2], ease=preexisting
  +1161 byte; local routes through setEmotePos/setEmoteScale wrappers with param-derived ease.
- D-12 | load@0x52fdd4 | EmotePlayer.cpp load | P1 | Binary lazily new()s the EmoteObject chain
  in load (operator new 0x28 + EmoteObject_init); local builds chain eagerly in ctor.
- D-13 | getModule@0x52fb98 | EmotePlayer.cpp getModule | P1 | Binary uses global-id-keyed
  ordered-map insert-or-default; local returns a single stored _module field.

## 5. MISSING (binary member absent in local with correct name+binding)
- "clear" member (binary->create cb)
- TimelinePlayFlagDifference constant
- getOuterForce real body (binary sub_530B28; local is STUB_WARN)
- binary's 'queing'/'bustScale'/'setTimelineBlendRatio'/'pass'/'modified' aliases

## 6. IDB improvements (saved)
- renamed 0x52fb98->D3DEmotePlayer_getModule, 0x52fd84->D3DEmotePlayer_create,
  0x52fdd4->D3DEmotePlayer_load
- comments at 6 mismatch sites documenting NAME/callback aliasing
- idb_save OK

## 7. Verdict
NEEDS ARCHITECTURAL REWORK (Cluster C claim CONFIRMED).
Local `D3DEmotePlayer` is a HYBRID: it merges the binary's `D3DEmotePlayer` 54-member facade
with the broader `EmotePlayer`-subclass property surface, and does not reproduce the binary's
deliberate member-name aliasing (clear/queing/bustScale/setTimelineBlendRatio/pass/modified)
nor the `TimelinePlayFlagDifference` constant. The member set must be rebuilt to match the
exact 54-entry table in section 1 before per-callback logic alignment is meaningful.
