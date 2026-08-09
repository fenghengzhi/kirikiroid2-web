# Android `libkrkr2.so` 内嵌 psbfile 插件函数覆盖 manifest（2026-07-19）

## 证据边界

本 manifest 唯一权威来源是 Android kirikiroid2 `libkrkr2.so` 中内嵌的 psbfile
实现；不查看、引用或从任何外部 `psbfile.dll` 推导源码结构或行为。下文出现的
`L"PSBFile.dll"` 仅是 `libkrkr2.so` 自身静态初始化器中的 NCB 模块注册字面量，
不表示存在另一份可作参考的 DLL 实现。

完整 114-function emitted topology 的无重复树状索引、主要运行调用链与对象生命周期树见
[`psbfile_function_tree_2026-07-25.md`](psbfile_function_tree_2026-07-25.md)。本 manifest
继续作为地址集合与证据摘要的 canonical 来源。

## 2026-07-23 真实 Android oracle 状态

Android 12 / API 31 `userdebug` arm64-v8a AVD 上的正式 ADB/RPC/Frida runner 已闭合
正常 raw、storage、MDF、seed filter 与 cross-container PSBMedia 生命周期：

- raw octet/storage：`0x598268/0x598538 → 0x598708 → 0x598960`，均 `status=ok`；
- MDF：2026-07-23 外部物料快照中的 50,708-byte `.ks.scn` 经两入口均解为
  700,308-byte PSB 并通过 strict refresh；该快照的 142 只天然 MDF 全部可正常解压，
  没有 failure 样本。2026-07-24 后续恢复的当前 `reference/` 再次包含 142 只 `mdf\0`
  `.scn`；只读 zlib 全量复扫为 142/142 成功、0 failure、71 份去重 decoded 内容。对当前
  220 只 PSB/MDF candidate 去重并从 root 递归遍历 packed Array/Dictionary 后，110 份唯一
  内容、23,414,992 个可达节点全部解析成功，tag `0x0B` 仍为 0；
- seed filter：203,302 字节与独立 host xorshift 全量一致，Frida 捕获
  `0x598268 → 0x598708 → 0x6863CC → 0x598960`；
- tag `0x09` 整数边界：天然 `m2logo.mtn@0x36F8` 的
  `09 00 00 00 ff 00` 经公开 `new PSBFile(path).root[...]` 路径得到
  `tvtInteger(4)`/`4278190080`，raw `GetInt@0x599438` 得到相同 X0、W32 为
  `-16777216`，`GetDouble@0x5992E8` 为 `4278190080.0`；Frida 捕获 factory、
  storage load、root NCB wrapper、Dictionary/Array dispatch、CreateVariant→int64 assignment
  与两只 raw getter；
- media：singleton/class/Full TJS 均就绪，`ezsave → encrypted motion → ezsave` 的
  dispatch 替换、旧 borrowed stream metadata、`0x8F7D68` deleting destructor 与 roundtrip
  全部 `status=ok`；Frida 捕获 `0x59849C/0x59993C/0x599E04/0x598538/0x598708/`
  `0x59A330/0x59A0B4/0x59A4B0/0x8F7C74/0x5998C4/0x8F7D68`。

runner 同步纠正了两项被实机证伪的前提：`READY` 不是 Full-TJS-ready，media 必须先
`startupFrom`；`/data/local/tmp` 也不是 app-readable storage，输入现 staging 到 APK 私有
files 目录并继承 app UID。此前的一次性 in-memory adapter smoke 仍不计作 Android 结果。

## 边界纠正

IDA 枚举证明 PSBFile 的业务/NCB 入口覆盖 `0x59641C..0x59B708`，共 111 个
function。旧审计不仅把 `0x59AA84` 错当成终点，还被 IDA 的函数合并漏掉
`0x59A8D8`、`0x59A968` 与 `0x59B14C` 三个独立序言；三者拆分后均有唯一 vtable
data xref。

2026-07-24 又从两侧交叉确认了该起止边界。紧邻起点之前的八只函数
`0x596144/0x596168/0x596170/0x596240/0x596264/0x59626C/0x5963F0/0x596414`
各自唯一的 data xref 槽依次为
`0x1A0B170/0x1A0B180/0x1A0B1A8/0x1A0B290/0x1A0B2A0/0x1A0B2C8/`
`0x1A0B3B0/0x1A0B3C0`。前两槽位于 `noise@0x1A0B078`，中间三槽位于
`generateWhiteNoise@0x1A0B198`，末三槽位于
`gaussianBlur@0x1A0B2B8`；三张连续的 `0x120`-byte NCB vtable 都只由
`layerExImage_NCB_ClassBody@0x594814` 安装。因此这八只函数属于前一只
layerExImage 插件而非 psbfile。末端的
`std::vector<std::string>` 慢路径 `0x59B7E8` 由 PSB
`GetDictionaryKeys@0x598E64` 直接触发，故仍计入；下一只独立序言
`0x59B9C8` 则由下一静态初始化器注册为 `PackinOne.dll` callback；再后的独立代码
布局可枚举到 `TextRenderBase_ncb_registerMembers@0x59BCCC`，均不属于 psbfile。

2026-07-23 又证伪了“下一函数与 PSBFile 无调用关系”的结论：
`GetDictionaryKeys@0x598E64` 在 vector 容量耗尽时由 `0x598FFC` 调 PLT
`0x423250/0x42325C`，最终进入
`std::vector<std::string>::_M_emplace_back_aux<std::string &>@0x59B7E8`。
因此连续主实现簇是 111 个业务/NCB 入口加 1 个本源码触发的 STL 扩容慢路径，
共 **112** 个；最后一只函数的 exclusive end 是 `0x59B9C8`。该地址有独立
`SUB SP,#0x30` 序言，`sub_42CFA0@0x42CFB0..0x42CFC8` 又把它作为字面
`PackinOne.dll` 的 callback 注册；函数体从 `fstat.dll` 起加载一组子插件。因此
`sub_59B9C8@0x59B9C8` 属下一模块，而不是 vector helper 的尾部。IDB 已把此前错并的
`0x59B7E8..0x59BC2C` 拆成 `0x59B7E8..0x59B9C8` 与
`0x59B9C8..0x59BC2C` 两只函数并保存。

下列 A–G 分组穷举连续主实现簇的 112 个相关函数；每组地址数之和为
`42 + 1 + 10 + 19 + 17 + 22 + 1 = 112`。H 组另列 code xref 普查不会覆盖、但由
PSBFile 翻译单元发射的两只静态初始化函数；完整 PSBFile 模块专属 emitted-function 拓扑因此为
`112 + 2 = 114`。

