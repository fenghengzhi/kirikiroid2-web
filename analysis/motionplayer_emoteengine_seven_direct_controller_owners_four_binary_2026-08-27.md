# `EmoteEngine` 七个 direct-controller owner 四端生命周期审计

## 结论

`MP-L07` 已闭合。四个参考二进制共同恢复出七个只在 `EmoteEngine` 构造期
publication、在正常析构期反向销毁、在整个 live Engine 生命周期内不替换 pointee 的
single/unique owner：

```text
EmoteEngine
    unique owner -> Position       EmoteVarController(count=2)
    unique owner -> Scale          EmoteVarController(count=1)
    unique owner -> Color          EmoteVarController(count=4)
    unique owner -> Angle          EmoteAngleController
    unique owner -> Bust force     EmoteVarController(count=2)
    unique owner -> Hair force     EmoteVarController(count=2)
    unique owner -> Parts force    EmoteVarController(count=2)
```

这里必须区分两种“替换”：

1. **owner/pointee 替换不存在**：七个指针在 Engine constructor 成功构造各 pointee 后各写
   一次；后续 setter、reset、progress、serialize/unserialize 都只原地修改 controller，
   没有第二次 `new`、pointer swap 或 live-pointee delete；
2. **target queue 替换存在**：`append=false` 的 setter 会先清 queue 并置 idle，再插入新
   keyframe；`append=true` 保留旧前缀后追加。插入分配抛出时，replace 路径已经清空旧
   状态，append 路径仍保留旧前缀，但七个 owner 指针均保持不变。

普通析构在 wind 和 late containers 之后按
`parts → hair → bust → angle → color → scale → position → Player` 销毁。controller 没有
vptr、virtual/deleting destructor 或 shared control block；每个 live pointee执行 ordinary
destructor 后 scalar delete。

本地 `std::unique_ptr<...>` owner、raw array 子 owner、queue replace/append 以及正常析构顺序
均匹配，没有 semantic C++ edit。四个 IDB 已补充确定性命名、owner/EH 注释、书签并保存。

## 1. 本轮四端证据

所有表内函数均在本轮 fresh decompile；constructor、destructor、controller constructor、
controller destructor 和两个 setter 的完整 disassembly cursor 均为 `done=true`。

### 1.1 Engine constructor/destructor

| 目标 | `EmoteEngine` ctor / 指令数 | `EmoteEngine` dtor / 指令数 |
|---|---:|---:|
| Android arm64 | `0x67B76C` / 848 | `0x67C898` / 304 |
| Android armv7 | `0x560948` / 304 | `0x5610E8` / 71 |
| iOS arm64 | `0x1001B7FB0` / 187 | `0x1001B8B4C` / 97 |
| iOS armv7 | `0x1B7788` / 318 | `0x1B814E` / 99 |

### 1.2 Controller constructor/destructor/setter

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `EmoteVarController` ctor | `0x664410` / 67 | `0x554180` / 45 | `0x1001A4AD0` / 44 | `0x1A3FEC` / 95 |
| `EmoteVarController` dtor | `0x680E88` / 35 | `0x563536` / 15 | `0x1001C46DC` / 17 | `0x1C1D62` / 15 |
| `EmoteVarController::setTarget` | `0x6646E0` / 100 | `0x5542B0` / 55 | `0x1001A4C44` / 38 | `0x1A418C` / 42 |
| `EmoteAngleController::setTarget` | `0x663870` / 105 | `0x553AD4` / 62 | `0x1001A4308` / 46 | `0x1A3798` / 51 |

Android armv7 还将一次 unique-owner reset lowering 成独立的 11 指令 helper
`0x56351C`：读 slot；非空时调用 `EmoteVarController` ordinary destructor，再 scalar
delete；最后把 slot 写零。它不是 pointee destructor，不能与表中的 `0x563536` 合并。

iOS armv7 的 controller constructor 另有独立 SJLJ landing `0x1A40DE`；它覆盖三个
`new[]` call-site，但只析构 deque 子对象并 resume，不释放已经写入的 raw arrays。

## 2. 七个 owner 的 ABI slot

七个 owner 紧跟 `Player` owner，四端逻辑顺序完全一致：

| owner | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `Player` | `+1064` | `+532` | `+696` | `+348` |
| Position | `+1072` | `+536` | `+704` | `+352` |
| Scale | `+1080` | `+540` | `+712` | `+356` |
| Color | `+1088` | `+544` | `+720` | `+360` |
| Angle | `+1096` | `+548` | `+728` | `+364` |
| Bust force | `+1104` | `+552` | `+736` | `+368` |
| Hair force | `+1112` | `+556` | `+744` | `+372` |
| Parts force | `+1120` | `+560` | `+752` | `+376` |
| raw wind owner（下一个 member） | `+1128` | `+564` | `+760` | `+380` |

这些是目标 ABI 的物理 offset，不要求 portable C++ 复制 padding。关键共同事实是：

