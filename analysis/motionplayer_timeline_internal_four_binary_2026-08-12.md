# motionplayer 时间线内部状态/seek/window 四参考二进制复原（2026-08-12）

## 结论

本轮以 `reference/binaries/` 的 Android arm64、Android armv7、iOS arm64、
iOS armv7 四份当前参考产物重新从调用拓扑定位并强制刷新反编译，闭合了以下
五个源函数：

- `EmoteEngine::initializeTimelineState_guess`
- `EmoteEngine::initializeTimelineControllers_guess`
- `EmoteEngine::seekTimeline_guess`
- `EmoteEngine::applyTimelineWindow_guess`
- `EmoteEngine::setVariable`（IDA 名称统一为
  `EmoteEngine_setVariable_guess`）

项目原有的 `Like_0x...` 名称和地址注释来自旧 `libkrkr2.so`，在当前 Android
arm64 参考中已经分别落入 `setColor`、`playTimeline`、`getAnimating`、
`buildEyeControl` 等无关函数，不能继续作为证据。

## 当前四份映射

| 源函数 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| initialize timeline state | `0x66D03C` | `0x5590E8` | `0x1001ABD5C` | `0x1AB4B0` |
| initialize timeline controllers | `0x66DC20` | `0x559848` | `0x1001AC5DC` | `0x1ABDA4` |
| seek timeline | `0x66EE30` | `0x55A0F8` | `0x1001AD2C0` | `0x1ACA22` |
| apply timeline window | `0x6671FC` | `0x555BC0` | `0x1001A6BDC` | `0x1A636C` |
| setVariable | `0x66E608` | `0x559D84` | `0x1001ACDBC` | `0x1AC5F4` |

调用者定位表：

| 调用者 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| playTimeline | `0x670350` | `0x55AA70` | `0x1001ADE0C` | `0x1AD53C` |
| resetActiveTimelines | `0x6670F0` | `0x555B4C` | `0x1001A6844` | `0x1A5FC0` |

`playTimeline` 的共同顺序为：必要时构建 state -> 写 flags/构建轨道 controller ->
`seek(..., 0.0)`。`resetActiveTimelines` 直接调用 window helper。四份 caller 与 callee
均重新反编译，不沿用旧数据库伪名。

## 旧地址失效证明

在当前 Android arm64 IDB 中查询旧地址：

| 旧地址 | 当前实际所属函数 |
|---:|---|
| `0x66FC5C` | `EmotePlayer_setColorCompat_guess`（函数起点 `0x66FB5C`） |
| `0x670840` | `EmoteEngine_playTimeline_guess`（起点 `0x670350`） |
| `0x671A50` | `EmoteEngine_getAnimating_guess`（起点 `0x671378`） |
| `0x669E1C` | `EmoteEngine_buildEyeControl_guess`（起点 `0x669B5C`） |
| `0x671228` | `EmotePlayer_getVariableRange_guess`（起点 `0x670FCC`） |

因此已从编译源码中删除这些地址型名称和本纵向内的旧地址行注释；当前地址只保留
在本文档和 IDB 中。

## 源容器结构

### TimelineData

四份 builder 对 TimelineData 的分配大小分别是：

| ABI | 分配大小 | 对应实现 |
|---|---:|---|
| Android arm64 | `0x50` | libstdc++ `std::deque<Track>` header |
| Android armv7 | `0x28` | libstdc++ `std::deque<Track>` header |
| iOS arm64 | `0x30` | libc++ `std::deque<Track>` header |
| iOS armv7 | `0x18` | libc++ `std::deque<Track>` header |

它不是跨平台固定的“80B 对象”；对象本身就是一个自然 ABI 的 deque header。
源码类型因此从 `EmoteTimelineData80B` 改为 ABI 中立的 `EmoteTimelineData`。

### Track 与 Frame

Track 自然步长为：

- 两份 64 位参考：56 字节；
- 两份 32 位参考：28 字节。

源码类型因此从 `EmoteTimelineTrack56B` 改为 `EmoteTimelineTrack`。成员语义共同为：

1. `ttstr label`；
2. `bool instantVariable`；
3. `std::vector<Frame>`；
4. 单指针 owner `std::unique_ptr<EmoteVarController> controller`；
5. `float output`。

Frame 在四份中均为 24 字节：

```text
double time
bool   typeZero
float  value
double easingWeight
```

Frame vector 步长在四份中都是 24。最后一帧是 sentinel：seek 只扫描到
`size()-2`，window 也从不 dispatch 最后一帧。

Deque block 的元素数同样体现标准库差异：

