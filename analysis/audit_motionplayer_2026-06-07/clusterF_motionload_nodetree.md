# CLUSTER F 复审 — Player motion load + NodeTree 构建 + MotionNode

> 2026-06-07。权威来源：libkrkr2.so（IDB libkrkr2.so.i64，本轮 hexrays_ready）。
> 范围：cpp/plugins/motionplayer/ {PlayerMotionLoad.cpp, NodeTree.cpp, NodeTree.h,
> MotionNode.h, internal/value_structs.h, internal/player_containers.h, internal/ttstr_hash.h}。
> 协议：decompile -> 伪代码 -> 本地对照 -> 六维对比。本轮交叉核实并**推翻 2026-05-30 报告 F1/F2/F3**。

---

## ⛔ 头号结论：2026-05-30 报告的 F1/F2/F3（P0 path-key）已被本轮反编译证伪

2026-05-30 报告称（最大 P0）：「Player+24 node-index map 应由层级 path ttstr 作 key，
经 buildNodePathKey@0x6B5C1C 构建；本地用扁平 label 是 wrong key space；path-builder 缺失（F2 MISSING）」。

**本轮独立反编译（buildNodeTree_recursive @0x6B4A6C，指令级）证明该结论错误**：

```
0x6b4ca8  Motion_propGetByName(&v30, &v33, L"label", ...)   ; v30 = RAW PSB "label"
0x6b4ce0  v17 = deque::size()_before_push - 1               ; 新节点 0-based index
0x6b4ce4  *Player_nodePathMap_lowerBoundInsert(a1+3, &v30) = v17  ; a1+3 = Player+24, key=&v30=RAW label
```

- PropGet(0x6b4ca8) 与 insert(0x6b4ce4) 之间 **无任何 `BL Player_buildNodePathKey`**。key 就是 RAW label。
- `Player_nodePathMap_lowerBoundInsert @0x6B50B8`：`a1 = Player+24`，比较 `i[4]`(=pair ttstr key) 用 sub_9B1ED0（UTF-16 lex），是 std::map<ttstr,int> RB-tree。
- `xrefs_to(0x6B5C1C)` = **仅两个调用者**：0x6b2e08（resetMotionState loop3）、0x6b84c4（pruneHM3）。**两者都喂 HM3（Player+1184），从不喂 Player+24**。

**当前本地代码已经正确**（且自带反编译依据注释，NodeTree.cpp:110-130）：
- `_nodeLabelMap[widen(label)] = index`（NodeLabelMap = std::map<ttstr,int,ttstr_utf16_less>）—— RAW label key，对齐 0x6B50B8。
- path-key builder **存在**：`buildNodePathKeyLike_0x6B5C1C`（RuntimeSupport.cpp:1241），且有消费者：
  HM3 restore（PlayerFrameProgress.cpp:1749）、HM3 populate（PlayerFrameProgress.cpp:1897）。F2「缺失」不成立。
- HM3 populate（resetMotionState loop3）**已实现**且 path-key 化（PlayerFrameProgress.cpp:1889-1902），
  门控顺序 `joinTarget(node+46) -> nodeType mask 0x19D` 精确对齐 0x6b2dcc/0x6b2df8。

**IDB 修正**：0x6b4ce4/0x6b4ce0 旧注释仍写"key=buildNodePathKey full path"（被证伪产物），本轮就地改为
正确结论 + 证伪依据；idb_save 完成。

---

## 审计结论（本簇范围）：⚠️ 部分偏差（无 P0；剩余为已记录 STL/TJS-dispatch 容器策略 + 2 处局部门控）

旧报告唯一的 P0（path-key）经核实**不存在**。本簇当前无 P0。剩余偏差分两类：
(A) 容器/访问架构策略（std::deque vs KiriKiri inline deque、PSB C++ helper vs TJS dispatch）——已记录、CLAUDE.md ≥P1；
(B) 两处 initNodeFields 局部门控偏差（mesh 子键无条件读、meshCombine 缺失）——可在原数据流直接修复。

---

## 逐项对比

