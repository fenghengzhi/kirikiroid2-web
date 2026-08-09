# MotionPlayer 全量二进制对齐审计 — 主报告

> 日期：2026-05-30
> 方法：从 **libkrkr2.so 一侧**枚举 motionplayer 模块全部函数（0x52E000–0x700000 命名 ~230 个 + 大量 sub_），切成 12 个簇，每簇一个 `binary-alignment-auditor` 子 agent 反编译对比本地 `cpp/plugins/motionplayer/`。
> 性质：只读审计（除 1 处已修 EmoteAngleController_step），各簇明细见 `clusterA..L_*.md`。
> 关联：[MotionPlayer_Restoration_Review_2026-05-30.md](../MotionPlayer_Restoration_Review_2026-05-30.md)

## 总结论：**未达成 100% 还原。地基稳固，上层系统性偏差。**

| 维度 | 状态 |
|------|------|
| 对象布局 / 字段偏移 / vtable / 大小 | ✅ 普遍对齐（Player 1384B、EmoteEngine 1496B、各 controller POD、EmotePlayer 24B 壳）|
| 对象生命周期（裸指针 + 手动 new/delete）| ✅ 普遍对齐（例外见 P0 表 EmoteObject unique_ptr）|
| **内部容器实现** | ❌ 系统性用 STL（unordered_map/deque/vector/list）替代 KiriKiri 内联哈希表/deque |
| **数据流 / 调用链** | ❌ 多个核心子系统走不同拓扑（progress 帧步进、getVariable 级联、findSource、alpha-mask）|
| **NCB 类暴露面** | ❌ EmotePlayer / D3DEmotePlayer / Player 三个类的成员集与二进制不符（成员错配 + 类边界搬移）|
| 边界行为（默认值/分支门控）| ⚠️ 部分缺失（progress dt 门控、anchor damp 公式、particle 双层门控）|

**差分现状**：logo 用例 trace compare 0 mismatch 全绿（m2logo/yuzulogo 逐层一致）。**这只覆盖非 emote logo 渲染路径**——本审计暴露的多数 P0 在 emote 角色动画 / 变量级联 / 资源缓存 / alpha-mask 路径，差分测试当前不覆盖，故"全绿"不等于"已还原"。

---

## 一、跨簇系统性根因（不是逐行 bug，是架构选择）

这些在 ≥6 个簇重复出现，是"未达成 100%"的主因。按 CLAUDE.md「不接受功能等价」永远算偏差，但**差分绿、回归风险高，必须分阶段 CI 验证，禁止盲改**：

1. **STL 容器替代 KiriKiri 内联容器**（A/B/E/F/G/J/K/L 全部命中）
   - 4 个 Player 内联 HM（+264/+320/+1184/+1240，prime-bucket 单链 ttstr key）→ `std::unordered_map`
   - 节点/控制器 deque（stride 2632 / 160 / 0xA48 块）→ `std::deque`
   - 改变桶分布、迭代顺序、节点生命周期链 → 影响依赖插入序的遍历（EmoteEngine bind-loop、getLoopTime Array、isAnimating 桶扫描）。
2. **C++ PSBDictionary helper 替代 TJS dispatch**（F/H/J/K）：二进制大量通过 `iTJSDispatch2::PropGet(player+528 / node 对象)` 读 PSB；本地直接读 PSB struct/cache。偏移多数对，但 dispatch 链/对象生命周期不同。
3. **NCB 类暴露面重组**（C/D/E/K，历史缺口，现已修复）：二进制 `Motion.EmotePlayer`
   (`0x67FAC8`, 70 成员 + 2 常量，含曾漏读的 UTF-16 `activateSelectorTarget`) /
   `D3DEmotePlayer`(`0x52E504`, 54 成员) / `Player`(`0x6D69C8`, 92 成员) 是三套独立 API。

---

## 二、按严重度的主问题清单（每项注明簇 + 证据地址 + 本地位置 + 回归风险）

### P0 — 真数据流分歧（输出错 / 子系统缺失）

