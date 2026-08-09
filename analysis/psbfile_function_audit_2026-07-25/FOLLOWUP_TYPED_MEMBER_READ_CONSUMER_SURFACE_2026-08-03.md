# typed-member 读值 / 取址 consumer 表面复核（2026-08-03）

## 结论

本轮继续只以 Android ARM64 `libkrkr2.so` 和 IDA 中由目标访问链修复出的类型为
权威，将 typed-member 表面里带真实 EA 的全部 `R/RW/address` 语义行闭合到机器
producer lane、第一消费者或 residual anchor 的来源。

> 后续统计说明：本报告闭合的是 483-row 旧基线中的 instruction-backed 读/取址侧。
> 完整 raw surface 复扫随后提升 5 条 read-only typed rows，并以独立 producer/consumer
> contract 验证；当前 typed-member 总数为 488，详见
> [FOLLOWUP_RAW_MEMORY_DEREFERENCE_SURFACE_2026-08-03.md](FOLLOWUP_RAW_MEMORY_DEREFERENCE_SURFACE_2026-08-03.md)。

结果为 **ALIGNED / 无生产代码 GAP**：

- 311 条语义事件，落在 61 个 owner FDE 的 290 个机器站点；
- 267 条 direct-producer 语义行展开为 268 个物理 lane，形成 288 条 first-event
  relation；
- 44 条 residual-anchor 语义行展开为 45 个物理 lane，形成 45 条单来源关系；
- 311 条语义事件全部从 owner 正常入口可达，landing-only 为 0；
- pre-event loop、未分类终点、未声明 volatile-call clobber 均为 0。

本轮没有修改 `cpp/`、fixture 或测试物料，因此不触发构建。

## 覆盖边界

基线仍是
[FOLLOWUP_TYPED_MEMBER_FIELD_SURFACE_2026-08-03.md](FOLLOWUP_TYPED_MEMBER_FIELD_SURFACE_2026-08-03.md)
固定的 483 条旧基线 ctree member 语义行：

```text
R=312 / W=147 / RW=10 / address=14
```

本轮选择其中带真实 EA 的读/取址部分：

```text
R=297 / RW=3 / address=11
semantic events=311 / unique machine sites=290 / owner FDEs=61
```

其余 25 条读/取址行是 optimizer-synthetic expression：

```text
R=15 / RW=7 / address=3
```

它们继续保留在原 typed-member 语义统计中，但没有一一对应的机器地址，故本轮不伪造
producer 或 consumer site。写侧的 108 条 EA-backed `W/RW` 已由
[FOLLOWUP_TYPED_MEMBER_WRITE_VALUE_PRODUCER_SURFACE_2026-08-03.md](FOLLOWUP_TYPED_MEMBER_WRITE_VALUE_PRODUCER_SURFACE_2026-08-03.md)
独立闭合。

## direct producer 物理形状

267 条 direct 语义行中，一条 aggregate `LDP` 同时表达两个 qword，故展开为 268 个
物理 lane：

| producer | lane | 宽度/用途 |
| --- | ---: | --- |
| `LDR` | 189 | `W=24 / X=165`，普通或带 writeback 的字段读取 |
| `LDRB` | 22 | `W` byte 读取 |
| `LDRH` | 2 | `W` halfword 读取 |
| `LDUR` | 6 | `W` unscaled 读取 |
| `LDURH` | 1 | `W` unscaled halfword 读取 |
| `LDP` | 38 | 19 个站点的 operand 0/1，均为 `X` |
| `ADD` | 10 | inline member / wrapper 字段取址，均为 `X` |
| **合计** | **268** | **W=55 / X=213** |

访问模式按 lane 为：

```text
R=256 / address=12
memptr=215 / memref=53
```

清单同时固定 33 个 base-type 族、32 个 member-type 族、语义 field offset、物理
register bank、producer operand、width 和 exact instruction word。ARM64 offset 只进入
分析清单，不反写成 wasm32 padding/packing。

## 第一消费者全集

268 个 producer lane 形成：

```text
257 × 1 first event
  8 × 2 first events
  3 × 5 first events
= 288 relations
```

角色分布为：

| first-event 角色 | 关系 | 含义 |
| --- | ---: | --- |
| `use` | 196 | 当前值被读取，且该指令不改写同一物理 bank |
| `transform` | 46 | 当前值既被读取又在同一 bank 中更新 |
| `call-use` | 37 | 同站点同 bank 出现在既有 direct/indirect ABI argument 清单 |
| `call-boundary` | 6 | 易失 bank 在未用作参数的后续 call 处失效 |
| `terminal` | 3 | 对应路径直接抵达 `RET` |

`call-use` 不是根据“后面恰好有 BL/BLR”推断：验证器复用完整 direct/indirect
argument manifest，逐条要求 `(owner, call, physical bank)` 三元组存在。其余 call
对 `X0..X18` 是 volatility boundary，对 `X19..X28` 则继续追踪。

物理 bank 分布为：

