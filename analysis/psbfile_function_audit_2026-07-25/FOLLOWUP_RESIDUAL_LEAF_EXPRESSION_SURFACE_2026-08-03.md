# Follow-up：剩余 `cot_fnum/cot_empty/cot_helper` 叶节点面闭环

日期：2026-08-03

权威目标：Android ARM64 `reference/libkrkr2/libkrkr2.so`

SHA-256：`ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`

## 结论

- ECT1 中除已有独立门禁的 `cot_num/cot_var/cot_obj` 外，恰剩
  **118 个叶节点**：`4 cot_fnum + 3 cot_empty + 111 cot_helper`。
- 118 行覆盖 39 个 owner，全部是 optimizer-synthetic，落在 87 个 normal-entry
  exact-word anchor；56 个 anchor 承载一行，31 个承载
  `_ReadStatusReg(TPIDR_EL0)` 的 callee/argument 两行。
- 80 个 helper callee 与完整 `cot_call` 面的 80 个 helper-intrinsic call 精确相等；
  31 个 helper argument 全部是对应 `_ReadStatusReg` 的 `TPIDR_EL0` 参数。没有 helper
  被误算成真实 `BL/BLR`，也没有真实业务函数藏在 residual 集合中。
- 完整 9,629-row expression tree 现严格分为 **4,829 个叶节点 + 4,800 个内部节点**；
  六种叶 op 为 `num/var/obj/helper/fnum/empty`，没有第七种未分类叶节点，也没有内部
  operator 丢失 child。
- fresh Android ARM64 反编译确认四个浮点常量、无限循环空条件、hidden-sret 空返回和
  `GetListAt` cleanup 后空返回均与当前源码一致；没有生产 `cpp/` GAP。本轮不改
  `cpp/`、测试或 fixture，也不构建生产目标。

## 全量 census

| op | row | owner | 语义 |
| --- | ---: | ---: | --- |
| `cot_helper` | 111 | 37 | 80 个 helper callee + 31 个 system-register argument |
| `cot_fnum` | 4 | 2 | 三个 `0.0`、一个 `1.0` |
| `cot_empty` | 3 | 3 | 一只无限循环空条件、两只 `void` 空返回 |
| **合计** | **118** | **39（并集）** | **87 个 normal anchor** |

child relation 精确分为：

```text
callee=80 arg:0=31 y=3 return=3 cond=1
```

直接父节点精确分为：

```text
call=111 asg=3 return=3 for=1
```

所有行均为 synthetic，故 `raw_ea=BADADDR`；门禁只把 nearest statement/expression
anchor 当作 realization 锚点，不伪称每个 synthetic leaf 都有一条一对一机器指令。

## `cot_helper`：decompiler intrinsic，不是缺失业务函数

| helper | callee | argument | 来源边界 |
| --- | ---: | ---: | --- |
| `_ReadStatusReg` | 31 | 0 | stack-canary TLS 读取的 Hex-Rays 包装 |
| `TPIDR_EL0` | 0 | 31 | 上述 31 个 call 的唯一参数，与 callee 共用 MRS anchor |
| `LODWORD` | 15 | 0 | 低 32 位截取/类型恢复表达式 |
| `LOBYTE` | 5 | 0 | 低 8 位截取/类型恢复表达式 |
| `__ldaxr` | 12 | 0 | 目标 libstdc++ COW/refcount exclusive-load lowering |
| `__stlxr` | 12 | 0 | 与上项配对的 exclusive-store lowering |
| `vdupq_n_s64` | 2 | 0 | SIMD broadcast intrinsic rendering |
| `__CFADD__` | 2 | 0 | carry/overflow 条件的 Hex-Rays helper rendering |
| `atomic_load` | 1 | 0 | 原子 byte load 的 decompiler rendering |
| **合计** | **80** | **31** | **111** |

callee 侧的八类计数逐项等于
[完整 `cot_call` 面](FOLLOWUP_CALL_EXPRESSION_SURFACE_2026-08-03.md)中的 helper family。
31 个 `_ReadStatusReg` call 又逐个满足：

