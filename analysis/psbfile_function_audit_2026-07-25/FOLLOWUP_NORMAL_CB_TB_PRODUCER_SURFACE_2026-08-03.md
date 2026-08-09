# Follow-up：normal CB/TB tested-register producer contract

`NORMAL-CB-TB-PRODUCER-CONTRACT`

日期：`2026-08-03`。

## 结论

上一轮已经固定 216 个 `CBZ/CBNZ` 与 41 个 `TBZ/TBNZ` 的 branch word、寄存器、
宽度、测试位、taken/fallthrough；本轮继续回答每个 branch 的 tested register **从哪里来**。

对权威 Android ARM64 `libkrkr2.so` 的 114 个 MANIFEST FDE 重建 entry-rooted normal
CFG 后，257 个 CB/TB branch 分布在 57 个 owner，共得到 287 条 reaching-definition
关系：

| 来源角色 | 关系数 |
| --- | ---: |
| 显式写寄存器指令 | 246 |
| 函数入口参数 | 14 |
| 调用返回 `W0/X0` | 27 |
| 合计 | 287 |

244 个 branch 只有一个来源；其余 13 个为 CFG join，来源数分布为
`2:5, 3:1, 4:5, 5:2`，最大 5。所有路径均闭合，未出现任何跨越 `BL/BLR` 后继续使用
volatile `X1..X18` 的未定义来源。

这 287 条关系已作为 6,498-byte canonical manifest 压缩嵌入
[`verify_elf_surface.py`](verify_elf_surface.py)，解压后 SHA-256 为：

```text
7126e73f3c4c1a380d9b5d0c338deaa1420a41ce58ca1ed4e915f2ac9fef53f2
```

fresh 反编译与当前源码对照没有发现生产实现 GAP；本轮没有修改 `cpp/`，不触发构建。
114 项结论继续为 `ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`。

## 恢复方法与一次工具纠偏

本轮没有把“branch 前一条指令”当成 producer，也没有单独相信寄存器值追踪器：

1. 沿用已经独立验证的 normal CFG、switch destination 与 normal-only noreturn 集；
2. 为每个 CB/TB branch 解码 `Rt`，从全部 predecessor 反向遍历，而不是只扫线性地址；
3. 使用 IDA AArch64 processor module 的 canonical `CF_CHG1..CF_CHG6` 元数据识别真正
   写 `Wn/Xn` 的 operand，并保存 producer word 与 operand index；
4. `BL/BLR` 对 `W0/X0` 是 call-return producer，对 `X1..X18` 是必须报错的 volatile
   clobber，对 `X19..X28` 则按 AAPCS64 callee-saved 继续回溯；
5. 无 predecessor 且没有显式 writer 时，来源必须是同一 owner 的入口参数；
6. 每条反向路径必须在 manifest 中声明的 writer/call/entry 精确停止，缺失或多余均失败。

代表性纠偏是 `CBZ X0@0x597174`。通用寄存器值追踪曾错误报告
`ADD X1,SP,#var_D8@0x597168`；机器级最后写 `X0` 的实际指令是
`LDR X0,[SP,#var_D8]@0x597170`。新的抽取以 `CF_CHG*` operand 为准，并在 verifier 中
再次从 raw word 验证目标寄存器，因此不会把栈地址构造误当成载入值。

canonical row 格式为：

```text
branch:   owner:u32, branch:u32, kind:u8, width:u8, reg:u8, bit:u8, ndefs:u8
producer: role:u8, kind:u8, operand:u8, address:u32, word:u32
```

verifier 会独立重建 257 个 branch header，检查所有 producer 地址属于对应 normal graph、
word 未漂移，并按 `LDP Rt/Rt2`、`STLXR Ws` 与其余 producer 的真实 destination bitfield
复核寄存器编号。

## producer 指令与源码层分类

| producer | 数量 | 源码层含义 |
| --- | ---: | --- |
| `LDR` | 135 | 字段、栈临时、packed scalar 或指针载入 |
| `LDUR` | 4 | packed 16/32-bit 非对齐载入 |
| `LDP` | 7 | vector/member-pointer pair 等双寄存器载入 |
| `LDAR` | 1 | static guard acquire-load |
| `MOV` | 51 | 参数/返回/默认值/布尔值传递 |
| `STLXR` | 12 | 原子引用计数或 guard store-exclusive status |
| `AND` | 11 | packed 24-bit 或 bool/flag mask |
| `ADD` | 1 | W32 index normalization，随后测试 bit 31 |
| `SUB` | 16 | 引用计数或索引/计数结果 |
| `CSINV` | 1 | 条件化标量结果 |
| `LSR` | 2 | packed scalar 移位 |
| `ORR` | 3 | 标量/flag 合成 |
| `CSET` | 1 | 生命周期布尔 guard |
| `CSEL` | 1 | 条件选择结果 |
| direct `BL` return | 20 | helper 的 `W0/X0` 返回 |
| indirect `BLR` return | 7 | TJS/NCB vcall 的 `W0` 状态 |
| `ENTRY` | 14 | ABI 入参直接被测试 |

