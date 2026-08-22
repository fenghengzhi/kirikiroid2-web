# Motionplayer 插件模块归属、依赖链与注册生命周期（四参考）

日期：2026-08-14

## 1. 结论

本纵切面从 `reference/binaries/` 的 Android arm64、Android armv7、iOS arm64、
iOS armv7 四个当前参考目标重新恢复 motionplayer 相关内建插件的 translation-unit
归属、静态 auto-register 构造顺序、模块依赖、`LoadModule` 容器数据流和注册生命周期。

四端共同结论为：

- 实际存在四个相关模块名：`motionplayer.dll`、`emoteplayer.dll`、
  `DrawDeviceD3D.dll` 和依赖别名 `DrawDeviceD3DZ.dll`；没有
  `d3demoteplayer.dll` 或 `emoteplayer_d3d.dll`；
- `D3DEmoteModule` 与 `D3DEmotePlayer` 不属于 `emoteplayer.dll`，而是与
  `DrawDeviceD3D`、`D3D`、`D3DLayer`、`D3DImage`、`D3DPicture` 位于同一个
  `DrawDeviceD3D.dll` translation-unit 静态初始化 bundle；
- 该 bundle 的七个 class auto-register 对象按上述源码顺序构造；auto-register
  constructor 把对象插到全局链表头，所以最终 `LoadModule` 的 class callback 顺序反转为
  `D3DEmotePlayer -> D3DEmoteModule -> D3DPicture -> D3DImage -> D3DLayer ->
  D3D -> DrawDeviceD3D`；
- `DrawDeviceD3D.dll` 的 PreRegist callback 依次直接注册并 first-publish
  `D3DLayerBase` native class ID、加载 `emoteplayer.dll`、直接注册并覆盖
  `D3DLayerObjectNativeInstance` native class ID word；依赖加载返回值被忽略；
- `DrawDeviceD3DZ.dll` 的唯一 PreRegist 并非空函数；它加载
  `DrawDeviceD3D.dll` 并忽略 bool，因此是独立提交 marker 的 dependency alias；
- `emoteplayer.dll` 只有一个 PreRegist callback。它先加载 `motionplayer.dll`，再把
  `EmotePlayer` 发布到 `Motion`，最后向 `Motion.ResourceManager` 注入两个 PSB decrypt
  setter；
- 因而完整依赖方向是
  `DrawDeviceD3DZ.dll -> DrawDeviceD3D.dll -> emoteplayer.dll -> motionplayer.dll`；
  没有反向边；
- 引擎启动只为三条 NCB line 建索引并 eager load `xp3filter.dll`；motion/emote/
  DrawDevice 可由内部 map 发现，但要等 `Plugins.link` 或依赖 callback 才真正注册；
- `ncbAutoRegister::LoadModule` 对模块名建立小写临时值；命中“已经注册”或内部模块 map
  miss 都返回 `false`，不是幂等成功；
- 成功路径严格按 `PreRegist -> ClassRegist -> PostRegist` 三行执行，每一行按内部
  `std::list` 正向遍历；所有 callback 都正常返回之后才向全局 registered set 插入模块名；
- callback 抛异常时不会插入 registered set，但之前已经发生的脚本发布、native class ID
  注册或依赖加载不回滚。以后重试会从该模块的第一个 PreRegist callback 重新开始；
- 当前集成式 loader 没有 module unload 路径。auto-register 类型仍包含
  `Unregist(isRegist=false)` wrapper；Android final image 还保留无内部 caller 的 aggregate
  `AllUnregist`，iOS 将 aggregate traversal dead-strip，但 `LoadModule`/`Plugins.unlink`/static
  container teardown 均不调用这些 wrapper；
- 本地源码此前把两个 D3D class registrar 放在 `motionplayer/main.cpp` 的
  `emoteplayer.dll` 区域，并把重复加载返回值实现成 `true`。两处均与四端不符，现已修正。

本文使用绝对地址仅作为四参考证据映射。可编译源码注释只保留语义说明；无法由 stripped
产物确定的原始 C++ 名继续使用 `_guess`。

## 2. 四端根地址

### 2.1 模块 bundle、PreRegist 与 loader

