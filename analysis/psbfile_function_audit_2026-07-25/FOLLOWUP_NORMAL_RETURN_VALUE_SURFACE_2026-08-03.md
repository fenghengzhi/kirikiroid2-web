# Follow-up：normal W0/X0/D0 返回值生产者数据流

`NORMAL-RETURN-VALUE-CONTRACT`

日期：`2026-08-03`。本轮继续只使用权威 Android ARM64
`reference/libkrkr2/libkrkr2.so`、IDA MCP 与当前工作树；没有读取同版本源码、旧私库、
Git LFS 对象，也没有使用 Android/iOS ARMv7 材料。没有修改 `cpp/` 或测试物料。

## 结论

上一轮 normal CFG 已固定 114 个函数、4,956 条正常指令、162 个 `RET` 与四类
source-facing ABI；本轮继续回答其中每个 **W0/X0/D0 值返回点究竟由哪条指令产生**。

- 162 个 `RET` 精确分为 `void=28 / hidden-sret=5 / W0=96 / X0=19 / D0=14`。
  去掉没有 source-visible scalar value 的 void 与 hidden-sret 后，得到 **72 个 owner、
  129 个值返回 `RET`**。
- 沿 entry-rooted normal CFG 的完整 predecessor graph 回溯，共得到 **160 条
  reaching-definition 关系**：157 个显式 instruction writer、2 个 direct `BL` return、
  1 个 indirect `BLR` return；入口残留值为 0。
- 112 个 `RET` 为单来源；17 个为多来源 join，来源数分布为
  `2:10, 3:2, 4:3, 5:2`，最大 5。没有路径越过未声明的 volatile call return。
- 显式来源精确分为 `memory=9 / MOV transfer=116 / arithmetic-or-conversion=32`；
  producer mnemonic 共 17 类。
- fresh 反编译与当前源码逐项对照没有发现生产 `GAP`，继续维持
  `ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`。

## 恢复方法

本轮没有把“RET 前最近一条 MOV”当成返回值，也没有把 caller 反编译类型反推给 callee：

1. 沿用已独立验证的 42 张 switch table、normal-only noreturn 集与 entry-rooted CFG；
2. 由 114/114 IDB prototype 将 source-visible value owner 分成 W0、X0、D0；
3. 使用 IDA AArch64 processor module 的 canonical `CF_CHG1..CF_CHG6` 元数据枚举真实
   destination operand，并把 W/X 视为同一物理 GPR、B/S/D/H/Q/V 视为同一物理 SIMD
   register；
4. 从每个 `RET` 的 predecessor 开始反向遍历，遇到写物理 register 0 的指令即停；
5. `BL/BLR` 只作为 ABI call-return producer 停止，其他路径不得无声明地跨过调用；
6. 每条路径必须精确命中 manifest 声明的 writer/call，缺失、多余、错误 word、错误
   destination 或入口残留均使门禁失败。

canonical row 格式为：

```text
return: owner:u32, ret:u32, ret_word:u32, abi:u8, ndefs:u8
source: role:u8, kind:u8, operand:u8, address:u32, word:u32
```

129 行 canonical manifest 为 3,566 bytes，解压后 SHA-256 为：

```text
f48ee4b3fe77aea780fcac69922191641492851f591692a957601ad974191849
```

## ABI 与来源分类

| ABI | owner / RET | 说明 |
| --- | ---: | --- |
| W0 | 59 / 96 | `bool/int/uint/tjs_error/refcount` 等 32-bit source-facing 返回 |
| X0 | 12 / 19 | raw pointer、dispatch、adaptor 与 borrowed data/string pointer |
| D0 | 1 / 14 | `PSBRawNode::GetDouble@0x5992E8` 的全部 tag 分支 |

12 个 X0 owner 为：

```text
0x59673C 0x596BC4 0x596C70 0x597E98 0x597EB8 0x5981F8
0x598B58 0x5996E4 0x59993C 0x59A0B4 0x59A330 0x59ABD8
```

| 来源类 | 关系数 | 精确 producer |
| --- | ---: | --- |
| memory | 9 | `3 LDR + 1 LDRSB + 4 LDUR + 1 LDURSH` |
| transfer | 116 | `MOV` 的常量、零值、pointer、helper result 搬运 |
| arithmetic/conversion | 32 | `6 ADD + 1 AND + 3 BFI + 2 CINC + 3 CSET + 1 FCVTZS + 3 FMOV + 2 SBFX + 10 SCVTF + 1 SUB` |
| call return | 3 | `2 BL + 1 BLR` |

