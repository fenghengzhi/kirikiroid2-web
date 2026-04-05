# MotionPlayer & EmotePlayer — 本地代码与 libkrkr2.so 未对齐报告

**分析日期**: 2026-04-05
**对比基础**: `analysis/EmotePlayer_Internal_Implementation.md`, `analysis/Player_Class_Layout_libkrkr2so.md`, `analysis/MotionPlayer_NCB_Registration.md`

---

## 严重程度说明

| 级别 | 含义 |
|------|------|
| **P0-CRITICAL** | 架构性偏差，导致功能完全无法工作或行为根本错误 |
| **P1-HIGH** | 逻辑偏差，特定场景下行为错误 |
| **P2-MEDIUM** | 实现简化，大部分场景可工作但细节不一致 |
| **P3-LOW** | 缺少功能但不影响主流程 |

---

## 1. EmotePlayer 架构性偏差

### 1.1 [P0-CRITICAL] 对象层次完全错误

**二进制架构** (3 层委托):
```
D3DEmotePlayerNativeInstance (24 bytes)
  └→ EmoteObject (40 bytes, sub_67DBAC)
      ├→ ResourceManager (232 bytes, sub_6A88CC)
      └→ Player (1496 bytes, sub_67E38C)
```

**本地代码** (扁平 shared_ptr):
```
EmotePlayer
  └→ shared_ptr<EmotePlayerRuntime>  // 简单的 map/vector 容器
```

**影响**: EmotePlayer 的所有方法在二进制中都委托给内部 Player 对象（1496 字节的巨型结构体，包含 6 种动画控制器 deque、哈希表、物理引擎）。本地代码用 `std::map<string, double>` 替代了整个动画管线。

**来源**: `analysis/EmotePlayer_Internal_Implementation.md` §2

---

### 1.2 [P0-CRITICAL] setVariable 是类型分发系统，不是简单 map 写入

**二进制** (sub_671228): 9-case switch 根据变量类型路由到不同的动画控制器：
```c
switch (controllerType) {
    case 0/1/2: 基础类型 — physicsDisabled 时跳过
    case 4: coordControllers (deque at Player+256) → sub_6638B0
    case 5: scaleControllers (deque at Player+336) → sub_6652D4
    case 6: rotControllers (deque at Player+416, stride=24) → sub_665E34
    case 7: windControllers (deque at Player+576, stride=24) → sub_667300
    case 8: forceControllers (deque at Player+656, stride=48) → sub_6681E4
}
// 未注册变量 → 写入 evalResult hashmap (Player+1440)
```

**本地代码** (`EmotePlayer.cpp:160`):
```cpp
void EmotePlayer::setVariable(ttstr label, double value) {
    _variables[detail::narrow(label)] = value;  // 简单 map 写入
    _modified = true;
}
```

**影响**: 所有 E-mote 变量驱动的动画（表情切换、口型同步、物理形变）完全不工作。变量值存入了死数据，不会驱动任何控制器。

**来源**: `analysis/EmotePlayer_Internal_Implementation.md` §3.10

---

### 1.3 [P0-CRITICAL] progress() 缺少完整物理引擎

**二进制** (sub_530A5C → sub_67D01C): 完整的物理/动画步进引擎：
1. 步进 6 种控制器类型（coord/scale/rot/force/wind/frame）
2. 每步上限 1.1 秒
3. 运行 bust/hair/parts 弹簧物理
4. 将计算结果写回渲染子系统

**本地代码** (`EmotePlayer.cpp:370`):
```cpp
void EmotePlayer::progress(double dt) {
    pass(dt);  // → _progress += dt; stepTimelines(...)
}
```

**影响**: progress 只是时间累加 + 时间线状态机步进，缺少所有物理模拟（弹簧、阻尼、风力）和控制器插值。

---

### 1.4 [P1-HIGH] setScale 缺少 baseScale * userScale 乘法

