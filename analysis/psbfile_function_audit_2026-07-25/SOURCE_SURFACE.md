# psbfile 本地源码面 → Android 证据映射

日期：`2026-07-26`。本文件补足 [MANIFEST.md](MANIFEST.md) 的反向检查：MANIFEST
穷举的是 Android `libkrkr2.so` 可归属 psbfile 的独立 emitted 入口；这里检查当前本地
`cpp/plugins/psbfile/` 中没有独立 Android 入口的 inline、模板、别名、特殊成员和 helper
factorization。两种方向不能互相替代。

## 权威制品与 emitted 边界

- 权威文件：`reference/libkrkr2/libkrkr2.so`，大小 `27,929,688` bytes，SHA-256
  `ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`，ELF Build-ID
  `985d9f685e07ce4497472523c3e84b1f38989235`。
- 该文件与 `reference/apk/krkr2 1.4.4.apk` 内
  `lib/arm64-v8a/libkrkr2.so` 的大小和 SHA-256 完全相同。
- 连续 psbfile 主实现簇为 `0x59641C..0x59B9C8` 的 112 个入口（末项
  `0x59B7E8` 为本源码触发的 `vector<string>` 扩容慢路径）；`0x59B9C8` 已由独立序言和
  `PackinOne.dll` 注册关系排除。另有 `0x42CEF8/0x42CF28` 两个静态初始化入口，总计
  114。地址集合、任务树和逐函数报告由 `verify_audit.py` 机械验证。
- 权威 ELF `.eh_frame` 现提供独立完成性证明：static-init 区间恰有 2 个 FDE，主簇恰有
  112 个 FDE；两段 start 集合都与 MANIFEST 完全相等，FDE 范围逐项首尾相接并分别精确
  结束于 `0x42CFA0`、`0x59B9C8`。两个 end 自身是下一模块的独立 FDE；fresh 反编译由
  `"PackinOne.dll"` registration 与八个子插件字面量证明其归属。可重复校验见
  [FOLLOWUP_ELF_FDE_SURFACE_VERIFIER_2026-08-03.md](FOLLOWUP_ELF_FDE_SURFACE_VERIFIER_2026-08-03.md)。
- 同一 ELF 校验现另固定完整 exception surface：114 个 MANIFEST FDE 中 39 个带
  LSDA、75 个只有 unwind 元数据。该独立拓扑复扫补齐了
  `GetDictionaryValue@0x598D58` 旧报告遗漏的 terminate/propagate/caller-cleanup
  三层边界；本地 owner 析构和 lookup 接口的 exception-spec 分层一致。完整地址集合与
  call-site table 解码见
  [FOLLOWUP_ELF_LSDA_SURFACE_2026-08-03.md](FOLLOWUP_ELF_LSDA_SURFACE_2026-08-03.md)。
- raw `PSBFile/PSBRawNode` 生命周期簇进一步把 10 张 LSDA 的 51 个 call-site entry
  逐字段固定为 18 个无 landing、16 个 cleanup-only、17 个 null-type catch-all；
  IDA guarded ranges/landing handler 与本地 RAII/exception-spec 分层一致。完整表与门禁见
  [FOLLOWUP_RAW_LSDA_CALLSITE_TOPOLOGY_2026-08-03.md](FOLLOWUP_RAW_LSDA_CALLSITE_TOPOLOGY_2026-08-03.md)。
- 同一方法现已覆盖全部 39 张 LSDA：232 个 entry = 77 个无 landing + 80 个
  cleanup-only + 75 个 null-type catch-all；IDA 独立复核 `80 type_id=-2 / 75
  type_id=-1 / 0 mismatch`。完整 manifest、action/type-table 边界及“catch-all 不等于一律
  terminate”的限制见
  [FOLLOWUP_FULL_LSDA_CALLSITE_TOPOLOGY_2026-08-03.md](FOLLOWUP_FULL_LSDA_CALLSITE_TOPOLOGY_2026-08-03.md)。
- 上述结论证明 **Android emitted surface → local** 的入口覆盖完整；单凭入口缺失或零 xref
  不证明原始 C++ helper/call 不存在，也不能恢复函数名、accessor 拼写、`noexcept` token
  或宏展开前组织。若独立入口与 caller 中存在逐指令完整克隆、SROA 或 EH 等额外正证据，
  仍可证明某条源码级 inline 调用；`0x59673C → 0x596BC4/0x596C70` 正是该例外。
- caller 方向也已逐项穷举：IDA 的 349 个 code-xref 中主实现簇外原始数为 305，但权威
  ELF 证明其中两条是本地 weak `vector<string>` 定义的 PLT alias，不是 caller。真实
  `.text` surface 为 303 个 direct `BL`，落到 15 个目标、25 个 owner FDE，去重为
  71 组 owner→target。`ObjSource::drawLayer` by-value 参数见
  [FOLLOWUP_EXTERNAL_CONSUMER_XREF_2026-08-02.md](FOLLOWUP_EXTERNAL_CONSUMER_XREF_2026-08-02.md)；
  ELF canonical gate、record/adaptor 特殊成员与 PLT relocation 纠正见
  [FOLLOWUP_EXTERNAL_CONSUMER_LIFECYCLE_SURFACE_2026-08-04.md](FOLLOWUP_EXTERNAL_CONSUMER_LIFECYCLE_SURFACE_2026-08-04.md)；
  非直接函数指针、重定位与 vtable 归属的反向闭环见
  [FOLLOWUP_INVERSE_POINTER_REFERENCE_SURFACE_2026-08-04.md](FOLLOWUP_INVERSE_POINTER_REFERENCE_SURFACE_2026-08-04.md)；
  唯一非空跨模块 OwnerFilter 的 TU-static、copy-assignment、manager/invoker 与
  LoadStorage→Adopt 生命周期闭环见
  [FOLLOWUP_OWNER_FILTER_BRIDGE_LIFECYCLE_SURFACE_2026-08-04.md](FOLLOWUP_OWNER_FILTER_BRIDGE_LIFECYCLE_SURFACE_2026-08-04.md)。

## 判定标签

| 标签 | 含义 |
| --- | --- |
| `EMITTED_PROVEN` | Android 有独立入口、vtable 槽、注册 callback 或 ABI thunk；精确行为由对应逐函数报告约束。 |
| `INLINE_SHAPE_PROVEN` | 没有独立入口，但一个或多个 caller 的指令、EH、引用计数或字段数据流证明该语义步骤存在；精确 helper/token 仍可能不可恢复。 |
| `CROSS_ABI_BOUNDARY_PROVEN` | 权威 Android arm64 body/完整 inline clone 已约束行为，独立谱系的 iOS arm64 又保留独立函数或 direct call，因而共享源码边界/record 分组得到交叉支持；精确 identifier、member/free、pointer/reference 或 header-inline token 仍可受限。 |
| `FACTORIZATION_GUESS` | 本地为了表达重复机器码形状建立了 helper/record；二进制不能证明原源码采用同一抽象边界。必须保留 `_guess`，相关源码结构维度不得写成完全 `MATCH`。 |
| `LOCAL_LIVE_TOKEN_UNPROVEN` | 本地调用面明确存活，语义字段也有 Android 证据，但 accessor/typedef/特殊成员的精确源码 token 没有独立证据。它不是“死 API”，也不是删除依据。 |

## 文件级覆盖

| 本地文件 | emitted 对应面 | 非 emitted 源码面结论 |
| --- | --- | --- |
| `PSBDispatch.h`, `main.cpp` | MANIFEST A 的 42 个 dispatch/packed/vtable 入口，C/F 的 typed NCB 入口以及 H 的静态初始化 | dispatch 类双基类顺序、32+3 槽和对象生命周期有 vtable/relocation 正证据；`PSBFileConvertor<T>` 与宏展开前拼写仍是 token/factorization 上限。旧的 TU-local `throwUnknownType` wrappers 已在所有 classifier consumer 闭合后删除，不再冒充原源码边界。 |
| `PSBRawFile.cpp`, `PSBRawFile.h` | B、D 的 raw owner/node/file 入口及 G 的 vector 慢路径 | intrusive retain/release、raw node 对 PSBFile-compatible holder special members 的复用和字段 accessors 多数被 caller 内联；GetTypeCategory/GetDouble wrapper 与 GetInt 的共享-helper 调用现按 target clone + 同源 retained-call 证据恢复。 |
| `PSBPackedInternal.h` | 被 A/B/D/E 多个入口内联消费 | 除 packed helpers 外，现集中共享 classifier、窄/宽 integer 与 raw-double decoder；行为由权威 Android arm64 完整 clones 约束，packed/numeric 分层由 iOS arm64 独立复核，精确名字/token 仍保留 `_guess` 上限。 |
| `PSBMedia.cpp`, `PSBMedia.h` | MANIFEST E 的 17 个 storage-media/NCB/`ttstr` 入口 | `PSBMedia` 的 inline refcount/name/default destructor 因 vtable 被实际发射，属于 `EMITTED_PROVEN`；内部 packed view 仍受 `PsbArray_guess` 上限约束。 |
| `PSBMediaRegistry.cpp`, `PSBMediaRegistry.h` | `initPsbFile@0x59849C` 与静态 callback 注册链 | 函数本体和 callback 关系已发射；精确宏 token 只能由本地模板交叉参照，不能由 stripped 二进制单独证明。 |

## 独立入口与 caller 内联克隆

