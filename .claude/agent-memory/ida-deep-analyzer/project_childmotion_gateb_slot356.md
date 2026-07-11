---
name: childmotion-gateb-slot356
description: childMotionPass@0x6BE0C0 gate B (slot+356) 真实语义 — 是活动帧槽的 src ttstr(slot+36 of active slot), 由 mergeFrameContent 写, NOT findSource; node+1996 是死字段恒 0
metadata:
  type: project
---

childMotionPass@0x6BE0C0 type-3 子节点 play/destroy 判定:
- v14=activeSlotIndex=*(int*)(node+1392) (LDRSW [X26+0x570])
- 帧槽: slot0=node+320, slot1=node+856 (stride 536). slot 内偏移: +0(frameIdx) +8(time) +20(mask) +24(invisible) +25(interpFlag) +36(src ttstr) +288(act).
- gate A @0x6BE31C: LDRB [node+536*v14+0x158]=slot+24(invisible). CBZ(==0)→play候选; !=0→destroy.
- gate B @0x6BE368: LDR [node+536*v14+0x164]=slot+36(active slot 的 src ttstr dispatch). CBZ(NULL)→destroy; 非空→play.
  注意: 0x164=356, 是 slot+36 不是 node+356(只有 v14==0 时二者数值相等).

slot+36(="slot+356") 唯一写者 = mergeFrameContent@0x692ce4 (*(slot+9)=src ttstr),
门控 (1<<nodeType)&0x1849 (type3: 1<<3=8, 8&0x1849=8 命中); 且帧 content 必须真带 'src' 属性.
resetFrameSlot@0x69261c (parseFrame 开头调) 清 slot+36=0. parseFrame@0x6926B4 不写 slot+36.

关键纠正(推翻 prompt 框架): Motion_Player_findSource(sub_6948E8) 不写 slot+356.
findSource(a1=node+200 源结构, a2=Player, a3=slot+356 是输入=src名字, a4=slot+348 是输入=icon名).
findSource 把结果写 node+200 源结构(+200 valid byte, +224 texture, +232/240 w/h). slot+356 是它的输入不是输出.
findSource 的 bitmask 门 (node+1996 || (6145/6153 & (1<<type))) 只控制 node+200 源解析跑不跑, 跟 slot+356 无关.

node+1996(0x7CC) 语义: 全二进制唯一写者 = MotionNode_initFields@0x6f1aec (STR WZR, 零初始化). 无任何运行时写者.
=> node+1996 对所有节点恒为 0 (死字段, 疑似未启用的 meshSync/debug flag). bitmask 门里它永远是 0, 退化成纯 (1<<type) 项.
读者(14处, 全是同一 findSource 门): initNodeTimeline/preProgressDirtyNodes/advanceRootAndNodes/advanceNodeFrames/rewindRootAndNodes/sub_6BB300/sub_6BC4F0(5次)/sub_6BD8DC/sub_6C2334.

slot+24/slot+880 build-time init: MotionNode_recursiveBuild@0x6b4be8/0x6b4bec STRB 1 (=invisible). 故 fresh 子节点 slot+24=1 → destroy.

parent vs grandchild bg@12 判别因子 = slot+36(src) 是否被 mergeFrameContent 填:
取决于该 Player 当前活动帧槽是否落在一个 content 带 'src' 的帧上(即 motion 已 advance 到可见帧).
grandchild 在 child-Player dt=0 firstFrame 只 reseek/初始 parse 到 time=0 帧(frameList[0]=type2@time0 无src content)→ slot+36 NULL → destroy.
parent 经真实 dt advance 落到 type0@frame71(带 src content)→ mergeFrameContent 填 slot+36 → play.
=> 是候选(b)+(c)组合, 不是(a)(node+1996 恒0不可能是判别因子).
