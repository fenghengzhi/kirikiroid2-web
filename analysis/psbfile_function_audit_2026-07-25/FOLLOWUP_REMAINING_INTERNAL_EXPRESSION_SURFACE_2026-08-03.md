# Follow-up：剩余完整 internal expression 六族闭环

日期：2026-08-03

权威目标：Android ARM64 `reference/libkrkr2/libkrkr2.so`

SHA-256：`ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`

## 结论

- 完整 ECT1 的 4,800 个 internal expression 中，call/member/raw-memory/assignment 四个
  dedicated projection 已覆盖 2,820 行；本轮把其余 **1,980 行**全部闭合。
- 1,980 行覆盖 74 个 owner、30 种 op、90 种结果类型、94 种有序 child shape 与
  1,352 个 normal-entry exact-word anchor；没有 landing-only 或无 child 的行。
- 六族精确分为 `744 arithmetic + 689 cast + 257 predicate + 252 ref + 36 mutation +
  2 comma = 1980`。1,658 行 concrete、322 行 optimizer-synthetic。
- 252 个 address-of operand 与 LVS1、RMC2、typed-member、`cot_obj` 四个独立投影闭合；
  36 个 mutation target 与 local/member/raw 三个 RMW 投影闭合。差异仅保留为已有 semantic
  signature 的机器 realization 层次差异，不制造新源码字段或语句。
- fresh Android ARM64 反编译核对 native-instance 地址暴露、numeric cast、media refcount、
  packed vector/name 与 resource lookup 代表链；当前源码逐项可解释，没有生产 `cpp/`
  GAP。本轮不改 `cpp/`、测试或 fixture，也不构建生产目标。

## 完整 internal partition

ECT1 以“有直接 child”为 internal 判据。五个集合互斥且并集恰为全部 internal 节点：

| projection | op | row |
| --- | --- | ---: |
| call | `cot_call` | 405 |
| member | `cot_memptr/cot_memref` | 625 |
| raw memory | `cot_ptr/cot_idx` | 667 |
| assignment | `cot_asg` | 1,123 |
| 本轮 remainder | 六族 30 种 op | 1,980 |
| **合计** | **全部 internal op** | **4,800** |

```text
405 + 625 + 667 + 1123 + 1980 = 4800
```

这项等式是逐 `(owner, expression ordinal)` 的集合相等，不是只比较总数。与既有
`4829 leaf + 4800 internal = 9629` 一起，完整 expression tree 现在既按 leaf/internal，
又按全部 internal operator family 双重闭合。

## 全量 census

| 维度 | 数量 |
| --- | ---: |
| owner | 74 |
| row | 1,980 |
| statement root / nested | 240 / 1,740 |
| family / op | 6 / 30 |
| relation / parent op | 14 / 29 |
| result type / child type | 90 / 90 |
| child shape | 94 |
| concrete / synthetic | 1,658 / 322 |
| raw EA site | 1,257 |
| normal anchor | 1,352 |
| single / shared anchor | 875 / 477 |
| 每 anchor 最大 row | 7 |
| 最大 expression 深度 | 10 |

anchor cardinality 精确为：

```text
1 row=875 anchors
2 rows=372 anchors
3 rows=70 anchors
4 rows=28 anchors
5 rows=5 anchors
7 rows=2 anchors
```

expression flags 为 `0:112 / 1:1729 / 33:139`。所有 concrete row 都满足
`raw_ea == anchor`；synthetic row 只继承 nearest physical anchor，不获得虚构地址。

## 六族与 30 种 operator

| family | concrete | synthetic | 合计 |
| --- | ---: | ---: | ---: |
| arithmetic | 704 | 40 | 744 |
| cast | 478 | 211 | 689 |
| predicate | 252 | 5 | 257 |
| ref | 189 | 63 | 252 |
| mutation | 33 | 3 | 36 |
| comma | 2 | 0 | 2 |
| **合计** | **1,658** | **322** | **1,980** |

30 种 op 的完整计数为：

```text
add=356 asgadd=5 band=81 bnot=2 bor=14 cast=689 comma=2 eq=32
land=25 lnot=79 lor=11 mul=57 ne=35 postdec=6 postinc=3 predec=1
preinc=21 ref=252 sge=17 sgt=5 shl=13 sle=4 slt=6 sshr=6 sub=181
uge=7 ugt=30 ult=6 ushr=32 xor=2
```

unary operator 固定一个 `x` child；binary operator 固定有序 `x/y` 两个 child。14 种
owner-local relation 为：

```text
arg:0=131 arg:1=44 arg:2=32 arg:3=14 arg:4=1 arg:6=1 callee=5
cond=197 expr=26 return=15 selector=1 step=1 x=954 y=558
```

29 种直接 parent 为：

