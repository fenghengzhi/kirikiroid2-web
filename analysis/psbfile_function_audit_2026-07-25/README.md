# Android `libkrkr2.so` 内嵌 psbfile：逐函数六维复原审计

## 范围

本目录按
[`../psbfile_function_tree_2026-07-25.md`](../psbfile_function_tree_2026-07-25.md)
中的 114 个独立 emitted 入口逐一审计。唯一权威来源是 Android kirikiroid2
`libkrkr2.so`；不得查看、引用或从任何外部 `psbfile.dll` 推导。

每个 `functions/0xADDR.md` 的叶节点初审只由一个 function-leaf subagent 审计一个地址；
协调者不替 leaf 合并多个函数结论。**该叶节点初审阶段**是只读工作，不修改 `cpp/`。
初审汇总后的 GAP 闭合、独立复核和验收属于后续阶段，须另行满足仓库规定的 Android
反编译证据与修改前置条件，并把结果回写到对应报告和汇总文档。

## 六维判定

每一维只能使用以下状态：

- `MATCH`：现有 Android 证据覆盖的行为与本地实现一致；
- `GAP`：有独立 Android 证据证明本地存在具体偏差；
- `PARTIAL`：部分已对齐，但仍有已定位的未闭合部分；
- `EVIDENCE_LIMITED`：stripped/O3/ABI 合并使源码 token 无法唯一恢复；
- `N/A`：该 emitted 入口确实不涉及该维度，并说明原因。

六个维度固定为：源码结构、数据流、调用链、对象生命周期、内部容器实现、边界行为。
`MATCH` 仅表示二进制当前可证明范围内一致，不宣称 stripped 二进制可以证明不可观察的
原始拼写。

## 单函数报告必备内容

1. 地址、canonical 名称、分组、Android vtable/NCB/调用链归属；
2. 本轮 fresh `decompile` 记录；必要时补 `disasm`/xref；
3. 不超过 10 行、包含分支/默认值/错误边界的 Android 伪代码；
4. 本地文件、符号与精确行号；
5. 六维逐项证据和状态；
6. 总结论：`ALIGNED`、`HAS_GAP` 或 `EVIDENCE_LIMITED`；
7. 若为 `GAP/PARTIAL`，列出最小差异，但不得在本轮修改生产代码。

## 汇总规则

只有当 114 个 canonical 地址各有且仅有一份报告、地址集合与函数树双向相等，并完成独立
一致性复核后，才生成总表。总表不会用多数函数的结论替代任何缺失 leaf。

## 机械校验

运行 `python3 analysis/psbfile_function_audit_2026-07-25/verify_audit.py`。除地址集合、树、
报告 schema 和判定计数外，脚本还会解析 114 份报告中的本地源码行号引用，检查
`cpp/...:line`、`cpp/...:start-end`、多段范围、Markdown“第 N–M 行”和唯一 basename
别名是否落在当前真实文件范围内。`main.cpp` 在本审计中明确解析为
`cpp/plugins/psbfile/main.cpp`；其余 basename 仅在仓库 `cpp/` 下唯一时才接受。因此
Android 地址、日期和版本号不会被当成本地源码行号。

脚本还把当前 `cpp/plugins/psbfile/` 的完整文件集合与
[`SOURCE_SNAPSHOT.sha256`](SOURCE_SNAPSHOT.sha256) 双向比较，并逐文件校验 SHA-256。
新增、删除、改名或修改任一插件文件都会使审计失败。该快照只证明 114 份报告仍对应当前
插件源码，不替代 Android 反编译证据；有意修改 `cpp/` 后必须先重审受影响入口、更新报告，
最后才能更新快照。落地原因、当前构建/测试证据及边界见
[`FOLLOWUP_CURRENT_SOURCE_SNAPSHOT_GATE_2026-08-03.md`](FOLLOWUP_CURRENT_SOURCE_SNAPSHOT_GATE_2026-08-03.md)。

