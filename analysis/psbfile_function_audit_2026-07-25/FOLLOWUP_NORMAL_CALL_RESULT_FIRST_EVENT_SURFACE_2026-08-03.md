# normal call-result 首事件表面复核（2026-08-03）

## 结论

本轮以 Android ARM64 `libkrkr2.so` 的 entry-rooted normal CFG 为唯一权威，补齐了
`FOLLOWUP_NORMAL_RETURN_VALUE_SURFACE_2026-08-03.md` 的正向互补面：从每个会继续执行的
`BL/BLR` fallthrough 出发，沿所有有限 CFG 路径追踪物理 `W0/X0`，直到第一次出现显式
读取、无读取覆盖、下一调用边界或 `RET`。

结果为 **ALIGNED / 无生产代码 GAP**：311 个调用点、419 条首事件关系全部闭合；147 个
direct-GPR、125 个 direct-void 与 39 个 indirect 调用的结果流均与当前源码的返回使用、
临时对象、refcount、隐藏返回对象及 wrapper 错误码路径一致。尤其是 125 个 direct-void
调用没有一条路径显式读取其 `W0/X0` 残留。没有修改 `cpp/`，没有创建或修改 fixture，
因此不触发构建。

## 表面定义

纳入范围是 114 个 MANIFEST owner 的 normal-entry 可达图中，拥有 fallthrough successor
的全部调用：

| 调用类 | site | 结果类 |
| --- | ---: | --- |
| direct `BL` | 272 | 147 direct-GPR + 125 direct-void |
| indirect `BLR` | 39 | 39 indirect |
| 合计 | 311 | 311 |

直接目标的返回类来自本轮完整 IDB prototype sweep：54 个唯一 direct-GPR target、21 个
唯一 direct-void target。唯一缺少导入 prototype 的 `0x423250` 由二进制自身 mangled
symbol `std::vector<std::string>::_M_emplace_back_aux<...>` 明确约束为 `void`。没有 direct
FP-return 调用；本表只需追踪 AArch64 的同一物理 GPR0 bank。

事件优先级为：

1. 指令通过 IDA canonical `CF_USE1..6` 显式读取 `W0/X0`：`use`；
2. 未读取而通过 `CF_CHG1..6` 改写 `W0/X0`：`overwrite`；
3. 未发生前两项而到达下一 `BL/BLR`：`call-boundary`；
4. 未发生前三项而到达 `RET`：`ret-reaches`。

`call-boundary` 是刻意中性的机器边界：旧 `X0` 可能成为下一 callee 的 arg0，同时也会在
调用后成为 volatile 返回寄存器；没有独立 callee contract 时，不把它武断标成“消费”或
“丢弃”。`ret-reaches` 也只表示寄存器值到达机器 `RET`，不自动等于 source-facing 返回值。

## 完整计数

| 首事件关系 | 数量 | 说明 |
| --- | ---: | --- |
| `use` | 86 | 71 direct-GPR + 15 indirect；direct-void 为 0 |
| `overwrite` | 250 | 首次改写前没有读取旧返回寄存器 |
| `call-boundary` | 49 | 全部到 direct `BL`；语义保持中性 |
| `ret-reaches` | 34 | 全部属于非 GPR-result owner，见下节 |
| **合计** | **419** | 311 个调用点的完整 first-event 关系 |

事件 cardinality 为：

```text
1 -> 234 calls
2 -> 57 calls
3 -> 11 calls
4 -> 7 calls
5 -> 2 calls
```

86 条显式读取精确分为：

| 用途 | 数量 | 指令 |
| --- | ---: | --- |
| register transfer | 45 | `MOV` |
| temporary/object storage | 9 | `STR` |
| predicate/error/index gate | 30 | `2 CBNZ + 4 CBZ + 1 CMN + 3 CMP + 6 TBNZ + 14 TBZ` |
| signed wrapper transform | 2 | `MVN` |

