# Follow-up：字面量、跳转表与共享全局数据面

日期：`2026-08-03`。本轮继续只使用权威 Android ARM64
`reference/libkrkr2/libkrkr2.so` 与 IDA MCP；未读取同版本源码、外部私库、Git LFS
对象，也未使用任何 Android/iOS ARMv7 材料。没有修改 `cpp/` 或测试物料。

## 结论

- 对 MANIFEST 的 114 个入口逐条枚举全部 `DataRefsFrom`，114/114 均有有效函数边界；
  共得到 333 条指令级 data xref、221 个唯一“函数→目标”关系和 133 个唯一目标。
- `.rodata` 的 76 个目标恰好分成 **34 个真实字面量**与 **42 张跳转表**；普通 ASCII
  扫描曾只看到两个 `std::vector` 诊断名，按 UTF-16LE/raw xref 重扫后才得到完整集合。
- 42 张表共有 **915 个 signed-relative 槽**，解析为 **194 个唯一、4-byte 对齐且位于
  owner FDE 内的目标**。分类器、packed count、String/Resource index、整数与 Real
  解码的 case 分组均与当前源码逐项一致。
- 16 个 `.bss` 目标闭合为 lazy native-class ID、两只 AutoRegister、guarded PSBMedia
  singleton、四字段 class-info 状态和两个跨模块运行时对象；没有发现本地缺失的
  psbfile-owned 全局生命周期。
- 因此本轮没有新的生产 `GAP`，继续维持
  `ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`。

## 方法与目标分类

IDAPython 对每个 MANIFEST 函数执行 `idautils.FuncItems` 与
`idautils.DataRefsFrom`，再按 ELF segment 分类；字符串不依赖 IDA 的 ASCII-only
汇总，而是从真实目标 VMA 读取 raw bytes，分别按 UTF-8、UTF-16LE 与 UTF-32LE 检测。
随后对每个 switch instruction 读取 `ida_nalt.get_switch_info`，并用
`ida_xref.calc_switch_cases` 独立恢复 case→destination。

初始 133 个目标的 segment 分布为：`.rodata=76`、`.data.rel.ro=18`、`.bss=16`、
`.got=4`、`.data=1`、`extern=1`；其余 `.text/.plt/无 segment` 项是 AArch64 immediate
被 IDA 记录成 data ref 的分析伪影，不被当作数据对象。`.data.rel.ro` 中的 final
vtable/address-point 已由
[FOLLOWUP_STATIC_OBJECT_VTABLE_SURFACE_2026-08-03.md](FOLLOWUP_STATIC_OBJECT_VTABLE_SURFACE_2026-08-03.md)
固定。

## 34 个字面量

| 组 | 目标内容 | target VMA / 本地对照 |
| --- | --- | --- |
| 模块/类/成员 | `PSBFile.dll`、`PSBFile`、`root`、`load`、`finalize` | `0x14C8D8C/0x14C8DA4/0x14C8DB4/0x14D8FC4/0x14C0C7C`；`main.cpp:14,753-754` 与 ncbind registration 路径一致 |
| dispatch 类型 | `PSBValueClass`、`String`、`Octet`、`Array`、`Dictionary`、`count` | `0x14C8D70/0x14CC602/0x14CE752/0x14C4C24/0x14D61CC/0x1525448`；`main.cpp:137,351-465` 一致 |
| wrapper 类型 | `Function`、`Property` | `0x15072A6/0x14CCEA8`；`NCB_REGISTER_CLASS` 展开路径一致 |
| media | `psb`、`%1: cannot open psbfile`、canonical 空宽字符串 | `0x14C900C/0x14C9014/0x1522752`；`PSBMedia.h:25`、`PSBMedia.cpp:52-109,141` 一致 |
| PSB 诊断 | internal type、bool/long int/double/int conversion、undefined key、storage/octet/load argument | `0x14C8DBE..0x14C9044`；`PSBPackedInternal.h:98,185`、`main.cpp:587,625`、`PSBRawFile.cpp:259,355,450-474` 完全相同，包括 internal-type 尾部换行 |
| ncbind 诊断 | `Invalid instance type.`、`Already registerd class.`、`No Global Dispatch, Regist failed.`、`Multiple constructors.(`、`)`、`No class object.`、`Can't create instance` | `0x14C2514..0x14C2A0A` 与 `0x152AD4E`；`ncbind.hpp:164,206,217,1864,1881,1897` 一致，保留目标原有 `registerd` 拼写 |
| 容器实现 | `vector::reserve`、`vector::_M_emplace_back_aux` | `0x14C2A3F/0x14C908A`；分别由目标内 `std::vector<std::string>` reserve/扩容路径引用，和本地 raw key/vector 实现一致 |

