# EmotePlayer 内部 C++ 实现��度分析

**目标文件**: `libkrkr2.so` (ARM64, Android)
**分析工具**: IDA Pro + MCP
**分析日期**: 2026-04-05
**前置文档**: `analysis/MotionPlayer_EmotePlayer.md` (NCB 注册表面已覆盖)

---

## 1. 概述

本文档深入分析 EmotePlayer 的 **实际 C++ 实现**，超越 NCB ��定注册层面，追踪每个方法从 D3DEmotePlayer 壳到底层 Player 引擎的完整调用链。

### 核心发现

1. **D3DEmotePlayer 不是独立实现** — 它是一个极薄的壳（24 bytes 原生实例），所有逻辑委托给内部 Player 对象
2. **Player 对象是一个 ~1500 字节的巨型结构体**，包含 6 种类型的动画控制器 deque、变量哈希表、时间线管理等
3. **progress() 实际跳转到 Player 引擎的 sub_67D01C**，这是一个完整的物理/动画步进引擎
4. **setVariable 实际是一个复杂的类型分发系统**（switch 9 种 case），不是简单的 map 写入
5. **contains() 支持三种几何体**：圆形、矩形、四边形凸多边形，不是简单的 AABB

---

## 2. 对象层次结构

### 2.1 D3DEmotePlayer 原生实例 (24 bytes, 由 sub_68629C 创建)

```
struct D3DEmotePlayerNativeInstance {    // size = 0x18 (24 bytes)
    void**   vtable;    // +0   → off_1A18BB0
    void*    emoteObj;  // +8   → 指向 EmoteObject (sub_67DBAC 创建的 0x28 结构体)
    uint8_t  flags;     // +16  → 初始化标志
};
```

创建函数 `sub_68629C` (0x68629C):
```c
result = new(0x18);
result->vtable = off_1A18BB0;
result->emoteObj = NULL;   // +8, 后续由 load() 填充
result->flags = 0;         // +16
```

### 2.2 D3DEmotePlayer 包装层对象布局

NCB 方法中的 `a1` 参数是 D3DEmotePlayer 包装对象，其字段布局从属性 getter/setter 反推：

| 偏移 | 类型 | 字段名 | 来源 |
|------|------|--------|------|
| +8 | ptr | ownerLayerRef | sub_52FB98 → 获取宿主 Layer |
| +24 | ptr* | emoteObj | → EmoteObject 指针 (核心) |
| +32 | ptr* | emoteObj2 | → 第二个 EmoteObject (用于 clone) |
| +40 | float | baseScale | → setScale 中的基础���放因子 |
| +44 | float | userScale | → setScale 中用户缩放值 |
| +48 | bool | visible | show()/hide() 直接写此位 |
| +49 | bool | smoothing | smoothing 属性 |

### 2.3 EmoteObject 结构体 (0x28 = 40 bytes, 由 sub_67DBAC 创建)

```c
// sub_67DBAC(a1, psbPaths):
struct EmoteObject {       // size = 0x28 (40 bytes = 5 * QWORD)
    void*   resourceMgr;  // +0  → ResourceManager (sub_6A88CC)
    void*   player;       // +8  → Player 对象 (sub_67E38C, size 0x5D8)
    ptr*    loadedPSBs[];  // +16,+24,+32 → 加载的 PSB 数据引用数组
};
```

### 2.4 Player 对象布局 (size = 0x5D8 = 1496 bytes)

从大量属性访问模式反推的 Player 关键字段偏移：

