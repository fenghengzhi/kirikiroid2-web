# Follow-up：raw Dictionary lookup 与 `PSBRawNode` holder 生命周期 oracle

日期：`2026-08-03`。本轮继续只使用 Android arm64 `libkrkr2.so` 与仓库已有、未修改的
`reference/xp3/logo_test/m2logo.mtn`。在 raw `PSBFile` hidden-sret 通道之上，新增天然
root Dictionary 的 strict/non-strict lookup、miss-preserve、destination overwrite、
`self == outNode` alias、IsValid 与 Contains 临时对象生命周期 oracle。没有修改 `cpp/`
生产实现，没有生成或改写 PSB/MDF fixture。

fresh Android 证据与当前生产实现一致，未发现新的确定 GAP；审计统计仍为
`99 ALIGNED / 15 EVIDENCE_LIMITED / 0 HAS_GAP`。

## fresh Android arm64 证据

本轮重新反编译并核对完整指令：

- `PSBRawNode_GetDictionaryValueStrict_guess @ 0x598C58`
- `PSBRawNode_GetDictionaryValue_guess @ 0x598D58`
- `PSBRawNode_IsValid_guess @ 0x598E44`
- `PSBRawNode_ContainsDictionaryKey_guess @ 0x5995D8`
- `PSB_FindNameIndex_guess @ 0x59641C`
- `PSB_FindDictionaryValueOffset_guess @ 0x59659C`

不超过 10 行的关键伪代码：

```text
lookup(self,key,out):
  if !FindNameIndex(names,key,&i) || !FindDictionaryOffset(node+1,i,&off): return false
  savedNode=self.node; Release(out.owner)
  out.owner=self.owner; AddRef(out.owner); out.node=savedNode+1+off; return true
strict(self,key,X8): same two lookups; miss throws fixed diagnostic; hit writes retained {owner,child}
isValid(self): return self.owner!=null && self.node!=null
contains(self,key): tmp={null,null}; if category(self.node)==7: result=lookup(self,key,&tmp)
contains tail/unwind: Release(tmp.owner); return result
```

关键边界：

- non-strict 两级 lookup 任一失败都在写出参数之前返回 false；旧 out holder、node 和
  owner refcount 必须逐字节/逐值不变。
- 命中先保存 source node，再 release destination 旧 owner，随后才重读 source owner、
  retain，并最后写 child。没有 self guard；`self == outNode` 必须保留这条
  capture → release → reload/retain → write 的危险顺序。
- strict 成功通过 hidden `X8` 返回 16-byte raw node 并 retain 一次。strict miss 会构造
  narrow-key `ttstr` 并走固定异常；本轮不让异常跨 RPC 边界，也不把 helper-return 的
  `{0,0}` 死边界冒充自然成功样本。
- IsValid 只检查两个指针，不读取 node tag、owner refcount 或 buffer。
- Contains 先零构造 16-byte 临时 raw node；Dictionary 命中时内部 lookup 的一次 retain
  在返回前由临时析构抵消，miss/非 Dictionary 也走同一 cleanup 拓扑。

## 本地生产实现对照

- `cpp/plugins/psbfile/PSBRawFile.cpp` 的 `GetDictionaryValue` 保留两级 packed helper
  短路；命中先计算 child，再执行 `value.file_ = file_`，由共享 `PSBFile::operator=` 完成
  release-old → copy/reload source → retain，最后写 `value.node_`。没有 alias guard。
- `GetDictionaryValueStrict` 使用相同两级 helper，miss 调固定异常，成功构造
  `PSBRawNode(file_, child)`，即通过共享 holder 生命周期 retain 一次。
- `ContainsDictionaryKey` 在 classifier gate 前默认构造 raw-node 临时，只对 category 7
  委托 non-strict getter；RAII 在正常/异常路径释放临时引用。
- `IsValid_guess` 精确为 `GetOwner()!=nullptr && GetNode()!=nullptr`。
- packed names trie 与 Dictionary offset 继续使用 `PSBPackedInternal`/raw byte view；没有
  替换成 `std::map`、TJS Dictionary 或预先解码的 host 容器。

因此本轮不改生产代码；oracle 只把已审计对齐但此前缺少设备级对象生命周期观察面的路径
固定下来。

## 天然 pin 与断言

