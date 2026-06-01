---
name: m1-progress-stage-machine
description: M1 progress 帧步进机模块对齐进展 (Stage A-D, 2026-06-01)；含 differential 本机跑法、CI 盲区、reseek/advance gate 边界
metadata:
  type: project
---

M1 progress 帧步进机剩余 4 stage 对齐 (2026-06-01)。范围: 仅 layer 事件流调用点 + reseek
stand-in + 注释忠实化；P5/P6 核心游标机 (Player_progress_inner @0x6C106C LABEL_48) 未动。

**Why:** 把 M1 剩余作为模块整体对齐 libkrkr2.so，additive-first + 每 stage CI 绿。
**How to apply:** 下次推进 M1 时直接读此文件 + analysis/Player_progress_frame_stepping_M1_plan.md §8。

## 本轮完成 (改动文件: PlayerFrameProgress.cpp, PlayerFrameStepping.cpp)
- **Stage A (DONE, 行为变更但 differential 0 mismatch)**: 洞3 layer 事件流调用点重定位。
  把末尾单次 `seekLayerEventStreamLike_0x6B6ADC(_clampedEvalTime)` 删除，移入每个
  advanceRoot/rewind 等价点 (layer 在 node 前, 同 +456)。证据: advanceRootAndNodes
  @0x6B6ADC 内部顺序固定 [layer(+916 cursor)→root(+548)→var-track→node walk(LABEL_86)],
  全部 keyed on *(a1+456)。修复 loop-wrap 段内事件丢失 + align/sync snap 1帧延迟。
- **Stage B Step B1 (DONE)**: 修正 stand-in reseekTimelineCursorsLike_0x6B86C8 落帧门控
  bug — 二进制 0x6B89D4..0x6B8A28 byte-verified: gate 用 **cursor frame** (v100=
  PropGetByNum(+916)) 的 type/content, 不是 frames[cursor+1]。port 原读 nextF (错)。
- **Stage C (注释忠实化 DONE, 实际 teardown DEFER)**: 0x671764 错位调用点 xref 确认。
- **Stage D (评估 DONE, 无工作)**: `_nodes` **已是 std::deque<MotionNode>** (commit 8cee351
  "A8"), 容器选型早已对齐。M1_plan §1 / module_motionplayer §G 的 "本地 std::vector" 过时。

## 关键 RE 边界 (复用，勿重推)
- **advance/rewind layer gate (0x6B6DD8/0x6B9D0C) = +1093-only**: align 不 time-gate;
  align→+483=1,snap +456/+1120; sync→+1098=1,snap,pushSync; action(ungated)→pushAction(void,name)。
- **reseekTimelineCursors layer gate (0x6B8A8C) = +1093 AND +920==+456 (精确落帧)**: align 有
  二次 +920==+456 门控; sync 只需 +1093。**与 advance 形式不同**——两者不可混用。
- progress_inner @0x6C106C 各调用点 +456 值: 0x6C1468 advanceRoot=totalFrames; 0x6C1488
  **reseekTimelineCursors**=loopTime; 0x6C11B0 advanceRoot=wrapped; 0x6C13A4 advanceRoot=min;
  0x6C1408 rewind=loopTime; 0x6C1428 **reseekTimelineCursors**=totalFrames; 0x6C11C0 rewind=wrapped。
  ⚠ 0x6C1488/0x6C1428 是 reseekTimelineCursors (全量+精确门控), **不是** advanceRoot — port
  这两点只加 node seek, **不加** advance-form layer seek (gate 形式会错)。
- **0x671764 Player_preProgress** = playing-list controller stepper, xref 仅 EmoteEngine_progress
  (0x67D01C) + sub_675E40, **不在** progress_inner 链。progress_inner 的 preProgress 是
  Player_preProgressDirtyNodes @0x6B6878。frameProgress:998 调它是错位, 应迁 EmoteEngine.cpp (DEFER)。
- **_evalResultValues = HM2 @+320 的 port mirror, 非凭空多出** (0x6C4668 LABEL_132
  HM2_upsert(+320,label)=value, logo-diff gated)。但每帧 `.clear()` 是 port-invented
  (二进制 HM2 持久, progress_inner 入口只清 +1152/+483)。移除 clear = 高风险行为变更, DEFER。

## 本轮 DEFER (含原因, 供下次决策)
- **Stage B Step B2 (live firstFrame 接线)**: frameProgress **从不读 _firstFrame** (死字段,
  仅 setTickCount@Player.h:225 + child motion 置位)。firstFrame 种子分支 (progress_inner
  0x6C10E4) live 缺失, 当前靠 _queuing 近似 (PlayerFrameProgress.cpp:957-972) 承载。忠实化需
  live reseekTimelineCursors (三流多数 DEFERRED) + 行为变更, 且 firstFrame 是否被 logo 覆盖未确认。
- **Stage C 实际 teardown**: 迁出 0x671764 需 progress 拓扑大重构 (frameProgress 是 emote/非-emote
  混合体); selectActiveClip/activeClipTime 仍 6 处嵌入 render/child; 均无 additive 路径。

## CI 盲区 (Stage A 事件路径)
yuzulogo/m2logo oracle traces **不含 onAction/sync 断言**, 大概率无 type==1 tag-action 帧
(trace 无 action/sync 痕迹)。所以 Stage A 对事件触发的修复在 logo differential 中 **inert** —
differential PASS 只证未破坏 Motion 状态。真验证需含 tag-action 帧的 motion runtime 复核
(playwright + 日志捕获 onAction 实参, §8.7 警告)。本轮未做, 标注为盲区。

## 本机 differential 跑法 (重要, 之前误判为不可跑)
本机 macOS **能跑** motion_playback wasmtime differential (python wasmtime 43.0.0 已装 + lldb):
1. 构建 guest: `cmake --preset "Wasmtime Headless Debug Config"` (首次) +
   `cmake --build out/wasmtime/debug --target krkr2_wasmtime_guest` (含改动重建)
2. 跑 (无 `timeout` 命令, 用绝对路径):
   `cd tests/differential && python3 python/run_motion_playback_wasmtime.py
    --wasm <ABS>/out/wasmtime/debug/krkr2_wasmtime_guest.wasm
    --startup-xp3 <ABS>/reference/xp3/logo_test_oracle.xp3
    --case yuzulogo --case m2logo --lldb-timeout 480`
   绿 = "PASS: m2logo (93 frames) / PASS: yuzulogo (243 frames)"。
- ⚠ macos 单测 target `motionplayer-dll` 当前 **预存编译 drift** 阻塞 (motionplayer-dll.cpp:630/
  673 用了改名的 TimelinePlayFlagSequential 等 API), 非本轮引入, stash 验证确认。本模块回归网靠
  wasmtime differential, 不靠 macos 单测。
