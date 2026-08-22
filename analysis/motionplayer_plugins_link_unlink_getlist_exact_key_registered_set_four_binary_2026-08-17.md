# Plugins.link/unlink/getList exact-key、registered-set snapshot 与生命周期四端恢复

日期：2026-08-17

## 1. 范围与结论

V210 闭合“startup 只 eager load xp3filter”之后，本轮继续检查紧邻的 script-visible
`Plugins` native class。事实源仍严格限定为 `reference/binaries/` 的 Android arm64-v8a、
Android armeabi-v7a、iOS arm64、iOS armv7 四个当前参考二进制。

本轮恢复：

- `TVPCreateNativeClass_Plugins` 的三项静态 method surface；
- `Plugins.link` 的参数、module-key、返回值与异常边界；
- `Plugins.unlink` 的真实 no-op 行为；
- `Plugins.getList` 的 `std::set<ttstr>` 遍历、数组 ownership 与异常清理；
- 当前端口里由旧调试需求引入、但四端没有的日志与 post-load 尾巴。

四端共同结论：

1. `Plugins` 只注册三个 static method：`link`、`unlink`、`getList`；三者 member flags
   都是 `0x10000`；
2. `link` 要求至少一个参数，把 `param[0]` 转成临时 `ttstr` 后，直接调用 public
   `ncbAutoRegister::LoadModule`，不做 storage-name 提取，也不把 `.tpm` 改为 `.dll`；
3. `link` 丢弃 loader bool、不写 result，因而 module missing、already registered、首次成功
   都向脚本返回 `TJS_S_OK`；只有参数/转换/loader callback 异常可见；
4. `unlink` 同样要求一个参数并保留 `ttstr` 构造/析构，但不查询、不删除 registered set，
   不调用任何 Unregist callback；result 非空时写整数 `1`；
5. `getList` 无参数数量 gate；它总是新建 Array，按 `TVPRegisteredPlugins` 的 comparator
   顺序逐项复制 committed module key，最后把 owning object Variant 写给 result；
6. getList 的 set 是 loader commit marker 容器，所以只含成功 pipeline 的小写名字，不含
   仅被 `AllRegist` 索引但未注册的 module；
7. 四端均不存在当前端口的宽字符串 `"(info) Loading Plugin: "`，public link 的反编译中也
   没有日志、Emscripten console hook、`d3dMotion/d3dMode` mutation 或无消费者的小写转换；
8. 产品 autoload 与 script link 的入口不同，但最终都把完整输入当作 module-map key：
   script-visible `Plugins.link` 直接使用首参；autoload 则枚举 project、`/system`、`/plugin`
   三个目录，把 `Path + "/" + Name` 直接交给 public loader。Android 保留发现时的 `.tpm`
   `Name`，iOS 在发现时把末尾 `.tpm` 改成 `.dll`；两端都不提取 basename/storage-name。

## 2. 四端函数映射

