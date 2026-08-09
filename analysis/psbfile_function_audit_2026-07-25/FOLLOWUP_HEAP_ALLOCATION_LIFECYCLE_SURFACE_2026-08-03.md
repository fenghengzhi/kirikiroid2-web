# 堆分配、发布与清理生命周期闭环（2026-08-03）

权威目标：Android ARM64 `reference/libkrkr2/libkrkr2.so`

SHA-256：`ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`

## 结论

本轮把既有“调用点、实参 producer、返回值 first-event、normal CFG、LSDA landing”五个
机器表面连接成一张 source-facing 堆生命周期表。结果如下：

- 114 个 MANIFEST 函数内共有 **20 个直接分配结果站点**：
  `operator new=15 / TJSAlignedAlloc=4 / TJSAllocVariantOctet=1`；覆盖 15 个 owner、
  12 个对象/缓冲区族和 16 种发布策略。
- 20/20 分配调用均与既有 direct-argument manifest 精确相交；分配尺寸、alignment、
  Octet data/length 的全部来源不缺失。20/20 也与 call-result manifest 相交，第一条
  `X0` 使用/覆盖锚点全部一致。
- 分配后共有 **68 个显式生命周期锚点**：59 个 normal、9 个 landing-only。另以
  8 条 LSDA 边固定 7 个分配点的 constructor/copy 异常清理。
- 直接释放/共享引用释放的完整 census 为 **120 个站点**：80 个 raw/object/storage
  释放与 40 个 shared-reference release；其中 normal=83、landing=37，`BL=114`、
  cross-FDE tail `B=6`。
- 另固定 3 条正常泄漏路径和 8 条异常泄漏边，覆盖 5 个分配点。它们不是“缺少验证”或
  “建议安全化”的问题，而是 Android 目标明确保留的边界。
- fresh decompile 与本地实现逐段对照后没有发现新的生产 GAP；本轮不修改 `cpp/`、fixture
  或测试物料，也不触发构建。

这里的“完整”限定为 **114 个 MANIFEST 函数内对三个直接 allocator target 的调用结果，
以及五个直接 release/refcount-helper target 的调用站点**。优化后的同一 allocation site
可以在循环中产生多个动态对象，跨函数转交后的最终 virtual destruction 也不能静态配成
一条唯一边；本报告没有把 site census 冒充逐实例一一配对。

## 20 个直接分配站点

表中地址均为真实 ARM64 调用地址；IDA 伪代码有时把调用后的结果承载地址显示在表达式
注释上，机器门禁仍以 call word 地址为准。

