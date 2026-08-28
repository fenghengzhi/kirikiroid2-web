# controller 容器元素 owner、borrow 与 publication 四端审计

## 结论

`MP-L08` 已闭合。四个参考二进制共同恢复出 `EmoteEngine` 前部十个 metadata deque 的
十种独立 source element 语义；不能把它们压成统一的 `{void *controller, label}`：

```text
EmoteEngine
  deque #1  element unique-owns EmoteSpringState
  deque #2  element unique-owns EmoteBustChainSpring
  deque #3  element unique-owns EmoteBustChainSpring
  deque #4  element unique-owns EmoteBlinkController
  deque #5  element unique-owns EmoteEyebrowController
  deque #6  element unique-owns EmoteMouthController
  deque #7  element owns two ttstr values; no controller pointee
  deque #8  element unique-owns EmoteVarController
  deque #9  element unique-owns EmoteSelectorController
             selector.optionList[].refCtl borrows deque #8 pointees
             entry.targets[] borrows deque #8 entries（native builder保持为空）
  deque #10 element unique-owns EmoteLoopController

  controller-ref map owns ttstr keys + POD {type,index}
      resolves/borrows one deque element at use time; owns no controller

  chain spring.collisionCurve borrows Engine raw wind emitter
```

Eye/Eyebrow controller 还各自内嵌一个 owning mesh-resolver graph：两个 keyframe/path deque、
edge vector、`deque<vector<float>>` node rows，以及
`vector<{deque<pair<float,float>> path,float dist}>` candidate rows。resolver 调用只临时 borrow
这组 embedded containers 和目标 track；没有 persistent resolver pointer 或 shared owner。

所有 controller/spring builder 都在目标对象 constructor 成功后把 pointee 暂存为 raw
pointer，直到目标 deque element 的 raw-pointer constructor/emplace 成功才转交 single
ownership。因此 constructor 本身抛出由 new-expression cleanup；constructor 已完成后，
属性读取、内部 vector resize 或 deque growth 抛出则可能泄漏 raw pointee。owner publication
之后的 label/map 写入抛出不回滚 element，而是留下完整或部分发布的 live owner。

本地十种 typed elements、`unique_ptr` handoff、borrowed edges、稀疏 resolver、partial commit、
reset/normal-destruction 顺序均匹配，没有 semantic C++ edit。四个 IDB 已补充 100 个确定性
函数名、owner/EH 注释、书签并保存。

## 1. 四端 builder 函数

九个 unique builder 覆盖十个 deque：#2/#3 共享 Chain builder，仅目标 deque 与 type tag
不同。下表函数本轮均 fresh decompile；完整 disassembly 均 `cursor.done=true` 且
`truncated=false`。

| builder | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| #1 Bust/simple spring | `0x6683F8` / 520 | `0x55659C` / 328 | `0x1001A7DDC` / 250 | `0x1A730C` / 423 |
| #2/#3 Chain spring | `0x668DB0` / 871 | `0x556B84` / 558 | `0x1001A87C0` / 416 | `0x1A7DCC` / 664 |
| #4 Eye | `0x669B5C` / 263 | `0x55739C` / 175 | `0x1001A91F4` / 124 | `0x1A8800` / 206 |
| #5 Eyebrow | `0x669F7C` / 263 | `0x557618` / 175 | `0x1001A9540` / 124 | `0x1A8B68` / 206 |
| #6 Mouth | `0x66A39C` / 321 | `0x557894` / 203 | `0x1001A988C` / 157 | `0x1A8ED0` / 261 |
| #8 Transition | `0x66A8A4` / 269 | `0x557B84` / 173 | `0x1001A9C9C` / 131 | `0x1A9314` / 214 |
| #9 Selector | `0x66ACDC` / 593 | `0x557E04` / 331 | `0x1001AA030` / 412 | `0x1A96D8` / 626 |
| #10 Loop | `0x66B860` / 448 | `0x558440` / 296 | `0x1001AAA8C` / 216 | `0x1AA158` / 333 |
| #7 Clamp | `0x66C23C` / 321 | `0x55892C` / 213 | `0x1001AB0A8` / 166 | `0x1AA760` / 275 |

