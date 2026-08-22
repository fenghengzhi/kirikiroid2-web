# Player persistent source workspace 四参考生命周期审计（2026-08-14）

## 1. 范围与结论

本文闭合 `Player` 内部六个连续 `tTJSVariant` 槽组成的持久 source workspace：两个
`ResourceManager` owner、descriptor Dictionary、primary internal Layer、color Dictionary
和 secondary work Layer。重点是它们的构造顺序、引用图、lazy materialization、渲染调用链、
半初始化边界和析构顺序；绝对地址只保留在本文和 recovery IDB，不写回编译源注释。

四份当前参考共同支持的源级结构为：

```cpp
tTJSVariant findSourceResourceManager;
tTJSVariant sourceCacheObject;
tTJSVariant sourceDescriptor;       // persistent Dictionary
tTJSVariant internalRenderLayer;    // primary Layer; initially Void
tTJSVariant sourceColors;           // persistent Dictionary
tTJSVariant internalSourceWorkLayer;// secondary Layer; initially Void
```

其中 descriptor 和 colors 在 `Player` 构造期只创建一次，之后每个 prepared render item
覆盖这两个同一对象中的属性。两个 Layer 延迟创建，但 lazy gate **只检查 primary 槽是否为
Void**；primary 又在尺寸探测、`setSize` 和 work Layer 创建之前发布。因此任何 primary 发布后的
失败都会留下不可由后续调用自动修复的粘滞半初始化状态。这是四端一致的原生边界。

## 2. 参考样本与函数映射

| 样本 | SHA-256 |
|---|---|
| `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | `05E2FF4C77F1561608AD7703153D2FB09855BF223237A85DC2267FFF1388564F` |
| `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | `A15C238EC6F21C17D0889B064AE1AD47EC85B4F1530A3611F206B7190FF456AF` |
| `Kirikiroid2_1.3.9_iOS_arm64` | `733BA5D3FD0798E41DDBAC0F0A5B484E7CD20443EE5313781E0E32D1633E18E3` |
| `Kirikiroid2_1.3.9_iOS_armv7` | `733BA5D3FD0798E41DDBAC0F0A5B484E7CD20443EE5313781E0E32D1633E18E3` |

iOS 两项是同一个 fat image 的独立 architecture slice，因此文件哈希相同。

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `Player` ctor | `0x6CC110` | `0x5935C4` | `0x10011EC04` | `0x11D488` |
| materialize workspace | `0x6CB57C` | `0x592F7C` | `0x10011E2BC` | `0x11CAC8` |
| regular post-draw update | `0x6CBBB8` | `0x59327C` | `0x10011E6CC` | `0x11CF20` |
| accurate SLA post-draw update | `0x6CBD18` | `0x593344` | `0x10011E808` | `0x11D078` |
| shared render-source resolver | `0x6BEF50` | `0x58AD94` | `0x1001143E0` | `0x111E08` |
| `Player` dtor | `0x6CCEBC` | `0x593C24` | `0x10011F2A0` | `0x11DCC4` |

recovery IDB 中对应名字为 `Player_materializeInternalRenderLayers_guess`、
`Player_updateLayerAfterDraw_guess`、`Player_updateAccurateSLAAfterDraw_guess` 和
`Player_resolveRenderSource_guess`。

## 3. 六槽精确布局

| 槽 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| find-source RM | `+0x27C` / `+636` | `+0x1AC` / `+428` | `+0x20C` / `+524` | `+0x16C` / `+364` |
| source-cache RM | `+0x290` / `+656` | `+0x1B8` / `+440` | `+0x220` / `+544` | `+0x178` / `+376` |
| descriptor | `+0x2A4` / `+676` | `+0x1C4` / `+452` | `+0x234` / `+564` | `+0x184` / `+388` |
| primary Layer | `+0x2B8` / `+696` | `+0x1D0` / `+464` | `+0x248` / `+584` | `+0x190` / `+400` |
| colors | `+0x2CC` / `+716` | `+0x1DC` / `+476` | `+0x25C` / `+604` | `+0x19C` / `+412` |
| work Layer | `+0x2E0` / `+736` | `+0x1E8` / `+488` | `+0x270` / `+624` | `+0x1A8` / `+424` |
| canonical RM（非连续尾槽） | `+0x3E0` / `+992` | `+0x2AC` / `+684` | `+0x370` / `+880` | `+0x26C` / `+620` |

