---
name: krkr2-debug
description: Guides the full KrKr2 WebAssembly debug workflow from build to browser. Use when the user needs to debug, test, or troubleshoot the runtime environment end-to-end.
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
playwright-cli open http://localhost:8080/index.html
playwright-cli snapshot
playwright-cli console
playwright-cli screenshot --filename=debug.png
playwright-cli close
```
