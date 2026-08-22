# MotionPlayer type-10 feedback-anchor 四端对照（2026-08-12）

## 结论

本专题以 `reference/binaries/` 中 Android arm64、Android armv7、iOS
arm64、iOS armv7 四个当前参考二进制为共同真值，重新核验 type-10
feedback-anchor 的更新阶段、内部 Layer 引用流、尺寸读取、阻尼计算、矩阵
重建、颜色残差、节点构造默认值和异常/浮点边界。旧 `libkrkr2.so` 地址注释不
再作为证据。

四端控制流相同，编译器布局和 STL deque ABI 不同。最容易被常规 C++ 直觉
错误“修正”的行为是：

- RGB 基准存在一个跨节点、未初始化的栈变量；四份颜色相同且 blend 高半字节
  为 `0x10` 时读取该变量。第一次命中可以读取未初始化值，之后会沿用前一个
  已提交节点的基准。
- RGB 不是普通的逐通道同索引更新。输入字节顺序是 `2,1,0,3`，残差顺序是
  `0,1,2,3`，输出字节顺序是 `0<-2, 1<-0, 2<-1, 3<-3`；残差 1 还复用
  通道 0 的整数分母。
- opacity 与 RGB 使用方向相反的 clamp。输入为 NaN 时 opacity 落到 `0`，RGB/
  alpha 落到 `255`。
- 残差除法不检查零分母。结果为零时会生成 NaN，不能保留旧残差。
- 节点构造将四个 packed-color word 清零；灰色 `0xFF808080` 是 clip slot 的
  常见默认值，不是 `MotionNode` 工作颜色的构造默认值。
- anchor 阶段没有独立的 `anchorEnabled` 字段。gate 只写
  `SourceState::valid`。

## 函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `Player_updateAnchorFeedback_guess` | `0x6BD908` (`0x7E0`) | `0x589C00` (`0x734`) | `0x100113024` (`0x6EC`) | `0x110908` (`0x780`) |
| `MotionNode_rebuildLocalMatrix_guess` | `0x696D20` | `0x572F80` | `0x1000F6A7C` | `0xF36BC` |
| `ncbPropAccessor` 整数读取 | `0x6609BC`（第二次读取/转换；存在性探测在 caller 内联） | `0x496B84` | `0x1000F9468` | `0xF651C` |
| `MotionNode_ctor_guess` | `0x6EED94` | `0x5ACC70` | `0x10014151C` | `0x1425BC` |
| ctor 调用的 common-field 初始化 | `0x696770` | `0x572A2C` | `0x1000F6580` | `0xF316C` |

四个 phase 都是 `Player_updateLayers_guess` 的最后一个 type-specific 更新调用，
紧跟 type-4 particle-system phase。更新函数自身随后清理 per-frame 标志。

## 关键节点 ABI 对照

本表仅供反编译复核；portable C++ 不应把这些偏移写进源代码注释。

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| deque 节点步长 | `2632` | `2272` | `2648` | `2228` |
| `nodeType` | `+28` | `+20` | `+28` | `+20` |
| accumulated active | `+1505` | `+1265` | `+1521` | `+1233` |
| 四个 packed color | `+100` | `+84` | `+100` | `+84` |
| source valid | `+200` | `+184` | `+200` | `+184` |
| source Variant | `+204` | `+188` | `+204` | `+188` |
| source width/height | `+232/+240` | `+208/+216` | `+232/+240` | `+204/+212` |
| active slot index | `+1392` | `+1160` | `+1392` | `+1128` |
| feedback timespan | `+2432` | `+2096` | `+2448` | `+2056` |
| opacity residual | `+2440` | `+2104` | `+2456` | `+2064` |
| 16 个 color residual | `+2448..+2568` | `+2112..+2232` | `+2464..+2584` | `+2072..+2192` |

Android arm64 使用 libstdc++ deque 的一节点 block；Android armv7 亦由
单节点 block 路径定位。iOS 两端使用 libc++ deque，每个 block 放 16 个节点。
四端的逻辑索引都从 1 开始，所以只要进入 body，根节点 0 必然存在。

## 共同主流程

四端共同伪代码如下。`CopyRef`、临时 dispatch 引用和未初始化局部变量都是
语义的一部分：

