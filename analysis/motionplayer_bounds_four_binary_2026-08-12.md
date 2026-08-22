# MotionPlayer `bounds` 四参考二进制纵切面（2026-08-12）

## 1. 结论

四份当前参考二进制在 `Motion.Player.bounds`、`Motion.EmotePlayer.bounds` 和
递归 AABB 计算上语义一致。旧源码中“总是返回四个坐标”“无 motion 时返回全零”以及
`1e308`/first-item 合并均不是当前四端实现。

准确行为是：

1. 两个脚本类都把 `bounds` 注册成只读属性；`EmotePlayer` 仅转发给内嵌
   `Player`，没有 facade 本地 AABB；
2. 每次读取都新建一个 Dictionary；
3. 若 `maxX >= minX && maxY >= minY` 不成立，Dictionary 只有
   `isValid=false`；
4. 若顺序成立，则依次插入 `left/top/right/bottom/width/height` 六个 Real，
   最后插入 Boolean `isValid`；
5. `isValid` 不是普通 ordered/finite 测试。四个值都必须是“符号位为 0 的有限
   binary64”；因此负有限数、`-0.0`、任意 NaN 和两个无穷值都无效；
6. `calcBounds` 入口先独立保留一个 Player 所持 Object dispatch，然后无条件把
   Player AABB 重置为精确 `+DBL_MAX,+DBL_MAX,-DBL_MAX,-DBL_MAX`；
7. 点和 child AABB 都用包含等号的 `<=`/`>=` 比较逐成员合并。首点 NaN 不会
   污染哨兵；相等的后写 `-0.0` 可以替换先写 `+0.0`；
8. 每个普通节点的临时 AABB 使用 float 和精确 `±FLT_MAX`，最后以
   `floorf/floorf/ceilf/ceilf` 写回四个 float；
9. 无贡献节点时 Player 保持无序 double 哨兵，getter 因而只返回
   `isValid=false`，绝不把它改成零矩形。

## 2. 函数与注册映射

| 目标 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| Player `bounds` 注册 | `0x6D4EFC` | `0x59830E` | `0x100124B40` | `0x123E12` |
| `Player_getBounds_guess` | `0x6C9E64` | `0x59226C` | `0x10011CBD4` | `0x11B53C` |
| EmotePlayer `bounds` 注册 | `0x67DC34` | `0x56164A` | `0x1001B5618` | `0x1B5266` |
| `EmotePlayer_getBounds_guess` | `0x67F28C` | `0x562040` | `0x1001B610C` | `0x1B5EC4` |
| `classifyIEEE754Real_guess` | `0xA0C7A0` | `0x75F618` | `0x1002583C4` | `0x259750` |
| `Player_calcBoundsRecursive_guess` | `0x6C10E4` | `0x58BE38` | `0x100115C68` | `0x11354C` |

四个 EmotePlayer getter 是单调用转发器，嵌入 Player 指针位置分别为：

| Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---:|---:|---:|---:|
| `+1064` | `+532` | `+696` | `+348` |

## 3. Player AABB 布局

| 目标 | minX | minY | maxX | maxY |
|---|---:|---:|---:|---:|
| Android ARM64 | `+152` | `+160` | `+168` | `+176` |
| Android ARMv7 | `+120` | `+128` | `+136` | `+144` |
| iOS ARM64 | `+128` | `+136` | `+144` | `+152` |
| iOS ARMv7 | `+104` | `+112` | `+120` | `+128` |

四项都是 binary64。getter 未做 float 转换；`width` 和 `height` 也在 double 域直接
相减，然后以 TJS Real 写入。

## 4. Getter 的公共伪代码

```text
getBounds(player):
    dict = new Dictionary

    if player.maxX >= player.minX && player.maxY >= player.minY:
        dict.left   = Real(player.minX)
        dict.top    = Real(player.minY)
        dict.right  = Real(player.maxX)
        dict.bottom = Real(player.maxY)
        dict.width  = Real(player.maxX - player.minX)
        dict.height = Real(player.maxY - player.minY)

        valid = classify(player.minX) == 0 &&
                classify(player.maxX) == 0 &&
                classify(player.minY) == 0 &&
                classify(player.maxY) == 0
        dict.isValid = Boolean(valid)
    else:
        dict.isValid = Boolean(false)

    return Variant(Object=dict, ObjThis=dict)
```

四端插入顺序完全相同，且每个成员都使用独立的持久 hint 槽；flags 为
`TJS_MEMBERENSURE`。所有 `PropSet` 返回值都被忽略。旧实现中只写四个坐标且无条件
暴露它们，与四端均不符。

### 4.1 Dictionary 生命周期

getter 的构造/返回链为：

1. 创建 Dictionary dispatch；
2. property-builder/facade 临时对象持有该 dispatch；
3. 构造返回 Object Variant，`Object` 与 `ObjThis` 都指向同一 dispatch；
4. facade 析构释放自己的引用，返回 Variant 保留最终 owner。