另运行
`python3 analysis/psbfile_function_audit_2026-07-25/verify_elf_surface.py`，可从权威 ELF
`.eh_frame` 独立验证两只 static-init FDE 与 112 只主簇 FDE 的起点集合、逐项连续范围、
上下相邻模块边界；其中前驱 `0x42CE6C/0x596414` 已由 fresh IDA 的
`LayerExImage.dll` 注册和 `noise/generateWhiteNoise/gaussianBlur` vtable 构造归属闭合，
后继仍为 `0x42CFA0/0x59B9C8`。脚本现还机械固定 39 只 LSDA-bearing FDE 与 75 只
unwind-only FDE 的精确集合，并要求所有 LSDA 地址唯一且落在 `.gcc_except_table`。
39 份对应逐函数报告也必须各自保留至少一条 EH/LSDA/landing/cleanup 证据。
在此基础上，全部 39 张 LSDA call-site table 又被逐字段固定：合计 232 项，精确分类为
77 项无 landing、80 项 cleanup-only、75 项 null-type catch-all；39 份报告都必须保留
专用拓扑标记。raw `PSBFile/PSBRawNode` 生命周期簇的 10 张表（51 项）仍作为专用子集
重复校验。
该脚本固定目标 SHA-256；`llvm-dwarfdump` 不在 `PATH`
时用 `--llvm-dwarfdump /absolute/path/to/llvm-dwarfdump` 指定。方法与 fresh 边界归属证据见
[FOLLOWUP_ELF_FDE_SURFACE_VERIFIER_2026-08-03.md](FOLLOWUP_ELF_FDE_SURFACE_VERIFIER_2026-08-03.md)
和
[FOLLOWUP_ELF_LSDA_SURFACE_2026-08-03.md](FOLLOWUP_ELF_LSDA_SURFACE_2026-08-03.md)，
raw 生命周期簇的完整 51-entry 解码见
[FOLLOWUP_RAW_LSDA_CALLSITE_TOPOLOGY_2026-08-03.md](FOLLOWUP_RAW_LSDA_CALLSITE_TOPOLOGY_2026-08-03.md)，
39 张表的全量闭环见
[FOLLOWUP_FULL_LSDA_CALLSITE_TOPOLOGY_2026-08-03.md](FOLLOWUP_FULL_LSDA_CALLSITE_TOPOLOGY_2026-08-03.md)。
同一 verifier 现还直接读取 10 个静态对象表面、177 个 qword，机械固定 `.init_array`
顺序、dispatch 主/次地址点及 offset-to-top/RTTI prefix、media/AutoRegister/adaptor 表和
factory/root/load 三只完整 34-slot typed wrapper 表；证据、源码映射与 IDB 注释见
[FOLLOWUP_STATIC_OBJECT_VTABLE_SURFACE_2026-08-03.md](FOLLOWUP_STATIC_OBJECT_VTABLE_SURFACE_2026-08-03.md)。
同一 verifier 还固定 34 个 data-referenced UTF-8/UTF-16 字面量、canonical 空串的两级
指针链，以及 42 张 switch table 的 915 个 signed-relative 槽；所有 destination 必须
对齐并位于 owner MANIFEST FDE。完整 xref 分类、case partition、共享全局与源码对照见
[FOLLOWUP_LITERAL_SWITCH_GLOBAL_SURFACE_2026-08-03.md](FOLLOWUP_LITERAL_SWITCH_GLOBAL_SURFACE_2026-08-03.md)。
同一 verifier 最后直接解码 114 个 FDE 的全部 `BL/B/BLR/BR`，固定 567 个 transfer
site、86 个 direct target、39 条 MANIFEST 内调用边与 54 个无非-switch-transfer 函数；
完整 direct/indirect/tail 分类见
[FOLLOWUP_CALLSITE_SURFACE_2026-08-03.md](FOLLOWUP_CALLSITE_SURFACE_2026-08-03.md)。
其后又把 46 个非 switch indirect transfer 的目标寄存器 producer 独立固定：44 个
fixed-offset `LDR`、2 个 Itanium member-pointer register-offset `LDR`，共 18 类 ABI 角色；
完整站点、虚表槽/字段语义与源码对照见
[FOLLOWUP_INDIRECT_ABI_SURFACE_2026-08-03.md](FOLLOWUP_INDIRECT_ABI_SURFACE_2026-08-03.md)。
同一直接调用集合的 65 个 MANIFEST 外 target 也已逐个语义化：435 个 site 全部归入
EH/runtime、allocation/memory/compression、libstdc++、storage/stream、script global、
diagnostic/log、ttstr/narrow、ncbind 与 Variant/closure 九类；32 个 `sub_*` 已 fresh
decompile、加 `_guess` 名并保存 IDB。详见
[FOLLOWUP_EXTERNAL_CALLEE_SURFACE_2026-08-03.md](FOLLOWUP_EXTERNAL_CALLEE_SURFACE_2026-08-03.md)。
同一 verifier 现还解码完整 stack-frame/local-lifetime surface：114 个入口精确分为
57 framed + 57 frameless，framed 中 52 个入口建帧、5 个诊断慢路径 shrink-wrap；
31 个 canary、10 种 callee-saved GPR 集合与唯一 `D8` spill 均逐函数固定。39 个 LSDA
owner 全部 framed，另有 18 个 framed-but-unwind-only；fresh 反编译与本地 RAII scope
对照未发现生产 GAP。详见
[FOLLOWUP_STACK_FRAME_LIFETIME_SURFACE_2026-08-03.md](FOLLOWUP_STACK_FRAME_LIFETIME_SURFACE_2026-08-03.md)。
同一 verifier 现还把 44 个 MANIFEST 内 direct transfer 从边集合推进为逐调用点 ABI
contract：21 类参数角色、8 类返回消费、2 个 hidden-sret、1 个 non-trivial Variant
by-value、2 个 `uint32 → size_t` zero-extension 与 ncbind 的两只空 tag-reference 均由
producer word 固定。`CopyFirstArgument_guess@0x59B708` 的四参数 prototype 已按 caller
正证据修正并保存 IDB；本地 raw/media/ncbind 分层无生产 GAP。详见
[FOLLOWUP_INTERNAL_CALL_CONTRACT_SURFACE_2026-08-03.md](FOLLOWUP_INTERNAL_CALL_CONTRACT_SURFACE_2026-08-03.md)。
其后对 114 个函数的 typed-member ctree 表面做首次独立枚举：当时的 483-row 旧基线
精确分为 `R=312/W=147/RW=10/address=14`；416 条 instruction-backed 行折叠为 385 个
唯一 word/62 个 owner FDE 并接入 ELF 门禁。该轮裸指针复核补回 7 个 IDB 类型传播缺口；
补型后 fresh 反编译直接显示 owner/header/raw node/dispatch/media/ncbind 的字段链。该统计
后来由完整 `cot_ptr/cot_idx` 复扫继续纠正，不能再称为最终完整表面。详见
[FOLLOWUP_TYPED_MEMBER_FIELD_SURFACE_2026-08-03.md](FOLLOWUP_TYPED_MEMBER_FIELD_SURFACE_2026-08-03.md)。
最后把 39 张 LSDA 表的 landing cleanup contract 也展开到逐指令层：155 条非零引用折叠为
150 个唯一 landing，精确分成 75 个 cleanup-only 与 75 个 catch-all；前者全部
`_Unwind_Resume`，后者为 72 个直接 terminate 与 3 个 catch/delete/rethrow。显式 ARM64
CFG 的 569 个唯一 landing 指令与正常流零交叉，168 个 transfer 及全部 successor 已由
canonical digest 固定；本地 Factory、stream、Variant/ttstr/vector/ncbind 清理层与故意
保留的 raw-data 异常泄漏边界一致，未发现生产 GAP。详见
[FOLLOWUP_LANDING_CLEANUP_CONTRACT_SURFACE_2026-08-03.md](FOLLOWUP_LANDING_CLEANUP_CONTRACT_SURFACE_2026-08-03.md)。
其后对互补的 entry-rooted normal CFG 做独立解码：4,956 条正常流指令与 569 条
landing-only 指令零交叉并完整覆盖 5,525 条 FDE 指令。208 个正常终点精确分为
162 `RET`、11 direct tail、1 indirect tail 与 34 true-noreturn；114 个 prototype 又分为
37 void、71 `W0/X0`、5 hidden-sret `X8`、1 `D0`。28 个 TVP/TJS diagnostic 调用保留
helper-return fallback，不能套用 landing 的 noreturn 集；当前源码的默认值、X8 对象返回与
void tail 均一致，未发现生产 GAP。详见
[FOLLOWUP_NORMAL_CFG_TERMINAL_CONTRACT_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_CFG_TERMINAL_CONTRACT_SURFACE_2026-08-03.md)。
其后把 162 个 `RET` 中的 129 个 source-visible W0/X0/D0 value return 继续闭合到完整
producer surface：72 个 owner 共形成 160 条 reaching-definition 关系，精确分为 157 个
instruction writer、2 个 direct call return 与 1 个 indirect call return；112 个 RET 为
单来源，17 个为多来源 join，入口残留与未声明 call-clobber 均为 0。96 个 W0、19 个
X0 与 14 个 D0 返回的 success/error/null/diagnostic-default/callback 值均由 exact
word、destination 与 predecessor path 固定，当前源码未发现生产 GAP。详见
[FOLLOWUP_NORMAL_RETURN_VALUE_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_RETURN_VALUE_SURFACE_2026-08-03.md)。
其后从反方向闭合全部 continuing normal call 的 GPR0 结果首事件：57 个 owner 的
272 `BL` 与 39 `BLR` 共形成 419 条关系，精确分为 86 个显式 use、250 个无读取
overwrite、49 个中性 call-boundary 与 34 个 `RET` reach。147 个 direct-GPR 中 71 个、
39 个 indirect 中 15 个被显式读取；125 个 direct-void 的读取数严格为 0。34 个
`RET` reach 又全部属于 28 void + 5 hidden-sret + 1 FP owner，GPR-return owner 为 0，
因此没有把寄存器残留误判为源码返回。四条 pre-event loop 的有限 exit 与 311 个调用点
均已由 exact word/mask/CFG 门禁固定，当前源码未发现生产 GAP。详见
[FOLLOWUP_NORMAL_CALL_RESULT_FIRST_EVENT_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_CALL_RESULT_FIRST_EVENT_SURFACE_2026-08-03.md)。
其后从输入侧闭合全部 normal direct transfer 的参数 producer：59 个 owner 中精确枚举
306 个 `BL` 与 11 个 out-of-owner tail `B`，80 个目标的 prototype 共形成 446 个纯寄存器
参数和 475 条 reaching-definition 关系。447 个显式 writer、18 个入口参数与 10 个前序
`BL` return 全部沿 normal predecessor 闭合；11 个多来源参数最大 13 路，7 个 hidden
`X8`、1 个 `D0` 与三条 pre-index `STR` writeback 也逐字固定。未声明 volatile call
clobber、entry residue、stack/scattered argloc 均为 0；当前 packed/raw/media/ncbind 参数
链未发现生产 GAP。详见
[FOLLOWUP_NORMAL_DIRECT_ARGUMENT_PRODUCER_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_DIRECT_ARGUMENT_PRODUCER_SURFACE_2026-08-03.md)。
其后补齐输入侧的最后一类调用：46 个非 switch indirect transfer 的 117 个 `X0..X7`
语义参数沿 normal/LSDA 两类显式 CFG 闭合为 120 条 producer 关系。40 个站点正常入口
可达，6 个只在 landing cleanup 中可达；119 个 instruction writer 与唯一 deleting-
destructor entry `X0` 全部逐字固定，未声明 call clobber/entry residue 为 0。该轮还用
call-operand 类型纠正 `0x59B1A8` 的 stale `X4` 假第五参，并固定两处八参 `FuncCall`。
当前 callback/vtable/manager/member-pointer 参数链未发现生产 GAP。详见
[FOLLOWUP_INDIRECT_ARGUMENT_PRODUCER_SURFACE_2026-08-03.md](FOLLOWUP_INDIRECT_ARGUMENT_PRODUCER_SURFACE_2026-08-03.md)。
其后继续闭合 typed-member 的写值侧：带真实 EA 的 `W=105/RW=3` 共形成 108 个字段
写事件、101 个 store 站点与 109 条 producer 关系。107 个事件单来源，唯一两路 join 是
`std::vector<std::string>::end` 的两条分配分支；84 个显式 writer、3 个构造入口参数与
22 个 ZR 初始化均由 selected store operand、exact word 和 normal predecessor path 固定，
未声明 call clobber/entry residue 为 0。四组完整 11-field header population、Resolve
成功尾部 output commit 与 ncbind field bundle 均和当前源码一致，无生产 GAP。详见
[FOLLOWUP_TYPED_MEMBER_WRITE_VALUE_PRODUCER_SURFACE_2026-08-03.md](FOLLOWUP_TYPED_MEMBER_WRITE_VALUE_PRODUCER_SURFACE_2026-08-03.md)。
其后闭合互补的读值/取址侧：带真实 EA 的 `R=297/RW=3/address=11` 共 311 条
typed-member 语义事件，落在 290 个站点与 61 个 owner。267 条 direct-producer 语义行
展开为 268 个 lane、288 条 first-event relation；44 条 residual-anchor 语义行展开为
45 个 lane、45 条单来源关系。37 个 call-use 全部由既有 ABI argument manifest 证明，
pre-event loop、landing-only 与未声明 volatile call-clobber 均为 0。packed/header、
OwnerFilter、media raw resource 与 ncbind member-pointer 数据流均和当前源码一致，无生产
GAP。详见
[FOLLOWUP_TYPED_MEMBER_READ_CONSUMER_SURFACE_2026-08-03.md](FOLLOWUP_TYPED_MEMBER_READ_CONSUMER_SURFACE_2026-08-03.md)。
其后收口 typed-member 表面最后 67 条没有独立 ctree EA 的 optimizer-synthetic 语义行：
33 个 owner 的 `W=42/R=15/RW=7/address=3` 展开为 73 个唯一机器锚点，精确分成
62 个 assignment/RMW/address/cast coalesced store、6 个 direct `LDR`、3 个
`std::function` manager `BLR` 与 2 个 packed-tag switch `BR`。全部锚点 normal-entry
reachable；三条 BLR 与两条 BR 分别由独立 indirect ABI / switch manifest 交叉证明，
且 `416 EA-backed + 67 synthetic = 483` 重新闭合当时的旧基线。当前 dispatch 双 vptr、
OwnerFilter、raw-node switch、adaptor、paramsFunctor 与 vector commit 仍无生产 GAP。详见
[FOLLOWUP_TYPED_MEMBER_SYNTHETIC_REALIZATION_SURFACE_2026-08-03.md](FOLLOWUP_TYPED_MEMBER_SYNTHETIC_REALIZATION_SURFACE_2026-08-03.md)。
随后完整枚举当前 IDB 的 `cot_ptr/cot_idx` 表面并逐行绑定机器锚点：又以
`Transfer@0x598A64` 和 `Resolve@0x59A4B0` 的 field/consumer 正证据提升 5 条 read-only
typed rows，故当前 typed-member 总数纠正为
`488 = 421 EA-backed + 67 synthetic`（`R=317/W=147/RW=10/address=14`，390 个唯一
EA 站点）。剩余 raw surface 为 `667 = 461 cot_ptr + 206 cot_idx`，形成 610 个唯一
normal-entry 锚点，610/610 exact word 与 CFG 门禁均通过；本地 packed/raw/media/TJS/
NCB/STL 数据流无生产 GAP。详见
[FOLLOWUP_RAW_MEMORY_DEREFERENCE_SURFACE_2026-08-03.md](FOLLOWUP_RAW_MEMORY_DEREFERENCE_SURFACE_2026-08-03.md)。
随后把调用、实参、返回 first-event、normal CFG 与 LSDA landing 合并为直接堆生命周期
门禁：20 个 allocator result 精确分为 `15 operator new + 4 aligned alloc + 1 Octet alloc`，
并闭合到 68 个发布/转移/清理锚点、8 条 constructor/copy 异常清理边。五个 direct
release/refcount-helper target 的完整 census 为 120 site（`83 normal + 37 landing`；
`80 raw/object/storage + 40 shared release`），另固定 3 条正常与 8 条异常的所有权丢失
边界。fresh 逐段对照确认当前 Factory、Load/LoadStorage/Adopt、media cache、ncbind 与
old-libstdc++ vector 生命周期均一致，无生产 GAP、无 `cpp/` 修改。详见
[FOLLOWUP_HEAP_ALLOCATION_LIFECYCLE_SURFACE_2026-08-03.md](FOLLOWUP_HEAP_ALLOCATION_LIFECYCLE_SURFACE_2026-08-03.md)。
随后把 shared-release 调用 census 推进成完整引用计数状态机：6 个初值、10 个非原子
retain 三元组、19 个 owner decrement-release、3 个优化折叠零探针与 16 对独占原子
指令全部获得 exact-word/owner/终结契约。16 对原子指令完整分成 9 个 pthread-gated
COW decrement 与 7 个 shared-string retain；15 个 virtual 引用转换和 7 个 direct helper
target 的 93 个站点又与既有 ABI/callee surface 交叉，并固定 10 个外部 helper body。
24 个 aligned-dealloc 现严格分为 `19+3+1+1`，本地 intrusive owner、dispatch、media、
Variant String/Octet/Object 与 ncbind 顺序无生产 GAP。详见
[FOLLOWUP_REFERENCE_COUNT_STATE_MACHINE_SURFACE_2026-08-03.md](FOLLOWUP_REFERENCE_COUNT_STATE_MACHINE_SURFACE_2026-08-03.md)。
随后把 114 个 FDE 对 `.bss` 的引用从零散文字说明推进为完整全局状态机门禁：94 行
DataRef 精确覆盖 16 个目标、22 个 owner 与 93 个唯一站点，角色为
`29 address + 31 read + 34 write`，CFG 分区为 `84 normal + 10 landing`。9 个语义对象归入
6 个生命周期家族；lazy native ID、两只 AutoRegister 静态对象、PSBMedia pointer/guard、
class-info 的六组 set/clear 转换、三路注册链头及 COW empty representation 均固定 exact
word、发布顺序和异常边。本地源码逐项一致，无生产 GAP、无 `cpp/` 修改。详见
[FOLLOWUP_GLOBAL_BSS_STATE_MACHINE_SURFACE_2026-08-03.md](FOLLOWUP_GLOBAL_BSS_STATE_MACHINE_SURFACE_2026-08-03.md)。
随后闭合 `.rodata/.bss` 之外的 initialized-data / relocation 表面：39 条
`.data.rel.ro` xref 覆盖 18 个目标、13 个 owner；48 条 GOT 指令组成 24 组
`ADRP→LDR`，精确分为 `16 normal + 8 landing`，并落到 4 个 slot、5 条动态 relocation。
另固定三张 base/interface vtable 的 50 个 qword、12 个 Itanium address point 与 24 次
constructor/destructor vptr 发布。fresh 反编译确认 dispatch 双继承、typed wrapper 的
embedded interface、media/class/adaptor 生命周期及 COW/pthread/canonical-empty 链均与
本地一致，无生产 GAP、无 `cpp/` 修改。详见
[FOLLOWUP_INITIALIZED_DATA_RELOCATION_SURFACE_2026-08-03.md](FOLLOWUP_INITIALIZED_DATA_RELOCATION_SURFACE_2026-08-03.md)。
随后把残余 code-address-like DataRef 闭合为完整取址调用面：27 条 `.text` 引用中只有
10 行是真实 callable materialization，组成 7 个 target；其余为 15 个 packed
`0xFFFFFF` mask 与 2 个 MDF magic，另有 1 个落进 `.plt` 的 PSB magic。7 个 target 精确
覆盖 pre-register、Factory、`root`/`load` member pointer、CreateEmptyAdaptor、finalize 和
dummy constructor callback。18 条伪 dref 已在 IDB 改回 numeric 并删除；新门禁自行解码
AArch64 logical/MOV-wide immediate，当前本地宏和 wrapper 调用链无生产 GAP、无 `cpp/`
修改。详见
[FOLLOWUP_ADDRESS_TAKEN_CALLABLE_SURFACE_2026-08-03.md](FOLLOWUP_ADDRESS_TAKEN_CALLABLE_SURFACE_2026-08-03.md)。
随后闭合 DataRef census 的最后 20 行 no-segment target：它们不是外部地址或假引用，而是
IDA 为 typed stack-local 字段生成的 7 个 `0xFF...` stroff identity。20 行对应 7 个
owner、19 个机器站点与 24 个字段事件，精确分为 `7 read + 13 write`、
`18 normal + 2 landing`；两条 landing 是 `PSBFile::Load` 空 `std::function` 临时的
异常析构 manager read。raw-node 双清零、Resolve current.node 替换/延迟提交、NCB
delegate 默认状态与 PropSet params functor 均由 SP-relative 指令解码、CFG 和 canonical
SHA 固定。本地源码逐项一致，无生产 GAP、无 `cpp/` 修改。详见
[FOLLOWUP_STACK_LOCAL_DREF_SURFACE_2026-08-03.md](FOLLOWUP_STACK_LOCAL_DREF_SURFACE_2026-08-03.md)。
随后闭合完整数值 ctree 表面，并与机器立即数明确分层：114 个 FDE 共 1,181 条
cot_num，覆盖 95 个 owner，精确分为 1,133 条具体 EA 与 48 条 optimizer-synthetic；
全部映射到 1,055 个 normal-entry exact-word anchor。独立 o_imm census 则有 1,208 行/
1,160 site，两集合只有 485 个 site 交集；其余 machine-only ADD/ADRL/SUB/MRS/ADRP
不能直接当作 portable C++ token。classifier、packed width、错误码、flag、callback
arity、numeric shift/default 与当前源码一致，无生产 GAP、无 cpp/ 修改。详见
[FOLLOWUP_NUMERIC_CTREE_SURFACE_2026-08-03.md](FOLLOWUP_NUMERIC_CTREE_SURFACE_2026-08-03.md)。
随后把局部变量与局部对象面从 frame/LSDA 的机器侧证据推进到完整 Hex-Rays
`lvar/cot_var` 表面：114 个 FDE 共 1,056 个声明，覆盖
`311 argument / 111 stack / 945 register / 72 result / 58 byref`；3,073 条使用覆盖
770 个声明，另有 286 个未进入最终 ctree 的 ABI/result/O3 中间声明。1,686 条
EA-backed 与 1,387 条 synthetic 使用全部绑定到 2,214 个 normal-entry exact-word
anchor，landing-only 为 0。fresh 反编译确认 Variant、ttstr/COW string、OwnerFilter、
raw node、paramsFunctor、hidden result 与 wrapper 临时量的声明顺序、作用域和逆序析构
均与当前源码一致；未发现生产 GAP、无 `cpp/` 修改。后续完整表达式树交叉又纠正旧
LVS1 中两个“同一 lvar 同时作为调用首参和尾参”时由 `equal_effect` 误选首参的
relation，现分别固定为 `0x59A968:17→a4` 与 `0x59AD84:30→a5`，其余 3,071 行
不变。详见
[FOLLOWUP_LVAR_CTREE_SURFACE_2026-08-03.md](FOLLOWUP_LVAR_CTREE_SURFACE_2026-08-03.md)。
随后闭合完整 Hex-Rays statement/control-tree：114 个 FDE 共 2,922 个 `cinsn/cit_*`
节点，覆盖 12 种 op、119 种完整 relation 与 122 种 detail；2,882 个 concrete 节点和
40 个 optimizer-synthetic 节点全部绑定到 1,942 个 normal-entry exact-word anchor。
block/if/loop/switch 的父子次序、46 个 goto 到 36 个同 owner label、synthetic parent
anchor 与最大深度 15 均由独立门禁固定。fresh 反编译确认唯一 `for`/`continue`、嵌套
while、最深 switch/cleanup 树与 Resolve 的 RAII loop 都能由当前源码加 O3/STL 展开解释；
未发现生产 GAP、无 `cpp/` 修改。详见
[FOLLOWUP_STATEMENT_CTREE_SURFACE_2026-08-03.md](FOLLOWUP_STATEMENT_CTREE_SURFACE_2026-08-03.md)。
随后闭合完整 Hex-Rays `cexpr/cot_*` 表达式树：9,629 个节点覆盖 108 个 owner、
42 种 op、17 种 relation、271 种类型与 360 种 operator detail；其余 6 个 owner 是
已知 nullsub。7,077 个 concrete 与 2,552 个 optimizer-synthetic 节点全部落到
3,076 个 normal-entry exact-word anchor，最大深度 11。门禁同时逐行交叉
1,935 个 statement root、1,181 个 `cot_num` 与修正后的 3,073 个 `cot_var`，
零差异；fresh 代表函数对照未发现生产 GAP、无 `cpp/` 修改。详见
[FOLLOWUP_EXPRESSION_CTREE_SURFACE_2026-08-03.md](FOLLOWUP_EXPRESSION_CTREE_SURFACE_2026-08-03.md)。
随后从 ECT1 派生完整 `cot_call` 调用表达式面：405 个节点精确分成
`285 direct + 40 computed indirect + 80 helper intrinsic`，覆盖 638 个有序实参与
405 个唯一 normal-entry anchor。325 个真实 transfer 全部逐站点命中机器 callsite；
机器侧额外的 `194 direct + 6 indirect` 保持为 EH、canary、implicit lifetime 与
compiler lowering。40 个 computed call 恰等于独立 indirect surface 的 40 个 normal
站点，余下 6 个严格 landing-only；fresh direct/indirect/helper/landing 对照无生产 GAP、
无 `cpp/` 修改。详见
[FOLLOWUP_CALL_EXPRESSION_SURFACE_2026-08-03.md](FOLLOWUP_CALL_EXPRESSION_SURFACE_2026-08-03.md)。
随后从同一 ECT1 派生完整 `cot_obj` 对象/地址叶节点面：457 个节点覆盖 70 个 owner、
154 个唯一目标、101 种类型与 447 个 normal-entry anchor，并按权威 ELF section 形成
八类互斥分区：`285 direct callee + 7 address-taken callable + 18 numeric artifact +
47 literal + 24 address point + 1 literal pointer + 70 BSS object/subobject + 5 extern`。
所有类别分别与 call、callback/artifact、literal、initialized-data、global-BSS 和 GOT
重定位面集合闭合。fresh 对照无生产 GAP；同时由 `base+0x18` 字符子对象、`data-24`
sentinel 与同 ELF 同类符号尺寸把 COW empty storage 从旧记载的 8 bytes 纠正为 32 bytes。
详见
[FOLLOWUP_OBJECT_EXPRESSION_SURFACE_2026-08-03.md](FOLLOWUP_OBJECT_EXPRESSION_SURFACE_2026-08-03.md)。
最后三类 ECT 叶节点随后也被独立闭合：`4 cot_fnum + 3 cot_empty + 111 cot_helper`
覆盖 39 个 owner、87 个 normal-entry anchor。80 个 helper callee 与 `cot_call` helper
集合完全相等，31 个 `TPIDR_EL0` 参数逐个与 `_ReadStatusReg` 共用 MRS anchor；四个
binary64 常量另固定实际 producer，三只 empty 则固定 infinite-loop/void-return statement
归属。完整 9,629-row tree 因此严格分为 4,829 leaf + 4,800 internal，无未分类节点；
fresh 对照无生产 GAP、无 `cpp/` 修改。详见
[FOLLOWUP_RESIDUAL_LEAF_EXPRESSION_SURFACE_2026-08-03.md](FOLLOWUP_RESIDUAL_LEAF_EXPRESSION_SURFACE_2026-08-03.md)。
随后继续闭合完整 member-expression 链：ECT1 中 625 个 `cot_memptr/cot_memref`
覆盖 63 个 owner、478 个 normal-entry exact-word anchor，精确分为 539 个最外层
表达式与 86 个嵌套 base 节点。既有 typed-member 面本来就有意只投影最外层；其
419 个 concrete signature 与 60 个 synthetic signature 现和 outer 集合完全相等，
新增门禁则把此前未独立固定的 86 个 nested 节点、六种完整链形与 537 个 variable /
2 个 helper-call terminal base 全部闭合。fresh owner→header→names/entries 代表链对照
无生产 GAP、无 `cpp/` 修改。详见
[FOLLOWUP_MEMBER_EXPRESSION_CHAIN_SURFACE_2026-08-03.md](FOLLOWUP_MEMBER_EXPRESSION_CHAIN_SURFACE_2026-08-03.md)。
随后闭合数量最大的内部 operator——完整 `cot_asg` 面：1,123 个 assignment
覆盖 69 个 owner、1,052 个 normal-entry exact-word anchor，并按直接 lhs 形成
`804 lvar + 158 member + 109 raw memory + 32 global object + 20 helper pseudo-lvalue`
五类互斥分区。五类分别与 LVS1、typed-member、RMC2、global-BSS 与 residual helper
面交叉；2 个 nested raw write、6 个 ECT-only member realization、2 个 STP 第二 lane
source subobject 和 4 个 landing-only global cleanup write 均保持各自层次。fresh 对照
无生产 GAP、无 `cpp/` 修改。详见
[FOLLOWUP_ASSIGNMENT_EXPRESSION_SURFACE_2026-08-03.md](FOLLOWUP_ASSIGNMENT_EXPRESSION_SURFACE_2026-08-03.md)。
其余全部内部 operator 随后也被一次性闭合：1,980 个节点精确分为
`744 arithmetic + 689 cast + 257 predicate + 252 ref + 36 mutation + 2 comma`，
覆盖 74 个 owner、1,352 个 normal-entry exact-word anchor。`ref` 的 local/member/raw/
object 四路地址投影、mutation 的 local/member/raw 三路 RMW 投影均与既有独立面双向
交叉；`405 call + 625 member + 667 raw + 1,123 assignment + 1,980 remainder = 4,800`
又完整分割所有 internal 节点。fresh native-instance/numeric/refcount/vector/media 代表链
对照无生产 GAP、无 `cpp/` 修改。详见
[FOLLOWUP_REMAINING_INTERNAL_EXPRESSION_SURFACE_2026-08-03.md](FOLLOWUP_REMAINING_INTERNAL_EXPRESSION_SURFACE_2026-08-03.md)。
随后把完整 ECT1 表达式树反投影到全部 normal CFG：3,076 个唯一表达式 anchor 与
1,880 条 machine-only residual 严格组成 4,956 条 normal-entry 指令。residual 精确分成
781 computation、554 memory、370 branch、143 return、32 call；memory 又完整分成
418 stack、42 switch-table 与 94 条语义 lowering。后 94 条逐项与 global/GOT/refcount
独立表面、vtable/tag/closure/output/reload 形状交叉，最后三个未命名站点也经 fresh
反编译闭合。无生产 GAP、无 `cpp/` 修改。详见
[FOLLOWUP_SOURCE_MACHINE_BRIDGE_SURFACE_2026-08-04.md](FOLLOWUP_SOURCE_MACHINE_BRIDGE_SURFACE_2026-08-04.md)。
随后把 MANIFEST 的 caller 方向从 IDA xref 普查升级为权威 ELF 反向门禁：完整 `.text`
实际只有 303 个外部 direct `BL`，落到 15 个 psbfile 入口、25 个 owner FDE 与 71 组
owner→target；旧口径多出的两条是本地 weak `vector<string>` 定义的 PLT/dynsym alias，
不是 consumer call。fresh 复核同时闭合 `LoadedResourceRecord` 构造/rollback/逆序析构、
atlas record vector 清理与 ObjSource 的 native/adaptor `native && !sticky` 生命周期；12 个
FDE、9 个 raw-owner release site、2 条 helper edge 和七 qword adaptor vtable 已进入
`verify_elf_surface.py`。当前源码一致，无生产 GAP、无 `cpp/` 修改。详见
[FOLLOWUP_EXTERNAL_CONSUMER_LIFECYCLE_SURFACE_2026-08-04.md](FOLLOWUP_EXTERNAL_CONSUMER_LIFECYCLE_SURFACE_2026-08-04.md)。
随后闭合 direct caller 的非直接对偶：完整 `.text` 只有 8 个 MANIFEST-page ADRP
候选，7 个是 MANIFEST 内四个注册 owner 的 callback/member pointer，第八个精确解析到
独立 `PackinOne.dll@0x59B9C8`，只是同页碰撞；`ADR/LDR-literal/MOV-wide/logical-
immediate` 均为 0。完整 `.rela.dyn/.rela.plt` 又把剩余地址承载面严格分为
`2 init-array + 60 vtable + 2 weak STL PLT alias`，共覆盖 71 个唯一 MANIFEST target；
没有外部业务 FDE 取 MANIFEST 函数地址。psbfile 专属 vtable header/address point 也无
外部引用，共享 `ncbind` 基表的跨模块引用保持为共享模板证据。无生产 GAP、无 `cpp/`
修改；详见
[FOLLOWUP_INVERSE_POINTER_REFERENCE_SURFACE_2026-08-04.md](FOLLOWUP_INVERSE_POINTER_REFERENCE_SURFACE_2026-08-04.md)。
随后沿唯一非空跨模块 `OwnerFilter` 继续闭合 producer→global→consumer→invoker：13 个
FDE 精确分为 2 个 psbfile MANIFEST 入口与 11 个 motionplayer 生产/消费入口；原先被 IDA
合并的 `0x6A87D0` 薄包装器与 `0x6A87E8` copy-assignment 已按独立 `.eh_frame` FDE 拆开。
完整门禁固定三处 TU-static 地址物化、四个 manager/invoker callable、六条 direct edge、
`1×op2 + 3×op3` manager 调用，以及 `Adopt@0x598858` 唯一实际 invoker。seed 的 8-byte
capture/低 W32 消费、TJS closure 控制块、临时与旧全局目标的独立析构、LoadStorage 到
Adopt 的 const-ref 转发均与当前源码一致；无生产 GAP、无 `cpp/` 修改。详见
[FOLLOWUP_OWNER_FILTER_BRIDGE_LIFECYCLE_SURFACE_2026-08-04.md](FOLLOWUP_OWNER_FILTER_BRIDGE_LIFECYCLE_SURFACE_2026-08-04.md)。
继续向上回溯两个 setter 的注册 owner 后，发现本地模块入口仍把 Android 的单一
`emoteplayer.dll` callback 拆成 pre/post 两段，并在 `motionplayer.dll` 主表提前挂了
EmotePlayer。本轮已恢复 Android 的单函数顺序：加载 motionplayer、取得 Motion、创建并
挂接完整 EmotePlayer class、复用同一 Variant 取得 ResourceManager、依次用同一 method
Variant 注入 seed/func setter；本地 guard、第二只 method Variant 与额外 global Release
均已删除。ARM64 门禁现固定 8 FDE、3 个唯一 callback 物化、11 条入口/类加载边、Motion
registrar 的 11 条 subclass edge 及零 EmotePlayer 引用。随后又把 Motion 恢复为精确的
`23 constants -> 11 subclasses -> 2 functions` 单一注册流：Player 是第六个 subclass，
不再存在 `global.Player`、post alias、deferred free-function attach 或 `useD3D` descriptor
覆盖。`motionplayer_static_init` 又证明模块没有自定义 pre/post/unregister callback，本地
两个 `ShortCutInitial*KeyMap` 字典表达式及三只 callback node 已删除。扩展门禁固定 23 条
constant edge、两个 callback 物化、两个 member-add、完整 motion static-init FDE 及两个
forbidden UTF-16 literal 零命中；21/21 cases、1555/1555 assertions 与 Web Debug link
通过。`cpp/plugins/psbfile/` 本体没有变化，
99/15/0 与源码快照统计不变。详见
[FOLLOWUP_EMOTE_REGISTRATION_INJECTION_SURFACE_2026-08-04.md](FOLLOWUP_EMOTE_REGISTRATION_INJECTION_SURFACE_2026-08-04.md) 与
[FOLLOWUP_MOTION_NAMESPACE_REGISTRAR_SURFACE_2026-08-04.md](FOLLOWUP_MOTION_NAMESPACE_REGISTRAR_SURFACE_2026-08-04.md)。
其后继续细化同一 normal CFG 的全部条件谓词：66 个 owner 的 437 个
`B.cond/CBZ/CBNZ/TBZ/TBNZ` 精确形成 874 条 taken/fallthrough edge；180 个 `B.cond`
全部沿唯一线性 predecessor 恢复到 NZCV producer，结果为
`176 CMP + 3 CMN + 1 SUBS`、未解析 0。condition code 又固定为
73 equality、84 unsigned、23 signed；bit 0 bool、bit 10 `TJS_MEMBERMUSTEXIST` 与 bit 31
signed error/negative index 的用途没有混淆。本地 packed/dispatch/raw/media/ncbind 的
有符号性与标志位逐项一致，未发现生产 GAP。详见
[FOLLOWUP_NORMAL_BRANCH_PREDICATE_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_BRANCH_PREDICATE_SURFACE_2026-08-03.md)。
其后又把 180 个 `CMP/CMN/SUBS` 从“哪条指令产生 NZCV”推进到完整输入数据流：48 个
owner 的 180 个 producer 共读取 260 个寄存器 operand，沿 normal CFG 闭合为 320 条
reaching-definition 关系。235 个输入为单来源，25 个为多来源 join，最大 9 路；313 个
显式 writer、4 个入口参数与 3 个 `W0/X0` call return 均由 exact word/destination 固定，
volatile call-clobber 为 0。trie/dictionary、classifier diagnostic fallback、packed count、
vector/COW-string 和 signed error 边界均与当前源码一致，未发现生产 GAP；详见
[FOLLOWUP_NORMAL_NZCV_INPUT_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_NZCV_INPUT_SURFACE_2026-08-03.md)。
其后把 216 个 `CBZ/CBNZ` 与 41 个 `TBZ/TBNZ` 从“branch 测试哪个寄存器/bit”继续推进到
完整 reaching-definition contract：57 个 owner 的 257 个 branch 共闭合 287 条来源，
精确分为 246 个显式 instruction writer、14 个入口参数与 27 个 `W0/X0` call return；
244 个单来源和 13 个多来源 join 的每一条 predecessor 路径均已闭合，volatile
`X1..X18` call clobber 为 0。producer word、destination operand、source class 与
6,498-byte canonical manifest 现均由同一 ELF verifier 固定。本地 packed/dispatch/raw/
media/ncbind 的默认值、对象临时、bool/TJS error 与 ARM member-pointer low-bit 路径一致，
未发现生产 GAP。详见
[FOLLOWUP_NORMAL_CB_TB_PRODUCER_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_CB_TB_PRODUCER_SURFACE_2026-08-03.md)。

