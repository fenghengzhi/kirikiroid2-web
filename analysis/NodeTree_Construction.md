# Node Tree Construction Analysis

## Overview

Node tree 构建发生在 `Player_playImpl` -> `Player_initNonEmoteMotion` -> `Player_buildNodeTree` 调用链中。
构建完成后，每帧由 `Player_updateLayers` 评估 timeline 动画并更新 node 属性。

## 调用链

```
Player_play (0x6B21E8)
  └── Player_playImpl (0x6B2284)
        ├── Player_loadMotion (0x6B0F10) — 调用 TJS onFindMotion/findMotion 回调获取 PSB 数据
        ├── [type==1] Player_initEmoteMotion (0x6B2E90) — emote motion (角度分区选择)
        │     └── Player_initNonEmoteMotion (0x6B365C) — 最终都走到这里
        └── [type==0] Player_initNonEmoteMotion (0x6B365C) — 非 emote motion
              ├── 读取 PSB: loopTime, lastTime, tag, priority
              ├── 读取 PSB: content → priority[0] → "content" (frameList 数据)
              ├── sub_6B1718 — 参数化初始化 (parameterize 模式)
              ├── sub_6B1ECC — 参数表后处理
              ├── Player_buildNodeTree (0x6B51F0) ★★★ 核心入口
              ├── Player_initVariables (0x6CD750) — 读取 PSB "variable" 键
              └── 设置初始时间/播放状态
```

## Player_buildNodeTree (0x6B51F0) — 核心入口

### 流程

1. 获取 PSB content 字典 (Player+528) 作为 ResourceManager (Player+992) 的引用
2. 调用 `Player_resetAndReleaseNodes` (0x6B56F8) — 释放旧 nodes，调用 `releaseLayerId` 释放旧 layer ID
3. 读取 PSB `content → "layer"` 键获取 layer 数组
4. **调用 `Player_buildNodeTree_recursive(player, 0, layerArray)`** — 从 root 递归构建
5. 遍历构建好的 nodes，处理 nodeType==12 (deflector) 的 `stencilCompositeMaskLayerList`
6. 清理 label→node 映射 tree

### 关键伪代码

```c
void Player_buildNodeTree(Player* player) {
    Dict* content = get_tjs_dict(player + 528);   // PSB content
    ResourceManager* rm = get_tjs_obj(player + 992);
    
    Player_resetAndReleaseNodes(player);           // 释放旧 nodes
    
    var layerArray = content["layer"];              // 从 PSB 读取 layer 数组
    Player_buildNodeTree_recursive(player, 0, layerArray);  // parentIdx=0 表示 root
    
    // 后处理: deflector nodes (type==12) 的 mask layer 链接
    for (int i = 1; i < nodeCount; i++) {
        Node* node = getNode(player, i);
        if (node->type == 12 && (node->stencilType & 4)) {
            // 遍历 stencilCompositeMaskLayerList, 在 name→node 映射中查找
            // 将找到的 nodes 链接到 deflector 的 maskLayerList
        }
    }
}
```

## Player_buildNodeTree_recursive (0x6B4A6C) — 递归构建

### 参数
- a1: Player* (qword pointer)
- a2: int parentIndex (父 node 在 deque 中的 index)
- a3: TJS variant (当前 layer 数组)

### 流程

对 layer 数组中的每个元素:
1. 在 deque 中分配新的 2632-byte node (push_back)
2. 设置初始 flags: `node+344 = 1` (hasSource), `node+880 = 1`
3. 设置 `node+36 = parentIndex`
4. 获取当前 layer 元素 (TJS dict)
5. 读取 `"label"` 属性并注册到 label→nodeIndex 映射 (红黑树)
6. **调用 `ResourceManager::requireLayerId` 获取两个 layer ID → 存入 node+16 和 node+20**
7. **调用 `Player_initNodeFields(player, node, layerElementDict)` — 填充 node 的所有属性**
8. 读取当前元素的 `"children"` 键
9. **递归调用自身: `Player_buildNodeTree_recursive(player, currentIndex, children)`**

### 关键伪代码

```c
void Player_buildNodeTree_recursive(Player* p, int parentIdx, Array layers) {
    ResourceManager* rm = get_tjs_obj(p[124]);  // Player+992
    int count = layers.length;
    
    for (int i = 0; i < count; i++) {
        // 1. 分配新 node
        Node* node = deque_push_back(p->nodeDeque);  // 2632 bytes
        node->hasSource_344 = 1;
        node->field_880 = 1;
        node->parentIndex = parentIdx;
        
        // 2. 获取 layer element
        Dict* elem = layers[i];
        Dict* layerDict = as_dict(elem);
        
        // 3. 注册 label
        string label = layerDict["label"];
        int myIndex = deque_size(p->nodeDeque) - 1;
        labelTree_insert(p->labelTree, label, myIndex);
        
        // 4. 获取 layer IDs
        node->layerId1 = rm->requireLayerId(elem);    // node+16
        node->layerId2 = rm->requireLayerId(elem);    // node+20
        
        // 5. 填充 node 字段
        Player_initNodeFields(p, node, elem);
        
        // 6. 递归处理 children
        var children = layerDict["children"];
        Player_buildNodeTree_recursive(p, myIndex, children);
    }
}
```

## Player_initNodeFields (0x6B3C78) — 填充 Node 属性

### 从 PSB 读取并填充的字段

