# ResourceManager::load 缓存、校验、插入与 dispatch 生命周期四参考二进制联合恢复

日期：2026-08-27

## 1. 结论

四端 `ResourceManager::load` 具有同一条源码级数据流：先用 `TVPGetPlacedPath` 替换入参，按规范化
`ttstr` 查 `_loadedModules`；命中时只复制缓存的 `PSBFile` holder，未命中时才检查 storage、读取并
Adopt PSB、严格读取根节点 `id/spec/version`、更新进程内这个 ResourceManager 实例的粘滞 `_spec`，
然后以 `_loadedModules[path]` 默认构造完整 `LoadedResourceRecord` 并写入 file。无论 hit 还是 miss，
每次调用最后都会新建一个 `PSBValueDispatch`，把同一个 raw PSB owner 的独立引用发布成 TJS object
closure。

本地 `cpp/plugins/motionplayer/ResourceManager.cpp` 已经保留这些看似尖锐但真实的边界：`_spec` 不在
每次 miss 前归零，且在 version-too-new 检查前更新；缓存 hit 不复验文件或根字段；map 中若出现空
file holder，公共返回块会继续解引用；`LoadStorage` 使用进程全局 filter 的 lvalue，不做快照或加锁。
本轮未发现运行时 C++ 语义偏差，不为标准库 bucket/node/rehash 展开手写 ABI 结构。

## 2. 四端主 callback 与完整读取

| 平台 | `load` callback | 完整指令 | armv7 cleanup |
|---|---:|---:|---:|
| Android arm64 | `0x6A616C` | 501 | — |
| Android armv7 | `0x57B338` | 246 | — |
| iOS arm64 | `0x1001012D8` | 225 | — |
| iOS armv7 | `0xFE40C` | 364 | `0xFE7C0`，128条、32个call-site状态 |

四个 callback 均已 fresh decompile，并分页完成 full disassembly。iOS armv7 的 SjLj dispatcher 也
完成 fresh decompile/full disassembly；它按调用点状态释放规范化路径临时值、根/label/message
临时对象、尚未转移的 `PSBFile` holder、部分构造的返回 Variant 等 owner，然后继续 unwind。

路径规范化目标与同平台 `unload` 完全共享：

| 平台 | normalize helper |
|---|---:|
| Android arm64 | `0x8EBC80` |
| Android armv7 | `0x6B85DC` |
| iOS arm64 | `0x1001937AC` |
| iOS armv7 | `0x1930B0` |

callback 先把 helper 返回 holder 写回自己的 `path` 参数，再进行 map lookup，因此 map key、报错路径、
storage 检查和缓存插入全部使用规范化路径；没有并存的“原始路径 key”分支。

## 3. storage、PSB holder 与 dispatch helper 映射

| helper | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `TVPIsExistentStorage` | `0x8EB4C0`，40条 | `0x6B8138`，33条 | `0x100193128`，22条 | `0x192A00`，52条 |
| `PSBFile::LoadStorage` | `0x598918`，115条 | `0x4DD0A0`，78条 | `0x1000ED468`，69条 | `0xE9874`，111条 |
| `PSBFile::Transfer` | `0x598E44`，18条 | `0x4DD350`，14条 | `0x1000ED8E4`，15条 | `0xE9BE2`，14条 |
| `PSBValueDispatch` ctor | `0x597EB4`，17条 | `0x4DCB50`，19条 | `0x1000EC248`，15条 | `0xE8874`，17条 |

这些 helper 均完成 fresh decompile/full disassembly。四端 `LoadStorage` 的共同内部序列为：

1. 再次执行同平台 placed-path normalization；
2. 以 read 模式创建 storage stream，null 或 size `< 9` 返回 false；
3. 分配并读取完整 stream；
4. 若数据以 MDF 标记开头，尝试解码，并在成功时释放/替换原始 allocation；
5. 调用 `PSBFile::Adopt(data, size, processGlobalFilter)`；
6. 由 Adopt 验证 PSB 签名/表布局并让 owner 接管数据。

四个 callback 传入的是同一个进程全局 emote decrypt filter 的引用：Android arm64
`0x1AB52E0`、Android armv7 `0x11117E8`、iOS arm64 `0x101B697A8`、iOS armv7
`0x187D4B0`。没有 per-call `std::function` 快照、mutex 或原子发布；并发替换 filter 与 load 属于
数据竞争边界。

`Transfer` 复制 raw-owner holder、执行相应引用计数转移并清空 source。dispatch constructor 再复制
并 AddRef file owner，另存根 entries/node 指针，初始化自身引用计数和 valid 标志。callback 用同一个
dispatch 同时组成 object/objectthis closure，再释放本地创建引用，所以调用返回后 closure 独立拥有
dispatch，而 dispatch 独立拥有 raw PSB owner。

## 4. 共同源码伪代码

