---
name: clusterH-timeline-framestep-audit
description: 簇H timeline/frame-stepping审计(2026-06-07) — P2/P3/P4独立端口 vs advance/rewind/advanceNode/reseek/parseFrame/merge 6函数;6维总评+H-P1偏差+地址映射
metadata:
  type: project
---

簇H (PlayerFrameStep/PlayerFrameStepping/PlayerTimeline) 审计结论 (2026-06-07)。
报告: analysis/audit_motionplayer_2026-06-07/clusterH_timeline_framestep.md

**关键: 这些是 P2/P3/P4 独立单测端口 (free functions), 未接 live path。**
注意同名陷阱: `advanceNodeFramesLike_0x6B7E44` 有两个重载 —
- P3/P4 版 `(NodeFrameStreamsLike&, TimelineSeekStateLike&)` = 簇H范围(本审计)
- live 版 `(MotionNode&, double)` 在 PlayerUpdateLayerEval.cpp:616 = 簇G/其他范围

**地址↔本地映射 (全部 fresh decompile 核对, byte-exact):**
- parseFrame 0x6926B4 ↔ parseFrameLike_0x6926B4 ✅
- mergeFrameContent 0x692AB0 ↔ mergeFrameContentLike_0x692AB0 ✅
- advanceNodeFrames 0x6B7E44 ↔ advanceNodeFramesLike (P3版) ✅
- advanceRootAndNodes 0x6B6ADC ↔ advanceRootAndNodesLike_0x6B6ADC ✅
- rewindRootAndNodes 0x6B9A3C ↔ rewindRootAndNodesLike_0x6B9A3C ✅
- reseekTimelineCursors 0x6B86C8 ↔ reseekTimelineCursorsLike_0x6B86C8 ✅(layer/root)

**byte-verified mergeFrameContent gate (易错点, 全部本地已对):**
- ti gate = mask&0x04000000 (slot+23 byte &4) NOT 任意位
- cp gate = interp(slot+25) && mask&0x10000 (slot+22 byte &1)
- color -1 fill = 仅当 color位(0x200)缺 AND blendMode&0xF0==0 (0x6933D4)
- ti+curves(0xF800)+cp 全部在 interpFlag(slot+25) gate 之后(LABEL_95跳过)

**唯一偏差 H-P1 (P1):** reseek 末尾 binary 对**每个**node(idx>=1)调
Player_initNodeTimeline(seed slot cursor vs +456); 本地端口只对 hasChild node
跑 advanceNodeFrames, 非child init DEFERRED (PlayerFrameStepping.cpp:587-596)。
真data-flow缺口但隔离,无需re-arch。修法additive(给!hasChild加inline seed),
oracle-inert,等node-level reseek fixture再落,**勿造fixture**。IDB注释@0x6B91B0已加。

**H-N1 note:** node-walk `dequeSize-1 <= idx`/÷329/÷3 magic 是 libstdc++ deque
size()/index 内联展开, 非源码token。flat std::vector 是正确源码模型, 勿当
off-by-one/sentinel误改 (簇D/F教训)。`-1`抵消的是1-extra-block sentinel。

**旧簇G SEVERE表已过时:** 那针对旧STL状态机(_timelines map等), P2/P3/P4端口
当时不存在。本审计用当前代码+新反编译重核, 推翻簇G "整个node-deque缺失"结论
(node-deque帧步进核心已1:1端口, 只是未wire live)。

**合法PLATFORM_BOUNDARY(已列):** TJS Array dispatch→PSBList; var-track deque
(160B opaque records, sub_6B786C/6B7A70); findSource/action/sync body;
mesh/bezierPatch; icon-handle; +280 aux list/pruneHM3。

总评: ⚠部分偏差(仅H-P1), 无停滞(首次审这些端口), 无需架构重构。
