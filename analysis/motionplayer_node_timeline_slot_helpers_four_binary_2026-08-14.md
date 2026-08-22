# MotionPlayer 节点 timeline slot helper 四参考二进制复原（2026-08-14）

## 1. 范围与证据约束

本记录只以 `reference/binaries/` 中四个当前参考产物为联合权威，不以历史
`libkrkr2.so` 的地址、函数边界或源码注释反推语义：

- Android arm64-v8a `libmotionplayer.so`
- Android armeabi-v7a `libmotionplayer.so`
- iOS arm64 slice
- iOS armv7 slice

本轮对四份 recovery IDB 逐一做了 fresh decompile、code-xref 和调用者复核。
未知原始 C++ 名仍以 `_guess` 结尾；绝对地址只记录在本文，不写进新编译源码注释。

> 2026-08-16 补充：本文件已正确定位 modified-emoteEdit prepass 及其对 absolute
> initializer 的调用，但未完整恢复 `emoteEdit` 的严格 Object 转换、独立 retained owner、
> 专用 member hint、setter 临时销毁顺序和 malformed natural-failure 边界。fresh 四端
> 证据与回归闭环见
> `motionplayer_modified_emote_edit_owner_hint_natural_failure_four_binary_2026-08-16.md`；
> 这些细节以新记录为准。

> 2026-08-16 V151 补充：本文件第 4 节恢复的字段职责、selective reset、六 caller 与
> type/action gate 仍成立，但当时没有完整表达 parser 的 retained NCB source tree、
> `time/type/content/mask/act` 连续共享 hint、typed getter 的 receiver/objthis/HRESULT
> 边界、action retain-before-release 和六类异常 mutation prefix。fresh 四端闭环见
> `motionplayer_node_frame_parse_nested_ncb_shared_hint_lifecycle_four_binary_2026-08-16.md`；
> accessor 生命周期与 hint 身份以该新记录为准。

## 2. 四体函数地图

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| `Player_initializeNodeTimelineSlots_guess` | `0x6B388C` | `0x5827D8` | `0x10010A57C` | `0x107EE8` |
| `MotionNode_seekParameterizedFrames_guess` | `0x6B5224` | `0x58387C` | `0x10010BA1C` | `0x1093A0` |
| `Player_refreshParameterizedNodeTimelines_guess` | `0x6B7D30` | `0x5851BC` | `0x10010DF70` | `0x10B8A8` |
| `MotionNodeFrameSlot_parse_guess` | `0x68FA94` | `0x56EDE0` | `0x1000F1464` | `0xED638` |
| `MotionNodeFrameSlot_reset_guess` | `0x68F9EC` | `0x56ED5A` | `0x1000F13A0` | `0xED558` |
| `MotionNodeFrameSlot_mergeContent_guess` | `0x68FE90` | `0x56F06C` | `0x1000F1970` | `0xEDD80` |
| `Player_refreshModifiedNodeTimelines_guess` | `0x6B3C58` | `0x582A7C` | `0x10010A88C` | `0x10820C` |
| `Player_advanceTimelineStreams_guess` | `0x6B3EBC` | `0x582BE0` | `0x10010AA08` | `0x1083D8` |
| `Player_rewindTimelineStreams_guess` | `0x6B6E1C` | `0x584838` | `0x10010D230` | `0x10AB68` |
| `Player_reseekTimelineCursors_guess` | `0x6B5AA8` | `0x583C8C` | `0x10010C3CC` | `0x109DAC` |

Android arm64 recovery IDB 原先把 `0x6B7D30..0x6B7DEC` 错并进前一个
`Player_random_guess`。该区段有独立 prologue/epilogue，另外三体也均为独立函数；
本轮重新建立 `0x6B7B98..0x6B7D2C` 与 `0x6B7D30..0x6B7DEC` 两个函数边界，
随后 fresh decompile 恢复出与另外三体一致的非 root deque 循环。四份 IDB 已统一命名、
应用 prototype/注释并保存。

## 3. 数据布局差异

以下是 helper 直接访问、且四体能交叉确认的节点前部布局。它们反映指针宽度、
Variant 大小和 libstdc++/libc++ deque 实现差异，不表示算法分叉。

