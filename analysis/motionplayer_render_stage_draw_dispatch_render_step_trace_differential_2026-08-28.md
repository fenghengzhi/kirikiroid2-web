# motionplayer render-stage / draw-dispatch / render-step trace差分（MP-V05，2026-08-29终态更新）

> **2026-08-29 CI 实跑纠正：** run `33206772913` 证明提交
> `6a366f37` 接入的 active Android `--stage render_path` 不可执行。当前
> Kirikiroid2 1.3.9 agent 自 `c85306fc` 起明确只支持 `trace_flatten`；文件中
> 保留的 render-stage offsets 属于退役的 1.4.4 lane，尚未对 1.3.9 独立
> rebase。失败 job `98969852903` 在请求七个未 rebase stage 时按设计 fail
> closed。active workflow 已恢复为 Android `trace_flatten` 录制和基础 playback
> compare；Wasmtime render-stage 只保留为单边诊断 artifact。下文第 3 节原先
> 所称的 active paired render pipeline 已被这次实跑证伪，以本纠正为准。

## 1. 结论与状态

`MP-V05` 的最终 disposition 为 `EVIDENCE_BLOCKED`（终态，但不是当前工作树的完整跨 lane PASS）。
当前工作树 Wasmtime 15 Hz render-stage 已完整执行并通过产物内部审计。提交前复核 GitHub 全量
artifact 后还确认：run 540 / commit `a514c688...` 确实保留同两份 hash-pinned 15 Hz fixture 的
Android/Wasmtime `render_path` artifacts，并且当时的 render-step compare 成功。剩余缺口是版本/schema
核验和当前工作树的 draw-dispatch 跨 lane compare，而不是“Android partner 从未存在”：

- `render-stage`：当前 Wasmtime 已按 per-case 15 Hz、hash-pinned XP3 采集；GitHub 有同 fixture 的
  Android artifact，但它来自较早的 port commit；
- `render-step`：比较prepare/commands/build-flow以及execute pre/post、
  updateLayerAfterDraw pre/post和post-draw像素hash；
- `draw-dispatch`：比较route、steps、prepareOk、D3D mode、canvas/update调用和internal assign；
- 三个比较器（含基础playback）保留各自退出码，任一失败即使job失败，同时三个报告都会上传；
- workflow YAML、artifact upload/download配对、参数依赖和相关Python语法已本地验证；
- Wasmtime 两 case 共 88 trace frames、1472 stage events、176 张 1920×1080 PNG；PNG 全部成功解码，
  文件字节数、尺寸和 RGBA SHA-256 与 manifest 逐项一致；
- `draw_dispatch`、`render_prepare`、`render_commands` 在两 case 都非空，基础 playback 仍为 0 mismatch。

不能标`VERIFIED`，因为 run 540 的 compare job 只调用了 playback 与
`compare_motion_render_steps.py`，没有调用后续验收纳入的 `compare_motion_draw_dispatch.py`；而最新基线
run 550 / commit `5cc45b36...` 的 artifact API 又只有 playback oracle、record logs 和 libharness。
本机未认证 GitHub artifact 下载返回 401，也没有 adb/emulator/Docker/APK/harness/frida-server，无法在
本轮把 run 540 Android oracle 做 schema 核验后与当前 capture 重新配对。旧 60 Hz pair 同样没有被
冒充为当前证据。

## 2. 发现的流水线缺口

提交前复核得到的准确时间线是：

1. 2026-08-10 的 active `.github/workflows/differential.yml` 已采集 per-case 15 Hz Android/Wasmtime
   render-stage，并运行 render-step comparator；
2. run 540 成功上传 oracle/Wasmtime render artifacts 和 compare report；
3. 该版本尚未调用 `compare_motion_draw_dispatch.py`；
4. 到最新基线 run 550 时，公开 artifacts 又只剩 normalized playback；
5. 当前工作树重新接入 render-step 与 draw-dispatch 两个 comparator，并保留独立退出码。

先前只检查 run 550 和 2026-05-30 disabled workflow，遗漏了 run 540；“GitHub 上不存在 15 Hz
Android partner”的结论已撤回。

## 3. 已被 CI 证伪并撤回的 paired render workflow 接线

提交 `6a366f37` 曾尝试以下 CI/测试接线，但它把旧 1.4.4 render offsets
误用于当前 1.3.9 oracle，因此不再是 active workflow：

### Wasmtime job

每个`yuzulogo`/`m2logo`仍使用当前工作流已校验SHA-256的独立15 Hz XP3，向现有runner追加：

```text
--record-render-stages
--record-render-step-checkpoints
--checkpoint-render-only
--render-artifact-dir .../motion_playback_render_stages_wasmtime
```

两个case写入同一artifact root，由现有manifest merge逻辑合并；随后上传`manifest.json`和`events/**`。

