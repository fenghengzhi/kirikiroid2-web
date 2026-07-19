---
name: clampcontrol-67C8A8
description: sub_67C8A8 clampControl binder — function/struct/builder map, 40B deque7 layout, and the live-path double-clamp architecture conflict
metadata:
  type: project
---

# clampControl binder sub_67C8A8 @0x67C8A8 (EmoteEngine deque#7)

## Function map (libkrkr2.so -> local)
- `sub_67C8A8` @0x67C8A8 = per-entry clampControl binder. Reads engine HM(+1440=HM7),
  writes player HM1/HM2 via Player_bindParameterValue(engine+1064). Local live body is
  `EmoteEngine::applyClampControlsLike_0x67C8A8` (EmoteEngine.cpp).
- Builder/populator = `EmoteEngine_buildClampControl` @0x66EE5C ("clampControl" key,
  dispatched from EmoteEngine_applyMetadata_buildControllers @0x67D4D0). Local populator
  = `EmoteEngine::buildClampControlLike_0x66EE5C` -> `_clampControlDeque7`.
- Callees: sub_67C560 (var-track cascade) = EmoteEngine::accumulateTimelineContributionLike_0x67C560;
  sub_67C6B0 (mirror flag) = EmoteEngine::shouldMirrorEvalLabelLike_0x67C6B0;
  Player_bindParameterValue @0x6C4668 = Player::bindParameterValueLike/writeEvalResultValueLike_0x6C4668.
- `sub_67CC9C` @0x67CC9C = bind-loop + sub_67C8A8 bundled; has NO callers in binary
  (dead). The former local Player model has been deleted rather than retained as
  live object state.

## deque#7 (clampControl) 40B element layout (evidence: builder 0x66EE5C writes v9-40..v9)
- +0  int32  type  (Motion_propGetInt "type")  -> disk-remap mode (0 squircle / 1 clamp-circle)
- +4  pad (zeroed)
- +8  double min (Motion_propGetDouble "min")
- +16 double max (Motion_propGetDouble "max")
- +24 ttstr  var_lr (X-axis HM key)
- +32 ttstr  var_ud (Y-axis HM key)
Builder gate: only "enabled"==true entries pushed. Deque header base engine+496;
finish._M_cur engine+528; block=480B=12 elems. Local struct: EmoteClampControlEntry_Deque7
(EmoteEngine.h), member EmoteEngine::_clampControlDeque7 (live builder and live binder).

## Binder math (verified 1:1)
range=max-min; norm=2*(val-min)/range-1 (both axes, min as subtrahend). if(x!=0&&y!=0):
mode1 -> if sqrt(x^2+y^2)>1: x=cos(atan2(y,x)), y=sin(...). mode0 squircle:
ratio=|x/y| clamp to <=1; invLen=1/sqrt(ratio^2+1); proj=norm*invLen; projLen=sqrt;
scale=(1-cos(ratio*PI/2))*(sin(projLen*PI/2)/projLen -1)+1; norm=proj*scale.
final=min+range*(norm+1)*0.5. X negated iff sub_67C6B0 mirror set; Y never.

## MIGRATION DONE 2026-06-03 (supersedes the prior "live-path double-clamp conflict")
Binary places bind-loop + sub_67C8A8 ONLY in EmoteEngine_progress (@0x67d3a4 / @0x67d3f8).
Player_progress_inner @0x6C106C and the child-motion pass @0x6BE2A4 run progress_inner with
NO bind-loop and NO clamp (re-confirmed by fresh decompile 2026-06-03). The clamp + a
redundant bind-loop formerly ALSO ran on the Player progress path through a local model of
sub_67CC9C (WRONG location). That call was first removed from frameProgress; the now
unreferenced local wrapper and its eager clamp side table were subsequently deleted. The
clamp runs solely from EmoteEngine::progress as
`applyClampControlsLike_0x67C8A8()`, right after the
HM7 bind-loop (~line 1939) and before step-7 player().progressFramesLike_0x6D2A54. The
bind-loop was ALREADY live in EmoteEngine::progress over _labelToValueHM7, so removal of the
frameProgress _evalResultList bind-loop is pure de-duplication. No double-clamp and no dead
Player-side owner remain.

### 2026-07-18 receiver/input correction
(1) The live body now belongs to EmoteEngine, matching binary ownership; it iterates
    `_clampControlDeque7` built directly from raw PSB metadata. The former Player body over
    `MotionSnapshot::clampControls` is no longer in the live call chain.
(2) It reads `_labelToValueHM7` directly and uses the Engine raw mirror helper before binding
    through the embedded Player.
(3) 2026-07-18 later correction: `sub_67C560` now belongs to EmoteEngine and iterates
    HM3@+936, active-label vector@+1040, and `EmoteTimelineData80B::variableList`'s 56B
    track deque directly. The former delegation to Player decoded timeline tables has been
    removed from the live clamp path.
(4) 2026-07-19 closure: fresh xrefs confirmed `sub_67C8A8` is called by live
    `EmoteEngine_progress@0x67D01C` and caller-less `sub_67CC9C` only. The dead local
    `Player::applyEvalResultPostProcessLike_0x67CC9C`, its Player mirror helper, and
    `MotionSnapshot::clampControls` were therefore deleted; this corrects the earlier
    note that recommended retaining the local dead-function model.

### Earlier math/data-flow corrections retained
(1) sub_67C560 var-track cascade now runs on each axis value before normalize
    (Engine HM3/+1040 implementation on varLr & varUd) — was omitted.
(2) reads ENGINE HM7 = _engineBack->_labelToValueHM7 (engine+1440 = sub_67C8A8 v6=result+180),
    NOT player HM2 (_evalResultValues). Removed port-invented getVariable fallback +
    varLr/varUd empty-key guard + zero-range guard + squircle projLen>0 guard (binary has
    none). type!=0 -> circle (acts only when type==1 && radius>1); type==0 -> squircle.
    Squircle scale uses CLAMPED ratio v37 (not raw). 2*(v-min)/range = self-add in binary,
    bit-identical to *2.0 in port.

### VERIFICATION 2026-06-03
Logo path (Motion.Player.progress -> progressMsLike -> frameProgress) has empty clampControls
+ empty _evalResultList, so both the clamp body and the removed post-process are no-ops for
logo. motion_playback differential: yuzulogo PASS (243 frames) + m2logo PASS (93 frames), both
oracle bit-identical. web/debug + wasmtime_guest build clean. No oracle exercises a populated
clampControl deque, so the gap fixes are reconstruction-only (no runtime witness) but inert for
all current fixtures.

### VERIFICATION 2026-07-18
macOS Release motionplayer-dll 57/57 and Web Debug 54/54 linked. Full motionplayer-dll stayed
at the same four known failure categories (8/12 cases; this random order 173/177 assertions),
with no new failure or crash. Current fixtures still contain no populated clampControl witness.
