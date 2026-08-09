# Android `libkrkr2.so` 内嵌 psbfile：114 函数审计汇总

审计与复原日期：`2026-07-25`。唯一权威来源是 Android kirikiroid2 `libkrkr2.so`。
每个 emitted 入口均由一个独立 function subagent 进行 fresh IDA `decompile` 并与本地
实现逐项对照；随后又对全部 15 个 `EVIDENCE_LIMITED` 入口作针对性二次 fresh 复核，
所有发现的确定 GAP 均按同一 Android 证据闭合并重新验收。

2026-07-26 又以 Android 1.3.9 的 15/15 指令级映射和同 release iOS 的 helper/assert
边界作非权威交叉复核。目标 `0x59A0B4` 自身包含 `0x5996E4` 的完整 inline clone，iOS
对应体又保留 direct call；本地据此把 `GetResourceData` 的手写资源解码恢复为
`value.GetResource(size)`。精确 target token 仍不唯一，因此统计不变。

2026-08-02 又将 `PSBValueDispatch_ctor_guess@0x597AD4` 的 IDB 旧类型从
`pointer-return + raw owner slot` 纠正为普通 `void(this, const PSBFile *, node)` 构造器
ABI，并同步修正三个 new-expression caller 注释。重新反编译后伪 `return result` 已消失、
owner 读取明确显示为 `file->owner`。同轮又为完整 primary/secondary dispatch vtable 的
33 个入口补齐接口 ABI；关键函数现直接显示正确的 32 位错误码、参数角色与字段访问。
全量 X8 prototype 筛选还纠正了 `GetRoot@0x598A3C` 与 load 参数复制 helper
`0x59B708` 的两条伪 X0 返回；当前五个 sret 入口均为 `void + result@X8`。
详见 [FOLLOWUP_IDB_DISPATCH_ABI_TYPES_2026-08-02.md](FOLLOWUP_IDB_DISPATCH_ABI_TYPES_2026-08-02.md)。
这些修正只清除已证伪的分析状态，不改变 99/15/0 统计。

同日继续对 factory/root/load typed NCB wrapper 做 fresh 复核，在 IDB 中建立二进制自身
可证明的 `paramsFunctor`、ARM64 member-pointer 与三种 wrapper 记录，并成功补齐 11 个
函数 ABI。`PropGet/PropSet/FuncCall/Invoke/GetFlags/deleting destructor` 现均显示准确
参数角色与返回宽度；`0x59B708` 的 functor 也由目标内 `+4/+8/+0x10` 证据从 `void *`
收紧为中性 ABI 记录。详见
[FOLLOWUP_IDB_NCB_WRAPPER_ABI_TYPES_2026-08-02.md](FOLLOWUP_IDB_NCB_WRAPPER_ABI_TYPES_2026-08-02.md)。
该轮同样只纠正 IDB/分析材料，不改变 99/15/0 统计。

随后对 class-info / AutoRegister / RegistItem 尾链继续 fresh 复核，并成功应用 18 个函数
类型。目标内字段访问固定了 class-info 四字段、AutoRegister `+0x18` className、两层
registrar state、adaptor native/sticky 与 item-interface 四槽；六条残留 X0 伪返回已改为
`void`，两个常量 callback 也恢复了完整 TJS 四参数 ABI。详见
[FOLLOWUP_IDB_CLASSINFO_REGISTRATION_ABI_TYPES_2026-08-02.md](FOLLOWUP_IDB_CLASSINFO_REGISTRATION_ABI_TYPES_2026-08-02.md)。
该轮没有发现生产 GAP，统计仍为 99/15/0。

最终又对 114 个 canonical 地址机械导出 114/114 prototype，清理模块静态初始化、
`PSBRawOwner` 构造器、`CreateAdaptor` 与两个保留精确 mangled name 的 vector helper 共
五项最后泛化。复扫后不再存在 `void *` prototype、`__n128` 或无依据
`__int64/_QWORD/_DWORD`；仅保留三处真实的 64 位 raw size/`size_t`。详见
[FOLLOWUP_IDB_FINAL_PROTOTYPE_SWEEP_2026-08-02.md](FOLLOWUP_IDB_FINAL_PROTOTYPE_SWEEP_2026-08-02.md)。

最后把仍未关闭的事项按证据性质拆分归档：15 个 `EVIDENCE_LIMITED` 只有在出现 Android
1.4.4 目标内符号、DWARF、同版本源码或新的 retained boundary 时才能把精确 token
唯一化；这不是实现阻塞，当前复原继续采用二进制支持最强的源码候选，也不得为升级标签
转去追索外部源码或私库。损坏 MDF、filter 后
offset failure、损坏 packed table、自然 tag `0x0B` 极端值和 `>4 GiB` storage 则需要
现成天然输入。当前仓库只读盘点发现 142 个有效 MDF-success 输入，但没有自然
zlib-failure 或超 4 GiB 文件。完整门槛与下一动作见
[FOLLOWUP_REMAINING_EVIDENCE_QUEUE_2026-08-02.md](FOLLOWUP_REMAINING_EVIDENCE_QUEUE_2026-08-02.md)。
后续 Boolean/category-1 阴性盘点把天然输入队列扩为六项。

随后又把“尚无已定位的自然 tag `0x0B`”升级为全资产可达节点盘点：222 个物理
PSB/MDF 解压去重为 112 份 PSB，23,415,372 个可达节点 112/112 解析成功，已知
`m2logo.mtn@0x36F8` tag `0x09` 锚点命中，但 tag `0x0B` 总数为 0。扫描器、fresh
Android `GetInt/CreateVariant` 边界与结果见
[FOLLOWUP_NATURAL_TAG0B_INVENTORY_2026-08-02.md](FOLLOWUP_NATURAL_TAG0B_INVENTORY_2026-08-02.md)。

扫描器又统计了全部天然整数 tag 的值域与 Variant/GetInt 差值，并为极值恢复路径。当前
资产实际存在 `0x04..0x09`，不存在 `0x0A..0x0C`；Android oracle 已从一个 tag `0x09`
节点扩为七个固定样本，覆盖零值、8/16 位负数、24/32 位值，以及两条 40 位完整值与
低 32 位分叉。七组现已在 Android ARM64 原生执行与全量 trace 中全部通过。oracle 设计详见
[FOLLOWUP_NATURAL_INTEGER_ORACLE_2026-08-02.md](FOLLOWUP_NATURAL_INTEGER_ORACLE_2026-08-02.md)。

同一扫描器又完成 Resource tag、table index 与实际文件区间盘点：1,240 个 tag `0x19`
全部表内，唯一 raw tag `0x1A` 是 56395 对 73 项表的自然 OOB，不能送入成功路径。
Android oracle 现固定 `ezsave.pimg/2157.tlg` 的 612 字节 Resource，同时检查 raw
`GetResource` 借用地址和公开 TJS Octet 深拷贝、owner 释放及局部 refcount 下降。
本地生命周期单测、Android ARM64 原生执行与 trace 均已通过。详见
[FOLLOWUP_NATURAL_RESOURCE_ORACLE_2026-08-02.md](FOLLOWUP_NATURAL_RESOURCE_ORACLE_2026-08-02.md)。

扫描器随后补齐 Real 分类/位模式极值与 String table/index/UTF-8 路径盘点。天然
`0x1D/0x1E/0x1F` 和 `0x15/0x16` 已固定为五个公开 Variant + raw getter oracle；
`0x1F` 复用原始 MDF，String 两例还区分了公开 copy 生命周期与 raw borrowed pointer。
所有 host pin、本地回归、Android ARM64 原生执行与 trace 均已通过。详见
[FOLLOWUP_NATURAL_REAL_STRING_ORACLE_2026-08-03.md](FOLLOWUP_NATURAL_REAL_STRING_ORACLE_2026-08-03.md)。

PSBMedia 的 11-slot interface 现另有 direct oracle：singleton 固定非原子
`2 → 3 → 2`，GetName 固定 UTF-16 `psb`，两个 Normalize 保持输入对象位，
GetLocallyAccessibleName 覆盖非空/空清理；两只 target-allocated 空对象分别固定
zero-ref `0xFFFFFFFF` 非删除边与 ref-one deleting destructor，并独立调用 complete
destructor。fresh Android 证据与生产实现一致，Android ARM64 原生执行与 trace 已通过，
未修改 `cpp/`。详见
[FOLLOWUP_PSBMEDIA_INTERFACE_LIFECYCLE_ORACLE_2026-08-03.md](FOLLOWUP_PSBMEDIA_INTERFACE_LIFECYCLE_ORACLE_2026-08-03.md)。

同一进程期 media 现又有独立 `--media-array`：固定未改写 `ezsave.pimg` 的 SHA-256、
`$/layers` 路径、`0x20b` 天然 Array 节点、count `32` 与 packed table 原始前缀，再经
原生 `PSBMedia::GetListAt@0x5999F4` 要求 lister 严格收到 `"0".."31"`。fresh Android
证据与生产 Array 分支一致，Android ARM64 原生执行与 trace 已通过，仍未修改 `cpp/`。详见
[FOLLOWUP_PSBMEDIA_ARRAY_ORACLE_2026-08-03.md](FOLLOWUP_PSBMEDIA_ARRAY_ORACLE_2026-08-03.md)。

2026-08-03 最终又在固定的 ARM64-only APK 上合并执行全部 raw/scalar/shape/resource/media
模式：无 trace 为 24/24 `ok`，单次全量 `--trace` 亦为 24/24 `ok`，且均无 cleanup error。
启动包 continuous handler 的跨线程竞争、集合回调共享状态和临时 Variant refcount 观察
口径已在 oracle 层纠正；目标哈希、命令、逐 case 事件数与审计影响见
[FOLLOWUP_ANDROID_ARM64_RUNTIME_ORACLE_2026-08-03.md](FOLLOWUP_ANDROID_ARM64_RUNTIME_ORACLE_2026-08-03.md)。

最终函数面又由权威 ELF `.eh_frame` 独立复核：两只 static-init FDE 与 112 只主簇 FDE
的起点集合精确等于 MANIFEST，全部范围连续且分别结束于下一模块入口
`0x42CFA0/0x59B9C8`。fresh 反编译以 `"PackinOne.dll"` registration 与八个子插件字面量
确认两个边界的下一模块归属。后续 fresh IDA 又证明直接前驱
`layerExImage_ModuleRegist@0x42CE6C` 精确结束于 `0x42CEF8`，并把主簇前 8 个入口全部
归入 `layerExImage` 的 `noise/generateWhiteNoise/gaussianBlur` NCB vtable；最后一只
`0x596414` 精确结束于 `0x59641C`。verifier 现可重复执行这组双向边界检查。详见
[FOLLOWUP_ELF_FDE_SURFACE_VERIFIER_2026-08-03.md](FOLLOWUP_ELF_FDE_SURFACE_VERIFIER_2026-08-03.md)。

同一 verifier 随后扩展到完整 exception surface：114 个 FDE 精确拆为 39 个
LSDA-bearing 与 75 个 unwind-only 入口。逐报告交叉复扫据此找到唯一遗漏的
`GetDictionaryValue@0x598D58`：目标仅为 owner 析构内联的 aligned dealloc 建立
terminate landing，两个 packed helper 异常直接传播；parent `ContainsDictionaryKey`
则清理已构造的 raw-node 临时后继续 unwind。本地隐式 non-throw owner 析构与未标
`noexcept` 的 lookup 接口完全对应，无生产 GAP。详见
[FOLLOWUP_ELF_LSDA_SURFACE_2026-08-03.md](FOLLOWUP_ELF_LSDA_SURFACE_2026-08-03.md)。

随后又直接解码 raw `PSBFile/PSBRawNode` 生命周期簇全部 10 张 call-site table：51 项中
18 项无 landing、16 项 cleanup-only、17 项经 null type-table entry 解析为 catch-all，
fresh IDA 确认后者落到 terminate handler。本地自动对象清理层次、可传播接口与隐式
non-throwing 析构逐项一致；新门禁机械锁定全部四元组和 10 份报告标记，仍无生产 GAP。
详见
[FOLLOWUP_RAW_LSDA_CALLSITE_TOPOLOGY_2026-08-03.md](FOLLOWUP_RAW_LSDA_CALLSITE_TOPOLOGY_2026-08-03.md)。

最后把同一解码扩展到全部 39 个 LSDA 入口：232 项精确分为 77 个无 landing、80 个
cleanup-only、75 个 null-type catch-all；IDA `tryblks` 独立得到
`80 type_id=-2 / 75 type_id=-1 / 0 mismatch`。全量复扫同时固定了一个方法边界：
catch-all 既可能是析构失败 terminate，也可能是 catch/清理/rethrow，不能仅凭 action 值
反推函数级 `noexcept`。39 份报告与完整四元组现均受门禁保护，生产判定不变。详见
[FOLLOWUP_FULL_LSDA_CALLSITE_TOPOLOGY_2026-08-03.md](FOLLOWUP_FULL_LSDA_CALLSITE_TOPOLOGY_2026-08-03.md)。

