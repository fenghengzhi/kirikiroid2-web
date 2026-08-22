# motionplayer DrawDevice 公开 NCB 回调、参数数目与边界顺序（四参考二进制）

日期：2026-08-15

## 结论

四个参考二进制为根 `D3D` / DrawDevice 对象生成了相同的 34 项 NCBind 注册表。公开固定签名方法不是手写的宽松回调；它们都经过生成的 typed-method descriptor，并共享同一套前置检查、结果清空、参数转换和 native adaptor 查找顺序。

本轮最重要的源码级修正是：

1. `checkEnable` 和 `getModule` 都要求至少一个参数，并且包装器一定先把 `arg[0]` 转成 `ttstr`。业务体才忽略这个字符串。
2. `getModule` 成功时返回 `tvtInteger(0)`，不是 `Void`。
3. typed wrapper 只检查最小参数数目，转换其签名所需的前 N 项；所有额外参数均被忽略。
4. 方法包装器在参数数目检查前清空非空 `result`，但 `membername` 与空 `objthis` 的拒绝发生在清空之前。
5. 属性 descriptor 也有固定顺序。尤其只读属性的 `PropSet` 会先因没有 setter 返回 `TJS_E_ACCESSDENYED`，甚至不会检查 receiver 或 value。
6. `transState` 在 `double` 精度下执行 `min(max(x, 0), 1)` 后才窄化成 `float`，因此 NaN 保持 NaN。
7. `primaryLayers`、`getPrimaryLayerBitmap` 和 `capture` 的原版业务体比旧移植注释更严格：仅保留反编译中确实存在的空值分支，其余指针链不添加“安全”保护。

这些结论只来自 `reference/binaries/` 中 Android arm64、Android armv7、iOS arm64、iOS armv7 四份参考，不沿用原 `libkrkr2.so` 注释。

## 参考目标与记号

| 简称 | 平台 / ABI |
|---|---|
| A64 | Android arm64-v8a |
| A32 | Android armeabi-v7a / Thumb |
| I64 | iOS arm64 |
| I32 | iOS armv7 / Thumb |

文中的绝对地址只用于复核四份 recovery IDB，不写入编译源码。剥离符号后仍不能从 RTTI 或字符串证明的恢复名继续带 `_guess`。

## 34 项公开注册顺序

四份根类注册函数分别位于 A64 `0x52A618`、A32 `0x492790`、I64 `0x10023070C`、I32 `0x22F622`。四者注册顺序完全一致：

```text
 1 children                 RO property
 2 clearColor               RW property
 3 transState               RW property
 4 add                      method
 5 remove                   method
 6 startTransition          method
 7 stopTransition           method
 8 update                   method
 9 checkEnable              method
10 getModule                method
11 capture                  method
12 offsetX                  RW property
13 offsetY                  RW property
14 setOffset                method
15 stretchType              RW property
16 bicubicParam             RW property
17 forceRenderTexture       RW property
18 interface                RO property
19 setPrimarySize           method
20 primaryWidth             RW property
21 primaryHeight            RW property
22 setScreenRect            method
23 screenLeft               RW property
24 screenTop                RW property
25 screenWidth              RW property
26 screenHeight             RW property
27 primaryLayers            RO property
28 layerManagerIndex        RW property
29 getPrimaryLayerBitmap    method
30 destLeft                 RO property
31 destTop                  RO property
32 destWidth                RO property
33 destHeight               RO property
34 generated class/factory completion entry
```

源码宏中 33 个显式 member 行与 NCBind 注册末尾生成的类/工厂完成项共同构成反编译注册序列。公开名字、顺序及 descriptor 种类在四架构上相同。

## 固定签名方法的最小参数数目

下表列出每种签名共享的 `FuncCall` 入口。`add` 与 `remove` 因签名相同而共享一个模板实例；其余相同语义的方法在四份二进制中也保持同一最小参数数目。

| 方法 | 最小 argc | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|---:|
| `add`, `remove` | 1 | `0x535A1C` | `0x498FB0` | `0x1002379E8` | `0x236A6C` |
| `startTransition` | 1 | `0x535C10` | `0x4991CC` | `0x100237C80` | `0x236DDC` |
| `stopTransition` | 0 | `0x535E4C` | `0x49946C` | `0x100237FB4` | `0x237224` |
| `update` | 1 | `0x535F54` | `0x499608` | `0x1002381D8` | `0x2374A4` |
| `checkEnable` | 1 | `0x53617C` | `0x499890` | `0x100238508` | `0x237850` |
| `getModule` | 1 | `0x5364A4` | `0x499BBC` | `0x100238908` | `0x237D2C` |
| `capture` | 2 | `0x5367E0` | `0x499EF8` | `0x100238D20` | `0x238220` |
| `setOffset` | 2 | `0x536E44` | `0x49A4F8` | `0x100239458` | `0x238B00` |
| `setPrimarySize` | 2 | `0x537958` | `0x49AF78` | `0x10023A0B0` | `0x23995C` |
| `setScreenRect` | 4 | `0x537E1C` | `0x49B420` | `0x10023A69C` | `0x23A018` |
| `getPrimaryLayerBitmap` | 2 | `0x5384F4` | `0x49B930` | `0x10023AD38` | `0x23A760` |