| 分配点 | allocator 实参 | 分配族 | 发布/转移与终结边界 | 本地源码结构 |
| --- | --- | --- | --- | --- |
| `0x5968DC` | `new(0x30)` | `PSBValueDispatch` | closure 临时量 → `CopyRef@596940` → 临时析构 → construction ref `Release@596958` | `main.cpp:670-678` |
| `0x596B5C` | `TJSAllocVariantOctet(data,length)` | Variant Octet backing | 临时 Octet → `CopyRef@596B6C` → 临时析构 | `main.cpp:650-665` |
| `0x597D38` | `new(dynamic capacity)` | `vector<char>` backing | 新 backing 成为当前 local vector；旧 backing 释放，函数尾释放最终 backing | `PSBRawFile.cpp:112-135` |
| `0x597F64` | `new(0x38)` | factory NCB wrapper | `RegistItem@597FC4` 接管；ctor 抛出时 `delete@5980E8` | `main.cpp:751-755`、`ncbind.hpp:1397-1436` |
| `0x597FD4` | `new(0x50)` | property NCB wrapper | `RegistItem@598038` 接管；ctor 抛出时共享 `delete@5980E8` | `main.cpp:751-755`、`ncbind.hpp:1441-1496` |
| `0x598048` | `new(0x40)` | method NCB wrapper | tail `RegistItem@5980B8` 接管；ctor 抛出时共享 `delete@5980E8` | `main.cpp:751-755`、`ncbind.hpp:1325-1358` |
| `0x598128` | `new(8)` | `PSBFile` native holder | `*result=file@598138` 先发布；两条 catch landing 释放 owner/data 与 holder，但不清 result 槽 | `main.cpp:732-748` |
| `0x598210` | `new(0x30)` | root `PSBValueDispatch` | inline 初始化、owner retain，直接 `RET@598264` 发布 fresh ref | `main.cpp:690-700` |
| `0x5982D4` | `TJSAlignedAlloc(size,4)` | Octet raw copy | `Adopt@5982FC` 成功转交；false 时 `delete[]@598328` | `PSBRawFile.cpp:463-479` |
| `0x5983E8` | `TJSAlignedAlloc(expected,4)` | Octet MDF output | zlib failure `delete[]@59840C`；成功后同一 Adopt/false-delete 边 | `PSBRawFile.cpp:463-479` 与 MDF helper |
| `0x5984CC` | `new(0x28)` | `PSBMedia` singleton | global static pointer `@5984FC` 后 guard release；无 process cleanup 注册 | `PSBMediaRegistry.cpp:6-11` |
| `0x5985C8` | `TJSAlignedAlloc(streamSize,4)` | storage raw input | `ReadBuffer` 填充后交给 Adopt；false/异常均不释放该 raw pointer | `PSBRawFile.cpp:482-513` |
| `0x598674` | `TJSAlignedAlloc(expected,4)` | storage MDF output | zlib failure delete[]；成功才 aligned-free source 并选择 decoded；Adopt false/异常不清 selected buffer | `PSBRawFile.cpp:482-513` |
| `0x598764` | `new(0x68)` | `PSBRawOwner` | ref=1 后替换 `self->owner`；先终结旧 owner，保留临时 holder 的 incoming-zero 清理形状 | `PSBRawFile.cpp:516-539`、`PSBRawFile.h:31-70` |
| `0x5991D0` | `new(8*capacity)` | `vector<string>` reserve backing | move COW string 槽、释放旧 backing，再提交 begin/end/capacity | `PSBRawFile.cpp:280-306` 的 `result.reserve` |
| `0x599998` | `new(0x20)` | `tTVPMemoryStream` | ctor 后直接返回；ctor 抛出由 `delete@5999E8` 回收 new-expression storage | `PSBMedia.cpp:133-146` |
| `0x599ECC` | `new(8)` | cache candidate `PSBFile` | Load false 显式 delete；non-null adaptor 认领；adaptor null 仍提交 Void/cache 并泄漏 file | `PSBMedia.cpp:19-49` |
| `0x59AABC` | `new(0xB0)` | native class object | `ncb_classInit` 后先写 `_classobj@59AAEC`，再注册 class/finalize；ctor 异常 delete | `ncbind.hpp:1853-1873` |
| `0x59ABE4` | `new(0x18)` | empty instance adaptor | 初始化 `{vptr,null,false}` 后直接返回 | `ncbind.hpp:228-231` 与 adaptor 字段 |
| `0x59B864` | `new(8*newCapacity)` | `vector<string>` emplace backing | copy 新元素、move 旧元素、释放旧 backing、提交三指针；copy 抛出时 delete 新 backing 并重抛 | `PSBRawFile.cpp:300-305` 的 `emplace_back` |

allocator 的 ABI 实参形状也被交叉固定：

```text
operator new: X0 unsigned-64 size，15/15 均为单一 producer
TJSAlignedAlloc: X0 signed-32 size + X1 signed-8 alignment(4)，4/4 均完整
TJSAllocVariantOctet: X0 data pointer + X1 unsigned-32 length；length 有两条 tag 分支 producer
```

这不是根据本地 `new` 反推二进制：每一项均先由目标 direct call、AArch64 实参切片和
first-result event 独立确定，再与本地源码对照。

## 高风险 owner 的 fresh 伪代码

本轮 fresh decompile 了 `0x598268`、`0x598538`、`0x599E04`、`0x5980F4`、
`0x597F38`、`0x59AA84`、`0x59B7E8`，并补扫其余分配 owner。关键逻辑压缩为十行：