`PSBValueDispatch_getString_guess@0x596BC4` 与
`PSBValueDispatch_getResource_guess@0x596C70` 都有独立、无 xref 的 emitted 入口；同时，
`CreateVariant_guess@0x59673C` 的 String/Resource 两段分别是它们的完整 O3 内联克隆。Resource 分支还
保留典型的 inlining/SROA 证据：独立 helper 的 `*size` store 在 caller 中消失，length
直接留在寄存器送入 Octet allocator，而 null 路径完全不读取 size。

因此，“零 xref”不能再被解释为本地 helper 没有源码 caller。当前本地已恢复
`CreateVariant_guess → getString_guess/getResource_guess` 的成员调用；Release `-O3` 仍会内联它们，且
反汇编确认 resource null gate 保留；iOS arm64 另保留相应 direct call。精确原名、
pointer/reference 拼写仍不可恢复，
所以三份报告继续保持 `EVIDENCE_LIMITED`，但调用拓扑已闭合。

同样，`PSBMedia_GetResourceData_guess@0x59A0B4` 的
`0x59A0EC..0x59A214` 是 `PSBRawNode_GetResource_guess@0x5996E4` 在绑定栈上 raw node
后的完整 inline clone；iOS arm64 另保留 raw accessor direct call。本地现已从手写复制体恢复为
`value.GetResource(size)`。源码调用链据此闭合为 `MATCH`；权威 arm64 没有保留该 `BL`，
所以 emitted TASK_TREE 仍忠实使用 `[helper]`。精确 member 名/token继续
`EVIDENCE_LIMITED`，但不能再把 source call 本身写成未证。

## 没有独立入口的本地构造

| 本地构造 | 标签 | Android 可证明范围与当前处理 |
| --- | --- | --- |
| `detail::ReadUnaligned_guess<T>` | `FACTORIZATION_GUESS` | 多个入口证明未对齐读取的值、宽度、次序和 first-fault 边界；不能证明原源码统一调用一个 `memcpy` 模板。保持 `_guess`，不得据此宣称 helper 结构 1:1。 |
| `ReadPackedCount_guess`, `ReadPackedValue_guess` | `FACTORIZATION_GUESS` | target 证明 count/value 的逐 tag 运算，但没有独立入口能证明原源码分别拥有这两个 helper；它们继续作为带 `_guess` 的本地 header factorization。 |
| `PsbArray_guess` | `CROSS_ABI_BOUNDARY_PROVEN` | 权威 arm64 多个 consumer 证明 packed 标量与完整 inline clone；iOS arm64 保留独立四字段 constructor，并写出 `nBytes/count/width/data` 次序。record/constructor 分组得到跨谱系支持；仍不可恢复的是类型名、字段名、member/free/header-inline 与 `operator[]` token，所以标识符继续保留 `_guess`，相关报告的源码结构 verdict仍受限。 |
| `GetTypeCategory_guess` | `FACTORIZATION_GUESS` | 权威 arm64 的 11 个 consumer 保存完整或 category-specialized residual，iOS arm64 另保留 shared classifier call。行为与 consumer 集已闭合，但精确 helper/member/header-inline token 仍不可唯一恢复。 |
| `DecodeInteger32_guess`, `DecodeInteger64_guess`, `DecodeNumberAsDouble_guess` | `CROSS_ABI_BOUNDARY_PROVEN` | 权威 arm64 `CreateVariant/GetDouble/GetInt` 保存完整 numeric clones，iOS arm64 独立复核三层共享调用拓扑；只剩精确名字及 member/free/header-inline token 不可恢复。 |
| `PSBRawOwner::AddRef` | `INLINE_SHAPE_PROVEN` | 非原子递增在 target caller 中反复展开，iOS arm64 独立复核；尚无独立 AddRef target，class 内/外、inline 与精确命名仍不可恢复。 |
| `PSBRawOwner::Release` | `CROSS_ABI_BOUNDARY_PROVEN` | 权威 arm64 在 holder/raw-node consumer 中内联 decrement→zero→owner dtor→delete，iOS arm64 保留共享 Release。共享 Release 边界与当前 `delete this` 数据流得到交叉支持；仍不能区分 class 内/外定义、inline token或精确 identifier。 |
| `tryDecodeMdf_guess` | `CROSS_ABI_BOUNDARY_PROVEN` | 权威 arm64 `Load@0x5982B0..0x59841C` 与 `LoadStorage@0x5985E0..0x5986B8` 保存两份完整算法 clone，iOS arm64 `0x1000ED5B4` 独立复核恰有两个 caller。共享 `(source,uint32 size-in/out) -> decoded/null` 边界得到交叉支持；精确名字、pointer/reference 和 TU/member token 未保存，故保留 `_guess`。 |
| `PSBRawOwner::GetHeader` | `LOCAL_LIVE_TOKEN_UNPROVEN` | 本地在 dispatch/raw/media/motionplayer 多处存活；Android owner 的 header-view 指针和各 consumer 字段读取已证明。精确 accessor、const overload 与命名没有 emitted 边界。 |
| `PSBRawOwner::GetData/GetSize` | `LOCAL_LIVE_TOKEN_UNPROVEN` | **不是死 API**：`motionplayer/ResourceManager.cpp:201` 同时调用两者，`:207` 再调用 `GetSize()`。Android 已证明 owner raw allocation 与 signed-64 size 字段的消费语义；精确 accessor factorization 未发射。任何“未引用/应删除”结论均已被这些调用点证伪。 |
| `PSBRawNode` 默认/单参数 root/holder+node 构造、隐式复制/析构/copy assignment | `INLINE_SHAPE_PROVEN` | Android strict/try getter、Resolve 与 motionplayer caller证明 raw result 是 holder+node，并约束 Release-old→copy owner→AddRef→copy node。同源 iOS arm64 保留两参数链 `GetRoot@0x1000ED8C8 -> raw ctor@0x1000EEF28 -> holder assignment@0x1000ED740`，又独立保留单参数 root ctor `0x1001263B8`。后者恰有七个 motionplayer caller，与 Android `0x694AB0/0x695FA0/0x6A9870/0x6A99A4/0x6AA058/0x6AA360/0x6AAF08` 的七份 inline clone 一一对应，并正证“先取 entries→共享 holder assignment→写 node”的边界。当前因此以 `PSBFile`-compatible 首子对象复用 special members，并恢复两个 constructor overload；成员/继承/共同底层 holder 的精确 token 仍不可唯一恢复。 |
| `PSBRawNode::GetOwner/GetNode/GetType` | `LOCAL_LIVE_TOKEN_UNPROVEN` | holder 首子对象、独立 node 与 raw tag 读取均有广泛正证据；当前 accessor 在多个生产 caller 存活。是否原本是同名 inline member、直接字段访问或另一 holder API 不可唯一恢复。 |
| `PSBRawNode::GetFile_guess` | `FACTORIZATION_GUESS` | 权威 arm64 dispatch ctor 三 caller分别传 standalone 一指针 holder，或 raw node 首地址与独立 node；iOS arm64 独立复核。本地 accessor只把该已证明的 PSBFile-compatible holder 交给 dispatch ctor。原源码可能使用成员、基类转换或共同 holder API，故 accessor 名仍保留 `_guess`；旧 `GetOwnerSlotAddress_guess` 已被更强因子化证据淘汰并删除。 |
| `PSBFile` 默认/复制构造、copy assignment、析构 | `INLINE_SHAPE_PROVEN` | NCB、`ResourceManager_loadResource@0x6A8D8C`、`Transfer@0x598A64` 展开 owner copy/AddRef/Release/clear；iOS 一指针 assignment/raw ctor 提供完整 caller/EH。当前 Rule-of-Three 数据流有证据；iOS raw ctor 的 EH cleanup 包围 assignment call，正证该调用边界 potentially throwing，因此 copy assignment 不显式写 `noexcept`。copy constructor 的精确 exception-spec token 仍未恢复，赋值没有 self guard。 |
| `PSBFile::GetOwner` | `LOCAL_LIVE_TOKEN_UNPROVEN` | 唯一 owner 字段和 holder ABI 可证明；accessor 在 root/media/motionplayer 路径存活。dispatch 现在接收 holder 对象本身，不再需要暴露裸 owner-slot 地址。精确 accessor 名仍不能由 stripped/O3 唯一恢复。 |
| `PSBFile::OwnerFilter = std::function<void(PSBRawOwner &)>` | `LOCAL_LIVE_TOKEN_UNPROVEN` | Adopt/LoadStorage 的可选 filter 判空、调用与异常传播已证明；精确 alias 名、引用拼写及模板 token 没有独立 emitted 身份。当前类型只在有相容调用约束时保留，不把别名拼写写成 ground truth。 |
| `PSBFileConvertor<T>` 与 `NCB_SET_CONVERTOR` | `FACTORIZATION_GUESS` | typed NCB wrapper、adaptor 与 Variant 生命周期的 emitted 面已在 C/F 组闭合；没有独立入口能证明 converter class 的名称、两个 overload 或宏展开前组织。 |
| `NCB_REGISTER_CLASS(PSBFile)` / `NCB_PRE_REGIST_CALLBACK(initPsbFile)` | `INLINE_SHAPE_PROVEN` | 注册字面量、callback/member pointer、typed wrapper、class-info 与静态初始化入口均有正证据；具体宏拼写来自本地模板交叉参照，不是二进制保留的源码 token。 |

## 本轮纠正与审计影响

1. 纠正一次错误的 negative-search 结论：`PSBRawOwner::GetData/GetSize` 在
   `ResourceManager.cpp:201/207` 明确存活，不能标成 dead/missing，也不能删除。
