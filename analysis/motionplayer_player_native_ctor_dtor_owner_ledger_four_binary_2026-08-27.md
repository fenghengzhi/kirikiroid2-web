# Player native constructor / destructor / owner 总账（四参考二进制）

日期：2026-08-27

## 1. 本 slice 的位置

NCB 构造 descriptor、参数个数/Void sentinel、native allocation 与 adaptor attach 已由
`motionplayer_player_ncb_surface_and_constructor_four_binary_2026-08-26.md` 闭合；MotionNode、
PreparedRenderItem、旧树 reset 和 Player destructor 的显式 clear 顺序已由
`motionplayer_motionnode_prepared_item_deque_lifetime_four_binary_2026-08-27.md` 闭合。

本报告补上两者之间缺失的 native `Player::Player(Variant)` 完整成员构造、persistent owner、默认值、
root publication 和异常 rollback，并把已闭合 destructor 交叉成一份 source-order / reverse-order 总账。
同时处理本地 constructor 中参考实现不存在的 logger/PRTDIAG side effects。

## 2. 四端完整 native ctor

| 平台 | constructor | allocation size | 完整指令 | 主函数范围 |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6CC110` | `0x568` | 593 | `0x6CC110..0x6CCA54` |
| Android armv7 | `0x5935C4` | `0x3B0` | 281 | `0x5935C4..0x593918` |
| iOS arm64 | `0x10011EC04` | `0x4B8` | 226 | `0x10011EC04..0x10011EFA8` |
| iOS armv7 | `0x11D488` | `0x348` | 499 | 含分块/SjLj constructor flow |

四端均 fresh decompile，并从 offset 0 读取完整 disassembly；四个 cursor 都是 `done=true`。
Android arm64 的 593 条包含 libstdc++ unordered/deque allocation 与 constructor cleanup chunks；
iOS armv7 的 499 条包含 libc++ container setup、VFP stores 和 SjLj 状态。allocation size 是 ABI/STL
结果，不能通过 padding 写入 portable class。

已闭合 destructor 对应为 Android arm64 `0x6CCEBC` 311 条、Android armv7 `0x593C24` 99 条、
iOS arm64 `0x10011F2A0` 101 条、iOS armv7 `0x11DCC4` 175 条（另有 108 条 SjLj cleanup）。

## 3. 四类构造调用者

四端 xref 联合得到相同 source-level 调用族，每个产品各四项：

1. EmoteEngine 构造主 Player；
2. buildNodeTree 初始化 type-3 node 时构造 nested-motion child Player；
3. particle-system phase 构造 particle child Player；
4. NCB `Motion.Player` materializer 构造脚本直接拥有的 Player。

iOS 对其中部分调用保留一条 4-byte thunk，Android 直接 call；这不是第五种 owner。四族均把同一个
外部 ResourceManager Variant 传入，但每个新 Player 都建立自己的三份 Variant CopyRef。

## 4. source-order member 与容器构造

四端共同的逻辑顺序为：

```text
rootPlayer = this
parentPlayer = null
currentDispatch = null
construct NodeLabelMap
zero camera position/target/stereo triples and camera offsets
publish DBL_MAX, DBL_MAX, -DBL_MAX, -DBL_MAX bounds sentinels

construct MotionNode deque
construct EvalCascadeMap and LabelValueMap
selectedParameterEntry = null
construct parameter vector and ramp multimap

construct timeline Variants/containers/strings in declaration order
copy rm argument -> findSourceResourceManager
copy rm argument -> sourceCacheObject
default-construct descriptor/internal layers/colors/work layer slots
construct pending strings, draw region, event vector and four live strings
copy rm argument -> canonical resourceManager
default-construct findMotion context, outline, meshline, tag Variant

construct PerNodeLayerStateMap, VariableSnapshotMap and variable-scope deque
```

两份早期 RM owner 与 canonical RM owner 是三个独立 Variant；Object 类型时各自 AddRef。constructor
没有 native ResourceManager by-value member，也没有 SourceCache/ResourceManager back-pointer。

NodeLabelMap 是 ordered map；四个 hash containers 使用目标标准库的普通 chained unordered map，
max-load-factor 为 1，默认构造不预建业务节点。Android libstdc++ 可能为 deque map 初始化分配
小型 block-map；iOS libc++ 保存自己的 start/count/map 形状。这些物理差异由标准库承担。

## 5. persistent Dictionary 两阶段 publication

全部非平凡成员构造后共同执行：

```text
descriptorCreationOwner = TJSCreateDictionaryObject()
sourceDescriptor = Variant(descriptor, descriptor)

colorsCreationOwner = TJSCreateDictionaryObject()
sourceColors = Variant(colors, colors)

sourceDescriptor.PropSet(
    TJS_MEMBERENSURE, L"color", colorMemberHint,
    sourceColors, sourceDescriptor)
