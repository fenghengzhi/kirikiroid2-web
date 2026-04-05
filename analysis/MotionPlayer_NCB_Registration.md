# MotionPlayer 插件 NCB 类注册完整分析

**目标文件**: `libkrkr2.so` (ARM64, Android)
**分析工具**: IDA Pro + MCP
**分析日期**: 2026-04-05

---

## 1. 初始化链概览

MotionPlayer 插件的完整初始化链如下：

```
ELF .init_array (0x19EA308)
  └─> motionplayer_static_init (0x42EE18)
       ├─> 初始化全局状态
       ├─> 注册 "motionplayer.dll" 插件条目
       │    ├─> NCB 类 "BezierPatch"，挂载到 "LayerMeshSupport" 覆盖 "Layer"
       │    └─> NCB 命名空间 "Motion"，关联 motionplayer.dll
       └─> __cxa_atexit 注册析构函��

当引擎加载 "motionplayer.dll" 时（Plugins.link 调用）:
  Motion_namespace_ncb_factory (0x6FC06C)
    └─> Motion_namespace_ncb_register (0x6D9B08)
         ├─> 23 个常量定义
         ├─> 10 个子类注册 (Point, Circle, Rect, Quad, LayerGetter,
         │   Player, SourceCache, ObjSource, ResourceManager,
         │   SeparateLayerAdaptor, D3DAdaptor)
         └─> 2 个命��空间级函数 (doAlphaMaskOperation, getD3DAvailable)

当引擎加载 "emoteplayer.dll" 时:
  emoteplayer_entry (0x682528)
    ├─> 查找 "Motion" 命名空间
    ├─> EmotePlayer_ncb_register (0x685BC0)
    │    └─> EmotePlayer_ncb_registerMembers (0x67FAC8)
    ├─> 注册 EmotePlayer 类到 Motion.EmotePlayer
    ├─> 注册 setEmotePSBDecryptSeed (0x685D30) 到 Motion.ResourceManager
    └─> 注册 setEmotePSBDecryptFunc (loc_685E60) 到 Motion.ResourceManager
```

---

## 2. 静态初始化函数

### motionplayer_static_init (0x42EE18, 0x154 bytes)

```c
void motionplayer_static_init() {
    // 初始化全局数据结构
    qword_1AB80F8 = 0;
    qword_1AB80D8 = 0;
    // ... 零初始化全局状态

    // 复制预配置数据
    xmmword_1AB80B0 = xmmword_14D7C90;  // 预设配置块
    xmmword_1AB80C0 = xmmword_14D7CA0;

    // 注册析构函数
    __cxa_atexit(map_destructor, &qword_1AB80D0, &qword_1AA4000);
    __cxa_atexit(sub_6925FC, &qword_1AB8108, &qword_1AA4000);

    // 注册 BezierPatch NCB 类
    //   挂载到 "LayerMeshSupport" 覆盖 "Layer"
    qword_1AB82B0 = &qword_1AB8278;
    qword_1AB82B8 = L"BezierPatch";
    qword_1AB82A8 = L"motionplayer.dll";
    qword_1AB8290 = {L"LayerMeshSupport", L"Layer"};

    // 注册 Motion 命名空间 NCB 类
    //   链接到 motionplayer.dll
    qword_1AB8508 = off_1A21C18;  // Motion 命名空间工厂 vtable
    qword_1AB8510 = L"motionplayer.dll";
    qword_1AB8520 = L"Motion";

    // 将注册项加入全局 NCB 链表 (xmmword_1AB8920)
    __cxa_atexit(Function_base_destructor, &xmmword_1AB82E0, &qword_1AA4000);
}
```

**关键发现**:
- motionplayer.dll 注册了两个顶层 NCB 类: "BezierPatch"（Layer 扩展）和 "Motion"（命名空间）
- BezierPatch 通过 "LayerMeshSupport" overlay 注入到 "Layer" 类中
- Motion 是一个命名空间类，包含所有 MotionPlayer 子类

---

## 3. Motion 命名空间注册

### Motion_namespace_ncb_register (0x6D9B08)

这是 Motion 命名空间的核心注册函数，由 Motion_namespace_ncb_factory (0x6FC06C) 在插件加载时调用。

#### 3.1 常量定义 (23 个)

