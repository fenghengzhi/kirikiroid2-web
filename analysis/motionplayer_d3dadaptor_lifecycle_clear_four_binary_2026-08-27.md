# D3DAdaptor 构造/析构、进程共享实例与 clear 静态缓存四参考二进制联合恢复

日期：2026-08-27

## 1. 本 slice 闭合的根

本轮把 D3DAdaptor 从“公开 callback 已闭合”推进到对象生命周期根：

1. 唯一的五参数 native 构造函数及其异常清理；
2. 析构函数的 map、target texture、Window 逆向释放顺序与 terminate 边界；
3. Android 保留、iOS dead-strip 的独立 `releaseTargetTexture` helper；
4. `Player::draw` 内联的进程级 shared D3DAdaptor 创建/发布/永久保留；
5. `clearTargetTexture` 的 `clearEnabled` gate、两个独立函数静态 guard、borrowed render
   method、矩形构造和 in-place render operation。

四端证据还证明本地默认构造与二阶段 `initialize_guess` 只是测试便利，不属于参考 source
shape：每个平台的参数构造函数都只有 Factory 与 shared-Player 两个 native 调用者。因而本轮
删除这组额外 API，并让所有测试直接使用唯一的五参数构造。

## 2. 构造函数等价类与字段初始化

| 平台 | constructor | 正常/内含 cleanup 指令 | 独立 unwind cleanup | 两个调用点 |
|---|---:|---:|---:|---|
| Android arm64 | `0x6AAEF0` | 48 | 内含 `0x6AAF94` 起 map cleanup | Factory `0x6AAB44`；Player `0x6D3540` |
| Android armv7 | `0x57D0AC` | 44 | `0x57D11E`，5 条 | Factory `0x57CF34`；Player `0x597980` |
| iOS arm64 | `0x100103FA8` | 46 | `0x100104060`，6 条 | Factory `0x100103CFC`；Player `0x100123E28` |
| iOS armv7 | `0x10128C` | 90 | `0x101376`，14 条 | Factory `0x1010C2`；Player `0x1230E4` |

共同源级顺序：

```text
D3DAdaptor(window, width, height, centerX, centerY):
    construct scalar/default members and empty ordered map
    this.width   = width
    this.height  = height
    this.centerX = centerX
    this.centerY = centerY
    this.window  = window

    if window != null:
        window.AddRef()

    this.targetTexture = privateOpenGLRenderManager.CreateTexture2D(
        pixels=null, pitch=0,
        width=width, height=height,
        format=RGBA, flags=0)
```

四端明确初始化以下共同状态：dormant int32 为 0、`visible=false`、
`canvasCaptureEnabled=false`、`clearEnabled=true`、`resizable=false`、
`alphaOpAdd=false`、clearColor 为 0、target 为 null，并建立空的有序 map header。参数字段在
调用 Window `AddRef` 前已经全部发布。

### 2.1 source 同一、STL ABI 不同

成员到 target 为止在同指针宽度平台一致；map ABI 令最终对象大小不同：Android arm64
shared callsite 分配 `0x68`，iOS arm64 分配 `0x50`；Android armv7 分配 `0x40`，iOS
armv7 使用更紧凑的 map 布局。应保留共同的
`std::map<iTVPTexture2D *, tTJSRefHolder<iTVPTexture2D>>` source，而不是在本地复制任何
一个平台的红黑树 padding/sentinel 布局。

### 2.2 构造失败与尖锐 owner 边界

Window 是 raw retained pointer，不是 RAII holder。构造体先 `AddRef` Window，再创建 target；
如果 Window `AddRef` 或 target 创建抛出，四端 unwind 只析构已经构造的空 map，然后继续
unwind。它不会 `Release` Window slot。因此 target 创建失败会遗留刚取得的 Window 引用。

这不是推荐的现代所有权结构，但正是参考边界。本地构造函数通过 raw `_window` member 与
body 内 `AddRef` 保持同一 partial-construction 行为；不能把 Window 改成普通 ref-holder，
否则 C++ 自动 unwind 会“修复”原版泄漏。