| 简称 | `DrawDeviceD3D` static init | DrawDevice PreRegist | `emoteplayer` static init | Emote PreRegist | public `LoadModule(ttstr)` | inner loader |
|---|---:|---:|---:|---:|---:|---:|
| A64 | `0x42CBD8` | `0x53101C` | `0x42EEE0` | `0x67F908` | `0x548E24` | `0x701DE8` |
| A32 | `0x2FF094` | `0x49516C` | `0x3013BC` | `0x5623EC` | `0x4A9648` | `0x5BA8E8` |
| I64 | `0x10024CB00` | `0x1002335C8` | `0x1001CAE20` | `0x1001B65DC` | `0x100287B38` | `0x10029FDE4` |
| I32 | `0x24E6D8` | `0x2323C0` | `0x1C8EB2` | `0x1B645C` | `0x28A8A4` | `0x2A48FC` |

较早的 recovery IDB 把四个 `emoteplayer` static init 函数命名成了
`motionplayer_staticInit_guess`。新鲜 string xref、callback target 和模块名对象共同证明该
名字过时；四份 IDB 已改为 `emoteplayer_registration_staticInit_guess`。

### 2.2 D3D class auto-register wrapper

| 简称 | Module `Regist` | Module `Unregist` | Player `Regist` | Player `Unregist` |
|---|---:|---:|---:|---:|
| A64 | `0x540EC4` | `0x540F54` | `0x542178` | `0x5422DC` |
| A32 | `0x4A2B40` | `0x4A2BC4` | `0x4A3AD0` | `0x4A3B54` |
| I64 | `0x100244320` | `0x100244388` | `0x100245634` | `0x10024569C` |
| I32 | `0x2446D4` | `0x244788` | `0x245D28` | `0x245DDC` |

每对第一个 wrapper 都向共享 native-class registration template 传
`isRegist=true`，第二个传 `isRegist=false`。这同时验证它们确实是 class-line auto-register
对象的虚函数实现，而不是普通脚本 method callback。

## 3. 模块名与 class 名 string 证据

### 3.1 模块名

以 UTF-16LE 并包含终止零搜索：

| string | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| `motionplayer.dll` | `0x14D4222` | `0xD84BA8` | `0x10195B980`, `0x1019609F6` | `0x174DCE4`, `0x1752D5A` |
| `emoteplayer.dll` | `0x14BF2B8` | `0x30142C`, `0x4951FC` | `0x1019609D2`, `0x101970640` | `0x1752D36`, `0x17629EC` |
| `DrawDeviceD3D.dll` | `0x14BE332` | `0xD7625E` | `0x10196F620` | `0x17619CC` |
| `DrawDeviceD3DZ.dll` | `0x14BEEDA` | `0xD76C7E` | `0x101970258` | `0x1762604` |

四端对 `d3demoteplayer.dll` 和 `emoteplayer_d3d.dll` 的 ASCII、UTF-16LE、UTF-32LE 搜索
都为零结果。因此不能为两个 D3D class 虚构第四个 module，也不能再仅凭类名前缀把它们
归入 `emoteplayer.dll`。

### 3.2 DrawDevice translation-unit 的七个 class 名

| class | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| `DrawDeviceD3D` | `0x14BE356` | `0x2FF1DC` | `0x10196F644` | `0x17619F0` |
| `D3D` | `0x15531B8` | `0x2FF1FC` | `0x10196F960` | `0x1761D0C` |
| `D3DLayer` | `0x14BE62A` | `0xD7655A` | `0x10196F968` | `0x1761D14` |
| `D3DImage` | `0x14BE70C` | `0xD765C8` | `0x10196FA5A` | `0x1761E06` |
| `D3DPicture` | `0x14BE71E` | `0xD765DA` | `0x10196FA90` | `0x1761E3C` |
| `D3DEmoteModule` | `0x14BE7D0` | `0xD76648` | `0x10196FB42` | `0x1761EEE` |
| `D3DEmotePlayer` | `0x14BE8D6` | `0xD766FE` | `0x10196FC48` | `0x1761FF4` |

每个目标的七条 xref 都汇聚到同一个 static-init bundle，而不是分别落在 DrawDevice 与
emoteplayer 的 bundle。这个汇聚关系是本次 class module 归属修正的直接证据。

`D3D` 的短字符串在四端都可由 UTF-16 后缀池或相邻静态对象取址隐藏，不能仅按 IDA
默认 string item 数量判断 class 数量。对 static-init 指令和宽字符串字节重新做 xref 后，
七个 registrar 的链才完整闭合。

