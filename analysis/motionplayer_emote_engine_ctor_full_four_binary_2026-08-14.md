# MotionPlayer `EmoteEngine` 完整构造顺序、容器分配与异常回滚四参考复原（2026-08-14）

## 结论

本纵切面重新反编译 `reference/binaries/` 中四份当前 1.3.9 参考产物的完整
`EmoteEngine` constructor，并把此前分别闭合的 HM1–HM7、十个 typed deque、四个
`vector<ttstr>`、三个 Variant、Player/七个 direct-controller owner、wind raw owner、
trigger bytes 与 direct-controller setter 串回同一条 declaration-order 构造链。

旧编译源码总览中的以下表述不成立：

- “4 个 inline `vector reserve(10)`”；
- “后面还有更多 vector reserve(10)”；
- 以旧 Android A64 `a1[index]`/`sub_xxx` 当作可移植成员身份；
- 在 ctor body 中等所有 HM4–HM7 都构造完后，才 `reset(new Player/controller)`；
- 用一个较宽的本地 reset 顺手把 idle controller 的 `phase`/`invDuration` 清零。

真实共同源码结构是：

1. 十个 typed deque；
2. mirror pattern vector、HM1、HM2、HM3；
3. 三个 timeline-label vectors；
4. Player 与七个 direct controller 的单指针 owner member initializers；
5. wind/cache/trigger/double/Variant 普通成员；
6. HM4、HM5、HM6、HM7；
7. ctor body 按 position、scale、angle、color 顺序执行四次 duration-zero setter，
   每次调用前写 `dirty=true`。

Android 中出现的 `10` 是 old-libstdc++ 七个 unordered 容器的默认 bucket 请求，
prime policy 把它映射为 11 buckets；四个 vector 都只是空 `{begin=end=cap=null}`。
iOS libc++ 的七个 hash 容器和十个 deque 则保持 zero/lazy，不在 Engine constructor 中
分配 bucket/deque block。这是 STL ABI 差异，不是两个产品算法，也不应在 portable 源码中
手写 `reserve(10)` 或 `rehash(10)`。

## 四端入口与对象尺寸

| 目标 | Engine ctor | Engine | Player allocation | VarController | AngleController |
|---|---:|---:|---:|---:|---:|
| Android arm64-v8a | `0x67B76C` | 1496B | `0x568` / 1384B | `0x80` / 128B | `0x70` / 112B |
| Android armv7 | `0x560948` | 788B | `0x3B0` / 944B | `0x48` / 72B | `0x44` / 68B |
| iOS arm64 | `0x1001B7FB0` | 1064B | `0x4B8` / 1208B | `0x60` / 96B | `0x50` / 80B |
| iOS armv7 | `0x1B7788` | 568B | `0x348` / 840B | `0x38` / 56B | `0x34` / 52B |

地址只用于 recovery IDB 定位，不进入新的编译源码注释或 helper 名。

## 完整成员区域映射

| 成员/区域 | Android A64 | Android A32 | iOS A64 | iOS A32 |
|---|---:|---:|---:|---:|
| deque #1..#10 | `+0`, 10×80B | `+0`, 10×40B | `+0`, 10×48B | `+0`, 10×24B |
| mirror pattern vector | `+800` | `+400` | `+480` | `+240` |
| HM1 positive set | `+824` | `+412` | `+504` | `+252` |
| HM2 negative set | `+880` | `+440` | `+544` | `+272` |
| HM3 timeline map | `+936` | `+468` | `+584` | `+292` |
| three timeline vectors | `+992` | `+496` | `+624` | `+312` |
| Player owner | `+1064` | `+532` | `+696` | `+348` |
| seven controller owners | `+1072..+1120` | `+536..+560` | `+704..+752` | `+352..+376` |
| raw wind owner | `+1128` | `+564` | `+760` | `+380` |
| five wind floats | `+1136..+1152` | `+568..+584` | `+768..+784` | `+384..+400` |
| eight trigger/state bytes | `+1156..+1163` | `+588..+595` | `+788..+795` | `+404..+411` |
| five doubles | `+1168..+1200` | `+600..+632` | `+800..+832` | `+412..+444` |
| three Variants | `+1208/+1228/+1248` | `+640/+652/+664` | `+840/+860/+880` | `+452/+464/+476` |
| HM4 instant set | `+1272` | `+676` | `+904` | `+488` |
| HM5 range map | `+1328` | `+704` | `+944` | `+508` |
| HM6 var-ref map | `+1384` | `+732` | `+984` | `+528` |
| HM7 label-value map | `+1440` | `+760` | `+1024` | `+548` |

