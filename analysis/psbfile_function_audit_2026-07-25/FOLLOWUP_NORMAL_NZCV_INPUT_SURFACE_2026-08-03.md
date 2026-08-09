# Follow-up：normal NZCV producer 输入数据流

`NORMAL-NZCV-INPUT-CONTRACT`

日期：`2026-08-03`。本轮继续只使用权威 Android ARM64
`reference/libkrkr2/libkrkr2.so` 与 IDA MCP；没有读取同版本源码、外部私库或 Git LFS
对象，也没有使用 Android/iOS ARMv7 材料。没有修改 `cpp/` 或测试物料。

## 结论

上一轮已经固定 180 个 `B.cond` 的 condition code、唯一线性 NZCV producer 及
`176 CMP + 3 CMN + 1 SUBS` 分布；本轮继续回答这些 producer 的每个寄存器输入
**从哪里来**。

- 180 个 producer 分布在 48 个 owner；96 个 immediate `CMP`、3 个 immediate `CMN`、
  1 个 immediate `SUBS` 各读取一个寄存器，80 个 register `CMP` 各读取两个寄存器，
  合计 **260 个 operand**。
- 沿 entry-rooted normal CFG 的全部 predecessor 回溯后，共得到 **320 条
  reaching-definition 关系**：313 个显式 instruction writer、4 个入口参数、3 个
  `W0/X0` call return。
- 235 个 operand 为单来源；25 个为多来源 join，来源数分布为
  `2:15, 3:1, 4:2, 5:4, 6:1, 9:2`，最大 9。没有任何路径跨越 volatile
  `X1..X18` call-clobber。
- 输入位宽为 `W=129 / X=131`；producer form 为 `immediate=100 / register=160`，即
  `operand0=180 / operand1=80`。100 个 immediate form 的 12 个常量及全部 producer word
  同样受门禁保护。
- fresh 反编译与当前源码逐项对照后没有发现生产 `GAP`，继续维持
  `ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`。

## 恢复方法

本轮没有把 compare 前的线性“最近赋值”当成输入来源：

1. 沿用已独立验证的 normal CFG、switch destination 与 normal-only noreturn 集；
2. 从 raw word 解码每个 `CMP/CMN/SUBS` 的 `Rn`，register form 再解码 `Rm`；
3. 使用 IDA AArch64 processor module 的 canonical `CF_CHG1..CF_CHG6` 元数据枚举真实
   写寄存器 operand，避免把 memory/base operand 误判成 destination；
4. 对每个输入分别从 NZCV producer 的 predecessor 开始反向遍历；这对
   `SUBS W8,W8,#1@0x59988C` 尤其重要，producer 自己写出的新 W8 不能覆盖其旧输入；
5. `BL/BLR` 只允许作为 `W0/X0` call-return source；遇到 `X1..X18` 必须报 volatile
   clobber，`X19..X28` 依 AAPCS64 继续回溯；
6. 每条路径必须精确停在 manifest 声明的 writer/call/entry，缺失、多余或跨过其他已知
   writer 均失败。

canonical row 格式为：

```text
operand: owner:u32, branch:u32, nzcv:u32, nzcv_word:u32,
         condition:u8, nzcv_kind:u8, width:u8, form:u8,
         operand_slot:u8, register:u8, ndefs:u8
source:  role:u8, kind:u8, operand:u8, address:u32, word:u32
```

260 行 canonical manifest 为 9,500 bytes，解压后 SHA-256 为：

```text
865b6824f74e1f2cedb2e27837d6dd67e5a69f760a9c473e0263ba9505739cff
```

## 来源分类

| 来源 | 关系数 | 源码层含义 |
| --- | ---: | --- |
| memory | 148 | `105 LDR + 13 LDUR + 11 LDP + 7 LDRB + 4 LDRH + 3 LDURH + 5 LDAXR`，读取字段、packed scalar、vector pointer 或旧 refcount |
| transfer | 60 | `MOV` 的参数保存、默认值、loop-carried 值与 classifier result |
| select | 4 | `2 CSEL + 2 CSINC`，dictionary binary-search 的 lower/upper 更新 |
| arithmetic | 101 | `75 SUB + 14 ADD + 10 AND + 1 ASR + 1 SXTW`，索引、长度、packed mask 与 pointer difference |
| entry | 4 | ABI 入参直接参加比较 |
| call return | 3 | `2 BL + 1 BLR` 的 `W0/X0` 返回值 |

100 个 immediate form 的常量分布为：

```text
0:5, 1:15, 2:2, 3:30, 4:28, 6:1, 7:1, 9:1, 11:2, 27:1, 29:2, 64:12
```

