# `EmotePlayer` scale、单向 trigger、`variableKeys` 与 `animating` 四二进制联合恢复

日期：2026-08-27

## 1. 本 slice 的闭合范围

本报告闭合 `analysis/motionplayer_emoteplayer_ncb_surface.tsv` 序号 42..53：

- `setHairScale`、`setPartsScale`、`setBustScale` 三个 method；
- `hairScale`、`bustScale`、`partsScale` 三个复用上述 setter 的 read/write property；
- `debugPrint`、`queuing`、`directEdit`、`selectorEnabled` 四个 read/write byte property；
- `variableKeys` 与 `animating` 两个 read-only property。

这 12 个脚本成员在每个平台对应 16 个唯一 callback 函数；三个 scale property 的 setter
与同名 method 是精确地址 alias。除 callback 本身外，本轮还完整读取
`selectorEnabled` 尾调用的 selector 同步 helper。四个参考二进制共同构成权威。

## 2. callback 地址与 fresh 指令覆盖

三个 scale 的 method/setter 与 getter：

| callback | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `setHairScale` / `hairScale set` | `0x67F300`，2 | `0x5620AC`，3 | `0x1001B619C`，2 | `0x1B5F84`，3 |
| `setPartsScale` / `partsScale set` | `0x67F308`，2 | `0x5620B6`，3 | `0x1001B61A4`，2 | `0x1B5F8E`，3 |
| `setBustScale` / `bustScale set` | `0x67F310`，2 | `0x5620C0`，3 | `0x1001B61AC`，2 | `0x1B5F98`，3 |
| `hairScale get` | `0x67F318`，2 | `0x5620CA`，3 | `0x1001B61B4`，2 | `0x1B5FA2`，3 |
| `bustScale get` | `0x67F320`，2 | `0x5620D4`，3 | `0x1001B61BC`，2 | `0x1B5FAC`，3 |
| `partsScale get` | `0x67F328`，2 | `0x5620DE`，3 | `0x1001B61C4`，2 | `0x1B5FB6`，3 |

四个 byte property 与 `variableKeys`：

| callback pair | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `debugPrint get/set` | `0x67F330/0x67F338`，2/3 | `0x5620E8/0x5620EE`，2/3 | `0x1001B61CC/0x1001B61D4`，2/3 | `0x1B5FC0/0x1B5FC6`，2/3 |
| `queuing get/set` | `0x67F344/0x67F34C`，2/3 | `0x5620F6/0x5620FC`，2/3 | `0x1001B61E0/0x1001B61E8`，2/3 | `0x1B5FCE/0x1B5FD4`，2/3 |
| `directEdit get/set` | `0x67F358/0x67F360`，2/3 | `0x562104/0x56210A`，2/3 | `0x1001B61F4/0x1001B61FC`，2/3 | `0x1B5FDC/0x1B5FE2`，2/3 |
| `selectorEnabled get/set` | `0x67F36C/0x67F374`，2/3 | `0x562112/0x562118`，2/3 | `0x1001B6208/0x1001B6210`，2/3 | `0x1B5FEA/0x1B5FF0`，2/3 |
| `variableKeys get` | `0x67F380`，3 | `0x562122`，5 | `0x1001B621C`，3 | `0x1B5FFA`，5 |

两个非叶函数：

| function | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `getAnimating` | `0x671378`，852 | `0x55B18C`，257 | `0x1001AE5D8`，457 | `0x1ADE54`，640 |
| selector sync | `0x66E0FC`，148 | `0x559A8C`，105 | `0x1001AC8A4`，131 | `0x1AC0D0`，195 |

64 个唯一 callback body 与四个同步 helper 均完成 fresh decompile；每个函数同时 fresh
读取 disassembly，总指令数由完整函数边界计算。AArch32 setter 多出的 `VMOV`，以及四端
`getAnimating`/同步 helper 的巨大指令数差异，来自 ABI、异常模型与 Android
libstdc++ / Apple libc++ 的 deque、unordered container 展开，不代表源级控制流差异。

