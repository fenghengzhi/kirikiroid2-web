# Motion.ResourceManager 完整 NCB 注册面与 constructor 生命周期四参考审计（2026-08-14）

## 结论

四份当前参考二进制共同给出同一份 `Motion.ResourceManager` 发布面：

- 一个 generated typed constructor，源码签名为
  `ResourceManager(tTJSVariant, tjs_int)`；
- 恰好 12 个成员 descriptor，发布顺序四端完全一致；
- `bufLayer` 是唯一 property，而且是 getter-only；
- 其余 11 项都是 ordinary typed method；
- 这张 registrar 表没有 raw callback，也没有常量；
- `loadSource`、`clearCache`、`bufLayer` 直接重新绑定 `SourceCache` 的三个原 callback，
  不是 derived forwarding shim，也没有第二套 cache 字段；
- `setEmotePSBDecryptSeed` / `setEmotePSBDecryptFunc` 不属于这 12 项；它们由独立
  `emoteplayer.dll` 注册路径稍后注入同一个 class object；
- constructor 的唯一一个 Void 参数是 ncbind 空-adaptor sentinel；普通调用至少需要
  两个参数，只读取前两个并接受 surplus；
- receiver/adaptor 不在入口检查。普通参数有效时会先完整构造 native
  `ResourceManager`，attach 失败才析构并释放它，返回 `TJS_E_NATIVECLASSCRASH`。

本轮还发现一个此前被“成功构造后的最终状态相同”掩盖的生命周期偏差。四份参考都先
执行 `new Math.RandomGenerator()`，成功后才给 layer-id `std::set<uint32_t>` 分配并插入
哨兵 `0`，最后写入 `nextLayerId=1` 与独立未知状态 `=1`。本地原先使用成员初始化器
`_usedLayerIds{0}`，会在脚本求值之前分配哨兵节点；正常路径看不出差别，但脚本求值或
set 插入抛异常时的已构造子对象与回滚边界不同。本轮已恢复参考顺序。

## 四端 registrar 与 constructor 链映射

| 角色 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `ResourceManager` registrar | `0x6A8C9C` | `0x57C3A8` | `0x100102E88` | `0x1002FC` |
| registrar 大小 | `0x63C` | `0xF6` | `0x1A8` | `0x180` |
| registrar 指令数 | 385 | 107 | 94 | 118 |
| constructor descriptor register | registrar inline | `0x57C510` | `0x100103030` | `0x10047C` |
| constructor Function factory | registrar inline | `0x5A7C7C` | `0x10013A46C` | `0x13A4C8` |
| constructor descriptor install | generic inline call | `0x5A7CD8` | `0x10013A4F4` | `0x13A5C4` |
| typed constructor `FuncCall` | `0x6E9A98` | `0x5A7DC0` | `0x10013A644` | `0x13A730` |
| construct + adaptor attach | `0x6E9B70` | `0x5A7E50` | `0x10013A6E8` | `0x13A79C` |
| allocation + argument conversion | `0x6E9C88` | `0x5A7F10` | `0x10013A7D8` | `0x13A8E4` |
| arg0 Variant staging helper | `0x6E9DDC` | `0x5A7FB4` | `0x10013A8A4` | `0x13A9F4` |
| allocation EH cleanup | inline `0x6E9DAC..0x6E9DD8` | 未恢复为独立可见 landing pad | `0x10013A870` | `0x13A9AC` SJLJ |
| native constructor | `0x6A5CAC` | `0x57B1EC` | `0x100101158` | `0xFE254` |
| native destructor | `0x6A5F74` | `0x57B2E4` | `0x10010126C` | `0xFE3B4` |
| native allocation size | `0xE8` | `0x80` | `0xC8` | `0x70` |

Android ARM64 把 constructor descriptor object 的 `0x38` 字节分配和初始化直接内联进
registrar；另三端保留独立 factory。32 位 Function object 分配 `0x20`，64 位分配
`0x38`。这些是 ncbind descriptor 的尺寸，不是 native `ResourceManager` 尺寸。

