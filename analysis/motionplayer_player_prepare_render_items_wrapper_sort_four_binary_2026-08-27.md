# Player prepared-item wrapper 与 stable-sort owner/EH 四参考二进制联合恢复

日期：2026-08-27

## 1. 结论

四端 `Player::prepareRenderItems` 具有同一份很窄的源级职责：检查 motion-content
type tag，以中性继承色和两个 false lineage flag 调用递归 prepared-item builder，只对
main pointer-vector 做稳定排序，然后返回成功。auxList 只传给 builder，不参与排序。

本地核心 gate、参数、比较器和返回值已经匹配；生产路径中额外存在的 logo path 查询、
第二份 sort-key vector、字符串流格式化、日志和特定 motion/frame 快照输出不在任何参考端中。
本轮将这组 sidecar 从 wrapper 删除，保留仅在 `KRKR2_WASMTIME_HEADLESS` 测试构建中启用的
differential trace envelope。

本项闭合 wrapper、递归 builder 的调用边界以及 stable-sort 临时缓冲 owner/EH；
`Player::appendPreparedRenderItems` 的深层递归主体随后由
`MP-G11-PLAYER-APPEND-PREPARED-ITEMS`独立闭合。

## 2. 四端函数、比较器与完整指令

| 平台 | wrapper | wrapper 指令 | comparator | comparator 指令 | cleanup |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x6D2544` | 61 | `0x6D22E0` | 5 | wrapper 内 landing `0x6D2628` |
| Android armv7 | `0x596DF0` | 58 | `0x596C28` | 8 | 无独立 landing |
| iOS arm64 | `0x100122F68` | 45 | `0x100122BF4` | 5 | 无独立 landing |
| iOS armv7 | `0x121FDC` | 79 | `0x121C06` | 8 | `0x1220C0`, 14 指令 |

四个 wrapper、四个 comparator 和 armv7 SjLj cleanup 均做了 fresh decompile；上述
243 + 26 + 14 = 283 条指令全部完整读取。Android armv7 comparator 原本只是前一函数尾部之后的
内部入口，本轮按精确 `0x596C28..0x596C40` 边界定义为独立函数后重新反编译，避免把后续约
2800 条无关 listing 错算进比较器。

递归 builder 调用目标为：

| 平台 | builder call target |
|---|---:|
| Android arm64 | `0x6BF714` |
| Android armv7 | `0x58B178` |
| iOS arm64 | `0x1001148F8` |
| iOS armv7 | `0x1123D8` |

四个 IDB 中 wrapper、builder call boundary、comparator 与 armv7 cleanup 已命名、注释、
bookmark 并保存。builder 名称只表达已验证的调用身份，不代表其深层主体已经完成审计。

## 3. 精确共同源形状

去除标准库实现细节后，四端共同伪代码是：

```text
bool Player::prepareRenderItems(mainList, auxList) {
    if (motionContentTypeTag == 0)
        return false

    appendPreparedRenderItems(
        mainList, auxList,
        0xFF808080, false, false)

    stable_sort(mainList.begin, mainList.end,
        (lhs, rhs) => lhs->sortKey < rhs->sortKey)
    return true
}
```

tag 为零时不调用 builder、不读取 list 元数据，也不清空或修改任一输入 list。tag 非零时，
即使 builder 没有向 mainList 追加元素，wrapper 仍经过 stable-sort 空区间并返回 true。
wrapper 自身也不要求 list 初始为空：若私有调用者传入已有元素，mainList 的既有元素会和新追加
元素一起排序，auxList 的既有元素保留。当前已审计的 direct D3D/canvas callers 都在调用前
default-construct 两个空 vector，但这不是 wrapper 内部强制的契约。

继承色按源级 32-bit bit pattern `0xFF808080` 传递；32 位反编译显示为 signed
`-8355712`，只是 ABI 表示差异。两个 lineage 参数均为整数零/false，没有读取 Player 上的
替代默认值。

## 4. comparator、稳定性与浮点边界

64 位端从 item 的 `+0x40` 读取 sortKey；32 位端从 `+0x28` 读取。结构偏移不同，比较语义完全
相同：

```text
return lhs->sortKey < rhs->sortKey
```

ARM `FCMP/VCMPE` 后使用 minus 条件置 bool；unordered/NaN 的 N flag 不成立，因此单次比较返回
false。不存在 null gate、secondary key、node index tie-break、total-order transform 或 epsilon。

- 两个相等有限值、相等 infinity 以及 `+0.0/-0.0` 属于等价元素，stable-sort 保留输入相对次序；
- 任一 pointer 为 null 时，比较器立即解引用并 fault；
- 含 NaN 的 `<` 不满足 `std::stable_sort` 所要求的 strict-weak-order 等价传递性。参考程序没有
  修复这个前置条件，因此 NaN sortKey 属于标准库/优化器相关 sharp boundary，不能承诺四端输出
  顺序一致；
- comparator 没有可抛操作，正常对象域内只做两次 load、一次浮点比较和 bool materialization。

## 5. stable-sort 临时容器的四端实现差异

这些差异来自各端随 binary 链接/内联的标准库模板，并非四份不同 motionplayer 算法。

### Android arm64 / libstdc++

wrapper 自己计算 pointer count，并用 nothrow `operator new` 尝试分配尽可能大的临时 pointer
buffer。失败时把容量反复减半，直到成功或降为零；零容量走无缓冲 stable-sort helper，非零走
带缓冲 helper。成功和普通无缓冲路径最终都调用 matching nothrow delete，null delete 也允许。
计数还被标准库 `max_size` 上限夹住；畸形 vector 的 begin/end 差值并未由 motionplayer 校验。

若排序 helper 展开异常，wrapper 内 landing 保存异常对象，释放临时 buffer，再调用
`_Unwind_Resume`。builder 抛异常发生在 buffer 尚未创建之前，直接向外传播，两个输入 list 中
builder 已完成的追加不回滚。

### Android armv7 / libstdc++

wrapper 把 pointer count 交给 adaptive temporary-buffer helper。helper 返回 pointer 与实际容量；
pointer 非空走 buffered stable-sort，空则走 in-place/no-buffer helper，之后用 matching nothrow
delete。wrapper 没有独立 EH landing：分配是 nothrow，比较器与 pointer move 不抛；参考源码没有
额外 transaction 或 cleanup object 可恢复 list 内容。

### iOS arm64 / libc++

只有连续区间字节数至少 1025，即至少 129 个 8-byte pointer 时才请求临时 buffer；128 个及以下
直接把 null/zero buffer 交给 libc++ stable-sort helper。大区间 allocation helper返回 pointer 与
容量，排序后由 wrapper 普通 `operator delete`。函数没有独立 landing；在该具体实例中
comparator 和 pointer move 都不抛，builder 则发生在 buffer 分配之前。

### iOS armv7 / libc++ + SjLj

阈值同样是 128 个元素：字节差大于 512 才请求 buffer。wrapper 在 stable-sort call-site 前把
SjLj selector 设为 1；独立 cleanup `0x1220C0` 只在 buffer 非空时 ordinary-delete，然后恢复
selector 并 `Unwind_SjLj_Resume`。非法 landing selector 进入 trap/abort 边。builder 的 call-site
仍是无 cleanup 的 `-1`，因为此时 buffer 尚不存在。

上述临时 buffer 只存放 `PreparedRenderItem*`，不拥有 item，也不触碰 auxList。stable-sort 的
中间写入不是事务性的；若超出正常 comparator 域发生 fault/未定义行为，wrapper没有顺序回滚。

## 6. 数据流与对象生命周期

wrapper 借用 Player、mainList、auxList 和 list 中的 raw item pointers。它不创建/销毁
PreparedRenderItem；item 的延迟分配与 Player node cache 归属在递归 builder 中。排序只重排
mainList 的 pointer slots，不更改 item 内的 parent/child/source/geometry 字段，也不修改 auxList。

正常成功数据流为：

```text
Player motion-content tag
    -> recursive builder mutates Player node caches + appends main/aux pointers
    -> stable-sort reorders main pointer slots by item.sortKey
    -> projection consumes sorted mainList
    -> D3D/canvas renderer consumes projected mainList
