# `EmotePlayer` timeline、selector 与 late query API 四二进制联合恢复

日期：2026-08-27

## 1. 本 slice 的闭合范围

本报告闭合 `analysis/motionplayer_emoteplayer_ncb_surface.tsv` 序号 54..73：

- direct Engine bindings：`setMirror/skip/getTimelineBlendRatio/getMainTimelineLabelList/
  getDiffTimelineLabelList/getLoopTimeline/getTimelineTotalFrameCount/getPlayingTimelineInfoList/
  isSelectorTarget/activateSelectorTarget/deactivateSelectorTarget`；
- 六个 native-instance raw callback：`playTimeline/stopTimeline/getTimelinePlaying/
  setTimelineBlendRatio/fadeInTimeline/fadeOutTimeline`；
- facade queries：`getVariableRange/getVariableFrameList/getCommandList`。

四端 80 个 callback body 均 fresh 完整读取；另对 play/stop/is-playing/blend/fade、Player
variable-range 和 Player command-list helper 闭包做 fresh decompile/disassembly。四个参考二进制
共同构成权威。

## 2. callback 地址与完整指令数

| callback | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `setMirror` | `0x66F190`，16 | `0x55A336`，16 | `0x1001AD644`，16 | `0x1ACCEA`，16 |
| `skip/resetControllers` | `0x66BF6C`，180 | `0x558888`，64 | `0x1001AB03C`，27 | `0x1AA714`，25 |
| `playTimeline raw` | `0x670224`，74 | `0x55A9EC`，43 | `0x1001ADD74`，33 | `0x1AD460`，66 |
| `stopTimeline raw` | `0x67F38C`，60 | `0x562130`，48 | `0x1001B6228`，34 | `0x1B6008`，74 |
| `getTimelinePlaying raw` | `0x67F47C`，75 | `0x5621C8`，52 | `0x1001B62D8`，38 | `0x1B6108`，79 |
| `setTimelineBlendRatio raw` | `0x670674`，internal | `0x55AB8C`，93 | `0x1001ADFB0`，79 | `0x1AD72C`，137 |
| `fadeInTimeline raw` | `0x670ACC`，148 | `0x55AD64`，68 | `0x1001AE200`，53 | `0x1AD97C`，96 |
| `fadeOutTimeline raw` | `0x670DD4`，124 | `0x55AEA8`，71 | `0x1001AE364`，55 | `0x1ADB24`，100 |
| `getTimelineBlendRatio` | `0x67F5A8`，53 | `0x562270`，12 | `0x1001B6398`，12 | `0x1B6218`，12 |
| `getVariableRange` | `0x670FCC`，176 | `0x55AF8C`，94 | `0x1001AE454`，68 | `0x1ADC6C`，128 |
| `getVariableFrameList` | `0x67F67C`，88 | `0x5622A0`，61 | `0x1001B63C8`，48 | `0x1B623C`，90 |
| main/diff label lists | `0x672334/0x6724A0`，91/91 | `0x55B5C8/0x55B63C`，38/38 | `0x1001AEF14/0x1001AEFA0`，29/29 | `0x1AE6F4/0x1AE7C8`，61/61 |
| loop/total | `0x67260C/0x6727D0`，110/53 | `0x55B6B0/0x55B750`，48/13 | `0x1001AF02C/0x1001AF0D4`，31/12 | `0x1AE89C/0x1AE9A4`，72/13 |
| playing info | `0x6728A4`，211 | `0x55B788`，107 | `0x1001AF104`，87 | `0x1AE9D0`，146 |
| selector query/activate/deactivate | `0x67F7DC/0x672BFC/0x672FD4`，73/146/147 | `0x562378/0x55B908/0x55BAD4`，40/59/60 | `0x1001B64D0/0x1001AF2F0/0x1001AF628`，64/81/82 | `0x1B6394/0x1AEBE4/0x1AEE48`，64/80/81 |
| `getCommandList` facade | `0x67F900`，1315 | `0x5623DE`，5 | `0x1001B65D4`，2 | `0x1B644C`，5 |