这些分类来自精确 instruction word 与 destination，不从本地变量名或返回表达式猜测。

## 17 个多来源 RET

| owner / RET | source 集 | source-facing 边界 |
| --- | --- | --- |
| `0x596D90 / 0x596E08` | `MOV@0x596DC8/0x596DD8/0x596DF8` | `0 / -1002 / -1` |
| `0x597854 / 0x597914` | `MOV@0x5978C8/0x5978D0/0x5978D8/0x5978F0` | `0 / -1006 / -1002 / -1001` |
| `0x597A40 / 0x597AB8` | `SUB@0x597A54 + MOV@0x597AAC` | decrement 后 refcount / delete 后 0 |
| `0x5981F8 / 0x598264` | `BL@0x598210 + MOV@0x598258` | fresh dispatch allocation / null |
| `0x598268 / 0x5983D8` | `MOV@0x598338/0x5983B0` | failed-Adopt diagnostic continuation false / success 与其他 continuation true |
| `0x598708 / 0x59875C` | `MOV@0x598748 + AND@0x598938 + MOV@0x598950` | invalid false / filter validation / no-filter true |
| `0x598960 / 0x598A30` | `MOV@0x5989EC + CSET@0x598A2C` | zero/default / final size comparison |
| `0x598D58 / 0x598E38` | `MOV@0x598E04/0x598E10` | dictionary hit true / miss false |
| `0x5992E8 / 0x5993EC` | `FMOV@0x59930C/0x5993E4` | `1.0 / diagnostic-default 0.0` |
| `0x599438 / 0x599528` | `MOV@0x59945C/0x599520` | common GetInt true/zero；其他值走独立 RET |
| `0x599554 / 0x5995D4` | `MOV@0x599574/0x5995CC` | category 0 / unknown fallback -1 |
| `0x5998C4 / 0x599934` | `CSET@0x599908 + MOV@0x599910` | resource non-null / EnsureContainer false |
| `0x59B14C / 0x59B25C` | 4 个 `MOV` + `BLR@0x59B1A8` | `-1001 / callback / 0 / -1008` |
| `0x59B28C / 0x59B370` | 3 个 `MOV` + `SBFX@0x59B33C` | root PropGet 的 `-1001/-1007/-1008/invoke` |
| `0x59B378 / 0x59B458` | 4 个 `MOV` + `SBFX@0x59B420` | root PropSet 的 `-1001/-1007/-1008/-1/invoke` |
| `0x59B48C / 0x59B554` | `MOV@0x59B528/0x59B530` | invoke true / null-native false |
| `0x59B570 / 0x59B6B0` | `MOV@0x59B5AC/0x59B678/0x59B680/0x59B688` | `-1001 / 0 / -1008 / -1004` |

## 三个 call-return producer

| owner / RET | call | 精确返回语义 |
| --- | --- | --- |
| `GetRootDispatch@0x5981F8 / 0x598264` | `BL operator new@0x598210` | 正常 throwing allocation 的 X0；owner-null 路径另写 null |
| `CreateEmptyAdaptor@0x59ABD8 / 0x59AC00` | `BL operator new@0x59ABE4` | 0x18-byte adaptor allocation；后续字段写不改 X0 |
| factory wrapper `0x59B14C / 0x59B25C` | callback `BLR@0x59B1A8` | callback 非零错误码原样返回；成功路径另写 0 或 -1008 |

`GetRootDispatch` 与 `CreateEmptyAdaptor` 都调用普通 throwing `operator new`，没有二进制
证据支持 `nothrow` 或 allocation-null 分支；唯一 null 是 `GetRootDispatch` 的 owner-null
显式 `MOV X0,XZR@0x598258`。

## fresh 反编译与本地逐行对照

本轮 fresh `decompile` 覆盖：

```text
0x596D90 0x597854 0x597A40 0x5981F8 0x598268 0x598708
0x598960 0x598D58 0x5992E8 0x599438 0x599554 0x5998C4
0x59B14C 0x59B28C 0x59B378 0x59B48C 0x59B570 0x59ABD8
```

