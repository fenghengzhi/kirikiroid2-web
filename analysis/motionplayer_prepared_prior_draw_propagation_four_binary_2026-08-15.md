# MotionPlayer prepared-item 节点级 `priorDraw` 传播四端复原（2026-08-15）

## 结论

四个当前参考二进制都在 `MotionNode` 内保存一字节 Boolean `priorDraw`。它不是
`Player::priorDraw` 属性的别名：前者来自当前 node 保留的 `emoteEdit["priorDraw"]`，控制
prepared-item 的 flag18 及 child 递归传播；后者位于 Player，自成一条 render-command / submit
门控数据流。

`Player_appendPreparedRenderItems_guess` 的第三个 lineage Boolean 可还原为单调 OR 链：

```text
childInheritedFlag18 = callerInheritedFlag18 || ownerNode.priorDraw
ordinaryItem.flag18 = callerInheritedFlag18 || currentNode.priorDraw
```

type-4 particle child、type-3 plain child 和 type-3 wrapper child 三条递归路径都使用当前
container node 的 Boolean；没有任何一路读取 `Player::_priorDraw`。一旦某个祖先 node 把该值
置真，本次递归子树内不会再清零，最终每个普通 child item 都发布真 flag18。

旧可编译源码注释只记录了 libkrkr2 风格的 `sub_6636D4`、`0x6BC6C4` 和统一
`node+48`。它不适用于当前四份 reference：当前 Android arm64 的 `0x6BC6C4` 位于
`Player_updateParticleSystems_guess` 内，不是 `priorDraw` 写入点；`+48` 也只吻合两个
64 位布局，两个 32 位布局的字段在 `+40`。本轮已删除这些旧地址，并按源级语义恢复字段类型
与注释。

## 四目标布局与写入者

| 目标 | `MotionNode` stride | node `priorDraw` byte | vertex writer | property-result store | false store |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0xA48` | `+0x30` | `0x6B98D0` | `0x6B9AA4` | `0x6B9A5C` |
| Android armv7 | `0x8E0` | `+0x28` | `0x5866F8` | `0x586846` | `0x58685E` |
| iOS arm64 | `0xA58` | `+0x30` | `0x10010F6AC` | `0x10010F7C0` | `0x10010F7F4` |
| iOS armv7 | `0x8B4` | `+0x28` | `0x10CE30` | `0x10DAF2` | `0x10DB18` |

四个 writer 都是 `Player_updateLayersVertexComputation_guess`，共同控制流为：

```text
if currentNode.forceVisible:
    emoteEditCopy = CopyRef(currentNode.emoteEditVariant)
    currentNode.priorDraw = bool(
        emoteEditCopy.PropGet("priorDraw", same_receiver_objthis))
    release emoteEditCopy
else:
    currentNode.priorDraw = false

continue with parent mesh-state / mesh-ancestor resolution
```

Android arm64 在 helper 返回后显式 `AND W8, W0, #1` 再 `STRB`；其余三端直接存 helper
返回值的低 byte。这里不是四份源代码的语义差异：helper 的源级返回类型是 Boolean，正常调用
已经产生规范 `0/1`。四端 false 分支都覆盖写零，因此旧帧的真值不会在
`forceVisible == false` 时残留。

读取 `priorDraw` 发生在 node 初始化之后的 vertex pass。初始化只 CopyRef 原始
`emoteEdit` owner；property callback 在 parent mesh-state bytes 被消费前运行，并以独立保留的
Variant copy 覆盖 callback 生命周期。这与本地 `PlayerUpdateGeometry.cpp` 的 owner 和执行顺序
一致。

## 三条 child 递归路径

