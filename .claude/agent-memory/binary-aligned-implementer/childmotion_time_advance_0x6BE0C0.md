---
name: childmotion-time-advance-0x6BE0C0
description: child motion player 时间推进机制 (childMotionPass 0x6BE0C0 + progress_inner 0x6C106C + updateLayers 0x6BBD44)。DRACU 标题黑屏/泄漏排查的二进制权威链
type: project
---

DRACU child-motion 时间推进的完整二进制机制（反编译 0x6BE0C0/0x6C106C/0x6BBD44 取证）。

**Why:** 排查 DRACU 标题 char/show 自引用 child motion 递归不收敛 (28000 players/1.9GB)。端口 char/show child `_clampedEvalTime` 卡 0，'bg' 节点 (frameList=[t2@0,t0@71]) 永停 frame0、永不 done、永不被 childMotionPass DESTROY。

**How to apply:** 修 child 时间推进时按此链对照，勿再推翻。

二进制 child 时间推进 = 两段协作：
1. **time-sync (0x6BE49C, re-init-gated, SEED only)**: 仅在 re-init 帧 `(v13&5)||node+44`(0x6BE37C) 且 `child+1099(_allplaying) && child+480(_queuing)`(0x6BE478) 时跑。`childTime = parent+1120(_frameTickCount) - slot+8(clipStartTime) + slot+376(motionTimeOffset)`；写 child+1120/+456，**置 child+480/+481 = 0x0101 (queuing+firstFrame)** (0x6BE4E8)。slot base = node+320+536*slotIdx；clipStartTime 在 parseFrame(0x6927E0) 从 motion 帧 "time" 写一次、不每帧重设(故 childTime 随 parent tick 增长)。
2. **progress_inner LABEL_48 自增 (0x6C1344, 每帧, 主推进)**: `if(!child+480 queuing){ +1120 += +592(_deltaTime); +456=min(+1120,+1128) }`。0x6BE2A4 调 `child.progress_inner(parent+592 /*parent._deltaTime*/)`。

**关键 queuing 生命周期**: child+480(_queuing) 在 `Player_updateLayers` 末尾 **无条件清 0** (0x6BBDFC，LABEL_146，二进制无 nodes-empty 早退)。同处清 +608(_noUpdateYet 0x6BBDF8)、每非 root node 的 +44/+1504 (0x6BBD2C/0x6BBD30)。
- 首 re-init 帧: initNonEmoteMotion(非chain分支 0x6B3AAC STRH 0x0101) 置 queuing=1+firstFrame=1 → time-sync 跑(seed) → frameProgress firstFrame块清 firstFrame=0 → updateLayers 清 queuing=0。
- 后续帧: time-sync gate `allplaying&&queuing(0)`=FALSE → skip → frameProgress queuing=0/firstFrame=0 → LABEL_48 自增。
- same-motion dedup (Player_playImpl 0x6B22C4 gate `(a3&5)|| motion differs`): 同 motion 无 Force/AsCan → return 不调 initNonEmoteMotion，不动 queuing/firstFrame。端口 onFindMotion@PlayerMotionLoad.cpp:55 已忠实复刻。
- cycle terminator: childMotionPass 0x6BE31C `if(!slot+24 slotDone)` → play; else 0x6BE328 DESTROY child(resetAndReleaseNodes+release +976/+984)，递归收敛。

**候选端口偏差 (firstFrame fall-through, 未经 runtime 确认)**: 二进制 firstFrame 块(0x6C1108..0x6C1328) **fall-through 到 LABEL_48**(0x6C1330)；端口 PlayerFrameProgress.cpp:2323 **return**。端口注释证明"LABEL_48 gated no-op return"**仅对 queuing=1 成立**；chain 分支(0x6B3AC0/PlayerCore.cpp:911 只置 firstFrame、不置 queuing) 下 firstFrame=1&queuing=0 时二进制 LABEL_48 会 `+1120+=+592` 自增，端口早退不增。若 chain 路径活跃即为 bug。端口 frameProgress fall-through 还会触发误植的 preProgressPlayingTimelinesLike_0x671764(2370)，破坏 yuzulogo(243→242)，故不能裸删 return，需复刻 LABEL_48 跳过 preProgress。

**端口诊断探针**: PlayerUpdateChildMotion.cpp FRAMEDIAG 已扩展，dump parent/child tick/dt/queuing/firstFrame/allplaying/syncGate/clipStart/tOffset (chara=='char'&&src含char/show，前12次)。用于 runtime 定位 c.q/c.ff/c.dt 哪个卡住。logo yuzulogo(243)/m2logo(93) 差分 0-mismatch。