| Node Offset | 字段名 | PSB Key | 数据类型 |
|-------------|--------|---------|---------|
| +0 | label (ttstr) | "label" | string |
| +8 | parameter ptr | "parameterize" | int (index) |
| +24 | coordinate | "coordinate" | int |
| +28 | **type** | "type" | int |
| +40 | inheritMask | "inheritMask" | int |
| +46 | joinTarget | "joinTarget" | bool |
| +47 | groundCorrection | "groundCorrection" | bool |
| +52 | stencilType | "stencilType" | int |
| +64 | frameList (variant) | "frameList" | TJS variant |
| +84 | transformOrder[0] | transformOrder[0] | int |
| +88 | transformOrder[1] | transformOrder[1] | int |
| +92 | transformOrder[2] | transformOrder[2] | int |
| +96 | transformOrder[3] | transformOrder[3] | int |
| +1980 | emoteEdit (variant) | "emoteEdit" | TJS variant |
| +2000 | meshTransform | "meshTransform" | int |
| +2004 | meshSyncChildMask | "meshSyncChildMask" | int |
| +2008 | meshDivision | "meshDivision" | int |
| +1964 | meshCombine | "meshCombine" | bool |

### Type-specific 字段

- **type==0 (layer)**: `node+2136 = "objTriPriority"` (int)
- **type==1 (shape)**: `node+32 = "shape"` (int)
- **type==3 (sub-player)**: 创建子 Player 对象，存入 node+1912
- **type==4 (emitter/particle)**: 
  - node+2164: particle, +2168: particleMaxNum, +2192: particleAccelRatio
  - node+2172: particleInheritAngle, +2176: particleInheritVelocity
  - node+2180: particleFlyDirection, +2184: particleApplyZoomToVelocity
  - node+2188: particleDeleteOutsideScreen, +2189: particleTriVolume
  - node+2200: particleMotionList (variant)
- **type==6 (wind)**: node+2380 = 0
- **type==9 (anchor)**: `node+2376 = "anchor"` (int)
- **type==12 (deflector)**: node+2576 = "stencilCompositeMaskLayerList" (variant)

### **未在构建时填充的字段**

- **node+232 (sourceWidth)** — 不在此处设置
- **node+240 (sourceHeight)** — 不在此处设置
- node+248/256 (originX/originY) — 不在此处设置
- node+264/272 — 不在此处设置
- node+280 (matrix初始值) — 不在此处设置

## sourceWidth/sourceHeight (node+232, node+240) 设置分析

### 关键发现

**sourceWidth 和 sourceHeight 不在 node tree 构建时填充。** 它们仅在以下位置被设置：

#### Player_evaluateCameraNodes (0x6C0528) — 仅 nodeType==10

```c
// 在 Player_updateLayers 末尾调用
void Player_evaluateCameraNodes(Player* player) {
    for each node where type == 10 && node->hasSource:
        if (player->field_74 == 0 || !player->field_612)
            node->field_200 = 0;  // 不处理
            continue;
        
        // 从 Player 的 source icon 字典 (Player+696) 读取
        Dict* sourceIcon = player->sourceIconDict;  // Player+696
        node->sourceIconRef = sourceIcon;
        node->field_200 = 1;
        
        // ★★ 关键: 从 source icon 读取 width/height ★★
        int w = sourceIcon.has("width") ? sourceIcon["width"] : 0;
        *(double*)(node + 232) = (double)w;     // sourceWidth
        
        int h = sourceIcon.has("height") ? sourceIcon["height"] : 0;
        *(double*)(node + 240) = (double)h;     // sourceHeight
        
        // 同时计算 origin
        *(double*)(node + 248) = w * 0.5;       // originX = width/2
        *(double*)(node + 256) = h * 0.5;       // originY = height/2
        *(double*)(node + 264) = 0;
        *(double*)(node + 272) = 0;
        *(double*)(node + 280) = 1.0;           // matrix = identity
        *(double*)(node + 288) = 1.0;
```

#### 对于非 camera 的普通 nodes (type 0, 1, 2 等)

**在整个 binary 的 motionplayer 代码 (0x690000-0x700000) 中，没有任何 STR D 指令写入 node+232 或 node+240 偏移（除了 camera 节点处理）。**

这意味着普通 node 的 sourceWidth/sourceHeight 是通过**其他方式**设置的：
1. 可能是 `sub_699940` (calcMatrix, 0x699940) 中通过 source icon 查找间接设置
2. 可能是 render tree builder (`sub_6C2334`) 在构建 render entries 时从 source icon 直接读取 width/height，而不存储到 node 中
3. **最可能**: 普通 nodes 的 width/height 直接从 source icon 的 texture metadata 获取，不缓存在 node 结构中

## PSB 层级结构

```
PSB root
  └── content (dict)
        ├── "layer" (array) ← Player_buildNodeTree 从这里递归
        │     ├── [0] (dict) — root 的第一个子 node
        │     │     ├── "label": string
        │     │     ├── "type": int (0=layer, 1=shape, 3=subplayer, 4=emitter, ...)
        │     │     ├── "coordinate": int
        │     │     ├── "frameList": variant
        │     │     ├── "inheritMask": int
        │     │     ├── "transformOrder": array[4]
        │     │     ├── "children": array ← 递归处理
        │     │     └── ...
        │     └── [1] ...
        ├── "variable" (array) ← Player_initVariables 处理
        ├── "loopTime": double
        ├── "lastTime": double
        ├── "tag": variant
        └── "priority": variant
```

## 结论

1. **Node tree 在 play/setMotion 时一次性构建**，不在每帧重建
2. **sourceWidth/sourceHeight (node+232/240) 不在构建时填充**
3. 对于 camera nodes (type==10)，sourceWidth/sourceHeight 在每帧的 `Player_evaluateCameraNodes` 中从 source icon 的 `width`/`height` 属性动态读取
4. 对于普通 nodes，width/height 信息很可能不存储在 node 结构中，而是在渲染时直接从 source icon / texture metadata 获取
