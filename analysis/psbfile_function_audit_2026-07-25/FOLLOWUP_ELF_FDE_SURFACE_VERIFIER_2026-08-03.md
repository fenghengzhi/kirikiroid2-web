# Follow-up：ELF `.eh_frame` 函数面独立校验

日期：`2026-08-03`。本轮不依赖既有 IDA function list，直接从权威 Android
`reference/libkrkr2/libkrkr2.so` 的 `.eh_frame` 提取 FDE 函数范围，与 114-address
MANIFEST 做集合及连续性双向校验。目标是排除“IDA 漏建内部函数，导致 MANIFEST 与报告
彼此自洽但共同漏项”的完成性风险。本轮没有修改生产 `cpp/`。

## 权威输入与方法

- ELF SHA-256：
  `ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`。
- `llvm-dwarfdump --eh-frame` 共解析出 `45,697` 个 FDE。
- 对 psbfile 两段 emitted surface 分别比较：
  - static init：`[0x42CEF8, 0x42CFA0)`；
  - main cluster：`[0x59641C, 0x59B9C8)`。
- 每段同时要求：FDE start 集合与该区间 MANIFEST 地址集合完全相等；按 start 排序后，
  每个 FDE end 必须精确等于下一 FDE start；最后一个 end 必须精确等于区间 exclusive
  end；exclusive end 自身必须有下一模块的独立 FDE，且不能出现在 MANIFEST。

实际结果：

```text
PASS
binary_sha256=ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38
manifest=114 static_init_fdes=2 main_fdes=112
contiguous=true previous_boundaries=0x42CE6C,0x596414 previous_boundary_ends_match=true next_boundaries=0x42CFA0,0x59B9C8 adjacent_boundaries_in_manifest=false
```

因此主簇 112 个 FDE 从 `0x59641C` 首尾相接到 `0x59B9C8`，无任何未登记 start、内部
断点、重叠或尾部空洞；两只 static-init FDE 同样从 `0x42CEF8` 连续到 `0x42CFA0`。

## IDA 独立交叉核验

本轮另用 IDA `entity_query` 枚举 `0x59641C..0x59B9C8`，返回 113 个函数：前 112 个
与 main MANIFEST 完全一致，第 113 个恰是 exclusive boundary `0x59B9C8`。对该区间
全部 249 条 `SUB` 再筛 `SUB SP,SP,#imm`，得到 32 个栈帧入口，全部地址都等于其
containing function start；没有隐藏在既有 IDA function 内部的第二函数序言。

`.eh_frame` 集合不依赖该 IDA 结果，IDA 查询只作为另一条工具链的交叉核验。

## fresh 下边界归属证据

后续 fresh IDA 又把两个区间的直接前驱纳入双向边界检查：

- static-init 前驱 `layerExImage_ModuleRegist@0x42CE6C` 的 FDE 精确结束于
  `0x42CEF8`。fresh decompile 保留 `"layerExImage"`、`"Layer"`、
  `"LayerExImage.dll"` 与 `layerExImage_PreRegistCallback`，因此它属于
  `layerExImage` 注册，不是漏掉的 psbfile static init。
- 主簇前的 IDA 全函数枚举得到 8 个入口：`0x596144/0x596168/0x596170/`
  `0x596240/0x596264/0x59626C/0x5963F0/0x596414`。fresh decompile、完整
  `layerExImage_NCB_ClassBody@0x594814` 反汇编及 vtable data refs 将它们分成三组：
  `noise` 的 `off_1A0B078`、`generateWhiteNoise` 的 `off_1A0B198`、
  `gaussianBlur` 的 `off_1A0B2B8`。最后一组的 vtable 在
  `0x1A0B2C8/0x1A0B3B0/0x1A0B3C0` 分别引用
  `0x59626C/0x5963F0/0x596414`；其构造点 `0x594C0C..0x594C20` 同时写入
  `layerExImage_gaussianBlur`，注册字面量为 `"gaussianBlur"`。
- 直接前驱 `0x596414` 的 FDE 精确结束于首个 psbfile 入口 `0x59641C`；后半段
  `0x59A800..0x59B900` 的 23 个 IDA 入口则与 MANIFEST #90..#112 完全一致。
  因而主簇连续区间外没有被相邻模板代码遮蔽的 psbfile emitted 入口。

`verify_elf_surface.py` 现机械要求 `0x42CE6C -> 0x42CEF8` 与
`0x596414 -> 0x59641C` 两个前驱 FDE 边界保持精确，并要求两个前驱都不进入
MANIFEST；这与原有两只 exclusive-end 下一模块检查合成双向边界门禁。

## fresh 边界归属证据

本轮 fresh `decompile(0x59B9C8)` 与 `decompile(0x42CFA0)`：

```text
static@0x42CFA0:
  registration.name = UTF16("PackinOne.dll")
  registration.callback = 0x59B9C8
  link registration into the global plugin-record chain
callback@0x59B9C8:
  sequentially construct/load/release fstat.dll, savestruct.dll, scriptsEx.dll,
  shrinkCopy.dll, layerExBTOA.dll, layerExImage.dll, layerExRaster.dll,
  csvParser.dll
```

`xref_query(0x59B9C8)` 只有来自 `0x42CFA0` 的两处 data xref，分别写入上述 registration
record。因此 `0x42CFA0` 与 `0x59B9C8` 是下一模块的 static registration/callback 配对，
不是 psbfile static init 尾部或 `vector<string>` helper 尾部；两段 exclusive end 的归属
都由目标内字面量和 callback 地址正证据闭合。

## 可重复 verifier

新增 `verify_elf_surface.py`。它固定目标 SHA-256，解析 MANIFEST，调用
`llvm-dwarfdump --eh-frame`，并对集合、连续性、边界 FDE 和 114 总数失败即退出：

```bash
python3 analysis/psbfile_function_audit_2026-07-25/verify_elf_surface.py \
  --llvm-dwarfdump /absolute/path/to/llvm-dwarfdump
```

若 `llvm-dwarfdump` 已在 `PATH`，或已导出带该工具的 `EMSDK`，可省略显式路径。

同日后续已把该 verifier 扩展到完整 LSDA exception surface：额外固定
`39 LSDA-bearing + 75 unwind-only` 的精确 MANIFEST 集合，并校验 LSDA 唯一性与
`.gcc_except_table` 范围。新增证据、`0x598D58` 的唯一报告遗漏及 call-site table
解码见
[FOLLOWUP_ELF_LSDA_SURFACE_2026-08-03.md](FOLLOWUP_ELF_LSDA_SURFACE_2026-08-03.md)。

## 审计影响

- Android emitted surface 的 114-address 完整性现在不仅由 IDA/MANIFEST/TASK_TREE/报告
  自洽证明，还由 ELF unwind metadata 独立证明；上下相邻 FDE 的模块归属也已由目标内
  字面量、callback 与 vtable 构造点闭合。
- 本轮没有发现新的生产 GAP，也没有改变任一函数六维 verdict；统计继续为
  `99 ALIGNED / 15 EVIDENCE_LIMITED / 0 HAS_GAP`。
- 15 项 remaining limitation 仍是 stripped/O3 下无法唯一恢复的源码 token；本校验只
  证明 emitted 入口集合完整，不把函数边界完整性错误外推成源码 token 已恢复。