`applyMetadata` 四端 caller 分母证明统一调用顺序是：

```text
resetMetadataState
optional variableList
#1 Bust
#2 Hair chain
#3 Parts chain
#4 Eye
#5 Eyebrow
#6 Mouth
#8 Transition
optional #9 Selector
#10 Loop
#7 Clamp
Mirror / Instant / Timeline
syncSelectorControls
```

因此 Selector 构造 borrowed option 时，Transition deque 已完整完成本轮 build；之后正常
metadata build 不再向 #8 append，borrowed pointee 地址在本次 metadata lifetime 内稳定。

## 2. pointee constructor 与内部 owner

| pointee constructor | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `EmoteSpringState` | `0x65F828` / 135 | `0x55176C` / 116 | `0x1001A18C4` / 82 | `0x1A099C` / 157 |
| `EmoteBustChainSpring` | `0x6662D8` / 430 | `0x5554F0` / 317 | `0x1001A6104` / 245 | `0x1A5710` / 413 |
| `EmoteBlinkController` | `0x65FD48` / 792 | `0x551B34` / 375 | `0x1001A1C8C` / 280 | `0x1A0E50` / 476 |
| `EmoteEyebrowController` | `0x661BEC` / 686 | `0x552CDC` / 299 | `0x1001A31F4` / 227 | `0x1A2560` / 369 |
| `EmoteMouthController` | `0x663078` / 102 | `0x55369C` / 64 | `0x1001A3DE4` / 39 | `0x1A3200` / 85 |
| `EmoteVarController` | `0x664410` / 67 | `0x554180` / 45 | `0x1001A4AD0` / 44 | `0x1A3FEC` / 95 |
| `EmoteSelectorController` | `0x66B778` / 58 | `0x5583B6` / 33 | `0x1001B7DFC` / 33 | `0x1B75EC` / 73 |
| `EmoteLoopController` | builder 内联 | builder 内联 | builder 内联 | builder 内联 |

全部 constructor 也 fresh decompile 并完整 disassemble，无截断。

### 2.1 目标 object size

| pointee | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Simple spring | `0x48` | `0x48` | `0x48` | `0x48` |
| Chain spring | `0xB0` | `0xA8` | `0xB0` | `0xA8` |
| Blink | `0x170` | `0xD8` | `0x110` | `0xA8` |
| Eyebrow | `0x150` | `0xB8` | `0xF0` | `0x88` |
| Mouth | `0x70` | `0x48` | `0x50` | `0x38` |
| Transition Var | `0x80` | `0x48` | `0x60` | `0x38` |
| Selector | `0x80` | `0x48` | `0x60` | `0x38` |
| Loop | `0x20` | `0x14` | `0x20` | `0x14` |

Blink/Eyebrow/Var/Selector 的差异主要来自 Android old-libstdc++ 与 iOS libc++ 的
deque/vector header；不是 inheritance、vptr、额外 source owner 或 target-specific class。

### 2.2 nested owner graph

```text
Blink / Eyebrow controller
  owns primary deque<12B keyframe>
  owns secondary deque<pair<float,float>>
  embeds MeshResolverState
    owns vector<pair<float,float>> edgeTable
    owns deque<vector<float>> nodeRows
      each row owns its float buffer
    owns vector<MeshPathRow> outputRows
      each row owns deque<pair<float,float>> path

Mouth controller
  owns deque<12B keyframe>

Transition Var controller
  owns deque<20B keyframe>
  raw-owns current/start/target float arrays

Selector controller
  owns deque<12B command>
  owns vector<SelectorOption>
    each option borrows EmoteVarController from deque #8

Loop controller
  owns vector<12B loop keyframe>

Simple/Chain spring
  no nested heap owner
Chain spring
  borrows optional wind emitter through collisionCurve
```

