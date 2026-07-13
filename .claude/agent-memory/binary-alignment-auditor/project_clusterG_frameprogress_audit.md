---
name: clusterG-frameprogress-audit
description: 簇G frameProgress(PlayerFrameProgress.cpp) vs progress_inner@0x6C106C 对齐审计结论;3处局部偏差+子函数对齐表
metadata:
  type: project
---

dev/motion (2026-06-07) 簇G 帧进度引擎全量审计。报告: analysis/audit_motionplayer_2026-06-07/clusterG_frameprogress.md。

**结论 ⚠️ 部分偏差**: frameProgress 入口拓扑 + LABEL_48 forward/reverse loop-wrap 已 1:1 复刻
progress_inner@0x6C106C(逐行, 含双向 wrap do-while 0x6C14C4/0x6C1454)。2026-05-30 起点 ledger 的
SEVERE/G1-G18 表已过时(针对已替换的旧 STL 时间线状态机)。

**3 处局部偏差(均 logo-inert, 非架构重构)**:
1. 入口缺 `if(+482 _directEdit)initEmoteMotion(2)`(0x6C10A4) — directEdit 路径, logo inert; initEmoteMotion(2) 本身 port TODO。
2. **firstFrame 块读陈旧 _deltaTime**: 二进制 0x6C1108 v8=+592(入口 0x6C1094 设 speedMul*dt, 在 firstFrame 块**前**); 本地 line 2241 读 _deltaTime 但本帧赋值在 line 2312(块**后**)→ 用上帧残留值。影响 reverse-from-end seed + reverseSeekFlag 方向选择。修复: _deltaTime=_speedMul*actualDelta 上移到入口。logo 单向正向 inert。
3. **尾部 port-invented _allplaying/_syncActive 覆写**(line 2592-2593 + firstFrame 块内 2238-2239): 二进制 progress_inner 无此写; +1099(loopArmed) 二进制只在 STOP 分支写 0(0x6C13F4/0x6C1384); _syncActive 二进制 progress_inner 无此字段。会覆盖 STOP 分支刚设的 +1099=0。logo _playingTimelineLabels 恒空→inert。

**字段映射(disasm 直读确认)**: +482=_directEdit、+592=_deltaTime(speedMul*dt)、+1093=_speed(gate bool 非速度)、+1168=_speedMul(乘子)、+609=_reverseSeekFlag、+480=_queuing(progressFlags gate)、+481=_firstFrame、+483=_motionCompleted、+1098=_syncWaiting、+1099=_allplaying(loopArmed)、+456=_clampedEvalTime、+1120=_frameTickCount、+1128=_cachedTotalFrames、+1136=_loopTime、+376=active/default parameter entry。CORRECTION 2026-07-13：旧记载“本地无字段、恒 0”错误；本地语义等价字段为 `_defaultParameterEntryPtr`，0x6C106C 专路已恢复。

**子函数对齐(本文件内全 ✅)**: advanceRootAndNodes_0x6B6ADC(4-stream layer①→root②→var-track③→node④, 反编译 0x6B6ADC 确认)、rewind 同边界、seekLayer/seekRoot/var-track 三步进、reseekTimelineCursors_0x6B86C8、HM3/HM4 init-restore 链(memory hm3-loop2-restore)、interpolateVarTrack_0x6BBE20、resetMotionState_0x6B2D3C。
**❓ 跨文件递归待审**: progressSeekNodeSlotsLike(node walk④ inline seek: parseFrame 0x6926B4 + mergeFrameContent 0x692AB0 + mask&4 per-node action slot+288 + node+346/+882 merge gate + findSource, 在 PlayerUpdateChildMotion/Render)、preProgressDirtyNodes_0x6B6878、updateLayers/calcBounds/dispatchEvents。

**Why**: 这是最核心子系统; 起点 ledger 已过时, 勿据其 G1-G18 重判 SEVERE。
**How to apply**: 改 frameProgress 前对照上面字段映射(disasm 直读, 非命名推断); firstFrame 块 deltaTime 时机是真 bug(#2); 删尾部 _allplaying/_syncActive 前先 grep 调用方。logo 0-mismatch 是非回归守护非功能验证(无 reverse/emote/timeline fixture)。
