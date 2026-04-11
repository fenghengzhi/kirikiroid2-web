---
name: krkr2-mtndump
description: 使用 mtndump 工具把 KiriKiri2 的 .mtn / .psb motion 文件里所有 src/ 源贴图导出为独立的 PNG，并生成一份 TSV manifest（source、png、宽高、origin、BGRA 标志）。用于 KrKr2/E-mote/EmotePlayer 的离线资源提取、differential testing（对比 libkrkr2.so 与 Web port 的贴图输出）、调试 motionplayer/EmotePlayer 渲染路径，或者只是想知道一个 motion 到底引用了哪些 src/<group>/<name> 贴图。典型触发：用户提到 "mtndump"、"dump motion"、"dump .mtn"、"dump .psb"、"提取 motion 贴图"、"解包 e-mote"、"导出 PSB 图像"、"EmotePlayer 的 src"、"emote 贴图"、"motion snapshot"；或者在调试 motionplayer 渲染问题需要看原始贴图、做 KrKr2 差分测试需要对比贴图输出、提到 `tests/test_files/emote/*.psb` 或 seed `742877301` 时。
---

# KrKr2 Motion PSB 贴图导出器（mtndump）

## 工具位置

```
tools/bin/mac/rel/mtndump
```

## 它做什么

加载一个 `.mtn`（KiriKiri2 motion 文件）或 `.psb`（motion 类型的 PhyreEngine Script Binary），把 PSB 树里 `source/<group>/icon/<name>/pixel` 节点存放的每一张贴图解码后导出为 RGBA PNG。同时生成 `manifest.tsv`，记录每张贴图的元信息。

PSB 解析走的是项目里本地实现的 `motion::detail::loadMotionSnapshot`（`cpp/plugins/motionplayer/RuntimeSupport.cpp`）和 `motion::internal::findPSBResourceBySourceName`（`cpp/plugins/motionplayer/PlayerInternal.h`）。后者架构对齐 libkrkr2.so `sub_6948E8`，所以工具输出的贴图、宽高、`origin_x`/`origin_y` 都应该和 Android 原版一致——这是它适合做 differential testing 的原因。

## 前置条件

### 构建

mac 构建：

```bash
export VCPKG_ROOT=/Users/bytedance/vcpkg
cmake --preset "MacOS Release Config" -DBUILD_TOOLS=ON -DBISON_EXECUTABLE=/opt/homebrew/opt/bison/bin/bison
cmake --build out/macos/release --target mtndump
```

构建产物 `tools/bin/mac/rel/mtndump`（约 49 MB，静态链接 krkr2plugin + motionplayer + core）。如果之前没构建过 `BUILD_TOOLS`，第一次 configure 可能要几分钟（vcpkg 会装 opencv4 等依赖）。

### 不支持的平台

`BUILD_TOOLS` 只在 non-iOS / non-Android / non-Web 时启用。Emscripten / Android NDK 构建不会产出 mtndump。

## 基本用法

