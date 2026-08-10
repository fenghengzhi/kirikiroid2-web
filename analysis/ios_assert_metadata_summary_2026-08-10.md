# iOS `__assert_rtn` 元数据统计（2026-08-10）

## 范围

- 目标：`Kirikiroid2_1.3.9_iOS_arm64` 与 `Kirikiroid2_1.3.9_iOS_armv7`。
- 本报告统计标准 C/C++ `assert` 路径的 `__assert_rtn(func, file, line, expression)` 四类直接信息。
- Objective-C `NSAssertionHandler` 不在本次统计范围内。
- 两个 IDB 会话均经 `server_health` 核对，`module` 和 `input_path` 分别对应仓库内两份 iOS 参考二进制；Hex-Rays 与字符串缓存均可用。

## 提取方法

- arm64：指令级枚举 `BL ___assert_rtn`，目标桩 `0x1014CEF18`；每个调用点回溯 `X0/X1/W2/X3`，得到 `func/file/line/expression`。
- armv7：指令级枚举 `BLX j____assert_rtn`，跳板 `0xE04B68`（最终导入桩 `0x135DB74`）；逐个 fresh decompile 调用者函数并读取四个常量实参。
- 共 fresh decompile 215 个独立调用者函数：arm64 144 个，armv7 71 个。
- `xrefs_to` 的 `xref_count` 分别显示 603/554，但接口只展开 10 条明细，且与可执行段中的直接调用枚举不一致；因此没有把它当作调用总数。清单以指令级完整扫描结果为准。

## 汇总

| 指标 | arm64 | armv7 | 合并 |
|---|---:|---:|---:|
| 直接调用点 | 300 | 115 | 415 |
| 独立调用者函数 | 144 | 71 | 215 |
| 去重四元组 | 247 | 63 | 247 |
| 不重复的 `func` 值 | 110 | 33 | 110 |
| 不重复的 `file` 值 | 31 | 14 | 31 |
| 不重复的 `line` 数值 | 237 | 63 | 237 |
| 不重复的 `expression` 值 | 129 | 38 | 129 |
| 无法解析四字段的调用 | 0 | 0 | 0 |

直接调用点多于四元组，是因为编译器会为不同控制流路径生成重复的 `__assert_rtn` 调用。arm64 有 53 个重复调用，armv7 有 52 个重复调用。

## 架构交集

- 两架构共同四元组：63。
- 仅 arm64：184。
- 仅 armv7：0。
- armv7 的全部 63 个去重四元组都是 arm64 集合的子集；arm64 额外保留了更多核心、插件和第三方库断言。

## 按源文件统计

`tuple` 是按 `(func, file, line, expression)` 去重后的数量；`call` 是直接机器调用点数量。

| 编译时源文件 | arm64 tuple/call | armv7 tuple/call | 合并 tuple |
|---|---:|---:|---:|
| `src/core/visual/RenderManager.cpp` | 27/70 | 27/70 | 27 |
| `vendor/jxrlib/current/jxrgluelib/JXRGlueJxr.c` | 45/45 | 0/0 | 45 |
| `vendor/jxrlib/current/image/decode/strdec.c` | 38/38 | 0/0 | 38 |
| `vendor/jxrlib/current/image/encode/strenc.c` | 29/29 | 0/0 | 29 |
| `vendor/jxrlib/current/image/sys/strcodec.c` | 22/22 | 0/0 | 22 |
| `src/core/movie/ffmpeg/MathUtils.h` | 2/10 | 2/10 | 2 |
| `src/plugins/LayerExDraw.cpp` | 6/6 | 6/6 | 6 |
| `src/core/visual/ogl/RenderManager_ogl.cpp` | 5/5 | 5/5 | 5 |
| `vendor/jxrlib/current/image/decode/segdec.c` | 10/10 | 0/0 | 10 |
| `vendor/jxrlib/current/jxrgluelib/JXRMeta.c` | 10/10 | 0/0 | 10 |
| `src/core/visual/ogl/pvrtc.cpp` | 4/4 | 4/4 | 4 |
| `src/plugins/AlphaMovie.cpp` | 4/4 | 4/4 | 4 |
| `src/core/sound/win32/WaveMixer.cpp` | 3/3 | 3/3 | 3 |
| `src/plugins/PSBFile.cpp` | 3/3 | 3/3 | 3 |
| `src/plugins/ripemd160.c` | 3/3 | 3/3 | 3 |
| `src/libavcodec/vorbisenc.c` | 6/6 | 0/0 | 6 |
| `src/core/movie/ffmpeg/AEChannelInfo.cpp` | 3/4 | 0/0 | 3 |
| `src/core/movie/ffmpeg/KRMovieLayer.cpp` | 2/2 | 2/2 | 2 |
| `src/core/movie/ffmpeg/Ref.h` | 1/2 | 1/2 | 1 |
| `vendor/jxrlib/current/image/encode/segenc.c` | 4/4 | 0/0 | 4 |
| `src/libavformat/asfdec_f.c` | 4/4 | 0/0 | 4 |
| `vendor/jxrlib/current/image/sys/adapthuff.c` | 3/3 | 0/0 | 3 |
| `vendor/jxrlib/current/image/sys/perfTimerANSI.c` | 3/3 | 0/0 | 3 |
| `src/core/movie/ffmpeg/AEFactory.cpp` | 1/1 | 1/1 | 1 |
| `src/core/movie/ffmpeg/KRMoviePlayer.cpp` | 2/2 | 0/0 | 2 |
| `src/core/visual/BitmapLayerTreeOwner.cpp` | 1/1 | 1/1 | 1 |
| `src/plugins/sqlite3/xp3_vfs.cpp` | 1/1 | 1/1 | 1 |
| `vendor/jxrlib/current/image/decode/JXRTranscode.c` | 2/2 | 0/0 | 2 |
| `src/core/environ/ui/ConsoleWindow.cpp` | 1/1 | 0/0 | 1 |
| `src/core/visual/LayerBitmapIntf.cpp` | 1/1 | 0/0 | 1 |
| `vendor/jxrlib/current/image/decode/postprocess.c` | 1/1 | 0/0 | 1 |

## 输出文件

- `ios_assert_metadata_2026-08-10.csv`：247 行去重四元组；包含四个直接字段、出现架构及各架构调用次数。
- `ios_assert_calls_2026-08-10.csv`：415 行直接调用点；额外包含二进制、架构、IDA 调用者函数、调用者地址和调用点地址。

CSV 均使用 UTF-8、RFC 4180 风格双引号转义。地址只在其所属二进制内有效。
