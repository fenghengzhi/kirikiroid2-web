# CLUSTER K — render items build + render targets + layer query 对齐审计 (2026-06-07)

> 权威源: libkrkr2.so (IDB libkrkr2.so.i64)。本轮只读审计 + IDB rename/comment + idb_save，无 cpp/ 改动。
> 范围(逐行全覆盖): PlayerRenderItems.cpp(1143) / PlayerRenderTargets.cpp(1599) / PlayerLayerQuery.cpp(433)。
> 方法: 反编译 0x6C2334(build) / 0x6C4E28(execute requireLayer) / 0x6C7440(renderToCanvas) /
>       0x6D5658(SLA dispatcher) / 0x6D5948(ResolveSLATarget) / 0x6C9CA8(accurate SLA) /
>       0x6CBCE4(acquireLayerById) / 0x6DCD0C(map insert) / 0x6CE7D8(updateLayerAfterDraw)。

> **2026-07-23 superseded correction：**旧 P1-I3 “build→execute phase
> divergence” 已证伪：本地 `buildRenderCommands` 现在就是
> `renderToCanvasLike_0x6C7440` 在 `!priorDraw` 下执行的 `0x6C4E28` pre-walk，
> 并未回到 `0x6C2334`。Loop B union 的是 group/child paintBox，而非 child clip。
> 当前剩余缺口以 clusterJ 的 acquire caller payload、accurate-SLA 和函数拆分为准。

## 当前审计结论（2026-07-23）：⚠️ 部分偏差，但 P1-I3 phase-divergence 已证伪

`buildRenderCommands` 是本地 `0x6C4E28` pre-walk，而不是 `0x6C2334` build；因此
requireLayerId/item+20 已处于正确函数阶段。当前开放项是 acquire caller payload 专用值类型、
accurate-SLA 持久树、0x6C7440 wrapper/executor 源码函数边界及 item 源码结构，不再包含 P1-I3。

---

## 反编译伪代码摘要(本轮新确认部分)

### build: sub_6C2334 @0x6C2334 (item alloc 0x1B0=432B)
- item 字段写入 +16/+17/+18/+19 等已由 cluster I/RenderItem_Field_Mapping 详尽核实。
- **本轮关键负证: 0x6C2334 内 grep `+424`=0 hits, `requireLayer`=0 hits, `sub_6C4E28`=0 hits。**
  → item+424 与 item+20 不在 `0x6C2334` 写入；本地同样由独立的
  `buildRenderCommands`（`0x6C4E28` counterpart）写入，因此这项负证不构成 phase 偏差。

### execute requireLayer: sub_6C4E28 @0x6C4E28 (= Player_emitRenderItem_requireLayer)
```
Loop A (a2 mainList): for item:
  if (!item+19) skip                                       // drawFlag gate @0x6c5dc0
  clip = clamp(a4 viewport, item+184..196, item+200..212)  // floor/ceil
  if (clipL<clipR && clipT<clipB && !item+16):
    item+21=1; item+216..228 = clip                        // @0x6c4f88
    if (player+760 SLA exists):
      if (!item+20):                                       // LABEL_28 gate
        L"requireLayerId" dispatch -> item+424 = layerId   // @0x6c5234
        item+20 = 1                                        // @0x6c5240 latch
    sub_6C6B48(player+760, item+424, ...) -> item+304 leafLayer
    setSize/fillRect + switch(item+280): affineCopy/meshCopy/bezierPatchCopy
  else: item+21 = 0                                        // @0x6c5e6c
Loop B (a3 boundsList/composed): for item:
  seed group paintBox(+184); union visible child paintBox(+184), then camera/viewport narrow
  if (!item+340) create composed Layer (L"Layer" via Window.mainWindow) -> item+324
  setSize/fillRect; child alpha-mask loop gate (child+21 && child+320):
    Motion_doAlphaMaskOperation(... item+244)
  item+21=1; item+16=0; item+216..228 = composed clip
```

