# DrawDeviceD3D 七类 NCB 表面四端对账

## 结论

`DrawDeviceD3D.dll` 在四个参考二进制中都按同一顺序发布七个脚本类：

1. `DrawDeviceD3D`
2. `D3D`
3. `D3DLayer`
4. `D3DImage`
5. `D3DPicture`
6. `D3DEmoteModule`
7. `D3DEmotePlayer`

本报告闭合前六个类的完整 NCB 表面、具体 factory / constructor 边界以及
独立 ClassInfo/adaptor 身份；`D3DEmotePlayer` 的 4 常量、54 成员、typed
factory、clone、析构和 7 个故意 TODO 已由
`motionplayer_d3demoteplayer_surface_factory_clone_todo_four_binary_2026-08-27.md`
独立闭合。

四端共同结论：

- `DrawDeviceD3D` 与 `D3D` 发布相同顺序、相同绑定目标的 33 个有名成员，
  但两者的 ClassInfo tuple、class ID、factory descriptor、concrete adaptor、
  concrete vtable 和 deleting destructor 均独立；共享脚本表面不能推导为同一
  native identity。
- `D3DLayer` 发布 3 个常量和 7 个成员；factory 只消费 arg0，通过内部
  `D3DLayerBase` identity 解包 root，忽略 surplus。
- `D3DImage` 发布 3 个成员；factory 与 `D3DLayer` 使用相同的 root-base
  identity，但构造不同的独立 concrete adaptor。
- `D3DPicture` 发布 9 个成员；typed factory 严格要求
  `(D3DLayer, D3DImage)`，只消费前两个参数，忽略 surplus。
- `D3DEmoteModule` 发布 7 个成员；零参数构造器接受所有非负 argc 并忽略
  argv，只有 exact-one-Void 进入生成 descriptor 的 empty-adaptor sentinel。
- 七个 concrete class registration 都有独立 Regist/Unregist 路径；四个集成
  loader 都没有 module-unload / registered-set erase 调用者，因此运行期不会
  回收已发布的 tuple 和 global class。

现有本地实现和用例与上述表面及边界一致，本切面不需要语义 C++ 修改。

## 静态注册根与类顺序

| 目标 | 静态记录 | 类记录中的共同顺序 |
|---|---:|---|
| Android arm64 | `0x42CBD8` | DrawDeviceD3D → D3D → D3DLayer → D3DImage → D3DPicture → D3DEmoteModule → D3DEmotePlayer |
| Android armv7 | `0x2FF094` | 同左 |
| iOS arm64 | `0x10024CB00` | 同左 |
| iOS armv7 | `0x24E6D8` | 同左 |

四库分别对 `DrawDeviceD3D.dll`、`DrawDeviceD3DZ.dll`、七个 class name 和
本报告的所有成员名执行 UTF-16LE 含终止符原始字节搜索。总计 68 个本切面
相关名字在每一库均至少有一个完整命中，没有 missing pattern，所有搜索 cursor
均为 `done=true`。短名字可能因常量池 suffix folding 出现在长字符串内部；因此
字符串存在性只用于确认精确字节和大小写，成员归属与顺序另由完整 registrar
反编译/反汇编确定。

## 六个成员注册器

| 类 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `DrawDeviceD3D` | `0x52A618`（990） | `0x492790`（326） | `0x10023070C`（297） | `0x22F622`（358） |
| `D3D` | `0x52BC18`（990） | `0x492F10`（326） | `0x100230FF0`（297） | `0x22FDFA`（358） |
| `D3DLayer` | `0x52CE8C`（275） | `0x49345C`（93） | `0x100231618`（85） | `0x230408`（107） |
| `D3DImage` | `0x52D768`（134） | `0x493950`（38） | `0x100231AFC`（35） | `0x230932`（41） |
| `D3DPicture` | `0x52DCE0`（316） | `0x493BBC`（92） | `0x100231DD0`（83） | `0x230B86`（102） |
| `D3DEmoteModule` | `0x52E388`（264） | `0x493E54`（82） | `0x100232078`（75） | `0x230DB0`（90） |

