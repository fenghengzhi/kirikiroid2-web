---
name: module-motionplayer
description: motionplayer 模块对齐审计状态（截至 2026-05-30 review-only 评估）
metadata:
  type: project
---

motionplayer 模块（cpp/plugins/motionplayer/，约 24.6K LOC，40+ 文件）当前处于"中等成熟度"对齐阶段。

**Why:** 该模块已有 2026-04-05 的完整 misalignment 报告（analysis/MotionPlayer_EmotePlayer_Misalignment_Report.md），随后做过较大重构——EmotePlayer 现已正确委托给内部 `Player _player`（对应二进制 EmoteObject→Player 关系），setVariable 已实现 9-case 类型分发（对应 sub_671228），多数 P1 字段（outline/meshline 改 ttstr、priorDraw 改 double、setScale 引入 baseScale×userScale）已修复。

**已完成的对齐里程碑（截至 2026-05-30）：**
- **5f2c846** "Align motion::Player class layout"（P0 字段顺序基础）
- **4a2cf6e** "Restore EmotePlayer 4-level heap object pointer chain"（D3DEmotePlayerNativeInstance → EmoteObject → EmoteEngine → Player 拓扑）
- **75dd72e** "relocate controller/animator containers to EmoteEngine (P0-1)"：5 个 `std::deque<VariableAnimatorState>` @ EmoteEngine +256/+336/+416/+576/+656 + `std::unordered_map<std::string, VariableAnimatorState> _variableAnimators` @ +1384 全部在 EmoteEngine.h:70-78 声明完毕，容器类型与二进制 1:1；Player.h 仅保留 `_engineBack->` 间接访问的 helper 方法，**这些字段在 Player 上 0 残留**
- **2026-05-29 commit f0fd57** P0 完成：HM2 锚点 + 删除 `_mirrorPositiveCache/Negative` Web 侧 memoization
- **A8/A9/A10 系列**：把 PlayerRuntime 拆掉，_nodes / _preparedRenderItems / _nodeLabelMap 等容器全部上提到 Player 自身（commits a385aa6 / b6246a0 / 8cee351）

**仍未对齐的根本问题（按严重度排序）：**

### A. EmotePlayer 链路（已大幅修正）
- D3DEmotePlayerNativeInstance(24B) → EmoteObject(40B) → EmoteEngine(1496B) → Player(1384B) 4 级堆指针拓扑已就位
- 但 EmoteEngine 的内部布局并未对齐：二进制 EmoteEngine_ctor (0x67E38C) 在 a1[0..720] 范围内排了**10 个 80B std::deque**（10 个不同的动画/控制器收集器），然后 a1[133]=Player*、a1[134..140]=7 个独立堆分配的 controller 对象（0x80B/0x70B）。本地 EmoteEngine.h 仅声明 5 个 deque + 1 个 unordered_map + 若干 scalar，**缺约 5 个 deque 与 7 个独立 controller heap 对象**。
- D3DEmotePlayer wrapper 的字段集（baseScale +40 / userScale +44 / visible +48 / smoothing +49）布局正确；缺 ≥56B 总尺寸下其余字段的反编译验证。

### B. Player 内存布局（1384B flat vs Web port shared_ptr 拆分）
- 本地 Player.h 仍以 60+ 命名字段平铺，**未尝试匹配 1384B 字节级 offset**——这是 PLATFORM_BOUNDARY 标注允许的（libc++ vs libstdc++ unordered_map 字节差异，详 player_containers.h header），但内部子结构（HM1/HM2/HM3/HM4 alias）已通过 player_containers.h + value_structs.h 在类型层面就位；
- HM1 (EvalCascadeMap @ +264)、HM2 (LabelValueMap @ +320)、HM3 (PerNodeLayerStateMap @ +1184)、HM4 (DispatchAliasMap @ +1240) 容器**全部声明完毕**，但 HM3 的"per-node-path 696B 状态"在本地仍由 `layerId → renderLayerStates` 模型替代，**数据模型差异巨大**；缺 `Player_buildNodePathKey` @ 0x6B5C1C 等价物 + 每帧 `pruneHM3_byNodeIdentity` 调用点。
- HM4 alias 表存在但**未在任何函数中读写**——等同于死代码。

### C. EmoteEngine 物理引擎缺失
- 二进制有 7 个独立 controller 堆对象（0x80B/0x70B 各种），处理 wind/outer-force/bust/hair/parts 三路；本地把 startWind/setOuterForce 实现为简单字段写入。
- 二进制 sub_530A5C → sub_67D01C 完整物理引擎（步进 6 种控制器、上限 1.1s/step、风/弹簧/阻尼）**完全未实现**——本地 progress() 仅做时间累加 + 时间线状态机。

### D. NCB 注册偏差
- `D3DEmotePlayer_ncb_registerMembers` (0x52E504) 含**二进制复制粘贴 bug**：
  - `bustScale` 实际绑到 `get/setBodyScale`
  - `modified` (RO) 绑到 `getPlayCallback`
  - `queing` 绑到 `get/setBustScale`
  - `clear` 绑到 `D3DEmotePlayer_create`
  - `pass` 绑到 `addPlayCallback`
  - `partsScale` 绑到 `getPartsScale` getter + `sub_530120` setter
