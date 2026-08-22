# MotionPlayer 四参考二进制对象生命周期基线（2026-08-11）

## 1. 范围与证据原则

本文只记录本轮从 `reference/binaries/` 四份当前 IDB 重新反编译得到的 `Player` / `EmoteEngine` 构造、析构和直接所有权证据。旧 `libkrkr2.so` 注释中的单体地址不再作为入口依据。

本轮已确认两个有代表性的过时地址：

- 旧注释所称 Android arm64 `Player_ctor @ 0x6CED30`，在当前参考二进制中位于 `sub_6CE908` 的大型渲染/层导出函数内部，不是构造函数。
- 旧注释所称 Android arm64 `Player_dtor @ 0x6CFADC` 也不是当前析构入口；当前实际入口是 `0x6CCEBC`。

因此编译源码中的说明只描述四端共同语义；当前地址和 ABI 偏移集中保留在本文。

## 2. Player 构造入口、对象大小和来源调用点

| 目标 | 当前 `Player` 大小 | 构造实现 | type-3 child node-init | child 分配/构造证据 |
| --- | ---: | ---: | ---: | --- |
| Android arm64 | `0x568`（1384） | `0x6CC110` | `0x6B1058` | `operator new(0x568)`，随后调用 `0x6CC110` |
| Android armv7 | `0x3B0`（944） | `0x5935C4` | `0x580FA4` | `operator new(0x3B0)`，随后调用 `0x5935C4` |
| iOS arm64 | `0x4B8`（1208） | `0x10011EC04` | `0x100108720` | `operator new(0x4B8)`，随后调用构造实现 |
| iOS armv7 | `0x348`（840） | `0x11D488` | `0x105E70` | `operator new(0x348)`，随后调用构造实现 |

相关递归 node builder 分别为 Android arm64 `0x6B1E4C`、Android armv7 `0x5818B0`、iOS arm64 `0x100109328`、iOS armv7 `0x106BDC`。四端都由 type-3 节点路径分配完整 child `Player`，因此对象大小和构造入口不依赖旧符号猜测。

四端构造的共同语义可归纳为：

```text
Player(rootOwner=self, parent=null, currentDispatch=null)
construct label map / node deque / variants / vectors / maps / render state
retain the same ResourceManager dispatch in three independent Variant owners
create persistent source descriptor dictionary
create persistent color dictionary
descriptor.color = colors
seed scalar defaults:
  packedColor = -8355712
  pixelateDivision = 100
  identity-like values = 1.0
  one smoothing/default coefficient = 0.2
  z-like scale = 1.5
  bounds min/max = +/- max double
append one constructor-owned synthetic root node
copy the four root transform/order defaults into that node
```

共同结构之外，Android 使用 libstdc++ 容器布局，iOS 使用 libc++ 容器布局；32 位与 64 位的指针、`ttstr`/variant owner 和容器头宽度也不同。因此四份对象大小分别是 `0x568/0x3B0/0x4B8/0x348`，不能把 Android arm64 的 `+1064` 等偏移推广为跨平台源码布局。

## 2.1 Motion.Player NCB 构造、挂载与空 adaptor 哨兵

旧 `main.cpp` 注释把 Android arm64 的 NCB factory/ctor 入口写成了当前二进制中
不成立的旧地址。由第 2 节四个真实 `Player_ctor_guess` 反向追踪，当前脚本构造链为：

| 阶段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| NCB constructor callback | `0x6F3FB0` | `0x5B0798` | `0x100146384` | `0x1468E4` |
| allocate/construct/attach invoke | `0x6F4088` | `0x5B0828` | `0x100146428` | `0x146950` |
| 独立 allocate + ctor helper（若未内联） | `0x6F41A0` | 内联于上一项 | 内联于上一项 | `0x146A98` |
| 首参数 Variant CopyRef helper | `0x6F424C` | `0x5B090C` | `0x10014654C` | `0x146B6C` |

四端共同边界如下：

1. constructor method 被嵌套 member name 调用时先走普通基类分派；实际构造路径只处理
   默认 member。
2. `numparams == 1 && param[0].Type() == tvtVoid` 是 ncbind 内部的空 adaptor
   哨兵：立即返回成功，不分配或挂载 `Player`。`CreateAdaptor` 正是用该路径先取得
   adaptor 壳，再手工写入 sticky/native pointer。
3. 其余路径要求 `numparams >= 1`；零参数返回 `TJS_E_BADPARAMCOUNT`。大于一的参数
   数量被接受，但构造 helper 只 CopyRef `param[0]`，后续参数完全不读取。
4. 以当前 ABI 的精确对象大小分配 `Player`，把该首参数 Variant 传入
   `Player_ctor_guess`，随后销毁栈上临时 Variant。这个值是 dispatch closure owner，
   不是预先 unbox 的 `ResourceManager *`。
5. 构造成功后，通过接收者的 `NativeInstanceSupport(GETINSTANCE,
   PlayerClassID, &adaptor)` 查找 Player adaptor，再写 native pointer。接收者为空、
   查询失败或 adaptor 为空都返回 `TJS_E_NATIVECLASSCRASH`，并先完整析构、再释放
   刚构造的 Player。
6. 分配后若 Variant 转换、Player 构造、挂载或其他调用抛异常，landing pad 会释放
   尚未完成构造的 allocation；若完整 Player 已存在，则调用 Player 析构后释放，
   再重新抛出。成功挂载后，adaptor 的 `Invalidate`/析构路径在非-sticky 状态下拥有
   Player；它 delete 后无条件把 native slot 和 sticky byte 清零。

本地 `NCB_CONSTRUCTOR((tTJSVariant))` 所实例化的 ncbind 模板与上述边界同形，故保留
其代码结构，只删除旧地址叙事并换成四端语义注释。新增回归覆盖零参数失败、单 Void
空 adaptor、两参数时只使用第一个 ResourceManager dispatch，以及 adaptor 释放所拥有
Player 的正常生命周期。

## 3. Player 析构入口和精确阶段顺序

| 目标 | 当前析构实现 | `resetAndReleaseOldNodeTree` | SeparateLayerAdaptor 字段 |
| --- | ---: | ---: | ---: |
| Android arm64 | `0x6CCEBC` | `0x6B2AD8` | `+760` |
| Android armv7 | `0x593C24` | `0x581F3C` | `+500` |
| iOS arm64 | `0x10011F2A0` | `0x100109ACC` | `+648` |
| iOS armv7 | `0x11DCC4` | `0x107358` | `+436` |

实际析构入口由 reset helper 的 fresh xref 反向确认，而不是由旧注释推断。四份析构体的共同阶段严格为：

```text
purge parameter-ramp references from self and ancestor maps

// 必须仍早于 node tree reset
destroy every live element in parameterEntries
parameterEntries.end = parameterEntries.begin

destroy one intermediate owner/container
resetAndReleaseOldNodeTree()
  release each non-root node's layerId1/layerId2/prepared render-layer id
  destroy child Players and non-root nodes
  preserve constructor-owned root node

delete SeparateLayerAdaptor
SeparateLayerAdaptor = null
destroy ordinary members in reverse declaration order
destroy the surviving root with the node deque member
```

