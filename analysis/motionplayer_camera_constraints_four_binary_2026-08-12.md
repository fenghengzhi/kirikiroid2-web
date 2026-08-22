# MotionPlayer type-9 camera constraints 四端对照（2026-08-12）

## 结论

本专题以 `reference/binaries/` 中 Android arm64、Android armv7、iOS
arm64、iOS armv7 四个当前参考二进制为共同真值，重新核验
`Player::updateLayers` 中 type-9 camera-constraint 阶段的调用顺序、目标节点查找、
约束类型翻转、三轴累加器、节点平移和跨帧 dirty 生命周期。旧
`libkrkr2.so` 地址及其衍生注释不再作为证据。

四端控制流一致，主要差异来自指针宽度、节点步长和 libstdc++/libc++ deque
布局。旧本地实现有四处会改变可观察行为的偏差，现已按四端共同结果修复：

- 不应以 `accumulated.active` 过滤约束节点；原生 gate 只有 type 9 与当前
  clip slot 的 `done == false`。
- 目标来自当前 slot 的原始 `anchor.target`，通过 Player 的有序标签表查找；
  miss 回退到根节点，不是恒用根节点。
- 水平翻转对 type 2 的映射是反直觉的 `2 -> 3`，不是 `2 -> 0`。
- 相机约束实际发生平移时会发布一个 Player 跨帧 dirty 字节；下一帧每个节点
  的 timeline-dirty 输入都要包含该字节。该字节在本帧约束阶段前清零，因此
  不是同帧反馈。

## 函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `Player_updateLayers_guess` | `0x6B871C` (`0xAE4`) | `0x5856E0` (`0x9DC`) | `0x10010E544` (`0xB50`) | `0x10BE5C` (`0xA76`) |
| `Player_applyCameraConstraints_guess` | `0x6B93E0` (`0x4F0`) | `0x586228` (`0x4C4`) | `0x10010F22C` (`0x438`) | `0x10CA04` (`0x42A`) |
| `Player_findNodeByRawLabel_guess` | `0x6B2EB8` (`0x144`) | `0x58220C` (`0xB4`) | `0x100109EEC` (`0x118`) | `0x10777C` (`0x120`) |

Android arm64 的 phase 将有序表查找 helper 内联到调用点；实际红黑树 walker
保留在 `0x6EF608`（`0xE4`）。它按 `ttstr` key 比较并在 miss 时返回
Player 内的 map sentinel。由于这是泛型容器实例化而非可以由四端共同确定的原始
源函数名，IDB 中没有强行给它赋予过度具体的名称。

## 阶段顺序与数据流

四端的共同顺序是：

```text
for each node in main update loop:
    timelineDirtyArg = player.cameraConstraintDirty
                     || node.groundCorrection
                     || parent.accumulated.dirty
                     || node.delta.dirty
    evaluate/update node using timelineDirtyArg

player.cameraConstraintDirty = false
applyCameraConstraints(player)
continue later updateLayers phases
```

因此 dirty 字节是跨帧发布器：第 N 帧 camera phase 写入；第 N+1 帧主节点循环
消费；第 N+1 帧 camera phase 之前又清零，随后只在该帧确有非零最终偏移时重新
发布。没有同帧重新执行前面的 timeline evaluation。

camera phase 自身在 preview 模式直接返回；节点数不足 2 时也返回。Android
libstdc++ deque 的反编译表达式看似带 block 偏置，但还原后的逻辑数量检查与两个
iOS libc++ deque 目标相同，都是要求至少存在根节点和一个实际节点。

## 关键 ABI 对照

这些偏移只用于反编译复核，不应进入 portable C++ 源码注释。

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| MotionNode 步长 | `2632` | `2272` | `2648` | `2228` |
| `nodeType` | `+28` | `+20` | `+28` | `+20` |
| active slot index | `+1392` | `+1160` | `+1392` | `+1128` |
| slot 步长 | `536` | `432` | `536` | `420` |
| slot 0 `done` | `+344` | `+320` | `+344` | `+308` |
| slot 0 `anchor.target` | `+836` | `+712` | `+836` | `+692` |
| `anchorType_guess` | `+2376` | `+2040` | `+2392` | `+2004` |
| accumulated flip X/Y | `+1507/+1508` | `+1267/+1268` | `+1523/+1524` | `+1235/+1236` |
| accumulated X/Y/Z | `+1512/+1520/+1528` | `+1272/+1280/+1288` | `+1528/+1536/+1544` | `+1240/+1248/+1256` |
| Player preview | `+1092` | `+744` | `+980` | `+680` |
| Player camera dirty | `+610` | `+410` | `+498` | `+346` |

