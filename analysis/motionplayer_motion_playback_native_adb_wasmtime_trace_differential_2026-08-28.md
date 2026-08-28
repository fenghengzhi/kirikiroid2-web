# motionplayer native / ADB / Wasmtime playback trace差分（MP-V04，2026-08-29终态更新）

## 1. 结论与状态

`MP-V04` 当前为 `VERIFIED`。Android 1.3.9 ADB oracle、当前工作树 Wasmtime full guest 和当前工作树
macOS native runner 已在相同两份 hash-pinned 15 Hz fixture 上形成三侧闭环：m2logo 25 帧、yuzulogo
63 帧；Wasmtime 与 native 两个 port lane 均经独立 comparator 对 Android oracle 得到零差异。

本轮取得的当前证据：

- HEAD：`5cc45b36c8ea64aa7ef710846ffc956efe02c3e9`；
- ADB/Redroid Android 1.3.9 oracle：m2logo 25帧、yuzulogo 63帧，共88帧；
- 归一化layer snapshot：843 + 1575 = 2418个；
- GitHub artifact ZIP SHA-256与API元数据逐字一致；
- 两个JSON通过frame连续性、精确字段集、layer index、opacity范围与有限浮点值检查；
- 两份 fixture 的 SHA-256 分别为 `704729...cb94` 和 `038bab...e3df`；
- 当前 Wasmtime guest 使用 uv 管理的 x86_64 CPython 3.11 驱动 Emscripten 完整重建，Wasm SHA-256
  为 `8e86bc31b8b8f3f62272131fd907aff78b27227a05b294412406eafe0f488faf`；
- Wasmtime direct full-guest：25/25 与 63/63 PASS；
- 新鲜 macOS native LLDB：25/25 与 63/63 PASS；
- 两个 port lane 都通过 frame 连续性、layer index/schema 和独立 full comparator。

机器可读状态在`analysis/motionplayer_v04_trace_artifacts.tsv`。Android捕获成功不等于port通过；旧
commit的port trace即使能与当前Android JSON零差异，也不会被冒充成当前源码验证。

## 2. 当前HEAD Android oracle

公开run与job：

- workflow run：`https://github.com/fenghengzhi/kirikiroid2-web/actions/runs/32971807749`；
- ADB job：`https://github.com/fenghengzhi/kirikiroid2-web/actions/runs/32971807749/job/98187268290`；
- artifact id：`9608002821`，name `motion_playback-oracle-traces`；
- API公布archive digest：
  `aab64d950c7f31a35f42187a6b8b8e6531f5ab15491de45427cebb9c1f725d12`。

GitHub的匿名artifact download端点返回401。本轮通过公开artifact镜像下载同一run/name压缩包；本地
SHA-256恰为上述API digest，证明下载内容与GitHub登记的不可变artifact相同。压缩包只在临时目录
解压，不把大体积oracle JSON提交为仓库golden。

| case | frames | layer snapshots | layer count/frame | JSON SHA-256 |
|---|---:|---:|---:|---|
| m2logo | 25 | 843 | 31～39 | `49937cd9cd1ac2c42881e05b09d069a37d04c52f2a296b14983bc70fb550e65f` |
| yuzulogo | 63 | 1575 | 固定25 | `ae61b8ca60d004a3ce8cce63b2ec78629db2ab6c787b1febd28e1199bd243113` |

每个disk frame恰为`{frame,layers}`，frame从0连续递增；每个layer恰有当前归一化schema的18字段，
index与数组位置一致、opacity在0..255、八个浮点字段全部finite。这里验证的是已归一化disk artifact；
raw Frida capture的`projection/samplePoint/diagnostics`严格检查发生在CI归一化之前，不能对disk JSON
重复调用raw validator。

## 3. 当前工作树 Wasmtime 侧

GitHub jobs API给出同一HEAD的逐step结果：

| step | 结果 | 时间 |
|---|---|---|
| Verify 15 Hz timing contract | success | 2026-08-26 13:05:52Z |
| Build Wasmtime full guest | success | 13:05:52Z～13:52:26Z |
| Run motion_playback Wasmtime LLDB guest-debug | failure | 13:52:26Z～13:53:30Z |
| Upload Wasmtime traces | skipped | 无port artifact |

上述表保留为 2026-08-26 CI 失败史，不再代表当前状态。本机随后取得两份精确 fixture，并修复了
Wasmtime host 的超大线性内存字符串读取、continuous-event pump、x86_64 LLDB guest ABI 与直接 trace
导出路径。最终 current-worktree 产物为：

| case | frames | layer snapshots | port JSON SHA-256 | compare |
|---|---:|---:|---|---|
| m2logo | 25 | 843 | `3a9f49233b9cc164058cba4ad207885db100896b08a82abd264ef37fa053ad72` | PASS |
| yuzulogo | 63 | 1575 | `23600a0ac9ad285c44220172acfc1cc18313875acff93f612de92d440e770d6a` | PASS |

