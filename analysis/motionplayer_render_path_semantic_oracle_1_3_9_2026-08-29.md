# MotionPlayer 1.3.9 semantic render_path oracle

## 结论

当前工作树已建立以下配对流程：

```text
Kirikiroid2 1.3.9 Android arm64 libgame.so
  -> Frida 在已 rebase 的函数边界采样
  -> draw_dispatch / render_prepare / render_commands / render_execute
  -> 平台无关 JSON 语义事件

port-wasm / Wasmtime full Guest
  -> KRKR2_WASMTIME_HEADLESS C++ scope 插桩
  -> 同四类平台无关 JSON 语义事件

两侧 artifact
  -> stage shape + draw route + execute envelope 比较
```

这个 contract 不比较 native 地址、寄存器、指针、STL 布局或 CPU 指令，也不把
PNG/hash 当成成功条件。因此 Wasmtime host 是 x86_64、arm64 或其他受支持平台时，
比较 schema 不需要按 host CPU 重写。Frida 地址仍然与 Android oracle 的 APK/ABI
绑定；当前 active Android lane 明确绑定 Kirikiroid2 1.3.9 arm64-v8a。

## 四份参考二进制证据

本次重新打开并等待 Hex-Rays ready 的权威输入为：

- `reference/binaries/Kirikiroid2_1.3.9_Android_arm64-v8a.so`
- `reference/binaries/Kirikiroid2_1.3.9_Android_armabi-v7a.so`
- `reference/binaries/Kirikiroid2_1.3.9_iOS_arm64`
- `reference/binaries/Kirikiroid2_1.3.9_iOS_armv7.thin-armv7`

下表地址均为各 IDA database 中的 EA；Android shared object image base 为 0，
iOS arm64 image base 为 `0x100000000`，iOS armv7 image base 为 `0x4000`。

| 语义边界 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `Player::draw` dispatch | `0x6D3398` | `0x597864` | `0x100123C84` | `0x122F28` |
| direct D3D route | `0x6D2F70` | `0x59761C` | `0x100123844` | `0x122AAC` |
| direct SLA route | `0x6D2A38` | `0x597328` | `0x1001233C8` | `0x12257C` |
| render prepare | `0x6D2544` | `0x596DF0` | `0x100122F68` | `0x121FDC` |
| apply prepared projection | `0x6D2644` | `0x596EB0` | `0x100123038` | `0x1220F0` |
| append/build prepared items | `0x6BF714` | `0x58B178` | `0x1001148F8` | `0x1123D8` |
| build render commands | `0x6C2208` | `0x58C7C4` | `0x1001167BC` | `0x114118` |
| accurate SLA execute | `0x6C7088` | `0x590468` | `0x10011A9E8` | `0x118D70` |
| ordinary Canvas execute | `0x6C4820` | `0x58E2CC` | `0x1001186E0` | `0x11653C` |
| ordinary post-draw update | `0x6CBBB8` | `0x59327C` | `0x10011E6CC` | `0x11CF20` |
| `Debug.message` callback | `0xA178BC` | `0x76511C` | `0x100259BDC` | `0x25B180` |

四份 `Player::draw` 反编译都呈现相同控制流：严格转换 target，探测 direct
D3D，再探测 direct SLA；两条 direct adaptor route 均早于 ordinary prepare；仅
ordinary route 执行 prepare、projection、Canvas execute 和 post-draw update。
因此 Frida agent 在 direct route 中不会把内部 helper 的 prepare/projection 调用
误写成顶层 ordinary draw route。

direct SLA 的四份反编译都先调用 accurate SLA renderer，再复制 SLA target
variant 并执行更新。Android arm64 accurate renderer 的函数入口反汇编进一步确认：
第二参数是 SLA 对象，target variant 位于该对象 `+0x14`；64 位 C++ 对象中的同一
字段位置是 `+20`。`Debug.message` 由跨 ASCII/UTF-8、UTF-16LE、UTF-32LE 的宽
字符串搜索定位，四份注册函数和 callback 均已反编译；callback ABI 与 agent 使用的
`args[1]` 参数数量、`args[2]` 参数数组一致。当前 semantic-only lane 不依赖该 callback
产生图像事件，它仅作为已核验诊断入口保留。

## 双侧事件映射

| stage | Android 1.3.9 Frida | Wasmtime Guest | 比较内容 |
|---|---|---|---|
| `draw_dispatch` | `Player::draw` 及 direct D3D/SLA route 边界 | `Player::draw` trace scope | enter/leave、target checks、route、prepare disposition |
| `render_prepare` | prepare 与 projection 函数边界 | prepare/projection scopes | 每帧事件 kind 顺序 |
| `render_commands` | append items 与 build commands 边界 | build-items/build-commands scopes | 每帧事件 kind 顺序；build-flow 字段仅作诊断 |
| `render_execute` | accurate SLA 与 Canvas renderer 边界 | ordinary execute scope；本次补齐 accurate SLA execute scope | 每帧必须存在 `execute_enter`、`execute_leave` envelope |

Guest 的 accurate SLA scope 只在 `KRKR2_WASMTIME_HEADLESS` 下编译；Web、Android、
iOS 和普通 native 产品构建的渲染语义不受影响。Android agent 在 attach 时发布
`motion-render-semantic-v1` capability 和精确四 stage 列表，Python tracer 必须看到
完全相同的列表才继续，否则 detach 并 fail closed。

