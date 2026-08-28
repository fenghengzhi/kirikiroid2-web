# Player buildNodeTree 四参考二进制联合恢复

日期：2026-08-27

## 1. 证据范围与结论

本 slice 沿普通 motion initializer 的真实调用顺序，闭合以下连续路径：

```text
Player::buildNodeTree
  -> resetAndReleaseOldNodeTree
  -> motionContent.layer
  -> recursive raw-layer flatten
       -> MotionNode append/partial publication
       -> raw-label map operator[]
       -> ResourceManager.requireLayerId x2
       -> initializeNodeFromLayer
       -> children recursion
  -> type-12 stencil-composite mask post-link
```

四端共同源码结构已经明确：tree builder 先在旧树仍存活时构造 owning motion-content
accessor，随后 reset；递归构建使用深度优先 pre-order，把 constructor-created synthetic root
保留在 index 0。每个真实节点在读取 raw layer 数组元素之前就 append 到 deque 并发布 parent/
done partial state；raw label、两个 layer id、字段 initializer 和 children 递归再依次提交。整个
构建没有 transaction rollback。

本地 `NodeTree.cpp` 的主数据流、字段读取顺序、type-specific 初始化、type-3 child ownership、
duplicate label map 和 stencil post-link 基本一致。本轮 fresh 对照发现并已在证据固化后
修正三项本地残留：

1. `Player::buildNodeTree_guess` 插入了参考二进制不存在的 PRTDIAG/logo trace，且日志位于
   reset 与 `motionContent.layer` getter 之间，改变 re-entrant/throw frontier；
2. stencil post-pass 的 native upper bound 是进入 pass 时的 node-count snapshot，本地循环每次
   重读 `_nodes.size()`；
3. native initializer只保存解析后的 `MotionParameterEntry*`，本地还给诊断字段
   `parameterizeIndex` 赋值，并经 fallback helper重新解析。这一项的 producer可在本 slice
   纠正为共享 `selectParameterEntry` helper；诊断字段本身及所有消费 sidecar需在对应消费者
   fresh 审计后再完全删除。

以下证据与逐行比较均在任何本轮语义修改之前完成；第 15 节记录修改结果。

## 2. 四端函数映射与完整指令

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| build wrapper + post-link | `0x6B25D0`，320 | `0x581CC8`，176 | `0x1001097C8`，142 | `0x107060`，207 |
| recursive flatten | `0x6B1E4C`，397 | `0x5818B0`，230 | `0x100109328`，182 | `0x106BDC`，268 |
| initialize node fields | `0x6B1058`，776 | `0x580FA4`，504 | `0x100108720`，433 | `0x105E70`，739 |
| NodeLabelMap `operator[]` | `0x6B2498`，77 | `0x581C54`，44 | `0x100141740`，40 | `0x142844`，42 |
| child Player adaptor creator | `0x6EEB74`，92 | `0x58185C`，30 | `0x1001092A0`，29 | `0x106B08`，67 |

所有 20 个函数均 fresh decompile；完整 disassembly 分别覆盖
1662/984/826/1323 条独立指令，全部 cursor `done=true`。wrapper 只有 ordinary
`initNonEmoteMotion` 一个 code caller；recursive helper 只有 wrapper 和 self-recursion 两个
caller；node initializer 只有 recursive helper 一个 caller。label-map insertion helper只被该
递归构建路径调用。child adaptor helper另被 particle child producer复用，这与两个内部 child
producer共享 `CreateAdaptor(native,false,false)` 的源码结构一致。

已闭合的依赖：

- old-tree reset：`analysis/motionplayer_motionnode_prepared_item_deque_lifetime_four_binary_2026-08-27.md`；
- parameter vector、选择和 purge：
  `analysis/motionplayer_player_parameter_table_pipeline_four_binary_2026-08-27.md`；
- raw-label resolver：
  `analysis/motionplayer_layergetter_player_producers_and_borrowed_lifetime_four_binary_2026-08-26.md`；
- Player constructor、root invariant 与 child member owners：现有 Player lifetime/constructor
  coverage slices。

## 3. 33 个宽属性名的原始字节验证

