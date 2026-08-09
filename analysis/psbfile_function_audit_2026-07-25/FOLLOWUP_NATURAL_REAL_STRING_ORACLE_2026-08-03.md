# Follow-up：天然 Real/String 运行时 oracle

日期：`2026-08-03`。本轮只读遍历仓库已有 PSB/MDF，扩展 Real/String 的天然样本盘点、
Android 原生 oracle 入口与 trace 元数据。没有修改 `cpp/`，没有生成、解密、破坏或改写
任何 PSB/MDF 测试物料。

## Android 权威边界

本轮 fresh IDA MCP 反编译了
`PSBValueDispatch_CreateVariant_guess@0x59673C`、
`PSBRawNode_GetDouble_guess@0x5992E8`、
`PSBRawNode_GetString_guess@0x598B58` 与
`PSBValueDispatch_getString_guess@0x596BC4`，并复核了公开赋值入口
`sub_A0FF94`（Real）与 `sub_A0FEB4`（UTF-8 String）。与本轮 oracle 直接相关的
伪代码为：

```text
CreateVariant/GetDouble: 1D -> 0.0；1E -> double(unaligned float32)；1F -> unaligned float64
String index 默认 0；15/16/17/18 分别读 u8/u16/u24/u32
GetString 非 category-4 先返回 null；未知 tag 由 classifier 抛错
offset = packed strings[index]；没有 count/index/range/NUL guard
GetString 返回 stringsData + offset 的 owner 内借用指针
CreateVariant 用该 const char* 赋值到 tvtString，结果拥有独立 String 内容
```

这些运行时检查只约束已由 Android 二进制证明的数据流、位模式与生命周期；不能唯一恢复
stripped/O3 已删除的 helper/type/field 名、member/free、header-inline token 或损坏输入的
first-fault 源码顺序。

## 全资产只读盘点

`scan_psbfile_natural_boundaries.py` 现在除 Integer/Resource 外，还记录每种 Real tag 的
分类与有限值极值、每种 String tag 的 index 极值、字符串表有效性、绝对数据 offset、
UTF-8 SHA/前缀和可达路径。它不保存所有节点，也不改写输入。完整扫描结果为：

```text
physical_candidates=222 unique_decoded_psb=112 parsed_unique_psb=112 failed_unique_psb=0
reachable_nodes_unique=23415372
Real 0x1D: 10500（zero=10500）
Real 0x1E: 23124（finite=23124）
Real 0x1F: 70527（finite=70527）
String 0x15: 2049872 / index_in_table=2049872
String 0x16: 6925356 / index_in_table=6925356
```

当前自然资产没有可达 String `0x17/0x18/0x2C`。扫描到的所有 Real 都是有限值或
`0x1D` 的正零，没有 NaN、Infinity 或 negative-zero；因此这些缺失边界不能用修改正常
文件字节的方式制造。

## 五个固定天然样本

| case | 物理输入 | decoded offset / 完整节点字节 | 公开/原始期望 |
| --- | --- | --- | --- |
| `tag1d-zero` | `m2logo.mtn` | `0x51BA` / `1d` | double bits `0000000000000000` |
| `tag1e-float32-widen` | `m2logo.mtn` | `0x5399` / `1e 85 eb 41 40` | double bits `000000a0703d0840` |
| `tag1f-float64` | `★プロローグa（始まり）.ks.scn` | `0x11A903` / `1f 9a 99 99 99 99 19 72 c0` | double bits `9a999999991972c0` |
| `tag15-utf8` | `m2logo.mtn` | `0x445B` / `15 46` | `レイヤ5`，10 bytes，data `0x57B8` |
| `tag16-utf8` | `config.psb` | `0x82D55` / `16 52 02` | `src/ボタン/btsys_タイトルbtn_on`，38 bytes，data `0x8D794` |

物理文件 SHA-256 分别固定为：

- `m2logo.mtn`：
  `4382de8283cc0782fd269b16d3157bf3a9ec28916440f9192690eb178c0c18fe`；
