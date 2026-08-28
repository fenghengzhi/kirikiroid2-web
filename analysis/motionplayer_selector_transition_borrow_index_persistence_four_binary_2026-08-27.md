# Selector / Transition 借用、索引与持久状态总审计（四参考二进制，2026-08-27）

## 1. 任务结论

本 slice 闭合 `MP-R13`。四个参考二进制共同恢复出的 source graph 是：

```text
Engine
├─ deque #8 owns TransitionEntry
│  └─ unique/raw single owner -> EmoteVarController(count=1)
└─ deque #9 owns SelectorEntry
   ├─ unique/raw single owner -> EmoteSelectorController
   │  └─ vector<Option> contains borrowed EmoteVarController*
   └─ vector<TransitionEntry*> targets (borrowed, normally empty)
```

Transition metadata 先建 owner deque；Selector 的每个 option 从 deque 头开始做 first-equal
label scan，只借用第一条匹配 Transition 的 scalar controller，不转移所有权。Selector 的 float
selection 在三个消费点都执行相同的 ARM float-to-signed-int32 饱和、向零截断语义。持久化只覆盖
controller 的 live interpolation fields，不保存 owner graph、借用关系、命令队列或 entry gate。

本地生产实现与四端共同伪代码一致，本轮没有生产语义修改；新增一个 Transition 持久状态测试，
锁定完整八键外层 schema、七个 controller 字段、命令队列/gate 保留以及缺字段的顺序性 partial
restore。

## 2. fresh 四端证据

本轮 fresh decompile、完整 disassembly 和 xrefs 覆盖下列 60 个 distinct function ranges；60 个
decompile 全部成功，9,060 条指令全部完整且未截断，共 154 个 xrefs。

| entity | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Transition builder | `0x66A8A4` / 269 | `0x557B84` / 173 | `0x1001A9C9C` / 131 | `0x1A9314` / 214 |
| Selector builder | `0x66ACDC` / 593 | `0x557E04` / 331 | `0x1001AA030` / 412 | `0x1A96D8` / 626 |
| Selector ctor | `0x66B778` / 58 | `0x5583B6` / 33 | `0x1001B7DFC` / 33 | `0x1B75EC` / 73 |
| apply selection | `0x665490` / 77 | `0x5549B8` / 85 | `0x1001A5514` / 75 | `0x1A4B04` / 84 |
| enqueue | `0x6655C4` / 108 | `0x554AB8` / 62 | `0x1001A5640` / 37 | `0x1A4C10` / 40 |
| reset | `0x665774` / 55 | `0x554B68` / 36 | `0x1001A56D4` / 44 | `0x1A4C7E` / 43 |
| step | `0x665850` / 64 | `0x554BC4` / 54 | `0x1001A5790` / 60 | `0x1A4CF6` / 70 |
| selector sync | `0x66E0FC` / 148 | `0x559A8C` / 105 | `0x1001AC8A4` / 131 | `0x1AC0D0` / 195 |
| target query | `0x67F7DC` / 73 | `0x562378` / 40 | `0x1001B64D0` / 64 | `0x1B6394` / 64 |
| target activate | `0x672BFC` / 146 | `0x55B908` / 59 | `0x1001AF2F0` / 81 | `0x1AEBE4` / 80 |
| target deactivate | `0x672FD4` / 147 | `0x55BAD4` / 60 | `0x1001AF628` / 82 | `0x1AEE48` / 81 |
| serialize Transition | `0x674A9C` / 140 | `0x55C9A4` / 81 | `0x1001B09A0` / 89 | `0x1B0294` / 140 |
| serialize Selector | `0x674CD0` / 173 | `0x55CAD0` / 115 | `0x1001B0B6C` / 117 | `0x1B04A0` / 192 |
| restore Transition | `0x677400` / 522 | `0x55E13C` / 348 | `0x1001B1F2C` / 151 | `0x1B19F0` / 218 |
| restore Selector | `0x677C48` / 513 | `0x55E578` / 413 | `0x1001B2218` / 145 | `0x1B1CD4` / 207 |

按平台汇总为 Android arm64 3,086、Android armv7 1,995、iOS arm64 1,652、iOS armv7
2,327 条指令。Android old-libstdc++ 与 iOS libc++ 的 deque/vector 展开不同，但下面的数据流、
调用顺序和异常前缀一致。

## 3. owner graph 与 metadata 构建顺序

### 3.1 Transition owner

对每个 enabled transition metadata item：

1. `new EmoteVarController(1)`；
2. 以 raw pointer 构造 deque #8 的 owner entry，entry flag 初值为1；
3. 从 metadata 读取 label 并赋给已发布的 `back()`；
4. `_variableControllerRefs[label] = {type=7, index=metadataIndex}`。