IDA 把多项 UTF-16 literal 渲染成 `"e"`、`"p"`、`"j"`、`"f"`、`"m"` 等单字母。
本轮按 `ida-search-string` 的宽字符串流程，在四库对下列 33 个完整 UTF-16LE+terminator byte
pattern逐项搜索，所有 132 个分页 cursor 都完成且每项至少有一个命中：

```text
layer, label, parameterize, coordinate, joinTarget, groundCorrection,
frameList, inheritMask, transformOrder, meshTransform, meshSyncChildMask,
meshDivision, meshCombine, type, stencilType, objTriPriority, shape,
motionIndependentLayerInherit, particle, particleMaxNum, particleAccelRatio,
particleInheritAngle, particleInheritVelocity, particleFlyDirection,
particleApplyZoomToVelocity, particleDeleteOutsideScreen, particleTriVolume,
particleMotionList, anchor, stencilCompositeMaskLayerList, emoteEdit,
children, requireLayerId
```

各平台 node-field 专用连续 literal block以 `emoteEdit` 开始、以
`stencilCompositeMaskLayerList` 结束：

| 平台 | block start | block end | `requireLayerId` |
|---|---:|---:|---:|
| Android arm64 | `0x14D5D80` | `0x14D6098` | `0x14D5A54` |
| Android armv7 | `0x581628`/`0xD857E8` | `0xD85A80` | `0xD85574` |
| iOS arm64 | `0x10195C28E` | `0x10195C5BA` | `0x10195BF02` |
| iOS armv7 | `0x174E5F2` | `0x174E91E` | `0x174E266` |

Android armv7 的少数短 literal被编译器放入 code-adjacent literal pool；这只是 placement差异，
不是属性名或 getter hint 差异。

## 4. wrapper 的 owner 与 reset 边界

共同伪代码：

```text
buildNodeTree():
    motionOwner = strict owning accessor(copy(motionContentVariant))
    resetAndReleaseOldNodeTree()

    rawLayers = motionOwner.GetValue("layer")
    buildRecursive(rawLayers, parentIndex=0)

    nodeCount = nodes.size()                  // snapshot once
    for index in [1, nodeCount):
        node = nodes[index]
        if node.type != 12 or !(node.stencilType & 4):
            continue
        resolveStencilMaskLinks(node)
```

motion accessor在 reset 前构造。若 `_motionContentVariant` 非 Object，转换先抛，旧树完全不动。
一旦 accessor构造成功，reset 会释放旧 layer ids、销毁非根节点并清 raw-label map；随后
`layer` getter或递归任一点抛出，都只留下新树的 partial prefix，旧树不会恢复。

accessor owner贯穿 reset、layer getter、完整递归和 post-link，直到函数退出才 Release。脚本在
reset callback或 layer callback中替换 Player canonical motion Variant，不会重定向当前 build。

参考四端在 reset 与 `layer` getter之间没有日志、路径匹配或额外 callback。当前本地 PRTDIAG
恰好插在该边界，必须删除。

## 5. recursive flatten 的发布顺序

共同伪代码：

```text
buildRecursive(rawLayers, parentIndex):
    layers = strict owning accessor(copy(rawLayers))
    count = layers.Count()                    // snapshot once

    // New snapshot per recursion level, before sibling loop.
    rmOwner = copy(player.resourceManager).AsObject()

    for arrayIndex in [0, count):
        thisIndex = nodes.size()
        nodes.emplace_back()                  // value constructor first
        node = nodes.back()
        node.parentIndex = parentIndex
        node.slot0.done = true
        node.slot1.done = true

        rawLayer = layers[arrayIndex]
        layer = strict owning accessor(copy(rawLayer))

        rawMapLabel = layer.GetValue("label") // first label read
        nodeLabelMap[rawMapLabel] = thisIndex

        layerIdResult = Void
        node.layerId1 = rmOwner.requireLayerId(result=layerIdResult).AsInteger()
        node.layerId2 = rmOwner.requireLayerId(result=layerIdResult).AsInteger()

        initializeNodeFields(player, node, rawLayer)

        rawChildren = layer.GetValue("children")
        buildRecursive(rawChildren, thisIndex)
```

节点 append 和 parent/done stores 都早于 numeric layer lookup。因此以下异常都会留下至少一个
已构造的 partial node：numeric getter失败、raw layer不是 Object、label getter、map allocation、
layer-id callback/conversion、任一字段 getter、children getter或任意后代失败。

