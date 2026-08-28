# LayerGetter Array getters 与内部 Items 容器（四参考二进制，2026-08-26）

## 1. 范围

本纵切面闭合 LayerGetter 的 `coord`、`mtx`、`color`、`bezierPatch` 四个返回
Array 的 getter，包括元素顺序/类型、float→double 与 uint32→int64 转换、临时
Variant owner，以及 TJS Array native `Items` 在四个 ABI/STL 组合下的 block/map
实现。`vtx` 的 Dictionary owner 流另见独立报告。

## 2. 四端映射

| getter | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `coord` | `LayerGetter_getCoord_guess@0x6994B0` | `...@0x574A7C` | `...@0x1000F8764` | `...@0xF54A4` |
| `mtx` | `LayerGetter_getMtx_guess@0x6996DC` | `...@0x574BC8` | `...@0x1000F88A4` | `...@0xF5654` |
| `color` | `LayerGetter_getColor_guess@0x699A88` | `...@0x574D64` | `...@0x1000F8AB8` | `...@0xF58FC` |
| `bezierPatch` | `LayerGetter_getBezierPatch_guess@0x699D90` | `...@0x574E80` | `...@0x1000F8B80` | `...@0xF5A14` |

16 个 getter 均在本轮 fresh decompile。还 fresh decompile 了四端 real append、
float-promotion append、integer append/grow helper，并复用本轮已闭合的
`createTJSArrayWithItems_guess` 与通用 Variant copy/destructor 证据。

## 3. 共同源码伪代码

```text
coord(node):
    out = fresh TJS Array + borrowed native Items
    Items.emplace_back(real(node.eval.x))
    Items.emplace_back(real(node.eval.y))
    Items.emplace_back(real(node.eval.z))
    return Variant CopyRef(out); destroy local out

mtx(node):
    out = fresh TJS Array + borrowed native Items
    Items.emplace_back(real(node.matrix[0]))
    Items.emplace_back(real(node.matrix[1]))
    Items.emplace_back(real(node.matrix[2]))
    Items.emplace_back(real(node.matrix[3]))
    return Variant CopyRef(out); destroy local out

color(node):
    out = fresh TJS Array + borrowed native Items
    for i in 0..3:
        packed = load_uint32(node.packedColorBytes + 4*i)
        Items.emplace_back(integer(uint64(packed)))
    return Variant CopyRef(out); destroy local out

bezierPatch(node):
    if node.meshType != 1:
        return Void
    out = fresh TJS Array + borrowed native Items
    for each {float x, float y} in node.meshControlPoints:
        Items.emplace_back(real(double(x)))
        Items.emplace_back(real(double(y)))
    return Variant CopyRef(out); destroy local out
```

所有属性都读取 live node；没有 facade/node null guard。每次调用创建不同的 TJS Array，
不会缓存或返回 node 内部 Array。Array native `Items` 是从新 Array 借用的容器地址，
其生命由 Array dispatch/native instance 持有，不被 LayerGetter 单独 Release。

## 4. 元素边界

### 4.1 `coord`

精确顺序是 x、y、z，三个元素都是 TJS real（type tag 5），直接复制三段 binary64
bit pattern。没有 float 缩窄、clamp、finite 检查或坐标模式转换。

### 4.2 `mtx`

精确顺序是原始存储中的四个连续 double：`m11,m12,m21,m22`。getter 不转置、不求逆、
不把它扩成 3x3/4x4，也不附加 translation。四端字段都在 node 的早期区域，而不是
紧邻末尾 evaluated x/y/angle block；这是原始 source member order 的重要证据。

### 4.3 `color`

每项从四个连续 32-bit word 读取。所有目标都是 little-endian；机器码把 32-bit
load 零扩展为 Variant 的 64-bit integer payload，并设置 type tag 4。因此
`0x89abcdef` 返回正的 `2309737967`，不是负的 signed int32；本地先 `memcpy` 到
`uint32_t` 再 cast 到 `tjs_int64` 精确保留这一点。

### 4.4 `bezierPatch`

- `meshType != 1`：只把隐藏返回 Variant 的 type tag 写成 Void，不创建 Array；
- `meshType == 1` 且 vector 为空：返回一个新建的空 Array，不是 Void；
- 非空：按 vector 顺序把每个 `{float x,float y}` 展平成 `x0,y0,x1,y1,...`；
- 每个 float 在装入 type-5 Variant 时按目标 IEEE 规则提升为 double，保留 NaN、Inf、
  signed zero 和 float 已有 payload；没有归一化。

循环依据 live vector 的 begin/end 差除以 8 得到 `MeshPoint` 数量，元素 stride 为 8。

## 5. TJS Array `Items` 的真实内部容器

元素 Variant 的物理尺寸在当前参考 ABI 中为：LP64 `20` 字节，ILP32 `12` 字节。
Android 与 iOS 使用两套不同 deque 实现，但源语义都是在末尾原位构造一个 Variant。

### 5.1 Android：libstdc++ 风格 segmented deque

| 目标 | 元素字节 | 每 block 元素数 | 实际 block 分配字节 |
|---|---:|---:|---:|
| Android arm64 | 20 | 25 | 500 (`0x1f4`) |
| Android armv7 | 12 | 42 | 504 (`0x1f8`) |

正常 append 比较 `finish.cur` 与 `finish.last - element_size`；未到最后一个可用 slot 时：

1. 在 `finish.cur` 写 payload；
2. 写 type tag 5 或 4；
3. `finish.cur += element_size`。

