---
name: krkr2-debug
description: 指导 Codex 中的 KrKr2 WebAssembly 完整调试工作流，从构建、服务器到应用内浏览器。当用户需要调试、测试或排查运行时环境的端到端问题时使用。
---

# KrKr2 调试工作流（Codex）

## 编译

参见 [krkr2-build skill](../krkr2-build/SKILL.md)。

## 运行服务器

参见 [krkr2-server skill](../krkr2-server/SKILL.md)。

## 浏览器自动化调试

服务器启动后，使用 Codex 的 `browser:control-in-app-browser` skill 控制应用内浏览器。不要使用或假定存在 `playwright-cli`，也不要把 Claude 版 `krkr2-debug` 的 CLI 命令移植到 Codex 工作流。

开始浏览器操作前必须完整读取 `browser:control-in-app-browser` skill，并严格按它完成运行时初始化、浏览器选择以及浏览器自身文档读取。浏览器连接和 tab binding 应在同一调试任务内复用；tab 失效时重新取得 tab，不要重复初始化浏览器运行时。

### URL 参数

- `?game=xxx.zip` — 加载 ZIP 打包的游戏（ZIP 内含 XP3 文件）
- `?xp3=xxx.xp3` — 直接加载单个 XP3 文件（需先复制到 `out/web/debug/` 目录）
- `?game=xxx.zip&entry=data.xp3` — 加载 ZIP 并指定入口 XP3

不要混用 `game` 和 `xp3`。

### 日志捕获（重要）

WASM 引擎每秒可能产生数百条控制台日志。浏览器提供的近期 console 视图可能遗漏初始化阶段日志，不能单独作为诊断依据。

正确流程：

1. 先取得应用内浏览器和一个空白 tab。
2. 按该浏览器完整文档提供的初始化脚本接口，在导航前注入日志捕获脚本。
3. 再导航到目标 KrKr2 URL。
4. 通过 tab 的页面执行接口分批读取 `window._filteredLogs`。
5. 截图并检查可见页面状态；输入问题还要验证浏览器事件是否抵达页面。

注入脚本的核心逻辑如下；具体调用方法必须使用当前浏览器文档中支持的 API，不要包装成 `playwright-cli run-code`：

```js
window._allLogCount = 0;
window._filteredLogs = [];

const original = {
  log: console.log,
  warn: console.warn,
  error: console.error,
};

const capture = (level, args) => {
  window._allLogCount++;
  const message = args
    .map(value => typeof value === "string" ? value : String(value))
    .join(" ");

  if (!message.includes("isExistentStorage") &&
      !message.includes("UpdateToDrawDevice") &&
      !message.includes("InternalComplete2") &&
      !message.includes("DrawCompleted") &&
      !message.includes("BasicDrawDevice::Show") &&
      !message.includes("_TVPDeliverContinuousEvent") &&
      !message.includes("DrawDevice::Update")) {
    window._filteredLogs.push(`[${level}] ${message}`);
  }
};

console.log = function(...args) {
  capture("LOG", args);
  original.log.apply(console, args);
};
console.warn = function(...args) {
  capture("WARN", args);
  original.warn.apply(console, args);
};
console.error = function(...args) {
  capture("ERR", args);
  original.error.apply(console, args);
};
```

过滤条件必须根据问题调整：调试 storage 时不要过滤 storage 日志；调试渲染时可过滤高频绘制日志。日志应分批取回，避免一次返回过长内容。

### 输入事件调试

测试游戏输入时，优先使用浏览器文档支持的鼠标点击接口。不要跨调用拆分触摸 down/up，也不要假定 tab 启用了 touch context。

判断输入是否进入引擎前，先在页面上安装 capture listener，分别统计 `pointerdown`、`pointerup`、`mousedown`、`mouseup` 和 `click`。只有浏览器层事件计数正常后，才继续排查 KrKr2 的 Window、DrawDevice 和脚本事件链。

### 典型调试流程

1. 将完整游戏 ZIP 或所需 XP3 放到构建输出目录；不要用不完整的单独 XP3 集合测试游戏初始化。
2. 使用 `krkr2-server` 启动带跨域隔离响应头的服务器。
3. 按 Browser skill 初始化并选择应用内浏览器，完整读取其文档。
4. 创建空白 tab，并在导航前安装日志捕获脚本。
5. 导航到 `http://localhost:端口/index.html?...`。
6. 等待页面达到目标状态，检查页面可见状态并截图。
7. 分批读取关键日志；需要输入时优先发送鼠标点击并检查 capture 计数器。
8. 将浏览器层、WASM 日志和引擎调用链证据分开记录，避免把浏览器自动化问题误判为引擎回归。
