# MotionSubNode / child-player 四参考二进制复原记录（2026-08-12）

## 范围与结论

本轮只把 `Player::updateLayers` 后置阶段中的 type-3 MotionSubNode、其持有的
child `Player` 生命周期，以及 child pending-event 聚合链作为一个纵向审计。
Android arm64、Android armv7、iOS arm64、iOS armv7 四份当前参考二进制共同作为
语义来源；旧 `libkrkr2.so` 地址注释不作为证据。

四端共同实现可以概括为：每帧遍历非 root 的 type-3 节点，恰好解析一次 retained
child，对 dirty/mode、slot/source 和 replay flag 分层分流；活动路径同步 motion、时间、
角度和 child root 状态，最后进入共享 child step；teardown 路径则停止 child、销毁其
节点树并清空两项 motion 名称，而且故意不执行共享 child step。

本轮纠正的核心边界是：这里不是一个带有“安全 child/null/root fallback”的高层接口。
非对象 Variant 在对象转换阶段抛异常；NCB 查询失败可以得到 null，但主循环随后直接
解引用；空 root 同样直接下标。共享源实现把这些 malformed-state 边界留给调用者/进程，
没有在本函数中吞掉。

## 四端函数映射

### MotionSubNode 主体

| 目标 | 函数地址 | 大小 | IDB 名称 |
|---|---:|---:|---|
| Android arm64 | `0x6BB4A0` | `0xD10` | `Player_updateMotionSubNodes_guess` |
| Android armv7 | `0x587E00` | `0x9D8` | `Player_updateMotionSubNodes_guess` |
| iOS arm64 | `0x100110EEC` | `0xB1C` | `Player_updateMotionSubNodes_guess` |
| iOS armv7 | `0x10E68C` | `0xBE2` | `Player_updateMotionSubNodes_guess` |

四个函数都只有 `self`，没有源代码层的 `currentTime` 参数。Web 诊断仍可在 portable
实现中读取 `_clampedEvalTime`，但这不是 native 调用 ABI 的一部分。

2026-08-14 后续 fresh audit 进一步确认：诊断不仅不能进入 ABI，也不能在 snapshot
关闭或 preview return 前物化 motion path。本地 parent/child path conversion 现已移入
opt-in snapshot gate；完整证据见
`analysis/motionplayer_motion_sub_snapshot_isolation_four_binary_2026-08-14.md`。

### `Player_updateLayers` 调用点与最终 flags 清理

| 目标 | caller | MotionSub 调用点 | 非 root flags 清理 |
|---|---:|---:|---|
| Android arm64 | `0x6B871C` | `0x6B9070` | 完整 byte 写零，逻辑字段偏移 `+44` |
| Android armv7 | `0x5856E0` | `0x586014` | 完整 byte 写零，逻辑字段偏移 `+36` |
| iOS arm64 | `0x10010E544` | `0x10010EFB4` | 完整 byte 写零，逻辑字段偏移 `+44` |
| iOS armv7 | `0x10BE5C` | `0x10C83A` | 完整 byte 写零，逻辑字段偏移 `+36` |

因此 portable 调度签名改成无参数，帧末清理使用 `node.flags = 0`，而不是只清 bit 0。

### pending-event 聚合（2026-08-13 纠正）

| 目标 | parent/child wrapper | range-insert specialization | child clear/erase-at-end |
|---|---:|---:|---:|
| Android arm64 | 主函数内联 | `0x6F0A1C` | 主函数内联 |
| Android armv7 | `0x58A952` | `0x5AE268` | `0x5AE568` |
| iOS arm64 | `0x100113E64` | `0x100115A70` | wrapper 内联析构循环 |
| iOS armv7 | `0x11186C` | `0x11337C` | wrapper 内联析构循环 |

后续把该字段与 enqueue/dispatch 的同一 Player 偏移交叉核对后，旧
`ChildMotionRenderItem` 解释被证伪。IDB 已改用以下未知精确源名的保守名称：

- `Player_prependAndClearChildPendingEvents_guess`；
- `MotionEventVector_prependRange_guess`；
- Android armv7 的额外 outlined helper 为
  `MotionEventVector_eraseAtEnd_guess`。

这里的插入位置明确是 `parent.begin()`，不是 `parent.end()`。插入完成后 child
元素按逆字段顺序析构，最后把 child vector 的 end 重置到 begin；capacity/end-cap
不变。因此 `clear()` 是正确的源级表达，交换空 vector 或 shrink 都不等价。