对 Git 历史第二份 Android ARM64 构建另运行
`python3 analysis/psbfile_function_audit_2026-07-25/verify_historical_arm64_lineage.py`
（Xcode 工具不在默认解析路径时显式传 `--llvm-dwarfdump` 与 `--objdump`）。该独立门禁
只验证历史制品是否提供新 retained boundary；不参与当前权威 ELF 的总判定。

脚本还会从当前源码自动扫描 `PSBFileFactory`、`NCB_REGISTER_CLASS(PSBFile)`、
`PSBFile::GetRootDispatch`、`PSBFile::{Load,LoadStorage,Adopt,GetRoot}` 的定义范围，并要求各
canonical 关联报告至少有一个源码引用与真实定义范围相交。定义行号不写死，源码移动后若
报告未同步，校验会直接失败。

## 非权威外部旁证

Windows `psbfile.dll` 的本地静态谱系调查及其不可采信边界单独记录在
[EXTERNAL_LINEAGE_NON_AUTHORITY_2026-07-26.md](EXTERNAL_LINEAGE_NON_AUTHORITY_2026-07-26.md)。
该材料只用于排除错误证据路径，不参与 Android 逐函数判定，也不得驱动 `cpp/` 修改。

官方 Android/iOS 1.3.9 release 的同源映射、iOS 保留的 `PSBFile.cpp` assert 名、helper
边界及其非权威限制见
[ANDROID_139_IOS_LINEAGE_2026-07-26.md](ANDROID_139_IOS_LINEAGE_2026-07-26.md)。
其中任何 source-factorization 选择都必须先由权威 Android arm64 自身的完整数据流或
inline clone 独立约束；iOS arm64 只作独立谱系复核。

