# MotionPlayer non-root node incremental seek 四参考闭环（2026-08-15）

## 1. 范围、命名与结论

本纵切面只重新审计正常 forward/rewind 四流成员末尾的 non-root node phase。
证据来自 `reference/binaries/` 中四个当前参考产物：

- Android arm64-v8a `libmotionplayer.so`；
- Android armeabi-v7a `libmotionplayer.so`；
- iOS arm64 slice；
- iOS armv7 slice。

旧 Android `libkrkr2.so` 地址、旧源码注释以及当前移植 helper 的形状均不作为函数
身份或控制流证据。本文中的绝对地址只用于恢复记录，不写回编译源码注释。四个
参考二进制都把本 phase 内联在完整的 tag、root、variable-track、node 四流成员里；
本地 `seekNodeTimelineSlotsIncrementalPhase_guess` 是 source-level 抽取名，并不声称
原版有这个独立符号。剥离符号下不能证明的原始名继续使用 `_guess`。

四端共同语义现已闭合：

1. node deque 走半开区间 `1..<live nodeCount`，每处理一个 node 后重新求 size；
2. parameter-bound node 在 forward/rewind 都调用同一个共享双向 stepper，并跳过
   ordinary node 的 action/dirty/merge/source inline tail；
3. ordinary forward 快照 raw active selector，建立只供 dynamic count 的 frame-list
   owner，使用 wrapping signed limit、live ordered-LT gate，并按
   selector -> parse -> action -> swap 顺序跨 frame；
4. ordinary rewind 不读 count、不建立 frame-list owner，使用 live ordered-GT gate，
   按 selector -> wrapping decrement -> parse entered slot -> action -> live recheck 跨帧；
5. 两个方向都只在至少完成一次 crossing 后延迟写 `node.flags = 1`，随后按 physical
   slot0、slot1 merge，再执行 source gate；
6. source gate 直接计算 `1 << nodeType`，四端都没有范围保护。这个边界在 C++ 对
   越界 shift 属于未定义行为，而具体机器指令在 AArch64 与 ARM32 上可能表现不同，
   不能用一个“安全”范围检查把参考行为统一掉。

## 2. aggregate ABI、phase 边界与容器布局

| 项目 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| forward 四流成员 | `0x6B3EBC` | `0x582BE0` | `0x10010AA08` | `0x1083D8` |
| forward node loop | `0x6B4744` | `0x583084` | `0x10010B07C` | `0x10892A` |
| rewind 四流成员 | `0x6B6E1C` | `0x584838` | `0x10010D230` | `0x10AB68` |
| rewind node loop | `0x6B7538` | `0x584C28` | `0x10010D700` | `0x10AFC0` |
| aggregate code size | `2632` / `2632` | `2272` / `2272` | `2648` / `2648` | `2228` / `2228` |
| node deque ABI | libstdc++，大元素一元素 block | libstdc++，大元素一元素 block | libc++，16 元素 block | libc++，16 元素 block |

表中的两项 code size 分别对应 forward/rewind；四端各自的两个聚合成员大小相同。
所有 caller 都只传 `this`，没有额外 target time。node phase 的比较直接读取 Player
live evaluation 字段，所以本地 helper 也不能接受或缓存 aggregate 入口的浮点快照。

Android 的 libstdc++ 大元素 deque 与 iOS 的 libc++ 固定 16 元素 block 产生完全不同
的 block/index 算术，但共同源码范围是：

```text
for (i = 1; i < nodes.size(); ++i)
    process(nodes[i])
```

不是 `1..<size-1`，也不存在尾 sentinel。size 表达式在每轮 node 完成后重新计算；
frame getter、action 入队、merge 和 source lookup 都可能重入脚本或资源代码，故不能
把 nodeCount 在 phase 入口快照。

## 3. 关键字段的四端偏移