## A. PSBValueDispatch 与 packed dispatch ABI（42）

本地对应 `cpp/plugins/psbfile/PSBDispatch.h` 的完整类声明和
`cpp/plugins/psbfile/main.cpp` 的 37 个 out-of-line `PSBValueDispatch` 定义
（32 个接口槽、构造器、`CreateVariant_guess` 与 3 个私有 helper），以及多继承生成的三组
secondary-base 重复入口。

```text
0x59641C 0x59659C 0x59673C 0x596BC4 0x596C70
0x596D78 0x596D80 0x596D88 0x596D90 0x596E0C 0x596E14 0x596E1C
0x596E24 0x596ED0 0x596ED8 0x596EE0 0x596EE8 0x596EF0 0x596F04
0x596F0C 0x596F38 0x596F3C 0x596F40 0x596F48 0x596F50
0x5975C0 0x5975D0 0x5975D8 0x5975E0 0x5976B4 0x5976BC 0x5976C4
0x597854 0x597A18 0x597A20 0x597A28 0x597A2C 0x597A30 0x597A38
0x597A40 0x597AC0 0x597AD4
```

三组重复入口分别是 `Construct@0x597A30/0x597A38`、native
`Invalidate@0x596F38/0x596F3C` 与 native `Destruct@0x597A28/0x597A2C`：每组前者属于
main vtable 追加槽，后者属于 secondary `iTJSNativeInstance` vtable；它们不是三只析构
wrapper。本地每组共享同一源码方法，正是多继承 thunk/重复入口应还原的 source-level 形状；
六个 IDB 入口已分别补 main/secondary 配对注释并保存。

2026-07-24 fresh decompile/disasm ctor `0x597AD4` 与三个直接 caller
`0x6A931C/0x6AA124/0x6AA424`，确认构造器除 this 外有两个独立实参：X1 指向一只
one-pointer owner holder/slot，X2 直接携带 node；函数依次复制 owner、AddRef、写 node、
置 valid。三处 caller 均分别准备 X1/X2，异常清理也没有按值 raw-node holder 临时量，故
旧文档的“可能只接收一只 PSBRawNode”已被证伪并删除。

2026-08-02 又穷举同源 iOS arm64：dispatch ctor `0x1000EC248` 的三 caller
`0x100101630/0x100102024/0x1001021A4` 分别传 standalone PSBFile 或 raw node 首子对象；
raw ctor/GetRoot 链 `0x1000EEF28 <- 0x1000ED8C8` 又证明该首子对象复用
PSBFile-compatible holder assignment。
因此旧的“X1 可能只是任意 owner-slot reference，精确因子化完全不可恢复”已被更正；本地
现用 `const PSBFile& + node`。仍不可唯一恢复的是共同 holder 原名、raw node member/base
token及 accessor 拼写，不是 holder 生命周期本身。

同一轮又闭合了此前仅记为 `raw-node-like` 的另一只独立入口：单参数 root constructor
arm64 `0x1001263B8` 先读取 `file.owner->header->entries`，再调用上述 holder assignment，
最后写 node，并在 assignment 异常路径只清理 result holder。全 `__text` 恰有七个 caller，
逐个对应 Android `sub_6948E8/sub_695DE8/sub_6A96F8/
sub_6A9ED4/sub_6AAB3C` 的七份 inline clone。本地现以 `PSBRawNode(const PSBFile&)`
恢复该独立边界；`PSBFile::GetRoot` 和 strict/raw/media 路径仍使用证据不同的两参数 ctor。

同轮完整 vtable/layout 复审又确认 main address point `0x1A0B3D8` 的 32 槽与
secondary address point `0x1A0B4E8` 的 3 槽，secondary offset-to-top 为 `-8`；这硬证
`iTJSDispatch2` primary + `iTJSNativeInstance` secondary 的基类顺序。ctor/所有分配点的
对象尺寸均为 `0x30`，语义布局为 `vptr@+0/+8, ref@+16, owner@+24, node@+32,
valid@+40`，与本地 ARM64 record-layout 完全一致。`Release@0x597A40` 在 ref 归零后恢复
两张 derived vptr、释放 owner、再 delete this；dispatch `Invalidate@0x596F0C` 只清 valid，
native Invalidate/Destruct 均为 no-op。未发现可证差异。holder+node 构造在 Android 机器码中
呈 owner-store→AddRef→node-store，但三步无抛且互不别名，优化器可重排；不能据此把
initializer-list 与 constructor-body assignment 中的任一种冒充唯一源码形状。

其中 `0x596F50` 已由完整 tag switch、四 Variant 生命周期、array/dictionary packed
遍历和 callback 三参数调用确认命名为 `PSBValueDispatch_EnumMembers`；不再是 guess。
2026-07-23 的 local→Android Release 对象审计还确认，`GetCount@0x5975E0`、
`PropGetByNum@0x5976C4` 与 `PropGet@0x597854` 的四个直接 count 解码站点已不再产生
`ReadPackedCount_guess` 调用边界；`main.cpp.o` 的 symbol/relocation 表已复核。

2026-07-23 fresh decompile/disasm `PropGet@0x597854` 进一步确认 dictionary
分支 `0x59795C..0x5979C8` 只构造一只 `tTJSNarrowStringHolder`，并把同一
`Buf` 依次交给 name-trie 与 dictionary-offset 查找；成功、失败及异常路径均在离开
该分支时析构 holder。本地已删除此前额外的 `ttstr` → `std::string` 转换链，恢复同一
临时对象数量、数据流和析构边界。

同日更深一轮逐函数复审又纠正三处 Variant 调用边界：`PropGet@0x597854` 的 array
`count` 通过 `operator=(tjs_int32)@0xA0FF28` 以 `SXTW` 落入 Integer，而不是把
`uint32_t` 零扩展为 `tjs_int64`；`CreateVariant_guess@0x59673C` 的 String 与
`EnumMembers@0x596F50` 的 dictionary name 直接调用 narrow
`operator=(const char *)@0xA0FEB4`，不先构造 `ttstr`；Enum 的 flags 先 default-construct，
再以 `tjs_int32(0)` 赋值，随后才构造 callback result。本地现已按这些 helper call、
临时 owner 数量和异常析构顺序复刻。

同一 `EnumMembers` 的 unknown-tag block `0x59748C..0x59749C` 还会在异常 helper
意外返回后把 category 写成 `-1`，再完成四只 Variant 的生命周期并返回
`TJS_E_NOTIMPL`。本地此前错误保留初始化值 `0`，现已纠正；对 PSBFile 范围内其余
`sub_95440C/sub_95458C` continuation 的独立扫描没有发现第二处差异。

