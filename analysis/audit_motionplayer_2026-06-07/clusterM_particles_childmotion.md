# Cluster M Audit — Particle system + childMotion (2026-06-07)

Authoritative: libkrkr2.so. Re-audit of cluster L (2026-05-30) after recent commits
moved the type-4 particle chain + dead render-item splice. No cpp/ edits this pass.
IDB comments added @0x6bf314/0x6bf38c/0x6bf32c/0x6bf710/0x6be270/0x6c1a00 + saved.

## Scope
- PlayerUpdateParticles.cpp (826): updateLayersPhase3_ParticleEmitter / _ParticleSystem
- PlayerUpdateChildMotion.cpp (585): updateLayersPhase3_MotionSubNode + aggregate splice

## Binary anchors (decompiled THIS pass)
| binary fn | addr | local |
|---|---|---|
| Player_updateLayers_childMotionPass | 0x6BE0C0 | updateLayersPhase3_MotionSubNode |
| Player_particleStepChildren | 0x6C17A4 | physics_step block + Pass1/Pass2 |
| sub_6F363C (vector<DeadRI>::_M_range_insert) | 0x6F363C | aggregateChildMotionRenderItemsLike_0x6F363C |
| Player_particleEmitterPass (system) | 0x6BF0DC | updateLayersPhase3_ParticleSystem |
| Player_updateLayers_particleEmitterPass | 0x6BEDD0 | updateLayersPhase3_ParticleEmitter |
| Player_evaluateTimeline | 0x699AE4 | (mirror node+2224..2288 source) |
| Player_mergeFrameContent | 0x693C64 | (slot prt block source) |

## Findings vs cluster L

### L1 (was P0) -> RESOLVED. BLOCK1 two-level gate now faithfully replicated.
Binary 0x6BF314: `if (particleInheritVelocity(node+2176) != 2) goto LABEL_64`.
inheritVel==2: 0x6BF38C inner gate `!slotDone(slot+344) && particleInheritAngle(node+2172)`.
  - matrixChanged -> LABEL_36 full xform -> goto LABEL_64 (NO deltaPos add)
  - matrix UNCHANGED -> fall through to 0x6BF32C deltaPos add
inheritVel==2 && (slotDone||!inheritAngle) -> 0x6BF32C deltaPos add
Local (Particles.cpp:214,320): main-if (matrixChanged=full xform; else=deltaPos add) +
else-if (slotDone||!inheritAngle -> deltaPos add). Control-flow now 1:1. ALIGNED.
RESIDUAL M1 (P2): binary updates prevM (node4[145]/[146]) INSIDE the inner gate
INDEPENDENT of childCount (0x6BF3F8, before the `if(Count>=1)` loop @0x6BF440).
Local folds `childCount>=1` into the main if-condition, so when childCount==0 the
matrix branch is skipped entirely and prevM is NOT refreshed -> 1-frame matrix-tracking
lag for empty emitters. oracle-inert. Fix: hoist prevM write out of the childCount gate.

### M2 (NEW, P2): emission trigger selector source.
Binary emission reads trigger from **slot+736** at BOTH the gate (0x6BF710
`!*((_DWORD*)v64+184)`) and the freq/count selector (0x6BF680 `v66=(int*)(v64+736)`).
Confirmed slot+736 == prt.trigger: mergeFrameContent@0x693D74 writes `slot[104]` (slot
base node+320+536*idx, so slot[104]=+416 -> node+536*idx+736) = PSB "trigger" gated by
prt-mask&1. evaluateTimeline type-4 mirror (node+2224..2288) carries ONLY 9 doubles
(fmin/fmax/vmin/vmax/amin/amax/zmin/zmax/range) — trigger is NOT mirrored.
Local: gate (Particles.cpp:360) uses `activeSlot().prtTrigger` (slot-level, correct),
but selector (line 366) uses `pn.prtTrigger` (a port-only node-level eval mirror written
PlayerUpdateLayerEval.cpp:99 from state.prtTrigger<-slot.prtTrigger). Two different
sources where binary reads ONE (slot+736). Numerically equal same-frame (node.prtTrigger
is a same-frame copy of slot.prtTrigger), but architecturally the node-level prtTrigger
mirror is a port invention with no binary basis. Fix: line 366 -> `activeSlot().prtTrigger`;
consider dropping node.prtTrigger field (MotionNode.h:482) as a non-binary mirror.
NOTE: emitter pass (0x6BEDD0) trigger is a DIFFERENT field slot+708 (=node+320 base +388
= model-block region) used for its own 2/3/4 switch — do not conflate with emission's
slot+736 prt.trigger.

