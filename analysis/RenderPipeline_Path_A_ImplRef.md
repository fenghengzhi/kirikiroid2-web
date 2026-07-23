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

**libkrkr2.so Path A**：`Player_drawCompat (0x6D5FB8)` → `sub_6D5164 (0x6D5164)` → `sub_6C2334 (0x6C2334)` 按 `nodeType` 位掩码（`0x1441 / 0x1449`，bits {0,6,10,12} 或 {0,3,6,10,12}）决定节点是否进入 `mainList`，**完全不看 `stencilType`**。选择 `0x1441` 还是 `0x1449` 的 `Player+1092` 字节经 NCB 绑定复核后已确认是 `preview`，不是 `completionType`；TJS `completionType` 是独立的 `Player+1144 int`。每个入列节点在 `node+1904` 持有持久 RenderNode 指针、在 `node+1944` 置 `drawnThisFrame=1`；后续 `Player_renderToCanvas → sub_6C7440` 迭代 `mainList`。

**当前 port 状态（2026-07-23）**：早期实现曾误把 Path B 的 `node+1960 drawFlag` 当作 Path A 入列门；当前 `PlayerRenderItems.cpp::appendPreparedRenderItems` 已按 `preview ? 0x1449 : 0x1441` 独立入列，并在 `MotionNode` 上持有 `preparedRenderItem` / `drawnThisFrame`。type3 Branch A wrapper、`item+264` 祖先链、type12 aux/child list、`stencilComposite`、item/node 三组 mesh vector，以及 `sub_698188` source-clip 四角颜色重映射均已实现。每个 draw caller 现在在栈上创建独立的 `PreparedRenderItemList mainList/auxList`，只对 main 排序并将两表沿 build/execute 调用链传递；`Player` 不再持有这些临时指针表，也不存在诊断 union。

---

## 2. 字段对照表

libkrkr2.so RenderNode 的字段偏移在 `node` 结构上（Motion 节点本体，2632 字节）和在 RenderNode 上（`sub_6C2334` 分配的 0x1B0 字节）是两个空间，要分开看。

### 2.1 Motion node 侧（2632B node ↔ port `motion::detail::MotionNode`）

| offset | 语义 | port 成员 | 状态 |
|---|---|---|---|
| node+52 | `stencilType`（PSB 种子，运行时只读；`sub_6B43B0` 的 type3 分支受 `Player+1092 preview` 门控） | `stencilType`/`stencilTypeBase` | 已实现；已删除错误的每帧 OR |
| **Player+1092** | `preview`，1B bool；选择渲染/时间轴 node-type mask | `Player::_preview`（传入 `buildNodeTree`，也被 render build/execute 读取） | 已实现；旧文档把它误叫 `completionType`，现已纠正 |
| **Player+1144** | TJS `completionType`，32-bit int；与 `Player+1092` 独立 | `Player::_completionType` | 已实现；不得再拿它代替 preview gate |
| **node+1904** | 持久 RenderNode 指针 (0x1B0 item) | `MotionNode::preparedRenderItem` | 已实现；由节点析构释放，不再用按帧 map 模拟所有权 |
| **node+1944** | `drawnThisFrame` (BYTE)，`sub_6C2334` 入列时置 1、每次构建前清 0 | `MotionNode::drawnThisFrame` | 已实现；type12 后处理按此门控 |
| node+1952 | 可见祖先指针（`sub_6BD8DC` 写） | `visibleAncestorIndex`（L263） | 已有（索引替代指针） |
| **node+1960** | `drawFlag`（BYTE），**Path B 产物**；`sub_6BD8DC` 写，`sub_6C2334` type0 主分支不读 | `drawFlag`（L261） | 已有但**语义错位**：port 误当 Path A gate（§7.1） |
| node+1996 | forceVisible | `forceVisible`（L262） | 已有 |

### 2.2 RenderNode 侧（0x1B0-byte item ↔ `PreparedRenderItem`）

