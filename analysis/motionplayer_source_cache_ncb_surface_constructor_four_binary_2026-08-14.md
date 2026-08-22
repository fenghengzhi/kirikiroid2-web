# Motion.SourceCache 完整 NCB 注册面、双构造路径与 adaptor 生命周期四参考审计（2026-08-14）

## 结论

四份当前参考二进制共同给出同一份 `Motion.SourceCache` 发布面：

- 脚本只发布一个 **零参数** generated typed constructor；
- registrar 随后恰好注册三个 descriptor，顺序固定为 `loadSource`、`clearCache`、
  getter-only `bufLayer`；
- 没有常量、raw callback、setter 或额外 constructor overload；
- native `SourceCache(tTJSVariant owner, tjs_int cacheSize)` 确实存在，但它只由
  `ResourceManager` 作为 base-subobject 构造路径直接调用，并不发布到脚本；
- 零参数 constructor 的 lower-bound 是 `numparams < 0`。因此任何非负数量的普通 surplus
  参数都被完全忽略，不读取、不转换，也不会被误当成 `owner/cacheSize`；
- 一个 Void 参数仍是 ncbind 的空-adaptor shell sentinel：成功建立 script object，但不分配
  native `SourceCache`；
- ordinary zero/surplus 路径执行 `new SourceCache()`，得到三个 Void Variant、两个零计数和
  空 list；它不会读取 owner，也不会建立 `bufLayer`；
- attach 失败才销毁并释放刚构造的 native object，返回 `TJS_E_NATIVECLASSCRASH`；
- adaptor `Invalidate`/destruction 在非-sticky native 上直接执行 C++ 成员析构，顺序为
  cache list → `bufLayer` → `primaryLayer` → `owner` → storage。它不调用 public
  `clearCache()`，所以 cached Layer 不收到脚本可见的 `Invalidate`。

本轮据此纠正了本地一条过时注册注释/实现：原先
`NCB_CONSTRUCTOR((tTJSVariant, tjs_int))` 把 ResourceManager 的 base-construction signature
错误扩大成 SourceCache 的脚本 constructor。现已恢复为 `NCB_CONSTRUCTOR(())`，并把默认
constructor 改成首声明处 `SourceCache() = default`，使 ncbind 的 `new SourceCache()` 保持
C++ value-initialization 语义。

## 四端 registrar 与 constructor 链映射

| 角色 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `SourceCache` registrar | `0x6A5988` | `0x57B0DC` | `0x100100F90` | `0xFE12A` |
| registrar 大小 | `0x228` | `0x56` | `0x88` | `0x72` |
| constructor descriptor register | registrar inline | `0x57B14C` | `0x100101018` | `0xFE19C` |
| constructor Function factory | registrar inline | `0x5A6870` | `0x100138C18` | `0x138CEC` |
| constructor descriptor install | registrar inline | `0x5A68CC` | `0x100138CA0` | `0x138DE8` |
| zero-arg constructor `FuncCall` | `0x6E835C` | `0x5A69B4` | `0x100138DF0` | `0x138F54` |
| allocate + adaptor attach | `0x6E8430` | `0x5A6A44` | `0x100138E90` | `0x138FC0` |
| native `SourceCache(owner,cacheSize)` | `0x6A4CD4` | `0x57AADC` | `0x10010071C` | `0xFD824` |
| native allocation size | `0x58` | `0x34` | `0x60` | `0x38` |

Android ARM64 把 constructor descriptor allocation/factory/install 展开进 registrar；其 outer
Function object vptr 为 `0x1A1A5D8`，`FuncCall` 槽位于 `0x1A1A5E8`。另三端 outer
Function vptr 分别为 Android ARMv7 `0x10BACF8`、iOS ARM64 `0x101AE26D8`、iOS ARMv7
`0x1832FF8`；其 `FuncCall` 槽分别在 `+8`、`+0x10`、`+8`。两个 32 位槽保存的 Thumb
函数指针最低位为 `1`，表中地址已去掉 ISA tag。