```text
X0=43 X1=5 X2=3 X3=2 X8=115 X9=32 X10=10 X11=9 X12=1 X16=2
X19=6 X20=16 X21=12 X22=6 X23=2 X24=2 X25=1 X26=1
```

## 多分支高风险关系

11 个多 first-event lane 均由完整 normal CFG 分支集合产生：

1. `DecodeName@0x596BC4` 的 `LDP@0x596BD8` 同时读取
   `header->strings` 与 `header->stringsData`；后者 `X8` 的两条 first use 是
   `ADD@0x596C5C/0x596C68`。
2. `PSBFile::Load@0x598268` 的 `filter.targetStorage` 由
   `LDR@0x598414` 进入 `X21`，并在两条正常分支分别先抵达
   `MOV@0x5982D0/0x5982E8`。
3. `PSBMedia::GetResourceData@0x59A0B4` 的 `out.owner` 有两个 first use；
   三种 payload 宽度读取产生的 `X11` 各有五个分支终点：
   `MUL` use、`MUL` transform、两个易失 `BL` boundary 或 `RET`。
4. `Resolve@0x59A4B0` 的 `ADD@0x59A4E0` 物化 `self->file` 地址；
   first event 为 `LDR@0x59A4EC` transform 或 `BL@0x59A530` ABI use。
5. root/load wrapper 的 Itanium member pointer 通过
   `LDP@0x59B4B0/0x59B610` 读取 function-or-vtable offset 与 this-adjust/virtual
   flag；函数 lane 在 vtable `LDR` transform 或 `BLR` target use 处分流，adjust
   lane 则先进入 `ADD`。

这些关系固定了 packed table、OwnerFilter、media raw-resource clone 和 ncbind
member-pointer 的中间数据流，不能用“最后结果相同”替换其计算层次。

## residual anchor 与来源

44 条不能把 ctree member expression 直接归为 load/address producer 的语义行展开为
45 个 anchor lane：

| anchor 类别 | lane | 实际消费点 |
| --- | ---: | --- |
| predicate-register | 27 | `CBZ/CBNZ` 的显式 register operand |
| predicate-NZCV | 2 | member 值先进入 `CMP`，语义站点为后续 `B.EQ/CSET` |
| store-anchored | 12 | member read 作为 store operand；一只 `STP` 展开两个 lane |
| transfer-anchored | 2 | member read 作为 transfer source operand |
| compiler SIMD pseudo-member | 2 | `vdupq_n_s64(0).n128_u64`，物理来源为 `XZR` |

45 条来源全部单一：

```text
instruction=42 / architectural zero=3
LDR=35 MOV=3 ADD=2 LDP=1 SUBS=1 ZERO=3
entry=0 / call-return=0 / call-clobber=0
```

两个 NZCV anchor 的映射被单独固定：

- `B.EQ@0x599E70` 实际消费 `CMP@0x599E6C` 的 operand 1 / `X0`；
- `CSET@0x599E80` 实际消费 `CMP@0x599E7C` 的 operand 0 / `X0`；
- 两个 `X0` 都由 `requestedContainer.storage` 的
  `LDR@0x599E68` 唯一提供。

反向路径从 actual consumer 的每个 normal predecessor 出发，必须先命中清单中的 exact
writer；若先命中另一条已证明 writer、易失 call boundary 或无来源入口，验证立即失败。

## fresh 反编译证据

本轮 fresh decompile 复核五个覆盖不同对象族的高风险函数：

- `PSBFile_Load@0x598268`：String/Octet 类型门、MDF size 更新、copy allocation、
  empty `OwnerFilter`、Adopt 和两种失败返回；
- `PSBRawNode_GetDictionaryKeys_guess@0x598E64`：Dictionary gate、packed count、
  reserve、DecodeName、end/capacity 分支、copy/emplace 与 COW cleanup；
- `PSBMedia_EnsureContainer_guess@0x599E04`：Variant type、container storage
  null/length/content gate、新 holder、adaptor Variant、retain/release 与失败清理；
- `PSBMedia_GetResourceData_guess@0x59A0B4`：Resolve、owner/header 三表读取、raw-tag
  payload 宽度分派、offset/length mask、borrowed pointer 和 owner terminal release；
- `PSBFile_loadMethod_FuncCall_guess@0x59B570`：membername/objthis/argc 错误、
  native instance lookup、paramsFunctor 字段、成对 member-pointer 读取、virtual
  adjustment、首参数转换、间接调用与 result Variant 转换。

目标逻辑摘要不超过十行：

```text
enumerate every EA-backed typed-member R/RW/address semantic row
if its machine form is LDR/LDP/LDRB/LDRH/LDUR/LDURH/ADD, select each physical lane
walk every normal successor until the first use, transform, ABI call-use, call boundary, or RET
prove ABI call-use against the independent direct/indirect argument manifests
for residual predicates/stores/transfers, select the actual consumer operand
walk every normal predecessor backwards to the first exact writer or architectural zero
reject undeclared events, writers, volatile call crossings, roots, terminals, and incomplete paths
require exact equality with all declared consumer/source sets
```

## 本地逐段对照

