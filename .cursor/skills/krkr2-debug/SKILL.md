---
name: krkr2-debug
description: Guides building and running the KrKr2 WebAssembly project for local debugging and testing. Use when the user needs to compile the project, start the development server, load game files, or troubleshoot the runtime environment.
---

# KrKr2 调试工作流

## 编译

### 前置条件

确保以下环境变量已设置：

```bash
export VCPKG_ROOT=/path/to/vcpkg
source /path/to/emsdk/emsdk_env.sh
```

依赖工具：Emscripten SDK、vcpkg、ninja、cmake 3.31.1+、bison 3.8.2+、python3。

### 构建命令

调试时使用 Debug 构建：

```bash
cmake --preset "Web Debug Config"
cmake --build out/web/debug
```

产物在 `out/web/debug/` 下：`index.html`、`index.js`、`index.wasm`、`index.data`、`index.worker.js`。

发布时使用 Release 构建：

```bash
cmake --preset "Web Release Config"
cmake --build out/web/release
```

## 运行

Web 版本需要跨域隔离响应头（COOP / COEP）以支持 `SharedArrayBuffer`，普通 HTTP 服务器无法正常运行。使用项目自带的 `coi-server.py`：

```bash
python3 coi-server.py out/web/debug [http端口] [https端口] [选项]
```

默认启动：
- **HTTP** 端口 8080 — 用于 `localhost` 本地调试
- **HTTPS** 端口 8443 — 用于局域网设备访问

打开 `http://localhost:8080/index.html` 进入页面。

## 加载游戏文件

### 单个 .xp3 文件

```bash
python3 coi-server.py out/web/debug --xp3 /path/to/game/data.xp3
```

服务器映射文件到 `/data.xp3`，输出含 `?xp3=` 参数的完整 URL，打开即自动加载。

### ZIP 压缩包

```bash
python3 coi-server.py out/web/debug --zip /path/to/game.zip
```

多个 `.xp3` 时用 `--entry` 指定：

```bash
python3 coi-server.py out/web/debug --zip /path/to/game.zip --entry data.xp3
```

### URL 查询参数

| 参数 | 说明 |
|------|------|
| `?xp3=<url>` | 从 URL 加载单个 `.xp3` 文件 |
| `?game=<url>` | 从 URL 下载解压 `.zip` 包 |
| `?entry=<name>` | 配合 `game` 使用，自动选择指定 `.xp3` |

## 浏览器自动化调试

服务器启动后，除了让用户手动访问页面外，也可以使用 `playwright-cli` 进行自动化调试。参考 [playwright-cli skill](../../../.claude/skills/playwright-cli/SKILL.md)。

典型流程：

```bash
playwright-cli open http://localhost:8080/index.html
playwright-cli snapshot
playwright-cli console
playwright-cli screenshot --filename=debug.png
playwright-cli close
```

## 局域网 HTTPS 访问

`SharedArrayBuffer` 要求 `localhost` 或 HTTPS。局域网访问需将证书放在 `coi-server.py` 同级目录：

```bash
openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt -days 365 -nodes
```
