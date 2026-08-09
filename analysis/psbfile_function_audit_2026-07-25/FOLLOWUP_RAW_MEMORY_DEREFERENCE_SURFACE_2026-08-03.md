# 裸内存解引用与 typed-member 增量闭环（2026-08-03）

权威目标：Android ARM64 `reference/libkrkr2/libkrkr2.so`

SHA-256：`ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`

## 结论与旧统计纠正

本轮继续只以当前 Android ARM64 目标与 fresh IDA ctree 为权威，完整枚举 114 个
MANIFEST 函数中的 `cot_ptr/cot_idx`，并把每一行闭合到目标 ELF 的具体机器实现。

- 旧 typed-member 报告中的 `483 = 416 EA-backed + 67 synthetic` 是当时 IDB 类型状态下
  的**精确旧基线**，不是最终完整表面；后续裸访问复扫又识别出 5 条 read-only
  typed-member 语义行。当前总数为 **488 = 421 EA-backed + 67 synthetic**，模式为
  `R=317 / W=147 / RW=10 / address=14`，唯一 EA 站点由 385 增为 390。
- 当前仍为 raw expression 的完整表面是 **667 行**：
  `cot_ptr=461 / cot_idx=206`，按模式分为
  `R=407 / W=111 / RW=5 / address=144`。
- 667 行中 496 行自带 EA，171 行为 optimizer-synthetic；它们分别形成 475 个 EA 站点、
  169 个 synthetic anchor 站点，合并后为 **610 个唯一实现锚点**。
- 610/610 锚点都属于相应 MANIFEST owner FDE，ARM64 word 与清单一致，并且全部从函数
  正常入口可达；landing-only 为 0。554 个锚点承载 1 行，56 个共享锚点承载 2–3 行。
- fresh 反编译和本地逐段对照没有发现生产代码 GAP；本轮不修改 `cpp/`、fixture 或测试
  物料，因此不触发构建。

旧报告曾记录 `cot_ptr=480`。该数字只覆盖当时类型状态下的一类裸表达式；本轮补型会把
原先的 raw row 提升成 typed-member，而新增的 `cot_idx` 全面枚举又扩展了审计范围。
因此 `480` 与当前 `461+206` 不是同口径增减，不能相减解释为机器访问消失。

## 新增的 5 条 typed-member 语义

本轮只修正 IDB 局部类型，没有按本地源码反推类型：每条提升都同时由 field load 和其首个
语义消费者证明。

| owner | typed read | producer | 首个语义消费者 | 约束 |
| --- | --- | --- | --- | --- |
| `Transfer_guess@0x598A64` | `owner->refCount@0` | `LDR W8,[X20] @0x598A80` | `CBNZ W8 @0x598A84` | incoming-zero 删除边界 |
| `Transfer_guess@0x598A64` | `owner->data@88` | `LDR X0,[X20,#88] @0x598A88` | `BL @0x598A8C` | raw allocation dealloc 参数 |
| `Resolve_guess@0x59A4B0` | `nativeFileHolder->owner@0` | `LDR X20,[X8] @0x59A53C` | `LDR X8,[X20,#8] @0x59A540` | `PSBFile → owner → header` 链 |
| `Resolve_guess@0x59A4B0` | `oldCurrentOwner->data@88` | `LDR X0,[X22,#88] @0x59A6B0` | `BL @0x59A6B4` | 旧 current owner 终结释放 |
| `Resolve_guess@0x59A4B0` | `nextOwnerForCleanup->data@88` | `LDR X0,[X22,#88] @0x59A6E0` | `BL @0x59A6E4` | 新临时 owner 的 incoming-zero 清理 |

原 483 行 canonical payload 保持原样，作为可复现历史基线；验证器另加这 5 行 promotion
contract，要求 producer/consumer exact word、owner FDE 与 normal-CFG 可达性同时成立。
这样不会为了更新汇总数而重写已经闭合的 67 行 synthetic manifest。

## fresh 反编译证据

本轮 fresh decompile：