```text
call.detail == args=1
callee.detail == helper=_ReadStatusReg
arg:0.detail == helper=TPIDR_EL0
callee.anchor/word == arg:0.anchor/word
```

这一区分很重要：`_ReadStatusReg(TPIDR_EL0)` 在 ctree 中是 source-like call，但机器只有
一条 `MRS`；`LODWORD/LOBYTE/__CFADD__` 也不证明原 C++ 显式调用了同名函数。相反，
`__ldaxr/__stlxr` 固定的是目标标准库/原子 lowering 与 COW 生命周期，不要求 wasm32
源码手写 AArch64 intrinsic。

## 四个 `cot_fnum`

| owner / ordinal | source value | ECT anchor | 实际值 producer |
| --- | --- | --- | --- |
| `CreateVariant@0x59673C / 11` | binary64 `0.0` | `0x59678C FMOV D8,XZR` | 同 anchor |
| `GetDouble@0x5992E8 / 6` | binary64 `0.0` | `0x599308 FMOV D1,XZR` | 同 anchor |
| `GetDouble@0x5992E8 / 9` | binary64 `1.0` | `0x59930C FMOV D0,#1.0` | 同 anchor |
| `GetDouble@0x5992E8 / 11` | binary64 `0.0` return | `0x59937C RET` | `0x599378 FMOV D0,XZR` |

门禁同时固定 decimal spelling、8-byte width、IEEE-754 bits、ECT anchor word 与物理 producer
word。最后一行说明 synthetic leaf 的 nearest anchor 语义：ctree 的 source `return 0.0`
挂在 `RET`，真正写 `D0` 的指令是前一条 `FMOV`；两者都独立校验，不能把 `RET` 误叫成
常量 producer。

## 三个 `cot_empty`

| owner / ordinal | statement | anchor | fresh 含义 |
| --- | --- | --- | --- |
| `FindNameIndex@0x59641C / 235` | `for(init=asg; cond=empty; step=preinc)` | `0x596500 ADD X0,X1,#1` | `for (i=name+1; ; ++i)` 的空条件 |
| `GetDictionaryKeys@0x598E64 / 27` | `return(expr=empty)` | `0x598EC4 BR X8` | 非 Dictionary case 的 hidden-sret `return;`；anchor 是 switch dispatch |
| `GetListAt@0x5999F4 / 326` | `return(expr=empty)` | `0x599CD8 CBNZ X19,...` | Dictionary string cleanup 后 `!out.owner` 的 `return;` |

这里同样不把 anchor mnemonic 反推成源码 token：`0x598EC4` 是 65-entry classifier 的
switch `BR`，只是 synthetic return 的 nearest statement anchor；真实空 vector 已在
函数入口通过 hidden-sret 三指针清零构造，非 Dictionary case 最终走正常 epilogue。

## fresh 反编译与本地逐行对照

本轮 fresh decompile/disasm 覆盖 `0x59641C`、`0x59673C`、`0x598E64`、`0x5992E8`、
`0x5999F4`。关键行为压缩为九行：

```text
FindNameIndex: 初始化 trie state；越界返回 false；for(;;) 校验 parent/check。
FindNameIndex: 命中 NUL 时写 nameIndex 返回 true；否则推进 cursor/state 并再做 unsigned bound。
CreateVariant: real 路径从 0.0 默认值开始，按 raw tag 产生整数转换、0.0、float 或 double。
GetDouble: true 返回 1.0；false/tag1D 返回 0.0；整数、float、double 分支各自转换。
GetDouble: 未知 tag 调 diagnostic；若 helper 返回，fallback 仍返回 0.0。
GetDictionaryKeys: 先构造空 hidden-sret vector；非 Dictionary 立即空返回。
GetDictionaryKeys: Dictionary 才构造 reusable COW string，reserve 后按 packed key 顺序 emplace。
GetListAt: Ensure/Resolve 失败返回；Array/Dictionary 分支分别列出 index/key。
GetListAt: Dictionary 临时 string cleanup 后，owner 为空直接 return，否则释放 raw owner。
```

