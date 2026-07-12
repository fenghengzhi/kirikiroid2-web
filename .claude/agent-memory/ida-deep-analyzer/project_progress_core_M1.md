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

CORRECTION 2026-07-12: 0x671764 的 this 是 **EmoteEngine**，不是 Player。0x67D050..60 证明 X0=engine、W1=0、V0=original dt；它遍历 engine+1040/+1048 playing vector，并查 engine HM3@+936。错位的 Player::frameProgress 调用已移除，调用恢复到 EmoteEngine::progress。

## Byte-verified 字段类型 (a1=Player*)
- +480 progressFlags = **1-byte** (LDRB @0x6C1330, NOT 16-bit; clusterG "init 257" 存疑). LSB=1 时冻结 +1120 游标推进但仍跑 advanceRoot.
- +481 firstFrame 1-byte one-shot; +483 motionCompleted 1-byte (STRB WZR @entry 每帧清零, byte-verified @0x6C108C)
- +482 emoteMode 1-byte; +609 reverseSeekFlag 1-byte; +1093 motionStopGate 1-byte; +1098 syncWaiting; +1099 loopArmed
- +456 clampedEvalTime double = **min(+1120,+1128)** 标量 (NOT timeline-map lookup — 本端 G4 activeClipTime() 错误根因)
- +592 deltaTime double = +1168 speedMul * dt; +1120 frameTickCount, +1128 totalFrames, +1136 loopTime 全 double
- +1152 dword @entry 清零 (用途待定)

## 入口门控 (ENTRY GATE, 控制能否到达 LABEL_48 loop-wrap) — 2026-06-06 新增
Hex-Rays 把 +376==0 块乱序展示, 按反汇编地址重排后入口真实顺序:
1. 无条件副作用 (0x6C1080..0x6C10AC): +592=speedMul*dt, +483=0, 清+1152, [emote]initEmoteMotion, preProgressDirtyNodes. **在任何 return 之前**.
2. 0x6C10B0 `if(+376 activeTimeline)` 分发: !=0 走直接 node-deque walk(读+40当游标); ==0 走 loc_6C10E4.
3. loc_6C10E4 (+376==0 路径): `if(+481 firstFrame==0 && +1099 loopArmed==0) goto loc_6C1270` (renderList 检查); 否则查 +1098/+483 短路后落 LABEL_48.
4. loc_6C1270: `if(*(+384)==*(+392))` renderList 空 → return result. 非空 → node-deque walk → return.

**关键**: do-while loop-wrap @0x6C14CC(fwd)/0x6C145C(rev) 嵌在 `(+481!=0 || +1099!=0)` 门控**之内**. 未播放 child(+376==0, +481=0, +1099=0, renderList 空) 在 0x6C1278 早返回, **根本到不了 LABEL_48**. 这(非 loopTime<lastTime invariant, 非 +1136<0 默认)才是二进制避免全0 child 空转的真正机制.

## 主推进逻辑 (LABEL_48 @0x6C1330)
if !+480: +1120 += +592; +456 = min(+1120, +1128)   ← G3/G4 真正逻辑
正向(d>=0) 到尾且 +1136 loopTime>=0 → loop wrap modulo; <0 → 停尾(+1099=0)
反向(d<0) 对称 rewind + wrap to head

## 本端 frameProgress 入口勘误 (PlayerFrameProgress.cpp:1928) — 2026-06-06
本端开头 `if(!_speed) return;` 是 port-invented 错位守卫. 二进制 progress_inner 入口**无**此判断; _speed(+1093) 只是 advance/rewindRootAndNodes 内部 align/sync/action 事件 gate, 不门控整个 progress. 正确替换: 入口无条件副作用在前, 然后 `if(!_firstFrame && !_allplaying && renderListEmpty()) return;` (复刻 0x6C10E4/F0 + 0x6C1278). 本端 +1099=_allplaying(Player.h:1104), 无 +376 字段(恒走 +376==0 路径). renderList=二进制+384/+392 指针对, 本端对应容器待 grep 确认.

### 已实施 (2026-06-06, commit 待提交; binary-alignment-auditor 复核通过)
入口已重构为 progress_inner 真实拓扑(替换 commit 8883587 的临时门控 `if(!_firstFrame && !_allplaying)return`):
- 删除 port-invented `if(!_speed) return;`.
- 入口补 `_motionCompleted=false`(0x6C108C, Player.h:1148 早已注明"cleared each progress entry"但漏实现). +1152 DWORD 清零(0x6C1088)本端 Player 无该字段(所有 +1152 引用都是 EmoteEngine._windFreqY=engine+1152, 不同对象)→ 未建模字段缺口, 不臆造.
- loc_6C10E4: `if(!_firstFrame && !_allplaying){ ... }` 内含 loc_6C1270 renderList 空检查.
- **renderList(+384/+392) 身份已查实**: 56B element vector(begin/end/cap, 首字段 tTJSVariant*), ctor 清零@0x6CEE84, framesel(parse 0x6926B4/merge 0x692AB0/lerp 0x699AE4)产出, updateLayers@0x6BBD44 消费(count=bytelen/56), initNonEmoteMotion@0x6B3914 Release+end=begin 清空. **本端无 1:1 容器**(整个 node-deque 帧步进核心已 STL 化, 见下节), 其"有无可步进节点内容"语义用 `_nodes.empty()` 近似(ACCEPTABLE-PLATFORM-BOUNDARY); 非空分支(0x6C127C..0x6C130C node-deque walk → advanceNodeFrames)用 progressSeekNodeSlotsLike_0x6C106C 复刻.
- 短路: `if(_syncWaiting)return`(0x6C10F8) 再 `if(_motionCompleted)return`(0x6C1100, 入口已清→dead-but-faithful).
- firstFrame 块: 二进制 +481, 本端由 _queuing(+480) 承载(STRH 0x0101 同置 +480/+481, 本端 play 仅更新 _queuing). 控制流差异(二进制 fall-through vs 本端 return)经审计 observationally-inert(firstFrame 帧 +480 gate=1 主导, 二者净效果等价).
- 验证: logo render-events 逐事件 byte-identical(5287, 指针归一化); 千恋万花标题渲染正常无死循环(rAF 持续推进, 无 LOOPWRAP-FWD-HANG).

## 关键勘误 (本端 Player.h)
- Player.h:665 `_speed` 注释 "Aligned to +1093: bool flag" 是**错的**. +1093 是 motionStopGate (action/sync/align 开关), 不是 speed. speed 倍率在 **+1168 (double)**, 本端无对应字段.

## 本端缺失 (整个 node-deque 帧步进核心)
reseekTimelineCursors / advanceNode/Root / rewindRoot / parseFrame(0x6926B4) / mergeFrameContent(0x692AB0) 在 frameProgress live path 中全部缺失, 被 STL _timelines + control-animator 状态机替代 (architecture-level divergence, 需分阶段 re-arch, 见 plan P1-P7).

## differential 回归网
- logo diff (motion_playback/{yuzulogo,m2logo}) = 全帧逐层 Motion 状态, 守护 live-path 改动 (plan P5/P6/P7)
- staged oracle motion_playback_stages/frame_selection 在 evaluateTimeline@0x699AE4.leave 每节点采样 {activeSlot,nodeType,flags,visible,opacity} = 精准游标回归网 (P3/P4/P6)
