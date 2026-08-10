# psbfile 四参考二进制审计（2026-08-10）

本文只记录本轮通过原生 `mcp__idalib__*` 对
`reference/binaries/` 四个 IDB 重新取得的证据，不继承已删除的
`libkrkr2.so` 单文件结论。

## 参考目标

| 简称 | 参考二进制 | 配套 IDB / `database` |
| --- | --- | --- |
| Android arm64 | `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | `Kirikiroid2_1.3.9_Android_arm64-v8a.so.i64` / `android_arm64` |
| Android armv7 | `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | `Kirikiroid2_1.3.9_Android_armabi-v7a.so.i64` / `android_armv7` |
| iOS arm64 | `Kirikiroid2_1.3.9_iOS_arm64` | `Kirikiroid2_1.3.9_iOS_arm64.i64` / `ios_arm64` |
| iOS armv7 | `Kirikiroid2_1.3.9_iOS_armv7` | `Kirikiroid2_1.3.9_iOS_armv7.i64` / `ios_armv7` |

## 已确认函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | --- | --- | --- | --- |
| 静态注册初始化 | `sub_42D308` | `sub_2FF764` | `InitFunc_33` | `InitFunc_33` |
| 预注册回调 | `sub_59887C` | `sub_4DD018` | `sub_1000ED3C8` | `sub_E9760` |
| NCB 类成员注册 | `sub_598318` | `sub_4DCD5C` | `sub_1000ECFC8` | `sub_E9384` |
| `PSBFile` factory | `sub_5984D4` | `sub_4DCDD8` | `sub_1000ED08C` | `sub_E940C` |
| `root` getter | `sub_5985D8` | `sub_4DCE74` | `sub_1000ED148` | `sub_E9520` |
| `load` | `sub_598648` | `sub_4DCEB8` | `sub_1000ED1B4` | `sub_E955C` |
| storage load | `sub_598918` | `sub_4DD0A0` | `sub_1000ED468` | `sub_E9874` |
| MDF decode | 内联于 load/storage load | `sub_4DD17C` | `sub_1000ED5B4` | `sub_E99D0` |
| Adopt/owner replacement | `sub_598AE8` | `sub_4DD200` | `sub_1000ED654` | `sub_E9A28` |
| owner Refresh | `sub_598D40` | `sub_4DD2A0` | `sub_1000ED7E8` | `sub_E9B36` |
| owner destructor body | `sub_598F1C` | `sub_4DD38C` | `sub_1000ED920` | `sub_E9C00` |
| shared owner Release | 调用点内联 | `sub_4DE564` | `sub_1000EEEFC` | `sub_EB014` |
| raw `GetRoot` | `sub_598E1C` | `sub_4DD33A` | `sub_1000ED8C8` | `sub_E9BD0` |
| raw `Transfer` | `sub_598E44` | `sub_4DD350` | `sub_1000ED8E4` | `sub_E9BE2` |

## 已确认共同语义

### NCB factory

四份实现均：

1. 分配一个只含 owner 指针的空 `PSBFile`；
2. 在调用 `load` 前先把指针写入结果槽；
3. 仅当参数数目至少为一时，复制第一个 variant 到临时对象并调用
   `load`；
4. 正常返回零；
5. 异常时析构已发布的对象并原样重抛，但不清空结果槽。

异常清理分别表现为 AArch64/ARM EH 尾块或 iOS armv7 SJLJ landing
pad，编码不同但对象生命周期相同。

### root getter

四份实现均先检查 owner。空 holder 返回 null；非空时分配 dispatch，
保存 owner 和 root entries，并对 owner 增加一次引用。32 位 dispatch
为 0x18 字节，64 位为 0x30 字节。

### load

四份实现均只接受 string 与 octet：

- string：复制成独立字符串，调用 storage load；失败时抛出
  `cannot open psb file : %1`；
- octet：先尝试 MDF 解压；不适用或解压失败时复制原 octet；随后
  Adopt；
- 其他类型：抛出 `invalid argument for PSBFile.load()`；
- octet Adopt 失败：释放候选缓冲区，抛出
  `octet: invalid psb file.`。

异常辅助函数若意外返回，四份二进制都保留相同边界返回值：参数类型
错误路径返回 true，octet 无效路径返回 false。

### MDF

共同格式为：

- 最小输入长度 0x0b；
- magic `0x0066646d`；
- 偏移 4 处为目标长度；
- 从偏移 8 开始调用 zlib `uncompress`；
- 解压失败释放目标缓冲区并返回 null；
- 成功时用 zlib 实际输出长度回写 size。

失败释放并不是正常的 aligned-dealloc 配对。Android arm64 的
`sub_A0C748@0xA0C748`、Android armv7 的 `sub_75F5E8@0x75F5E8`、iOS
arm64 的 `sub_10025836C@0x10025836C` 和 iOS armv7 的
`sub_259720@0x259720` 都先用 `operator new[]` 分配更大的块，把原始指针
写在对齐地址前方，再返回 16 字节对齐的内部指针；但 MDF 解压失败时，四份
实现均直接对这个内部指针调用 `operator delete[]`。本地实现故意保留该
allocator mismatch，而不是“修正”为 `TJSAlignedDealloc`。

Android arm64 将算法内联进两个调用点；其余三份保留共享 helper。

### Adopt、owner 和 Refresh

共同顺序为：

1. 要求 size 至少 0x40 且 magic 为 `0x00425350`；
2. 新 owner 初始引用计数为零，并建立内嵌 header view；
3. 临时 holder 加引用，赋值给目标（先 Release 旧 owner，再 AddRef 新
   owner），随后析构临时 holder；净引用数为一；
4. 存在 filter 时先调用 filter，再以 validate=true 重建 header view；
5. 无 filter 时返回 true。

owner 布局的交叉 ABI 证据：

- 两份 64 位二进制的 owner 为 0x68 字节，size 字段位于 +0x60，宽
  8 字节；
- 两份 32 位二进制的 owner 为 0x38 字节，size 字段位于 +0x34，宽
  4 字节；
- 四份 Refresh 都使用有符号 GT/GE 条件码比较 size 与八个
  `uint32_t` offset。

因此源码字段应是指针宽度有符号整数，而不是固定 `int64_t`。本轮
已将 `PSBRawOwner::size_` 与 getter 改为 `std::intptr_t`。

本轮进一步用汇编条件码确认比较宽度和符号性：Android arm64
`sub_598D40@0x598D40` 使用 `B.LE/B.LT/CSET GT`，Android armv7
`sub_4DD2A0@0x4DD2A0` 使用 `BGE/BGT/MOVLT`，iOS arm64
`sub_1000ED7E8@0x1000ED7E8` 使用 `B.GE/B.GT/CSET LT`，iOS armv7
`sub_E9B36@0xE9B36` 使用 `BGE/BGT/MOVLT`。偏移读取仍是 32 位原始值并用于
指针加法；只有验证比较转换到 owner 的有符号 size 宽度。本地实现因此在每个
比较点显式转成 `intptr_t`，避免 wasm32 的 usual arithmetic conversions 把
`intptr_t` 与 `uint32_t` 比较错误地改成无符号语义。

