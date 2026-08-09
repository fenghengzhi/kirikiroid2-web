# Follow-up：自然 tag `0x0B` 全资产可达节点盘点

日期：`2026-08-02`。本轮只补运行时边界物料证据，不修改 `cpp/`，不生成、破坏、解密或
改写任何 PSB/MDF。扫描器为
`tests/differential/python/scan_psbfile_natural_boundaries.py`。

## Android 权威边界

本轮 fresh IDA MCP `decompile(addr="0x599438")` 再次确认
`PSBRawNode_GetInt_guess` 的 tag `0x0B` 分支只读取 `node+1` 的低 32 位并以有符号
32 位 ABI 返回。fresh `decompile(addr="0x59673C")` 则确认
`PSBValueDispatch_CreateVariant_guess` 对同一 tag 构造完整 56 位正整数：

```text
node = self->node
if node.tag == 0x0B in GetInt:
    return signed32(u32(node + 1))
if node.tag == 0x0B in CreateVariant:
    value = u32(node + 1) | (u64(u16(node + 5)) << 32) | (u64(node[7]) << 48)
    assign Integer Variant(value)
```

因此只有天然 tag `0x0B` 节点的高 24 位非零时，才能直接观察 public Variant 与 raw
`GetInt` 的这条特化差异。普通字节搜索不能证明某个 `0x0B` 位于可达 PSB node，必须按
真实 Array/Dictionary packed offsets 从 header entries 遍历。

## 只读扫描方法

扫描器按 Android 已审计的数据契约执行：

- `PSB\0` 直接读取；`mdf\0` 只在内存中 zlib 解压并核对声明长度；
- 以 header `+36` 的 entries offset 为根；
- Array `0x20` 按单张 packed offsets 表遍历；
- Dictionary `0x21` 按 keys 表计数、第二张 offsets 表定位 child；
- packed count、width `1..5`、W32 product 与 width-5 shift-modulo 均保持 Android 规则；
- 只统计从 root 可达且成功按 tag classifier 解释的 node；相同解压后 PSB 以 SHA-256
  去重；只为实际发现的 tag `0x0B` 与各整数 tag 的选中极值反向解码名字并输出路径。

为避免扫描器自身产生“成功但走错树”的假阴性，默认同时检查已知自然锚点
`reference/xp3/logo_test/m2logo.mtn@0x36F8 == 0x09`。

## 可复现结果

```bash
python3 tests/differential/python/scan_psbfile_natural_boundaries.py
```

```text
physical_candidates=222 unique_decoded_psb=112 parsed_unique_psb=112 failed_unique_psb=0 reachable_nodes_unique=23415372
mdf_files=142 zlib_ok=142 declared_size_matches=142
tag0b_unique_nodes=0 tag0b_high32_nonzero=0
integer_tag=0x04 count=3610586 min=0 max=0 max_variant_get_int_delta=0
integer_tag=0x05 count=2213209 min=-122 max=127 max_variant_get_int_delta=0
integer_tag=0x06 count=1621988 min=-2500 max=27747 max_variant_get_int_delta=0
integer_tag=0x07 count=11874 min=33793 max=4195948 max_variant_get_int_delta=0
integer_tag=0x08 count=126 min=33556076 max=37750380 max_variant_get_int_delta=0
integer_tag=0x09 count=31186 min=4278190080 max=4294967295 max_variant_get_int_delta=4294967296
resource_tag=0x19 count=1240 index_in_tables=1240 resource_in_file=1240 min_size=2 max_size=6492120
resource_tag=0x1A count=1 index_in_tables=0 resource_in_file=0 min_size=none max_size=none
anchor={"actual_tag": "0x09", "expected_tag": "0x09", "file": "reference/xp3/logo_test/m2logo.mtn", "match": true, "offset": "0x36F8"}
invalid_resource={"files": ["tests/test_files/emote/e-mote3.0バニラパジャマa.psb"], "index": 56395, "index_in_tables": false, "offset": "0x28B2", "path": "$", "raw_hex": "1a4bdc", "resource_in_file": false, "resource_offset": null, "resource_size": null, "sha256": "a890e176f4f23fd9de2233972684e54dbdb1dfceb09a460292ebeed6b122c4b5", "tag": "0x1A"}
```

结论严格限定为当前 `reference/` 与 `tests/test_files/`：222 个物理 PSB/MDF 输入解压后
形成 112 份唯一 PSB，23,415,372 个唯一内容内的可达节点全部解析成功；已知 tag `0x09`
锚点命中，但没有一个可达 tag `0x0B` 节点，更不存在高 32 位非零的自然边界值。

这不是“PSB 格式不会出现 tag `0x0B`”的结论，也不削弱 Android 静态边界证据；它只证明
当前仓库资产不能关闭该运行时覆盖项。后续获得新天然资产后可直接重跑同一只读扫描器；
若出现结果，再记录文件哈希、node offset、完整路径与原始 8 字节，并扩展 Android oracle。
