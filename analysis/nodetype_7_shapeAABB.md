# nodeType=7 (shapeAABB) 专项处理分析

**目标**：在 libkrkr2.so 中递归反编译所有与 nodeType=7 相关的处理代码，定位本地 Web 端可能遗漏或错误实现的分支。

**结论**：libkrkr2.so 中 nodeType=7 的处理**非常集中**，只有 3 个函数真正关心它：
1. `sub_6BDCC0` (0x6BDCC0) — **唯一显式按 `type==7` 分支**的 phase3 函数，计算 shapeAABB 并建立 `node+1936` 链表
2. `sub_6B3C78` (0x6B3C78) — PSB 节点初始化，但 **type==7 没有 case 分支**，仅读通用字段
3. `sub_6C2334` (0x6C2334) — 渲染 prepare，**不检查 type**，无差别读取 `node+1936` 指针并写入 `PreparedRenderItem+200..+215`

其他所有 phase3 函数（`sub_6BC000`、`sub_6BC4F0`、`sub_6BD8DC`、`Player_processCameraNode`、`sub_6BDE94`、`sub_6BE0C0`、`sub_6BEDD0`、`sub_6BF0DC`、`Player_evaluateCameraNodes`）**都不对 type=7 做特殊处理**。

---

## 1. 涉及 nodeType=7 的函数

### 1.1 `sub_6B3C78` @ 0x6B3C78 — PSB 节点初始化

读取所有节点共用的 PSB 字段（label/parameterize/coordinate/joinTarget/groundCorrection/frameList/inheritMask/transformOrder/meshTransform/type/stencilType），然后按 `type` 分支：

```c
switch (node[28] /* type */) {
    case 0: node[2136] = readInt("objTriPriority"); break;
    case 1: node[32]   = readInt("shape"); break;       // type=1 only
    case 3: /* motion sub-player setup */ break;
    case 4: /* particle setup */ break;
    case 6: node[2380] = 0; break;
    case 9: node[2376] = readInt("anchor"); break;
    case 0xC: node[2576] = readVariant("stencilCompositeMaskLayerList"); break;
    // case 7: NO BRANCH — falls through default, reads no extra fields
}
```

**关键发现**：type=7 **没有 case 分支**，不读 `shape`、`shapeAABB_*`、`boundingBox` 任何专属字段。shapeAABB 只使用通用字段（inheritMask、transformOrder、stencilType、joinTarget 等）。

### 1.2 `sub_6BDCC0` @ 0x6BDCC0 — **Shape AABB 计算（核心）**

phase3 中调用。**这是唯一显式 `if (type == 7 && active)` 的函数**。完整伪代码（精简）：

```c
for (i = 1; i < N; ++i) {
    node = nodes[i];
    parent = nodes[node[+36]];     // via parentIndex

    if (node[+28 /*type*/] == 7 && node[+1505 /*accum.active*/]) {
        // 读取本节点 drawAffineMatrix（世界空间 2x2，由 phase2 末尾 sub_699940+matmul 产生）
        m11 = node[+120]; m12 = node[+128]; m21 = node[+136]; m22 = node[+144];
        // 世界空间位置
        posX = node[+1512]; posY = node[+1520]; posZ = node[+1528];
        // active slot 的 clip 原点（slot = node + 536*slotIdx）
        slot = node + 536 * node[+1392];
        ox = slot[+376]; oy = slot[+384];
        // player.zFactor * posZ
        pzs_offset = player[+1112] * posZ;

        // 原点变换
        oox = oy*m12 + ox*m11;
        ooy = oy*m22 + ox*m21;

        // 候选端点：±16 单位 extent
        x1 = posX - m11*16 - m12*16 - oox;   // v26
        x2 = posX + m11*16 + m12*16 - oox;   // v27
        y1 = posY - m21*16 - m22*16 - ooy;   // v28
        y2 = posY + m21*16 + m22*16 - ooy;   // v29

        // 取 min/max（矩阵可能带负号翻转）
        xMin = min(x1, x2);  xMax = max(x1, x2);
        yMin = min(y1, y2);  yMax = max(y1, y2);

        // 写入 node+2144..+2156（4 float）
        // 注意：y 方向加 pzs_offset，x 方向不加
        node[+2144 /*xMin*/] = (float)xMin;
        node[+2148 /*yMin*/] = (float)(pzs_offset + yMin);
        node[+2152 /*xMax*/] = (float)xMax;
        node[+2156 /*yMax*/] = (float)(pzs_offset + yMax);

        // 与父节点的 clip 相交
        parentClipPtr = parent[+1936];   // float*
        if (parentClipPtr) {
            if (parentClipPtr[0] >= node[+2144]) node[+2144] = parentClipPtr[0];
            if (parentClipPtr[1] >= node[+2148]) node[+2148] = parentClipPtr[1];
            if (parentClipPtr[2] <= node[+2152]) node[+2152] = parentClipPtr[2];
            if (parentClipPtr[3] <= node[+2156]) node[+2156] = parentClipPtr[3];
        }
        newClipPtr = node + 2144;       // 本节点成为后续 clip 源
    } else {
        // 非 type-7 或 inactive：继承父节点的 clip 指针
        newClipPtr = (void*) parent[+1936];
    }
    node[+1936] = newClipPtr;
}
```