parameter vector 的四端布局也直接显示了这个早清阶段：Android arm64 `+384/+392`、元素步长 `56`；Android armv7 `+252/+256`、步长 `48`；iOS arm64 `+296/+304`、步长 `56`；iOS armv7 `+204/+208`、步长 `44`。各 ABI 的元素宽度不同，但“purge 后、node reset 前将 vector 变为空”的生命周期边界完全一致。

四端在 reset 前额外销毁的中间 owner/container 分别通过 Android arm64 `sub_6BE1C8(a1+1296)`、Android armv7 `sub_58A43C(a1+224)`、iOS arm64 `sub_100129DE8(a1+1152)` 表现；iOS armv7 反编译器把同一调用的实参传播隐藏为 `sub_128CC0()`。这不改变参数 vector 与 node tree 的相对顺序。

## 4. EmoteEngine 构造入口、Player 所有权和布局差异

| 目标 | 当前 engine 构造 | Player 分配/调用点 | Player 字段 | 7 个 controller 字段 | wind 字段 |
| --- | ---: | --- | ---: | --- | ---: |
| Android arm64 | `0x67B76C` | `0x67BA1C: new(0x568)`；`0x67BA28 -> 0x6CC110` | `+1064` | `+1072..+1120` | `+1128` |
| Android armv7 | `0x560948` | `0x560AC4: new(0x3B0)`；`0x560ACA -> 0x5935C4` | `+532` | `+536..+560` | `+564` |
| iOS arm64 | `0x1001B7FB0` | `0x1001B803C: new(0x4B8)`；经 `0x100109164` thunk 到 `0x10011EC04` | `+696` | `+704..+752` | `+760` |
| iOS armv7 | `0x1B7788` | `0x1B7850: new(0x348)`；经 `0x106990` thunk 到 `0x11D488` | `+348` | `+352..+376` | `+380` |

共同构造数据流为：

```text
construct/zero ten deque headers and the scalar/container region
allocate Player using this ABI's exact Player size
construct Player with the incoming ResourceManager dispatch owner
store Player in a one-pointer `std::unique_ptr` owner
allocate seven controller objects into consecutive one-pointer
`std::unique_ptr` owners in declaration order
initialize engine flags/default scalars
reset controller values in native order:
  position -> scale -> angle -> color
seed color controller with {128, 128, 128, 255}
```

Android arm64 构造体显示十个逐项 deque 初始化；Android armv7 同样按 `+0,+40,...` 初始化十个 deque；iOS arm64 先 `bzero(0x218)`，iOS armv7 先 `memset(0x10C)`，然后再构造需要非平凡初始化的对象。差异来自标准库和 ABI，不是高层生命周期差异。

## 5. EmoteEngine 正常析构入口与 ctor-unwind 陷阱

| 目标 | 当前正常析构实现 | Player 析构调用 |
| --- | ---: | --- |
| Android arm64 | `0x67C898` | 直接调用 `0x6CCEBC` |
| Android armv7 | `0x5610E8` | 经 `0x563C5E` thunk 调用 `0x593C24` |
| iOS arm64 | `0x1001B8B4C` | 经 `0x10011F548` thunk 调用 `0x10011F2A0` |
| iOS armv7 | `0x1B814E` | 经 `0x11E034` thunk 调用 `0x11DCC4` |

iOS arm64 `0x1001B829C` 是构造失败时的 `__noreturn` unwind cleanup landing pad，最终 resume unwind；iOS armv7 `0x1B7B02` 是对应的 SjLj ctor-unwind cleanup。Android arm64 把同一类 landing blocks 合并在 ctor 尾部。三者都会先释放尚未写入 member slot 的临时 allocation，再按已成功构造的 owner 前缀逆序析构；它们都不是正常析构入口，不能用来推导成功构造对象的完整析构顺序。

2026-08-13 的 direct-owner 复核纠正了本文早期的 “owning raw pointer” 表述。Android
armv7 正常析构保留了三个接收 slot 地址的 specialization：Var controller、Angle controller
和 Player。它们与 Android libstdc++ 的 `unique_ptr` 析构形态一致，delete 后清 slot；iOS
libc++ 两端则先把 slot exchange 为 null，再析构/delete。结合三端 ctor-unwind 自动销毁已构造
member 前缀，八个字段的共同源码类型是单指针宽度 `std::unique_ptr<T>`，而不是普通 `T*`。
独立专题见 `motionplayer_engine_direct_owner_unwind_four_binary_2026-08-13.md`。

四份正常析构体共同显示：

```text
delete wind emitter first
(normal destructor does not clear the dying raw-owner slot)

destroy trailing maps/vectors/containers in reverse declaration order

for unique_ptr controller member = last .. first:
  libc++: exchange field to null, then destroy/delete old pointer
  libstdc++: destroy/delete pointer, then store null

destroy the Player unique_ptr member with the same library-specific order

destroy remaining earlier maps/vectors/deques in reverse declaration order
```

特别是 wind 的顺序在四端都一致：它早于所有七个 direct controller 和 Player，也早于较早声明的 spring/deque 成员析构。bust-chain spring 的 `collisionCurve` 只是借用 wind；原版允许该字段在 spring 对象稍后析构前短暂悬空，证明这些析构路径不会解引用该 borrow。

2026-08-13 的 `setWind` replacement 复核又确认 wind 本身是 raw owner，而不是第 5 节
其余八个单指针字段所用的 `unique_ptr`：replacement 先 delete、slot 不清零、再尝试分配，
allocation failure 会留下悬空字段。见
`motionplayer_wind_raw_owner_replacement_four_binary_2026-08-13.md`。

## 6. 与本地实现的逐行对照（修改前）

| 本地位置 | 四参考二进制 | 结论 |
| --- | --- | --- |
| `Player::~Player()` 先调用 `purgeParameterRampMapLike_*` | 相同 | 保留 |
| purge 后立即遍历/释放 non-root layer ids 并 reset node tree | 四端都先显式清空 `_parameterEntries`，再进入 tree reset | **缺少可观察的 vector 生命周期阶段**；必须在 reset 前 `_parameterEntries.clear()` |
| reset 后删除 `_renderSeparateLayerAdaptor` | 四端相同 | 保留 |
| `EmoteEngine::~EmoteEngine()` 先清十组 deque-owned controller/spring | 四端第一项都是 delete raw wind owner；较早 deque 到最后阶段才析构 | **wind 销毁时点过晚** |
| 本地在 spring 清理后才 delete wind，并以“避免 dangling borrow”解释 | 四端刻意先删 wind，随后才析构持有 borrowed `collisionCurve` 的 spring | 旧解释与四端相反，必须删除 |
| 七个 direct controller 作为 raw member 手动删除 | 四端实际是 `unique_ptr` 逆声明顺序析构 | 改为单指针 owning wrapper；显式阶段用 `reset()` |
| Player raw member 在 direct controller 后手动删除 | 四端实际是声明更早的 `unique_ptr<Player>` | 改为 `unique_ptr<Player>`；相对阶段不变 |
| trailing typed STL 成员依赖 C++ 自动析构 | 四端在 controller 之前析构 HM7..HM4/三个 Variant，在 Player 后析构 timeline/mirror 容器，最后才是 deque #10..#1 | 后续四端完整字段映射已闭合；见 `analysis/motionplayer_engine_destruction_order_four_binary_2026-08-11.md` |