### accurate SLA: sub_6C9CA8 @0x6C9CA8
```
acquire L"Layer" from a2+20; emitRenderItem_requireLayer(a1,a3,a4); // @0x6c9e74
for item (gate item+17||item+16||!item+232 skip; clip vs viewport):
  sub_6C6B48(a2, item+52 /*layerId*/, ...) -> leaf node            // @0x6ca0b0
  switch(item+48 & 0xF): blendMode -> v45 layerType {2,14,15,16,17}
  if (visible-gated composite, item+264==0 || blend6):
    Player_acquireLayerById(a2, item+56 /*layerId2*/)             // @0x6cab64
    -> assignImages + setSize + child alpha-mask(child+21 && !child+16)
  switch(item+280): affineCopy/meshCopy/bezierPatchCopy(leaf)
                    OR drawMeshFrame/drawBezierPatch*/drawLine(composite)
  setPos/type/visible/opacity on layer
```
→ **三个独立 layer-id 确认**: item+52(=node+16, leaf via sub_6C6B48),
  item+56(=node+20, composite via acquireLayerById), item+424(=requireLayerId, non-SLA path)。

### acquireLayerById: Player_acquireLayerById @0x6CBCE4 (RENAMED, was buildRenderTree_guess)
- lower_bound on `a1+120` std::map<int,LayerVariant> Rb_tree keyed by `node+32`(int)。
- miss → `SLA_orderedMap_insert_0x6DCD0C`(0x6DCD0C) 插入。
- 构造 `L"Layer"` TJS 对象(2 args=a1, a1+20), `absolute = node+160 + node+164`,
  `hitThreshold = 256`, cache 在 map node+40。
- **从 sub_6C9CA8 @0x6cab64 以 item+56 调用** → 确认它是 acquireLayerById，不是 build-tree。
  起点文档 I8 的"mis-named"已凭调用站点证据落实(已 rename + comment)。

### map insert: SLA_orderedMap_insert_0x6DCD0C @0x6DCD0C
- `operator new(0xD0=208B)` 节点, `node+32 = key(int)`, `_Rb_tree_insert_and_rebalance`。
- 标准 libstdc++ `std::map<int, node>` Rb_tree 内联。

### SLA dispatcher: sub_6D5658 @0x6D5658
- sub_6D5164(build+sort) → applyTranslateOffset → 分支 `byte_1AB84F4`(ogl_accurate_render
  OR hasGPUAccel, cxa_guard byte_1AB84F8):
  - OFF: ResolveSLATarget(0x6D5948) → RenderMotionFrame(0x6DE738) → Layer_UpdateRect(0x800F4C)
  - ON: sub_6C9CA8(accurate) + sub_6CE938(piledCopy post)

### ResolveSLATarget: sub_6D5948 @0x6D5948
- SLA+56==1 复用 SLA+40; 否则 lazy-create PrivateMotionGLL(g_PrivateMotionGLL_ClassID,
  cxa_guard byte_1AB8580) under ownerLayer, 存 SLA+40, SetSize(SLA clip w/h)。

### updateLayerAfterDraw: sub_6CE7D8 @0x6CE7D8
- `player+612 = player+613` **无条件快照**(每次 post-draw)。gate `if(player+613)`:
  sub_6CE19C + dispatch `assignImages(a2)` on layer player+696。

---

## 逐项对比

