# D3DAdaptor NCB 注册面四参考二进制联合恢复

日期：2026-08-27

## 1. slice 边界

本轮闭合 `Motion.D3DAdaptor` 的 delayed-subclass wrapper、独立 ClassInfo setup、16 行
成员 registrar、Factory 原生回调和全部 method/property callback 入口。纹理 target 的所有权、
software-copy map、`captureCanvas` 两条渲染后端路径以及 clear/copy 的逐行边界继续作为
独立 body/container/lifetime slice；注册面状态和 body 状态不合并。

## 2. subclass / ClassInfo 调用链

| 平台 | `Motion.D3DAdaptor` wrapper | delayed setup | member registrar |
|---|---|---|---|
| Android arm64 | `0x6FC6D8` | `0x6FC848` | `0x6AA274` |
| Android armv7 | `0x599848` | `0x5B74C8` | `0x57CC58` |
| iOS arm64 | `0x100126244` | `0x10014EDB4` | `0x1001039A4` |
| iOS armv7 | `0x12526C` | `0x150C70` | `0x100D94` |

四端都为 D3DAdaptor 建立独立 `ncbClassInfo<D3DAdaptor>` tuple；它不复用 Player、
SeparateLayerAdaptor 或 ResourceManager 的 class ID、class object、cast thunk 或 native offset。
`Motion` registrar 随后以 vptr-only static subclass item 发布这个独立 class dispatch；Player 的
进程级共享 D3D renderer 是另一条 raw-owner 数据流，不通过这个 NCB subclass 暴露。

Factory descriptor 是动态类名 constructor row，也是 16 行中的第一行。它令 ClassInfo 进入
constructor-seen 状态并抑制 ncbind 的 retained dummy constructor。NativeClass 仍先创建空的
`{native = null, sticky = false}` adaptor；正常 Factory 成功后由生成层把返回的 native 指针写入
该 adaptor。Factory 回调本身只返回 raw native 指针，不直接操作 receiver adaptor。

## 3. 精确 16 行发布表

| # | 脚本名 | kind | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---|---|---|---|---|---|
| 1 | `<factory>` | Factory | `0x6AA8F8` | `0x57CEBC` | `0x100103C30` | `0x100FD4` |
| 2 | `setPos` | method | `0x6AAB84` | `0x57CF64` | `0x100103D3C` | `0x101128` |
| 3 | `setSize` | method | `0x6AAB88` | `0x57CF66` | `0x100103D40` | `0x10112A` |
| 4 | `setClearColor` | method | `0x6AAB90` | `0x57CF6C` | `0x100103D48` | `0x101130` |
| 5 | `setResizable` | method | `0x6AAB98` | `0x57CF70` | `0x100103D50` | `0x101134` |
| 6 | `removeAllTextures` | method | `0x6AAC98` | `0x57CF74` | `0x100103D58` | `0x101138` |
| 7 | `removeAllBg` | method/no-op | `0x6AACD0` | `0x57CF7A` | `0x100103D88` | `0x101154` |
| 8 | `removeAllCaption` | method/no-op | `0x6AACD4` | `0x57CF7C` | `0x100103D8C` | `0x101156` |
| 9 | `registerBg` | method/no-op | `0x6AACD8` | `0x57CF7E` | `0x100103D90` | `0x101158` |
| 10 | `registerCaption` | method/no-op | `0x6AACDC` | `0x57CF80` | `0x100103D94` | `0x10115A` |
| 11 | `unloadUnusedTextures` | method/no-op | `0x6AACE0` | `0x57CF82` | `0x100103D98` | `0x10115C` |
| 12 | `visible` | read/write property | get `0x6AACE4`, set `0x6AACEC` | get `0x57CF84`, set `0x57CF88` | get `0x100103D9C`, set `0x100103DA4` | get `0x10115E`, set `0x101162` |
| 13 | `alphaOpAdd` | read/write property | get `0x6AACF8`, set `0x6AAD00` | get `0x57CF8C`, set `0x57CF90` | get `0x100103DAC`, set `0x100103DB4` | get `0x101166`, set `0x10116A` |
| 14 | `captureCanvas` | method | `0x6AAD0C` | `0x57CF94` | `0x100103DBC` | `0x10116E` |
| 15 | `canvasCaptureEnabled` | read/write property | get `0x6AAEC8`, set `0x6AAED0` | get `0x57D09C`, set `0x57D0A0` | get `0x100103F88`, set `0x100103F90` | get `0x10127C`, set `0x101280` |
| 16 | `clearEnabled` | read/write property | get `0x6AAEDC`, set `0x6AAEE4` | get `0x57D0A4`, set `0x57D0A8` | get `0x100103F98`, set `0x100103FA0` | get `0x101284`, set `0x101288` |

