# MotionNode 源声明顺序、owner 生命周期与未初始化边界（四参考二进制，2026-08-27）

## 1. 范围与完成度

本纵切面联合审计四端 `MotionNode` 的默认构造、默认值尾 helper、编译器生成的
成员逐项 copy assignment 和析构器，目标是从构造顺序、复制分段和逆序析构恢复
源声明关系，而不是把任一 ABI 的 padding 写进 portable C++。

| 端 | default ctor | defaults tail | copy assignment | destructor |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6EED94`，179 | `0x696770`，96 | `0x6F1A6C`，172 | `0x6F206C`，92 |
| Android armv7 | `0x5ACC70`，79 | `0x572A2C`，114 | `0x5AECA0`，174 | `0x5AF220`，50 |
| iOS arm64 | `0x10014151C`，89 | `0x1000F6580`，100 | `0x10014451C`，183 | `0x10012A48C`，69 |
| iOS armv7 | `0x1425BC`，142 | `0xF316C`，153 | `0x144FCA`，213 | `0x1290A6`，68 |

16 个函数均 fresh decompile，并从 offset 0 完整读取合计 1973 条指令；所有
disassembly cursor 均 `done=true`。四库已统一命名
`MotionNode_defaultCtor_guess`、`MotionNode_initializeDefaultsTail_guess`、
`MotionNode_copyAssign_guess`、`MotionNode_dtor_guess`，添加函数注释/bookmark 并保存。

最终闭包再次 fresh 读取同一组 16 个函数的反编译和完整反汇编，并把覆盖状态升级为
`IMPLEMENTED`：portable 声明已按下面的共同 owner 顺序重排；精确 64 字节 dormant 区和
对象末尾 copied-only word 被表达为两个真实、未初始化、参与默认复制的不透明 source
member，而不是 ABI padding。二进制不能唯一给出它们的原始名称和语义类型，所以实现明确
保留 object representation，不在 `float[16]`、`double[8]` 或任意命名 record 中捏造一种。

## 2. 物理对象与 deque 容器差异

| 端 | `sizeof(MotionNode)` | deque block |
|---|---:|---:|
| Android arm64 | 2632 | libstdc++，每 block 1 node |
| Android armv7 | 2272 | libstdc++，每 block 1 node |
| iOS arm64 | 2648 | libc++，每 block 16 nodes |
| iOS armv7 | 2228 | libc++，每 block 16 nodes |

尺寸和 block 策略是 ABI/STL 差异，不是条件源码分支。range erase 的 relocation 分支
最终都落到本表的成员 copy assignment；正常 Player 的 non-root suffix erase 通常不走
relocation，但该函数仍证明类型允许 copy assignment，而且 raw
`PreparedRenderItem *` 会像普通标量一样浅拷贝。

## 3. 四端共同源声明骨架

忽略 ABI padding 后，构造/赋值/析构共同给出的 owner 顺序是：

```text
ttstr layerName
trivial identity/parameter/type/flag fields
tTJSVariant frameList
trivial transform/color/matrix/vertex/previous-position fields
SourceState.valid + tTJSVariant object + texture/scalars + ttstr path
ClipSlot slots[0]
ClipSlot slots[1]
active-slot selector
std::string dormantString
trivial pre-evaluation state
two independent LayerGetter visibility bytes
AccumulatedState
DeltaState
HitData shapeGeometry
64-byte dormant trivial region (exact source type unresolved)
float vertices[8]
float bounds[4]
PreparedRenderItem *preparedRenderItem
tTJSVariant childPlayer
trivial render/mesh-link bytes and pointers
tTJSVariant emoteEdit
trivial mesh configuration
vector<MeshPoint> meshControlPoints
vector<MeshPoint> compositeMeshPoints
vector<MeshPoint> transformedMeshControlPoints
trivial mesh inverse/AABB/particle configuration
tTJSVariant particleMotionList
trivial particle interpolation/configuration
tTJSVariant particleArray
trivial anchor/emitter state
ttstr emitterDtgt
trivial anchor damping state
tTJSVariant stencilCompositeMaskLayerList
vector<MotionNode *> stencilCompositeMaskNodes
one copied-only trailing word (source name/type unresolved)
```

`preparedRenderItem` 是 raw owner，析构函数体先 `delete`，随后编译器才按声明逆序销毁
成员；所以“它最先释放”不等于“它是最后声明成员”。copy assignment 位于 child Variant
之前的 trivial memcpy 分段，独立确认了它在源对象中的位置。

## 4. 前部字段与 owner 坐标

地址只用于恢复声明关系：

| 逻辑字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| label | `+0` | `+0` | `+0` | `+0` |
| parameter pointer | `+8` | `+4` | `+8` | `+4` |
| coordinate / type | `+24/+28` | `+16/+20` | `+24/+28` | `+16/+20` |
| shape / parent / inherit | `+32/+36/+40` | `+24/+28/+32` | `+32/+36/+40` | `+24/+28/+32` |
| priorDraw / stencilType | `+48/+52` | `+40/+44` | `+48/+52` | `+40/+44` |
| timeline ratio | `+56` | `+48` | `+56` | `+48` |
| frameList Variant | `+64` | `+56` | `+64` | `+56` |
| transformOrder | `+84` | `+68` | `+84` | `+68` |
| 2x2 matrix | `+120` | `+104` | `+120` | `+104` |
| vertex position | `+152` | `+136` | `+152` | `+136` |
| previous position | `+176` | `+160` | `+176` | `+160` |
| SourceState valid/object | `+200/+204` | `+184/+188` | `+200/+204` | `+184/+188` |
| SourceState path | `+312` | `+288` | `+312` | `+284` |
| slots | `+320/+856` | `+296/+728` | `+320/+856` | `+288/+708` |
| active selector | `+1392` | `+1160` | `+1392` | `+1128` |
| dormant `std::string` | `+1400` | `+1168` | `+1400` | `+1136` |

两个 64 位目标的前部关系相同；32 位 Android 的旧 libstdc++ COW string 是 4 字节，
iOS libc++ string 是 12 字节，导致后部整体坐标分叉。`SourceState.path` 在四端都是
`ttstr` owner；它不是 active selector 后的独立 `std::string`，两者析构路径不同。

## 5. AccumulatedState、DeltaState 与 LayerGetter 的真实边界

| 端 | getter visible/branch bytes | AccumulatedState | DeltaState |
|---|---:|---:|---:|
| Android arm64 | `+1496/+1497` | `+1504` | `+1584` |
| Android armv7 | `+1256/+1257` | `+1264` | `+1344` |
| iOS arm64 | `+1512/+1513` | `+1520` | `+1600` |
| iOS armv7 | `+1228/+1229` | `+1232` | `+1312` |

AccumulatedState 的共同 0x50-byte 语义顺序是：

```text
dirty, active, visible, flipX, flipY,
posX, posY, posZ, angle,
scaleX, scaleY, slantX, slantY, opacity
```

DeltaState 紧随其后，具有相同数值尾部和 `dirty/activeOverride/visibleOverride` 头。
默认 tail 先初始化 AccumulatedState，再初始化 DeltaState；本地原先声明顺序相反，而且
把 evaluated block 头误排成 `visible,active,dirty`，现已纠正。

更重要的是，LayerGetter 三个 visibility callback 不读取这个 block：

```text
getVisible       = node.layerGetterVisible
getBranchVisible = node.layerGetterBranchVisible
getLayerVisible  = node.layerGetterVisible && node.layerGetterBranchVisible
```

四端完整 default ctor/default tail 均不写这两个字节；完整 raw-layer initializer、
updateLayers root/phase helpers 也不写。对 motionplayer 代码区逐条 rendered-disassembly
搜索后，直接触达只剩三个 getter 和 compiler-generated copy assignment。故这是原版真实
的未初始化读取边界，不能继续解释成 `accumulated.visible/active`。本地已新增两枚独立
字段、保持无默认成员初始化，并让 LayerGetter 直接读取；`MotionNode()` 改为 user-provided
构造，避免无参数 allocator construction 先对整个对象执行 value-zero-initialization。

## 6. `forceVisible` 不存在：它是 live emoteEdit Variant 类型标签

| 端 | emoteEdit Variant | Type tag |
|---|---:|---:|
| Android arm64 | `+1980` | `+1996` |
| Android armv7 | `+1708` | `+1720` |
| iOS arm64 | `+1996` | `+2012` |
| iOS armv7 | `+1672` | `+1684` |

时间轴 source refresh、modified prepass、vertex priorDraw、quad admission、geometry mirror、
visibility 和 prepared-item admission 的所有所谓 `forceVisible` gate 都直接读取上述
Variant type word并与零比较。构造器把它作为 emoteEdit Variant 的类型字段初始化，
raw-layer initializer 通过 Variant assignment 覆写；四端都不存在第二个 parallel int。

本地原先同时保留 `emoteEditVariant` 和 `int forceVisible`，因此 re-entrant Variant clear、
非 Object non-Void Variant和测试手工赋值都可能让两份状态分裂。现已删除影子整数，所有
路径统一使用 `emoteEditVariant.Type() != tvtVoid`；helper 参数也改名为
`emoteEditPresent`。这同时恢复了 native 对 live type tag 的逐次重读边界。

## 7. 后部 owners、mesh 与 stencil 修正

| owner/字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| prepared item pointer | `+1904` | `+1664` | `+1920` | `+1628` |
| child Variant | `+1912` | `+1668` | `+1928` | `+1632` |
| emoteEdit Variant | `+1980` | `+1708` | `+1996` | `+1672` |
| three mesh vectors | `+2024/+2048/+2072` | `+1740/+1752/+1764` | `+2040/+2064/+2088` | `+1704/+1716/+1728` |
| mesh inverse + float offsets | `+2096..+2132` | `+1776..+1812` | `+2112..+2148` | `+1740..+1776` |
| particle motion Variant | `+2200` | `+1880` | `+2216` | `+1844` |
| particle Array Variant | `+2296` | `+1968` | `+2312` | `+1932` |
| emitter target ttstr | `+2384` | `+2048` | `+2400` | `+2012` |
| stencil list Variant | `+2576` | `+2240` | `+2592` | `+2200` |
| stencil pointer vector | `+2600` | `+2252` | `+2616` | `+2212` |
| copied-only trailing word | `+2624` | `+2264` | `+2640` | `+2224` |

此前工作笔记曾把 trailing word 猜成 `stencilCompositeMaskReferenced`；四端完整搜索否定
该命名：trailing word 只在 copy assignment 中逐字复制，没有构造写入、build post-link
写入或 render consumer。真实 stencil-reference 是 render-link cluster 中靠近
`drawFlag/hasMeshData` 的单字节字段，而不是对象末尾 word。本报告不为 trailing word
发明名称或默认值。

mesh inverse 也不在 HitData 与 vertices 之间；它位于三个 mesh vector 后。vertex helper
仅在 live mesh gate 下写 inverse，calcViewParam/deformation 再读取同一组字段。最终 portable
声明已把该组移回三个 vector owner 之后。

## 8. 两块不可命名但可精确表达的 source-level 成员

HitData 末尾到 vertices 开头之间四端都恰好有 64 字节：

| 端 | dormant 64-byte range | vertices |
|---|---:|---:|
| Android arm64 | `+1792..+1855` | `+1856` |
| Android armv7 | `+1552..+1615` | `+1616` |
| iOS arm64 | `+1808..+1871` | `+1872` |
| iOS armv7 | `+1516..+1579` | `+1580` |

完整构造/default tail 不初始化，motionplayer 代码区没有直接 member-offset consumer，
copy assignment 只把它包含在 trivial block 中。跨 ABI 恒定 64 字节证明它是一个真实
source member/record，而非 ABI padding；但现有证据不能在 `float[16]`、`double[8]` 或
其它 record 中作唯一选择。对象尾部 4 字节同理。

portable 实现因此使用两个具名的证据边界类型：

```cpp
struct DormantRecord64_guess { std::byte objectRepresentation[64]; };
struct CopiedOnlyTrailingWord_guess { std::byte objectRepresentation[4]; };
```

二者都没有默认成员初始化，仍由 compiler-generated copy assignment 逐成员复制；没有析构
副作用。这里的 `byte` 不是用来凑单端 offset 的 padding，而是对四端共同、真实却语义不可观测
成员的最小诚实表示。源码注释和 coverage 明确记录原始类型名仍不可识别。

## 9. 本地逐行对照与实施

最终闭包实施：

- `MotionNode.h`：user-provided default ctor；新增两枚独立 LayerGetter visibility 字节；
  AccumulatedState 头顺序和 Accumulated/Delta 声明顺序；删除 `forceVisible`；新增 live
  `hasEmoteEdit_guess()`；
- 以 `layerName -> frameList -> SourceState -> ClipSlot[2] -> dormant std::string ->
  accumulated/delta -> shape/opaque/vertices/bounds -> prepared/child -> emoteEdit -> mesh
  vectors -> particle owners -> emitter ttstr -> stencil owners -> trailing opaque word` 重排
  portable 数据成员；
- 把早期 2×2 matrix 从 0x50-byte `AccumulatedState` 拆为独立 `MatrixState`，机械更新运行
  C++ 与测试的 129 个访问点；
- 加入两个无默认初始化的不透明 source member 和尺寸静态断言；
- `PlayerLayerQuery.cpp`：三个 visibility getter 改读独立字节并保留左到右短路；
- `PlayerUpdateLayerEval.cpp`、`PlayerUpdateGeometry.cpp`、`PlayerRenderItems.cpp`：所有
  admission/refresh/mirror gate 直接重读 emoteEdit Variant Type；
- unit case：显式证明 LayerGetter visibility 与 accumulated 状态相互独立，并把原有
  forced-visible cases 改由 non-Void emoteEdit 建立 native gate。

两块不透明 member 的原始业务名称/类型仍是不可逆信息，但其可观察的大小、未初始化、
copy-only、无析构副作用和声明边界已经实现；不再把“无法命名”误写成“无法表达”。

## 10. 验证限制

执行 `git diff --check`、coverage 12 列、duplicate ID、残留 `node.forceVisible`、旧
`accumulated.m11/m12/m21/m22` 和旧 LayerGetter mapping 搜索。四库已补充不透明 member 与
string getter 的函数注释/bookmark 并保存。当前环境仍缺 CMake/Ninja/Emscripten，且
standalone clang 被工程依赖头阻断，因此不能声称正式 unit/Web build 通过。
