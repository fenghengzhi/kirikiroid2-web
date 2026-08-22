# MotionPlayer raw-label 递归、共享 child visitor 与 `zFactor` 四端对照（2026-08-12）

## 1. 结论

本轮对 `reference/binaries/` 中四个当前参考二进制重新定位并 fresh-decompile 后，确认
`Player` 至少有五条看似独立的递归功能共用同一个 child-player visitor：

1. raw-label 递归查找；
2. `zFactor` 递归传播；
3. processed-mesh-vertex 递归求和；
4. `contains` 递归命中；
5. variable-range 递归折叠。

这个共享 visitor 的真实边界与本地旧实现有三处重要差异：

- flat node deque 的结束状态在每轮条件判断时重新读取，不是 range-for 开始时固定的
  end 快照；
- type-4 particle node 在整个 count 读取和子回调循环期间只持有一份 Array dispatch；
- 四个参考二进制都有同一个已发布行为：循环次数来自 Array count，但每次都重新读取
  `Array[0]`，从不读取循环下标。因此第 0 个 particle child 被访问 `count` 次，后续 child
  永远不会被这个共享 visitor 访问。

此外，原本地内联 `setZFactor` 只写 `_zFactor`，漏掉了原版的相等早退、root dirty 和
递归传播。四端共同顺序是：比较 -> 写父 Player 的 zFactor -> 标记 root dirty -> 访问
child Player。该 setter 没有空 deque 或空 child 保护。

## 2. 四端函数映射

表中每格为“函数入口 / IDA 函数大小”。

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| `Player_findNodeByRawLabel_guess` | `0x6B2EB8` / `0x144` | `0x58220C` / `0xB4` | `0x100109EEC` / `0x118` | `0x10777C` / `0x120` |
| `Player_visitChildPlayerDispatches_guess` | `0x6B33FC` / `0x278` | `0x5824E4` / `0x142` | `0x10010A13C` / `0x220` | `0x107A20` / `0x206` |
| `Player_findNodeByRawLabelVisitorInvoke_guess` | `0x6EF6EC` / `0x48` | `0x5AD280` / `0x26` | `0x100141B54` / `0x44` | `0x142B6A` / `0x26` |
| `Player_setZFactor_guess` | `0x6B1D6C` / `0xE0` | `0x5817B4` / `0x7C` | `0x100109198` / `0xD0` | `0x1069C4` / `0x108` |
| `Player_setZFactorVisitorInvoke_guess` | `0x6F0F60` / `0x24` | `0x5AE580` / `0x18` | `0x100143550` / `0x24` | `0x14441C` / `0x18` |

四个 `zFactor` setter 和四个 invoke thunk 已写入以下归一化 IDA prototype：

```text
void Player_setZFactor_guess(void *self, double value)
bool Player_setZFactorVisitorInvoke_guess(
    void *capture, void *childPlayerArg)
```

第二个机器级参数是 `std::function<bool(Player *)>` invoke ABI 的参数槽地址；64-bit iOS
fresh decompile 因此显示 `*(void **)childPlayerArg`，并不表示源级回调参数是 `Player **`。

## 3. raw-label 查找的精确数据流

四端共同伪代码为：

```text
findNodeByRawLabel(player, label, recursive):
  it = player.labelToFlatNodeIndex.find(label)
  if it != end:
    return &player.flatNodeDeque[it->second]

  if !recursive:
    return null

  found = null
  visitChildPlayers(player, child -> {
    found = child.findNodeByRawLabel(label, recursive)
    return found == null
  })
  return found
```

可观察边界如下：

- 当前 Player 的 label map 永远优先于 child；本层命中时完全不递归。
- map 中保存的 flat-node index 直接用于 deque 索引，没有额外 bounds 检查；损坏的映射
  不会被静默转成 miss。
- `recursive == false` 时只执行本地 map 查询。
- 递归时按共享 visitor 的 flat-node 顺序深度优先搜索；第一个 child 命中会令回调返回
  false，从而终止整个当前层 walk。
- 回调按引用捕获同一个 `found`、label 和 recursive flag；递归调用继续传入原始 flag，
  不是在进入 child 后强制改成 false。
- type-4 的 Array[0] 重复行为也作用于 raw-label 查找。若第 0 个 child 未命中，它会被
  重新递归 `count` 次；即使后续 particle child 有匹配 label，也永远不可达。

## 4. 共享 child visitor 的容器和生命周期

### 4.1 flat node deque

四端 iterator arithmetic 显示的 `MotionNode` ABI 大小为：