| 检查项 | 二进制行为 @addr | 本地实现 | 状态 |
|---|---|---|---|
| node-index map key | RAW label ttstr，std::map<ttstr,int> @0x6b4ce4/0x6B50B8 | `_nodeLabelMap[widen(label)]` NodeLabelMap | ✅ 对齐（旧 F1/F3 证伪）|
| path-key builder | buildNodePathKey @0x6B5C1C，仅喂 HM3 | buildNodePathKeyLike_0x6B5C1C（RuntimeSupport.cpp:1241）有调用者 | ✅ 存在（旧 F2 证伪）|
| HM3 populate path-key | resetMotionState loop3 @0x6b2e08，gate joinTarget+mask0x19D | PlayerFrameProgress.cpp:1889-1902，门控顺序一致 | ✅ 对齐 |
| node+46 语义 | joinTarget bool，唯一 writer @0x6b3ef0 propGetBool("joinTarget")&1 | MotionNode.h:60 joinTarget；NodeTree.cpp:161 | ✅ 对齐（非 visible）|
| parentIndex node+36 | STR a2 @0x6b4bf8；递归传子自身 index | node.parentIndex=parentIdx；walkTree 传 thisIdx | ✅ 对齐 |
| newNodeIndex 计算 | deque size-1 ptr 算术 @0x6b4ce0 | nodes.size()-1（append-only 等价）| ⚠️ 容器模型分歧（值等价）|
| 双 requireLayerId | 2× FuncCall idx16 L"requireLayerId" @0x6b4d24/0x6b4dbc -> node+16/+20 | dispatchRequireLayerId()×2，NodeTree.cpp:105-106 | ✅ 对齐（域/时序/计数）|
| 节点容器 | KiriKiri inline deque，operator new(0xA48)+initFields @0x6F1914 | std::deque<MotionNode> emplace_back | ⚠️ STL 策略（≥P1，已记录）|
| MotionNode 字段初始化 | MotionNode_initFields @0x6F19B4 手动偏移清零+node+1400=&empty ttstr+sub_699390 | C++ default member init | ⚠️ 容器模型分歧（终态等价）|
| PSB 字段读取 | iTJSDispatch2::PropGet/FuncCall（v47 vtbl+32）@0x6B3C78 | PSB::PSBDictionary C++ helper（nodeTreePsb*）| ⚠️ TJS-dispatch 架构（≥P1）|
| meshTransform 子键门控 | `if(v21!=0)` 才读 meshSyncChildMask/meshDivision/meshCombine @0x6b4198 | NodeTree.cpp:183-190 **无条件**读 meshSyncChildMask/meshDivision | ❌ 缺 meshTransform!=0 门控 |
| meshCombine | node+1964 = propGetBool("meshCombine") @0x6b4238（在 meshTransform!=0 内，HasProp 门控）| NodeTree.cpp **未读取** meshCombine | ❌ 缺失（meshCombineEnabled 字段在，无 writer）|
| type==3 preview gate | `if(*(a1+1092)) stencilType&=~4` @0x6b43a4 | walkTree parentPreview!=0（=当前 Player._preview）| ✅ 对齐（a1 全程不变）|
| type==3 child Player | new(0x568)+Player_ctor(child,a1+992)+CreateAdaptor->node+1912 @0x6b43c0 | new Player(RM)+CreateAdaptor->childPlayerVar | ⚠️ 子键属性传播部分（见下）|
| type==4 particle Array | sub_704CB8(TJSCreateArrayObject)->node+2296 @0x6b45e4 | TJSCreateArrayObject->particleArrayVar | ✅ 对齐（TJS Array dispatch）|
| stencilCompositeMaskList | type12+bit2，Player+24 find(RAW)->node+2600 vector + node+1961=1 @0x6B5388 | _nodeLabelMap find(RAW)+stencilCompositeMaskReferenced bool | ⚠️ key 对齐，存储 bool vs ptr-vector |
| Player+24 容器选型 | std::map<ttstr,int> RB-tree，cmp sub_9B1ED0 | std::map<ttstr,int,ttstr_utf16_less> | ✅ 选型+比较器对齐 |
| HM1-4 hash | KiriKiri UTF-16 hash：null `Ptr`→0；非零 Hint 复用；否则计算并写 Hint；非 null 计算结果 0→`0xFFFFFFFF` | 1025/6/9/32769/11 算术 mix 原本正确；旧 functor 的 null/Hint 缺口已于 2026-07-26 修复 | ✅ 当前对齐（旧 “byte-for-byte” 结论已 superseded） |

**2026-07-26 hash 纠正证据**：fresh decompile `0x6F52AC`、`0x686944`、`0x6F2674`、
`0x689760`、`0x6885CC`、`0x6E2060/0x6E2150/0x6E2484/0x6E2574` 交叉证明上述完整
functor 语义。旧实现直接调用 `s.c_str()`，会把 null `Ptr` 折叠为空串且完全跳过 backing rep
的 `Hint@+68`；因此本报告原先的 “byte-for-byte” 判定不成立，仅算术 mix 判定仍成立。

---

## 偏差详情（可直接修复，B 类）

### ❌ D1 — meshTransform!=0 子键门控缺失（NodeTree.cpp:182-190）
二进制 0x6b4194 写 node+2000=meshTransform，**0x6b4198 `if(v21)`** 才进入读 meshSyncChildMask(0x6b41b8)/
meshDivision(0x6b41d8)/meshCombine(0x6b4238)。本地无条件读 meshSyncChildMask/meshDivision。当 meshTransform==0
但 PSB 仍带 meshSyncChildMask/meshDivision 键时，本地会污染 meshFlags/meshDivision（二进制保持 0）。
**修复**：把 NodeTree.cpp:185-190 的两读包进 `if (node.meshType != 0) { ... }`。

