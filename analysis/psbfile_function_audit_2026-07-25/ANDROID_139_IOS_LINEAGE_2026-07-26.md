# Android 1.3.9 / iOS arm64 1.3.9 的 psbfile 谱系旁证

日期：`2026-07-26`。

## 证据边界

Android kirikiroid2 1.4.4 的 `reference/libkrkr2/libkrkr2.so` 仍是唯一权威来源。
Android 1.3.9 与 iOS arm64 1.3.9 只用于寻找源码结构候选；任何选择都必须再由目标
自身的指令、CFG、独立入口或完整内联克隆约束。跨版本或跨平台材料不能单独升级
verdict、恢复精确 token，或覆盖目标边界行为。

## 制品身份

| 制品 | SHA-256 / Build ID |
| --- | --- |
| Android 1.4.4 `libkrkr2.so` | SHA-256 `ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`；Build-ID `985d9f685e07ce4497472523c3e84b1f38989235` |
| Android 1.3.9 arm64 `libgame.so` | SHA-256 `05e2ff4c77f1561608ad7703153d2fb09855bf223237a85dc2267fff1388564f`；Build-ID `de22234bffa0545d276b705487ca0c3d35101386` |
| iOS thin arm64 | SHA-256 `a50b27d624a05c3936530e2b3a5dd79acfe267b3a99e6019cfca038ca269fbc2`；UUID `C332004C-E8E3-3780-9FE2-75D5A8DFDDDA` |

Android 两份 ELF 都是 stripped AArch64，采用相同 NDK/Clang/O3 条件。iOS arm64
可执行文件同样没有可用的 PSB C++ symbol、RTTI 或 DWARF 名称，但
`LC_FUNCTION_STARTS` 提供可靠函数边界，且 `cryptid=0`。

## Android 1.3.9 到目标 1.4.4

15 个受限节点均满足 `android_1.3.9_address = android_1.4.4_address + 0x3E0`。
两版对应簇的 1,683 条指令除分支/GOT/地址物化重定位外逐项一致。这证明旧 Android
与目标 emitted 结构同源，但相同 stripped/O3 条件没有补回源码 token。

## iOS arm64 的 15 项语义映射

| Android 1.4.4 | iOS 1.3.9 arm64 | iOS 保留的额外结构 |
| --- | --- | --- |
| `0x59641C` | `0x1000EB74C / 0x164` | trie；两次 packed-view helper call |
| `0x59659C` | `0x1000EB8B0 / 0x120` | 二分查找；两次 packed-view helper call |
| `0x59673C` | `0x1000EB9D0 / 0x3CC` | classifier、integer、double/string/resource helper calls |
| `0x596BC4` | `0x1000EC010 / 0xE4` | string helper 调 packed-view constructor |
| `0x596C70` | `0x1000EC0F4 / 0x154` | chunkData gate、offset/length views |
| `0x596F50` | `0x1000EC458 / 0x448` | CreateVariant、DecodeName 与 callback 分层 |
| `0x597AD4` | `0x1000EC248 / 0x40` | 双 vptr、ref=1、holder retain、独立 node、valid |
| `0x597B1C` | `0x1000ECD6C / 0x25C` | 三次 packed-view ctor、vector growth |
| `0x598A64` | `0x1000ED8E4 / 0x3C` | hidden-sret copy、AddRef、Release、source null |
| `0x598B58` | `0x1000ED94C / 0xFC` | classifier 与 packed-view constructor |
| `0x5992E8` | `0x1000EDDE0 / 0x8`、`0x1000EBF1C / 0xF4` | wrapper tail-call raw-double decoder |
| `0x599438` | `0x1000EDDE8 / 0xFC` | integer decoder calls 与 wrapper 特化 |
| `0x5996E4` | `0x1000EDF78 / 0x14C` | 两只 packed views；raw resource accessor |
| `0x599DD8` | `0x1000EE728 / 0x2C` | media slot 10；release/null-store |
| `0x59A0B4` | `0x1000EE92C / 0x74` | `Resolve → GetResource → raw-node dtor` |

地址和大小不单独作为等同性证据；映射由字符串簇、函数边界、tag/packed 数据流、
vtable 槽、调用拓扑与生命周期共同建立。

## iOS arm64 保留的源码名字

`___assert_rtn` 保留原路径
`/Volumes/E/Projects/kirikiri2_mob/kirikiri2/src/plugins/PSBFile.cpp`，以及
`CreateVariant`、`PropGetByNum`、`PropGet` 三个 `__func__` 字面量。目标没有保存
这些精确 identifier，因此本地与 IDB 仍以 `_guess` 标记证据上限。

## iOS arm64 保留的 helper 与生命周期边界

- MDF helper `0x1000ED5B4` 恰有 typed Load `0x1000ED208` 与 storage loader
  `0x1000ED508` 两个 caller；目标两份完整 inline clone约束全部行为。
- packed-view constructor `0x1000EE0C4` 按 `nBytes/count/width/values` 次序写四个
  语义字段，并在 11 个 consumer 中共有 21 个 direct callsite。
- shared classifier `0x1000EBD9C` 有 10 个 direct caller和一条 raw-wrapper tail call；
  integer decoders `0x1000EE14C/0x1000EE1B4` 与 raw-double dispatcher
  `0x1000EBF1C` 保留当前 numeric 分层。
- `Transfer@0x1000ED8E4` 保留 result copy、AddRef、Release 与 source clear。
- assignment `0x1000ED740`、raw-node constructor `0x1000EEF28`、root constructor
  `0x1001263B8`、dispatch constructor `0x1000EC248` 与 shared Release
  `0x1000EEEFC` 支持“PSBFile-compatible holder 子对象 + 独立 node”的分层。
- `GetResourceData@0x1000EE92C` 直接保留 `Resolve → GetResource → raw-node dtor`。

这些 retained boundaries只选择由 Android 目标完整 body/residual 已约束的源码候选；
精确 type、field、member/free、pointer/reference、header-inline 与 special-member token
继续受限。

## 对 15 个 verdict 的影响

- 新发现的确定行为 GAP：`0`。
- 由旧 Android 或 iOS arm64 单独升级为 `ALIGNED`：`0`。
- 统计继续为 `ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`。
- iOS arm64 direct calls只加强 target-compatible factorization 选择；Android 目标仍是
  全部分支、默认值、生命周期和边界行为的唯一权威。