2026-07-23 的 fresh decompile 与尾部反汇编还确认，`CreateVariant_guess@0x59673C` 在入口
`0x596764` 保存 destination 到 `X19`，所有正常/throw-helper-return 分支汇入共同尾部，
并在 `0x596B88` 以 `MOV X0,X19` 返回同一 destination 地址。四个直接 caller
`0x5971B4/0x5973A4/0x597848/0x5979B4` 均忽略返回值。本地已从 `void` 恢复为返回
现有 destination pointer；原源码是 pointer 还是 reference 在 ARM64 ABI 上不可区分。

同日纯静态复核（未运行或修改 oracle runner、未使用 Frida）又把 dispatch 三个容易被
Hex-Rays 折叠的尾边界逐指令钉死：`GetCount@0x5975E0` 只有 tag `0x20` 且 member name
为空时才写 out/返回 0，已知非 Array tag 与未知-tag helper-return 均为
`TJS_E_NOTIMPL(-1002)`；`PropGetByNum@0x5976C4` 的已知非 Array tag为
`TJS_E_MEMBERNOTFOUND(-1001)`，越界 miss 只在非 MUSTEXIST 时清 result；
`PropGet@0x597854` 的 unknown-tag helper-return 在 `0x5978EC` 重新检查 MUSTEXIST，
与普通 miss 一样选择清 result/0 或 `-1001`。这些写/清站点均没有 result-null guard。
本地控制流和错误码已经一致，无需修改生产行为。IDB 同步补入
`PSBValueDispatch` ARM64 语义布局、五只函数签名和 vtable/尾路径注释；私有转换 helper
命名已由同源 iOS 的 `__func__="CreateVariant"` 旁证纠正为
`PSBValueDispatch_CreateVariant_guess`，接口槽保留确定的
`EnumMembers/GetCount/PropGetByNum/PropGet` 名称后保存。

2026-07-24 fresh disasm `FindDictionaryValueOffset_guess@0x59659C` 还钉死了二进制可见控制流：
`0x596620..0x596660` 是 `while(lower < upper)`，相等分支从 `0x596640` 也汇入
`0x596674` 的 post-loop `lower >= upper` gate。本地此前用显式 empty check、无限循环和
循环内失败，健康输入结果相同但没有直接表达该可见 CFG；现已改为 while 与循环后 gate。
stripped O3 仍不能排除另一种等价源码写法被优化成同一 CFG，因此这里不把 `while` token
宣称为唯一原始源码。packed tag 默认 count=0、任一 midpoint equal、平行 offsets table、
W32 offset 合成及无 bounds gate均保持原样。

`assign` 的两只复杂临时量也已沿公共 TJS helper 静态闭合。Octet 路径
`0x596B50..0x596B74 → 0xA0E0F4/0xA0FB64/0xA0F778/0xA0F790` 的引用数为
`1 → 2 → 1`；Array/Dictionary child dispatch 路径 `0x5968F8..0x596958` 的引用数为
`1 → 3 → 5 → 3 → 2`，最终两次引用分别属于 result 的 Object 与 ObjThis。
本地 `tTJSVariant(data,size)`、`tTJSVariant(dispatch,dispatch)`、Variant 赋值、临时析构和
显式 construction-reference Release 的对象数量与顺序均一致，不再把这两条列为未闭合
生命周期。IDB 对 stripped binary 中没有字面原名的两只公共 helper 使用
`TJSAllocVariantOctet_guess` / `tTJSVariant_ReleaseContent_guess`，不把本地源码拼写冒充
二进制自身的名字证据。

## B. packed name 反向解码 helper（1）

```text
0x597B1C
```

本地对应共享的 `detail::DecodeName_guess`。fresh `xrefs_to(0x597B1C)` 的四个真实
消费者是 `PSBValueDispatch::EnumMembers@0x596F50`、零 xref 的包装入口
`0x5975C0`、`PSBRawNode::GetDictionaryKeys@0x598E64` 与
`PSBMedia::GetListAt@0x5999F4`；此前写在这里的
`GetDictionaryKey/GetDictionaryEntry` convenience API 已删除。临时
`std::vector<char>`、reverse、`std::string` 构造和析构顺序已复原。2026-07-24 的
`0x597E08..0x597E10` 又以 mangled callee 直接证明最后一步是
`std::string::assign(const char *, size_t)`，包括空 vector 的 null/0 调用；本地已从
iterator-range overload 改为 `data()+size()` 数据流，不再发射额外 range-template 调用面。
二进制能证明 overload 与实参，不能唯一证明 begin pointer 的源码 accessor 拼写。Debug
对象复核只剩 `basic_string::assign(const char*, unsigned long)` undefined symbol；该次
macOS Release checkpoint 的 `psbfile-dll` **577/577**、`motionplayer-dll` **1376/1376**，Wasmtime full guest 链接与
15 Hz timing contract **7/7** 均通过。

## C. PSBFile typed NCB 前段与 factory/root（10）

```text
0x597E98 0x597EA8 0x597EB8 0x597EC8 0x597ED0
0x597F08 0x597F24 0x597F38 0x5980F4 0x5981F8
```

本地对应 `ncbClassInfo<PSBFile>` 的 typed-class 状态读取、一次性安装与清理，
`NCB_REGISTER_CLASS(PSBFile)` 的 Factory→root→load 注册体、`PSBFileFactory`
（含 load 异常清理）以及 guarded root getter。这 10 个入口不构成
`PSBFileConvertor` 独立 emitted 边界的证据；convertor 是否存在或完全内联，仍不能由该
地址组唯一判定。

## D. raw PSBFile / owner / node / packed table（19）

```text
0x598268 0x598538 0x598708 0x598960 0x598A3C 0x598A64 0x598AAC
0x598B3C 0x598B58 0x598C58 0x598D58 0x598E44 0x598E64 0x599174
0x5992E8 0x599438 0x599554 0x5995D8 0x5996E4
```

本地对应 `PSBRawFile.cpp/.h`。`0x599174` 是该源码中 dictionary-key vector 的
`std::vector<std::string>::reserve` 实例化；其余覆盖 load、MDF、owner/header refresh、
按值返回并消费 holder 的 transfer helper `0x598A64`、复用 PSBFile-compatible 首 holder
子对象 special members 的 raw node 生命周期、try/strict/contains、category/int/real/string/resource。2026-07-24 fresh
disasm 确认 `Resolve@0x59A6D0/0x59A6D8` 对同一 refcount 的 identity load/store，结合
strict getter 的 AddRef 和 Android-target AArch64 代码生成，闭合了生产 rvalue 路径的
copy assignment + temporary destruction；`0x598708/0x598A64/0x6A9238` 同形。manifest
仍不声称 stripped O3 二进制可排除不可见位置的其他声明或证明 self guard。