Android arm64 `setTimelineBlendRatio` 是 `0x670350` 的 `0x670674` internal entry。完整
371 条 containing-function disassembly 已读取，入口后的 raw callback 分支也逐指令核对；
没有创建重叠函数。Android arm64 `getCommandList` 的总数包含两条 facade wrapper 与 distant
shared Player chunks。

## 3. 核心 helper 覆盖

| helper family | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| play | `0x670350`，371 | `0x55AA70`，87 | `0x1001ADE0C`，90 | `0x1AD53C`，149 |
| stop | `0x679680`，38 | `0x55F6E4`，28 | `0x1001B341C`，28 | `0x1B2F70`，26 |
| is-playing | callback 内联 | `0x55ACAC`，19 | `0x1001AE100`，30 | `0x1AD8CC`，30 |
| set blend controller | `0x67098C`，80 | `0x55ACDC`，46 | `0x1001AE178`，34 | `0x1AD918`，33 |
| fade-in coordinator | callback 内联 | `0x55AE44`，36 | `0x1001AE2E8`，31 | `0x1ADABC`，38 |
| Player variable range | `0x6D3970`，118 | `0x597C00`，95 | `0x1001241FC`，70 | `0x1234D8`，134 |
| Player command list | wrapper/chunks 共 1315 | `0x595FF0`，838 | `0x100121EB0`，596 | `0x120CF8`，1032 |

以上 helper 与 callback 全部 disassemble 至 `cursor.done=true`，并用单函数 decompiler取得
未截断伪代码。平台差异来自 inlining、STL deque/vector/unordered-map、AArch32 SjLj 和
Variant/string CopyRef 展开；源级顺序一致。

## 4. `setMirror` 与 `skip` 的 controller 生命周期

`setMirror` 没有相等值早退：

```text
mirrorRequested = convertedBool
mirrorChanged = mirrorRequested != metadataMirrorBase
Player.setFlipX(mirrorChanged)
resetControllers()
```

因此重复写同一值仍会 reset controller。Player 接收的是 derived XOR，不是脚本原值。

`skip` 直接绑定 `resetControllers`，共同顺序：

1. while 扫 active timeline labels；通过 map `operator[]` 取得 state，因此孤儿 active label
   会物化默认 state；
2. `loopBegin>=0` 时只在 blend owner 非空时 reset，然后 index++；`loopBegin<0` 时应用
   final timeline window并 erase 当前 label，不递增 index；
3. reset bust/hair/parts 三个 outer-force controller；
4. hair/parts 与两组 bust spring nodes 都把 spring firstFlag、node initFlag 写 1；
5. reset eye、eyebrow；mouth 有队列时 current=back.endRad 后 clear，无队列但 active 时
   current=endVal；
6. reset selector、transition、position、scale；
7. angle 有队列时取 back.endRad并 clear；只有 active 时取 targetRad，以 `6.2832f` 循环归一
   到 `[0,6.2832)`；最后 reset color。

无穷 angle target 会使归一 while 永不收敛；NaN 两个比较都 false并原样写 currentRad。这是
原生边界，不可替换成 `fmod`。任一中途异常/非法 owner 不回滚此前 reset。

## 5. 六个 raw callback 的参数矩阵

| callback | label | optional args | 缺 label |
|---|---|---|---|
| play | 必需 | flags=Integer，缺省 0，低 32 位 | `TJS_E_BADPARAMCOUNT` |
| stop | 可省略 | 无 | 空 label→停止全部，成功 |
| get-playing | 可省略 | 无 | 空 label→任一 active，成功 |
| set blend ratio | 必需 | duration=0，ease=1，autoStop=false | `TJS_E_BADPARAMCOUNT` |
| fade in | 必需 | duration=0，ease=1 | `TJS_E_BADPARAMCOUNT` |
| fade out | 必需 | duration=0，ease=1 | `TJS_E_BADPARAMCOUNT` |