按源码语义聚合，显式 instruction producer 为
`147 memory + 51 transfer + 12 atomic-status + 36 scalar`。17 个 producer class 均在
manifest 中实际出现；这里没有用某一条代表行替代同类其他 branch。

按 branch kind 加权后的关系数为
`CBZ=185, CBNZ=59, TBZ=24, TBNZ=19`。它们大于/等于 branch 数的差值正好来自下节的
13 个多来源 join。

## 13 个多来源 join

| owner / branch | reaching producer | 二进制语义 |
| --- | --- | --- |
| `0x59659C / 0x5965F4` | `0x5965C4 LDRB`, `0x5965CC MOV 0`, `0x5965D4 LDURH`, `0x5965E0 AND 24-bit`, `0x5965E8 LDUR` | dictionary packed count 的 5 种 tag/default |
| `0x596F50 / 0x5972B4` | `0x5970A8 LDRB`, `0x597218 MOV 0`, `0x597220 LDRH`, `0x59722C AND 24-bit`, `0x597234 LDUR` | `EnumMembers` packed count 的 5 种来源 |
| `0x597B1C / 0x597D04` | `0x597CC4 AND`, `0x597CD8 MOV 0` | decoded count 与 unknown-tag default |
| `0x597B1C / 0x597D8C` | `0x597C9C MOV 0`, `0x597D48 LDR`, `0x597D84 MOV`, `0x597DA4 MOV` | vector begin/end/path pointer join |
| `0x597B1C / 0x597DAC` | `0x597CC4 AND`, `0x597CD8 MOV 0` | 同一 decoded-count/default join 的后续消费 |
| `0x597B1C / 0x597E14` | `0x597D3C MOV`, `0x597D50 MOV 0`, `0x597DB4 MOV 0`, `0x597DC8 MOV` | allocation/current pointer 的四条路径 |
| `0x598268 / 0x598320` | `0x5982E0 MOV X20,X0`, `0x5983F8 MOV X20,X0` | 两个 allocation result 路径 |
| `0x599174 / 0x59928C` | `0x599208 LDP`, `0x599284 LDR` | vector begin 的旧/新 storage 路径 |
| `0x5999F4 / 0x599BE4` | `0x599B3C LDRB`, `0x599BC4 LDRH`, `0x599BD0 AND 24-bit`, `0x599BD8 LDUR` | Dictionary lister packed count |
| `0x59A4B0 / 0x59A5E0` | `0x59A5C4 LDR`, `0x59A5DC LDR` | 同一 stack slot 的两个控制流来源 |
| `0x59A4B0 / 0x59A654` | `0x59A5C4`, `0x59A5DC`, `0x59A638`, `0x59A650` 的 `LDR` | segment/rest 临时的四路径 join |
| `0x59A4B0 / 0x59A714` | 同上四个 `LDR` | 清理前再次检查相同 pointer join |
| `0x59AEEC / 0x59B01C` | `0x59AFA0 CSET`, `0x59AFB8 MOV 0`, `0x59B000 MOV 1` | 临时对象是否需要 `Release` 的三路径 guard |

这些 join 均来自真实 CFG predecessor；尤其 `DecodeName` 与 `Resolve` 不能被线性“最近
定义”简化，否则会丢失默认值、分配失败或临时对象清理路径。

## 入口参数与调用返回边界

14 条入口来源如下；冒号右侧为直接被 branch 测试的 ABI 参数：

```text
0x596E24:0x596E2C X2          0x596F0C:0x596F10 X2
0x5975E0:0x5975F0 X2          0x5976C4:0x5977B8 W1 bit10
0x597854:0x597880 X2          0x598960:0x5989E4 W1 bit0
0x598AAC:0x598AB4 X1          0x59B14C:0x59B170 X2
0x59B28C:0x59B2B8 X2          0x59B378:0x59B39C X2
0x59B378:0x59B3BC X5          0x59B378:0x59B3C0 X4
0x59B48C:0x59B4AC X2          0x59B570:0x59B5A8 X2
```

27 条 call-return 来源精确分为 20 个 direct `BL` 与 7 个 indirect `BLR`：

