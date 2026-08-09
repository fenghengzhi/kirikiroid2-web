# Follow-up：天然整数 tag 运行时 oracle 扩面

日期：`2026-08-02`。本轮在全资产可达节点盘点的基础上，把 Android oracle 从一个
tag `0x09` 节点扩展为七个天然节点。没有修改 `cpp/`，没有生成、破坏、解密或改写任何
PSB/MDF，也没有引入新的测试物料。

## Android 权威边界

本轮 fresh IDA MCP 分别反编译
`PSBValueDispatch_CreateVariant_guess@0x59673C`、
`PSBRawNode_GetInt_guess@0x599438` 与
`PSBRawNode_GetDouble_guess@0x5992E8`。与本轮 oracle 直接相关的伪代码为：

```text
tag 0x04: full = 0
tag 0x05/0x06/0x07/0x08: full = sign_extend(payload, 8/16/24/32)
tag 0x09: full = sign_extend(payload, 40)
CreateVariant: result = Integer(full)
GetInt, tag 0x04..0x08: return signed32(full)
GetInt, tag 0x09..0x0C: return signed32(low32(payload))
GetDouble: return double(full)
```

这里的运行时覆盖只检验二进制已经固定的数据流和 ABI 结果；它不能消除
`CreateVariant/GetInt/GetDouble` 精确 helper 名、member/free、header-inline 或源代码
factorization 的 `EVIDENCE_LIMITED` 上限。

## 全资产数值盘点

`scan_psbfile_natural_boundaries.py --json` 现在除 tag 计数外，还为每个整数 tag 输出完整
Variant 最小值、最大值，以及 Variant 与 `GetInt` 差值最大的天然节点，并只为这些候选
反向恢复路径。完整扫描仍为 222 个物理输入、112 份唯一 PSB、23,415,372 个可达节点、
112/112 解析成功；天然整数分布为：

| tag | 可达节点数 | 全局最小值 | 全局最大值 | 最大 `abs(Variant-GetInt)` |
| --- | ---: | ---: | ---: | ---: |
| `0x04` | 3,610,586 | 0 | 0 | 0 |
| `0x05` | 2,213,209 | -122 | 127 | 0 |
| `0x06` | 1,621,988 | -2500 | 27747 | 0 |
| `0x07` | 11,874 | 33793 | 4195948 | 0 |
| `0x08` | 126 | 33556076 | 37750380 | 0 |
| `0x09` | 31,186 | 4278190080 | 4294967295 | 4294967296 |

当前唯一 PSB 中仍没有 `0x0A/0x0B/0x0C`；这三种 tag 的运行时覆盖继续需要新天然资产，
不能从正常文件改字节制造。

## 七个固定 oracle 节点

`--integer-boundary` 现在固定两份已有输入及 SHA-256：

- `reference/xp3/logo_test/m2logo.mtn`：
  `4382de8283cc0782fd269b16d3157bf3a9ec28916440f9192690eb178c0c18fe`；
- `reference/xp3/caution_test/DRACU-RIOT/data/motion/autoskip.psb`：
  `131b436405c0aa8cd137a496c98fb77a77da95ca29e8af4597da1f7a42fd4a5d`。

| case | 文件 | offset / 原始字节 | Variant / `GetInt` |
| --- | --- | --- | --- |
| `tag04-zero` | `m2logo.mtn` | `0x48CB` / `04` | `0` / `0` |
| `tag05-signed8-negative` | `m2logo.mtn` | `0x40E6` / `05 8e` | `-114` / `-114` |
| `tag06-signed16-negative` | `m2logo.mtn` | `0x3705` / `06 7c ff` | `-132` / `-132` |
| `tag07-signed24` | `m2logo.mtn` | `0x30FD` / `07 13 00 08` | `524307` / `524307` |
| `tag08-signed32` | `m2logo.mtn` | `0x4174` / `08 6c 06 40 02` | `37750380` / `37750380` |
| `tag09-low32-sign` | `m2logo.mtn` | `0x36F8` / `09 00 00 00 ff 00` | `4278190080` / `-16777216` |
| `tag09-low32-all-ones` | `autoskip.psb` | `0x12C3` / `09 ff ff ff ff 00` | `4294967295` / `-1` |

每个 case 都通过真实 `new PSBFile(path)` 和 root Dictionary/Array adaptor 取公开 Variant；
另一次 raw load 在相同 offset 构造借用型 Android `PSBRawNode` view，分别调用
`0x599438` 与 `0x5992E8`。adapter 逐项要求：

1. 输入 SHA、完整节点字节和 tag 宽度匹配；
2. 公开结果类型为 `tvtInteger`，64 位 payload 等于表中 Variant；
3. `W0` 的有符号解释等于表中 `GetInt`，同时 `X0` 等于该值的零扩展低 32 位；
4. `D0 == double(Variant)`；
5. 每个 case 单独清理输出 Variant、TJS globals 和 raw owner holder。

## 本地实现对照

- `scan_psbfile_natural_boundaries.py` 只从 root 遍历真实 packed Array/Dictionary；每个
  去重 PSB 内只为整数极值保留 offset，最后恢复路径，不改变输入。
- `run_psbfile_load_adb.py` 集中固定两份输入与七个 case；启动前验证 SHA，设备端每份输入
  只按 ASCII alias staging，不改内容。
- `psbfile_load.py::run_integer_boundary_case` 按 tag `0x04..0x0C` 的完整节点长度验证 pin，
  再分别检查完整 Variant、低 32 位 `GetInt` 和完整 `GetDouble`；不再把 tag `0x09` 的
  full value 错当成 `X0` 期望。

## 验证状态

- 完整扫描：222/112/23,415,372，parse/container error 均为 0；
- 两份 SHA、七组 offset/字节 pin 和七组 signed-low32 独立主机检查通过；
- 三个 Python 文件 `py_compile` 通过，runner `--help` 与 `git diff --check` 通过；
- 后续 Android ARM64 实测已补齐：七组原生 case 在无 trace 与单次全量 trace 中全部
  `ok`。固定 APK/目标哈希与事件数见
  [FOLLOWUP_ANDROID_ARM64_RUNTIME_ORACLE_2026-08-03.md](FOLLOWUP_ANDROID_ARM64_RUNTIME_ORACLE_2026-08-03.md)。

审计总数保持 `ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`。
