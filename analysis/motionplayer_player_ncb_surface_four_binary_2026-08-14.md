# Motion.Player 完整 NCB 注册面与 constructor/clear 边界四参考审计（2026-08-14）

## 结论

四份当前参考二进制共同给出同一份 `Motion.Player` 脚本发布面：

- 一个 typed constructor，源码形态为 `Player(tTJSVariant)`；
- 不注册常量；
- 恰好 92 个成员，而且 property 与 method 按固定顺序交错发布；
- 60 个 property，其中 43 个 RW、17 个 RO；
- 32 个 method，其中 27 个 ordinary typed、2 个 explicit-signature typed、3 个 raw callback；
- raw callback **只有** `setVariable`、`play`、`progress`；
- `clear` 不是 raw callback，而是绑定
  `void Player::drawToLayerRecursive_guess(tTJSVariant, tTJSVariant)` 的 generated typed method；
- constructor 只消费首个 Variant；唯一一个 Void 参数是 ncbind 的空 adaptor sentinel，
  其他 surplus 参数被接受但不读取。

本地旧实现虽然已经包含全部 92 个名字，却按“property/坐标优先、method 后置”重新分组，
因此发布顺序不等价；旧 `#70/#73` 与单 Android ARM64 地址注释也已随之错位。更重要的
是，本地曾把 `Player.clear` 改成手写 raw shim：零个或一个参数也会成功、第二个 fill
参数可省略、receiver error 与 typed adapter 不同。这不是编译器生成差异，而是脚本可见
边界偏差。本轮已经按四端真实 descriptor family 删除该 shim。

## 四端 registrar 与关键函数映射

| 角色 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `Player` registrar | `0x6D3DA8` | `0x597EC8` | `0x1001244F8` | `0x123848` |
| registrar 大小 | `0x2A30` | `0xA30` | `0xEE8` | `0xDB8` |
| registrar 指令数 | 2578 | 905 | 860 | 1005 |
| constructor callback | `0x6F3FB0` | `0x5B0798` | `0x100146384` | `0x1468E4` |
| construct + adaptor attach | `0x6F4088` | `0x5B0828` | `0x100146428` | `0x146950` |
| 独立 allocate + construct helper | `0x6F41A0` | inline in attach | inline in attach | `0x146A98` |
| `clear` native body | `0x6D0160` | `0x595720` | `0x10012139C` | `0x120168` |
| two-Variant typed creator | registrar inline | `0x5B3A98` | `0x10014A154` | `0x14B352` |
| two-Variant typed `FuncCall` | `0x6F74B4` | `0x5B3B70` | `0x10014A2A4` | `0x14B53C` |
| two-Variant member invoke | `0x6F75D0` | `0x5B3C30` | `0x10014A384` | `0x14B5D0` |

Android ARM64 为大量 descriptor 构造做了 registrar 内联，因此没有与另三端完全同形的
`createFunction` 函数边界；它仍把相同 member pointer 写入 Function dispatch object，
其 vtable 的 `FuncCall` 槽落到 `0x6F74B4`。另外三端的 creator 都被两个同签名成员复用，
所以 recovery IDB 使用源码未知名
`NCB_PlayerTwoVariantVoidMethod_createFunction_guess`，而没有把模板 specialization
武断命名为 clear-only factory。

## 精确 92 项发布顺序

下面是四个 registrar 共同的构造/发布顺序。本地 `main.cpp` 也按此逐项编号；顺序不是
文档美观问题，因为 descriptor 创建失败会令类对象停留在已经发布的前缀状态。

