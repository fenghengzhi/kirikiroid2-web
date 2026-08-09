# Follow-up：114-entry normal CFG / terminal contract surface

`NORMAL-CFG-TERMINAL-CONTRACT`

## 结论

- 权威 Android ARM64 `libkrkr2.so` 的 114 个 psbfile FDE 共有 **5,525 条**机器指令。
  从每个函数入口只沿显式 ARM64 正常边展开得到 **4,956 条**；上一轮 150 个 LSDA
  landing 的唯一指令集合为 **569 条**。两者 **0 overlap**，并且
  `4,956 + 569 = 5,525`，恰好完整覆盖全部 FDE 指令。
- 4,956 条正常流指令共有 5,337 条显式 successor edge 和 **208 个终点**：
  **162 RET + 11 direct tail + 1 indirect tail + 34 true-noreturn BL**。
- 114/114 当前 IDB prototype 的 source-facing 返回 ABI 精确分为：
  **37 void + 71 W0/X0 + 5 hidden-sret X8 + 1 D0**。不能把 void 构造器尾部残留的
  incoming `X0` 当作源码返回值，也不能把 non-trivial hidden-sret 简化成普通指针返回。
- landing 专用 noreturn 集不能复用于正常流。正常流中 4 个 TVP/TJS diagnostic target
  共 28 个调用点都保留显式 fallthrough/default；真正没有 continuation 的只有
  `__stack_chk_fail` 31 点、`std::bad_alloc` 1 点和 `std::length_error` 2 点。
- fresh 反编译与当前源码逐项对照后，返回 ABI、default continuation、hidden-sret 构造、
  void tail 和本地 RAII 均一致；本轮没有发现新的 `cpp/` GAP，因此没有修改生产代码。

本轮只使用 Android ARM64 二进制、IDA MCP 与当前工作树；没有使用 Android ARMv7、
iOS ARMv7、旧私库、LFS 对象或同版本源码。

## 为什么 normal CFG 必须使用独立 noreturn 语义

上一轮的 `EXPECTED_LANDING_PAD_NORETURN_TARGETS` 用于异常 cleanup closure。它包含
TVP/TJS throw helper，因为 cleanup 路径不应穿过这些调用继续执行；但该集合的注释也明确
说明它不是“所有调用点均为源码 noreturn”的泛化结论。

正常业务函数恰好保留多条 helper-return 边界：

| 调用点 | helper | emitted continuation |
| --- | --- | --- |
| `0x597678` | `TVPThrowExceptionMessage_guess` | `MOV W0,#-1002`，`GetCount` 返回 `TJS_E_NOTIMPL` |
| `0x598CF8` | `TVPThrowExceptionMessage_ttstr_guess` | 释放临时 ttstr，`STP XZR,XZR,[result]` 返回空 raw node |
| `0x5993E0` | `TVPThrowExceptionMessage_guess` | `FMOV D0,XZR`，`GetDouble` 返回 `0.0` |
| `0x59951C` | `TVPThrowExceptionMessage_guess` | `MOV W0,WZR`，`GetInt` 返回 `0` |
| `0x5995C8` | `TVPThrowExceptionMessage_guess` | `MOV W0,#-1`，classifier 返回 unknown category |

全量 28 个 continuation call 的 target 计数为：

| target | 站点 | 语义 |
| --- | ---: | --- |
| `0x95440C` | 22 | TVP 固定消息 diagnostic |
| `0x95458C` | 3 | TVP 带 ttstr 参数 diagnostic |
| `0xA0E48C` | 1 | Variant conversion error |
| `0xA0E9EC` | 2 | TJS null-access error |

若沿用 landing 集，这 28 点会被错误截断为终点，并漏掉 37 条真实 fallback 指令。本轮的
normal decoder 只把目标内确有无 continuation 的三类调用作为终点。

## 显式 CFG 伪代码（9 行）

```text
for each owner FDE: graph = DFS(owner)
  BL true_noreturn => terminal；其他 BL => fallthrough
  B inside FDE => target；B outside FDE => direct tail
  B.cond/CBZ/TBZ => target + fallthrough
  switch BR => 既有 42 张表的全部目的地；其他 BR => indirect tail
  RET/BRK/HLT/ERET => terminal；普通指令 => fallthrough
for each LSDA landing: landingGraph = 既有 landing 专用 decoder
assert normalGraph ∩ landingGraph == ∅
assert normalGraph ∪ landingGraph == every instruction in all 114 FDEs
```

