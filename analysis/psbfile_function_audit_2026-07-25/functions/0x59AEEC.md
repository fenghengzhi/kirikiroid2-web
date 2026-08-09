# `0x59AEEC` — `PSBFile_RegistItem_guess`

> `LSDA-CALLSITE-TOPOLOGY`（2026-08-03）：FDE 的 LSDA `0x186E58C` 恰有 16 项：
> 3 项无 landing、5 项 cleanup-only、8 项 action-1 null-type catch-all；fresh IDA 区分
> 名称/Variant/member 注册失败后的分层 cleanup 与各 Variant 析构失败 terminate。

## 审计身份

- canonical 地址：`0x59AEEC`
- canonical 名称：`PSBFile_RegistItem_guess`
- IDA 当前标签：`PSBFile_ncbRegistNativeClass_RegistItem_guess`。Android 二进制没有
  保留该模板专门化的精确源码符号，故 canonical 名称继续保留 `_guess`。
- 分组：F（typed NCB wrapper、adaptor 与反注册）
- TASK_TREE parent：`0x597F38 PSBFile_ncb_registerMembers_guess`
- parent 关系：`[direct-call]`；同一个 canonical parent 在 `0x597FC4`、
  `0x598038`、`0x5980B8` 三次调用本入口，不能据此重复创建函数 agent。
- 审计 agent path：`/root/f_42cf28/f_59a8d8/f_597f38/f_59aeec`
- 本轮日期：2026-07-25
- 唯一权威：Android kirikiroid2 `libkrkr2.so` 内嵌实现；未查看、引用或从外部
  `psbfile.dll` 推导。`PSBFile.dll` 在二进制中只允许解释为 NCB 模块名字面量，
  本函数本身不读取该字面量。

## Android vtable / NCB / 调用链归属与 fresh 证据

2026-07-25 本新会话由
`/root/f_42cf28/f_59a8d8/f_597f38/f_59aeec` fresh 调用 IDA MCP
`decompile(addr="0x59AEEC")`；随后调用完整
`disasm(addr="0x59AEEC", include_total=true, max_instructions=500)`、
`xrefs_to(0x59AEEC)`、wrapper vtable 原始字节读取与基础设施符号查询复核。
本地址此前没有可直接沿用的报告，本报告完全基于本轮 fresh 证据。

- fresh decompile / disasm 覆盖 `0x59AEEC..0x59B148`，IDA size 为 `0x260`，
  完整 149 条指令；下一 emitted 入口从 `0x59B14C` 开始。本入口是
  `ncbRegistNativeClass<PSBFile>` 的 `RegistItem` 语义实现。虽然源码方法是 virtual
  override，PSBFile 的 typed 注册体已被优化器去虚化为三处 retained direct call；
  `xrefs_to(0x59AEEC)` 只返回 parent 内的 `0x597FC4/0x598038/0x5980B8`。
- 入参语义由 `0x59AF10..0x59AF28` 与后续访问确定为
  `{delegate, memberName, item}`。delegate 的 `+0` 是 class-name 指针、`+8` 是
  class object、`+0x10` 是 `hasCtor`；`0x59AF24` 对 member name 与 class name 做的
  是**指针相等**比较，不是字符串内容比较。
- 当名称指针相等且 `hasCtor` 已为 true 时，`0x59AF34..0x59B030` 依次构造
  `L"Multiple constructors.(" + className + L")"`，调用 `sub_A183A4` 的日志路径，
  再释放所有已构造的 refcounted 字符串临时量。`0x59B034..0x59B038` 无论原 flag
  是否为 true 都把 `hasCtor` 写为 true；若名称指针不等则完全不触碰该 flag。
- `0x59B03C` 是唯一 item-null gate。item 为 null 时不注册、也不调用 Release；此前
  若名称等于 class name，`hasCtor=true` 的副作用仍然保留。
- item 非空时，`0x59B040..0x59B098` 按顺序取得
  `classObj=delegate+8`、`item->GetDispatch()`、`className=delegate+0`、
  `item->GetType()`、`item->GetFlags()`，并直接调用
  `ncb_registerMember@0x9F5AF4(classObj, memberName, dispatch, className, type, flags)`；
  `0x59B09C..0x59B0A8` 随后虚调 `item->Release()`。
- 三只 PSBFile item 共享的 `ncbIMethodObject` vtable address-point `0x19FE1F8`
  原始四槽为 `0x534DA4/0x534DAC/0x534DBC/0x534DCC`，分别对应
  GetDispatch/GetFlags/GetType/Release；最后一槽已由 IDA 识别为
  `nullsub_257@0x534DCC`。因此 Android **仍发出 Release 调用**，但本节点三只内嵌
  interface item 的具体 Release 是 no-op；不能因为结果不可观察而删掉该源码步骤。
