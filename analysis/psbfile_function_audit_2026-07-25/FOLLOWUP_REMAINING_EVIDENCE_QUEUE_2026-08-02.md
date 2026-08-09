# Follow-up：剩余证据队列与天然物料盘点

日期：`2026-08-02`。本文件把最终 prototype 复扫之后仍未关闭的事项分成两条互不替代的
队列：15 个 stripped/O3 源码 token 的证据上限，以及 6 个只缺天然输入的运行时边界。
本轮没有修改 `cpp/`，没有生成、破坏或改写任何 PSB/MDF 测试物料。

## 结论

- 114 个 emitted 入口继续为
  `ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`；没有新生产实现 GAP。
- 114/114 prototype 已完成机械复扫。继续修 IDB ABI 类型可以提高反编译可读性，但不能
  唯一恢复 stripped/O3 已删除的 C++ identifier、member/free、pointer/reference、
  `const`、header-inline 或 helper factorization token。
- 运行时 PASS/FAIL 只能补边界覆盖，不能把上述源码 token 自动升级为 `ALIGNED`。

这里的“证据上限”只限制**精确源码拼写的唯一性证明**，不是源代码复原的阻塞项。
`libkrkr2.so` 本体与 IDA 证据已经足够继续复原：对每个仍有多个等价 token 的位置，采用
当前二进制支持最强的 target-compatible 源码候选，以 `_guess` 标注不能唯一化的名字，
并继续审计后续函数。不得等待同版本源码，不得为消除审计标签而追索外部私库、LFS 对象
或其他非权威源码。

## 队列 A：15 个源码 token 证据上限

| 类别 | canonical 地址 | 当前无法唯一恢复的内容 | 仅用于唯一化审计标签的可选正证据 |
| --- | --- | --- | --- |
| packed/scalar 与 classifier 分层 | `0x59641C`、`0x59659C`、`0x59673C`、`0x596BC4`、`0x596C70`、`0x596F50`、`0x597B1C`、`0x598B58`、`0x5996E4`、`0x59A0B4` | 精确 helper/type 名、member/free/header-inline token、部分 pointer/reference 拼写 | Android 1.4.4 目标内符号、DWARF、同版本源码，或能区分候选的新增目标内 retained boundary |
| numeric helper factorization | `0x5992E8`、`0x599438` | integer/raw-double helper 的精确名字、声明位置和源码拆分 | 同上；等价 switch 或另一份 O3 inline clone不能唯一恢复拼写 |
| dispatch 构造器 | `0x597AD4` | class 内/外定义、参数 `const`/reference 的精确 token | 构造器符号、调试类型或同版本源码 |
| transfer helper | `0x598A64` | hidden-sret 对应的精确返回类型拼写及 helper token | 目标调试类型、符号或同版本源码 |
| empty `ttstr` token | `0x599DD8` | 默认空字符串的精确构造/常量表达式 token | 能保留该表达式的目标内调试/源码证据 |

同源 iOS arm64 已帮助约束共享边界和 target-compatible factorization，但它不是 Android
1.4.4 的命名权威，不能单独消除这 15 项。重复反编译同一份 Android stripped/O3 函数、
仅调整 Hex-Rays 类型或用运行时正常样本得到相同结果，也不能跨越该证据上限。

2026-08-03 又从 Git 历史定位到另一份 Android ARM64 `libkrkr2.so`：SHA-256
`05e2ff4c77f1561608ad7703153d2fb09855bf223237a85dc2267fff1388564f`、Build-ID
`de22234bffa0545d276b705487ca0c3d35101386`。机械对照证明 114 个 FDE 全部固定平移
`+0x3E0`、39 张 LSDA/232 个 call-site 完全同形，5,525 条 psbfile 指令的操作码/寄存器
骨架零差异；全部差异只是链接地址物化。该历史树没有 PSB 源码/调试伴随文件，动态符号
差异也只来自无关的 `std::bind` 实例，因此它是同一 psbfile 机器骨架的第二链接布局，
不是能消除 token 上限的独立 retained boundary。完整门禁见
[FOLLOWUP_HISTORICAL_ANDROID_ARM64_LINEAGE_2026-08-03.md](FOLLOWUP_HISTORICAL_ANDROID_ARM64_LINEAGE_2026-08-03.md)。

2026-08-03 对这 15 个入口再次逐个使用当前 Android ARM64 目标 fresh `decompile`，并
加深复扫 `PSBRawOwner` 生命周期与 `GetDictionaryKeys` 容器/异常清理。结果仍为实现六维
一致、仅精确 token 不可唯一化；地址级证据和构建/consumer 门禁见
[FOLLOWUP_BINARY_CONTINUATION_SWEEP_2026-08-03.md](FOLLOWUP_BINARY_CONTINUATION_SWEEP_2026-08-03.md)。
这次复扫再次证明队列 A 不是继续执行阻塞项。

同日又把 114 个函数入口之外的静态对象面纳入 ELF 门禁：`.init_array`、dispatch 双
地址点、media/AutoRegister/adaptor 与三只 typed wrapper 共 10 个表面、177 个 qword
逐值通过。它强化了可证明的继承/virtual/注册拓扑，但没有把 stripped 精确名字 token
冒充为可恢复；详见
[FOLLOWUP_STATIC_OBJECT_VTABLE_SURFACE_2026-08-03.md](FOLLOWUP_STATIC_OBJECT_VTABLE_SURFACE_2026-08-03.md)。

