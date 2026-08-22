# MotionPlayer prepared-item type-3 child/wrapper 与 parent link 四端复原（2026-08-14）

## 结论

`Player_appendPreparedRenderItems_guess` 对非 preview、active 的 type-3 Motion node 有两条
路径，但两条路径在分流前只解析一次 persistent child Variant：直接借用其中的 dispatch，
做 Player native-instance 查询，取得 raw `Player *`。这里没有局部 Variant CopyRef、dispatch
AddRef 或 null-child guard；persistent node Variant 是唯一正常 owner。non-object 在 object
conversion 阶段抛异常，query 失败/null native 则把 null raw pointer 交给后续递归成员调用。

当 node 的 `drawFlag` 与 `stencilCompositeMaskReferenced` 都为零时，child 直接向 caller
main/aux 递归；任一非零时则复用 node 持久 `PreparedRenderItem` 作为 wrapper，先在其
`childItems` 内构建 child，再把完整 child range 插入 caller main。wrapper 本身从不进入
caller main；只有 `drawFlag` 会把同一 wrapper 指针追加到 caller aux，mask-reference 单独命中
不会追加 aux。

本轮还闭合了普通 item 与 type-3 wrapper 共用的 visible-ancestor parent link 边界。原生字段
是 nullable raw `MotionNode *`，四端都只判 null：非 null 就直接确保该 node 的 persistent
item 并把其指针写入 `parentItem`。没有 deque 范围检查，也不排除 ancestor 指向当前 node
自身。本地以 index 代替 raw pointer，因此 `-1` 必须是唯一 null 哨兵；此前的 upper-bound
和 self guards 已删除。

## 四目标映射

| 目标 | builder | type-3 entry / borrowed child resolve | plain recursion | wrapper aux/ancestor | wrapper child recursion | caller-main range insert |
|---|---:|---:|---:|---:|---:|---:|
| Android arm64 | `0x6BF714` | `0x6C0610` / `0x6C061C..0x6C0668` | `0x6C0504` | `0x6BFE8C` | `0x6BFF3C` | `0x6BFF50` |
| Android armv7 | `0x58B178` | `0x58B36A` / `0x58B384..0x58B3BC` | `0x58BAFA` | `0x58BA58..0x58BA98` | `0x58BABC` | `0x58BACA` |
| iOS arm64 | `0x1001148F8` | `0x100114B14` / `0x100114B30..0x100114B7C` | `0x1001153BC` | `0x1001150AC..0x100115104` | `0x100115160` | `0x100115174` |
| iOS armv7 | `0x1123D8` | `0x112500` / `0x11251A..0x112562` | `0x112D44` | `0x112C7A..0x112CBA` | `0x112CFA` | `0x112D0C` |

普通 source-item 的 parent-link 位置独立对应如下：

| 目标 | nullable visible-ancestor read / ensure / item store |
|---|---:|
| Android arm64 | `0x6BF9BC..0x6BFA34` |
| Android armv7 | `0x58B5D8..0x58B5E8` |
| iOS arm64 | `0x100114E38..0x100114E4C` |
| iOS armv7 | `0x11285E..0x112874` |

后三端调用已命名的 `ensureNodePreparedRenderItem_guess`；Android arm64 将同一构造内联。
item 自身的 raw owner、稳定地址、布局和析构已经记录在
`motionplayer_prepared_render_item_lifecycle_four_binary_2026-08-13.md`。

## 入口和 child owner

四端共同入口顺序是：

```text
selectedNode.drawnThisFrame = false
... non-preview type-4 branch ...
if !selectedNode.accumulated.active:
    continue

if !player.preview && selectedNode.type == 3:
    inheritedColor = selectedNode.inheritFlags & 0x200
        ? effectiveColor
        : 0xFF808080

    childDispatch = selectedNode.childPlayerVariant.AsObjectNoAddRef()
    nativeAdaptor = childDispatch.NativeInstanceSupport(
        GETINSTANCE, PlayerClassID)
    child = success && nativeAdaptor ? nativeAdaptor.native : null

    ... choose plain or wrapper path ...
```

Variant conversion和 native query 发生在 draw/mask 分流之前。因此即便 node 最后会走
wrapper，malformed child 仍会先抛/返回 null；不会因 wrapper 已存在而绕过 child。

没有局部 owner 的含义是：

