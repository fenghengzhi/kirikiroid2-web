# LayerGetter NCB 表面与默认构造生命周期（四参考二进制，2026-08-26）

## 1. 范围

本纵切面闭合 `Motion.LayerGetter` 的 delayed-subclass member registrar、构造描述符
虚表入口、零参数构造边界、facade 分配/初始化和 adaptor 所有权转移。29 个 getter
的函数体和它们读取的 `MotionNode` 字段另设纵切面；本报告只证明每个脚本属性绑定
到哪个 native callback，不能代替函数体证据。

四端共同表面精确为：一个构造器，随后 29 个 getter-only property。没有 setter、
普通 method、常量、factory 或 raw callback。`x/left` 绑定同一 native getter，
`y/top` 绑定另一同一 getter，所以 29 个属性只对应 27 个不同 callback。

## 2. 四端根映射

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| member registrar | `LayerGetter_ncb_members@0x698730` | `...@0x574628` | `...@0x1000F81AC` | `...@0xF4FF8` |
| ctor descriptor vtable | `0x1A18B48` | `0x10B9FB0` | `0x101AE0C40` | `0x18322B0` |
| ctor `FuncCall` | `LayerGetter_NCB_ctor_dispatch_guess@0x6DFFAC` | `...@0x5A0BBC` | `...@0x100131274` | `...@0x13016C` |
| construct/attach | `LayerGetter_NCB_construct_and_attach_guess@0x6E0080` | `...@0x5A0C4C` | `...@0x100131314` | `...@0x1301D8` |
| ClassInfo class ID | `0x1AB5730` | `0x1111AC4` | `0x101ADF620` | `0x18317AC` |

构造 `FuncCall` 地址来自各 descriptor vtable slot 2；32 位表中保存的是最低位为 1
的 Thumb 指针，表中地址已正规化为函数起点。四个 registrar、四个构造入口和四个
construct/attach 主体均在本轮 fresh decompile；Android arm64 registrar 因模板完全
内联而有 787 条指令，另用八个原生 disasm 分页逐项核对 descriptor、键和 callback。

## 3. 精确 property 顺序与 callback 映射

| # | 脚本属性 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---|---:|---:|---:|---:|
| 1 | `type` | `0x6993F4` | `0x5749DE` | `0x1000F86AC` | `0xF5408` |
| 2 | `label` | `0x699400` | `0x5749E4` | `0x1000F86B8` | `0xF540E` |
| 3 | `src` | `0x699424` | `0x574A04` | `0x1000F86DC` | `0xF542E` |
| 4 | `visible` | `0x69945C` | `0x574A32` | `0x1000F8710` | `0xF545C` |
| 5 | `branchVisible` | `0x699468` | `0x574A3A` | `0x1000F871C` | `0xF5464` |
| 6 | `layerVisible` | `0x699474` | `0x574A42` | `0x1000F8728` | `0xF546C` |
| 7 | `x` | `0x699498` | `0x574A5A` | `0x1000F874C` | `0xF5484` |
| 8 | `y` | `0x6994A4` | `0x574A6A` | `0x1000F8758` | `0xF5494` |
| 9 | `left` | `0x699498` | `0x574A5A` | `0x1000F874C` | `0xF5484` |
| 10 | `top` | `0x6994A4` | `0x574A6A` | `0x1000F8758` | `0xF5494` |
| 11 | `coord` | `0x6994B0` | `0x574A7C` | `0x1000F8764` | `0xF54A4` |
| 12 | `flipX` | `0x69961C` | `0x574AF4` | `0x1000F87EC` | `0xF5588` |
| 13 | `flipY` | `0x699628` | `0x574AFC` | `0x1000F87F8` | `0xF5590` |
| 14 | `zoomX` | `0x699634` | `0x574B04` | `0x1000F8804` | `0xF5598` |
| 15 | `zoomY` | `0x699640` | `0x574B14` | `0x1000F8810` | `0xF55A8` |
| 16 | `angleDeg` | `0x69964C` | `0x574B24` | `0x1000F881C` | `0xF55B8` |
| 17 | `angleRad` | `0x699658` | `0x574B38` | `0x1000F8828` | `0xF55C8` |
| 18 | `slantX` | `0x699680` | `0x574B70` | `0x1000F8850` | `0xF55FC` |
| 19 | `slantY` | `0x69968C` | `0x574B80` | `0x1000F885C` | `0xF560C` |
| 20 | `originX` | `0x699698` | `0x574B90` | `0x1000F8868` | `0xF561C` |
| 21 | `originY` | `0x6996B4` | `0x574BA8` | `0x1000F8880` | `0xF5634` |
| 22 | `opacity` | `0x6996D0` | `0x574BC0` | `0x1000F8898` | `0xF564C` |
| 23 | `mtx` | `0x6996DC` | `0x574BC8` | `0x1000F88A4` | `0xF5654` |
| 24 | `vtx` | `0x699894` | `0x574C44` | `0x1000F893C` | `0xF5744` |
| 25 | `color` | `0x699A88` | `0x574D64` | `0x1000F8AB8` | `0xF58FC` |
| 26 | `bezierPatch` | `0x699D90` | `0x574E80` | `0x1000F8B80` | `0xF5A14` |
| 27 | `shape` | `0x699F28` | `0x574F34` | `0x1000F8C60` | `0xF5B38` |
| 28 | `motion` | `0x699FB0` | `0x574F7E` | `0x1000F8CE8` | `0xF5C0C` |
| 29 | `particle` | `0x699FD4` | `0x574F9A` | `0x1000F8D0C` | `0xF5C28` |

