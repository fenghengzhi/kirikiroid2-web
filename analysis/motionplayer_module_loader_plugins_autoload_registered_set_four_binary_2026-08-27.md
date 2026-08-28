# 模块加载、`Plugins`、自动加载与已注册集合四端审计

## 结论

`MP-A03` 已完成四端静态闭合。本切面把模块名从静态 NCB registrar 链开始，经过
内部索引、`LoadModule`、`Plugins.link/unlink/getList`、启动自动扫描和进程退出析构的
完整状态链对齐到本地实现。四个参考二进制在可见语义上相同，平台差异只落在
ABI、标准库实现、iOS 的 `.tpm` → `.dll` 名字改写，以及最终链接器是否保留未调用的
autoload-count getter。

本地 `cpp/core/plugin/ncbind.cpp`、`cpp/core/plugin/ncbind.hpp`、
`cpp/core/plugin/PluginImpl.cpp` 和 `cpp/core/plugin/PluginIntf.cpp` 已匹配参考行为，本任务
没有发现需要修改的 C++ 语义。四个 IDB 已补充确定性名字、状态边界注释和书签并保存。

本报告只闭合模块加载机制与其容器/脚本表面，不替代 `motionplayer` 各模块 registrar
内容、callback body 或正式 Web/单元测试验证；后者继续由 `MP-A32`、各功能切面和
`MP-V01..V16` 独立跟踪。

## 四端函数矩阵

下表中的指令数均来自本轮原生 IDA 完整反汇编；所有相关 decompile 成功，完整反汇编
cursor 均到达 `done=true`。

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `ncbAutoRegister::LoadModule` 外层 | `0x548E24` / 34 | `0x4A9648` / 28 | `0x100287B38` / 16 | `0x28A8A4` / 46 |
| `LoadModule` 降小写后的状态机 | `0x701DE8` / 99 | `0x5BA8E8` / 69 | `0x10029FDE4` / 55 | `0x2A48FC` / 103 |
| `AllRegist(line)` | `0x548EAC` / 56 | `0x4A96A0` / 51 | `0x100287B8C` / 52 | `0x28A950` / 97 |
| `TVPLoadInternalPlugins` | `0x548D04` / 71 | `0x4A9598` / 31 | `0x100287ACC` / 21 | `0x28A7DC` / 55 |
| `tvpLoadPlugins` | `0x907618` / 740 | `0x6C77BC` / 153 | `0x1003F1CD4` / 139 | `0x3D923C` / 326 |
| `TVPCreateNativeClass_Plugins` | `0x908570` / 70 | `0x6C7D68` / 72 | `0x1003F20EC` / 65 | `0x3D96E0` / 144 |
| `Plugins.link` | `0x9086A0` / 38 | `0x6C7E58` / 29 | `0x1003F2210` / 20 | `0x3D9884` / 64 |
| `Plugins.unlink` | `0x908738` / 41 | `0x6C7EB8` / 33 | `0x1003F2274` / 23 | `0x3D9944` / 67 |
| `Plugins.getList` | `0x9087DC` / 288 | `0x6C7F20` / 110 | `0x1003F22E4` / 99 | `0x3D9A0C` / 207 |
| directory callback | merged block `0x9089B0` | `0x6C8088` / 51 | `0x1003F2538` / 114 | `0x3D9C60` / 216 |
| NCB 容器 initializer | `0x42F408` / 29 | `0x3018E0` / 31 | `0x1002A03DC` / 24 | `0x2A4DD8` / 29 |

Android arm64 的 `Plugins.getList`、异常 landing pads 和 directory callback 被 IDA 合并
为一个函数范围；`getList` 的逻辑入口与 scanner block 已分别注释，没有通过破坏性
undefine/resize 强行拆分。

## 1. 模块名规范化

`AllRegist(line)` 和 `LoadModule` 都先复制 `ttstr`，再原地执行同一个 ASCII-only
降小写逻辑：只把 UTF-16 code unit `A..Z` 加 32，其他 code unit 原样保留。四端对应
helper 的完整指令数为 33/17/17/17；其循环和比较边界完全一致。