**二进制** (sub_530260):
```c
float baseScale = *(float*)(this + 40);   // 基础缩放
*(float*)(this + 44) = (float)scale;      // 保存用户值
float finalScale = baseScale * scale;     // 两者相乘
sub_667300(scaleAnimator, &finalScale, ...);
```

**本地代码** (`EmotePlayer.cpp:129`):
```cpp
void EmotePlayer::setScale(double s) { _scale = s; }
```

**影响**: 缺少基础缩放因子，当 PSB 数据设置了 baseScale ≠ 1.0 时角色大小不正确。

---

### 1.5 [P1-HIGH] contains() 只有 AABB，缺少圆形和四边形碰撞

**二进制** (sub_690DF0): 支持 3 种碰撞体：
- type=1: 圆形（距离 < 半径）
- type=2: AABB 矩形
- type=3: 凸四边形（叉积绕序测试）

**本地代码** (`EmotePlayer.cpp:384`):
```cpp
bool EmotePlayer::contains(double x, double y) {
    // 仅 AABB
    return x >= _coordX && x <= (_coordX + scaledWidth) && ...;
}
```

**影响**: 圆形和四边形碰撞区域的点击检测不准确。

---

### 1.6 [P1-HIGH] startWind 只存了两个 double，缺少风模拟器

**二进制** (sub_6709AC): 创建 0x61C (1564) 字节的风模拟器对象：
- 相位追踪
- meshDivisionRatio 振幅缩放
- 方向感知
- 存储到 Player+1128

**本地代码** (`EmotePlayer.cpp:183`):
```cpp
void EmotePlayer::startWind(double a, double b, double c) {
    _outerForceX = a + c;
    _outerForceY = b;
}
```

**影响**: 风力动画没有物理模拟效果。

---

### 1.7 [P1-HIGH] setOuterForce 缺少 bust/hair/parts 分发

**二进制** (sub_672D58): 根据字符串标签分发到不同物理控制器：
- `"bust"` → Player+1104
- `"h"` → Player+1112 (注意: 是 "h" 不是 "hair")
- `"parts"` → Player+1120

**本地代码** (`EmotePlayer.cpp:374`):
```cpp
void EmotePlayer::setOuterForce(double x, double y) {
    _outerForceX = x;
    _outerForceY = y;
}
```

**影响**: 外力只存了两个值，无法分别控制胸/发/部件的物理效果。

---

### 1.8 [P2-MEDIUM] getScale/getRot/getColor 在二进制中是硬编码桩

**二进制**:
```c
double getScale() { return 1.0; }   // 0x5302DC
double getRot()   { return 0.0; }   // 0x53030C
int    getColor() { return 0; }     // 0x530320
```

**本地代码**: 返回存储的 `_scale`/`_rot`/`_color` 成员值。

**影响**: 不影响功能（getter 返回值在游戏脚本中较少使用），但行为不一致。本地代码的行为实际上更合理。

---

### 1.9 [P2-MEDIUM] setCoord/setRot/setColor 缺少 animator 委托

**二进制**: 这些方法都委托给对应的 animator 对象：
- setCoord → coordAnimator (Player+1072) → sub_667300
- setRot → rotAnimator (Player+1096) → sub_666490 (含角度归一化 [0, 2π))
- setColor → colorAnimator (Player+1088) → RGBA 4-float 解包

**本地代码**: 直接写成员变量 (`_coordX = x; _rot = rot; _color = color`)

**影响**: 缺少过渡动画（transition time/ease type 参数被忽略），角度不归一化。

---

### 1.10 [P2-MEDIUM] create() 语义错误

**二进制** (sub_52FD84): `create()` 实际是**销毁/重置**操作——释放两个 EmoteObject (this+24 和 this+32)。

**本地代码** (`EmotePlayer.cpp:77`): 清空 snapshot 和 timelines，语义近似但实现不同。

---

### 1.11 [P2-MEDIUM] load() 缺少 EmoteObject 创建链

**二进制** (sub_52FDD4):
1. 销毁旧 EmoteObject
2. 收集所有参数到 TJS Array
3. 创建新 EmoteObject (40b) → ResourceManager (232b) → Player (1496b)
4. 加载 PSB，解析 metadata.base.chara/motion
5. 调用 Player_play 启动初始动画