## 3. Engine 字段顺序与 ABI 可见偏移

四端共同源级顺序是：

```text
directEdit : bool
selectorEnabled : bool
queuing : bool
dirty : bool
debugPrint : bool
compiler double alignment
metadataScale : double
inverseCombinedScale : double
hairScale : double
partsScale : double
bustScale : double
variableLabelsBase : tTJSVariant
variableLabels : tTJSVariant
```

callback 直接证明的偏移如下：

| field | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `directEdit` | `+0x487` | `+0x24F` | `+0x317` | `+0x197` |
| `selectorEnabled` | `+0x488` | `+0x250` | `+0x318` | `+0x198` |
| `queuing` | `+0x489` | `+0x251` | `+0x319` | `+0x199` |
| `dirty`（同步 helper 写） | `+0x48A` | `+0x252` | `+0x31A` | `+0x19A` |
| `debugPrint` | `+0x48B` | `+0x253` | `+0x31B` | `+0x19B` |
| `hairScale` | `+0x4A0` | `+0x268` | `+0x330` | `+0x1AC` |
| `partsScale` | `+0x4A8` | `+0x270` | `+0x338` | `+0x1B4` |
| `bustScale` | `+0x4B0` | `+0x278` | `+0x340` | `+0x1BC` |
| `variableLabelsBase` | `+0x4B8` | `+0x280` | `+0x348` | `+0x1C4` |
| `variableLabels`（同步 helper 读） | `+0x4CC` | `+0x28C` | `+0x35C` | `+0x1D0` |

iOS armv7 的五个 byte 后立即自然对齐到 `double`；其余三端在源码层不需要显式 padding。
Variant 的 20/12-byte ABI 差异解释两个 Variant 字段的距离，不能把这些数值恢复成 C++
填充数组。

## 4. 三个 scale 的精确边界

共同伪代码只有原始读写：

```text
setHairScale(v):  engine.hairScale  = v
setPartsScale(v): engine.partsScale = v
setBustScale(v):  engine.bustScale  = v

getHairScale():  return engine.hairScale
getPartsScale(): return engine.partsScale
getBustScale():  return engine.bustScale
```

这些 setter：

- 不设置 `dirty`；
- 不入队或推进任何 controller；
- 不读取 metadata scale 或 mesh division ratio；
- 不 clamp、取绝对值、有限化或规范化；
- 原样保留 NaN、正负无穷和 `-0.0` 的 double bit pattern。

脚本发布顺序是 `hairScale, bustScale, partsScale`，但源字段和 method 顺序是
`hair, parts, bust`。property setter 不是包装函数，而是与三个 method 精确复用同一成员
地址。

## 5. 四个 Boolean property 实际是单向 trigger

前三个 setter 的四端共同伪代码：

```text
setDebugPrint(convertedBoolIgnored): engine.debugPrint = true
setQueuing(convertedBoolIgnored):    engine.queuing = true
setDirectEdit(convertedBoolIgnored): engine.directEdit = true
```

callback 没有读取第二个 ABI 参数。NCBind 仍会在进入 C++ 成员前执行 Variant→Boolean
转换；转换成功后，`false`、`true` 和 `Void` 的结果对 setter 无影响，均固定写 1。若转换
本身抛异常，成员当然尚未进入。公开 API 没有把这三个字节写回 false 的对称路径。

`selectorEnabled` 同样不是普通 assignment：

```text
setSelectorEnabled(convertedBoolIgnored):
    engine.selectorEnabled = true
    tailcall syncSelectorControls(engine)
```

即使字段已经是 true、即使脚本写入 false，也仍无条件执行同步。三个普通 trigger 是
无异常叶写；selector setter 则先提交 byte 写，再进入会分配 Array、复制 Variant 和调用
脚本方法的同步过程。后者抛异常时，已经写入的 true 不回滚。