## 4. `DrawDeviceD3D.dll` 静态注册拓扑

### 4.1 源码构造顺序

四端 static-init bundle 共同构造以下 class auto-register 对象：

```text
DrawDeviceD3D
  -> D3D
  -> D3DLayer
  -> D3DImage
  -> D3DPicture
  -> D3DEmoteModule
  -> D3DEmotePlayer
```

这个顺序对应一个 translation unit 中七个 `NCB_REGISTER_CLASS` declaration 的源代码顺序。
它不是 `LoadModule` 的执行顺序。

`ncbAutoRegister` constructor 的共同等价行为是：

```cpp
item->_next = top[line];
top[line] = item;
```

`AllRegist(line)` 随后从 `top[line]` 向 `_next` 遍历，并用 `push_back` 把指针加入该 module
的行列表。因此七个 class callback 在内部 list 和 `LoadModule` 中的顺序为：

```text
D3DEmotePlayer
  -> D3DEmoteModule
  -> D3DPicture
  -> D3DImage
  -> D3DLayer
  -> D3D
  -> DrawDeviceD3D
```

本地宏实现已有相同的 head insertion 与 `push_back` 数据流。修复 class declaration 的
translation-unit 归属和源码顺序后，无需为运行时顺序另写排序代码。

### 4.2 callback 对象

同一个 bundle 在七个 class auto-register 对象之后还构造：

1. module name 为 `DrawDeviceD3D.dll`、目标为非空 DrawDevice PreRegist 的 callback object；
2. module name 为 `DrawDeviceD3DZ.dll`、目标为 dependency shim 的 PreRegist callback
   object。该 callback 调 public `LoadModule("DrawDeviceD3D.dll")` 并忽略 bool。

`DrawDeviceD3DZ.dll` 没有上述七个 class 的独立 registrar。它有自己的 internal-map entry 和
registered marker，但其脚本 surface 来自 nested load 的 main module，不能把 dependency alias
误写成空 callback 或第二套 class bundle。

### 4.3 DrawDevice PreRegist 精确顺序

四端共同伪代码：

```cpp
void DrawDeviceD3D_PreRegist_guess() {
    auto baseID = TJSRegisterNativeClass(L"D3DLayerBase");
    D3DLayerBaseClassInfo.Set(L"D3DLayerBase", baseID, nullptr);
    (void)ncbAutoRegister::LoadModule(L"emoteplayer.dll");
    D3DLayerObjectClassID =
        TJSRegisterNativeClass(L"D3DLayerObjectNativeInstance");
}
```

V208 的四端 ClassInfo 纵切面已证明这里没有 `FindNativeClass` fallback：base 是
`{initialized, name, classID, nullptr classObject}` 的 internal ClassInfo first-publication；第二个
ID 只是一个 process-global word，每次 callback 重试都可被直接覆盖。

两个 native class 名地址为：

| string | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| `D3DLayerBase` | `0x14BF29E` | `0xD77012` | `0x101970626` | `0x17629D2` |
| `D3DLayerObjectNativeInstance` | `0x14BF2D8` | `0xD7702C` | `0x101970660` | `0x1762A0C` |

关键生命周期边界：

- 第一个 native class ID 在依赖加载之前发布；
- nested `LoadModule` 的 bool 被丢弃，所以 dependency 已加载时返回 `false` 不会使
  DrawDevice 加载失败；
- dependency 抛异常则继续向外传播，第二个 class ID 尚未注册，外层 DrawDevice module
  名也尚未插入 registered set；
- dependency 正常返回后，无论其 bool 是 true 还是 false，都会继续注册第二个 class ID；
- 外层随后才进入 DrawDevice 的 ClassRegist 行。

本地 `DrawDeviceD3D.cpp` 原先既有错误的 lazy native-ID helper，又把
`DrawDeviceD3DZ_PreRegist` 留空。main callback 已按 direct register/first-publish 恢复，
companion callback 也已按四端恢复为加载 main module。

## 5. `emoteplayer.dll` 的唯一 PreRegist

### 5.1 module partition

四端 `emoteplayer_registration_staticInit_guess` 只构造一个
`emoteplayer.dll` PreRegist callback object。它没有 `D3DEmoteModule` 或
`D3DEmotePlayer` class-line auto-register 对象。

