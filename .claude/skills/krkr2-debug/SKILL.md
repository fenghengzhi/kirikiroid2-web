---
name: krkr2-debug
description: 指导 KrKr2 WebAssembly 完整调试工作流，从构建到浏览器。当用户需要调试、测试或排查运行时环境的端到端问题时使用。
---

# KrKr2 调试工作流

## 编译

参见 [krkr2-build skill](../krkr2-build/SKILL.md)。

## 运行服务器

参见 [krkr2-server skill](../krkr2-server/SKILL.md)。

## 浏览器自动化调试

服务器启动后，除了让用户手动访问页面外，也可以使用 `playwright-cli` 进行自动化调试。参考 [playwright-cli skill](../playwright-cli/SKILL.md)。

### URL 参数

- `?game=xxx.zip` — 加载 ZIP 打包的游戏（ZIP 内含 XP3 文件）
- `?xp3=xxx.xp3` — 直接加载单个 XP3 文件（需先复制到 `out/web/debug/` 目录）
- `?game=xxx.zip&entry=data.xp3` — 加载 ZIP 并指定入口 XP3

### 日志捕获（重要）

**playwright-cli 的 `console` 命令只保留最近约 200 条日志**，WASM 引擎运行时每秒可产生数百条日志，早期的初始化日志会被后续高频日志挤出。`console` 命令获取的日志**严重不完整**，不能依赖它来诊断问题。

正确做法：使用 `addInitScript` 在页面加载前注入日志捕获脚本，按需过滤高频重复消息。

```bash
# 1. 打开空白浏览器（不要直接 open URL）
playwright-cli open

# 2. 注入日志捕获脚本（在页面加载前执行）
playwright-cli run-code "async page => {
  await page.context().addInitScript(() => {
    window._allLogCount = 0;
    window._filteredLogs = [];
    const orig = console.log;
    const origW = console.warn;
    const origE = console.error;
    const cap = (lvl, args) => {
      window._allLogCount++;
      const msg = args.map(a => typeof a === 'string' ? a : String(a)).join(' ');
      // 过滤掉高频重复消息，按需调整过滤条件
      if (!msg.includes('isExistentStorage') &&
          !msg.includes('UpdateToDrawDevice') &&
          !msg.includes('InternalComplete2') &&
          !msg.includes('DrawCompleted') &&
          !msg.includes('BasicDrawDevice::Show') &&
          !msg.includes('_TVPDeliverContinuousEvent') &&
          !msg.includes('DrawDevice::Update')) {
        window._filteredLogs.push('[' + lvl + '] ' + msg);
      }
    };
    console.log = function() { cap('LOG', [...arguments]); orig.apply(console, arguments); };
    console.warn = function() { cap('WARN', [...arguments]); origW.apply(console, arguments); };
    console.error = function() { cap('ERR', [...arguments]); origE.apply(console, arguments); };
  });
}"

# 3. 再导航到目标页面
playwright-cli goto "http://localhost:8080/index.html?xp3=data.xp3"

# 4. 等待一段时间后取回日志
# 查看总数和过滤后数量
playwright-cli eval "JSON.stringify({total: window._allLogCount, filtered: window._filteredLogs.length})"
# 分批取回过滤后的日志（每次取 80 条左右避免超长）
playwright-cli eval "JSON.stringify(window._filteredLogs.slice(0, 80))"
playwright-cli eval "JSON.stringify(window._filteredLogs.slice(80, 160))"
# 或者只取不重复的关键日志
playwright-cli run-code "async page => {
  const logs = await page.evaluate(() => {
    return window._filteredLogs.filter(l =>
      !l.includes('某个高频但不重要的消息')
    );
  });
  return JSON.stringify(logs);
}"
```

**关键原则：**
- 永远**先 open 空白页 → 注入 addInitScript → 再 goto 目标页面**，否则初始化日志会丢失
- addInitScript 的过滤条件根据场景调整，比如调试渲染问题时过滤掉 storage 查询日志，调试 storage 时过滤掉渲染日志
- `playwright-cli console` 只作为**快速浏览**用途，**不能作为诊断依据**

### 典型调试流程

```bash
# 复制游戏文件到构建输出目录
cp /path/to/game.xp3 out/web/debug/

# 用 headed 模式打开（WebGL 截图需要）
playwright-cli open --browser=chrome --headed

# 注入日志捕获
playwright-cli run-code "async page => {
  await page.context().addInitScript(() => {
    window._logs=[];
    const o=console.log, w=console.warn, e=console.error;
    const c=(l,a)=>{const m=a.map(x=>typeof x==='string'?x:String(x)).join(' ');window._logs.push('['+l+'] '+m);};
    console.log=function(){c('L',[...arguments]);o.apply(console,arguments);};
    console.warn=function(){c('W',[...arguments]);w.apply(console,arguments);};
    console.error=function(){c('E',[...arguments]);e.apply(console,arguments);};
  });
}"

# 导航到目标页面
playwright-cli goto "http://localhost:8080/index.html?xp3=game.xp3"

# 等待加载后截图
sleep 3
playwright-cli screenshot --filename=/tmp/debug.png

# 取回日志（用 run-code 避免 eval 的 TypeError）
playwright-cli run-code "async page => {
  return JSON.stringify(await page.evaluate(() => ({
    total: window._logs.length,
    logs: window._logs.slice(0, 80)
  })));
}"

# 关闭
playwright-cli close
```