四个 outer Function vtable 的 `FuncCall` 槽分别落到上表的 typed constructor
`FuncCall`。可直接验证的槽位是 Android ARM64 `0x1A1AA68`、Android ARMv7
`0x10BAF40`、iOS ARM64 `0x101AE2B68`、iOS ARMv7 `0x1833240`；32 位槽内 Thumb
函数指针带最低位 `1`。

## 精确 12 项发布顺序与 native target

| # | 脚本名 | descriptor | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---:|---|---|---:|---:|---:|---:|
| 1 | `loadSource` | typed method, inherited target | `0x6A4F88` | `0x57ACC8` | `0x1001009AC` | `0xFDB50` |
| 2 | `clearCache` | typed method, inherited target | `0x6A5818` | `0x57B018` | `0x100100F10` | `0xFE0D4` |
| 3 | `bufLayer` | typed RO property, inherited getter | `0x6A58DC` | `0x57B060` | `0x100100F84` | `0xFE11A` |
| 4 | `load` | typed method | `0x6A616C` | `0x57B338` | `0x1001012D8` | `0xFE40C` |
| 5 | `unload` | typed method | `0x6A697C` | `0x57B6F8` | `0x100101A28` | `0xFEC04` |
| 6 | `unloadAll` | typed method | `0x6A60D8` | `0x57B32C` | `0x1001012CC` | `0xFE3FE` |
| 7 | `isExistMotion` | typed method | `0x6A6AD8` | `0x57B780` | `0x100101AC8` | `0xFECF4` |
| 8 | `findMotion` | typed method | `0x6A72B4` | `0x57B9F8` | `0x100101E84` | `0xFF11C` |
| 9 | `findSource` | typed method | `0x6A7F1C` | `0x57BDE0` | `0x100102594` | `0xFF890` |
| 10 | `random` | typed method | `0x6A894C` | `0x57C1CC` | `0x100102C90` | `0x1000F0` |
| 11 | `requireLayerId` | typed method | `0x6A8A74` | `0x57C258` | `0x100102D40` | `0x100240` |
| 12 | `releaseLayerId` | typed method | `0x6A8B30` | `0x57C2C8` | `0x100102DB8` | `0x10028A` |

Android ARM64 recovery IDB 当前仍把 `loadSource` 与 `clearCache` 的实体边界并进较大的
`SourceCache_ctor_guess` recovery function；表中两项是 registrar member pointer 直接保存
的真实入口，而不是把那个错误的大函数边界当作 target。其余三端保留清晰独立函数实体。

源级 typed 形态与本地声明一致：

```text
loadSource(source, descriptor) -> Variant
clearCache()                   -> void
bufLayer                       -> Variant getter; setter = null
load(path)                     -> Variant
unload(path)                   -> void
unloadAll()                    -> void
isExistMotion(project, path)   -> bool
findMotion(project, path)      -> Variant
findSource(moduleKey, path)    -> Variant
random()                       -> double
requireLayerId()               -> int32
releaseLayerId(id)             -> int32
```

四个 property descriptor 的 setter/member-adjustment 槽都明确为零。这里的
`loadSource/clearCache/bufLayer` 指向 `SourceCache` base subobject 的同一实现；
`ResourceManager` 没有另一个 `_bufLayer`，也没有 forwarding function 带来的额外
Variant CopyRef。

## Constructor `FuncCall` 的精确脚本边界

四端收敛为同一个高层次序：

```text
if membername != null:
    return TJS_E_MEMBERNOTFOUND       // result untouched

if numparams == 1 && param[0].Type == Void:
    return TJS_S_OK                   // result untouched; no allocation

if result != null:
    result.Clear()

if numparams < 2:
    return TJS_E_BADPARAMCOUNT

native = new ResourceManager(
    owning Variant conversion of param[0],
    ordinary TJS int32 conversion of param[1])

if objthis/adaptor attach fails:
    delete native
    return TJS_E_NATIVECLASSCRASH

return TJS_S_OK
```

由此得到以下边界：

