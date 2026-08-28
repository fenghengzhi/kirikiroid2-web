# LayerGetter.vtx Dictionary/Array owner 流（四参考二进制，2026-08-26）

## 1. 范围与四端映射

本纵切面闭合 `LayerGetter.vtx` 的正常数据流和 owner 流：从 node 的八个 float
vertex 值构造四个新 Dictionary，再装入一个新 Array。它与 `Quad.p` 共用同一组
进程静态 `x/y` member hint，但数据源是 float vertex output，不是 geometry record
中的 double。

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| `vtx` getter | `LayerGetter_getVtx_guess@0x699894` | `...@0x574C44` | `...@0x1000F893C` | `...@0xF5744` |
| Dictionary factory | `0x9C6D40` | `0x7384A8` | `0x1000A7A38` | `0xA6900` |
| object append helper/slow path | inline / `0x68942C` | `0x55F68C` | `0x100120FF8` | `0x11FE2C` |
| x/y hint slots | `0x1AB5234/0x1AB5238` | `0x1111768/0x111176C` | `0x101B696FC/0x101B69700` | `0x187D42C/0x187D430` |

四个 getter 均在本轮 fresh decompile。x/y helper、hint 和外层 Array owner 与本轮
已闭合的 `Quad.p` 路径交叉验证。

## 2. 共同源码伪代码

```text
out = fresh TJS Array + borrowed native Items

for i = 0..3:
    dict = fresh TJS Dictionary accessor
    dict.SetValue("x", real(double(node.vertices[2*i])),
                  TJS_MEMBERENSURE, &globalXHint)
    dict.SetValue("y", real(double(node.vertices[2*i+1])),
                  TJS_MEMBERENSURE, &globalYHint)
    Items.emplace_back(ObjectVariant(dict.dispatch, dict.dispatch))
    destroy local dictionary accessor

return Variant CopyRef(out)
destroy local out Variant
```

每次 getter 调用创建五个新脚本对象（一个 Array、四个 Dictionary）。Dictionary
顺序固定为 TL、TR、BR、BL 对应的当前 vertex 数组顺序；getter 本身不重新排序、
求 AABB、应用矩阵或做 camera conversion。

## 3. 数据与转换边界

| 目标 | vertex[0] 起点 | 元素 stride | 每点布局 |
|---|---:|---:|---|
| Android arm64 | node `+1856` | 8 | `float x; float y` |
| Android armv7 | node `+1616` | 8 | `float x; float y` |
| iOS arm64 | node `+1872` | 8 | `float x; float y` |
| iOS armv7 | node `+1580` | 8 | `float x; float y` |

SetValue helper 把每个 float 按 IEEE 规则提升为 TJS real/double。NaN、Inf、signed
zero 与 float 已有精度原样进入 double；没有 clamp 或 finite gate。循环次数用无符号
比较固定为四，既不读 vector size，也不因退化点提前停止。

脚本键是完整 UTF-16LE `x` / `y`，flags 恒为 `512` (`TJS_MEMBERENSURE`)；hint
参数是进程静态可写槽。首次/后续 SetValue 可能更新 hint，四个点和 `Quad.p` 都复用
同一槽，不为每个 Dictionary 重置。

## 4. Dictionary owner 流

每轮 factory 返回一个本地 accessor 所拥有的 Dictionary dispatch。向 Array Items
追加 object Variant 时：

1. object Variant 的 Object 与 ObjThis 都指向同一 Dictionary dispatch；
2. 两个字段分别 AddRef，所以 object Variant 拥有两条引用边；
3. 追加完成后本地 accessor 析构，Release 它自己的 factory/accessor 引用；
4. Array Items 内的 object Variant继续以 Object+ObjThis 两条边拥有 Dictionary；
5. Array 析构/清空时，元素 Variant 对两条边分别 Release。

外层 Array Variant复制到隐藏返回槽后再析构局部 Variant，owner 平衡与
`Quad.p` 完全相同。getter 不把 Dictionary 存回 node，也不让本地 accessor 活过
当前迭代。

## 5. 失败与边界

- node/facade 没有 null guard；第一次 vertex load 前的任何无效 pointer 都直接落入
  native 未定义边界；
- Dictionary SetValue 返回的普通 TJS error 没有在 getter 源层转换为 bool 或跳过
  元素；helper/exception 行为直接传播；
- 四个 Dictionary 必须按顺序追加，不能过滤某个失败点或用一个 Dictionary 反复
  改值；
- Array Items block grow 规则与 Array-container 报告相同：Android 为 25/42 元素
  block，iOS 为 204/341 元素 block；本 getter正常只追加四项，通常走当前 block，
  但一个新 Array 的初始 deque 状态仍由 factory 决定；
- Android arm64 与 iOS armv7 显式 cleanup 形态能看到 local Dictionary release；
  Android armv7 extab 与 iOS arm64 LSDA 的每个 SetValue/append call-site frontier
  仍需完整 EH 审计，单列为 evidence-blocked。

## 6. 本地逐行对照

`PlayerLayerQuery.cpp::LayerGetter::getVtx` 当前：

- 创建一个 fresh Array；
- 固定循环四次；
- 对 `vertices[i*2]` 与 `[i*2+1]` 分别写 `x/y`；
- 使用 `TJS_MEMBERENSURE` 和共享 `xMemberHint_guess/yMemberHint_guess`；
- 用相同 dispatch 作为 Object 与 ObjThis 追加；
- 每轮 Dictionary accessor 为局部 owner。

逐项与四端正常路径一致，不需要运行 C++ 修改。当前 `MotionNode.h` 把 vertices 按
逻辑区放在后部；reference 坐标是完整 node source-order 账本的一部分，仍需与构造/
析构证据合并后恢复声明顺序。

## 7. 2026-08-27 EH 闭包

`motionplayer_layergetter_quad_exception_frontiers_four_binary_2026-08-27.md` 已闭合 vtx 的
每个 SetValue/append/return前沿：Android arm64 ordinary landing、iOS arm64 18 条
LSDA-only cold cleanup与 iOS armv7 44 条 SjLj dispatcher按 active state Release current
Dictionary、析构 outer Array并 resume；destructor-throw states terminate。Android armv7
完整函数和相邻 catalog无本帧 cleanup。该异常条目现为 `IMPLEMENTED`；MotionNode
source-order 仍由独立结构条目承接。
