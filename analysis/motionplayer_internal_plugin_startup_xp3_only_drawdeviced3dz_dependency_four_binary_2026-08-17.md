# motionplayer 内建插件启动加载边界与 DrawDeviceD3DZ 依赖别名四端恢复

日期：2026-08-17

## 1. 范围与结论

本轮继续只把 `reference/binaries/` 的 Android arm64-v8a、Android armeabi-v7a、
iOS arm64、iOS armv7 四个当前参考二进制作为事实源，独立检查：

- 引擎启动时建立内建 NCB module 索引的入口；
- 启动入口究竟立即加载哪些 module；
- `motionplayer.dll`、`emoteplayer.dll`、`DrawDeviceD3D.dll` 的索引、注册与依赖边界；
- `DrawDeviceD3DZ.dll` callback 的真实行为；
- callback 返回值、异常传播、registered-set commit 与无回滚边界。

四端共同结论：

1. 启动入口依次为三条 registration line 建索引，然后**只**加载
   `xp3filter.dll`；它不主动加载 `motionplayer.dll` 或 `emoteplayer.dll`；
2. 启动入口直接调用 inner loader，忽略返回 bool；临时 `ttstr` 正常销毁，loader
   异常不被吞掉；
3. `AllRegist` 完成以后，motion/emote/DrawDevice module 已经存在于内部 map，但尚未出现在
   `TVPRegisteredPlugins`，也尚未执行 callback；当前端口的 source/test-only inline
   `HasModule` 可观察该状态，四份最终参考镜像本身不保留该 helper；
4. 游戏脚本的 `Plugins.link` 或其他 module 的 PreRegist 才触发它们的 lazy registration；
5. `DrawDeviceD3DZ.dll` 不是空 callback。它是一个独立可索引、可提交 registered marker
   的依赖别名 module，其唯一 PreRegist 调用 public
   `LoadModule("DrawDeviceD3D.dll")` 并忽略 bool；
6. 因而依赖图为
   `DrawDeviceD3DZ -> DrawDeviceD3D -> emoteplayer -> motionplayer`；
7. 所有 dependency bool 都被丢弃，所以“依赖已经加载”返回 `false` 不会阻止外层成功；
   异常则逐层传播，外层 module 不写 registered marker，但已经完成的前缀不回滚；
8. 当前源码曾沿用旧 `libkrkr2.so` 结论，在启动期额外加载 motion/emote，并把
   `DrawDeviceD3DZ` 写成空 callback；两处均已按四端证据修正。

绝对地址只出现在本报告和 recovery IDB 中；编译源码注释只保留语义。stripped/private
名称继续使用 `_guess`。

## 2. 四端函数映射

