# 引用计数状态机闭环（2026-08-03）

权威目标：Android ARM64 `reference/libkrkr2/libkrkr2.so`

SHA-256：`ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`

## 结论

本轮没有把前一轮的 40 个 shared-release 调用点直接当成“引用计数已经审完”，而是独立
枚举 114 个 MANIFEST 函数内的初值、retain、release、零引用终结、原子回退、虚调用和
类型辅助函数，并对外部 helper body 固定哈希。结果为：

- 初值 6 处：dispatch `ref=1` 两处、media `ref=1` 一处、owner `ref=0` 两处、
  Adopt 完成后的 owner bootstrap `ref=1` 一处。
- 非原子 `load → ±1 → store`：10 个 retain 三元组；19 个 owner decrement-release，
  严格分成 14 个 normal 与 5 个 landing-only。
- 另有 3 个优化折叠后的 retain/release identity。机器代码不再做算术，但仍保留 incoming
  `ref==0` 时的终结边界；不能因为没有 `SUB` 就从源码生命周期中删掉这些临时量。
- 114 个函数内独占原子指令完整 census 恰为 16 对：9 个 old-libstdc++ COW string
  decrement、7 个 `tTJSVariantString`/`ttstr` retain。9 个 COW decrement 均保留
  pthread presence gate、原子路径、非原子 fallback 和 `old<=0` delete。
- 15 个显式间接引用转换精确分为 `AddRef=5 / Release=9 / deleting terminal=1`；
  `GetDispatch@0x59B050` 返回 borrowed pointer，不被误算成 AddRef。
- 7 个直接 helper target 共 93 个调用/尾调用，另固定 10 个外部 helper body。
- 本地源码逐项一致，未发现新的生产 GAP；本轮不改 `cpp/`、fixture 或测试物料，也不构建。

最终判定：**ALIGNED / 无新增生产 GAP**。

## fresh 反编译证据

本轮 fresh decompile 覆盖 `0x597A40`、`0x597AC0`、`0x596F50`、`0x598E64`、
`0x599174`、`0x599878`、`0x599888`、`0x5999F4`、`0x59A4B0`、`0x59AEEC`、
`0xA13120`、`0xA13274`、`0xA0F5E0`、`0xA0F778`、`0xA0F790`、`0xA0FA0C`、
`0xA0FB64`、`0x8E3C20`、`0x97F2E0` 与 `0x14A3D90`。关键状态机压缩为十行：

```text
Owner AddRef: ++ref
Owner Release: next=--ref; if next==0 then aligned_free(data), delete owner
Folded pair: ref=ref; if incoming ref==0 then execute the same owner finalizer
Dispatch Release: next=--ref; if zero release owner and delete dispatch; return next
Media Release: if ref==1 delete this; else --ref
COW release: old=atomic-or-plain decrement; if old<=0 delete representation
COW copy: if ref<0 clone; else retain nonempty representation by atomic-or-plain increment
TJS string Release: if observed ref==0 finalize; else atomic decrement
Variant CopyRef: retain source by type before ReleaseContent(destination)
Object closure: AddRef/Release Object first, ObjThis second; global/class/item wrappers keep call order
```

`0xA0F778` 是只调用 `0xA0F790` 的析构包装。`0xA0F790` 先把 Variant type 清为 Void，
然后按 Object、String、Octet 分支释放；Object 分支先 Object 后 ObjThis，Octet 在 `ref==1`
时释放 data/object，否则只递减。`0xA13274` 的 String 规则与普通“递减后判零”不同：读取
到 0 时直接进入 `0xA13120` 终结体，非零才原子递减。上述差异均在本地对应类型中保留。

## intrusive owner、dispatch 与 media

### 初值与 retain

| 家族 | 初值站点 | retain 机器站点 | 源码语义 |
| --- | --- | --- | --- |
| `PSBValueDispatch` | `597AF4`, `59823C` 写 1 | dedicated `597AC0..597AC8`，另由构造路径保留 owner | fresh dispatch ref 从 1 开始，`++ref` 返回新值 |
| `PSBMedia` | `5984E8` 写 1 | `599878..599880` | 非原子 `++_ref` |
| `PSBRawOwner` | `59876C`, `598AAC` 写 0；`5987FC` 写 1 | `596914`, `597B04`, `598244`, `598A50`, `598CCC`, `598DF8`, `59A550`, `59A764` 起始的八组三元组 | ctor 从 0 开始，由 holder/dispatch copy 或 Adopt bootstrap 接管 |