同日继续枚举 114 个入口的全部 data xref，补回普通 ASCII 汇总遗漏的 UTF-16LE 表面。
`.rodata` 的 34 个字面量、42 张 switch table/915 个 case 槽以及 canonical 空串指针链
现均由 ELF 门禁固定；classifier、packed/numeric 分支和共享 `.bss` 生命周期与当前源码
一致，没有新增 GAP。该正证据进一步约束行为和内部控制流，但仍不能唯一化 helper 名、
header-inline 或 member/free token，因此不改变队列 A。详见
[FOLLOWUP_LITERAL_SWITCH_GLOBAL_SURFACE_2026-08-03.md](FOLLOWUP_LITERAL_SWITCH_GLOBAL_SURFACE_2026-08-03.md)。

同日又绕过组件工具的空 internal-call-graph 结果，直接解码全部 114 个 FDE 的
`BL/B/BLR/BR`。567 个 transfer site、39 条 MANIFEST 内 edge、65 个外部 direct callee
与 46 个非 switch 间接调用均已分类并由 ELF 门禁固定；现有调用边、tail、callback、
vdispatch/member-pointer 与 EH cleanup 路径全部一致，没有新增 GAP。该结果约束 emitted
call topology，但不能唯一恢复被 O3 内联/折叠的源码 helper token，故同样不改变队列 A。
详见 [FOLLOWUP_CALLSITE_SURFACE_2026-08-03.md](FOLLOWUP_CALLSITE_SURFACE_2026-08-03.md)。

同日继续对 46 个非 switch indirect transfer 做逐点 target-register backward slice，
把 44 个 fixed-offset producer `LDR` 与 2 个 Itanium member-pointer register-offset
`LDR` 的精确 word、寄存器和 18 类 ABI 角色接入 ELF 门禁。callback、stream、lister、
`std::function`、dispatch/NCB 与 typed root/load wrapper 均闭合到当前源码，没有新增 GAP。
该结果约束间接调用 ABI，但仍不能从 stripped/O3 产物唯一恢复 helper 名或模板实例的原始
spelling，因此不改变队列 A。详见
[FOLLOWUP_INDIRECT_ABI_SURFACE_2026-08-03.md](FOLLOWUP_INDIRECT_ABI_SURFACE_2026-08-03.md)。

随后把 435 个 MANIFEST 外 direct site 的 65 个 target 逐个分类，并 fresh decompile 其中
32 个 stripped `sub_*`。全部 target 均是九类通用 runtime/storage/TJS/ncbind/libstdc++
边界，没有遗漏的 PSB 私有 helper；精确 target/site 数已接入 ELF 门禁。该结论进一步
闭合调用链和清理边，但不能恢复这些 stripped 通用 helper 的原 spelling，因此不改变
队列 A。详见
[FOLLOWUP_EXTERNAL_CALLEE_SURFACE_2026-08-03.md](FOLLOWUP_EXTERNAL_CALLEE_SURFACE_2026-08-03.md)。

随后继续固定 114 个 FDE 的 stack-frame/local-lifetime surface：57 个 framed 与 57 个
frameless、52 个 entry frame、5 个 shrink-wrapped diagnostic frame、31 个 canary、
callee-saved register mask 和唯一 `D8` spill 均已逐项接入 ELF 门禁。五个慢路径 frame
没有隐藏 owning local；39 个 LSDA frame 与较大 Variant/ttstr/raw-node/vector/ncbind
scope 也和本地一致，没有新增 GAP。frame byte 数和寄存器分配是 Android 编译产物，不能
唯一化 stripped 的 helper/type 名，因此不改变队列 A。详见
[FOLLOWUP_STACK_FRAME_LIFETIME_SURFACE_2026-08-03.md](FOLLOWUP_STACK_FRAME_LIFETIME_SURFACE_2026-08-03.md)。

随后把 44 个 MANIFEST 内 direct transfer 从 call-edge 集合继续细化为逐调用点 contract：
21 类参数角色、8 类返回消费、两处 hidden-sret、Variant by-value 间接 ABI、两种
`uint32 → size_t` zero-extension 与 ncbind 两只空 tag-reference 的 producer word 均已
接入门禁。本地 packed/raw/media/ncbind 调用层与生命周期一致，没有新增 GAP。该结果
精确约束 emitted ABI 与 source-facing 模板形状，但仍不能从 stripped/O3 产物唯一恢复
空 tag/template/helper 的原 spelling，因此不改变队列 A。详见
[FOLLOWUP_INTERNAL_CALL_CONTRACT_SURFACE_2026-08-03.md](FOLLOWUP_INTERNAL_CALL_CONTRACT_SURFACE_2026-08-03.md)。

随后继续枚举 typed-member/对象字段表面的 483-row 旧基线：416 条带具体 EA，折叠成
385 个唯一机器站点/62 个 owner FDE 并由 ELF word digest 固定；另外 67 条为
optimizer-synthetic expression。该轮裸指针筛查发现 7 个 IDB 类型传播缺口，补型后
owner/header/raw-node/dispatch/media/ncbind 字段集合、读写模式与当前源码嵌套结构一致，
没有新增 GAP。后续完整 `cot_ptr/cot_idx` 复扫又提升 5 条 typed read，故本段 483 不再是
最终总数。字段名与 exact member/free/header-inline spelling 仍受 stripped/O3 限制，
且 ARM64 byte offset 不是 portable 源码 token，因此不改变队列 A。详见
[FOLLOWUP_TYPED_MEMBER_FIELD_SURFACE_2026-08-03.md](FOLLOWUP_TYPED_MEMBER_FIELD_SURFACE_2026-08-03.md)。

