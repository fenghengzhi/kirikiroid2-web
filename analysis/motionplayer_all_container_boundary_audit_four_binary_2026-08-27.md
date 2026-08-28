# motionplayer 全容器 empty / duplicate / erase / invalidation / allocation / partial-commit 总审计（四参考二进制，2026-08-27）

## 1. 任务结论

本 slice 是 `MP-C15` 的跨容器验收，不重复把 `MP-C01..C14` 当作“已经分别通过所以自动
通过”。本轮重新建立 container-role denominator，并对每种会改变可观察边界的 archetype 逐项
核对：

- empty 与越界/缺失路径；
- duplicate 的保留、折叠、覆盖或删除数量；
- erase/clear 后哪些 element、owner、capacity/bucket 仍存活；
- iterator/reference/pointer invalidation，包括原版故意保留的 re-entrant UB 边界；
- map/block/node/value 分配失败时的 cleanup；
- script callback、getter、转换或后续 allocation 抛出时已经提交的 prefix。

结论：本地 portable containers、owner 字段、erase loop 和发布顺序与四端共同源码结构一致；
没有发现新的生产语义差异，不需要修改生产 C++。原版的 unsafe live iteration、callback
reentry、partial publication、raw-owner leak window 和 malformed-input unchecked access 均作为
兼容边界保留，没有加入事务回滚、iterator snapshot、dedup 或 bounds guard。

Android old-libstdc++ 与 iOS libc++ 的具体 map/deque/list/tree 展开由 `MP-C16` 统一分类；
`MP-C15` 在这里闭合的是这些展开背后的共同操作及其可观察成功/失败状态。

## 2. fresh 四端 checkpoint

在 `MP-C01..C14` 的 direct reports 之外，本轮又 fresh decompile、完整 disassemble 并
`xrefs_to` 12 种代表 archetype 的四端实例，共 48 个 distinct function ranges、9,183 条完整
未截断指令和 284 个 xrefs。全部 decompile 成功，全部 disassembly cursor 为 `done=true`。

| archetype | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 | 指令总数 |
|---|---:|---:|---:|---:|---:|
| node deque partial append | `0x6B1E4C` / 397 | `0x5818B0` / 230 | `0x100109328` / 182 | `0x106BDC` / 268 | 1,077 |
| parameter vector/multimap erase | `0x6C1A48` / 490 | `0x58C4D8` / 231 | `0x100116410` / 188 | `0x113D54` / 291 | 1,200 |
| pending-event vector live dispatch | `0x6C1870` / 118 | `0x58C3A8` / 90 | `0x10011622C` / 97 | `0x113B64` / 145 | 450 |
| selector deque/options publication | `0x66ACDC` / 593 | `0x557E04` / 331 | `0x1001AA030` / 412 | `0x1A96D8` / 626 | 1,962 |
| active-timeline vector erase | `0x679680` / 38 | `0x55F6E4` / 28 | `0x1001B341C` / 28 | `0x1B2F70` / 26 | 120 |
| layer-id set range erase | `0x6A8B30` / 51 | `0x57C2C8` / 49 | `0x100102DB8` / 52 | `0x10028A` / 47 | 199 |
| SourceCache list trim | `0x6A3EE0` / 55 | `0x57A106` / 37 | `0x1000FFA1C` / 39 | `0xFCCD2` / 37 | 168 |
| SeparateLayer trees invalidate/clear | `0x6C46C4` / 87 | `0x58E174` / 109 | `0x10011844C` / 130 | `0x116280` / 190 | 516 |
| listener list remove/reentry | `0x5311C8` / 25 | `0x4952AC` / 24 | `0x100233720` / 11 | `0x23259A` / 9 | 69 |
| TJS Array Items deque grow | `0x6DFC90` / 47 | `0x5A099C` / 40 | `0x1000FAED8` / 47 | `0xF7F90` / 54 | 188 |
| D3D dual batch vectors | `0x6AB39C` / 606 | `0x57D3DC` / 655 | `0x100104450` / 545 | `0x101850` / 888 | 2,694 |
| global cubic-basis map cache | `0x69DE30` / 167 | `0x576C7C` / 142 | `0x1000FB4A8` / 107 | `0xF854C` / 124 | 540 |

这些 checkpoint 不是用 12 个函数替代完整 denominator；它们专门重验跨报告最容易出现缝隙
的 mutation/failure frontiers。每个具体容器的 producer、consumer、copy/move/dtor 与测试仍由
下列 C01..C14 direct slices 提供。

## 3. container-role denominator

