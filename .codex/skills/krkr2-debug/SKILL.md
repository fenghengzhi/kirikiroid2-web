---
name: krkr2-debug
description: 指导 KrKr2 WebAssembly 的完整调试工作流，从构建、服务器到 playwright-cli 浏览器自动化。当用户需要调试、测试或排查运行时环境的端到端问题时使用。
---

# KrKr2 调试工作流

## 编译

参见 [krkr2-build skill](../krkr2-build/SKILL.md)。

## 运行服务器

参见 [krkr2-server skill](../krkr2-server/SKILL.md)。

## 浏览器自动化调试

服务器启动后，统一使用 `playwright-cli` 控制浏览器。开始操作前完整读取 [playwright-cli skill](../playwright-cli/SKILL.md)，并先确认命令可用：

```bash
playwright-cli --version
```

如果只有项目本地版本，先用 `npx --no-install playwright --version` 确认，再把下文命令中的 `playwright-cli` 替换为 `npx playwright cli`。

同一调试任务固定复用具名 session `krkr2`，避免命令连接到错误的浏览器实例：

```bash
playwright-cli -s=krkr2 open
playwright-cli list
```

session 失效时重新 `open`；任务结束后执行 `playwright-cli -s=krkr2 close`。

### URL 参数

- `?game=xxx.zip` — 加载 ZIP 打包的游戏（ZIP 内含 XP3 文件）
- `?xp3=xxx.xp3` — 直接加载单个 XP3 文件（需先复制到 `out/web/debug/` 目录）
- `?game=xxx.zip&entry=data.xp3` — 加载 ZIP 并指定入口 XP3

不要混用 `game` 和 `xp3`。

### 日志捕获（重要）

WASM 引擎每秒可能产生数百条控制台日志。浏览器提供的近期 console 视图可能遗漏初始化阶段日志，不能单独作为诊断依据。

正确流程：

1. 用 `playwright-cli -s=krkr2 open` 创建空白页面。
2. 在导航前用 `run-code` 调用 `page.addInitScript` 安装日志捕获脚本。
3. 用 `goto` 导航到目标 KrKr2 URL。
4. 用 `eval` 分批读取 `window._filteredLogs`。
5. 用 `snapshot` 和 `screenshot` 检查可见页面状态；输入问题还要验证浏览器事件是否抵达页面。

导航前执行：

```bash
playwright-cli -s=krkr2 run-code "async page => {
  await page.addInitScript(() => {
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
        .map(value => typeof value === 'string' ? value : String(value))
        .join(' ');

      if (!message.includes('isExistentStorage') &&
          !message.includes('UpdateToDrawDevice') &&
          !message.includes('InternalComplete2') &&
          !message.includes('DrawCompleted') &&
          !message.includes('BasicDrawDevice::Show') &&
          !message.includes('_TVPDeliverContinuousEvent') &&
          !message.includes('DrawDevice::Update')) {
        window._filteredLogs.push('[' + level + '] ' + message);
      }
    };

    console.log = function(...args) {
      capture('LOG', args);
      original.log.apply(console, args);
    };
    console.warn = function(...args) {
      capture('WARN', args);
      original.warn.apply(console, args);
    };
    console.error = function(...args) {
      capture('ERR', args);
      original.error.apply(console, args);
    };
  });
}"

playwright-cli -s=krkr2 goto "http://localhost:端口/index.html?game=game.zip"
```

过滤条件必须根据问题调整：调试 storage 时不要过滤 storage 日志；调试渲染时可过滤高频绘制日志。日志应分批取回，避免一次返回过长内容。

```bash
playwright-cli -s=krkr2 --raw eval "JSON.stringify({all: window._allLogCount, pending: window._filteredLogs.length})"
playwright-cli -s=krkr2 --raw eval "JSON.stringify(window._filteredLogs.splice(0, 200))"
```

### 输入事件调试

测试游戏输入时优先使用 `click`，或在一次 `run-code` 中完成 `page.mouse.click(x, y)`。不要把触摸 down/up 拆成多个 CLI 调用；需要触摸上下文时，用 `open --mobile` 或 `open --device=...` 创建新 session。

判断输入是否进入引擎前，先在页面上安装 capture listener，分别统计 `pointerdown`、`pointerup`、`mousedown`、`mouseup` 和 `click`。只有浏览器层事件计数正常后，才继续排查 KrKr2 的 Window、DrawDevice 和脚本事件链。

```bash
playwright-cli -s=krkr2 run-code "async page => {
  await page.evaluate(() => {
    if (window._krkrInputCaptureInstalled) return;
    window._krkrInputCaptureInstalled = true;
    const types = ['pointerdown', 'pointerup', 'mousedown', 'mouseup', 'click'];
    window._inputEventCounts = Object.fromEntries(types.map(type => [type, 0]));
    for (const type of types) {
      window.addEventListener(type, () => window._inputEventCounts[type]++, true);
    }
  });
}"

playwright-cli -s=krkr2 snapshot
playwright-cli -s=krkr2 click e15
# canvas 无可用 ref 时，在一次调用中完成坐标点击
playwright-cli -s=krkr2 run-code "async page => { await page.mouse.click(400, 300); }"
playwright-cli -s=krkr2 --raw eval "JSON.stringify(window._inputEventCounts)"
```

### 典型调试流程

1. 将完整游戏 ZIP 或所需 XP3 放到构建输出目录；不要用不完整的单独 XP3 集合测试游戏初始化。
2. 使用 `krkr2-server` 启动带跨域隔离响应头的服务器。
3. 完整读取 `playwright-cli` skill，确认命令可用并打开具名 session `krkr2`。
4. 在空白页面通过 `page.addInitScript` 安装日志捕获脚本。
5. 用 `goto` 导航到 `http://localhost:端口/index.html?...`。
6. 用 `snapshot`、`screenshot` 和页面状态断言确认已达到目标状态。
7. 用 `eval` 分批读取关键日志；需要输入时发送鼠标点击并检查 capture 计数器。
8. 将 CLI 自动化层、WASM 日志和引擎调用链证据分开记录，避免把自动化问题误判为引擎回归。
9. 调试结束后关闭 `krkr2` session。
