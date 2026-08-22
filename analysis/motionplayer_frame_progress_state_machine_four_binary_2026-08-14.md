# MotionPlayer `Player::frameProgress` 状态机（四参考二进制，2026-08-14）

## 1. 范围与本轮纠正

本轮只重新反编译 `reference/binaries/` 四个当前目标中的 Player frame core，
不再使用旧 `libkrkr2.so` 的地址来推定函数身份。此前的四流、full reseek、node-slot
helper、Join/HM1 专项仍提供被调函数内部证据；本记录专门固定 frame core 自己的
分支拓扑和调用次数。

本轮发现并修正三个源码偏差：

1. 非 parameterized 的 first-frame reseek 正常完成后，原生不是无条件 return；它会
   落入公共 queue-gated cursor/wrap 状态机。本地旧实现直接 return，在调用者显式清
   queue gate 的组合状态下漏掉本次 `speed * dt`。
2. forward/reverse loop-wrap 的 `fullReseek` 已经包含 absolute node-slot 初始化；原生
   不在该 call 后额外调用 node incremental phase。本地旧实现各多调用一次，已删除。
3. idle 分支的非空 gate 不是独立 prepared-frame vector，而是同一个
   `_parameterEntries` 参数表。旧实现用必含合成 root 的 `_nodes` 近似，几乎
   每次 idle progress 都会多扫描一次 node deque。

## 2. 四份 frame core 定位

| 目标 | 函数 | 大小 |
|---|---:|---:|
| Android arm64 | `0x6BE44C` | `0x468` |
| Android armv7 | `0x58A63A` | `0x318` |
| iOS arm64 | `0x100113B50` | `0x314` |
| iOS armv7 | `0x111556` | `0x316` |

四份 recovery IDB 统一命名为 `Player_frameProgress_guess`。Android arm64 因
libstdc++ deque 的节点索引运算与两个 parameterized-node 循环以内联形态保留，函数
明显更大；其他三端把等价遍历更多地归并为 out-of-line helper。这是 STL/编译器形态
差异，不改变状态机。

主要被调函数地址沿用当前四参考专项中的映射：

| 被调阶段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| full reseek | `0x6B5AA8` | `0x583C8C` | `0x10010C3CC` | `0x109DAC` |
| forward four streams | `0x6B3EBC` | `0x582BE0` | `0x10010AA08` | `0x1083D8` |
| reverse four streams | `0x6B6E1C` | `0x584838` | `0x10010D230` | `0x10AB68` |
| parameterized-node refresh | `0x6B7D30` | `0x5851BC` | `0x10010DF70` | `0x10B8A8` |

## 3. 入口副作用顺序

四端共同顺序：

```text
processedMeshVerticesNum = 0
motionCompleted = false
deltaTime = speed * inputDt
if directEdit:
    initEmoteMotion(flags = 2)
refreshModifiedNodeTimelines()
```

因此：

- mesh work counter 和 completed byte 在任何后续 early return 前清零；
- `deltaTime` 是缩放后的 frame-domain delta，没有 clamp、substep 或 raw-dt 旁路字段；
- direct-edit 初始化与 modified-node refresh 位于 completed 检查之前，二者可以在同一
  次调用中重新发布 sync-wait/completed 状态；
- 四个 Player hash map 都不在入口 clear，尤其 HM2 必须跨 frame progress 持久。

## 4. 顶层路由

### 4.1 Player-level parameter 路径

若 selected/default parameter pointer 非空，core 读取 parameter entry 的 value：

```text
if firstFrame:
    rawTick = parameterValue
    firstFrame = false
    evalTime = parameterValue
    fullReseek()
    return

if parameterValue > evalTime:
    rawTick = evalTime = parameterValue
    forwardFourStreams(parameterValue)
    return

if parameterValue < evalTime:
    rawTick = evalTime = parameterValue
    reverseFourStreams(parameterValue)
    return

refreshParameterizedNodeTimelines()
return
```

注意此处的 first-frame 分支确实直接 return；第 5 节纠正的是普通时间轴 first-frame
分支，二者不可合并。

### 4.2 idle 路径

没有 selected parameter，且 `!firstFrame && !allplaying` 时，四端都比较
`_parameterEntries` 的 vector `begin/end`：

- 空：立即 return；
- 非空：只刷新 parameterized nodes，然后 return。

字段位移与既有 parameter pipeline 证据完全重合：

| 目标 | selected pointer | parameter vector begin/end | ramp map |
|---|---:|---:|---:|
| Android arm64 | `+376` | `+384/+392` | `+408` |
| Android armv7 | `+248` | `+252/+256` | `+264` |
| iOS arm64 | `+288` | `+296/+304` | `+320` |
| iOS armv7 | `+200` | `+204/+208` | `+216` |