### 3.1 `Player` 与 `MotionNode`

| role | source container | owner/borrow | direct evidence |
|---|---|---|---|
| flat node store | `deque<MotionNode>` | element owns Variants/vectors/child Player；cross-node pointers borrow | C01、MotionNode lifetime、build tree |
| raw label resolver | `map<ttstr,int>` | key owns backing；mapped POD | C01、C14 |
| parameter table | `vector<MotionParameterEntry>` | entries own id/value fields | C02 |
| parameter ramps | `multimap<ttstr,Entry*>` | key owns；mapped pointer borrows vector entry | C02、C14 |
| variable tracks | `deque<VariableLabelScope>` | element owns key/slot values | C02 |
| HM1/HM2/HM3/HM4 | four `unordered_map<ttstr,...>` | key owns；mapped owner/POD按 type | C02、C14 |
| per-node mesh/chain/cache | multiple vectors inside `MotionNode`, ClipSlot and HM1 value | element owners；node/mask/result pointers borrow | C01/C02/C05/C09 |
| pending events | `vector<MotionEvent>` | two Variant owners per element | C04 |
| prepared items/commands | deque/vector/list-like source roles recovered per phase | prepared item owns child pointer vectors and render payload owners | C09 |
| SeparateLayer pass state | two ordered maps plus payload vectors | map node owns Layer/string/vectors | C09 adjunct slices |

### 3.2 `EmoteEngine` 与 controllers

| role | source container | boundary owner |
|---|---|---|
| ten controller categories | ten distinct typed deques | raw owner is transferred into element only at category-specific publication point |
| angle/var/loop/selector queues | deque/vector key tracks | controllers own elements; step/pop destroys only consumed element |
| eye/eyebrow/mouth resolver graphs | edge vector、deque-of-vectors、output vector、scratch deques/vectors | controller/resolver owns persistent graph/output; scratch is call-local |
| selector options/targets | options vector owns records; targets vector borrows transition elements | transition deque outlives borrow on normal destruction; reset creates a brief non-dereferenced dangling interval |
| hair/parts/bust chains | typed deques and fixed records | records borrow wind/other nodes where reported |
| wind | raw controller owner plus fixed 128-slot pool | no dynamic container allocation in pool; stop predicate has separate width disposition |
| timeline | state unordered map、three declared/active label vectors、Track deque、Frame/cursor vectors | state owns data/controller/raw Variant; Track owns controller/frame/key |
| mirror/instant/range/value | unordered sets/maps | `ttstr` key owner and typed mapped owner/POD |

这些角色由 C03、C06、C07、C08 和 C14 共同闭合；没有把十个异构 deque 合并成一个 uniform
record，也没有把 fixed pool 误记为 dynamic vector。

### 3.3 resource、D3D、TJS、global 与 transient

| subsystem | containers | direct evidence |
|---|---|---|
| ResourceManager | loaded-module unordered map、record 内 Win/KRKR maps、layer-id set | C10、C14 |
| SourceCache | `list<Entry>` LRU-like physical list | C11 |
| D3DAdaptor/Layer/DrawDevice | software texture map、listener list、Front/Back/managed/module trees、manager vector | C12 |
| D3D/geometry rendering | destination/source/mesh/cell/batch vectors、fixed arrays | C09/G reports |
| TJS Array/Dictionary | Array native Items deque、Dictionary hash/container | C13 |
| SeparateLayer | active/retired ordered maps with payload vectors | C09 adjunct reports |
| process globals | cubic basis map、unit Bezier vector、software texture map、registered-module set | L15/C11/C12/G reports |
| EmoteObject facade | module-path vector | object lifetime reports |

Port-only `MotionTraceWeb` diagnostic registries are not reference motionplayer state and therefore不伪装成
参考二进制容器；它们仍受本地普通 C++ tests/teardown约束，但不进入四参考一比一 denominator。

## 4. empty 行为矩阵