`0x59876C..0x5987FC` 属于 `PSBFile::Adopt_guess@0x598708` 内联构造片段，不是独立 FDE。
校验器以真实 owner `0x598708` 固定这两次写入，避免把 allocation-site 地址伪造成函数边界。

### 19 个 owner decrement-release

| flow | aligned-free 终结调用 | 数量 |
| --- | --- | ---: |
| normal | `597A98`, `59881C`, `598DD8`, `599644`, `599A90`, `599F88`, `59A160`, `59A6B4`, `59A74C`, `59A7A4`, `59AC4C`, `59ACCC`, `59AD58`, `59B21C` | 14 |
| landing-only | `5981C8`, `5996C8`, `599DC0`, `59A26C`, `59A8B8` | 5 |

每项均固定连续 `LDR W → SUB #1 → STR W → CBNZ`，随后从同一 owner base 读取 data，
调用 `TJSAlignedDealloc@0xA0DE90`，再调用 `operator delete@0x415740`。终结前没有补零、
饱和或线程安全包装。

### 3 个优化折叠零探针

| owner | ref load / zero-test | 终结调用 | 源码生命周期 |
| --- | --- | --- | --- |
| `Adopt@598708` | `598828 / 598830` | `598838 / 598840` | replacement assignment 的 retain 与临时析构折叠为 identity |
| `Transfer@598A64` | `598A80 / 598A84` | `598A8C / 598A94` | hidden-sret 发布与 source 清空之间的转移临时量 |
| `Resolve@59A4B0` | `59A6D0 / 59A6DC` | `59A6E4 / 59A6EC` | strict result temporary 的 retain/release identity |

24 个 aligned-dealloc 站点现有无遗漏分区：

```text
19 owner decrement terminals
+ 3 folded zero-probe terminals
+ 1 raw MDF/source-buffer replacement @5986B0
+ 1 direct PSBRawOwner destructor body @598B48
= 24
```

这条分区同时排除了“还有一个未分类 refcount terminal”的可能。

## 两类原子状态机

### 9 个 old-libstdc++ COW decrement

| owner | normal atomic loop | landing atomic loop | delete 终点 |
| --- | --- | --- | --- |
| `EnumMembers@596F50` | `5974C0/5974C8` | `59755C/597564` | `5974EC`, `597578` |
| `GetDictionaryKeys@598E64` | `599038/599040` | `5990C0/5990C8`, `599140/599148` | `599064`, `5990F4`, `59916C` |
| vector reserve `@599174` | `599234/59923C` | — | `599260` |
| `GetListAt@5999F4` | `599CA0/599CA8` | `599D40/599D48` | `599CD0`, `599D5C` |
| vector growth `@59B7E8` | `59B938/59B940` | — | `59B964` |

每一行的非原子 fallback 也被固定为独立 `LDR W → SUB #1 → STR W`，与 atomic path 汇合
后按旧值 `<=0` 删除 representation。这里恢复出的源码 token 是普通 `std::string`/
`std::vector<std::string>` 生命周期；pthread gate 与 COW header 是目标 Android 旧标准库的
内联实现，不应在 wasm32 生产代码中伪造一套自定义 ABI 容器。

### 7 个 shared-string retain

`599FD8`、`59A5CC`、`59A608`、`59A640`、`59AF68`、`59AF88`、`59AFD8` 起始的七个
64-bit `LDAXR → ADD #1 → STLXR → retry` 循环覆盖 Load adaptor、Resolve 与 RegistItem 中
的 ttstr/Variant String copy。对 114 个 FDE 的独立指令扫描未发现第 17 对 LDAXR/STLXR；
16 对全部被上述 9+7 两类状态机解释。

## virtual 与直接 helper 调用面

### 间接引用转换

| 操作 | 站点 | 数量 |
| --- | --- | ---: |
| AddRef | `599F30`, `599F40`, `59ADFC`, `59B4E4`, `59B4F4` | 5 |
| Release | `596958`, `599F5C`, `59A3CC`, `59A9DC`, `59AA58`, `59AE48`, `59AE7C`, `59B0A8`, `59B524` | 9 |
| deleting terminal | `59989C` | 1 |

这些站点与完整 indirect ABI manifest 交叉验证了 producer load、target register 和 owner
FDE。`RegistItem@59B050` 的 `item->GetDispatch()` 是 borrowed result；其真正的 Object/
ObjThis retain 发生在随后 Variant 构造路径，不能人为给 GetDispatch 增加引用。

### 直接 helper

