# MotionPlayer PreparedRenderItem draw-affine 两阶段管线：四参考二进制联合复原（2026-08-13）

## 1. 结论

四个当前参考二进制都把 prepared-item 的 draw-affine 实现放在同一个递归 builder
内部，并明确分成两个阶段：

1. 先从 `MotionNode` 向持久 `PreparedRenderItem` 复制原始 corners、paintBox、viewport
   与三个彼此独立的 mesh vector；
2. 若根 Player 的 draw-affine non-identity flag 非零，再在同一个 item 上原地变换指定
   字段，随后才进入 item 的挂接/入队路径。

因此，旧源码中“在后续 command materializer 才应用 affine”以及旧
`libkrkr2.so` 地址 `0x6C2BB0` 都不再是有效依据。当前四端证据表明，这不是一个后续
命令阶段，也不是复制各字段时零散执行的若干变换，而是递归 prepared-item builder
自己的统一后段。

## 2. 四目标函数与阶段位置

| 参考二进制 | 递归 builder | 原始 corners 复制 | affine 后段入口/flag gate |
|---|---:|---:|---:|
| Android arm64-v8a | `Player_appendPreparedRenderItems_guess` `0x6BF714` | `0x6C0978..0x6C09A4` | `0x6BFF68` / `0x6BFF70` |
| Android armv7 | `Player_appendPreparedRenderItems_guess` `0x58B178` | `0x58B558..0x58B574` | `0x58B6AE..0x58B6BC` |
| iOS arm64 | `Player_appendPreparedRenderItems_guess` `0x1001148F8` | `0x100114D70` 附近 | `0x10011517C..0x100115188` |
| iOS armv7 | `Player_appendPreparedRenderItems_guess` `0x1123D8` | `0x112722..0x112740` | `0x11296A..0x112974` |

各目标中，raw-copy 与 affine block 都落在同一个函数边界内。Android arm64 的大函数
优化最激进，点与矩形 helper 被内联；另外三个目标保留了部分独立 helper，但数据阶段
完全相同。

## 3. 根 Player 是唯一 affine owner

递归 builder 不读取当前 child Player 自己的矩阵，而是先通过 root/canonical-owner
指针取 draw-affine 状态。四端对应字段如下：

| 目标 | non-identity flag | `m11/m12/m21/m22` | `tx/ty` |
|---|---:|---:|---:|
| Android arm64 | root `+0x263` | `+0x328/+0x330/+0x338/+0x340`，double | `+0x348/+0x34C`，float |
| Android armv7 | root `+0x19B` | `+0x218/+0x220/+0x228/+0x230`，double | `+0x238/+0x23C`，float |
| iOS arm64 | root `+0x1F3` | `+0x2B8/+0x2C0/+0x2C8/+0x2D0`，double | `+0x2D8/+0x2DC`，float |
| iOS armv7 | root `+0x15B` | `+0x1D8/+0x1E0/+0x1E8/+0x1F0`，double | `+0x1F8/+0x1FC`，float |

共同点变换公式为：

```text
x' = m11 * x + m12 * y + tx
y' = m21 * x + m22 * y + ty
```

矩阵乘加在 double 域中执行。点 vector/corners 的结果写回 prepared item 时转换为
float；矩形路径则把四个 double 角点各自先转换为 float，随后才在 float 域执行
min/max 与 floor/ceil。root flag 为零时不依据矩阵值重新判断 identity；整个后段直接
跳过。这意味着 flag 与矩阵值若被人为制造成不一致状态，原生以 flag 为准。

## 4. 第一阶段：复制原始 item 数据

四端在 affine gate 之前完成以下写入：

- `corners[8]`：从节点四角逐 float 复制，尚未变换；
- `paintBox[4]`：从节点持久 bounds/AABB 直接复制；
- `viewport[4]`：有有效 `clipAABB` 时复制，否则保留 invalid sentinel；
- `commandCompositeMeshPoints`：总是从节点 composite vector 赋值，因此源 vector 为空
  时也会清空 item 旧内容；
