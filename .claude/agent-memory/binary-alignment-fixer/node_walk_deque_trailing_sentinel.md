---
name: node-walk-deque-trailing-sentinel
description: 二进制 node-deque 比真实节点多 1 个 trailing 元素;所有 node-walk 的 `dequeSize-1` 退出条件 == 本地 `i < _nodes.size()`,NO off-by-one。不要改成 i+1<size()
metadata:
  type: project
---

motionplayer node-walk 循环上界：二进制 `idx < dequeSize - 1` 等价于本地 `i < _nodes.size()`，**不是** off-by-one。

**事实**：libkrkr2.so 的 node-deque (`Player+200`, 2632B stride) 携带 **一个额外的 trailing past-the-end 元素**，超出真实节点数。因此 `dequeSize == realNodeCount + 1`，于是 `dequeSize - 1 == realNodeCount == 本地 _nodes.size()`（本地 deque 无 trailing 元素）。所有 node-walk 的 `... - 1 <= idx -> break/return` 退出条件，那个 `-1` **抵消的是 trailing sentinel，不是真实末节点**——遍历范围 `[1, realNodeCount-1]` = 全部真实非 root 节点，与本地 `i < _nodes.size()` 逐位一致。

**4 个 node-walk 点全部如此**（全在 PlayerUpdateLayerEval.cpp）：
- progress_inner 0x6C12D8 / advanceRootAndNodes LABEL_86 0x6B7398 / rewindRootAndNodes — 本地共享 `progressSeekNodeSlotsLike_0x6C106C`
- reseekTimelineCursors 0x6B9200 — 本地 `reseekNodeTimelineSlotsLike_0x6B91B0`
- preProgressDirtyNodes 0x6B6920 — 本地 `preProgressDirtyNodesLike_0x6B6878`

**独立交叉核实**：`Player_buildNodeTree @0x6B51F0`（0x6B531C）有一个**独立的** deque-size 表达式，同样 `- 1`，其 `v9=1; while(++v9 >= dequeSize-1)` 循环读真实节点字段（node+28 type==12），证明 `dequeSize = realNodeCount + 1`。

**Why（铁证）**：2026-06-06 把三处改成 `i+1 < size()`（误信审计「_nodes 与 deque 1:1」断言）→ yuzulogo wasmtime 差分 **468 mismatches**（最后一个真实节点 layer_index 24 从 frame 64 起停止被 seek，active False/opacity 不动）。基线 `i < size()` 逐位 PASS。改回后 yuzulogo+m2logo 全 PASS。

**How to apply**：任何 motionplayer node-walk 对齐任务，遇到二进制 `dequeSize - 1` 退出条件，**不要**据此把本地 `i < _nodes.size()` 改成 `i+1 < size()`。审计报告若声称「`_nodes` 与二进制 deque 1:1」——该断言**错误**，deque 多一个 trailing sentinel。先跑基线差分（git stash 改动 → 重建 guest → 跑 logo）确认现状再动手。差分入口：`run_motion_playback_wasmtime.py --wasm out/wasmtime/debug/krkr2_wasmtime_guest.wasm --startup-xp3 reference/xp3/logo_test_oracle_<case>.xp3 --case <case>`（wasmtime 是 python 模块,非 CLI;guest target = krkr2_wasmtime_guest）。