每个 callback 按 argv 顺序完成一次转换后才读下一项；转换异常不会读取后续参数。成功统一
返回 `TJS_S_OK`。`getTimelinePlaying` 对 supplied result slot 无 null guard，先发布 typed
Boolean；raw method outer adaptor才负责调用协议。

ease 在 double 域执行：

```text
ease == 0 : 1
ease > 0  : ease + 1
otherwise : 1 / (1 - ease)
```

随后才压成 float。`-0.0==0` 得 1；NaN 走 otherwise并仍为 NaN。

## 6. timeline play/stop/blend 状态机

`playTimeline(label, flags)`：

- flags bit0 先 `stopTimeline(empty)`；即使 label 随后 miss，清空已经提交；
- map miss 拼接并记录精确文本 `timeline label not found '<label>'.`，然后返回；
- map hit 对整个 active vector执行 `std::count`，只有 count==0 才 append；
- timelineData 为空才初始化 state，随后初始化 controllers并 seek 到 0。

`stopTimeline(empty)` clear whole active vector；非空只 find/erase 第一个 equal label。
`isTimelinePlaying(empty)` 等价于 active 非空；非空使用第一个 find。

`setTimelineBlendController` map miss静默返回；hit 时 lazy init timelineData，然后对 blend
controller setTarget(value, duration, easing, append=Engine.queuing)，最后把 autoStop 存为
double 1/0。

`setTimelineBlendRatio` raw callback 有意与 fade-in 不对称：

```text
if not playing:
    play(label, flags=3)
    setBlend(label, value=0, duration=0, ease=1, autoStop=false)
    return success                 # 不读 duration/ease/autoStop
setBlend(label, value=1, optional duration/ease/autoStop)
```

`fadeInTimeline` 非播放时也 play3、blend0，但随后无条件再 blend1；`fadeOutTimeline` 直接
blend0且 autoStop=true。未知 label时 play会先因 flags3 clear active列表、记录日志，blend
调用再静默 miss。

`getTimelineBlendRatio` 只有 map hit 且 timelineData 非空才把 retained float blendWeight
扩大为 double；否则返回 +0.0。

## 7. variable range 与 frame list owner

`getVariableRange(label)` 先查 Engine range unordered-map：

- hit：每次 fresh Dictionary，按 min 后 max发布两个 Real，返回 owning Variant；
- miss：CopyRef label 后递归调用 Player range folder。Player 以 DBL_MAX/-DBL_MAX 初始化，
  合并本地重复参数与 child Player；只有严格 ordered `min<max` 才返回 fresh min/max
  Dictionary，否则返回 Void。NaN extrema不能通过该 gate。

Engine hit不要求 min<max，原样发布缓存 extrema。SetValue status忽略；异常可留下 partial
Dictionary，但 owner按 unwind释放。

`getVariableFrameList(label)` 则：

1. CopyRef持久 `_variableFrameLists` Variant并强制 ToObject；
2. accessor 保留 dispatch，临时 Variant立即 clear；
3. 以动态 label name、label 自带 hint、flags=0 做 PropGet；status忽略；
4. CopyRef output Variant到 hidden return，析构 output，再释放 accessor dispatch。

因此 miss通常返回 Void，但脚本 Dictionary override 可以返回任意 type；getter不新建 Array，
也不克隆返回 Object。

## 8. label lists、loop/total 与 playing info

main/diff getter每次创建 fresh Array，按对应 vector 原顺序把每个 ttstr直接构造成 String
Variant；重复、空串均保留，无 reserve/filter/script dispatch。

`getLoopTimeline` map hit返回 `loopBegin>=0.0`；miss记录同一精确日志并返回 false。NaN和
负值均 false，`-0.0` true。`getTimelineTotalFrameCount` 仅 map hit且 `loopBegin>=0.0` 时
返回 raw lastTime double；否则 +0.0，不记录日志。

playing-info：

```text
result = fresh Array
for label in activeTimelineLabels:
    state = timelineStates.find(label)
    if missing: continue
    item = fresh Dictionary
    item.label = label
    item.flags = signed Int32(state.flags low32)
    item.blendRatio = double(state.blendWeight float)
    result.push_back(item)
return result
```