当前已按该规则恢复
`CreateVariant_guess/GetTypeCategory/IsInstanceOf/EnumMembers/GetCount/PropGet/PropGetByNum/GetString/GetDictionaryKeys/ContainsDictionaryKey/GetListAt` 的共享
classifier 调用，以及 `CreateVariant_guess/GetDouble/GetInt` 的窄/宽 integer 与 raw-double
helper 分层，并恢复 `PSBMedia::GetResourceData →
PSBRawNode::GetResource`；这些调整不改变 `99 ALIGNED / 15 EVIDENCE_LIMITED / 0 HAS_GAP`
统计，精确 helper 名和源码 token 仍按证据上限保留 `_guess`。

`CreateAdaptor == null` 的真实 Android ARM64 运行时闭环见
[FOLLOWUP_CREATE_ADAPTOR_NULL_RUNTIME_2026-08-02.md](FOLLOWUP_CREATE_ADAPTOR_NULL_RUNTIME_2026-08-02.md)：
现有 `ezsave.pimg` 已覆盖 `EnsureContainer` 的 Void `_file`/container 提交与恢复后的同名
重载；该覆盖不升级任何源码 token 判定。

全部天然成功路径与生命周期 oracle 随后已在当前 Android ARM64-only APK 上完成真实
执行：[FOLLOWUP_ANDROID_ARM64_RUNTIME_ORACLE_2026-08-03.md](FOLLOWUP_ANDROID_ARM64_RUNTIME_ORACLE_2026-08-03.md)
记录了固定目标哈希、24/24 无 trace PASS、24/24 单次全量 trace PASS、逐 case 事件数和
引用计数观察口径。该闭环未发现新生产 GAP，也不升级 15 个源码 token 上限。