| 字段/对象 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| `node.parameterEntry` | `+8` | `+4` | `+8` | `+4` |
| `parameterEntry.value` | `+40` | `+32` | `+40` | `+32` |
| `node.nodeType` | `+28` | `+20` | `+28` | `+20` |
| `node.frameListVariant` | `+64` | `+56` | `+64` | `+56` |
| slot 0 | `+320` | `+296` | `+320` | `+288` |
| slot stride / slot 1 | `536 / +856` | `432 / +728` | `536 / +856` | `420 / +708` |
| `activeSlotIndex` | `+1392` | `+1160` | `+1392` | `+1128` |
| node dirty byte | `+44` | `+36` | `+44` | `+36` |
| `forceVisible` | `+1996` | `+1716` | `+2012` | `+1680` |
| `Player.preview` | `+1092` | `+744` | `+980` | `+680` |
| node value size used by deque walk | `2632` | `2272` | `2648` | `2228` |

64 位两个 slot 的共同 stride 是 536；32 位 Android 为 432，iOS 为 420。四体的
`parse`/`merge` 形参始终是 slot 子对象，不是完整 `MotionNode`。因此本地
`ClipSlot` 中曾经由 merger 从 node 复制的 `hasTransformOrder/transformOrder[4]`
镜像没有原生 ABI 来源，而且无消费者，已删除；实际变换顺序仍直接属于 node。

## 4. slot parser 的职责与边界

四体共同 prototype 的源码形状为：

```text
parse(slot, rawFrameListVariant, signedFrameIndex)
```

调用者把下标原样传入；parser 自己先调用独立的 slot reset，再写 `frameIndex`，然后
直接对 raw frame list 做
`PropGetByNum(index)`。它读取 `time`、`type`、`content.mask`，并在 action mask 存在时
保留 `content.act`。`type == 0` 把 slot 标为 done 并提前返回；type 2/3 决定普通/
crossfade 状态。

四体 reset helper 都只释放并置空 `src` 字符串 owner，以及 ccc/occ/acc/zcc/scc/cp/
mesh-curve 七个 Variant owner；它清零帧号、time、type/mask、merged 与变换标量，并把
mesh-point vector 的 end 重置到 begin（保留 capacity/allocation）。非直觉但四体一致的
边界是：reset **不释放 icon owner，也不释放 action owner**。后续 merger 在适用的
node-type gate 内总会以新 `src`、`icon`（缺失时为空串）覆盖它们；parser 则只在新 action
mask 存在时覆盖 action。没有相应 mask 时旧 owner 虽仍存活，但消费者受 mask/type gate
保护而不会读取它。本地原先显式 `actionValue.Clear()`，会改变引用计数、异常后的部分
状态以及后续覆盖前的对象生命周期，现已删除并加入 retained-action 回归断言。

逐条 store 对齐还排除了两个旧端口成员：native slot 没有 `width/height` 字段，reset、
merger 和 evaluator 都不读写它们；旧本地字段只被诊断日志读取，现已删除。native
`updateLayers` 在 evaluator 成功后只测试 active slot 的 `done` byte：done 分支复制父状态，
非 done 分支直接进入 delta/inheritance。四体都没有第二个 slot `hasSync` 条件，因此旧本地
`hasSync` 字段及其整段父状态复制分支也是从旧 `libkrkr2.so` 注释延续下来的伪控制流，现已
删除。tag-stream 的 `content.sync` 属于 `frameProgress/skipToSync`，并不存入 node clip slot。

parser 同样不保存 raw `frame["type"]` 整数：它只将 type 0 编码为 `done=true`，将 type
2/3 编码为 `crossfading=false/true`。四体 slot 中没有独立 `frameType` 成员，节点中也没有
仅供日志使用的 `currentFrameType` 镜像；这两个旧本地字段现已删除。诊断输出需要展示
0/2/3 时，由这两个真实 byte 反推，不再扩张运行时对象状态。

重要边界：

- parser 没有下标范围检查，也不把负下标钳到 0；
- 调用者和 parser 都假定 frame-list Variant 能进入普通 TJS 对象分派路径；
- 因此空列表、错误 Variant、缺失元素或抛异常的 getter 不是“静默空帧”；
- reset 和 `frameIndex` 写入先于 numeric lookup，异常可以留下已部分修改的 slot；
- parser 不 merge content，也不刷新 source、切换 active index或派发 action。

