---
name: ksdec
description: 使用 ksdec 工具解密 KiriKiri2 加密的剧情脚本文件（.ks/.tjs）。当需要读取、检查或分析 FEFE 加密（模式 0/1/2）的 .ks 或 .tjs 文件，或处理 KiriKiri2/kirikiroid2 游戏的 KAG 剧情脚本时使用。也在用户要求解码、解密或转储剧情文件，或遇到非纯文本的二进制 .ks 文件时使用。
---

# KiriKiri2 剧情脚本解密器（ksdec）

## 工具位置

| 主机 | Release 产物 |
|------|--------------|
| macOS | `tools/bin/mac/rel/ksdec` |
| Linux | `tools/bin/linux/rel/ksdec` |
| Windows | `tools/bin/win/rel/ksdec.exe` |

下文用 `<ksdec>`、`<xp3>` 和 `<temp-dir>` 表示当前主机二进制及临时目录，执行前必须替换为真实路径。

## 功能说明

解密 KiriKiri2 FEFE 加密的剧情脚本文件并输出 UTF-8 文本。处理 KiriKiri2 TextStream 使用的所有加密模式：

| 格式 | 文件头 | 说明 |
|------|--------|------|
| FEFE 模式 0 | `FE FE 00` | 对 UTF-16LE 进行 XOR 加密 |
| FEFE 模式 1 | `FE FE 01` | 对 UTF-16LE 进行相邻位交换 |
| FEFE 模式 2 | `FE FE 02` | zlib 压缩的 UTF-16LE |
| 纯 UTF-16LE | `FF FE` | 带 BOM 标记，无加密 |
| 纯 UTF-16BE | `FE FF` | 带 BOM 标记，字节序翻转 |
| 纯 UTF-8 | 任意 | 直接透传 |

## 用法

```bash
# 解密输出到标准输出
<ksdec> input.ks

# 解密输出到文件
<ksdec> -o output.txt input.ks

# 批量解密多个文件
<ksdec> file1.ks file2.ks file3.tjs

# 解密目录下所有 .ks 文件
find <temp-dir>/gamedata -name "*.ks" -exec <ksdec> {} \;
```

诊断信息（检测到的格式、字符数）输出到 stderr。解密后的文本输出到 stdout。

## 典型工作流

1. 使用 xp3 工具解包游戏归档
2. 使用 ksdec 解密剧情脚本文件
3. 分析 KAG 脚本逻辑（标签、标记、宏）

```bash
# 完整流程示例
<xp3> -o <temp-dir>/gamedata game.xp3
<ksdec> <temp-dir>/gamedata/data/sysscn/first.ks
```

## 从源码构建

```bash
# 独立编译（无需 cmake）
c++ -std=c++17 -O2 -lz -o <ksdec-output> tools/ksdec/main.cpp

# 通过当前仓库已有的 macOS native preset（与其他工具一起构建）
cmake --preset "MacOS Release Config" -DBUILD_TOOLS=ON
cmake --build out/macos/release --target ksdec
```

Linux/Windows 当前没有仓库内置 native preset；先提供经过验证的 native CMake 配置，再构建 `ksdec` target，禁止编造 preset 名。

## 技术细节

加密/解密逻辑与 `cpp/core/base/TextStream.cpp`（tTVPTextReadStream 构造函数，第 157-210 行）一致。该工具是一个独立的 C++ 二进制文件，仅依赖 zlib——不需要 TJS2 引擎。
