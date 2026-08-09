# normal direct-call 参数 producer 表面复核（2026-08-03）

## 结论

本轮继续只以 Android ARM64 `libkrkr2.so` 的 entry-rooted normal CFG 和 IDA 类型信息为
权威，补齐调用结果表面的输入侧互补面：枚举全部普通直接 `BL` 与跨 owner 的直接尾调用
`B`，恢复每个目标的 AAPCS64 参数位置，再从调用点沿所有 normal predecessor 反向追踪每个
参数寄存器的完整 reaching producer 集。

结果为 **ALIGNED / 无生产代码 GAP**：317 个直接转移点、446 个参数与 475 条来源关系全部
闭合；没有未声明的 volatile call clobber、入口残值、stack 参数或 scattered argloc。当前
`CreateVariant`、packed 枚举、raw load/name/vector、media cache/path lookup、NCB 注册与
typed wrapper 的参数生产顺序均与二进制一致。本轮没有修改 `cpp/`、fixture 或测试物料，
因此不触发构建。

## 覆盖定义

范围是 114 个 MANIFEST owner 的正常入口可达图：

| 转移类 | site | 说明 |
| --- | ---: | --- |
| direct `BL` | 306 | 包含 34 个 true-noreturn call；不只统计有 fallthrough 的调用 |
| out-of-owner direct tail `B` | 11 | 目标位于当前 FDE 外 |
| **合计** | **317** | 59 个 caller owner、80 个唯一 target |

目标 prototype 的 argloc 来自完整 IDB 类型面。`0x406D70` 是零参数、noreturn 的 stack
check fail；`0x423250` 的 PLT 名字本身表明 vector emplace helper，实际实现
`0x59B7E8` 的两参数 prototype 固定其 `X0/X1` 布局。其余直接目标均有可用 tinfo。全部
446 个参数都是 `reg1`；没有 stack、register pair 或 scattered 参数。

## ABI 参数面

| bank | 参数数 | 主要用途 |
| --- | ---: | --- |
| `X0` | 280 | `this`、result/out、allocation pointer、free/delete/Release 参数 |
| `X1` | 97 | value/key/name/source/member literal |
| `X2` | 48 | index/size/out pointer/item/type tag |
| `X3` | 7 | filter、callback flag、hidden helper 参数 |
| `X4` | 3 | callback/stream 参数 |
| `X5` | 3 | callback/stream 参数 |
| `X8` | 7 | non-trivial hidden result storage |
| `D0` | 1 | `CreateVariant@0x596A34` 的 double assignment |

arity 精确为：

```text
0 -> 37 sites
1 -> 181 sites
2 -> 50 sites
3 -> 37 sites
4 -> 9 sites
6 -> 3 sites
```

IDA 参数类型按 ABI 存储类与宽度固定为：

```text
pointer/8 = 291
bool/1 = 2
signed/1,4,8 = 5,16,71
unsigned/2,4,8 = 3,23,24
integral-unspecified/8 = 10
floating/8 = 1
```

## 完整来源关系

446 个参数形成 475 条 producer 关系：

| 来源角色 | 关系 | 约束 |
| --- | ---: | --- |
| explicit instruction | 447 | exact address、word、mnemonic、destination operand |
| entry parameter | 18 | 正常入口传入并沿该路径未被覆盖 |
| preceding direct-call return | 10 | 仅 `X0`，在前序 `BL` 处停止 |
| call clobber | 0 | 没有把易失参数跨调用误连 |
| entry residue | 0 | 没有把非参数寄存器初值伪装成来源 |

cardinality 为 `435×1 + 7×2 + 2×4 + 1×5 + 1×13 = 475`；11 个参数是多来源，最大
汇合为 13 路。447 个显式 instruction source 分成：

```text
80  memory       = 2 LDP + 74 LDR + 1 LDRB + 1 LDRH + 2 LDUR
257 transfer     = 214 MOV + 43 ADRL
107 arithmetic   = 80 ADD + 6 AND + 3 BFI + 1 CSINV + 2 FCVTZS
                   + 2 LSL + 8 SUB + 5 SXTW
3   writeback    = pre-index STR base update
```

