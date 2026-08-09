# Follow-up：`SourceCache` 构造与 descriptor-keyed cache 闭环

日期：`2026-07-26`。本文件记录沿 psbfile/raw-node 真实 motionplayer consumer 继续追踪后，
对 `SourceCache_ctor@0x6A78F4` 与 `SourceCache_loadSource@0x6A7BA8` 完成的闭环。
这两个函数都不属于 psbfile 114-address MANIFEST，因此不改变 114 份逐函数报告的地址集合、
六维统计分母或 `99 ALIGNED / 15 EVIDENCE_LIMITED / 0 HAS_GAP` 计数。唯一权威来源仍是
Android kirikiroid2 `libkrkr2.so`。

## Fresh Android 证据：构造函数 `0x6A78F4`

本轮 fresh IDA MCP `decompile(addr="0x6A78F4")` 证明：构造函数先把 owner 独立复制进成员、
初始化当前 cache 状态和 list、保存 cache limit；随后另由仍在作用域内的**原始形参
`owner`** 构造临时严格 Object accessor，并读取 `primaryLayer`。该 accessor 与成员 `_owner`
是两条独立的 Variant/引用生命周期，不能把前者改写成从后者构造。函数从全局 script
dispatch 直接调用成员 `CreateNew(L"Layer", 2, {_owner, primaryLayer})`，把返回 dispatch 包进
Variant 后依次释放初始 created 引用和 global 引用，再赋给 `_bufLayer`。二进制没有 null
gate、native-layer recovery 或替代 Layer factory。

### Android 关键伪代码（8 行）

```text
this.owner = copy(owner); currentBytes = 0; init(entries); limit = cacheSize
ownerAccessor = retain_strict_accessor(owner)                       // 原始形参
primary = ownerAccessor.GetValueStrict(L"primaryLayer")
global = TVPGetScriptDispatch()
created = global.CreateNew(0, L"Layer", 2, {this.owner, primary}, global)
createdVariant = Variant(created, created)
created.Release(); global.Release()
this.primaryLayer = primary; this.bufLayer = copy(createdVariant)
```

### 本地逐项映射

- `cpp/plugins/motionplayer/SourceCache.cpp:511-513` 由成员构造把形参 `owner` 独立复制到
  `_owner` 并保存 `_cacheLimitBytes`；类内默认初始化保持 `_currentCacheBytes == 0`，
  `std::list<Entry>` 按默认构造建立空 cache。
- `SourceCache.cpp:517-520` 另以 `ncbPropAccessor{tTJSVariant(owner)}` 从**原始形参**构造临时
  严格 Object accessor，并读取唯一的 `primaryLayer` 属性；它不复用成员 `_owner`，也没有
  native-layer fallback。
- `SourceCache.cpp:320-335` 的共享 helper 取得 global dispatch，按原顺序传两个 Variant
  参数，直接执行 `CreateNew(0, L"Layer", ..., 2, args, global)`，构造结果 Variant 后释放
  created 初始引用与 global 引用；helper 没有 null 检查或失败恢复。
- `SourceCache.cpp:521-524` 以成员 `_owner` 和刚读取的 `_primaryLayer` 调用 helper，再复制
  helper 结果到 `_bufLayer`；临时 Variant 随后按正常 C++
  生命周期释放；`_primaryLayer` 与 `_bufLayer` 的持有边界和 Android 一致。
- `tests/unit-tests/plugins/motionplayer-dll.cpp:56-77` 的 `UnitTestLayerClass::CreateNew`
  直接守护 global 对 `Layer` 成员解析后的默认成员调用（`membername == nullptr`）、非空
  `objthis`、恰好两个 Object 参数，并重新读取 `params[0].primaryLayer` 验证它与
  `params[1]` 是同一 dispatch；`:102-148` 则建立该 `Layer` class 与
  `kag.primaryLayer` 测试边界，防止 ctor 的 CreateNew 调用链被再次简化。

## Fresh Android 证据：`loadSource@0x6A7BA8`

本轮 fresh IDA MCP `decompile(addr="0x6A7BA8")` 证明：descriptor accessor 先建立临时
AddRef；函数入口构造一个完整 Entry candidate，初始 key/layer 为 Void、src 为 null、
byteWeight 为 0，而 blendMode 与四个 colors 槽不初始化。读取 descriptor 后，同一个 Entry
同时参与 cache hit 比较、颜色更新和 miss materialization；没有另造一个“安全初始化”的
miss Entry。

### Android 关键伪代码（10 行）

```text
accessor = retain_accessor(descriptor); Entry e                 // key/layer Void, src null, bytes=0
                                                              // blend/colors 未初始化
e.key = accessor.GetValue(L"key"); e.src = GetStr(L"src", null); e.blend = GetInt(L"blendMode", 0)
color = accessor.GetValue(L"color")
if color != Void: for i=0..3: e.colors[i] = GetInt(color, i, 0)
else: e.colors[0] = (e.blend & 0xF0) ? 0xFF808080 : 0xFFFFFFFF // [1..3] 不写
if matching node: return unchanged layer, or copy colors+bake+push_front(copy)+erase(old)
trim cache; global = TVPGetScriptDispatch()
e.layer = global.CreateNew(0, L"Layer", 2, {owner, primaryLayer}, global); bake(source, e)
currentBytes += e.byteWeight; entries.push_front(copy(e)); return e.layer
```

