# Motion.LayerGetter 完整 NCB 注册面、live facade 与边界行为四参考审计（2026-08-14）

## 结论

四份当前参考二进制共同发布同一份 `Motion.LayerGetter`：

- 一个零参数 generated typed constructor；
- constructor 后恰好 29 个 getter-only typed property，顺序与当前 `main.cpp` 一致；
- 没有 setter、ordinary method、constant、raw callback 或 constructor overload；
- `left` 与 `x` 复用同一 native getter target，`top` 与 `y` 复用同一 target；
- 默认 native record 只有一个置 null 的 `MotionNode*`，64 位为 8 字节、32 位为 4 字节；
- typed constructor 忽略全部非负 surplus 参数；一个 Void 参数仍是 ncbind 的空 adaptor shell；
- 正常 Player query 返回的 facade 只借用 live `MotionNode`。script adaptor 负责删除 facade
  wrapper，但从不拥有、析构或删除 `MotionNode`；
- 所有 29 个 getter 都直接解引用 borrowed node，没有 null guard。脚本直接构造的真实 native
  对象可用于验证 descriptor/constructor，但调用任一 getter 会自然进入 null-dereference 边界；
- scalar/string getter 每次从 live node 读取；`coord/mtx/vtx/color/bezierPatch/shape` 每次重新
  materialize Variant/container，`motion/particle` 只复制当前持久 Variant；
- `layerVisible` 精确为 `accumulated.visible && accumulated.active`，不读取 `drawFlag`；
- `angleRad` 的机器级顺序不是简单的 `angle * pi / 180`，而是
  `p = angle * pi; (p + p) / 360`。两式在普通范围数值等价，但前者会掩盖参考实现的中间
  doubling overflow。

本纵切面因此产生两项 production 修正：删除 `drawFlag` 上“被 layerVisible 消费”的旧单目标
注释，并把 `angleRad` 恢复为四端共同的 multiply → double → divide 顺序。新增 class-object
constructor 测试和大有限角度 overflow 测试锁定这两类边界。

## 四端 registrar、class 与 constructor 链

| 角色 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| member registrar | `0x698730` | `0x574628` | `0x1000F81AC` | `0xF4FF8` |
| registrar 大小 | `0xCC4` | `0x298` | `0x4B0` | `0x3EA` |
| class registration wrapper | `0x6FACE8` | `0x5996B0` | `0x100125F74` | `0x1250BC` |
| class-info initialization | `0x6FAE58` | `0x5B65A4` | `0x10014D928` | `0x14F588` |
| constructor descriptor register | registrar inline | `0x5749B8` | `0x1000F865C` | `0xF53E2` |
| constructor Function factory | registrar inline | `0x5A0A78` | `0x10013109C` | `0x12FF04` |
| constructor descriptor install | registrar inline | `0x5A0AD4` | `0x100131124` | `0x130000` |
| constructor `FuncCall` | `0x6DFFAC` | `0x5A0BBC` | `0x100131274` | `0x13016C` |
| native allocate + attach | `0x6E0080` | `0x5A0C4C` | `0x100131314` | `0x1301D8` |

constructor outer Function vtable 与 `FuncCall` slot 为：

| ABI | outer vptr | `FuncCall` slot | slot target |
|---|---:|---:|---:|
| Android ARM64 | `0x1A18B48` | `0x1A18B58` | `0x6DFFAC` |
| Android ARMv7 | `0x10B9FB0` | `0x10B9FB8` | Thumb `0x5A0BBD` |
| iOS ARM64 | `0x101AE0C40` | `0x101AE0C50` | `0x100131274` |
| iOS ARMv7 | `0x18322B0` | `0x18322B8` | Thumb `0x13016D` |

32 位 vtable 中最低位 `1` 是 Thumb ISA tag；本文代码地址统一使用去 tag 的偶数地址。

