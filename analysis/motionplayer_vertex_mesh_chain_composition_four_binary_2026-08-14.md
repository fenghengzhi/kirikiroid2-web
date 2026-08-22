# MotionPlayer 顶点网格链组合、分隔与建格（四参考二进制，2026-08-14）

## 1. 范围与结论

本轮只以 `reference/binaries/` 中 Android arm64、Android armv7、iOS arm64、
iOS armv7 四个当前参考目标为证据，重新恢复 `Player_updateLayers` 顶点阶段中的：

1. 实际 parent 到运行期 mesh-ancestor 链的选择；
2. 顶点阶段 dirty 传播和 skipped-node 边界；
3. `hasMeshData`、派生 inheritance separator、原始 PSB `meshCombine` 三个独立状态；
4. raw 4×4 patch 沿真实 parent 链的 displacement 组合；
5. own-affine patch、单位方格/仿射四角网格的构造；
6. ancestor 链的全网格阶段、仅 anchor 阶段和最终平移；
7. `std::vector<MeshPoint>` 的复用、尺寸错误后继续执行、无检查索引和整数边界。

旧本地实现把 raw `meshCombine` 与派生 separator 合并成一个
`meshCombineEnabled`，并用它同时控制粒子/子 motion 继承和 raw patch 组合。这会改变
链结构、dirty 传播、patch 内容和 processed-vertex 计数。现已拆为：

```cpp
bool meshCombine;                       // 持久 raw PSB property
bool meshInheritanceSeparator_guess;   // 每帧派生
bool meshVertexPassDirty_guess;         // 本次顶点遍历派生
```

此外，旧实现还存在以下偏差，现已修正：

- `hasMeshData` 只检查 mesh type/vector，漏掉 slot done、source valid 与 bit 8；
- 把普通 accumulated position 过早降为 float 后再加 inverse offset；
- 对所有 source node 覆盖 `vertexPosX/Y`，而原生只在 type 1/5 特殊块写该字段；
- 用 source origin 作为 ancestor cascade anchor，并按完整首尾 delta 平移网格；
- 未复原 raw-combine 的 `working += parent - identity`、size mismatch 后继续执行和临时
  vector 跨节点复用；
- 直接在四角上做不一致的网格构造，漏掉 type-1 先建 unit quad 再过自身 patch；
- mesh division 没有保持 unsigned saturation、own signed-domain cap 与 inherited
  unsigned-domain cap 的分叉、32 位乘法回绕、无保护 unsigned division和 50 上限的组合。

## 2. 四端函数与对象映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| vertex computation | `0x6B98D0` (`0x13EC`) | `0x5866F8` (`0xD86`) | `0x10010F6AC` (`0xF88`) | `0x10CE30` (`0xF58`) |
| bilinear grid builder | `0x6B8348` (`0x258`) | `0x585458` (`0x130`) | `0x10010E2BC` (`0x13C`) | `0x10BC22` (`0x11C`) |
| node initializer | `0x6B1058` | `0x580FA4` | `0x100108720` | `0x105E70` |
| raw `meshCombine` store | `0x6B1618` | `0x5811F6` | `0x100108A34` | `0x1061DE` |
| runtime unit-quad object | `0x1AB50B0` | `0x1111610` | `0x101B69598` | `0x187D2E0` |

四份 recovery IDB 已将主函数命名为
`Player_updateLayersVertexComputation_guess`，网格 helper 命名为
`buildBilinearMeshGrid_guess`。三个原本已有 data item 的 unit-quad 全局已命名为
`unitMeshQuadCorners_guess`；Android arm64 的 BSS 起点没有现成 data name，故只添加语义
注释，并把其 32-byte 映像初值命名为 `unitMeshQuadCornersInit_guess`。所有新恢复的原始
C++ 名均保留 `_guess`。

### 2.1 关键 MotionNode 字段布局