`Motion.EmotePlayer` 本身是一个 delayed subclass，由 callback 手工 setup 和发布；这解释了
为什么它属于 `emoteplayer.dll`，但不会以普通 class auto-register object 的形式出现在同一
static-init bundle。

### 5.2 callback 数据流

四端共同的源级等价顺序为：

```text
1. LoadModule("motionplayer.dll"); ignore bool
2. TVPGetScriptDispatch(); obtain AddRef'd global dispatch
3. global.PropGet("Motion") -> value
4. setup EmotePlayer delayed subclass with isRegist=true; ignore setup result
5. Motion.PropSet("EmotePlayer", class object, static-class flags)
6. Motion.PropGet("ResourceManager") -> reuse value
7. create setEmotePSBDecryptSeed method descriptor
8. convert ResourceManager value to object
9. PropSet descriptor with TJS_MEMBERENSURE | TJS_STATICMEMBER
10. create/reuse method Variant for setEmotePSBDecryptFunc
11. PropSet with the same flags
12. destroy methodValue, then value
```

两次 decrypt setter 的 `PropSet` flags 在四端都为十进制 `66048`，即 `0x10200`，对应
`TJS_MEMBERENSURE | TJS_STATICMEMBER`。

callback 对 AddRef'd global dispatch 没有显式 `Release`。这是四端共同可见的 process-lifetime
引用泄漏/持有边界；本地不能为了“更干净”而在这里额外释放并改变引用计数时序。

另外两个异常边界也必须保留：

- nested motionplayer load 的 bool 被忽略，但异常不会被吞掉；
- EmotePlayer setup 的返回结果不参与后续 gate，callback 仍继续执行 class publication 与
  decrypt setter 注入。

本地 `EmotePlayerPreRegist` 的操作顺序已经与四端一致。本次只删除了错误放在其后的两个
D3D class registrar。

## 6. `ncbAutoRegister` 内部容器与 `LoadModule`

### 6.1 容器形状

四端控制流与本地声明共同恢复出以下源码结构：

```cpp
std::set<ttstr> TVPRegisteredPlugins;

struct INTERNAL_PLUGIN_LISTS {
    std::list<const ncbAutoRegister *> lists[3];
};

std::map<ttstr, INTERNAL_PLUGIN_LISTS> _internal_plugins;
```

三行索引固定为：

```text
0 = PreRegist
1 = ClassRegist
2 = PostRegist
```

这里的 `std::list` 保存静态 auto-register 对象的 borrowed pointer。list 不拥有这些对象；
对象本身具有静态存储期，内部 map/list 只是启动后建立的执行索引。

### 6.2 `AllRegist` 建索引

`AllRegist` 并不执行插件 callback。它对三条全局 head-insert 链分别进行遍历：

```cpp
for each line in Pre, Class, Post:
    for item = top[line]; item; item = item->_next:
        lower = lowercase(item->moduleName)
        _internal_plugins[lower].lists[line].push_back(item)
```

因此：

- module key 在建索引时已经小写化；
- 同 module、同行的顺序保留 head-chain 顺序，也就是 translation-unit 静态构造顺序的反序；
- 三行之间不交错；即使某个 ClassRegist object 在源码上早于 PreRegist callback 构造，
  `LoadModule` 仍先跑整个 PreRegist 行；
- `operator[]` 可在首次见到 module 时 value-initialize 三个空 list。

V215 进一步闭合重复调用边界：这里的 `push_back` 没有 once guard、clear 或 pointer dedupe。
每次 `AllRegist(line)` 都重走完整 head chain并把同一批 borrowed pointer 再 append 一代；异常
只保留已完成 prefix。尚未 committed module 随后会按 line-major 顺序执行全部 occurrence，
已 committed module 则由 registered-set early guard 截断但不清理 dormant duplicate nodes。
四端 list ABI、非事务 generation 与 set/map 静态 teardown 见
`analysis/motionplayer_ncb_repeated_allregist_append_only_index_static_teardown_four_binary_2026-08-17.md`。

### 6.3 public wrapper 与 inner loader

public wrapper 建立规范化的小写 `ttstr` temporary，调用 inner loader，再析构 temporary 并返回
bool。inner loader 的四端共同伪代码为：