| 常量名 | 值 | 说明 |
|--------|-----|------|
| **LayerType 枚举** | | |
| `LayerTypeObj` | 0 | 对象层 |
| `LayerTypeShape` | 1 | 形状层 |
| `LayerTypeLayout` | 2 | 布局层 |
| `LayerTypeMotion` | 3 | 动画层 |
| `LayerTypeParticle` | 4 | 粒子层 |
| `LayerTypeCamera` | 5 | 摄像机层 |
| **ShapeType 枚举** | | |
| `ShapeTypePoint` | 0 | 点 |
| `ShapeTypeCircle` | 1 | 圆 |
| `ShapeTypeRect` | 2 | 矩形 |
| `ShapeTypeQuad` | 3 | 四边形 |
| **PlayFlag 位标志** | | |
| `PlayFlagForce` | 1 (bit 0) | 强制播放 |
| `PlayFlagChain` | 2 (bit 1) | 链式播放 |
| `PlayFlagAsCan` | 4 (bit 2) | 尽可能播放 |
| `PlayFlagJoin` | 8 (bit 3) | 合并播放 |
| `PlayFlagStealth` | 16 (bit 4) | 静默播放 |
| **TransformOrder 枚举** | | |
| `TransformOrderFlip` | 0 | 翻转优先 |
| `TransformOrderAngle` | 1 | 角度优先 |
| `TransformOrderZoom` | 2 | 缩放优先 |
| `TransformOrderSlant` | 3 | 倾斜优先 |
| **Coordinate 枚举** | | |
| `CoordinateRecutangularXY` | 0 | XY 直角坐标系 |
| `CoordinateRecutangularXZ` | 1 | XZ 直角坐标系 |
| **MaskMode 枚举** | | |
| `MaskModeStencil` | 0 | 模板遮罩 |
| `MaskModeAlpha` | 1 | Alpha 遮罩 |

注: 所有常量通过 ncb_addConstant_wrapper 注册，flags = 0x10000。

#### 3.2 子类注册 (10 个)

每个��类遵循 NCB 注册模式:
1. 调用 `XXX_ncb_register(L"ClassName", enabled)` 注册类
2. 如果成功，分配 vtable wrapper 并调用 `sub_6FCAAC(parent, L"ClassName", wrapper)` 挂载���命名空间

| 子���名 | 注册函数 | 成员注册函数 | vtable |
|--------|---------|-------------|--------|
| `Point` | Motion_Point_ncb_register (0x6FC700) | Point_ncb_registerMembers (0x690FBC) | off_1A21CA8 |
| `Circle` | sub_6FCD38 (0x6FCD38) | Circle_ncb_registerMembers (0x691300) | off_1A21D10 |
| `Rect` | sub_6FD128 (0x6FD128) | Rect_ncb_registerMembers (0x6916A4) | off_1A21D78 |
| `Quad` | sub_6FD518 (0x6FD518) | Quad_ncb_registerMembers (0x691AD0) | off_1A21DE0 |
| `LayerGetter` | sub_6FD908 (0x6FD908) | LayerGetter_ncb_registerMembers (0x69B350) | off_1A21E48 |
| `Player` | Motion_Player_ncb_register (func) | Player_ncb_registerMembers (0x6D69C8) | off_1A21EB0 |
| `SourceCache` | Motion_SourceCache_ncb_register (func) | SourceCache_ncb_registerMembers (0x6A85A8) | off_1A21F18 |
| `ObjSource` | Motion_ObjSource_ncb_register (func) | ObjSource_ncb_registerMembers (0x69CCB8) | off_1A21F80 |
| `ResourceManager` | Motion_ResourceManager_ncb_register (func) | ResourceManager_ncb_registerMembers (0x6AB8BC) | off_1A21FE8 |
| `SeparateLayerAdaptor` | SeparateLayerAdaptor_ncb_register (func) | SeparateLayerAdaptor_ncb_registerMembers (0x6ABFAC) | off_1A22050 |
| `D3DAdaptor` | Motion_D3DAdaptor_ncb_register (func) | D3DAdaptor_ncb_registerMembers (0x6ACE94) | off_1A220B8 |

#### 3.3 命名空间级函数 (2 个)

在 D3DAdaptor 注册之后，还直接在 Motion 命名空间上注册��两个函数:

| 方法名 | 实现函数 | 说明 |
|--------|---------|------|
| `doAlphaMaskOperation` | Motion_doAlphaMaskOperation (0x6AF104) | Alpha 遮罩操作 |
| `getD3DAvailable` | Motion_getD3DAvailable (0x6B0960) | 检查 D3D 是否可用 |

---

## 4. 各子类成员详表

### 4.1 Motion.Point (0x690FBC)

| 类型 | 名称 | 实现函数 | 说明 |
|------|------|---------|------|
| Function | (constructor) | 内联 | 构造函数 |
| Property(RO) | type | sub_691248 (getter) | ��状类型 = ShapeTypePoint |
| Function | contains | sub_690DF0 | 点包含判定 |
| Property(RO) | x | sub_691250 (getter) | X 坐标 |
| Property(RO) | y | sub_691258 (getter) | Y 坐标 |

