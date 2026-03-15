# KrKr2 模拟器

KrKr2 模拟器是一款跨平台的模拟器，旨在运行使用吉里吉里引擎（也称为 T Visual Presenter）制作的游戏。该模拟器支持在 Android、Windows、Linux、MacOS 和 Web（WebAssembly）等多个平台上运行，帮助用户在不同设备上体验吉里吉里引擎制作的游戏。

**语言 / Language**: 中文 | [English](README.md)

## 目录

- [KrKr2 模拟器](#krkr2-模拟器)
  - [目录](#目录)
  - [支持平台](#支持平台)
  - [依赖构建工具](#依赖构建工具)
  - [编译环境配置](#编译环境配置)
    - [环境变量](#环境变量)
    - [编译步骤](#编译步骤)
  - [可执行文件位置](#可执行文件位置)
  - [运行 Web 版本](#运行-web-版本)
  - [代码格式化](#代码格式化)
  - [支持的游戏列表](#支持的游戏列表)
  - [插件资源](#插件资源)
  - [许可证](#许可证)

## 支持平台

- **Android**:
  - `arm64-v8a`
  - `x86_64`
- **Windows**:
  - x86_64
- **Linux**:
  - x86_64
- **MacOS**:
  - arm64
- **Web**（WebAssembly）:
  - 所有现代浏览器（Chrome、Edge、Firefox、Safari）

## 依赖构建工具

- **Android**:
  - [ninja@latest](https://github.com/ninja-build/ninja/releases)
  - [cmake@3.31.1+](https://cmake.org/download/)
  - [vcpkg@latest](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started)
  - [Android SDK@33](https://developer.android.com)
  - [Android NDK@28.0.13004108](https://developer.android.com/ndk/downloads)
  - [JDK@17](https://jdk.java.net/archive/)
  - `bison@3.8.2+`
  - `python3`
  - `NASM@latest`
- **Windows**:
  - [ninja@latest](https://github.com/ninja-build/ninja/releases)
  - `Visual Studio 2022`
  - `vcpkg@latest`
  - [cmake@3.31.1+](https://cmake.org/download/)
  - [winflexbison@2.5.25](https://github.com/lexxmark/winflexbison)
  - `python3`
  - `NASM@latest`
- **Linux**:
  - [ninja@latest](https://github.com/ninja-build/ninja/releases)
  - `GCC`
  - `vcpkg@latest`
  - [cmake@3.31.1+](https://cmake.org/download/)
  - `bison@3.8.2+`
  - `python3`
  - `NASM@latest`
  - `YASM`
- **MacOS**:
  - Xcode
  - `vcpkg@latest`
  - [ninja@latest](https://github.com/ninja-build/ninja/releases)
  - [cmake@3.31.1+](https://cmake.org/download/)
  - `bison@3.8.2+`
  - `python3`
  - `NASM@latest`
- **Web**:
  - [Emscripten SDK (emsdk)](https://emscripten.org/docs/getting_started/downloads.html)
  - `vcpkg@latest`
  - [ninja@latest](https://github.com/ninja-build/ninja/releases)
  - [cmake@3.31.1+](https://cmake.org/download/)
  - `bison@3.8.2+`
  - `python3`

## 编译环境配置

### 环境变量

请根据所使用的平台配置以下环境变量：

- **Android**:
  - `VCPKG_ROOT`: `/path/to/vcpkg`
  - `ANDROID_SDK`: `/path/to/androidsdk`
  - `ANDROID_NDK`: `/path/to/androidndk`
- **Windows**:
  - `VCPKG_ROOT`: `D:/vcpkg`（注意使用正斜杠 `/` 或双反斜杠 `\\`）
  - `bison`: 将 `winflexbison` 的路径添加到 `PATH` 环境变量中。
- **Linux OR MacOS**:
  - `VCPKG_ROOT`: `/path/to/vcpkg`
- **Web**:
  - `VCPKG_ROOT`: `/path/to/vcpkg`
  - `EMSDK`: `/path/to/emsdk`（执行 `source emsdk_env.sh` 自动设置）

> **注意**: 在 Windows 上，环境变量路径必须使用 `/` 或 `\\`，避免使用单一的 `\`。例如：
>
> - **错误示例**: `D:\vcpkg`（cmake 不转义 `\`，导致路径错误）
> - **正确示例**: `D:/vcpkg`

### 编译步骤

- **Android**:
  - 运行: `./platforms/android/gradlew -p ./platforms/android assembleDebug`
> 如果遇到`glib`无法安装查看[FAQ#安装glib失败](./doc/FAQ.md#安装glib失败)
  
- **Windows**:
  - 运行: `./scripts/build-windows.bat`
  
- **Linux**:
  - 运行: `./scripts/build-linux.sh`

- **MacOS**:
  - 运行:
  ```
    cmake --preset="MacOS Debug Config"
    cmake --build --preset="MacOS Debug Build"
  ```

- **Web**:
  ```bash
  source /path/to/emsdk/emsdk_env.sh
  cmake --preset "Web Release Config"
  cmake --build out/web/release
  ```
  Debug 版本将 `Release` 替换为 `Debug`：
  ```bash
  cmake --preset "Web Debug Config"
  cmake --build out/web/debug
  ```
  
- **使用Docker容器**:
  - Build Android: `docker build -f .devcontainer/android.Dockerfile -t android-builder .`
  - Build Linux: `docker build -f .devcontainer/linux.Dockerfile -t linux-builder .`

## 可执行文件位置

- **Android**:
  - Debug 版本: `platforms/android/out/android/app/outputs/apk/debug/*.apk`
  - Release 版本: `platforms/android/out/android/app/outputs/apk/release/*.apk`
- **Windows**:
  - 可执行文件: `out/windows/debug/bin/krkr2/krkr2.exe`
- **Linux**:
  - 可执行文件: `out/linux/debug/bin/krkr2/krkr2`
- **MacOS**:
  - 可执行文件: `out/macos/debug/bin/krkr2/krkr2.app`
- **Web**:
  - Debug: `out/web/debug/krkr2.html`、`krkr2.js`、`krkr2.wasm`、`krkr2.worker.js`
  - Release: `out/web/release/krkr2.html`、`krkr2.js`、`krkr2.wasm`、`krkr2.worker.js`

## 运行 Web 版本

Web 版本需要 [跨域隔离](https://web.dev/cross-origin-isolation-guide/) 响应头（`COOP` / `COEP`）以支持 `SharedArrayBuffer`（pthreads 依赖）。普通的 HTTP 服务器无法正常运行。

使用项目自带的 `coi-server.py`：

```bash
python3 coi-server.py out/web/release [http端口] [https端口] [--xp3 游戏文件.xp3]
```

服务器同时启动两个服务：
- **HTTP** 端口 8080（默认）— 用于 `localhost` 本地调试
- **HTTPS** 端口 8443（默认）— 用于局域网内其他设备访问

然后在浏览器中打开 `http://localhost:8080/krkr2.html`。

#### 直接指定游戏文件

通过 `--xp3` 参数可以让服务器直接托管一个本地 `.xp3` 文件：

```bash
python3 coi-server.py out/web/release --xp3 /path/to/game/data.xp3
```

服务器会将该文件映射到 `/data.xp3` 路径，并输出包含 `?xp3=` 参数的完整 URL。打开该 URL 后，网页会自动下载并启动游戏，无需手动选择文件。

也可以通过 URL 参数 `?xp3=<地址>` 手动指定任意可访问的 `.xp3` 文件地址：

```
http://localhost:8080/krkr2.html?xp3=/data.xp3
```

> **注意**：`SharedArrayBuffer` 要求 `localhost` 或 HTTPS 环境。如需局域网访问，将 `server.crt` 和 `server.key` 放在 `coi-server.py` 同级目录以启用 HTTPS 服务。可通过以下命令生成自签名证书：
> ```bash
> openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt -days 365 -nodes
> ```

## 代码格式化
- `clang-format@20`
- **Linux**:
    ```bash
    clang-format -i --verbose $(find ./cpp ./platforms ./tests ./tools -regex ".+\.\(cpp\|cc\|h\|hpp\|inc\)")
    ```

- **MacOS**:
    ```bash
    clang-format -i --verbose $(find ./cpp ./platforms ./tests ./tools -name "*.cpp" -o -name "*.cc" -o -name "*.h" -o -name "*.hpp" -o -name "*.inc")
    ```

- **Windows**:
    ```powershell
    Get-ChildItem -Path ./cpp, ./platforms, ./tests, ./tools -Recurse -File | 
    Where-Object { $_.Name -match '\.(cpp|cc|h|hpp|inc)$' } | 
    ForEach-Object { clang-format -i --verbose $_.FullName }
    ```

## 支持的游戏列表
- [games](./doc/support_games.txt)

## 插件资源

您可以在 [wamsoft 的 GitHub 仓库](https://github.com/orgs/wamsoft/repositories?type=all) 中找到相关的插件和工具库。

## 许可证

此项目遵循 MIT 许可证。详细信息请参阅 [LICENSE](./LICENSE) 文件。