2026-08-02 的 iOS arm64 shared assignment 与 raw ctor 全 caller 穷举进一步排除了本地
旧的 raw-node 独立 Rule-of-Three：raw node 现在持有 `PSBFile`-compatible 首子对象和独立
node，默认/copy/assignment/destruction 复用 `PSBFile`。assignment 的 iOS ctor EH cleanup
还正证该调用边界 potentially throwing，因此 `PSBFile::operator=` 不显式写 `noexcept`；
赋值仍无 self guard。独立单参数 root ctor 的 iOS arm64 七 caller 穷举又恢复了 motionplayer
七处 `PSBRawNode(file)` 源码边界；原始 member/base 与共同 holder 名保留为证据限制。

2026-07-24 对 load/refresh 的异常与高位边界逐指令复审又闭合两处此前遗漏的源码差异。
`PSBFile::Load@0x598340..0x59834C` 的 String 分支不是给输入 VariantString 加引用，而是
`ttstr_c_str@0xA13390 → ttstr_createFromWide@0xA136C0`：先取得输入字符指针，再为局部
`ttstr` 分配并复制独立字符串；`0x598424..0x598490` 的 landing pad 也释放这只新对象。
本地已从 `ttstr(value)` 改为字符指针构造，Debug ARM64 relocation 明确变成
`tTJSVariant::GetString → tTJSString(const tjs_char*)`，不再调用 Variant-sharing ctor。

同轮确认 owner 的 size 字段具有 **signed 64-bit** 语义：standalone
`Refresh@0x5989F0..0x598A2C` 使用 `B.LE/B.LT/CSET GT`，Adopt 内联 refresh
`0x598894..0x598940` 使用 `GT/GE`；而 Adopt 的入口 `size < 0x40`
`@0x598728..0x598730` 仍用 unsigned `B.CC`。因此外层 size 参数保持 `size_t`，只将
owner 内保存的字段与 getter 改为 `int64_t` 语义。完整 `+0x60` consumer 复扫只发现
Refresh 的 X64 比较，以及 decrypt callback `0x6865E8` 的 W32 accessor 长度和
`0x686610` 的 X64 TJS Integer；本地原有两次显式转换恰与它们一致。macOS ARM64 Release
对象的 Refresh 已产生 signed `CCMP ... GE/GT` 与 `CSET GT`。精确 typedef 拼写
（`tjs_int64`/`int64_t`/signed long 等）无法由 stripped O3 唯一恢复，故只声明可证的
signed-64 语义。

随后对本组全部 owner/raw getter 做了另一轮独立 fresh decompile/disasm。owner ctor 在
data-null 时不初始化 header/headerStorage，Refresh 无条件先发布八个 `data+offset` 指针、
验证失败不回滚；Adopt 的 unsigned `<0x40` 短路、temporary-holder replacement、filter
抛出/Refresh-false 后仍保留新 owner，均与本地一致。Storage Adopt 拒绝会泄漏 raw data，
Octet Adopt 拒绝则对 aligned pointer 直接 `operator delete[]`；三处混合释放和 EH 只析构
stream 的边界也再次对齐，未发现新的 `cpp/` 差异。

raw-node 侧 `0x598B58/0x598C58/0x598D58/0x598E44/0x598E64/0x5992E8/
0x599438/0x599554/0x5995D8/0x5996E4` 逐项确认：try miss 不写 out、hit 先 Release
目标再安装/AddRef；strict helper-return 清空 sret；contains 在 tag switch 前构造临时 raw
node；keys 仅在 Dictionary gate 后构造 reusable string。String 的 `0x2C` 和 Resource 的
非资源 tag 都刻意使用 index 0，resource chunkData-null 不写 size；数值截断、packed W32
wrap 与 UXTW/SXTW 也全部一致。2026-07-24 fresh disasm 又纠正了 tag `0x0B`：
`GetDouble`/dispatch `CreateVariant_guess` 读取完整 7 字节且不扩 bit55，但 `GetInt@0x599544` 只执行一次
4 字节 `LDUR W0,[X8,#1]`，不会读取 `node+5..7`；本地现已拆开这两条 fault/data-flow
边界。剩余只有 helper/source spelling
无法由 stripped binary 唯一判定，不构成实现差异。

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
现由四字段 `PsbArray_guess::nBytes` 保存这一完整 W32 相对长度，并始终从原始 table begin
做 UXTW 加址。`PropGetByNum@0x597810..0x597814`
的 entry-table 索引也已从 signed host product 改为 W32 product + UXTW。其最终 node
relative 仍按 `0x59783C..0x597840` 保持 W32 + SXTW；两类扩展没有互相泛化。

`GetInt@0x599438` 对 tag `0x09/0x0A/0x0C` 在函数体内会留下完整 X0；完整 20 个 direct xref
中有 18 个只消费 W0，`0x694CA8/0x694CEC` 两处完全丢弃返回值。fresh callee disasm 又显示
负 8/16 位路径直接 `LDRSB/LDURSH W0; RET`，float/double 路径也以 `FCVTZS W0` 返回；若
接口返回有符号 64 位，这些负值路径在 X0 中会变成错误的正数。四个 direct caller 另以
`SCVTF D0,W0` 明确做 signed-32 转换。因此返回类型语义现已闭合为有符号 32 位，本地
`tjs_int` 签名正确；宽 tag 路径遗留的 X0 高半部不属于 32 位 ABI 结果。两只 getter 的
nested CFG 与最小 codegen 对照支持 decoder-shaped 边界；精确名字和“inline helper vs
显式 nested switch”仍开放。此前据此把 tag `0x0B` 也并入共享 64-bit decoder 是错误的：
fresh `GetInt@0x599438` 逐指令结果在 `0x599544..0x599548` 明确只有低 4 字节 load+return，
不存在可被 wrapper 优化掉的 `node+5..7` 读取。本地因此只让 `GetDouble` 使用完整 56-bit
decoder，`GetInt` 保留独立低字读取。

