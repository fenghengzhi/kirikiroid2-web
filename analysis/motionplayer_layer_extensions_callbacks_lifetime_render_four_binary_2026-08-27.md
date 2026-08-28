# `MotionLayerExtensions` 九个回调与 per-Layer 状态四二进制联合恢复

日期：2026-08-27

## 1. 本 slice 的闭合范围

本报告闭合通过 lazy instance hook 直接附加到脚本 `Layer` 的九个成员 body：

- `debugMeshApp/debugBezierApp` 两个 read/write Variant property；
- `meshCopy/operateMesh/bezierPatchCopy/operateBezierPatch` 四个 mesh render 入口；
- `drawMeshFrame/drawBezierPatchFrame/drawBezierPatchMeshFrame` 三个脚本可见 frame helper。

同时闭合 per-Layer payload 字段/owner、face/type/mode 状态机、clear、clip、plain/Bezier
render core、公共 mesh submit、debug grid/control-frame、TJS Array 嵌套容器、member hint、
异常 partial commit 和所有负 division/空 appearance/method-null 边界。四个参考二进制共同
构成权威。

## 2. callback 地址与 fresh 指令覆盖

两个 property 的 getter/setter 指令数：

| property callback | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `debugMeshApp get/set` | `0x6A1768/0x6A1774`，3/2 | `0x578BB4/0x578BC0`，5/2 | `0x1000FE1C8/0x1000FE1D4`，3/2 | `0xFB11E/0xFB12A`，5/2 |
| `debugBezierApp get/set` | `0x6A177C/0x6A1788`，3/2 | `0x578BC6/0x578BD2`，5/2 | `0x1000FE1DC/0x1000FE1E8`，3/2 | `0xFB130/0xFB13C`，5/2 |

七个 method：

| callback | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `meshCopy` | `0x69F150`，105 | `0x577924`，88 | `0x1000FC6E8`，76 | `0xF9654`，124 |
| `operateMesh` | `0x69F304`，181 | `0x577A44`，82 | `0x1000FC864`，72 | `0xF97F4`，113 |
| `drawMeshFrame` | `0x69F5E4`，481 | `0x577B50`，286 | `0x1000FC9C0`，259 | `0xF996C`，406 |
| `bezierPatchCopy` | `0x69FD7C`，105 | `0x577F3C`，89 | `0x1000FCF78`，76 | `0xF9F08`，125 |
| `operateBezierPatch` | `0x69FF30`，181 | `0x57805C`，82 | `0x1000FD0F4`，72 | `0xFA0A8`，113 |
| `drawBezierPatchFrame` | `0x6A0210`，582 | `0x578168`，354 | `0x1000FD250`，408 | `0xFA220`，577 |
| `drawBezierPatchMeshFrame` | `0x6A0B3C`，392 | `0x5786AC`，254 | `0x1000FDAF8`，244 | `0xFAAA4`，367 |

44 个 callback 函数全部完成 fresh decompile/disassembly。核心 helper 指令覆盖：

| helper | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| refresh face | `0x69AB18`，104 | `0x57551C`，60 | `0x1000F936C`，49 | `0xF63F8`，85 |
| auto mode | `0x69ACD0`，75 | `0x5755E4`，47 | `0x1000F94CC`，28 | `0xF6558`，66 |
| mode → bitmap method | callback 内联 | `0x577684`，101 | `0x1000FC29C`，107 | `0xF9328`，99 |
| clear whole Layer | `0x69EF1C`，136 | `0x577774`，120 | `0x1000FC4B8`，103 | `0xF9410`，159 |
| render plain mesh | `0x69E0D0`，338 | `0x576E08`，255 | `0x1000FB660`，224 | `0xF86B0`，353 |
| render Bezier mesh | `0x69E630`，239 | `0x577184`，208 | `0x1000FBBB8`，201 | `0xF8C00`，314 |
| submit mesh | `0x69CD80`，123 | `0x576288`，113 | `0x1000FA650`，113 | `0xF77CC`，163 |
| debug grid | `0x69D1F0`，320 | `0x5765C8`，207 | `0x1000FAB34`，181 | `0xF7C04`，286 |
| debug Bezier control | `0x69D7B0`，413 | `0x5768E8`，253 | `0x1000FB054`，213 | `0xF80C0`，332 |