| ABI | block 字节 | Track/block |
|---|---:|---:|
| Android arm64 | 504 | 9 |
| Android armv7 | 504 | 18 |
| iOS arm64 | 4088 | 73 |
| iOS armv7 | 4088 | 146 |

## initializeTimelineState 数据流与生命周期

共同执行顺序如下：

1. 分配并默认构造新的 TimelineData/deque；删除旧 `state.timelineData` 后替换。
2. 从 `rawElement` 读取 `loopBegin`、`loopEnd`、`lastTime`。
3. 写 `blendWeight = 1.0f`、`autoStop = 0.0`。
4. 分配 `EmoteVarController(1)`；删除旧 blend controller 后替换，并以
   `{value=1, duration=0, power=0, append=false}` 重置。
5. 读取 `variableList`，按原数组顺序追加 Track；不排序、不去重、不跳空 label。
6. Track 的 `instantVariable` 来自 Engine instant-variable set 的成员测试。
7. 读取每条轨道的 `frameList`，按原顺序追加全零 Frame。
8. 每帧先读 `time` 与 `type`；`typeZero = (type == 0)`。
9. `type != 0` 才读取 `content.value` 和 `content.easing`。value 先从 TJS real
   转 double，再窄化 float。easing 保存为：

   ```text
   easing == 0 : 1
   easing >  0 : easing + 1
   easing <  0 : 1 / (1 - easing)
   ```

10. `maxFrameTime` 从 `0.0` 开始用有序 `>` 更新；仅当 metadata 的
    `lastTime < 0.0` 时以该最大值覆盖。NaN 不会更新最大值。

该函数不写 `flags`、`currentTime` 或 `frameCursors`。每个variable先取得frameList和
Count再emplace Track；每个raw frame则先取得数组元素再emplace Frame。Track/Frame一旦
emplace，后续PropGet失败不会移除它，且整个builder不会恢复已经替换的旧TimelineData。
完整提交/失败前缀复核见
`analysis/motionplayer_timeline_initialization_commit_lifecycle_four_binary_2026-08-15.md`。

四个平台的 controller 自然分配大小为 `0x80/0x48/0x60/0x38`；这是同一源类型
在不同标准库/指针宽度下的 ABI 差异，不应写进类型名。

## TJS member hint 身份

四份 builder 都向这些 PropGet 传递非空、进程级静态 hint 指针。恢复的逻辑 slot：

- timeline-only：`loopBegin`、`loopEnd`、`lastTime`、`variableList`、`time`、
  `content`、`easing`；
- Engine 共享：`label`、`type`、`frameList`、`value`。

共享不是推测。以 Android arm64 为例：

- `engineLabelHint_guess @ 0x1AB4F18` 被 variable/eye/eyebrow/mouth/
  transition/selector/timeline builders、timeline info、状态序列化与恢复共同引用；
- `engineTypeHint_guess @ 0x1AB4F6C` 被 clamp builder、timeline frame builder 和
  一个运行时 type 查询共同引用；
- `engineFrameListHint_guess @ 0x1AB4F1C` 被 variable-list 与 timeline builder
  共同引用；
- `engineValueHint_guess @ 0x1AB4EF8` 被 selector 状态与 timeline content 共同引用。

四 ABI 的 timeline hint 地址（按
`loopBegin, loopEnd, lastTime, variableList, frameList, label, time, type,
content, value, easing` 排列）：

| ABI | 地址序列 |
|---|---|
| Android arm64 | `1AB4F80, 1AB4F84, 1AB4F88, 1AB4F8C, 1AB4F1C, 1AB4F18, 1AB4F90, 1AB4F6C, 1AB4F94, 1AB4EF8, 1AB4F98` |
| Android armv7 | `1111518, 111151C, 1111520, 1111524, 11114B4, 11114B0, 1111528, 1111504, 111152C, 1111490, 1111530` |
| iOS arm64 | `101B6A030, 101B6A034, 101B6A038, 101B6A03C, 101B69FCC, 101B69FC8, 101B6A040, 101B6A01C, 101B6A044, 101B69FA8, 101B6A048` |
| iOS armv7 | `187DA50, 187DA54, 187DA58, 187DA5C, 187D9EC, 187D9E8, 187DA60, 187DA3C, 187DA64, 187D9C8, 187DA68` |

源码原来的 `timelineInfoLabelHint_guess` 因此范围过窄，已改为
`engineLabelHint_guess` 并由 timeline builder 与 playing-info writer 共用。本轮同时在
已闭合的 clamp builder 上恢复同一个 `engineTypeHint_guess`。