| offset | 语义 | port 成员 | 状态 |
|---|---|---|---|
| item+16/17/18 | filter / nodeType skip / preview flag | `rawFlag16` / `skipFlag0` / `skipFlag1` | 已有 |
| item+19 | drawFlag first-pass | `drawFlag`（在 `PlayerRenderItems.cpp` population 时赋值） | 已实现；不作为普通 Path A 入列门 |
| item+21 | clip-valid，保留持久 item 的部分写生命周期 | `rawFlag21` | 已实现于节点持有的持久 item；无旁路 lifetime map |
| item+24/+32 | children vector（仅 type12/type3 填） | `childItems` | 已有 |
| item+168..180 | 继承/player 颜色乘算后，再由 `sub_698188` 按 source clip 重映射的四角 packed colors | `packedColors` | 已按两次 early-return、分支内 `FCVTZS #8`、W32 lane 插值与 Variant 局部生命期实现 |
| **item+244** | stencil/composite 标志（node+52 拷贝） | `stencilComposite` | 已实现并完成语义命名 |
| **item+264** | ancestor-chain 指针；wrapper/普通 item population 按可见祖先写入 | `parentItem` | 已实现；type12 post-pass 不反推子 item 的 parent |
| item+344/+376/+400 | composite / raw Bezier patch / affine-transformed patch 三个独立 `std::vector<MeshPoint>` | `commandCompositeMeshPoints` / `commandBezierPatchPoints` / `meshPoints` | 已实现；不是同一 vector 的别名 |
| item+304/+324 | leaf/composed layer variant | `PreparedRenderItem::leafLayer` / `composedLayer` | 已实现；在持久 item 上原地 build/execute |

---

## 3. Call Stack 映射

| libkrkr2.so 函数 | 职责 | port 文件 / 函数 / 行号 | 状态 |
|---|---|---|---|
| `Player_drawCompat` @ 0x6D5FB8 | 顶层 draw 分派，三路径选一 | `Player::draw*` 入口（`PlayerRender.cpp:980` 附近，实际入口多处） | 已实现（Layer path） |
| **`sub_6D5164` @ 0x6D5164** | 调 `sub_6C2334` 构建 mainList + 用 `sub_6D4F00` 做 sort | `PlayerRenderItems.cpp::prepareRenderItems()` | build + sort + bool 返回均已建模；`player+544` type-tag gate 映射为 `hasMotionContent()` |
| **`sub_6C2334` @ 0x6C2334** | 遍历 node deque，按 nodeType mask 构建 flat mainList + 特殊 auxList | `PlayerRenderItems.cpp::appendPreparedRenderItems()` | **主体已实现** — mask 为 `_preview ? 0x1449 : 0x1441`；type3 Branch A wrapper、普通 item、type12 aux/child list 和持久 node-owned item 均已落地；main/aux 由 caller-stack 持有 |
| `sub_6D5264` Player_applyTranslateOffset | 给 mainList 每项加 cameraOffset | `PlayerRenderItems.cpp::applyPreparedRenderItemTranslateOffsets(mainList)` | 已实现为独立后处理，只遍历 mainList |
| **`sub_6C4E28` @ 0x6C4E28** | 两 pass：leafLayer 渲染 + auxList 复合聚合 | `PlayerRenderExecute.cpp::buildRenderCommands()` | leaf 与 aux 复合 pass 已拆开；aux 来源已包含 type3 Branch A 与 type12 |
| **`sub_6C7440` @ 0x6C7440** | 最终合成主循环，迭代 mainList，direct / buffered 后提交 | `PlayerRenderExecute.cpp::executeLayerRenderCommands()` | **部分实现** — fresh `0x6C7B44..0x6C7B9C` 证据表明结构 gate 是 blend 低位分流，随后 `clearEnabled || item+264 != 0` 强制 buffered；`meshType` 和 `item+244` 不是这个 gate。本地仍在外层对 `parentItem` 直接 `continue`，且内层 direct 判据混入 `hasChildren/visibleAncestorIndex`，导致原生的 parent buffered→祖先 mask→`operateRect` 链不可达（见 §7.3） |

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