2026-07-23 的真实 arm64 oracle 进一步闭合天然 tag `0x09`：固定 SHA-256 的
`m2logo.mtn` 节点在公开 TJS Dictionary/Array 路径得到 type 4、payload `4278190080`；
同节点的原始 `GetInt` 返回寄存器 X0 为 `0x00000000FF000000`，正式 W32 结果为
`-16777216`，`GetDouble` 为 `4278190080.0`。这证明 40-bit positive 在惰性 dispatch
赋值时保留 64 位，而 raw getter 按 signed-32 返回；X0 高半部是 incidental register state，
不再保留 `tjs_int64` 候选。

`sub_598D58@0x598D58` 还有一个此前漏记的真实 alias caller：
`sub_695DE8@0x696A84..0x696A90` 同时把 `&v278` 传入 X0 与 X2，调用
`sub_598D58(&v278, "clip", &v278)`。这证明 try-get 命中序列本身没有 self guard，
仍执行 Release-old→重读 source owner→AddRef→写 child；本地直接序列与之相同。它不证明
未知的通用 raw-node assignment special member 是否存在 self guard。

完整 `sub_695DE8` 进一步证明这里有两只不同 raw node。持久 `var_B0` 在 `0x6960D4`
初始化，持续跨越请求探测、枚举、第二遍 decode、packed metadata 与 self-alias clip，
最终在 `0x697380..0x6973A4` 清理；每记录 lookup 临时 `p` 则在 `0x696F90` 构造、
`0x69724C..0x697274` 析构。Android 先按 encounter order 把所有 0x40-byte record 写入
连续 value vector，再发布 `record+0x10` rect 子对象指针并第二遍 decode。每轮先在
`0x696F94` 对上一状态的 `var_B0` 查 `pal`，后在 `0x696FC0..0x697004` 才赋当前 record，
所以 palette gate 是 `record[0]←last`、`record[i]←record[i-1]`，不是当前 record 自查。

record 的已证实拓扑是 raw node + 内嵌 rect payload + 单一 sourceKey；rect tail 保存
parent backpointer、content w/h 和 BGRA，packer 只持 rect 指针，再经 backpointer 取回
record。`sub_698074@0x698074` 只析构 sourceKey/Release raw owner，不释放 BGRA；正常
upload 后现场 free 且不清指针，全透明分支现场 free+清零并仅改 rect 为 2×2。本地已从
`vector<unique_ptr<Record>>`、整 record 继承 rect、双 string 与 destructor-owned BGRA
改成相同 value-vector/composition/lifetime 数据流。`ImagePacker::pack@0xA6E50C` 的 bool
结果被 Android caller 忽略，第二次 cache lookup 也无 end guard；本地同步恢复。Android
`pack(n=0)` 仍追加 `_rect2D@0xA6DAB0` 返回的 0×0 bin，caller 在
`0x6968C0..0x696C20` 仍创建 0×0 texture、经过空 rect loop 并无条件 Release；本地已删除
原有的零尺寸 bin skip，恢复这条调用链与 construction-reference 生命周期。Android
逐 record 执行 metadata→subrect Update→free；本地 software texture 的既有 rect 接口
现已实现 left/top 目标偏移，atlas loop 同步恢复逐记录顺序；由整页 batch 引入的已知
时序差异已在源码结构上消除，真实 Android atlas runtime 边界仍待设备 oracle 验证。
是否把 atlas build 写成独立 inline helper 不能由优化后二进制唯一判定，不再把 helper
源码拼写冒充成未闭合生命周期。

后续 fresh 指令复核又闭合了同一 source decode 的 allocator/整数边界。Win
`0x694E44..0x694FBC` 使用 W32 pitch/bytes、`TJSAlignedAlloc(bytes,4)`、unsupported
分支先 free、正常 CreateTexture 后立即 free，再进入 map；CreateTexture null 也没有
guard，slot 操作后无条件 Release。resource size 槽未初始化，RGBA count 是 S32 `/4`，
A8L8 是 signed W32 do-loop，正奇数长度保留末次越界读。KRKR `0x696FA8..0x697248`
以 S64 dimension product、低 W32 allocator/count 和低 S32 alpha gate 混用，BGRA/index
均走 aligned family；upload `0x696BE0..0x696C0C` free 后不清字段。两处 texture call 的
最后参数均是 literal 1。本地已逐项恢复，并用显式 W32 模运算避免 host `size_t` 扩宽、
implementation-defined high-bit signed cast 和 signed-overflow 改写损坏输入边界；palette
vector 仍保留普通 allocator。software renderer 的 nonnull-pixel borrowed view 与
Android/OGL upload-copy 合同仍单独记录为 backend 差异，不反向改变插件生命周期。

请求 icon lookup 的另一项已闭合差异仍成立：Android `0x69612C..0x696154` 在测试返回位
之前先 Release strict icon-root 临时量；本地用显式 scope 保存 bool，确保 temporary owner
在 `if(!found)` 之前析构。

2026-07-23 逐指令复审还确认 `PSBFile::Load@0x598268` 的 invalid-type throw helper
若意外返回，`0x5983B0` 显式返回 true；本地此前会继续进入 `AsOctet()`，现已纠正。
同时，`CreateVariant_guess@0x59673C`、`getResource_guess@0x596C70`、
`PSBRawNode::GetResource@0x5996E4` 与 `PSBMedia::GetResourceData@0x59A0B4`
都先取得 chunk-offset view、再取得 chunk-length view；索引 entry 时再先读 length、后读
offset。三个 helper 原本已符合这两层顺序，只有 `assign` 的 entry 访问相反；现已纠正，
使损坏 table 下的第一个 entry 读取边界也与二进制一致。

## E. media pre-register、singleton 与 psb: gateway（17）

```text
0x59849C 0x5997F0 0x599830 0x599878 0x599888 0x5998A8
0x5998BC 0x5998C0 0x5998C4 0x59993C 0x5999F4 0x599DD8
0x599E04 0x59A0B4 0x59A284 0x59A330 0x59A4B0
```

本地对应 `PSBMediaRegistry.cpp` 与 `PSBMedia.cpp/.h`：process-lifetime singleton、
非原子 refcount、normalize nullsub、exists/open/list/local-name、container replacement、
borrowed resource pointer 与 raw-node Resolve 生命周期。
2026-07-23 的纯静态 fresh decompile 加上 `off_1A0B510` vtable 槽序再次逐项确认该 17-entry
归属；在不运行 oracle runner、不使用 Frida 的本轮复审中，本地字段顺序、析构逆序、
adaptor-null 泄漏/缓存更新、borrowed stream 和 delayed Resolve out 均未发现新偏差。
IDB 已把除两只既有 nullsub 外的 media/辅助边界按证据强度命名为 `_guess`，补入 ARM64
语义类型与函数头证据注释后保存；后缀明确表示 vtable/调用链映射不是二进制字面原名。
2026-07-23 的对象审计确认 `Resolve@0x59A4B0` 已直接建立 root raw node，
`GetResourceData@0x59A0B4` 已直接展开 resource/chunk table 解码；`PSBMedia.cpp.o` 不再
引用 `PSBFile::GetRoot@0x598A3C` 或 `PSBRawNode::GetResource@0x5996E4`。
`0x59A4B0` 的正常返回位于 `0x59A7D8`，异常 landing pads 延伸到 `0x59A8D4`；
其真实函数边界在 `0x59A8D8`，后者是下一只独立自动注册入口。