raw `GetRoot` 在四份中都复制 holder、增加 owner 引用，并另外保存 root entries
指针。raw `Transfer` 则复制 owner/entries 到结果，先对 owner AddRef，再按相同
Release 路径清空源 holder；目标最终持有一份引用，源节点变为 owner/entries 都为空。
这不是裸指针 move，也不能由默认成员赋值代替。

### storage load 的边界泄漏

四份实现都先分配完整文件缓冲区，再调用 stream read。对应异常清理
只销毁 stream 和已经构造的临时字符串，不释放该缓冲区：

- Android arm64：`sub_598918` 尾部 EH；
- Android armv7：`0x4DD154..0x4DD170` 未归属尾块；
- iOS arm64：`sub_1000ED57C`；
- iOS armv7：SJLJ handler `sub_E9986`。

此外，正常执行到 Adopt 但 Adopt 返回 false 时，四份实现也只关闭
stream 并返回 false，不释放候选缓冲区。源码保留这两个泄漏边界。

## packed array、name trie 与 dictionary

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | --- | --- | --- | --- |
| name trie 查找 | `sub_5967FC` | `sub_4DBB4C` | `sub_1000EB74C` | `sub_E817C` |
| dictionary value offset 查找 | `sub_59697C` | `sub_4DBCA8` | `sub_1000EB8B0` | `sub_E825C` |
| name 解码 | `sub_597EFC` | `sub_4DCB88` | `sub_1000ECD6C` | `sub_E91A4` |
| strict dictionary value | `sub_599038` | `sub_4DD49C` | `sub_1000EDA48` | `sub_E9D10` |
| non-throwing dictionary value | `sub_599138` | `sub_4DD544` | `sub_1000EDB08` | `sub_E9E1C` |
| raw-node `IsValid` 独立边界 | `sub_599224` | `sub_4DD5C4` | 未保留 | 未保留 |
| dictionary keys | `sub_599244` | `sub_4DD5D8` | `sub_1000EDB8C` | `sub_E9E70` |

四份 name trie 实现都把前两个连续 packed array 当作 double-array trie：
以 `charset[0] + byte` 建立首状态，要求 `namesData[state] == parent`，遇到
NUL 时返回 `charset[state]`，否则以当前 state 为 parent 继续推进。所有数组
索引和乘法都保持 `uint32_t` 语义。

dictionary offset 查找在四份中都是相同的 `lower < upper` 二分搜索。命中分支
跳出循环后仍经过 `lower >= upper` 失败门；成功时解析紧随 keys 的 offsets 表，
最终偏移为 `keys.nBytes + offsets.nBytes + offsets[middle]`。

name 解码在四份中都解析 names 区域中的三个连续 packed array，从
`namesData[nameIndexes[nameIndex]]` 开始沿 parent 链回溯，把
`node - charset[parent]` 压入 `vector<char>`，反转后通过明确长度的
`string::assign(data, size)` 写入结果。Android 的 libstdc++ 与 iOS 的 libc++
展开不同，算法和空字符串边界相同。

non-throwing dictionary value 在 miss 时不触碰输出；命中时先捕获 child，释放
输出旧 owner，再复制并 AddRef 当前 owner，最后写 child 指针。strict 版本在任一
查找 miss 时抛出相同消息；若异常 helper 意外返回，四份都返回空 raw node。

dictionary keys 共同结构为：构造空 `vector<string>`，仅 category 7 继续，解析
keys 和死值 offsets view，reserve 后复用一个临时 string 逐项 DecodeName 并
拷入 vector。Android 内联 category classifier，iOS 保留 helper 调用。
`IsValid` 的 `owner != null && node != null` 独立短函数仅由两个 Android 链接保留，
且均无 xref；iOS 对应相邻函数范围没有该边界，不能为它伪造映射地址。

此前源码注释中的 Android arm64 `0x59641C`、`0x59659C`、`0x597B1C`
在当前 IDB 中分别落入不相关函数，已经从可编译代码注释中移除，未继续沿用。

## raw-node scalar 与 resource 边界

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | --- | --- | --- | --- |
| string pointer | `sub_598F38` | `sub_4DD3A0` | `sub_1000ED94C` | `sub_E9C90` |
| double | `sub_5996C8`（内联 decoder） | `sub_4DD7F0` -> `sub_4DBFB0` | `sub_1000EDDE0` -> `sub_1000EBF1C` | `sub_EA088` -> `sub_E86D8` |
| int | `sub_599818` | `sub_4DD800` | `sub_1000EDDE8` | `sub_EA098` |
| type category | `sub_599934` | `sub_4DD88C` | `sub_1000EDEE4` -> `sub_1000EBD9C` | `sub_EA116` -> `sub_E8650` |
| contains dictionary key | `sub_5999B8` | `sub_4DD918` | `sub_1000EDEF0` | `sub_EA120` |
| resource pointer/size | `sub_599AC4` | `sub_4DD9D8` | `sub_1000EDF78` | `sub_EA1F0` |

string getter 仅接受 category 4；0x15..0x18 解码 1/2/3/4 字节索引，category-4
扩展 tag 0x2c 落入 index=0 默认分支。返回值是 owner 内 stringsData 的借用指针，
不分配也不延长 owner 生命周期。

GetInt/GetDouble 的共同 tag 分派、窄/宽有符号整数解码、float/double 转换和错误
消息一致。Android arm64 把 raw-double 和整数 decoder 完整内联；其余目标保留
不同层级的 helper。GetInt 的源码 ABI 返回 `tjs_int`（32 位），0x0b 在该 wrapper
只暴露低 32 位；GetDouble 对 0x09..0x0c 使用完整 int64 后转 double。

0x07 的第三个数据字节同样不是无符号高字节：Android arm64 `sub_599818` 与 iOS
arm64 `sub_1000EE14C` 使用 `LDRSB`，Android armv7 `sub_4DDAA8` 与 iOS armv7
`sub_EA2EC` 也使用 `LDRSB` 后左移 16，形成有符号 24 位结果。64 位 GetInt 某些
分支会顺带在 X0 留下高位，但已检查的直接消费者和 32 位 ABI 都只观察 W0；这些
额外高位不属于 `tjs_int` 返回值。

0x09 与 0x0a 分别把偏移 +5 的有符号 8/16 位高部符号扩展到 64 位；0x0b 则是
刻意不同的边界：Android armv7 `sub_4DDADC` 与 iOS armv7 `sub_EA320` 都以
`LDRB + LDRH + ORR` 拼出高 24 位，iOS arm64 `sub_1000EE1B4` 和 Android arm64
内联在 `sub_5996C8` 的路径也都使用无符号 byte/halfword。也就是说 0x0b 产生高八位
清零的 56 位非负数，不对 bit 55 做符号扩展；本地 decoder 故意保留这一行为。
对应的 24/40/48 位符号扩展与 56 位不扩展边界现已加入 psbfile 单元测试。

type category 的八组映射四份完全一致。Android wrapper 内联完整 switch，iOS
wrapper 调用共享 helper。旧注释中的 Android arm64 `0x599554` 实际位于
`std::vector<string>::reserve`，不是分类函数，已纠正为 `sub_599934`。

