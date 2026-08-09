# 非 switch 间接调用参数 producer 表面复核（2026-08-03）

## 结论

本轮继续只以 Android ARM64 `libkrkr2.so`、IDA 中由目标 vtable/member-pointer/callback
记录恢复的类型，以及目标自身的 normal/LSDA CFG 为权威，把已有的 46 个非 switch
间接 transfer 从“目标寄存器生产者已闭合”推进到“全部语义实参生产者已闭合”。

结果为 **ALIGNED / 无生产代码 GAP**：45 个 `BLR` 与 1 个 deleting-destructor tail
`BR` 共暴露 117 个 `X0..X7` 参数，形成 120 条 reaching-definition 关系；40 个站点从
owner 正常入口可达，6 个只从 LSDA landing root 可达。全部路径均在显式 writer 或唯一
合法入口参数处停止，没有跨越 volatile call clobber，也没有 stack/scattered argloc 或
未声明的 entry residue。本轮没有修改 `cpp/`、fixture 或测试物料，因此不触发构建。

## 覆盖定义

基线来自
[FOLLOWUP_INDIRECT_ABI_SURFACE_2026-08-03.md](FOLLOWUP_INDIRECT_ABI_SURFACE_2026-08-03.md)
固定的 46 个非 switch 间接目标：44 个 fixed-offset vtable/callback `LDR` 与 2 个 Itanium
member-pointer register-offset `LDR`。本轮新增的是这些 transfer 的**调用输入侧**：

| flow class | transfer | 数量 | CFG 根 |
| --- | --- | ---: | --- |
| normal | `BLR` | 39 | owner FDE entry |
| normal | `BR` | 1 | owner FDE entry；deleting-destructor tail |
| landing-only | `BLR` | 6 | 对应 owner 的非零 LSDA landing roots |
| **合计** |  | **46** | 18 个 owner |

六个 landing-only 站点严格为：

```text
598458 59848C 5986E0 59A088 59AA48 59AA58
```

它们不能借用 owner entry 的寄存器状态；校验器会解析 `.gcc_except_table`，只合并确实可达
该 callsite 的 landing CFG，再从该图反向求参数来源。

## 46 个站点的语义 prototype

`P/U32/S32` 分别表示 64-bit pointer、32-bit unsigned 与 32-bit signed 参数。参数顺序
就是 AAPCS64 的 `X0..X7`；没有把 call target 所在寄存器算作参数。

| 角色 | site | 语义参数 |
| --- | --- | --- |
| dispatch `Release` | `596958`, `59B524` | `P` |
| Enum callback `FuncCall` | `597200`, `5973F0` | `P,U32,P,P,P,S32,P,P` |
| `std::function` manager | `598318`, `59837C`, `598458`, `59848C`, `599F04`, `59A088` | `P,P,S32` |
| stream `GetSize` | `5985A0`, `5985B8` | `P` |
| stream delete | `59862C`, `5986E0` | `P` |
| owner-filter invoke | `598858` | `P,P` |
| media delete tail | `59989C` | `P` |
| storage lister `Add` | `599BA4`, `599C50` | `P,P` |
| adaptor ref ops | `599F30`, `599F40`, `599F5C` | `P` |
| adaptor `CreateNew` | `59A3B4` | `P,U32,P,P,P,S32,P,P` |
| adaptor global `Release` | `59A3CC` | `P` |
| adaptor native lookup | `59A3F4` | `P,U32,S32,P` |
| Resolve native lookup | `59A514` | `P,U32,S32,P` |
| unregister `DeleteMember` | `59A9CC`, `59AA48` | `P,U32,P,P,P` |
| unregister `Release` | `59A9DC`, `59AA58` | `P` |
| registration AddRef/Release | `59ADFC`, `59AE48`, `59AE7C` | `P` |
| registration `PropSet` | `59AE6C` | `P,U32,P,P,P,P` |
| item interface slots | `59B050`, `59B068`, `59B07C`, `59B0A8` | `P` |
| factory callback | `59B1A8` | `P,S32,P,P` |
| factory native lookup | `59B1DC` | `P,U32,S32,P` |
| root native lookup | `59B30C`, `59B3E8` | `P,U32,S32,P` |
| root typed invoke/ref | `59B4C8`, `59B4E4`, `59B4F4` | `P` |
| load native lookup | `59B5F0` | `P,U32,S32,P` |
| load member invoke | `59B640` | `P,P` |

