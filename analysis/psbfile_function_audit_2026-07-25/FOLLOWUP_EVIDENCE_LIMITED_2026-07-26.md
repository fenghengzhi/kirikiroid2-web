# 15 个 `EVIDENCE_LIMITED` 节点的 Android-only 再审计

日期：`2026-07-26`。

## 结论

本轮由主线 fresh decompile 与六组独立只读 agent 对 15 个受限节点重新执行 Android-only
复核，并补查 caller、FDE/LSDA、vtable、stack frame、全 IDB 符号以及标量化模式。

- 新发现的确定生产实现 GAP：**0**。
- 可从 `EVIDENCE_LIMITED` 升为 `ALIGNED` 的节点：**0**。
- 因本轮证据需要修改的 `cpp/`：**0**。
- 统计继续为 `ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`。

这不是用 negative search 宣布“原 helper 不存在”。相反，本轮明确区分了两类事实：
机器码可唯一证明的运行时结构继续记为 `MATCH`；被 O3/SROA/ABI 合并抹除、且仍有多个
源码候选生成同一机器码的部分继续记为 `EVIDENCE_LIMITED`。

## packed view / scalar replacement

涉及：`0x59641C`、`0x59659C`、`0x59673C`、`0x596BC4`、`0x596C70`、
`0x596F50`、`0x597B1C`、`0x598B58`、`0x5996E4`、`0x59A0B4`。

Android 可唯一证明：

1. packed width tag `0x0D..0x11` 对应 stride 1..5；无效 width 在 entry 地址/读取前
   采用各 caller 已记录的零/base fallback。
2. index 与 `index*width` 保持 W32，地址扩展使用 UXTW；entry 是未对齐 u32 load 后
   按 AArch64 register-shift modulo 掩码。
3. `0x59641C/0x59659C/0x596BC4/0x596C70` 均为零栈局部、零 `BL/BLR` 的标量叶；
   `0x59673C/0x596F50` 的 packed 段也没有构造器或 `operator[]` 调用。
4. `0x596BC4/0x596C70` 的独立入口与 `0x59673C` 两段完整内联克隆，继续支持当前
   `CreateVariant_guess → getString_guess/getResource_guess` 调用拓扑；resource clone 中 size-out
   store 被 SROA 消除。
5. `0x597B1C` 的三表回溯、`std::vector<char>` 三指针增长、reverse、string assign 和
   异常清理可证明；第三张表未消费的 count/next 已完全消失。
6. `0x598B58` 返回 owner 内 `stringsData` 的借用指针；八个真实 caller 都在有效 owner
   持有期内消费该指针。
7. `0x5996E4` 的 raw-node 是 owner/node 两个槽，返回借用 chunk pointer；九个 caller
   的 cleanup-only LSDA 均允许调用展开异常，当前接口不带 `noexcept` 是正确的。
8. `0x59A0B4` 仍明确执行 Resolve → chunkData gate → offsets/lengths view →
   length 写 size → offset 形成借用指针，并在返回前释放局部 raw-node owner。

仍不可唯一恢复：

- Android arm64 目标单独不能证明四个标量原属一个 record；iOS arm64
  保留独立构造边界和 `nBytes/count/width/data` 字段次序，支持当前 `PsbArray_guess`
  的对象分层，但类型名、字段名、目标端
  member/free/header-inline 身份及 `operator[]` token 仍不可证；
- member、free、header-inline helper 或手写表达式之间的 factorization；
- pointer/reference 与 const 拼写；
- 被 dead-load elimination 删除的 count/nBytes 读取，以及损坏地址下优化前源码的
  first-fault 次序。

全 IDB 以多组名字、字符串、函数、全局和指令形状交叉搜索均没有发现独立 packed-view
ctor/operator 符号；该结果只用于确认没有新的正证据，不作为“不存在”的证明。

## numeric decoder factorization

涉及：`0x5992E8`、`0x599438`。

- 两个 emitted getter 的正常路径都没有拥有型栈对象、raw-node copy assignment、
  AddRef/Release 或 packed view；唯一业务调用是 default 错误分支。
- 所有实际 consumer 都保留对 getter 的直接 `BL`。strict lookup 产生的两指针 raw-node
  临时由 caller 在标量消费完成后释放，不能把 caller assignment 错归入 getter。
- `CreateVariant_guess@0x59673C` 保留第三份同构 narrow/wide decoder shape，证明共同算法形状；仅凭
  这轮 Android-only 证据仍无法区分共享 inline helper 与重复 switch。
- `GetInt` 的 tag `0x0B` 明确保留 low-word-only 特化，阻止把 wrapper 错写成无条件调用
  七字节 decoder。

2026-08-02 的后续复核补到独立分层旁证：同源 iOS 在
`CreateVariant/GetTypeCategory/IsInstanceOf/EnumMembers/GetCount/PropGet/PropGetByNum/GetString/GetDictionaryKeys/ContainsDictionaryKey/GetListAt` 保留 classifier call，在
`CreateVariant/GetDouble/GetInt` 保留窄/宽 integer 与 raw-double dispatcher 的 call，且
`GetInt` 仍把 `0x0B` 与 float/double 转换留在 wrapper。回到 target，各对应 Android 函数
已有完整或 category-specialized residual 独立约束所有行为，因此当前本地恢复这些共享
helper，删除旧 `DecodeFloat_guess/DecodeDouble_guess` 平级边界。
逐 tag 数据流、返回宽度和 caller 生命周期仍为 `MATCH`；Android 1.4.4 的精确 helper
名字及 member/free/header-inline token 继续受限。详见
[ANDROID_139_IOS_LINEAGE_2026-07-26.md](ANDROID_139_IOS_LINEAGE_2026-07-26.md)。