```text
double carriedRgbBase_guess;                    // deliberately uninitialized

for nodeIndex in [1, nodeCount):
    node = nodes[nodeIndex]
    if node.type != 10 or !node.accumulated.active:
        continue

    player.needsInternalAssignImages = true
    if player.deltaTime == 0 or !player.internalRenderLayerReady:
        node.source.valid = false                // only this byte is cleared
        continue

    internal = retain(player.internalRenderLayer.AsObject())
    node.source.object = CopyRef(player.internalRenderLayer)
    node.source.valid = true
    node.source.width  = internal.getIntValue("width",  0)
    node.source.height = internal.getIntValue("height", 0)
    node.source.origin = (width/2, height/2)
    node.source.clip = (0, 0, 1, 1)
    release(internal)

    scaledDelta = player.deltaTime / player.speedMultiplier
    dampPower = player.deltaTime
        * (scaledDelta * player.deltaTime / scaledDelta)
        / scaledDelta / 60 / node.feedbackTimespan

    if angle >= 180:
        angle = 360 - (360-angle)*dampPower
    else:
        angle *= dampPower
    scaleX = pow(scaleX * 32 / width,  dampPower)
    scaleY = pow(scaleY * 32 / height, dampPower)
    slantX *= dampPower
    slantY *= dampPower

    rebuildLocalMatrix(node)
    if !player.independentLayerInherit:
        node.matrix = root.matrix * node.matrix   // root node 0, no guard

    dampOpacity(node)
    node.position = root.position
        + dampPower * (node.position-root.position) // root node 0, no guard

    allEqual = packedColor[0] == packedColor[1]
            && packedColor[1] == packedColor[2]
            && packedColor[2] == packedColor[3]
    defaultBlend = (activeSlot.blendMode & 0xF0) == 0x10

    if allEqual:
        if defaultBlend:
            rgbBase = carriedRgbBase_guess       // may be indeterminate
        else:
            rgbBase = 255
            if packedColor[0] == 0xFFFFFFFF:
                continue                         // carry is not committed
        if packedColor[0] == 0xFF808080:
            carriedRgbBase_guess = rgbBase
            continue
        colorCount = 1
    else:
        rgbBase = defaultBlend ? 128 : 255
        colorCount = 4

    for colorIndex in [0, colorCount):
        dampPackedColorSet(node, colorIndex, rgbBase, dampPower)
    if colorCount == 1:
        packedColor[1..3] = packedColor[0]
    carriedRgbBase_guess = rgbBase
```

### 内部 Layer 引用与失败顺序

每个通过 gate 的节点都会独立执行以下所有权序列：

1. 复制 Player 的 internal-Layer Variant 到临时 Variant。
2. `AsObject()`，获得 accessor 持有的 dispatch 引用；void/non-object 会在这里
   抛出，而 producer flag 已经置位。
3. 清临时 Variant。
4. 将 Player 的 internal-Layer Variant 复制到 `node.source.object`。
5. 写 `source.valid=true`。
6. 读取尺寸并写 source 几何。
7. accessor 析构，释放它自己的 dispatch 引用；节点 source 的 Variant 仍独立
   持有对象。

gate 失败不会清 `source.object`、width/height/origin/clip 或阻尼残差。对象转换、
第二次属性读取和整数转换的异常同样不会被 anchor phase 捕获。

### 尺寸属性的双读取

Android armv7 和两个 iOS 目标保留了完整的
`ncbPropAccessor::getIntValue(member, 0)` helper：

```text
if !PropGet(TJS_MEMBERMUSTEXIST, member, hint=null):
    return 0
value = PropGet(0, member, hint=null)     // HRESULT ignored by converter path
return value.AsInteger()
```

Android arm64 把存在性探测内联在 caller，只留下第二次普通读取/整数转换 helper。
width 与 height 独立执行；失败得到 0。没有正尺寸 clamp，后续 `32/width` 与
`32/height` 保留除零边界。

## opacity 数据流

四端都把 accumulated opacity 当作无符号 32 位值转成 double：

```text
u = uint32(opacity)
normalized = (u == 0) ? 1/255 : double(u)/255
raw = pow(normalized, dampPower) * 255 * opacityResidual

result = 0
if raw >= 0:
    result = raw
    if raw > 255:
        result = 255

opacity = int(result)
denominator = result
if int(result) < 0:                       // compiled dead correction
    denominator += 2^32
opacityResidual = result / denominator    // zero is 0/0 -> NaN
```

因此：

- 负 `int` opacity 先按 `uint32_t` 扩展，通常饱和为 255；不能按有符号负值计算。
- raw 为 NaN 时两个有序比较都失败，`result` 保持 0。
- positive fractional result 的 residual 是 `result/result == 1`，不是相对截断整数
  的误差累计。
- result 为 0 时 residual 必须变为 NaN；不得保留旧值。

## packed-color 数据流

四端的常量表字节均已直接读取并确认：索引 0 是 double `255.0`，索引 1 是
double `128.0`。仅“四份颜色不全相同”分支按 default-blend 布尔索引该表。

单个四字节组的共同伪代码：