没有从 IDA FlowChart 导入 EH pseudo edge；所有 successor 都由 ELF 指令 word 与既有 42 张
switch table 直接解码。

## 完整机器面

| 项目 | 数量 |
| --- | ---: |
| FDE / 返回 ABI prototype | 114 / 114 |
| 全部 FDE 指令 | 5,525 |
| 正常流指令 | 4,956 |
| landing-only 唯一指令 | 569 |
| normal/landing overlap | 0 |
| 正常 successor edge | 5,337 |
| 正常终点 | 208 |
| `RET` | 162 |
| direct / indirect tail | 11 / 1 |
| true-noreturn `BL` | 34 |
| continuing diagnostic `BL` | 28 |

正常指令族计数：

| 类别 | 数量 | 类别 | 数量 |
| --- | ---: | --- | ---: |
| ordinary | 3,726 | fallthrough `BL` | 272 |
| true-noreturn `BL` | 34 | in-FDE `B` | 232 |
| direct tail `B` | 11 | `B.cond` | 180 |
| `CBZ/CBNZ` | 216 | `TBZ/TBNZ` | 41 |
| `BLR` | 39 | switch `BR` | 42 |
| indirect tail `BR` | 1 | `RET` | 162 |

每函数终点数分布为：65 个函数 1 个、36 个函数 2 个、7 个函数 3 个、2 个函数 4 个、
1 个函数 6 个、1 个函数 8 个、2 个函数 14 个。14-exit 两函数正是
`GetDouble@0x5992E8` 与 `GetInt@0x599438`；8-exit 为 classifier `0x599554`，6-exit 为
dispatch `GetCount@0x5975E0`。

## 返回 ABI 与终点交叉表

| ABI 类别 | 函数 | `RET` | direct tail | indirect tail | true-noreturn |
| --- | ---: | ---: | ---: | ---: | ---: |
| `void` | 37 | 28 | 11 | 1 | 11 |
| `W0/X0` | 71 | 115 | 0 | 0 | 20 |
| hidden-sret `X8` | 5 | 5 | 0 | 0 | 3 |
| `D0` | 1 | 14 | 0 | 0 | 0 |

五个 hidden-sret 入口为：

1. `PSBFile_GetRoot_guess@0x598A3C`：通过 `X8` 写 owner/node 并 retain owner；
2. `PSBFile_Transfer_guess@0x598A64`：先向 `X8` 发布 owner，再处理 incoming-zero 销毁边并
   清空 source；
3. `PSBRawNode_GetDictionaryValueStrict_guess@0x598C58`：hit 构造 retained child；
   diagnostic-return miss 在 `0x598D08` 清零两个结果槽；
4. `PSBRawNode_GetDictionaryKeys_guess@0x598E64`：在 `X8` 构造三指针
   `std::vector<std::string>`，非 Dictionary 返回已构造的空 vector；
5. `PSBFile_loadMethod_CopyFirstArgument_guess@0x59B708`：按值复制首参数并在 `0x59B778`
   直接 copy-construct 到 `X8` Variant 结果。

唯一 `D0` 函数 `GetDouble@0x5992E8` 的 14 个 return block 分别保留 bool、窄/宽整数、
float/double 与 diagnostic-return `0.0`。`GetInt@0x599438` 的 14 个 `W0` block 则保留
tag `0x0B` 只取 low word、float/double 截断和 diagnostic-return `0`；二者不能合并成同一
宽整数 return producer。

## tail contract

11 个 direct tail 的 target 计数为：

| target | 站点 | 语义 |
| --- | ---: | --- |
| `operator delete@0x415740` | 6 | deleting-destructor wrapper |
| `ttstr/narrow helper@0x54DEFC` | 1 | void string assignment tail |
| `PSB_DecodeName_guess@0x597B1C` | 1 | source-level name decoder tail |
| `RegistItem_guess@0x59AEEC` | 1 | NCB member registration tail |
| storage register media `0x8EA2C8` | 1 | pre-register tail |
| `tTJSVariant_dtor_guess@0xA0F778` | 1 | PSBMedia complete-destructor tail |

唯一 indirect tail 是 `PSBMedia_Release_guess@0x599888` 的 `0x59989C: BR X1`：
`refCount==1` 时 tail 到 deleting destructor；否则减计数后由 `0x5998A4` 本地 `RET`。
两条路径都是 `void`，不应为 tail 人工制造 result 局部量。

