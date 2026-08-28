# Motion D3D source getter 与 software texture map 四参考二进制联合恢复

日期：2026-08-27

## 1. 结论

四个参考二进制的 D3D source getter 具有同一条源级数据流：先返回 descriptor 中已有的
atlas texture；未命中时再经 ResourceManager 尝试恢复 atlas；仍失败才把通用 Layer fallback
的 main-image texture 交给 D3DAdaptor。只有进程默认 renderer 为 software 时，fallback
texture 才进入 adaptor 的有序 map，并通过 Motion 私有 `opengl` manager 上传一份 static
texture。

map 的共同源级形状是：

```text
std::map<iTVPTexture2D *, tTJSRefHolder<iTVPTexture2D>>
```

key 是 borrowed raw identity；mapped value 是 intrusive holder。Android 的 libstdc++ 在
`emplace` 中先构造候选节点、再检查重复 key，iOS 的 libc++ 先搜索、只在 miss 时分配节点。
这是标准库实现边界，不是两套 motionplayer 源码。当前本地 `find`、private-manager upload、
`emplace` 和 raw return 已与共同语义一致。

## 2. 四端函数等价类

| 平台 | source getter invoke | 完整指令数 | map emplace | 完整指令数 | lookup / unwind 辅助 |
|---|---:|---:|---:|---:|---|
| Android arm64 | `0x6EE440` | 160 | `0x6EE778` | 77 | lookup 内联；通用 predecessor `0x1480D30`（28），RB link/rebalance `0x1480ED0`（112） |
| Android armv7 | `0x5AC518` | 157 | `0x5AC700` | 53 | insert-position `0x5AC7A8`（38），link/count `0x5AC800`（22） |
| iOS arm64 | `0x10014019C` | 118 | `0x1001403FC` | 57 | lookup `0x1001403A8`（21），link/count `0x1001404E0`（22） |
| iOS armv7 | `0x1414C0` | 196 | `0x141736` | 54 | lookup `0x1416FC`（27），link/count `0x1417B6`（21），SjLj cleanup `0x1416AC`（24） |

source getter 是 render envelope 所构造 `std::function` 的 invoke body。四端都完整读取了函数
反汇编与反编译；并另外完整读取上述 specialized lookup/link helpers。Android arm64 的两个
`0x148...` helper 是共享 libstdc++ 红黑树原语，因而只记录调用关系，不把它们误命名为
motion 专属函数。

## 3. 共同数据流与调用顺序

```text
getSourceTexture(item):
    source = item.sourceState
    if source.texture != null:
        return source.texture                         // borrowed atlas fast path

    rmDispatch = player.findSourceResourceManager.AsObject()
    rm = NCB GetNativeInstance(rmDispatch)
    moduleKey = ttstr(player.findMotionContextVariant)
    atlasLoaded = loadKrkrAtlasSource(source, rm, moduleKey)
    destroy moduleKey

    if atlasLoaded && source.texture != null:
        return source.texture                         // borrowed recovered atlas

    layerVariant = loadRenderSourceLayerFromItem(player, item)
    layer = Layer::FromVariant(layerVariant)
    sourceTexture = layer.GetMainImage().GetTexture() // GetMainImage applies font

    if !TVPIsSoftwareRenderManager():
        destroy layerVariant
        return sourceTexture                          // borrowed fallback texture

    if adaptor.softwareTextureCopies contains sourceTexture:
        destroy layerVariant
        return mappedHolder.GetObjectNoAddRef()       // borrowed map hit

    manager = getPrivateOpenGLRenderManager()
    pixels = sourceTexture.GetScanLineForRead(0)
    pitch = sourceTexture.GetPitch()
    width = sourceTexture.width
    height = sourceTexture.height
    format = sourceTexture.GetFormat()
    copy = manager.CreateTexture2D(
        pixels, pitch, width, height, format, STATIC)
    adaptor.softwareTextureCopies.emplace(sourceTexture, copy)
    destroy layerVariant
    return copy                                       // raw factory reference
```

顺序上的边界不可合并：

- 初始 `source.texture` fast path 在 software selector、ResourceManager 和任何临时 owner 之前；
- atlas helper 成功后必须再次检查 `source.texture`，且该返回仍绕过 software map；
- atlas helper 可以在失败时改写/清空 `source.object`，fallback 接收的是调用后的状态；
- fallback 先完成 Layer conversion、`GetMainImage`/ApplyFont 和 texture 取得，之后才检查默认
  renderer 是否为 software；
- software miss 先取得 Motion 私有 manager，再依次读取 scanline、pitch、width、height、
  format，最后以 flag `1` 创建 static texture；
- 私有 manager 决定 upload 去向；`TVPIsSoftwareRenderManager()` 仍只负责选择是否需要桥接。

## 4. 内部容器实现

### 4.1 共同红黑树语义

四端均以 source texture 的 raw pointer identity 做严格有序比较。反汇编中的比较是
32/64-bit unsigned pointer 比较；key 从不 `AddRef`、`Release` 或解引用。查找返回 node 或
header/end sentinel。只有唯一插入才链接节点、做红黑树 rebalance 并把 size 增加一；重复
key 不增加 size，也不替换已有 mapped holder。

节点 payload 布局由 ABI 决定：

| ABI | node 大小 | key | mapped intrusive holder |
|---|---:|---:|---:|
| 64-bit libstdc++ / libc++ | `0x30` | `+0x20` | `+0x28` |
| 32-bit libstdc++ / libc++ | `0x18` | `+0x10` | `+0x14` |