### L8 (was P1) -> RESOLVED. parameterEntry fallback uses player+376 default entry.
Binary 0x6BE210: `v11=*(node+8); if(!v11) v11=v4[47](player+376); v12 = v11?*(v11+48):0`.
Local resolveNodeParameterEntry (PlayerInternal.h:288) chain: node.parameterEntry ->
_parameterEntries[idx] (throw if OOR) -> _defaultParameterEntryPtr (=player+376) ->
_defaultParameterEntry. Returns mode via that resolved entry; only 0 when all null.
Matches binary `else v12=0` tail. ALIGNED.

### L9 (was P1) -> RESOLVED. skip-gate node+1504 == accumulated.dirty.
Binary 0x6BE270: when v12(parameterEntry->mode)==0, `if(!node+1504) goto LABEL_18`.
node+1504 = "needs-update/dirty" byte (Player_Rendering_Architecture: "update needed
if player+610||node+47||parent+1504||node+1584"). MotionNode.h AccumulatedState orders
visible(+1506)/active(+1505)/dirty(+1504); accumulated.dirty IS node+1504. The 05-30
"+1504 vs dirty bit" doubt is FALSIFIED. Local `!v12 && !mn.accumulated.dirty` ALIGNED.

### L10/L11 (was P1, MISSING drawlist splice) -> RESOLVED (present + faithful).
Both passes now perform the parent<-child dead-render-item splice + child-clear:
- childMotion LABEL_18 0x6BE2C0: `sub_6F363C(parent+936, parent.begin, child+936, child+944)`
  then destroy each elem +24/+4 tTJSVariant, child.end=child.begin. Local
  aggregateChildMotionRenderItemsLike_0x6F363C (ChildMotion.cpp:25) via std::vector
  insert(begin(), child.begin, child.end) + child.clear(). Called ChildMotion.cpp:578.
- particle Pass2 0x6C1A00: identical `sub_6F363C(a1+936, *(a1+936), v17[117], v17[118])`.
  Local Particles.cpp:819. ALIGNED.
Container topology: sub_6F363C IS std::vector<T>::_M_range_insert, 44B POD element
(1 int + 2 tTJSVariant copied via sub_A0FB64 AddRef-copy). Binary uses raw STL vector;
local std::vector matches the construction site. Insert position = parent.begin()
(NOT end) — local insert(begin(),...) correct. Buffers always empty this build -> inert,
but replicated per dead-data policy. ALIGNED.

### Pass2 child-step field propagation (0x6C1984) — ALIGNED.
Binary v19=child+200(root): v19[242]=parentClip(a2+1936), v19[244]=forceVisible(a2+1952),
v19[246]=visibleAncestor v14 (v14=a2 if meshCombine(a2+1963) else a2+1968). progress_inner
with a1+592(_deltaTime) then updateLayers. Local Particles.cpp:792-808: meshParentIdx =
meshCombineEnabled?pi:visibleAncestorIndex; cr.parentClipIndex/visibleAncestorIndex/
forceVisible set; frameProgress(_frameLastTime) [see M3] + updateLayers. ALIGNED.

### M3 (NEW, P2): particle Pass2 child step time arg.
Binary 0x6C19E4 calls progress_inner with `*(a1+592)` = parent _deltaTime (speedMul*delta).
Local Particles.cpp:808 calls `child->frameProgress(_frameLastTime)` = raw dt. Diverges
when _speedMul<0 with positive actualDelta (same class as the already-fixed R1.B-audit-C
in childMotion 0x6BE4A0). childMotion path uses _deltaTime correctly; particle Pass2 still
uses _frameLastTime. Fix: Particles.cpp:808 -> child->frameProgress(_deltaTime).

### M4 (NEW, P2, platform): Pass1 cull viewport rect source.
Binary 0x6C18F0 cull: `MaxX(c+168)>scr[848] && MinX(c+152)<scr[856] && MaxY(c+176)>scr[852]
&& MinY(c+160)<scr[860]` where scr=*(float**)a1 (device/window rect). Local Particles.cpp:769
uses `bMaxX>0 && bMinX<sw && bMaxY>0 && bMinY<sh` with sw/sh=_width/_height and left/top=0.
Field pairing matches (left/right/top/bottom rectangle-intersect) but binary reads a real
window rect (scr+848/852/856/860) vs port's 0,0,width,height. Web has no equivalent window
rect at that offset -> reasonable platform approximation, BUT NOT annotated as
PLATFORM_BOUNDARY. Add `// PLATFORM_BOUNDARY: Web uses 0,0,width,height for off-screen cull
(binary reads window rect *(*a1+848..860))` at Particles.cpp:769.

## Emitter pass (0x6BEDD0) — re-verified ALIGNED.
active/slotDone guard (node+1505 / slot+344), flags-byte(node+44) gate, dtgt compare
(ttstr c_str + sub_9B1ED0), LABEL_21/27 timer (parentTime = node+8?entry->value:player+1120),
trigger switch 2/3/4 (slot+708) with sub_6C1540 crossfade. No invented logic. ALIGNED.

## Subfunction alignment table
| callee | addr | status |
|---|---|---|
| sub_6F363C range_insert | 0x6F363C | OK (std::vector match, 44B POD, insert@begin) |
| Player_particleStepChildren | 0x6C17A4 | OK (Pass1 cull[M4]/Pass2 step[M3]) |
| Player_evaluateTimeline t4 mirror | 0x699AE4 | OK (9-double mirror, no trigger) |
| Player_mergeFrameContent prt | 0x693C64 | OK (slot+736 trigger=slot[104], gate mask&1) |
| Player_play / Player_playImpl | 0x6BE46C/0x6BFA68 | not re-audited (cluster E) |
| sub_697D34 path split | -- | OK (used by both, "/" split) |
| Player_progress_inner | 0x6C106C | cluster G (audited) |
| Player_updateLayers (child) | 0x6BB33C | cluster H |

## Platform boundaries (listed, audit-skipped)
- _directEdit emote-angle paths (Particles.cpp:265/675, ChildMotion.cpp:471): emote bone
  path player+464 + Player_initEmoteMotion — N/A web. Annotated in-code.
- M4 off-screen cull viewport rect (Particles.cpp:769): NOT yet annotated -> action item.

## Verdict
PARTIAL DEVIATION (minor, all P2). The 05-30 P0/P1 set is fully resolved:
- L1 BLOCK1 two-level gate -> faithfully nested (RESOLVED; residual M1 prevM/childCount P2)
- L8 parameterEntry player+376 fallback -> RESOLVED
- L9 node+1504 skip-gate == accumulated.dirty -> RESOLVED (05-30 field doubt falsified)
- L10/L11 parent<-child dead-renderitem splice -> RESOLVED (present, std::vector 1:1, inert)
Container/lifetime/dispatch architecture ALIGNED (TJS Array add/erase for particle children,
new Player+CreateAdaptor+Release, std::vector dead buffer matching construction site).
Residual P2: M1 (prevM hoist), M2 (emission trigger uses node mirror not slot+736),
M3 (Pass2 frameProgress _frameLastTime vs _deltaTime), M4 (unannotated platform cull rect).
None blocks visibility; all are local-dataflow fixes on the existing structure.
