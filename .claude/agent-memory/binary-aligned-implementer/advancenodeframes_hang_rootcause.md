---
name: advancenodeframes-hang-rootcause
description: ROOT CAUSE (pinned via watchdog backtrace + exit-probes 2026-06-04) of the m2logo CI hang from the advanceNodeFrames (0x6B7E44) convergence — a null-motion child Player ("レイヤ1") left _speed=1/totalFrames=0 spins the progress_inner loop-wrap do-loop PlayerFrameProgress.cpp:2100. Fix = make advanceNodeFramesLike leave the parameterized node's active-slot (done/src/index) identical to advanceNodeFrameSelectionLike so the child-motion play decision matches baseline.
metadata:
  type: project
---

# advanceNodeFrames (0x6B7E44) 收敛 → m2logo CI 挂起:已钉死根因（2026-06-04）

工具:本会话给 `wasm_lldb_motion_trace.py` 加的 watchdog backtrace(已提交 996d559)
+ guest `exit(N)` 探针(命中即 guest 非零退出 → harness 经 driver-exit 路径回显 guest stderr,
绕过"挂起时日志被 kill 丢弃")。**重要方法论:本地验证必须用 per-case xp3
`reference/xp3/logo_test_oracle_m2logo.xp3`,不能用 runner 默认 xp3,见
[[local-motion-playback-differential-per-case-xp3]]。**

## 死循环精确位置
`PlayerFrameProgress.cpp:2100-2102`(progress_inner 前向 loop-wrap,忠实复刻二进制
0x6C14C4/0x6C14CC):
```
do { v7 = v7 + _loopTime - _cachedTotalFrames; } while(_cachedTotalFrames <= v7);
```
当 `_cachedTotalFrames <= _loopTime`(实测都是 0)→ 减量 0 → 永不退出。**该 do 循环
本身是忠实的,不要改它**;二进制不挂是因为其流程永不带 totalFrames=0 走到这里。

## 触发状态(exit-probe 实测值)
一个 nodeType=3 子动作节点 `mn.label='レイヤ1'`,其 child Player:
`_speed=1`(在播放) / `_activeMotion=null` / `childMotionName='' `/ `_cachedTotalFrames=0`
/ `_loopTime=0`。active slot:`src='' done=1 frameIdx=0 clipStartTime=0`,`paramVal=0`。
→ child.frameProgress(dt=1) 进入前向 wrap,totalFrames=0 → 2100 空转。

## 根因链
1. reroute(progressSeekNodeSlotsLike 对参数化节点改走 `advanceNodeFramesLike_0x6B7E44`,
   PlayerUpdateLayerEval.cpp)改变了 "レイヤ1" 的 **active-slot 定位 / done / src**,与旧路径
   `advanceNodeFrameSelectionLike_0x6926B4`(其结尾 433-440 还会
   frameStateFromNodeSlots + markNodeNoActiveFrame/currentFrameType/markNodePayloadDirtyFromState)
   不一致。
2. updateLayersPhase3_MotionSubNode(PlayerUpdateChildMotion.cpp)的子动作播放决策依赖该 slot 状态:
   - line 45 `if (mn.activeSlot().done) { cleanup; continue; }` 跳过 frameProgress;
   - line 66 `if (!src.empty())` 才 onFindMotion/play。
   在某一帧 "レイヤ1" 因 reroute 呈现了一个会触发 play 但**加载失败**的 src →
   child 变成 `_speed=1` 但 `_activeMotion=null`(totalFrames=0);后续帧 src='' 不再停止它 →
   playing-but-motionless child 持续被 frameProgress → 2100 空转。
3. 基线(reseek-only,advanceNodeFrames 回退)不播放该 null child(slot done/src 取值不同),故不挂。

## 修复方向(用户已选 A=根因对齐;未实施)
让 `advanceNodeFramesLike_0x6B7E44` 对参数化节点留下与
`advanceNodeFrameSelectionLike_0x6926B4` **一致的 active-slot(activeSlotIndex/frameIndex/done/src)
+ 节点 payload/active 状态**(很可能要补回旧路径 433-440 的 state-establish 尾,或确保 seek 终止后
slot.done/src 与旧路径相同)。**下一步定位**:同一帧对 "レイヤ1" 同时打印 old-path 与 new-path
留下的 activeSlotIndex/frameIndex/done/src 做差分,锁定具体偏差字段,再对齐。
不要改 2100 do 循环本身(忠实)。改完用 watchdog 工具 + per-case xp3 跑 m2logo+yuzulogo 双 case 验证。

## 现状
分支 dev/motion 绿(reseek-only)。advanceNodeFrames 仍 REVERTED(见 [[advancenodeframes-0x6B7E44-convergence]])。
reseek 收敛(Player::reseekTimelineCursors)已落地并经 CI 验证(m2logo 93=93 / yuzulogo 243=243)。
