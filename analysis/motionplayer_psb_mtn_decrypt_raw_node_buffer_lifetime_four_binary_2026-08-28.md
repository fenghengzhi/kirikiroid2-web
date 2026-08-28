# PSB/MTN 输入、解密、raw-node 与 buffer 生命周期四参考二进制联合恢复

日期：2026-08-28  
原始任务：`MP-D05`

## 1. 结论

四端共享同一条源码级所有权链：字符串输入经 `LoadStorage` 读取为一块暂时没有 owner 的裸缓冲区，
octet 输入则先尝试 MDF 解压、否则复制；`Adopt` 先验证最小长度和 `PSB\0` 签名，再建立一个非原子的
intrusive `PSBRawOwner` 并把它发布到目标 `PSBFile`。只有 storage/MTN 路径把进程级 decrypt filter
传给 `Adopt`；普通 octet 路径传空 filter。

filter 被调用时 owner 已经安装到目标 holder。filter 可以原地修改 owner 的 buffer；返回后才执行
`Refresh(true)` 重建所有内部表指针并做有限的 offset 边界检查。filter 抛出、`Refresh(true)` 返回
false、或后续根字段校验抛出，都不会把目标 holder 回滚到旧 owner。这个顺序以及下述异常泄漏、
allocator/deallocator 不对称和借用指针边界均是四端共同可观察行为，本地实现已经匹配；本轮不修改
运行时 C++ 语义。

所有 `PSBRawNode`、`PSBValueDispatch` 和 `ObjSource` 中的 node/header/table 指针都只是指向 owner
allocation 内部的借用视图。它们不单独释放这些指针，而是同时保存一个 `PSBFile` holder 来 retain
同一个 owner。因此 `ResourceManager::unload` 只移除缓存自身的引用：此前返回的 dispatch/raw node/
`ObjSource` 仍然有效，直到最后一个 intrusive owner 引用释放；最后一次释放只执行一次
`TJSAlignedDealloc(data)`，随后删除 owner 对象。

## 2. 本轮 fresh 四端证据总量

本轮通过原生 `mcp__idalib__*` 对 70 个独立函数范围重新执行 decompile、完整 disassembly 和
`xrefs_to` 审计；全部 disassembly 都是 `truncated=false`，没有以采样窗口冒充完整函数。

| 平台 | 独立范围 | 完整指令 | `xrefs_to` | IDB 更新 |
|---|---:|---:|---:|---|
| Android arm64 | 16 | 1,579 | 243 | 7 个 helper 命名、16 条任务注释、1 个书签 |
| Android armv7 | 18 | 958 | 248 | 9 个 helper 命名、18 条任务注释、1 个书签 |
| iOS arm64 | 18 | 857 | 257 | 9 个 helper 命名、18 条任务注释、1 个书签 |
| iOS armv7 | 18 | 1,292 | 271 | 9 个 helper 命名、18 条任务注释、1 个书签 |
| 合计 | 70 | 4,686 | 1,019 | 34 个 helper 命名、70 条注释、4 个书签；四库原位保存 |

指令数包含 `PSBFile::Load`、`LoadStorage`、MDF helper/内联体、`Adopt`、filter 调用包装、
`Refresh`、owner ctor/dtor/Release、`GetRoot`、dispatch ctor/Release、严格 raw-node lookup、
`ObjSource` 析构、两种 decrypt invoker、全局 filter replacement，以及 ResourceManager load/unload。

## 3. 四端函数映射

### 3.1 输入、MDF、Adopt 与 owner

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| `PSBFile::Load(Variant)` | `0x598648`，172 条 | `0x4DCEB8`，94 条 | `0x1000ED1B4`，101 条 | `0xE955C`，158 条 |
| `PSBFile::LoadStorage` | `0x598918`，115 条 | `0x4DD0A0`，78 条 | `0x1000ED468`，69 条 | `0xE9874`，111 条 |
| MDF decode | 内联于两个 caller | `0x4DD17C`，51 条 | `0x1000ED5B4`，39 条 | `0xE99D0`，36 条 |
| `PSBFile::Adopt` | `0x598AE8`，149 条 | `0x4DD200`，56 条 | `0x1000ED654`，58 条 | `0xE9A28`，56 条 |
| `std::function` 调用包装 | Adopt 内联/间接调用 | Adopt 内联/间接调用 | `0x1000ED788`，23 条 | `0xE9AEC`，25 条 |
| `PSBRawOwner::Refresh` | `0x598D40`，55 条；Adopt 中另有内联体 | `0x4DD2A0`，62 条 | `0x1000ED7E8`，56 条 | `0xE9B36`，62 条 |
| standalone owner ctor | `0x598E8C`，36 条 | `0x4DD36E`，13 条 | Adopt 内联 | Adopt 内联 |
| owner dtor | `0x598F1C`，7 条 | `0x4DD38C`，7 条 | `0x1000ED920`，10 条 | `0xE9C00`，41 条（含 armv7 EH 形态） |
| shared owner `Release` | 调用点内联 | `0x4DE564`，12 条 | `0x1000EEEFC`，11 条 | `0xEB014`，12 条 |

