---
name: dracu-childmotion-recursion-0x6BE0C0
description: DRACU title 同步无限递归。终止器=playImpl dedup gate(0x6b22ac)+firstFrame一次性(+481)+child复用,非"fresh节点延迟seek"(该方向五轮反编译后推翻)。childMotionPass三门全忠实勿改。残留缺口=首帧单帧栈内同步深化为何不爆栈无单点反编译证据
metadata:
  type: project
---

DRACU-RIOT 标题 `progress(0)` 单帧栈内同步无限递归(Application::Run 永不返回,RUNDIAG 实证)。自引用节点 bg@nodeIdx12(type-3, src='motion/char/show', frameList=[0:type2@time0, 1:type0@time71])。

## 【2026-06-21 第五轮:修复方向根本性纠正——推翻"fresh节点延迟seek"】
前数轮(含本 memory 旧版)主张"二进制对 freshly-built type-3 子节点延迟 seek,保留 build-time slot.done=1"。**五轮深度反编译后此方向被证伪**:
- 反编译实证(ida agent a56ba758):fresh-just-played child 首帧 progress_inner **一定调 reseekTimelineCursors**(+376!=0 走 0x6C10E0 / +376==0 走 0x6C1318,两条都 reseek),reseek 尾循环 0x6B91B0 对 child 每个 idx>=1 节点(含 type-3 孙节点 N')**无 gate**调 initNodeTimeline→parseFrame(slot0,frame0=type2)→ **N'+344=0**(type2 写 0,非 1)。**二进制首帧也把孙节点刷成 done=0,不存在"延迟 seek"。**
- **字段误称纠正**:`node+344`(slot+24)不是 "done",是 **invisibleFlag**。parseFrame@0x6926B4: type==0→1(0x692828)/type==2,3→0(0x69280c);build-time→1(0x6B4BE8 唯一非帧写者)。"done"诱导误以为 type2 写它会"完成节点",制造了不存在的语义。
- **层级混淆纠正(致命)**:`childMotionPass(result)` result=当前 Player,遍历**自己**的 node-deque 读**自己的** type-3 节点 +344。P.childMotionPass 读 N(P 的节点)定 child C;C.childMotionPass(在 LABEL_18 的 C.updateLayers 里)读 N'(C 的节点)定 grandchild C'。前轮把 N' 在 C 首帧被刷 0(成立)与 frida 稳态观测 done=1 当成同一时刻,制造伪矛盾。

## 真终止器=三机制时序耦合(0x6b22ac dedup gate 为核心)
1. **playImpl same-motion dedup gate @0x6b22ac**: `(flags&5 Force|AsCan)||(curMotion +976/+984 != newMotion by 变体type+wcscmp名)` 才 loadMotion+buildNodeTree;否则 return **不 rebuild 也不 re-init**。这是真正递归终止器。
2. **firstFrame 一次性(+481)**: dedup gate return 时不重跑 initNonEmoteMotion → child+481 不重设 → child 过首帧 → 下帧 progress 走增量 LABEL_48(advanceRootAndNodes @0x6C1330)**不再 reseek 孙节点** → N'+344 维持最后值 → grandchild destroy 后该支不再被 advance 触达 → 稳定,destroy=165 不爆。
3. **build-time N'+344=1 + parseFrame invisibleFlag 语义** = childMotionPass gate A(0x6BE31C)读 1→DESTROY/0→play 的前提。
**childMotionPass 三门(gate A 0x6BE31C / gate B 0x6BE368 src dispatch / re-play gate 0x6BE37C `(v13&5)||node+44`)一个都不能改——本来就该每帧 play。** 前轮"对齐 childMotionPass gate"方向错。

## 端口偏差(确认,但落点是跨函数架构)
- 端口 onFindMotion dedup gate(PlayerMotionLoad.cpp:58 `_activeMotion && _motionKey==name && !(flags&Force|AsCan)`)结构忠实,但**仅 2/852 命中**(Android builds=28=gate 几乎全命中复用)。不命中根因=child player 每帧是 fresh(_motionKey 空/_activeMotion null)→ dedup 永不成立 → 每帧 buildNodeTree(PlayerMotionLoad.cpp:421 先 reset 再 NodeTree.cpp:289 无条件 new Player)→ 整树重建 + 每帧 firstFrame=1 → 每帧 reseek 孙节点刷 done=0 → 每帧深化 → 28000/1.9GB。
- child 每帧 fresh 的根=**单帧栈内同步深化**(非跨帧):LABEL_18(PlayerUpdateChildMotion.cpp:668-669)同步 `child.frameProgress(_deltaTime)+child.updateLayers()`→ child.childMotionPass→play grandchild→build→LABEL_18→... 在同一 progress(0) 调用栈内深化,从不返回下一帧,故 dedup gate(跨帧才有意义)无机会生效。与 project_dracu_title memory TAPROOT(RUNDIAG Run 永不返回)一致。

## 残留未闭合缺口(五轮后仍缺,BLOCKING,user 终验)
**首帧单帧栈内同步深化,二进制为何不爆栈?** 所有五轮分析都未给出二进制单点终止证据:
- 二进制首帧 child.progress_inner 也 reseek 孙节点 N'→done=0(frame0 type2,selectionTime=child+456=fmin(lastTime,0)=0 落 frame0)→ child.childMotionPass 读 N'.done=0 → 理应 play grandchild → 同步深化。
- 唯一能止住单帧深化的是某层 N'.done 被 reseek 成 1(type0 帧),仅当 selectionTime 落 type0@71 帧;但 child+456=0 恒落 frame0(type2)。
- "dedup gate 跨帧生效"只解释稳态,**不解释单帧栈内为何不无限深化**(跨帧 gate 在单帧栈内不适用)。
- 可能的真相(未证):二进制首帧 progress(0) 确实只深化有限层(dt 真实驱动让某层 child+456 推进到 71→孙 done=1→destroy),即 DRACU 标题二进制拿到的是**真实 dt 非 0**(而端口 TAPROOT=dt=0 冻结)。若如此,根因仍是上游 driver dt=0(project_dracu_title memory line 71-77),childMotionPass/seek/build 全忠实,无 motion-side 单点 fix。需 frida 在二进制 DRACU 首帧 hook child+456/孙 done 逐层值确认深化是否有限 + dt 是否非 0。

## 关键地址
playImpl dedup gate 0x6b22ac;progress_inner 0x6C106C(reseek 分支 0x6C10E0/0x6C1318,增量 LABEL_48 0x6C1330);reseekTimelineCursors 0x6B86C8(尾循环 0x6B91B0 无 gate);initNodeTimeline 0x6B64AC(parseFrame 0x6B66AC,clamp v19=min(scan,count-2) 0x6B6698);parseFrame 0x6926B4(+344 写 0x692828=1/0x69280c=0,type key 0x692800 默认 dword_1AB8124);buildNodeTree 0x6B4BE8(build +344/+880=1);childMotionPass 0x6BE0C0(gate A 0x6BE31C/DESTROY 0x6BE328/play 0x6BE360/LABEL_18 0x6BE2A4);advanceNodeFrames 0x6B7E44(ping-pong +1392 @0x6b7f40/0x6b7f98,count=2 两循环均不进=不翻);node+1392 写点仅 4(0x6b66f4=0/0x6b7f40/0x6b7f98/build calloc 0)。

## 端口落点(若 user 终验确认 dt 非 0 是根,则以下全是症状不需改)
PlayerUpdateChildMotion.cpp(childMotionPass 三门忠实,LABEL_18:668-669);PlayerMotionLoad.cpp(onFindMotion dedup:58,buildNodeTree:391/421);NodeTree.cpp:289(child new);PlayerFrameProgress.cpp:2233(progress_inner 入口 gate,`_nodes.empty()` 替代二进制 renderList 空 +384==+392——注:此替代对 fresh-未-play Player 不等价但实务无影响,因能进 progress_inner 的 child 都已被同帧 play)。**高敏感区 m2logo CI,任何改动须 yuzulogo=243/m2logo=93 wasmtime 差分零回归。**

## 已证伪/已纠正记录(勿复开)
- "fresh 节点延迟 seek / build-time done=1 保留到下帧"——证伪(首帧 reseek 必刷 N'+344=0)。
- "node+344=done,type2 写它=节点完成"——证伪(是 invisibleFlag,type2 写 0)。
- "progress_inner _nodes.empty() vs renderList 空是递归根因"——证伪(能进 progress_inner 的 child 已 play,不命中该 gate)。
- "childMotionPass gate / activeSlotIndex ping-pong 缺失是落点"——证伪(端口 ping-pong 在位 PlayerUpdateLayerEval.cpp:497/509;count=2 两循环均不进,与二进制一致)。
- "markNodePayloadDirtyFromState(端口独有 node+44 脏通道)"——已删(正确对齐,零 logo 回归保留),但不足以修 DRACU(实测仍每 ~12s 增长)。

相关:[[project_dracu_title_blank_and_player_leak]](TAPROOT=dt=0/Run 永不返回) [[sub6C7440_setclip_dispatch]] [[progress_inner_firstframe_block]]