| # | 簇 | 二进制 @addr | 本地 | 摘要 | 风险 |
|---|----|-------------|------|------|------|
| M1 | G | progress_inner 0x6C106C + advance/rewind 0x6B6ADC/0x6B9A3C + parseFrame/merge 0x6926B4/0x692AB0 | PlayerFrameProgress.cpp / PlayerUpdateLayerEval.cpp | **2026-07-19 更新：**旧 STL Player timeline map/control animator 已删除，raw node-deque/frameList 两槽 parse/merge、前进/后退/reseek 与 +480/+592/+1120/+456 主游标链已接回；仍需闭合原版 per-frame renderList owner 与本地 `_nodes` 门控的容器差异 | 中 |
| M2 | B | EmoteEngine progress 0x67D01C（6 deque step→HM7 + bind-loop）+ stepHairParts 0x67B748 + stepBust 0x67BCE8 | EmoteEngine.cpp:243-314 | 6 个 deque step 全 STUB_WARN；缺 `if(dt!=0)` 门控；post-loop 用原始 dt 非残余 dt；bind-loop 遍历 HM7 插入序链（本地空体）；hair/bust 物理未实现 | 高 |
| M3 | J | getVariable 0x533E1C + HM1 cascade 0x6CD39C + evalKey_cascade 0x6CD23C | PlayerVariable.cpp | getVariable 实为 scope-gate→HM1 join("scope::label")→HM2 fallback / HM4-first→HM1 fallback 的级联；本地只读 `_evalResultValues`+frame ranges，无 HM1/HM4/scope-join | 高 |
| M4 | J | clearHM3_HM4 0x6B80E4 + evalKey_cascade | Player.h `_dispatchAliasMap` | HM4(+1240) 是 **owning ttstr→tTJSVariant\***（dtor 逐个 Release，eval 读 node+16 为 double）；本地建模成 non-owning `iTJSDispatch2*` 别名图且未用 → 数据流错 | 中 |
| M5 | F | buildNodeTree_recursive 0x6B4A6C + buildNodePathKey 0x6B5C1C(**MISSING**) | NodeTree.cpp | 二进制按**层级 path ttstr**（`parent/child/..`）做节点索引/stencil/HM3 key；本地按扁平 PSB label → 重名碰撞、值串。path-key builder 本地完全缺失 | 高 |
| M6 | K | doAlphaMaskOperation 0x6AF104 | 缺 / main.cpp:279 | 整个 alpha-mask op 缺失（shader cache + CPU `dst.a=src.a*dst.a/255` + 边界 fillRect）；且本地把它误挂在 Player 而非 namespace | 中 |
| M7 | H | anchor 0x6C0528（type10） | PlayerUpdateAnchor.cpp:36 | dampPow 公式错：二进制 `dt*(..)/v27/60/node+2432`，v27=player592/player1168；本地用 `abs(frameLastTime)/60/anchorDamping`。且 w/h 走 PSB dispatch，本地走 cache | 中 |
| M8 | H | calcBounds 0x6C3D04 | PlayerRenderItems.cpp:32 | 二进制**递归** type3 子动作 + type4 粒子子节点；本地是扁平 | 中 |
| M9 | K | ObjSource 0x69CCB8 / RM 0x6AB8BC / findSource 0x6948E8 | ResourceManager/PlayerResource | **【2026-07-23 再纠正】** 2026-07-19 的 raw mapped-record/ObjSource owner 结论保留；旧 CLOSED 漏掉 `0x6F1060→0x695DE8` 第二调用者、item→SourceState alias 与 getter 后 rect 重读。上述链及解码分支边界现已补齐；KRKR 整页上传为 Web API 边界，但未审计余部不得外推为全局 100% | AUDITED SITES + BOUNDARY |
| M10 | L | particleSystem splice 0x6C1A00 + childMotion 0x6BE2C0（`sub_6F363C` 父←子 drawlist 拼接）| PlayerUpdateParticles.cpp:791 | 二进制把子 drawlist 拼进父 +936；本地无此 splice → 粒子/子精灵可能丢失（除非经 cluster I 渲染路径已覆盖，需核） | 中 |
| M11 | D | D3DEmotePlayer 成员集 0x52E504 + contains 0x530B5C | main.cpp:443-530 | 54 成员表对不上（TimelinePlayFlagDifference 名错、"clear"绑 create cb、5 处故意 name/cb 别名未复刻、~28 EmotePlayer 风格属性多注册）；contains 本地自造 AABB 重载 | 中 |

### P1 — 结构/类型/容器/边界

