# angle / var / loop controller ctor-step-reset-completion 总审计（四参考二进制，2026-08-27）

## 1. 任务结论

本 slice 逐项闭合 `MP-R11`：

- `EmoteVarController` 的 raw-array owner、20B queue key、target replace/append、step、reset、
  completion和failure window；
- `EmoteAngleController` 的12B queue key、两套turn常量、shortest-path、step/reset以及
  NaN/Inf边界；
- `EmoteLoopController` 的vector key构建、wrap sampler、negative/NaN/non-positive-span行为；
- Engine progress里三族controller的实际调用顺序；
- public `animating` 对这些controller的真实完成判定分母。

本地生产实现已经与四端共同源码一致，没有生产语义修改。本轮补两组遗漏的回归断言：

1. live metadata loop controller不使 `animating` 返回 true；
2. `resetControllers` 提交 Var/Angle queued tail，却完全不重置 loop index/accum/keys。

## 2. fresh 四端证据

本轮对 36 个 distinct function ranges 全部 fresh decompile、完整 disassembly、`xrefs_to`，
合计 6,103 条完整未截断指令与 204 个 xrefs；所有 decompile成功，所有cursor
`done=true`。

| entity | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Var ctor | `0x664410` / 67 | `0x554180` / 45 | `0x1001A4AD0` / 44 | `0x1A3FEC` / 95 |
| Var step | `0x663FD8` / 270 | `0x554014` / 118 | `0x1001A48C0` / 107 | `0x1A3E48` / 114 |
| Angle step | `0x663A14` / 127 | `0x553B98` / 122 | `0x1001A43C0` / 123 | `0x1A3838` / 137 |
| Loop step | progress内联 `0x67A3F8` / 302 | `0x554D48` / 48 | `0x1001A5984` / 39 | `0x1A4F38` / 51 |
| Engine resetControllers | `0x66BF6C` / 180 | `0x558888` / 64 | `0x1001AB03C` / 27 | `0x1AA714` / 25 |
| getAnimating | `0x671378` / 852 | `0x55B18C` / 257 | `0x1001AE5D8` / 457 | `0x1ADE54` / 640 |
| Var setTarget | `0x6646E0` / 100 | `0x5542B0` / 55 | `0x1001A4C44` / 38 | `0x1A418C` / 42 |
| Angle setTarget | `0x663870` / 105 | `0x553AD4` / 62 | `0x1001A4308` / 46 | `0x1A3798` / 51 |
| Loop builder | `0x66B860` / 448 | `0x558440` / 296 | `0x1001AAA8C` / 216 | `0x1AA158` / 333 |

Android arm64把 Loop sampler内联进Engine progress；其余三端保留out-of-line step。
这是一项明确 inline disposition，不是 arm64 缺失 loop behavior。

## 3. `EmoteVarController` 构造与owner

共同 logical layout：

```text
deque<20B keyframe> queue
int32 count
int32 state
float* currentValue  // raw new[] owner
float* startValue    // raw new[] owner
float* targetValue   // raw new[] owner
float powCount
float phase
float invDuration
```

constructor顺序是 empty deque、`count`、`state=0`、三次 `new float[count]()`。pow/phase/
invDuration不初始化；state gate在正常读取前写入它们。三块array不是smart owner，所以失败边界是：

- 第一次 allocation失败：只有deque/controller storage cleanup；
- 第二次失败：`currentValue`泄漏；
- 第三次失败：`currentValue`与`startValue`泄漏；
- outer pending new-expression只free controller storage，不调用未完成对象的ordinary dtor。

ordinary dtor按 `current -> start -> target` delete[]，随后member deque自动析构。四端controller
大小差异来自pointer width与deque header，不是额外成员。

## 4. Var target与20B keyframe

共同伪代码：

```text
if ordered(duration <= 0):
    queue.clear()
    state = 0
    current[0..count) = values
    return

if !append:
    queue.clear()
    state = 0

queue.emplace_back(values, count, duration, powCount)
```

keyframe是五个float words：四个 `channelAndDuration` 加pow。先写word3=duration，再复制
`count` channel words。因此 count=4 Color的alpha覆盖word3，step会把alpha当duration；这是
四端共同source-level record边界。

- `duration=NaN` 使ordered `<=` 为false，进入queue path；
- replace在allocation之前clear，失败留下idle empty queue；
- append失败保持旧queue/state；
- immediate snap不改pow/phase/invDuration；
- no null/count/output guard，内部caller承担precondition。