- `PSBFile_Transfer_guess@0x598A64`；
- `PSBMedia_Resolve_guess@0x59A4B0`；
- `PSBFile_ncbRegistNativeClass_RegistItem_guess@0x59AEEC`；
- `PSBRawNode_GetInt_guess@0x599438`。

目标高风险对象流摘要不超过十行：

```text
Transfer: result.owner = self.owner; if result.owner != null: ++result.owner.refCount
Transfer: old = self.owner; self.owner = null
Transfer: if old != null && old.refCount == 0: dealloc(old.data); delete old
Resolve: dispatch = fileVariant.AsObjectNoAddRef(); native = GetNativeInstance(dispatch)
Resolve: current.owner = native.owner; current.node = current.owner.header.entries
Resolve: split path; for each segment require ContainsDictionaryKey(segment)
Resolve: next = current.GetDictionaryValueStrict(segment); release prior temporary owner
Resolve: only at successful final segment assign current to caller output and return true
Resolve: every miss returns false without changing caller output
```

`GetInt_guess@0x599438` 的 tag `0x0C` 路径在反编译文本中一度看似通过 raw pointer 读取
64 位数；对 ctree、寄存器定义和指令重新交叉后确认它只是同一寄存器被复用于
`node + 1` 的 unaligned scalar load，不是 `PSBRawNode` 的额外指针字段。当前
`ReadUnaligned_guess<tjs_int64>(node + 1)` 不需要修改。

`RegistItem_guess@0x59AEEC` 的 UTF-16 `L")"` synthetic 地址表达式没有独立 EA；其最近
真实实现分别是 `ADRL@0x59AFC4/0x59AFF0`。这两条与其余 synthetic 行一样保留为
“语义行 → 真实锚点”，没有伪造 member EA。

## IDB 类型纠正

根据上述 field/consumer 正证据持久修正了四个局部：

- `Transfer@0x598A64`：`owner` → `PSBRawOwner *`；
- `Resolve@0x59A4B0`：`oldCurrentOwner` → `PSBRawOwner *`；
- `Resolve@0x59A4B0`：`nextOwnerForCleanup` → `PSBRawOwner *`；
- `Resolve@0x59A4B0`：`nativeFileHolder` → `PSBFile *`。

补型后的 fresh 反编译直接出现 `owner->refCount/data`、
`nativeFileHolder->owner->header->entries` 与两条 cleanup `owner->data`。这纠正的是 IDB
展示层，不代表二进制或本地源码新增字段。

## 667 行 raw surface 的语义族

| raw 访问族 | 目标机器形状 | 本地源码对照 |
| --- | --- | --- |
| packed PSB byte stream | `LDRB/LDRH/LDRSB/LDUR*`、unaligned word、tag/index 地址计算 | `PSBPackedInternal.h:18-23,103-180,207-259` 保留 unaligned scalar decoder、packed width/count 与 `PsbArray_guess` |
| header/node/resource 中间指针 | owner/header/node 与 strings/chunk/entries 的 load/address chain | `PSBRawFile.h:17-70,77-115`、`PSBRawFile.cpp:137-174,309-440` 保留 holder/header/node、引用计数和资源路径 |
| media output/local aggregate | local `PSBRawNode`、成功尾部 output commit、失败保持 out | `PSBMedia.cpp:52-109` 保留 native holder、root local、逐段严格 lookup 与成功后赋值 |
| TJS/stream/vtable/NCB ABI | virtual slot、member-pointer、manager/invoker 与 callback 参数间接访问 | `PSBDispatch.h`、`main.cpp` 与 `cpp/core/plugin/ncbind.hpp` 保留双 vptr、wrapper/adaptor/functor 分层 |
| COW string/vector/STL | COW refcount、iterator/end/capacity、聚合 load/store | `PSBRawFile.cpp:280-306` 与现有 `ttstr/std::string/std::vector` 生命周期一致 |
| 编译器/runtime | stack canary、栈输出槽、UTF-16 地址物化 | 只作为 ARM64 编译实现证据，不反写成 C++ 对象字段 |

