# MotionPlayer CameraNode 四端对照（2026-08-12）

## 结论

本专题以 `reference/binaries/` 中 Android arm64、Android armv7、iOS
arm64、iOS armv7 四个当前参考二进制为共同真值，重新核验 type-5
CameraNode 的节点选择、raw-label target 查找、二维 camera offset、camera-query
状态和跨帧保留边界。旧 `libkrkr2.so` 地址注释不再作为证据。

四端共同证明：CameraNode 使用前一阶段生成的 `vertexPosX/Y/Z`，而不是节点的
accumulated position。active slot 的 `camera.target` 命中时，二维焦点和 query
target 都改用命中节点；空 target 或 miss 时二维焦点退回 camera 节点，但 query
target 坐标保留上一帧值。X/Y 投影差值还会先窄化为 float，之后才进入根 Player
的 affine 和整数化路径。

旧本地实现只保留了第一 active camera、affine、FOV 与角度公式的大致骨架，遗漏
了 target 查找和 float 窄化，读错了位置数据源，并在每帧把 target 坐标错误覆盖
为 camera 坐标。本轮已按四端共同控制流修复。

## 旧函数边界注释的纠正

本地源码曾把 Android arm64 `0x6BDA28` 标成独立 CameraNode helper，并把相邻
visibility/shape 段也标成 `0x6BDxxx`。fresh lookup 证明 `0x6BDA28` 落在
`Player_updateAnchorFeedback_guess` 的真实函数范围 `0x6BD908..0x6BE0E8`
内；该地址只是 type-10 feedback-anchor 循环内部，不是 CameraNode 函数入口。

真实 CameraNode helper 位于 vertex/visibility 后处理链的更早位置：

| 目标 | 真实函数 | 大小 | 旧误注 |
|---|---:|---:|---:|
| Android arm64 | `0x6BAE08` | `0x298` | `0x6BDA28` |
| Android armv7 | `0x587748` | `0x20A` | 由旧 A64 地址类推 |
| iOS arm64 | `0x1001108C4` | `0x25C` | 由旧 A64 地址类推 |
| iOS armv7 | `0x10E048` | `0x20C` | 由旧 A64 地址类推 |

四个入口现统一命名为 `Player_updateCameraNode_guess`。修复本专题时没有再把旧
单端地址搬入 compiled source comment。

## `updateLayers` 调用位置

四端主函数中的共同调用顺序为：

```text
Player_applyCameraConstraints_guess
Player_updateLayersVertexComputation_guess
visibility helper
Player_updateCameraNode_guess
shape-AABB helper
shape-geometry helper
motion-child helper
Player_updateParticleEmitters_guess
Player_updateParticleSystems_guess
Player_updateAnchorFeedback_guess
```

CameraNode 因而消费 vertex-computation 的本帧输出。它位于 visibility 后，但不
读取 draw flag，也没有 preview gate、slot-done gate 或 source-valid gate。

四端主函数与 camera helper 映射：

| 目标 | `Player_updateLayers_guess` | `Player_updateCameraNode_guess` |
|---|---:|---:|
| Android arm64 | `0x6B871C` (`0xAE4`) | `0x6BAE08` (`0x298`) |
| Android armv7 | `0x5856E0` (`0x9DC`) | `0x587748` (`0x20A`) |
| iOS arm64 | `0x10010E544` (`0xB50`) | `0x1001108C4` (`0x25C`) |
| iOS armv7 | `0x10BE5C` (`0xA76`) | `0x10E048` (`0x20C`) |

## 关键 ABI 对照

这些偏移只用于反编译复核，不应进入 portable C++ 源码注释。