```text
Factory: file=new PSBFile; *result=file; try optional Load(copy(arg0)); catch { delete file; throw; }
Load: data=MDF-decode-or-aligned-copy(octet); if !Adopt(data,size) { delete[] data; throw; }
Load: Adopt 成功即由新 PSBRawOwner 接管；Adopt 自身抛出时 caller 不补 raw cleanup
LoadStorage: stream=CreateStream; data=AlignedAlloc(u32(stream.size),4); stream.ReadBuffer(data,size)
LoadStorage: decoded!=null 时 aligned-free(data) 并选择 decoded；随后直接 return Adopt(selected,...)
LoadStorage: ReadBuffer/Adopt/filter 抛出只析构 stream；Adopt false 也不释放 selected buffer
Adopt: validate signature/size; replacement.owner=new PSBRawOwner(data,size); ref=1; assign into self
Adopt: 先 decrement/free 旧 owner；filter 非空时调用它并 Refresh(true)，否则 true
EnsureContainer: new file; Load false delete；成功后 adaptor=CreateAdaptor(file,false,false)
EnsureContainer: adaptor null 仍将 _file 置 Void、更新 _container、返回 true，file 未被认领
```

## exception cleanup 与泄漏边界

### 有回收的 8 条 LSDA 边

| 分配点 | throwing call | landing | 最终 cleanup | 契约 |
| --- | --- | --- | --- | --- |
| `597F64` | wrapper ctor `597F6C` | `5980E0`, action 0 | `delete@5980E8` | factory wrapper new-expression cleanup |
| `597FD4` | wrapper ctor `597FDC` | `5980D4`, action 0 | `delete@5980E8` | property wrapper cleanup |
| `598048` | wrapper ctor `598050` | `5980D0`, action 0 | `delete@5980E8` | method wrapper cleanup |
| `598128` | Variant copy `598148` | `5981A0`, action 1 | holder `delete@5981D8` | published Factory holder catch/rethrow |
| `598128` | `Load@598154` | `598190`, action 1 | holder `delete@5981D8` | 同一 catch/rethrow，结果槽不清零 |
| `599998` | stream ctor `5999A8` | `5999E0`, action 0 | `delete@5999E8` | memory-stream new-expression cleanup |
| `59AABC` | `ncb_classInit@59AACC` | `59ABA4`, action 0 | `delete@59ABAC` | native class construction cleanup |
| `59B864` | COW string copy `59B884` | `59B99C`, action 1 | new backing `delete@59B9AC` | vector strong-exception cleanup/rethrow |

### 明确保留的丢失所有权

机器门禁把“存在某个 delete”与“这条失败边实际走到该 delete”分开：

- `LoadStorage` 的两个 selected-buffer allocation 在 `Adopt=false` 后从 `0x59861C` 到
  `RET@0x598664`；可达子图不经过 `0x59869C/0x5986B0`。这固定 input/decoded 两条
  false-return 泄漏路径。
- `EnsureContainer` 的 adaptor-null 分支从 `0x599FB8` 到 `RET@0x59A02C`；可达子图不经过
  Load-false 的 `0x599F88/0x599F90/0x599F98`，因此成功加载的 raw file 没有 owner。
- 8 条 exception-leak edge 分别覆盖 Load 的 Adopt、LoadStorage 的 ReadBuffer/decode/
  Adopt，以及 EnsureContainer 的 LoadStorage/CreateAdaptor。相应 landing 只清
  filter、stream 或 ttstr；禁止的 raw/object deallocator target 均不在该 landing graph。

这与 `PSBRawFile.cpp:495-513`、`PSBMedia.cpp:30-49` 中现有 raw pointer/空 adaptor 表达
一致。不能换成 `unique_ptr` 兜底、不能让 adaptor-null 变成 false，也不能给 cache 更新做
回滚；这些“更安全”的改法都会偏离目标边界。

## 完整 direct release census

| target | 语义族 | normal | landing | 合计 |
| --- | --- | ---: | ---: | ---: |
| `0xA0DE90` | aligned dealloc | 19 | 5 | 24 |
| `0x415740` | scalar/object `operator delete` | 32 | 12 | 44 |
| `0x426B40` | `operator delete[]` | 3 | 0 | 3 |
| `0x14A3C0C` | old-libstdc++ vector/COW storage delete thunk | 5 | 4 | 9 |
| `0xA13274` | shared backing/reference release | 24 | 16 | 40 |
| **合计** | 5 个 target | **83** | **37** | **120** |

前四族共 80 个 raw/object/storage release；最后一族 40 个是 refcounted backing release，
两者不能合并成同一种 `delete`。44 个 `operator delete` 中有 6 个 source-visible
cross-FDE tail `B`，其余 release 都是 `BL`，故总数为 `BL=114 / B=6`。

