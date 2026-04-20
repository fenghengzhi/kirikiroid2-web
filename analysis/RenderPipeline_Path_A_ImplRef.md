# Render Pipeline Path A — Port 实现参考 (Implementation Reference)

> 消化已有分析 + 新反编译 `sub_6D5164` + 对照 port 现状，给 port 重写工程师的可执行清单。
>
> 本文档是 **映射表** 而非反编译记录。除 `sub_6D5164` 外不复述反编译，直接引用：
> - `analysis/Player_Rendering_Architecture_libkrkr2so.md`（§3.3 / §5.2 / §6-7）
> - `.claude/agent-memory/ida-deep-analyzer/project_render_executor.md`
> - `.claude/agent-memory/ida-deep-analyzer/project_sub6C2334_render_list_builder.md`
> - `.claude/agent-memory/ida-deep-analyzer/project_render_pipeline_timing.md`
> - `.claude/agent-memory/ida-deep-analyzer/project_updateLayers_phase2_accum.md`
> - `.claude/agent-memory/ida-deep-analyzer/project_player_completionType.md`

---

## 1. 摘要

**libkrkr2.so Path A**：`Player_drawCompat (0x6D5FB8)` → `sub_6D5164 (0x6D5164)` → `sub_6C2334 (0x6C2334)` 按 `nodeType` 位掩码（`0x1441 / 0x1449`，bits {0,6,10,12} 或 {0,3,6,10,12}）决定节点是否进入 `mainList`，**完全不看 `stencilType`**，每个入列节点在 `node+1904` 写 RenderNode 指针、`node+1944` 置 1；后续 `Player_renderToCanvas → sub_6C7440` 只迭代 `mainList`。

**port 走错路**：`PlayerUpdateLayers.cpp:1625-1639` 把 `stencilType == 0` 当成 visibility 的 hard-gate 把 `drawFlag` 置 false，接着整个 render pipeline（`PlayerRenderPrepare` → `buildRenderCommands`）用 `drawFlag` 作为 top-level gate。这等于把 libkrkr2.so 里 **Path B (`sub_6BD8DC`)** 的侧写出物（`node+1960 drawFlag`）当成 Path A 的门。结果：m2logo / yuzulogo 里 `stencilType=0`、`nodeType=0` 的节点在二进制里应被 Path A 照样入 `mainList` 渲染，port 直接被 `drawFlag=false` 截断。

---

## 2. 字段对照表

libkrkr2.so RenderNode 的字段偏移在 `node` 结构上（Motion 节点本体，2632 字节）和在 RenderNode 上（`sub_6C2334` 分配的 0x1B0 字节）是两个空间，要分开看。

### 2.1 Motion node 侧（2632B node ↔ port `motion::detail::MotionNode`）

| offset | 语义 | port 成员 | 状态 |
|---|---|---|---|
| node+52 | `stencilType`（PSB 种子，运行时只读；仅 `sub_6B43B0` 在 type3+completionType!=0 清 bit 2） | `stencilType`/`stencilTypeBase`（L80-81） | 已有；未提交 diff 已去掉错误的每帧 OR |
| **node+1092** | `Player::completionType`（**在 Player 上**）1B bool，TJS 可写 | `Player::_completionType`（`PlayerRender.cpp:874` 传入 `buildNodeTree`） | 已有；未提交 diff 已透传到 type3 stencilType &= ~4 |
| **node+1904** | RenderNode 指针 (0x1B0 item) | **无独立成员**；`_runtime->preparedRenderItems[]` + `entryPtrByNode` map 等价 | 语义有，**无 node 直连成员**；Phase 2 建议加 `preparedItemIndex` |
| **node+1944** | `drawnThisFrame` (BYTE)，`sub_6C2334` 入列时置 1，顶部清 0 | **缺失** | **PORT GAP**；下游 calcBounds / type3 propagate 需要此键 |
| node+1952 | 可见祖先指针（`sub_6BD8DC` 写） | `visibleAncestorIndex`（L263） | 已有（索引替代指针） |
| **node+1960** | `drawFlag`（BYTE），**Path B 产物**；`sub_6BD8DC` 写，`sub_6C2334` type0 主分支不读 | `drawFlag`（L261） | 已有但**语义错位**：port 误当 Path A gate（§7.1） |
| node+1996 | forceVisible | `forceVisible`（L262） | 已有 |

### 2.2 RenderNode 侧（0x1B0B item ↔ `PreparedRenderItem`, `RuntimeSupport.h:240+`）