因此精确规则是：

- module key 大小写不敏感的范围仅为 ASCII `A..Z`；
- `/`、路径前缀、扩展名和非 ASCII 字符不会被删除或规范化；
- 不提取 basename，不折叠 `.`/`..`，也不做 filesystem canonicalization；
- NUL 是循环终点，内部 key 是完整 lowercased `ttstr`。

本地 `tTJSString::AsLowerCase/ToLowerCase` 也是同一个 ASCII 范围实现，所以
`ncbind.cpp:18` 与 `ncbind.hpp:2133` 的 key 规范化匹配。

## 2. `AllRegist`：静态链到内部 map

每个 registrar line 都有一条进程期 head-insert 单链。`AllRegist(line)` 的共同流程是：

1. 从该 line 的静态 head 开始，沿 `_next` 走到 null；
2. 复制 registrar 的 module name 并 ASCII-lowercase；
3. 对 `_internal_plugins[name]` 执行查找/默认插入；
4. 把 registrar 原始指针追加到该 module 的 `lists[line]` 尾部；
5. 继续下一个静态 registrar。

这里没有 once flag、clear、set-membership 检查或 pointer dedupe。重复调用
`TVPLoadInternalPlugins` 会重新扫描三条链，并把相同的 borrowed registrar 指针再次
追加。若字符串转换或分配抛异常，已经追加的前缀保留；下次调用会从静态链头重走。

三个 line 的调用顺序固定为 `0, 1, 2`，对应 Pre/Class/Post。不同 ABI 的 node/list
宽度不同，但没有语义差异。

## 3. `LoadModule` 状态机与提交点

四端共同伪代码为：

```text
name = ASCII-lowercase(copy(input))
if name in TVPRegisteredPlugins:
    return false
it = _internal_plugins.find(name)
if it == end:
    return false
for line in [Pre, Class, Post]:
    for registrar in it->lists[line]:
        registrar->Regist()
TVPRegisteredPlugins.insert(name)
return true
```

关键边界：

- 已注册和不存在的模块都返回 `false`，两者对调用者没有不同错误码；
- registered-set 的插入严格位于所有 callback 成功返回之后；
- callback 抛异常时不插入 set，但先前 callback 的外部副作用不回滚；
- 因为没有 commit，重试会再次执行该 module 的完整 list，包括已经成功过的 callback；
- 若 `AllRegist` 曾重复追加而 module 仍未 commit，同一个 registrar 可在一次加载中被
  调用多次；若 module 已 commit，最前面的 set gate 阻止访问重复 map 内容；
- 无锁、无 reentrancy marker。callback 在当前 key commit 前递归加载同一个 module 时，
  仍会重新进入该 module。

map 的完整 xref 分母在四端都只有：global initializer、`AllRegist` 和 inner
`LoadModule`。最终二进制没有第三个 `HasModule` runtime consumer；本地
`ncbAutoRegister::HasModule` 是 source/test diagnostic，不是参考 ABI 或脚本表面。

## 4. `TVPLoadInternalPlugins`

四端都先对 line 0、1、2 执行 `AllRegist(line)`，随后只构造并加载
`xp3filter.dll`。本轮以 UTF-16LE 原始 pattern 搜索并跟随 xref 回到这四个根，普通
string-cache 对宽字符串的 negative 结果没有被当成“不存在”。

这里没有 startup once guard。`motionplayer.dll`、`emoteplayer.dll`、
`DrawDeviceD3D.dll` 和 `DrawDeviceD3DZ.dll` 仅进入 internal map，等待依赖 callback 或
`Plugins.link` 后续加载；`TVPLoadInternalPlugins` 不会逐个 eager-load 它们。

## 5. `Plugins` 脚本表面

`TVPCreateNativeClass_Plugins` 在四端都注册恰好三个 static method，顺序为：

```text
link
unlink
getList
```

名字通过 UTF-16LE 原始 pattern 和 creator xref 确认；Hex-Rays 在若干目标把未类型化
宽字符串错误显示成首字符，不影响原始 bytes 和 registration operation。

### `link`

