# PSBValueDispatch 构造器、hidden-sret 与完整 vtable ABI 类型纠正

日期：`2026-08-02`。

## 范围与权威证据

本轮只修正 Android `libkrkr2.so` IDB 的类型与注释，不修改生产 C++。权威目标仍是
`reference/libkrkr2/libkrkr2.so`（SHA-256
`ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`）。

主会话 fresh 执行了：

- `decompile/disasm/xrefs_to(0x597AD4)`，并重新检查三个 direct caller
  `0x6A931C/0x6AA124/0x6AA424`；
- `decompile(0x596D90/0x596E24/0x596EF0/0x596F0C/0x596F40)`；
- 完整导出 `0x59641C..0x59B7E8` 的当前函数原型，确认 dispatch vtable 仍有一组
  stripped 默认 `__int64` 类型残留。

各入口的 vtable slot、函数体和边界仍由对应 `functions/0xADDR.md` 报告约束。本轮没有从
本地名称反向命名二进制，也没有用类型标注升级任何 stripped 源码 token 的证据等级。

## 构造器旧类型纠正

`PSBValueDispatch_ctor_guess@0x597AD4` 的旧 IDB 类型是
`PSBValueDispatch *(result, PSBRawOwner *const *ownerSlot, node)`。完整 17 条指令与三个
new-expression caller 实际证明：

```text
Ctor(this, fileHolder, node):
    写 primary/secondary vptr；refCount = 1
    owner = fileHolder.owner；保存 owner
    if owner != null: owner.refCount++          // 非原子 uint32
    保存 node；valid = true
    裸 RET；三个 caller 均不消费残留 X0
```

同谱系 iOS arm64 的 standalone holder / raw-node-first-subobject caller 只用于在 Android 已
约束的候选中支持 `PSBFile`-compatible holder 分组。IDB 现改为
`void(PSBValueDispatch *self, const PSBFile *file, const uint8_t *node)`；这里的 pointer 是
`const PSBFile &` 的目标 ABI 表达。三处 caller 的旧 `ownerSlot/result` 注释也改为
`fileHolder/constructor this`。重新反编译已显示 `owner = file->owner`，并且伪
`return result` 消失。

## primary / secondary vtable 补型

IDB 已补充 opaque `iTJSNativeInstance` 与 `tTJSVariantString`，并一次性成功应用 33 个函数
类型（`applied=33, failed=0`）：

- primary dispatch slots：`FuncCall/FuncCallByNum`、`PropGet/PropGetByNum`、
  `PropSet/PropSetByNum/PropSetByVS`、`GetCount/GetCountByNum`、`EnumMembers`、
  `DeleteMember/DeleteMemberByNum`、dispatch `Invalidate/InvalidateByNum`、
  `IsValid/IsValidByNum`、`CreateNew/CreateNewByNum`、`Reserved1`、
  `IsInstanceOf/IsInstanceOfByNum`、`Operation/OperationByNum`、
  `NativeInstanceSupport`、`ClassInstanceInfo`、`Reserved2/Reserved3`；
- primary native tail：`Construct`、native `Invalidate`、native `Destruct`；
- secondary `iTJSNativeInstance` table：`Construct` thunk、`Invalidate`、`Destruct`。

`AddRef/Release` 与四个已有非平凡 dispatch 方法原先已经带正确类型，不重复改写。所有
一指令 stub 的完整未消费参数来自已证明的 vtable 接口槽 ABI；函数体仍只决定实际读取与
返回，不因为补型而虚构数据流。

## 复反编译结果

- `NativeInstanceSupport@0x596D90` 现明确显示
  `(self, flag, classid, iTJSNativeInstance **pointer) -> int`，返回值直接为
  `-1002/-1/0`，成功写入 `&self->native_vftable`；
- `IsInstanceOf@0x596E24` 现恢复六参数 ABI，明确区分 `membername` 与 `classname`；
- `IsValid@0x596EF0` 现直接读取 `self->valid` 并返回 `1/2`；
- dispatch `Invalidate@0x596F0C` 现明确以 `membername` gate，随后读取/清除
  `self->valid` 并返回 `-1002/-1006/0`；
- `DeleteMemberByNum@0x596F40` 现显示完整四参数接口与 `int(-1002)`，不再把 W0 返回
  错展成 64 位正数。

最后已调用 `idb_save` 保存活动 IDB。该轮修正的是分析数据库的已证伪状态；生产源码本来
已经逐项对齐，故没有 `cpp/` 修改，审计统计仍为
`ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`。

## hidden-sret 伪返回纠正

随后对 112 个连续主实现簇入口的全部当前 prototype 做机械筛选，找出所有带 X8 sret 的
函数。`Transfer/GetDictionaryValueStrict/GetDictionaryKeys` 已是正确的 `void + X8`；另两项
仍错误保留 X0 返回：

1. `PSBFile_GetRoot_guess@0x598A3C`：fresh 10 指令 disasm 与唯一 caller
   `ResourceManager_loadResource@0x6A8F20` 明确证明 X8 是两指针 `PSBRawNode` 结果槽，X0
   从入口保持 `self` 只是 ABI 残值。类型已改为
   `void __usercall(const PSBFile *self@X0, PSBRawNode *result@X8)`；复反编译的伪
   `return self` 已消失。
2. `PSBFile_loadMethod_CopyFirstArgument_guess@0x59B708`：fresh 56 指令 disasm 与唯一 caller
   `0x59B570:0x59B634` 明确证明 X8 是按值 `tTJSVariant` 结果槽；函数构造结果后析构局部
   Variant，尾部 `RET` 前没有建立 X0 返回。类型已改为
   `void __usercall(void *functor@X0, tTJSVariant_psb_arm64 *result@X8)`，先清除伪 X0 返回。
   后续目标内 fresh 复核又以本函数直接读取的 `+4/+0x10` 和唯一 caller 写入的
   `+4/+8/+0x10` 为独立证据，将 functor 收紧为中性
   `PSB_paramsFunctor_arm64 *` ABI 记录；仍未猜 stripped 模板实例的精确源码名。详见
   [FOLLOWUP_IDB_NCB_WRAPPER_ABI_TYPES_2026-08-02.md](FOLLOWUP_IDB_NCB_WRAPPER_ABI_TYPES_2026-08-02.md)。

第二次全量 X8 prototype 查询现只返回五个已知 sret 入口，全部为 `void + result@X8`。
本轮最终再次保存 IDB。
