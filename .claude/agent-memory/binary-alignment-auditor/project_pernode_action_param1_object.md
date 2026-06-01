---
name: pernode-action-param1-object
description: 砖5/洞2 per-node onAction param1 是 *(node+0) = "label" 字符串变体(tvtString), 本地 widen(layerName) 忠实; 参数化节点(node+8!=0)不推 per-node action(本地需 gate)
metadata:
  type: project
---

per-node onAction (砖5/洞2) 的 param1 在 libkrkr2.so 中是 `*(node+0)` = 节点的 **"label" 字符串变体**(tvtString)，**不是** layer dispatch 对象。本地 `widen(node.layerName)`(layerName=PSB "label", NodeTree.cpp:108)是忠实端口。

**纠错(2026-06-01 实证推翻先前 "param1=Object" 结论):** 先前审计误判 param1 是 Object 变体。重新反编译证伪：
- `Player_initNodeFields` @0x6B3C78 在 **0x6B3DC8** `sub_529524(...,L"label",...)` PropGet "label" → **0x6B3DF4** `*(node+0)=v36[0]`(label 变体的 object 指针, 即 tTJSVariantString*)。**node+0 就是 label 字符串对象**, 不是 layer 对象。
- push 现场 advance@0x6B74B4-E4: `MOV W8,#2; STUR [var_60]`(tag=**2=tvtString**) + `LDR X8,[node]; STUR [var_70]`(data=*(node+0)) + AddRef → param1 = {type=tvtString, data=label 串对象}。param2=slot+0x120(=+288)="act"。
- 故 param1 是 label 串(值层面 = node.layerName)。MotionNode.h:54 注释的 `*(node+0)+16=iTJSDispatch2*` 是 onGroundCorrection(sub_6BAA10)的另一处读法, 与 per-node action push 无关; **勿用 node.tjsLayerObject 改 param1**, 那会偏离二进制。残留差异仅 ABI 表示(复用串对象 vs 新建 tvtString), 按 CLAUDE.md 字节布局工作法无需对齐。

**Why:** sub_6B638C=Player_pushActionEvent_guess @0x6B638C 推 44B {type=0(onAction), frame变体@+4, action变体@+24} 到 player+936 deque, 被 Player_dispatchEvents@0x6C4490 type0 消费(type1=onSync, type2=空). gate 均 `(slot+22 & 4)!=0`(=mask 0x40000 act 帧)。注意区分: layer/root 流(0x6B6E68)param1 是 **VOID** 变体(洞3); per-node 是 **tvtString**(洞2)。

**How to apply:** per-node onAction param1 = node "label" 串, 本地 `widen(node.layerName)` 正确, 无需 MotionEvent 承载 Object。已于 PlayerUpdateLayerEval.cpp:367-379 钉死证据注释。

**过度触发 bug(真 bug, 已修):** 三处节点循环(advance @0x6B73B4 / rewind for(j=1) / progress_inner@0x6C106C for(j=1))都分流: `X8=*(node+8); if(X8!=0){ Player_advanceNodeFrames(node,player); continue; }`(advance 0x6B73B4-C8 实证) — 参数化节点(node+8=parameterEntry)走 advanceNodeFrames(0x6B7E44, **无 &4 mask 检查、无 pushActionEvent**)且 continue, **不推 per-node action**; 只有 node+8==0(非参数化)内联 2-slot seek 才推。本地原先对所有 i>=1 节点传 &_pendingEvents = 参数化节点多发 onAction。**已修**(PlayerUpdateLayerEval.cpp:582): `nodeEvents = (node.parameterEntry==nullptr) ? &_pendingEvents : nullptr`。node+8=parameterEntry(56B param table 选项, PSB "parameterize", node init 0x6B3EA0)。

**preProgressDirtyNodes @0x6B6878 (洞1):** 遍历 idx=1; 门控 node+1996!=0 && emoteEdit(node+1980) PropGet("modified")&1; clear=PropSet(512,"modified",void)@0x6B6A08; rebuild=initNodeTimeline_guess@0x6B6A1C(=sub_6B64AC). 是 progress_inner 第一步(0x6C10AC, firstFrame/cursor 之前). 本地 inert 移植忠实(modified 永不置位故省略 clear 安全). 注意 sub_6B64AC 自身在 0x6B6780 含内嵌 per-node action push(gate selT==slot+328 && slot+342&4) — 重建路径核对时勿漏.