随后继续展开 39 张 LSDA 表的完整 landing cleanup contract：155 条非零引用折叠为
150 个唯一入口；75 个 cleanup-only 全部 `_Unwind_Resume`，75 个 catch-all 精确分为
72 个直接 terminate 与 3 个 catch/delete/rethrow。569 个唯一 landing 指令、1,150 个
per-root 指令实例、168 个 transfer、全部 successor 及与正常流零交叉的结论均已接入
ELF 门禁。本地 Factory/stream/Variant/ttstr/vector/ncbind 清理层和 raw-data 异常泄漏
边界一致，没有新增 GAP。该结果约束异常对象生命周期和清理顺序，但 stripped/O3 仍不能
唯一恢复每个隐式析构/模板 helper 的原 spelling，因此不改变队列 A。详见
[FOLLOWUP_LANDING_CLEANUP_CONTRACT_SURFACE_2026-08-03.md](FOLLOWUP_LANDING_CLEANUP_CONTRACT_SURFACE_2026-08-03.md)。

随后继续枚举 114-entry normal CFG / terminal contract：4,956 条正常指令与 569 条
landing-only 指令零交叉并完整覆盖 5,525 条 FDE 指令；208 个终点精确分为 162 `RET`、
11 direct tail、1 indirect tail 与 34 true-noreturn。IDB 114/114 prototype 又把源码返回
ABI 固定为 37 void、71 `W0/X0`、5 hidden-sret `X8`、1 `D0`；28 个 TVP/TJS
diagnostic 调用保留 helper-return fallback，不能套用 landing 的 noreturn 集。本地默认值、
non-trivial return 与 tail 生命周期均一致，没有新增 GAP。该结果约束正常控制流、返回数据流
和边界 continuation，但仍不唯一化 stripped helper/identifier spelling，因此不改变队列 A。
详见
[FOLLOWUP_NORMAL_CFG_TERMINAL_CONTRACT_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_CFG_TERMINAL_CONTRACT_SURFACE_2026-08-03.md)。

随后继续细化同一 normal CFG 的 437 个条件 branch：66 个 owner、874 条
taken/fallthrough edge 已逐行固定；180 个 `B.cond` 全部回溯到唯一 NZCV producer，精确为
`176 CMP + 3 CMN + 1 SUBS`，并把 equality/unsigned/signed condition、`CBZ/CBNZ` 宽度及
`TBZ/TBNZ` bit 0/10/31 一并接入 ELF 门禁。本地 packed/dispatch/raw/media/ncbind 的
有符号性、TJS error 与 flag gate 均一致，没有新增 GAP。该结果约束边界谓词与中间 flags
数据流，但仍不能从 stripped/O3 产物唯一恢复 helper/identifier spelling，因此不改变
队列 A。详见
[FOLLOWUP_NORMAL_BRANCH_PREDICATE_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_BRANCH_PREDICATE_SURFACE_2026-08-03.md)。

随后把其中 257 个 `CBZ/CBNZ/TBZ/TBNZ` 的 tested register 沿完整 predecessor graph
继续回溯到 reaching producer：287 条关系精确分成 246 个 instruction writer、14 个
入口参数与 27 个 `W0/X0` call return；244 个单来源、13 个多来源 join 全部闭合，
volatile call clobber 为 0。packed default、vector/allocation、ttstr 临时、entry bool、
lifetime guard、TJS signed error 和 ARM member-pointer low-bit 均与当前源码一致，没有
新增 GAP。该结果进一步约束数据流、对象临时与 ABI flag，但仍不能唯一恢复 stripped/O3
删除的 helper/type/identifier spelling，因此不改变队列 A。详见
[FOLLOWUP_NORMAL_CB_TB_PRODUCER_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_CB_TB_PRODUCER_SURFACE_2026-08-03.md)。

随后继续闭合 42 张 switch table 的 selector producer 与完整分派链：42 个 selector 在
normal CFG 上全部唯一回溯，精确为 41 条 raw-tag `LDRB` 关系与 1 条 chained `SUB`
关系；41 个 `tag-lowcase` normalizer、唯一 zero-based reuse，以及 42 组 unsigned
`CMP/B.HI` 和 `ADRP/ADD/LDRSW/ADD/BR` 共形成 335 条固定链指令。当前 classifier、packed、
numeric、String/Resource、Array/Dictionary 数据流一致，没有新增 GAP。该结果固定 selector
来源、归一化、范围 default 与跳表地址数据流，但仍不能唯一恢复 stripped/O3 删除的
helper/identifier spelling，因此不改变队列 A。详见
[FOLLOWUP_SWITCH_SELECTOR_DISPATCH_SURFACE_2026-08-03.md](FOLLOWUP_SWITCH_SELECTOR_DISPATCH_SURFACE_2026-08-03.md)。

