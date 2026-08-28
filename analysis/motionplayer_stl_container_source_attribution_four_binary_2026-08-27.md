# Android libstdc++ / iOS libc++ 容器展开归因与共同源码选型（四参考二进制，2026-08-27）

## 1. 任务结论

本 slice 闭合 `MP-C16`：把四参考二进制里所有与 container 有关的差异拆成三类：

1. **共同源码容器、目标 STL 私有展开不同**：header、node、block、bucket、rehash、growth、
   iterator、cached size、inline helper 和 exception cleanup；
2. **共同源码逻辑、pointer width/compiler lowering 不同**：对象 offset、整数宽度、调用约定、
   inlining、DWARF/EHABI/LSDA/SjLj；
3. **真正需要源码条件表达的差异**：KRKR atlas record 的两个 owning member 顺序随
   `_LIBCPP_VERSION` 分叉。它影响 vector growth copy 与 reverse destruction，因此不能被当作
   padding；当前源码已经显式保留。

反推的共同容器选型是：

- contiguous dynamic sequences：`std::vector`；
- block-map stable-element sequences：`std::deque`；
- per-node bidirectional sequence：`std::list`；
- ordered unique/multi keys：`std::map` / `std::multimap` / `std::set`；
- bucketed unique keys：`std::unordered_map` / `std::unordered_set`；
- inline fixed records：`std::array` 或原生固定数组；
- controller-owned fixed-size buffers：raw/new[] owner，而不是伪造 vector；
- wind 128 slots：fixed pool，不是 dynamic container。

本地声明已经使用这些 portable source containers；没有把参考 STL header、node padding、bucket
policy 或 block size手写进 Web 对象，因此无需生产语义修改。

## 2. fresh 四端 checkpoint

本轮 fresh decompile、完整 disassemble、`xrefs_to` 8 个 attribution archetype 的四端实例，
共 32 个 distinct function ranges、5,004 条完整未截断指令、240 个 xrefs。全部 decompile
成功，全部 disassembly cursor `done=true`。

| archetype | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 | 指令总数 |
|---|---:|---:|---:|---:|---:|
| Player container-header ctor | `0x6CC110` / 593 | `0x5935C4` / 281 | `0x10011EC04` / 226 | `0x11D488` / 499 | 1,599 |
| Engine container-header ctor | `0x67B76C` / 848 | `0x560948` / 304 | `0x1001B7FB0` / 187 | `0x1B7788` / 318 | 1,657 |
| Resource map/set/list ctor | `0x6A5CAC` / 177 | `0x57B1EC` / 63 | `0x100101158` / 44 | `0xFE254` / 93 | 377 |
| SeparateLayer two-tree ctor | `0x6C3DB4` / 92 | `0x58DBDC` / 67 | `0x1001298C4` / 50 | `0x128890` / 101 | 310 |
| manager vector add | `0xA72C14` / 67 | `0x7970B0` / 34 | `0x1002DC360` / 22 | `0x2DBD28` / 21 | 144 |
| TJS Array deque grow | `0x6DFC90` / 47 | `0x5A099C` / 40 | `0x1000FAED8` / 47 | `0xF7F90` / 54 | 188 |
| timeline unordered insert | `0x685060` / 75 | `0x5669AC` / 60 | `0x1001A6938` / 148 | `0x1A6074` / 237 | 520 |
| SourceCache list copy/push | `0x6E8040` / 47 | `0x5A67DE` / 33 | `0x100100E54` / 38 | `0xFDFB0` / 91 | 209 |

这些 fresh checkpoints 与 C01..C15 direct container reports交叉：constructor证明 member/header
数量与顺序，operation helper证明实际 growth/link/find/erase路径；只看 object offset或只看一个
`push_back` 都不足以反推 source type。

## 3. 反推方法与排错准则

每个容器选型至少同时使用以下信号：

1. default constructor 对 header/sentinel/start/finish/size/max-load 的 writes；
2. element append/insert 时 allocation 大小、link方式、copy/move/owner acquisition顺序；
3. find/iteration/erase 对 header/node/bucket/block字段的访问；
4. clear 与 dtor 是否只销毁 elements、是否保留 backing、最终何时 free storage；
5. LP64/ILP32 stride是否只按 pointer/element size变化；
6. Android两端是否共享 libstdc++ policy、iOS两端是否共享 libc++ policy；
7. 四端成功路径是否恢复成同一 C++ operation，即使 helper层级和EH不同。

由此避免以下常见误判：