**语义**：
- type=7 节点以 `±16 单位 × drawAffineMatrix` 为基础几何，**减去原点变换后的偏移**，加上 **y 方向的 zFactor*posZ 投影偏移**，得出 AABB。
- 然后与最近祖先的 shapeAABB 求交集。
- 本节点的 `+2144..+2156` 成为后续子孙节点的 clip 源。
- **非 type=7 的节点，+1936 每帧重新从父节点获取**（不累计写回 +2144）。

### 1.3 `sub_6C2334` @ 0x6C2334 — 渲染 prepare（消费者）

**两处读取 `node+1936`**（分别在 0x6C2660 和 0x6C27EC）：

```c
PreparedRenderItem *item = ...;
// 先写运行时 AABB（本节点自身在世界空间的几何包围盒）
item[+184..+199] = node[+1888..+1903];   // 16 字节，4 float

// 然后写 clip AABB（来自最近 shapeAABB 祖先）
float *clipPtr = (float*) node[+1936];
item[+200..+215] = clipPtr ? *clipPtr : xmmword_14D7C60;
```

`xmmword_14D7C60` 的字节内容（已验证）：

```
00 00 80 3F  00 00 80 3F  00 00 80 BF  00 00 80 BF
= {1.0f, 1.0f, -1.0f, -1.0f}
```

**这是一个"空 clip" 哨兵值**（x1=1,y1=1,x2=-1,y2=-1 —— xMin>xMax 且 yMin>yMax）。

### 1.4 `sub_6C4E28` @ 0x6C4E28 — 渲染执行 alpha mask composer

读取 `PreparedRenderItem+200..+215`（上一步写的 clip AABB）：

```c
// v80 = max(a4[0], item[+184]);   // paintBox start
// v83 = max(a4[1], item[+188]);
// v84 = min(a4[2], item[+192]);
// v85 = min(a4[3], item[+196]);

// 然后检查 clip 有效性：
v103 = item[+208];   // clip.x2
v104 = item[+200];   // clip.x1
if (v103 >= v104) {
    v110 = item[+212];   // clip.y2
    v111 = item[+204];   // clip.y1
    if (v110 >= v111) {
        // clip 有效 → 收紧 paintBox
        v80 = max(v80, floorf(v104));
        v83 = max(v83, floorf(v111));
        v84 = min(v84, ceilf(v103));
        v85 = min(v85, ceilf(v110));
    }
}
// clip 无效（默认 {1,1,-1,-1}）→ 不收紧，透传 paintBox
if (v80 < v84 && v83 < v85 && !item[+16]) {
    // 渲染此 item
    item[+21] = 1;
    item[+216..+228] = {v80, v83, v84, v85};  // effective rect
    ...
}
```

**语义**：
- clip AABB 作为 paintBox 的**上界收紧**。
- 默认值 `{1,1,-1,-1}` 的 `v103 >= v104` (-1>=1) 为 false → **完全跳过收紧**，等价于 "无 shapeAABB 限制"。
- 若 item 不在 clip 内（`v80 >= v84` 或 `v83 >= v85`），整个 item 的 `+21` 标志置 0 → 不参与渲染。

---

## 2. 完整数据流图