```cpp
bool LoadModule_impl_guess(const ttstr &lowerName) {
    if(TVPRegisteredPlugins.find(lowerName) !=
       TVPRegisteredPlugins.end())
        return false;

    auto it = _internal_plugins.find(lowerName);
    if(it == _internal_plugins.end())
        return false;

    for(int line = 0; line != 3; ++line) {
        for(const ncbAutoRegister *item : it->second.lists[line])
            item->Regist();
    }

    TVPRegisteredPlugins.insert(lowerName);
    return true;
}
```

Android arm64 的编译器把三个 list loop 展开，另三端保留明显的三次循环或 loop counter；
语义相同。

### 6.4 返回值矩阵

| 输入状态 | callback | set 插入 | 返回值 |
|---|---|---|---|
| 已在 `TVPRegisteredPlugins` | 不执行 | 不变 | `false` |
| 不在 set，内部 map miss | 不执行 | 不变 | `false` |
| 内部 module 存在，全部 callback 正常 | 三行全部执行 | 最后插入 | `true` |
| callback 抛异常 | 已完成前缀保留 | 不插入 | 异常传播 |

本地旧实现唯一的控制流偏差是第一行返回 `true`。这会把“本次真正执行了注册”与“模块早已
注册”混为一谈，也使 unit fixture 无法覆盖参考边界；现已改为 `false`。

### 6.5 非事务与重入边界

registered set insertion 是整个 callback pipeline 的 commit point，但这不是事务：

- callback 前缀造成的脚本 property、native class ID、class-info pointer 和依赖 module
  注册不会自动撤销；
- 失败模块没有 registered-set marker，重试会重新执行其全部 callback；
- loader 没有单独的 “loading/in progress” 集合。因此如果模块依赖形成同步环，registered
  marker 尚未写入时可能递归进入同一模块。当前三模块依赖是严格有向链，不形成环；
- nested dependency 成功后，即便外层后续 callback 抛异常，dependency 已有自己的 registered
  marker，不会随外层回滚；
- module map miss 不会向 registered set 插入名字，也不会生成一个空 module 执行成功假象。

### 6.6 集成启动只 eager load xp3filter

V210 对四端 startup loader 的重新检查纠正了本报告初版未覆盖的启动边界：

| 目标 | startup loader | `AllRegist(line)` helper | startup `xp3filter.dll` |
|---|---:|---:|---:|
| A64 | `0x548D04` | `0x548EAC` | `0x14BF490` |
| A32 | `0x4A9598` | `0x4A96A0` | `0x4A95F4` |
| I64 | `0x100287ACC` | `0x100287B8C` | `0x101971B64` |
| I32 | `0x28A7DC` | `0x28A950` | `0x1763F10` |

四端共同执行三次 line indexing，然后构造 `ttstr("xp3filter.dll")`、直接调用 inner loader、
忽略 bool 并销毁 temporary。startup 没有 motionplayer/emoteplayer load xref。其余 module 在
`AllRegist` 后只是 indexed/discoverable；真正 callback registration 等待游戏的
`Plugins.link` 或依赖 callback。

V211 进一步确认 script-visible `Plugins.link` 直接把完整首参传给 public NCB loader并丢弃
bool：它不执行 path extraction/`.tpm` rewrite，也不把 missing/already-loaded false 暴露给脚本。
V213 随后确认 autoload 同样没有 basename/storage-name extraction；其平台差异发生在 discovery
record：Android 保留 `.tpm` Name，iOS 改写为 `.dll`，最终都把完整 `Path + "/" + Name` 交给
loader。详情见
`analysis/motionplayer_plugins_link_unlink_getlist_exact_key_registered_set_four_binary_2026-08-17.md`。
autoload 的完整四端证据见
`analysis/motionplayer_physical_tpm_autoload_platform_name_rewrite_full_key_four_binary_2026-08-17.md`。

因此必须区分：

- `_internal_plugins` 命中：module 已索引；
- `TVPRegisteredPlugins` 命中：module pipeline 已成功提交；
- script/native property 已发布：callback 前缀可能已执行，即使异常导致 marker 未提交。

完整启动/依赖证据与产物差异见
`analysis/motionplayer_internal_plugin_startup_xp3_only_drawdeviced3dz_dependency_four_binary_2026-08-17.md`。

## 7. class 对象生命周期与 unload 边界

七个 DrawDevice class auto-register 对象在进程静态初始化期构造，并持有 module name 与 class
name。`AllRegist` 建立 borrowed-pointer 索引；第一次成功 `LoadModule` 才调用它们的
`Regist` 虚函数。

