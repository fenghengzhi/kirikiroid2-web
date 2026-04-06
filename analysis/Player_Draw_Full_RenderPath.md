# Player.draw() 完整渲染路径分析

> Analysis target: libkrkr2.so (kirikiroid2 Android, arm64-v8a)
> Date: 2026-04-06
> Method: IDA Pro MCP 完整调用链反编译

---

## 1. 完整调用时序

TJS 调用 `player.progress(dt)` 触发以下**严格有序**的 4 步流程：

```
Player_progressCompat (0x6D2A98) — TJS "progress" NCB 入口
  ├── 1. Player_progress_inner(player, dt)     // 时间推进 + timeline interpolation
  ├── 2. Player_updateLayers(player)           // 节点状态更新
  │       └── Phase 3 post-loop:
  │           ├── sub_6BC4F0  — vertex computation (node+1856..1884)
  │           ├── sub_6BD8DC  — visibility flags
  │           ├── sub_6BDCC0  — Shape AABB → node+1936 (pointer chain)
  │           └── ... (其他 post-loop 处理)
  ├── 3. Player_calcBounds(player)  ★★★关键★★★
  │       └── 从 node+1856..1884 计算 bbox → node+1888..1900
  └── 4. Player_dispatchEvents(player)
```

TJS 调用 `player.draw(base)` 触发渲染：

```
Player_draw (0x6D5FB8) — TJS "draw" NCB 入口
  ├── 检查 D3DAdaptor → Player_drawD3D
  ├── 检查 SLA → Player_DrawSLA
  └── Layer 直接渲染路径:
      ├── sub_6D5164 — 排序渲染列表
      │   └── sub_6C2334 — 构建 render tree (node → renderNode)
      │       ├── 复制 vertices: renderNode+136 = node+1856 (8 floats)
      │       ├── 复制 bbox: renderNode+184 = node+1888 (4 floats)
      │       ├── 复制 viewport: renderNode+200 = *node+1936 or default
      │       ├── 如果 drawAffineMatrix flag (player+611): 变换 vertices
      │       └── 排序 render items
      ├── Player_applyTranslateOffset — 加 cameraOffset
      ├── sub_6C7440 (Player_renderToCanvas) — 实际渲染
      │   └── 对每个 renderNode:
      │       ├── clip 检查 (renderNode+200 vs renderNode+184)
      │       ├── sub_6C1B70 (loadSource) → 获取 source texture
      │       ├── 读取 source width/height (v46/v47)
      │       ├── bufLayer.setSize(clipW, clipH)
      │       └── bufLayer.affineCopy(source, 0,0,srcW,srcH,
      │                               renderNode+136..164 经偏移, blendMode, 1)
      └── Player_updateLayerAfterDraw
```

## 2. 三组坐标/尺寸的区别

### 2.1 node+232/240 (sourceWidth/sourceHeight) — 类型: double
- **用途**: sub_6BC4F0 用它计算 dst vertices (node+1856..1884)
- **写入者**: **仅** Player_evaluateCameraNodes (0x6C0528), **仅** nodeType==10
- **对 nodeType==0**: 始终为 0.0 (memset 初始化值)
- **影响**: sourceWidth=0 → vertices 退化为单点 → bbox 退化 → setSize(0,0) → 不渲染

### 2.2 node+1856..1884 (vertices) — 类型: 8 个 float (4个角点xy)
- **写入者**: sub_6BC4F0 (0x6BC4F0), 在 Player_updateLayers Phase 3
- **计算**: origin = pos - matrix * originOffset; 4 corners = origin + matrix * (w, h)
- **如果 sourceWidth/Height=0**: 全部退化为 origin 点

### 2.3 loadSource 返回的 width/height — 类型: int
- **来源**: TJS resourceManager.loadSource() 返回的 Layer 对象
- **用途**: affineCopy 的 srcWidth/srcHeight 参数
- **不等于 sourceWidth/Height!** 这是 texture 的实际像素尺寸

## 3. Bounding Box 链

### node+1888..1900 (4 floats: minX, minY, maxX, maxY)
- **写入者**: Player_calcBounds (0x6C3D04), 在 progress() 第 3 步
- **计算方式**: 从 node+1856..1884 (vertices) 计算 min/max, 然后 floor/ceil
- **复制到**: renderNode+184..196 (在 sub_6C2334 中)
- **用途**: sub_6C7440 中 bufLayer.setSize 的 clip 裁剪

### node+1936 (pointer to 4 floats)
- **写入者**: sub_6BDCC0 (Shape AABB, nodeType=7), 在 Player_updateLayers Phase 3
- **继承方式**: nodeType!=7 的节点继承父节点的 node+1936
- **如果无 Shape AABB**: null → 使用默认值 (1.0, 1.0, -1.0, -1.0) = 无效空 bbox
- **复制到**: renderNode+200..215 (在 sub_6C2334 中)
- **用途**: sub_6C7440 中的 viewport clip 检查

## 4. sub_6C7440 渲染流程（非D3D路径）