每个入口只做 `argc < required` 判断。没有 `argc == required` 判断，因此：

```text
argc < N  -> TJS_E_BADPARAMCOUNT
argc >= N -> 仅转换 arg[0..N-1]，忽略 arg[N..]
```

`stopTransition` 的模板仍有形式上的 `argc < 0` 分支；正常 TJS 调用的非负 `argc` 总会通过。它并不因此跳过 member-name、receiver 或 native adaptor 检查。

## `checkEnable` / `getModule` 的完整 typed 转换链

这两个方法的业务函数极短，很容易被误判为零参数 stub。descriptor 的 registrar、factory、构造函数、vtable、`FuncCall`、invoke command 和 converter 串起来后，四份参考都明确证明它们是 `ttstr` 单参数成员函数。

### `checkEnable(ttstr) -> bool`

| 目标 | registrar / factory / ctor | descriptor vtable | `FuncCall` | invoke | `ttstr` converter |
|---|---|---:|---:|---:|---:|
| A64 | 模板实例由根注册函数引用 | `0x19FB988` | `0x53617C` | `0x536298` | `0x53638C` |
| A32 | `0x4997B8` / `0x4997EC` / `0x499828` | `0x10AB6E0` | `0x499890` | `0x499950` | `0x4999F4` |
| I64 | `0x1002383B8` / `0x10023840C` / `0x100238470` | `0x101AEF650` | `0x100238508` | `0x1002385E8` | `0x1002386A0` |
| I32 | `0x237668` / `0x237690` / `0x237750` | `0x1839760` | `0x237850` | `0x2378E4` | `0x2379E8` |

业务体地址为 A64 `0x52B9C0`、A32 `0x492D7C`、I64 `0x100230E68`、I32 `0x22FCEA`。转换完成后业务体忽略字符串并返回 C++ `false`；返回值 materializer 将它写成 TJS `tvtInteger(0)`。

### `getModule(ttstr) -> tTJSVariant`

| 目标 | registrar / factory / ctor | descriptor vtable | `FuncCall` | invoke | `ttstr` converter |
|---|---|---:|---:|---:|---:|
| A64 | 模板实例由根注册函数引用 | `0x19FBAA8` | `0x5364A4` | `0x5365C0` | `0x5366C8` |
| A32 | `0x499AE4` / `0x499B18` / `0x499B54` | `0x10AB770` | `0x499BBC` | `0x499C7C` | `0x499D30` |
| I64 | `0x1002387B8` / `0x10023880C` / `0x100238870` | `0x101AEF770` | `0x100238908` | `0x1002389E8` | `0x100238AB8` |
| I32 | `0x237B44` / `0x237B6C` / `0x237C2C` | `0x18397F0` | `0x237D2C` | `0x237DC0` | `0x237EDC` |

业务体地址为 A64 `0x52B9C8`、A32 `0x492D80`、I64 `0x100230E70`、I32 `0x22FCEE`。它构造 discriminator 为 4、整数 payload 为 0 的 `tTJSVariant`，即 `tvtInteger(0)`。这不是“返回成功码但不写 result”，也不是默认 `Void`。

两组 converter 的控制流与已识别的 `Player.getLayerNames(ttstr)` 模板一致，进一步排除了 `tTJSVariant`、`const tjs_char *` 或零参数签名。

## typed method descriptor 的错误与副作用顺序

四架构的各模板实例共同给出如下顺序：

```text
1. membername != nullptr
      -> TJS_E_MEMBERNOTFOUND (-1001)
      -> result 保持原值

2. objthis == nullptr
      -> TJS_E_NATIVECLASSCRASH (-1008)
      -> result 保持原值

3. result != nullptr
      -> result.Clear()，变成 Void

4. argc < required
      -> TJS_E_BADPARAMCOUNT (-1004)
      -> result 已是 Void

5. 从 objthis 取目标类 native adaptor / native instance
      -> 失败返回 TJS_E_NATIVECLASSCRASH (-1008)
      -> result 已是 Void

6. 依声明签名转换前 required 个参数
      -> 多余参数不读取

7. 调用业务体并 materialize 返回值
      -> void 方法保持第 3 步产生的 Void
```