| 语义字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `MotionNode.parameterEntry` | `+8` | `+4` | `+8` | `+4` |
| `MotionNode.nodeType` | `+28` | `+20` | `+28` | `+20` |
| `MotionNode.flags` | `+44` | `+36` | `+44` | `+36` |
| `MotionNode.frameListVariant` | `+64` | `+56` | `+64` | `+56` |
| physical slot0 | `+320` | `+296` | `+320` | `+288` |
| slot stride | `536` | `432` | `536` | `420` |
| `MotionNode.activeSlotIndex` | `+1392` | `+1160` | `+1392` | `+1128` |
| `MotionNode.forceVisible` | `+1996` | `+1716` | `+2012` | `+1680` |
| `Player.preview` | `+1092` | `+744` | `+980` | `+680` |
| `Player.liveEvaluation` | `+456` / `+0x1C8` | `+288` / `+0x120` | `+344` / `+0x158` | `+228` / `+0xE4` |

slot stride 的 Android/iOS armv7 差异不是算法分叉，而是两套 ABI 下 Variant、ttstr、
alignment 和 nested payload 的布局结果。active/other 的选择必须保留 raw cursor 与
parity 混用：active 用 raw `slots[cursor]`，other 用 `(cursor & 1) == 0` 选另一个
physical slot。把 active 也先钳成 0/1 会抹掉畸形 cursor 的原生边界。

## 4. parameterized node 的路由

| 边界 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| forward parameter gate/call | `0x6B479C` | `0x5830BC` | `0x10010B0B4` | `0x10896A` |
| rewind parameter gate/call | `0x6B7590` | `0x584C4E` | `0x10010D738` | `0x10AFF8` |
| shared stepper | `0x6B5224` | `0x58387C` | `0x10010BA1C` | `0x1093A0` |

`parameterEntry != nullptr` 时两个 aggregate 方向都调用同一
`MotionNode_seekParameterizedFrames_guess(node, player)`，随后直接进入下一 node。
共享 stepper 使用 eased parameter value 做 forward 加 corrective rewind，本 phase 不再
额外触发 ordinary node action，也不重复 ordinary dirty/merge/source tail。其独立
parse/merge/source 边界已记录于
`analysis/motionplayer_node_timeline_slot_helpers_four_binary_2026-08-14.md`。

## 5. ordinary forward

### 5.1 四端指令锚点

| 边界 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| raw active snapshot | `0x6B47BC` | `0x5830C8` | `0x10010B0C0` | `0x108978` |
| frame-list CopyRef owner | `0x6B47CC..0x6B4818` | `0x5830D0..0x5830E2` | `0x10010B0D0..0x10010B0F0` | `0x108984..0x10899A` |
| dynamic count | `0x6B4820` | `0x5830E8` | `0x10010B0F8` | `0x1089A4` |
| wrapping `count-2` | `0x6B4834` | `0x5830F8` | `0x10010B108` | `0x1089B4` |
| signed active/limit compare | `0x6B483C` | `0x583100` | `0x10010B110` | `0x1089BC` |
| live eval / ordered-LT | `0x6B4854..0x6B4864` | `0x58311C..0x583128` | `0x10010B134..0x10010B140` | `0x1089E2..0x1089EE` |
| selector commit | `0x6B4874` | `0x583134` | `0x10010B150` | `0x1089FC` |
| wrapping next index | `0x6B487C` | `0x58313C` | `0x10010B158` | `0x108A06` |
| parse | `0x6B4888` | `0x583142` | `0x10010B164` | `0x108A0E` |
| action gate/enqueue | `0x6B488C..0x6B48C4` | `0x583146..0x583180` | `0x10010B168..0x10010B1A0` | `0x108A18..0x108A4A` |
| loop limit recheck | `0x6B48D8..0x6B48E4` | `0x58318C..0x583198` | `0x10010B1AC..0x10010B1B8` | `0x108A56..0x108A62` |
| delayed exact `flags=1` | `0x6B46A0` | `0x5831AC` | `0x10010B1DC` | `0x108A70` |
| physical merges | `0x6B46B4/0x6B46CC` | `0x5831BC..0x5831D4` | `0x10010B1EC/0x10010B20C` | `0x108A7E..0x108AAE` |
| direct source shift | `0x6B46EC` | `0x5831E6` | `0x10010B238` | `0x108AC0` |
| `findSource` | `0x6B4718` | `0x583208` | `0x10010B278` | `0x108AE6` |
| local owner release | `0x6B471C..0x6B4734` | `0x58320C..0x58321A` | `0x10010B27C..0x10010B29C` | `0x108AEA..0x108AFA` |

