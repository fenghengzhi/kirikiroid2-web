# MotionPlayer 编译源码注释 provenance 清理（四参考，2026-08-16）

## 1. 结论

本纵切面不改变对象布局、控制流或可观察行为；它修正会把后续恢复工作带回旧移植阶段的
编译源码注释。清理范围仅限已经有当前四参考纵切面闭合的事实：

- `MotionNode::drawnThisFrame` 的真实生产/消费链；
- Player layer-id 的 no-name、ResourceManager dispatch 边界；
- Player node deque/raw-label map 的当前 build-side view；
- Headless render snapshot 与生产 Player 状态的隔离；
- EmoteEngine typed deque families 已完成的当前结构；
- 不属于当前类型布局的历史 pseudo-fields/parallel animator buckets/payload caches。

旧 `libkrkr2.so` 地址、P3-B/A6/A8/A10 阶段号、过期 cleanup review 和删除日期不再出现在
这些 compiled-source 注释中。绝对地址仍只保留在 `analysis/` 的四参考证据里。

## 2. `drawnThisFrame`：原注释已被当前实现证伪

旧 `MotionNode.h` 注释声称该 byte “currently has no port consumer”并仅供后续 Phase 4
differential oracle 使用。当前源码与四参考证据都已证明相反：

1. priority-selected node 在本轮处理入口被清零；
2. ordinary/type-3/particle admission 在各自的 native publication point 写 1；
3. prepared main-list 的后段过滤读取它；
4. type-12 stencil-composite pass 读取当前 node 及 mask target node 的该 byte；
5. motion trace 与 Wasmtime differential 输出只是额外观察者，并非唯一消费者。

四参考入口与 store 见
`analysis/motionplayer_prepared_ordinary_admission_publication_four_binary_2026-08-14.md`：

| 目标 | recursive builder | ordinary early store |
|---|---:|---:|
| Android arm64 | `0x6BF714` | `0x6C06B8` |
| Android armv7 | `0x58B178` | `0x58B3F4` |
| iOS arm64 | `0x1001148F8` | `0x100114BC8` |
| iOS armv7 | `0x1123D8` | `0x112598` |

stencil consumer 见
`analysis/motionplayer_stencil_composite_render_items_four_binary_2026-08-14.md`：

| 目标 | type-12 eligibility | mask-node loop |
|---|---:|---:|
| Android arm64 | `0x6C0B20` | `0x6C0BE0` |
| Android armv7 | `0x58BB22` | `0x58BB8A` |
| iOS arm64 | `0x1001153F8` | `0x1001154C8` |
| iOS armv7 | `0x112D76` | `0x112E16` |

新注释因此直接描述 clear → admission publication → main/stencil consumption 数据流，
不再保留已失真的项目阶段判断。

## 3. layer-id 注释改为当前调用链

`Player.h` 与 `PlayerResource.cpp` 原注释仍以 P3-B 子任务、删除日期、Android Player
物理偏移和全仓 grep 过程描述当前 API。它们已替换为跨 ABI 的源级事实：

- Player 没有 by-name layer-id allocation route；
- prepared-item require-layer latch 触发 fresh id；
- allocation/release 都经 retained ResourceManager dispatch 的 NCB 方法；
- Player 不通过 cached native ResourceManager pointer 绕过 TJS 调用边界。

四份当前参考的三类 `requireLayerId` producer 与唯一 `releaseLayerId` reset caller、临时
Variant/dispatch owner 范围及异常 Release 路径，已完整记录在
`analysis/motionplayer_layer_id_dispatch_owner_lifetimes_four_binary_2026-08-16.md`。

Headless `_renderLayerStates` 注释也改为正向说明：它是 differential-only snapshot map；
生产 Player 不存在 name-to-id/id-to-name maps，普通 layer id 来自无参数 RM allocator。

## 4. container/layout 注释改为正向当前结构

本轮删除或改写以下源码考古残留：

- `A8 / A9 temporary ... A10 cleanup review`：改为 split-TU build helpers 对持久 node
  deque/raw-label map 的 mutable view，并明确 stable-address/raw-label invariant；
- `TJS variant slots (Phase A6)` 与 `Node tree ... (Phase A8)`：改为当前字段角色标题；
- EmoteEngine 顶部“typed deques are being migrated individually”：改为十个 typed deque
  families 已恢复的 ownership/publication/exception-prefix 事实；
- Engine `_bindListHead` pseudo-field、parallel animator tombstone、Player duplicate value map、
  legacy animator alias、MotionNode payload-cache 删除日期与 PlayerCore dead-helper tombstone；
- `PlayerUpdateLayerEval` 的 cache-removal过程注释：改为当前 dirty publication/consumption
  规则。

这些删除项本就不是当前字段或函数；继续把旧推测名留在 compiled source 容易让搜索结果
误判为尚存结构。其历史纠偏依据仍保存在对应 `analysis/motionplayer_*four_binary*.md`，没有
丢失逆向证据。

## 5. 有意保留的 `phase`/historical 文字

残留扫描后保留的主要类别都描述运行时语义而非项目迁移状态：

- `updateLayers` Phase 1/2/3 是当前函数拆分和真实执行顺序；
- blink phase 0/10/11/12 是控制器状态机值；
- `queing`/`clear` 的 historical wording 记录参考二进制真实脚本拼写；
- constructor historical ordering、superseded TJS Array owners、STL register residue 等是
  当前生命周期/ABI 边界。

因此不能为了“清零关键词”把这些有效说明一并删除。

## 6. Recovery IDB 状态

本纵切面只迁移 portable compiled-source 注释，没有产生新的二进制语义、函数名或地址
发现；上述 producer/consumer 与 layer-id owner 证据已经在先前纵切面写入并保存于四份
recovery IDB。本轮没有用本地项目阶段号覆盖 IDB 中的四参考语义注释。

## 7. 验证

- 普通 Web 与 `KRKR2_WASMTIME_HEADLESS=1` 两种配置下，完整
  `motionplayer-dll.cpp` Emscripten syntax-only 检查均通过；
- `cmake --build --preset "Web Debug Build"` 成功重编 35 个步骤并链接最终
  `index.html/index.wasm`；
- `cmake --build --preset "Wasmtime Headless Debug Build" --target motionplayer`
  成功重编 32 个步骤并链接 Headless `libmotionplayer.a`；
- 诊断仅有仓库既存的 `_tss`、imagepacker attribute、pthread memory-growth、JSPI 与
  Emscripten JS-library warnings；
- scoped `git diff --check` 与新分析文档 trailing-whitespace 检查通过；
- 全 `cpp/plugins/motionplayer` 扫描确认 P3-B、Phase A6/A8、A8/A9/A10 cleanup、
  `currently has no port consumer`、`Removed 2026-*` 和 `being migrated` 均为零；剩余
  `Phase 1/2/3` 与 blink phase 都属于第 5 节列出的运行时语义。
