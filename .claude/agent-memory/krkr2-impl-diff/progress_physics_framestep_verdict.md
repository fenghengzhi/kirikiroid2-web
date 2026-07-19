---
name: progress-physics-framestep-verdict
description: 2026-06-03 fresh-decompile 裁决 progress/弹簧/帧步进；纠正"var-track 统一DEFERRED"与"弹簧是stub"两个误判；地址映射
metadata:
  type: project
---

2026-06-03 fresh-decompile 复核 motionplayer progress/frame-stepping/弹簧物理三子系统裁决。

**关键纠正（证伪既有注释/认知）：**
1. var-track 三流**非统一 DEFERRED**。前向已实装于 PlayerFrameProgress.cpp:1030 `Player::advanceVariableTracksLike_0x6B6ADC` + :1132 interpolateVarTrackValuesLike_0x6BBE20(bezier 0x69A754)。但 PlayerFrameStepping.cpp:272 `detail::advanceRootAndNodesLike_0x6B6ADC` 是**重复 port** 且其中 var-track 漏接(:318 DEFERRED 注释过时)。
2. 弹簧物理**非 stub**——stepBust(0x67BCE8)/stepHairParts(0x67B748)/springStep(0x662768)/chainStep(0x6689A4) 均逐行 port(EmoteEngine.cpp:416/521, EmoteSpring.cpp:31/120)。logo inert 仅因 deque 空(无 metadata)+target/const 未喂，非物理缺失。按 CLAUDE.md 须保留。
3. 0x530A5C 是 18B thunk 内联到 0x67D01C(EmoteEngine_progress)，同一函数。

**两条独立调用链(任务曾混为一谈):**
- A EmoteEngine 物理: NCB@0x52E504 -> progress thunk 0x530A5C -> 体 0x67D01C -> {3×EmoteVarController_step, stepHairParts, 2×stepBust(a4=springConst +1184/+1192, a3=chain deque engine+80/+160)}. EmoteEngine 偏移+1104/+1112/+1120=physics controller target。
- B Player M1 帧步进: Player_progress_inner 0x6C106C -> {firstFrame:reseek 0x6B86C8 | fwd:advance 0x6B6ADC | rev:rewind 0x6B9A3C}. Player 偏移+1120=frameTickCount,+1128 totalFrames,+456 clampedEvalTime,+480 gate,+481 firstFrame,+609 reverseSeek,+1136 loopTime。与 A 的同名偏移纯巧合。

**reseek merge 序列陷阱:** advance@0x6B7178/0x6B71A0 = merge(+48) 两次(gate +70/+126 均 slot[0]); reseek@0x6B8F30.. = step+merge 各作用 +48 与 +104 两 slot。勿把 reseek 按 advance 双-slot[0] 形态实现。advanceVariableTracks(PlayerFrameProgress.cpp:1127-1128)正确=两次merge slot[0]。

**2026-07-18 收敛更正：** 下方 2026-06-03 `Open gaps` 是历史快照，不是当前状态。
G1/G2/G3/G6/G7 均已关闭；特别是 EmoteEngine bind 后处理已实装
`0x67C560` Engine HM3/+1040 timeline cascade、`0x67C6B0` raw mirror caches 和
`Player_bindParameterValue@0x6C4668`，不得再称为 stub。G4/G5 的最新裁决见后续
M1/HM3 memory，不从本条历史段落推导。

**2026-07-19 再纠正：** 下文 2026-06-04 的 “Stage B 活路径仍用 advance-form
代替 reseek-form” 已被后续实现证伪；live `reseekTimelineCursors@0x6B86C8` 已含
layer/root/var-track/node/tail。增量层/root helper 也已按前进与后退函数边界拆成四个，
不再是两个合并双向 helper。历史段落仅用于解释当时状态。

边界行为均 ✅: fmin(dt,1.1) 物理cap 三处全保留; LABEL_48 gated clamp; loop-wrap do-while; depth-ramp 常量 0.03125/28.0/6.28318531 + chain 0.015625/4.0/0.0451603944/0.0392699082 逐一核对。

