# Follow-up：Android ARM64 ELF LSDA exception surface 全量闭环

日期：`2026-08-03`。本轮只读取权威 Android ARM64
`reference/libkrkr2/libkrkr2.so`，没有修改 `cpp/`、测试物料或二进制。

## 结论

- 权威 SHA-256 仍为
  `ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`。
- 114 个 MANIFEST FDE 中，**39** 个带 personality/LSDA，**75** 个只有 unwind
  FDE；其中两只 static-init FDE 都不带 LSDA，主 surface 为
  `39 LSDA + 73 unwind-only = 112`。
- 39 个 LSDA 入口与 MANIFEST、连续 FDE surface 完全一致；LSDA 地址全部唯一且位于
  `.gcc_except_table [0x1857A70,0x19D943C)`。
- 逐函数报告交叉复扫找到一个真实的审计记录遗漏：
  `PSBRawNode_GetDictionaryValue_guess@0x598D58` 是唯一带 LSDA、但旧报告完全没有
  EH/landing 证据的入口。fresh IDA、原始 LSDA bytes 和 caller LSDA 已补齐；本地
  exception-spec 与对象清理顺序一致，故判定仍为 `ALIGNED`，不是生产 GAP。
- 总统计保持 `99 ALIGNED / 15 EVIDENCE_LIMITED / 0 HAS_GAP`。

## 机械方法

1. 用 `llvm-dwarfdump --eh-frame` 读取完整 ELF FDE；不以 IDA 函数边界代替 ELF
   unwind 元数据。
2. 解析每个 FDE 的 `[start,end)` 与可选 `LSDA Address`，再和 114-entry MANIFEST
   做集合、连续性和 next-module boundary 比较。
3. 对 LSDA-bearing start 再做精确集合比较；地址必须唯一并落在权威
   `.gcc_except_table` section 内。
4. 对所有 39 份逐函数报告搜索 EH/LSDA/landing/cleanup 证据，定位到唯一遗漏
   `0x598D58` 后，fresh 反编译目标和 canonical parent，并直接解码两份 call-site
   table。该 39/39 报告覆盖也已进入 verifier，缺文件或完全没有 EH marker 都会失败。

`verify_elf_surface.py` 已扩展为同时验证 FDE 与 LSDA surface。可复现命令：

```bash
python3 analysis/psbfile_function_audit_2026-07-25/verify_elf_surface.py \
  --llvm-dwarfdump /Users/bytedance/Developer/emsdk/upstream/bin/llvm-dwarfdump
```

固定输出：

```text
PASS
binary_sha256=ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38
manifest=114 static_init_fdes=2 main_fdes=112
contiguous=true next_boundaries=0x42CFA0,0x59B9C8 next_boundaries_in_manifest=false
eh_surface=true lsda_fdes=39 unwind_only_fdes=75 lsda_reports=39
lsda_call_sites=true functions=39 call_sites=232 no_landing=77 cleanup=80 catch_all=75 reports=39
raw_lsda_topology=true functions=10 call_sites=51 no_landing=18 cleanup=16 catch_all=17 reports=10
```

## 39-entry LSDA surface

按 canonical 地址排序：

```text
59673C 596F50 597854 597A40 597B1C 597F38
5980F4 598268 59849C 598538 598708 598A64 598B3C 598C58 598D58 598E64
5995D8 5997F0 599830 59993C 5999F4 599E04
59A0B4 59A284 59A330 59A4B0 59A8D8 59A968 59AA84 59AC0C 59AC7C
59AD08 59AD84 59AEEC
59B14C 59B48C 59B570 59B708 59B7E8
```

这张表描述的是异常清理/terminate 元数据拓扑，不等价于“函数一定向 caller 抛异常”。
例如一个入口可以只因内联的隐式 non-throw destructor 需要 terminate landing 而带
LSDA；是否向 caller unwind 仍须解码 call-site entry 与 caller guarded range。

## `0x598D58` 的遗漏闭环

fresh IDA 证据：

- `decompile/disasm(0x598D58)`：FDE 范围 `0x598D58..0x598E44`，正常主体末端
  `0x598E38 RET`，`0x598E3C` 是 stack-canary failure，`0x598E40` 调
  `sub_520FAC`（catch + terminate helper）。