2026-07-24 的生命周期复核进一步确认，循环内直接表达式
`current = current.GetDictionaryValueStrict(key)` 依赖 copy assignment 与返回临时析构，
不是本地此前手写的 move assignment。两组猜测性 holder move special members已删除；
zero-ref 删除边界继续由原始 AddRef/Release 相消自然产生。
该次本地验证为 macOS Release `psbfile-dll` **577/577**（10 cases）、
`motionplayer-dll` **1376/1376**（21 cases）；Wasmtime guest 链接成功且 Debug Wasm 不再
发射两个 holder 的 move special-member 符号，15 Hz timing contract **7/7** 通过。

2026-08-02 holder-factorization 改造后的 macOS Release 回归为 `psbfile-dll`
**583/583**（10 cases）、`motionplayer-dll` **1386/1386**（21 cases）、
`motionplayer-ttstr-hash-test` **109/109**（24 cases）；这些现有 fixture 只承担非回归守护，
结构结论仍由 Android 与双 iOS 静态证据承担。`cmake --build out/web/debug -j 8` 完整
链接成功，`cmake --build out/wasmtime/debug --target krkr2_wasmtime_guest -j 8` 也重新生成
guest wasm；只有既有编译器/Emscripten 警告。

现有加密 motion PSB 已证明可作为第二只有效 raw container；测试通过
`ezsave.pimg → encrypted motion PSB → ezsave.pimg` 覆盖
`EnsureContainer@0x599E04` 的成功 replacement、旧 `tTVPMemoryStream` 自身 metadata/析构
在 replacement 后仍可用，以及切回首容器。stream 的 borrowed/non-retaining 性质来自
ctor/dtor 反编译证据，测试不读取悬挂 block。此前“缺少第二容器、无法覆盖 replacement”的
结论已被证伪并删除；该用例的本地守护和后续真实 Android oracle 现均已通过。

2026-07-23 已补 `run_psbfile_load_adb.py --media-lifecycle`：真实 APK harness 会用相同
两只 tracked 资产执行 `ezsave → raw motion → ezsave`，观察 PSBMedia 的 Android ABI 状态，
并在 replacement 后只读取旧 stream 自身 metadata，再调用 deleting destructor
`0x8F7D68`；不会解引用悬挂 Block。Frida target 同步覆盖 `0x59993C/0x599E04/
0x59A0B4/0x59A330/0x59A4B0/0x8F7C74/0x8F7D68`。开发时仅用一次性、未入库的
in-memory engine test double 直接 smoke 过 host-side adapter 状态机；它绕过 runner，
没有模拟或验证 ADB 命令、TCP/RPC transport，也没有启动 Android 或执行 `libkrkr2.so`，
因此不记作可复现测试结果。后续正式 runner 已在真实 arm64 AVD 上取得上述二进制实测，
两类结果没有混记。
fresh IDA 也已把原来合并的 `0x8F7D04..0x8F7DC0` 拆为 complete destructor
`0x8F7D04` 与独立 deleting destructor `0x8F7D68`，补类型/注释并保存 IDB。

对最初两只资产的 dictionary listing 可达性做了严格交叉核实：`ezsave.pimg` root 的 11 个直属
子节点只有八个 Resource、两个 Integer 与一个 Array，没有 Dictionary；未过滤 motion root
是 Resource `0x1A`。`Resolve@0x59A4B0` 不允许直接返回 root，每个 segment 又必须通过
`ContainsDictionaryKey@0x5995D8`，后者对 Array/Resource 直接 false。因此这两只 tracked
资产确实不能触达 `GetListAt` 的 dictionary branch。恢复为普通 ignored 目录后的
`reference/` 另有现成 `autoskip.psb`，其 `source/main/icon` 是按纯 Dictionary key 可达的
三键 Dictionary；真实 Android oracle 已按顺序收到 `arrow/auto/skip`，Frida 链命中
`0x59849C → 0x5999F4 → 0x599E04 → 0x598538 → 0x598708 → 0x59A330 → 0x59A4B0`。
因此旧的“现有物料不能闭合”结论已被资料恢复后的完整扫描证伪并纠正。

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
| `0x59B268` | typed factory wrapper deleting destructor | `ncbNativeClassFactory<PSBFile>` |
| `0x59B28C` | root property getter入口与 native-instance检查 | `Property("root", ...)` |
| `0x59B378` | root property只读 setter边界 | `Property(..., setter=0)` |
| `0x59B460` | root property wrapper deleting destructor | `Property("root", ...)` |
| `0x59B484` | root property specialization `GetFlags()` 返回 0 | `Property("root", ...)` |
| `0x59B48C` | 调 native root getter并 CopyRef 到结果 | `PSBFile::GetRootDispatch` binding |
| `0x59B570` | `load` method argc/native-instance/结果 wrapper | `Method("load", &PSBFile::Load)` |
| `0x59B6DC` | load wrapper deleting destructor | method wrapper |
| `0x59B700` | load method specialization `GetFlags()` 返回 0 | method wrapper |
| `0x59B708` | 首参数 Variant 按值转换链 | `PSBFile::Load(tTJSVariant)` binding |

2026-07-24 的 Android-only vtable 边界复核纠正了这里此前的两项归属错误：factory
vtable 以 `0x1A0B5C0/0x1A0B5C8` 的 `offset-to-top/typeinfo` 零前缀开始，address point
为 `0x1A0B5D0`，其中 `FuncCall@0x1A0B5E0 → 0x59B14C`，deleting destructor
`@0x1A0B6C8 → 0x59B268`。下一张 root-property vtable 的零前缀位于
`0x1A0B6E0/0x1A0B6E8`，address point 为 `0x1A0B6F0`，其中
`PropGet@0x1A0B710 → 0x59B28C`、`PropSet@0x1A0B720 → 0x59B378`、deleting
destructor `@0x1A0B7E8 → 0x59B460`、`GetFlags@0x1A0B7F8 → 0x59B484`。
因此旧表把 `0x59B268` 错写成 root-property 析构、又把 `0x59B460` 错写成独立 getter
invoker 析构，现已就地纠正；`0x59B484` 也不再模糊写成“invoker base”。load-method
vtable 同形地由 `0x1A0B800/0x1A0B808` 零前缀、`0x1A0B810` address point、
`FuncCall@0x1A0B820 → 0x59B570`、deleting destructor `@0x1A0B908 → 0x59B6DC`
与 `GetFlags@0x1A0B918 → 0x59B700` 闭合。

