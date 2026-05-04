# KrKr2 WebAssembly 移植

## 项目目标

本项目的最高目标不是功能等价的 WebAssembly 移植，而是尽可能 100% 一比一复原 Android kirikiroid2 `libkrkr2.so` 的源代码结构、数据流、调用链、对象生命周期、内部容器实现和边界行为。

`libkrkr2.so` 反编译结果是唯一权威来源。本地代码、变量名、现有抽象、Web/Cocos/Emscripten 适配层都不能反向推导原始行为。除明确标注且不可避免的平台边界外，`cpp/` 实现必须优先复刻 `libkrkr2.so` 的架构和中间步骤，而不是追求表面行为一致。

如果在修复某个具体问题的过程中，发现当前代码修改对该问题本身没有直接帮助，但它推进了“尽可能 100% 一比一复原 Android kirikiroid2 `libkrkr2.so` 的源代码结构、数据流、调用链、对象生命周期、内部容器实现和边界行为”这个方向，则该修改不应因为当前问题未被解决而自动撤销。只要该修改有反编译证据支撑、没有引入已知回归，并且让本地实现更接近 `libkrkr2.so`，可以保留它作为架构复原进展。

## 构建
- 调试版：`cmake --preset "Web Debug Config"` → `cmake --build out/web/debug`
- 发布版：`cmake --preset "Web Release Config"` → `cmake --build out/web/release`
- 依赖：emsdk 已 source、VCPKG_ROOT 已设置、ninja、cmake 3.31.1+、bison 3.8.2+
- 输出：`out/web/{debug,release}/` → index.html, index.js, index.wasm, index.data, index.worker.js
- 环境变量：见 `.claude.local.md`（机器特定的 EMSDK/VCPKG_ROOT 路径）

### 构建陷阱
- 必须导出 EMSDK_PYTHON — vcpkg ffmpeg 构建需要（系统 Python 缺少 `match` 语法）
- 改 CMakeLists.txt（增删改文件）后必须重跑 `cmake --preset` 再构建
- bison 报错"require 3.8.2 but have 2.3"时加 `-DBISON_EXECUTABLE=/opt/homebrew/opt/bison/bin/bison`
- 构建前必须关闭 coi-server — 否则提供旧 wasm

## 项目结构
- `cpp/plugins/` — NCB 插件 DLL（每个文件 = 一个虚拟 .dll 模块）
  - `PackinOne.cpp` — 批量加载器，`Plugins.link("PackinOne.dll")` 时加载 8 个子插件
  - `DrawDeviceD3D.cpp` — iTVPDrawDevice 封装（Web 构建的 D3D 桩实现）
- `cpp/plugins/motionplayer/` — EmotePlayer + Player (MotionPlayer)，带 NCB TJS2 绑定，详见各文件头注释
- `cpp/core/tjs2/` — TJS2 脚本引擎核心
- `cpp/core/visual/WindowIntf.cpp` — Window 类：drawDevice setter 要求 `interface` 属性返回 iTVPDrawDevice*
- `cpp/core/plugin/PluginImpl.cpp` — TVPLoadPlugin（由 Plugins.link 调用）、TVPLoadInternalPlugins（启动时）
- `cpp/core/base/StorageIntf.cpp` — 自动路径表、TVPAddAutoPath、TVPGetPlacedPath
- `cpp/core/environ/web/Platform.cpp` — Web 平台启动逻辑，自动挂载 ZIP 中的同级 xp3 文件
- `tests/unit-tests/plugins/motionplayer-dll.cpp` — MotionPlayer/EmotePlayer 单元测试

## 代码模式
- TJS2 属性绑定：`NCB_PROPERTY(name, getter, setter)`、`NCB_PROPERTY_RO(name, getter)`
- TJS2 方法绑定：`NCB_METHOD(name)`、`NCB_METHOD_RAW_CALLBACK(name, &Class::func, flags)`
- 桩模式：`#define STUB_WARN(name) LOGGER->warn("ClassName::" #name "() stub called")`
- 字符串转换：`detail::narrow(ttstr)` → std::string、`detail::widen(std::string)` → ttstr