resource getter 先检查 header.chunkData；为空时返回 null 且不写 size。非空时解析
chunkOffsets/chunkLengths，只有 0x19..0x1c 改写 index，其他 tag 保持 index=0；
随后无类别门控地写 `size = lengths[index]` 并返回
`chunkData + offsets[index]`。返回块同样只是借用 owner 内存。

## dispatch、variant 与对象生命周期

### 原始实现文件名

iOS arm64 在 `0x1014F2121`、iOS armv7 在 `0x1380C44` 保留同一条 ASCII
断言路径：

`/Volumes/E/Projects/kirikiri2_mob/kirikiri2/src/plugins/PSBFile.cpp`

两处都已读取完整原始字节并确认 NUL 边界。iOS arm64 的 xref 来自
`sub_1000EB9D0@0x1000EB9D0`、`sub_1000EC980@0x1000EC980`、
`sub_1000ECB10@0x1000ECB10`；iOS armv7 的 xref 来自
`sub_E8308@0xE8308`、`sub_E8E60@0xE8E60`、`sub_E8F60@0xE8F60`，分别是
CreateVariant、PropGetByNum 和 PropGet 的断言路径。Android 两端对
`PSBFile.cpp` 的 IDA string search 以及 UTF-8、UTF-16LE、UTF-32LE 原始字节
搜索都为零命中，符合其 release 构建裁掉断言字符串，不能据此否定文件名。

因此本地原先承载这些实现的 `main.cpp` 已重命名为 `PSBFile.cpp`；这一步只恢复
被编译时路径直接证明的文件名。其它 psbfile translation unit 是否也来自同一
原始文件，目前只有地址连续性等间接证据，暂不据此强行合并。

为排除遗漏，本轮还对 `PSBMedia.cpp`、`PSBRawFile.cpp`、
`PSBPackedInternal.h`、`PSBMedia` 和 `PSBRawFile` 在四份 IDB 中分别完成 IDA
字符串表搜索以及 UTF-8、UTF-16LE、UTF-32LE 原始字节搜索，并跑完全部分页；结果
均为零命中。`PSBFile.cpp` 仍只在两份 iOS 中命中上述完整路径。这个 negative result
只能说明其它 translation-unit 名称没有静态字面证据，不能证明它们应被合并；本地
继续保留当前拆分，并把其余文件边界标为未知。

内部标识符也单独复核：`PSBValueDispatch`、`PSBRawOwner`、`PSBRawNode`、
`PSBFileConvertor`、`PsbArray` 在四份 IDB 的字符串表以及 UTF-8、UTF-16LE、
UTF-32LE 原始字节搜索中均为零命中，且每个结果均已跑到 `cursor.done=true`。
因此这些本地类型名只能作为行为/结构的语义标签；除二进制直接证明的
`PSBFile` 与 `CreateVariant` 外，不能把其余名字宣称为原始源码标识符，相关内部
helper 继续保留 `_guess`。

同一组 iOS `assert` 元数据还精确保留了 `__func__` 字符串 `CreateVariant`：iOS
arm64 的字符串位于 `0x1014F2113`，由 `sub_1000EB9D0` 引用，并与
`PSBFile.cpp` 路径及 `result->AsObjectNoAddRef()` 表达式一起传入断言；armv7 保留
同构调用。该成员名因此已从推测的 `CreateVariant_guess` 恢复为确定的
`CreateVariant`。没有同等级字符串或符号证据的内部 helper 仍保留 `_guess`。
断言行号还给出原始文件内的相对顺序：CreateVariant 为第 400 行，PropGetByNum 为
第 613 行，PropGet 为第 632 行；这三者是源码位置证据，不把编译后地址顺序误当成
源文件顺序。本地 `PSBFile.cpp` 的三个定义现已按
`CreateVariant -> PropGetByNum -> PropGet` 排列，与该直接证据一致。

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | --- | --- | --- | --- |
| dispatch 构造 | `sub_597EB4` | `sub_4DCB50` | `sub_1000EC248` | `sub_E8874` |
| AddRef | `sub_597EA0` | `sub_4DCB44` | `sub_1000ECD58` | `sub_E919A` |
| Release | `sub_597E20` | `sub_4DCB0C` | `sub_1000ECD00` | `sub_E9164` |
| CreateVariant | `sub_596B1C` | `sub_4DBD78` | `sub_1000EB9D0` | `sub_E8308` |
| string variant | `sub_596FA4` | `sub_4DC044` | `sub_1000EC010` | `sub_E8758` |
| resource variant | `sub_597050` | `sub_4DC0D8` | `sub_1000EC0F4` | `sub_E87C8` |
| DecodeName 成员包装 | `sub_5979A0` | `sub_4DC740` | 未保留独立边界 | 未保留独立边界 |
| IsInstanceOf | `sub_597204` | `sub_4DC25C` | `sub_1000EC32C` | `sub_E8938` |
| EnumMembers | `sub_597330` | `sub_4DC38C` | `sub_1000EC458` | `sub_E8A18` |
| GetCount | `sub_5979C0` | `sub_4DC760` | `sub_1000EC8B0` | `sub_E8DF0` |
| PropGetByNum | `sub_597AA4` | `sub_4DC830` | `sub_1000EC980` | `sub_E8E60` |
| PropGet | `sub_597C34` | `sub_4DC978` | `sub_1000ECB10` | `sub_E8F60` |
| NativeInstanceSupport | `sub_597170` | `sub_4DC1D0` | `sub_1000EC2A0` | `sub_E88C4` |
| IsValid | `sub_5972D0` | `sub_4DC340` | `sub_1000EC3F8` | `sub_E89CA` |
| Invalidate | `sub_5972EC` | `sub_4DC356` | `sub_1000EC414` | `sub_E89E0` |

### PSBValueDispatch 主 vtable 全表

以下 32 个槽按 `iTJSDispatch2` 的声明顺序列出；最后三个是
`iTJSNativeInstance` 新虚函数追加到 primary vtable 的实现。标记 `-1002` 的 19 个
槽均已逐函数 fresh decompile，四份都保留独立函数边界并无条件返回
`TJS_E_NOTIMPL`，不能把它们合并成一个共享 helper。