四端 class-info 路径都创建 native class、注册 class ID/class object，并安装 finalize 与 instance
adaptor 簇。Android ARMv7、iOS ARM64、iOS ARMv7 明确分配一个 4/8 字节 subclass adaptor；
其 deleting method 只销毁这只小 adaptor。Android ARM64 的 class-info/finalize 状态机等价。
这一级同样没有 `MotionNode` ownership。

## 精确 29 项发布顺序与 native target

| # | 脚本名 | typed return | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---:|---|---|---:|---:|---:|---:|
| 1 | `type` | `int` | `0x6993F4` | `0x5749DE` | `0x1000F86AC` | `0xF5408` |
| 2 | `label` | `ttstr` | `0x699400` | `0x5749E4` | `0x1000F86B8` | `0xF540E` |
| 3 | `src` | `ttstr` | `0x699424` | `0x574A04` | `0x1000F86DC` | `0xF542E` |
| 4 | `visible` | `bool` | `0x69945C` | `0x574A32` | `0x1000F8710` | `0xF545C` |
| 5 | `branchVisible` | `bool` | `0x699468` | `0x574A3A` | `0x1000F871C` | `0xF5464` |
| 6 | `layerVisible` | `bool` | `0x699474` | `0x574A42` | `0x1000F8728` | `0xF546C` |
| 7 | `x` | `double` | `0x699498` | `0x574A5A` | `0x1000F874C` | `0xF5484` |
| 8 | `y` | `double` | `0x6994A4` | `0x574A6A` | `0x1000F8758` | `0xF5494` |
| 9 | `left` | `double` | same as `x` | same as `x` | same as `x` | same as `x` |
| 10 | `top` | `double` | same as `y` | same as `y` | same as `y` | same as `y` |
| 11 | `coord` | `Variant` | `0x6994B0` | `0x574A7C` | `0x1000F8764` | `0xF54A4` |
| 12 | `flipX` | `bool` | `0x69961C` | `0x574AF4` | `0x1000F87EC` | `0xF5588` |
| 13 | `flipY` | `bool` | `0x699628` | `0x574AFC` | `0x1000F87F8` | `0xF5590` |
| 14 | `zoomX` | `double` | `0x699634` | `0x574B04` | `0x1000F8804` | `0xF5598` |
| 15 | `zoomY` | `double` | `0x699640` | `0x574B14` | `0x1000F8810` | `0xF55A8` |
| 16 | `angleDeg` | `double` | `0x69964C` | `0x574B24` | `0x1000F881C` | `0xF55B8` |
| 17 | `angleRad` | `double` | `0x699658` | `0x574B38` | `0x1000F8828` | `0xF55C8` |
| 18 | `slantX` | `double` | `0x699680` | `0x574B70` | `0x1000F8850` | `0xF55FC` |
| 19 | `slantY` | `double` | `0x69968C` | `0x574B80` | `0x1000F885C` | `0xF560C` |
| 20 | `originX` | `double` | `0x699698` | `0x574B90` | `0x1000F8868` | `0xF561C` |
| 21 | `originY` | `double` | `0x6996B4` | `0x574BA8` | `0x1000F8880` | `0xF5634` |
| 22 | `opacity` | `int` | `0x6996D0` | `0x574BC0` | `0x1000F8898` | `0xF564C` |
| 23 | `mtx` | `Variant` | `0x6996DC` | `0x574BC8` | `0x1000F88A4` | `0xF5654` |
| 24 | `vtx` | `Variant` | `0x699894` | `0x574C44` | `0x1000F893C` | `0xF5744` |
| 25 | `color` | `Variant` | `0x699A88` | `0x574D64` | `0x1000F8AB8` | `0xF58FC` |
| 26 | `bezierPatch` | `Variant` | `0x699D90` | `0x574E80` | `0x1000F8B80` | `0xF5A14` |
| 27 | `shape` | `Variant` | `0x699F28` | `0x574F34` | `0x1000F8C60` | `0xF5B38` |
| 28 | `motion` | `Variant` | `0x699FB0` | `0x574F7E` | `0x1000F8CE8` | `0xF5C0C` |
| 29 | `particle` | `Variant` | `0x699FD4` | `0x574F9A` | `0x1000F8D0C` | `0xF5C28` |

