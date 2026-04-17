---
name: motionplayer node 的 delta/override 块是架构关键
description: libkrkr2.so MotionPlayer node 结构在 +1584..+1660 有独立的 delta/override 块，TJS setter 写入，phase2 消费，这是无法用"单一 state + 双层合并"等价替代的架构特性
type: feedback
---

# 规则：motionplayer 累加逻辑必须承认 delta/override 块

**原因**: libkrkr2.so sub_6BB33C phase2 (0x6BB6A4..0x6BB700) 的向量 merge 明确是 `accum = delta ∘ accum` 模式：
- `*(v22+1544) = vmulq_f64(*(v22+1624), *(v22+1544))` — delta.scale * accum.scale
- `*(v22+1512) = vaddq_f64(*(v22+1592), *(v22+1512))` — delta.pos + accum.pos
- `*(v22+1576) = *(v22+1656) * *(v22+1576) / 255` — delta.opacity * accum.opacity / 255

delta 块由外部 setter (setLayerPosition / setLayerScale / setVariable) 写入，**持久化到被 phase2 消费**，消费后清 `+1584` dirty byte。

**应用方式**:
- 审计 `cpp/plugins/motionplayer/` 下任何涉及 node accumulated 的代码时，检查是否有独立的 delta 存储。
- 若本地 MotionNode 只有 `accumulated` 和 `localState`/`interpolatedCache` 字段、没有 delta override 存储，这是架构缺陷——TJS setter 的语义无处安放。
- 修复时优先重构数据布局加入 `node.delta`（对应 +1584..+1660）、`node.deltaDirty`（对应 +1584 byte），而非在 accumulated 上叠加多层变换来模拟。
- evaluateLayerContent 插值得到的 state 应进入 accumulated（对应 +1504..+1580），不应与 delta 共用字段。