```text
add=141 asg=350 band=42 bor=16 call=228 cast=316 comma=2 do=10 eq=11
expr=26 for=1 idx=40 if=187 land=35 lnot=5 lor=21 mul=48 ne=19 ptr=376
return=15 shl=13 sshr=2 sub=26 switch=1 uge=1 ugt=17 ult=2 ushr=27 xor=2
```

这些 topology 计数固定的是恢复出的有序表达式树；它们不把 ARM64 register allocation、
被 O3 删除的临时名或 decompiler factorization 宣称为唯一原始拼写。

## cast：固定精确矩阵，不从类型名猜语义

689 个 `cot_cast` 形成：

```text
unique source type=55
unique destination type=72
unique ordered source->destination pair=187
```

门禁逐行保存 source child 的 op/type/detail/flags/realization/EA/anchor/word 与 destination
type，同时对 187 个有序 type pair 做集合固定。这里的类型字符串是 Hex-Rays 对目标 ABI
的 recovered type evidence；审计不会据 `unsigned int`、pointer spelling 或临时聚合名
推导额外源码 API、强制 cast token 或 wasm32 对象布局。代表 numeric conversions 由
`GetDouble@0x5992E8` 的 tag switch 独立确认，而不是从类型名反推。

## ref：四路 address-of 交叉

252 个 `cot_ref` 的直接 operand 精确分为：

| operand | concrete | synthetic | 合计 | 独立交叉 |
| --- | ---: | ---: | ---: | --- |
| local `var` | 50 | 33 | 83 | LVS1 全部 `mode=address` |
| raw `idx` root | 0 | 110 | 110 | RMC2 address root |
| member `memptr` | 11 | 3 | 14 | typed-member 全部 address signature/occurrence |
| global/static `obj` | 7 | 38 | 45 | ECT1 全部 `obj parent=ref` |
| **合计** | **68** | **184** | **252** | **四路闭合** |

RMC2 的完整 address 集合还有 34 个位于上述 110 个 `idx` operand 内部的 raw descendant：

```text
ptr under idx/ref=30
ptr under band/ref=4
110 roots + 34 descendants = 144 RMC2 address rows
```

因此 `ref operand=110` 与 `raw address rows=144` 同时成立：后者多出的 34 行是同一地址
表达式内部的 base/mask 计算，不是额外 34 个 `cot_ref`。member 侧的 11 个 concrete 与
3 个 synthetic occurrence 都与 typed-member address 面双向相等，没有 ECT-only 差集。

## mutation：三路 RMW 交叉

36 个 mutation operator 的完整 target shape 为：

```text
asgadd(var,var)=5
postdec(ptr)=5
postdec(var)=1
postinc(var)=3
predec(memptr)=1
preinc(memptr)=10
preinc(var)=11
```

按目标家族重新分区：

| target | row | 独立交叉 |
| --- | ---: | --- |
| local | 20 | LVS1 全部 `mode=read-write` |
| raw memory | 5 | RMC2 全部 `mode=RW`，均为 `postdec(ptr)` |
| typed member | 11 | 3 concrete + 8 synthetic member occurrence |
| **合计** | **36** | **三路闭合** |

8 个 synthetic member occurrence 去重为 7 个 signature；既有 typed-member synthetic
面固定其中 7 个机器 occurrence。ECT1 唯一额外 realization 为：

```text
owner=0x59A4B0 base=PSBRawOwner * member=unsigned int op=memptr offset=0
anchor=0x59A76C word=0xB9000109
```

它仍是已知 owner-refcount `preinc` signature，不是第八种字段语义。新门禁明确要求旧
7 occurrence 是新 8 occurrence 的真子集，差集只能是这一项。

## arithmetic、predicate 与 comma

arithmetic 族覆盖 add/sub/mul/shift/bitwise 的全部 744 行；predicate 族覆盖 equality、
signed/unsigned comparison 与 logical conjunction/negation 的全部 257 行。它们的 child
次序、parent/relation、type、exact ARM64 word 和 normal reachability 已逐行纳入 semantic
sequence；branch 是否 materialize、比较是否与 conditional branch 合并仍由既有 normal
predicate/NZCV 面独立约束。

两只 concrete `cot_comma` 分别位于：

```text
DecodeName@0x597B1C：packed width/value assignment 后继续比较/索引
GetResourceData@0x59A0B4：owner/header assignment 后继续 chunkData 判定
```

它们各自包含 assignment child，证明目标恢复出的 value-producing comma 数据流不能在
expression census 中扁平化；当前普通 C++ 的命名局部与顺序表达可以生成同一目标结构，
不需要把反编译临时名或寄存器复用抄进源码。

## fresh 反编译与本地逐行对照