| offset | 语义 | port 成员 | 状态 |
|---|---|---|---|
| item+16/17/18 | filter / nodeType skip / preview flag | `rawFlag16` / `skipFlag0` / `skipFlag1` | 已有 |
| item+19 | drawFlag first-pass | `drawFlag`（赋值 `PlayerRenderPrepare.cpp:340`） | 已有但继承 §7.1 错位 |
| item+21 | clip-valid | `clipFlag`/`rawFlag21` | 半实现 |
| item+24/+32 | children vector（仅 type12/type3 填） | `childItems` | 已有 |
| **item+244** | stencil/composite 标志（node+52 拷贝） | `updateCount`（**误命名**，`PlayerRenderPrepare.cpp:387`） | **需重命名为 `stencilComposite`** |
| **item+264** | ancestor-chain 指针（仅 type3/type12 写） | `parentItem` | 已有；写入条件见 §7.3 |
| item+304/+324 | leaf/composed layer variant | 间接经 `RenderCommand::leafLayer` | 半实现 |

---

## 3. Call Stack 映射

| libkrkr2.so 函数 | 职责 | port 文件 / 函数 / 行号 | 状态 |
|---|---|---|---|
| `Player_drawCompat` @ 0x6D5FB8 | 顶层 draw 分派，三路径选一 | `Player::draw*` 入口（`PlayerRender.cpp:980` 附近，实际入口多处） | 已实现（Layer path） |
| **`sub_6D5164` @ 0x6D5164** | 调 `sub_6C2334` 构建 mainList + 用 `sub_6D4F00` 做 std::sort | `PlayerRenderPrepare.cpp:776-804`（比较器 + std::sort 在同一段内联） | **部分实现** — 排序逻辑有，但调用前置的 `player+544 != 0` gate 和 "sub_6C2334 的返回条件"没有显式建模 |
| **`sub_6C2334` @ 0x6C2334** | 遍历 node deque，按 nodeType mask 构建 flat mainList + 特殊 auxList | `PlayerRenderPrepare.cpp:appendPreparedRenderItems()` L194-905 + `PlayerRenderPrepare.cpp:815-876`（parent/child 关系重建） | **部分实现** — 主循环的 nodeType mask（L203 `bitmask = _runtime->isEmoteMode ? 5193 : 5185`）与二进制 mask 不一致（二进制是 0x1441/0x1449 = 5185/5193，匹配）；auxList 的 type12 分支存在但 Branch A (nodeType=3) 未实现 |
| `sub_6D5264` Player_applyTranslateOffset | 给 mainList 每项加 cameraOffset | `PlayerUpdateLayers.cpp` phase3 cameraOffset 合入 | 已实现（合入 vertex 计算阶段，不是后处理） |
| **`sub_6C4E28` @ 0x6C4E28** | 两 pass：leafLayer 渲染 + auxList 复合聚合 | `PlayerRender.cpp:1625` `buildCommandOutput` lambda 起（leaf）；复合聚合 `PlayerRender.cpp:1685+` (`ensureLeafCommandLayer`) | **部分实现** — leaf path 存在；aux 复合 pass 可用但 auxList 来源不对（type3 Branch A 缺失） |
| **`sub_6C7440` @ 0x6C7440** | 最终合成主循环，迭代 mainList，LABEL_59 direct path / LABEL_63 composed path | `PlayerRender.cpp:1810` 附近外层循环 + `PlayerRender.cpp:1863`(direct) / `PlayerRender.cpp:1670+`(buffered) | **部分实现** — direct/buffered 分支均在，但 `shouldUseDirectRenderPathLike_0x6C7440` (L495) 的判据是 `visibleAncestorIndex<0 && blend.lowNibble ∈ {0} ∪ {6..15}`，**少了 "stencilType!=0 ⇒ composed" 和 "item+264!=0 ⇒ composed"** 两个原生 gate（见 §7.3） |

---

## 4. `sub_6D5164` 反编译与分析

### 4.1 反编译伪代码（0x6D5164, IDA MCP, 压缩 ~25 行）

