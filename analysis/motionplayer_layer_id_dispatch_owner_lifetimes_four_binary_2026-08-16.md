# MotionPlayer layer-id dispatch 所有权与回调生命周期（四参考二进制，2026-08-16）

> V163 follow-up：本文 owner-scope 结论保持有效；旧树 reset 的 member-hint 现已由四端相邻
> 全局序列进一步闭合，并从 TU-local storage 迁到
> `motion::detail::releaseLayerIdMemberHint_guess`。它后接 `window/piledCopy`。详见
> `analysis/motionplayer_old_node_reset_release_window_piled_hint_sequence_four_binary_2026-08-16.md`。
>
> 2026-08-18 V237 follow-up：本文的 retained-owner 范围继续成立；common builder 与 reset
> 之间的 item latch 生命周期现已完整闭合。`rawFlag20=false` 时 `renderLayerId` 是 dormant/stale
> 槽，clip success 先于 allocation 提交；普通 require failure 会把 Void 转 0 并 latch，exception
> 则不 latch。reset release 前不清 flag/ID，throw 会阻止 suffix erase，因此 retry 可再次释放。
> Android armv7 的 flag-before-ID 只是两条相邻、无 throwing point 的指令调度，不是不同的 C++
> partial-commit。详见
> `analysis/motionplayer_render_layer_id_latch_persistence_release_four_binary_2026-08-18.md`。

## 结论

四份参考二进制的 layer-id 调用不是从 Player 的 ResourceManager Variant 裸借用 dispatch。所有生产调用点都先建立一个独立的 Object 引用，而且其持有范围分成两类：

- 通用 render-command builder 与 private-GLL builder：每次 `requireLayerId` 调用各复制一次 ResourceManager Variant、以 `AsObject()` 增加独立 Object 引用、在回调前析构临时 Variant，并在结果转换/析构后 Release Object；
- node-tree builder 与旧树 reset：分别在整个函数/递归层或整个 teardown 循环外只保有一次 dispatch，跨越其内部的所有 getter、子对象回调和 layer-id 调用，最后在正常返回或异常展开时 Release。

当前端口的 node-tree 与 reset 已经复原了第二类范围；偏差位于 `Player::dispatchRequireLayerId` / `dispatchReleaseLayerId`：它们使用 `AsObjectNoAddRef()` 直接借用 Player 字段。该借用在 re-entrant TJS 回调期间没有独立所有权，也不符合 render 两个 builder 的四份反编译。

## 字符串与生产 xref 集合

对 ASCII、UTF-16LE、UTF-32LE 三种编码重新搜索后，每份二进制都恰有一份 UTF-16LE `requireLayerId` 和一份 UTF-16LE `releaseLayerId`，ASCII/UTF-32LE 均为零：

| 平台 | `requireLayerId` UTF-16LE | `releaseLayerId` UTF-16LE |
|---|---:|---:|
| Android arm64 | `0x14D5A54` | `0x14D5A72` |
| Android armv7 | `0xD85574` | `0xD85592` |
| iOS arm64 | `0x10195BF02` | `0x10195BF20` |
| iOS armv7 | `0x174E266` | `0x174E284` |

去除 NCB 注册表引用后，`requireLayerId` 的生产函数集合在四个平台完全一致：

| 平台 | node-tree builder | 通用 render builder | private-GLL builder |
|---|---:|---:|---:|
| Android arm64 | `0x6B1E4C` | `0x6C2208` | `0x6DBB18` |
| Android armv7 | `0x5818B0` | `0x58C7C4` | `0x59CB20` |
| iOS arm64 | `0x100109328` | `0x1001167BC` | `0x10012B7D0` |
| iOS armv7 | `0x106BDC` | `0x114118` | `0x12A304` |

`releaseLayerId` 的唯一生产函数则是旧 node tree reset：

| 平台 | reset function |
|---|---:|
| Android arm64 | `0x6B2AD8` |
| Android armv7 | `0x581F3C` |
| iOS arm64 | `0x100109ACC` |
| iOS armv7 | `0x107358` |

iOS listing 中若干 UTF-16 引用仍被 IDA 渲染为截断的 `"r"`；上述原始 byte 搜索与 data xref 集合排除了把它误判成一字节成员名的可能。

## render builder：每次调用独立保有

通用 builder 和 private-GLL builder 在四个平台均重复同一顺序：

```text
ResourceManager Variant copy-construct
dispatch = copy.AsObject()        // 独立 AddRef
destroy/clear copy                // 回调前释放 closure 的临时 Object/ObjThis owners
result = Void
dispatch.FuncCall(
    flags=0,
    member="requireLayerId",
    argc=0,
    argv=null,
    objthis=dispatch)
id = result.AsInteger()
write id, then latch rawFlag20
destroy result
dispatch.Release()
```

代表性指令边界：

