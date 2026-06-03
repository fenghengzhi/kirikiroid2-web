---
name: m2-setvariable-controller-subsystem
description: M2 setVariable 9-case dispatch (0x671228) + 5 controller append handlers + builder (0x67D4D0) — fresh-decompile verdict 2026-06-03; HM6+deque-index READER architecture vs local parallel controllerBindings/Legacy-deque model
metadata:
  type: project
---

Fresh-decompile audit 2026-06-03 (read-only). Authoritative addresses:

- **setVariable 0x671228** = EmotePlayer::setVariable (a1=EmotePlayer ~1576B, NOT motion::Player). Inline FNV-ish hash of key → `sub_6887F4(this+1384 [HM6], hash%*(this+1392), key)`. If found AND `*(result)` (embedded dispatch-obj ptr) non-null: compute easeWeight v22 (dur==0→1; >0→dur+1; <0→1/(1-dur)); set `*(this+1162)=1`; switch on `*(obj+16)` (=VarRef.type). **Index = `*(unsigned int*)(obj+20)`** indexes the controller deque at fixed EmotePlayer offsets via libstdc++ deque-index math: case4→+256 (sub_6638B0), case5→+336 (sub_6652D4), case6→+416 (sub_665E34, inline 2-label compare → else *(ctl+108)=(int)value), case7→+576 (Animator_setKeyframes 0x667300, gated on *(elem+16) byte), case8→+656 (sub_6681E4). cases 0/1/2 gated on `*(this+1159)` byte (break vs return). default→return. If lookup miss OR obj null → `Player_HM2_upsert_labelToValue(this+1440)` raw `*(double*)=value`.

- **Controller append handlers** (all share clear-on-(dur<=0)/replace-or-append-keyframe deque pattern, element 504B blocks via `operator new(0x1F8)`, push 3 floats {value,dur,easeWeight}; a2&1 = _emoteAnimatorFlag append-vs-replace):
  - sub_6638B0 (case4) & sub_6652D4 (case5): DUAL-buffer controller (+16/+24/+32 AND +96/+104/+112 deques, `+296`int `+300`float commit fields). Identical bodies.
  - sub_665E34 (case6): single deque +16..+72, `+80`int `+84`float.
  - 0x667300 Animator_setKeyframes (case7): single deque + `+80` count → memcpy keyframe payload; calls EmoteVarController_deque20B_pushback.
  - sub_6681E4 (case8): single deque +16..+72 `+84`int; on dur<=0 calls EmoteSelectorController_applySelection(a1,(int)a3,0,0).

- **builder 0x67D4D0** = EmoteEngine_applyMetadata_buildControllers. Runs **14** builders in order (not 6): variableList, bust(sub_66B018), hair(sub_66B9D0 a1+80), parts(sub_66B9D0 a1+160), eye, eyebrow, mouth, transition, selector, loop(sub_66E480), clamp(sub_66EE5C), mirror(sub_66F364), instantVariableList(sub_66F64C), timeline. Each *Control PropGet → builder operator-new's controller, push {ctl,label[,label2]} into category deque, insert HM6{type,index}. Also reads "mirror"(bool→setRootFlipX), "scale"(double→+1168, EmoteVarController_step→+1176=1/(scale*v16)).

- **getVariable 0x533E1C** scope-router: sub_6CD16C scans var-track deque @*(a1+1312) (160B/40-qword stride) for label. IN-scope → HM1_cascadeJoinAndLookup(0x6CD39C, HM1+264). ELSE → evalKey_cascade(0x6CD23C, HM4+1240 first). **This is a DIFFERENT table fork than setVariable** (setVariable reads HM6+1384 for controller dispatch & HM2+1440 for raw write; getVariable reads HM4/HM1). The "double-table fork" = setVariable populates HM6/HM2(+controllerDeques); getVariable reads HM4/HM1. They do NOT share one map.

- sub_67C560 = bust/hair target+const bind-loop: walks `result+936` deque, per obj flag&2 walk +16..+48 sub-deque 56B nodes, strcmp `*a2` key → `*a3 += elem[+48]*obj[+72]`. NOT ported.
- sub_661F7C = mesh/value resolver (~1925B): clears 88B-stride deque @+104/+112, sub_660028, min-scan 88B elems @+80 (sentinel -1.0), then bezier-ish deque splice into a2 64-block deque. NOT ported.

**Local model (DIVERGENT architecture, value-equivalent intent):** PlayerVariable.cpp setVariableResolvedWeightLike_0x671228 dispatches via `_activeMotion->controllerBindings` (std::unordered_map) → switch(binding.type) → per-type `std::deque<LegacyVariableAnimatorState>` buckets (_type4..8ControllerAnimators in EmoteEngine.h) searched BY STRING (findInDeque/PlayerCore.cpp:60). Queue logic re-implemented inline as VariableAnimatorState (queue.push_back VariableKeyframe). The binary-faithful `_scalarHM6_1384` VarRefMap EXISTS in EmoteEngine.h but is NOT the runtime dispatch source. None of sub_6638B0/6652D4/665E34/6681E4/667300/67C560/661F7C are ported (addresses appear only as provenance comments). getVariable IS ported faithfully (PlayerVariable.cpp:637 — 2-branch router matches 0x533E1C).

VERDICT per dim: structure ❌ (parallel binding-map+legacy-deque vs HM6-index+controller-classes); data-flow 🟡 (getVariable ✅, setVariable diverges); call-chain ❌ (no controller-class append calls); lifetime 🟡; container ❌ (unordered_map controllerBindings + Legacy deque vs HM6 + typed controller deques); boundary 🟡 (easeWeight ✅ exact, dur-sign branch ✅, cases 0/1/2 gate present but simplified, case-6 label/talkLabel 2-field compare partially modeled via role=="label").

Open gaps confirmed still open: bust/hair bind-loop sub_67C560, sub_661F7C resolver, clamp/mirror/instantVariable/timeline builders, HM6↔controllerBindings unification, controller-class append handlers (5×), DUAL-buffer for case4/5.
