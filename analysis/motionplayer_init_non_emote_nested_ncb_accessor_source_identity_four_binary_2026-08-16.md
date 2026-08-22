# Player initNonEmoteMotion nested ncb source identity 四参考二进制复原（2026-08-16）

> V162 校正：本文 V147 的 accessor/owner/分支结论仍有效，但“`parameter` 为 init-private
> storage”已被更完整的四端相邻全局序列证据取代。它只有 init 一个 consumer，却位于
> `isValid` 与 `releaseLayerId` 之间的进程级 4-byte hint 序列。详见
> `analysis/motionplayer_init_parameter_hint_global_boundary_four_binary_2026-08-16.md`。

## 范围与结论

本纵切面从 `reference/binaries/` 的四份当前发布物重新反编译
`Player_initNonEmoteMotion_guess`，专门补齐旧初始化纵切面尚未闭合的 C++ source shape：

- `_motionContentVariant` 是否只用于逐次 raw getter，还是先复制/强制成函数级
  `ncbPropAccessor`；
- `priority[0].content` 实际由几个 accessor、几个中间 Variant 组成；
- priority accessor 与 root-item accessor 的精确作用域和析构先后；
- container clear、parameter parse、建树、变量初始化与两个长期 accessor 的生命周期关系；
- `parameterize` 与 `parameter` 两次读取的 typed getter、flags、objthis 和 hint 身份；
- 普通失败 HRESULT、脚本重入清 owner 与异常展开时的可观察边界。

四端共同结果不是 raw helper 链，也不是三个并列的长期 accessor。真实形状是：

1. 一个函数级 motion-content accessor；
2. 一个仅包围 priority 数字索引读取的临时 accessor；
3. 一个由 `[0]` 结果建立、一直存活到函数尾的 root-item accessor。

临时 priority accessor 在 root `content` getter 之前已经释放；root-item accessor 则跨过
node-label/parameter container clear、parameter 分支、`buildNodeTree()`、
`initVariables()` 和后续尾块。motion-content accessor比root accessor释放得更晚。

portable旧代码把这些读取折叠进 `motionPropGet*` wrapper，既没有表达 retained source，也无法
锁定临时 priority owner 的结束位置。本轮已恢复成四端共同的直接 `ncbPropAccessor` 结构。

## 四端函数映射

| 参考二进制 | 入口 | IDA大小 |
|---|---:|---:|
| Android arm64 | `0x6B0A3C` | `0x61C` |
| Android armv7 | `0x580C28` | `0x24E` |
| iOS arm64 | `0x100108258` | `0x31C` |
| iOS armv7 | `0x1058F8` | `0x348` |

函数名来自recovery语义命名。四份发布物均为stripped输入，不能证明原始C++标识符，所以
函数及新增测试入口继续保留 `_guess`。

## 三层 accessor 与 owner tree

四端共同source tree为：

```text
Player._motionContentVariant
└─ copied conversion Variant
   └─ function-wide motionContent ncbPropAccessor
      ├─ typed real loopTime              flags=0, hint=null
      ├─ typed real lastTime              flags=0, hint=null
      ├─ typed Variant tag
      │  └─ copy-assign Player._tagFrameSourceVariant
      ├─ typed Variant priority
      │  └─ copy-assign Player._priorityFrameSourceVariant
      │     └─ temporary priority ncbPropAccessor
      │        └─ typed Variant [0]
      │           └─ function-wide rootItem ncbPropAccessor
      │              └─ typed Variant content
      │                 └─ copy-assign Player._rootContentVariant
      ├─ typed Variant parameterize        shared nonnull hint
      └─ typed Variant parameter           single-consumer global nonnull hint
```

最外层motion accessor建立后，其conversion Variant在第一次property读取前立即析构；因此后续
owner不是栈上转换Variant，而是accessor自身持有的dispatch。

