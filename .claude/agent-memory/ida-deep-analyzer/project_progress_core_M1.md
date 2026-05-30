---
name: Player progress core (M1 / Cluster G) field map + reseek discovery
description: progress_inner @0x6C106C byte-verified field types, the previously-undocumented reseekTimelineCursors @0x6B86C8, and the +1093 _speed mislabel correction
metadata:
  type: project
---

# Player progress core 帧步进机 (M1 / Cluster G)

权威 plan: analysis/Player_progress_frame_stepping_M1_plan.md

## 调用拓扑 (fresh decompile 2026-05-30)
progress_inner @0x6C106C 调用链:
- @entry: +483=0, +592=speedMul(+1168)*dt, if +482 emote→initEmoteMotion(2)
- Player_preProgressDirtyNodes @0x6B6878 (旧 sub_6B6878, **本轮重命名**) — 真正的 preProgress: 遍历 node-deque, node+1996 timelineDirty 门控, 读 node+1980 motion dict 'modified' key, 重建 initNodeTimeline. **不是 0x671764!**
- Player_reseekTimelineCursors @0x6B86C8 (旧 sub_6B86C8, **本轮重命名**, 此前完全未文档化) — 全量游标 re-seek 到 +456: layer 流(+1072→cursor +916/+920/+928 + action/sync/align gate @+1093)、root 流(+548→+568/+576/+584, content→+616)、variable-track deque(+1312..1368, 160B stride, 2-slot seed via sub_6B786C/sub_6B7A70)、per-node initNodeTimeline、Player_pruneHM3_byNodeIdentity、+280 list sub_6B9650. 在 firstFrame 种子和 loop wraparound 时调用.
- advance/rewindRootAndNodes, advanceNodeFrames (per-node 帧步进, 见 framesel memory)

注意: Player_preProgress @0x671764 是 **playing-list controller stepper** (a1[130..131] playing-list 上 EmoteVarController_step), 与 progress_inner 解耦. 本端 preProgressPlayingTimelinesLike_0x671764 接错了调用点.

## Byte-verified 字段类型 (a1=Player*)
- +480 progressFlags = **1-byte** (LDRB @0x6C1330, NOT 16-bit; clusterG "init 257" 存疑). LSB=1 时冻结 +1120 游标推进但仍跑 advanceRoot.
- +481 firstFrame 1-byte one-shot; +483 motionCompleted 1-byte (STRB WZR @entry 每帧清零, byte-verified @0x6C108C)
- +482 emoteMode 1-byte; +609 reverseSeekFlag 1-byte; +1093 motionStopGate 1-byte; +1098 syncWaiting; +1099 loopArmed
- +456 clampedEvalTime double = **min(+1120,+1128)** 标量 (NOT timeline-map lookup — 本端 G4 activeClipTime() 错误根因)
- +592 deltaTime double = +1168 speedMul * dt; +1120 frameTickCount, +1128 totalFrames, +1136 loopTime 全 double
- +1152 dword @entry 清零 (用途待定)

## 主推进逻辑 (LABEL_48 @0x6C1330)
if !+480: +1120 += +592; +456 = min(+1120, +1128)   ← G3/G4 真正逻辑
正向(d>=0) 到尾且 +1136 loopTime>=0 → loop wrap modulo; <0 → 停尾(+1099=0)
反向(d<0) 对称 rewind + wrap to head

## 关键勘误 (本端 Player.h)
- Player.h:665 `_speed` 注释 "Aligned to +1093: bool flag" 是**错的**. +1093 是 motionStopGate (action/sync/align 开关), 不是 speed. speed 倍率在 **+1168 (double)**, 本端无对应字段.

## 本端缺失 (整个 node-deque 帧步进核心)
reseekTimelineCursors / advanceNode/Root / rewindRoot / parseFrame(0x6926B4) / mergeFrameContent(0x692AB0) 在 frameProgress live path 中全部缺失, 被 STL _timelines + control-animator 状态机替代 (architecture-level divergence, 需分阶段 re-arch, 见 plan P1-P7).

## differential 回归网
- logo diff (motion_playback/{yuzulogo,m2logo}) = 全帧逐层 Motion 状态, 守护 live-path 改动 (plan P5/P6/P7)
- staged oracle motion_playback_stages/frame_selection 在 evaluateTimeline@0x699AE4.leave 每节点采样 {activeSlot,nodeType,flags,visible,opacity} = 精准游标回归网 (P3/P4/P6)