- `meshPoints`：仅在 composite 为空、节点 mesh type 为 1 且 raw control points 非空时，
  从 processed/transformed control-point vector 赋值；
- `commandBezierPatchPoints`：同一 type-1 分支中从 raw control-point vector 赋值。

关键布局对应如下：

| 字段 | 64-bit item | 32-bit item | Android A64 node | Android A32 node | iOS A64 node | iOS A32 node |
|---|---:|---:|---:|---:|---:|---:|
| corners | `+0x88` | `+0x70` | `+0x740` | `+0x650` | `+0x750` | `+0x62C` |
| paintBox | `+0xB8` | `+0xA0` | `+0x760` | `+0x670` | `+0x770` | `+0x64C` |
| viewport | `+0xC8` | `+0xB0` | 节点 clip-AABB 指针所指数据 | 同左 | 同左 | 同左 |
| composite vector | `+0x158` | `+0x11C` | 节点对应 vector | 同左 | 同左 | 同左 |
| raw Bezier vector | `+0x178` | `+0x12C` | 节点对应 vector | 同左 | 同左 | 同左 |
| processed mesh vector | `+0x190` | `+0x138` | 节点对应 vector | 同左 | 同左 | 同左 |

这些偏移只用于逆向证据定位，不进入编译源码注释。

## 5. 第二阶段：选择性原地变换

root non-identity flag 非零时，四端顺序一致：

```text
entry.corners
  -> entry.commandCompositeMeshPoints
  -> entry.meshPoints
  -> valid entry.viewport 的四角 AABB 变换与向外取整
  -> entry.paintBox 的四角 AABB 变换与向外取整
  -> 后续 parent/child 挂接与 main/aux 入队
```

`commandBezierPatchPoints` 被刻意排除。也就是说，prepared item 同时保存：

- 已经进入最终 draw-affine 坐标系的 processed mesh points；
- 仍在原始节点坐标系中的 raw Bezier control points。

这不是 vector 声明顺序导致的偶然遗漏。64-bit 两端分别遍历 item `+0x158` 与
`+0x190`，跳过中间的 `+0x178`；32-bit 两端分别遍历 `+0x11C` 与 `+0x138`，跳过
中间的 `+0x12C`。四端共同给出同一选择集合。

## 6. 矩形边界行为

### 6.1 四角包围盒

矩形不是只变换左上/右下。原生变换：

```text
(left, top)
(right, top)
(right, bottom)
(left, bottom)
```

每个角点的 double 乘加结果先单独收窄为 float，然后分别取四个 float x/y 的 min/max，
得到旋转、错切后的 axis-aligned envelope。最终存储：

```text
left   = floor(minX)
top    = floor(minY)
right  = ceil(maxX)
bottom = ceil(maxY)
```

这是相对于收窄后 float 角点的向外取整。先收窄再取整与在 double 域取完 min/max 后
才转换不同：靠近整数边界时，错误的后者可能产生相差 1 的边界。

四端的 min/max 都使用有序 float compare/select。两值相等（包括 `+0/-0`）或比较
unordered 时选择右操作数；因此本地 helper 不能直接假设 `std::min/std::max` 的
NaN/有符号零选择规则等价。当前端口以显式 `<`/`>` 条件表达式保留这一边界。

### 6.2 viewport 与 paintBox 的非对称 gate

viewport 只在原始 clip AABB 满足以下条件时有效：

```text
right >= left && bottom >= top
```

clip-AABB 指针非空时，四端先无条件复制四个 raw float，再检查有效性；因此 pointer-backed
但 `right < left` 或 `bottom < top` 的无效坐标仍留在 item 中，只是不进入矩形 helper。
只有指针为空时才写入 `{1, 1, -1, -1}` sentinel。