| 槽 | 方法 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 | 行为 |
| ---: | --- | --- | --- | --- | --- | --- |
| 0 | AddRef | `sub_597EA0` | `sub_4DCB44` | `sub_1000ECD58` | `sub_E919A` | 实现 |
| 1 | Release | `sub_597E20` | `sub_4DCB0C` | `sub_1000ECD00` | `sub_E9164` | 实现 |
| 2 | FuncCall | `sub_597E00` | `sub_4DCAF6` | `sub_1000ECCE0` | `sub_E914E` | -1002 |
| 3 | FuncCallByNum | `sub_597DF8` | `sub_4DCAEC` | `sub_1000ECCD8` | `sub_E9144` | -1002 |
| 4 | PropGet | `sub_597C34` | `sub_4DC978` | `sub_1000ECB10` | `sub_E8F60` | 实现 |
| 5 | PropGetByNum | `sub_597AA4` | `sub_4DC830` | `sub_1000EC980` | `sub_E8E60` | 实现 |
| 6 | PropSet | `sub_597A9C` | `sub_4DC826` | `sub_1000EC978` | `sub_E8E56` | -1002 |
| 7 | PropSetByNum | `sub_597A94` | `sub_4DC81C` | `sub_1000EC970` | `sub_E8E4C` | -1002 |
| 8 | GetCount | `sub_5979C0` | `sub_4DC760` | `sub_1000EC8B0` | `sub_E8DF0` | 实现 |
| 9 | GetCountByNum | `sub_5979B8` | `sub_4DC754` | `sub_1000EC8A8` | `sub_E8DE4` | -1002 |
| 10 | PropSetByVS | `sub_5979B0` | `sub_4DC74A` | `sub_1000EC8A0` | `sub_E8DDA` | -1002 |
| 11 | EnumMembers | `sub_597330` | `sub_4DC38C` | `sub_1000EC458` | `sub_E8A18` | 实现 |
| 12 | DeleteMember | `sub_597328` | `sub_4DC382` | `sub_1000EC450` | `sub_E8A0C` | -1002 |
| 13 | DeleteMemberByNum | `sub_597320` | `sub_4DC378` | `sub_1000EC448` | `sub_E8A02` | -1002 |
| 14 | Invalidate(dispatch) | `sub_5972EC` | `sub_4DC356` | `sub_1000EC414` | `sub_E89E0` | 实现 |
| 15 | InvalidateByNum | `sub_5972E4` | `sub_4DC34C` | `sub_1000EC40C` | `sub_E89D6` | -1002 |
| 16 | IsValid | `sub_5972D0` | `sub_4DC340` | `sub_1000EC3F8` | `sub_E89CA` | 实现 |
| 17 | IsValidByNum | `sub_5972C8` | `sub_4DC336` | `sub_1000EC3F0` | `sub_E89C0` | -1002 |
| 18 | CreateNew | `sub_5972C0` | `sub_4DC32C` | `sub_1000EC3E8` | `sub_E89B6` | -1002 |
| 19 | CreateNewByNum | `sub_5972B8` | `sub_4DC322` | `sub_1000EC3E0` | `sub_E89AC` | -1002 |
| 20 | Reserved1 | `sub_5972B0` | `sub_4DC318` | `sub_1000EC3D8` | `sub_E89A2` | -1002 |
| 21 | IsInstanceOf | `sub_597204` | `sub_4DC25C` | `sub_1000EC32C` | `sub_E8938` | 实现 |
| 22 | IsInstanceOfByNum | `sub_5971FC` | `sub_4DC250` | `sub_1000EC324` | `sub_E892C` | -1002 |
| 23 | Operation | `sub_5971F4` | `sub_4DC246` | `sub_1000EC31C` | `sub_E8922` | -1002 |
| 24 | OperationByNum | `sub_5971EC` | `sub_4DC23C` | `sub_1000EC314` | `sub_E8918` | -1002 |
| 25 | NativeInstanceSupport | `sub_597170` | `sub_4DC1D0` | `sub_1000EC2A0` | `sub_E88C4` | 实现 |
| 26 | ClassInstanceInfo | `sub_597168` | `sub_4DC1C4` | `sub_1000EC298` | `sub_E88BA` | -1002 |
| 27 | Reserved2 | `sub_597160` | `sub_4DC1BA` | `sub_1000EC290` | `sub_E88B0` | -1002 |
| 28 | Reserved3 | `sub_597158` | `sub_4DC1B0` | `sub_1000EC288` | `sub_E88A6` | -1002 |
| 29 | Construct | `sub_597E10` | `sub_4DCB04` | `sub_1000ECCF0` | `sub_E915C` | 0 |
| 30 | Invalidate(native) | `nullsub_234@sub_597318` | `nullsub_132@sub_4DC374` | `nullsub_67@sub_1000EC440` | `nullsub_65@sub_E89FE` | 空 |
| 31 | Destruct(native) | `nullsub_236@sub_597E08` | `nullsub_134@sub_4DCB00` | `nullsub_69@sub_1000ECCE8` | `nullsub_67@sub_E9158` | 空 |

secondary `iTJSNativeInstance` vtable 只含三个 this-adjusted 副本：Construct 分别为
`sub_597E18/sub_4DCB08/sub_1000ECCF8/sub_E9160`；Invalidate 分别为
`nullsub_235@sub_59731C/nullsub_133@sub_4DC376/nullsub_68@sub_1000EC444/nullsub_66@sub_E8A00`；
Destruct 分别为
`nullsub_237@sub_597E0C/nullsub_135@sub_4DCB02/nullsub_70@sub_1000ECCEC/nullsub_68@sub_E915A`。

CreateVariant 的八个类别共同为：0 清空、1 boolean、2 int64、3 double、
4 string、5 octet、6/7 创建共享同一 raw owner 的 dispatch。未知类别由分类器
抛错；若异常 helper 意外返回，结果保持原值。boolean decoder 的完整源码形态在
两份 iOS 中保留：tag 2/3、所有窄/宽整数、float/double、0x1d false；Android
把已知类别传播进函数后裁掉了不可能的数值分支。

octet 分支的三个 runtime 边界如下；Android arm64 的 raw-resource 解码本身内联在
CreateVariant 中，其余三份先调用各自 `resource variant` wrapper，但得到裸块后都
遵循“分配 octet -> 以临时 type=3 variant CopyRef 到输出 -> 销毁临时”的同一链：

| 边界 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | --- | --- | --- | --- |
| octet 分配 | `sub_A0C9F4` | `sub_75F76E` | `sub_100318F54` | `sub_31E2F4` |
| variant CopyRef/赋值 | `sub_A0E464` | `sub_760440` | `sub_100319E14` | `sub_31F1C0` |
| 临时 octet variant 析构 | `sub_A0E078` | `sub_760238` | `sub_100319A60` | `sub_31EF1C` |

两份 iOS 还保留源级对象后置断言：CreateVariant 创建 object 后断言
`AsObjectNoAddRef()` 非空；PropGet/PropGetByNum 成功出口再次断言。Android release
构建裁掉这些断言。本地源码已经恢复这两层断言，而不是只保留功能等价的对象赋值。

EnumMembers 在判断 array/dictionary 类别前先构造输出 variant；no-value flag 令回调
收到两个参数，否则收到三个，回调返回值始终忽略。IsInstanceOf 故意不检查
valid/owner，非空 member name 返回 `E_NOTIMPL`，只识别 String、Octet、Array、
Dictionary。GetCount 则检查 valid/owner，且只接受 category 6；未知 packed count
tag 返回零。四份 `GetCount` 对 tag `0x10` 都把原始 32 位 count 直接写入
`tjs_int`，因此 `0x80000000..0xffffffff` 在脚本边界可见为负数，不做无符号范围
保护或饱和。PropGetByNum 同样先按 32 位模加法处理负索引，再用 signed
`index < 0 || index >= count` 判界；只有判界通过才构造 packed offset table。成功路径
把 table 自身 `nBytes` 与元素 offset 以 32 位相加，再把合计值按有符号 32 位扩展后
加到 packed base；iOS arm64 在伪代码中明确保留 `(int)(v14 + v13)`。PropGet 与
PropGetByNum 都不保护空输出指针。Invalidate 只清除 `valid_`，不释放 owner，也不清除
node。