这条 vtable → `FuncCall` → allocate/attach 链与 native `(owner,cacheSize)` constructor 没有
交叉：四端后者的唯一直接 caller 都是 `ResourceManager` constructor，调用点分别为
`0x6A5CD8`、`0x57B204`、`0x100101174`、`0xFE274`。因此不能仅凭 C++ 中存在一个双参数
constructor，就把它填入 SourceCache 的 NCB registrar。

## 精确三项发布顺序与 native target

| # | 脚本名 | descriptor | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---:|---|---|---:|---:|---:|---:|
| 1 | `loadSource` | ordinary typed method | `0x6A4F88` | `0x57ACC8` | `0x1001009AC` | `0xFDB50` |
| 2 | `clearCache` | ordinary typed method | `0x6A5818` | `0x57B018` | `0x100100F10` | `0xFE0D4` |
| 3 | `bufLayer` | typed read-only property | `0x6A58DC` | `0x57B060` | `0x100100F84` | `0xFE11A` |

`bufLayer` descriptor 的 setter/member-adjustment slots 四端都为零。`ResourceManager` 的
12-member registrar 前三项再次保存上述完全相同的 callback 地址；不存在 derived forwarding
shim 或第二套 cache implementation。

源级 typed 形态为：

```text
loadSource(source, descriptor) -> Variant
clearCache()                   -> void
bufLayer                       -> Variant getter; setter = null
```

`loadSource`、entry list、eviction、`clearCache` 的内部边界已在相邻 SourceCache 纵切面中
分别闭合。本文件聚焦 class publication、两条 constructor 路径、对象布局和 adaptor/native
所有权边界。

## Zero-argument constructor `FuncCall` 的精确边界

四端 fresh decompile 收敛为同一控制流：

```text
if membername != null:
    return TJS_E_MEMBERNOTFOUND       // -1001; result untouched

if numparams == 1 && param[0].Type == Void:
    return TJS_S_OK                   // adaptor shell; result untouched

if result != null:
    result.Clear()

if numparams < 0:
    return TJS_E_BADPARAMCOUNT        // -1004

native = new SourceCache()            // no parameter read or conversion

if objthis/adaptor attach fails:
    delete native
    return TJS_E_NATIVECLASSCRASH     // -1008

return TJS_S_OK                       // result remains Void
```

由此得到以下 observable 边界：

- `SourceCache()` 是普通成功路径，而不是 BADPARAMCOUNT；
- 一个 Void 参数早于 result clear、argc gate、allocation 和 attach，是专用空-shell sentinel；
- `SourceCache(17,23,29)` 也成功，并且三个值全部不读取。即使 surplus Variant 的转换会抛
  异常也没有影响，因为 bridge 根本不触发转换；
- generated zero-arg template 仍保留 `numparams < requiredCount` 检查。requiredCount 为零，
  所以只有内部负数计数失败；普通 TJS `CreateNew` 无法产生这一负数输入；
- membername 分支和 Void sentinel 分支不清 result；普通路径先把 result 置 Void；
- constructor bridge 没有 ordinary typed method 的 upfront null-receiver gate。native object
  先完整分配/构造，attach 阶段才验证 `objthis` 能否提供目标 class adaptor；
- attach 失败会在返回 `-1008` 前运行 native destructor 和 `operator delete`，不会发布半构造
  pointer；
- 成功路径不把 native object 或 script object写入 `result`；TJS class `CreateNew` 的外层负责
  返回所创建的 dispatch。

四端 bridge 都只把 `result/numparams/params/objthis` 与 descriptor state 打包给 allocation
helper；allocation helper 不访问 params。与 ResourceManager 的双参数 bridge 相比，这里没有
Variant staging、integer conversion、owner AddRef 或 cache-size读取。

