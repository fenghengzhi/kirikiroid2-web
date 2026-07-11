---
name: dracu-fresh-type3-child-done-timing
description: DRACU 标题同步无限递归——fresh type-3 子节点 build-vs-seek 时序裁决；slot.done build-time=1 如何被保持
type: project
---

2026-06-21 fresh-decompile 裁决（DRACU 标题同步无限递归 / fresh type-3 子节点 build-vs-seek 时序）。

**地址映射（权威）**
- build-time slot.done 初值：`Player_buildNodeTree_recursive` @0x6B4B80，写点 @0x6b4be8(slot0+24=node+344=1)/@0x6b4bec(slot1+24=node+880=1)。对所有节点无 type 区分。case3 child 构造 sub_6B43C0 @0x6B43C0 不碰 slot.done，只 new(0x568) child Player 存 node+1912。
- slot+24(done) 全部写者：build @0x6b4be8/ec→1；parseFrame(@0x6926B4) type!=0 @0x69280c→0；type==0 @0x692828→1（经 resetFrameSlot @0x692628）。
- 延迟 seek 根门控 = progress_inner @0x6C106C 入口 @0x6C10F0：`+481 firstFrame==0 && +1099 loopArmed==0` → 落 0x6C1278 块 → renderList 空(+384==+392) early-return。fresh child 两标志皆 0 → 永不进 seek（reseek/advanceNodeFrames/advanceRoot）。loop-wrap do-while @0x6C14CC 不可达。这是二进制避免全零 +1136/+1128 child 自旋的真机制（非输入守卫/非 +1136<0 默认）。
- childMotionPass @0x6BE0C0：destroy gate @0x6be31c `if(!slot.done)`，fresh done=1→不跳→destroy @0x6BE328(resetAndReleaseNodes+release +976/+984)+continue(无递归)。play gate @0x6be368 还要 slot+356(src)非空，fresh=NULL→也 destroy。
- advanceNodeFrames @0x6B7E44 seek target @0x6B7E90 = *(node+8)+40 (param value) 或回退 player+456；只 type!=0 帧清 done。

**端口对照（现状对齐 ✓）**
- ClipSlot `done=true` 默认 MotionNode.h:136；`frameIndex=-1` 默认（已-seed 门控字段）。
- done 翻 0 唯一点：PlayerUpdateLayersInternal.h:150 `slot.done=!s.visible`（经 populateSlotFromState←populateClipSlotFromFrameLike_0x6926B4 @PlayerUpdateLayerEval.cpp:265）；s.visible 仅 line 250(!parsed.invisible,type!=0)置 true。
- destroy gate：PlayerUpdateChildMotion.cpp:81 `mn.activeSlot().done`；src gate line 101。
- per-node 已-seed 门控：PlayerUpdateLayerEval.cpp:477 `frameIndex<0&&otherSlot frameIndex<0` → initializeNodeTimelineSlotsLike_0x6B64AC（lazy 安全网）。

**历史无限递归根因（已修复 ✓）** PlayerUpdateLayerEval.cpp:523-545：曾在 advanceNodeFrameSelection 尾无条件调 INVENTED markNodePayloadDirtyFromState 钉 node+44=1 → evaluateTimeline internalDirty(@0x699B1C)→accumulated.dirty(node+1504) 恒1 → childMotionPass 永不 skip → 子 motion 每帧重 play、_deltaTime=0 → 子树永不 done → 无限递归(28000节点/1.9GB/黑屏)。已移除，node+44 仅 seek 迭代体内置1，无 seek 时 @0x6BBD2C 后置清0 settle。

**唯一待核实（上游根门控差异 #3）** 端口 frameProgress(PlayerFrameProgress.cpp) 入口是否忠实复刻 progress_inner @0x6C10F0 的 +481/+1099 双零 gate + renderList 空 early-return @0x6C1278。这比 per-node frameIndex 门控更上游/更权威，是二进制延迟 seek 的真根因。差异 #6(低): reseekTimelineCursors @0x6B91B0 对 idx>=1 每节点 initNodeTimeline，端口仅 hasChild 跑 advanceNodeFrames，非 child init DEFERRED(clusterH H-P1)，不影响 fresh-done。