```

`sourceDescriptor` 和 `sourceColors` 各自是 persistent Variant owner；dictionary factory 返回的 creation
owner 另外保持到 constructor 尾部。`descriptor.color` publication 又让 descriptor 持有 colors。
顺序固定为 descriptor create/assign、colors create/assign、PropSet。没有 rollback transaction：若
PropSet 抛出，C++ constructor cleanup 逆序销毁已构造 persistent members，同时局部 creation owner
也各 Release 一次，descriptor 内已成功发生的 member writes由 descriptor 最终 destruction处理。

正常尾部 creation owners 按 colors -> descriptor 顺序 Release；persistent Variants 和
descriptor.color 继续持有对象。

四端 PropSet 前后都没有 logger、motion path、类型打印或调试 callback。原始宽 key 是完整
`color`；部分 Hex-Rays 输出只显示首字符是 string rendering artifact。

## 6. 标量默认值和故意未初始化槽

共享默认值包括：

- bounds 为 `+DBL_MAX,+DBL_MAX,-DBL_MAX,-DBL_MAX`；
- `queuing=true`、`firstFrame=false`、`directEdit=false`、`motionCompleted=false`；
- `noUpdateYet=true`，其余 reverse/camera-dirty/draw-affine-ready bytes 为 false；
- draw affine 为 identity，camera velocity/offset/target/position 为零；
- `pixelateDivision=100`；
- preview/camera/stereo/prior/independent/syncWaiting/allplaying/hasCamera 为 false；
- `syncActive` 从 process-global `defaultSyncActive` 取一次构造快照；
- camera FOV 为精确 binary64 `0.2`，zFactor/frame tick 为 `+0.0`；
- completionType/maskMode/processed count 为零；
- packed color 为 `0xFF808080`；outsideFactor/speed/meshDivisionRatio 为 `1.5/1.0/1.0`。

四端共同故意不写七个 source POD 槽：

1. `emoteMotionIndex`；
2. `layerFrameCursor`；
3. `layerCurTime`；
4. `layerNextTime`；
5. `cachedTotalFrames`；
6. `loopTime`；
7. 最后一个 legacy load residual dispatch pointer。

它们由后续 ordinary/emote initialization 在真实消费前提交，或只属于 Android 遗留不可达 load
边界。给这些成员添加 `=0` 会改变 malformed/early-call 行为，不能以“安全初始化”名义修复。

## 7. synthetic root 的 publication frontier

只有 descriptor.color PropSet 正常返回后，constructor 才：

```text
nodes.emplace_back()                    // sole synthetic root
copy process-global defaultTransformOrder[4]
    -> nodes.back().transformOrder[4]
```

普通 `MotionNode` value construction使 parentIndex、flags、字段和四个 transform-order slots按其自身
constructor初始化；Player 随后只覆盖 root 的 transform order。四端没有额外 logical node ordinal
字段。若 deque block/node construction 抛出，两个 persistent dictionaries 已发布，随后由
constructor rollback按逆序销毁；没有半个 root 被当成成功 Player 暴露给调用者。

成功返回时 rootPlayer仍为 self、parentPlayer为 null。nested/particle owners在各自 caller 中于
constructor成功后再提交 parent/root links；把 link store搬进通用 Player ctor会改变 partial commit。

## 8. constructor rollback 与 destructor 正常路径

### 8.1 抛出时

C++ constructor按“仅销毁已经完成构造的 members”逆序 rollback：晚期 variable-scope deque/maps，
tag/style/context/RM Variants，strings/event/draw region，render workspace，timeline containers，parameter
containers，node deque，最后 label map。new-expression caller在 constructor抛出时释放 raw Player
allocation；未达到 caller publication store，不产生已发布 child/native slot。

dictionary creation-owner guards补足 persistent Variant之外的 factory返回引用；root construction抛出
时 colors和descriptor都按 colors -> descriptor局部顺序释放，然后 member rollback继续逆序释放
persistent owners。Android landing pads与iOS SjLj/cleanup形状不同，但共同 source owner链一致。

### 8.2 正常析构

正常 `Player::~Player` 先执行显式 body：

```text
purgeParameterRampMap()
parameterEntries.clear()
variableLabelScopes.clear()
resetAndReleaseOldNodeTree()
delete renderSeparateLayerAdaptor
renderSeparateLayerAdaptor = null
nodes.clear()                         // synthetic root destruction point
```

随后 compiler-generated member destruction从最后 member逆序运行；已经 clear 的容器只释放保留
storage。三个 RM owners、descriptor/colors/workspace、strings/maps等在其声明逆序时点 Release。
这与 constructor rollback不同：正常析构复用旧树 invalidation/releaseLayerId path，并显式把 root
销毁前沿提到自动 member destruction之前。

## 9. 本地对照与证据后修改

`Player.h` 的 source-level member order、自然 initializers、七个无 initializer POD、三个 RM Variant、
两个 persistent Dictionary、raw SeparateLayerAdaptor owner和 container选型已与四端共同结构一致。
`PlayerCore.cpp` 的 constructor body也正确保持 dictionary publication -> root append -> default order
copy -> local creation-owner release。

唯一确认的不匹配是 body最前面的两条：

```text
LOGGER("Motion.Player constructor called")
LOGGER("PRTDIAG ... rmType")
```

它们发生在 dictionary creation之前，会读取 Variant type、格式化指针并可能分配/抛出，让 native
从未存在的异常前沿阻止 Player construction。完成四端完整证据后已删除两条日志；constructor现在
从既有 member initialization直接进入 descriptor factory。

四个 IDB 已添加完整 ctor owner/default/uninitialized/root 注释与 bookmark并保存。

## 10. 验证限制

实施后执行 `git diff --check`、coverage 12列/duplicate-ID和源码静态顺序检查。当前环境缺少
CMake/Ninja/Emscripten正式工具链；独立 clang++语法检查会被仓库外部依赖头阻断，因此不能声称
unit/Web build通过。没有构造失败注入fixture，不捏造异常物料；rollback结论来自四端完整 landing/
SjLj指令流和C++ member-order交叉证据。

