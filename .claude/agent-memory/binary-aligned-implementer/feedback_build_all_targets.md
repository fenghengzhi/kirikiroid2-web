---
name: feedback-build-all-targets
description: 新增/删除 motionplayer .cpp 文件后必须更新 platforms/wasmtime/CMakeLists.txt 的手工源列表并构建 krkr2_wasmtime_guest，否则 differential.yml CI 在 wasm 链接阶段报 unsupported import
type: feedback
---

新增、删除或重命名 `cpp/plugins/motionplayer/` 下的 .cpp 文件时，除了改 `cpp/plugins/motionplayer/CMakeLists.txt`，**必须同步更新 `platforms/wasmtime/CMakeLists.txt` 里 `krkr2_wasmtime_guest_objects` 的手工源文件列表**，然后本地构建 `krkr2_wasmtime_guest` target 验证链接，再 push。

**原因：** EmoteEngine P0 重构（2026-05-30）只构建了 `out/web/debug` 和 `out/macos/debug --target motionplayer-dll`，没构建 wasmtime guest。抽出独立 EmoteEngine.cpp 后，ctor/dtor 的 out-of-line 定义不在 guest 源列表里，differential.yml 的 wasmtime job 在 LLDB guest-debug 阶段报 `unsupported import(s): env._ZN6motion11EmoteEngineC1ENS_15ResourceManagerE, env._ZN6motion11EmoteEngineD1Ev`（未解析符号变成 wasm import）。web/debug 链接的是整个 motionplayer 库所以没暴露；guest 用的是手工 curated 子集。

**应用方式：**
- 阶段 6（构建与运行时验证）必须包含三个 target，不只是 web/debug：
  1. `cmake --build out/web/debug`
  2. `cmake --build out/macos/debug --target motionplayer-dll`（如适用）
  3. `cmake --preset "Wasmtime Headless Debug Config" -DBISON_EXECUTABLE=/opt/homebrew/opt/bison/bin/bison` 然后 `cmake --build out/wasmtime/debug --target krkr2_wasmtime_guest`
- 改了任何 CMakeLists.txt 后，构建 guest 前必须重跑对应 preset（CLAUDE.md 已有此通则）。
- guest 源列表是手工维护的显式清单（platforms/wasmtime/CMakeLists.txt:6 起），不会自动跟随 motionplayer/CMakeLists.txt。新增类时 grep `platforms/wasmtime/CMakeLists.txt` 确认是否漏。
- 本地构建环境一行（见 .claude.local.md）：
  `export EMSDK=/Users/bytedance/emsdk && export EMSDK_PYTHON=$EMSDK/python/3.13.3_64bit/bin/python3 && export VCPKG_ROOT=/Users/bytedance/vcpkg && export PATH="/opt/homebrew/opt/bison/bin:$EMSDK:$EMSDK/upstream/emscripten:$EMSDK/node/20.18.0_64bit/bin:/opt/homebrew/bin:$PATH"`

相关：[[emoteengine_p0_refactor_2026-05-30]]