priority路径包含两个不同的accessor。数字getter先把`[0]`写入Variant结果；root accessor从
这个结果copy/force取得source。随后按顺序销毁数字结果、释放临时priority accessor、销毁
priority accessor的conversion Variant，之后才调用root accessor的`content` getter。不能把
priority accessor声明到函数尾，也不能让root `content`继续借用已经结束的priority accessor。

root accessor建立后，`content`结果copy-assign到Player持久Variant。root accessor自身仍继续
存在：脚本getter即使重入清掉Player里的priority/root Variant，也不能提前销毁root dispatch。

## 关键位置

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| motion accessor source copy | `0x6B0A78` | `0x580C46` | `0x100108280` | `0x105920` |
| motion conversion Variant dtor | `0x6B0ACC` | `0x580C5A` | `0x1001082A4` | `0x105970` |
| typed `loopTime` | `0x6B0AE8` | `0x580C78` | `0x1001082C0` | `0x1059A0` |
| typed `lastTime` | `0x6B0B08` | `0x580C8C` | `0x1001082E0` | `0x1059C6` |
| typed `tag` / persistent assign | `0x6B0B38` / `0x6B0B58` | `0x580C9E` / `0x580CA8` | `0x100108304` / `0x100108310` | `0x1059DA` / `0x1059EA` |
| typed `priority` / persistent assign | `0x6B0B8C` / `0x6B0BB0` | `0x580CC2` / `0x580CCE` | `0x100108338` / `0x100108348` | `0x105A0E` / `0x105A20` |
| temporary priority accessor copy | `0x6B0BC4` | `0x580CDC` | `0x10010835C` | `0x105A32` |
| typed numeric `[0]` | `0x6B0C20` | `0x580CFC` | `0x100108388` | `0x105A56` |
| root accessor construction | `0x6B0C2C` | `0x580D06` | `0x100108390` | `0x105A5C` |
| numeric result dtor | `0x6B0C74` | `0x580D12` | `0x1001083A4` | `0x105A6C` |
| temporary priority accessor release | `0x6B0C88` | `0x580D2A` | `0x1001083BC` | `0x105A80` |
| priority conversion Variant dtor | `0x6B0C90` | `0x580D2E` | `0x1001083C4` | `0x105A84` |
| typed root `content` / persistent assign | `0x6B0CBC` / `0x6B0CDC` | `0x580D42` / `0x580D4C` | `0x1001083E4` / `0x1001083F0` | `0x105AA2` / `0x105AB2` |
| node-label/parameter clear开始 | `0x6B0CF0` | `0x580D5A` | `0x100108408` | `0x105AC6` |
| typed `parameterize` | `0x6B0D58` | `0x580D8A` | `0x100108454` | `0x105B18` |
| object append/finalize入口 | `0x6B0D84` | `0x580D98` | `0x10010846C` | `0x105B2A` |
| typed `parameter` | `0x6B0DCC` | `0x580DC2` | `0x1001084AC` | `0x105B70` |
| parameter parse | `0x6B0DEC` | `0x580DCA` | `0x1001084B8` | `0x105B7C` |
| build tree | `0x6B0E60` | `0x580DEA` | `0x1001084E0` | `0x105BAA` |
| parameterize Variant dtor | `0x6B0EA4` | `0x580E2C` | `0x100108524` | `0x105BFC` |
| root accessor release | `0x6B0EB8` | `0x580E44` | `0x100108540` | `0x105C10` |
| motion accessor release | `0x6B0ED4` | `0x580E5A` | `0x100108558` | `0x105C22` |

各架构的寄存器分配与异常landing pad不同，但上述正常路径偏序完全一致。特别是
`priority release < root content < container clear < parameterize < build/init < root release < motion release`。

## parameter hints 的共享与全局序列边界（V162 校正）

| 槽 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| shared `parameterize` | `0x1AB53FC` | `0x1111898` | `0x101B698C4` | `0x187D568` |
| single-consumer global `parameter` | `0x1AB5498` | `0x1111934` | `0x101B69960` | `0x187D604` |

