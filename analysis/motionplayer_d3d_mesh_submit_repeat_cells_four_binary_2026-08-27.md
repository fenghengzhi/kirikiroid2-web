# Motion shared D3D mesh submit、重复纹理与 cell admission 四参考二进制联合恢复

日期：2026-08-27

## 1. 结论

四个参考二进制的 D3D deep renderer 与 Layer motion 扩展最终汇入同一个公共 mesh helper。
它负责 source texture 的手工引用、越界 source rect 的软件重复纹理、source 网格坐标生成、
outer/mesh 两组点的裁剪分流、逐点与逐 cell admission、selected-cell 容器、每格六顶点展开，
以及 type-erased submit callback 的最终调用。

本地几何、容器和重复位图主干已经与四端一致。本轮发现并修复一项对象生命周期偏差：本地
`TextureReference_guess` RAII 会在异常时释放 source，而原版使用手工 `AddRef/Release`；正常
成功和所有普通 `false` 出口显式释放，但分配、日志、texture factory 或 submit callback 抛异常
时不会补释放。现已删除该 RAII，并把单元测试改为验证 submit 抛异常后留下一个 source 引用，
再由测试显式回收。

本 slice 闭合公共 mesh helper、software repeat helper、submit wrapper、主要临时容器和正常/异常
owner 边。Bezier basis cache 与 patch tessellation 是下一独立 slice，不由本报告冒充闭合。

## 2. 四端函数图

### 2.1 公共 mesh helper

| 平台 | helper | 完整指令数 | 异常清理形态 |
|---|---:|---:|---|
| Android arm64 | `0x69AFE4` | 1829 | 函数尾内联 DWARF landing chunks；清临时 buffers，不 Release source |
| Android armv7 | `0x575800` | 871 | 主体内无本地 landing cleanup |
| iOS arm64 | `0x1000F974C` | 787 | 主体内无显式 landing cleanup |
| iOS armv7 | `0xF685C` | 1035 | SjLj cleanup `0xF736A`，173 条；清活动 string/vector，不 Release source |

共同源级签名恢复为：

```text
bool buildAndSubmitMeshTriangles(
    Rect& inoutClipOrComputedBounds,
    Texture* source,
    Rect sourceRect,
    const vector<Point>& boundsPoints,
    const vector<Point>& meshPoints,
    int divisionX,
    int divisionY,
    function<void(Texture*, const vector<Point>& sourceVertices,
                  const vector<Point>& destinationVertices)> submit)
```

Android arm64 的 fresh decompile 输出超过工具单次显示上限，因此结论没有依赖被截断的伪代码：
1829 条指令按 500/500/500/329 完整分页读取，并与另外三端完整伪代码及反汇编逐段交叉确认。
四端主函数总计完整读取 4522 条指令；另完整读取 iOS armv7 的 173 条 SjLj cleanup。

### 2.2 software repeat helper

| 平台 | helper | 完整指令数 | 构造失败 cleanup |
|---|---:|---:|---|
| Android arm64 | `0x69AE08` | 119 | 尾部 landing pad delete raw bitmap allocation 后 resume |
| Android armv7 | `0x575688` | 127 | 无独立 cleanup body |
| iOS arm64 | `0x1000F9570` | 114 | 无独立 cleanup body |
| iOS armv7 | `0xF6650` | 174 | `0xF6830`，12 条，delete raw allocation 后 SjLj resume |

### 2.3 submit callback envelope

Android 两端在主函数调用点直接检查 type-erased owner/invoke slot；空 callback 进入标准
`bad_function_call` helper。iOS 把 envelope 分出为：

| 平台 | wrapper | 完整指令数 |
|---|---:|---:|
| iOS arm64 | `0x1000FA5F0` | 23 |
| iOS armv7 | `0xF7780` | 25 |

两个 wrapper 都先检查 libc++ owner，空 owner 分配并抛 `std::bad_function_call`；非空时把
source texture pointer 作为 value argument 的地址交给 callable invoke，同时透传两个 point
vector 引用。callback 返回后主 helper 才 Release source、提交 bounds并返回 `true`。

## 3. source texture 与重复位图生命周期

共同入口无 null gate，第一项动作是直接增加 source texture 引用。source rect 只在以下任一
条件成立时进入 repeat 分支：left<0、top<0、right>textureWidth、bottom>textureHeight。

software manager 为真时：

```text
repeated = makeRepeatedSoftwareBitmap(source,
                                      inout sourceTop,
                                      inout sourceLeft,
                                      sourceWidth,
                                      sourceHeight)
if repeated != null:
    source.Release()
    source = privateOpenGLManager.CreateTexture2D(repeated)
```