```c
__int64 sub_6D5164(__int64 player, __int64 *mainList) {
  if ( !*(_DWORD*)(player + 544) ) return 0;     // 0x6d5178 — gate
  sub_6C2334();                                   // 0x6d5198 — build; ARM X0..X5
                                                  //  由 caller Player_drawCompat 0x6D60C4..E0
                                                  //  置 X1=&mainList X2=&auxList X3=0xFF808080 W4=W5=0
  __int64 v3 = *mainList, v4 = mainList[1];
  void *v6;
  if ( v4 - v3 < 1 ) {                            // 空/退化
LABEL_8:
    v6 = nullptr;
    sub_6F58BC(v3, v4, sub_6D4F00);               // insertion sort fallback
  } else {
    unsigned __int64 v5 = (v4 - v3) >> 3;
    while ( 1 ) {
      v6 = operator new(8 * v5, std::nothrow);
      if ( v6 ) break;
      v5 >>= 1; if ( !v5 ) goto LABEL_8;          // OOM → 退化
    }
    sub_6F5A04(v3, v4, v6, v5, sub_6D4F00);       // sort with scratch buffer
  }
  operator delete(v6, std::nothrow);
  return 1;
}
```

### 4.2 自然语言解释

`sub_6D5164` 就是一个 **"build + sort" 的薄 wrapper**：

1. **Gate**：`player+544 == 0` 时直接返回 0，表示本 Player 无 renderList 上下文（当前 clip 尚未就绪等），调用方（`Player_drawCompat`）据此跳过整个后续 render 流程。
2. **Build**：调用 `sub_6C2334()`（IDA 显示无参数但 ARM64 调用约定下 X0..X5 直接沿用 caller 设好的值 — `X1=&mainList`, `X2=&auxList`, `X3=default color 0xFF808080`, `W4=W5=0`；见 `Player_drawCompat` 0x6D60C4..0x6D60E0 序列），在 mainList/auxList 上写入新一帧的 RenderNode 项。
3. **Sort**：对 `*mainList` (begin..end) 区间用比较器 `sub_6D4F00` 做 std::sort；内部走 "分配辅助缓冲 → sub_6F5A04 (大概是带缓冲的快排/归并)" 的路径；分配失败或列表为空/过小时退化到 `sub_6F58BC` (insertion sort) 原地排序。
4. **Callees 唯一重要一项** 就是 `sub_6C2334`。`sub_6F5A04` / `sub_6F58BC` / `sub_6D4F00` 都是通用 sort 基础设施。`mcp__ida-pro-mcp__callees 0x6D5164` 返回空列表（IDA 把它们识别为 tail helper）进一步佐证这是薄包装。

### 4.3 回答："是不是 sort 包装？"

**是。** `sub_6D5164` 确认是 "build then sort mainList" 的薄 wrapper，除了一个 `player+544` 空 gate 和 "无 renderList 返回 0" 的 bool 语义外，没有任何独立业务逻辑。port 现状的 `PlayerRenderPrepare.cpp:776-804` 那段 std::sort 已经正确对齐这个函数的 sort 语义（比较器用的是等价的 `sortKey`），唯一缺失的是 `player+544` 的显式 early-return（见 §7.4）。

---

## 5. `sub_6C2334` — Port 视角要点

引用 `project_render_executor.md` / `project_sub6C2334_render_list_builder.md`。port 必须复刻：

1. **入列 gate = nodeType mask**，不看 `stencilType`。mask = `completionType ? 0x1449 : 0x1441`。port L203 数值 `5193/5185` 正确，但需去掉 `drawFlag` 二次 gate。
2. **每项写 `node+1904` / `node+1944=1`**。port 无对应成员（见 §2），需加 `drawnThisFrame` 或用 `PreparedRenderItem` 存在性等价。
3. **`item+264` 只在 Branch A (type3 wrapper) 和 type12 composite 两处写**。port 的 `selfSeedChildList`/`parentItem` 覆盖了 type12 半边，**缺 Branch A**。
4. **mainList flat**。type0 叶节点（如 moji_y）就是 top-level item，vertices 已 world-space。
5. **auxList 只装 type12 复合父 + type3 wrapper**，`sub_6C7440` 不直接迭代，仅 `sub_6C4E28` Pass 2 用于 composedLayer 聚合。
6. **type12 secondary loop**：`node+28==12 && (node+52 & 4) && node+1944` → push auxList + self-seed `item+24` + 按子 nodeType (0 直 append / 3 preview 直 append, 非 preview splice grandchildren) 分流。
7. **direct vs composed**：`sub_6C7440` direct path ⇔ `meshType==0 && item+244_stencil==0 && item+264==0`，缺任一走 composed。

---

## 6. `sub_6BD8DC` — Path B 真实用途

见 `analysis/Player_Rendering_Architecture_libkrkr2so.md §3.3`。核心：

