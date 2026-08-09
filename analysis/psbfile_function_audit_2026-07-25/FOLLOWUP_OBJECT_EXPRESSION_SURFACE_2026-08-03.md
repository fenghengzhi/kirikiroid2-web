# Follow-up：完整 Hex-Rays `cot_obj` 对象/地址叶节点面闭环

日期：2026-08-03

权威目标：Android ARM64 `reference/libkrkr2/libkrkr2.so`

SHA-256：`ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`

## 结论

- 完整 ECT1 中共有 **457 个 `cot_obj`**，覆盖 70 个 owner、154 个唯一目标、
  101 种恢复类型和 447 个 normal-entry exact-word anchor。
- 57 个 concrete 与 400 个 optimizer-synthetic 叶节点全部按 ELF section、直接父节点、
  child relation 和既有独立证据面分类；没有遗漏、重叠或 landing-only 节点。
- 457 行形成八类互斥分区：285 个 direct callee、7 个真实 address-taken callable、
  18 个 code-range numeric artifact、47 个 literal、24 个 initialized address point、
  1 个 canonical-empty pointer object、70 个 BSS global/subobject、5 个 extern import。
- fresh Android ARM64 反编译确认 MDF/PSB magic、24-bit mask、NCB callback/vptr、
  literal-pool suffix、COW empty representation 和 `pthread_create` 分支均与当前源码一致；
  没有新增生产 `cpp/` GAP，本轮不改 `cpp/`、测试或 fixture，也不构建生产目标。
- 本轮纠正一条既有审计资料：old-libstdc++ COW empty representation 的完整对象范围是
  `0x1C95280..0x1C952A0`（`0x20` bytes），不是先前记录的首 qword `0x8` bytes。

## 全量 census

| 分类 | row | 唯一目标 | owner |
| --- | ---: | ---: | ---: |
| direct callee | 285 | 79 | 59 |
| address-taken callable | 7 | 7 | 4 |
| code-value artifact | 18 | 3 | 12 |
| literal | 47 | 33 | 26 |
| initialized address point | 24 | 12 | 13 |
| literal pointer object | 1 | 1 | 1 |
| global state object/subobject | 70 | 19 | 22 |
| extern import address | 5 | 1 | 5 |
| **合计** | **457** | — | — |

唯一目标按权威 ELF section/IDA extern pseudo-segment 分区为：

| section | row | 唯一目标 |
| --- | ---: | ---: |
| `.text` | 251 | 77 |
| `.plt` | 59 | 11 |
| `.rodata` | 47 | 33 |
| `.data.rel.ro` | 24 | 12 |
| `.data` | 1 | 1 |
| `.bss` | 70 | 19 |
| `extern` | 5 | 1 |
| **合计** | **457** | **154** |

直接父节点精确分为：

```text
call=314 asg=57 ref=45 cast=23 idx=9 statement-root=6 eq=2 band=1
```

child relation 精确分为：

```text
callee=285 x=112 arg:0=28 y=25 cond=4 return=2 arg:1=1
```

## 与既有独立证据面的集合交叉

### direct callee

285 个 `relation=callee,parent=call` 的 `cot_obj` 与
[完整 `cot_call` 面](FOLLOWUP_CALL_EXPRESSION_SURFACE_2026-08-03.md)逐行闭合：

```text
MANIFEST internal=43
MANIFEST external=242
direct target=79
```

它们正是 274 个 normal `BL` 与 11 个 out-of-owner tail `B`。没有 data target 被误当
callee，也没有 source direct call 缺失机器 transfer。

### address-taken callable 与 code-range numeric artifact

既有 machine DataRef 面中的 10 行真实 callback/member-function materialization 会在
Hex-Rays source tree 中折叠为 7 个 `cot_obj`：三组 `ADRP+ADD` 各恢复为一只源码地址，
四组 `ADRL` 各恢复为一只源码地址。因此 `(target,owner)` 集合恰好是七组：

