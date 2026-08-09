# typed-member 写值 producer 表面复核（2026-08-03）

## 结论

本轮继续只以 Android ARM64 `libkrkr2.so` 与 IDA 中已经由目标偏移、调用链和数据流修复的
对象类型为权威，把既有 typed-member 表面的 instruction-backed `W/RW` 子集进一步闭合到
每个字段写入值的完整 producer。

> 后续统计说明：本报告闭合的是 483-row 旧基线中的 instruction-backed `W/RW`，其
> 108 个写事件不受后续修正影响。完整 raw surface 复扫只新增 5 条 read-only typed
> promotion；当前 typed-member 总数为 488，详见
> [FOLLOWUP_RAW_MEMORY_DEREFERENCE_SURFACE_2026-08-03.md](FOLLOWUP_RAW_MEMORY_DEREFERENCE_SURFACE_2026-08-03.md)。

结果为 **ALIGNED / 无生产代码 GAP**：共 108 个字段写事件，落在 32 个 owner FDE 的
101 个实际 store 站点，形成 109 条 reaching-definition 关系。全部 108 个事件都从 owner
正常入口可达，landing-only 为 0；107 个事件是单来源，唯一多来源事件只有
`std::vector<std::string>::end` 的两条分配分支。没有 call return、call clobber 或 entry
residue。本轮没有修改 `cpp/`、fixture 或测试物料，因此不触发构建。

## 覆盖边界

基线来自
[FOLLOWUP_TYPED_MEMBER_FIELD_SURFACE_2026-08-03.md](FOLLOWUP_TYPED_MEMBER_FIELD_SURFACE_2026-08-03.md)
固定的 483 条旧基线 ctree member 语义行：

```text
R=312 / W=147 / RW=10 / address=14
```

其中 416 条带指令 EA，67 条是 optimizer-synthetic expression。本轮只处理带真实 EA 的
`W/RW`：

```text
W=105 / RW=3 / events=108
unique store sites=101 / owner FDEs=32
synthetic W/RW rows without an individual EA=49
```

49 条 synthetic `W/RW` 仍保留在原 typed-member 语义统计中；它们没有一一对应的机器
地址，因此这里不伪造 store site，也不把它们错误宣布为缺失。

## store 与 operand 形状

同一个 `STP` 可以同时产生两条字段语义事件；14 个 `STP` 机器站点因此对应 21 个事件。
每个事件都固定 selected operand，而不是只固定整条指令：

| store | 机器站点 | 字段事件 | selected operand 规则 |
| --- | ---: | ---: | --- |
| `STRB` | 9 | 9 | operand 0，1 byte |
| `STRH` | 8 | 8 | operand 0，2 bytes |
| `STR` | 70 | 70 | operand 0，4/8 bytes 由 opcode 固定 |
| `STP` | 14 | 21 | operand 0=`Rt`，operand 1=`Rt2`，各 8 bytes |
| **合计** | **101** | **108** | operand 0=94，operand 1=14 |

`STP` 的字段映射规则来自已修复的 typed-member ctree：当同一站点有两条字段行时，较小
member offset 对应 operand 0，较大 offset 对应 operand 1；只有一条字段行的 7 个站点都
是第二个 qword，故对应 operand 1。该规则没有从寄存器名或本地结构猜测。

访问模式精确为：

```text
W=105
RW=3
```

三个 `RW` 是目标 ctree 的 compound refcount 更新；机器层仍由 load/arithmetic/store
组成，不能因为最终指令是普通 `STR` 就降成纯写语义。

## 写值来源全集

108 个事件形成 `107×1 + 1×2 = 109` 条来源关系：

| 来源角色 | 关系 | 约束 |
| --- | ---: | --- |
| explicit instruction | 84 | exact address、word、destination register、producer kind |
| owner entry parameter | 3 | exact owner/site/bank 三元组 |
| architectural zero register | 22 | selected store operand 必须就是 `WZR/XZR` |
| preceding-call return | 0 | 没有字段值依赖前序 call 返回 |
| call clobber | 0 | 没有用易失寄存器越过未声明 call |
| entry residue | 0 | 没有把非参数入口状态伪装成字段来源 |

84 条显式 instruction source 分为：

```text
47 address     = ADD
21 memory      = 13 LDR + 8 LDRH
14 transfer    = MOV
2 arithmetic   = SUB + SUBS
```

物理写值 bank 分布为：

```text
X0=2 X1=1 X2=2 X8=45 X9=7 X10=8 X11=1 X12=2 X13=3 X14=5
X16=1 X19=2 X20=1 X21=2 X22=2 X23=1 X25=1 ZR=22
```

反向 slice 对 `X0..X18` 把 `BL/BLR` 当作易失边界；`X19..X25` 则按 AAPCS64 继续穿过
call。所有路径最终都精确命中清单中的 writer/entry source，没有发生一次未声明的
volatile call 穿越。