repeat helper 返回 null 时保留旧 source 及其入口 AddRef。返回 bitmap 时先 Release 旧 source，
再调用私有 OpenGL manager 的 texture factory；factory 抛异常时旧 source 已释放，fresh bitmap
construction reference 仍无人回收。factory 返回 null 也不检查，后续普通出口会在 null 上调用
Release而崩溃。成功创建的新 texture reference 代替旧 source进入余下流程。

non-software 分支只重要级别记录宽字符串
`Repeat texture for opengl is not implemented yet.`，不改变 source rect、source owner或 texture。

正常 `false` 出口共有三类：输入 clip/rect无效、outer bounds完全不相交、selected cell为空；
四端都在返回前显式 Release 当前 source。成功出口先调用 submit，再显式 Release、写回四个
bounds word、返回 true。没有 scope guard：repeat helper、vector reserve/growth、日志、factory、
empty-function throw或用户 submit throw 都可能使当前 source reference泄漏；bounds仍保持调用前
值，因为写回发生在 submit成功之后。

## 4. software repeat 的内部数据流

helper 先读 source texture 的 width/height，再用 `value - floor(value/dimension)*dimension`
原地规范化 sourceLeft/sourceTop。这个规则使负坐标映射回 `[0, dimension)`；dimension为0时保留
原生除零/浮点转换未定义边界，不加保护。

```text
horizontalCopies = (sourceWidth  + textureWidth  + sourceLeft - 1) / textureWidth
verticalCopies   = (sourceHeight + textureHeight + sourceTop  - 1) / textureHeight
if horizontalCopies == 1 && verticalCopies == 1:
    return null

bitmap = new Bitmap(horizontalCopies * textureWidth,
                    verticalCopies * textureHeight,
                    32)
```

新 bitmap 始终为32 bpp，不按 source texture format分流。第一阶段逐 source row工作：对每一行，
把 `textureWidth*4` bytes 横向复制 `horizontalCopies` 次。第二阶段不是复制已横向展开的第一
band；它从原 source scanline 0开始，每次复制 `sourcePitch*textureHeight` bytes到后续 destination
band起点，共 `verticalCopies-1` 次。这会把原始 pitch布局原样打包进宽 destination，是四端一致、
看似不对称但可观察的行为。

bitmap constructor成功后的 fresh reference由 caller直接交给 texture factory，本 helper和 caller
都不 Release bitmap；仅 constructor抛异常时，A64 landing pad和iOS armv7 SjLj cleanup会 delete
尚未完成构造的 raw allocation。

## 5. source coordinate vectors 与 signed 边界

source width/height用 signed 32-bit subtraction得到。divisionX/Y各自只在非负时生成 vector，
循环范围均为闭区间 `0..division`：

```text
sourceColumns[x] = sourceWidth  * double(x) / double(divisionX) + sourceLeft
sourceRows[y]    = sourceHeight * double(y) / double(divisionY) + sourceTop
```

division为0时仍执行一次并产生原生 floating divide-by-zero/NaN路径；不会提前拒绝。cellCount是
`divisionX * divisionY` 的 signed 32-bit word结果，随后直接转成 `size_t` 交给
`selectedCells.reserve`。负结果会成为巨大 reserve；乘法溢出保留目标机 word语义。

helper随后无条件读取 `boundsPoints[0]`。空 boundsPoints不是安全空输入，而是越界/崩溃边界。
第一点的 bounds规则特殊：先把 x/y按目标转换截向零，再对 right/bottom做 signed word `+1`；
后续点则对 `x+1.0` / `y+1.0`再转换。这使 `(-1,0)` 内负小数、NaN、无穷和 int32边缘具有
不同结果，本地已有专门用例保持这一差异。

## 6. outer bounds 与 cell admission

当 `boundsPoints` 与 `meshPoints` 是同一个 vector对象时，helper跳过独立 outer scan，直接进入
逐 mesh-point/cell路径。两者不同时先扫描 boundsPoints：

1. 输入 clip若 left>=right 或 top>=bottom：Release source并返回 false；
2. outer bounds有效且完整包含于 clip：把 `[0, cellCount)` 全部压入 selectedCells；
3. outer bounds与 clip严格相交：进入细粒度选择；
4. 其余情况：最终无 selected cell，Release source并返回 false。

细粒度路径先为每个 mesh point生成一个 byte，点命中采用半开矩形：
`left <= x < right && top <= y < bottom`。同时扫描整个 meshPoints形成最终候选 bounds。