raw ctree 的直接父 op 分布为：

```text
None=47 add=24 asg=312 band=49 bor=9 call=33 cast=25 eq=4
idx=31 land=1 lnot=3 ne=1 postdec=5 ptr=6 ref=110 sub=7
```

决定访问模式的祖先只有 `asg=404 / ref=144 / postdec=5 / None=114`。这项独立记录能区分
“某个裸表达式出现在 assignment 子树里”与“它本身是写目标”，防止只看最近父节点把
address-use 或读改写误分类。

25 类实现 mnemonic 的 occurrence 为：

```text
ADD=80 ADRL=6 BFI=11 BR=38 CBZ=9 CSEL=6 DUP=4 FMOV=5 INS=7 LDP=4
LDR=171 LDRB=66 LDRH=18 LDRSB=8 LDUR=65 LDURB=12 LDURH=15 LDURSH=5
MOV=2 STP=24 STR=94 STRB=6 STRH=1 STUR=6 SUB=4
```

24 个 raw realization anchor 也属于当前 390-site typed-member 集；这是同一机器指令同时
承载外层 typed member 与内层 raw dereference 的正常交集，不是重复计数或类型冲突。

## 机械门禁

`verify_elf_surface.py` 新增 `RMC2` canonical payload。每行固定：

```text
owner + owner-local ordinal + cot_ptr/cot_idx + access mode
+ immediate parent + mode-defining ancestor + concrete/synthetic EA
+ realization anchor + mnemonic class + exact ARM64 word
```

payload 机器部分为 23,383 bytes，SHA-256：
`b79801b8f2074cdc64f5e018d82f6099297f0cb6d6c6ac3e17e7ea3f9a8107f1`。
完整 expression/result/base type 文本序列为 85,201 bytes，SHA-256：
`096bb010db939c2b0ed6bf75a5abfaf5a010583da3cb51e385f849e1c7374034`；该 digest 作为
payload 尾部绑定机器清单，任何语义或顺序漂移都会失败。

新增通过输出：

```text
typed_member_promotion_surface=true owners=2 sites=5 consumers=5 normal=5 landing=0 r=5 roles=3 zero_ref_guard=1 dealloc_argument=3 owner_header_chain=1 legacy=483 promoted=5 total=488 instruction_words=true paths_complete=true
raw_memory_surface=true owners=56 rows=667 ptr=461 idx=206 r=407 w=111 rw=5 address=144 ea_backed=496 synthetic=171 ea_sites=475 synthetic_sites=169 anchors=610 normal=610 landing=0 single=554 shared=56 max=3 typed_intersection=24 mnemonics=25 parents=16 mode_parents=4 semantic_bytes=85201 semantic_sha256=true instruction_words=true paths_complete=true
```

## 本地逐段结论

1. `Transfer` 的 copy/AddRef、source clear、incoming-zero cleanup 与 owner raw allocation
   释放顺序已经由 `PSBFile` Rule-of-Three 表达复刻；新增 typed rows 只让这条链在 IDA 中
   显式可读。
2. `Resolve` 已从 Variant object 经 adaptor 取得 `PSBFile *`，再走
   `owner → header → entries` 构造 root；逐段 lookup、临时 owner 清理与成功尾部 output
   commit 都与目标一致。
3. packed byte/index 访问保留 unaligned read、W32 index product、width gate 与
   `PsbArray_guess` 四字段源码结构；没有把它们误收编成对象字段。
4. TJS/stream/vtable、COW/STL 与 stack/runtime 裸访问均有现存源码层或明确编译器边界；
   没有发现缺失容器、隐藏 owner、额外缓存或生命周期 workaround。

因此本轮总判定为 **ALIGNED / 无新增生产 GAP**。这不是因为 raw 访问“不可观察”而跳过，
而是每一行都已经由 fresh ctree 语义、exact ARM64 word、正常 CFG 锚点和本地逐段实现共同
闭合。