**本地代码** (`EmotePlayer.cpp:84`): `lookupModuleSnapshot(data)` + `primeTimelineStates()`

---

### 1.12 [P3-LOW] playTimeline 缺少错误抛出和哈希表查找

**二进制** (sub_672F70):
- 查找不到时间线 → 抛出 `"timeline label not found 'xxx'."`
- 使用哈希表 (Player+936) 查找
- flags & 1 (parallel) 时先停止所有现有时间线

**本地代码**: 使用 `std::unordered_map` 查找，无错误抛出。

---

## 2. Player (MotionPlayer) 架构性偏差

### 2.1 [P0-CRITICAL] Player 对象布局完全不同

**二进制**: 1384 字节，包含：
- 根节点指针 (+200) → 2632 字节 Node 结构体
- 4 个哈希表 (+264, +320, +1184, +1240)
- 变量 deque (+1296, 160 字节/项)
- 渲染列表 (+384, 56 字节/项)
- 多个 ttstr 字段 (+484~+736)
- 大量 packed bool/byte 标志 (+480~+611, +908~+1100)

**本地代码**: 使用 `shared_ptr<PlayerRuntime>` + 40+ 个独立成员变量，布局完全不同。

**影响**: 所有属性的 getter/setter 在二进制中通过固定偏移访问，本地代码通过成员名访问——逻辑等价但结构不同。

---

### 2.2 [P0-CRITICAL] 根节点属性访问模式不一致

**二进制**: 大量属性通过 `*(player+200)` 访问根节点（Node 结构体），然后偏移：
```
x       = *(double*)(*(player+200) + 1592)
y       = *(double*)(*(player+200) + 1600)
angle   = *(double*)(*(player+200) + 1616)
visible = *(byte*)(*(player+200) + 1586)
flipX   = *(byte*)(*(player+200) + 1587)
opacity = *(int*)(*(player+200) + 1656)
```

**本地代码**: 使用 `_runtime->nodes[0]` 的 MotionNode 结构体成员。MotionNode 字段名和偏移不保证与二进制对齐。

**影响**: 如果 MotionNode 布局与 Node 不一致，属性设置可能写错位置。

---

### 2.3 [P1-HIGH] tickCount 缺少时间单位转换

**二进制**: 内部使用 60fps 帧数，TJS API 使用毫秒。转换公式：
```
getter: return frameTickCount * 1000.0 / 60.0;    // 帧→毫秒
setter: frameTickCount = value * 60.0 / 1000.0;   // 毫秒→帧
```

**本地代码** (`Player.h:137`):
```cpp
void setTickCount(double v) { _tickCount = v; }
double getTickCount() const { return _tickCount; }
```

**影响**: 时间单位不匹配导致 TJS 脚本读写 tickCount 时值偏差 60 倍。

---

### 2.4 [P1-HIGH] resourceManager/lastTime/loopTime getter 缺少单位转换

同上，二进制中 `resourceManager`(读+1128*1000/60)、`lastTime`(读+1136*1000/60)、`loopTime` 都需要帧→毫秒转换，本地代码直接返回原始值。

---

### 2.5 [P1-HIGH] Player 属性类型不匹配

| 属性 | 二进制类型 | 本地类型 | 问题 |
|------|-----------|---------|------|
| `outline` | ttstr (+1032, 20 bytes) | bool | 二进制是字符串，本地是布尔 |
| `meshline` | ttstr (+1052, 20 bytes) | bool | 同上 |
| `speed` | bool/byte (+1093) | double | 二进制是布尔标志，本地是浮点数 |
| `colorWeight` | bool/byte (+1097) | double | 二进制是布尔标志，本地是浮点数 |
| `project` | int (+1144) | tTJSVariant | 二进制是整数，本地是变体类型 |
| `priorDraw` | double (+1160) | int | 二进制是浮点，本地是整数 |
| `opacity` | int (root+1656) | double | 二进制是 int32，本地代码 setOpacity 接受 double |