2. 纠正旧的 owner-slot 证据上限：iOS arm64 的 holder assignment/raw constructor/
   `GetRoot`/dispatch caller 穷举已证明 raw node 首子对象与 PSBFile 复用同一一指针
   holder 生命周期。旧 `GetOwnerSlotAddress_guess` 和 raw-node 独立 Rule-of-Three 已删除；
   剩余不确定性仅是共同 holder 的名字及 member/base 源码 token。
3. `CreateVariant_guess/GetTypeCategory/IsInstanceOf/EnumMembers/GetCount/PropGet/PropGetByNum/GetString/GetDictionaryKeys/ContainsDictionaryKey/GetListAt` 已恢复共享
   classifier 调用；
   `CreateVariant_guess/GetDouble/GetInt` 已从手写展开/旧四-helper 结构恢复窄/宽 integer
   与 raw-double 调用。`GetDouble/GetInt` 的数据流、调用链和
   边界仍为 `MATCH`，精确 helper token 继续归入源码结构 `EVIDENCE_LIMITED`；总统计仍是
   `99/15`，不是新增行为 GAP。
4. 当前 114 个 emitted 入口仍无 `HAS_GAP`，但 15 个 `EVIDENCE_LIMITED` 明确阻止把
   “尽可能 100% 复原”误报为已经数学证明完成。
5. 沿真实 consumer `ResourceManager_loadResource@0x6A8D8C` 的 fresh 复核又删除了
   invalid-spec label 路径中 caller 自造的 null→empty 归一化，并恢复 raw-node 临时跨
   narrow `ttstr` 构造与 throw 的生命周期；详见
   [FOLLOWUP_0x6A8D8C.md](FOLLOWUP_0x6A8D8C.md)。
6. `EnumMembers@0x596F50` 的 Dictionary 路径在 callback 后 fresh reload `self->node`；
   当前本地不再错误复用 callback 前缓存。List 与 Dictionary 的 W32/UXTW 地址表达式
   也已在逐函数报告中分开记录。
7. 沿 raw-node/packed helper 的 MANIFEST 外 consumer 继续追踪，已闭合
   `sub_6DA454` signed size、`0x6948E8` pixel raw-node 临时、`0x695DE8` outer key-vector、
   ResourceManager `ttstr` 哈希与 SourceCache Entry/Layer 构造等差异；这些修复不改变
   114 个 emitted 入口的统计分母，详见同目录 follow-up 文档。
8. `FindNameIndex@0x59641C` 只消费 names section 前两张 base/check packed 表；旧注释把
   helper 本体误写成遍历三表，现已就地纠正。第三张 name-index 表由别的消费路径使用。
9. `PSBValueDispatch_ctor_guess@0x597AD4` 的 Hex-Rays pointer return 不是源代码 return；
   三个 caller 都忽略 X0，失败时由各自 new-expression cleanup 删除 allocation。报告现按
   普通构造函数隐式完成记录，不再从 ABI 残值反推源码 token；IDB 的旧 pointer-return /
   raw-owner-slot 类型和三处 caller 注释也已同步改成 `void(this, const PSBFile *, node)` /
   `fileHolder + constructor this` 并保存。
10. `Transfer@0x598A64` 的 caller EH 证明接口 potentially-throwing，且调用失败时不析构
   尚未完成的 hidden-sret；zero-ref cleanup 中 `sub_A0DE90` 又会先读 `data-8`，因此
   null data 会在 owner delete/source-clear 前 fault。两层边界均已从旧泛化中拆开。
11. `CreateAdaptor-null` 已由真实 Android ARM64 runtime 补证：null class-object 下
    `EnsureContainer@0x599E04` 仍提交 Void `_file` 与 container 并返回 true；恢复 class
    object 后同名请求因 Object gate 重新加载。该结果只关闭运行时覆盖缺口，不改变
    converter/helper 精确 token 的 `FACTORIZATION_GUESS` 上限。
12. 对 112 个主实现簇入口的全部 IDB prototype 机械筛选又纠正两条 X8 hidden-sret 伪返回：
    `GetRoot@0x598A3C` 不再显示返回残留 self，`load` 首参数复制 helper `0x59B708` 不再
    显示返回最后一次析构的 X0。当前五个 X8 入口均明确建模为 `void + result@X8`。
13. factory/root/load typed NCB wrapper 的 fresh 复核又为 11 个入口补齐准确 ABI，建立
    `paramsFunctor`、ARM64 member-pointer 和三种 wrapper 的 IDB-only 记录；旧的
    `GetFlags __int64()/省略 self` 与 `0x59B708 void *functor` 状态已经就地纠正。记录见
    [FOLLOWUP_IDB_NCB_WRAPPER_ABI_TYPES_2026-08-02.md](FOLLOWUP_IDB_NCB_WRAPPER_ABI_TYPES_2026-08-02.md)。
14. class-info / AutoRegister / RegistItem 尾链的 18 个入口也已补齐 ABI；class-info、两层
    registrar、autoreg、adaptor 与 item-interface 状态均由目标字段访问固定。六条残留
    X0 伪返回和两个 callback 泛化签名已纠正，记录见
    [FOLLOWUP_IDB_CLASSINFO_REGISTRATION_ABI_TYPES_2026-08-02.md](FOLLOWUP_IDB_CLASSINFO_REGISTRATION_ABI_TYPES_2026-08-02.md)。
15. 114/114 prototype 最终机械复扫又清理五项最后泛化：`.init_array` 静态初始化与
    `PSBRawOwner` 构造器的伪返回、`CreateAdaptor` 的 `void *`、两个 vector helper 的
    泛化容器参数。严格筛选现只剩目标证明确为 size/size_t 的三处 64 位整数，记录见
    [FOLLOWUP_IDB_FINAL_PROTOTYPE_SWEEP_2026-08-02.md](FOLLOWUP_IDB_FINAL_PROTOTYPE_SWEEP_2026-08-02.md)。
16. 最终证据队列已拆成“15 个源码 token 上限”与“六个天然输入运行时边界”；前者不能
    由 runtime PASS 或重复 O3 反编译升级，后者不能通过改造正常 PSB/MDF 制造。只读物料
    盘点和下一动作见
    [FOLLOWUP_REMAINING_EVIDENCE_QUEUE_2026-08-02.md](FOLLOWUP_REMAINING_EVIDENCE_QUEUE_2026-08-02.md)。
17. 自然 tag `0x0B` 已从文件名/字节级搜索推进到完整可达树遍历：112 份唯一 PSB 的
    23,415,372 个 node 全部成功分类，既有 tag `0x09` 锚点命中而 `0x0B` 为 0。该结果
    证明当前资产没有可用于观察 `CreateVariant(full56) / GetInt(low32)` 分歧的自然节点，
    不把阴性物料结果误写成源码边界缺失。记录见
    [FOLLOWUP_NATURAL_TAG0B_INVENTORY_2026-08-02.md](FOLLOWUP_NATURAL_TAG0B_INVENTORY_2026-08-02.md)。
18. 同一可达树扫描已扩展到全部整数 tag 的计数、值域和 Variant/GetInt 最大差值；当前
    天然 `0x04..0x09` 已映射为七个固定 Android oracle case，包括
    `0xFFFFFFFF → GetInt(-1)`。两份 SHA、七组 offset/字节与 signed-low32 主机 pin 均
    通过；Android ARM64 原生执行与全量 trace 亦已通过，不改变 99/15/0 verdict。记录见
    [FOLLOWUP_NATURAL_INTEGER_ORACLE_2026-08-02.md](FOLLOWUP_NATURAL_INTEGER_ORACLE_2026-08-02.md)。
19. Resource 盘点现独立验证 table index 与文件区间：1,240 个 tag `0x19` 全部有效，
    唯一 raw tag `0x1A` 是 56395 对 73 项表的自然 OOB。新增 oracle 固定既有
    `ezsave.pimg/2157.tlg`，同时观察 `GetResource` 借用指针与公开 Octet copy/refcount；
    本地 owner-release 生命周期、Android ARM64 原生执行与 trace 均已通过。记录见
    [FOLLOWUP_NATURAL_RESOURCE_ORACLE_2026-08-02.md](FOLLOWUP_NATURAL_RESOURCE_ORACLE_2026-08-02.md)。
20. Real/String 盘点现覆盖 10,500 个 `0x1D`、23,124 个 `0x1E`、70,527 个 `0x1F`
    及 8,975,228 个 `0x15/0x16` String 节点；所有 String index 都在表内。新增五个
    天然 oracle 同时比较公开 Variant 与 raw `GetDouble/GetString`，String 还验证
    owner-release 后 copy 存活及借用指针地址。host pin、本地回归、Android ARM64 原生
    执行与 trace 均已通过。记录见
    [FOLLOWUP_NATURAL_REAL_STRING_ORACLE_2026-08-03.md](FOLLOWUP_NATURAL_REAL_STRING_ORACLE_2026-08-03.md)。
21. 当前 ARM64-only APK 已把 raw/scalar/shape/resource/media 全部模式合并跑通：无 trace
    24/24、全量 trace 24/24，均无 cleanup error。启动线程隔离、局部 refcount 口径和
    固定目标哈希见
    [FOLLOWUP_ANDROID_ARM64_RUNTIME_ORACLE_2026-08-03.md](FOLLOWUP_ANDROID_ARM64_RUNTIME_ORACLE_2026-08-03.md)。
