---
name: m1-framestep-rootstream-verdict
description: M1 帧步进历史裁决；R-1/R-2 已关闭，2026-07-19 helper 边界已方向拆分；不得外推为整个 psbfile/motionplayer 无缺口
metadata:
  type: project
---

2026-06-07 READ-ONLY 全面复核(fresh decompile 14 函数: 0x6C106C/0x6B6ADC/0x6B9A3C/0x6B86C8/0x6B7E44/0x6B64AC/0x6B9650/0x6B531C/0x6B1ECC/0x6C4668)。**SUPERSEDES 06-05 与 06-06 verdict 的 R-1/R-2/G-OByOne 三条 open gap —— 全部 FALSIFIED。**

**CURRENT CORRECTION 2026-07-19:** R-1 的“反向 root 已存在”结论仍成立，但本地
实现名已从合并的 `seekRootContentStreamLike_0x6B6ADC` 改为独立的
`rewindRootContentStreamLike_0x6B9A3C`，以复原二进制函数边界。旧符号引用仅是历史。
本文件末尾“无 REAL open 缺口”也只覆盖当时帧步进审计范围，不能用于宣称整个
psbfile/motionplayer 已 100% 复原；当前仍有 snapshot 兼容状态与
2026-07-19 fresh decompile 0x6B9A3C/0x6B7E44/0x6B64AC/0x6B86C8 后确认
`progressSeekNodeSlotsLike_0x6C106C` 的反向 node-layer 帧槽路径已闭合；旧“未闭合”结论
是实现更新后未同步的陈旧记录。反向四流、node+8 分流、0x6BA1CC 单向后退与 reseek
全节点双槽播种均已接线；同时删除共享 seek 中二进制不存在的 lazy seed 兜底。
`initPhysics` 已于 2026-07-19 由 `0x67FAC8` 注册证据纠正：它直接绑定已完成的
`EmoteEngine_applyMetadata_buildControllers@0x67D4D0`，不再是 open physics 项。

**★ 两条旧 open gap 经独立交叉核实证伪(强断言纠正):**

1. **R-2 / G-OByOne「node-walk off-by-one」= FALSIFIED。** 所有 4 个 node-walk(progress_inner 0x6C1230/0x6C12D8、advanceRootAndNodes 0x6B7398、reseekTimelineCursors 0x6B9200、preProgressDirtyNodes 0x6B6920、sub_6B9650 0x6B97F0)的退出表达式 `[deque-size-expr] - 1 <= idx` 中那个 `-1` **不是** source `size()-1`、**不是** trailing sentinel——它抵消 libstdc++ `std::deque::size()` 内联对 >512B 元素(MotionNode=2632B → 1-elem/block)产生的 **+1 BIAS**(size 用 `(start.last-start.cur)/T` 算出 = realSize+1;magic 0xE719AD850EC8C0F9=329⁻¹/+1、0x18E6527AF1373F07=-1/329,329=2632/8)。证据 = **0x6B531C(buildNodeTree)的自反汇编 IDA 注释**:deque 恰好 realNodeCount 个元素(root+N children,**无 sentinel**),`dequeSize-1 == real size()`,循环跑 `idx∈[1,realSize-1]` = 全部非-root 节点。本地 `for(i=1; i<_nodes.size(); ++i)` **就是忠实 source**。运行时反证(注释 0x6B531C/PlayerUpdateLayerEval.cpp:780):`i+1<size()` 会丢末节点 → yuzulogo 468 mismatch;`i<size()` byte-exact。这正是 CLAUDE.md 警告的 deque size()展开陷阱。**前两次(R-2 off-by-one、更早的 trailing-sentinel)都只看消费循环上界没看构造点,连错两次,本轮按 CLAUDE.md 看构造点(0x6B531C)定论。**

2. **R-1「rewindRootAndNodes 反向 root 段 0x6B9E84 缺失」= FALSIFIED。** 0x6B9A3C @0x6B9E84 确有反向 root content-snapshot while 循环(gate `+576>+456`;`--(+568)`;`+616=priority[cur].content`;`+584=+576`;`+576=priority[cur].time`)。本地 **已实装**:seekRootContentStreamLike_0x6B6ADC(PlayerFrameProgress.cpp:1061-1068)reverse 分支字节对齐(注释亦记 R-B1 closed)。旧 verdict「注释1022证伪 = 缺失」本身才是过时——1022 注释说的是「entry 不重算 +576/+584」(正确,二进制 incremental),与反向段存在不矛盾。