### ❌ D2 — meshCombine（node+1964）未读取
二进制 0x6b4238（在 meshTransform!=0 且 HasProp("meshCombine") 内）：`node+1964 = propGetBool("meshCombine")&1`。
MotionNode.h:88 有 `meshCombineEnabled`（注释标 node+1963，应为 node+1964）但 NodeTree.cpp 无 writer。
**修复**：在 D1 的 `if(meshType!=0)` 块内加 `if((*psbNode)["meshCombine"]) node.meshCombineEnabled = propGetBool;`
（HasProp 门控，缺键不写）。同时复核 meshCombineEnabled 偏移注释 1963 vs 1964（二进制是 +1964）。

---

## 架构性偏差（🔧 — 已记录的 STL/dispatch 策略，非本簇可局部修复）

均为全 Player 范围的容器/访问策略，已在 value_structs.h / player_containers.h 标注，CLAUDE.md 判 ≥P1 但非 boundary：
- **C1 节点容器**：std::deque<MotionNode>（RAII）替代 KiriKiri inline deque（operator new(0xA48)+手动 initFields/destroy）。
  构造点已核实 @0x6F1914 pushBlock = libstdc++ deque push_back 展开。容器**选型对齐**（deque），元素生命周期模型分歧。
- **C2 PSB 访问**：nodeTreePsb*（PSBDictionary C++ cast）替代 iTJSDispatch2::PropGet/FuncCall（@0x6B3C78 全程 v47 vtbl+32）。
  字段→偏移映射正确，访问架构非 TJS-dispatch。
- **C3 MotionNode 字段**：C++ default member init 替代 MotionNode_initFields @0x6F19B4 手动偏移清零序列
  （含 node+1400=&byte_1C95298 空 ttstr rep + sub_699390）。终态按字段等价。

这些不靠局部 patch 解决；属全模块容器政策，留待统一重构（module-alignment-driver 级）。本簇内不计新 P0。

---

## 子函数对齐状态（本轮反编译）

- `Player_buildNodeTree_recursive` 0x6B4A6C — ✅ 反编译核实；node-index map=RAW label（证伪旧 F1/F2/F3）。
- `Player_buildNodePathKey` 0x6B5C1C — ✅ 反编译核实；仅 HM3 消费；本地 buildNodePathKeyLike 已实现且对齐。
- `Player_nodePathMap_lowerBoundInsert` 0x6B50B8 — ✅ 反编译核实；Player+24 std::map<ttstr,int>，cmp sub_9B1ED0。
- `Player_initNodeFields` 0x6B3C78 — ⚠️ 反编译核实；字段映射对齐，2 处局部偏差（D1/D2）；TJS-dispatch 访问（C2）。
- `MotionNode_initFields` 0x6F19B4 — ⚠️ 反编译核实；手动偏移清零 vs RAII（C3）。
- `Player_nodesDeque_pushBlock` 0x6F1914 — ⚠️ 反编译核实；operator new(0xA48) deque block vs std::deque（C1）。
- `Player_resetMotionState_clearAndRebuild` 0x6B2B7C — ✅ 反编译核实；loop2 HM4 / loop3 HM3 path-key 均已本地对齐。
- requireLayerId 双调用 @0x6b4d24/0x6b4dbc — ✅ 对齐（旧报告 F5 RESOLVED 仍成立）。

## 容器拓扑核实（按 CLAUDE.md：看构造点，非消费循环上界）
- 节点 deque 构造点 = pushBlock @0x6F1914（每块 operator new(0xA48=2632B)，1-elem/block）。
- resetMotionState 三个循环上界用 `0xE719AD85...` magic + `-1`/`-3` = libstdc++ `deque::size()` 对 2632B/160B 元素的
  内联展开（CLAUDE.md 记录的 STL size() 陷阱），**不是**源码 `size()-1` token，本地 `< size()` 正确，无 off-by-one。

## 20B 关键帧元素（数据契约）
- 本簇范围未触及 20B 曲线关键帧 POD 内部布局（属 ClipSlot/parseFrame 簇）；MotionNode 内 slot+640 mesh 元素为
  8B {float x,float y}，本地 std::vector<float> 平铺视图保留数据契约（MotionNode.h:186 已标注）。无偏差。

## 平台边界标注（本簇 // PLATFORM_BOUNDARY:）
- value_structs.h:6 — ttstr 16B(Android)/8B(Web) + STL sizeof 差异致 HM3/var-scope 字节大小不可对齐。逻辑 1:1（字段/dtor 序/refcount）。合法。
- player_containers.h:11 — std::unordered_map sizeof 32B(Web)/56B(Android) 致 sizeof(Player) 不可等。合法。
- RuntimeSupport.cpp:1236 — buildNodePathKey ttstr->std::string 物化（同字符序列）。合法。
（C1/C2/C3 容器策略**未**标 PLATFORM_BOUNDARY，故仍计为 ≥P1 偏差，非边界。）

## 修复建议（优先级）
1. **D1**（NodeTree.cpp:185-190）：mesh 子键读包进 `if(node.meshType!=0)`，对齐 0x6b4198 门控。
2. **D2**（NodeTree.cpp，D1 块内）：加 meshCombine HasProp 门控读 -> meshCombineEnabled；复核偏移 1963->1964。
3. C1/C2/C3 不在本簇修；标记为全模块容器政策重构项。
