# Follow-up：normal branch predicate / NZCV producer contract

`NORMAL-BRANCH-PREDICATE-CONTRACT`

日期：`2026-08-03`。

## 结论

权威 Android ARM64 `libkrkr2.so` 的 114 个 MANIFEST FDE 中，entry-rooted normal CFG
共有 437 个条件分支，分布在 66 个 owner，并精确产生 874 条
`taken + fallthrough` 谓词边：

| 指令类 | 数量 |
| --- | ---: |
| `B.cond` | 180 |
| `CBZ` | 159 |
| `CBNZ` | 57 |
| `TBZ` | 24 |
| `TBNZ` | 17 |
| 合计 | 437 |

这 437 行现已由
[`verify_elf_surface.py`](verify_elf_surface.py)
逐行固定 branch word、owner、target、fallthrough、寄存器宽度/编号、测试位；全部 180 个
`B.cond` 又沿唯一线性 predecessor 回溯到真实 NZCV producer。回溯结果为
`176 CMP + 3 CMN + 1 SUBS`，未解析数为 0，最远距离为 7 条指令。canonical row digest 为：

```text
702d74e6774b6ecbc0917a9933a0106364d5e8ed6a246166a52b03b0dfe598b3
```

fresh 反编译复核没有发现本地生产实现 GAP；本轮没有修改 `cpp/`，因此不触发 Web 构建。
114 项结论继续为 `ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`。

## 机械恢复方法

1. 从已验证 FDE 起点进入 normal CFG；沿用专门的 normal noreturn 集，不导入 LSDA arc。
2. 对每个条件 branch 独立解码 target 与 fallthrough，并要求 CFG successor 恰为这两项。
3. `CBZ/CBNZ` 固定 sense、`W/X` 宽度与 `Rt`；`TBZ/TBNZ` 固定 sense、bit 与 `Rt`。
4. 对 `B.cond` 固定 condition code，并沿唯一的 `address - 4` predecessor 向前回溯。
5. 回溯不得跨越 call/branch/return；首个 Add/Sub `S=1` 指令必须可解码为
   `CMP/CMN/SUBS/ADDS`。
6. 将 branch row 和 producer row 编成 canonical byte stream，再固定 SHA-256。

这与上一轮 normal CFG successor/terminal 门禁互补：上一轮证明“分支通向哪里”，本轮继续
证明“分支实际测试什么、有符号性是什么、NZCV 来自哪条指令”。

## `B.cond` 条件码与有符号性

| 语义族 | 条件码计数 | 合计 |
| --- | --- | ---: |
| equality | `EQ=21, NE=52` | 73 |
| unsigned | `HS=3, LO=9, HI=70, LS=2` | 84 |
| signed | `GE=4, LT=13, GT=5, LE=1` | 23 |

目标中没有 normal-flow `MI/PL/VS/VC/AL/NV` 条件行。这里不能把 `HI/HS/LO/LS` 改写成
signed C++ 比较，也不能把 `GE/LT/GT/LE` 当成 packed unsigned 上界；新的 verifier 会对
condition code 和 producer word 的任一漂移直接失败。

## NZCV producer 全表

| producer | 宽度/形式 | 数量 |
| --- | --- | ---: |
| `CMP` | `W, immediate` | 94 |
| `CMP` | `W, register` | 16 |
| `CMP` | `X, immediate` | 2 |
| `CMP` | `X, register` | 64 |
| `CMN` | `W, immediate` | 2 |
| `CMN` | `X, immediate` | 1 |
| `SUBS` | `W, immediate` | 1 |

producer 距离分布为 `1:148, 2:26, 3:2, 5:1, 6:2, 7:1`。四个非 `CMP` 站点是：

- `0x597D10: cmn x23, #1`：`DecodeName_guess@0x597B1C` 的 vector 最大尺寸边；
- `0x59988C: subs w8, w8, #1`：单例引用计数 decrement 同时产出后继分支的 NZCV；
- `0x59A570: cmn w0, #1`：`Resolve@0x59A4B0` 的 `firstSlash == -1`；
- `0x59A5A8: cmn w21, #1`：同函数后续 `slash == -1`。

注意唯一 `SUBS` 是 immediate form；`0x71000508` 明确解码为
`subs w8, w8, #1`，不能记为 register form。

## compare-and-branch / test-bit contract

`CBZ/CBNZ` 的宽度与 sense：

| 组合 | 数量 |
| --- | ---: |
| `CBZ Wt` | 28 |
| `CBZ Xt` | 131 |
| `CBNZ Wt` | 40 |
| `CBNZ Xt` | 17 |

`TBZ/TBNZ` 只测试三个 bit：

| 组合 | 数量 | 主要语义 |
| --- | ---: | --- |
| `TBZ bit 0` | 22 | bool/status 为 false |
| `TBNZ bit 0` | 6 | bool/status 为 true |
| `TBZ bit 10` | 2 | `TJS_MEMBERMUSTEXIST (0x400)` 未设置 |
| `TBNZ bit 10` | 2 | `TJS_MEMBERMUSTEXIST (0x400)` 已设置 |
| `TBNZ bit 31` | 9 | signed TJS error 或 negative `IndexOf` |

因此 bit 31 与 bit 0 不能互换：`NativeInstanceSupport` 的负错误码和 `IndexOf == -1` 是
signed-negative gate；packed helper、`Contains`、`LoadStorage` 等返回的 bool 才是 bit 0。