22. 审计现机械绑定当前 `cpp/plugins/psbfile/` 的完整 10 文件集合与逐文件 SHA-256；
    新增、删除、改名或修改任一文件都会使 `verify_audit.py` 失败。当前快照已通过
    `598 assertions in 11 test cases` 的 Mac Debug 回归，Web Debug `psbfile/krkr2`
    产物亦为最新；该门禁只防止报告相对源码陈旧，不把哈希冒充 Android 对齐证据。详见
    [FOLLOWUP_CURRENT_SOURCE_SNAPSHOT_GATE_2026-08-03.md](FOLLOWUP_CURRENT_SOURCE_SNAPSHOT_GATE_2026-08-03.md)。
23. Git 历史的第二份 Android ARM64 构建现已机械映射：114 个 FDE 固定平移
    `+0x3E0`，39 张 LSDA/232 个 call-site 同形，5,525 条指令操作码/寄存器骨架零差异；
    136 个不同立即数全部由 `ADRP` 地址物化解释。该 stripped 构建没有 PSBFile 插件
    语义动态符号、调试节或源码伴随物，因此只能证明同源链接布局，不能升级 15 个 token
    上限。详见
    [FOLLOWUP_HISTORICAL_ANDROID_ARM64_LINEAGE_2026-08-03.md](FOLLOWUP_HISTORICAL_ANDROID_ARM64_LINEAGE_2026-08-03.md)。
24. 又对 Android 目标内 RTTI/typeinfo 与私有 helper 名做精确搜索，并 fresh 复扫
    `Load/LoadStorage/EnsureContainer/Resolve`。dispatch vtable RTTI 为空，目标没有保留
    guessed 私有类型/helper 名；MDF 混合释放族、失败泄漏、adaptor-null 与 path/raw-node
    生命周期仍和本地一致。`0x599E04/0x59A4B0` 的 40 个局部语义名已写入并保存 IDB，
    详见
    [FOLLOWUP_TARGET_ONLY_RTTI_MDF_MEDIA_RESWEEP_2026-08-03.md](FOLLOWUP_TARGET_ONLY_RTTI_MDF_MEDIA_RESWEEP_2026-08-03.md)。
25. emitted 函数清单之外的静态对象拓扑现也有独立门禁：从目标 ELF 的 file-backed
    `PT_LOAD` 直接读取 `.init_array`、dispatch primary/secondary、media、class
    AutoRegister、instance adaptor 以及 factory/root/load typed wrapper，共 10 个表面、
    177 个 qword。prefix 的 offset-to-top/RTTI、全部槽位和初始化顺序均与当前继承、
    virtual override、ncbind 宏路径一致；未发现生产 GAP。详见
    [FOLLOWUP_STATIC_OBJECT_VTABLE_SURFACE_2026-08-03.md](FOLLOWUP_STATIC_OBJECT_VTABLE_SURFACE_2026-08-03.md)。
26. 114 个入口的 data-xref surface 现也机械固定：`.rodata` 的 76 个目标严格分成
    34 个字面量和 42 张 switch table；后者共有 915 个 signed-relative case 槽、194 个
    唯一 FDE 内 destination。全部字面量、空串 pointer chain、classifier partition、
    packed/numeric 分支和 16 个 `.bss` 全局生命周期均与当前源码一致；未发现生产 GAP。
    详见
    [FOLLOWUP_LITERAL_SWITCH_GLOBAL_SURFACE_2026-08-03.md](FOLLOWUP_LITERAL_SWITCH_GLOBAL_SURFACE_2026-08-03.md)。
27. 完整 callsite surface 现由 ELF word decoder 固定：114 个 FDE 内 567 个 transfer
    site 精确分为 468 `BL`、11 cross-FDE tail `B`、45 `BLR`、1 indirect tail `BR` 与
    42 switch `BR`；其中 44 site/39 edge 连接 MANIFEST 内函数，46 个非 switch indirect
    transfer 均闭合到现有 callback/vtable/member-pointer/manager 路径。调用链未发现生产
    GAP；详见
    [FOLLOWUP_CALLSITE_SURFACE_2026-08-03.md](FOLLOWUP_CALLSITE_SURFACE_2026-08-03.md)。
28. 46 个非 switch indirect transfer 现进一步拥有独立 target-producer 门禁：44 个
    fixed-offset `LDR` 与 2 个 Itanium pointer-to-member register-offset `LDR` 的 owner、
    transfer、producer word、目标寄存器和 18 类语义角色全部固定。`std::function`
    manager/invoker、stream、lister、dispatch/NCB vslot、析构 tail 与 typed member wrapper
    均和当前源码/模板分层一致，未发现生产 GAP；详见
    [FOLLOWUP_INDIRECT_ABI_SURFACE_2026-08-03.md](FOLLOWUP_INDIRECT_ABI_SURFACE_2026-08-03.md)。
29. 435 个 MANIFEST 外 direct site 的 65 个唯一 callee 现也全部语义化并接入门禁：
    9 类通用 EH/runtime、allocation/memory/compression、libstdc++、storage/stream、
    script global、diagnostic/log、ttstr/narrow、ncbind 与 Variant/closure 边界完整覆盖；
    32 个 stripped `sub_*` 经 fresh decompile 后均非遗漏的 PSB 私有 helper，并以 `_guess`
    名/证据注释保存 IDB。源码调用层与清理路径一致，未发现生产 GAP；详见
    [FOLLOWUP_EXTERNAL_CALLEE_SURFACE_2026-08-03.md](FOLLOWUP_EXTERNAL_CALLEE_SURFACE_2026-08-03.md)。
30. 完整 stack-frame/local-lifetime surface 现也由 AArch64 word decoder 固定：114 个
    owner 精确分为 57 framed + 57 frameless；framed 中 52 个入口分配、5 个诊断慢路径
    shrink-wrap，另有 31 个 canary、10 种 GPR save mask 与唯一 `D8` spill。39 个 LSDA
    owner 全部 framed，18 个 framed owner 只有 unwind 元数据。fresh decompile 与本地
    Variant/ttstr/raw-node/vector/ncbind functor scope 对照未发现生产 GAP；详见
    [FOLLOWUP_STACK_FRAME_LIFETIME_SURFACE_2026-08-03.md](FOLLOWUP_STACK_FRAME_LIFETIME_SURFACE_2026-08-03.md)。
31. 44 个 MANIFEST 内 direct transfer 现进一步拥有逐调用点 ABI contract 门禁：42 个
    `BL` 与 2 个 tail `B` 精确分为 21 类参数角色和 8 类返回消费。两处 hidden-sret、
    Variant by-value、两种 `uint32 → size_t` zero-extension 与 ncbind 两只空 tag-reference
    的 producer word 均被固定。caller 正证据补回
    `CopyFirstArgument_guess@0x59B708` 的 `X1/X2` 空 tag const-ref 参数；本地
    packed/raw/media/ncbind 源码分层逐项一致，未发现生产 GAP。详见
    [FOLLOWUP_INTERNAL_CALL_CONTRACT_SURFACE_2026-08-03.md](FOLLOWUP_INTERNAL_CALL_CONTRACT_SURFACE_2026-08-03.md)。
32. typed-member/对象字段表面的首轮独立清单和 ELF word 门禁形成 483-row 旧基线：
    114 个函数当时按 `R=312/W=147/RW=10/address=14` 分类；416 条带地址的行折叠为
    385 个唯一站点/62 个 owner FDE。该轮裸指针交叉复核补回 7 个 IDB 类型传播缺口，
    fresh 反编译直接显示 owner/header/raw-node/dispatch/media/ncbind 字段链；后续完整
    `cot_ptr/cot_idx` 复扫又提升 5 条 typed read，因此本项不再声称是最终总数。
    目标 O3 展开的 owner/node 仍映射为源码 `PSBFile` 首子对象与嵌套 `PSBRawNode`，不把
    ARM64 扁平布局反写成 wasm32 padding；当前字段集合、容器选择和生命周期未发现生产
    GAP。详见
    [FOLLOWUP_TYPED_MEMBER_FIELD_SURFACE_2026-08-03.md](FOLLOWUP_TYPED_MEMBER_FIELD_SURFACE_2026-08-03.md)。
33. 39 个 LSDA owner 的 landing cleanup contract 现也拥有逐指令清单和 ELF 门禁：155 条
    非零 landing 引用折叠为 150 个唯一入口，其中 75 个 cleanup-only 全部到
    `_Unwind_Resume`，75 个 catch-all 分成 72 个直接 terminate 与 3 个
    catch/delete/rethrow。显式 ARM64 CFG 固定了 569 个唯一指令、1,150 个 per-root
    指令实例、168 个 transfer 及全部 successor，并证明 landing 与正常流零交叉。
    Factory 发布后捕获回收、stream/Variant/ttstr/vector/ncbind 清理顺序和 LoadStorage
    的 raw-data 异常泄漏边界均与当前源码一致，未发现生产 GAP。详见
    [FOLLOWUP_LANDING_CLEANUP_CONTRACT_SURFACE_2026-08-03.md](FOLLOWUP_LANDING_CLEANUP_CONTRACT_SURFACE_2026-08-03.md)。
34. 114 个 entry-rooted normal CFG 现拥有独立终点/返回 ABI 门禁：4,956 条正常指令与
    569 条 landing-only 指令零交叉并完整覆盖 5,525 条 FDE 指令。5,337 条 successor
    导出 162 `RET`、11 direct tail、1 indirect tail 与 34 true-noreturn；114 个 prototype
    分为 37 void、71 `W0/X0`、5 hidden-sret `X8`、1 `D0`。28 个 TVP/TJS diagnostic
    调用全部保留 helper-return default，因此 landing 专用 noreturn 集没有被错误套用。
    当前 constructor、scalar/FP return、X8 non-trivial return、void tail 与源码逐项一致，
    未发现生产 GAP。详见
    [FOLLOWUP_NORMAL_CFG_TERMINAL_CONTRACT_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_CFG_TERMINAL_CONTRACT_SURFACE_2026-08-03.md)。