| family | empty/missing behavior |
|---|---|
| node deque | constructor-created synthetic root 是稳定最小状态；ordinary build loops跳过空 raw layer list；直接 malformed consumer仍可能 unchecked deref |
| parameter/vector/multimap | empty parameter table使 idle path早退；empty `equal_range` no-op；无合成 default entry |
| unordered unique maps/sets | find/erase miss no-op；`operator[]` 仅在真实 subscript consumer物化 default mapped value |
| timeline | null-backed stop label clear全部 active；named miss no-op；stale `at` 抛，stale subscript物化 default state |
| controller queues | empty step通常 no-op/保持当前 scalar；各 controller 的 clear/reset 顺序不同，不统一成一个 policy |
| selector targets | 四端无 writer，正常保持 empty；consumer保留 empty branch，不合成 target |
| render vectors | empty selected cells在 Release source后返回 false；empty prepared lists跳过；fixed arrays仍保持构造值/未初始化边界 |
| layer-id set | release miss等价 erase `[end,end)`，返回0；release(0)若 sentinel存在则删整 suffix |
| SourceCache/listener list | empty trim/clear/remove no-op；scratch bufLayer不是 list element |
| TJS Array | count 0 返回 owning empty Array；NativeInstanceSupport 非零状态发布 null Items borrow，consumer依原路径失败 |
| ordered SeparateLayer maps | miss resolver按具体入口 ensure 或 return null；clear empty tree只重置 header/state |
| global cache | first miss才构造；hit不重复分配；负/极值 division保持既有 resize/UB边界 |

## 5. duplicate 行为矩阵

duplicate 不是全局 dedup：

| container role | duplicate disposition |
|---|---|
| `NodeLabelMap` | unique key；caller后写 mapped index；首 key backing保留 |
| parameter multimap | 每个 duplicate id 独立 node，`equal_range` 全部消费 |
| HM/unordered unique maps | equality-equal key复用 node；mapped value按 caller overwrite/reuse；首 key owner保留 |
| timeline declared/active vectors | declared与外部 malformed active duplicates均保留；named stop只删第一个；live scans逐项处理 |
| controller resolver map | later duplicate locator覆盖；所有旧 deque elements及 owners继续存活 |
| event/prepared/mask/listener/manager sequences | duplicates按物理序列保留；可能重复 callback、render、mask或 manager owner |
| D3D listener remove | 删除所有相等 payload nodes，不是只删第一个 |
| SourceCache identity hit | 相同 triple且颜色同直接返回；颜色变时 push-front copy再 erase old，不留两个 persistent nodes |
| Resource module/source maps | unique；duplicate candidate按目标 STL cleanup，persistent首 key/value语义由对应 insert path决定 |
| layer-id set | duplicate insertion no-op；counter scan继续直到空 key |
| SeparateLayer maps | ordinal key unique；active/retired间 move/reuse，不把同一 ordinal长期保留为两个 owners |
| cubic basis map | division key unique；hit返回现有 table；miss先发布 empty mapped vector再 resize/fill |

## 6. erase、clear 与 invalidation

### 6.1 vector

- growth/reallocation 使所有 raw element pointers/iterators失效；erase 使 erase point及之后失效；
  clear销毁 elements但通常保留 capacity。
- pending-event dispatcher 故意持有 raw cursor，同时每轮重读 live `data()+size()`。callback append
  未 realloc 时新 tail可在同轮处理；一旦 realloc，旧 cursor失效，参考实现没有 recovery。
- active-timeline named stop用 `find` 后 erase一个 element；重复项只少一个。其他 cleanup path
  明确使用 remove/erase range删除所有 matches。
- dual render batch按顺序 reserve/build source/destination vectors；两者不是事务性 pair。
- HM1 `heapResult.clear()`保留 capacity；随后 rebuild allocation失败留下 empty/partial rebuild和已清
  weight状态。

### 6.2 deque

- node/controller/TJS Items deque 的 block-map layout按目标 STL不同；source只依赖相应
  push/emplace/pop/clear语义。
- `MotionNode` append成功后 reference稳定到后续字段读取；后续 property/map/layer-id/child失败
  留下 partial element。
- controller pop只销毁 front consumed owner；clear按 element物理顺序销毁全部 live elements，
  block/map storage的保留/释放由具体 clear/dtor区分。
- TJS Items grow可能分成 replacement map allocation和new element block allocation；后一阶段
  抛出时可留下 capacity/map-only变化而没有新 Variant element。

### 6.3 list

- SourceCache erase使用返回的 next iterator；被删 node之外的 iterator稳定。Entry按
  `src -> layer -> key` 逆成员顺序释放。
- color-changing hit不是 splice，而是 `push_front(copy)` 后 `erase(old)`；新 key/layer/src先
  AddRef，旧 node随后 Release。
- listener `remove`删除全部 duplicate。notification直接走 live list；current callback自删会
  失效当前迭代器，删 future/append tail同轮可见，异常停止剩余 callback。原版不 snapshot。

### 6.4 ordered tree / set

- exact erase只失效目标 node；`it = erase(it)` loops在 parameter multimap和 SourceCache-like
  tree paths中保留 next。