```
┌──────────────────────────────────────────────────────────────────┐
│ Phase 2 (0x6BB33C Player_updateLayers 主循环)                    │
│ ──────────────────────────────────────────                       │
│ 累加 position/scale/slant/angle/flipXY/blendMode/opacity         │
│ → node+1512/1520/1528 (pos), +1544/1552 (scale), +1560/1568(slt) │
│ → node+1536 (angle), +1507/1508 (flipX/Y), +1576 (opacity)       │
│ → sub_699940 构建 local 2x2 → 乘以 parent 的 drawAffineMatrix    │
│ → node+120/128/136/144 = world-space drawAffineMatrix (a,b,c,d)  │
└──────────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌──────────────────────────────────────────────────────────────────┐
│ Phase 3 (updateLayers 尾部)                                      │
│ sub_6BC000  → 处理 camera 节点                                    │
│ sub_6BC4F0  → icon size 传播（type 无关）                         │
│ sub_6BD8DC  → visibility propagate（写 node+1960，type 无关）    │
│ Player_processCameraNode → camera 后处理                         │
│ sub_6BDCC0  → ★ Shape AABB 计算（type==7）                       │
│                                                                   │
│   对于每个 type=7 active 节点：                                   │
│     1. 从 +120..+144 读 world matrix                             │
│     2. 从 slot+376/+384 读 clip 原点 ox/oy                       │
│     3. 计算 ±16 extent AABB，加 y 方向 pzs 偏移                  │
│     4. 与 parent+1936 求交                                        │
│     5. 写 node+2144..+2156，node+1936 = &node[+2144]              │
│                                                                   │
│   对于非 type=7 节点：                                            │
│     node+1936 = parent+1936  (指针链传播)                        │
│                                                                   │
│ sub_6BDE94 → shape 几何（type==1，不是 type==7）                 │
│ sub_6BE0C0 → ...                                                  │
│ sub_6BEDD0 → ...                                                  │
│ sub_6BF0DC → ...                                                  │
│ Player_evaluateCameraNodes → camera 最终处理                      │
└──────────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌──────────────────────────────────────────────────────────────────┐
│ Render Prepare (sub_6C2334) — 为每个可见 node 生成 RenderItem   │
│                                                                   │
│   item+184..+199 = node+1888..+1903  (node 自身 geometry AABB)  │
│   item+200..+215 = (node+1936 ? *ptr : {1,1,-1,-1})             │
│                                 └─── 最近 shapeAABB 祖先的 AABB │
└──────────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌──────────────────────────────────────────────────────────────────┐
│ Render Execute (sub_6C4E28) — 为每个 RenderItem 合成 mask        │
│                                                                   │
│   读 item+200..+215 作为 clip 上界                                │
│   paintBox = intersect(parent paintBox, item geometry, clip)     │
│   若 item 不在 paintBox 内 → item[+21]=0（跳过渲染）             │
│   否则写 item+216..+228 = effective rect                         │
└──────────────────────────────────────────────────────────────────┘
```

**"对自己节点的效果"**：type=7 节点自己不渲染（没有 source），只产生 `+2144..+2156` clip rect。

**"对子树的效果"**：所有子孙节点的 `node+1936` 链会指向此 type=7 节点，使其 RenderItem 的 `+200..+215` 填入此 clip rect。alpha mask 合成时会把 paintBox 收紧到 clip rect 内。

**"对兄弟节点的效果"**：**零影响**。`node+1936` 仅通过树遍历继承，不跨兄弟传播。

---

## 3. 读取的 PSB 字段汇总

type=7 节点**只读通用字段**（无 type=7 专属字段）：
| 字段 | node offset | 读取位置 |
|------|------------|---------|
| `label` | +0  | sub_6B3C78 通用路径 |
| `parameterize` | +8 | sub_6B3C78 |
| `coordinate` | +24 | sub_6B3C78 |
| `joinTarget` | +46 | sub_6B3C78 |
| `groundCorrection` | +47 | sub_6B3C78 |
| `frameList` | +64 | sub_6B3C78 |
| `inheritMask` | +40 | sub_6B3C78（对 m2logo.mtn str_clip = 0x0240066c）|
| `transformOrder` | +84/88/92/96 | sub_6B3C78 |
| `meshTransform` | +2000 | sub_6B3C78（通常 0）|
| `type` | +28 | sub_6B3C78（此处 = 7）|
| `stencilType` | +52 | sub_6B3C78 |

**明确确认 type=7 不读**：`shape`、`shapeAABB_*`、`boundingBox`、`objTriPriority`、`particle*`、`stencilCompositeMaskLayerList`、`anchor`。

---

## 4. 包围盒的实际用途

问题"若 libkrkr2.so 的 type=7 处理涉及'根据 state.width/height × accum.m 计算包围盒并写回 stencil buffer'之类的操作，详细说明这个包围盒用在哪"的精确回答：

**不是 stencil buffer**，**也不是 drawDevice 剪裁矩形**。

包围盒是 **CPU 端的 `PreparedRenderItem::viewport` 等价物**（item+200..+215），在 **alpha mask 合成阶段**（`sub_6C4E28` → `Motion_doAlphaMaskOperation`）作为 **每个 item 的 paintBox 上界**使用：

1. 遍历所有 RenderItem，计算每个 item 的 effective paintBox = intersect(parent paintBox, item geometry bounds, **clipRect from +200..+215**)。
2. 若 effective paintBox 退化（`xMin >= xMax` 或 `yMin >= yMax`）→ `item+21 = 0`，**不参与本次渲染**。
3. 否则此 item 参与 alpha mask 合成，绘制范围被 clip rect 限制（通过 `Motion_doAlphaMaskOperation` 的 (x,y,w,h) 参数）。

