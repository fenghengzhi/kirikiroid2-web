# Web 版 7z 归档支持：四参考二进制对照分析

日期：2026-09-02

## 问题与复现证据

游戏包 `【KR】NEKOPARA 0.rar` 解压后包含一个名为 `patch.xp3` 的文件。它并非 XP3，而是标准 7z 归档：文件头为 `37 7a bc af 27 1c`，内含 `時計.png`。

Web Debug 与 ASan 构建都能装载入口归档和其余 XP3，但在脚本执行 `TVPAddAutoPath("file://./patch.xp3>")` 后，于 `Rebuilding Auto Path Table` 阶段崩溃。ASan 报告：

```text
AddressSanitizer: null-pointer-dereference on address 0x00000004
READ of size 4 at 0x00000004 thread T0
PC 0x0140d563: tTVPArchive::AddRef()
```

本地调用链为：

```text
tTVPArchiveCache::Get
  -> TVPOpenArchive(name, true)
  -> tTJSRefHolder<tTVPArchive>(arc)
  -> arc->AddRef()
```

当前 Web 构建从 `core_base_module` 移除了 `7zArchive.cpp`，并以 `cpp/core/environ/web/stubs.cpp` 中恒定返回 `nullptr` 的 `TVPOpen7ZArchive` 替代。因此物理文件存在、但没有任何可用归档解析器时，空指针进入 `tTJSRefHolder` 并崩溃。

## 四参考二进制映射

四个目标的 `.i64` 数据库均可读，Hex-Rays 均可用；以下函数都在本轮重新反编译。

| 目标 | `TVPOpen7ZArchive` | `SevenZipArchive::Open` | `TVPOpenArchive` |
| --- | --- | --- | --- |
| Android arm64 | `sub_8DA3E4 @ 0x8da3e4` | `sub_8DA5B4 @ 0x8da5b4` | `sub_90C074 @ 0x90c074` |
| Android armv7 | `sub_6AEAEA @ 0x6aeaea` | `sub_6AEBA4 @ 0x6aeba4` | `sub_6C99F0 @ 0x6c99f0` |
| iOS arm64 | `sub_100285C8C @ 0x100285c8c` | `sub_100285D48 @ 0x100285d48` | `sub_1001D96B8 @ 0x1001d96b8` |
| iOS armv7 | `sub_288660 @ 0x288660` | `sub_28875C @ 0x28875c` | `sub_1D802E @ 0x1d802e` |

用于定位重建路径的字符串 `(info) Rebuilding Auto Path Table ...` 在普通字符串搜索中均未命中；按 UTF-16LE 搜索后命中如下地址，并读取字节确认了完整字符串和终止符：

| 目标 | UTF-16LE 地址 |
| --- | --- |
| Android arm64 | `0x1519468` |
| Android armv7 | `0xdbe856` |
| iOS arm64 | `0x10196e06c` |
| iOS armv7 | `0x1760418` |

## 参考实现的共同语义

四个 `TVPOpen7ZArchive` 的共同伪代码为：

```cpp
auto pos = stream->GetPosition();
bool is7z = stream->ReadI16LE() == 0x7A37;
stream->SetPosition(pos);
if(!is7z) return nullptr;

auto archive = new SevenZipArchive(name, stream, normalizeFileName);
if(!archive->Open()) {
    delete archive;
    return nullptr;
}
return archive;
```

四个 `SevenZipArchive::Open` 均调用 `SzArEx_Open`。失败时释放/清空所持流并返回 `false`；成功时遍历 `NumFiles`、跳过目录、读取 UTF-16 文件名、按参数规范化名称、保存 `(名称, 原始索引)`，并在启用规范化时排序。

四个 `TVPOpenArchive` 的共同伪代码为：

```cpp
auto stream = TVPCreateStream(name);
if(!stream) return nullptr;

for(auto creator : {TVPOpenZIPArchive,
                    TVPOpen7ZArchive,
                    TVPOpenTARArchive,
                    tTVPXP3Archive::Create}) {
    if(auto archive = creator(name, stream, normalizeFileName))
        return archive;
    stream->SetPosition(0);
}

delete stream;
return nullptr;
```

Android arm64 被编译器展开为四段连续调用；Android armv7、iOS arm64 和 iOS armv7 保留四项函数表循环。对象大小、容器布局和原子引用计数代码生成因 ABI 不同而不同，但探测顺序、7z 魔数判断、打开与枚举语义完全一致。

## 与当前源码的对照

`cpp/core/base/impl/StorageImpl.cpp` 中的 `ArchiveCreators` 已保持参考顺序：ZIP、7z、TAR、XP3；每个解析器失败后也会把流位置恢复为零。

`cpp/core/base/7zArchive.cpp` 已完整实现参考路径：以 `0x7A37` 判断格式、调用 7zip SDK、枚举 UTF-16 名称、跳过目录、规范化并排序。偏差仅在 Web 构建配置：

- `vcpkg.json` 以 `!emscripten` 排除了 `7zip`。
- `cpp/core/base/CMakeLists.txt` 在 Emscripten 下移除 `7zArchive.cpp` 并定义 `KRKR2_NO_7ZIP`。
- `cpp/core/environ/web/stubs.cpp` 用恒定返回空的同名函数补符号。

另外，`tTVPArchiveCache::Get` 在物理文件存在但 `TVPOpenArchive` 返回空时没有空值保护，这解释了 ASan 所见的 `tTVPArchive::AddRef()` 空指针。不过参考二进制本身提供并调用 7z 解析器；对这个兼容性问题，恢复缺失后端比仅隐藏崩溃更忠实，也能让游戏实际读取补丁内容。

## Emscripten 可构建性验证

使用仓库固定的 vcpkg baseline、overlay port 和 `wasm32-emscripten` triplet，在临时安装根中执行 `vcpkg install 7zip:wasm32-emscripten` 已成功，构建版本为 `7zip@24.09#1`，包 ABI 为：

```text
5fb4607134c9164125ad03f703ad59523e067d6a44f200ed391fcd0505f94bf1
```

产物包含 `7zip` 头文件、`lib7zip.a` 和 CMake package config，因此无需引入与参考实现不同的 libarchive 适配层。

## 实施方案

1. 为 Emscripten 启用已有 `7zip` vcpkg 依赖。
2. Web 构建继续编译已有 `7zArchive.cpp`，链接 `7zip::7zip`。
3. 删除 Web 环境中恒定返回空的 `TVPOpen7ZArchive` stub。
4. 用原故障游戏经 Playwright CLI 回归，确认 `patch.xp3` 被识别、自动路径重建越过原崩溃点并进入游戏界面。

## 实施后验证

- `Web Debug Config` 重新配置与编译成功，构建版本为 `20260902033325`。
- `llvm-nm` 确认 `core_base_module` 定义了 `TVPOpen7ZArchive` 与 `SevenZipArchive::Open`，`core_environ_module` 不再提供空实现。
- 用 Playwright CLI 加载原游戏 ZIP 后，日志经过 `TVPAddAutoPath: file://./patch.xp3>` 和随后的自动路径重建，继续输出 `Startup script ended.`，未再出现 Wasm 运行时异常。
- 1280×720 游戏画布成功显示《ネコぱら Vol.0》主菜单；控制台唯一 error 是与游戏无关的 `/favicon.ico` 404。