随后继续把 180 个 `B.cond` 的 `CMP/CMN/SUBS` producer 展开到输入来源：260 个寄存器
operand 共闭合为 320 条关系，其中 235 个单来源、25 个多来源 join、最大 9 路；313 个
显式 writer、4 个入口参数与 3 个 call return 均由 exact word/destination 约束，volatile
call-clobber 为 0。trie/dictionary、classifier diagnostic fallback、packed count、vector/
COW-string 与 typed wrapper 边界和当前源码一致，没有新增 GAP。该结果固定比较输入的
来源、循环携带值与默认值，但仍不能唯一恢复 stripped/O3 删除的 identifier/factorization，
因此不改变队列 A。详见
[FOLLOWUP_NORMAL_NZCV_INPUT_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_NZCV_INPUT_SURFACE_2026-08-03.md)。

随后继续把 162 个正常 `RET` 中的 129 个 source-visible W0/X0/D0 value return 回溯到
完整 producer：72 个 owner、160 条关系精确分成 157 个 instruction writer、2 个 direct
call return 与 1 个 indirect call return；112 个单来源、17 个多来源 join 全部闭合，
入口残留与未声明 call-clobber 为 0。TJS error/default、refcount、allocation/null、numeric
conversion、short-circuit 与 typed callback 返回值均与当前源码一致，没有新增 GAP。该
结果固定返回值的数据流与 helper-return 边界，但仍不能唯一恢复 stripped/O3 删除的
identifier/factorization，因此不改变队列 A。详见
[FOLLOWUP_NORMAL_RETURN_VALUE_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_RETURN_VALUE_SURFACE_2026-08-03.md)。

随后从全部 311 个 continuing normal `BL/BLR` fallthrough 正向闭合 GPR0 首事件：419 条
关系精确分为 86 个显式 use、250 个 overwrite、49 个 call-boundary 与 34 个 `RET`
reach；125 个 direct-void 调用的显式 use 为 0，全部 `RET` reach 也只属于 void/
hidden-sret/FP owner。四条 pre-event loop 的有限出口与全部 event word/mask 已接入 ELF
门禁。本轮固定调用结果使用、临时对象和寄存器残留边界，没有新增 GAP，也不能唯一恢复
stripped/O3 删除的 identifier/factorization，因此不改变队列 A。详见
[FOLLOWUP_NORMAL_CALL_RESULT_FIRST_EVENT_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_CALL_RESULT_FIRST_EVENT_SURFACE_2026-08-03.md)。

随后从输入侧闭合全部 317 个 normal direct transfer 的参数来源：306 个 `BL` 与 11 个
direct tail 共包含 446 个 register arg、475 条 reaching-definition 关系，精确分为
447 个 instruction writer、18 个 entry parameter 与 10 个 preceding-call return；11 个
多来源 join 最大 13 路，volatile call-clobber 与 entry residue 为 0。7 个 hidden `X8`、
唯一 `D0`、三条 pre-index `STR` writeback 及 `STLXR` 不写 memory base 的纠正均已进入
ELF 门禁。当前 packed/raw/media/ncbind 参数数据流没有新增 GAP；该结果仍不能唯一恢复
stripped/O3 删除的 identifier/factorization，因此不改变队列 A。详见
[FOLLOWUP_NORMAL_DIRECT_ARGUMENT_PRODUCER_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_DIRECT_ARGUMENT_PRODUCER_SURFACE_2026-08-03.md)。

随后从输入侧补齐全部 46 个非 switch indirect transfer：117 个 `X0..X7` 参数沿
40 个 normal-entry 与 6 个 LSDA landing-only callsite 闭合为 120 条关系，精确为
119 个 instruction writer 与唯一 deleting-destructor entry `X0`；3 个参数为两路 join，
未声明 call clobber/entry residue 为 0。factory callback 的 stale `X4` 假第五参已由
call-operand type 纠正，两处 Enum callback 则固定为完整八参 `FuncCall`。该结果继续约束
callback/vtable/manager/member-pointer 和异常清理输入数据流，但仍不能唯一恢复 stripped/O3
删除的 identifier/factorization，因此不改变队列 A。详见
[FOLLOWUP_INDIRECT_ARGUMENT_PRODUCER_SURFACE_2026-08-03.md](FOLLOWUP_INDIRECT_ARGUMENT_PRODUCER_SURFACE_2026-08-03.md)。

随后继续闭合 typed-member 的 instruction-backed `W/RW` 写值来源：108 个字段事件落在
101 个 store 站点，形成 109 条关系；84 个 instruction writer、3 个 constructor entry
parameter、22 个 ZR source 和唯一 vector-end 两路 join 均由 selected operand、exact word
与 normal predecessor path 固定，未声明 call clobber/entry residue 为 0。四次完整 header
population、raw/dispatch/media output commit、vector 与 ncbind 状态更新均与当前源码一致，
没有新增 GAP。该结果约束字段写入的数据流、构造参数、zero initialization 与容器 commit，
但仍不能唯一恢复 stripped/O3 删除的 identifier/factorization，因此不改变队列 A。详见
[FOLLOWUP_TYPED_MEMBER_WRITE_VALUE_PRODUCER_SURFACE_2026-08-03.md](FOLLOWUP_TYPED_MEMBER_WRITE_VALUE_PRODUCER_SURFACE_2026-08-03.md)。