- 本地 main.cpp:496-583 全部"修正"为语义正确版本，**反向违反 1:1 复原目标**。
- `Player` NCB 注册（Player_ncb_registerMembers @ 0x6D69C8）属性顺序在 main.cpp:126-294 大致对齐，但部分属性名未对齐：本地 `queuing` 对应二进制 `queing` 拼写（同时段 D3DEmotePlayer 主类有此 bug；Player 主类是否也有需复核）。

### E. setVariable 类型分发已实现，但走错容器
- 本地 setVariableResolvedWeightLike_0x671228 (PlayerVariable.cpp:322) 实现了 case 0/1/2/3/4/5/7/8 类型分发，逻辑结构与二进制 sub_671228 一致；
- 但**写入路径走的是 Player::_evalResultValues**（std::unordered_map<string,double>），不是 EmoteEngine+1384 的 `_variableAnimators` 表；这跟 §A 缺的 5 个 deque + 7 个 controller heap 对象耦合。

### F. NCB 入口名/地址映射
- libkrkr2.so 中 EmotePlayer NCB 注册在 `EmotePlayer_ncb_registerMembers` (0x67FAC8)、Player 注册在 `Player_ncb_registerMembers` (0x6D69C8)、D3DEmotePlayer 注册在 `D3DEmotePlayer_ncb_register` (0x541D98) → `D3DEmotePlayer_ncb_registerMembers` (0x52E504)；
- 2026-04-05 报告引用的 0x6D69C8 仍准确，IDB 已演化但地址不变。

### G. 容器复刻冲突
- 二进制 `_nodes` 是 `std::deque<MotionNode>` (2632B/element)；本地 detail::MotionNode 尺寸未与 2632B 对齐（PLATFORM_BOUNDARY allowed）
- `_preparedRenderItems` 二进制是 `std::vector<RenderItem 56B>` @ Player+936/944；本地用 `std::vector<detail::PreparedRenderItem>`，element 大小未审计

**How to apply:** 下一次推进该模块时优先级：
- **P0（验证）**：对 EmoteEngine_ctor (0x67E38C) 全部 10 个 deque + 7 个独立 controller heap 对象做完整反编译，扩充本地 EmoteEngine.h 以包含全部容器
- **P1（高 ROI、纯查漏补缺）**：清空 `analysis/Player_Class_Layout_Alignment_TODO §6 🔬` 反编译清单（+864/+408/+1296/+484../+636/+676 等字段的语义/类型确认）
- **P2（大工程）**：重构 per-layer state 模型 — `layerId → state` 改为 `node_path_ttstr → 696B value`，引入 `Player_buildNodePathKey` @ 0x6B5C1C 等价物 + per-frame `pruneHM3_byNodeIdentity` 调用点
- **P3**：物理引擎（WindSimulator + OuterForce 三路）补全
- **P4**：HM4 alias map 引入实际用法消除死代码
- **P5（对齐 vs 修正抉择）**：在 analysis/ 明确标注哪些 NCB binding "二进制 bug 需保留"哪些"语义正确版本"，避免反复来回修
- 不要再单独修方法体——继续逐方法 fix 会把"方法等价但容器不一致"的差异越积越多

**2026-05-30 review-only 增量发现：**
1. 单元测试入口路径已确认：tests/unit-tests/plugins/motionplayer-dll.cpp；macos debug preset 下 3/7 用例通过、205/209 assertions 通过 (98%)；4 个失败用例分别命中 isExistMotion 异常边界、pimg findSource 链、isTimelinePlaying 语义、logo.mtn 图层枚举。
2. 构建在 web/debug preset 下完全通过，无本模块编译错误。
3. EmoteObject_init (0x67DBAC) 是真正的 "setModule" 入口，执行 ResourceManager_loadResource + 读 metadata/base/chara/motion + 调 Player_play 的完整链。本地 EmotePlayer.cpp 的 setModule 仅赋 `_module` 字段，初始化逻辑分散在 setChara/setMotion 各自的 setter——测试用例 4 (logo.mtn 图层 0/15) 很可能就是此问题表征。
4. STUB_WARN 残留：EmotePlayer.cpp 仍有 4 处 STUB（assignState / initPhysics / getOuterForce / pimg 相关），都是 P3 物理引擎缺失的直接后果。

**容器迁移核查方法（防止重复误报）：** 在认为某个 `_xxx` 字段"应迁出 Player"之前，先 `grep -n "_xxx" cpp/plugins/motionplayer/Player.h` 确认 Player.h 是否真的声明该字段——很多容器**仅出现在 .cpp 的 `_engineBack->_xxx` 访问点**，并不在 Player 上。

**测试入口/构建命令：**
- macos debug 单元测试：`cmake --preset "MacOS Debug Config" -DBUILD_TESTS=ON && cmake --build out/macos/debug --target motionplayer-dll-tests && ./out/macos/debug/tests/unit-tests/plugins/motionplayer-dll-tests`
- web 构建：`cmake --preset "Web Debug Config" && cmake --build out/web/debug`
- differential test 入口（如有）：tests/differential/wasmtime/motion_playback_wasmtime.cpp 通过 A10 暴露的 Player::activeMotion()/nodes()/preparedRenderItems*() accessors 工作