以上 35 个 helper body 也全部 fresh 读取；parse/tessellate、coordinate probe 和更深的公共
mesh triangle helper复用相邻 BezierPatch/mesh slice 已闭合的四端等价类。

## 3. per-Layer payload 与 Variant 生命周期

四端共同的源级字段顺序是：

```text
owner             : iTJSDispatch2*      # borrowed
faceCache         : tjs_int             # 初值 0
debugMeshApp      : tTJSVariant         # 初值 Void
debugBezierApp    : tTJSVariant         # 初值 Void
```

可见偏移为 64 位 `debugMesh=+12/debugBezier=+32`，32 位
`debugMesh=+8/debugBezier=+20`。差异来自 owner 指针和 Variant ABI；不能在源码添加 padding。
getter 是 Variant CopyRef，setter 是 Variant copy assignment：新 Object/ObjThis owner 先被
保留，旧值随后释放。payload 析构按成员逆序释放 debugBezier、debugMesh；`owner` 从不
AddRef/Release。

attach hook 在首次访问任一九成员时查询 Layer 已附加实例；不存在才
`new Payload(objthis)` 并写回 Layer native slot。后续调用共享 face cache 和两个 debug
Variant。同一 payload 由 Layer/adaptor teardown 销毁，不是 registrar 全局对象；allocation
或 attach 失败前不会发布可用实例。这个 hook 生命周期已由注册面报告的四端
attach/setup/unregister 链与本轮字段访问共同确认。

## 4. face/type 与 bitmap-method 状态机

每次 copy 或 operate 都重新 refresh face；cache 不是跨调用的粘滞脚本值：

```text
faceCache = owner.getIntValue("face", hint=null)
if faceCache == 128:
    type = owner.getIntValue("type", hint=null)
    if type == 2 or 13 <= type <= 28: faceCache = 0
    else if type == 12:               faceCache = 4
    else:                             faceCache = 1
```

`getIntValue` 是 null-hint MEMBERMUSTEXIST probe，再在成功时做 flags=0 Integer read；失败
probe 贡献 0 且没有第二次读取。auto mode 先独立读取 `type`，按 signed comparison返回
`type > 28 ? 1 : type`；负数原样保留。之后 mode resolver 又独立 refresh face。

精确 mapping：

| mode | face 0 | face 1 | face 4 | 其他 |
|---:|---:|---:|---:|---|
| 1 | 1 | 0 | 15 | fail |
| 2 | 3 | 2 | 14 | fail |
| 12 | 13 | 11 | 12 | fail |

固定 mapping 为 `3→4, 4→5, 5→6, 8→7, 9→8, 10→9, 11→10`；
`13..28→mode+3`。其余 mode 失败。mode 128 先经 auto mode。失败立即抛
`"operateMesh: not drawable face type."`；Bezier operate 故意复用同一条
`operateMesh` 文本。

## 5. copy/operate wrapper 的共同流程

copy 两条：

```text
ownerAccessor = retain(owner)
refreshFace()
holdAlpha = false
if face == 1: holdAlpha = owner.holdAlpha as bool
else if face != 0 && face != 4:
    throw meshCopy/bezierPatchCopy specific message
if clear:
    clearWholeLayer()
sourceRect = [left, top, wrap32(left+width), wrap32(top+height)]
render*(source, sourceRect, points, divX, divY,
        bitmapMethod=0, holdAlpha, opacity=255, stretchType)
```

operate 两条：

```text
if mode == 128: mode = signed auto mode
if !resolveBitmapMethod(mode): throw operateMesh message
ownerAccessor = retain(owner)
holdAlpha = owner.holdAlpha as bool       # 所有成功 mode 都读取
sourceRect = wrap32 edge construction
render*(..., bitmapMethod, holdAlpha, opacity, stretchType)
```

copy 只有 face 1 读取 holdAlpha；face 0/4 使用 false。clear 在 render 的任何 point/source/
clip conversion 前执行，脚本副作用不会在后续异常时回滚。`clearWholeLayer` 的读取顺序是
`neutralColor, height, width`，随后以五个 Integer Variant 调
`fillRect(0,0,width,height,neutralColor)`；receiver 是 owner，status 忽略。