### Android job（已撤回）

正常playback oracle成功后，逐case重置app/forward/logcat，再调用现有
`run_motion_stage_oracle.py`：

```text
--startup-xp3 <同一case的15 Hz XP3>
--case <case>
--stage render_path
--record-render-step-checkpoints
--checkpoint-render-only
```

它使用已经运行的Redroid、harness和Frida，不引入新的binary fixture。

### Compare job（render 部分已撤回）

compare job新增两组artifact download，并依次运行：

1. `compare_motion_playback_traces.py`；
2. `compare_motion_render_steps.py --allow-render-flow-diagnostics --ignore-layer-save`；
3. `compare_motion_draw_dispatch.py`。

三个退出码独立保存；所有stdout/stderr分别形成playback Markdown、render-step text和draw-dispatch text，
即使比较失败也由`if: always()`上传。`allow-render-flow-diagnostics`只把build-flow字段差异降为diagnostic，
不会忽略stage shape或图像checkpoint；`ignore-layer-save`改用直接render checkpoint，避免fixture
saveLayerImage路径冒充renderer输出。

## 4. 当前 Wasmtime 执行与产物审计

当前 full guest 由 uv x86_64 CPython 3.11 驱动 Emscripten 重建，Wasm SHA-256 为
`8e86bc31b8b8f3f62272131fd907aff78b27227a05b294412406eafe0f488faf`。由于诊断 scope 原本只定义未接线，
第一次 local artifact 的 draw/prepare/command stage 为空；随后仅在 `KRKR2_WASMTIME_HEADLESS` 下把
`Player::draw`、prepare/build/execute 和 update checkpoint 连接到既有 trace API，产品 Web/Android/iOS/
普通 native 语义不变。最终 manifest：

- 路径：`tests/differential/artifacts/motion_playback_render_stages_wasmtime_20260829_v2/manifest.json`；
- SHA-256：`7855146efdf61fe7d5c94e52edc2de3099e7165e5850f526a36b0821190c0642`；
- m2logo：25 帧、464 events、50 PNG；
- yuzulogo：63 帧、1008 events、126 PNG；
- 合计：88 帧、1472 events、176 PNG；
- draw-dispatch：352 events；render-prepare：352；render-commands：416；
- 当前 fixture 走 `separate_layer_adaptor`，因此 ordinary canvas 的 update checkpoint 为空是实际路由
  disposition，不再把空的 draw/prepare/command 主阶段视为成功。

已执行检查：

- 用独立 fail-closed validator 复核 manifest、PNG path-set/解码/尺寸/文件长度/RGBA SHA-256、
  case/frame 序列、1472 个 event envelope/kind summary，以及五个必需阶段非空：
  `python3 tests/differential/python/validate_motion_render_artifact.py --artifact-root
  tests/differential/artifacts/motion_playback_render_stages_wasmtime_20260829_v2`，结果
  `PASS: cases=2 frames=88 events=1472 images=176`；
- 用PyYAML解析当时的 workflow 只能证明 YAML 语法有效；run `33206772913`
  随后证明其中 Android render-stage 能力契约无效；
- 硬校验两组render artifact name各有一upload、一download；
- 硬校验render-step和draw-dispatch comparator各只接线一次；
- `py_compile`四个capture/compare Python入口；
- 两个compare CLI的参数依赖由代码路径和`--help`检查；
- 176 个 PNG 的完整 decode、RGBA hash、尺寸、文件字节数和 manifest path-set 审计；
- 1472 个 event 的 schema/source/stage/case/frame range/kind summary 审计；
- 两份 render-capture playback port trace 对 Android playback oracle：25/25 与 63/63 PASS；
- `git diff --check`。

本轮仍未执行：

- run 540 Android artifact 的认证下载、schema/identity 完整复核；
- Android/Wasmtime current-pair render-step compare；
- Android/Wasmtime current-pair draw-dispatch compare。

因此 Wasmtime capture 本身是直接执行证据，但它不能证明 Android/Wasmtime render 语义相等。

## 5. GitHub run 540 的 15 Hz paired render 证据

全仓库 Actions artifact API 显示，run `31397645808`（run 540）、commit
`a514c6889f18a306789eef1fe6c32a1bc92f6479`、2026-08-10 同时保留：

- Android `motion_playback-oracle-render-stages`：artifact `9066479201`，943998 bytes，
  digest `a73082f0795c9a83a756ed00dbfd8718de78e5313e4b988d79ff25b4f4e7017b`；
- Wasmtime `motion_playback-wasmtime-render-stages`：artifact `9066827131`，706120 bytes，
  digest `9cd60bd4e4ba029ee0e5c28aac98349e934b19c8cf2092c32088ab2b7529cf04`；
