---
name: controller-stepping-typed-deque-vs-dead-buckets
description: EmoteEngine controller stepping (0x67D01C/0x671228) uses ONE typed-deque model; the _type4..8ControllerAnimators parallel buckets were dead port residue (removed 2026-06-05).
metadata:
  type: project
---

Controller stepping in libkrkr2.so is a SINGLE typed-deque model on the EmotePlayer/EmoteEngine `this`. No independent per-Player animator bucket exists.

**EmoteEngine_progress @0x67D01C** step loops (verified fresh decompile 2026-06-05):
- +256/272/280/288 deque#4 (16B stride) EmoteVarController4_step_guess
- +336/352/360/368 deque#5 (16B) sub_665600
- +416/432/440/448 deque#6 (24B) sub_666068 — writes 2 HM outputs (label + talkLabel)
- +656/672/680/688 deque#8 (48B) sub_668470
- +576/592/600/608 deque#7 (24B) EmoteVarController_step
- +736/752/760/768 deque (16B) inline curve eval
All step outputs go ONLY into HM7 via Player_HM2_upsert_labelToValue(this+1440,key). Then bind-loop @0x67d3a4 walks HM7 head (*(this+1456)) → Player_bindParameterValue_writesHM1_HM2. dt sub-stepped fmin(dt,1.1) @0x67d0b0.

**setVariable @0x671228** cases 4/5/6/7/8 index the SAME deques (+256/+336/+416/+576/+656, map-spill +280/+360/+440/+600/+680). Confirms these deques are the LIVE per-nodeType animator storage. case 7/8 have a gate byte (+16) check. Fallback (no key / type 0-2): Player_HM2_upsert_labelToValue(this+1440,key)=value.

**Local LIVE mirror:** EmoteEngine.cpp _stateMachineDeque4/5, _compositeVarDeque6, _auxVarDeque8, _vectorVarDeque9, _lookupCurvesDeque10 (push @712-1224, read @1675-1722, stepped in EmoteEngine::progress @EmoteEngine.cpp:1776 lines 1835-1913 → _labelToValueHM7). This is the faithful 0x67D01C port.

**DEAD residue removed 2026-06-05:** `_type4..8ControllerAnimators` deques + `_variableAnimators` map (type detail::LegacyVariableAnimatorState, header internal/legacy_variable_state.h) were a superseded parallel stepping model. ZERO push/emplace/insert across cpp/ — clear()/erase() only. PlayerFrameProgress.cpp had a duplicate stepControllerBucket while-loop over them (operated on empty containers = no-op). Removed: 5 deques + 1 map + 6 accessors (controllerAnimatorBucketLike/find/erase/clearControllerAnimatorStateLike_0x671228) + findInDeque/eraseInDeque helpers + 2 type aliases (VariableKeyframe/VariableAnimatorState) + the legacy_variable_state.h header. refreshFixedControllerEvalOutputsLike_0x67D01C rewritten to read ONLY _evalResultValues (HM7/HM2 mirror) → getVariable fallback (dropped dead-bucket branch). Logo differential byte-identical (m2logo 93f + yuzulogo 243f PASS) confirms byte-neutral.

**Lesson:** a "*Like_0xADDR"-named local helper/container is NOT proof of binary correspondence — these had address-named symbols yet corresponded to NO binary container. Always verify write-path (push/emplace), not just the suggestive name. clear()/erase()-only container = dead.
