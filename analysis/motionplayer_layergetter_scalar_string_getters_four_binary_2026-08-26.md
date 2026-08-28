# LayerGetter scalar/string getters（四参考二进制，2026-08-26）

## 1. 范围与结论

本纵切面闭合 LayerGetter 的 17 个不同 scalar callback 和两个 string callback：
`type`、`label`、`src`、三种 visibility、`x/left`、`y/top`、两种 flip、两种 zoom、
两种 angle、两种 slant、两种 origin 和 `opacity`。`coord/mtx/vtx/color/bezierPatch/
shape/motion/particle` 的 Variant/container owner 流另行闭合。

2026-08-27 的完整 MotionNode ctor/copy/dtor 审计纠正了本报告对三种 visibility 的
旧字段归属；详见 `motionplayer_motionnode_source_order_four_binary_2026-08-27.md`。
2026-08-27 的最终 source-order 闭包又把 `layerName` 恢复为首个数据 owner、把
`SourceState -> ClipSlot[2] -> active selector -> dormant std::string` 恢复为同一连续声明关系，
因此 `label/src` 不再受结构账本缺口阻塞。除这些纠正外，四端共同语义与本地
`PlayerLayerQuery.cpp` 一致。所有 getter 都先直接解引用 facade
中唯一的 raw node pointer，没有 null、owner、generation 或 Player-liveness guard。
因此默认脚本构造留下的 null node 和 Player/node-tree 生命周期结束后的悬空指针，
在属性读取处都不会被转换成 Void 或错误码。

## 2. 四端 callback 映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `type` | `0x6993F4` | `0x5749DE` | `0x1000F86AC` | `0xF5408` |
| `label` | `0x699400` | `0x5749E4` | `0x1000F86B8` | `0xF540E` |
| `src` | `0x699424` | `0x574A04` | `0x1000F86DC` | `0xF542E` |
| `visible` | `0x69945C` | `0x574A32` | `0x1000F8710` | `0xF545C` |
| `branchVisible` | `0x699468` | `0x574A3A` | `0x1000F871C` | `0xF5464` |
| `layerVisible` | `0x699474` | `0x574A42` | `0x1000F8728` | `0xF546C` |
| `x` / `left` | `0x699498` | `0x574A5A` | `0x1000F874C` | `0xF5484` |
| `y` / `top` | `0x6994A4` | `0x574A6A` | `0x1000F8758` | `0xF5494` |
| `flipX` | `0x69961C` | `0x574AF4` | `0x1000F87EC` | `0xF5588` |
| `flipY` | `0x699628` | `0x574AFC` | `0x1000F87F8` | `0xF5590` |
| `zoomX` | `0x699634` | `0x574B04` | `0x1000F8804` | `0xF5598` |
| `zoomY` | `0x699640` | `0x574B14` | `0x1000F8810` | `0xF55A8` |
| `angleDeg` | `0x69964C` | `0x574B24` | `0x1000F881C` | `0xF55B8` |
| `angleRad` | `0x699658` | `0x574B38` | `0x1000F8828` | `0xF55C8` |
| `slantX` | `0x699680` | `0x574B70` | `0x1000F8850` | `0xF55FC` |
| `slantY` | `0x69968C` | `0x574B80` | `0x1000F885C` | `0xF560C` |
| `originX` | `0x699698` | `0x574B90` | `0x1000F8868` | `0xF561C` |
| `originY` | `0x6996B4` | `0x574BA8` | `0x1000F8880` | `0xF5634` |
| `opacity` | `0x6996D0` | `0x574BC0` | `0x1000F8898` | `0xF564C` |

表中 19 行、76 个目标均在本轮 fresh decompile；`angleRad` 另外 fresh disasm，
所有 scalar callback 已补正确 `int/bool/double` 函数签名并保存四库。

## 3. 共同源码伪代码

```text
getType:          return node.nodeType
getLabel:         return ttstr CopyRef(node.label)
getSrc:           return ttstr CopyRef(node.slots[node.activeSlotIndex].src)

getVisible:       return node.layerGetterVisible
getBranchVisible: return node.layerGetterBranchVisible
getLayerVisible:  return node.layerGetterVisible &&
                         node.layerGetterBranchVisible

getX/getLeft:     return node.accumulated.x
getY/getTop:      return node.accumulated.y
getFlipX/Y:       return node.accumulated.flipX/flipY
getZoomX/Y:       return node.accumulated.scaleX/scaleY
getAngleDeg:      return node.accumulated.angle

tmp = node.accumulated.angle * binary64(0x400921fb54442c00)
getAngleRad:      return (tmp + tmp) / binary64(0x4076800000000000)

getSlantX/Y:      return node.accumulated.slantX/slantY
getOriginX/Y:     return node.slots[node.activeSlotIndex].ox/oy
getOpacity:       return node.accumulated.opacity
```

`layerVisible` 保持左到右短路：visible 为零时不再读取 active。虽然正常 live node
的两个字节都有效，这一读取顺序仍是原始边界的一部分。

