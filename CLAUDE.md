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
- C++ logging in Emscripten: `spdlog`/`printf`/`fprintf(stderr)` NOT captured by playwright. Use `EM_ASM({ console.warn(...) })` for browser-visible logs
- ZIP game loading via playwright takes ~10 min; plan test cycles accordingly

## Workflow
- CRITICAL: All fixes MUST be aligned to libkrkr2.so. Do NOT guess or patch — decompile the corresponding function in libkrkr2.so first, understand how the original does it, then replicate that logic. Use `/ida-decompile` skill and `mcp__ida-pro-mcp__decompile` for every non-trivial fix.
- IMPORTANT: When fixing a bug, do NOT directly apply a guessed fix. First add logging/debug output to confirm the root cause, verify the hypothesis, then apply the actual fix.
- When a C++ function needs implementation (draw, captureCanvas, etc.), find and decompile the exact function in libkrkr2.so before writing any code. Do not invent behavior.
- ANTI-PATTERN: Do NOT try multiple "maybe this will work" patches in sequence. Each failed attempt wastes time and muddies the code. Instead: decompile libkrkr2.so → understand the real data flow → implement once correctly.
- ANTI-PATTERN: Do NOT guess the reason for a failure. Every conclusion must be backed by evidence — either from decompiling libkrkr2.so, disassembling game scripts (tjsdump), or runtime logs. Phrases like "maybe", "probably", "might be" indicate guessing. Stop and gather evidence first.
- ANTI-PATTERN: Do NOT use an uncertain "possible" answer as the basis for a fix. If you are not sure whether X is the cause, do NOT write code to fix X. Instead, decompile libkrkr2.so or add runtime logging to confirm X first, THEN fix.
- When something doesn't display/render, decompile the full rendering chain in libkrkr2.so (Layer→DrawDevice→Texture→Cocos2D) before touching local code. The local rendering pipeline may differ from libkrkr2.so.
- When IDA decompilation 100% confirms an identifier's real name, rename it immediately via `mcp__ida-pro-mcp__rename`. This applies to functions (`func`), global variables/data (`data`), local variables (`local`), and stack variables (`stack`) — not just functions. When the name is a best guess but not 100% confirmed, still rename it but append `_guess` suffix (e.g. `Layer_Update_guess`). Building a readable symbol table makes all future decompilation output more useful.
