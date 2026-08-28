# Motion cubic Bezier basis cache 与 4×4 patch tessellation 四参考二进制联合恢复

日期：2026-08-27

## 1. 结论

四个参考二进制共享一个进程生命周期、无锁的
`map<int, vector<vector<double>>>` cubic Bezier basis cache，以及一个以该cache计算4×4控制点
patch网格的公共tessellation helper。basis helper有六组native caller；tessellation helper有两个
caller，分别是Player/D3D deep renderer和相邻Layer motion绘制路径。

本地cache布局、miss/hit、四项Bernstein权重、逐点16项累加和输出顺序已经正确。本轮只发现
一项极端源结构偏差：本地在`division + 1`之前先转`size_t`，把`INT_MAX`边界定义化；四端指令
证明原始表达式先做signed-int加一，再转换给`vector::resize`。现已恢复该表达式，并明确保留
Android/iOS arm64优化器在这个C++ UB点上的不同结果。

本 slice闭合basis cache、ordered-map/nested-vector内部数据流、全局生命周期、division边界、
tessellation控制点读取/输出顺序和主要异常owner。公共mesh helper已由
`MP-R14-D3D-MESH-SUBMIT-CELLS`闭合；两者现在共同接通deep renderer的Bezier路径。

## 2. 四端函数与完整指令

### 2.1 basis cache helper

| 平台 | helper | 完整指令数 |
|---|---:|---:|
| Android arm64 | `0x69DE30` | 167 |
| Android armv7 | `0x576C7C` | 142 |
| iOS arm64 | `0x1000FB4A8` | 107 |
| iOS armv7 | `0xF854C` | 124 |

四端合计完整读取540条basis指令。共同签名是：

```text
const vector<vector<double>>& cubicBezierBasisTable(int division)
```

### 2.2 patch tessellation helper

| 平台 | helper | 完整指令数 |
|---|---:|---:|
| Android arm64 | `0x6D9138` | 86 |
| Android armv7 | `0x59ABC8` | 120 |
| iOS arm64 | `0x1001289AC` | 84 |
| iOS armv7 | `0x127C6C` | 110 |

四端合计完整读取400条tessellation指令。共同签名是：

```text
vector<Point> tessellateBezierPatch(
    const vector<Point>& controlPoints,
    int divisionX,
    int divisionY)
```

所有八个主函数都fresh decompile，940条指令全部一次或分页完整读取；没有以被截断的伪代码
替代反汇编证据。

## 3. cache全局对象与调用闭包

cache root与global-init root为：

| 平台 | cache root | 初始化bundle |
|---|---:|---:|
| Android arm64 | `0x1AB50D0` | `0x42F1F8` |
| Android armv7 | `0x1111630` | `0x3016E8` |
| iOS arm64 | `0x101B695B8` | `0x10014FC74` |
| iOS armv7 | `0x187D300` | `0x151C98` |

本轮重新读取四个cache root的完整xref集合：除basis helper自身外只有对应global-init bundle；
全局构造/析构顺序已在模块root slice闭合。共同翻译单元顺序是POD unit quad → basis map →
default 4×4 patch point vector → 后续NCB binding state；`__cxa_atexit`逆序销毁后续binding、默认
点vector，最后销毁basis map。map析构递归释放每个node、外层vector storage和每行double
vector storage；没有插件级手工clear、锁或独立owner。

basis helper每端恰好有六个code xref：一组单basis用户、两组同时查X/Y的patch用户、
`drawBezierPatchFrame`和公共tessellation等。tessellation helper每端恰好两个xref：shared D3D
deep renderer与相邻的另一个render/Layer路径。由此排除了“D3D私有cache”或“每Player cache”
结构；所有Player、D3DAdaptor和Layer扩展共享同一进程map。

## 4. ordered-map与nested-vector布局

Android使用libstdc++红黑树header/sentinel；A64 node的int key位于node+32、mapped
`vector<vector<double>>`位于+40，A32对应+16/+20。iOS使用libc++ tree/end sentinel，A64
mapped value同样在node+40，A32在node+20。此处只恢复标准容器类型，不在C++里复制ABI node
padding。

共同hit/miss流程：

```text
it = cache.find(division)
if it != cache.end():
    return it->second

table = cache[division]          // 先发布default空mapped value
table.resize(division + 1)       // signed-int加一后转size_type
if division < 0:
    return table

for index in [0, division]:
    append four basis doubles to table[index]
return table
```

Android arm64把lower_bound/比较和hinted unique insert内联，Android armv7保留`map::operator[]`；
iOS分别调用libc++ tree find/insert helper。四端源级结果一致：miss时map node在resize和row填充
之前已经可见。

cache没有guard、mutex或thread-local隔离。并发首次查询同一/不同key会进入标准容器data race；
重入也可能在持有node/vector引用时改写树。原版不做防御。

## 5. miss异常与cache poisoning

cache hit直接返回mapped vector，不验证outer size、row size或是否完整。miss先插入default node，
再resize，随后逐行各push四次。因此：