`setPos` 与第 7-11 行在四端都是单指令 return 的空函数；这是公开边界行为，不应把
Windows/旧后端可能存在的背景图、caption 或 unused-texture 逻辑补进当前源。其余 callback
都是独立函数起点，没有 Android arm64 合并函数内部入口。property descriptor 还包含空的
indexed getter/setter 槽；表中的 get/set 方向由四端 thunk body 和本地 `NCB_PROPERTY`
签名共同确认，而不是仅依赖 descriptor 内部字段顺序。尤其 Android arm64 的展开布局与
其余三端 helper 参数顺序不同，四组方向均以实际 load/store body 为准。

## 4. Factory 发布链与参数边界

| 阶段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| Factory descriptor publish | registrar inline | `0x57CE94` | `0x100103BDC` | `0x100FAC` |
| Factory callback | `0x6AA8F8` | `0x57CEBC` | `0x100103C30` | `0x100FD4` |
| native constructor | `0x6AAEF0` | `0x57D0AC` | `0x100103FA8` | `0x10128C` |

四端共同语义：

```text
D3DAdaptorFactory(result, argc, argv, objthis):
    if argc < 5:
        return TJS_E_BADPARAMCOUNT

    window = argv[0].AsObjectNoAddRef()
    if window is null or window.IsInstanceOf("Window", window) != TJS_S_TRUE:
        throw "must set Window object"

    width   = argv[1].AsInteger() narrowed to tjs_int
    height  = argv[2].AsInteger() narrowed to tjs_int
    centerX = argv[3].AsInteger() narrowed to tjs_int
    centerY = argv[4].AsInteger() narrowed to tjs_int
    native = new D3DAdaptor(window, width, height, centerX, centerY)
    *result = native
    return TJS_S_OK
```

精确边界如下：

- `argc >= 5`，所以第 6 个及后续参数被忽略；
- 首参不是从一个容器读取名为 `Window` 的属性，而是把首参本身取为 object，并对该
  object 调用 `IsInstanceOf(..., "Window", objthis = window)`；
- 后四参分别走 Variant integer conversion，源结构应保持四个 `tjs_int`/`int`，不复刻
  ABI 临时寄存器或 padding；
- native constructor 成功返回后才写 `*result`；分配后构造抛异常时四端都存在
  `operator delete` 清理，iOS armv7 额外表现为 SjLj cleanup；
- Factory 回调不读取 `objthis`，也不直接销毁或替换 adaptor 中可能已有的 native。

四端 native 分配大小分别为 `0x68 / 0x40 / 0x50 / 0x34`，差异来自 LP64/ILP32、STL 与
平台对象布局，只用于确认 ABI，不应转化为源代码 padding。

## 5. registrar 共同伪代码

```text
registerMembers(state):
    publishFactory(dynamicClassName, D3DAdaptorFactory)
    publishMethod("setPos", noOp)
    publishMethod("setSize", setSize)
    publishMethod("setClearColor", setClearColor)
    publishMethod("setResizable", setResizable)
    publishMethod("removeAllTextures", removeAllTextures)
    publishMethod("removeAllBg", noOp)
    publishMethod("removeAllCaption", noOp)
    publishMethod("registerBg", noOp)
    publishMethod("registerCaption", noOp)
    publishMethod("unloadUnusedTextures", noOp)
    publishReadWriteProperty("visible", getVisible, setVisible)
    publishReadWriteProperty("alphaOpAdd", getAlphaOpAdd, setAlphaOpAdd)
    publishMethod("captureCanvas", captureCanvas)
    publishReadWriteProperty(
        "canvasCaptureEnabled", getCanvasCaptureEnabled, setCanvasCaptureEnabled)
    publishReadWriteProperty("clearEnabled", getClearEnabled, setClearEnabled)
```

