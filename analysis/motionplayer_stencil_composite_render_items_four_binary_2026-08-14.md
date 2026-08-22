# MotionPlayer stencil-composite prepared-item 后处理四端复原（2026-08-14）

## 结论

四个当前参考二进制共同证明，stencil-composite 的 render-item 拓扑不是在
`getCommandList` 序列化阶段临时推导，也不是从已经生成的 main list 反向扫描得到。
它是 `Player_appendPreparedRenderItems_guess` 在完成当前 Player 的普通节点遍历之后执行的
第二个、独立的 **原始节点顺序后处理**：

1. 只选 `type == 12 && (stencilType & 4) != 0 && drawnThisFrame` 的节点；
2. 确保其 node-owned persistent `PreparedRenderItem` 存在；
3. 把同一个借用指针追加到共享 aux list；
4. 清空该 item 的 `childItems`，先放入 item 自身；
5. 按 `stencilCompositeMaskNodes` 的原始 pointer-vector 顺序处理每个目标；
6. type 0 直接追加目标 item；type 3 在 preview 模式追加 wrapper item，在普通模式展开
   wrapper 的整个 `childItems` 范围；其他类型跳过。

原始 mask pointer-vector 被完全信任：元素加载后立即读取目标节点的
`drawnThisFrame`，四端都没有空指针检查；重复指针也不去重。因此本地原有
`if (!maskNode)` 容错是可观察的非原生边界，本轮已删除。

本轮只使用 `reference/binaries/` 的 Android arm64、Android armv7、iOS arm64 和
iOS armv7 四个目标重新取证；旧 `libkrkr2.so` 的单端地址式源码注释不作为证据。

## 四目标函数和代码块映射

| 目标 | recursive builder | type-12 资格块 | mask 元素循环 | pointer-vector range insert |
|---|---:|---:|---:|---:|
| Android arm64 | `Player_appendPreparedRenderItems_guess` `0x6BF714` | `0x6C0B20` | `0x6C0BE0` | `0x6F0804` |
| Android armv7 | `Player_appendPreparedRenderItems_guess` `0x58B178` | `0x58BB22` | `0x58BB8A` | `0x5AE0BC` |
| iOS arm64 | `Player_appendPreparedRenderItems_guess` `0x1001148F8` | `0x1001153F8` | `0x1001154C8` | `0x100115864` |
| iOS armv7 | `Player_appendPreparedRenderItems_guess` `0x1123D8` | `0x112D76` | `0x112E16` | `0x11318C` |

四个 range-insert 实例都已在 recovery IDB 中重命名为
`PreparedRenderItemPtrVector_insertRange_guess`。它们实现平凡裸指针元素的
`vector::insert(position, first, last)`：范围为空时直接返回；容量足够时原位搬移，容量不足
时分配新 backing、按 prefix → inserted range → suffix 的顺序复制，最后释放旧 backing。
元素本身没有 AddRef、Release 或 delete。

## 当前四端布局对应

下表用于说明反编译中的字段身份。偏移只属于各原生 ABI，不进入可编译源码注释：

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| node type | `+0x1C` | `+0x14` | `+0x1C` | `+0x14` |
| node stencil byte | `+0x34` | `+0x2C` | `+0x34` | `+0x2C` |
| node prepared-item owner | `+0x770` | `+0x680` | `+0x780` | `+0x65C` |
| node `drawnThisFrame` | `+0x798` | `+0x694` | `+0x7A8` | `+0x670` |
| mask vector begin/end | `+0xA28/+0xA30` | `+0x8CC/+0x8D0` | `+0xA38/+0xA40` | `+0x8A4/+0x8A8` |
| current Player preview byte | `+0x444` | `+0x2E8` | `+0x3D4` | `+0x2A8` |

native node stride 分别为 Android arm64 `0xA48`、Android armv7 `0x8E0`、iOS
arm64 `0xA58`、iOS armv7 `0x8B4`。四端外层循环都从逻辑节点 1 开始，保持 node
container 的正向逻辑顺序；Android libstdc++ deque 与 iOS libc++ deque 的寻址公式不同，
但不是不同的源级遍历算法。

`PreparedRenderItem::childItems` 则是 64 位 item 的 `+0x18/+0x20/+0x28` 和 32 位
item 的 `+0x10/+0x14/+0x18` 三指针 vector。后处理的 clear 只令 end 回到 begin，保留
capacity 和 backing；随后 push item 自身。该自指针只形成借用拓扑，不是所有权环。

## 共同数据流与调用次序

四端可以归一为以下源级轮廓：

```text
appendPreparedRenderItems(player, main, aux, inheritedColor, flags...):
    build current player's ordinary/type-3/type-4 items
    // recursive child calls share main and aux and finish their own post-pass

    for node in player.nodes[1..end) in logical order:
        if node.type != 12:
            continue
        if (node.stencilType & 4) == 0:
            continue
        if node.drawnThisFrame == 0:
            continue

        parentItem = ensureNodePreparedRenderItem(node)
        aux.push_back(parentItem)

        parentItem.childItems.clear()       // capacity retained
        parentItem.childItems.push_back(parentItem)

        for maskNode in node.stencilCompositeMaskNodes in stored order:
            // raw load then immediate dereference; no null check
            if maskNode.drawnThisFrame == 0:
                continue
            if maskNode.type == 0:
                parentItem.childItems.push_back(
                    ensureNodePreparedRenderItem(maskNode))
            else if maskNode.type == 3:
                maskItem = ensureNodePreparedRenderItem(maskNode)
                if player.preview:
                    parentItem.childItems.push_back(maskItem)
                else:
                    parentItem.childItems.insert(
                        end,
                        maskItem.childItems.begin,
                        maskItem.childItems.end)
            else:
                continue
```