- `numparams < 1` 返回 `TJS_E_BADPARAMCOUNT`（`-1004`）；
- 只转换 `param[0]` 为 `ttstr`，surplus 参数不读取；
- 把完整字符串直接交给 `ncbAutoRegister::LoadModule`；
- 忽略 loader 的 `bool`；
- 无论模块已加载还是不存在，只要参数转换/加载 callback 没抛异常，就返回
  `TJS_S_OK`；没有 result variant publication。

因此 `Plugins.link("Dir/MotionPlayer.DLL")` 查找的是
`dir/motionplayer.dll` 这个完整 key，不是 `motionplayer.dll`。

### `unlink`

- 同样要求至少一个参数并只转换 `param[0]`；
- 不调用 `AllUnregist`，不遍历 callback，不从 set/map 擦除，也不检查名字；
- 若 result 非空，写入整数/布尔真；
- 返回 `TJS_S_OK`。

本地经 `TVPUnloadPlugin(name)` 返回 `true` 的额外函数层在优化后被内联/消除，最终
可见语义与四端一致。

### `getList`

- 新建脚本 Array；
- 只遍历 `TVPRegisteredPlugins`，不遍历 `_internal_plugins`；
- 按 `std::set<ttstr>` 的 key 顺序依次 `PropSetByNum`；
- 元素是已经 lowercased 的完整 key；
- 构造/赋值失败时释放 Array 并重新抛出；成功时发布 Array variant 后再释放本地 ref。

所以“已索引但尚未加载”的 module 不出现在列表中，callback 抛异常且未 commit 的
module 也不出现。

## 6. 自动扫描与加载链

`tvpLoadPlugins` 的共同顺序是：

1. 调用 `TVPLoadInternalPlugins`；
2. 从 project-dir 派生 executable folder；
3. 依次扫描 folder、`folder/system`、`folder/plugin`；
4. 对 regular-file entry 检查最后四个 bytes 是否与 `.tpm` case-insensitive 相等；
5. 生成 `{Path, Name}` record；
6. 只按 `Name` 的 bytewise `std::string` lexical order 排序；
7. 把完整发现数写入 signed 32-bit global；
8. 对每个 item 先记录 `(info) Loading ` + `Name`；
9. 构造 `Path + "/" + Name` 并把完整字符串交给 `LoadModule`。

排序比较器的字段偏移在四端都指向 record 的第二个 string，即 `Name`，不比较
`Path`。Android 的 libstdc++ introsort/insertion-sort helper 内联了 `Name` 的
length+`memcmp`；iOS 的独立 comparator 读取 record 的第二个 libc++ string（LP64
偏移 24，ILP32 偏移 12）并做相同 lexical less-than。

### 平台名字差异

- Android：发现 `Foo.TPM` 时 `Name` 保留原始拼写和 `.tpm/.TPM` 后缀；
- iOS：scanner materialize record 时先去掉最后四 bytes，再追加精确 `.dll`；
- 两端都保留 `Path`，加载时都不提取 basename。

因此自动扫描最后传给内部 loader 的通常是带目录的完整 key。internal map 的 key 是
registrar 声明的 module name；如果没有同样的路径前缀，loader 返回 `false`，而
autoload loop 不暴露该 `false`。这不是本地端口自行增加的 fallback。

### 边界与异常

- extension 检查前没有 `filename.length() >= 4` gate；参考和本地都直接形成
  `end - 4` 指针。这是需要保留/隔离的输入前置条件，而不是安全化后仍可宣称 1:1；
- extension comparison 是 `strcasecmp`，不是 module-key 的 UTF-16 ASCII-lower loop；
- scan 或 sort 在 count assignment 前抛异常时，旧的 count snapshot 保留；
- 成功发现空列表会显式发布 `0`；
- count 在 logging/load loop 之前发布，所以后续任一日志、字符串构造或 callback
  抛异常时，新发现数仍可见；它是 discovered count，不是 successfully-loaded count；
- 函数入口不先把 count 清零。

Android 两端保留了一个无副作用的 raw getter，分别只有 3/4 条指令；其 count-global
xref 除 autoload 写和 getter 读外为空。iOS 两个最终镜像只保留 autoload 写，getter
因未调用被 dead-strip。