| 语义 | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| vertex-pass dirty marker | `+45` | `+37` | `+45` | `+37` |
| accumulated dirty input | `+1504` | `+1264` | `+1520` | `+1232` |
| `hasMeshData` | `+1962` | `+1694` | `+1978` | `+1658` |
| derived separator | `+1963` | `+1695` | `+1979` | `+1659` |
| raw `meshCombine` | `+1964` | `+1696` | `+1980` | `+1660` |
| mesh ancestor pointer | `+1968` | `+1700` | `+1984` | `+1664` |
| mesh type / sync mask / division | `+2000/+2004/+2008` | `+1720/+1724/+1728` | `+2016/+2020/+2024` | `+1684/+1688/+1692` |
| grid divisions X/Y | `+2012/+2016` | `+1732/+1736` | `+2028/+2032` | `+1696/+1700` |
| raw patch vector | `+2024` | `+1740` | `+2040` | `+1704` |
| composite grid vector | `+2048` | `+1752` | `+2064` | `+1716` |
| own transformed patch vector | `+2072` | `+1764` | `+2088` | `+1728` |
| inverse 2×2 / inverse offsets | `+2096/+2128` | `+1776/+1808` | `+2112/+2144` | `+1740/+1772` |

三个位于 `hasMeshData` 后的相邻 byte 不是同一 bitfield 的不同别名。特别是 raw
`meshCombine` 在四个 initializer 中从同名 PSB property 持久写入，顶点阶段不会把它改写；
separator 与 dirty marker 则按遍历顺序逐节点重算。

## 3. parent 选择、callback 与 dirty 传播

遍历从 node index 1 开始，直接用存储的实际 parent index 取 deque 元素：

```cpp
node = nodes[index];
parent = nodes[node.parentIndex]; // 无负数或范围检查
```

在读 parent mesh 状态之前，force-visible 节点从自己的 `emoteEdit` Variant 读取
`priorDraw`。随后运行期 mesh 链头为：

```cpp
node.meshAncestor =
    (parent.hasMeshData || parent.meshInheritanceSeparator)
        ? &parent
        : parent.meshAncestor;

node.meshVertexPassDirty =
    node.accumulated.dirty ||
    (node.meshAncestor && node.meshAncestor->meshVertexPassDirty);
```

dirty gate 位点分别为 `0x6B9B30`、`0x5868C0`、`0x10010F8D8`、
`0x10DB8C`。由于 traversal 是 parent-first，ancestor marker 是同一帧已经计算的状态，
不是上一帧缓存。

### 3.1 dirty=false 的保留行为

dirty=false 时不会重算 `hasMeshData` 或 separator，也不会清空 derived vectors；这些字段保留
之前的值。唯一仍执行的 mesh 修正是：当当前节点 raw `meshCombine` 且旧
`hasMeshData` 为 true 时，沿实际 parent 链移除已经被 raw-combine 吸收、同时恰好等于
`meshAncestor` 的节点。父节点的 raw `meshCombine` 决定是否继续上行。

该 skipped path 没有 parent/null/bounds 防护；本地实现保留这一边界。

## 4. 每帧 live mesh 与继承分隔位

dirty path 中 `hasMeshData` 的共同条件是：

```cpp
node.hasMeshData =
    !node.activeSlot.done &&
    node.meshType != 0 &&
    !node.rawPatch.empty() &&
    node.source.valid &&
    (node.meshSyncChildMask & 8) != 0;
```

四端结果写回位点为 `0x6B9B10`、`0x5868A2`、`0x10010F8B0`、
`0x10DB6A`。因此 raw vector 非空并不自动意味着节点可作为当前帧 mesh ancestor。

separator 的共同表达式为：

```cpp
node.meshInheritanceSeparator =
    node.meshAncestor != nullptr &&
    (node.inheritMask & 0x02000000) == 0;
```

反编译器把它表现为读取 `inheritMask` 32-bit word 的高字节 bit 1。写回位点分别为
`0x6B9BF8`、`0x586958`、`0x10010F954`、`0x10DC18`。该 byte 的 true
语义是“继承网格链存在，但这里把全网格变形阶段截断”；它既不是 raw
`meshCombine`，也不是 `meshSyncChildMask & 1`。

## 5. raw meshCombine 与函数级临时 vector

主函数进入 node loop 前构造一只局部 `std::vector<MeshPoint>`，离开整个函数时才析构。
每次 `clear()` 只令 end=begin，容量跨节点保留。共同伪代码为：