**frameProgress 入口拓扑 = ✅ 1:1(本轮重新逐行核对 0x6C106C):**
- 入口副作用 0x6C1080-0x6C10AC:speedMul读 / +1152=0(本地无字段,标注缺口非遗漏)/ +483=0 / +592=speedMul*dt / if(+482)initEmoteMotion(2)(本地缺,+482==0 时 inert)/ preProgressDirtyNodes。✅(2 inert 缺口,见下)
- +376 activeTimeline 分支（CORRECTION 2026-07-13）：旧结论“本地无 +376 字段、恒走 0 路径”错误。`_defaultParameterEntryPtr` 正是 Player+376 的语义等价字段，且由 0x6B365C 对应的 parameterize dictionary/index 路径赋值。`PlayerFrameProgress.cpp` 现按 0x6C106C 复刻 entry.value 的 first-frame seed、前进、后退及等值 parameterized-node refresh；遗漏该分支曾使标题按钮在 select=0 时仍随 dt 进入 over 帧。
- loc_6C10E4 门控 `if(!+481 && !+1099)` → renderList 空检查(本地 _nodes.empty)→ 非空 node-walk → return。✅ 含 firstFrame(+481)块完整复刻:(b)0x6C1120 reverse-from-end 种子 + (a)0x6C1130 +609 reverseSeekFlag 方向 seek(forward seek-to-0 / reverse seek-to-+1128)+ else plain reseek,块末 fall-through(本地 return,论证为 LABEL_48 gated no-op 等价)。✅ 此前坍缩(旧 G-FF609)已修复。
- LABEL_48 字节级✅:gated clamp(0x6C1340)/forward at-end loop-wrap do-while(0x6C14C4 `v7+=+1136-+1128`)/reverse 3-branch(LABEL_57/reset-0/loop-wrap 0x6C1454)/两 reseek 点 0x6C1488(fwd)+0x6C1428(rev)全 1:1。

**最近 5 commit 移植裁决(be77533/c640347/bb85bac/5fc6169/e8e4499)= ✅ 忠实,非过度简化/打补丁:**
- **be77533 Player+408 param-ramp → std::multimap**:finalizeParameterTableLike_0x6B1ECC(PlayerVariable.cpp)逐 entry emplace(widen(id),&entry);applyParameterRampsLike_0x6C4C0C equal_range+normalize。对齐 sub_6B1ECC(RB-tree leaf insert sub_6F16AC,dup id 保留)+ 0x6C4C0C(sub_6F2F98 equal_range,ramp 公式 `val=duration*(clampedRaw-min)/range` + mode写+48)。**纠正 2026-07-13**：finalize 会把子 Player 的 entry 指针注册到整条 parent 链，不能仅靠 vector reserve 保证生命周期；Player_dtor@0x6CFADC 首先调用 0x6CDE18，从当前/祖先 +408 map 删除 value==该 entry 的节点。本地现由 purgeParameterRampMapLike_0x6CDE18 复刻。
- **c640347 sub_6B9650 heapResult builder + reseek STEP5(B)接通**:rebuildEvalCascadeHeapResultLike_0x6B9650(PlayerVariable.cpp:315)对齐 sub_6B9650:weight==0 gate→return / weight清0 / heapResult.clear / scan node[1..size-1] type∈{3,4} / 祖先链(parentIndex 爬升,node+36)与 chainSegments 比对(truncate 窗口=ref.size,frozen-ref 重比是二进制 artifact 忠实复刻)/ 匹配 push &node。consumer = bindParameterValueLike_0x6C4668(:461)@0x6C4978 逐 heapResult 节点 type4(PropGet particle child)/type3(PropGet child Player member 200)→ child+408 equal_range ramp by SUFFIX。✅ 全字节核对 0x6C4668。
- **bb85bac pruneHM3 loop2 + node+46=joinTarget + loop3 gate**:pruneHM3ByNodeIdentityLike_0x6B826C(PlayerFrameProgress.cpp:1690)loop1(HM4→active slot.value gate typeZeroFlag)+ loop2(HM3 per-node restore gate joinTarget+nodeType match,erase matched)+ 终末 clearHM3/HM4。resetMotionStateLike_0x6B2D3C(:1853)loop3 gate 顺序 joinTarget FIRST(0x6b2dcc)→ nodeType mask 0x19D(0x6b2df8)。✅
- **5fc6169/e8e4499 A 类 restore 子路径 contentMask/type-3 child/mesh/type-4 粒子链**:hm3InitValueFromNodeLike_0x699510(:1905)+ hm3RestoreValueToNodeLike_0x6997F0(:1989)按 0x699510/0x6997F0 字节布局复刻(mesh FIRST gate node+2000==1、type-3 V+544 move+clear、type-4 V+672+doneFlag early-return+V+600..664 particleInterp、common block gate `!slot+344 && !V+32`、slantX(slot+152)忠实跳过只写 slantY)。✅