```text
pre-register callback
factory callback
root getter member
load method member
create-empty adaptor
finalize-empty callback
dummy-constructor callback
```

其余 18 个落入 `.text/.plt` 的非 callee 叶节点全部是已知数值碰撞：

| Hex-Rays `cot_obj` | 行数 | 真实数值 | 证据 |
| --- | ---: | ---: | --- |
| `0xFFFFFC` | 15 | `0xFFFFFF` | ctree 为 `&loc_FFFFFC + 3`；机器为 `AND #0xFFFFFF` |
| `0x66646C` | 2 | `0x0066646D` | IDA offset delta；机器 MOV-wide 为 `mdf\0` |
| `0x425350` | 1 | `0x00425350` | 数值恰与 PLT VMA 相同；机器 MOV-wide 为 `PSB\0` |

三类的 18 个 `(owner,anchor)` 与
[address-taken/artifact 面](FOLLOWUP_ADDRESS_TAKEN_CALLABLE_SURFACE_2026-08-03.md)
完全相等。verifier 不因目标位于可执行 section 就把它提升成 callable。

### literal 与 initialized data

47 个 `.rodata` 叶节点精确分为 `45 UTF-16LE + 2 UTF-8`。其中 38 行直接指向 literal，
9 行是 `cot_idx(base,index)`；verifier 按 `base + index * sizeof(tjs_char)` 解析后得到
33 个非空 literal：

```text
0x14CC5F6 +  6*2 -> String
0x14CCEA2 +  3*2 -> Property
0x14CE730 + 17*2 -> Octet
0x14D617C + 40*2 -> Dictionary
0x150729C +  5*2 -> Function   （出现两次）
0x1525426 + 17*2 -> count
0x152ACD0 + 63*2 -> )          （出现两次）
```

唯一未由 `.rodata cot_obj` 直接出现的 literal 是 canonical empty string
`0x1522752`；它由 `.data` 对象 `0x1AA7EF8` 指向，恰好补齐既有 34-literal 面。

24 个 `.data.rel.ro` 叶节点只落在既有 12 个 Itanium address point 上，覆盖 class/pre
callback、dispatch primary/secondary、method interface、factory/root/load wrapper、media、
native-class、instance-adaptor 与 native-instance base；没有 table header、RTTI slot 或
任意中间 qword 被误当成 vptr。

### BSS object/subobject 与 COW 资料纠正

70 个 `.bss cot_obj` 全部落入既有九个语义对象，恢复出 19 个字段/子对象地址。
这比 machine DataRef 面的 16 个 target 多三只 optimizer-synthetic subobject：

```text
class AutoRegister +0x08
pre-register callback +0x08
COW empty representation +0x18
```

前两只是相邻字段。第三只给出新的、足以纠正旧资料的正证据：

```text
EnumMembers@0x596F50:
data = &byte_1C95298;               // base + 0x18
...
header = data - 24;
if (header != &qword_1C95280) release/delete;
```

因此 COW sentinel/storage 的源码语义范围至少覆盖 24-byte header 与尾随字符存储；
同一 ELF `.dynsym` 中可见的另外两只 `_S_empty_rep_storage` 也各有 `st_size=0x20`。
`EXPECTED_GLOBAL_BSS_OBJECTS` 已把该对象从 `0x8` 就地纠正为 `0x20`，九只语义对象的
合计零初始化范围从 164 改为 188 bytes。既有 94 行 machine DataRef、16 个机器 target
和 digest 不变，因为 GOT/指令仍以 base 为目标；新 `cot_obj` 面另行固定 base 与
`base+0x18` 两个 source-facing subobject。

这属于目标 old-libstdc++ ABI 内联产物。当前源码继续使用普通 `std::string` /
`std::vector<std::string>`，不在 wasm32 中伪造 Android 私有字节布局。

### extern import