| target | 站点数 | 状态机角色 |
| --- | ---: | --- |
| `0x8E3C20` | 4 | 获取 global dispatch 并 AddRef |
| `0xA0F5E0` | 4 | Variant copy construction |
| `0xA0F778` | 32 | Variant destructor wrapper |
| `0xA0F790` | 5 | Variant type-specific release |
| `0xA0FB64` | 6 | retain-before-release CopyRef |
| `0xA13274` | 40 | Variant String release |
| `0x14A3D90` | 2 | COW string clone-or-retain copy helper |
| **合计** | **93** | 7 个 target |

另对 `0x97F2E0` global AddRef wrapper、`0xA13120` String finalizer、`0xA0FA0C` Variant
release helper 连同上表函数共 10 个 helper body 固定 `{address,size,SHA-256}`，避免调用点
不变但终结规则漂移。

## 本地逐行对照

| Android 状态机 | 本地实现 | 对照 |
| --- | --- | --- |
| owner 从 0 起步；AddRef 递增；Release 先递减、零时 delete | `PSBRawFile.h:31-44,57-60` | 一致 |
| PSBFile copy/assignment/destructor 驱动 owner retain/release，assignment 无 self guard | `PSBRawFile.h:81-106` | 顺序一致 |
| dispatch 初值 1，AddRef 返回递增值，Release 返回递减值并在 0 delete | `PSBDispatch.h:131`、`main.cpp:96-107` | 一致 |
| media 初值 1；`ref==1` 直接 delete，否则递减 | `PSBMedia.h:12-23,45` | 一致 |
| Variant String ref 从 0 起，零值 Release 终结、非零递减 | `tjsVariantString.h:57-72`、`tjsVariantString.cpp:442-455` | 一致 |
| Octet 从 1 起，`ref==1` delete，否则递减 | `tjsVariant.h:45-61`、`tjsVariant.cpp:44-64` | 一致 |
| Variant copy 按类型 retain；Object 先 Object 后 ObjThis | `tjsVariant.cpp:320-338`、`tjsVariant.h:195-217` | 一致 |
| CopyRef 在释放 destination 前 retain source | `tjsVariant.cpp:476-520` | 一致，包含 alias 边界 |
| Clear/ReleaseContent 先清 type，再按 Object/String/Octet 释放 | `tjsVariant.cpp:378-402`、`tjsVariant.h:488-526` | 一致 |
| global dispatch getter 返回持有引用，ncbind 使用后 Release | `ScriptMgnIntf.cpp:651-657`、`ncbind.hpp:1890-1933,2016-2051` | 一致 |
| dictionary keys 使用普通 `std::vector<std::string>` reserve/emplace | `PSBRawFile.cpp:280-306` | 与目标源码层容器一致；不复制目标标准库 ABI |

CreateVariant 的 Object/ObjThis 引用级联也由 `main.cpp:670-678` 保留：fresh dispatch 从 1
开始，closure 的两个槽各自持有引用，CopyRef 与临时析构按目标顺序抵消，最后显式释放
construction reference；不能合并成一个智能指针引用。

## 机械门禁

`verify_elf_surface.py` 新增三组互相交叉的输出：

```text
reference_count_surface=true initializers=6 initializer_families=4 retain_triples=10 retain_families=3 owner_releases=19 owner_zero_probes=3 owner_finalizers=22 normal_owner_releases=14 landing_owner_releases=5 aligned_partition=24 machine_sites=348 sha256=true
reference_count_atomic_surface=true pairs=16 cow_decrements=9 cow_normal=5 cow_landing=4 fallback_paths=9 shared_retains=7 exclusive_census_complete=true
reference_count_helper_surface=true direct_targets=7 direct_sites=93 direct_roles=7 helper_bodies=10 body_roles=10 indirect_sites=15 indirect_roles=9 addref=5 release=9 terminal=1 body_sha256=true
```

门禁同时执行：

1. 348 个状态机 instruction `{address,word}` 序列哈希；
2. 全 114 FDE 的 LDAXR/STLXR 独立 census，并要求恰好等于 9+7 表；
3. 19+3+1+1 对 24 个 aligned-dealloc 的完整分区；
4. owner/COW 终结站点与既有 normal/landing heap surface 交叉；
5. 15 个 virtual 转换与 indirect ABI surface 交叉；
6. 93 个 direct helper 站点与 complete external-callee surface 交叉；
7. 10 个外部 helper body 的地址、长度与 SHA-256。

完整 ELF 门禁通过。由于没有 `cpp/` 变化，本轮按规则不触发构建；结论来自 fresh
反编译、全函数机器 census 和本地逐行对照，而不是因为缺少运行时 fixture 而 defer。