## 4. `angleRad` 精确浮点次序

四端指令序列均为 `FMUL/VMUL`、同一结果寄存器自加 `FADD/VADD`、最后
`FDIV/VDIV`。两个常量在四端的 bit pattern 完全相同：

- pi：`0x400921fb54442c00`；
- 360：`0x4076800000000000`。

因此不能代数化为 `angle * pi / 180`，也不能先把 `2/360` 合并。原始顺序先舍入
`angle*pi`，再舍入加倍，而且加倍可能在等价除 180 表达式仍有限时先溢出。本地
显式 `angleTimesPi` 临时和 `angleTimesPi + angleTimesPi` 正是为保存该顺序。

## 5. string CopyRef 所有权

`label` 和 `src` 都不构造新的字符缓冲区，也不访问 TJS property：

1. 读取 live node 中的内部 `ttstr` 共享缓冲指针；
2. 把该指针写入隐藏返回对象；
3. 非 null 时原子递增缓冲区引用计数；
4. 返回的 `ttstr` 成为独立 owner，稍后析构时自行减引用。

64 位用 `LDAXR/STLXR` 循环，32 位用 `DMB + LDREX/STREX + DMB`。`src` 在复制前
每次重新读取 active-slot selector；它没有缓存 slot、没有回退到 icon/label，也
不读取 inactive slot。空内部字符串保持 null shared pointer，不分配空缓冲区。

## 6. ABI 字段坐标与结构证据

以下偏移只用于恢复 source relation，不写入 portable C++ padding：

| 逻辑字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| node label owner | `+0` | `+0` | `+0` | `+0` |
| node type | `+28` | `+20` | `+28` | `+20` |
| active-slot index | `+1392` | `+1160` | `+1392` | `+1128` |
| clip-slot stride | `536` | `432` | `536` | `420` |
| slot src owner | `+356` | `+328` | `+356` | `+316` |
| slot origin x/y | `+376/+384` | `+344/+352` | `+376/+384` | `+328/+336` |
| independent getter visible/branch | `+1496/+1497` | `+1256/+1257` | `+1512/+1513` | `+1228/+1229` |
| accumulated flip x/y | `+1507/+1508` | `+1267/+1268` | `+1523/+1524` | `+1235/+1236` |
| accumulated x/y | `+1512/+1520` | `+1272/+1280` | `+1528/+1536` | `+1240/+1248` |
| accumulated angle | `+1536` | `+1296` | `+1552` | `+1264` |
| accumulated scale x/y | `+1544/+1552` | `+1304/+1312` | `+1560/+1568` | `+1272/+1280` |
| accumulated slant x/y | `+1560/+1568` | `+1320/+1328` | `+1576/+1584` | `+1288/+1296` |
| accumulated opacity | `+1576` | `+1336` | `+1592` | `+1304` |

两个 64 位目标的 slot stride 相同，但 accumulated 起点相差 16；两个 32 位目标的
slot stride、selector 和 accumulated 坐标也不同。这些差异来自 STL/ABI/声明布局，
不是条件源码分支。

特别重要的是，四端 label owner 都位于 node `+0`。最终结构闭包没有为满足某个单端
偏移添加 padding，而是先联合默认构造、默认值尾、编译器生成 copy assignment 与析构器，
再把 `layerName` 恢复为 portable `MotionNode` 的首个数据成员；`src` 的两个 slot owner 与
active selector 也位于同一个恢复后的 owner 序列。ABI 坐标仍只留在本报告中。

## 7. 本地逐行对照

`cpp/plugins/motionplayer/PlayerLayerQuery.cpp` 当前：

- `getType` 的字段一致；visibility 三项现已按 C10 纠正为累计块之前的两枚独立字节，
  `layerVisible` 仍保持相同左到右短路；
- `getX/getLeft`、`getY/getTop` 分别返回同一字段，允许编译器 ICF 合并；
- flip、zoom、angle、slant、opacity 直接返回对应 accumulated 字段；
- `getOriginX/Y` 每次调用 `activeSlot()` 后读取 `ox/oy`；
- `getLabel/getSrc` 按值返回 `ttstr`，产生同一 CopyRef owner；
- `getAngleRad` 显式保存原始三步浮点次序。

getter body 本身没有语义偏差。最终结构闭包修改的是它们所读取对象的声明/生命周期：
`MotionNode.h` 已恢复首部 `layerName`、双 slot owner、active selector 和独立 dormant
`std::string` 的共同顺序，因而 `getLabel/getSrc` 的按值 `ttstr` 返回现在同时满足 body 与
owner 结构等价。

## 8. 验证与剩余项

最终闭包再次 fresh decompile 两个 string getter 的四端 8 个函数，并完整读取
`9/14`、`12/15`、`9/13`、`12/15` 条指令，共 99 条，均未截断；四库已补充 owner
语义注释、bookmark 并保存。当前机器缺 CMake/Emscripten，正式 unit/Web build 仍不可运行，
该限制不能冒充为已通过构建。