本轮低风险、四体直接支持的实施项只有两项：

1. `Player` purge 后、node-tree teardown 前显式 `_parameterEntries.clear()`。
2. `EmoteEngine` 析构体入口 delete `_windEmitter`，删除原先位于 spring/controller 之后的 wind cleanup，并把注释改为地址无关的四体共同语义。stop 分支另外执行 delete + null。

本节当时刻意保留的 trailing-container 精确性缺口，已在后续
`analysis/motionplayer_engine_destruction_order_four_binary_2026-08-11.md` 中通过
四端正常析构体的完整字段映射闭合：portable 析构现按
`HM7..HM4/Variant -> controller -> Player -> timeline/mirror -> deque #10..#1`
释放元素和 backing storage。

这两项改变对象生命周期和边界行为，但不把任何单一 ABI 偏移硬编码进本地 C++ 布局。

## 7. 实施与验证

已按第 6 节实施两项修正；后续 2026-08-13 direct-owner 复核又把 Player 与七个 direct
controller 字段恢复为 `unique_ptr`，并保留正常析构中的既定 `reset()` 阶段。构造、正常析构及
ctor-unwind cleanup 入口/注释已写回四份 IDB，四份数据库均原位保存成功。

- `out/web/debug`：motionplayer 受影响目标重新编译，静态库和最终 `index.html/index.wasm` 链接成功。
- `out/wasmtime/debug --target krkr2_wasmtime_guest`：guest wasm 链接并完成 exnref 转换；随后串行增量复验为 `ninja: no work to do`。
- 初次并行启动两份 `emsdk_env.ps1` 时，二者争写全局 `emsdk_set_env.ps1`，Wasmtime 日志出现一次 `PermissionError`，但构建进程返回 0。串行复验消除了该环境初始化竞争；这不是源码或链接错误。
- `git diff --check`：通过；只有工作区既有的 LF→CRLF 提示。

## 8. EmoteObject：三成员真实结构与构造入口

对第 4 节已确认的 `EmoteEngine` 构造入口做 fresh xref，并重新反编译完整 caller 后，得到当前 `EmoteObject` 初始化入口：

| 目标 | EmoteObject 大小 | init/construct 实现 | RM 大小/构造 | Engine 大小 | RM adaptor wrapper |
| --- | ---: | ---: | --- | ---: | ---: |
| Android arm64 | `0x28`（40） | `0x67AF8C` | `0xE8` / `0x6A5CAC` | `0x5D8` | `0x67B5EC` |
| Android armv7 | `0x14`（20） | `0x5604B8` | `0x80` / `0x57B1EC` | `0x318` | `0x56083C` |
| iOS arm64 | `0x28`（40） | `0x1001B4984` | `0xC8` / `0x100101268` | `0x428` | `0x1001B4E40` |
| iOS armv7 | `0x14`（20） | `0x1B4500` | `0x70` / `0xFE3B0` | `0x238` | `0x1B49E0` |

四体开头都把对象初始化成严格三个字段：

```text
+0                  owning ResourceManager*
+pointerSize        owning EmoteEngine*
+2*pointerSize      vector<ttstr> begin/end/cap
```

即 64 位为 `8 + 8 + 24 = 40` 字节，32 位为 `4 + 4 + 12 = 20` 字节。对象内没有第四个 RM-dispatch/Variant 字段。

共同构造和数据流为：

```text
zero {rm, engine, modulePaths vector}
kag = eval("global.kag")
rm = new ResourceManager(kag, 20 MiB)

adaptor = make sticky NCB adaptor around rm
temporaryVariant = object Variant(adaptor, adaptor)  // 两个对象槽各持有引用
engine = new EmoteEngine(temporaryVariant)
destroy temporaryVariant                            // 早于路径 vector copy

modulePaths = inputPaths
for path in modulePaths:
  loaded = rm.load(path)                             // 最后一项留在 loaded
metadata = loaded.metadata
base = metadata.base
chara = base.chara
motion = base.motion
engine.Player.project = inputPaths.back()
engine.Player.chara = chara
engine.Player.play(force, motion)
engine.applyMetadata(metadata)
```

temporary Variant 的精确释放点在 Android arm64 `0x67B080`、Android armv7 `0x56054A`、iOS arm64 `0x1001B4A60`、iOS armv7 `0x1B4608`；四端都在 engine 字段写入之后、modulePaths copy 之前。Engine/Player 内部已经把同一 dispatch 分别保存在自己的 owner 中，因此外层对象不再持有第四份 persistent owner。

## 9. EmoteObject 正常析构与本地结构差异

| 目标 | 正常析构实现 | Engine 析构 | RM 析构 | modulePaths 析构 |
| --- | ---: | ---: | ---: | ---: |
| Android arm64 | `0x67C800` | `0x67C898` | `0x6A5F74` | 析构元素后 free buffer |
| Android armv7 | `0x5610BE` | `0x5610E8` | `0x57B2E4` | `0x4976AE` |
| iOS arm64 | `0x1001B5058` | `0x1001B8B4C` | `0x1001012D4` | `0x10002E0F8` |
| iOS armv7 | `0x1B4CCE` | `0x1B814E` | `0xFE408` | `0x2CAC4` |

共同析构顺序是：

```text
if engine:
  engine.~EmoteEngine()
  operator delete(engine)
if rm:
  rm.~ResourceManager()
  operator delete(rm)
modulePaths.~vector<ttstr>()
```

四端正常析构没有清理 persistent RM-dispatch member，因为该成员不存在。Engine 先于 RM 被销毁，保证所有 Player/child Player 持有的 adaptor Variant 在 native RM 仍存活时释放；sticky adaptor 不接管 RM 的 delete。

> **2026-08-13 owner 类型补充：** 正常析构中的显式 destructor/delete 本身不足以
> 区分 raw pointer 与内联的 `unique_ptr`。fresh constructor-unwind 证明，RM/Engine 一旦
> 成功构造并写入 member slot，后续 paths/load/metadata 异常只销毁 vector 和临时 TJS
> owner，不释放两个已发布 heap 对象；只有尚未完成 constructor 的 pending allocation
> 会被 delete。两字段因此是故意保留失败泄漏边界的 raw owner。完整四端 landing-path
> 对照见 `motionplayer_emoteobject_raw_owner_ctor_failure_four_binary_2026-08-13.md`。

### 9.1 与本地实现的逐行对照（修改前）