- 注册得到的 dispatch 指向三只 wrapper 主对象。fresh 原始 vtable 字节确认：
  factory address-point `0x1A0B5D0` 的 FuncCall 槽 `0x1A0B5E0` 为 `@59B14C`；
  root Property address-point `0x1A0B6F0` 的 PropGet 槽 `0x1A0B710` 为 `@59B28C`；
  load Method address-point `0x1A0B810` 的 FuncCall 槽 `0x1A0B820` 为 `@59B570`。
  这证明三条 TASK_TREE child 边是 `[registration]`，本入口没有直接调用它们。
- `0x59B0F4..0x59B140` 是重复构造器日志表达式的异常清理路径：按构造进度释放
  `")"` 结果、中间拼接结果和前缀字符串，再 `_Unwind_Resume`。metadata vcall 或
  `ncb_registerMember` 抛出时没有本帧 item owner/guard，异常直接传播且位于其后的
  `item->Release()` 不执行；当前本地同样没有补造 cleanup。
- 旧 Hex-Rays 把函数返回型误推为指针，是无返回值方法中 X0 残值及多个虚调用造成的
  原型恢复错误；三个 caller 均不消费返回值。2026-08-02 已纠正为
  `void(state, name, const ncbIMethodObject_arm64 *item)`，复反编译直接显示
  `GetDispatch/GetType/GetFlags/Release` 四槽。

## Android 伪代码（9 行）

```text
RegistItem(r, name, item):
    if name == r.className:                         // 指针相等，不是字符串比较
        if r.hasCtor: Log("Multiple constructors.(" + r.className + ")")
        r.hasCtor = true                            // 重复也继续，不抛错
    if item == null: return void                    // ctor flag 副作用已发生
    dsp = item.GetDispatch(); type = item.GetType(); flags = item.GetFlags()
    RegisterNCM(r.classObj, name, dsp, r.className, type, flags)
    item.Release(); return void
    // 日志临时量异常时逐项析构并重抛；metadata/RegisterNCM 抛出则跳过 Release
```

上面覆盖了名称不等、首次构造器、重复构造器、null item、正常注册和异常清理边界；
本入口对 null delegate/class object、悬空 name 或非法 item vtable 没有额外保护。

## 本地精确文件、符号、行号与逐行对照

- `cpp/core/plugin/ncbind.hpp:1819-1841`，符号 `ncbRegistNativeClassBase`：
  `NameT/ItemT/FlagsT` 类型与 `_className` 字段；对应伪代码的 `name/item` 与
  `r.className`。
- `cpp/core/plugin/ncbind.hpp:1843-1853,1940-1942`，符号
  `ncbRegistNativeClass<PSBFile>`：字段为 `_classobj`、`_hasCtor`，构造默认值是
  `0/false`。Android 的 `delegate+8/+0x10` 是 ARM64 ABI 布局证据，本地由编译器按
  wasm ABI 自行布局，没有用 padding 硬凑偏移。
- `cpp/core/plugin/ncbind.hpp:1879-1887`，目标符号
  `ncbRegistNativeClass<PSBFile>::RegistItem`：第 1880 行保留 NameT 指针相等判定；
  第 1881 行只在重复时记录同一完整日志；第 1882 行无条件把 ctor flag 置 true；
  第 1884 行保留 item-null gate；第 1885 行以 class object、name、dispatch、class
  name、type、flags 调用注册 helper；第 1886 行仅在 helper 正常返回后调用 Release。
- `cpp/core/plugin/ncbind.hpp:835-844`，`ncbIMethodObject`：四个虚槽顺序严格为
  `GetDispatch/GetFlags/GetType/Release`；`cpp/core/plugin/ncbind.hpp:895-904` 的
  内嵌 `ncbNativeClassMethodBase::iMethod` 把前三项转发给 parent，而 Release 保留为空
  override，与 Android `0x19FE1F8` 四槽及 `nullsub_257` 一一对应。
- `cpp/core/plugin/ncbind.hpp:849-868,880-904`，`ncbNativeClassMethodBase`：
  `_type`、`_name` 和 `_imethod._this` 的共同 wrapper 结构提供当前函数消费的 dispatch、
  type、flags metadata；默认 `GetFlags()` 为 0。factory/root/load 各自 wrapper 的具体
  调用行为属于三个 child 节点，不在本报告代审计。
- `cpp/core/tjs2/tjsNative.h:228-234`，`TJSNativeClassRegisterNCM`：参数顺序精确为
  `(cls,name,dsp,classname,type,flags=0)` 并转发 `cls->RegisterNCM(...)`；
  `cpp/core/tjs2/tjsNative.cpp:248-290` 是该 callee 的本地实体。此处只证明当前入口的
  handoff 位置和 ownership 边，不替基础设施 callee 扩大审计范围。