`D3DEmoteModule` 和 `D3DEmotePlayer` 的 wrapper pair 共同显示：

```text
Regist wrapper   -> native-class template(..., isRegist=true)
Unregist wrapper -> native-class template(..., isRegist=false)
```

但是集成式 `LoadModule` 只有注册 pipeline 与 registered set insert，没有：

- 从 registered set erase；
- 逆序运行 Post/Class/Pre Unregist；
- 删除 module 内发布的 global class property；
- 清空 class-info object；
- 释放 dependency 的引用计数式 module ownership。

V217 进一步确认 Android surviving `AllUnregist` 即使被外部触发，也直接按 registrar top chain
执行 forward `PreRegist -> ClassRegist -> PostRegist`，不读取/erase registered set，不读取/clear
internal map，且每个静态 registrar只调用一次，不随 V215 的 duplicate list occurrences 放大。
两份 Android 镜像内 caller 均为零；两份 iOS 没有 aggregate function/top-chain reader。具体
`Unregist` wrapper 仅由 vtable data slot 保留。完整证据见
`analysis/motionplayer_ncb_allunregist_android_survivor_ios_deadstrip_no_unload_consumer_four_binary_2026-08-17.md`。

因此本地测试进程中第一次加载后的 class object 会持续存在。测试不能用一个 port-only static
bool 假装第二次加载仍成功；应读取真实 registered set，并要求第二次 loader 返回 `false`。

## 8. 本地源码修复

### 8.1 class registrar 搬迁

`cpp/plugins/motionplayer/main.cpp` 的 `emoteplayer.dll` 区域现在只包含
`EmotePlayerPreRegist`。以下两块已移动到 `cpp/plugins/DrawDeviceD3D.cpp`，紧随
`D3DPicture` registrar：

- `D3DEmoteModule`：6 property + 1 method，精确 7-member table；
- `D3DEmotePlayer`：4 constant + 10 property + 44 method，精确 54-member table。

移动后 `DrawDeviceD3D.cpp` 的七个 class declaration 顺序与四端 static-init bundle 完全一致。
这里没有直接复制仓库 HEAD 中的旧 D3DEmotePlayer block：那个历史块仍把九个固定签名入口
注册成手写 raw shim，并保留 `getPlayCallback`、`create`、`addPlayCallback` 等已经被当前
四参考 surface 推翻或重命名的 callback。首次完整编译立即暴露这些 stale references；最终
搬迁块以刚闭合的四端 54 项 interleaved table 为准，只有 variadic `load` 保持 raw，其他
43 个 method 全部使用 generated typed descriptor。源码扫描确认旧 `*Compat` shim 引用为零。

由于 `D3DLayer` registrar 现在与 D3DEmotePlayer factory descriptor 位于同一 translation
unit 且更早声明，旧 `main.cpp` 中手写的错误 `NCB_TYPECONV_BOXING(D3DImage)` 也已删除；宏本身会
在正确位置生成 specialization。

### 8.2 dependency callback

`DrawDeviceD3D_PreRegist` 已新增，并严格保持：

```text
D3DLayerBase ID -> emoteplayer load -> D3DLayerObjectNativeInstance ID
```

依赖 loader bool 显式丢弃，避免把参考实现的 “already loaded = false” 误当成外层失败。

`DrawDeviceD3DZ_PreRegist` 也已从空函数恢复为：

```cpp
(void)ncbAutoRegister::LoadModule(TJS_W("DrawDeviceD3D.dll"));
```

它拥有独立 registered marker，但不复制 main module 的七个 class registrar。

### 8.3 loader bool

`cpp/core/plugin/ncbind.cpp` 的 registered-set hit 分支从 `return true` 改为
`return false`。其余 map lookup、三行正向遍历、末尾 insert 与 missing-module false 已经一致，
没有进行额外重构。

### 8.4 startup eager-load 边界

`cpp/core/plugin/PluginImpl.cpp` 的 `TVPLoadInternalPlugins()` 已删除旧
`libkrkr2.so` 推断留下的 motionplayer/emoteplayer eager load；当前只调用 `AllRegist()` 和
`LoadModule("xp3filter.dll")`。unit fixture 因为不执行游戏 `Plugins.link`，仍显式加载
motion/emote，但注释已明确这是测试准备，不是产品 startup 行为。