这些区域无空洞地解释了 1496/788/1064/568 字节四种完整 Engine 大小。64 位
Variant 区域占三份 20B Variant 加 4B 对齐，32 位恰为三份 12B Variant。

## 十个 deque 的默认构造边界

Android 两端对每个 deque 先清 header，再调用该 element specialization 的
old-libstdc++ `_M_initialize_map(0)`。即使 element count 为零，也会：

1. 分配默认 8-pointer map；
2. 分配一个 element block；
3. 把 start/finish cursor 都定位到这个 block 的起点。

因此 Android Engine 构造在接触 Player 前已有二十次 deque 相关 heap allocation；任何一次
失败都只展开此前完成的 deque prefix。不同 element 类型产生不同 block capacity，已经在
各 deque #1–#10 的 owner/container 专题中逐一记录。

两个 Android IDB 中识别出的九个 specialization（#2/#3 共用一个）为：

| deque 角色 | Android A64 | Android A32 |
|---|---:|---:|
| #1 hair/parts | `0x681E30` | `0x5640D4` |
| #2/#3 bust chain | `0x681FAC` | `0x5641DE` |
| #4 eye | `0x682138` | `0x5642D0` |
| #5 eyebrow | `0x68228C` | `0x5643C2` |
| #6 mouth | `0x6823E0` | `0x5644B4` |
| #7 clamp | `0x682578` | `0x5645C0` |
| #8 transition | `0x6826F4` | `0x5646B2` |
| #9 selector | `0x68288C` | `0x5647BE` |
| #10 loop | `0x682A08` | `0x5648CE` |

iOS 两端没有对应 allocation helper call；ctor 的 prefix zeroing 已同时形成十个合法空
libc++ deque header。portable Web 同样使用 libc++，自然落在 lazy 侧，不需要伪造 Android
empty-deque allocation。

## vectors 与七个 unordered 容器

四个 vector 的共同初态全部是空 begin/end/capacity：

- `_variableMatchList800`；
- `_timelineLabels992`；
- `_timelineDiffLabels1016`；
- `_activeTimelineLabels1040`。

没有一个 vector 调用 `reserve(10)`。旧反编译把邻接 HM header 的 bucket-policy 数字错归给
vector。

Android 的 HM1、HM2、HM3、HM4、HM5、HM6、HM7 各自调用 requested-count 10 的
old-libstdc++ unordered constructor，得到：

```text
bucket_count = 11
bucket storage = 11 * sizeof(pointer)      // 88B / 44B
first node = null
size = 0
max_load_factor = 1.0f
```

Android A32 还保留了容易辨认的独立 helpers：HM1/HM2/HM4 共用
`0x565842`，HM3 为 `0x5649C0`，HM5 为 `0x564A16`，HM6 为 `0x564A6C`，HM7
为 `0x564AC2`。Android A64 将七条路径内联在 Engine ctor 中。

iOS 的每个 40/20B libc++ header 则是 bucket pointer/count/first/size 全零、
`max_load_factor=1.0f`，没有 bucket allocation。第一次成功插入时才建立 bucket array。

## owner member initializer 与构造失败

八个 owner 的声明/构造顺序为：

```text
Player
position VarController(2)
scale VarController(1)
color VarController(4)
angle controller
bust outer-force VarController(2)
hair outer-force VarController(2)
parts outer-force VarController(2)
```

机器码对每项都先执行 `operator new(sizeof(T))`、再调用 `T` constructor，完整成功后才把
pointer 发布到对应 owner slot。抛出边界是：

- allocation 失败：当前 slot 尚未改变；
- pointee constructor 失败：new-expression 只回收当前 pending allocation；
- 随后从当前项之前最后一个已完成 owner 开始逆序销毁；只有所有八项都已经完成时，入口才是
  parts→hair→bust→angle→color→scale→position→Player；
- 再继续展开三个 timeline vectors、HM3、HM2、HM1、pattern vector 与 deque prefix。

