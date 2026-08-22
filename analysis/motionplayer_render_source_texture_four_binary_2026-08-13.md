# Player render-source texture 与 D3DAdaptor 软件副本树四端复核（2026-08-13）

## 范围与函数映射

本轮从 Player 已准备 render item 的源描述符出发，追到 Layer native 转换、
`iTVPTexture2D` 提取、软件渲染器静态副本创建，以及 `D3DAdaptor` 内部有序树的
插入/命中/清理。四个当前参考目标和配套 IDB 均可读，目标函数映射如下：

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| Player render-source texture getter | `0x6EE440` | `0x5AC518` | `0x10014019C` | `0x1414C0` |
| 软件纹理树 `emplace` helper | `0x6EE778` | `0x5AC700` | `0x1001403FC` | `0x141736` |
| strict Layer Variant wrapper | `0xA7959C` | `0x79AFCE` | `0x10035FF10` | `0x36366C` |
| strict Layer Object/native helper | 同函数内联 | `0x79AFF0` | `0x10035FF40` | `0x36368C` |
| cached software-renderer helper | `0x848BDC` | `0x65728C` | `0x100323EB8` | `0x32930C` |
| D3DAdaptor 构造 | `0x6AAEF0` | `0x57D0AC` | `0x100103FA8` | `0x10128C` |
| 目标纹理 release helper | `0x6AB058` | `0x57D16E` | 析构内联 | 析构内联 |
| 软件纹理树递归清理 | `0x6D8C38` | `0x59A8CE` | `0x1001285E4` | `0x127928` |
| `removeAllTextures` | `0x6AAC98` | `0x57CF74` | `0x100103D58` | `0x101138` |

四份 getter 已命名为 `Player_getRenderSourceTexture_guess`，四份插入 helper 已
命名为 `D3DAdaptor_softwareTextureMapEmplace_guess`。二进制没有留下足以证明的
原始 C++ 符号名，因此保留 `_guess`。

## 四端共同数据流

四端 getter 的共同源级控制流为：

```text
if sourceState.texture != null:
    return sourceState.texture

resourceManager = nullable native ResourceManager from persistent findSource Variant
if resourceManager != null:
    Player_loadKrkrAtlasSource(sourceState, resourceManager, ...)
    if sourceState.texture != null:
        return sourceState.texture

layerVariant = descriptor bridge(sourceState / prepared item)
layer = tTJSNI_Layer::FromVariant(layerVariant)       // strict
source = layer.GetMainImage().GetTexture()            // both calls unguarded

if !TVPIsSoftwareRenderManager():                     // cached first result
    return source

found = d3dAdaptor.softwareTextureCopies.find(source)
if found != end:
    return found.mappedHolder.GetObjectNoAddRef()

copy = OpenGLRenderManager.CreateTexture2D(
    source.GetScanLineForRead(0),
    source.GetPitch(),
    source.GetWidth(),
    source.GetHeight(),
    source.GetFormat(),
    STATIC /* 1 */)
d3dAdaptor.softwareTextureCopies.emplace(source, intrusiveHolder(copy))
return copy
```

四端还共同固定了更细的求值顺序：先取得 private OpenGL manager，再依次查询
`scanline -> pitch -> width/height -> format`，最后才调用 factory。当前实现用显式局部
变量表达这一顺序，不依赖 C++ 函数实参的未指定求值顺序。完整指令地址和异常边界见
`motionplayer_d3d_adaptor_software_texture_emplace_commit_four_binary_2026-08-15.md`。

这里的 `OpenGLRenderManager` 不是当前 process-default manager。四端 miss 路径都
经过函数局部静态缓存，第一次按宽字符串名字 `"opengl"` 取得专用 manager，以后
复用该指针。Android Hex-Rays 把同一宽字面量截断显示成 `"o"`，但原始字符宽度、
具名 renderer helper 和其余目标共同确认完整名字。软件后端作为默认 renderer 时，
这一区分正是 CPU source 上传到 GPU/D3D texture 的必要数据流；本地不能简化成
无参 `TVPGetRenderManager()`。

