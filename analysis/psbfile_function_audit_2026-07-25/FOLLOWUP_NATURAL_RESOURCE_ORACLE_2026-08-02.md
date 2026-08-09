# Follow-up：天然 Resource 借用/复制生命周期 oracle

日期：`2026-08-02`。本轮只使用仓库既有天然 PSB，补齐 Resource 的可达节点盘点、
Android 原生 oracle 入口和本地生命周期回归测试。没有修改 `cpp/`，没有生成、解密、
破坏或改写任何 PSB/MDF 测试物料。

## Android 权威边界

本轮 fresh IDA MCP 反编译了
`PSBRawNode_GetResource_guess@0x5996E4`、
`PSBValueDispatch_CreateVariant_guess@0x59673C`、
`TJSAllocVariantOctet_guess@0xA0E0F4`、
`tTJSVariant_CopyRef_guess@0xA0FB64` 与
`tTJSVariant_dtor_guess@0xA0F778`。直接相关伪代码不超过 10 行：

```text
if owner.header.chunkData == null: return null        // size 不写
offsets = PackedArray(chunkOffsets); lengths = PackedArray(chunkLengths)
index = 0
tag 19/1A/1B/1C: index = u8/u16/u24/u32(node+1); default 保持 0
*size = lengths[index]
return chunkData + offsets[index]                     // owner 内借用指针
CreateVariant(category 5): data = getResource(node, size)
非空数据构造复制型 Octet(ref=1)；CopyRef 到 result 后 ref=2
临时 Variant 析构再 Release，最终 result 独占该 Octet(ref=1)
```

目标函数没有 Resource index/count guard。该事实决定了自然输入必须先证明 index 同时落在
offset/length 两张表内，不能把任意看似 `0x1A` 的字节序列直接送入成功路径。

## 全资产 Resource 盘点

`scan_psbfile_natural_boundaries.py` 仍只从 root 按真实 packed Array/Dictionary offset
遍历，并只对表内 index 解码资源 offset/length。完整只读扫描结果为：

```text
physical_candidates=222 unique_decoded_psb=112 parsed_unique_psb=112 failed_unique_psb=0 reachable_nodes_unique=23415372
mdf_files=142 zlib_ok=142 declared_size_matches=142
resource_tag=0x19 count=1240 index_in_tables=1240 resource_in_file=1240 min_size=2 max_size=6492120
resource_tag=0x1A count=1 index_in_tables=0 resource_in_file=0 min_size=none max_size=none
invalid_resource={"files": ["tests/test_files/emote/e-mote3.0バニラパジャマa.psb"], "index": 56395, "index_in_tables": false, "offset": "0x28B2", "path": "$", "raw_hex": "1a4bdc", "resource_in_file": false, "resource_offset": null, "resource_size": null, "sha256": "a890e176f4f23fd9de2233972684e54dbdb1dfceb09a460292ebeed6b122c4b5", "tag": "0x1A"}
```

唯一 raw tag `0x1A` 的两张资源表都只有 73 项，而节点 index 为 56395；它是自然文件中
可达但不安全的 OOB 边界，不是可用的成功 oracle。另在内存中用既有 seed `742877301`
解密该文件的 `[encryptData, chunkOffsets)` 区间，不写回文件；解密后 root 为 `0x21`，
30,590 个可达节点中有 77 个 Resource，全部为表内 tag `0x19`，没有 tag `0x1A`。
完整资产中也没有可达 tag `0x1B/0x1C/0x2D`。

## 固定天然样本

成功路径使用既有 `tests/test_files/emote/ezsave.pimg`：

- 文件 SHA-256：
  `d90d4ee955258b63efdc648f159990aa2c605dceef396ab9ea56eb8d281a7aa3`；
- root member：`2157.tlg`；
- node：offset `0x1FE`，完整字节 `19 00`，Resource index `0`；
- 资源数据：绝对 offset `0xB1C`，长度 `612`；
- 资源 SHA-256：
  `62adc968fb6380e3e7a718ef39e7bf44d5231e9e2f08b4fa390cf38e0fc47005`。

该文件的 offset/length 表各有 8 项，八个 tag `0x19` Resource 都在文件范围内；所选
节点及 612 字节数据均由 host 在启动 Android 前再次核对。

## 本地实现逐项对照

| Android 数据流 | 本地复刻 |
| --- | --- |
| `chunkData == null` 直接返回且不写 size | `cpp/plugins/psbfile/PSBRawFile.cpp:402-405` |
| 先构造 offsets、再构造 lengths | `PSBRawFile.cpp:406-407` |
| index 默认 0；`19/1A/1B/1C` 按 `u8/u16/u24/u32` 解码 | `PSBRawFile.cpp:408-425` |
| 先写 `lengths[index]`，再返回 `chunkData + offsets[index]` | `PSBRawFile.cpp:426-427` |
| dispatch 的 source-level Resource helper 保留同一结构 | `cpp/plugins/psbfile/main.cpp:65-94` |
| category 5 调 helper，再构造复制型 `tTJSVariant` | `main.cpp:645-665` |

逐项对照未发现生产实现差异，因此本轮没有 `cpp/` 修改。

## Oracle 与本地回归

- `run_psbfile_load_adb.py --resource-boundary` 固定上述文件 SHA、节点、数据 offset、长度、
  资源 SHA 与公开表达式 `root["2157.tlg"]`。
- 公开路径要求结果为 `tvtOctet`，先释放 PSBFile global，再验证复制数据；随后显式析构
  `TJS_GLOBAL` 输出 Variant，要求 Octet refcount 从至少 2 下降且仍不少于 1，并保持数据
  地址/内容不变；Full TJS 持久 Variant 栈可能在压缩前多持有一份临时引用。
- 独立 raw load 直调 `GetResource@0x5996E4`，要求返回地址严格等于
  `raw_data + 0xB1C`、size 为 612、内容 SHA 相同；公开 Octet 数据地址必须与 raw 借用
  指针不同。
- trace target 集固定 18 个入口，加入 `GetResource`、Octet allocation、CopyRef、Variant
  release 与 raw owner destructor；所有入口都有参数个数和 return-kind 声明。
- `PSB Resource Variant owns copied bytes after owner release` 原生单测使用同一既有文件，
  在所有 PSBFile/raw-node/root dispatch holder 离开作用域后再次逐字节检查 Octet。

## 验证状态

- 完整扫描：222/112/23,415,372，112/112 解析成功；1,240 个 tag `0x19` 全部表内且
  在文件范围内，唯一 tag `0x1A` 明确为表外；
- MacOS Debug/Release `psbfile-dll` 均增量构建成功；两套完整回归均为
  598 assertions / 11 test cases，新增单测单独经 CTest 运行也通过；
- 四个 Python 文件 `py_compile`、runner `--help`、18-entry trace 元数据检查与
  `git diff --check` 通过；host pin preflight 在任何 guest 调用前完成文件/node/resource
  三重校验；
- `verify_audit.py` 通过：114/114 报告与任务树/manifest 双向一致，verdict 仍为 99/15；
- 后续 Android ARM64 `--resource-boundary` 已在无 trace 与单次全量 trace 中均为 `ok`；
  固定 APK/目标哈希、局部 refcount 口径与事件数见
  [FOLLOWUP_ANDROID_ARM64_RUNTIME_ORACLE_2026-08-03.md](FOLLOWUP_ANDROID_ARM64_RUNTIME_ORACLE_2026-08-03.md)。

该运行时覆盖补强 borrowed/copy/refcount 生命周期证据，但不能唯一恢复 stripped/O3 已
删除的 helper 名、member/free 或 header-inline token。审计总数保持
`ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`。