输入 SHA-256：
`4382de8283cc0782fd269b16d3157bf3a9ec28916440f9192690eb178c0c18fe`。

只读 host parser 固定：

| 节点/键 | decoded offset | tag / 角色 |
| --- | ---: | --- |
| root | `0xB4B` | `0x21` Dictionary |
| `object` | `0xB72` | `0x21` Dictionary；成功 child |
| `version` | `0x5399` | `0x1E` Real；覆盖 destination 后的非 Dictionary |
| `icon42` | — | 存在于全局 names trie，但不在 root 或 `object`；第二 helper miss |
| `__psbfile_raw_missing__` | — | 不在全局 names trie；第一 helper miss |

设备 adapter 的引用链：

1. raw load 得 owner ref=1；`GetRoot@0x598A3C` 得 retained root，ref `1 → 2`。
2. `IsValid@0x598E44` 验证 root=true、默认空/owner-only/node-only 均为 false，ref 保持 2。
3. strict lookup `object` 经 `X8` 得 `{owner,raw+0xB72}`，ref `2 → 3`。
4. 默认构造的普通 out lookup `object` 成功，ref `3 → 4`。这里 out 必须从 `{0,0}`
   开始，因为 Android 命中会先 Release 旧 owner；任意指针 sentinel 都会错误触发释放。
5. 用自造 key 和全局已有 `icon42` 分别触发第一/第二 helper miss；两次都必须返回 false、
   完整保留 `{owner,node}`，ref 保持 4。
6. 同一 out 改查 `version`：先 release 旧 `object` holder，再 retain 同一 source owner，
   node 改为 `raw+0x5399`，ref 净值仍为 4。
7. Contains 对 root 的 hit/两种 miss 以及 Real child 的非 Dictionary 分支分别返回
   true/false/false/false；内部临时 retain/release 后 ref 仍为 4。
8. 以 root raw node 同时作为 self/out 查 `object`，要求 node 原地变为 `0xB72`，ref
   仍为 4；随后在 alias 对象上做第二-helper miss，必须保持完整输出和 ref。
9. 顺序释放 strict、普通、alias-root holder，ref 必须 `4 → 3 → 2 → 1`；最后释放原始
   `PSBFile` holder 触发 terminal owner 析构。Android `0x695CBC` 不清空任一 holder 槽。

## trace 与当前验证

shape trace 从 53 扩为 59 个唯一地址，新增上述六个入口：strict hidden-sret 的普通参数
计数为 2/return kind `void`；non-strict 为 3/int；IsValid 为 1/int；Contains 为 2/int；
两只 packed helper 均为 3/int。`ADDR_NAMES/ARG_COUNTS/RETURN_KINDS` 三张映射完整。

- 三个相关 Python 文件 `py_compile`：通过。
- 天然 pin：`icon42` 全局存在且 root/object 均不存在；自造 miss key 全局不存在；通过。
- adapter 控制流 smoke：使用未修改的真实文件字节与内存模型跑通
  `1 → 2 → 3 → 4`、两级 miss preserve、overwrite/alias 净 4、释放
  `3 → 2 → 1 → terminal`。该 smoke 只验证 host 断言与 cleanup 编排，不替代 Android。
- shape trace：59/59 地址唯一，三张元数据映射无缺项。
- hidden-sret harness 的 arm64 `X8` 静态验证沿用
  [FOLLOWUP_RAW_HOLDER_LIFECYCLE_ORACLE_2026-08-03.md](FOLLOWUP_RAW_HOLDER_LIFECYCLE_ORACLE_2026-08-03.md)。
- legacy Android NDK 与设备当前均缺席；既有 arm64 prebuilt 仍不含 `CALL_SRET`。设备执行
  前需先重建/重打包，随后运行真实 `--shape-boundary --trace`。这保留运行时验证缺口，
  不改变反编译证据、生产实现判定或审计统计。

后续 direct `GetDictionaryKeys@0x598E64` oracle 把 hidden-sret carrier 扩到 32 字节，
观察 24-byte gnustl `vector<string>`、COW reps 与目标内析构，shape trace 扩为 63/63；见
[FOLLOWUP_RAW_DICTIONARY_KEYS_VECTOR_ORACLE_2026-08-03.md](FOLLOWUP_RAW_DICTIONARY_KEYS_VECTOR_ORACLE_2026-08-03.md)。