第一段 atlas retry 不是每次都重新解析脚本：已有 descriptor texture 具有最高
优先级；只有它为空时才尝试从持久的 `findSource` ResourceManager Variant 取得
nullable native pointer，并调用 atlas loader。atlas loader 若回填 texture，getter
立即返回；只有仍为空时才走 descriptor-to-Layer bridge。

ResourceManager 转换在这一步是 nullable 风格：native query 状态失败或输出为空
都会让 atlas retry 被跳过。它不能推广到下一步 Layer 转换；四端 Layer 路径调用
的都是引擎 strict helper。

## strict Layer 链与空值边界

Layer fallback 的异常/崩溃时序由四端共同证明：

1. 非 Object Variant 在 `AsObjectNoAddRef` 转换时抛出；
2. 非空 Object 的 `NativeInstanceSupport(GETINSTANCE, Layer::ClassID, ...)` 返回失败
   时抛出 `TVPSpecifyLayer`；
3. typed-null Object 或“query 成功但输出 Layer 为空”会从 helper 得到 null，getter
   随后无 guard 调用 `GetMainImage`，产生原生空指针解引用；
4. `GetMainImage()` 返回 null 时同样没有 guard，下一次 `GetTexture()` 立即解引用
   空指针；
5. `GetTexture()` 返回 null 时，GPU 分支可以正常返回 null；软件分支会先用 null
   key 查询 map，miss 后无 guard 调用 source 的纹理虚函数而崩溃。

另一个已定型的 caller 会对 `GetMainImage()` 结果做显式 null test，说明 getter 中
缺少该 test 不是 Hex-Rays 漏显，而是这个函数自身的真实边界。

本地此前存在 nullable Layer resolver 和 `image ? texture : nullptr` 安全化。四端
证据否定了这两层分支；当前 `textureFromLayerVariant` 已改为 strict
`tTJSNI_Layer::FromVariant` 后无条件执行 `GetMainImage()->GetTexture()`。

## 软件纹理副本的容器与 mapped holder

四端对象内都是以 source texture 指针比较的 `std::map` 红黑树：

```cpp
std::map<iTVPTexture2D *, tTJSRefHolder<iTVPTexture2D>>
```

key 只是借用的身份指针，没有 AddRef/Release。mapped node 只保存一个纹理
指针，但构造时无条件 AddRef，析构/清树时无条件 Release；命中返回这个指针而
不新增引用。这与仓库核心已有 `TJS::tTJSRefHolder<T>` 的构造、复制、析构和
`GetObjectNoAddRef()` 语义逐项一致，也解释了为什么 mapped type 不是裸指针或
带 null guard 的 `tRefPtr`。

创建失败的边界因此也不是“缓存 null”：`CreateTexture2D` 若返回 null，mapped
holder 在插入时就无条件调用 `copy->AddRef()`，在节点成功入树之前产生空指针
解引用。

GNU libstdc++ 两端和 libc++ 两端在 duplicate insertion 的内联展开不同：

- Android/GNU 先分配并构造候选节点（mapped texture AddRef），再查找插入点；
  key 已存在时释放 mapped texture 并删除候选节点；
- iOS/libc++ 先完成 key lookup，只在确认缺失后才分配节点并 AddRef mapped
  texture。

这不是插件源码分支，而是同一双参数 `std::map::emplace(source, copy)` 经两套 STL
实现产生的对象生命周期差异。mapped holder 以其非 explicit raw-pointer 构造器在
node 内构造；本地保留同一调用形状，让目标 STL 自己决定 duplicate 候选时机。

