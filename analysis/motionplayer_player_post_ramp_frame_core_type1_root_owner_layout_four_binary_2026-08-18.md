# Player post-ramp frame-core / type-1 / root owner 连续布局（V250，2026-08-18）

## 1. 结论

V250 从 V249 的 `ParameterRampMap` 后继续，闭合到 `rootContentVariant` 末端。四份参考共享的
源码声明顺序是：

```text
double evaluationCursor
double emoteAngle
double cameraAngle
bool queuing
bool firstFrame
bool directEdit
bool motionCompleted
tTJSVariant emoteDivision
int32 emoteMotionIndex          // constructor deliberately leaves it uninitialized
tTJSVariant emoteMotionList
tTJSVariant motionContent
tTJSVariant priorityFrameSource
int32 rootFrameCursor
[ABI alignment before double when required]
double rootCurrentTime
double rootNextTime
double frameDelta
double cameraDamping
bool noUpdateYet
bool reverseSeek
bool cameraConstraintDirty
bool drawAffineMatrixNonIdentity
bool internalRenderLayerReady
bool needsInternalAssignImages
[2 bytes ABI padding before Variant]
tTJSVariant rootContent
```

这条连续链同时纠正两项 portable 旧布局：

1. ramp 后三个 double 不是 `evaluation/loopTime/lastTime`，而是
   `evaluation/emoteAngle/cameraAngle`；真正的 frame last/loop pair 在对象后部；
2. `_needsInternalAssignImages` 与 `_internalRenderLayerReady` 的旧声明顺序相反。native 先放
   consumer snapshot `internalRenderLayerReady`，再放 producer `needsInternalAssignImages`。

V249 的 Android armv7 ramp-tree size 也在本轮纠正：object 从 `+0x108` 占 24 bytes 到
`+0x120`，不是 28 bytes 到 `+0x124`。`+0x120` 已是 evaluation cursor 的首字节。

## 2. 四端完整 offset 矩阵

| 成员 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| ramp tree end / evaluation cursor | `+0x1C8` | `+0x120` | `+0x158` | `+0xE4` |
| emoteAngle | `+0x1D0` | `+0x128` | `+0x160` | `+0xEC` |
| cameraAngle | `+0x1D8` | `+0x130` | `+0x168` | `+0xF4` |
| queuing | `+0x1E0` | `+0x138` | `+0x170` | `+0xFC` |
| firstFrame | `+0x1E1` | `+0x139` | `+0x171` | `+0xFD` |
| directEdit | `+0x1E2` | `+0x13A` | `+0x172` | `+0xFE` |
| motionCompleted | `+0x1E3` | `+0x13B` | `+0x173` | `+0xFF` |
| division Variant | `+0x1E4` | `+0x13C` | `+0x174` | `+0x100` |
| emoteMotionIndex | `+0x1F8` | `+0x148` | `+0x188` | `+0x10C` |
| motionList Variant | `+0x1FC` | `+0x14C` | `+0x18C` | `+0x110` |
| motionContent Variant | `+0x210` | `+0x158` | `+0x1A0` | `+0x11C` |
| priorityFrameSource Variant | `+0x224` | `+0x164` | `+0x1B4` | `+0x128` |
| rootFrameCursor | `+0x238` | `+0x170` | `+0x1C8` | `+0x134` |
| root current/next time | `+0x240/+0x248` | `+0x178/+0x180` | `+0x1D0/+0x1D8` | `+0x138/+0x140` |
| frame delta | `+0x250` | `+0x188` | `+0x1E0` | `+0x148` |
| camera damping | `+0x258` | `+0x190` | `+0x1E8` | `+0x150` |
| noUpdate/reverse/constraint/affine bytes | `+0x260..+0x263` | `+0x198..+0x19B` | `+0x1F0..+0x1F3` | `+0x158..+0x15B` |
| internalReady / needsAssign | `+0x264/+0x265` | `+0x19C/+0x19D` | `+0x1F4/+0x1F5` | `+0x15C/+0x15D` |
| rootContent Variant | `+0x268` | `+0x1A0` | `+0x1F8` | `+0x160` |
| next owner: findSource ResourceManager | `+0x27C` | `+0x1AC` | `+0x20C` | `+0x16C` |

`tTJSVariant` 在两份 64 位 ABI 中占 20 bytes，在两份 32 位 ABI 中占 12 bytes。priority owner
后跟一个 int32 cursor；Android arm64/armv7 与 iOS arm64 需要为后继 double 插入 4-byte padding，
iOS armv7 的 double 只要求 4-byte alignment，所以 cursor 后直接开始 rootCurrentTime。