Blink/Eyebrow constructor 的每个 STL member 一经构造即由 controller subobject ownership
覆盖；constructor 后续 property/container allocation 抛出时，已构造 member 按反序 unwind，
outer new-expression 再释放 controller storage。它们没有类似 Var controller raw-array 的
constructor-prefix leak。

`EmoteVarController` 第二/第三 raw-array allocation失败泄漏前缀的独立证据与 disposition
见 `MP-L07`；本任务只把该行为接入 deque #8 element 的 owner 图。

## 3. element stride 与成员析构顺序

| deque element | 64-bit stride | 32-bit stride | reverse member destruction |
|---|---:|---:|---|
| #1 Simple spring | 48 | 28 | keyY → keyX → shapeLabel → spring |
| #2/#3 Chain spring | 56 | 32 | keyC → keyB → keyA → shapeLabel → spring |
| #4 Eye | 16 | 8 | label → Blink controller |
| #5 Eyebrow | 16 | 8 | label → Eyebrow controller |
| #6 Mouth | 24 | 12 | talkLabel → label → Mouth controller |
| #7 Clamp | 40 | 28 | varUd → varLr；无 pointee |
| #8 Transition | 24 | 12 | label → Var controller |
| #9 Selector | 48 | 24 | targets vector → label → Selector controller |
| #10 Loop | 16 | 8 | label → Loop controller |

四端 clear/range-destructor bodies逐 element 验证了这张表：所有 ttstr 先 Release，随后
ordinary pointee destructor + scalar delete；Spring/Chain pointee没有 non-trivial member，
因此只需 scalar delete。Selector 的两个 borrowed collections 只释放自己的 vector buffer，
不 delete target entry 或 transition controller。

## 4. 每类 publication frontier

### 4.1 #1 Simple spring

共同顺序：

```text
read enabled / param accessor
raw = new EmoteSpringState(metadata)
read param.op -> storedXYZ
read param.p  -> posXYZ
read param.pv -> velXYZ
read param.ofs -> biasY
deque#1.emplace_back(raw)             # owner publication
entry.shapeLabel = baseLayer
entry.keyX = var_lr
entry.keyY = var_ud
resolver[keyX] = {type=0, metadataIndex}
resolver[keyY] = {type=0, metadataIndex}
```

constructor 成功后到 emplace 之间的 property/conversion/deque-growth异常泄漏 raw 72-byte
spring。emplace 后的异常保留 live entry；已成功的 string/map publication 不回滚。

### 4.2 #2/#3 Chain spring

共同顺序：

```text
raw = new EmoteBustChainSpring(metadata)
read op/ofs/bendR/bendS
own temporary accessors bp -> p -> pv
decode each two-element vec3 array into p -> pv -> bp state
targetDeque.emplace_back(raw)         # owner publication
entry.shapeLabel = baseLayer
entry.keyA = var_lr
entry.keyB = var_lrm
entry.keyC = var_ud
resolver[keyA] = {typeTag, metadataIndex}
resolver[keyB] = {typeTag, metadataIndex}
resolver[keyC] = {typeTag, metadataIndex}
```

raw leak window 比 #1 更长，覆盖全部 nested array decode。entry constructor 将四个 ttstr
置 empty、anchors 置零，但故意不写 `initFlag` 与 ABI padding。每次 chain step 在可能读取
wind 之前把 `collisionCurve = Engine._windEmitter`，它始终只是 borrow。

### 4.3 #4/#5 Eye/Eyebrow

共同 publication shape：

```text
raw = new ConcreteController(metadata)
targetDeque.emplace_back(raw)         # owner publication
entry.label = metadata.label
resolver[label] = {type=4 or 5, metadataIndex}
```

controller constructor 自己的 container prefix 有 C++ unwind；constructor 正常返回后到
emplace growth成功前没有临时 owner，因此该窄窗口抛出会泄漏整个 controller 及其 nested
containers。label/map失败发生在 owner publication 后，只留下 empty/assigned label 或缺失/
半发布 resolver。

### 4.4 #6 Mouth

```text
raw = new MouthController(metadata)
deque#6.emplace_back(raw)              # owner publication; labels initially empty
entry.label = label
entry.talkLabel = talkLabel
resolver[label] = {6, metadataIndex}
resolver[talkLabel] = {6, metadataIndex}
```