## 唯一多来源事件

`sub_59B7E8` 的 `STP@0x59B8F8` 以 operand 1 / `X25` 写 vector `end`。根据分配路径，
`X25` 只可能来自：

```text
ADD@0x59B8D0  word=0x91004119
ADD@0x59B8E4  word=0x91002299
```

两条 predecessor 分支在同一个 store 汇合，因此这是 108 个写事件中唯一的两来源 join。
`begin` 与 `capacityEnd` 的更新仍各自单来源。当前
`PSBRawNode::GetDictionaryKeys()` 使用 `std::vector<std::string>`、`reserve` 与
`emplace_back`，自然保留该 old-libstdc++ vector growth/helper 数据流，没有用别的容器或
手写数组替代。

## 三个入口参数来源

只有三个写值直接来自 owner entry；它们全部是已恢复 prototype 的真实参数：

| owner | store | bank | 字段 |
| --- | --- | --- | --- |
| `PSBValueDispatch` ctor `0x597AD4` | `0x597B10` | `X2` | `node` |
| `PSBRawOwner` ctor `0x598AAC` | `0x598AB0` operand 0 | `X1` | `data` |
| `PSBRawOwner` ctor `0x598AAC` | `0x598AB0` operand 1 | `X2` | signed `size` |

所有其他非零 bank 在抵达函数入口之前都必须命中显式 producer；这避免把“当前寄存器刚好
还保留某值”误判为源码参数。

## header 四组完整写入

`PSBRawHeader` 占 44 个事件，精确等于四次完整的 11 字段 population：

1. `PSBFile_Adopt_guess@0x598708` 的第一条 owner 建立路径；
2. 同函数旧 owner/临时 holder 生命周期折叠后的第二条路径；
3. `PSBRawOwner_Refresh_guess@0x598960`；
4. `PSBRawOwner_ctor_guess@0x598AAC`。

每组都保持同一 producer 形状：

```text
signature <- LDR W
version/encrypt <- LDRH W
encryptData/names/strings/stringsData <- ADD(rawData, decodedOffset)
chunkOffsets/chunkLengths/chunkData/entries <- ADD(rawData, decodedOffset)
```

因此 header 不是一个直接指向输入头部的裸 cast；目标确实先保存独立 inline view，再逐项
物化八个表指针。当前 `PSBRawHeader` 与 `PSBRawOwner::Refresh()` 保留同样的 11 字段和
计算顺序。

## 类型与对象家族

108 个写事件的 source-facing 类型/宽度为：

```text
pointer/8=74
bool/1=6
signed/4=6 signed/8=2
unsigned/1=3 unsigned/2=8 unsigned/4=8
aggregate/8=1
```

它们覆盖 14 个 base-type 族和 18 个 field-type 族，主要对象分布为：

| 对象族 | 写事件 | 目标数据流 |
| --- | ---: | --- |
| `PSBRawHeader` | 44 | 四组完整 header population |
| `PSBRawOwner` | 8 | data/size/header 与 refcount 生命周期 |
| `PSBValueDispatch` | 11 | refcount、owner/node、valid |
| `PSBRawNode` / pointer | 10 | holder/node 初始化、lookup/output assignment |
| `PSBMedia` | 3 | singleton refcount 与 cached container |
| `OwnerFilter` | 3 | `std::function` target/manager/invoker 清零 |
| vector | 7 | begin/end/capacityEnd 初始化与 growth commit |
| ncbind state/adaptor/class info/params functor | 22 | registration、native holder、argument/result bundle |

唯一 aggregate/8 是目标 `ttstr` 存储对象的整体赋值，不把它误报成普通 pointer store。

## fresh 反编译证据

本轮 fresh decompile 复核了四个高风险写链：

- `PSBFile_Adopt_guess@0x598708`：两条完整 header population、临时 holder 引用生命周期、
  filter 调用与 conditional Refresh；
- `PSBRawOwner_ctor_guess@0x598AAC`：entry `X1/X2` 先写 data/size，再建立 header view 和
  11 字段；
- `PSBMedia_Resolve_guess@0x59A4B0`：`current` node 的中间更新与 refcount 变换，caller
  output 只在成功尾部写入；
- `sub_59B7E8`：vector growth 的两条新 `end` 计算路径在 `STP@0x59B8F8` 合流，并提交
  begin/end/capacityEnd。

目标逻辑摘要不超过十行：

```text
enumerate every EA-backed typed-member W/RW event in all 114 owner FDEs
decode STRB/STRH/STR/STP and select the exact field-value operand
map that operand to its physical Xn/ZR bank and semantic field type/width
for ZR, require the selected store operand itself to be the zero source
otherwise walk every normal-CFG predecessor backwards from the store
stop at the first exact writer, declared call boundary, or true entry parameter
continue across calls only for AAPCS64 nonvolatile X19..X28 banks
collect branch-specific sources and require exact set equality for every event
```