getter 全部只是无符号 byte load；typed property adaptor 再把它发布为脚本 Integer/Boolean
语义值。

## 6. `variableKeys` 的拥有型返回与快照生命周期

四端 getter 都对 `variableLabelsBase` 执行 `tTJSVariant` copy construction：64 位通过 hidden
return 指针尾调 CopyRef helper，32 位显式建立 frame 后调用相同语义的 copy constructor。
它不是：

- 新建 Array；
- 返回 `variableLabels` 当前工作集；
- 返回借用 dispatch；
- 把内部 Variant move/clear 给调用者。

Engine 构造后该字段最初是 Void，因此首次 metadata reset 前读取也是 Void。metadata reset
或 selector 同步发布 Array 后，重复 getter CopyRef 同一 dispatch；调用者持有自己的引用，
所以后续字段替换乃至 Engine 析构都不会使旧快照失效。property 没有 setter，脚本写入走
read-only descriptor 的 access-denied 路径。

## 7. selector 同步 helper 的数据流、容器与 partial commit

四端共同伪代码：

```text
fresh = create Array together with native Items owner
engine.variableLabelsBase = fresh.variant
fresh.Items = native(engine.variableLabels).Items
engine.dirty = true

for entry in selectorDeque:
    entry.flag = engine.selectorEnabled
    if engine.selectorEnabled:
        entry.selector.commandQueue.clear()
        entry.selector.selState = 0
        applySelection(entry.selector, index=0, duration=0, power=0)
    else:
        std::remove(fresh.Items.begin, fresh.Items.end, Variant(entry.label))
        # returned new-end is deliberately ignored; no erase/shrink

    for target in entry.targets:
        if engine.selectorEnabled:
            variableLabels.remove(target.label)
        else:
            setTarget(target.controller, value=0, duration=0,
                      power=0, append=false)
```

公开 setter 固定把字段写成 true，因此其正常入口总走 enabled 分支；false 分支仍是 helper
本体的一部分，可由 Engine 内部直接状态和调用触达，源码必须保留。几个关键 owner/边界：

- fresh Array 在复制 Items 前就写入公开 backing Variant；复制或后续循环抛异常时，字段可能
  已指向一个空或部分提交的新 Array；
- `dirty` 在 Items 复制成功后才置 true；
- selector command deque clear 会析构已有命令节点但保留实现允许保留的 deque map/block；
- disabled 分支只有 `std::remove` 压缩赋值，没有 erase，尾部元素数量和析构次数不变；
- enabled target 分支通过脚本 Array 的 `remove` member 调用，status 忽略，但异常继续传播；
- 已清队列、已重置 selector、已删除 label 或已写 controller target 的副作用均不回滚。

Android 和 iOS 的 deque block/map 阈值、Variant copy helper 与 EH landing 展开不同；迭代顺序、
提交顺序和异常可见状态一致。

## 8. `animating` 聚合判定器

`animating` 直接绑定 `EmoteEngine::getAnimating`，没有 `EmotePlayer` 包装层。四端共同逻辑：

```text
active(ctl) = ctl.state != 0 || !ctl.queue.empty()

if active(position) || active(scale) ||
   angle.state != 0 || !angle.queue.empty():
    return true

timelineDrivenLabels = empty unordered_set<ttstr>
for label in activeTimelineLabels:
    state = timelineStates.find(label)
    if state missing: continue
    if state.timelineData is null: continue

    for track in state.timelineData.variableList:
        timelineDrivenLabels.insert(track.label)

    if active(state.blendController) || state.loopBegin < 0.0:
        return true

for selector in selectorDeque:
    if (selector.selState != 0 || !selector.commandQueue.empty()) &&
       selector.label not in timelineDrivenLabels:
        return true

for transition in auxTransitionDeque:
    if active(transition.controller) && transition.label not driven:
        return true

for eye in eyeDeque:
    if (eye.trackState != 0 || !eye.valueTrack.empty()) && eye.label not driven:
        return true

for eyebrow in eyebrowDeque:
    if (eyebrow.trackState != 0 || !eyebrow.valueTrack.empty()) &&
       eyebrow.label not driven:
        return true

for mouth in mouthDeque:
    if (mouth.state != 0 || !mouth.valueTrack.empty()) &&
       mouth.label not driven && mouth.talkLabel not driven:
        return true

return false
```

