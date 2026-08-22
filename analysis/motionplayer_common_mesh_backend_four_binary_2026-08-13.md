# motionplayer common mesh backend：四参考二进制复原

日期：2026-08-13

本轮复原的是 Player direct/D3D 与 `__Private_Motion_GLLayer` 共用的 mesh
后端。它不是简单的“把全部网格格子展开后提交”：原实现把源纹理越界扩展、
整体包围盒、逐格裁剪、两套顶点容器、回调调用与源纹理引用计数放在一个公共
函数里。以下地址只用于逆向证据，不进入编译源码注释。

## 四端映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `RenderMesh_buildAndSubmitTriangles_guess` | `0x69AFE4` | `0x575800` | `0x1000F974C` | `0xF685C` |
| `RenderMesh_makeRepeatedSoftwareBitmap_guess` | `0x69AE08` | `0x575688` | `0x1000F9570` | `0xF6650` |
| Player batch callable | `0x6D9BA8` | `0x59B4D0` | `0x1001294E0` | `0x128572` |
| Private GLL callable | `0x6DB41C` | `0x59C790` | `0x10012B3AC` | `0x12A080` |
| 独立 `std::function` trampoline | 内联 | armv7 主函数内联；throw helper `0xD64380` | `0x1000FA5F0` | `0xF7780` |

四个 recovery IDB 均已为主函数、repeat helper、Player callable 与可识别的
callback trampoline 写入语义名/注释并保存。

## 函数接口与容器

四端共同接口可还原为：

```cpp
bool buildAndSubmit(
    SubmitCallback callback,
    Rect &clipRect,
    Texture *source,
    Rect sourceRect,
    vector<PointD> const &boundsPoints,
    vector<PointD> const &meshPoints,
    int divX,
    int divY);
```

关键容器：

- `vector<double> sourceColumns`，长度在非负 `divX` 下为 `divX + 1`；
- `vector<double> sourceRows`，长度在非负 `divY` 下为 `divY + 1`；
- `vector<int> selectedCells`，先 reserve `divX * divY`，元素为行优先 cell
  index；
- partial-clip 分支的 `vector<uint8_t> pointInside`，每个目标网格点一字节；
- 最终两条并行的 `vector<PointD>`：destination 与 source，每个被选 cell
  各写六点；
- `std::function` callback 只调用一次，不是每 cell 或每 range 调用。

`boundsPoints` 与 `meshPoints` 是否为同一 vector 对象是语义分支，不是内容
比较：

- composite mesh / geometry type 2：两个参数是同一对象，直接进入逐点/逐格
  clip 路径；
- Bezier patch / geometry type 1：`boundsPoints` 是偏移后的控制点，
  `meshPoints` 是细分网格。先用控制点包围盒做快速判定，只有部分相交时才
  扫细分网格。

## 源纹理生命周期与越界源矩形

主函数一进入就对 source texture 增加一次引用，早于 source rect、clip、
division 和 vector 内容验证。正常成功路径在 callback 返回后显式 Release；
空 clip 的特定早退也显式 Release；其余普通 return 与异常路径由编译器生成的
scope cleanup 负责。

越界条件四端一致：

```text
left < 0 || top < 0 || right > source.width || bottom > source.height
```

GPU/non-software 后端不抛异常，也不跳过绘制，而是以 important=true 记录完整
UTF-16 消息：

```text
Repeat texture for opengl is not implemented yet.
```

随后仍使用原 texture 与原 source coordinates 继续。

软件后端调用 repeat helper：

1. 用 `floor(left / textureWidth)` 与 `floor(top / textureHeight)` 把 source
   left/top 归一到纹理周期内；
2. 计算覆盖所需的水平与垂直 copy 数；两者都为 1 时返回 null；
3. 否则创建宽为 `horizontalCopies * textureWidth`、高为
   `verticalCopies * textureHeight` 的 32-bpp bitmap；
4. 水平循环逐扫描线复制固定 `textureWidth * 4` 字节；
5. 垂直循环从原 source base 复制 `sourcePitch * textureHeight` 字节到每个
   后续 band。这一实现是刻意不对称的，并不是再复制已横向展开的第一 band；
6. 主函数先 Release 原 source texture，再经当前 render manager 的 bitmap
   texture factory 取得替代 texture；factory 返回值即使为空也原样成为后续
   source，保持自然边界行为。

repeat helper 内部 fresh bitmap 的所有权没有可见的显式 `Release`；源码按
参考调用链保留这一点。2026-08-15 又 fresh 核对了四端 helper/caller 交界并修正
portable 实现中曾经提前执行 bitmap texture factory 的顺序；完整逐指令表见
`motionplayer_common_mesh_repeat_handoff_four_binary_2026-08-15.md`。