两个 render-layer bool 后均有 2-byte padding，使 rootContent 按 4-byte Variant alignment 开始。
这些是 ABI padding，不是第三、第四个 render-layer flag。

## 3. post-ramp 三 double：旧 loop/last 解释的反证

四个 constructor 都将整组三 double 清为 `+0.0`：

| 目标 | constructor zero |
| --- | ---: |
| Android arm64 | `0x6CC48C/0x6CC494` |
| Android armv7 | `0x59379E..0x5937BE`，24-byte `__aeabi_memclr8` |
| iOS arm64 | `0x10011EE14/0x10011EE18` |
| iOS armv7 | `0x11D7D8..0x11D7F0` |

只看这一组零写无法命名后两个 double。direct writers/readers 给出身份：

### 3.1 emoteAngle

- type-1 play 首次进入 direct-edit 时从 Engine angle 槽复制到第二个 double；
- `Player_setAngleDeg_guess` / `Player_setAngleRad_guess` 直接覆盖它并重新选择 emote motion；
- `Player_initEmoteMotion_guess` 同时读取 emoteAngle 与 cameraAngle；
- child/particle propagation 复制同一 Player angle 状态。

四端最直接的 set-angle stores 是：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| ---: | ---: | ---: | ---: |
| `0x6BE3B0` | `0x58A588` | `0x100113A10` | `0x111424` |

### 3.2 cameraAngle

`Player_updateCameraNode_guess` 的 cameraActive path 将 camera-to-target normalized angle 写入第三个
double；motion child 与 particle child 再复制它。代表性 writer：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| ---: | ---: | ---: | ---: |
| `0x6BB044/0x6BB090` | `0x587910/0x587948` | `0x100110AB0/0x100110B0C` | `0x10E212/0x10E24A` |

### 3.3 真正的 lastTime / loopTime

property getters 读取完全不同的后部 pair：

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| frameLastTime | `+0x468` | `+0x310` | `+0x3F8` | `+0x2CC` |
| frameLoopTime | `+0x470` | `+0x318` | `+0x400` | `+0x2D4` |
| direct getter pair | `0x6D6B84/0x6D6B8C` | `0x598FF6/0x599000` | `0x1001256D8/0x1001256E0` | `0x1248F8/0x124902` |

所以本地 `_loopTime/_cachedTotalFrames` 被移出 post-ramp prefix；其更晚的完整邻接仍留给后续布局
纵切面，不用当前临时 declaration order 冒充已闭合 native offset。

## 4. 四个 frame-state bytes

constructor 最终状态共同为：

```text
queuing        = true
firstFrame     = false
directEdit     = false
motionCompleted= false
```

Android 两端常把前两个 byte 合成 little-endian halfword `0x0001`；iOS arm64 分开写；iOS armv7
也把四个 byte 分散调度。字段身份来自完整读写集：

- queuing 阻止排队阶段普通 raw-cursor 累加，并作为 geometry first-update gate；
- firstFrame 是一次性 reseek/反向首帧播种状态；
- directEdit 由 type-1 play 置一、ordinary play 清零，并控制 Player emote-angle motion route；
- motionCompleted 在每次 frameProgress 入口清零，由 align/sync/nested work 置一作为 cooperative stop。

它们是四个独立 `bool`，不是一个 flags word。constructor 的 halfword store 只是 optimizer 合并，公开
setter 和内部 writers 仍逐 byte 操作。

## 5. type-1 owner cluster 与 partial commit

constructor 只把五个 Variant 的 type tag 初始化为 Void：

| 目标 | tag-zero group |
| --- | ---: |
| Android arm64 | `0x6CC250..0x6CC260` |
| Android armv7 | `0x593670..0x593680`（以 vector-base-relative stores 表达） |
| iOS arm64 | `0x10011EC98..0x10011ECAC` |
| iOS armv7 | `0x11D566`、`0x11D574`、`0x11D582`、`0x11D590` 及 root tag |

type-1 `playImpl` 的共同顺序是：

```text
if !directEdit:
    emoteAngle = Engine.angle
    Engine.angle = 0
directEdit = true

division = motion["division"]       // independent Variant assignment
motionList = motion["motionList"]   // independent Variant assignment
emoteMotionIndex = -1
initEmoteMotion(playFlags)
```

第二个 property getter/copy 失败不会回滚已经提交的 division owner；index 在第二个 assignment 成功
后才写 `-1`。constructor 对 index 没有任何 store，所以 portable `int _emoteMotionIndex;` 必须保持
无 initializer。为“安全”写 0 或 -1 会改变构造后但首次 type-1 play 前的原始边界。

