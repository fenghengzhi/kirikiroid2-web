# `calcBounds` type-3 borrowed child 四参考恢复（2026-08-14）

## 1. 结论

`Player::calcBounds` 的非 preview type-3 路径在 active-slot done gate 之后执行。四端都直接
从 node 持久 `childPlayerVar` 借用 Object dispatch，查询 Player native instance 后得到 raw
`Player *`；没有 Variant CopyRef、没有 dispatch AddRef、没有 native Player owner，也没有
null guard。递归 child AABB pass 返回后，父函数继续使用同一 raw 指针读取四个 double，逐项
窄化为 float 写进 node AABB，再将这四个 float 提升为 double 合并进 parent AABB。

当前 `MotionNode::getChildPlayer()` 已具有相同的 no-addref/native-query 形状，本纵切面无需
修改 resolver；补充的是调用方生命周期、写入顺序、异常边界、测试与四份 IDB 语义。

## 2. 四端地址映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `Player_calcBoundsRecursive_guess` | `0x6C10E4` | `0x58BE38` | `0x100115C68` | `0x11354C` |
| active-slot done gate | `0x6C140C` | `0x58BFB0` | `0x100116154` | `0x113726` |
| non-preview/type-3 gate | `0x6C1414..0x6C1420` | `0x58BFB8..0x58BFC0` | `0x100115D14..0x100115D20` | `0x11372E..0x113734` |
| 原字段 Object/no-addref 转换 | `0x6C1428..0x6C1434` (`0x6C15C4` conversion slow path) | `0x58BFC6..0x58BFD0` (`0x58C0DC` slow path) | `0x100115D28..0x100115D38` (`0x100115E6C` slow path) | `0x11373A..0x113746` (`0x11384A` slow path) |
| Player native query / raw pointer | `0x6C1438..0x6C146C` | `0x58BFD4..0x58BFF8` | `0x100115D3C..0x100115D70` | `0x11374A..0x113778` |
| malformed query 的 null raw pointer | `0x6C15CC` | `0x58C0E4` | `0x100115E74` | `0x113852` |
| raw child 递归 `calcBounds` | `0x6C15D4` | `0x58C0E8` | `0x100115E7C` | `0x11385C` |
| child double→node float 四次写入 | `0x6C15DC..0x6C1608` | `0x58C0F8..0x58C132` | `0x100115E84..0x100115EB0` | `0x11386C..0x1138A0` |
| node float→parent double 合并 | `0x6C1614..0x6C1654` | `0x58C142..0x58C2D8` | `0x100115FCC..0x100116018` | `0x1138B8..0x113A44` |
| 完成后跳过普通路径 | `0x6C1658` | `0x58C2D8` | `0x10011601C` | `0x113A50` |

32 位反编译中 conversion slow path 后的显式 null 赋值是抛异常 helper 的不可达后继形状；
源码级共同语义仍是 `AsObjectNoAddRef`：非 Object Variant 抛出，Object Variant 才进入
native query。Object dispatch 为 null、query 失败、或 query 输出 native record 为 null 时，
resolver 返回 null，而调用方立即递归成员调用。

## 3. 数据流与精度边界

```text
if activeSlot.done:
    continue

if !preview && node.type == 3:
    child = queryNativePlayer(node.childPlayerVariant)  // borrowed
    child.calcBounds()                                  // no null guard

    node.minX = float(child.minX)
    node.minY = float(child.minY)
    node.maxX = float(child.maxX)
    node.maxY = float(child.maxY)

    parent.minX = minInclusive(parent.minX, double(node.minX))
    parent.minY = minInclusive(parent.minY, double(node.minY))
    parent.maxX = maxInclusive(parent.maxX, double(node.maxX))
    parent.maxY = maxInclusive(parent.maxY, double(node.maxY))
    continue
```

父函数不是直接把 child double 合并到自己的 double AABB；node 的四个 float 是强制中间
发布点。因此超出 float 精度的有限 double 会先按目标 FP 规则窄化，overflow 可成为 infinity，
NaN payload/符号细节也受 double→float 转换约束；parent 只看到再次提升后的 float 值。
四个 node float 全部写完后才开始 parent merge。

child 自身的普通 bounds 路径本来就由 float 几何生成整数化 float AABB，常规内容通常看不出
额外窄化；nested child 或 sentinel/非有限边界仍能体现这一中间存储结构。

## 4. owner 与重入边界

- `childPlayerVar` 在 resolver 前后都不 CopyRef；
- NCB query 返回的 native Player 也不增加生命周期引用；
- child 递归建立的是它自己的 ResourceManager Object owner，不会替父 node 的 adaptor 提供
  owner；
- 若递归回调替换父 node 的 `childPlayerVar`，父函数不会重新解析新 child，返回后仍从原 raw
  指针读取 AABB；
- 若父 node Variant 是原 adaptor 的最后 owner，递归期间清除它可使该 raw child 悬空；四端
  没有防御性 retain，这是需要原样记录而不应“安全化”的 malformed/reentrant UAF 边界；
- 非 Object child Variant 在 parent 已经重置四个 `±DBL_MAX` sentinel 后抛出；node AABB 未被
  本分支改写；
- wrong-native/null native Object 落到 null raw pointer，随后直接成员调用，属于 crash/UB
  边界而非安静跳过。

## 5. 回归与验证

新增 bounds 回归覆盖：

1. type-3 child 含普通几何时，child 递归得到 `{1,2,11,21}`，四值先发布到 node float
   AABB，再以同值合并到 parent getter；
2. type-3 child 为 Integer Variant 时，`calcBounds` 在 parent sentinel reset 后抛出，随后
   `getBounds()` 仍呈现只有 `isValid=false` 的一成员 Dictionary。

验证结果：

- 完整 motionplayer 单测 TU 使用真实 Emscripten response file 执行
  `-fsyntax-only`：通过；仅有仓库既有 `_tss` literal-operator 弃用警告；
- 当前源码状态执行 `cmake --build out/web/debug --parallel 8`：通过，重建
  `PlayerRenderItems.cpp`、motionplayer 静态库并成功链接最终 `index.html`；
- 对本纵切面涉及的源码、测试、分析和计划文件执行 `git diff --check`：通过；仅有工作树
  既有 LF/CRLF 转换提示；
- 四份 recovery IDB 的 resolver、递归调用、float 发布与 parent merge 注释均已原位保存。