## 5. Var step状态机

共同执行顺序：

```text
count = ctl.count

if state==0 && queue not empty:
    key = queue.front
    start[i] = current[i]
    target[i] = key.channel[i]
    state = 1
    invDuration = 1 / key.word3
    powCount = raw-float-copy(key.word4)
    pop_front
    phase = 0
    // fall through

if state==1:
    phase += invDuration * dt
    if phase >= 1:
        phase = 1
        current = target
        state = 0
    else:
        w = powf(phase, powCount)
        current = start + w*(target-start)

out = current
```

关键行为：

- idle setup与第一次数值update在同一call；
- 本call完成一个key后不启动下一个queue key；下一call才pop；
- `dt=0` 仍可启动key并输出start值；
- `phase=NaN`时`>=1`为false，pow/interpolation传播NaN且state保持active；
- negative dt可使phase负，pow的domain结果原样传播；
- completion只由`phase>=1`触发，clamp为1后commit target；没有epsilon或duration done flag。

## 6. Var reset

共享reset helper有null no-op，然后：

```text
if queue not empty:
    state = 0
    current = queue.back.channels
    queue.clear()
else if state != 0:
    state = 0
    current = target
```

它选择最后一个queued destination，不是front/当前active；queue branch优先于active state。
reset不改start/target/pow/phase/invDuration。direct Position/Scale/Color、outer-force、Transition和
timeline blend/track owners按各caller进入同一语义。

## 7. Angle ctor与target

Angle constructor只确定：empty deque、`state=0`、`currentRad=0`、`targetRad=0`；start、
invDuration、pow、phase保持未初始化到setup/restore。

target先用截断turn常量迭代normalise：

```text
while end < 0:       end += 6.2832f
while end >= 6.2832: end -= 6.2832f

if !(duration > 0):
    queue.clear(); state=0; currentRad=end; return
if !append:
    queue.clear(); state=0
queue.push({end,duration,pow})
```

- NaN duration走snap，与Var family相反；
- NaN angle通过两个loop并原样snap/queue；
- `+Inf/-Inf`在相应减/加loop永不收敛；
- replace clear-before-allocation，append保留旧prefix；
- setter不重置phase，只有step setup才写phase=0。

## 8. Angle step与两套turn常量

Angle与Var最重要的结构差异：active update分支在前，idle setup是`else if`，不会同call推进。

active path：

```text
p = phase + invDuration*dt
phase = p
if p >= 1:
    phase = 1
    current = wrap_iterative(target, 6.2832f)
    state = 0
else:
    current = wrap_iterative(
        powf(p,pow)*(target-start)+start, 6.2832f)
```

idle setup path：

```text
start = current
dest = queue.front.end
if abs(dest-current) > pi:
    dest +=/-= 6.28318531f  // accurate shortest-path turn
target = dest
state=1; invDuration=1/duration; pow=raw-copy; phase=0
pop_front
// no active update this call
```

结果normalization固定用`6.2832f`，shortest path固定用`6.28318531f`；合并成一个`2*pi`常量会
改变长期边界。NaN phase不complete并传播；infinite intermediate在wrap loop可不终止。

## 9. Angle reset

Engine reset的Angle路径不是通用Var reset：

- queue非空：`currentRad = queue.back.endRad`、state=0、clear queue；**不再normalise**；
- queue为空且state非0：取live `targetRad`，以`6.2832f`迭代normalise后写current；
- idle empty：no-op；
- start/invDuration/pow/phase仍不清。

所以人工/restore形成的queued `endRad=-1`会被raw提交为`-1`，而active `targetRad=13`会归一为
约`0.4336`。新增unit断言锁住这两个branch。

## 10. Loop构造、step与unchecked边界

builder共同顺序：

```text
raw = new default LoopController
raw.currentIndex = 0
raw.accum = 0
raw.keys.resize(rawTransitionCount)
for each raw triple:
    keys[i] = {start,end,span}
deque10.emplace_back(raw)       // owner publication
entry.label = var_loop
resolver[label] = {type=3, metadataIndex}
```

resize/decode/deque-grow之前raw pointer尚未被persistent owner接管；异常泄漏controller及已分配
key vector，和四端一致。disabled metadata导致resolver保存稀疏原始index，后续不bounds-check。

step：

