# Follow-up：Git 历史第二份 Android ARM64 构建的 psbfile 谱系核验

日期：`2026-08-03`。本轮只比较两份 Android AArch64 `libkrkr2.so`。当前 1.4.4
制品仍是唯一行为与命名权威；历史制品只用于寻找是否存在另一种 retained boundary、EH
形状或代码生成，不能反向覆盖当前目标。

## 制品身份

| 项目 | 当前权威制品 | Git 历史制品 |
| --- | --- | --- |
| 来源 | `reference/libkrkr2/libkrkr2.so` | commit `8f4d8af5d64728630dd04133ab5d14c3c7cc5a64` 的 blob `11979ea5e1d8acce2fb17690edebd0f3f4292a5e` |
| 大小 | `27,929,688` | `27,917,400` |
| SHA-256 | `ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38` | `05e2ff4c77f1561608ad7703153d2fb09855bf223237a85dc2267fff1388564f` |
| Build-ID | `985d9f685e07ce4497472523c3e84b1f38989235` | `de22234bffa0545d276b705487ca0c3d35101386` |
| 符号状态 | stripped，无 debug section | stripped，无 debug section |

commit `9a305d744fbb2450eff227fe397ef38a50b7ec18` 把该历史 blob 替换成当前 blob。
历史 commit 的完整路径树中没有任何含 `psb` 的源码、object、map 或调试伴随文件；因此
这条线索只提供第二个链接布局，不能提供原始 identifier/header token。

## 机械同源性结果

新增
[`verify_historical_arm64_lineage.py`](verify_historical_arm64_lineage.py)
直接用 `git cat-file blob` 读取历史对象，并只在自动删除的临时文件中交给
`llvm-dwarfdump/objdump`。验证结果：

- MANIFEST 的 114 个 FDE 全部在历史构建中以固定 `+0x3E0` 映射；每个 exclusive end
  同样平移，LSDA 有无逐项一致；
- 39 个 LSDA 入口的 232 个 call-site tuple 与 type-table encoding 全部相同；
- 两个 static-init surface 的 42 条指令与主 surface 的 5,483 条指令，共 5,525 条：
  `3,958` 条文本完全相同，`1,431` 条只改变 PC-relative 地址，剩余 `136` 条只改变
  `add/ldr` 地址立即数；后 136 条全部能在前 1–7 条指令内回溯到同一 base register 的
  `ADRP`，操作码/寄存器骨架差异为 0；
- 历史构建只多出 8 个与 `std::bind<void(*)(int,int,int)>` 相关的动态符号；两份构建均无
  PSBFile 插件语义动态符号、`.symtab`、`.debug_*` 或 `.gnu_debugdata`。

因此历史构建中的 psbfile 不是一份能揭示新 helper 边界的独立代码生成：它是相同
psbfile 机器骨架在另一链接布局中的副本。地址变化不能升级 15 个 stripped/O3 源码 token。

## Fresh Android ARM64 复核

为避免仅凭跨构建字节对齐下结论，本轮又 fresh decompile 并复查 xref：

### `PSB_FindNameIndex_guess@0x59641C`

```text
count/base/check = decode two consecutive packed tables
state = base[0] + first_byte(name)
if state >= count: return false
loop: if check[state] != parent: return false
      if current byte == 0: out = base[state]; return true
      parent = state; state = base[state] + next_byte(name)
      if state >= count: return false
```

三个 direct caller 仍为 `PropGet@0x597854`、strict lookup `0x598C58` 与 try lookup
`0x598D58`。本地 `PSBRawFile.cpp:52-80` 精确保持该双表 trie 数据流；
`PSBPackedInternal.h:226-259` 的 record/type/token 仍是 O3 无法唯一恢复的部分。

### `PSBValueDispatch_ctor_guess@0x597AD4`

```text
self.vptrs = primary + secondary
self.refCount = 1
self.owner = file.owner
if self.owner != null: ++self.owner.refCount
self.node = node
self.valid = true
```

三处 new-expression caller 仍为 `0x6A931C/0x6AA124/0x6AA424`，全部忽略 X0；本地
`main.cpp:20-26` 的 holder+node 构造与生命周期一致。历史构建没有增加 ctor 符号或调试
类型，故 `const/reference`、共同 holder 名及 member/base token 继续受限。

### `PSBMedia_GetLocallyAccessibleName_guess@0x599DD8`

```text
if out.storage != null:
    Release(out.storage)
    out.storage = null
return
```

唯一引用仍是 vtable data xref；本地 `PSBMedia.cpp:221-224` 使用 `name.Clear()`。
历史构建生成完全相同的清空序列，仍不能在 `Clear()`、默认空临时赋值等可生成同一 O3
代码的 token 间作唯一选择。

## 审计影响

- 新增 Android ARM64 正证据路径，但没有发现生产实现差异；
- 没有修改 `cpp/`，不刷新 `SOURCE_SNAPSHOT.sha256`；
- 114 个结论保持
  `ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`；
- 该历史 blob 今后不得被重复包装成“独立编译器/独立源码边界”来消除 `_guess`。

## 验证命令

```bash
python3 analysis/psbfile_function_audit_2026-07-25/verify_historical_arm64_lineage.py \
  --llvm-dwarfdump \
  /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/llvm-dwarfdump \
  --objdump \
  /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/objdump
python3 analysis/psbfile_function_audit_2026-07-25/verify_audit.py
```