同日又回到当前 Android ARM64 目标，对全部 15 个 `EVIDENCE_LIMITED` 入口 fresh
`decompile`，并加深复扫 `PSBRawOwner@0x598AAC/0x598960/0x598B3C` 与
`GetDictionaryKeys@0x598E64`。目标的未初始化/null 故障边界、signed-64 offset 顺序、
aligned dealloc、旧 libstdc++ 三指针 vector/COW string/emplace/异常清理均与本地一致；
Web Release psbfile、Mac Debug/Release psbfile 及 motionplayer consumer 全部通过，统计仍为
99/15/0。完整证据与测试矩阵见
[FOLLOWUP_BINARY_CONTINUATION_SWEEP_2026-08-03.md](FOLLOWUP_BINARY_CONTINUATION_SWEEP_2026-08-03.md)。

下一轮又直接检查 Android 目标内 RTTI/typeinfo 与私有 helper 名，并 fresh 复扫
`Load@0x598268`、`LoadStorage@0x598538`、`EnsureContainer@0x599E04`、
`Resolve@0x59A4B0`。目标没有保留可唯一化 guessed 私有类型的 RTTI/name；两只 MDF clone
的混合 allocator/deallocator、失败泄漏，以及 PSBMedia adaptor/path/raw-node 生命周期均与
当前实现一致。IDB 已持久化 40 个局部语义名，生产统计仍为 99/15/0。详见
[FOLLOWUP_TARGET_ONLY_RTTI_MDF_MEDIA_RESWEEP_2026-08-03.md](FOLLOWUP_TARGET_ONLY_RTTI_MDF_MEDIA_RESWEEP_2026-08-03.md)。

随后把函数清单之外的静态对象拓扑纳入机械审计：目标 ELF 的 `.init_array` 双入口、
dispatch 32-slot primary + `-8` secondary、media 11-slot、AutoRegister/adaptor 以及
factory/root/load 三只 34-slot typed wrapper，共 10 个表面、177 个 qword，现全部由
`verify_elf_surface.py` 逐值固定。fresh constructor/static-init/wrapper 证据与当前源码
逐项一致，没有生产 GAP；详见
[FOLLOWUP_STATIC_OBJECT_VTABLE_SURFACE_2026-08-03.md](FOLLOWUP_STATIC_OBJECT_VTABLE_SURFACE_2026-08-03.md)。

随后继续从 114 个函数枚举全部 data xref：333 条指令级引用、133 个唯一目标中，
`.rodata` 的 76 项精确分成 34 个 UTF-8/UTF-16 字面量与 42 张 switch table。新门禁
固定 1,268 字节字面量、canonical 空串的两级指针链和 915 个 signed-relative case 槽；
194 个唯一 destination 全部落在 owner FDE。classifier、packed/numeric 分支以及 16 个
`.bss` 全局生命周期与当前源码逐项一致，没有生产 GAP；详见
[FOLLOWUP_LITERAL_SWITCH_GLOBAL_SURFACE_2026-08-03.md](FOLLOWUP_LITERAL_SWITCH_GLOBAL_SURFACE_2026-08-03.md)。

最后直接从 114 个 FDE 的 AArch64 word 恢复完整 callsite surface：567 个 transfer
site 精确分成 468 `BL`、11 cross-FDE tail `B`、45 `BLR`、1 indirect tail `BR` 与
42 switch `BR`。479 个 direct site 指向 86 个目标，其中 44 site/39 unique edge 位于
MANIFEST 内；46 个非 switch 间接 transfer 全部闭合到现有 callback/vtable/member-pointer/
manager 路径。当前调用链仍无生产 GAP；详见
[FOLLOWUP_CALLSITE_SURFACE_2026-08-03.md](FOLLOWUP_CALLSITE_SURFACE_2026-08-03.md)。

随后把 46 个非 switch 间接 transfer 的 target producer 从人工分类升级为独立 ELF
门禁：44 个 fixed-offset `LDR` 与 2 个 Itanium member-pointer register-offset `LDR` 的
producer word、目标寄存器和 18 类语义角色全部固定。fresh decompile 再次确认
`std::function` manager/invoker、stream vslot、lister、NCB/global dispatch 与 root/load
typed member-pointer 链均和本地源码/模板一致，仍无生产 GAP；详见
[FOLLOWUP_INDIRECT_ABI_SURFACE_2026-08-03.md](FOLLOWUP_INDIRECT_ABI_SURFACE_2026-08-03.md)。

再后把 435 个 MANIFEST 外 direct site 的 65 个唯一 target 全部语义化：它们精确分入
9 类通用 EH/runtime、allocation/memory/compression、libstdc++、storage/stream、script
global、diagnostic/log、ttstr/narrow、ncbind 与 Variant/closure 边界。32 个原 `sub_*`
均已 fresh decompile、以 `_guess` 名和证据注释写入 IDB；没有发现隐藏的 PSB 私有 helper，
源码调用层与对象清理路径仍一致。详见
[FOLLOWUP_EXTERNAL_CALLEE_SURFACE_2026-08-03.md](FOLLOWUP_EXTERNAL_CALLEE_SURFACE_2026-08-03.md)。

最后把 114-entry stack-frame/local-lifetime surface 也接入独立 ELF 门禁：目标精确为
57 framed + 57 frameless；57 个 frame 中 52 个在入口分配，5 个只在诊断/转换慢路径
shrink-wrap。31 个 canary、10 种 callee-saved GPR mask、唯一 `D8` spill 以及
`39 LSDA-framed + 18 unwind-only-framed` 交叉关系全部逐函数固定。fresh decompile 证明
五个 16-byte 慢路径 frame 不是隐藏 RAII local；较大 frame 的 Variant/ttstr/raw-node/
vector/ncbind functor scope 与既有 LSDA landing 及本地源码逐层一致，仍无生产 GAP。详见
[FOLLOWUP_STACK_FRAME_LIFETIME_SURFACE_2026-08-03.md](FOLLOWUP_STACK_FRAME_LIFETIME_SURFACE_2026-08-03.md)。

最后把 44 个 MANIFEST 内 direct transfer 的逐调用点 ABI contract 也接入门禁：42 个
`BL` 与 2 个 cross-FDE tail `B` 被精确分成 21 类参数角色和 8 类返回消费方式；两处
hidden-sret、Variant by-value copy/dtor、两种 `uint32 → size_t` zero-extension 与 ncbind
两只空 tag-reference 的 producer word 全部固定。fresh caller/callee 证据纠正了
`CopyFirstArgument_guess@0x59B708` 旧 IDB prototype 遗漏 `X1/X2` 的问题；本地
raw/media/ncbind 调用分层仍无生产 GAP。详见
[FOLLOWUP_INTERNAL_CALL_CONTRACT_SURFACE_2026-08-03.md](FOLLOWUP_INTERNAL_CALL_CONTRACT_SURFACE_2026-08-03.md)。

随后完成 114 函数的首轮 typed-member/对象字段表面枚举：当时的 483-row 旧基线按
`R=312/W=147/RW=10/address=14` 分类；416 条带 EA 的行折叠为 385 个唯一机器站点，
覆盖 62 个 owner FDE 并由 ELF word digest 固定。该轮 `cot_ptr=480` 只是当时类型状态下的
初步单类快照，筛查补回 7 个 Hex-Rays 类型传播缺口；它不再代表当前完整 raw surface。
补型后 owner→header→names/string/chunk、refcount/data、raw-node output 与 media/ncbind
字段链均直接可读，源码嵌套结构和生命周期仍无生产 GAP。
详见
[FOLLOWUP_TYPED_MEMBER_FIELD_SURFACE_2026-08-03.md](FOLLOWUP_TYPED_MEMBER_FIELD_SURFACE_2026-08-03.md)。

最后完成 39 个 LSDA owner 的 landing cleanup contract 全面：155 条非零引用折叠为
150 个唯一 landing，75 个 cleanup-only 全部恢复展开，75 个 catch-all 则精确分为
72 个直接 terminate 与 3 个 catch/delete/rethrow。独立 ARM64 CFG 解码固定了 569 个
唯一 landing 指令、1,150 个 per-root 指令实例和 168 个 transfer，并证明其与正常流
零交叉。本地 Factory 显式 catch、stream/Variant/ttstr/vector/ncbind RAII 与 raw-data
异常泄漏边界均一致，未发现生产 GAP。详见
[FOLLOWUP_LANDING_CLEANUP_CONTRACT_SURFACE_2026-08-03.md](FOLLOWUP_LANDING_CLEANUP_CONTRACT_SURFACE_2026-08-03.md)。

随后完成互补的 114-entry normal CFG / terminal contract：4,956 条正常指令与 569 条
landing-only 指令零交叉，二者完整覆盖全部 5,525 条 FDE 指令；5,337 条 successor edge
导出 208 个终点，分为 162 `RET`、11 direct tail、1 indirect tail 与 34 true-noreturn。
114 个返回 ABI 分为 37 void、71 `W0/X0`、5 hidden-sret `X8`、1 `D0`；28 个 TVP/TJS
diagnostic 调用显式保留 helper-return fallback，不能误用 landing noreturn 集。当前源码
默认值、X8 non-trivial return、constructor void ABI 与 tail 生命周期均一致，无生产 GAP。
详见
[FOLLOWUP_NORMAL_CFG_TERMINAL_CONTRACT_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_CFG_TERMINAL_CONTRACT_SURFACE_2026-08-03.md)。

随后把 162 个正常 `RET` 中的 129 个 source-visible W0/X0/D0 value return 全部回溯到
producer：72 个 owner 共形成 160 条关系，精确分为 157 个 instruction writer、2 个
direct `BL` return 与 1 个 indirect `BLR` return。112 个 RET 为单来源，17 个为多来源
join；96 个 W0、19 个 X0、14 个 D0 返回中的 TJS error、refcount、null、diagnostic
default、conversion 与 callback value 均由 exact word/destination 和完整 predecessor
path 固定，入口残留和未声明 call-clobber 均为 0。fresh 反编译确认当前源码无生产 GAP。
详见
[FOLLOWUP_NORMAL_RETURN_VALUE_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_RETURN_VALUE_SURFACE_2026-08-03.md)。
其后又从每个 continuing `BL/BLR` fallthrough 正向追踪物理 `W0/X0` 的首事件：311 个
call 共形成 419 条路径关系，分为 86 `use`、250 `overwrite`、49 `call-boundary` 与
34 `ret-reaches`。147 个 direct-GPR 中 71 个、39 个 indirect 中 15 个被显式使用，而
125 个 direct-void 的显式使用严格为 0。全部 34 个 `RET` reach 都属于 void/hidden-sret/
FP owner，GPR owner 为 0；四条 atomic/vector pre-event loop 的有限 exit 也全部闭合。
当前源码的调用结果、临时对象和 wrapper 数据流未发现生产 GAP；详见
[FOLLOWUP_NORMAL_CALL_RESULT_FIRST_EVENT_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_CALL_RESULT_FIRST_EVENT_SURFACE_2026-08-03.md)。

随后补齐全部 normal direct transfer 的输入参数 producer surface：306 个直接 `BL` 与
11 个 out-of-owner tail `B` 覆盖 59 个 caller owner、80 个目标，完整 prototype 产生
446 个 AAPCS64 register arg 与 475 条来源关系。435 个参数单来源，11 个多来源，最大
13 路；447 个 instruction writer、18 个 entry parameter、10 个 preceding-`BL` return
全部闭合，volatile call clobber 与 entry residue 均为 0。7 个 hidden `X8`、唯一 `D0`、
三条真正 pre-index `STR` base writeback，以及 `0x59AFE8` 必须来自前序 concat return 而
非 `STLXR` memory base 的纠正均已进入 exact-word 门禁。当前源码未发现生产 GAP；详见
[FOLLOWUP_NORMAL_DIRECT_ARGUMENT_PRODUCER_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_DIRECT_ARGUMENT_PRODUCER_SURFACE_2026-08-03.md)。

随后完成互补的非 switch indirect argument producer surface：45 个 `BLR` 与 1 个
deleting-destructor tail `BR` 共暴露 117 个 `X0..X7` 参数、120 条来源关系。40 个站点
从 owner entry 可达，6 个只在 LSDA cleanup 图中可达；114 个参数单来源、3 个两路 join，
119 个 instruction writer 与唯一 entry `X0` 全部闭合，未声明 call-clobber 与 entry
residue 为 0。call-operand 类型还纠正了 factory callback `0x59B1A8` 的 stale `X4`
假第五参，并固定 Enum callback 两处完整八参 `FuncCall`。当前 callback/vtable/
`std::function`/member-pointer 参数顺序与本地源码/模板分层一致，无生产 GAP。详见
[FOLLOWUP_INDIRECT_ARGUMENT_PRODUCER_SURFACE_2026-08-03.md](FOLLOWUP_INDIRECT_ARGUMENT_PRODUCER_SURFACE_2026-08-03.md)。

随后把 483-row typed-member 旧基线中的 instruction-backed `W/RW` 继续闭合到写值 producer：
32 个 owner 的 108 个语义写事件落在 101 个 `STRB/STRH/STR/STP` 站点，形成 109 条
reaching-definition 关系。107 个事件单来源，唯一两路 join 为 vector growth 的 `end`；
84 个 instruction writer、3 个 owner-entry 参数和 22 个 architectural zero source 全部
由 selected store operand、exact word 和 normal CFG 路径固定，call return/clobber 与
entry residue 均为 0。四组完整 header population、raw/dispatch/media output commit、
vector 与 ncbind 状态写链均与当前源码一致，无生产 GAP。详见
[FOLLOWUP_TYPED_MEMBER_WRITE_VALUE_PRODUCER_SURFACE_2026-08-03.md](FOLLOWUP_TYPED_MEMBER_WRITE_VALUE_PRODUCER_SURFACE_2026-08-03.md)。

