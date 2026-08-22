# MotionPlayer ShapeGeometry 四端对照（2026-08-12）

## 结论

本专题以 `reference/binaries/` 中 Android arm64、Android armv7、iOS
arm64、iOS armv7 四个当前参考二进制为共同真值，重新核验
`updateLayers` 后处理链中节点内 Point/Circle/Rect/Quad 几何记录的生产阶段。
旧 `libkrkr2.so` 地址和由该地址推导的源码注释不再作为证据。

四端实现具有同一控制流：helper 只遍历非根节点；只有 `nodeType == 1`
且当前 active slot 的 `done` 为零时才写记录。通过 gate 后先无条件把
`shapeType` 写进记录首个 32 位 type，再按 0/1/2/3 分别只写 point、
circle、rect、quad 使用的 double 槽位。未知 shapeType 只改 type；跳过节点
什么都不改。因此记录是跨帧保留、按形状局部覆写的内嵌对象，不是每帧清零的
临时结果。

本轮证伪三项本地假设：旧 Android arm64 地址 `0x6BDE94` 不是当前 helper
入口；quad 原点来自当前 active slot 的 `ox/oy`，不是缓存
`clipOriginX/Y`；MotionNode 构造不会初始化整条 `HitData`。四端机器码还
共同保留了一组可观察的浮点加法分组，第四顶点与前三个顶点不同。

## 函数边界与调用链

| 目标 | `Player_updateShapeGeometry_guess` | 大小 | `Player_updateLayers_guess` |
|---|---:|---:|---:|
| Android arm64 | `0x6BB274` | `0x22C` | `0x6B871C` (`0xAE4`) |
| Android armv7 | `0x587BAC` | `0x24E` | `0x5856E0` (`0x9DC`) |
| iOS arm64 | `0x100110CE0` | `0x1FC` | `0x10010E544` (`0xB50`) |
| iOS armv7 | `0x10E46C` | `0x220` | `0x10BE5C` (`0xA76`) |

旧 portable 声明将该阶段标成 Android arm64 `sub_6BDE94`。四端当前共同
顺序为：

```text
Player_updateVisibility_guess
Player_updateCameraNode_guess
Player_updateShapeAABB_guess
Player_updateShapeGeometry_guess
MotionSubNode helper
```

四个最新 `Player_updateLayers_guess` 反编译结果的引用表都解析出
`Player_updateShapeGeometry_guess` 及其对应入口。该 helper 不接收当前时间，
也没有 preview early-return。

## 节点内记录布局

节点中的记录与公开 `Motion.Point/Circle/Rect/Quad` 使用同一源码级结构：

```cpp
struct HitData {
    int32_t type;
    std::array<double, 15> values;
};
```

`values` 的槽位含义为：

| 槽位 | 使用者 | 含义 |
|---|---|---|
| `0,1` | point/circle | x/y 或 center x/y |
| `2` | circle | radius |
| `3..6` | rect | left/top/right/bottom |
| `7..14` | quad | x0/y0、x1/y1、x2/y2、x3/y3 |

64 位目标和 Android ARMv7 的 ABI 把 `values` 对齐到记录 `+8`，记录总大小
`0x80`；iOS ARMv7 的 double ABI 对齐为 4，`values` 从记录 `+4` 开始，
总大小 `0x7C`。详细的公开 NCB 对象、完整记录复制与 contains 边界见
`analysis/motionplayer_geometry_four_binary_2026-08-11.md`。

## ABI 对照

这些偏移只用于反编译复核，不应进入 portable C++ 源码注释。

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| MotionNode 步长 | `2632` | `2272` | `2648` | `2228` |
| `nodeType` | `+28` | `+20` | `+28` | `+20` |
| `shapeType` | `+32` | `+24` | `+32` | `+24` |
| active slot index | `+1392` | `+1160` | `+1392` | `+1128` |
| slot 步长 | `536` | `432` | `536` | `420` |
| slot `done` | `node+344+536*i` | `node+320+432*i` | `node+344+536*i` | `node+308+420*i` |
| slot `ox` | `node+376+536*i` | `node+344+432*i` | `node+376+536*i` | `node+328+420*i` |
| slot `oy` | `node+384+536*i` | `node+352+432*i` | `node+384+536*i` | `node+336+420*i` |
| vertex X/Y | `+152/+160` | `+136/+144` | `+152/+160` | `+136/+144` |
| accumulated scale X/Y | `+1544/+1552` | `+1304/+1312` | `+1560/+1568` | `+1272/+1280` |
| accumulated matrix | `+120..144` | `+104..128` | `+120..144` | `+104..128` |
| geometry type | `+1664` | `+1424` | `+1680` | `+1392` |
| geometry values | `+1672..1784` | `+1432..1544` | `+1688..1800` | `+1396..1508` |