| # | 簇 | 二进制 | 本地 | 摘要 |
|---|----|--------|------|------|
| M12 | A | EmoteAngleController_step 0x666634 | EmoteAngleController.cpp | ✅ **已修**（本对话）：无 fall-through / phase 不在 setup 重置 / 完成存 1.0 / 6.2832 截断常量 |
| M13 | A | EmotePhysics_springStep 0x662768 | 缺 | 弹簧物理整体未实现（诚实 stub） |
| M14 | B | EmoteObject_destroy 0x67F420 | EmotePlayer.h:310 | `unique_ptr<EmoteObject>` vs 二进制裸 new/delete（CLAUDE.md 硬规则违反，独立可修）|
| M15 | E | Player ncb 0x6D69C8 (92 成员) | main.cpp | 24 成员缺（flip/opacity/visible/slant/zoom/angleDeg/coordinate/bounds/setCoord/clear/contains/onAction/onSync...）；70 本地多出（多属 D3DEmotePlayer 暴露面被上提到 Player）|
| M16 | E | setChara 0x6D94B0 / setTickCount 0x6D96C0 / setAngleDeg 0x6CD0EC / getLoopTime 0x6D139C | PlayerCore/Variable | 这些"看似标量"的 accessor 二进制有副作用（重播 dispatch、clamp、emoteMode 分支、返回 **TJS Array**），本地是平凡赋值/标量 |
| M17 | E | initVariables 0x6CD750 | PlayerVariable.cpp | 二进制 driven by player+528 ttstr-dispatch PropGet(variable/label/scope)→推 160B 项进 +1296 deque；本地读 PSB struct 进 std::vector |
| M18 | J | HM2 +320 key | Player.h:872 | ttstr key vs 本地 std::string（P1-2，仍 open）|
| M19 | F | initNodeFields 0x6B3C78 / MotionNode_initFields 0x6F19B4 | NodeTree.cpp/MotionNode.h | TJS dispatch vs PSBDictionary helper（偏移对，dispatch 链不同）|
| M20 | H | setFlip/Slant/AngleRad/RootOpacity/RootVisible 0x6C0F1C.. + applyTranslateOffset 0x6D5264 + purgeNodeLabelMap 0x6CDE18 | PlayerUpdateGeometry / 缺 | root 节点 setter 二进制写 root(+200) accum 槽 + dirty；本地写 viewport 标量（API 面不同）；后两者本地缺 |
| M21 | L | childMotion 门控 0x6BE270 / particle BLOCK1 0x6BF314 | PlayerUpdateChildMotion:38 / Particles:213 | skip-gate 测 node+1504 vs 本地 `accumulated.dirty`；粒子双层门控被压平 |
| M22 | I | render item owner + clipRect | RuntimeSupport.h / MotionNode.h | 2026-07-23：clipRect 已统一为 float[4]；item 改为 MotionNode 持久 owner + caller-stack pointer lists。`new(0x1B0)` 仅为 Android ABI 尺寸，不是 wasm32 padding 目标 |

### P2 — 注释/语义/方法体顺序（不影响布局）

| # | 簇 | 摘要 |
|---|----|------|
| M23 | E | **2026-07-23 纠正**：旧“`_tjsRandomGenerator` 实为 +676”结论已证伪。+676/+716 是 render descriptor/color 对象；+992 是 RM dispatch，Player::random 经其调 RM.random；真 RNG 在 RM+144 |
| M24 | B | progress deque 迭代顺序注释 #8/#9 写反；color seed xmmword_14D68D0 未读（TODO）|
| M25 | I | sub_6CBCE4 本地误名 "buildRenderTree"，实为 acquireLayerById(Rb_tree<int,Layer>)；sub_6C4E28 是 execute 阶段 emitter 非 build |
| M26 | G | evaluateTimelines_guess 实为 teardown sweep，命名误导 |

---

## 三、重要纠正（推翻先前结论）

> **2026-07-23 superseded correction：**下面第 1 项对“本地 `skipFlag1` 反相存储 +
> `_renderItemInheritedFlag18` 侧挂”的描述仅反映 2026-05-30 历史实现。当前由
> `appendPreparedRenderItems(..., inheritedFlag18)` 直接传递 a6，item+18 按原极性存入
> 历史遗留名称 `skipFlag1`，harness 直接输出；无侧挂、无反相层。