### 4.2 Motion.Circle (0x691300)

| 类型 | 名称 | 实现函数 | 说明 |
|------|------|---------|------|
| Function | (constructor) | 内联 | 构造函数 |
| Property(RO) | type | sub_691248 (getter) | 形状类型 = ShapeTypeCircle |
| Function | contains | sub_690DF0 | 圆形包含判定 |
| Property(RO) | x | sub_691250 (getter) | 圆心 X |
| Property(RO) | y | sub_691258 (getter) | 圆心 Y |
| Property(RO) | r | sub_6915FC (getter) | 半径 |

### 4.3 Motion.Rect (0x6916A4)

| 类型 | 名称 | 实现函数 | 说明 |
|------|------|---------|------|
| Function | (constructor) | 内联 | 构造函数 |
| Property(RO) | type | sub_691248 (getter) | 形状类型 = ShapeTypeRect |
| Function | contains | sub_690DF0 | 矩形包含判定 |
| Property(RO) | l | sub_691A00 (getter) | 左边 |
| Property(RO) | t | sub_691A08 (getter) | 上边 |
| Property(RO) | w | sub_691A10 (getter) | 宽度 |
| Property(RO) | h | sub_691A20 (getter) | 高度 |

### 4.4 Motion.Quad (0x691AD0)

| 类型 | 名称 | 实现函数 | 说明 |
|------|------|---------|------|
| Function | (constructor) | 内联 | 构造函数 |
| Property(RO) | type | sub_691248 (getter) | 形状类型 = ShapeTypeQuad |
| Function | contains | sub_690DF0 | 四边形包含判定 |
| Property(RO) | p | sub_691CF4 (getter) | 顶点数组 |

### 4.5 Motion.LayerGetter (0x69B350)

| 类型 | 名称 | 实现函数 | 说明 |
|------|------|---------|------|
| Function | (constructor) | 内联 | 构造函数 |
| Property(RO) | type | (getter) | 层类型 |
| Property(RO) | label | (getter) | 标签 |
| Property(RO) | visible | (getter) | 可见性 |
| Property(RO) | branchVisible | (getter) | 分支可见性 |
| Property(RO) | layerVisible | (getter) | 层可见性 |
| Property(RO) | x | (getter) | X 坐标 |
| Property(RO) | y | (getter) | Y 坐标 |
| Property(RO) | left | (getter) | 左边位置 |
| Property(RO) | top | (getter) | 上边位置 |
| Property(RO) | flipX | (getter) | X 翻转 |
| Property(RO) | flipY | (getter) | Y 翻转 |
| Property(RO) | zoomX | (getter) | X 缩放 |
| Property(RO) | zoomY | (getter) | Y 缩放 |
| Property(RO) | angleDeg | (getter) | 角度（度） |
| Property(RO) | angleRad | (getter) | 角度（弧度） |
| Property(RO) | slantX | (getter) | X 倾斜 |
| Property(RO) | slantY | (getter) | Y 倾斜 |
| Property(RO) | originX | (getter) | 原点 X |
| Property(RO) | originY | (getter) | 原点 Y |
| Property(RO) | opacity | (getter) | 不透明度 |
| Property(RO) | mtx | (getter) | 变换矩阵 |
| Property(RO) | vtx | (getter) | 顶点数据 |
| Property(RO) | color | (getter) | 颜色 |
| Property(RO) | bezierPatch | (getter) | 贝塞尔补丁 |
| Property(RO) | shape | (getter) | 形状 |
| Property(RO) | motion | (getter) | 动画 |
| Property(RO) | particle | (getter) | 粒子 |

### 4.6 Motion.Player (0x6D69C8) — 最大的子类

#### 属性 (Properties)