四库均已把 27 个不同 callback 命名为 `LayerGetter_get*_guess`。Android armv7
原 IDB 把 `angleRad/slantX/slantY/originX/originY` 识别成裸 label 而非函数；本轮按
相邻注册指针和确切边界定义成五个独立 Thumb 函数后再命名，没有把它们错误合并到
前一个 leaf。

## 4. 字符串证据

29 个键在四端都以 UTF-16LE（含双零终止）用原始字节搜索，再以 registrar xref
选中实际候选。每个实际地址已设为相应长度的 `const unsigned short[N]`。部分地址是
编译器合并字符串的内部后缀，例如 Android arm64 的 `type`、`visible`、`x/y/left`
和 Android armv7 literal pool；Hex-Rays 因已有外层 data item 边界仍可能只显示
首字符。完整原始字节、终止符、helper 的 `const unsigned short *name` 签名和
registrar xref 一致，因此报告使用完整脚本键，不能从 UI 的首字符回退成单字母 API。

## 5. 构造边界与共同伪代码

LayerGetter 使用与四个 geometry 类相同的 ncbind 零参数模板边界：

```text
CtorFuncCall(membername, result, numparams, params, objthis):
    if membername != null: return TJS_E_MEMBERNOTFOUND       // -1001
    if numparams == 1 and params[0].type == Void:
        return TJS_S_OK                                      // 不清 result，不附着
    if result != null: result.Clear()
    if numparams < 0: return TJS_E_BADPARAMCOUNT             // -1004

    facade = operator new(sizeof(void *))
    facade.node = null                                       // 唯一字段写入
    adaptor = objthis.NativeInstanceSupport(
        TJS_NIS_GETINSTANCE, LayerGetterClassID)
    if adaptor lookup succeeded and adaptor != null:
        adaptor.instance = facade
        return TJS_S_OK
    operator delete(facade)
    return TJS_E_NATIVECLASSCRASH                            // -1008
```

64 位分配 `8` 字节，32 位分配 `4` 字节；四端都只写一个 null pointer。普通路径
接受任意非负参数数目并忽略冗余参数；恰好一个 Void 是特殊的空-adaptor 哨兵。
Void 路径早于 `result.Clear()`，这一顺序与 geometry 构造器一致。

## 6. 对象所有权边界

- facade 自身只保存一个 raw `MotionNode*`；默认脚本构造把它设为 null。
- 构造器从脚本对象取得已经存在的类专属、非 sticky adaptor；它不创建或注册第二个
  adaptor。
- 附着成功后 adaptor 独占 facade，并在 Invalidate/析构时 scalar-delete facade；
  facade 不拥有 `MotionNode`。
- 附着失败在所有端立即 scalar-delete facade，返回 `-1008`。
- 安装 live `MotionNode*` 的 Player producer、节点替换后的悬空边界和 29 个 getter
  是否有 null guard 不由本注册/构造切面推断，必须在后续函数体/producer 纵切面
  fresh decompile。

## 7. 本地逐项对照

`cpp/plugins/motionplayer/main.cpp` 的 LayerGetter 表具有完全相同的 29 项顺序，全部
使用 `NCB_PROPERTY_RO`；`x/left` 分别绑定 `getX/getLeft`、`y/top` 分别绑定
`getY/getTop`，但当前本地这两对薄 wrapper 的二进制折叠语义要在 getter body 报告
中继续核对。

`cpp/plugins/motionplayer/SourceCache.h` 当前 facade 只有
`detail::MotionNode *_node = nullptr`，默认构造不增加其他状态；源结构与 `8/4` 字节
分配和唯一 null 写一致。现有 non-owning/悬空注释需要后续 producer/getter 证据
最终闭合，本报告不把它们提前升级为完整验证。

本纵切面未发现运行 C++ 的注册或构造偏差，因此不修改语义实现。

## 8. 剩余项

下一依赖步骤是分组闭合 27 个不同 getter body：先审计 16 个直接 scalar/string leaf，
再审计 `coord/mtx/vtx/color/bezierPatch/shape/motion/particle` 的 Variant 容器与 owner
路径，最后从 Player 的 `getLayerGetter/getLayerGetterList` 反向闭合 facade producer、
MotionNode 借用生命周期与节点树替换边界。
