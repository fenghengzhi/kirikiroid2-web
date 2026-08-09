# Follow-up：完整 call-site 与 tail-transfer surface

日期：`2026-08-03`。本轮继续只读权威 Android ARM64 `libkrkr2.so`。组件级分析工具曾把
internal call graph 错汇总为空，因此本轮不采用该 negative result，而是从 114 个 FDE 的
真实 AArch64 指令与 IDA code xref 重新枚举 `BL/B/BLR/BR`。没有修改 `cpp/` 或测试物料。

## 结论

- 114 个 FDE 内共有 **567** 个 transfer site：`468 BL`、`11` 个跨 FDE 尾 `B`、
  `45 BLR`、`1` 个非 switch `BR`，另有 `42` 个已由 jump-table surface 证明的 switch
  dispatch `BR`。
- 479 个 direct call/tail site 指向 86 个唯一目标；其中 **44 个 site / 39 个唯一边**连接
  MANIFEST 内函数，剩余 435 个 site 指向 65 个通用 C++/TJS/ncbind/storage callee。
- 46 个非 switch 间接 transfer 已逐点读取前置 vtable/member-pointer/manager load：全部可
  归属到现有本地 callback、stream、lister、OwnerFilter、NCB adaptor/wrapper 或析构路径。
- 54 个 MANIFEST 函数没有非 switch transfer；它们都是纯标量/访问器/stub/no-op/static
  initializer 形状，没有发现本地额外 helper 或目标缺失 callee。
- 完整 callsite serialization 与 39 条 internal edge 现由 ELF 门禁机械固定；本轮没有
  新生产 GAP，统计继续为 `99/15/0`。

## 枚举口径

对每个 MANIFEST FDE 的每个 4-byte word 直接解码：

- `BL imm26` 全部计为 direct call；
- 只有 destination 落在 owner FDE 外的无条件 `B imm26` 才计为 tail transfer；
- `BLR Xn` 计为 indirect call；
- `BR Xn` 若地址属于已验证的 42 个 switch instruction，则计为 switch dispatch，否则计为
  indirect tail（目标中仅 `PSBMedia::Release@0x59989C` 一处）。

这避免把函数内普通 `B`、条件跳转或 switch `BR` 误写成 callee。每条记录序列化为
`<owner:u64, ea:u64, kind:u8, target-or-register:u64>`；567 条的 SHA-256 为
`162c0eb8a8f6cc1f6a904241f5e5a1b144bff9e294d4167e4174929c85adf986`。

## 39 条 MANIFEST 内调用边

### packed / dispatch

```text
PropGet@597854 -> FindNameIndex@59641C ->/ FindDictionaryOffset@59659C -> CreateVariant@59673C
PropGetByNum@5976C4 -> CreateVariant@59673C
EnumMembers@596F50 -> CreateVariant@59673C, DecodeName@597B1C
decodeName wrapper@5975C0 -tail-> DecodeName@597B1C
```

其中第一行表示 `PropGet` 分别直调三个 helper，不表示三个 helper 互相调用。对应本地
`main.cpp:135-173,218-279,374-443,573-687` 与 `PSBPackedInternal.h:270-280`。

### raw file / node

```text
Factory@5980F4 -> Load@598268
Load@598268 -> LoadStorage@598538, Adopt@598708
LoadStorage@598538 -> Adopt@598708
Strict/Try Dictionary@598C58/598D58 -> FindNameIndex@59641C, FindDictionaryOffset@59659C
GetDictionaryKeys@598E64 -> DecodeName@597B1C
ContainsDictionaryKey@5995D8 -> GetDictionaryValue@598D58
```

对应 `PSBRawFile.cpp:249-307,443-541` 与 `main.cpp:732-748`。目标中的 MDF clone 是
`Load/LoadStorage` 内联体，不存在额外 MANIFEST edge；本地也保持源码级 helper 被优化
内联的结构，而没有伪造目标不存在的 emitted 入口。

### PSBMedia

