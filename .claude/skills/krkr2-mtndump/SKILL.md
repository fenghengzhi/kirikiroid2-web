---
name: krkr2-mtndump
description: 使用 mtndump 从 KiriKiri2 的 .mtn / .psb motion 文件导出 src/ 源贴图和 manifest.tsv。适用于查看 motion 引用的贴图、尺寸与锚点，排查源贴图问题，批量验证解密 seed，以及比较两个 mtndump 版本的导出结果。典型触发：用户提到 "mtndump"、"dump motion"、"dump .mtn"、"dump .psb"、"提取 motion 贴图"、"导出 PSB 图像"、"EmotePlayer 的 src"、"emote 贴图"，或提到 `tests/test_files/emote/*.psb`、seed `742877301`。
---

# KrKr2 Motion PSB 贴图导出器（mtndump）

## 工具位置

| 主机 | Release 产物 |
|------|--------------|
| macOS | `tools/bin/mac/rel/mtndump` |
| Linux | `tools/bin/linux/rel/mtndump` |
| Windows | `tools/bin/win/rel/mtndump.exe` |

下文用 `<mtndump>`、`<xp3>` 和 `<temp-dir>` 表示当前主机二进制及临时目录，执行前必须替换为真实路径。

## 它做什么

加载一个 `.mtn`（KiriKiri2 motion 文件）或 `.psb`（motion 类型的 PhyreEngine Script Binary），把 PSB 树里 `source/<group>/icon/<name>/pixel` 节点存放的每一张贴图解码后导出为 RGBA PNG。同时生成 `manifest.tsv`，记录每张贴图的元信息。

## 前置条件

### 构建

当前仓库已有 preset 的 macOS Release 构建：

```bash
test -n "$VCPKG_ROOT"  # 应由当前机器配置提供，不要写死个人目录
cmake --preset "MacOS Release Config" -DBUILD_TOOLS=ON
cmake --build out/macos/release --target mtndump
```

macOS 构建产物为 `tools/bin/mac/rel/mtndump`。Linux/Windows 当前没有仓库内置 native preset；先提供经过验证的 native CMake 配置，再构建 `mtndump` target，禁止编造 preset 名。如果之前没构建过 `BUILD_TOOLS`，第一次 configure 可能要几分钟（vcpkg 会安装依赖）。

### 不支持的平台

`BUILD_TOOLS` 只在 non-iOS / non-Android / non-Web 时启用。Emscripten / Android NDK 构建不会产出 mtndump。

## 基本用法

```bash
# 明文 PSB（无加密）
<mtndump> -o <temp-dir>/out file.psb

# 加密 emote PSB — 必须提供 seed
<mtndump> -s 742877301 -o <temp-dir>/out "e-mote3.0バニラパジャマa.psb"

# 批量处理
<mtndump> -s 742877301 -o <temp-dir>/out emote1.psb emote2.psb emote3.psb

# 从 XP3 解包再处理（典型工作流）
<xp3> -o <temp-dir>/game game.xp3
<mtndump> -s <seed> -o <temp-dir>/motion_dump <temp-dir>/game/**/*.mtn
```

## 参数

| 参数 | 说明 |
|---|---|
| `files` (位置参数，至少一个) | 一个或多个 `.mtn` / `.psb` 输入文件。目录会被跳过。 |
| `-o, --output` | 输出根目录（默认 `./`）。不存在会自动创建。 |
| `-s, --seed` | 解密种子（默认 `0`，即明文）。加密 PSB 必须传这个，否则解析会抛 `PSBArray bad length type size`。 |
| `-h, --help` | 打印帮助 |
| `-v, --version` | 打印版本 |

## 已知 seed 值

- **`742877301`** — krkr2 项目的 emote 测试 fixture 种子（来自 `tests/unit-tests/plugins/motionplayer-dll.cpp:21` 的 `kEmoteSeed`）。`tests/test_files/emote/` 下所有 .psb/.pimg 都用这个 seed 加密。
- **`0`** — 明文 PSB，适用于已经被 `ksdec` 之类工具解密或本来就没加密的文件。

游戏自带的 motion 资源通常使用游戏特定的 seed，需要从游戏脚本（`Config.tjs` 或初始化流程）里查 `Motion.EmotePlayer.setEmotePSBDecryptSeed(N)` 调用。可以用 `tjs2-disasm` skill 反汇编相关 .tjs 找这个数字。

## 输出结构

```
<output_root>/
└── <input_stem>/                 # 例如输入是 foo.psb，这里就是 foo/
    ├── manifest.tsv              # TSV，首行是表头
    └── src/
        ├── <group1>/
        │   ├── <name1>.png       # 8-bit RGBA, non-interlaced
        │   ├── <name2>.png
        │   └── ...
        ├── <group2>/
        │   └── ...
        └── #custom/              # "#" 开头是 e-mote 自定义槽位
            └── 1.png
```

**注意**：输出路径直接拼 PSB 里的 `src/<group>/<name>` 字段，不做路径清洗。对一次性 dev 工具这是可接受的——但不要对来源不明的 PSB 使用 mtndump 写到系统敏感目录。

### manifest.tsv

7 列，tab 分隔：

| 列 | 含义 |
|---|---|
| `source` | PSB 里的完整 src 路径，例如 `src/face_nose/icon1` |
| `png` | 相对 `manifest.tsv` 的 PNG 路径 |
| `width` | 贴图宽（像素）|
| `height` | 贴图高（像素）|
| `origin_x` | 贴图锚点 X |
| `origin_y` | 贴图锚点 Y |
| `decoded_bgra` | 解码过程使用的颜色顺序标志；`1` 表示 BGRA，`0` 表示 RGBA。导出的 PNG 始终为 RGBA |