```text
idx = currentIndex
accum += dt
span = keys[idx].span
while span <= accum:
    idx = (idx+1) % keys.size
    accum -= span
    span = keys[idx].span
publish idx/accum only if loop crossed
t = accum/span
out = t*end + (1-t)*start
```

边界：

- negative dt不rewind index，允许`t<0`向segment外外推；
- NaN dt使`span<=accum` false，accum/out传播NaN；
- zero/negative span可能除零或non-terminating advance；
- empty keys、invalid index和null output均无guard；
- modulo count在malformed empty path可触发除零/非法访问；
- sampler没有state/done flag，它是持续循环曲线，不会“完成”。

## 11. Engine调用链与reset遗漏

每个progress slice的metadata controller顺序为 Eye → Eyebrow → Mouth → Selector →
Transition(Var step) → Loop，随后root Position → Color → Scale → Angle，最后wind。outer force只在
original dt非零且非direct-edit的尾阶段step。

`resetControllers` 则处理active timelines、outer forces、spring flags、Eye/Eyebrow/Mouth、
Selector、Transition、Position、Scale、Angle、Color；它**没有任何 deque #10 Loop pass**。
因此skip/mirror reset后loop的index、accum、keys继续原状态。新增test直接覆盖这一遗漏语义。

## 12. completion与public `animating`

controller自身：

- Var/Angle active完成条件是state由phase ordered `>=1`转idle；queue仍可非空，所以family active
  predicate是`state!=0 || !queue.empty()`；
- Loop没有completion state，永远只采样。

public `getAnimating` 的direct-controller前缀仅检查：

```text
active(Position) || active(Scale) ||
Angle.state != 0 || !Angle.queue.empty()
```

它故意排除：

- direct Color；
- Bust/Hair/Parts outer-force Var controllers；
- 全部 metadata Loop controllers。

后续另查 timeline blend/negative loop、Selector、Transition、Eye、Eyebrow、Mouth，并按timeline
driven labels过滤。现有test已有Color排除；本轮新增Loop排除，防止以后把“持续loop采样”误当成
public animating永久true。

## 13. 本地映射与测试

生产：

- `cpp/plugins/motionplayer/EmoteVarController.h:54` / `.cpp:14`：layout/ctor；
- `EmoteVarController.cpp:29`：Var step；
- `EmoteVarController.cpp:72`：Var target；
- `EmoteVarController.cpp:95`：Var reset；
- `cpp/plugins/motionplayer/EmoteAngleController.h:32` / `.cpp:41`：Angle ctor/step；
- `EmoteAngleController.cpp:82`：Angle target；
- `cpp/plugins/motionplayer/EmoteLoopController.cpp:7`：Loop sampler；
- `cpp/plugins/motionplayer/EmoteEngine.cpp:1250`：Engine reset family/order；
- `EmoteEngine.cpp:1339`：animating denominator；
- `EmoteEngine.cpp:1523`：root controller step chain；
- `EmoteEngine.cpp:2038`：Loop builder。

tests：

- `tests/unit-tests/plugins/motionplayer-dll.cpp:27763`：Var/Angle target、step、NaN与Color word3；
- `:27953`：Loop owner、wrap、negative dt、NaN；
- 新增紧随其后的reset queued-tail/loop-omission test；
- `:30424`：animating family，新增live loop exclusion。

本轮只新增test断言，没有生产ABI/语义修改。`git diff --check`通过；正式unit/Web/runtime执行
仍由MP-V任务统一完成。

## 14. IDB固化与disposition

四库完成14项新helper命名：四端Var step、四端Angle step、三端out-of-line Loop step、四端root
step helper（A64 Loop为inline无独立rename）。36个R11 roots写入comments，4个bookmarks；四库保存。

| 要求 | disposition |
|---|---|
| ctor/owner | Var raw arrays、Angle inline ctor、Loop raw builder window闭合 |
| set/queue | Var/Angle gate、replace/append、NaN/Inf闭合 |
| step | Var fallthrough、Angle mutually-exclusive、Loop wrap sampler闭合 |
| reset | Var queued-tail/target、Angle raw-tail/normalized-target、Loop omitted闭合 |
| completion | Var/Angle state+queue；Loop无done；public denominator明确 |
| malformed/FP | negative/NaN/Inf/zero-span/empty/index边界保留 |
| local | 生产已匹配；补两组回归断言；无task-local静态缺口 |

因此 `MP-R11` 可标为 `CLOSED_STATIC`；动态验证继续由 `MP-V` 跟踪。
