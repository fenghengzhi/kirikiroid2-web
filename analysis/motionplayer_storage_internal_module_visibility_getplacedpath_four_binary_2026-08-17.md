# 内建 module 与 Storages.getPlacedPath/isExistentStorage 命名空间隔离四端恢复

日期：2026-08-17

## 1. 范围与结论

V210/V211 已分别闭合内建 module 的 startup/indexed/registered 状态与 `Plugins` public
surface。本轮继续检查当前端口里另一条直接影响 motionplayer 加载判断的路径：
`Storages.getPlacedPath`、`Storages.isExistentStorage` 和底层 `TVPGetPlacedPath` 是否把 NCB
internal module 当成“存在的 storage”。

事实源仍只使用 `reference/binaries/` 的 Android arm64-v8a、Android armeabi-v7a、
iOS arm64、iOS armv7 四个当前参考二进制。

四端共同结论：

1. `Storages.getPlacedPath` 把首参转为 `ttstr`；只有 result 非 null 时才调用
   `TVPGetPlacedPath`，并把返回字符串复制给 result；
2. `Storages.isExistentStorage` 也只在 result 非 null 时调用同一个 `TVPGetPlacedPath`，
   销毁返回 temporary 后写入整数 `returnedString != empty`；
3. `TVPGetPlacedPath` 是真实 storage/current-path/autopath/cache/archive resolver；四库完整
   xref 审计均证明它不读取 `TVPRegisteredPlugins`，也不读取 NCB `_internal_plugins`；
4. 因而一个 module 即使已经被 `AllRegist` 索引，或已经成功提交 registered marker，仍不会
   自动成为 `Storages` 可解析的 `.dll` path；
5. indexed/registered module 属于 Plugins namespace；physical/archive storage 属于
   Storages namespace。两者只可由更上层 script policy 组合，native resolver 不合并它们；
6. `CanLoadPlugin` 是游戏/系统 script 层逻辑，不是这四个 binary 中的 native symbol。本轮能
   证明的是它若调用 `Storages.getPlacedPath/isExistentStorage`，native primitive 不会为 NCB
   module 合成 path；
7. 当前端口曾在 `TVPGetPlacedPath` 起始处加入 registered-set/HasModule early return，并有
   “motion.tjs/AffineSourceMotion” 调试注释；该兼容层与四端不符，现已删除；
8. 旧源码注释把 Android arm64 `0x8EE294` 说成 isExistentStorage native，但该地址实际是
   Storages class creator 注册 `chopStorageExt` descriptor 的 callsite，已清除该过时地址。

## 2. 四端函数与字符串映射

### 2.1 native surface

| 目标 | Storages creator | getPlacedPath Process | isExistentStorage Process | `TVPGetPlacedPath` |
|---|---:|---:|---:|---:|
| Android arm64 | `0x8EE030` | `0x8EE59C` | `0x8EE674` | `0x8EBC80` |
| Android armv7 | `0x6B9368` | `0x6B97FC` | `0x6B987C` | `0x6B85DC` |
| iOS arm64 | `0x1001948A4` | `0x100194D34` | `0x100194DC8` | `0x1001937AC` |
| iOS armv7 | `0x194308` | `0x1949AC` | `0x194A94` | `0x1930B0` |

恢复名：

- `TVPCreateNativeClass_Storages_guess`；
- `Storages_getPlacedPath_Process_guess`；
- `Storages_isExistentStorage_Process_guess`；
- `TVPGetPlacedPath_guess`。

Android arm64 的 IDA function boundary 有一项既有问题：`0x8EE59C` 被错误延长到
`0x8EECE4`，把 `0x8EE674` isExistentStorage 和后续多个相邻 Process/EH 区域并进同一函数。
creator 的独立 method pointer、各自 prologue/return/EH tail 明确给出真实入口。为避免破坏性
undefine/recreate 大片代码，本轮给第二入口建立 code-label rename、line comment 与 bookmark，
没有硬拆 IDB bytes。

### 2.2 method-name 宽字符串

| string | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `getPlacedPath` | `0x15106E4` | `0x6B9610` | `0x10195F58E` | `0x17518F2` |
| `isExistentStorage` | `0x1510700` | `0x6B9634` | `0x10195F5AA` | `0x175190E` |

每端另有一个 `isExistentStorage` 宽字符串副本供其它 plugin/surface 使用；本轮逐条检查 xref，
只采用汇入 Storages creator 的上表副本，避免仅凭同文字符串混淆函数归属。