## 为什么首声明处 `= default` 是 ABI/生命周期修复

ncbind 的 generated zero-argument path 使用 `new ClassT()`。对当前 `SourceCache` 而言，四端
机器码共同表现为 value-initialized native object：

- Android ARM64/ARMv7/iOS ARMv7 先把整块 native storage 清零，再修好 list sentinel；
- iOS ARM64 清零前三个 Variant 和两个整数覆盖的前 `0x48` 字节，再初始化 libc++ list 的
  next/prev/size；
- 最终三个 Variant 都是 Void，`currentCacheBytes=0`、`cacheLimitBytes=0`，list empty。

若 `SourceCache::SourceCache() = default` 在类外定义，它在 C++ 规则下是 user-provided default
constructor；`new SourceCache()` 不再先 zero-initialize 两个无成员初始化器的整数。正常 allocator
常返回清零页会掩盖问题，但那不是语言保证，也不等同四端机器码。

把 `SourceCache() = default` 放在类内第一次声明处后，它是非 user-provided defaulted
constructor；`new SourceCache()` 恢复先 zero-initialize object、再 default-construct三个
Variant 和 list 的形态。这里没有给两个整数添加 default member initializer，因为那会改变
普通 default-initialization（例如不带括号的栈对象）边界；参考证据只要求 ncbind 的
value-initialized `new SourceCache()` 路径。

## Native 对象布局与 list ABI

| source member | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `_owner` Variant | `+0x00`, `0x14` B | `+0x00`, `0x0C` B | `+0x00`, `0x14` B | `+0x00`, `0x0C` B |
| `_primaryLayer` Variant | `+0x14`, `0x14` B | `+0x0C`, `0x0C` B | `+0x14`, `0x14` B | `+0x0C`, `0x0C` B |
| `_bufLayer` Variant | `+0x28`, `0x14` B | `+0x18`, `0x0C` B | `+0x28`, `0x14` B | `+0x18`, `0x0C` B |
| `_currentCacheBytes` | `+0x3C` | `+0x24` | `+0x3C` | `+0x24` |
| `_cacheLimitBytes` | `+0x40` | `+0x28` | `+0x40` | `+0x28` |
| `_entries` list | `+0x48`, `0x10` B | `+0x2C`, `0x08` B | `+0x48`, `0x18` B | `+0x2C`, `0x0C` B |
| total allocation | `0x58` | `0x34` | `0x60` | `0x38` |

Android 两端使用旧 libstdc++ `std::list` sentinel 形态，object 内是 next/prev 两个 pointer；
iOS 两端使用 libc++ list，object 内除 next/prev 外还保存 size。源码级容器仍是同一个
`std::list<SourceCache::Entry>`，不能把目标 STL header 展开写成插件自有 intrusive list。

默认脚本 constructor 的五个标量/Variant slot 均为 Void/zero；typed base constructor 则在
同一布局上填入 owner、limit 和 scratch Layer。allocation size 的四端差异完全由 pointer
width、Variant size 与 STL ABI解释，不代表不同的 SourceCache class hierarchy。

## Native `(owner, cacheSize)` constructor：ResourceManager 专用 base 路径

四端 typed native constructor 的 success path共同为：

```text
owning-copy owner into _owner
default-construct _primaryLayer = Void
default-construct _bufLayer = Void
_currentCacheBytes = 0
_cacheLimitBytes = cacheSize
construct empty _entries list

make an additional owning owner Variant / strict Object conversion
read owner.primaryLayer
copy-assign the closure to _primaryLayer
evaluate/create global Layer with arguments {_owner, _primaryLayer}
copy-assign the created closure to _bufLayer
destroy temporaries in reverse order
```

这里 owner by-value 与后续 accessor/construction 临时都会产生可见 AddRef/Release；不能把
typed constructor 改成借用 dispatch pointer。`bufLayer` 是 constructor-owned persistent scratch
Layer，它不进入 cache list，也不随 public `clearCache()` 被替换或失效。