每个 cell按 row-major编号，四角索引为：

```text
p00 = x + y       * (divisionX + 1)
p10 = p00 + 1
p01 = x + (y + 1) * (divisionX + 1)
p11 = p01 + 1
```

任一角点命中就立即选择该 cell，不再要求 cell AABB严格相交。因此共享边上的 inside角可以把
相邻两格都纳入。四角都未命中时，helper才以同样的截向零/`point+1`规则构造 cell AABB，并
要求两个 rect各自有效且满足四个严格 overlap条件。AABB仅接触 clip边缘不算相交。

被AABB选中的 cell会把其 bounds再 fold进先前全 mesh point扫描得到的 bounds；正常非空点集
下通常幂等，但赋值顺序和first-selected状态仍按原版保留。索引、division与点数之间没有一致性
检查：畸形division或过短meshPoints可除零或越界。

## 7. selected-cell 容器与六顶点展开

selectedCells为空时显式 Release source并返回 false。非空时，destination/source两个
`vector<Point>`分别 reserve `selectedCells.size()*6`，然后按selectedCells顺序逐格追加。

source和destination采用同一固定三角形绕序：

```text
TL, TR, BL,
TR, BL, BR
```

source顶点来自sourceColumns/sourceRows；destination顶点来自meshPoints。所有selected cell
先展开到两个完整vector后才调用一次submit。source vector先于destination vector构造/增长；
任一分配失败按平台异常策略清理已构造vector，但不补 source Release。

submit期间调用者传入的clip/bounds尚未更新。callback可重入、抛异常或改变外部target/source；
helper不做rollback。callback正常返回后顺序严格为：Release source → 写left/top/right/bottom →
返回true。

## 8. 四端共同伪代码

```text
source.AddRef()
if sourceRect escapes source texture:
    if software:
        repeated = repeatAndNormalize(source, sourceTop, sourceLeft, w, h)
        if repeated:
            source.Release()
            source = privateGL.CreateTexture2D(repeated)
    else:
        importantLog(repeatNotImplemented)

build inclusive source column/row vectors
cellCount = int32_word(divisionX * divisionY)
selected.reserve(size_t(cellCount))
initialize bounds from boundsPoints[0]

if boundsPoints is not meshPoints:
    scan outer bounds
    reject invalid clip
    if clip fully contains outer: select all cells
    else if not strict overlap: reject

if selected empty:
    scan mesh points: half-open pointInside + whole-mesh bounds
    for every cell:
        if any corner inside: select
        else if strict cell-AABB overlap: select and fold bounds

if selected empty: source.Release(); return false
reserve source/destination vertices at 6 per cell
expand every selected cell as TL,TR,BL / TR,BL,BR
submit(source, sourceVertices, destinationVertices)
source.Release()
publish bounds
return true
```

## 9. 本地改动

- 删除仅用于本 helper 的 `TextureReference_guess` RAII；
- 入口仍直接 `AddRef`，software replacement前、三个普通false出口和submit成功后改为显式
  `Release`；
- 保留异常时不补Release、factory前旧source已释放、bounds在submit后才提交的原生顺序；
- 更新 `common mesh emits the four-reference cell winding exactly`：submit callback抛异常后先
  断言source不再independent，再手工Release泄漏引用并确认恢复；
- repeat bitmap算法、point/cell admission、selected容器与六顶点绕序逐行核对后保持不变。

## 10. 验证与剩余边界

- 四端主 helper fresh decompile；Android arm64被截断的伪代码以完整1829条分页反汇编替代；
- 四端4522条主 helper、534条repeat helper、iOS armv7 173+12条SjLj cleanup，以及iOS
  23/25条submit wrapper均完整读取；
- 四个IDB均已命名主/repeat helper，iOS wrapper和armv7 cleanup也已命名；函数注释、书签写入
  并原位保存；
- 本地可执行验收继续使用coverage 12列检查、现有mesh/repeat单测源级审计、Python helper
  compile与`git diff --check`；
- 当前环境缺少CMake、Ninja和Emscripten，standalone syntax check又被缺失的
  `boost/locale.hpp`阻塞，因此不宣称正式native/Web build或单测已运行。

`cubicBezierBasisTable_guess` 与 `tessellateBezierPatch_guess` 后续已由
`MP-R14-BEZIER-BASIS-TESSELLATION` 闭合，包括cache key、vector-of-vector布局、division边界、
16点权重读取、输出顺序和异常owner。下一步继续完整root/body闭包审计。
