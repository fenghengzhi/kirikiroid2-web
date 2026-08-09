# Follow-up：switch selector 生产者与完整 dispatch 链

日期：`2026-08-03`。本轮继续只使用权威 Android ARM64
`reference/libkrkr2/libkrkr2.so` 与 IDA MCP；没有读取同版本源码、外部私库或 Git LFS
对象，也没有使用 Android/iOS ARMv7 材料。没有修改 `cpp/` 或测试物料。

## 结论

- 先前已固定的 42 张 switch table、915 个 signed-relative entry 与 194 个 owner-FDE 内
  destination，现进一步连接到 **20 个 owner、42 个 selector 和完整 dispatch chain**。
- 42 个 selector 在 entry-rooted normal CFG 上全部是单来源：**41 条 raw-tag `LDRB`
  关系 + 1 条 chained `SUB` 关系**。它们归并成 32 个唯一 producer site，即 31 个
  `LDRB` 与 1 个 `SUB`；没有入口参数、call return 或 volatile `X1..X18` call-clobber。
- 41 个 switch 先执行 `SUB Widx,Wsrc,#lowcase`，唯一例外 `0x5977AC` 直接复用
  `SUB W12,W9,#13@0x597710` 的 zero-based 结果。42 个 switch 随后都执行 unsigned
  `CMP/B.HI default`，再以 `ADRP + ADD + LDRSW + ADD + BR` 读取 signed-relative 表项。
- 语义链共 **335 条指令实例**：41 个 normalizer，加 42 组 `CMP/B.HI/ADRP/table-ADD/
  LDRSW/dispatch-ADD/BR`。producer 指令作为独立来源表面计数，不重复并入 335。
- 当前源码的 classifier、packed count、narrow/wide integer、String/Resource index 与
  Array/Dictionary selector 数据流逐项一致。本轮没有新的生产 `GAP`，继续维持
  `ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`。

## 完整 selector 分布

lowcase 分布为：

```text
0:1, 1:11, 2:2, 4:1, 5:3, 9:3, 13:14, 21:3, 25:4
```

table case-count 分布为：

```text
4:28, 28:1, 30:2, 65:11
```

这些数值不是从本地 `switch` 语句反推。IDAPython 先枚举目标内全部 switch site 与
`ida_nalt.get_switch_info`，再读取每个 selector、range/default 和表地址物化指令；随后
离线 verifier 从 ELF word 与 normal CFG 独立复核。42 条记录的 owner/site/table/default、
producer、lowcase、case-count、寄存器和每条 chain word 已序列化为 canonical manifest；
raw manifest 为 3,738 bytes，SHA-256 为
`4d188bb33285e5df2881f96784d638ae28e9c8638af367072c0315b09264aed8`。

## 关键数据流

### CreateVariant `0x59673C`

fresh `decompile(0x59673C)` 确认同一个 `LDRB@0x596770` 生产 raw tag，随后分别形成：

- `tag-1` 的 65-entry classifier；
- `tag-4` 的 28-entry numeric/conversion family；
- `tag-5` 与 `tag-9` 的 narrow/wide integer family；
- `tag-21` 与 `tag-25` 的 String/Resource index family。

这六个 switch 共享同一 tag load，但每条 predecessor reverse walk 都在该唯一 producer
停止；中间没有 call-clobber。它们对应 `main.cpp:557-675` 的 category-first 数据流，以及
`PSBPackedInternal.h:37-188` 的 classifier 和 scalar decoder。

### PropGetByNum `0x5976C4`

fresh `decompile(0x5976C4)` 确认外层 `tag-1` classifier 后，`LDRB@0x59770C` 读取 packed
count tag，`SUB@0x597710` 形成 `tag-13`。该结果先驱动 `0x59772C` 的 count-width switch，
随后在完成 signed negative-index normalization 和 bounds gate 后，被 `0x5977AC` 直接作为
zero-based selector 再用一次；第二张表之前没有第二个 `SUB`。这与
`main.cpp:209-274` 中“先解 count，再做 W32 index normalization/bounds，最后构造
`PsbArray_guess` 并取 element”的顺序一致。

### GetDouble / GetInt

fresh `decompile(0x5992E8)` 与 `decompile(0x599438)` 均显示 `LDRB` raw tag 先进入
`tag-2` 的 30-entry outer conversion table，再进入 `tag-5` narrow 与 `tag-9` wide
integer table。`GetInt` 的 wide family 仍保留 tag `0x0B` 独立 low32 path；没有把它错误
合并到通用 64-bit return。对应
`PSBPackedInternal.h:107-188` 与 `PSBRawFile.cpp:316-365`。

