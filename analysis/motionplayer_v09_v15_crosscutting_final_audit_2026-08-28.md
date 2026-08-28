# Motionplayer MP-V09～MP-V15 横向最终审计（2026-08-29终态更新）

## 结论

MP-V09～V13 的静态横向分母已经可重复闭合；MP-V14 的 494 行四端注册契约静态分母完整，
三个暴露出的运行时边界也已经完成四端 fresh re-evidence、源码/夹具纠偏和最终原生回归。
MP-V15 现为最终差异清单：共享源码语义 gap 与 IDA transport blocker 已清零，剩余项只有三个
明确平台边界、一个 ABI/STL/compiler 解释类，以及两个归属 MP-V03/MP-V05 的终态外部证据 blocker；
MP-V04 已完成 ADB/native/Wasmtime playback 验证。

| 任务 | 状态 | 分母 | 剩余项 |
|---|---|---|---|
| `MP-V09` | `VERIFIED_STATIC` | 943 unique symbols / 3816 occurrences | None in name hygiene; _guess deliberately remains where the stripped references expose semantics but not source identifiers |
| `MP-V10` | `VERIFIED_STATIC` | 52 hex-comment tokens / 9 staleness-marker comments | None in source token hygiene |
| `MP-V11` | `VERIFIED_STATIC` | 199 coverage rows; 24 rows explicitly mention inline/dead-strip/absent/folding disposition | None; the final runtime reconciliation used fresh evidence from all four reference databases |
| `MP-V12` | `VERIFIED_STATIC` | 139 original business/boundary tasks -> 306 associations -> 159 unique terminal four-target slices | No static denominator gap; MP-V03 and MP-V05 retain explicit terminal external-evidence blockers while MP-V04 is verified |
| `MP-V13` | `VERIFIED_STATIC` | 32 MP-L/MP-C object-owner-container tasks -> 50 unique four-target slices | Runtime destruction/reentry fault injection remains only where MP-V02 recorded unavailable material |
| `MP-V14` | `VERIFIED_RUNTIME` | 494 unique EVIDENCED_4_4 registration contracts | None; final declared-order native result is 357 cases, 356 passed, one expected integration skip, and all 23259 assertions passed; random seed 2862347432 also passes all 23260 assertions |
| `MP-V15` | `FINAL_DIFFERENCE_LIST` | 3 platform boundaries + 1 ABI/compiler disposition + 2 external verification gaps | The list is final for the audited scope; VERIFY-001/002 are terminal EVIDENCE_BLOCKED rows under MP-V03 and MP-V05, not successful Android executions |

## MP-V09：`_guess` 全量审计

源码范围是 `cpp/plugins/motionplayer/` 与 `cpp/plugins/DrawDeviceD3D.cpp`。生成器逐词枚举了
943 个唯一标识、3816 次出现。每一行都明确区分测试专用
label、已证实脚本 binding 的本地 label，以及只有语义/布局证据但没有 source identifier 证据的
label。当前没有一个标识具有足以授权去掉 `_guess` 的四端 source-name 证据，所以全部保留；这不是
把名字猜测提升为事实。

明细：`analysis/motionplayer_v09_guess_audit.tsv`。

## MP-V10：旧地址、旧 helper 与注释

`Like_0x...`、IDA 自动名（`sub_/loc_/off_/unk_...`）和 `@0x...` 裸代码地址均为零命中。
源码注释中的 52 个十六进制 token 已逐行分类为数值/flag 或四端
ABI/layout/size 说明；9 个 TODO/旧实现措辞也逐行标为参考故意 TODO
或当前纠错说明。地址和平台 offset 表继续只存在于 `analysis/`。

明细：`analysis/motionplayer_v10_legacy_token_audit.tsv`。

## MP-V11～V13：四端 disposition、函数/任务分母与对象分母

- coverage 共 199 行，四个平台字段、实现、报告、验证和 gap 字段全部非空，
  slice ID 无重复；其中 24 行显式记录 inline/dead-strip/absent/folding。
- `tasks.md` 的 139 个 MP-A/L/C/D/R/G/B 原始任务全部为 `CLOSED_STATIC`，通过
  306 个 task-slice 关联覆盖 159 个唯一、
  四端终态语义 slice。
- 其中 16 个 MP-L 与 16 个 MP-C 任务通过 50 个唯一 slice 覆盖
  构造/析构、owner、container、vtable 与其边界家族。

这个结论恢复的是 `tasks.md` 定义的静态分母。MP-V06～V08 的本地 build/runtime/diagnostic
验证已经闭合；MP-V03 与 MP-V05 的 Android 直接证据仍以终态 `EVIDENCE_BLOCKED` 明确保留。

## MP-V14：脚本表面

`motionplayer_registration_contracts.tsv` 恰好有 494 个唯一契约，全部
`EVIDENCED_4_4`，script name、kind、binding、argument contract、四端字段与报告均非空。生成链仍会
对 316 行 NCB 基础分母和最终 494 行分母硬失败。

本轮针对原生 suite 暴露的三个边界完成了四数据库复核：

1. DrawDeviceD3D manager item `IsVisible` 必须显式走 `tTJSVariant::operator bool()`；
2. NCBind Boolean wrapper 的 Void-to-bool 路径原本已经与四端一致，失败来自测试未先建立
   `selectorEnabled` 同步所要求的 metadata owner，修复的是 fixture，不是全局 converter；
3. `Motion.EmotePlayer` typed Factory 需要一个隐式 receiver formal，才能恢复脚本可见的 arg0 gate。

最终完整原生 declared-order 结果为 357 cases：356 passed、1 个 live-OpenGL expected skip；
23259 assertions 全部通过。曾暴露全局 TJS class-table 顺序依赖的随机 seed `2862347432` 在 fixture
自持 `ScopedCoreScriptEngine` 后也以 23260/23260 assertions 通过；ttstr hash suite 另有
23 cases / 150 assertions 全部通过。因此 MP-V14 已从静态分母提升为运行时验证完成。

## MP-V15：最终差异清单

`analysis/motionplayer_v15_final_differences.tsv` 最终有六行：三个明确平台边界、一个已解释的
ABI/STL/compiler 类，以及两个终态外部差分证据 blocker。三个共享源码语义行与 IDA transport 阻塞行已经
因 fresh 四端证据、实现纠偏和最终回归而删除。VERIFY-001/002 不妨碍 MP-V15 的清单审计完成，
也不再制造 partial/open 状态；它们明确表示 MP-V03/MP-V05 不是 Android PASS。

## 可重复命令

```sh
python3 tools/motionsim/generate_local_ncb_inventory.py
python3 tools/motionsim/generate_ncb_equivalence_ledger.py
python3 tools/motionsim/generate_registration_contracts.py
python3 tools/motionsim/generate_tasks_status.py
python3 tools/motionsim/generate_final_crosscutting_audits.py
python3 -m py_compile tools/motionsim/generate_final_crosscutting_audits.py
git diff --check
```