| 平台 | 通用 builder copy/call/release | private builder copy/call/release |
|---|---|---|
| Android arm64 | call 区 `0x6C2584`，其前完成 copy/AsObject/dtor，其后显式 Release | copy `0x6DC4C0`，call 区 `0x6DC510`，随后显式 Release |
| Android armv7 | copy `0x58C9AC`，call `0x58C9DE`，Release `0x58C9FC` | copy `0x59D26C`，call `0x59D29E`，Release `0x59D2BE` |
| iOS arm64 | copy `0x1001169B0`，call `0x1001169FC`，Release `0x100116A28` | copy `0x10012BAB0`，call `0x10012BAFC`，Release `0x10012BB24` |
| iOS armv7 | AsObject/dtor 区 `0x1144E8`，call `0x11452C`，Release `0x11455C` | AsObject/dtor 区 `0x12A6E0`，call `0x12A71C`，Release `0x12A744` |

临时 Variant 在 FuncCall 前已经死亡，因此原 closure 的 `ObjThis` 不跨回调保留。`AsObject()` 返回并 AddRef 的 Object 被同时用作 receiver 与 `objthis`。若 Player 字段在回调中被替换、清空，当前调用仍由该独立引用保护；若 FuncCall、结果转换或后续写入抛出，异常清理路径同样 Release 它。

这也解释了为什么只把 `AsObjectNoAddRef()` 换成 `AsObject()` 而不加 RAII 仍不完整：异常路径必须和正常尾部一样释放。

## node-tree builder：每个递归层一次保有

node-tree builder 在进入 layer 循环前复制其 ResourceManager Variant 参数、调用 `AsObject()`、析构临时 Variant，然后把 retained dispatch 保存在 callee-saved register/局部 owner 中。一个递归层的所有 node 共用它：

- 每个 node 连续两次无参 `requireLayerId` 共用同一 member-hint 与 dispatch；
- 随后的 raw-field getter、children getter 与递归子调用期间，父层 dispatch 仍然存活；
- 子递归层从传入 Variant 再建立自己的独立 owner；
- 本层返回/异常展开时才 Release。

端口 `NodeTree.cpp` 的 `RetainedDispatch resourceManager` 已符合此范围，没有改成逐调用 helper。

2026-08-16 follow-up：同一 recursive walker 中的 layers 和逐层对象已进一步恢复为真实
`ncbPropAccessor`，但 ResourceManager 仍保持本节描述的 raw retained dispatch。四端在对象
布局上明确区分这两条 owner 路径；不能因为 layer getter 使用 accessor 就把 RM 一并改成
accessor。详见
`analysis/motionplayer_node_tree_ncb_accessor_source_identity_four_binary_2026-08-16.md`。

## reset：整个 teardown 一次保有

四份 reset 都在任何 child Invalidate 之前复制 Player ResourceManager Variant并取得独立 Object。它跨越：

1. type-3/type-4 owned Player 的 re-entrant Invalidate；
2. HM1 live value/cache reset；
3. 每个非 root node 的 `layerId1`、`layerId2`；
4. prepared item latch 为真时的 active render layer id；
5. 直到 release 循环结束。

每次 `releaseLayerId` 都传一个 Integer Variant 参数并复用同一 dispatch 与 member hint。端口
`PlayerMotionLoad.cpp` 的函数外层 retained dispatch 保持不变；V163 只删除了匿名 namespace 的
重复 hint，并接入精确进程级全局槽。

## 源码修正

`cpp/plugins/motionplayer/PlayerResource.cpp`：

- 新增异常安全的 retained-dispatch RAII owner；
- `dispatchRequireLayerId` 与 `dispatchReleaseLayerId` 现在都执行 Variant copy → `AsObject()` → Clear temporary；
- FuncCall 继续把 retained Object 同时作为 receiver 与 `objthis`；
- 正常返回和异常展开都会 Release Object；
- callsite 自己提供的 hint storage 仍然保留，private-GLL 与通用 builder 不会错误共享同一个 native member-hint slot。

`tests/unit-tests/plugins/motionplayer-dll.cpp` 新增引用计数与调用边界回归：

- closure 的 Object/ObjThis 指向同一 probe 时，复制 closure 产生两次 AddRef，`AsObject()` 再产生一次；
- FuncCall 入口已看到 `+3 AddRef / +2 Release`，说明临时 closure 已清而独立 Object owner 仍在；
- FuncCall 返回后为 `+3 / +3`，说明 retained Object 正常释放；
- require 为零参数并返回 Integer，release 为单 Integer 参数；
- receiver 对象被作为 `objthis` 传回。

## IDB 更新

四份 recovery IDB 的 node builder、通用 builder、private builder 与 reset 均已追加各自 owner scope 注释，并保存到 `out/ida-recovery/` 对应平台目录。

## 验证

- ordinary Emscripten 单元测试翻译单元语法检查：通过，只有既有 `_tss` literal-operator 弃用警告；
- `KRKR2_WASMTIME_HEADLESS=1` 同一翻译单元语法检查：通过，同一既有警告；
- Web Debug `motionplayer` archive：`2/2` 通过；
- Wasmtime Headless Debug `motionplayer` archive：`2/2` 通过；
- 两套 archive 只出现既有 `_tss` 与 `imagepacker.h` 错置 `nodiscard` 警告；
- Web Debug 完整目标：`1/1` 链接通过；只出现既有 pthread/memory-growth、JSPI 与 Emscripten JS library 警告。