| 目标 | `sizeof(MotionNode)` |
| --- | ---: |
| Android ARM64 | `0xA48`（2632） |
| Android ARMv7 | `0x8E0`（2272） |
| iOS ARM64 | `0xA58`（2648） |
| iOS ARMv7 | `0x8B4`（2228） |

共同外层循环等价于：

```cpp
for(auto it = nodes.begin(); it != nodes.end(); ++it) {
    // dispatch current node
}
```

这里关键的不是语法，而是每轮条件都读取 live deque end。四端对应的 end 重读点为：

| Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| ---: | ---: | ---: | ---: |
| `0x6B34B8` | `0x5825F2` | `0x10010A2FC` | `0x107AB6` |

因此不能改成在进入函数时保存 size/end 的遍历。回调导致的 live 容器状态变化会参与后续
条件判断；原函数也没有为这种变化增加锁、版本号或异常转换。

### 4.2 type 3：单 child Variant

type 3 从节点保存的 child-player Variant 取得 NCB native `Player *`，然后立即调用 visitor：

- native-instance 转换错误直接向外传播；
- visitor 不过滤 null native pointer；
- 回调返回 false 时立即停止整个 flat-node walk；
- visitor 自身不额外持有 child Player；其可用期来自节点内 Variant/adaptor 所有权。

这意味着某些 caller（例如 `setZFactor`、processed count、`contains`）会直接解引用 null
child，不能在本地 visitor 中统一补空指针保护。

### 4.3 type 4：一次 Array retain，重复 element zero

type 4 的共同伪代码为：

```text
array = retain/convert(node.particleArrayVariant)
count = array.count
if count >= 1:
  for i = 0 .. count-1:
    element = array[0]                 // 注意：是字面量 0，不是 i
    child = convertNCBPlayer(element)
    if visitor(child) == false:
      release array
      return
release array
```

Array dispatch 的一份 retain 横跨 count getter、全部 element getter 和全部 callback；不会
在每轮循环重新 retain Array。每一轮仍重新读取 `Array[0]` 并重新转换 element Variant，
所以 callback 若替换第 0 项，下一轮能观察新对象。count 只读一次，之后修改数组长度不会
改变本轮迭代次数。

四端传给 numeric element helper 的字面量零位于：

| Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| ---: | ---: | ---: | ---: |
| `0x6B35B8` | `0x582590` | `0x10010A264` | `0x107B86` |

Array 转换、count getter、element getter 和 NCB native-instance 转换的错误都向外传播。
即便 callback 返回 false 或发生异常，已经持有的 Array dispatch 仍按 C++ 清理路径释放。

## 5. `zFactor` setter

### 5.1 共同伪代码与顺序

```text
setZFactor(player, value):
  if player.zFactor == value:
    return

  player.zFactor = value
  player.flatNodeDeque[0].delta.dirty = true
  visitChildPlayers(player, child -> {
    child.setZFactor(value)
    return true
  })
```

四端 Player 字段和 root dirty 字节的 ABI offset 不同：

| 目标 | `Player::zFactor` | root `delta.dirty` |
| --- | ---: | ---: |
| Android ARM64 | `Player + 1112` | `root + 1584` |
| Android ARMv7 | `Player + 768` | `root + 1344` |
| iOS ARM64 | `Player + 1000` | `root + 1600` |
| iOS ARMv7 | `Player + 700` | `root + 1312` |

setter 直接通过 deque element zero 寻址 root，不检查 deque 是否为空。正常构造函数始终
创建 root；若对象状态已损坏为空 deque，则这里保留原版非法访问边界。

写入顺序不能交换：parent zFactor 已先更新，root dirty 其次，最后才进入可能抛错的 child
转换/Array getter/child setter。因而递归阶段抛异常时，父对象的两个状态变化仍然保留。

### 5.2 浮点边界

四端都执行普通 ordered floating equality：

- 相同有限值立即返回，root 不 dirty，也完全不遍历 child；
- `+0.0 == -0.0`，所以只改变零的符号位会被吞掉，保留旧符号位；
- `NaN != NaN`，同一个 NaN 值重复设置仍每次 dirty 并递归；
- 没有 epsilon、有限性检查或 canonicalization。

### 5.3 递归与 particle bug 的组合效果

传播回调按值捕获 double，调用 child setter 后恒返回 true，因此这个 caller 自己不会请求
visitor 早停。它仍继承共享 visitor 的全部边界：