对应的 idle compare 指令位于 Android arm64 `0x6BE658`、Android armv7
`0x58A792`、iOS arm64 `0x100113CA8`、iOS armv7 `0x1116B0`。四份
`Player_appendParameterEntry_guess` 也分别在相同 vector 三元组上 append/grow：Android
arm64 `0x6AEAF8` 使用 `+384/+392/+400`，Android armv7 `0x57FA14` 使用
`+252/+256/+260`，iOS arm64 `0x100106D00` 使用 `+296/+304/+312`，iOS armv7
`0x104168` 使用 `+204/+208/+212`。因此它不是只有 STL 形状相似的新容器，
而是参数生产管线的同一所有者：

- `Player_parseParameterList_guess` / `Player_appendParameterEntry_guess` 生产记录；
- `Player_finalizeParameterTable_guess` 把 vector 元素的借用指针插入 ramp map；
- `_selectedParameterEntry` 也是指向该 vector 元素的借用指针；
- 析构时先从 ramp map 清理这些指针，再释放 parameter vector，最后拆 node tree。

vector 非空只决定是否执行 parameterized-node 扫描，不保证某个 node 的
`parameterEntry` 非空，所以扫描仍可能是 no-op。反之，旧 `_nodes.empty()` gate
因 constructor-created root 而几乎始终为 false，会在空参数表时多扫描 node deque。
本地现已直接检查 `_parameterEntries.empty()`。idle 路径仍永远不进入下面的
wrap `do/while`，所以全零 `lastTime/loopTime/tick` 不会形成死循环。

## 5. 普通 first-frame 不是无条件 return

进入 timed state machine 后先检查 `syncWaiting || motionCompleted`。若 firstFrame：

1. 清 firstFrame；
2. `delta < 0 && rawTick == 0` 时，以 `lastTime` 同时种入 rawTick/evalTime；
3. reverse-seek flag 为 false：在当前 evalTime 做一次 full reseek；
4. reverse-seek flag 为 true：
   - forward：暂存 evalTime，先在 0 full reseek，再恢复并 forward four streams；
   - reverse：只有 `lastTime > rawTick` 时先在 lastTime full reseek，再恢复并 reverse
     four streams；
5. 每个阶段后按原位点重查 syncWaiting/motionCompleted；
6. 未短路则落入第 6 节的公共状态机。

四端都能直接看到 full-reseek call 返回后流向公共 queue-byte snapshot/`deltaTime`
加载，而不是函数 epilogue。普通 play 的 queue byte 通常仍为 true，所以公共块既不加
rawTick，也不会在 not-at-end 分支推进四流；这解释了旧实现为何在常见 fixture 上看似
等价。但 public queue setter 可以在 firstFrame 尚未消费前清 gate，此时原生会在
reseek 后继续把 `speed*dt` 加到 rawTick。本地新增回归固定这个组合边界。

## 6. 公共 cursor/wrap 状态机

```text
gate = queuing
delta = deltaTime

if !gate:
    rawTick += delta
    evalTime = min_ordered(rawTick, lastTime)

if delta >= 0:
    if rawTick < lastTime:
        if !gate: forwardFourStreams(evalTime)
    else:
        evalTime = lastTime
        if loopTime < 0:
            allplaying = false
            if !gate: forwardFourStreams(lastTime)
        else:
            forwardFourStreams(lastTime)
            if neither cooperative-stop byte is set:
                evalTime = loopTime
                fullReseek()
                wrap rawTick by repeatedly adding loopTime-lastTime
                evalTime = rawTick
                forwardFourStreams(rawTick)
else:
    if rawTick >= 0 && rawTick >= loopTime:
        if !gate: reverseFourStreams(evalTime)
    else if loopTime < 0:
        evalTime = rawTick = 0
        allplaying = false
        if !gate: reverseFourStreams(0)
    else:
        evalTime = loopTime
        reverseFourStreams(loopTime)
        if neither cooperative-stop byte is set:
            evalTime = lastTime
            fullReseek()
            wrap rawTick by repeatedly adding lastTime-loopTime
            evalTime = rawTick
            reverseFourStreams(rawTick)
```

这里的 ordered comparisons 对 NaN 不做修复：`delta >= 0` 为 false，因此 NaN 走
reverse 半边；后续比较也按目标浮点比较的 unordered 结果自然分流。loop interval 为
零时 wrap `do/while` 可能不终止；原生依靠上游 motion 数据与 idle gate，不在 core
增加周期有效性守卫。

## 7. full reseek 的完整性与调用次数

四份 frame core 的 call-site 图共同显示：

- first-frame/plain 与 direction-aware 分支：一次 full reseek，必要时接一次对应方向
  的 four-stream 增量；