更重要的是，HM4–HM7 在这些 owner **之后** 构造。Android 的 trailing hash bucket allocation
若抛出，已经完成的八个 owner 必须参与回滚。本地旧 body 中 `owner.reset(new T)` 发生在
所有自动成员已构造以后，错误地把八次 allocation 移到了 HM4–HM7 之后。本轮把它们恢复为
member initializer new-expressions，使 C++ declaration-order 构造/回滚重新与四端一致。

iOS arm64 的独立 unwind body 位于 `0x1001B829C`；iOS armv7 的 SjLj body 位于
`0x1B7B02`。Android A64 把 landing blocks 合并在 ctor 大函数尾部。Android A32 当前 ctor
则不只是“没有独立 Hex-Rays function start”：其 304 条指令内没有任何本地 member-cleanup
landing，四个 trailing unordered helper 及 seed helper call 后也没有 prefix rollback。该端
仍由正常析构 helper、字段宽度与 store 次序证明 owner 类型/声明顺序，但不能把其他三端的
异常清理阶梯投射到它。ctor-failure 不调用完整 `EmoteEngine::~EmoteEngine()`，也不套用
normal destructor 的 wind-first 阶段。

## HM4–HM7 构造与 seed failure 的精确后缀

四个 trailing hash member 都位于三个 Variant 之后。Android old-libstdc++ 每项先写空 node/
size/load-factor header，再求 11 buckets、分配并清零 bucket array，最后才把 bucket pointer
发布到当前 member；因此 allocation/length failure 时当前 HM 仍是部分构造对象，不能调用它的
完整 destructor。Android A64 的 landing entry 分别是：

- HM4 失败：跳过 HM4，先析构三个 Variant，再进入八 owner 与更早前缀；
- HM5 失败：先析构完整 HM4，再按上项继续；
- HM6 失败：先析构完整 HM5、HM4，再继续；
- HM7 失败：先析构完整 HM6、HM5、HM4，再继续。

Android A64 的四个 seed setter 都被内联成 delete/memcpy/标量写路径，HM7 完成后没有新的
C++ throw call site，也因此 ctor EH tail 中没有“从完整 HM7 开始”的入口。Android A32 的
HM4/HM5/HM6/HM7 helpers 位于 `0x565842/0x564A16/0x564A6C/0x564AC2`，同样各自形成
11-bucket eager table，但如上所述 ctor 本身没有对应 cleanup landing。

iOS libc++ 的 HM4–HM7 构造只有 header zero stores 与 `max_load_factor=1.0f`，没有 allocation
或可抛 constructor call。iOS arm64 四次 seed helper 的共同 landing `0x1001B8348` 和 iOS
armv7 SJLJ stored call_site 16..19 / switch cases 15..18 都证明：任一 seed 失败时，四个 HM 与
三个 Variant 已全部完成，回滚严格从 HM7→HM6→HM5→HM4→frameLists→labels→labelsBase 开始，
然后才进入 parts→…→Player。不能把这个 full-tail 入口概括为 Android eager-HM 构造失败入口。

## raw wind、trigger bytes、doubles 与 Variants

raw wind owner 和五个 float cache 全为零，不分配 emitter。八个连续 byte 的源字段顺序和
最终成功构造值为：

| byte | 初始写入 | ctor body 后 |
|---|---:|---:|
| mirror requested | 0 | 0 |
| mirror metadata base | 0 | 0 |
| mirror changed/XOR | 0 | 0 |
| directEdit/syncWaiting | 0 | 0 |
| selectorEnabled | 1 | 1 |
| queuing/append | 0 | 0 |
| dirty | source-level 0；Android 落物为 0，iOS 两端省略死写 | 1 |
| debugPrint | 0 | 0 |

dirty 不是与 selector 一起初始写成 1；四端都在最后四次 seed setter 的每次调用前另写
`dirty=1`。Android A64/A32 先以包含 selector 的 32-bit store 同时把 dirty byte 写成 0；iOS
A64/A32 则因该 false 在任何可观察读取前必被 body 覆盖而完全省略零写，第一条 concrete store
就是 position seed 前的 1。portable 字段声明仍保留 source-level false，成功构造后的 true 来自
ctor body。

五个相邻 double 都为 `1.0`：metadata scale、组合 scale 的倒数缓存、hairScale、
partsScale、bustScale。三个 Variant 只保证 default-constructor 的 type tag 为 Void；ctor
不会建立 published variable Array/Dictionary，也不能把未被 type tag 使用的 payload bytes
概括成独立零初始化字段。

