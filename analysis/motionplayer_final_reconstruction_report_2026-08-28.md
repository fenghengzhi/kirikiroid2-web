# Motionplayer 最终恢复报告与可重复验收（MP-V16，2026-08-29终态更新）

## 1. 验收结论

`tasks.md` 所定义的 motionplayer 静态恢复分母、本地实现映射、四参考二进制取证、注册契约、
对象/容器/生命周期审计、macOS 原生运行时、ttstr 哈希回归、Web Debug 正式构建以及最终差异清单
已经闭合。163 个 ticket 当前全部为终态：146 个 `CLOSED_STATIC`、15 个 `VERIFIED`、2 个
`EVIDENCE_BLOCKED`。

本报告不把“尽可能 100% 一比一复原”误写成可证明的原始源码逐字符恢复：四个参考文件都是
stripped optimized binaries，编译前 identifier、translation-unit 切分、inline/ICF/dead-strip 前形态
有不可逆信息损失。当前可证明的结论是：`tasks.md` 的静态业务/边界分母已逐项映射到四端语义实体，
本地可执行行为在现有原生测试分母内通过；无法由二进制证明的本地名称继续保留 `_guess`，没有把
推测名字升级成事实。

MP-V04 已完成两份 hash-pinned 15 Hz fixture 的 Android oracle、当前 Wasmtime 与新鲜 macOS native
三侧闭环，25+63 帧在两个 port lane 都是零 mismatch。MP-F08 因而达到 `VERIFIED`。MP-V03 与
MP-V05 仍不能写成 PASS：前者缺当前 scalar ADB 返回值；后者虽然在 GitHub run 540 找到同两份
hash-pinned 15 Hz fixture 的 Android/Wasmtime render artifacts，且当时的 render-step compare 成功，
但该 run 位于 `a514c688...`、没有调用 draw-dispatch comparator，尚未形成当前工作树的完整 paired
render-step/draw-dispatch 结果。两项按本计划允许的终态明确标为 `EVIDENCE_BLOCKED`，不是尚未定位的
共享源码语义差异。

## 2. 最终分母

所有数字均由当前生成器和 TSV 重新计算，而不是手填完成率：

| 分母 | 当前值 | 权威文件 |
|---|---:|---|
| `tasks.md` 唯一 ticket | 163 | `analysis/motionplayer_tasks_status.tsv` |
| MP-A/L/C/D/R/G/B 业务与边界 ticket | 139 | `analysis/motionplayer_v09_v15_audit_summary.tsv` |
| business task-to-slice 关联 | 306 | 同上 |
| 唯一终态四端 business slice | 159 | 同上 |
| coverage rows | 199 | `analysis/motionplayer_coverage.tsv` |
| NCB 基础等价行 | 316 | `analysis/motionplayer_ncb_equivalence.tsv` |
| 唯一注册契约 | 494 | `analysis/motionplayer_registration_contracts.tsv` |
| MP-L/MP-C 生命周期与容器 ticket | 32 | `analysis/motionplayer_v09_v15_audit_summary.tsv` |
| 唯一生命周期/容器 slice | 50 | 同上 |
| 显式 inline/dead-strip/absent/folding coverage rows | 24 | 同上 |
| 唯一 `_guess` identifier / 出现次数 | 943 / 3816 | `analysis/motionplayer_v09_guess_audit.tsv` |
| hex-comment / staleness-marker review | 52 / 9 | `analysis/motionplayer_v10_legacy_token_audit.tsv` |

生成链对缺失任务、重复 slice、空四端 disposition、非终态 business slice、注册行数变化和缺少
契约字段都会 fail closed。最终状态扫描返回 146 `CLOSED_STATIC`、15 `VERIFIED`、2
`EVIDENCE_BLOCKED`，没有 partial/open 行。

## 3. 四参考目标与证据身份

