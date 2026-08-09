# Follow-up：当前 psbfile 源码快照与审计防陈旧门禁

日期：`2026-08-03`。

## 结论

现有 `verify_audit.py` 已能证明 114 个地址、TASK_TREE、MANIFEST、逐函数报告、六维状态、
源码行号引用和七个语义锚点彼此一致，但此前不能证明这些报告仍对应当前
`cpp/plugins/psbfile/` 的完整内容。现新增 `SOURCE_SNAPSHOT.sha256`：门禁同时比较文件集合
和每个文件的 SHA-256，防止源码修改、新增、删除或改名后继续沿用陈旧的
`99 ALIGNED / 15 EVIDENCE_LIMITED / 0 HAS_GAP` 结论。

该门禁不把哈希当作二进制对齐证据。哈希只绑定“已经逐函数审计过的本地快照”；任何有意
源码变更仍必须先满足 Android fresh decompile、伪代码和逐行对照前置条件，再更新相关报告，
最后才允许刷新快照。

## 覆盖集合

快照覆盖 `cpp/plugins/psbfile/` 当前全部 10 个文件：CMake source list、dispatch/media/raw
三组声明与实现、packed internal header、registry 声明与实现及 NCB 主 translation unit。
验证器通过实际目录递归枚举建立集合，因此快照未列出的新增文件与已经消失的旧文件都会
独立失败，不会只检查仍然存在的交集。

## 当前状态复核

- 10 个插件文件的修改时间均早于 114 份最终报告和汇总；未发现审计后源码漂移；
- `psbfile-dll` Mac Debug 目标为最新产物，直接执行得到
  `598 assertions in 11 test cases` 全部通过；
- Web Debug `libpsbfile.a` 与 `index.wasm` 均晚于插件源码，`psbfile` 与 `krkr2` 目标由
  Ninja 判定无待构建工作；
- Android ARM64 oracle 的无 trace 与全量 trace 仍各为 24/24，通过记录见
  [FOLLOWUP_ANDROID_ARM64_RUNTIME_ORACLE_2026-08-03.md](FOLLOWUP_ANDROID_ARM64_RUNTIME_ORACLE_2026-08-03.md)；
- 权威 ELF 的 114 FDE、39 张 LSDA 表和 232 个 call-site 仍由
  `verify_elf_surface.py` 独立通过。

## 新正证据检索边界

本轮还复查了仓库全部分支/历史对象、本地 reference/test 制品、权威 ELF section/build-id/
dynamic-symbol/path-string 面，以及公开的 Kirikiroid2、Kirikiroid2Yuri 和 debloated 仓库树。
没有发现 Android 1.4.4 的 PSBFile 源码、PDB/DWARF/map 或额外未剥离对象。权威 ELF 只有
`.dynsym`、`.eh_frame`、`.gcc_except_table` 等既有表面，没有 `.debug_*` 或
`.gnu_debugdata`；公开插件清单仍把 `PSBFile.dll` 源码位置留空。

因此这次检索不升级 15 个 stripped/O3 源码 token，也不驱动 `cpp/` 改名或
factorization 变化。后续 Git object 级复扫确实又定位到一份历史 Android ARM64 stripped
构建，但完整 FDE/LSDA/指令骨架对照证明它只是同一 psbfile 的第二链接布局，仍没有新增
源码/符号证据；该纠正与门禁见
[FOLLOWUP_HISTORICAL_ANDROID_ARM64_LINEAGE_2026-08-03.md](FOLLOWUP_HISTORICAL_ANDROID_ARM64_LINEAGE_2026-08-03.md)。

## 验证命令

```bash
python3 analysis/psbfile_function_audit_2026-07-25/verify_audit.py
python3 analysis/psbfile_function_audit_2026-07-25/verify_elf_surface.py \
  --llvm-dwarfdump \
  /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/llvm-dwarfdump
out/macos/debug/tests/unit-tests/plugins/psbfile-dll
cmake --build out/web/debug --target psbfile
cmake --build out/web/debug --target krkr2
```

本轮没有修改 `cpp/`，审计统计保持
`ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`。
