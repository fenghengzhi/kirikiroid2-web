# KrKr2 WebAssembly 移植

## 交流语言
- 始终用**简体中文**回复用户（代码、标识符、命令、二进制符号名保持原文）。

## 项目目标

本项目的最高目标不是功能等价的 WebAssembly 移植，而是尽可能 100% 一比一复原 Android kirikiroid2 `libkrkr2.so` 的源代码结构、数据流、调用链、对象生命周期、内部容器实现和边界行为。

`libkrkr2.so` 反编译结果是唯一权威来源。本地代码、变量名、现有抽象、Web/Cocos/Emscripten 适配层都不能反向推导原始行为。除明确标注且不可避免的平台边界外，`cpp/` 实现必须优先复刻 `libkrkr2.so` 的架构和中间步骤，而不是追求表面行为一致。

如果在修复某个具体问题的过程中，发现当前代码修改对该问题本身没有直接帮助，但它推进了“尽可能 100% 一比一复原 Android kirikiroid2 `libkrkr2.so` 的源代码结构、数据流、调用链、对象生命周期、内部容器实现和边界行为”这个方向，则该修改不应因为当前问题未被解决而自动撤销。只要该修改有反编译证据支撑、没有引入已知回归，并且让本地实现更接近 `libkrkr2.so`，可以保留它作为架构复原进展。

## 构建
- 调试版：`cmake --preset "Web Debug Config"` → `cmake --build out/web/debug`
- 发布版：`cmake --preset "Web Release Config"` → `cmake --build out/web/release`
- 依赖：emsdk 已 source、VCPKG_ROOT 已设置、ninja、cmake 3.31.1+、bison 3.8.2+
- 输出：`out/web/{debug,release}/` → index.js, index.wasm, index.worker.js, build-config.js, assets.zip（UI 资源 stored-zip；--preload-file/index.data 已移除，游戏与 UI 文件经 VirtualLazyFS 懒加载，见 `cpp/core/environ/web/VirtualLazyFS.h`）。**这里只有引擎，没有页面**
- **前端与 wasm 完全解耦**，是两套独立构建：
  - CMake/emscripten → 引擎（无 `--shell-file`，不烘焙 HTML）
  - `platforms/web/webui`（Vue 3 + Vite）→ 页面，`npm run build` 产出 `webui/dist`，其中的引擎文件由 Vite 插件从 `out/web/release` 取（可用 `KRKR2_ENGINE_DIR` 覆盖）
- **改前端不需要装 emsdk、也不触发 wasm 重链**：`cd platforms/web/webui && npm run dev`。反之改 cpp/ 后重跑 `npm run build` 即可拿到新 wasm
- 引擎调试（单个 xp3，不经游戏库/D1）：先 `npm run build`，再 `python3 coi-server.py platforms/web/webui/dist --xp3 /path/to/data.xp3`，打开它打印的 `/play.html?xp3=...`
- 环境变量：见 `.claude.local.md`（机器特定的 EMSDK/VCPKG_ROOT 路径）

### CI（与上面的解耦一一对应）
- `build-web.yml`（引擎/wasm）与 `build-webui.yml`（前端/Vite）**互不触发**。前者的 `paths-ignore` 排除了 `platforms/web/webui/**`，后者只在该目录变动时跑
- `build-web.yml` 有两条互相独立的触发路径，改过滤规则要**两边都改**：
  - 推 `web` / `dev/*`（日常路径）→ 走它自己的 `push` + `paths-ignore`
  - 推 `main` → `Code Format Check` 跑完经 `workflow_run` 拉起它；而 `workflow_run` **不支持 paths 过滤**，所以这条路上的门在 `code-format-check.yml` 的 `paths`
  - 结论：只改一处都会漏。`code-format-check.yml` 的 `push.branches` 是 `[ main ]`，它在 `web` 上根本不触发，别误以为它是唯一的门