### GetListAt `0x5999F4`

fresh `decompile(0x5999F4)` 显示 `LDRB@0x599A50` 先驱动 `tag-1` 的 65-entry classifier；
Array 与 Dictionary 分支再分别由 `LDRB@0x599ACC`、`LDRB@0x599B14` 读取 packed count
tag，并各自以 `tag-13` 驱动 4-entry 表。对应 `PSBMedia.cpp:149-218` 的 classifier、
Array count 与 Dictionary key view 构造顺序。

## 目标逻辑摘要（不超过 10 行）

```text
raw_tag = *(uint8_t *)selector_source
idx = raw_tag - lowcase                         // 41/42 switches
if (idx > case_count - 1) goto default          // unsigned B.HI
table = page(table_vma) + page_offset(table_vma)
disp = sign_extend_32(*(int32_t *)(table + idx * 4))
goto table + disp
// 0x5977AC: idx reuses SUB@0x597710 and skips a second normalizer
```

## 本地逐行对照

| Android selector/chain | 本地源码 | 对照结论 |
| --- | --- | --- |
| 11 张 `tag-1` / 65-entry classifier | `PSBPackedInternal.h:37-101` 及各 specialized caller | category 分组、unknown default 和 caller-local clone 一致 |
| `tag-5` / `tag-9` narrow/wide integer | `PSBPackedInternal.h:107-152` | 4-entry width family、default 0 与读取宽度一致 |
| `tag-2` 30-entry double/int outer | `PSBPackedInternal.h:154-188`、`PSBRawFile.cpp:316-365` | bool/integer/Real/default conversion 路径一致；GetInt `0x0B` 保持独立 |
| 14 张 `tag-13` packed selector | `PSBPackedInternal.h:190-205`、`PSBRawFile.cpp:52-135` 及 caller-local clones | 1/2/3/4-byte 选择、default 与消费顺序一致 |
| CreateVariant 六级 selector | `main.cpp:557-675` | classifier-first、numeric、String/Resource family 与共享 raw tag producer 一致 |
| PropGetByNum 三张表 | `main.cpp:209-274` | count selector、W32 index gate、zero-based selector reuse 的顺序一致 |
| GetListAt 三张表 | `PSBMedia.cpp:149-218` | classifier 后才读取 Array/Dictionary packed selector，一致 |

这里对齐的是源码数据流和分支顺序，不把 AArch64 的寄存器编号、ADRP page 或表项偏移写进
可移植 C++。全部现有表达都已有目标反编译支持，因此没有 `cpp/` 修改，也无需构建。

## IDB 改善

以 `SWITCH-SELECTOR-CONTRACT` 为统一 marker，在下列关键 normalizer/compare 站点添加了
15 条 instruction comment，并在可映射处同步到伪代码视图：

```text
0x596420 0x596774 0x59679C 0x59682C 0x59687C
0x597710 0x597790
0x5992F0 0x599318 0x599344
0x599440 0x599490
0x599A54 0x599AD0 0x599B18
```

注释固定了 selector 的来源、lowcase、unsigned range gate，以及 `0x597710 → 0x5977AC`
的复用关系。IDB 已保存。

## 机械门禁

`verify_elf_surface.py` 新增 canonical selector manifest 与两层检查：

1. 逐字段验证 42 条 owner/site/table/default、producer、寄存器和 exact instruction word；
2. 从 normal CFG predecessor 反向遍历，要求所有路径都在声明 producer 停止，且不越过
   未声明 call return、entry 参数或 volatile call-clobber；
3. 解码 `SUB/CMP/B.HI` 的立即数、位宽和 default successor；
4. 解码 `ADRP+ADD` 的精确 table VMA，以及 `LDRSW+ADD+BR` 的寄存器数据流。

复现命令：

```bash
python3 analysis/psbfile_function_audit_2026-07-25/verify_elf_surface.py \
  --llvm-dwarfdump /Users/bytedance/Developer/emsdk/upstream/bin/llvm-dwarfdump
```

新增输出：

```text
switch_selector_surface=true owners=20 switches=42 selectors=42 raw_tag_loads=41 chained_normalized=1 unique_producers=32 single_source=42 volatile_call_clobbers=0 sha256=true
switch_dispatch_chain=true normalizers=41 zero_based=1 unsigned_hi_guards=42 table_bases=42 ldrsw=42 dispatch_adds=42 branches=42 chain_instructions=335
```

该门禁固定的是二进制实际 selector 数据流和编译器分派链，不把 switch lowering 形态冒充
原始 C++ token。15 个 stripped/O3 identifier/factorization 上限继续保留。
