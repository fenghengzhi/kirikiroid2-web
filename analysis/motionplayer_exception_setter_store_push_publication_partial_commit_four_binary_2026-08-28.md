# MotionPlayer 异常下 setter/store/push/publication 部分提交四参考横向审计

日期：2026-08-28  
原始任务：`MP-B06`

## 1. 结论

四个参考二进制共同证明，motionplayer几乎没有跨多个setter/store/push/publication的事务或rollback。
异常处理只负责释放当时仍由栈临时量持有的owner；已经写入native field、脚本对象、容器节点、
Layer/GPU状态或全局注册状态的前缀通常保留。

必须区分四层保证：

1. 单个Variant/string assignment自身先取得新owner再释放旧owner，赋值成功后slot完整；
2. 单个标准容器insert在allocation/constructor失败时按对应STL实现清理candidate，但函数中更早的
   clear/store/push/Hint publication不回滚；
3. 多字段native object和脚本Dictionary/Array按源码顺序逐项提交，后续异常留下prefix；
4. 外部dispatch/GPU/Layer调用一旦发生，其副作用不由motionplayer补偿，后续异常也不恢复。

failed TJS status与抛异常也不能合并：很多setter/FuncCall忽略普通HRESULT并继续，只有真正的C++/TJS
异常进入unwind。本轮没有发现production偏差；现有实现保持source-order mutations、RAII临时owner
cleanup和persistent residue。本任务无需production edit。

## 2. 本轮 fresh 四端证据

本轮用原生`mcp__idalib__*`对64个代表全部publication家族的独立函数范围重新执行decompile、
完整disassembly、strings/constants/callees和`xrefs_to/from`审计。所有decompile成功，所有
disassembly均`truncated=false`。

| 平台 | 独立范围 | 完整指令 | `xrefs_to` | `xrefs_from` | IDB 更新 |
|---|---:|---:|---:|---:|---|
| Android arm64 | 16 | 10,987 | 42 | 16 | 16条任务注释、1个书签 |
| Android armv7 | 16 | 6,443 | 25 | 16 | 16条任务注释、1个书签 |
| iOS arm64 | 16 | 5,221 | 33 | 16 | 16条任务注释、1个书签 |
| iOS armv7 | 16 | 8,129 | 26 | 16 | 16条任务注释、1个书签 |
| 合计 | 64 | 30,780 | 126 | 64 | 64条注释、4个书签；四库原位保存 |

## 3. 四端函数映射

| publication范围 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| one-shot internal Layer | `0x6CB57C`，398 | `0x592F7C`，212 | `0x10011E2BC`，178 | `0x11CAC8`，298 |
| ResourceManager load | `0x6A616C`，501 | `0x57B338`，246 | `0x1001012D8`，225 | `0xFE40C`，364 |
| ResourceManager findMotion | `0x6A72B4`，791 | `0x57B9F8`，262 | `0x100101E84`，255 | `0xFF11C`，396 |
| Player findSourceForNode | `0x691CC8`，1,191 | `0x570500`，676 | `0x1000F316C`，586 | `0xEF97C`，952 |
| recursive node build | `0x6B1E4C`，397 | `0x5818B0`，230 | `0x100109328`，182 | `0x106BDC`，268 |
| initVariables | `0x6CAB30`，430 | `0x592944`，215 | `0x10011D540`，196 | `0x11BF04`，275 |
| playMotionImpl | `0x6AF664`，459 | `0x580158`，281 | `0x100107540`，236 | `0x104AE8`，386 |
| Engine unserialize | `0x675424`，260 | `0x55CF3C`，159 | `0x1001B1130`，122 | `0x1B0B80`，228 |
| calcView output | `0x6CE908`，1,349 | `0x594958`，798 | `0x1001201CC`，613 | `0x11EED4`，977 |
| command-list serialization | `0x6D0E2C`，1,315 | `0x595FF0`，838 | `0x100121EB0`，596 | `0x120CF8`，1,032 |
| append prepared items | `0x6BF714`，1,507 | `0x58B178`，944 | `0x1001148F8`，820 | `0x1123D8`，1,034 |
| private GLL queue builder | `0x6DBB18`，761 | `0x59CB20`，671 | `0x10012B7D0`，465 | `0x12A304`，703 |
| D3D deep renderer | `0x6AB39C`，606 | `0x57D3DC`，655 | `0x100104450`，545 | `0x101850`，888 |
| D3D source getter/insert | `0x6EE440`，160 | `0x5AC518`，157 | `0x10014019C`，118 | `0x1414C0`，196 |
| SourceCache clear | `0x6A5818`所在combined range，763 | `0x57B018`，30 | `0x100100F10`，29 | `0xFE0D4`，29 |
| LoadModule commit boundary | `0x701DE8`，99 | `0x5BA8E8`，69 | `0x10029FDE4`，55 | `0x2A48FC`，103 |

