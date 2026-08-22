# Motionplayer ResourceManager 模块 map 生命周期四参考审计（2026-08-14）

## 范围与证据版本

本轮闭合 `ResourceManager::_loadedModules`、mapped `LoadedResourceRecord`、其内层
Win texture map 与 KRKR atlas map 的源码结构、节点 ABI、发布时点、引用计数、异常回滚、
erase/clear/destructor 行为。所有结论都重新来自 `reference/binaries/` 的四个当前参考目标，
不继承旧 `libkrkr2.so` 注释。

| 代号 | 参考文件 | SHA-256 |
| --- | --- | --- |
| A64 | Android arm64-v8a | `05E2FF4C77F1561608AD7703153D2FB09855BF223237A85DC2267FFF1388564F` |
| A32 | Android armeabi-v7a | `A15C238EC6F21C17D0889B064AE1AD47EC85B4F1530A3611F206B7190FF456AF` |
| I64/I32 | iOS fat binary 的两个 slice | `733BA5D3FD0798E41DDBAC0F0A5B484E7CD20443EE5313781E0E32D1633E18E3` |

恢复 IDB 为 `sla_a64_recovery`、`sla_a32_recovery`、`sla_i64_recovery`、
`sla_i32_recovery`。

## 最重要的旧结论纠正

源码没有对 outer map 或两个 nested map 显式调用 `rehash(10)`。

- A64/A32 使用旧 libstdc++：默认构造的 hashtable policy 自身携带 10 的初始策略，并在
  构造期物化 bucket allocation；机器码里的常数 10 属于 STL 默认构造实现。
- I64/I32 使用 libc++：默认构造只清零 bucket/list/size header，并设置默认 load factor，
  bucket allocation 延迟到第一次插入。
- 因而四份共同源码只能还原成普通 `std::unordered_map` default construction。把 Android
  展开的 10 抄成插件源码 `rehash(10)` 会使当前 Web STL 在构造期产生参考 iOS 没有的
  allocation/throw 行为，也会错误改变空 map 的 bucket 状态。

本轮已从 `ResourceManager` 两个 constructor 和 `LoadedResourceRecord` constructor 移除
三个显式 `rehash(10)`。原审计文档中“两个内层 map 各请求 10”也已改成上述 ABI 分歧。

## ResourceManager 函数与 outer map 位置

| 语义 | A64 | A32 | I64 | I32 |
| --- | --- | --- | --- | --- |
| constructor | `0x6A5CAC` | `0x57B1EC` | `0x100101158` | `0xFE254` |
| destructor | `0x6A5F74` | `0x57B2E4` | `0x10010126C` | `0xFE3B4` |
| `unloadAll` | `0x6A60D8` | `0x57B32C` | `0x1001012CC` | `0xFE3FE` |
| `load` | `0x6A616C` | `0x57B338` | `0x1001012D8` | `0xFE40C` |
| `unload` | `0x6A697C` | `0x57B6F8` | `0x100101A28` | `0xFEC04` |
| `_loadedModules` object offset | `+0x58` | `+0x34` | `+0x60` | `+0x38` |

A64 IDB 原先错误地把 destructor、异常尾和 `unloadAll` 合成
`0x6A5F74..0x6A616C`。逐指令边界显示 destructor 正常 `RET` 在 `0x6A60C8`，
`0x6A60D0/0x6A60D4` 是异常尾，而 `0x6A60D8` 是新的标准序言。本轮已重建为
`ResourceManager_dtor_guess: 0x6A5F74..0x6A60D8` 与
`ResourceManager_unloadAll_guess: 0x6A60D8..0x6A616C`。

## outer mapped record

四端共同源码成员顺序为：

```cpp
PSB::PSBFile file;
unordered_map<ttstr, WinSourceTextureEntry> winSourceTextures;
unordered_map<ttstr, PackedSourceAtlasEntry> krkrSourceEntries;
```

| ABI | record size | `file` | Win map | KRKR map |
| --- | ---: | --- | --- | --- |
| A64 | `0x78` / 120 | `+0x00`, 8B | `+0x08`, 56B | `+0x40`, 56B |
| A32 | `0x3C` / 60 | `+0x00`, 4B | `+0x04`, 28B | `+0x20`, 28B |
| I64 | `0x58` / 88 | `+0x00`, 8B | `+0x08`, 40B | `+0x30`, 40B |
| I32 | `0x2C` / 44 | `+0x00`, 4B | `+0x04`, 20B | `+0x18`, 20B |