```
对每个 renderNode:
    // 跳过条件
    if (renderNode+17 || renderNode+16 || opacity==0): skip

    // viewport clip 检查
    if (renderNode+208 >= renderNode+200 && renderNode+212 >= renderNode+204):
        // 有效 viewport: 计算 clip rect, setClip
    else:
        // 无效/空 viewport: setClip(无参数) → 清除 clip

    // 共同路径
    opacity = renderNode+232
    dirtyRect.add(renderNode+184..196)
    setColor(renderNode+168..180)
    setSource(renderNode+8)
    
    // 纹理加载
    source = loadSource(player, renderNode+256+4)
    srcW = source.width    // ★ 纹理实际像素宽度
    srcH = source.height   // ★ 纹理实际像素高度

    // bufLayer clip from node bbox
    v57 = max(renderNode+184, 0)   // clip minX
    v61 = max(renderNode+188, 0)   // clip minY
    v58 = min(renderNode+192, bufW) // clip maxX
    v53 = min(renderNode+196, bufH) // clip maxY
    
    if (v58 < v57 || v53 < v61): skip  // 无效裁剪

    bufLayer.setSize(v58-v57, v53-v61)

    // 根据 meshType 分派
    switch (meshType):
        case 0:  // 无 mesh → affineCopy
            bufLayer.affineCopy(
                source,
                srcLeft=0, srcTop=0, srcW, srcH,   // ★ 完整纹理
                affineMode=0,
                renderNode+136 -0.5-v57,  // vertex0.x 减去 clip 偏移
                renderNode+140 -0.5-v61,  // vertex0.y
                renderNode+144 -0.5-v57,  // vertex1.x
                renderNode+148 -0.5-v61,  // vertex1.y
                renderNode+160 -0.5-v57,  // vertex3.x (注意: 跳过 vertex2!)
                renderNode+164 -0.5-v61,  // vertex3.y
                blendMode, stNearest=1
            )
        case 1:  // bezierPatchCopy
        case 2:  // meshCopy
```

## 5. 核心发现: 为什么 sourceWidth/Height=0 不是问题

**答案: nodeType=0 的普通渲染节点的 sourceWidth/Height (node+232/240) 确实始终为 0。**

这意味着:
1. sub_6BC4F0 计算的 vertices 全退化为 origin 点
2. Player_calcBounds 计算的 bbox 退化为单点 (originX, originY, originX, originY)
3. sub_6C7440 中 setSize 得到 (0, 0) → bufLayer 为空 → affineCopy 无效果

**但 binary 确实能正常渲染。** 这可能说明:

### 假设 A: 实际渲染不走 sub_6BC4F0 + sub_6C7440 的 vertex 路径
也许 binary 的实际显示来自另一条路径（D3D 或 SLA），而非这里分析的 Layer 直接路径。需要确认游戏实际使用的是哪条渲染路径。

### 假设 B: sourceWidth/Height 通过我们未发现的路径设置
IDA 的搜索可能遗漏了动态计算的 offset 或通过寄存器间接寻址的写入。

### 假设 C: affineCopy 使用 source 尺寸而非 node 尺寸
affineCopy 的 srcW/srcH (v46/v47) 来自 loadSource, 是非零的纹理尺寸。dst 坐标虽然退化, 但 TJS Layer 的 affineCopy 实现可能在 dst 退化时使用 src 尺寸作为 fallback。

**最可能的答案是假设 A**: 实际的动画游戏使用 D3D 或 SLA 路径渲染。Layer 直接路径可能仅用于特殊场景。

## 6. 完整函数地址表

| 地址 | 名称 | 用途 |
|------|------|------|
| 0x6D2A98 | Player_progressCompat | TJS progress() NCB 入口 |
| 0x6D2A54 | sub_6D2A54 | progress 内部包装 (progress_inner+updateLayers+calcBounds+dispatch) |
| 0x6D5FB8 | Player_draw | TJS draw() NCB 入口, 3路分发 |
| 0x6D5164 | sub_6D5164 | 构建并排序渲染列表 |
| 0x6C2334 | sub_6C2334 | 构建 render tree (node→renderNode) |
| 0x6C7440 | Player_renderToCanvas | 最终渲染循环 (affineCopy/meshCopy/bezierPatchCopy) |
| 0x6D5264 | Player_applyTranslateOffset | 加 cameraOffset 到所有 vertices |
| 0x6C1B70 | sub_6C1B70 | loadSource: 加载 source texture via TJS ResourceManager |
| 0x6BC4F0 | sub_6BC4F0 | vertex computation: node+232/240 → node+1856..1884 |
| 0x6C3D04 | Player_calcBounds | bbox computation: node+1856..1884 → node+1888..1900 |
| 0x6BDCC0 | sub_6BDCC0 | Shape AABB chain: nodeType=7 → node+1936 指针链 |
| 0x6BD8DC | sub_6BD8DC | visibility flag: → node+1960 |
| 0x6C0528 | Player_evaluateCameraNodes | nodeType=10: 写 sourceWidth/Height (node+232/240) |
| 0x699390 | sub_699390 | node 初始化函数 |
| 0x6F19B4 | sub_6F19B4 | deque push_back: memset(0) + sub_699390 |

## 7. RenderNode 结构 (从 sub_6C2334 + sub_6C7440 逆向)

```
偏移    类型    来源             用途
+0      ptr     node+0           TJS variant (source reference)
+8      var     ?                source key
+16     byte    ?                flag (skip if set)
+17     byte    ?                flag (skip if set)  
+18     byte    ?                flag (used after clip)
+19     byte    node+1960/1961   draw flag
+48     int     clip+364         blend mode (4 bits)
+136    float×8 node+1856..1884  4 corner vertices (dst for affineCopy)
+168    int×4   ?                RGBA color channels
+184    float×4 node+1888..1900  bounding box (minX,minY,maxX,maxY)
+200    float×4 *node+1936       viewport clip bbox (from Shape AABB chain)
+232    int     node+1576        accumulated opacity (0-255)
+244    int     node+52          update count
+248    var     ?                key variant
+256    ptr     node+200 addr    pointer to source deque data
+264    ptr     node+1904        sub-render node pointer
+272    int     node+2012        mesh subdivU
+276    int     node+2016        mesh subdivV
+280    int     node+2000        meshType (0=none, 1=bezier, 2=mesh)
+344    deque   node+2048        mesh vertices (deformed)
+368    int     ?                mesh precision
+376    deque   node+2024        mesh base/control points
+400    deque   node+2072        mesh control points array
```
