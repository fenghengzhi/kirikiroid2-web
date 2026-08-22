# MotionPlayer visibility / visible-ancestor 四端对照（2026-08-12）

## 结论

本专题以 `reference/binaries/` 中 Android arm64、Android armv7、iOS
arm64、iOS armv7 四个当前参考二进制为共同真值，重新核验 `updateLayers`
后处理链中的 visibility flag 与 visible-ancestor 构建阶段。旧
`libkrkr2.so` 地址注释不再作为证据。

四端控制流相同：helper 只遍历非根节点；每个节点先从 parent 当前帧的
`drawFlag`/ancestor 建链，再按 slot done、一个非零节点字段、accumulated active、
force-visible、preview 类型掩码和 source valid 计算自己的 `drawFlag`。

本轮证伪两个本地“安全化”行为：

- helper 不会重写 root `drawFlag`。root 的 draw flag 与 nullable ancestor 在
  MotionNode 构造时清零，visibility phase 原样保留。
- parent index 没有范围 guard。四端都直接索引 deque；portable 实现不能在负值
  或越界值上安静保留旧 ancestor。

布尔 gate 的反直觉分支得到保留：只要前三个基础条件通过，而 force-visible 为
零且 node type 不在当前 mask 内，`drawFlag` 直接为 true，即使 source invalid。

## 函数边界与调用链

| 目标 | `Player_updateVisibility_guess` | 大小 | `Player_updateLayers_guess` |
|---|---:|---:|---:|
| Android arm64 | `0x6BACBC` | `0x14C` | `0x6B871C` (`0xAE4`) |
| Android armv7 | `0x58762C` | `0x116` | `0x5856E0` (`0x9DC`) |
| iOS arm64 | `0x1001107BC` | `0x108` | `0x10010E544` (`0xB50`) |
| iOS armv7 | `0x10DF88` | `0xC0` | `0x10BE5C` (`0xA76`) |

旧本地注释把 Android arm64 visibility 标成 `0x6BD8DC`。该地址属于后面的
type-10 feedback-anchor helper 内部，不是 visibility 函数入口。真实共同顺序是：

```text
Player_updateLayersVertexComputation_guess
Player_updateVisibility_guess
Player_updateCameraNode_guess
shape-AABB helper
```

visibility 没有 preview early-return；preview 只改变类型掩码。

## ABI 对照

这些偏移仅供反编译复核，不应进入 portable C++ 源码注释。

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| MotionNode 步长 | `2632` | `2272` | `2648` | `2228` |
| `nodeType` | `+28` | `+20` | `+28` | `+20` |
| parent index | `+36` | `+28` | `+36` | `+28` |
| 非零基础字段（本地 `stencilType`） | `+52` | `+44` | `+52` | `+44` |
| source valid | `+200` | `+184` | `+200` | `+184` |
| active slot index | `+1392` | `+1160` | `+1392` | `+1128` |
| active slot `done` | `node+344+536*i` | `node+320+432*i` | `node+344+536*i` | `node+308+420*i` |
| accumulated active | `+1505` | `+1265` | `+1521` | `+1233` |
| visible-ancestor pointer | `+1952` | `+1688` | `+1968` | `+1652` |
| draw flag | `+1960` | `+1692` | `+1976` | `+1656` |
| force-visible integer | `+1996` | `+1716` | `+2012` | `+1680` |
| Player preview | `+1092` | `+744` | `+980` | `+680` |

原生 ancestor 是 nullable `MotionNode *`；portable 结构用节点索引替换指针，
`-1` 表示 null。该替换不改变选择逻辑。

## 共同伪代码

```text
for nodeIndex in [1, nodes.size):
    node = nodes[nodeIndex]

    parentIndex = node.parentIndex
    parent = nodes[parentIndex]                // unchecked
    if parent.drawFlag:
        node.visibleAncestor = parent
    else:
        node.visibleAncestor = parent.visibleAncestor

    if node.activeSlot.done:
        node.drawFlag = false
    else if node.stencilType == 0:
        node.drawFlag = false
    else if !node.accumulated.active:
        node.drawFlag = false
    else:
        mask = player.preview ? 6153 : 6145
        if node.forceVisible != 0 or (mask & (1 << node.type)) != 0:
            node.drawFlag = node.source.valid
        else:
            node.drawFlag = true
```

掩码展开为：

```text
normal  6145 = 0x1801 -> types 0, 11, 12
preview 6153 = 0x1809 -> types 0, 3, 11, 12
```

本地保留原生形态的 signed `1 << nodeType`，没有添加 type 范围检查或改成安全
位集合 helper。异常 nodeType 的语言/目标边界不应在没有四端证据时被重新定义。

## ancestor 数据流

ancestor 写入发生在当前节点 `drawFlag` 重算之前。由于树构造的 deque 顺序让
parent 先于 child，child 读取的是 parent 在同一 visibility pass 已更新的值：

