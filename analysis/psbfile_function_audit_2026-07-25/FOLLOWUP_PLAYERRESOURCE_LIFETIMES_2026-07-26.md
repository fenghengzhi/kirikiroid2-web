# Follow-up：PlayerResource raw-node 与 key-vector 生命周期闭环

日期：`2026-07-26`。本文件记录 psbfile 审计沿真实 PlayerResource consumer 继续追踪后，
闭合的两项确定对象生命周期差异：`Motion_Player_findSource@0x6948E8` 的 Win atlas pixel
strict raw-node 临时量，以及 `sub_695DE8@0x695DE8` 的 KrKr atlas outer group-key vector。
两者都在 psbfile 114-address MANIFEST 之外，仅作为跨模块 consumer follow-up；不增加逐函数
报告，也不改变 `114 = 99 ALIGNED + 15 EVIDENCE_LIMITED` 的统计。

## Fresh Android 正证据

### Win pixel strict raw-node temporary：`0x6948E8`

`Motion_Player_findSource@0x6948E8` 先把 `texture` strict lookup 的 raw node 保存在长生命周期
局部量中，再以该 node 执行 `pixel` strict lookup 和 `GetResource`。`0x694E18..0x694E3C`
在读取 `sourceSize` 或消费 borrowed pixel pointer **之前**，先析构 `pixel` strict lookup
产生的 raw-node 临时量；外层 `textureNode` 仍持有相同 PSB owner，因此 chunk storage 在后续
格式转换和 texture upload 期间保持存活。源码结构不能用一个具名 `pixelNode` 把该临时量的
生命周期错误延长到函数作用域。

### Outer group-key vector：`0x695DE8`

`sub_695DE8@0x695DE8` 在 `0x69616C` 构造 outer group-key vector。它不是仅服务于 outer
enumeration loop 的 range temporary：枚举完成后，该 vector 仍跨越 record decoding、packing
和 atlas upload 存活，直到 `0x696C80..0x697340` cleanup 区间才逐个释放字符串并销毁容器，
随后才销毁 record vector。相反，每个 group 的 inner icon-key vector 仍是当轮 group 范围内
的临时容器，在该 group 的 inner enumeration 结束时析构；Android 没有把所有 inner keys
提升为跨 packing/upload 存活的共享容器。

## Android 关键伪代码（10 行）

```text
texture = strict(groupNode, "texture")
pixels = strict(texture, "pixel").GetResource(size)
destroy(pixelStrictTemporary)
consume_and_upload(pixels, size)                 // texture keeps owner alive
records = Vector<Record>(); groups = source.GetDictionaryKeys()
for group in groups:
    icons = strict(strict(source, group), "icon").GetDictionaryKeys()
    for icon in icons: records.emplace_back(...)
pack(records); upload_atlases(records)            // groups remains alive
destroy(groups); destroy(records)                 // each icons died per group
```

## 本地当前逐行对照

| Android 行为 | 当前本地复刻 |
| --- | --- |
| 长生命周期 `texture` raw node 持有 owner。 | `cpp/plugins/motionplayer/PlayerResource.cpp:63-64` 把 `texture` strict lookup 结果保存为 `textureNode`，覆盖后续像素消费与上传。 |
| `pixel` strict raw node 仅活到 `GetResource` full-expression 末尾。 | `PlayerResource.cpp:80-86` 直接链式执行 `textureNode.GetDictionaryValueStrict("pixel").GetResource(sourceSize)`；没有具名 `pixelNode` 延长临时量生命周期，注释明确对应 `0x694E18..0x694E3C` cleanup。 |
| outer group keys 在 `records` 之后构造，并跨 decoding、packing、upload 存活。 | `PlayerResource.cpp:394-401` 先构造 `records`，再把 `sourceRoot.GetDictionaryKeys()` 保存为具名 `groupKeys`；其函数作用域延伸穿过 `:422-510` 的 decode、pack 与 atlas upload。 |
| inner icon keys 每个 group 单独构造并在该 group 结束时销毁。 | `PlayerResource.cpp:402-417` 保持 `for(const auto &iconName : iconRoot.GetDictionaryKeys())` 的 range temporary；它没有被提升到 outer loop 或 packing/upload 作用域。 |
| cleanup 次序为 outer keys 在 record vector 之前销毁。 | `groupKeys` 在 `records` 之后声明（`:394,399`），C++ 逆声明顺序析构使 `groupKeys` 先于 `records` 销毁，对齐 `0x696C80..0x697340`。 |

## 六维影响

| 维度 | 闭环影响 |
| --- | --- |
| 源代码结构 | Win 路径恢复“长生命周期 texture node + full-expression pixel temporary”；KrKr 路径恢复具名 outer key vector，同时保留每组 inner range temporary。 |
| 数据流 | borrowed pixel pointer/size 从短命 pixel node 流向转换与上传，但 owner 保活来自 texture node；group strings 继续供 record key 构造，随后不参与 pack 算法。 |
| 调用链 | 两条路径仍分别由 `Motion_Player_findSource@0x6948E8` 和共享 KrKr atlas builder `sub_695DE8@0x695DE8` 驱动；没有插入替代 lookup、拷贝 pixel buffer 或二次枚举。 |
| 对象生命周期 | pixel strict temporary 在消费前释放而 owner 继续存活；outer key vector 跨 packing/upload，inner vector 每 group 释放，最终 outer keys 先于 records 清理。 |
| 内部容器实现 | 保持 Android 的两个独立 `std::vector<std::string>` 生命周期层级：一个跨后半函数的 outer vector，以及逐组构造/销毁的 inner vector。 |
| 边界行为 | borrowed resource 不因临时 raw node 销毁而悬空；空 group/icon 集合仍沿原 pack/upload 与容器 cleanup 顺序执行，没有新增早退或安全拷贝。 |

## 已运行验证

当前源码已通过以下原生非回归验证：

- `motionplayer-ttstr-hash-test`：`109 assertions in 24 test cases`；
- `motionplayer-dll`：`1386 assertions in 21 test cases`；
- `psbfile-dll`：`577 assertions in 10 test cases`。

这些测试守护当前 PlayerResource、motionplayer 与 psbfile 集成路径不回归；对象析构位置和
容器生命周期的源码复原结论仍以本轮 fresh Android 反编译/反汇编证据为权威。

最终当前源码的 Web Debug 与 Wasmtime guest 也已重编、链接通过；m2logo/yuzulogo
完整捕获 25/63 帧，trace hash 与汇总记录一致，structural comparator 复现既有
31/21 个 opacity ±1 mismatch。该运行时结果只证明没有新增可见回归，不替代上述临时量/
容器生命周期的 fresh IDA 证据；详见 [SUMMARY.md](SUMMARY.md)。
