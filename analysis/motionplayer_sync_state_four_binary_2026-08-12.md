# MotionPlayer Player 同步状态链四参考二进制纵切面（2026-08-12）

## 1. 结论与证据范围

本轮重新从 `reference/binaries/` 的四个当前产物定位
`defaultSyncActive -> Player 构造 -> syncActive/syncWaiting ->
releaseSyncWait/skipToSync -> frame progression`，不沿用旧
`libkrkr2.so` 地址注释作为函数身份。四端在源码级状态机上完全一致：

1. `defaultSyncActive` 是进程全局 byte/bool，镜像初值为 `false`；构造器只在
   实例构造时复制一次到 `syncActive`，之后修改 class default 不追写既有实例；
2. `syncActive` 是可读写脚本属性；构造器与公开 setter 是四端仅有的两个写入点；
3. `syncWaiting` 是只读脚本属性。普通 motion 初始化清零；三个时间流推进器在
   sync frame 上置位；`releaseSyncWait` 只清它，不修改 `syncActive`；
4. 三个推进器都先以 `syncActive && content.align` 对齐时间，再重新读取
   `syncActive` 并以 `content.sync` 进入等待及排队 sync event；
5. frame progression 在 reseek/advance/rewind 之后反复读取 `syncWaiting`，一旦
   置位立即停止本帧后续步骤；
6. `skipToSync` 这个名字具有误导性：它不清/置同步 byte，也不直接派发 sync
   event。它遍历 tag stream 的死读取链，然后建立普通 frame cursor/queue 状态；
7. `skipToSync` 不是 `fmax/fmin`。它保留一次 pre-loop `lastTime` 快照，非空遍历
   后再读取一次上界，并使用 ordered `< 0`、`> upper` 比较，因此 NaN、signed
   zero、负上界及 reentrant getter 都有可观察边界。

旧源码里来自另一份 `libkrkr2.so` 的单端地址只在本纵切面涉及的注释中清除；其他
尚未重审的纵切面不在本轮批量改写。本文集中保存当前地址和 ABI offset，编译源码
仅描述四端共同语义。

## 2. 字符串、注册与函数映射

普通 IDA string search 没有找到这些成员名；按 UTF-16LE 原始字节搜索后得到：

| 成员字符串 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `defaultSyncActive` | `0x14D6316` | `0xD85C24` | `0x10195C98C` | `0x174ECF0` |
| `syncActive` | `0x14D63F2` | `0xD85D00` | `0x10195CAF4` | `0x174EE58` |
| `syncWaiting` | `0x14D655E` | `0xD85E6C` | `0x10195CCA0` | `0x174F004` |
| `releaseSyncWait` | `0x14D6612` | `0x598BF8`* | `0x10195CE9E` | `0x174F202` |
| `skipToSync` | `0x14D66B0` | `0xD85F72` | `0x10195CF9E` | `0x174F302` |

\* ARMv7 Android 的该地址是 registrar 代码中的内联宽字面量位置；实际注册 xref
在 `0x5986F4` 附近，不应把 code literal 误判成独立 rodata object。

四端 Player registrar：

| Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---:|---:|---:|---:|
| `0x6D3DA8` | `0x597EC8` | `0x1001244F8` | `0x123848` |

### 2.1 注册位置

| 成员 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `defaultSyncActive` | `0x6D3EAC` | `0x597F04` | `0x100124540` | `0x123888` |
| `syncActive` | `0x6D4758` | `0x598124` | `0x100124868` | `0x123B70` |
| `syncWaiting` | `0x6D5028` | `0x59835C` | `0x100124BB8` | `0x123E78` |
| `releaseSyncWait` | `0x6D5E7C` | `0x598700` | `0x10012512C` | `0x12437A` |
| `skipToSync` | `0x6D6220` | `0x5987C4` | `0x10012524C` | `0x124488` |

### 2.2 Accessor/method/constructor

| 目标 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| Player ctor | `0x6CC110` | `0x5935C4` | `0x10011EC04` | `0x11D488` |
| default get | `0x6D67D8` | `0x598D24` | `0x100125430` | `0x124626` |
| default set | `0x6D67E4` | `0x598D30` | `0x10012543C` | `0x124634` |
| syncActive get | `0x6D6A6C` | `0x598EBE` | `0x1001255B0` | `0x1247B6` |
| syncActive set | `0x6D6A74` | `0x598EC4` | `0x1001255B8` | `0x1247BC` |
| syncWaiting get | `0x6D6B7C` | `0x598FF0` | `0x1001256D0` | `0x1248F2` |
| releaseSyncWait | `0x6D6E28` | `0x59913A` | `0x100125954` | `0x124B54` |
| skipToSync | `0x6D08E4` | `0x595C48` | `0x100121A78` | `0x1207F8` |

