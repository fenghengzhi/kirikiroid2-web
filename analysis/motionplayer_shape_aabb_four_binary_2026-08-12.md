# MotionPlayer ShapeAABB / clip chain 四端对照（2026-08-12）

## 结论

本专题以 `reference/binaries/` 中 Android arm64、Android armv7、iOS
arm64、iOS armv7 四个当前参考二进制为共同真值，重新核验
`updateLayers` 后处理链中的 ShapeAABB 生成与父级 clip 传播。旧
`libkrkr2.so` 地址注释不再作为证据。

四端实现具有同一控制流和同一浮点比较边界：helper 只遍历非根节点；只有
`nodeType == 7 && accumulated.active` 的节点生成自己的四元素 AABB，其他节点
每帧直接转发 parent 的 nullable `clipAABB` 指针。合格节点从当前 active slot 的
`ox/oy`、accumulated 仿射矩阵和位置计算候选边界，先在 double 域按原生比较
顺序选轴边界，再给 Y 加投影 Z，窄化成 float，最后与 parent clip 做 parent
优先的 float clamp，并把 `clipAABB` 发布为自身 `shapeAABB` 缓冲区。

本轮证伪了四类本地偏差：旧 Android arm64 地址 `0x6BDCC0` 不是 helper
入口；parent index 没有范围 guard；原点取 current slot 的原始 `ox/oy`，不是
缓存的 `clipOriginX/Y`；Z 必须在 Y 候选排序之后加入。常规 `std::min/max`、
`std::minmax` 或“先比较再 swap”也都不能保持参考二进制的 NaN 和有符号零行为。

后续 diagnostic-isolation 复核还确认，四个 `Player_updateShapeAABB_guess` 都是 0 string
refs、0 direct calls。Web 侧 SNAPSHAPE 的 motion-path conversion、path filter、layer-label
narrow、frame window 和 `fprintf` 均不属于 native helper；它们现只在 phase entry 缓存的
snapshot gate 命中后执行。完整证据见
`motionplayer_geometry_diagnostic_isolation_four_binary_2026-08-14.md`。

## 函数边界与调用链

| 目标 | `Player_updateShapeAABB_guess` | 大小 | `Player_updateLayers_guess` |
|---|---:|---:|---:|
| Android arm64 | `0x6BB0A0` | `0x1D4` | `0x6B871C` (`0xAE4`) |
| Android armv7 | `0x587978` | `0x232` | `0x5856E0` (`0x9DC`) |
| iOS arm64 | `0x100110B20` | `0x1C0` | `0x10010E544` (`0xB50`) |
| iOS armv7 | `0x10E274` | `0x1F8` | `0x10BE5C` (`0xA76`) |

旧本地声明将 Android arm64 ShapeAABB 标为 `sub_6BDCC0`。该地址不对应
当前四参考中的函数入口；真实共同顺序为：

```text
Player_updateLayersVertexComputation_guess
Player_updateVisibility_guess
Player_updateCameraNode_guess
Player_updateShapeAABB_guess
ShapeGeometry helper
```

四个最新 `Player_updateLayers_guess` 反编译结果的引用表均解析出上述新名称和
对应入口。ShapeAABB 不接收当前时间参数，也没有 preview early-return。

## ABI 对照

这些偏移只用于反编译复核，不应进入 portable C++ 源码注释。

### Player 与 MotionNode

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Player `zFactor` | `+1112` | `+768` | `+1000` | `+700` |
| MotionNode 步长 | `2632` | `2272` | `2648` | `2228` |
| `nodeType` | `+28` | `+20` | `+28` | `+20` |
| parent index | `+36` | `+28` | `+36` | `+28` |
| active slot index | `+1392` | `+1160` | `+1392` | `+1128` |
| slot 步长 | `536` | `432` | `536` | `420` |
| accumulated active | `+1505` | `+1265` | `+1521` | `+1233` |
| accumulated pos X | `+1512` | `+1272` | `+1528` | `+1240` |
| accumulated pos Y | `+1520` | `+1280` | `+1536` | `+1248` |
| accumulated pos Z | `+1528` | `+1288` | `+1544` | `+1256` |
| `clipAABB` 指针 | `+1936` | `+1680` | `+1952` | `+1644` |
| `shapeAABB[0..3]` | `+2144..2156` | `+1824..1836` | `+2160..2172` | `+1788..1800` |

### 矩阵与 active-slot 原点

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| accumulated `m11` | `+120` | `+104` | `+120` | `+104` |
| accumulated `m12` | `+128` | `+112` | `+128` | `+112` |
| accumulated `m21` | `+136` | `+120` | `+136` | `+120` |
| accumulated `m22` | `+144` | `+128` | `+144` | `+128` |
| active slot `ox` | `node+376+536*i` | `node+344+432*i` | `node+376+536*i` | `node+328+420*i` |
| active slot `oy` | `node+384+536*i` | `node+352+432*i` | `node+384+536*i` | `node+336+420*i` |