## 11 个多来源参数

| call / 参数 | 来源摘要 | 源码约束 |
| --- | --- | --- |
| `5968D0` `X1` | 2 个 `MOV` | bool true/false 分支 |
| `596A14` `X1` | 13 个 `MOV/SXTW/BFI/FCVTZS/LDUR` | 全部 integer raw-tag/default 分支 |
| `596B5C` `X1` | `AND/MOV` | packed resource length/default |
| `596E88` `X1` | 4 个 `ADRL` | 四个 class literal |
| `597350` `X2` | `AND/MOV` | Dictionary key index |
| `5979F8` `X1` | `LDRB/MOV/LDRH/AND/LDUR` | Array packed count |
| `597E10` `X0` | entry `X0` + `LDP` operand 1 | vector growth 前后恢复 string result |
| `598FCC` `X2` | `AND/MOV` | raw Dictionary key index |
| `599C30` `X2` | `AND/MOV` | media Dictionary key index |
| `59A004` `X0` | 4 个 `LDR` | EnsureContainer 的四条 owner Release 路径 |
| `59A7E8` `X0` | 2 个 `LDR` | Resolve 的 owner Release 路径 |

## hidden `X8`、`D0` 与 call-return 转发

七个 hidden-result 参数是：

```text
598570 TVPGetPlacedPath         X8 <- MOV@59856C
599E54 ttstr::SubString         X8 <- ADD@599E44
59A588 ttstr::SubString         X8 <- ADD@59A57C
59A5C0 ttstr::SubString         X8 <- ADD@59A5B0
59A5FC ttstr::SubString         X8 <- ADD@59A5F0
59A694 strict dictionary lookup X8 <- MOV@59A68C
59B634 CopyFirstArgument        X8 <- ADD@59B624
```

唯一 `D0` 参数是 `assign_double@0x596A34`，由 `MOV@0x596A30` 产生。十条直接 call-return
转发覆盖注册 wrapper 的三次 allocation→ctor、`Load` 的 allocation→`memcpy/ttstr/
uncompress`、`Open` 的 allocation→constructor，以及 `RegistItem` 的 string-concat/
Release 链。

其中 `Release@0x59AFE8` 的 `X0` 正确来自 `BL@0x59AFCC`。早期机械枚举曾把
`STLXR W9,X8,[X0]@0x59AFE0` 的 memory base `X0` 错当成被写寄存器；复核 IDA operand 与 AArch64
语义后已纠正：普通 memory operand 不写 base，只有显式 `!` writeback 才写。三条真实
writeback source 全是注册体里的 `STR Xn,[X2,#0x20]!`，其 operand 1 定义 `X2`。

## 11 条直接尾调用

- `decodeName` member wrapper 尾调共享 `DecodeName`，保留 entry `W2`；
- `DecodeName` 析构尾调 delete；
- NCB `registerMembers` 最后一项尾调 `RegistItem`，`X2` 来自 pre-index `STR` 写回；
- media pre-register 尾调 storage-media 注册；
- PSBMedia complete/deleting destructor 尾调 base destructor/delete；
- `GetName` 尾调 ttstr wide assign；
- adaptor 及三只 typed wrapper deleting destructor 尾调 delete。

这些 tail 的参数源、恢复 frame 后的寄存器值与当前 source-level thin wrapper/destructor
边界一致。

## fresh 反编译与本地逐段对照

本轮 fresh `decompile/analyze_batch` 覆盖：

```text
59673C 596F50 597854 597B1C 597F38 598268 598E64
5999F4 599E04 59A4B0 59AEEC 59B570
```