四体各有六个 code xref：绝对初始化两次、forward inline phase 一次、parameterized
stepper 两次、reverse inline phase 一次。这一调用计数同时确认 parser 是共享的独立
helper，而两个方向的普通 node phase 仍内联在四流大函数中。

## 5. content merger 的职责与边界

四体共同 prototype 的源码形状为：

```text
merge(slot, narrowNodeType, rawFrameListVariant)
```

它首先把 slot 的 merged byte 设为真；若 parser 已将该 slot 标为 done，则立刻返回。
否则以 `slot.frameIndex` 再次读取 raw frame、取得 `content`，按 `contentMask` 写入默认值
及各条件块：src/icon、origin、coord、blend/color/opacity、flip/angle/scale/slant、
crossfade curves、mesh、child motion、model、particle、camera、anchor 和 feedback。

`icon` 的“缺失时空串”不是单次普通读取。四体共同语义是先以
`TJS_MEMBERMUSTEXIST` 调用一次 `PropGet`，销毁探测用临时 Variant；若成功，再以 flags=0
第二次 `PropGet` 并转换为字符串，若失败则复制预先构造的空串。A64 将这段 helper 内联，
A32/iOS 两体保留同构的独立 helper。因此成功 getter 会被调用两次，且可重入 getter 的
第二次结果才进入 slot；失败 getter 即使写了 result，该临时值也会被丢弃。

### 5.1 reset 的 ABI 对齐表

以下偏移都以 merger/parser 收到的真实 `slot*` 为基址；反编译中常见的
`node + stride*active + 常数` 还包含 64 位节点内 slot 起始偏移 `+320`，不能直接把该常数
称为 slot-relative：

| 语义 | Android A64 / iOS A64 | Android A32 | iOS A32 |
|---|---:|---:|---:|
| frame index | `+0` | `+0` | `+0` |
| start time | `+8` | `+8` | `+4` |
| `ti` / content mask | `+16 / +20` | `+16 / +20` | `+12 / +16` |
| done / crossfade / merged | `+24/+25/+26` | `+24/+25/+26` | `+20/+21/+22` |
| icon / src owners | `+28/+36` | `+28/+32` | `+24/+28` |
| blend / origin | `+44 / +56,+64` | `+36 / +48,+56` | `+32 / +40,+48` |
| colors / opacity / coord | `+72 / +88 / +96..112` | `+64 / +80 / +88..104` | `+56 / +72 / +80..96` |
| flip / angle / scale / slant | `+120 / +128 / +136,+144 / +152,+160` | `+112 / +120 / +128,+136 / +144,+152` | `+104 / +112 / +120,+128 / +136,+144` |
| six curve Variants | `+168..+287` | `+160..+231` | `+152..+223` |
| action / mesh-curve Variant | `+288 / +296` | `+232 / +236` | `+224 / +228` |
| mesh vector begin/end/cap | `+320/+328/+336` | `+248/+252/+256` | `+240/+244/+248` |
| motion flags / dt | `+344/+348` | `+260/+264` | `+252/+256` |

reset 将 vector 的 end 复位到 begin、保留 allocation/capacity，随后把 motion flags 与 dt
两个 word 清零。它不清空其后的 `docmpl/dofst/dtgt/time`；这些字段只有在新 frame 带
motion mask 时才由 merger 的 block defaults/条件读取覆盖。本地原先完全漏掉 flags/dt
清零，现已按四体共同边界补齐并加入旧值回归。

### 5.2 type-6 emitter 与 type-4 particle 的字段分界

旧 `libkrkr2.so` 注释曾把 type-6 emitter 的 active-slot 消费点解释成
`prt.trigger`/`motion.dtgt`，同时在节点上制造第二个 `prtTrigger` 镜像。四体 fresh decompile
显示真实数据流不是这样：

- emitter 的持久 source identity 从活动 slot 的 `src` owner 复制；src 为空、slot done 或
  accumulated inactive 时，清 emitter active/source/timer；
- emitter 模式读取 `model.dt`；模式 4 的 raw-label 查找读取 `model.dtgt`；初始化 timer 使用
  `parentTime - slot.time + model.timeOffset`；
- A64/iOS A64 的三个 model 字段是真 slot `+388/+392/+408`，反编译中的 node-expression
  常数分别是 `+708/+712/+728`；A32 为真 slot `+300/+304/+312`，iOS A32 为
  `+288/+292/+300`；