随后继续闭合互补的 typed-member `R/RW/address` consumer/source：311 条带真实 EA
的语义事件落在 290 个站点与 61 个 owner。267 条 direct producer 语义行展开为
268 个 lane、288 条 first-event relation；44 条 residual anchor 语义行展开为
45 个 lane、45 条单来源关系。37 个 call-use 均由独立 ABI argument manifest 证明，
landing-only、pre-event loop 与未声明 volatile call-clobber 为 0。该结果继续约束
字段读取、取址、predicate/store/transfer anchor、media resource clone 与 ncbind
member-pointer 中间数据流，但仍不能唯一恢复 stripped/O3 删除的 identifier/factorization，
因此不改变队列 A。详见
[FOLLOWUP_TYPED_MEMBER_READ_CONSUMER_SURFACE_2026-08-03.md](FOLLOWUP_TYPED_MEMBER_READ_CONSUMER_SURFACE_2026-08-03.md)。

随后继续闭合 typed-member 表面剩余的 67 条 optimizer-synthetic 语义行：它们没有
独立 ctree EA，但全部可映射到 73 个真实机器锚点，精确为 62 个 coalesced/assignment/
RMW store、6 个 direct `LDR`、3 个 function-manager `BLR` 与 2 个 packed-tag switch
`BR`。所有锚点均 normal-entry reachable，BLR/BR 又分别由独立 ABI/switch manifest
交叉证明；`416 EA-backed + 67 synthetic = 483`。该结果补齐旧基线 synthetic 部分的
机器实现，
没有新增 GAP，也不能唯一恢复 stripped/O3 删除的 identifier/factorization，因此不改变
队列 A。详见
[FOLLOWUP_TYPED_MEMBER_SYNTHETIC_REALIZATION_SURFACE_2026-08-03.md](FOLLOWUP_TYPED_MEMBER_SYNTHETIC_REALIZATION_SURFACE_2026-08-03.md)。

随后完整枚举当前 `cot_ptr/cot_idx` 表面：5 条 read-only promotion 把 typed-member 总数
纠正为 `488 = 421 EA-backed + 67 synthetic`；剩余
`667 = 461 cot_ptr + 206 cot_idx` 行全部绑定到 610 个 normal-entry exact-word 锚点。
这只加强现有 packed/raw/media/TJS/NCB/STL 结构证据，没有新增 GAP，也不改变队列 A；
详见
[FOLLOWUP_RAW_MEMORY_DEREFERENCE_SURFACE_2026-08-03.md](FOLLOWUP_RAW_MEMORY_DEREFERENCE_SURFACE_2026-08-03.md)。

随后又完整枚举 114 个 FDE 的 cot_num：1,181 行覆盖 95 个 owner，1,133 条具体 EA 与
48 条 synthetic 全部绑定到 1,055 个正常流机器锚点；并与 1,208-row machine immediate
census 做集合分层，避免把 frame/address/system-register immediate 反向当成源码 token。
classifier、packed width/mask、TJS error/flag、callback arity 和 numeric shift/default
均与当前源码一致，没有新增 GAP，也不改变队列 A；详见
[FOLLOWUP_NUMERIC_CTREE_SURFACE_2026-08-03.md](FOLLOWUP_NUMERIC_CTREE_SURFACE_2026-08-03.md)。

随后又完整枚举 114 个 FDE 的 lvar/cot_var：1,056 个声明覆盖 112 个 owner，3,073 条
使用覆盖 77 个 owner 与 770 个声明，286 个声明没有最终 use；声明位置/角色为
`311 argument / 111 stack / 945 register / 72 result / 58 byref`，使用模式为
`R=2166 / W=804 / RW=20 / address=83`。1,686 条 EA-backed 与 1,387 条 synthetic
使用全部绑定到 2,214 个 normal-entry exact-word anchor。该证据固定局部对象种类、参数/
结果槽、作用域与普通/异常析构关系，但不会把 ARM64 stack offset、location 或 stripped/O3
删除的 identifier 反向制造成 portable C++ token；fresh 对照没有新增 GAP，也不改变
队列 A。后续 ECT1 逐行交叉纠正旧 LVS1 中
`0x59A968:17 a0→a4` 与 `0x59AD84:30 a0→a5` 两处重复实参 relation，
其余 3,071 行不变。详见
[FOLLOWUP_LVAR_CTREE_SURFACE_2026-08-03.md](FOLLOWUP_LVAR_CTREE_SURFACE_2026-08-03.md)。

随后又完整枚举 114 个 FDE 的 statement/control-tree：2,922 个 `cinsn/cit_*` 节点覆盖
12 种 op、119 种 relation 与 122 种 detail；2,882 个 concrete 与 40 个 synthetic
节点全部绑定到 1,942 个 normal-entry exact-word anchor。block/if/loop/switch 父子次序、
46 个 goto 到 36 个同 owner label 与最大深度 15 均已固定。fresh 对照中唯一
`for`/`continue`、内联 STL loop 与 RAII cleanup tree 没有形成新增 GAP，也不改变队列 A；
详见
[FOLLOWUP_STATEMENT_CTREE_SURFACE_2026-08-03.md](FOLLOWUP_STATEMENT_CTREE_SURFACE_2026-08-03.md)。