宽消息原始字节四端唯一命中：

| 文本 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| meshCopy error | `0x14D54C2` | `0xD8509A` | `0x10195B8A0` | `0x174DC04` |
| operateMesh error | `0x14D5506` | `0xD850DE` | `0x10195B8E4` | `0x174DC48` |
| bezierPatchCopy error | `0x14D5550` | `0xD85128` | `0x10195B92E` | `0x174DC92` |

## 6. plain/Bezier render core 数据流

plain core 先读取 flat points，再读取 clip；Bezier core 顺序相反：先 clip、再
parse/tessellate。这个异常/owner 顺序在四端一致。

plain point parser：

- GetCount 后以 signed truncating `count/2` 计算 PointD 数；
- count 0/1 得到空 vector；奇数 count 忽略最后一个坐标；
- -1 得到空；≤-2 会把负 point count 扩成巨大 reserve；
- 每个纳入的 x/y 都 probe-then-Real，缺失贡献 0。

clip 四字段均 flags=0 严格 Integer 读取并使用四个独立 hint：
`clipLeft, clipTop, clipWidth, clipHeight`；right/bottom 用低 32 位 wrap-add。随后：

```text
targetLayer = strict Layer.FromObject(owner)
sourceLayer = strict Layer.FromVariant(source)
sourceTexture = sourceLayer.mainImage.texture
method = TVPGetRenderManager().GetRenderMethod(
             opacity, holdAlpha, bitmapMethod)
if method == null:
    return                         # 不 submit/update/debug

submitted = submitLayerMesh(
    target main image, clip, source texture/source rect,
    plain: points/points,
    Bezier: controlPoints/tessellatedPoints,
    divisions, method, stretchType)
if submitted:
    owner.update(clipLeft, clipTop, clipWidth, clipHeight)

drawGridDebug(debugMeshApp)
if Bezier:
    drawBezierControlFrame(debugBezierApp)
```

submit false 仍执行 debug；submit true 的 update 在 debug 前。update 用四个 Integer
`left,top,width,height`，status 忽略。public mesh submit 的 stretch parameter static、
source texture manual AddRef、cell admission/winding、target/reference texture和异常泄漏边已
由相邻 mesh slice 逐项闭合，本轮 callback 参数和调用顺序完全吻合。

## 7. debug helper 的精确边界

`debugMeshApp` 为 Void 时，debug-grid 在创建 Layer accessor/Array 前返回。非 Void 时：

- 解析全局 `Layer` class dispatch；
- 创建一个可复用 line Array；每一行/列前 clear native Items；
- 先按 y=0..divY 画横线，再按 x=0..divX 画竖线；
- 每个 point 以 fresh nested `[x,y]` Array append；
- 调 `Layer.drawLines(debugAppearance, lineArray)`，owner 作为 objthis，status 忽略；
- 任一 division 为负时对应外循环完全跳过；另一维非负时仍会 dispatch 空 line Array。

`debugBezierApp` 为 Void 时同样立即返回。非 Void 时先手工 AddRef owner，再取得 Layer class
和 basis(3)，依次提交四条 row Bezier、四条 column Bezier；每条含四个 nested point，
调用 `drawBeziers`。它只在 render method 非 null 的 Bezier core 中执行。

`drawLines` 和 `drawBeziers` 使用两个独立 process-static hint，不与 Player 单数
`drawLine` 或 update/clip hint alias。

## 8. 三个 public frame callback

### 8.1 `drawMeshFrame`

先按 plain parser 构造 points，再取得 Layer class；随后：

1. y=0..divY 横线，边界 y=0/divY 用 outline，其余 meshline；
2. x=0..divX 竖线，边界 x=0/divX 用 outline，其余 meshline；
3. appearance 只有 Variant type 精确为 Void 才跳过；typed-null Object 仍 dispatch；
4. 每次线条创建 fresh outer Array 和 fresh nested point Arrays；
5. division 负值的空/dispatch规则与 debug grid 相同。

### 8.2 `drawBezierPatchMeshFrame`

顺序是先取得全局 Layer class owner，再 parse/tessellate control points，最后复用上述
grid-frame 算法。Layer lookup 成功后 tessellation 抛出时，class accessor 仍按 unwind
Release。