| 目标 | class creator | `link` Process | `unlink` Process | `getList` Process | getList set-root load |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x908570` | `0x9086A0` | `0x908738` | `0x9087DC` | `0x90881C` |
| Android armv7 | `0x6C7D68` | `0x6C7E58` | `0x6C7EB8` | `0x6C7F20` | `0x6C7F44` |
| iOS arm64 | `0x1003F20EC` | `0x1003F2210` | `0x1003F2274` | `0x1003F22E4` | `0x1003F2318` |
| iOS armv7 | `0x3D96E0` | `0x3D9884` | `0x3D9944` | `0x3D9A0C` | `0x3D9A56` |

四份 IDB 使用的恢复名为：

- `TVPCreateNativeClass_Plugins_guess`；
- `Plugins_link_Process_guess`；
- `Plugins_unlink_Process_guess`；
- `Plugins_getList_Process_guess`。

即便它们与当前源码 macro 生成的局部 `Process` 对应，stripped binary 仍不能证明局部 struct
的精确 mangled 名，所以统一保留 `_guess`。

creator 分配的 native-class object 为 LP64 `0xA8` B、ILP32 `0x6C` B。它随后构造 base 并按
`link -> unlink -> getList` 顺序注册三个 `nitMethod`，每项均传 static-member flag
`0x10000`。没有第四个 plugin management method。

## 3. `Plugins.link`

### 3.1 四端共同伪代码

```cpp
tjs_error Plugins_link_Process_guess(
    tTJSVariant *result,
    tjs_int numparams,
    tTJSVariant **param,
    iTJSDispatch2 *objthis) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT; // -1004

    ttstr name(*param[0]);
    (void)ncbAutoRegister::LoadModule(name);
    return TJS_S_OK;
}
```

`result` 与 `objthis` 都未被读取。reference method 不调用一个会写日志的
`TVPLoadPlugin` wrapper，而是直达 V210 已恢复的 public NCB wrapper。public wrapper 本身会
构造 lowercase temporary，所以大小写仍被规范化；这里所谓 exact-key 指 lowercasing 之前的
整个字符串不再做其它结构变化。

### 3.2 exact-key 矩阵

假设内部 map 只有 `probe.dll`：

| script argument | public loader 查找 key | 行为 |
|---|---|---|
| `probe.dll` | `probe.dll` | 命中，首次执行 callback |
| `PROBE.DLL` | `probe.dll` | 同上或 registered-hit false |
| `probe.tpm` | `probe.tpm` | map miss |
| `plugin/probe.dll` | `plugin/probe.dll` | map miss |

后两项不会提取 final component，也不会 rewrite extension。即便 map miss 返回 `false`，
`Plugins.link` 仍丢弃 bool、返回 `TJS_S_OK` 并保持传入 result Variant 原值。

V213 对 `tvpLoadPlugins` 的四端逐指令审计进一步确认：autoload 不存在另一个负责提取
storage-name/basename 的 helper。它把枚举记录的 `Path + "/" + Name` 直接转换成 `ttstr` 后调用
同一个 public loader；Android 的 `Name` 保留 `.tpm`，iOS 的 `Name` 已在 discovery callback
中改写成 `.dll`。因此 script 与 autoload 的差别只在输入由谁构造以及 iOS discovery-time
扩展名改写，public loader 的 exact-full-key 边界一致。完整证据见
`analysis/motionplayer_physical_tpm_autoload_platform_name_rewrite_full_key_four_binary_2026-08-17.md`。

### 3.3 异常与 commit

- 参数不足在构造 `ttstr` 之前返回 `TJS_E_BADPARAMCOUNT`；
- Variant→ttstr 转换抛异常时，不进入 loader；
- loader false 永不变成脚本错误；
- callback 抛异常时继续向外传播，registered marker 不提交，但 callback 前缀不回滚；
- 正常/异常路径都按各平台 EH ABI 销毁已经构造的 name temporary；
- iOS armv7 显式 SjLj landing 位于 `0x3D9918`，调用 ttstr cleanup 后 resume。

因此 `Plugins.link` 是 void-like script surface，不是 loader bool surface。

## 4. `Plugins.unlink`

四端共同伪代码：

```cpp
tjs_error Plugins_unlink_Process_guess(
    tTJSVariant *result,
    tjs_int numparams,
    tTJSVariant **param,
    iTJSDispatch2 *objthis) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    ttstr name(*param[0]);
    bool res = true;
    if(result)
        *result = static_cast<tjs_int>(res);
    return TJS_S_OK;
}
```

临时 `name` 在业务上完全未消费，但四端都仍执行 Variant→ttstr 转换及析构。因此 malformed
conversion 仍可抛异常，不能把整个 method 简化成“无条件写 1”并跳过 conversion。

业务边界为：

- 不从 `TVPRegisteredPlugins` erase；
- 不在 `_internal_plugins` 查 module；
- 不运行 Post/Class/Pre 的 `Unregist`；
- 不撤销 script/native class publication；
- 不递减 dependency ownership；
- result 为 null 时仍构造/析构 name，然后返回 `TJS_S_OK`；
- result 非 null 时写入 `tvtInteger(1)`。

这进一步确认当前内建 plugin 生命周期是 process-lifetime registration；`unlink` 的 success
值不能被解释成实际卸载成功。

## 5. `Plugins.getList`

### 5.1 共同数据流

```cpp
tjs_error Plugins_getList_Process_guess(tTJSVariant *result, ...) {
    iTJSDispatch2 *array = TJSCreateArrayObject();
    try {
        tjs_int index = 0;
        for(const ttstr &name : TVPRegisteredPlugins) {
            tTJSVariant value(name);
            array->PropSetByNum(
                TJS_MEMBERENSURE, index++, &value, array);
        }
        if(result)
            *result = tTJSVariant(array, array);
    } catch(...) {
        array->Release();
        throw;
    }
    array->Release();
    return TJS_S_OK;
}
```

四端 `PropSetByNum` flag 都是 `0x200`。调用状态没有成为 loop gate；异常仍按普通 C++
exception 传播。临时 string Variant 对 set node 中的共享 `tTJSString` 增加引用，数组写入再按
TJS Variant 语义持有自己的引用，随后临时释放。

result 为 null 时也会完整创建、填充、释放数组。它不是“result null 就 early return”的
优化入口。

### 5.2 ordered set 与 ABI 差异

getList 直接遍历 `TVPRegisteredPlugins` 的红黑树，不先复制到 vector，也不按 load-time 另建
顺序。源级语义是 `std::set<ttstr>` comparator order。loader 只插入 lowercase name，因此
返回数组通常也是小写 committed keys。

V212 已进一步证明这个 set 只服务 Plugins loader commit/getList：四端 `TVPGetPlacedPath`
均无该 set 或 internal module map 的 xref，registered name 不会自动成为 storage path。详情见
`analysis/motionplayer_storage_internal_module_visibility_getplacedpath_four_binary_2026-08-17.md`。

node 中 key payload 的四端可见偏移：

| STL/ABI | pointer size | node 内 `ttstr` payload 偏移 | successor |
|---|---:|---:|---|
| Android libstdc++ arm64 | 8 | `+0x20` | `_Rb_tree_increment` helper |
| Android libstdc++ armv7 | 4 | `+0x10` | `_Rb_tree_increment` helper |
| iOS libc++ arm64 | 8 | `+0x1C` | inline leftmost/right-parent traversal |
| iOS libc++ armv7 | 4 | `+0x10` | inline leftmost/right-parent traversal |

iOS arm64 的 packed libc++ node 不能从 Android LP64 的 `+0x20` 机械外推；四端恢复以源码级
set semantics 为共同层，只在报告/IDB 保留 ABI 偏移。

### 5.3 ownership 与异常路径

Array 初始返回一个 owning reference。result 非空时构造 `{Object=array,
ObjectThis=array}` 的 object Variant，经过 copy assignment 后由 result 持有；临时 Variant
析构抵消临时引用，函数尾再释放初始 Array reference。

任一 set-key Variant 构造、array property write 或 result copy assignment 抛异常时，已经写入
Array 的前缀由 Array 自己拥有；catch 释放 Array，Array teardown 再释放前缀元素，然后原异常
传播。iOS armv7 的多 landing SjLj CFG 显式展示 source/self/array 的分阶段清理。

## 6. 当前源码纠偏

`cpp/core/plugin/PluginImpl.cpp` 完成两项行为恢复。

第一，script-visible `Plugins.link` 从：

```text
TVPLoadPlugin
  -> path/storage extraction
  -> .tpm rewrite
  -> NCB loader
  -> port-only logging/debug tail
