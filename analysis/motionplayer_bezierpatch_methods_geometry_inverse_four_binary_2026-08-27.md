# `BezierPatch` 八个几何方法四二进制联合恢复

日期：2026-08-27

## 1. 本 slice 的闭合范围

本报告闭合直接附加到脚本 `Layer` 的 stateless `BezierPatch` helper 的八个
callback body：

- 三个 flat point transform：`affinePatch`、`translatePatch`、
  `affineTranslatePatch`；
- 两个 bounds：`calcPatchBounds`、固定 10×10 tessellation 的
  `calcMeshBounds`；
- 单点/列表 bicubic evaluation：`calcBezierPatch`、`calcBezierPatchList`；
- 固定 10×10 网格上的反向 triangle/affine 求解：
  `reverseCalcBezierPatch`。

本轮同时闭合 indexed TJS 读取 helper、fresh Array/Dictionary owner、16 点 control
vector、121 点 tessellated vector、Bezier basis 运算顺序、bounds hint、反向扫描顺序、
退化/NaN/奇数/负 count/异常 partial publication 边界。四个参考二进制共同构成权威。

## 2. callback 地址与 fresh 指令覆盖

| callback | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `affinePatch` | `0x6A1D40`，193 | `0x578D90`，115 | `0x1000FE308`，81 | `0xFB244`，134 |
| `translatePatch` | `0x6A2048`，183 | `0x578F28`，108 | `0x1000FE4B4`，73 | `0xFB450`，130 |
| `affineTranslatePatch` | `0x6A2328`，200 | `0x5790B0`，119 | `0x1000FE640`，87 | `0xFB64C`，138 |
| `calcPatchBounds` | `0x6A264C`，236 条内部范围 | `0x579258`，180 | `0x1000FE804`，127 | `0xFB868`，233 |
| `calcMeshBounds` | `0x6A2A04`，215 条内部范围 | `0x5794F8`，181 | `0x1000FEAB8`，151 | `0xFBBDC`，258 |
| `calcBezierPatch` | `0x6A2D6C`，302 条内部范围 | `0x5797A0`，177 | `0x1000FEE38`，157 | `0xFC014`，222 |
| `calcBezierPatchList` | `0x6A3230`，400 | `0x579A18`，228 | `0x1000FF134`，192 | `0xFC360`，278 |
| `reverseCalcBezierPatch` | `0x6A3874`，348 | `0x579D48`，252 | `0x1000FF508`，240 | `0xFC7A4`，346 |

Android arm64 的 bounds/mesh/evaluate 三个入口仍保留在同一个 IDA 函数范围：
`0x6A264C` 总计 753 条，从 `0x6A2A04` 起余 517 条，从 `0x6A2D6C` 起余
302 条。因此三个源级 slice 分别是 236/215/302 条；本轮按地址连续读取
`0x6A264C..0x6A2A04` 的 236 条，并完整读取后两个 registrar 保存的内部入口，没有
创建重叠函数。

四端 32 个 callback 都已取得 fresh decompile/disassembly；下列共享 helper 也完成 fresh
完整读取：

| helper | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| probe-then-Real indexed read | callback 内联 | `0x4BFBD8`，38 | `0x1000FBB5C`，23 | `0xF8BC0`，24 |
| parse + tessellate | `0x69E9F8`，219 | `0x577430`，185 | `0x1000FBFF0`，146 | `0xF9054`，218 |
| point-in-triangle | reverse callback 内联 | `0x59A300`，77 | `0x100127EF8`，79 | `0x1272C4`，82 |
| reverse affine triangle | `0x6D8544`，142 | `0x59A400`，110 | `0x100128038`，101 | `0x1273E4`，147 |

## 3. flat transform 的共同伪代码

```text
readCoordinate(accessor, index):
    if !accessor.HasValue(index):       # MEMBERMUSTEXIST probe
        return 0.0
    return accessor[index] as Real      # 第二次 indexed dispatch

transform(flatPoints, operation):
    result = fresh native Array
    points = retained accessor(flatPoints)
    countBits = uint32(points.GetCount())
    for indexBits = 0; indexBits < countBits; indexBits += 2:
        x = readCoordinate(points, int32bits(indexBits))
        y = readCoordinate(points, int32bits(indexBits + 1))
        append Real(operation.x(x, y))
        append Real(operation.y(x, y))
    return result
```

三个 operation 精确为：

```text
affine:          x*m11 + y*m21,       x*m12 + y*m22
translate:       x + offsetX,         y + offsetY
affineTranslate: x*m11 + y*m21 + ox,  x*m12 + y*m22 + oy
```