示例（`e-mote3.0バニラパジャマa.psb` 的前几行）：

```tsv
source	png	width	height	origin_x	origin_y	decoded_bgra
src/#custom/1	src/#custom/1.png	476	500	238	250	0
src/#custom/3	src/#custom/3.png	116	127	58	63.5	0
src/face_nose/icon1	src/face_nose/icon1.png	44	50	22	25	0
```

## 退出码

| 码 | 含义 |
|---|---|
| `0` | 所有输入文件全部成功导出 |
| `1` | 命令行解析失败（参数错误） |
| `2` | 至少一个输入文件失败（不存在、加密失败、写入失败等）。批处理里其它文件照样继续处理，最后统一返 `2` |

这让 shell 脚本能可靠地区分 "全成功" 和 "部分/全部失败"。

## 常见错误与排查

### `Error processing foo.psb: PSBArray bad length type size`

最常见的错误。PSB 是加密的，但没给 `-s <seed>`（或 seed 不对），导致字节流被错误地当成明文解析。先确认：

1. 文件开头是不是 `50 53 42 20`（`"PSB "`）——明文 PSB 头
2. 如果不是，尝试已知 seed（emote 测试：`742877301`）
3. 都不对的话，用 `tjs2-disasm` 反汇编游戏 `Config.tjs` 或 `Initialize.tjs` 找 `setEmotePSBDecryptSeed(N)`

### `Failed to load raw PSB: foo.psb`

通常表示输入不是有效 PSB，或者加密 seed 不正确。raw reader 不再根据 eager type handler 拒绝文件；只要根下存在 `source/<group>/icon/<name>`，`.mtn`、`.psb` 或 `.pimg` 都可以导出。

### `Skipping invalid file: foo.psb`

路径不存在，或给的是一个目录。mtndump 不会递归目录，需要自己展开 glob：`mtndump foo/**/*.psb`（需 shell 支持 `globstar`）。

### `skip src/xxx/yyy: raw icon/pixel unavailable`

工具发现了一个 `src/<group>/<name>` 引用，但无法导出对应贴图。常见原因：

- PSB 中没有对应的 `source/<group>/icon/<name>` 条目
- 贴图宽高、atlas 范围或 pixel 数据无效
- motion 引用了其它文件中的外部资源；mtndump 不会自动加载依赖文件
- PSB 数据损坏，或使用了错误的解密 seed

## 典型用例

### 1. 快速看一个 motion 引用了哪些 src

```bash
<mtndump> -s 742877301 -o <temp-dir>/dump emote.psb
cut -f1 <temp-dir>/dump/emote/manifest.tsv | head -50
```

### 2. 比较两个 mtndump 版本的导出结果

分别使用基线版本和待验证版本的 `mtndump` 处理同一个输入，然后比较 PNG 与 manifest。这个流程适合检查代码修改是否改变了工具输出。

```bash
# 两个 checkout 必须使用各自构建出的 mtndump
<baseline-mtndump> -s 742877301 -o <temp-dir>/baseline_dump ref.psb
<candidate-mtndump> -s 742877301 -o <temp-dir>/candidate_dump ref.psb
diff -r <temp-dir>/baseline_dump/ref <temp-dir>/candidate_dump/ref
```

### 3. 调试 EmotePlayer 渲染异常时查源贴图

当发现某个 src（例如 `src/face_eye_hitomi_l`）在 Web 构建里渲染不对，想知道源贴图是什么样：

```bash
<mtndump> -s <seed> -o <temp-dir>/debug broken.psb
<image-viewer> <temp-dir>/debug/broken/src/face_eye_hitomi_l/icon1.png
```

用当前主机的图片查看器预览并对比渲染出来的异常画面（macOS 可用 `open`，Linux 常用 `xdg-open`，Windows PowerShell 可用 `Start-Process`）。如果源贴图本身就是错的，说明 PSB 解析或 palette 解码有问题；如果源贴图正确但渲染出来的合成图错，说明问题在 Layer/DrawDevice 渲染路径。

### 4. 批量校验一批 emote 资产的 seed 是否正确

```bash
for f in /path/to/assets/*.psb; do
  <mtndump> -s 742877301 -o <temp-dir>/check "$f" > /dev/null 2>&1 \
    && echo "OK  $f" \
    || echo "FAIL $f"
done
```

FAIL 的文件要么 seed 不对，要么不是 motion 类型的 PSB。

### 5. 看 manifest 里某张贴图的 origin

锚点 debug 时经常需要的数据：

```bash
grep "src/face_nose/icon1" <temp-dir>/dump/emote/manifest.tsv
# src/face_nose/icon1   src/face_nose/icon1.png   44   50   22   25   0
#                                                 ^W   ^H   ^OX  ^OY  ^BGRA
```

`origin_x=22, origin_y=25` 说明这张 44×50 的贴图锚点位于正中心。

## 不适用场景

- **渲染过的合成画面**：mtndump 只导 **源** 贴图。如果你想看一个角色在某个动作某一帧的合成效果，需要跑完整 EmotePlayer 渲染路径（浏览器里或者写一个基于 motionplayer 的离线渲染工具）。
- **TJS2 脚本**：不处理 `.tjs` / `.ks`，那是 `tjs2-disasm` / `ksdec` 的工作。
