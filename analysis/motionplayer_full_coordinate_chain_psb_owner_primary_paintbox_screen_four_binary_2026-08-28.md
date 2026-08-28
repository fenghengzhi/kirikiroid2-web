# PSB → ownerLayer → primaryLayer → paintBox → screen 完整坐标链四参考二进制联合恢复

日期：2026-08-28  
原始任务：`MP-G22`

## 1. 结论

四个参考二进制的完整坐标链可以精确分成两个契约域：

```text
MotionPlayer 数值域
PSB source/layer fields
  → node local/accumulated transform
  → source quad / Bezier or composite mesh
  → node bounds
  → PreparedRenderItem corners + paintBox
  → camera offset / stereovision projection
  → target clip
  → target-Layer-local Canvas/D3D pixels

KiriKiri Layer 合成域
target Layer local pixels
  → target Layer 自身 left/top/父子关系/宿主合成
  → screen
```

`ownerLayer`和`primaryLayer`不是数值坐标变换。它们是创建脚本`Layer`对象时使用的生命周期和父子
挂接参数：前者提供owner/window闭包，后者是parent/target Layer。MotionPlayer不会读取它们的
`left`、`top`，也不会从parent Layer取第二套矩阵。最终target Layer如何摆到screen由KiriKiri Layer
compositor完成，已经位于motionplayer.dll的契约边界之外。

本地实现逐阶段对应四端；未发现需要修改production语义的差异。现有unit资产已经分别锁定source
解析、变换顺序、顶点、bounds、prepared item、projection、clip、Canvas和D3D提交，不需要为同一公式
新增重复测试。本任务的贡献是首次把这些局部slice连接成一条无缺口的数据流，并明确
`primaryLayer → screen`不是待补的motionplayer矩阵。

## 2. 本轮 fresh 四端证据总量

本轮使用原生`mcp__idalib__*`对64个独立函数范围重新执行decompile、完整disassembly和
`xrefs_to`审计。所有disassembly均为`truncated=false`。

| 平台 | 独立范围 | 完整指令 | `xrefs_to` | IDB 更新 |
|---|---:|---:|---:|---|
| Android arm64 | 16 | 13,285 | 48 | 16条任务注释、1个书签 |
| Android armv7 | 16 | 9,608 | 46 | 16条任务注释、1个书签 |
| iOS arm64 | 16 | 8,150 | 46 | 16条任务注释、1个书签 |
| iOS armv7 | 16 | 11,472 | 46 | 16条任务注释、1个书签 |
| 合计 | 64 | 42,515 | 186 | 64条注释、4个书签；四库原位保存 |

根集合贯穿node初始化、source resolver、updateLayers、local transform、vertex/mesh phase、calcBounds、
prepared-item builder、projection、render-command builder、SeparateLayerAdaptor构造和payload resolver、
internal Layer materializer、Canvas envelope、D3D envelope、mesh submit及post-draw。根函数都已有稳定名称，
所以本轮没有为了制造统计而重复rename。

## 3. 四端完整根映射

### 3.1 Android arm64

| 阶段 | 地址 | 完整指令 | `xrefs_to` |
|---|---:|---:|---:|
| initialize node | `0x6B1058` | 776 | 1 |
| findSourceForNode | `0x691CC8` | 1,191 | 5 |
| updateLayers root | `0x6B871C` | 685 | 5 |
| local transform helper | `0x696D20` | 105 | 5 |
| vertex/mesh phase | `0x6B98D0` | 1,265 | 1 |
| calcBounds | `0x6C10E4` | 480 | 4 |
| append prepared items | `0x6BF714` | 1,507 | 5 |
| prepared projection | `0x6D2644` | 253 | 4 |
| build render commands | `0x6C2208` | 1,766 | 2 |
| SeparateLayerAdaptor ctor | `0x6C3DB4` | 92 | 2 |
| SLA payload resolver | `0x6C3F28` | 387 | 3 |
| internal Layer materializer | `0x6CB57C` | 398 | 2 |
| Canvas renderer | `0x6C4820` | 2,363 | 1 |
| D3D envelope | `0x6AB204` | 100 | 2 |
| mesh submit | `0x69AFE4` | 1,829 | 5 |
| ordinary post-draw | `0x6CBBB8` | 88 | 1 |

### 3.2 Android armv7