`motionContent` 是成功 load 的唯一持久 loaded-content owner。其后紧邻 priority frame source，
priority 后才是 root cursor/current/next；tag frame source 与 layer cursor位于对象更后部，不能因为
运行时 initializer 按 tag→priority→rootContent 赋值，就把 tag 声明也塞进这条物理 cluster。

## 6. root cursor、delta/damping 与控制字节

root stream 的 forward/rewind/reseek 都直接读写这里的 priority owner、cursor 与 current/next times。
cursor 是 wrapping int32；current/next 是 double。priority getter 或 frame getter 抛出时，已完成的
cursor/content/time assignments 不回滚。

frameProgress 在任何后续 early return 前执行：

```text
motionCompleted = false
deltaTime = speed * inputDt
```

camera damping 紧随 delta；updateLayers 积分 camera velocity 后，仅在 damping 精确不等于 `1.0` 时
运行 pow attenuation。后面的四 byte 分别是：

- noUpdateYet：首次 derivative/particle path；
- reverseSeek：普通 first-frame full reseek 的方向 latch；
- cameraConstraintDirty：跨帧 force-dirty；
- drawAffineMatrixNonIdentity：六参数 affine setter 每次重算，不是 sticky。

这些 V247 已闭合的角色在 V250 被移回它们相对 root owner 的真实位置。

## 7. internal-render producer/consumer pair

紧随四 control bytes 的两个 bool 的原生顺序是：

```text
internalRenderLayerReady
needsInternalAssignImages
```

四端 `updateLayers` 入口都清第二个 byte：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| ---: | ---: | ---: | ---: |
| `0x6B8748`：`+0x265` | `0x585700`：`+0x19D` | `0x10010E570`：`+0x1F5` | `0x10BE7C`：`+0x15D` |

anchor type 10 可重新置 producer。ordinary/accurate post-draw 都先执行：

```text
internalRenderLayerReady = needsInternalAssignImages
if !needsInternalAssignImages:
    return
```

所以 earlier byte 是 consumer snapshot，later byte 是 producer；旧 portable declaration 先 needs、后 ready
会反转物理地址。本轮把二者移动到 damping-control group 后并交换成参考顺序。

## 8. rootContent owner 与析构/异常回滚

rootContent 是本轮连续区的最后一个 owner。正常 destructor 的 reverse order：

| owner | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| rootContent | `0x6CD0FC` | `0x593D2C` | `0x10011F3C0` | `0x11DE50` |
| priorityFrameSource | `0x6CD104` | `0x593D34` | `0x10011F3C8` | `0x11DE5A` |
| motionContent | `0x6CD10C` | `0x593D3C` | `0x10011F3D0` | `0x11DE64` |
| motionList | `0x6CD114` | `0x593D44` | `0x10011F3D8` | `0x11DE6E` |
| division | `0x6CD11C` | `0x593D4C` | `0x10011F3E0` | `0x11DE78` |
| then ramp map | `0x6CD124` | `0x593D54` | `0x10011F3E8` | `0x11DE80` |

constructor unwind tails保持相同的“只销毁已经构造完成 member”的逆序关系；iOS armv7 SJLJ landing
pad 尤其清楚地从 later Variant owners 落到这五项，再落到 ramp/vector/HM2/HM1/deque/map。
raw emoteMotionIndex、六个 bool 和 scalar doubles 都没有 owner destructor。

rootContent 结束后的直接 member 已由既有 source-workspace 纵切面确定为 retained ResourceManager
Variant；V251 将从该 owner 开始继续，而不是把当前临时 `_chara/_motionKey/_outline` 当成直接后继。

## 9. portable 源码修改

`cpp/plugins/motionplayer/Player.h` 已：

- 将 evaluation/emoteAngle/cameraAngle 三 double 移到 ramp map 后；
- 紧接四个 frame-state bytes；
- 移回 division/index/motionList/motionContent/priority owner cluster；
- 移回 root cursor/current/next、delta/damping 与四 control bytes；
- 将 internalReady/needsAssign 移回并纠正先后；
- 将 rootContent 放在 pair 后；
- 从后部旧位置删除所有重复声明；
- 让 later tag owner 与 layer cursor继续保持独立，未伪造 tag 与 root 物理邻接；
- 将 loopTime/lastTime 注释改成明确的 late pair，不再声称属于 post-ramp triple。

没有改写 public property、play、frameProgress、updateLayers 或 post-draw 算法；变化集中在 class layout、
automatic owner lifetime 和过时注释。

## 10. IDB 写回和 iOS armv7 安全保存

写回统计：

| IDB | comments | bookmarks | renames |
| --- | ---: | ---: | ---: |
| Android arm64 | 6 | 6 | 0 |
| Android armv7 | 8 | 6 | 0 |
| iOS arm64 | 6 | 6 | 0 |
| iOS armv7 | 6 | 6 | 3 |
| total | 26 | 24 | 3 |

