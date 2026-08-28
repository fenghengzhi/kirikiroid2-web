# motionplayer geometry / Bezier / position / hit-test Wasmtime 与 ADB 差分（MP-V03，2026-08-28）

## 1. 结论与状态

`MP-V03` 的最终 disposition 是 `EVIDENCE_BLOCKED`，不能标为全量 `VERIFIED`：

- 当前源码新鲜编译出的三个 WebAssembly 标量 family 已由独立 Wasmtime CLI 执行，
  `geometry_hit_test` 10/10、`bezier_curve` 6/6、`position_interp` 5/5，合计 21/21
  与仓库现有 expected **逐值完全相等**；
- 本机没有连接的 Android arm64 设备/模拟器，也没有已安装或仓库内可直接安装的
  `krkr2-harness.apk`，因此三个 ADB return-value runner 未能获得本轮新鲜结果；
- 精确到当前 HEAD 的公开 CI `adb-frida` job 只执行 `motion_playback` oracle recording，
  没有调用三个标量 ADB runner，不能拿它替代本项 ADB 证据；
- oracle README 中的 Android 10/10、6/6、5/5是历史状态，只作背景，不提升为本轮验证；
- 2026-08-29 的重复审计仍找不到本地 `adb`、Android SDK/emulator、Docker、仓库 APK、预构建
  harness APK 或 frida-server；最新公开 Differential 仍是 run 550，workflow/产物也没有三个 scalar
  ADB runner。因此该缺口按 `tasks.md` 允许的终态词汇收口为 `EVIDENCE_BLOCKED`，而不是无限期
  保留含糊的 `PARTIAL_VERIFICATION`。

权威逐例结果为 `analysis/motionplayer_v03_differential_results.tsv`。本轮没有修改任何spec或
expected，也没有把未执行的ADB侧写成PASS。

## 2. 验证分母

| family | spec数 | 当前 Wasmtime | 当前 ADB | disposition |
|---|---:|---:|---:|---|
| `geometry_hit_test`（包含全部Point/shape hit-test边界） | 10 | 10/10 PASS | 未执行 | Wasmtime已验证，ADB环境阻塞 |
| `bezier_curve` | 6 | 6/6 PASS | 未执行 | Wasmtime已验证，ADB环境阻塞 |
| `position_interp` | 5 | 5/5 PASS | 未执行 | Wasmtime已验证，ADB环境阻塞 |
| **合计** | **21** | **21/21 PASS** | **0/21 executed** | **ADB直接证据终态阻塞** |

几何10例覆盖圆内/边界/外、矩形左上闭与右下开、四边形内外与两种绕序、非法type；Bezier
6例覆盖单段中点、线性、t前后截断和多段两侧；position 5例覆盖t=0、t=1、无easing、带easing
和同点输入。position当前spec的rotation curve均为空，见第5节的ABI限制。

## 3. 新鲜 Wasmtime 执行

验证基线：

- git HEAD：`5cc45b36c8ea64aa7ef710846ffc956efe02c3e9`；
- host：macOS Darwin 25.6.0，x86_64；
- Emscripten：6.0.8（commit `aeb67926e7de656da38bc807d83050af93578758`）；
- 独立 Wasmtime CLI：31.0.0（commit `7a9be587f`）。

三个module通过仓库统一入口构建，而不是手写独立编译命令：

```sh
python3 tests/differential/run_all.py --build-only \
  --family geometry_hit_test \
  --family bezier_curve \
  --family position_interp
```

随后通过新加的CLI runner执行同一spec目录：

```sh
python3 tests/differential/python/run_scalar_wasmtime_cli.py \
  --wasmtime /path/to/wasmtime \
  --family geometry_hit_test \
  --family bezier_curve \
  --family position_interp
```

runner逐例输出JSONL且只在所有actual等于expected时返回0。本轮21行均为`status=ok`；结果已规范化
存入TSV。构建产出的三个`.wasm`只是本轮临时产品，不作为源文件或golden提交。

## 4. 为什么增加 Wasmtime CLI lane

仓库原runner把Wasmtime嵌入Python进程并可配合LLDB采样。在当前桌面host上：

1. Python `wasmtime` 48.0.0能instantiate，但第一次调用导出函数时进程被SIGKILL（137）；
2. 降到与独立CLI相同的大版本31.0.0后，instantiate仍成功，导出函数调用仍被SIGKILL；
3. 同一module经官方独立Wasmtime CLI 31.0.0执行正常，21/21通过。

因此失败边界是本机Python宿主内嵌JIT执行权限/进程环境，不是case mismatch。新增CLI lane复用相同
Wasm module和spec，只绕开Python内嵌JIT。原LLDB runner仍保留，且可用
`KRKR2_WASMTIME_DIRECT`选择较低内存配置；这个开关没有被误报为已解决当前host的SIGKILL。

