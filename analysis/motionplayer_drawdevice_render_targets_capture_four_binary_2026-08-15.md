# MotionPlayer DrawDevice render targets / capture 四参考二进制恢复

日期：2026-08-15

## 结论

四个 `reference/binaries/` 参考实现共同证明，DrawDevice root 的三个 render-target
字段承担不同角色：`FrontTarget` 与 `BackTarget` 是 `Show()` 跨帧复用的双缓冲；
`CurrentTarget` 是绘制期间临时发布给 child 的 borrowed 指针。独立 `capture()` 每次另建
一张 texture，先发布到 `CurrentTarget`；后半段不保留创建返回值作为权威 local，而是为
software/GPU Layer 转交及最终 Release 多次重读 live `CurrentTarget`，正常完成后才清零。
四份 root 构造函数都不初始化 `CurrentTarget`；它在第一次发布前是 indeterminate，
而不是 null。正常 root 绘制入口会在任何 child `Draw` 前覆盖它，因此这个构造边界不会
改变普通 `capture`/`Show` 的调用链，但仍属于必须一比一保留的对象状态。

本轮还纠正了一个会改变虚调用 ABI 的旧推断：`D3DLayerObject::Draw` 接收的是
一个按 `const &` 传递的 8 字节 `{ float x; float y; }` point，不是
`iTVPTexture2D *target`。四端都把 root 的两个 32-bit float 字段原样复制到栈上，
没有 `FCVT` 或整数转换；原始类型拼写已剥离，所以恢复源码保守命名为
`D3DPoint_guess`，不能再套用整数 `tTVPPoint`。具体 `D3DLayer` 忽略该 point，转而从 Parent 读取
`CurrentTarget`，再把 texture 传给 `D3DLayerListener::Draw(iTVPTexture2D *)`。

## 1. 四端函数映射

| 函数 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| root `capture` | `0x531468` | `0x495778` | `0x100233FA8` | `0x232CA8` |
| root `Show` | `0x531890` | `0x495978` | `0x100234294` | `0x232F1C` |
| `D3DLayer::Draw` | `0x533624` | `0x496EC6` | `0x100235AAC` | `0x2348B2` |
| Layer `AssignTexture` | `0x8071A0` | `0x6308A8` | `0x10007A164` | `0x772FC` |
| texture `Update` wrapper | `0x7FC094` | `0x62BC68` | `0x100409CFC` | `0x3F1B2A` |
| `setPrimarySize` | `0x52BA54` | `0x492DE0` | `0x100230EF8` | `0x22FD52` |
| `setScreenRect` | `0x52BA98` | `0x492E0C` | `0x100230F38` | `0x22FD80` |
| `setScreenWidth` | `0x529A94` | `0x49222C` | `0x10022FF68` | `0x22F0BE` |
| `setScreenHeight` | `0x529AF8` | `0x492268` | `0x10022FFF0` | `0x22F108` |

独立 FrontItems 绘制 helper 在 Android armv7 `0x4963F8`、iOS arm64
`0x100234ECC`、iOS armv7 `0x233B08`；Android arm64 把等价循环内联进调用者。
独立 target release helper 在 iOS arm64 `0x10022FFA4`、iOS armv7 `0x22F0DC`；
Android 两端把相同的 Front-then-Back body 内联。

## 2. `capture(targetLayer, frontIndexLimit)` 精确数据流

四端共同顺序可归纳为：

```text
UpdateObjects_guess(0)   // one shared Variant; does not read/clear root UpdateState

CurrentTarget = renderManager.CreateTexture2D(
    null, 0, uint32(primaryWidth), uint32(primaryHeight), RGBA, 0)
offset = { OffsetX, OffsetY }

for child in FrontItems ordered traversal:          // pointer multiset; comparator reads child.frontIndex
    if child.IsVisible()
       && (frontIndexLimit == 0 || child.frontIndex < frontIndexLimit)
       && (child.drawPlane & 1) != 0:
        child.Draw(offset)

layer = strict Layer conversion(targetLayer)
if software renderer:
    source = CurrentTarget.GetPixelData()  // 第一次重读 live field
    pitch  = CurrentTarget.GetPitch()      // 第二次重读，可为另一 texture
    sizeTarget = CurrentTarget             // 第三次重读
    width  = sizeTarget.GetWidth()
    height = sizeTarget.GetHeight()
    layer.SetSize(width, height)
    image = layer.GetMainImage()        // exactly once
    image.Update(source, pitch, 0, 0, width, height)
else:
    layer.AssignTexture(CurrentTarget)     // 再次重读 live field

CurrentTarget.Release()                    // Layer handoff 后再次重读
CurrentTarget = null
return true
```