### Player 字段

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| root/draw-affine owner 指针 | `+0` | `+0` | `+0` | `+0` |
| camera XYZ | `+72/+80/+88` | `+40/+48/+56` | `+48/+56/+64` | `+24/+32/+40` |
| target XYZ | `+96/+104/+112` | `+64/+72/+80` | `+72/+80/+88` | `+48/+56/+64` |
| camera offset X/Y float | `+144/+148` | `+112/+116` | `+120/+124` | `+96/+100` |
| normalized camera angle | `+472` | `+304` | `+360` | `+244` |
| camera active | `+1094` | `+746` | `+982` | `+682` |
| stereovision active | `+1095` | `+747` | `+983` | `+683` |
| has camera | `+1100` | `+752` | `+988` | `+688` |
| camera FOV | `+1104` | `+760` | `+992` | `+692` |
| Z factor | `+1112` | `+768` | `+1000` | `+700` |

根/draw-affine owner 的 2x2 矩阵偏移为 Android arm64 `+808..+832`、Android
armv7 `+536..+560`、iOS arm64 `+696..+720`、iOS armv7 `+472..+496`。
CameraNode 无条件解引用 Player 的 owner 指针；没有空指针 guard。

### MotionNode 与 active slot

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| 节点步长 | `2632` | `2272` | `2648` | `2228` |
| `nodeType` | `+28` | `+20` | `+28` | `+20` |
| vertex X/Y/Z | `+152/+160/+168` | `+136/+144/+152` | `+152/+160/+168` | `+136/+144/+152` |
| active slot index | `+1392` | `+1160` | `+1392` | `+1128` |
| accumulated active | `+1505` | `+1265` | `+1521` | `+1233` |
| CameraNode FOV eval output | `+2368` | `+2032` | `+2384` | `+1996` |
| slot 步长 | `536` | `432` | `536` | `420` |
| active `camera.target` | `node+824+536*i` | `node+704+432*i` | `node+824+536*i` | `node+684+420*i` |

`camera.target` 与前一专题的 `anchor.target` 是同一 slot 中的不同 ttstr 字段，
不能互换。CameraNode FOV 是 timeline evaluator 写入节点的 type-5 输出；普通
构造有意不初始化它，不能为“安全”添加构造默认值。

## 共同伪代码

以下伪代码合并 deque ABI 展开差异，同时保留状态写入顺序：

```text
player.hasCamera = false

for cameraIndex in [1, nodes.size):
    camera = nodes[cameraIndex]
    if camera.type != 5 or !camera.accumulated.active:
        continue

    player.hasCamera = true

    target = null
    focus = camera
    rawTarget = camera.activeSlot.cameraTarget
    if rawTarget.backingPointer != null:
        target = player.findNodeByRawLabel(rawTarget, recursive=false)
        if target != null:
            focus = target

    root = nodes[0]
    deltaX = -float(focus.vertexX - root.vertexX)
    deltaY = -float(
        focus.vertexZ * player.zFactor + focus.vertexY
        - (player.zFactor * root.vertexZ + root.vertexY))

    owner = *player.rootPlayer
    player.cameraOffsetX = float(int(
        owner.m11 * double(deltaX) + owner.m12 * double(deltaY) + 0.5))
    player.cameraOffsetY = float(int(
        owner.m21 * double(deltaX) + owner.m22 * double(deltaY) + 0.5))

    if player.cameraActive:
        player.cameraFov = camera.cameraFov
        player.cameraXYZ = camera.vertexXYZ
        if target != null:
            player.targetXYZ = target.vertexXYZ

        radians = atan2(
            player.cameraZ - player.targetZ,
            player.cameraX - player.targetX)
        degrees = radians * -57.2957795 + 90.0
        while degrees < 0:   degrees += 360
        while degrees >= 360: degrees -= 360
        player.cameraAngle = degrees

    break
```

## 节点选择与 target 边界

- 函数开头总会清 `hasCamera`；没有非根节点时直接结束。
- 按 deque 顺序扫描，第一条 `type == 5 && accumulated.active` 的节点胜出；
  后续 active camera 不参与。没有检查当前 slot 的 `done`。
- 进入命中 body 后根节点 0 直接存在并被读取，没有 empty guard。
- 二进制检查 `camera.target` 的 ttstr backing pointer 是否为 null。本地
  `ttstr::IsEmpty()` 同样精确检查 `Ptr == nullptr`。
- null-backed empty target 不执行 map lookup。即使 raw-label map 中存在空 key，
  CameraNode 也不会命中它。这与 type-9 camera constraint 不同：constraint 会
  直接查空 key。