这里 `membername` 表示有人试图在 descriptor 上继续访问嵌套成员，而不是根类正常的公开名字。正常 `root.FuncCall("getModule", ...)` 在根对象派发后进入 descriptor 的 default member，此时传给 descriptor 的 `membername` 是空。

这套顺序产生几个可观察且不能交换的边界：

| 调用条件 | 返回值 | 非空 `result` 的状态 |
|---|---:|---|
| descriptor + 非空嵌套 member name | `-1001` | 保留旧值 |
| descriptor + 空 receiver | `-1008` | 保留旧值 |
| 参数不足 | `-1004` | 已清成 Void |
| receiver 存在但没有本类 native adaptor | `-1008` | 已清成 Void |
| 成功的 void 方法 | `0` | Void |
| 成功的 `checkEnable` | `0` | Integer 0 |
| 成功的 `getModule` | `0` | Integer 0 |

## typed property descriptor 的错误与副作用顺序

### `PropGet`

```text
1. membername != nullptr       -> TJS_E_MEMBERNOTFOUND (-1001)
2. descriptor 没有 getter     -> TJS_E_ACCESSDENYED (-1007)
3. objthis == nullptr          -> TJS_E_NATIVECLASSCRASH (-1008)
4. result != nullptr           -> Clear()
5. native adaptor 查找失败     -> TJS_E_NATIVECLASSCRASH (-1008)
6. 调 getter 并 materialize
```

### `PropSet`

```text
1. membername != nullptr       -> TJS_E_MEMBERNOTFOUND (-1001)
2. descriptor 没有 setter     -> TJS_E_ACCESSDENYED (-1007)
3. objthis == nullptr          -> TJS_E_NATIVECLASSCRASH (-1008)
4. value == nullptr            -> TJS_E_FAIL (-1)
5. native adaptor 查找失败     -> TJS_E_NATIVECLASSCRASH (-1008)
6. 转换恰好一个 value，调用 setter
```

与方法不同，`PropSet` 没有 `result` 槽可清空。只读属性的“setter 是否存在”检查排在 receiver 和 value 之前，所以对 `interface` descriptor 调 `PropSet(objthis=null, value=null)` 的结果是 `-1007`，不是 `-1008` 或 `-1`。

### 代表性四平台入口

| descriptor | 操作 | A64 | A32 | I64 | I32 |
|---|---|---:|---:|---:|---:|
| `transState` RW | `PropGet` | `0x53573C` | `0x498C10` | `0x100237650` | `0x2365C0` |
| `transState` RW | `PropSet` | `0x53586C` | `0x498C9C` | `0x10023775C` | `0x236626` |
| `interface` RO | `PropGet` | `0x537734` | `0x49AD18` | `0x100239DAC` | `0x239650` |
| `interface` RO | `PropSet` | `0x537854` | `0x49ADB4` | `0x100239E4C` | `0x2396B6` |

对应 descriptor vtable：`transState` 为 A64 `0x19FB3E8`、A32 `0x10AB410`、I64 `0x101AEF0B0`、I32 `0x1839490`；`interface` 为 A64 `0x19FC168`、A32 `0x10ABAD0`、I64 `0x101AEFE30`、I32 `0x1839B50`。

## 业务体边界与副作用矩阵

包装器只决定“能否进入业务体、如何转参和如何回写”；下表是进入业务体后的真实行为。两层不能混写。