- **不是主渲染 gate**。Phase 3 post-loop 子步骤，只写 `node+1960 drawFlag` 和 `node+1952 visibleAncestor`，**不分配 RenderNode、不入 mainList**。
- **`drawFlag` 下游消费者**：
  - `Player_calcBounds @ 0x6C3D04`：bbox 跳过过滤。
  - `sub_6BE0C0`（type3 Motion）：propagate 父状态到子 Player 时判断是否推进。
  - `sub_6C2334 @ 0x6C2AAC`：仅 Branch A (type3) 读一次（见 `project_sub6C2334_render_list_builder.md`），type0 主分支不读。
- **port**：`MotionNode.h:261 drawFlag` **保留**，给 calcBounds / type3 propagate 用；但不得作为 Path A 入列门。

---

## 7. Port 当前错误清单

### 7.1 `PlayerUpdateLayers.cpp:1625-1639` —— 本身正确，但**下游消费方式错了**

**更正（2026-04-21）**：本节原版说删除 `stencilType==0` 分支，是错的。事后 analyst 重扫（`0x6BD958 LDR W2, [X1,#0x34]` / `CBZ W2, loc_6BDA00`）确认：

- `sub_6BD8DC` 的 `node+52` **就是 init-time 写入的 `stencilType`**（假说 A），libkrkr2.so 全量扫描只有 `Player_initNodeFields` 三处写 `[X?,#0x34]`（0x6B42B4 / 0x6B42C0 / 0x6B43B0），**没有任何 per-frame 路径**写它
- 原生 `sub_6BD8DC` 的确有 `stencilType==0 → drawFlag=0` 这条分支（汇编证据确凿）
- port `PlayerUpdateLayers.cpp:1625-1639` **按 libkrkr2 原样复刻了 `sub_6BD8DC`，本身没错**，维持现状即可

**真正的 bug 在下游 —— port 把 `drawFlag` 当 Path A 渲染门用**，但 libkrkr2.so 里 `drawFlag` 只是 Path B 的产物，不决定节点是否进入 `mainList`。对照 `grep drawFlag cpp/plugins/motionplayer/`：

| 文件:行 | 用法 | 原生对齐？ |
|---|---|---|
| `PlayerUpdateLayers.cpp:1600` | root `drawFlag = active && hasSource` | ✅ 对齐 `sub_6BD8DC` root 分支 |
| `PlayerUpdateLayers.cpp:1613` | `visibleAncestor` 链查找用 `!drawFlag` 跳过 | ✅ 对齐 `sub_6BD8DC` @ 0x6BD9D8 |
| `PlayerUpdateLayers.cpp:1625-1639` | 按 `sub_6BD8DC` 计算自身 `drawFlag` | ✅ 本 §7.1 以前误判为错，现更正为正确 |
| **`PlayerRender.cpp:1000`** | `if (!entry.drawFlag \|\| opacity<=0) continue` —— **Path A 迭代主循环跳过未渲染** | ❌ **错**；`sub_6C7440` 迭代 mainList 不看 drawFlag，只看 item 自己的 flag16/skipFlag |
| **`PlayerRenderPrepare.cpp:75`** | calcBounds 跳过 `!drawFlag` 节点 | ✅ 对齐 Path B（`Player_calcBounds @ 0x6C3D04` 确实读 `drawFlag`） |
| `PlayerRenderPrepare.cpp:252/273/542/622/625` | 诊断日志中打印 drawFlag | ✅ 无副作用 |
| **`PlayerRenderPrepare.cpp:340-341`** | `entry.drawFlag = node.drawFlag \|\| stencilCompositeMaskReferenced \|\| ...` —— 拷到 `item+19` | ⚠️ 部分对齐 —— `sub_6C2334` 给 `item+19` 赋的是 `node+1960 ? 1 : node+1961`（两字段 OR），port 写法等价；但这个 `item+19` 本身不是 Path A 入列门，后续谁读它要重新核 |
| `PlayerQuery.cpp:43` | TJS `layerVisible` 属性 getter | ✅ 返回给脚本用，无副作用 |