| 偏移 | 类型 | 字段名 | 来源证据 |
|------|------|--------|---------|
| +256 (0x100) | deque<ptr> | coordControllers | setCoord→sub_667300 访问的第一个 deque |
| +336 (0x150) | deque<ptr> | scaleControllers | setVariable case 5 |
| +416 (0x1A0) | deque<ptr,24> | rotControllers | setVariable case 6, stride=24 |
| +576 (0x240) | deque<ptr,24> | windControllers | setVariable case 7, stride=24 |
| +656 (0x290) | deque<ptr,48> | forceControllers | setVariable case 8, stride=48 |
| +736 (0x2E0) | deque<ptr> | frameInterpolators | progress 中的帧插值器 |
| +936 (0x3A8) | hashmap | timelineHashMap | playTimeline 哈希查找 |
| +944 (0x3B0) | uint64 | timelineMapBuckets | 哈希表桶数 |
| +992 (0x3E0) | vector<ptr> | mainTimelineLabels | countMainTimelines = (p[125] - p[124]) >> 3 |
| +1000 (0x3E8) | ptr | mainTimelinesEnd | |
| +1016 (0x3F8) | vector<ptr> | diffTimelineLabels | countDiffTimelines |
| +1024 (0x400) | ptr | diffTimelinesEnd | |
| +1040 (0x410) | vector<ptr> | playingTimelines | countPlayingTimelines |
| +1048 (0x418) | ptr | playingTimelinesEnd | |
| +1064 (0x428) | ptr* | renderPlayer | → 渲染子系统 Player 实例 |
| +1072 (0x430) | ptr* | coordAnimator | setCoord 委托目标 |
| +1080 (0x438) | ptr* | scaleAnimator | setScale 委托目标 |
| +1088 (0x440) | ptr* | colorAnimator | setColor 委托目标 |
| +1096 (0x448) | ptr* | rotAnimator | setRot 委托目标 |
| +1104 (0x450) | ptr* | bustPhysics | setOuterForce("bust") |
| +1112 (0x458) | ptr* | hairPhysics | setOuterForce("h" = hair) |
| +1120 (0x460) | ptr* | partsPhysics | setOuterForce("parts") |
| +1128 (0x468) | ptr* | windSimulator | startWind 创建/管理 (size 0x61C) |
| +1136 (0x470) | float | windParam1 | startWind 参数缓存 |
| +1140 (0x474) | float | windParam2 | |
| +1144 (0x478) | float | windAmplitude | |
| +1148 (0x47C) | float | windFreqX | |
| +1152 (0x480) | float | windFreqY | |
| +1159 (0x487) | byte | physicsDisabled | 为1时跳过物���模拟 |
| +1161 (0x489) | byte | bustScaleFlag | bustScale getter 读取此位 |
| +1162 (0x48A) | byte | dirtyFlag | setCoord/setScale/setRot/setColor/setVariable 设为1 |
| +1168 (0x490) | double | meshDivisionRatio | startWind 中用于缩放 |
| +1176 (0x498) | double | meshDivisionRatio_dup | meshDivisionRatio getter |
| +1184 (0x4A0) | double | hairScale | hairScale getter |
| +1192 (0x4A8) | double | partsScale | partsScale getter |
| +1200 (0x4B0) | double | bodyScale | bodyScale getter |
| +1384 (0x568) | hashmap | variableHashMap | setVariable 的变量查找表 |
| +1392 (0x570) | uint64 | variableMapBuckets | |
| +1440 (0x5A0) | hashmap | evalResultMap | progress 中写入变量求值结果 |
| +1456 (0x5B0) | ptr* | evalResultList | 链表头，progress 后处理遍历 |

---

## 3. 方法实现详解

### 3.1 create (sub_52FD84, 0x52FD84)

**极简实现** — 仅销毁旧对象：

```c
void D3DEmotePlayer::create(this) {
    // 销毁 this+32 处的 EmoteObject
    if (this->emoteObj2) {         // +32
        sub_67F420(this->emoteObj2);  // 递归释放 EmoteObject 内部资源
        delete this->emoteObj2;
    }
    // 销毁 this+24 处的 EmoteObject
    if (this->emoteObj) {          // +24
        sub_67F420(this->emoteObj);
        delete this->emoteObj;
    }
    this->emoteObj = NULL;   // +24
    this->emoteObj2 = NULL;  // +32
}
```

**关键发现**: `create()` 不是"创建"——它是"重置/销毁"。名字容易误导。

### 3.2 load (sub_52FDD4, 0x52FDD4)

**加载 PSB 数据并创建新的 EmoteObject**：

```c
int D3DEmotePlayer::load(this, numArgs, argArray, emotePlayerObj) {
    // 1. 先销毁旧对象（与 create 相同）
    destroy_emote_obj(emotePlayerObj + 32);
    destroy_emote_obj(emotePlayerObj + 24);
    emotePlayerObj->emoteObj = NULL;
    emotePlayerObj->emoteObj2 = NULL;

    // 2. 收集所有参数到 TJS Array（动态数组）
    tjs_array paths;  // vector<tTJSVariant*>
    for (int i = 0; i < numArgs; i++) {
        variant = convertToVariant(argArray[i]);
        paths.push_back(variant);
    }

    // 3. 创建新的 EmoteObject
    void* emoteObj = new(0x28);  // 40 bytes
    sub_67DBAC(emoteObj, &paths);  // 初始化 EmoteObject + Player
    emotePlayerObj->emoteObj = emoteObj;  // 存储到 +24

    // 4. 清理 TJS Array
    cleanup(paths);
    return 0;
}
```

### 3.3 sub_67DBAC — EmoteObject 核心初始化 (0x67DBAC)

**这是最关键的函数**，创建完整的 Player 管线：