1. **Gate**：`player+544 == 0` 时直接返回 0；该位置是 `player+528` motion-content `tTJSVariant` 的 type tag，表示没有已加载的 motion content，而不是“clip 尚未就绪”。调用方（`Player_drawCompat`）据此跳过后续 render 流程。
2. **Build**：调用 `sub_6C2334()`（IDA 显示无参数但 ARM64 调用约定下 X0..X5 直接沿用 caller 设好的值 — `X1=&mainList`, `X2=&auxList`, `X3=default color 0xFF808080`, `W4=W5=0`；见 `Player_drawCompat` 0x6D60C4..0x6D60E0 序列），在 mainList/auxList 上写入新一帧的 RenderNode 项。
3. **Sort**：对 `*mainList` (begin..end) 区间用比较器 `sub_6D4F00` 做 std::sort；内部走 "分配辅助缓冲 → sub_6F5A04 (大概是带缓冲的快排/归并)" 的路径；分配失败或列表为空/过小时退化到 `sub_6F58BC` (insertion sort) 原地排序。
4. **Callees 唯一重要一项** 就是 `sub_6C2334`。`sub_6F5A04` / `sub_6F58BC` / `sub_6D4F00` 都是通用 sort 基础设施。`mcp__ida-pro-mcp__callees 0x6D5164` 返回空列表（IDA 把它们识别为 tail helper）进一步佐证这是薄包装。

### 4.3 回答："是不是 sort 包装？"

**是。** `sub_6D5164` 确认是 "build then sort mainList" 的薄 wrapper，除了一个 `player+544` type-tag gate 和返回 bool 外，没有独立业务逻辑。当前 port 在 `prepareRenderItems()` / `appendPreparedRenderItems()` 中以 `hasMotionContent()` 复刻 gate，并按 `sortKey` 排序 mainList；旧文档所说“缺少 `player+544` early-return”已被后续实现证伪。

---

## 5. `sub_6C2334` — Port 视角要点

引用 `project_render_executor.md` / `project_sub6C2334_render_list_builder.md`。port 必须复刻：

1. **入列 gate = nodeType mask**，不看 `stencilType`。mask = `preview ? 0x1449 : 0x1441`；`preview` 是 `Player+1092`，不是 TJS `completionType` (`Player+1144`)。当前 port 已独立使用这一 gate，不再把 `drawFlag` 当普通节点的二次入列门。
2. **每项写 `node+1904` / `node+1944=1`**。当前 port 由 `MotionNode::preparedRenderItem` 和 `MotionNode::drawnThisFrame` 显式承载。
3. **Branch A (type3 wrapper) 的 `item+264` 祖先链已实现**；普通 item 也在自身 population 时直接写 `parentItem`。type12 post-pass 只维护自己的 `childItems`，不反推或重写子 item 的 parent。
4. **mainList flat**。type0 叶节点（如 moji_y）就是 top-level item，vertices 已 world-space。
5. **auxList 只装 type12 复合父 + type3 wrapper**，`sub_6C7440` 不直接迭代，仅 `sub_6C4E28` Pass 2 用于 composedLayer 聚合。
6. **type12 secondary loop**：`node+28==12 && (node+52 & 4) && node+1944` → push auxList + self-seed `item+24` + 按子 nodeType (0 直 append / 3 preview 直 append, 非 preview splice grandchildren) 分流。
7. **direct vs buffered**：fresh `sub_6C7440` 指令证据证伪了旧结论。`meshType` 和 `item+244_stencil` 不属于结构 gate；先按 blend 低 4 位分流，再以 `clearEnabled || item+264!=0` 进入 buffered。`item+264` 非空不是 skip，而是后续祖先 alpha-mask 链的起点。

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

## 7. 当前对齐状态（2026-07-23）

### 7.1 已纠正的旧结论