- 看到分块存储就一律写 custom pool；
- 把 libc++ split-buffer deque误记成 vector；
- 把 old-libstdc++ unordered bucket predecessor/header写成业务链表；
- 因 iOS list保存 size、Android list不保存，就声明两个不同 source types；
- 把 candidate-first 与 find-first emplace当成显式平台业务分支；
- 为匹配 object `sizeof` 在 portable class中塞参考 STL padding；
- 从 unordered physical iteration推断一个跨平台稳定顺序。

## 4. `std::vector` 归因

四端 vector 均表现为 contiguous `[begin,end,cap)`，element stride由 element ABI决定。共同证据：

- index寻址是 `begin + index * stride`；
- grow分配一段更大连续 storage，按 element copy/move owner顺序构造新 range，逆序/顺序销毁旧
  range后 free旧 allocation；
- `data()`/begin raw pointer与 size是 adjacent pointer subtraction；
- erase对 tail执行 move/memmove并收缩 logical end；
- clear只销毁 live range，保留 capacity；dtor最后 free begin allocation。

manager vector、parameter entries、timeline labels/cursors/frames、mesh points、events、module paths、
render batches和resolver vectors都符合这一模型。Android/iOS的 growth factor、inline/cold cleanup、
`std::string`/Variant element move细节可以不同；source仍是 `std::vector<T>`。

Manager Add的四端共同顺序是 `push_back(pointer)` 后 `AddRef`。Android grow body较长，iOS紧凑
内联；这不是 iOS用了fixed array。duplicate允许、Remove首个、Release-before-erase与 reentry
invalidity也来自共同调用顺序，而非 vector ABI差异。

## 5. `std::deque` 归因

### 5.1 block policy

两套 STL 都是 pointer map + separately allocated element blocks，但 policy明显不同：

| element role | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| TJS Variant size | 20 | 12 | 20 | 12 |
| TJS Items per block | 25 | 42 | 204 | 341 |
| TJS block有效字节 | 500 | 504 | 4080 | 4092 |
| Timeline Track size | 56 | 28 | 56 | 28 |
| Track per block | 9 | 18 | 73 | 146 |
| Track block有效字节 | 504 | 504 | 4088 | 4088 |

Android old-libstdc++使用约512-byte block、map plus start/finish iterator（node/cur/first/last）；
iOS libc++使用约4096-byte block、pointer-map split buffer与 absolute start/size。`TimelineData`
deque header allocation也因此为 Android `0x50/0x28`、iOS `0x30/0x18`。

这些不同 block counts恰好随 element stride变化并落在各自 STL policy附近，是同一
`std::deque<T>` 的强证据；如果是业务 fixed chunk，Android/iOS不会在所有 element types上同时
切换约512/4096策略。

### 5.2 growth 与 failure

Android map reserve/recenter可先提交，随后 new block allocation失败而只留下 map-capacity变化；
iOS构造 temporary split-buffer map/new block，成功后一次 swap到 live header，失败 cleanup临时
storage。成功 append后的 element顺序一致。

portable source必须继续调用 `std::deque::push/emplace`，不能自己模拟任一私有 growth算法。
Web构建观察其所用 libc++语义；当参考两套 STL在“只有capacity改变”的 allocation-failure内部
状态上本就不同，不存在一个业务条件可以同时复制两者。

## 6. `std::list` 归因

SourceCache entries与D3D listener序列都是 per-element node + bidirectional links + sentinel：

- insert只分配一个node并在相邻node间link；
- erase unlink/delete一个node，其他node地址稳定；
- splice-like命中路径实际是 copy-node `push_front` 后 erase old；
- list traversal不做contiguous stride或block-map运算。

Android old-libstdc++ sentinel/header不维护本报告范围可见的 cached size；iOS libc++ header维护
size并在link/unlink更新。iOS RemoveListener还可把 matching runs转入 temporary list再统一释放，
Android边扫描边unlink/delete。两者共同恢复为 `std::list::remove` / ordinary list operations。

不能因为 iOS有size word就给本地 Entry/List包装器增加平台字段；当前 `std::list<Entry>` 和
listener list声明已让目标标准库自行选择布局。

## 7. ordered tree：`map` / `multimap` / `set`

四端 ordered containers均显示红黑树 parent/left/right/color、header sentinel、root/leftmost/
rightmost和node count，但 header/node排列不同：Android为旧libstdc++ tree形态，iOS为 libc++
`__tree` 形态。

source type由操作语义区分：