```c
void EmoteObject_init(EmoteObject* self, tjs_array** psbPaths) {
    memset(self, 0, sizeof(EmoteObject));  // 40 bytes

    // 1. 创建 ResourceManager
    ttstr kagPath = L"global.kag";
    self->resourceMgr = new(0xE8);  // ResourceManager, 232 bytes
    ResourceManager_init(self->resourceMgr, kagPath, ...);

    // 2. 从 ResourceManager 获取 drawDevice (E-mote 渲染上下文)
    drawDevice = ResourceManager_getDrawDevice(self->resourceMgr, 1, 0);

    // 3. 创建 Player 对象 (核心！)
    player = new(0x5D8);  // Player, 1496 bytes
    Player_init(player, drawDevice);  // sub_67E38C
    self->player = player;

    // 4. 逐个加载 PSB 文件到 ResourceManager
    for (path in *psbPaths) {
        ResourceManager_loadResource(self->resourceMgr, path);
    }

    // 5. 从 "metadata" → "base" 获取角色/动画名称
    metadata = resourceMgr->getMember("metadata");
    base = metadata->getMember("base");

    // 6. 查找 "chara" 和 "motion" 字段
    charaName = base->getMember("chara");
    motionName = base->getMember("motion");

    // 7. 将最后一个 PSB 路径设置为 renderPlayer 的上下��
    lastPSB = psbPaths->back();
    renderPlayer = player->renderPlayer;  // +1064
    renderPlayer->context = lastPSB;

    // 8. 设置角色查找回调
    Player_setCharaCallback(player, charaName);

    // 9. 调用 Player_play 启动初始动画
    Player_play(player->renderPlayer, 1, motionName);

    // 10. 完成初始化
    Player_finalize(player, metadata);
}
```

**关键发现**:
- EmoteObject 内含完整的 ResourceManager + Player 管线
- PSB 元数据中的 `metadata.base.chara` 和 `metadata.base.motion` 决定初始角色和动画
- 创建后立即调用 `Player_play` 启动首个 motion

---

### 3.4 show/hide (sub_530068/sub_530074)

**极简**：
```c
void show(this)  { *(byte*)(this + 48) = 1; }
void hide(this)  { *(byte*)(this + 48) = 0; }
// visible getter: return *(byte*)(this + 48);
// visible setter: *(byte*)(this + 48) = value & 1;
```

---

### 3.5 setCoord (sub_5301EC, 0x5301EC)

```c
void setCoord(this, x, y, transitionTime, easeType) {
    player = *(this->emoteObj + 8);  // Player*
    float coords[2] = {(float)x, (float)y};
    animator = player->coordAnimator;  // player+1072
    prevBustScale = player->bustScaleFlag;  // player+1161 (byte)
    player->dirtyFlag = 1;                  // player+1162
    sub_667300(animator, coords, prevBustScale, transitionTime, easeType);
}
```

### 3.6 setScale (sub_530260, 0x530260)

**注意：包含基���缩放乘法！**

```c
void setScale(this, scale, transitionTime, easeType) {
    float baseScale = *(float*)(this + 40);   // 基础缩放因子
    *(float*)(this + 44) = (float)scale;      // 保存用户缩放值
    player = *(this->emoteObj + 8);
    float finalScale = baseScale * (float)scale;  // 两者相乘!
    animator = player->scaleAnimator;  // player+1080
    player->dirtyFlag = 1;
    sub_667300(animator, &finalScale, player->bustScaleFlag, transitionTime, easeType);
}
```

**关键发现**: 实际缩放值 = `baseScale * userScale`。本地代码仅存了单一 `_scale`，缺少这个乘法逻辑。

### 3.7 setRot (sub_5302E4, 0x5302E4)

```c
void setRot(this, rot, transitionTime, easeType) {
    player = *(this->emoteObj + 8);
    animator = player->rotAnimator;  // player+1096
    player->dirtyFlag = 1;
    sub_666490(animator, player->bustScaleFlag, rot, transitionTime, easeType);
}
```

`sub_666490` 特殊之处：在设置前先将角度归一化到 [0, 2*PI)：
```c
while (rot < 0.0) rot += 6.2832;
while (rot >= 6.2832) rot -= 6.2832;
```

### 3.8 setColor (sub_530314, 0x530314)

```c
void setColor(this, color, transitionTime, easeType) {
    player = *(this->emoteObj + 8);
    // 解包 RGBA 到 4 个 float
    float rgba[4] = {
        (float)(color & 0xFF),         // R
        (float)((color >> 8) & 0xFF),  // G
        (float)((color >> 16) & 0xFF), // B
        (float)((color >> 24) & 0xFF)  // A
    };
    animator = player->colorAnimator;  // player+1088
    player->dirtyFlag = 1;
    sub_667300(animator, rgba, player->bustScaleFlag, transitionTime, easeType);
}
```

### 3.9 getScale / getRot / getColor

这些都是**桩实现**：
```c
double getScale() { return 1.0; }   // 0x5302DC — 永远返回 1.0!
double getRot()   { return 0.0; }   // 0x53030C — 永远返回 0.0!
int    getColor() { return 0; }     // 0x530320 — 永远返回 0!
```