**不涉及的东西**：
- 不写入任何 stencil buffer / stencilType。
- 不调 drawDevice 的 scissor/clip API。
- 不影响 Cocos2D OpenGL 渲染的 viewport。
- 纯 CPU 端合成 mask 时的像素范围限制。

---

## 5. 本地 Web 端实现对比（仅观察，不修改）

本地 `cpp/plugins/motionplayer/PlayerUpdateLayers.cpp::updateLayersPhase3_ShapeAABB`（1646-1712 行）的实现**架构上与 libkrkr2.so 对齐**：

- 使用 `sn.accumulated.m11..m22` 对应 libkrkr2 的 `node+120..+144`（本地 AccumulatedState.m 存储的就是 phase2 末尾 calcMatrix 结果 × 父矩阵后的 world-space drawAffineMatrix，注释已明确）。
- 使用 `sn.parentClipIndex` 替代 libkrkr2 的 `node+1936` 指针，每帧从 `nodes[parentIndex].parentClipIndex` 继承，语义与指针链一致。
- type=7 active 时计算 AABB → 与 parent clip 相交 → 设 `parentClipIndex = si`。

**本地 `PlayerRenderPrepare.cpp`**（430-482 行）的实现**逻辑与 sub_6C2334+sub_6C4E28 的语义一致**：
- `kInvalidPreparedPaintBox` 是本地版本的 `xmmword_14D7C60` 等价物。
- 使用 `clipNode.shapeAABB[2] >= clipNode.shapeAABB[0] && [3] >= [1]` 检查 clip 有效性，对应 sub_6C4E28 的 `v103 >= v104 && v110 >= v111`。
- 对 clip AABB 应用 `transformPoint`（4 个角）后取 min/max —— 本地把 clip 从 **世界空间**再变换到了 **某个 viewport 坐标系**，这是 libkrkr2 没有的步骤（libkrkr2 的 clip 始终在世界空间直接与 item bounds 相交）。

### 需要调查的疑点（供参考，不代表 bug）

1. **`PlayerRenderPrepare.cpp:463-482` 的 `transformPoint` 变换**：libkrkr2 的 `sub_6C4E28` 直接用 world-space clip rect 与世界空间 item AABB 做 min/max 交集，**不额外变换 clip rect**。本地多了一层 `transformPoint`，若 `transformPoint` 不是 identity，clip rect 在坐标系中会被二次变换。需检查 `transformPoint` 的矩阵内容（是否实际是 identity 或某个 viewport → world 映射）。

2. **`sn.accumulated.m11..m22` 是否真的等于 libkrkr2 的 world-space `+120..+144`**：本地在 phase2 末尾有无执行等价于 `sub_699940` 的 local 2x2 重建 + 父乘法？若本地 `accumulated.m` 只是"累加后但未做 local transformOrder 分解"的中间量，与 `+120..+144` 会有细微差异（例如 transformOrder=0/2/3 顺序不同导致的非交换性矩阵）。

3. **对于 m2logo.mtn 的 str_clip (nodeIndex=18, inheritMask=0x0240066c)**：
   - 位 0x04 = angle inherit
   - 位 0x08 = flipX inherit
   - 位 0x20 = scaleX inherit (libkrkr2 phase2 中 `if (v46 & 0x20)` 分支)
   - 位 0x40 = scaleY inherit
   - 位 0x20000 = Z inherit (或 0x04 移位)
   - 位 0x200000 = ?
   - 位 0x400000 = joinTarget (记忆 project_updateLayers_phase2_accum.md 已确认)
   - 位 0x2000000 = ?
   这些位决定了 phase2 累加时哪些维度继承父节点。shapeAABB 本身不读 inheritMask，但**它依赖的 `+120..+144` 在 phase2 由 inheritMask 驱动**，inheritMask 错误会间接影响 AABB 计算。

---

## 6. 总结

- **type=7 在 libkrkr2.so 中只有 1 个专项处理函数**：`sub_6BDCC0`。
- **不读 shape/shapeAABB_*/boundingBox 字段**，只用通用字段 + 世界矩阵 + slot 原点。
- **不涉及 stencil buffer / drawDevice clip**，只做 CPU 端 alpha mask paintBox 收紧。
- **对子树影响**：通过 `node+1936` 指针链传播 clip rect 到所有子孙 RenderItem。
- **对兄弟/父节点无影响**。
- 本地实现架构与 libkrkr2.so **数据流对齐**，可能的细节差异点在 §5 三条疑点中列出，需要独立验证（非本次任务）。