Android arm64 和 iOS arm64 的 Variant 槽宽为 `0x14`；Android armv7 和 iOS armv7 为
`0x0C`。这解释了绝对偏移差异，不改变字段顺序或语义。canonical RM 位于后续字符串字段
之后，不属于六槽连续 workspace，但构造函数把同一输入 dispatch CopyRef 到它，故本文在
表中列出其相对位置。

## 4. 构造数据流和引用图

四端构造函数的共同顺序为：

```text
CopyRef(rm) -> find-source RM member
CopyRef(rm) -> source-cache RM member
default construct descriptor/primary/colors/work as Void
...
CopyRef(rm) -> canonical RM member

descriptorRaw = TJSCreateDictionaryObject()
sourceDescriptor = Object(descriptorRaw, descriptorRaw)
colorsRaw = TJSCreateDictionaryObject()
sourceColors = Object(colorsRaw, colorsRaw)
sourceDescriptor.PropSet(MEMBERENSURE, "color", sourceColors)
Release(colorsRaw)
Release(descriptorRaw)
```

关键点不是只有最终值，而是两份 factory creation-return raw owner 都跨过整个 `color`
`PropSet` 存活，正常尾部再按 `colors -> descriptor` 顺序释放。四端对应尾部为：

| raw owner release | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| colors | `0x6CC5E4..0x6CC5F4` | `0x5938D2..0x5938E8` | `0x10011EF4C..0x10011EF5C` | `0x11D9D6..0x11D9E4` |
| descriptor | `0x6CC634` | `0x5938F0..0x5938FE` | `0x10011EF70..0x10011EF80` | `0x11D9F2..0x11DA00` |

正常尾部释放 factory owners 后的引用图是：

```text
Player.sourceDescriptor ───────────────► descriptor Dictionary
                                           │ property "color"
                                           ▼
Player.sourceColors ───────────────────► colors Dictionary
```

也就是说 colors 至少有两个长期 owner：成员 Variant 和 descriptor property；descriptor 有成员
Variant owner。raw factory owners 只是构造期的额外层，不能在各自第一次成员赋值后立即释放，
否则会改变 `PropSet` 抛错时的展开次序和存活引用数。

iOS armv7 的 SJLJ cleanup 从 `0x11DA22` 开始，明确把已经构造的 workspace Variant 纳入
逆序 teardown；Android arm64 的 landing-pad 尾链也逐槽回收。恢复实现因此使用局部 raw
dispatch guard，并让声明顺序产生 `colors -> descriptor` 的析构顺序；成员 Variant 仍由 C++
构造失败展开独立回收。

## 5. Lazy materializer 的精确状态机

四端可以统一成下列伪代码：

```text
if primary.Type != Void:
    return

targetObject = strict object(target)
window = targetObject["window"]

primary = new Layer(owner=window, parent=target)
publish internalRenderLayer = primary

height = probeMustExistThenGetInteger(target, "height", missing=0)
width  = probeMustExistThenGetInteger(target, "width",  missing=0)
primary.setSize(width, height)       // HRESULT ignored

work = new Layer(owner=window, parent=target)
publish internalSourceWorkLayer = work
work.setSize(width, height)          // HRESULT ignored
```

primary/work 发布点分别为：

| publish | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| primary | `0x6CB708` | `0x592FE4` | `0x10011E368` | `0x11CBA6` |
| work | `0x6CB94C` | `0x5930D2` | `0x10011E4B0` | `0x11CCF0` |

尺寸读取不是简单 `PropGet`：先以 `TJS_MEMBERMUSTEXIST` 探测；探测返回失败则该维直接取
零；成功才执行普通 get 并转整数。顺序固定为 `height` 后 `width`，但 `setSize` 参数顺序仍为
`width, height`。两个 `setSize` 的 HRESULT 都不影响后续控制流。

2026-08-16 的 accessor source-identity 复核进一步确认，这两次尺寸读取复用函数开头从完整
target Variant 构造的同一个 `ncbPropAccessor`；probe 与 ordinary get 都通过其
`HasValue(name,hint)` / `GetValue<tjs_int>(name,Tag,0,hint)` 模板，并共用对应的 process-wide
member-hint slot。helper 只借用 accessor，不另行 AddRef/Release。详见
`motionplayer_internal_workspace_dimension_ncb_accessor_four_binary_2026-08-16.md`。