**改法方向**：
1. **保留** `PlayerUpdateLayers.cpp:1625-1639` 原状（drawFlag 计算正确）
2. **`PlayerRender.cpp:1000` 必须改** —— 把 `!entry.drawFlag` 从主渲染循环的跳过条件移除；节点是否渲染由"是否在 mainList"决定（Path A 的入列 gate 在 `PlayerRenderPrepare.cpp` 里按 nodeType mask 控制），不是 drawFlag
3. **`PlayerRenderPrepare.cpp:340-341`** 保留（`item+19` 赋值语义对齐），但 Phase 2 要确认下游 `item+19` 的消费者是不是只在 Path B（bounds）场景，不在 Path A 主渲染
4. **补 Path A 的独立入列 gate** —— 在 `PlayerRenderPrepare.cpp:appendPreparedRenderItems` 外层 for 循环里，决定 "是否把此 node 做成 PreparedRenderItem" 的条件必须是 **nodeType mask `0x1441/0x1449` + 祖先链条件**（对齐 `sub_6C2334` @ 0x6C3148..0x6C3200），**完全不看 drawFlag**。当前 port 此处行为需要逐行核（§7.3 有部分覆盖）

### 7.2 五个未提交文件处置

基于 `git diff cpp/plugins/motionplayer/`：

| 文件 | 改动 | 处置 | 理由 |
|---|---|---|---|
| `MotionNode.h`（-5/+4） | 只改注释：把 `stencilType` 从"运行时重建"澄清为"init-time 固定" | **保留** | 注释对齐 `project_sub6C2334_render_list_builder.md` 的权威结论（"NEVER written by 0x6C2334"），有益于未来维护者 |
| `NodeTree.h` / `NodeTree.cpp`（+1/+18 参数透传） | 给 `buildNodeTree` / `walkTree` 加 `parentCompletionType` 参数，type3 分支实现 `stencilType &= ~4` | **保留** | 对齐 `sub_6B3C78 @ 0x6B43A0-0x6B43B4` 字节验证过的 CBZ gate（见 `project_player_completionType.md`）；独立于 Path A 问题；无副作用 |
| `PlayerRender.cpp`（+2） | 传 `_completionType` 到 `buildNodeTree` | **保留** | 同上，是 NodeTree 改动的调用侧适配 |
| `PlayerUpdateLayers.cpp`（-4） | 删除 `stencilType = stencilTypeBase \| currentFrameType` 的每帧 OR（root + loop 两处） | **保留** | 对齐 libkrkr2.so 全量扫描结论：`node+52` 只有 3 处 init-time 写入（`Player_initNodeFields` @ 0x6B42B4/0x6B42C0/0x6B43B0），无任何 per-frame 写入。每帧 OR 会破坏 init-time 值的稳定语义。**删除后 stencilType=0 节点的 `drawFlag` 会按 sub_6BD8DC 正确地保持 0**，这是对齐原生的结果；m2logo 黑屏根因不在这里，而在下游 `PlayerRender.cpp:1000` 误把 `drawFlag` 当渲染门（见 §7.1 修订版）。**必须搭配 §7.1 的 `PlayerRender.cpp:1000` 修复一起合入**，才能看到 stencilType=0 节点正常渲染 |

所有 5 个文件都应**保留，但必须等 §7.1 的下游 `drawFlag` 消费方修掉后再提交**。当前单独合入会继续黑屏。

### 7.3 `PlayerRenderPrepare.cpp:820-837` parentItem 过度设置

详见 `project_render_executor.md` "Local Web Port Misalignments"。当前 port 代码已经收敛到两条写入路径：

- `PlayerRenderPrepare.cpp:811-818`：初始化 `parentItem=nullptr`，只给 `selfSeedChildList==true` 的 item 自 push 自己到 childItems（对齐 type12 composite 的 self-seed）。
- `PlayerRenderPrepare.cpp:841-876`：只处理 `selfSeedChildList` 标记的 type12 parent，按子 nodeType 分流（type0 直 append、type3 preview 模式直 append / 非 preview splice grandchildren）— 对齐 `sub_6F3424` @ 0x6C3800..0x6C3924。

**当前缺口**：**Branch A (nodeType=3 sub-player wrapper) 的 `item+264 = visibleAncestor.item` 写入完全缺失**。对应原生 `sub_6C2334 @ 0x6c2b28` 的 `parentItem->+264 = visibleAncestor_item` 写入。port 需要在 preparedRenderItems 里对 nodeType=3 节点额外跑一遍：找到它的 visibleAncestor 对应的 item，把自己的 `parentItem` 指向它（此处语义是"ancestor chain pointer"，不是"视觉父"）。