这些立即数分别覆盖 bool/zero、packed width、container category、最小 stream/MDF 大小与
65-tag classifier 上界；它们来自 producer word，不从本地常量反推。

## 25 个多来源 operand

| owner / compare | 多来源输入 | 目标数据流 |
| --- | --- | --- |
| `0x59641C / 0x5964C8,0x596560` | packed count W8，各 5 路 | 1/2/3/4-byte 与 default 后的 trie state unsigned 上界 |
| `0x59641C / 0x596518,0x596548` | loop parent W3，各 2 路 | 初始 0 与上一轮 state 的 dictionary-parent equality |
| `0x59659C / 0x596674` | lower W12 为 2 路；upper W10 为 6 路 | `MOV/CSINC` 与 packed count/default/`CSEL` 汇合后的最终 lower>=upper gate |
| `0x596F50 / 0x597018,0x597024` | category W22，各 9 路 | category `0..7` 加 diagnostic helper 返回后的显式 `-1@0x597498` |
| `0x596F50 / 0x597100` | Array count W9，5 路 | packed width/default 后的 `count>=1` |
| `0x596F50 / 0x597428` | COW pointer X16，2 路 | Array/Dictionary cleanup 的共享 canonical-empty 比较 |
| `0x596F50 / 0x5974E0` | old refcount W9，2 路 | `LDAXR` 与非原子 `LDR` 两条运行时路径 |
| `0x5976C4 / 0x597788` | packed count W13，5 路 | normalized signed index 与 count 的第二 bounds gate |
| `0x597B1C / 0x597CF0` | vector current/end 各 2 路 | empty storage 与 growth path 汇合 |
| `0x597B1C / 0x597DBC` | X21 为 3 路、X23 为 2 路 | allocation/copy 后的 vector pointer join |
| `0x597B1C / 0x597DCC,0x597DD8,0x597DF8` | 分别 2/4/2 路 | post-growth end、reverse bound 与 loop-carried reverse pointer |
| `0x598E64 / 0x599058` | old refcount W9，2 路 | Dictionary key temporary 的 atomic/non-atomic release |
| `0x599174 / 0x599254` | old refcount W9，2 路 | `vector<string>::reserve` element cleanup |
| `0x5999F4 / 0x599BB8` | Array count W21，4 路 | 四种有效 packed width；unknown tag 已在 switch default 退出 |
| `0x5999F4 / 0x599C88` | COW pointer X13，2 路 | nonempty/empty Dictionary temporary 的 sentinel 比较 |
| `0x5999F4 / 0x599CC4` | old refcount W9，2 路 | Dictionary temporary 的 atomic/non-atomic release |
| `0x59B7E8 / 0x59B958` | old refcount W9，2 路 | vector growth cleanup 的 atomic/non-atomic release |

这里按 operand 而非 branch 计数；同一 register-form `CMP` 的 `Rn/Rm` 若都来自 join，会占
两行，合计恰为 25。

## 入口参数与 call return

四条入口关系为：

```text
FindDictionaryValueOffset@0x59659C: entry nameIndex W1 -> CMP@0x59663C/0x59664C
NativeInstanceSupport@0x596D90: entry flag W1 -> CMP@0x596DA8
factory wrapper@0x59B14C: entry numparams W5 -> CMP@0x59B17C
```

三条 call-return 关系为：

| owner / compare | call source | 返回语义 |
| --- | --- | --- |
| `0x596D90 / 0x596DF0` | direct `BL@0x596DE8` | lazy native-class ID，与保存的 `classid` 比较 |
| `0x598538 / 0x5985A4` | indirect `BLR@0x5985A0` | stream `GetSize()`，与最小 9 bytes 比较 |
| `0x59A4B0 / 0x59A570` | direct `BL@0x59A56C` | `ttstr::IndexOf`，由 `CMN W0,#1` 实现 `== -1` |

## fresh 反编译与本地逐行对照

本轮 fresh `decompile` 覆盖
`0x59641C/0x59659C/0x596F50/0x597B1C/0x598E64/0x599174/0x5999F4/
0x59B7E8/0x596D90/0x598538/0x59A4B0/0x59B14C`。关键映射为：