| 本地位置 | 四参考二进制 | 结论 |
| --- | --- | --- |
| `EmoteObject` 字段 `_rm, _rmDispatch, _engine, _modulePaths` | 只有 `_rm, _engine, _modulePaths` | **多出一个 persistent Variant，改变结构和 refcount 生命周期** |
| ctor 将 adaptor 存入 `_rmDispatch` | adaptor Variant 只在栈上 | 应改为构造局部 owner |
| ctor 在整个 init/load/metadata 流程中保留 `_rmDispatch` | Engine 构造后、路径 vector copy 前立即销毁临时 Variant | 应用内层作用域精确恢复释放点 |
| dtor 在 engine 后执行 `_rmDispatch.Clear()` | Engine 后直接析构 RM | 删除该额外阶段；Engine/Player 自有 Variant 已在 Engine 析构中释放 |
| engine -> RM -> modulePaths 的相对析构顺序 | 四端同序 | 保留 |

实施方向：删除 `_rmDispatch` 字段；构造时用局部 Variant 包住 sticky adaptor，在同一内层作用域构造 Engine，并在作用域结束时、`_modulePaths = modulePaths` 之前释放临时 Variant。这样同时恢复三成员源码拓扑、构造异常清理和正常析构阶段。

### 9.2 实施与验证

已删除 `_rmDispatch` 成员，并把 sticky adaptor owner 改成仅包围 Engine 构造的局部 `tTJSVariant` 作用域；正常析构现在直接执行 Engine -> RM，paths 仍由普通成员析构。

- 四份 `EmoteObject` init/dtor 入口已分别以 `EmoteObject_init_guess` / `EmoteObject_dtor_guess` 写回 IDB，dry-run 与正式 rename 均成功，四份数据库已原位保存。
- 同一个已初始化 Emscripten shell 中串行运行 Web build 和 Wasmtime guest build，二者均完整编译、链接成功，Wasmtime 也完成 exnref 转换。
- 源码树中不再存在 `_rmDispatch` 引用；分析文档保留“修改前”对照项。
- `git diff --check` 通过，只有工作区既有 LF→CRLF 提示。

## 10. Motion.EmotePlayer：NCB adaptor 直接拥有 Engine

### 10.1 typed Factory、必需首参数与 empty-shell sentinel

从第二组 `EmoteEngine_ctor_guess` xref、当前 `TimelinePlayFlagParallel` UTF-16LE 注册字符串和完整 callback 重新定位出：

| 目标 | typed Factory `FuncCall` | construct + attach | 独立 make-Engine helper | first-arg Variant normalizer | member registration |
| --- | ---: | ---: | ---: | ---: |
| Android arm64 | `0x689CA4` | `0x689D7C` | `0x689E94` | `0x689F40` | `0x67CEA8` |
| Android armv7 | `0x56A280` | `0x56A310` | 内联 | `0x56A3F4` | `0x5612E8` |
| iOS arm64 | `0x1001C5F18` | `0x1001C5FBC` | 内联 | `0x1001C60E0` | `0x1001B5130` |
| iOS armv7 | `0x1C3158` | `0x1C31C8` | `0x1C3310` | `0x1C33E4` | `0x1B4DE0` |

四个 outer `FuncCall` 共同先执行：membername 非空返回 `-1001`；恰好一项 Void 在 result clear
之前作为 empty-adaptor sentinel 返回成功；其余路径清 result；`numparams < 1` 返回 `-1004`；一项
以上才进入 construct + attach。因此普通零参数调用不可能到达 first-arg normalizer。

进入 typed invoke 后，四端 first-arg normalizer/construct 分支完全一致：

```text
rmDispatch = owning by-value copy(*param[0])

engine = operator new(ABI-specific EmoteEngine size)
EmoteEngine_ctor(engine, rmDispatch)
destroy rmDispatch

adaptor = objthis.NativeInstanceSupport(GETINSTANCE, EmotePlayer class id)
if adaptor exists:
  adaptor.instance = engine
  return TJS_S_OK
else:
  engine.~EmoteEngine()
  operator delete(engine)
  return TJS_E_NATIVECLASSCRASH       // -1008
```

normalizer 内部保留的 `<1 -> Void` 泛型分支被 outer lower-bound gate 截断，不能用来推导脚本零参
行为。一参数透传首个 Variant，多余参数被 wrapper 接受但不读取。旧结论“零参数构造持有 Void RM
的 Engine/首参可选”不准确；真实注册形态是 `EmotePlayer *factory(tTJSVariant)` 的 typed Factory。

Android arm64/iOS armv7 把 allocation+Engine ctor 拆成 helper，Android armv7/iOS arm64 在 NCB callback 内联；这是编译器函数边界差异，不是所有权差异。

### 10.2 generic adaptor 的 instance/sticky 生命周期

| 目标 | adaptor `_deleteInstance`/invalidate body | instance 字段 | sticky 字节 |
| --- | ---: | ---: | ---: |
| Android arm64 | `0x6836B0` | `+8` | `+16` |
| Android armv7 | body `0x5651C8`（Invalidate/析构 cluster 从 `0x565178` 开始） | `+4` | `+8` |
| iOS arm64 | `0x1001B9180` | `+8` | `+16` |
| iOS armv7 | `0x1B883A` | `+4` | `+8` |

这四个函数与仓库 `ncbInstanceAdaptor<T>::_deleteInstance()` 的模板布局和行为一一对应：

```text
if adaptor.instance != null && !adaptor.sticky:
  adaptor.instance.~EmoteEngine()
  operator delete(adaptor.instance)
adaptor.instance = null
adaptor.sticky = false
```

所以原版对象链是：

```text
TJS EmotePlayer object
  -> ncbInstanceAdaptor payload/sticky
       -> one heap EmoteEngine
            -> one heap Player
```

这里没有 `EmoteObject`、独立 ResourceManager 或 modulePaths vector；`EmoteObject` 只属于 `D3DEmotePlayer` 的另一条链。二进制层面无法区分模板源码是“直接把 EmoteEngine 注册成 EmotePlayer native type”还是“零数据 facade 类型复用 Engine 存储”，但可以排除“facade 再 owning 一个 EmoteObject/Engine 指针”的额外堆层。

## 11. Motion.EmotePlayer.clear 不是生命周期 teardown

从四端 member registration 中的当前 UTF-16LE `clear` 条目确认：

| 目标 | clear member wrapper | wrapper 调用的 Player body | Engine 内 Player 字段 |
| --- | ---: | ---: | ---: |
| Android arm64 | `0x67EE44` | `0x6D0160` | `+1064` |
| Android armv7 | `0x561DA8` | `0x595720` | `+532` |
| iOS arm64 | `0x1001B5D04` | `0x10012139C` | `+696` |
| iOS armv7 | `0x1B595C` | `0x120168` | `+348` |

四份 wrapper 都只做：

```text
target = owning copy(arg0)
fill = owning copy(arg1)
Player_drawToLayerRecursive(engine.player, target, fill)
destroy fill
destroy target
```