这里的 `player.preview` 是当前递归层的 `Player` 第一个参数，而不是 root Player 的
preview，也不是 type-3 child Player 的 preview。四端函数开头都把第一个参数保存在
长期活跃寄存器中，后处理从该对象读取上述 preview 字节。

当前 Player 在普通节点遍历中调用 child Player 时，共用调用者的 main/aux vector；child
调用在返回前先完成自己的 type-12 后处理。因此共享 aux 的总体顺序是递归深度优先产生的
已有条目，随后才是当前 Player 按节点顺序追加的 type-12 composite 条目。外层
`prepareRenderItems` 之后只 stable-sort main list；这个后处理不会排序 aux，也不会根据 main
的最终排序重排 mask children。

## 三种目标行为

### type 0

目标必须已经 `drawnThisFrame`。满足时才懒创建/复用它的 persistent item，然后把该 item
裸指针直接 push 到 parent `childItems`。目标 item 不因此额外进入 main；正常情况下它已在
前一遍 ordinary builder 中进入 main。

### type 3，普通模式

后处理不把 wrapper 自身放入 parent child list，而是对 wrapper 当前的
`childItems[begin,end)` 做 range insert。该范围来自前面的 type-3 wrapper 构造及 child
Player 递归，因此 parent stencil mesh 看到的是 type-3 下展开的真实 render children。

- 空 child range 什么也不追加；
- 多个 child 保持 wrapper 内原始顺序；
- 同一个 type-3 mask 指针出现多次，就多次插入完整范围；
- 插入只复制裸 item 指针，item 仍由各自 MotionNode 所有。

### type 3，preview 模式

后处理读取当前 Player 的 live preview byte，直接追加 type-3 persistent wrapper item，
不展开其可能保留的 child range。该 preview 读取发生在 mask 循环内部，所以原生并没有在
进入后处理前缓存一个独立 snapshot。

## 边界行为与异常时间线

- canonical node-tree builder 只把真实 type-0/type-3 节点地址写入 mask vector，因而正常
  数据不会出现 null；但后处理自身没有防御，null 元素会在读取 `drawnThisFrame` 时直接
  非法解引用，而不是被静默跳过。
- mask vector 不去重，也不根据 label、node index、main order 或 sort key 二次排序。
- 即使构树阶段保证目标类型为 0/3，render 后处理仍重复检查 type；其他类型直接跳过。
- 未 drawn 的目标在 ensure 之前跳过，不会仅因被 mask 引用就分配 persistent item。
- qualifying type-12 parent 的同一 item 指针同时可存在于 main、aux 以及自身 childItems[0]；
  三处都是 borrow，不增加 owner 数量。
- parent ensure 先于 aux push。若 item 分配抛异常，两个输出 vector 都未被本步骤修改。
- aux push 先于 child clear。若 aux 扩容失败，parent 的旧 child range 保持不变。
- child clear 先于 self push。若 self push 扩容失败，aux 已含 parent 指针，而 parent child
  range 已经变空；没有事务式回滚。
- 后续每个 direct push/range insert 也按已完成前缀保留部分结果；异常继续向外传播。
- `childItems.clear()` 和 item 析构都只管理 pointer-vector backing，不释放任何元素。

## 本地修正与测试

`cpp/plugins/motionplayer/PlayerRenderItems.cpp` 原有拓扑、顺序、type-0/type-3 分支以及
preview gate 已经基本一致，但额外接受 null mask pointer。本轮删除该 null guard，并把
仍引用旧单目标地址/布局数字的相邻注释改为四端共同的源级说明。

单元测试现额外锁定：

- 原始 mask pointer-vector 的反向顺序与重复元素都原样进入 childItems；
- `getCommandList` 生成的 stencil mesh Array 保留相同的重复别名顺序；
- 普通模式下重复 type-3 mask target 重复展开 wrapper child range；
- preview 模式下同一 type-3 target 改为重复追加 wrapper 本身；
- parent、target 与 child 的 persistent item 地址在多次构建中保持稳定。

null 直接解引用边界不在单元测试进程内主动触发，以免把预期 native crash 变成测试进程
本身的非确定失败；它由四端逐指令证据和源码中不再存在的 guard 共同固定。

## recovery IDB 改善

四个 recovery IDB 均已：

- 在 type-12 资格块标注三项 gate；
- 在 parent item block 标注 aux append、clear 与 self seed 次序；
- 在 mask load 点标注 raw pointer 立即解引用、无 null guard、无 dedup；
- 在 type-3 分支标注 current-Player preview 对 wrapper append / child-range insert 的选择；
- 将对应 STL range-insert 实例命名为
  `PreparedRenderItemPtrVector_insertRange_guess`。

这些名字保留 `_guess`，因为参考二进制没有可恢复的原始 C++ 标识符；精确地址只记录在本文。

## 验证

- motionplayer 单元测试翻译单元的 Emscripten 完整语法检查通过；
- `cmake --build --preset "Web Debug Build"` 完整编译及最终 WebAssembly/HTML 链接通过；
- `git diff --check` 通过，仅报告仓库既有的 LF/CRLF 转换提示；
- 四个 recovery IDB 均在语义命名和行注释写回后保存成功。

## V235 后续复核（2026-08-18）

V235 在恢复 type-3 wrapper stale-source 边界时重新展开了四端完整 type-12 post-pass，确认本报告的
aux→clear→self→stored-order masks 拓扑仍成立，并补齐每端 exact commit stage 与 allocation-failure
prefix。post-pass 本身只重建 `childItems`，不刷新 parent item 的 ordinary fields；type-3 mask 在
normal 模式展开的 wrapper 同样没有 sourceState publication。后续统一报告见
`motionplayer_prepared_type3_wrapper_stencil_stale_source_four_binary_2026-08-18.md`。