默认构造顺序是 `file -> Win map -> KRKR map`；普通 C++ 逆序析构为
`KRKR map -> Win map -> file`。所以 record 移除会先释放全部 atlas textures，再释放
Win textures，最后释放 PSB owner。当前 `LoadedResourceRecord` 的声明顺序保持这一点。

## outer unordered_map 节点 ABI

| ABI | node size | 字段顺序/偏移 |
| --- | ---: | --- |
| A64 | `0x90` | next `+0x00`; key `+0x08`; record `+0x10`; cached hash `+0x88` |
| A32 | `0x48` | next `+0x00`; key `+0x04`; record `+0x08`; cached hash `+0x44` |
| I64 | `0x70` | next `+0x00`; cached hash `+0x08`; key `+0x10`; record `+0x18` |
| I32 | `0x38` | next `+0x00`; cached hash `+0x04`; key `+0x08`; record `+0x0C` |

这不是一个跨平台可共享的手写 node：Android libstdc++ 把 cached hash 放在 node 尾，
iOS libc++ 把它放在 next 后。恢复源码只表达标准容器与 mapped value；上述 node 类型仅写入
各 recovery IDB，供反编译和内存布局审计。

### outer `operator[]` 映射

| 语义 | A64 | A32 | I64 | I32 |
| --- | --- | --- | --- | --- |
| outer `operator[]` | `0x6E8DC4` | `0x5A7488` | `0x100101798` | `0xFE940` |
| candidate ctor | `0x6E8EEC` | `0x5A751C` | inline | inline |
| insert candidate | `0x6E8F98` | `0x5A7588` | inline | inline |
| mapped ctor | `0x6E90DC` | `0x5A762C` | inline | inline |
| unwind landing | split helpers | split helpers | `0x1001019F0` | `0xFEBBA` |
| rehash policy/core | `0x6E9218` | `0x5A7794` / `0x5A7808` | `0x100139CDC` / `0x100139DBC` | `0x139E38` / `0x139EDC` |
| erase-node helper | `0x6E930C` | STL helper chain | inline/STL helper | inline/STL helper |

共同的 miss 流程是：分配 candidate node，保留 key，default-construct 完整 record，按
load-factor policy 判断是否 grow buckets，最后才把 node 接进 bucket/list 并增加 size。
candidate construction 或 rehash 抛出时：

1. 已构造的 KRKR map 被销毁；
2. Win map 被销毁；
3. PSB holder 被释放；
4. key holder 被释放；
5. node allocation 被 delete；
6. map 的逻辑内容保持不变。

rehash 自身也先准备新 bucket state，再迁移/提交；异常时恢复 policy 并保留旧 buckets。
所以 outer `operator[]` 的“插入新空 record”具有强回滚，而它返回之后 caller 对 mapped
record 的后续赋值不再具有同样的事务性。

## `load` 的数据流与发布边界

四端共同流程为：

1. 用 `TVPGetPlacedPath` 替换输入 path；这个标准化结果同时用于 lookup 和 insert。
2. hit：复制 `record.file` 到局部 holder；不会运行 `operator[]`，不会覆盖现有 record。
3. miss：检查 storage，加载 PSB，严格验证根 `id == "motion"`、`spec`、version 等字段。
4. 将新 holder 转移到局部 `selected`。
5. 执行 `_loadedModules[path]`。只有该步成功后新空 record 才可见。
6. 对 `record.file` 赋值：释放旧 owner（新 record 时为 null），写新 owner，再 AddRef。
7. 从 `selected` 新建 root dispatch 并返回 TJS Variant。

这给出三个必须保留的边界：

- outer allocation/rehash 抛出：candidate 完全回滚，map 中没有 path。
- `operator[]` 成功而后续 file assignment/dispatch construction 抛出：已经发布的 record
  不会由 loader 回滚。特别是 fresh dispatch construction 失败时，带 file 的 cache entry
  仍然保留。
- 正常 hit 从来不替换旧 record；源码里的赋值 helper 虽然能表达 replacement，但 loader
  的正常控制流先 hit-return-to-common-tail，不能据此声称 load 会覆盖同 key module。

## Win nested texture map

### 函数与 node

| ABI | `operator[]` | node size | 字段顺序/偏移 |
| --- | --- | ---: | --- |
| A64 | `0x6DF530` | `0x20` | next `+0`; key `+8`; texture `+0x10`; hash `+0x18` |
| A32 | `0x5A03FC` | `0x10` | next `+0`; key `+4`; texture `+8`; hash `+0x0C` |
| I64 | `0x1000F3E38` | `0x20` | next `+0`; hash `+8`; key `+0x10`; texture `+0x18` |
| I32 | `0xF094C` | `0x10` | next `+0`; hash `+4`; key `+8`; texture `+0x0C` |

