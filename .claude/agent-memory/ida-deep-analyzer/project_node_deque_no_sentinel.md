---
name: node-deque-no-sentinel
description: 裁决 — Player+184 node deque 无 trailing sentinel；dequeSize==realNodeCount(root+N child) 不是 +1；所有 node-walk `-1` 是 +1 偏置抵消非 sentinel
metadata:
  type: project
---

**裁决 (b)：node-deque 没有 trailing sentinel/terminator。** 先前 memory/注释里"dequeSize==N+1，多一个 trailing sentinel"的解释**已被证伪**，2026-06-06 反编译+代数+数值模拟三重确认推翻。本地 _nodes(std::deque<MotionNode>) 已正确等价，**不要**去 buildNodeTree 末尾补 push、**不要**把 walk 改成 size()-1。

## 容器构造（反编译证据）
- `Player_ctor@0x6CED30` (push 段 @0x6cf17c-0x6cf1f8)：inline `_M_push_back` **恰好 push 1 个元素 = root(index0)**，随后 `v21-2548..` 写 root 初始字段(dword_1AA40D8..)。**没有第二个 push，没有 sentinel**。
- `Player_resetAndReleaseNodes@0x6B56F8` (erase @0x6b59ac → sub_6F3E0C=std::deque::erase)：擦除 `[index1, end)`，**保留 root**。所以 buildNodeTree 总是从 1-元素(root-only) deque 起步。
- `Player_buildNodeTree_recursive@0x6B4A6C` (push 段 @0x6b4b84-0x6b4be8)：对 PSB `content["layer"]` 数组每元素 inline push 1 个 node + 递归 children。最终 size = 1(root) + N_real_children。**realNodeCount == 最终 deque size**。

## 为什么所有 walk 用 `dequeSize - 1`（决定性）
deque 元素 2632B > 512 → libstdc++ `__deque_buf_size=1`（1 elem/block）。二进制算 size 时用 `(start.last - start.cur)/T` 代替标准的 `(start.cur - start.first)/T`：
```
(start.last - start.cur)/T = 1 - (start.cur - start.first)/T   // 因 1-elem block: start.last=start.first+T
```
代入 → 这个子表达式 = `std::deque::size() + 1`（恒带 +1 偏置）。紧跟的字面 `- 1` 把偏置消掉 → **`dequeSize - 1 == 真实 size()`**。即 `-1` 不是抵消 sentinel，是抵消 +1 偏置。

- index magic `248037625 = 329^-1 mod 2^32`（buildNodeTree 32-bit MADD W，T>>3=329）。
- progress_inner@0x6C106C 退出项(@0x6C1230 / 0x6C12D8)用 64-bit 变体：`0xE719AD850EC8C0F9=+1/329`、`0x18E6527AF1373F07=-1/329`，语义等价，同样 `项 = real_size`，循环 i∈[1, real_size-1]=全部非 root 节点。
- 数值模拟 S=1,2,5,20 全部验证 `exit-term == real_size`；S=1(仅root) → term=1 → 不进 walk。

## 受影响的等价循环上界（全部 = real_size，遍历 index 1..real_size-1，跳过 root）
buildNodeTree 后处理@0x6B531C/0x6B5374、progress_inner@0x6C12D8、advanceRootAndNodes@0x6B7398、reseek@0x6B9200、preProgressDirtyNodes@0x6B6920、resetAndReleaseNodes@0x6b5850。本地正确上界 = `i < _nodes.size()`。`i+1<size()` 会丢最后一个真实节点（运行时旁证：yuzulogo 468 mismatch）。

## v17 = 新节点 0-based deque index
buildNodeTree_recursive @0x6b4ce0：`v17 = size_before_push - 1`(同 +1 偏置-1 抵消)，正好是即将填充节点的 index（root=0, children=1,2,..）。指针在 loop-top @0x6b4b84-90 push 前采样。

IDB 注释已就地更正（0x6b531c / 0x6b4ce0 / 0x6cf17c），idb_save 完成。