| Android ARM64 | 输入数据流 | 当前源码 |
| --- | --- | --- |
| `FindNameIndex@0x59641C` | packed count/default、initial/loop parent 分开汇合，再做 unsigned state bound/equality | `PSBRawFile.cpp:52-79` 的 `uint32_t parent/state` 与相同循环顺序 |
| `FindDictionaryValueOffset@0x59659C` | entry nameIndex、packed upper、`CSINC/CSEL` lower/upper 更新进入最终 gate | `PSBRawFile.cpp:82-109` 的 `uint32_t lower/upper/middle/candidate` |
| `EnumMembers@0x596F50` | category 九路包含 helper-return `-1`；Array/Dictionary count 与 COW cleanup 独立 | `main.cpp:371-442` 显式保留 classifier `-1` continuation、两条容器循环和 `std::string` 生命周期 |
| `PropGetByNum@0x5976C4` | 5 路 packed count 与 W32 normalized signed index 比较 | `main.cpp:209-274` 的 modulo-2^32 normalization 与 signed bounds |
| `DecodeName@0x597B1C` | vector empty/grow/copy/reverse 的全部 pointer join | `PSBRawFile.cpp:112-135` 的 `vector<char>` parent walk、reverse、assign |
| `GetDictionaryKeys@0x598E64`、`reserve@0x599174`、grow@`0x59B7E8` | old-libstdc++ 三指针 vector 与 COW string atomic/non-atomic release | `PSBRawFile.cpp:280-306` 的 `vector<string>` reserve/emplace 生命周期 |
| `LoadStorage@0x598538` | stream vcall `X0` size 先做 `<9`，再窄化为 `uint32_t` | `PSBRawFile.cpp:482-500` 的 `stream->GetSize() < 9` 与后续 cast |
| `GetListAt@0x5999F4` | signed Array loop、unsigned Dictionary loop与各自 COW cleanup | `PSBMedia.cpp:149-218` 的 `tjs_int`/`uint32_t` 两条路径 |
| `Resolve@0x59A4B0` | direct `IndexOf` return 以 `CMN #1` 判断 `-1`；Contains bool 独立 | `PSBMedia.cpp:52-109` 的 first/next slash、strict lookup 与 delayed out write |
| `NativeInstanceSupport@0x596D90`、factory wrapper@`0x59B14C` | entry flag/argc 与 lazy-ID/callback 边界分开 | `main.cpp:455-471,732-754` 及 typed ncbind wrapper 的相同 gate |

## 目标逻辑摘要（不超过 10 行）

```text
trieState = packedBase + inputByte; if trieState >= packedCount: fail
require decodedParent == previousState; previousState starts at zero
dictionary binary search updates lower/upper, then requires lower < upper
category unknown: call diagnostic; if it returns, category remains -1
only categories 6/7 enumerate; packed count/default feeds their loop bounds
vector/COW cleanup reads oldRef through atomic or non-atomic path; delete iff oldRef <= 0
LoadStorage rejects streamSize < 9 before narrowing size to uint32
Resolve treats IndexOf return -1 as miss/last-segment and keeps Contains as a bool gate
typed wrappers compare entry flag/argc separately from callback/native-class call results
```

## IDB 改善

已在 23 个关键 compare 站点追加 `NORMAL-NZCV-INPUT-CONTRACT` 注释，覆盖 packed/default、
loop-carried、9 路 category、vector pointer、atomic/non-atomic refcount、入口参数与 call return：

```text
0x5964C8 0x596518 0x59663C 0x596674
0x597018 0x597100 0x597428 0x5974E0 0x597788
0x597CF0 0x597DBC 0x597DD8
0x599058 0x599254 0x599BB8 0x599C88 0x599CC4 0x59B958
0x596DA8 0x596DF0 0x5985A4 0x59A570 0x59B17C
```

IDB 已保存。

## 机械门禁

`verify_elf_surface.py` 现会独立重建 180 个 NZCV producer、解码 260 个输入寄存器、验证
全部 source word/destination，并从 producer predecessor 重走每条 CFG 路径。新增输出：

```text
normal_nzcv_input_surface=true owners=48 branches=180 operands=260 relations=320 single_source=235 multi_source=25 max_sources=9 instruction=313 entry=4 call_return=3 volatile_call_clobbers=0 paths_complete=true sha256=true
normal_nzcv_input_source_classes=true memory=148 transfer=60 select=4 arithmetic=101 direct_call_return=2 indirect_call_return=1 producer_classes=18
normal_nzcv_input_forms=true w=129 x=131 immediate=100 register=160 operand0=180 operand1=80 immediate_constants=12
```

该门禁固定的是 compare 输入数据流、默认值、循环携带值和 ABI 来源，不把 register
allocation 或 O3 compare lowering 冒充原始 C++ token。没有创建或修改 fixture；没有
`cpp/` 变更，因此不触发构建。15 个 stripped/O3 identifier/factorization 上限继续保留。