Android arm64 把 MDF helper 内联进 string/storage 与 octet 两个 caller；另外三个目标保留同一 shared
helper。两个 Android IDB 都还保留一个 standalone owner ctor，当前 task 根对它的 xref 为零；四端
`Adopt` 都可独立内联或标量化相同构造序列。这些是优化/链接边界，不改变 portable 源码结构。

### 3.2 raw-node、dispatch、ObjSource 与缓存

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| `PSBFile::GetRoot` | `0x598E1C`，10 条 | `0x4DD33A`，11 条 | `0x1000ED8C8`，7 条 | `0xE9BD0`，7 条 |
| `PSBValueDispatch` ctor | `0x597EB4`，17 条 | `0x4DCB50`，19 条 | `0x1000EC248`，15 条 | `0xE8874`，17 条 |
| dispatch `Release`/delete | `0x597E20`，31 条 | `0x4DCB0C`，21 条 | `0x1000ECD00`，21 条 | `0xE9164`，21 条 |
| strict child raw node | `0x599038`，63 条 | `0x4DD49C`，61 条 | `0x1000EDA48`，42 条 | `0xE9D10`，82 条 |
| `ObjSource` dtor | `0x6E145C`，101 条 | `0x5A1EE8`，13 条 | `0x100132A60`，16 条 | `0x131AF8`，50 条 |
| `ResourceManager::load` | `0x6A616C`，501 条 | `0x57B338`，246 条 | `0x1001012D8`，225 条 | `0xFE40C`，364 条 |
| `ResourceManager::unload` | `0x6A697C`，87 条 | `0x57B6F8`，47 条 | `0x100101A28`，35 条 | `0xFEC04`，69 条 |

`GetRoot` 和严格 child lookup 都返回“一个 retained `PSBFile` holder + 一个内部 node 指针”。dispatch
constructor 同样先 retain file owner，再单独保存 node 指针；dispatch 自身另有从 1 开始的非原子
32 位引用计数。`ObjSource` 的显式析构体先 Release lazy texture，随后隐式 `PSBRawNode` member
析构再 Release raw owner；四端顺序一致。

### 3.3 decrypt filter 与全局 target

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| seed filter invoke | `0x6837AC`，33 条 | `0x56522E`，33 条 | `0x1001B92E8`，31 条 | `0x1B8992`，38 条 |
| callable filter invoke | `0x6838A0`，140 条 | `0x5652C0`，90 条 | `0x1001B94A8`，61 条 | `0x1B8AB0`，104 条 |
| replace process-global filter | `0x6A5BB0`，62 条 | `0x57B174`，44 条 | `0x1001010C4`，37 条 | `0xFE1F0`，39 条 |

两种 setter 都替换同一个进程全局 `OwnerFilter`，不是每个 `ResourceManager` 的成员。replacement 采用
copy/swap 等价语义：复制新 target 抛出时旧 target 仍在；成功后旧 target 在 setter 返回前销毁。
load 与 replacement 之间没有 mutex、原子快照或线程安全发布。

## 4. 共同输入数据流

```text
String / MTN path
  -> placed-path normalization
  -> create read stream
  -> size >= 9
  -> aligned raw allocation
  -> ReadBuffer
  -> optional MDF decode
       success: free source allocation, use decoded allocation/size
       reject: keep original allocation as PSB candidate
  -> Adopt(data, size, process-global decrypt filter)

Octet
  -> optional MDF decode
  -> otherwise aligned allocation + memcpy
  -> Adopt(data, size, empty filter)

Adopt
  -> require size >= 0x40 and signature == PSB\0
  -> allocate raw owner; initial refcount = 0
  -> construct header view from ten header fields
  -> publish replacement owner into destination PSBFile
  -> if filter exists:
       filter(owner)              // mutable borrow of installed owner
       return owner.Refresh(true) // no rollback on false/throw
     else:
       return true                // no second Refresh
```