## PSBMedia 与 storage media 注册

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | --- | --- | --- | --- |
| complete destructor | `sub_599BD0` | `sub_4DDBA4` | `sub_1000EE310` | `sub_EA3FA` |
| AddRef | `sub_599C58` | `sub_4DDBFC` | `sub_1000EE398` | `sub_EA454` |
| Release | `sub_599C68` | `sub_4DDC04` | `sub_1000EE3A8` | `sub_EA45C` |
| GetName | `sub_599C88` | `sub_4DDC18` | `sub_1000EE3C8` | `sub_EA46E` |
| NormalizeDomainName | `nullsub_238@sub_599C9C` | `nullsub_136@sub_4DDC2C` | `nullsub_71@sub_1000EE3DC` | `nullsub_69@sub_EA480` |
| NormalizePathName | `nullsub_239@sub_599CA0` | `nullsub_137@sub_4DDC2E` | `nullsub_72@sub_1000EE3E0` | `nullsub_70@sub_EA482` |
| CheckExistentStorage | `sub_599CA4` | `sub_4DDC30` | `sub_1000EE3E4` | `sub_EA484` |
| Open | `sub_599D1C` | `sub_4DDC80` | `sub_1000EE42C` | `sub_EA4B0` |
| GetListAt | `sub_599DD4` | `sub_4DDD28` | `sub_1000EE4BC` | `sub_EA590` |
| GetLocallyAccessibleName | `sub_59A1B8` | `sub_4DDF04` | `sub_1000EE728` | `sub_EA7E4` |
| EnsureContainer | `sub_59A1E4` | `sub_4DDF18` | `sub_1000EE754` | `sub_EA7F8` |
| GetResourceData | `sub_59A494` | `sub_4DE038` | `sub_1000EE92C` | `sub_EA9D0` |
| Resolve | `sub_59A890` | `sub_4DE2DC` | `sub_1000EEBCC` | `sub_EAD1C` |
| 预注册回调/内联构造 | `sub_59887C` | `sub_4DD018` | `sub_1000ED3C8` | `sub_E9760` |

四份 vtable 共同证明对象布局顺序为 vptr、非原子 `int` 引用计数、
`tTJSVariant _file`、`ttstr _container`。对象大小在 64 位为 0x28，在 32 位为
0x18。构造函数被内联进预注册回调，建立 ref=1、void `_file`、空 `_container`；
析构按逆序先销毁 `_container` 再销毁 `_file`。两个 normalize 槽位都是空函数。
GetName 的 IDA 字符串列表在 iOS 中只显示 `"p"`，但四份地址的原始字节均为
UTF-16LE `70 00 73 00 62 00 00 00`，即 `"psb"`。

EnsureContainer 的共同控制流是：要求名字至少含一个 `/`；截取首段容器名；仅在
`_file.Type()==tvtObject` 且缓存容器名相等时直接成功。否则分配 PSBFile 并调用
storage load；false 时删除 holder。load 成功后创建 adaptor，再通过临时 variant
赋给 `_file`，最后更新 `_container`。若 adaptor 创建返回 null，四份仍把 `_file`
更新为 void、提交容器名并返回 true，同时没有回收刚分配的 native holder；本地保留
这个泄漏边界。

Resolve 不为 `_file` 类型错误、NativeInstanceSupport 失败或空 owner 增加保护；这些
路径仍可在解引用处失败。它从首个 `/` 后开始逐段遍历，每段只构造一个 narrow
holder，先 ContainsDictionaryKey，再 strict lookup。任一缺失返回 false 且不触碰
调用者输出；只有最后一段成功后才执行输出的 Release-old、copy、AddRef、write-node。

GetResourceData 构造空 raw node，Resolve 成功后调用 GetResource，并在退出时释放临时
owner。Android arm64 把 raw resource decoder 完整内联；其余三份保留 wrapper 调用。
CheckExistentStorage 使用短路 `EnsureContainer && resource != null`。Open 在 Ensure
失败时返回 null，在 resource 为空时抛 `%1: cannot open psbfile`；若异常 helper
意外返回，仍会继续构造空 block 的 memory stream。

Open 使用的四个 memory-stream 构造边界为 `sub_8F8054`、`sub_6BEC38`、
`sub_100265524 -> sub_100265490`、`sub_265D88 -> sub_265C70`。非空 block 会把
Reference 置 true；对应析构只在 block 非空且 Reference=false 时释放内存，且从不
持有 PSB owner。因此存活的 stream 只是借用当前容器缓冲区，切换媒体容器可使它悬空。

GetListAt 仅为 array/category 6 和 dictionary/category 7 产出项目。array count 只认
0x0d..0x10，未知 count tag 直接结束；每项是十进制索引字符串。iOS armv7 的初始
伪代码漏掉该整数参数，但 `sub_EA590` 的 Thumb 指令在调用 `sub_19F4CC` 前以 R1
携带并逐次递增索引，而 `sub_19F4CC` 明确读取 R1，因此不是平台差异。dictionary
路径构造 keys 与随后一个死值 offsets view，复用一个 string DecodeName 后交给
lister。其他已知类别不枚举，未知 tag 通过共同分类器抛内部错误。

预注册回调的函数内静态指针由 C++ guard 只初始化一次，但
TVPRegisterStorageMedia 调用位于 guard 之后，所以每次回调都会再次注册同一指针。

## 外部消费者与资源缓存边界

对 storage load、GetRoot/Transfer、strict dictionary lookup、GetString、GetDouble、
ContainsDictionaryKey 和 `PSBValueDispatch` 构造做完整直接 xref 审计后，插件函数簇之外
的调用者收敛为以下八组。每组的 raw-node 调用形状、字符串和控制流都能在四份中对应；
它们是上层业务消费者，不是遗漏的第二套 psbfile 实现。

| 消费者组 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | --- | --- | --- | --- |
| texture 元数据/资源物化 | `sub_691CC8` | `sub_570500` | `sub_1000F316C` | `sub_EF97C` |
| icon/source texture 集解析 | `sub_6931C8` | `sub_570F54` | `sub_1000F4098` | `sub_F0BE4` |
| texture 几何小包装（originX、originY、width、height、clip） | `sub_69A3F4/sub_69A4B8/sub_69A57C/sub_69A65C/sub_69A73C` | `sub_57511C/sub_575180/sub_5751E4/sub_575258/sub_5752CC` | `sub_1000F8E88/sub_1000F8EEC/sub_1000F8F50/sub_1000F8FD0/sub_1000F9050` | `sub_F5D4C/sub_F5E04/sub_F5EBC/sub_F5F8C/sub_F605C` |
| Motion::ResourceManager load/cache | `sub_6A616C` | `sub_57B338` | `sub_1001012D8` | `sub_FE40C` |
| Motion raw-node 查找 | `sub_6A6AD8` | `sub_57B780` | `sub_100101AC8` | `sub_FECF4` |
| Motion child dispatch 物化/缓存 | `sub_6A72B4` | `sub_57B9F8` | `sub_100101E84` | `sub_FF11C` |
| icon 资源查找/物化 | `sub_6A7F1C` | `sub_57BDE0` | `sub_100102594` | `sub_FF890` |
| ObjSource pixel/palette 懒物化 | `sub_6D7834` | `sub_599A34` | `sub_10012686C` | `sub_125D4C` |
| Player `isExistMotion` 包装 | `sub_6CDBD4` | `sub_5942F4` | `sub_10011F558` | `sub_11E054` |