### 本地逐项映射

- `cpp/plugins/motionplayer/SourceCache.h:42-53` 的 Entry 字段顺序为
  `key/layer/src/blendMode/colors/byteWeight`；只有 `byteWeight = 0` 有显式 scalar
  initializer。`tTJSVariant` 和 `ttstr` 默认构造分别给出 Void/null，`blendMode` 与
  `colors[4]` 则保持未初始化。
- `SourceCache.cpp:530-539` 先构造 descriptor accessor，再只构造一次完整 `Entry entry`；
  accessor 承担 Android 临时 AddRef/Release，Entry 不保留 borrowed `source`。
- `SourceCache.cpp:541-546` 通过 accessor 复制 key，按 empty/null `ttstr` 默认值读取 src，
  并以 0 为 blendMode 默认值；这些读取都落到同一个 candidate。
- `SourceCache.cpp:548-562` 在 color 非 Void 时逐项以 0 为默认值读取四个元素；Void 分支只
  写 `colors[0]`，刻意不写 `[1..3]`，没有添加安全零初始化。
- `SourceCache.cpp:564-580` 用同一 Entry 完成 hit key/src/blend 比较与 packed-color 比较；
  颜色变化时先更新旧节点并 bake，然后执行 `push_front(*it)` + `erase(it)`。这不是
  `splice`：copy-front 先增加 key/Layer/src 的引用，erase 随后按旧节点的
  src → Layer → key 生命周期销毁。
- `SourceCache.cpp:584-595` miss 路径仍使用该 Entry：先 trim，经
  `createLayerVariantLike_0x6A78F4_0x6A7BA8` 直接调用 global `CreateNew`，再 bake、累计
  `byteWeight`、copy-front 并返回 layer；没有 fallback factory 或第二套 cache payload。

## 六维影响

| 维度 | 闭环影响 |
| --- | --- |
| 源代码结构 | 恢复 ctor 的严格 owner accessor 与直接全局 Layer construction；恢复入口单一完整 Entry candidate，删除拆分 helper/安全补值结构。 |
| 数据流 | `owner → primaryLayer → CreateNew` 两参数链、descriptor 各字段到同一 Entry、以及该 Entry 从查找到 miss materialization 的流向均与 Android 对齐。 |
| 调用链 | ctor 和 miss 都走 `TVPGetScriptDispatch → CreateNew(L"Layer") → Variant → Release`；没有本地 native-layer/factory recovery 支路。 |
| 对象生命周期 | owner/primary/bufLayer 持有、descriptor accessor 临时 retain、created/global Release，以及 list copy-front 后 erase-old 的引用次序均已恢复。 |
| 内部容器实现 | 保持 Android 的 descriptor-keyed `std::list<Entry>`，hit 变更为 copy-front+erase，miss 为 copy-front；没有第二套 by-name cache topology。 |
| 边界行为 | CreateNew 失败/null 不作安全恢复；Entry 未初始化槽与 Void color 只写 `[0]` 的 first-fault/indeterminate 边界按二进制保留。 |

## by-name facade 的证据边界

本地仍保留 `SourceCache::loadSourceByName`、`Player::loadSource` 与
`loadRawSourceVariant` 这组 Web compatibility facade。当前二进制 negative evidence 无法区分
“原源码不存在”与“源码存在但被 stripped/O3/链接器消除”，因此不能仅据未发现 emitted
函数就删除这些源码 token。它们没有建立第二套 cache topology，也不改变上述 Android
descriptor-keyed `std::list<Entry>` 闭环；该部分继续记作链接器消除歧义下的证据受限边界，
而不是确定 GAP。此结论同样不改变 psbfile 114-address 统计。

## 已运行验证

- `motionplayer-ttstr-hash-test`：`109 assertions in 24 test cases`；
- `motionplayer-dll`：`1386 assertions in 21 test cases`；
- `psbfile-dll`：`577 assertions in 10 test cases`。

这些现有测试用于守护引用计数、cache 命中/失配和插件集成不回归；对未初始化颜色槽、
CreateNew 失败边界等现有 fixture 未覆盖的路径，正确性仍以本轮 fresh Android 反编译证据
为权威，不从零制造物料，也不把未覆盖误写成 defer 理由。

最终当前源码的 Web Debug 已重编、链接通过；最后一处 `cpp/` 更新后也重新构建了
Wasmtime guest。m2logo/yuzulogo 完整捕获 25/63 帧，当前 trace 与汇总记录的 SHA-256
一致；structural comparator 仍复现既有 31/21 个 opacity ±1 mismatch。它们只承担
非回归守护，不覆盖 Entry 未初始化槽或 CreateNew 失败边界；完整结果见
[SUMMARY.md](SUMMARY.md)。