equal、empty、duplicate label均接受；同一 map key 的后写覆盖 resolver，但不会删除任何
已有 deque owner。

### 4.5 #7 Clamp

#7 是唯一没有 controller pointee的元素：

```text
deque#7.emplace_back()                 # zero/default entry先 publication
entry.type = type
entry.varLr = var_lr
entry.varUd = var_ud
entry.minValue = min
entry.maxValue = max
```

它不写 controller-ref map。任一后续 getter/conversion 抛出都保留已 append 的 partial entry；
builder 不清旧 deque，重复调用继续 append。

### 4.6 #8 Transition

```text
raw = new EmoteVarController(count=1)
deque#8.emplace_back(raw)              # owner publication
entry.flag = 1                         # raw entry constructor阶段
entry.label = label
resolver[label] = {7, metadataIndex}
```

raw Var constructor 的 array failure边界延续 MP-L07；constructor完成后的 deque growth失败
泄漏完整 controller。后续 Selector 命中时只 borrow `entry.ctl.get()` 并清 `entry.flag=0`，
不移动 ownership。

### 4.7 #9 Selector

每个 metadata element先读 label；disabled entry调用 variable-label remove后直接跳过。enabled
entry 的共同顺序：

```text
local vector<SelectorOption> options
for option in optionList:
    read option.label
    restart scan deque#8 from front
    first equal label:
        borrowed = transitionEntry.ctl.get()
        transitionEntry.flag = 0
        removeVariableLabel(option.label)
    read offValue, onValue
    options.push_back({borrowed, offValue, onValue})

raw = new SelectorController(move(options))
    move vector ownership into controller
    immediately applySelection(index=0, duration=0, fade=0)

deque#9.emplace_back(raw)              # owner publication
entry.label = metadata label
entry.flag remains constructor-uninitialized
entry.targets default-constructs empty
resolver[label] = {8, metadataIndex}
```

关键 partial commit：transition flag与 variable-label removal 发生在后续 float read、options
growth、Selector allocation/constructor之前；任何异常都不回滚这些 side effects。Selector
constructor 的 initial apply可以原地修改 borrowed Transition controller，queue allocation
异常同样不会回滚较早 option。

Selector constructor成功后到 deque #9 emplace之间仍是 raw pointer，growth失败会泄漏
Selector及其 option vector。entry publication以后 label/map异常只留下 partial entry。

`entry.targets` 是另一条 `vector<TransitionEntry*>` borrow边；四端完整 builder、sync与三个
selector-target API 的 xref 分母均没有 writer，正常 native lifetime内保持 empty。读路径
必须保留，但不能从 option labels推导或自动填充。

### 4.8 #10 Loop

```text
raw = new zero/default LoopController
raw.keys.resize(transitionList.Count)
decode every [start,end,span] triple into 12B element
deque#10.emplace_back(raw)             # owner publication
entry.label = var_loop
resolver[label] = {3, metadataIndex}
```

new-expression在 trivial/default construction后已经结束；`resize` 和全部 triple decode都在
raw local阶段。任一异常都会泄漏 controller以及已经分配/填充的 key vector。deque growth
失败同样泄漏；publication后的 label/map异常留下 live partial entry。

## 5. borrowed resolver 与稀疏 index

`_variableControllerRefs` node owns：

```text
ttstr key owner
int32 type
int32 metadataIndex
```

它不持有 pointer、AddRef或删除责任。`setVariable` 命中后根据 type直接用 index访问对应
deque，再 borrow element/pointee完成 setter。所有 builders都保存原始 metadata loop index，
但 disabled elements不 append placeholder。因此 disabled hole会使 resolver index与 compacted
deque position分离；后续路径没有 bounds guard。这是四端共同 malformed-metadata边界。

duplicate/equal/empty keys允许；后写 resolver覆盖旧 `{type,index}`，旧 deque element与其
heap owner继续存活直至 reset/dtor。map clear先释放 resolver keys/nodes，不影响 pointee。