不存在同时读取并改写 GPR0 的 first event；每个 event 的地址、32-bit instruction word、
mnemonic、operand-use mask 与 operand-change mask 都进入 canonical payload。

## `RET` 残留不等于源码返回

34 条 `ret-reaches` 按 owner source-facing ABI 精确分为：

| owner ABI | 关系 | 解释 |
| --- | ---: | --- |
| void | 28 | 析构、Release、注册与容器 helper 的 GPR 残留 |
| hidden-sret `X8` | 5 | 源码对象结果写入 `X8` 指向的存储，不由 GPR0 返回 |
| FP `D0` | 1 | `GetDouble@0x5992E8` 的源码结果在 `D0` |
| GPR `W0/X0` | **0** | 没有把未读 call residue误判为源码返回 |

因此 `DecodeName@0x597B1C` 的 `std::string::assign`、`Transfer@0x598A64` 的 delete、
`GetDouble@0x5992E8` 的 diagnostic helper、`RegistItem@0x59AEEC` 的 item Release 等
路径即使机器 GPR0 能到某个 `RET`，也不代表这些 void/hidden-sret/FP owner 返回该值。

## 四条首事件前循环

前向 CFG 还发现四个调用结果在第一次事件前穿过循环。它们不是未闭合路径：每个循环的
有限 exit 都到达声明事件，非事件 terminal 为 0；但循环携带事实本身属于数据流证据，
不能用一次线性扫描抹掉。

| call | 循环 | exit 后首事件 |
| --- | --- | --- |
| `tTJSVariant_dtor@0x599FCC` | `0x599FD8..0x599FE4` atomic retain | `overwrite@0x599FE8` |
| `ttstr_SubString@0x59A5C0` | `0x59A5CC..0x59A5D4` atomic retain | `overwrite@0x59A5E4/0x59A5F4` |
| `ttstr_SubString@0x59A5FC` | `0x59A608..0x59A610` atomic retain | `overwrite@0x59A618` |
| string copy ctor `0x59B884` | `0x59B8AC..0x59B8BC` vector move loop | cleanup/return 分支上的 4 个声明事件 |

前两只 `SubString` 的 source-facing `ttstr` 结果通过 hidden `X8` 写入局部对象；循环中保留的
GPR0 只是非语义残留。vector copy ctor 则把语义对象构造在新 vector slot，GPR0 同样不是
容器元素本体。

## 关键调用链证据

| producer call | first event | 还原约束 |
| --- | --- | --- |
| `TJSAllocVariantOctet@0x596B5C` | `STR X0@0x596B60` | 临时 Variant 先持有 Octet，再 CopyRef、析构，refcount 链不能折叠 |
| stream `GetSize BLR@0x5985A0` | `CMP X0,#9@0x5985A4` | 首次查询只做最小长度 gate |
| stream `GetSize BLR@0x5985B8` | `MOV X22,X0@0x5985BC` | 第二次查询保存为 allocation/read size |
| `Adopt@0x59860C` | `TBZ W0,#0@0x598610` | storage load 的 false/true 分支直接由 Adopt bool 决定 |
| `wcscmp_utf16@0x599EC0` | `CBZ W0@0x599EC4` | cached-container equality 使用 compare 返回值 |
| factory callback `BLR@0x59B1A8` | `CBNZ W0@0x59B1AC` | 非零 callback 错误码沿后续路径原样返回 |
| root Invoke `BL@0x59B334/0x59B418` | `MVN@0x59B338/0x59B41C` | getter/setter wrapper 保留二进制的 signed normalize 序列 |
| item Release `BLR@0x59B0A8` | void owner 的 `RET/call` 边界 | Release 结果从不作为注册函数返回值读取 |

## fresh 反编译与本地对照

本轮 fresh `decompile/analyze_batch` 覆盖：