四端没有“创建后判空并返回 Void”的分支。源码已删除旧防御性空检查，以保持相同
调用链与边界。

## 5. `isValid` 分类器

分类器直接检查 binary64 符号、指数和尾数。公共等价逻辑为：

```text
if exponent != 0x7ff:
    return sign ? 8 : 0
if mantissa != 0:
    return sign ? 9 : 1
return sign ? 10 : 2
```

| 输入类别 | 正符号 | 负符号 |
|---|---:|---:|
| 有限值（含 subnormal、零） | `0` | `8` |
| NaN | `1` | `9` |
| infinity | `2` | `10` |

getter 只关心结果是否为零，所以源码级精确等价式为：

```cpp
std::isfinite(value) && !std::signbit(value)
```

这保留了几个反直觉但可观察的边界：

- 有序的负坐标矩形仍有六个 geometry member，但 `isValid=false`；
- `-0.0` 保持 TJS Real 的符号位，并令 `isValid=false`；
- 正有限数（包括 `+0.0` 和正 subnormal）有效；
- NaN 通常先令 ordered 比较失败，因此只出现 `isValid=false`；
- infinity 可以满足 ordered 比较，因此可出现六个 geometry member，但仍无效。

四次分类调用严格短路，顺序为 `minX,maxX,minY,maxY`。

## 6. `calcBounds` 入口 owner 与哨兵

四端入口均先 CopyRef 一个 Player Variant、将其转换为 Object 并额外保留 dispatch，
然后销毁临时 Variant；最终在正常退出和异常展开时释放 retained dispatch。字段位置为：

| Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---:|---:|---:|---:|
| `+0x27C` (`+636`) | `+0x1AC` (`+428`) | `+0x20C` (`+524`) | `+0x16C` (`+364`) |

该 owner 在函数体内没有方法调用，但生命周期仍是二进制可见结构。源码使用与现有
node-tree teardown 相同的 `Variant copy -> AsObject -> copy.Clear -> RAII Release`
形状予以保留。非 Object Variant 在哨兵重置前抛出转换异常。

四端在销毁该临时 Variant 后都直接写入下面的四个 sentinel；中间没有读取 motion
context、构造路径字符串或调用诊断 helper。旧 Web 源码残留的一次未使用
`matchedMotionPath()` 已从普通路径移出，仅在显式启用 Web logo-chain 诊断 sidecar 时惰性
构造。该差异在 motion context 为 Octet 时可观察：Octet 不能转为 `ttstr`，但默认参考形状
的 `calcBounds` 完全不读取该字段，因而仍正常重置 sentinels；回归用例已固定这一无关字段
隔离边界。显式启用的非原版诊断模式仍允许读取路径，这是探针自身的 opt-in 行为。

随后四端无条件写入精确 bit pattern：

```text
minX = 0x7fefffffffffffff  // DBL_MAX
minY = 0x7fefffffffffffff
maxX = 0xffefffffffffffff  // -DBL_MAX
maxY = 0xffefffffffffffff
```

旧源码的 `hasMotionContent()` 全零早退和十进制 `1e308` 均已移除。

## 7. 节点遍历与合并数据流

公共控制流：

```text
for nodeIndex = 1 .. nodeCount-1:
    if !preview && node.type == particle:
        for each particle child:
            child.calcBounds()
            merge child Player AABB

    if activeSlot.done:
        continue

    if !preview && node.type == nested-motion:
        child.calcBounds()
        node.bounds = float(child Player AABB)
        merge node.bounds
        continue

    if node.type not in (preview ? 0x1449 : 0x1441):
        continue
    if !node.source.valid:
        continue

    nodeMinX = +FLT_MAX
    nodeMinY = +FLT_MAX
    nodeMaxX = -FLT_MAX
    nodeMaxY = -FLT_MAX

    if compositeMeshPoints not empty:
        scan every composite point
    else if transformedMeshControlPoints not empty:
        scan exactly 16 points
    else:
        scan exactly 4 ordinary corners

    node.bounds = [floorf(minX), floorf(minY),
                   ceilf(maxX), ceilf(maxY)]
    merge node.bounds into Player AABB
```

每个 `extendPoint` 和 `merge` 都是四次互相独立的包含等号比较：

```text
if incomingMinX <= currentMinX: currentMinX = incomingMinX
if incomingMinY <= currentMinY: currentMinY = incomingMinY
if incomingMaxX >= currentMaxX: currentMaxX = incomingMaxX
if incomingMaxY >= currentMaxY: currentMaxY = incomingMaxY
```

没有 `haveBounds`/`haveNodeBounds`，没有先检查 incoming AABB 是否 ordered，也没有
首元素整块赋值。由此自然得到：

- NaN 比较为 false，不更新相应哨兵；
- 无序 child 哨兵通常不更新 parent；
- 相等值会执行后写，故 signed zero 的最终符号取决于遍历顺序；
- 无贡献时 sentinels 原样保留。

### 7.1 type-4 粒子 Array owner 补充