底层 Player body 已经在本地 `Player::drawToLayerRecursive_guess` 建模为 gated recursive draw-to-target：motion Variant 非 void 时，依次尝试 D3DAdaptor 快速清理、SeparateLayerAdaptor target 解包和普通 Layer/callable fill，然后递归 type-3 child Player；D3D 命中或无 MainImage Layer 会提前阻断递归。完整边界见 `analysis/motionplayer_draw_to_layer_four_binary_2026-08-11.md`。它不删除、置空或重建 Engine/Player。`clear` 这个 TJS 名称与实现语义本来就不直观，但四端注册绑定没有歧义。

## 12. 与本地 Motion.EmotePlayer 的逐行对照（修改前）

| 本地位置 | 四参考二进制 | 结论 |
| --- | --- | --- |
| `NCB_CONSTRUCTOR(())` + `EmotePlayer() = default` | typed Factory 要求至少一个 Variant；正常路径分配 Engine 并只透传必需的首个 Variant，唯一一项 Void 是 empty-shell sentinel | **本地零参构造与 payload 拓扑都不匹配；脚本方法还会解引用空对象** |
| `EmotePlayer` own `_primaryObj` | generic adaptor 直接 own Engine | **错误引入 EmoteObject、RM、paths 和额外堆分配** |
| accessor 经 `_primaryObj->engine()` | adaptor instance 本身就是 Engine payload | 应恢复单 Engine 存储 |
| `EmotePlayer::~EmotePlayer()` delete EmoteObject | adaptor `_deleteInstance` 对 Engine 执行正常析构/delete | 应直接走 Engine 基类/本体析构 |
| `EmotePlayer::clear()` delete `_primaryObj` 并置 null | clear wrapper 把两个 Variant 转给 Player recursive draw-to-layer | **把普通渲染方法误实现成生命周期 teardown** |
| `NCB_METHOD(clear)` typed 二参数绑定 | native wrapper 有两个 Variant 实参槽 | 与四端一致；不得改成 optional raw callback |

本地最接近二进制单堆对象结构的表达是让 `EmotePlayer` 成为**无新增数据、无虚函数**的 `EmoteEngine` facade 派生类型：构造直接转发 Engine ctor，析构自动进入 Engine dtor，并用 `static_assert(sizeof(EmotePlayer) == sizeof(EmoteEngine))` 防止悄然长出额外 storage。ncbind 的 `ncbInstanceAdaptor<EmotePlayer>` 自身已经提供二进制同形的 instance/sticky 字段。注册使用一项 `tTJSVariant` 的 typed pointer-return factory；普通零参数失败，唯一一项 Void 只创建 empty shell，surplus 被忽略。

这是一种本地模板适配；能确定的原版边界是“adaptor 到单个 Engine-sized payload，无 EmoteObject 中层”，不能仅凭反编译声称原始 C++ 一定写了继承声明。

## 13. 本地恢复结果与验证

已按第 10～12 节证据完成以下实现：

- `EmotePlayer` 改为无新增字段、无虚函数的 `EmoteEngine` facade；`engine()` 直接返回自身，`player()` 直接进入基类持有的 Player。
- 删除 Motion.EmotePlayer 原先错误引入的 `_primaryObj`/`EmoteObject` 中间层和自定义 teardown；D3DEmotePlayer 的独立 EmoteObject 链保持不变。
- 用 `static_assert(sizeof(EmotePlayer) == sizeof(EmoteEngine))` 固化“单个 Engine-sized payload”的本地结构约束。
- NCB 构造改为 typed pointer-return factory：普通零参数返回 BADPARAMCOUNT，唯一一项 Void 是 empty-shell sentinel，一项以上只复制首参并忽略 surplus，然后创建 payload。
- `clear` 保持 typed two-Variant method，参数 owner 建立后无条件进入 Player 的
  motion-content gate与递归draw-to-layer worker；不再触碰对象生命周期。2026-08-15
  registrar/body复核纠正了本文早期的 raw/optional 误判，见
  `analysis/motionplayer_emoteplayer_clear_contains_typed_four_binary_2026-08-15.md`。

验证结果：

- `cmake --build out/web/debug --parallel`：成功（9/9）。
- `cmake --build out/wasmtime/debug --target krkr2_wasmtime_guest --parallel`：成功（8/8）。
- 编译通过上述 size `static_assert`；警告仅为仓库既有 `_tss`、`nodiscard`、pthread memory growth 与 JSPI 警告。
- `git diff --check`：通过，仅有工作树 LF→CRLF 提示。

四个 IDB 已按 `_guess` 规则命名并保存：

- `EmotePlayer_ncb_construct_guess`
- `EmotePlayer_makeEngineFromFirstArg_guess`（仅 Android arm64 / iOS armv7 的独立函数边界）
- `EmotePlayer_normalizeFirstCtorArg_guess`
- `EmotePlayer_ncb_registerMembers_guess`
- `EmotePlayer_clear_guess`
- `Player_drawToLayerRecursive_guess`

## 14. D3DEmotePlayer 双槽生命周期与 clone

> **2026-08-12 更正：** 本节关于双槽 teardown、EmoteObject
> serialize/unserialize 和新壳默认 scalar/flag 的结论继续有效；但“clone 无参并沿用
> old.owner”已被 fresh typed-wrapper 证据推翻。真实签名是
> `D3DEmotePlayer *clone(D3DLayer *targetOwner)`，新壳注册到显式传入的目标 owner。
> factory 也不是 raw `tjs_error` callback，而是 typed pointer-return factory。完整
> wrapper/result/lifetime 边界以
> `analysis/motionplayer_d3d_shell_lifecycle_four_binary_2026-08-12.md` 为准。

### 14.1 四端函数映射

| 阶段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| member registrar | `0x52E8E4` | `0x494078` | `0x100232278` | `0x230F46` |
| `clear`/双槽 teardown | `0x530164` | `0x4948C4` | `0x100232C1C` | `0x231840` |
| `load` callback | `0x5301B4` | `0x494920` | `0x100232CB0` | `0x231890` |
| `clone` callback | `0x53039C` | `0x4949D4` | `0x100232DC8` | `0x2319DC` |
| EmoteObject clone helper | `0x67CD58` | `0x5611FC` | `0x1001B50A4` | `0x1B4CFC` |
| shell ctor used by clone | callback 内联 | `0x497824` | `0x100236300` | `0x235022` |
| shell normal dtor | `0x533FE0` | `0x497870` + `0x497988` | `0x100236374` + `0x1002364C4` | `0x235076` + `0x235164` |

四份构造布局按 ABI 归一化后都是：owner/listener base 状态、两个 null EmoteObject 槽、两个 `1.0f` scale 字段、两个 false 字节。对象大小为 arm64 的 `0x38` 和 armv7 的 `0x24`。clone 创建的新壳也走同一构造语义，因此壳层 scale/visible/smoothing 保持构造默认值，并不从旧壳复制。