**重要发现**: 在二进制中，这三个 getter 都是硬编码返回值，不读取任何字段。本地代码返回 `_scale`/`_rot`/`_color` 成员是合理的近似，但二进制的行为确实如此。

---

### 3.10 setVariable (sub_5305C8 → sub_671228, 0x671228)

`sub_5305C8` 是极薄包装：
```c
void setVariable(this, label, value, ...) {
    player = *(this->emoteObj + 8);
    sub_671228(player, label, value, ...);
}
```

`sub_671228` (Player::setVariable) 是一个**复杂的类型分发系统**：

```c
void Player_setVariable(player, label, value, transitionTime, easeType) {
    // 1. 计算变量名哈希
    hash = computeStringHash(label);

    // 2. 在变量哈希表中查找
    entry = hashmap_find(player+1384, hash % player->varMapBuckets, label);

    if (!entry || !entry->controllerPtr) {
        // 变量不存在 → 写入评估结果表
        evalSlot = hashmap_getOrInsert(player+1440, label);
        *evalSlot = value;
        return;
    }

    // 3. 计算 ease 权重
    if (easeType == 0.0) weight = 1.0;
    else if (easeType > 0.0) weight = easeType + 1.0;
    else weight = 1.0 / (1.0 - easeType);

    player->dirtyFlag = 1;  // +1162

    // 4. 根据控制器类型分发
    switch (entry->controllerType) {  // *(entry+16)
        case 0: case 1: case 2:
            // 基础类型 — 如果 physicsDisabled 则跳过
            if (player->physicsDisabled) break;  // +1159
            return;

        case 4:  // 坐标控制器 (deque at +256)
            controller = deque_at(player+256, entry->index);
            sub_6638B0(controller, bustScaleFlag, value, transitionTime, weight);
            break;

        case 5:  // 缩放控制器 (deque at +336)
            controller = deque_at(player+336, entry->index);
            sub_6652D4(controller, bustScaleFlag, value, transitionTime, weight);
            break;

        case 6:  // 旋转控制器 (deque at +416, stride 24)
            controller = deque_at_24(player+416, entry->index);
            // 特殊: 检查 label 匹配 controller 的 name1 或 name2
            if (label matches controller->name1) {
                *(controller + 108) = (int)value;  // 直接设整数值
            } else if (label matches controller->name2) {
                sub_665E34(controller, bustScaleFlag, value, transitionTime, weight);
            }
            break;

        case 7:  // 风力控制器 (deque at +576, stride 24)
            controller = deque_at_24(player+576, entry->index);
            if (!controller->enabled) return;
            sub_667300(controller, &value, bustScaleFlag, transitionTime, weight);
            break;

        case 8:  // 外力控制器 (deque at +656, stride 48)
            controller = deque_at_48(player+656, entry->index);
            if (!controller->enabled) return;
            sub_6681E4(controller, bustScaleFlag, value, transitionTime, weight);
            break;
    }
}
```

**关键发现**: setVariable 不是简单的键值存储！它是一个将变量名映射到具体动画控制器的分发系统。9 种 case 对应不同类型的动画参数（坐标、缩放、旋转、风力、外力等）。

### 3.11 getVariable (sub_5305D4 → sub_533E1C, 0x533E1C)

```c
double getVariable(this, label) {
    player = *(this->emoteObj + 8);
    renderPlayer = player->renderPlayer;  // +1064

    // 1. 检查是否是已注册的差分变量
    isDiffVar = sub_6CD16C(renderPlayer, label);

    if (isDiffVar) {
        // 差分变量 → 从差分系统获取值
        return sub_6CD39C(renderPlayer, label);
    } else {
        // 普通变量 → 从普通系统获取值
        return sub_6CD23C(renderPlayer, label);
    }
}
```

---

### 3.12 playTimeline (sub_530880 → sub_672F70, 0x672F70)

`sub_530880` 只是包装：
```c
void playTimeline(this, label, flags) {
    player = *(this->emoteObj + 8);
    sub_672F70(player, label, flags);
}
```

`sub_672F70` (Player::playTimeline) 的完整逻辑：