```cpp
const vector<MeshPoint> *effectivePatch = &node.rawPatch;
if(node.meshCombine) {
    if(node.hasMeshData) {
        working = node.rawPatch;
        MotionNode *p = &actualParent;
        for(;;) {
            if(p->hasMeshData) {
                if(p == node.meshAncestor)
                    node.meshAncestor = p->meshAncestor;

                if(byte_size(working) != byte_size(p->rawPatch))
                    log(L"mesh size is different.");

                for(size_t i = 0; i != working.size(); ++i) {
                    working[i].x = working[i].x +
                        (p->rawPatch[i].x - identityPatch[i].x);
                    working[i].y = working[i].y +
                        (p->rawPatch[i].y - identityPatch[i].y);
                }
            }
            if(!p->meshCombine)
                break;
            p = &nodes[p->parentIndex];
        }
    } else {
        working.clear();
    }
    effectivePatch = &working;
}
```

四端 raw-combine 分支入口为 `0x6B9D14`、`0x586A98`、`0x10010FAAC`、
`0x10CEF4`。重要数据流是“先处理当前 parent，再看该 parent 的 raw combine 决定是否继续”，
不是根据 child 的值一直走完整 meshAncestor 链。

### 5.1 错误和越界边界

- 比较的是两只 vector 的 byte span；不相等时记录精确 UTF-16 文本
  `mesh size is different.`；
- 记录后不 return、不截断为较短长度；循环次数始终是 `working.size()`；
- parent patch 较短会被无检查越界读取；
- identity patch 固定 16 点，`working.size()>16` 也会越界读取 identity；
- `working.empty()` 时仍可记录 mismatch，但算术循环为空；
- parent index 和上行链都不做范围或环检测。

本地 helper `addBezierPatchDelta_guess` 有意保留上述错误后继续和无检查索引，不把它美化为
`min(size)` 或 exception。

## 6. own-affine patch、inverse map 与普通顶点

该区域有两层不同的 type mask，不能合并：

```cpp
outer = forceVisible || ((_preview ? 7241 : 7233) & typeBit);
if(!slot.done && outer && source.valid) {
    // raw combine、derived vector clear、own transformed patch/inverse

    materialize = forceVisible ||
        (((_preview ? 5193 : 5185) & typeBit) && !source.blank);
    if(materialize) {
        // affine corners、composite grid、ancestor cascade、force-visible mirror
    }
}
```

内层 gate 位点为 `0x6BA20C`、`0x586EB8`、`0x10010FDD4`、`0x10D302`。
normal mask `5185` 选择 type 0/6/10/12，preview 的 `5193` 另加 type 3。
`source.blank` 只抑制普通路径；`forceVisible` 同时绕过 type mask 和 blank。内层失败时旧
`vertices[8]` 保留，但 composite vector 已在外层被 clear，own transformed patch/inverse 仍可
按本帧输入更新。

raw/combined `effectivePatch` 被用于本节点的 derived patch：

1. composite 与 transformed 两只 vector 先 `clear()`，保留容量；
2. 仅当 `meshType==1 && !effectivePatch.empty()` 时把 transformed resize 到恰好 16；
3. 无检查读取 `effectivePatch[0..15]`；
4. 用 source width/height、accumulated 2×2、source origin+slot origin 和 position 在
   double 域做 affine，再逐坐标降为 float；
5. 只有 `hasMeshData` 为 true 才写 inverse matrix/offset，且 determinant 为零时不保护。

普通 source quad 按 TL/TR/BR/BL 写四角。这里的 quad origin 是 source-space origin 经过
affine 后的位置，不会写回 `vertexPosX/Y`。原生只在 node type 1/5 的特殊路径把
accumulated anchor 逐层过 mesh ancestor 后写 `vertexPosX/Y/Z`；该路径每个 live ancestor
把 evaluator 的 float 输出重新扩为 double。

position 映射保留如下非对称精度：

```cpp
double tx = doublePositionX + double(ancestor.inverseOffsetX);
double ty = doublePositionY + double(ancestor.inverseOffsetY);
float u = float(invM11 * tx + invM12 * ty);
float v = float(invM21 * tx + invM22 * ty);
MeshPoint out = evaluateBezierPatchVector(ancestor.transformedPatch, u, v);
```

网格点自身已是 float，故其 offset 加法先在 float 域完成，再扩为 double 参与矩阵乘法。

## 7. bilinear grid builder 与 unit quad 生命周期

四端 helper 都先以 32-bit wrap 计算：

```cpp
int32_t count = int32_t(uint32_t(cellsX + 1) *
                        uint32_t(cellsY + 1));
output.resize(size_t(count));
```

AArch64 明确是 `MUL Wn` 后 `SXTW Xn`；ARM32 直接把 32-bit product 交给 vector resize。
随后对 `y=0..cellsY`、`x=0..cellsX` 做：