这里的 `i` 是未检查的 active-slot index。helper 通过 node 内嵌 slot 数组直接
读取 `ox/oy`；没有读取本地结构中另存的 `clipOriginX/Y`。

## 共同伪代码

```text
for nodeIndex in [1, nodes.size):
    node = nodes[nodeIndex]
    parent = nodes[node.parentIndex]                 // unchecked

    if node.type != 7 or !node.accumulated.active:
        node.clipAABB = parent.clipAABB
        continue

    slot = node.slots[node.activeSlotIndex]          // unchecked
    m11, m12, m21, m22 = node.accumulated.matrix
    px, py, pz = node.accumulated.position

    originX = slot.oy*m12 + slot.ox*m11
    originY = slot.oy*m22 + slot.ox*m21

    xFirst  = px - m12*16 - m11*16 - originX
    xSecond = m12*16 + px + m11*16 - originX
    yFirst  = py - m22*16 - m21*16 - originY
    ySecond = m22*16 + py + m21*16 - originY

    xMin = xFirst <  xSecond ? xFirst  : xSecond
    xMax = xFirst <= xSecond ? xSecond : xFirst
    yMin = yFirst <  ySecond ? yFirst  : ySecond
    yMax = yFirst <= ySecond ? ySecond : yFirst

    projectedZ = player.zFactor * pz
    node.shapeAABB[0] = float(xMin)
    node.shapeAABB[1] = float(projectedZ + yMin)
    node.shapeAABB[2] = float(xMax)
    node.shapeAABB[3] = float(projectedZ + yMax)

    if parent.clipAABB != null:
        node.shapeAABB[0] =
            parent[0] < node[0] ? node[0] : parent[0]
        node.shapeAABB[1] =
            parent[1] < node[1] ? node[1] : parent[1]
        node.shapeAABB[2] =
            parent[2] > node[2] ? node[2] : parent[2]
        node.shapeAABB[3] =
            parent[3] > node[3] ? node[3] : parent[3]

    node.clipAABB = &node.shapeAABB[0]
```

表达式中的加减结合次序按反编译和机器指令保留。尤其 `xFirst` 不是先求一个
“中心点”再减半径，`xSecond` 也不是对 `xFirst` 取对称值；有限普通数下看似等价
的重排会在舍入、无穷或 NaN 输入上改变边界。

## double 轴排序的精确边界

四端机器码检查区间：

| 目标 | compare/select 指令区间 |
|---|---:|
| Android arm64 | `0x6BB1E0..0x6BB260` |
| Android armv7 | `0x587A80..0x587B4C` |
| iOS arm64 | `0x100110C08..0x100110CA8` |
| iOS armv7 | `0x10E376..0x10E43C` |

共同选择规则为：

```text
minimum = first <  second ? first  : second
maximum = first <= second ? second : first
```

因此不能折叠为抽象的“取最小/最大”：

| 输入关系 | minimum | maximum |
|---|---|---|
| `first < second` | first | second |
| `first > second` | second | first |
| 相等（含 `+0/-0` 数值相等） | second | second |
| first=NaN、second=finite | second | first(NaN) |
| first=finite、second=NaN | second(NaN) | first |

相等时两个结果都选择第二操作数，故 `first=+0, second=-0` 会同时产生 `-0`。
unordered 时两个结果分别保留不同操作数，常规 `std::minmax` 或 swap 写法无法
表达这个分裂。

## Z 投影、float 窄化与 parent clamp

Y 的两个候选先在不含 Z 的 double 域排序；之后才计算
`projectedZ = zFactor * posZ` 并分别相加。将 Z 提前放入两个候选再排序，对普通
有限数通常相同，但会改变 NaN、无穷、舍入以及 signed-zero 路径，因此不是四端
等价实现。

四个边界先由 double 表达式计算，再逐项窄化并存入 float `shapeAABB`。parent
clamp 读取的已经是 float child 值，比较与选择也在 float 域进行。其共同规则为：

```text
clampedMin = parentMin < childMin ? childMin : parentMin
clampedMax = parentMax > childMax ? childMax : parentMax
```

相等和 unordered 都选择 parent 操作数：

- child 与 parent 数值相等但符号零不同，结果保留 parent 的零符号；
- parent 为 NaN 时结果为 parent NaN；
- child 为 NaN、parent 有限时结果为 parent 有限值。

