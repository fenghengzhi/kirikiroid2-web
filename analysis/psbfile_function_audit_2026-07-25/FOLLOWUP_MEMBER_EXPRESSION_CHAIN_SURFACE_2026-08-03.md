# Follow-up：完整 `cot_memptr/cot_memref` member-expression 链闭环

日期：2026-08-03

权威目标：Android ARM64 `reference/libkrkr2/libkrkr2.so`

SHA-256：`ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`

## 结论

- 完整 ECT1 中共有 **625 个 `cot_memptr/cot_memref` 节点**，覆盖 63 个 owner、
  35 种结果类型、39 种 base 类型与 99 种字段形状。
- 625 行精确分为 `427 cot_memptr + 198 cot_memref`、540 个 concrete 与 85 个
  optimizer-synthetic；全部绑定到 478 个 normal-entry exact-word anchor，没有
  landing-only 节点。
- 既有 typed-member read/write/synthetic 面有意只保留最外层 member expression；
  本轮证明其 419 个 concrete 唯一 signature 与 60 个 synthetic 唯一 signature 恰好
  等于 539 个 outer 节点的去重集合。旧门禁没有漏掉 outer expression。
- 真正新增的独立覆盖是 **86 个 nested base 节点**：81 个 concrete、5 个 synthetic。
  它们与 539 个 outer 节点组成六种完整字段链，最大 member 深度为三层。
- fresh Android ARM64 反编译确认 `owner -> header -> names/entries`、raw-node/PSBFile
  holder、wrapper/container、vector/COW 等代表字段链均能由当前源码逐层解释；没有生产
  `cpp/` GAP。本轮不改 `cpp/`、测试或 fixture，也不构建生产目标。

## 全量 census

| 维度 | 计数 |
| --- | ---: |
| owner | 63 |
| member 节点 | 625 |
| `cot_memptr` | 427 |
| `cot_memref` | 198 |
| concrete / synthetic | 540 / 85 |
| raw EA site | 409 |
| normal anchor | 478 |
| single / shared anchor | 354 / 124 |
| 每 anchor 最大节点数 | 4 |
| outer / nested | 539 / 86 |
| outer concrete / synthetic | 459 / 80 |
| nested concrete / synthetic | 81 / 5 |
| statement root | 36 |

child relation 精确分为：

```text
x=444 y=120 cond=36 arg:0=14 arg:1=5 callee=5 arg:2=1
```

直接父节点精确分为：

```text
asg=271 cast=82 memref=54 if=36 memptr=32 call=25 lnot=21 sub=21
ptr=16 ref=14 idx=13 add=12 preinc=10 eq=5 ne=4 land=3 band=2 bor=2
predec=1 slt=1
```

`memptr` 的 `ptrsize=8` 共 427 行，`memref` 的 `ptrsize=0` 共 198 行；恢复出的
reference width 为 `0/8/16/20/24/32/72` 七类。目标字段 offset 为：

```text
0:259 4:8 6:4 8:115 12:6 16:98 24:27 32:26 40:21 48:18
56:8 64:10 88:21 96:4
```

这些数字固定的是 Android ARM64 目标中被反编译器恢复的成员选择与链拓扑；它们只作为
语义归属和无遗漏证据，不要求 wasm32 复刻 ARM64 对象字节偏移，也不支持加入 padding。

## outer projection 与 86 个 nested 节点

既有 typed-member 系列从每个访问点提取最外层 typed expression，以便关联真实 load/store
和 producer/consumer。相同 `(owner,site,base type,member type,op,offset)` 可对应多只
ctree 节点，所以 459 个 concrete outer 节点去重为 419 个 signature，80 个 synthetic
outer 节点去重为 60 个 signature。新门禁双向断言：

```text
typed read/anchor/write/promotion concrete signatures == outer concrete signatures
typed synthetic signatures == outer synthetic signatures
```

因此“typed-member 只见 outer”是明确的投影边界，不是旧数据损坏。其代价是下列形式的
中间 base 过去没有独立行级门禁：

```text
rawNode.file.owner.header.names
rawNode.file.owner.header.entries
psbFile.owner.header.entries
wrapper.memberPointer.{ptr,adjustor}
vector.{begin,end,capacity}
```

本轮直接从 ECT1 枚举所有 member child，沿 `relation=x` 一直走到非 member terminal，
由此补齐 86 个 nested 节点，并拒绝缺 child、错误 relation、跨 owner parent 或链中断。

## 六种完整链形

链形按“最外层到最内层”记录：

| chain shape | root |
| --- | ---: |
| `memptr` | 320 |
| `memref` | 138 |
| `memref > memptr` | 54 |
| `memptr > memptr` | 16 |
| `memptr > memref` | 6 |
| `memptr > memptr > memptr` | 5 |
| **合计** | **539** |

member ancestor depth 精确为 `depth0=539 / depth1=81 / depth2=5`。539 条链最终落到
537 个 `cot_var` 与 2 个 `cot_call`；后两只来自 `vdupq_n_s64` 的 Hex-Rays SIMD
pseudo-member rendering，不是丢失的业务对象字段。

代表字段族包括：