每次 descriptor publish 前都重新检查 registering flag。Android arm64 把 Factory 及多数
descriptor 分配展开在 registrar 内；其余三个平台更多地调用模板 publisher helper。发布
顺序、descriptor kind、callback 等价类和中止边界一致。

## 6. 本地逐行对照

`cpp/plugins/motionplayer/main.cpp` 当前 `NCB_REGISTER_SUBCLASS_DELAY(D3DAdaptor)` 与上表
16 行完全一致：第一行使用 `Factory(&D3DAdaptor::factory)`，随后 10 个 method、两组
read/write property、`captureCanvas` 和最后两组 read/write property，顺序无偏移。

`cpp/plugins/motionplayer/D3DAdaptor.cpp` 的 Factory 已保持 `argc < 5` gate、首参
`AsObjectNoAddRef` + `Window` instance gate、四个整数转换、分配后写 result 和成功返回值；
本轮无需修改运行时 C++。`D3DAdaptor.h` 的 `setPos` 以及五个 legacy method 也保持空实现。

## 7. fresh 证据、状态与剩余工作

- 完整读取四个 subclass wrapper：89/25/28/25 条指令；
- 完整读取四个 delayed setup：80/48/32/71 条指令；
- 完整读取四个 registrar：403/141/127/159 条指令；
- 完整读取四个 Factory callback：157/56/60/121 条指令，并读取三个独立 Factory
  publisher helper；Android arm64 publisher 已内联在 registrar；
- 对 16 行对应的 20 个四端 callback 地址全部完成 fresh function map，确认所有地址都是
  函数起点及六组 no-op 等价类；
- 四个 IDB 已完成 wrapper/setup/registrar/Factory/callback 命名，关键入口已注释、添加
  registrar/Factory 书签并原位保存；
- 本轮把 16 条注册面提升为 `EVIDENCED_4_4`；Factory body 标为
  `FACTORY_EVIDENCED_4_4`。后续 `MP-C14-D3DADAPTOR-SIMPLE-STATE-CLEAR` 已逐体闭合
  14 条简单状态、兼容空操作和纹理 map clear；随后
  `MP-D14-D3DADAPTOR-CAPTURE-CANVAS` 又闭合 `captureCanvas`，15 个非 Factory body
  现已全部提升为 `IMPLEMENTED`。

后续注册面组 `BezierPatch`、`MotionLayerExtensions_guess` 和 `EmotePlayer` 也已闭合。
D3DAdaptor 的 software texture-copy map 键值所有权、whole-map clear、`captureCanvas`
pitch/size/静态纹理复用和 target texture 替换已经由上述后续 slices 闭合；
`MP-L14-D3DADAPTOR-LIFECYCLE` / `MP-D14-D3DADAPTOR-CLEAR` 又闭合了构造、析构异常
生命周期、进程 shared adaptor 和 clear render method cache；默认/私有 manager 分界由
`MP-R14-MOTION-PRIVATE-OPENGL-ENVELOPE` 闭合；software source map 的查找、插入和失败
owner 边现由 `MP-R14-D3D-SOURCE-GETTER-MAP-INSERT` 闭合。shared deep renderer 的外层
逐 item/method/batch/stencil 数据流现也由 `MP-R14-D3D-DEEP-BATCH-STENCIL` 闭合，公共
mesh helper 随后由 `MP-R14-D3D-MESH-SUBMIT-CELLS` 闭合；相邻剩余重点是 Bezier
basis/tessellation helper，后者也已由 `MP-R14-BEZIER-BASIS-TESSELLATION` 闭合。