```text
Variant ResourceManager::load(ttstr path):
    path = TVPGetPlacedPath(path)

    selected = empty PSBFile
    it = loadedModules.find(path)
    if it != loadedModules.end():
        selected = it.value.file
    else:
        if !TVPIsExistentStorage(path):
            throw "Motion::ResourceManager: file not found '%1'."

        loaded = empty PSBFile
        if !loaded.LoadStorage(path, processGlobalEmoteDecryptFilter):
            throw "cannot open psb file : %1"

        root = loaded.GetRoot()
        id = root.strict("id").GetString()
        if id == null || strcmp(id, "motion") != 0:
            throw "this psb file is not motion file: %1"

        spec = root.strict("spec").GetString()
        if spec != null && strcmp(spec, "krkr") == 0:
            this.spec = 1
        if spec != null && strcmp(spec, "win") == 0:
            this.spec = 2
        if this.spec == 0:
            throw message containing root.strict("label").GetString()

        version = root.strict("version").GetDouble()
        if version > 3.0300001:
            throw "motion file '" + root.strict("label").GetString() +
                  "' is too new."

        selected = loaded.Transfer()
        loadedModules[path].file = selected

    owner = selected.owner
    dispatch = new PSBValueDispatch(selected, owner.header.entries)
    result = VariantObjectClosure(dispatch, dispatch)
    dispatch.Release()
    return result
```

源码表达中应继续使用 `unordered_map::find` 和 `operator[]`。四端生成的 node 大小、bucket policy、
rehash和异常清理只是各自 STL/ABI 展开，不是需要写进 portable C++ 的显式分支。

## 5. hit 与 miss 的精确数据流

### 5.1 cache hit

- find 使用与 `unload` 相同的 `ttstr_hash` 缓存 hash 和精确 `ttstr_equal`；没有再次 lowercase、
  basename 或自行替换 slash。
- 只复制 `cached->second.file`；不调用 `TVPIsExistentStorage`、`LoadStorage`、根字段 getter或
  `_spec` setter。
- 外部文件删除、修改、decrypt filter 替换和缓存记录中的其他两张 texture map 都不影响这次
  raw file选择。
- hit 仍分配新的 `PSBValueDispatch`；因此同一路径连续两次 `load` 的 TJS object identity 不同，
  但其内部 raw owner 相同。

### 5.2 cache miss

- existence false 与 LoadStorage false 是两个不同报错路径；前者尚未创建 raw PSB owner，后者按
  当前 holder/stream cleanup规则释放已经发布的 owner。
- `id` 必须是 raw string 且精确等于 ASCII `motion`；null、其他类型或其他大小写都失败。
- `spec` 两个比较是独立 `if`：`krkr -> 1`，`win -> 2`。它们只在命中已知字符串时覆盖 `_spec`。
- version 通过 raw double读取并以 ordered `>` 与 `3.0300001` 比较；相等接受，NaN 也因比较为
  false而接受。
- 只有全部校验通过后才 Transfer 并把记录提交进 map；文件不存在、load失败、id失败、spec失败或
  version太新都不会留下新 map node。

## 6. 粘滞 `_spec` 与校验顺序边界

`_spec` 是 ResourceManager 实例状态，不是局部变量；四端都没有在 miss 开始时清零。这产生必须
保留的顺序效应：

- 新实例首次载入 null/未知 `spec` 时，`_spec == 0`，进入“不适配spec”异常；
- 实例已成功或曾经走到已知 spec 后，再载入 null/未知 `spec`，旧的非零 `_spec` 会让该检查通过；
- `_spec` 在 version 检查之前更新，因此已知 spec 但 version too new 的文件虽然不会进入 cache，
  仍会永久改变这个实例的 `_spec`；
- cache hit 不读文件 spec，也不把 `_spec` 恢复为该缓存文件的 spec。

严格 `label` lookup 只在两个异常消息分支求值。没有额外 null/type guard；其 temporary raw node、
`ttstr` 和完整 message 的构造/析构顺序由四端 EH 路径保留。

## 7. `_loadedModules[path]` 的容器实现与异常 owner

| 平台 | get-or-insert | 额外 helper / cleanup |
|---|---:|---|
| Android arm64 | `0x6E8DC4`，73条 | node构造 `0x6E8EEC`，43条；record ctor `0x6E90DC`，79条；insert/rehash `0x6E8F98`，81条 |
| Android armv7 | `0x5A7488`，59条 | node构造 `0x5A751C`，30条；record ctor `0x5A762C`，55条；insert/rehash `0x5A7588`，52条 |
| iOS arm64 | `0x100101798`，150条 | libc++ 内联构造、rehash和commit |
| iOS armv7 | `0xFE940`，232条 | 插入 SjLj cleanup `0xFEBBA`，23条 |

共同语义：operator[] 先按精确 key 再查一次；miss 时复制/AddRef outer `ttstr` key，默认构造
`LoadedResourceRecord` 的 null PSB holder 和两个空 inner unordered-map，再按负载因子需要 rehash，
最后才把 node 链入 outer map并返回 mapped地址。随后 file assignment把 `selected` owner引用写入记录。