### 5.2 共同伪代码

```text
cursor = node.activeSlotIndex                         // getter 前快照 raw 值
frameListOwner = CopyRef(node.frameListVariant)
count = frameListOwner["count"].AsInteger32()
limit = signed32(uint32(count) - 2)
active = &node.slots[cursor]                         // raw cursor
other  = &node.slots[(cursor & 1) == 0]              // parity opposite
seeked = false

while signed(active.frameIndex) < limit:
    if livePlayerEvaluation < other.clipStartTime:   // ordered LT
        break

    node.activeSlotIndex = ((node.activeSlotIndex & 1) == 0)
    parse(active,
          node.frameListVariant,                     // persistent re-read
          signed32(uint32(other.frameIndex) + 1))
    if other.contentMask & 0x40000:
        enqueueAction(node.layerName, other.actionValue)
    seeked = true
    swap(active, other)

if seeked:
    finishOrdinaryTail(node)

destroy(frameListOwner)                              // after source tail
```

`frameListOwner` 只被 count getter 使用，但其生命周期跨过 parse、action、merge 与 source
refresh 到 node 尾部。parse/merge 故意重读 `node.frameListVariant`；若 count getter 清空
持久字段和调用者最后一个 owner，本地 owner 仍使 dispatch 活到 unwind，但紧随其后的
parse 会在 Void 持久字段上失败。这种 local-owner/persistent-source split 不能合并成
统一 source 引用。

`count-2` 在 32 位寄存器中回绕，再以 signed domain 与 `active.frameIndex` 比较。
时间门是 ordered `if (eval < other.time) break`；NaN 比较为 false，forward 会继续到
signed limit。每轮重新读取 Player live evaluation，getter/action 的重入修改立即影响
下一 crossing。

### 5.3 action 与异常提交前缀

forward action 读取的是 crossing 前的 old-other slot，不是刚 parse 的 old-active slot。
准确顺序是：

```text
selector store
wrapping next-index calculation
parse old-active slot
action from crossed old-other slot
local active/other swap
```

因此：

- parse 抛出：selector 已切换，parser 内先写的 frameIndex/merged 前缀可见，无 action；
- action 构造或入队抛出：selector 与 parser state 已提交，但 `seeked`、flags、merge、
  source tail 尚未提交；
- 完整 crossing 后才允许 `flags=1` 与后续 tail。

## 6. ordinary rewind

### 6.1 四端指令锚点

| 边界 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| raw active / no-count path | `0x6B75AC` | `0x584C54` | `0x10010D744` | `0x10AFFE` |
| initial live ordered-GT | `0x6B75B0..0x6B75C4` | `0x584C58..0x584C64` | `0x10010D748..0x10010D758` | `0x10B006..0x10B012` |
| selector commit | `0x6B75FC` | `0x584C94` | `0x10010D7A0` | `0x10B056` |
| wrapping decrement | `0x6B7608` | `0x584C9C` | `0x10010D7A8` | `0x10B062` |
| parse | `0x6B7614` | `0x584CA2` | `0x10010D7B4` | `0x10B066` |
| action gate/enqueue | `0x6B7618..0x6B764C` | `0x584CAA..0x584CD8` | `0x10010D7BC..0x10010D7F0` | `0x10B06E..0x10B0A2` |
| live ordered-GT recheck | `0x6B765C..0x6B7664` | `0x584CE2..0x584CF2` | `0x10010D800..0x10010D808` | `0x10B0B8..0x10B0C4` |
| delayed exact `flags=1` | `0x6B766C` | `0x584CFA` | `0x10010D81C` | `0x10B0CA` |
| physical merges | `0x6B7680/0x6B7698` | `0x584D08..0x584D24` | `0x10010D82C/0x10010D848` | `0x10B0D8..0x10B0FA` |
| direct source shift | `0x6B76C8` | `0x584D42` | `0x10010D878` | `0x10B11C` |
| `findSource` | `0x6B76F0` | `0x584D60` | `0x10010D8B4` | `0x10B142` |

