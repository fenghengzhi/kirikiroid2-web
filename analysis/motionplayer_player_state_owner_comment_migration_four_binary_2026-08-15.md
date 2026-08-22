# MotionPlayer `Player` 状态/owner 旧单端注释迁移（四参考，2026-08-15）

## 1. 范围

本轮处理 `cpp/plugins/motionplayer/Player.h` 中四组仍然绑定旧单端物理布局的注释：

1. `frameProgress` 的 `_deltaTime`、`_firstFrame`、`_motionCompleted`、
   `_reverseSeekFlag` 与 `_allplaying`；
2. type-1 wrapper motion 的 `division` / `motionList` Variant owner 与 motion index；
3. 已删除的 Player `_nextLayerId` 与真正的 ResourceManager layer-id allocator；
4. 已删除的 Player-side animator buckets 与真正的 EmoteEngine controller/HM6/HM7
   数据流。

证据重新取自 `reference/binaries/` 四个当前目标的 recovery IDB。旧
`libkrkr2.so` 地址、Android arm64 单端偏移以及带地址的历史 helper 名均不参与
语义裁决。可执行源码只保留跨 ABI 成立的字段角色；本文件集中保存地址与物理布局。

> 2026-08-16 补充：本文件的 frame-core 阶段顺序保持有效；其中
> `refreshModifiedNodeTimelines()` 内部的严格 emoteEdit owner、专用 hint、setter 与异常
> 边界现由
> `motionplayer_modified_emote_edit_owner_hint_natural_failure_four_binary_2026-08-16.md`
> 完整记录。

## 2. fresh 函数定位

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `Player_progressWrapper_guess` | `0x6CFE78` | `0x595598` | `0x100121204` | `0x11FFB4` |
| `Player_progressBridge_guess` | `0x6CFE34` | `0x595570` | `0x1001211C0` | `0x11FF88` |
| `Player_frameProgress_guess` | `0x6BE44C` | `0x58A63A` | `0x100113B50` | `0x111556` |
| `Player_ctor_guess` | `0x6CC110` | `0x5935C4` | `0x10011EC04` | `0x11D488` |
| `Player_playImpl_guess` | `0x6AF664` | `0x580158` | `0x100107540` | `0x104AE8` |
| `Player_initEmoteMotion_guess` | `0x6B0270` | `0x5807E0` | `0x100107D38` | `0x105350` |
| `ResourceManager_requireLayerId_guess` | `0x6A8A74` | `0x57C258` | `0x100102D40` | `0x100240` |
| `EmoteEngine_setVariable_guess` | `0x66E608` | `0x559D84` | `0x1001ACDBC` | `0x1AC5F4` |
| `EmoteEngine_progressCore_guess` | `0x67A3F8` | `0x55FEF0` | `0x1001B4304` | `0x1B3E10` |

四端脚本 `Player.progress` wrapper 都只做：native receiver 解包、参数数量检查、
`param[0].AsReal()`、`milliseconds * 60.0 / 1000.0`，再进入 progress bridge。
bridge 暂时发布当前 dispatch，依次执行 frame core、updateLayers、calcBounds 与事件转移，
最后清 current-dispatch borrow。wrapper 没有第二套进度状态。

## 3. frame core 字段矩阵

fresh `Player_frameProgress_guess` 的入口共同执行：

```text
processedMeshVerticesNum = 0
motionCompleted = false
deltaTime = speed * inputDt
if directEdit: initEmoteMotion(2)
refreshModifiedNodeTimelines()
```

四端入口都把传入的 frame delta 直接与 `_speedMul` 相乘，并只提交乘积到
`_deltaTime`。乘法前没有把原始参数写进任何 Player member，也没有 finite、正值或
零值检查；`-0`、NaN 与无穷完全服从目标浮点 ABI 的乘法结果。脚本只读
`frameLastTime` 是 motion `lastTime` 元数据 owner，不是“上一次 progress 参数”。
因此源码里曾出现的 `_frameLastTime = dt` / Android arm64 `+904` 说法是旧 port
虚构状态，不能保留为当前结构说明。