```

改为直接：

```cpp
ttstr name = *param[0];
(void)ncbAutoRegister::LoadModule(name);
```

第二，本轮先从 autoload wrapper 删除四端不存在的：

- Emscripten `[PLUGIN-LINK]` console hook；
- `(info) Loading Plugin: ... Success/Failed` 日志分支；
- “force d3dMotion/d3dMode” 的过时注释；
- 实际没有消费者、也没有 force 行为的 `AsStdString` + lowercase transform。

这同时删除不再需要的 `spdlog`/`emscripten.h` include。autoload 自己已有
`TVPAddImportantLog("(info) Loading " + filename)`；本轮没有删除未检查的其它启动日志。

V213 随后闭合 autoload 本身并继续纠偏：删除并无参考实现对应物的
`TVPLoadInternalPlugin`，使 `TVPLoadPlugin` 直接调用 public loader；discovery record 保留
`Path`/`Name` 两字段，只在 `__APPLE__` 分支执行 `.tpm -> .dll` 的 Name 改写；加载循环继续
按 Name 排序、记录 count/log，并把完整 `Path + "/" + Name` 传入 loader。这项后续修正不改变
本文关于 script-visible exact-key 的结论。

`Plugins.unlink` 与 `getList` 的当前算法本已与四端一致，只补测试，不进行无证据重构。

## 7. 回归测试

`tests/unit-tests/plugins/motionplayer-dll.cpp` 增加一个只存在于测试 TU 的静态
`plugins-link-probe.dll` registrar；它分别记录 Regist/Unregist 次数，不污染产品 module surface。

新回归通过真实 `TVPCreateNativeClass_Plugins()` dispatch 覆盖：

1. `link` 零参数返回 `TJS_E_BADPARAMCOUNT`，result 保持 37；
2. `plugins-link-probe.tpm` 与 `plugin/plugins-link-probe.dll` 都返回 `TJS_S_OK`，但 callback
   count 仍为 0、marker 不存在；
3. exact `.dll` key 首次调用 callback 一次并提交 marker；
4. uppercase repeat 仍返回脚本 `TJS_S_OK`、不重复 callback、result 保持 37；
5. `unlink` 零参数失败；合法参数写 result=1，但 Unregist count 保持 0、marker 保留；
6. `getList` 返回 Array，count 等于 registered set size，每项与 set comparator 顺序逐项一致。

测试不依赖现有产品 module 的 load 顺序，也不虚构 unload reset；probe 名只有该测试可访问。

## 8. recovery IDB 写回

四份 recovery IDB 均顺序打开、写回、保存、关闭，最终 session 列表为空。每库完成：

- 4 个 function rename；
- 4 个 function type application；
- 4 条 function comment；
- 2 条 loader-call/set-root line comment；
- 4 个 bookmark。

四库总计 16 rename、16 type application、16 set comment、8 append comment、16 bookmark。
写回前还在四库分别对 UTF-16LE `"(info) Loading Plugin: "` 做精确字节搜索，全部零命中。

## 9. 验证与产物差异

- ordinary test TU syntax-only：通过；
- `KRKR2_WASMTIME_HEADLESS=1` test TU syntax-only：通过；
- 两者只有既有 `_tss` literal-operator warning；
- `cmake --build out/web/debug`：通过；
- `cmake --build out/wasmtime/debug`：通过；
- 两个 Wasm 均 `WebAssembly.validate=true`；
- imports/exports 保持 Web `539/69`、Wasmtime `538/69`；
- 两个 CTest tree exit 0，明确报告 `No tests were found`；新增行为回归由两种 test TU
  syntax-only 完整编译，但没有虚报 runtime execution；
- `git diff --check` exit 0，仅有工作树既有 LF→CRLF warning。

产品产物：

| 产物 | size | SHA-256 |
|---|---:|---|
| Web `out/web/debug/index.wasm` | 85,659,809 | `8B96AEBEAEFC869FFDD3709F12ADD56F9530A3707E2B9B94B5268EC9A6CF07AE` |
| Wasmtime `out/wasmtime/debug/index.wasm` | 85,006,982 | `101CEFBF8C68B5CBDD94867D41DE57096DCFBC1D488AE31B1551C8CAD198DD58` |

相对 V210 两份产物都精确减少 1,025 B，section delta 同构：

| section | Web V211 | Web delta | Wasmtime V211 | Wasmtime delta |
|---|---:|---:|---:|---:|
| FUNCTION | `0x1BD30` | 0 | `0x1BA4F` | 0 |
| GLOBAL | `0xD5C2` | 0 | `0xD5EA` | 0 |
| CODE | `0x1A421DC` | `-0x36A` | `0x19EA18A` | `-0x36A` |
| DATA | `0x5A3F20` | `-0x97` | `0x5A1190` | `-0x97` |
| name | `0x3185EE4` | 0 | `0x3141D7A` | 0 |

CODE/DATA 对称减少而 FUNCTION/name/import/export 不变，符合删除日志/debug/post-load inline body、
保留既有 public function surface 的源码变化。

## 10. 未过度推断的部分

- class creator/local `NCM_*::Process` 的私有 C++ 符号已剥离，恢复名继续使用 `_guess`；
- V213 已完成 autoload 的四端逐指令审计：它没有 storage-name/basename normalization；Android
  保留 `.tpm` Name，iOS 在 discovery 阶段改写为 `.dll`，最终均以完整 joined path 作为
  module key。本文对 script link 的 exact-key 结论与该后续闭环一致；
- getList 暴露 comparator order，但本文没有为 `ttstr::operator<` 再建立新的四端纵切面；
- creator construction failure 的 EH 形态随平台编译器不同，本轮只在 IDB 记录明确可见的 landing，
  不把某一端的 landing layout 外推到四端；
- 本纵切面闭合的是 Plugins public surface 与 registered-set snapshot，不代表 motionplayer
  总目标已经完成。
