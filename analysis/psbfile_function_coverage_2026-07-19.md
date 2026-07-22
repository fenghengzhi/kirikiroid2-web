# PSBFile.dll 函数覆盖 manifest（2026-07-19）

## 边界纠正

IDA 枚举证明 PSBFile 相关实现覆盖 `0x59641C..0x59B708`，共 111 个
function。旧审计不仅把 `0x59AA84` 错当成终点，还被 IDA 的函数合并漏掉
`0x59A8D8`、`0x59A968` 与 `0x59B14C` 三个独立序言；三者拆分后均有唯一 vtable
data xref。下一函数 `0x59B7E8` 是通用
`std::vector<std::string>::_M_emplace_back_aux` 实例化；它没有来自本区间的 code xref，
只有 PLT/data xref，不属于 PSBFile typed NCB 调用链。

下列分组穷举 111 个入口；每组地址数之和为 `42 + 1 + 10 + 19 + 17 + 22 = 111`。

## A. PSBValueDispatch 与 packed dispatch ABI（42）

本地对应 `cpp/plugins/psbfile/PSBDispatch.h` 的完整类声明和
`cpp/plugins/psbfile/main.cpp` 的 35 个 out-of-line `PSBValueDispatch` 定义、
iTJSDispatch2/iTJSNativeInstance 槽和析构包装。

```text
0x59641C 0x59659C 0x59673C 0x596BC4 0x596C70
0x596D78 0x596D80 0x596D88 0x596D90 0x596E0C 0x596E14 0x596E1C
0x596E24 0x596ED0 0x596ED8 0x596EE0 0x596EE8 0x596EF0 0x596F04
0x596F0C 0x596F38 0x596F3C 0x596F40 0x596F48 0x596F50
0x5975C0 0x5975D0 0x5975D8 0x5975E0 0x5976B4 0x5976BC 0x5976C4
0x597854 0x597A18 0x597A20 0x597A28 0x597A2C 0x597A30 0x597A38
0x597A40 0x597AC0 0x597AD4
```

其中 `0x596F50` 已由完整 tag switch、四 Variant 生命周期、array/dictionary packed
遍历和 callback 三参数调用确认命名为 `PSBValueDispatch_EnumMembers`；不再是 guess。
2026-07-23 的 local→Android Release 对象审计还确认，`GetCount@0x5975E0`、
`PropGetByNum@0x5976C4` 与 `PropGet@0x597854` 的四个直接 count 解码站点已不再产生
`ReadPackedCount_guess` 调用边界；`main.cpp.o` 的 symbol/relocation 表已复核。

## B. packed name 反向解码 helper（1）

```text
0x597B1C
```

本地对应共享的 `detail::DecodeName_guess`。fresh `xrefs_to(0x597B1C)` 的四个真实
消费者是 `PSBValueDispatch::EnumMembers@0x596F50`、零 xref 的包装入口
`0x5975C0`、`PSBRawNode::GetDictionaryKeys@0x598E64` 与
`PSBMedia::GetListAt@0x5999F4`；此前写在这里的
`GetDictionaryKey/GetDictionaryEntry` convenience API 已删除。临时
`std::vector<char>`、reverse、`std::string` 构造和析构顺序已复原。

## C. PSBFile typed NCB 前段与 factory/root（10）

```text
0x597E98 0x597EA8 0x597EB8 0x597EC8 0x597ED0
0x597F08 0x597F24 0x597F38 0x5980F4 0x5981F8
```

本地对应 `PSBFileConvertor`、`PSBFileFactory`、factory 异常清理和 guarded root getter。

## D. raw PSBFile / owner / node / packed table（19）

```text
0x598268 0x598538 0x598708 0x598960 0x598A3C 0x598A64 0x598AAC
0x598B3C 0x598B58 0x598C58 0x598D58 0x598E44 0x598E64 0x599174
0x5992E8 0x599438 0x599554 0x5995D8 0x5996E4
```

本地对应 `PSBRawFile.cpp/.h`。`0x599174` 是该源码中 dictionary-key vector 的
`std::vector<std::string>::reserve` 实例化；其余覆盖 load、MDF、owner/header refresh、
按值返回并消费 holder 的 transfer helper `0x598A64`、raw node 的已观察
copy/transfer 净语义、try/strict/contains、category/int/real/string/resource。优化后
二进制不能证明这些净语义是否来自用户声明的 copy/move special member，也不能证明
相应 self guard；manifest 不把本地 special-member 拆分计作已确认源码形状。

2026-07-22 的 allocator-family 复核补充：`0x598268/0x598538` 的四个 raw/MDF
分配点均调用 `TJSAlignedAlloc(bytes,4)`，owner 析构 `0x598B3C` 与 storage MDF 成功替换
source 调 `TJSAlignedDealloc`；三个失败点 `0x598328/0x59840C/0x59869C` 则逐指令确认
直接调用 `operator delete[]`。本地已恢复这组看似不安全但属于原版源码边界的混合调用链。
同轮确认 `PropGetByNum@0x5976C4` 的最终 packed 相对偏移是 W32 回绕后 SXTW 加址，本地
已从 host-size `uint32_t` 零扩展改为显式 `uint32_t` 回绕与 `int32_t` 符号扩展。
`EnumMembers@0x596F50` 的 dictionary value 路径则在
`0x597388..0x59739C` 先以 W32 合并 table-end displacement 与 entry offset，再 UXTW
加到 `node+1`；本地也已显式复刻这次 32-bit 进位回绕，未改动 array 的 host-base 路径。