## 3. `Storages.getPlacedPath`

四端共同伪代码：

```cpp
tjs_error Storages_getPlacedPath_Process_guess(
    tTJSVariant *result,
    tjs_int numparams,
    tTJSVariant **param,
    iTJSDispatch2 *objthis) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    ttstr path = *param[0];
    if(result)
        *result = TVPGetPlacedPath(path);
    return TJS_S_OK;
}
```

边界：

- 参数不足在 path conversion 前返回 `-1004`；
- result 为 null 时仍构造并销毁输入 path，但不调用 resolver；
- resolver 返回 empty string 也是 `TJS_S_OK`，empty 本身表示 miss；
- result copy assignment 抛异常时，返回 temporary 与 path 均按平台 EH 清理后传播；
- method 不读取 NCB loader bool、internal map 或 registered set。

## 4. `Storages.isExistentStorage`

四端共同伪代码：

```cpp
tjs_error Storages_isExistentStorage_Process_guess(
    tTJSVariant *result,
    tjs_int numparams,
    tTJSVariant **param,
    iTJSDispatch2 *objthis) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    ttstr path = *param[0];
    if(result) {
        ttstr placed = TVPGetPlacedPath(path);
        bool exists = !placed.IsEmpty();
        // placed is destroyed before integer assignment in all four outputs
        *result = static_cast<tjs_int>(exists);
    }
    return TJS_S_OK;
}
```

四端都不是“尝试 open stream”或“查询 Plugins map”。它复用 resolver 的 placed-path result。
returned string temporary 在 result integer assignment 前销毁；若析构的最后引用触发底层释放，
其时序也不能后移到 method return 之后。

iOS armv7 两个 Process 都有 SjLj cleanup：已经构造 input/placed 时，conversion、resolver 或
result assignment 抛异常会按 call-site state 清理相应 temporary 再 resume。

## 5. NCB 容器的负向 xref 证据

V210/V211 已识别 registered set 与 internal module map。本轮对四端各自容器 header/base 做
完整 data-xref 查询：

| 目标 | registered set base | internal module map base | storage resolver xref |
|---|---:|---:|---|
| Android arm64 | `0x1AB5938` | `0x1AB5968` | 0 |
| Android armv7 | `0x1111BCC` | `0x1111BE4` | 0 |
| iOS arm64 | `0x10256B910` | `0x10256B928` | 0 |
| iOS armv7 | `0x218F190` | `0x218F19C` | 0 |

正向 xref 闭合为：

- registered set：static init/destruction、inner LoadModule lookup/insert、V211 getList traversal；
- internal map：static init/destruction、V210 AllRegist indexing、inner LoadModule lookup；
- 两者均没有 `TVPGetPlacedPath`、getPlacedPath Process 或 isExistentStorage Process xref。

这比在巨大 resolver 伪代码中“没看到某个名字”更强：两个具体全局容器的全部消费者已经列完，
storage 链不在其中。

## 6. 状态矩阵

对于没有真实文件/archive entry 的 `motionplayer.dll` 类内建 module：

| NCB state | internal map | port source/test `HasModule` | registered set | Plugins.getList | Storages.getPlacedPath | Storages.isExistentStorage |
|---|---:|---:|---:|---|---|---:|
| static ctor 后、AllRegist 前 | miss | false | false | 不含 | empty | false |
| AllRegist 已索引 | hit | true | false | 不含 | empty | false |
| callback 成功并提交 marker | hit | true | true | 含 | empty | false |
| callback 前缀后抛异常 | hit | true | false | 不含 | empty | false |

如果同名真实 `.dll` storage 确实存在，最后两列当然可由 filesystem/archive resolver 命中；
关键边界是 NCB state 本身不合成这个命中。

因此上层 script 若希望把 built-in module 视为可加载，应查询/尝试 `Plugins.link`，或采用自己的
policy；不能把参考 native `Storages` primitive 改造成 Plugins namespace alias。

## 7. 当前源码纠偏

`cpp/core/base/StorageIntf.cpp` 删除：

1. `TVPGetPlacedPath` 起始处的 `TVPRegisteredPlugins || ncbAutoRegister::HasModule` early return；
2. 未被调用的 `TVPIsInternalPlugin` helper；
3. helper 内 `.dll NOT found` 调试日志；
4. 因上述逻辑才需要的 `ncbind.hpp` include；
5. 旧 `@0x8EE294` 绝对地址与“synthesize motion_*.tjs”注释。