- type-3 null child 会被直接解引用；
- type-4 只有 Array[0] 收到值，后续 particle child 保持旧值；
- Array[0] 在 count 大于一时被重复调用。普通数值第一次写入后，后续调用因 child 已相等
  而早退；NaN 则每次重复调用都会继续 dirty 和向下递归。

`zFactor` UTF-16LE 宽字符串和注册代码证据为：

| 目标 | 宽字符串 | 注册引用 |
| --- | ---: | ---: |
| Android ARM64 | `0x14D64D4` | `0x6D4D00` |
| Android ARMv7 | `0xD85DE2` | `0x598272`、`0x598280` 附近 |
| iOS ARM64 | `0x10195CBF8` | `0x100124A50` |
| iOS ARMv7 | `0x174EF5C` | `0x123D42`、`0x123D48`、`0x123D5C` 附近 |

普通字符串搜索在四端均为空；按 UTF-16LE bytes 搜索后才定位到这些 TJS 宽字面量。

## 6. 五个 caller 的差异

Android ARM64 对共享 visitor 的五个直接 call site 是 `0x6B1DDC`、`0x6B2F74`、
`0x6CE450`、`0x6D0854`、`0x6D3C90`；其余三端交叉引用得到相同五类语义 caller。

| caller | 回调返回 false 的条件 | 聚合/副作用 |
| --- | --- | --- |
| `setZFactor` | 从不 | 先写 parent/root，再传播同一 double |
| raw-label lookup | 当前 child 命中 | 保存首个 `MotionNode *` |
| processed mesh vertices | 从不 | `uint32` wraparound 累加 |
| `contains` | 当前 child 命中 | 保存 true，停止整棵当前 walk |
| variable-range fold | 从不 | 折叠 child 的 min/max |

因为所有 caller 都共享同一分派层，type-4 element-zero 行为并非只影响 layer query：它也
影响 zFactor、mesh count、contains 和 variable-range 的递归覆盖范围。

## 7. 本地同步

源码同步包括：

- `Player::visitChildPlayerDispatches_guess` 改成每轮重读 `_nodes.end()`；
- type-4 分支用一个 scoped Array dispatch 覆盖 count 和完整循环；
- element getter 按四端原样传字面量 `0`，而不是局部循环变量；
- `Player::setZFactor` 从错误的 inline scalar store 改成相等早退、root dirty、递归传播的
  out-of-line 实现；
- 没有增加空 deque、空 child、错误吞并或数组 index 修复；
- 移除相关路径里仍指向旧 `libkrkr2.so` 地址的 `setZFactor` 源码注释。

单元覆盖新增：

- 两个 particle child 时，recursive layer lookup 能命中 child 0，却不能命中 child 1；
- 同一个节点下 `zFactor` 只传播给 child 0，parent/child 0 root dirty，child 1 保持默认值；
- 设置相同值完全无副作用；
- `+0 -> -0` 被相等判断吞掉并保留正零；
- 重复设置 NaN 每次都重新标记 dirty。

## 8. IDB 同步

四份 IDB 均已完成并保存：

- raw-label lookup、共享 visitor 和 lookup invoke thunk 已统一命名、注释并重编译；
- literal-zero element call 和 live deque-end 读取点均增加行注释；
- `Player_setZFactor_guess` 与 `Player_setZFactorVisitorInvoke_guess` 已统一命名和 prototype；
- zFactor root-dirty store、共享 visitor call 和浮点边界已增加函数/行注释；
- 修改后再次强制 fresh-decompile，确认四端函数名、prototype、回调链与伪代码一致。

## 9. 验证

本轮完成后执行：

- 用 Web `PlayerCore.cpp` 的真实 compile command 对完整
  `tests/unit-tests/plugins/motionplayer-dll.cpp` 做 `-fsyntax-only`：成功；唯一输出为仓库既有
  `_tss` literal-operator deprecated warning；
- `cmake --build out/web/debug --target motionplayer --parallel`：成功；
- `cmake --build out/wasmtime/debug --target motionplayer --parallel`：成功；
- `cmake --build --preset "Web Debug Build" --parallel`：成功生成/链接 `index.html` 与 Wasm；
- `cmake --build out/wasmtime/debug --target krkr2_wasmtime_guest --parallel`：成功链接 guest
  Wasm 并完成 exnref exception 转换；
- 上述四个 build target 立即各重跑一次：全部 `ninja: no work to do.`；
- `git diff --check`：退出码 0；只有工作区既有 LF/CRLF 提示，无 whitespace error。