| 检查项 | 二进制行为 | 本地实现 | 状态 |
|--------|-----------|---------|------|
| ①item 字段 +18 skipFlag1 | inherited18||(node+48 priorDraw!=0) @0x6c33c0 | PlayerRenderItems.cpp:477 `!(inheritedFlag18||(node.priorDraw!=0))` 取反存 | ✅ |
| ①item +16 rawFlag16 | node+201 @0x6c33a8 | :474 `entry.rawFlag16=node.renderTreeFlag201` | ✅ |
| ①item +17 skipFlag0 | ((preview?1097:1089)&(1<<type))==0 | :475-476 同式 | ✅ |
| ①item +52/+56 layerId/layerId2 | node+16/node+20 @0x6c341c/0x6c3428 | :482-483 layerId/layerId2 | ✅ |
| ①item +424 requireLayerId | 0x6C4E28-only @0x6c5234 | `buildRenderCommands`（0x6C4E28 counterpart） | ✅ phase aligned |
| ②flag +20 latch | 0x6C4E28 LABEL_28 @0x6c5240 | `buildRenderCommands` rawFlag20 | ✅ phase aligned |
| ②build +19 drawFlag | node+1960?1:(a5|node+1961) | :471-473 +needsGroupEntry term | ✅ |
| ②item alloc 432B raw | operator new(0x1B0) 单块 | PreparedRenderItem+NativeRenderItemFields STL 拆分 | ⚠ container(I4) |
| ③render target 容器(SLA map) | std::map<int,node> Rb_tree(0xD0节点 0x6DCD0C) | SeparateLayerAdaptor.h:41 `std::map<tjs_uint32,NativeSLANodeLike>` | ✅ |
| ③PrivateMotionGLL lazy-create | SLA+40 g_PrivateMotionGLL_ClassID under ownerLayer | ensurePrivateMotionGLLLike_0x6D5948 | ✅ |
| ③SLA 双路 accurate/non | byte_1AB84F4 gate(0x6D5658) | renderToSeparateLayerAdaptor:1269 isAccurateSlaRenderEnabled() 双分支 | ✅ |
| ③updateLayerAfterDraw +612/+613 | 无条件 +612=+613; gate +613 assignImages@696 | updateLayerAfterDrawLike_0x6CE7D8:1462 无条件 ready=needs; gate; assignImages on _internalRenderLayer | ✅ |
| ④layer query acquireLayerById | std::map<int,Layer> Rb_tree, absolute=x+y, hitThreshold=256 | std::map(SLA Map) + acquire via resolveRenderLayerNodeLike_0x6C6B48 | ✅ |
| ④getLayerNames | TJS Array, Player+24 std::map<ttstr,int> in-order, KEY only | collectLayerNames: _nodeLabelMap(std::map<ttstr,int>) 升序 narrow filter | ✅ |
| ④getLayerGetterList | flat node deque(Player+200) nodeIndex 序, 每非root节点一 getter | getLayerGetterList: _nodes[1..] 逐个 | ✅ |
| accurate mesh switch 0/1/2 | affineCopy/meshCopy/bezierPatchCopy(leaf) + drawMeshFrame/drawBezierPatch*(composite) | renderAccurateSlaLike:893-915 affine/Bezier/Mesh Copy | ✅(leaf) / ⚠ composite drawMeshFrame 路径未独立(见下) |

---

## 偏差详情

### ✅ P1-I3 已证伪：requireLayerId/item+20 phase placement
- 二进制 item+424 与 item+20 只由 `sub_6C4E28 @0x6c5234/0x6c5240` 写入；
  `0x6C2334` 不碰它们。
- 本地曾因把函数名 `buildRenderCommands` 误当 `0x6C2334 build` 而报告 phase 偏差；实际
  该函数正是 `0x6C4E28` counterpart，并由 `renderToCanvasLike_0x6C7440` 在
  `!priorDraw` 时调用，所以阶段已对齐。旧“移回 execute”的建议删除。
- item 源码结构仍可独立审计，但不能用 ARM64 0x1B0 对象尺寸要求 wasm32 硬凑布局。

### ⚠ I4 (standing, container-class): item 拆分为两个 STL struct
- 二进制 item = 单块 operator new(0x1B0)=432B raw, build/execute 跨阶段就地消费。
- 本地拆为 NativeRenderItemFields + PreparedRenderItem(std::vector/string/tTJSVariant)。
- 按 CLAUDE.md 字节布局复刻工作法: 这是**源码结构层**的容器拆分, 非字节偏移问题; 但二进制
  确为单块 raw, 拆成两 struct 在"源码结构/对象生命周期"维度仍是偏差。trace 0-diff, 保留。

### ⚠ accurate composite 路径 drawMeshFrame/drawBezierPatchFrame 未独立复刻
- sub_6C9CA8 LABEL_85 后, 对 visible-gated composite(item+264==0 || blend6)走
  acquireLayerById + assignImages + child alpha-mask, 然后按 item+280 用
  `drawMeshFrame`(0x6cb0d8)/`drawBezierPatchMeshFrame`(0x6cb200)/`drawBezierPatchFrame`
  (0x6cb298)/`drawLine`(0x6cafe8) 而非 leaf 的 meshCopy/affineCopy。