## 本地逐行对照

| Android 伪代码步骤 | 本地实现 |
| --- | --- |
| dispatch/Octet 先放临时 Variant，再 CopyRef、析构临时量 | `main.cpp:650-678` 保留 Octet 临时和 Object/ObjThis 双引用、最终 construction Release |
| Factory 先写 caller result，后执行可抛 Load；catch delete 后原槽不清 | `main.cpp:732-748` 顺序逐项一致 |
| root getter null guard 后直接 new dispatch 并返回 fresh ref | `main.cpp:690-700` 没有改走低层无 guard root helper |
| storage raw buffer 不使用 owning RAII，stream 使用 RAII | `PSBRawFile.cpp:482-513` 仅 `stream` 为 `unique_ptr`，两个 buffer 都是 raw pointer |
| Adopt 用临时 `PSBFile replacement` 形成 AddRef/assignment/destruction token | `PSBRawFile.cpp:516-539` 保留同一 holder/owner 交接和旧 owner 终结顺序 |
| DecodeName 使用 `vector<char>`，dictionary keys 使用 `vector<string>` reserve/emplace | `PSBRawFile.cpp:112-135,280-306` 保留两类容器而未换自定义平坦 buffer |
| singleton 为函数局部 `static PSBMedia * = new`，每次 callback 都 register | `PSBMediaRegistry.cpp:6-11` 一致且无退出清理对象 |
| EnsureContainer raw file 在 adaptor 认领前没有 RAII | `PSBMedia.cpp:19-49` 只在 Load false 显式 delete；null/异常边不补 delete |
| Open 返回 borrowed-resource memory stream | `PSBMedia.cpp:133-146` 直接 `new tTVPMemoryStream(data,size)` |
| 三只 member wrapper、native class 与 empty adaptor 走 ncbind 模板 | `ncbind.hpp:203-231,1325-1496,1853-1888` 保留 template object/registration 生命周期 |

逐行对照没有发现为了 Web 平台做的额外 ownership、STL 容器替换、shared_ptr 简化或失败
回收补丁。现有源码已经忠实保留目标的正常、异常和故意泄漏边界。

## IDB 纠正

fresh 复扫时发现 `CreateVariant_guess@0x59673C` 的旧函数注释仍引用非 Android 谱系作为
命名旁证。本轮已将其改成纯 Android 目标表述：目标有四个 direct caller，但没有可恢复的
精确私有 C++ identifier，因此继续保留 `_guess`。这只纠正 IDB 展示层，不改变二进制、
源码或 verdict。

## 机械门禁

`verify_elf_surface.py` 新增三层互相交叉的检查：

1. 20 个 allocation call 必须同时命中 exact allocator target、direct-argument row、
   direct-GPR call-result row 和声明的 first event；
2. 68 个生命周期锚点必须匹配 exact ARM64 word、normal/landing 分类，并由相应 allocation
   的正常 continuation 或指定 LSDA root 到达；8 条 cleanup edge 与 11 条 leak edge 另行
   验证路径；
3. 对五个 release target 全量扫描 114 个 FDE，实际站点集合必须和 120-site manifest
   完全相等，且每个站点的 normal/landing 分类不得变化。

新增通过输出：

```text
heap_lifecycle_surface=true allocation_owners=15 allocations=20 allocator_targets=3 families=12 policies=16 anchors=68 normal_anchors=59 landing_anchors=9 roles=50 exception_edges=8 exception_allocations=7
heap_leak_boundary_surface=true normal_paths=3 exception_edges=8 allocations=5 roles=8 cleanup_absence=true paths_complete=true
heap_release_surface=true owners=35 targets=5 families=5 sites=120 raw=80 shared=40 normal=83 landing=37 bl=114 tails=6 site_census_complete=true
```

完整 `verify_elf_surface.py` 门禁通过。由于没有 `cpp/` 修改，本轮按规则不构建；这不是跳过
验证，而是源码对照没有产生需要编译的差异。

最终判定：**ALIGNED / 无新增生产 GAP**。对象发布、转移、正常清理、constructor 异常
清理与所有权丢失五类边界均已从孤立调用点推进为可重复的跨表面生命周期契约。