| 目标 | 内建启动 loader | `AllRegist(line)` helper | DrawDevice main PreRegist | DrawDeviceD3DZ PreRegist | DrawDevice bundle |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x548D04` | `0x548EAC` | `0x53101C` | `0x5310F0` | `0x42CBD8` |
| Android armv7 | `0x4A9598` | `0x4A96A0` | `0x49516C` | `0x495228` | `0x2FF094` |
| iOS arm64 | `0x100287ACC` | `0x100287B8C` | `0x1002335C8` | `0x100233668` | `0x10024CB00` |
| iOS armv7 | `0x28A7DC` | `0x28A950` | `0x2323C0` | `0x2324C0` | `0x24E6D8` |

recovery IDB 中分别使用：

- `TVPLoadInternalPlugins_guess`；
- `ncbAutoRegister_AllRegistLine_guess`；
- `DrawDeviceD3DZ_PreRegist_guess`。

这些函数的精确私有源码名不能由 stripped binary 证明，所以即使本地有同名实现，恢复名仍
保留 `_guess`。

## 3. 启动入口的数据流

### 3.1 四端共同伪代码

四份反编译的控制流共同等价于：

```cpp
void TVPLoadInternalPlugins_guess() {
    ncbAutoRegister_AllRegistLine_guess(0);
    ncbAutoRegister_AllRegistLine_guess(1);
    ncbAutoRegister_AllRegistLine_guess(2);

    ttstr name(L"xp3filter.dll");
    (void)LoadModule_impl_guess(name);
}
```

`0/1/2` 仍分别是 PreRegist、ClassRegist、PostRegist。三个 helper call 只把静态
head-insert 链转换为 module map 中的三个 `std::list`，不执行 module callback。

这里有两个容易混淆的边界：

- startup 调的是 inner loader，而普通 dependency callback 调 public `LoadModule(ttstr)`；
- `xp3filter.dll` 已经是小写 literal，所以 startup 无需 public wrapper 再构造小写副本。

startup 丢弃 loader bool。`xp3filter.dll` 已注册或 map miss 时仍正常返回；loader callback
抛出的异常则不被转成 bool。临时字符串的异常清理不改变已完成的 NCB 索引。

V215 又确认这个 startup 没有 once guard：每次调用都会让三个 line helper 各把同一条 registrar
head chain 再 append 到 module 的 borrowed-pointer list。已 committed 的 xp3filter 随后由 marker
guard 返回 false，不重跑 callback；尚未 committed 的 motion/emote/DrawDevice 会在以后首次 load
时执行累积 occurrence。完整重复/异常前缀与静态 container teardown 见
`analysis/motionplayer_ncb_repeated_allregist_append_only_index_static_teardown_four_binary_2026-08-17.md`。

### 3.2 startup 字符串证据

| 目标 | startup `xp3filter.dll` | 另一个静态副本（如有） |
|---|---:|---:|
| Android arm64 | `0x14BF490` | 同一 literal pool 即可覆盖 |
| Android armv7 | `0x4A95F4` | `0x301248` |
| iOS arm64 | `0x101971B64` | `0x101956622` |
| iOS armv7 | `0x1763F10` | `0x1748986` |

Android armv7 的 startup literal 被编译器放进代码附近，而另一个副本由静态 registrar 使用；
不能因为只搜索默认 IDA string item 而漏掉前者。iOS 两端也各有 startup 与静态副本。

### 3.3 motion/emote 不在启动加载链

对以下三个 module 名重新做 UTF-16LE 精确字节搜索并逐条检查 executable xref：

| module | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `motionplayer.dll` | `0x14D4222` | `0xD84BA8` | `0x10195B980`, `0x1019609F6` | `0x174DCE4`, `0x1752D5A` |
| `emoteplayer.dll` | `0x14BF2B8` | `0x30142C`, `0x4951FC` | `0x1019609D2`, `0x101970640` | `0x1752D36`, `0x17629EC` |
| `DrawDeviceD3D.dll` | `0x14BE332` | `0xD7625E` | `0x10196F620` | `0x17619CC` |

四端均没有从 startup loader 指向 motion/emote 的 executable xref。除静态 registrar 的
module-name 构造外，可执行消费关系为：

- Emote PreRegist -> `motionplayer.dll`；
- DrawDeviceD3D PreRegist -> `emoteplayer.dll`；
- DrawDeviceD3DZ PreRegist -> `DrawDeviceD3D.dll`。

所以“startup 已加载 motion/emote”不是四端行为，也不能由“module 已被 AllRegist 索引”推导。

## 4. indexed、registered 与 published 是三个状态

启动期的状态迁移为：

```text
静态构造：auto-register object 加入三条 head chain
  -> AllRegistLine(0..2)：按 module 建立三条 borrowed-pointer list
  -> xp3filter inner LoadModule：执行其 callback 并在成功末尾提交 registered marker