因此 registrar 有 29 个脚本 property，却只有 27 个不同 native getter body。`left/x` 与
`top/y` 是 descriptor-level alias，不是两个重复实现。所有 property descriptor 都没有 setter
target/member adjustment；对 class-object instance 执行 `PropSet` 应返回 access denied，而不是
创建覆盖字段。

部分 IDA decompile 会把 UTF-16 member 名误渲染成单字母窄字符串。这是 string renderer 的
显示噪声：registrar 内连续 descriptor 顺序、UTF-16 byte evidence、四端 target 簇和本地 NCB
macro 共同确认上表完整名称。不能把单字母伪显示提升成额外 alias。

## Zero-argument constructor 的精确状态机

四端 constructor `FuncCall` 共同为：

```text
if membername != null:
    return TJS_E_MEMBERNOTFOUND       // -1001; result untouched

if numparams == 1 && param[0].Type == Void:
    return TJS_S_OK                   // empty adaptor shell; result untouched

if result != null:
    result.Clear()

if numparams < 0:
    return TJS_E_BADPARAMCOUNT        // -1004

native = operator new(pointer_size)
native.node = null

if objthis/adaptor metadata attach fails:
    operator delete(native)
    return TJS_E_NATIVECLASSCRASH     // -1008

return TJS_S_OK                       // result remains Void
```

重要边界：

- required argument count 为零，所以任意非负 `numparams` 都通过；所有参数都不读取、不转换；
- 一个且仅一个 Void 走 ncbind sentinel 分支，不分配 native record；
- `LayerGetter(1, 2, ...)` 仍发布真实 native wrapper，但 wrapper 的 node pointer 为 null；
- sentinel/membername 分支早于 result clear；正常分支才先清 result；
- attach helper 不先检查 objthis；它先分配并清零 native，再查询 instance metadata；
- attach 失败只需 `operator delete`，因为 native record 只有一个 trivial borrowed pointer；
- attach 成功把 native pointer写入 adaptor metadata，不把 native pointer返回到 result。

当前类内 `LayerGetter() = default` 加 `_node = nullptr` 精确对应四端的一槽清零。脚本 constructor
不是用于建立可查询节点的公开 factory；有效 facade 由 `Player::getLayerGetter` 与
`Player::getLayerGetterList` 的 native path 创建。

## 一指针 facade、adaptor ownership 与生命周期

### Instance adaptor 映射

| 角色 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `CreateAdaptor` path | `0x6F2B1C` | `0x5AFB24` | `0x1001452D0` | `0x145B88` |
| `CreateEmptyAdaptor` | `0x6FAFAC` | `0x5B668C` | `0x10014DA40` | `0x14F6E4` |
| `Invalidate` | `0x6FAFE0` | `0x5B66B0` | `0x10014DA74` | `0x14F708` |
| destructor entry/body | `0x6FB018` | `0x5B66CC` | `0x10014DAAC` → `0x10014DAC4` | `0x14F722` → `0x14F736` |
| deleting destructor | `0x6FB06C` | `0x5B6704` | `0x10014DAB0` | `0x14F726` |
| adaptor allocation size | `0x18` | `0x0C` | `0x18` | `0x0C` |

`CreateEmptyAdaptor` 的自然布局为：

| slot | 64-bit | 32-bit |
|---|---:|---:|
| vptr | `+0x00` | `+0x00` |
| native `LayerGetter*` | `+0x08` | `+0x04` |
| sticky byte | `+0x10` | `+0x08` |

初始化把 native pointer 和 sticky 都写零。`Invalidate` 四端共同执行：

```text
native = adaptor.native
if native != null && adaptor.sticky == false:
    operator delete(native)           // LayerGetter dtor is trivial
adaptor.native = null
adaptor.sticky = false
```