- `NodeLabelMap`：unique `std::map<ttstr,int,ttstr_utf16_less>`；duplicate返回旧node；
- `ParameterRampMap`：`std::multimap`；equal key总是插入，`equal_range`遍历全部；
- layer-id：unique `std::set<uint32_t>`；find后 erase suffix；
- SeparateLayer active/retired：two `std::map<uint32_t,payload>`；whole-tree O(1) swap；
- cubic basis：`std::map<int,vector<vector<double>>>`；miss先创建 mapped vector。

SeparateLayer swap特别有鉴别力：四端都交换完整root/leftmost/rightmost/size并修复root parent到新
sentinel；这不是两个业务linked lists。erase rebalance helper的层级、node size和EH属于 STL
private implementation，不应移植。

ordered in-order traversal是source-visible稳定顺序；与 unordered不同，本地不能换成hash map。

## 8. unordered map/set 归因

共同 source traits：

- bucket array + chained owning nodes；
- node保存/可访问32-bit cached hash；
- key equality在cached hash命中后执行；
- max load factor为1；
- clear销毁nodes但保留outer bucket allocation，dtor才free buckets；
- rehash不改变key/value owner身份。

目标差异：

| trait | Android old-libstdc++ | iOS libc++ |
|---|---|---|
| empty/default buckets | 可见eager/default bucket policy，某些map选择约10个初始bucket | null/zero起始，首次insert再选bucket |
| rehash policy | prime-like bucket counts，普通 unsigned modulo | power-of-two时mask，否则modulo |
| node order | common observed form为`next -> key/value -> cached hash` | `next -> cached hash -> key/value` |
| bucket/header relation | predecessor/before-begin风格链路与old GNU helpers | libc++ first-node/bucket predecessor形式 |
| empty clear | 可仍memset已有bucket slots | 常按size==0直接return |
| duplicate emplace | 某些specialization candidate-first，发现duplicate再Release/delete | 常find-first，miss才allocate/CopyRef |

timeline state map node allocation在四端为 `0x88/0x70/0x88/0x60`；相同 mapped source value加
pointer width和node field order即可解释。LoadedResourceRecord内两个nested maps、Engine sets/maps
及Player HM1..HM4也共享相同目标 policy。

unordered iteration order从来不是跨STL稳定API。四端 source共同使用 unordered container的路径
要么直接find、要么把每node效果保持独立/非公开顺序；报告中明确记录需要保留 target-specific
physical walk的函数。本地不添加排序，也不试图让 Web 同时复制 Android和iOS互不相同的顺序。

## 9. `std::string`、fixed containers 与 raw arrays

Android旧libstdc++与iOS libc++的 `std::string` size、SSO/COW-era owner表示、copy/move/dtor展开
不同；Resource path split、KRKR atlas source key和临时narrow keys仍反推为 `std::string`，不是
平台自制char buffer。

`std::array`/原生fixed arrays没有动态header、node或growth；colors、rect、matrix、fixed spring
records等直接inline在owner中。三个 controller float buffers显示三次独立 `new[]`、稳定raw pointer
和对应 delete[]，不能为了统一审计改成vector。wind 128-slot pool同样由固定inline slots和free/live
状态证明，不是deque/vector。

## 10. object size为什么不能决定 portable layout

同一 logical members下，目标对象 size分别为：

| object | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| ResourceManager | `0xE8` | `0x80` | `0xC8` | `0x70` |
| EmoteEngine | `0x5D8` | `0x318` | `0x428` | `0x238` |
| Player | `0x568` | `0x3B0` | `0x4B8` | `0x348` |

差值主要来自 pointer width与大量deque/map/unordered/list header。四端 constructor的逻辑
member order一致，destructor逆序closure也一致。因此本地只对真正ABI-independent element record
做合理 static assertions；不要求 Web `sizeof(Player)`等于任一原生对象，也不注入虚假padding。

## 11. exception/unwind 也是目标展开，不是container业务分支

相同 source RAII在四端可以表现为：

- Android arm64：main body landing pads / DWARF unwind；
- Android armv7：EHABI或某些函数不发本帧cleanup body；
- iOS arm64：Mach-O LSDA选择无普通code-xref的cold cleanup；
- iOS armv7：SjLj state machine与独立cleanup函数。

是否存在显式cleanup block不能单独用于否定 C++ owner。应结合 normal dtor、pending allocation、
target ABI和另外三端。TJS deque temporary split-buffer、atlas record vector、map candidate与script
Variant locals均按这个准则归因。

## 12. 真正的 source-level platform/STL-family条件