边界行为同样是恢复目标的一部分：

- 创建前没有正尺寸检查；signed primary 尺寸按原生参数 ABI 转为 unsigned；
- capture 的 fanout 固定传 integer 0，完全不消费 root UpdateState；callback 中的
  `update(newState)` 会保持 pending，留给下一次 Show；
- 一整轮 fanout 共用同一个 Variant，live tree successor 在 callback 返回后才计算；callback
  抛出时先析构 Variant并退出，texture 创建尚未发生；
- texture 创建结果没有 null guard，创建失败会在之后自然崩溃；
- 新 target 不执行 `FillARGB`，初始像素取决于 render manager；
- visibility 判断先于 index/draw-plane 判断；index 门槛是严格 `<`，零表示不限；
- target Layer 是绘制完成后才严格转换，错误对象不会阻止之前的绘制副作用；
- software 路径调用 `GetPixelData` 而非旧注释中的 `GetScanLineForRead(0)`；pixels、pitch 与
  width/height 前分别重读 live `CurrentTarget`，所以重入替换可让三组数据来自不同 texture；
  路径没有 source/pitch/main-image guard，也不是逐行手工 `memcpy`；
- GPU 路径直接调用 BaseLayer 的 `AssignTexture`；
- 函数没有 RAII cleanup。child Draw、Layer 转换、Update 或 AssignTexture 抛异常时，
  `CurrentTarget` 保持最后一次 live publication；最初创建的 texture 与当前 field 可能已经不同。
  只有正常返回路径才重读 field、Release 该当前值并清零。重入替换会使最初 texture 泄漏，
  也可能让正常尾部 Release 一个并非本次 Create 所拥有的 pointer。

上述 `GetPixelData` 与 live-field 多次重读由 V274 对四份 canonical IDB 的 fresh 指令复核纠正；
此前本文及 portable 源码把后半段写成稳定 local `target`，属于旧报告过时结论。完整 V274 证据见
`motionplayer_drawdevice_capture_live_currenttarget_pixel_handoff_four_binary_2026-08-22.md`。

## 3. `Show()` 的双缓冲分配与复用

`Show()` 会先 snapshot `UpdateState` 并执行对象更新，只有整轮 fanout 与共享 Variant析构正常
完成才清零字段，之后才检查 Window。无 Window时仍会遍历可见 front item、调用 `OnUpdate`
并在成功时消费 update state；callback 抛出则不清零。Window gate 只阻止
LayerManager item settings 同步、target ensure、绘制和 present。通过 gate 后先按
Managers vector 顺序同步各 item settings，再进入 target ensure 逻辑。先前把 Window
误写成入口第一道 gate 的注释已经由四份当前参考二进制纠正。

共享 Variant、live tree cursor、capture 非消费和 Show success-only commit 的完整 V269 证据见
`motionplayer_root_updateobjects_shared_variant_tree_iterator_updatestate_commit_four_binary_2026-08-21.md`。

复用门槛只读取 `BackTarget`：

```text
if BackTarget != null
   && BackTarget.width  >= uint32(primaryWidth)
   && BackTarget.height >= uint32(primaryHeight):
    reuse FrontTarget and BackTarget
else:
    Release/null FrontTarget
    Release/null BackTarget
    FrontTarget = CreateTexture2D(primaryWidth, primaryHeight, RGBA)
    BackTarget  = CreateTexture2D(primaryWidth, primaryHeight, RGBA)
```

因此：

- 它完全不检查 `FrontTarget` 是否存在、尺寸是否足够或是否与 BackTarget 匹配；只要
  BackTarget 通过 gate，后续就会直接使用当前 FrontTarget；
- 扩容会重建，缩小 primary size 会保留较大的 texture，属于 grow-only 复用；
- 释放顺序固定 Front 后 Back，创建顺序也固定 Front 后 Back；
- 创建 Front 成功、创建 Back 失败时没有 rollback，root 会保留部分发布状态；
- 创建完成后没有 `BackTarget` null guard；
- `ReleaseTargets` 只处理 Front/Back，不读取、Release 或清零 `CurrentTarget`。
- 根析构沿用同一边界；`StartBitmapCompletion` 也不消费 `CurrentTarget`，而是从传入
  manager 的 draw buffer 取得 render manager、render target 与 reference texture。

