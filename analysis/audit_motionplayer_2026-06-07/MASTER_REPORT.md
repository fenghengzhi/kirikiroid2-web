# MotionPlayer 全量二进制对齐审计 — 主报告（2026-06-07）

> 日期：2026-06-07（HEAD = dev/motion @0b62dc3）
> 方法：把 `cpp/plugins/motionplayer/` **全部 75 个文件 / ~39,600 行**（含 `internal/`）切成 14 个功能簇，每簇一个 `binary-alignment-auditor` 子 agent，**树状递归**反编译 libkrkr2.so 对应函数（每簇 agent 自身递归进子函数），对当前 HEAD 代码做六维（源码结构 / 数据流 / 调用链 / 对象生命周期 / 内部容器实现 / 边界行为）逐行对比。
> 性质：只读审计（除各 agent 顺手的 IDB rename/comment + idb_save）。各簇明细见 `clusterA..N_*.md`。
> 前序：[../audit_motionplayer_2026-05-30/MASTER_REPORT.md](../audit_motionplayer_2026-05-30/MASTER_REPORT.md)

## 簇 → 文件覆盖映射（75 文件全覆盖，无遗漏）

| 簇 | 主题 | 主要文件 |
|----|------|---------|
| A | Emote 控制器组（11 controller + spring + mesh） | EmoteAngle/Blink/BlinkRng/Eyebrow/Loop/Mouth/Selector/Var/Wind/Spring/MeshResolver |
| B | EmoteEngine 核心 | EmoteEngine.cpp/.h |
| C | EmotePlayer 类 + NCB 暴露面 | EmotePlayer.cpp/.h |
| D | main.cpp NCB + D3DEmotePlayer + Motion namespace + D3D 适配 | main.cpp, D3DAdaptor, D3DEmoteModule.h, MotionNodeBridge |
| E | Player 类核心 + 生命周期 + 资源入口 | PlayerCore.cpp, Player.h, PlayerInternal.h, PlayerResource.cpp |
| F | Motion 加载 + NodeTree + MotionNode | PlayerMotionLoad.cpp, NodeTree.cpp/.h, MotionNode.h, internal/* |
| G | 帧进度引擎 frameProgress | PlayerFrameProgress.cpp（2747） |
| H | Timeline + 帧步进端口 | PlayerTimeline.cpp, PlayerFrameStep(.cpp/.h), PlayerFrameStepping(.cpp/.h) |
| I | updateLayers 评估 + geometry + anchor | PlayerUpdateLayers(.cpp/Internal.h), PlayerUpdateLayerEval.cpp, PlayerUpdateGeometry.cpp, PlayerUpdateAnchor.cpp |
| J | 渲染管线 part1：execute + internal + dispatch | PlayerRenderExecute.cpp, PlayerRenderInternal.cpp/.h, PlayerDrawDispatch.cpp, PlayerRender.cpp |
| K | 渲染管线 part2：items 构建 + targets + layer query | PlayerRenderItems.cpp, PlayerRenderTargets.cpp, PlayerLayerQuery.cpp |
| L | 变量系统 + 4 内联 HashMap 级联 | PlayerVariable.cpp, internal/value_structs.h, internal/ttstr_hash.h |
| M | 粒子系统 + childMotion | PlayerUpdateParticles.cpp, PlayerUpdateChildMotion.cpp |
| N | 资源 + SourceCache + GLL + SLA + 支撑 | ResourceManager, SourceCache, PrivateMotionGLL, SeparateLayerAdaptor, RuntimeSupport, MotionTraceWeb |

---

## 总结论：**地基与多数子系统已对齐；剩余偏差集中在「渲染 execute/build 阶段放置」+ 少量局部数据流，已无 2026-05-30 那批架构级 P0。**

相比 2026-05-30「未达成 100%，上层系统性偏差」，本轮发现**代码自那以后大幅演进，旧报告的绝大多数 P0 已被消除或被新反编译证伪**。当前状态：

| 维度 | 状态 |
|------|------|
| 对象布局 / 字段语义 / vtable / 生命周期（裸指针 new/delete） | ✅ 普遍对齐 |
| 内部容器实现 | ✅ **重大修正**：4 个 Player HM 经字节核实**本就是 libstdc++ `unordered_map<ttstr,…>`**（非自研内联 HM）；node/controller 用 `std::deque` 是源码选型对齐。**2026-05-30「STL 替代 KiriKiri 内联容器」这条跨簇系统性根因被证伪** —— libkrkr2.so 本身就用 STL，本地选型正确 |
| NCB 暴露面（EmotePlayer/D3DEmotePlayer/Player/Motion namespace） | ✅ 三类 + namespace 成员集**逐条 1:1**（69/54/92 成员，故意 alias 全保留）；旧「暴露面重组」P0 已消除 |
| 数据流 / 调用链 | ⚠️ 残留：渲染 execute/build 阶段放置（item+424/+20 闩、leaf/composed 双 Rb_tree pre-walk pass）、firstFrame `_deltaTime` 读序、Player::isAnimating 委托 |
| 边界行为 | ⚠️ 少量局部：mesh `if(meshType!=0)` 门控缺、calcBounds 漏 `!_preview` gate、particle Pass2 用错 dt 字段 |

**差分现状**：logo 用例 trace/render compare 0 mismatch（非回归守护）。本审计暴露的剩余偏差多对 logo fixture **oracle-inert**（emote 角色动画 / anchor / accurate-SLA / 变量级联路径，logo 不触发）——按 CLAUDE.md，oracle-inert 不降低其作为「未达成 100%」证据的地位，但也意味着无现成 fixture 验证修复。

---

## 一、跨簇汇聚发现（多 agent 独立命中，最高优先）

### ★ C1 — firstFrame 块读取过期 `_deltaTime`（player+592 赋值位置错位）
**簇 G / I / M 三处独立命中，已主控实证核实并校准严重度。**

- **二进制**：`progress_inner@0x6C106C` 在**入口** 0x6C1094 即 `*(a1+592)=speedMul*dt`，**先于** firstFrame 块 0x6C1108 读取它。
- **本地**：[PlayerFrameProgress.cpp:2312](../../cpp/plugins/motionplayer/PlayerFrameProgress.cpp#L2312) 才赋 `_deltaTime=_speedMul*actualDelta`，位于 firstFrame 块（2237–2297，块内 line 2296 `return`）**之后**。故 firstFrame 路径 line 2241 读到的是**上一帧残值（首帧为 0.0）** → 影响 reverse-from-end seed（2245 `deltaTime<0`）与 reverseSeekFlag 方向（2253）。
- **修法**：把 `_deltaTime = _speedMul * actualDelta;` 上提到入口（firstFrame 块之前），与二进制 0x6C1094 对齐。
- **⚠️ 严重度校准（主控核实）**：簇 I 报告「`_deltaTime` **从未赋值**、anchor gate `==0` 恒真→anchor 永不运行」是**夸大**。根因是其 grep 用了 `_deltaTime *=`（乘等于）模式，匹配不到 `_deltaTime = `——正是 CLAUDE.md 反复警告的 **negative-grep 假阴性**。实际 2312 行每（非首）帧赋值且 `_deltaTime` 是持久成员，anchor（在 progress 之后跑）能看到有效值。真实问题仅为簇 G 的**首帧读序错位** + 簇 I-2 的 phase1 用 `_frameLastTime`（未缩放）而非 `_deltaTime`（缩放）、簇 M3 的 particle Pass2 同样用错 `_frameLastTime`。属局部，speedMul=1 时 inert。

### ★ C2 — 渲染 execute/build 阶段放置（2026-06-07 修复轮：P1-I3 / I4 已证伪，仅 J1/J7 残留）
> **2026-06-07 修复轮纠正（runtime Frida + 本地代码 + 主控独立复读三方一致）**：
> - **P1-I3 证伪**：本地 `Player::buildRenderCommands`（[PlayerRenderExecute.cpp:13](../../cpp/plugins/motionplayer/PlayerRenderExecute.cpp#L13)）**就是 0x6C4E28 execute pre-walk 的 counterpart**（Frida `PLAYER_BUILD_COMMANDS_OFF=0x6C4E28` + 通篇注释 + 从 PlayerRenderTargets.cpp 渲染入口调用）。requireLayerId/item+20 闩（行 81-112）**已在正确的二进制函数（0x6C4E28 LABEL_28）内**。簇 K 把本地函数名 "buildRenderCommands" 误映射成 "build 阶段/0x6C2334"，得出幽灵偏差；其 grep "0x6C2334 不写 +424" 正确，但推论"本地移到 build"错。
> - **I4 证伪/churn**：`struct PreparedRenderItem : NativeRenderItemFields`（RuntimeSupport.h:304）**本就是单个继承对象（一次分配）**；压平成扁平 432B struct 违反 CLAUDE.md 字节布局工作法（432B 是 ARM64 ABI 产物，不需对齐）。非偏差。
- **残留真偏差 = J1/J7 → ✅ 2026-06-07 已忠实复刻**：二进制渲染是**双函数双层管线**（0x6C4E28 leaf pre-walk emitter Loop A 的 drawable 体把 affineCopy/meshCopy/bezierPatchCopy 画到 leaf 层 item+304 + Loop B composed compose；0x6C7440 顶层 submit），leaf(key item+424)/composed(key item+56) 层挂在 SLA 两棵 `std::map<int,Layer>` Rb_tree 上。原本地把 leaf-copy 折进 `executeLayerRenderCommands` 的 `buildItemOutput` 递归、用 port-invented `_renderLayerStates` 近似。**修复**：新增 `emitLeafLayerCopyLike_0x6C4E28`（Loop A drawable 体，经 SLA `resolveRenderLayerNodeLike_0x6C6B48` 物化 item+304 到 `_managedTargets` Rb_tree）+ `composeGroupLayersLike_0x6C4E28`（Loop B，item+324），`buildRenderCommands`(=0x6C4E28) 在 drawable 分支调 leaf-emit、循环后调 Loop B；`executeLayerRenderCommands`(=0x6C7440) 改为 **submit-only**（消费预建 leaf/composed，不再 rebuild）；删除 port-invented `_renderLayerStates`。J6（leaf 新层 absolute=node+160+node+164 然后 ++node+164）随结构对齐落地。复刻中据二进制纠正 3 处 Loop B 精度（union 取 child **paintBox** 非 clipRect；EMPTY 用 camera-clamp / SIZE 用 viewport-narrow 双 tuple；child+320 gate 经跨函数 grep 证恒 0 无 producer → 以恒假 gate 忠实复刻、不发明本地字段）。**验证（最强非回归级）**：build 绿（web+wasmtime）、trace 两 logo PASS、build_flow item-field **0-mismatch**、**1008/1008 execute_post PNG 逐字节与基线相同**（证 logo 唯一走的 DIRECT 路径未动）。**诚实缺口**：leaf/buffered 路径无 fixture 触发，PNG-identical 是非回归守护而非该 inert 路径正确性证明（无法捏造 fixture，按 CLAUDE.md 标注）。
- accurate-SLA 持久树（J8）+ accurate composite drawMeshFrame 子路径仍仅存在于 `KRKR2_WASMTIME_HEADLESS` 诊断 guard 内，属另一条「缺失架构」（见 N1），**不在本次 scope**。
- **2026-06-07 同轮已落地**：J4（删 0x6C7440 中无二进制依据的 `renderLayer->Update(false)`）+ J9（preview 不透明度 `>>1`），同样 build 绿、差分 0-mismatch、PNG 一致。
- **改动文件**：PlayerRenderExecute.cpp（+403/-276）+ Player.h（+24 声明）。未 commit，保留工作树供复验。

---

## 二、按严重度的剩余「未达成 100%」清单（本轮 fresh 反编译）

### P0 — 真数据流分歧 / 子系统缺失
| # | 簇 | 二进制 @addr | 本地 | 摘要 |
|---|----|-------------|------|------|
| N1 | J/K | 双层管线 0x6C4E28 + 0x6C7440 + leaf/composed 双 Rb_tree | PlayerRenderExecute/Items | accurate-SLA composite 子路径（0x6C9CA8 LABEL_85 后 acquireLayerById+assignImages+drawMesh/Bezier）本地仅 leaf 分支；execute/build 阶段放置不 1:1（见 C2）|
| N2 | C | EmotePlayer #50 animating + D3DEmotePlayer animating → Player_isAnimating 0x673F98 | EmotePlayer.cpp | animating 误委托到 getAllplaying 0x6CCE34（不同函数）；二进制是 3 controller bucket + 5 哈希表扫描。需先递归移植 Player::isAnimating |
| N3 | C | frameLastTime/lastTime sub_681E94 读 Player+1128 RAW | EmotePlayer.cpp | EmotePlayer 两 getter 二进制同读 +1128 无转换；本地 frameLastTime 读 `_frameLastTime`、lastTime 读 `_loopTime` 且带 `*1000/60`（注意与 Motion.Player 带转换的 6D9420/6D9448 是两套，勿混）|

### P1 — 局部数据流 / 边界行为（可在现数据流上修，无需重构）
| # | 簇 | 二进制 @addr | 本地 | 摘要 |
|---|----|-------------|------|------|
| N4 | G | progress_inner 入口 0x6C1094 | PlayerFrameProgress.cpp:2312 | firstFrame 读过期 `_deltaTime`（见 C1）；并删 2238-2239/2592-2593 的 port-invented `_allplaying/_syncActive` 覆写（二进制无）|
| N5 | G | 入口 emoteMode gate 0x6C10A4 | PlayerFrameProgress.cpp 入口 | 缺 `if(_directEdit) initEmoteMotion(2)`（directEdit 路径，initEmoteMotion 本身 port TODO）|
| N6 | I | phase1 root velocity 0x6BB38C/0x6BB400 | PlayerUpdateLayerEval.cpp:938/949 | phase1 用 `_frameLastTime`（未缩放）应为 `_deltaTime`（缩放）+ 缺 `&&_frameLastTime>0` subgate（speed=1 inert）|
| N7 | F | mesh field gate 0x6b4198 | NodeTree.cpp:185-190 | 读 meshSyncChildMask/meshDivision **缺 `if(meshType!=0)` 门控** |
| N8 | F | meshCombine node+1964 @0x6b4238 | NodeTree.cpp | meshCombine 字段二进制有 writer，本地**未读取** |
| N9 | I | calcBounds type3/type4 递归 0x6C3D04 | PlayerRenderItems.cpp:187/193 | 两递归臂二进制都有 `!_preview`(player+1092) gate，本地漏 |
| N10 | M | particle Pass2 child step | PlayerUpdateParticles.cpp:808 | 用 `_frameLastTime`，二进制用 `_deltaTime`(a1+592)（childMotion 路径已修，粒子路径漏修）|
| N11 | M | BLOCK1 prevM 更新 | PlayerUpdateParticles.cpp | prevM 二进制独立于 childCount，本地折进 `childCount>=1` if → childCount==0 矩阵滞后 1 帧 |
| N12 | M | emission trigger slot+736 | PlayerUpdateParticles.cpp:366 | selector 用 port-only `pn.prtTrigger`（node 镜像无 trigger，无二进制依据）；应读 slot+736 |
| N13 | H | reseek 末尾 Player_initNodeTimeline | PlayerFrameStepping.cpp:587 | 二进制对每个 node(idx≥1) 种 slot 游标；本地只对 hasChild node 跑（!hasChild init DEFERRED，无 fixture）|
| N14 | J | 尾部 `renderLayer->Update(false)` | PlayerRenderExecute | 二进制 0x6C7440 尾部无 Update（仅 setClip+release）|
| N15 | J | absolute = node.x+y 0x6C6B48 | PlayerRenderExecute | 本地用 `_nextLayerAbsolute++`，二进制 leaf 层 absolute=node160+node164 |
| N16 | E | setStealthChara / setChara dedup 0x6C0E9C/0x6B29C0 | PlayerCore.cpp | setStealthChara 纯赋值缺 dedup/+968-guard/+776-flush；setChara 缺 +960 同步、且多清了 _motionKey |

### P2 — 注释 / 命名 / 顺序 / inert（不影响布局）
| # | 簇 | 摘要 |
|---|----|------|
| N17 | E | **2026-07-23 纠正**：旧建议把 `_tjsRandomGenerator` 从 +992 改标 +676 仍然错误。+676/+716 是 render descriptor/color 对象（ctor 0x6CED30，0x6CF080 建立 `color`关系）；+992 是 RM dispatch，真 RNG 在 `ResourceManager_ctor@0x6A88CC` 的 RM+144。PlayerCore.cpp:535 setChara 注释仍引旧误名 |
| N18 | B | ctor COLOR seed：二进制 xmmword_14D68D0={128,128,128,255}f（get_bytes 确认），本地留零+TODO，现可确认应 seed |
| N19 | C/D | D3DEmotePlayer NCB property/method 注册顺序：二进制交错，本地 property 提前（ininert，应按 0x52E504 重排）；'progress' #50 cb 二进制是 EmoteEngine_progress 0x52f76c，需确认 wrapper tail-call；MaskModeStencil/Alpha 二进制在 Motion namespace 也注册（0x6d9d24/0x6d9d3c），本地漏 |
| N20 | A | EmoteBlinkRng.cpp:62-71 next() 用两次独立 nextWord，二进制预减批量取双词（word2 在 v1==2 regen）；624 词边界 regen 时机差一次抽样（墙钟种子无 oracle）|
| N21 | N | RM unloadAll 真址 = 0x6A8CF8。2026-07-19 后续纠正：ObjSource clip/ensureTexture/drawLayer 与 raw owner/node/texture 生命周期已闭合，旧 clip-STUB 结论已证伪。 |

---

## 三、重要纠正（推翻先前结论，已就地修 IDB/注释）

1. **「STL 容器替代 KiriKiri 内联容器」系统性根因被证伪（簇 L 字节核实）**：4 个 Player HM 经 byte-verify 本就是 libstdc++ `unordered_map<ttstr,V,ttstr_hash>`；+1296=`deque<VariableLabelScope>`、+408=`multimap<ttstr,Entry*>`。libkrkr2.so 自身用 STL，**本地选型正确**，不存在「STL→自研 HM」重构目标。这推翻 2026-05-30 MASTER 一/1。
2. **HM4 value 非 owning tTJSVariant*（簇 L 证伪 2026-05-30 M4 + 旧 clusterJ）**：`clearHM3_HM4@0x6B8118` 释放的是 `node+8`=**KEY ttstr**，value@node+16 是裸 double。本地 `unordered_map<ttstr,double>` 正确。已 IDB 注释纠正。
3. **node-index map 用 RAW label 非 path-key（簇 F 证伪 2026-05-30 M5）**：`buildNodeTree_recursive@0x6B4A6C` insert key = `propGetByName(L"label")` RAW；`buildNodePathKey@0x6B5C1C` 的 2 caller 都喂 HM3(+1184)，从不喂 Player+24。本地 `_nodeLabelMap[widen(label)]` 正确，path-builder 存在（RuntimeSupport.cpp:1241）。已就地纠正 IDB 旧误注释 + 更新 memory。
4. **chara setter / loopTime getter 误名证伪（簇 E）**：chara setter 真身 = 0x6C0E9C（2026-05-30 误把 0x6D94B0 当它，实为 stealthChara setter mode16）；loopTime getter=0x6D9448（scalar），数组 getter 0x6D139C 实为 variableKeys。「setChara 重播 dispatch」说法不成立（是 dedup helper 0x6B29C0）。
5. **2026-05-30 cluster G「整个 node-deque 帧步进核心缺失」过时（簇 G/H）**：该核心已 1:1 端口（progress_inner 入口 + LABEL_48 双向 wrap、advanceRootAndNodes 4-stream、parse/merge gate byte-exact）；旧 SEVERE 表针对的是已被替换的旧 STL 时间线状态机。
6. **node+1504=accumulated.dirty 证实（簇 M 证伪旧疑虑）**：MotionNode 字段序 dirty(+1504)/active(+1505)/visible(+1506) 正确，childMotion skip-gate 对齐。
7. **D3DEmotePlayer「NEEDS ARCHITECTURAL REWORK」P0 已消除（簇 D）**：54 成员表已重建到二进制精确形状，6 个故意 alias 全保留，M6 namespace attach 回归（f50f197）已修。

---

## 四、平台边界（已确认合法，注明技术原因，非偏差）

- **Player_findSource@0x6948E8 上传原语**（簇 N，2026-07-18 纠正）：外层资源记录与 Win/KRKR 两张内表、AddRef/Release、unload 生命周期均已复原。Win/spec=2 与 KRKR/spec=1 都直接消费 mapped record 内的 raw `PSBFile` 节点；KRKR 复原 all-group 枚举、两种 RL、palette 与透明 2x2。Web 纹理不能对非零 offset 逐子矩形更新，因此 KRKR atlas 先在 CPU 组整页再一次 Update；只有这一上传原语是具体平台边界。
- **PSB C++ helper vs TJS dispatch**（簇 F C2）：部分 ctor/字段读走 PSBDictionary helper 而非 `iTJSDispatch2::PropGet`；偏移对、dispatch 链不同。属 load 层政策，需全模块统筹（非局部）。
- **RuntimeSupport.cpp / MotionTraceWeb.cpp**（簇 N）：Web-port MotionSnapshot 资源模型 + logo-trace 诊断 shim，无 1:1 二进制函数（二进制直接 load→TJS dict）。端口宿主层。
- **D3D 桩**（簇 D）：D3DAdaptor 多数成员 = 二进制 nullsub_81..86，本地空 stub 忠实；removeAllTextures（二进制有真体 sub_6AD8B8）缺 PLATFORM_BOUNDARY 注释（N21 类）。
- 渲染：无 per-vertex 顶点色 → 4-corner bake、纹理按 (name,color) 缓存。
- clipRect float[4] 已于 2026-07-23 在生产字段与 Wasmtime harness 同步修正；原簇 J5 类型偏差关闭。

---

## 五、递归发现的子函数待办（树状递归未展开到底的叶子）

- **Player_isAnimating 0x673F98**（簇 C，N2 依赖）— 3 controller bucket + 5 哈希表扫描，未移植。
- **EmoteMeshResolver 引擎体 0x660028**（簇 A）— ~1925 行 DFS，入口已确认，引擎体需独立 vertical。
- **bind-loop callees sub_67C560/67C6B0/67C8A8**（簇 B）。
- **var-track writer sub_6B786C/6B7A70、pushSync/Action body 6B6294/6B638C、initNodeTimeline 非 child 路径**（簇 H，N13）。
- **childRoot Player +1064 建模**（簇 L L-1，getVariable cascade 在 childRoot 上，本地在 this；独立 Player 时值同源故 inert）。
- **variableKeys(engine+1208)/modifyRoot(+1584)**（簇 C 的历史 open；后续状态见主审计）。
  `initPhysics` 已于 2026-07-19 由 `0x67FAC8 → 0x67D4D0` 注册证据关闭：它就是
  metadata/controller builder，不是独立 physics STUB。
- **EmotePhysics_springStep / hair/bust 引擎进一步 callee**（簇 A/B，step 数学已交叉核对）。

---

## 六、IDB 改善（本轮各 agent 已 idb_save）
rename + comment 约 30+ 处：`Player_findSource@0x6948E8`、`Player_acquireLeafLayerById@0x6C6B48`、`Player_acquireComposedLayerById@0x6CBCE4`、`Player_renderToCanvas@0x6C7440`、`Player_renderAccurateSLA@0x6C9CA8`、SLA map insert 0x6DCD0C、EmoteSelectorController_ctor@0x66E398、buildNodeTree key 误注释纠正 0x6b4ce4、clearHM3_HM4 value 语义纠正 0x6B8118、chara setter off-by-one 纠正、'progress' cb 0x52f76c、_deltaTime firstFrame gap 注释等。明细见各 cluster 文件末「IDB 修正记录」。

---

## 七、对「是否 100% 一比一复原」的回答

**尚未 100%，但远比 2026-05-30 接近，且性质已变。** 2026-05-30 的失败是**架构级**（容器选型、暴露面、帧步进核心整体走不同拓扑）；本轮证明那批架构问题**绝大多数已被代码演进解决，或本就是旧报告/旧 memory 的误判**（STL 选型、path-key、HM4 owning、chara 误名 5 处大翻案）。当前剩余「未达成 100%」收敛为两类：

1. **渲染 execute/build 阶段放置 + accurate-SLA 双 Rb_tree 持久树**（C2 / N1）—— 唯一仍需「先重构数据流再修」的架构项，值 0-diff 但 phase 不 1:1，必须 CI 下统筹，禁止打补丁。
2. **一批局部数据流 / 边界 gate 缺失**（N2–N16）—— 多数可在现数据流上**加性修复**，但多对 logo fixture oracle-inert，无现成 fixture 验证（按 CLAUDE.md 仍照实复刻、标注验证缺口，**不**从零造 fixture）。

六维逐簇结论：源码结构 ✅ / 对象生命周期 ✅ / 内部容器实现 ✅（重大修正）/ NCB 暴露面 ✅ / 数据流 ⚠️（渲染阶段放置 + 局部 dt/gate）/ 边界行为 ⚠️（少量 gate 缺失）。
