---
name: playwright-cli
description: 自动化浏览器交互，用于 Web 测试、表单填写、截图和数据提取。当用户需要浏览网页、与页面交互、填写表单、截图、测试 Web 应用或从网页提取信息时使用。
allowed-tools: Bash(playwright-cli:*)
---

# 使用 playwright-cli 进行浏览器自动化

## 快速入门

```bash
# 打开新浏览器
playwright-cli open
# 导航到页面
playwright-cli goto https://playwright.dev
# 使用快照中的 ref 与页面交互
playwright-cli click e15
playwright-cli type "page.click"
playwright-cli press Enter
# 截图（较少使用，快照更常用）
playwright-cli screenshot
# 关闭浏览器
playwright-cli close
```

## 命令

### 核心

```bash
playwright-cli open
# 打开并直接导航
playwright-cli open https://example.com/
playwright-cli goto https://playwright.dev
playwright-cli type "search query"
playwright-cli click e3
playwright-cli dblclick e7
playwright-cli fill e5 "user@example.com"
playwright-cli drag e2 e8
playwright-cli hover e4
playwright-cli select e9 "option-value"
playwright-cli upload ./document.pdf
playwright-cli check e12
playwright-cli uncheck e12
playwright-cli snapshot
playwright-cli snapshot --filename=after-click.yaml
playwright-cli eval "document.title"
playwright-cli eval "el => el.textContent" e5
playwright-cli dialog-accept
playwright-cli dialog-accept "confirmation text"
playwright-cli dialog-dismiss
playwright-cli resize 1920 1080
playwright-cli close
```

### 导航

```bash
playwright-cli go-back
playwright-cli go-forward
playwright-cli reload
```

### 键盘

```bash
playwright-cli press Enter
playwright-cli press ArrowDown
playwright-cli keydown Shift
playwright-cli keyup Shift
```

### 鼠标

```bash
playwright-cli mousemove 150 300
playwright-cli mousedown
playwright-cli mousedown right
playwright-cli mouseup
playwright-cli mouseup right
playwright-cli mousewheel 0 100
```

### 保存为

```bash
playwright-cli screenshot
playwright-cli screenshot e5
playwright-cli screenshot --filename=page.png
playwright-cli pdf --filename=page.pdf
```

### 标签页

```bash
playwright-cli tab-list
playwright-cli tab-new
playwright-cli tab-new https://example.com/page
playwright-cli tab-close
playwright-cli tab-close 2
playwright-cli tab-select 0
```

### 存储

```bash
playwright-cli state-save
playwright-cli state-save auth.json
playwright-cli state-load auth.json

# Cookies
playwright-cli cookie-list
playwright-cli cookie-list --domain=example.com
playwright-cli cookie-get session_id
playwright-cli cookie-set session_id abc123
playwright-cli cookie-set session_id abc123 --domain=example.com --httpOnly --secure
playwright-cli cookie-delete session_id
playwright-cli cookie-clear

# LocalStorage
playwright-cli localstorage-list
playwright-cli localstorage-get theme
playwright-cli localstorage-set theme dark
playwright-cli localstorage-delete theme
playwright-cli localstorage-clear

# SessionStorage
playwright-cli sessionstorage-list
playwright-cli sessionstorage-get step
playwright-cli sessionstorage-set step 3
playwright-cli sessionstorage-delete step
playwright-cli sessionstorage-clear
```

### 网络

```bash
playwright-cli route "**/*.jpg" --status=404
playwright-cli route "https://api.example.com/**" --body='{"mock": true}'
playwright-cli route-list
playwright-cli unroute "**/*.jpg"
playwright-cli unroute
```

### 开发者工具

```bash
playwright-cli console
playwright-cli console warning
playwright-cli network
playwright-cli run-code "async page => await page.context().grantPermissions(['geolocation'])"
playwright-cli tracing-start
playwright-cli tracing-stop
playwright-cli video-start
playwright-cli video-stop video.webm
```

## 打开参数
```bash
# 创建会话时使用指定浏览器
playwright-cli open --browser=chrome
playwright-cli open --browser=firefox
playwright-cli open --browser=webkit
playwright-cli open --browser=msedge
# 通过扩展连接浏览器
playwright-cli open --extension

# 使用持久化配置文件（默认配置文件在内存中）
playwright-cli open --persistent
# 使用自定义目录的持久化配置文件
playwright-cli open --profile=/path/to/profile

# 使用配置文件启动
playwright-cli open --config=my-config.json

# 关闭浏览器
playwright-cli close
# 删除默认会话的用户数据
playwright-cli delete-data
```

## 快照

每条命令执行后，playwright-cli 会提供当前浏览器状态的快照。

```bash
> playwright-cli goto https://example.com
### 页面
- 页面 URL: https://example.com/
- 页面标题: Example Domain
### 快照
[Snapshot](.playwright-cli/page-2026-02-14T19-22-42-679Z.yml)
```

也可以使用 `playwright-cli snapshot` 命令按需获取快照。

如果未提供 `--filename`，会使用时间戳创建新快照文件。默认使用自动命名，当快照是工作流结果的一部分时使用 `--filename=`。

## 浏览器会话

```bash
# 创建名为 "mysession" 的新浏览器会话，使用持久化配置
playwright-cli -s=mysession open example.com --persistent
# 手动指定配置文件目录（仅在明确要求时使用）
playwright-cli -s=mysession open example.com --profile=/path/to/profile
playwright-cli -s=mysession click e6
playwright-cli -s=mysession close  # 停止命名浏览器
playwright-cli -s=mysession delete-data  # 删除持久化会话的用户数据

playwright-cli list
# 关闭所有浏览器
playwright-cli close-all
# 强制终止所有浏览器进程
playwright-cli kill-all
```

## 本地安装

某些情况下用户可能需要在本地安装 playwright-cli。如果运行全局 `playwright-cli` 二进制文件失败，使用 `npx playwright-cli` 运行命令。例如：

```bash
npx playwright-cli open https://example.com
npx playwright-cli click e1
```

## 示例：表单提交

```bash
playwright-cli open https://example.com/form
playwright-cli snapshot

playwright-cli fill e1 "user@example.com"
playwright-cli fill e2 "password123"
playwright-cli click e3
playwright-cli snapshot
playwright-cli close
```

## 示例：多标签页工作流

```bash
playwright-cli open https://example.com
playwright-cli tab-new https://example.com/other
playwright-cli tab-list
playwright-cli tab-select 0
playwright-cli snapshot
playwright-cli close
```

## 示例：使用开发者工具调试

```bash
playwright-cli open https://example.com
playwright-cli click e4
playwright-cli fill e7 "test"
playwright-cli console
playwright-cli network
playwright-cli close
```

```bash
playwright-cli open https://example.com
playwright-cli tracing-start
playwright-cli click e4
playwright-cli fill e7 "test"
playwright-cli tracing-stop
playwright-cli close
```

## 特定任务

* **请求模拟** [references/request-mocking.md](references/request-mocking.md)
* **运行 Playwright 代码** [references/running-code.md](references/running-code.md)
* **浏览器会话管理** [references/session-management.md](references/session-management.md)
* **存储状态（cookies、localStorage）** [references/storage-state.md](references/storage-state.md)
* **测试生成** [references/test-generation.md](references/test-generation.md)
* **追踪** [references/tracing.md](references/tracing.md)
* **视频录制** [references/video-recording.md](references/video-recording.md)
