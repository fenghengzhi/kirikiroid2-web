# `Player_initNonEmoteMotion_guess` 四端共同实现（2026-08-12）

## 范围与入口

本轮只以 `reference/binaries/` 的四个当前参考二进制为依据，对普通 motion 初始化器做
新鲜反编译与源码对照：

| 参考二进制 | 入口 | 大小 |
|---|---:|---:|
| Android ARM64 | `0x6B0A3C` | `0x61C` |
| Android ARMv7 | `0x580C28` | `0x24E` |
| iOS ARM64 | `0x100108258` | `0x31C` |
| iOS ARMv7 | `0x1058F8` | `0x348` |

四端虽然内联程度、临时对象清理形态与 ARM32/ARM64 布局不同，但可见的数据流、提交
顺序和异常边界一致。

## 共同数据流

四端共同伪代码为：

```text
initNonEmoteMotion(playFlags):
    loopTime       = motionContent.loopTime
    lastTime       = motionContent.lastTime
    tagOwner       = motionContent.tag
    priorityOwner  = motionContent.priority
    rootOwner      = priorityOwner[0].content

    nodeLabelMap.clear()
    parameterEntries.clear()
    // ramp multimap 不在这里整体 clear；旧 entry 的精确指针由后续
    // finalize/destructor purge 维护

    parameterize = motionContent.parameterize
    if parameterize is Object:
        appendParameterEntry(parameterize)
        finalizeParameterTable()
        if parameterEntries is not empty:
            selectedParameterEntry = &parameterEntries[0]
    else:
        parseParameterList(motionContent.parameter)
        if parameterize is Integer:
            selectedParameterEntry = selectParameterEntry(parameterize)
        else:
            selectedParameterEntry = null

    syncWaiting = false
    allplaying = true
    buildNodeTree()
    initVariables()

    if !(playFlags & Chain):
        frameTickCount = 0
        clampedEvalTime = min(0, lastTime)
        queuing = true
    firstFrame = true
```

重要边界：前五个 Variant owner 全部取得以后才清 node-label map 与 parameter vector；旧
node deque 仍由 `buildNodeTree()` 自己进入 teardown。这里也不会整体清 ramp multimap：
其 key 可同时指向本 Player、兄弟与后代 Player 的 entry，旧 entry 的精确指针由
parameter pipeline 的 finalize/destructor purge 维护。object `parameterize` 分支只有在
append 产生记录后才改 selected pointer。integer 分支按 unsigned 边界选择，并以
`parameter id out of range.` 抛错。这些结论与 parameter-pipeline 专项记录一致；旧版
“parameter ramp multimap 不清”已迁移为该专项完成后的容器语义。

### 2026-08-16 V147 fresh addendum：nested ncb source identity

本节原先用`motionContent.priority[0].content`表达值语义，但没有闭合真实C++ source identity。
V147重新反编译四端后确认，函数头先从`_motionContentVariant`的copy建立一个函数级
`ncbPropAccessor`；其conversion Variant在第一次读取前销毁，而accessor一直存活到函数尾。
`loopTime/lastTime/tag/priority`均通过这个accessor做flags=0 typed读取。

priority路径另有一个full-expression临时accessor。它读取typed numeric `[0]`，由结果建立
函数级root-item accessor；数字结果、临时priority accessor及其conversion Variant全部在
root `content` getter之前销毁。root accessor随后跨过两个container clear、parameter分支、
建树与变量初始化，并在motion accessor之前释放。旧wrapper源码无法表达这项析构边界，现已
恢复为2个长期具名accessor加1个临时accessor的直接ncbind形状。

`parameterize`读取使用的32位hint槽还与`Player_initNodeFields_guess`共享；`parameter`使用
init-private槽。四端精确地址及xref、完整owner树、HRESULT/reentrancy probe与IDB落地见
`motionplayer_init_non_emote_nested_ncb_accessor_source_identity_four_binary_2026-08-16.md`。

## 相邻状态字节与异常提交边界

源码此前只在函数末尾写 `_allplaying=true`，完全没有清 `_syncWaiting`。这同时造成值错误
与异常边界错误。四端的确切证据如下：

