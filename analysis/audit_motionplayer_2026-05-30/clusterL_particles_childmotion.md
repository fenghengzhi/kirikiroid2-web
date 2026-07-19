# Cluster L Audit — Particle eval + child/sub-motion update

Date: 2026-05-30. Authoritative: libkrkr2.so. No cpp/ edits. IDB renamed + saved.

## Binary anchors (all called in sequence from Player_updateLayers @0x6bb33c)

| Binary func (renamed) | addr | called @ | local file | local fn |
|---|---|---|---|---|
| Player_updateLayers_childMotionPass | 0x6BE0C0 | 0x6bbc90 | PlayerUpdateChildMotion.cpp | updateLayersPhase3_MotionSubNode |
| Player_updateLayers_particleEmitterPass | 0x6BEDD0 | 0x6bbc98 | PlayerUpdateParticles.cpp | updateLayersPhase3_ParticleEmitter |
| Player_updateLayers_particleSystemPass | 0x6BF0DC | 0x6bbca0 | PlayerUpdateParticles.cpp | updateLayersPhase3_ParticleSystem |
| Player_particleStepChildren | 0x6C17A4 | 0x6bf714/.. | PlayerUpdateParticles.cpp | physics_step block |

Architecture confirmed: each pass is a DEDICATED flat loop over all nodes filtering ONE
nodeType (3/6/4) — NOT a per-node switch dispatch. There is no node-type dispatch table;
updateLayers calls the three passes back-to-back. Child recursion (type 3 + type 4) is via
Player_progress_inner(child) + Player_updateLayers(child) on the child Player.

Container/lifetime ALIGNED:
- particle children = TJS Array (node.particleArrayVar) via add/erase dispatch + sub_6C1678
  index get; mirrors binary sub_6238C8/"add"/"erase"/sub_6C1678. NOT std::vector. GOOD.
- child Player created via new Player + CreateAdaptor + tTJSVariant + Release; matches binary
  operator new(0x568)/Player_ctor/sub_6F1794/AddRef/Release. GOOD.
- velocity on child _cameraVelocityX/Y/Z (player+784/792/800), damping _cameraDamping
  (player+600). GOOD.

## Findings