```c
void Player_playTimeline(player, label, flags) {
    // 1. 如果是 parallel 模式 (flags & 1)，先停止所有现有时间线
    if (flags & 1) {
        ttstr emptyLabel = L"";  // word_1522752
        Player_stopTimeline(player, emptyLabel);  // sub_67C2A0
    }

    // 2. 在时间线哈希表中查找 label
    hash = computeStringHash(label);
    entry = hashmap_find(player+936, hash % player->timelineMapBuckets, label);

    if (!entry || !entry->timelineData) {
        // 时间线不存在 → 抛出错误
        error = "timeline label not found '" + label + "'.";
        throw error;
        return;
    }

    timeline = entry->timelineData;

    // 3. 检查是否已在 playingTimelines 列表���
    playingList = &player->playingTimelines;  // +1040
    duplicateCount = 0;
    for (item in playingList) {
        if (strcmp(item, label) == 0) duplicateCount++;
    }

    if (duplicateCount > 0) {
        // 已存在 → ���接使用现有条目
    } else {
        // 不存在 → 添加到 playingTimelines 列表
        playingList.push_back(label);
    }

    // 4. 初始化时间线播放节点（如果不存在则创建）
    playNode = timeline + 16;
    if (!playNode->initialized) {
        sub_66FC5C(player, playNode);  // 初始化播放���点
    }

    // 5. 设置播放状态
    sub_670840(playNode, flags);          // 配置播放参数
    sub_671A50(player, playNode, 0.0);    // 重置播放时间到 0
}
```

### 3.13 stopTimeline (sub_530898 → sub_67C2A0, 0x67C2A0)

```c
void Player_stopTimeline(player, label) {
    if (label != NULL && *label != 0) {
        // 停止特定时间线 → 从 playingTimelines 列表中移除
        result = remove_from_list(player->playingTimelines, label);
        if (result != listEnd) {
            // sub_68C200: 调整列表并释放条目
        }
    } else {
        // 空 label → 停止所有时间线
        for (item in playingTimelines) {
            release(item);
        }
        player->playingTimelinesEnd = player->playingTimelines;  // 清空列表
    }
}
```

---

### 3.14 progress (sub_530A5C → sub_67D01C, 0x67D01C)

这是 EmotePlayer 最复杂的函数。`sub_530A5C` 包装了完整的物理/动画步进引擎：

```c
void progress(this, deltaTime) {
    if (deltaTime == 0.0) return;

    player = *(this->emoteObj + 8);

    // === 委托到 Player 的完整 progress 引擎 (sub_67D01C) ===

    // 1. 调用 Player_preProgress (sub_671764)
    //    - 处理所有播放中的时间线
    //    - 推进时间、检测循环/结束

    // 2. 主循环：以最大 1.1 秒的步长处理 deltaTime
    while (player->dirtyFlag) {
        dt = fmin(deltaTime, 1.1);  // 单步上限 1.1 秒
        player->dirtyFlag = 0;

        // 2a. 更新 6 种控制器类型
        //     每种控制器对应一个 deque，遍历执行 step(dt)

        // 坐标控制器 (player+256, stride 16)
        for (ctrl in deque(player+256 .. player+288)) {
            sub_663BDC(ctrl, &output, dt);     // 坐标插值
            evalMap[ctrl.varKey] = output;       // 写入结果
        }

        // 缩放控制器 (player+336, stride 16)
        for (ctrl in deque(player+336 .. player+368)) {
            sub_665600(ctrl, &output, dt);     // 缩放插值
            evalMap[ctrl.varKey] = output;
        }

        // 旋转控制器 (player+416, stride 24)
        for (ctrl in deque(player+416 .. player+448)) {
            sub_666068(ctrl, &rotOutput, &altOutput, dt);
            evalMap[ctrl.varKey1] = rotOutput;
            evalMap[ctrl.varKey2] = altOutput;
        }

        // 外力控制器 (player+656, stride 48)
        for (ctrl in deque(player+656 .. player+688)) {
            sub_668470(ctrl, &output, dt);
            evalMap[ctrl.varKey] = output;
        }

        // 风力控制器 (player+576, stride 24)
        for (ctrl in deque(player+576 .. player+608)) {
            sub_666BF8(ctrl, &output, dt);
            evalMap[ctrl.varKey] = output;
        }

        // 帧插值器 (player+736, stride 16)
        for (interp in deque(player+736 .. player+768)) {
            // 帧推进逻辑（带循环）
            currentFrame = interp->currentFrame;
            timeInFrame += dt;
            while (timeInFrame >= frameLength) {
                currentFrame = (currentFrame + 1) % totalFrames;
                timeInFrame -= frameLength;
            }
            // 线性插值当前帧和下一帧
            t = timeInFrame / frameLength;
            value = lerp(frame[currentFrame], frame[currentFrame+1], t);
            evalMap[interp->varKey] = value;
        }

        // 2b. 更新时间线状态 (sub_6766E0)
        sub_6766E0(player, dt);

        // 2c. 如果有风模拟器，更新风
        if (player->windSimulator && player->windSimulator->active) {
            sub_6687E8(dt);  // 风物理步进
        }

        deltaTime -= dt;
    }

    // 3. 后处理：遍历评估结果，应用到渲染系统
    for (evalEntry = player->evalResultList; evalEntry; evalEntry = evalEntry->next) {
        sub_67C560(player, evalEntry->key, evalEntry->value);
        savedValue = evalEntry->value;
        needsNegate = sub_67C6B0(player, evalEntry->key);
        finalValue = needsNegate ? -savedValue : savedValue;
        // 发送到 renderPlayer 的变量系统
        sub_6C4668(player->renderPlayer, evalEntry->key, 0, finalValue);
    }

    // 4. 更新 bustScaleFlag 相关状态 (sub_67C8A8)
    sub_67C8A8(player);

    // 5. 渲染更新 (sub_6D2A54)
    sub_6D2A54(player->renderPlayer, 0, deltaTime);

    // 6. 物理模拟（如果未禁用）
    if (deltaTime != 0.0 && !player->physicsDisabled) {
        float fdt = (float)deltaTime;
        sub_666BF8(player->bustPhysics, &output, fdt);   // +1104 bust
        sub_666BF8(player->hairPhysics, &output, fdt);   // +1112 hair
        sub_666BF8(player->partsPhysics, &output, fdt);  // +1120 parts
        sub_67B748(player, fdt);                          // 基础物理
        // bust/hair 物理弹簧更新
        sub_67BCE8(player, player->hairPhysics, player+80,
                   player->hairScale, fdt);    // +1184
        sub_67BCE8(player, player->partsPhysics, player+160,
                   player->partsScale, fdt);   // +1192
    }
}
```