private OpenGL target factory 返回 null 时没有本地检查：null 会被发布为已完成构造的
target slot，后续
render/capture/clear 才会在各自的无检查解引用点失败。

## 3. 析构函数与 reverse owner 顺序

| 平台 | destructor | 完整指令数 | map-dtor helper | terminate cleanup |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6AAFCC` | 35 | `0x6AAFB0` | 函数内 `0x6AB03C..0x6AB054` |
| Android armv7 | `0x57D12E` | 22 | `0x59A934` | `0x57D160`，5 条 |
| iOS arm64 | `0x1001040A0` | 31 | `0x100104078` | `0x10010411C`，5 条 |
| iOS armv7 | `0x1013BC` | 71 | `0x1013A8` | `0x101470`，11 条 |

共同正常路径：

```text
~D3DAdaptor():
    softwareTextureCopies.clear()

    if targetTexture != null:
        targetTexture.Release()
        targetTexture = null

    if window != null:
        window.Release()
        // deliberately do not clear the raw slot

    softwareTextureCopies.~map()  // normally sees the already-empty tree
```

因此 map 在正常析构中出现两次 tree erase：第一次是显式 `removeAllTextures()`，它逐 value
释放 software-copy holder 并把 map 复位为空；第二次是隐式 member destructor，正常只检查
空 root。target slot 在 Release 后写 null，Window slot 在最终 Release 后不写 null。

析构函数是 non-throwing 生命周期边界。iOS armv7 明确为 target/Window Release 建立 SjLj
call-site 状态；异常路径先运行 map member cleanup，再进入 terminate。其他 ABI 以 landing
chunk、DWARF cleanup 或直接 terminate helper 表达同一 source-level noexcept 结果。不能在
本地捕获并吞掉这些异常，也不能改变正常释放顺序。

Android arm64/armv7 还保留独立的 `releaseTargetTexture` 叶 helper
（`0x6AB058` / `0x57D16E`），均执行 null gate、Release、slot=null；iOS 链接结果将其
dead-strip/内联。这个差异是链接 disposition，不代表源级类在 iOS 上拥有不同 member。

## 4. Player 的进程级 shared D3DAdaptor

shared 对象不是函数静态 RAII owner，而是每平台 `.bss` 中零初始化的 raw pointer。创建
逻辑内联于 `Player::draw`：

```text
if globalSharedAdaptor == null:
    width  = MainWindow.GetWidth()
    height = MainWindow.GetHeight()
    storage = operator new(sizeof(platform D3DAdaptor))
    owner = MainWindow.GetOwnerNoAddRef()
    native = D3DAdaptor(owner, width, height,
                        width / 2, height / 2)
    globalSharedAdaptor = native

return globalSharedAdaptor
```

关键生命周期事实：

- 无 atomic、mutex 或 C++ static guard；并发首次进入存在原版竞争；
- width/height 在分配前采样，owner 在分配后、构造前取得；
- center 对负尺寸也使用 C++ signed `/ 2` 的向零截断；
- global slot 只在构造成功返回后发布；失败保持 null，后续 draw 会重试；
- 对象从不注册进程退出析构，也没有释放 root；成功创建后永久保留 Window 和 target；
- shared 创建发生在 SLA/ordinary target 分派与后续脚本调用之前；后续失败不会撤销已发布
  shared adaptor。

本地 `g_sharedD3DAdaptor_guess` 与提取出的 `ensureSharedD3DAdaptor` 保持这些数据/owner 边；
提取 helper 是可读性边界，不增加独立 owner 或 guard。

## 5. `clearTargetTexture` 与两个独立 function-local statics

| 平台 | clear body | 完整指令数 | guard-abort cleanup |
|---|---:|---:|---:|
| Android arm64 | `0x6AB08C` | 84 | body 内 `0x6AB1DC..0x6AB200` |
| Android armv7 | `0x57D184` | 100 | 编译结果无独立显式 guard-abort landing |
| iOS arm64 | `0x100104130` | 67 | `0x10010425C`，8 条 |
| iOS armv7 | `0x10149C` | 130 | `0x101638`，19 条 |

共同伪代码：

```text
clearTargetTexture(color):
    if !clearEnabled:
        return

    static method = privateOpenGLRenderManager.GetRenderMethod("FillARGB")
    static colorId = method.EnumParameterID("color")

    method.SetParameterColor4B(colorId, uint32(color))
    rect = {0, 0, targetTexture.width, targetTexture.height}
    emptySources = {}
    privateOpenGLRenderManager.OperateRect(
        method, targetTexture, targetTexture, rect, emptySources)
