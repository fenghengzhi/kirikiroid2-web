# motionplayer state clone / serialize / restore 临时 owner 与部分提交审计（四参考二进制，2026-08-27）

## 1. 范围与结论

本报告逐项闭合原始任务：

- `MP-L14`：`clone/serialize/unserialize/assignState` 的临时 owner 和部分提交边界；
- `MP-R21`：state serialize/restore、clone 和 incomplete-state 边界。

本轮对四份参考 IDB fresh 读取了下列共同闭包：

- `EmoteEngine::serializeState` 与 `EmoteEngine::unserializeState`；
- 八个 state group 的 serializer 与八个 restore helper；
- `EmoteObject::clone`；
- `D3DEmotePlayer::clone`；
- `D3DEmotePlayer::assignState` 的故意 TODO 叶子。

总计 84 个函数实例。每个实例都 fresh decompile，并以完整 disassembly 和 xref
复核；四端反汇编合计 15234 条指令，所有游标均完整、没有截断，所有反编译均成功。
四端编译器产生的 EH 形状和 STL 细节不同，但共享源码层的 owner、提交顺序、状态 schema
和尖锐边界一致。

核心结论是：这套状态机制不是事务式快照或事务式恢复。

1. `serializeState` 在创建返回 Dictionary 前，已经强制运行 timeline / controller 的
   零步推进并写回 live Engine / Player 状态；后续序列化失败不回滚这些写入。
2. serializer 的 Dictionary、Array、item Dictionary 和临时 Variant 都是短期 refcount
   owner；异常会释放临时对象和已经发布到局部结果中的前缀，不形成 refcount 泄漏。
3. `unserializeState` 按 `timeline → eye → eyebrow → mouth → transition → selector → base
   → outerforce` 原地恢复；后组失败、脚本 getter 抛出或 native UB 都不会回滚前组。
4. `EmoteObject::clone` 在对象构造完成后仍把 copy 留作无 guard 的 raw local；serialize 或
   unserialize 抛出时会泄漏完整 copy。只有 state Variant 会在它已经构造后正确 unwind。
5. `D3DEmotePlayer::clone` 先构造并注册 listener shell，再 clone primary；inner clone
   失败会泄漏 shell 和 listener 注册。secondary 从不复制。
6. `D3DEmotePlayer::assignState` 严格要求 Object、尝试一次非抛型 native-instance probe，
   随后无条件抛出精确 TODO；它没有任何 persistent state commit。

当前本地 C++ 与四端联合证据相符，本轮没有语义 C++ 修改。

## 2. 顶层入口证据

表中数字是完整函数指令数。

| 函数 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `EmoteEngine::serializeState` | `0x673220`，398 | `0x55BB70`，188 | `0x1001AF774`，147 | `0x1AEF30`，255 |
| `EmoteEngine::unserializeState` | `0x675424`，260 | `0x55CF3C`，159 | `0x1001B1130`，122 | `0x1B0B80`，228 |
| `EmoteObject::clone` | `0x67CD58`，44 | `0x5611FC`，37 | `0x1001B50A4`，26 | `0x1B4CFC`，63 |
| `D3DEmotePlayer::clone` | `0x53039C`，41 | `0x4949D4`，16 | `0x100232DC8`，19 | `0x2319DC`，54 |
| `D3DEmotePlayer::assignState` | `0x530530`，28 | `0x494AC4`，33 | `0x100232F08`，24 | `0x231B4E`，28 |

`D3DEmotePlayer::clone` 到 `EmoteObject::clone`，再到 serialize / unserialize 的直接
call edge 在四端都闭合。`assignState` 的 UTF-16LE TODO 文本与函数本体已经分别验证，
没有用单次字符串搜索替代函数语义证据。

## 3. 八组 serializer / restore helper

### 3.1 serializer

