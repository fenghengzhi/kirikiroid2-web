# XP3 参考

文中的 `<python>` 表示当前机器可用的 Python 3 解释器；执行前替换为 `python` 或 `python3`。

## 构建

`tools/xp3` 只会在原生平台构建中启用。仓库根 `CMakeLists.txt` 里，`BUILD_TOOLS` 仅在非 Web / Android / iOS 构建时生效，因此不要用 Web preset 构建它。

当前仓库提供的 native preset：

### macOS Release

```bash
cmake --preset "MacOS Release Config"
cmake --build out/macos/release --target xp3
```

当前 `CMakePresets.json` 没有 Linux/Windows native preset。脚本在这些主机上只能报告缺口和预期二进制位置，不能给出不存在的 preset 命令。需要 Linux/Windows 原生构建时，先添加或选择经过验证的 native CMake 配置，再构建 `xp3` target；不要使用 Web / Android / iOS preset。

macOS 调试版可使用现存的 `MacOS Debug Config`。`scripts/resolve_xp3_binary.py --build-type debug` 会先核对目标 preset 是否真实存在。

## 二进制路径

`tools/CMakeLists.txt` 会把工具输出到 `tools/bin/<platform>/<mode>/`：

- macOS Release: `tools/bin/mac/rel/xp3`
- macOS Debug: `tools/bin/mac/dbg/xp3`
- Linux Release: `tools/bin/linux/rel/xp3`
- Linux Debug: `tools/bin/linux/dbg/xp3`
- Windows Release: `tools/bin/win/rel/xp3.exe`
- Windows Debug: `tools/bin/win/dbg/xp3.exe`

## 命令行为

当前实现支持的核心参数：

```text
xp3 [--help] [--version] [--output VAR] [--xp3filter VAR] files...
```

- `files`: 一个或多个待解包的 `.xp3`
- `-o`, `--output`: 父输出目录
- `--xp3filter`: 指定 `xp3filter.tjs` 路径

注意：

- 最终输出目录不是单纯的 `<output>/`，而是 `<output>/<archive-stem>/`
- 如果未显式传 `--xp3filter`，程序会尝试读取待解包文件同目录下的 `xp3filter.tjs`
- 无效输入文件会被跳过，不会让整批任务立刻失败
- 路径会先做规范化处理；在非 Windows 平台支持 `~`，在 Windows 还会展开 `%VAR%`

## 示例

### 解包单个 XP3

```bash
XP3_BIN="$(<python> .claude/skills/krkr2-xp3-tool/scripts/resolve_xp3_binary.py --build-type release)"
"$XP3_BIN" -o ./out ./game/data.xp3
```

上面的命令会把文件解到 `./out/data/` 下，而不是直接解到 `./out/`。

### 一次解包多个 XP3

```bash
XP3_BIN="$(<python> .claude/skills/krkr2-xp3-tool/scripts/resolve_xp3_binary.py --build-type release)"
"$XP3_BIN" -o ./out ./game/data.xp3 ./game/patch.xp3
```

输出会分别落到：

- `./out/data/`
- `./out/patch/`

### 显式指定 xp3filter

```bash
XP3_BIN="$(<python> .claude/skills/krkr2-xp3-tool/scripts/resolve_xp3_binary.py --build-type release)"
"$XP3_BIN" -o ./out --xp3filter ./filters/xp3filter.tjs ./game/data.xp3
```

### 使用同目录 xp3filter

如果 `./game/data.xp3` 同目录下存在 `./game/xp3filter.tjs`，直接运行：

```bash
XP3_BIN="$(<python> .claude/skills/krkr2-xp3-tool/scripts/resolve_xp3_binary.py --build-type release)"
"$XP3_BIN" -o ./out ./game/data.xp3
```

程序会自动加载这个过滤脚本。