### 8.3 `drawBezierPatchFrame`

读取恰好 16 个 control point（32 个 probe-then-Real 坐标），取得 basis(3)，然后两组各
sample=0..3：

- 第一组按 row 求值，每条只 append row 0..2 的三个 nested point；sample 0/3 用 outline，
  1/2 用 meshline；sample 0 对整个 Items 序列 reverse；
- 第二组按 column 求值，同样只 append column 0..2；sample 3 reverse；
- 非 Void appearance 调 `Layer.drawBeziers(appearance, curve)`。

“只取 3 点”和两个不对称 reverse 是四端原行为，不可按常见 cubic control frame
直觉改成 4 点或统一方向。

## 9. 字符串、owner 与平台差异

UTF-16LE raw-byte 搜索已对
`face/type/holdAlpha/neutralColor/clipLeft/clipTop/clipWidth/clipHeight/fillRect/update/
drawLines/drawBeziers/Layer` 在四端读至 `cursor.done=true`。例如 core 专用 clip 四元组
位于 Android arm64 `0x14C202C..0x14C2062`、Android armv7
`0xD78792/0xD85036/0xD85046/0xD787A4`、iOS arm64
`0x10195B806..0x10195B83C`、iOS armv7 `0x174DB6A..0x174DBA0`。
反编译器的 `"c"/"h"/"f"/"u"/"d"/"L"` 均只是被截断的完整宽成员名。

平台差异限于：

- Android arm64 inline mode resolver，其余三端为独立 helper；
- AArch64/AArch32 的 Variant temporary 展开、vector/deque reverse 和 EH landing；
- Android libstdc++ 与 iOS libc++ 容器 grow/delete 形态；
- 64/32 位 payload 偏移。源级字段、分支、脚本调用、容器和 owner 无差异。

所有 fresh Array/Variant/vector/accessor 按 C++ 逆序 unwind；已经发生的 clear、update、
drawLines/drawBeziers 和目标纹理写入不回滚。debug property 外部 alias 可活过下一次 setter，
因为 getter CopyRef；payload teardown 后 alias 仍持有自己的引用。

## 10. 与本地源码/测试逐行对照

本地实现对应 `cpp/plugins/motionplayer/MotionLayerExtensions.h:52` 与
`cpp/plugins/motionplayer/MotionLayerExtensions.cpp:946` 起的 payload/property/state/render/
frame 方法，以及同文件 `361` 起的 dispatch/debug/submit helpers。

现有测试覆盖：

- `tests/unit-tests/plugins/motionplayer-dll.cpp:6929`：clear 的 face probe、属性/hint顺序与
  fillRect Integer argv；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:7030`：drawLines/drawBeziers 独立 hint 和
  三个 frame callback；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:7138`：plain/Bezier 共用 clip/holdAlpha
  hint 与异常 cutoff；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:7280`：face/type probe、auto mode、负 type和
  invalid face边界；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:7440`：公共 mesh submit cell winding、
  clip reject 和异常 source-texture owner。

四端逐行对照未发现新的运行语义偏差；本 slice 不修改 C++，只增加报告、台账状态与 IDB
元数据。之前 BezierPatch slice 的 U/V weight 修正不影响本组 frame helper使用的共享
basis-table路径。

## 11. 状态结论与验证边界

`MotionLayerExtensions_guess` 九行从 `BODY_PENDING_SEPARATE_SLICE` 提升为
`IMPLEMENTED`。NCB pending 从 79 降为 70，`IMPLEMENTED` 从 51 增为 60；剩余 70 行恰好
都是 `EmotePlayer` callback，316 条注册面仍为 316/316、`UNMAPPED=0`。

四份 IDB 已统一 helper 命名、添加 callback/helper 注释与九个 member 书签并原位保存。
生成器确定性、strict TSV 与 `git diff --check` 在台账回填后复核。当前环境缺少 CMake、
Ninja、Emscripten，独立 syntax check 受缺失第三方头文件阻塞，因此不宣称正式 build/unit
runtime。完整 root-reachable helper/object/container 分母及 70 个 EmotePlayer body 仍待闭合。