外部资源对象本身的注册也已按当前四份文件重定位。`SourceCache` registrar 为
`sub_6A5988/sub_57B0DC/sub_100100F90/sub_FE12A`，其
`loadSource/clearCache/bufLayer` 回调分别为
`sub_6A4F88/sub_57ACC8/sub_1001009AC/sub_FDB50`、
`sub_6A5818/sub_57B018/sub_100100F10/sub_FE0D4`、
`sub_6A58DC/sub_57B060/sub_100100F84/sub_FE11A`。派生的
`ResourceManager` registrar 为
`sub_6A8C9C/sub_57C3A8/sub_100102E88/sub_1002FC`，并原样重新列出这三个继承成员，
再注册本表中的 load、raw-node、child 和 icon-source 消费入口。因此本地继承结构不是
从现有 C++ 反推，而是由四份 NCB 注册表共同证明。

### KRKR atlas 主链

旧源码名中的 `0x6948E8`、`0x695DE8`、`0x697D34` 与 `0x6F1060` 都来自已经
移除的历史 Android `libkrkr2.so`，不能继续充当当前地址。四份当前二进制的闭合映射为：

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | --- | --- | --- | --- |
| Player find-source 主函数 | `sub_691CC8` | `sub_570500` | `sub_1000F316C` | `sub_EF97C` |
| KRKR atlas 路径解析、整组解码/打包与命中回填 | `sub_6931C8` | `sub_570F54` | `sub_1000F4098` | `sub_F0BE4` |
| render-time texture getter / atlas retry | `sub_6EE440` | `sub_5AC518` | `sub_10014019C` | `sub_1414C0` |
| 通用 `ttstr` separator split | `sub_695114` | `sub_571C50` | `sub_1000F52D0` | `sub_F1D20` |

atlas 主函数先用 split helper 按 `/` 拆分持久化 `SourceState.path`，仅检查首段
`"src"`，随后直接消费第 2、3 段；它在模块 cache 中查找路径命中，miss 时枚举
`root["source"][group]["icon"]` 的全部图标，构造值语义 record vector，解码像素，
经 `ImagePacker` 打包整组 atlas，再回查请求项。record 的共同源码字段顺序是
`PSBRawNode + rect/pointer/dimensions/bgra + std::string`；由两套 STL ABI 展开出的
vector stride 分别为 Android arm64 `0x40`、Android armv7 `0x2c`、iOS arm64
`0x50`、iOS armv7 `0x34`。四份都先完成可能扩容的 record append，再发布 rect
子对象指针，因而本地 `std::vector<record>` 加第二遍 pointer publication 的结构成立。

`ImagePacker::pack` 的当前 helper/callsite 映射为：Android arm64
`sub_A6DA58@0xA6DA58 <- sub_6931C8@0x693C94`，Android armv7
`sub_79436C@0x79436C <- sub_570F54@0x5717A8`，iOS arm64
`sub_100054E20@0x100054E20 <- sub_1000F4098@0x1000F4C30`，iOS armv7
`sub_53EB8@0x53EB8 <- sub_F0BE4@0xF15FA`。四个 helper 都在任一矩形尺寸大于
`TVPMaxTextureSize` 时返回 0，成功时返回 1；空输入仍追加一个 0x0 bin 后返回 1。
四个 atlas 调用点都不读取返回寄存器，而是直接消费输出 bins。失败因而留下空输出，
随后请求项的第二次 cache lookup 仍被解引用；本地不能为此补成功判断或 end guard。

每条 render-time getter 都先返回 `SourceState.texture`；为空时从 Player 持久化的
motion-context variant 构造临时 `ttstr`，调用同一 atlas 主函数，并在判断返回值前
销毁临时值。atlas 仍失败时才沿对象 dispatch 的 load-source 路径获取纹理。四个
atlas 主函数也各自恰有这两个调用方向：find-source 与 render-time getter。

Android arm64 的 `sub_6931C8` 不是损坏的短函数：fresh `lookup_funcs` 给出
`0x6931C8..0x695114`、大小 `0x1f4c`，fresh `disasm` 得到 1997 条指令。Hex-Rays
文本超过 MCP 单次展示上限而以 1023 字符预览返回，不能据此把函数误判成截断定义。
分段反汇编确认 palette 分支的字节 RL 循环位于 `0x6941CC..0x694280`，无 palette
分支的四字节 RL 循环位于 `0x694084..0x6941C8`；两条都内联于 atlas 主函数。

字符串证据没有依赖 IDA 对宽字符串的错误展示。`find(type=string)` 能定位窄
`"src/"` 到四个 atlas 主函数；随后 `find_bytes` 对 UTF-8、UTF-16LE、UTF-32LE
的 `"src"`（含终止符）跑完全部分页。四份相关 UTF-16LE 起点与引用分别是：

- `android_arm64`：`0x14D50A2` -> `sub_6931C8@0x693268`；
- `android_armv7`：`0xD84CB8` -> `sub_570F54@0x570FB2/0x570FB8`；
- `ios_arm64`：`0x10195B2DA` -> `sub_1000F4098@0x1000F412C`；
- `ios_armv7`：`0x174D63E` -> `sub_F0BE4@0xF0C88/0xF0C8E/0xF0C94`。

四处原始字节均为 `73 00 72 00 63 00 00 00`，前后边界还原为独立 `"src"`
并紧邻后续宽字符串；UTF-32LE 结果均为 0。Android arm64/iOS 的字符串列表把该
地址误显示成 `"s"`，原始字节搜索纠正了这一展示错误。

本轮据此把 `findSourceForNodeLike_0x6948E8`、
`loadKrkrAtlasSourceLike_0x695DE8`、`loadRenderSourceTextureForItemLike_0x6F1060`
和 `splitTtstrLike_0x697D34` 改成地址无关的 `_guess` 语义名；内部 atlas record/rect
也移除了旧地址后缀。与这些名字相关的 11 个 motionplayer translation unit 已通过
Emscripten `-fsyntax-only`，没有新增编译错误。

Motion loader 是唯一一组直接调用 `PSBFile::LoadStorage` 的外部代码，也是除下一组
child lookup 外唯一直接构造 `PSBValueDispatch` 的外部代码。它先按标准化 `ttstr`
查询 `unordered_map`；容器在四份 manager 对象中的偏移分别为 `+0x58/+0x34/+0x60/+0x38`。
键对象缓存懒计算 hash，bucket 内仍做字符串相等比较；Android 32 位明确使用
`hash % bucket_count`，iOS 64 位在 bucket 数为二次幂时用 mask，否则用 modulo，属于
各自 STL 展开差异。

outer map 的 mapped record 构造也已从旧地址重定位。Android arm64 为
`sub_6E8DC4 -> sub_6E8EEC -> sub_6E90DC`，Android armv7 为
`sub_5A7488 -> sub_5A751C -> sub_5A762C`；iOS arm64 在 `sub_100101798` 内联，
iOS armv7 在 `sub_FE940` 内联。四份共同的源码成员顺序是保留的 PSB owner、Win
texture map、KRKR atlas map；两个内层 map 各自按初始 bucket 请求 10 初始化。普通 C++
逆序析构因此为 KRKR map、Win map、PSB owner，本地 `LoadedResourceRecord` 的声明顺序
与之相同。

