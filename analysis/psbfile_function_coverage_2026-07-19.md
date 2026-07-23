# PSBFile.dll 函数覆盖 manifest（2026-07-19）

## 2026-07-23 真实 Android oracle 状态

Android 12 / API 31 `userdebug` arm64-v8a AVD 上的正式 ADB/RPC/Frida runner 已闭合
正常 raw、storage、MDF、seed filter 与 cross-container PSBMedia 生命周期：

- raw octet/storage：`0x598268/0x598538 → 0x598708 → 0x598960`，均 `status=ok`；
- MDF：现有 50,708-byte `.ks.scn` 经两入口均解为 700,308-byte PSB 并通过 strict
  refresh；当前 `reference/` 的 142 只天然 MDF 全部可正常解压，没有 failure 样本；
- seed filter：203,302 字节与独立 host xorshift 全量一致，Frida 捕获
  `0x598268 → 0x598708 → 0x6863CC → 0x598960`；
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

2026-07-23 又证伪了“下一函数与 PSBFile 无调用关系”的结论：
`GetDictionaryKeys@0x598E64` 在 vector 容量耗尽时由 `0x598FFC` 调 PLT
`0x423250/0x42325C`，最终进入
`std::vector<std::string>::_M_emplace_back_aux<std::string &>@0x59B7E8`。
因此完整相关集合是 111 个业务/NCB 入口加 1 个本源码触发的 STL 扩容慢路径，
共 **112** 个；最后一只函数的 exclusive end 是 `0x59B9C8`。该地址有独立
`SUB SP,#0x30` 序言，`sub_42CFA0@0x42CFB0..0x42CFC8` 又把它作为字面
`PackinOne.dll` 的 callback 注册；函数体从 `fstat.dll` 起加载一组子插件。因此
`sub_59B9C8@0x59B9C8` 属下一模块，而不是 vector helper 的尾部。IDB 已把此前错并的
`0x59B7E8..0x59BC2C` 拆成 `0x59B7E8..0x59B9C8` 与
`0x59B9C8..0x59BC2C` 两只函数并保存。

下列分组穷举 112 个相关函数；每组地址数之和为
`42 + 1 + 10 + 19 + 17 + 22 + 1 = 112`。

## A. PSBValueDispatch 与 packed dispatch ABI（42）

本地对应 `cpp/plugins/psbfile/PSBDispatch.h` 的完整类声明和
`cpp/plugins/psbfile/main.cpp` 的 37 个 out-of-line `PSBValueDispatch` 定义
（32 个接口槽、构造器、`assign` 与 3 个私有 helper），以及相关析构包装。

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

2026-07-23 fresh decompile/disasm `PropGet@0x597854` 进一步确认 dictionary
分支 `0x59795C..0x5979C8` 只构造一只 `tTJSNarrowStringHolder`，并把同一
`Buf` 依次交给 name-trie 与 dictionary-offset 查找；成功、失败及异常路径均在离开
该分支时析构 holder。本地已删除此前额外的 `ttstr` → `std::string` 转换链，恢复同一
临时对象数量、数据流和析构边界。

同日更深一轮逐函数复审又纠正三处 Variant 调用边界：`PropGet@0x597854` 的 array
`count` 通过 `operator=(tjs_int32)@0xA0FF28` 以 `SXTW` 落入 Integer，而不是把
`uint32_t` 零扩展为 `tjs_int64`；`assign@0x59673C` 的 String 与
`EnumMembers@0x596F50` 的 dictionary name 直接调用 narrow
`operator=(const char *)@0xA0FEB4`，不先构造 `ttstr`；Enum 的 flags 先 default-construct，
再以 `tjs_int32(0)` 赋值，随后才构造 callback result。本地现已按这些 helper call、
临时 owner 数量和异常析构顺序复刻。

同一 `EnumMembers` 的 unknown-tag block `0x59748C..0x59749C` 还会在异常 helper
意外返回后把 category 写成 `-1`，再完成四只 Variant 的生命周期并返回
`TJS_E_NOTIMPL`。本地此前错误保留初始化值 `0`，现已纠正；对 PSBFile 范围内其余
`sub_95440C/sub_95458C` continuation 的独立扫描没有发现第二处差异。