`CreateAdaptor(native, sticky, throw)` 会以一个 Void 参数创建 script shell，查询该 shell 的
LayerGetter adaptor metadata，然后写入 supplied native pointer 与 sticky byte。Player 的 facade
builder 固定传 `sticky=false`，因此成功 publication 后 adaptor 删除 one-pointer wrapper；wrapper
内部的 `MotionNode*` 仍只是 borrowed address。

要保留的失败差异：如果 `CreateAdaptor` 正常返回 null，Player-side builder 返回 Void，但没有
回收已分配的 wrapper，因此泄漏一个 8/4 字节 facade。成功 path 则由 adaptor 回收 facade。
无论哪条路径都没有 `MotionNode` destructor/delete。

`LayerGetter` 对 live node 的借用还形成两条既有生命周期边界：

- 同一 facade 会观察到 node 字段的后续变化，它不是 creation-time snapshot；
- node tree rebuild 或 Player destruction 后 facade 可以悬空。参考实现没有 owner retain、generation
  token 或 validity check，不能用 shared ownership/weak handle 擅自“修复”。

Player-side list/query 的 publication、flat deque 遍历、duplicate/Void 保留与 adaptor-null leak 已在
相邻 `motionplayer_layer_getter_lifecycle_four_binary_2026-08-12.md` 纵切面闭合；本文集中记录
class registration、constructor、完整 property surface 与 getter 内部数据流。

## 29 个 getter 的 live 数据流

### Scalar 与 string 家族

| property | 每次读取的 live source | 返回/复制行为 |
|---|---|---|
| `type` | `nodeType` | 32-bit integer |
| `label` | `layerName` | owning `ttstr` copy |
| `src` | `activeSlot().srcValue` | 先按当前 active slot index/stride 选槽，再作 owning `ttstr` copy |
| `visible` | `accumulated.visible` | bool |
| `branchVisible` | `accumulated.active` | bool |
| `layerVisible` | `accumulated.visible && accumulated.active` | bool；不读 `drawFlag` |
| `x`, `left` | `accumulated.posX` | same native target, double |
| `y`, `top` | `accumulated.posY` | same native target, double |
| `flipX/Y` | `accumulated.flipX/flipY` | bool |
| `zoomX/Y` | `accumulated.scaleX/scaleY` | double |
| `angleDeg` | `accumulated.angle` | double |
| `slantX/Y` | `accumulated.slantX/slantY` | double |
| `originX/Y` | `activeSlot().ox/oy` | 当前 active slot，每次重选，double |
| `opacity` | `accumulated.opacity` | 32-bit integer |

这些 getter 都没有 frozen/cache layer；`src` 和 `originX/Y` 尤其不能缓存 constructor 时的 slot。
`layerVisible` 四端重新反编译后均只有 visible/active 两次读取和 conjunction；`drawFlag` 只属于
render-item construction。原 `MotionNode.h` 中把 getter 归给 `drawFlag` 的注释来自过时目标。

### `angleRad` 的浮点操作顺序

四端都装载同一 binary64 pi literal，byte pattern 为：

```text
18 2d 44 54 fb 21 09 40
```

并执行：

```text
p = accumulated.angle * pi
p = p + p
return p / 360.0
```

编译器没有把它重写为 `/ 180.0`。例如有限 `angle = 4.0e307`：第一步仍有限，第二步溢出为
`Inf`，最终仍为 `Inf`；代数化简成 `angle * pi / 180` 会错误返回有限值。当前源码显式保存
中间 `angleTimesPi` 并自加，回归测试用上述有限输入锁住这一 machine boundary。

### `coord` 与 `mtx`

每次成功读取都创建一个新的 TJS Array：

```text
coord = [posX, posY, posZ]
mtx   = [m11, m12, m21, m22]
```

没有复用 persistent Array，也没有返回 node 内部存储视图。`coord` 的 node field offset 为：