共享槽的数据xref在四端都同时落入：

- `Player_initNonEmoteMotion_guess`；
- `Player_initNodeFields_guess`。

xref数量分别为4、6、2、4，但去重后的语义caller集合四端相同。源码因此不能各声明一个
init-private和node-private槽；现在统一使用
`motion::detail::parameterizeMemberHint_guess`。

`parameter`槽的数据xref去重后四端都只有`Player_initNonEmoteMotion_guess`；V147 曾据此把它
保持为 `Player_parameterListHint_guess` 私有槽。V162 进一步核对左右邻接地址，确认它是
`isValid` 后、`releaseLayerId` 前的独立进程级 cache word，故现已改为
`motion::detail::parameterMemberHint_guess`。两次getter均是typed Variant、flags=0，并把各自
source dispatch同时作为receiver和objthis。

Android arm64的共享槽已经是可独立命名的data item，recovery库命名为
`parameterizeMemberHint_guess`。其余三端的slot仍显示成大块BSS数组内部表达式：
`dword_1111810[34]`、`qword_101B697F8[25]+4`、`dword_187D4E0[34]`。这些库保留既有
`nodeParameterizeMemberHint_guess` listing alias，并在精确data地址写入“Player与node共享”
注释；不能为了统一显示名而拆毁整段相邻hint BSS。实现身份由精确地址和双caller xref决定，
不是由decompiler选择显示base+offset还是alias决定。

## 容器、parameter 与长期生命周期

`_nodeLabelMap.clear()`和`_parameterEntries.clear()`只在motion、tag、priority、root相关owner
全部建立之后执行。root accessor与motion accessor都跨过这两个clear。

随后`parameterize`存入局部Variant：

- Object分支直接append一条entry并finalize；非空时选择front；
- 非Object分支再用同一motion accessor读取`parameter`，parse后按integer/null规则选择；
- parameter局部在非Object分支结束时析构；
- parameterize局部一直活过状态提交、建树、变量初始化与尾部状态更新，然后才析构；
- parameterize析构后才依次释放root accessor和motion accessor。

因此parameter getter或parse中的脚本重入不能通过清Player持久Variant销毁motion/root source。
`buildNodeTree()`或`initVariables()`抛出时，已经提交的container与状态不回滚，但异常展开仍按
root先、motion后的关系释放两个accessor。

## HRESULT 与边界行为

四端的required读取都走ncbind typed conversion：

- `loopTime`、`lastTime`、`tag`、`priority`、numeric `[0]`、`content`、`parameterize`和
  `parameter`共8个typed `GetValue`形状；
- flags均为0；前6项hint为null，后两项分别使用共享/私有非null hint；
- ordinary getter HRESULT不会由caller另行判断；dispatch若先写出可转换值再返回普通失败，
  typed路径仍消费该值；
- 未写出可用值、类型强制失败或getter直接抛异常时，按当前已建立owner逆序展开；
- root `content` getter执行时临时priority accessor已经死亡，这是可由脚本析构副作用观察的
  边界，不只是编译器临时变量美化。

这里没有增加null-dispatch fallback、空priority fallback或额外rollback；发布物中不存在这些
保护。

## portable源码与回归探针

本轮源码修改包括：

- `cpp/plugins/motionplayer/PlayerCore.cpp`
  - `initNonEmoteMotion_guess`建立函数级motion accessor；
  - 4个named motion字段改为直接typed getter；
  - 以full-expression临时priority accessor取得`[0]`并构造长期root accessor；
  - root `content`、`parameterize`和`parameter`改为直接typed getter；
  - V147 当时新增 init-local parameter hint；V162 已按相邻全局布局迁为共享声明/定义；
  - 继续复用共享 parameterize hint。
- `cpp/plugins/motionplayer/NodeTree.cpp`
  - 删除node-private parameterize hint，改用同一共享槽。