这条路径只在 `ResourceManager` native constructor 进入 `SourceCache` base subobject 时发生。
它解释了为什么 ResourceManager 构造后 `bufLayer` 为 Object，而脚本直接
`new Motion.SourceCache()` 的 `bufLayer` 必须为 Void；二者不是 overload selection，而是两个
不同 publication boundary。

## Adaptor 布局、publication 与释放

四端 `ncbInstanceAdaptor<SourceCache>` 布局一致映射为：

| field | 64-bit | 32-bit |
|---|---:|---:|
| vptr | `+0x00` | `+0x00` |
| native instance pointer | `+0x08` | `+0x04` |
| sticky/native-owned flag | `+0x10` | `+0x08` |
| total adaptor size | `0x18` | `0x0C` |

相关路径：

| 角色 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| adaptor create | `0x6FB7BC` | `0x5B6BC8` | `0x10014E138` | `0x14FEC8` |
| adaptor vptr | `0x1A1EE60` | `0x10BD13C` | `0x101AE6F60` | `0x183543C` |
| Invalidate/release entry | `0x6FB7F0` | merged `0x5B6BEC` | thunk `0x10014E16C` | thunk `0x14FEEC` |
| D1 destructor | `0x6FB8A0` | merged `0x5B6BEC` | `0x10014E170` | `0x14FEF0` |
| D0 destructor | `0x6FB900` | merged `0x5B6BEC` | `0x10014E1B4` | `0x14FF1C` |
| shared native release | `0x6FB7F0` | merged `0x5B6BEC` | `0x10014E1E8` | `0x14FF40` |

Android ARMv7 recovery IDB 当前把 Invalidate/D1/D0 合并成一个 `0x88` 字节函数实体；函数内
仍能完整看到同一 cleanup body。iOS 两端把 Invalidate/D1/D0 保留成小 thunk，再汇入 shared
release helper。Android ARM64 的 Invalidate 自身就是 shared release body。

release 的精确 owner gate 与顺序为：

```text
native = adaptor.native
if native != null && adaptor.sticky == false:
    destroy every cache-list node and its owned Entry values
    destroy _bufLayer
    destroy _primaryLayer
    destroy _owner
    operator delete(native)

adaptor.native = null
adaptor.sticky = false
```

sticky adaptor 表示 native storage 由外部 C++ owner 管理，因此 script adaptor 失效时只断开
pointer，不删除 native。普通 constructor attach 的 native 是非-sticky，必须由 adaptor 回收。
无论是否删除 native，release 返回前都清 pointer 和 sticky state，阻止二次释放。

公开 `clearCache()` 会逐项向 cached Layer 发送 `Invalidate`，再释放 list entries；native
destructor/adaptor release 则直接运行 list/Variant destructor，不复用该公开 callback。这是
可观察的边界，不能用 `~SourceCache(){ clearCache(); }` 合并两条路径。

Android ARM64 另有清晰独立的 native destructor body `0x6E85EC`，同样显示 list →
`_bufLayer(+0x28)` → `_primaryLayer(+0x14)` → `_owner(+0)`。`ResourceManager` 四端析构也在
derived containers 回收后进入完全相同的 SourceCache base teardown。

## Android ARM64 recovery function-boundary 注意事项

当前 Android ARM64 recovery IDB 历史上把三个相邻实现误合并成
`SourceCache_ctor_guess @ 0x6A4CD4`，总大小 `0xC08`。四端 registrar target、另三端独立
函数边界与 ARM64 control-flow 共同确定真实区间为：

```text
SourceCache(owner,cacheSize)  [0x6A4CD4, 0x6A4F88)
loadSource                   [0x6A4F88, 0x6A5818)
clearCache                   [0x6A5818, 0x6A58DC)
```