| # | 脚本名 | # | 脚本名 | # | 脚本名 | # | 脚本名 |
|---:|---|---:|---|---:|---|---:|---|
| 1 | `defaultSyncActive` | 24 | `stereovisionActive` | 47 | `x` | 70 | `getLayerNames` |
| 2 | `defaultTransformOrder` | 25 | `outline` | 48 | `y` | 71 | `play` |
| 3 | `resourceManager` | 26 | `meshline` | 49 | `left` | 72 | `progress` |
| 4 | `lastTime` | 27 | `maskMode` | 50 | `top` | 73 | `clear` |
| 5 | `loopTime` | 28 | `colorWeight` | 51 | `setFlip` | 74 | `stop` |
| 6 | `variableKeys` | 29 | `independentLayerInherit` | 52 | `flipX` | 75 | `setCameraOffset` |
| 7 | `chara` | 30 | `transformOrder` | 53 | `flipY` | 76 | `getCameraOffset` |
| 8 | `stealthChara` | 31 | `coordinate` | 54 | `setOpacity` | 77 | `releaseSyncWait` |
| 9 | `motion` | 32 | `zFactor` | 55 | `opacity` | 78 | `draw` |
| 10 | `stealthMotion` | 33 | `cameraTarget` | 56 | `setVisible` | 79 | `setDrawAffineTranslateMatrix` |
| 11 | `tags` | 34 | `cameraPosition` | 57 | `visible` | 80 | `contains` |
| 12 | `motionKey` | 35 | `cameraFOV` | 58 | `setSlant` | 81 | `calcViewParam` |
| 13 | `project` | 36 | `cameraAlive` | 59 | `slantX` | 82 | `getCommandList` |
| 14 | `completionType` | 37 | `bounds` | 60 | `slantY` | 83 | `getLayerMotion` |
| 15 | `preview` | 38 | `playing` | 61 | `setZoom` | 84 | `getLayerGetter` |
| 16 | `priorDraw` | 39 | `allplaying` | 62 | `zoomX` | 85 | `getLayerGetterList` |
| 17 | `outsideFactor` | 40 | `syncWaiting` | 63 | `zoomY` | 86 | `skipToSync` |
| 18 | `meshDivisionRatio` | 41 | `frameLastTime` | 64 | `useD3D` | 87 | `onAction` |
| 19 | `speed` | 42 | `frameLoopTime` | 65 | `pixelateDivision` | 88 | `onSync` |
| 20 | `syncActive` | 43 | `hasCamera` | 66 | `setVariable` | 89 | `onGroundCorrection` |
| 21 | `tickCount` | 44 | `angleDeg` | 67 | `getVariable` | 90 | `onFindMotion` |
| 22 | `frameTickCount` | 45 | `angleRad` | 68 | `modifyRoot` | 91 | `isExistMotion` |
| 23 | `cameraActive` | 46 | `setCoord` | 69 | `processedMeshVerticesNum` | 92 | `setStereovisionCameraPosition` |

四端都在第 92 项之后才进入一次性的 Bezier 4x4 identity-grid 初始化与 ARM/NEON
evaluator promotion；这段不是额外的脚本成员。

## Descriptor family 精确分类

### 43 个 typed RW property

`defaultSyncActive`, `defaultTransformOrder`, `chara`, `stealthChara`, `motion`,
`stealthMotion`, `motionKey`, `project`, `completionType`, `preview`, `priorDraw`,
`outsideFactor`, `meshDivisionRatio`, `speed`, `syncActive`, `tickCount`,
`frameTickCount`, `cameraActive`, `stereovisionActive`, `outline`, `meshline`,
`maskMode`, `colorWeight`, `independentLayerInherit`, `transformOrder`, `coordinate`,
`zFactor`, `angleDeg`, `angleRad`, `x`, `y`, `left`, `top`, `flipX`, `flipY`,
`opacity`, `visible`, `slantX`, `slantY`, `zoomX`, `zoomY`, `useD3D`,
`pixelateDivision`。

### 17 个 typed RO property

`resourceManager`, `lastTime`, `loopTime`, `variableKeys`, `tags`, `cameraTarget`,
`cameraPosition`, `cameraFOV`, `cameraAlive`, `bounds`, `playing`, `allplaying`,
`syncWaiting`, `frameLastTime`, `frameLoopTime`, `hasCamera`,
`processedMeshVerticesNum`。

### 27 个 ordinary typed method

`setCoord`, `setFlip`, `setOpacity`, `setVisible`, `setSlant`, `setZoom`,
`getVariable`, `modifyRoot`, `getLayerNames`, `stop`, `setCameraOffset`,
`getCameraOffset`, `releaseSyncWait`, `setDrawAffineTranslateMatrix`, `contains`,
`calcViewParam`, `getCommandList`, `getLayerMotion`, `getLayerGetter`,
`getLayerGetterList`, `skipToSync`, `onAction`, `onSync`, `onGroundCorrection`,
`onFindMotion`, `isExistMotion`, `setStereovisionCameraPosition`。

### 2 个 explicit-signature typed method

| 脚本名 | 绑定目标 | typed 形态 |
|---|---|---|
| `clear` | `Player::drawToLayerRecursive_guess` | `void(tTJSVariant, tTJSVariant)` |
| `draw` | `Player::draw` complete render dispatcher | `void(tTJSVariant)`; direct stored target, zero adjustment |

`clear` 的 C++ member 名和脚本名不同，正是旧端口最容易误判为 raw callback 的原因。
四端 registrar 的 descriptor object、stored member pointer 和 Function vtable 共同证明它
仍是 generated typed specialization。

member 78 `draw` 的 2026-08-16 fresh registrar/body/typed-adapter 复核还确认：四端都直接
保存完整 `Player::draw(tTJSVariant)` renderer 与零 adjustment，并不存在第二个
`Player::drawCompat(Variant*)` native member。typed adapter 产生的 argv[0] 按值副本就是
draw 参数，body 直接在该参数上执行 D3D/SLA/ordinary routing。端口已删除人为 helper
call edge 并将完整 body 收回 `Player::draw`；详见
`analysis/motionplayer_player_draw_direct_typed_entry_four_binary_2026-08-16.md`。