- 非空 target 使用 Player raw-label ordered map，`recursive=false`；比较保留
  大小写和斜杠，不做路径正规化。
- miss 将 `target` 保持为 null，并让二维 `focus` 回退到 camera 自身。
- hit 后直接消费 map 中的节点索引，没有 bounds guard。损坏/越界 map 的原生
  行为不能用 clamp 或根节点 fallback 改写。

## vertex 数据流与二维 offset

CameraNode 读取 node 的早期 vertex-output 块。该块由紧邻的
`Player_updateLayersVertexComputation_guess` 产生，和 accumulated transform
块是两个不同物理位置：Android arm64 分别在 `+152` 与 `+1512`，其他目标也
有相同的结构性分离。

target hit 后，focus 是 target 节点；否则 focus 是 camera 节点。X 使用
`focus.vertexX-root.vertexX`，Y 使用带 Z factor 的投影：

```text
focus.vertexZ*zFactor + focus.vertexY
  - (zFactor*root.vertexZ + root.vertexY)
```

两个差值各自先转为 float 再取负，之后又提升到 double 参与 affine。这个窄化
会丢失大坐标低位，不能把整个表达式一直保留为 double。

affine 结果先加 `0.5`，再按 C++/ARM conversion 的向零整数化形态转 int，最后
转成 float 存储。它不是对称的 round-to-nearest：例如 `+1.6` 得 2，`-1.6`
经 `+0.5` 后得到 -1。四端的整数化具体为 signed-int32 saturation：NaN 得零，
正负溢出分别得到 `INT32_MAX/INT32_MIN`，随后才从 int32 转 float。不得替换为
`std::round`、`floor(x+0.5)` 或保留 double；完整指令与 invalid/overflow 边界见
`motionplayer_camera_offset_conversion_four_binary_2026-08-16.md`。

## cameraActive 与跨帧查询状态

二维 camera offset 与 cameraActive gate 无关，只要找到 active CameraNode 就会
更新。cameraActive 关闭时，FOV、camera XYZ、target XYZ 和 cameraAngle 全部
保留旧值。

cameraActive 开启时：

1. 总是从实际 camera 节点（不是 focus 节点）复制 FOV 和 vertex XYZ。
2. 只有 non-null target hit 才复制 target vertex XYZ。
3. empty/miss 时 target XYZ 不清零、不退回根、不改成 camera，而是保留上次
   成功命中或构造初值。
4. angle 随即使用本帧 camera 与“本帧命中或历史保留”的 target 计算。
5. Y 坐标虽被保存，但 angle 只使用 X/Z。

因此 target 消失的一帧仍可以围绕旧 target 产生新角度。这是明确的跨帧状态机，
不是未完成占位。

若整帧没有 active CameraNode，函数只留下 `hasCamera=false`；camera offset 与
所有 camera-query 字段均保持上次值。若有 active camera 但 cameraActive 关闭，
`hasCamera` 和二维 offset 更新，query 字段仍保持旧值。

`cameraActive` 与相邻的 `stereovisionActive` 是两个独立的公开 read/write byte。
前者是本函数中 FOV、camera/target XYZ 和 cameraAngle 的唯一 gate；后者在本函数中
完全不读，只控制 prepare/sort 之后的 PreparedRenderItem perspective pass。旧分析曾把
Player `+1094/+746/+982/+682` 错称 stereovisionActive，本轮按四端 accessor、注册名和
唯一内部读者共同纠正。

## 角度与浮点边界

四端均调用 `atan2(cameraZ-targetZ, cameraX-targetX)`，使用十进制常量
`-57.2957795` 和 `90.0`，再以两个 while 循环归一化到通常的 `[0,360)`。
不要替换为更高精度 `180/pi`，也不要改成 `fmod`，因为 NaN、符号零和舍入边界
会改变。

常见方向：

```text
camera-target = ( X= 0, Z= 1 ) ->   0 degrees
camera-target = ( X= 0, Z=-1 ) -> 180 degrees
camera-target = ( X=-1, Z= 0 ) -> 270 degrees
```