## dispatch constructor

涉及：`0x597AD4`。

- 三个 direct caller `0x6A931C/0x6AA124/0x6AA424` 都先 `new(0x30)`，再以
  `X1=owner slot/holder 地址`、`X2=node` 调用构造器，并忽略返回寄存器。
- 三处 constructor-throw landing pad 都删除刚分配的对象存储，再清理 caller 局部量并
  `_Unwind_Resume`，正证普通 C++ new-expression 构造生命周期。
- 构造器机器序固定为双 vptr、ref=1、复制 owner、非空 owner 的非原子 retain、复制
  node、valid=true。两个 vtable prefix 的 typeinfo 均为 null，没有 RTTI 名字锚点。
- 同形内联构造在 `0x5981F8` 已出现不同指令调度，说明优化器改变了成员初始化表面顺序。

iOS arm64 穷举复核同一结构：ctor `0x1000EC248` 的三个 caller
`0x100101630/0x100102024/0x1001021A4` 都分别从 standalone PSBFile 或 raw node 首子对象
传入同一 holder。raw-node ctor 与 `GetRoot` 又由 `0x1000EEF28/0x1000ED8C8` 独立证明
该首子对象复用 PSBFile assignment 生命周期。

因此普通构造器、最终对象 shape 和共享 holder 因子化得到权威 target 与独立 iOS
iOS arm64 的共同支持；旧结论中
“`PSBRawOwner **`、任意一指针 holder、owner-pointer reference 仍完全不可区分”的范围
已经被证伪。仍无法唯一选择的是共同 holder 的原始类型名、raw node 通过 member 还是
base 嵌入它，以及构造器的成员初始化/委托构造/直接字段 token。本地选择
`PSBValueDispatch(const PSBFile&, node)` 与 `PSBRawNode::GetFile_guess()` 表达当前
最强证据，并用 `_guess` 标出剩余 token 上限。

## consuming transfer helper

涉及：`0x598A64`。

- 唯一 caller 以 X8 hidden-sret 和 X0 source 调用；caller EH range 覆盖整条调用，异常
  清理不触碰未完成的 hidden result。
- zero-ref 分支与 `PSBRawOwner_dtor_guess@0x598B3C` 共享同一 aligned deallocator；
  `sub_A0DE90` 精确读取 `data-8` 保存的 `new[]` 原指针，非空时 `delete[]`，随后 caller
  再 scalar delete owner。data 为 null 时会在 owner delete/source clear 前 fault。
- 正常 owner 的 AddRef/Release 被优化相消，结果取得同一 owner、source 清空；当前
  Rule-of-Three 表达式保持这条对象生命周期与 potentially-throwing 接口。
- iOS arm64 保留 copy/AddRef、共享 Release 与 source-clear 的 special-member 序列；
  target arm64 的 zero gate是该净零序列的 O3 结果。

source 的实际对象身份、hidden-sret 完成边界和两层 delete 路径都已证明；但 helper 原名、
member/free 身份及 exact special-member token 仍不可恢复。

## empty `ttstr` token

涉及：`0x599DD8`。

目标 11 指令只执行旧 storage 的 null gate、一次 Release 和 null store；真实 storage-media
vslot 10 caller 与其异常清理均已证明。`name = ""` 会多出 allocator 调用，因此可排除。
但 `name.Clear()`、`name = ttstr()` 和 `name = {}` 在 O3 下都可把已知-null 临时量的
AddRef/析构/EH 完全删除，生成同一序列。另一个单槽 `ttstr` comparator不能替目标提供
源码 token；两槽 `PSBRawNode` 空赋值则可由 ABI 明确排除。

iOS arm64 的 media 对应槽也只执行 old-null gate、条件 Release 和 null store；它仍不能
区分上述多个能生成同一序列的空 `ttstr` token。本地 `name.Clear()` 是证据最强且所有
可观察维度一致的重建，不能据机器码把它提升为唯一源码拼写。

## ELF 与外部证据边界

权威 ELF 是 stripped，保留 `.dynsym/.dynstr` 和 `.eh_frame`，不含 `.symtab`、DWARF、
`.gnu_debugdata` 或 compressed debug section。`.comment` 同时包含 GCC 4.9、Android
Clang 3.8 与 Android Clang 5.0 标记，无法把某个工具链唯一归属到 psbfile translation
unit。不同 revision 的 Windows DLL 只能作为发现线索，其明确语义差异和不可采信规则见
[EXTERNAL_LINEAGE_NON_AUTHORITY_2026-07-26.md](EXTERNAL_LINEAGE_NON_AUTHORITY_2026-07-26.md)。

## 本轮写入与验证

- `0x5996E4.md` 已补入 FDE/九处 caller LSDA 与 potentially-throwing 接口证据；没有改变
  verdict。
- 修正了 12 份报告中此前只能通过“行号范围有效”、却没有真正覆盖所述字段/转调的源码
  语义锚点。
- 原生 Debug 三目标重建状态为 `ninja: no work to do`；随后测试通过：
  `109/24`、`1386/21`、`577/10`。
- `verify_audit.py` 与 `git diff --check` 继续作为最终机械门禁；本 follow-up 不改变
  MANIFEST、TASK_TREE 或 114 报告地址集合。