Android arm64的command-list target解析到共享body而非8-byte facade；SourceCache clear位于与loadSource
合并的763指令范围。本轮均读取完整resolved范围，没有把forward或中间地址误作小函数。

## 4. 通用异常模型

共同source形状可写成：

```text
temporary owners = acquire/copy inputs
commit persistent field/container/external side effect A
commit B
call potentially throwing getter/setter/allocator/callback
commit C
...

on exception:
    destroy live temporaries in reverse construction order
    destroy unlinked STL candidates required by that ABI
    do not restore A or B
    do not execute C or later stages
    propagate exception
```

四端DWARF/LSDA、ARM EHABI和iOS armv7 SjLj landing-pad形状不同；差异决定cleanup codegen和临时
destructor表，不改变source-level commit顺序。没有任何catch-all把对象恢复到入口快照。

## 5. one-shot publication：primary/work Layer

internal Layer materializer的精确顺序是：

```text
if primary.Type != Void: return
owner = target.window
primaryCandidate = new Layer(owner,target)
persistentPrimary = primaryCandidate
primaryObject = strictObject(copy(persistentPrimary))
height = target.height or 0
width  = target.width or 0
primaryObject.setSize(width,height)
workCandidate = new Layer(owner,target)
persistentWork = workCandidate
workObject = strictObject(copy(persistentWork))
workObject.setSize(width,height)
```

primary publication早于strict conversion、尺寸读取、第一次setSize和work创建。任何后续异常都可留下
`primary != Void && work == Void`或两者已发布但仅一部分setSize成功。下次调用只看primary Type，
不会repair。这是最强的persistent residue例子；事务式“两个Layer都成功才commit”会偏离reference。

## 6. ResourceManager load与findMotion

load的阶段提交包括：placed path替换、PSB holder、sticky `_spec`、loaded map node/file owner和fresh
dispatch。`_spec`可在version检查以前从0变1/2；随后version/label/message构造抛异常不会恢复旧值。
map insertion只在transfer/validation后发生，但成功插入后fresh dispatch构造失败不会擦除cache。

findMotion的成功对象按以下层级物化：最终raw node owner、fresh PSBValueDispatch、fresh two-element
Array、actual matched module-key String和object element。Array/element emplace或key construction失败时，
当前栈owner按unwind释放；已经插入到persistent容器或已经发布到外部alias的前缀不被跨层回滚。
普通miss仍返回Void，不与异常的partial state合并。

## 7. SourceState逐字段提交

findSourceForNode有三条route，但都不是temporary完整对象后swap：

- spec 2/Win可先写`valid=true`，再读取icon metadata；后续strict getter失败留下valid和此前字段；
- spec 1先写`path=src`，KRKR helper/cache失败后generic fallback继续或异常，path保留；
- generic先清texture、调用script findSource，再写valid、width、height、origin、blank、clip和textureRect；
  任一getter/owner conversion异常留下已写prefix；
- callback普通failed status走显式invalid route，异常则直接unwind，二者不可互换。

本地对live `SourceState`原地写入，未用local candidate+swap，保持了这些观察点。

## 8. node tree、variable tracks与playback

recursive node build先`emplace_back` persistent MotionNode，再写parent/done/source/layer字段、插入
NodeLabelMap、递归children并执行post-link stencil/particle设置。property conversion、map allocation、
child递归或layer-id callback异常时，新node和更早siblings保留；tree不是all-or-nothing。

initVariables先clear旧track deque，再对每个item先append default track，随后依序读取label/value/
scope并构造secondary state。任一后续throw留下default或部分track，以及前面已完成entries；不会还原
clear前deque。

playback更广：label、motion result、array element、sync/playing flags、parameter containers、node tree、
variables和clock按阶段提交。buildNodeTree/initVariables异常可留下`syncWaiting=false`、`allplaying=true`
以及partial node/track状态。join map转移、parameter vector clear和selected pointer也没有transaction。

## 9. state restore

Engine unserialize按固定8组schema原地恢复live对象：controllers、selectors、springs、wind/scalars、
timeline/variable maps与子对象逐项写入。每个getter/index/Variant conversion或容器allocation都可能抛；
此前group和当前group的字段prefix保留，后续group不执行。temporary input owners被释放，但已恢复的
controller target/queue/scalar不回滚。

clone/assignState边界同理：new facade/shell owner的publication、raw clone owner泄漏和D3D listener attach
各有独立commit点，不能用一个copy-and-swap Engine snapshot替换。

## 10. calcView和command-list输出

calcView在创建任何output Dictionary之前已经执行cursor clamp、`frameProgress(0)`和`updateLayers()`；
因此output allocation/SetValue失败不会回滚Player时间、layers或bounds。之后outer viewParams和每个
node output按key顺序逐项写，mesh/clip/coord/color/matrix嵌套Array也逐项发布。

