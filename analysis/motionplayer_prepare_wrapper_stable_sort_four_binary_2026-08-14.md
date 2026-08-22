# MotionPlayer prepareRenderItems 外层与 stable-sort 四端复原（2026-08-14）

## 结论

`Player_prepareRenderItems_guess` 是 recursive prepared builder 外唯一的 motion-content gate 和
main-list sort wrapper。四个当前参考二进制都不创建、不清空 caller 的 main/aux vectors：
motion-content Variant 为 Void 时立即返回 false，两个 vector 完全不动；任何非 Void type tag
都先用 neutral color 与两个 false lineage flags 递归 append，然后对 caller main 的**整个当前
范围**做 stable sort，aux 原样保持，最后无条件返回 true，即使 main 为空。

sort comparator 也是 trusted raw-pointer 边界。四端 comparator 都直接解引用两只
`PreparedRenderItem *`，读取同一个 double `sortKey` 并只返回 `lhs < rhs`；没有 null guard、
node-index/tie-break fallback、NaN 特判或 secondary ordering。本地旧 comparator 给 null pointer
定义了排序位置，制造了参考实现不存在的容错；本轮已删除。

## 四目标函数映射

| 目标 | prepare wrapper | content gate | recursive append | stable-sort driver | comparator |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x6D2544` | `0x6D2560` | `0x6D2578` | `0x6D25D4` / `0x6D25FC` | `PreparedRenderItem_sortKeyLess_guess` `0x6D22E0` |
| Android armv7 | `0x596DF0` | `0x596E0E` | `0x596E20` | `0x596E44` / `0x596E52` | `PreparedRenderItem_sortKeyLess_guess` `0x596C28` |
| iOS arm64 | `0x100122F68` | `0x100122F84` | `0x100122F9C` | `0x100122FF8` | `PreparedRenderItem_sortKeyLess_guess` `0x100122BF4` |
| iOS armv7 | `0x121FDC` | `0x12202E` | `0x122048` | `0x122092` | `PreparedRenderItem_sortKeyLess_guess` `0x121C06` |

Android armv7 comparator 原本只是紧邻下一函数前的匿名 Thumb code island
`loc_596C28`，没有 IDA function boundary。本轮按 `0x596C28..0x596C40` 建立真实 leaf function，
随后四端统一命名并 fresh decompile。

## content gate 字段身份

四端测试的是 persistent motion-content Variant 的 32-bit type tag：

| 目标 | type-tag 位置 |
|---|---:|
| Android arm64 | Player `+0x220` / `+544` |
| Android armv7 | Player `+0x160` / `+352` |
| iOS arm64 | Player `+0x1B0` / `+432` |
| iOS armv7 | Player `+0x124` / `+292` |

共同语义不是 object-pointer null check、`hasMotionContent()` 深层验证或 nodes-size gate，而只是
`Variant.Type() != tvtVoid`。因此 Integer/String/Object-with-null-dispatch 等任何非 Void tag 都会
进入 recursive builder；后续是否抛出由 builder 对 priority/source 等字段的实际消费决定。

Void 路径在读取 main/aux header 之前返回：

- 不 clear main；
- 不 clear aux；
- 不把既有 item 的 drawn/field 状态重置；
- 不 stable-sort预存 main；
- 返回 false。

## 共同调用链

```text
prepareRenderItems(player, callerMain, callerAux):
    if player.motionContentVariant.Type == Void:
        return false

    player.appendPreparedRenderItems(
        callerMain,
        callerAux,
        inheritedColor = 0xFF808080,
        inheritedDrawFlag19 = false,
        inheritedFlag18 = false)

    stable_sort(callerMain.begin, callerMain.end,
                PreparedRenderItem_sortKeyLess_guess)
    return true
```

wrapper 不记录 builder append 前的 begin，也不只排序“本轮新增 suffix”。如果 caller 传入已有
entries，新旧全部参加同一个 stable sort。正常 render/getCommandList callers 通常先构造空
vectors，但这是 caller 约定，不是本函数内部强制条件。

recursive builder 抛异常时 sort 不执行、bool 不返回；main/aux 以及 node/item 的已完成前缀按
各内层纵切面记录保留。builder 正常返回后，即便没有 append 任何 item，sort empty range 后
仍返回 true。

## comparator 的精确行为

四端 fresh decompile 统一为：

```text
less(lhsItem, rhsItem):
    return lhsItem.sortKey < rhsItem.sortKey