- type-4 particle system 则直接读取活动 slot 的 `prt.trigger`，并从 evaluator 的九 double
  node mirror 读取 prt 数值；没有独立 node-level trigger producer 或 copy。

此外 merger 的原生键名是 `motion.timeOffset` 与 `model.timeOffset`。四端若只在 IDA
中把首字符定义成字符串项，伪代码会误显示成 `"t"`；原始 UTF-16LE 字节和调用点实际
装载地址都指向完整长键。本地让 emitter 使用 model 三字段、删除 node-level
`prtTrigger`，并加入一个刻意给 src/motion/prt/model 四组冲突值的回归，验证 source 只负责
identity，而 model 只负责 mode/target/timer offset。

同一问题也影响相邻尾块的显示：四端原始字节确认完整键为 `prt.trigger/fmin/fmax/vmin/
vmax/amin/amax/zmin/zmax/range`、`camera.fov/target`、`anchor.target` 与
`feedback.timespan`。其中 `fmax/vmax/amax/zmax/timespan` 曾被伪代码截成首字符；四份
recovery IDB 已重建这些 UTF-16 数组边界并重新保存。merger 的 block 行为保持为：只有
外层 content-mask 命中才打开子对象，`prt` 先无条件写整块默认值再按 `prt.mask` 覆盖，
model/camera/anchor/feedback 则读取各自固定字段并替换字符串 owner。

四端字段地址还恢复了 slot 尾部的共同声明次序：

```text
mesh vector
motion { flags, dt, docmpl, dofst, dtgt, timeOffset }
model  { loop, dt, dtgt, timeOffset }
prt    { trigger, fmin, fmax, vmin, vmax, amin, amax, zmin, zmax, range }
camera { fov, target }
anchor { target }
feedback { timespan }
```

本地 `ClipSlot` 已按这个次序重排，因此字符串/Variant/vector owner 的赋值和逆序析构不再
沿用旧端口的按消费者分组顺序。没有任何本地调用对 `ClipSlot` 做裸 `memcpy`、按本地
`sizeof` 序列化或字节偏移寻址；重排只改变 C++ 自然布局和 owner 生命周期，不改变已有
成员访问 API。四端构造、merger、消费者和析构现已闭环复核：

- 三个 label owner 只各占一个原生 `ttstr` 指针槽；构造把指针清零，merger 只替换该
  指针，消费者只把其起始地址传给 raw-label lookup，析构也只释放该指针；
- 紧随 `model.dtgt`、`camera.target`、`anchor.target` 的 32 位位置均无构造写、merger 写、
  消费读取或析构动作；其位置由后续 `double`/分组布局造成，不能解释成 target-index、
  hash、hint 或 cache；
- 因此源码不伪造任何伴随业务成员。精确二进制偏移继续由本文记录，便携 C++ 结构只保留
  可观测字段与自然对齐。

### 5.4 slot 构造与析构生命周期

本轮继续识别并保存了单 slot 析构 helper：

| 函数 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `MotionNodeFrameSlot_destroy_guess` | `0x6DA44C` | `0x59BC0A` | `0x10012A0A4` | `0x128E4A` |

`MotionNode` 普通构造先为每个 slot 建立空的字符串/Variant/vector owner，再调用 reset；
`MotionNode` 析构则先处理节点后部 owner，随后按 slot1、slot0 的顺序调用该 helper。每个
slot 内部的释放顺序在四端一致：

```text
anchor.target
camera.target
model.dtgt
motion.dtgt
mesh vector backing allocation
meshCurve Variant
action string
cp / scc / zcc / acc / occ / ccc Variants
src string
icon string
```

这是声明顺序的严格逆序；`feedback.timespan`、camera FOV、prt 数值块、model/motion 数值块
都没有析构动作。Android arm64 的 `MotionNode` 析构以两个显式调用展开，另外三端把两个
slot 编译成 stride 递减循环，但 owner 序列相同。四份 recovery IDB 已统一补上函数名、
`void(void*)` prototype 和职责注释。

旧端口还曾把 Android arm64 `0x6F468C` 注释成 `MotionNode_copy`，据此实现一个“复制除
prepared render item 外所有成员”的本地 `operator=`。当前参考重新 lookup 后，该地址
实际落在 `0x6F465C` 的 NCB 调用包装器中；fresh decompile 只检查参数、构造一个 Variant
参数包装并调用函数指针，完全不接触 MotionNode。这个旧地址和旧的“保留目标 item owner”
语义都不成立。

