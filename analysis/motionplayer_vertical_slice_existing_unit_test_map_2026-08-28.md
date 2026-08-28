# motionplayer 已闭合纵切面 → 现有 unit test 映射（MP-V01，2026-08-28）

## 1. 结论

`MP-V01` 已完成。`tasks.md` 中所有 127 个已闭合纵切面任务（A/L/C/D/R/G）都已逐项展开
到其直接 coverage slice，再映射到当前仓库已有的 unit-test TU、具体 `TEST_CASE` 或
translation-unit compile-time contract。权威机器可读产物是：

- `analysis/motionplayer_vertical_test_matrix.tsv`；
- 生成器 `tools/motionsim/generate_verification_matrix.py`。

映射结果不是“只列有测试的条目”：没有现存测试的 ticket/slice 也保留明确的
`NO_EXISTING_TEST` 行，供 `MP-V02` 判断是否能仅用现有 fixture 表达。生成器不创建 fixture、
不把共享回归冒充独立 native oracle，也不因缺少现成物料回退静态实现状态。

## 2. 分母与统计

原始纵切面任务分母：

| 任务族 | 数量 |
|---|---:|
| MP-A | 32 |
| MP-L | 16 |
| MP-C | 16 |
| MP-D | 13 |
| MP-R（含 R03a～R03e） | 26 |
| MP-G | 24 |
| **合计** | **127** |

127 个 ticket 展开为 294 个 ticket-to-slice association，涉及 149 个唯一 coverage slice：

| association disposition | 数量 | 含义 |
|---|---:|---|
| `EXISTING_RUNTIME_TEST` | 190 | coverage implementation已有当前 unit-test行锚点，可解析到具体 `TEST_CASE` |
| `EXISTING_COMPILE_TIME_CONTRACT` | 2 | 只有 unit-test TU 顶部的类型/构造性 static assertion |
| `NO_EXISTING_TEST` | 102 | 该 slice 当前没有 test implementation引用 |
| **合计** | **294** | 每个association恰有一个disposition |

按原始 ticket 汇总：101 个 ticket 至少映射到一个现有 runtime `TEST_CASE`；26 个 ticket 当前
没有任何现有 runtime test。compile-time contract只出现在同时另有runtime test的ticket中，
因此没有“仅compile-time、整ticket无runtime”的额外分类。

## 3. 矩阵列语义

`motionplayer_vertical_test_matrix.tsv` 固定为 8 列：

1. `task_id`：原始 A/L/C/D/R/G ticket；
2. `description`：`tasks.md` 的原始要求；
3. `slice_id`：该 ticket 的直接 coverage slice；
4. `evidence_status`：slice 的 `IMPLEMENTED` 或 `PLATFORM_BOUNDARY`；
5. `mapping_kind`：上述三种 test disposition；
6. `test_cases`：当前 enclosing `TEST_CASE` 名或 compile-time contract标记；
7. `test_locations`：coverage中登记的原始 test file/line引用；
8. `remaining_verification`：后续差分、构建或 fixture决策。

一项 ticket 引用多个 slice 时，每个 slice 独占一行。这避免“某个聚合 ticket 有一个测试”
掩盖其中其他 slice 无测试，也避免把一条共享 test line重复解释成完整 ticket oracle。

## 4. 当前无现存 runtime test 的 26 个 ticket

| ticket | 当前验证缺口类别 |
|---|---|
| MP-A01 | registrar/module重复注册与卸载根 |
| MP-A03 | module lowercase、Plugins.link、autoload与registered set |
| MP-A04 | 双模块完整类/常量/成员枚举 |
| MP-A05 | 本地/参考注册表多、少、错绑、顺序和dead-strip对账 |
| MP-A09 | geometry构造/属性/contains聚合表面 |
| MP-A10 | LayerGetter完整只读表面与Array owner聚合 |
| MP-A21 | Player构造wrapper receiver/publication/rollback |
| MP-A30 | D3DEmotePlayer独立层级、factory、clone和故意TODO |
| MP-A32 | 全注册字符串、arity、默认参数和绑定目标最终对账 |
| MP-L01 | Player构造默认与故意未初始化字段 |
| MP-L02 | Player析构owner逆序与解绑 |
| MP-L03 | Player构造失败的逐publication回滚 |
| MP-L04 | Player payload/adaptor vtable与deleting thunk |
| MP-L05 | EmoteObject→Engine→Player owner链 |
| MP-L06 | EmotePlayer facade与D3D shell owner拓扑 |
| MP-L07 | Engine七个direct-controller owner生命周期 |
| MP-L15 | global RNG/cache/registrar/static guard生命周期 |
| MP-L16 | AddRef/Release、native instance、borrowed与deleting destructor总审计 |
| MP-C13 | TJS Array/Dictionary items和owner handoff聚合 |
| MP-C15 | 全容器empty/duplicate/erase/invalidation/exception横审 |
| MP-C16 | libstdc++/libc++展开归因与共同源码容器选型 |
| MP-D07 | source descriptor/path/source-state构造复制析构 |
| MP-D13 | layer-id require/release suffix/duplicate/wrap边界 |
| MP-G13 | prepared item构造/priority/duplicate/stencil聚合 |
| MP-G23 | Web/Cocos明确平台边界 |
| MP-G24 | 一帧input state→final draw products快照 |

这里的“无现存 runtime test”只表示 coverage中的实现引用没有指向当前测试，不表示静态四端
证据缺失，也不自动要求新 fixture。`MP-V02` 会逐项区分：

- 可以用已有对象/fixture/trace表达，补 test；
- 只能用 compile-time/static ledger验证；
- 需要当前不存在的native/ADB/游戏物料，记录验证缺口并放弃新增 test。

## 5. 现有测试映射的强度边界

矩阵中的现有 test 分为三种实际强度，不能混用：

- **结构/纯函数回归**：直接调用 reconstructed helper/对象，能够锁定容器、数值或owner边界；
- **TJS/NCB集成回归**：通过真实 dispatch、Array、Layer、Plugin surface执行，能观察arity、receiver、
  Variant owner和异常，但仍不是四端native oracle；
- **headless/trace回归**：验证Wasmtime/headless可观察产品，只有与native/ADB trace对照后才是差分证据。

因此 `EXISTING_RUNTIME_TEST` 只表示“已有可执行本地回归”，不把它提升为 `VERIFIED` 的native
等价结论。`MP-V03`～`MP-V05`负责差分，`MP-V06`～`MP-V08`负责正式构建、unit执行和非回归。

## 6. 可重复生成与完整性断言

从仓库根目录运行：

```sh
python3 tools/motionsim/generate_tasks_status.py
python3 tools/motionsim/generate_verification_matrix.py
```

生成器硬性校验：

- 恰有 127 个 `CLOSED_STATIC` 纵切面 ticket；
- 每个 ticket 至少一个直接 coverage slice；
- 所有引用 slice存在且为 `IMPLEMENTED`/`PLATFORM_BOUNDARY`；
- 恰有 294 个 task-to-slice association；
- test location解析只接受 `tests/...:<line>`；
- 输出每行恰有 8 个 TSV 字段。

当前复跑结果为 127/294/149，association disposition总和 190+2+102=294。生成器和矩阵本身
均通过 Python compile/TSV字段检查。

## 7. 完成 disposition

- task status：`VERIFIED`（V01自身的完整映射产物已由生成器复核）；
- C++ semantic edit：无；
- fixture edit：无；
- matrix coverage：127/127 ticket、294/294 association；
- next：`MP-V02`逐项处理102个 `NO_EXISTING_TEST` association和26个无runtime-test ticket。
