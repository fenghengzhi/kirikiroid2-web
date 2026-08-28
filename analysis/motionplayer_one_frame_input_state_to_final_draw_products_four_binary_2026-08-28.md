# MotionPlayer 一帧 input state → final draw calls 数据产品快照四参考联合恢复

日期：2026-08-28  
原始任务：`MP-G24`

## 1. 结论

参考实现中的“一帧”不是一个原子`updateAndDraw()`函数，而是两个脚本可见入口之间的有序协议：

```text
progress/frameProgress(input dt)
  → Engine timelines/controllers/variables
  → Player frame/timeline/slot state
  → accumulated transforms + geometry + bounds
  → pending script callbacks

draw(target)                         # 独立的后续调用
  → target route
  → PreparedRenderItem main/aux products
  → route-dependent projection
  → Layer commands / private queue / D3D batches
  → TJS Layer primitives or OperateTriangles
```

两个入口之间没有generation或transaction。progress尾部的script callback可以reload、改变量或改变
target相关状态；随后draw消费的是callback返回后的live Player对象，但不会自动重跑已经完成的
updateLayers/calcBounds。异常同样保留之前阶段的partial commit。

四端一帧管线还有两个不能被“统一化”的关键顺序：

1. `EmoteEngine::progress`把原始dt交给Player完整执行
   `frameProgress → updateLayers → calcBounds → events`以后，才step outer-force、hair/parts和bust
   physics tail。这个tail不会回写本帧已经生成的Player geometry，通常影响下一帧bind/update。
2. ordinary target在Player sticky `useD3D`为真时，prepare后直接进入shared D3D adaptor；该分支
   **不调用camera/stereovision projection**。direct D3D、direct SLA和ordinary Canvas都会projection。

本地实现已经对应这两处顺序和所有阶段数据产品；未发现需要修改production语义的差异。本任务把
此前分散的纵向slice首次拼成一条带分支、owner和partial-commit边界的端到端frame contract。

## 2. 本轮 fresh 四端证据总量

本轮使用原生`mcp__idalib__*`对80个独立函数范围重新执行decompile、完整disassembly和
`xrefs_to`审计。所有disassembly均为`truncated=false`，所有decompile均无error。

| 平台 | 独立范围 | 完整指令 | `xrefs_to` | IDB 更新 |
|---|---:|---:|---:|---|
| Android arm64 | 20 | 14,775 | 52 | 20条任务注释、1个书签 |
| Android armv7 | 20 | 11,481 | 45 | 20条任务注释、1个书签 |
| iOS arm64 | 20 | 9,763 | 48 | 20条任务注释、1个书签 |
| iOS armv7 | 20 | 13,670 | 46 | 20条任务注释、1个书签 |
| 合计 | 80 | 49,689 | 191 | 80条注释、4个书签；四库原位保存 |

Android arm64的Engine progress是函数chunk布局：语义入口`0x67A3F8`由IDA归到包含wrapper/chunk的
`0x67EC94`范围，本轮完整范围为309条；没有为制造独立函数而创建重叠边界。

## 3. frame/update半帧的四端根

| 数据阶段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Engine progress | chunk `0x67A3F8` / range 309 | `0x55FEF0`，95 | `0x1001B4304`，89 | `0x1B3E10`，104 |
| Player frameProgress | `0x6BE44C`，278 | `0x58A63A`，240 | `0x100113B50`，197 | `0x111556`，238 |
| updateLayers dispatcher | `0x6B871C`，685 | `0x5856E0`，764 | `0x10010E544`，719 | `0x10BE5C`，821 |
| vertex/mesh product | `0x6B98D0`，1,265 | `0x5866F8`，1,108 | `0x10010F6AC`，961 | `0x10CE30`，1,297 |
| calcBounds | `0x6C10E4`，480 | `0x58BE38`，402 | `0x100115C68`，332 | `0x11354C`，433 |
| pending-event tail | `0x6C1870`，118 | `0x58C3A8`，90 | `0x10011622C`，97 | `0x113B64`，145 |
| Player draw router | `0x6D3398`，371 | `0x597864`，293 | `0x100123C84`，270 | `0x122F28`，423 |
| prepare/sort | `0x6D2544`，61 | `0x596DF0`，58 | `0x100122F68`，45 | `0x121FDC`，79 |
| recursive prepared builder | `0x6BF714`，1,507 | `0x58B178`，944 | `0x1001148F8`，820 | `0x1123D8`，1,034 |
| camera/stereo projection | `0x6D2644`，253 | `0x596EB0`，327 | `0x100123038`，228 | `0x1220F0`，335 |

