# Follow-up：39-entry LSDA call-site topology 全量闭环

日期：`2026-08-03`。本轮只读取权威 Android ARM64
`reference/libkrkr2/libkrkr2.so` 和当前本地源码；没有修改 `cpp/`、fixture、安装包或
二进制。

## 结论

- 39 个 LSDA-bearing FDE 的 **全部 232 个 call-site entry** 已逐项解码并写入独立预期
  manifest，不再只验证“这个函数有 LSDA”。
- 全量分类为：77 项无本地 landing、80 项 cleanup-only、75 项 action-1 null-type
  catch-all；分别覆盖 31、28、28 个函数。
- 39 张表都使用 `DW_EH_PE_udata4` call-site encoding 与 `0x9C` type-table encoding；
  所有非零 action 都恰为 1，action record 都恰为 `(type_filter=1,next=0)`，反向 type slot
  都为 null。
- fresh IDA `ida_tryblks` 独立得到 `80 type_id=-2 + 75 type_id=-1`，与 ELF 的 155 个
  非零 landing 一一相等，**0 mismatch**。
- 全量复扫没有发现本地 exception-spec、RAII 层次、对象生命周期或边界行为的新确定
  偏差；统计保持 `99 ALIGNED / 15 EVIDENCE_LIMITED / 0 HAS_GAP`。

## 不能混淆的语义边界

LSDA 的 action 1 在本目标中证明“null-type catch-all”，但**不能单凭 action 值把所有
landing 都叫作 terminate**：

- 隐式 non-throwing destructor/deallocator 的 catch-all landing 通常直接进入
  `sub_520FAC@0x520FAC`（`__cxa_begin_catch -> std::terminate`）。
- `PSBFile_Factory_guess@0x5980F4` 等函数也使用 catch-all 来捕获任意异常、清理已构造
  对象，然后 `__cxa_rethrow`；rethrow 自身和 `__cxa_end_catch` 又可能拥有独立
  cleanup/terminate landing。
- `std::vector<std::string>::_M_emplace_back_aux@0x59B7E8` 同样先 catch、释放新 buffer、
  rethrow，再以独立 cleanup 结束 catch；这不是函数级 `noexcept`。

因此，全量门禁机械命名为 `catch_all`；只有 fresh landing disassembly 能进一步区分
terminate 与 cleanup/rethrow。raw 生命周期子集的 17 个 action-1 landing 已逐个确认均为
terminate，见
[FOLLOWUP_RAW_LSDA_CALLSITE_TOPOLOGY_2026-08-03.md](FOLLOWUP_RAW_LSDA_CALLSITE_TOPOLOGY_2026-08-03.md)。

## 39 张表的聚合索引

| 函数 | LSDA | entries | no landing | cleanup | catch-all |
| --- | ---: | ---: | ---: | ---: | ---: |
| `0x59673C` | `0x186D9D4` | 5 | 3 | 2 | 0 |
| `0x596F50` | `0x186DA1C` | 11 | 2 | 7 | 2 |
| `0x597854` | `0x186DABC` | 3 | 2 | 1 | 0 |
| `0x597A40` | `0x186DAE8` | 1 | 0 | 0 | 1 |
| `0x597B1C` | `0x186DB04` | 5 | 2 | 3 | 0 |
| `0x597F38` | `0x186DB4C` | 7 | 4 | 3 | 0 |
| `0x5980F4` | `0x186DBAC` | 8 | 3 | 1 | 4 |
| `0x598268` | `0x186DC24` | 13 | 4 | 3 | 6 |
| `0x59849C` | `0x186DCE0` | 2 | 1 | 1 | 0 |
| `0x598538` | `0x186DD00` | 12 | 3 | 7 | 2 |
| `0x598708` | `0x186DDB0` | 4 | 2 | 0 | 2 |
| `0x598A64` | `0x186DDF4` | 1 | 0 | 0 | 1 |
| `0x598B3C` | `0x186DE10` | 1 | 0 | 0 | 1 |
| `0x598C58` | `0x186DE2C` | 6 | 3 | 1 | 2 |
| `0x598D58` | `0x186DE8C` | 3 | 2 | 0 | 1 |
| `0x598E64` | `0x186DEC4` | 4 | 1 | 3 | 0 |
| `0x5995D8` | `0x186DF00` | 5 | 2 | 1 | 2 |
| `0x5997F0` | `0x186DF50` | 1 | 0 | 0 | 1 |
| `0x599830` | `0x186DF6C` | 1 | 0 | 0 | 1 |
| `0x59993C` | `0x186DF88` | 3 | 2 | 1 | 0 |
| `0x5999F4` | `0x186DFB4` | 16 | 3 | 7 | 6 |
| `0x599E04` | `0x186E098` | 14 | 3 | 6 | 5 |
| `0x59A0B4` | `0x186E160` | 5 | 2 | 1 | 2 |
| `0x59A284` | `0x186E1B0` | 6 | 3 | 1 | 2 |
| `0x59A330` | `0x186E210` | 6 | 3 | 3 | 0 |
| `0x59A4B0` | `0x186E264` | 24 | 3 | 9 | 12 |
| `0x59A8D8` | `0x186E3B0` | 6 | 3 | 1 | 2 |
| `0x59A968` | `0x186E410` | 5 | 2 | 1 | 2 |
| `0x59AA84` | `0x186E460` | 7 | 3 | 2 | 2 |
| `0x59AC0C` | `0x186E4CC` | 1 | 0 | 0 | 1 |
| `0x59AC7C` | `0x186E4E8` | 1 | 0 | 0 | 1 |
| `0x59AD08` | `0x186E504` | 1 | 0 | 0 | 1 |
| `0x59AD84` | `0x186E520` | 7 | 3 | 2 | 2 |
| `0x59AEEC` | `0x186E58C` | 16 | 3 | 5 | 8 |
| `0x59B14C` | `0x186E670` | 3 | 2 | 0 | 1 |
| `0x59B48C` | `0x186E6A8` | 3 | 2 | 1 | 0 |
| `0x59B570` | `0x186E6D4` | 4 | 2 | 2 | 0 |
| `0x59B708` | `0x186E710` | 5 | 1 | 4 | 0 |
| `0x59B7E8` | `0x186E758` | 6 | 3 | 1 | 2 |
| **合计** | — | **232** | **77** | **80** | **75** |