## MotionEvent 元素与 vector ABI（2026-08-13 纠正）

range-insert specialization 的复制/析构步幅证明元素源形状是：

```cpp
struct MotionEvent {
    int32_t type;
    tTJSVariant param1;
    tTJSVariant param2;
};
```

| ABI | 元素步幅 | `type` | Variant `param1` | Variant `param2` |
|---|---:|---:|---:|---:|
| 两个 64 位目标 | 44 | `+0` | `+4`，20 bytes | `+24`，20 bytes |
| 两个 32 位目标 | 28 | `+0` | `+4`，12 bytes | `+16`，12 bytes |

对应 Player 内 vector 对象的目标偏移分别为 Android arm64 `+936`、Android
armv7 `+656`、iOS arm64 `+824`、iOS armv7 `+592`。四端 enqueue 与 dispatcher
也逐一读取这些完全相同的偏移；构造函数各自只在该处初始化一个三指针 vector，
Player 析构也只销毁这一处。因此它不是独立 residual/render 容器，而是 Player 唯一
持久 `MotionEvent` vector。portable 源码现在只保留 `_pendingEvents`，不再保留被
证伪的 `_childMotionRenderItems_guess` 第二字段。

## 共同控制流伪代码

下列伪代码省略 ABI 偏移和诊断分支，但保留分支顺序、跳转边界与对象生命周期：

```text
if player.preview:
    return

for node in nonRootNodes:
    if node.type != MotionSub(3):
        continue

    parameter = resolveNodeParameterEntry(player, node)
    mode = parameter ? parameter.mode : 0

    child = *node.childPlayerVar.AsObjectNoAddRef().native<Player>()
    childRoot = child.nodes[0]

    if mode == 0 && !node.accumulated.dirty:
        goto shared_child_step

    src = node.activeSlot.src
    if node.activeSlot.done || src.owner == null:
        child.allplaying = false
        child.variableLabelScopes.clear()
        child.resetAndReleaseOldNodeTree()
        child.stealthMotion.clear()
        child.motionKey.clear()
        continue

    if (mode & 5) != 0 || node.flagsByte != 0:
        node.flagsByte = 1
        segments = split(src, '/')
        if segments.size == 1:
            child.setChara(src)
            child.play(activeSlot.icon, activeSlot.motionFlags | mode)
        else:
            child.setChara(segments[1])
            child.play(segments[2], activeSlot.motionFlags | mode)

        if child.allplaying && child.queuing:
            childTime = parent.frameTick - slot.clipStart + slot.motionTimeOffset
            if parent.deltaTime < 0 and child.loopTime >= 0:
                repeatedly wrap childTime against child.totalFrames/loopTime
            childTime = fmax(childTime, 0.0)
            child.frameTick = childTime
            child.clampedEvalTime = min-by-branch(childTime, child.totalFrames)
            child.queuing = true
            child.firstFrame = true
            if !parent.queuing:
                child.reverseSeek = true
        destroy segments

    preserve decompiler-visible if (!true) jump token

    compute dofst / optional dual-slot crossfade offset
    compute optional angle from direct/delta/interpolated/target mode
    apply origin offset only for coordinate mode 0 or 1

    write child root position
    child.setFlip(...)
    child.setZoom(...)
    copy camera angle; direct-edit may reinitialize motion
    if angle is present: child.setAngleDeg(...)
    child.setSlant(...)
    child.setOpacity(...)
    child.setVisible(parentNode.accumulated.active)
    copy player packed color
    copy or rotate child root matrix
    childRoot.delta.dirty = true

shared_child_step:
    childRoot.clipAABB = node.clipAABB
    childRoot.meshAncestor = node.meshCombine ? &node : node.meshAncestor
    childRoot.visibleAncestor = node.visibleAncestor
    child.frameProgress(parent.deltaTime)
    child.updateLayers()
    parent.pendingEvents.insert(parent.pendingEvents.begin,
                                child.pendingEvents.begin,
                                child.pendingEvents.end)
    child.pendingEvents.clear()  // destroy Variants; retain capacity
```

关键点是最前面的 dirty/mode 快路仍然进入 shared child step，却不会读取 slot done
或 src。teardown 则使用 `continue`，明确跳过 shared child step。两个路径不能合并成一个
“如果 child 有效就更新”的宽松分支。

## child Variant、指针与生命周期

四端都先执行普通 Variant object conversion，再做 NCB native-instance 查询。