## 4. renderer/sink半帧的四端根

| 分支/产品 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| build render commands | `0x6C2208`，1,766 | `0x58C7C4`，1,348 | `0x1001167BC`，1,083 | `0x114118`，1,582 |
| Canvas sink | `0x6C4820`，2,363 | `0x58E2CC`，1,891 | `0x1001186E0`，1,531 | `0x11653C`，2,155 |
| ordinary post-draw | `0x6CBBB8`，88 | `0x59327C`，57 | `0x10011E6CC`，59 | `0x11CF20`，90 |
| direct D3D coordinator | `0x6D2F70`，54 | `0x59761C`，45 | `0x100123844`，48 | `0x122AAC`，88 |
| direct SLA coordinator | `0x6D2A38`，184 | `0x597328`，118 | `0x1001233C8`，121 | `0x12257C`，207 |
| accurate SLA sink | `0x6C7088`，2,051 | `0x590468`，1,676 | `0x10011A9E8`，1,328 | `0x118D70`，1,955 |
| private GLL Draw_GPU | `0x6DA94C`，407 | `0x59BFB4`，420 | `0x10012A9B4`，416 | `0x129724`，621 |
| shared D3D deep sink | `0x6AB39C`，606 | `0x57D3DC`，655 | `0x100104450`，545 | `0x101850`，888 |
| mesh/cell submit | `0x69AFE4`，1,829 | `0x575800`，871 | `0x1000F974C`，787 | `0xF685C`，1,035 |
| D3DAdaptor envelope | `0x6AB204`，100 | `0x57D2CC`，79 | `0x100104284`，87 | `0x101680`，140 |

## 5. 帧输入快照

progress入口观察的不是单个dt，还包括多组persistent state：

```text
Engine
  active timeline labels + timeline state map
  eye/eyebrow/mouth/selector/transition/loop controller deques
  direct root controllers: position/scale/color/angle
  outer-force controllers + wind emitter + physics node deques
  variable-value map + mirror/clamp metadata + dirty/directEdit

Player
  playback firstFrame/queue/allplaying/sync/completion/loop cursors
  variable tracks and node dual ClipSlots
  parameter maps and externally bound values
  node deque + source state + previous accumulated state
  camera/root/draw transform and renderer flags

Call inputs
  original frame dt
  later draw target Variant and its exposed native class IDs
```

`EmotePlayer.progress(milliseconds)`先用binary64独立乘60、再除1000得到frame-unit dt；
`EmotePlayer.frameProgress`直接把frame-unit dt传给同一Engine root。0、负数、NaN和infinity不由facade
统一拦截。

## 6. Engine progress产品

Engine保留`originalDt`，另用working dt执行：

1. `preProgress(false, originalDt)`推进active timeline windows、loop、blend和track controllers；
2. 当`remaining > 0 || dirty`时以最多1.1的double slice循环；每slice缩窄一次float；
3. 严格按eye、eyebrow、mouth、selector、transition、loop六个deque step并写
   `_variableValues`；
4. step四个root controller，再step live wind emitter；
5. 退出slice loop后，对每个variable加入timeline contribution、应用mirror，调用
   `Player.bindParameterValue`；
6. 应用clamp controls；
7. 只把`originalDt`一次性交给`Player.progressFrames(nullptr, originalDt)`；
8. Player完整bridge返回后，若`originalDt != 0 && !directEdit`，才step outer-force controllers和
   hair/parts/bust spring。