相关字段的物理偏移如下；这正是旧 Android arm64 注释不能继续留在 portable header
里的原因：

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `_deltaTime` | `+592` | `+392` | `+480` | `+328` |
| `_speedMul` | `+1168` | `+824` | `+1056` | `+756` |
| `_firstFrame` | `+481` | `+313` | `+369` | `+253` |
| `_motionCompleted` | `+483` | `+315` | `+371` | `+255` |
| `_reverseSeekFlag` | `+609` | `+409` | `+497` | `+345` |
| `_allplaying` | `+1099` | `+751` | `+987` | `+687` |

入口写点分别是：

- Android arm64：清 completed `0x6BE46C`，写 scaled delta `0x6BE474`；
- Android armv7：清 completed `0x58A656`，写 scaled delta `0x58A660`；
- iOS arm64：清 completed `0x100113B64`，写 scaled delta `0x100113B70`；
- iOS armv7：清 completed `0x11156A`，写 scaled delta `0x111576`。

普通 first-frame 分支会清 first-frame，按方向执行 full reseek，并在没有 cooperative
stop 时落入共享 cursor/wrap machine；parameter-selected first-frame 则在自己的 full
reseek 后返回。reverse-seek byte 只在普通 first-frame 分支读取并消费。四端对
`_allplaying` 都使用单个 byte 的读写：停止路径直接清 byte，没有第二个 loop-armed bit，
也没有 bit-level read/modify/write。因此旧注释里基于某两个 Android arm64 `STRB`
位置解释出的结论虽然“单 byte”方向正确，但地址与偏移不是可移植声明的一部分。

## 4. type-1 wrapper 的三个提交槽

成功加载后，`playImpl` 先把 result[0] 提交到 motion-content owner，再读 motion type。
type 等于 1 时，它分别读取 `division` 和 `motionList`，用 Variant copy assignment
立即提交到两个不同 Player member；随后把独立 int32 index 写成 `-1`，再调用
`initEmoteMotion`。

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| division Variant | `+484` | `+316` | `+372` | `+256` |
| motion index | `+504` | `+328` | `+392` | `+268` |
| motion-list Variant | `+508` | `+332` | `+396` | `+272` |
| motion-content Variant | `+528` | `+344` | `+416` | `+284` |

精确提交点：

| 动作 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| division copy-assign | `0x6AFA30` | `0x58034E` | `0x1001077B0` | `0x104DAE` |
| motion-list copy-assign | `0x6AFA88` | `0x580376` | `0x1001077E8` | `0x104DEC` |
| index = -1 | `0x6AFA98` | `0x580384` | `0x1001077F8` | `0x104DFC` |
| init call | `0x6AFAA4` | `0x58038C` | `0x100107804` | `0x104E06` |

Variant assignment是两个独立提交点：第二次 property get/copy 失败不会撤销第一次已经
完成的 owner replacement。四份 constructor 的完整重反编译都能看到 adjacent
type-1 mode byte 被清零，但在 index 的上表偏移没有 constructor store；第一次 type-1
play 才写 `-1`。因此本地 `int _emoteMotionIndex;` 保持无 member initializer 是有意的
生命周期边界，不应因普通 C++ 风格偏好改成 `= -1` 或 `= 0`。

## 5. layer-id allocator 的真正所有者和容器

`ResourceManager_requireLayerId_guess` 四端共同算法为：

```cpp
while (usedLayerIds.find(nextLayerId) != usedLayerIds.end())
    ++nextLayerId;                 // uint32 wrap
const std::uint32_t id = nextLayerId;
usedLayerIds.insert(id);           // exception leaves final increment undone
++nextLayerId;
return static_cast<std::int32_t>(id);
```