```text
Check/Open@5998C4/59993C -> EnsureContainer@599E04, GetResourceData@59A0B4
GetListAt@5999F4 -> EnsureContainer@599E04, Resolve@59A4B0, DecodeName@597B1C
EnsureContainer@599E04 -> IndexOf@59A284, LoadStorage@598538, CreateAdaptor@59A330
GetResourceData@59A0B4 -> Resolve@59A4B0
Resolve@59A4B0 -> IndexOf@59A284, Contains@5995D8, StrictGet@598C58
```

对应 `PSBMedia.cpp:12-213`。目标 `GetResourceData` 内联 `GetResource`，因此没有
`@5996E4` direct edge；本地保留 source-level member call，并已由完整 clone 证明，不把
优化后无 `BL` 误判成调用链 GAP。

### NCB registration / typed wrappers

```text
registerMembers@597F38 -> RegistItem@59AEEC (2 BL + final tail B)
AutoRegister::Regist@59A8D8 -> RegistBegin@59AA84, registerMembers@597F38, RegistEnd@59AD84
AutoRegister::Unregist@59A968 -> registerMembers@597F38
root PropGet/PropSet@59B28C/59B378 -> typed root Invoke@59B48C
load FuncCall@59B570 -> CopyFirstArgument@59B708
```

这与 `NCB_REGISTER_CLASS(PSBFile)`、`Factory/Property/Method` 以及
`ncbind.hpp` 的 Regist/Unregist/wrapper 模板层一致。`RegistEnd` 在 `@59A8D8` 有两个
callsite，但集合层仍是一条 source→target edge，故 44 site 对应 39 unique edge。

## 65 个外部 direct target

高频 target 说明 target call surface 主要固定对象生命周期与异常边，而不是遗漏的
psbfile helper：

| target | site 数 | 语义 |
| --- | ---: | --- |
| `sub_520FAC@0x520FAC` | 72 | catch + `std::terminate` landing helper |
| `operator delete@0x415740` | 44 | owner/wrapper/vector 对象释放，含 5 个 deleting-destructor tail 与 1 个 DecodeName vector-cleanup tail |
| `tTJSVariant_Release@0xA13274` | 40 | ttstr/variant backing release |
| `tTJSVariant_dtor_guess@0xA0F778` | 32 | Variant 析构，含 PSBMedia complete-destructor tail |
| `__stack_chk_fail@0x406D70` | 31 | stack canary failure |
| `_Unwind_Resume@0x41A950` | 28 | EH cleanup continuation |
| aligned dealloc `sub_A0DE90` | 24 | raw PSB owner allocation释放 |
| diagnostic `sub_95440C` | 22 | PSB/ncbind exception helper |
| `operator new@0x414210` | 15 | dispatch/raw owner/wrapper/vector allocation |

其余 direct targets 是 `uncompress/memcpy/memmove`、stream/storage、ttstr/Variant、
`std::vector<std::string>` reserve/emplace、NCB class/member registration 与 guard runtime；
均已在对应逐函数报告中有 source mapping。

## 46 个非 switch 间接 transfer

逐点 backward slice 得到以下闭合分类：