- `ResourceManager()` 和任意普通单参数调用都返回 `TJS_E_BADPARAMCOUNT`；C++ 侧存在的
  port convenience default constructor 不会扩大脚本发布面。
- 恰好一个 Void 参数不是“把 cache size 默认成 0”，而是创建 adaptor shell 的专用
  sentinel。该分支早于 result clear、argc gate、objthis 检查和 native allocation。
- 两个及以上参数通过 lower-bound gate；只访问 `param[0]` 与 `param[1]`，第三项及以后
  即使不可转换也不读取。
- 普通路径先清 result，再检查 argc。因此短参数失败留下 Void；sentinel 与非空
  membername 路径则保留调用前 result。
- constructor adapter 没有 ordinary typed method 的入口 null-receiver gate。给出两个
  有效参数但 `objthis == null` 时，先完成脚本求值、容器构造与 native allocation，attach
  阶段才析构/free 并返回 `TJS_E_NATIVECLASSCRASH`。
- 参数转换或 native constructor 抛出的 C++ exception 继续向外传播；它不被改写成
  `TJS_E_NATIVECLASSCRASH`。后者只表示 adaptor attach 失败。

## 参数 staging、转换与异常清理

### 参数 0：by-value `tTJSVariant`

四个 generated helper 都不只是裸借用 `*param[0]`，而是保留同一套 staging：

1. 从 `param[0]` CopyRef 到第一临时 Variant；
2. 把它 copy-assign 到一个先为 Void 的临时 Variant；
3. 析构第一临时；
4. 再 copy-construct 一个中间临时；
5. copy-construct 最终 outgoing Variant；
6. 逆序析构中间值与 copy-assigned 值；
7. native constructor 返回后析构 outgoing Variant。

这组临时复制是 ncbind by-value functor 展开的可见所有权形态。它说明 `kag` closure 在
进入 `SourceCache` 前已经由 adapter 独立持有；不能把签名改成借用引用来省略 AddRef/
Release。helper 自身保留“argc<1 时生成 Void”的模板 fallback，但 outer `FuncCall` 已经要求
`argc>=2`，所以这个分支在正常注册调用中不可达。

### 参数 1：ordinary TJS integer conversion

allocation helper 对 `param[1]` 先建立 owning Variant 临时，再执行普通
`tTJSVariant::AsInteger()`，最后销毁临时；结果截到 native `tjs_int`。没有 clamp、类型预检
或 optional default。模板中“argc<2 时用 Void 再转 0”的 fallback 同样被 outer gate
封死。

### new-expression 回滚

- Android ARM64 allocation helper 在自己的尾部保留 landing pads：按当前 live state
  析构 param1/param0 临时、`operator delete` 尚未完成构造的 `0xE8` storage，再
  `_Unwind_Resume`。
- iOS ARM64 使用独立 cleanup thunk `0x10013A870`，执行同样的 Variant 清理、delete 与
  resume。
- iOS ARMv7 通过 SJLJ cleanup `0x13A9AC`，按 landing-state 分支清理一个或两个 Variant，
  delete `0x70` storage，再 `__Unwind_SjLj_Resume`。
- Android ARMv7 的短 helper 在当前 recovery 函数实体中没有恢复出独立可见 landing pad。
  这项差异只记录为编译器/unwind recovery 差异；不能据此虚构不同的 C++ 类或删除
  common source 的异常安全。

## Native 对象布局与容器 ABI

四个 allocation size 与成员起点共同确定下表。所有 offset 都只记录在本分析文件；源码
保持普通 C++ 成员，不硬编码目标 ABI。