随后闭合 typed-member 的 instruction-backed 读值/取址侧：61 个 owner 的
`R=297/RW=3/address=11` 共形成 311 条语义事件、290 个实际站点。267 条 direct
producer 语义行展开为 268 个 lane 和 288 条 first-event relation，角色精确为
`196 use / 46 transform / 37 call-use / 6 call-boundary / 3 RET`；44 条 residual
anchor 语义行展开为 45 个 lane 和 45 条单来源关系。37 个 call-use 全部与独立
direct/indirect argument manifest 的同站点同 bank 输入一致，landing-only、未声明
volatile call-clobber 与 pre-event loop 均为 0。当前 packed/header、OwnerFilter、
media resource clone 与 ncbind member-pointer 中间数据流无生产 GAP。详见
[FOLLOWUP_TYPED_MEMBER_READ_CONSUMER_SURFACE_2026-08-03.md](FOLLOWUP_TYPED_MEMBER_READ_CONSUMER_SURFACE_2026-08-03.md)。

随后把没有独立 ctree EA 的最后 67 条 typed-member optimizer-synthetic 语义行闭合到
73 个真实机器锚点：`W=42/R=15/RW=7/address=3` 覆盖 33 个 owner，62 个 occurrence
实现为 assignment/RMW/address/cast coalesced store，另有 `6 LDR + 3 BLR + 2 BR`。
73/73 均 normal-entry reachable；function-manager BLR 与 packed-tag switch BR 分别和
独立 indirect ABI / switch manifest 一致。五条多 occurrence 语义、cast 语法下实际
coalesced store 的边界、dispatch 双 vptr、adaptor 与 vector commit 全部由 exact word
固定，`416 EA-backed + 67 synthetic = 483`，闭合的是旧基线而非最终完整表面。当前源码
无生产 GAP。详见
[FOLLOWUP_TYPED_MEMBER_SYNTHETIC_REALIZATION_SURFACE_2026-08-03.md](FOLLOWUP_TYPED_MEMBER_SYNTHETIC_REALIZATION_SURFACE_2026-08-03.md)。

随后全量枚举当前 IDB 的 `cot_ptr/cot_idx`，并按 owner-local ordinal、父语法、访问模式、
语义摘要、exact ARM64 word 与 normal-CFG 锚点固定每一行。`Transfer@0x598A64` 与
`Resolve@0x59A4B0` 的 producer/consumer 正证据又提升 5 条 read-only typed rows，故当前
typed-member 总数为 `488 = 421 EA-backed + 67 synthetic`，即
`R=317/W=147/RW=10/address=14`、390 个唯一 EA 站点。剩余 raw surface 为
`667 = 461 cot_ptr + 206 cot_idx`，496 条 EA-backed 与 171 条 synthetic 共同映射到
610 个唯一机器锚点，全部 normal-entry reachable；本地 packed/raw/media/TJS/NCB/STL
结构与生命周期无生产 GAP。详见
[FOLLOWUP_RAW_MEMORY_DEREFERENCE_SURFACE_2026-08-03.md](FOLLOWUP_RAW_MEMORY_DEREFERENCE_SURFACE_2026-08-03.md)。

随后把既有 allocator/release 调用点推进成跨表面的 source-facing 生命周期契约：20 个
direct allocation result 全部与实参 producer、first result event 相交，并闭合到 68 个
normal/landing 发布、转移和清理锚点；8 条 LSDA constructor/copy edge 单独证明
new-expression/catch cleanup。五个 direct release/refcount-helper target 的 120 个站点
完整分为 `83 normal / 37 landing` 与 `80 raw / 40 shared`，3 条正常和 8 条异常泄漏边
也固定为 cleanup-absence contract。Factory 发布后删除不清槽、LoadStorage raw leak、
EnsureContainer adaptor-null/认领前异常、singleton、ncbind wrapper 与两种 vector backing
均和当前源码一致，无生产 GAP。详见
[FOLLOWUP_HEAP_ALLOCATION_LIFECYCLE_SURFACE_2026-08-03.md](FOLLOWUP_HEAP_ALLOCATION_LIFECYCLE_SURFACE_2026-08-03.md)。

随后独立闭合引用计数状态机，而不把 40 个 shared-release 调用点当成完整证明：6 个初值、
10 个非原子 retain、19 个 owner decrement-release、3 个 folded zero-probe、9 个
old-libstdc++ COW decrement 与 7 个 shared-string atomic retain 均已逐站点固定。全 114 FDE
独占原子 census 恰为 16 对；15 个 virtual 转换精确分成 `5 AddRef / 9 Release / 1 terminal`，
7 个 direct helper target 共 93 个站点，另固定 10 个 helper body。aligned dealloc 的
`19 decrement + 3 zero-probe + 1 raw replacement + 1 direct destructor = 24` 分区完整，
本地源码无生产 GAP。详见
[FOLLOWUP_REFERENCE_COUNT_STATE_MACHINE_SURFACE_2026-08-03.md](FOLLOWUP_REFERENCE_COUNT_STATE_MACHINE_SURFACE_2026-08-03.md)。

随后独立枚举 114 个 MANIFEST FDE 的全部 `.bss` DataRef，并把全局/静态对象生命周期
闭合为机器契约：94 行、93 个唯一站点、16 个目标、22 个 owner，精确分为
`29 address / 31 read / 34 write` 与 `84 normal / 10 landing`。9 个语义对象覆盖 lazy
native ID、两只 AutoRegister、PSBMedia singleton pointer/guard、class-info/guard、三路
AutoRegister 链头和 COW empty representation；六组 class-info transition、7 个 singleton/
native direct call 与静态构造顺序均由 exact word 和 CFG 固定。本地实现逐项一致，无生产
GAP、无 `cpp/` 修改。详见
[FOLLOWUP_GLOBAL_BSS_STATE_MACHINE_SURFACE_2026-08-03.md](FOLLOWUP_GLOBAL_BSS_STATE_MACHINE_SURFACE_2026-08-03.md)。

随后独立闭合 `.rodata/.bss` 之外的 initialized-data / relocation 表面：39 条
`.data.rel.ro` xref 精确覆盖 18 个 address-point header/target 与 13 个 owner；24 组
GOT pair 覆盖 4 个 slot、7 个 owner并分为 `16 normal / 8 landing`。三张此前未单独固定的
base/interface vtable 共 50 个 qword；12 个 Itanium address point、5 条动态 relocation 与
24 次 vptr publication 又把 dispatch 双继承、typed wrapper embedded interface、
media/class/adaptor 构造析构及 COW/pthread/canonical-empty 链闭合到 exact word、CFG 和
canonical SHA。本地实现逐项一致，无生产 GAP、无 `cpp/` 修改。详见
[FOLLOWUP_INITIALIZED_DATA_RELOCATION_SURFACE_2026-08-03.md](FOLLOWUP_INITIALIZED_DATA_RELOCATION_SURFACE_2026-08-03.md)。

随后把 IDA 的 code-address-like DataRef 全量分流：27 条 `.text` 引用精确拆成 10 行真实
callable 取址、15 行 packed `0xFFFFFF` mask 与 2 行 MDF magic；唯一 `.plt` 引用则是
PSB magic。10 行形成 7 个 callback/member-function target、4 个 producer owner，全部为
normal flow；18 个数值碰撞也由 exact word 与 AArch64 immediate 解码证明。IDB 已删除
全部伪 dref并留下 magic collision 注释。AutoRegister、typed Factory/Property/Method、
CreateEmptyAdaptor、finalize 与 dummy constructor 的本地宏/模板调用链逐项一致，无生产
GAP、无 `cpp/` 修改。详见
[FOLLOWUP_ADDRESS_TAKEN_CALLABLE_SURFACE_2026-08-03.md](FOLLOWUP_ADDRESS_TAKEN_CALLABLE_SURFACE_2026-08-03.md)。

DataRef census 的最后 20 行 no-segment target 随后也被独立闭合：fresh IDA
`get_tid_name` 证明它们是 7 个 typed stack-local stroff identity，而不是 ELF 地址或待删
假引用。20 行对应 19 个 exact-word site、24 个源码字段事件，精确分为
`7 read / 13 write` 与 `18 normal / 2 landing`；两条 landing 均为 `PSBFile::Load`
销毁空 OwnerFilter 的 manager read。raw-node 默认构造、Resolve current.node 状态机、
NCB delegate 默认值与 setter params functor 均与当前源码一致，无生产 GAP、无 `cpp/`
修改。详见
[FOLLOWUP_STACK_LOCAL_DREF_SURFACE_2026-08-03.md](FOLLOWUP_STACK_LOCAL_DREF_SURFACE_2026-08-03.md)。

完整 cot_num 表面随后也被机械闭合：1,181 条数值表达式覆盖 95 个 owner，
1,133 条有具体 EA、48 条为 synthetic，全部落到 1,055 个 normal-entry exact-word
anchor。它与 1,208-row raw machine-immediate census 只有 485 个 site 交集，证明 ctree
源码常量不能由 ARM64 immediate 简单替代，frame/address/system-register immediate 也不能
反向写进 C++。fresh 反编译确认 classifier tag、packed width/mask、TJS error/flag、
callback arity、numeric shift/default 和当前源码一致，无生产 GAP、无 cpp/ 修改。详见
[FOLLOWUP_NUMERIC_CTREE_SURFACE_2026-08-03.md](FOLLOWUP_NUMERIC_CTREE_SURFACE_2026-08-03.md)。

完整 Hex-Rays 局部变量面随后也被机械闭合：114 个 FDE 共恢复 1,056 个 lvar 声明，
其中 `311 argument / 111 stack / 945 register / 72 result / 58 byref`；3,073 条
`cot_var` 使用覆盖 770 个声明，286 个声明没有最终 use。1,686 条具体 EA 与 1,387 条
optimizer-synthetic 使用全部落到 2,214 个 normal-entry exact-word anchor，landing-only
为 0。该声明/使用分层避免把 ARM64 location、stack offset 或 O3 删除的临时量误写成
wasm32 layout；fresh 反编译确认当前 Variant、字符串、raw node、OwnerFilter、
paramsFunctor、hidden result 与 NCB wrapper 生命周期一致，无生产 GAP、无 `cpp/` 修改。
后续 ECT1 逐行交叉纠正旧 LVS1 中两处重复实参的 relation：
`0x59A968:17 a0→a4` 与 `0x59AD84:30 a0→a5`；旧生成器按 `equal_effect`
误取同一 lvar 的首个 call child，其余 3,071 个使用行不变。
详见 [FOLLOWUP_LVAR_CTREE_SURFACE_2026-08-03.md](FOLLOWUP_LVAR_CTREE_SURFACE_2026-08-03.md)。

完整 Hex-Rays statement/control-tree 随后也被机械闭合：114 个 FDE 共 2,922 个
`cinsn/cit_*` 节点，精确覆盖 `759 block / 1379 expr / 321 if / 42 switch / 27 loop /
177 break / 1 continue / 46 goto / 164 return / 6 empty`。2,882 个 concrete 与 40 个
optimizer-synthetic 节点全部落到 1,942 个 normal-entry exact-word anchor；block/if/
loop/switch 父子次序、119 种 relation、122 种 detail、46 个 goto 到 36 个同 owner label
及最大深度 15 均由独立门禁固定。fresh 反编译确认当前源码与唯一 `for`/`continue`、
DecodeName 的 STL 内联 loop、GetListAt 的最深 switch/cleanup 树及 Resolve 的 RAII loop
一致，无生产 GAP、无 `cpp/` 修改。详见
[FOLLOWUP_STATEMENT_CTREE_SURFACE_2026-08-03.md](FOLLOWUP_STATEMENT_CTREE_SURFACE_2026-08-03.md)。

完整 Hex-Rays expression tree 随后也被机械闭合：114 个 FDE 共 9,629 个
`cexpr/cot_*` 节点，覆盖 108 个 owner、42 种 op、17 种 relation、271 种类型与
360 种 operator detail；6 个零表达式 owner 均为已知 nullsub。7,077 个 concrete 与
2,552 个 optimizer-synthetic 节点全部落到 3,076 个 normal-entry exact-word anchor，
最大深度 11。门禁逐行交叉 1,935 个 statement root、1,181 个 `cot_num` 与修正后的
3,073 个 `cot_var`，全部零差异。fresh 代表函数对照未发现生产 GAP、无 `cpp/`
修改。详见
[FOLLOWUP_EXPRESSION_CTREE_SURFACE_2026-08-03.md](FOLLOWUP_EXPRESSION_CTREE_SURFACE_2026-08-03.md)。

完整 `cot_call` 调用表达式面随后从 ECT1 派生并与机器 callsite 双向分层：
405 个节点精确分为 `285 direct + 40 computed indirect + 80 helper intrinsic`，
覆盖 638 个有序实参与 405 个唯一 normal-entry anchor。325 个真实 transfer 全部命中
机器站点；机器侧多出的 200 个站点严格分成 194 direct 与 6 landing-only indirect，
保持为 EH、canary、implicit lifetime 与 compiler lowering。40 个 computed call
又与独立 indirect ABI surface 的 40 个 normal 站点完全相等。fresh direct/indirect/
helper/landing 代表对照无生产 GAP、无 `cpp/` 修改。详见
[FOLLOWUP_CALL_EXPRESSION_SURFACE_2026-08-03.md](FOLLOWUP_CALL_EXPRESSION_SURFACE_2026-08-03.md)。