这里的 `i` 是 active-slot index；四端都直接使用它计算 slot 地址，没有范围
检查。

## 共同控制流

```text
for nodeIndex in [1, nodes.size):
    node = nodes[nodeIndex]

    if node.nodeType != 1:
        continue

    slot = node.slots[node.activeSlotIndex]       // unchecked
    if slot.done != 0:
        continue

    geometry.type = node.shapeType               // always written
    switch unsigned32(node.shapeType):
        case 0:
            geometry.values[0] = node.vertexX
            geometry.values[1] = node.vertexY

        case 1:
            geometry.values[0] = node.vertexX
            geometry.values[1] = node.vertexY
            geometry.values[2] =
                node.accumulated.scaleX * 16.0 * 0.5

        case 2:
            halfWidth  = node.accumulated.scaleX * 16.0 * 0.5
            halfHeight = node.accumulated.scaleY * 16.0 * 0.5
            geometry.values[3] = node.vertexX - halfWidth
            geometry.values[4] = node.vertexY - halfHeight
            geometry.values[5] = node.vertexX + halfWidth
            geometry.values[6] = node.vertexY + halfHeight

        case 3:
            build quad values[7..14] as below

        default:
            // type changed; all 15 doubles retained
```

`shapeType` 是 32 位字段。A64 反编译器把 jump selector 表成 unsigned 64 位，
iOS arm64 显式显示 `<= 3` gate；四端对任何负值或大于 3 的值都走 default，
但此前 type 写入仍保留原始低 32 位。

## eligibility 与跨帧保留

gate 严格只有 node type 与 slot done 两项。这里没有：

- accumulated active；
- visibility `drawFlag`；
- source valid；
- parent 状态；
- preview；
- `shapeType` 合法性预检。

因此 inactive、不可见或 source invalid 的 type-1 节点，只要 slot done 为零，
仍然更新几何。相反，slot done 非零时连 `geometry.type` 都不会刷新。

不同 shape 只写自己的槽位。例：circle 更新 `0..2` 后切换到 rect，rect 只写
`3..6`，旧 circle 槽位仍在；切换到未知 type 又只改首个 type。该状态随后由
`LayerGetter.shape` 的完整记录复制路径整体搬到独立堆对象，而不是在 getter
处按当前类型重新清零或重建。

## 构造、复制与对象生命周期

四端对 MotionNode ctor/common initializer 的 fresh decompile 均显示，节点内完整
geometry 字节区没有任何写入：

| 目标 | geometry 字节区 | ctor/common-init 结果 |
|---|---:|---|
| Android arm64 | `1664..1791` | 无读写 |
| Android armv7 | `1424..1551` | 无读写 |
| iOS arm64 | `1680..1807` | 无读写 |
| iOS armv7 | `1392..1515` | 无读写 |

`shapeType` 本身在节点基础字段初始化时为零，但 geometry 的 `type` 和 15 个
double 是另一块独立存储，不能因此推断为零。portable 声明必须是
`HitData shapeGeometry;`，不能写成 `HitData shapeGeometry{};`。

同一检查还发现四端构造都不写 type-7 的 `shapeAABB[4]` 缓冲；本轮同步移除
该数组的默认零初始化，并回补 ShapeAABB 专题说明。两者都遵循“生产阶段先写、
消费者后读”的生命周期；非法调用顺序下的分配器残留边界不应由 portable 代码
擅自定义为零。

节点内 geometry 不单独分配或析构；它随 MotionNode 的 deque 元素存活。节点
复制路径按完整记录搬运它，公开 shape getter 再分配对应 facade 的独立完整副本；
公开副本的 adaptor 才拥有该堆记录。

## quad 数据流

令：

```text
originX = slot.ox*m11 + slot.oy*m12
originY = slot.ox*m21 + slot.oy*m22

nx1 = m11 * -8        nx2 = m12 * -8
ny1 = m21 * -8        ny2 = m22 * -8
px1 = m11 *  8        px2 = m12 *  8
py1 = m21 *  8        py2 = m22 *  8
```

四个顶点按左上、右上、右下、左下顺序写入。四端机器指令共同的实际加法分组为：

```text
x0 = vertexX + ((nx1 + nx2) - originX)
y0 = vertexY + ((ny1 + ny2) - originY)

x1 = vertexX + ((px1 + nx2) - originX)
y1 = vertexY + ((py1 + ny2) - originY)

x2 = vertexX + ((px1 + px2) - originX)
y2 = vertexY + ((py1 + py2) - originY)

x3 = (nx1 + (vertexX + px2)) - originX
y3 = (ny1 + (vertexY + py2)) - originY
```