- ordinary execution 依靠 `node.childPlayerVariant` 持有 adaptor/native child；
- resolver 不调用 AddRef，递归返回后也不 Release；
- child 递归中若通过重入途径清掉 parent node Variant，并且没有其他 owner，raw child 可在
  当前成员调用期间失效；四端没有 snapshot owner 来阻止这一点；
- non-object（Void/Integer 等）由 `AsObjectNoAddRef` 抛 conversion error；object dispatch 为
  null、class query 失败或成功 query 给出 null native 都可形成 null child，caller 无 guard。

本地 `MotionNode::getChildPlayer()` 保持 borrowed resolver：只让 non-object 转换异常自然
传播，对 null dispatch/query 返回 null。前一纵切面删除的通用 child-recursion null early
return 同时恢复了 type-3 的该 unchecked call 边界。

## plain 路径

gate 为严格的双零：

```text
if node.drawFlag == 0 && node.stencilCompositeMaskReferenced == 0:
    child.appendPreparedRenderItems(
        callerMain,
        callerAux,
        inheritedColor,
        callerInheritedDrawFlag19,
        callerInheritedFlag18 || node.priorDraw)
    continue
```

plain 路径不设置 node `drawnThisFrame`，不确保 wrapper item，不清 `childItems`，也不额外
range-insert。child 直接改动 caller 的两个 vector，递归产生的正常 item 顺序原样进入父级。

## wrapper 路径

`drawFlag != 0 || maskReferenced != 0` 的共同伪代码是：

```text
node.drawnThisFrame = true
wrapper = ensure(node.preparedRenderItem)

refresh wrapper owner label, bounds/viewport, stencil and wrapper flags
apply canonical-root draw affine to wrapper bounds when enabled
wrapper.drawFlag = false

parent = null
if node.drawFlag:
    callerAux.push_back(wrapper)
    if node.visibleAncestor != null:
        parent = ensure(node.visibleAncestor.preparedRenderItem)
wrapper.parentItem = parent

wrapper.childItems.clear()                    // capacity retained
child.appendPreparedRenderItems(
    wrapper.childItems,
    callerAux,
    inheritedColor,
    true,                                      // forced child draw flag
    callerInheritedFlag18 || node.priorDraw)

callerMain.insert(callerMain.end,
                  wrapper.childItems.begin,
                  wrapper.childItems.end)
continue
```

重要拓扑如下：

- wrapper 是 node 独占、跨帧地址稳定的 raw-owner item；
- wrapper `childItems` 只借用 descendant item pointers，clear 保留 backing capacity；
- child recursion 的 main 改为 wrapper `childItems`，aux 仍是 caller 的同一 vector；
- forced draw argument 恰好为 1，与 caller 原 `inheritedDrawFlag19` 无关；
- child range 全部完成后才插入 caller main，wrapper 本身不插入；
- maskRef-only 会构造 wrapper/child range，但 aux 不含 wrapper，`parentItem` 被写 null；
- drawFlag 会先追加 aux，再解析 ancestor parent；ancestor item 的懒创建不自动把 ancestor
  加入 main 或 aux。

## parent link 的 raw-pointer 边界

visibility phase 正常情况下把每个非 root node 的 native field写成 parent node 或 parent 已
解析的 ancestor raw pointer。prepared builder 的消费者只做：

```text
if visibleAncestorPointer != null:
    parentItem = ensure(visibleAncestorPointer.preparedRenderItem)
else:
    parentItem = null
```

它不验证 pointer 是否属于当前 deque、是否仍存活、是否为当前 node、是否对应
`visibleAncestorIndex < nodes.size()`。因此 self ancestor 是稳定且可测试的边界：ensure 返回
当前 node 已有 item，最终 `item.parentItem == item`。普通 item 与 type-3 wrapper 两路都如此。

portable 结构保留 index surrogate 以免 deque copy/build 代码混入宿主 raw pointer，但消费
条件已改成 `visibleAncestorIndex != -1`，随后直接 unchecked `_nodes[index]`。这比原先
`0 <= index < size && index != node.index` 更接近 native：

- `-1` 对应 null；
- self 对应 self pointer并形成 self parent link；
-其他非法值不再 quiet-clear parent link，而进入 unchecked container boundary。

## publication 与异常时序

四端 wrapper 的可观察部分更新顺序为：

1. child resolver；
2. node `drawnThisFrame = true`；
3. ensure/reuse wrapper并逐字段刷新；
4. 若 drawFlag，先 `aux.push_back(wrapper)`；
5. 再读取/ensure visible ancestor；
6. 最后把本轮局部 parent pointer 写入 persistent `wrapper.parentItem`；
7. clear `childItems`；
8. recursive child build；
9. range-insert caller main。