五个 `extern` 叶节点均为 IDA pseudo-address `0x1D159F8`，owner 恰为五条 normal COW
decrement 路径。它们与 `.got` slot `0x1A9F750` 的
`R_AARCH64_GLOB_DAT -> pthread_create` 动态重定位、既有九组 normal/landing GOT load
共同闭合；source tree 中的五行分别是五个 `if (&pthread_create)` normal 判断，未把
四条 landing/重复 load 伪造成额外源码对象。

## fresh 反编译与本地对照

本轮 fresh decompile `0x596F50`、`0x598268`、`0x598708`、`0x597F38`、
`0x59A4B0`、`0x42CF28`。关键行为压缩为十行：

```text
EnumMembers: Dictionary 临时 string 从 COW empty data 开始；析构比较 data-24 sentinel。
EnumMembers: packed 24-bit 项只执行 value & 0xFFFFFF。
Load: octet 首字为数值 0x0066646D 时尝试 MDF zlib 解压，否则原样复制。
Adopt: size>=0x40 且首字为数值 0x00425350 才建立 PSBRawOwner。
registerMembers: 分配 factory/root/load wrapper，写各 vptr、literal 与 callback/member。
Resolve: null ttstr storage 经 canonical `.data` pointer 取得空字符串。
static init: 发布 class/pre-callback vptr、PSBFile.dll/PSBFile literal 与 callback。
pthread branch: 原子 decrement 可用时走 exclusive loop，否则走普通后减。
```

| Android ARM64 对象/地址结构 | 当前源码对照 |
| --- | --- |
| MDF/PSB 数值 magic 与 24-bit packed mask | [`PSBRawFile.cpp`](../../cpp/plugins/psbfile/PSBRawFile.cpp)、[`PSBPackedInternal.h`](../../cpp/plugins/psbfile/PSBPackedInternal.h) 使用普通整数常量 |
| factory/root/load callback/member 与 NCB literal | [`main.cpp`](../../cpp/plugins/psbfile/main.cpp)、[`ncbind.hpp`](../../cpp/core/plugin/ncbind.hpp) 保留同一注册拓扑 |
| String/Octet/Array/Dictionary 及 Function/Property suffix-pool 结果 | `main.cpp` 与 `ncbind.hpp` 的字面 token 一致；不复制 linker pool 基址 |
| 12 个 consumed address point | 当前 dispatch/media/NCB class 的普通 C++ 继承与虚函数对象构造一致 |
| COW empty storage / pthread gate | 当前普通 STL 源码选型一致；目标私有 ABI 不进入 portable C++ |

没有证据支持新增手写地址、ARM64 offset、`pthread_create` 判断或 linker literal-pool
表达式；这些都属于编译/链接/目标标准库实现，不是应复制进 `cpp/` 的源码 token。

## 机械门禁

本面直接由 ECT1 重建，不复制第二份 row payload。canonical semantic sequence 为
95,535 bytes，SHA-256：

```text
b625197b0d1341f6e828e80eb2d5a47cf4b3d2c075b359ad8ee52a184930e453
```

每一行固定：

```text
owner / ordinal / statement / parent / parent-op / relation / type / detail /
exflags / concrete / raw-EA / anchor / exact-word / raw-target / ELF-section /
classification / resolved-target / semantic-container
```

`verify_elf_surface.py` 固定输出：

```text
object_expression_surface=true owners=70 rows=457 roots=6 targets=154 types=101 classes=8 sections=7 containers=47 concrete=57 synthetic=400 raw_sites=57 anchors=447 normal=447 landing=0 single=443 shared=4 max=4 direct=285 address_taken=7 artifacts=18 literals=47 indexed_literals=9 resolved_literals=33 address_points=24 literal_pointers=1 global_objects=70 extern_imports=5 semantic_bytes=95535 semantic_sha256=true section_partition=true cross_surfaces=true paths_complete=true
```

完整 ELF verifier 继续通过；114-entry 总判定仍为
`ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`。本面固定的是当前 IDB 恢复出的全部
`cot_obj` 叶节点及其跨表面归属，不把“数值位于代码段”或“链接器合并了字符串后缀”
反向制造成原始 C++ 函数/全局对象。