继续下钻四端 deque range-erase 的通用搬移分支后，找到了真正的编译器生成成员复制赋值：

| 函数 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `MotionNode_copyAssign_guess` | `0x6F1A6C` | `0x5AECA0` | `0x10014451C` | `0x144FCA` |

四个 helper 都按声明顺序赋值字符串、Variant、两个 slot、三个 mesh vector 和平凡字段；特别
重要的是，它们也会把 `preparedRenderItem` 裸指针一起浅拷贝。对应覆盖窗口分别为 Android
arm64 `+1496..+1911`（owner 在 `+1904`）、Android armv7 `+1256..+1667`（owner 在
`+1664`）、iOS arm64 `+1512..+1927`（owner 在 `+1920`）、iOS armv7
`+1228..+1631`（owner 在 `+1628`）。因此它不是安全的深复制，也不会保留目标旧 owner。

原生实际旧树清理传入 `[begin + 1, end)`。四端 range-erase 都因尾侧剩余元素数为零而直接
析构非根后缀并收缩容器，不在这条运行路径执行赋值 helper；但通用 STL 实例化仍必须生成两侧
搬移分支，所以元素类型在编译期必须可赋值。本地据此保留同一个 `deque::erase` 调用，并把
`MotionNode` 恢复为默认成员复制构造/赋值。这样既匹配正常尾删，也保留中间 erase 或直接赋值
时 owner 浅复制、潜在重复释放的原生危险边界，而不再人为提供“跳过 owner”的安全版本。

#### slot 复制赋值与 STL 异常分叉

四个 `MotionNodeFrameSlot_copyAssign_guess` 都没有 slot 级事务包装；共同源码级顺序是：

```text
header -> icon -> src -> transform/presentation block
-> ccc/occ/acc/zcc/scc/cp Variants -> action -> meshCurve
-> meshControlPoints vector
-> motion block -> model block -> particle block
-> camera.target -> anchor.target -> feedback.timespan
```

字符串赋值采用“先 retain 源 owner、再 release 目标旧 owner、最后替换”的顺序；Variant
赋值复用 `tTJSVariant_copyAssign_guess` 的同类共享-owner 安全顺序。mesh vector 是 slot 中唯一
可能分配的中段 owner；slot wrapper/Android vector helper会跳过 vector 自赋值。因此普通
`slot = slot` 在四端保持安全，但 slot 整体没有在入口提前返回：前面的平凡 memcpy、字符串和
Variant 自赋值仍会执行，只有 vector 层跳过实际复制。

当 mesh vector 扩容失败时，两套 STL ABI 具有真实可观察分叉：

| 目标 | vector helper | 容量不足时的顺序 | 失败后的目标 vector |
|---|---:|---|---|
| Android arm64 | `0x696AC8` | 分配新缓冲 -> 复制 -> 删除旧缓冲 -> 提交三指针 | 保持旧值 |
| Android armv7 | `0x572DAE` | 同上 | 保持旧值 |
| iOS arm64 | `0x100130EF8` | 清空/释放旧缓冲 -> 长度检查/分配 -> 复制 | 已为空 |
| iOS armv7 | `0x12FDE8` | 同上 | 已为空 |

足够容量时四端都复用原缓冲并按 8 字节平凡 `MeshPoint` 序列覆盖，随后更新 end；源为空时
只把 end 收回 begin，保留 capacity。若 vector assignment 抛出，前面的 header/string/Variant/
action/meshCurve 已经来自 source，而 motion 及其后的尾块仍保持 destination 旧值；Android 的
mesh vector 也保持旧值，iOS 的 mesh vector 已空。这是编译器默认逐成员赋值与目标 STL 的
自然异常中间态，本地使用默认 `ClipSlot::operator=` 正好保留相同源码结构，不应改成 copy-swap。

所有四体都只把 node type 的窄整数值传给 merger。它不接收、保存或反向恢复
`MotionNode*`；source lookup、active index、dirty、action 和 Player 生命周期都不属于
该函数。Android arm64 显示八个 xref，其他优化形态显示五个；差异来自两个 slot 的
merge 是否被编译为循环/共享尾块，不是职责差异。

## 6. 绝对初始化 helper

