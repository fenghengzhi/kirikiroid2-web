---
name: node44-invented-dirty-channel-removed
description: markNodePayloadDirtyFromState 是端口发明的 node+44 每帧无条件脏化通道(二进制无对照);移除它打破 DRACU type-3 子树不收敛环
metadata:
  type: project
---

DRACU-RIOT 标题黑屏 + 1.9GB / 28000 节点泄漏的根因之一 — 2026-06-21。

**根因**: 端口 `markNodePayloadDirtyFromState`(PlayerUpdateLayerEval.cpp,原在
`advanceNodeFrameSelectionLike_0x6926B4` 尾部行 545 无条件调用)是发明的 node+44
(node.flags bit0x01)脏化通道,用 lastActive*(hasLastActivePayload/lastActiveFrameIndex/
lastActiveSrc/lastActiveMotionFlags/lastActiveMotionDtgt)端口字段做 payload-change 检测,
命中即 `node.flags|=0x01`。它在 seek 的 forward/backward 循环**之外**每帧执行,对静态(不
跨帧)节点也每帧把 node+44 置 1。

**二进制铁证**(ida-deep-analyzer + 自 disasm 0x6BB5D0 全函数):
- node+44 置 1 的全部 6 通道无一是"每帧无条件、与跨帧无关":3 个 seek(advanceNodeFrames
  0x6B7FBC / advanceRootAndNodes inline 0x6B72C0 / rewindRootAndNodes 0x6BA28C)全部被
  `if((v9&1)==0) goto early-out`(LABEL_25/98) 守卫=仅本帧实际跨帧时置;init seek 0x6B66F8;
  childMotionPass 0x6BE388(play-gate 后);reset/clear 路径 0x6B2BE4/0x6B81E0(非每帧)。
- evaluateTimeline(0x699AE4)对 node+44 **纯读**(0x699B1C `result = a2 || node+44`),
  从不写;无任何 payload-change 检测;node+1504 在此函数完全不出现。
- Player_updateLayers post-loop(0x6BBD2C/0x6BBD30)每帧无条件 `node+44=0` + `node+1504=0`。
- 静态节点本应:不跨帧→seek 不置 node+44→保持上帧 post-loop 清成的 0→evaluateTimeline
  返回 false(若 a2 也 0)→accumulated.dirty(node+1504) settle。

**环**: 发明通道使父静态节点每帧 node+44=1→evaluateTimeline 恒 true→childMotionPass
(0x6BE0C0)跳过门 `!v12 && !node+1504`(本地 PlayerUpdateChildMotion.cpp:74,= LABEL_18
0x6BE270)不命中→每帧跑状态传播→无条件脏化 child-root delta.dirty(0x6BEBAC,本地
:580 `cr.delta.dirty=true`,cr=child._nodes[0])→child-root accumulated.dirty=1→传染该
child 的 'bg'→'bg' 不跳过→脏化孙-root→环不收敛→子 motion 每帧重播 _deltaTime=0→子 player
时间永不自增→'bg' 永不到 done→type-3 子树永不 destroy→递归发散。

**(2)/(3) 不是端口偏离**(交叉核实): 全仓 `delta.dirty=true` 写点 100% 作用在 root
(root./_nodes[0]./cr.(=child._nodes[0])./rootNode.),无一写非-root。DIRTYDIAG 的 'bg'
deltaDirty=1/rootDirty=1 是环传播(child-root delta.dirty 由父 childMotionPass 0x6BEBAC
每帧无条件置,二进制相同),非独立源。修好 (1) node44 让父静态节点 settle → childMotionPass
跳过 → 不脏化 child-root → 环收敛。delta.dirty 清理时机本地与二进制一致(0x6BB5F8 在
`if(evalRet)` 块内清 node+1584,evalRet=false 时不清;post-loop 不清 delta — 全对齐)。

**改动**: PlayerUpdateLayerEval.cpp 删 markNodePayloadDirtyFromState + markNodeNoActiveFrame
(调用 3 处 + 定义);MotionNode.h 删 5 个 lastActive* 字段(无其它消费者,自洽闭环)。保留 357
(init 0x6B66F8)/522(fwd seek 0x6B6ADC)/535(bwd seek 0x6B9A3C)的 `node.flags|=0x01`(有
二进制依据,在迭代体内/init)。

**a2 (evaluateTimeline force 实参) 真实公式**(0x6BB5E0 LABEL_29,留参考):
`v25 = player+610 || node+47 || PARENT+1504 || node+1584`;parent=v23 由 node+36 climb
得(跳过 node+42&0x40 的祖先)。本地 timelineDirtyArg = forceDirty||needGround(node.groundCorrection)
||parentDirty||deltaDirty — needGround vs node+47 待来日核(本次未动)。