2026-07-22 的全站点 packed-width 审计又确认，所有会消费 table end 的站点都先把
`header-10 + count*width` 在 W32 内整体回绕，再从原始 table begin 做 UXTW 加址；本地
`PackedArrayView_guess::end` 已恢复这一根地址规则。`PropGetByNum@0x597810..0x597814`
的 entry-table 索引也已从 signed host product 改为 W32 product + UXTW。其最终 node
relative 仍按 `0x59783C..0x597840` 保持 W32 + SXTW；两类扩展没有互相泛化。

## E. media pre-register、singleton 与 psb: gateway（17）

```text
0x59849C 0x5997F0 0x599830 0x599878 0x599888 0x5998A8
0x5998BC 0x5998C0 0x5998C4 0x59993C 0x5999F4 0x599DD8
0x599E04 0x59A0B4 0x59A284 0x59A330 0x59A4B0
```

本地对应 `PSBMediaRegistry.cpp` 与 `PSBMedia.cpp/.h`：process-lifetime singleton、
非原子 refcount、normalize nullsub、exists/open/list/local-name、container replacement、
borrowed resource pointer 与 raw-node Resolve 生命周期。
2026-07-23 的对象审计确认 `Resolve@0x59A4B0` 已直接建立 root raw node，
`GetResourceData@0x59A0B4` 已直接展开 resource/chunk table 解码；`PSBMedia.cpp.o` 不再
引用 `PSBFile::GetRoot@0x598A3C` 或 `PSBRawNode::GetResource@0x5996E4`。
`0x59A4B0` 的正常返回位于 `0x59A7D8`，异常 landing pads 延伸到 `0x59A8D4`；
其真实函数边界在 `0x59A8D8`，后者是下一只独立自动注册入口。

## F. typed NCB 自动注册与尾链（22）

| 地址 | 二进制职责 | 本地生成来源 |
| --- | --- | --- |
| `0x59A8D8` | 自动注册 `Regist()`：RAII Begin→body→End | `ncbNativeClassAutoRegister<PSBFile>` |
| `0x59A968` | 自动反注册 `Unregist()`：DeleteMember/Release/clear class info | `ncbNativeClassAutoRegister<PSBFile>` |
| `0x59AA84` | class init、single-registration state、finalize 注册 | `NCB_REGISTER_CLASS(PSBFile)` |
| `0x59ABD8` | 0x18-byte native holder `{vptr,PSBFile*,sticky/no-delete}` 构造 | ncb instance adaptor |
| `0x59AC04` | finalize callback 返回 0 | NCB finalize wrapper |
| `0x59AC0C` | holder cleanup/reset | ncb instance adaptor |
| `0x59AC7C` | complete destructor | ncb instance adaptor |
| `0x59AD08` | deleting destructor | ncb instance adaptor |
| `0x59AD84` | member/global dispatch 注册 | typed class registrar |
| `0x59AEE4` | missing native method返回 `TJS_E_NOTIMPL` | NCB method base |
| `0x59AEEC` | duplicate-constructor gate与 member metadata 注册 | typed member registrar |
| `0x59B14C` | raw factory `FuncCall`：void-shell、factory、NIS/install 边界 | `ncbNativeClassFactory<PSBFile>` |
| `0x59B268` | root property wrapper deleting destructor | `Property("root", ...)` |
| `0x59B28C` | root property getter入口与 native-instance检查 | `Property("root", ...)` |
| `0x59B378` | root property只读 setter边界 | `Property(..., setter=0)` |
| `0x59B460` | root getter invoker deleting destructor | property invoker |
| `0x59B484` | invoker base返回 0 | property invoker |
| `0x59B48C` | 调 native root getter并 CopyRef 到结果 | `PSBFile::GetRootDispatch` binding |
| `0x59B570` | `load` method argc/native-instance/结果 wrapper | `Method("load", &PSBFile::Load)` |
| `0x59B6DC` | load wrapper deleting destructor | method wrapper |
| `0x59B700` | method wrapper base返回 0 | method wrapper |
| `0x59B708` | 首参数 Variant 按值转换链 | `PSBFile::Load(tTJSVariant)` binding |

尾链逐函数 fresh decompile 未发现本地手写简化：`Factory/Property/Method` 声明通过
仓库同一 ncbind 模板生成 holder、注册状态、属性/方法 wrapper 和参数生命周期。

## 覆盖结论

111 个 Android 入口均已归属于生产源码、明确的 NCB/iTJS ABI wrapper 或本源码触发的
STL 实例化；没有未归属业务入口。该 manifest 只证明 **Android→local** 的入口归属完整，
不能反向证明 **local→Android** 没有额外抽取边界。另行完成的 local→Android Release
对象审计已消除三类已确认额外边界：dispatch 三函数四站点的 count-helper 调用、
`PSBMedia::Resolve→PSBFile::GetRoot`，以及
`PSBMedia::GetResourceData→PSBRawNode::GetResource`。这不能反向证明原始源码没有
等价 inline helper，也不改变 111-entry Android manifest。当前 raw owner/node 的若干小方法只有
上层调用点内联行为证据、没有独立 Android 入口；其名字与是否为原始 inline helper 仍不能
仅由本地源码宣称。manifest 也不替代损坏输入的 Android runtime oracle；MDF zlib failure、
filter 后 offset failure、损坏 packed table、tag `0x0B`、>4 GiB storage，以及成功
跨-container media replacement、旧 borrowed stream 生命周期、dictionary listing 和
CreateAdaptor-null 分支，仍按主分析记录为缺少天然 fixture 的验证缺口。

本轮修改后 macOS Release `psbfile-dll` 为 **554/554**（8 cases），
`motionplayer-dll` 为 **1197/1197**（15 cases），Web Debug 最终链接通过。