`Resolve@0x59A4B0` 的空 segment 并非凭空构造的新局部常量：目标先经 GOT
`0x1AA35A8 → 0x1AA7EF8`，再取得 `0x1522752` 的 canonical 空 `tjs_char` 缓冲区。
本地 `segment.c_str()` 在空 `ttstr` 上走同一共享空串语义。新门禁同时固定这两个指针槽，
防止以后把该路径误写成 null fallback。

## 42 张跳转表

分布为 `28 × 4-entry`、`1 × 28-entry`、`2 × 30-entry`、`11 × 65-entry`，合计
915 槽。完整 table VMA、owner function、switch instruction、entry count 与 raw table
SHA-256 已写入 `verify_elf_surface.py`；门禁还把每个 signed 32-bit displacement 加回
table VMA，要求 destination 位于 owner FDE。

最关键的 65-tag 分类表在 `GetTypeCategory@0x599554` 恢复为：

| category | raw tag 集合 |
| ---: | --- |
| 0 | `01,23,24,25,26,3F` |
| 1 | `02,03,27,2F,33,37,3B` |
| 2 | `04..0C,28,29,30,31,34,35,38,39,3C,3D` |
| 3 | `1D,1E,1F,2E,41` |
| 4 | `15,16,17,18,2C` |
| 5 | `19,1A,1B,1C,2D` |
| 6 / 7 | `20` / `21` |
| throw | 其余 `01..41` 表内值及范围外值 |

同一分组在 `CreateVariant/IsInstanceOf/EnumMembers/GetCount/PropGet/PropGetByNum/
GetString/GetDictionaryKeys/ContainsDictionaryKey/GetListAt` 的 65-entry specialized clone
中保持一致。其余表进一步固定：

- packed count 为 `0D/0E/0F/10 → 1/2/3/4-byte`；
- String index 为 `15/16/17/18`，Resource index 为 `19/1A/1B/1C`；
- narrow integer 外层接受 `04..08`，内层 `05..08` 解码且 `04/default → 0`；
- wide integer为 `09..0C`；raw double/int outer tables 保留 `02/03`、`1D/1E/1F` 与
  各自 conversion-error default；
- `DecodeName@0x597B1C` 两张表的 case destination 顺序虽然是
  `0D,0F,10,0E`，恢复出的语义仍精确对应 1/3/4/2-byte packed view，而非源码 case
  排列顺序证据。

这些分组逐项对应 `PSBPackedInternal.h:37-207`、`main.cpp:135-653`、
`PSBRawFile.cpp:274-405` 与 `PSBMedia.cpp:162-213`。没有出现本地遗漏 tag、错误 default、
错误诊断或错误宽度。

## 共享全局与生命周期