| 业务体 | A64 | A32 | I64 | I32 | 精确语义 |
|---|---:|---:|---:|---:|---|
| `getChildren` | `0x529418` | `0x491DA0` | `0x10022FAC0` | `0x22EC24` | 只按 live `FrontItems` 遍历；owner snapshot非空且 `IsValid(...) == 1` 才把 live `ScriptOwner` field作为Object/ObjThis直接emplace到native Array Items；无`PropSetByNum`，不遍历`BackItems`。 |
| `getClearColor` | `0x52B7EC` | `0x492C56` | `0x100230D1C` | `0x22FC14` | 直接读构造未初始化的 `uint32` 字段；第一次 setter 前没有确定默认值。 |
| `setClearColor` | `0x529614` | `0x491F54` | `0x10022FD28` | `0x22EE68` | 先写本地 `ClearColor`，再在相邻 guessed scalar pointer 非空时执行 `*pointer = color`；不 release target、不写 root-state byte、不请求更新。 |
| `getTransState` | `0x52B7F4` | `0x492C5A` | `0x100230D24` | `0x22FC18` | 读 `float` 字段并扩展成 `double`。 |
| `setTransState` | `0x52B800` | `0x492C68` | `0x100230D30` | `0x22FC26` | 在 `double` 域执行 `min(max(x,0),1)`，再存 `float`；NaN 保留。 |
| `add` | `0x52B82C` | `0x492CA8` | `0x100230D58` | `0x22FC5E` | 从参数对象解包 `D3DLayerBase` adaptor，再进插入 helper；解包失败可把 null 传给 helper。 |
| `remove` | `0x52B8B0` | `0x492D00` | `0x100230DBC` | `0x22FC92` | 解包成功且 native 非空才分别删 front/back；任一删除成功后先调 child detach/invalidate hook，再调 root changed hook。 |
| `startTransition` | `0x529628` | `0x491F60` | `0x10022FD3C` | `0x22EE74` | 从 options 对象读取 method/vague/rule；不存在 method 时选普通模式；末尾激活并把 state 置 1。 |
| `update` | `0x52B978` | `0x492D58` | `0x100230E20` | `0x22FCC6` | 严格顺序：存 `UpdateState` → root changed hook → Window 非空时 `RequestUpdate`。 |
| `checkEnable` | `0x52B9C0` | `0x492D7C` | `0x100230E68` | `0x22FCEA` | 忽略已转换字符串，返回 false。 |
| `getModule` | `0x52B9C8` | `0x492D80` | `0x100230E70` | `0x22FCEE` | 忽略已转换字符串，返回 Integer 0 Variant。 |
| capture callback thunk | `0x52B9D8` | `0x492D8C` | `0x100230E80` | `0x22FCFA` | typed callback 到主 vtable 的 capture 实现。 |
| capture implementation | `0x531468` | `0x495778` | `0x100233FA8` | `0x232CA8` | 不添加 target/main-image 等空保护，也没有异常回滚；正常尾部 release target 并把 `CurrentTarget` 清空。 |
| `getPrimaryLayers` | `0x52A2B4` | `0x4925AC` | `0x1002304FC` | `0x22F430` | snapshot Managers begin/end，严格解引用primary layer；owning owner getter先AddRef一次，再把cached owner作为Object/ObjThis直接emplace到native Array Items；owner可空，每个non-null owner每次调用永久泄漏1 ref。 |
| `setLayerManagerIndex` | `0x52A498` | `0x492684` | `0x100230610` | `0x22F58C` | 负数或 `index >= size` 抛出 `"invalid layer manager index."`；成功只存索引。 |
| `getPrimaryLayerBitmap` | `0x52A4DC` | `0x4926AC` | `0x100230654` | `0x22F5B8` | V272纠正：唯一no-op是`manager->GetDrawDeviceData()`返回null；非空item后先转换target，再读取item构造期缓存的PrimaryLayer，source/main-image/texture均严格裸指针解引用。 |
| `setPrimarySize` | `0x52BA54` | `0x492DE0` | `0x100230EF8` | `0x22FD52` | 先无条件存 width/height，再在 Window 非空时调 `NotifySrcResize`。 |
| `setScreenRect` | `0x52BA98` | `0x492E0C` | `0x100230F38` | `0x22FD80` | 总是先存 left/top；仅尺寸变化时存 width/height、依次释放 FrontTarget/BackTarget，并向第四个 root-state byte 写 1。 |
| `setForceRenderTexture` | `0x52BA38` | `0x492DCE` | `0x100230EE0` | `0x22FD40` | 即使值没变也存值，并向第四个 root-state byte 写 1；四端插件代码区没有该 byte 的读取。 |

补充的 getter/setter 地址需要区分；旧笔记曾把 getter 地址误标为 setter，并把
`screenWidth/screenHeight` setter 误标成 `screenLeft/screenTop`。四端重新反编译后的正确映射为：