随后又完整枚举 expression tree：9,629 个 `cexpr/cot_*` 节点覆盖 108 个 owner、
42 种 op、17 种 relation、271 种类型与 360 种 detail；7,077 个 concrete 与
2,552 个 synthetic 节点全部绑定到 3,076 个 normal-entry exact-word anchor。
1,935 个 statement root、1,181 个 `cot_num` 与 3,073 个 `cot_var` 均与独立
manifest 逐行零差异。该结果固定完整运算树、类型、调用参数位置和 parent/statement
归属，但仍不能唯一恢复被 stripped/O3 删除的 identifier/factorization；fresh 对照无
新增 GAP，也不改变队列 A。详见
[FOLLOWUP_EXPRESSION_CTREE_SURFACE_2026-08-03.md](FOLLOWUP_EXPRESSION_CTREE_SURFACE_2026-08-03.md)。

随后从 ECT1 派生完整 `cot_call` 面并与机器 transfer 分层：405 个节点精确分为
`285 direct + 40 computed indirect + 80 helper intrinsic`，325 个真实 transfer
全部逐站点命中机器 callsite。机器侧额外 200 个站点严格分为 194 direct 与 6
landing-only indirect，保持为 EH、canary、implicit lifetime 和 compiler lowering；
40 个 computed call 恰等于独立 indirect ABI surface 的全部 normal 站点。该结果防止
把机器 helper/EH 调用反向制造成显式 C++ call，不唯一化 stripped helper 名；fresh
对照无新增 GAP，也不改变队列 A。详见
[FOLLOWUP_CALL_EXPRESSION_SURFACE_2026-08-03.md](FOLLOWUP_CALL_EXPRESSION_SURFACE_2026-08-03.md)。

随后从 ECT1 派生完整 `cot_obj` 叶节点面：457 个节点按 ELF section 和既有 call、
callback/artifact、literal、initialized-data、global-BSS、GOT 重定位面形成八类互斥
分区。该结果固定 direct/address-taken/data/global/import 地址与三类数值碰撞的边界，
也解析 9 个 literal-pool base+index，不把 linker/IDA 表达反向制造成源码 token。
fresh 对照无新增 GAP，不改变队列 A；但 `base+0x18` 与 `data-24` 正证据已把旧 COW
empty storage 范围由 8 bytes 就地纠正为 32 bytes。详见
[FOLLOWUP_OBJECT_EXPRESSION_SURFACE_2026-08-03.md](FOLLOWUP_OBJECT_EXPRESSION_SURFACE_2026-08-03.md)。

随后闭合 ECT1 最后三类 leaf：`4 cot_fnum + 3 cot_empty + 111 cot_helper` 全部具有
normal-entry exact-word anchor，80 个 helper callee 与 `cot_call` helper 集合相等，
31 个 system-register 参数逐个配对。四个浮点值另固定物理 producer；三只空表达式
固定 loop/void-return statement，而不把 nearest anchor mnemonic 误当源码 token。
完整 9,629-row tree 现机械分区为 4,829 leaf 与 4,800 internal。fresh 对照无新增 GAP，
不改变队列 A；详见
[FOLLOWUP_RESIDUAL_LEAF_EXPRESSION_SURFACE_2026-08-03.md](FOLLOWUP_RESIDUAL_LEAF_EXPRESSION_SURFACE_2026-08-03.md)。

随后从 ECT1 派生完整 `cot_memptr/cot_memref` 链面：625 个 member 节点全部落到
normal-entry exact-word anchor，其中 539 个 outer expression 与既有 typed-member
读/写/synthetic projection 双向闭合，86 个 nested base 则获得此前没有的独立逐节点
门禁。六种链形、最大三层 member 深度、字段 offset/reference width 与 terminal base
均已固定。该结果确认 owner→header→names/entries 等中间字段链没有被 outer-only
projection 隐藏，但 ABI offset 只作目标证据，不反向制造 wasm32 padding 或精确字段名；
fresh 对照无新增 GAP，也不改变队列 A。详见
[FOLLOWUP_MEMBER_EXPRESSION_CHAIN_SURFACE_2026-08-03.md](FOLLOWUP_MEMBER_EXPRESSION_CHAIN_SURFACE_2026-08-03.md)。

随后派生完整 `cot_asg` 面：1,123 个 assignment 的有序 lhs/rhs、直接目标类别与
normal-entry exact-word anchor 已固定；local/member/raw/global/helper 五类目标又与
五个既有独立表面交叉。该结果能区分正常 source assignment、nested address node、
landing-only store 与 Hex-Rays `LOBYTE/LODWORD` pseudo-lvalue，但仍不唯一化被 O3
删除的局部名、临时 factorization 或 register-part spelling。fresh 对照无新增 GAP，
也不改变队列 A；详见
[FOLLOWUP_ASSIGNMENT_EXPRESSION_SURFACE_2026-08-03.md](FOLLOWUP_ASSIGNMENT_EXPRESSION_SURFACE_2026-08-03.md)。