### 6.2 共同伪代码

```text
cursor = node.activeSlotIndex
active = &node.slots[cursor]                         // raw cursor
if !(active.clipStartTime > livePlayerEvaluation):   // ordered GT
    continue

other = &node.slots[(cursor & 1) == 0]
seeked = false
do:
    node.activeSlotIndex = ((node.activeSlotIndex & 1) == 0)
    parse(other,
          node.frameListVariant,
          signed32(uint32(active.frameIndex) - 1))
    if other.contentMask & 0x40000:
        enqueueAction(node.layerName, other.actionValue)
    seeked = true
    if !(other.clipStartTime > livePlayerEvaluation):
        break
    swap(active, other)
while true

if seeked:
    finishOrdinaryTail(node)
```

rewind 没有任何 `count` 属性读取，也不 CopyRef frame list。index decrement 是无零保护
的 32 位减法：零回绕为 `0xFFFFFFFF`，进入 TJS 数字 getter 时按同位型 signed 值
解释为 `-1`。initial 和 loop-tail 都使用 ordered `>`；NaN 使条件为 false，所以 rewind
停止。parser/action 完成后才重读 live evaluation，time getter 或回调的重入变化立即
决定是否再退一帧。

rewind action 来自刚刚 parse 的 entered other slot。其异常前缀与 forward 类似：
selector 和 parser state 先提交，action 抛出时尚未发布 `flags=1`，也不执行 merge 或
source refresh。

## 7. 共同 ordinary tail 与 direct-shift 边界

```text
finishOrdinaryTail(node):
    node.flags = 1                                  // exact overwrite, not OR
    if !node.slots[0].merged:
        merge(node.slots[0], node.nodeType, node.frameListVariant)
    if !node.slots[1].merged:
        merge(node.slots[1], node.nodeType, node.frameListVariant)

    mask = player.preview ? 6153 : 6145
    if node.forceVisible || ((1 << node.nodeType) & mask):
        findSource(node)
```

flags 写入在 crossing loop 完整结束后，且覆盖整个 byte 为 `1`，不是 `flags |= 1`。
若一帧都未跨过，旧 flags 完全不变，也不 merge 或 findSource。merge 顺序固定为
physical slot0 再 slot1，二者都重读持久 frame-list Variant。

source gate 的直接 shift 证据：

| 方向 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| forward | `0x6B46EC LSL W8,W26,W8` | `0x5831E6 LSL.W` | `0x10010B238 LSL` | `0x108AC0 LSL.W` |
| rewind | `0x6B76C8 LSL W8,W26,W8` | `0x584D42 LSL.W` | `0x10010D878 LSL` | `0x10B11C LSL.W` |

四处前面都没有 `nodeType < 32`、`0 <= nodeType` 或等价范围 gate。AArch64 variable
shift 使用低 5 位作为 shift count；ARM32 register shift 对 `>=32` 的定义不同。原始
C++ 的越界 signed-left-shift 本身又有 UB，所以这个边界只能按“源码无 guard、平台
机器结果可能不同”恢复，不能在 port 中添加范围判断。`mergeNodeFrameContent_guess`
内部若存在自己的 node-type 分派边界属于独立 helper 审计，不由本纵切面外推修改。

## 8. 本地实现修正

`cpp/plugins/motionplayer/PlayerUpdateLayerEval.cpp` 已完成：

- 将 ordinary forward/rewind 从旧共享近似路径拆开；
- phase 不再接收 target 参数，每次比较读取 `_clampedEvalTime`；
- forward 恢复 raw selector、count-only owner、32 位 wrapping limit/index、
  ordered-LT、parse-before-action 和 owner 尾生命周期；
