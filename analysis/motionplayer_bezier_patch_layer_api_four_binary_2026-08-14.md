# Motionplayer `BezierPatch` Layer API、静态生命周期与逆映射四参考恢复

日期：2026-08-14

本纵切面只使用 `reference/binaries/` 中四个当前参考目标的新反编译、反汇编、交叉引用和
ASCII/UTF-16LE/UTF-32LE 字节检索结果。旧 `libkrkr2.so` 地址和现有移植注释不作为证据。

## 结论概览

四端都存在一个原生名精确为 `BezierPatch`、附加到脚本 `Layer` 类的无状态 ncbind 类。它按
固定顺序提供八个方法：

1. `affinePatch`
2. `translatePatch`
3. `affineTranslatePatch`
4. `calcPatchBounds`
5. `calcMeshBounds`
6. `calcBezierPatch`
7. `calcBezierPatchList`
8. `reverseCalcBezierPatch`

它不是现有 per-Layer native adaptor 的八个实例方法，也不是八个互不相关的
`NCB_ATTACH_FUNCTION` 注册器。回调都不读取 Layer owner；原始结构与
`NCB_ATTACH_CLASS(BezierPatch, Layer)` 加八个静态 `NCB_METHOD` 一致。

普通 IDA 字符串搜索没有找到这些名字，因为参考镜像保存的是宽字面量。对 UTF-16LE 字节序列
重搜后，四个目标对每个方法名都得到唯一注册命中。`BezierPatch` 本身还会作为
`calcBezierPatch*` 的子串出现；只有带代码交叉引用的独立宽串才是 native class 名。

## 四端函数映射

| 恢复语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| TU 静态初始化 bundle | `0x42F1F8` | `0x3016E8` | `0x10014FC74` | `0x151C98` |
| 八方法注册 | `0x6A195C` | `0x578C48` | `0x1000FE1F0` | `0xFB146` |
| `affinePatch` | `0x6A1D40` | `0x578D90` | `0x1000FE308` | `0xFB244` |
| `translatePatch` | `0x6A2048` | `0x578F28` | `0x1000FE4B4` | `0xFB450` |
| `affineTranslatePatch` | `0x6A2328` | `0x5790B0` | `0x1000FE640` | `0xFB64C` |
| `calcPatchBounds` | `0x6A264C` | `0x579258` | `0x1000FE804` | `0xFB868` |
| `calcMeshBounds` | `0x6A2A04` | `0x5794F8` | `0x1000FEAB8` | `0xFBBDC` |
| `calcBezierPatch` | `0x6A2D6C` | `0x5797A0` | `0x1000FEE38` | `0xFC014` |
| `calcBezierPatchList` | `0x6A3230` | `0x579A18` | `0x1000FF134` | `0xFC360` |
| `reverseCalcBezierPatch` | `0x6A3874` | `0x579D48` | `0x1000FF508` | `0xFC7A4` |
| inclusive point-in-triangle | 内联于逆映射 | `0x59A300` | `0x100127EF8` | `0x1272C4` |
| 逆仿射三角形映射 | `0x6D8544` | `0x59A400` | `0x100128038` | `0x1273E4` |

Android arm64 的自动分析最初把 `calcPatchBounds`、`calcMeshBounds` 和
`calcBezierPatch` 错合成一个函数。三个独立回调 data reference、三个标准 prologue 和其他三端
边界共同给出精确区间：`0x6A264C..0x6A2A04`、`0x6A2A04..0x6A2D6C`、
`0x6A2D6C..0x6A3230`。恢复 IDB 已先重建整个指令区间，再按上述边界建立三个函数，避免在未
完整定义代码时产生伪 `JUMPOUT`。

## 静态对象、容器 ABI 与退出顺序

同一初始化 bundle 的源码顺序在四端完全一致：

1. POD `float unitBezierPatchQuad[8] = {0,0,1,0,1,1,0,1}`；
2. `std::map<int, std::vector<std::vector<double>>>` cubic-basis cache，随后注册其析构；
3. `std::vector` default 4×4 Bezier patch points，随后注册其析构；
4. `BezierPatch`→`Layer` ncbind registration state 及其 `std::function`/注册对象析构；
5. 后续 `Motion` registration state。