随后闭合剩余 1,980 个 internal operator：cast/ref/arithmetic/predicate/mutation/comma
六族的有序 child、parent/relation/type 与 exact-word anchor 全部固定，address-of 与
RMW operand 又分别交叉 local/member/raw/object 独立投影。结合四个 dedicated internal
projection，完整 4,800 个 internal 节点现已严格分割且无未分类项。该结果仍不把
Hex-Rays recovered type spelling、局部 factorization 或 optimizer-synthetic 节点提升为
唯一原始 token；fresh 对照无新增 GAP，也不改变队列 A。详见
[FOLLOWUP_REMAINING_INTERNAL_EXPRESSION_SURFACE_2026-08-03.md](FOLLOWUP_REMAINING_INTERNAL_EXPRESSION_SURFACE_2026-08-03.md)。

## 队列 B：天然输入限定的运行时边界

对 `reference/` 与 `tests/test_files/` 做了只读盘点：当前可见 5,805 个非 APK/IPA 文件中
没有大于 `0xFFFFFFFF` 字节的文件；最大文件是
`reference/xp3/dracu/dracu.zip`，为 `740,622,267` 字节。76 个
`.psb/.pimg/.mtn` 候选全部以 `PSB\0` 开头，最大候选为
`reference/xp3/title_bg_motion/title_bg.mtn`，为 `21,952,984` 字节。

另发现 `reference/xp3/{caution_test,dracu_boot}/DRACU-RIOT/scn/` 中已有 142 个天然
`mdf\0` 文件。主机只读验证结果为：142/142 zlib 解压成功、声明长度全部匹配、解压后
142/142 以 `PSB\0` 开头；因此它们是可复用的 MDF-success 输入，却没有一例自然
zlib-failure。oracle README 中“仓库没有 committed MDF fixture”的旧说明已据此纠正。

进一步使用只读可达节点扫描器对所有 magic 为 `PSB\0/mdf\0` 的文件做 SHA-256 去重和
完整树遍历：222 个物理输入形成 112 份唯一 PSB，23,415,372 个可达节点全部解析成功，
已知 `m2logo.mtn@0x36F8 == 0x09` 锚点命中，但 tag `0x0B` 节点为 0。方法、fresh Android
边界与可复现输出见
[FOLLOWUP_NATURAL_TAG0B_INVENTORY_2026-08-02.md](FOLLOWUP_NATURAL_TAG0B_INVENTORY_2026-08-02.md)。

扫描器现另输出每个整数 tag 的全局值域、Variant/GetInt 最大差值与对应路径。当前资产
实际包含 `0x04..0x09`，并已选出七个固定 oracle case，覆盖两条 tag `0x09` 低字分叉；
`0x0A..0x0C` 仍为 0。详见
[FOLLOWUP_NATURAL_INTEGER_ORACLE_2026-08-02.md](FOLLOWUP_NATURAL_INTEGER_ORACLE_2026-08-02.md)。

扫描器还验证了 Resource index 与真实资源区间：当前 1,240 个 tag `0x19` 全部表内且
位于文件范围，唯一 raw tag `0x1A` 的 index 56395 超出 73 项表；没有自然
`0x1B/0x1C/0x2D`。因此成功 oracle 使用既有 `ezsave.pimg/2157.tlg` 的 tag `0x19`，
同时覆盖 raw borrowed pointer 与 TJS Octet 深拷贝/refcount；详情见
[FOLLOWUP_NATURAL_RESOURCE_ORACLE_2026-08-02.md](FOLLOWUP_NATURAL_RESOURCE_ORACLE_2026-08-02.md)。

扫描器再扩展到 Real 分类/位模式与 String table/index/UTF-8。天然 `0x1D/0x1E/0x1F`
均已选出固定样本；天然 String 只有 `0x15/0x16`，共 8,975,228 个 index 全部表内，
并已选出两条含非 ASCII UTF-8 的 copy/borrowed-pointer oracle。`0x17/0x18/0x2C`、NaN、
Infinity 与 negative-zero 在当前资产中不存在。详见
[FOLLOWUP_NATURAL_REAL_STRING_ORACLE_2026-08-03.md](FOLLOWUP_NATURAL_REAL_STRING_ORACLE_2026-08-03.md)。