### 5.1 半初始化边界

| 中断/失败时点 | 留下的状态 | 下次 materialize |
|---|---|---|
| primary 发布前 | primary/work 都是 Void | 会重试 |
| primary 发布后、尺寸完成前 | primary Object；work Void | 立即返回，不修复 |
| primary `setSize` 后、work 发布前 | primary 已尝试 sizing；work Void | 立即返回，不修复 |
| work 发布后、work `setSize` 前 | 两槽都是 Object；work 未完整 sizing | 立即返回，不修复 |
| 全部完成 | 两槽均为同 owner/parent、同初始尺寸 Layer | 立即返回，不重新同步尺寸 |

因此该 helper 不是 transaction，也没有 rollback/retry flag。即使目标 Layer 后来改变尺寸，
materializer 也不会因尺寸变化重建或 resize workspace。primary-match resolver 又直接假设 work
已经存在；半初始化路径可能在后续 `assignImages`/属性读取中自然失败。

## 6. Post-draw 更新链

regular 和 accurate SLA 路径都先无条件执行：

```text
internalRenderLayerReady = needsInternalAssignImages
if !needsInternalAssignImages: return
materialize(target)
```

随后二者分叉：

- regular：`primary.assignImages(target)`；
- accurate SLA：重新读取目标 `height/width`，再执行
  `primary.piledCopy(0, 0, target, 0, 0, width, height)`。

两条路径都只消费 primary，不直接触碰 work；work 专供 shared source resolver 的内部来源
染色。两条路径也都不会清 `needsInternalAssignImages`。`internalRenderLayerReady` 是 producer
flag 的当帧快照，anchor type 10 在后续帧读取它；它不是 workspace 完整性标志。

## 7. Shared render-source resolver 与五后端调用链

五种渲染后端最后都进入同一个 Player resolver；不存在 D3D、PrivateMotionGLL 或 SLA 各自
维护一套 source workspace 的情况。

| caller 类别 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| build render commands | `0x6C2A44` | `0x58CBB6` | `0x100116C80` | `0x1147B2` |
| render to canvas | `0x6C4E70` | `0x58E5C8` | `0x1001189FC` | `0x116DAE` |
| accurate SLA renderer | `0x6C77F0` | `0x590A7C` | `0x10011AF44` | `0x11945A` |
| PrivateMotionGLL route/wrapper | `0x6DBF1C` | `0x59CD18` | `0x10012C178` | `0x12ACE0` |
| D3DAdaptor texture getter | `0x6EE560` | `0x5AC5C2` | `0x100140278` | `0x1415C6` |

每个 caller 在进入 resolver 前，覆盖 persistent descriptor 的 `key`、`src`、`blendMode`，
并按数值索引 `0..3` 覆盖 persistent colors。packed color 先按 `uint32_t` 读取，再零扩展到
TJS Integer payload；descriptor/colors 是跨调用复用的 mutable dictionaries，不是 per-item
snapshot。

resolver 的分支条件精确为：

```text
source.Type == Object
&& primary.Type == Object
&& source.Object == primary.Object
```

比较的是 dispatch pointer；没有额外 non-null gate，因此两个 typed-null Object 也可判等。

internal fast path：

```text
blendMode = descriptor["blendMode"]
colors[0..3] = sourceColors[0..3].AsInteger()
result = work.assignImages(primary)
height = probeMustExistThenGetInteger(work, "height", missing=0)
width  = probeMustExistThenGetInteger(work, "width",  missing=0)
tint(work, rect=(0,0,width,height), colors,
     alphaMode=((blendMode & 0xF0) == 0x10))
return result
```

2026-08-16 的 accessor-chain 复核进一步确认 fast path 依次构造 descriptor、color、work 三个
`ncbPropAccessor`：blendMode 是一次 hinted named `GetValue<tjs_int>`，colors 是四次无 probe 的
indexed `GetValue<tjs_int>`，只有 work height/width 使用 hinted `HasValue` + second `GetValue`
双读；正常析构严格 work→color→descriptor。详见
`motionplayer_resolve_source_ncb_accessor_chain_four_binary_2026-08-16.md`。