## 明确不在 contract 内的内容

以下旧 1.4.4 深层诊断没有被冒充为 1.3.9 能力：

- `layer_save`；
- Layer/Bitmap 内部字段和 raw MainImage probe；
- `execute_pre`/`execute_post`、`updateLayerAfterDraw_pre/post` PNG；
- visual readback、RGBA hash 和 per-step image compare；
- private motion GLL/native object layout。

Android `render_path` 收到任何 image/raw/visual option 时立即返回 usage error。语义
artifact 仍保留 schema 兼容的 `images` envelope，但带
`semanticOnly: true`、空 `captureSurfaces`、空 `phases`、`imageCount: 0`；validator
会检查这些字段确实为空，不能用缺失文件或被忽略的 PNG 蒙混通过。

## Actions 接线

`.github/workflows/differential.yml` 当前执行：

1. Wasmtime job 对 `yuzulogo`、`m2logo` 各自使用 hash-pinned 15 Hz XP3，生成
   port trace 和四阶段 semantic render artifact，并运行 fail-closed validator；
2. `adb-frida` job 保留现有 normalized playback oracle，再逐 case 重启 app、清理
   forward/logcat，以 Frida 录制同四阶段 Android artifact，并运行同一 validator；
3. compare job 下载两侧 trace/artifact，分别运行 playback comparator、
   `compare_motion_render_steps.py --semantic-only` 和 draw-dispatch comparator；
4. 三个退出码独立保存，任一非零使 job 失败，但三个报告仍全部上传。

已禁用、仅供手工参考的 ARM-Linux workflow 也改用同一 semantic-only artifact
和 comparator contract，避免在其他 Wasmtime host 架构上重新落回旧 PNG offset。

semantic render-step comparator 会忽略两侧可能存在的 post-draw marker 等非核心诊断，
但不会忽略 prepare/commands/execute 的 event shape。即使两侧都没有 PNG，只要
`execute_enter`/`execute_leave` 缺失，比较仍然失败。

## 本地验证

已完成：

- Emscripten full Guest 重编成功；
- Frida JavaScript `node --check`；
- 修改过的 Python 入口 `py_compile`；
- semantic compare/artifact、timing、strict-oracle 单测共 18 项通过；
- 两个 full Guest case 写入同一 artifact root 后 validator 通过：88 帧、1384
  events、0 images；
- `m2logo` 为 25 帧、439 events，`yuzulogo` 为 63 帧、945 events；
- `m2logo` 四 stage 分别为 draw 100、prepare 100、commands 164、execute 75 events；
- `m2logo` 的 execute 核心 envelope 为 25 enter + 25 leave，另有 25 个被 semantic
  comparator 排除的 post-draw marker；
- `yuzulogo` 的 execute 核心 envelope 为 63 enter + 63 leave，另有 63 个
  post-draw marker；
- `git diff --check`。

本机没有启动 Kirikiroid2 1.3.9 Android/Redroid + Frida 完整环境；Android runtime
和最终双侧结果使用下节的 GitHub Actions 实跑证据，不以本地静态检查代替。

## GitHub Actions 验证

commit `f9d8030626b93bf66b6fe32ffb2977b3c8be9289` 的
[Differential Tests run 33240943350](https://github.com/fenghengzhi/kirikiroid2-web/actions/runs/33240943350)
和 [Build Web run 33240943410](https://github.com/fenghengzhi/kirikiroid2-web/actions/runs/33240943410)
均为 `success`。Differential 各 job：

- `build-legacy-harness`：PASS；
- `adb-frida`：PASS；Android semantic validator 为 2 cases / 88 frames /
  1296 events / 0 images；
- `wasmtime`：PASS；Guest semantic validator 为 2 cases / 88 frames /
  1384 events / 0 images；
- `motion-playback-compare`：PASS。

双侧 compare 的实际结果：

| compare | m2logo | yuzulogo |
|---|---|---|
| normalized playback | PASS，25 frames，无 mismatch | PASS，63 frames，无 mismatch |
| prepare/commands/execute semantic shape | PASS，全部 mismatch count 为 0 | PASS，全部 mismatch count 为 0 |
| draw-dispatch | PASS，25/25 `separate_layer_adaptor` | PASS，63/63 `separate_layer_adaptor` |

同一 run 上传的核心 artifact：

- Android semantic render：artifact `9711375592`，digest
  `9e46ef8a356a4ec61462b2125c852aeebd4b485eb77e19319314ae86f262477a`；
- Wasmtime semantic render：artifact `9711405645`，digest
  `e1d5306ced2354a638d061d5e307427d28c1bdf66feceff0cbf6d2f8c9ca6048`；
- compare report：artifact `9711408951`。

在达到 PASS 前，CI 还暴露并修复了两个仅在真实 Android harness 路径可见的问题：

1. commit `e44e656c` 移除了 libgame.so 明确拒绝的旧 standalone-libkrkr2
   `TJS_INIT`，统一使用已验证的 `startupFrom` 启动链；
2. commit `f9d80306` 在事件按 case 归一化时写入 canonical render `source`，使
   event envelope 与 manifest source 一致并通过 fail-closed validator。
