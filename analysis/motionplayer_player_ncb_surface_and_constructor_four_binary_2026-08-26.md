# Player NCB 注册表面与构造桥（四参考二进制，2026-08-26）

## 1. 结论与范围

本纵切面闭合 `Motion.Player` 的 NCB member registrar、构造 descriptor 虚表入口、
脚本参数边界、native 分配/构造/adaptor attach，以及 registrar 尾部的一次性
Bezier 4×4 网格初始化。92 个公开成员的函数体仍按后续纵切面逐组闭合；本报告证明的
是完整“表面分母”和每个表面项到 native callback 的四端绑定，不能把
`EVIDENCED_4_4` 的注册行误读成 callback body 已全部恢复。

四端共同的公开形状精确为：

- 一个 `Player(tTJSVariant)` 构造 descriptor；
- 43 个 read/write property，其中 `defaultSyncActive/defaultTransformOrder`
  是带 `TJS_STATICMEMBER` 的 class-static property，其余 41 个是实例 property；
- 17 个 read-only property；
- 27 个普通 typed method；
- 3 个 native-instance raw callback method（`setVariable/play/progress`）；
- 2 个显式 typed-detail method（脚本名 `clear/draw`）；
- 合计 92 个公开 descriptor，没有常量、factory 或额外隐藏脚本成员。

92 行权威地址表位于
`analysis/motionplayer_player_ncb_surface.tsv`。表中同时记录四端完整 UTF-16LE
名字地址、实际 publication call、callback 地址集合和本地宏形状。

## 2. 四端根与构造链

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| Player member registrar | `Player_ncb_members@0x6D3DA8` | `...@0x597EC8` | `...@0x1001244F8` | `...@0x123848` |
| ctor descriptor publication | registrar 内联，首个 publish `0x6D3E3C` | `Player_ncb_register_constructor@0x598CFC` | `...@0x1001253E0` | `...@0x124600` |
| descriptor factory | registrar 内联，分配 `0x38` | `Player_ncb_ctor_descriptor_factory@0x5B0654` | `...@0x1001461AC` | `...@0x14667C` |
| descriptor vtable | `0x1A1C8B8` | `0x10BBE68` | `0x101AE49B8` | `0x1834168` |
| vtable slot 2 / FuncCall | `Player_ncb_ctor_dispatch@0x6F3FB0` | `...@0x5B0798` | `...@0x100146384` | `...@0x1468E4` |
| materialize + attach | `Player_ncb_ctor_materialize_and_attach@0x6F4088` | `...@0x5B0828` | `...@0x100146428` | `...@0x146950` |
| arg0 Variant copy chain | `Player_ncb_ctor_copy_arg0@0x6F424C` | `...@0x5B090C` | `...@0x10014654C` | `...@0x146B6C` |
| native allocation helper | `Player_ncb_construct_native@0x6F41A0` | attach 内联 | attach 内联 | `Player_ncb_construct_native@0x146A98` |
| native Player ctor | `Player_ctor_guess@0x6CC110` | `...@0x5935C4` | `...@0x10011EC04` | `...@0x11D488` |
| native allocation size | `0x568` | `0x3B0` | `0x4B8` | `0x348` |

descriptor vtable 的 slot 2 是外部 TJS `FuncCall` 入口；32 位表保存带 Thumb 位的
函数指针，表中已写正规化后的函数起点。四个 allocation size 的差异来自
LP64/ILP32、libstdc++/libc++ 与内部容器物理布局，不能转写成本地源代码 padding。

以上 registrar、factory、slot-2 dispatch、materializer、attach 和 native ctor 都在
本轮 fresh decompile；Android arm64 的模板 registrar 有 2578 条指令，另以原生
disasm 分 26 页重建 93 个 publication call（构造器 + 92 个公开成员）。

## 3. 构造边界与共同伪代码

四端共同控制流可归纳为：

```text
PlayerCtorFuncCall(membername, result, argc, argv, objthis):
    if membername != null:
        return TJS_E_MEMBERNOTFOUND                         // -1001

    if argc == 1 and argv[0].Type() == Void:
        return TJS_S_OK                                    // 空 adaptor 哨兵
                                                            // 不清 result，不构造 native

    if result != null:
        result.Clear()

    if argc < 1:
        return TJS_E_BADPARAMCOUNT                         // -1004

    arg0 = CopyRef(argv[0])                                // 只复制第一个参数
    native = operator new(sizeof(Player))
    Player::Player(native, arg0)                           // attach 查询之前完整构造

    adaptor = objthis.NativeInstanceSupport(
        TJS_NIS_GETINSTANCE, PlayerClassID)

    if adaptor lookup succeeded and adaptor != null:
        adaptor.native = native                            // 只覆盖 native slot
        return TJS_S_OK

    native->~Player()
    operator delete(native)
    return TJS_E_NATIVECLASSCRASH                          // -1008
```

因此必须保留以下边界：

- 零实参不是默认构造，而是 `-1004`；
- 恰好一个 Void 是 ncbind 的空壳哨兵，早于 `result.Clear()` 返回成功；
- 任意非 Void arg0 都按原始 Variant 类型 CopyRef；多余参数被接受并忽略；
- Player 在 receiver adaptor 查询前就已完整构造；
- 普通 attach 失败会完整析构并 scalar-delete 新 Player，不泄漏本次对象；
- attach 只写 adaptor 的 native slot。若对已含 native 的 receiver 重复调用构造器，
  旧指针不会先被 teardown；覆盖导致旧 native 泄漏，这是可观察的原生边界；