## 四次 direct-controller seed

共同顺序为：

```cpp
dirty = true;
varSetTarget(position, {0.0f, 0.0f}, 0.0f, 0.0f, queuing);
dirty = true;
varSetTarget(scale, {1.0f}, 0.0f, 0.0f, queuing);
dirty = true;
angleSetTarget(angle, 0.0f, 0.0f, 0.0f, queuing);
dirty = true;
varSetTarget(color, {128.0f, 128.0f, 128.0f, 255.0f}, 0.0f, 0.0f,
             queuing);
```

Android A64 将四个 setter 大段内联；Android A32 对 position/scale/color 调共享 var setter，
angle duration-zero 分支内联；iOS A64/I32 保留四次 typed helper call。`queuing` 此时为 false，
而 duration≤0 分支本身也不读取 append 决策，所以部分优化器把首个或全部 argument 常量化。

duration≤0 var setter 的精确写集只有：清 queue、`state=0`、复制 `currentValue[0..count)`。
angle setter 清 queue、`state=0`、写规范化后的 `currentRad=0`。它们都不写 idle 状态下不会读取
的 `powCount/phase/invDuration` 尾字段。本地旧 lambda 额外清 position/scale 的 phase 与
invDuration，改变了故意未初始化字节；本轮改为直接调用已闭合的共享 setter。

三个 outer-force VarController 不经过 seed setter；其 constructor 已把三个 channel arrays
零填充，因此两个 current channel 初值仍是 0，但其 interpolation tail 同样保持未初始化。

## 源码、测试与 IDB 落地

- `EmoteEngine.cpp`
  - 删除旧 `sub_xxx`/A64 index/“vector reserve(10)”构造总览；
  - Player 与七个 controller 改回 declaration-order member initializers；
  - 最后四个 seed 改为共享 var/angle setter，保留 dirty 写入与精确调用顺序；
  - 不再提前初始化 idle interpolation tails。
- `EmoteEngine.h`
  - `_dirty` 声明初值改回 false，并说明成功 Engine 的 true 来自四次 seed 写入。
- `motionplayer-dll.cpp`
  - 在 direct-controller 用例入口锁定四个 seed output、七个 hash 容器/四个 vectors 的空状态、
    三个 Void Variant、wind/cache/trigger/double defaults 与三个 outer-force current arrays；
  - 不读取四端故意未初始化的 controller tail。
- 四份 recovery IDB
  - 新增并核对 1496/788/1064/568B `EmoteEngineLayout*_guess` types；
  - ctor prototype 改为相应 layout pointer；
  - Android 九个 deque `_M_initialize_map` specialization 改用 role-based `_guess` 名与类型；
  - Android A32 HM3/HM5 unordered ctor helper 改用语义名；
  - ctor/unwind/helper 写入 allocation、声明顺序、defaults、seed 与异常边界注释；
  - 强制刷新相关 Hex-Rays pseudocode，并成功原位保存四份 recovery IDB。

## 验证

源码与 IDB 落地后完成以下验证：

- 用 Emscripten 对完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` 执行 syntax-only，
  返回 0；只出现既有 `_tss` deprecation warning；
- `cmake --build --preset "Web Debug Build"` 完成 8 个步骤并成功产出最终
  `index.html`/Wasm；只有既有 `_tss`、imagepacker `nodiscard`、pthread/memory-growth、
  JSPI 与 JS library warning；
- 定向扫描确认 `PB stubbed`、`vector reserve(10)`、`sub_xxx_init`、旧
  `resetVarController` 和旧 A64 index 式 ctor 注释均已消失；
- source-order 扫描确认八个 owner 都位于 member initializer list，ctor body 中恰有四次
  `_dirty = true`，并按 position、scale、angle、color 调用已闭合的精确 setter；
- 定向 `git diff --check` 返回 0；仅报告工作区既有的 LF/CRLF 提示，没有 whitespace error；
- 四份 recovery IDB 的 ctor、unwind、Android deque/map helper 均已强制刷新 Hex-Rays，
  并成功原位保存。

这些验证锁定的是本纵切面的构造顺序、初值与异常回滚；它们不把尚未逐项恢复的其他
motionplayer 路径概括为已经完整一比一。