Android 使用旧 libstdc++ `_Rb_tree<uint32_t>`，iOS 使用 libc++ `__tree<uint32_t>`；两者
都是有序 set，不是 unordered/open-addressing 容器。四端 ctor 都先插入保留 key 0，
再把 `nextLayerId` 置 1。set/counter 均位于 ResourceManager，不位于 Player。
因此删除 Player `_nextLayerId` 是结构性纠正，而不是把状态迁到
`_nextLayerAbsolute`；后者是另一个独立 Player 语义槽。

本轮 fresh decompile 还再次确认 require 的边界行为：查重循环和最终自增均按
`uint32_t` 自然回绕；insert 抛出时不会执行最终 counter increment；返回值只把同一
32-bit bit pattern 解释为脚本侧 int32。

## 6. controller buckets 属于 EmoteEngine，不属于 Player

四份 `EmoteEngine_setVariable_guess` 都先查 Engine HM6（label -> category/index）：

- 命中受支持 category 时，按记录的 deque index 取得 Engine-owned controller 并 enqueue/
  setTarget；
- transition/selector gate为0或mouth label不匹配时直接返回，不写HM7；只有HM6 miss，或
  type 0/1/2且directEdit打开时，执行 `HM7[label] = value`；
- HM7 的 miss 路径 CopyRef key，value-initialize mapped double，再由 assignment 覆盖。

2026-08-15 fresh router复核还固定了dirty-before-switch、实际call分支内的lazy float
窄化、mouth signed-int32饱和转换和Primary/D3D两套ease管线；见
`analysis/motionplayer_set_variable_router_double_ease_integer_conversion_four_binary_2026-08-15.md`。

HM6/HM7 的对象内偏移也随 ABI/STL 实现漂移：

| Engine map | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| HM6 var-ref routing | `+1384` | `+732` | `+984` | `+528` |
| HM7 label -> double | `+1440` | `+760` | `+1024` | `+548` |

HM7 fallthrough store 位于 `0x66E73C / 0x559E02 / 0x1001ACE58 / 0x1AC66E`。
progress core 则在每个 `min(remaining, 1.1)` slice 中步进 Engine deque family，扩大
float 输出为 double 后 upsert 同一个 HM7，再做 loop/root controllers、HM7-to-Player
binding、clamp、Player bridge 与 physics tail。

旧源码注释把 Android arm64 的六个 deque cursor 读概括成“#4-#9”，也不够准确。
fresh body 实际遍历的是 metadata deque #4、#5、#6、#8、#9、#10；#10 loop-control
在 Android arm64 内联，在另外三端通过 `EmoteEngine_stepLoopControls_guess` 外提。
十个 deque 的 header 大小分别为 Android 80/40 B、iOS 48/24 B，不能把任一端 cursor
偏移当作源级字段表。

最重要的所有权结论不依赖这些物理差异：这些 deque、controller raw owner、HM6 key/
route record 和 HM7 key/value node 都由 EmoteEngine 构造、重置并析构。Player 的
`frameProgress` 只消费由 bridge 传入的 Player 状态；它没有平行的 controller bucket
lookup、step、clear 或 destructor chain。本地早期的
`_type4..8ControllerAnimators` / `_variableAnimators` 及 address-bearing helper 名属于
旧 stepping model 残留，删除它们保持 native object/data-flow 边界。

## 7. 源码迁移结果

`Player.h` 现在：

- 用入口覆盖、one-shot seed、cooperative stop、single playing byte 描述进度字段；
- 用独立 Variant owner 与“constructor 不初始化/index 在 playImpl 写 -1”描述 type-1
  wrapper；
- 用 ResourceManager-owned ordered uint32 set/counter 描述 layer-id；
- 用 Engine HM6 -> typed deque/HM7 -> progress bridge 描述 controller 数据流；
- 不再在这四段 compiled-source 注释中保存绝对地址或某一 ABI 的 member offset。

本轮只迁移注释和证据文档，没有改变字段声明、执行代码或对象大小。