| 分类 | site | target source shape |
| --- | ---: | --- |
| Variant/dispatch release | 2 | `CreateVariant` 与 root typed Invoke 的临时 Object/ObjThis release |
| EnumMembers callback | 2 | Array/Dictionary 两个 `callback->FuncCall` 循环 |
| `std::function` manager cleanup | 6 | `Load` 与 `EnsureContainer` 正常/EH 路径的 `OwnerFilter` 清理 |
| stream virtuals | 4 | `LoadStorage` 的两次 `GetSize` 与正常/EH 两条 virtual destructor 边 |
| `OwnerFilter` invoke | 1 | `Adopt` 成功后的 `filter(*owner_)` |
| PSBMedia deleting-destructor tail | 1 | `Release` 的 ref==1 分支经 vtable slot 1 `BR X1` |
| storage lister | 2 | Array index与 Dictionary key 的 `lister->Add` |
| newly-created adaptor ref ops | 3 | `EnsureContainer` 构造 Variant 所需的 AddRef/AddRef/Release |
| adaptor/class-object creation | 3 | `CreateAdaptor` 的 `CreateNew`、temporary release、`NativeInstanceSupport` |
| Resolve native lookup | 1 | `_file` dispatch 的 `NativeInstanceSupport` |
| Unregist global dispatch | 4 | 两条 delete-member/release 路径 |
| RegistEnd global/class dispatch | 4 | class object retain、global publish与 release |
| RegistItem item interface | 4 | `GetDispatch/GetType/GetFlags/Release` |
| factory wrapper | 2 | factory callback + native instance lookup |
| root PropGet/PropSet native lookup | 2 | 两只 wrapper 各一处 `NativeInstanceSupport` |
| root typed Invoke | 3 | member-pointer GetRoot + returned dispatch two retains；最终 release 计入首行 |
| load wrapper | 2 | native instance lookup + member-pointer `PSBFile::Load` |

这些 site 与 `PSBMedia.cpp`、`PSBRawFile.cpp`、`main.cpp`、`ncbind.hpp` 的对象/接口边界
一一对应。没有把 `BLR` 简化成未知阻塞项，也没有凭本地名字猜 target vslot。

## 11 个 direct tail 与 54 个无 transfer 函数

11 个跨 FDE `B` 包括：`decodeName -> DecodeName`、`registerMembers -> RegistItem`、
pre-register callback -> storage registration、PSBMedia name -> ttstr assignment、一个
Variant destructor tail、5 个 deleting-destructor -> `operator delete`，以及 1 个
DecodeName vector-cleanup -> `operator delete`。它们与本地 wrapper/return-tail/defaulted
destructor/container cleanup 结构一致。

54 个无非-switch transfer 入口由 dispatch `TJS_E_NOTIMPL` stubs、class-info accessors、
native no-op、refcount leaves、raw holder scalar accessors和两只 static initializer 组成。
“无 transfer”由完整 FDE 解码证明，不是一次 negative grep。

## 机械门禁

`verify_elf_surface.py` 现同时：

1. 直接解码 114 个 FDE 的 `BL/B/BLR/BR`；
2. 区分 owner 内 branch、cross-FDE tail 与 42 个 switch `BR`；
3. 比较五类精确计数、完整 serialization SHA、39 条 internal edge；
4. 固定 direct target、internal/external site 与 no-transfer function 数。

通过输出：

```text
callsite_surface=true sites=567 direct_calls=468 direct_tails=11 indirect_calls=45 indirect_tails=1 switch_dispatches=42
callsite_targets=86 internal_sites=44 internal_edges=39 external_sites=435 no_transfer_functions=54 sha256=true
```

该门禁固定 emitted call/tail topology；被 Android O3 内联且有独立完整 clone 证明的
source-level helper 仍按既有证据恢复，不能反过来以“没有 BL”为由删除。

后续输入侧复核已把其中全部 306 个 normal direct `BL` 与 11 个 direct tail 从“目标
拓扑”推进到 446 个 AAPCS64 参数、475 条完整 producer 关系；详见
[FOLLOWUP_NORMAL_DIRECT_ARGUMENT_PRODUCER_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_DIRECT_ARGUMENT_PRODUCER_SURFACE_2026-08-03.md)。

同一输入侧复核随后覆盖全部 46 个非 switch indirect transfer：117 个 `X0..X7` 参数、
120 条 producer 关系分别沿 40 个 normal-entry 与 6 个 LSDA landing-only CFG 闭合，
并纠正 factory callback 的 stale `X4` 假第五参；详见
[FOLLOWUP_INDIRECT_ARGUMENT_PRODUCER_SURFACE_2026-08-03.md](FOLLOWUP_INDIRECT_ARGUMENT_PRODUCER_SURFACE_2026-08-03.md)。
