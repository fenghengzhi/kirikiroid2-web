---
name: p6-seek-lives-in-cursor-machine
description: M1 P6 blocker — binary's progress-pass seek is sub_6926B4 INSIDE advance/rewind cursor machine (P2/P3/P4 parallel structs), NOT a hoistable live seek; updateLayers in binary is already pure interpolate
metadata:
  type: project
---

M1 P6「把 live seek 从 updateLayers 搬到 progress」是**不可在 task 约束内完成**的结构矛盾。决定性反编译证据（本轮全部一手）：

**Player_updateLayers @0x6BB33C** — 主循环每节点**只调** `Player_evaluateTimeline(node, dirty, *(a1+456))`（+456 = clampedEvalTime，eval-at-time 插值）。全函数**无 sub_6926B4 调用**。即：二进制的 updateLayers 已经是纯 interpolate。

**Player_progress_inner @0x6C106C** — seek 由 `Player_advanceNodeFrames(0x6B7E44)` / `Player_advanceRootAndNodes(0x6B6ADC)` / `Player_rewindRootAndNodes(0x6B9A3C)` / `Player_reseekTimelineCursors(0x6B86C8)` 完成。这些是 cursor-stepping。

**xrefs_to 0x6926B4** = {Player_initNodeTimeline_guess 0x6B64AC, Player_advanceRootAndNodes 0x6B6ADC, Player_advanceNodeFrames 0x6B7E44, Player_rewindRootAndNodes 0x6B9A3C}。**没有 updateLayers**。
=> 二进制里 sub_6926B4 是 cursor 机器的**子程序**，只从 progress 侧被调，操作的是 stream-cursor 解析槽。

**本端两套 slot 是分开的存储：**
- live: `MotionNode::ClipSlot slots[2]`（MotionNode.h:184），被 `advanceNodeFrameSelectionLike_0x6926B4`(seek) + `evaluateTimelineLike_0x699AE4`(interp) 共用。
- 并行参考(P2/P3/P4): `ParsedFrameSlotLike_0x6926B4 slots[2]`（PlayerFrameStepping.h:131），被 `advanceRootAndNodesLike_0x6B6ADC`/`advanceNodeFramesLike_0x6B7E44` 操作。

**矛盾点：** task 要求「用现有 live 函数把 seek 搬进 progress，且禁止动 P2/P3/P4 的 PlayerFrameStep*/PlayerFrameStepping*」。但二进制里 progress 侧的 seek **就是** advance/rewind 机器（= P2/P3/P4 那套，操作并行 ParsedFrameSlot），它不读写 live ClipSlot。把 live `advanceNodeFrameSelectionLike_0x6926B4` 搬进 frameProgress 不会更接近二进制——二进制 progress 不调用 live-slot 路径，而是调用并行 cursor 路径写并行槽，再由（尚未 wire 的）路径把结果灌回。

**真正的对齐路线（非本 task 范围）：** 让 progress 调用已实现的 `advanceRootAndNodesLike_0x6B6ADC` 写并行 ParsedFrameSlot，再建立 ParsedFrameSlot→ClipSlot 的灌入桥，最后 updateLayers 删掉 inline seek 只留 interpolate。这需要动 P2/P3/P4（task 明令禁止），是 P7+ 工作。

**时序耦合结论（附带确认，正面）：** live seek (`advanceNodeFrameSelectionLike_0x6926B4` + `frameSelectionTimeLike_0x6B7E44`) 是纯 per-node-local：只读 node.psbNode/slots/parameterEntry/flags + 标量 currentTime(=_clampedEvalTime)，**不读 parent.accumulated**。所以「seek↔interpolate 跨父子 transform 时序耦合」**不存在**——若 task 约束允许，单纯把 live seek 提前到 progress（_clampedEvalTime 在 0x867 算定之后）是时序安全的。漂移风险来自「搬了 live seek 但二进制 progress 根本不走 live-slot 路径」这个架构错位，而非时序。
