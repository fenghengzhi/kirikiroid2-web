# EmotePlayer NCB 73 行注册面四参考二进制联合恢复

日期：2026-08-27

## 1. slice 边界与结果

本轮闭合 `Motion.EmotePlayer` 的 delayed subclass wrapper、独立 native-class setup、
73 行 registrar、typed one-Variant Factory 链、2 个静态常量、12 个 raw callback descriptor、
全部 ordinary/direct-Engine method 与 property callback 入口。

73 行的精确四端地址、kind、alias、内部入口 disposition 和 body 状态保存在：

- `analysis/motionplayer_emoteplayer_ncb_surface.tsv`；
- 合并后的 `analysis/motionplayer_ncb_equivalence.tsv`。

本轮只闭合注册面。除 Factory 和常量外的 70 个 callback body 仍以
`BODY_PENDING_SEPARATE_SLICE` 记录；这避免把“大 registrar 已对齐”误当成 Engine 状态机、
timeline、selector、Variant owner 和边界行为已经全部逐行恢复。

## 2. subclass / ClassInfo 调用链

| 平台 | delayed subclass wrapper | native-class setup | 73-row registrar |
|---|---|---|---|
| Android arm64 | `0x682FA0` | `0x683528` | `0x67CEA8` |
| Android armv7 | `0x564E2C` | `0x56506C` | `0x5612E8` |
| iOS arm64 | `0x1001B8CD0` | `0x1001B8FB8` | `0x1001B5130` |
| iOS armv7 | `0x1B82B8` | `0x1B8660` | `0x1B4DE0` |

四端 setup 都建立独立的 EmotePlayer native class ID / class dispatch / finalize 成员和 delayed
registration state。`emoteplayer.dll` 的模块回调随后加载 `motionplayer.dll`，从 `Motion`
取得该 class dispatch，并把它以脚本名 `EmotePlayer` 发布；同一个模块回调还向
`ResourceManager` 注入两个 PSB decrypt 方法。后两项不属于这张 73 行成员表。

EmotePlayer 的 native payload 是直接拥有 Player/EmoteEngine 状态的对象，不是
`D3DEmotePlayer -> EmoteObject` shell，也不复用 Player 的 NCB ClassInfo tuple。四端
native-class infrastructure 大小 `0xB0 / 0x70 / 0xB0 / 0x70` 和 payload 分配大小
`0x5D8 / 0x318 / 0x428 / 0x238` 都是 ABI/STL 差异，不应转化成源 padding。

## 3. 73 行发布顺序

完整地址表见 TSV；对象内顺序固定为：

| 台账序号 | 本地成员编号 | 分组 | 数量 |
|---:|---:|---|---:|
| 1 | — | typed Variant Factory | 1 |
| 2-3 | — | `TimelinePlayFlagParallel=1`、`TimelinePlayFlagDifference=2` | 2 |
| 4-22 | #1-19 | 第一组 method，其中 #14-19 为 raw callback | 19 |
| 23-37 | #20-34 | 9 个 read/write property + 6 个 read-only property | 15 |
| 38-44 | #35-41 | affine/camera/root/三组 scale method | 7 |
| 45-53 | #42-50 | 7 个 read/write property + 2 个 read-only property | 9 |
| 54-73 | #51-70 | 第二组 method，其中 #53-58 为 raw callback | 20 |

换言之：

```text
Factory
TimelinePlayFlagParallel = 1
TimelinePlayFlagDifference = 2
progress ... setOuterForce                  # 19 functions
completionType ... processedMeshVerticesNum # 15 properties
setDrawAffineTranslateMatrix ... setBustScale
hairScale ... animating                     # 9 properties
setMirror ... getCommandList                # 20 functions
```

`analysis/motionplayer_emoteplayer_ncb_surface.tsv` 的 sequence 1..73 与本地候选逐项 join；
生成器拒绝缺号、错名、任一平台空字段或额外 evidence。

## 4. typed one-Variant Factory

| 阶段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| descriptor publisher | registrar inline | `0x561C18` | `0x1001B5B20` | `0x1B5712` |
| descriptor invoke | `0x689CA4` | `0x56A280` | `0x1001C5F18` | `0x1C3158` |
| native construct + adaptor attach | `0x689D7C` | `0x56A310` | `0x1001C5FBC` | `0x1C31C8` |
| argument materialize / allocation | `0x689E94` | `0x56A3F4` | `0x1001C60E0` | `0x1C3310` |
| native payload constructor | `0x67B76C` | `0x560948` | `0x1001B7FB0` | `0x1B7788` |

共同边界：

```text
FactoryInvoke(memberName, result, argc, argv, objthis):
    if memberName != null:
        return TJS_E_MEMBERNOTFOUND
    if argc == 1 and argv[0] is Void:
        return TJS_S_OK                 # ncbind empty-adaptor sentinel
    if argc < 1:
        return TJS_E_BADPARAMCOUNT

    rmDispatch = copy/materialize Variant(argv[0])
    native = new EmotePlayer(rmDispatch)
    if native creation returned null:
        throw "NativeClassInstance creation faild."
    if receiver adaptor lookup/attach fails:
        destroy native
        delete native
        return TJS_E_FAIL               # -1008
    receiver.native = native
    return TJS_S_OK
```

因此普通零参数调用失败，一个 Void 被保留为空 adaptor sentinel，额外参数被忽略；arg0
按完整 `tTJSVariant` 复制/物化，不是只取裸 dispatch 指针。Factory 不是 raw callback
overload，也不把 `ResourceManager *` 直接作为 ABI 参数暴露给脚本。

## 5. descriptor kind、alias 与边界事实

### 5.1 12 个 raw callback

两组 raw callback 恰好是：