`thisIndex` 是 append 前的 deque size，只写入 label map和后续 children parentIndex；四端
initializer没有把它写入一个 MotionNode `index` 成员。修改前本地 `node.index` 只服务非原生
诊断，不能被当成参考结构证据。

## 6. ResourceManager snapshot 和 layer-id 两阶段提交

每个 recursion invocation会从 live `player._resourceManager` 重新创建独立 dispatch owner，然后
在这一层所有 siblings之间复用。由此产生精确 re-entrant语义：

- sibling callback替换 Player canonical RM，不会改变同一层后续 siblings的 receiver；
- children recursion进入新 invocation，会读取替换后的 live canonical RM，因此可以换 receiver；
- parent recursion返回后仍恢复使用 parent层原 owner。

`requireLayerId` 两次都使用 `flags=0`、同一 hint、`numparams=0`、`objthis=rmOwner`，并复用同一个
Variant result。FuncCall普通失败码被忽略，随后仍执行 `AsInteger`。第一项已写后第二次调用/
转换抛出时，只提交 layerId1。result owner一直活到该 node完成 children recursion后才析构。

RM Variant为非 Object时在 recursion入口的 AsObject边界抛；Object转换得到 null时后续无 guard
调用保留原始 crash边界。

## 7. raw-label map 的 duplicate 和 owner语义

四端 helper共同实现 `std::map<ttstr,int,ttstr_utf16_less>::operator[]`：

- lower_bound查找使用 UTF-16 code-unit lexicographic comparator；null backing排在非 null前；
- miss时分配节点、CopyRef key并 value-initialize mapped int为0；
- caller随后写 `thisIndex`；
- duplicate key不分配新节点，只覆盖已有 mapped index，保留第一次插入的 key owner；
- 空 key合法；不做去重日志或节点回滚。

raw-map label与 `node.layerName` 是两个独立 property read。第一个发生在 layer-id申请前，第二个
发生在 node initializer内部、emoteEdit之后。side-effecting getter可以让 map key与节点持有
的 layerName不同；本地当前实现保留了这种双读。

## 8. node-field initializer 的固定读取顺序

共同顺序：

```text
if HasValue("emoteEdit", hint):
    emoteEditVariant = GetValue("emoteEdit", hint)
else:
    emoteEditVariant.Clear()

layerName = GetValue("label", hint)
parameterEntry = selectParameterEntry(GetValue("parameterize", hint))
coordinateMode = GetValue("coordinate", hint)
joinTarget = GetValue("joinTarget", hint)
groundCorrection = GetValue("groundCorrection", hint)
frameListVariant = GetValue("frameList", hint)
inheritFlags = GetValue("inheritMask", hint)

transform = owning accessor(GetValue("transformOrder", hint))
for index in [0,4):
    transformOrder[index] = transform.getIntValue(index, default=0)

meshType = GetValue("meshTransform")
if meshType != 0:
    meshFlags = GetValue("meshSyncChildMask")
    meshDivision = GetValue("meshDivision")
    if HasValue("meshCombine"):
        meshCombine = GetValue("meshCombine")

nodeType = GetValue("type")
if HasValue("stencilType"):
    stencilType = GetValue("stencilType")
else:
    stencilType = 0

switch nodeType: ...
```

`transformOrder` 每个 present index先做 MUSTEXIST probe再普通 read；missing写默认0。四个
index依次提交，任一异常保留前缀。meshType为0时不触碰三个 mesh-specific字段，依赖
MotionNode value constructor的初始状态。

四端 native parameter selector只返回 pointer：non-Integer -> null；Integer先窄化为 unsigned，
负数/越界抛 exact `"parameter id out of range."`；命中返回参数 vector 元素地址。initializer
没有第二个 store保存 index。Android arm64把 selector内联，另三端调用与 ordinary initializer
共用的 helper。

## 9. type-specific 初始化

### 9.1 type 0 / 1 / 6 / 9 / 12

- type 0：读取 `objTriPriority`；
- type 1：读取 `shape`；
- type 6：只把 emitter-active byte写 false；
- type 9：读取 `anchor`；
- type 12：retain `stencilCompositeMaskLayerList` Variant，供 build尾 post-link。

未列出的 type不执行额外读取。

### 9.2 type 3 child Player

