# PSBFile NCB factory / root / load wrapper ABI 类型纠正

日期：`2026-08-02`。

## 范围与证据边界

本轮只修正 Android `libkrkr2.so` 活动 IDB 的类型、字段记录与注释，不修改生产
`cpp/`。权威证据是目标函数自身的 fresh decompile/disasm、vtable 槽、注册构造点和
caller/callee 数据流；本地 ncbind 模板只作最终交叉对照，不用于反向决定二进制行为。

本轮 fresh 复核的五个高风险入口是：

- factory `FuncCall@0x59B14C`；
- root Property `PropGet@0x59B28C`、`PropSet@0x59B378` 与 typed
  `Invoke@0x59B48C`；
- load Method `FuncCall@0x59B570`，并重新反编译其 hidden-sret 参数 helper
  `@0x59B708`。

同时复核并补型同一 vtable 的 deleting destructor / `GetFlags` sibling：
`0x59B268/0x59B460/0x59B484/0x59B6DC/0x59B700`。

## 目标内可证明的 ARM64 ABI 记录

IDB 新增下列中性记录：

| 记录 | 大小 | 目标内字段证据 |
| --- | ---: | --- |
| `PSB_paramsFunctor_arm64` | `0x18` | `+4 numparams`、`+8 result`、`+0x10 param`；首 4 字节只记作 `converterStorage`，不猜 stripped 模板 token。 |
| `PSB_member_pointer_arm64` | `0x10` | 两 qword Itanium member-pointer：function/vtable offset 与 this-adjust/virtual flag。 |
| `PSBFile_ncbFactoryWrapper_arm64` | `0x38` | `+0x30` 是四参 factory callback。 |
| `PSBFile_rootPropertyWrapper_arm64` | `0x50` | `+0x30` getter、`+0x40` setter，各为两 qword member-pointer。 |
| `PSBFile_loadMethodWrapper_arm64` | `0x40` | `+0x30` 是 load member-pointer。 |

三个 wrapper 的前 `0x30` 字节只在 IDB 中记作 `baseStorage`。这是 Android ARM64
对象布局的分析记录，不是原始源码 padding，也不得复制到 wasm32 生产类中硬凑偏移。

## 应用的函数类型

一次 batch 成功应用 11 个函数类型（`applied=11, failed=0`）：

```text
0x59B14C int factory_FuncCall(self, flag, membername, hint,
                              result, numparams, param, objthis)
0x59B268 void factory_deletingDestructor(self)

0x59B28C int root_PropGet(self, flag, membername, hint, result, objthis)
0x59B378 int root_PropSet(self, flag, membername, hint, const param, objthis)
0x59B460 void root_deletingDestructor(self)
0x59B484 uint32 root_GetFlags(self)
0x59B48C bool root_Invoke(paramsFunctor, memberPointer, const PSBFile *)

0x59B570 int load_FuncCall(self, flag, membername, hint,
                           result, numparams, param, objthis)
0x59B6DC void load_deletingDestructor(self)
0x59B700 uint32 load_GetFlags(self)
0x59B708 void __usercall load_CopyFirstArgument(functor@X0,
                                                tTJSVariant result@X8)
```

`0x59B708` 仍保留 hidden-sret 的 `void + result@X8`，但 `functor@X0` 已从泛化
`void *` 收紧为 `PSB_paramsFunctor_arm64 *`。这不是从本地模板猜名：目标函数直接读取
`+4/+0x10`，唯一 caller `@0x59B570` 又在同一对象写入 `+4/+8/+0x10`，足以独立固定该
ABI 记录；不可恢复的精确模板实例名仍未写入 IDB。

## fresh 复反编译结果

- factory/root/load 三个 vtable 调用入口现均显示标准 TJS 参数角色及 32 位 `int`
  错误码，`-1001/-1004/-1007/-1008/-1` 不再被扩展成泛化 64 位返回；
- root wrapper 现直接显示 `self->getter/self->setter`，load wrapper 显示
  `self->method`；两者仍保留 member-pointer 的 this-adjust 与 virtual 分支；
- root `Invoke` 现为 `bool`，并直接读取 `io->result`；
- `0x59B708` 现直接读取 `functor->numparams/functor->param`，hidden-sret 结果仍由
  `X8` 构造；
- 两个 `GetFlags` 现为 `unsigned int(self)`，返回常量零；三个 deleting destructor
  现带各自 wrapper `self` 类型。

factory 的 callback 字段已经是准确四参函数指针，但 Hex-Rays 在间接调用表达式中仍可能
把未覆写的 `X4/result` 打印成第五参数。raw AArch64 在 `0x59B190..0x59B1A8` 只为
`X0..X3` 准备 `{&inst, numparams, param, objthis}`；因此这是反编译器残影，不是源码
参数。IDB 已在 `0x59B1A8` 就地注释该边界。

最后已调用 `idb_save`，活动数据库保存到
`C:\Users\fenghengzhi\libkrkr2\libkrkr2\libkrkr2.so.i64`。本轮没有发现生产实现
行为差异，故没有 `cpp/` 修改，统计保持
`ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`。