35. normal CFG 的完整 branch-predicate contract 现也由独立门禁固定：66 个 owner 中
    437 个条件 branch 精确形成 874 条 taken/fallthrough edge；180 个 `B.cond` 全部回溯到
    唯一线性 NZCV producer，精确分为 `176 CMP / 3 CMN / 1 SUBS`，未解析 0、最大距离 7。
    条件族为 73 equality、84 unsigned、23 signed；216 个 `CBZ/CBNZ` 的 W/X 宽度/寄存器
    与 41 个 `TBZ/TBNZ` 的 bit/sense/寄存器也逐行进入 digest。fresh 复核确认 packed trie/
    dictionary 为 unsigned，array/TJS error/IndexOf 为 signed，bit 0/10/31 语义与当前源码
    一致，未发现生产 GAP。详见
    [FOLLOWUP_NORMAL_BRANCH_PREDICATE_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_BRANCH_PREDICATE_SURFACE_2026-08-03.md)。
36. 同一 normal CFG 的 216 个 `CBZ/CBNZ` 与 41 个 `TBZ/TBNZ` 现进一步闭合到 tested
    register 的完整 reaching-definition surface：57 个 owner、257 个 branch 共 287 条
    来源，精确分为 246 个显式 writer、14 个入口参数与 27 个 `W0/X0` call return。
    244 个单来源与 13 个多来源 join 的每条 predecessor 路径均在声明 producer 停止；
    volatile `X1..X18` call clobber 为 0。显式来源分类为
    `147 memory / 51 transfer / 12 atomic-status / 36 scalar`，另有 20 个 direct 与 7 个
    indirect call return。packed count/default、DecodeName vector/allocation、Resolve ttstr
    临时、Refresh entry bool、RegistItem lifetime guard、TJS signed-error 与两只 ARM
    member-pointer low-bit 路径均和当前源码一致，未发现生产 GAP。详见
    [FOLLOWUP_NORMAL_CB_TB_PRODUCER_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_CB_TB_PRODUCER_SURFACE_2026-08-03.md)。
37. 42 张 switch table 现进一步闭合到 selector producer 与完整编译器 dispatch chain：
    20 个 owner 的 42 个 selector 全部单来源，精确为 41 条 raw-tag `LDRB` 关系与 1 条
    chained `SUB` 关系；32 个唯一 producer 中没有入口参数、call return 或 volatile
    call-clobber。41 个 `SUB tag-lowcase` normalizer 加唯一 zero-based reuse，再接 42 组
    `CMP/B.HI/ADRP/ADD/LDRSW/ADD/BR`，共固定 335 条链指令。classifier、packed count、
    narrow/wide integer、String/Resource 与 Array/Dictionary selector 的生产和消费顺序均与
    当前源码一致，未发现生产 GAP。详见
    [FOLLOWUP_SWITCH_SELECTOR_DISPATCH_SURFACE_2026-08-03.md](FOLLOWUP_SWITCH_SELECTOR_DISPATCH_SURFACE_2026-08-03.md)。
38. 180 个 `B.cond` 的 `CMP/CMN/SUBS` 现进一步闭合到每个寄存器输入的完整来源：48 个
    owner、260 个 operand 共形成 320 条 reaching-definition 关系，精确分为 313 个显式
    writer、4 个入口参数与 3 个 `W0/X0` call return。235 个单来源与 25 个多来源 join
    全部沿 normal CFG 前驱闭合，最大 9 路且 volatile call-clobber 为 0；输入又精确分成
    `129 W / 131 X` 与 `100 immediate-form / 160 register-form`。trie/dictionary 上下界、
    classifier 的 `-1` diagnostic fallback、packed count、vector/COW-string refcount、
    `IndexOf == -1` 与 typed-wrapper argc/flag 均与当前源码一致，未发现生产 GAP。详见
    [FOLLOWUP_NORMAL_NZCV_INPUT_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_NZCV_INPUT_SURFACE_2026-08-03.md)。
39. 162 个正常 `RET` 中的 129 个 source-visible W0/X0/D0 value return 现进一步闭合到
    完整 producer surface：72 个 owner 共形成 160 条 reaching-definition 关系，精确
    分为 157 个显式 instruction writer、2 个 direct `BL` return 与 1 个 indirect
    `BLR` return。112 个 RET 为单来源，17 个为多来源 join，最大 5 路；ABI 精确为
    `96 W0 / 19 X0 / 14 D0`，入口残留与未声明 call-clobber 均为 0。显式 producer 又
    分成 `9 memory / 116 MOV transfer / 32 arithmetic-or-conversion`。TJS error、refcount、
    allocation/null、diagnostic fallback、numeric conversion、short-circuit 与 typed-wrapper
    callback 返回值均与当前源码一致，未发现生产 GAP。详见
    [FOLLOWUP_NORMAL_RETURN_VALUE_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_RETURN_VALUE_SURFACE_2026-08-03.md)。
40. 311 个 continuing normal `BL/BLR` 现拥有与返回 producer 互补的正向 first-event
    contract：57 个 owner 共形成 419 条关系，精确分为 86 个 physical `W0/X0` use、
    250 个无读取 overwrite、49 个中性 call-boundary 与 34 个 `RET` reach。147 个
    direct-GPR 中 71 个、39 个 indirect 中 15 个被显式使用；125 个 direct-void 的 use
    为 0。34 个 `RET` reach 全部分入 28 void、5 hidden-sret、1 FP owner，GPR owner 为 0，
    所以没有把 call residue 误判为源码返回；四条 atomic/vector pre-event loop 的有限
    exit 也全部闭合。当前调用结果、临时对象、refcount 与 wrapper 数据流未发现生产 GAP。
    详见
    [FOLLOWUP_NORMAL_CALL_RESULT_FIRST_EVENT_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_CALL_RESULT_FIRST_EVENT_SURFACE_2026-08-03.md)。
41. 306 个 normal direct `BL` 与 11 个 out-of-owner direct tail `B` 现拥有完整的参数
    producer contract：59 个 caller owner、80 个 target 共暴露 446 个纯寄存器 arg，沿
    predecessor CFG 闭合成 475 条关系。精确分为 447 个 instruction writer、18 个入口
    参数与 10 个 preceding-`BL` return；435 个单来源、11 个多来源 join，最大 13 路，
    volatile call-clobber 与 entry residue 均为 0。ABI bank 为
    `280 X0 / 97 X1 / 48 X2 / 7 X3 / 3 X4 / 3 X5 / 7 X8 / 1 D0`；显式来源又分为
    `80 memory / 257 transfer-address / 107 arithmetic-conversion / 3 pre-index writeback`。
    这同时固定 CreateVariant raw-tag joins、DecodeName packed index、Load allocation-return
    转发、media hidden-sret、NCB tag temporary 与注册体 base writeback，并纠正了把普通
    `STLXR` memory base 误判为寄存器 writer 的机械错误。当前源码未发现生产 GAP。详见
    [FOLLOWUP_NORMAL_DIRECT_ARGUMENT_PRODUCER_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_DIRECT_ARGUMENT_PRODUCER_SURFACE_2026-08-03.md)。
42. 46 个非 switch indirect transfer 现也拥有完整参数 producer contract：18 个 owner 的
    `45 BLR + 1 BR` 共暴露 117 个 `X0..X7` arg，沿 40 个 normal-entry site 与 6 个
    landing-only site 的显式 CFG 闭合为 120 条关系。114 个参数单来源、3 个两路 join；
    来源精确为 119 个 instruction writer 与唯一 media deleting-destructor entry `X0`，
    未声明 volatile call-clobber、call return、entry residue 均为 0。显式来源分为
    `17 memory / 77 transfer / 23 address / 2 select`。call-operand 类型同时纠正
    `0x59B1A8` 的 stale `X4` 假第五参，并固定 `0x597200/0x5973F0` 的八参
    `iTJSDispatch2::FuncCall`。当前 callback/vtable/manager/member-pointer 与 LSDA cleanup
    参数生命周期未发现生产 GAP。详见
    [FOLLOWUP_INDIRECT_ARGUMENT_PRODUCER_SURFACE_2026-08-03.md](FOLLOWUP_INDIRECT_ARGUMENT_PRODUCER_SURFACE_2026-08-03.md)。
43. typed-member 的 instruction-backed 写值侧现也拥有完整 producer contract：32 个
    owner 的 `W=105/RW=3` 共形成 108 个字段事件、101 个实际 store 站点与 109 条来源。
    107 个事件单来源，唯一两路 join 为 `std::vector<std::string>::end` 的两条分配路径；
    来源精确为 84 个 instruction writer、3 个构造 entry parameter 与 22 个 ZR operand，
    call return/clobber、entry residue 和 landing-only event 均为 0。四组完整 11-field
    header population、dispatch/raw/media output commit、vector growth 与 ncbind state 写链
    均和当前源码一致，未发现生产 GAP。详见
    [FOLLOWUP_TYPED_MEMBER_WRITE_VALUE_PRODUCER_SURFACE_2026-08-03.md](FOLLOWUP_TYPED_MEMBER_WRITE_VALUE_PRODUCER_SURFACE_2026-08-03.md)。
