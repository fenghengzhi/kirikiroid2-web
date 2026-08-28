# MP-V06 Web Debug 正式配置与构建

## 结论

`MP-V06` 已在当前工作树（基线 `HEAD` 为 `5cc45b36c8ea64aa7ef710846ffc956efe02c3e9`）上完成最终 runtime reconciliation 之后的 Web Debug 重新配置、全量编译和固定产物核验，可标记为 `VERIFIED`。2026-08-29 在追加 Wasmtime-only render trace 接线和测试 fixture 独立性修复后，又以 `uv` CPython 3.11 路径前置的环境完成了一次 339/339 全量重建；下列产物是该最新成功链接结果。

本次不是 syntax-only、单个翻译单元或复用历史 CI 的推断。CMake 完整配置了 `Web Debug Config`，vcpkg manifest 的 116 个包全部就绪，Ninja 完成最终 `index.html` 链接步骤；随后对五个固定交付产物逐一执行存在性、字节数和 SHA-256 检查。机器可读结果见 `analysis/motionplayer_v06_web_build_products.tsv`。

## 工具链与配置

| 项目 | 实际值 |
|---|---|
| CMake | 4.4.2 |
| Ninja | 1.13.0.git.kitware.jobserver-pipe-1 |
| Bison | 3.8.2 |
| Emscripten | 4.0.23，提交 `7a5d93b50f6a3a35e85a0d2fc9e667b8498e6aed` |
| CMake preset | `Web Debug Config` |
| `CMAKE_BUILD_TYPE` | `Debug` |
| vcpkg triplet | `wasm32-emscripten` |
| 输出目录 | `out/web/debug` |

本机 `/usr/local` 的 unsigned CMake/Ninja/Bison 会进入不可中断状态，因此本轮使用 ad-hoc 签名的临时副本恢复同一 CMake/Ninja/Emscripten 工具链。Emscripten 第一次延迟构建 SDL_ttf 时暴露嵌套 cache lock；串行预热 `zlib`、`freetype`、`harfbuzz` 和 `harfbuzz-mt` 后，从清空的 sysroot cache 完整重编译成功。等价的正式配置/构建入口为：

```sh
cmake --preset "Web Debug Config" \
  -DBISON_EXECUTABLE=/path/to/signed/bison \
  -DCMAKE_MAKE_PROGRAM=/path/to/signed/ninja
```

正式构建命令为：

```sh
cmake --build out/web/debug
```

## 构建中发现并修复的集成错误

第一次进入 motionplayer 翻译单元时，`Player.h` 在只持有指针的声明中使用了 `tTJSNI_BaseLayer`，但没有前置声明。补充全局 `class tTJSNI_BaseLayer;` 后，所有受影响的 motionplayer 翻译单元通过编译。该改动不要求完整类型，不改变布局、调用链或运行时行为。

随后 `SeparateLayerAdaptor::assign(const SeparateLayerAdaptor &)` 的 NCB 强类型包装在模板实例化时缺少 `SeparateLayerAdaptor` 的标准 Variant 装箱/拆箱映射。该类必须继续使用独立的 delayed-subclass 注册路径，因此没有把 registrar 改成另一类宏；只在 registrar 前补充 `NCB_TYPECONV_BOXING(SeparateLayerAdaptor)`。这使现有强类型签名走 ncbind 的标准严格 native-instance unboxing，未修改 `assign` 方法体或注册表行数与顺序。

这两项均为正式构建揭示的声明/模板集成缺口。修复后 `cpp/plugins/motionplayer/libmotionplayer.a` 成功链接，整个 Web 可执行目标也完成最终链接。

## 固定产物

| 产物 | 字节数 | SHA-256 |
|---|---:|---|
| `index.html` | 85,083 | `988944679c6348e92a2761b2d5c2804ef50968f91831e30696c13843532a56a9` |
| `index.js` | 616,859 | `ad40f49f0982264e17cabf947d3eadebba5712faccb5c04ed81a7a0c2fce6c62` |
| `index.wasm` | 85,369,212 | `c45da319e632286689d8453c9219ac7cb759e54a518c2da97ea32ecca72d01cd` |
| `vlfs.js` | 41,636 | `850db5f563f2ce3a9cb9b91bdafcfbe265931835a509420fc75454f3dd12631f` |
| `assets.zip` | 7,853,397 | `1c464057f96491b04c3cb74a5de9adce14eefe124b842fab932a6a18c4985cea` |

五项均存在且为普通文件，哈希是在成功链接后直接从 `out/web/debug` 重新计算。

## 诊断边界

构建成功不等于外部四端差分全部通过。链接阶段仍报告 Emscripten 的已知警告，包括 pthread 与可增长内存的性能提示、JSPI/ASYNCIFY 实验性提示，以及 JS library 内部符号依赖警告；它们没有使构建失败。完整诊断和非回归已由 `MP-V08` 收口，motionplayer unit/hash 目标也已由 `MP-V07` 最终通过。

本任务的验收结论是：最终修复后的当前工作树能够通过正式 Web Debug 全量构建，并生成全部五个约定产物。该结论不替代 `MP-V03`～`MP-V05` 仍需外部环境的差分缺口。