- parameter vector销毁前先按 mapped pointer从 ancestor multimap逐个 purge，避免 dangling
  borrowed `Entry*`。
- `releaseLayerId(id)` 从 exact hit删到 end；miss不删，counter不回退。
- SeparateLayer invalidate/clear中 callback可先提交；抛出时已 invalidated nodes与未清 tail构成
  partial tree state。

### 6.5 unordered map/set

- erase只销毁目标 owning key/value node；clear销毁全部 nodes但保留或释放 buckets取决于
  clear vs dtor；rehash使 iterator失效但 element reference/pointer保持 standard guarantee。
- motionplayer insertion paths不在可能 rehash 的 insert 前后持有 container iterator；外部 raw
  pointers只在对应 owner container保证下使用。
- hash Hint可在 allocation之前发布；failure不回滚 Hint，详见 C14。

## 7. allocation/exception guarantee 总表

| frontier | 已提交状态 | cleanup / no-rollback |
|---|---|---|
| vector/deque element allocation失败 | 当前单次 insert没有新 element | 先前 clear、earlier elements和script side effects保留 |
| node deque append成功、后续 getter失败 | partial MotionNode、parent/done已发布 | element由 deque持有到 reset/dtor；不回滚 |
| map/set node allocation失败 | 未 link candidate由 STL cleanup | 已计算 Hint、earlier map nodes/counter reads不回滚 |
| mapped value constructor失败 | candidate key/value prefix逆序销毁 | persistent container原 logical membership保持；ABI capacity细节另分类 |
| rehash/bucket allocation失败 | current insertion按该 specialization cleanup | 已发布 input state和可能的 candidate temporaries按目标路径处理 |
| TJS deque map grow成功、block失败 | 可能保留 map capacity/centering变化 | 没有新 public Variant item；owner closure仍有效 |
| selector/controller owner先发布、label/map失败 | deque partial element/owner存活 | resolver可能缺项或保持 earlier mapping；reset/dtor最终释放 |
| SourceCache create/bake/push失败 | earlier trim、created Layer、script writes按阶段保留 | raw Layer/texture leak windows按 direct report保持 |
| SeparateLayer callback失败 | earlier invalidation/Layer writes保留 | 未处理 tree tail仍存在；无 transaction |
| D3D dual vector第二次 reserve/build失败 | 第一 vector可能已有 allocation/elements | function-local unwind各自析构；persistent script/texture side effects不补偿 |
| global basis map table resize/fill失败 | map key与 empty/partial mapped vector已发布 | guarded/manual static初始化状态按平台；无 erase rollback |
| event/listener callback抛出 | earlier callbacks和mutable container edits保留 | 只清已构造 locals/retained dispatch；剩余 callbacks不执行 |

## 8. owner 与 destruction closure

共同规则不是“container一律 own pointee”，而是逐 element type：

- `ttstr`、Variant、unique/intrusive holders、nested vectors/deques通常由 node/element拥有；
- `MotionNode*`、parameter `Entry*`、selector target、manager/listener等明确标注 borrow；
- raw owner只有在对应 publication store后转移；此前 allocation/constructor异常可能泄漏或由
  pending-new-expression cleanup处理，不能统一套 `unique_ptr`；
- clear销毁 elements但不一定释放 container backing；ordinary dtor在 element closure后释放
  blocks/buckets/map；
- callback-capable owner Release/Invalidate 可能 reenter；只有参考先 snapshot 的路径才 snapshot，
  其余保留 live traversal。

Player reset先清借用 parameter vector entry 的 ramps，再销毁 vector；Engine reset/dtor按十个
controller deque和 selector borrow的特定顺序；timeline state dtor按 cursor/raw/controller/data/key
逆序；SourceCache/D3D/SeparateLayer分别遵守自己的回调与 mapped holder closure。没有发现
跨容器 double-owner、漏掉的 persistent owner或错误的 destructor顺序。

## 9. 本地实现与测试映射

代表实现位置：