44. typed-member 的 instruction-backed 读值/取址侧现也拥有完整 consumer/source
    contract：61 个 owner 的 `R=297/RW=3/address=11` 共形成 311 条语义事件、290 个
    实际站点。267 条 direct producer 语义行展开为 268 个 lane 与 288 条 first-event
    relation，精确分为 `196 use / 46 transform / 37 call-use / 6 call-boundary / 3 RET`；
    44 条 residual anchor 语义行展开为 45 个 lane 与 45 条单来源关系，来源为
    `42 instruction + 3 ZR`。所有语义事件均 normal-entry reachable；37 个 call-use
    全部由独立 direct/indirect argument manifest 证明，landing-only、pre-event loop 与
    未声明 volatile call-clobber 均为 0。当前 packed/header、OwnerFilter、media resource
    与 ncbind member-pointer 数据流未发现生产 GAP。详见
    [FOLLOWUP_TYPED_MEMBER_READ_CONSUMER_SURFACE_2026-08-03.md](FOLLOWUP_TYPED_MEMBER_READ_CONSUMER_SURFACE_2026-08-03.md)。
45. typed-member 表面最后 67 条没有独立 ctree EA 的 optimizer-synthetic 语义行现也拥有
    完整 machine-realization contract：33 个 owner 的
    `W=42/R=15/RW=7/address=3` 展开为 73 个唯一锚点，62 个是
    assignment/RMW/address/cast coalesced store，另有 `6 LDR / 3 BLR / 2 BR`。
    62 条语义单 occurrence、4 条双 occurrence、1 条三 occurrence；全部 anchor
    normal-entry reachable。三条 function-manager `BLR` 与两条 packed-tag switch `BR`
    分别由独立 indirect ABI / switch manifest 交叉证明；cast 下的 syntactic `R` 与实际
    store realization 保持分层，没有伪造 member-load EA。`416 EA-backed + 67 synthetic`
    重新闭合 483-row 旧基线，当前 dispatch/OwnerFilter/raw-node/adaptor/
    paramsFunctor/vector 数据流未发现生产 GAP。详见
    [FOLLOWUP_TYPED_MEMBER_SYNTHETIC_REALIZATION_SURFACE_2026-08-03.md](FOLLOWUP_TYPED_MEMBER_SYNTHETIC_REALIZATION_SURFACE_2026-08-03.md)。
46. 当前完整 `cot_ptr/cot_idx` 裸内存表面现也拥有逐行语义与机器 realization 门禁：
    5 条经 field load + 首消费者双重证明的 read-only promotion 把 typed-member 总数纠正为
    `488 = 421 EA-backed + 67 synthetic`，模式为
    `R=317/W=147/RW=10/address=14`，唯一 EA 站点为 390。剩余 667 行 raw expression
    精确分为 `461 cot_ptr + 206 cot_idx` 与 `R=407/W=111/RW=5/address=144`；496 条
    EA-backed、171 条 synthetic 映射到 610 个唯一 exact-word 锚点，610/610 均从 owner
    正常入口可达。`Transfer` owner/refcount/data、`Resolve` native holder/header/cleanup、
    packed byte/index、TJS/stream/vtable、COW/STL 与 runtime 边界逐项对照无生产 GAP。详见
    [FOLLOWUP_RAW_MEMORY_DEREFERENCE_SURFACE_2026-08-03.md](FOLLOWUP_RAW_MEMORY_DEREFERENCE_SURFACE_2026-08-03.md)。
47. 直接堆生命周期现由 allocation argument/result、发布锚点、LSDA cleanup 与完整 release
    census 联合固定：15 个 owner 的 20 个 allocation site 精确分为
    `15 operator new + 4 TJSAlignedAlloc + 1 TJSAllocVariantOctet`，并映射到 12 个分配族、
    16 种所有权策略和 68 个 exact-word 锚点（59 normal、9 landing）。8 条
    constructor/copy exception edge 闭合 7 个 allocation 的回收；五个 direct
    release/refcount-helper target 的 120 个站点严格分为
    `83 normal / 37 landing`、`80 raw-object-storage / 40 shared-reference`，另固定
    3 条正常与 8 条异常的 cleanup-absence/leak 边。当前 Factory、Load/LoadStorage/Adopt、
    PSBMedia cache/singleton/stream、ncbind wrapper/class/adaptor 与两类 old-libstdc++ vector
    生命周期逐项一致，无生产 GAP、无 `cpp/` 修改。详见
    [FOLLOWUP_HEAP_ALLOCATION_LIFECYCLE_SURFACE_2026-08-03.md](FOLLOWUP_HEAP_ALLOCATION_LIFECYCLE_SURFACE_2026-08-03.md)。
48. 引用计数现从 release-call census 推进为完整状态机门禁：6 个 ref 初值、10 个
    `load → +1 → store` retain、19 个 owner `load → -1 → store → zero terminal` 与
    3 个 optimizer-folded identity/zero-probe 均固定 owner、exact word、normal/landing
    与终结调用。全 114 FDE 的独占原子指令恰为 16 对，完整分成 9 个带 pthread gate/
    non-atomic fallback/`old<=0` delete 的 COW decrement 和 7 个 shared-string retain。
    15 个 indirect 引用转换为 `5 AddRef / 9 Release / 1 deleting terminal`；7 个 direct
    helper target 共 93 site，并固定 10 个 helper body。24 个 aligned-dealloc 的
    `19+3+1+1` 分区闭合；当前 owner/dispatch/media、Variant String/Octet/Object 与 ncbind
    生命周期顺序无生产 GAP、无 `cpp/` 修改。详见
    [FOLLOWUP_REFERENCE_COUNT_STATE_MACHINE_SURFACE_2026-08-03.md](FOLLOWUP_REFERENCE_COUNT_STATE_MACHINE_SURFACE_2026-08-03.md)。
49. 114 个 MANIFEST FDE 的完整 `.bss` DataRef census 现固定 94 行、93 个唯一站点、
    16 个目标与 22 个 owner；角色严格为 `29 address + 31 read + 34 write`，normal/landing
    分区为 `84+10`。9 个语义对象归入 6 个生命周期家族：lazy native ID、class/callback
    AutoRegister、PSBMedia singleton pointer/guard、class-info/guard、三路注册链头与
    old-libstdc++ COW empty representation。lazy publication、read-old/construct/commit、
    singleton guard abort、六组 class-info transition 和 13 个 COW read 均由 exact word、
    direct-call target、CFG 与 canonical SHA 固定；当前源码逐项一致，无生产 GAP、无
    `cpp/` 修改。详见
    [FOLLOWUP_GLOBAL_BSS_STATE_MACHINE_SURFACE_2026-08-03.md](FOLLOWUP_GLOBAL_BSS_STATE_MACHINE_SURFACE_2026-08-03.md)。
50. 114 个 FDE 的剩余 initialized-data / relocation census 现闭合为 39 条
    `.data.rel.ro` xref、24 组 GOT `ADRP→LDR` 与 5 条动态 relocation；前者覆盖 18 个目标、
    13 个 owner，后者覆盖 4 个 slot、7 个 owner并严格分成 `16 normal + 8 landing`。
    三张补充 base/interface vtable 的 50 个 qword、12 个 Itanium address point 与 24 次
    vptr publication 进一步固定 dispatch 双继承、typed NCB wrapper embedded interface、
    media/native-class/adaptor 构造析构，以及 AutoRegister/COW/pthread/canonical-empty
    relocation 链。全部 exact word、register/slot、symbol/addend、CFG 与 canonical SHA
    通过门禁；当前源码逐项一致，无生产 GAP、无 `cpp/` 修改。详见
    [FOLLOWUP_INITIALIZED_DATA_RELOCATION_SURFACE_2026-08-03.md](FOLLOWUP_INITIALIZED_DATA_RELOCATION_SURFACE_2026-08-03.md)。
51. code-address-like DataRef 现完整拆分为 `10 real + 18 numeric collision`：27 条 `.text`
    引用中，10 行 `ADRP/ADD/ADRL` 组成 7 个 callback/member-function target；其余为
    15 个 packed `0xFFFFFF` mask 与 2 个 MDF magic。唯一 `.plt` 引用是 PSB magic，不是
    cocos2d callable。真实 target 覆盖 pre-register、Factory、`root`/`load` member
    pointer、CreateEmptyAdaptor、finalize 与 dummy constructor；全部 28 个 site 均为
    normal flow。IDB 已删除 18 条伪 dref；ELF 门禁自行解码 logical/MOV-wide immediate、
    exact target/register chain 与两份 canonical SHA。当前宏/模板调用链逐项一致，无生产
    GAP、无 `cpp/` 修改。详见
    [FOLLOWUP_ADDRESS_TAKEN_CALLABLE_SURFACE_2026-08-03.md](FOLLOWUP_ADDRESS_TAKEN_CALLABLE_SURFACE_2026-08-03.md)。
52. DataRef census 的最后 20 行 no-segment target 现也拥有独立机器门禁：fresh IDA
    `get_tid_name` 将其精确解析为 7 个 typed stack-local stroff identity，而不是 ELF
    地址、损坏指针或待删假引用。20 行覆盖 7 个 owner、19 个 site 与 24 个字段事件，
    精确分为 `7 read + 13 write`、`18 normal + 2 landing`；两条 landing 都属于
    `PSBFile::Load` 空 OwnerFilter 临时的异常析构。verifier 逐行解码 SP base、scaled
    displacement、lane、读写方向与寄存器语义，并固定 840-byte canonical SHA。raw-node
    双清零、Resolve current.node 替换/延迟 out commit、NCB delegate 默认状态与 PropSet
    params functor 均和当前源码一致，无生产 GAP、无 `cpp/` 修改。详见
    [FOLLOWUP_STACK_LOCAL_DREF_SURFACE_2026-08-03.md](FOLLOWUP_STACK_LOCAL_DREF_SURFACE_2026-08-03.md)。