## 调试工具
- XP3 解包：`tools/bin/mac/rel/xp3 -o /tmp/out file.xp3`
- TJS2 字节码反汇编：`tools/bin/mac/rel/tjsdump file.tjs`（使用 `/tjs2-disasm` 技能）
- 构建原生工具：`cmake --preset "MacOS Release Config" -DBUILD_TOOLS=ON -DBISON_EXECUTABLE=/opt/homebrew/opt/bison/bin/bison && cmake --build out/macos/release --target tjsdump`

## 调试注意事项
- 不要用单独的 XP3 文件测试 — 不完整的 XP3 集合会导致初始化失败，掩盖真正的 bug
- 浏览器自动化：使用 `playwright-cli` 技能。游戏用触摸事件处理左键点击；用 CDP `Input.dispatchTouchEvent` 或确保 onMouseDownEvent 中使用 BUTTON_LEFT
- C++ 日志（`spdlog`/`printf`/`fprintf(stderr)`）均输出到浏览器控制台
- **playwright-cli `console` 命令只保留最近约 200 条日志，WASM 引擎每秒产生数百条，绝大部分会丢失**。必须用 `addInitScript` 注入捕获脚本：先 `open` 空白页 → `run-code` 注入 `addInitScript` 过滤+收集日志 → 再 `goto` 目标页面 → 用 `eval` 取回 `window._filteredLogs`。详见 krkr2-debug skill
- URL 参数：`?xp3=file.xp3` 加载单个 XP3，`?game=file.zip` 加载 ZIP 包。注意不要混用

## IDA MCP 逆向工程

### 核心原则
- 无 Android kirikiroid2 源代码，仅有 libkrkr2.so 二进制。所有逆向使用 IDA MCP
- libkrkr2.so 与本地代码并非一一对应。始终以反编译结果为权威来源，本地代码可能有误或不完整
- **完全对齐架构，不接受功能等价** — 必须复刻 libkrkr2.so 的代码架构和内部实现（如 TJS dispatch 包装、TJS Array 管理），不能用 C++ 简化替代（如 shared_ptr、std::vector）即使行为结果相同

### 已有分析成果
- `analysis/` 目录包含详细逆向文档 — 分析新函数前先检查是否已有记录

### IDA 工具注意事项
- `mcp__ida-pro-mcp__decompile` 配合函数地址获取伪代码
- `mcp__ida-pro-mcp__find` 配合 type "string" 定位字符串引用，但仅匹配 ASCII/UTF-8 — UTF-16 用 `/ida-search-string` 技能
- IDA 可能只显示 UTF-16 字符串首字符（如 "f" 代表 "fstat.dll"）— 用十六进制转储或 `get_operand_value` 解析
- IDA 经常将 UTF-16LE 字符串误标为 ASCII（如 `"z"` 实际是 `"zx"`）。原因：UTF-16LE 的 `7A 00 78 00` 被 IDA 在 `7A 00` 处截断为 ASCII `"z"`。libkrkr2.so 中所有传给 `iTJSDispatch2::PropGet` 的 key 都是 `tjs_char*`=UTF-16LE，因此**反编译中出现的单字符字符串常量都应怀疑是截断的 UTF-16LE**。遇到时用 `get_bytes(addr, 16)` 确认真实内容，然后通过 `set_type` 逐个修复 IDA 标注
- IDA 有时合并独立函数 — 检查 `loc_` 地址处是否有 `SUB SP` 函数序言
- NCB 类注册函数：查找 `ncb_addMember` (0x54242C) 和 `ncb_addConstant` (0x52FA58) 调用
- 已重命名函数完整列表见 `.claude/skills/ida-decompile/SKILL.md` "Named Functions" 表
- NCB 模块加载（`LoadModule`）不区分大小写（加载前转小写）

## 工作流 — 代码修改前置条件（BLOCKING）

任何对 cpp/ 目录的代码修改（Edit/Write），**必须**满足以下全部条件，缺一不可。不满足条件的修改视为无效，必须回退。