```

对 motionplayer/emoteplayer/DrawDevice 模块而言，startup 后只有第二步完成：

| 观察面 | startup 后结果 |
|---|---|
| `_internal_plugins` | 存在 |
| 当前端口 source/test-only `HasModule(exact-lowercase-name)` | `true`；非参考镜像 surface |
| `TVPRegisteredPlugins` | 不存在 |
| module callback | 尚未执行 |
| Motion/Emote/DrawDevice script class publication | 尚未由对应 module 完成 |

内部 map 存在不是“已经成功注册”。当前端口的 inline `HasModule` 只是这个状态的 source/test
diagnostic；V216 的四端完整 map xref 证明最终参考镜像没有保留该 function/ABI。registered set
才是 loader 的成功 commit marker；脚本 class/property 又是 callback 执行产生的第三层
observable state。异常或重入可使 script/native 前缀已经发布，但 registered marker 仍缺失，
所以三者不能合并成一个 bool。负向 surface 证据见
`analysis/motionplayer_ncb_hasmodule_deadstrip_source_test_diagnostic_four_binary_2026-08-17.md`。

V212 又确认这两个 NCB 容器都不是 Storage namespace：indexed module 与 committed marker 都
不会让 `Storages.getPlacedPath/isExistentStorage` 合成一个不存在的 `.dll` path。证据见
`analysis/motionplayer_storage_internal_module_visibility_getplacedpath_four_binary_2026-08-17.md`。

V211 随后闭合了这里的 script 入口：`Plugins.link` 把首参转换为 `ttstr` 后直接调用 public
NCB loader，丢弃 bool并保持 result；它不会提取 storage-name 或把 `.tpm` 改成 `.dll`。完整
public surface、unlink no-op 和 getList set snapshot 见
`analysis/motionplayer_plugins_link_unlink_getlist_exact_key_registered_set_four_binary_2026-08-17.md`。

## 5. DrawDeviceD3DZ 的真实 callback

### 5.1 字符串与 static bundle

| 目标 | `DrawDeviceD3D.dll` | `DrawDeviceD3DZ.dll` |
|---|---:|---:|
| Android arm64 | `0x14BE332` | `0x14BEEDA` |
| Android armv7 | `0xD7625E` | `0xD76C7E` |
| iOS arm64 | `0x10196F620` | `0x101970258` |
| iOS armv7 | `0x17619CC` | `0x1762604` |

同一个 DrawDevice translation-unit bundle 除七个 class auto-register 与 main
`DrawDeviceD3D.dll` PreRegist object 外，还构造一个 module name 为
`DrawDeviceD3DZ.dll` 的 PreRegist object。它没有自己的 class-line registrar，但 callback
绝非空函数。

### 5.2 四端共同伪代码

```cpp
void DrawDeviceD3DZ_PreRegist_guess() {
    (void)ncbAutoRegister::LoadModule(L"DrawDeviceD3D.dll");
}
```

这是 public wrapper：它会把名字规范化为小写、检查 registered set、查内部 map、运行三行
callback，最后提交 main module marker。companion 不读取返回 bool，因此：

- main 尚未加载：nested call 注册 main；companion callback 正常返回后，再提交 companion；
- main 已加载：nested call 返回 `false`；companion 仍可首次成功并提交自己的 marker；
- main map miss：nested call 返回 `false`；companion 仍会提交。这是 loader 的普通
  bool-ignore 边界，尽管当前 bundle 保证 main 已被索引；
- main callback 抛异常：异常传播；companion 不提交 marker；main 已完成的 callback 前缀
  不回滚。

因此 `DrawDeviceD3DZ.dll` 是依赖/别名 shim，不是 `DrawDeviceD3D.dll` 的第二套 class surface，
也不是无效果的空 module。

## 6. 完整 lazy dependency 图与生命周期

```text
startup
  -> index all built-in modules
  -> eagerly register xp3filter.dll only

game Plugins.link("motionplayer.dll")
  -> motionplayer

game Plugins.link("emoteplayer.dll")
  -> emoteplayer PreRegist
     -> motionplayer

game Plugins.link("DrawDeviceD3D.dll")
  -> DrawDeviceD3D PreRegist
     -> publish D3DLayerBase ClassInfo prefix
     -> emoteplayer
        -> motionplayer
     -> publish D3DLayerObjectNativeInstance ID

game Plugins.link("DrawDeviceD3DZ.dll")
  -> DrawDeviceD3DZ PreRegist
     -> DrawDeviceD3D
        -> emoteplayer
           -> motionplayer
