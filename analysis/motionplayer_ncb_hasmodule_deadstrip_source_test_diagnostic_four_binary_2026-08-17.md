# NCB HasModule final-image absence 与 source/test diagnostic 边界四端审计

日期：2026-08-17

## 1. 为什么需要单独纠偏

V210/V212 为说明 indexed/registered/storage 三种状态，曾把当前端口
`ncbAutoRegister::HasModule` 写进状态矩阵。该 helper 在 `ncbind.hpp` 中是 inline：

```cpp
static bool HasModule(const ttstr &name) {
    return _internal_plugins.find(name) != _internal_plugins.end();
}
```

但用户目标要求所有“参考行为”回到 `reference/binaries/` 四个当前二进制，而不是把本地/旧源码
自动当作事实。本轮因此重新审计四端 internal-map 的完整 xref 和所有含 `HasModule` 的 IDB
entity/name，区分：

- binary 可证的 internal-map 状态；
- final image 中是否存在 pure-query native function；
- 当前端口 inline helper 自己的 source/test contract。

## 2. 四端共同结论

1. 四份最终参考镜像均不存在命名或可识别为 `HasModule` 的 function/entity；
2. 四端 internal map 的完整 direct runtime xref 都只有两类业务函数：
   `AllRegist(line)` append/index builder 与 inner `LoadModule` lookup/callback pipeline；
3. 第三类 direct xref 是 global container initializer；destructor 通过 atexit object pointer 接收
   map，不形成另一个 direct base xref，也不是 query surface；
4. 因此最终参考产品没有 standalone pure-query ABI/script method，可让调用方只观察 indexed
   state 而不尝试 load；
5. 当前端口产品源码也没有 product callsite；Web/Wasmtime 的 `llvm-nm --demangle` 都找不到
   `ncbAutoRegister::HasModule`，它只因 unit-test TU 调用而作为 inline test code 存在；
6. 当前 inline helper 的 exact、non-lowercasing、non-mutating 行为可以作为端口 source/test
   contract 测试，但不能写成“四参考反编译已证明的 HasModule ABI”；
7. 旧报告已纠偏：reference observable state 改写为 internal-map hit/miss，`HasModule` 明确标为
   port source/test diagnostic。

## 3. 四端 internal-map xref 闭包

| 目标 | internal map | initializer | `AllRegist(line)` | inner `LoadModule` | standalone query |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x1AB5968` | `0x42F408` | `0x548EAC` | `0x701DE8` | 0 |
| Android armv7 | `0x1111BE4` | `0x3018E0` | `0x4A96A0` | `0x5BA8E8` | 0 |
| iOS arm64 | `0x10256B928` | `0x1002A03DC` | `0x100287B8C` | `0x10029FDE4` | 0 |
| iOS armv7 | `0x218F19C` | `0x2A4DD8` | `0x28A950` | `0x2A48FC` | 0 |

GOT/literal-pool address cells不算业务 consumer。Android armv7/iOS armv7 因 position-independent
address materialization 对同一 global 产生多条 data xref，但全部仍汇入表中三个函数类别。

四库对 function/name regex `(?i)HasModule` 都返回零结果。由于 map base 的所有 direct xref 已
闭合，这不是“只因 stripped name 搜不到”：若保留一个 standalone tree-find function，它仍必须
读取同一 map header/root，并出现在 xref 集合中。

## 4. final-image 可证状态模型

四端可以直接证明：

```text
static registrar chains
  -> AllRegist appends borrowed pointers into internal map
  -> inner LoadModule looks up map and executes callbacks
  -> successful pipeline inserts registered marker
