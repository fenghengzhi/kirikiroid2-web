# KrKr2 Emulator

This repository contains the **KrKr2 Emulator**, a cross-platform emulator designed to run games made with the **KiriKiri engine** (also known as T Visual Presenter).  
It supports **Android, Windows, Linux, MacOS, and Web (WebAssembly)**, allowing users to play KiriKiri engine games on multiple platforms.  

**语言 / Language**: [中文](README_CN.md) | English

---

## KrKr2 Emulator

### Table of Contents

- [KrKr2 Emulator](#krkr2-emulator)
  - [Supported Platforms](#supported-platforms)
  - [Build Tools](#build-tools)
  - [Build Environment Setup](#build-environment-setup)
    - [Environment Variables](#environment-variables)
    - [Build Steps](#build-steps)
  - [Executable Location](#executable-location)
  - [Code Formatting](#code-formatting)
  - [Supported Games](#supported-games)
  - [Plugin Resources](#plugin-resources)
  - [License](#license)

---

## Supported Platforms

- **Android**:
  - `arm64-v8a`
  - `x86_64`
- **Windows**:
  - x86_64
- **Linux**:
  - x86_64
- **MacOS**:
  - arm64
- **Web** (WebAssembly):
  - All modern browsers (Chrome, Edge, Firefox, Safari)

---

## Build Tools

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

---

## Build Environment Setup

### Environment Variables

- **Android**:
  - `VCPKG_ROOT=/path/to/vcpkg`
  - `ANDROID_SDK=/path/to/androidsdk`
  - `ANDROID_NDK=/path/to/androidndk`
- **Windows**:
  - `VCPKG_ROOT=D:/vcpkg`
  - Add `winflexbison` path to `PATH`.
- **Linux / MacOS**:
  - `VCPKG_ROOT=/path/to/vcpkg`
- **Web**:
  - `VCPKG_ROOT=/path/to/vcpkg`
  - `EMSDK=/path/to/emsdk` (set automatically by `source emsdk_env.sh`)

> **Note**: On Windows, use `/` or `\\` instead of a single `\` in paths.

---

### Build Steps

- **Android**:
  ```bash
  ./platforms/android/gradlew -p ./platforms/android assembleDebug
  ```

  > If you encounter `glib` installation issues, see [FAQ#安装glib失败](./doc/FAQ.md#安装glib失败)

* **Windows**:

  ```powershell
  ./scripts/build-windows.bat
  ```

* **Linux**:

  ```bash
  ./scripts/build-linux.sh
  ```

* **MacOS**:

  ```bash
  cmake --preset="MacOS Debug Config"
  cmake --build --preset="MacOS Debug Build"
  ```

* **Web**:

  ```bash
  source /path/to/emsdk/emsdk_env.sh
  cmake --preset "Web Release Config"
  cmake --build out/web/release
  ```

  For debug build, replace `Release` with `Debug`:

  ```bash
  cmake --preset "Web Debug Config"
  cmake --build out/web/debug
  ```

* **Using Docker**:
* Build Linux: `docker build -f dockers/linux.Dockerfile -t linux-builder .`
* Build Android: `docker build -f dockers/android.Dockerfile -t android-builder .`

---

## Executable Location

* **Android**:
  * Debug: `platforms/android/out/android/app/outputs/apk/debug/*.apk`
  * Release: `platforms/android/out/android/app/outputs/apk/release/*.apk`
* **Windows**: `out/windows/debug/bin/krkr2/krkr2.exe`
* **Linux**: `out/linux/debug/bin/krkr2/krkr2`
* **MacOS**: `out/macos/debug/bin/krkr2/krkr2.app`
* **Web**:
  * Debug: `out/web/debug/krkr2.html`, `krkr2.js`, `krkr2.wasm`, `krkr2.worker.js`
  * Release: `out/web/release/krkr2.html`, `krkr2.js`, `krkr2.wasm`, `krkr2.worker.js`

---

## Serving the Web Build

The Web build requires [Cross-Origin Isolation](https://web.dev/cross-origin-isolation-guide/) headers (`COOP` / `COEP`) for `SharedArrayBuffer` (used by pthreads). A regular HTTP server will not work.

Use the included `coi-server.py`:

```bash
python3 coi-server.py out/web/release [http_port] [https_port] [--xp3 game.xp3]
```

The server starts two services simultaneously:
- **HTTP** on port 8080 (default) — for `localhost` debugging
- **HTTPS** on port 8443 (default) — for LAN access from other devices

Then open `http://localhost:8080/krkr2.html` in your browser.

#### Serving a Game File Directly

Use the `--xp3` flag to have the server host a local `.xp3` file:

```bash
python3 coi-server.py out/web/release --xp3 /path/to/game/data.xp3
```

The file is served at `/data.xp3`, and the printed URL includes the `?xp3=` query parameter. Opening that URL will automatically download and start the game without manual file selection.

You can also manually specify any accessible `.xp3` URL via the query parameter:

```
http://localhost:8080/krkr2.html?xp3=/data.xp3
```

> **Note**: `SharedArrayBuffer` requires either `localhost` or HTTPS. For LAN access, place `server.crt` and `server.key` alongside `coi-server.py` to enable the HTTPS service. Generate a self-signed certificate with:
> ```bash
> openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt -days 365 -nodes
> ```

---

## Code Formatting
- **clang-format@20**
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


---

## Supported Games

* See [games list](./doc/support_games.txt)

---

## Plugin Resources

* Available at [wamsoft GitHub repositories](https://github.com/orgs/wamsoft/repositories?type=all)

---

## License

MIT License. See [LICENSE](./LICENSE) for details.

---