---

### 2.6 [P1-HIGH] angleDeg/angleRad 转换逻辑缺失

**二进制**:
- `angleDeg` getter: `root_angle * 180.0 / π` (rad→deg)
- `angleDeg` setter: `value * π / 180.0` (deg→rad)
- 内部存储是弧度

**本地代码**: Player.h 中没有 `angleDeg`/`angleRad` 属性注册。NCB 注册中也未见。

---

### 2.7 [P1-HIGH] 多个 Player 属性应为只读但注册为可写

**二进制 RO 属性**（NCB 仅注册 getter）:
- `defaultSyncActive`, `defaultTransformOrder`
- `resourceManager`, `lastTime`, `loopTime`, `variableKeys`
- `tags`
- `playing`, `allplaying`, `syncWaiting`
- `frameLastTime`, `frameLoopTime`, `frameTickCount`
- `hasCamera`, `bounds`
- `cameraTarget`, `cameraPosition`, `cameraFOV`, `cameraAlive`

**本地代码**: 多数注册为 `NCB_PROPERTY(name, getter, setter)` 即可读写。

**影响**: TJS 脚本可能意外写入只读属性。

---

### 2.8 [P2-MEDIUM] Player.pixelateDivision 默认值错误

**二进制**: 构造函数设置 `dword(+912) = 100`

**本地代码**: 无此字段（Player.h 中未定义）。

---

### 2.9 [P2-MEDIUM] TransformOrder 枚举值不匹配

**二进制**:
```
TransformOrderFlip  = 0
TransformOrderAngle = 1
TransformOrderZoom  = 2
TransformOrderSlant = 3
```

**本地代码** (`Player.h:57`):
```cpp
TransformOrderFlip  = 0
TransformOrderSlant = 1  // 二进制中 = 3
TransformOrderZoom  = 2
TransformOrderAngle = 3  // 二进制中 = 1
```

**影响**: TransformOrder 枚举值与二进制不一致，setTransformOrder 行为错误。

---

## 3. NCB 注册偏差

### 3.1 [P1-HIGH] Motion 命名空间缺少 Point/Circle/Rect/Quad/LayerGetter 子类

**二进制** 注册了 10 个子类:
- ✅ Player, ResourceManager, SourceCache, ObjSource, SeparateLayerAdaptor, D3DAdaptor
- ❌ Point, Circle, Rect, Quad, LayerGetter — **本地代码缺少**

**影响**: TJS 脚本无法使用 `new Motion.Point()`、`new Motion.Rect()` 等几何体，也无法使用 `Motion.LayerGetter` 获取层属性。

---

### 3.2 [P1-HIGH] SeparateLayerAdaptor 缺少 absolute/targetLayer/assign/c 方法

**二进制** (0x6ABFAC):
```
Property: absolute (get/set)
Property: targetLayer (get/set)
Function: c (resetRenderState)
Function: assign
```

**本地代码**: 只有 `factory()` 和 `getOwner()`，无任何属性或方法注册。

---

### 3.3 [P2-MEDIUM] ResourceManager 注册不完整

**二进制** (0x6AB8BC) — 继承 SourceCache 成员:
```
loadSource, clearCache, bufLayer (RO),
load, unload, unloadAll,
isExistMotion, findMotion, findSource,
random, requireLayerId, releaseLayerId
```

**本地代码 NCB**:
```
load, unload, clearCache,
setEmotePSBDecryptSeed, setEmotePSBDecryptFunc
```

**缺少**: loadSource, bufLayer, unloadAll, isExistMotion, findMotion, findSource, random, requireLayerId, releaseLayerId

---

### 3.4 [P2-MEDIUM] D3DEmoteModule 在二进制中不存在

**本地代码** 注册了 `D3DEmoteModule` 类（含 MaskMode/TimelinePlayFlag 常量 + maskMode/maskRegionClipping 等属性）。