完整 `cot_obj` 对象/地址叶节点面随后也从 ECT1 派生并与六个既有机器/数据表面闭合：
457 个节点覆盖 70 个 owner、154 个唯一目标、101 种类型与 447 个 normal-entry anchor；
八类互斥分区为 `285 direct callee + 7 address-taken callable + 18 numeric artifact +
47 literal + 24 address point + 1 literal pointer + 70 BSS object/subobject + 5 extern`。
它既阻止 `0xFFFFFF/MDF/PSB` 数值碰撞被提升成 callable，也解析 9 个 literal-pool
基址下标并闭合 canonical empty pointer。fresh 对照无生产 GAP；新 source-tree 正证据
同时把 COW empty storage 的旧范围由 8 bytes 就地纠正为 32 bytes。详见
[FOLLOWUP_OBJECT_EXPRESSION_SURFACE_2026-08-03.md](FOLLOWUP_OBJECT_EXPRESSION_SURFACE_2026-08-03.md)。

剩余 `cot_fnum/cot_empty/cot_helper` 叶节点随后也从 ECT1 完整闭合：118 行精确分成
`4 + 3 + 111`，覆盖 39 个 owner 与 87 个 normal-entry anchor。80 个 helper callee
与完整 `cot_call` helper 集合相等，31 个 `TPIDR_EL0` argument 又逐个与
`_ReadStatusReg` 共用同一 MRS anchor；四个 binary64 常量同时固定 nearest ECT anchor
与实际值 producer。六类 leaf 因而恰有 4,829 行，其余 4,800 行全部为有 child 的
内部 operator，完整覆盖 9,629 行。fresh 对照无生产 GAP、无 `cpp/` 修改。详见
[FOLLOWUP_RESIDUAL_LEAF_EXPRESSION_SURFACE_2026-08-03.md](FOLLOWUP_RESIDUAL_LEAF_EXPRESSION_SURFACE_2026-08-03.md)。

完整 `cot_memptr/cot_memref` member-expression 链随后也从 ECT1 派生并机械闭合：
625 个节点覆盖 63 个 owner、478 个 normal-entry exact-word anchor，分为
`427 memptr + 198 memref`、539 个 outer 与 86 个 nested base。旧 typed-member
读/写/synthetic 面有意只保留 outer projection；其 419 个 concrete signature 与
60 个 synthetic signature 现和 outer 集合双向相等，新面则进一步固定六种链形、
最大三层 member 深度以及 `537 var + 2 helper call` terminal base。fresh
owner→header→names/entries 与 wrapper/container 代表链对照无生产 GAP、无 `cpp/`
修改。详见
[FOLLOWUP_MEMBER_EXPRESSION_CHAIN_SURFACE_2026-08-03.md](FOLLOWUP_MEMBER_EXPRESSION_CHAIN_SURFACE_2026-08-03.md)。

完整 `cot_asg` 赋值表达式面随后也被机械闭合：1,123 个 assignment 覆盖
69 个 owner 与 1,052 个 normal-entry exact-word anchor；1,119 个 root 与四个
comparison/comma 内 nested assignment 均固定有序 `x=lvalue/y=rvalue`。直接 lhs
精确分为 `804 lvar + 158 member + 109 raw memory + 32 global object + 20 helper
pseudo-lvalue`，并分别与 LVS1、typed-member、RMC2、global-BSS、residual helper
面双向分层。fresh static-init/DecodeName/GetInt/media/unregister 对照无生产 GAP、无
`cpp/` 修改。详见
[FOLLOWUP_ASSIGNMENT_EXPRESSION_SURFACE_2026-08-03.md](FOLLOWUP_ASSIGNMENT_EXPRESSION_SURFACE_2026-08-03.md)。

完整 internal remainder 随后也从 ECT1 派生并机械闭合：1,980 个节点覆盖 74 个 owner、
30 种 op 与 1,352 个 normal-entry exact-word anchor，精确分为 `744 arithmetic +
689 cast + 257 predicate + 252 ref + 36 mutation + 2 comma`。252 个 address-of 与
36 个 RMW operator 分别和 local/member/raw/object 独立投影闭合；187 种 cast type pair
只作 recovered-type 精确矩阵，不从类型文本猜源语义。它与四个 dedicated projection
共同证明 `2820 + 1980 = 4800` 个 internal 节点无遗漏。fresh 对照无生产 GAP、无
`cpp/` 修改。详见
[FOLLOWUP_REMAINING_INTERNAL_EXPRESSION_SURFACE_2026-08-03.md](FOLLOWUP_REMAINING_INTERNAL_EXPRESSION_SURFACE_2026-08-03.md)。

完整 source/machine bridge 随后也被机械闭合：全部 4,956 条 normal-entry 指令严格分成
3,076 个唯一 ECT1 expression anchor 与 1,880 条 residual。后者按五族固定为
`781 computation + 554 memory + 370 branch + 143 return + 32 call`；554 条 memory
再分为 `418 stack + 42 switch-table + 94 semantic lowering`。94 条语义补集的
global/GOT/refcount、vtable、Variant、closure/output 与三个 reload 角色全部有独立
交叉或 fresh 反编译证据。无生产 GAP、无 `cpp/` 修改。详见
[FOLLOWUP_SOURCE_MACHINE_BRIDGE_SURFACE_2026-08-04.md](FOLLOWUP_SOURCE_MACHINE_BRIDGE_SURFACE_2026-08-04.md)。

随后继续完成 normal branch-predicate / NZCV producer contract：437 个条件 branch 覆盖
66 个 owner，精确形成 874 条 taken/fallthrough edge；其中 180 个 `B.cond` 全部沿唯一
线性 predecessor 回溯成功，producer 为 `176 CMP + 3 CMN + 1 SUBS`，未解析 0、最大距离
7。condition code 分为 73 equality、84 unsigned、23 signed；216 个 `CBZ/CBNZ` 的 W/X
宽度和 41 个 `TBZ/TBNZ` 的 bit 0/10/31 也逐行固定。fresh 反编译确认当前源码没有混淆
packed unsigned bounds、signed TJS/IndexOf/array 边界或 `TJS_MEMBERMUSTEXIST` bit 10，无
生产 GAP。详见
[FOLLOWUP_NORMAL_BRANCH_PREDICATE_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_BRANCH_PREDICATE_SURFACE_2026-08-03.md)。

## 最终结论

| 总判定 | 数量 | 含义 |
| --- | ---: | --- |
| `ALIGNED` | 99 | Android 当前可证明的六维范围内未发现确定偏差。 |
| `EVIDENCE_LIMITED` | 15 | 未发现确定偏差，但 stripped/O3/ABI 不能唯一恢复部分源码 token 或 factorization。 |
| `HAS_GAP` | 0 | 初审与二次复核发现的确定偏差均已按 Android 证据闭合。 |
| **合计** | **114** | 地址集合与 MANIFEST、TASK_TREE、报告集合完全一致。 |

初审及后续 consumer 复核闭合了九组由 Android 正证据确认的生产实现差异；下文按八个
闭环章节展开，其中“闭环五”合并记录 dispatch helper 与 callback 后 node reload 两组。
2026-08-02 的谱系分层复核又闭合
`GetListAt/GetDictionaryKeys/GetString/ContainsDictionaryKey/IsInstanceOf` 五组
negative-xref 误判，并恢复一只共享 MDF helper，故当前累计十五组：

1. `ttstr_IndexOfChar_guess@0x59A284`：恢复“单个 `tjs_char` 地址 + 显式长度 1
   allocator”，当前为 `tTJSString(&c, 1)`；
2. `PSBFile_Transfer_guess@0x598A64`：caller EH 表证明接口是 potentially-throwing，已从
   声明和定义移除错误的显式 `noexcept`；
3. `PSBRawNode_GetResource_guess@0x5996E4` 的真实 caller `sub_6DA454`：恢复一个未初始化
   `uint32` size 槽在三次资源读取间复用，并恢复 RL 的 signed `<1` gate、gate 前
   `sourceEnd` 形成及 palette signed `/4`；
4. `ResourceManager_loadResource@0x6A8D8C` 的 invalid-spec label 路径：删除 caller 自造的
   null→`""` 归一化，并把 strict raw-node、borrowed pointer、narrow `ttstr` 与 throw 放回
   同一 full-expression，恢复 `ttstr → raw-node` 的异常析构顺序；
5. `PSBValueDispatch_CreateVariant_guess@0x59673C`：由 `0x596BC4/0x596C70` 的完整 O3 内联克隆
   证明并恢复 String/Resource 两个源码级成员调用；resource null 路径保留未初始化 size，
   同时以当前 Clang 所需的条件实参守住 Android `chunkData` gate；
6. `PSBValueDispatch_EnumMembers@0x596F50`：Dictionary callback 后重新读取 `self->node`，
   再以 W32 wrapped displacement 和 `+1` 形成 value node，不再错误复用 callback 前缓存；
7. motionplayer 的共享 `ttstr` unordered-container hasher 及三条 ResourceManager consumer：
   恢复 null backing→0、Hint 复用/写回、computed-zero→`0xFFFFFFFF`，以及 UTF-16 separator
   与 nullable project-key 临时 key 数据流；
8. `SourceCache_ctor@0x6A78F4` / `loadSource@0x6A7BA8`：恢复全局 `Layer` CreateNew 调用链、
   原始 owner 参数 accessor、单一完整 Entry、未初始化颜色槽、copy-front/erase 生命周期；
9. `Motion_Player_findSource@0x6948E8` 与 `sub_695DE8@0x695DE8`：恢复 Win pixel raw-node
   临时的立即析构，以及 KrKr outer group-key vector 跨 packing/upload 的完整存活区间。
10. `PSBMedia_GetListAt_guess@0x5999F4`：恢复共享 classifier，以及 Dictionary 分支
    `keys + dead offsets` 两只顺序 packed view；
11. `PSBRawNode_GetDictionaryKeys_guess@0x598E64`：恢复共享 classifier、gate 后 reusable
    string 与 `keys + dead offsets` 对象拓扑；
12. `PSBRawNode_GetString_guess@0x598B58`：恢复共享 classifier 与单只 offsets view，
    保留 borrowed pointer、`0x2C→0` 和 width `1..5`；
13. `PSBRawNode_ContainsDictionaryKey_guess@0x5995D8`：恢复共享 classifier，同时保留
    gate 前临时 raw node、唯一 dictionary getter 调用及正常/异常析构；
14. `PSBValueDispatch_IsInstanceOf@0x596E24`：恢复共享 classifier，保留
    `membername` 早退、category `4..7` 的四类映射、默认 false 与单次 UTF-16 比较。
15. `PSBFile_Load@0x598268` / `LoadStorage@0x598538`：权威 arm64 两份完整 MDF clone
    约束全部行为，iOS arm64 `0x1000ED5B4` 独立复核共享解压边界；
    本地删除两份手写重复体，恢复
    `tryDecodeMdf_guess(source,size-in/out)`，同时保留两 caller 不同的 null fallback、
    buffer replacement 与泄漏边界。

`EVIDENCE_LIMITED` 不是低优先级、不是 oracle-inert 判定，也不是 GAP；它只表示二进制
证据不足以唯一证明原始 C++ 拼写。没有 Android 正证据时，本审计不把不确定性升级成差异。
本地 inline/helper/template 的反向清单与证据等级另见
[SOURCE_SURFACE.md](SOURCE_SURFACE.md)；它明确区分 emitted 完整性与 source token 完整性。

## 调度完整性与逻辑任务树

机械快照确认 `114/114` 个非 root function agent 全部完成。主 agent 只直接启动两个
Android 静态初始化根，后续节点严格由其 canonical parent 递归派发；每份报告记录的
agent path 与 [TASK_TREE.md](TASK_TREE.md) 推导出的唯一路径一致。

```text
ROOT
├─ 0x42CEF8 PSBFile_ncbClassInfo_static_init
│  └─ class-info 构造及 5 个 class-info 成员（C/H）
└─ 0x42CF28 psbfile_static_init
   ├─ PSBMedia pre-register / storage media / resource path（D/E）
   ├─ PSBFile AutoRegister / NCB adaptor / factory / root / load（C/D/F）
   │  ├─ PSBFile Load → owner/raw-node/STL 路径（D/G）
   │  └─ GetRootDispatch → PSBValueDispatch 全 vtable 与 packed helper（A/B）
   └─ AutoRegister Unregist → class-info Clear（C/F）
```

最终任务树还纳入了两项 fresh 证据驱动的关系纠正：

1. `0x59A968 → 0x597F08` 是 `[helper]`，不是 `[direct-call]`：unregister 中 Clear
   shape 被内联，`0x597F08` 没有该调用 xref。
2. `0x59B708` 只复制首个 `tTJSVariant` 参数；load wrapper 随后通过注册的 member
   pointer 间接进入 `0x598268`，因此两者不是一条保留的直接调用边。

非 canonical DAG 边只作为 cross-reference 记录，没有重复派发函数 agent。

## 六维总表