**reseekTimelineCursors body 复核 ✅(0x6B86C8)**:STEP1 layer coarse DOUBLE-INCREMENT scan(0x6B8770,for++i + body++i on time<target)+ min(i,count-2) + INT-TRUNC curTime/nextTime(`(double)(int)`,0x6B891C/0x6B89D0)+ gate `+920==+456 && type==1`(0x6B8AC0,精确帧 vs advance-form 的 +1093-only)→ 本地 reseekTimelineCursors(:1405) STEP1(:1470-1546)逐行对齐含 int-trunc。STEP2 root single-step + min(cursor,count-2) + content snapshot(:1552)。STEP3 var-track reseed(reseedVariableTracksLike,UNCONDITIONAL merge slot[0]=v41/slot[1]=v41+1,无 !merged gate,0x6B8F30)✅。STEP4 node-init loop 0x6B91B0 → reseekNodeTimelineSlotsLike(:863,per-node initNodeTimeline + tail action push)✅。STEP5 tail pruneHM3 + sub_6B9650 per-HM1-entry heapResult rebuild(:1685)✅。

**initNodeTimeline tail per-node action push 已实装 ✅(0x6B64AC @0x6B674C)**:`if(v8==*(node+328) && (*(node+342)&4)) pushAction(player,label=*(node+0),node+608)`。本地 initializeNodeTimelineSlotsLike_0x6B64AC(:333)tail(:378-385)`if(selectionTime==slot0.clipStartTime && !slot0.action.empty) push {0,layerName,action}`。wired 进 reseekNodeTimelineSlots(:873)+preProgressDirtyNodes(:922)。✅

**var-track 三流 ✅**:advance(0x6B7124,merge slot[0]两次)/rewind(0x6B9FCC,merge slot[0]+slot[1]非对称)/reseed(0x6B8F30,无 toggle 双 slot fresh)step=sub_6B786C/merge=sub_6B7A70 字节对齐(PlayerFrameProgress.cpp:1071/1173/1274)。

**advanceNodeFrames(0x6B7E44)✅**:seek target=*(node+8)+40=parameterEntry->value(CONFIRMED 经 initNodeTimeline 0x6B6500 交叉核实)。本地 advanceNodeFramesLike→advanceNodeFrameSelectionLike(node+8 split:param→无事件,非param→inline seek 带事件)。forward break `cur.fi>=count-2 || target<other.time`、corrective-backward、tail(node+44=1 / 2×mergeFrameContent gate node+346/+882 / findSource gate node+1996|mask)全对齐。

**OPEN（旧清单已被后续 raw-frame/source 复原部分取代）:**
- O-1(低,inert):entry 0x6C10A4 `if(+482)initEmoteMotion(2)` 本地 frameProgress 无。+482==0(非 emote-edit)恒 inert。
- O-2(低,未建模字段):entry 0x6C1088 `*(DWORD)(+1152)=0` 本地 Player 无 +1152 字段(grep 确认 +1152 全是 EmoteEngine._windFreqY,异对象)。不臆造,标注。
- O-3 CORRECTED/DONE 2026-07-18: live ClipSlot 已保存 raw src/icon ttstr/variant，
  pruneHM3 的 findSource 调用链可进入真实 resolver；RM mapped record 的 Win/KRKR
  纹理表也已复原。Win/spec=2 与 KRKR/spec=1 均已走 raw PSB；source 像素导航不再是帧步进的 open gap。
- O-4(低,DEFERRED 但已记):sub_697D34 chainDispatches build(0x6c48bc,bindParameterValue HM1 首插)纯 TJS-dispatch scope 解析,无 port consumer(getVariable 读 HM2 非 HM1 链)。本地 chainSegments 用 splitScopeSegmentsLike 建(:513),仅 dispatch 句柄那半 DEFERRED。
- O-5 CORRECTED/DONE 2026-07-12: 0x671764 的 this 是 EmoteEngine（0x67D060: X0=engine/W1=0/V0=original dt），不是 Player。frameProgress 错位调用已删除，EmoteEngine::progress 调用已恢复；DRACU START 仍卡死，故另有第二触发点。

**六维评分(2026-06-07)**:源码结构✅9(per-stream split 是已知有意架构 + DEAD FrameStepping 重复,但活路径忠实)/数据流✅9/调用链✅9(O-1/O-5 两处错位/缺调用)/生命周期✅9/容器✅9(multimap/deque/unordered_map 选型全对齐,off-by-one 证伪)/边界✅9(firstFrame +609 已修,layer int-trunc gate 对齐)。**结论:帧步进/timeline/progress 子系统高度 1:1,无 REAL open 缺口;仅 5 条 inert/平台边界/已知 blend。最近 5 commit 移植质量优秀。**