### 12.1 KRKR atlas record

四端 record owner order与stride：

| target family | 64-bit | 32-bit | owner declaration order |
|---|---:|---:|---|
| Android libstdc++ | `0x40` | `0x2C` | `PSBRawNode iconNode -> rect/tail -> std::string sourceKey` |
| iOS libc++ | `0x50` | `0x34` | `std::string sourceKey -> rect/tail -> PSBRawNode iconNode` |

这不是 container header padding：vector grow时两个非平凡owners的copy顺序、old range reverse
destruction顺序也跟着互换。本地 `PlayerResource.cpp:201` 用 `_LIBCPP_VERSION` 选择相同 member
order，是四端证据要求的真实 source-build分叉，必须保留。

### 12.2 不属于本任务的条件

- `setWind` 的 64/32 stop predicate是pointer-width两两分叉，但不是 STL container lowering；
- Bezier NEON的FMA/非FMA是target FP codegen选择，不是 vector/array类型差异；
- `sizeof(D3DPicture)`、RNG state size等 static assertions是ABI证据，不是容器业务条件。

在 motionplayer source中没有第二个 `_LIBCPP_VERSION` container-record分叉。不能把上述数值/ABI
条件误计成“Android需要不同容器”。

## 13. 本地 source mapping

- `cpp/plugins/motionplayer/internal/player_containers.h`：Player hash/tree/deque aliases；
- `cpp/plugins/motionplayer/Player.h`：node/parameter/event/render state containers；
- `cpp/plugins/motionplayer/EmoteEngine.h`：Engine typed deques、timeline、sets/maps/vectors；
- `cpp/plugins/motionplayer/ResourceManager.h`：outer/nested unordered maps与layer-id set；
- `cpp/plugins/motionplayer/SourceCache.h`：entry list；
- `cpp/plugins/motionplayer/SeparateLayerAdaptor.h`：two ordered maps；
- `cpp/plugins/motionplayer/D3DAdaptor.h`：software texture map；
- `cpp/core/visual/impl/DrawDevice.h`：manager vector；
- `cpp/plugins/motionplayer/RuntimeSupport.cpp:449`：TJS Array owning closure + native Items deque；
- `cpp/plugins/motionplayer/PlayerResource.cpp:201`：唯一STL-family record member-order条件；
- `cpp/plugins/motionplayer/internal/ttstr_hash.h`：portable common key semantics，不硬编码bucket。

`rg`确认 source未使用 `_LIBCPP_VERSION/__GLIBCXX__` 去选择两套 map/deque/list/hash业务类型。
容器声明让当前 toolchain提供物理实现；only atlas record order保留有证据的 family condition。

## 14. supporting reports

本归因以以下 direct evidence为核心：

- Player native ctor/dtor owner ledger；
- EmoteObject/Engine/Player owner chain；
- timeline Track deque/state map container report；
- TJS Array/Dictionary exception-frontier report；
- Resource load/unload map reports；
- SourceCache list report；
- SeparateLayer ordered-map report；
- D3D texture/listener/manager container report；
- `ttstr` hash/equality report；
- KRKR atlas/ImagePacker report；
- MP-C15 all-container boundary report。

规范文件名、addresses和implementation paths位于 `analysis/motionplayer_coverage.tsv`；这里不复制
每个subslice的长地址清单。

## 15. IDB 固化与 disposition

四个 IDB 的8个 attribution roots（共32个地址）写入 `MP-C16` comments并各加task bookmark；
这些roots此前已有准确专项名称，本轮不重复rename。四库在固化后保存。

| 验收项 | disposition |
|---|---|
| vector/deque/list/tree/hash选型 | constructor + operation + dtor三角证据闭合 |
| Android/iOS header/node/block差异 | target STL ABI，不写入portable业务结构 |
| default bucket/rehash/duplicate candidate差异 | target STL policy，共同unordered source |
| clear/dtor/capacity failure差异 | 各STL展开；共同logical element/owner结果已由C15闭合 |
| unordered iteration | target-specific；不人为排序或声称跨平台稳定 |
| object size差异 | pointer width + STL headers；不伪造padding |
| EH/inlining差异 | compiler/ABI；不反推额外业务catch |
| true source family split | KRKR atlas record member order已由 `_LIBCPP_VERSION`准确表达 |
| local implementation | 无新语义缺口，无生产修改 |

因此 `MP-C16` 可标记为 `CLOSED_STATIC`。正式build/runtime verification仍由 `MP-V` 跟踪。
