---
name: player-progress-inner-loopTime-invariant
description: Binary invariant - Player_progress_inner @0x6C106C reads player+1136 (loopTime) 5 times but NEVER writes it; the value is the FIXED clip->loopTime boundary set once at PlayerCore.cpp:557
metadata:
  type: project
---

Player_progress_inner @0x6C106C 内 +1136 (loopTime, double) 5 reads / 0 writes — 经 2026-05-30 独立反编译确认。

**5 个读位置**：
- 0x6C1364 — REVERSE 入口 v28
- 0x6C13E4 — FORWARD at-end gate v31
- 0x6C143C — REVERSE 分支 C wrap pre-loop v32
- 0x6C147C — FORWARD at-end raw `LDR X8,[X19,#0x470]` + `STR X8,[X19,#0x1C8]`（int64 bit 拷到 +456，等价于 double 赋值）
- 0x6C14BC / 0x6C14C4 — FORWARD wrap 循环体（同一 LDR 被复用）

**Wrap 公式**：
- FWD (0x6C14C4): `v7 = v7 + +1136 − +1128`，while `+1128 <= v7`，写回 +1120 + +456（不写 +1136）
- REV (0x6C1454): `v7 = v7 − +1136 + +1128`，while `+1136 > v7`，写回 +1120 + +456（不写 +1136）

**Why**：+1136 是 clip->loopTime 边界常量。之前 web port 误把 `_loopTime += actualDelta` 放在 progress_inner 入口（commit pre-e11ecef），会让 +1136 漂出 +1128 范围，强制 forward-at-end 走 LOOP 而不是 STOP，并悬挂 do-while。e11ecef 已移除该写入。

**How to apply**：审计任何 progress / loop-wrap 相关代码时，把 "+1136 在 progress_inner 内只读不写" 视作强不变量。如果遇到本地代码对 `_loopTime` / `player+1136` 写赋值（含 +=、=、memcpy 等），且声称在 progress_inner 路径上发生，几乎肯定是偏差。clip->loopTime 真正的写入点在 PlayerCore.cpp:557 / clip 装载路径，不在 progress 路径。