Android libstdc++ 的两个 inner map 默认 `max_load_factor == 1.0`，并在当前实现中采用prime bucket
policy；arm64 默认构造可见约10个初始bucket的选择。iOS libc++ 的空map为null/zero起始形态，同样
保留 `max_load_factor == 1.0`，首次插入时再选择bucket。node大小与布局不同只来自指针宽度和STL。

若 node/key/inner-map/bucket构造或 rehash 抛出，四端都销毁已经构造的 inner map、PSB holder、key
和 detached node，不把半成品发布到 outer map。iOS armv7 的 `0xFEBBA` 明确按构造状态执行
`KRKR -> Win -> PSB -> key/node` cleanup。源码使用 `operator[]` 即可获得这些平台正确路径。

## 8. dispatch、unload 与对象生命周期

cache 只持有 raw file holder，不缓存 dispatch。每次 `load` 都执行：

```text
cache/file holder --AddRef--> new PSBValueDispatch
new dispatch --object + objectthis owners--> returned tTJSVariant
local creation reference --Release--> only returned closure owners remain
```

因此 `unload(path)` 或 `unloadAll()` 只释放 map 自己的 file/texture owner；此前返回的 dispatch 仍
通过自己的 `PSBFile` holder保留 raw PSB allocation和entries指针。它不会被 ResourceManager 主动
invalidate。只有最后一个 dispatch/cache/raw-node owner释放后，底层 allocation才结束生命周期。

若 map 中因破坏、未定义并发或部分外部写入出现“key存在但 file holder为空”，hit路径不会回退到
miss，而会在公共返回块读取 null owner/header；四端都没有防御检查。本地同样没有制造与参考不同的
自动修复。

## 9. LoadStorage 的异常与内存边界

- stream创建失败或文件短于9字节返回 false；callback 抛 `cannot open psb file`。
- `ReadBuffer` 抛出时四端 EH 会销毁 stream/路径临时值，但读取前分配的裸 data buffer没有 owner，
  因而不会被清理；本地有意保留该异常泄漏边界。
- MDF decode成功时先释放源 allocation，再把解码 allocation和更新后的size交给 Adopt；decode返回
  null则继续把原始数据视作PSB候选。
- storage路径的 Adopt拒绝同样返回 false，但当前共同路径不回收传入的裸 data buffer；不能擅自
  与 octet load路径的显式失败释放合并。
- callback 没有 catch；所有 TVP异常和分配异常继续向脚本绑定层传播。已拥有的 holder/Variant按
  各平台 DWARF/SjLj cleanup释放。
- `ResourceManager`、process-global filter和outer map均无锁；并发 load/unload/filter replacement
  属于数据竞争，不能从单线程反编译结果推导线程安全。

## 10. 本地逐行对照

`cpp/plugins/motionplayer/ResourceManager.cpp:311..387` 当前已经逐项匹配：

- 入参先 `path = TVPGetPlacedPath(path)`；
- hit只复制 `cached->second.file`；
- miss的 existence、LoadStorage、严格 `id/spec/version`、粘滞 `_spec` 和消息构造顺序一致；
- `selected = loaded.Transfer_guess()` 后执行 `_loadedModules[path].file = selected`；
- 公共尾部由 `selected.GetOwner()->GetHeader()->entries` 新建 dispatch，再用同一指针组成
  object/objectthis并释放本地引用。

相关共享实现也已对齐：

- `cpp/plugins/psbfile/PSBRawFile.cpp:433`：holder Transfer；
- `cpp/plugins/psbfile/PSBRawFile.cpp:484`：storage、MDF、Adopt与异常泄漏边；
- `cpp/plugins/psbfile/PSBFile.cpp:21`：dispatch constructor retain；
- `cpp/plugins/motionplayer/ResourceManager.h`：outer/inner portable STL owner结构。

没有为了让源码“更安全”而修正 sticky spec、null cached holder、Adopt失败泄漏或并发边界；这些都是
四端共同可观察行为。本轮无需修改运行时 C++。

## 11. 可执行测试证据与验证边界

`tests/unit-tests/plugins/motionplayer-dll.cpp:27053` 已覆盖正常 owner主干：同一路径连续 load 得到
两个不同 dispatch；两者均可读取 `id/spec`；ResourceManager unload 后先前返回的 dispatch 仍可读取
metadata。这与“缓存 raw holder、每次新建 facade、facade独立 retain raw owner”的四端模型一致。

本轮还完成：

- 四端 callback、storage/transfer/dispatch、operator[]及其异常 helper 的 fresh decompile/full
  disassembly；
- 四端 IDB 的 helper命名、callback/helper/global注释、load书签并原位保存；
- NCB账本确定性重生成、TSV严格字段检查和 `git diff --check`；
- 没有伪造 fixture或把单端STL细节写成跨端语义。

当前环境没有 CMake/Ninja/Emscripten和完整依赖头，不能宣称正式 unit/Web build。工具链恢复后应
补跑：cache hit identity/raw-owner retain、首次未知spec失败、旧spec继承、too-new后spec仍改变、NaN
version、Adopt失败与并发未定义边界的现有/新增真实fixture测试。