- `setVariable`、`setCoord`、`setScale`、`setRotate`、`setColor`、`setOuterForce`；
- `playTimeline`、`stopTimeline`、`getTimelinePlaying`、`setTimelineBlendRatio`、
  `fadeInTimeline`、`fadeOutTimeline`。

其余 method 全部使用 typed descriptor，包括直接绑定 `EmoteEngine` 成员的项目。不能因为
目标函数直接落在 Engine body，就把它们改写成 raw callback。

### 5.2 精确 callback alias

四端共同保留以下地址 identity：

- `motionKey` 与 `project` 使用同一 getter/setter pair；
- `frameLastTime` 与 `lastTime` 使用同一 getter；
- `frameLoopTime` 与 `loopTime` 使用同一 getter；
- `hairScale`/`partsScale`/`bustScale` property setter 分别复用
  `setHairScale`/`setPartsScale`/`setBustScale` method callback。

`animating` 是 direct `EmoteEngine` getter-only property，setter 与 indexed slots 为 null。

### 5.3 Android arm64 内部入口

Android arm64 有三个必须保留的非独立入口：

- `frameProgress` callback `0x67A3F8` 是 `0x530E3C` 的 distant tail chunk；
- `motionKey`/`project` setter `0x67C6C0` 属于 `0x67C4AC` 合并函数；
- `setTimelineBlendRatio` raw callback `0x670674` 属于 `0x670350` 合并函数。

Android armv7 的 `progress` callback 原先落在未建函数的 `0x561D08..0x561D24`：本轮以
原生指令边界建立独立函数，fresh decompile 得到 `milliseconds * 60 / 1000` 后 tail-call
Engine progress。没有为 Android arm64 的三个内部入口创建重叠函数。

### 5.4 property 方向以 body 为准

Android arm64 大量展开 property descriptor，内存槽顺序不能直接当成 get/set 方向。本轮
对 setter store 与 getter load/hidden-return body 逐组核验后才写入 TSV。这个检查同时发现
并修正了上一 D3DAdaptor slice 中 Android arm64 四组 property 的方向标签；其余三个平台
使用 helper 参数顺序且已由 thunk body 复核。

## 6. 脚本名原始字节验证

iOS arm64 和 iOS armv7 registrar 各有 72 个非 Factory 脚本名指针。本轮直接读取对应
UTF-16LE bytes，两个平台都是 72/72 与候选序列完全相同。Android armv7 的 42 个
EmotePlayer-local 名称直接验证，另 30 个复用已经由 Player registrar 报告验证的共享
`Player_ncb_name_*` 字符串。Android arm64 直接得到 70/72；`serialize` 是
`unserialize` 字面量内部偏移，`activateSelectorTarget` 复用相邻字面量地址，二者通过完整
disassembly 注释与其余三端同序 callback 共同确认。

这避免把 Hex-Rays 偶尔显示的单字母 `"f"`、`"s"`、`"g"` 等误当成真实脚本名。

## 7. 本地逐行对照

`cpp/plugins/motionplayer/main.cpp` 当前 `NCB_REGISTER_SUBCLASS_DELAY(EmotePlayer)` 与四端
原生表完全一致：

- 第一行 `Factory(&EmotePlayer::factory)`；
- 两个常量值和发布顺序为 1、2；
- 70 个成员的脚本名、kind 和顺序完全一致；
- 12 个 raw callback 集合无增减；
- direct `EmoteEngine` bindings、read-only properties 和 callback aliases 完全一致。

`cpp/plugins/motionplayer/EmotePlayer.cpp` 的 Factory 已保留 typed one-Variant 源结构。本轮
无需修改运行时 C++。

## 8. fresh 证据与验证状态

- 完整读取四个 registrar：1698/617/564/695 条指令；
- 完整读取四个 wrapper：89/48/32/71 条指令；
- 完整读取四个 setup：80/73/57/102 条指令；
- 完整读取 Factory publisher/invoke/construct/argument-native-build 链：
  Android arm64 publisher 内联，其余 publisher 16/20/16 条；invoke 45/48/35/35 条；
  construct 68/62/51/87 条；argument/native-build 43/45/34/55 条；
- 四端各核对 79 个唯一 callback 函数地址。iOS 两端全部为独立函数起点；Android arm64
  保留三个 chunk/internal entry；Android armv7 补建并验证唯一漏识别的 progress 函数；
- 四个 IDB 已完成 wrapper/setup/registrar/Factory 链和 raw callback 命名，关键入口已
  注释、添加 registrar/Factory 书签并原位保存；
- 73 条全部为 `EVIDENCED_4_4`：Factory 为 `FACTORY_EVIDENCED_4_4`，两个常量为
  `NOT_APPLICABLE_CONSTANT`。本注册面切片最初把其余 70 条保守记录为
  `BODY_PENDING_SEPARATE_SLICE`；后续四个 body companion slice 已将它们全部提升为
  `IMPLEMENTED`。

至此 316 个本地 NCB 候选全部具有四端 native registration evidence，`UNMAPPED=0`。
后续 body slice 已把全局 `BODY_PENDING_SEPARATE_SLICE` 从 101 降到 0、`IMPLEMENTED`
从 29 提升到 130。EmotePlayer 序号 4..22 的主流程/状态快照/raw setter，23..41 的 Player
facade，42..53 的 scale/trigger/variableKeys/animating，以及 54..73 的 late
timeline/selector 都已经接入四端完整函数闭包、owner、容器和边界报告。下一阶段不是继续
寻找 EmotePlayer 注册名或 callback，而是审计 NCB 之外由模块根、vtable、函数指针、静态
生命周期和 renderer/resource 路径可达的剩余 helper 分母。
