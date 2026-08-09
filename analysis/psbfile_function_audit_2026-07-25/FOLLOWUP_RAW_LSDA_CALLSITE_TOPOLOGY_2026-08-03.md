# Follow-up：raw PSB 生命周期簇的 LSDA call-site 精确拓扑

日期：`2026-08-03`。本轮只读取权威 Android ARM64
`reference/libkrkr2/libkrkr2.so`；没有修改 `cpp/`、fixture、安装包或二进制。

## 结论

- 对 raw `PSBFile/PSBRawOwner/PSBRawNode` 生命周期簇的 10 个 LSDA-bearing FDE，已从
  “函数带 LSDA”推进到 **完整 call-site table 逐字段校验**。
- 10 张表合计 51 项：18 项无本地 landing、16 项 cleanup-only、17 项 action-1
  catch-all。17 项的 action record 都是 `(type_filter=1,next=0)`，对应 type-table entry
  都为 null；fresh IDA 又确认其 landing 进入 `__cxa_begin_catch -> std::terminate`。
- 33 项非零 landing 与 IDA C++ try-block ranges 完全一致；cleanup-only landing 都在销毁
  当时已构造对象后 `_Unwind_Resume`，catch-all landing 都体现隐式 non-throwing 析构/释放
  边界。
- 本地 exception-spec、自动对象层次、`std::function`/`ttstr`/`std::vector` 清理和
  intrusive owner 生命周期逐项一致；没有发现新的生产 GAP，统计保持
  `99 ALIGNED / 15 EVIDENCE_LIMITED / 0 HAS_GAP`。

## 独立证据与解码方法

1. 从 `.eh_frame` 读取 FDE 的精确 `[start,end)` 与 LSDA VMA，不使用报告文字反推地址。
2. 直接解析 ELF64 section table，要求目标为 little-endian AArch64，且
   `.gcc_except_table` 精确为 `[0x1857A70,0x19D943C)`。
3. 对每张 LSDA 解码 `LPStart`、type-table offset、`DW_EH_PE_udata4` call-site encoding、
   ULEB128 table length，以及每项 `(start,length,landing,action)`；非 canonical padding
   ULEB（例如 `b3 80 00`）也按值解码，不按字节拼写猜测。
4. action 1 再读取 action record 与反向 type-table slot，确认它确为 null-type catch-all，
   不把任意非零 action 武断写成 terminate。
5. fresh IDA 使用 `ida_hexrays.get_tryblks` 复核 10 个函数的 C++ guarded ranges；再以
   disassembly 核对 cleanup 的析构/`_Unwind_Resume` 和 catch-all 的
   `sub_520FAC@0x520FAC`。本轮 IDAPython 只读，没有修改或保存 IDB。

## 完整 51-entry 表

格式为 `(relative_start, length, relative_landing, action)`；`landing=0` 表示本地无
landing，`action=0 + landing!=0` 表示 cleanup-only，`action=1` 表示已验证的 null-type
catch-all。

```text
598268 / LSDA 186DC24:
  (000,088,000,0) (088,014,20C,0) (0A4,010,1D4,1)
  (0B4,038,000,0) (0EC,014,1D8,0) (108,010,1D0,1)
  (11C,010,1BC,0) (134,004,1CC,1) (138,0AC,000,0)
  (1E4,010,204,1) (1FC,004,208,1) (218,010,230,1)
  (228,00C,000,0)

59849C / LSDA 186DCE0:
  (02C,008,084,0) (034,068,000,0)

598538 / LSDA 186DD00:
  (000,03C,000,0) (03C,010,1B4,0) (054,004,194,1)
  (064,024,198,0) (088,010,188,0) (098,010,190,0)
  (0C8,010,18C,0) (0D8,060,000,0) (138,020,190,0)
  (174,008,18C,0) (17C,044,000,0) (1C0,008,1CC,1)

598708 / LSDA 186DDB0:
  (000,114,000,0) (114,004,254,1) (130,004,250,1)
  (134,124,000,0)

598A64 / LSDA 186DDF4:
  (028,004,044,1)

598B3C / LSDA 186DE10:
  (00C,004,018,1)

598C58 / LSDA 186DE2C:
  (000,094,000,0) (094,010,0E0,0) (0AC,004,0DC,1)
  (0B0,03C,000,0) (0EC,008,0FC,1) (0F4,00C,000,0)

598D58 / LSDA 186DE8C:
  (000,080,000,0) (080,004,0E8,1) (084,068,000,0)

598E64 / LSDA 186DEC4:
  (0D8,00C,20C,0) (114,00C,214,0) (164,038,218,0)
  (19C,174,000,0)

5995D8 / LSDA 186DF00:
  (06C,004,108,1) (0A0,028,0D0,0) (0C8,028,000,0)
  (0F0,004,104,1) (0F4,018,000,0)
```

