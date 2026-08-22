# `Player_initNodeFields_guess` 四端字段流与可重入 preview 边界（2026-08-12）

## 四端入口

本轮对 `reference/binaries/` 四个当前参考二进制重新反编译完整 node field initializer：

| 参考二进制 | 入口 | 大小 |
|---|---:|---:|
| Android ARM64 | `0x6B1058` | `0xC8C` |
| Android ARMv7 | `0x580FA4` | `0x58A` |
| iOS ARM64 | `0x100108720` | `0x778` |
| iOS ARMv7 | `0x105E70` | `0x820` |

四端函数原型均可恢复为：

```text
void Player_initNodeFields_guess(Player *player,
                                 MotionNode *node,
                                 Variant const *layer)
```

没有第四个 preview 参数。ARM64 的更大体积主要来自 Variant/ttstr 引用计数和异常清理的
展开，不代表额外的逻辑字段。

## 共同字段顺序

四端共同数据流为：

```text
retain dispatch from a copy of layer Variant

if emoteEdit exists: node.emoteEdit = layer.emoteEdit
else:                node.emoteEdit.clear()
node.label          = layer.label
node.parameterEntry = selectParameterEntry(layer.parameterize)
node.coordinate     = layer.coordinate
node.joinTarget     = bool(layer.joinTarget)
node.groundCorrect  = bool(layer.groundCorrection)
node.frameList      = layer.frameList
node.inheritMask    = layer.inheritMask

transform = retain object(layer.transformOrder)
for i in 0..3:
    node.transformOrder[i] = int(transform[i], default=0)

node.meshTransform = layer.meshTransform
if node.meshTransform != 0:
    node.meshSyncChildMask = layer.meshSyncChildMask
    node.meshDivision      = layer.meshDivision
    if layer.meshCombine exists:
        node.meshCombine = bool(layer.meshCombine)

node.type = layer.type
if layer.stencilType exists: node.stencilType = layer.stencilType
else:                        node.stencilType = 0

switch node.type:
    0:  node.objTriPriority = layer.objTriPriority
    1:  node.shape          = layer.shape
    3:  initialize type-3 child player
    4:  initialize particle state/properties/child Array
    6:  node.emitterActive = false
    9:  node.anchor        = layer.anchor
    12: node.stencilCompositeMaskLayerList = layer.stencilCompositeMaskLayerList
```

`selectParameterEntry` 自己处理 Variant 类型：integer 使用 unsigned 范围检查并返回 Player
parameter vector 内的借用指针，其他类型返回 null。`transformOrder` 固定读取四个数字索引；
holder 没有额外 Type guard。所有 Variant owner 都按字段独立 CopyRef，不能折叠成一个解码
树 owner。

type-4 分支四端还共同建立：单位 previous matrix、零 previous angle/timer、inactive emitter
flag；随后按顺序读取 particle、particleMaxNum、particleAccelRatio、particleInheritAngle、
particleInheritVelocity、particleFlyDirection、particleApplyZoomToVelocity、
particleDeleteOutsideScreen、particleTriVolume 和 particleMotionList，最后创建新的 TJS Array。
不同优化器对连续普通 store 做了明显调度，因此机器指令写序不应误当成唯一源语句顺序；
第一个可能回调的 particle getter 之前，全部默认字段已经写完。

## type-3 的 live preview 读取

读取 `layer.type` 和可选 `layer.stencilType` 都可能进入脚本 getter。进入 type-3 case 后，四端
才从 Player 对象现读 preview：

| 参考二进制 | preview 读取 | Player 布局偏移 | 后继行为 |
|---|---:|---:|---|
| Android ARM64 | `0x6B1788` | `+1092` | 非零时 `stencilType &= ~4` |
| Android ARMv7 | `0x58129E` | `+744` | 同上 |
| iOS ARM64 | `0x100108B5C` | `+980` | 同上 |
| iOS ARMv7 | `0x10630E` | `+680` | 同上 |

因此 preview 不是 tree-build entry 的快照。若前序 getter 回调把 Player.preview 从 false 改成
true，当前节点必须立即观察到 true，并在创建 type-3 child 之前清 stencil bit 4。

本地旧实现给 `detail::buildNodeTree`、recursive builder 和 node initializer 增加了四端都不
存在的 `parentPreview` 参数，并在整棵树开始前传入一次 `_preview`。这会让同一棵树内后续
节点、甚至当前节点的可重入 getter 看到过时状态。

## 源码修正

- `cpp/plugins/motionplayer/Player.h`、`NodeTree.h`、`NodeTree.cpp`
  - 删除合成的 `parentPreview` 参数；
  - type-3 case 在所有前序字段回调结束后调用 `player.getPreview()`；
  - 递归层不再传播 preview 快照。
- `cpp/plugins/motionplayer/PlayerMotionLoad.cpp`
  - tree builder 改回二参数调用；
  - 相关诊断标签从旧单二进制地址改为语义标签。
- `tests/unit-tests/plugins/motionplayer-dll.cpp`
  - 新增可重入 layer dispatch：读取 `type` 时把 Player.preview 改为 true；
  - 初始 `stencilType=4` 的 type-3 节点建完后必须清 bit 4。旧快照实现会保留该 bit。

测试还提供最小 `requireLayerId` ResourceManager dispatch、固定四项 transformOrder 和空
children Array；没有伪造 PSB 文件，也没有绕开真实 TJS property/Array 调用边界。

## IDB 改进

四个入口均已统一为三参数
`void Player_initNodeFields_guess(void *player, void *node, const void *layerVariant)`。函数注释
补入完整字段顺序和 live-preview 结论，四个 preview load 指令分别补入“前序 getter 可重入
改值、true 清 stencil bit 4”的行注释。四端 force recompile 后伪代码均显示该注释位于
Player byte load，随后保存成功。

## 验证

- Web Debug 与 Wasmtime Debug 的 `motionplayer` 静态库均编译成功，增量复跑均为
  `ninja: no work to do.`；
- Web Debug 完整页面链接成功；
- Wasmtime Debug `krkr2_wasmtime_guest` 完整 wasm 链接及转换成功；
- 两个完整目标的增量复跑均为 `ninja: no work to do.`；
- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` 使用 Web Debug 的真实 Emscripten 参数
  执行 `-fsyntax-only` 通过，仅有仓库既有 `_tss` 弃用警告；
- 当前没有可直接运行该 Catch 翻译单元的 native 测试目标，因此不把语法检查描述成运行时
  测试执行。