- 每个 slot 只有一个 pointer；
- 没有 reference count/control block；
- 没有备用/replacement slot；
- slot 写入发生在相应 pointee constructor 正常返回之后；
- 正常析构后 slot 是否先清零是 optimizer/library lowering 差异，不改变 single-owner 语义。

## 3. 构造顺序与 publication

四端共同 source order：

```text
Player
Position(count=2)
Scale(count=1)
Color(count=4)
Angle
Bust(count=2)
Hair(count=2)
Parts(count=2)
```

每个 controller 都遵循同一 publication 边界：

```text
storage = operator new(controller_size)
controller_ctor(storage, optional_count)
owner_slot = storage                  // ctor 正常返回后才 publication
```

目标 allocation size：

| controller | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| ordinary `EmoteVarController` | `0x80` | `0x48` | `0x60` | `0x38` |
| `EmoteAngleController` | `0x70` | `0x44` | `0x50` | `0x34` |

Angle constructor 在 Engine constructor 中内联：先建立空 deque，再写
`state=0/currentRad=0/targetRad=0`；`startRad/invDuration/powCount/phase` 保持未初始化，
直到后续 setup/restore 写入。

全部七个 owner 建好后，Engine constructor 以同一套 setter 依次 seed：

```text
Position = {0, 0}
Scale    = {1}
Angle    = 0
Color    = {128, 128, 128, 255}
```

每次 setter 前都写 `dirty=true`；三个 outer-force controller 不另行 seed，保持 constructor
产生的全零 current/start/target arrays 与 idle state。

## 4. `EmoteVarController` 内部 raw owner

四端共同 logical layout：

```text
deque<20-byte keyframe> queue
int count
int state
float* currentValue       raw new[] owner
float* startValue         raw new[] owner
float* targetValue        raw new[] owner
float powCount
float phase
float invDuration
```

deque header 的目标大小分别是 Android `0x50/0x28`、iOS `0x30/0x18`；这解释了
controller 总大小与三个数组 pointer offset 的 ABI 差异，不是额外 source member。

constructor 共同顺序是：

1. default-construct empty deque；
2. 写 `count` 与 `state=0`；
3. 依次分配 `current/start/target` 三块 `4*count` bytes；
4. 三次分配全部成功后，依次将三块数组清零。

普通 destructor 共同顺序是：

```text
delete[] currentValue
delete[] startValue
delete[] targetValue
destroy queue
```

它是 ordinary non-polymorphic destructor；Engine owner 随后另行 scalar-delete controller
storage。

### 4.1 构造失败前沿

三个 array pointer 是 raw owner，不是三个 `unique_ptr<float[]>`。因此：

- 第一次 `new[]` 抛出：无 array 已发布，只清理 deque/controller storage；
- 第二次 `new[]` 抛出：已写入的 `currentValue` array 泄漏；
- 第三次 `new[]` 抛出：已写入的 `currentValue` 与 `startValue` arrays 泄漏；
- pending controller 的 outer new-expression 只释放 controller storage，不调用完整
  `EmoteVarController` destructor；
- earlier **完整 controller owner** 仍由 Engine constructor unwind 按反向前缀销毁。

Android arm64 landing 与 iOS armv7 SJLJ landing 均明确只清 deque；Android armv7/iOS
arm64 也没有删除已发布 raw arrays 的 cleanup path。四端共同保留这个历史 leak boundary。
本地 raw pointer members 和普通 constructor body具有同样的失败语义；把数组改成 smart
owner 会修复泄漏，但会偏离参考。

## 5. owner “替换”分母

Engine constructor/destructor、direct setters、resetControllers、progress、serialize 和
unserialize 的完整四端函数共同证明：live Engine 期间没有 controller owner replacement。
所有非构造用途都是：

```text
controller = owner_slot             // borrow
mutate controller->state/queue/data // in-place
```

具体包括：

- `resetControllers` 清 queue/提交最后 target，不分配新 controller；
- progress/step 原地推进 phase/current/target；
- serialize 只读取七个对象；
- unserialize/restore 原地写 state、queue 与 scalar/array 数据；
- clone 先构造新的 Engine，因此得到全新七 owner，再迁移 state；源/副本不共享 pointee。

唯一与“替换”对应的运行时行为位于 controller queue，不位于 owner pointer。

## 6. Var target queue：replace、append 与异常

共同伪代码：

```text
if (ordered duration <= 0) {
    queue.clear()
    state = 0
    copy count values to currentValue
    return
}

if (!append) {
    queue.clear()
    state = 0
}
queue.emplace_back(values, count, duration, powCount) // 20 bytes
```

边界：

- `duration = NaN` 时 ordered `<=` 为 false，进入 queue path；
- `append=false` 在 deque allocation 之前清旧 queue/置 idle，allocation 抛出会留下“已清空
  且无新 keyframe”的有效 controller；
- `append=true` allocation 抛出时旧 queue prefix 与 state 保持；
- owner pointer 始终不变；
- keyframe 先写 word 3=duration、word 4=pow，再复制 `count` 个 channel word；count=4 的
  Color 会用 alpha 覆盖 word 3，后续 step 因而把 alpha 当 duration。这是四端共同边界，
  不是越界写。

