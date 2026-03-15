# KrKr2 Web

基于 WebAssembly 的 **KiriKiri2 引擎**（T Visual Presenter）移植，让吉里吉里引擎游戏直接在浏览器中运行。

> 这是一个专注于 Web 平台的个人分支，不接受 Pull Request。

**语言 / Language**: 中文 | [English](README.md)

---

## 支持浏览器

Chrome、Edge、Firefox、Safari（任何支持 WebAssembly + SharedArrayBuffer 的现代浏览器）。

---

## 编译

### 依赖工具

- [Emscripten SDK (emsdk)](https://emscripten.org/docs/getting_started/downloads.html)
- [vcpkg](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started)
- [ninja](https://github.com/ninja-build/ninja/releases)
- [cmake 3.31.1+](https://cmake.org/download/)
- `bison 3.8.2+`
- `python3`

### 环境变量

```bash
export VCPKG_ROOT=/path/to/vcpkg
source /path/to/emsdk/emsdk_env.sh   # 自动设置 EMSDK
```

### 编译步骤

```bash
cmake --preset "Web Release Config"
cmake --build out/web/release
```

编译 Debug 版本，将 `Release` 替换为 `Debug`：

```bash
cmake --preset "Web Debug Config"
cmake --build out/web/debug
```

### 产物位置

```
out/web/release/
  krkr2.html
  krkr2.js
  krkr2.wasm
  krkr2.data
  krkr2.worker.js
```

---

## 运行

Web 版本需要[跨域隔离](https://web.dev/cross-origin-isolation-guide/)响应头（`COOP` / `COEP`）以支持 `SharedArrayBuffer`。普通 HTTP 服务器无法正常运行。

使用项目自带的 `coi-server.py`：

```bash
python3 coi-server.py out/web/release [http端口] [https端口] [--xp3 游戏文件.xp3]
```

服务器同时启动：
- **HTTP** 端口 8080（默认）— 用于 `localhost` 本地调试
- **HTTPS** 端口 8443（默认）— 用于局域网内其他设备访问

然后在浏览器中打开 `http://localhost:8080/krkr2.html`。

### 直接指定游戏文件

通过 `--xp3` 参数让服务器托管本地 `.xp3` 文件：

```bash
python3 coi-server.py out/web/release --xp3 /path/to/game/data.xp3
```

服务器会将该文件映射到 `/data.xp3` 路径，并输出包含 `?xp3=` 参数的完整 URL。打开该 URL 后，网页会自动下载并启动游戏，无需手动选择文件。

也可以通过 URL 参数手动指定任意可访问的 `.xp3` 文件地址：

```
http://localhost:8080/krkr2.html?xp3=/data.xp3
```

### 局域网 HTTPS 访问

`SharedArrayBuffer` 要求 `localhost` 或 HTTPS 环境。如需局域网访问，将 `server.crt` 和 `server.key` 放在 `coi-server.py` 同级目录：

```bash
openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt -days 365 -nodes
```

---

## 支持的游戏列表

见 [games list](./doc/support_games.txt)。

---

## 许可证

MIT 许可证。详见 [LICENSE](./LICENSE)。