`GetCount` 只调用一次并按低 32 位无符号解释。count 为 0 返回 fresh 空 Array；奇数
count 的最后一个 y 访问越过 nominal count，probe 失败后贡献 0；自定义 dispatch 返回负
count 时会转成巨大无符号上界，并持续 indexed dispatch 直到 32 位 index wrap/异常/外部
终止。代码没有“偶数长度”校验，也不先把输入 materialize 成 `std::vector`。

Array 使用 native `tTJSArrayNI::Items` 直接 append Real Variant。输入 accessor 持有
dispatch owner；结果 Array 在进入循环前已经构造/发布到 hidden return owner。indexed
conversion 或 vector growth 抛出时，已经 append 的前缀不回滚，局部 owner 仍负责最终
Release。

## 4. bounds 字典与严格比较边界

`calcPatchBounds` 使用与 transform 相同的 count/pair/read 规则，并以：

```text
left   = DBL_MAX
top    = DBL_MAX
right  = -DBL_MAX
bottom = -DBL_MAX

for each (x,y):
    if x < left:   left = x
    if y < top:    top = y
    if x > right:  right = x
    if y > bottom: bottom = y
```

四个比较保持分离且严格；NaN 坐标不更新任何 extremum。空输入仍返回 fresh Dictionary，
保留上述四个 sentinel，并计算 `width=right-left`、`height=bottom-top`；溢出到
Infinity/NaN 不 clamp。

Dictionary 的写序为：

```text
left, top, right, bottom, width, height
```

全部 value 是 Real，flags 为 `TJS_MEMBERENSURE`，使用 process-wide geometry hint 六元组。
`calcMeshBounds` 唯一额外缺陷是 `left` 用同一个值和同一个 hint 连续写两次，因此它的
脚本可见顺序是 `left, left, top, right, bottom, width, height`。任一 PropSet 抛出都保留
先前写入的 partial Dictionary；没有事务回滚。

## 5. `calcMeshBounds` 与 parse/tessellate 数据流

`calcMeshBounds` 共同流程为：

```text
control = vector<PointD>()
mesh = vector<PointD>()
parseAndTessellate(flatControlPoints, divX=10, divY=10,
                   control, mesh)

bounds = sentinels
unusedAccessor = retained accessor(flatControlPoints)
for point in mesh:
    includeStrict(bounds, point)
return Dictionary(bounds, duplicateLeft=true)
```

第二个 `unusedAccessor` 的确在四端构造并保留到 callback 尾；即使 bounds 循环只读
`mesh`，它仍带来一次可见的 input Variant conversion/AddRef/Release，不能删成“无用代码”。

parse/tessellate helper 的源级结构为两个 `std::vector<tTVPPointD>`：

1. control vector reserve 16；
2. mesh reserve 的输入是在 32 位有符号乘法中计算
   `(divX+1)*(divY+1)`，再按平台 `size_t` 扩宽；wrap 成负数会变成巨大 reserve；
3. 对 input 直接读取恰好 32 个 Real 坐标，不调用 GetCount，也不做
   MEMBERMUSTEXIST probe；缺失/错误由严格 conversion 自然抛出；
4. 分别取得 divX/divY 的进程级 cubic basis cache；
5. `divY<0` 在两个 basis lookup 之后返回；每个非负维度按闭区间
   `0..division` 迭代；
6. 每个输出点显式从 `{0,0}` 开始，按 control index 0..15 顺序执行
   `weight = basisY[y][row] * basisX[x][column]` 和两个逐项加法。

Android 使用 libstdc++ vector helper，iOS 使用 libc++ 形态；节点/容量指针和 cleanup
landing 不同，源级容器、元素顺序和 owner 一致。

## 6. 单点/列表 bicubic evaluation 与原版 UB

`calcBezierPatch` 和 `calcBezierPatchList` 都先通过 probe-then-read helper读取恰好
16 个 control point，不检查 input count。单点版返回 flat `[x,y]` Array；列表版对
parameter Array 使用无符号 count/pair 规则，每个 `[u,v]` 追加两个 Real，奇数尾部
`v=0`。

四端共同的 basis 多项式值是：

```text
B0(t) = (1-t) * ((1-t) * (1-t))
B1(t) = ((1-t) * (1-t)) * t * 3
B2(t) = (1-t) * t * t * 3
B3(t) = t * t * t
```

但原 callback 展开的乘法结合顺序不对称，四端指令一致：