同轮 UTF-16LE `captureCanvas` 搜索在每端都只有一个字符串，所有 xref 均属于
`D3DAdaptor` registrar；92 项 Player 表没有该名称。本地未注册、零调用的
`Player::captureCanvasCompat` raw callback 是 port 残留，已经删除。2026-08-16 后续又以
fresh draw-target xref 与同一 string/xref 集确认无参 `Player::captureCanvas()/draw()`
同样没有 production/native source edge，现已一并删除；D3DAdaptor 的真实一参数成员不变。

### 3 个 native-instance raw callback

`setVariable`, `play`, `progress`。

它们经 raw callback descriptor 保存普通回调函数指针，不携带 typed member-function
pointer。Android ARM64 的 `play`/`progress` descriptor 分别直接保存
`0x6CFFE8`/`0x6CFE78`；iOS ARM64 的 `setVariable` 使用只出现一次的 raw creator
`0x100125890`，`play`/`progress` 共用另一个 raw creator `0x1001258E4`。这与
`clear` 使用 `0x10014A154 -> 0x10014A2A4` 的 typed Function object 链截然不同。

## Constructor bridge、分配与 adaptor attach

四份 callback 的共同高层顺序为：

```text
if membername != null:
    return TJS_E_MEMBERNOTFOUND

if numparams == 1 and param[0].Type == Void:
    return TJS_S_OK                  // empty-adaptor sentinel; no allocation

clear result when result != null
if numparams < 1:
    return TJS_E_BADPARAMCOUNT

copy param[0] into an owning temporary Variant
allocate and construct Player(temp)
destroy temp
resolve Player adaptor slot from objthis
if slot missing:
    destroy Player
    free allocation
    return TJS_E_NATIVECLASSCRASH
slot.native = Player*
return TJS_S_OK
```

这里有几个容易被普通“单参数 constructor”描述丢失的边界：

- 恰好一个 Void 参数的 sentinel 分支早于 result clear，因此它不分配 Player，也不改
  既有 result；它供 ncbind 创建空 adaptor 壳使用。
- 零参数不是默认构造，而是 `TJS_E_BADPARAMCOUNT`；constructor 的 C++ default argument
  不代表脚本 bridge 接受零参数。
- 两个及更多参数会通过 lower-bound gate，但只有 `param[0]` 被 CopyRef；其余参数完全
  不读取。
- 普通非 Void `param[0]` 即使本身不是 ResourceManager object，也先按 Variant 原样拥有，
  再进入 Player constructor 的既有语义。
- receiver/adaptor attach 在完整构造之后；attach 失败必须走 Player 析构和 operator
  delete，而不是泄漏半发布对象。
- constructor 抛异常时，已经完成的 Variant/Player 子对象按各端异常表清理并继续抛出；
  不把 C++ exception 改写成成功或 null instance。

四端完整 Player allocation size 分别为 Android ARM64 `0x568`、Android ARMv7
`0x3B0`、iOS ARM64 `0x4B8`、iOS ARMv7 `0x348`。这些差异来自目标 ABI、STL 与
32/64 位布局，不能据此虚构四份不同源码类。attach 成功后，64 位 adaptor 把 native
pointer 写入 `+8`，32 位写入 `+4`。

## `clear` typed `FuncCall` 的精确边界

四个 generated wrapper 都收敛为相同的可观察次序：

```text
if membername != null:
    return TJS_E_MEMBERNOTFOUND       // result untouched
if objthis == null:
    return TJS_E_NATIVECLASSCRASH     // result untouched
if result != null:
    result.Clear()
if numparams < 2:
    return TJS_E_BADPARAMCOUNT

resolve Player native instance from objthis
if resolution fails:
    return TJS_E_NATIVECLASSCRASH

target = owning CopyRef(param[0])
fill   = owning CopyRef(param[1])
invoke stored void Player member(target, fill)
destroy fill
destroy target
return TJS_S_OK
```

由此得到以下脚本边界：

- `clear()` 与 `clear(target)` 都必须返回 `TJS_E_BADPARAMCOUNT`；不存在 optional fill。
- null receiver 比 argc gate 更早，因此即使只给一个参数也返回
  `TJS_E_NATIVECLASSCRASH`，并保留调用前 result。
- 非 null 但错误类型的 receiver 先清 result、再做 argc gate：一个参数返回
  `TJS_E_BADPARAMCOUNT`；两个参数才进入 native unwrap 并返回
  `TJS_E_NATIVECLASSCRASH`。