边界 append 先确认 deque map 尾部至少还有两个指针 slot；不足时扩/搬 map，然后在
`finish.node + 1` 分配新 block。当前元素仍写到旧 block 的最后可用位置，随后把
finish 的 node/first/cur/last 切到新 block。Android arm64 real grow helper 为
`0x6DFC90`，float-promotion grow 为 `0x684F98`；armv7 对应 `0x5A099C` 和
float normal/slow 路径 `0x5545C8 -> 0x5668FC`。integer grow 使用同一 map/block
规则（armv7 `0x5A0A2C`；arm64 在 color getter 中内联）。

### 5.2 iOS：libc++ 风格 block map + absolute start/size

| 目标 | 元素字节 | 每 block 元素数 | 一个满 block payload |
|---|---:|---:|---:|
| iOS arm64 | 20 | 204 (`0xcc`) | 4080 |
| iOS armv7 | 12 | 341 (`0x155`) | 4092 |

container 保存 block-pointer map、absolute start 和 size。append 的目标为：

```text
absolute = start + size
block     = absolute / blockElementCount
slot      = absolute % blockElementCount
address   = map[block] + slot * elementSize
```

若 `start + size` 已到 map 表示的最后容量，先调用 deque grow helper，再重新取 map、
start 和 size。构造完成后只递增 size。real helpers 是
`0x1000FAED8 / 0xF7F90`，float-promotion helpers 是
`0x1001210EC / 0x11FEE4`，integer helpers 是
`0x100122C08 / 0x121C1E`。

这些 STL 差异属于容器实现/ABI disposition，portable 代码不应硬编码 block 数字；
但覆盖审计必须记录它们，因为越界、分配时点和异常前已经提交的元素前缀由这些规则
决定。

## 6. Variant owner 流

`createTJSArrayWithItems_guess` 产生一个局部 object Variant，并保留 Array dispatch 的
Object/ObjThis 所有权；同时返回/保存 native `Items` 的 borrowed pointer。每个 scalar
append 就地构造无外部 owner 的 integer/real Variant。完成后：

1. 通用 Variant copy-constructor 把局部 Array Variant复制到隐藏返回槽；object
   Variant 对 Object 与 ObjThis 各 AddRef 一次；
2. 局部 Variant 析构对两者各 Release 一次；
3. 返回槽成为 Array 的唯一当前返回 owner，Items 继续随 Array 存活。

如果 Array factory 失败/返回不兼容对象，具体边界由已闭合的
`createTJSArrayWithItems_guess` helper 决定；这些 getter 不加第二层检查。

## 7. 四端字段坐标

| 逻辑字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| coord x/y/z | `1512/1520/1528` | `1272/1280/1288` | `1528/1536/1544` | `1240/1248/1256` |
| matrix[0..3] | `120..144` | `104..128` | `120..144` | `104..128` |
| packed colors | `100..112` | `84..96` | `100..112` | `84..96` |
| meshType gate | `2000` | `1720` | `2016` | `1684` |
| mesh vector begin/end | `2024/2032` | `1740/1744` | `2040/2048` | `1704/1708` |

当前 portable `MotionNode.h` 把 matrix 放在 `AccumulatedState`、把 packed colors 和
mesh vector 按逻辑区域分组；reference 的物理 source order 显示它们位于截然不同的
声明区域。这不影响通过字段名表达的 getter 值，但意味着完整源结构尚未 1:1。必须和
构造/复制/析构证据一起恢复声明顺序，不能在本切面局部移动 owner 成员。

## 8. 本地逐行对照

`PlayerLayerQuery.cpp` 当前：

- `getCoord` 按 x/y/z 追加三个 double；
- `getMtx` 按 m11/m12/m21/m22 追加四个 double；
- `getColor` 用 `memcpy` 读取四个 `uint32_t` 并零扩展到 `tjs_int64`；
- `getBezierPatch` 先精确检查 `meshType != 1`，随后按 vector 顺序把 float x/y
  提升为 double；
- 四者都使用 `createTJSArrayWithItems_guess` 的 native Items 快路径，没有脚本级
  `PropSet` 循环。

语义逐项一致，本纵切面无需修改运行 C++。

## 9. 异常清理证据边界

正常 owner 流和每次 append 的前缀提交点已经四端闭合。Android arm64 的 landing
pad、Android armv7 `.ARM.extab`、iOS arm64 LSDA 与 iOS armv7 SjLj 对每一个 block
grow/append call-site 的精确 cleanup frontier 尚未全部展开。源码 RAII 会在异常时
析构局部 Array Variant，但在缺少完整 EH 表证据前，覆盖账本将“每个调用点究竟保留
多少已追加元素、是否还有临时 float/integer Variant cleanup”单列为
`EVIDENCE_BLOCKED`，不把正常路径报告冒充异常路径已闭合。

## 10. 2026-08-27 后续边界说明

`motionplayer_tjs_array_dictionary_exception_frontiers_four_binary_2026-08-27.md` 已闭合
底层 `std::deque<tTJSVariant>` 的四端 map/block reserve、libstdc++ map-before-block
提交、libc++ split-buffer staging，以及六组 Player caller 的精确 EH 矩阵；因此
`MP-C10-TJS-ARRAY-ITEMS` 已升级为 `IMPLEMENTED`。

这一步本身没有自动关闭四组 LayerGetter call-site；随后
`motionplayer_layergetter_quad_exception_frontiers_four_binary_2026-08-27.md` 又逐体展开
20 个 getter body、Android arm64 landing、五个 iOS arm64 LSDA-only cold cleanup、五个
iOS armv7 SjLj cleanup与 Android armv7无本帧 cleanup disposition。因此上层 scalar/
nested container EH也已闭合，`MP-L10-LAYERGETTER-ARRAY-EH` 现为 `IMPLEMENTED`。