| group | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| timeline | `0x673BC4`，201 | `0x55C0E4`，119 | `0x1001AFE68`，98 | `0x1AF5BC`，173 |
| eye | `0x673EEC`，269 | `0x55C290`，169 | `0x1001B008C`，150 | `0x1AF838`，266 |
| eyebrow | `0x674328`，269 | `0x55C500`，169 | `0x1001B03A0`，150 | `0x1AFBB4`，266 |
| mouth | `0x674764`，203 | `0x55C770`，155 | `0x1001B06B4`，146 | `0x1AFF30`，258 |
| transition | `0x674A9C`，140 | `0x55C9A4`，81 | `0x1001B09A0`，89 | `0x1B0294`，140 |
| selector | `0x674CD0`，173 | `0x55CAD0`，115 | `0x1001B0B6C`，117 | `0x1B04A0`，192 |
| base | `0x674F88`，160 | `0x55CC70`，108 | `0x1001B0DC4`，82 | `0x1B073C`，160 |
| outerforce | `0x675208`，135 | `0x55CDF0`，94 | `0x1001B0F98`，71 | `0x1B0980`，140 |

### 3.2 restore

| group | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| timeline | `0x675834`，231 | `0x55D184`，159 | `0x1001B1410`，138 | `0x1B0EB0`，230 |
| eye | `0x675BE4`，502 | `0x55D398`，348 | `0x1001B16E0`，145 | `0x1B11CC`，215 |
| eyebrow | `0x6763D0`，510 | `0x55D7D8`，338 | `0x1001B19A4`，143 | `0x1B1484`，210 |
| mouth | `0x676BE4`，511 | `0x55DBF4`，413 | `0x1001B1C68`，145 | `0x1B1734`，209 |
| transition | `0x677400`，522 | `0x55E13C`，348 | `0x1001B1F2C`，151 | `0x1B19F0`，218 |
| selector | `0x677C48`，513 | `0x55E578`，413 | `0x1001B2218`，145 | `0x1B1CD4`，207 |
| base | `0x67846C`，176 | `0x55EAC0`，106 | `0x1001B24DC`，83 | `0x1B1F8C`，158 |
| outerforce | `0x67872C`，224 | `0x55EC4C`，90 | `0x1001B26CC`，70 | `0x1B21DC`，135 |

八对 helper 在四端全部只有一个顶层 state caller；没有发现第二条隐藏事务路径、备用
rollback helper 或平台专用恢复顺序。

## 4. 精确 state schema 与故意不完整的快照

顶层 Dictionary 始终尝试按下列顺序写八个键：

```text
timeline, eye, eyebrow, mouth, transition, selector, base, outerforce
```

子 schema 为：

| group | 保存字段 |
|---|---|
| timeline item | `label`, `flags | 1`, `curTime`, `blendRatioCtrl`, `stopWhenBlendDone` |
| eye / eyebrow item | `label`, `phase`, `frame`, `v`, `target`, `length`, `lengthDone`, `exponent`, `speed`, `rq` |
| mouth item | `label`, `phase`, `mouth`, `frame`, `prev`, `target`, `tick`, `exponent`, `speed` |
| transition item | var-controller schema，再加 `label` |
| selector item | `label`, `value`, `phase`, `speed`, `tick` |
| base | `coord`, `scale`, `color`, `rotate` |
| outerforce | `bust`, `hair`, `parts` |
| var-controller | `phase`, `tick`, `speed`, `exponent`, `frame[]`, `prev[]`, `target[]` |
| angle-controller | `phase`, `tick`, `speed`, `exponent`, `frame`, `prev`, `target` |
| request-queue item | `p0`, `p1` |

“serialize” 并不表示完整对象复制。固定八键 schema 之外的 live 状态不会进入快照，包括
renderer/resource cache、metadata topology 本身、普通 Player 绘制缓存、wind emitter、
var-controller 的 pending command queue，以及 selector 的 options、command deque、entry gate
和 dormant targets。timeline 只保存可以在新对象已有 metadata 上重建运行位置的字段，
不直接复制 timelineData owner。`D3DEmotePlayer::clone` 还明确不复制 secondary shell state。

