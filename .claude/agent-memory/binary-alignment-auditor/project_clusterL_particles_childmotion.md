---
name: clusterL-particles-childmotion
description: Cluster L audit — particle/child-motion passes binary anchors + drawlist-splice MISSING finding
metadata:
  type: project
---

Cluster L (particle eval + child/sub-motion) audited 2026-05-30 vs libkrkr2.so.

Binary anchors (renamed in IDB, all called back-to-back from Player_updateLayers @0x6bb33c):
- 0x6BE0C0 Player_updateLayers_childMotionPass (nodeType==3) ↔ updateLayersPhase3_MotionSubNode (PlayerUpdateChildMotion.cpp)
- 0x6BEDD0 Player_updateLayers_particleEmitterPass (nodeType==6) ↔ updateLayersPhase3_ParticleEmitter
- 0x6BF0DC Player_updateLayers_particleSystemPass (nodeType==4) ↔ updateLayersPhase3_ParticleSystem
- 0x6C17A4 Player_particleStepChildren ↔ physics_step block

Architecture confirmed (NOT a node-type switch): three dedicated flat per-nodeType loops, called in sequence. Child recursion = Player_progress_inner(child)+Player_updateLayers(child).

ALIGNED: particle children stored in TJS Array (node.particleArrayVar, add/erase dispatch, sub_6C1678 index get) NOT std::vector; child Player = new+CreateAdaptor+tTJSVariant+Release matching new(0x568)/Player_ctor/sub_6F1794.

HIGHEST-RISK open finding (L10/L11): binary child-motion (0x6be2c0) AND particle step (0x6c1a00) both splice child drawlist into parent via sub_6F363C(parent+936, child+936..944) + release loop. Local passes call child->updateLayers() but do NOT splice child render items into parent _renderItems. Either platform-refactored through cluster I emitRenderItem (0x6C4E28) or particle/child sprites are DROPPED. Must cross-check with cluster I before claiming particles render.

Other P1s: L8 parameterEntry mode fallback (binary uses player+376 default entry, not literal 0); L9 dirty-gate tests node+1504 not the dirty bit. L1 P0: BLOCK1 inheritVel==2 two-level binary guard (0x6bf314 then 0x6bf38c slotDone=activeSlot+8 byte) flattened into one local if + else-if.

Verdict: PARTIAL DEVIATION. Ledger: analysis/audit_motionplayer_2026-05-30/clusterL_particles_childmotion.md