由此得到四端共同的部分状态：

- wrapper allocation 抛异常时，drawn byte 已为 true；
- aux growth 抛异常时，parentItem store 尚未执行，persistent wrapper 保留上一帧旧 parent；
- ancestor item ensure 抛异常时同样保留旧 parent，而 wrapper 已成功追加 aux；
- parent store 后 child recursion 抛异常时，childItems 保留已构造 prefix，caller main 尚未
  接收该 range，caller aux 与 descendant 对 shared aux 的已完成追加不回滚；
- caller-main range insert 抛异常时，wrapper childItems 已完整构建，其他 persistent/item
  和 aux 修改不回滚；具体 caller vector 内部强保证取决于对应 STL 实现。

本地旧代码在 aux push 之前先把 `wrapper.parentItem = nullptr`，错误消除了前两类异常后的
stale-parent 状态。本轮改为局部 `wrapperParentItem`，在 aux/ancestor 操作完成后统一写回。

普通 item 的 parent link 同样在 source/color/opacity/stencil/draw 写入之后、raw paint/clip
geometry 复制之前发生。本地 parent-link block 已移到相同语义阶段；这使 ancestor ensure
与 malformed clip pointer 的先后关系不再沿用旧单端注释推断。

## 本地修正与测试

实现调整位于 `PlayerRenderItems.cpp`、`MotionNode.h` 与 test-only `Player.h` entry：

1. wrapper/ordinary 两路均把 nullable index 判断改为 `!= -1`，删除 upper-bound 和 self
   guards；
2. wrapper 使用局部 parent pointer，aux push 和 ancestor ensure 成功后才发布
   `wrapper.parentItem`；
3. ordinary parent-link materialization 移到四端共同的 source/color/opacity/stencil 后、
   paint/clip geometry 前；
4. 删除旧 Android arm64 绝对地址注释，精确映射只保留本文；
5. 增加不经过 command generation 的 test-only prepared topology entry，未注册为脚本成员。

测试在同一个 type-3 parent/child fixture 中令 wrapper node 和 child ordinary leaf 各自把
visible ancestor 指向自身，直接运行 recursive prepared builder，并确认：

```text
wrapper.parentItem == wrapper
leafItem.parentItem == leafItem
```

既有测试继续覆盖 plain 无 wrapper、maskRef-only wrapper、drawFlag aux+ancestor、child range
拓扑，以及 normal/preview stencil-composite 对 type-3 range/wrapper 的不同消费方式。

## recovery IDB 改善

四个 recovery IDB 都已在 type-3 entry、borrowed child resolver、plain recursion、wrapper
aux/ancestor、wrapper child recursion/range insert，以及 ordinary parent-link 位置写入语义
注释。注释明确区分：child dispatch 没有 local retain；visible ancestor 只有 null test；
self 不排除；aux/ancestor 成功后才 store parent；递归无 null-child guard。

## 验证

- 修改后的完整 motionplayer Catch2 翻译单元 Emscripten syntax-only 检查通过，仅有既有
  `_tss` literal warning；
- `cmake --build --preset "Web Debug Build"` 完成 33 个受头文件影响的 motionplayer 编译/
  链接步骤并成功生成最终 Web 输出；
- scoped `git diff --check` 通过，仅报告仓库既有的 LF/CRLF 转换提示；
- 四个 recovery IDB 在 type-3 borrowed child、plain/wrapper、aux-before-parent、self/raw
  ancestor 和 ordinary parent-link 注释写回后均保存成功。

## V235 后续纠正（2026-08-18）

本报告已正确固定 wrapper 拓扑和 aux-before-parent 时序，但“逐字段刷新”没有列出 wrapper
明确不写的 native 字段。V235 对四端完整 wrapper 窗口复核后确认：wrapper 只刷新
`ownerLabel`、paintBox、viewport、draw=false、stencil、parent 和 childItems；尤其没有任何
`sourceState` store，也不写 ordinary numeric/source/mesh suffix。fresh pointer 保持 dormant，reused
item 保持 stale。源码已删除旧 `wrapper.sourceState=&node.source`，并用 child-Octet 异常 fixture 的
显式 sentinel 锁定边界。完整映射与异常矩阵见
`motionplayer_prepared_type3_wrapper_stencil_stale_source_four_binary_2026-08-18.md`。