getCommandList先重建persistent prepared items，再创建fresh outer Array；每个item Dictionary依次写
key/id/src/coordinate/opacity/blend/coord/mtx/color/origin/clip/mesh等。Dictionary写完后才append到outer，
但persistent command/prepared state已经更新；较早已append items不会因later item失败而删除。

script对象若在failure前已通过外部alias暴露，partial Dictionary/Array仍可被观察；只靠局部RAII释放
不能推断外部对象状态回滚。

## 11. render item、private queue与GPU side effects

appendPreparedRenderItems会在late main/aux list append以前更新node-owned persistent item、`drawn`、
source/geometry/mesh fields。后续source getter、recursive child、allocation或list append失败可留下
`drawn=true`但当前list没有对应item，或mesh vector只写前缀。

Private GLL builder先修改stencil/clip/leafLayer/renderLayerId/rawFlag20，再clear native queue并逐item
重建。`requireLayerId`、source resolver、texture owner或deque/vector增长异常都保留前面item mutation
和已入队prefix；不会恢复旧queue。

D3D deep renderer的flush、stencil begin/end、selector/method发布和OperateTriangles调用具有外部状态。
draw callback抛异常时已提交GPU/target state、batch字段和texture refs不由motionplayer补偿；normal tail
的clear/EndStencil也不会执行。

## 12. cache/container操作的局部保证

D3D source getter把factory raw ref、intrusive holder、map insertion和caller publication分开。部分ABI
会先分配candidate再查duplicate，部分先查再分配；单次emplace失败清理unlinked candidate。但若map
node已经link后续owner/caller step抛异常，node保留，不做erase。

SourceCache clear逐Layer调用`Invalidate`，然后才清list/node和复位byte count。第N个callback抛异常
时前N-1个及当前已完成的外部invalidations保留，list仍含未处理tail，count不一定已复位。

unordered hash Hint publication通常早于node allocation；allocation失败也保留input backing的Hint。
这些例子说明“STL insert有strong guarantee”不等于外围motionplayer函数有transaction。

## 13. module注册commit

LoadModule先检查registered set，再找internal map，按Pre/Class/Post顺序调用所有registrar。只有全部
callback成功返回后才insert registered key。callback抛异常时set不commit，但先前registrar对全局
class/member表的副作用保留；重试会从完整module list开头再次执行，包括早先已成功过的callback。

这不是rollback，而是late commit + non-transactional callbacks。增加“loading”marker、跳过成功prefix
或异常后unregister都会改变reference重入与重试行为。

## 14. ignored HRESULT不触发unwind

很多Layer/TJS calls忽略普通status：setSize、setClip、SetValue、PropSet、copy/operate/fill/update等。
callee返回failed HRESULT但不throw时，caller继续后续publication；只有callee真正抛异常才停止。
故测试/实现必须分别注入：

- failed status without throw；
- failed status after writing result/side effect；
- thrown exception before side effect；
- thrown exception after side effect。

把前三者统一成`if(TJS_FAILED) throw`会移动commit frontier，属于语义偏差。

## 15. 本地与测试对照

本地实现按reference source order直接修改persistent对象，并只让局部owner使用RAII。重点对应：

- `PlayerRenderTargets.cpp`：primary/work one-shot publication；
- `ResourceManager.cpp`、`PlayerResource.cpp`：cache、find result和SourceState prefix；
- `NodeTree.cpp`、`PlayerMotionLoad.cpp`、`PlayerCore.cpp`：node/track/playback staged commit；
- `EmoteEngine.cpp`：serialize/restore group order；
- `PlayerLayerQuery.cpp`、`PlayerRenderItems.cpp`：output/prepared staged publication；
- `PrivateMotionGLL.cpp`、`MotionRenderBackend.cpp`：queue/GPU side effects；
- `SourceCache.cpp`、`D3DAdaptor.cpp`：invalidate/map node owner；
- `ncbind.cpp`：registrar late commit。

现有单元测试已经覆盖materializer one-shot residue、playback result/index prefix、initVariables partial
track、state restore prefix、calcView output owner、D3D source insertion owner、SourceCache invalidation和
TJS Array/Dictionary exception frontiers。其余allocator/callback故障注入统一由`MP-V02/MP-V07`汇总，
不重复制造fixture。

## 16. 最终判定

`MP-B06`没有剩余task-local静态差异。setter/store/push/publication之间的commit点、temporary cleanup、
persistent residue、ignored status和平台EH差异已完成四端映射；64条IDA任务注释、4个书签已写入并
保存到四库。

正式native unit、Web Debug、failure-injection runtime和cross-reference differential仍属于`MP-V`阶段。