| owner | branch <- call producer | 返回语义 |
| --- | --- | --- |
| `0x597854` | `0x597928 <- BL@0x597924`; `0x597980 <- BL@0x59797C`; `0x597998 <- BL@0x597994` | UTF-16 compare、name lookup、dictionary lookup |
| `0x598268` | `0x598400 <- BL@0x5983FC` | `uncompress` status |
| `0x59849C` | `0x5984C4 <- BL@0x5984C0` | `__cxa_guard_acquire` |
| `0x598538` | `0x598610 <- BL@0x59860C`; `0x598690 <- BL@0x59868C` | `Adopt` bool、`uncompress` status |
| `0x598C58` | `0x598C98 <- BL@0x598C94`; `0x598CB0 <- BL@0x598CAC` | strict name/dictionary lookup |
| `0x598D58` | `0x598D98 <- BL@0x598D94`; `0x598DB0 <- BL@0x598DAC` | non-strict name/dictionary lookup |
| `0x5995D8` | `0x599680 <- BL@0x59967C` | raw dictionary getter bool |
| `0x5998C4` | `0x5998F0 <- BL@0x5998EC` | `EnsureContainer` bool |
| `0x59993C` | `0x599968 <- BL@0x599964` | `EnsureContainer` bool |
| `0x5999F4` | `0x599A30 <- BL@0x599A2C`; `0x599A48 <- BL@0x599A44` | `EnsureContainer`、`Resolve` bool |
| `0x599E04` | `0x599EC4 <- BL@0x599EC0` | UTF-16 container equality |
| `0x59A0B4` | `0x59A0E8 <- BL@0x59A0E0` | `Resolve` bool |
| `0x59A330` | `0x59A3F8 <- BLR@0x59A3F4` | adaptor-create TJS result，bit 31 |
| `0x59A4B0` | `0x59A518 <- BLR@0x59A514`; `0x59A684 <- BL@0x59A680` | native-instance TJS result、Contains bool |
| `0x59AEEC` | `0x59AFD4 <- BL@0x59AFCC` | `ttstr` concat result pointer |
| `0x59B14C` | `0x59B1AC <- BLR@0x59B1A8`; `0x59B1E0 <- BLR@0x59B1DC` | factory callback status、NIS TJS result |
| `0x59B28C` | `0x59B310 <- BLR@0x59B30C` | root getter TJS result |
| `0x59B378` | `0x59B3EC <- BLR@0x59B3E8` | root setter TJS result |
| `0x59B570` | `0x59B5F4 <- BLR@0x59B5F0` | load wrapper TJS result |

call-return 只允许落在 `W0/X0`；反向切片中出现的其他 volatile call clobber 数为 0。

## 41 个 test-bit branch 的来源语义

| bit | branch 数 | reaching value 的实际来源 |
| ---: | ---: | --- |
| 0 | 28 | bool helper/callback return、entry bool、moved/masked bool、atomic guard byte、临时生命周期 guard、Itanium member-pointer low flag |
| 10 | 4 | `TJS_MEMBERMUSTEXIST (0x400)`：一处直接入口 `W1`，三处由 `MOV W20,W1` 保存 |
| 31 | 9 | W32 normalized index、`IndexOf` 负值或 TJS/NIS signed error result |

关键边界包括：

- `TBZ W1,#0@0x5989E4` 直接测试 `Refresh(bool validateOffsets)` 的入口参数；clear path
  在写完 header view 后直接返回 true，set path 才做 signed size/offset 比较。
- `TBZ W9,#0@0x59B4BC` 与 `0x59B618` 的 `W9` 都来自前一只 `LDP` 的第二 operand；
  这是 ARM64 Itanium pointer-to-member 的 low-bit virtual/direct dispatch flag，不是业务 bool。
- `TBNZ W24,#0@0x59B01C` 的三个 reaching definition 是 `CSET/MOV 0/MOV 1`，决定
  RegistItem 字符串构造链中的临时 dispatch 是否 `Release`。
- bit 31 的九条路径全部来自 W32 signed-negative 语义，没有任何 bool producer 被混入。

## fresh 反编译与当前源码对照

本轮对多来源、入口参数、call-return 与 member-pointer 高风险 owner 重新 decompile，结果：