对应机器码区间：

| 目标 | quad 运算与写回区间 |
|---|---:|
| Android arm64 | `0x6BB38C..0x6BB450` |
| Android armv7 | `0x587CF0..0x587DE0` |
| iOS arm64 | `0x100110DBC..0x100110E88` |
| iOS armv7 | `0x10E568..0x10E650` |

这不是纯排版细节。令 `vertexX=1e16`、`nx1=-1e16`、`nx2=+1`、
`px2=-1`、`originX=0`：

- 原生分组的 x0 与 x3 都为 0；
- 旧 `vertexX + nx1 + nx2` 左结合式产生 x0=+1；
- 旧 `vertexX + nx1 + px2` 左结合式产生 x3=-1。

portable helper 以中间量显式保留上述共同分组，防止有限大数舍入、无穷、NaN
或 signed-zero 路径被“数学等价”重排。

## point / circle / rect 边界

- point 写 x/y，但公开 `Point.contains` 对 type 0 仍返回 false。
- circle 半径严格为 `scaleX * 16.0 * 0.5`，不取绝对值。负 scale 产生负
  radius；后续 circle contains 因半径平方而与正值同半径一致。
- rect 分别使用 scaleX/scaleY 的有符号半尺寸，不对 left/right 或 top/bottom
  排序。负 scale 可产生 left > right 或 top > bottom，后续 contains 沿用原始
  半开比较边界。
- 本阶段完全不读取 vertex Z 或 Player zFactor；它消费的是前置
  vertex-computation 阶段已经生成的 vertex X/Y。

## 本地差异与修复

| 旧本地行为/注释 | 四端证据 | 修复 |
|---|---|---|
| A64 声明注释写 `sub_6BDE94` | 真入口 `0x6BB274`，其余三端也有独立 helper | 删除 compiled source 旧地址，本文记录纠错 |
| quad 使用 `clipOriginX/Y` | 四端按 active-slot index 直接取 `ox/oy` | 改读当前 slot |
| quad 全部写成普通左结合表达式 | 四端机器码共享上节两种分组 | 抽出显式分组 helper |
| `HitData shapeGeometry{}` | 四端构造均不写完整记录 | 移除默认零初始化 |
| `float shapeAABB[4] = {}` | 四端构造均不写该缓冲 | 同步移除默认零初始化 |
| 大段 switch 难以直接测试部分写入 | 共同主体稳定、gate 独立 | 抽成 `updateShapeGeometryRecord_guess` |
| 相关声明继续携带旧单端地址 | 地址已经被四端证伪 | compiled source 删除，ABI/地址只留 analysis |

原有 gate 顺序、type 先写、circle/rect 公式以及按 shape 局部写槽位的主体均与
四端相符，未添加清零、范围检查或合法性 fallback。

## 测试与构建验证

新增确定性测试源覆盖：

- point 只写 `0..1`，其余预填哨兵保持；
- circle 只写 `0..2`，并保留负 scale 产生的负 radius；
- rect 只写 `3..6`，负 scale 不重排左右边界；
- quad 只写 `7..14`；
- 大数探针区分原生 operation grouping 与旧左结合表达式；
- 未知负 shapeType 只更新 type，完整 values 数组保持。

验证结果：

- Web debug `index.html` 增量构建、链接成功，随后 Ninja 显示 no work。
- Wasmtime guest 增量构建、链接及 exnref 转换成功，随后 Ninja 显示 no work。
- 使用当前 Web Emscripten 参数对单元测试源做 syntax-only 检查成功，仅有项目
  既有 `_tss` literal-operator deprecation warning。
- 当前 Windows 原生 Catch 可执行文件仍受既有 vcpkg/cocos2dx 配置约束；本轮
  没有伪造替代 fixture，也没有把 syntax-only 描述成已执行测试。

## IDB 改进

四个 IDB 已统一命名并保存：

- `Player_updateShapeGeometry_guess`；
- current node、slot base/index、shape type 等局部变量按各端可恢复程度命名；
- function start、node-type gate、slot-done gate、type 发布、quad 数据源和局部
  槽位写入添加行级注释；
- MotionNode constructor/common initializer 添加 geometry 与 shapeAABB 未初始化
  的生命周期注释；
- 四个 helper 和四个 `Player_updateLayers_guess` 都做 fresh decompile，主函数
  引用表显示新名称；
- 四端 quad 机器指令区间逐一检查，没有从单端反编译器的扁平表达式推断浮点
  结合顺序。

四份数据库均通过 IDA 原生保存成功。