| 维度 | `MATCH` | `EVIDENCE_LIMITED` | `GAP` | `N/A` | 合计 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 源代码结构 | 99 | 15 | 0 | 0 | 114 |
| 数据流 | 114 | 0 | 0 | 0 | 114 |
| 调用链 | 114 | 0 | 0 | 0 | 114 |
| 对象生命周期 | 80 | 0 | 0 | 34 | 114 |
| 内部容器实现 | 22 | 0 | 0 | 92 | 114 |
| 边界行为 | 108 | 6 | 0 | 0 | 114 |

当前没有任何 `GAP` 或 `PARTIAL` 维度。`0x59A284`、`0x59673C`、`0x596F50` 与
`0x598A64` 中由正证据确认的结构/调用/数据/异常契约差异均已闭合；沿 MANIFEST 外
consumer 发现的 `0x6DA454`、`0x6A8D8C`、`0x6948E8`、`0x695DE8`、ResourceManager
hash/split 及 SourceCache 构造/Entry 差异也均已闭合。后者不改变 114-address 六维统计
分母。

2026-08-02 又对 MANIFEST 114 个目标逐项完成 caller 方向普查；2026-08-04 的权威 ELF
反向扫描纠正了旧 IDA xref 口径：349 个 IDA code-xref 中主簇外原始数为 305，但其中
`0x40CD20/0x423250` 是两只本地 weak `vector<string>` symbol 的
`ADRP/LDR/ADD/BR` PLT alias，并非 consumer call。真实 `.text` direct surface 为
303 个 `BL`、15 个目标入口、25 个 caller FDE 和 71 组去重 caller→target 对。沿尚未单独归档的
`ObjSource_width/height/clip/drawLayer@0x69D19C..0x69D6D8` fresh 反编译未发现新 GAP；
同时以 `0x69CCB8 → 0x6E45D8 → 0x6E4734 → 0x69D6D8` 证明 drawLayer 的
`tTJSVariant` by-value copy/call/destruct 生命周期。完整口径、伪代码与本地映射见
[FOLLOWUP_EXTERNAL_CONSUMER_XREF_2026-08-02.md](FOLLOWUP_EXTERNAL_CONSUMER_XREF_2026-08-02.md)。

同一 2026-08-04 反向门禁又 fresh 闭合 `LoadedResourceRecord@0x6EBCFC/0x6DB3E8`、
atlas record vector destructor `0x698074` 与 ObjSource native/adaptor 链
`0x6E3E28..0x6E407C/0x6FE774..0x6FE9F0`：record 构造顺序、异常 rollback、逆序析构、
`native && !sticky` ownership gate、七 qword adaptor vtable 与九个 raw-owner release
站点均和本地普通 C++/ncbind 模板一致。IDA 错并的 `0x6E40F0` 已按独立 FDE 拆开；无
生产 GAP、无 `cpp/` 修改。详见
[FOLLOWUP_EXTERNAL_CONSUMER_LIFECYCLE_SURFACE_2026-08-04.md](FOLLOWUP_EXTERNAL_CONSUMER_LIFECYCLE_SURFACE_2026-08-04.md)。

同一反向方向的非直接地址承载面现也已独立闭合。完整 `.text` 只有 8 个能落到
MANIFEST 页的 ADRP：7 个由 MANIFEST 内 4 个注册 owner 解析到真实 callback/member
pointer，另一个解析到独立 PackinOne callback `0x59B9C8`；四类替代绝对取址均为 0。
完整动态重定位表则固定 `62 R_AARCH64_RELATIVE = 2 init-array + 60 vtable`，以及两只
weak STL local definition 的 `R_AARCH64_JUMP_SLOT`。文件 GOT 初值是 PLT0，不再把
IDA 加载后已解析的 GOT 值误报成 initialized pointer。三类共覆盖 71 个唯一 MANIFEST
target，外部业务 FDE 的函数取址 owner 为 0；专属 vtable header/address point 也只有
MANIFEST 内引用。无生产 GAP、无 `cpp/` 修改。详见
[FOLLOWUP_INVERSE_POINTER_REFERENCE_SURFACE_2026-08-04.md](FOLLOWUP_INVERSE_POINTER_REFERENCE_SURFACE_2026-08-04.md)。

沿该反向面继续追踪唯一非空跨模块 `OwnerFilter` 后，完整 producer/consumer 生命周期现已
闭合。13 个 FDE 精确分成 `2 MANIFEST + 11 motionplayer external`；`.eh_frame` 证明旧
IDA 所合并的 `0x6A87D0` 薄包装器与 `0x6A87E8` copy-assignment body 实为两个独立
函数。门禁固定 32-byte TU-static、三处全局地址、四个 callable、六条 direct edge、
`1×op2 + 3×op3` manager 调用及 `Adopt@0x598858` 唯一 invoker。seed 8-byte capture、
TJS closure/control block、两个 setter 临时析构与 ResourceManager→LoadStorage→Adopt
const-ref 转发均与本地一致；无生产 GAP、无 `cpp/` 修改。详见
[FOLLOWUP_OWNER_FILTER_BRIDGE_LIFECYCLE_SURFACE_2026-08-04.md](FOLLOWUP_OWNER_FILTER_BRIDGE_LIFECYCLE_SURFACE_2026-08-04.md)。

沿同一路径继续回溯 setter 注册 owner 后，又关闭一项外部 motionplayer 生产 GAP。
Android 的 `emoteplayer_static_init@0x42EB00` 只登记
`emoteplayer_entry@0x682528` 一只 init callback；entry 在同一 Variant/refcount 生命周期内
完成 dependency load、完整 EmotePlayer class 挂接与两个 ResourceManager setter 注入。
本地现已删除 Motion 主表的提前 EmotePlayer row、split post callback、额外 guard/Variant
和 global Release。扩展后的门禁固定 8 FDE、7 个 UTF-16 literal、3 个唯一 callback 物化、
11 条 entry/class direct edge、Motion registrar 的 11 条 subclass edge、52 个 semantic
word 与五只完整 FDE；`forbidden_motion_hits=0`。随后继续关闭 Player/post-alias 开放项：
Motion 现按 `23 constants -> 11 subclasses -> 2 functions` 单流注册，Player 是第六条，
顶层 class、post alias、deferred function attach 与 `useD3D` 字典覆盖均已删除。扩展门禁
新增 23 条 constant edge、两个 function callback 物化与两个 member-add。继续 fresh
decompile `motionplayer_static_init@0x42EE18` 后，又删除二进制不存在的 pre/post/unregister
callback nodes 与两个 `ShortCutInitial*KeyMap` 表达式；完整 static-init FDE 和两个 UTF-16
零命中也进入门禁。canonical surface 为 10,062 bytes。21/21 cases、1555/1555
assertions 与 Web Debug link 通过。
psbfile 本体 10 文件未改，99/15/0 统计及源码快照不变。详见
[FOLLOWUP_EMOTE_REGISTRATION_INJECTION_SURFACE_2026-08-04.md](FOLLOWUP_EMOTE_REGISTRATION_INJECTION_SURFACE_2026-08-04.md) 与
[FOLLOWUP_MOTION_NAMESPACE_REGISTRAR_SURFACE_2026-08-04.md](FOLLOWUP_MOTION_NAMESPACE_REGISTRAR_SURFACE_2026-08-04.md)。

同一轮 retained-function 普查还发现共享 MDF decoder：权威 arm64
`0x598268/0x598538` 各保存一份完整 inline clone，iOS arm64 helper 恰有两个 caller。由此闭合本地两份手写重复体的
source-factorization GAP；证据和逐行映射见
[FOLLOWUP_MDF_HELPER_FACTORIZATION_2026-08-02.md](FOLLOWUP_MDF_HELPER_FACTORIZATION_2026-08-02.md)。

## A–H 分组汇总

| 组 | 逻辑范围 | 函数数 | `ALIGNED` | `EVIDENCE_LIMITED` | `HAS_GAP` |
| :---: | --- | ---: | ---: | ---: | ---: |
| A | `PSBValueDispatch`、packed dictionary helper、完整 vtable ABI | 42 | 35 | 7 | 0 |
| B | packed name decoder | 1 | 0 | 1 | 0 |
| C | NCB class-info、成员注册、factory/root callback | 10 | 10 | 0 | 0 |
| D | `PSBFile` load/adopt/owner 与 raw-node 访问器 | 19 | 14 | 5 | 0 |
| E | pre-register、`PSBMedia` storage/resource 路径 | 17 | 15 | 2 | 0 |
| F | AutoRegister、instance adaptor、NCB factory/property/method wrapper | 22 | 22 | 0 | 0 |
| G | emitted `std::vector<std::string>` 实例 | 1 | 1 | 0 | 0 |
| H | 两个静态初始化根 | 2 | 2 | 0 | 0 |
| **合计** |  | **114** | **99** | **15** | **0** |

## 114 个逐函数结论

以下地址按 [MANIFEST.md](MANIFEST.md) 的 A–H 顺序列出；每个地址在
[`functions/`](functions/) 下都有唯一完整报告。

### A（42）

- `ALIGNED`（35）：`0x5975C0`、`0x597AC0`、`0x597A40`、`0x597A20`、
  `0x597A18`、`0x597854`、`0x5976C4`、`0x5976BC`、`0x5976B4`、
  `0x5975E0`、`0x5975D8`、`0x5975D0`、`0x596F48`、`0x596F40`、
  `0x596F0C`、`0x596F04`、`0x596EF0`、`0x596EE8`、`0x596EE0`、
  `0x596ED8`、`0x596ED0`、`0x596E24`、`0x596E1C`、`0x596E14`、
  `0x596E0C`、`0x596D90`、`0x596D88`、`0x596D80`、`0x596D78`、
  `0x597A30`、`0x596F38`、`0x597A28`、`0x597A38`、`0x596F3C`、
  `0x597A2C`。
- `EVIDENCE_LIMITED`（7）：`0x59641C`、`0x59659C`、`0x597AD4`、
  `0x59673C`、`0x596BC4`、`0x596C70`、`0x596F50`。

### B（1）

- `EVIDENCE_LIMITED`（1）：`0x597B1C`。

### C（10）

- `ALIGNED`（10）：`0x597E98`、`0x597EA8`、`0x597EB8`、`0x597EC8`、
  `0x597ED0`、`0x597F08`、`0x597F24`、`0x597F38`、`0x5980F4`、
  `0x5981F8`。

### D（19）

- `ALIGNED`（14）：`0x598268`、`0x598538`、`0x598708`、`0x598A3C`、
  `0x598AAC`、`0x598960`、`0x598B3C`、`0x598C58`、`0x598D58`、
  `0x598E44`、`0x598E64`、`0x599174`、`0x5995D8`、`0x599554`。
- `EVIDENCE_LIMITED`（5）：`0x598A64`、`0x598B58`、`0x5992E8`、
  `0x599438`、`0x5996E4`。

### E（17）

- `ALIGNED`（15）：`0x59849C`、`0x5997F0`、`0x599830`、`0x599878`、
  `0x599888`、`0x5998A8`、`0x5998BC`、`0x5998C0`、`0x5998C4`、
  `0x59993C`、`0x5999F4`、`0x599E04`、`0x59A284`、`0x59A330`、`0x59A4B0`。
- `EVIDENCE_LIMITED`（2）：`0x599DD8`、`0x59A0B4`。

### F（22）

- `ALIGNED`（22）：`0x59A8D8`、`0x59A968`、`0x59AA84`、`0x59ABD8`、
  `0x59AC04`、`0x59AC0C`、`0x59AC7C`、`0x59AD08`、`0x59AD84`、
  `0x59AEE4`、`0x59AEEC`、`0x59B14C`、`0x59B268`、`0x59B28C`、
  `0x59B378`、`0x59B460`、`0x59B484`、`0x59B48C`、`0x59B570`、
  `0x59B6DC`、`0x59B700`、`0x59B708`。

### G（1）

- `ALIGNED`（1）：`0x59B7E8`。

### H（2）

- `ALIGNED`（2）：`0x42CEF8`、`0x42CF28`。

## GAP 闭环一：`0x59A284`

完整证据见 [functions/0x59A284.md](functions/0x59A284.md)。

| 项目 | 结论 |
| --- | --- |
| Android 证据 | `0x59A2B0` 只写一个 16-bit `character`；`0x59A2B4` 置长度 `1`；`0x59A2B8` 调用显式长度 allocator `sub_A1381C(ref, 1)`。 |
| 修复前本地实现 | `tjsString.h` 的字符包装器调用 `tTJSString(c)`；公共单字符构造器形成 `{c, 0}` 并调用无长度 allocator。 |
| 当前本地实现 | `cpp/core/tjs2/tjsString.h:452` 调用 `tTJSString(&c, 1)`，经 `:78-79` 直接进入 `tjsVariantString.cpp:557-566` 的显式长度 allocator。 |
| 闭合维度 | 源代码结构、数据流、调用链均由 `GAP` 变为 `MATCH`。 |
| 生命周期与边界 | 仍保留一个临时 `ttstr`、正常/EH 条件 Release、空 self/空字符/miss 返回 `-1`、命中索引与 `start` 原样传递。 |
| 底层读取次序 | fresh `sub_A13708@0xA13708` 与 `sub_9B16A4@0x9B16A4` 证明 Android 在长度 `1` 路径同样观察下一 code unit 后按长度复制并补 NUL；本地不增加 `{c,0}` 安全垫。 |