构造器旧 pointer-return/owner-slot、两条 hidden-sret 伪返回与完整 dispatch vtable ABI 的 IDB 纠正见
[FOLLOWUP_IDB_DISPATCH_ABI_TYPES_2026-08-02.md](FOLLOWUP_IDB_DISPATCH_ABI_TYPES_2026-08-02.md)。
该轮为 33 个 primary/secondary vtable 入口补齐接口类型并保存 IDB，只清除已证伪的分析
状态，不改变生产代码或 99/15/0 判定。

factory/root/load typed NCB wrapper 的 11 个 ABI 类型、三种 wrapper/member-pointer 记录及
`paramsFunctor +4/+8/+0x10` 字段复原见
[FOLLOWUP_IDB_NCB_WRAPPER_ABI_TYPES_2026-08-02.md](FOLLOWUP_IDB_NCB_WRAPPER_ABI_TYPES_2026-08-02.md)。
该轮同步纠正 `GetFlags` 的旧 `__int64()/省略 self` 与 `0x59B708` 的旧 opaque-functor
分析状态；未修改生产代码，统计仍为 99/15/0。

class-info、AutoRegister、registrar 与 adaptor 尾链的 18 个 ABI 类型和六种 IDB-only
状态/interface 记录见
[FOLLOWUP_IDB_CLASSINFO_REGISTRATION_ABI_TYPES_2026-08-02.md](FOLLOWUP_IDB_CLASSINFO_REGISTRATION_ABI_TYPES_2026-08-02.md)。
该轮清除了 `InfoCtor/registerMembers/Regist/Unregist/RegistBegin/RegistItem` 的残留 X0
伪返回，并恢复两个 callback 的完整四参数 ABI；没有生产代码差异。

