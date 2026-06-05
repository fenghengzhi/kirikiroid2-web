---
name: m1-framestep-rootstream-verdict
description: M1 帧步进/frameProgress/timeline fresh-decompile 复核(2026-06-06,复核最新3提交 3ab1685/8883587/8393dfa);frameProgress 入口拓扑 1:1✅;LABEL_48 forward/reverse loop-wrap 字节级✅;5 open gap(off-by-one 全node-walk/firstFrame +609 branch/initNodeTimeline tail 已实装✅/pruneHM3 tail/+482 initEmoteMotion)
metadata:
  type: project
---

2026-06-06 fresh-decompile 复核 M1 frameProgress/timeline(重点验证最新3提交 3ab1685 "入口1:1+删maxTF" / 8883587 / 8393dfa)。本轮 fresh decompile: 0x6C106C / 0x6B86C8 / 0x6B6ADC / 0x6B64AC。**不信任前 session 注释。**

**最新3提交裁决 — frameProgress 入口拓扑重构 = ✅ 正确，无新偏差/无 port-invention:**
- 入口无条件副作用(0x6C1080-0x6C10AC)忠实: +483=0(line1946)/+592=speedMul*dt(line2062)/preProgressDirtyNodes(line1964)。删 port-invented `if(!_speed)return` 正确(二进制入口无 play/null 守卫,fresh 复核确认)。
- loc_6C10E4 入口门控 1:1: `if(!_firstFrame && !_allplaying)`(line1970)=`if(!+481&&!+1099)`(0x6C10F0)✅; renderList 空→return via `_nodes.empty()`(line1985)=`if(+384==+392)return`(0x6C1278)✅; syncWaiting(line1997)/motionCompleted(line2000)短路=0x6C10FC✅。
- maxTF 删除 = ✅ 正确。二进制 progress_inner 5读0写 +1136,never mutate;+1128/+1136 唯一成对 writer = initNonEmoteMotion @0x6B370C/0x6B372C。maxTF 单覆盖 +1128 破坏 loopTime<lastTime 不变量,确是 forward loop-wrap 空转死循环根因。
- LABEL_48 字节级✅: gated clamp(0x6C1340 line2176)/forward at-end loop(0x6C13F0 line2198,do-while v7+=+1136-+1128 line2228)/reverse 3-branch(0x6C1360 line2247,LABEL_57/reset-0/loop-wrap line2261)全部与二进制 1:1,含 0x6C1488/0x6C1428 两 reseek 点。

**容器/字段映射 ✅**: _nodes=deque(idx0=root index0/parent-1,见 ensureRootNodeLike_0x6CED30 RuntimeSupport.cpp:1192),1:1 binary node-deque。_firstFrame(+481)/_queuing(+480)/_allplaying(+1099)/_reverseSeekFlag(+609) 字段齐备(Player.h:1147/1100/1104/1155)。

**initNodeTimeline tail per-node action push 已实装 ✅(纠正前 R-4):** fresh 0x6B64AC tail @0x6B674C `if(v8==*(node+328) && (*(node+342)&4)) pushAction(player,*(node+0)=label,node+608)`. 本地 initializeNodeTimelineSlotsLike_0x6B64AC(PlayerUpdateLayerEval.cpp:332-339)已忠实复刻(selectionTime==slot0.clipStartTime && !slot0.action.empty → push {0,label,action})。已 wired 进 reseekNodeTimelineSlotsLike_0x6B91B0(:784) + preProgressDirtyNodes(:824)。R-4 CLOSED。

**5 OPEN GAP(诚实清单,按严重度):**
- G-OByOne(中,REAL,范围比前 R-2 记录大): 三个 node-walk 循环全部 `i<_nodes.size()`(1..size-1) vs 二进制全部 `m<count-1`(1..count-2,跳过末节点)。fresh 三处全证: progress_inner walk 0x6C1288(`-1<=j`return)/advanceRootAndNodes LABEL_86 0x6B7398(`-1<=k`)/reseek node-loop 0x6B91B0(`-1<=m`)。本地 progressSeekNodeSlotsLike(PlayerUpdateLayerEval.cpp:700)+reseekNodeTimelineSlotsLike(:776)+preProgressDirtyNodes(:805)全 off-by-one。≥2 child 末节点带渲染内容时真实多步进1节点。非 logo-observable(0-mismatch)但全 node-walk 子系统系统性偏离,非仅 reseek。
- G-FF609(中,REAL): frameProgress firstFrame 分支(line2022 `if(_queuing){reseekTimelineCursors(_clampedEvalTime);return}`)是二进制 0x6C10E4 firstFrame 块的**坍缩**,丢两段: (a)0x6C1130 +609 reverseSeekFlag 分支(set 时 reseek-to-0 forward / reseek-to-+1128 reverse + advance/rewindRootAndNodes,本地 blanket reseek-to-当前+456 不复现方向 seed); (b)0x6C1120 `v8<0&&+1120==0` reverse-from-end 种子(+456=+1128;+1120=+1128)。且本地 RETURN 而二进制 fall-through 到 LABEL_48(注释 line2010-2017 用 +480 gate 论 net-equiv,但只覆盖非-609 路径)。_reverseSeekFlag 确被写(PlayerUpdateChildMotion.cpp:182,child reverse seek),故 child 反向 motion 可达。非 logo-observable。
- G-Prune(低,DEFERRED): reseekTimelineCursors STEP5 tail 0x6B9234 pruneHM3_byNodeIdentity + 0x6B9650 player+280 aux-list 仍 DEFERRED(PlayerFrameProgress.cpp:1720-1724)。无 live HM3-per-node consumer,但按 CLAUDE.md 应复原。
- G-EmoteInit(低,inert): 二进制 entry 0x6C10A4 `if(+482 emoteMode) initEmoteMotion(2)` 本地 frameProgress 无此调用。+482==0(非 emote-edit)时 inert。
- G-1152(低,未建模字段): 二进制 entry 0x6C1088 `*(DWORD)(+1152)=0`,本地 Player 类无 +1152 字段(grep 确认 +1152 全是 EmoteEngine._windFreqY,异对象)。不臆造,标注缺口。

**reseekTimelineCursors body 复核 ✅**: STEP1 layer coarse double-inc scan(0x6B8770,line1541)/STEP2 root single-step(0x6B8C1C,line1652)/STEP3 var-track reseed(reuse reseedVariableTracksLike,0x6B8F30)/STEP4 node-init loop(reseekNodeTimelineSlotsLike→initNodeTimeline per node)全部结构对齐;layer 段 int-trunc curTime/nextTime(line1570/1575)+gate(+920==+456 precise,line1579)与 0x6B8AC0 一致。

**六维评分**: 源码结构🟡7(per-stream split 已知架构偏离)/数据流✅9/调用链✅9/生命周期✅9/容器✅8(off-by-one)/边界🟡8(firstFrame +609 坍缩)。frameProgress 入口+LABEL_48 高度对齐(最新3提交质量好),主遗留=系统性 node-walk off-by-one + firstFrame 坍缩 + 3 inert tail。优先 G-OByOne(影响最广)。