**关键发现**:
1. progress 不是简单的时间推进——它是完整的物理引擎步进
2. 单步上限 1.1 秒，大于此值会分多步执行
3. 包含 6 种不同类型的动画控制器更新
4. 物理模拟（bust/hair/parts 弹簧）在主循环后单独执行
5. `physicsDisabled` (player+1159) 可以跳过整个物理部分

---

### 3.15 startWind / stopWind (sub_6709AC)

```c
void Player_startWind(player, minAngle, maxAngle, amplitude, freqX, freqY) {
    // 归一化参数（如果 amplitude < 0，交换 min/max）
    absAmplitude = fabs(amplitude);
    if (amplitude < 0) swap(minAngle, maxAngle);

    // 如果参数无效，销毁风模拟器
    if (absAmplitude == 0.0 || minAngle == maxAngle || (freqX == 0.0 && freqY == 0.0)) {
        if (player->windSimulator) {     // +1128
            delete player->windSimulator;
            player->windSimulator = NULL;
        }
        return;
    }

    // 如果参数变化需要重建模拟器
    if (player->windSimulator == NULL ||
        player->windParam1 != min || player->windParam2 != max) {
        if (player->windSimulator) delete player->windSimulator;
        windSim = new(0x61C);  // 1564 bytes 的风模拟器
        scaledMin = minAngle / player->meshDivisionRatio;  // +1168
        scaledMax = maxAngle / player->meshDivisionRatio;
        sub_670AFC(windSim, scaledMin, scaledMax);  // 初始化
        player->windSimulator = windSim;
    }

    // 保存参数
    player->windParam1 = minAngle;      // +1136
    player->windParam2 = maxAngle;      // +1140
    player->windAmplitude = absAmplitude;// +1144
    player->windFreqX = freqX;          // +1148
    player->windFreqY = freqY;          // +1152

    // 配置风模拟器
    windSim->freqX = freqX;             // +1548
    windSim->freqY = freqY;             // +1552
    scaledAmplitude = absAmplitude / player->meshDivisionRatio;
    direction = (windSim->phase < windSim->prevPhase) ? -1.0 : 1.0;
    windSim->active = 1;                // +1544
    windSim->scaledAmplitude = direction * scaledAmplitude;  // +1556
    windSim->counter = 0;               // +1560
}

// stopWind 就是 startWind(0,0,0,0,0)
void stopWind(player) {
    startWind(player, 0, 0, 0, 0, 0);
}
```

---

### 3.16 setOuterForce (sub_672D58)

```c
void Player_setOuterForce(player, label, x, y, transitionTime, easeType) {
    if (!label || !*label) return;

    float coords[2] = {(float)x, (float)y};

    if (strcmp(label, L"bust") == 0) {
        target = player->bustPhysics;    // +1104
    } else if (strcmp(label, "h") == 0) {  // 注意: "h" 不是 "hair"!
        target = player->hairPhysics;    // +1112
    } else if (strcmp(label, L"parts") == 0) {
        target = player->partsPhysics;   // +1120
    } else {
        return;  // 未知标签 → 忽略
    }

    sub_667300(target, coords, player->bustScaleFlag, transitionTime, easeType);
}
```

**关键发现**: hair 的 outerForce 标签是 `"h"`，不是 `"hair"`。这个字符��比较使用的是 sub_9B1ED0 (wcsicmp)。

---

### 3.17 contains (sub_530B5C → sub_690DF0, 0x690DF0)