MDF 检测要求输入至少 11 字节且首 dword 为 `mdf\0`。期望解压长度来自第二个 dword；zlib 实际输出
长度会写回 `size`。解压失败返回 null，caller 继续把原输入视作普通 PSB 候选；解压成功才替换源
allocation。四端都没有要求 MDF 声明长度与最终实际长度再次相等。

## 5. decrypt 的精确借用范围

seed filter 从当前 header view 的 `encryptData` 开始，到 `chunkOffsets` 之前结束；长度先按 pointer
差计算，再截成 signed 32 位。长度 `<= 0` 时直接返回。随机流固定为四个 32 位 xorshift 状态：
`123456789, 362436069, 521288629, low32(seed)`。每个随机 word 从低字节开始异或；剩余 word 值为
零就是 refill sentinel，因此含零高字节的 word 可能提前生成下一 word，而不是严格消费四字节。

callable filter 的 control block retain 传入 closure 的 Object 与 ObjThis。每次调用建立一个直接借用
`owner.data/owner.size` 的 `CBinaryAccessor`，再以 `{accessor, size}` 两个参数调用 closure，`objthis`
实参为 null，返回值被忽略。四端都没有在 `tTJSVariant` 已 AddRef accessor 后释放 constructor 的初始
引用，因此每次 callable decrypt 留下一个 accessor 引用；不能在移植中擅自修成 RAII 平衡。

accessor 不拥有 raw allocation。同步回调期间 owner 由 `Adopt` 后的目标 `PSBFile` 保持；脚本若把
accessor 保存到回调之后，它只是继续指向 owner buffer，并不会阻止后续最后一次 owner Release 释放
allocation。这是刻意保留的借用/悬空边界，不应把 accessor 改成 buffer owner。

## 6. `Refresh(true)` 的验证范围与缺失验证

四端先无条件从 data 重新读取签名、版本、encrypt 标志和八个 offset，并立即重建所有内部指针；之后
才根据 `validateOffsets` 决定是否检查。`Adopt` 的 filter 后路径传 true；constructor/初始构造等价路径
传 false 或直接内联，不验证 offset。

true 路径只做下列 signed pointer-width 比较：

- `size > encryptDataOffset`；
- `size >= names/strings/stringsData/chunkOffsets/chunkLengths/chunkDataOffset`；
- `size > entriesOffset`。

它不再次校验 signature/version/encrypt，不要求 offset 单调，不检查各表元素数量、packed array 宽度、
chunk 的 offset+length、字符串终止符或 dictionary value offset。失败时已经重建的 header view 和目标
holder都保留；返回 false 只通知 caller，不回滚 owner 或 buffer 内容。

## 7. owner、借用指针与销毁时序

```text
raw allocation
    ^ owned exactly once by
PSBRawOwner (intrusive non-atomic refcount)
    ^ retained by any number of
PSBFile holders
    +-- ResourceManager cached record
    +-- PSBRawNode.file_  -------- node_ borrows allocation interior
    +-- PSBValueDispatch.value_ -- node_ borrows allocation interior
    `-- ObjSource._source ------- node_ borrows allocation interior