因此 `EmoteObject::clone` 的语义是“以相同 module path 新建独立对象，再回放这份有限
Engine state”，不是内存级 deep copy；D3D clone 又比它少一层 secondary 状态。

## 5. 临时 owner 图

```text
D3DEmotePlayer::clone
  borrowed target D3DLayer
    listener list borrows ───────→ completed D3DEmotePlayer shell
                                   raw owns primary ─→ EmoteObject
                                   raw owns secondary → null（clone 不复制）
  new-expression pending storage ─→ ctor 返回后结束 owner 责任
  completed shell raw local ──────→ 无异常 guard
                                   └─ inner clone 抛出：shell + listener 泄漏

EmoteObject::clone
  new-expression pending storage ─→ 只覆盖 EmoteObject ctor
  completed EmoteObject raw local ─→ 无异常 guard
    raw owns ResourceManager
    raw owns EmoteEngine
      raw owns Player / controllers
  state tTJSVariant ──────────────→ refcount owns serialized Dictionary closure
                                   └─ unserialize 退出/抛出时释放
  serialize 抛出 ────────────────→ completed copy 泄漏；state 尚未成为 live owner
  unserialize 抛出 ──────────────→ state 正确释放；completed copy 仍泄漏

serializeState
  result ncbPropAccessor ─────────→ 接管新 Dictionary 的 refcount
  child tTJSVariant ──────────────→ 每个 SetValue 后立即释放
  group Array Variant ────────────→ pin 住 Array native payload
  item ncbPropAccessor ───────────→ 接管每个新 item Dictionary
  Array item Variant ─────────────→ emplace 后 refcount 拥有 item closure
  return tTJSVariant ─────────────→ AddRef 成独立返回 owner；accessor 再 Release

unserializeState
  by-value data Variant ──────────→ caller 输入的独立 refcount owner
  retained dispatch ──────────────→ data.Clear 后仍存活
  child Variant ─────────────────→ 每组 PropGet 一个，restore 返回后立即释放
  item Variant / accessor ────────→ item 扫描期间保持 Object closure
  retained dispatch ──────────────→ 正常与 catch/rethrow 各 Release 恰好一次
```

这里的 `raw local` 与 `refcount owner` 不能互换。给两个 clone 添加 `unique_ptr` guard 会修复
原版泄漏，却会改变四参考共同的异常边界。

## 6. serialize 的写回和失败前沿

共同顺序是：

```text
preProgress(force=true, dt=0)
step eye(0), eyebrow(0), mouth(0), selector(0), transition(0)
write their current values into Engine variable-value map
step root position/color/scale/angle at 0 and write Player-facing root state
create result Dictionary
serialize and SetValue eight groups in fixed order
return independent Object Variant
```

因此即使返回 Dictionary 从未创建，active timeline、controller 当前值、Engine variable map
和 Player root state 也可能已经改变。任一 controller step、unordered-map 写入、Dictionary /
Array 分配、property dispatch 或 item vector 增长抛出，已经发生的 live-state 前缀都保留。

返回对象自身采用正常 RAII：

- group Array 构造失败时，其 Variant 释放 Array 和已插入 item 前缀；
- item 构造或 property 写入抛出时，item accessor 与 group Array 逆序释放；
- 顶层后组失败时，result accessor 释放顶层 Dictionary，Dictionary 再释放已经 SetValue 的
  前组 children；
- `SetValue` 的返回 status 被忽略，因此脚本对象只返回 failure status 而不抛出时，调用仍会
  继续，可能形成缺键或缺字段的成功返回对象；
- active timeline label 通过 map subscript 访问，stale label 会先物化默认 state，再在缺失
  controller 等后续访问中进入原版尖锐边界，不会被静默跳过。

## 7. unserialize 的部分提交与 incomplete-state 矩阵