| target | target data flow | 本地结构 |
| --- | --- | --- |
| `0x1AB5098` | `NativeInstanceSupport@0x596D90` 的零初始化、无锁 lazy `PSBValueClass` ID cache | `main.cpp:461-465` 的函数静态 `valueClassId` |
| `0x1AB50A0` / `0x1AB50C0` | `static_init@0x42CF28` 构造 class AutoRegister 与 pre-register AutoRegister | `NCB_REGISTER_CLASS` + `NCB_PRE_REGIST_CALLBACK` |
| `0x1AB50E8` / `0x1AB50F0` | `preRegister@0x59849C` 的 function-local pointer 与 `__cxa_guard` | `PSBMediaRegistry.cpp:8-11` 的 guarded static singleton |
| `0x1AB50F8..0x1AB5110` | initialized/name/id/classObject 四字段，由 Set/Clear/Regist/Unregist/wrappers 共享 | `ncbClassInfo<PSBFile>` 静态状态 |
| `0x1AB5118` | class-info dynamic-zero-init guard，先于两只 AutoRegister | 与 `.init_array`/static-object gate 一致 |
| `0x1AB8920` | `0x42CF28` 消费的跨模块注册容器状态 | 只作为 ncbind 外部注册基础设施记录；无证据把它命名成 psbfile 私有字段 |
| `0x1C95280` + `pthread_create` | 五个 `std::vector` 路径共享的 libstdc++ 线程/atomicity 运行时判断 | 编译器/标准库实现面，不是 psbfile-owned singleton |

## 目标逻辑摘要（不超过 10 行）

```text
category = exact 0x01..0x41 tag partition; unknown => internal-type throw
count(0D..10) = 1/2/3/4-byte packed unsigned value; default = 0
stringIndex(15..18) and resourceIndex(19..1C) use the same 1/2/3/4-byte family
int32(05..08), int64(09..0C), float(1E), double(1F) preserve distinct defaults
CreateVariant dispatches category first, then performs category-local raw-tag decode
all container consumers repeat the same classifier partition before Array/Dictionary work
Resolve converts an empty path segment through the canonical shared empty tjs_char buffer
registration globals are initialized class-info -> two AutoRegister records -> guarded media singleton
```

## IDB 改善

以 fresh `decompile(0x596D90/0x59849C/0x596E24/0x597F38/0x59A4B0/0x59AEEC)`
和 data-xref 证据为依据，以下五个无名 `.bss` 对象已用 `_guess` 后缀重命名并添加地址注释：

- `PSBValueDispatch_nativeClassId_guess@0x1AB5098`；
- `PSBFile_classAutoRegister_guess@0x1AB50A0`；
- `PSBFile_preRegisterAutoRegister_guess@0x1AB50C0`；
- `PSBMedia_staticInstance_guess@0x1AB50E8`；
- `PSBMedia_staticInstance_guard_guess@0x1AB50F0`。

IDB 已保存。名字只表达当前最强 source-shape candidate；二进制没有保留对应原始变量
identifier，因此没有删除 `_guess`。

## 机械门禁

`verify_elf_surface.py` 新增 literal/switch surface。复现命令：

```bash
python3 analysis/psbfile_function_audit_2026-07-25/verify_elf_surface.py \
  --llvm-dwarfdump /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/llvm-dwarfdump
```

新增输出：

```text
literal_surface=true literals=34 bytes=1268 pointers=2 utf8_utf16=true
switch_surface=true tables=42 entries=915 destinations=194 owner_fdes=true
```

上述 table/destination 拓扑的后续 selector-producer 与完整
`SUB/CMP/B.HI/ADRP/ADD/LDRSW/ADD/BR` 分派链门禁见
[FOLLOWUP_SWITCH_SELECTOR_DISPATCH_SURFACE_2026-08-03.md](FOLLOWUP_SWITCH_SELECTOR_DISPATCH_SURFACE_2026-08-03.md)。
它另固定 42 个 selector 的唯一来源和 335 条链指令，不改变本报告的 915-entry 表面口径。

该门禁不把 raw table hash 冒充源码 token；它固定的是目标确实保留的诊断/注册字面量、
case partition、destination topology 与共享空串指针链。15 个 stripped/O3 精确命名与
factorization 上限继续保留。