```

所以以下状态仍成立，但 observation 名称要严谨：

| 状态 | internal map | registered set | final-image pure query |
|---|---|---|---|
| static registrar 构造后、AllRegist 前 | miss | miss | 无 |
| AllRegist 索引后 | hit | miss | 无 |
| callback 成功后 | hit | hit | 无 |
| callback prefix 后抛异常 | hit | miss | 无 |

internal-map hit 是从容器与 loader/indexer control flow 得出的事实，不要求存在 `HasModule` API。
对最终产品调用方而言，实际可用入口是 public `LoadModule`/script `Plugins.link`：它们会尝试
执行 callback并可能改变 registered/script/native state，不是 pure observation。

## 5. 当前端口 inline helper 的独立契约

虽然它不是 reference final-image surface，当前 helper 的源码语义很明确：

```cpp
return _internal_plugins.find(input) != _internal_plugins.end();
```

因此仅对当前端口 source/test 而言：

- 不调用 `AsLowerCase`/`ToLowerCase`；
- 不提取 basename/path，不改扩展名；
- 不读取 `TVPRegisteredPlugins`；
- 使用 `find` 而非 `operator[]`，miss 不创建空 module node/list；
- 不执行 callback、不提交 marker、不修改 list；
- map key 由 AllRegist 预先 lowercase，所以 `motionplayer.dll` 可 hit，
  `MOTIONPLAYER.DLL`、`plugin/motionplayer.dll` 和 empty key miss。

这与 public loader 有意不同：public loader先 lowercases complete ttstr，再进入 inner loader；因此
uppercase basename 可 load，而 source/test `HasModule(uppercase)` 返回 false。不能用 helper 的
false 预测 public loader 一定 miss。

## 6. 当前源码与测试纠偏

### 6.1 `ncbind.hpp`

保留 inline helper，因为 unit fixture 需要无副作用地断言测试 registrar 已被本地 AllRegist
索引；但注释现在明确：

- helper 是 integrated port 的 source/test diagnostic；
- 四参考 final image 没有对应 function/ABI；
- exact/non-normalizing 行为来自当前源码，不冒充 binary recovery。

没有删除 helper，也没有为 final image 不存在的 surface反向添加 export/script method。当前
Web/Wasmtime 产品本来就不 emit 它，因此保留测试便利不改变产品。

### 6.2 `PluginImpl.cpp` 与旧分析

startup 注释从“remain discoverable through HasModule”改成“remain indexed in internal map”。
V210/V212/V215 报告中的状态矩阵和测试说明都补上 source/test 限定，reference 列只使用
internal-map hit/miss。

### 6.3 回归

既有 autoload probe 在 lowercase `autoload-path-probe.dll` hit 后，新增断言：

- uppercase key miss；
- full path key miss；
- empty key miss；
- 三次 miss 后 lowercase key仍 hit。

这些断言明确标注为 port source/test contract；它们验证 exact `find` 无 normalization/插入副作用，
不声称执行了 reference HasModule 函数。

## 7. recovery IDB 写回

四库累计写回并保存关闭：

- 4 项 semantic rename：四端 inner loader 统一为
  `ncbAutoRegister_LoadModuleImpl_guess`；
- 12 项 append comment：每端 internal map、AllRegist、inner loader；
- 8 项 bookmark：每端 map xref closure 与 sole runtime lookup consumer；
- 不创建虚假的 HasModule function label/type/bookmark。

四库均已保存，最终 `idb_list` 为零 session。

## 8. 验证与产物

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` syntax-only 均通过；
- Web 82-step、Wasmtime 119-step 受影响 TU 全量重编译并成功链接；
- 两个 build tree 的 CTest 均返回 0，仍无已注册 CTest；
- 两份 Wasm 均 `WebAssembly.validate == true`；
- imports/exports 保持 Web `539/69`、Wasmtime `538/69`；
- `llvm-nm --demangle` 在两份产品 Wasm 都无 `HasModule`；
- `git diff --check` 返回 0，仅有仓库既有 LF/CRLF 提示。

产品产物与 V215 字节级一致：

| 产物 | size | SHA-256 |
|---|---:|---|
| Web `index.wasm` | 85,657,793 B | `858A3677901252A11D37637BC3BE7423D1ACD9D019080E64E18276379CE49D55` |
| Wasmtime `index.wasm` | 85,004,934 B | `FC8847E666976A424C9BD1A4780E5124F071D114CB6373B1F6985AC350A22C08` |

section 同样不变：

| section | Web | Wasmtime |
|---|---:|---:|
| FUNCTION | `0x1BD2D` | `0x1BA4C` |
| GLOBAL | `0xD5C2` | `0xD5EA` |
| CODE | `0x1A41AB5` | `0x19E9A63` |
| DATA | `0x5A3F00` | `0x5A1150` |
| name | `0x3185E4E` | `0x3141CE4` |

## 9. 未过度推断的部分

- final image 无 helper 不能证明原始开发源码从未声明 inline `HasModule`；能证明的是它没有
  surviving code/ABI/consumer，不能作为运行时参考 surface；
- 当前 helper 的 exact behavior 是端口源码契约，不倒灌成 binary fact；
- 本纵切面只闭合 HasModule 的证据身份与 map consumer closure，不代表 motionplayer 总目标完成。
