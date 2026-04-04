---
name: krkr2-xp3-tool
description: 使用仓库内置的 `tools/xp3` 工具解包 XP3 归档，包括原生构建步骤、输出目录结构和 `xp3filter.tjs` 处理。当用户需要解包 XP3 文件、运行 `xp3` 命令行工具、查看 `tools/xp3` 或处理 `xp3filter.tjs` 时使用。
---

# KrKr2 XP3 工具

## 适用场景

- 解包一个或多个 `.xp3` 文件
- 查找或运行项目内 `xp3` 命令行工具
- 处理 `xp3filter.tjs`
- 说明 `tools/xp3` 的输出目录与参数行为

## 快速流程

1. 先定位可执行文件：

```bash
python3 .claude/skills/krkr2-xp3-tool/scripts/resolve_xp3_binary.py --build-type release
```

2. 如果脚本提示未构建，按它给出的本机 CMake 命令先构建原生版本。不要使用 Web preset，Web preset 会关闭 `BUILD_TOOLS`。
3. 解包单个归档：

```bash
XP3_BIN="$(python3 .claude/skills/krkr2-xp3-tool/scripts/resolve_xp3_binary.py --build-type release)"
"$XP3_BIN" -o /path/to/out /path/to/data.xp3
```

4. 需要过滤脚本时：

```bash
"$XP3_BIN" -o /path/to/out --xp3filter /path/to/xp3filter.tjs /path/to/data.xp3
```

## 关键约定

- `-o` / `--output` 指定的是父输出目录；工具会再追加一层“归档名去掉扩展名后的目录”。
- 如果没有显式传 `--xp3filter`，工具会尝试读取待解包 `.xp3` 同目录下的 `xp3filter.tjs`。
- 可一次传多个归档，结果会分别落到 `<output>/<archive-stem>/`。
- `tools/xp3/README.md` 当前没有写出 `--xp3filter`，以 `tools/xp3/main.cpp` 的实现为准。

## 额外资料

- 构建、路径和示例命令见 [reference.md](reference.md)
- 二进制定位脚本：`scripts/resolve_xp3_binary.py`
- 实现入口：`tools/xp3/main.cpp`