- 本地 renderAccurateSlaLike_0x6C9CA8 只实现了 leaf 的 affineCopy/BezierPatchCopy/MeshCopy
  (PlayerRenderTargets.cpp:893-915), **未实现 composite 分支的 acquireLayerById +
  drawMeshFrame/drawBezierPatchFrame/drawLine 子路径**。
- 这属"binary present, local absent"的 composite 渲染子路径缺口(非局部偏差, 需新增数据流)。
  对当前 logo fixture(无 type12 composite + accurate-SLA 组合)oracle-inert, 但是架构缺口,
  应择期补(标 MISSING)。

---

## 架构性偏差(🔧)
- **P1-I3 已于 2026-07-23 证伪**：`buildRenderCommands` 是当前
  `0x6C7440` 内 `!priorDraw` pre-walk 的本地 helper，不是 `0x6C2334` item-build；
  requireLayerId/item+20 已在正确阶段。ARM64 的 0x1B0 尺寸也不要求 wasm32
  收敛成手工 padding 的 POD。

## 子函数对齐状态
- ✅ Player_acquireLayerById @0x6CBCE4 — std::map<int,Layer> Rb_tree, 本地 SLA Map 对齐。已 rename+comment。
- ✅ SLA_orderedMap_insert_0x6DCD0C @0x6DCD0C — std::map insert(0xD0节点)。已 rename+comment。
- ✅ sub_6D5948 ResolveSLATarget — PrivateMotionGLL lazy-create, 本地对齐。
- ✅ sub_6D5658 SLA dispatcher — accurate/non 双路 gate, 本地对齐。
- ✅ sub_6CE7D8 updateLayerAfterDraw — +612/+613 + assignImages@696, 本地精确对齐。
- ⚠ sub_6C9CA8 accurate SLA — leaf 路径对齐; composite drawMeshFrame/acquireLayerById 子路径本地缺(见偏差详情)。
- ⚠ sub_6C4E28 emitRenderItem_requireLayer — pre-walk/float/leaf/group/refresh gate 已接入；`0x6C5264..0x6C532C` acquire caller payload 专用值类型仍开放。
- ✅ Motion_doAlphaMaskOperation — 两处 child alpha-mask loop(Loop B + composite)gate child+21&&child+320 / child+21&&!child+16, 本地 markStencilMaskChain 对齐。

## 平台边界标注
- 本轮范围内未发现新增 `// PLATFORM_BOUNDARY:` 注释段。D3D GPU 路径(PlayerRenderTargets.cpp
  renderItemsToD3DTextureLike_0x6ADFBC 等)用 `#if !defined(KRKR2_WASMTIME_HEADLESS)` 包裹
  GL 调用, 属构建配置门控而非平台边界声明(逻辑本身已对齐 0x6ADFBC), 不计偏差。

## 修复建议
1. P1-I3 已关闭；不得再按 ARM64 0x1B0 尺寸硬凑本地 POD，也不得把当前
   `0x6C4E28` helper 误移到其他阶段。下一步应反编译并复原 acquire caller payload。
2. accurate composite 子路径: 在 renderAccurateSlaLike_0x6C9CA8 补 visible-gated composite
   分支(Player_acquireLayerById 等价 + assignImages + child alpha-mask +
   drawMeshFrame/drawBezierPatchFrame/drawLine), 对齐 sub_6C9CA8 LABEL_85 后。标 MISSING,
   现 fixture oracle-inert, 缺运行时验证手段, 按 CLAUDE.md 忠实复刻不 defer。

## IDB 变更(idb_save done)
- rename 0x6CBCE4 buildRenderTree_guess → Player_acquireLayerById
- rename 0x6DCD0C sub_6DCD0C → SLA_orderedMap_insert_0x6DCD0C
- comment @0x6CBCE4 (acquireLayerById 调用站点+语义), @0x6DCD0C (std::map insert 0xD0)