| 名称 | Getter | Setter | 说明 |
|------|--------|--------|------|
| defaultSyncActive | sub_6D93F8 | (RO) | 默认同步激活状态 |
| defaultTransformOrder | sub_6B097C | (RO) | 默认变换顺序 |
| resourceManager | sub_6D9414 | (RO) | 资源管理器引用 |
| lastTime | sub_6D9420 | (RO) | 最后时间 |
| loopTime | sub_6D9448 | (RO) | 循环时间 |
| variableKeys | sub_6D139C | (RO) | 变���键列表 |
| chara | sub_6C0E9C (set) / sub_6D9470 (get) | RW | 角色 |
| stealthChara | sub_6D94B0 (set) / sub_6D9490 (get) | RW | 静默角色 |
| motion | Player_setMotion (set) / Player_getMotion_ncb (get) | RW | 动作 |
| stealthMotion | sub_6D9584 (set) / Player_getStealthMotion (get) | RW | 静默动作 |
| tags | sub_6D9618 | (RO) | 标签 |
| motionKey | sub_6B4978 (set) / sub_695BE0 (get) | RW | 动作键 |
| project | sub_6B4978 (set) / sub_695BE0 (get) | RW | 项目 |
| completionType | sub_6D962C (set) / sub_6D9624 (get) | RW | 完成类型 |
| preview | sub_6D963C (set) / sub_6D9634 (get) | RW | 预览 |
| priorDraw | sub_6D9650 (set) / sub_6D9648 (get) | RW | 优先绘制 |
| outsideFactor | sub_6D9664 (set) / sub_6D965C (get) | RW | 外部因子 |
| meshDivisionRatio | — | — | 网格细分比率 |
| speed | — | — | 速度 |
| syncActive | — | — | 同步激活 |
| tickCount | — | — | Tick 计数 |
| frameTickCount | — | — | 帧 Tick 计数 |
| cameraActive | — | — | 摄像机激活 |
| stereovisionActive | — | — | 立体视觉激活 |
| outline | sub_6D973C (set) / sub_6D9730 (get) | RW | 轮廓 |
| meshline | sub_6D9750 (set) / sub_6D9744 (get) | RW | 网格线 |
| maskMode | sub_6D9760 (set) / sub_6D9758 (get) | RW | 遮罩模式 |
| colorWeight | sub_6CD724 (set) / sub_6CD710 (get) | RW | 颜色权重 |
| independentLayerInherit | sub_6CC9D4 (set) / sub_6D9768 (get) | RW | 独立层继承 |
| transformOrder | sub_6CC2C4 (set) / sub_6CC188 (get) | RW | 变换顺序 |
| coordinate | sub_6B4980 (set) / sub_6D9770 (get) | RW | 坐标系 |
| zFactor | sub_6B498C (set) / sub_6D977C (get) | RW | Z 因子 |
| cameraTarget | sub_6CC714 | (RO) | 摄像机目标 |
| cameraPosition | sub_6CC874 | (RO) | 摄像机位置 |
| cameraFOV | sub_6D9784 | (RO) | 摄像机 FOV |
| cameraAlive | sub_6D978C | (RO) | 摄像机是否活跃 |
| bounds | sub_6CCA84 | (RO) | 边界 |
| playing | Player_getPlaying | (RO) | 是否播放中 |
| allplaying | Player_getAllplaying | (RO) | 全部播放中 |
| syncWaiting | sub_6D979C | (RO) | 同步等待中 |
| frameLastTime | sub_6D97A4 | (RO) | 帧最后时间 |
| frameLoopTime | sub_6D97AC | (RO) | 帧循环时间 |
| hasCamera | sub_6CCF98 | (RO) | 是否有摄像机 |
| angleDeg | sub_6C0F84 (set) / sub_6C1780 (get) | RW | 角度（度） |
| angleRad | sub_6CD0EC (set) / sub_6CD0C0 (get) | RW | 角度（弧度） |
| x | sub_6CD028 (set) / sub_6D98A8 (get) | RW | X 坐标 |
| y | sub_6CD048 (set) / sub_6D98B4 (get) | RW | Y 坐标 |
| left | sub_6CD028 (set) / sub_6D98A8 (get) | RW | 左 (同 x) |
| top | sub_6CD048 (set) / sub_6D98B4 (get) | RW | 上 (同 y) |
| flipX | sub_6CD068 (set) / sub_6D98C0 (get) | RW | X 翻转 |
| flipY | sub_6CD08C (set) / sub_6D98CC (get) | RW | Y 翻转 |
| opacity | sub_6D98D8 (get) | RW | 不透明度 |
| visible | sub_6D98E4 (get) | RW | 可见性 |
| slantX | sub_6D135C (set) / sub_6D98F0 (get) | RW | X 倾斜 |
| slantY | sub_6D137C (set) / sub_6D98FC (get) | RW | Y 倾斜 |
| zoomX | sub_6D131C (set) / sub_6D9908 (get) | RW | X 缩放 |
| zoomY | sub_6D133C (set) / sub_6D9914 (get) | RW | Y 缩放 |
| useD3D | sub_6D9920 (set) / sub_695DE0 (get) | RW | 使用 D3D |
| pixelateDivision | sub_6D9934 (set) / sub_6D992C (get) | RW | 像素化分割 |
| processedMeshVerticesNum | sub_6D1018 | (RO) | 已处理网格顶点数 |

#### 方法 (Functions)