- `tests.yml` / `differential.yml` 同样加了 `paths-ignore`：它们要 emsdk + vcpkg（differential 还要 Android 模拟器 + Frida），纯前端/文档改动不该拉起来
- **默认分支是 `web` 不是 `main`**（origin 与 upstream 皆然，main 停在 706 个提交之前、且没有 `platforms/web/`）。加 workflow 时 `branches:` 别照抄 `code-format-check.yml` 的 `[ main ]` —— 那样 PR 进 web（唯一的实际合并路径）时整个检查不跑
- artifact 只上传引擎六项（index.js / index.wasm[.map] / index.worker.js / build-config.js / assets.zip），页面产物是 `build-webui.yml` 的 `webui-dist`。想要能跑的整站：解开 `web-engine`，然后 `KRKR2_ENGINE_DIR=<解开的目录> npm run build`
- `index.worker.js` 只在 pthread 构建里出现，当前配置不产它 —— artifact 与 Vite 插件都按"缺了不算错"处理

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
- `cpp/core/environ/web/Platform.cpp` — Web 平台启动逻辑
- `cpp/core/environ/web/VirtualLazyFS.{h,cpp}` + `platforms/web/webui/public/vlfs.js` — VirtualLazyFS：游戏/UI 文件懒加载（JSPI 挂起读 + pthread 代理 + OPFS spill + 写 overlay），仅 Chromium 137+
- `platforms/web/webui/` — Web 前端，Vue 3 + Vite + Cloudflare Worker，独立于 CMake 构建（详见 `webui/README.md`）
  - `index.html` / `play.html` / `admin.html` — 三个 MPA 入口。**不是审美选择**：引擎是硬单例（Web Lock + `booted` 标志），wasm runtime 进 `main()` 后无法同页销毁重建，"退出游戏"必须整页卸载
  - `src/gallery|player|admin|shared|styles/` — Vue 侧。播放页支持 `?xp3=`/`?game=`/`?entry=`（引擎调试入口，绕过 D1 游戏库）
  - `worker/` — Cloudflare Worker：路由 + 安全头（COOP/COEP）、PBKDF2 登录、D1 游戏库读写、封面代理
  - `public/` — **引擎层，原样输出不经打包**。这一层与产品前端解耦，整体替换 Vue 部分也不影响它：
    - `js/engine/` — 引擎引导层：`boot-guards`（单例锁/JSPI 检测/WebGL 精度补丁）、`memory`（wasm Memory 预分配）、`fs-util`、`vlfs-bridge`、`engine.js`
    - `js/engine/engine.js` — **`window.KrKr2Engine` facade**：前端与 wasm 的唯一接口（`boot` / `loadSource` / `setSaveSpace` / `setHostDir`）。与 C++ 的契约字段 `Module._startupXp3Path`（被 `Platform.cpp` 经 EM_JS 读取）等在此文件头部有说明
    - `js/loaders/` — 数据源加载器（json-url / xp3-url / zip-url / xp3-file / zip-file / folder / fsa-dir），只注册字节进 VLFS，进度经回调上报
  - `scripts/gen-sw.js` — 构建后生成 `dist/sw.js`（precache 列表必须现算：Vite 产物带内容哈希，手写列表一上线就全 404）
- `platforms/web/gen_build_config.cmake` — 链接后生成 `build-config.js`（从 index.js 读取烘焙的 INITIAL_MEMORY，另带 buildVersion/pwa/localZipPicker/assetBase），取代原先注入 shell.html 的 configure_file 占位符；页面侧默认值兜底见 `webui/public/js/config.js`
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
- 浏览器自动化：使用 `playwright-cli` 技能。**测游戏输入优先用 `page.mouse.click(x,y)`**。CDP `Input.dispatchTouchEvent` 有坑：每次 `run-code` 新建 CDP session，触摸 down/up 必须同一 session 配对，跨调用的悬挂触摸会让浏览器抑制后续全部鼠标事件，表现为"点击无反应"假回归；`page.touchscreen.tap` 需要 context `hasTouch:true` 否则 throw。判断输入是否进引擎：先在 window 挂 capture 计数器确认浏览器层派发
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