- 三个及更多参数成功通过；wrapper 只 CopyRef `param[0]`/`param[1]`，不读取 surplus。
- void return 不发布 Boolean true；正常成功后的 result 保持 Void。
- typed adapter 在进入 Player body 之前就拥有两个 Variant。即使 Player 没有 motion、
  body 立即 no-op，两次 CopyRef/逆序析构边界仍然存在。
- Android ARM64 的 native unwrap 被编译为直接调用 objthis 的
  `NativeInstanceSupport` vtable 槽，另三端更多地保留共享 helper；返回语义相同。

本地旧 `Player::clearCompat` 没有最低参数门：无 motion 时零参数也成功，有 motion 时
一个 target 加 optional Void fill 也成功；它还手写 receiver error 与 result clear。这些
行为均已删除。2026-08-15 对另一张 `Motion.EmotePlayer` 注册表 fresh复核后确认，它的
`clear` 同样是 typed two-Variant method，而不是此前记录的 optional raw callback；两张
表拥有各自 stored member pointer/creator，但共享相同 arity与 Player body。见
`analysis/motionplayer_emoteplayer_clear_contains_typed_four_binary_2026-08-15.md`。

## `clear` body 与本审计的边界

`clear` 的 stored member pointer 在四端都直接指向既有
`Player_drawToLayerRecursive_guess`：

1. 无 motion content 立即成功返回；
2. D3DAdaptor target 走颜色清理后立即返回；
3. SeparateLayerAdaptor target 先替换为其内部 targetLayer；
4. 原生 Layer 存在但没有 MainImage 时停止，不 fill、也不递归；
5. object fill 作为四整数参数 callable；非 object fill 经全局 Layer 类的五参
   `fillRect`；
6. 最后只递归 index 1 起、type 3 的 child Player，每一层重新拥有 target/fill。

该 body 的 class-ID、Layer MainImage gate、callable/ordinary fill 与递归生命周期已经在
`analysis/motionplayer_draw_to_layer_four_binary_2026-08-11.md` 完整闭合。本文件新增的
关键结论是：`Motion.Player.clear` 自己也通过 typed two-Variant adapter 进入该 body；
此前只审计 `Motion.EmotePlayer.clear` wrapper 并不能证明 Player 注册表应使用 raw shim。

## 本地修正

- `main.cpp` 将 92 项从分组式布局改为四端精确交错顺序，并逐项编号。
- 删除 Player block 的旧单目标绝对地址注释；绝对地址只保留在本分析文档。
- 将 raw `clear` 注册改为 explicit typed detail，绑定
  `Player::drawToLayerRecursive_guess(tTJSVariant, tTJSVariant)`。
- 删除 `Player::clearCompat` 声明与实现；后续四端复核也删除了错误的
  `EmotePlayer::clearCompat`，恢复 typed `EmotePlayer::clear(Variant, Variant)`。
- `PlayerTimeline.cpp` 明确 body 接收 typed adapter 已拥有的两个 Variant，且无 motion
  是成功 no-op。
- 新增 registered-method 回归：覆盖 membername、null receiver、wrong receiver、result
  clear、二参数 lower bound、surplus acceptance 与 void result。
- 修正旧 lifecycle 文档中把 Player body 同时写成
  `drawToLayerCompat / clearCompat` 的过时表述。2026-08-16 的 fresh registrar、xref、
  worker 与 two-Variant adapter 复核进一步确认该 worker 本身就是直接 stored typed target，
  不是 compatibility shim；本地成员因此改用未知源码名的语义
  `drawToLayerRecursive_guess`。

## Recovery IDB 改进与验证

四份 recovery IDB 已统一完成：

- registrar 与 `clear` registration site 写入 92 项/三 raw/typed clear 注释；
- three non-inlined creator 分别命名为
  `NCB_PlayerTwoVariantVoidMethod_createFunction_guess`；
- allocation/Function constructor、`FuncCall`、member invoke 与可独立识别的 param0/param1
  CopyRef helper 按 `_guess` 规则命名；
- 四个 `FuncCall` 应用同一份八参数 iTJSDispatch 形态 prototype；
- constructor callback、construct/attach、allocate helper、typed creator、`FuncCall` 与
  invoke 在 rename/type/comment 后全部 fresh decompile，无 decompiler error；
- 四份 IDB 随后原位保存。

源码验证结果：

- 结构扫描：constructor `1`、member `92`、顺序逐项相等、raw callback 精确等于
  `setVariable/play/progress`、`clear` 精确绑定二 Variant typed detail、Player
  `clearCompat` 声明/定义为零。
- 整份 `motionplayer-dll.cpp` Emscripten TU syntax check 通过；只保留仓库既有 `_tss`
  literal-operator deprecation warning。
- `cmake --build --preset "Web Debug Build"` 重新编译和链接通过。
- 当前工程没有配置可直接运行这份 Catch2 motionplayer TU 的 native unit executable；
  新测试完成整 TU 编译验证，但没有伪造运行结论。