扫描器随后补齐 Null/Array/Dictionary 形状，并把三个天然样本接入 public Variant、raw
category、ordered value/no-value `EnumMembers` 与 dispatch-owner refcount oracle。该 oracle
也固定 `NativeInstanceSupport` 的进程期 class-id cache、mismatch output 保留、borrowed
secondary-base 地址、无引用变化及 valid=0 后仍成功边界；同时固定 primary/secondary
address point、offset-to-top/RTTI、完整 32-slot primary 顺序、六个
`Construct/Invalidate/Destruct` lifecycle vslot，以及 19 个 unsupported primary slot 在
valid=1/0 下恒返 `-1002` 且不写 output/对象/引用的边界，并加入 `IsInstanceOf` 在 valid=0
后仍直接分类 node 的边界。
同一 shape oracle 现再以 hidden-sret `X8` 直调 raw `GetRoot@0x598A3C` 与
`Transfer@0x598A64`，固定正常 owner 的 retain/所有权移动/source 清空/terminal release
数据流；这补充运行时可观察面，但不消除 `0x598A64` 的精确 helper/token 证据上限。
随后同一天然 root 又补齐 raw strict/non-strict Dictionary lookup 的 retained child、两级
helper miss 保留 out、覆盖旧 destination、`self==out` alias、IsValid 与 Contains 临时
生命周期；这些同样补观察面，不改变 15 个 stripped/O3 token 上限。
同一 root 现又补 direct `GetDictionaryKeys`：以 24-byte hidden-sret 读取 Android gnustl
`vector<string>` 的三指针、精确 reserve、单指针 COW string、packed 顺序和目标内析构；
这推进内部容器与对象生命周期观察面，但同样不把正常样本结果升级为源码 token 证据。
PSBMedia 现另补不依赖 PSB 内容的 direct interface oracle：完整 11-slot vtable、singleton
非原子引用、固定名称、两个 Normalize no-op、GetLocallyAccessibleName 以及 target-allocated
空对象的 complete/deleting destructor 和 zero-ref underflow。它同样只补可观察生命周期，
不消除 `0x599DD8` 的空字符串 token 上限；详情见
[FOLLOWUP_PSBMEDIA_INTERFACE_LIFECYCLE_ORACLE_2026-08-03.md](FOLLOWUP_PSBMEDIA_INTERFACE_LIFECYCLE_ORACLE_2026-08-03.md)。
同一 `ezsave.pimg` 的 root Dictionary 还直接暴露 `layers` Array；现已固定该天然节点的
SHA-256、offset `0x20b`、count `32` 与 packed prefix，并通过独立 `--media-array`
要求原生 `GetListAt@0x5999F4` 严格回调 `"0".."31"`。这补齐 Array listing 的直接
观察面，但同样不改变 15 个 token 上限；详情见
[FOLLOWUP_PSBMEDIA_ARRAY_ORACLE_2026-08-03.md](FOLLOWUP_PSBMEDIA_ARRAY_ORACLE_2026-08-03.md)。
复扫也首次显式盘点
category-1 tags：
`0x02/0x03/0x27/0x2F/0x33/0x37/0x3B` 在 23,415,372 个可达节点中全部为 0，故当前既
没有天然 true/false，也没有 bool-conversion 异常样本。详见
[FOLLOWUP_NATURAL_SHAPE_LIFECYCLE_ORACLE_2026-08-03.md](FOLLOWUP_NATURAL_SHAPE_LIFECYCLE_ORACLE_2026-08-03.md)
与
[FOLLOWUP_NATURAL_BOOLEAN_INVENTORY_2026-08-03.md](FOLLOWUP_NATURAL_BOOLEAN_INVENTORY_2026-08-03.md)。

| 尚缺边界 | 当前盘点结果 | 下一项合法动作 |
| --- | --- | --- |
| 损坏 MDF / zlib failure | 142 个天然 MDF 全部成功，未发现自然失败样本 | 仅在获得现成、来源明确的自然失败输入后，以 `--input` 运行；不修改现有 MDF |
| filter 后 offset failure | 现有 oracle 索引没有固定到会在真实 filter 后触发 strict refresh false 的自然输入 | 等待现成天然输入及其 filter/seed；记录哈希后复用 |
| 损坏 packed table | 现有 oracle 索引没有固定到自然损坏 packed table | 等待现成天然输入；不从正常 PSB 改字节制造 |
| 自然 tag `0x0B` 极端值 | 112 份唯一 PSB 的 23,415,372 个可达节点中 tag `0x0B` 为 0；已知 tag `0x09` 锚点正常命中 | 新天然资产到手后重跑只读扫描器；命中后固定路径、offset、哈希与原始字节，再扩展 oracle |
| 天然 Boolean/category-1 | `0x02/0x03/0x27/0x2F/0x33/0x37/0x3B` 全部为 0 | 新天然资产到手后重跑只读扫描器；成功值固定 `0x02/0x03`，转换异常只接受其余五种天然 tag，不改字节制造 |
| `>4 GiB` storage | 当前盘点文件均小于等于 `0xFFFFFFFF` | 仅在获得真实大文件时运行；不创建 sparse/合成文件冒充物料 |

本文件形成时曾因 `adb devices` 为空而中止一次 `scenelist.scn --storage --trace`；该历史
状态现已被后续 ARM64 实测取代。固定 ARM64-only APK 上的 raw/scalar/shape/resource/media
成功路径已无 trace 24/24、全量 trace 24/24 通过，详见
[FOLLOWUP_ANDROID_ARM64_RUNTIME_ORACLE_2026-08-03.md](FOLLOWUP_ANDROID_ARM64_RUNTIME_ORACLE_2026-08-03.md)。
这些成功路径不会关闭上表六个仍需新天然物料的失败/极端边界。

## 下一可执行顺序

1. 不等待队列 A 的可选证据；继续用 Android ARM64 二进制逐函数选择最强源码候选并推进
   六维复原。只有目标内出现能区分候选的新正证据时，才顺手消除对应 `_guess`/token
   上限；不得为此转去寻找同版本源码、外部私库或 LFS 对象。
2. 出现天然边界资产时，先记录来源、尺寸、哈希、原始 magic/节点 offset，再复用现有
   oracle；先用 `scan_psbfile_natural_boundaries.py` 做可达节点盘点，不生成或改造 fixture。
3. 已有 24-case ARM64 命令只需作为 runner/设备/目标哈希的非回归门禁重跑；整数、
   Real/String、Null/Array/Dictionary dispatch、raw holder/lookup/key-vector、Resource 与
   media 生命周期的设备观察均已补齐。只有上表出现新天然输入，才扩展对应失败/极端
   case；结果继续与队列 A 的 15 个源码 token 判定分开记录。