阶段产品快照：

| 产品 | owner/容器 | 本帧consumer |
|---|---|---|
| timeline current time/cursors/blend | Engine timeline map/vector | variable accumulation、下一帧 |
| controller output values | Engine unordered map | Player parameter binder |
| dirty/root direct state | Engine fields/controllers | 当前slice与下帧 |
| bound parameter values | Player HM/query maps | Player frameProgress/updateLayers |
| physics tail新状态 | Engine owning controllers/deques | 下一次bind/update；不回写已完成的本帧geometry |

unordered-map物理枚举顺序随STL变化，但每个label独立写入；不能把它转成一个有顺序副作用的统一
vector loop。

## 7. Player progress bridge产品

bridge共同顺序固定：

```text
currentDispatch = input raw pointer
frameProgress(originalDt)
updateLayers()
calcBounds()
dispatchPendingEvents(reload currentDispatch field)
currentDispatch = null                 # normal return only
```

Engine调用传入null；直接`Motion.Player.progress`脚本入口传入objthis。pending events为空时不触碰
dispatch；非空时dispatcher无条件AddRef，因此Engine facade路径产生pending event时保留原始null
dispatch崩溃边界。不能悄悄把它改成skip或从别处补receiver。

`frameProgress`提交：

- `_deltaTime = speedMul * dt`、frame/eval/clamped cursors；
- forward/reverse/loop/reseek后的tag/root/variable/node cursor；
- 每个variable/node两个slot的parsed/merged content与active index；
- source refresh、sync/completion bytes；
- pending sync/action vector前缀。

`updateLayers`随后消费slot/parameter state，依次产生：

- root/parent accumulated position、scale、angle、color、opacity和2×2 matrix；
- delta position、camera constraint、vertex/mesh、visibility、camera node、shape、nested motion、
  particle和anchor产品；
- `_needsInternalAssignImages`以及本帧source/mesh/visibility state；
- normal tail清node flags/dirty、parameter mode、queue/no-update bytes。

`calcBounds`最后从composite points、16-point transformed mesh或four corners生成floor/ceil node bounds和
Player aggregate bounds。事件callback发生在这些产品已经提交之后；callback改变timeline/transform
不会自动重算本帧geometry。

## 8. draw target路由矩阵

`Player::draw(target)`先做严格Object转换和D3D class-ID probe，再做第二次严格转换和SLA probe。
四条路线不是一条统一renderer：

| route | prepare | projection | command/backend | final product |
|---|---|---|---|---|
| direct D3DAdaptor target | 自己构造main/aux并prepare | **是** | adaptor envelope → deep D3D | private OpenGL triangle batches；sticky byte在prepare前置true |
| direct SeparateLayerAdaptor | 自己prepare | **是** | process-static accurate选择 | accurate Layer calls，或legacy private queue/Draw_GPU |
| ordinary target，sticky false | 顶层prepare | **是** | Canvas + ordinary post-draw | Layer operate/copy/mesh/mask；可选assignImages |
| ordinary target，sticky true | 顶层prepare | **否** | shared D3D adaptor + captureCanvas | unprojected prepared geometry进入D3D，再capture到target |

direct D3D/SLA helpers各自构造main/aux，顶层不会复用一份list。SLA backend选择的function-static在
prepare+projection成功以后首次初始化，此后进程级固定。legacy路线只有
`TVPWindowUpdateEventsDelivering == false`时立即`Update(false)`；否则queue已经发布但当前调用不产生
`Draw_GPU`，等待宿主Layer更新。

## 9. PreparedRenderItem产品快照

`prepareRenderItems`检查motion-content type tag，然后调用递归builder，最后只stable-sort main list。

builder从physical node/source state产生persistent node-owned item：