| Android ARM64 参数数据流 | 当前源码对照 |
| --- | --- |
| `CreateVariant@0x59673C` 的 bool/int/double/String/Resource/container 每个 call 参数均从原 raw-tag 分支产生 | `main.cpp:557-688` 保留 category 层、内部 tag switch、默认值与临时 Variant 生命周期 |
| `EnumMembers@0x596F50` 的 packed key/count、DecodeName index、callback params 顺序 | `main.cpp:371-449` 保留 Array/Dictionary 两路、同一 params 数组与 no-value arity |
| `PropGet@0x597854` 的五路 packed count 到 signed int32 assignment | `main.cpp:121-204` 保留 `0x0D..0x10/default` 与无 null-output guard |
| `DecodeName@0x597B1C` 的 result pointer 在 vector growth 后恢复，再传 `assign(data,size)` | `PSBRawFile.cpp:112-135` 保留 byte vector、reverse 与同一 two-argument assign |
| registration `0x597F38` 的 factory/root/load 三项及最后 pre-index writeback tail | `main.cpp:732-755` 继续由 NCB `Factory/Property/Method` 生成同一对象链 |
| `Load@0x598268` 的 allocation return 直接转发给 `memcpy/uncompress`，string path 新建 ttstr | `PSBRawFile.cpp:442-479` 保留 by-value Variant、MDF decode、copy 与 fresh ttstr |
| raw/media Dictionary DecodeName 的 packed key join 与 vector/lister 生命周期 | `PSBRawFile.cpp:280-306`、`PSBMedia.cpp:149-219` 保留 shared DecodeName、reserve/emplace 与 callback 顺序 |
| media `EnsureContainer/Resolve` 的 hidden-sret substring/strict lookup、Release 多路来源 | `PSBMedia.cpp:19-109` 保留 cache gate、segment temporary、narrow holder 与 delayed output commit |
| load typed wrapper `0x59B570` 的 `X8` Variant result和两只空 type-tag temporary | `ncbind.hpp:928-964,1164-1188` 保留 paramsFunctor extraction 与 member invoke 分层 |

未发现需要修改 `cpp/` 的差异。

## 目标逻辑摘要（不超过 10 行）

```text
enumerate every normal direct BL and every B whose target leaves its owner FDE
recover the target prototype's ordered AAPCS64 register argument locations
for each argument, start from every predecessor of the transfer site
stop at an exact instruction writer, declared preceding-BL return, or entry
an intervening BL/BLR clobbers every recorded volatile bank unless declared
only explicit writeback memory operands may define their base register
collect every branch-specific source and reject undeclared explicit writers
require all paths to close and all site/argument/source sets to match exactly
```

## IDB 与机械门禁

已在 18 个高风险 call/source site 追加 `DIRECT-ARG-SOURCE` 注释并保存 IDB。

`verify_elf_surface.py` 新增 13,161-byte canonical payload（SHA-256
`8a6a8d544bcf728c34de596aaa918c3f53c410959b8f4f3ea3c546370122cc4f`）。校验器会重新构建
114 个 normal CFG、重新枚举 317 个 transfer、核对 target/word/arity/bank/type/size，并为
446 个参数重走 predecessor slice。新增输出：

```text
normal_direct_argument_surface=true owners=59 sites=317 bl=306 direct_tails=11 targets=80 zero_arg=37 operands=446 relations=475 single_source=435 multi_source=11 max_sources=13 instruction=447 entry=18 call_return=10 call_clobber=0 entry_residue=0 paths_complete=true sha256=true
normal_direct_argument_abi=true x0=280 x1=97 x2=48 x3=7 x4=3 x5=3 x8=7 d0=1 pointer=291 bool=2 signed=92 unsigned=50 integral_unspecified=10 floating=1 max_arity=6
normal_direct_argument_source_classes=true memory=80 transfer=257 arithmetic=107 writeback=3 volatile_call_clobbers=0 producer_classes=18
```

15 个 stripped/O3 identifier/factorization 上限保持不变；本轮新增的是调用输入数据流、
hidden-result、容器临时和 writeback 边界的正证据，不需要也不允许转向 ARMv7、废弃私库或
Git LFS。
