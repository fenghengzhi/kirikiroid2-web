# MotionPlayer modified-emoteEdit prepass 的 owner、hint 与失败边界（四参考，2026-08-16）

## 1. 结论

四个 `reference/binaries/` 的 `Player_refreshModifiedNodeTimelines_guess` 都遍历非 root
节点的实时 deque 区间 `[1, nodeCount)`。每个节点唯一的前置门槛是
`forceVisible != 0`；通过后立即复制 `emoteEditVariant`、做严格 Object 转换并取得一个
独立 retained dispatch。没有 `Type == tvtObject` 预检，也没有 null-dispatch 友好跳过。

同一 retained dispatch 与同一专用 process-global member hint 先执行
`PropGet("modified")`。Boolean 为真时，函数把 Integer `0` 通过
`PropSet(TJS_MEMBERENSURE, "modified")` 写回，忽略 setter status，销毁 setter 临时
Variant，然后调用完整的 `Player_initializeNodeTimelineSlots_guess(Player &, MotionNode &)`。
dispatch owner 跨 getter、setter 和 initializer 存活，并在正常/异常路径统一释放。

这条链是脚本可观察的数据流，不是“web port 没有 setter 所以 inert”的占位路径：
`emoteEdit` 本身是脚本 owner，外部代码可以发布 `modified`，而 prepass 也确实通过 TJS
setter 清零。

## 2. fresh 四端映射

| 目标 | prepass | copy / strict retained Object | get `modified` | zero / set `modified` | absolute initializer | owner release |
|---|---:|---:|---:|---:|---:|---:|
| Android arm64 | `0x6B3C58` | `0x6B3D3C` / `0x6B3D44..0x6B3D84` | `0x6B3DA0` | `0x6B3DA8..0x6B3DF0` | `0x6B3DFC` | `0x6B3E00..0x6B3E18` |
| Android armv7 | `0x582A7C` | `0x582ADA..0x582AEA` | `0x582B04` | `0x582B08..0x582B1E` | `0x582B26` | `0x582B30..0x582B38` |
| iOS arm64 | `0x10010A88C` | `0x10010A914..0x10010A92C` | `0x10010A948` | `0x10010A94C..0x10010A964` | `0x10010A970` | `0x10010A978..0x10010A98C` |
| iOS armv7 | `0x10820C` | `0x1082AC..0x1082C2` | `0x1082EC` | `0x1082F0..0x108318` | `0x108324` | `0x10832A..0x108338` |

四份 fresh 函数体的分支形状一致。Android 使用 libstdc++ deque map/block 算术，iOS
使用 libc++ deque 布局；这只改变节点寻址。每次脚本回调/initializer 返回后，循环条件
都重新从 Player 的 deque 读取 live end/count，没有固定长度快照，也没有尾 sentinel。

## 3. 专用 member hint

`modified` getter 与 setter 在每个平台都共享一处可写的 process-global hint：

| 目标 | hint 地址 | fresh xref 结果 |
|---|---:|---|
| Android arm64 | `0x1AB541C` | 2 个引用，均在该 prepass 内 |
| Android armv7 | `0x11118B8` | 6 个引用，均为该函数主体/EH 邻接块 |
| iOS arm64 | `0x101B698E4` | 1 个装载点，在该 prepass 内 |
| iOS armv7 | `0x187D588` | 6 个引用，均在该函数主体内 |

因此它不是 flipX/flipY/zoom/slant 等 emoteEdit 属性的共享缓存，也不是每次调用的栈
临时。本地恢复为 TU-local `emoteEditModifiedHint_guess`，getter 和 setter 都传其地址。
绝对地址只保留在本分析与 recovery IDB 中。

## 4. TJS helper 边界

prepass 使用的 Boolean getter helper 对照如下：

| 目标 | Boolean named getter |
|---|---:|
| Android arm64 | `0x660AB4` |
| Android armv7 | `0x552124` |
| iOS arm64 | `0x1000F3078` |
| iOS armv7 | `0xEF7F0` |

四端共同执行一次 `PropGet(flags, member, hint, &temporary, dispatch)`，receiver 与
objthis 是同一 dispatch；它忽略 HRESULT，随后把临时 Variant 转为 Boolean 并析构。
没有 `TJS_MEMBERMUSTEXIST` probe，也没有先调用 HasValue。