| 阶段 | 地址 | 完整指令 | `xrefs_to` |
|---|---:|---:|---:|
| initialize node | `0x580FA4` | 504 | 1 |
| findSourceForNode | `0x570500` | 676 | 5 |
| updateLayers root | `0x5856E0` | 764 | 4 |
| local transform helper | `0x572F80` | 129 | 5 |
| vertex/mesh phase | `0x5866F8` | 1,108 | 1 |
| calcBounds | `0x58BE38` | 402 | 3 |
| append prepared items | `0x58B178` | 944 | 5 |
| prepared projection | `0x596EB0` | 327 | 4 |
| build render commands | `0x58C7C4` | 1,348 | 2 |
| SeparateLayerAdaptor ctor | `0x58DBDC` | 67 | 2 |
| SLA payload resolver | `0x58DCD4` | 229 | 3 |
| internal Layer materializer | `0x592F7C` | 212 | 2 |
| Canvas renderer | `0x58E2CC` | 1,891 | 1 |
| D3D envelope | `0x57D2CC` | 79 | 2 |
| mesh submit | `0x575800` | 871 | 5 |
| ordinary post-draw | `0x59327C` | 57 | 1 |

### 3.3 iOS arm64

| 阶段 | 地址 | 完整指令 | `xrefs_to` |
|---|---:|---:|---:|
| initialize node | `0x100108720` | 433 | 1 |
| findSourceForNode | `0x1000F316C` | 586 | 5 |
| updateLayers root | `0x10010E544` | 719 | 4 |
| local transform helper | `0x1000F6A7C` | 106 | 5 |
| vertex/mesh phase | `0x10010F6AC` | 961 | 1 |
| calcBounds | `0x100115C68` | 332 | 3 |
| append prepared items | `0x1001148F8` | 820 | 5 |
| prepared projection | `0x100123038` | 228 | 4 |
| build render commands | `0x1001167BC` | 1,083 | 2 |
| SeparateLayerAdaptor ctor | `0x1001298C4` | 50 | 2 |
| SLA payload resolver | `0x100117E88` | 190 | 3 |
| internal Layer materializer | `0x10011E2BC` | 178 | 2 |
| Canvas renderer | `0x1001186E0` | 1,531 | 1 |
| D3D envelope | `0x100104284` | 87 | 2 |
| mesh submit | `0x1000F974C` | 787 | 5 |
| ordinary post-draw | `0x10011E6CC` | 59 | 1 |

### 3.4 iOS armv7

| 阶段 | 地址 | 完整指令 | `xrefs_to` |
|---|---:|---:|---:|
| initialize node | `0x105E70` | 739 | 1 |
| findSourceForNode | `0xEF97C` | 952 | 5 |
| updateLayers root | `0x10BE5C` | 821 | 4 |
| local transform helper | `0xF36BC` | 131 | 5 |
| vertex/mesh phase | `0x10CE30` | 1,297 | 1 |
| calcBounds | `0x11354C` | 433 | 3 |
| append prepared items | `0x1123D8` | 1,034 | 5 |
| prepared projection | `0x1220F0` | 335 | 4 |
| build render commands | `0x114118` | 1,582 | 2 |
| SeparateLayerAdaptor ctor | `0x128890` | 101 | 2 |
| SLA payload resolver | `0x115B34` | 329 | 3 |
| internal Layer materializer | `0x11CAC8` | 298 | 2 |
| Canvas renderer | `0x11653C` | 2,155 | 1 |
| D3D envelope | `0x101680` | 140 | 2 |
| mesh submit | `0xF685C` | 1,035 | 5 |
| ordinary post-draw | `0x11CF20` | 90 | 1 |

## 4. PSB source字段进入node

node初始化从PSB layer读取类型、父索引、local transform/timeline字段、source标识、mesh和clip相关字段，
然后updateLayers在root/parent累计状态上应用local变化。source不是初始化时复制一张完整图片：
`findSourceForNode`按atlas或generic source路由，向node的`SourceState`发布：

```text
width, height
originX, originY
clip/source texture rectangle
owning object Variant（generic ObjSource）
borrowed texture pointer（atlas route）
valid / blank / route flags
```

因此source图像坐标的基准由尺寸、origin和当前active slot附加origin决定；PSB layer的position/scale/
angle等则通过node accumulated state和2×2 matrix进入模型坐标。两组数据直到vertex phase才汇合。

## 5. node累计变换到四角和mesh

普通source quad的共同公式为：