| 名称 | 实现函数 | 说明 |
|------|---------|------|
| setCoord | sub_6CCFF8 | 设置坐标 |
| setFlip | sub_6C0F1C | 设置翻转 |
| setOpacity | sub_6C1028 | 设置不透明度 |
| setVisible | sub_6C1048 | 设置可见性 |
| setSlant | sub_6C0FF8 | 设置倾斜 |
| setZoom | sub_6C0F54 | 设置缩放 |
| setVariable | &loc_6D0E70 | 设置变量 |
| getVariable | sub_6CD39C | 获取变量 |
| modifyRoot | sub_6CD0B0 | 修改根节点 |
| getLayerNames | &loc_6D10E0 | 获取层名称列表 |
| progress | (via helper) | 推进动画 |
| stop | Player_stop | 停止播放 |
| setCameraOffset | sub_6D9A38 | 设置摄像机偏移 |
| getCameraOffset | &loc_6D0AB0 | 获取摄像机偏移 |
| releaseSyncWait | sub_6D9A48 | 释放同步等待 |
| setDrawAffineTranslateMatrix | &loc_6D4F14 | 设置仿射变换矩阵 |
| contains | &loc_6D333C | 包含判定 |
| calcViewParam | sub_6D1528 | 计算视图参数 |
| getCommandList | &loc_6D3A4C | 获取命令列表 |
| getLayerMotion | sub_6D3998 | 获取层动画 |
| getLayerGetter | sub_6D38F4 | 获取层获取器 |
| getLayerGetterList | sub_6D4F88 | 获取层获取器列表 |
| skipToSync | &loc_6D3504 | 跳到同步点 |
| onAction | nullsub_87 | 动作回调 (空实现) |
| onSync | nullsub_88 | 同步回调 (空实现) |
| onGroundCorrection | sub_6D9A58 | 地面校正回调 |
| onFindMotion | sub_6D9A60 | 查找动画回调 |
| isExistMotion | sub_6D07F4 | 检查动画是否存在 |
| setStereovisionCameraPosition | sub_6D0040 | 设置立体视觉摄像机位置 |

### 4.7 Motion.SourceCache (0x6A85A8)

| 类型 | 名称 | 实现函数 | 说明 |
|------|------|---------|------|
| Function | (constructor) | 内联 | 构造函数 |
| Function | loadSource | &loc_6A7BA8 | 加载源数据 |
| Function | clearCache | &loc_6A8438 | 清除缓存 |
| Property(RO) | bufLayer | sub_6A84FC (getter) | 缓冲层 |

### 4.8 Motion.ObjSource (0x69CCB8)

| 类型 | 名称 | 实现函数 | 说明 |
|------|------|---------|------|
| Function | (constructor) | 内联 | 构造函数 |
| Property(RO) | originX | sub_69D014 (getter) | 原点 X |
| Property(RO) | originY | sub_69D0D8 (getter) | 原点 Y |
| Property(RO) | width | sub_69D19C (getter) | 宽度 |
| Property(RO) | height | sub_69D27C (getter) | 高度 |
| Property(RO) | clip | sub_69D35C (getter) | 裁剪区域 |
| Function | drawLayer | sub_69D6D8 | 绘制到层 |

### 4.9 Motion.ResourceManager (0x6AB8BC)

| 类型 | 名称 | 实现函数 | 说明 |
|------|------|---------|------|
| Function | (constructor) | 内联 | 构造函数 |
| Function | loadSource | &loc_6A7BA8 | 加载源 |
| Function | clearCache | &loc_6A8438 | 清除缓存 |
| Property(RO) | bufLayer | sub_6A84FC (getter) | 缓冲层 |
| Function | load | ResourceManager_loadResource | 加载资源 |
| Function | unload | sub_6A959C | 卸载资源 |
| Function | unloadAll | &loc_6A8CF8 | 卸载全部 |
| Function | isExistMotion | sub_6A96F8 | 检查动画是否存在 |
| Function | findMotion | sub_6A9ED4 | 查找动画 |
| Function | findSource | sub_6AAB3C | 查找源 |
| Function | random | sub_6AB56C | 随机 |
| Function | requireLayerId | sub_6AB694 | 请求层 ID |
| Function | releaseLayerId | sub_6AB750 | 释放层 ID |

注: ResourceManager 继承了 SourceCache ��成员 (loadSource, clearCache, bufLayer)，通过调用相同的实现函数。

### 4.10 Motion.SeparateLayerAdaptor (0x6ABFAC)

| 类型 | 名称 | 实现函数 | 说明 |
|------|------|---------|------|
| Function | (constructor) | 内联 | 构造函数 |
| Property | absolute | sub_6AC260 (get) / sub_6AC258 (set) | 绝对坐标模式 |
| Property | targetLayer | sub_6AC274 (get) / sub_6AC268 (set) | 目标层 |
| Function | c | Player_resetRenderState_guess | 重置渲染状态 (注: IDA 显示名为 "c") |
| Function | assign | &loc_6AC410 | 赋值 |