## 9. 测试与机器校验

`tests/unit-tests/plugins/motionplayer-dll.cpp` 新增/修正：

- `loadInternalModuleExact` 用端口 source/test-only inline `HasModule` 先确认已知内部 module 的
  map hit，再按 registered set 状态断言 loader 结果恰为 `!alreadyLoaded`；V216 已确认四份最终
  参考镜像都 dead-strip 该 helper，它不是参考 ABI；
- fixture 第一次加载 motionplayer/emoteplayer 时接受 `true`，后续无卸载 fixture 请求要求
  `false`；
- DrawDevice ownership test 在 process 尚未加载 DrawDevice 时，要求两个 D3D class-info
  object 和 global class property 都不存在；加载后两者都存在；
- 显式覆盖大小写规范化后的重复 DrawDevice load 返回 `false`；
- 显式覆盖内部 map miss 返回 `false` 且不污染 registered set；
- 显式从 `DrawDeviceD3DZ.dll` 首次加载 main module，并确认 main/companion 两个 marker、
  D3D class-info 与 global class publication；
- 显式覆盖 main 预先已加载时 companion 仍可首次成功，以及两者重复 load 均为 `false`；
- 后续需要 DrawDevice 的 D3DEmotePlayer 测试不再错误要求每次调用都返回 `true`。

源码机器扫描结果：

```text
DrawDevice class source order:
  DrawDeviceD3D -> D3D -> D3DLayer -> D3DImage -> D3DPicture
  -> D3DEmoteModule -> D3DEmotePlayer
D3DEmoteModule table: 6 property + 1 method = 7
D3DEmotePlayer table: 4 constants; 10 property + 44 method = 54
D3D class registrars remaining in motionplayer/main.cpp: 0
DrawDevice PreRegist loads emoteplayer: true
Emote PreRegist loads motionplayer: true
registered-set hit returns false: true
```

完整 test TU Emscripten syntax check 已通过，只有仓库既有 `_tss` literal operator warning。
`Web Debug Build` 随后重新编译 `ncbind.cpp`、`DrawDeviceD3D.cpp` 和 motionplayer 静态库并
成功链接 `index.html`；只剩同一 `_tss` warning、Emscripten pthread/memory-growth 与 JSPI
既有 warning。

## 10. recovery IDB 写回

四份 recovery IDB 均完成：

- 重命名 module static init、两个 PreRegist、public/inner loader 和四个 D3D class
  Regist/Unregist wrapper；所有推断名保留 `_guess`；
- 为 PreRegist、loader 与 class wrapper 写入保守函数原型；
- 注释七 class 源码/运行时双顺序、依赖链、Emote publication、三行 list、异常重试与
  no-unload 边界；
- 增加 DrawDevice bundle、DrawDevice PreRegist、Emote PreRegist、inner loader 书签；
- 类型写入后重新 decompile 四端 inner loader，均显示 false/false/three-list/insert/true
  同构控制流；
- 四份数据库均已成功保存到 `out/ida-recovery/` 对应目标目录。

V210 又在每库补写 startup loader、单 line helper 和 non-empty companion PreRegist：四库合计
12 rename、12 type application、12 set comment、12 append comment、12 bookmark；并覆盖此前
把 companion 标成 empty callback、把 main PreRegist 标成 find-or-register 的过时注释。四库
再次原位保存并关闭。

## 11. 未过度推断的部分

- stripped binary 不能证明原始文件名、namespace 拼写或 helper 的精确源码名；本文的静态
  init、PreRegist 与 inner loader 名仍用 `_guess`；
- 本纵切面只恢复 module-level `std::map`/`std::set`/三 `std::list` 的语义容器结构，没有把
  四端 STL 红黑树/list node 的每个 ABI 字节布局重复展开；
- `DrawDeviceD3DZ.dll` 已确认是 non-empty dependency alias；它没有独立 class registrar，
  但会 nested-load main module 并在正常返回后提交自己的 marker；
- class `Unregist` wrapper 的存在不等于当前 loader 提供 unload API；只有找到实际调用方和
  registered-set erase 才能恢复 module unload，而四端当前链没有这样的路径；
- global dispatch 缺少显式 Release 是当前 callback 的可观察结果，但是否为原作者有意的
  process-lifetime hold 无法由产物区分，因此只记录行为，不命名其设计意图。
