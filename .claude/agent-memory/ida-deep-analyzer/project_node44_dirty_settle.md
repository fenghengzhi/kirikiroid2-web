---
name: node44-dirty-settle
description: node+44(byte) 在 seek 路径的 settle 机制——静态 type-3 节点靠早退保持 0,纠正 0x6B7E44 IDA 注释的误导
metadata:
  type: project
---

# node+44(byte) 在 progress-pass seek 路径的 settle 机制(指令级)

二进制中 node+44(byte) 只在 seek 循环**实际跨帧迭代过**时才被置 1;静态节点(selectionTime 落当前 active slot 区间、0 次迭代)走早退路径**绕过**置位,保持上帧 post-loop 清成的 0。这是 type-3 child motion 能 settle、不每帧重播的根因前提。

## 写 node+44=1 的全部指令(仅 2 处,均门控在跨帧)
- **0x6B7FBC** `STRB W9(=1),[X20,#0x2C]` in Player_advanceNodeFrames(0x6B7E44,参数化节点)。位于 loc_6B7FB4。入边:0x6B7F6C(B.HI,需 backward) 或 0x6B7F70(TBNZ W26#0,forward 迭代过 v9==1)。早退:0x6B7F74 `B loc_6B803C`(LABEL_25,v9==0 且无需 backward)→绕过。
- **0x6B72C0** `*(_BYTE*)(v41+44)=1` in Player_advanceRootAndNodes(0x6B6ADC,非参数化 inline seek)。位于 LABEL_88(0x6B72BC)。门:`(v47&1)==0 → goto LABEL_98(0x6B733C)` 绕过;v47 仅 0x6B74F4 跨帧才置 1。外层还有 `*v45 < count-2` 假时整块跳过。

## 不写 node+44 的函数
- progress_inner(0x6C106C):自身无 node+44/node+1504 写;firstFrame 块(0x6C1108..)只写 player 自身字段(+481/+456/+1120/+1128/+609),不碰任何 node。调 seek 在 0x6C1264/0x6C130C(advanceNodeFrames)、0x6C1468 等(advanceRootAndNodes)。
- evaluateTimeline(0x699AE4):对 a1+44 **仅读**(0x699B1C `result=(a2&1)||node+44!=0`),无写。它写 node+56/+1507/+1508/+1536..1576/+2224..2288。

## settle 链(已确证闭合)
上帧 post-loop(0x6BBD2C 区)清 node+44=0 → 本帧 seek 静态节点不跨帧、走 LABEL_25/LABEL_98 早退、不碰 node+44 → node+44 仍 0 → evaluateTimeline internalDirty=a2||node+44 返回 false → node+1504 不写 → childMotionPass `!node+1504` 跳过门生效、不重播。

## ⚠纠正:0x6B7E44 内 IDA 注释误导
0x6B7FBC 处那段 set_comments("node+44=1 at runtime is EXPECTED, NOT a bug")**不完整**:它没区分跨帧节点(=1 正常)与静态节点(必须 settle 回 0)。对静态 type-3 节点,node+44 每帧=1 就是 bug——端口若在 seek 尾部无条件每帧跑 markNodePayloadDirty(payload 不变也置),即破坏 settle 链 → 每帧重播 child motion。修复:node+44=1 必须收进 forward/backward 迭代体内(同 0x6B7FBC/0x6B72C0 位置),静态早退路径不得触碰。