共同顺序：

```text
if live player.preview:
    node.stencilType &= ~4
node.meshType = 0

child = new Player(copy(live player.resourceManager))
child.rootPlayer = player.rootPlayer           // non-owning
child.parentPlayer = &player                   // non-owning

independent = false
if layer.HasValue("motionIndependentLayerInherit"):
    independent = layer.GetValue(...)
if child.independentLayerInherit != independent:
    child.root.delta.dirty = true
    child.independentLayerInherit = independent

child.type3RootTransformAlreadyPropagated = true
child.findMotionContextVariant = player.findMotionContextVariant
child.root.coordinateMode = node.coordinateMode
child.setZFactor(player.zFactor)
child.root.transformOrder = node.transformOrder

dispatch = PlayerAdaptor.CreateAdaptor(child, sticky=false, throw=false)
node.childPlayerVariant = Variant(dispatch, dispatch) if dispatch else Void
```

preview byte在所有前置 property callbacks后才读取，不是 build-entry snapshot。CreateAdaptor
发生在 child完全链接/初始化之后。class object缺失、CreateNew普通失败或 null result时返回
null且新 child泄漏；fresh shell创建成功但 native adaptor property lookup普通失败时返回非 null
Object shell，shell内 native slot为null，原 child仍泄漏。四端均保留这两个 malformed boundary。

若 adaptor成功，Variant构造的 AddRef/Release序列最终给 node留一个 owning dispatch；old-tree
reset通过该 Variant Invalidate child，普通 Variant销毁再释放 adaptor/native。

### 9.3 type 4 particle

type 4先写 identity previous matrix、零 angle/timer、false emitter flag，再按顺序读取：

```text
particle
particleMaxNum
particleAccelRatio
particleInheritAngle
particleInheritVelocity
particleFlyDirection
particleApplyZoomToVelocity
particleDeleteOutsideScreen
particleTriVolume
particleMotionList
```

最后创建 fresh TJS Array并存入 particle Array Variant。任一 property异常不会创建 Array，且保留
此前 scalar/Variant前缀。Array创建返回 null时结果保持 Void；没有 fallback容器。

## 10. children递归的 owner栈

每个 node在 children getter和递归期间同时持有：当前 recursion的 layers accessor与 RM dispatch、
raw layer Variant、layer accessor、raw-map label owner、复用的 layer-id result Variant、children
Variant，以及 initializer中仍未离开作用域的临时 owner。子 recursion再叠加自己的 layers/RM
owners。

正常路径按 children owner、layer-id result、raw label、layer accessor、raw layer Variant的逆序
释放当前 node locals。异常 unwind遵循同样的已构造-owner逆序；deque/map/layer-id等 persistent
发布不回滚。四端 ABI只在 AArch64 DWARF、ARM EHABI/SjLj landing-pad形状上不同，源码 owner
前沿一致。

## 11. stencil-composite post-link

共同伪代码：

```text
nodeCount = nodes.size()
for index = 1; index < nodeCount; ++index:
    node = nodes[index]
    if node.type != 12 or !(node.stencilType & 4):
        continue

    maskList = owning accessor(copy(node.maskLayerListVariant))
    maskCount = maskList.Count()
    for maskIndex in [0, maskCount):
        label = maskList[maskIndex].AsString()
        target = findNodeByRawLabel(label, recursive=false)
        if target == null:
            continue
        if target.type == 0 or target.type == 3:
            node.maskNodes.push_back(target)
            target.maskReferenced = true
```

nodeCount与每个 maskCount都只读取一次。duplicate mask label会追加重复 pointer；不去重。map miss
和其他 target type跳过。push成功后才写 target flag，所以 vector allocation抛出时 flag仍旧值；
后续 label抛出时此前 pointer/flag前缀保留。maskNodes仅借用 deque元素地址，deque在正常 motion
生命周期内不再增长；old-tree suffix erase负责结束这些 aliases。

## 12. ABI差异但无源码语义差异

| 项 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| MotionNode natural stride | 2632 | 2272 | 2648 | 2228 |
| parameter record stride | 56 | 48 | 56 | 44 |
| child Player allocation | 0x568 | 0x3B0 | 0x4B8 | 0x348 |

