# MotionPlayer pending-event 完整生命周期四参考二进制记录（2026-08-13）

## 结论

四个当前参考二进制共同证明：Player 只有一个持久
`std::vector<MotionEvent>`。本地此前的 `_pendingEvents` 与
`_childMotionRenderItems_guess` 不是两个源字段；所谓 child residual/render-item
聚合实际把 child 的同一 pending-event vector 插到 parent 的开头，然后 clear child。
旧注释和 IDB `ChildMotionRenderItem*` 名称已被证伪并同步纠正。

元素共同源形状为：

```cpp
struct MotionEvent {
    int type;
    tTJSVariant param1;
    tTJSVariant param2;
};
```

`type == 0` 对应 `onAction(param1, param2)`；`type == 1` 对应 `onSync()`；
其它值由 dispatcher 跳过。

## 四端映射与布局

| 角色 | Android arm64-v8a | Android armabi-v7a | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| enqueue sync | `0x6B3674` | `0x582674` | `0x10010A3A4` | `0x107C98` |
| enqueue action | `0x6B376C` | `0x582740` | `0x10010A47C` | `0x107DB0` |
| dispatch | `0x6C1870` | `0x58C3A8` | `0x10011622C` | `0x113B64` |
| parent prepend + child clear | `0x6BE8B4`；两个主调用点另有内联体 | `0x58A952` | `0x100113E64` | `0x11186C` |
| vector range insert | `0x6F0A1C` | `0x5AE268` | `0x100115A70` | `0x11337C` |
| erase-at-end helper | 内联 | `0x5AE568` | wrapper 内联 | wrapper 内联 |
| Player constructor | `0x6CC110` | `0x5935C4` | `0x10011EC04` | `0x11D488` |

| 目标 | Player vector 偏移 | 元素步幅 | type | param1 | param2 |
|---|---:|---:|---:|---:|---:|
| Android arm64-v8a | `+0x3A8` | 44 | `+0` | `+4` / 20B | `+24` / 20B |
| Android armabi-v7a | `+0x290` | 28 | `+0` | `+4` / 12B | `+16` / 12B |
| iOS arm64 | `+0x338` | 44 | `+0` | `+4` / 20B | `+24` / 20B |
| iOS armv7 | `+0x250` | 28 | `+0` | `+4` / 12B | `+16` / 12B |

构造函数在各自偏移把 begin/end/end-cap 三指针清零；enqueue、child aggregate、
dispatcher 和 Player 析构均引用这同一偏移，没有第二个相同形状 vector 的构造或
析构。64/32 位步幅差异来自 `tTJSVariant` ABI，不是源码差异。

## 生产调用链穷举

四库 xref 数量完全一致：sync helper 各有 3 个 caller，action helper 各有 6 个
caller。caller 角色和逐目标入口如下：

| caller 角色 | Android arm64-v8a | Android armabi-v7a | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| absolute node timeline init（action 1 处） | `0x6B388C` | `0x5827D8` | `0x10010A57C` | `0x107EE8` |
| forward variable/layer tracks（sync 1、action 2 处） | `0x6B3EBC` | `0x582BE0` | `0x10010AA08` | `0x1083D8` |
| full reseek（sync 1、action 1 处） | `0x6B5AA8` | `0x583C8C` | `0x10010C3CC` | `0x109DAC` |
| rewind variable/layer tracks（sync 1、action 2 处） | `0x6B6E1C` | `0x584838` | `0x10010D230` | `0x10AB68` |

因此生产点不是仅由某一个 layer loop 偶然写入：absolute seed、forward、full reseek、
rewind 四条时间线链共同复用 action helper，而 sync 出现在后三条 layer/variable-track
扫描中。portable 实现已把这些原有的直接 `push_back` 全部路由到统一 enqueue helper。

## 共同控制流

```text
enqueueSync(player):
    temporary = MotionEvent(1, Void, Void)
    player.events.push_back(copy(temporary))
    destroy temporary.param2, temporary.param1

enqueueAction(player, param1, actionString):
    temporary.type = 0
    temporary.param1 = copy(param1)
    temporary.param2 = String(actionString)  // retains string owner
    player.events.push_back(copy(temporary))
    destroy temporary.param2, temporary.param1

aggregate(parent, child):
    parent.events.insert(parent.events.begin,
                         child.events.begin, child.events.end)
    child.events.clear()  // destroy param2 then param1; keep capacity

dispatch(player, dispatch):
    if player.events.begin == player.events.end:
        return
    dispatch.AddRef()     // unconditional: null is a native crash boundary
    result = Void
    cursor = player.events.begin
    while cursor != player.events.end:  // reload live end every iteration
        if cursor.type == 0:
            p1 = copy(cursor.param1)
            p2 = copy(cursor.param2)
            dispatch.FuncCall("onAction", result, [p1, p2], dispatch)
            destroy p2, p1
        else if cursor.type == 1:
            dispatch.FuncCall("onSync", result, [], dispatch)
        ++cursor
    destroy result
    dispatch.Release()
```