同轮把本组及前段 class-info 中仍为 `sub_*` 的 30 个入口按上述 vtable、注册字面量与
完整调用链补为带 `_guess` 的 IDB 语义名并保存；`_guess` 明确表示这些不是二进制保留的
原始 C++ 符号。尤其 `0x59AA84/0x59AD84/0x59AEEC` 分别标成
`RegistBegin/RegistEnd/RegistItem` 语义，`0x59B268/0x59B460` 分别标成 factory/property
deleting destructor，避免错误的 wrapper 层级继续传播。

尾链逐函数 fresh decompile 未发现本地手写简化：`Factory/Property/Method` 声明通过
仓库同一 ncbind 模板生成 holder、注册状态、属性/方法 wrapper 和参数生命周期。

2026-07-24 又逐一复审 factory callback/raw wrapper、root property、load method、adaptor
及注册/反注册的异常 landing pads，仍未发现差异。Android factory 在 `Load` 抛出时删除已
发布 holder、保留悬挂 result slot 后重抛；root result 被忽略时 fresh dispatch 泄漏，空
holder 且需要 result 时 convertor 会 null-Release；raw factory callback 返回错误也不清理
已写 native pointer。`load` 的首参数确实经历多级按值 Variant copy，异常按构造阶段逆序
析构；注册 body 抛出仍执行 End，而 End 的 PropSet 抛出只析构 class Variant、不释放 raw
global。这些边界均由现有 ncbind 模板自然生成。当前 `PSBFileConvertor` 声明没有 emitted
符号或调用，但 stripped O3 不能证明原源码不存在一个未实例化模板；因此既不删除它，也不
把 Resolve/load 等生产链改走它。

2026-07-24 的静态数据拓扑复扫又把 code-xref 之外的两只构造入口纳入范围。
`.init_array@0x19EA088` 先指向 `0x42CEF8`：guard `0x1AB5118` 首次进入时依次把
`ncbClassInfo<PSBFile>` 的 `initialized/name/id/class-object` 四字段
`0x1AB50F8/0x1AB5100/0x1AB5108/0x1AB5110` 清为 `false/null/0/null`，再把 guard 置 1；
这与 `ncbClassInfo<T>::info` 的默认构造函数逐字段一致。`.init_array@0x19EA090` 再指向
`0x42CF28`，后者不调用额外 helper，而是在 `0x1AB50A0..0x1AB50E7` 原位建立两只对象
并插入全局 autoreg 链。第一只为
`{vptr=off_1A0B578, module=L"PSBFile.dll", next, className=L"PSBFile"}`；
第二只为 `{vptr=off_19FD8E8, module=L"PSBFile.dll", next,
preRegist=0x59849C, postRegist=null}`。`off_1A0B578` 的两个虚槽正是
`Regist@0x59A8D8/Unregist@0x59A968`；callback vtable 的两个槽落在通用 ncbind
实现。该构造函数没有 PSB 专属 `__cxa_atexit`，也没有对应 `.fini_array` 入口。

PSB 专属可写静态状态只有三组：`dword_1AB5098` 是
`PSBValueDispatch::NativeInstanceSupport@0x596D90` 的 lazy `PSBValueClass` id；
`qword_1AB50E8 + guard@0x1AB50F0` 是 `0x59849C` 的函数局部
`PSBMedia*`；`0x1AB50F8/0x1AB5100/0x1AB5108/0x1AB5110 + guard@0x1AB5118` 依次承载
`ncbClassInfo<PSBFile>` 的 initialized/name/id/class-object 状态，并由
`0x597E98..0x597F08`、`0x59A968` 和 `0x59AA84` 读写。跳转表、GOT、全引擎共享的
空字符串与 libstdc++/pthread 状态另有大量非 PSB xref，不属于插件自有静态对象。

本地 object 对应发射
`ncbNativeClassAutoRegister_PSBFile`、
`ncbCallbackAutoRegister_PreRegist_initPsbFile_0`、函数局部 `valueClassId`、
`ncbClassInfo<PSBFile>::_info`/guard，以及 `initPsbFile()::psbMedia`/guard。macOS Release
`main.cpp.o` 的 `__mod_init_func` 也保持 class-info initializer 在
`__GLOBAL__sub_I_main.cpp` 之前的次序；Release
把两组 BSS 分别合并为 `main.cpp.o` 与 `PSBMediaRegistry.cpp.o` 的
`__MergedGlobals`，但 relocation 仍完整指向同一 vtable、`initPsbFile`、
`__cxa_guard_*` 与 `TVPRegisterStorageMedia`。这是优化后的符号合并，不是源级对象缺失；
Android 与本地没有可证的额外/缺失 static、guard 或注册生命周期差异。

2026-07-24 又完成 PSB 全区间的异常表、RTTI 与 cleanup 拓扑审计。唯一显式源码级捕获
仍是 `PSBFileFactory@0x5980F4`：`__cxa_begin_catch@0x5981A8` →
`__cxa_rethrow@0x5981DC` → `__cxa_end_catch@0x5981E8`，对应本地
`catch (...) { delete file; throw; }`。其余异常边都是 Variant/string/vector/raw-node
析构、guard abort 后 `_Unwind_Resume` 的 cleanup-only landing pad；LSDA 没有正数 RTTI
类型条目，解码中的 `-1/-2` 是异常表特殊标记，不能冒充命名异常类型。

vtable 前缀也没有可用 typeinfo：dispatch 主组 `0x1A0B3C8/0x1A0B3D0`、次级组
`0x1A0B4D8=-8/0x1A0B4E0=0`，media 组 `0x1A0B500/0x1A0B508`，以及 NCB 组
`0x1A0B568/0x1A0B588/0x1A0B5C0/0x1A0B6E0/0x1A0B800` 的 typeinfo 槽均为零；
这能证明二进制里没有可用运行时 typeinfo，不能单独反推精确编译开关。`PSBMedia`
完整/删除析构 `0x5997F0/0x599830` 仍按 `_container → _file → delete` 顺序；
`ncbInstanceAdaptor<PSBFile>@0x59ABD8/0x59AC0C/0x59AC7C/0x59AD08`
仍是 `{vptr, raw instance pointer, sticky}`。guarded media pointer `0x1AB50E8` 没有
PSB 专属 `__cxa_atexit`/`.fini_array` 注册。本地对应的直接多继承、显式 factory catch、
adaptor 裸指针/sticky 所有权和 process-lifetime media 指针均无需修改。