- `operator new`/构造异常沿 C++ 异常路径传播，不被伪装成 `-1008`。

本地 `NCB_CONSTRUCTOR((tTJSVariant))` 与这些参数边界一致；当前不需要修改构造宏。

## 4. 92 行表面与重要别名

完整逐行表不在 Markdown 重复，避免两份地址源漂移；使用 TSV 作为唯一逐行账本。
其顺序与本地 `cpp/plugins/motionplayer/main.cpp` 的 `#1..#92` 完全一致。

需要特别保留的 callback 复用关系：

- `motionKey` 与 `project` 是同一 getter/setter 对；
- `defaultSyncActive/defaultTransformOrder` 的 callback 不接收 `this`，descriptor
  不查询 Player adaptor；二者是 class-static property，不是“访问全局值的实例方法”；
- `x` 与 `left` 是同一 getter/setter 对；
- `y` 与 `top` 是同一 getter/setter 对；
- `opacity` 的 setter 与公开方法 `setOpacity` 是同一 callback；
- `visible` 的 setter 与公开方法 `setVisible` 是同一 callback；
- `onAction` 与 `onSync` 的四端 callback 都是独立 nullsub，不应合并成一个地址；
- `getLayerGetter`、`getLayerGetterList` 已在独立 producer 纵切面闭合函数体，但
  仍作为 Player 表面第 84/85 行保留，不能从本表删去。

Android arm64 的 descriptor 对由模板内联构造，寄存器加载顺序不稳定；TSV 中
`android_arm64_callbacks_unordered_if_pair` 只表达准确 callback 集合，不在没有
leaf-body 证据时把加载先后伪装成 getter/setter 角色。三个 compact registrar 的参数
槽顺序则直接保留 descriptor 的 getter/setter 次序。后续 body 纵切面会逐项给
Android arm64 集合恢复角色。

## 5. UTF-16LE 名字证据

92 个名字 × 4 个目标共 368 项均完成以下审计：

1. 搜索完整 UTF-16LE 原始字节并要求双零终止；
2. 从所有同文候选中选中回到 `Player_ncb_members` 的 xref 或 disasm operand；
3. 与 publication 顺序和 callback descriptor 同行核对；
4. 在四个 IDB 中把实际地址硬化为相应长度的 `unsigned short[N]` 并命名为
   `Player_ncb_name_*`；
5. 保存前重新反编译 compact registrar，确认原先只显示首字符的键恢复为完整名字。

唯一需要单独解释的是 `x`：编译器把它放进其他宽字面量的后缀或 registrar literal
pool，普通 data-xref 不一定落在内部地址。四端实际指针分别是
`0x1507F7E / 0x598AE0 / 0x10195B28E / 0x174D5F2`；publication 指令、周边原始字节
`78 00 00 00` 和相邻 `y` 项共同证明其边界。其余 364 项可由普通 xref 与分段
disasm 的并集直接唯一定位。

## 6. Registrar 尾部的一次性运行时副作用

第 92 行发布后，四端都执行同一额外逻辑；它不产生第 93 个脚本成员：

```text
if bezierGrid.begin == bezierGrid.end:
    reserve exactly 16 Float2 elements
    for i in 0..15:
        append {
            x = float((i & 3) / 3.0),
            y = float((i >> 2) / 3.0)
        }

if (cpuFeatureWord & 0x0200000F) == 0x02000003:
    activeBezierEvaluator = armNeonBezierEvaluator
```

容器第一次填充后的逻辑 size 为 16，坐标顺序为
`(0,0),(1/3,0),(2/3,0),(1,0),...,(1,1)`。Android arm64 把 reserve/grow 完全内联；
Android armv7、iOS arm64、iOS armv7 分别调用自己的 reserve/grow helper。四端都只在
`begin == end` 时填充；若容器处于“非空但不足 16”状态不会补齐。ARM+NEON
函数指针提升发生在填充判断之后，每次 registrar 运行都会重新检查 CPU feature word。

本地 `internal::initializeBezierPatchRuntime_guess()` 正确放在 92 项之后；其容器与
NEON evaluator 函数体仍属于后续内部容器/渲染纵切面，本报告只闭合调用位置和共同
边界。

## 7. 本地逐项对照

`cpp/plugins/motionplayer/main.cpp` 当前 Player 表面与二进制共同证据一致：

- 构造器签名为一个 `tTJSVariant`；
- 92 项顺序、名字和宏类别完全相同；
- raw callback 只用于 `setVariable/play/progress`；
- typed-detail 只用于 `clear/draw`；
- `processedMeshVerticesNum` 保持 read-only property；
- registrar 尾部初始化不混入公开 member 计数。

本纵切面没有发现新的运行 C++ 表面偏差，因此只增加证据账本，不修改 Player 运行
语义。已有 Geometry 修复和测试变化属于先前独立纵切面。

## 8. 剩余项

表面闭合以后，下一步按依赖关系分组恢复 callback body：

1. 先闭合默认值、直接 scalar/flag、时间和别名 getter/setter；
2. 再闭合 Variant/string/Array/Dictionary 返回值与 setter CopyRef；
3. 再闭合播放状态机、变量系统、相机、命令列表和递归节点方法；
4. 最后把 Player ctor/dtor 的完整成员顺序、异常清理和所有内部容器与这些 callback
   体交叉验收。

在所有 92 个 body 都有四端 disposition 前，不能把 Player 整体标记为
`IMPLEMENTED` 或 `VERIFIED`。
