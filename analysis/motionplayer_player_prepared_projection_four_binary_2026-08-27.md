# Player prepared-item camera/stereovision projection 四参考二进制联合恢复

日期：2026-08-27

## 1. 结论

四个参考二进制在`prepareRenderItems`稳定排序后、进入D3D或canvas/SLA renderer前，共享同一个
in-place projection pass。它对main prepared-item pointer vector中的每个item先应用两个float
camera offset，再按可选stereovision camera执行depth projection并重建paintBox。

本地`applyPreparedRenderItemProjectionCore_guess`的字段选择、数值顺序、mesh分流、NaN gate、
paintBox重建和不处理字段均与四端一致，本轮无需修改运行代码。此 slice把direct D3D
coordinator中此前独立保留的projection缺口闭合；它之前的`prepareRenderItems` wrapper、
递归builder调用边界与stable-sort owner/EH随后由`MP-G11-PLAYER-PREPARE-SORT-WRAPPER`
闭合，递归builder深层主体又由`MP-G11-PLAYER-APPEND-PREPARED-ITEMS`闭合。

## 2. 四端函数与完整指令

| 平台 | helper | 完整指令数 |
|---|---:|---:|
| Android arm64 | `0x6D2644` | 253 |
| Android armv7 | `0x596EB0` | 327 |
| iOS arm64 | `0x100123038` | 228 |
| iOS armv7 | `0x1220F0` | 335 |

四端fresh decompile，合计1143条指令全部完整读取。共同源级签名是：

```text
void Player::applyPreparedRenderItemProjection(
    vector<PreparedRenderItem*>& mainList)
```

只有sorted main list作为容器参数；aux/composite-owner list不进入此调用边界。vector只被遍历，
不resize、不替换pointer，也不拥有item。

## 3. camera输入与projection origin

Player上的两个camera offset是float。stereovision flag为真时：

```text
originX = stereovisionCameraX(double) + double(cameraOffsetX)
originY = stereovisionCameraY(double) + double(cameraOffsetY)
cameraZ = stereovisionCameraZ(double)
```

flag为假时三个double local都置0。offset仍无条件应用；三个double只在后续stereo branch使用。
读取顺序在ABI字段偏移不同后保持相同。没有getter、Variant、锁或临时owner。

## 4. 无条件float translation

mainList按pointer顺序遍历，每个pointer立即解引用，无null gate。对每个item严格依次处理：

1. 固定四个corner的8个float，逐(x,y)做float加法；
2. `commandCompositeMeshPoints`的每个float point；
3. 仅当`meshType == 1`时处理实际`meshPoints`；
4. paintBox四个float全部加offset；
5. viewport四个float全部加offset。

四端对corners都按固定32 bytes处理，而不是读取dynamic length；本地对应
`std::array<float,8>`。两个point vector按raw begin/end步长8遍历，空vector不读取。

`commandBezierPatchPoints`不在这个pass中处理。Bezier renderer使用的实际`meshPoints`在
meshType 1时被平移；raw command payload保持原坐标。viewport即使是无效/null sentinel
`{1,1,-1,-1}`也照常平移，且后续不会做perspective projection。

所有offset addition都在float域执行；不先提升为double。这决定大坐标、subnormal、signed zero和
rounding边界。

## 5. stereovision gate与投影公式

translation完成后，只有以下条件同时成立才投影：

```text
stereovisionActive && item.sortKey != stereovisionCameraZ
```

比较是普通ordered C++ equality：`+0.0 == -0.0`会跳过projection；任一NaN不相等，因此进入
projection。两个相等infinity也跳过；不同/NaN/infinity组合进入后保留原生IEEE除法行为。

每个点按x后y顺序计算：

```text
denominator = itemZ - cameraZ
projectedX = sourceX - itemZ * (sourceX - originX) / denominator
projectedY = sourceY - itemZ * (sourceY - originY) / denominator
store float(projectedX), float(projectedY)
```

source float先提升double，乘、减、除均为独立double操作，最后各窄化一次float。四端没有FMA。
gate排除了普通有限`denominator == 0`的相等情况，但NaN等unordered边仍可产生NaN。

## 6. paintBox重建

进入projection时，translation后的旧paintBox被覆盖为：

```text
{ FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX }
```

随后每投影一个点立即按以下顺序grow：floor(x)、floor(y)、ceil(x)、ceil(y)，并用普通ordered
`<`/`>`比较更新left/top/right/bottom。NaN compare全部为false，因此对应sentinel可保留。

grow来源与顺序固定为：

1. 四个corners；
2. 全部`commandCompositeMeshPoints`；
3. 仅meshType 1的全部实际`meshPoints`。

command Bezier payload、viewport和其他child vectors不参与重建。如果三组几何都为空（corners在
正常结构中固定存在，畸形对象才可能破坏），sentinel不被“修正”。

## 7. 对象、容器与异常边界

helper只修改借入item，不分配、不调用用户代码、不构造owner，也没有异常landing pad。普通
执行中可见的sharp边界只有trusted raw pointers、畸形vector begin/end和浮点特殊值：

- mainList含null会立即崩溃；
- vector元数据畸形会越界；
- 几何修改是逐字段in-place，无transaction或rollback；
- 中途同步fault时，之前item/字段已提交；
- stereo关闭或Z相等只跳过projection，之前translation仍保留。

它不改变mainList顺序、item parent/child关系、source descriptor、clip flag、stencil bytes或
sortKey。

## 8. 四端共同伪代码

```text
if stereo:
    origin = stereoCameraXY + floatCameraOffset
    projectionZ = stereoCameraZ
else:
    origin = 0
    projectionZ = 0

for itemPtr in mainList:
    item = *itemPtr
    translate(item.corners)
    translate(item.commandCompositeMeshPoints)
    if item.meshType == 1: translate(item.meshPoints)
    translate(item.paintBox[4])
    translate(item.viewport[4])

    if !stereo || item.sortKey == projectionZ:
        continue

    item.paintBox = {+FLT_MAX,+FLT_MAX,-FLT_MAX,-FLT_MAX}
    project-and-grow(item.corners)
    project-and-grow(item.commandCompositeMeshPoints)
    if item.meshType == 1:
        project-and-grow(item.meshPoints)
```

## 9. 本地核对与验证

- `translatePreparedPoint_guess`保持float addition；
- `projectPreparedPoint_guess`保持double中间量与逐操作顺序；
- `growProjectedPaintBox_guess`保持ordered compare的NaN行为；
- core只处理main list、四角、composite points和meshType 1实际mesh；paintBox/viewport只在正确
  阶段修改；
- `commandBezierPatchPoints`不触碰，viewport不投影；
- 本轮没有运行语义改动。

四个IDB已追加函数注释与书签并原位保存。coverage 12列、deterministic NCB输出、Python helper
compile和`git diff --check`继续作为可用验收。当前缺少CMake/Ninja/Emscripten且standalone
syntax check被`boost/locale.hpp`缺失阻塞，因此不宣称正式unit/Web build已运行。