有效且 affine flag 为零时，四端保留逐 float 原样复制的 viewport，不执行 floor/ceil；
因此即使矩阵数值恰好是单位矩阵，也不能无条件调用 rounded helper。有效且 affine flag
非零时，才执行四角变换和向外取整。无效 pointer-backed viewport 与 null-pointer
sentinel 都不进入矩形 helper。

paintBox 没有对应的有效性 gate：affine flag 非零时总会被送入同一四角包围盒语义并向外
取整。flag 为零时则保持第一阶段复制的节点 raw bounds。

### 6.3 helper 形态

| 目标 | 四角 envelope helper | 向外取整 wrapper |
|---|---:|---:|
| Android arm64 | builder 内联 | builder 内联 |
| Android armv7 | `transformRectByDrawAffine_guess` `0x5902B0` | 调用点分别执行 floor/ceil |
| iOS arm64 | `transformRectByDrawAffine_guess` `0x10011A8D8` | `transformAndRoundRectByDrawAffine_guess` `0x100115840` |
| iOS armv7 | `transformRectByDrawAffine_guess` `0x118BE8` | `transformAndRoundRectByDrawAffine_guess` `0x113160` |

iOS arm64 的 envelope helper 被 Hex-Rays 推成了不完整返回签名，但汇编明确通过 SIMD
寄存器携带四个 float 分量；不能依据伪代码缺失的高分量把它误判为只返回两个值。

## 7. 对本地源代码的恢复

`cpp/plugins/motionplayer/PlayerRenderItems.cpp` 已恢复为相同的两阶段结构：

- corners 与 paintBox 先作 raw copy；
- 三个 mesh vector 先按原生分支完成独立赋值；
- 随后用一个 late affine block 依次原地变换 corners、composite 与 processed mesh；
- raw Bezier vector 保持未变换；
- viewport 和 paintBox 统一复用
  `transformAndRoundPreparedRect_guess` 的四角 envelope/向外取整语义；
- identity/disabled 路径继续保持 viewport 和 paintBox 原始小数值，不误做取整；
- 移除了该段旧 `libkrkr2.so` 地址与硬编码 item 偏移注释，当前地址仅保存在本分析文档。

这次调整在通常最终输出上可能与原先“复制时立刻变换”的写法相同，但恢复了真正的数据
流边界：后续若继续复原复用旧 item、分支残留、异常路径或 trace 点，阶段次序会直接影响
可观察状态。

## 8. IDB 增量改进

四个当前 IDB 已写入 affine-stage 入口注释。Android armv7 与 iOS 两端的矩形 helper
已按上述 `_guess` 名称重命名并补充语义；Android arm64 的等价逻辑为内联实现，故只在
builder 内联区标注。四个 IDB 均已原位保存。

## 9. 验证

- `Web Debug Build`：成功重新编译 `PlayerRenderItems.cpp`、归档 motionplayer 并链接
  `index.html/index.wasm`；只有仓库既有 `_tss`、pthread memory-growth、JSPI 与
  Emscripten JS library warning。
- `Wasmtime Headless Debug Build`：成功重新编译普通 motionplayer 与 guest objects，
  并完成最终链接；诊断同上，无新增错误。
- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp`：复用 Web Debug 的真实
  Emscripten defines/includes/ABI 参数并加入 `out/syntax-check` 的 Catch2 与
  `test_config.h`，执行 `-fsyntax-only` 成功；唯一诊断为既有 `_tss` warning。
- 回归断言已加入既有 `getCommandList` 持久-item 测试，覆盖 affine corners、带小数
  raw AABB 的 paintBox/viewport 向外取整、processed mesh point 变换，以及 raw Bezier
  control points 不变。当前 CMake 配置没有可直接运行本组 Catch2 用例的 executable，
  因此这里只声明完整翻译单元编译验证，不把 syntax-only 冒充成运行时执行。
- `git diff --check`：成功；输出仅含工作树既有的 LF→CRLF 提示。
- 四个 IDB 在最终 float/narrowing 与 invalid-viewport 注释补充后再次保存成功。