`defaultSyncActive` 的 get/set 不接收 Player；其他 accessor/method 接收 Player。
NCB surface 把前者注册为 class RW property，把 `syncActive` 注册为 instance RW，
把 `syncWaiting` 注册为 instance RO，并把两个 method 注册为零实参回调。

## 3. 四端布局和 process-global storage

| 字段 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `syncActive` | `+1093` (`0x445`) | `+745` (`0x2E9`) | `+981` (`0x3D5`) | `+681` (`0x2A9`) |
| `syncWaiting` | `+1098` (`0x44A`) | `+750` (`0x2EE`) | `+986` (`0x3DA`) | `+686` (`0x2AE`) |
| allplaying/skip gate | `+1099` | `+751` | `+987` | `+687` |
| clamped evaluation cursor | `+456` | `+288` | `+344` | `+228` |
| queuing / firstFrame | `+480/+481` | `+312/+313` | `+368/+369` | `+252/+253` |
| tag owner | `+1072` | `+732` | `+960` | `+668` |
| raw frame cursor | `+1120` | `+776` | `+1008` | `+708` |
| cached `lastTime` | `+1128` | `+784` | `+1016` | `+716` |
| `loopTime` | `+1136` | `+792` | `+1024` | `+724` |

全局 default byte：

| Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---:|---:|---:|---:|
| `0x1AB54A8` | `0x111160C` | `0x102517794` | `0x2143970` |

四处 image byte 都是零。四端 getter 是 byte load，setter 是 byte store；Android
ARM64 setter 因 bool ABI 先把入参 `AND 1`。这不是脚本语义差异。

构造器在四端都读取对应 global byte 并写入 instance `syncActive`，同时将
`syncWaiting` 初始化为零。global setter 不枚举实例，也不回写现有 Player。

## 4. Writer/read 交叉引用审计

对四种 layout 的 `syncActive` field displacement 做指令交叉搜索后，每端都只有两个
写入者：Player ctor 和公开 setter。读者为 getter，以及：

- `Player_advanceTimelineStreams_guess`；
- `Player_rewindTimelineStreams_guess`；
- `Player_fullReseek_guess`。

函数地址：

| 目标 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| advance | `0x6B3EBC` | `0x582BE0` | `0x10010AA08` | `0x1083D8` |
| rewind | `0x6B6E1C` | `0x584838` | `0x10010D230` | `0x10AB68` |
| full reseek | `0x6B5AA8` | `0x583C8C` | `0x10010C3CC` | `0x109DAC` |

`syncWaiting` 的写入集合包括 ctor/普通 motion init、上述三个 stepper 和
`releaseSyncWait`；frame progress core 多次读取它作为短路门。源码中曾经出现过的
`syncActive = syncWaiting && allplaying` 没有四端 writer 证据，必须保持删除状态。

## 5. align/sync 数据流和 frame progression 门控

三个 stepper 虽然分别处理正向、反向和绝对 reseek，frame content 的共同逻辑是：

```text
if player.syncActive:
    contentForAlign = frame.content
    if bool(contentForAlign.align):
        player.motionCompleted = true
        player.clampedEvalCursor = player.layerCurrentTime
        player.rawFrameCursor = player.layerCurrentTime

if player.syncActive:                    // 重新读取，不复用第一处条件
    contentForSync = frame.content
    if bool(contentForSync.sync):
        player.syncWaiting = true
        player.clampedEvalCursor = player.layerCurrentTime
        player.rawFrameCursor = player.layerCurrentTime
        Player_enqueueSyncEvent_guess(player, frame/content state)
```

两处 `syncActive` 检查之间存在 TJS dispatch，因此重新读取具有 reentrant 可观察性；
源码不能合并成一个缓存的 bool。align 与 sync 还分别取得 content Variant owner，
不能把所有 member read 简化成对同一个裸 dispatch 的无生命周期读取。

frame progress 的 first-frame、forward wrap 和 reverse wrap 链在调用 full reseek、
advance 或 rewind 后重新测试 `syncWaiting`（并列测试 motionCompleted）。若某个脚本
getter 令 stepper 置位等待，后续 node/cursor 步骤在当前帧立即停止。等待不是由
`syncActive` accessor 自身产生，也不由 `skipToSync` 产生。

## 6. `releaseSyncWait`

四端共同函数体等价于：

```cpp
void releaseSyncWait(Player *player) {
    player->syncWaiting = false;
}
```

没有参数检查之外的业务逻辑，没有 `syncActive=false`、event queue 清理、cursor
变更、allplaying 变更或隐式 resume 调用。下一次 frame progression 是否继续，取决于
调用方何时再次推进。

## 7. `skipToSync` 的共同伪代码