| ABI | posX | posY | posZ |
|---|---:|---:|---:|
| Android ARM64 | `+1512` | `+1520` | `+1528` |
| Android ARMv7 | `+1272` | `+1280` | `+1288` |
| iOS ARM64 | `+1528` | `+1536` | `+1544` |
| iOS ARMv7 | `+1240` | `+1248` | `+1256` |

`mtx` 的四个 double 在 Android/iOS ARM64 为 `+120/+128/+136/+144`，在两个 32 位目标为
`+104/+112/+120/+128`。ABI offset 差异不改变四项顺序。

### `vtx`

四端都是固定四次循环：

1. 创建 fresh outer Array；
2. 对 8 个连续 float 按 `(x0,y0)...(x3,y3)` 分组；
3. 每组创建 fresh Dictionary；
4. 以 `TJS_MEMBERENSURE` 顺序写 `x`、`y`；
5. 把 owning object closure 追加到 outer Array。

四个 dictionary 使用 process-wide `x`/`y` member-hint slots，而不是每点私有 hint。vertex
buffer 起点分别为 Android ARM64 `node+1856`、Android ARMv7 `node+1616`、iOS ARM64
`node+1872`、iOS ARMv7 `node+1580`。调用两次 `vtx` 会得到两个 outer Array 和八个不同的
point dictionary；结果不是 live proxy。

### `color`

每次创建 fresh Array，把 node 中四个连续 packed 32-bit word 逐项 zero-extend 后作为 TJS integer
追加。native word offsets 在 64 位目标为 `+100/+104/+108/+112`，32 位目标为
`+84/+88/+92/+96`。这里必须保持 unsigned 解释：高位为 1 的颜色不能符号扩展成负 64 位值。

### `bezierPatch`

精确 gate 与 flatten 行为为：

```text
if node.meshType != 1:
    return Void
array = new TJS Array
for point in vector<MeshPoint> in native order:
    array.push(double(point.x))
    array.push(double(point.y))
return array
```

`meshType == 1` 且 vector 为空时返回 empty Array，不是 Void。每次调用都重新创建 Array，且
按 vector 当前 `[begin,end)` 遍历；没有固定 16 点假设、shape fallback 或 odd-count 处理，因为
native vector element 本身总是成对的 `MeshPoint`。

### `shape`

四端都先复制完整 native shape record，再按 shape type 创建 Point/Circle/Rect/Quad adaptor；
unsupported type 返回 Void。每次成功都是新 native geometry copy 和新 script adaptor，不是指向
node shape 的借用。若 geometry `CreateAdaptor` 正常返回 null，已复制 native shape 不被 caller
回收，保持原版 leak 边界。各 geometry 的 exact property surface、constructor/adaptor 和 Quad
point-container 语义已在 `motionplayer_geometry_four_binary_2026-08-11.md` 闭合，此处不重复。

### `motion` 与 `particle`

```text
motion   = nodeType == 3 ? copy(childPlayerVar)  : Void
particle = nodeType == 4 ? copy(particleArrayVar) : Void
```

gate 只检查 `nodeType`。通过 gate 后直接做 Variant CopyRef；不验证 `motion` 一定是 object，
也不验证 `particle` 一定是 Array/object。错误类型、Void 或被脚本重入替换后的当前 Variant 都按
普通 Variant 内容复制，不能添加更“友好”的 type guard。

## Null、异常与容器 publication 边界

- 29 个 getter 的第一步都是从 facade node slot 取 raw pointer 并解引用；无 null guard；
- scalar getter 不捕获异常，也不提供默认值；
- `coord/mtx/vtx/color/bezierPatch` 在每次调用中从空 container 逐项追加，没有 tail commit 或
  rollback；分配/写 property/扩容异常可以留下仅存在于 unwind path 的部分 container；