Integer/byte setter helper 的对应地址为 Android arm64 `0x5A2540`、Android armv7
`0x4E2568`、iOS arm64 `0x100102BD0`、iOS armv7 `0xFFFF8`。它从 byte 值构造 Integer
Variant，以传入 flags/member/hint 和同一 receiver/objthis 调一次 PropSet，比较 status
是否恰为 `TJS_S_OK`，再销毁临时 Variant。prepass 不观察该 Boolean 返回值，所以 setter
失败不阻止随后的绝对 initializer。

## 5. owner 与销毁顺序

四端的共同 source-level 语义可写成：

```text
for (i = 1; i < liveNodeCount; ++i) {
    node = liveNode(i)
    if (node.forceVisible == 0) continue

    temporary = CopyRef(node.emoteEditVariant)
    emoteEdit = strict AsObject(temporary)  // retained dispatch
    Destroy(temporary)

    if (!ToBoolean(PropGet(emoteEdit, "modified", dedicatedHint))) {
        Release(emoteEdit)
        continue
    }

    zero = Integer(0)
    ignore PropSet(emoteEdit, TJS_MEMBERENSURE,
                   "modified", dedicatedHint, zero)
    Destroy(zero)
    initializeNodeTimelineSlots(player, node)
    Release(emoteEdit)
}
```

独立 owner 的作用在重入时可见：getter 可以清空或替换
`node.emoteEditVariant`，但 Boolean-false 的释放、Boolean-true 的 setter 以及 initializer
前后的 unwind 都仍操作既有 retained dispatch。逐次从 persistent Variant 重新取得裸指针
会在 getter 回调后丢失 receiver，并非四端行为。

setter 的 Integer 临时必须在 initializer 前析构；若 initializer 抛异常，异常清理只需要
释放 retained emoteEdit dispatch（以及 initializer 自身已建立的局部 owner），不会把 zero
Variant 的生命周期错误延长到 timeline parsing。

## 6. malformed 与异常提交边界

旧本地实现用 `rawDispatchObject` 把两种输入静默跳过：非 Object Variant 和 Object type
但 null dispatch。四端没有这层过滤：

- 非 Object 在 `forceVisible` 通过后立即进入严格 `AsObject`，转换异常自然传播；
- typed-null Object 也不降级为 continue，而是在严格 owner/首次实际使用处自然失败；
- getter status 被忽略，结果临时再执行普通 Boolean 转换；
- setter status 被忽略，即使失败仍调用 absolute initializer；
- modified 清零发生在 initializer 之前；initializer 抛异常不会回滚已发生的 PropSet；
- retained dispatch 通过异常清理释放，persistent Variant 的重入修改同样不回滚。

这与 node absolute reseed 的非事务式提交模型一致：脚本访问、slot reset/parse/merge、source
refresh 或 action 中任一步失败，都只按已经执行到的位置保留状态，不提供高层 rollback。

## 7. 源码与差分回归

本轮源码迁移：

- 删除仅由该 prepass 使用的 `rawDispatchObject`；
- `forceVisible` 后复制 persistent Variant，严格构造 `ncbPropAccessor`，随后立即清理临时
  Variant；
- 用 retained dispatch 和专用 hint 执行一次 Boolean getter；
- 用同一 dispatch/hint 执行 scoped Integer-zero setter，并在 initializer 前结束临时作用域；
- 保持 `for (i < _nodes.size())` 的 live deque 终点重读；
- 删除 `Player.h` 中“没有 modified setter 因而 inert”的过时注释。

单元回归固定三个可观察边界：

1. modified-false getter 重入清空 persistent owner 后，调用仍正常返回，dispatch 在分支尾
   恰好析构一次，getter flags 为 0、hint 非空、objthis 为 retained receiver；
2. modified-true getter 清空 persistent owner 后，setter 仍以同一 receiver/hint、
   `TJS_MEMBERENSURE` 和 Integer 0 执行；即使返回 `TJS_E_FAIL`，仍进入故意抛错的
   absolute initializer，owner 在 unwind 中恰好释放一次；
3. force-visible 节点的 Integer emoteEdit 不再静默跳过，而是抛出严格 Object 转换异常。

## 8. 与旧记录的关系

`motionplayer_node_timeline_slot_helpers_four_binary_2026-08-14.md` 已正确识别 prepass 函数
边界及其对 absolute initializer 的调用，但没有完整记录 `emoteEdit` 的 retained owner、
专用 hint、setter 临时销毁顺序和 malformed natural-failure 边界；这些细节以本文为准。

`motionplayer_player_state_owner_comment_migration_four_binary_2026-08-15.md` 已正确把该
prepass 放在 scaled-delta 写入之后的 frame-core 入口。本轮只补齐其内部对象生命周期与
脚本分派边界，不改变 frameProgress 的阶段顺序。