去除 ABI 的 Variant/exception-unwind 展开后，四端源码形状为：

```text
skipToSync(player):
    if !player.allplaying:
        return
    if !(player.loopTime < 0.0):
        return

    tags = CopyRef(player.persistentTagOwner)
    count = getCount(tags)
    initial = player.lastTime
    upper = initial

    for i = 0; i < count; ++i:
        frame = tags[i]
        if int(frame.type) == 1:
            discard double(frame.time)
            content = frame.content
            discard bool(content.sync)

    if count >= 1:
        upper = player.lastTime

    cursor = initial
    if cursor < 0.0:
        cursor = 0.0

    evaluation = cursor
    if cursor > upper:
        evaluation = upper

    player.queuing = true
    player.firstFrame = true
    player.rawFrameCursor = cursor
    player.clampedEvaluationCursor = evaluation
```

### 7.1 精确读取/提交顺序

顺序不是实现细节：

1. 先通过 tag owner 读取 `count`，后读取第一次 `lastTime`；
2. 循环对每个 numeric item 创建独立 frame Variant；
3. 仅当 `type == 1` 时按 `time -> content -> sync` 顺序读取；
4. frame owner 活过其全部成员读取，content owner 活过 `sync` 读取；
5. `count >= 1` 时才在遍历结束后二次读取 `lastTime`；空/负 count 不重读；
6. 所有脚本 dispatch 与数值转换成功后，才一起提交 queue/cursor 状态。

因此 getter/转换异常会保留此前 getter 的脚本副作用及 reentrant Player 修改，但不会
提交本函数尾部的 `queuing/firstFrame/raw/evaluation` 写入。原生 exception unwind 会
按逆序释放当时仍存活的 content、frame 和 tags owner。

### 7.2 死读取不可删除

`time` 和 `sync` 的数值结果都不参与 cursor 计算；`type/content` 也只用于达到后续
dispatch。但四个 getter 可能抛异常、修改 Player 或管理对象生命周期，故这一整条
“死”读取链都是可观察行为，不能以优化端口为由删除。

### 7.3 ordered comparison 与 IEEE-754 边界

ARM64 Hex-Rays 一度把下界/上界渲染为 `fmax/fmin`；结合实际 compare/select 指令和
两个 ARMv7 的显式条件分支，四端共同源码行为是 ordered `<` 与 `>`：

- `NaN < 0` 和 `NaN > upper` 均为 false，所以 NaN 原样进入 raw/evaluation cursor；
- `-0.0 < 0` 为 false，raw 和 evaluation 都保留负零符号；
- `initial < 0` 时 raw cursor 变成精确 `+0.0`；若 `upper` 仍为负值，随后
  `+0.0 > upper` 为真，所以 evaluation 又被 cap 成该负值；
- `upper=NaN` 时 `cursor > upper` 为 false，evaluation 保留 cursor；
- 不能用 `std::fmax/std::fmin` 替代，因为它们的 NaN 与 signed-zero 规则不等价。

### 7.4 reentrant snapshot 边界

非空 tag dispatch 可以在 numeric/member getter 内回入 Player 并改变 `lastTime`。
此时 raw cursor 仍取遍历前 `initial`，evaluation cap 取遍历后的第二次读取。例如：

```text
initial lastTime = 10
tags[0] getter changes lastTime to 4
result raw cursor = 10
result evaluation cursor = 4
```

同一 getter 若清除 Player 的 persistent tag field，函数栈上的 `tags = CopyRef(...)`
仍使 container、frame 和 content 活过整个读取链；这也是不能改成 `const &` 借用的
原因。

## 8. Android ARM64 IDB 函数边界修复

该 IDB 原先把 `Player_contains_guess` 从 `0x6D071C` 错误延伸到 `0x6D0CD4`，把
`skipToSync` 整体吞进了前一个函数。按 registrar callback、控制流终点及下一个函数
入口修复后：

| 函数 | 修复后范围 | 大小 |
|---|---|---:|
| `Player_contains_guess` | `0x6D071C .. 0x6D08E4` | `0x1C8` |
| `Player_skipToSync_guess` | `0x6D08E4 .. 0x6D0CD4` | `0x3F0` |
| next `Player_getLayerGetter_guess` | 从 `0x6D0CD4` 开始 | — |

两个函数都重新强制反编译；`contains` 不再显示 skip body，`skipToSync` 有独立、完整
的 exception-unwind 与返回路径。

## 9. 修改前本地差异

本轮编辑前，本地 class default、构造复制、RW/RO NCB surface、release 单 byte 清零
以及三个 stepper 的两段 gate 已大体符合四端。剩余实质偏差集中在 `skipToSync`：