全部 114 个 canonical 入口的最终 prototype 机械复扫及最后五项纠正见
[FOLLOWUP_IDB_FINAL_PROTOTYPE_SWEEP_2026-08-02.md](FOLLOWUP_IDB_FINAL_PROTOTYPE_SWEEP_2026-08-02.md)。
当前 114/114 均可导出；严格筛选不再存在泛化 `void *`、`__n128` 或无依据
`__int64/_QWORD/_DWORD`，只剩三处有目标数据流证明的真实 64 位 size/size_t。

Git 历史中另一份 stripped Android ARM64 构建的同源性门禁见
[FOLLOWUP_HISTORICAL_ANDROID_ARM64_LINEAGE_2026-08-03.md](FOLLOWUP_HISTORICAL_ANDROID_ARM64_LINEAGE_2026-08-03.md)。
`verify_historical_arm64_lineage.py` 直接读取历史 blob，并证明 114 个 FDE、39 张
LSDA/232 个 call-site 与 5,525 条指令骨架均只是固定链接布局平移；该构建没有新增
PSBFile 插件语义符号、debug section 或源码伴随物，故不得用来消除 stripped/O3 token
上限。

最终复扫后的两条独立剩余队列见
[FOLLOWUP_REMAINING_EVIDENCE_QUEUE_2026-08-02.md](FOLLOWUP_REMAINING_EVIDENCE_QUEUE_2026-08-02.md)：
15 个 `EVIDENCE_LIMITED` 只能由 Android 1.4.4 的新增正证据把精确 token 唯一化；这不
阻塞继续采用二进制支持最强的源码候选，也不授权等待或追索同版本源码。六个运行时失败/
极端边界则只接受天然输入。只读盘点还确认仓库已有 142 个有效 `mdf\0 → PSB\0` 输入，但
没有自然 zlib-failure 或 `>4 GiB` 文件。