### 4.11 Motion.D3DAdaptor (0x6ACE94)

| 类型 | 名称 | 实现函数 | 说明 |
|------|------|---------|------|
| Function | (constructor) | D3DAdaptor_constructor | 构造函数 |
| Function | setPos | nullsub_81 | 设置位置 (空实现) |
| Function | setSize | sub_6AD7A8 | 设置大小 |
| Function | setClearColor | sub_6AD7B0 | 设置清除颜色 |
| Function | setResizable | sub_6AD7B8 | 设置可调整大小 |
| Function | removeAllTextures | sub_6AD8B8 | 移除所有纹理 |
| Function | removeAllBg | nullsub_82 | 移除所有背景 (空实现) |
| Function | removeAllCaption | nullsub_83 | 移除所有字幕 (空实现) |
| Function | registerBg | nullsub_84 | 注册背景 (空实现) |
| Function | registerCaption | nullsub_85 | 注册字幕 (空实现) |
| Function | unloadUnusedTextures | nullsub_86 | 卸载未使用纹理 (空实现) |
| Property | visible | sub_6AD90C (get) / sub_6AD904 (set) | 可见性 |
| Property | alphaOpAdd | sub_6AD920 (get) / sub_6AD918 (set) | Alpha 加法运算 |
| Function | captureCanvas | D3DAdaptor_captureCanvas | 捕获画布 |
| Property | canvasCaptureEnabled | sub_6ADAF0 (get) / sub_6ADAE8 (set) | 画布捕获启用 |
| Property | clearEnabled | sub_6ADB04 (get) / sub_6ADAFC (set) | 清除启用 |

---

## 5. EmotePlayer 类���册

### emoteplayer_entry (0x682528, 0x280 bytes)

EmotePlayer 通过独立的入口函数注册，将自己加入到已存在的 Motion 命名空间中。

#### 注册流程

```c
void emoteplayer_entry() {
    // 1. 创建 "motionplayer.dll" 字符串并注册
    ttstr dllName = L"motionplayer.dll";
    sub_548A44(&dllName);  // 标记 DLL 为已加载

    // 2. 获取 TJS2 全局对象
    global = sub_8E3C20();

    // 3. 查找 "Motion" 命名空间
    global->getMember(0, L"Motion", 0, &motionNS, global);

    // 4. 注册 EmotePlayer NCB 类
    EmotePlayer_ncb_register(L"EmotePlayer", 1);

    // 5. 将 EmotePlayer 挂载到 Motion 命名空间
    ncb_registerMember(motionNS, L"EmotePlayer", qword_1AB8078, L"EmotePlayer", 0, 0x10000);

    // 6. 查找 ResourceManager 子类
    motionNS->getMember(0, L"ResourceManager", 0, &rmNS, motionNS);

    // 7. 注册 setEmotePSBDecryptSeed 到 ResourceManager
    wrapper1 = ncb_createFuncWrapper(EmotePlayer_setEmotePSBDecryptSeed_callback);
    rmNS->registerMember(66048, L"setEmotePSBDecryptSeed", 0, wrapper1, rmNS);

    // 8. 注册 setEmotePSBDecryptFunc 到 ResourceManager
    wrapper2 = ncb_createFuncWrapper(&loc_685E60);
    rmNS->registerMember(66048, L"setEmotePSBDecryptFunc", 0, wrapper2, rmNS);
}
```

### EmotePlayer_ncb_registerMembers (0x67FAC8)

EmotePlayer 的成员注册函数，注册大量方法和属性。

#### EmotePlayer 独有常量

| 常量名 | 值 | 说明 |
|--------|-----|------|
| `TimelinePlayFlagParallel` | (在注册中) | 并行播放标志 |
| `TimelinePlayFlagDifference` | (在注册中) | 差异播放标志 |

#### EmotePlayer 方法

