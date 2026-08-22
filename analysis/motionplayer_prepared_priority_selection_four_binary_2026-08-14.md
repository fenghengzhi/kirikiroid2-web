# MotionPlayer prepared-item priority 选择与 dispatch 生命周期四端复原（2026-08-14）

## 结论

`Player_appendPreparedRenderItems_guess` 不按 node deque 的自然顺序生成普通 render item。
四个当前参考二进制都先把 Player 的 persistent priority-content Variant 复制成局部 owner，
从副本取得一个 **独立 AddRef 的 dispatch**，随即清掉 Variant 副本；之后每次循环通过这同一
dispatch 反向读取零基节点索引，并在完成普通构建和 stencil-composite 后处理后才 Release。

该生命周期对重入是可观察的：`PropGetByNum` 可以在第一次调用中替换或清空 Player 的
persistent priority-content Variant，但当前 builder 的剩余读取仍继续调用进入循环前保留的
原 dispatch。本地旧实现每轮重新从 Player Variant 借用对象，既不匹配对象生命期，也会在
上述重入后改变后续 receiver；本轮已恢复四端 owner。

节点选择同样是 trusted/unchecked 边界：索引 Variant 转成 32 位 `tjs_int` 后执行低 32 位
`+1`，随后直接寻址 deque。只对被选中的节点清 `drawnThisFrame`；重复索引反复清同一节点，
未出现的节点保留旧值，没有预先全量清零或修复 pass。

## 四目标映射

| 目标 | recursive builder | priority owner acquisition | indexed integer getter | loop normal release |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6BF714` | `0x6BF8BC` | `VariantObject_getIntByIndex_guess` `0x660B9C` | `0x6C0DAC` |
| Android armv7 | `0x58B178` | `0x58B1FC` | `VariantObject_getIntByIndex_guess` `0x4C7970` | `0x58BC48` |
| iOS arm64 | `0x1001148F8` | `0x10011494C` | `VariantObject_getIntByIndex_guess` `0x100069180` | `0x1001155AC` |
| iOS armv7 | `0x1123D8` | `0x112448` | `VariantObject_getIntByIndex_guess` `0xEF730` | `0x112EE4` |

低层 indexed getter 与“属性存在时读取，否则返回 caller default”的外层 helper 不是同一
函数。后三端 recovery IDB 早先只给外层 helper 命名；本轮把四端实际由 render builder
调用的低层实例统一命名为 `VariantObject_getIntByIndex_guess`，保留 `_guess`。

## persistent 字段和 ABI 对应

priority-content Variant 在四端 Player 中的位置如下，精确偏移只用于反编译对应：

| 目标 | Player priority-content Variant | node count/容器形式 |
|---|---:|---|
| Android arm64 | `+0x268` | libstdc++ deque map/iterator 差值计算 |
| Android armv7 | `+0x1A0` | libstdc++ deque map/iterator 差值计算 |
| iOS arm64 | `+0x1F8` | libc++ deque，size 见 Player `+0xC8` |
| iOS armv7 | `+0x160` | libc++ deque，size 见 Player `+0x9C` |

Android 两端虽然用 map node、block begin/end 和 magic reciprocal 组合计算逻辑 size，iOS
两端则能直接读取保存的 size，但四端在源级上都是反复查询 `nodes.size()`。它们都要求
`size >= 2` 才取得 priority dispatch；只有 root 节点时，在 packed-color 权重合成之后直接
返回，不复制、不转换 priority Variant，也不读取 draw-affine/preview 节点路径。

## dispatch 的准确 owner 时间线

四端共同顺序是：

```text
effectiveColor = multiply(inheritedColor, player.colorWeight)
if player.nodes.size < 2:
    return

priorityCopy = CopyRef(player.priorityContentVariant)
priorityDispatch = priorityCopy.AsObject()    // independently retained
priorityCopy.Clear()

... reverse indexed reads, recursive child builds, ordinary items ...
... current Player stencil-composite node-order post-pass ...

priorityDispatch.Release()
```

Android arm64 在 type tag 为 object 时直接从 Variant 副本取 dispatch 并调用其 AddRef；其他
type 进入 Variant 对象转换异常。另三端通过各自 ABI helper 表达相同的 CopyRef → AsObject
→ Clear。正常退出处均在 stencil 后处理之后；四端 landing pad/SjLj cleanup 也都释放同一
dispatch owner。

因此：

- non-object priority Variant 在至少有一个非 root node 时，于任何节点
  `drawnThisFrame` 修改之前抛转换异常；
- root-only Player 不触发该转换；
- persistent Variant 在 getter 内被清空，不会释放当前 builder 的最后一个对象引用；
- getter 重入替换 persistent Variant，不会让后续迭代切换到新 dispatch；
- builder 正常/异常退出均释放 retained dispatch，不把它保存到 prepared item 或输出 list。

## 反向读取和 unchecked node 选择

设每轮开始重新读取的 node count 为 `N`，逻辑循环变量从 1 开始。共同算法是：

```text
logicalIndex = 1
while logicalIndex < current nodes.size:
    N = current nodes.size
    priorityPosition = N - logicalIndex - 1

    value = Void Variant
    status = priorityDispatch.PropGetByNum(
        flags = 0,
        index = priorityPosition,
        result = &value,
        objthis = priorityDispatch)
    ignore(status)
    raw = low32(value.AsInteger())
    selected = low32_wrap(raw + 1)

    node = unchecked nodes[selected]
    node.drawnThisFrame = 0
    ... process selected node ...

    logicalIndex += 1