| Android ARM64 行为 | 当前源码对照 |
| --- | --- |
| `for(;;)` 空条件及 trie 的每轮 bound/default | [`PSBRawFile.cpp:52-79`](../../cpp/plugins/psbfile/PSBRawFile.cpp) 保留相同 infinite loop、parent/state 更新与三条 false 边 |
| `CreateVariant/GetDouble` 的 `0.0/1.0` 与 diagnostic fallback | [`PSBPackedInternal.h:158-187`](../../cpp/plugins/psbfile/PSBPackedInternal.h) 固定相同 tag 分支；[`main.cpp:631-635`](../../cpp/plugins/psbfile/main.cpp) 与 [`PSBRawFile.cpp:360-365`](../../cpp/plugins/psbfile/PSBRawFile.cpp) 保留共享调用层 |
| hidden-sret 空 vector、Dictionary gate、reusable string/vector 生命周期 | [`PSBRawFile.cpp:280-306`](../../cpp/plugins/psbfile/PSBRawFile.cpp) 保留先构造 result、gate 后构造 key、reserve/emplace 顺序 |
| GetListAt 的两个提前返回、两种容器循环与 raw-node cleanup | [`PSBMedia.cpp:149-219`](../../cpp/plugins/psbfile/PSBMedia.cpp) 保留 `EnsureContainer/Resolve` gate、Array signed loop、Dictionary unsigned loop与作用域析构 |
| canary、COW exclusive loop、位截取等 helper | 当前普通 C++/STL/atomic 结构让目标编译器生成；不把 IDA helper 名写成 portable source API |

四个业务函数的条件、默认值、输出写入、容器构造时点和 cleanup 边均已有直接 Android
证据；当前实现逐项复刻。helper intrinsic 的精确名字则属于 decompiler/ABI rendering，
不能据此新增本地函数或 AArch64 专用源码。

## 完整叶节点分区

ECT1 的结构性 leaf（没有 child）恰为：

```text
cot_num=1181
cot_var=3073
cot_obj=457
cot_helper=111
cot_fnum=4
cot_empty=3
合计=4829
```

其余 4,800 行全部是有 child 的内部 operator；`4829 + 4800 = 9629`。verifier 同时要求：

- 六类 leaf 都没有 child；
- 六类以外的每个 operator 都至少有一个 child；
- residual 118 行不与已有 `num/var/obj` 专用面重叠；
- 所有 87 个 realization anchor 均在各 owner 的 entry-rooted normal CFG 中。

## 机械门禁

residual canonical semantic sequence 为 17,017 bytes，SHA-256：

```text
60dc1a61636178d3159b3a61dd1e888b4bab35cc9efdd2320965d0e0a35cace0
```

每一行固定：

```text
owner / ordinal / statement / parent / parent-op / op / relation / type /
detail / depth / exflags / concrete / raw-EA / anchor / exact-word
```

`verify_elf_surface.py` 固定输出：

```text
residual_leaf_expression_surface=true owners=39 rows=118 roots=4 ops=3 relations=5 types=15 details=12 concrete=0 synthetic=118 raw_sites=0 anchors=87 normal=87 landing=0 single=56 shared=31 max=2 helper_callees=80 helper_arguments=31 helper_names=9 floating=4 empty=3 leaf_ops=6 leaf_rows=4829 internal_rows=4800 semantic_bytes=17017 semantic_sha256=true helper_call_cross=true floating_producers=true leaf_partition=true paths_complete=true
```

完整 ELF verifier 继续通过；114-entry 总判定仍为
`ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`。本面闭合的是 ECT1 最后三类叶节点及完整
leaf/internal 集合，不把 synthetic anchor、Hex-Rays intrinsic 名或目标标准库 lowering
反向制造成原始 C++ API。