1. **build_flow / skipFlag1 / item+18 实际已对齐**（簇 I 反编译证明）。`sub_6C2334 @0x6c33c0`：`item+18 = inheritedFlag18 || (node+48!=0)`；`node+48 = priorDraw`（`sub_6BC4F0 @0x6bc6c4` 证明：仅 forceVisible 时 = `priorDraw&1`）。本地 `PlayerRenderItems.cpp:477` `skipFlag1 = !(inheritedFlag18||(node.priorDraw!=0))` **逐位精确**。m2logo items[1] frame12+ 残余 mismatch 不是公式 bug，而是 build/execute **阶段放置**差（rawFlag20 闩锁位置），低优先。
2. **requireLayerId(node+16/+20) 提前物化不是偏差**（簇 F）：二进制 `buildNodeTree_recursive` 0x6B4D24/0x6B4DBC 本就每节点 2× requireLayerId，与本地 NodeTree.cpp:102-105 一致。render-build 的 `requireLayerId`(item+424) 是**第三个**独立 layer-id。先前 review 的 build_flow 担忧对 node+16/+20 而言是误判。
3. **EmotePlayer 不是 finalize-only**（簇 C）：`EmotePlayer_loadClass 0x685BC0` 同时调
   classInit(注册 finalize) + ncb_registerMembers(`0x67FAC8`, 70 成员 + 2 常量) 进同一类 →
   `Motion.EmotePlayer` 有完整引擎 API。推翻旧 finalize-only 结论。
4. **P0-2 / P2-4（EmoteEngine 6 map 字节块 + applyVarControllers 顺序）已修**（簇 B 复核确认）。

---

## 四、修复执行计划（按风险/独立性分层）

### 第 1 层 — 安全、隔离、已反编译验证（可直接做，不碰绿差分）
- [x] **M12** EmoteAngleController_step 控制流（本对话已修）
- [ ] **M14** EmoteObject unique_ptr→裸指针（CLAUDE.md 硬规则，独立）
- [ ] **M23/M24/M25/M26** 注释/命名纠正（需各自反编译确认偏移后改注释）
- [ ] **M13** EmotePhysics_springStep 实现（隔离 POD 函数，反编译 0x662768 后实现）

### 第 2 层 — 子系统级，需 binary-alignment-fixer + CI（中风险）
- **M2** EmoteEngine progress + hair/bust（先反编译 setVariable 写入路径定 HM value 类型 → 实现 6 step + bind-loop）
- **M6** doAlphaMaskOperation 实现 + 移到 namespace
- **M7/M8** anchor damp 公式 + calcBounds 递归
- **M9/M11** ObjSource/RM/D3DEmotePlayer 成员集重建（先精确枚举二进制成员表）
- **M16/M17** Player accessor 副作用 + initVariables dispatch 源

### 第 3 层 — 架构重构，必须 module-alignment-driver 主循环统筹 + 分阶段 CI（高风险，禁止盲改）
- **M1** progress 帧步进机重架（从 progress_inner 向下，含 advance/rewind/parse/merge）
- **M3/M4/M18** getVariable 级联 + HM1/HM4 语义 + HM2 key 类型 + 4 内联 HM 容器
- **M5/M15** NCB 三类暴露面重组（EmotePlayer/D3DEmotePlayer/Player 类边界）
- **M5(F)** path-key 节点索引重建（buildNodePathKey + re-key 3 个 map）
- 跨簇容器层：STL→KiriKiri 内联 HM/deque（保插入序，影响 M2 bind-loop / getLoopTime / isAnimating）

### 不应盲目推进的理由（CLAUDE.md + 先前 review 一致）
- 差分当前全绿，第 2/3 层多数改动会触碰渲染/变量/帧路径，本地无 Android oracle 无法验证。
- 必须「先重构使数据流与伪代码一一对应，再修」，禁止在架构不一致基础上打补丁。
- 每个第 2/3 层项独立分支 + differential CI 验证不破坏 trace/render compare（0 mismatch 基线）。

---

## 五、IDB 改善（本次各 agent 已 idb_save）
重命名 + 注释约 60 处：EmoteEngine_dtor/progress 真体 0x67D01C、Player_emitRenderItem_requireLayer 0x6C4E28、node+48=priorDraw、HM3/HM1 helper 组、Motion_*_ncb_registerMembers、updateLayers 三个 pass（childMotion/particleEmitter/particleSystem）、anchor/camera node pass 命名纠正等。明细见各 cluster 文件末。