```text
posX = accumulated.posX
posY = accumulated.posY + accumulated.posZ * zFactor

totalOX = source.originX + activeSlot.ox
totalOY = source.originY + activeSlot.oy

orgX = posX - (m12 * totalOY + totalOX * m11)
orgY = posY - (totalOY * m22 + totalOX * m21)

cw = source.width
ch = source.height

TL = (orgX,                         orgY)
TR = (orgX + m11*cw,                orgY + m21*cw)
BR = (orgX + m11*cw + m12*ch,       orgY + m21*cw + m22*ch)
BL = (orgX + m12*ch,                orgY + m22*ch)
```

matrix和accumulated position是double计算；每个最终vertex store窄化为float。父子transform的应用顺序
已经在updateLayers phase1/phase2中完成，renderer不会再向owner/primary Layer索取数值矩阵。

mesh路径从4×4 Bezier control patch求点，必要时叠加mesh-combine delta并沿ancestor mesh chain变形，
再按division/grid派生提交点。四端都把PSB内的索引、16点patch、division和相关vector元数据视为可信；
畸形数据可越界或除零，没有额外安全gate。

## 6. 顶点到bounds，再到PreparedRenderItem

`calcBounds`对普通node按固定优先级选择点集：

1. composite points非空时使用全部composite points；
2. 否则transformed mesh非空时固定读取最前16点；
3. 否则使用四个source-quad corners。

min/max比较使用`<=`/`>=`，然后left/top取`floor`，right/bottom取`ceil`并存回float。NaN会因ordered
comparison不更新对应sentinel；函数不会修复非有限输入。

`appendPreparedRenderItems`把node数据投影到跨renderer的持久item：

```text
node.vertices                   → item.corners
node.bounds                     → item.paintBox
{posX, posY + zFactor*posZ}     → item.commandCoord
node.matrix                     → item.matrix
source.origin + activeSlot      → item.commandOrigin
&node.source                    → item.sourceState（borrow）
parent/child node-owned items   → item parent/child pointers（borrow）
```

root affine可在main list发布前再次变换corners、mesh和paintBox。item不保存ownerLayer/primaryLayer的
position；这再次排除了隐含的Layer-to-screen数值变换。

## 7. camera offset、stereovision和paintBox重建

projection pass首先对每个main item无条件执行float域的camera offset：四角、composite points、
`meshType == 1`实际mesh、paintBox和viewport都平移。raw Bezier command points不变，viewport只平移不做
透视投影。

stereovision启用且`itemZ != cameraZ`时，每个几何点用double中间量：

```text
denominator = itemZ - cameraZ
x' = x - itemZ * (x - originX) / denominator
y' = y - itemZ * (y - originY) / denominator
```

最后窄化为float。进入投影分支时旧paintBox被丢弃，以`{+FLT_MAX,+FLT_MAX,-FLT_MAX,-FLT_MAX}`
重建，并对投影后四角、composite points、meshType 1 mesh逐点floor/ceil。相等Z跳过投影但保留先前
translation；NaN、不等infinity及除法异常都保持原生IEEE行为，没有finite检查或回滚。

## 8. paintBox到target clip和Layer-local像素

`buildRenderCommands`将paintBox与target clip相交；有viewport时先对viewport做floor/ceil再参与相交。
leaf item只有在相交结果两个轴都严格为正时才创建/复用Layer并发布`clipRect`。leaf Layer尺寸为：

```text
width  = clipRight  - clipLeft
height = clipBottom - clipTop
```

几何复制时减去clip origin，故payload坐标成为该leaf Layer的局部坐标。composed group也先在同一target
坐标空间合并child paintBox，再创建局部Layer。

Canvas envelope把target Layer的`width/height`变成`[0,0,w,h]` target clip，逐item调用`Layer.setClip`。
宽高先在float相减，再提升为TJS Real；向draw-region累计paintBox时按toward-zero整数转换。affine/mesh
复制遵循参考实现的`-0.5`pixel-center约定，buffered路径还减bufferLeft/bufferTop。

D3D envelope同样使用`{0,0,target.width,target.height}`，普通adaptor加`+0.5,+0.5`offset后提交到私有
OpenGL manager。mesh cell拆成两个triangle，顺序固定为：

```text
TL, TR, BL
TR, BL, BR
```

所有点都已经是target texture局部坐标；D3D backend不会读取脚本Layer的父层位置。

## 9. ownerLayer / primaryLayer 的精确角色

shared Layer factory接收两个borrowed dispatch：`owner`和`parent`。factory通过`Layer.CreateNew`创建
Layer并建立脚本对象owner/objthis关系；它不读取任何坐标属性。