- non-object（包括 Void、Integer）在 `AsObjectNoAddRef()` 阶段抛异常；
- object dispatch 为 null 或 native-instance 查询失败可返回 null；
- MotionSub 主循环没有 null child guard，立即建立 child 引用；
- 主循环也没有 `child.nodes.empty()` guard，立即取得 root zero；
- child/root 指针只解析一次，并贯穿本节点的活动、shared 和 teardown 路径；
- 所以不能通过重复 `getChildPlayer()`、静默跳过 malformed child、或临时构建 root
  来“增强健壮性”，这些都会改写四端边界行为。

本轮相应删除 `MotionNode::getChildPlayer()` 的预先 `Variant::Type()` 检查。该 helper
现在让非对象转换异常自然传播，只对 null dispatch/NCB failure 返回 null。

teardown 的内部顺序也保持：先 `_allplaying=false`，再清 variable-label scope，随后
reset/release 旧 node tree，最后先清 stealth motion 名称、再清 primary motion 名称。
清树逻辑沿用此前已完成的四端 NodeTree 生命周期纵向，本轮没有新造替代释放器。

## replay、路径拆分与时间同步

### replay gate

活动 slot/source 并不意味着每帧重播。共同 gate 是：

```text
(parameterMode & 5) != 0 || completeFlagsByte != 0
```

进入后把完整 byte 写成 `1`。这既不是 `flags |= 1`，也不是测试 bit 0；例如输入
`0x80` 会触发 replay，输出必须恰好为 `1`。帧末 caller 再把完整 byte 写零。

### `src` 拆分

- 恰好一个 segment：`setChara(src)`，播放 active slot 中的 icon；
- 不是一个 segment：直接读取 `[1]` 和 `[2]`，分别 setChara/play；
- 没有 `size == 2` fallback，也没有对 `[2]` 的保护；
- active-slot index 在整条路径中保持不变，slot flip 发生在更早的 clip evaluation；
- split vector 活到 time-sync 完成后才析构，不能缩到 setChara/play 两个调用周围。

`setChara()` 同时提交 pending stealth character；该字段不是第二个 motion 请求。

### child 时间

同步只在 `child._allplaying && child._queuing` 内发生。方向取 parent `_deltaTime`
（已乘 speed），不是 raw frame delta。反向且 child loopTime 非负时，使用 while 对
`cachedTotalFrames`/`loopTime` 做原样回绕。

下界必须使用 C `fmax(childTime, 0.0)` 语义：NaN 得 `+0.0`，`-0.0` 也得
`+0.0`。然后先发布 child frame tick，再对 totalFrames 做显式 `>` 上界分支并写
clamped evaluation time。最后同时置 queuing/firstFrame；parent 不 queued 时另置 child
reverse-seek。这里没有 timeline 遍历。

## angle、origin 与 IEEE 边界

### dual-slot dofst blend

进入条件是 `motionDocmpl && crossfading && !other.done && other.motionDt != 0`。随后
先比较两个 dofst：若精确相等，必须直接返回当前 dofst，不读取 parameter value，不做
除法，不解释 easing Variant，也不做角度归一化。该短路也保留第一个操作数的 signed
zero。因此测试使用相等的 `-0.0/+0.0`、零分母和非法 easing Variant 锁定旁路。

不相等时才：

1. 按超过 180 度的差值把另一端加/减 360，选择短路径；
2. 直接计算 `(parentTime-currentStart)/(otherStart-currentStart)`，无零分母 guard；
3. easing 非 Void 时调用尚待独立纵向复核的 Bezier Variant helper；
4. 线性混合；
5. 只做一次 `<0 +360` 或 `>=360 -360` 调整，不使用循环归一化。

### 四种 angle mode

- mode 1：`dofst + accumulated.angle`，不归一化；
- mode 2：通常从上一帧 delta position 做 atan2；child `_noUpdateYet` 时落到 mode 3；
- mode 3：只在 crossfading 且 opposite slot 未 done 时工作，对 slot position 在 `t`
  和 `t+0.0001` 采样，用差分方向做 atan2；
- mode 4：无条件用原始 `dtgt` 做 label-map lookup，空 label 不特殊处理；NodeTree
  本来就允许把原始空 label 插进 map；
- 未知 coordinate mode 在对应 case 中保留默认 angle，但走 native 的“结果存在”
  边界；没有把未知模式当成 mode 0。