## 7. Angle target queue：normalization、replace 与异常

共同伪代码：

```text
while (endRad < 0)      endRad += 6.2832f
while (endRad >= 6.2832f) endRad -= 6.2832f

if (!(duration > 0)) {
    queue.clear()
    state = 0
    currentRad = endRad
    return
}

if (!append) {
    queue.clear()
    state = 0
}
queue.push_back({endRad, duration, powCount}) // 12 bytes
```

边界：

- `duration = NaN` 走 immediate snap，与 Var controller 的 NaN 行为相反；
- `endRad = NaN` 通过两个 normalization loop；随后 snap 或 queue NaN；
- `endRad = +Inf/-Inf` 在相应加减 loop 中保持 infinity，函数不终止；
- positive-duration 的 replace/append allocation failure 与 Var controller相同；
- owner pointer 始终不变。

## 8. 普通析构与重入边界

四端 `EmoteEngine` normal destructor 共同 phase：

1. scalar-delete raw wind emitter；
2. 析构 late variable/timeline/hash/Variant members；
3. direct controllers：Parts → Hair → Bust → Angle → Color → Scale → Position；
4. ordinary `Player` destructor + scalar delete；
5. earlier timeline/cache/controller deques 继续反向析构。

iOS 通常在调用 pointee destructor/delete 之前把 owner slot 清零；Android arm64 把若干
null store 调度到 delete 后，Android armv7 的独立 owner-reset helper 也在 delete 后写零。
共同 source contract 只是 ordinary single-owner reset，不保证 callback/reentrant observer
看到哪一条目标指令级时序。

参考没有 Engine-destruction reentrancy guard。正常业务路径上 controller destructor 只释放
array/deque，不回调 Engine；若 allocator/destructor hook 非正常重入 dying Engine，Android
与 iOS 可观察到不同 slot-null timing。portable 实现保留 `unique_ptr::reset()` source
contract，不硬编码任一目标的指令调度。

## 9. 本地映射

| 参考语义 | 本地位置 | 结果 |
|---|---|---|
| 七个 one-pointer owners 与顺序 | `cpp/plugins/motionplayer/EmoteEngine.h:787` | 匹配 |
| Player + 七 controller member-initializer顺序 | `cpp/plugins/motionplayer/EmoteEngine.cpp:804` | 匹配 |
| position/scale/angle/color seed顺序 | `cpp/plugins/motionplayer/EmoteEngine.cpp:839` | 匹配 |
| 七 controller reverse reset + Player | `cpp/plugins/motionplayer/EmoteEngine.cpp:884` | 匹配 |
| Var raw array layout/owners | `cpp/plugins/motionplayer/EmoteVarController.h:54` | 匹配 |
| Var raw allocation/zero initialization | `cpp/plugins/motionplayer/EmoteVarController.cpp:14` | 成功状态与失败 leak边界匹配 |
| Var normal destructor | `cpp/plugins/motionplayer/EmoteVarController.cpp:117` | 匹配 |
| Var queue snap/replace/append | `cpp/plugins/motionplayer/EmoteVarController.cpp:72` | 匹配 |
| Angle queue snap/replace/append | `cpp/plugins/motionplayer/EmoteAngleController.cpp:82` | 匹配 |
| reset/progress/serialize/unserialize 原地使用 | `cpp/plugins/motionplayer/EmoteEngine.cpp` | 匹配；无 owner replacement |

本任务没有 semantic C++ edit。IDB 中已由完整函数证据支持的 controller ctor/dtor/setter
名称从 `sub_*`/`*_guess` 改为确定性名称；source-level `_guess` 符号的全仓审计仍由独立
final audit 跟踪，不能据此把所有 `_guess` 一并视为已证实。

## 10. Disposition

| 观察项 | disposition |
|---|---|
| 七个 controller owner 永不 live-replace | 共同 source 语义；原地 mutate |
| `append=false/true` | queue replace/append，不是 owner replacement |
| Var 第二/第三 array allocation失败泄漏前缀 | 四端共同 raw-owner历史边界；保留 |
| controller ctor失败时 pending outer storage释放 | new-expression cleanup；早期完整 Engine owner反向 unwind |
| Android/iOS slot清零时点不同 | compiler/library lowering；single-owner source一致 |
| Android armv7 独立 owner-reset helper | 编译器生成 helper；不是第二种对象或 destructor |
| deque header与controller object大小不同 | libstdc++/libc++及 LP64/ILP32 ABI边界 |
| Angle infinity normalization不终止 | 四端共同浮点边界；保留 |
| Var/Angle 对 NaN duration 的分支相反 | 四端共同 ordered/unordered比较语义；保留 |

`MP-L07` task-local 静态缺口为零；controller element 的完整 copy/move/EH、全 Engine state
transfer 以及跨全对象 owner 总数仍分别由 `MP-L08`、`MP-L14/MP-R21`、`MP-L16/MP-V13`
独立跟踪。