- forward loop wrap：`forward(lastTime) -> fullReseek(loopTime) -> wrap ->
  forward(wrappedTick)`；
- reverse loop wrap：`reverse(loopTime) -> fullReseek(lastTime) -> wrap ->
  reverse(wrappedTick)`。

两条 wrap 链在 `fullReseek` 后都没有第五个“node-only incremental”调用。full reseek
本身顺序为 layer/tag、root/priority、variable tracks、absolute node slots、Join HM4/HM3
restore-prune、HM1 cached-result rebuild。额外 node-only call 会重复跨 slot/source side
effect，不能以“通常已经定位所以是 no-op”为理由保留。

## 8. 源码与测试落地

- `PlayerFrameProgress.cpp` 删除 first-frame 尾部的 unconditional return，使其按四端
  共同 CFG 落入公共状态机；
- 删除 forward/reverse wrap 中 full reseek 后的额外
  `seekNodeTimelineSlotsIncrementalPhase_guess`；
- 将残留的旧单库地址式源码名 `preProgressDirtyNodesLike_0x6B6878` 统一改为四端
  恢复 IDB 已采用的语义名 `refreshModifiedNodeTimelines_guess`；
- 修正 full reseek、Player 字段与 core 内部过时的旧单库地址叙述；编译源码继续用
  语义名，当前地址集中保存在本文；
- 将 idle gate 从 `_nodes.empty()` 改为四端共同的 `_parameterEntries.empty()`，
  并删除“prepared-frame vector”的过时注释；
- 单元翻译单元新增“play 后显式清 queue gate，再 first-frame progress(1)”回归，
  要求 raw frame tick 变为 `1.0`，而不是旧实现的 `0.0`。
- 新增 idle gate 正反回归：参数 vector 空时，即使 node 已挂接外部
  parameter entry 也不进位；vector 非空且 node 指向其元素时，同一 idle
  progress 把 active slot 从 frame 0 推到 frame 1。

## 9. 验证与恢复数据库回写

- `Web Debug Build` 完整重建与最终 `index.html`/Wasm 链接通过；
- 使用 Web Debug 实际编译参数对完整
  `tests/unit-tests/plugins/motionplayer-dll.cpp` 做 Emscripten syntax-only，通过；
- 两次校验均只出现仓库既有的 `_tss`、`nodiscard` 和 Emscripten 链接警告，没有
  本轮新增错误；
- 四份恢复 IDB 的 `Player_frameProgress_guess` 均应用
  `void (void *player, double frameDt)` 原型，并在函数入口、普通 first-frame reseek、
  公共状态机入口、正反向 loop full-reseek call site 写入对应 CFG/调用次数说明；
- 四份 IDB 的 idle compare 位点及
  `Player_appendParameterEntry_guess` 入口均回写 parameter vector 身份、本端
  begin/end/cap 位移、selected pointer 与 ramp map 借用关系；
- 四份恢复 IDB 均已保存。

## 10. 2026-08-15 四流成员内部闭环补充

本状态机调用的 forward/reverse four-stream 成员已继续按 phase fresh 审计。两个成员
仍都是 this-only；旧 `(Player *, double)` prototype 和 phase target 快照已经撤销。

四个内联 phase 现均已闭合：

1. layer/tag：aggregate-scope tag source owner、forward `count >= 1`、rewind
   `count != 0`、live evaluation、align→sync→action；
2. root/priority：tag 后才构造但跨剩余 phase 存活的 priority owner、forward 无 count
   gate、rewind 无 count lookup、content/current/next 的原始提交顺序；
3. variable-track：forward count-only owner 与 persistent step/merge split、raw cursor、
   slot0/slot0 merge；rewind 无 count/owner、slot0/slot1 merge；
4. non-root node：live half-open deque、parameterized shared-stepper route、ordinary
   forward count-only owner 与 rewind no-count、selector/parse/action 异常前缀、delayed
   exact flags、physical slot merge 与无范围保护的 source-mask shift。

三条流共同恢复 32 位 cursor/count 算术：forward ordered-LT 在 NaN 下继续，rewind
ordered-GT 在 NaN 下停止；cursor 零反向下溢以 signed `-1` 进入数字 dispatch。tag 与
priority aggregate owner 在 variable/node phase 后才以 priority、tag 逆序析构。

精确地址和回归分别见：

- `analysis/motionplayer_incremental_tag_root_streams_four_binary_2026-08-15.md`；
- `analysis/motionplayer_variable_track_incremental_seek_four_binary_2026-08-15.md`；
- `analysis/motionplayer_node_incremental_seek_four_binary_2026-08-15.md`。

这里闭合的是 normal forward/reverse aggregate 的四流内部状态机；其他 progress 分支、
邻接 helper 与插件其他模块仍需继续按四份当前参考逐段审计。