| 目标 | SHA-256 | IDB / slice | 架构与 imagebase |
|---|---|---|---|
| `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | `05e2ff4c77f1561608ad7703153d2fb09855bf223237a85dc2267fff1388564f` | `mp_android_arm64` | ELF AArch64, `0x0` |
| `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | `a15c238ec6f21c17d0889b064ae1ad47ec85b4f1530a3611f206b7190ff456af` | `mp_android_armv7` | ELF ARM EABI5, `0x0` |
| `Kirikiroid2_1.3.9_iOS_arm64` | `733ba5d3fd0798e41ddbac0f0a5b484e7cd20443ee5313781e0e32d1633e18e3` | `mp_ios_arm64` | fat Mach-O arm64, `0x100000000` |
| `Kirikiroid2_1.3.9_iOS_armv7` | `733ba5d3fd0798e41ddbac0f0a5b484e7cd20443ee5313781e0e32d1633e18e3` | `mp_ios_armv7` | 同一 fat Mach-O armv7, `0x4000` |

两个 iOS 路径内容相同，是同一 fat Mach-O；两个 IDB 分析不同 slice，地址从未跨库复用。最终运行时
纠偏前，原生 IDA transport 已恢复，四库均通过 `status=ok` / `hexrays_ready=true` 健康检查。
本轮三条边界分别在四端 fresh decompile，并对关键 wrapper 读取完整 disassembly；随后加入注释和
bookmark，保存并正常关闭四库。详细函数地址、控制流与 Variant 生命周期见
`analysis/motionplayer_runtime_reconciliation_four_binary_2026-08-28.md`。

## 4. 恢复出的结构、数据流与调用链

### 4.1 模块根与脚本表面

四端共同根结构是：

```text
motionplayer.dll static registration
  -> Layer-attached BezierPatch registrar
  -> Motion native class registrar
       -> constants / delayed subclasses / namespace methods

emoteplayer.dll module callback
  -> LoadModule("motionplayer.dll")
  -> global.Motion
  -> Motion.EmotePlayer native class
  -> Motion.ResourceManager
  -> setEmotePSBDecryptSeed / setEmotePSBDecryptFunc

DrawDeviceD3D dependency root
  -> LoadModule("emoteplayer.dll")
  -> resolve D3DLayerObjectNativeInstance class id
  -> publish borrowed D3D native-instance identity
```

316 行 NCB 基础等价表和 494 个最终注册契约固定了 script name、member kind、binding、arity、默认值、
receiver/result 规则和四端位置。MP-V14 的最终原生回归进一步证明这些静态契约已落到本地 wrapper
行为，而不只是表面字符串相同。

### 4.2 资源到一帧绘制的数据流

恢复后的主干可以概括为：

```text
PSB/MTN bytes + decrypt callbacks
  -> ResourceManager load/cache/validate
  -> SourceCache / ObjSource / atlas textures
  -> Player motion/node tree + variables/timeline/selectors
  -> frameProgress phase ordering
       motion -> controllers -> particle/shape/mesh -> layer update
  -> prepared items / command list / sorting / stencil/batch state
  -> ordinary Layer or SeparateLayerAdaptor / D3D adaptor
  -> render manager texture/composite submission
  -> WebGL/Cocos presentation boundary
```

对应报告逐项覆盖 load/unload/cache、node 构造、variable binding、timeline/selector、camera、Bezier、
particle、mesh、layer getter、command build、canvas、D3D texture 与最终 render platform boundary。
MP-V04 的当前跨执行环境 playback trace 已完成；MP-V05 的剩余项是把可复用的 Android 15 Hz oracle
artifact 与当前 capture 做 schema/版本核验并补跑 draw-dispatch，而不是这条静态调用链仍有未映射节点。

### 4.3 对象生命周期与内部容器

32 个 MP-L/MP-C ticket 通过 50 个唯一四端 slice 覆盖：native instance/adaptor 的 owning 与
borrowed 边、Engine/Player/facade 链、构造/析构、AddRef/Release、全局静态 guard/cache/RNG、node
deque、ordered map、parameter table、selector/control deque、resource map/set、atlas/source map、
prepared-item/command containers，以及 callback reentry/clear/replacement 后的 publication 边界。

Android libstdc++ 与 iOS libc++ 的 node/block 形态、32/64 位 slot/pointer 宽度、EH 展开和
inline/ICF/dead-strip 被记为 ABI/compiler disposition；只有可观察共同语义进入 portable C++，没有
照抄平台 STL 私有布局。无法取得 destruction/reentry fault-injection material 的案例已在 MP-V02
显式分类，未用“无测试”等同“无风险”。