- `cpp/plugins/motionplayer/MotionDispatch.h`与`RuntimeSupport.cpp`
  - 声明/定义共享`parameterizeMemberHint_guess`。
- `cpp/plugins/motionplayer/Player.h`
  - 只增加测试用薄入口，不改变production调用链。

定向源码审计确认该init块为3个accessor角色（2个具名长期对象加1个full-expression临时对象）、
8次typed getter、旧`motionPropGet*`调用0次；同一共享hint同时出现在PlayerCore与NodeTree。

新增Catch2 probe
`ordinary init retains motion and root ncb sources across reentrant owner clears`编码下列边界：

1. `loopTime` getter清掉Player最后一份持久motion owner，后续4次motion读取仍成功到达；
2. `priority` getter写出source后清自己的storage，临时accessor继续使priority活到numeric读取完成；
3. numeric `[0]` getter写出root后清priority storage和Player持久priority owner；root仍存活；
4. root `content` getter确认priority accessor已死亡，写出整数7后返回普通失败HRESULT；
5. `parameterize` getter确认root仍活着后主动抛`eTJSError`；
6. unwind先销毁root，root析构时motion仍活；随后才销毁motion；三个dispatch都恰好析构一次；
7. 每次flags、hint、numeric index、objthis和读取顺序都逐项断言。

默认Web/Wasmtime preset关闭tests，所以这条probe由普通与headless完整test TU做语法/类型编译，
没有冒充runtime pass。它把未来启用Catch目标时应锁定的重入/析构关系写成可执行断言；当前
决定性证据仍是四份发布物的逐指令生命周期。

## IDB落地

四个recovery IDB均完成：

- V147 当时写入 `Player_parameterListHint_guess` data 命名；V162 已把四端精确槽统一重建为
  `g_motion_parameterMemberHint_guess`；
- shared parameterize精确data地址增加双caller说明，A64独立item采用共享名，其余三端保留
  大BSS内部alias；
- init函数级V147注释1条；
- accessor建立、8次读取、persistent assign、临时priority teardown、container clear、
  parameter分支、build/init和长期accessor teardown逐地址注释25条；
- shared/single-consumer data 地址注释各 1 条；其中后者的 V147 私有解释已由 V162 校正；
- `V147 initNonEmoteMotion nested ncb source identity` bookmark；
- init与`Player_initNodeFields_guess`均force-recompile成功；
- init反编译四端当时均回读单-consumer hint；V162 已进一步回读其全局序列身份；共享
  parameterize 槽xref四端均回读两个语义caller；
- `search_text(..., include=comments)`在每个init函数范围回读26/26个V147注释；
- 四份数据库最终原位保存成功。

## 验证

- 普通完整test TU `-fsyntax-only`通过，仅有仓库既有`_tss`弃用warning；
- `KRKR2_WASMTIME_HEADLESS=1`完整test TU `-fsyntax-only`通过，仅有同一warning；
- Web Debug完整增量构建与最终wasm链接成功；
- Wasmtime Headless Debug完整增量构建与最终wasm链接成功；
- Web `index.wasm`为85,638,458 bytes，Wasmtime `index.wasm`为84,985,599 bytes；两者都由
  `llvm-objdump -h`完整识别，section解析退出码为0；
- 两个build目录当前都没有注册CTest项目，因此`ctest`报告`No tests were found`；本页不把
  ninja步数或syntax-only编译误写成运行时测试数；
- 定向源码审计和四端IDB读回均通过；最终diff/whitespace检查记录在本轮收尾中。

本纵切面只闭合普通motion初始化器的nested ncb source identity、hint共享、临时/长期owner与
HRESULT/重入边界。parameter entry ABI、ramp multimap、node builder、变量初始化和Chain尾部
数值语义继续由各自四端纵切面约束；这不表示整个motionplayer已经100%复原。