边界行为：

- 直接 controller 集合只包含 position、scale、angle；例如 color controller 单独 active 不会
  使本 getter 返回 true；
- active label 在 timeline map 中缺失时跳过；map 中存在但 `timelineData==null` 时整项跳过，
  即使 blend controller active 或 `loopBegin<0` 也仍视为不贡献；
- 一旦 `timelineData` 存在，native 直接解引用 blend-controller owner，没有额外 null guard；
  人工构造“有 data、无 blend owner”的状态违反内部生命周期前提；
- timeline track label 先全部插入临时 unordered_set，随后才检测该 timeline 的 blend/loop；
  重复 label 合并，查找使用原生 `ttstr` hash/equality；
- `loopBegin<0.0` 对 `-0.0`、NaN 均为 false；
- mouth 必须两个输出 label 都被 timeline 驱动才抑制自身 activity；只抑制一个仍返回 true；
- 任一 early true 都按 RAII 释放临时 unordered-set 节点/buckets；hash、allocation 或 String
  CopyRef 异常传播，不转换成 false。

四端函数大小差异主要来自 unordered-set 插入/查找、Android libstdc++ 与 Apple libc++
bucket/node 策略、AArch64 原子 String CopyRef 展开和 iOS armv7 SjLj 清理；逻辑顺序与 owner
规则相同。

## 9. 与本地源码和测试逐行对照

本地对应：

- `cpp/plugins/motionplayer/EmotePlayer.h:220`：scale alias、四个 trigger 与
  `variableKeys` CopyRef；
- `cpp/plugins/motionplayer/EmoteEngine.h:829`：五个 byte、scale triplet 和三个 Variant 的
  源级声明顺序；
- `cpp/plugins/motionplayer/EmoteEngine.cpp:1104`：selector 同步；
- `cpp/plugins/motionplayer/EmoteEngine.cpp:1339`：`getAnimating`；
- `cpp/plugins/motionplayer/main.cpp:465`：序号 42..53 的 method/property 发布。

现有单元测试已经覆盖：

- `tests/unit-tests/plugins/motionplayer-dll.cpp:12700`：scale alias、NaN/Inf/负零和不置 dirty；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:28366`：false/Void 单向 trigger、重复
  selector assignment 和 fresh Array 发布；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:28414`：`variableKeys` Void、CopyRef alias、
  替换后旧快照和 Engine 析构后的 owner；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:30216`：直接 controller、timeline owner、五类
  deque、mouth 双 label 和无 timeline-data 状态。

逐行对照没有发现新的 C++ 运行语义偏差，本 slice 不修改 C++，也不制造只为当前平台
地址服务的 ABI padding。

## 10. 状态结论与验证边界

`EmotePlayer` 序号 42..53 共 12 行从 `BODY_PENDING_SEPARATE_SLICE` 提升为
`IMPLEMENTED`。全局 NCB pending 从 70 降为 58，`IMPLEMENTED` 从 60 增为 72；注册面仍为
316/316、`UNMAPPED=0`。

四份 IDB 已统一 17 个 callback/helper 名称，添加函数注释和五组关键书签并原位保存。
生成器确定性、strict TSV 与 `git diff --check` 在台账回填后复核。当前环境缺少 CMake、
Ninja、Emscripten，独立 syntax check 又受缺失第三方头文件阻塞，因此不宣称正式
build/unit runtime。剩余 58 行为 `EmotePlayer` 序号 4..41 与 54..73；完整 root-reachable
helper/object/container 分母仍待闭合。