| item产品 | 来源 |
|---|---|
| ownerLabel、commandKey、layer IDs | node label / motion context / persistent IDs |
| sortKey | accumulated Z |
| commandCoord、matrix、origin | accumulated position/matrix + source/slot origin |
| packedColors、opacity、blend | accumulated inherited color/opacity + slot blend |
| corners、paintBox、viewport | vertex/bounds/clip products |
| sourceState pointer | node-owned SourceState borrow |
| parent/child pointers | visible ancestor、nested/stencil topology borrows |
| meshType/divisions/points | composite or Bezier processed/raw vectors |
| mainList entry | ordinary/flattened child render order |
| auxList entry | group/stencil composite producer order |

main/aux vector只拥有pointer buffer；item由MotionNode跨帧拥有和复用。字段逐项in-place覆盖，
exception可留下drawn=true、item半刷新或list前缀。stable sort仅重排main pointer slots；equal Z保持输入
顺序，NaN sortKey保留标准库strict-weak-order sharp boundary。

## 10. projection产品

执行projection的三个route对sorted main items原地修改：

```text
camera offset(float)
  → corners/composite/meshType1 mesh/paintBox/viewport
optional stereo(double intermediates)
  → corners/composite/meshType1 mesh
  → paintBox reset and floor/ceil rebuild
```

auxList不投影；command Bezier raw points不投影；viewport只translation。sticky shared-D3D route直接
绕过整个函数，保留builder产出的未投影corners/paintBox。这是route contract，不得在shared helper
中“补齐”。

## 11. Layer command产品

Canvas和accurate SLA需要把item转为Layer products。`buildRenderCommands`在target clip下：

- 计算paintBox/viewport交集，发布`rawFlag21`、float clip和toward-zero dirty rect；
- 惰性创建SeparateLayerAdaptor；
- 创建/复用leaf和composed Layers；
- 按aux group合并child paintBox、copy/mask并发布group clip；
- 保留active/retired ordered maps和异常partial commit。

Canvas item executor进一步输出两类TJS call：

- direct path：`operateAffine`、`operateBezierPatch`、`operateMesh`；
- buffered path：在`bufLayer`做`affineCopy/bezierPatchCopy/meshCopy`、祖先alpha mask，再
  `operateRect`到target。

Canvas正常尾调用无参数`setClip`；ordinary post-draw仅当needs flag为真才materialize internal/work
Layers并`assignImages(target)`。

accurate SLA则按layerId/payload复用Layer，必要时copy source、apply mask/debug，最后固定发布
`setPos/type/visible/opacity`并在normal tail清retired Layers。legacy SLA先把prepared items复制成
private Layer owning texture queue；`Draw_GPU`稍后消费queue。

## 12. D3D/GL最终draw-call产品

D3D deep sink先做stencil/clip prepass，再逐item读取live source descriptor和target callback，选择
render method并进入TriangleBatch。最终`OperateTriangles`产品可完整表示为：

```text
method pointer/name + uniform packedColor + optional alpha threshold
targetTexture + referenceTexture
integer clip rect
triangle count
sourceTexture
sourceVertices[triangleCount * 3]
destinationVertices[triangleCount * 3]
current stencil mask/write state
```

affine直接产生两个triangle。Bezier/composite mesh先经过公共helper：source row/column vectors、outer/
cell admission、selected cell list，再按每cell固定顺序展开：

```text
TL, TR, BL
TR, BL, BR
```

TriangleBatch key变化时flush；相同method/source/target/clip/color合并，key故意不含reference texture。
OperateTriangles抛异常时vector不clear，GL state/target不回滚。normal tail final flush后才EndStencil。

## 13. 一帧对象生命周期快照

```text
process lifetime
  method caches, private OpenGL manager, shared D3D adaptor,
  accurate-SLA backend choice, stencil/overflow statics

Engine lifetime
  timeline/controller deques, maps, wind/physics owners

Player lifetime
  node deque
    ├─ SourceState owners/borrows
    └─ one persistent PreparedRenderItem owner per node
  resource/source cache Variants
  persistent internal Layers / render adaptor maps

progress call lifetime
  controller outputs/slices, slot/frame temporaries,
  retained event dispatch/result/action Variants

draw call lifetime
  main/aux pointer-vector storage
  target/source/Layer accessors and Variants
  mesh/point arrays, payloads, TriangleBatch vectors

borrowed edges
  item → SourceState, item → parent/child item,
  lists → item, atlas texture raw pointer,
  factory owner/parent dispatch, target/source callbacks
```