这些偏移属于本次二进制证据，不进入可移植 C++ 的手写 padding 或 ABI 模拟结构。

### 4.2 Android libstdc++ 的 eager candidate node

Android 两端先 `operator new`，把 key 与 mapped texture 写入候选节点，并立即对 mapped
texture 执行 intrusive `AddRef`。随后才重新搜索插入位置：

- unique：链接/rebalance，`size++`，候选节点成为 map 所有；
- duplicate：对候选 mapped holder 执行一次 `Release`，删除未使用节点，返回既有 node；
- Android arm64 直接内联这一判定；armv7 由 `0x5AC7A8` 返回既有 node/插入父位置，
  `0x5AC800` 完成 link 与 count。

### 4.3 iOS libc++ 的 lazy node construction

iOS 两端先在 `emplace` helper 内再次搜索 key。若已存在，直接返回原 node，不分配候选
节点，也不对传入 mapped texture `AddRef`。只有 miss 才分配节点、写入 key/mapped、执行
intrusive `AddRef`、link/rebalance 并 `size++`。

外层 getter 在 create 之前已经查过一次 map；内层重复检查是 `std::map::emplace` 自身的
正确性边界，不是业务层的第二份 cache。map 没有 mutex 或 guard；并发读写在源级本来就是
未定义行为，因此不能把 ABI 的 duplicate cleanup 当作线程安全协议。

## 5. 引用、对象生命周期与失败边界

- atlas fast path、recovered-atlas path 与非-software fallback 都返回 borrowed raw texture，
  不增加引用；
- ResourceManager extraction 使用严格 Variant `AsObject()`，取得的新 dispatch 引用没有在
  getter 中释放；每次进入 retry path 都保留这一 native 泄漏边；
- software map 的 key 只是 borrowed identity。map 生命周期不能延长 source texture 生命周期；
  键变成悬空 identity 后，树比较仍只比较数值，但业务层必须依靠清理时序避免错误复用；
- mapped holder 在 unique insertion 时增加一次 texture 引用；map hit 返回 holder 内 raw
  pointer，不再 `AddRef`；`removeAllTextures()`/析构清树时每个 mapped holder `Release` 一次；
- `CreateTexture2D` 的 factory creation reference 被保留在 raw local。成功插入后，首个 miss
  返回的 texture 同时有 factory reference 与 map-holder reference；后续 hit 只借用 holder
  reference；
- caller 没有释放首个 factory reference。因而 map clear 后，首个 miss 返回值仍可用，直到
  外部显式释放那一份 factory reference；这是当前单元测试精确保留的非对称所有权；
- factory 若正常返回 null，mapped holder 构造会无条件解引用 null 来 `AddRef`，没有安全返回；
- factory 成功后若 map node allocation 抛异常，raw `copy` 没有 RAII owner，factory reference
  泄漏；map 保持未插入。四端都没有 catch 或 texture rollback；
- iOS armv7 的 24 条 SjLj cleanup 只按构造状态销毁 live 的 TJS string/Variant 临时量并继续
  unwind，不释放 raw factory copy，也不回滚已完成的 map insertion；其他 ABI 以各自的
  DWARF/内联 owner cleanup 表达相同 C++ 临时对象语义。

## 6. 本地实现对照

本地关键映射为：

- `SourceCache::loadRenderSourceTextureForItem_guess`：descriptor/atlas/fallback 分流与临时
  module key；
- `D3DAdaptor::_softwareTextureCopies`：raw pointer key + `tTJSRefHolder` mapped value；
- `D3DAdaptor::getRenderTexture_guess`：default-software gate、map hit borrow、private-manager
  static upload、`emplace` 与 raw factory return；
- `D3DAdaptor::removeAllTextures`/destructor：whole-map holder release；
- 单元测试 `D3DAdaptor caches a holder ref but leaves the software copy factory ref`：两次 getter
  identity、map size、clear 后首个 factory ref 仍存活以及最终显式 Release。

本轮逐语句对照后没有发现需要修改的运行语义。只补充了 map-hit 不 AddRef 与 node allocation
失败泄漏的源码注释；private `opengl` manager 路由已在前一 slice 修复。

## 7. 验证与剩余边界

- 四端 source getter、emplace 与 iOS armv7 cleanup 均 fresh decompile，并完整读取全部
  160/157/118/196、77/53/57/54 与 24 条指令；
- specialized lookup/link helpers 全部 fresh decompile，并读取完整指令计数；
- 两个主函数已在四个 IDB 统一命名、注释、书签；specialized lookup/link helpers 与 armv7
  cleanup 也已命名、注释；四个 IDB 原位保存；
- coverage TSV 严格 12 列检查、deterministic NCB ledger regeneration、Python helper compile 与
  `git diff --check` 均作为本 slice 验收；
- 当前环境缺少 CMake、Ninja 和 Emscripten，且单头文件语法检查被缺失的
  `boost/locale.hpp` 阻塞，因此不宣称正式 native/Web build。

本 slice 已闭合 source getter、software map lookup/emplace、holder 生命周期和失败引用边。
四端 shared deep renderer 的 item admission、method cache、batch flush、stencil 与异常 owner
状态后来由 `MP-R14-D3D-DEEP-BATCH-STENCIL` 闭合；其公共 mesh helper 又由
`MP-R14-D3D-MESH-SUBMIT-CELLS` 独立闭合。