```cpp
cursor = std::fmax(_cachedTotalFrames, 0.0);
clamped = std::fmin(_cachedTotalFrames, cursor);
```

这段代码：

- 没有保存 pre-loop snapshot；
- 没有在非空遍历后重读 `lastTime`；
- 错误改变 NaN/signed-zero/负上界行为；
- 无法表现 reentrant getter 令 raw/evaluation cursor 分离。

另有若干相关注释仍把旧 Android `libkrkr2.so` 地址写在编译源码中，容易把历史单端
观察误当成当前四端映射。

## 10. 源码和测试落地

### 10.1 源码

- `PlayerTimeline.cpp`
  - 保留独立 tags/frame/content Variant owner 和全部死读取；
  - 加入 pre-loop `initialLastTime`；
  - 仅在 `count >= 1` 时重读 evaluation upper limit；
  - 用 ordered `< 0` 和 `> upper` 提交两个 cursor；
  - 注释明确 tag dispatch 可以 re-enter Player。
- `PlayerLayerQuery.cpp`
  - `releaseSyncWait` 保持只清 waiting byte；相关旧地址改为四端语义说明。
- `Player.h`
  - 明确 class default 的构造时复制及 `syncActive` writer 集；
  - 增加未注册的 test-only 状态注入 hook，用于 tag owner/reentrancy/IEEE-754
    差分边界；不扩展脚本 API。
- `main.cpp`
  - 明确 `syncWaiting` native descriptor 没有 setter。
- `PlayerFrameProgress.cpp`
  - 相关 writer/reader 注释改为四端共同结论；
  - 清除本纵切面内残留的旧单端地址，保留每一步后重新读取 waiting 的控制流。

### 10.2 Unit case

新增 `Player sync state chain preserves native gates and cursor boundaries`，覆盖：

- class default 只影响随后构造的实例；
- `releaseSyncWait` 清 waiting 而保留 active；
- `!allplaying` 和 `loopTime >= 0` 两个 skip gate 无副作用；
- empty tags 的普通 cursor/queue 提交；
- NaN、`-0.0` 和负 lastTime；
- reentrant non-empty tags：遍历中把 lastTime 从 10 改为 4 并清 persistent owner，
  验证 raw=10、evaluation=4、读取顺序及三个 dispatch 析构计数；
- NCB `syncActive` RW、`syncWaiting` 写入拒绝、零参数 `releaseSyncWait`。

当前 Web 配置的 unit target 未启用，因此本轮不声称运行了该 Catch2 case；验证分成
完整 translation-unit 语法编译和现有 Web/Wasmtime 链接构建。

## 11. IDB 改进

四个 IDB 均完成：

- 命名 `Player_getSyncActive_guess`、`Player_setSyncActive_guess`、
  `Player_getSyncWaiting_guess`、`Player_releaseSyncWait_guess`、
  `Player_skipToSync_guess`；default accessor 已沿用现有正确命名；
- 设置 `bool(void)`、`void(bool)`、`bool(void *player)`、
  `void(void *player,bool)`、`void(void *player)` 函数类型；
- 命名/注释 `g_Player_defaultSyncActive_guess`；
- 在实际 registration point 添加成员映射注释；初次误落在 Android ARM64
  registrar 中间位置的注释已清除并移到正确 xref；
- 为七个 API 函数与 registrar 强制刷新 Hex-Rays cache，并再次 fresh-decompile；
- 四份 IDB 全部保存。Android ARM64 另包含第 8 节的函数边界修复。

保存目标为对应四份参考数据库：Android ARM64/ARMv7 `.so.i64` 与 iOS
ARM64/ARMv7 IDB。

## 12. 验证

本轮实际完成：

- 使用 Web build 的真实 include/define/编译选项，对完整
  `tests/unit-tests/plugins/motionplayer-dll.cpp` 执行 `-fsyntax-only`：通过；仅有
  既存 `_tss` warning；
- `out/web/debug` 的 `motionplayer` static library：通过；
- `out/wasmtime/debug` 的 `motionplayer` static library：通过；
- `out/wasmtime/debug` 的 `krkr2_wasmtime_guest`：通过并链接/转换 wasm；
- `out/web/debug` 的 `krkr2` 及默认目标：最终通过；
- 对上述五个目标以 `--parallel 1` 再次构建：全部 `ninja: no work to do`；
- 四端相关函数 fresh-decompile 后保存 IDB。

Web 主程序第一次工具调用在链接进行中超时；紧接着第二次并发写同一个
`index.wasm` 曾得到一次 `permission denied`。残留的第一次链接随后正常完成，相关
进程退出后，`krkr2` 与默认目标的并行/串行复验均为 no-work。因此这不是源码、符号
或稳定链接失败，而是两次重叠 linker 对同一输出文件的短暂争用。
