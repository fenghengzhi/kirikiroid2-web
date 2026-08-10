---
name: krkr2-build
description: 使用 CMake 和 Emscripten 编译构建 KrKr2 WebAssembly 项目。当用户需要编译、构建或重新构建项目、配置构建预设或排查构建错误时使用。
---

# KrKr2 编译构建

## 前置条件

确保以下环境变量已设置。POSIX shell：

```bash
export VCPKG_ROOT=/path/to/vcpkg
source /path/to/emsdk/emsdk_env.sh
```

Windows PowerShell 使用同一 emsdk 提供的 PowerShell 环境脚本，不要把 POSIX 的
`source` 命令照搬到 Windows：

```powershell
$env:VCPKG_ROOT = "C:\path\to\vcpkg"
. "C:\path\to\emsdk\emsdk_env.ps1"
```

使用 vcpkg overlay 构建时应确认 `EMSDK_PYTHON` 已由环境脚本导出；若没有，显式指向
当前 emsdk 自带的 Python。这样可避免 vcpkg 误选不兼容的旧解释器或重复下载另一份
嵌入式 Python。不要把这一要求描述成“所有系统 Python 都缺少 `match` 语法”。

依赖工具：Emscripten SDK、vcpkg、ninja、cmake 3.31.1+、bison 3.8.2+、Python 3。
Windows 通常调用 `python`，POSIX 环境通常调用 `python3`；以当前机器实际可执行文件
为准。必须执行 `bison --version`（Windows 也可能是 `win_bison --version`）核对真实
版本，不能根据文件夹或包名猜测；WinFlexBison 2.5.24 实际只有 Bison 3.7.4，不能
处理本项目 `%require "3.8.2"` 的语法文件，2.5.25 才提供 Bison 3.8.2。

如果 CMake 在 Windows 找不到 Ninja 或 Bison，可在配置时传入实际路径：

```powershell
cmake --preset "Web Debug Config" `
  -DCMAKE_MAKE_PROGRAM=C:\path\to\ninja.exe `
  -DBISON_EXECUTABLE=C:\path\to\win_bison.exe
```

## 首次构建前：预编译 Emscripten 端口

Emscripten 的端口库（SDL2、SDL2_ttf 等）在首次使用时按需编译并缓存。Ninja 并行构建时多个 `em++` 进程同时触发端口编译会导致缓存锁冲突（`EM_CACHE_IS_LOCKED` 断言失败）。

**首次构建前**（或清空 emsdk 缓存后），必须先单线程预编译端口：

```bash
embuilder build sdl2 sdl2_ttf sdl2-mt sdl2_ttf-mt
```

此命令会自动编译 freetype、harfbuzz 等依赖。`-mt` 后缀为 pthreads 变体。缓存建立后后续构建无需重复执行。

## 构建命令

固定主要产物：`index.html`、`index.js`、`index.wasm`、`vlfs.js`、`assets.zip`。
`index.worker.js`、`.symbols` 等 sidecar 取决于当前 Emscripten 版本和构建选项，不能
作为固定成功判据。`--preload-file` 生成的 `index.data` 已移除；UI 与游戏资源通过
VirtualLazyFS 按需读取。

### Debug 构建（默认，无 Asan）

```bash
cmake --preset "Web Debug Config"
cmake --build out/web/debug
```

产物在 `out/web/debug/` 下。

### Debug Asan 构建

启用 AddressSanitizer，用于内存问题排查：

```bash
cmake --preset "Web Debug Asan Config"
cmake --build out/web/debug-asan
```

产物在 `out/web/debug-asan/` 下。

### Release 构建

```bash
cmake --preset "Web Release Config"
cmake --build out/web/release
```

产物在 `out/web/release/` 下。