顶层先强制 Object 转换，保留 dispatch 后清掉 by-value data owner。非 Object 输入在任何
Engine state 改动前失败。Object 输入随后按八键顺序逐组 `PropGet`；status 不检查，缺键
以 Void 进入对应 restore helper。每个 child Variant 在进入下一组前释放。

| 输入边界 | 四端共同结果 |
|---|---|
| 缺少 / 非 Array `timeline` | 先执行 `stopTimeline("")` 清空全部 active timeline，再返回；这是故意的 destructive partial commit |
| timeline item 非 Object | 跳过该 item |
| timeline item 缺 label / label 未在 metadata state map 中 | 跳过该 item |
| timeline 缺 flags / curTime | 分别使用 `0` / `0.0`，随后 play + seek；缺 autoStop / blend controller 时保留对应当前值 |
| 缺少 / 非 Array eye、eyebrow、mouth、transition、selector | 该 group 不改动 |
| group item 非 Object或缺 label | 跳过该 item |
| eye 的未知 label | 安全跳过；eye 是唯一检查 search result 与 end 的 controller group |
| eyebrow / mouth / transition / selector 的未知 label | 无条件解引用 end iterator，进入 native UB；不能健壮化成 skip |
| base / outerforce 非 Object | 整组不改动 |
| base / outerforce 缺 child key | child Void 使对应 controller restore no-op；其他已处理 controller 不回滚 |
| controller scalar 字段缺失 | 保留该字段旧值；前面已恢复字段不回滚 |
| var-controller channel property 存在但内容短、类型错误或 getter 抛出 | 按 channel index 顺序原地写；异常或畸形边界可留下数组前缀更新 |
| eye / eyebrow `rq` 缺失或不是 Array | 保留旧 request queue |
| `rq` 是 Array | 先 clear 旧 queue，再逐项读 `p0/p1` 并 emplace；中途失败只保留新前缀，旧 queue 不恢复 |
| angle `target` | 四端都错误地再次写 `startRad`，覆盖 `prev`；`targetRad` 不恢复 |

组内字段也不是事务：例如 eye / eyebrow 先写 scalar track 字段，最后才处理 request queue；
queue rebuild 抛出时，scalar 前缀已经提交。mouth 与 selector 同样按各自字段顺序直接写入。
顶层 catch 只负责 Release retained dispatch 并 rethrow，不承担状态 rollback。

## 8. 两层 clone 的提交边界

### 8.1 `EmoteObject::clone`

四端共同伪代码：

```text
copy = new EmoteObject(source.modulePaths)
state = source.engine.serializeState()
copy.engine.unserializeState(state)
return copy
```

publication / cleanup 边界：

1. `operator new` 后到 constructor 成功前，new-expression cleanup 拥有 pending storage；
2. constructor 成功后，copy 是没有 guard 的 raw local；
3. serialize 抛出时 copy 泄漏，且 source 可能已经保留 serialize 的 live-state 写回前缀；
4. serialize 返回后 state Variant 成为 live refcount owner；
5. unserialize 抛出时 state Variant 释放，但 copy 及其独立 RM / Engine / Player / controller
   owner tree 泄漏；copy 内已经恢复的 state 前缀也随泄漏对象留存；
6. 成功时才把 raw copy 发布给 caller。

iOS armv7 的 SjLj `call_site` 最直接地区分 constructor pending cleanup、无 state owner 的
serialize 区间，以及只清理 state Variant 的 unserialize 区间；其他三端的 landing-pad /
EH 表与之同构。

### 8.2 `D3DEmotePlayer::clone`

共同顺序：

```text
shell = new D3DEmotePlayer(borrowedD3DLayer)  // ctor 立即 AddListener(shell)
shell.primary = source.primary.clone()
return shell
```

shell constructor 成功后 new-expression cleanup 结束；listener registration 已经对外可见，
但 shell 尚未交给任何持续 owner。inner clone 抛出会同时留下：

- 一个 completed shell；
- D3DLayer listener list 中指向它的 borrowed entry；
- shell 中仍为 null 的 primary slot；
- inner `EmoteObject::clone` 自己可能已经泄漏的完整对象。