## initializeTimelineControllers

共同边界：

1. 无条件 `state.flags = flags`；
2. `(flags & 2) == 0` 立即返回；
3. 仅对 `frameList` 非空且非 instant 的 Track 操作；
4. controller 为空时分配 `EmoteVarController(1)`；
5. 已存在时才以全零 target/duration/power、`append=false` 重置；新构造 controller
   不再额外调用 reset。

flags先于bit门提交；bit关闭时不读timelineData，null owner安全，bit打开时无null guard。
空轨和instant轨完全保留已有controller/queue/state；后续关闭flags也不回收已创建的
track controller。2026-08-15 fresh指令与回归复核见
`analysis/motionplayer_timeline_initialization_commit_lifecycle_four_binary_2026-08-15.md`。

## seekTimeline

共同算法：

1. `frameCursors.clear()` 只缩短到零，保留 vector capacity；随后才无guard地解引用
   `timelineData`。null data会在cursor已清空后失败。
2. 逐 Track 遍历。
3. 当 `(flags & 4) != 0 && instantVariable` 时跳过整条轨道，**不向 cursor vector
   追加元素**。
4. `internalRoute = (flags & 2) != 0 && !instantVariable`。
5. 空帧和单帧轨道都追加 cursor `0`，不 dispatch。
6. `size >= 2` 时只扫描 `[0, size-2]`。每槽先在`!typeZero`时更新
   `lastActionFrame`，再测试`current.time <= target && next.time > target`；因此target在
   frame0之前或为NaN时仍可能重放未来/最后一个pre-sentinel action。
7. 非跳过 Track 总会在dispatch前追加当前 cursor。
8. 找到 action frame 时，先算`raw = next.time - target - 1.0`，再以ordered
   `raw <= 0.0 ? +0.0 : raw`钳位；这会规范化`-0`并传播NaN。internal route写Track
   controller，其他路径调用普通`EmoteEngine::setVariable`。
9. 全部Track正常完成后才写`state.currentTime = target`；较早cursor/dispatch前缀不回滚。

2026-08-15 的cursor clear/null-owner、future-action与ordered clamp复核见
`analysis/motionplayer_timeline_seek_cursor_clear_future_action_four_binary_2026-08-15.md`。

原 IDB 名 `EmoteEngine_setVariableFromTimelineFrame_guess` 是错误的范围推断：四份完整
函数体都包含 HM6 查询、0/1/2/4/5/6/7/8 全类型路由和 HM7 fallthrough，证明它就是
通用 `setVariable`。fresh code xref显示timeline有三类直接caller：seek、window和pass
flush；另外两个caller是Primary raw callback与D3D façade。lazy float窄化、mouth
signed-int转换及两套ease管线见
`analysis/motionplayer_set_variable_router_double_ease_integer_conversion_four_binary_2026-08-15.md`。

## applyTimelineWindow

共同算法：

1. `timelineData == nullptr` 时跳过全部 Track/cursor 工作，但仍执行最后的
   `currentTime = targetTime`。2026-08-15 fresh 四端复核补回了旧版遗漏的这个门。
2. data 非空时以物理 Track 序号 `trackIndex` 遍历 deque。
3. flags&4 的 instant Track 被跳过，但 `trackIndex` 仍递增。
4. 非跳过 Track 从 `frameCursors[trackIndex]` 取有符号 32 位 cursor；没有范围检查。
5. 若 cursor 小于 `frameCount-1`，测试下一帧是否跨过窗口：
   - inclusive：`next.time <= targetTime`；
   - strict：`next.time < targetTime`。
6. 每个跨过的 `!typeZero` 帧仅在它后面仍有一帧时 dispatch；因此尾 sentinel 永远
   不 dispatch。
7. transition 用“再下一帧”的 time 计算：
   `max(afterNext.time - targetTime - 1.0, 0.0)`。
8. 更新 cursor，并在遍历结束后写 `state.currentTime = targetTime`。

### 必须保留的 cursor 错位边界

seek 对 flags&4 instant Track 不追加 cursor；window 却按物理 Track 序号索引 cursor，
而且跳过 instant 时仍递增该序号。因此 instant Track 位于普通 Track 之前时，后续
普通 Track 可能读取 compact cursor vector 之外或读取错位元素。四份都如此。源码
当前实现保留该不安全边界，没有补 bounds check，也没有把 cursor 改成一轨一项。

### NaN 的 32/64 位产物差异