| 目标 | recursive builder | type-4 call | type-3 wrapper call | type-3 plain call |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6BF714` | `0x6C0A8C` | `0x6BFF3C` | `0x6C0504` |
| Android armv7 | `0x58B178` | `0x58B332` | `0x58BABC` | `0x58BAFA` |
| iOS arm64 | `0x1001148F8` | `0x100114ABC` | `0x100115160` | `0x1001153BC` |
| iOS armv7 | `0x1123D8` | `0x11282A` | `0x112CFA` | `0x112D44` |

每个 call site 都在调用前读取所选 owner node 的 `+0x30/+0x28` byte，并与 caller 的
lineage 参数合并：

- type-4：对 particle Array 的每个 numeric element 重复计算并传同一 OR 结果；
- type-3 plain：child 直接写 caller main/aux vectors；
- type-3 wrapper：child main 改为 persistent wrapper 的 child vector，并强制 draw flag，
  但 flag18 的 OR 规则不变。

inherit-color bit `0x200`、draw flag19 和 flag18 是相互独立的实参；node `priorDraw` 只参与
最后一项。wrapper 是否进入 aux、child 是否直写 main 也不改变这条 Boolean 数据流。

## 普通 item 发布

| 目标 | node byte read / decision | `PreparedRenderItem` flag18 byte store | item byte offset |
|---|---:|---:|---:|
| Android arm64 | `0x6C0794` | `0x6C07A0` | `+0x12` |
| Android armv7 | `0x58B460` | `0x58B46A` | `+0x0A` |
| iOS arm64 | `0x100114C60` | `0x100114C68` | `+0x12` |
| iOS armv7 | `0x11260A` | `0x112616` | `+0x0A` |

四端都先检查 caller lineage Boolean。它为真时直接发布真；为假时才读取当前普通 node 的
`priorDraw`，把 `node != 0` 写入 item flag18。该 store 在 source admission、persistent item
ensure 和若干基础 item 字段写入之后，但在 command-key Variant 转字符串及最终 main-vector
push 之前。后续异常不会回滚已经写入 persistent item 的 flag18。

对 child 子树而言，两级 OR 会自然叠加：container node 在递归 call 处写入 inherited flag，
child builder 再把它与 child 当前普通 node 的 `priorDraw` 合并。没有 XOR、赋值覆盖或
`Player::_priorDraw` fallback。

## Player 属性与 node 字段严格分离

`Player::priorDraw` 的 NCB getter/setter、默认值和 render submit 数据流已记录在
`motionplayer_outline_meshline_prior_draw_four_binary_2026-08-12.md`。本轮重新扫描四个
recursive builder 后，三个 self-call family 的 flag18 构造都只从 selected node base 读取
`+0x30/+0x28`；没有从 Player 属性区取值。

因此下列状态完全合法：

```text
Player.priorDraw = true
type3Node.priorDraw = false
callerInheritedFlag18 = false
=> child ordinary item.flag18 = false
```

Player 属性仍可在稍后的 command build / submit 阶段改变哪些 item 被处理；它不会反向改写
node field，也不会在 prepared-item recursion 中冒充 node lineage。

## 非规范 Boolean ABI 边界

内部调用者、node writer 和外层 prepare wrapper 都只生产规范 C++ Boolean `0/1`，所以四端
正常行为一致。若外部 hook 绕过 C++ 类型系统，直接向 recursive builder 的最后一个 ABI
参数注入非规范 byte，则编译器产物存在可观察差异：

- 两个 arm64 产物按 bit 0 检查（`a6 & 1`）；
- Android armv7 的若干分支按 `a6 != 0` 检查；
- iOS armv7 可见 byte OR 形式。

这是伪造无效 `bool` 表示后的 ABI 外行为，不应被提升为源级插件契约。本地签名继续使用
`bool`，所有受支持调用路径保持规范表示。

## 本地修正与测试

本轮本地调整为：

1. `MotionNode::priorDraw` 从宽 `int` 改为一字节 `bool`，默认 `false`；
2. vertex pass 直接保存 Boolean property 结果，并在 false 分支覆盖 `false`；
3. child 递归和普通 item 发布使用明确的 Boolean OR；
4. 删除 `sub_6636D4`、`0x6BC6C4` 和统一 `node+48` 旧注释；
5. 扩展 type-3 render-builder 单测，分别锁定 Player 属性不参与、owner node 真值传播和
   caller inherited 真值传播。

测试特意先令 `parent.setPriorDraw(true)`、node flag 为 false，确认 child item flag18 仍为
false；再分别打开 node flag 和 caller inherited flag，确认两者都能独立把 persistent child
item flag18 置真。

## recovery IDB 改善

四个 recovery IDB 均在以下位置追加语义注释/书签：

- vertex pass 的 Boolean property-result store 与 false overwrite；
- type-4、type-3 wrapper、type-3 plain 三类 recursive call 的 node-level OR；
- ordinary item flag18 publication；
- Player 属性与 node byte 的 owner 分离。

绝对地址只保留在本文和 recovery IDB，不进入新的可编译源码注释。