顺序由 active vector决定；sparse missing state跳过，不修复 active列表。Dictionary字段写和
Array append发生后不回滚，外部持有的返回 Array独立于后续 Engine状态。

UTF-16LE raw searches已对 `min/max/label/flags/blendRatio/timeline label not found '/key/
coord/mtx/color/clipRect/mesh` 在四端读至 complete cursor。

## 9. selector target 三方法

`isSelectorTarget` 每进入一个 selector entry就先执行
`entry.flag=selectorEnabled`，再扫描其 borrowed target pointer vector；第一条 label相等返回
true。即使 miss，flag写仍可观察。

activate/deactivate 都只处理第一个 match：

```text
entry.selector.commandQueue.clear()
entry.selector.selState = 0
applySelection(entry.selector, matchedTargetIndex, 0, 0)
entry.flag = activate ? 0 : 1
for every selector:   step(dt=0); variableValues[label] = value
for every transition: step(dt=0); variableValues[label] = value
return
```

两者唯一差异是 flag 0/1。`_variableValues[]` 插入/赋值可能 rehash或抛异常；此前 selection、
flag与较早 map写不回滚。原生代码没有 selector-target writer，正常对象中 targets为空，
所以三个 API 通常只是 scan/no-op；仍不能删掉完整命中路径。

## 10. `getCommandList` facade

callback只取得 embedded Player并进入已闭合的 command-list serializer；详细结构见
`analysis/motionplayer_player_get_command_list_four_binary_2026-08-26.md`。本轮 fresh 再读
四端完整 1315/838/596/1032 条 shared body，确认：

- 先 build persistent prepared items和 borrowed main/aux lists，只序列化 sorted main；
- 每次返回 fresh outer Array，每个 item为 fresh Dictionary；
- 固定发布 key/id/src/coordinate/opacity/blendMode/coord/mtx/color/originX/originY/
  triPriority/clipRect/mesh等 native字段与嵌套 Array；
- clipRect条件、mesh点平铺、Integer宽度、owner与异常 partial commit延续既有报告；
- auxiliary prepared items不进入返回列表。

## 11. 本地逐行对照与测试

本地对应 `cpp/plugins/motionplayer/EmotePlayer.cpp:760`、
`cpp/plugins/motionplayer/EmoteEngine.cpp:1145`、`:1227`、`:1310`、`:2926`、`:3065`、
`:3089`、`:3114`，以及 `cpp/plugins/motionplayer/PlayerLayerQuery.cpp:795`。

现有测试覆盖：

- `tests/unit-tests/plugins/motionplayer-dll.cpp:14607` 与 `:14839`：command list owner/order/
  empty facade；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:28348`：selector inert targets 与 flag写；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:29120`：raw callback和 blend first-call不对称；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:29268`：late direct bindings；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:29507`：Engine/Player range与 frame-list owner；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:30198`：reset的 missing active timeline物化；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:31874`、`:31916`、`:32025`：timeline enumeration、
  sparse info、list owner、lookup/queue/fade边界。

逐行对照未发现新的 C++ 运行语义偏差；本 slice 不修改 C++。

## 12. 状态结论与验证边界

`EmotePlayer` 序号 54..73 共 20 行从 `BODY_PENDING_SEPARATE_SLICE` 提升为
`IMPLEMENTED`。全局 NCB pending 从 39 降为 19，`IMPLEMENTED` 从 91 增为 111；注册面仍为
316/316、`UNMAPPED=0`。

四份 IDB 已统一 callback/helper 命名，internal entry单独注释，添加五组关键书签并原位
保存。生成器确定性、strict TSV 与 `git diff --check` 在台账回填后复核。当前环境缺少
CMake、Ninja、Emscripten，独立 syntax check受缺失第三方头文件阻塞，因此不宣称正式
build/unit runtime。剩余 19 行恰好是 `EmotePlayer` 序号 4..22；完整 root-reachable
helper/object/container 分母仍待闭合。