本轮没有对恢复 IDB 执行 destructive undefine/redefine。已把两个内部 `loc_` 改为
`SourceCache_loadSource_entry_guess` 与 `SourceCache_clearCache_entry_guess`，并在 registrar、
入口和 bookmark 中写入真实边界。这样既保存已恢复的 xref/type/comment，又不继续把旧误合并
边界当作原版源码结构证据。

## 本地修正

- `main.cpp`：把 SourceCache 的 NCB constructor 从过时的
  `(tTJSVariant,tjs_int)` 改为精确零参数；三项 descriptor 与顺序不变。
- `SourceCache.h`：把默认 constructor 改成首声明处 `SourceCache() = default`，恢复 ncbind
  `new SourceCache()` 的 value-initialization。
- `SourceCache.cpp`：删除类外 user-provided defaulted constructor definition；typed
  `(owner,cacheSize)` constructor 与 method bodies 不变。
- `motionplayer-dll.cpp`：新增真实 `Motion.SourceCache` class-object `CreateNew` 回归，覆盖
  零参数 native publication、单 Void adaptor shell、三项 surplus ignore，以及默认
  `bufLayer=Void`/empty cache。

这里没有修改 `loadSource`、`clearCache` 或 cache entry container 实现；它们已经由相邻四端
纵切面闭合，本轮新证据只要求修正脚本 publication 与默认构造语义。

## Recovery IDB 改进

四份 recovery IDB 已完成并原位保存：

- constructor register/factory/install（可独立识别者）、zero-arg `FuncCall`、allocate/attach、
  adaptor create/release 与 Android ARM64 native destructor 按 `_guess` 规则命名；
- 四个 constructor `FuncCall` 统一应用八参数 dispatch prototype；
- registrar、bridge、allocation helper、typed base constructor 与 adaptor cleanup 写入四端
  共识注释和 bookmark；
- Android ARM64 只标记真实内部入口，不 destructive split 历史误合并函数；Android ARMv7
  adaptor 合并体明确命名为 `_merged_guess`；
- rename/type/comment 后对四个 constructor bridge 与四个 native release body fresh
  decompile；gate、offset 和 cleanup order 均可读；
- 四份 recovery IDB 均保存成功。

## 验证

- 整份 `motionplayer-dll.cpp` Emscripten TU syntax check 通过；只保留仓库既有 `_tss`
  literal-operator deprecation warning。
- `cmake --build --preset "Web Debug Build"` 完成 32/32 targets，重新编译 SourceCache、
  main registrar 和依赖对象并成功链接 `index.html`；输出只含仓库既有编译/链接警告。
- 最终精确源码扫描确认 SourceCache block 恰好有一个 `NCB_CONSTRUCTOR(())`、两个 method、
  一个 RO property、零 typed overload、零 raw callback，且顺序为
  `loadSource -> clearCache -> bufLayer`。
- 声明扫描确认首声明处 inline `SourceCache() = default` 存在，类外
  `SourceCache::SourceCache() = default` 已不存在；回归测试、分析文档和 plan link 均存在。
- `git diff --check` 通过；仓库既有 LF/CRLF 提示不视为内容错误。
- 当前 CMake 没有配置可直接运行该 Catch2 motionplayer TU 的 native executable；新增回归
  会做整 TU 编译验证，但不伪造运行结论。

相邻已闭合纵切面：

- `analysis/motionplayer_source_cache_load_source_four_binary_2026-08-13.md`
- `analysis/motionplayer_source_cache_entry_lifetime_four_binary_2026-08-13.md`
- `analysis/motionplayer_source_cache_clear_cache_boundary_four_binary_2026-08-13.md`
- `analysis/motionplayer_source_cache_buf_layer_four_binary_2026-08-13.md`
- `analysis/motionplayer_resource_manager_ncb_surface_constructor_four_binary_2026-08-14.md`
- `analysis/motionplayer_resource_manager_module_map_lifecycle_four_binary_2026-08-14.md`