后续四端复审确认：每个非 preview type-4 节点只从持久 Array `Variant` 建立一次
retained dispatch owner，该 owner 覆盖 count、所有数字元素读取和全部 child 递归
`calcBounds`。这使 getter/child 重入清空或替换持久字段时仍继续使用原 receiver。源码已
从逐次 `MotionNode::getParticleCount/getParticleChild` 重新借用改为调用方单 owner；精确
地址、异常边界与回归见
`motionplayer_calc_bounds_particle_owner_four_binary_2026-08-14.md`。

### 7.2 type-3 borrowed child 补充

type-3 路径直接从 node 持久 child `Variant` 借用 dispatch/native Player，不建立局部 owner，
且无 null guard。child 递归返回后，四个 double 必须先逐项窄化并发布到 node float AABB，
再提升合并 parent double AABB；重入替换字段不会刷新 raw child，最后 owner 被清除时可形成
原版悬空边界。四端地址、精度与异常顺序见
`motionplayer_calc_bounds_type3_borrowed_child_four_binary_2026-08-14.md`。

### 7.3 点容器选择补充

普通 node 依次选择 composite vector 全范围、transformed vector 固定 16 点、内联固定 4 角；
transformed 只以非空为 gate，size>16 忽略尾部，size<16 继续越过逻辑 end。两个连续 vector
的四端 record/字段偏移、循环地址和回归见
`motionplayer_calc_bounds_point_container_selection_four_binary_2026-08-14.md`。

### 7.4 node-type mask 与 shift UB

合法域 normal mask `0x1441` 接受 `{0,6,10,12}`，preview mask `0x1449` 额外接受 type 3；
表达式保持 unchecked `1 << nodeType`。malformed shift 在 C++17 属于 UB，成品 AArch64 取低
5 位，而 AArch32 register shift 取低 8 位后对 32..255 产生零；Web wasm 跟随低 5 位。
精确指令与跨端示例见
`motionplayer_calc_bounds_node_type_mask_shift_four_binary_2026-08-14.md`。

## 8. 源码修复

本轮修改：

- `PlayerCore.cpp`
  - 重建七成员/一成员的条件 Dictionary；
  - 增加七个持久 member hint；
  - 以 `isfinite && !signbit` 恢复分类器语义；
  - 保留 property 顺序、typed Boolean、忽略 `PropSet` 返回值和精确返回 owner；
- `PlayerRenderItems.cpp`
  - 恢复入口 Object dispatch owner；
  - 移除无 motion 的零矩形捷径；
  - 改用精确 DBL/FLT sentinels、float 节点 min/max 和包含等号的逐成员比较；
  - 移除 `haveBounds`/`haveNodeBounds` 与 unordered 预过滤；
- `Player.h`、`MotionNode.h`、`main.cpp`
  - 清理本纵切面中指向旧 `libkrkr2.so`/M15 的过时注释，改为地址无关语义；
  - 添加未注册的 `_guess` 测试观察入口，不改变脚本 API；
- `tests/unit-tests/plugins/motionplayer-dll.cpp`
  - 覆盖 fresh Dictionary、无序一成员形状、EmotePlayer 转发、正有限、负有限、
    `+0/-0` 后写、首点 NaN、infinity、空遍历 sentinels 和非 Object owner 异常。

## 9. IDB 改进

四份 IDB 均已：

- 将 8 个 Player/EmotePlayer getter 统一命名；
- 将 4 个 binary64 分类器命名为 `classifyIEEE754Real_guess`；
- 在 getter、转发器、分类器、递归 AABB 函数和 8 个属性注册点写入语义注释；
- 对每端 getter、转发器、分类器和 `calcBounds` 共 4 个函数强制清除 Hex-Rays
  缓存，并在本轮对话中 fresh decompile；
- 成功原位保存数据库。

## 10. 验证

- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` 使用 Web
  `compile_commands.json` 的真实 Emscripten 定义、头路径及
  `out/syntax-check` Catch2/test config 执行 `-fsyntax-only`：通过；唯一诊断为
  仓库既有 `_tss` literal-operator 弃用警告；
- `cmake --build out/web/debug --target motionplayer --parallel 8`：通过；
- `cmake --build out/wasmtime/debug --target motionplayer --parallel 8`：通过；
- `cmake --build out/wasmtime/debug --target krkr2_wasmtime_guest --parallel 8`：
  通过；31 个受影响对象重编，最终 wasm 链接和 `wasm-opt` exnref 转换成功；
- `cmake --build out/web/debug --target krkr2 --parallel 8`：通过；31 步受影响对象
  重编并成功链接 `index.html`；
- `cmake --build out/web/debug --parallel 8`：完整默认 Web target 通过；
- 上述两套静态库、Wasmtime guest、Web `krkr2` 和 Web 默认 target 串行复验均为
  `ninja: no work to do`；
- `git diff --check`：通过；输出仅为工作树既有 LF/CRLF 转换警告。