## 5. 最终运行时纠偏

最后一次 fresh 四端核对产生或裁决了以下改动：

1. `DrawDeviceManagerItem::IsVisible()`：把可能误选 non-const Object conversion 的
   `static_cast<bool>(value)` 改为显式 `value.operator bool()`，保留 PropGet status、Variant 构造/
   析构和 exception 路径。
2. `Motion.EmotePlayer` Factory：增加无名 `iTJSDispatch2 *` receiver formal，使 NCBind 正确计算
   一个脚本可见 arg0；单 Void sentinel、zero-argc `BADPARAMCOUNT` 和 surplus-ignore 与四端一致。
3. Boolean trigger：四端证明现有 NCBind Void-to-bool 已正确；`selectorEnabled` 失败来自 fixture 未先
   建立 metadata owner。测试改为先走 `resetMetadataState()`，没有增加产品 fallback；诊断期全局
   Boolean converter 已撤回。
4. DrawDeviceD3D headless test：加入未注册、默认 null 的 render-manager override，测试注入 exact-size
   内存 texture 和 no-op composite manager；默认产品仍缓存 named `"opengl"` manager。它消除了
   `glGetIntegerv -> InitGL -> capture` 的无窗口上下文崩溃，而没有掩盖 D3D ownership/脚本表面。
5. Web 正式构建此前揭示的 `tTJSNI_BaseLayer` 前置声明和
   `NCB_TYPECONV_BOXING(SeparateLayerAdaptor)` 模板集成缺口保持闭合；二者不改变 native layout 或
   注册行数。

## 6. 最终验证矩阵

| 任务 | 状态 | 当前结果 |
|---|---|---|
| MP-V03 scalar differential | `EVIDENCE_BLOCKED` | 当前 Wasmtime CLI 21/21；当前 ADB 0/21；本机与最新 CI 均无设备/APK/harness 执行路径，不冒充 PASS |
| MP-V04 playback trace | `VERIFIED` | Android oracle、当前 Wasmtime、新鲜 native 各 88 frames/2418 layer snapshots；两个 port lane 均 0 mismatch |
| MP-V05 render stage | `EVIDENCE_BLOCKED` | 当前 Wasmtime 88 frames/1472 events/176 decoded PNG 全部通过内部审计；run 540 同 fixture paired render-step 成功，但当前工作树 draw-dispatch 跨 lane 未执行 |
| MP-V06 Web Debug | `VERIFIED` | Emscripten 4.0.23 从清空 cache 完成 338/338，全五项产物已记录 SHA-256 |
| MP-V07 native runtime | `VERIFIED` | 357 cases：356 pass、1 expected live-GL skip；declared order 23259/23259、原失败随机 seed 2862347432 为 23260/23260；hash 23/150 全过 |
| MP-V08 diagnostics | `VERIFIED` | `git diff --check` 通过；recovery-only debug 输出 0；六个 baseline/reference `TVPAddLog` |
| MP-V09～V13 | `VERIFIED` | name/token、四端 disposition、全业务 slice、全生命周期/容器分母闭合 |
| MP-V14 | `VERIFIED` | 494 contracts + fresh 四端 runtime reconciliation + native 回归通过 |
| MP-V15 | `VERIFIED` | 最终六行差异清单，无 shared semantic / IDA transport blocker |
| MP-V16 | `VERIFIED` | 本报告与可重复命令已发布 |

Web 固定产物以 `analysis/motionplayer_v06_web_build_products.tsv` 为准；2026-08-29 的 339/339
全量重建后，当前 `index.wasm` 为 85,369,212 bytes，SHA-256
`c45da319e632286689d8453c9219ac7cb759e54a518c2da97ea32ecca72d01cd`。

## 7. 最终差异清单

`analysis/motionplayer_v15_final_differences.tsv` 只有六行：

- 三个 `ACCEPTED_PLATFORM_BOUNDARY`：cursor/skip 的 AArch64 FMAX 与 ARMv7 compare/select 浮点
  指令边界，以及 native GPU 与 WebGL/Cocos presentation/context failure model；
