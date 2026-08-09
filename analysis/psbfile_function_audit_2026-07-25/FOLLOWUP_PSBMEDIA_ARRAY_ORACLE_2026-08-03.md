# Follow-up：PSBMedia 天然 Array listing oracle

日期：`2026-08-03`。本轮为 `PSBMedia::GetListAt@0x5999F4` 的 Array 分支补一条
direct Android oracle。输入只使用仓库已有且未改写的
`tests/test_files/emote/ezsave.pimg`；没有生成、修补或重新编码 PSB/MDF fixture，也没有
修改生产 `cpp/`。

fresh Android 反编译与本地生产实现继续一致，没有发现新的确定 GAP；114 项审计仍保持
`99 ALIGNED / 15 EVIDENCE_LIMITED / 0 HAS_GAP`。本 follow-up 增加的是可运行观察面，
不改变 stripped/O3 无法唯一恢复的源码 token 判定。

## 天然输入边界

只读 packed walker 从 PSB header 的 root offset 开始遍历全部 379 个可达节点，并同时
记录 Dictionary 键路径与 Array 下标路径。`ezsave.pimg` 中恰有一只可达 Array：

| 项目 | 固定值 |
| --- | --- |
| 输入 SHA-256 | `d90d4ee955258b63efdc648f159990aa2c605dceef396ab9ea56eb8d281a7aa3` |
| raw / decoded size | `64585 / 64585` |
| root offset | `0x1d0` |
| 路径 | `$/layers`；全程只经过 Dictionary 键 |
| Array node | offset `0x20b`，tag `0x20` |
| packed count | header `0x0d`，count `32` |
| packed value width | tag `0x0e`，width `2` |
| packed table size | `67` bytes |
| 32-byte prefix | `200d200e000036006c009f00d4000a0140017501ab01e60122025e029a02d602` |

`PSBMedia::Resolve` 只按斜杠分隔的 Dictionary key 前进，因此“最终节点为 Array、此前
没有 Array index”的 `layers` 正好可由 storage 名
`psb-media-array.pimg/layers` 直接触达。

## fresh Android 证据

本轮重新调用 `decompile(0x5999F4)`，取得
`PSBMedia_GetListAt_guess@0x5999F4` 的完整伪代码。Array 分支位于
`0x599ACC..0x599BBC`；关键行为不超过 10 行：

```text
GetListAt(name,lister): if !EnsureContainer(name) return
  value={owner:null,node:null}; if !Resolve(name,value) goto cleanup
  if rawTag != Array(0x20): handle other categories / cleanup
  countTag 0x0d => u8; 0x0e => u16; 0x0f => u24; 0x10 => u32-as-signed
  invalid countTag => cleanup
  if count < 1: cleanup
  for signed index=0; index<count; ++index:
    temp=ttstr(index); lister->Add(temp); destroy temp
  cleanup: Release value.owner once when non-null
```

具体机器码边界：

- `0x599A2C` 先调用 `EnsureContainer@0x599E04`，失败直接返回；成功后才在
  `0x599A34` 零构造 raw node。
- `0x599A44` 调用 `Resolve@0x59A4B0`；false 进入统一 owner cleanup。
- raw tag `0x20` 进入 Array 分支。count tag `0x0D..0x10` 分别读取
  `u8/u16/u24/u32`；`u32` 结果留在 signed W32 循环上界。
- `0x599B84` 把 index 初始化为 0；`0x599B98` 构造十进制 `ttstr(index)`；
  `0x599BA4` 经 lister vslot 0 回调；`0x599BB0` 释放逐项临时字符串；
  `0x599BBC` 使用 signed `index < count`。
- count 为 0 或 signed negative 均不回调；无 lister null guard。
- `0x599A74..0x599A98` 对非空 owner 恰好一次 intrusive Release；归零时释放 raw data
  和 owner。

## 本地生产实现对照

`cpp/plugins/psbfile/PSBMedia.cpp:149-219` 已逐行复刻上述结构：

1. `149-157` 保留顺序 `EnsureContainer`、默认 `PSBRawNode`、`Resolve` 两级门控。
2. `162-163` 只在 shared classifier 返回 category 6 时进入 Array 分支。
3. `164-187` 从 `node+1` 读取 packed header，按四种 tag 生成 signed `tjs_int count`，
   invalid tag 直接 return。
4. `188-190` 以 signed `tjs_int index` 从 0 枚举，并逐项调用
   `lister->Add(ttstr(index))`。
5. `PSBRawNode` 与逐项 `ttstr` 的 RAII 覆盖普通 return 和异常 unwind；没有额外持有
   `self` 或 lister。

六维对照没有新增偏差，因此本轮不修改生产实现。

## direct oracle 设计

新增 runner 入口 `--media-array`：

1. runner 先固定输入 SHA-256；adapter 再固定 node offset、tag、32-byte prefix、packed
   count 及 encoded table size，任何自然输入漂移都在设备调用前失败。
2. 现有输入以 ASCII alias `psb-media-array.pimg` 放入 app 可读目录，并只添加该目录为
   storage auto path。
3. adapter 调原生 `PSBMedia::GetListAt@0x5999F4`；harness 只提供 ABI-compatible
   `iTVPStorageLister`，不实现 PSB traversal、count decode 或 index 格式化。
4. 回调结果必须严格等于 `"0".."31"`，顺序、个数和文本全部固定。
5. 调用后同时要求 media `_file` 为非空 Object、Object/ObjThis 相同、缓存 container
   精确等于 alias，避免“伪 lister 返回正确但 native container 没加载”的弱断言。
6. adapter 逆序释放两个 host 创建的 `ttstr` payload；原生逐项临时字符串生命周期由
   `GetListAt` 自身执行。

该入口复用现有 `STORAGE_LIST` 协议与 `PSBFILE_MEDIA_TARGETS`；`0x5999F4`、
`0x599E04`、`0x598538`、`0x598708`、`0x59A330`、`0x59A4B0` 已在 23-entry media
trace catalog 中，无需扩大通用字符串 helper 的 trace 噪声面。

## 当前验证

- adapter、runner、天然边界 scanner 的 `py_compile`：通过。
- runner `--help` 已公开 `--media-array`。
- 真实 `ezsave.pimg` 静态 pin：node `0x20b`、count `32`、packed table `67` bytes、
  预期范围 `0..31` 全部通过。
- 纯主机 fake control-flow：`status=ok`，完整接收 32 个有序字符串，并验证两个 host
  `ttstr` 以 LIFO 顺序释放。该检查只验证 adapter 控制流，不冒充 Android runtime。
- 本轮没有修改 harness protocol、native harness 或生产 `cpp/`。

后续真实 Android ARM64 target 已执行以下模式，并在无 trace 与单次全量 trace 中均为
`ok`：

```bash
python3 tests/differential/python/run_psbfile_load_adb.py \
  --media-array \
  --startup-xp3 reference/xp3/caution_minimal/caution_minimal.xp3 \
  --trace
```

固定 APK/目标哈希和事件数见
[FOLLOWUP_ANDROID_ARM64_RUNTIME_ORACLE_2026-08-03.md](FOLLOWUP_ANDROID_ARM64_RUNTIME_ORACLE_2026-08-03.md)。
该运行已补齐 `PSBMedia::GetListAt` Array 分支的 target 回调序列，不改变现有审计统计
或 verdict。