括号内为完整指令数。24 个函数全部 fresh decompile，且以最大 50000 指令的
单函数读取完整 disassembly，24 个 cursor 均 `done=true`；没有用截断的
Hex-Rays 文本代替 registrar 分母。Android arm64 对两个 33-name registrar
大量内联 NCBind descriptor 构造，因此指令数显著高于另外三端，但有名成员和
绑定回调序列不变。

## `DrawDeviceD3D` / `D3D` 共同表面

两类各有一个独立 raw factory，再按下表顺序发布 33 个名字：

| # | 名字 | 形式 / 绑定 |
|---:|---|---|
| 1 | `children` | read-only property → `getChildren` |
| 2 | `clearColor` | read/write property |
| 3 | `transState` | read/write property |
| 4 | `add` | method → borrowed `D3DLayerObject` lookup |
| 5 | `remove` | method → borrowed `D3DLayerObject` lookup |
| 6 | `startTransition` | method |
| 7 | `stopTransition` | method |
| 8 | `update` | method |
| 9 | `checkEnable` | method |
| 10 | `getModule` | method |
| 11 | `capture` | method |
| 12 | `offsetX` | read/write property |
| 13 | `offsetY` | read/write property |
| 14 | `setOffset` | method |
| 15 | `stretchType` | read/write property |
| 16 | `bicubicParam` | read/write property |
| 17 | `forceRenderTexture` | read/write property |
| 18 | `interface` | read-only property，绑定 native draw-device interface pointer |
| 19 | `setPrimarySize` | method |
| 20 | `primaryWidth` | read/write property |
| 21 | `primaryHeight` | read/write property |
| 22 | `setScreenRect` | method |
| 23 | `screenLeft` | read/write property |
| 24 | `screenTop` | read/write property |
| 25 | `screenWidth` | read/write property |
| 26 | `screenHeight` | read/write property |
| 27 | `primaryLayers` | read-only property |
| 28 | `layerManagerIndex` | read/write property |
| 29 | `getPrimaryLayerBitmap` | method |
| 30 | `destLeft` | read-only property |
| 31 | `destTop` | read-only property |
| 32 | `destWidth` | read-only property |
| 33 | `destHeight` | read-only property |

`DrawDeviceD3D` 和 `D3D` registrar 在四端都引用同一批 native member callback；
差异只发生在 factory、具体 vptr 和 concrete ClassInfo/adaptor family。特别是：

- `D3DLayerBase` 是额外的 sticky borrowed root view，不能替代任一 concrete ID；
- 一个 `DrawDeviceD3D` shell 不能作为 `D3D` concrete receiver，反向亦然；
- Android linker 可能折叠字节相同的 complete destructor，但 deleting destructor、
  vtable 和 NCB identity 仍然独立，这只是 dead-strip/ICF 形态差异。

## 两个 root raw factory

| 目标 | `DrawDeviceD3D` factory | `D3D` factory |
|---|---:|---:|
| Android arm64 | `0x52B654`（99） | `0x52CC54`（99） |
| Android armv7 | `0x492BFC`（27） | `0x49337C`（33） |
| iOS arm64 | `0x100230C88`（32） | `0x10023156C`（37） |
| iOS armv7 | `0x22FB28`（70） | `0x230300`（78） |

八个 factory 都 fresh decompile 并完整 disassemble。共同源级行为：

```text
if argc < 2:
    return TJS_E_BADPARAMCOUNT

width  = to_int32(argv[0])
height = to_int32(argv[1])
fresh  = new ConcreteRoot(width, height, objthis)
publish *result only after construction succeeds
return TJS_S_OK
```

surplus 不转换。生成的 raw native-class descriptor 另有 exact-one-Void
empty-adaptor sentinel；普通 attach 失败会删除 fresh root 并返回
`TJS_E_NATIVECLASSCRASH`。四工具链对转换/构造异常的 landing-pad ownership
存在已知 ABI 差异，但正常返回、结果发布时机和脚本错误码一致；这些差异没有被
portable 代码误修成一个统一 RAII 行为。