cache miss 时 loader 调用 storage load，验证根 `id == "motion"`，把 `spec` 的
`"krkr"/"win"` 映射为模式 1/2，并拒绝高于约 `3.0300001` 的 `version`。随后通过
Transfer 把 owner holder 放入 unordered_map：覆盖时先 Release 旧 owner，赋值后对新
owner AddRef。cache hit 先为局部 holder AddRef。最终 root dispatch 构造再次持有 owner；
写入 TJS object variant 时同一 dispatch 作为 object/this 各持有一个引用，局部初始引用
随后 Release。因此 map 替换或 loader 临时对象销毁不会使已返回 dispatch 悬空。

Motion child 物化把输入拆成两个 key，按
`root["object"][first]["motion"][second]` 查找；先查指定文件的 unordered_map 条目，
再遍历 manager 的 fallback 资源链。成功时为 child 构造 owner-sharing dispatch 并写入
上层 motion cache；两条成功路径各有一个构造点。miss 不制造伪 raw node，最终把输出
置为 void。完整 xref 中四份 dispatch 构造都只有三个外部调用点：loader root 一次，
child lookup 两次。

Player 的 `isExistMotion` 四个包装回调分别为
`sub_6CDBD4/sub_5942F4/sub_10011F558/sub_11E054`。四份都用持久化角色名和输入名称
构造 `"motion/<stealthChara>/<name>"`，再以
`{findMotionContextVariant, path}` 两个参数调用所持有 ResourceManager dispatch 的
`isExistMotion` 成员并把 Variant 转为 bool；它们不直接探测 storage，也不建立
Player-local cache。本地旧 `Player_isExistMotion@0x6D07F4` 注释已据此移除。

### Emote PSB 解密过滤器

对 `setEmotePSBDecryptSeed` / `setEmotePSBDecryptFunc` 重新执行了四份 UTF-8、
UTF-16LE、UTF-32LE 全编码搜索并跑完分页。两组当前 NCB 回调与过滤器主体为：

独立 emoteplayer 模块注册器分别为
`sub_67F908/sub_5623EC/sub_1001B65DC/sub_1B645C`。四份都先取得 `Motion`、
注册 `EmotePlayer`，再取得 `Motion.ResourceManager` 并把下面两个 setter 作为静态成员
注入；它们不属于 ResourceManager 自身的十二成员注册表。

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | --- | --- | --- | --- |
| seed setter | `sub_683110` | `sub_564EC0` | `sub_1001B8D68` | `sub_1B83AC` |
| seed xorshift invoker | `sub_6837AC` | `sub_56522E` | `sub_1001B92E8` | `sub_1B8992` |
| function setter | `sub_683240` | `sub_564F58` | `sub_1001B8E50` | `sub_1B84D0` |
| function invoker | `sub_683994` | `sub_5652C0` | `sub_1001B94A8` | `sub_1B8AB0` |
| process-wide replacement | `sub_6A5BB0` | `sub_57B174` | `sub_1001010B0` | `sub_FE1E0` |

seed setter 只要求至少一个参数，对 `p[0]` 做普通 TJS Integer 转换并捕获完整
64 位值；实际流状态使用其低 32 位。四份 invoker 都以
`123456789/362436069/521288629/seed` 初始化四字状态，以同一个 xorshift 公式每四个
字节生成一次新字，并异或 `[encryptData, chunkOffsets)`；长度先截为有符号 32 位，
小于一时直接返回。

function setter 同样接受额外参数，只把 `p[0]` 转为 Object/ObjThis closure。64 位闭包
保存两个 8 字节 dispatch 指针，32 位保存两个 4 字节指针；引用计数 control block
被作为 `std::function` 的唯一捕获复制。调用时四份都创建 CBinaryAccessor，传入
`{accessor, ownerSize}` 两个 Variant 并忽略脚本返回值。构造者初始 accessor 引用没有在
Variant AddRef 后平衡 Release，因此本地保留了这一共同泄漏边界。替换 helper 都先复制
新 target、与进程全局 target 交换，再销毁旧 target。

Android arm64 IDB 原先把两个 setter 错并为 `sub_683110: 0x683110..0x683528`。
机器码在 `0x68320C` 已返回，`0x683214..0x68323C` 是第一函数异常尾，第二个标准序言
从 `0x683240` 开始；本轮拆成 `0x683110..0x683240` 与 `0x683240..0x683528`。
同一区域还把 8 字节调用包装、管理器和调用主体并在一起；现已拆成
`sub_6838A0: 0x6838A0..0x6838A8`、`sub_6838A8: 0x6838A8..0x683994`、
`sub_683994: 0x683994..0x683AD0`。fresh decompile 分别恢复 target 管理与
CBinaryAccessor 调用，修复已保存回 IDB。

texture/icon 消费者只借用 raw string/resource 指针，并在对应 owner holder 仍存活时
立即比较、解码或复制，没有把裸指针越过调用返回保存。pixel/palette 解码器读取
width/height、可选 `compress == "RL"`、`pixel` 与可选 `pal`；需要时分配临时块完成
RLE、RGB 反转和 palette 展开，再把像素复制进上层图像对象并释放临时块。它不改变
psbfile 的 owner 或 dispatch 生命周期契约。

### pixel/palette 的 RL 解码边界

同一种 PSB `pixel`/`pal` 数据有两条相互独立的上层消费者链：Player 的整组 atlas
构建，以及 `ObjSource` 的按对象懒纹理物化。编译器在部分架构共享了独立 RL helper，
在另一些架构把循环内联；不能因为机器码 helper 相同就把两条调用链合并。完整映射为：

| 消费者/变体 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | --- | --- | --- | --- |
| atlas 8-bit/palette | 内联于 `sub_6931C8` | 内联于 `sub_570F54` | `sub_1000F5510`，调用者 `sub_1000F4098` | `sub_F1F6A`，调用者 `sub_F0BE4` |
| atlas 32-bit/RGBA | 内联于 `sub_6931C8` | `sub_571DA4`，调用者 `sub_570F54` | `sub_1000F5474`，调用者 `sub_1000F4098` | `sub_F1F10`，调用者 `sub_F0BE4` |
| ObjSource 8-bit/palette | 内联于 `sub_6D7834` | 内联于 `sub_599A34` | `sub_1000F5510`，调用者 `sub_10012686C` | `sub_F1F6A`，调用者 `sub_125D4C` |
| ObjSource 32-bit/RGBA | 内联于 `sub_6D7834` | `sub_571DA4`，调用者 `sub_599A34` | `sub_1000F5474`，调用者 `sub_10012686C` | `sub_F1F10`，调用者 `sub_125D4C` |

四份都由压缩资源的字节长度驱动循环，只检查初始长度是否至少为一，并以
`src < src + compressedSize` 作为唯一终止条件。marker 高位为一时重复
`(marker & 0x7f) + 3` 个元素，否则复制 `marker + 1` 个元素；palette 路径元素
宽一字节，RGBA 路径元素宽四字节。调用者按 `width * height * (pal ? 1 : 4)`
分配输出，但解码循环不接收输出长度，也不检查 marker payload 是否仍位于输入范围。
因此截断输入会越界读，声明的像素数与压缩流展开长度不一致会越界写或留下未写区域；
这些是四份共同边界，不应由端口中的 `srcEnd` payload 检查或 `dstEnd` 截断隐藏。