另外三类 borrow：

- Selector option `refCtl`：first-label match borrow deque #8 pointee；never delete；
- Selector entry `targets`：borrow deque #8 entry；正常 builder保持 empty；
- Chain spring `collisionCurve`：borrow live wind emitter；每次 step前刷新，spring dtor不读。

## 6. reset、普通析构与 borrow lifetime

四端 metadata reset：

| 目标 | `resetMetadataState` / 指令数 |
|---|---:|
| Android arm64 | `0x666D08` / 250 |
| Android armv7 | `0x555AD8` / 32 |
| iOS arm64 | `0x1001A67BC` / 34 |
| iOS armv7 | `0x1A5F4C` / 32 |

共同 source phase：

```text
clear controller-ref map
clear deque #1
clear deque #2
clear deque #3
clear deque #4
clear deque #5
clear deque #6
clear deque #7
clear deque #8
clear deque #9
clear deque #10
clear mirror/timeline/late metadata containers
```

reset按 declaration order清 #8 后清 #9，因而 Selector option 在短窗口内持有 dangling
Transition pointer；#9 element/controller destructor只释放自身 vectors/deques/label，不解引用
borrow，所以 reset仍安全。resolver map在所有 owners之前清除，不留下可查询的 stale locator。

普通 `EmoteEngine` destructor 则按 #10 → #1反向销毁：#9 Selector先死，#8 Transition后死，
borrow lifetime自然覆盖。wind emitter比 spring deques更早删除，但 Spring/Chain destructor
不读取 `collisionCurve`；dangling borrow不被观察。

### 6.1 clear/range-destructor 完整证据

| element family | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| #1 Simple spring | `0x6800DC` / 112 | `0x5629A4` / 24 | `0x1001B7164` / 76 | `0x1B6D4E` / 81 |
| #2/#3 Chain | `0x68029C` / 45 | `0x562B80` / 24 | `0x1001B7298` / 78 | `0x1B6E2A` / 73 |
| #4 Eye | `0x6804C4` / 91 | `0x562D7C` / 24 | `0x1001B73DC` / 78 | `0x1B6EEC` / 77 |
| #5 Eyebrow | `0x680750` / 91 | `0x562F14` / 24 | `0x1001B7514` / 78 | `0x1B6FC0` / 77 |
| #6 Mouth | `0x6809DC` / 90 | `0x5630AC` / 24 | `0x1001B764C` / 75 | `0x1B7094` / 82 |
| #7 Clamp | `0x680BD4` / 82 | `0x563258` / 24 | `0x1001B777C` / 70 | `0x1B7178` / 74 |
| #8 Transition | `0x680D1C` / 91 | `0x5633C0` / 24 | `0x1001B78A0` / 73 | `0x1B723C` / 80 |
| #9 Selector | `0x680F14` / 53 | `0x563560` / 24 | `0x1001B79C8` / 80 | `0x1B7318` / 80 |
| #10 Loop | `0x6810A4` / 95 | `0x563718` / 24 | `0x1001B7B5C` / 70 | `0x1B7432` / 70 |

全部 helper fresh decompile/disassemble，完整 cursor无截断。Android armv7 的 24 指令函数是
复用 range-worker 的 thin clear lowering；不能据其短小误判为 trivial element。

## 7. nested controller 普通析构

四端共同顺序：

- Blink/Eyebrow：resolver output rows（逐 row销毁 path deque）→ node rows（逐 row释放
  vector buffer）→ edge vector → secondary pair deque → primary 12B deque；
- Mouth：primary 12B deque；
- Transition Var：destructor body依次 `delete[] current/start/target`，随后 primary 20B deque；
- Selector：option vector（borrowed pointers不 delete）→ command deque；
- Loop：key vector；
- Simple/Chain spring：trivial ordinary destructor + scalar delete。

resolver 运行时先 clear旧 candidate rows与 destination secondary track，再搜索；每个 emitted
candidate拥有自己的 path deque。选中项的 path从 back逐个迁到 destination并被消费，未选中
项继续由 outputRows拥有，直到下一次 resolve、controller destructor或 constructor unwind。