正常有限值下四份 transition clamp 等价。2026-08-15 直接检查指令后确认旧版把
64 位 `FMAX` 错读成了 numeric max：它不是 `FMAXNM`。因此表达式为 NaN 时四端都
传播 NaN；两份 32 位的有序 negative 分支同样不会选择零。

真正的产物差异只剩极端 signed-zero：64 位 `FMAX(x,+0)` 对 `x=-0` 选 `+0`，32 位
只在 ordered negative时替换，因而保留 `-0`。Web/wasm32源码继续使用
`std::max(x, 0.0)`，对应32位参考的 first-operand signed-zero语义。完整复核见
`analysis/motionplayer_timeline_window_null_data_cursor_routing_four_binary_2026-08-15.md`。

## IDB 改进

四个 IDB 均完成：

- 五个函数统一重命名；
- 五个函数写入源顺序原型（包括 arm32 hard-float 下 bool/double 分离寄存器造成的
  旧反编译参数乱序）；
- 强制刷新 Hex-Rays；
- 函数注释记录所有权、sentinel、cursor 错位和普通 setVariable 身份；
- 使用 UTF-16LE byte pattern 重新查找 11 个 property 字面量；
- 以精确长度 `uint16_t[]` 重建字符串边界并统一命名；
- 11 个 member-hint global 在四份中统一命名；
- 保存四个数据库。

精确字面量长度（含终止零）为：`loopBegin[10]`、`loopEnd[8]`、
`lastTime[9]`、`variableList[13]`、`frameList[10]`、`label[6]`、`time[5]`、
`type[5]`、`content[8]`、`value[6]`、`easing[7]`。这也消除了旧反编译中显示成
`"v"`、`"f"` 或指向长错误消息中间的伪字符串。

## 源码与测试改动

- 删除四个 `Like_旧地址` 内部方法名，改用 `_guess`。
- TimelineData/Track 改为 ABI 中立命名，所有调用点与测试同步。
- 恢复本纵向 11 个 PropGet hint，并恢复 playing-info `label` 与 timeline builder 的
  shared slot。
- 保留 seek/window 的 cursor 不对称、空/单帧 cursor、尾 sentinel、不做 bounds
  check 等边界。
- 恢复 window 的 null-data skip + unconditional currentTime commit；新增回归确认空
  decoded data不读取 cursor也不崩溃。
- seek transition改为四端共同的ordered `<=0`选择，且新增cursor capacity复用与
  target早于frame0仍重放future action的回归。
- 新增单元用例覆盖：
  - flags&4 instant Track 不占 cursor；
  - 空/单帧 Track 仍各占一个零 cursor；
  - seek dispatch 最后一个非零 action 而不 dispatch sentinel；
  - inclusive window dispatch 跨过的 action，但尾 sentinel 不执行；
  - loopEnd 使用 strict window，位于边界的帧在回 seek 前不执行。

## 验证

- `cmake --build out/web/debug --target motionplayer -j 8`：通过。
- `cmake --build out/wasmtime/debug --target motionplayer -j 8`：通过。
- 从 Web `compile_commands.json` 复用 EmoteEngine 的完整 Emscripten 参数，对
  `tests/unit-tests/plugins/motionplayer-dll.cpp` 执行 `-fsyntax-only`：通过；只有项目
  既有 `_tss` literal-operator 弃用警告。
- `cmake --build out/web/debug --target index.html -j 8`：完整 Web 链接通过。
- `cmake --build out/wasmtime/debug --target krkr2_wasmtime_guest -j 8`：guest 完整
  链接及 exnref 转换通过。
- `git diff --check`（本轮四个文件）：通过；只有工作区既有 LF/CRLF 提示。
- 四个 IDB 均 `idb_save` 成功。

当前 CMake 配置没有提供可直接运行这组 Catch2 用例的 motionplayer test executable，
因此这里只声明完整测试翻译单元的编译验证，不把 `-fsyntax-only` 冒充成运行时测试。

## 2026-08-13 owner 结构复核

本文早期以“owning pointer + 手写 delete”的可移植语义记录 Track 与 HM3 的三个
owner slot。随后对四端 HM3 node destruction、TimelineData/Track range destruction、
lazy replacement 和 Android armv7 owner-reset specialization 联合复核，已经进一步
闭合真实源结构：Track controller、HM3 timelineData、HM3 blendController 都是
单指针 `std::unique_ptr` owner，而不是普通 raw pointer field。源码已经删除手写析构/
移动 special members，详细证据见
`analysis/motionplayer_hm3_timeline_owner_lifecycle_four_binary_2026-08-13.md`。
