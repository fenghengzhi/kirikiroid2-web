# ResourceManager NCB 注册面四参考二进制联合恢复

日期：2026-08-27

## 1. slice 边界

本轮闭合 `Motion.ResourceManager` 的 subclass wrapper、独立 ClassInfo setup、13 行成员
registrar、typed constructor 参数边界与全部 callback 入口。方法 body 的 PSB 加载、嵌套
unordered_map、纹理 owner、Layer ID set 和异常边界在本报告中仍作为独立资源/容器 slice；注册面
状态和 body 状态继续分开记录。后续 Layer ID set slice 已闭合其中两个callback，见第7节更新。

## 2. subclass / ClassInfo 调用链

| 平台 | `Motion.ResourceManager` wrapper | delayed setup | member registrar |
|---|---|---|---|
| Android arm64 | `0x6FBEA4` | `0x6FC014` | `0x6A8C9C` |
| Android armv7 | `0x5997C0` | `0x5B6F80` | `0x57C3A8` |
| iOS arm64 | `0x100126154` | `0x10014E6AC` | `0x100102E88` |
| iOS armv7 | `0x1251DC` | `0x150480` | `0x1002FC` |

共同调用链与 `SourceCache` / `ObjSource` 相同：wrapper 根据 registering 状态创建或清理
独立的 `ncbClassInfo<ResourceManager>` tuple，registrar 发布成员，成功注册后才向
`Motion` 发布 subclass adaptor。ResourceManager 的 tuple、class ID、class object 和 adaptor
与 SourceCache 分离；虽然 C++ native 对象以 SourceCache 为首基类，脚本 subclass item
不携带 SourceCache parent metadata 或 pointer adjustment。

## 3. 精确 13 行发布表

| # | 脚本名 | kind | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---|---|---|---|---|---|
| 1 | `<constructor>` | typed constructor `(Variant, int)` | inline `0x6A8CBC` | `0x57C510` | `0x100103030` | `0x10047C` |
| 2 | `loadSource` | method | entry `0x6A4F88` | `0x57ACC8` | `0x1001009AC` | `0xFDB50` |
| 3 | `clearCache` | method | entry `0x6A5818` | `0x57B018` | `0x100100F10` | `0xFE0D4` |
| 4 | `bufLayer` | getter-only property | `0x6A58DC` | `0x57B060` | `0x100100F84` | `0xFE11A` |
| 5 | `load` | method | `0x6A616C` | `0x57B338` | `0x1001012D8` | `0xFE40C` |
| 6 | `unload` | method | `0x6A697C` | `0x57B6F8` | `0x100101A28` | `0xFEC04` |
| 7 | `unloadAll` | method | entry `0x6A60D8` | `0x57B32C` | `0x1001012CC` | `0xFE3FE` |
| 8 | `isExistMotion` | method | `0x6A6AD8` | `0x57B780` | `0x100101AC8` | `0xFECF4` |
| 9 | `findMotion` | method | `0x6A72B4` | `0x57B9F8` | `0x100101E84` | `0xFF11C` |
| 10 | `findSource` | method | `0x6A7F1C` | `0x57BDE0` | `0x100102594` | `0xFF890` |
| 11 | `random` | method | `0x6A894C` | `0x57C1CC` | `0x100102C90` | `0x1000F0` |
| 12 | `requireLayerId` | method | `0x6A8A74` | `0x57C258` | `0x100102D40` | `0x100240` |
| 13 | `releaseLayerId` | method | `0x6A8B30` | `0x57C2C8` | `0x100102DB8` | `0x10028A` |

前三个非构造成员在四端都直接保存 SourceCache registrar 的同一 callback 地址；这里
没有 ResourceManager 专属 forwarding wrapper。`bufLayer` descriptor 的 setter 与 indexed
getter/setter 槽全为 null。其余九行都是普通 typed method，没有 raw callback。

Android arm64 的 `loadSource` 和 `clearCache` 是 `0x6A4CD4` 函数范围中的两个内部入口；
`unloadAll` 是包含 ResourceManager destructor 的 `0x6A5F74` 函数范围中的内部入口。
其余平台为独立小函数。台账保留这些真实 disposition，不人为创建重叠函数。

## 4. typed constructor 证据

| 阶段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| descriptor publish | registrar inline | `0x57C510` | `0x100103030` | `0x10047C` |
| descriptor invoke | `0x6E9A98` | `0x5A7DC0` | `0x10013A644` | `0x13A730` |
| construct + adaptor attach | `0x6E9B70` | `0x5A7E50` | `0x10013A6E8` | `0x13A79C` |
| allocate + argument conversion | `0x6E9C88` | `0x5A7F10` | `0x10013A7D8` | `0x13A8E4` |
| first Variant copy path | `0x6E9DDC` | `0x5A7FB4` | `0x10013A8A4` | `0x13A9F4` |
| native constructor | `0x6A5CAC` | `0x57B1EC` | `0x100101158` | `0xFE254` |

四端共同边界：

```text
ResourceManagerConstructorInvoke(memberName, result, argc, argv, objthis):
    if memberName != null:
        return TJS_E_MEMBERNOTFOUND
    if special one-argument native-instance probe succeeds:
        return TJS_S_OK
    if argc < 2:
        return TJS_E_BADPARAMCOUNT

    kag = copy Variant(argv[0])
    cacheSize = convert argv[1] to tjs_int
    native = new ResourceManager(kag, cacheSize)
    if adaptor/native-instance attachment fails:
        destroy native
        delete native
        return TJS_E_NATIVECLASSCRASH
    publish native pointer
    return TJS_S_OK
```