```

false 返回时 callers 直接销毁两个空/原样 pointer-vector；true 返回后的外层 coordinator 对
prepare、projection、renderer 异常负责销毁 vector 连续存储，但 vector 永不 delete 指向的 item。

## 7. 本地偏差与本轮恢复

原本 wrapper 在 builder 前后额外执行：

1. logo trace/snapshot 全局 gate 与 matched motion path 查询；
2. path filter；
3. 按 mainList 大小分配并填充第二个 `vector<double>`；
4. 两个 `ostringstream` 的排序前后格式化和日志发布；
5. 特定 `m2logo.mtn`、frame range、node index 的 stderr 快照遍历。

四个 native wrapper 均无这些调用、分配、null 容错和异常点。sidecar 会改变 builder 成功后的
最先分配/最先抛异常位置，还会为 null item 添加参考比较器不存在的条件分支。本轮删除了整组
生产路径 sidecar，并移除不再需要的 `<optional>` include。headless-only trace 保留为差分测试
instrumentation，不进入正常 plugin/Web source path 的参考语义结论。

本地核心现在逐行对应 gate、六参数 builder call、main-only `std::stable_sort` 和 true return。

## 8. 验证与剩余范围

本轮做了四端 fresh decompile/full disassembly、比较器内部入口修复、armv7 SjLj cleanup 审计、
本地逐行对照和 `git diff --check`。正式 CMake/unit/Web build 仍因当前环境缺少 CMake、Ninja、
Emscripten 且不存在既有 build/out 而不能运行，不能把静态检查表述为正式测试通过。

四端 `Player::appendPreparedRenderItems` 深层递归主体已由
`analysis/motionplayer_player_append_prepared_render_items_four_binary_2026-08-27.md`闭合，包括
node分类、ancestor/color/stencil lineage、PreparedRenderItem延迟分配与复用、main/aux增长、
child flattening和partial-commit边。MotionNode/PreparedRenderItem最终析构、deque后缀erase、
Player显式root clear与唯一owner释放链随后由
`analysis/motionplayer_motionnode_prepared_item_deque_lifetime_four_binary_2026-08-27.md`闭合。
