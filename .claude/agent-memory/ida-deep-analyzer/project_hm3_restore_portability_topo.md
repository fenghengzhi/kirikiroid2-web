---
name: hm3-restore-portability-topo
description: pruneHM3 loop2 restore 4 条 A 类 DEFERRED 子路径 (contentMask/mesh/type3-child/type4-particle) 的可移植性裁决 + 叶子优先拓扑序 + type-4 仍缺的两个反编译证据点
metadata:
  type: project
---

3-function split (任务常误把全挂 evaluateTimeline 名下,纠正):
- eval node-base mirror = Player_evaluateTimeline @0x699AE4 (写 node+100/1536/2224..; switch node+28 = 4/5/10; NO type-3, mesh 是 node+2000==1 → sub_6996E8 从 slot+640 读到 node+2024)
- snapshot Node→V = Player_HM3_initValueFromNode @0x699510 (resetMotionState loop3)
- restore V→slot = Player_HM3_restoreValueToNode @0x6997F0 (pruneHM3 loop2 @0x6b857c; gate node+46 joinTarget!=0 && V+16==node+28; restore 真实 a2=entry+16, IDA decompile 漏标,disasm `ADD X1,X23,#0x10`@0x6b8564 证实)

可移植性裁决 (叶子优先拓扑序):
1. contentMask (slot+340=parseSlot+20 raw frame mask; init 0x699654 / restore 0x6998b8) — 证据齐, 本地 ClipSlot 缺 int contentMask, 加 1 字段+2 行即可. oracle-inert 纯架构, 无回归. **立即可做.**
2. type-3 child (node+1912; init 0x699598 / restore 0x699844) — 证据齐, 本地 childPlayerVar 已建模; 仅需把 V+544(ttstr_544) 改为持 childPlayerVar tTJSVariant 副本双向. 浅缺口. **可做.**
3. mesh restore (init 已实装 v.meshControlPoints=node.meshControlPoints; restore 0x699828 sub_6996E8=std::vector<float> deep-copy 已反编译) — init 侧 done; restore 缺本地 ClipSlot slot+640 mesh-eval vector. 加字段后可做. **半可移植.**
4. type-4 particle (node+2224..2288 → V+600..664 → memcpy slot+744 0x48; init 0x6995dc / restore 0x699890) — **DEFERRED, 缺最深前置**. 非对称: eval 读 slot+424..488 写 node+2224(crossfade lerp+easing v103); init 读 node+2224 写 V+600; restore 写 slot+744(非 node+2224). 本地 evaluateTimeline(PlayerUpdateLayerEval.cpp)用结构化 interpolatedCache(prtTrigger/prtF/V/A/Z/Range), 无 node+2224..2288 9-channel byte 镜像, 无 slot+744(72B=9 double)区, 无 slot+424..488.

type-4 仍缺的两个反编译证据点 (做之前必须先取):
- mergeFrameContent @0x692AB0 prt 块 (mask 0x100000 @0x693d74..0x693ecc): slot+424..488 如何从 PSB "prt" dispatch 写入, 是否==eval type-4 读的 slot+424..488
- slot+744 消费端: xref_to_field slot+744 找读者 (可能 sub_6BEDD0 @0x6BEDD0 粒子发射), 确认 72B particle-result 的渲染语义