| source member / subobject | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `SourceCache` base size | `0x58` | `0x34` | `0x60` | `0x38` |
| `_loadedModules` | `+0x58`, `0x38` B | `+0x34`, `0x1C` B | `+0x60`, `0x28` B | `+0x38`, `0x14` B |
| `_randomGenerator` | `+0x90`, `0x18` B | `+0x50`, `0x0C` B | `+0x88`, `0x18` B | `+0x4C`, `0x0C` B |
| `_usedLayerIds` | `+0xB0`, `0x28` B | `+0x5C`, `0x18` B | `+0xA0`, `0x18` B | `+0x58`, `0x0C` B |
| `_nextLayerId` | `+0xD8` | `+0x74` | `+0xB8` | `+0x64` |
| `_layerIdState_guess` | `+0xDC` | `+0x78` | `+0xBC` | `+0x68` |
| `_spec` | `+0xE0` | `+0x7C` | `+0xC0` | `+0x6C` |
| total | `0xE8` | `0x80` | `0xC8` | `0x70` |

Android 两端使用旧 libstdc++ container ABI：default-constructed `_loadedModules` 会按其
默认 policy materialize 十 bucket 形态；这不是插件源码调用 `rehash(10)`。iOS 两端的
libc++ unordered_map 初始为零 bucket，第一次 insertion 才分配。四端 source-level 都是
同一个 default-constructed `unordered_map<ttstr, LoadedResourceRecord>`。

`_usedLayerIds` 是 `std::set<uint32_t>`，不是 unordered set、name→id map 或 vector。
Android libstdc++ tree header 分别占 `0x28/0x18`，iOS libc++ tree 占 `0x18/0x0C`。
尾部三个字段始终是独立 32 位 slot；64 位 Android constructor 的
`0x100000001` 合并 store 只是一次写入相邻的 `next/state`，不是一个 64 位计数器。

## Native constructor 的真实顺序

去掉 STL ABI 展开后，四端共同顺序是：

```text
construct SourceCache base(kag, cacheSize)
default-construct _loadedModules
default-construct Void _randomGenerator
default-construct empty _usedLayerIds tree header
initialize _spec = 0

evaluate "new Math.RandomGenerator()" into _randomGenerator
insert uint32 sentinel 0 into _usedLayerIds
write _nextLayerId = 1
write _layerIdState_guess = 1
```

`SourceCache` base construction先拥有 owner/kag，读取 `primaryLayer`，建立一次性的
`bufLayer` closure，并建立空 cache list；derived constructor 不复制这些字段。正常成功后
layer-id set 为 `{0}`，第一次 `requireLayerId()` 因此返回 `1`。

顺序的异常含义是：

- RandomGenerator 脚本求值失败时，tree header 已构造但没有 sentinel node allocation；
- sentinel node allocation/insertion 失败时，已经得到的 RandomGenerator Variant 必须被
  析构；
- 两个值为 `1` 的尾字段只在 sentinel insertion 成功后写入；
- `_spec=0` 在脚本求值之前已经存在；它是独立 slot，不能和未知 state 合并。

本地旧 `_usedLayerIds{0}` 会把第一条变成“求值失败时还要回收一个已分配 tree node”。
这是对象未发布阶段的真实生命周期差异，因此不能以最终 set 内容相同为由保留。

## Attach、发布与析构

constructor 的 native publication 分两层：

1. allocation helper 返回一个完整 native pointer；
2. attach helper 对 `objthis` 调 `NativeInstanceSupport(GETINSTANCE, classId, &adaptor)`，
   要求调用成功且 adaptor 非空；
3. 64 位把 native pointer 写到 adaptor `+8`，32 位写到 `+4`；
4. 任一 attach 条件失败，立即运行 `ResourceManager` destructor 和 `operator delete`，
   返回 `-1008`。

因此不会发布半构造 native pointer。attach 失败会重复执行与普通 destruction 相同的
derived/base teardown；空-adaptor sentinel 则根本不进入这条链。

四端完整 destructor 顺序为：

1. derived destructor body 先 clear `_loadedModules` 的 nodes；每个 record 按自身成员逆序
   销毁 KRKR texture map、Win texture map、retained PSB owner；
2. 销毁 `_usedLayerIds` tree；
3. 释放 `_randomGenerator` Variant；
4. 销毁已为空的 outer unordered_map 并释放 bucket storage；
5. 进入 `SourceCache` teardown：销毁 cache entry list，随后按成员逆序释放
   `_bufLayer`、`_primaryLayer`、`_owner`。