**2026-06-03 M1 cluster READ-ONLY 二次复核 更正/确认:**
- 8139222 移除 per-frame HM2.clear = 忠实(已字节核实)。0x6C106C 入口: ldr+1168 / ldrb+482 / `str wzr,+1152`(DWORD) / `strb wzr,+483`(BYTE) / fmul→str+592。仅写 +1152/+483/+592,全函数从不写 4 个 HM(+264/+320/+1184/+1240)。HM2 跨帧持久确认。
- G1 误判更正: advance var-track(0x6B7124..0x6B71C8)在活路径 PlayerFrameProgress.cpp:1068 已实装且正确(slot[0] merge×2)。0x6B6ADC 反编译头注释"variable-track deque DEFERRED"是过时,实际有代码。G1 不是缺失,是"已实装"。
- G2 rewind 反向 var-track 确认缺(活路径): 0x6B9FCC..0x6BA034 反向循环 sub_6B786C(frameIdx-1)+merge slot[0]@+48 然后 slot[1]@+104(注意是 +104,不是 advance 的 +48 双打)。活路径 reverse 分支(:1582/:1606)调的是 FORWARD advanceVariableTracksLike,无反向步进。真实 gap。
- G3 reseek var-track 确认缺(活路径): 0x6B8F30.. = step(+48,v41)+merge(+48)+step(+104,v41+1)+merge(+104)+`*(+8)=0`。活路径所有 reseek 点(:1375 firstFrame / :1527 / :1586)只调 progressSeekNodeSlotsLike,无 var-track reseed。真实 gap。
- 三入口单位确认: 0x6D2A98(Motion.Player NCB) v10*60/1000→inner; 0x6D2A54(raw,引擎调)收 frame 直传; 0x6818B4(EmotePlayer)入口 a2*60/1000 后→sub_6D2A54(frame)。kMotionFramesPerMillisecond=60/1000 ✅。
- progressFramesLike 注释(:1692)误称中间 arg=pendingEvents cursor;实际 0x6D2A98 是 *(player+16)=a4(dispatch obj),dispatchEvents 回读它。inert 误注。
- PlayerFrameStepping.cpp = unit-test-only 重复 port(非活路径),其 var-track/reseek-tail DEFERRED 与活路径缺口无关,勿混为一谈(其 PlayerFrameStep.cpp parseFrame/merge 同理仅 unit-test)。

**2026-06-04 M1 cluster 三次复核(本轮 fresh-decompile 0x6C106C/0x6B6ADC/0x6B9A3C/0x6B86C8/0x6B786C/0x6B7A70/0x67D01C):**
- G2/G3 已关闭(更正上面"真实 gap"): 活路径 PlayerFrameProgress.cpp reverse 分支(:1857/:1884)已调 rewindVariableTracksLike_0x6B9A3C(反向步进,merge slot[0]@+48 然后 slot[1]@+104,与 0x6B9FCC 一致);所有 reseek 点(:1624 firstFrame/:1797/:1862)已调 reseedVariableTracksLike_0x6B86C8(step+merge 双-slot v41/v41+1,*(+8)=0,与 0x6B8F30 一致)。step/merge 字节级对齐 sub_6B786C(idx/+8 time/+22 merged=0)与 sub_6B7A70(+22 merged/+20 typeZero/+21 interp type2→0 type3→1/+4 interval/+3 value/easing copy)。
- **Stage B 状态(任务核心问题): 活路径用 ADVANCE-form layer/root seek,非 RESEEK-form。** 活 frameProgress 在 firstFrame + loop-wrap reseek 点(0x6C1488/0x6C1428 对应)调 seekLayerEventStreamLike(advance/rewind 双向 0x6B6B80/0x6B9AE8,gate=type==1 +1093-only,无 time-gate)而非 0x6B86C8 的 RESEEK-form layer scan(double-increment + min(i,count-2) + int-trunc curTime/nextTime + 精确帧 gate +920==+456 0x6B8AC0)。RESEEK-form 仅存在于 unit-test PlayerFrameStepping.cpp reseekTimelineCursorsLike_0x6B86C8。⇒ Stage B 在活路径 PARTIAL: var-track reseed ✅ 已接,但 layer/root 用 advance 语义代替 reseek 语义(精确帧 gate 差异)+ reseek-tail(initNodeTimeline 0x6B9228/pruneHM3 0x6B9234/+280 aux 0x6B9650)仍缺。
- 1.1s cap 归属确认: fmin(v14,1.1)@0x67D0B0 在 EmoteEngine_progress(0x67D01C)的 controller-step 外循环,**非** progress_inner(0x6C106C,后者一次性 +1120+=+592)。controller 桶序: deque#4(+256,16B)→#5(+336,16B)→#6(+416,24B,写 2 HM key i+1/i+2)→#8(+656,48B)→#7(+576,24B)→curve-walk(+736,16B)→applyVarControllers。活 frameProgress controller 循环只 5 桶(type4/5/6/8/7),缺第6桶(+736 curve-walk)+ applyVarControllers——属 M2/EmoteEngine 范畴,活 frameProgress 是 emote/non-emote 混合体(已知架构 blend,见代码注释)。
- 流序(advanceRootAndNodes 0x6B6ADC)确认: layer(+916)→root(+568,+616 content snapshot 无 event gate)→var-track deque(+1312)→node walk(LABEL_86)。活路径 inline 调用序一致(seekLayer→seekRoot→advanceVarTrack→progressSeekNodeSlots)✅。