## 7. 两个全局容器的生命周期

四个 static initializer 都先构造 `TVPRegisteredPlugins`，注册其 destructor；再构造
`_internal_plugins`，注册其 destructor。`__cxa_atexit` 按逆序调用，所以进程退出时：

1. 先析构 `_internal_plugins`；
2. 再析构 `TVPRegisteredPlugins`。

递归 tree teardown 的四端反编译显示：

- set 节点释放 lowercased `ttstr` key，再删除 node；
- map 节点析构 key 和三条 list 的 node/storage，再删除 map node；
- list 中保存的是 borrowed registrar pointer，不删除 registrar；
- 两个容器析构都不调用 `Regist/Unregist`，也不走 `AllUnregist`。

静态 registrar 自身是进程期永久 head-chain 节点；本轮没有发现 registrar destructor
注册或 shutdown unlink。

## 8. 完整 xref 分母

| global | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `TVPRegisteredPlugins` | `0x1AB5938` | `0x1111BCC` | `0x10256B910` | `0x218F190` |
| `_internal_plugins` | `0x1AB5968` | `0x1111BE4` | `0x10256B928` | `0x218F19C` |
| autoload count | `0x1AF12C0` | `0x1142A60` | `0x101B97428` | `0x18A62C8` |

registered set 的 direct runtime consumer 是 inner `LoadModule` 与 `Plugins.getList`；
其余 xref 是 initializer/destructor 或 data reference。internal map 的 direct runtime
consumer 只有 `AllRegist` 与 inner `LoadModule`。autoload count 的 iOS xref 只有写，
Android 另有 getter 读。

## 9. 本地逐项对照

| 参考要求 | 本地实现 | 结果 |
|---|---|---|
| 完整输入 ASCII-lowercase | `ncbind.cpp:18` | 匹配 |
| registered gate → map lookup → Pre/Class/Post → set commit | `ncbind.cpp:22-34` | 匹配 |
| `AllRegist` append-only borrowed pointers | `ncbind.hpp:2133-2150` | 匹配 |
| internal startup 只 eager-load `xp3filter.dll` | `PluginImpl.cpp:99-108` | 匹配 |
| 三目录扫描、`.tpm` case-insensitive、无短名 guard | `PluginImpl.cpp:78-96` | 匹配 |
| iOS record name 改 `.dll`，Android 保留 | `PluginImpl.cpp:86-93` | 匹配 |
| Name-only sort | `PluginImpl.cpp:61-66, 130-132` | 匹配 |
| count 在 load loop 前发布 | `PluginImpl.cpp:135-141` | 匹配 |
| 完整 `Path/Name` 传 loader | `PluginImpl.cpp:139-141` | 匹配 |
| `Plugins.link` 忽略 loader bool | `PluginImpl.cpp:307-322` | 匹配 |
| `Plugins.unlink` 仅报告 true | `PluginImpl.cpp:49-56, 324-340` | 匹配 |
| `getList` 只枚举 registered set | `PluginImpl.cpp:342-361` | 匹配 |

没有执行 semantic C++ edit。新增内容仅为本报告、coverage/task 映射，以及四个 IDB 的
确定性命名/注释/书签。

## 10. Disposition

| 观察差异 | disposition |
|---|---|
| LP64/ILP32 container/node/string 宽度 | ABI 差异，不改语义 |
| Android libstdc++ 与 iOS libc++ sort/tree 展开 | 标准库实现差异 |
| iOS `.tpm` → `.dll`，Android 保留 `.tpm` | 必须保留的平台行为 |
| Android count getter 存在、iOS dead-strip | 最终链接可达性差异 |
| Android arm64 `getList` 与 scanner block 被 IDA 合并 | IDB function-boundary disposition，不是源级合并 |
| `Plugins.unlink` 的 helper 层被优化掉 | inline/constant-fold，脚本语义相同 |

`MP-A03` 的 task-local 静态缺口为零；正式构建、单元测试和浏览器运行仍属于独立
verification tickets。