**二进制**: 这些常量直接注册在 Motion 命名空间上，没有独立的 D3DEmoteModule 类。EmotePlayer 的常量（TimelinePlayFlagParallel/TimelinePlayFlagDifference）在 EmotePlayer NCB 注册中定义。

---

### 3.5 [P2-MEDIUM] Player 缺少二进制中的多个属性/方法

**缺少的属性** (二进制有但本地没有):
- `defaultSyncActive` (static byte_1AB84A8)
- `defaultTransformOrder`
- `angleDeg`, `angleRad`
- `x`, `y`, `left`, `top` (→ root node 位置)
- `flipX`, `flipY`
- `slantX`, `slantY`, `zoomX`, `zoomY`
- `bounds` (AABB 计算)
- `pixelateDivision`
- `maskMode`

**缺少的方法**:
- `onAction` (nullsub_87, 空实现)
- `onSync` (nullsub_88, 空实现)
- `onGroundCorrection`
- `setSlant` (应分为 setSlantX/setSlantY)
- `setZoom` (应分为 setZoomX/setZoomY)

---

### 3.6 [P2-MEDIUM] EmotePlayer NCB 缺少二进制中的方法

**二进制** EmotePlayer (0x67FAC8) 注册了但本地 NCB 没有的:
- `frameProgress` (帧级推进)
- `serialize` / `unserialize` (状态序列化)
- `setRotate` (不同于 `setRot`)
- `setMirror`
- `setDrawAffineTranslateMatrix`
- `getCameraOffset` / `setCameraOffset`
- `modifyRoot`
- `setHairScale` / `setPartsScale` / `setBustScale` (方法，非属性)
- `debugPrint`
- `getTimelinePlaying`
- `getVariableRange` / `getVariableFrameList`
- `fadeInTimeline` / `fadeOutTimeline` / `getTimelineBlendRatio`

---

## 4. 对齐优先级建议

### 第一优先级（P0 架构性问题）
1. **EmotePlayer 对象层次重构** — 3 层委托架构 (D3DEmotePlayer → EmoteObject → Player)
2. **setVariable 类型分发系统** — 9-case switch + 动画控制器 deque
3. **progress 物理引擎** — 6 种控制器步进 + 弹簧物理

### 第二优先级（P1 逻辑问题）
4. **tickCount 时间单位转换** — 帧(60fps) ↔ 毫秒(TJS)
5. **TransformOrder 枚举值修正** — Angle=1, Slant=3 (非反)
6. **属性类型修正** — outline/meshline→ttstr, speed/colorWeight→bool
7. **setScale baseScale 乘法**
8. **contains() 多碰撞体**
9. **startWind 风模拟器**
10. **setOuterForce bust/h/parts 分发**
11. **补全 Point/Circle/Rect/Quad/LayerGetter 子类**

### 第三优先级（P2 完善）
12. 补全 ResourceManager/SeparateLayerAdaptor 方法
13. 补全 Player 缺少的属性 (angleDeg/angleRad/flipX/flipY 等)
14. setCoord/setRot/setColor animator 委托
15. RO 属性修正
16. D3DEmoteModule 移除或重构

---

## 5. 已对齐的部分（确认正确）

以下本地代码与二进制行为一致：
- ✅ EmotePlayer NCB 方法/属性名称和签名
- ✅ Player NCB 方法/属性名称（大部分）
- ✅ D3DAdaptor 方法列表和空桩实现 (setPos/removeAllBg 等)
- ✅ Motion 命名空间常量 (LayerType/ShapeType/PlayFlag/CoordinateType)
- ✅ emoteplayer.dll 依赖 motionplayer.dll 的加载顺序
- ✅ Player 构造函数中 Math.RandomGenerator 创建
- ✅ Player.parentColorPacked 默认 0xFF808080
- ✅ Player._noUpdateYet 标志
- ✅ Player.updateLayers 分阶段架构（Phase1/2/3 子函数）
- ✅ MotionEvent 结构体 (type=0: onAction, type=1: onSync)
- ✅ D3DAdaptor.captureCanvas 像素拷贝到 Layer 的实现
