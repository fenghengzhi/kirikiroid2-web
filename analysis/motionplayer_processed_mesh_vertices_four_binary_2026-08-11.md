# MotionPlayer `processedMeshVerticesNum` 四参考复原（2026-08-11）

## 结论

旧单体注释把 Player 中一个每帧清零的 DWORD 留作“用途未知”，本地随后又并存了
两个互不相干的模型：部分 `_processedMeshVerticesNum` 累加，以及无 caller 的
`_alphaOpCounter/alphaOpAdd()`。四份当前参考二进制共同证明，真实字段就是
只读属性 `processedMeshVerticesNum` 的本体计数，语义是：

```text
progress entry:
    localProcessedMeshVertices = 0

vertex-computation phase:
    add every charged mesh-point transformation performed by this Player

property getter:
    result = localProcessedMeshVertices
    visit every type-4 particle child and type-3 Motion child in node order:
        result += child.processedMeshVerticesNum   // recursive
    return result
```

本体字段、每次累加和递归和均为 `uint32`；溢出按模 `2^32` 回绕。它不是渲染
alpha 操作序号，也不是 Player 生命周期累计统计。

## 四端入口与布局

| 目标 | Player ctor | frameProgress | vertex phase | getter | Player 字段 | speed 字段 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Android ARM64 | `0x6CC110` | `0x6BE44C` | `0x6B98D0` | `0x6CE3F8` | `+1152` | `+1168` |
| Android ARMv7 | `0x5935C4` | `0x58A63A` | `0x5866F8` | `0x594710` | `+808` | `+824` |
| iOS ARM64 | `0x10011EC04` | `0x100113B50` | `0x10010F6AC` | `0x10011FDA8` | `+1040` | `+1056` |
| iOS ARMv7 | `0x11D488` | `0x111556` | `0x10CE30` | `0x11EA6C` | `+740` | `+756` |

四端字段都恰好位于 speed 前 16 字节。构造初始化证据分别为：

- Android ARM64 `0x6CC554` 的 `+1148` 八字节清零覆盖 `+1152`；
- Android ARMv7 `0x59389A` 直接清零 `+808`；
- iOS ARM64 `0x10011EEEC` 的 `+1036` 八字节清零覆盖 `+1040`；
- iOS ARMv7 `0x11D95C` 直接清零 `+740`。

progress 入口分别在 `0x6BE468 / 0x58A65C / 0x100113B60 / 0x111562`
写零。清零早于后续 timeline refresh、reseek 和其它可能早退的工作；所以一次
progress 即使很早返回，属性本体也已经是零。反过来，单独重复调用
`updateLayers` 而不进入 progress 不会自动清零，计数会继续累加。

## vertex phase 的四类收费点

四端大型 helper 虽因 STL ABI、node 步长和编译器控制流布局不同，收费点完全
一致：

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| shape/camera 原点经过一个 mesh 祖先 | `0x6B9C88` | `0x586A0C` | `0x10010F9F8` | `0x10DCE0` |
| 无 mesh 祖先的 type-1 自身网格 | `0x6BA494` | `0x586F92` | `0x1001100A0` | `0x10D4C8` |
| non-combine 祖先：整张 composite 网格加原点 | `0x6BA658` | `0x5870D8` | `0x1001101DC` | `0x10D634` |
| 剩余祖先链：只变换原点 | `0x6BA6F4` | `0x587180` | `0x10011026C` | `0x10D6E4` |

### 1. shape/camera 原点

node type 位测试为 `0x22`，即 type 1 和 type 5。沿 `meshAncestor` 单链前进；
每遇到 `hasMeshData` 的祖先，先把当前位置逆映射到其 patch 参数，再做 4x4
Bezier patch 求值，成功执行这次求值后计数 `+1`。二进制不以 vector size
增加额外保护；`hasMeshData` 就是可求值契约。

### 2. 顶层 type-1 网格

当当前 node 没有 mesh 祖先、`meshType == 1`，原版仍计入稍后渲染会处理的
tessellation 网格，即使这一分支不在 vertex helper 内物化 composite vector：

```text
division = uint32(root.meshDivisionRatio * uint32(node.meshDivision))
division = min(division, 50)
width  = uint32(source.width)
height = uint32(source.height)
splitX = denominator != 0 ? division * width / (width + height) : 0
count += (division - splitX + 2) * (splitX + 2)
```

`+2` 来自 node 中存储的 cell 数再转为 point 数：
`meshDivX = splitX + 1`、`meshDivY = division - splitX + 1`，因此 point grid 是
`(meshDivX+1)*(meshDivY+1)`。原生 AArch64 的零除数 `UDIV` 结果为零；本地继续
用显式 denominator 分支避免 wasm 整数除零 trap。

### 3. composite 网格经过 non-combine 祖先

当前 node 有 mesh 祖先时，helper 先生成并经自身 patch 映射 composite point
vector。随后，对每个仍处于 non-combine 段且 `hasMeshData` 的祖先：

```text
transform every composite point through ancestor patch
transform current origin through the same patch
count += compositePointCount + 1
```