```bash
# 明文 PSB（无加密）
tools/bin/mac/rel/mtndump -o /tmp/out file.psb

# 加密 emote PSB — 必须提供 seed
tools/bin/mac/rel/mtndump -s 742877301 -o /tmp/out "e-mote3.0バニラパジャマa.psb"

# 批量处理
tools/bin/mac/rel/mtndump -s 742877301 -o /tmp/out emote1.psb emote2.psb emote3.psb

# 从 XP3 解包再处理（典型工作流）
tools/bin/mac/rel/xp3 -o /tmp/game game.xp3
tools/bin/mac/rel/mtndump -s <seed> -o /tmp/motion_dump /tmp/game/**/*.mtn
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
| `origin_x` | 贴图锚点 X（从 icon 节点读取），libkrkr2.so 里用于 `origin = pos - matrix × (originX, originY)` |
| `origin_y` | 贴图锚点 Y |
| `decoded_bgra` | `1` 表示原始解码 buffer 是 BGRA 序（palette 路径），`0` 表示 RGBA 序。导出的 PNG 已经是 RGBA，这列只是 debug 标志 |

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

### `Failed to load motion snapshot: foo.psb (wrong seed for encrypted file?)`

PSB 格式能解析但不是 motion 类型。motionplayer 只接受 `type == Motion` 的 PSB。`emote` / `scn` / `image` / `mmo` 等其他 PSB 类型会在这里被拒绝。检查文件是不是真的是 motion：
- `.mtn` 后缀几乎总是 motion
- `.psb` 可能是任意 PSB 类型
- `.pimg` 通常是 emote，不是 motion（不能用 mtndump）

### `Skipping invalid file: foo.psb`

路径不存在，或给的是一个目录。mtndump 不会递归目录，需要自己展开 glob：`mtndump foo/**/*.psb`（需 shell 支持 `globstar`）。

### `skip src/xxx/yyy: icon node not found in PSB tree`

PSB 的 `source/<group>/icon/<name>` 路径找不到对应条目。通常因为：
- `src/<group>/<name>` 引用是 stale 的（PSB 里其它地方提到这个名字，但没有实际的 icon 节点）
- 命名不一致（大小写、连字符）
- motion 文件依赖外部 emote 资源（跨 motion 引用）——mtndump 不会跟着去加载别的文件

### `skip src/xxx/yyy: decoded pixel buffer empty`

`findPSBResourceBySourceName` 找到了 resource blob，但 RL 解压或 palette 解码返回空。通常是 PSB 资源格式不对或数据损坏。

### `skip src/xxx/yyy: decoded pixel buffer too small`

解码出来的字节数小于 `width * height * 4`。resource blob 格式可能被误判（比如 palette 路径被当成 raw RGBA），或者 width/height 元数据错了。

## 典型用例

### 1. 快速看一个 motion 引用了哪些 src

```bash
mtndump -s 742877301 -o /tmp/dump emote.psb
cut -f1 /tmp/dump/emote/manifest.tsv | head -50
```

### 2. Differential testing — 对比 libkrkr2.so 与 Web port 的贴图输出

libkrkr2.so 的 `sub_6948E8`（PSB 贴图查找）和本地 `findPSBResourceBySourceName` 架构对齐。理论上两边导出同一个 .psb 应该得到 bit-identical 的 PNG。不一致就说明本地实现有偏差——典型表现是 BGRA/RGBA 通道顺序错、palette 解码结果不一致、RL 解压 size 错。

```bash
# 两个 checkout 各自跑 mtndump，diff 结果
mtndump -s 742877301 -o /tmp/libkrkr2_ref ref.psb
mtndump -s 742877301 -o /tmp/port_out ref.psb
diff -r /tmp/libkrkr2_ref/ref /tmp/port_out/ref
```

### 3. 调试 EmotePlayer 渲染异常时查源贴图

当发现某个 src（例如 `src/face_eye_hitomi_l`）在 Web 构建里渲染不对，想知道源贴图是什么样：

```bash
mtndump -s <seed> -o /tmp/debug broken.psb
open /tmp/debug/broken/src/face_eye_hitomi_l/icon1.png
```

然后用 Finder 预览对比渲染出来的异常画面。如果源贴图本身就是错的，说明 PSB 解析或 palette 解码有问题；如果源贴图正确但渲染出来的合成图错，说明问题在 Layer/DrawDevice 渲染路径。

### 4. 批量校验一批 emote 资产的 seed 是否正确

```bash
for f in /path/to/assets/*.psb; do
  tools/bin/mac/rel/mtndump -s 742877301 -o /tmp/check "$f" > /dev/null 2>&1 \
    && echo "OK  $f" \
    || echo "FAIL $f"
done
```

FAIL 的文件要么 seed 不对，要么不是 motion 类型的 PSB。

### 5. 看 manifest 里某张贴图的 origin

锚点 debug 时经常需要的数据：

```bash
grep "src/face_nose/icon1" /tmp/dump/emote/manifest.tsv
# src/face_nose/icon1   src/face_nose/icon1.png   44   50   22   25   0
#                                                 ^W   ^H   ^OX  ^OY  ^BGRA
```

`origin_x=22, origin_y=25` 说明这张 44×50 的贴图锚点是正中心。libkrkr2.so 的贴图位置计算是 `screen_pos = layer_pos - transform × (origin_x, origin_y)`。

## 技术细节

- PSB 加载：`motion::detail::loadMotionSnapshot(path, seed)` → `PSBFile::loadPSBFile`
- 贴图查找：`motion::internal::findPSBResourceBySourceName`（navigate `source/<group>/icon/<name>`），对齐 libkrkr2.so `sub_6948E8 at 0x6948E8`
- 像素布局：BGRA 路径（palette）用 `memcpy` 整行拷贝；RGBA 路径（其它）逐像素 swap R/B
- PNG 写出：走 `tTVPBaseBitmap` + `TVPSaveAsPNG`（与游戏运行时同一条 PNG encoder）
- manifest 的 `decoded_bgra` 只是 debug flag，PNG 写出时已经统一成 RGBA

## 不适用场景

- **Emote pimg 文件**：pimg 是 `PSB::PSBType::Emote`，不是 Motion。mtndump 会直接拒绝。要从 pimg 提贴图请另写工具（或用 `tools/xp3` 解包然后靠 `psb_decode` 类工具处理）。
- **渲染过的合成画面**：mtndump 只导 **源** 贴图。如果你想看一个角色在某个动作某一帧的合成效果，需要跑完整 EmotePlayer 渲染路径（浏览器里或者写一个基于 motionplayer 的离线渲染工具）。
- **TJS2 脚本**：不处理 `.tjs` / `.ks`，那是 `tjs2-disasm` / `ksdec` 的工作。