四端差异限于 STL 实例化/内联与异常实现：Android arm64 的两个实际 child/particle
路径内联 prepend/clear，但同库还存在等体 outlined helper；Android armv7 单独拆出
erase-at-end；iOS 两端在 wrapper 内联析构循环。共同源码级容器操作不变。

## 顺序、重复派发与重入边界

- 本地 layer timeline action 的 `param1` 是 Void，`param2` 是 action string；node
  timeline action 的 `param1` 是 node label，`param2` 是 slot action string。
- child 整段插在 `parent.begin()`。连续聚合多个 child 时，后访问 child 的整段历史
  会位于更早 child 与 parent 原有事件之前；这不是 append。
- child clear 后不再自行重复这些已转移事件，但 parent 永不消费，故 root 每次
  progress 都会重新派发累计历史。
- dispatcher 保存 raw cursor，却在每轮重新读取 vector 的当前 end。回调在 capacity
  内追加事件时，新事件会在本轮继续被看到；若追加触发 reallocation，cursor 失效，
  保留参考实现的未定义/崩溃边界，不能改为 index 遍历来“修复”。
- action 参数在回调前复制到两个局部 Variant，所以回调看到的是独立临时 owner；
  callback-result Variant 在整轮中只构造一次并复用。
- callback 异常展开会销毁当前 action 临时、result 并 Release retained dispatch；但更
  外层 progress bridge 的 `_currentDispatch = nullptr` 不是 RAII，异常时不会执行。

## 本地源码对照

- `RuntimeSupport.h`：删除被证伪的 `ChildMotionRenderItem_guess`，event-specific helper
  直接接收两个 `std::vector<MotionEvent>`。
- `Player.h`：删除第二个 `_childMotionRenderItems_guess`；唯一 `_pendingEvents` 同时
  承担本地生产、child transfer 与 dispatch。
- `PlayerFrameProgress.cpp`：恢复独立 sync/action enqueue helper；dispatcher 使用 raw
  cursor + live-end 条件，不 clear，并用局部析构守卫保持异常路径 Release。
- `PlayerUpdateLayerEval.cpp`：node timeline helper 接收 event-owning Player，调用统一
  enqueue action helper，而不是直接写另一个 vector sink。
- `PlayerUpdateChildMotion.cpp` / `PlayerUpdateParticles.cpp`：统一调用
  `aggregateChildPendingEvents_guess`。
- Catch2：覆盖 child prepend + clear-retaining-capacity，以及 capacity 内重入 append
  当轮可见和重复 dispatch 不消费。

## IDB 改进

四库已把旧名称纠正为：

- `Player_prependAndClearChildPendingEvents_guess`；
- `MotionEventVector_prependRange_guess`；
- Android armv7 `MotionEventVector_eraseAtEnd_guess`。

enqueue、aggregate、range-insert、dispatch 均添加统一事件字段注释；强制刷新后 fresh
decompile 已显示新调用链。精确原始 C++ 名字无二进制字面证据，故继续保留 `_guess`。

## 验证

- `cmake --build out/web/debug --parallel`：完整 Web 链接成功；二次复核
  `ninja: no work to do`。
- `cmake --build out/wasmtime/debug --target krkr2_wasmtime_guest --parallel`：guest
  链接及 exnref/strip 后处理完成；二次复核 `ninja: no work to do`。
- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` 使用 Web Debug 的真实 Emscripten
  定义/头路径与 `out/syntax-check` Catch2 头执行 `-fsyntax-only`：通过；唯一诊断为
  仓库既有 `_tss` literal-operator deprecation warning。
- `git diff --check`：通过；仅报告工作树既有 LF/CRLF 提示。

当前配置没有可直接运行的 Catch2 motionplayer executable，因此这里只声明新增 case
已经随完整测试翻译单元通过编译，不把语法编译误报为运行时执行。