`argc >= 2`，因此额外参数被忽略；第一参数完整按 `tTJSVariant` copy/assign/copy-construct
链传入，第二参数通过 Variant integer conversion 变成 32 位 `tjs_int`。这排除了零参数
脚本构造器、只接收 owner 的构造器以及参数顺序倒置。四端分配大小不同仅反映 ABI/STL
布局，不应以 padding 复刻进源结构。

## 5. registrar 共同伪代码

```text
registerMembers(state):
    publishConstructor((tTJSVariant, tjs_int))
    publishMethod("loadSource", SourceCache::loadSource callback)
    publishMethod("clearCache", SourceCache::clearCache callback)
    publishReadOnlyProperty("bufLayer", SourceCache::getBufLayer callback)
    publishMethod("load", load callback)
    publishMethod("unload", unload callback)
    publishMethod("unloadAll", unloadAll callback)
    publishMethod("isExistMotion", isExistMotion callback)
    publishMethod("findMotion", findMotion callback)
    publishMethod("findSource", findSource callback)
    publishMethod("random", random callback)
    publishMethod("requireLayerId", requireLayerId callback)
    publishMethod("releaseLayerId", releaseLayerId callback)
```

每次 publish 前都重新检查 registering flag；Android arm64 展开 descriptor 分配和异常
清理，其余平台主要调用模板 helper。发布顺序、descriptor kind、callback 等价类相同。

## 6. 本地逐行对照

`cpp/plugins/motionplayer/main.cpp` 当前 `NCB_REGISTER_SUBCLASS(ResourceManager)` 的 13 行
与上表完全一致，包括：

- `NCB_CONSTRUCTOR((tTJSVariant, tjs_int))`；
- 显式重列 `loadSource`、`clearCache` 和只读 `bufLayer`；
- `load` 到 `releaseLayerId` 的九个普通 method；
- 不包含 `setEmotePSBDecryptSeed` / `setEmotePSBDecryptFunc`，它们由 emoteplayer 的独立
  registrar 注入。

因此本轮无需修改运行时 C++。

## 7. fresh 证据与剩余工作

- 完整读取四个 wrapper：89/25/28/25 条指令；
- 完整读取四个 delayed setup：80/48/32/71 条指令；
- 完整读取四个 registrar：385/107/94/118 条指令；
- 完整读取三端独立 constructor publisher 以及四端 descriptor invoke、construct/attach、
  allocate/convert、首参数 Variant copy 链；Android arm64 publisher 已包含在完整 registrar；
- 对所有 13 行完成四端 fresh function map / xref；
- 四个 IDB 已完成 wrapper/setup/registrar/constructor chain/独立 callback 命名，关键入口
  已注释并添加 registrar/typed-constructor 书签，数据库全部原位保存；
- 本注册面 slice 当时只把 13 条注册面提升为 `EVIDENCED_4_4`。后续
  `analysis/motionplayer_resourcemanager_layer_id_set_four_binary_2026-08-27.md` 已独立闭合
  `requireLayerId` / `releaseLayerId`，
  `analysis/motionplayer_resourcemanager_random_dispatch_four_binary_2026-08-27.md` 已闭合
  `random`，`analysis/motionplayer_resourcemanager_unload_all_map_clear_four_binary_2026-08-27.md`
  已闭合 `unloadAll` 的纯 module-map clear 与完整 owner 链，
  `analysis/motionplayer_resourcemanager_unload_single_node_four_binary_2026-08-27.md` 已闭合
  `unload` 的 normalization、精确key查找、单node摘除和owner析构，
  `analysis/motionplayer_resourcemanager_load_cache_validate_dispatch_four_binary_2026-08-27.md`
  已闭合 `load` 的cache hit/miss、严格PSB校验、粘滞spec、map插入和fresh dispatch owner；
  `analysis/motionplayer_resourcemanager_is_exist_motion_direct_fallback_scan_four_binary_2026-08-27.md`
  已闭合 `isExistMotion` 的query split、project定向查找、全map回退、strict/contains raw导航和
  owner/异常边，
  `analysis/motionplayer_resourcemanager_find_motion_dispatch_array_owner_four_binary_2026-08-27.md`
  已闭合 `findMotion` 的fresh raw dispatch、两元素Array、实际module key、deque owner与发布前泄漏边；
  `analysis/motionplayer_resourcemanager_find_source_blank_objsource_owner_four_binary_2026-08-27.md`
  已闭合 `findSource` 的`src/blank`分流、raw icon导航、ObjSource adaptor、Dictionary字段/类型和
  失败owner边；
  `analysis/motionplayer_sourcecache_load_clear_buflayer_four_binary_2026-08-27.md` 又闭合了
  ResourceManager 原样复用的 `loadSource` / `clearCache` / `bufLayer`，包括 cache list、
  trim/bake/tint、persistent scratch Layer 和异常边。至此 12 个非构造 callback body 全部为
  `IMPLEMENTED`；typed constructor 继续单列为 `CONSTRUCTOR_EVIDENCED_4_4`。

ResourceManager 的公开 callback body 已闭合，但这不替代完整 root-reachable native helper
分母、unordered_map/list/STL ABI 台账和跨 Player/ObjSource/renderer owner 图的最终审计。