- node分配抛异常时没有新key；
- node插入成功、outer resize抛异常时留下空或部分outer vector；
- 某一row push抛异常时，前面row与当前row前缀保留；
- 后续同key调用命中后直接返回半成品，不重试resize或补齐权重。

全局map在进程退出时仍会回收这种半成品。tessellation如果随后索引缺行/缺列，会按原版直接
越界；没有cache修复路径。

## 6. division边界与四端差异

`table.resize(division + 1)`的加法发生在signed int域，之后才转换到size type：

- `division == -1`：size为0，negative gate立即返回空table；
- `division < -1`：负结果转成巨大size，通常在resize抛`length_error`/分配异常；node已发布；
- `division == 0`：outer size为1，唯一循环项计算`0.0/0.0`，四个basis全为NaN；
- 正数N：生成N+1行，每行四项；
- `division == INT_MAX`：signed加一是C++ UB，四端编译结果不完全相同。

INT_MAX的具体机器边界：Android arm64先`ADD W`得到`0x80000000`再sign-extend成64-bit
size，形成接近`2^64`的请求；iOS arm64先sign-extenddivision再做64-bit add，形成
`0x80000000`；两个32位目标都把`0x80000000`作为size_t。它们都会在现实内存条件下失败，
但异常类型/失败阶段不保证相同。这是同一未定义源表达式的编译器差异，不应伪造一个跨平台
统一数学值。

本地原先的`static_cast<size_t>(division) + 1u`不是该源结构，已改回`resize(division + 1)`。

## 7. Bernstein basis标量顺序

每行先计算：

```text
t = double(index) / double(division)
u = 1.0 - t
u2 = u * u
```

再严格按以下离散乘法序列append：

```text
u * (u * u)
(t * u2) * 3.0
(t * (t * u)) * 3.0
t * (t * t)
```

四端指令均保留独立FMUL，没有重结合为幂函数。现有unit case按double bit pattern验证division=3
两行内部权重，并验证division=0四NaN和division=-1空table；本轮逐指令复核后保持这些用例。

## 8. tessellation数据流

helper第一步固定查询`basisX = basis(divisionX)`，第二步查询
`basisY = basis(divisionY)`，然后才建立/填充输出。第二次查询抛异常时，第一个cache条目已保留。

`divisionY < 0`时在两次cache查询之后返回空输出。否则y循环为闭区间`0..divisionY`；每个y
只有在`divisionX >= 0`时才运行闭区间`0..divisionX`的x循环。因此divisionX为负且Y非负时
会遍历空inner loop并返回空vector；不会读取controlPoints。

每个输出点从两个`0.0` accumulator开始，固定执行16次：

```text
weight = basisY[y][controlIndex / 4]
       * basisX[x][controlIndex % 4]
out.x = out.x + weight * controlPoints[controlIndex].x
out.y = out.y + weight * controlPoints[controlIndex].y
```

顺序是y外层、x内层，所以输出是row-major `(divisionY+1) × (divisionX+1)`。四端均发出weight
FMUL、coordinate FMUL、旧accumulator FADD，不使用FMA；x先更新，y随后更新。现有unit case
用高度消去敏感的16点数据按bit pattern验证四个内部输出，并验证0×0 division产生一个NaN点。

当两个division非负时，controlPoints无条件读取索引0..15；不足16点直接越界。cache若因前次
异常半成品而缺行/缺四项也直接越界。没有reserve：result vector按标准growth helper扩容；
输出prefix由调用者的sret vector持有，异常回收依赖各caller/toolchain的unwind状态。basis全局
cache不会rollback。

## 9. 本地改动

- `cubicBezierBasisTable_guess`的outer resize从“先转size_t再加一”恢复为
  `table.resize(division + 1)`，保留原始signed overflow边界；
- cache hit/miss、发布顺序、无锁global owner、四个标量权重、tessellation y/x顺序、16点逐项
  FMUL+FADD均逐行核对后保持不变；
- 现有basis/patch bit-pattern unit case保持不变，不添加会实际申请巨大内存的INT_MAX测试。

## 10. IDB与验证

- 四个IDB已把basis/tessellation主函数命名为`Motion_cubicBezierBasisTable`和
  `Motion_tessellateBezierPatch`，并把四个global root统一标注为
  `motion_cubicBezierBasisCache`；
- 四端函数注释、书签写入并原位保存；
- 四端fresh decompile、540条basis与400条tessellation指令完整读取；basis六caller、
  tessellation两caller和global-init xref集合重新核对；
- coverage 12列、deterministic NCB ledger、Python helper compile与`git diff --check`作为当前
  可用验收；
- 当前环境缺少CMake、Ninja和Emscripten，standalone syntax check被缺失的
  `boost/locale.hpp`阻塞，因此不宣称正式native/Web build或unit tests已运行。

本相邻helper组已闭合。下一步回到完整mapped-callback/root closure审计，按剩余body和container
ledger继续选择下一条root-reachable语义链。