## 精确 source 坐标

source width/height 先以 32-bit integer 相减，再转 double。每个分割坐标是：

```text
sourceX[x] = width  * double(x) / double(divX) + normalizedLeft
sourceY[y] = height * double(y) / double(divY) + normalizedTop
```

原实现只以符号位决定是否构造这些数组：division 为负时数组为空；division
为零时循环仍写一个 `0/0`，自然产生 NaN。它没有 `div >= 1` 的保护。cell
count 仍是有符号 32-bit `divX * divY`，随后传入 vector reserve，溢出和负值
也不被人工净化。

## 包围盒规则

> 2026-08-16 addendum：本节原先把所有上界都概括成
> `int(point + 1.0)`，遗漏了 `boundsPoints[0]` 的独立初始化：四端均先
> `int(point)`，再对所得 32-bit word 执行 wrapping `+1`。后续 outer point、
> mesh-point 重算与 cell AABB 才是 `int(point + 1.0)`。这会改变首点位于
> `(-1, 0)`、NaN 及饱和边界时的结果；精确逐端证据与回归见
> `motionplayer_common_mesh_clip_pruning_numeric_boundary_four_binary_2026-08-16.md`。

坐标转整数使用 toward-zero 截断，不是 `floor`/`ceil`：

```text
lower = int(point)
upper = int(point + 1.0)
```

这对负小数尤其重要。例如 `-0.75` 的 lower 是 0，而不是 -1。

Bezier 的 `boundsPoints != meshPoints` 快路径：

1. 控制点整体包围盒；
2. clip 为空立即 Release source、返回 false；
3. clip 全包含控制点包围盒时，把所有 `0 .. divX*divY-1` cell 加入；
4. 完全不相交返回 false；
5. 部分相交进入通用逐格判定。

`boundsPoints == meshPoints` 不做这组整体快路径，而是直接计算每个 mesh point
是否落在半开 clip：`left <= x < right && top <= y < bottom`。

逐格判定按四角 `00, 10, 01, 11`：

- 任意角落在 clip 内，cell 立即选中；
- 否则计算四角整数包围盒并与 clip 做严格 overlap；
- 被选 cell 同时并入最终写回包围盒；
- 若没有 cell 被选，callback 不调用，返回 false。

这不是精确的三角形/矩形相交测试；它是“角点命中，否则 cell AABB overlap”。

2026-08-15 fresh 尾部核对进一步确认：上述 bounds 只有在 callback 正常返回并且
current source 已 Release 后才写回 caller rect；empty selection 和 callback 异常都保留
input rect。逐端地址与回归见
`motionplayer_common_mesh_bounds_commit_four_binary_2026-08-15.md`。

## 六点绕序与一次回调

每个 selected cell 的 destination 和 source 都用相同顺序：

```text
00, 10, 01, 10, 01, 11
```

这个顺序由四端最终 vector 写入逐项确认。此前本地注释/实现曾写成
`00,01,10,01,11,10`，属于从旧目标迁移后留下的过时结论，本轮已纠正。

所有 selected cell 按升序行优先 index 合并到同一对 vector，最后一次性调用
callback。空 `std::function` 走 `std::bad_function_call`；callback 抛异常时 source
texture 与所有临时 vector 由 unwind cleanup 释放，callback 之后的显式 bounds
写回不发生。

Private callable 直接 `OperateTriangles`；Player callable 把同一批点追加到
`TriangleBatch_guess`，其 target/reference 为同一个纹理，真正提交可继续与前后
同状态 item 合批。common helper 写回的整数包围盒是函数自身的 output；两个 native
callable 捕获并继续使用 raw renderer/Private Draw 外层传入的原 clip，而不是把这个
output bounds 当作 `OperateTriangles` / batch key 的 clip。

## 本地落地

- 新共享实现：`cpp/plugins/motionplayer/MotionRenderBackend.{h,cpp}`；
- `PrivateMotionGLL.cpp` 的 Bezier 路径把控制点与细分点分别传入，共享 helper
  回调再执行原 Private `OperateTriangles`；
- `PlayerRenderTargets.cpp` 的 Bezier 路径同样传控制点/细分点，composite 路径
  同一个 vector 传两次；回调只向 `TriangleBatch_guess` 追加一次；
- 去除了两个调用方原先重复、无 clip pruning 且绕序错误的 mesh 展开循环。

验证：`cmake --build out/web/debug -j 8` 完整成功；`git diff --check` 无 whitespace
错误，仅有仓库既存的 LF/CRLF 提示。