arity 精确为：

```text
1 -> 23 sites
2 -> 4 sites
3 -> 6 sites
4 -> 7 sites
5 -> 2 sites
6 -> 1 site
8 -> 3 sites
```

117 个参数的 bank 与类型分布为：

```text
X0..X7 = 46,23,19,13,6,4,3,3
pointer/8 = 89
signed/4 = 16
unsigned/4 = 12
```

## 完整来源关系

117 个参数形成 `114×1 + 3×2 = 120` 条来源关系；最大 join 只有两路：

| 来源角色 | 关系 | 约束 |
| --- | ---: | --- |
| explicit instruction | 119 | exact address、word、destination operand、source kind |
| entry parameter | 1 | `PSBMedia` deleting destructor 的 entry `X0` |
| preceding-call return | 0 | 没有参数依赖前序 call 返回值 |
| call clobber | 0 | 没有跨越易失寄存器 call boundary |
| entry residue | 0 | 没有把非参数初值伪装成来源 |

显式 instruction source 分为：

```text
17 memory  = 4 LDP + 13 LDR
77 transfer = MOV
23 address  = ADD
2 select    = CSEL
```

三个多来源参数是：

1. `EnumMembers` Array callback `0x597200:X0`：`LDP@0x5971B8` 或
   `LDP@0x5971CC`；
2. `EnumMembers` Dictionary callback `0x5973F0:X0`：`LDP@0x5973A8` 或
   `LDP@0x5973BC`；
3. `RegistEnd` 的 `Release@0x59AE48:X0`：`LDR@0x59AE00` 或
   `MOV@0x59AE30`。

唯一 entry pass-through 是 `PSBMedia` deleting destructor tail `BR@0x59989C`：语义实参
只有 owner entry `0x599888` 传入的 `X0=this`。该站点的 `X1` 是间接跳转目标寄存器，
不是第二个参数。

## 两项 IDA ABI 纠正

### `0x59B1A8` 的 live `X4` 不是参数

`PSBFile_ncbFactoryWrapper_arm64::method` 的二进制记录固定 callback 为四参：

```text
X0 = &p
W1 = numparams
X2 = param
X3 = objthis
```

原 Hex-Rays 表达曾把调用前仍 live 的 `X4=result` 追加成第五参。原始指令
`0x59B190..0x59B1A8` 只准备 `X0..X3`；将 call operand 绑定到 wrapper member 的四参
function-pointer type 后，fresh decompile 已变为
`self->method(&p, numparams, param, objthis)`。这说明恢复语义 prototype 不能按“调用点还有
哪些 volatile 寄存器存活”猜 arity。

### `0x597200/0x5973F0` 固定为八参 `FuncCall`

两处 callback 都由 vtable `+0x10` 的 `iTJSDispatch2::FuncCall` 槽约束。IDA call operand
现具有完整八参类型；fresh `EnumMembers@0x596F50` 明确显示
`self,flag,membername,hint,result,numparams,param,objthis` 对应 `X0..X7`。两处 `X0`
因 closure Object/ObjThis fallback 各有两条 reaching `LDP`，其余七个参数均为单来源。

## fresh 反编译与本地逐段对照

本轮 fresh 反编译/复核覆盖 `0x59673C`、`0x596F50`、`0x597F38`、`0x598268`、
`0x59B14C`、`0x59B570`，并读取 46 个 call operand 的 ctree/指令级 argloc。正常可达的
40 个站点均有 ctree call；六个 landing-only call 不强迫 Hex-Rays 把异常清理伪装成正常
源码路径，而是以 LSDA CFG 与原始指令闭合。

