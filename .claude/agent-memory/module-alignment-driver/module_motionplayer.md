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
- **EmoteEngine 内部容器布局 2026-05-30 已对齐（P0-2 阶段 A+B+C，本驱动亲自执行）**：
  - 10 个 80B std::deque @0..799、7 个 controller heap 对象 @1072..1120、Player* @1064、_dirty@1162、meshRatio@1168/1176 早已就位。
  - 2026-07-18 fresh decompile 纠正：内联容器是 **4 个 `unordered_map<ttstr,V>` + 3 个 `unordered_set<ttstr>`**（HM#1@824 mirror-hit、HM#2@880 mirror-miss、HM#4@1272 instant-variable）+ **4 个 `std::vector<ttstr>`**（@800/992/1016/1040），不是“7 map + 4 variant-pointer vector”。+800 由 mirrorControl@0x66F364 填充；+992/+1016 由 timelineControl@0x66F80C 填充；+1040 被 sub_67C560 作为 ttstr key vector 消费。
  - value 类型证据：HM#7@1440 = `<ttstr,double>` 完全验证；HM#3@936 的 node 总长 0x88B = next8+key8+**112B mapped value**+hash8，raw timeline element tTJSVariant 由 0x66F80C CopyRef，dtor=sub_683E40。旧“~104B/opaque[104]”结论已证伪。
  - 删除 `_bindListHead`/`EmoteBindListEntry`——它物理上是 HM#7 的 `_M_before_begin._M_nxt`，不是真字段。
  - **PLATFORM_BOUNDARY（用户已拍板接受）**：libc++ unordered_map header≠libstdc++ 56B，sizeof(EmoteEngine) 不再精确=1496B，选 typed 语义对齐而非字节级 1496B（同 Player STL 策略）。
- D3DEmotePlayer wrapper 的字段集（baseScale +40 / userScale +44 / visible +48 / smoothing +49）布局正确；缺 ≥56B 总尺寸下其余字段的反编译验证。

### B. Player 内存布局（1384B flat vs Web port shared_ptr 拆分）
- 本地 Player.h 仍以 60+ 命名字段平铺，**未尝试匹配 1384B 字节级 offset**——这是 PLATFORM_BOUNDARY 标注允许的（libc++ vs libstdc++ unordered_map 字节差异，详 player_containers.h header），但内部子结构（HM1/HM2/HM3/HM4 alias）已通过 player_containers.h + value_structs.h 在类型层面就位；
- HM1 (`EvalCascadeMap` @ +264)、HM2 (`LabelValueMap` @ +320)、HM3
  (`PerNodeLayerStateMap` @ +1184)、HM4 (`VariableSnapshotMap` @ +1240)
  均已按当前证据声明并进入实际数据流；旧 `DispatchAliasMap` 结论已证伪。
- HM3 已使用 raw node-path key，reset/restore/prune 路径会写入、查找并清理
  `_perNodeLayerStateMap`；HM4 由 var-track reset 写入
  `_variableSnapshotMap`，并由级联变量读取路径优先查询，不是死代码。

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
- 二进制 `_nodes` 是 `std::deque<MotionNode>` (2632B/element)；**本地 `_nodes` 自 commit 8cee351("A8") 起已是 `std::deque<detail::MotionNode>`**（Player.h:1063；MotionNode.h:3 头注释确认；无 vector-only 操作残留）——容器选型已对齐，2026-06-01 Stage D 评估确认无迁移工作。detail::MotionNode 字节尺寸不与 2632B 对齐属 PLATFORM_BOUNDARY allowed（CLAUDE.md "对象 ABI 偏移永不需对齐"）。**任何称 "本地 std::vector<MotionNode>" 的旧记录（含 M1_plan §1）已过时。**
- `_preparedRenderItems` 二进制是 `std::vector<RenderItem 56B>` @ Player+936/944；本地用 `std::vector<detail::PreparedRenderItem>`，element 大小未审计

**How to apply:** 下一次推进该模块时优先级：
- **P0（验证）**：对 EmoteEngine_ctor (0x67E38C) 全部 10 个 deque + 7 个独立 controller heap 对象做完整反编译，扩充本地 EmoteEngine.h 以包含全部容器
- **P1（高 ROI、纯查漏补缺）**：清空 `analysis/Player_Class_Layout_Alignment_TODO §6 🔬` 反编译清单（+864/+408/+1296/+484../+636/+676 等字段的语义/类型确认）
- **P2（继续核实）**：HM3 mapped value 已定型为 owning raw element +
  `EmoteTimelineData80B*` + blend controller + runtime scalar/vector 状态；后续逐项复核
  restore/prune 与时间线边界行为，不再把 owner 源类型列为未知
- **P3**：物理引擎（WindSimulator + OuterForce 三路）补全
- **P4**：继续反编译 HM4 var-track 的帧推进细节；容器本身及读写调用链已不是缺口
- **P5（对齐 vs 修正抉择）**：在 analysis/ 明确标注哪些 NCB binding "二进制 bug 需保留"哪些"语义正确版本"，避免反复来回修
- 不要再单独修方法体——继续逐方法 fix 会把"方法等价但容器不一致"的差异越积越多

