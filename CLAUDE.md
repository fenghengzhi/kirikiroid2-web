# KrKr2 WebAssembly Port

## Build
- `cmake --preset "Web Debug Config"` then `cmake --build out/web/debug`
- Release: `cmake --preset "Web Release Config"` then `cmake --build out/web/release`
- Requires: emsdk sourced, VCPKG_ROOT set, ninja, cmake 3.31.1+, bison 3.8.2+
- Output: `out/web/{debug,release}/` → index.html, index.js, index.wasm, index.data, index.worker.js
- Full env one-liner: `export EMSDK=/Users/bytedance/emsdk && export VCPKG_ROOT=/Users/bytedance/vcpkg && export PATH="/opt/homebrew/opt/bison/bin:$EMSDK:$EMSDK/upstream/emscripten:$EMSDK/node/20.18.0_64bit/bin:/opt/homebrew/bin:$PATH"`
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
- `cpp/core/tjs2/` — TJS2 scripting engine core
- `cpp/core/visual/WindowIntf.cpp` — Window class: drawDevice setter requires `interface` property returning iTVPDrawDevice*
- `cpp/core/plugin/PluginImpl.cpp` — TVPLoadPlugin (called by Plugins.link), TVPLoadInternalPlugins (startup)
- `cpp/core/base/StorageIntf.cpp` — Auto path table, TVPAddAutoPath, TVPGetPlacedPath
- `cpp/core/environ/web/Platform.cpp` — Web-specific startup, auto-mounts sibling xp3 files from ZIP
- `tests/unit-tests/plugins/motionplayer-dll.cpp` — MotionPlayer/EmotePlayer unit tests

## Reverse Engineering with IDA MCP
- No Android kirikiroid2 source code is available — only libkrkr2.so binary. Use IDA MCP for all reverse engineering.
- `analysis_MotionPlayer_EmotePlayer.md` at project root has prior RE analysis of libkrkr2.so
- Use `mcp__ida-pro-mcp__decompile` with function addresses to get pseudocode
- Use `mcp__ida-pro-mcp__find` with type "string" to locate string references
- `mcp__ida-pro-mcp__find` ONLY matches ASCII/UTF-8 strings — use `/ida-search-string` skill for UTF-16
- IDA may show only first char of UTF-16 strings (e.g. "f" for "fstat.dll") — use hex dump or `get_operand_value` to resolve
- IDA sometimes merges separate functions — check for `SUB SP` prologues at `loc_` addresses
- NCB class registration functions in IDA: look for `sub_54242C` (addMember) and `sub_52FA58` (addConstant) calls
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

## Workflow
- IMPORTANT: When fixing a bug, do NOT directly apply a guessed fix. First add logging/debug output to confirm the root cause, verify the hypothesis, then apply the actual fix.