| id | func@addr | local file:line | sev | one-line |
|----|-----------|-----------------|-----|----------|
| L1 | 0x6BF314/0x6BF38C | PlayerUpdateParticles.cpp:213 | P0 | BLOCK1 guard collapses binary's 2-level structure: binary gates child-pos update on inheritVel==2 (0x6bf314) THEN matrix-update on (!slotDone && inheritAngle) (0x6bf38c) where slotDone is `v8[21].u8[536*v9+8]` (activeSlot+8 byte) — local ANDs all four into one `if`, then re-adds an `else if` fallback; dataflow merges two binary branch levels into a flattened ternary chain. Behaviorally near-equiv but structurally diverged; verify slotDone offset (active vs other slot). |
| L2 | 0x6BF440 (v11>=1) | PlayerUpdateParticles.cpp:255 | P2 | binary BLOCK1 inner child loop runs over `v11 = sub_56C694` = TJS Array count captured ONCE at 0x6bf300 BEFORE block; local recomputes childCount via getParticleCount() at top — same value, ordering ok. |
| L3 | 0x6BF788 LABEL_85 | PlayerUpdateParticles.cpp:386 | P1 | binary LABEL_85 timer-clamp is reached from BOTH freq mode (v67==0) and count mode fallthrough via LABEL_84/LABEL_95 when `*v66`(trigger)==0; local restricts clamp to `triggerType==0` branch only and comments "only reachable from frequency mode" — binary LABEL_84/LABEL_85 is also entered when v66 deref is 0 regardless. Edge case: trigger field 0 path. Re-verify control-flow join. |
| L4 | 0x6BF93C child ctor | PlayerUpdateParticles.cpp:446 | P2 | binary passes parent vtable copy `*(_QWORD*)v94=*(_QWORD*)v1; v94+8=v1` (sets parent ptr); local `new Player(_resourceManagerNative, this)` passes resmgr+parent — equivalent constructor contract, parent stored. OK. |
| L5 | 0x6C0174 inheritVel==1 | PlayerUpdateParticles.cpp:702 | P2 | binary gate `v8[136].u32[0]==1` (inheritVelocity==1) AND `dt!=0`; local matches (dt!=0 not dt>0). GOOD — noted, not a deviation. |
| L6 | 0x6BF7BC freq init | PlayerUpdateParticles.cpp:367 | P2 | binary first-frame init reads `*((double*)v2+251)` where v2 = L"y::Assign..." string base — that is a 60.0 constant pooled in rodata, i.e. 60.0/prtFmin; local uses literal 60.0. Equivalent constant. OK. |
| L7 | 0x6BE104 preview guard | PlayerUpdateChildMotion.cpp | P2 | binary guards the whole loop on `!*((_BYTE*)result+1092)`. Full AArch64 displacement scan identifies +1092 as the `preview` NCB property: ctor@0x6CF0A4 writes 0, getter@0x6D9638 reads it, setter@0x6D9640 writes it. The former `_isEmoteMode` mapping was wrong and has been corrected to `_preview`. |
| L8 | 0x6BE204 nodeType read | PlayerUpdateChildMotion.cpp:23 | P1 | binary reads parameterEntry mode from `v11=node+8; if(!v11) v11=player+47*8`(player+376 default entry) then mode=*(v11+48); local resolveNodeParameterEntry + ?mode:0 — verify fallback uses player+376 default entry not null→0. Binary fallback is player+376 entry, NOT literal 0. POSSIBLE off-by: local `v12 = parameterEntry ? parameterEntry->mode : 0` returns 0 when entry null, binary returns *(player+376+48). Check resolveNodeParameterEntry already applies the player+376 fallback. |
| L9 | 0x6BE270 dirty gate | PlayerUpdateChildMotion.cpp:38 | P1 | binary skip-to-LABEL_18 gate tests `node+1504` (accumulated.active-ish byte) when v12==0; local tests `!v12 && !mn.accumulated.dirty`. Binary byte is +1504 not the dirty bit; comment admits "binary tests node+1504, visible is node+1506". Field mismatch: confirm +1504 semantics (active vs dirty). |
| L10 | 0x6BE2A4/0x6BE2AC recursion | PlayerUpdateChildMotion.cpp:527 | P2 | child recursion order: binary progress_inner(child,dt) then updateLayers(child) then sub_6F363C drawlist merge + sub_A0F778 release loop (child+936..944). Local calls frameProgress+updateLayers but the post-recursion drawlist merge/release (0x6be2c0..0x6be2f8) is NOT in this fn — verify it lives in caller/emitRenderItem. POSSIBLY-MISSING drawlist splice. |
| L11 | 0x6C1A00 particle drawlist | PlayerUpdateParticles.cpp:791 | P1 | Player_particleStepChildren (0x6C17A4) after child updateLayers does sub_6F363C(a1+936,...) splice of child drawlist into parent + release loop (child+117*8). Local physics Pass2 calls child->updateLayers() but does NOT splice child draw items into parent _renderItems (a1+936). POSSIBLY-MISSING: particle render items may never reach parent draw list. HIGH-VALUE recheck. |

## MISSING / POSSIBLY-INVENTED

- MISSING (P1, L10+L11): post-recursion drawlist splice `sub_6F363C(parent+936, child+936..944)` +
  per-item release loop. Present in BOTH binary child-motion (0x6be2c0) and particle step
  (0x6c1a00). Neither local pass performs the parent<-child render-item splice here. If the
  web port routes child render items through a different path (emitRenderItem / cluster I), this
  is a platform refactor; if not, particle/child-motion sprites are dropped. Cross-check with
  cluster I (Player_emitRenderItem 0x6C4E28) — flag for module driver.
- NOT INVENTED: local BLOCK1 `else if (inheritVel==2 && childCount>=1)` fallback (line 319) DOES
  have binary basis — it is the unconditional `if(v11>=1){...add delta...}` tail at 0x6bf32c that
  runs after the matrix branch is skipped. Local splits it into a separate else-if; acceptable.
- Emitter pass (0x6BEDD0): local fully structurally matches (active/done guard, flags gate,
  LABEL_21/LABEL_27 timer, trigger switch 2/3/4 + sub_6C1540 crossfade). No invented logic. GOOD.

## Verdict

PARTIAL DEVIATION (mixed). Container/lifetime/dispatch architecture is ALIGNED (TJS Array,
new+adaptor, raw velocity fields) — reject-functional-equivalence bar met there. Logic-level:
emitter pass GOOD; child-motion + particle-system passes are mostly faithful but carry:
- 1 P0 structural flatten (L1 BLOCK1 two-level guard collapsed),
- ~4 P1 (L3 LABEL_85 join, L8/L9 parameterEntry/dirty-gate field-fallback mismatches,
  L10/L11 MISSING child->parent drawlist splice — highest risk),
plus benign P2s. Recommend: (1) re-derive L1 as nested binary branches; (2) confirm field
offsets +376/+1504 for L8/L9; (3) URGENT cross-cluster check of L11 drawlist splice with
cluster I render pipeline before declaring particles visible.