member registrar 共同暴露 `module/clear/load/clone/show/hide/visible/smoothing/meshDivisionRatio/queing/hairScale/partsScale/bustScale/...`。本地 D3DEmotePlayer 壳上的 `_useD3D`、`_opengl`、`_drawVisible`、`_drawOpacity` 及其访问器既不在四端构造布局中，也不在四端 member table 中；仓库内除旧 clone 的手工复制外没有使用点。

### 14.2 clear、load、析构的共同数据流

```text
clear(shell):
  if shell.secondary: destroy/delete shell.secondary
  if shell.primary:   destroy/delete shell.primary
  shell.primary = null
  shell.secondary = null

load(args, shell):
  clear(shell)
  paths = vector<ttstr>()
  for every supplied TJS argument in order:
    paths.push_back(ttstr(argument))
  shell.primary = new EmoteObject(paths)
  destroy paths
  return OK

~D3DEmotePlayer():
  clear-equivalent double-slot teardown
  if owner: owner.RemoveListener(this)
```

四端在这些边界上没有语义分歧。`load` 即使零参数也仍创建一个空-paths EmoteObject；访问主槽的方法不做 null 检查。Android arm64 把 slot teardown 直接展开在 load/dtor，另外三个目标更多复用独立 `clear` body，这是内联差异。

### 14.3 clone 的共同数据流

以下当时对 clone 内部 EmoteObject 迁移的描述仍成立；其中 owner 形参已按上方
2026-08-12 更正替换为调用者显式传入的 `targetOwner`：

```text
cloneShell(old, targetOwner):
  fresh = new D3DEmotePlayer(targetOwner) // remaining shell fields stay defaults
  fresh.primary = cloneEmoteObject(old.primary)
  return fresh native object/adaptor

cloneEmoteObject(old):
  fresh = new EmoteObject(old.modulePaths)
  state = old.engine.serialize()
  fresh.engine.unserialize(state)
  destroy state
  return fresh
```

EmoteObject clone helper 四端都以旧对象的 paths vector 构造新对象，然后从旧 Engine 取得一个 owning Variant 状态，并传给新 Engine 的 unserialize body。新 ResourceManager、Engine、Player 都是独立对象；运行中的 timeline/controller/variable state 通过 serialize/unserialize 迁移，而不是靠字段列表拷贝。

### 14.4 与本地实现的逐行对照（修改前）

| 本地位置 | 四参考二进制 | 结论 |
| --- | --- | --- |
| `create()` secondary→primary delete/null | 相同顺序 | 已对齐 |
| `load()` 同顺序 teardown，再 `new EmoteObject(paths)` | 相同 | 已对齐；旧注释“本地 eager”已被代码和四端共同证伪 |
| 析构 secondary→primary，再 RemoveListener | 相同 | 已对齐 |
| clone 用 `new EmoteObject(paths)` 后手工复制约 20 个壳/Engine 字段 | clone helper 统一走 Engine serialize→unserialize | **字段集合不完整且复制层级错误** |
| clone 复制 visible/smoothing/baseScale/userScale | 新壳保留构造默认值 | **边界行为错误** |
| `_useD3D/_opengl/_drawVisible/_drawOpacity` 只被 clone 复制 | 四端壳布局与 registrar 均无对应项 | port 残留，应从 D3D facade 移除 |

本地恢复方案是在 EmoteObject 上增加未知精确源码名的 `clone_guess()`，内部严格执行“paths 构造 + Engine serialize/unserialize”，并让 D3DEmotePlayer::clone 只把结果写入新壳 primary 槽。这样既恢复原版 helper 分层，也避免继续维护一份必然遗漏内部容器/控制器状态的字段复制清单。

### 14.5 实施、验证与 IDB 改进

上述恢复已经实施：

- 新增 `EmoteObject::clone_guess()`，按四端顺序执行 paths 重建、旧 Engine serialize、新 Engine unserialize。
- `D3DEmotePlayer::clone(targetOwner)` 不再复制壳字段或维护 Engine 字段白名单，只在
  目标 owner 上创建默认壳并写 primary clone。
- 删除四端壳布局和 member table 均不存在、仓库也没有真实使用者的 `_useD3D/_opengl/_drawVisible/_drawOpacity` 及其访问器。
- 修正“本地构造期 eager 创建主链”的过时注释；实现和四端都为 load-time lazy 创建。

验证结果：Web Debug 9/9、Wasmtime guest 8/8 均成功；`git diff --check` 通过，警告仍仅为仓库既有工具链警告。

四库均先通过 rename dry-run，再写入并成功保存以下已分析函数：

- `D3DEmotePlayer_ncb_registerMembers_guess`
- `D3DEmotePlayer_clearSlots_guess`
- `D3DEmotePlayer_load_guess`
- `D3DEmotePlayer_clone_guess`
- `EmoteObject_clone_guess`
- `D3DEmotePlayer_ctor_guess`（三个保留独立 ctor 的目标；Android arm64 在 clone 内联）
- `D3DEmotePlayer_dtor_guess`
- `D3DEmotePlayer_deleting_dtor_guess`（三个保留独立 deleting dtor 的目标）
- `D3DLayerListener_dtor_guess`（三个保留独立 base dtor 的目标）

## 15. D3DEmotePlayer.module、DrawDevice Modules 所有权与悬空边界

### 15.1 四端函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| `D3DEmotePlayer::getModule` | `0x52FF78` | `0x494864` | `0x100232B68` | `0x2317C0` |
| D3DEmoteModule member registrar | `0x52E388` | `0x493E54` | `0x100232078` | `0x230DB0` |
| D3DEmoteModule NCB constructor bridge | `0x54177C` | `0x4A30F0` | `0x100244AA8` | `0x244F64` |
| module property `PropGet` | `0x542DB4` | `0x4A42B4` | `0x1002460AC` | `0x246910` |
| getter invoke/result boxing | `0x542FB4` | `0x4A4460` | `0x1002461C4` | `0x246A4C` |
| module adaptor creation | `0x5430A4` | `0x4A44FC` | `0x100246344` | `0x246B54` |
| DrawDevice factory callback | `0x52B654` | `0x492BFC` | `0x100230C88` | `0x22FB28` |
| DrawDevice root constructor | `0x531274` | `0x4955C4` -> `0x495618` | `0x100233C10` -> `0x100233C88` | `0x23287C` -> `0x23295C` |
| DrawDevice complete destructor | `0x531410` -> `0x53244C` | `0x495744` -> `0x49606C` | `0x100233F54` -> `0x100233E1C` | `0x232C74` -> `0x232B14` |
| D3DEmoteModule complete/deleting dtor | `0x533B94` / `0x533B98` | `0x497464` / `0x497466` | `0x1002361CC` / `0x1002361D0` | `0x234F72` / `0x234F74` |