| 对象 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| unit quad | `0x1AB50B0` | `0x1111610` | `0x101B69598` | `0x187D2E0` |
| basis map | `0x1AB50D0` | `0x1111630` | `0x101B695B8` | `0x187D300` |
| default points vector | `0x1AB5108` | `0x1111648` | `0x101B695D0` | `0x187D30C` |

没有 `__cxa_guard`，所以 basis cache 不是函数内 magic static。Android 使用 libstdc++ map
record（64 位 48 字节、32 位 24 字节）；iOS 使用 libc++ tree/map record（64 位 24 字节、
32 位 12 字节）。

iOS 的 map 节点清理明确执行 left subtree→right subtree→mapped
`vector<vector<double>>`→node delete；外层 vector 按 24/12 字节 inner-vector record 从后往前
析构，每个 inner `vector<double>` 再释放自己的 backing allocation。Android 把 root 交给
libstdc++ `_M_erase`。default points 是 8 字节 float-pair 元素 vector，析构只需释放 backing
storage。

由于析构按 `__cxa_atexit` 注册逆序执行，退出时先销毁更晚的 binding state，再销毁 default
points，最后销毁 basis map。移植先前把 map 和 default points 定义在不同 translation unit，
无法保证这一相对顺序；现已把 unit quad、map、default points 按原顺序放回同一个主注册 TU。

## 平面数组的共同读取协议

除 `calcMeshBounds` 的固定 parser 外，其余 flat-array 路径都通过 `ncbPropAccessor`：

- `GetCount()` 的结果立即按 32 位无符号数使用；
- index 也以 32 位 word 每次加二，比较是 unsigned；
- 每个坐标先用 `TJS_MEMBERMUSTEXIST` 做一次 probe，成功后再执行第二次真实读取和 Real 转换；
- probe 失败贡献 `0.0`；
- 奇数 count 仍输出一个完整点，缺少的 `y`/`v` 为零；
- custom object 返回负 count 时会变成巨大的 unsigned iteration domain；`UINT32_MAX` 还会因
  `index += 2` 回绕形成不终止循环。

这个协议不能简化成先复制 `count/2` 个合法点，也不能把失败 probe 与读取合成一次调用，因为
两次 property access 对自定义/重入对象是可观察的。

## 三个变换方法

三个方法都先创建并发布结果 Array owner，然后创建输入 accessor；输出始终是平面数值序列：

```cpp
affinePatch:          x' = x*m11 + y*m21
                      y' = x*m12 + y*m22
translatePatch:       x' = x + dx
                      y' = y + dy
affineTranslatePatch: x' = x*m11 + y*m21 + dx
                      y' = x*m12 + y*m22 + dy
```

没有矩阵对象、嵌套 point Array、长度偶数检查或有限数检查。

## 两个 bounds 方法

`calcPatchBounds` 将 `left/top` 初始化为 `DBL_MAX`，将 `right/bottom` 初始化为
`-DBL_MAX`，再按严格 `<`/`>` 更新。NaN 坐标不会更新任何 extrema。结果 Dictionary 的写入
顺序精确为 `left, top, right, bottom, width, height`，所有写入使用
`TJS_MEMBERENSURE` 和六个共享 member-hint slot。空输入保留 sentinel，因此 width/height 的
`-DBL_MAX - DBL_MAX` 通常下溢为负无穷。

`calcMeshBounds` 的差异是：

- 先调用共享 parser/tessellator，division 固定为 10×10；
- parser 不读 count，也不 probe，直接转换恰好 32 个控制坐标；
- basis table 生成 121 个 row-major mesh points，再对它们求 bounds；
- parse 完成后又构造并保留一个没有参与 bounds 计算的输入 accessor，使第二次对象转换和
  AddRef/Release 仍可观察；
- Dictionary 写入存在稳定的原版 quirk：`left` 用同一值和同一 hint 连续写两次，再写其余五项。

## 正向 Bezier 计算与原版未初始化行为

`calcBezierPatch(patch,u,v)` 和 `calcBezierPatchList(patch,uvFlat)` 都先用 probe/零回退读取恰好
16 个控制点。它们不调用静态 basis cache，而是在每个求值点本地构造两组 cubic Bernstein
系数，然后按控制点 row-major 顺序执行：

```cpp
weight = basisV[controlIndex / 4] * basisU[controlIndex % 4];
point.x += weight * controls[controlIndex].x;
point.y += weight * controls[controlIndex].y;
```