上述 15 项随后全部以当前 Android ARM64 目标 fresh `decompile` 重走，另加
`PSBRawOwner` 生命周期和 `GetDictionaryKeys` 旧 libstdc++ 容器路径复扫；相关地址、
边界、Web 编译器 null-gate 检查及 Debug/Release consumer 验证见
[FOLLOWUP_BINARY_CONTINUATION_SWEEP_2026-08-03.md](FOLLOWUP_BINARY_CONTINUATION_SWEEP_2026-08-03.md)。
该轮仍未发现新生产 GAP，也未依赖任何非目标源码、私库、LFS 或其他架构材料。

随后继续尝试从 Android 目标自身的 RTTI/typeinfo 与名字字面量消除私有 `_guess`，并
fresh 复扫 MDF 两只完整 clone 及 PSBMedia cache/resolve 对象图；结果、10 行伪代码、
allocator/泄漏边界和 40 项 IDB 局部命名见
[FOLLOWUP_TARGET_ONLY_RTTI_MDF_MEDIA_RESWEEP_2026-08-03.md](FOLLOWUP_TARGET_ONLY_RTTI_MDF_MEDIA_RESWEEP_2026-08-03.md)。
目标 vtable RTTI 与私有类型名确实未保留，但这不阻塞继续使用最强 binary-backed 候选。

函数面之外的静态对象拓扑随后也被独立闭合：目标 ELF 的 10 个静态表面、177 个 qword
现受门禁保护，包括 `.init_array` 顺序、dispatch 双基类地址点、media/AutoRegister/
adaptor 和三只 typed wrapper 完整表。当前源码与全部表项一致，未发现生产 GAP；详见
[FOLLOWUP_STATIC_OBJECT_VTABLE_SURFACE_2026-08-03.md](FOLLOWUP_STATIC_OBJECT_VTABLE_SURFACE_2026-08-03.md)。

同一 114-address surface 的 data xref 现也已完整闭合：333 条指令级引用归并为 133 个
唯一目标，其中 `.rodata` 精确分成 34 个 UTF-8/UTF-16 字面量与 42 张 switch table。
新 ELF 门禁固定全部 1,268 字节字面量、canonical 空串的两级指针链，以及 915 个
signed-relative case 槽；全部 194 个 destination 均对齐并落在 owner FDE 内。本地
classifier、packed count、String/Resource、integer/Real 分支和共享全局生命周期逐项
一致，未发现生产 GAP；详见
[FOLLOWUP_LITERAL_SWITCH_GLOBAL_SURFACE_2026-08-03.md](FOLLOWUP_LITERAL_SWITCH_GLOBAL_SURFACE_2026-08-03.md)。

42 张表的 selector 数据流与完整编译器分派链随后也被闭合：20 个 owner 的 42 个 selector
全部唯一回溯到 41 条 raw-tag `LDRB` 关系与 1 条复用既有归一化结果的 `SUB` 关系；41 个
`tag-lowcase` normalizer、1 个 zero-based reuse，以及 42 组 `CMP/B.HI/ADRP/ADD/LDRSW/
ADD/BR` 共形成 335 条固定链指令。当前源码的 classifier、packed、numeric、String/
Resource 与 Array/Dictionary selector 顺序逐项一致，未发现生产 GAP；详见
[FOLLOWUP_SWITCH_SELECTOR_DISPATCH_SURFACE_2026-08-03.md](FOLLOWUP_SWITCH_SELECTOR_DISPATCH_SURFACE_2026-08-03.md)。