## 本地逐段对照

| Android ARM64 写链 | 当前源码对照 |
| --- | --- |
| owner ctor/Refresh/Adopt 的 data、signed size、header view 与 11 字段 | `cpp/plugins/psbfile/PSBRawFile.h:17-70` 与 `PSBRawFile.cpp:176-218,516-539` 保留独立 owner、inline header、raw allocation 和完整 offset 物化 |
| dispatch ctor 的 owner/node/valid 以及 AddRef/Release/Invalidate | `cpp/plugins/psbfile/PSBDispatch.h` 保留嵌套 raw node、独立 valid byte 和 singleton refcount |
| raw-node lookup/strict output 的 owner retain 与 node 延后提交 | `cpp/plugins/psbfile/PSBRawFile.cpp:220-265` 保留 destination holder assignment 后再写 child node |
| Resolve 的 local current 与成功尾部 caller output | `cpp/plugins/psbfile/PSBMedia.cpp:52-109` 保留 miss 不写 output、成功才 `value = current` |
| vector begin/end/capacityEnd 初始化与 growth | `cpp/plugins/psbfile/PSBRawFile.cpp:280-306` 保留 `std::vector<std::string>`、reserve、emplace_back |
| params functor 的 numparams/result/param 与 converter bundle | `cpp/core/plugin/ncbind.hpp:924-999,1164-1194` 保留 paramsFunctor/MethodCaller 分层 |
| native registration class object/constructor flag 与 Variant/global release | `cpp/core/plugin/ncbind.hpp:1843-1934,2031-2052` 保留 Begin/Item/End 生命周期和嵌套状态 |

逐行对照没有发现需要修改 `cpp/` 的差异。ARM64 field offset 只留在分析清单中，没有反写
成 wasm32 `_pad`、packing 或 `offsetof` 约束。

## IDB 与机械门禁

dispatch entry-node、Adopt data/size、两组 header population、owner ctor entry 参数、vector
双零初始化、Resolve 成功尾写以及唯一 vector 多来源 join 均已加入
`TYPED-MEMBER-WRITE-CONTRACT` 注释并保存。

`verify_elf_surface.py` 新增 3,683-byte canonical payload（SHA-256
`0129db04fc736704e413513f069f402fd67a283893a4098ed99bbf739ecd26af`）。门禁会：

1. 要求 101 个 store 站点全部属于既有 385-site typed-member instruction surface；
2. 逐事件核对 owner/site/word、store kind、selected operand、bank、mode、base/field type、
   field offset 与 store width；
3. 从 ELF 重建 32 个 owner 的 entry-rooted normal CFG，并要求 108 个事件全部正常可达；
4. 对 ZR 写入直接验证 store operand，对其余 bank 重走完整 predecessor slice；
5. 逐字核对 84 个 instruction producer、三个 entry source 与唯一两路 join；
6. 在未声明 writer、入口残留或 volatile call boundary 上失败。

新增通过输出：

```text
typed_member_write_surface=true owners=32 sites=101 events=108 normal=108 landing=0 w=105 rw=3 strb=9 strh=8 str=70 stp_events=21 stp_sites=14 paths_complete=true sha256=true
typed_member_write_sources=true relations=109 single_source=107 multi_source=1 max_sources=2 instruction=84 entry=3 zero=22 call_return=0 call_clobber=0 entry_residue=0 memory=21 transfer=14 address=47 arithmetic=2 volatile_call_clobbers=0
typed_member_write_types=true pointer=74 bool=6 signed=8 unsigned=19 aggregate=1 base_types=14 field_types=18 banks=18 producer_classes=8
```

15 个 stripped/O3 identifier/factorization 上限保持不变；本轮新增的是字段写入值、构造参数
直传、zero initialization、header materialization、output commit 与容器 growth join 的正证据，
不需要也不允许转向 ARMv7、废弃私库、同版本源码或 Git LFS。

互补的 instruction-backed `R/RW/address` 读值、取址与 first-consumer/source 闭环见
[FOLLOWUP_TYPED_MEMBER_READ_CONSUMER_SURFACE_2026-08-03.md](FOLLOWUP_TYPED_MEMBER_READ_CONSUMER_SURFACE_2026-08-03.md)。
没有独立 ctree EA 的 42 条 synthetic `W`、15 条 `R`、7 条 `RW` 与 3 条 address 行则
保留语义分类并闭合到 73 个真实 enclosing machine anchor，详见
[FOLLOWUP_TYPED_MEMBER_SYNTHETIC_REALIZATION_SURFACE_2026-08-03.md](FOLLOWUP_TYPED_MEMBER_SYNTHETIC_REALIZATION_SURFACE_2026-08-03.md)。