## `D3DLayer`

注册顺序：

| # | 名字 | 值 / 形式 |
|---:|---|---|
| 1 | `DrawPlaneFront` | constant `1` |
| 2 | `DrawPlaneBack` | constant `2` |
| 3 | `DrawPlaneBoth` | constant `3` |
| 4 | `visible` | read/write property |
| 5 | `frontIndex` | read/write property |
| 6 | `backIndex` | read/write property |
| 7 | `drawPlane` | read/write property |
| 8 | `setMatrix` | 16-float method |
| 9 | `setMatrixGL` | 16-float method |
| 10 | `setClip` | 4-float method |

factory 四端定位与完整指令数为 `0x52D308`（63）、`0x49361C`（65）、
`0x1002317E8`（48）、`0x230594`（91）。它要求 `argc >= 1`，arg0 必须是
Object，并用独立 `D3DLayerBase` class ID 做 `GETINSTANCE`；失败或 null root
返回 `TJS_E_INVALIDTYPE`。成功后只消费 arg0，surplus 被忽略。exact-one-Void
在 descriptor 层返回 concrete empty adaptor，Void+surplus 则离开 sentinel 并按
ordinary arg0 被拒绝。

## `D3DImage`

注册顺序为：read-only `width`、read-only `height`、method `load`。

factory 四端定位与完整指令数为 `0x52D98C`（109）、`0x4939F8`（71）、
`0x100231BE8`（53）、`0x2309DC`（89）。arity、Object gate、
`D3DLayerBase` unwrap、surplus 和 exact-one-Void 行为与 `D3DLayer` 相同；
成功时构造独立 `D3DImage`，把 raw pointer 加入 root managed-object set，
再由 concrete non-sticky adaptor 持有 image。

## `D3DPicture`

注册顺序：

1. `opacity` read/write property；
2. `blendMode` read/write property；
3. `stretchType` read/write property；
4. `bicubicParam` read/write property；
5. `assignImageRange`；
6. `clearImageRange`；
7. `setCoord`；
8. `setScale`；
9. `getScale`。

typed factory 的 attach / construct 完整定位：

| 目标 | attach | construct/unbox |
|---|---:|---:|
| Android arm64 | `0x53F140`（68） | `0x53F258`（64） |
| Android armv7 | `0x4A1014`（58） | `0x4A10D0`（78） |
| iOS arm64 | `0x10024227C`（48） | `0x100242374`（69） |
| iOS armv7 | `0x2420A0`（88） | `0x2421E4`（112） |

普通构造要求至少两个参数；arg0 严格解包为 `D3DLayer`，arg1 严格解包为
`D3DImage`，错误 native type 会抛出而不是静默传 null。只消费前两项，surplus
被忽略。构造出的 `0x60/0x40` listener shell 先注册到 layer；concrete adaptor
attach 失败时调用 deleting destructor，因此会注销 listener 再释放 shell。
exact-one-Void 仍由外层 descriptor 直接产生 empty adaptor；加入第二个参数就会
离开 sentinel 并进入严格转换。

原生默认值也由四端 constructor 一致确认：`opacity=255`、`blendMode=2`、
listener `stretchType=8`、`bicubicParam=-0.5f`、scale `1.0f`，ranges 为空。

## `D3DEmoteModule`

注册顺序：

1. `maskMode` read/write property；
2. `maskRegionClipping` read/write property；
3. `mipMapEnabled` read/write property；
4. `alphaOp` read/write property；
5. `protectTranslucentTextureColor` read/write property；
6. `pixelateDivision` read/write property；
7. `setMaxTextureSize` method。

零参数 constructor entry / construct-attach 完整定位：

| 目标 | entry | construct/attach |
|---|---:|---:|
| Android arm64 | `0x5416A8`（44） | `0x54177C`（142） |
| Android armv7 | `0x4A3060`（48） | `0x4A30F0`（65） |
| iOS arm64 | `0x100244A08`（34） | `0x100244AA8`（50） |
| iOS armv7 | `0x244EF8`（35） | `0x244F64`（87） |

