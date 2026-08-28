# Player shape-AABB phase 四参考二进制联合恢复

日期：2026-08-27

## 1. 本 slice 边界

本报告闭合 `Player::updateLayersPhase3_ShapeAABB` 的完整函数：非根节点遍历、parent 解析、
shape gate、局部矩形投影、parent clip 收缩、clip 指针发布，以及异常/边界行为。后续
shape-geometry、prepared-item 和 calcBounds 仍是独立 slice。

本地修改前在该 phase 中插入 `SNAPSHAPE` 路径诊断。它会在 native 算法前获取开关、构造 motion
path，并在算法后转换 label、格式化和写 stderr；还依赖一个参考 node record 中不存在的
`MotionNode::index`。这不是无语义的注释，因此必须先读取四端完整函数再删除。

## 2. 四端完整映射

| 平台 | 函数 | 完整指令 | 范围 |
|---|---:|---:|---:|
| Android arm64 | `0x6BB0A0` | 115 | `0x6BB0A0..0x6BB270` |
| Android armv7 | `0x587978` | 164 | `0x587978..0x587BA8` |
| iOS arm64 | `0x100110B20` | 112 | `0x100110B20..0x100110CDC` |
| iOS armv7 | `0x10E274` | 143 | `0x10E274..0x10E46A` |

四个函数均 fresh decompile，并从 offset 0 读取完整 disassembly；四个 cursor 都是 `done=true`。
它们是各自 `updateLayers` root 的第五个 phase3 target，紧邻 camera-node 与 shape-geometry。

## 3. 四端共同伪代码

```text
for i = 1 .. nodes.size-1:
    node = nodes[i]
    parent = nodes[node.parentIndex]       // gate 前，无范围检查

    if node.type != 7 or !node.accumulated.active:
        node.clipAABB = parent.clipAABB
        continue

    originX = slot.oy * m12 + slot.ox * m11
    originY = slot.oy * m22 + slot.ox * m21

    xa = posX - m12*16 - m11*16 - originX
    xb = m12*16 + posX + m11*16 - originX
    ya = posY - m22*16 - m21*16 - originY
    yb = m22*16 + posY + m21*16 - originY

    shape[0] = float(min-in-native-order(xa, xb))
    shape[1] = float(zFactor * posZ + min-in-native-order(ya, yb))
    shape[2] = float(max-in-native-order(xa, xb))
    shape[3] = float(zFactor * posZ + max-in-native-order(ya, yb))

    if parent.clipAABB != null:
        shape.minX = max-in-native-order(shape.minX, parent.minX)
        shape.minY = max-in-native-order(shape.minY, parent.minY)
        shape.maxX = min-in-native-order(shape.maxX, parent.maxX)
        shape.maxY = min-in-native-order(shape.maxY, parent.maxY)

    node.clipAABB = &node.shapeAABB[0]
```

所有四端都在 type/active gate 前解析 parent，且直接消费有符号 parent index；没有 bounds guard。
不合格节点只借用 parent clip 指针，不改自己的四个 shape float。合格节点先写四个 float，再按
parent clip 原地收缩，最后把 clip 指针发布为自身数组地址；空/反向相交不会归一化或清空。

## 4. 数据流与边界

投影使用 active slot 的 `ox/oy`、accumulated 的二维矩阵/位置以及 Player `zFactor`。所有计算先以
double 完成，在写 shape 数组时缩窄为 float；parent clip 比较在 float 域完成。`16.0` 是四端共同
常量，没有按 mesh division 或纹理尺寸缩放。

比较由条件选择实现，NaN 和相等值的 operand 选择顺序属于可观察边界；本地使用专门的
`orderShapeAxis_guess`、`clampShapeMinimumToParent_guess` 和
`clampShapeMaximumToParent_guess` 保留该顺序，不能随手替换成排序容器或带 validity check 的矩形类。

函数本身没有 callback、分配、字符串、logger、外部 helper call 或 recoverable exception path。
只有无效 deque/parent/node 内存与浮点环境属于 native 未防御边界。

## 5. ABI 差异

node record stride 分别为 Android arm64 `2632`、Android armv7 `2272`、iOS arm64 `2648`、
iOS armv7 `2228`。Android/libstdc++ 使用 deque iterator-difference 公式求 count；iOS/libc++
读取保存 count 并通过 map/block 解析节点。shape/clip/slot/矩阵字段 offset 因 ABI 不同，但共同
控制流、store 顺序与指针所有权一致。

32 位函数指令更多来自 VFP compare/select、deque addressing 和寄存器保存，不是算法分叉。

## 6. 修改前本地对照

native 主算法已匹配：从 index 1 遍历、parent-before-gate、type 7/active gate、四角投影、double 到
float、parent clip 收缩和 self pointer 发布均一致。

唯一确认的不匹配是一整组诊断旁路：

1. phase 入口读取 `logoSnapshotMarkEnabled`；
2. 可能调用 `matchedMotionPath` 并构造 `std::string`；
3. 计算固定 `43..50` 时间窗和 `m2logo.mtn` substring；
4. 固定筛选 `node.index == 18`；
5. 转换 layer label，格式化 `SNAPSHAPE` 并写 stderr。

四端完整函数都没有这些读取、调用和异常前沿，也没有读取一个逻辑 node ordinal。删除整个旁路，
不能只删 `fprintf` 而保留入口分配和路径访问。

## 7. 证据后实施与验证

在上述四端完整证据固化后，已从 `PlayerUpdateGeometry.cpp` 删除入口 snapshot 开关/path/time-window
构造和循环内 `SNAPSHAPE` block。shape-AABB body 现在从 nodes 引用直接进入非根遍历。

四个 IDB 已统一命名 `Player_updateLayersPhase3_ShapeAABB_guess`，添加语义注释与 bookmark 并保存。
后续 calcBounds 完整审计关闭最后一个 ordinal consumer 后，`MotionNode::index` 与 build/root 赋值也已
删除；shape-AABB 不再只是停用日志，而是彻底没有该 compiled sidecar 字段。
修改后执行 `git diff --check`、coverage 12 列与 duplicate-ID 检查。当前环境缺少
CMake/Ninja/Emscripten 正式工具链，不能声称 unit/Web build 通过。