精确顺序、相对 start/length/landing/action 四元组保存在
`verify_elf_surface.py` 的 `EXPECTED_ALL_LSDA_CALL_SITES`；verifier 直接从权威 ELF
重解码后逐元组比较，不以本表的聚合数字代替完整验证。

## IDA 独立交叉核对

本轮用现代 IDAPython `ida_tryblks.get_tryblks` 对 39 个 FDE 范围逐个读取 C++
try-block metadata：

```text
functions 39 cleanup_type_minus2 80 catchall_type_minus1 75 mismatches []
```

进一步对旧报告没有结构化 catch-all 说明的入口读取 guarded range 内 `BL/BLR` 与 landing
开头指令，确认了三类常见源代码形状：

1. 自动 `ttstr`/Variant/raw-node/vector/callable 的 cleanup-only landing，析构后
   `_Unwind_Resume`；
2. 析构或 deallocation 自身失败，直接进入 `sub_520FAC`；
3. catch-all 清理后 `__cxa_rethrow`，并为 rethrow/end-catch 再建立 cleanup/terminate
   landing。

这三类与各逐函数报告已有的本地对象 scope、intrusive owner、NCB adaptor/Variant 管理及
STL 慢路径对照一致。本轮只补强异常元数据与报告结构，不凭 LSDA 反推原始标识符或
`noexcept` token。

## 报告与机械门禁

- 39 份 LSDA-bearing 报告现在都含 `LSDA-CALLSITE-TOPOLOGY` 标记和各自聚合计数；
  raw 10 份报告继续保留更细的绝对 guarded range/landing 说明。
- verifier 要求 `EXPECTED_ALL_LSDA_CALL_SITES` 的 key set 精确等于 39-entry LSDA
  surface，避免漏表或多表。
- 每个 call-site range 必须非空、不重叠、不越出 FDE；非零 landing 必须位于所属 FDE。
- action 只能为 0/1；零 landing 不允许携带 action；所有 action 1 必须保持
  `(type_filter=1,next=0)` 与 null type slot。
- 39 张全量表与 10 张 raw 子集分别重复校验，后者防止全量重构意外弱化此前的专用门禁。

新增固定输出：

```text
lsda_call_sites=true functions=39 call_sites=232 no_landing=77 cleanup=80 catch_all=75 reports=39
raw_lsda_topology=true functions=10 call_sites=51 no_landing=18 cleanup=16 catch_all=17 reports=10
```

本轮验证结果：

- `verify_audit.py`：PASS，114/114 报告、图与 schema 完整，判定仍为 `99/15/0`；
- 权威 ELF live verifier：PASS，完整输出上述 `39/232/77/80/75/39` 与 raw 子集统计；
- fresh IDA try-block aggregate：`80 type_id=-2 / 75 type_id=-1 / mismatches []`；
- 负向把 `0x59673C` 错接到 `0x596F50` 的 LSDA：被逐项 topology mismatch 拒绝；
- 负向删除 full manifest key：被 key-set completeness gate 拒绝；
- 负向移除全量报告 marker：被 `missing-callsite-topology` 拒绝。

上层 FDE/LSDA 存在性闭环见
[FOLLOWUP_ELF_LSDA_SURFACE_2026-08-03.md](FOLLOWUP_ELF_LSDA_SURFACE_2026-08-03.md)。