- dispatch/raw node 经 holder/owner/header 到 `names` 或 `entries`；
- `PSBFile` 经 owner/header 到 root entries；
- NCB wrapper 的成员函数指针两字段、registration/adaptor 状态与 params functor；
- vector 的 begin/end/capacity、COW/`ttstr` data、media file/container；
- packed header、node、resource 与本地临时聚合的字段选择。

门禁固定 base type、结果 type、member op、offset、reference width 和完整 parent/child
关系；不会从恢复类型或 offset 猜测被 stripped 删除的精确字段名。

## fresh 反编译与关键逻辑摘要

本轮 fresh decompile 覆盖 `0x597854`、`0x598A3C`、`0x598C58`、`0x598D58`、
`0x59A4B0`。关键行为压缩为九行：

```text
PropGet: Dictionary 路径读取 self.value.owner.header.names，再以 self.value.node+1 查 value offset。
PropGet: 两次 packed lookup 均成功才用 node+1+offset 构造 Variant，否则继续 miss 路径。
GetRoot: 读取 self.owner.header.entries；复制 owner、执行 AddRef，再写返回 node；无 null guard。
Strict getter: 读取 self.file.owner.header.names，依次查 name/value；miss 调异常 helper。
Strict getter: hit 时 hidden-sret 得到 retained owner 与 node+1+offset。
Try getter: 同一两级 lookup；miss 返回 false，不碰输出。
Try getter: hit 时先释放输出旧 owner，再复制/retain source owner，最后写 child node。
Resolve: 从 native holder 取得 file，再以 file.owner.header.entries 构造局部 current。
Resolve: 逐段替换局部 current；只有最终成功才把 retained current 提交给调用方输出。
```

## 本地实现逐行对照

| Android ARM64 字段链/数据流 | 当前源码对照 |
| --- | --- |
| `PropGet@0x597854` 的 value→owner→header→names 与 value→node | [`main.cpp:121-190`](../../cpp/plugins/psbfile/main.cpp) 保留 owner/header/name trie 与 node/value-offset 两条链，并只在双 lookup 成功后构造 Variant |
| `GetDictionaryValueStrict@0x598C58` 的两级 lookup、throw/fallback 与 retained result | [`PSBRawFile.cpp:248-266`](../../cpp/plugins/psbfile/PSBRawFile.cpp) 保留相同 names/node 链、miss 异常与返回构造 |
| `GetDictionaryValue@0x598D58` 的 release-old→copy/retain→write-node 顺序 | [`PSBRawFile.cpp:220-240`](../../cpp/plugins/psbfile/PSBRawFile.cpp) 保留 alias-sensitive assignment 顺序，未加 self guard |
| `PSBFile::GetRoot@0x598A3C` 的 owner→header→entries 且无 null guard | [`PSBRawFile.cpp:542-546`](../../cpp/plugins/psbfile/PSBRawFile.cpp) 直接以 `owner_->GetHeader()->entries` 构造 raw node |
| `Resolve@0x59A4B0` 的 native file→owner→header→entries、局部 current 与延迟提交 | [`PSBMedia.cpp:52-109`](../../cpp/plugins/psbfile/PSBMedia.cpp) 保留相同根节点链、逐段替换和最终 `value = current` |
| holder/owner/header/node 的源码对象层次 | [`PSBRawFile.h:35-70`](../../cpp/plugins/psbfile/PSBRawFile.h)、[`PSBRawFile.h:75-120`](../../cpp/plugins/psbfile/PSBRawFile.h)、[`PSBRawFile.h:122-184`](../../cpp/plugins/psbfile/PSBRawFile.h) 分别保留 owner/header、单 owner holder 与 holder+node 结构 |

五个代表函数的条件分支、默认/失败行为、retain/release 次序与输出提交时点均已有直接
Android 证据。当前源码逐层保留相同对象关系；没有依据把目标 ABI offset 写入 C++。

## 机械门禁

member canonical semantic sequence 为 104,818 bytes，SHA-256：

```text
6b30ff6385ed09134769f3753e93569c2c9152623f02a18bb00319e2d3244993
```

每一行固定：

```text
owner / ordinal / statement / parent / parent-op / relation / op / result-type /
detail(offset,ptrsize,refwidth) / depth / exflags / concrete / raw-EA / anchor /
exact-word / outer-or-nested / member-ancestor-depth / base-op / base-type
```

`verify_elf_surface.py` 固定输出：

```text
member_expression_surface=true owners=63 rows=625 roots=36 ops=2 relations=7 types=35 details=45 base_types=39 field_shapes=99 concrete=540 synthetic=85 raw_sites=409 anchors=478 normal=478 landing=0 single=354 shared=124 max=4 outer=539 nested=86 outer_concrete=459 outer_synthetic=80 nested_concrete=81 nested_synthetic=5 chain_roots=539 chain_shapes=6 max_chain_depth=3 terminal_var=537 terminal_call=2 offsets=14 reference_widths=7 typed_ea_signatures=419 typed_synthetic_signatures=60 semantic_bytes=104818 semantic_sha256=true outer_typed_cross=true nested_chains_complete=true paths_complete=true
```

完整 ELF verifier 继续通过；114-entry 总判定仍为
`ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`。本面固定的是全部最外层与嵌套 member
表达式及其机器 realization/typed projection 关系，不把 ARM64 ABI layout、Hex-Rays
pseudo-member 或 stripped identifier 反向制造成 portable C++ token。