mode 3 的双采样选择在 2026-08-16 重新读取原始指令后更正为：
`first = next < 1 ? ratio : 0.9999`，`second = min(next,1)`。因此 next 为 NaN 时四端
first 都是 `0.9999`，不是旧文所称的 NaN ratio。second 存在目标分化：ARM64 `FMIN`
传播 NaN，ARMv7 ordered-select 得 `1.0`。接近 1 时 first 为 `0.9999`、second 为
`1.0`。完整更正与 type-6 emitter 的同型证据见
`motionplayer_shared_position_derivative_sample_select_four_binary_2026-08-16.md`。

origin offset 先按矩阵计算平移量，但只对 coordinate mode 0 的 X/Y 或 mode 1 的
X/Z 发布；其他 mode 三个坐标全不变。`originX == 0 && originY == 0` 是精确早退。

## child 状态传播与 visibility 证明

四端调用/写入顺序共同为：root position 直接写入、`setFlip`、`setZoom`、camera angle
copy、direct-edit reinit、可选 `setAngleDeg`、`setSlant`、`setOpacity`、`setVisible`、
Player packed-color copy、root matrix copy/rotation、root dirty。

最容易被旧实现混淆的是 visibility。四端的数据流均为 parent node 的 accumulated
**active** byte 写入 child root 的 **visible override**：

| 目标 | parent accumulated active | child root visible override |
|---|---:|---:|
| Android arm64 | `+1505` | `+1586` |
| Android armv7 | `+1265` | `+1346` |
| iOS arm64 | `+1521` | `+1602` |
| iOS armv7 | `+1233` | `+1314` |

这里不复制 parent accumulated-visible，不写 child active-override，也不传播
`forceVisible`。shared exit 只传播 clipAABB、条件 mesh ancestor、visible ancestor 三项。

矩阵增量角使用原始分组：

```cpp
(angleDifference * 3.14159265 + angleDifference * 3.14159265) / 360.0
```

不能重写成 `angleDifference * PI * 2 / 360`。共同源形状还更接近直接重复写四个
`cos`/`sin` 表达式：Android arm64 向量化后出现两次 cos 与两次 sin，iOS arm64
融合为 sincos，32 位 iOS 只调用一次各自函数。这是优化器差异，不应从任一端反推
portable 源必须缓存 trig 结果。

## 本地实现进入本轮前的差异

逐端 fresh decompile 后，与本地相关代码对照确认并修复了以下偏差：

1. MotionSub helper 仍带非 native 的 `currentTime` 参数；
2. child 解析/空 root 存在静默 guard，掩盖 native malformed-state 边界；
3. teardown 没有完整覆盖 null-owner src，且未严格按 stealth/primary 顺序清两项名称；
4. replay 只按 bit 处理或用 bitwise 更新，没有消费/覆盖完整 flags byte；
5. split vector 的生命周期过短，并为两段路径加入了 native 不存在的 fallback；
6. 时间下界和差分上界使用 `std::max/std::min`，NaN 与 signed-zero 不同；
7. dofst 相等时仍读取 parent time、除法或 easing，破坏精确旁路；
8. mode 4 对空 dtgt 做了额外 guard；
9. invalid coordinate mode 错误落入 mode 0 origin/angle 平面；
10. child state 通过直接字段/错误 setter 次序传播，并混入 active、visible 或
    `forceVisible` 的非 native 复制；
11. 角度矩阵用了代数上近似等价、浮点上不等价的分组或缓存 trig；
12. 事件容器被误判为独立 residual/render vector，导致 Player 内重复出第二个 vector；
13. `getChildPlayer()` 先检查 Variant type，把本应抛出的非对象输入静默变成 null；
14. caller 帧末只清 flags 的一个 bit，而四端都写零完整 byte。

在 MotionSub 纵向封账时没有顺手重命名
`interpolatePositionVariantLike_0x69A4D4`、`evaluateBezierVariantLike_0x69A754`
和 `initEmoteMotionLike_0x6B2E90`，因为 caller 复核不能替代 helper 自身的四库证据。
随后两个相邻纵向已经分别完成 position/control/easing 与 emote-init 的真实四端映射、
重命名和行为修正；详见
`motionplayer_position_interpolation_four_binary_2026-08-12.md` 与
`motionplayer_emote_init_four_binary_2026-08-12.md`。旧 `0x6B2E90` 还被证明不是当前
Android arm64 emote-init 的入口。

## Portable 源码改动

- `PlayerUpdateChildMotion.cpp`
  - 无参数 MotionSub phase；
  - 恢复上述分支、时间、角度、visibility、矩阵和 shared-exit 顺序；
  - 保留 `if (true)`/`if (!true)` 的反编译可见 source token；
  - 使用统一 `_pendingEvents` 和
    `aggregateChildPendingEvents_guess`；
  - 清除本纵向旧单端地址注释，ABI 证据只写本文。
