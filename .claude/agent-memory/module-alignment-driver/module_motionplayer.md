---
name: module-motionplayer
description: motionplayer 模块对齐审计状态（截至 2026-05-29 review-only 评估）
metadata:
  type: project
---

motionplayer 模块（cpp/plugins/motionplayer/，约 24.6K LOC，40+ 文件）当前处于"中等成熟度"对齐阶段。

**Why:** 该模块已有 2026-04-05 的完整 misalignment 报告（analysis/MotionPlayer_EmotePlayer_Misalignment_Report.md），随后做过较大重构——EmotePlayer 现已正确委托给内部 `Player _player`（对应二进制 EmoteObject→Player 关系），setVariable 已实现 9-case 类型分发（对应 sub_671228），多数 P1 字段（outline/meshline 改 ttstr、priorDraw 改 double、setScale 引入 baseScale×userScale）已修复。

**已完成的对齐里程碑（截至 2026-05-29）：**
- **5f2c846** "Align motion::Player class layout"（P0 字段顺序基础）
- **4a2cf6e** "Restore EmotePlayer 4-level heap object pointer chain"（D3DEmotePlayerNativeInstance → EmoteObject → EmoteEngine → Player 拓扑）
- **75dd72e** "relocate controller/animator containers to EmoteEngine (P0-1)"：5 个 `std::deque<VariableAnimatorState>` @ EmoteEngine +256/+336/+416/+576/+656 + `std::unordered_map<std::string, VariableAnimatorState> _variableAnimators` @ +1384 全部在 EmoteEngine.h:70-78 声明完毕，容器类型与二进制 1:1；Player.h 仅保留 `_engineBack->` 间接访问的 helper 方法，**这些字段在 Player 上 0 残留**
- **2026-05-29 commit f0fd57** P0 完成：HM2 锚点 + 删除 `_mirrorPositiveCache/Negative` Web 侧 memoization

**仍未对齐的根本问题：**
1. **Player 内存布局未对齐**：本地仍用 `shared_ptr<PlayerRuntime>` + 70+ 扁平字段（Player.h），与二进制的 1384 字节 flat 结构差异巨大。所有"按偏移访问 +200/+1592/+1656"的 getter/setter 在本地通过命名字段实现，等价但结构不同。
2. **HM3 (+1184) 完全缺失**：696B per-node-path 状态缓存（node_path_ttstr → mesh拷贝+PSB引用+TJS callback 表），本地以 `layerId → renderLayerStates` 模型替代，数据模型差异巨大；缺 `Player_pruneHM3_byNodeIdentity` @ 0x6B826C 节点身份验证机制
3. **HM4 (+1240) 缺失**：variable→label alias 反查表，本地走 O(N) 线性扫 `variableLabelEntries`
4. **NCB 入口名映射**：libkrkr2.so 中 EmotePlayer NCB 注册在 `EmotePlayer_ncb_registerMembers` (0x67FAC8)、Player 注册推测在 emoteplayer_entry (0x682528) 周围；2026-04-05 报告引用的 0x6D69C8 是早期符号，IDB 已演化。
5. **物理引擎缺失**：startWind/setOuterForce 仍是字段写入，未创建 1564 字节风模拟器对象，bust/h/parts 三路力源未分发。

**How to apply:** 下一次推进该模块时优先级：
- P1（高 ROI、纯查漏补缺）：清空 `analysis/Player_Class_Layout_Alignment_TODO §6 🔬` 反编译清单（+864/+408/+1296/+484../+636/+676 等字段的语义/类型确认）
- P2（大工程）：重构 per-layer state 模型 — `layerId → state` 改为 `node_path_ttstr → 696B value`，引入 `Player_buildNodePathKey` @ 0x6B5C1C 等价物 + per-frame `pruneHM3_byNodeIdentity` 调用点
- P3：物理引擎（WindSimulator + OuterForce 三路）补全
- P4：HM4 alias map 引入 `std::unordered_map<std::string, tTJSVariant*>` 消除线性扫描
- 不要再单独修方法体——继续逐方法 fix 会把"方法等价但容器不一致"的差异越积越多

**容器迁移核查方法（防止重复误报）：** 在认为某个 `_xxx` 字段"应迁出 Player"之前，先 `grep -n "_xxx" cpp/plugins/motionplayer/Player.h` 确认 Player.h 是否真的声明该字段——很多容器**仅出现在 .cpp 的 `_engineBack->_xxx` 访问点**，并不在 Player 上。