- `primaryWidth` getter/setter：A64 `0x52BA78`/`0x52BA80`，A32 `0x492DF4`/`0x492DFA`，I64 `0x100230F18`/`0x100230F20`，I32 `0x22FD68`/`0x22FD6E`。
- `primaryHeight` getter/setter：A64 `0x52BA88`/`0x52BA90`，A32 `0x492E00`/`0x492E06`，I64 `0x100230F28`/`0x100230F30`，I32 `0x22FD74`/`0x22FD7A`。
- `screenLeft` getter/setter：A64 `0x52BB10`/`0x52BB18`，A32 `0x492E56`/`0x492E5C`，I64 `0x100230F88`/`0x100230F90`，I32 `0x22FDB0`/`0x22FDB6`。
- `screenTop` getter/setter：A64 `0x52BB20`/`0x52BB28`，A32 `0x492E62`/`0x492E68`，I64 `0x100230F98`/`0x100230FA0`，I32 `0x22FDBC`/`0x22FDC2`。
- `screenWidth` getter/setter：A64 `0x52BB30`/`0x529A94`，A32 `0x492E6E`/`0x49222C`，I64 `0x100230FA8`/`0x10022FF68`，I32 `0x22FDC8`/`0x22F0BE`。
- `screenHeight` getter/setter：A64 `0x52BB38`/`0x529AF8`，A32 `0x492E72`/`0x492268`，I64 `0x100230FB0`/`0x10022FFF0`，I32 `0x22FDCC`/`0x22F108`。

其中 `primaryWidth/primaryHeight` 和 `screenLeft/screenTop` 的单字段 setter 都只是直接 store；
只有 `screenWidth/screenHeight` 的单字段 setter 执行“值变化才 release 双 target + 写 state byte”。
- `bicubicParam` getter/setter：A64 `0x52BA20`/`0x52BA28`，A32 `0x492DBC`/`0x492DC2`，I64 `0x100230EC8`/`0x100230ED0`，I32 `0x22FD2E`/`0x22FD34`；字段类型是 `float`。

`interface` getter 则不是普通成员字段读取。它返回真实多重继承对象的 `tTVPDrawDevice` 次基类地址，调整量为 A64 `+0x178`、A32 `+0xD4`、I64 `+0x118`、I32 `+0xA4`；getter 业务体分别为 `0x52BA4C`、`0x492DDA`、`0x100230EF0`、`0x22FD4C`。

## 源码与测试落点

本轮恢复落在：

- `cpp/plugins/DrawDeviceD3D.cpp`
  - `checkEnable(ttstr)` 和 `getModule(ttstr)` 恢复 typed 签名；
  - `getModule` 返回 Integer 0 Variant；
  - `transState` 使用 double clamp 后再存 float；
  - `getPrimaryLayers` 去除参考中不存在的 primary-layer/owner 防御分支；
  - `getPrimaryLayerBitmap` 只保留 manager-data-item-null 分支；source来自item构造期缓存，
    不是重新调用manager `GetPrimaryLayer()`；
  - `update` 保持 store → changed → window update 顺序。
- `tests/unit-tests/plugins/motionplayer-dll.cpp`
  - 覆盖缺参、精确参数和多余参数；
  - 覆盖 `membername`、null receiver、错误 native receiver 对 result 的不同影响；
  - 覆盖 RW/RO property descriptor 的拒绝顺序；
  - 覆盖 `transState` NaN 往返。

## 被否定的旧假设

| 旧假设 | 四参考结论 |
|---|---|
| `checkEnable()` / `getModule()` 是零参数 stub | 二者均为一参数 `ttstr` typed method；缺参返回 `-1004`。 |
| `getModule` 返回 Void | 成功返回 `tvtInteger(0)`。 |
| 固定签名方法必须恰好 N 个参数 | 只要求至少 N 个；额外参数忽略。 |
| 所有错误都会先清空 result | member-name 与 null receiver 错误发生在清空之前。 |
| RO `PropSet` 会先检查 receiver/value | setter 缺失检查更早，直接返回 `-1007`。 |
| `transState` 先窄化 float 或用分支 clamp | 在 double 域组合 `min/max`，最后存 float；NaN 保持。 |
| `primaryLayers` 会跳过 null primary layer 或 null owner | primary layer 被严格解引用；null owner 作为 null-object Variant 追加。 |
| `getPrimaryLayerBitmap` 对整个调用链做空保护 | 仅manager data item为空时返回；item非空后target/source/main-image/texture链严格，详见V272。 |
| capture 应人为加异常清理 guard | 四份业务体没有该 guard；只能复原已观察到的正常尾部清理。 |

## IDB 固化状态

四份 recovery IDB 均已完成并保存：

- 28 个本轮业务体、方法 descriptor 和属性 descriptor 的语义命名；
- 每个函数一条边界/副作用注释，共每库 28 条；
- 每库 5 个书签，覆盖 `getModule` 业务体、`getModule` typed wrapper、四参数 `setScreenRect` wrapper、`transState.PropGet` 和只读 `interface.PropSet`。

这样下一轮可以从已命名入口继续追 factory precondition、native adaptor 获取和 manager 回调，而不必重新从无符号模板实例辨认公开表面。