权威路径为 `tests/differential/artifacts/motion_playback_wasmtime_traces_20260829_v2/`。

## 4. 当前工作树 native 侧

2026-08-29 已取得并验证 CI 消费的两份既有外部物料：

- `reference/xp3/logo_test_oracle_yuzulogo_15hz.xp3`，期望SHA-256
  `038bab92728c80e917ed55224aa8c96cc04474d112d8a0e42730d908acd7e3df`；
- `reference/xp3/logo_test_oracle_m2logo_15hz.xp3`，期望SHA-256
  `704729939ceabc2d93388cdc8b980bfd79506bedf660d0911c3dd13e9f78cb94`。

`MacOS Debug Config` 重新 configure 后构建了
`out/macos/debug/tests/differential/native/motion_playback_native`，SHA-256
`3d796520012cc7699c105184b14b6a98421ac97a2aa35b092d2f3c060b31d787`。原 tracer 在单个
`Player::progress` 返回时过早提交一帧，导致同一 continuous delivery 后半段发布的新 active Player
丢失；改为以 `_TVPDeliverContinuousEvent` 为采样生命周期、在同一 delivery 中取完整度最高且最晚的
候选后，10 帧 smoke 与正式全量均逐帧闭合。

| case | frames | layer snapshots | port JSON SHA-256 | compare |
|---|---:|---:|---|---|
| m2logo | 25 | 843 | `668ef04a06cdb610df068e36566af4921bfffa2ed47fe28cb065e9cee16a1c3d` | PASS |
| yuzulogo | 63 | 1575 | `5aa3645cbc2282918e029c2e4f70b1e2efe2fe65bb51dc0277d11454fc6a379e` | PASS |

权威路径为 `tests/differential/artifacts/motion_playback_native_20260829_v2/`。

## 5. 历史trace只作基线

最近一个三侧compare成功的run `31414984370` 对应旧commit
`4cf372772edabb60e59324f2b5e0e2ddb16dae32`。本轮下载并校验了其三个artifact：

- Android oracle ZIP：`5cef8a7da581fa3ce43b8d4437b230863063f1cf895df2d1e9ede676e378b673`；
- Wasmtime port ZIP：`d5398ed2fbe476afc7f99228e174f15df07c74218ef35e353cd564b9cb65dfc5`；
- compare report ZIP：`6580791567d2e5595ca27551d8dff48bcbaa4c6c745018c97b7e1328dc046568`。

值得记录的是：旧run的两个Android oracle JSON与当前HEAD刚录制的两个JSON均byte-for-byte相同；用
当前comparator把旧port trace与当前oracle比较，m2logo 25帧和yuzulogo 63帧仍为0 mismatch。它证明
reference输入和Android投影稳定，但不能验证当前port：旧commit到HEAD在本项相关路径有88个文件、
23748行新增、22713行删除，其中motionplayer目录81个文件发生变化。

因此TSV将该结果明确标为`HISTORICAL_PASS_NOT_CURRENT`。

## 6. 已执行的最终检查

```sh
python3 tests/differential/python/test_motion_timing.py
python3 tests/differential/python/test_motion_playback_strict_oracle.py
python3 tests/differential/python/compare_motion_playback_traces.py \
  --oracle-trace-dir tests/differential/artifacts/motion_playback_android_oracle_20260828_base_head \
  --port-trace-dir tests/differential/artifacts/motion_playback_wasmtime_traces_20260829_v2
python3 tests/differential/python/compare_motion_playback_traces.py \
  --oracle-trace-dir tests/differential/artifacts/motion_playback_android_oracle_20260828_base_head \
  --port-trace-dir tests/differential/artifacts/motion_playback_native_20260829_v2
```

结果：timing 7/7、strict-validator unit 3/3通过；Wasmtime/Android 2/2 PASS；native/Android 2/2
PASS。另执行 artifact digest、JSON schema、连续 frame、layer index 与有限浮点检查；未修改 spec、
frame count 或 Android oracle。

## 7. 完成 disposition

- task status：`VERIFIED`；
- verified slices：`MP-V04-ADB-ORACLE-88-FRAMES`、`MP-V04-WASMTIME-88-FRAMES`、
  `MP-V04-NATIVE-88-FRAMES`；
- current ADB：2 cases、88/88 frames captured；
- current Wasmtime：2 cases、88/88 frames、2418 layer snapshots、0 mismatch；
- current native：2 cases、88/88 frames、2418 layer snapshots、0 mismatch；
- production semantic edit：0；新增接线均受 `KRKR2_WASMTIME_HEADLESS` 约束或位于差分工具；
- fixture/spec/oracle edit：0；
- remaining gap：无。
