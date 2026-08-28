# Player shape-geometry phase 四参考二进制联合恢复

日期：2026-08-27

## 1. 四端函数

| 平台 | 函数 | 完整指令 |
|---|---:|---:|
| Android arm64 | `0x6BB274` | 138 |
| Android armv7 | `0x587BAC` | 160 |
| iOS arm64 | `0x100110CE0` | 127 |
| iOS armv7 | `0x10E46C` | 145 |

四端均 fresh decompile，并完整读取 disassembly，cursor 全部 `done=true`。该函数位于
shape-AABB 之后；没有外部调用、分配、Variant owner、logger 或回调，副作用只落在
node-owned `HitData` 记录。

## 2. 共同控制流

四端共同伪代码为：

```text
for node in nodes[1..end):
    if node.nodeType != 1:
        continue
    slot = node.activeSlot()            // native index is consumed unchecked
    if slot.done:
        continue

    node.shapeGeometry.type = node.shapeType
    switch node.shapeType:
      0: write point slots 0..1
      1: write circle slots 0..2
      2: write rect slots 3..6
      3: write quad slots 7..14
      default: write no value slots
```

根节点不参与；`drawFlag`、`accumulated.active`、`source.valid`、preview 和 stencil 均不是门控。
函数先写 `type`，因此未知 shape kind 仍会发布新 type，而所有 value 槽保留旧字节。合法 kind
同样只覆盖自己所属槽位，整个记录从不整体清零。这与 native 构造阶段故意不初始化该记录组合成
可观察的 partial-record 生命周期。

## 3. 四种几何记录

point 使用 vertex phase 的最终 `vertexPosX/Y` 写槽 0、1。

circle 额外写：

```text
radius = accumulated.scaleX * 16.0 * 0.5
```

半径不取绝对值，负 scale 保留负半径；命中测试随后平方。

rect 使用相同固定 16 单位尺寸和两个 accumulated scale：

```text
halfWidth  = accumulated.scaleX * 16.0 * 0.5
halfHeight = accumulated.scaleY * 16.0 * 0.5
left   = vertexX - halfWidth
top    = vertexY - halfHeight
right  = vertexX + halfWidth
bottom = vertexY + halfHeight
```

负 scale 不重排边界；NaN/Inf 也没有净化。

quad 使用 accumulated 2x2 矩阵、active slot 的 `ox/oy` 和固定半径 8。先计算：

```text
originX = ox*m11 + oy*m12
originY = ox*m21 + oy*m22
```

再按 `(-8,-8)`, `(+8,-8)`, `(+8,+8)`, `(-8,+8)` 写四个点。四个优化构建的
浮点加减结合顺序一致，尤其第四点先把 position 与正项相加，再加负项、减 origin；本地 helper
显式拆出中间量保存了该舍入边界，而不是使用直观但不等价的统一公式。

## 4. 32/64 位布局差异

64 位 `HitData.values` 位于 type 后 8 字节，32 位 Android armv7 也按 8 对齐，而 iOS armv7
按 4 对齐；node stride、active-slot stride 和 field offsets 也随 ABI 改变。四端对字段角色和写入
集合完全一致，因此本地只保留自然 ABI 对齐和源级字段，不固化任何二进制 padding/offset。

## 5. 本地逐行对照

`Player::updateLayersPhase3_ShapeGeometry` 的 root exclusion、type/done gate、active-slot读取和输入字段
逐项匹配。`updateShapeGeometryRecord_guess` 的 type-first publication、partial slot writes、负 scale、
unknown kind retention 与 quad operation grouping 逐项匹配。已有单元用例覆盖：

- point/circle/rect 跨 kind 更新时旧槽保留；
- 负 scale 的 circle/rect 结果；
- 大数舍入敏感的 quad 运算分组；
- 未知 kind 只改 type、不改 values。

本轮无需修改编译语义。四库已统一命名、注释、bookmark 并保存。

## 6. 验证限制

已执行 coverage 严格 12 列、duplicate-ID 检查和 `git diff --check`。当前环境缺少正式
CMake/Ninja/Emscripten 依赖工具链，不能声称 unit/Web build 通过。