- horizontal/U 的 B2：`(((1-u) * u) * u) * 3`；
- vertical/V 的 B2：`(((1-v) * 3) * v) * v`；
- list 版由优化后的同构展开表现为
  `u * (u * (1-u)) * 3` 与 `v * (v * ((1-v) * 3))`；
- B0/B1/B3 仍保持各 callback 的逐次 scalar multiply；没有 `pow`、FMA 或 clamp。

对普通数学实数这些表达式等价，但 IEEE-754 有限值已经能产生 1 ULP 差异。例如
`t=0.4721359549995796` 时 horizontal native order 的 B2 bits 为
`0x3FD69796CAA1A980`，此前共用 vertical order 得到
`0x3FD69796CAA1A97F`。因此本地不能用一个统一的 B2 结合顺序同时生成 U/V。

evaluation 依次按 control index 0..15 计算
`vertical[index/4] * horizontal[index%4]`，再分别累加 x/y。原源码级
`tTVPPointD result;` 没有 value initialization：四端优化后一个 accumulator 常复用
basis 中间寄存器，另一个读取未初始化 stack/register residue，具体残值随 ABI/优化变化。
这是参考源的原版 undefined behavior，不能“修复”为 `{0,0}`，也不能编造稳定 fixture。

## 7. `reverseCalcBezierPatch` 共同伪代码

```text
result = Void
bounds = calcPatchBounds(flatControlPoints)  # control-point bounds
left/top/right/bottom = strict Real property reads
if !(top <= targetY && left <= targetX &&
     right >= targetX && bottom >= targetY):
    return Void                              # unordered/NaN 也拒绝

control, mesh = parseAndTessellate(input, 10, 10)
for row = 9 downto 0:
  for column = 9 downto 0:
    TL, TR, BL, BR = four points in 11-wide mesh
    u0=column/10; u1=(column+1)/10
    v0=row/10;    v1=(row+1)/10

    if pointInTriangle(TR, BL, BR, target):
        if reverseAffine(TR, BL, BR,
                         (u1,v0), (u0,v1), (u1,v1), target):
            return [u,v]
        continue              # first triangle degenerate: skip cell's second

    if pointInTriangle(TL, TR, BL, target) &&
       reverseAffine(TL, TR, BL,
                     (u0,v0), (u1,v0), (u0,v1), target):
        return [u,v]
return Void
```

bounds gate 在 AArch64 使用 `FCMP + B.HI/B.LT`；`B.HI` 包含 unordered，因此虽然
部分 Hex-Rays 输出显示成 negated `>` 链，四端实际都拒绝 target/bounds NaN，本地正向
四比较 gate 才是可移植等价写法。

`pointInTriangle` 以三点 orientation 选择 sign，三条边只在
`sign*line > 0` 时判 outside；边界在线上算 inside，line NaN 也不会触发 outside。
`reverseAffine` 只拒绝 determinant 精确等于 `+0/-0`；NaN determinant 会继续除法并
把 NaN 发布到结果。它在成功时创建 fresh `[mappedU,mappedV]` Array。

扫描顺序是右下 cell 到左上 cell，且每格先测 `TR-BL-BR`。共享边/重叠/折叠网格会因
这个顺序选择第一个成功三角形；第一三角形命中但 determinant 为零时不会再试同格第二
三角形。

## 8. 对象、容器与异常生命周期

- `BezierPatch` 本身无实例字段、constructor 或 per-Layer native owner；八个 callback
  只拥有调用期 Variant/accessor/vector/Array/Dictionary 临时对象。
- 输入 Variant 是 by-value owner；accessor 再持有其 dispatch，销毁时 Release。
- transform/evaluate result 是 fresh Array，Items 是 borrowed native vector pointer；
  append 过程中 Array Variant 是唯一 owner。
- control/mesh 是连续 `std::vector<PointD>`，正常和异常都按 mesh 后 control 的逆序
  释放；Android/iOS allocator/EH 形态不同。
- bounds Dictionary constructor 返回 dispatch 后立即由 Variant 接管并平衡初始 Release；
  property 写失败保留已提交前缀直到 owner unwind。
- reverse 的 bounds Dictionary/accessor 持有到函数尾；通过 bounds gate 后才分配两个
  point vector。找到结果后先释放 vector，再释放 bounds accessor/Variant。
- 所有脚本状态码处理都沿 ncb helper 原行为；本 slice 不添加 null、count、finite、
  determinant epsilon 或 allocation fallback 检查。

## 9. 宽字段名原始字节核验

