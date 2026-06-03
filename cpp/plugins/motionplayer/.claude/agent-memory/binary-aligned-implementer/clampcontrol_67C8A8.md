---
name: clampcontrol-67C8A8
description: sub_67C8A8 clampControl binder — function/struct/builder map, 40B deque7 layout, and the live-path double-clamp architecture conflict
metadata:
  type: project
---

# clampControl binder sub_67C8A8 @0x67C8A8 (EmoteEngine deque#7)

## Function map (libkrkr2.so -> local)
- `sub_67C8A8` @0x67C8A8 = per-entry clampControl binder. Reads engine HM(+1440=HM7),
  writes player HM1/HM2 via Player_bindParameterValue(engine+1064). Local per-entry
  BODY = `Player::applyClampControlsLike_0x67C8A8` (PlayerFrameProgress.cpp).
- Builder/populator = `EmoteEngine_buildClampControl` @0x66EE5C ("clampControl" key,
  dispatched from EmoteEngine_applyMetadata_buildControllers @0x67D4D0). Local populator
  = `RuntimeSupport::collectClampControlMetadata` -> `MotionSnapshot::clampControls`.
- Callees: sub_67C560 (var-track cascade) = Player::accumulateTimelineContributionLike_0x67C560;
  sub_67C6B0 (mirror flag) = Player::shouldMirrorEvalLabelLike_0x67C6B0;
  Player_bindParameterValue @0x6C4668 = Player::bindParameterValueLike/writeEvalResultValueLike_0x6C4668.
- `sub_67CC9C` @0x67CC9C = bind-loop + sub_67C8A8 bundled; has NO callers in binary
  (dead). Local model = `Player::applyEvalResultPostProcessLike_0x67CC9C`.

## deque#7 (clampControl) 40B element layout (evidence: builder 0x66EE5C writes v9-40..v9)
- +0  int32  type  (Motion_propGetInt "type")  -> disk-remap mode (0 squircle / 1 clamp-circle)
- +4  pad (zeroed)
- +8  double min (Motion_propGetDouble "min")
- +16 double max (Motion_propGetDouble "max")
- +24 ttstr  var_lr (X-axis HM key)
- +32 ttstr  var_ud (Y-axis HM key)
Builder gate: only "enabled"==true entries pushed. Deque header base engine+496;
finish._M_cur engine+528; block=480B=12 elems. Local struct: EmoteClampControlEntry_Deque7
(EmoteEngine.h ~237), member EmoteEngine::_clampControlDeque7 (documentation/unused).

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
redundant bind-loop formerly ALSO ran on the Player progress path via
Player::frameProgress -> applyEvalResultPostProcessLike_0x67CC9C (WRONG location). That
call is now REMOVED from frameProgress. The clamp runs solely from EmoteEngine::progress as
`player().applyClampControlsLike_0x67C8A8()` (EmoteEngine.cpp ~line 1963), right after the
HM7 bind-loop (~line 1939) and before step-7 player().progressFramesLike_0x6D2A54. The
bind-loop was ALREADY live in EmoteEngine::progress over _labelToValueHM7, so removal of the
frameProgress _evalResultList bind-loop is pure de-duplication. No double-clamp now.
applyEvalResultPostProcessLike_0x67CC9C is now caller-less (mirrors the caller-less binary
sub_67CC9C); kept as the model of the dead fn — do NOT call from any progress path.

### 2 fidelity gaps FIXED in applyClampControlsLike_0x67C8A8 (PlayerFrameProgress.cpp ~321)
(1) sub_67C560 var-track cascade now runs on each axis value before normalize
    (accumulateTimelineContributionLike_0x67C560 on varLr & varUd) — was omitted.
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
