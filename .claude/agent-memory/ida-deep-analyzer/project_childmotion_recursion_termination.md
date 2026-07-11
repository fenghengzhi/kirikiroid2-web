---
name: childmotion-recursion-termination
description: childMotionPass 自引用 type-3 子递归如何终止——play 但 dedup 不 rebuild(playImpl 0x6b22ac gate)，非"不 play"；node+44 时序真相
metadata:
  type: project
---

childMotionPass@0x6BE0C0 处理自引用 type-3 grandchild(frame0=type2 带 src)时，三门全过、**确实调 Player_play(grandchild's nested child player)**。终止不在 childMotionPass，在 playImpl 的 same-motion dedup gate。

**node+44 时序(已钉死)**: initNodeTimeline@0x6B64AC 0x6b66f8 设 node+44=1(progress_inner 阶段，reseekTimelineCursors tail 调它)。child.updateLayers@0x6BB33C 主 eval 循环只读 node+44(evaluateTimeline@0x699AE4 0x699b24 LDRB，不写)，唯一清零在 0x6bbd2c，位于 childMotionPass(0x6bbc90)**之后**。故 childMotionPass 读 grandchild node+44 时确为 1。play 门 (v13&5)||node+44 必过。

**gate A 实际语义(0x6be31c)**: LDRB [node+536*slotIdx+0x158]=slot+24. CBZ→play(0x6BE360); 非0→DESTROY(0x6BE328). **⚠ slot+24 字段名 = invisibleFlag(不可见标志)，绝不是 "done"**。前 4 轮 DRACU 调查反复卡死全因把它叫 "done" 制造伪矛盾。三个写者: (1) build-time @0x6B4BE8 STRB 1 (slot0+24=node+344 / slot1+24=node+880 全置 1, end-relative SUB#0x8F0/#0x6D8, 唯一非帧写者); (2) parseFrame@0x6926B4 type==0(invisible)→slot+24=1 且跳过 content/src(slot+356 留 NULL) @0x692828; (3) parseFrame type 2/3→slot+24=0 且解析 content(slot+356=src dispatch) @0x69280c. 故 type2@time0 帧: slot+24=0(过 gate A)、slot+356 非空(过 gate B)→play。

**DRACU 伪矛盾终结(B+C+D vs E 闭案)**: 设 P=parent / N=P 的 type3 节点 / C=N+1912 child Player / N'=C 的孙 type3 节点 / C'=N'+1912 grandchild. P.childMotionPass 读 **N+344** 决定 C; C.childMotionPass(LABEL_18 的 C.updateLayers 内)读 **N'+344** 决定 C'——**两个不同层级的节点**。前 4 轮把层级混淆成一个。
- B+C+D 成立: fresh child C 首帧 progress_inner **确实 reseek**(disasm 实证: +376==0 走 0x6C1318 BL reseek; +376!=0 走 0x6C10E0 B reseek; 两路都 reseek; prompt#4 "不 reseek" 假设错误). reseek 尾循环 0x6B91B0 对 C 每个 idx>=1 节点(含 N')无条件调 initNodeTimeline. v19 被 count-2(=0) clamp 到 0, selectionTime=C+456=fmin(lastTime,0)=0 → slot0=frame[0]=type2 → N'+344=0.
- E(frida 稳定读 1) 与 B+C+D(首帧 0) **不矛盾**, 因 frida 观测**稳态帧**非首帧. 三机制时序耦合: (1) child 被 parent 每帧 re-play 但 **playImpl dedup gate(0x6b22ac) 不取**(same motion & no Force/AsCan)→不 rebuild **也不 re-init**→child+481 不重设 1→过首帧; (2) 过首帧后 progress 走增量路径(0x6C1330 LABEL_48 advanceRootAndNodes)**不再 reseek N'**; (3) N'+344 维持最后写入值; grandchild C' 被 destroy 后该支不再 advance 触达, 停在导致 destroy 的状态 → 稳定 1, destroy 不爆.
- 没有任何一环读错二进制. 错误是 B+C+D→E 的逻辑链漏掉 firstFrame 一次性 + dedup-不-rebuild-不-reinit + build-time=1 三者耦合.

**真终止机制 = playImpl@0x6b22ac dedup gate**:
```
if ((flags&5)/*Force|AsCan*/ || (curMotion(+976/+984) != newMotion &&
     (类型差 +60 || wcscmp(c_str) != 0)))  -> reload: loadMotion + initNonEmoteMotion + buildNodeTree
else -> return 立即(NO rebuild)
```
play flags = `slot+664 | v13`. slot+664 = mergeFrameContent 写的 PSB `motion.flags`(slot[86]@节点基址+344). v13 = parameterEntry(node+8 或 player+376)->+48 mode(常 0).

**buildNodeTree case-3@0x6B43C0**: 0x6b43c0 operator new(0x568) **无条件**新建 child Player，不复用 node+1912 旧值，不在此 play。child player 仅构造+存 node+1912。

**首帧深化但收敛的原因**: 自引用 motion 名稳定(grandchild 的 slot+348 motion 路径 == 它已加载到 +976 的 motion)。第 1 次 play 进某层 nested player 时 +976=空→gate 取→buildNodeTree(深一层)。该层 progress+updateLayers+childMotionPass 再 play 更深一层 nested player——**但更深那层的 motion 与刚 build 的相同且 +976 已被 loadMotion 填**，加之 motion.flags 通常不含 Force(1)/AsCan(4)，故 dedup gate 不取→return，不再 build 更深→**链在有限深度收敛**。frida build=28 即树规模有限、稳态后每帧 dedup 全 no-op(Player_play 调用率≈0)。

**端口结论**: 不要改 childMotionPass 的门(它就该 play)。应核对 port 的 Player_play/playImpl **same-motion dedup gate**(flags&5 + curMotion 比较)是否存在且语义一致；port 28000 无限多半是每帧 fresh rebuild(dedup gate 缺失/比较失败)或 motion.flags 误带 Force/AsCan。

证据: childMotionPass 0x6BE0C0(gate 0x6be31c/0x6be368/0x6be37c/play 0x6be46c)/progress_inner 0x6C106C(firstFrame 0x6c10c4 return reseek)/initNodeTimeline 0x6B64AC(node+44=1 @0x6b66f8)/parseFrame 0x6926B4(slot+24 @0x692828/0x69280c)/updateLayers 0x6BB33C(node+44 clear @0x6bbd2c after childMotionPass 0x6bbc90)/playImpl 0x6b22ac gate/buildNodeTree 0x6B43C0(new 0x568 @0x6b43c0)/mergeFrameContent 0x692ce4(motion.flags slot[86] mask&1)