该闭环只修改字符版 wrapper，没有改动全局 `tTJSString(tjs_char)`，避免把单函数证据扩散
到 TextRender、Storage、LayerExDraw 等其他尚未完成对应反编译的路径。Web Debug 已完整
构建通过；新增原生单测的源文件已编译，并在不改源码/CMake 的临时重链接中补入仓库
已有 `libexpat.a` 后实际运行通过（1 case / 8 assertions）。常规测试 target 仍被既有的
`libarchive`/Expat 传递链接缺项阻断；独立的 `psbfile-dll` target 可正常链接，两个直接
覆盖 PSBMedia `'/'` 搜索路径的测试另通过 49 与 12 个断言，完整插件套件通过
`577 assertions in 10 test cases`。allocator overload 的源码级选择以 fresh IDA 与本地
调用面逐行对照为证明。

## GAP 闭环二：`0x598A64` potentially-throwing 接口

完整证据见 [functions/0x598A64.md](functions/0x598A64.md)。主线通过
`ida_tryblks.get_tryblks` 独立取得 caller `ResourceManager_loadResource@0x6A8D8C`
中覆盖 `BL 0x598A64` 的 C++ guarded range `[0x6A9204,0x6A9210)`，cleanup 从
`0x6A9410` 开始并最终 `_Unwind_Resume`。这不是 callee 内部 deallocation 异常进入
terminate 的同一层契约；旧本地显式 `noexcept` 会消掉 caller unwind edge。当前
`PSBRawFile.h:169` 与 `PSBRawFile.cpp:402` 均已移除该 token，正常 transfer 数据流不变。
补充 disasm 还确认 `sub_A0DE90` 不检查 data null，而是先读取 `data-8`；因此非法的
zero-ref owner 若 data 为 null，会在 owner delete 与 source-clear 前 fault。只有 data
非空且 deallocation 正常返回时，才会形成“owner 已删而 hidden result 仍悬垂”的边界。

## GAP 闭环三：`sub_6DA454` size 槽与 signed 消费

完整证据见 [FOLLOWUP_0x6DA454.md](FOLLOWUP_0x6DA454.md)。Android 三处 callsite
`0x6DA5A4/0x6DA608/0x6DA724` 都把同一 `[SP+0x24]` 传给
`PSBRawNode_GetResource_guess@0x5996E4`，且首个调用前没有初始化 store。当前
`SourceCache.cpp` 使用唯一的未初始化 `resourceSize`，同时供 pixel 解码和 palette
count 使用；没有用两个 `{}` 局部安全化该边界。后续完整 disasm 又证明
`0x6DA778 LDRSW` 后先在 `0x6DA780` 形成 `sourceEnd`，再以 signed `<1` gate 决定
RL8/RL32 是否解码；`0x6DA648..0x6DA6B4` 也以 signed `/4` 同时驱动 palette vector
与 `TVPReverseRGB`。两个 RL helper 和 palette 分支现已按同一顺序复刻，包括负值边界。

## GAP 闭环四：`ResourceManager_loadResource@0x6A8D8C` label 数据流与生命周期

完整证据见 [FOLLOWUP_0x6A8D8C.md](FOLLOWUP_0x6A8D8C.md)。Android
`0x6A9104..0x6A911C` 把 raw-node `GetString()` 返回值直接传给 `sub_A13878`；后者自身对
null/empty 返回空 `ttstr`。EH 表还证明 `0x6A9100..0x6A910C` 构造 `ttstr` 时 raw-node
临时仍存活；throw landing pad 从 `0x6A93C4` 起先释放 `ttstr`，再于
`0x6A93EC/0x6A9468..0x6A9484` 释放 raw-node。当前实现既删除了 caller 的额外
null-normalization，也以一条链式 full-expression 恢复该逆序析构。该 caller 在
114-address MANIFEST 外，因此不改变逐函数统计。

## GAP 闭环五：dispatch helper 调用与 callback 后 node reload

`CreateVariant_guess@0x59673C` 的 String/Resource 两段分别是相邻独立入口
`getString@0x596BC4`、`getResource@0x596C70` 的完整内联克隆；resource 的独立 out-store
在 caller 内被 SROA 消除。当前本地重新由 `CreateVariant_guess` 调用这两个成员，保留直接 narrow
Variant 赋值、未初始化 size 和 null Octet 生命周期。MacOS Release `-O3` 反汇编确认
`chunkData==null` 分支仍直接建立 null Octet，未读取 size，也未被当前 Clang 删除。

2026-08-02 的 fresh target 并排复核又确认：`CreateVariant_guess` 的 category、窄/宽 integer
与 Real 区域分别和 `GetTypeCategory@0x599554`、`GetInt/GetDouble@0x599438/0x5992E8`
中的逻辑同构；`IsInstanceOf@0x596E24`、`EnumMembers@0x596F50`、`GetCount@0x5975E0`、
`PropGet@0x597854`、`PropGetByNum@0x5976C4`、`GetString@0x598B58`、
`GetDictionaryKeys@0x598E64`、`ContainsDictionaryKey@0x5995D8` 与
`GetListAt@0x5999F4` 也保存 classifier 的完整 category-specialized residual。同源 iOS
iOS arm64 独立复核 classifier/numeric 分层；这些旁证只用于选择 target 已约束候选的源码
结构。当前本地因此恢复共享 helper 调用，删除旧的平级 float/double helper 与
对应手写展开；精确 identifier 和 member/free/header token 仍保留
`_guess/EVIDENCE_LIMITED`，统计不变。

`EnumMembers@0x596F50` 与 `GetCount@0x5975E0` 也已从完整 raw-tag clone 恢复共享
classifier 调用；EnumMembers 的 Dictionary
路径在 callback 后于 `0x59738C` fresh reload
`self->node`，再把 table-end 与 entry offset 先做 W32 合并、UXTW 加址并 `+1`；本地已从
旧的 callback 前缓存指针改为相同 reload。List 路径仍保留独立的 cached table base，
没有把两条 caller-specific 地址表达式错误合并。
`PropGetByNum@0x5976C4` 的 packed 解析分层又得到 iOS arm64 独立确认：该函数先在
caller 内联解首个 count 并完成 signed bounds gate，命中后才调用四字段 packed-view
constructor。当前本地因此继续保留 `main.cpp:220-243` 的首轮显式 decoder，以及
`main.cpp:263-273` 的第二轮 `PsbArray_guess` 构造；没有把首轮误合并进 view，也没有把
第二轮退回手写标量 clone。

同一复核纠正了 `GetListAt@0x5999F4` 的旧 negative-xref 推断：Android 中没有
`BL @599554` 只说明 classifier 被内联，不能证明源码拥有独立 raw-tag switch。iOS arm64
`0x1000EE50C` 保留 classifier call；Dictionary 分支又在
`0x1000EE560/0x1000EE570` 保留两次 packed-view constructor，且第二只 record 不消费。
当前本地已恢复这条 classifier 调用与
`keys + dead offsets` 对象拓扑；Release `-O3` 仍把 dead view 消除，符合 Android emitted
shape。

`GetDictionaryKeys@0x598E64` 也纠正了相同的 negative-xref 推断：iOS arm64
`0x1000EDBBC` 保留 classifier call，并在 `0x1000EDBDC/0x1000EDBF4` 保留两次
packed-view constructor；第二只 offsets record 不消费。当前本地已恢复共享 classifier 与
`keys + dead offsets`；
MacOS Release 对象在 `GetDictionaryKeys+0x38` 保留 classifier relocation，同时仍消除 dead
view，符合 Android target 的 emitted shape。

`GetString@0x598B58` 是第三处同类纠正：Android `0x598B58..0x598B84` 保存完整
category-4 specialized residual；iOS arm64 在 `0x1000ED968` 调 classifier、
`0x1000ED984` 构造 packed view。
当前本地已把手写 outer raw-tag switch 恢复为共享 classifier gate，保留单只 offsets view、
`0x2C→index 0`、width 1..5 与 borrowed pointer 返回。该修正不调用 raw-node category
wrapper；它直接恢复被 target O3 内联的底层 classifier source call。

`ContainsDictionaryKey@0x5995D8` 是第四处同类纠正：Android 在 gate 前先零构造
临时 raw node，随后保存完整 category-7 residual、唯一 raw dictionary getter 调用、
unknown-return→false continuation以及正常/异常 cleanup；iOS arm64
`0x1000EDF14/0x1000EDF2C` 保留 classifier/getter 调用。当前本地已恢复共享 classifier，
同时保持临时对象声明位置与
intrusive owner 生命周期。Mac Release object 在函数 `+0x24/+0x3C` 保留两条 relocation。

`IsInstanceOf@0x596E24` 是第五处同类纠正，也是穷举 classifier callsite 时补回的第六个
dispatch consumer。Android `0x596E3C..0x596EB4` 保存 category `4..7` 的完整 specialized
residual；iOS arm64 `0x1000EC350` 保留 classifier call。当前本地已
恢复 `classifier → 四类名字映射 → TJS_strcmp`，同时保持 `membername` 非空时先返回
`TJS_E_NOTIMPL`、不读 valid/owner、普通 category 默认 false。Release `main.cpp.o` 在
`IsInstanceOf+0x24/+0x44` 分别保留 classifier/`TJS_strcmp` relocation。
iOS arm64 全 `__text` 穷举得到恰好 10 个 classifier `BL` 加一条 raw wrapper
尾调用，即六个 dispatch、三个 raw-node、一个 media 和一个 wrapper，共 11 个 source
consumer；旧的 5-dispatch/10-consumer 计数已经就地纠正。

## GAP 闭环六：共享 `ttstr` 哈希与 ResourceManager consumers

完整证据见
[FOLLOWUP_RESOURCE_MANAGER_CONSUMERS_2026-07-26.md](FOLLOWUP_RESOURCE_MANAGER_CONSUMERS_2026-07-26.md)。
ResourceManager、Player 与 Emote 的各个实际 unordered-container 实例均独立证明：null
backing hash 为 0；非 null backing 先读 Hint，未缓存时执行 1025/9/32769 UTF-16 mix，
computed-zero 改为 `0xFFFFFFFF` 后写回 Hint。共享 functor、UTF-16 `/`/`:` separator
overload，以及两条 Variant project-key 的 nullable raw pointer 数据流现均按该证据复原。

## GAP 闭环七：`SourceCache` 构造、Entry 与 list 生命周期

完整证据见
[FOLLOWUP_SOURCECACHE_2026-07-26.md](FOLLOWUP_SOURCECACHE_2026-07-26.md)。构造函数与 miss
路径均恢复 `TVPGetScriptDispatch → CreateNew(L"Layer", {owner,primaryLayer})`；strict
accessor 从原始 owner 形参构造，而 `_owner` 是独立成员复制。`loadSource` 现从入口只构造
一个完整 Entry，保持 byteWeight=0、blend/colors 的未初始化边界，并以 Android 的
copy-front/erase 而非 splice 复刻引用计数与销毁顺序。

## GAP 闭环八：PlayerResource 的 raw-node 与 key-vector 生命周期

完整证据见
[FOLLOWUP_PLAYERRESOURCE_LIFETIMES_2026-07-26.md](FOLLOWUP_PLAYERRESOURCE_LIFETIMES_2026-07-26.md)。
Win pixel lookup 的 strict raw-node 临时现于 `GetResource` full-expression 末释放，父
`textureNode` 继续持有 borrowed chunk；KrKr atlas 的 outer group-key vector 现从
`0x69616C` 对应声明点跨 record packing、texture 创建与 upload 存活，内层 icon-key
vector 则仍在每组末析构。

## 15 个证据受限结论

- packed view / scalar replacement / first-fault 源码顺序无法唯一恢复：
  `0x59641C`、`0x59659C`、`0x59673C`、`0x596BC4`、`0x596C70`、
  `0x596F50`、`0x597B1C`、`0x598B58`、`0x5992E8`、`0x599438`、
  `0x5996E4`、`0x59A0B4`。
- 对其中 packed-view 一组，iOS arm64 `0x1000EE0C4` 独立保留
  `nBytes/count/width/data` 四字段构造边界。这支持当前 record 与字段次序，但没有恢复 target 的精确
  类型/字段名、member/free/header-inline token 或损坏输入 first-fault 源码顺序，因此
  不改变这些 verdict。
- stripped ABI 无法唯一恢复 pointer/reference、成员/free helper 或 special-member
  精确 token：`0x597AD4`、`0x598A64`。
- `0x599DD8` 的 11 指令序列由两处同编译器已知 `ttstr::Clear()` 形成，而已知
  `name = ""` 会多出 allocator 调用，故 `Clear()` 是证据最强的重建；但仍不能唯一排除
  `name = ttstr()` 等编译期已知-null 临时赋值。iOS arm64 `0x1000EE728` 生成同一语义
  序列，确认这条 token 歧义没有被跨平台旁证消除。

上述 15 个入口当前均无未闭合 GAP。二次复核在 `0x59673C/0x596BC4/0x596C70` 间以
完整内联克隆恢复了 string/resource helper 调用，又在
`0x59673C/0x599554/0x596E24/0x596F50/0x5975E0/0x5976C4/0x597854/0x598B58/0x598E64/0x5995D8/0x5999F4` 间恢复 classifier 分层，并在
`0x59673C/0x5992E8/0x599438` 间恢复 scalar helper 分层；在
`0x596F50` 闭合了 callback 后 node reload，
在 `0x598A64` 以 caller EH 表修正 potentially-throwing 接口；沿 `0x598B58`、
`0x5996E4`、`0x597B1C` 的真实 consumer 又闭合了 MANIFEST 外的 raw-node、signed size 与 key-vector
生命周期差异。报告仍保留精确命名、pointer/reference、packed view 及损坏内存 first-fault
等真正无法唯一恢复的证据上限，没有把“无法唯一恢复源码拼写”误写成完全对齐。