- `vtx` dictionary 的 `SetValue` 结果沿用现有 ncbind accessor 行为，没有额外 HRESULT gate；
- `shape` 和 Player-side facade publication 的 adaptor-null 分支保留 native leak；
- `motion/particle` 返回的是 owning Variant copy，所以 getter 返回值可以独立延长 script object
  生命周期；但 facade 对 node 自身仍然完全不 owning；
- tree rebuild 后悬空 facade 再调用 getter 是自然 use-after-free 风险，不转换成 Void。

## 本地实现对照与修正

`cpp/plugins/motionplayer/main.cpp` 已精确保持：

```text
constructor()
type, label, src, visible, branchVisible, layerVisible,
x, y, left, top, coord,
flipX, flipY, zoomX, zoomY, angleDeg, angleRad,
slantX, slantY, originX, originY, opacity,
mtx, vtx, color, bezierPatch, shape, motion, particle
```

本轮 production delta：

1. `PlayerLayerQuery.cpp::getAngleRad` 从代数化简的 `angle * pi / 180` 改为参考顺序
   `p = angle * pi; (p + p) / 360`；
2. `MotionNode.h::drawFlag` 注释删除错误的 LayerGetter consumer，只保留 render-item
   construction；并明确 `layerVisible` 属于 visible/active conjunction。

新增测试：

- `Motion.LayerGetter NCB constructor publishes only the null-node facade`：验证零参数真实 native、
  29 个 RO descriptor、一个 Void 的空 shell、两个 surplus 参数被忽略；测试不调用 null facade
  getter，从而只验证可安全观测的 constructor/class surface；
- 既有 live-facade 测试加入 `angle=4.0e307`，要求 `angleRad` 为 `Inf`，防止未来再次被
  algebraic simplification 改写。

## Recovery IDB 提升

四份 recovery IDB 已共同完成：

- registrar、class wrapper/class-info、constructor descriptor 链、FuncCall/allocate-attach 命名；
- 27 个 distinct getter body 的 `LayerGetter_get..._guess` 语义命名；
- `CreateAdaptor`、`CreateEmptyAdaptor`、`Invalidate`、destructor/deleting-destructor 命名；
- constructor `FuncCall` 施加带 8 个真实角色参数的 prototype；
- registrar、constructor、复杂 container getter、`layerVisible`、`angleRad` 与 adaptor lifecycle
  写回函数注释；
- registrar、constructor、angle overflow 和 non-owning facade 增加 recovery bookmark；
- Android ARMv7 原本被 literal pool/函数边界吞掉的 `angleRad/slantX/slantY/originX/originY`
  五个函数重新定义并命名；
- fresh decompile 已确认 typed 参数名、四端 multiply-double-divide 伪代码和 sticky=false delete
  wrapper 行为；四份 IDB 均已保存。

所有未知恢复名继续保留 `_guess`，未把分析地址写入 compiled source 注释。

## 验证

- motionplayer 完整 unit-test translation unit Emscripten syntax compile：通过；只有仓库既有 `_tss`
  deprecation warning；
- `Motion.LayerGetter` registrar 精确扫描：一个 zero-arg constructor、29 个 RO property、0 method、
  0 raw callback，顺序与上表一致；
- `cmake --build --preset "Web Debug Build"`：通过；
- `git diff --check`：通过；
- 当前仓库没有可直接运行的 native Catch2 motionplayer target，本纵切面因此如实记录 syntax
  coverage 与 Web build，不声称执行了 native runtime suite。

## 边界结论

`LayerGetter` 的正确抽象不是“安全的 layer DTO”，而是“由 script adaptor 管理的一指针 live
facade”：wrapper ownership 与 node ownership 完全分离；class constructor 可以发布一个 node
为空的真实 wrapper；复杂 getter才做快照容器；scalar getter始终直读 live node；tree 生命周期
变化不会通知或失效 facade。任何 null-safe default、node retain、snapshot cache、container reuse、
angle algebraic simplification 或 drawFlag 联动都会改变四份参考共同暴露的边界行为。