```c
bool contains(this, label, x, y) {
    player = *(this->emoteObj + 8);
    renderPlayer = player->renderPlayer;  // +1064

    // 1. 在 renderPlayer 中查找 label 对应的碰撞体
    collider = sub_6B5AD8(renderPlayer, label, 1);
    if (!collider) return false;

    // 2. 碰撞检测 (sub_690DF0)
    hitData = collider + 1664;  // 碰撞几何数据
    return hitTest(hitData, x, y);
}

// sub_690DF0: 三种碰撞体类型
bool hitTest(hitData, x, y) {
    int type = *(int*)hitData;

    switch (type) {
        case 1:  // 圆形
            cx = hitData[1]; cy = hitData[2]; r = hitData[3];
            return (x-cx)*(x-cx) + (y-cy)*(y-cy) <= r*r;

        case 2:  // 矩形 (AABB)
            left = hitData[4]; right = hitData[6];
            top = hitData[5];  bottom = hitData[7];
            return x >= left && x < right && y >= top && y < bottom;

        case 3:  // 四边形 (凸多边形，4 顶点)
            // 使用叉积符号法判断点是否在凸四边形内
            // 顶点在 hitData[8..15] (4 个 xy 对)
            sign = cross(v0→v1, v0→v2) >= 0 ? 1.0 : -1.0;
            for (i = 0..3) {
                edge = v[i] → v[(i+1)%4];
                if (sign * cross(edge, point-v[i]) > 0)
                    return false;
            }
            return true;

        default:
            return false;
    }
}
```

**关键发现**: 碰撞体类型由 PSB 数据中的 mesh 定义决定，不是简单的 AABB。本地代码的 AABB 近似仅覆盖了 case 2。

---

### 3.18 animating (sub_673F98)

`sub_530A38` → `sub_673F98`: 这是一个极其复杂的函数（~3000 行伪代码）。其核心逻辑是：

```c
bool Player_isAnimating(player) {
    // 1. 检查 3 个基础动画控制器是否有活动动画
    if (player->ctrl1->hasQueuedKeyframes || player->ctrl1->isPlaying) return true;
    if (player->ctrl2->hasQueuedKeyframes || player->ctrl2->isPlaying) return true;
    if (player->ctrl3->hasQueuedKeyframes || player->ctrl3->isPlaying) return true;

    // 2. 检查 playingTimelines 中每个时间线
    for (timeline in playingTimelines) {
        entry = hashmap_find(timelineMap, timeline);
        if (entry && entry->playNode) {
            // 检查 playNode 内的所有子节点
            // 如果任一子节点有活动关键帧队列 → return true
        }
    }

    // 3. 检查 6 种 deque 控制器中的所有条目
    //    对每个条目，在 evalResultMap 中查找其 label
    //    如果 label 已在 evalResultMap 中（说明正在被求值）→ return true

    // 分别检查:
    // - deque at player+656 (stride 48, force controllers)
    // - deque at player+576 (stride 24, wind controllers)
    // - deque at player+256 (stride 16, coord controllers)
    // - deque at player+336 (stride 16, scale controllers)
    // - deque at player+416 (stride 24, rot controllers, 检查 name1 + name2)

    return false;
}
```

---

## 4. 属性访问模式总结

### 直接访问 Player 字段的属性

| 属性 | 访问路径 | Player 偏移 |
|------|---------|-------------|
| `meshDivisionRatio` | this+24→+8→+1176 | player+1176 |
| `bustScale` | this+24→+8→+1161 | player+1161 (byte) |
| `hairScale` | this+24→+8→+1184 | player+1184 |
| `partsScale` | this+24→+8→+1192 | player+1192 |
| `bodyScale` | this+24→+8→+1200 | player+1200 |
| `countMainTimelines` | this+24→+8→(+1000 - +992)>>3 | player+992/1000 |
| `countDiffTimelines` | this+24→+8→(+1024 - +1016)>>3 | player+1016/1024 |
| `countPlayingTimelines` | this+24→+8→(+1048 - +1040)>>3 | player+1040/1048 |
| `playCallback` | this+24→+8→+1064→+200→+1584 | renderPlayer→...→+1584 |

### 直接访问包装对象字段的属性

| 属性 | 偏移 | 类型 |
|------|------|------|
| `visible` | this+48 | byte |
| `smoothing` | this+49 | byte |

---

## 5. 调用链总览