Android armv7 多出的两条 comment 专门纠正：

- NodeLabelMap 是 24-byte object，后有 4-byte double alignment padding；
- ParameterRampMap 是 24-byte object，从 `+0x108` 到 `+0x120`，无后继 padding。

iOS armv7 恢复三个四端已证实、仍保持 `_guess` 的 private identities：

- `Player_playImpl_guess`；
- `Player_getFrameLastTime_guess`；
- `Player_getFrameLoopTime_guess`。

iOS armv7 使用 different-path packed save：

- V249 canonical 备份：
  `out/idb-recovery/v250-ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.pre-v250.i64`；
- V250 candidate：同目录 `Kirikiroid2_1.3.9_iOS_armv7.v250.i64`；
- candidate 经独立 `C:\IDA\idat.exe -A` probe，退出码 0；
- V249 loose `id0/id1/nam` 移入 `pre-v250-canonical-loose/`；
- candidate 安装为 canonical 后，MCP reopen 成功读回三个新名称、V248 camera/bounds 名和 V250 comments；
- candidate/canonical 都是 376,887,504 bytes，SHA-256
  `C503D767998DF097F093A76183F10226CAFAA4767E37103D0D2CCD673072AF63`。

四份最终 V250 IDB：

| IDB | size | SHA-256 |
| --- | ---: | --- |
| Android arm64 | 366,656,687 | `3775B41B4842F54CB0649CB6D69F0B238FE4E5C967174262A0EE035DFA76436B` |
| Android armv7 | 345,642,531 | `D769BBA9A804F0065FA4B4BCE180E318FB4128C0A9A6829F65A00F9CF0681DD3` |
| iOS arm64 | 334,624,001 | `63B22E27CE77A71254B415BE381D721081B7880BDD9E0868F9C2527973690B6B` |
| iOS armv7 | 376,887,504 | `C503D767998DF097F093A76183F10226CAFAA4767E37103D0D2CCD673072AF63` |

## 11. 验证与最终 wasm 基线

- complete motionplayer Catch2 TU ordinary/headless syntax：通过，仅既有 `_tss` warning；
- Web：33-step affected rebuild 通过；
- Wasmtime：62-step main/guest-object rebuild 完成，随后复验 `ninja: no work to do.`；
- `krkr2_wasmtime_guest`：2-step build/link 通过并完成 exnref 转换；
- Web、Wasmtime、guest 三条 build 命令 no-work 复验通过；
- scoped `git diff --check`：无 whitespace error，仅工作树既有 LF/CRLF 提示；
- IDA session audit：0。

| product | size | FUNCTION | CODE | DATA | name | SHA-256 |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Web `index.wasm` | 85,655,362 | `0x1BD31` | `0x1A410C5` | `0x5A3E40` | `0x3185F7B` | `4C4A91E42190358D6D52AE3B2459277E4FEFD46DCE2D556CE6151F68E738805A` |
| Wasmtime `index.wasm` | 85,002,503 | `0x1BA50` | `0x19E9073` | `0x5A1090` | `0x3141E11` | `B6C725811AE30840AB9A709ED316B3FC903C03D4E94D7977F655D6BBC7BE5769` |
| Wasmtime guest | 151,478,410 | `0x1618E` | `0x13D7DED` | `0x4D1630` | `0x1421EBA` | `B6E2D2CA488D97B4573CCF96AAC602F1A8E8FB79DBA6FA5B1D9F0B17B676DE9F` |

相对 V249，Web/Wasmtime 主模块的总大小和 FUNCTION/CODE/DATA/name section size 再次完全不变，
但 hash 改变；这与大量 field displacement 和等长 destructor calls 的重排一致。guest 总大小增加
43 bytes，CODE 增加 2 bytes，列出的 FUNCTION/DATA/name size 不变，其余 41-byte 净变化位于未列
小节/自定义调试 metadata。

## 12. V251 follow-up

V251 已闭合 rootContent 后的六个连续 workspace Variant 与 raw SeparateLayerAdaptor owner。共同顺序为
find-source ResourceManager、source-cache ResourceManager、descriptor、primary Layer、colors、work Layer、
raw adaptor pointer；四端 pointer 结束后直接进入 pending stealth pair，不存在 `_chara/_motionKey/_outline`
或其他中间 owner。normal dtor、constructor unwind、primary-first sticky partial commit及 raw pointer的
pointee-dtor→free→slot-null时序见
`analysis/motionplayer_player_source_workspace_raw_adaptor_layout_four_binary_2026-08-18.md`。
