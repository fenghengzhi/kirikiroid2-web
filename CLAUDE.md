# KrKr2 WebAssembly Port

## Build
- `cmake --preset "Web Debug Config"` then `cmake --build out/web/debug`
- Release: `cmake --preset "Web Release Config"` then `cmake --build out/web/release`
- Requires: emsdk sourced, VCPKG_ROOT set, ninja, cmake 3.31.1+, bison 3.8.2+
- Output: `out/web/{debug,release}/` → index.html, index.js, index.wasm, index.data, index.worker.js
- Full env one-liner: `export EMSDK=/Users/bytedance/emsdk && export EMSDK_PYTHON=$EMSDK/python/3.13.3_64bit/bin/python3 && export VCPKG_ROOT=/Users/bytedance/vcpkg && export PATH="/opt/homebrew/opt/bison/bin:$EMSDK:$EMSDK/upstream/emscripten:$EMSDK/node/20.18.0_64bit/bin:/opt/homebrew/bin:$PATH"`
- EMSDK_PYTHON must be exported — vcpkg ffmpeg build fails without it (system Python lacks `match` syntax)
- After CMakeLists.txt changes (file add/remove/rename), must re-run `cmake --preset` before build
- If bison errors with "require 3.8.2 but have 2.3", add `-DBISON_EXECUTABLE=/opt/homebrew/opt/bison/bin/bison`
- IMPORTANT: Kill coi-server BEFORE building — it serves stale wasm if build runs while server is up
- Asan build (`Web Debug Asan Config`) produces ~126MB wasm; coi-server may fail to serve it

## Project Structure
- `cpp/plugins/` — NCB plugin DLLs (each file = one virtual .dll module)
  - `PackinOne.cpp` — Batch loader that loads 8 sub-plugins when `Plugins.link("PackinOne.dll")` is called
  - `DrawDeviceD3D.cpp` — iTVPDrawDevice wrapper (D3D stub for web build)