关键点是 `point` 在四个目标中都没有零初始化。Android/iOS、32/64 位生成物分别让第一个
累加器从不同的复用 basis 寄存器或未写栈槽开始；第二个坐标也有未定义起点。这只能由类似
`tTVPPointD point;` 后直接 `+=` 的原始源码解释，不是单端 Hex-Rays 变量恢复错误。

移植因此保留了源码级未定义行为并写明原因。它无法保证 Web 编译器恰好复现某一原生 ABI 的
偶然残值；把它改成 `{0,0}` 虽能得到数学正确值，却不是四个参考二进制背后的源码边界。

`calcBezierPatch` 返回嵌套层级为一的 `[x,y]` Array；`calcBezierPatchList` 对 UV flat Array
使用上述 unsigned pair loop，并把每个结果继续平铺成 `[x0,y0,x1,y1,...]`。

## `reverseCalcBezierPatch`

返回 Variant 初始为 Void。函数先调用 `calcPatchBounds`，再按共享 hint 读取四个边界。进入
搜索的门是正向 ordered conjunction：

```cpp
top <= targetY && left <= targetX &&
right >= targetX && bottom >= targetY
```

这既包含边界，又会在任一 bound/target 为 NaN 时拒绝搜索；不能改写为仅由四个 `>`/`<`
组成的 early-reject OR，否则 NaN 行为相反。

通过 gate 后再用固定 10×10 parser/tessellator。cell 遍历顺序为 row `9..0`、column `9..0`，
每格依次测试：

1. 几何 `(topRight, bottomLeft, bottomRight)`，对应 UV
   `((x+1)/10,y/10)`, `(x/10,(y+1)/10)`, `((x+1)/10,(y+1)/10)`；
2. 几何 `(topLeft, topRight, bottomLeft)`，对应 UV
   `(x/10,y/10)`, `((x+1)/10,y/10)`, `(x/10,(y+1)/10)`。

point-in-triangle 先由 orientation `< 0` 选择 `-1`，否则选择 `+1`；任一有符号 edge line
`> 0` 才算 outside。因此边界被接受，NaN edge comparison 则会落入 inside。AArch64 把这段
完全内联，其他三端保留独立 helper。

逆仿射 helper 只在 determinant **精确等于** `0.0` 时返回 false 且不改变结果；NaN
determinant 不等于零，所以会创建 `[NaN,NaN]` 并报告成功。若第一三角形声称包含 target 但
逆映射退化，原版不会再测试同 cell 的第二三角形，而是直接继续前一个 cell。第一个成功的
非退化映射立即返回 `[u,v]`；全部失败则保持 Void。

## 本地实现与 IDB 变化

- 新增原生名精确为 `BezierPatch` 的 stateless Layer attach class，并按参考顺序注册八个静态
  方法；
- 实现 unsigned flat-array probe/read 协议、三个变换、两个 bounds、正向单点/列表求值和
  固定 10×10 逆映射；
- 保留 `calcMeshBounds` 重复 `left` PropSet、第二个 unused accessor、空 bounds sentinel、
  NaN ordered gate、三角形退化继续规则和两个正向求值函数的未初始化 accumulator；
- 将 unit quad、basis map 和 default points 恢复到同一 TU 的原始初始化顺序；
- 四份 recovery IDB 均已修复/核验函数边界，写入 callback/helper/init/destructor 语义名、
  函数注释、unit-quad array 类型、basis-map/hint 数据名，并保存。

## 验证

- 完整 motionplayer 测试翻译单元语法检查通过；唯一输出是仓库既有 `_tss` literal-operator
  deprecated warning；
- `cmake --build --preset "Web Debug Build"` 完整编译与最终 WebAssembly/HTML 链接通过；
- `git diff --check` 在本纵切面完成后执行；换行提示若出现，只按既有工作树状态记录，不进行
  无关格式重写。

## 仍保留的不确定性

- `calcBezierPatch*` 的源码级未初始化行为已确定，但每次调用具体读到什么残值本来就不受 C++
  语言保证，不能把任一 ABI 的偶然寄存器值定义成跨平台契约；
- cubic basis 表达式的乘法结合按四端指令尽量恢复，但优化器在普通有限值之外仍可能重排；
- 分配失败、property getter/setter 抛出或用户对象重入时，标准库和 TJS runtime 的平台异常类型
  可以不同；共同可确认的是已经发生的 Array/Dictionary publication 与逐步写入不回滚。
