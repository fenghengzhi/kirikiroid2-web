---
name: clusterM-particles-childmotion-audit
description: 簇M(粒子系统+childMotion) 2026-06-07审计结论;05-30的L1/L8/L9/L10/L11全resolved;残留4个P2;关键字段/地址映射
metadata:
  type: project
---

簇M审计 (PlayerUpdateParticles.cpp / PlayerUpdateChildMotion.cpp) vs libkrkr2.so，2026-06-07。

**结论: PARTIAL DEVIATION (minor, 全P2)。05-30 cluster L 的 P0/P1 全部 RESOLVED。**

**地址↔本地映射 (本次反编译确认):**
- 0x6BE0C0 childMotionPass ↔ updateLayersPhase3_MotionSubNode
- 0x6C17A4 particleStepChildren ↔ physics_step Pass1/Pass2
- 0x6F363C = std::vector<DeadRI(44B POD)>::_M_range_insert ↔ aggregateChildMotionRenderItemsLike_0x6F363C
- 0x6BF0DC particleSystem ↔ updateLayersPhase3_ParticleSystem
- 0x6BEDD0 emitterPass ↔ updateLayersPhase3_ParticleEmitter
- 死缓冲区 splice: childMotion@0x6BE2C0, particle@0x6C1A00。insert position = parent.begin()(NOT end)。两buffer恒空→inert但已1:1复刻。

**05-30 偏差全部消除 (证据):**
- L1(P0) BLOCK1双层门控: 0x6BF314 `inheritVel(node+2176)!=2->LABEL_64`; 0x6BF38C 内层`!slotDone(slot+344)&&inheritAngle(node+2172)`; matrixChanged->full xform->LABEL_64(无deltaPos); matrix-unchanged->落穿0x6BF32C加deltaPos。本地main-if+else-if已1:1嵌套。RESOLVED。
- L8(P1) parameterEntry fallback: 0x6BE210 `!*(node+8)->v4[47](player+376)`; 本地resolveNodeParameterEntry的_defaultParameterEntryPtr=player+376。RESOLVED。
- L9(P1) skip-gate: 0x6BE270测node+1504; **node+1504=accumulated.dirty(needs-update byte)**, MotionNode.h AccumulatedState序visible(+1506)/active(+1505)/dirty(+1504)。05-30"+1504非dirty"疑虑**证伪**。RESOLVED。
- L10/L11(P1) drawlist splice: 已present且faithful(std::vector insert@begin + child clear)。RESOLVED。

**残留4个P2 (本地数据流局部fix, 不阻塞可见性):**
- M1: BLOCK1 prevM(node4[145]/[146])更新在二进制中独立于childCount(0x6BF3F8在`if(Count>=1)`@0x6BF440之前);本地把childCount>=1折进主if→childCount==0时prevM不刷新(1帧矩阵滞后)。Fix: prevM写出childCount gate。
- M2: emission trigger二进制两处(gate 0x6BF710 + selector 0x6BF680)都读**slot+736**(=prt.trigger, mergeFrameContent slot[104], gate mask&1)。node+2224 mirror只含9个double**不含trigger**。本地selector(Particles.cpp:366)用`pn.prtTrigger`(port-only node mirror, 无二进制依据), gate(line360)用`activeSlot().prtTrigger`(对)。Fix: line366→activeSlot().prtTrigger; 考虑删MotionNode.h:482 node.prtTrigger镜像。注意emitter pass(0x6BEDD0)trigger是另一字段slot+708,勿混。
- M3: particle Pass2 child step二进制0x6C19E4用`*(a1+592)`=_deltaTime(speedMul*delta);本地Particles.cpp:808用_frameLastTime(raw dt)。childMotion路径已用_deltaTime(R1.B-audit-C)。Fix: →_deltaTime。
- M4(platform): Pass1 cull viewport 0x6C18F0读window rect`*(*a1+848/852/856/860)`;本地用0,0,width,height。合理平台近似但**Particles.cpp:769未标PLATFORM_BOUNDARY**。需补注释。

**容器/生命周期: ALIGNED** — particle children=TJS Array add/erase dispatch(非std::vector); child Player=new+CreateAdaptor+Release; dead buffer=std::vector匹配构造点。