```
D3DEmotePlayer NCB 方法
│
├── 直接字段操作 (show/hide/visible/smoothing)
│   └── this+48, this+49
│
├── 委托到 Player 动画控制器 (setCoord/setScale/setRot/setColor)
│   ├── this+24 → EmoteObject+8 → Player
│   ├── Player+1072/1080/1088/1096 → 对应 Animator
│   └── sub_667300/sub_666490 → deque 关键帧管理
│
├── 委托到 Player 变量系统 (setVariable/getVariable)
│   ├── setVariable → sub_671228 → 9-case 类型分发
│   └── getVariable → sub_533E1C → diff/normal 分支
│
├── 委托到 Player 时间线系统 (playTimeline/stopTimeline)
│   ├── playTimeline → sub_672F70 → 哈希查找 + 播放节点创建
│   └── stopTimeline → sub_67C2A0 → 列表移除
│
├── 委托到 Player 物理系统 (startWind/setOuterForce)
│   ├── startWind → sub_6709AC → 风模拟器 (0x61C bytes)
│   └── setOuterForce → sub_672D58 → bust/hair/parts 三路分发
│
├── 委托到 Player 步进引擎 (progress)
│   └── sub_67D01C → 6 种控制器更新 + 物理模拟 + 渲染更新
│
└── 委托到 渲染子系统 (contains)
    └── sub_690DF0 → 3 种碰撞体类型 (circle/rect/quad)
```

---

## 6. 本地代码与二进制差异

| 项目 | 二进制实现 | 本地代码 | 差异程度 |
|------|-----------|---------|---------|
| **对象架构** | 24-byte 壳 → 40-byte EmoteObj → 1496-byte Player | shared_ptr<Runtime> 扁平结构 | 重大 |
| **setScale** | baseScale * userScale 乘法 | 单一 _scale | 中等 |
| **setVariable** | 9-case 类型分发到 deque 控制器 | std::map 写入 | 重大 |
| **getVariable** | diff/normal 双路查询 | map 查找 + snapshot | 中等 |
| **progress** | 6 种控制器步进 + 物理弹簧 | stepTimelines 简单时间推进 | 重大 |
| **contains** | 3 种碰撞体 (circle/rect/quad) | AABB 近似 | 中等 |
| **startWind** | 0x61C bytes 风模拟器 | 简单存值 | 重大 |
| **playTimeline** | 哈希表 + 播放节点 + 错误抛出 | map 写入 | 中等 |
| **getScale/getRot/getColor** | 硬编码返回 1.0/0.0/0 | 返回成员变量 | 低 (二进制也是桩) |
| **show/hide** | 直接写 byte | 写 _visible bool | 一致 |

---

## 7. 关键函数地址索引

| 函数 | 地址 | 大小 | 用途 |
|------|------|------|------|
| EmotePlayer 类加载 | 0x685BC0 | 260 | NCB 类注册入口 |
| EmotePlayerNativeInstance 创建 | 0x68629C | 44 | 24-byte 壳创建 |
| EmoteObject 初始化 | 0x67DBAC | ~2400 | ResourceManager + Player 创建 |
| EmoteObject 销毁 | 0x67F420 | ~160 | 递归释放资源 |
| create (reset) | 0x52FD84 | 80 | 销毁 EmoteObject |
| load | 0x52FDD4 | ~360 | 加载 PSB + 创建 EmoteObject |
| show | 0x530068 | 12 | this+48 = 1 |
| hide | 0x530074 | 8 | this+48 = 0 |
| setCoord | 0x5301EC | 116 | → Player+1072 |
| setScale | 0x530260 | 132 | baseScale * userScale |
| setRot | 0x5302E4 | 40 | → Player+1096 |
| setColor | 0x530314 | ~200 | RGBA 解包 |
| getScale | 0x5302DC | 8 | 返回 1.0 (桩) |
| getRot | 0x53030C | 8 | 返回 0.0 (桩) |
| getColor | 0x530320 | 8 | 返回 0 (桩) |
| setVariable | 0x5305C8 → 0x671228 | ~1300 | 9-case 类型分发 |
| getVariable | 0x5305D4 → 0x533E1C | ~220 | diff/normal 双路 |
| playTimeline | 0x530880 → 0x672F70 | ~720 | 哈希查找 + 节点创建 |
| stopTimeline | 0x530898 → 0x67C2A0 | ~160 | 列表移除 |
| progress | 0x530A5C → 0x67D01C | ~1200 | 完整步进引擎 |
| startWind | 0x530680 → 0x6709AC | ~300 | 风模拟器管理 |
| stopWind | 0x53068C | 36 | startWind(0,0,0,0,0) |
| setOuterForce | 0x530A8C → 0x672D58 | ~200 | bust/h/parts 三路 |
| contains | 0x530B5C → 0x690DF0 | ~300 | 3 种碰撞体 |
| animating | 0x530A38 → 0x673F98 | ~3000 | 全面状态检查 |
| Animator::set | 0x667300 | ~400 | deque 关键帧管理 |
| RotAnimator::set | 0x666490 | ~400 | 角度归一化 + deque |
| hitTest | 0x690DF0 | ~300 | circle/rect/quad |