entry 仅拒绝负 argc；所以 argc 0 正常构造，任意 surplus（包括 Void+surplus）
均被忽略。argc==1 且唯一参数为 Void 时，descriptor 在分配前返回 empty adaptor。
默认 native 为 `0x20/0x1C`，四端共同值为 `maskMode=1`、
`maskRegionClipping=false`、`mipMapEnabled=true`、`alphaOp=false`、
`protectTranslucentTextureColor=false`、`pixelateDivision=100`，其余 pointer/cache
字段为 null。attach 失败调用 deleting destructor 并返回 `-1008`。

## `D3DEmotePlayer`

本类在同一七类链中，但其 4 constants + 54 member surface、factory、clone、
TODO 和完整 listener 生命周期已在 MP-A30 报告逐项列出。这里重新确认其 class
record 紧跟 `D3DEmoteModule`，并在 `DrawDeviceD3D_PreRegist` 之前注册；它属于
`DrawDeviceD3D.dll`，不是 `emoteplayer.dll` 的 `Motion.EmotePlayer` 注册表面。

## 本地对照与验证

| 参考要求 | 本地位置 | 结论 |
|---|---|---|
| 七类顺序和 module name | `cpp/plugins/DrawDeviceD3D.cpp:12`、`:1652` | 匹配 |
| 两个 root 的独立类与 raw factory | `cpp/plugins/DrawDeviceD3D.cpp:1258`、`:1288` | 匹配 |
| root 共用 33-name surface | `cpp/plugins/DrawDeviceD3D.cpp:1614` | 匹配 |
| `D3DLayer` constants / 7 members | `cpp/plugins/DrawDeviceD3D.cpp:1675` | 匹配 |
| `D3DImage` 3 members | `cpp/plugins/DrawDeviceD3D.cpp:1697` | 匹配 |
| `D3DPicture` factory / 9 members | `cpp/plugins/DrawDeviceD3D.cpp:1490`、`:1708` | 匹配 |
| `D3DEmoteModule` constructor / 7 members | `cpp/plugins/DrawDeviceD3D.cpp:1731` | 匹配 |
| `D3DEmotePlayer` 54 members | `cpp/plugins/DrawDeviceD3D.cpp:1753` | 由 MP-A30 闭合 |

现有 unit-test TU 中的直接表面/边界用例包括：

- `tests/unit-tests/plugins/motionplayer-dll.cpp:8781`：七个全局 class、两个内部
  non-global native identity、root 33-name surface；
- `:8846`：`DrawDeviceD3D` / `D3D` 独立 raw factory、Void sentinel、surplus、
  foreign concrete adaptor failure；
- `:9655`：`D3DLayer` / `D3DImage` arity、type、sentinel 和 root-base unwrap；
- `:9857`：`D3DPicture` 两参数 strict factory、surplus、默认值和成员调用；
- `:15354`：`D3DEmoteModule` zero-arg、surplus 和 exact-one-Void。

本轮实际执行了四端 fresh decompile、完整 disassembly、68-name raw-byte matrix、
class string xref、factory/constructor 对照、本地逐行比较和 IDB 固化。四个 IDB
均已统一命名关键 registrar/factory，添加注释与书签并原位保存。Android arm64
的 `DrawDeviceD3D` delayed wrapper 入口位于一个 noreturn-adjacent 位置，IDA 当前
把它误并入前一 deque helper；工具不提供无损 function resize，因此保留原字节和
函数对象，只在真实 vtable entry 添加行注释/书签，没有用破坏性 undefine 重建。

## disposition

- 原始任务：`MP-A31`
- 静态状态：`CLOSED_STATIC`
- 覆盖切面：`MP-A31-DRAWDEVICED3D-SEVEN-CLASS-NCB-SURFACES`
- 本任务局部剩余差异：无
- 独立剩余工作：全项目所有注册字符串、默认参数与绑定目标的最终全局分母仍由
  `MP-A32` 跟踪；正式构建和运行时验证仍由 `MP-V01..V16` 跟踪。