- `sub_6BD8DC` 按 init-time `node+52 stencilType` 计算 `node+1960 drawFlag` 的路径本身正确；`drawFlag` 仍是 Path B 数据，不是普通 Path A 节点的入列门。
- 旧文档把 `Player+1092` 叫作 `completionType`。NCB 注册表字面绑定已证实它是 `preview`；`completionType` 是 `Player+1144 int`。`buildNodeTree` 与 calc/render node-mask 使用 `_preview`；item+18 execute gate 的 raw read 是 `Player+1096`，应对应 `_priorDraw`，不得再与 `_preview` 合并。
- `item+19` 仍按 `node+1960 ? 1 : (arg5 | node+1961)` 生成，并只作为 `sub_6C4E28` first-pass gate；它不重新决定 item 是否进入 mainList。
- `player+544` gate 已由 `hasMotionContent()` 映射，不再是缺口。

### 7.2 已实现的结构

| 原生结构 | 当前本地映射 | 证据状态 |
|---|---|---|
| `node+1904` 持久 item + `node+1944` 本帧标志 | `MotionNode::preparedRenderItem` + `drawnThisFrame` | 已实现；节点析构负责 item 生命周期 |
| type3 Branch A wrapper / `item+264` ancestor chain | wrapper population 直接写 `parentItem`，必要时只分配祖先节点的持久 item | 已实现；不再 post-sort 反推 parent |
| type12 main+aux 双入列、自 seed `item+24`、子列表 splice | `mainList` / `auxList` + `childItems` | 已实现 |
| `item+244` stencil/composite | `PreparedRenderItem::stencilComposite` | 已完成重命名与消费链对齐 |
| node `+2024/+2048/+2072` 与 item `+376/+344/+400` 三个 vector owner | 三个独立 `std::vector<MeshPoint>`，按原生分支复制/变换 | 已实现；不能退化成单 vector alias |
| item+21 等部分写生命周期 | 节点持有的持久 `PreparedRenderItem` 自身保留字段 | 已实现；旧 `renderItemNativeFieldLifetimeByNode` / `nativeLifetime*` 旁路 map 已删除 |

### 7.3 已闭合的生命周期结构与剩余缺口

- 原生 `mainList` 与 `auxList` 已恢复为 draw caller 栈上的两个临时 `std::vector<PreparedRenderItem *>`；build 只排序 main，translate 只遍历 main，build-command 阶段接收 main/aux，最终 execute 只提交 main。
- `NativePreparedRenderItemState` 的 owner 声明顺序已使 C++ 逆序析构与 `sub_6F4DFC @ 0x6F4DFC` 一致；Web 平台附加状态位于派生 `PreparedRenderItem`，在 native semantic base 之前析构。这是源码声明顺序/生命周期复原，不是 ARM64 数值偏移复原。
- fresh `sub_6C7440@0x6C7440` 已纠正旧文档：direct/buffered 结构 gate 不包含 `meshType==0` 或 `stencilComposite==0`。`meshType` 在分流后选 affine/Bezier/mesh，`item+244` 在祖先 mask 处消费。
- 当前真实缺口是 parented item 的调用链：原生先解析 source，因 `item+264!=0` 强制使用持久 `SourceCache.bufLayer`，再沿 `item+264` 链逐层调 `Motion_doAlphaMaskOperation@0x6C8390`，最后 `operateRect@0x6C8558`。本地外层 `if(item.parentItem) continue` 使这条链整体不可达；只删 `continue` 也不足以复原中间 layer/mask 生命期。

---

## 8. 回归核对顺序

1. type0 motion：确认 `drawFlag==0` 不阻止按 preview mask 入 mainList。
2. type3 motion：确认 wrapper 只在原生 Branch A 条件下创建，`parentItem` 指向可见祖先的持久 item。
3. type12 motion：确认同一持久 parent item 同时进入 main/aux，并按 preview 分支直接 append 或 splice child list。
4. meshType 1/2 motion：分别核对 raw patch、transformed patch、composite grid 三个 vector 的 owner、swap/copy 与 division 参数。
5. parented item：确认 source resolve 在 parent gate 之前，复用 `SourceCache.bufLayer`，按 `parentItem` 链逐层 mask，并以 `operateRect` 提交。