这同样不是可以无条件替换为 `std::max(child,parent)` / `std::min(child,parent)`
的实现细节。

## clip 指针数据流与生命周期

`shapeAABB` 是 MotionNode 自身的四 float 内嵌缓冲；`clipAABB` 是独立的 nullable
指针。它也不同于 visibility 阶段建立的 visible-ancestor 指针和 mesh 阶段使用的
祖先状态。四端构造函数/共同初始化函数都不写 `shapeAABB` 的 16 字节；portable
声明因此也不做零初始化。只有合格 type-7 节点发布该缓冲后，内容才有定义。

每帧对非根节点的状态转移只有两类：

- 不合格节点将 `clipAABB` 覆盖为 parent 当前帧的 `clipAABB`。它不保留上帧
  指针，也不发布自己的旧 `shapeAABB`。
- 合格节点先重写自身 `shapeAABB`，可选地与 parent clip 相交，然后将
  `clipAABB` 指回该内嵌缓冲。该指针随节点对象存活，不拥有单独堆分配。

deque 的树构造顺序使 parent 先于 child，因此 child 读取的是同一 pass 中 parent
已经传播或发布的 clip。helper 不触碰 root；root 的初始 nullable 状态来自节点
构造。原生对 parent index 与 active-slot index 都不做范围检查，portable 实现也
不在非法数据上安静继承旧值或跳过节点。

## eligibility 与边界行为

合格条件严格只有两个：node type 等于 7、accumulated active 非零。这里没有：

- active slot `done` gate；
- visibility `drawFlag` gate；
- source valid gate；
- preview 分支；
- parent clip 非空要求。

因此不可见但 active 的 type-7 节点仍会计算并发布 clip；非 type-7 或 inactive
节点则无条件转发 parent 指针。parent clip 为空时，合格节点仍产生自己的 clip，
只是跳过 clamp。

## 本地差异与修复

| 旧本地行为/注释 | 四端证据 | 修复 |
|---|---|---|
| A64 声明注释写 `sub_6BDCC0` | 真入口是 `0x6BB0A0`，其余三端也有清晰独立 helper | 删除 compiled source 旧地址，本文记录纠错 |
| parentIndex 先做范围检查并跳过 | 四端都直接索引 deque | 删除 guard，保留原生非法输入边界 |
| 不合格节点可能保留旧指针 | 四端每帧写 `parent.clipAABB` | 明确覆盖传播 |
| 原点使用缓存 `clipOriginX/Y` | 四端直接按 active slot index 读取 `ox/oy` | 改读 `activeSlot().ox/oy` |
| Y 候选先加 projected Z 再排序 | 四端先排序无 Z 候选，再分别加 Z | 恢复求值次序 |
| 以普通 min/max 或 swap 表达轴排序 | 四端 compare/select 在相等与 unordered 时有操作数偏好 | 增加显式双比较 helper |
| parent clamp 用普通数学 min/max | 四端在相等与 unordered 时选择 parent | 增加 parent-priority helper |
| 注释混淆 clip 与其他 ancestor 链 | 独立指针字段、独立 pass | 修正 MotionNode 生命周期注释 |

## 测试与构建验证

确定性测试新增覆盖：

- 有限数的正常轴排序；
- equality 选择第二操作数，并用 `+0/-0` 检查结果符号；
- first/second 分别为 NaN 时的 min/max 操作数分裂；
- parent clamp 在相等时保留 parent 零符号；
- parent/child 分别为 NaN 时的 parent-priority 选择。

验证结果：

- Web debug `index.html` 32 步增量构建和链接成功。
- Wasmtime guest 31 步增量构建、链接和 exnref 转换成功。
- 使用当前 Web Emscripten 参数对单元测试源做 syntax-only 检查成功，仅有项目
  既有 `_tss` literal-operator deprecation warning。
- 构建中的其他 warning 仍是既有 `_tss`、imagepacker `nodiscard` 与链接诊断。
- Windows 原生 Catch 可执行文件仍受既有 vcpkg/cocos2dx 配置约束，本轮没有
  伪造替代 fixture。

## IDB 改进

四个 IDB 已统一命名并保存：

- `Player_updateShapeAABB_guess`；
- current node、parent node、parent clip 局部变量按各端可恢复程度命名；
- function start、type/active gate、double min/max 选择、parent clamp、非合格节点
  clip 传播添加行级注释；
- 四个 helper 与四个 `Player_updateLayers_guess` 均做 fresh decompile，主函数
  引用表显示新名称；
- 四端机器指令 compare/select 区间均单独检查，没有从单端反编译器表面语法
  推断 NaN 或 signed-zero 语义。

四份数据库均通过 IDA 原生保存成功。
