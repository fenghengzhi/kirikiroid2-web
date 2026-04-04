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

典型流程：

```bash
playwright-cli open http://localhost:8080/index.html?game=game.zip&entry=data.xp3
playwright-cli snapshot
playwright-cli console
playwright-cli screenshot --filename=debug.png
playwright-cli close
```