- `cpp/plugins/motionplayer/NodeTree.cpp:245`：deque append-before-read partial node；
- `cpp/plugins/motionplayer/PlayerVariable.cpp:176`：duplicate multimap build与 exact pointer purge；
- `cpp/plugins/motionplayer/PlayerVariable.cpp:208`：HM1 vector clear/rebuild；
- `cpp/plugins/motionplayer/PlayerFrameProgress.cpp:620`：HM3 exact erase及终端 clear；
- `cpp/plugins/motionplayer/PlayerFrameProgress.cpp:1240`：pending-event live raw cursor；
- `cpp/plugins/motionplayer/EmoteEngine.cpp:2955`：timeline clear-vs-first-erase；
- `cpp/plugins/motionplayer/ResourceManager.cpp:513`：set duplicate scan与 suffix erase；
- `cpp/plugins/motionplayer/SourceCache.cpp:543`：list hit copy-before-erase；
- `cpp/plugins/motionplayer/SourceCache.cpp:716`：callback walk后 clear；
- `cpp/plugins/motionplayer/SourceCache.cpp:815`：erase-return-next trim；
- `cpp/plugins/motionplayer/SeparateLayerAdaptor.cpp`：active/retired map move、erase、clear；
- `cpp/plugins/motionplayer/RuntimeSupport.cpp:449`：owning Array closure + borrowed Items；
- `cpp/plugins/motionplayer/MotionRenderBackend.cpp:194`：map-before-resize basis partial publication；
- `cpp/plugins/motionplayer/MotionRenderBackend.cpp:684`：dual vector build与 empty release；
- `cpp/plugins/motionplayer/internal/ttstr_hash.h:27`：unordered key/Hint boundary。

对应 unit 源分散覆盖 node partial build、parameter duplicates/purge、pending event append/reentry、
controller sparse/duplicate publication、timeline duplicates/stale keys、render vectors、Resource set/map、
SourceCache list、D3D listener/manager duplicates、TJS Array grow owner、SeparateLayer map和 hash key
边界。direct locations已逐项记录在 C01..C14 coverage rows；本总审计不复制一套会漂移的 test
清单。

本轮没有生产或测试语义修改。执行了 coverage 12-column/duplicate-ID、tracker regeneration和
`git diff --check`；正式 unit/Web/runtime执行仍由 `MP-V` 独立任务负责。

## 10. supporting direct reports

核心 denominator：

- C01：`motionplayer_motionnode_source_order...`、`motionplayer_player_build_node_tree...`、
  `motionplayer_motionnode_prepared_item_deque_lifetime...`；
- C02：`motionplayer_player_parameter_table_pipeline...`、`motionplayer_player_init_variables...`、
  `motionplayer_player_variable_binder_dataflow...`；
- C03：`motionplayer_timeline_track_cursor_playlog_state_map_containers...`；
- C04：`motionplayer_player_frame_progress_events...`；
- C05：motion-sub、particle-emitter、particle-system phase reports；
- C06/C07：seven direct controller owners 与 controller element publication reports；
- C08：selector/transition/spring/bust/hair/parts/wind container report；
- C09：prepared append、build commands、D3D deep batch、SeparateLayer ordered-map reports；
- C10：ResourceManager load/unload/layer-id reports；
- C11：SourceCache、KRKR atlas、D3D source texture insert reports；
- C12：D3D texture/listener/manager containers 与 lifecycle reports；
- C13：TJS Array/Dictionary exception-frontier report；
- C14：`ttstr` hash/equality/key-boundary report；
- adjunct：global static/cache/RNG lifetime、EmoteObject chain和 render geometry reports。

所有文件名与 slice IDs 由 `analysis/motionplayer_tasks_status.tsv` 和
`analysis/motionplayer_coverage.tsv` 作为规范机器可读映射，不依赖本节省略的长前缀显示。

## 11. IDB 固化与 disposition

四个 IDB 的 12 个 checkpoint root（共 48 个地址）均写入 `MP-C15` boundary comments，并各加
一个 task bookmark；这些函数此前已有对应专项确定性命名，本轮没有为制造 rename 计数而改名。
四库在 comments/bookmarks 固化后保存。

| 验收要求 | disposition |
|---|---|
| every container role | persistent、nested、transient、TJS、global/fixed role denominator已建立 |
| empty | 每类 no-op/default/materialize/crash/clear 差异已列明 |
| duplicate | unique/multi/sequence/all-remove/first-remove分类完成 |
| erase/clear | element owner、capacity/bucket、suffix/first/all erase完成 |
| iterator invalidation | vector/deque/list/tree/unordered 与 live callback unsafe边界完成 |
| allocation failure | node/map/block/value/dual-vector/global cache publication完成 |
| partial commit | script/property/callback/owner/container prefixes无事务回滚矩阵完成 |
| platform distinction | source-common success/failure语义闭合；ABI展开交由 C16 |
| local implementation | 无新 semantic gap；无需生产修改 |

因此 `MP-C15` 可标为 `CLOSED_STATIC`。正式构建与 runtime verification仍由 `MP-V` 跟踪；
不能把尚未运行的单元测试误记成动态通过。