```cpp
double fy = double(y) / double(cellsY);
double left  = fy * BL + (1.0 - fy) * TL;
double right = fy * BR + (1.0 - fy) * TR;
double fx = double(x) / double(cellsX);
out[y*(cellsX+1)+x] = float2(
    right.x * fx + left.x * (1.0 - fx),
    right.y * fx + left.y * (1.0 - fx));
```

helper 没有 zero/negative guard。普通有限资产/ratio 下 stored divisions 至少为 1；
但 own-mesh product 转换后的 sign-bit-set word 会绕过 signed cap，随后生成 negative
stored divisions，因此 helper 自身的异常边界也能从 raw public ratio 到达。

unit quad 不是 constexpr local。它是 32-byte 进程级可写全局，静态初始化为：

```text
{0,0, 1,0, 1,1, 0,1}
```

Android arm64 的 `0x42F1F8` 从只读 32-byte 初值复制；Android armv7 的
`0x3016E8` 逐 word 写入；iOS 两端 `InitFunc_49` 分别位于 `0x10014FC74`、
`0x151C98` 并用 scalar/vector stores 初始化。

## 8. division、建格分支与整数语义

double 到 unsigned 32-bit 的转换在四端都是 saturating toward-zero 语义：NaN/非正数为 0，
`>=2^32` 为 `UINT32_MAX`，其余截断。转换后的 cap **按分支使用不同 comparison domain**：

```cpp
converted = fcvtzu(meshDivisionRatio * uint32(meshDivision));
ownType1Division = signed32_bits(converted) >= 50 ? 50 : converted;
inheritedSourceDivision = converted >= 50u ? 50 : converted;
```

因此 own type-1 路径保留 `0x80000000..0xFFFFFFFF`，inherited source 则把它们
cap 到 50。完整四端指令、NaN/Inf/`2^31` 表和 negative grid-counter 数据流见
[`motionplayer_update_layers_mesh_division_compare_domain_four_binary_2026-08-14.md`](motionplayer_update_layers_mesh_division_compare_domain_four_binary_2026-08-14.md)。

乘法、加法和 processed count 都按 uint32 回绕。AArch64 `UDIV` 的零除结果为 0；
Android armv7 调用 imported `__aeabi_uidiv`，iOS armv7 调用 libSystem imported
`___udivsi3`，两份 plugin 都不包含 external helper 的 zero policy。Web 本地
`unsignedDivideA64Profile_guess` 为避免 Wasm 整数除零 trap 而采用两份直接可证明的
AArch64 zero result；完整 owner/ABI 边界见
[`motionplayer_update_layers_unsigned_divide_zero_owner_four_binary_2026-08-14.md`](motionplayer_update_layers_unsigned_divide_zero_owner_four_binary_2026-08-14.md)。

### 8.1 有 ancestor 且 own type-1 patch 非空

调用点为 `0x6BA34C`、`0x586E4A`、`0x10010FF48`、`0x10D3B4`：

```cpp
splitX = (division * u32(width)) / u32(width + height);
meshDivX = splitX + 1;
meshDivY = division - splitX + 1;
buildGrid(meshDivX, meshDivY, composite, unitQuad);
for(point : composite)
    point = evaluateBezierPatchVector(transformedOwnPatch, point.x, point.y);
```

注意 helper 自身又各加 1，所以最终点数是
`(splitX+2)*(division-splitX+2)`。

### 8.2 其他有 ancestor 的节点

调用点为 `0x6BA4F4`、`0x586FF2`、`0x100110104`、`0x10D52E`。网格直接建在
本节点 affine TL/TR/BR/BL 四角上：

- type-1 但 patch 为空：仍用本节点自己的 signed-domain-cap scaled division；
- 非 type-1：沿 meshAncestor 找第一个 `hasMeshData` 节点，没有 null guard；先取其 scaled
  division（unsigned-domain cap），再按
  `u32(currentWidth+currentHeight) / u32(sourceWidth+sourceHeight)` 缩放，最后再次
  unsigned cap 50；
- 横向 split 用 `u32(currentWidth)` 和已经一次性转换的 `u32(currentWidth+currentHeight)`。

这意味着先分别转 width/height 再相加与先算 double sum 再转换在不同分支中有意不同。

### 8.3 无 ancestor 的 type-1 节点

该路径不构造 composite vector，只增加 processed count：