target ensure 后，`Show` 会在 active 判断前用 guarded statics 缓存 `FillARGB` method 与
`color` 参数 ID。transition 分支每帧只调用一次 `SetParameterColor4B(clearColor)`，再从
FrontTarget 读取一次宽高形成 full rect，把同一个 rect 依序用于 FrontTarget 与
BackTarget 的 Fill；它不是“每个 target 单独 lookup、设色、计算矩形”。随后
`CurrentTarget` 依次指向两者并绘制对应 map；
非 transition 分支只把 BackTarget 发布为 CurrentTarget 并遍历 FrontItems。正常尾部清零
CurrentTarget，并把 BackTarget 交给 form 的 `UpdateDrawBuffer`。中间虚调用或 render
manager 操作抛异常时同样没有 finally cleanup，CurrentTarget 会停留在最后发布的 target。

## 4. 屏幕尺寸与 primary 尺寸失效规则

`setPrimarySize(width,height)` 直接保存两个 primary 字段；Window 非 null 时调用其第一个
虚槽 `NotifySrcResize`。它不直接释放 render targets，也不写第四个 root-state byte。单独的
`primaryWidth` / `primaryHeight` setter 更窄：只 store，不通知、不失效 target。

`setScreenRect(left,top,width,height)` 总是先保存 left/top。只有 width 或 height 变化时才：

1. 保存新的 screen width/height；
2. Release/null FrontTarget；
3. Release/null BackTarget；
4. 向第四个 root-state byte 写入 1。

`setScreenWidth` 和 `setScreenHeight` 分别执行同样的“变化才释放双 target + 写 state byte”规则。
screen size 变化会强制重建，但重建 texture 的实际尺寸仍取 primaryWidth/primaryHeight，
而不是 screenWidth/screenHeight。所有这些失效路径都不处理 CurrentTarget。

后续对四份当前二进制的完整插件代码区逐指令复查纠正了这里的命名置信度：该 byte
在 A64/I64 主基类 `+0x43`、A32/I32 `+0x2B`，每端都只有上述三个 screen writer 和
`setForceRenderTexture` 共四个 `store 1`，没有 load/read。它很可能是 render-target
invalidation/dirty 状态，但当前产物没有消费者可以确认原字段名或消费语义；恢复源码因此
使用 `RenderTextureDirty_guess`，不再把“dirty”当成无 `_guess` 的已证实名称。

## 5. Draw ABI 与 concrete child 消费者

root 的 capture、transition-front、transition-back 和普通 Show 循环都会在各自栈帧构造
`D3DPoint_guess{OffsetX,OffsetY}`，再调用
`D3DLayerObject::Draw(const D3DPoint_guess&)`。这里的 `OffsetX/OffsetY` 是 float，
脚本属性和 `setOffset` 也都走 floating-point NCB 适配器。

具体 `D3DLayer::Draw` 的顺序为：

```text
ignore offset
if Parent == null || !Visible:
    return
target = Parent.CurrentTarget
for listener in List insertion order:
    listener.Draw(target)
```

它不检查 CurrentTarget，也不再次调用 listener `IsVisible()`。另一方面，
`DrawDeviceManagerItem::Draw` 也忽略 root point；该对象从自己的 Layer 脚本属性读取
`offsetX/offsetY`。目前两个已确认的 concrete D3DLayerObject 类型都不消费 root point，
但 ABI 仍必须保留，不能因现有派生类忽略它而删去参数。

## 6. 本轮恢复改动与验证

`cpp/plugins/DrawDeviceD3DIntf.h` / `DrawDeviceD3D.cpp` 已据四端证据：

- 把 D3DLayerObject Draw 虚槽修正为 point-reference ABI；
- capture 恢复 point 发布、严格 Layer conversion、software `GetPixelData -> GetPitch ->
  direct dimensions -> MainImage->Update` 与 GPU `AssignTexture`，并删除旧 graceful guards /
  手工复制；V274 进一步恢复各阶段和最终 Release 对 live `CurrentTarget` 的独立重读；
- ReleaseTargets 不再错误清零 CurrentTarget；
- Show 删除不存在的 BackTarget 创建后 guard，并保留只看 BackTarget 的复用门槛。

Web Debug 完整构建通过。四份 recovery IDB 已补 capture/Show、Draw、screen/primary setter、
release/front-loop helper 的 `_guess` 名称、函数注释和书签，并成功保存。

## 7. 保留的不确定性

- 私有 helper 原始拼写不可恢复，故继续使用 `EnsureTargets`、`ReleaseTargets` 或 `_guess`
  语义名；函数行为和调用点已闭合。
- 本轮已确认 root 的参考布局还包含一个独立 `tTVPDrawDevice` 次基类和第二 vptr；Web
  源码已通过真正的双继承、第二 vptr、adjusted interface 指针和完整析构顺序恢复；
  详细证据见同日的 multiple-inheritance/vtable 专项文档。