共同 prototype 是 `void(Player*, MotionNode*)`，参数顺序是 Player-first。共同数据流：

```text
selectionTime = node.parameterEntry
              ? node.parameterEntry->value
              : player.clampedEvaluationTime

count = frameList.count
selected = 0
if count >= 1:
    从 index 0 线性扫描 time
    相等        -> selected = index，停止
    target 小于 -> selected = index - 1，停止
    走到末尾    -> 保留最后扫描结果
selected = min(selected, count - 2)

parse(slot0, selected)
merge(slot0, nodeType)
parse(slot1, selected + 1)
merge(slot1, nodeType)
activeSlotIndex = 0
dirty = 1

if forceVisible || ((preview ? 6153 : 6145) & (1 << nodeType)):
    findSource(node.activeSlot)

if selectionTime == slot0.time && (slot0.mask & ACTION):
    enqueue onAction(node.label, slot0.action)
```

对象所有权/异常顺序是可观察的：frame-list 临时 dispatch owner 跨完整扫描、两次
parse/merge、source 和 action 尾部存活，并在函数尾释放。source lookup 先于 exact-frame
action；任一步抛异常都不会事务回滚此前 slot/active/dirty 写入。

2026-08-15 fresh 四端复核补足了此前没有写清的 owner/field split 与 malformed-count
边界：selection target 在任何动态访问前一次性快照；count/扫描使用 retained local owner，
但两组 parse/merge 仍读取 node 持久 `frameListVariant`。`count-2` 是 32 位回绕减法，
`INT_MIN` 选择 `0,1`，不能用触发 C++ signed-overflow UB 的直接减法。两组 parse/merge
全部成功后才提交 active=0/dirty；旧本地实现提前提交 active，在 parser 异常时不一致。
精确四端地址、owner 尾释放、source shift 边界与差分回归见
`motionplayer_node_absolute_reseed_four_binary_2026-08-15.md`。

空列表没有安全早退。`count == 0` 时初值 0 会与 `count - 2` 比较并得到负选中下标，
随后仍调用 parser 两次。旧本地 helper 的 `rawDispatchObject` 预检和 `false` 返回把这类
输入静默吞掉，现已移除。本地绝对 helper 也从“caller 做 source refresh”的拆分实现
恢复为完整 `Player &, MotionNode &` 边界，full reseek 和 modified-emoteEdit 两个调用者
都只调用它一次。

四体 code xref 完全一致：

- `Player_refreshModifiedNodeTimelines_guess` 调一次；
- `Player_reseekTimelineCursors_guess` 的非 root node 循环调一次。

## 7. 参数化双向 stepper

共同 prototype 是 `void(MotionNode*, Player*)`，与绝对 helper 相反，参数顺序为
node-first。它不接收 current time，而是无条件读取 `node.parameterEntry->value`。

共同算法：

1. 从 `activeSlotIndex` 取得 active/other 两个 ping-pong slot；
2. forward 条件为 active index `< count - 2` 且参数值 `>= other.time`；
3. 每次 forward 先翻转 active index，再把旧 active parser 为 `other.index + 1`；
4. 若新 active.time 仍大于参数值，进入 corrective-backward 循环；
5. backward 每次翻转 active index，把目标 slot parser 为 `oldActive.index - 1`；
6. 若一次跨越都没有发生，直接返回；
7. 发生跨越才标 dirty、仅 merge 两个 invalid slot，再执行同一 source gate/findSource；
8. 全路径都不派发 node action。

这解释了等时参数值变化为何仍能移动节点，也解释了 helper 必须持有 Player 参数：它的
尾部 source refresh 是自身职责，而不是 forward/reverse caller 的职责。本地旧实现只在
普通 forward/reverse caller 外层刷新 source，导致等时参数路径跨帧后保留旧 source；
现已把 Player 引用和 gated `findSource` 尾部移入 helper，并新增回归覆盖“无跨越不刷新、
跨越后刷新、parameterized 跨 action frame 不派发 action”。

## 8. 等时/未播放参数节点循环

`Player_refreshParameterizedNodeTimelines_guess(Player*)` 遍历半开区间
`[1, liveNodeCount)`，只在 `node.parameterEntry != nullptr` 时调用上述共享 stepper。
循环在 re-entrant helper 返回后重新读取 deque end/count；它不快照固定长度，也不处理
root node。

它有两类入口：