外部 Windows `psbfile.dll` 的只读静态调查确认了 M2 同产品谱系，也同时发现 packed
width、numeric 符号、integer ABI、resource helper 拆分和 name-decoder 容器等明确
revision 差异；因此没有一个 Android verdict 可由它关闭。样本身份、差异矩阵和为何
不得把该材料用于 `cpp/` 的规则见
[EXTERNAL_LINEAGE_NON_AUTHORITY_2026-07-26.md](EXTERNAL_LINEAGE_NON_AUTHORITY_2026-07-26.md)。

15 项的 Android-only 再审计、caller EH/LSDA、SROA、numeric clone 与 stripped ELF
证据边界已集中归档在
[FOLLOWUP_EVIDENCE_LIMITED_2026-07-26.md](FOLLOWUP_EVIDENCE_LIMITED_2026-07-26.md)。
该轮新增硬证据但没有发现新 GAP，也没有把不可识别的源码 token 冒充为已恢复。

Android 1.3.9 的 relocation-only 映射、iOS 1.3.9 的 `CreateVariant` assert、packed-view/
numeric helper 与 resource 调用链旁证见
[ANDROID_139_IOS_LINEAGE_2026-07-26.md](ANDROID_139_IOS_LINEAGE_2026-07-26.md)。
该材料现还包含全部 15 项的 iOS arm64 映射、helper/lifecycle 边界与 `PSBMedia` vtable
交叉核验。它没有关闭任何
`EVIDENCE_LIMITED`；只在 target 完整 inline clones 已独立约束的
`0x59673C/0x596E24/0x598B58/0x598E64/0x599554/0x5995D8/0x5992E8/0x599438/0x5999F4/0x59A0B4` 上推进了源码调用链/分层复原，并强化
`PsbArray_guess` 的 record/字段次序依据。

Git 历史随后又提供一份不同 SHA/Build-ID 的 stripped Android ARM64 `libkrkr2.so`。
新增门禁直接读取其 blob，并证明当前 114 个 FDE 全部固定平移 `+0x3E0`，39 张
LSDA/232 个 call-site 完全同形，5,525 条 psbfile 指令的操作码/寄存器骨架零差异；
136 个不同立即数全部是 `ADRP` 地址物化。历史树没有 PSB 源码/调试伴随物，动态符号
差异也仅来自无关 `std::bind` 实例，因此这只是同一机器骨架的第二链接布局，不能升级
15 个 token 上限。fresh `0x59641C/0x597AD4/0x599DD8` 复核、制品身份与可重复命令见
[FOLLOWUP_HISTORICAL_ANDROID_ARM64_LINEAGE_2026-08-03.md](FOLLOWUP_HISTORICAL_ANDROID_ARM64_LINEAGE_2026-08-03.md)。

当前又把 normal CFG 中全部 216 个 `CBZ/CBNZ` 与 41 个 `TBZ/TBNZ` 做完整
tested-register reaching-definition 闭环。257 个 branch 共得到 287 条关系：
`246 instruction writer + 14 entry parameter + 27 W0/X0 call return`；244 个单来源、
13 个多来源 join 的全部 predecessor 路径均在声明的 producer 精确停止，且没有跨越
volatile `X1..X18` call clobber。producer 指令又按
`147 memory / 51 transfer / 12 atomic-status / 36 scalar` 分类；20 个 direct call return、
7 个 indirect call return 和 ARM member-pointer low-bit 路径均与当前 packed/raw/media/
ncbind 源码一致。6,498-byte manifest、IDA 注释、可重复 verifier 与 fresh 对照见
[FOLLOWUP_NORMAL_CB_TB_PRODUCER_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_CB_TB_PRODUCER_SURFACE_2026-08-03.md)。
本轮没有生产 GAP，也没有修改 `cpp/`。

## 构建与运行时验收

- API 31 ARM64 AVD 上新增的 `--media-adaptor-null` oracle 使用现有
  `ezsave.pimg` 直调 `EnsureContainer@0x599E04`：class-object 槽为 null 时实测
  `true + Void _file + updated container`，恢复原指针后的同 container 重试实测
  `true + non-null Object/ObjThis`，整体 `status=ok`。完整方法、制品 hash 与证据边界见
  [FOLLOWUP_CREATE_ADAPTOR_NULL_RUNTIME_2026-08-02.md](FOLLOWUP_CREATE_ADAPTOR_NULL_RUNTIME_2026-08-02.md)。
- 当前最终源码的原生三套目标均重编、运行通过：
  `motionplayer-ttstr-hash-test` 为 `109 assertions in 24 test cases`，
  `motionplayer-dll` 为 `1386 assertions in 21 test cases`，`psbfile-dll` 为
  `598 assertions in 11 test cases`；Debug/Release 均通过。
- Web Debug 在确认无 `coi-server` 后重编 psbfile 三个翻译单元并重新链接成功；最终
  `index.wasm` 为 `84,490,820` bytes，`index.html/index.js/vlfs.js/assets.zip` 分别为
  `85,083/619,201/41,636/7,854,055` bytes，均非空，
  且没有晚于 wasm 的 psbfile/motionplayer 源文件。Emscripten 4.0.23 不再产生独立
  `index.worker.js`/`index.data`。
- MacOS Release `-O3` 的 `psbfile` 目标在本轮结构重构后重建通过。当前对象在
  `0x3CC/0x624/0xC28/0xDF8/0xEE4/0xF88` 分别为
  `PropGet/CreateVariant_guess/PropGetByNum/GetCount/IsInstanceOf/EnumMembers` 以及 `PSBMedia.cpp.o` 的
  `GetListAt+0x50`、`PSBRawFile.cpp.o` 的 `GetDictionaryKeys+0x38` 与
  `GetString+0x18`、`ContainsDictionaryKey+0x24` 保留到 classifier 的 relocation，
  `ContainsDictionaryKey+0x3C` 另保留到 dictionary getter 的 relocation；两处 dead offsets view 均由 Release
  优化器消除，GetString 的单只 packed view 则被内联/SROA；
  `0x684` load `chunkData`，`0x688` 的 `cbz` 直达 `0x8B8` null 路径并于
  `0x8C4` 跳到 `0xA28`，绕过 `0x690+` packed 解码与 `0xA24` Octet allocator，证明
  classifier 调用结构与 compiler-boundary conditional 都在最终产物中成立；
  独立 `getResource` 空路径也不写 size。新增源码级 `tryDecodeMdf_guess` 被 Release
  优化器分别内联到 `Load@0x1584` 的 `0x15DC..0x16AC` 与
  `LoadStorage@0x1798` 的 `0x183C..0x189C`，产物继续呈现 Android 的两份完整 clone。
- 为让 Wasmtime LLDB 驱动不受 Homebrew Python 升级影响，本轮在 `/tmp` 的一次性 Python
  3.12 环境安装依赖并固定 `wasmtime==43.0.0`；没有修改仓库或全局 Python 环境。
- 最终 Wasmtime guest 在最后一处 `cpp/` 更新后重新构建成功，大小为
  `148,876,277` bytes，mtime 新于全部相关 psbfile 源码。随后以现有
  `logo_test_oracle_m2logo_15hz.xp3` 启动 LLDB runner，但在 630 秒硬超时后以
  `Wasmtime LLDB trace timed out` 退出，stdout/stderr 为空且未生成 trace；因此没有继续
  重复更长的 yuzulogo 采集，也不把该工具超时写成行为 mismatch 或 PASS。
- 在本次 `ContainsDictionaryKey` 更新之前，上一轮 guest 曾用 per-case
  `logo_test_oracle_m2logo_15hz.xp3` 与 `logo_test_oracle_yuzulogo_15hz.xp3` 分别导出
  25/63 个采样帧；当时 trace SHA-256 为
  `fea3753071a3860a942fb259f40a2278a0a56a3ed7320b4f624320ec258bf454` 与
  `079d965e46c467611858d7737f2ca2a3aff6d19ee4b8370d02b29a978960c6e3`。
- 这两个历史 hash 与当时在隔离 worktree 重建的 detached `HEAD@7e105c9` trace 完全相同，
  只证明此前九组生产闭环没有给这两条 motion 路径引入新增状态差异；它们不冒充本次
  classifier-only 更新后的运行时证据。本次更新由 fresh 反编译、Android/iOS arm64 调用边界、
  Mac 两套 583/1386/109 回归、Release relocation 与 Web/Wasmtime build 守护。
- 当时 strict oracle 也并非全绿：m2logo/yuzulogo 分别仍有 31/21 个 opacity ±1 structural
  mismatch，完整数值比较分别为 960/251 mismatch；两种 comparator 都因此返回 1，
  但没有构建、LLDB、guest、XP3、帧缺失或崩溃错误。HEAD 基线产生相同 structural
  mismatch；因此这里只把 trace 等同作为非回归证据，不把既有 drift 隐瞒成 PASS，
  也不反向削弱 fresh 反编译证明的 oracle-inert 架构复原。

## 机械验收

[verify_audit.py](verify_audit.py) 对以下条件执行失败即退出的检查：

- MANIFEST、TASK_TREE、报告地址集合均为 114，双向完全相等；
- TASK_TREE 恰有两个根、parent 全存在、关系标签合法、图无环；
- 114 个报告路径均匹配真实递归 subagent path；
- 每份报告都有本轮日期、fresh decompile、本地精确行号、cross-reference、确定 GAP
  声明、不超过 10 行的 Android 伪代码、六个唯一维度状态与唯一总判定；
- `HAS_GAP` 与维度中的 `GAP/PARTIAL` 双向一致；
- `cpp/plugins/psbfile/` 的完整文件集合与 `SOURCE_SNAPSHOT.sha256` 双向相等，且 10 个
  文件的 SHA-256 全部匹配，防止源码漂移后继续沿用陈旧审计；
- 总判定解析只接受“总判定”章节中的独立 verdict 行，避免把“总判定不写成
  `ALIGNED`”之类说明句误统计为 `ALIGNED`。

最终运行结果为 `PASS`，核心输出：

```text
task_tree_nodes=114 task_tree_unique=114 manifest=114 reports=114
task_tree_manifest_reports_sets_equal=true graph_acyclic=true roots=2
expected_agent_paths=114 fresh_decompile_reports=114 schema_valid_reports=114
source_reference_reports=114 source_references=1694 semantic_anchors=7
source_snapshot_files=10 source_snapshot_sha256=true source_snapshot_set_equal=true
verdicts=ALIGNED:99,EVIDENCE_LIMITED:15
dimension[调用链]=MATCH:114
```

另运行 [verify_elf_surface.py](verify_elf_surface.py)，当前静态对象、数据与调用面输出为：