2026-07-23 的 fresh decompile 与尾部反汇编还确认，`assign@0x59673C` 在入口
`0x596764` 保存 destination 到 `X19`，所有正常/throw-helper-return 分支汇入共同尾部，
并在 `0x596B88` 以 `MOV X0,X19` 返回同一 destination 地址。四个直接 caller
`0x5971B4/0x5973A4/0x597848/0x5979B4` 均忽略返回值。本地已从 `void` 恢复为返回
现有 destination pointer；原源码是 pointer 还是 reference 在 ARM64 ABI 上不可区分。

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

`GetInt@0x599438` 对 tag `0x09/0x0A/0x0C` 在函数体内物化 X0；完整 20 个 direct xref
中有 18 个只消费 W0，`0x694CA8/0x694CEC` 两处完全丢弃返回值，没有 caller 消费 X0
高位。故现有证据只能证明 conversion/callsite 的 signed 32-bit 可观察语义，尚不能唯一判断
外层 method 源码返回型是 `tjs_int` 还是 `tjs_int64`；manifest 不把本地 32-bit 签名升格为
二进制事实。两只 getter 的 nested CFG 与最小 codegen 对照支持四个共享 decoder-shaped
边界，本地已用 `_guess` helper 复原；精确名字和“inline helper vs 显式 nested switch”仍开放。
其中 tag `0x0B` 的共享 64-bit decoder 读取完整 56 位且不扩展 bit55，GetInt 的高字节读取
被优化掉只说明 wrapper 最终截断，不再被当成源码只读低 32 位的证据。

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
同时，`assign@0x59673C`、`getResource_guess@0x596C70`、
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
2026-07-23 的对象审计确认 `Resolve@0x59A4B0` 已直接建立 root raw node，
`GetResourceData@0x59A0B4` 已直接展开 resource/chunk table 解码；`PSBMedia.cpp.o` 不再
引用 `PSBFile::GetRoot@0x598A3C` 或 `PSBRawNode::GetResource@0x5996E4`。
`0x59A4B0` 的正常返回位于 `0x59A7D8`，异常 landing pads 延伸到 `0x59A8D4`；
其真实函数边界在 `0x59A8D8`，后者是下一只独立自动注册入口。

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

## 覆盖结论

112 个 Android 相关函数均已归属于生产源码、明确的 NCB/iTJS ABI wrapper 或本源码触发的
STL 实例化；没有未归属业务入口。该 manifest 只证明 **Android→local** 的入口归属完整，
不能反向证明 **local→Android** 没有额外抽取边界。另行完成的 local→Android Release
对象审计已消除三类已确认额外边界：dispatch 三函数四站点的 count-helper 调用、
`PSBMedia::Resolve→PSBFile::GetRoot`，以及
`PSBMedia::GetResourceData→PSBRawNode::GetResource`。这不能反向证明原始源码没有
等价 inline helper。当前 raw owner/node 的若干小方法只有
上层调用点内联行为证据、没有独立 Android 入口；其名字与是否为原始 inline helper 仍不能
仅由本地源码宣称。manifest 也不替代损坏输入的 Android runtime oracle；MDF zlib failure、
filter 后 offset failure、损坏 packed table、tag `0x0B`、>4 GiB storage 和
CreateAdaptor-null 分支，仍按主分析记录为缺少天然 fixture 的验证缺口。dictionary listing
已由恢复后的天然 `autoskip.psb` 在真实 Android 闭合。
成功跨-container replacement 与旧 stream metadata/析构已同时由本地守护和真实 Android
ADB/RPC/Frida oracle 覆盖。borrowed/non-retaining 的“stream 不保活 owner”性质仍由
反编译证据证明；运行时只观察 stream 自身 metadata，刻意不解引用 replacement 后的悬挂
Block。一次性 adapter smoke 仍不计入二进制实测。

本轮修改后 macOS Release `psbfile-dll` 为 **575/575**（10 cases），
`motionplayer-dll` 为 **1376/1376**（21 cases），`motionplayer-ttstr-hash-test` 为
**100/100**（22 cases）；Web Debug 最终链接与显式 Wasmtime
`krkr2_wasmtime_guest` 目标通过。motion playback runner 在当轮尚未进入 guest，当时的
checkout 缺少 `reference/xp3/logo_test_oracle.xp3`。2026-07-23 `reference/` 改为普通
ignored 本地资料目录并恢复该现成文件（SHA-256
`9d9f336dcccb5370a433f87b81054c74ff5099e2bd18a499e0f1ccf769158a7f`）；因此“当前缺少
物料”不再成立。该变化只闭合输入可用性，不能倒推当轮 runner 已实际进入 guest；如需引用
motion playback 运行结果，仍须以恢复后的独立执行记录为准。