- `cpp/plugins/motionplayer/` — EmotePlayer + Player (MotionPlayer) classes with NCB TJS2 bindings
  - `main.cpp` — NCB_REGISTER_CLASS/SUBCLASS macros for TJS2 registration
  - `EmotePlayer.{h,cpp}` — E-mote SDK wrapper (D3DEmotePlayer derives from it)
  - `Player.{h,cpp}` — Motion animation player (registered as Motion.Player in TJS2)
  - `ResourceManager.{h,cpp}` — PSB resource loading + decrypt seed management
  - `D3DAdaptor.h` — Motion.D3DAdaptor pixel buffer (RE'd from libkrkr2.so sub_6ADB10)
  - `SeparateLayerAdaptor.h` — Motion.SeparateLayerAdaptor thin wrapper
- `cpp/core/tjs2/` — TJS2 scripting engine core
- `cpp/core/visual/WindowIntf.cpp` — Window class: drawDevice setter requires `interface` property returning iTVPDrawDevice*
- `cpp/core/plugin/PluginImpl.cpp` — TVPLoadPlugin (called by Plugins.link), TVPLoadInternalPlugins (startup)
- `cpp/core/base/StorageIntf.cpp` — Auto path table, TVPAddAutoPath, TVPGetPlacedPath
- `cpp/core/environ/web/Platform.cpp` — Web-specific startup, auto-mounts sibling xp3 files from ZIP
- `tests/unit-tests/plugins/motionplayer-dll.cpp` — MotionPlayer/EmotePlayer unit tests

## Reverse Engineering with IDA MCP
- No Android kirikiroid2 source code is available — only libkrkr2.so binary. Use IDA MCP for all reverse engineering.
- CRITICAL: libkrkr2.so and the current project code do NOT correspond 1:1. When analyzing libkrkr2.so, do NOT reference local project code as ground truth — the local code may be wrong or incomplete. Always treat libkrkr2.so decompilation as the authoritative source.
- `analysis/` directory contains detailed RE analysis docs — check before re-analyzing the same functions
- Key analysis docs: `PSB_RL_Decompression_libkrkr2so.md`, `SLA_Rendering_Chain_libkrkr2so.md`, `Window_DrawDevice_Scaling_libkrkr2so.md`
- SLA rendering chain (6 steps): PSB tree eval (0x6BB33C) → vertex computation (0x6BC4F0) → drawAffineMatrix transform (0x6C2334) → cameraOffset+rootOffset (0x6D5264) → PrivateMotionGLL child layer (0x6D5948) → direct vertex render (0x6DE738)
- Window scaling: TVPWindowLayer::RecalcPaintBox (0xaa7c58), ResetDrawSprite (0xaa7d70), SetPaintBoxSize (0xaa5a24). drawSprite.textureRect clips to paintBox (scWidth×scHeight), not full primaryLayer size
- `analysis_MotionPlayer_EmotePlayer.md` at project root has prior RE analysis of libkrkr2.so
- Use `mcp__ida-pro-mcp__decompile` with function addresses to get pseudocode
- Use `mcp__ida-pro-mcp__find` with type "string" to locate string references
- `mcp__ida-pro-mcp__find` ONLY matches ASCII/UTF-8 strings — use `/ida-search-string` skill for UTF-16
- IDA may show only first char of UTF-16 strings (e.g. "f" for "fstat.dll") — use hex dump or `get_operand_value` to resolve
- IDA sometimes merges separate functions — check for `SUB SP` prologues at `loc_` addresses
- NCB class registration functions in IDA: look for `ncb_addMember` (0x54242C) and `ncb_addConstant` (0x52FA58) calls
- Many functions have been renamed in IDA — see `.claude/skills/ida-decompile/SKILL.md` "Named Functions" table for the full list
- NCB module loading (`LoadModule`) is case-insensitive (lowercases before lookup)

## Code Patterns
- TJS2 property binding: `NCB_PROPERTY(name, getter, setter)`, `NCB_PROPERTY_RO(name, getter)`
- TJS2 method binding: `NCB_METHOD(name)`, `NCB_METHOD_RAW_CALLBACK(name, &Class::func, flags)`
- Stub pattern: `#define STUB_WARN(name) LOGGER->warn("ClassName::" #name "() stub called")`
- String conversion: `detail::narrow(ttstr)` → std::string, `detail::widen(std::string)` → ttstr

## Debugging
- XP3 extraction: `tools/bin/mac/rel/xp3 -o /tmp/out file.xp3`
- TJS2 bytecode disassembly: `tools/bin/mac/rel/tjsdump file.tjs` (use `/tjs2-disasm` skill)
- Build native tools: `cmake --preset "MacOS Release Config" -DBUILD_TOOLS=ON -DBISON_EXECUTABLE=/opt/homebrew/opt/bison/bin/bison && cmake --build out/macos/release --target tjsdump`
- Do NOT test with individual XP3 files — incomplete XP3 sets cause init failures that mask real bugs
- Browser automation: Use `playwright-cli` skill. Game uses touch events for left-click; use CDP `Input.dispatchTouchEvent` or ensure BUTTON_LEFT in onMouseDownEvent
- C++ logging in Emscripten: `spdlog`/`printf`/`fprintf(stderr)` all output to browser console, but playwright-cli console only shows a limited number of recent entries. When logs are voluminous, earlier C++ output gets pushed out. Use debug-capture.sh's addInitScript approach to capture specific keywords
- ZIP game loading via playwright takes ~10 min; plan test cycles accordingly
- RecalcPaintBox in `cpp/core/environ/cocos2d/MainScene.cpp` controls game→screen coordinate mapping. Key runtime values: viewSize, contentSize, paintBox, scale, offset
- Game uses exHeight (1440) > scHeight (1080) for extended layer area. primaryLayer contentSize = scWidth×scHeight, but some layers (AffineLayer) use exWidth×exHeight

## Workflow — 代码修改前置条件（BLOCKING）

任何对 cpp/ 目录的代码修改（Edit/Write），**必须**满足以下全部条件，缺一不可。不满足条件的修改视为无效，必须回退。

### 前置检查清单
1. **libkrkr2.so 函数地址** — 本次修改对齐的是哪个函数（例：sub_692AB0 at 0x692AB0）
2. **反编译证据** — 本次对话中必须有对该函数的 `mcp__ida-pro-mcp__decompile` 调用记录
3. **关键逻辑摘要** — 用伪代码写出 libkrkr2.so 的实际行为（不超过10行），包括所有条件分支和默认值
4. **本地实现对照** — 逐行说明本地代码如何复刻上述伪代码

### 硬性禁止（违反任何一条 = 立即停止并反编译）
- **禁止从 PSB 键名推导行为** — "PSB有opa键"不等于"应该读opa"。必须反编译确认读取条件（如 mask 位掩码门控）、默认值、数据类型
- **禁止从变量名推导语义** — "opacity"不等于"opa"。必须反编译确认 libkrkr2.so 实际使用的字符串常量
- **禁止"先改代码再验证"** — 必须"先反编译 → 写出 libkrkr2.so 伪代码 → 再改本地代码"
- **禁止把多个推测链接成结论** — 每一步都必须有独立的反编译/运行时日志证据
- **禁止从本地代码推断 libkrkr2.so 行为** — 本地代码可能是错的，libkrkr2.so 是唯一权威来源

### 标准工作流程
1. 发现问题 → 加诊断日志确认现象
2. 反编译 libkrkr2.so 对应函数 → 写出伪代码
3. 对比本地代码与伪代码 → 找到精确差异
4. 修改本地代码精确复刻伪代码 → 在注释中引用函数地址
5. 构建验证 → 运行时诊断确认修复

### 渲染/定位问题专项
- 修复前必须 trace 完整坐标链（PSB → ownerLayer → primaryLayer → paintBox → screen），每层有独立 transform
- 反编译完整渲染链（Layer→DrawDevice→Texture→Cocos2D），不要只看局部

### IDA 符号管理
- 当反编译 100% 确认标识符真名时，立即通过 `mcp__ida-pro-mcp__rename` 重命名（func/data/local/stack）
- 非 100% 确认的加 `_guess` 后缀（如 `Layer_Update_guess`）
