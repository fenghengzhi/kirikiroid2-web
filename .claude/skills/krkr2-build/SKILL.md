---
name: krkr2-build
description: 使用 CMake 和 Emscripten 编译构建 KrKr2 WebAssembly 项目。当用户需要编译、构建或重新构建项目、配置构建预设或排查构建错误时使用。
---

# KrKr2 编译构建

## 前置条件

确保以下环境变量已设置：

```bash
export VCPKG_ROOT=/path/to/vcpkg
source /path/to/emsdk/emsdk_env.sh
```

依赖工具：Emscripten SDK、vcpkg、ninja、cmake 3.31.1+、bison 3.8.2+、python3。

## 构建命令

产物文件：`index.html`、`index.js`、`index.wasm`、`index.data`、`index.worker.js`。

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