| 名称 | 实现函数 | 说明 |
|------|---------|------|
| progress | sub_6818B4 | 推进动画帧 |
| frameProgress | &loc_67D018 | 帧级推进 |
| initPhysics | (via ncb) | 初始化物理 |
| startWind | (via ncb) | 启动风效 |
| stopWind | sub_681A38 | 停止风效 |
| getVariable | (via ncb) | 获取变量 |
| contains | (via ncb) | 碰撞检测 |
| serialize | sub_675E40 | 序列化状态 |
| unserialize | (via ncb) | 反序列化状态 |
| setVariable | (via ncb) | 设置变量 |
| setCoord | (via ncb) | 设置坐标 |
| setScale | (via ncb) | 设置缩放 |
| setRotate | (via ncb) | 设置旋转 |
| setColor | (via ncb) | 设置颜色 |
| setOuterForce | (via ncb) | 设置外力 |
| setDrawAffineTranslateMatrix | (via ncb) | 设置仿射变换 |
| getCameraOffset | sub_681EF0 | 获取摄像机偏移 |
| setCameraOffset | (via ncb) | 设置摄像机偏移 |
| modifyRoot | sub_681F0C | 修改根节点 |
| setHairScale | sub_681F20 | 设置头发缩放 |
| setPartsScale | sub_681F28 | 设置部件缩放 |
| setBustScale | sub_681F30 | 设置胸部缩放 |
| debugPrint | (via ncb) | 调试打印 |
| setMirror | (via ncb) | 设���镜像 |
| skip | sub_66EB8C | 跳过动画 |
| playTimeline | (via ncb) | 播放时间线 |
| stopTimeline | (via ncb) | 停止时间线 |
| getTimelinePlaying | (via ncb) | 获取时间线播放状态 |
| setTimelineBlendRatio | (via ncb) | 设置时间线混合比率 |
| fadeInTimeline | (via ncb) | 淡入时间线 |
| fadeOutTimeline | (via ncb) | 淡出时间线 |
| getTimelineBlendRatio | (via ncb) | 获取时间线混合比率 |
| getVariableRange | (via ncb) | 获取变量范围 |
| getVariableFrameList | (via ncb) | 获取变量帧列表 |
| getMainTimelineLabelList | sub_674F54 | 获取主时间线标签列表 |
| getDiffTimelineLabelList | sub_6750C0 | 获取差分时间线标签列表 |
| getLoopTimeline | (via ncb) | 获取循环时间线 |
| getTimelineTotalFrameCount | (via ncb) | 获取时间线总帧数 |
| getPlayingTimelineInfoList | sub_6754C4 | 获取播放中时间线信息 |
| isSelectorTarget | (via ncb) | 是否选择器目标 |
| activateSelectorTarget | (via ncb) | 激活选择器目标 |
| getCommandList | sub_682520 | 获取命令列表 |

#### EmotePlayer 属性

| 名称 | 说明 |
|------|------|
| completionType | 完成类型 |
| chara | 角色 |
| motion | 动作 |
| motionKey | 动作键 |
| project | 项目 |
| maskMode | 遮罩模式 |
| meshDivisionRatio | 网格细分 |
| outline | 轮廓 |
| priorDraw | 优先绘制 |
| frameLastTime | 帧最后时间 |
| frameLoopTime | 帧循环时间 |
| lastTime | 最后时间 |
| loopTime | 循环时间 |
| bounds | 边界 |
| processedMeshVerticesNum | 已处理网格顶点数 |
| hairScale | 头发缩放 |
| bustScale | 胸部缩放 |
| partsScale | 部件缩放 |
| queuing | 队列中 |
| directEdit | 直接编辑 |
| selectorEnabled | 选择器启用 |
| variableKeys | 变量键列表 |
| animating | 正在动画 |

---

## 6. 类层次关系图

```
TJS2 Global
  └─ Motion (命名空间, via motionplayer.dll)
       │
       ├─ 常量: LayerTypeObj(0), LayerTypeShape(1), LayerTypeLayout(2),
       │        LayerTypeMotion(3), LayerTypeParticle(4), LayerTypeCamera(5)
       │        ShapeType*, PlayFlag*, TransformOrder*,
       │        Coordinate*, MaskMode*
       │
       ├─ Point          — 点形状 (x, y, contains)
       ├─ Circle         — 圆形状 (x, y, r, contains)
       ├─ Rect           — 矩形状 (l, t, w, h, contains)
       ├─ Quad           — 四边形  (p, contains)
       ├─ LayerGetter    — 层属性读取器 (28 个只读属性)
       │
       ├─ Player         — 动画播放器 (最大，~80 个成员)
       │                   47 属性 + 30 方法
       │
       ├─ SourceCache    — 源缓存 (loadSource, clearCache, bufLayer)
       ├─ ObjSource      — 对象源 (originX/Y, width/height, clip, drawLayer)
       │
       ├─ ResourceManager — 资源管理器 (继承 SourceCache + load/unload/find)
       │   └─ setEmotePSBDecryptSeed  (由 emoteplayer.dll 注入)
       │   └─ setEmotePSBDecryptFunc  (由 emoteplayer.dll 注入)
       │
       ├─ SeparateLayerAdaptor — 分离层适配器 (absolute, targetLayer, assign)
       ├─ D3DAdaptor     — D3D 适配器 (纹理/背景/字幕管理)
       │
       ├─ EmotePlayer    — E-mote 播放器 (由 emoteplayer.dll 注入)
       │                   ~60 个方法/属性
       │
       ├�� doAlphaMaskOperation()  — 命名空间级函数
       └─ getD3DAvailable()       — 命名空间级函数

Layer (覆盖)
  └─ BezierPatch (via LayerMeshSupport, motionplayer.dll)
```