- parent 当前可见：child 的 visible ancestor 直接指向 parent。
- parent 当前不可见：child 跳过 parent，继承 parent 已解析的 ancestor。
- parent 是 root 且 root 保持构造零值：直接 child 继承 root 的 null ancestor。

原生不检查 parentIndex 是否为负或小于 deque size。portable 代码也直接用该值
索引；不能在非法输入上保留节点旧的 `visibleAncestorIndex`，因为那会制造参考实现
不存在的跨帧残留分支。

ancestor 与 draw flag 都是 MotionNode 值的一部分，节点复制/赋值会随节点状态
一起搬运；visibility 每帧只重写非根节点的这两项。root 不进入循环。

## root 构造与生命周期

四端构造路径共同清零 draw flag 与 ancestor：

| 目标 | 构造/共同初始化写入 |
|---|---|
| Android arm64 | `MotionNode_initCommonFields_guess`：draw `0x6967A8`，ancestor `0x6967B0` |
| Android armv7 | `MotionNode_initCommonFields_guess`：ancestor `0x572A58`，draw `0x572A5C` |
| iOS arm64 | `MotionNode_initCommonFields_guess`：draw `0x1000F65B4`，ancestor `0x1000F65B8` |
| iOS armv7 | `MotionNode_initCommonFields_guess`：draw `0xF31A6`，ancestor `0xF31AA` |

四端均由真实 ctor 进入 common-field helper；V232 已将 Android arm64 的旧 ctor
名称 `0x696770` 纠正为 `MotionNode_initCommonFields_guess`，完整构造体位于
`0x6EED94`。
因此本地旧代码“root 总是可见，所以每帧按 accumulated.visible/source.valid 重算”
没有二进制依据。visibility helper 对只有 root 的 deque 什么都不做。

## drawFlag 决策边界

前三个 gate 具有严格优先级，任意失败都写 false：

1. active slot `done` 必须为 false；
2. 本地称为 `stencilType` 的整数必须非零；
3. accumulated active 必须为 true。

通过后有两条路线：

- `forceVisible != 0` 或 node type 被 mask 选中：最终值等于 `source.valid`。
- 两者都否：最终值为 true，不读取 source-valid 作为 gate。

因此 `forceVisible` 并非“不顾 source 强制显示”；它恰好把节点送入
source-valid gate。preview 只让 type 3 新加入同一 gate，而不是直接让 type 3
可见。type 5 等普通未掩码节点只要前三项成立，就能在 source invalid 时保持
drawFlag true。这些名字造成的直觉歧义不能反过来修改控制流。

## 本地差异与修复

| 旧本地行为/注释 | 四端证据 | 修复 |
|---|---|---|
| visibility 标成 A64 `0x6BD8DC` | 真入口 `0x6BACBC`；旧地址位于另一函数 | 删除 compiled source 旧地址，本文记录纠错 |
| 每帧写 root `drawFlag = visible && source.valid` | helper 从 index 1 开始；构造清 root flag/ancestor | 删除 root 写入 |
| parentIndex 先做 `0 <= p < size` 检查 | 四端直接 deque 索引 | 删除 guard，保留原生边界 |
| 大段注释把旧单端临时变量当源结构 | 四端共同逻辑可压缩为两个选择 helper | 改成 portable 生命周期注释和显式 helper |

布尔 gate 的既有主体结果与四端相符，没有改变 mask 数值、force-visible 语义、
slot-done/active 优先级或 unmasked fallback。

## 测试与构建验证

确定性测试新增覆盖：

- parent 可见时选择 parent，不可见时继承 parent ancestor，root/null 的 `-1`
  表示；
- slot done、零基础字段和 inactive 三个 false gate；
- force-visible 与 mask-selected 分支都依赖 source valid；
- normal 下 type 3 未掩码且 source invalid 仍为 true；preview 下 type 3 进入
  source-valid gate而变 false；
- 普通 unmasked type 5 在 source invalid 时仍为 true。

验证结果：

- Web debug `index.html` 32 步增量构建和链接成功。
- Wasmtime guest 31 步增量构建、链接和 exnref 转换成功。
- 使用当前 Web Emscripten 参数对单元测试源做 syntax-only 检查成功，仅有项目
  既有 `_tss` literal-operator deprecation warning。
- 构建中的其他 warning 仍是既有 `_tss` 与 imagepacker `nodiscard` 诊断。
- Windows 原生 Catch 可执行文件仍因既有 vcpkg/cocos2dx 配置问题不可用。

## IDB 改进

四个 IDB 已统一命名并保存：

- `Player_updateVisibility_guess`；
- current node 与 resolved visible-ancestor 局部变量按四端对应关系命名；
- function start、unchecked parent/ancestor selection 和 drawFlag gate 添加行级
  注释；
- 四个 helper 及四个 `Player_updateLayers_guess` 均 force recompile/fresh
  decompile，主函数引用显示新名称；
- MotionNode ctor/common initializer 重新 decompile，确认 root 初值，不沿用旧
  源码推断。

四份数据库均通过 IDA 原生保存成功。