- Player-level parameter 时间与当前 evaluation time 相等；
- 普通 Player 的 `firstFrame == false && loopArmed == false` early-return 路径，在原生
  render-list 非空时刷新参数节点。

Android armv7/iOS 两体把该循环保留为独立 helper，frame progress tail-call 它；Android
arm64 优化器在 frame progress 内复制了两份同形循环，同时在邻接区域还保留一个独立
副本。无论编译形态，循环都只处理 parameter-bound node。旧本地 early-return 分支误用
完整 incremental phase，会推进普通时间节点并可能派发 action；现已统一调用独立的
parameter-only helper，并加入普通节点 slot/index/dirty 全保持不变的回归用例。

## 9. 调用链归属

```text
Player frame progress
├─ modified-emoteEdit prepass
│  └─ Player_initializeNodeTimelineSlots_guess
├─ first frame / wrap / full reseek
│  └─ Player_reseekTimelineCursors_guess
│     └─ Player_initializeNodeTimelineSlots_guess for nodes [1, count)
├─ forward time
│  └─ Player_advanceTimelineStreams_guess
│     ├─ ordinary node: inline forward parse/merge/action/source phase
│     └─ parameterized node: MotionNode_seekParameterizedFrames_guess
├─ reverse time
│  └─ Player_rewindTimelineStreams_guess
│     ├─ ordinary node: inline reverse parse/merge/action/source phase
│     └─ parameterized node: MotionNode_seekParameterizedFrames_guess
└─ equal time / non-playing refresh
   └─ Player_refreshParameterizedNodeTimelines_guess
      └─ parameter-bound nodes only
         └─ MotionNode_seekParameterizedFrames_guess
```

普通 forward/reverse node phase 不是独立二进制 helper；它们是两个完整四流函数内部的
方向性区段。本地为了维护性抽成 source-level phase，但不能把这两个 C++ helper 当作
原生函数边界。共享 parser/merger、绝对 initializer、parameterized stepper 和三体保留的
parameter refresh loop 才是本轮确认的独立边界。

## 10. 源码与验证闭环

本轮源码改动：

- `mergeNodeFrameContent_guess` 收窄为 `(ClipSlot&, int nodeType, frameList)`；
- 删除 ClipSlot 无原生依据、无消费者的 transform-order 镜像；
- 删除旧 `width/height`、`hasSync`、raw `frameType` 与 node-level `prtTrigger` 残留；
- 修复 type-6 emitter 的 src identity 与 `model.{dt,dtgt,timeOffset}` 数据流，以及 merger
  被 IDA 单字符字符串项误导的 `motion.timeOffset`/`model.timeOffset` 长键；
- 绝对 initializer 恢复 Player-first 完整 helper 所有权；
- incremental primitive 删除会吞掉 malformed/empty 原生边界的 object 预检；
- parameterized stepper 恢复 helper-owned source-refresh tail；
- 新增 parameter-only Player loop，修复 equal-time 与 non-playing early-return 两个 caller；
- 纠正过时的 `MotionNode_copy` 地址与“跳过 prepared item owner”语义；恢复默认成员复制
  构造/赋值以及 native 的 deque range-erase 调用结构；
- 回归测试覆盖 no-crossing、crossing/source、绝对 exact action、参数化无 action，以及
  ordinary node 在 parameter-only refresh 中保持不变；另固定 slot 尾部完整长键/default、
  `MotionNode` 可复制赋值、prepared-item 裸 owner 浅复制及非根尾后缀 erase。

验证结果：

- 四份 recovery IDB fresh decompile、xref、prototype、命名与注释复核后保存成功；除 slot
  析构外，本轮还统一命名并记录了 `MotionNodeFrameSlot_copyAssign_guess`、
  `MotionNode_copyAssign_guess` 和 `MotionNodeDeque_eraseRange_guess` 三层容器生命周期 helper；
- Android arm64 错误函数边界已拆分并 fresh decompile 成功；
- Web Debug 全量受影响对象重编及 Wasm/HTML 最终链接成功；后续增量检查为
  `ninja: no work to do`；
- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` 复用 Web Debug 真实 Emscripten
  defines/includes/ABI 参数执行 `-fsyntax-only` 成功；唯一诊断为仓库既有 `_tss`
  literal-operator 弃用 warning；
- `git diff --check` 在文档落地前已通过；最终再执行一次确认。