完整调用面随后也从 AArch64 指令重建，而不采用组件工具的空 internal graph：排除 42 个
switch `BR` 后，目标有 468 个 `BL`、11 个 cross-FDE tail `B`、45 个 `BLR` 与 1 个
indirect tail `BR`。567-site serialization、39 条 MANIFEST 内边和 46 个间接 transfer
语义均与本地 helper/callback/vtable/member-pointer/EH 路径一致，未发现生产 GAP；详见
[FOLLOWUP_CALLSITE_SURFACE_2026-08-03.md](FOLLOWUP_CALLSITE_SURFACE_2026-08-03.md)。

自然 tag `0x0B` 的完整资产级阴性证据见
[FOLLOWUP_NATURAL_TAG0B_INVENTORY_2026-08-02.md](FOLLOWUP_NATURAL_TAG0B_INVENTORY_2026-08-02.md)：
只读扫描器成功遍历 112 份唯一 PSB 的 23,415,372 个可达节点，并由既有 tag `0x09`
offset 锚点验证寻址；当前资产中 tag `0x0B` 节点为 0，因此仍不能制造该边界 fixture。

同一扫描器随后补齐整数 tag 极值、差值与路径盘点，并把 Android oracle 从一个天然
tag `0x09` 节点扩为七个不可变样本，覆盖当前资产真实存在的 `0x04..0x09` 以及
`0xFFFFFFFF → GetInt(-1)` 边界。fresh `CreateVariant/GetInt/GetDouble` 证据、全部 pin 与
oracle 设计见
[FOLLOWUP_NATURAL_INTEGER_ORACLE_2026-08-02.md](FOLLOWUP_NATURAL_INTEGER_ORACLE_2026-08-02.md)。

同一只读扫描随后扩展到 Resource 表内有效性与资源区间，确认 1,240 个天然 tag `0x19`
全部可安全解析；唯一 raw tag `0x1A` 的 index 56395 超出 73 项表，不能冒充成功样本。
现以 `ezsave.pimg/2157.tlg` 固定 612 字节 tag `0x19`，同时覆盖 raw borrowed pointer 与
公开 TJS Octet 深拷贝/refcount 生命周期。fresh `GetResource/CreateVariant/Octet`
证据与完整 pin 见
[FOLLOWUP_NATURAL_RESOURCE_ORACLE_2026-08-02.md](FOLLOWUP_NATURAL_RESOURCE_ORACLE_2026-08-02.md)。

同一只读扫描随后补齐 Real 的分类/位模式极值与 String 的 table/index/UTF-8 路径，
固定五个天然样本覆盖 `0x1D/0x1E/0x1F`、`0x15/0x16`。新增 oracle 同时观察公开
Variant 与 raw `GetDouble/GetString`，并验证 String copy 脱离 owner 后存活以及 raw
借用地址。fresh 证据与完整 pin 见
[FOLLOWUP_NATURAL_REAL_STRING_ORACLE_2026-08-03.md](FOLLOWUP_NATURAL_REAL_STRING_ORACLE_2026-08-03.md)。

同一只读扫描继续覆盖 Null 与 Array/Dictionary 集合形状，并固定 `m2logo.mtn` 的一个
tag `0x01`、30 项 tag `0x20` 和 36 项 tag `0x21`。新增 `--shape-boundary` 同时观察
public Variant、raw category、class-id cache 与 borrowed secondary base 的 `NativeInstanceSupport`、
完整 32-slot primary vtable、精确 secondary address point、六个 no-op native lifecycle
vslot 与 19 个恒返 `-1002` 的 unsupported primary slot、
失效后仍直接读取 node 的 `IsInstanceOf`、按 packed 顺序的 value/no-value `EnumMembers`、
Object/ObjThis 双引用、清 global 至少释放两份 closure 引用、直接 AddRef/Release 的
严格 `+1/-1` 与 owner 保活；fresh
NativeInstanceSupport/unsupported-dispatch/Construct/native-Invalidate/native-Destruct/IsInstanceOf/
EnumMembers/ctor/CreateVariant/AddRef/Release/GetTypeCategory
证据与完整 pin 见
[FOLLOWUP_NATURAL_SHAPE_LIFECYCLE_ORACLE_2026-08-03.md](FOLLOWUP_NATURAL_SHAPE_LIFECYCLE_ORACLE_2026-08-03.md)。
同一 `--shape-boundary` 现另通过 AAPCS64 `X8` hidden-sret 直调 raw
`GetRoot@0x598A3C` 与 `Transfer@0x598A64`，固定 owner `1 → 2 → 2 → 1 → terminal`、
source 清空、空 holder 转移与 release helper 不清槽边界；fresh Android 证据、harness ABI
验证与天然 pin 见
[FOLLOWUP_RAW_HOLDER_LIFECYCLE_ORACLE_2026-08-03.md](FOLLOWUP_RAW_HOLDER_LIFECYCLE_ORACLE_2026-08-03.md)。
同一 raw root 继续覆盖 strict/non-strict Dictionary lookup 的两级 helper miss、out
preserve、destination overwrite、`self==out` alias、IsValid 与 Contains 临时 holder；
fresh Android 证据、天然 key/offset pin 与 `1 → 2 → 3 → 4 → terminal` 引用链见
[FOLLOWUP_RAW_DICTIONARY_LIFECYCLE_ORACLE_2026-08-03.md](FOLLOWUP_RAW_DICTIONARY_LIFECYCLE_ORACLE_2026-08-03.md)。
同一 root 的 ordered key 路径再以 24-byte hidden-sret 直接观察 Android gnustl
`std::vector<std::string>` 三指针拓扑、精确 reserve、单指针 COW string 与目标内析构，见
[FOLLOWUP_RAW_DICTIONARY_KEYS_VECTOR_ORACLE_2026-08-03.md](FOLLOWUP_RAW_DICTIONARY_KEYS_VECTOR_ORACLE_2026-08-03.md)。

PSBMedia 的简单接口现另有独立 `--media-interface`：固定 Android 11-slot vtable、
singleton `2 → 3 → 2` 非原子引用、UTF-16 `psb` 名称、两个 Normalize no-op、
GetLocallyAccessibleName 的非空/空清理，以及 target-allocated 空对象的 zero-ref
`0xFFFFFFFF` 与 complete/deleting destructor 边界。fresh 证据与生产逐项对照见
[FOLLOWUP_PSBMEDIA_INTERFACE_LIFECYCLE_ORACLE_2026-08-03.md](FOLLOWUP_PSBMEDIA_INTERFACE_LIFECYCLE_ORACLE_2026-08-03.md)。

`PSBMedia::GetListAt` 的 Array 分支现另有独立 `--media-array`：固定天然
`ezsave.pimg/$/layers` 的 SHA-256、node `0x20b`、count `32` 与 packed table prefix，
并要求原生 lister 严格回调 `"0".."31"`。fresh 反编译、逐行生产对照、host pin、
fake control-flow 见
[FOLLOWUP_PSBMEDIA_ARRAY_ORACLE_2026-08-03.md](FOLLOWUP_PSBMEDIA_ARRAY_ORACLE_2026-08-03.md)。

Boolean/category-1 盘点随后确认 `0x02/0x03/0x27/0x2F/0x33/0x37/0x3B`
在当前 23,415,372 个可达节点中全部为 0；因此未制造成功或 conversion-error fixture，
只把该缺口加入天然输入队列。fresh `CreateVariant/GetTypeCategory` 证据与复扫结果见
[FOLLOWUP_NATURAL_BOOLEAN_INVENTORY_2026-08-03.md](FOLLOWUP_NATURAL_BOOLEAN_INVENTORY_2026-08-03.md)。