其中 `+1` 精确对应单独的 origin 求值，不是哨兵元素或 vector off-by-one。

### 4. 剩余祖先链

到达 combine 边界后，剩余链不再逐点改写 composite vector，只继续映射 origin；
每个 `hasMeshData` 祖先计数 `+1`。最终 origin 与初值不同才把差值平移回整个
composite vector；这个平移循环不再增加计数。

## 递归 getter 与访问顺序

| 目标 | getter | child visitor callback | shared child walk | EmotePlayer facade |
| --- | ---: | ---: | ---: | ---: |
| Android ARM64 | `0x6CE3F8` | `0x6F29B4` | `0x6B33FC` | `0x67F294` |
| Android ARMv7 | `0x594710` | `0x5AFA64` | `0x5824E4` | `0x56204E` |
| iOS ARM64 | `0x10011FDA8` | `0x1001451D8` | `0x10010A13C` | `0x1001B6114` |
| iOS ARMv7 | `0x11EA6C` | `0x145AE6` | `0x107A20` | `0x1B5ED2` |

Android getter 构造一个 heap-backed `std::function` capture，iOS 使用 libc++ 的
small-object function wrapper；这是标准库布局差异，不是算法差异。四端 callback
都执行：

```text
*capturedAccumulator += child->getProcessedMeshVerticesNum()
return true
```

所以遍历永不中途停止，并递归覆盖任意深度。共享 child walk 按 node 顺序处理：

1. type 4：按 TJS Array 的索引顺序遍历全部 particle child；
2. type 3：访问唯一 Motion child；
3. 其它 node：跳过。

getter 不清零、不缓存，也不修改 child。child 的字段值取决于各 child 最近一次
progress/update 的时序；递归相加仍按 uint32 回绕。参考调用链没有在 callback
前增加 null 过滤，本地共享 visitor 也保留该契约。

## NCB 表面

UTF-16LE `processedMeshVerticesNum` 在四个 Player registrar 中都安装上述 getter，
没有 setter：

| 目标 | Player registrar | 属性安装点/字符串引用 |
| --- | ---: | ---: |
| Android ARM64 | `0x6D3DA8` | getter `0x6D5C10`，字符串 `0x6D5C30` |
| Android ARMv7 | `0x597EC8` | getter pointer `0x598646`，字符串序列 `0x598640..0x59864C` |
| iOS ARM64 | `0x1001244F8` | 字符串 `0x100125010`，getter `0x100125018` |
| iOS ARMv7 | `0x123848` | 字符串序列 `0x124270..0x124282`，getter `0x12427A` |

同名属性也由 EmotePlayer facade 暴露，facade 只取出内部 Player 并调用同一个递归
getter。D3DEmotePlayer 的当前 registrar 不含该属性；本地未注册的 D3D facade
setter/getter 残留因此一并删除。

## 本地差异与修正

修改前：

- `_processedMeshVerticesNum` 为有符号 `int`，没有 progress-entry 清零；
- getter 只返回本体，不递归 child/particle Player；
- ancestor composite 两类计数已经存在，但漏掉 shape/camera 原点和顶层网格；
- 暴露了没有参考 setter 的 C++ setter，并在 D3DEmotePlayer 留有未注册委托；
- `_alphaOpCounter/Player::alphaOpAdd()` 无内部 caller、无 Player NCB 成员，属于旧
  端口遗留；D3DAdaptor 的真实 `alphaOpAdd` bool 属性与它无关。

本轮修正：

- 字段和 getter 改为 `std::uint32_t`，在 `frameProgress` 最前端清零；
- getter 复用已四端确认的 child visitor，并对每个 child 递归求和；
- 补齐两个漏掉的收费点，已有 composite 累加也改为显式 uint32 算术；
- 删除 Player setter、未注册 D3DEmotePlayer 委托和死 `_alphaOpCounter/alphaOpAdd`；
- Player/EmotePlayer NCB 保持只读属性；当前地址只保留在本文，源码注释描述共同
  语义。

## IDB 改进与验证

四库已统一写入并 fresh-decompile 验证以下名字：

- `Player_getProcessedMeshVerticesNum_guess`
- `Player_updateLayersVertexComputation_guess`
- `Player_addChildProcessedMeshVertices_guess`
- `EmotePlayer_getProcessedMeshVerticesNum_guess`

Android ARMv7、iOS ARM64、iOS ARMv7 中原先只被识别成单字符 `p` 的 Player
UTF-16 常量已重命名为 `aProcessedMeshVerticesNum_utf16_guess`。getter 与 vertex
helper 的函数类型、构造/清零/四类累加/registrar/facade 注释也已写入四库。

验证结果：

- Web Debug 与 Wasmtime Debug 的 `motionplayer` 目标均成功完成增量编译和静态库
  链接；只出现仓库既有的 `_tss` literal-operator deprecation warning。
- 随后两套 Ninja dry-run 都报告 `no work to do`。
- `git diff --check` 通过；输出只有仓库既有的 LF/CRLF 转换提示。
- 四个修改后的 IDB 均已成功保存。