## 源码级生命周期对照

| Android 入口 | LSDA 所约束的源码层次 | 本地对照 |
| --- | --- | --- |
| `PSBFile_Load@0x598268` | Adopt/storage/filter 失败时按构造进度释放 Variant、路径与 callable；callable/Variant 析构异常终止 | `PSBFile::Load` 的自动 `tTJSVariant`、`ttstr` 与 `OwnerFilter` scope 保留同一分层 |
| `preRegister@0x59849C` | 静态构造的 `operator new` 失败先 `__cxa_guard_abort` 再 resume | 函数内静态注册对象使用 C++ guard 生命周期 |
| `LoadStorage@0x598538` | placed path、stream、raw buffer 在不同成功阶段进入 cleanup；析构释放边界单独 terminate | 本地自动 `ttstr`/stream holder、aligned buffer 接管与 `Adopt` 调用次序一致 |
| `Adopt@0x598708` | 旧 owner 与临时 holder 的最后引用释放是 non-throw；filter 异常不在本函数回滚 | 本地 intrusive `Release`、临时 replacement 和无 catch filter 调用一致 |
| `Transfer@0x598A64` | 最后引用的 aligned deallocation 处于 non-throwing 析构边界 | 本地 holder/owner special-member exception 层次一致 |
| `Owner dtor@0x598B3C` | 析构本体唯一 deallocation 抛出即 terminate | `PSBRawOwner::~PSBRawOwner()` 的隐式 non-throw contract 一致 |
| `strict getter@0x598C58` | 诊断 `ttstr` 构造/throw 失败 cleanup 后 resume；`ttstr` 释放自身抛出则 terminate | 本地自动 `ttstr` 与异常 helper 调用一致 |
| `getter@0x598D58` | packed lookup 异常直接传播；out-owner 旧值析构异常终止 | helper 未标 `noexcept`，owner 析构隐式 non-throw |
| `GetDictionaryKeys@0x598E64` | reserve、名称解码和 vector growth 按已构造 string/vector 层数 cleanup | 本地 `std::vector<std::string>` 与复用 string 生命周期一致 |
| `ContainsDictionaryKey@0x5995D8` | child getter/unknown-type 异常先析构自动 raw node 再 resume；该析构自身异常终止 | 本地自动 `PSBRawNode value`、intrusive owner release 与可传播 child 接口一致 |

因此，本轮的正证据补强的是异常数据流、对象生命周期和边界行为，不要求也不支持改写
`cpp/`。尤其不能把“有 LSDA”简化成“整个函数 noexcept”或“整个函数一定抛异常”。

## 机械门禁

`verify_elf_surface.py` 现在额外要求：

- 10 个 FDE 各自指向可解析的 LSDA；
- 51 项四元组逐项、逐顺序完全相等；
- range/landing 不越出所属 FDE，ranges 不重叠；
- action 只能是 0/1，17 个 action-1 的 record 和 null type slot 都保持不变；
- 10 份逐函数报告都保留 `LSDA-CALLSITE-TOPOLOGY` 证据标记。

固定新增输出：

```text
raw_lsda_topology=true functions=10 call_sites=51 no_landing=18 cleanup=16 catch_all=17 reports=10
```

同日后续已在不弱化本子集门禁的前提下扩展到全部 39 张 LSDA、232 个 call-site entry；
全量 manifest 与“catch-all 不等于一律 terminate”的语义边界见
[FOLLOWUP_FULL_LSDA_CALLSITE_TOPOLOGY_2026-08-03.md](FOLLOWUP_FULL_LSDA_CALLSITE_TOPOLOGY_2026-08-03.md)。

本轮验证结果：

- `python3 -m py_compile verify_elf_surface.py`：PASS；
- `verify_audit.py`：PASS，仍为 114/114 报告与 `99/15/0` 判定；
- 权威 ELF live verifier：PASS，输出上述 `10/51/18/16/17/10`；
- 负向把 `0x598D58` 错接到 `0x598E64` 的 LSDA：被精确拓扑差异拒绝；
- 负向移除 raw report 或专用 marker：分别被 `missing-report` 与
  `missing-callsite-topology` 拒绝；
- 因审计目录当前整体未跟踪，对本轮每个文件逐一运行
  `git diff --no-index --check /dev/null <file>`：PASS。

完整 FDE/39-entry LSDA surface 的上层校验见
[FOLLOWUP_ELF_LSDA_SURFACE_2026-08-03.md](FOLLOWUP_ELF_LSDA_SURFACE_2026-08-03.md)。