```text
0x59673C 0x597B1C 0x598538 0x598A64 0x598C58 0x5992E8
0x599E04 0x59A4B0 0x59AEEC 0x59B14C 0x59B28C 0x59B378 0x59B7E8
```

| Android ARM64 数据流 | 当前源码逐行复刻 |
| --- | --- |
| `CreateVariant@0x59673C` 的 Octet call result 先落临时 Variant，再 CopyRef/dtor | `main.cpp:645-666` 保留同一临时对象与 refcount 生命周期 |
| `LoadStorage@0x598538` 两次 vcall size、read、decode、Adopt bool 顺序 | `PSBRawFile.cpp:482-513` 保留两次 `GetSize`、raw allocation、replacement 与 Adopt 返回 |
| strict lookup `0x598C58` 的两级 bool gate、hidden-sret 与 retain | `PSBRawFile.cpp:249-265` 保留短路 throw/default continuation 和 retained child |
| `EnsureContainer@0x599E04` 的 compare/load/adaptor/Variant/container 生命周期 | `PSBMedia.cpp:19-49` 保留 cache gate、失败 delete、adaptor-null Variant 与 container 更新 |
| `Resolve@0x59A4B0` 的 hidden-sret substring、atomic ref no-op、strict lookup temporary | `PSBMedia.cpp:52-109` 保留 segment temporary、rest assignment、narrow holder 与 delayed output update |
| factory/root wrappers的 callback/dispatch result、错误码与 `MVN/SBFX` 归一化 | `main.cpp:704-755` 与 `ncbind.hpp` 生成层保持 factory/converter/property member-pointer 路径 |
| vector growth helper `0x59B7E8` 的 in-place COW pointer move、old-element release、storage replace | `GetDictionaryKeys` 保留源码层 `std::vector<std::string>::reserve/emplace_back` 的容器选择与操作顺序；ARM64 helper 是目标 gnustl 的具体展开 |

未发现需要改动 `cpp/` 的差异。

## 目标逻辑摘要（不超过 10 行）

```text
for each continuing normal BL/BLR, start at its fallthrough
walk every successor until physical W0/X0 is first read or overwritten
if neither happens, stop at the next call boundary or RET
record every branch-specific first event with exact word and operand masks
classify direct targets as GPR or void from IDB prototype/binary symbol evidence
direct-void results must never reach an explicit W0/X0 use
RET reachability is semantic only when the owner ABI itself returns GPR0
record pre-event loops separately; every finite loop exit must reach an event
reject undeclared call/RET/explicit-event sites and non-event terminals
```

## IDB 与机械门禁

已在 13 个高风险 producer/consumer/loop-residue site 追加
`NORMAL-CALL-RESULT-FIRST-EVENT` 注释并保存 IDB。

`verify_elf_surface.py` 新增 10,937-byte canonical payload（SHA-256
`b0ee7a5f982779bd3e1b8c4a8ed0f5782ba43f540c85d2b737440298decc95d1`），会重建 normal
CFG、重新枚举 311 个 continuing call、核对 direct target/BLR register/result class、逐事件
word/mask/role，并从每个 fallthrough 重走 first-event 图。新增输出：

```text
normal_call_result_first_event_surface=true owners=57 calls=311 direct=272 indirect=39 relations=419 single_event=234 multi_event=77 max_events=5 use=86 overwrite=250 call_boundary=49 ret_reaches=34 pre_event_loop_rows=4 unresolved_terminals=0 finite_paths_complete=true sha256=true
normal_call_result_classes=true direct_gpr=147 direct_void=125 indirect=39 direct_gpr_uses=71 direct_void_uses=0 indirect_uses=15 move=45 store=9 predicate=30 transform=2 direct_gpr_targets=54 direct_void_targets=21 use_classes=9
```

15 个 stripped/O3 identifier/factorization 上限保持不变；本轮新增的是调用结果消费、对象
生命周期与边界寄存器残留的正证据，不需要也不允许转向 ARMv7、废弃私库或 Git LFS。