`SourceCache`保存owner和primaryLayer，用它们创建缓存/工作Layer。`SeparateLayerAdaptor`从
primaryLayer构造target closure，之后以`_owner`和`_targetLayer`创建leaf/composed child Layer。
internal render-layer materializer也用同样factory关系创建internal/work Layers，并从target读取width/
height设工作区大小。ordinary post-draw最后把internal image assign给caller target；accurate SLA用
`piledCopy`复制相同target-local内容。

因此：

- `ownerLayer`决定对象归属与window上下文；
- `primaryLayer`决定parent/target层级；
- `paintBox`决定MotionPlayer在target坐标中的可见整数包围盒和裁剪；
- target Layer的`left/top`、更高层parent transform和最终screen位置属于宿主Layer compositor。

把`primaryLayer.left/top`再加到motion顶点会造成双重变换，反而偏离四个参考二进制。

## 10. 对象、容器、回调与边界行为

整条链没有transaction snapshot：node deque、prepared pointer vectors、SLA ordered maps和Layer
Variants在各自阶段按顺序发布。PSB getter、source load、Layer factory、property getter/setter、setClip、
copy/piledCopy/assignImages等回调均可抛出或重入；已完成的node字段、item字段、Layer map插入和clip
publication不回滚。

关键borrow包括`PreparedRenderItem::sourceState`、parent/child item、atlas texture、owner/parent factory
参数和D3D source texture。正常同步调用依赖这些对象在本帧consumer返回前保持live；四端没有generation、
锁或callback后重新定位。

数值边界同样逐级保留：double accumulated/matrix到float vertex的窄化、float min/max和floor/ceil、
float camera translation、double stereo、float paintBox、float-before-Real clip尺寸、toward-zero draw-region、
Canvas `-0.5`与D3D `+0.5`约定。没有任何一步用“更精确”的统一double管线替代这些阶段性舍入。

## 11. 本地逐行对照

本地实现与四端根逐项对应：

- `cpp/plugins/motionplayer/NodeTree.cpp:81`：node初始化；
- `cpp/plugins/motionplayer/PlayerResource.cpp:665`：source尺寸、origin、clip、object/texture发布；
- `cpp/plugins/motionplayer/PlayerUpdateLayers.cpp:11`：updateLayers root；
- `cpp/plugins/motionplayer/PlayerUpdateLayersInternal.h:621`：local/parent transform；
- `cpp/plugins/motionplayer/PlayerUpdateGeometry.cpp:153`：quad、mesh和ancestor变形；
- `cpp/plugins/motionplayer/PlayerRenderItems.cpp:321`：calcBounds；
- `cpp/plugins/motionplayer/PlayerRenderItems.cpp:442`：PreparedRenderItem publication；
- `cpp/plugins/motionplayer/PlayerRenderItems.cpp:971`：camera/stereovision projection；
- `cpp/plugins/motionplayer/PlayerRenderExecute.cpp:396`：target clip和leaf/composed command；
- `cpp/plugins/motionplayer/SeparateLayerAdaptor.cpp:204`、`:290`：target closure与Layer resolver；
- `cpp/plugins/motionplayer/PlayerRenderTargets.cpp:455`：internal/work Layer materializer；
- `cpp/plugins/motionplayer/PlayerRenderTargets.cpp:948`、`:1081`：D3D/Canvas envelopes；
- `cpp/plugins/motionplayer/PlayerRenderTargets.cpp:1162`：ordinary post-draw assignImages；
- `cpp/plugins/motionplayer/MotionRenderBackend.cpp:507`：mesh cell到triangle submit。

没有发现production差异，故本任务不做语义修改。

## 12. 既有回归资产与验证状态

已有unit tests分段覆盖这条链：

- `tests/unit-tests/plugins/motionplayer-dll.cpp:5748`：Canvas clip尺寸保持float-before-Real；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:6301`：composed group与target clip；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:6462`：fractional clip到TJS边界；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:6509`：private GLL clip；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:13770`：calcBounds点集、floor/ceil和特殊值；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:17993`：instance/local/parent transform顺序；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:20132`：source quad vertex materialization；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:24101`：Bezier/mesh helper链；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:25663`：resource/source/query完整链；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:26834`起：source route与fallback；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:32162`：camera/stereovision projection与paintBox重建。

本轮完成42,515条完整指令、186个`xrefs_to`、64条任务注释、4个书签和四库保存；coverage与
163-ticket映射随后重生成并执行严格列数、重复ID和`git diff --check`检查。正式native unit、Web Debug
及真实screen compositor集成运行仍归`MP-V`验证任务；静态闭合不伪称这些命令已经通过。

`MP-G22`没有剩余task-local静态差异。