`TVPIsExistentStorage` 保持“调用 `TVPGetPlacedPath` 并检查 non-empty”的既有算法，只换成四端
语义注释。其它 `TVPStorageTrace`/autoload/cache 行为没有在本纵切面中被顺带删除或重构。

## 8. 回归测试

`tests/unit-tests/plugins/motionplayer-dll.cpp` 新增只存在于测试 TU 的静态
`storage-existence-probe.dll` registrar，以及如下回归：

1. `ScopedCoreScriptEngine` 执行 `AllRegist` 后，以端口 source/test-only inline
   `HasModule(probe)==true` 确认 map hit、registered marker 不存在；该 helper 不冒充四参考 ABI；
2. 物理文件不存在时，`TVPGetPlacedPath(probe)` 为空且 `TVPIsExistentStorage(probe)==false`；
3. 测试手工插入同名 registered marker，再次要求两项结果仍为空/false；
4. RAII guard 在测试退出时 erase 人工 marker，不污染后续 process-lifetime module 测试。

这分别锁住 indexed-only 与 registered 两个当前源码旧 early-return 分支。

## 9. recovery IDB 写回

四份 recovery IDB 均顺序打开、写回、保存并关闭；最终无 session。总计：

- 16 项 semantic rename，其中 A64 isExistentStorage 因既有错误 function merge 使用 code-label
  rename，其余均为 function rename；
- 11 项 function type application；A64 merged second entry 没有伪造独立 function type；
- 15 条成功的 function set comment；
- 9 条 callsite/entry line append comment，包括 A64 第二入口的补充语义；
- 16 个 bookmark。

所有 stripped/private 名继续使用 `_guess`。没有为了追求整齐而破坏性 undefine A64 的大片
相邻代码。

## 10. 验证与产物差异

- ordinary/headless test TU syntax-only：通过；
- 只有既有 `_tss` literal-operator warning；
- Web Debug 与 Wasmtime Headless Debug 完整构建：通过；
- 两个 Wasm 均 `WebAssembly.validate=true`；
- imports/exports 保持 Web `539/69`、Wasmtime `538/69`；
- 两个 CTest tree exit 0 并明确报告 `No tests were found`；新增行为回归由两种 test TU
  syntax-only 完整编译，没有虚报 runtime execution；
- `git diff --check` exit 0，仅有工作树既有 LF→CRLF warning。

产品产物：

| 产物 | size | SHA-256 |
|---|---:|---|
| Web `out/web/debug/index.wasm` | 85,659,180 | `0ECB52A481A4F6A5F0DE8DD60DABE0040C14A95FDA6BE8F684079432494273A1` |
| Wasmtime `out/wasmtime/debug/index.wasm` | 85,006,353 | `AC1F180E8F1C542301D875917B3018033ED651FC27F1A7AE96FE933A78D911E2` |

相对 V211，两份产物都精确减少 629 B：

| section | Web V212 | Web delta | Wasmtime V212 | Wasmtime delta |
|---|---:|---:|---:|---:|
| FUNCTION | `0x1BD2F` | `-1` | `0x1BA4E` | `-1` |
| GLOBAL | `0xD5C2` | 0 | `0xD5EA` | 0 |
| CODE | `0x1A41FBE` | `-0x21E` | `0x19E9F6C` | `-0x21E` |
| DATA | `0x5A3F00` | `-0x20` | `0x5A1170` | `-0x20` |
| name | `0x3185EAE` | `-0x36` | `0x3141D44` | `-0x36` |

两配置 section delta 完全同构，符合删除 port-only helper/early branch/log literal 而不改变
import/export/global ABI surface。

## 11. 未过度推断的部分

- `CanLoadPlugin` 的具体 script 实现不在四参考 binary 内；本文只恢复它可能调用的 native
  storage primitives，不能替具体游戏脚本声明 policy；
- 本轮没有完整恢复 4 KB 级 `TVPGetPlacedPath` 的每条 autopath/cache/archive 分支，只闭合
  module-container 的无 xref边界与两个 public method 的调用/ownership；
- A64 IDB 的相邻 Process merge 已保守标记但未破坏性重建 function ranges；
- 删除 module-as-storage 兼容层可能暴露依赖该端口扩展的游戏脚本，这属于参考忠实度与额外
  compatibility 的真实差异；若以后需要兼容，应放在显式、可选的 script policy 层，而不是
  冒充四端 native behavior；
- 本纵切面不代表 motionplayer 总目标完成。