- fresh `decompile/disasm(0x520FAC)` 得到精确三指令
  `__cxa_begin_catch -> std::terminate`；fresh `decompile/disasm(0xA0DE90)` 得到
  `load [aligned_data-8] -> null gate -> operator delete[]`，排除把 landing 误记成可恢复
  cleanup。
- FDE 指向 LSDA `0x186DE8C`。其 39-byte call-site table 是三项：

| 函数相对区间 | 绝对区间 | landing/action | 含义 |
| --- | --- | --- | --- |
| `[+0x00,+0x80)` | `[0x598D58,0x598DD8)` | `0 / 0` | 两个 packed helper 的异常直接传播；本函数没有 cleanup。 |
| `[+0x80,+0x84)` | `[0x598DD8,0x598DDC)` | `+0xE8 / 1` | 唯一受 guard 的调用是 owner 析构内联后的 `sub_A0DE90`；异常进入 `0x598E40` terminate。 |
| `[+0x84,+0xEC)` | `[0x598DDC,0x598E44)` | `0 / 0` | `operator delete`、赋值尾、canary 与 epilogue 没有本地 cleanup。 |

- canonical parent `ContainsDictionaryKey@0x5995D8` 的 LSDA `0x186DF00` 把
  `[0x599678,0x5996A0)` 映射到 cleanup-only landing `0x5996A8`。该范围包含
  `BL GetDictionaryValue@0x59967C`；landing 释放已构造的栈上 raw node，然后在
  `0x5996D8` `_Unwind_Resume`。cleanup 自身的 aligned dealloc 若抛出则走
  `0x5996DC` terminate。

由此得到的源码级异常分层是：

```text
GetDictionaryValue(...)                       // 接口可向 caller unwind
  FindNameIndex(...)                          // 异常直接传播
  FindDictionaryValueOffset(...)              // 异常直接传播
  out.file = file                             // Release-old -> copy -> AddRef
    ~PSBRawOwner() implicit non-throw contract
      TJSAlignedDealloc(...) throw => terminate
caller temporary PSBRawNode                   // call 异常时 cleanup 后 resume
```

本地 `PSBRawOwner::~PSBRawOwner()` 没有显式 exception-spec；其成员均为平凡类型，普通
C++ 隐式 exception-spec 因而维持 non-throw contract。`GetDictionaryValue` 没有
`noexcept`，canonical parent 又使用自动 `PSBRawNode value`。这和上述目标 LSDA 的
传播、terminate 与 cleanup 三层完全一致，无需修改生产实现。

完整逐行结论已回填
[functions/0x598D58.md](functions/0x598D58.md)。

## 对 15 个 `EVIDENCE_LIMITED` 的影响

其中 5 个入口带 LSDA：
`0x59673C`、`0x596F50`、`0x597B1C`、`0x598A64`、`0x59A0B4`；其余 10 个
只有 unwind FDE。该拓扑补强 exception/object-lifecycle 证据，但不保留被 stripped/O3
删除的 helper/type 名、member/free、pointer/reference 或精确源码 token，因此不能据此
把任何一项自动升级为 `ALIGNED`。

特别是 `ctor@0x597AD4` 与 empty-`ttstr` path `0x599DD8` 均无 LSDA，
`Transfer@0x598A64` 有 LSDA；这与既有逐函数结论一致，却仍不能唯一恢复三者的原始
标识符或表达式拼写。

## 后续：raw 生命周期簇的完整 call-site topology

同日后续已把“39 个入口是否带 LSDA”的存在性门禁继续下钻到 raw
`PSBFile/PSBRawNode` 生命周期簇：10 张表的 51 个四元组、action record、null type slot
与 10 份逐函数证据标记现均由同一 verifier 固定。完整解码与本地 RAII/exception-spec
逐项对照见
[FOLLOWUP_RAW_LSDA_CALLSITE_TOPOLOGY_2026-08-03.md](FOLLOWUP_RAW_LSDA_CALLSITE_TOPOLOGY_2026-08-03.md)。

随后该方法已扩展到全部 39 张表，完整四元组、IDA `type_id=-2/-1` 交叉核对以及
catch-all/terminate/rethrow 的语义边界见
[FOLLOWUP_FULL_LSDA_CALLSITE_TOPOLOGY_2026-08-03.md](FOLLOWUP_FULL_LSDA_CALLSITE_TOPOLOGY_2026-08-03.md)。
