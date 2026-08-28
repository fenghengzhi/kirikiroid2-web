# motionplayer 现有 fixture 补测与验证缺口决策（MP-V02，2026-08-28）

## 1. 结论

`MP-V02` 已完成。输入分母是 `MP-V01` 矩阵中全部 102 个 `NO_EXISTING_TEST`
task-to-slice association（55 个唯一 slice），不是只挑容易测试的条目。每一行都已得到以下
一种明确 disposition，并写入：

- `analysis/motionplayer_fixture_decisions.tsv`；
- 生成器 `tools/motionsim/generate_fixture_decisions.py`。

本轮只使用仓库已经存在的 unit-test TU、对象构造器、TJS/NCB fixture和trace工具，没有创建
新的PSB/MTN/XP3/ADB/native oracle物料。稳定且原来确实没有直接用例的geometry facade边界新增
一个测试；其余可表达项复用并重新链接已有测试，不能表达的项保留具体原因而不伪造fixture。

## 2. 102 行决策统计

| fixture disposition | association数 | 处理 |
|---|---:|---|
| `REUSED_EXISTING_FIXTURE` | 91 | 复用当前TU中的具体 `TEST_CASE`，由生成器按名称解析当前行号 |
| `ADDED_EXISTING_FIXTURE_TEST` | 5 | 5个geometry slice共同映射到本轮新增的1个测试 |
| `NO_EXISTING_FAILURE_INJECTION_FIXTURE` | 2 | Player/NCB构造逐publication失败注入不存在，记录缺口 |
| `STATIC_ONLY_NO_RUNTIME_OBSERVATION` | 2 | vtable/deleting thunk与外部STL ABI归因只能做静态四端审计 |
| `LOCAL_PLATFORM_BOUNDARY_REGRESSION` | 1 | 复用3个Web/local renderer测试守护明确平台边界 |
| `REQUIRES_TRACE_DIFFERENTIAL` | 1 | 完整一帧产品必须由native/ADB/Wasmtime trace完成 |
| **合计** | **102** | **全部有 disposition** |

97/102行映射到至少一个当前 `TEST_CASE`；5行没有测试名，而且全部属于明确的“不造fixture”
或后续差分类型。55/55个唯一slice和102/102个association均由生成器硬校验。

## 3. 本轮新增测试

新增：

```text
geometry facades preserve type-only defaults and initialized shared getters
```

它只使用现有 `tests/unit-tests/plugins/motionplayer-dll.cpp` 和现有类型：

1. 默认构造 `Point/Circle/Rect/Quad`，只读取四端明确写入的 type word `0/1/2/3`；
2. 对default Point调用 `contains`；type 0直接返回false，不读取15个double；
3. 用显式清零并填值的 `HitData` 构造四种facade；
4. 检查Point x/y、Circle x/y/r与圆边界、Rect l/t/w/h和半开边界、Quad内外点；
5. 不读取默认构造的任何coordinate，不断言allocator/residue字节。

该单一测试覆盖本轮决策矩阵中的5个新增association：四个geometry subclass注册/构造slice和
共享geometry scalar slice。`MP-B03-GEOMETRY-CONTAINS`另复用既有NaN/quad方向测试，不重复造例。

## 4. 91 行复用既有 fixture

coverage实现路径此前没有把这些聚合/表面/lifecycle slice直接链接到测试，但当前TU已经包含
可用用例。生成器按完整 `TEST_CASE` 名称解析行号，避免把新增测试导致的行号漂移固化进
决策逻辑。主要复用组如下：

| slice族 | 代表测试 |
|---|---|
| registrar/module/autoload | `Plugins link...`、`indexed and registered...`、`autoload passes...` |
| NCB完整表面 | `Motion root NCB surface...`、`DrawDeviceD3D exposes the seven-class...` |
| Player/Emote/D3D constructor surface | `Motion.Player NCB constructor...`、`Motion.EmotePlayer typed Factory...`、`D3DEmotePlayer typed factory...` |
| SourceCache/ObjSource/ResourceManager | 三个NCB constructor测试、resource chain、raw-holder cache测试 |
| LayerGetter | null-node NCB constructor、live non-owning facade、shape/particle lookup测试 |
| Player direct/root state | completion/mask/preview、independentLayerInherit、outsideFactor/speed测试 |
| MotionNode/prepared item | value construction、shallow copy、suffix erase、prepared recursion/priority测试 |
| controller/global/refcount | direct controller、RNG、decrypt closure、SLA owner order、D3D reentry测试 |
| container/Array | prepared sort、selector raw gate、SourceCache list、fresh TJS Array测试 |
| render platform boundary | software atlas update、KRKR atlas upload、TriangleBatch cache-key测试 |

这里的“复用”不提升为native差分oracle；它只证明当前fixture已经能守护本地可表达的结构/边界。
每个association的完整test名称、当前文件/行和remaining gap都在TSV中，不由本报告再复制294行。

## 5. 明确不新增测试的5行

### 5.1 MP-L03的两行构造失败前沿

- `MP-C18-PLAYER-NATIVE-CTOR-DTOR-OWNER-LEDGER`；
- `MP-L11-PLAYER-CTOR`。

要求逐个触发Dictionary创建/赋值、descriptor PropSet、root deque allocation和NCB attachment失败。
当前fixture没有确定性allocator/new-expression/NCB attach失败注入，也没有现成native oracle物料。
用自造fake allocation层会改变对象和异常模型，违反“没有物料不造fixture”，所以只记录缺口。

### 5.2 MP-L04 non-polymorphic payload/vtable/deleting thunk

该slice的关键产品是四端type/vtable/thunk与ABI disposition。当前单一host test不能安全调用foreign
deleting thunk，也不能用本地vtable布局替代四端。保留静态四端ledger；不新增伪runtime测试。

### 5.3 MP-C16 libstdc++/libc++ source attribution

本机一次unit进程不能同时实例化Android旧libstdc++、iOS libc++ 64/32四种容器ABI。已有容器
行为测试能守护共同源码选择，但不能验证foreign lowering。该项的有效材料仍是四端完整反汇编。

### 5.4 MP-G24完整一帧产品

完整input state→final draw products需要同一fixture的native/ADB/Wasmtime trace；拆成若干unit值会
丢失顺序、owner和render product关系。已有trace工具留给 `MP-V04/MP-V05`，不在V02捏造快照。

## 6. G23平台边界处理

`MP-G23` 不是“无测试所以平台边界”。其技术原因已由独立四端报告证明。本轮只复用：

- software texture atlas sub-rect update；
- KRKR production atlas build/upload；
- TriangleBatch asymmetric cache key。

这些fixture守护Web/local适配层，不把无法表达的per-vertex/native renderer能力改标为已等价。

## 7. 可重复生成

```sh
python3 tools/motionsim/generate_tasks_status.py
python3 tools/motionsim/generate_verification_matrix.py
python3 tools/motionsim/generate_fixture_decisions.py
```

生成器校验：

- 输入恰为V01的102个 `NO_EXISTING_TEST` association；
- 唯一slice恰为55个；
- 每个task/slice有decision，L03两行使用task-specific override；
- 每个复用/新增测试名必须在当前TU唯一存在；
- 输出恰为8列；
- 不存在未分类association。

## 8. 完成 disposition

- task status：`VERIFIED`；
- existing material policy：满足，未创建fixture/asset/oracle；
- test changes：1个稳定geometry facade测试；
- decision coverage：102/102 association、55/55 unique slice；
- explicit gaps：5行，全部带技术原因和后续归属；
- next：`MP-V03`几何/Bezier/position/hit-test Wasmtime/ADB差分。