- `cpp/plugins/psbfile/main.cpp:751-755`，
  `NCB_REGISTER_CLASS(PSBFile)` 的 typed body 依次建立 Factory、`root` Property、
  `load` Method；`cpp/core/plugin/ncbind.hpp:1711-1724,1730-1732,1767-1776` 证明三项都经
  `DoItem → delegate.RegistItem` 进入本入口。它们对应 parent 的三个 direct call，
  不是三个独立的本函数实例。

逐行对应：伪代码第 1-4 行对应 `ncbind.hpp:1880-1883`；第 5 行对应
`:1884`；第 6 行对应 `:1885` 的三个 interface getter；第 7 行对应同一行的
`TJSNativeClassRegisterNCM`；第 8 行对应 `:1886` 和函数的 `void` 签名；第 9 行由
Android `0x59B0F4..0x59B140` 与同一 C++ 临时量/调用顺序的异常语义覆盖。

## 六维审计

| 维度 | 状态 | Android 与本地逐项证据 |
| --- | --- | --- |
| 源代码结构 | `MATCH` | Android 保留 `ncbRegistNativeClass<PSBFile>` delegate 的 class-name/class-object/ctor-flag 状态、一个 `RegistItem` override、`ncbIMethodObject` 四槽 interface 与注册 helper handoff；本地没有把它简化成裸 dispatch 注册或不同抽象，模板分层和步骤顺序一致。 |
| 数据流 | `MATCH` | Android 仅用 name 与 class-name 的指针相等更新 ctor flag；非空 item 的 dispatch/type/flags 与 delegate 的 class object/class name 原样组成 `RegisterNCM` 六参数。当前本地第 1879-1887 行保留同一输入、默认 `flags=0` 来源、顺序和无返回值结果。 |
| 调用链 | `MATCH` | Android 由唯一 parent `@597F38` 在三处直接进入，随后 vcall item metadata、直调 `ncb_registerMember@0x9F5AF4`、再 vcall item Release；本地是 `RegisterMembers → DoItem → ncbRegistNativeClass::RegistItem → TJSNativeClassRegisterNCM → item::Release`。`@59B14C/@59B28C/@59B570` 只作为注册后 wrapper vslots，不被伪装成 direct callees。 |
| 对象生命周期 | `MATCH` | duplicate 日志路径的 refcounted 字符串临时量在正常和异常路径均按构造进度释放；null item 不注册也不 Release；非空 item 先把 dispatch 交给 class registration，再调用 interface Release。三只 PSB item 的 Release 虽是 no-op，源码调用仍被两边保留；注册/metadata 抛出时两边都跳过该后置调用。 |
| 内部容器实现 | `N/A` | 本入口自身不构造、遍历或修改 STL/KiriKiri 容器、TJS Array 或 autoreg 链；class member table 的实际更新封装在 `ncb_registerMember` callee 中，不能归作本函数内联容器实现。 |
| 边界行为 | `MATCH` | Android 明确使用指针相等；重复构造器只记录日志而不拒绝后续注册；name 相等且 item 为 null 时仍置 ctor flag；name 不等时 flag 不变；item null 是唯一安全短路，其他无效指针无保护；异常传播与临时量 cleanup 均与本地 C++ 源码一致。 |

## 总判定

`ALIGNED`

在本轮 fresh Android decompile、完整 149 指令 disasm、唯一 caller 集、共享 item
interface vtable 与三只 wrapper vtable 绑定证据可证明范围内，`0x59AEEC` 与当前
`ncbRegistNativeClass<PSBFile>::RegistItem` 的六维行为一致。

## 确定 GAP

无。

证据边界：stripped/O3 二进制没有保留该 template override 的精确源码标识符，因此名称
继续使用 `_guess`；旧 pointer-return 已按 caller 数据流纠正为 `void`，不是生产行为
差异。详见
[../FOLLOWUP_IDB_CLASSINFO_REGISTRATION_ABI_TYPES_2026-08-02.md](../FOLLOWUP_IDB_CLASSINFO_REGISTRATION_ABI_TYPES_2026-08-02.md)。

## TASK_TREE 直接子节点与 cross-reference

- 尚待由本节点在阶段 B 派发的 canonical 直接子节点（本阶段未 spawn）：
  - `0x59B14C PSBFile_factory_FuncCall_guess [registration]`
  - `0x59B28C PSBFile_root_PropGet_guess [registration]`
  - `0x59B570 PSBFile_load_FuncCall_guess [registration]`
- 三条 child 关系均表示本入口把相应 dispatch 注册到 PSBFile class object，使后续 TJS
  vdispatch 到达 wrapper vslot；本入口没有直接调用任何 child，不能改标
  `[direct-call]`。
- 非 canonical cross-reference：`ncb_registerMember@0x9F5AF4` 是本入口唯一 retained
  direct infrastructure handoff，但不属于 114 个 psbfile canonical emitted 节点，不得
  为其创建函数 agent。
- parent `@597F38` 的三处 direct call 是同一个 canonical parent/function agent；不得按
  Factory/root/load 三次调用重复派发 `0x59AEEC`。