## 5. 最小 harness ABI 扩展及边界

为让独立CLI在不保持跨进程linear-memory写入状态的前提下传参，本轮只扩展测试harness：

- Bezier导出`get_bezier_result`及扁平化`run_bezier_curve_direct`；
- position导出`get_out_pos_ptr`及扁平化`run_position_interp_direct`；
- 原三个runner的隐藏driver报告增加`results`字段，便于非LLDB调用复用；
- `tests/differential/run_all.py`增加`--build-only`，仍由同一directive解析和Emscripten选项构建。

扁平化position ABI故意只支持当前5个`rotation_curve`为空的spec。若以后出现非空rotation数据，
CLI runner会fail closed并要求扩展ABI，不会静默忽略rotation。Bezier扁平ABI最多接受当前所需的7个
control points，并拒绝过长或x/y长度不等。以上都是测试harness约束，不是production语义改变。

## 6. ADB现场检查与真实阻塞

本轮下载并运行官方Android platform-tools 37.0.1；`adb devices -l`输出只有表头，设备数为0。
三个return-value runner的默认路径不要求Frida，但要求：

1. 一个rooted、API 24+、arm64的设备/模拟器；
2. 已安装包含Kirikiroid2 1.3.9 `libgame.so`和`libharness.so`的`krkr2-harness.apk`；
3. HarnessActivity能启动并在转发端口5039报告`READY`。

当前仓库也缺少：

- `reference/packages/Kirikiroid2_1.3.9.apk`；
- `tests/differential/oracle_runner/harness-apk/prebuilt/krkr2-harness.apk`。

因此无法在本机补装harness后执行。`tools/bin/android/frida-server`也不存在，但这只阻塞可选的
`--trace`/`--record-trace`路径，不是本项默认return-value差分未执行的主因。三个ADB runner均已通过
Python语法编译和`--help`入口检查；这只证明host端程序可启动，不等价于native执行。

## 7. CI与历史结果的证据隔离

当前HEAD对应的公开GitHub Actions run：

- run：`https://github.com/fenghengzhi/kirikiroid2-web/actions/runs/32971807749`；
- 成功的ADB job：`https://github.com/fenghengzhi/kirikiroid2-web/actions/runs/32971807749/job/98187268290`。

静态核对`.github/workflows/differential.yml`确认该job只录制并上传`motion_playback` Android oracle；
工作流中不存在`run_geometry_hit_test_adb.py`、`run_bezier_curve_adb.py`或
`run_position_interp_adb.py`调用。因此job成功不能推导三个标量family已执行。

`tests/differential/oracle_runner/README.md`列出的10/10、6/6、5/5来自既有Android记录；同时
`trace_targets.py`明确把标量native offsets标作历史、已移除的`libkrkr2.so`工具资料。两者均不会
被本报告冒充当前四参考二进制或当前HEAD的新鲜ADB结果。

2026-08-29 又通过 GitHub Actions API 复核最近30个run：最新 Differential 仍为 run 550 / commit
`5cc45b36c8ea64aa7ef710846ffc956efe02c3e9`，artifact清单只有 `motion_playback-oracle-traces`、
`motion_playback-oracle-record-logs` 和 `libharness-gnustl-arm64`。没有 scalar ADB result，也没有可从
CI下载后冒充设备返回值的替代物。

## 8. 完整性检查

本项收口时执行：

```sh
python3 -m py_compile \
  tests/differential/run_all.py \
  tests/differential/python/run_scalar_wasmtime_cli.py \
  tests/differential/python/run_scalar_wasmtime_direct.py \
  tests/differential/python/run_geometry_hit_test_wasmtime.py \
  tests/differential/python/run_bezier_curve_wasmtime.py \
  tests/differential/python/run_position_interp_wasmtime.py \
  tests/differential/python/wasm_lldb_runner.py \
  tools/motionsim/generate_tasks_status.py
python3 tools/motionsim/generate_tasks_status.py
git diff --check
```

另对结果TSV硬校验8列、21个唯一`family/case_id`、全PASS、family计数10/6/5，并确认没有改动
spec JSON或expected常量。

## 9. 完成 disposition

- task status：`EVIDENCE_BLOCKED`（终态，但不是 PASS）；
- verified slice：`MP-V03-SCALAR-WASMTIME-CLI-21-OF-21`；
- current-source Wasmtime：21/21 PASS；
- current ADB：0/21 executed，明确环境阻塞；
- expected/spec changes：0；
- production C++ semantic edit：0；
- evidence blocker：本机与最新公开 CI 都没有能执行三个 ADB return-value runner 的 Android
  设备/APK/harness组合；
- 外部条件改变后的最小动作：在已安装harness的rooted arm64设备上依次运行三个
  `run_*_adb.py --spec-dir ...`，保存21行`runner=android-adb-oracle`输出并与本TSV逐例合并。