draw返回时main/aux storage销毁，但persistent items不销毁。SLA maps/private queue/shared adaptor和
internal Layer products可跨帧；其clear/retire时机由各自route管理。

## 14. 异常、重入与partial commit时间线

没有一帧级rollback：

1. Engine controller/timeline writes先提交；后续binder/Player异常不恢复它们；
2. Player frame/slot writes先提交；updateLayers/calcBounds异常不回滚cursor；
3. geometry/bounds先提交；event callback异常不撤销本帧更新；
4. direct D3D probe先写sticky；prepare/render异常不清sticky；
5. builder/item/list逐字段发布；sort/projection/render异常保留persistent item变更；
6. projection原地逐点写；中途fault留下部分translated/projected item；
7. Layer/ordered-map/queue/GL calls逐项提交；异常只销毁live C++ owners；
8. BeginStencil后异常不补EndStencil，batch flush失败不清vectors，post-draw未到达则不补assign。

所有script/Layer/source callback都可能同步重入。borrowed node/item/source/texture/target pointer没有
generation或self-retain；reload/unload/clear可让当前consumer悬空。这些是四端共同边界，不得用全帧
snapshot或事务guard改变。

## 15. 本地逐行对照

- `cpp/plugins/motionplayer/EmotePlayer.cpp:515`：milliseconds到frame dt facade；
- `cpp/plugins/motionplayer/EmoteEngine.cpp:3817`：Engine一帧、binder、Player bridge和physics tail；
- `cpp/plugins/motionplayer/PlayerFrameProgress.cpp:915`、`:1292`：Player state machine和bridge；
- `cpp/plugins/motionplayer/PlayerUpdateLayers.cpp:11`：fixed update phase chain；
- `cpp/plugins/motionplayer/PlayerUpdateGeometry.cpp:153`：vertex/mesh products；
- `cpp/plugins/motionplayer/PlayerRenderItems.cpp:321`：bounds；
- `cpp/plugins/motionplayer/PlayerDrawDispatch.cpp:25`：四route draw router；
- `cpp/plugins/motionplayer/PlayerRenderItems.cpp:442`、`:931`、`:971`：item、sort、projection；
- `cpp/plugins/motionplayer/PlayerRenderExecute.cpp:396`：Layer command products；
- `cpp/plugins/motionplayer/PlayerRenderTargets.cpp:489`：sticky shared-D3D route；
- `cpp/plugins/motionplayer/PlayerRenderTargets.cpp:624`：accurate SLA；
- `cpp/plugins/motionplayer/PlayerRenderTargets.cpp:887`：direct D3D；
- `cpp/plugins/motionplayer/PlayerRenderTargets.cpp:1081`：Canvas；
- `cpp/plugins/motionplayer/PrivateMotionGLL.cpp:250`：legacy queue consumer；
- `cpp/plugins/motionplayer/MotionRenderBackend.cpp:507`、`:725`：mesh and batch final products。

没有发现production语义差异，故本任务不做运行代码修改。

## 16. 验证状态

本轮完成49,689条完整指令、191个`xrefs_to`、80条任务注释、4个书签和四库保存。既有unit资产按
阶段覆盖Engine progress/controller、Player playback/slots、update phase、vertices/bounds、draw route、
prepared item/sort/projection、Canvas/SLA/D3D/mesh与异常owner；本任务没有为已经覆盖的局部公式增加
重复测试。

coverage与163-ticket映射随后重生成并执行严格列数、重复ID和`git diff --check`检查。正式native
unit、Web Debug、frame trace/draw-call capture以及同输入跨四reference/Web的一帧differential仍归
`MP-V`验证任务；静态闭合不伪称这些运行已经通过。

`MP-G24`没有剩余task-local静态差异。