## G. GetDictionaryKeys 的 vector 扩容慢路径（1）

```text
0x59B7E8
```

该函数是
`std::vector<std::string>::_M_emplace_back_aux<std::string &>`。直接 xref 只落在
PLT thunk，但 `GetDictionaryKeys@0x598E64` 的 `0x598FF4..0x598FFC` 明确在
`finish == end_of_storage` 时调用该 thunk；非满容量分支则在 `0x598FD0..0x598FF0`
内联构造元素。本地 `result.emplace_back(key)` 保留同一 reusable lvalue string、
vector 容器和快/慢路径语义；Android 旧 libstdc++ 的 string 呈 COW refcount 路径，
本地 libc++ 的 `std::string` 并非 COW。这里对齐 source-level copy token、元素构造与
临时对象边界，不宣称跨标准库复刻 COW 内部布局或模板实体机器地址。

## H. PSBFile 静态初始化入口（2）

```text
0x42CEF8 0x42CF28
```

两者分别由 `.init_array@0x19EA088` 与 `.init_array@0x19EA090` 引用。前者是
`ncbClassInfo<PSBFile>::_info` 的 guarded 默认构造；后者构造 class/callback 两只 autoreg
对象并把 `initPsbFile@0x59849C` 写入 pre-register 槽。完整 `.init_array` 扫描确认前一项
`0x42CE6C` 属上一个模块、后一项 `0x42CFA0` 属 `PackinOne.dll`，没有第三只 PSB 专属初始化
入口；`.fini_array`、`__cxa_atexit` 与 PSB 函数簇也没有额外 PSB 析构入口。本地分别由
`ncbClassInfo<PSBFile>::_info` 静态对象、`NCB_REGISTER_CLASS(PSBFile)` 与
`NCB_PRE_REGIST_CALLBACK(initPsbFile)` 发射相同生命周期拓扑，无需修改生产代码。

## 覆盖结论

连续主实现簇的 112 个函数，加上两只静态初始化入口，共 **114 个** Android PSBFile 模块专属
emitted functions，均已归属于生产源码、明确的 NCB/iTJS ABI wrapper、本源码触发的 STL
实例化或静态对象生命周期；没有未归属业务入口。该 manifest 只证明 **Android→local** 的入口归属完整，
不能反向证明 **local→Android** 没有额外抽取边界。另行完成的 local→Android Release
对象审计已消除三类已确认额外边界：dispatch 三函数四站点的 count-helper 调用、
`PSBMedia::Resolve→PSBFile::GetRoot`，以及
`PSBMedia::GetResourceData→PSBRawNode::GetResource`。这不能反向证明原始源码没有
等价 inline helper。最新 local→Android Release 对象符号普查也已逐项映射所有 strong PSB
business functions；本地剩余额外符号只属于 ABI alias、weak COMDAT inline 析构和 ncbind
模板实例，stripped O3 不能据此断言原源码不存在等价 inline/template 声明。当前 raw owner/node 的若干小方法只有
上层调用点内联行为证据、没有独立 Android 入口；其名字与是否为原始 inline helper 仍不能
仅由本地源码宣称。manifest 也不替代损坏输入的 Android runtime oracle；MDF zlib failure、
filter 后 offset failure、损坏 packed table、tag `0x0B` 与 >4 GiB storage 仍按主分析记录
为缺少天然 fixture 的验证缺口。CreateAdaptor-null 已用现有 `ezsave.pimg`、临时
class-object 单槽清零及恢复后同名重试在真实 Android ARM64 闭合；dictionary listing 已由
恢复后的天然 `autoskip.psb` 在真实 Android 闭合。
成功跨-container replacement 与旧 stream metadata/析构已同时由本地守护和真实 Android
ADB/RPC/Frida oracle 覆盖。borrowed/non-retaining 的“stream 不保活 owner”性质仍由
反编译证据证明；运行时只观察 stream 自身 metadata，刻意不解引用 replacement 后的悬挂
Block。一次性 adapter smoke 仍不计入二进制实测。

本轮又用现成 `ezsave.pimg` 加固了 child-dispatch owner 生命周期：先销毁
`PSBFile`，再清空 root closure，Array 仍可 `GetCount/PropGetByNum`；随后清空 Array
closure，返回的 Dictionary 仍可 `PropGet("name")`。这直接守护
`CreateVariant_guess@0x59673C` 已反编译确认的 Object/ObjThis 双引用与 raw-owner 独立保活链，不构造
新 fixture，也不冒充 Android runtime oracle。macOS Release `psbfile-dll` 现为
**577/577**（10 cases）。此前同一 checkpoint 的
`motionplayer-dll` 为 **1376/1376**（21 cases），`motionplayer-ttstr-hash-test` 为
**100/100**（22 cases）；Web Debug 最终链接与显式 Wasmtime
`krkr2_wasmtime_guest` 目标通过。motion playback runner 在当轮尚未进入 guest，当时的
checkout 缺少 `reference/xp3/logo_test_oracle.xp3`。2026-07-23 曾在外部 ignored
`reference/` 快照恢复该现成文件（SHA-256
`9d9f336dcccb5370a433f87b81054c74ff5099e2bd18a499e0f1ccf769158a7f`），只闭合了当时的输入
可用性，不能倒推当轮 runner 已实际进入 guest。后续恢复后的当前 `reference/` 已重新包含
该物料；提交 `9d731e0b21498d47886a0993a0bac03759b82bc9` 对应的独立 Differential run
`30084689336` 已实际完成 Wasmtime guest、Android oracle 与最终 trace/PNG hash compare，
workflow conclusion 为 success。

最新生产代码 checkpoint `bfb0244cd03712e67dbc4c2009f3c894d2f53f68` 的独立
Differential run `30086865716` 同样为 success：`build-legacy-harness`、`wasmtime`、
`adb-frida`、`motion-playback-compare` 四个 job 全部通过。该 checkpoint 的 macOS Release
非回归为 `psbfile-dll` **577/577**、`motionplayer-dll` **1374/1374**；上文的 1376/1376
均保留为各自历史 checkpoint 的原始计数，不代表当前回归。