Hex-Rays 把 `right/bottom` 缩成 `"r"/"b"`，Android arm64 的 `height` 还显示为
相邻诊断文本。UTF-16LE raw-byte 搜索已在四端对
`left/top/right/bottom/width/height` 全部读至 `cursor.done=true`；callback 实际指针为：

| 平台 | left | top | right | bottom | width | height |
|---|---:|---:|---:|---:|---:|---:|
| Android arm64 | `0x14D6F9A` | `0x14CD548` | `0x14C175E` | `0x14C176A` | `0x14C72EE` | `0x14E12FA` |
| Android armv7 | `0xD75FD2` | `0xD7F6C6` | `0xD84EEA` | `0xD780EC` | `0xD7B808` | `0xD8E3C4` |
| iOS arm64 | `0x10195B61C` | `0x10195B626` | `0x10195B62E` | `0x10195B63A` | `0x10195B5D8` | `0x10195B5E4` |
| iOS armv7 | `0x174D980` | `0x174D98A` | `0x174D992` | `0x174D99E` | `0x174D93C` | `0x174D948` |

Android arm64 的 left/width 是相邻字面量内部 `+2` 指针，height 是诊断文本内部
`+0x62` 指针；right/bottom 的 IDA data item 只覆盖首字符。完整终止符和其余三端同序
xref 共同证明六个全名。四端 UTF-32LE 都没有本组对应命中。

## 10. 平台差异与共同源结构

- Android arm64 合并三个 callback；其余三端为独立函数。这是函数布局差异。
- 64 位 PointD/vector element 为 16 bytes；32 位同样是两个 double、仍为 16 bytes。
  vector 三指针和 Variant/accessor 大小不同，不应写 ABI padding。
- Android libstdc++ 与 iOS libc++ 的 reserve/grow/delete helper、stack guard 和 EH landing
  不同；源级容器一致。
- probe-then-read 在 Android arm64 内联，其余三端为小 helper；point-in-triangle 也只有
  Android arm64 内联。
- AArch64 会向量化/配对部分 basis multiply，AArch32 使用 VFP scalar 组合；U/V 结合
  顺序、16 项 accumulation、triangle 顺序和结果完全同构。

## 11. 与本地源码和测试逐行对照

本地主要对应：

- `cpp/plugins/motionplayer/MotionLayerExtensions.cpp:46`：unsigned count、probe/read、
  16 control point 和 parse/tessellate；
- `cpp/plugins/motionplayer/MotionLayerExtensions.cpp:195`：strict bounds 与
  Dictionary write/duplicate-left；
- `cpp/plugins/motionplayer/MotionLayerExtensions.cpp:257`：单点/列表 basis 与原版
  uninitialized accumulator；
- `cpp/plugins/motionplayer/MotionLayerExtensions.cpp:291`：triangle admission；
- `cpp/plugins/motionplayer/MotionLayerExtensions.cpp:318`：reverse affine；
- `cpp/plugins/motionplayer/MotionLayerExtensions.cpp:678`：八个 public callback；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:6768`：共享 tessellation basis 的 scalar
  operation order、division 0/negative 和 accumulation；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:6878`：bounds 六字段与 process-wide hint。

逐行对照确认 count、owner、bounds、tessellation、反向求解和所有上述尖锐边界已经匹配。
唯一语义偏差在旧的统一 `cubicBezierWeights_guess`：它把 vertical B2 的
`(1-t)*3*t*t` 结合顺序同时用于 U/V。四端证明 U 必须用
`(1-u)*u*u*3`，且 list 展开有自己的 operand/parenthesis 顺序。本 slice 将 weight 生成
拆成 single/list 的 horizontal/vertical 四个小源级 helper，并让两个 callback 显式选择；
不初始化原版 UB accumulator。

## 12. 状态与验证边界

完成本地 weight-order 修正后，`BezierPatch` 八行从
`BODY_PENDING_SEPARATE_SLICE` 提升为 `IMPLEMENTED`。NCB pending 将从 87 降到 79，
`IMPLEMENTED` 从 43 增为 51；注册面仍为 316/316、`UNMAPPED=0`。

本轮验证包括四端 fresh callback/helper decompile/disassembly、UTF-16LE raw-byte 搜索、
源码逐行比较、IDB 命名/注释/书签/保存、确定性台账重生成、strict TSV 和
`git diff --check`。当前环境缺少 CMake、Ninja、Emscripten，独立语法检查还受缺失第三方
头文件阻塞，因此不宣称正式 native/Web build 或 unit runtime 已完成。其余 79 个 pending
NCB callback 和完整 root helper/object/container 分母仍待闭合。