本轮已把 `PlayerResource.cpp` 中旧单文件地址命名的
`decodeKrkrRL8Like_0x696E40` / `decodeKrkrRL32Like_0x696D00` 改为
`decodePsbRL8_guess` / `decodePsbRL32_guess`，并只引用 atlas 调用链；
`SourceCache.cpp` 的对应实现则改为 `decodeObjSourceRL8_guess` /
`decodeObjSourceRL32_guess`，只引用 ObjSource 懒物化调用链。两处源码都写入上述四文件映射。
独立 `psb_rl_decompress_wasm.cpp` 也已移除旧 `sub_695DE8` 依据和通用带边界检查的
`align` 循环，改为与生产代码相同的两条压缩长度驱动循环。Emscripten 编译成功，
通过 Node 直接读取 WASM 输出验证现有 8 个 fixture 全部匹配。

`PSBMedia::Open` 在四份中只由 storage-media vtable 数据项引用，实际消费者经
`iTVPStorageMedia` 间接调用；`GetResourceData` 的直接调用者只限同一 media 的
CheckExistentStorage/Open。这解释了直接 xref 中没有另一个 media 入口，而不是分析
遗漏。

## IDB 修正

Android arm64 原 IDB 将 `0x59887C` 的完整函数错误并入前一个
`load` 函数。汇编显示该地址有独立 AArch64 prologue/epilogue。
本轮已把函数边界拆为：

- `sub_598648: 0x598648..0x59887C`
- `sub_59887C: 0x59887C..0x598918`

重新反编译后，`sub_59887C` 与另外三份预注册回调一致；修正已保存
回 Android arm64 IDB。

Android armv7 原 IDB 还把 vector 容量增长 helper 与后续 pixel/palette 解码器合并为
`sub_5999F4: 0x5999F4..0x599CF0`。`0x599A34` 有独立 Thumb prologue，另外三份也保留
独立同构函数。本轮已拆分并逐指令恢复为：

- `sub_5999F4: 0x5999F4..0x599A34`
- `sub_599A34: 0x599A34..0x599CF0`

fresh decompile 已恢复完整 width/height、RL、pixel/pal、临时缓冲区和图像物化流程，
修正已保存回 Android armv7 IDB。

Android arm64 的 texture clip wrapper 入口 `0x69A73C` 原被标成数据；原始字节
`ff c3 01 d1` 解码为标准 AArch64 `sub sp, sp, #0x70`，下一函数从 `0x69AAB8`
开始。本轮已建立 `sub_69A73C: 0x69A73C..0x69AAB8`，fresh decompile 与另外三份
clip wrapper 的 left/top/right/bottom 和对象引用序列一致，并已保存 IDB。

Android arm64 的 atlas 第二调用者也原本缺少函数定义。前一函数精确结束于
`0x6EE440`，该地址以 `sub sp, sp, #0x90` 型 AArch64 序言开始，下一已知函数从
`0x6EE708` 开始；atlas 主函数对它的 code xref 落在 `0x6EE528`。本轮建立
`sub_6EE440: 0x6EE440..0x6EE708` 后，fresh decompile 与 Android armv7
`sub_5AC518`、iOS arm64 `sub_10014019C`、iOS armv7 `sub_1414C0` 的
render-time texture getter 完整同构，修正已保存回 Android arm64 IDB。

## 历史 oracle 的证据等级

`tests/differential/oracle_runner/adapters/psbfile_load.py` 与
`tests/differential/oracle_runner/trace_targets.py` 中的 `0x598268`、`0x598960`
等偏移属于已移除的历史 Android `libkrkr2.so`，并不对应当前
`reference/binaries/` 的任何一份文件。它们仍可保留为旧 APK/Frida 运行工具，
但不能用于证明当前源码结构或四端一致性，也不能只替换少数入口地址后宣称完成
迁移：adapter 同时依赖 helper、全局对象、vtable 与析构入口，必须先针对某一份
明确选择的当前二进制完成整套 hook surface 映射。

本轮已在 adapter、target catalog 与 oracle-runner README 的入口处加入这一限制。
当前还原证据只接受本报告列出的四份 IDB fresh decompile/disassembly/xref，以及由
这些证据直接导出的本地验证；旧 oracle 偏移仅作历史运行上下文保留。

本轮源码注释清理覆盖 `psbfile` 本体及其直接 owner/filter、ResourceManager、
ObjSource、atlas 与 Player `isExistMotion` 消费链。`motionplayer` 中仍存在的渲染、
动画器、节点布局等历史单文件地址不参与本报告的 PSB 结论，也没有在缺少相应四文件
重定位的情况下被批量改写；它们属于各自功能后续单独审计的范围。

## 验证结果与证据边界

- `cpp/plugins/psbfile` 四个实现 translation unit、
  `tests/unit-tests/plugins/psbfile-dll.cpp`，以及本轮涉及的 11 个 motionplayer
  translation unit 均通过 Emscripten `-fsyntax-only`。这些门禁只有既有的
  `tjsString.h` literal-operator 空白等警告，没有新增编译错误。
- `tests/differential/wasmtime/psb_rl_decompress_wasm.cpp` 已由 Emscripten 4.0.23
  编译为独立 WASM；Node 直接实例化后，8 个现有 fixture 全部通过，覆盖空输入、
  literal、RLE、混合块及 1/4 字节元素宽度。
- 完整 `Web Debug Config` 已配置、生成并构建成功。vcpkg 的 wasm32-emscripten
  依赖全部安装完成，Ninja 的 339 个构建步骤全部通过，最终生成
  `out/web/debug/index.html`、`index.js`、`index.wasm` 与 store 模式
  `assets.zip`。最终链接只有既有 TJS/PSD、pthread memory growth、JSPI 实验性和
  JS library 等警告，没有 PSB、ResourceManager、motionplayer 或链接错误。
- 为跑通首次完整 Windows 构建，构建层另外处理了四项与 PSB 语义无关的问题：
  ffmpeg 的 Windows 命令行长度、GLib 为 Emscripten 安装 GDB auto-load 文件时
  嵌入 `C:` 的非法路径、未初始化的可选 `layerExImage` submodule，以及系统缺少
  `zip` 命令。没有为此拉取 `layerExImage`；资产包改由 CMake 自身确定性地产生
  store 条目。语法生成使用 WinFlexBison 2.5.25 所带 GNU Bison 3.8.2。
- 当前四文件证据能精确证明的原始 translation-unit 名只有 `PSBFile.cpp`；其他
  本地文件拆分属于按四份机器码重建出的可维护结构，不宣称恢复了未知的原始文件名。
  本项目不存在上游原始仓库，本轮没有搜索、克隆或引用任何所谓原版源码；所有还原
  结论都来自四份当前参考二进制的 fresh decompile/disassembly/xref 和上述本地验证。
- 尚未执行完整游戏内容的浏览器运行场景。`motionplayer` 中超出 PSB 直接消费链的
  历史单文件地址注释仍按前述范围边界留待各自功能的四文件审计，不能当作本报告结论。