destructor 不调用 public `clearCache()`，所以 cache entry Layer 在 destruction 中只随
Variant owner 释放，不发送 `Invalidate`。这与显式脚本 `clearCache()` 的 observable
行为不同。

## 与 emoteplayer 注入路径的所有权关系

`motionplayer.dll` 的 Motion namespace registrar 把 `ResourceManager` class object 作为
第九个 subclass 发布。独立 `emoteplayer.dll` registrar 先确保 motionplayer module 已加载，
取得同一个 `Motion.ResourceManager` class object，再注册：

- `setEmotePSBDecryptSeed`
- `setEmotePSBDecryptFunc`

所以运行时用户会在同一个 class object 上看见 12 项基础表和两个 decrypt setter，但
基础 `ResourceManager` registrar 本身仍只有 12 项。把 setter 写进本表会改变注册失败时
的 partial-prefix、模块依赖边界和 emoteplayer 未加载时的可见面。

## 本地修正

- `main.cpp` 删除 ResourceManager block 的旧四地址编译注释，改为四端共同的语义说明：
  精确 12 项、一个 RO property、其余 typed method、零 raw callback。
- `ResourceManager.h` 把 `_usedLayerIds` 恢复为 initially-empty set，并让两个尾部 `1`
  由 constructor body 写入；保留 `_spec=0` 的独立初始化。
- `ResourceManager.cpp` 的两个本地 constructor 都恢复
  `RandomGenerator eval -> insert(0) -> next=1 -> state=1` 顺序。
- 新增真实 `Motion.ResourceManager` class-object `CreateNew` 回归，覆盖零参数、单个 Void
  adaptor shell、普通单参数失败、三参数 surplus acceptance、native publication、
  persistent `bufLayer` 与首次 layer-id `1`。

这里没有改动 12 个 native method body；它们已经分别在 SourceCache、module-map、motion
query/random、findSource 与 layer-id allocator 的四端纵切面中闭合。

## Recovery IDB 改进

四份 recovery IDB 已完成并原位保存：

- registrar 写入“一 constructor + 精确 12 typed descriptor + decrypt setter 外部注入”注释；
- constructor register/factory/install（可独立识别者）、typed `FuncCall`、attach、allocation、
  arg0 staging 与 iOS cleanup thunk 统一按 `_guess` 规则命名；
- 四个 typed constructor `FuncCall` 应用统一八参数 dispatch prototype；
- native constructor/destructor 的既有 SourceCache、map ABI、sentinel 与 teardown 注释复核；
- rename/type/comment 后对四个 registrar 和四个 `FuncCall` 全部 fresh decompile；无
  decompiler error。Android ARMv7 仍报告 Hex-Rays local-variable-allocation warning，
  但函数体、所有 gate 与返回码完整可读；没有把 warning 伪报成语义失败；
- 四份 IDB 均保存成功。

## 验证

- 整份 `motionplayer-dll.cpp` Emscripten TU syntax check 通过；只保留仓库既有 `_tss`
  literal-operator deprecation warning。
- `cmake --build --preset "Web Debug Build"` 重新编译并链接通过。
- `git diff --check` 通过；仅出现仓库既有的 LF/CRLF 提示时不视为内容错误。
- 当前工程没有配置可直接运行这份 Catch2 motionplayer TU 的 native unit executable；
  新测试完成整 TU 编译验证，但不伪造运行结论。

相邻已闭合纵切面：

- `analysis/motionplayer_source_cache_buf_layer_four_binary_2026-08-13.md`
- `analysis/motionplayer_source_cache_load_source_four_binary_2026-08-13.md`
- `analysis/motionplayer_resource_manager_module_map_lifecycle_four_binary_2026-08-14.md`
- `analysis/motionplayer_resource_manager_motion_queries_random_four_binary_2026-08-14.md`
- `analysis/motionplayer_resource_manager_layer_id_allocator_four_binary_2026-08-14.md`
- `analysis/motionplayer_objsource_texture_owner_lifecycle_four_binary_2026-08-14.md`