`anchorType_guess` 是前一轮节点树构造专题已经交叉确认的单一物理字段；没有另
一个 camera 专用 type 字段。active slot 的地址由节点内 slot 基址加 active-slot
index 乘 slot 步长得到。四端都先读 slot 的 `done`，只对 `done == false` 的
type-9 节点继续。

## 共同伪代码

下列伪代码合并了四端的 deque/STL 展开差异，同时保留条件顺序、覆盖顺序和
浮点比较边界：

```text
if player.preview or nodes.size < 2:
    return

minX = minY = minZ = +double(FLT_MAX)
maxX = maxY = maxZ = -double(FLT_MAX)
directX = directY = directZ = 0
hasMinX = hasMinY = hasMinZ = false
hasMaxX = hasMaxY = hasMaxZ = false
hasDirectX = hasDirectY = hasDirectZ = false

for nodeIndex in [1, nodes.size):
    constraint = nodes[nodeIndex]
    slot = constraint.slots[constraint.activeSlotIndex]
    if constraint.type != 9 or slot.done:
        continue

    targetIndex = findNodeByRawLabel(slot.anchor.target, recursive=false)
    target = nodes[targetIndex >= 0 ? targetIndex : 0]

    type = constraint.anchorType_guess
    if constraint.accumulated.flipX:
        if type == 0: type = 2
        else if type == 2: type = 3       // intentional four-binary result
    if constraint.accumulated.flipY:
        if type == 3: type = 5
        else if type == 5: type = 3

    switch type:
      0: if target.x < constraint.x:
             delta = target.x - constraint.x
             if delta <= minX: minX = delta; hasMinX = true
      1: directX = target.x - constraint.x; hasDirectX = true
      2: if target.x > constraint.x:
             delta = target.x - constraint.x
             if delta >= maxX: maxX = delta; hasMaxX = true
      3: same minimum rule for Y
      4: same direct rule for Y
      5: same maximum rule for Y
      6: same minimum rule for Z
      7: same direct rule for Z
      8: same maximum rule for Z

offsetX = hasDirectX ? directX : hasMaxX ? maxX : hasMinX ? minX : 0
offsetY = hasDirectY ? directY : hasMaxY ? maxY : hasMinY ? minY : 0
offsetZ = hasDirectZ ? directZ : hasMaxZ ? maxZ : hasMinZ ? minZ : 0

if offsetX != 0 or offsetY != 0 or offsetZ != 0:
    player.cameraConstraintDirty = true
    for nodeIndex in [1, nodes.size):
        nodes[nodeIndex].accumulated.position += (offsetX, offsetY, offsetZ)
```

直接约束每次无条件覆盖，所以最后一个同轴 direct 节点胜出。min 使用 `<=`，
max 使用 `>=`，同值时后一个候选也会覆盖累加器，但最终数值不变。最终优先级为
`direct > max > min > 0`，不是按节点出现顺序统一竞争。

## 目标节点查找与边界

四端均使用 Player 的原始标签有序表，查找参数明确为非递归：

- key 是当前 active slot 的 `anchor.target`，不是节点自身 label，也不是 action
  path。
- 空字符串不特殊处理；如果表中确有空 key，就可以命中。
- 比较是 `ttstr` 有序比较，保留大小写和前导斜杠；不做路径正规化。
- miss 返回负值，camera phase 将其改为根节点索引 0。
- hit 后直接以返回整数索引 deque，没有额外 bounds guard。若内部 map 被破坏或
  存有越界索引，原生边界行为不是“安全回退”。本地实现也不应自行增加 clamp。

根节点只作为约束目标 fallback；最后的平移循环仍从节点 1 开始，不移动根节点。

## flip remap 的反直觉分支

四端 machine disassembly 均直接证明水平翻转的第二个 literal 是 3：type 0
变 2，type 2 变 3。该分支不能按“左右互换”直觉改成 2->0。随后垂直翻转只
交换 3 与 5，所以组合例子包括：

```text
type 0 + flipX            => 2
type 2 + flipX            => 3
type 2 + flipX + flipY    => 5
type 3 + flipY            => 5
type 5 + flipY            => 3
```

这一行为可能源于上游枚举/实现缺陷，但四个参考产物一致，故属于当前兼容目标。

## 浮点常量、NaN 与严格比较

三轴 min 初值是 `+static_cast<double>(FLT_MAX)`，max 初值是其负值。A64
常量池字节直接给出：

```text
+FLT_MAX promoted to double: 0x47EFFFFFE0000000
-FLT_MAX promoted to double: 0xC7EFFFFFE0000000
```

这不同于先写十进制 double literal `3.40282347e38`；后者在本机得到
`0x47EFFFFFE091FF3D`。本地 helper 因此从 `numeric_limits<float>::max()`
提升到 double，而不是近似十进制常量。

