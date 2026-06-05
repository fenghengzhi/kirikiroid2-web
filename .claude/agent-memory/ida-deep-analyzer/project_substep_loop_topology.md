---
name: substep-loop-topology
description: fmin(dt,1.1) 子步进循环只属于 EmoteEngine_progress@0x67D01C, NOT Player_progress_inner@0x6C106C; 两条 progress 链分属不同类
metadata:
  type: project
---

`while(dt>0){ slice=fmin(dt,1.1); ...; dt-=slice }` 子步进循环**只**存在于 `EmoteEngine_progress @0x67D01C`(EmotePlayer/D3DEmotePlayer 类的 NCB "progress"), 循环段 @0x67d0a4..0x67d370。

`Player_progress_inner @0x6C106C`(MotionPlayer/Player 类)**没有任何子步进循环**: @0x6c1090 一次 `FMUL +592 = speedMul*dtFrames`, dt 一次性消费, 无 fmin / 无 1.1。

两条 progress 入口完全不同的类:
- 非 emote: `Player.progress` NCB = `Player_progressCompat @0x6D2A98`(注册于 Player_ncb_registerMembers@0x6d69c8) → `Player_progress_inner @0x6C106C`(一次, dt=ms*60/1000)。**不经过任何 fmin 循环。**
- emote: `EmotePlayer.progress` NCB = `EmoteEngine_progress @0x67D01C`(注册于 D3DEmotePlayer_ncb_registerMembers@0x52e504) → 子步进循环, 每个 slice step 7 个 controller deque, 循环外再调 `sub_6D2A54`(@0x67d408) → `Player_progress_inner` 恰好一次(传完整 v12, 非 slice)。

关键: EmoteEngine_progress 的子步进每次迭代 **有副作用累积** —— slice 推进 controller 时间(如 @0x67d2d4 `*((float*)*v52+1) += slice`, 物理 controller 累加相位; 各 controller step(slice) 内部按 slice 推进时间状态写 HM2@+1440)。不是无意义重复。本地 PlayerFrameProgress.cpp:2038-2043 的循环体 refreshFixed...() 不消费 controllerDt = 双重 bug: (a)拓扑放错(应只属 EmoteEngine::progress)(b)循环体不按 slice 推进。

**应用方式**: 凡涉及 frameProgress / progress_inner 子步进, 二进制证据是 progress_inner 一次性消费 dt; 千恋万花标题卡死若栈顶在 frameProgress 的子步进 while, 该循环是本地误植, 不是二进制行为。
