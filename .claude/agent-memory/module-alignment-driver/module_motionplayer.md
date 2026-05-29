---
name: module-motionplayer
description: motionplayer 模块对齐审计状态（截至 2026-05-29 review-only 评估）
metadata:
  type: project
---

motionplayer 模块（cpp/plugins/motionplayer/，约 24.6K LOC，40+ 文件）当前处于"中等成熟度"对齐阶段。

**Why:** 该模块已有 2026-04-05 的完整 misalignment 报告（analysis/MotionPlayer_EmotePlayer_Misalignment_Report.md），随后做过较大重构——EmotePlayer 现已正确委托给内部 `Player _player`（对应二进制 EmoteObject→Player 关系），setVariable 已实现 9-case 类型分发（对应 sub_671228），多数 P1 字段（outline/meshline 改 ttstr、priorDraw 改 double、setScale 引入 baseScale×userScale）已修复。

**仍未对齐的根本问题：**
1. **Player 内存布局未对齐**：本地仍用 `shared_ptr<PlayerRuntime>` + 70+ 扁平字段（Player.h），与二进制的 1384 字节 flat 结构差异巨大。所有"按偏移访问 +200/+1592/+1656"的 getter/setter 在本地通过命名字段实现，等价但结构不同。
2. **NCB 入口名映射**：libkrkr2.so 中 EmotePlayer NCB 注册在 `EmotePlayer_ncb_registerMembers` (0x67FAC8)、Player 注册推测在 emoteplayer_entry (0x682528) 周围；2026-04-05 报告引用的 0x6D69C8 是早期符号，IDB 已演化。
3. **物理引擎缺失**：startWind/setOuterForce 仍是字段写入，未创建 1564 字节风模拟器对象，bust/h/parts 三路力源未分发。

**How to apply:** 下一次推进该模块时优先级：
- P0：Player 类布局重构（先 class-layout-auditor 锁定字段顺序，再拓扑分批 fix），否则所有方法对齐都建立在等价但结构不同的地基上
- P1：物理引擎（WindSimulator + OuterForce 三路）补全
- P2：load() / EmoteObject_init (0x67DBAC) 的 3 层对象创建链
- 不要再单独修方法体——继续逐方法 fix 会把"方法等价但容器不一致"的差异越积越多
