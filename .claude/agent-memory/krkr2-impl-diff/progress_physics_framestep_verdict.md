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

**Open gaps:** G1 advance 路径漏接 var-track(与 Player 路径重复不一致); G2 rewind 反向 var-track 缺; G3 reseek var-track 双-slot 重播缺; G4 reseek 收尾 initNodeTimeline_guess(0x6B9228)/pruneHM3(0x6B9234)/+280 aux sub_6B9650(0x6B9248) 缺; G5 per-node mask&0x40000 action push sub_6B638C 缺(Stage A 盲区); G6 EmoteEngine bind 后处理 sub_67C560/67C6B0/bindParameterValue+sub_6687E8 stub; G7 Player+1296 var-track std::vector vs libstdc++ deque 选型偏离。

边界行为均 ✅: fmin(dt,1.1) 物理cap 三处全保留; LABEL_48 gated clamp; loop-wrap do-while; depth-ramp 常量 0.03125/28.0/6.28318531 + chain 0.015625/4.0/0.0451603944/0.0392699082 逐一核对。

**2026-06-03 M1 cluster READ-ONLY 二次复核 更正/确认:**
- 8139222 移除 per-frame HM2.clear = 忠实(已字节核实)。0x6C106C 入口: ldr+1168 / ldrb+482 / `str wzr,+1152`(DWORD) / `strb wzr,+483`(BYTE) / fmul→str+592。仅写 +1152/+483/+592,全函数从不写 4 个 HM(+264/+320/+1184/+1240)。HM2 跨帧持久确认。
- G1 误判更正: advance var-track(0x6B7124..0x6B71C8)在活路径 PlayerFrameProgress.cpp:1068 已实装且正确(slot[0] merge×2)。0x6B6ADC 反编译头注释"variable-track deque DEFERRED"是过时,实际有代码。G1 不是缺失,是"已实装"。
- G2 rewind 反向 var-track 确认缺(活路径): 0x6B9FCC..0x6BA034 反向循环 sub_6B786C(frameIdx-1)+merge slot[0]@+48 然后 slot[1]@+104(注意是 +104,不是 advance 的 +48 双打)。活路径 reverse 分支(:1582/:1606)调的是 FORWARD advanceVariableTracksLike,无反向步进。真实 gap。
- G3 reseek var-track 确认缺(活路径): 0x6B8F30.. = step(+48,v41)+merge(+48)+step(+104,v41+1)+merge(+104)+`*(+8)=0`。活路径所有 reseek 点(:1375 firstFrame / :1527 / :1586)只调 progressSeekNodeSlotsLike,无 var-track reseed。真实 gap。
- 三入口单位确认: 0x6D2A98(Motion.Player NCB) v10*60/1000→inner; 0x6D2A54(raw,引擎调)收 frame 直传; 0x6818B4(EmotePlayer)入口 a2*60/1000 后→sub_6D2A54(frame)。kMotionFramesPerMillisecond=60/1000 ✅。
- progressFramesLike 注释(:1692)误称中间 arg=pendingEvents cursor;实际 0x6D2A98 是 *(player+16)=a4(dispatch obj),dispatchEvents 回读它。inert 误注。
- PlayerFrameStepping.cpp = unit-test-only 重复 port(非活路径),其 var-track/reseek-tail DEFERRED 与活路径缺口无关,勿混为一谈(其 PlayerFrameStep.cpp parseFrame/merge 同理仅 unit-test)。