| Android ARM64 参数/生命周期 | 当前源码对照 |
| --- | --- |
| `EnumMembers@0x596F50` 两路 `FuncCall` 以同一 `callbackResult/params` 传 2 或 3 个成员参数 | `cpp/plugins/psbfile/main.cpp:371-440` 保留四只 Variant、`params[3]`、Array/Dictionary 两循环与 callback `objthis=this` |
| `Load@0x598268` 的 `std::function` manager 正常/landing 析构与 `LoadStorage` stream `GetSize`/delete | `cpp/plugins/psbfile/PSBRawFile.cpp:442-539` 保留 by-value Variant、`unique_ptr` stream、filter gate/invoke 与异常生命周期 |
| storage lister 两路回调都传 `lister + ttstr temporary` | `cpp/plugins/psbfile/PSBMedia.cpp:149-219` 保留 Array index string 与 Dictionary decoded-key string 的 `lister->Add` |
| media `Release@0x599888` 在 ref==1 时走 deleting destructor tail，否则只递减 | `cpp/plugins/psbfile/PSBMedia.h:12-23` 保留同一非原子生命周期边界 |
| adaptor `CreateNew/Release/NativeInstanceSupport` 顺序与失败边界 | `cpp/core/plugin/ncbind.hpp:156-225` 保留 class-object gate、global dispatch、dummy param、adaptor lookup 与 sticky state |
| `RegistItem` 四只 interface slot与 `RegistEnd` class/global ref 顺序 | `cpp/core/plugin/ncbind.hpp:835-904,1879-1917,2035-2052` 保留 `GetDispatch/GetType/GetFlags/Release` 和 `PropSet/Release` 分层 |
| factory callback 发布 `PSBFile **`，root/load 继续走 typed member pointer | `cpp/plugins/psbfile/main.cpp:690-755` 与 `cpp/core/plugin/ncb_invoke.hpp:3-95`、`cpp/core/plugin/ncbind.hpp:924-985,1164-1194` 保留 factory、member-pointer、paramsFunctor 与 MethodCaller 层次 |

未发现需要修改 `cpp/` 的差异。

## 目标逻辑摘要（不超过 10 行）

```text
enumerate all 45 non-switch BLR sites and the one non-switch BR tail
recover each semantic prototype from target vslot/callback/member-pointer ABI
select owner-entry normal CFG or only the LSDA landing CFGs reaching that site
for each ordered X0..X7 argument, walk every predecessor backwards
stop at the first exact writer, a declared call result/clobber, or entry parameter
reject any undeclared explicit writer or volatile BL/BLR boundary on a path
collect all branch-specific reaching sources and require exact set equality
require site/flow/arity/bank/type/source aggregates and machine words to match
```

## IDB 与机械门禁

两处 Enum callback、六个 landing-only call、media tail、`RegistEnd` join 与 factory callback
均已加入 `INDIRECT-ARG-SOURCE` 证据注释并保存；factory 四参和两处 `FuncCall` 八参 call
operand 类型已纠正。

`verify_elf_surface.py` 新增 2,687-byte canonical payload（SHA-256
`28d7f0da3aba2449f13ccee312ff76e10d66cfcf4d7fa9acb5766a0d828f178f`）。门禁会：

1. 要求 payload 站点集合与既有 46-site indirect ABI surface 完全相等；
2. 逐点核对 `BLR/BR` word、target register、flow class、role、arity、bank 与 type/size；
3. 从 ELF 重新构建 40 个 normal 站点的 entry-rooted CFG；
4. 解析 LSDA，并只合并可达六个 cleanup call 的 landing CFG；
5. 为 117 个参数重走 predecessor slice，逐字核对全部 120 个 source；
6. 在任何未声明 writer、entry residue 或 volatile call boundary 上失败。

新增通过输出：

```text
indirect_argument_surface=true owners=18 sites=46 normal=40 landing=6 blr=45 br=1 operands=117 relations=120 single_source=114 multi_source=3 max_sources=2 instruction=119 entry=1 call_return=0 call_clobber=0 entry_residue=0 paths_complete=true sha256=true
indirect_argument_abi=true x0=46 x1=23 x2=19 x3=13 x4=6 x5=4 x6=3 x7=3 pointer=89 signed=16 unsigned=12 max_arity=8 target_registers=5 semantic_roles=18
indirect_argument_source_classes=true memory=17 transfer=77 address=23 select=2 volatile_call_clobbers=0 producer_classes=6
```

15 个 stripped/O3 identifier/factorization 上限保持不变；本轮新增的是间接调用输入数据流、
异常清理参数、callback/member-pointer arity 与临时对象生命周期的正证据，不需要也不允许
转向 ARMv7、废弃私库、同版本源码或 Git LFS。