### 前置检查清单
1. **libkrkr2.so 函数地址** — 本次修改对齐的是哪个函数（例：sub_692AB0 at 0x692AB0）
2. **反编译证据** — 本次对话中必须有对该函数的 `mcp__ida-pro-mcp__decompile` 调用记录
3. **关键逻辑摘要** — 用伪代码写出 libkrkr2.so 的实际行为（不超过10行），包括所有条件分支和默认值
4. **本地实现对照** — 逐行说明本地代码如何复刻上述伪代码

### 硬性禁止（违反任何一条 = 立即停止并反编译）
- **禁止从 PSB 键名推导行为** — 必须反编译确认读取条件（如 mask 位掩码门控）、默认值、数据类型
- **禁止从变量名推导语义** — 必须反编译确认 libkrkr2.so 实际使用的字符串常量
- **禁止"先改代码再验证"** — 必须"先反编译 → 写伪代码 → 再改本地代码"
- **禁止把多个推测链接成结论** — 每一步都必须有独立的反编译/运行时日志证据
- **禁止从本地代码推断 libkrkr2.so 行为** — 本地代码可能是错的
- **禁止在架构不一致的基础上打补丁** — 当修复需要 workaround 架构差异时（如本地代码缺少 libkrkr2.so 中存在的计算步骤、或存在 libkrkr2.so 中不存在的计算），必须先重构代码使数据流和计算步骤与 libkrkr2.so 反编译伪代码一一对应（同样的输入→同样的中间变量→同样的计算顺序→同样的输出），再进行修复。打补丁只会引入新 bug

### 标准工作流程
1. 发现问题 → 加诊断日志确认现象
2. 反编译 libkrkr2.so 对应函数 → 写出伪代码
3. 对比本地代码与伪代码 → 找到精确差异
4. 修改本地代码精确复刻伪代码 → 在注释中引用函数地址
5. 构建验证 → 运行时诊断确认修复

### 渲染/定位问题专项
- 修复前必须 trace 完整坐标链（PSB → ownerLayer → primaryLayer → paintBox → screen），每层有独立 transform
- 反编译完整渲染链（Layer→DrawDevice→Texture→Cocos2D），不要只看局部

### IDA 反编译质量改善（手动逐个修正）
反编译后如果发现以下问题，**当场修正**，不要留到以后。每次分析函数顺手修几个，IDB 质量持续提升。

#### UTF-16LE 字符串修正
发现截断的单字符字符串时：
1. `get_bytes(addr, 32)` 确认真实 UTF-16LE 内容
2. `set_type(addr, "tjs_char")` 或 `set_type(addr, "wchar16")` 修正类型标注
3. 重新 `decompile` 确认反编译输出已更新

#### 类型信息丰富
- `declare_type` — 把本地代码中的 C++ struct/class 定义导入 IDA（如 EmotePlayer、tTVPRect、iTJSDispatch2）
- `set_type` — 给函数签名设正确的参数和返回类型（如 `void __fastcall fn(EmotePlayer *this, int index)`）
- `infer_types` — 修正关键函数类型后调用，让 IDA 沿调用链自动传播类型
- 导入类型的优先级：高频基础类（iTJSDispatch2、tTJSVariant）> 当前分析的目标类 > 其余

#### 函数/变量重命名
- `rename` — 给 `sub_XXXX` 重命名为 `ClassName_MethodName`（命名规范见下方"IDA 符号管理"）
- 局部变量无法持久重命名时，用 `set_comments` 在函数头部标注关键变量含义

#### 修正后保存
- 一轮分析结束后调用 `idb_save` 持久化所有修正

### IDA 符号管理
- **重命名必须以本地代码为依据** — 必须先 grep 本地项目找到对应的类名::方法名，用 `ClassName_MethodName` 格式重命名。禁止从二进制行为推断命名（如把 `StartProcess` 猜成 `Process`）
- 无法在本地代码中找到对应标识符时，加 `_guess` 后缀（如 `Layer_Update_guess`）