## fresh Android 证据与当前源码对照

本轮 fresh `decompile`：`0x59673C`、`0x5975E0`、`0x598A3C`、`0x598A64`、
`0x598AAC`、`0x598C58`、`0x598E64`、`0x5992E8`、`0x599438`、`0x599554`、
`0x599888`、`0x59B708`。另用原始反汇编核实上述 fallback word。

| Android contract | 当前源码 |
| --- | --- |
| `CreateVariant` 返回原 `result`，classifier/convert throw-return 仍进入各自默认赋值或保持旧值 | `cpp/plugins/psbfile/main.cpp:557-687` 保留同一 pointer 返回及每类默认值 |
| `GetCount` 的 invalid object/member/type/count 分支分别返回 `-1006/-1002/-1002/0+success` | `cpp/plugins/psbfile/main.cpp:287-329` 保留同一 guard、count 与 TJS error |
| owner constructor 是 `void`，data-null 不写 header view | `cpp/plugins/psbfile/PSBRawFile.cpp:137-168` 不返回 self 且保留 null gate |
| strict lookup/keys/root/transfer 使用 X8 non-trivial return | `PSBRawFile.cpp:249-306,430-439,542-545` 以普通 C++ return 让各目标 ABI自行产生 hidden-sret |
| `GetInt/GetDouble/GetTypeCategory` 的全部 tag/default return | `PSBRawFile.cpp:309-364` 与 `PSBPackedInternal.h` 保留分类、宽度和 fallback |
| media Release 的 delete tail / decrement RET | `cpp/plugins/psbfile/PSBMedia.h:18-23` 保留同一两路对象生命周期 |
| typed load wrapper 的首 Variant 参数 copy chain | `cpp/core/plugin/ncbind.hpp:954-964` 继续由 paramsFunctor 按值提取，未改成借用引用 |

因此本轮结论为 `HAS_GAP: 0`。它不是以“测试看不到”为理由跳过实现，而是机器出口、
fresh 伪代码和当前 source-facing 表达三方逐项相同。

## verifier gate

`verify_elf_surface.py` 现固定：

1. 114 个 owner 的 4 类返回 ABI 集合；
2. 4,956 个正常流指令 word、5,337 条 successor 与 canonical digest；
3. 208 个终点的类别、target/register 和 per-owner 分布；
4. 28 个 continuing throw 与 34 个 true-noreturn 站点的完整 target 计数；
5. 12 个 tail 的 target/寄存器/void ABI；
6. 正常 4,956 与 landing 569 对全部 5,525 指令的零交叉完整分区。

当前输出：

```text
normal_cfg_terminal_surface=true functions=114 normal_instructions=4956 landing_instructions=569 fde_instructions=5525 edges=5337 terminals=208 returns=162 direct_tails=11 indirect_tails=1 noreturn_calls=34 partition_complete=true overlap=0
normal_return_abi=true classes=4 continuing_throw_calls=28 continuing_throw_targets=4 noreturn_targets=3 direct_tail_targets=6 prototype_classes=true successors=true sha256=true
```

后续已在这套同一 normal CFG 上把 129 个 source-visible W0/X0/D0 `RET` 继续回溯到
160 条完整 producer 关系，固定 112 个单来源与 17 个多来源返回点。逐 RET 默认值、
call-return 与 fresh 源码对照见
[FOLLOWUP_NORMAL_RETURN_VALUE_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_RETURN_VALUE_SURFACE_2026-08-03.md)。

canonical normal CFG SHA-256：
`13a423bbec5317bda994eef7e6de36ac03adae982a9d73770b8b9ffc34b00b7c`。

复现：

```bash
python3 analysis/psbfile_function_audit_2026-07-25/verify_elf_surface.py \
  --llvm-dwarfdump /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/llvm-dwarfdump
```

## IDB 与实现状态

- 已向 11 个关键 fallback/hidden-sret/tail 站点追加 `NORMAL-EXIT-CONTRACT` 注释，并保存
  当前 IDB；已有 typed-member/stack-lifetime 注释未被覆盖。
- 没有修改 `cpp/`，所以本轮不触发 Web 构建。
- 114 项审计统计继续为 `ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`；本轮新增的是
  全量正常控制流、返回 ABI与边界 continuation 的独立机械证据，不改变 stripped/O3
  精确 identifier/token 的 15 项上限。