mapped value 仅为一个 retained raw texture pointer。miss 时 `operator[]` 先发布 mapped null，
caller 再执行 texture replacement。四端在 caller 中的顺序完全一致：

1. 创建 texture，释放 BGRA；
2. nested `operator[]`（必要时发布 null node）；
3. 若 old 与 new 不同：`Release(old)`；
4. store new；
5. `AddRef(new)`；
6. `Release(new)` 的 construction reference；
7. 把 map 中保留的 pointer 写入 source result。

相同 pointer 会跳过 Release/AddRef。这里不是 AddRef-new 后 Release-old 的强保证写法；
当前 `WinSourceTextureEntry::setTexture` 已恢复成 Release-old、store、AddRef-new 的原顺序。
而源码由 `try_emplace` 改回 `map[key]`，显式表达 miss 先发布 null mapped value。

如果 nested allocation/rehash 在步骤 2 抛出，node 自身回滚。页面 texture 此时仍只有
construction reference。后续四端 landing-pad/SjLj 审计已确认 caller 没有把该 raw local
包成 RAII owner，也没有在异常路径补做 texture `Release`；因此这里确实泄漏 construction
reference。完整 call-site、槽位和 ABI 矩阵见
`motionplayer_resource_texture_construction_exception_four_binary_2026-08-15.md`。

## KRKR nested atlas map

### mapped descriptor

共同源码字段为 retained texture、两个 int origin、四个 int atlas rect、四个 double clip。

| ABI | mapped size | texture/origin | rect | clip |
| --- | ---: | --- | --- | --- |
| A64 | `0x40` | `+0x00/+0x08/+0x0C` | `+0x10` | `+0x20` |
| A32 | `0x40` | `+0x00/+0x04/+0x08` | `+0x0C` | `+0x20`（`+0x1C` padding） |
| I64 | `0x40` | `+0x00/+0x08/+0x0C` | `+0x10` | `+0x20` |
| I32 | `0x3C` | `+0x00/+0x04/+0x08` | `+0x0C` | `+0x1C`（double align 4） |

### 函数与 node

| ABI | `operator[]` | node size | 字段顺序/偏移 |
| --- | --- | ---: | --- |
| A64 | `0x6DF954` | `0x58` | next `+0`; key `+8`; mapped `+0x10`; hash `+0x50` |
| A32 | `0x5A0748` | `0x58` | next `+0`; pad `+4`; key `+8`; pad `+0x0C`; mapped `+0x10`; hash `+0x50`; tail pad `+0x54` |
| I64 | `0x1000F5598` | `0x58` | next `+0`; hash `+8`; key `+0x10`; mapped `+0x18` |
| I32 | `0xF1FC0` | `0x48` | next `+0`; hash `+4`; key `+8`; mapped `+0x0C` |

A32 `0x5A0748` 的机器码直接分配 `0x58`，key 写 `node+8`，对 `node+0x10`
起始的 `0x40` 字节调用 `memclr`；插入 helper 在 `node+0x50` 写 cached hash。因而两个
pair/mapped 对齐洞和尾 padding 都是实证，不能把 node 误画成紧凑的 80/84 字节对象。

### 原位发布顺序与异常可见状态

旧本地实现先在栈上构造完整 descriptor，最后 `insert_or_assign`；这给出了参考二进制
不存在的事务性。四端 caller 都先执行 `krkrSourceEntries[key]`，取得持久 mapped 地址后按
下列顺序原位写：

1. miss 时整个 mapped descriptor value-initialize 为全零并发布；hit 时直接取得旧 entry。
2. 若 texture 不同：Release old，store new，AddRef new。
3. 读取并立即写 `originX`。
4. 读取并立即写 `originY`。
5. 依次写 atlas `x, y, right, bottom` 四个 int。
6. 查找 clip：存在时按 `left, top, right, bottom` 逐 getter、逐 double slot 写；不存在时
   明确写 `{0, 0, 1, 1}`。
7. 有 BGRA 时调用 texture `Update`，随后释放 BGRA。
8. 一页结束后释放 atlas texture 的 construction reference。

因此：

- 新 entry 的默认 clip 也是全零，不是 `{0,0,1,1}`；后一默认值只有走到 no-clip 分支才写。
- 任一 metadata getter/conversion 抛出，新 entry 仍存在，已有 entry 也已被部分覆盖。
- texture replacement、origin、rect 与 clip 都不是整体事务；clip 自身也会保留已成功写入的
  prefix。用一个四 getter initializer-list 再整体赋值仍然过强。