disabled item 被跳过但仍占用 metadataIndex，因此 ref index 可以稀疏。controller 构造完成以后、
deque emplace 成功以前没有临时 RAII owner；grow 抛出时 raw controller 泄漏。emplace 后的 label
读取/赋值抛出则 entry 与 controller 已留在 deque 中，ref 尚未发布。

### 3.2 Selector owner 与 borrowed options

enabled selector 先读取 selector label，再逐项构造 option vector。每个 option：

1. 读取 option label；
2. 从 Transition deque 头开始线性扫描；
3. 第一条相等 label 命中时借用其 controller pointer、把 Transition entry flag 清0，并从原始
   variable binding list 移除该 label；随后立即 break；
4. 再读取 `offValue`、`onValue` 并 append POD option。

没有匹配时 `refCtl=null`，option 仍被保留，apply 时跳过。重复 option label 会反复借用同一条
first-match Transition；重复 Transition label 不会轮转到后续 owner。flag 清零和 variable-label
移除发生在 off/on 读取与 option vector grow 以前，所以后续异常不回滚前面的 side effects。

option vector 完成后，`new EmoteSelectorController(move(options))`；ctor 移入 vector 后立即 apply
index 0。ctor apply 抛出时 selector 的 deque/vector members 正常 unwind，new-expression 释放对象
allocation，但已经修改过的 borrowed Transition 状态不回滚。ctor 成功后到 selector deque emplace
之间仍有 raw-owner leak window；发布 entry 后才赋 label，再发布 `{type=8,index=metadataIndex}` ref。

Engine member 声明让 selector deque 在 transition deque 之前析构，因此 borrowed controllers 在
Selector owner/option vector 销毁期间仍存活。option 与 dormant targets 从未 delete 借用对象。

## 4. selection 的 float→index 边界

enqueue immediate、step pop 和 reset-back 三处共同执行 ARM signed conversion：

```text
NaN                         -> 0
value >= +2147483648.0f     -> INT_MAX
value <= -2147483648.0f     -> INT_MIN
finite in-range             -> truncate toward zero
```

这不是 C++ 对 out-of-range float 直接 `static_cast<int>` 的可移植行为。本地 helper 显式复现了
饱和边界。`+Inf/-Inf` 分别饱和到两端；`7.875/-7.875` 分别得到 `7/-7`。

duration gate 是 ordered `duration > 0`：0、负数和 NaN 都走 immediate path；这与 Var
controller 的 NaN-duration queue 行为不同。immediate 先 clear selector command deque、写
`selState=0`，再 apply converted index。apply 抛出时 clear/state 已提交。

正 duration 时 `append=false` 先 clear queue/state，再 push 12B `{selection,duration,fade}`；
`append=true` 保留 prefix。replace 后 grow 失败留下 empty idle queue；append grow 失败保留旧
queue/state。

## 5. applySelection 数据流

四端共同顺序是：

```text
selectedIndex = index
for option in optionList order:
    if option.refCtl == null: continue
    desired = optionIndex == selectedIndex ? onValue : offValue
    current = VarController.step(dt=0)
    busy = refCtl.state != 0 || !refCtl.queue.empty()
    if busy || abs(current - desired) >= 1e-7:
        span = onValue - offValue
        scaledDuration = abs((current - desired) / span) * duration
        VarController.setTarget(desired, scaledDuration, fade, append=false)
```

selectedIndex 在任何 borrowed call 以前发布；前面 option 的 transition mutation 不因后面 option
抛出而回滚。dt=0 step 本身可能启动一个 queued Var key，所以 current/busy 是 live observation，
不是裸字段读取。setter 永远使用 replace，selector selection 因而覆盖借用 controller 的外部队列。

没有 zero-span guard：非零 delta/zero span 生成 infinite duration；busy 且 delta/zero span 为0/0
可生成 NaN duration。也没有 selectedIndex range clamp；负数/超大 index 只让全部非空 options 取
offValue。NaN option values、span、duration 和 fade 都按原始 float 继续传播。

## 6. step、reset 与 overshoot

Selector 有自己的 duration ramp，但输出始终是 `float(selectedIndex)`；真实 Transition easing 由
借用的 Var controllers 完成。

- state 1：`accum += invDuration * dt`；ordered `accum >= 1` 时 clamp 1 并回 idle。NaN 不完成；
- 其他非零 state：不推进、不自愈；
- idle + command：先 copy+pop front，再 apply；apply 成功以后才写 `invDuration=1/duration`、
  `selState++`、`accum=0`；本 call 不再执行 active update；
- apply 抛出时当前 front 已丢失，later queue 保留，state 仍是入口值，但 selectedIndex 和前缀
  Transition side effects 已提交。