有序比较也必须原样保留：

- min/max 候选含 NaN 时，位置方向比较和累加器比较都会失败，不提交候选。
- direct 不比较，NaN 会直接写入 direct 累加器并置位。
- 最终 `NaN != 0.0` 为 true，因此 direct NaN 会发布 dirty，并把 NaN 平移传播
  给所有非根节点。
- `-0.0 != 0.0` 为 false，不会仅因负零发布 dirty。

Android armv7 与 iOS armv7 的 Hex-Rays 输出曾把最终条件打印得像只检查 X/Z；
逐指令复核确认两端都对 X、Y、Z 三个 double 依次比较。两个 64 位目标也明确
检查三轴，因此 portable 源码使用完整三轴条件。

## 跨帧 dirty 生命周期

| 操作 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| 主循环读取 | `0x6B89A0` | `0x5858E4` | `0x10010E824` | `0x10C29E` |
| camera phase 前清零 | `0x6B903C` | `0x585FEC` | `0x10010EF7C` | `0x10C812` |
| 非零偏移后置位 | `0x6B9824` | `0x586662` | `0x10010F5C8` | `0x10CDBE` |

四端构造路径都清该字节。主循环把它与当前节点 ground-correction、父节点
accumulated dirty 和当前节点 delta dirty 做 OR，然后传给 timeline evaluation。
字节不按节点消费，也不会在主循环中提前清除；本帧每个节点看到相同的上一帧值。
主循环结束后统一清零，再执行 camera phase。

即使某轴有约束候选，只要解析后的三个最终 offset 都比较为零，就不置 dirty，
也不进入平移循环。进入平移循环后，三个 offset 会一起加到每个非根节点，即便
其中一轴为零。

## 本地差异与修复

| 旧本地行为/注释 | 四端证据 | 修复 |
|---|---|---|
| 注释称 target 查找未实现，恒以根位置计算 | 当前 slot 的 raw `anchor.target` 经有序 map 查找，miss 才回 root | 接入 `_nodeIndexByRawLabel`，保留 empty-key 与无 bounds guard 边界 |
| 额外要求 `accumulated.active` | phase 没有 active gate | 删除 active 条件 |
| `flipX` 把 type 2 映射到 0 | 四端指令 literal 均为 3 | 恢复 2->3 |
| 使用近似十进制 double 极值 | 常量为 float max 精确提升 | 引入 `static_cast<double>(numeric_limits<float>::max())` |
| 只完成局部偏移，未发布后续 dirty | 四端都有构造清零、上一帧读取、本帧清零/置位生命周期 | 增加 `_cameraConstraintDirty_guess` 并接入主循环 |
| 相关注释引用旧单一 `libkrkr2.so` 地址 | 当前真值为四个参考产物 | 改为 portable 语义注释；地址只保留在本文 |

## 测试与构建验证

确定性单元测试新增覆盖：

- promoted `FLT_MAX` 的精确 bit pattern `0x47EFFFFFE0000000`；
- flip remap，包括关键 `2 -> 3` 和双翻转 `2 -> 5`；
- 同轴解析优先级 `direct > max > min > 0`；
- direct NaN 被保留；
- raw-label map 的命中/miss、空 key 和前导斜杠不等价。

本轮验证结果：

- Web debug `index.html` 完整增量构建成功。
- Wasmtime guest 完整增量构建成功。
- Emscripten 对 `tests/unit-tests/plugins/motionplayer-dll.cpp` 的 syntax-only
  检查成功。
- 仅出现项目既有 `_tss` 与 imagepacker `nodiscard` 警告。
- Windows 原生 Catch 可执行文件仍因既有 vcpkg/cocos2dx 配置问题不可用；没有
  为得到假阳性而制造替代 fixture。

## IDB 改进

四个 IDB 已统一命名并保存：

- camera phase 统一命名为 `Player_applyCameraConstraints_guess`；
- raw-label helper 统一命名为 `Player_findNodeByRawLabel_guess`；
- phase 中能可靠对应的约束节点、目标节点、约束类型和 X/Y/Z offset 局部变量
  已命名；iOS arm64 的目标指针与函数入参/返回共用同一 Hex-Rays 变量，未为追求
  表面统一而错误拆名；
- raw lookup、flip remap、dirty 置位，以及主函数 dirty 读取/清零处均已写入
  行级注释；
- fresh decompile 已确认调用名和关键局部变量传播。

四份数据库在本轮结束时均通过 IDA 原生保存成功。后续仍应把单端 STL 展开和
Hex-Rays 临时变量视作编译器表现，只把四端共同控制流提升为源代码结论。
