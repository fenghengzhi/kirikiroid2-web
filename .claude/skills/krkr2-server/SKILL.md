---
name: krkr2-server
description: 启动带跨域隔离响应头的 KrKr2 开发服务器，用于 WebAssembly 调试。当用户需要启动 coi-server、提供构建输出服务、加载 XP3 或 ZIP 游戏文件、或从局域网设备访问时使用。
---

# KrKr2 开发服务器

## 启动

Web 版本需要跨域隔离响应头（COOP / COEP）以支持 `SharedArrayBuffer`，普通 HTTP 服务器无法正常运行。使用项目自带的 `coi-server.py`：

下文 `<python>` 表示当前机器可用的 Python 3 解释器：Windows 通常是 `python`，POSIX 环境通常是 `python3`。

```bash
<python> coi-server.py out/web/debug [http端口] [https端口] [选项]
```

默认端口：
- **HTTP** 8080 — 总是启动，用于 `localhost` 本地调试
- **HTTPS** 8443 — 仅当 `coi-server.py` 同级存在 `server.crt` 和 `server.key` 时启动，用于局域网设备访问；没有证书时脚本会明确输出 `HTTPS not enabled`

打开 `http://localhost:8080/index.html` 进入页面。

## 加载游戏文件

### 单个 .xp3 文件

```bash
<python> coi-server.py out/web/debug --xp3 /path/to/game/data.xp3
```

服务器映射文件到 `/data.xp3`，输出含 `?xp3=` 参数的完整 URL，打开即自动加载。

### ZIP 压缩包

```bash
<python> coi-server.py out/web/debug --zip /path/to/game.zip
```

多个 `.xp3` 时用 `--entry` 指定：

```bash
<python> coi-server.py out/web/debug --zip /path/to/game.zip --entry data.xp3
```

### URL 查询参数

| 参数 | 说明 |
|------|------|
| `?xp3=<url>` | 从 URL 加载单个 `.xp3` 文件 |
| `?game=<url>` | 从 URL 下载解压 `.zip` 包 |
| `?entry=<name>` | 配合 `game` 使用，自动选择指定 `.xp3` |

## 局域网 HTTPS 访问

`SharedArrayBuffer` 要求 `localhost` 或 HTTPS。局域网访问需将证书放在 `coi-server.py` 同级目录：

```bash
openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt -days 365 -nodes
```