```

正常 `N` 个节点（含 root）会依次请求 priority 位置 `N-2, N-3, ..., 0`。priority 数据存的
是零基非 root layer index，因此合法值加一后映射到 node deque 的 `1..N-1`。

四端 getter 都先构造 Void result Variant，调用 dispatch 的 numeric getter，忽略返回码，
然后才 `AsInteger` 并析构 result。因此：

- 失败返回码但同时写入 result 时，写入值仍被使用；
- 失败且未写 result 时，Void 的整数转换结果进入后续路径（当前 TJS 行为为 0，于是选
  node 1）；
- String/Real 等按普通 Variant integer conversion 处理，可能转换或抛异常；
- result Variant 在每轮 conversion 后释放，不能跨轮保存 getter 返回对象。

四端 `+1` 都是 W/R 32 位机器加法，溢出按低 32 位回绕。本地现显式使用 unsigned 32 位
加法后还原 `tjs_int`，避免把 `INT32_MAX + 1` 留成 C++ signed-overflow UB。之后的 deque
选择没有范围检查：

- raw `-1` 加一得到 0，可合法选中 root；
- 重复值会重复处理同一个 node；
- 缺失的 node 不会被访问，也不会被统一清 drawn byte；
- 其他负值、过大值或溢出后的负值进入越界 deque 寻址，原生没有 throw/skip/clamp
  保护，其最终故障细节受 ABI、内存布局与编译器生成寻址影响。

## 重入时的 node-container 边界

priority position 在 getter 调用前用当轮 node count 计算；getter 返回后，四端从 Player 的
live deque header 寻址 selected node。循环尾又重新读取 node count 决定是否继续。因此若
恶意 getter 重入修改 node deque，原生不是对 deque 或 size 做快照：当轮 position 属于调用
前 size，而实际寻址和后续 loop limit 观察调用后的 live container。普通游戏数据不会这样
做，但这是 owner snapshot 与 container snapshot 必须区分的边界。

## 本地修正

`cpp/plugins/motionplayer/PlayerRenderItems.cpp` 已进行以下四端支持的调整：

1. 保留 color-weight 合成在最前，随后新增 `nodes.size() < 2` 的原生早退；
2. `CopyRef` `_rootContentVariant`，从副本 `AsObject()` 得到独立 retained dispatch，再清副本；
3. 每轮通过该 dispatch 做 flags 0、same-objthis 的 `PropGetByNum`，忽略 HRESULT；
4. 将零基索引的 `+1` 写成确定的 32 位回绕；
5. retained dispatch 活到整个 builder（含 stencil 后处理）退出，由 RAII 正常/异常 Release；
6. 删除源码中只适用于旧单端布局的 `Player+616` 注释，改为源级 owner/顺序说明；
7. 将本文件原先仅服务 calcBounds 的 retained-dispatch deleter 泛化为同一局部 RAII 类型。

`Player.h` 新增的 `setRootContentForDifferentialTest_guess` 只供测试注入 persistent owner，未
注册为 Motion.Player 脚本成员，不改变插件公开脚本面。

## 测试覆盖

新增测试使用自定义 numeric dispatch 验证：

- 首次 `PropGetByNum` 重入清空 Player persistent priority Variant 后，builder 仍完成全部
  降序位置读取；
- receiver 与 objthis 是同一个 retained dispatch，flags 恒为 0；
- identity priority 覆盖全部非 root node，逐个清 drawn byte；
- 全重复 priority 只反复清 node 1，其他 node 的旧 drawn byte 保留；
- getter 返回 `TJS_E_MEMBERNOTFOUND` 但写入 `-1` 时，错误码被忽略且 root 被选中；
- 外部最后一个测试 owner 清除后 dispatch 析构一次，没有 builder owner 泄漏。

越界值不在单元测试进程内主动触发；该路径的目标是保持 unchecked native 边界，而不是把
预期内存错误转化为稳定的测试异常。

## recovery IDB 改善

四个 recovery IDB 都已在以下位置写入语义注释：

- color/size gate 后的 priority Variant CopyRef → AsObject → Clear；
- 反向 position、ignored-HRESULT numeric getter、low-32 `+1` 和 unchecked selection；
- selected-only `drawnThisFrame` clear 的 duplicate/omitted 行为；
- 普通退出及异常 cleanup 的 retained dispatch Release。

四个低层 indexed getter 也已统一命名为 `VariantObject_getIntByIndex_guess`。精确地址只保留
在本文，不再进入可编译源码注释。

## 验证

- motionplayer 单元测试翻译单元 Emscripten 语法检查通过；
- `cmake --build --preset "Web Debug Build"` 完成 31 个增量编译/链接步骤并成功生成 Web
  输出；
- `git diff --check` 通过，仅有仓库既有的 LF/CRLF 转换提示；
- 四个 recovery IDB 在 helper 命名及 owner/loop/cleanup 注释写回后保存成功。

## V236 后续闭环（2026-08-18）

本报告已证明 selected-only clear、duplicate/omitted byte 和 live deque，但尚未把这些状态与同一
builder 尾部 type-12 post-pass 连成可执行矩阵。V236 四端复核确认：duplicate 每轮先 clear，final
byte 等于最后一次 admission，而 earlier main pointer 不回滚；omitted node 保留 entry stale byte。
post-pass 按自然 node 顺序直接消费 final live byte，不检查 priority membership 或 main membership。
新增 test-only getter hook 锁定 omitted stale-true self seed，以及 duplicate early-success/last-failure
产生 retained main duplicates 但无 aux/post-pass 的两类结果。完整证据见
`motionplayer_prepared_priority_duplicate_final_drawn_stencil_four_binary_2026-08-18.md`。