- compare report：artifact `9066864147`，836 bytes，
  digest `ce2aacdf1f5ad3a72acbcf422ef10be4286f80eff35175e21b043b38abe856de`。

该 commit 的 workflow 明确 pin 当前相同的两份 fixture SHA-256：yuzulogo
`038bab92728c80e917ed55224aa8c96cc04474d112d8a0e42730d908acd7e3df`、m2logo
`704729939ceabc2d93388cdc8b980bfd79506bedf660d0911c3dd13e9f78cb94`；Redroid 启动参数和运行时
property 都强制为 15 FPS，`test_motion_timing.py` 还验证 15 Hz 虚拟 tick。compare job
`93489361442` 的 artifact 下载、`Compare fresh motion_playback traces and PNG hashes` 与 report upload
全部 `success`，而 workflow 会在 playback 或 render-step 任一 comparator 非零时失败。因此 run 540
是有效的同 fixture 15 Hz paired render-step 证据。

限制也必须保留：它属于较早的 port commit，compare job 没有调用 draw-dispatch comparator；未认证
artifact ZIP 下载返回 401，所以本轮只验证公开元数据、workflow 契约和 job step 结论，没有声称已经
把其 Android events 与当前工作树 artifact 逐文件重跑。

## 6. 更早的 60 Hz 同批次历史 render artifact

更早一批被本地下载并逐文件分析的 60 Hz Android/Wasmtime render artifact 来自 run `26676943270`，commit
`46c0c10c061353539a01916e6f622d4b5e9b73e8`，日期2026-05-30。三份ZIP的本地SHA-256与GitHub
API digest完全一致：

- oracle render stages：
  `d58602820defcf11079af72ae4e21367898f9bb037b308067dae3de8d1419a4d`；
- Wasmtime render stages：
  `ecf01b40c5e2f77e8844e1735523bf08b2ef6ee2c12d04db48856c75778c96ef`；
- compare report：
  `efc8587c2b44dcd013c2809d023a1dd189663d7b07fbf273bc7bf12352d9ce7f`。

该run使用旧combined fixture和16.6666666667 ms delta（60 Hz），不是当前per-case 66.6666666667 ms
（15 Hz）契约。旧commit到当前HEAD在相关路径已有133个文件、42362行新增、21017行删除，其中
motionplayer目录82个文件变化，故只能作历史诊断。

## 7. 60 Hz 历史诊断没有被掩盖

同批次旧报告显示：

| case | contemporaneous render-step结果 |
|---|---|
| m2logo | FAIL：93帧中55个`execute_post.rgbaSha256`不一致，首差在frame 0 |
| yuzulogo | PASS：243帧，所有已启用render checkpoint一致 |

旧workflow没有调用draw-dispatch comparator。本轮用当前
`compare_motion_draw_dispatch.py`只读分析这对同批次旧artifact，得到：

| case | draw leaves | first mismatch |
|---|---:|---|
| m2logo | 93 | oracle route=`ordinary_layer`，Wasmtime=`separate_layer_adaptor` |
| yuzulogo | 243 | oracle route=`ordinary_layer`，Wasmtime=`separate_layer_adaptor` |

这两个结果说明V05不是形式验收：历史链上确实存在render image和draw routing差异。它们被TSV标为
`FAIL_NONCURRENT`，既不因时间久远而删除，也不直接宣判当前源码失败；当前15 Hz rerun才有权关闭。

## 8. 完成 disposition

- task status：`EVIDENCE_BLOCKED`（终态，但不是 PASS）；
- slices：`MP-V05-WASMTIME-RENDER-STAGE-15HZ`、`MP-V05-GITHUB-RUN540-ANDROID-RENDER-15HZ`、
  `MP-V05-CURRENT-DRAW-DISPATCH-EVIDENCE-BLOCKED`；
- active pipeline：Android 只启用 `trace_flatten`；paired render pipeline 为
  `EVIDENCE_BLOCKED`，必须先把 render offsets 独立 rebase 到 1.3.9；
- current Wasmtime artifact：PASS_ARTIFACT_AUDIT，88 frames / 1472 events / 176 PNG；
- GitHub 15 Hz Android artifact：run 540 存在，contemporaneous render-step compare PASS；
- current-worktree paired draw-dispatch runs：0；
- historical diagnostics：m2logo render-step FAIL、yuzulogo render-step PASS、两case draw-dispatch FAIL；
- production semantic edit：0；
- blocker 已精确固定：先以认证方式下载 run 540 artifacts，核验 schema/reference identity 后将 Android
  oracle 与当前 capture 补跑 render-step/draw-dispatch；若不兼容，再在 rooted arm64 Android 或等价 CI
  重跑当前 pipeline。完成当前配对前不得虚构 draw-dispatch PASS。