getter 在 emplace 后不读取 helper 返回的 node 或 inserted flag，而是无条件返回本次
factory 产生的 `copy`。因此如果 source/factory callback 在 pre-find 与 emplace 之间重入
并先插入同一 key，本次 emplace 保留 existing node，却返回新建的未缓存 copy；下一次
普通 hit 又返回 existing mapped copy。GNU duplicate 路径会 AddRef 后再 Release candidate
holder，libc++ duplicate 路径不会构造 candidate holder，但两端都不回收 factory creation
reference。跨线程访问则没有锁，属于 C++ data race，不是受支持的同步协议。

## 工厂引用与原实现的残留 creation ref

四端直接证明的引用顺序是：

```text
copy = CreateTexture2D(...)  // raw return
map mapped-holder construction: copy.AddRef()
caller: no copy.Release()
removeAllTextures/map destruction: copy.Release() once
```

同一个 `D3DAdaptor` 构造函数对 target texture 使用同一类工厂调用：它把返回值
直接存进 `_targetTexture`，不 AddRef；析构或 replacement 路径随后恰好 Release
一次。这独立证明工厂返回值携带一份 creation-owned reference，而不是从 refcount
0 开始等待 holder 接管。

因此可推导软件 copy 插入后的引用至少为“工厂 creation ref + map holder ref”；
清树只放掉 holder ref，getter caller 也没有释放 creation ref。也就是说，参考
实现会留下工厂创建引用。这里“caller 没有 Release”和“map 只 Release 一次”是
反编译直接事实；将其解释为残留/泄漏则建立在同一纹理工厂已由 target 路径证明
的所有权契约上。本地忠实保留该顺序，没有用额外 `copy->Release()` 静默修复。

## `removeAllTextures` 和析构顺序

四端 `removeAllTextures` 都递归销毁树节点，对每个 mapped holder 调用一次纹理
Release，删除节点并恢复空树 header。source key 不释放。析构源级顺序保持为：

```text
removeAllTextures()
release target texture
release Window dispatch
destroy already-empty map object
```

软件缓存 hit 不更新 LRU，也不比较纹理内容、尺寸或版本；同一个 source 指针在
显式清树前始终返回第一次创建的静态副本。

## 本地复原与验证

- `cpp/plugins/motionplayer/SourceCache.cpp`
  - 移除 nullable `resolveNativeLayer`；
  - Layer fallback 改为 strict conversion；
  - 移除 MainImage null guard，恢复无条件 `Layer -> MainImage -> Texture` 调用链。
- `cpp/plugins/motionplayer/D3DAdaptor.h/.cpp`
  - map mapped type 从 null-safe `tRefPtr` 改为非空安全的
    `TJS::tTJSRefHolder<iTVPTexture2D>`；
  - hit 使用 `GetObjectNoAddRef()`；
  - miss 无 source guard，piecewise 构造 mapped holder；
  - miss 通过 function-local static 缓存具名 `"opengl"` manager，而不是使用当前
    默认 software manager；
  - 用 locals 固定 manager、scanline、pitch、size、format 的原版回调/异常顺序；
  - 保留 emplace inserted result 被忽略、duplicate call 返回未缓存新 copy 的边界；
  - 不释放 factory creation ref；
  - `removeAllTextures` 由 map clear 触发 holder Release。

Wasmtime debug 的 `SourceCache.cpp` 与 `D3DAdaptor.cpp` 两个目标对象重新编译
成功，完整 MotionPlayer Catch2 翻译单元也以 Web Debug 的真实 Emscripten 参数通过
`-fsyntax-only`；唯一诊断是仓库既有 `_tss` literal-operator 弃用警告。

2026-08-15 已重新核对四份 live recovery IDB：四端 map-emplace helper 均已命名为
`D3DAdaptor_softwareTextureMapEmplace_guess`，iOS 两端独立 find helper 也已命名；getter、
candidate lifecycle 与 duplicate commit 注释/书签均写入，四次 `idb_save` 分别返回
`ok=true`。此前记录的 IDA worker 超时缺口已经闭合。