**2026-05-30 review-only 增量发现：**
1. 单元测试入口路径已确认：tests/unit-tests/plugins/motionplayer-dll.cpp；macos debug preset 下 3/7 用例通过、205/209 assertions 通过 (98%)；4 个失败用例分别命中 isExistMotion 异常边界、pimg findSource 链、isTimelinePlaying 语义、logo.mtn 图层枚举。
2. 构建在 web/debug preset 下完全通过，无本模块编译错误。
3. EmoteObject_init (0x67DBAC) 是真正的 "setModule" 入口，执行 ResourceManager_loadResource + 读 metadata/base/chara/motion + 调 Player_play 的完整链。本地 EmotePlayer.cpp 的 setModule 仅赋 `_module` 字段，初始化逻辑分散在 setChara/setMotion 各自的 setter——测试用例 4 (logo.mtn 图层 0/15) 很可能就是此问题表征。
4. **2026-07-19 纠正：** `initPhysics` 不是 P3 物理空桩。`EmotePlayer_ncb_registerMembers@0x67FAC8` 将该字面名称直接绑定到 `EmoteEngine_applyMetadata_buildControllers@0x67D4D0`；本地现已接入完整 raw metadata builder。D3DEmotePlayer/Player 的同名空桩没有对应成员证据，已删除。其余 STUB 必须各自重新反编译，不能再由 `initPhysics` 推导。

**容器迁移核查方法（防止重复误报）：** 在认为某个 `_xxx` 字段"应迁出 Player"之前，先 `grep -n "_xxx" cpp/plugins/motionplayer/Player.h` 确认 Player.h 是否真的声明该字段——很多容器**仅出现在 .cpp 的 `_engineBack->_xxx` 访问点**，并不在 Player 上。

**测试入口/构建命令（2026-05-30 修正）：**
- macos debug 单元测试 target 名是 `motionplayer-dll`（不是 motionplayer-dll-tests）：`cmake --build out/macos/debug --target motionplayer-dll && ./out/macos/debug/tests/unit-tests/plugins/motionplayer-dll`
  - **基线（已用 git stash 验证）**：7 用例 3 通过 4 失败、209 assertions 205 通过。这 4 个失败（cpp:517/570/668/780）是**预存**的、与 EmoteEngine 布局无关（依赖未移植的 motion 加载/图层枚举/step 函数）。改 EmoteEngine 布局后结果不变 → 该模块布局类改动的回归基线就是"仍是 3/4 + 205/209"。
- web 构建：`cmake --build out/web/debug`（本次 64/64 通过）
- differential test 入口（如有）：tests/differential/wasmtime/motion_playback_wasmtime.cpp 通过 A10 暴露的 Player::activeMotion()/nodes()/preparedRenderItems*() accessors 工作

**EmoteEngine 反编译符号锚点（本次确认/已 idb_save）：**
- EmoteEngine_ctor @0x67E38C（1496B；7×M_next_bkt(ptr,10) 是 7 个 inline unordered_map 铁证；reset 顺序 134→135→137→136 = pos→scale→angle→color）
- EmoteEngine_dtor @0x67F4B8（EmotePlayer/D3DEmotePlayer 共用；按降序 offset 释放 7 map + 4 vector + 7 controller + Player + matrixHeap）
- EmoteEngine_progress @0x67D01C（deque-step 把输出 upsert 进 HM#7@1440；bind-loop 遍历 HM#7，
  实装 sub_67C560 Engine HM3/+1040 cascade、sub_67C6B0 raw mirror cache 与
  Player_bindParameterValue；旧“全是 stub”结论已被 2026-07-18 代码/反编译证据纠正）
- EmoteEngine_applyVarControllers @0x6766E0（顺序 pos→color→scale→angle，每个 apply 紧跟其 step；scale 写 *(this+1176)=1.0/(this+1168 * v)）
- ttstr_doubleMap_upsert @0x686944（HM#7 与 Player HM2@+320 共用；node new(0x20)=32B {next@0,ttstr_key@8 atomic-incref,value@16,hash@24}，返回 node+16）
- sub_683E40（HM#3 mapped value=112B；函数接收 node+8/key 地址，因此其 +8/+16/+28/+96 分别对应 value+0/+8/+20/+88，按序释放 nested owner、raw variant 与 heap）
- 待确认：xmmword_14D68D0（color reset 4-float seed，rodata，仅 ctor@0x67e9c4 引用；MCP 无直接读 rodata 字节工具，留 TODO）

**未收敛项（需用户决策）：**
- **阶段 C 保序问题**：bind-loop 依赖 libstdc++ unordered_map 的 _M_before_begin 插入序单链表；libc++ 不暴露此链。当前 bind-loop body 全是 stub（无可观测行为），故改为直接迭代 typed map 并加 PLATFORM_BOUNDARY 注释。**若未来移植 sub_67C560 等且脚本观察顺序，需重新评估 KiriKiri 内联 hashtable**——本次按计划"风险/工作量过大则停下出方案"未做。
- **value 类型已收敛**：HM#1/HM#2/HM#4 是无 mapped value 的 set，HM#5 是
  `EmoteVariableRange`，HM#6 是 `EmoteVarRef{type,index}`，HM#3 是完整
  `EmoteHM3Value`（raw element/timelineData/blend controller/runtime state）。
- **Player HM 6→4 映射（P1-3）**：属 Player 类、独立模块，本次按范围边界**未碰**。已确认 EmoteEngine HM#7 与 Player HM2@+320 共用同一 upsert/hash（ttstr_doubleMap_upsert）——这是两模块的耦合点，后续对齐 Player HM 时应复用此 helper 语义。