### 证据是阻塞项，验证是尽力项（澄清）
- 上面"禁止先改代码再验证"约束的是**顺序**（反编译证据在前），**不是**"没有运行时验证手段就不许实现"。
- **不得仅因"无法验证"而阻塞或 defer 实现**：当目标函数已满足前置检查清单（已 decompile + 写出伪代码 + 本地对照）、但缺运行时验证手段（无 fixture / oracle / 差分覆盖）时，应照常忠实复刻 → 构建通过 → 用**现有**手段尽力补验证（复用现有 fixture / oracle、加诊断日志、必要时给工具补能力如解密 seed / dump 探针读现有资产）。**没有现成物料（fixture / 测试数据）就不实现对应测试，且不要尝试构建物料**——现有 fixture / oracle 覆盖不到该路径时，直接放弃新增测试（绝不从零捏造 fixture / 物料），在注释或 analysis/ 标注验证缺口即可。
- 仍然 BLOCKING 的只有**证据缺失**（未反编译就改、从键名/变量名/本地代码推断、把推测链接成结论、在架构不一致上打补丁）——**不是**验证缺失。
- **oracle-inert（改动对现有差分/运行时不可观察）不是拒绝、defer、降优先级、或"建议改做别的 P0"的理由。** 复原的价值标的是六维架构本身（源码结构 / 数据流 / 调用链 / 对象生命周期 / 内部容器实现 / 边界行为），**没有一维要求"能被现有 logo 差分观察到"**。一段忠实复刻即便对所有现有 fixture 全程 inert（容器拓扑 STL→KiriKiri 替换、变量级联、HM3 等 dead-data 快照、root/var-track 流……），只要有反编译证据且构建不回归，就照常实现并保留；此时 logo 0-mismatch 是**非回归守护**，不是它存在的理由。**禁止把"oracle 看不到"包装成 ROI / 可验证性论据去排挤架构复原工作**，也禁止据此把一个 open P0 判为"低价值"。
- 唯一**合法可停**的两种情况，且都不得用 inert 冒充：(a) **证据缺失**（见上）；(b) **明确标注且不可避免的平台边界**（如本地渲染栈无 per-vertex 顶点色 → 4-corner 颜色必须 bake，纹理只能按 (name,color) 缓存）——必须在代码注释 / analysis 写明边界的**具体技术原因**，"改动 oracle 观察不到"**不算**平台边界。
- **「不存在 / 缺失 / 死字段 / 架构前置缺失」是强断言，下结论前必须独立交叉核实，禁止单凭一次 negative grep 判定。** 宣布"本地缺少某机制 / 某字段从未被使用 / 前置数据流缺失 / 应判平台边界"之前，必须换搜索词、换工具、读调用链、或派独立 agent 复核确认它**确实**不存在。**`grep` 返回空 ≠ 不存在**——尤其注意 shell cwd 漂移（`cd` 失败后 cwd 不变）会让 `*.cpp` 之类相对 glob 静默落空。据一次空搜索就打"architecture-blocked"标签、defer、或写进注释/memory，会把错误固化并误导后续 session。反面教训：本仓库曾因一次漂移的 grep 误判 `_internalRenderLayer`/`updateLayerAfterDraw` 为"缺失"，把本可直接实装的 anchor w/h + 612 gate 错标为"架构前置缺失、不可移植"（commit 5018087，后由 eb347f5 纠正）。
- **memory / 代码注释 / analysis 笔记一旦被后续证据证伪，必须就地立即纠正，不得放任。** 反编译结论、agent-memory、`MEMORY.md` 索引、代码注释中只要发现方向搞反 / 前提错误 / 字段误判 / "缺失"误判，必须当场更新或删除那条记录，并在 commit 说明里点明"纠正了 X 及其原因"。**错误的 memory 会传染**——本仓库已多次被错误 memory 误导（M5 path-key 把 Player+24 误判为 path-keyed、M7 把已存在的渲染前置误判为缺失）。宁可多花一步纠正源头，也不要让错误结论在 memory/注释里留存继续误导。

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
- `rename` — 批量重命名，`batch` 下分 4 类对象：`func`（函数，按 `addr`+`name`）、`data`（全局/数据变量，`old`→`new`）、`local`（Hex-Rays 伪代码局部变量，按 `func_addr`+`old`→`new`）、`stack`（栈变量，同 local 形参）。支持 `dry_run`（只校验不改）、`allow_overwrite`、`stop_on_error`
- `sub_XXXX` 重命名为 `ClassName_MethodName`（命名规范见下方"IDA 符号管理"）走 `func`
- **局部变量可持久重命名**：`rename(batch={local:{func_addr, old, new}})` 等价于 IDA UI 右键 Rename，重命名后整个反编译体的所有引用都会更新（已实测 0x692AB0 的 v3→slot 持久生效）。`set_comments` 仅在需要额外标注语义时补充使用，不是因为 rename 改不动局部变量

#### 修正后保存
- 一轮分析结束后调用 `idb_save` 持久化所有修正