---

## 7. NCB 注册模式分析

### 7.1 标准 NCB 子类注册模式

每个子类的注册遵循相同的 C++ 模板模式:

```c
// 注册函数 (例: Motion_Point_ncb_register)
bool XXX_ncb_register(wchar_t* className, bool enabled) {
    if (global_initialized && !enabled) return false;

    // ��置类信息
    classInfo[0] = className;
    classInfo[1] = 0;
    classInfo[2] = 0;  // error flag

    if (enabled)
        XXX_classInit(&classInfo);  // 初始化类 (设置全局状态)

    XXX_registerMembers(&classInfoPtr);  // 注册方法和属性

    if (enabled && !error) {
        // 注册构造函数
        wrapper = ncb_createFuncWrapper(XXX_constructor);
        ncb_registerMember(parent, className, wrapper, classInfo, 1, 0);
    } else {
        // 清除全局状态
        global_classObj = 0;
        global_classFlags = 0;
        global_classPtr = 0;
        global_initialized = 0;
    }

    return global_classPtr != 0;
}
```

### 7.2 成员注册模式

方法和属性通过分配 NCB descriptor 对象并调用注册函数添加:

```c
// Function 注册: 分配 0x40 字节
descriptor = new(0x40);
descriptor[+16] = 1;           // 参数数量
descriptor[+24] = L"Function"; // 类型标签
descriptor[+32] = vtable;      // NCB vtable
descriptor[+40] = descriptor;  // self 指针
descriptor[+48] = impl_func;   // 实现函数指针
descriptor[+56] = 0;           // context (可选)
addMember(parent, L"methodName", descriptor + 32);

// Property 注册: 分配 0x50 字节
descriptor = new(0x50);
descriptor[+16] = 2;           // 参数数量 (getter + setter)
descriptor[+24] = L"Property"; // 类型标签
descriptor[+32] = vtable;
descriptor[+40] = descriptor;  // self 指针
descriptor[+48] = getter_func; // getter 实现
descriptor[+56] = 0;           // padding
descriptor[+64] = setter_func; // setter 实现 (0 = 只读)
descriptor[+72] = 0;
addMember(parent, L"propName", descriptor + 32);
```

---

## 8. IDA 已重命名符号

| 地址 | 原始名 | 新名称 |
|------|--------|--------|
| 0x42EE18 | sub_42EE18 | motionplayer_static_init |
| 0x682528 | emoteplayer_entry | emoteplayer_entry |
| 0x6AB8BC | sub_6AB8BC | ResourceManager_ncb_registerMembers |
| 0x6D9B08 | motionplayer_ncb_register | Motion_namespace_ncb_register |
| 0x690FBC | sub_690FBC | Point_ncb_registerMembers |
| 0x691300 | sub_691300 | Circle_ncb_registerMembers |
| 0x6916A4 | sub_6916A4 | Rect_ncb_registerMembers |
| 0x691AD0 | sub_691AD0 | Quad_ncb_registerMembers |
| 0x69B350 | sub_69B350 | LayerGetter_ncb_registerMembers |
| 0x69CCB8 | sub_69CCB8 | ObjSource_ncb_registerMembers |
| 0x6A85A8 | sub_6A85A8 | SourceCache_ncb_registerMembers |
| 0x6ACE94 | D3DAdaptor_registerNCB | D3DAdaptor_ncb_registerMembers |
| 0x6ABFAC | SeparateLayerAdaptor_registerProps | SeparateLayerAdaptor_ncb_registerMembers |
| 0x6D69C8 | Player_ncb_registerMembers | Player_ncb_registerMembers |
| 0x67FAC8 | Player_registerNCB | EmotePlayer_ncb_registerMembers |
| 0x685BC0 | sub_685BC0 | EmotePlayer_ncb_register |
| 0x685D30 | sub_685D30 | EmotePlayer_setEmotePSBDecryptSeed_callback |
| 0x6FC06C | sub_6FC06C | Motion_namespace_ncb_factory |
| 0x6FC1D0 | sub_6FC1D0 | Motion_namespace_ncb_factory2_guess |
| 0x6AF104 | sub_6AF104 | Motion_doAlphaMaskOperation |
| 0x6B0960 | sub_6B0960 | Motion_getD3DAvailable |