输入含 NaN 时 `atan2`/算术得到 NaN，两个有序 while 条件都为 false，最终保存
NaN。原生在计算角度弧度后曾先写一次 cameraAngle，再被归一化 degree 覆盖；
函数无中途回调，所以 portable 实现只提交最终 degree，不添加额外可观察事件。

## 本地差异与修复

| 旧本地行为/注释 | 四端证据 | 修复 |
|---|---|---|
| CameraNode 标作 A64 `0x6BDA28` | 真入口为 `0x6BAE08`；旧地址位于 type-10 helper 内 | 删除 compiled source 中该旧地址，本文记录纠错 |
| target lookup 留作占位，二维 focus 恒为 camera | non-empty `camera.target` raw lookup；hit 后 focus=target | 接入 ordered raw-label map，empty/miss 才回 camera |
| 使用 `accumulated.pos*` | helper 读取 vertex-output 物理块 | 改用 `vertexPosX/Y/Z` |
| 差值保持 double | 四端在 affine 前均窄化为 float | 添加显式 float 窄化与提升 |
| cameraActive 更新的 camera XYZ 来自 accumulated | 四端来自 camera vertex-output | 改用 camera vertex XYZ |
| CameraNode gate 误接 `_stereovisionActive` | 四端读取 `cameraActive` 前一 byte；后一个 byte 只供 post-prepare projection | 改接 `_cameraActive`，保留两属性独立 |
| target 始终保留旧值参与角度，随后被 camera XYZ 覆盖 | hit 时先写真实 target；empty/miss 保留旧 target；从不写 camera 到 target | 恢复条件写入和跨帧保留 |
| 相关字段注释继续引用旧单端地址 | 四个当前产物布局不同 | 改成 portable 生命周期/数据流注释 |

没有增加 slot-done、preview、source-valid、索引范围或 root-pointer guard；这些
“安全化”都会偏离四端共同边界。

## 测试与构建验证

确定性测试新增覆盖：

- CameraNode 对 null-backed empty target 跳过查找，而 non-empty raw label 正常
  命中、miss 返回无 target；
- `16777217.0` 的 float 窄化得到 `16777216.0f`；
- `+0.5` 后向零截断的正负不对称；
- 三个典型 X/Z 方向的角度结果；
- NaN 角度穿过归一化循环后仍为 NaN。
- `cameraActive` 与 `stereovisionActive` 的初值和写入彼此独立；
- 直接运行 CameraNode phase：`cameraActive=false, stereovisionActive=true` 时
  alive 更新而 query state 保留；反向设置时 FOV/position 正常发布。

验证结果：

- 最初 CameraNode 修复与本轮 gate 纠正后，Web Debug 和 Wasmtime Headless 均完整
  编译并完成最终链接。
- 使用当前 Web Emscripten 编译参数对
  `tests/unit-tests/plugins/motionplayer-dll.cpp` 的 syntax-only 检查成功，仅有
  项目既有 `_tss` literal-operator deprecation warning。
- Windows 原生 Catch 可执行文件仍因既有 vcpkg/cocos2dx 配置问题不可用；没有
  改造 fixture 来伪造可运行环境。

## IDB 改进

四个 IDB 已统一命名并保存：

- `Player_updateCameraNode_guess`；
- camera/focus/target 节点局部变量按四端可靠对应关系命名；
- Android armv7、两个 iOS 目标中独立的 root 节点局部变量也已命名；Android
  arm64 的 root 指针与 deque-start 临时值复用，未为表面统一而错误拆名；
- function start、target fallback、float 窄化/量化、target 跨帧保留和 angle
  归一化均添加了行级注释；
- helper 与四个 `Player_updateLayers_guess` 都经过 force recompile/fresh
  decompile，主函数引用已显示新名称。

四份数据库均通过 IDA 原生保存成功。后续审计相邻 visibility/shape helper 时，
应继续以 fresh lookup 确定真实边界，不复用本轮已证伪的 `0x6BDxxx` 注释链。