- `PlayerUpdateLayersInternal.h`
  - 抽出可独立测试的 teardown/replay、时间下界、双采样选择、dofst blend 和 origin helper；
  - 不添加 native 不存在的范围/零分母/NaN guard。
- `RuntimeSupport.h`
  - 2026-08-13 后续纠正：删除 `ChildMotionRenderItem_guess`；
  - 统一使用一 int/two Variant 的 `MotionEvent`；
  - 添加 event-specific prepend-at-begin + clear-retaining-capacity helper。
- `Player.h` / `PlayerUpdateParticles.cpp` / `PlayerUpdateLayers.cpp`
  - 统一成员、聚合入口、无参 phase 与完整 flags-byte 清理。
- `MotionNodeBridge.cpp` / `MotionNode.h`
  - 恢复普通 Variant object conversion 的异常边界。

## 测试与构建验证

新增三个确定性 Catch2 case，覆盖：

- teardown/replay gate，完整 flags byte 覆盖；
- dofst 相等时非法 easing/零分母旁路与 signed zero；
- 时间下界的 NaN/negative-zero，以及双采样的 NaN/临近 1 边界；
- coordinate mode 0/1/unknown 的 origin 发布；
- child pending-event prepend 顺序、两项 Variant 复制、child clear 后 capacity 保留；
- Void/Integer child Variant 的对象转换异常。

验证结果：

- 完整 `cmake --build --preset "Web Debug Build" --parallel`：最终
  `index.html/index.wasm` 链接成功；
- `cmake --build out/wasmtime/debug --target krkr2_wasmtime_guest --parallel`：链接、
  exnref 转换和 export strip 完成；随后增量复核为 `ninja: no work to do`；
- 复用 Web Debug `compile_commands.json` 的真实 Emscripten 定义/头路径，并加入
  `out/syntax-check` Catch2 与 `test_config.h`，对完整
  `tests/unit-tests/plugins/motionplayer-dll.cpp` 执行 `-fsyntax-only`：通过，仅有仓库
  既有 `_tss` literal-operator deprecation warning；
- `git diff --check`：通过，仅报告工作树既有 LF 到 CRLF 提示；
- 当前 `out/windows/debug` 仍没有有效 `build.ninja`，此前原生测试配置受外部
  vcpkg/cocos2dx 问题约束。因此本文只声明 Catch2 翻译单元已编译，不把它误报成
  原生测试 executable 已运行。

Wasmtime 首轮调用因外层 60 秒限制返回超时，但它启动的构建进程仍在正常执行
`wasm-opt --translate-to-exnref`。并行误触发的第二轮因同一输出文件被占用而失败；在
确认原进程、父子关系和命令行后未强杀它。原进程随后正常退出、Wasm 产物更新时间和
尺寸改变，最终 `ninja: no work to do` 证明完整后处理链已经提交。该文件占用不是源码
或链接符号失败。

## IDB 改进与最终保存

四份 IDB 均完成并原位保存：

- 四个主 helper 统一为 `Player_updateMotionSubNodes_guess(void *self)`；
- 聚合 wrapper/range-insert/Android armv7 erase-at-end helper 使用上文统一名称和类型；
- 在函数入口、dirty/mode gate、teardown、replay、visibility 数据流、shared exit、
  caller flags 清理和聚合入口添加四端语义注释；
- 最终 fresh decompile 再次确认四端主入口均显示新名称；
- 四端 shared exit 均显示 clip/mesh/visible-ancestor 三项传播，然后
  `Player_frameProgress_guess`、`Player_updateLayers_guess`、prepend/clear；
- Android arm64 的 child clear 保持 caller 内联形状，其余目标的 outlined wrapper
  保持各自真实结构，没有为了“统一反编译外观”人为拆函数；
- 四个 IDB save 均返回 `ok=true`。

## 后续相邻纵向

MotionSub caller 的 position Variant interpolation、Bezier easing、nested control
curve 与 direct-edit `initEmoteMotion` 均已在相邻纵向完成。这里原先列出的
`Player_loadMotion_guess` caller 与 `Player_evaluateTimeline_guess` type-specific 输出也已分别
完成四端 fresh-decompile 闭环；后者于 2026-08-15 再次核对 type 4/5/10 的 copy/lerp 与
早退边界。本记录不再把它们列为未完成项。
