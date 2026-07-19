# PSBFile.dll 函数覆盖 manifest（2026-07-19）

## 边界纠正

IDA 只读枚举证明 PSBFile 相关实现连续覆盖 `0x59641C..0x59B708`，共 108 个
function。旧审计把 `0x59AA84` 当作终点，实际它是 typed NCB 注册尾链的起点，因而
少计了 19 个模板入口。下一函数 `0x59B7E8` 是通用
`std::vector<std::string>::_M_emplace_back_aux` 实例化；它没有来自本区间的 code xref，
只有 PLT/data xref，不属于 PSBFile typed NCB 调用链。

下列分组穷举 108 个入口；每组地址数之和为 `42 + 1 + 10 + 19 + 17 + 19 = 108`。

## A. PSBValueDispatch 与 packed dispatch ABI（42）

本地对应 `cpp/plugins/psbfile/main.cpp` 的 `PSBValueDispatch`、raw→Variant 转换、
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

## B. packed name 反向解码 helper（1）

```text
0x597B1C
```

本地对应 `PSBRawNode::GetDictionaryKey/GetDictionaryEntry` 使用的 raw name trie 解码；
临时 `std::vector<char>`、reverse、`std::string` 构造和析构顺序已复原。

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
raw node copy/move/try/strict/contains、category/int/real/string/resource。

## E. media pre-register、singleton 与 psb: gateway（17）

```text
0x59849C 0x5997F0 0x599830 0x599878 0x599888 0x5998A8
0x5998BC 0x5998C0 0x5998C4 0x59993C 0x5999F4 0x599DD8
0x599E04 0x59A0B4 0x59A284 0x59A330 0x59A4B0
```

本地对应 `PSBMediaRegistry.cpp` 与 `PSBMedia.cpp/.h`：process-lifetime singleton、
非原子 refcount、normalize nullsub、exists/open/list/local-name、container replacement、
borrowed resource pointer 与 raw-node Resolve 生命周期。

## F. typed NCB 注册尾链（19）

| 地址 | 二进制职责 | 本地生成来源 |
| --- | --- | --- |
| `0x59AA84` | class init、single-registration state、finalize 注册 | `NCB_REGISTER_CLASS(PSBFile)` |
| `0x59ABD8` | 0x18-byte native holder `{vptr,PSBFile*,constructed}` 构造 | ncb instance adaptor |
| `0x59AC04` | finalize callback 返回 0 | NCB finalize wrapper |
| `0x59AC0C` | holder cleanup/reset | ncb instance adaptor |
| `0x59AC7C` | complete destructor | ncb instance adaptor |
| `0x59AD08` | deleting destructor | ncb instance adaptor |
| `0x59AD84` | member/global dispatch 注册 | typed class registrar |
| `0x59AEE4` | missing native method返回 `TJS_E_NOTIMPL` | NCB method base |
| `0x59AEEC` | duplicate-constructor gate与 member metadata 注册 | typed member registrar |
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

108 个入口均已归属于生产源码、明确的 NCB/iTJS ABI wrapper 或本源码触发的 STL
实例化；没有未归属业务入口。该 manifest 证明静态函数覆盖完整，但不替代损坏输入的
Android runtime oracle；MDF zlib failure 与 filter 后 offset failure 仍按主分析记录为
缺少天然 fixture 的验证缺口。