- `★プロローグa（始まり）.ks.scn`：
  `b3f47bdb7b54688f097d08d0f15ed9905c9c8e2413a3aaff6421b8e60f553616`；
- `config.psb`：
  `2f75a019655d9741dd613b2f18a07d4e54053fb2db4ff8b252be2ed72985eb75`。

tag `0x1F` 样本是已有 `mdf\0` 文件（物理 112,906 bytes，声明并解压为 1,278,267
bytes）。host 只在内存中解压后核对 offset/node；设备端仍接收未经修改的原始 MDF。
两个 String 数据 SHA-256 分别是
`ee099761ad5b7ef2161fb231afd6cfbc4f47283fee5021c19cfacf7eaaaf0257` 与
`9b8ce5d55aabd9c53396bd420c4e43928d64fa3a4e5fc4e09ef563b600af77f0`，且 host
要求固定长度之后的下一字节为 NUL。

## 本地实现逐项对照

| Android 数据流 | 本地复刻 |
| --- | --- |
| `1D/1E/1F -> 0.0/float32 widen/float64` | `cpp/plugins/psbfile/PSBPackedInternal.h:158-183` |
| 公开 Real 直接赋值为 `tvtReal` | `cpp/plugins/psbfile/main.cpp:631-635` |
| raw `GetDouble` 复用同一 decoder | `cpp/plugins/psbfile/PSBRawFile.cpp:360-365` |
| String category gate、默认 index 0、四种宽度 | `PSBRawFile.cpp:367-394` |
| raw 返回 `stringsData + offsets[index]` | `PSBRawFile.cpp:395-396` |
| 公开 String 直接从窄字符串赋值，无 `ttstr` 临时 | `main.cpp:636-643` |

逐项对照没有发现生产实现差异，因此本轮没有 `cpp/` 修改。

## Oracle 行为

- `--real-boundary` 对三个 case 先验证文件 SHA、decoded size、完整 node 和独立固定的
  little-endian double bits；公开 TJS 必须给出 `tvtReal` (`5`)，raw load 再直调
  `GetDouble@0x5992E8`，两边都按 8 字节位模式比较。
- `--string-boundary` 对两个 case 验证完整 node、字符串绝对 offset、长度、SHA 与 NUL；
  公开路径取得 `tvtString` (`2`) 后清空 PSBFile/value globals，再从独立
  `TJS_GLOBAL` Variant 读取 UTF-16 并转回 UTF-8，证明 copy 脱离 owner 仍存活。
- 独立 raw load 直调 `GetString@0x598B58`，要求返回地址严格等于
  `raw_owner_data + string_data_offset`，内容和 NUL 与同一 pin 一致。
- 新 Real/String trace target 分别为 15/17 个入口；新增的 `0xA0FF94`、`0xA0FEB4`、
  `0x598B58` 均补齐参数个数、地址名与 return-kind。

## 验证状态

- 完整只读扫描：222/112/23,415,372，112/112 解析成功，container/parse error 均为 0；
- 五组文件/node/value/string pin、MDF declared size、UTF-8 与 NUL host preflight 通过；
- scanner、adapter、runner、trace metadata 均通过 `py_compile`，CLI `--help` 暴露两个新
  mode，trace target 的 name/arg-count/return-kind 无遗漏；
- MacOS Debug/Release `psbfile-dll` target 均为最新，完整测试各通过
  598 assertions / 11 test cases；`git diff --check` 通过；
- 后续 Android ARM64 的三组 Real、两组 String 已在无 trace 与单次全量 trace 中全部
  `ok`；固定 APK/目标哈希与事件数见
  [FOLLOWUP_ANDROID_ARM64_RUNTIME_ORACLE_2026-08-03.md](FOLLOWUP_ANDROID_ARM64_RUNTIME_ORACLE_2026-08-03.md)。

本轮新增的是天然运行时覆盖，不改变逐函数源码 token 的证据上限。审计统计保持
`ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`。