```text
static_object_surface=true surfaces=10 qwords=177
init_array_order=true vtable_prefixes=true wrapper_tables=true
literal_surface=true literals=34 bytes=1268 pointers=2 utf8_utf16=true
switch_surface=true tables=42 entries=915 destinations=194 owner_fdes=true
switch_selector_surface=true owners=20 switches=42 selectors=42 raw_tag_loads=41 chained_normalized=1 unique_producers=32 single_source=42 volatile_call_clobbers=0 sha256=true
switch_dispatch_chain=true normalizers=41 zero_based=1 unsigned_hi_guards=42 table_bases=42 ldrsw=42 dispatch_adds=42 branches=42 chain_instructions=335
callsite_surface=true sites=567 direct_calls=468 direct_tails=11 indirect_calls=45 indirect_tails=1 switch_dispatches=42
callsite_targets=86 internal_sites=44 internal_edges=39 external_sites=435 no_transfer_functions=54 sha256=true
internal_call_contract_surface=true sites=44 edges=39 roles=21 result_classes=8 direct_tails=2 hidden_sret=2 special_producers=7 parameter_roles=true result_consumption=true
external_callee_surface=true targets=65 sites=435 roles=9 classified=true
external_consumer_surface=true sites=303 targets=15 owners=25 owner_target_pairs=71 direct_calls=303 direct_tails=0 target_roles=15 owner_roles=22 owner_fdes=true sha256=true
external_consumer_lifecycle=true lifetime_fdes=12 raw_release_sites=9 helper_edges=2 adaptor_qwords=7 plt_aliases=2 plt_aliases_are_calls=false vtable_prefix=true
inverse_pointer_reference_surface=true page_candidates=8 manifest_materializations=7 materialization_owners=4 external_materialization_owners=0 page_collisions=1 adr=0 ldr_literal=0 mov_wide=0 logical_immediate=0 relative_slots=62 init_array=2 vtable_slots=60 plt_aliases=2 targets=71 sha256=true
owner_filter_bridge_surface=true fdes=13 manifest_fdes=2 external_fdes=11 global_materializations=3 callable_materializations=4 direct_edges=6 manager_calls=4 op2=1 op3=3 invoker_calls=1 semantic_words=32 byte_ranges=9 range_bytes=1948 atexit=3 split_assignment_fdes=true sha256=true
indirect_abi_surface=true sites=46 fixed_loads=44 member_pointer_loads=2 roles=18 producer_words=true target_registers=true
indirect_argument_surface=true owners=18 sites=46 normal=40 landing=6 blr=45 br=1 operands=117 relations=120 single_source=114 multi_source=3 max_sources=2 instruction=119 entry=1 call_return=0 call_clobber=0 entry_residue=0 paths_complete=true sha256=true
indirect_argument_abi=true x0=46 x1=23 x2=19 x3=13 x4=6 x5=4 x6=3 x7=3 pointer=89 signed=16 unsigned=12 max_arity=8 target_registers=5 semantic_roles=18
indirect_argument_source_classes=true memory=17 transfer=77 address=23 select=2 volatile_call_clobbers=0 producer_classes=6
typed_member_instruction_surface=true sites=385 owners=62 owner_fdes=true instruction_words=true
typed_member_write_surface=true owners=32 sites=101 events=108 normal=108 landing=0 w=105 rw=3 strb=9 strh=8 str=70 stp_events=21 stp_sites=14 paths_complete=true sha256=true
typed_member_write_sources=true relations=109 single_source=107 multi_source=1 max_sources=2 instruction=84 entry=3 zero=22 call_return=0 call_clobber=0 entry_residue=0 memory=21 transfer=14 address=47 arithmetic=2 volatile_call_clobbers=0
typed_member_write_types=true pointer=74 bool=6 signed=8 unsigned=19 aggregate=1 base_types=14 field_types=18 banks=18 producer_classes=8
typed_member_read_surface=true owners=61 sites=290 semantic_events=311 normal=311 landing=0 r=297 rw=3 address=11 producer_rows=267 producer_lanes=268 anchor_rows=44 anchor_lanes=45 paths_complete=true sha256=true
typed_member_read_consumers=true relations=288 single=257 multi=11 max=5 use=196 transform=46 call_use=37 call_boundary=6 terminal=3 pre_event_loops=0 volatile_call_clobbers=0
typed_member_read_anchors=true sources=45 single=45 multi=0 max=1 instruction=42 zero=3 producer_classes=7 anchor_classes=5 base_types=33 member_types=32
typed_member_synthetic_surface=true owners=33 semantic_rows=67 occurrences=73 sites=73 normal=73 landing=0 w=42 r=15 rw=7 address=3 single=62 multi=5 max=3 ea_backed=416 total=483 paths_complete=true sha256=true
typed_member_synthetic_realization=true assignment_store=47 read_modify_write_store=7 address_store=3 direct_load=6 indirect_call_target=3 switch_dispatch=2 cast_store=5 machine_stores=62 str=53 stp=8 strb=1 ldr=6 blr=3 br=2 instruction_intersection=11 expressions=41 base_types=17 member_types=12
typed_member_promotion_surface=true owners=2 sites=5 consumers=5 normal=5 landing=0 r=5 roles=3 zero_ref_guard=1 dealloc_argument=3 owner_header_chain=1 legacy=483 promoted=5 total=488 instruction_words=true paths_complete=true
raw_memory_surface=true owners=56 rows=667 ptr=461 idx=206 r=407 w=111 rw=5 address=144 ea_backed=496 synthetic=171 ea_sites=475 synthetic_sites=169 anchors=610 normal=610 landing=0 single=554 shared=56 max=3 typed_intersection=24 mnemonics=25 parents=16 mode_parents=4 semantic_bytes=85201 semantic_sha256=true instruction_words=true paths_complete=true
numeric_ctree_surface=true owners=95 rows=1181 ea_backed=1133 synthetic=48 ea_sites=1023 anchors=1055 normal=1055 landing=0 single=956 shared=99 max=4 parents=22 types=22 mnemonics=39 values=54 zero=286 one=281 semantic_bytes=132066 semantic_sha256=true instruction_words=true paths_complete=true
lvar_ctree_surface=true declaration_owners=112 use_owners=77 declarations=1056 uses=3073 used=770 unused=286 args=311 stack=111 reg=945 result=72 byref=58 types=97 widths=8 definition_anchors=781 special_definitions=1 ea_backed=1686 synthetic=1387 ea_sites=1419 use_anchors=2214 single=1574 shared=640 max=6 r=2166 w=804 rw=20 address=83 parents=35 relations=11 mnemonics=65 semantic_bytes=362197 semantic_sha256=true instruction_words=true paths_complete=true
statement_ctree_surface=true owners=114 rows=2922 ops=12 blocks=759 expr=1379 if=321 switches=42 loops=27 breaks=177 continue=1 gotos=46 returns=164 concrete=2882 synthetic=40 ea_sites=1942 anchors=1942 normal=1942 landing=0 single=1124 shared=818 max=5 relation_catalog=119 relations=6 detail_catalog=122 detail_fields=47 labels=36 max_depth=15 semantic_bytes=273424 semantic_sha256=true instruction_words=true paths_complete=true
expression_ctree_surface=true functions=114 owners=108 zero_owners=6 statements=2922 rows=9629 roots=1935 ops=42 relations=17 types=271 details=360 concrete=7077 synthetic=2552 raw_sites=3058 anchors=3076 normal=3076 landing=0 single=674 shared=2402 max=20 max_depth=11 root_relations=6 synthetic_ops=18 statement_root_cross=1935 numeric_cross=1181 lvar_cross=3073 semantic_bytes=1025218 semantic_sha256=true instruction_words=true paths_complete=true cross_surfaces=true
call_expression_surface=true owners=61 rows=405 roots=252 direct=285 indirect=40 helpers=80 concrete=385 synthetic=20 args=638 max_args=8 return_types=14 callee_types=107 argument_types=64 callee_shapes=120 direct_targets=79 internal_sites=43 external_sites=242 internal_targets=21 external_targets=58 internal_edges=39 helper_names=8 raw_sites=385 anchors=405 normal=405 landing=0 source_transfers=325 machine_transfers=525 machine_only=200 source_direct=285 machine_direct=479 machine_only_direct=194 source_indirect=40 machine_indirect=46 machine_only_indirect=6 machine_direct_targets=86 machine_only_direct_targets=7 realizations=5 callee_ops=7 arities=8 semantic_bytes=144542 semantic_sha256=true instruction_words=true normal_indirect_complete=true source_machine_split=true
object_expression_surface=true owners=70 rows=457 roots=6 targets=154 types=101 classes=8 sections=7 containers=47 concrete=57 synthetic=400 raw_sites=57 anchors=447 normal=447 landing=0 single=443 shared=4 max=4 direct=285 address_taken=7 artifacts=18 literals=47 indexed_literals=9 resolved_literals=33 address_points=24 literal_pointers=1 global_objects=70 extern_imports=5 semantic_bytes=95535 semantic_sha256=true section_partition=true cross_surfaces=true paths_complete=true
residual_leaf_expression_surface=true owners=39 rows=118 roots=4 ops=3 relations=5 types=15 details=12 concrete=0 synthetic=118 raw_sites=0 anchors=87 normal=87 landing=0 single=56 shared=31 max=2 helper_callees=80 helper_arguments=31 helper_names=9 floating=4 empty=3 leaf_ops=6 leaf_rows=4829 internal_rows=4800 semantic_bytes=17017 semantic_sha256=true helper_call_cross=true floating_producers=true leaf_partition=true paths_complete=true
source_machine_bridge_surface=true functions=114 normal=4956 expression_anchors=3076 residual=1880 families=5 mnemonics=28 memory=554 stack=418 switch=42 semantic=94 semantic_roles=11 semantic_bytes=67886 semantic_sha256=true normal_partition=true ect_anchor_cross=true global_cross=true got_cross=true refcount_cross=true switch_bijection=true paths_complete=true
stack_local_dref_surface=true owners=7 targets=7 rows=20 sites=19 semantic_fields=24 read=7 write=13 normal=18 landing=2 roles=11 sp_relative=true stroff_namespace=true sha256=true
stack_frame_surface=true functions=114 framed=57 frameless=57 entry_frames=52 shrink_wrapped=5 canaries=31 lsda_frames=39 unwind_only_frames=18 gpr_patterns=10 simd_spills=1 allocation_words=true saved_registers=true
eh_surface=true lsda_fdes=39 unwind_only_fdes=75 lsda_reports=39
lsda_call_sites=true functions=39 call_sites=232 no_landing=77 cleanup=80 catch_all=75 reports=39
landing_pad_contract_surface=true owners=39 landings=150 cleanup_resume=75 terminate=72 catch_rethrow=3 unique_instructions=569 contract_instructions=1150 unique_transfers=168 direct_targets=14 blr_roles=3 normal_overlap=0 sha256=true
normal_cfg_terminal_surface=true functions=114 normal_instructions=4956 landing_instructions=569 fde_instructions=5525 edges=5337 terminals=208 returns=162 direct_tails=11 indirect_tails=1 noreturn_calls=34 partition_complete=true overlap=0
normal_return_abi=true classes=4 continuing_throw_calls=28 continuing_throw_targets=4 noreturn_targets=3 direct_tail_targets=6 prototype_classes=true successors=true sha256=true
normal_return_value_surface=true owners=72 returns=129 w0=96 x0=19 d0=14 relations=160 single_source=112 multi_source=17 max_sources=5 instruction=157 entry=0 call_return=3 volatile_call_clobbers=0 paths_complete=true sha256=true
normal_return_value_source_classes=true memory=9 transfer=116 arithmetic=32 direct_call_return=2 indirect_call_return=1 producer_classes=17
normal_call_result_first_event_surface=true owners=57 calls=311 direct=272 indirect=39 relations=419 single_event=234 multi_event=77 max_events=5 use=86 overwrite=250 call_boundary=49 ret_reaches=34 pre_event_loop_rows=4 unresolved_terminals=0 finite_paths_complete=true sha256=true
normal_call_result_classes=true direct_gpr=147 direct_void=125 indirect=39 direct_gpr_uses=71 direct_void_uses=0 indirect_uses=15 move=45 store=9 predicate=30 transform=2 direct_gpr_targets=54 direct_void_targets=21 use_classes=9
normal_direct_argument_surface=true owners=59 sites=317 bl=306 direct_tails=11 targets=80 zero_arg=37 operands=446 relations=475 single_source=435 multi_source=11 max_sources=13 instruction=447 entry=18 call_return=10 call_clobber=0 entry_residue=0 paths_complete=true sha256=true
normal_direct_argument_abi=true x0=280 x1=97 x2=48 x3=7 x4=3 x5=3 x8=7 d0=1 pointer=291 bool=2 signed=92 unsigned=50 integral_unspecified=10 floating=1 max_arity=6
normal_direct_argument_source_classes=true memory=80 transfer=257 arithmetic=107 writeback=3 volatile_call_clobbers=0 producer_classes=18
normal_branch_predicate_surface=true owners=66 branches=437 b_cond=180 cbz=159 cbnz=57 tbz=24 tbnz=17 edges=874 successors=true sha256=true
normal_nzcv_producer_surface=true producers=180 cmp=176 cmn=3 subs=1 unresolved=0 max_distance=7 equality=73 unsigned=84 signed=23 registers=true bits=true
normal_nzcv_input_surface=true owners=48 branches=180 operands=260 relations=320 single_source=235 multi_source=25 max_sources=9 instruction=313 entry=4 call_return=3 volatile_call_clobbers=0 paths_complete=true sha256=true
normal_nzcv_input_source_classes=true memory=148 transfer=60 select=4 arithmetic=101 direct_call_return=2 indirect_call_return=1 producer_classes=18
normal_nzcv_input_forms=true w=129 x=131 immediate=100 register=160 operand0=180 operand1=80 immediate_constants=12
normal_cb_tb_producer_surface=true owners=57 branches=257 relations=287 single_source=244 multi_source=13 max_sources=5 instruction=246 entry=14 call_return=27 volatile_call_clobbers=0 paths_complete=true sha256=true
normal_cb_tb_source_classes=true memory=147 transfer=51 atomic=12 scalar=36 direct_call_return=20 indirect_call_return=7 producer_classes=17
raw_lsda_topology=true functions=10 call_sites=51 no_landing=18 cleanup=16 catch_all=17 reports=10
```

逐函数 canonical 名称、分组和唯一报告入口见 [MANIFEST.md](MANIFEST.md)；真实父子拓扑与
非 canonical cross-reference 见 [TASK_TREE.md](TASK_TREE.md)。源码快照门禁的范围、语义与
当前构建/测试复核见
[FOLLOWUP_CURRENT_SOURCE_SNAPSHOT_GATE_2026-08-03.md](FOLLOWUP_CURRENT_SOURCE_SNAPSHOT_GATE_2026-08-03.md)。

另运行 [verify_historical_arm64_lineage.py](verify_historical_arm64_lineage.py)，独立输出：

```text
manifest_fdes=114 address_shift=0x3E0 fde_shape_mismatch=0
lsda_functions=39 lsda_call_sites=232 lsda_shape_mismatch=0
instructions=5525 exact=3958 address_only=1431 adrp_anchored_immediate_only=136 shape_mismatch=0
historical_only_dynsyms=8 psbfile_semantic_dynamic_symbols=0 debug_sections=0
```

该输出只证明历史 ARM64 构建没有增加独立 psbfile 代码/符号边界；它不参与当前权威制品的
`99/15/0` 计数，也不把相同 O3 机器骨架升级成唯一源码 token。