```

关键边界：

- `PSBFile` copy 先复制 owner 后 AddRef；析构 Release；assignment 按“Release old、copy pointer、AddRef”
  顺序且没有 self-assignment guard，自赋值可先销毁最后一个 owner。
- 非 throwing `PSBRawNode::GetDictionaryValue(key, output)` 先算出 child，随后对 `output.file_` 执行上述
  assignment，再写 node。`output` 与 source alias 时同样保留 release-before-retain 风险。
- `GetRoot` 不检查空 owner；只有脚本可见的另一个 root getter 做空 holder 防御。
- ResourceManager cache 只保存 raw file holder；每次 load 即使 cache hit 仍创建新的 dispatch facade。
- unload/unloadAll 不 invalidate 已发布 facade；最后一个外部 dispatch/raw node/ObjSource 释放之前，
  header、node、string 与 resource interior pointer 都仍受 owner allocation 支撑。
- dispatch refcount与 raw owner refcount都为非原子普通 32 位计数；并发 AddRef/Release/load/unload/filter
  replacement 是数据竞争边界。

## 8. 精确的 buffer 释放与泄漏边界

| 路径 | 四端共同结果 |
|---|---|
| stream null 或 size `< 9` | 返回 false；没有 raw allocation |
| storage `ReadBuffer` 抛出 | stream/路径临时对象清理；读取前分配的 raw buffer 泄漏 |
| MDF decode 失败 | decoded buffer 以 `delete[]` 释放；原 buffer 继续作为 PSB 候选 |
| MDF decode 成功 | source 以 `TJSAlignedDealloc` 释放；decoded buffer 交给 Adopt/owner |
| storage `Adopt` 前置拒绝 | 返回 false；caller 不回收 raw buffer，发生泄漏 |
| octet `Adopt` 前置拒绝 | caller 对 aligned pointer 执行 `delete[]` 后抛错；保留 allocator mismatch |
| filter 抛出 | 新 owner 已安装；stack cleanup 释放临时 filter，但不恢复旧 owner |
| filter 后 `Refresh(true)` 失败 | 返回 false；新 owner 仍安装且拥有 buffer |
| owner 最后一次 Release | `TJSAlignedDealloc(data)`，然后 delete owner；内部视图不逐个释放 |

这里的 `delete[]`/`TJSAlignedDealloc` 不对称并非推荐实现方式，而是四端反编译共同证明的边界行为。
为了 1:1 复原，不能把 storage 与 octet 失败路径合并成一个“更安全”的智能指针流程。

## 9. 本地源码逐行对照

本地实现已经覆盖四端共同结构：

- `cpp/plugins/psbfile/PSBRawFile.cpp:21`：MDF detection/decode 和失败释放；
- `cpp/plugins/psbfile/PSBRawFile.cpp:138`：owner 内部 header view；
- `cpp/plugins/psbfile/PSBRawFile.cpp:171`：唯一 raw allocation 析构；
- `cpp/plugins/psbfile/PSBRawFile.cpp:176`：重建指针与 signed-width offset validation；
- `cpp/plugins/psbfile/PSBRawFile.cpp:229`：raw-node replacement 的 release-before-retain 顺序；
- `cpp/plugins/psbfile/PSBRawFile.cpp:444`：string/octet 分流与 octet failure cleanup；
- `cpp/plugins/psbfile/PSBRawFile.cpp:484`：storage stream、MDF 与裸 buffer 异常边界；
- `cpp/plugins/psbfile/PSBRawFile.cpp:517`：owner publication、filter、Refresh 和无回滚顺序；
- `cpp/plugins/psbfile/PSBRawFile.h:34`：intrusive owner 与 one-pointer holder；
- `cpp/plugins/psbfile/PSBFile.cpp:21`、`:103`：dispatch retain 与非原子 Release/delete；
- `cpp/plugins/motionplayer/ResourceManager.cpp:125`、`:134`、`:191`：全局 target、seed/filter closure；
- `cpp/plugins/motionplayer/ResourceManager.cpp:310`、`:393`：cache/facade 发布和 unload；
- `cpp/plugins/motionplayer/SourceCache.h:125`、`SourceCache.cpp:307`：`ObjSource` raw-node owner 与 texture-first teardown。

本轮只修正了 `PSBRawOwner` constructor 的证据注释：fresh map 表明两个 Android 链接都保留 standalone
ctor，而两个 iOS 链接将其内联；四端 Adopt 都可能独立标量化同一序列。没有改变对象布局、数据流、
释放时序或异常行为。

## 10. 可执行证据与验证边界

已有 unit source 直接覆盖三条最关键的可观察边界：

- `tests/unit-tests/plugins/motionplayer-dll.cpp:6557`：seed setter 参数转换与全局替换；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:6575`：callable closure 双 owner、同步 accessor 参数、旧 target
  在替换时销毁；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:27307`：同一路径两次 load 返回不同 dispatch，unload 后外部
  dispatch 仍能读取 raw metadata，证明 cache/facade 独立 retain owner。

仓库还保留 `tests/differential/oracle_runner/adapters/psbfile_load.py`、
`tests/differential/python/run_psbfile_load_adb.py` 和 natural-boundary scanner，用真实 PSB/MDF 文件覆盖
输入路径；它们的实际设备/正式构建执行归入 `MP-V` 验证任务，不在静态 closure 中伪造结果。

本轮完成四端 fresh 反编译、4,686 条完整指令、1,019 个 `xrefs_to`、34 个 helper 命名、70 条任务
注释、4 个书签、四库保存、coverage/163-ticket 映射更新和文本一致性检查。`MP-D05` 没有剩余的
task-local 静态差异；正式 native unit、Web Debug 和真实 PSB/MDF differential 执行仍由 `MP-V` 独立追踪。