| Android ARM64 读/consumer 链 | 当前源码对照 |
| --- | --- |
| `Load@0x598268` 的 Variant type、Octet data/size、MDF/filter/Adopt | `cpp/plugins/psbfile/PSBRawFile.cpp:442-479` 保留 String/Octet 两分支、完整 MDF clone、empty filter 与各失败默认值 |
| `GetDictionaryKeys@0x598E64` 的 vector begin/end/capacity、reserve/emplace/COW | `cpp/plugins/psbfile/PSBRawFile.cpp:280-306` 保留 `std::vector<std::string>`、两个 packed view、reserve、DecodeName、emplace |
| `EnsureContainer@0x599E04` 的 cached Variant/ttstr 比较与 adaptor commit | `cpp/plugins/psbfile/PSBMedia.cpp:19-49` 保留 type+container gate、新 PSBFile、adaptor Variant、object Release、file/container 顺序提交 |
| `GetResourceData@0x59A0B4` 的 local raw node、header 三表与 owner release | `cpp/plugins/psbfile/PSBMedia.cpp:112-124` 保留 `Resolve → value.GetResource(size)` 源码调用；Android `-O3` 只内联该 callee，不改变源码层次 |
| `Resolve@0x59A4B0` 的 self->file 取址、root/current、segment loop 与成功尾写 | `cpp/plugins/psbfile/PSBMedia.cpp:52-109` 保留 adaptor lookup、local node、同一 narrow key、miss preserve output 与成功 commit |
| `load FuncCall@0x59B570` 的 functor/member-pointer/virtual invoke | `cpp/core/plugin/ncbind.hpp:924-1009,1087-1194,1335-1347` 保留 paramsFunctor、doInvoke、native instance gate、MethodCaller/member-pointer 调用与 result convertor |
| typed root/load 注册语义 | `cpp/plugins/psbfile/main.cpp:690-700,704-730,751-754` 保留 root getter、PSBFile convertor 与 `Method("load", &PSBFile::Load)` |

逐段对照没有发现需要修改 `cpp/` 的差异。目标的 scalarized stack/member offset 不被
误写成本地对象 ABI；本地仍以普通 C++ 字段、嵌套 holder、`std::vector` 和 ncbind
模板自然生成 wasm32 布局。

## IDB 与机械门禁

`0x596BD8`、`0x598414`、`0x599E70`、`0x599E80`、`0x59A13C`、
`0x59A4E0`、`0x59B4B0`、`0x59B610` 与 `0x59B888` 已加入
`TYPED-MEMBER-READ-CONSUMER/ANCHOR` 证据注释并保存。

[verify_elf_surface.py](verify_elf_surface.py) 新增两份 canonical payload：

| payload | 原始字节 | SHA-256 |
| --- | ---: | --- |
| direct producer + first event | 9,620 | `adaa11592cb1a2a6b6509a4134681e6de6d2bc1ab8f28ad8779babd699fd4d19` |
| residual anchor + source | 1,890 | `d38e845c8fea821cc26bfce7170042343ece53adc2807ed9051f7978ca7e5cbe` |

门禁会：

1. 核对每个语义 row 的 owner/site/mode/base/member/offset/op 与 producer/consumer lane；
2. 从 ELF 验证 exact producer、semantic site、actual consumer、source 和 event word；
3. 重建 61 个 owner 的 entry-rooted normal CFG 与 predecessor 图；
4. 对 268 个 producer lane 重走完整 forward first-event closure；
5. 对 45 个 residual anchor lane 重走完整 backwards source closure；
6. 复用 direct/indirect argument 清单验证 37 个 call-use；
7. 在未声明事件、writer、call boundary、root、terminal 或非完整路径上失败。

新增通过输出：

```text
typed_member_read_surface=true owners=61 sites=290 semantic_events=311 normal=311 landing=0 r=297 rw=3 address=11 producer_rows=267 producer_lanes=268 anchor_rows=44 anchor_lanes=45 paths_complete=true sha256=true
typed_member_read_consumers=true relations=288 single=257 multi=11 max=5 use=196 transform=46 call_use=37 call_boundary=6 terminal=3 pre_event_loops=0 volatile_call_clobbers=0
typed_member_read_anchors=true sources=45 single=45 multi=0 max=1 instruction=42 zero=3 producer_classes=7 anchor_classes=5 base_types=33 member_types=32
```

15 个 stripped/O3 identifier/factorization 上限保持不变；本轮新增的是字段读值、取址、
第一消费者、predicate/store/transfer anchor 与 member-pointer 中间数据流的正证据。
继续复原不依赖 ARMv7、废弃私库、同版本源码或 Git LFS。

本报告刻意不为 25 条 synthetic `R/RW/address` 行伪造 producer EA；这些行后来已和
另外 42 条 synthetic `W` 行一起闭合到真实 enclosing machine anchor，详见
[FOLLOWUP_TYPED_MEMBER_SYNTHETIC_REALIZATION_SURFACE_2026-08-03.md](FOLLOWUP_TYPED_MEMBER_SYNTHETIC_REALIZATION_SURFACE_2026-08-03.md)。
