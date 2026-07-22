---
name: player-pimpl-split
description: [已过时-2026-05-30] PlayerRuntime pimpl 已在 A10 删除,容器全部内联进 motion::Player 本体;EmoteEngine 用 raw Player* + new/delete 持有。保留作历史。
metadata:
  type: project
---

# [过时] motion::Player pimpl 拆分 — A10 已移除

**2026-05-30 复审**: `shared_ptr<PlayerRuntime>` 拆分**已不存在**。
A10 阶段把 PlayerRuntime 所有字段 hoist 进 `class Player` 本体(Player.h:616-807 直接成员)。
RuntimeSupport.h:329 注释确认 "PlayerRuntime struct deleted after Phase A1-A9"。

## 2026-05-30 当时架构（历史快照）
- `class Player`(Player.h)直接内联全部容器: `std::deque<MotionNode> _nodes`,
  6×`std::unordered_map`, 多个 `std::vector`, `std::map<string,int> _nodeLabelMap`,
  `std::list<EvalResultEntry>` 等 —— **不再有 pimpl 指针**
- `EmoteEngine` 用 `Player* _player = new Player(...)` (EmoteEngine.cpp:40) +
  `delete _player` (EmoteEngine.cpp:106) — raw ptr + manual new/delete,对齐二进制 `new(0x568)`
- 子 Player 同样 `new Player(...)` (NodeTree.cpp:230, PlayerUpdateParticles.cpp:446)

## 2026-07-23 correction

下列旧容器映射不再是现行结论：Player-owned live `std::vector<PreparedRenderItem>` 已删除；每个 `MotionNode` 持久拥有一个 `PreparedRenderItem*`，draw 的 main/aux `vector<PreparedRenderItem*>` 位于 caller stack。Player+936/+944 是独立的 44B-element DEAD child render aggregate，本地由 `_childMotionRenderAggregate` 复刻。Player+384 的 56B-element 容器语义仍须从自身 producer/consumer 独立确认，不能再命名为 live render list。

## 历史容器选型判断(权威表见 [[player-container-layout]])
- 6 个 std::unordered_map ↔ 二进制 4 个 KiriKiri inline HM(+264/+320/+1184/+1240) → ⚠️ 语义对齐容器不同
- std::deque<MotionNode> ↔ KiriKiri deque(+184, node 2632B) → ⚠️
- ~~std::vector<PreparedRenderItem> ↔ 裸数组 stride 56B(+384)~~ → 已证伪，见上方 correction
- ~~std::vector<VariableLabelEntry> ↔ 裸数组 stride 44B(+936)~~ → 已证伪；+936 是 DEAD aggregate

权威字段表: [[player-1384b-flat-spec]]。