本轮 fresh decompile 覆盖 `0x596D90`、`0x597B1C`、`0x5992E8`、`0x599878`、
`0x599888`、`0x59A0B4`。关键行为压缩为六行：

```text
NativeInstanceSupport: flag!=2 -> -1002；懒注册 class id；id 不同 -> -1；匹配时 *out=&secondary-vtable，返回 0。
DecodeName: 解三只 packed array；沿 parent 取字符并 push；reverse；assign(begin,size) 到输出 string。
GetDouble: tag 2/3/1D 给 1/0，4..8 与 9..C 分别按有符号整数转 double，1E/1F 读 float/double；其余 diagnostic 后 0。
AddRef: ++self->_ref。
Release: _ref==1 时 deleting destructor；否则 --_ref。
GetResourceData: Resolve 失败返回 null；成功后由 raw node 的 owner/header/chunk tables 取 offset/length 并交付资源指针。
```

| Android ARM64 数据流 | 当前源码对照 |
| --- | --- |
| `NativeInstanceSupport@0x596D90` 的 flag/id 谓词与 secondary-vtable address-of | [`main.cpp:455-471`](../../cpp/plugins/psbfile/main.cpp) 保留相同三路返回；`static_cast<iTJSNativeInstance *>(this)` 生成 secondary base 地址 |
| `DecodeName@0x597B1C` 的 packed arithmetic、vector mutation、reverse/assign | [`PSBRawFile.cpp:112-134`](../../cpp/plugins/psbfile/PSBRawFile.cpp) 保留三只 array view、parent loop、`push_back`、`reverse`、`assign` 顺序 |
| `GetDouble@0x5992E8` 的 cast/tag/default | [`PSBPackedInternal.h:154-187`](../../cpp/plugins/psbfile/PSBPackedInternal.h) 保留完整 tag switch；[`PSBRawFile.cpp:360-364`](../../cpp/plugins/psbfile/PSBRawFile.cpp) 保留 source-level helper boundary |
| `PSBMedia::AddRef@0x599878` / `Release@0x599888` | [`PSBMedia.h:12-22`](../../cpp/plugins/psbfile/PSBMedia.h) 保留初值 1、increment、`==1` delete/else decrement；void AddRef 的前/后缀 token 不由目标唯一化 |
| `GetResourceData@0x59A0B4` 的 Resolve 与 inlined raw-resource chain | [`PSBMedia.cpp:112-124`](../../cpp/plugins/psbfile/PSBMedia.cpp) 保留 source-level `Resolve` 后 `value.GetResource(size)` 调用边界 |

这六组代表覆盖本面的 address-of、cast、predicate、arithmetic、mutation 与 comma 数据流。
当前实现逐项可由 Android 证据解释；没有依据增加 ABI padding、Hex-Rays pseudo-lvalue、
显式 comma 拼写或只属于机器寄存器的数据成员。

## 机械门禁

remaining-internal canonical semantic sequence 为 453,674 bytes，SHA-256：

```text
d1a6b79bc7228101cdbbe627218c7a96c781fad33d847c77c47d91a27276ca65
```

每一行固定：

```text
owner/ordinal/statement/parent/parent-op/relation/family/op/type/detail/depth/
exflags/concrete/raw-EA/anchor/exact-word/direct-child-count +
每个有序 child 的 ordinal/relation/op/type/detail/exflags/concrete/raw-EA/anchor/exact-word
```

`verify_elf_surface.py` 固定输出：

```text
remaining_internal_expression_surface=true owners=74 rows=1980 roots=240 nested=1740 families=6 ops=30 relations=14 parents=29 types=90 child_types=90 child_shapes=94 concrete=1658 synthetic=322 raw_sites=1257 anchors=1352 normal=1352 landing=0 single=875 shared=477 max=7 max_depth=10 cast_pairs=187 cast_source_types=55 cast_dest_types=72 ref_ops=4 ref_lvars=83 ref_raw_roots=110 ref_raw_descendants=34 ref_member_concrete=11 ref_member_synthetic=3 ref_objects=45 mutation_targets=3 mutation_lvars=20 mutation_raw=5 mutation_member_concrete=3 mutation_member_synthetic=8 mutation_member_signatures=7 mutation_member_typed_occurrences=7 mutation_member_ect_only=1 internal_rows=4800 dedicated_rows=2820 semantic_bytes=453674 semantic_sha256=true cast_matrix=true ref_cross=true mutation_cross=true internal_partition=true paths_complete=true
```

完整 ELF verifier 继续通过；114-entry 总判定仍为
`ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`。本面固定的是所有未被 dedicated
projection 单列的 internal operator、有序 child 与跨表面地址/RMW 归属；它不把
optimizer-synthetic 节点、recovered type spelling 或目标 ABI lowering 反向制造成原始
C++ token。