Android使用 libstdc++ deque/map布局，iOS使用各自 libc++布局；A64 Android的 deque size由
iterator difference公式（其中自然包含 `node_difference - 1`）计算，不能把 Hex-Rays表达式的
`-1`误判成少处理最后一个节点。四端 post-link实际都遍历 `[1, sizeSnapshot)`。

Android arm64内联 parameter selector且内联较多 CreateAdaptor generic body；另外三端保留小 wrapper。
iOS armv7的较大指令数主要来自 SjLj register/unregister/cleanup，不代表额外属性或分支。

## 13. 修改前本地逐行对照

匹配项：

- `NodeTree.cpp:25` retained Object dispatch的 Variant-copy -> AsObject -> Variant-destroy顺序；
- `NodeTree.cpp:35` ignored FuncCall status、共享 result和 Integer conversion；
- `NodeTree.cpp:47` type-3 child link/state/adaptor/leak边界；
- `NodeTree.cpp:82` node field property顺序和 type switch；
- `NodeTree.cpp:252` Count/RM snapshot、append-first、label双读、layer-id两阶段提交和children递归；
- `NodeTree.cpp:304` root `layer` owner与 stencil post-link type gates；
- `internal/player_containers.h:68` NodeLabelMap tree/UTF16 comparator；
- `PlayerMotionLoad.cpp:173` accessor-before-reset骨架。

待修项：

1. `PlayerMotionLoad.cpp:174..239` 的 PRTDIAG/logo trace完全无四端对应，并在原生敏感边界插入
   callbacks；应把 wrapper恢复为 accessor -> reset -> detail build三步。
2. `NodeTree.cpp:312` 的 post-link outer loop重读 `_nodes.size()`；应先 snapshot。
3. `NodeTree.cpp:103..113` 先写 `parameterizeIndex`再由 `resolveNodeParameterEntry`取 pointer；
   应恢复共享 selector直接返回 pointer，普通 initializer也复用它。`parameterizeIndex`字段本身
   暂不在本 slice删除，因为它仍被其他未完成 fresh审计的诊断/consumer源文件引用。

这三项修改都只在本报告和上述四端 fresh map/decompile/disassembly之后进行。

## 14. 验证与下一依赖

现有测试已经覆盖 live preview时点、transformOrder双读/default、全部 named hint、retained
ResourceManager callback owner、ignored requireLayerId failure和旧树 reset边界。修改后将执行 strict
coverage列检查、`git diff --check`、可用脚本语法检查；当前环境仍缺 CMake/Emscripten正式
工具链，因此不能声称 unit/Web build通过。

buildNodeTree完成后，ordinary initializer的下一真实依赖是 `initVariables`。它构建
VariableLabelScope deque，并把刚完成的 node tree/parameter state交给随后 frame/update路径。

## 15. 证据后实施结果

在完成本报告第 2 至 13 节的四端 map、fresh decompile/disassembly、共同伪代码、ABI差异与本地
逐行对照后，已经实施：

1. `PlayerMotionLoad.cpp` 删除完整 buildNodeTree PRTDIAG/logo-trace sidecar及其仅存 helper/include，
   wrapper恢复为 `own accessor -> reset -> detail build`；
2. `NodeTree.cpp` 的 stencil post-link先把 deque size窄化为 native `int` snapshot，再遍历
   `[1,nodeCount)`；
3. 新增四端源码形状一致的 `internal::selectParameterEntry_guess`，ordinary initializer与node
   initializer共用；node producer不再写诊断 `parameterizeIndex`；
4. 增加 selector测试，覆盖 non-Integer null、index 0 pointer alias、negative unsigned-narrow和
   end-index exact error。

运行时正常构建路径现在只把 `MotionParameterEntry*`写入 node。后续parameter-pointer consumer
slice已fresh审计motion-sub与particle-emitter全部use-site，并删除
`MotionNode::parameterizeIndex`与`resolveNodeParameterEntry` fallback。后续 motion-sub、phase2/root、
shape-AABB 与 calcBounds 各自的 fresh 四端完整审计又关闭了所有 diagnostic ordinal 消费者，因而
删除 `MotionNode::index` 以及本函数 append 后的冗余赋值；`thisIndex` 本地变量仍严格服务 label map
和 children parentIndex。

四库已写入五个 domain names、函数注释和 build-root bookmarks，并原位保存 IDB。