D3DEmoteModule 的独立 ClassInfo static init 分别位于 `0x42CB18`、`0x2FEFD4`、
`0x10024CA40`、`0x24E628`；旧表中的 `0x42CBD8`、`0x2FF094`、`0x10024CB00`、
`0x24E6D8` 实际是随后构造七个 DrawDeviceD3D auto-register object 的另一组 bundle init，
不能再当作 ClassInfo 初始化。真正 InfoT 把该类绑定到 class-id 字段 `0x1AAF6A8`、
`0x110E228`、`0x101AEE4C8`、`0x1838EA4`。因此 getter 使用的 map key 并不是模块路径、
哈希或临时枚举值，而是 NCB 为 `D3DEmoteModule` 分配的运行时 class id。

### 15.2 getter 的共同对象链和伪代码

四端首先沿相同对象链取根：

```text
D3DEmotePlayer shell
  -> raw D3DLayer owner              (+8 on 64-bit / +4 on 32-bit)
  -> DrawDeviceObjectBase Parent     (+16 on 64-bit / +8 on 32-bit)
  -> Modules ordered map
```

归一化后的共同实现为：

```text
getModule(shell):
  root = shell.d3dLayerOwner.Parent        // no null guard
  id = ncbClassInfo<D3DEmoteModule>::id
  it = root.Modules.find(id)
  if it == end || it.value == null:
    module = new D3DEmoteModule
    root.Modules[id] = module
  else:
    module = it.value
  return module                            // C++ pointer result
```

Android arm64 内联展开红黑树 lower-bound 查找；Android armv7 使用 `0x497390` 查找和 `0x4973D4` 写入；iOS arm64 使用 `0x100236178` / `0x1002360E4`；iOS armv7 使用 `0x234F38` / `0x234ED8`。四端都只在“缺项或已有 null 值”时分配，已有非空项直接返回同一地址；父指针为空时都没有保护分支。

### 15.3 D3DEmoteModule 实例布局、默认值和访问器边界

| 字段 | arm64 偏移 | armv7 偏移 | 构造默认值 | setter 边界 |
| --- | ---: | ---: | ---: | --- |
| virtual dtor vptr | `+0` | `+0` | D3DEmoteModule vtable | — |
| `maskMode` | `+8` | `+4` | `1` | 原值写入，无校验 |
| `maskRegionClipping` | `+12` | `+8` | `false` | bool 写入 |
| `mipMapEnabled` | `+13` | `+9` | `true` | bool 写入 |
| `protectTranslucentTextureColor` | `+14` | `+10` | `false` | bool 写入 |
| `alphaOp` | `+16` | `+12` | `0` | 原值写入，无校验 |
| `pixelateDivision` | `+20` | `+16` | `100` | 原值写入，无校验 |
| max texture width | `+24` | `+20` | `0` | 第一个参数原值写入 |
| max texture height | `+28` | `+24` | `0` | 第二个参数原值写入 |

总大小为 arm64 `0x20`、armv7 `0x1C`。所有属性都是每个实例自己的字段，不是进程级 static。`setMaxTextureSize(w,h)` 在四端都仅保存两个整数，没有 warning、clamp、交换、负数检查或即时纹理重建。模块 vtable 只有 complete destructor 和 deleting destructor 两个有效槽；complete destructor 为空，deleting destructor 直接 `operator delete(this)`。

### 15.4 DrawDevice 根的四棵红黑树和 Modules 所有权

构造器按顺序初始化 FrontItems、BackItems、ManagedObjects、Modules 四个有序树容器。Modules 的 ABI 布局为：

| 目标 | Modules 容器/哨兵区域 | root/首个遍历节点 | node key | node value |
| --- | ---: | ---: | ---: | ---: |
| Android arm64 | root object `+224` | `+240` | node `+32` | node `+40` |
| Android armv7 | root object `+120` | `+128` | node `+16` | node `+20` |
| iOS arm64 | root object `+144` | `+144`（end sentinel 在 `+152`） | node `+32` | node `+40` |
| iOS armv7 | root object `+80` | `+80`（end sentinel 在 `+84`） | node `+16` | node `+20` |

Android 使用 libstdc++ 风格 tree header，iOS 使用 libc++ 风格 tree/end-node 布局，这是偏移和遍历代码不同的原因；key/value 语义一致。

根析构在释放 render targets/transition resource 之后、销毁四棵树节点之前，显式中序遍历 Modules。对每个非空 value，它都从 value 的 vptr 调用第二个虚槽（arm64 `vptr+8`、armv7 `vptr+4`），即 deleting destructor。随后才调用 Modules、ManagedObjects、BackItems、FrontItems 的树清理 helper。故 Modules 的值类型语义是“拥有的、带虚析构的 module base 指针”，绝不是当前本地 `map<uint32, void*>` 所表达的非拥有裸值；普通 `std::map` 析构只释放节点，无法产生二进制中的显式虚析构循环。

四端扫描均只发现 D3DEmotePlayer getter 使用这张表；插入 helper 也只有该 getter 一个调用者。宽/窄/UTF-32 字节搜索均未发现可恢复的通用 module base 类名，因此本地恢复时必须把该基类名标成 `_guess`，但“虚析构基类 + map 拥有其值”的类型结构已经由四端共同确定。

### 15.5 NCB boxing 与原版的双重所有权缺陷

getter 的机器级返回值在 X0/R0 中只是一个地址；真正区分 `T&` 和 `T*` 的证据来自其结果转换 specialization。四端 invoke body 都调用对应 adaptor creation helper，并把 sticky 参数传为 `0`：

```text
native = getModule(shell)
dispatch = ncbInstanceAdaptor<D3DEmoteModule>::CreateAdaptor(
    native, sticky=false, err=false)
result = Variant(dispatch, dispatch)
```

adaptor helper 取得新 TJS 对象的 D3DEmoteModule native adaptor，写入 `adaptor.instance = native`；只有 sticky 参数为 1 才把 `adaptor.sticky` 置 1。这里四端均为 0。与二进制模板实例以及仓库同版 ncbind 的规则一致：`box<T*>` 为 `Sticky=false`，`box<T&>` 为 `Sticky=true`。因此原始 getter 的源码返回类型应为 `D3DEmoteModule*`，不是引用，也不是 `tTJSVariant`。

这与 15.4 的根所有权叠加后形成原版真实存在的双重所有权窗口：

1. 第一次读取 `player.module` 时，根 map 分配并保存 module，返回的新 TJS wrapper 也以 non-sticky 方式拥有同一指针。
2. wrapper 先失效/析构时，它 delete module；D3DEmoteModule 析构为空且没有 root back-pointer，不能清空或擦除 map 项。
3. 后续 getter 看到的是“非 null 的悬空 value”，不会重新分配，而会再次装箱该已释放地址；访问即 UAF，再释放即 double free。
4. 即使没有再次读取，DrawDevice 根稍后析构时也会对悬空 value 调 deleting destructor；反过来，若根先析构而 wrapper 仍存活，wrapper 后续失效也会再次 delete。

这个行为很可能是原插件缺陷，但四端在 map ownership、无回删路径、pointer boxing 三项上完全一致。目标是 1:1 复原，因此实现不能用返回引用、sticky wrapper、shared ownership、map erase callback 或根析构跳过 value 等“安全化”手段静默改变它。

### 15.6 与本地实现的逐行对照（修改前）