```

`sortKey` 在两个 64-bit item ABI 中位于 `+0x40`，两个 32-bit item ABI 位于 `+0x28`。
它是 builder 从 accumulated `posZ` 复制的同一 double，随后 camera projection 也消费此字段；
不是临时 sort-only key。

边界行为：

- null `lhs` 或 `rhs` 在 comparator 被调用时立即进入无效解引用；没有“null 排最后”语义；
- 相等 finite doubles 比较两向均 false，stable sort 保留其输入相对次序；
- `-0.0` 和 `+0.0` 两向均 false，属于同一 equivalent group，保持输入顺序；
- finite 与 `+/-infinity` 按普通 ordered less-than；
- 任一 operand 为 NaN 时该次 `<` 返回 false，没有把 NaN 映射成零、无穷或固定尾部。

混合 NaN 与 finite values 时 comparator 不满足 C++ stable-sort 所要求的 strict weak ordering：
例如 `finiteA ~ NaN`、`NaN ~ finiteB`，但 `finiteA < finiteB` 仍可能为 true。源代码层对此类
输入没有定义稳定全序；四端只是把同一个 raw comparator交给各自 STL，实际 permutation 可随
libstdc++/libc++ 算法、元素个数和 buffer path 不同。portable 实现不应额外“修复”NaN order，
测试也不把某一个宿主产生的 NaN 排列冒充四端共同契约。

## stable-sort 临时容器策略

四端都使用标准库 stable-sort，但 STL ABI/implementation 形状不同：

- Android arm64 先用 `std::nothrow` 尝试元素数大小的 pointer buffer，失败时反复把请求数
  除二；降到零后走无 buffer driver；
- Android armv7 通过 libstdc++ temporary-buffer helper 获取可用 pointer count，null 时走
  无 buffer driver；
- iOS arm64 在 pointer range bytes 超过 1024（128 elements）时才请求 libc++ buffer，
  否则直接以 null/zero buffer进入 driver；
- iOS armv7 对应阈值为 512 bytes，同样是 128 pointer elements。

临时 buffer 是 non-owning pointer storage，不 AddRef、不复制 item object、不改变 node owner。
普通 buffer allocation failure不会从 prepare wrapper 抛 `bad_alloc`，而是降级到 in-place/
无-buffer stable-sort path。最终 buffer 正常释放。

main vector 本身在 recursive append 阶段可能增长并抛异常；stable-sort 阶段只移动已有 raw
pointers，不再改变 size/capacity。aux 完全不进入 sort driver。

## 本地修正与测试

`PlayerRenderItems.cpp` comparator 已改为直接：

```cpp
return lhs->sortKey < rhs->sortKey;
```

删除了 `lhs && rhs` 与把非 null 排在 null 前的 portable safety branch。精确目标地址只保留
本文，compiled source 只说明 trusted raw-pointer/ordered-double 语义。

后续四端 diagnostic-isolation 复核还确认：四个 wrapper 都是 0 string references，直接
call set 中除 recursive builder 外只有各平台 stable-sort buffer/driver 与清理 helper。
本地用于输出 `sortKeysBefore` 的 `vector<double>` 不是 native stable-sort buffer；它现仅在
path-specific trace 开启时构造。motion path、排序前副本、ostringstream 和 snapshot scan
也全部处于各自 opt-in gate，普通 wrapper 仍只有 `gate -> append -> stable_sort -> true`。
完整证据见 `motionplayer_prepared_items_diagnostic_isolation_four_binary_2026-08-14.md`。

新增 test-only wrapper 未注册为 Motion.Player 脚本成员。确定性测试使用 root-only Player：

1. motion-content Void 时，预填 main 的乱序四 item 和 aux 的 `{valid, nullptr}` 完全不变，
   返回 false；
2. 把 motion-content 设为 Integer 1，证明 gate 只看非 Void tag；root-only builder 在 priority
   conversion 前早退；
3. wrapper 对全部预存 main 按 `-3, 1(first), 1(second), 5` stable-sort，两个 equal item 保留
   输入顺序；
4. aux 仍保持 `{valid, nullptr}`，证明它既不清空也不参加 comparator。

没有在 main 放 null 并强迫 comparator调用，因为四端目标行为是无效解引用，不应把进程故障
改造为 Catch2 异常。aux 中 null 是安全的，恰好证明 wrapper 从不读/sort aux elements。

## recovery IDB 改善

- 四端 comparator 统一命名 `PreparedRenderItem_sortKeyLess_guess`；
- Android armv7 建立此前缺失的 comparator function boundary；
- 四个 wrapper 的 content gate、neutral recursive append、whole-main stable sort 和
  comparator raw pointer/NaN 边界均写入语义注释。

## 验证

- 修改后的完整 motionplayer Catch2 翻译单元 Emscripten syntax-only 检查通过，仅有既有
  `_tss` literal warning；
- `cmake --build --preset "Web Debug Build"` 完成 31 个受 Player header 影响的编译/链接
  步骤并成功生成最终 Web 输出；
- scoped `git diff --check` 通过，仅报告仓库既有的 LF/CRLF 转换提示；
- 四个 recovery IDB 在 comparator 统一命名、armv7 leaf function 建立及 gate/build/sort/
  trusted-pointer 注释写回后均保存成功。