| Android ARM64 owner | tested-register 证据 | 当前源码映射 |
| --- | --- | --- |
| `EnumMembers@0x596F50` | List/Dictionary 的 packed count 各自保留 tag/default producer；callback/null gate 分离 | [`main.cpp:371-442`](../../cpp/plugins/psbfile/main.cpp) 的两条容器循环与 callback 参数/生命周期 |
| `FindDictionaryValueOffset@0x59659C` | 5-way packed width/default join 后再执行 unsigned binary search | [`PSBRawFile.cpp:82-109`](../../cpp/plugins/psbfile/PSBRawFile.cpp) 的 `PsbArray_guess` 与 `uint32_t` 上下界 |
| `DecodeName@0x597B1C` | count/default、vector pointer、allocation pointer 三组 CFG join 均闭合 | [`PSBRawFile.cpp:112-135`](../../cpp/plugins/psbfile/PSBRawFile.cpp) 的 `vector<char>` parent walk/reverse/assign |
| `PSBFile::Load@0x598268` | 两个 allocation result、Adopt bool、uncompress status 均保持独立 | [`PSBRawFile.cpp:442-479`](../../cpp/plugins/psbfile/PSBRawFile.cpp) 的 String/Octet、decode/copy、Adopt 分支 |
| `Refresh@0x598960` | `W1 bit0` 直接来自入口；false 直接 true，true 才验证 offsets | [`PSBRawFile.cpp:176-217`](../../cpp/plugins/psbfile/PSBRawFile.cpp) 的同顺序 header 写入与 gate |
| `vector::reserve@0x599174` | `X22` 来自 `LDP/LDR` 两条 begin-storage 路径，随后统一 null gate/refcount cleanup | [`PSBRawFile.cpp:280-306`](../../cpp/plugins/psbfile/PSBRawFile.cpp) 的 gnustl vector/COW-string 调用层 |
| `PSBMedia::GetListAt@0x5999F4` | Ensure/Resolve call bool 与 Dictionary 4-way count join 独立 | [`PSBMedia.cpp:149-219`](../../cpp/plugins/psbfile/PSBMedia.cpp) 的两级 bool gate和 Array/Dictionary 分支 |
| `PSBMedia::Resolve@0x59A4B0` | native/TJS error、Contains bool、四路径 ttstr pointer join 与 delayed out write | [`PSBMedia.cpp:52-109`](../../cpp/plugins/psbfile/PSBMedia.cpp) 的 root/segment/current 生命周期 |
| `RegistItem@0x59AEEC` | 三来源 W24 guard 只控制对应临时对象 Release，registration/item cleanup 顺序不变 | [`main.cpp:729-755`](../../cpp/plugins/psbfile/main.cpp) 与 ncbind 注册模板生成的相同对象链 |
| root typed invoke `0x59B48C` | entry native pointer null、member-pointer low bit、dispatch result/ref handling | [`ncbind.hpp:1087-1191`](../../cpp/core/plugin/ncbind.hpp) 与 [`ncbind.hpp:1452-1482`](../../cpp/core/plugin/ncbind.hpp) 的 typed property invoke |
| load wrapper `0x59B570` | membername/objthis/argc gate、NIS signed error、native pointer null、member-pointer low bit、bool result | [`ncbind.hpp:1087-1191`](../../cpp/core/plugin/ncbind.hpp) 与 [`ncbind.hpp:1325-1358`](../../cpp/core/plugin/ncbind.hpp) 的 method invoke；注册点为 [`main.cpp:751-755`](../../cpp/plugins/psbfile/main.cpp) |

代表性 Android 行为可压缩为不超过 10 行：

```text
Refresh(data,size,validate): write all header pointers from eight u32 offsets
if !validate: return true
return size>offsetEncrypt && size>=offsetNames && size>=offsetStrings &&
       size>=offsetStringsData && size>=offsetChunkOffsets &&
       size>=offsetChunkLengths && size>=offsetChunkData && size>offsetEntries
typed invoke: reject missing obj/native/result as each wrapper specifies
decode ARM member pointer; low bit selects virtual-slot load versus direct target
call target; TJS_FAILED(result) is bit31, ordinary bool/status is bit0
```

本地恰好保留这些不同来源和 gate；没有发现可由本轮证据驱动的 `cpp/` 差异。

## IDB 与验证状态

- 已在 17 个关键站点追加 `NORMAL-CB-TB-PRODUCER-CONTRACT` 注释：
  `0x597170,0x5965F4,0x5972B4,0x597D04,0x597D8C,0x597DAC,0x597E14,`
  `0x598320,0x59928C,0x599BE4,0x59A5E0,0x59A654,0x59A714,0x59B01C,`
  `0x5989E4,0x59B4BC,0x59B618`；当前 IDB 已保存。
- verifier 新增输出：

```text
normal_cb_tb_producer_surface=true owners=57 branches=257 relations=287 single_source=244 multi_source=13 max_sources=5 instruction=246 entry=14 call_return=27 volatile_call_clobbers=0 paths_complete=true sha256=true
normal_cb_tb_source_classes=true memory=147 transfer=51 atomic=12 scalar=36 direct_call_return=20 indirect_call_return=7 producer_classes=17
```

- `python3 -m py_compile .../verify_elf_surface.py`：PASS。
- `verify_elf_surface.py`：PASS。
- 没有创建或修改 fixture；没有修改 APK、二进制、安装包或 `cpp/`。