## fresh 反编译复核与源码对照

本轮对下列高风险族重新调用 IDA decompile，并逐项对照当前源码：

| Android ARM64 | fresh 证据中的谓词 | 当前源码 |
| --- | --- | --- |
| `FindNameIndex_guess@0x59641C` | trie state/count 全部使用 unsigned 上界；terminal 用 equality | `cpp/plugins/psbfile/PSBRawFile.cpp:52-79` 的 `uint32_t state/parent` 与 `state >= nElementCount` |
| `FindDictionaryValueOffset_guess@0x59659C` | lower/upper/candidate 的二分比较均为 unsigned | `cpp/plugins/psbfile/PSBRawFile.cpp:82-109` 的 `uint32_t` lower/upper/middle/candidate |
| `PropGetByNum@0x5976C4` | W 位模加后按 signed 检查 `<0`、`>=count`；bit 10 控制 miss | `cpp/plugins/psbfile/main.cpp:209-274` 先保留 modulo-2^32，再 `int32_t` 比较，并测试 `TJS_MEMBERMUSTEXIST` |
| `PropGet@0x597854` | valid/owner、两只 dictionary bool 与 bit-10 miss 分支独立 | `cpp/plugins/psbfile/main.cpp:121-206` 保留相同分类、lookup 短路和 output clear/error 边界 |
| `DecodeName_guess@0x597B1C` | parent walk、vector growth、`size == SIZE_MAX` length error、reverse/assign | `cpp/plugins/psbfile/PSBRawFile.cpp:112-135` 的 `vector<char>` 拓扑与相同顺序 |
| `PSBFile::Load@0x598268` | Variant type equality、bool return gate和 diagnostic-return fallback 分离 | `cpp/plugins/psbfile/PSBRawFile.cpp:442-479` 的 String/Octet 分叉、`LoadStorage/Adopt` bool 与默认返回 |
| `PSBMedia::GetListAt@0x5999F4` | Array loop 为 signed `int`；Dictionary loop 为 unsigned count/index | `cpp/plugins/psbfile/PSBMedia.cpp:149-208` 分别使用 `tjs_int` 与 `uint32_t` |
| `PSBMedia::EnsureContainer@0x599E04` | `IndexOf` 以 bit 31 判断负值；cache/load/adaptor 用各自 bool/null gate | `cpp/plugins/psbfile/PSBMedia.cpp:19-49` 保留 signed slash、object/cache 与 adaptor null 路径 |
| `CreateAdaptor_guess@0x59A330` | NIS/TJS 失败为 signed-negative；sticky/throwOnFail 为 bit 0 | `cpp/core/plugin/ncbind.hpp:157-174,203-224` 保留 TJS error、null、sticky 的不同谓词 |
| `PSBMedia::Resolve@0x59A4B0` | 两个 `== -1` 由 `CMN #1` 实现；Contains 为 bool；out 仅成功尾写 | `cpp/plugins/psbfile/PSBMedia.cpp:52-109` 保留 `-1`、bool 与 delayed output/refcount 顺序 |
| `PSBFile_ncbFactory_FuncCall_guess@0x59B14C` | membername、one-void-param、callback status、NIS signed error 各自独立 | `cpp/core/plugin/ncbind.hpp:1408-1428` 保留 wrapper 的分支顺序与错误返回 |

代表性 Android 行为可压缩为：

```text
PropGetByNum: invalid -> -1006; non-Array -> -1001
count = decodePackedCount(); indexBits = uint32(num) + (num < 0 ? uint32(count) : 0)
index = bit_cast<int32>(indexBits)
if index < 0 || index >= count: return flag&0x400 ? -1001 : (clear(out), 0)
Resolve: firstSlash == -1 -> false; each slash == -1 marks last segment
if !Contains(segment) -> false; otherwise strict-get into retained current
only the successful last-segment tail assigns caller out and returns true
GetListAt: Array uses signed index<count; Dictionary uses unsigned index<count
```

## IDB 与验证状态

- 已在 12 个关键站点追加 `NORMAL-PREDICATE-CONTRACT` 注释：
  `0x597784,0x5977B8,0x5978BC,0x5979CC,0x597D10,0x59988C,0x599BBC,`
  `0x599C6C,0x599E40,0x59A3D0,0x59A570,0x59A5A8`；当前 IDB 已保存。
- verifier 的两个新增输出为：

```text
normal_branch_predicate_surface=true owners=66 branches=437 b_cond=180 cbz=159 cbnz=57 tbz=24 tbnz=17 edges=874 successors=true sha256=true
normal_nzcv_producer_surface=true producers=180 cmp=176 cmn=3 subs=1 unresolved=0 max_distance=7 equality=73 unsigned=84 signed=23 registers=true bits=true
```

上述 180 个 NZCV producer 的后续 260-operand/320-relation 输入来源闭环见
[FOLLOWUP_NORMAL_NZCV_INPUT_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_NZCV_INPUT_SURFACE_2026-08-03.md)。
它固定每个 `CMP/CMN/SUBS` 寄存器输入的完整前驱来源，不改变本报告的 branch/condition
统计口径。

- `python3 -m py_compile .../verify_elf_surface.py`：PASS。
- `verify_elf_surface.py`：PASS。
- 没有修改 fixture、APK、二进制或安装包；范围仅为现有 Android ARM64 权威 ELF。
