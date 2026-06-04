---
name: m1-framestep-rootstream-verdict
description: M1 帧步进/root-stream 子系统 fresh-decompile 复核(2026-06-05);LIVE split vs binary monolith;3 open gap R-1/R-2/R-3;R-1 注释证伪需纠正
metadata:
  type: project
---

2026-06-05 fresh-decompile 复核 M1 帧步进/root-stream(advanceNodeFrames 0x6B7E44 / advanceRootAndNodes 0x6B6ADC / rewindRootAndNodes 0x6B9A3C / reseek 0x6B86C8[用户给的 0x6B91B0 是此函数内 node-init loop 标号,非独立函数] / inline seek 0x6B73DC fwd & 0x6BA1CC rev).

**三套并行实现(分清 LIVE/DEAD):**
- DEAD monolithic 1:1: PlayerFrameStepping.cpp(190/272/386/482) — 仅 tests/motionplayer-dll.cpp 调用;注释含过时 "var-track DEFERRED"(LIVE 实为已实装)
- LIVE per-stream split: PlayerFrameProgress.cpp — advanceRootAndNodes_0x6B6ADC(t)=seekLayer(884)+seekRoot(1029)+advanceVarTracks(1109); rewind=+rewindVarTracks(1211); reseek=reseekTimelineCursors(1442)+reseedVarTracks(1312)+reseekNodeTimelineSlots; 顶层 driver 2040-2235(0x6C13xx 锚点)
- LIVE node seek 引擎: PlayerUpdateLayerEval.cpp advanceNodeFrameSelectionLike_0x6926B4(374); 0x6B7E44(533)→delegate; inline wrapper PlayerInternal.h 1491/1498

**容器选型 ✅**: _nodes=std::deque<MotionNode>(Player+184), _variableLabelScopes=std::deque(Player+1296) — 对齐 binary deque(非 vector 替换),node-walk 480B块/2632B步长 deque 遍历语义保留.

**byte-verified ✅**: action push arg2=slot+0x120(disasm 0x6B74D8 `add x2,x22,#0x120`); rev var merge slot[0]then slot[1](0x6BA010=+48/0x6BA024=+104) vs fwd merge slot[0]两次(0x6B7178/0x6B71A0 都+48); param1=*(node+0)=label(0x6B3DF4 seed); seek target *(node+8)+40==parameterEntry->value(D-A1链).

**3 open gap(真实缺失,非平台边界):**
- R-1(🟡中): seekRootContentStreamLike_0x6B6ADC 只有 forward 循环,缺 rewind root 反向段. binary 0x6B9E84..0x6B9FC4 确是 `--+568` 反向 do-while. **注释 PlayerFrameProgress.cpp:1022 断言"无反向 root scan"与 fresh decompile 直接矛盾,需就地纠正**(证伪即纠正). logo priority<2帧→inert.
- R-2(🟡低): reseekNodeTimelineSlots 用 i<nodes.size(), binary 是 m<dequeSize-1(off-by-one,本地多遍历末节点). loop-wrap 才触发,logo 不命中.
- R-3(🟡低): reseek TAIL pruneHM3 0x6B9234 + sub_6B9650 aux-list 0x6B9248 DEFERRED(无 live consumer,但按 CLAUDE.md 仍应复原 → 记为未做,非平台边界).

**平台边界(合法)**: B-1 sub_A0FB64 tTJSVariant copy→shared_ptr(+616 root content); B-2 var easing 存 PSB value.

**六维评分**: 源码结构🟡7(monolith→split) / 数据流✅9 / 调用链✅9 / 生命周期✅9 / 容器✅9 / 边界🟡7. 综合高度对齐,主遗留=结构性 split 偏离(已知)+3 inert真实缺口. 优先纠 R-1 错误注释.
