# 天然 Boolean / category-1 资产盘点

## 结论

Android `PSBValueDispatch_CreateVariant_guess@0x59673C` 的 category-1 路径只把 tag
`0x02` 转成 true、tag `0x03` 转成 false；`0x27/0x2F/0x33/0x37/0x3B` 先进入同一
category，随后抛出 `psb: can't convert value to bool.`。本轮只读复扫仓库 112 份唯一
decoded PSB 的 23,415,372 个可达节点，以上七种 tag 全部为 0。

因此当前没有合法天然样本可新增 Boolean 成功 oracle，也没有合法天然样本可新增
bool-conversion 异常 oracle。按仓库规则，本轮不改正常 PSB 字节、不生成 fixture、不从
零构造物料，也不修改 `cpp/`。这是一条资产级阴性证据，不是生产实现 GAP；审计统计仍为
`99 ALIGNED / 15 EVIDENCE_LIMITED / 0 HAS_GAP`。

## fresh Android 证据

本轮 fresh `decompile`：

- `PSBValueDispatch_CreateVariant_guess @ 0x59673C`
- `PSBRawNode_GetTypeCategory_guess @ 0x599554`

关键伪代码：

```text
category = GetTypeCategory(node[0])
// category 1 tags: 02,03,27,2F,33,37,3B
if node[0] == 0x02: result = true
else if node[0] == 0x03: result = false
else: throw("psb: can't convert value to bool.")
if throw helper unexpectedly returns: result = false
```

Android `0x5967EC..0x5967F4/0x5968C8..0x5968D4` 直接写 true/false；五种 conversion
tag 在 `0x596960` 进入固定异常 helper。`GetTypeCategory@0x599554` 独立确认七种 tag
全部返回 category 1。

## 本地对照

`cpp/plugins/psbfile/main.cpp:573-591` 先调用共享 classifier，再用 `boolean=false` 保留
默认 continuation；仅 `0x02` 改成 true，`0x03` 保持 false，其余调用同一固定异常。
`cpp/plugins/psbfile/PSBPackedInternal.h:37-53` 的 category-1 tag 集合与 Android 完全
一致。没有确定代码差异，故不改生产实现。

## 扫描器扩展与结果

`tests/differential/python/scan_psbfile_natural_boundaries.py` 现在为每一种可达 category-1
tag 记录 count、首个 offset、文件 SHA 与路径；JSON 字段为
`boolean_summary_unique`。human 输出在集合为空时显式写
`boolean_tags_present=none`，避免把“没有输出”误读成扫描器没有覆盖该类别。

完整复扫保持：

- physical candidates：222
- unique decoded PSB：112
- reachable nodes：23,415,372
- parse failures：0
- MDF：142/142 zlib 成功且声明长度匹配
- category-1 tags：none
- 已知 `m2logo.mtn@0x36F8 == 0x09` anchor：命中

## 后续合法动作

只有在获得现成、来源明确的天然 PSB/MDF 后才重跑扫描器。若出现 `0x02/0x03`，固定
文件 SHA、decoded offset、原始 tag 与 TJS 可达路径后扩展成功 oracle；若出现其余五种
tag，则固定相同证据后扩展异常 oracle。不得从正常节点改 tag 制造样本。