53. 完整数值 ctree 现拥有独立机器 realization 门禁：114 个 FDE 的 1,181 条 cot_num
    覆盖 95 个 owner，精确分成 1,133 条 EA-backed 与 48 条 optimizer-synthetic，
    全部绑定到 1,055 个 normal-entry exact-word anchor；语义文本为 132,066 bytes。
    独立机器 immediate census 为 1,208 行/1,160 site，与 ctree 仅交叉 485 site，故
    frame/local、relocation、MRS selector 等 ARM64 immediate 不会被误当成 portable
    源码 token。classifier、packed、numeric、TJS error/flag 与 callback arity 均和
    当前源码一致，无生产 GAP、无 cpp/ 修改。详见
    [FOLLOWUP_NUMERIC_CTREE_SURFACE_2026-08-03.md](FOLLOWUP_NUMERIC_CTREE_SURFACE_2026-08-03.md)。
54. 完整 Hex-Rays 局部变量表面现拥有独立声明/使用/机器 realization 门禁：114 个 FDE
    共恢复 1,056 个 lvar 声明，112 个 owner 有声明；3,073 条 `cot_var` 使用覆盖 77 个
    owner 与 770 个声明，286 个声明没有最终 use。声明 flags 精确为
    `311 argument / 111 stack / 945 register / 72 result / 58 byref`；使用精确为
    `R=2166 / W=804 / RW=20 / address=83`。1,686 条 EA-backed 与 1,387 条 synthetic
    使用全部绑定到 2,214 个 normal-entry exact-word anchor；1,055 个普通声明又折叠为
    781 个 normal definition anchor，唯一特殊项是 `GetInt@0x599438` 的 result
    pseudo-location。fresh 反编译确认 Variant/字符串/raw node/OwnerFilter/
    paramsFunctor/hidden-result 与 NCB wrapper 局部对象的声明、作用域和析构层次均和当前
    源码一致。后续 ECT1 交叉纠正旧 LVS1 中
    `0x59A968:17 a0→a4` 与 `0x59AD84:30 a0→a5` 两处重复实参 relation，
    其余 3,071 个使用行不变；无生产 GAP、无 `cpp/` 修改。详见
    [FOLLOWUP_LVAR_CTREE_SURFACE_2026-08-03.md](FOLLOWUP_LVAR_CTREE_SURFACE_2026-08-03.md)。
55. 完整 Hex-Rays statement/control-tree 现拥有独立结构/机器 realization 门禁：114 个
    FDE 共枚举 2,922 个 `cinsn/cit_*` 节点、12 种 op、119 种 relation 与 122 种
    detail。2,882 个 concrete 与 40 个 optimizer-synthetic 节点全部绑定到 1,942 个
    normal-entry exact-word anchor；`block/if/loop/switch` 父子次序、46 个 goto 到 36 个
    同 owner label、synthetic parent anchor 与最大深度 15 均逐节点验证。fresh 反编译
    确认唯一 `for`/`continue`、DecodeName 的内联 STL loop、GetListAt 的最深 switch/
    cleanup 树和 Resolve 的 RAII loop 均能由当前源码解释，无生产 GAP、无 `cpp/` 修改。
    详见
    [FOLLOWUP_STATEMENT_CTREE_SURFACE_2026-08-03.md](FOLLOWUP_STATEMENT_CTREE_SURFACE_2026-08-03.md)。
56. 完整 Hex-Rays expression tree 现固定 9,629 个 `cexpr/cot_*` 节点、108 个
    owner、42 种 op、17 种 relation、271 种类型与 360 种 operator detail；6 个零
    expression owner 均为已知 nullsub。7,077 个 concrete 与 2,552 个 synthetic 节点
    全部绑定到 3,076 个 normal-entry exact-word anchor，最大深度 11。门禁逐行交叉
    1,935 个 statement root、1,181 个 `cot_num` 与修正后的 3,073 个 `cot_var`，
    全部零差异；fresh 代表对照无生产 GAP、无 `cpp/` 修改。详见
    [FOLLOWUP_EXPRESSION_CTREE_SURFACE_2026-08-03.md](FOLLOWUP_EXPRESSION_CTREE_SURFACE_2026-08-03.md)。
57. 完整 `cot_call` 调用表达式面现由 ECT1 与机器 callsite 共同固定：405 个节点
    精确分成 `285 direct + 40 computed indirect + 80 helper intrinsic`，覆盖
    638 个有序实参与 405 个唯一 normal-entry anchor。325 个 source-tree transfer
    全部命中机器站点；机器侧多出的 `194 direct + 6 indirect` 保持为 EH、canary、
    implicit lifetime 与 compiler lowering。40 个 computed call 恰等于独立
    indirect ABI surface 的 40 个 normal 站点，余下 6 个严格 landing-only；fresh
    direct/indirect/helper/landing 对照无生产 GAP、无 `cpp/` 修改。详见
    [FOLLOWUP_CALL_EXPRESSION_SURFACE_2026-08-03.md](FOLLOWUP_CALL_EXPRESSION_SURFACE_2026-08-03.md)。
58. 完整 `cot_obj` 对象/地址叶节点面现固定 457 个节点、70 个 owner、154 个唯一
    目标、101 种类型与 447 个 normal-entry exact-word anchor。按 ELF section 与既有
    独立表面形成八类互斥分区：285 direct callee、7 address-taken callable、18
    code-range numeric artifact、47 literal、24 initialized address point、1 literal
    pointer、70 BSS object/subobject、5 extern import。9 个 literal-pool 下标全部解析到
    既有 literal，MDF/PSB/mask 碰撞不再可能被提升为 callable；fresh 对照无生产 GAP、
    无 `cpp/` 修改。`base+0x18` COW 字符子对象与 `data-24` sentinel 还纠正旧审计范围：
    empty storage 为 32 bytes，不是 8 bytes。详见
    [FOLLOWUP_OBJECT_EXPRESSION_SURFACE_2026-08-03.md](FOLLOWUP_OBJECT_EXPRESSION_SURFACE_2026-08-03.md)。
59. ECT1 剩余叶节点现完整闭合为 `4 cot_fnum + 3 cot_empty + 111 cot_helper`：118 行
    覆盖 39 个 owner、87 个 normal-entry exact-word anchor，全部为 synthetic。
    80 个 helper callee 与 `cot_call` helper 集合逐项相等；31 个 `TPIDR_EL0` 参数
    全部与对应 `_ReadStatusReg` 共用 MRS anchor。四个 binary64 常量固定值 bit、nearest
    ECT anchor 与物理 producer；三只 empty 固定 `FindNameIndex` infinite-loop condition、
    `GetDictionaryKeys` hidden-sret 空返回与 `GetListAt` cleanup 后空返回。完整表达式树
    由此严格分为 `4829 leaf + 4800 internal = 9629`，无未分类 op。fresh 对照无生产
    GAP、无 `cpp/` 修改。详见
    [FOLLOWUP_RESIDUAL_LEAF_EXPRESSION_SURFACE_2026-08-03.md](FOLLOWUP_RESIDUAL_LEAF_EXPRESSION_SURFACE_2026-08-03.md)。
60. 完整 `cot_memptr/cot_memref` member-expression 链现固定 625 个节点、63 个 owner、
    35 种结果类型、39 种 base 类型与 99 种字段形状；540 个 concrete 与 85 个
    synthetic 节点全部绑定到 478 个 normal-entry exact-word anchor。539 个 outer
    表达式与既有 typed-member 面的 419 个 concrete / 60 个 synthetic 唯一 signature
    双向相等；新增门禁独立闭合此前 outer projection 未包含的 86 个 nested base，
    并固定六种链形、最大三层 member 深度与 `537 var + 2 helper call` terminal base。
    fresh owner→header→names/entries、wrapper/container 与 COW/vector 代表链对照无生产
    GAP、无 `cpp/` 修改。详见
    [FOLLOWUP_MEMBER_EXPRESSION_CHAIN_SURFACE_2026-08-03.md](FOLLOWUP_MEMBER_EXPRESSION_CHAIN_SURFACE_2026-08-03.md)。
61. 完整 `cot_asg` 面现固定 1,123 个 assignment、69 个 owner、1,052 个
    normal-entry exact-word anchor 与 64 种 lhs/rhs op pair。直接 lhs 形成
    `804 lvar + 158 member + 109 raw memory + 32 global object + 20 helper
    pseudo-lvalue` 五类互斥分区，并分别与 LVS1、typed-member、RMC2、global-BSS、
    residual helper 面交叉。门禁保留 2 个 nested raw write、6 个 ECT-only member
    realization、2 个 STP 第二 lane source subobject 与 4 个 landing-only global
    cleanup write 的层次差异，不把它们制造成额外普通 assignment。fresh 对照无生产
    GAP、无 `cpp/` 修改。详见
    [FOLLOWUP_ASSIGNMENT_EXPRESSION_SURFACE_2026-08-03.md](FOLLOWUP_ASSIGNMENT_EXPRESSION_SURFACE_2026-08-03.md)。