| Android ARM64 | 返回数据流 | 当前源码 |
| --- | --- | --- |
| `NativeInstanceSupport@0x596D90`、`PropGet@0x597854` | 所有 TJS success/error fallback 分开写 W0 后汇合 | `main.cpp:121-206,455-471` 保留相同 guard、清空与错误码 |
| `Release@0x597A40` | nonzero 返回减后计数；归零销毁后返回 0 | `main.cpp:101-112` 的 refcount/delete 两路生命周期一致 |
| `GetRootDispatch@0x5981F8` | owner-null 返回 null；否则分配并构造 fresh dispatch | `main.cpp:690-701` 的 null gate 与 `new PSBValueDispatch` 一致 |
| `Load@0x598268` | Octet Adopt 失败的 helper-return 为 false；String/非法类型 helper-return 为 true | `PSBRawFile.cpp:442-479` 已显式保留三种 continuation |
| `Adopt@0x598708`、`Refresh@0x598960` | invalid/filter/no-filter 与 validation comparison 分开生产 bool | `PSBRawFile.cpp:176-217,516-540` 保留同一 validation 数据流 |
| `GetDictionaryValue@0x598D58` | miss false；hit 构造 retained child 后 true | `PSBRawFile.cpp:249-273` 保留 out 写入与 retain 顺序 |
| `GetDouble/GetInt/classifier@0x5992E8/0x599438/0x599554` | 14/14/8 个 RET 保留各 tag conversion 与 diagnostic fallback | `PSBRawFile.cpp:309-364`、`PSBPackedInternal.h` 保留 0.0/0/-1 默认值 |
| `CheckExistentStorage@0x5998C4` | `EnsureContainer && GetResourceData != null` 短路 | `PSBMedia.cpp:127-130` 逐项一致 |
| typed factory/root/load wrappers | membername、objthis、argc、native instance、invoke result 各自产生 TJS 错误码 | `main.cpp:732-754` 与 `ncbind.hpp` 生成层保持相同 wrapper/member-pointer 路径 |

## 目标逻辑摘要（不超过 10 行）

```text
for each source-visible RET: walk every normal predecessor until W0/X0/D0 is written
NativeInstanceSupport and PropGet return their exact TJS error/default codes
Release returns decremented refcount, but returns zero after destruction
GetRootDispatch returns null for no owner, otherwise the fresh allocation pointer
Load returns false only on failed-Adopt diagnostic continuation; other continuations return true
Adopt/Refresh/lookup/media predicates preserve invalid, filtered, hit/miss and short-circuit values
GetDouble/GetInt/classifier preserve diagnostic defaults 0.0/0/-1
typed wrappers preserve callback result and exact -1001/-1004/-1007/-1008 fallbacks
no value RET reaches entry residue or crosses an undeclared call return
```

## IDB 改善

已在 17 个多来源 `RET` 与唯一单来源 allocation call-return `RET@0x59AC00` 追加
`NORMAL-RETURN-VALUE-CONTRACT` 注释：

```text
0x596E08 0x597914 0x597AB8 0x598264 0x5983D8 0x59875C
0x598A30 0x598E38 0x5993EC 0x599528 0x5995D4 0x599934
0x59B25C 0x59B370 0x59B458 0x59B554 0x59B6B0 0x59AC00
```

IDB 已保存。

## 机械门禁

`verify_elf_surface.py` 现会从目标 ELF 重建正常 CFG，枚举全部 source-visible value RET，
验证 canonical payload 的 SHA-256、每个 source word/register-zero destination/call class，并
从每个 RET 重走所有 predecessor 路径。新增输出：

```text
normal_return_value_surface=true owners=72 returns=129 w0=96 x0=19 d0=14 relations=160 single_source=112 multi_source=17 max_sources=5 instruction=157 entry=0 call_return=3 volatile_call_clobbers=0 paths_complete=true sha256=true
normal_return_value_source_classes=true memory=9 transfer=116 arithmetic=32 direct_call_return=2 indirect_call_return=1 producer_classes=17
```

没有创建或修改 fixture；没有 `cpp/` 变更，因此不触发构建。15 个 stripped/O3
identifier/factorization 上限继续保留，但它们不阻塞后续二进制驱动的六维复原。

后续正向互补面已从全部 311 个 continuing `BL/BLR` fallthrough 追到 GPR0 首事件，
并证明 125 个 direct-void 调用的显式 use 为 0、34 个 `RET` reach 全部属于非 GPR
owner；详见
[FOLLOWUP_NORMAL_CALL_RESULT_FIRST_EVENT_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_CALL_RESULT_FIRST_EVENT_SURFACE_2026-08-03.md)。