| 本地位置 | 当前本地行为 | 四参考二进制 | 结论 |
| --- | --- | --- | --- |
| `D3DEmotePlayer::getModule()` | 返回第一条已加载模块路径或 void Variant | 按 class id 从 D3D root Modules 取/建 D3DEmoteModule 指针 | 完全不同的数据源、类型和生命周期 |
| `D3DEmoteModule` 字段 | 全部 `inline static` | 每实例字段，布局为 `0x20/0x1C` | 对象结构错误 |
| module defaults | mask `0`、mipmap false、pixelate `0` | mask `1`、mipmap true、pixelate `100` | 默认值错误 |
| `setMaxTextureSize` | 只记 warning，不保存 | 原样保存两个 int | 方法语义缺失 |
| root `Modules` | `map<uint32, void*>`，析构不处理 values | 有序 map 持有虚析构 module pointers，根析构逐值 delete | 所有权和析构链错误 |
| D3DLayer bridge | 没有访问 parent Modules 的接口 | getter 直接沿 D3DLayer Parent 访问 map | 数据流缺口 |
| 源注释 | 仍引用旧 `libkrkr2.so` 地址并把 module 解释成路径 | 当前四参考共同证伪 | 过时，修改相关代码时移除 |

本地实施方案：增加一个精确名未知、以 `_guess` 标记且只有虚析构的 D3D module base；把根 map 改为该 base 指针并在根析构 body 中逐值 delete；给 D3DLayerObject 增加最小 parent-module 查找/写入桥；让 D3DEmoteModule 继承该基类并恢复实例字段；让 D3DEmotePlayer getter 使用 NCB class id 查找/懒建并返回 `D3DEmoteModule*`。返回 pointer 会让现有 ncbind 自动生成与四端相同的 non-sticky boxing，从而连同上述危险边界一并复原。

### 15.7 实施、验证与 IDB 改进

上述恢复已经实施：

- 增加只有虚析构的 `D3DModuleBase_guess`，把 DrawDevice 根的 Modules 改成 owning base-pointer map，并在根析构 body 中逐值 `delete`。
- 增加 D3DLayer owner 到 parent Modules 的最小查找/写入桥；与四端一样不增加 parent null guard。
- D3DEmoteModule 改为每实例字段，恢复 `1/false/true/false/0/100/0/0` 默认值、无校验 setters 和保存两个整数的 `setMaxTextureSize`；静态断言固定其 64/32 位大小为 `0x20/0x1C`。
- D3DEmotePlayer `module` getter 改为用 `ncbClassInfo<D3DEmoteModule>::GetID()` 查找/懒建 root module，并返回 `D3DEmoteModule*`；删除未注册、只把 Variant 当路径送入 load 的本地 `setModule` 残留。
- 触及区域内引用旧 `libkrkr2.so` 地址、错误默认值和“module 是路径”的注释已移除；地址证据集中保留在本节。

Web Debug 全目标和 Wasmtime guest 均成功构建；未出现本轮新增 warning，输出仍只有仓库既有的 literal-operator、`nodiscard` 和 pthread/memory-growth warning。`git diff --check` 通过。

四库 rename dry-run 均为零冲突，随后实际写入并成功保存。共同写入的主要名字为：

- `D3DEmotePlayer_getModule_guess`
- `D3DEmoteModule_ncb_registerMembers_guess`
- `D3DEmoteModule_ncbConstruct_guess`
- `D3DEmoteModule_complete_dtor_guess`
- `D3DEmoteModule_deleting_dtor_guess`
- `D3DEmotePlayer_modulePropGet_guess`
- `D3DEmotePlayer_moduleGetterInvoke_guess`
- `D3DEmoteModule_createAdaptor_guess`
- `D3DEmoteModule_classId_guess`
- `D3DEmoteModule_vtable_guess`
- `DrawDeviceD3D_factory_guess`
- `DrawDeviceD3D_ctor_guess`（三个保留独立 final ctor 的目标）
- `DrawDeviceObjectBase_ctor_guess`
- `DrawDeviceD3D_complete_dtor_guess`
- `DrawDeviceObjectBase_dtor_guess`
- `DrawDeviceD3D_ncb_registerMembers_guess`
- `DrawDeviceD3D_ncb_register_guess`
- `DrawDeviceD3D_ncb_unregister_guess`

Android arm64 的 `DrawDeviceD3D_ncb_register_guess` / unregister 原先被 IDA 错并进前一个 STL helper；本轮按类描述符中的两个精确函数指针拆分函数边界、重新定义代码并成功保存，因此后续反编译不再把注册逻辑误显示成 vector reallocation。

后续细化记录：`EmoteVarController` 的四端构造、reset、析构、字段初始化边界与
owner 链见 `analysis/motionplayer_var_controller_lifecycle_four_binary_2026-08-11.md`；
该轮进一步确认它是拥有三个数组和一个 deque 的非多态普通类，原端口的
free ctor/dtor helper 与显式尾部 `pad` 字段均不是最接近参考源码的结构。

`EmoteAngleController` 的 inlined constructor、12B naked deque 复用、析构和
phase setup 宽存储见
`analysis/motionplayer_angle_controller_lifecycle_four_binary_2026-08-11.md`。
该轮确认旧名 `EmoteAngleController_ctor_12Bdeque` 实际是共享 deque initializer，
并修正了“setup 保留 phase”的错误解释。

`EmoteMouthController` 的字典构造、setter 源码参数顺序、双 HM7 key 输出、
12B naked deque、entry owner 和异常 unwind 见
`analysis/motionplayer_mouth_controller_lifecycle_four_binary_2026-08-11.md`。
该轮进一步纠正了把 AArch64 的 `W1 + S0..S2` 寄存器类别顺序误当成源码参数
顺序的问题；ARM32 caller 证明真实顺序为 `value/duration/power/append`。

Player 图像空方法与 D3DAdaptor nullsub 的同名边界见
`analysis/motionplayer_unused_player_image_methods_four_binary_2026-08-11.md`：
D3DAdaptor 的六个空导出是四端真实 API，而 Player 上三个无 caller 空 wrapper
不在精确 92-member 表内，已作为旧端口遗留删除。

Player 每帧 mesh 工作计数、四类累加点、递归 child/particle 汇总与 uint32
回绕边界见
`analysis/motionplayer_processed_mesh_vertices_four_binary_2026-08-11.md`。该轮确认
旧“未知 DWORD”和本地部分实现实际属于 `processedMeshVerticesNum`，并移除了
不对应当前 Player API 的 `_alphaOpCounter/alphaOpAdd()` 残留。

Eye/Eyebrow 的 value-track enqueue、Eyebrow 构造/布局/reset/析构所有权闭环见
`analysis/motionplayer_eye_eyebrow_enqueue_lifecycle_four_binary_2026-08-11.md`。
该轮确认正 duration 的 replace 也必须同时清除 resolver 次轨道，并修复了旧本地
仅清主轨道、可能继续消费上一条命令路径片段的偏差。