```

### 5.1 guard 和缓存生命周期

- `clearEnabled=false` 在读取任一 guard、manager、target 前立即返回；
- method pointer 与 colorId 使用两个不同 guard。method 初始化成功后立即发布；colorId
  初始化失败不会撤销已发布 method，下一次只重试第二 guard；
- Android arm64 与两个 iOS ABI 对正在初始化的 guard 有显式 abort+resume cleanup；Android
  armv7 的该编译结果没有显式 guard-abort landing；
- 两个 cached value 都是 trivial，退出时无 destructor，也不 Release render method；method
  是 borrowed/raw pointer；
- 初始化 method 时调用一次 Motion 私有 OpenGL manager getter，实际 OperateRect 前每次
  再调用同一 getter。callsite 不另存局部 static；getter 自身拥有 process cache。

### 5.2 render operation 边界

颜色按低 32 位原样传入；clearColor property 并不参与该函数。矩形 width/height 直接来自
target texture，left/top 固定 0。method 设置后没有锁、null 检查或回滚；OperateRect 的
source 与 target 是同一个 texture，source rect array 为空。manager/method/target 的任何
null 或异常都按调用顺序暴露，callback 不修复 target 状态。

## 6. 本地源结构改动和验证

- 删除 `D3DAdaptor()`、`initialize_guess` 和
  `initializeFromWindowObject_guess`，仅保留参考实现存在的五参数构造；
- 三处测试从 default+initialize 改为直接参数构造；software texture cache 测试使用 null
  Window 的参数构造，仍经过真实 target 创建/析构；
- 新增 compile-time 断言：D3DAdaptor 不可默认构造，且可由
  `(iTJSDispatch2*, int, int, int, int)` 构造；
- 本地参数构造、析构和 clear 正常 body 已与四端共同伪代码一致；clear 注释补充 Android
  armv7 无显式 guard-abort landing 的平台差异；
- Android armv7 两个原先未定义的 cleanup chunk 已恢复为函数；四端构造/析构/map-dtor/
  release/clear/cleanup 已命名、注释、书签并原位保存 IDB。

现有构造边界测试覆盖 Factory/Window gate、参数字段、默认 flags、target 尺寸与 setSize
不重建 target；capture、map holder 和 direct/shared render 调用由相邻 D3D/Player tests
覆盖。当前环境缺少 CMake、Ninja 和 Emscripten，且单头文件语法检查被缺失的
`boost/locale.hpp` 阻塞，因此本 slice 不宣称完成正式 native/Web 构建。

后续 `MP-R14-MOTION-PRIVATE-OPENGL-ENVELOPE` 已闭合私有 OpenGL manager root 与
`renderFromPlayer` envelope；`MP-R14-D3D-SOURCE-GETTER-MAP-INSERT` 又闭合了
software-source `getRenderTexture` 的查找/插入/失败 owner 边；
`MP-R14-D3D-DEEP-BATCH-STENCIL` 随后闭合 shared deep renderer 的逐 item、method、batch
与 stencil 状态，`MP-R14-D3D-MESH-SUBMIT-CELLS` 又闭合公共 mesh
submit/repeat/cell/AABB helper。构造、析构、clear、capture、software map和mesh生命周期
均已闭合；相邻剩余项是 Bezier basis/tessellation helper。
该相邻helper后来也由 `MP-R14-BEZIER-BASIS-TESSELLATION` 闭合。