- `operator[]` 内部 allocation/rehash 失败仍然回滚 candidate node；非事务性从它成功返回
  后开始。

当前源码已改为持久 mapped reference、全零 default descriptor、Release/store/AddRef
texture 顺序以及 clip 四槽逐次赋值，保留上述部分状态。

## erase、clear 与 destructor

`unload(path)` 标准化 path 后查找并 erase 一个 outer node。erase：

- 从 bucket/list 解链并减 size；
- 逆序销毁 mapped record，再销毁 key/node；
- 不缩容，也不释放 bucket allocation。

`unloadAll()` 调用 clear：遍历销毁全部 node，把 bucket heads/list head/size 复位；同样保留
buckets。`ResourceManager::~ResourceManager()` 在 derived destructor body 中再次 clear，
然后自动 member destructor 对已空 node range 无事可做，最后才释放 bucket storage。这个
“body clear + automatic map destruction”不是双重释放；它决定 textures/PSB owner 在后续
derived members 与 `SourceCache` base teardown 前已经释放。

## 源码恢复内容

- `LoadedResourceRecord` 和 `_loadedModules` 恢复普通 default construction，移除显式
  `rehash(10)`。
- Win nested publish 恢复 `operator[]`，纹理 owner replacement 恢复
  Release-old -> store-new -> AddRef-new。
- KRKR nested publish 从栈上完整 descriptor + `insert_or_assign` 改为 `operator[]` 后原位写。
- KRKR 新 descriptor clip 初始化从 `{0,0,1,1}` 改成全零；no-clip 分支显式写默认 clip。
- explicit clip 改成四个逐 getter、逐 slot assignment，以保留 prefix mutation。
- touched source comments 移除当前纵切面的绝对地址；地址只保留在本分析文档。

## recovery IDB 改善

四份 IDB 已写入：

- `ResourceManager_load_guess`、`ResourceManager_unload_guess`、
  `ResourceManager_unloadAll_guess`；
- `LoadedModuleMap_subscript_guess` 及 candidate/insert/rehash/unwind 相关 helper；
- `WinSourceTextureMap_subscript_guess`；
- `PackedSourceAtlasMap_subscript_guess`；
- 四种 `LoadedResourceRecord_*_guess`、outer node、Win node、atlas descriptor/node 类型；
- constructor STL 分歧、candidate 强回滚、load 发布点、nested partial mutation、
  erase/clear/destructor 顺序的函数注释。

类型尺寸经 IDA local-type inspection 校验为：outer record `120/60/88/44`，outer node
`144/72/112/56`，Win node `32/16/32/16`，atlas mapped `64/64/64/60`，atlas node
`88/88/88/72` 字节。

## 验证

本轮验证结果：

- `cmake --build out/web/debug --parallel 1`：成功，重编译并链接 Web 目标；只出现仓库
  既有 `_tss`、pthread/memory-growth 与 JS library warning。
- 从 Web Debug `compile_commands.json` 复用 `EmoteEngine.cpp` 的真实 define、include、ABI
  和 Emscripten 参数，并加入既有 `out/syntax-check` Catch2/test config，对完整
  `tests/unit-tests/plugins/motionplayer-dll.cpp` 执行 `-fsyntax-only`：成功；唯一诊断为
  仓库既有 `_tss` deprecated literal-operator warning。
- 第二次 `cmake --build out/web/debug --parallel 1`：`ninja: no work to do.`。
- `git diff --check`：成功；仅报告工作树既有的 LF -> CRLF 转换提示，没有 whitespace
  error。
- 四份 recovery IDB：在类型/函数名/注释写入和尺寸复核后均已成功原位保存到
  `out/ida-recovery/{android-arm64,android-armv7,ios-arm64,ios-armv7}/`。

## 仍未闭合

- 原版私有 C++ 类型/成员的真实符号名不存在，恢复名继续以 `_guess` 标记。
- A32 node 对齐洞的位置已由写偏移闭合，但它们在具体旧 libstdc++ 模板层级中属于
  pair padding、node hash storage 还是 allocator 对齐包装，不应伪造为插件源码字段。
- atlas/Win 页面 texture construction raw local 的异常 cleanup 路由已由
  `motionplayer_resource_texture_construction_exception_four_binary_2026-08-15.md` 闭合；源码
  保留原版的 raw-local 泄漏边界，没有擅自添加 RAII 修复。
- 此纵切面闭合不表示整个 motionplayer 已一比一完成；其余高价值对象、调用链和异常路径
  仍按 `plan.md` 继续审计。