- rewind 恢复 no-count/no-owner、ordered-GT、zero-underflow `-1`、entered-slot action；
- 两方向都恢复 delayed exact `flags=1`、physical slot0/slot1 merge 和 direct shift
  source gate；
- node deque size 在每轮后重读；parameterized node 继续使用已独立审计的共享 stepper。

`PlayerFrameProgress.cpp` 的 aggregate 调用和 `Player.h` 声明已去掉 node phase target
参数。旧 `seekNodeFramesForwardPhase_guess` /
`seekNodeFramesBackwardPhase_guess` source wrapper 已移除，避免继续暗示参考二进制有
两个独立 node helper。新增 test-only 入口只服务 differential-style Catch2 harness，
不注册为 `Motion.Player` 脚本 API。

## 9. 回归覆盖与 numeric-read 推导

新增 Catch2 用例覆盖：

1. forward count getter 把 evaluation 从 5 改为 25，证明后续 crossing 使用 live time；
2. forward action 顺序为 old-other 的 `frame-one`，再为下一次 old-other 的
   `frame-two`；
3. forward NaN 通过 ordered-LT gate 推进到 signed count limit；
4. count getter 清空 node 持久 source 与最后 external owner，local owner 仍保活到
   parse 失败 unwind；selector 与 parser frameIndex 已提交，而 flags 保持旧 `0x40`；
5. rewind 完全不读 count；frame 0 的 time getter 把 evaluation 从 20 改为 5 后，
   loop-tail live recheck 再走一步，并把 zero index 回绕成数字 `-1`；
6. rewind action 顺序为 newly entered `zero`、`negative`；
7. rewind NaN 在 initial ordered-GT gate 立即停止，numeric getter 和 dirty tail 都不触发。

forward 两次 crossing 各由 parser 读取数字 frame 2、3；尾部 physical merge 又分别
读取 frame 2、3，所以完整数字读取序列是 `{2,3,2,3}`。rewind 两次 crossing 由 parser
读取 0、-1，尾部 merge 再读 0、-1，所以序列是 `{0,-1,0,-1}`。这同时固定了 parser
与 merge 的 source 重读次数和相对顺序。

## 10. IDB 固化与验证

四份 recovery IDB 均已写入以下边界的 line comment：

- live half-open deque loop 与 parameterized route；
- raw selector、forward count-only owner、wrapping limit、live ordered-LT；
- selector-before-parse、parse-before-action、delayed exact flags、physical merge；
- rewind no-count、ordered-GT、wrapping zero-underflow、entered-slot action、live recheck；
- direct raw-nodeType shift 与 forward owner 尾释放。

每份 IDB 另加四个 bookmark：forward count-only owner、forward parse-before-action、
rewind no-count underflow、node source direct shift。四份数据库均在写入后
`idb_save ok=true`。

验证结果：

- 完整 motionplayer Catch2 翻译单元用 Web Debug 的真实 Emscripten response file
  执行 `-fsyntax-only` 成功，仅有仓库既有 `_tss` warning；
- `cmake --build --preset "Web Debug Build" -- -j 8` 完成全部 33 个 compile、archive、
  `index.html` 与 Wasm 最终链接步骤；
- 完整构建只有既有 `_tss`、nodiscard 与 Emscripten warning，没有本轮新增错误。

## 11. 四流成员闭环状态

至此 normal forward/rewind 四流 aggregate 的全部四个 inline phase 都有 fresh 四端
专题：

1. layer/tag 与 root/priority：
   `analysis/motionplayer_incremental_tag_root_streams_four_binary_2026-08-15.md`；
2. variable-track：
   `analysis/motionplayer_variable_track_incremental_seek_four_binary_2026-08-15.md`；
3. non-root node：本文。

这只闭合了两个 normal incremental aggregate 的内部数据流，不代表整个 motionplayer
插件已完成。后续仍应按 fresh 四参考证据继续清除邻接 helper 和其他模块中残留的旧
`libkrkr2.so` 身份、过时字段解释与端口自创边界。