成功时只发布 primary；secondary 保持 constructor 默认 null。source primary 是未经 null
guard 的直接输入，clone 不是空 shell 的安全操作。

## 9. `assignState` 的故意 TODO

四端共同语义是：

```text
require input Variant to be Object
if dispatch != null:
    probe D3DEmotePlayer native instance with throwOnMismatch=false
throw eTJSError("TODO: implement D3DEmotePlayer::assignState()")
```

精确文本为：

```text
TODO: implement D3DEmotePlayer::assignState()
```

probe 结果不保存、不读取，不会把 primary、secondary 或任何 Engine state 提交到 receiver。
非 Object 输入可在严格转换处先失败；Object/native mismatch 不会替代最终 TODO。参数的
refcount owner 由 typed adapter / 按值参数 unwind 释放。

## 10. 本地逐项对照

| 参考要求 | 本地位置 | 结论 |
|---|---|---|
| 顶层八键顺序、serialize 前置写回 | `cpp/plugins/motionplayer/EmoteEngine.cpp:3448` | 匹配 |
| 八个 group serializer 与有限 schema | `cpp/plugins/motionplayer/EmoteEngine.cpp:3295` | 匹配 |
| 八个 restore 顺序、dispatch owner 与 catch Release | `cpp/plugins/motionplayer/EmoteEngine.cpp:3782` | 匹配 |
| timeline destructive missing-state 行为 | `cpp/plugins/motionplayer/EmoteEngine.cpp:3510` | 匹配 |
| eye safe miss 与其余四组 unchecked miss | `cpp/plugins/motionplayer/EmoteEngine.cpp:3566` | 匹配 |
| base / outerforce 顺序恢复 | `cpp/plugins/motionplayer/EmoteEngine.cpp:3726` | 匹配 |
| controller field / channel / request-queue partial commit | `cpp/plugins/motionplayer/EmoteEngine.cpp:354` | 匹配 |
| angle target 写入 startRad 的 shipped quirk | `cpp/plugins/motionplayer/EmoteEngine.cpp:499` | 匹配 |
| `EmoteObject` raw copy leak 边界 | `cpp/plugins/motionplayer/EmotePlayer.cpp:120` | 匹配 |
| D3D primary-only clone 与 listener shell leak | `cpp/plugins/motionplayer/EmotePlayer.cpp:239` | 匹配 |
| Object/native probe 后精确 TODO | `cpp/plugins/motionplayer/EmotePlayer.cpp:257` | 匹配 |

现有测试直接覆盖：

- `tests/unit-tests/plugins/motionplayer-dll.cpp:27363`：D3D typed factory / clone owner 边界；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:29803`：八键 schema、controller round-trip 和 angle quirk；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:29872`：timeline active flag 强制位；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:29893`：eye unknown-label 安全 skip；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:29911`：缺 timeline 仍清空 active list；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:32537`：D3D `assignState` 精确 TODO。

本轮只做四端静态闭环与 IDB 改善，没有修改 C++ 或测试。正式 native / Web 构建和运行时
测试仍由 `MP-V06..V08` 统一跟踪，不能把本任务的静态闭环误报为全项目已验证。

## 11. IDB 改善与 disposition

- 64 个 group helper 已在四份 IDB 中统一写入确定性语义名；
- 84 个函数实例均加入本任务函数注释；
- 20 个顶层入口写入 task bookmark；
- 四份 IDB 均已原位保存。

disposition：

- 原始任务：`MP-L14`；静态状态：`CLOSED_STATIC`；
- 原始任务：`MP-R21`；静态状态：`CLOSED_STATIC`；
- 覆盖切面：`MP-L14-STATE-CLONE-SERIALIZE-RESTORE-PARTIAL-COMMIT`；
- 本任务局部剩余静态差异：无；
- 全局剩余：正式构建 / 运行测试、跨对象总 owner 审计和最终 163 项验收仍独立开放。