fallback path 只有：

```text
return sourceCacheObject.loadSource(source, sourceDescriptor)
```

fast path 不调用 cache，也不克隆 descriptor；work 先从 primary `assignImages`，再原地施加四角
tint。`assignImages` 的返回 Variant 是 resolver 返回值，work member 自己保留 Layer owner。

## 8. 正常析构与 property graph teardown

六槽按声明逆序析构，四端均为：

```text
work -> colors member -> primary -> descriptor member
     -> source-cache RM -> find-source RM
```

| 析构槽序列 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| workspace/RM cleanup span | `0x6CD0D0..0x6CD0F8` | `0x593CFC..0x593D24` | `0x10011F394..0x10011F3BC` | `0x11DE18..0x11DE4A` |

`colors` member 虽先于 descriptor 释放，但 descriptor 的 `color` property 仍持有 colors
Dictionary；到 descriptor member 随后释放、Dictionary teardown 其 property graph 时，才释放
这份最后的内部引用（除非脚本端另有 alias）。因此不能把两个 Dictionary 改成互不关联的
native map，也不能在 colors member 析构时主动 clear descriptor property。

canonical RM 位于更后声明位置，故它在到达这组六槽之前已经按其真实布局顺序释放；三份 RM
Variant 是同一 dispatch 的独立 CopyRef owner。V257 进一步证明 Player 尾部 raw pointer 是
未初始化的 dead load residual dispatch，不是 SourceCache fast pointer；portable render helper
现从稳定的 retained RM owner 按需 unwrap native ResourceManager，不再增加缓存字段。

## 9. 本地恢复与 IDB 改进

本轮实施：

- 将 helper 从过时的单一 Android arm64 地址式名字改为
  `materializeInternalRenderLayers_guess`、`resolveRenderSource_guess`、
  `loadRenderSourceLayerFromItem_guess`、`loadRenderSourceTextureFromItem_guess` 等四参考语义名；
- 保留 primary-only gate、publish-before-sizing 和无 rollback 行为；
- 用两个局部 dispatch release guard 让 ctor creation-return owners 跨过 `color` PropSet，并在
  正常/异常路径都按 `colors -> descriptor` 释放；
- 在 `Player.h` 写清六槽声明顺序、persistent dictionary 引用和 primary/work lazy 语义；
- 四份 recovery IDB 声明 `PlayerSourceWorkspace_guess` 槽布局，重命名 materializer、两条
  post-draw 更新和 resolver，并在 ctor raw release、primary/work publish、dtor 处加入注释。

## 10. 验证

验证结果：

- `cmake --build out/web/debug --parallel 1` 完整成功，生成 `index.html/index.wasm`；
- 随后相同命令增量复跑得到 `ninja: no work to do`；
- 独立以 Web compile database 的 motionplayer flags 对
  `tests/unit-tests/plugins/motionplayer-dll.cpp` 执行 `-fsyntax-only`，成功；
- `git diff --check` 成功；
- 仅出现仓库既有的 `_tss` literal-operator、pthread memory-growth、JSPI experimental 和
  Emscripten JS-library warning，没有本轮新增编译 warning。

首次最终链接曾因 `index.wasm` 瞬时 `permission denied` 停止；motionplayer 的 14 个编译/
归档步骤当时已经完成。未结束任何用户进程、未删除输出，直接重跑最终链接即成功，因此该
瞬时文件占用不构成源代码或链接符号失败。

## 11. 仍未闭合

- target `window`、共享 Layer constructor、四端 landing-pad/SjLj 临时量矩阵以及
  publish 后不回滚边界，已在
  `motionplayer_shared_layer_factory_exception_lifecycle_four_binary_2026-08-15.md` 闭合；
- persistent dictionaries 被脚本外部 alias 后，在 Player 析构之外的最终释放时点；
- 内部 work Layer 四角 tint helper 已闭合其 packed-color 数学，但 GPU/软件 Layer 操作抛错时
  是否保留部分像素结果仍属于 Layer 后端专门纵切。

这些未知项不影响六槽布局、引用图、lazy gate、sticky partial-state、共享 resolver 和正常
析构顺序的四端结论。