```cpp
processed += (division - splitX + 2) * (splitX + 2);
```

四端计数 store 位点为 `0x6BA494`、`0x586F8E`、`0x1001100A0`、
`0x10D4C8`；全部是 32-bit wrap。

## 9. ancestor 链的两阶段映射

网格构造后以当前 accumulated anchor（不是 quad origin）为 `meshPosition`。链被 separator
分成两段。

### 9.1 phase 1：全网格 + anchor

当当前节点 separator 为 false 时，继续处理 separator 也为 false 的 ancestor：

```cpp
while(ancestor && !ancestor->separator) {
    if(ancestor->hasMeshData) {
        map every composite point through ancestor;
        meshPosition = map anchor through ancestor;
        processed += composite.size() + 1;
    }
    ancestor = ancestor->meshAncestor;
}
```

四端入口为 `0x6BA510`、`0x58700E`、`0x100110128`、`0x10D54E`。

### 9.2 phase 2：仅 anchor

在 phase 1 结束后立即保存当前 anchor。剩余链只映射 anchor：

```cpp
shiftBase = meshPosition;
while(ancestor) {
    if(ancestor->hasMeshData) {
        meshPosition = map anchor through ancestor;
        processed += 1;
    }
    ancestor = ancestor->meshAncestor;
}
delta = float(meshPosition - shiftBase);
for(point : composite)
    point += delta;
```

四端第二阶段入口为 `0x6BA688`、`0x5870FC`、`0x100110200`、`0x10D658`。
关键边界是：网格只平移 phase 2 造成的 delta；phase 1 的形变已经逐点写入，不能再把
original-to-final 完整 anchor delta 加一次。

## 10. 本地实现与验证

本轮修改覆盖：

- `MotionNode.h`：拆分 raw combine、derived separator、vertex-pass dirty；
- `NodeTree.cpp`：`meshCombine` property 写 raw 字段；
- `PlayerLayerQuery.cpp`、`PlayerUpdateChildMotion.cpp`、`PlayerUpdateParticles.cpp`：消费派生
  separator，不再误用 raw property；
- `PlayerUpdateLayersInternal.h`：patch delta、point/position ancestor map、bilinear grid、
  saturating u32 conversion 和 safe portable UDIV helper；
- `PlayerUpdateGeometry.cpp`：恢复函数级临时 vector、dirty/hasMesh/separator、raw patch
  composition、own patch、网格分支、两阶段 ancestor mapping 与计数；
- `tests/unit-tests/plugins/motionplayer-dll.cpp`：identity displacement、3×3 row-major bilinear
  grid、零/负 cell、NaN/负数/截断/Inf division、零除，以及 5185/5193 + blank/force
  gate 回归。

验证结果：

- motionplayer 完整测试翻译单元 Emscripten syntax check 通过；
- `Web Debug Build` 完整构建/链接通过；
- Web Debug Wasm 的 `buildBilinearMeshGrid_guess` 保留单条 `i32.mul` 尺寸计算、signed
  loop compare、`f64.div/mul/add` 插值与最终 `f32.demote_f64`；
- Wasm 的 `addBezierPatchDelta_guess` 保留每坐标 `f32.sub` 后 `f32.add` 的
  `working + (ancestor - identity)` 结合顺序；position mapper 则先
  `f64.promote_f32` inverse offset 再与 double position 相加，矩阵结果才 demote；
- Wasm 的 division 路径将 signed meshDivision 作为 `f64.convert_i32_u`，50 比较为
  `i32.ge_u`，非零分母使用 `i32.div_u`，零分母显式返回 0；
- `git diff --check` 无内容错误；仓库现有 LF/CRLF 提示不属于本轮 whitespace 错误；
- 四份 recovery IDB 已写入函数/全局命名、关键语义注释并成功保存。

## 11. 本纵切面之外的已知未闭合项

本轮只宣告 mesh-chain composition 闭合，不把整个 vertex function 误报为完整：

1. force-visible TJS 镜像已在
   `motionplayer_force_visible_geometry_mirror_four_binary_2026-08-14.md` 独立闭合；真实键为
   `coord`/`mtx`，并确认写入经过 ancestor 映射的 anchor position；
2. vertex function 邻接的 render-item/material/stencil 生成仍应按各自四端调用链继续审计。

这些边界保留在计划中，避免旧单目标注释或当前高层实现被误当成已经验证的原版行为。