- 一个 `EXPLAINED_NO_COMMON_SOURCE_CHANGE`：pointer width、layout、STL、EH、inline/ICF/dead-strip；
- `VERIFY-001`：当前 scalar ADB lane，终态 `EVIDENCE_BLOCKED`；
- `VERIFY-002`：当前工作树 15 Hz render-step/draw-dispatch paired 结果，终态 `EVIDENCE_BLOCKED`；
  GitHub run 540 的同 fixture Android partner 与成功 render-step 已入账，playback 已从该行移除。

之前的三个 shared-source semantic 行和 IDA transport blocker 行已删除，因为它们已经获得 fresh
四端证据并通过最终回归。平台/ABI 行不是失败；VERIFY 行则继续保留具体解阻动作。

## 8. 两个终态证据阻塞项及外部条件改变后的最小动作

### MP-V03

提供 rooted API 24+ arm64 设备/模拟器和已安装 `krkr2-harness.apk`，执行现有三个 ADB runner，保存
21 行 `android-adb-oracle` 输出并与已经通过的 Wasmtime 21 行逐例合并。

### MP-V05

当前 Wasmtime artifact 已完成。优先使用已认证 GitHub 下载 run 540 的 artifact `9066479201`
（Android）、`9066827131`（Wasmtime）与 `9066864147`（compare report），核验 schema、fixture hash 和
reference APK identity；兼容时把 Android oracle 与当前 capture 补跑 render-step 和 draw-dispatch
comparator。若 schema 或 recorder 契约不兼容，再在 rooted arm64 Android/等价 CI 上重跑当前 pipeline。
旧 60 Hz pair 中的 m2logo execute-post hash 和两 case draw route 差异仍只作诊断基线。

## 9. 可重复检查命令

```sh
# 权威台账与横向审计
python3 tools/motionsim/generate_local_ncb_inventory.py
python3 tools/motionsim/generate_ncb_equivalence_ledger.py
python3 tools/motionsim/generate_registration_contracts.py
python3 tools/motionsim/generate_tasks_status.py
python3 tools/motionsim/generate_final_crosscutting_audits.py
python3 -m py_compile \
  tools/motionsim/generate_tasks_status.py \
  tools/motionsim/generate_final_crosscutting_audits.py

# 本地原生验证
out/macos/debug/tests/unit-tests/plugins/motionplayer-dll --reporter compact
out/macos/debug/tests/unit-tests/plugins/motionplayer-ttstr-hash-test --reporter compact

# Web 正式构建；当前主机需使用可执行、已签名的等价 CMake/Ninja/Bison/Emscripten
cmake --preset "Web Debug Config"
cmake --build out/web/debug
shasum -a 256 \
  out/web/debug/index.html out/web/debug/index.js out/web/debug/index.wasm \
  out/web/debug/vlfs.js out/web/debug/assets.zip

# 当前已实现的 scalar Wasmtime lane
python3 tests/differential/run_all.py --build-only \
  --family geometry_hit_test --family bezier_curve --family position_interp

# MP-V04 当前三侧 playback 复核
python3 tests/differential/python/compare_motion_playback_traces.py \
  --oracle-trace-dir tests/differential/artifacts/motion_playback_android_oracle_20260828_base_head \
  --port-trace-dir tests/differential/artifacts/motion_playback_wasmtime_traces_20260829_v2
python3 tests/differential/python/compare_motion_playback_traces.py \
  --oracle-trace-dir tests/differential/artifacts/motion_playback_android_oracle_20260828_base_head \
  --port-trace-dir tests/differential/artifacts/motion_playback_native_20260829_v2
python3 tests/differential/python/run_scalar_wasmtime_cli.py \
  --wasmtime /path/to/wasmtime \
  --family geometry_hit_test --family bezier_curve --family position_interp

# MP-V05 当前 Wasmtime render artifact 的 fail-closed 完整性复核
python3 tests/differential/python/validate_motion_render_artifact.py \
  --artifact-root \
  tests/differential/artifacts/motion_playback_render_stages_wasmtime_20260829_v2

# 最终完整性
git diff --check
```

四端二进制语义取证的可重复单位不是 grep 结果，而是对应报告中的 target identity、函数地址、
decompile/disassembly、xref/宽字符串字节、IDB 注释/bookmark/save 记录。任何新的产品语义修改都应继续
遵守同一四端门槛；不能因为本报告发布就把后续猜测自动视为证据。