```

每一层 loader 都只在自己的三行 callback 全部正常返回之后，才把自己的小写 module name
插入 `TVPRegisteredPlugins`。依赖先完成，所以正常从 companion 首次加载后的 marker 提交顺序
为：

```text
motionplayer -> emoteplayer -> DrawDeviceD3D -> DrawDeviceD3DZ
```

这不是引用计数式 module ownership。当前集成 loader 没有 unload、marker erase 或依赖逆序
释放路径；静态 registrar 和 internal-map borrowed pointer 都保持到进程结束。

## 7. 当前源码纠偏

### 7.1 `PluginImpl.cpp`

`TVPLoadInternalPlugins()` 现在只执行：

```cpp
ncbAutoRegister::AllRegist();
ncbAutoRegister::LoadModule(TJS_W("xp3filter.dll"));
```

删除了旧 `libkrkr2.so` 推断留下的 motionplayer/emoteplayer eager load。源码注释明确区分
index 与 registration，并说明后续由 `Plugins.link` 或 dependency callback 触发。

### 7.2 `DrawDeviceD3D.cpp`

`DrawDeviceD3DZ_PreRegist()` 从空函数恢复为加载 `DrawDeviceD3D.dll` 并丢弃 bool。main
PreRegist 的注释同步采用 V208 已闭合的真实边界：

1. `TJSRegisterNativeClass("D3DLayerBase")`；
2. `D3DLayerBaseClassInfo::Set(name, id, nullptr)` first-publish；
3. load emoteplayer，ignore bool；
4. 直接覆盖单 word `D3DLayerObjectNativeInstance` ID。

这里没有 global `D3DLayerBase` script class，也没有 find-or-register lazy fallback。

### 7.3 build metadata comment

`cpp/plugins/CMakeLists.txt` 删除了旧单目标绝对地址，只保留 DrawDevice main/companion
共享 `BinaryAccessor` translation-unit ownership 与 dependency shim 语义。绝对地址继续集中在
本报告。

## 8. 回归测试

`tests/unit-tests/plugins/motionplayer-dll.cpp` 的 fixture 注释现在明确：产品 startup 只 eager
load xp3filter；测试显式加载 motion/emote，是因为 unit TU 不执行游戏的 `Plugins.link` 启动脚本。

新增/改写的 companion 回归覆盖：

1. 读取 main/companion 进入测试前各自的 registered state，兼容无 unload 的进程级 fixture；
2. 首次加载 companion 的结果严格等于 `!companionAlreadyLoaded`；
3. 无论 main 原先是否已加载，返回后 main 与 companion marker 都存在；
4. `D3DEmoteModule`、`D3DEmotePlayer` ClassInfo 与 global class property 均已发布；
5. companion/main 的混合大小写重复 load 都返回 `false`；
6. map-miss module 仍返回 `false` 且不写 marker。

这组测试既证明 companion 的 nested dependency，也没有虚构 module unload 或要求测试执行顺序。

## 9. recovery IDB 写回

四份 recovery IDB 已严格顺序打开、写回、保存、关闭，最终无遗留 session。每库完成：

- 3 个函数语义 rename：startup loader、单 line index helper、companion PreRegist；
- 3 个函数原型应用；
- 3 条 set comment：startup、companion 与已由 V208 纠正的 main PreRegist；
- 3 条 append comment：startup xp3 call、static bundle、companion string/callsite；
- 3 个 bookmark。

四库总计 12 rename、12 type application、12 set comment、12 append comment、12 bookmark。
写回前还分别对五个 module literal 做 UTF-16LE 精确搜索，避免 IDA 默认 string item 遗漏
代码附近的宽 literal。

## 10. 验证与产物差异

- ordinary Web test TU syntax-only：通过；
- `KRKR2_WASMTIME_HEADLESS=1` test TU syntax-only：通过；
- 两者只有既有 `_tss` literal-operator warning；
- `cmake --build out/web/debug`：通过；
- `cmake --build out/wasmtime/debug`：通过；
- 两个产物均 `WebAssembly.validate=true`；
- imports/exports 保持 Web `539/69`、Wasmtime `538/69`；
- 两个 CTest tree 均 exit 0 并明确报告 `No tests were found`；行为回归由两种 syntax-only
  配置完整编译，但没有虚报 runtime execution；
- `git diff --check` exit 0，仅有工作树既有 LF→CRLF warning。

产物：

| 产物 | size | SHA-256 |
|---|---:|---|
| Web `out/web/debug/index.wasm` | 85,660,834 | `45556E3596EAFE0E242357C32594DB78352E8E35797A4784FC5406B7BE0209F2` |
| Wasmtime `out/wasmtime/debug/index.wasm` | 85,008,007 | `B5FB86EF4B73F864830C885600AE781389FD056CACA482B88E2AE1FE59AD3558` |

相对 V209，Web 减少 112 B，Wasmtime 减少 80 B；ABI surface 未变化：

| section | Web V210 | Web delta | Wasmtime V210 | Wasmtime delta |
|---|---:|---:|---:|---:|
| FUNCTION | `0x1BD30` | 0 | `0x1BA4F` | 0 |
| GLOBAL | `0xD5C2` | 0 | `0xD5EA` | 0 |
| CODE | `0x1A42546` | `-0x10` | `0x19EA4F4` | `-0x10` |
| DATA | `0x5A3FB7` | `-0x60` | `0x5A1227` | `-0x40` |
| name | `0x3185EE4` | 0 | `0x3141D7A` | 0 |

差异只落在 CODE/DATA：删除两个错误 startup load，同时恢复 companion dependency callback；
没有新增 import/export、函数索引或 global ABI 变化。

## 11. 未过度推断的部分

- startup/private helper 的原始 C++ 名与 translation-unit 文件名无法从 stripped binary 精确证明；
- 本轮证明 companion 是 dependency alias，但没有把它解释成平台压缩、渲染后端或历史兼容名；
- `Plugins.link` 何时由每个具体游戏脚本调用取决于游戏内容；四端只证明 startup 没有 eager
  load motion/emote，以及 dependency callback 的方向；
- startup inner loader 的 bool 被忽略不等于 xp3filter 一定成功；map miss/已注册仍是正常返回，
  callback 异常仍传播；
- 本纵切面只闭合 module startup 与 dependency 生命周期，不代表 motionplayer 总目标已经完成。