```text
rgb(input, residual):
    v = (input == 0) ? 1 : input
    raw = rgbBase * pow(v/rgbBase, dampPower) * residual
    result = 255
    if raw <= 255:
        result = raw
        if raw < 0:
            result = 0
    return result

c0 = rgb(byte[2], residual[0])
c1 = rgb(byte[1], residual[1])
c2 = rgb(byte[0], residual[2])

a = (byte[3] == 0) ? 1/255 : byte[3]/255
c3 = colorClamp(pow(a, dampPower) * 255 * residual[3])

i0 = int(c0); i1 = int(c1); i2 = int(c2); i3 = int(c3)
residual[0] = c0 / double(i0)
residual[1] = c1 / unsignedDouble(i0)   // deliberately shares i0
residual[2] = c2 / unsignedDouble(i2)
residual[3] = c3 / unsignedDouble(i3)

byte[0] = i2
byte[1] = i0
byte[2] = i1
byte[3] = i3
```

`colorClamp` 先以 255 为 fallback，再只在 `raw <= 255` 时接收 raw，所以 NaN
得到 255。`unsignedDouble` 中对负整数加 `2^32` 的路径在当前 clamp 后不可达，
但四个编译物都保留该转换形态，本地源码也保留了对应 token。

## 矩阵 helper

四个 `MotionNode_rebuildLocalMatrix_guess` 都按 `transformOrder[0..3]` 依次
处理 flip、angle、zoom、slant，先重建 node 的 local 2x2。angle 的弧度表达式
共同为：

```text
(angle * 3.14159265 + angle * 3.14159265) / 360.0
```

不能替换为高精度 pi，也不能折叠成 `angle * 2*pi/360` 后期待逐位等价。

## 构造与跨帧生命周期

四端 `MotionNode` 构造共同证明：

- 四个 packed-color word 全部清零。
- opacity residual 初始化为 `1.0`。
- 16 个 color residual 全部初始化为 `1.0`。
- `feedbackTimespan` 在普通构造中不写，继续保持有意未初始化；timeline
  evaluation 的 type-10 分支在消费前写入。

四端的真实 ctor 都建立成员 owner 后进入表中的 common-field helper；早期把
Android arm64 的 `0x696770` 直接命名成 ctor 是 V232 已纠正的旧标注。节点赋值路径
按值复制工作颜色和全部残差，因此它们
属于节点的持久跨帧状态。相比之下，`carriedRgbBase_guess` 只属于一次 phase 调用的
栈帧，跨节点但不跨调用。

## 本地差异与修复

| 旧本地行为 | 四端证据 | 修复 |
|---|---|---|
| 额外维护 `anchorEnabled` | phase 只写 source valid 字节 | 删除字段及复制/写入 |
| gate 同时清 `anchorEnabled` | gate 只清 source valid，其他 source 数据保留 | 仅清 `source.valid` |
| root matrix/position 前检查 deque 非空 | body 从 index 1 开始，直接读 root 0 | 删除两处 guard |
| opacity 按 signed int 转 double | 四端使用 unsigned conversion | 显式转 `uint32_t` |
| opacity/RGB 用 `std::clamp` | 两个原生 clamp 的 NaN fallback 相反 | 展开有序比较 |
| residual 分母为零时保留旧值 | 四端无条件除法 | 删除 guard，保留 NaN/Inf |
| RGB 按 `0,1,2,3` 同索引更新 | 四端共同的非对称读写和共享分母 | 逐项复原 |
| 每节点独立选择 128/255 | equal/default 分支读取未初始化的 carry | 恢复未初始化跨节点局部变量与 commit 时点 |
| 只跳过 equal gray | non-default equal white 也直接跳过 | 恢复 white/gray 两种分支 |
| `MotionNode::colorBytes` 构造为灰色 | 四端构造全部清零 | 改为零初始化 |
| transform helper 使用高精度 pi 和重排表达式 | 四端共同 literal/expression | 恢复 `3.14159265` 双乘表达式 |

未给 `carriedRgbBase_guess` 添加默认值，也未写依赖它首次取值的运行时测试；这正是
四端共同保留的未初始化边界。确定性 helper 测试覆盖了两种 NaN clamp、unsigned
opacity、零除残差、非对称 RGB byte/scale 数据流和截断 pi 表达式。

## IDB 改进

四个 IDB 已统一命名并保存：

- `Player_updateAnchorFeedback_guess`
- `MotionNode_rebuildLocalMatrix_guess`
- `ncbPropAccessor_getIntValue_guess`；Android arm64 的拆分 helper 命名为
  `ncbPropAccessor_getValueInt_guess`
- 三个非内联 common initializer 命名为 `MotionNode_initCommonFields_guess`
- Android/iOS 64 位 RGB table 以及 Android armv7 的两个常量项已命名
- phase 中的 RGB current/carry 和 damp-power 局部变量已按四端对应关系命名
- producer gate、颜色分支、颜色 worker、构造残差初始化和矩阵常量均添加了
  行级注释

保存后应继续以 fresh decompile 检查命名传播，不把任一单端的编译器临时变量名
误当成确定的原始源代码标识符。