### IDA 符号管理
- **命名权威 = 二进制自身的名字证据，不是本地项目。** 优先级：
  1. **二进制里字面存在的名字** —— NCB 注册的成员字符串（`ncb_addMember` 的 key）、字符串常量、RTTI/typeinfo、导出符号。这是 ground truth，**读取它 ≠ 推断**；与本地冲突时**以二进制为准**。本地代码是“待验证 / 可能错”的一方，**绝不能反过来当命名权威**。（反面教训：本仓库 angle 访问器 getAngleDeg/getAngleRad 本地一度接反，正确映射来自注册函数 `Player_ncb_registerMembers@0x6D69C8` 里字面绑定 "angleDeg"→`0x6C1780`/"angleRad"→`0x6CD0C0`；若“以本地为据”就会把错误命名灌进 IDB。）
  2. **本地代码仅作交叉参照** —— 用于确认类名 + `ClassName_MethodName` 写法约定，且**仅在二进制无任何名字信号（纯 `sub_XXXX`）时**使用。
- **禁止从二进制行为推断 / 猜测命名**（如把 `StartProcess` 猜成 `Process`）。“读二进制里字面存在的名字”属第 1 项（允许）；“看行为猜名字”才是被禁止的。
- 二进制名字证据与本地标识符都找不到对应名时，加 `_guess` 后缀（如 `Layer_Update_guess`）。
- **IDB 里发现的误命名 = 被证伪的产物，必须就地修复**（与 memory / 注释 / analysis 的「证伪即纠正」同规则，见工作流 BLOCKING 节）：`rename` 到正确名 + 函数头 `set_comments` 记录纠正依据（注册站点 / 字符串地址）+ `idb_save`；并同步更新代码里按旧符号写的注释。

## 字节布局复刻工作法（重要方法论）
- **忠实复刻 ≠ 写"更安全"的代码。** 二进制里的死值运算（算了不消费的指针/偏移）、未初始化局部、refcount no-op（AddRef+即刻 Release）都是**源码 token**，必须复刻，不得因"更安全 / oracle 不可观察"省略或补 0 初始化。死值→`T* x=&...; (void)x;`（`(void)` 仅压跨编译器 unused 警告）；no-op→即刻析构的拷贝（如 `ttstr x=src;`）。
- 目标是复刻**源代码**（Android kirikiroid2 的 .cpp/.h），不是复刻 libkrkr2.so 这个**编译产物**。so 的 packed 偏移布局是 NDK clang(ARM64) 算出来的；我们的 wasm 是同一份源码经 emscripten clang(wasm32) 算出来的，**两者字节偏移不一致是 ABI 必然，可接受**
- 复刻“源代码结构”= 写普通 C++ 类（带继承/字段名/方法语义）让编译器自由算偏移。**禁止**用 `#pragma pack` / `_padN` 填充 / `static_assert(offsetof/sizeof==N)` 去硬凑 ARM64 字节布局——作者源码写的是字段名（`double time;`），从不写 `_pad4`；硬凑既非源码、在 wasm32 下又因指针 4B/ttstr 尺寸不同而凑不准
- 要对齐的六维全是语义层：源码结构 / 数据流 / 调用链 / 对象生命周期 / 内部容器选型（用 deque/hashmap 而非 std 替代，指**实现选型**对齐，非字节布局）/ 边界行为。**没有一维要求字节偏移一致**
- 字节偏移 + 证据地址的正确归属是 `analysis/*.md`（反编译对照笔记），**不是任何被编译的 C++ 文件**。偏移是反编译时确认字段语义/无遗漏的工具，不进代码
- 例外：当二进制按 `*(float*)(elem+4*i)` 真的按字节读某容器**元素的内部数据格式**时，该元素 POD 的内部字段布局是与平台无关的数据契约，可保留（如曲线关键帧 20B 元素）；但“**对象在内存的 ABI 偏移**”永不需对齐
- **反编译里的容器尺寸表达式（循环上界 / `dequeSize-1` / `+1`）可能是 STL `size()`/迭代器的内联展开产物，不是源码 token。** 判定「容器拓扑 / 源码结构是否已还原」必须反编译容器的**构造点**（push/erase 计数），不能只看消费循环的上界。反例（本仓库实测）：libstdc++ `std::deque::size()` 对 >512B 元素（1-elem/block，如 node 2632B）展开成 `(start.last-start.cur)/T == size()+1`，故反编译 `idx < dequeSize-1` **就是**源码 `idx < size()`，那个 `-1` 是编译器产物而非源码 `size()-1`，更不是 sentinel。曾因只看上界连错两次（先判 off-by-one，再判 trailing sentinel）。数值等价 ≠ 源码偏离：先确认反编译表达式是不是 STL 展开，再下「源码被简化」结论