`PlayerRender.cpp:1156-1167` 根据 `preparedItem->parentItem != nullptr` 生成 `hasRenderParent = true`，进而被 `PlayerRender.cpp:1855` 的 top-level 过滤器跳过。yuzulogo 场景下没有 type3/type12，所以理论上 `parentItem` 应该全是 null，这条过滤器应该不会误伤；m2logo 场景下有 type3 所以 Branch A 缺失会导致 type3 子渲染出不来。

### 7.4 `MotionNode` 需要新增 / 重命名的成员

| 变更 | 位置 | 对齐 |
|---|---|---|
| **新增** `bool drawnThisFrame` 或者 `int preparedItemIndex` | `MotionNode.h`（`drawFlag` 附近） | libkrkr2.so `node+1944`：Path A 入列产物，是原生下游跳过未入列节点的键；port 目前靠 `PreparedRenderItem` 存在性隐式表达，Phase 2 正式对齐时建议显式化 |
| **重命名** `PreparedRenderItem::updateCount` → `stencilComposite`（或 `compositeFlags`） | `RuntimeSupport.h:277` | 该字段从 `node.stencilType` 拷贝来（`PlayerRenderPrepare.cpp:387`），对齐 `item+244 composite flags`；名字 `updateCount` 误导 |
| **新增** `Player::_hasRenderList` (`player+544` 镜像) gate | `Player.h` + `PlayerRenderPrepare.cpp`（prepare 入口） | 对齐 `sub_6D5164` 第一行 `if (!*(DWORD*)(a1+544)) return 0`；port 现在没有等价 early return |
| （可选）`MotionNode::renderTreeFlag201` 改名 | `MotionNode.h:364` | 已对应 `node+201`，名字尚可 |

---

## 8. Port 重写粗粒度 checklist

最小正确改法的步骤序（不是 Phase 2 详设）：

1. **`node+52` 语义已澄清（2026-04-21）**：libkrkr2.so 全量扫描只有 3 处 init-time 写入（`Player_initNodeFields` @ 0x6B42B4/0x6B42C0/0x6B43B0），无 per-frame 路径。`sub_6C2334 @ 0x6C341C` 的 `STR` 写的是 **RenderItem+0x34**，不是 Node+0x34。Path B `sub_6BD8DC` 读的就是 init-time 的 `stencilType`。Phase 2 据此对齐假说 A。
2. **`PlayerUpdateLayers.cpp:1625-1639` 保留** —— 本段已正确复刻 `sub_6BD8DC`。**真正要改的是 `PlayerRender.cpp:1000`**：移除 `!entry.drawFlag` 作为主渲染循环跳过条件（它是 Path A iteration，不应读 Path B 产物）。保留 `PlayerRenderPrepare.cpp:75` 的 bounds 跳过（calcBounds 在原生里就读 drawFlag）。
3. **验证 `PlayerRenderPrepare.cpp:appendPreparedRenderItems` 外层 for 循环**的入列条件是否已对齐 `sub_6C2334` 的 nodeType mask `0x1441/0x1449` + 祖先链 gate；**显式确认不依赖 `drawFlag` / `stencilType`**。`PlayerRenderPrepare.cpp:340-341` 的 `entry.drawFlag = node.drawFlag || stencilCompositeMaskReferenced || ...` 拷贝本身对齐 `sub_6C2334` 给 `item+19` 的赋值，保留。
4. **增补 Branch A (nodeType=3)** — 在 `PlayerRenderPrepare.cpp:820-876` 的 parentItem 赋值段之后，加一段专门处理 `node.nodeType == 3` 的 item：把它的 `parentItem` 指向 `visibleAncestorIndex` 对应的 item（对齐 `sub_6C2334 @ 0x6c2b28`）。
5. **在 `MotionNode` 增加 `drawnThisFrame` 或等价** — 让 Path A 入列事件显式化，给 `Player_calcBounds` 对齐（port 的 `PlayerRenderPrepare.cpp:75` 现在用 `node.drawFlag` 做 bounds gate，改成用 `drawnThisFrame` 更准确）。
6. **重命名 `PreparedRenderItem::updateCount` → `stencilComposite`** — 语义对齐。
7. **增补 `player+544` gate** — 在 `Player::drawIntoCanvas`（或等价入口）开头加 early return，对齐 `sub_6D5164` 第一行。
8. **端到端验证序**：m2logo（type0-only，验证 §7.1 修复）→ yuzulogo（type0 + 多 letter，验证 §7.3 的 parentItem 不过度写）→ 带 type3 的 motion（验证 Branch A 新增）→ 带 type12 mask 的 motion（验证现有 selfSeedChildList 仍工作）。
