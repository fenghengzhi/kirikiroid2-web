---
name: updatelayers-geometry-particles-var-childmotion-verdict
description: 2026-06-07 fresh full-decompile audit of motionplayer updateLayers/geometry/particles/variable/childMotion subsystems vs libkrkr2.so; verdicts + open gaps
metadata:
  type: project
---

2026-06-07 只读审计 (fresh decompile 0x6BB33C/0x6BF0DC/0x6BE0C0/0x699510/0x6997F0)。结论：5 个子系统整体高度忠实 (架构/数据流/调用链/容器选型/边界行为全对齐)，e8e4499/5fc6169 的 type-4 restore + 粒子链移植确认忠实，非过度简化。

**✅ 已确证忠实 (byte/逻辑对照通过):**
- updateLayers 主累加 0x6BB33C：camera vel/damping pow(d,dt/60)、root delta memcpy、phase2 inheritFlags 0x1FC fast-path + per-bit slow-path + independentLayerInherit root 反算链、mesh deform sub_69AE74、onGroundCorrection sub_6BAA10、phase3 10-pass 顺序 (0x6BBC60..0x6BBCA8) 全对齐。本地 PlayerUpdateLayerEval.cpp:1099 phase2 用 readNodeFrameSlotsLike (只读 slot) → evaluateTimelineLike_0x699AE4，复刻 binary "progress 填 slot / updateLayers 只读不 cursor-step" 两遍数据流 (0x6bb5f0 Player_evaluateTimeline)。
- particleEmitterPass 0x6BF0DC：读 node+2224..2288 eval 镜像 (particleInterp[0..8]) 而非 slot prt-block 直读；freq/count 双模式、RNG 序 (sub_6BA7B8)、sphere/box/disk 发射、speed/angle/zoom lerp 顺序、coordinateMode 2D/3D 分支、inheritVelocity==1 加 parentDelta/dt、maxNum erase、particleStepChildren 全对齐 PlayerUpdateParticles.cpp。child 创建走 new Player+CreateAdaptor+Array.add (TJS dispatch，非 std::vector)。
- emitter pass (nodeType==6, sub_6BEDD0)：flags==0/!=0 门控、triggerType 2/3/4、sub_6C1540 crossfade 偏移、findNodeByLabel(_nodeLabelMap)=sub_6F2228 对齐。
- childMotion 0x6BE0C0：v12=parameterEntry->mode (回退 player+376 default entry，resolveNodeParameterEntry 含 _defaultParameterEntryPtr 回退，忠实非返回 0)；slotDone cleanup (allplaying=0/sub_6C0DE8/resetNodes)；src split "/"；angle case1-4 (atan2/sub_69A4D4 finite-diff/target lookup)；origin offset；matrix 直拷 vs 旋转分支；clip-chain 传播 (1936/1968/1952)；LABEL_18 无条件 frameProgress+updateLayers 全对齐。
- type-4 restore 链 0x699510(init)/0x6997F0(restore)：V+544 type3 child Player dispatch、V+672 type4 particle Array dispatch (均 tTJSVariant 非 ttstr，已纠正)；slot+744≡slot+424 alias 注释正确表达 (PlayerUpdateLayerEval.cpp:122-166 + PlayerVariable 无幽灵 prtResult)；type-4 init snapshot node+2224..2288→V+600..664 仅 slot+344(done)==0 时；mesh 非对称 (init 读 node+2024 / restore 写 slot+640) 对齐。
- variable：getVariable 2-branch scope-router (0x533E1C: inScope→HM1cascade，else HM4(_variableSnapshotMap)→HM1/HM2)；setVariable 经 writeEvalResultValueLike_0x6C4668→bindParameterValue HM1(_evalCascadeMap)/HM2(_evalResultValues)/+408 ramp (equal_range)。sub_6B9650 heapResult rebuild + 0x697D34 chainSegments split 忠实。容器：HM=unordered_map<ttstr>，ramp=multimap，cascade chain=vector<ttstr>。
- node-walk 上界全部 `i < _nodes.size()`：binary `dequeSize-1` 是 libstdc++ deque::size() 对 >512B(node 2632B,1-elem/block) 的 +1 bias 内联，非 sentinel。已多处交叉核实 (0x6C12D8/0x6B7398/0x6B9200/0x6B6920/0x6BE1D0)。

**❌/🟡 OPEN 偏差 (真实缺口):**
- **GAP-1 (P2, childMotion render-list 合并缺失)**: binary 0x6BE0C0 末尾 0x6BE2C0 调 `sub_6F363C(parent+936, child+936, child+944)` 把 child 的 render-item list (child+936..944, 44B/elem) 合并进 **parent+936**，随后 0x6BE2D0 释放并清空 child+936..944。本地 PlayerUpdateChildMotion.cpp:538-539 仅 `child.frameProgress + child.updateLayers`，**完全缺失 child→parent render-list 聚合 + child list 清理**。grep 确认本地无 +936/+944/6F363C/renderList 对应。属真实数据流缺口 (child 渲染产物如何回灌父 draw list)，非平台边界。粒子 system pass (0x6BF0DC 的 particleStepChildren) 同理可能涉及，需复核 sub_6C17A4 是否也有 +936 聚合 (本次未 decompile particleStepChildren，标 DEFERRED 复核)。
- 注：审计 prompt 提到的 "+992 FuncCall / +656 defer" — +992 已正确处理为 RM dispatch (child ctor 传 parent+992)；childMotion 中无 +656 路径，onFindMotion+play 走 sub_6BE0C0 链忠实。

证据地址全部 fresh-decompile 于本次会话。无 IDB 修改 (只读)。