reset 有两个分支：queue 非空时先 state=0、取 back selection 做 immediate apply，成功后才 clear
queue；apply 抛出会留下完整 queue。queue 空且 active 时先 state=0，再 reapply current selection；
idle 时无操作。reset 不清 options，也不修改 entry gate/targets。

## 7. selector 同步与 dormant targets

同步 helper 的 publication 顺序是：

1. create fresh TJS Array；
2. 立即把 fresh Variant 发布为 `_variableLabelsBase`；
3. 把当前 `_variableLabels.Items` 赋给 fresh `Items`；
4. `_dirty=true`；
5. 遍历 selectors，先 `entry.flag=_selectorEnabled`。

enabled 时 clear selector queue/state 并 apply selection0；disabled 时对 fresh Items 调用
`std::remove(label)`，但故意忽略 returned new-end，不 erase、不 shrink，所以 size 不变，只有压缩
prefix/tail 内容发生原生库定义的 assignment 变化。

每个 selector entry 另有 borrowed `targets` vector：enabled 时从 live variable labels 移除 target
label；disabled 时把 target Transition snap 到0。对所有四端 builder、root caller 与 xref closure 的
审计都没有找到 writer，因此正常构造对象的 targets 永远为空。

query 仍会先把每个 visited entry.flag 写成 selectorEnabled，再扫描 targets。activate/deactivate 在
人为畸形地填充 targets 时按 deque/target 顺序取第一条匹配：clear selector queue/state、apply 对应
index、分别写 flag 0/1，然后以 dt=0 step 全部 selectors 和 transitions 并发布 variableValues，
最后立即 return。正常对象中三者均不匹配、没有 controller mutation。

## 8. 持久化 schema

### 8.1 Transition

每条 Transition 保存 Dictionary 的八个 key：

```text
label
phase       = state
tick        = phase
speed       = invDuration
exponent    = powCount
frame[]     = currentValue[0..count)
prev[]      = startValue[0..count)
target[]    = targetValue[0..count)
```

Transition 固定 `count=1`，但调用共享 Var schema。命令 queue、entry flag、owner/ref map 均不保存。
restore 只原地修改已由 metadata 建好的 controller，因此这些 live 字段继续保留。

### 8.2 Selector

每条 Selector 只保存五个 key：

```text
label, value=selectedIndex, phase=selState,
speed=invDuration, tick=accum
```

command deque、option vector/borrowed pointers、entry flag 与 dormant targets 全部缺席。restore 不会
重新 apply selection，因此 restored selectedIndex 可以暂时与 borrowed Transition live values 不一致；
后续 controller progress/reset/显式 selection 才重新驱动它们。

### 8.3 restore 边界与 partial commit

两类 restore 都先要求外层 native TJS Array；非 Array 直接返回。非 Object item 与缺少合法 label
的 item 被跳过。label lookup 返回第一条相等 entry；**unknown label 没有 end check**，四端都会
无条件解引用 end iterator，属于原版 native UB，不能静默跳过。

Transition 属性按 `phase -> tick -> speed -> exponent -> frame -> prev -> target` 顺序探测；
Selector 按 `value -> phase -> speed -> tick`。缺字段保留原 live value。属性 conversion、强制 Object、
数组索引或 callback 抛出时，前面已经写入的 scalar/channel prefix 不回滚；后面字段不执行。
serialization 自身只构造临时 Array/Dictionary owners，不修改 Engine controller state。

## 9. 本地实现与回归保护

共同伪代码与本地逐行比较覆盖：

- `cpp/plugins/motionplayer/EmoteSelectorController.h/.cpp`：object fields、借用 option、转换、
  enqueue/apply/step/reset；
- `cpp/plugins/motionplayer/EmoteEngine.h/.cpp`：Transition/Selector owner entries、builders、sync、
  target APIs、serialize/restore；
- `tests/unit-tests/plugins/motionplayer-dll.cpp`：ctor index0、borrowed setter、replace/append/reset、
  NaN/Inf/向零转换、Selector 五字段、dormant targets 与 getAnimating 既有保护。

本轮新增 `transition persistence restores scalar state but preserves live queue and gate`：验证外层
label 加共享 Var 七字段、三组 channel Array、完整 restore、queue/gate 排除，以及只有 phase 的
partial item 不覆盖其他 live fields。

## 10. IDB 改进与 disposition

四个 IDB 的 60 个审计 roots 均追加 `MP-R13` 语义注释；各数据库在 selector builder root 添加一条
任务 bookmark 并保存。相关函数此前已由直接 slice 获得稳定语义名，本轮没有为了计数重复 rename。

因此 `MP-R13` 可标为 `CLOSED_STATIC`。新增测试已经完成源码级登记；正式 native unit、Web runtime
与 differential 执行仍统一由 `MP-V` 任务跟踪。