| 参考二进制 | 状态写 | 后继调用 |
|---|---|---|
| Android ARM64 | `0x6B0E54`: `*(word *)(player+1098)=0x0100` | `0x6B0E60` build，`0x6B0E68` variables |
| Android ARMv7 | `0x580DE4`: `*(word *)(player+750)=0x0100` | `0x580DEA` build，`0x580DF0` variables |
| iOS ARM64 | `0x1001084D8`: `*(word *)(player+986)=0x0100` | `0x1001084E0` build，`0x1001084E8` variables |
| iOS ARMv7 | `0x105B9A`: `player[687]=1`; `0x105BA0`: `player[686]=0` | `0x105BAA` build，`0x105BB4` variables |

各布局中低字节都是 `_syncWaiting`，高字节都是 `_allplaying`。三个构建用 little-endian
16 位合并写 `0x0100`，iOS ARMv7 用两个显式 byte store；共同语义均为：

```text
syncWaiting = false
allplaying = true
```

而且写入严格位于 `buildNodeTree()` 和 `initVariables()` 之前。因此后两者任一抛出时，
这两个状态已经提交，不存在回滚。

## Chain 尾部

四端的分支方向会因编译器布局不同而相反，但共同边界不变：

- 非 Chain：清 frame tick，把 eval time 设为 `min(0, lastTime)`，并置 queuing；
- Chain：保留上述三个字段；
- 两条路径最终都置 firstFrame。

机器码可分别出现非 Chain 分支内的 `word 0x0101` 与分支后的重复 firstFrame byte store。
源码用一次共同的 `_firstFrame=true` 表达相同的最终写入和顺序。

### 2026-08-16 数值最小值补充

重新检查尾块原始指令后，两个 32 位目标都把该表达式展开为
`lastTime < +0 ? lastTime : +0`，因此零必须是 `std::min` 的第一操作数；旧文和旧源码写成
`min(lastTime, 0)` 会在 `lastTime=NaN` 时错误保留 NaN。两个 64 位目标使用
`FMINNM(lastTime,+0)`，同样把 NaN 变成 `+0`。四端共同的普通数与 NaN 行为是：负数
保留，正数和 NaN 得 `+0`。

signed zero 是目标代码生成的真实分化：两个 ARMv7 ordered-select 在 `lastTime=-0.0`
时保留第一操作数的 `+0`，两个 ARM64 `FMINNM` 则按 numeric-min 选择 `-0`。本地采用最能
解释 32 位操作数身份、也与旧源码家族一致的共享源码形状
`std::min(0.0, lastTime)`；不能把四端描述成对 `-0` 具有同一个最终位模式。详细指令表、
源码取舍和回归见
`motionplayer_nonchain_initial_time_operand_order_four_binary_2026-08-16.md`。

## 源码修正与回归面

- `cpp/plugins/motionplayer/PlayerCore.cpp`
  - 在建树前写 `_syncWaiting=false; _allplaying=true;`；
  - 删除函数末尾过晚的 `_allplaying=true`；
  - 将 Chain 尾部改写为四端共同伪代码，并删除旧 `libkrkr2.so` 单地址注释。
- `tests/unit-tests/plugins/motionplayer-dll.cpp`
  - 在真实普通 motion play 前预置 `syncWaiting=true/allplaying=false`；
  - play 成功后断言 sync waiting 已清且 playing 已置位。

当前测试覆盖正常返回边界；四端反编译同时证明状态写早于两个可能抛出的复杂初始化调用，
异常提交顺序记录在本文件中，后续可在不伪造 PSB fixture 的前提下找到自然抛错样本时再加
异常路径执行测试。

## IDB 改进

四个入口均已统一为
`void Player_initNonEmoteMotion_guess(void *player, unsigned int playFlags)`，并在函数注释中补全
普通初始化器的数据流、parameter 容器边界、相邻状态提交和 Chain 尾部。三个 word store
与 iOS ARMv7 的两个 byte store 都增加了字段语义和“先于建树/变量初始化”的异常边界注释。
四端均 force recompile 后保存成功。

## 验证

- Web Debug 完整构建与链接完成，增量复验为 `ninja: no work to do.`；
- Wasmtime Debug `krkr2_wasmtime_guest` 完整 wasm 链接及转换完成，增量复验为
  `ninja: no work to do.`；
- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` 使用 Web Debug 的真实 Emscripten
  参数执行 `-fsyntax-only` 通过，仅有仓库既有 `_tss` 弃用警告；
- 当前仓库没有可直接运行这份 Catch 翻译单元的 native 测试目标，因此这里只声明真实
  编译/链接与完整测试翻译单元语法验证，不把 `-fsyntax-only` 冒充为运行时执行。