## 8. 本地映射与测试

| 参考语义 | 本地位置 | 结果 |
|---|---|---|
| 十种独立 element 与 owner/borrow字段 | `cpp/plugins/motionplayer/EmoteEngine.h:219` | 匹配 |
| 十 deque declaration order | `cpp/plugins/motionplayer/EmoteEngine.h:725` | 匹配 |
| metadata reset #1→#10 | `cpp/plugins/motionplayer/EmoteEngine.cpp:946` | 匹配 |
| Eye/Eyebrow/Mouth builders | `cpp/plugins/motionplayer/EmoteEngine.cpp:1771` | 匹配 |
| Selector/Transition builders | `cpp/plugins/motionplayer/EmoteEngine.cpp:1896` | 匹配 |
| Loop/Clamp builders | `cpp/plugins/motionplayer/EmoteEngine.cpp:2038` | 匹配 |
| Simple/Chain spring builders | `cpp/plugins/motionplayer/EmoteEngine.cpp:2328` | 匹配 |
| Blink nested resolver owners | `cpp/plugins/motionplayer/EmoteBlinkController.h:35` | 匹配 |
| Eyebrow nested resolver owners | `cpp/plugins/motionplayer/EmoteEyebrowController.h` | 匹配 |
| Mesh resolver candidate/path ownership | `cpp/plugins/motionplayer/EmoteMeshResolver.h:30` | 匹配 |
| Selector borrowed option vector | `cpp/plugins/motionplayer/EmoteSelectorController.h:24` | 匹配 |
| Chain borrowed wind pointer | `cpp/plugins/motionplayer/EmoteSpring.h:85` | 匹配 |

现有 unit 源覆盖 spring/chain element move ownership、九个 builders、duplicate/empty/sparse
resolver、Selector first-match borrow/flag、targets empty、reset/dtor containers，主要位于
`tests/unit-tests/plugins/motionplayer-dll.cpp:11427`、`:11872`、`:12162`、`:12329`、
`:12355`、`:20586`、`:28324` 与 `:30381`。正式 test binary运行仍由 `MP-V07/V08` 跟踪，
不能把源码断言当作已运行验证。

本任务没有 semantic C++ edit。四端 builder、controller constructor、deque clear helper与
metadata reset已从 `sub_*`/`*_guess` 统一为 task-local确定性 IDB 名称；source `_guess`
全仓审计仍由独立 final audit负责。

## 9. Disposition

| 观察项 | disposition |
|---|---|
| 十个 deque element 形状不同 | source-level类型差异；不得统一成 void-pointer record |
| constructor成功后、emplace前 raw pointee泄漏 | 四端共同 builder历史边界；保留 |
| owner publication后 label/map异常保留 partial entry | 四端共同 partial commit；保留 |
| Loop resize/decode处于 raw-owner窗口 | 四端共同扩展 leak frontier；保留 |
| Selector匹配副作用早于自身 owner publication | 四端共同 partial commit；保留 |
| Selector options借用Transition pointee | borrowed；owner始终在 deque #8 |
| Selector targets无 writer、保持空 | dormant borrowed vector；保留读路径，不合成数据 |
| reset #8先于#9造成短暂 dangling borrow | 不解引用的安全 teardown边界；保留 source order |
| normal dtor #9先于#8 | borrow lifetime覆盖 |
| chain collisionCurve借用wind | 每 step刷新；dtor不解引用 |
| `{type,index}`保存稀疏 metadata index | non-owning unchecked resolver；保留 malformed边界 |
| duplicate/empty key覆盖 resolver但不删旧 owner | 四端共同 map publication语义 |
| Android/iOS deque block与header差异 | libstdc++/libc++ ABI边界；不在 portable source硬编码 |

`MP-L08` task-local 静态缺口为零；全 Engine serialize/unserialize临时 owner、global/static
lifetime与跨对象 AddRef/Release总审计继续由 `MP-L14`、`MP-L15`、`MP-L16` 独立跟踪。