62. 剩余全部 internal expression 现固定 1,980 个节点、74 个 owner、30 种 op、
    94 种 child shape 与 1,352 个 normal-entry exact-word anchor；六族精确分为
    744 arithmetic、689 cast、257 predicate、252 ref、36 mutation、2 comma。
    `ref` 的 83 local、144 raw、14 member、45 object 地址来源与 mutation 的 20 local、
    5 raw、11 member RMW occurrence 均与既有独立表面分层交叉；唯一 ECT-only member
    mutation 保持为已知 signature 的额外 realization。四个 dedicated projection
    `405 call + 625 member + 667 raw + 1123 assignment = 2820` 与本面 1,980 行严格组成
    全部 4,800 个 internal 节点。fresh 对照无生产 GAP、无 `cpp/` 修改。详见
    [FOLLOWUP_REMAINING_INTERNAL_EXPRESSION_SURFACE_2026-08-03.md](FOLLOWUP_REMAINING_INTERNAL_EXPRESSION_SURFACE_2026-08-03.md)。
63. 完整 source/machine bridge 现把 114 个 FDE 的 4,956 条 normal-entry 指令严格
    分成 3,076 个唯一 ECT1 expression anchor 与 1,880 条 residual。residual 五族为
    `781 computation + 554 memory + 370 branch + 143 return + 32 call`；memory 又
    分成 `418 stack + 42 switch-table + 94 semantic lowering`。94 条语义补集覆盖
    global/GOT/refcount、33 条 vtable base、17 条 Variant tag/narrow payload、closure/
    output 与三个 fresh reload；所有站点均固定 exact word/mnemonic/role，并与 42 张
    switch table 一一对应。当前源码的普通 C++ 容器、NCB 模板与对象生命周期已表达
    相同来源结构，无生产 GAP、无 `cpp/` 修改。详见
    [FOLLOWUP_SOURCE_MACHINE_BRIDGE_SURFACE_2026-08-04.md](FOLLOWUP_SOURCE_MACHINE_BRIDGE_SURFACE_2026-08-04.md)。
64. MANIFEST 的反向 consumer surface 现从 ELF `.text` 独立重建：303 个 direct `BL`
    覆盖 15 个 target、25 个 owner FDE 与 71 组 owner→target，8,787-byte canonical
    serialization 固定 exact word。IDA 旧普查额外两条已由 `.rela.plt/.dynsym` 证明是
    `vector<string>` 本地 weak definition 的 PLT alias，而非 source caller。fresh
    `LoadedResourceRecord`、atlas record 与 ObjSource native/adaptor 对照进一步固定
    12 个 lifetime FDE、9 个 raw-owner terminal release、2 条 wrapper/helper edge 和
    七 qword adaptor vtable；本地声明顺序、异常 rollback、逆序析构及
    `native && !sticky` gate 一致。无生产 GAP、无 `cpp/` 修改。详见
    [FOLLOWUP_EXTERNAL_CONSUMER_LIFECYCLE_SURFACE_2026-08-04.md](FOLLOWUP_EXTERNAL_CONSUMER_LIFECYCLE_SURFACE_2026-08-04.md)。
65. direct caller 的非直接对偶现从完整 `.text` 和全部 dynamic relocation 独立重建：
    8 个 MANIFEST-page ADRP 候选严格分成 7 个内部注册 callable 与 1 个解析到
    `PackinOne@0x59B9C8` 的同页碰撞；`ADR/LDR-literal/MOV-wide/logical-immediate`
    均为 0。62 个 RELATIVE relocation 精确等于 `2 init-array + 60 vtable`，另两只
    weak STL definition 只通过 JUMP_SLOT/PLT alias 暴露；三类覆盖 71 个唯一目标，
    外部业务 FDE 取址 owner 为 0。psbfile 专属 vtable header/address point 也没有
    外部引用，共享 ncbind 基表引用未被误归属。无生产 GAP、无 `cpp/` 修改。详见
    [FOLLOWUP_INVERSE_POINTER_REFERENCE_SURFACE_2026-08-04.md](FOLLOWUP_INVERSE_POINTER_REFERENCE_SURFACE_2026-08-04.md)。
66. 唯一携带非空 `PSBFile::OwnerFilter` 的跨模块桥现独立闭合：13 个 FDE 严格分成
    `2 MANIFEST + 11 external`，其中 `.eh_frame` 纠正了 IDA 对 `0x6A87D0` 薄包装器与
    `0x6A87E8` copy-assignment body 的错误合并。32-byte TU-static 对象只有三处地址
    物化；两个 setter 只有四个 manager/invoker callable，copy/temporary/old-global
    生命周期严格形成 `1×op2 + 3×op3`，最终只有 `Adopt@0x598858` 执行 invoker。
    seed 8-byte capture、TJS closure 控制块、ResourceManager→LoadStorage→Adopt const-ref
    转发均与当前源码一致。门禁固定 32 个语义 word、9 段 1,948-byte 局部控制流与
    6,057-byte canonical surface；无生产 GAP、无 `cpp/` 修改。详见
    [FOLLOWUP_OWNER_FILTER_BRIDGE_LIFECYCLE_SURFACE_2026-08-04.md](FOLLOWUP_OWNER_FILTER_BRIDGE_LIFECYCLE_SURFACE_2026-08-04.md)。
67. 沿该 bridge 继续向 setter 的注册 owner 回溯，发现并关闭一项 motionplayer 侧生产
    GAP：Android 只有一只 `emoteplayer.dll` init callback；它在同一函数内加载
    `motionplayer.dll`、创建/挂接完整 `Motion.EmotePlayer`，再把两个 setter 注入
    `Motion.ResourceManager`。本地现已从 Motion 主表移除提前注册，删除 split pre/post
    callback、本地 guard、第二只 method Variant 与额外 global Release，恢复同一 Variant
    复用及 `0x10000/0x10200` flag 顺序。独立 ELF 门禁现固定 8 FDE、7 个 UTF-16 literal、
    `1 entry + 2 setter` 唯一物化、11 条 entry/class edge、Motion registrar 的 11 条
    subclass edge、52 个语义 word 与五只完整 FDE；registrar 内 EmotePlayer 引用为 0。
    后续 fresh decompile 又关闭 Player/post-alias 项：Motion 现在严格按 23 constants、11
    subclasses、2 functions 注册，Player 是第六条；顶层 class、post alias、deferred
    function attach 与 `useD3D` 字典覆盖均已删除。扩展门禁固定 23 条 constant edge、两个
    callback 物化和两个 member-add。`motionplayer_static_init@0x42EE18` 又排除了自定义
    pre/post/unregister callback，本地两个 `ShortCutInitial*KeyMap` 表达式及三只 callback
    node 已删除；static-init 完整 FDE 与两个 forbidden UTF-16 零命中进入同一门禁，
    canonical surface 为 10,062 bytes。21/21 test cases、1555/1555 assertions 与 Web Debug
    link 通过。该修改不触碰 `cpp/plugins/psbfile/` 的
    10 文件快照，因此 `SOURCE_SNAPSHOT.sha256` 保持有效。详见
    [FOLLOWUP_EMOTE_REGISTRATION_INJECTION_SURFACE_2026-08-04.md](FOLLOWUP_EMOTE_REGISTRATION_INJECTION_SURFACE_2026-08-04.md) 与
    [FOLLOWUP_MOTION_NAMESPACE_REGISTRAR_SURFACE_2026-08-04.md](FOLLOWUP_MOTION_NAMESPACE_REGISTRAR_SURFACE_2026-08-04.md)。

## 后续证据队列

本节只记录“精确 token 能否被证明唯一”的审计上限，不是后续实现的等待条件。
Android ARM64 `libkrkr2.so` 与 IDA 证据足以继续源代码复原；多候选处使用当前最强
target-compatible 表达并保留 `_guess`。不得为了升级标签而转去寻找同版本源码、外部
私库或 LFS 对象。

- numeric/packed/MDF helper 的跨函数边界已由权威 arm64 clones 约束，
  并由同源 iOS arm64 复核，当前 target-compatible
  分层已恢复；若要进一步消除 `_guess/EVIDENCE_LIMITED`，仍需能约束 Android 1.4.4
  精确名字/member-free/header token 的目标内符号、调试信息或同版本源码。同一 stripped/O3
  函数的等价反编译重述不能完成该升级。packed helper 亦遵循同一证据门槛。
- `PSBFile` copy ctor/assignment 的显式 `noexcept`、owner/node accessor 名称、
  `OwnerFilter` alias 与 converter class 均属于精确 token 证据上限；除非 caller EH、符号、
  RTTI、另一同源构建或其他二进制正证据能区分候选，否则只记录限制，不凭偏好改代码。
- `Kirikiroid2_1.3.9.apk` 的 `libgame.so` 是不同版本，只能作为寻找同源结构线索；在证明
  版本/函数同一性前不能覆盖 1.4.4 `libkrkr2.so` 的权威结论。
- 该版本的 15 项 Android 映射、iOS function-start/helper/assert 证据、明确的证据边界与
  `GetResourceData -> GetResource` 恢复见
  [ANDROID_139_IOS_LINEAGE_2026-07-26.md](ANDROID_139_IOS_LINEAGE_2026-07-26.md)。
- 最终 15 个 token 上限与六个天然输入边界的分流、物料盘点和触发条件见
  [FOLLOWUP_REMAINING_EVIDENCE_QUEUE_2026-08-02.md](FOLLOWUP_REMAINING_EVIDENCE_QUEUE_2026-08-02.md)。
