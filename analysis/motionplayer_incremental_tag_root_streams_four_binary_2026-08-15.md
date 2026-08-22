# MotionPlayer 增量 tag/root 时间流四参考二进制复原（2026-08-15）

## 1. 范围与证据约束

本纵切面重新审计两个完整的增量四流成员中最前面的 layer/tag 与
root/priority 区段。证据只来自 `reference/binaries/` 的四个当前参考产物：

- Android arm64-v8a `libmotionplayer.so`；
- Android armeabi-v7a `libmotionplayer.so`；
- iOS arm64 slice；
- iOS armv7 slice。

旧 `libkrkr2.so` 地址、旧源码注释以及本地 helper 的形状均不作为函数身份依据。
本文中的绝对地址仅用于证据映射，不进入编译源码注释；`_guess` 后缀仍表示剥离
符号后的语义恢复名。

本轮闭合的原生成员都同时内联 tag、root、variable-track 与 non-root node 四个
phase。本地把它们拆成 source-level helper 只是为了可读性和测试隔离，不声称参考
二进制存在这些独立 helper。

## 2. 函数、ABI 与 Player 字段

| 项目 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| forward 四流成员 | `0x6B3EBC` | `0x582BE0` | `0x10010AA08` | `0x1083D8` |
| rewind 四流成员 | `0x6B6E1C` | `0x584838` | `0x10010D230` | `0x10AB68` |
| live evaluation | `Player+0x1C8` | `Player+0x120` | `Player+0x158` | `Player+0xE4` |
| tag source | `Player+1072` | `Player+732` | `Player+960` | `Player+668` |
| tag cursor | `Player+916` | `Player+636` | `Player+804` | `Player+572` |
| tag current/next time | `+920/+928` | `+640/+648` | `+808/+816` | `+576/+584` |
| priority source | `Player+548` | `Player+356` | `Player+436` | `Player+296` |
| root cursor | `Player+568` | `Player+368` | `Player+456` | `Player+308` |
| root content | `Player+616` | `Player+416` | `Player+504` | `Player+352` |
| root current/next time | `+576/+584` | `+376/+384` | `+464/+472` | `+312/+320` |

两个成员的全部三个 caller 在四端都只设置一个 `this` 参数，没有额外的浮点
target 参数。循环中的时间门直接重读 Player 的 live evaluation 字段。因此本地
`advanceTimelineStreams_guess()` / `rewindTimelineStreams_guess()` 以及 tag/root phase
均不应携带 target 快照。

## 3. aggregate owner 的构造、作用域与逆序释放

四端 forward 与 rewind 都有相同的 owner 拓扑：

```text
tagOwner = CopyRef(Player.tagSource)
run tag phase through tagOwner

priorityOwner = CopyRef(Player.prioritySource)
run root phase through priorityOwner
run variable-track phase
run non-root node phase

destroy priorityOwner
destroy tagOwner
```

priority owner 不是函数入口就构造，也不是 root phase 的局部临时；它只在 tag phase
完整返回后构造。两个 owner 又都跨越 variable-track 与 non-root node phase 活到
aggregate 尾部。动态 getter 可以清掉 Player 持久字段乃至调用者最后一个外部引用，
本次四流遍历仍由这两个局部 owner 保活。若 tag phase 抛出，priority owner 尚未构造；
若后续 phase 抛出，C++ unwind 仍按 priority、tag 的顺序释放。

### 3.1 forward 构造与尾释放

| 边界 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| tag owner copy | `0x6B3EF4` | `0x582BFE` | `0x10010AA34` | `0x1083FE` |
| priority owner copy | `0x6B42C4` | `0x582E38` | `0x10010AD00` | `0x10868C` |
| priority owner release | `0x6B48F4..0x6B4904` | `0x583268..0x583270` | `0x10010B2C4..0x10010B2D8` | `0x108B10..0x108B1E` |
| tag owner release | `0x6B4908..0x6B4920` | `0x58327E..0x583286` | `0x10010B2DC..0x10010B2F0` | `0x108B22..0x108B30` |

### 3.2 rewind 构造与尾释放

| 边界 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| tag owner copy | `0x6B6E58` | `0x584856` | `0x10010D25C` | `0x10AB8E` |
| priority owner copy | `0x6B7214` | `0x584A76` | `0x10010D4FC` | `0x10AE0C` |
| priority owner release | `0x6B76F8..0x6B7708` | `0x584DA0..0x584DA8` | `0x10010D8D4..0x10010D8E8` | `0x10B15A..0x10B168` |
| tag owner release | `0x6B7714..0x6B7724` | `0x584DB6..0x584DBE` | `0x10010D8EC..0x10010D900` | `0x10B16C..0x10B17A` |

## 4. forward layer/tag phase

### 4.1 四端边界映射

| 边界 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| dynamic count | `0x6B3F54` | `0x582C1A` | `0x10010AA60` | `0x108458` |
| `count >= 1` gate | `0x6B3F58..0x6B3F5C` | `0x582C1E..0x582C20` | `0x10010AA64..0x10010AA68` | `0x10845E..0x108460` |
| wrapping `count-2` | `0x6B3F64` | `0x582C28` | `0x10010AA70` | `0x108468` |
| signed cursor/limit compare | `0x6B3F68` | `0x582C2C` | `0x10010AA74` | `0x10867C` loop |
| live evaluation load | `0x6B3F9C` | `0x582C7C` | `0x10010AAB4` | `0x10846E` |
| ordered-LT break | `0x6B3FA4..0x6B3FA8` | `0x582C84..0x582C8C` | `0x10010AABC..0x10010AAC0` | `0x108476..0x10847E` |
| cursor increment/store | `0x6B3FAC..0x6B3FB0` | `0x582C90..0x582C92` | `0x10010AAC4..0x10010AAC8` | `0x108484..0x108486` |

### 4.2 共同伪代码

```text
count = tagOwner["count"].AsInteger32()
if count >= 1:
    limit = signed32(uint32(count) - 2)
    while signed(layerCursor) < limit:
        if livePlayerEval < layerNextTime:
            break

        layerCursor = signed32(uint32(layerCursor) + 1)   // 先提交
        current = tagOwner[signed(layerCursor)]
        layerCurTime = current["time"].AsReal()
        next = tagOwner[signed32(uint32(layerCursor) + 1)]
        layerNextTime = next["time"].AsReal()

        if current["type"].AsInteger() != 1:
            continue
        content = current["content"]
        if syncActive:
            if content["align"].AsBool():
                motionCompleted = true
                livePlayerEval = layerCurTime
                frameTick = layerCurTime
            if syncActive && content["sync"].AsBool():
                syncWaiting = true
                livePlayerEval = layerCurTime
                frameTick = layerCurTime
                enqueueSync()
        action = content["action"].AsString()
        if action is nonempty:
            enqueueAction(void, action)
```

这里有五个容易被“合理化”而破坏一比一的边界：

1. `count` 是动态属性，不是容器固有长度；只有 `count >= 1` 才进入整个 phase，零和
   所有负数都不读取任何数字下标。
2. `count-2` 是 32 位寄存器回绕后按 signed 比较，不能用有 signed-overflow UB 的
   C++ `int` 减法表达。
3. forward 时间门的源码形状是 ordered `if (eval < nextTime) break`。任一侧为 NaN
   时比较为 false，因此 NaN 会继续推进到 signed limit。
4. cursor 用 32 位加法回绕并在第一个数字 getter 前提交；后续 getter 抛出时 cursor
   保留已推进状态。下一个 frame 的数字索引也使用 live/reloaded cursor 的回绕加一。
5. align 不要求 `layerCurTime == eval`；它可以把 live evaluation 与 frame tick 拉回
   当前 tag 时间。sync getter 前再次读 `syncActive`，action 又始终位于 sync 处理之后。
   align 或回调造成的时间变化会立刻影响下一轮的 live gate。

## 5. forward root/priority phase

### 5.1 四端边界映射

| 边界 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| dynamic count | `0x6B4318` | `0x582E56` | `0x10010AD2C` | `0x1086AC` |
| wrapping `count-2` | `0x6B4320` | `0x582E5E` | `0x10010AD30` | `0x1086B2` |
| signed cursor/limit compare | `0x6B4324` | `0x582E62` | `0x10010AD38` | `0x1086BA` |
| live eval / next-time gate | `0x6B4344..0x6B4350` | `0x582EA6..0x582EB6` | `0x10010AD60..0x10010AD6C` | `0x1086C6..0x1086D6` |
| cursor increment/store | `0x6B4354..0x6B4358` | `0x582EB8..0x582EBA` | `0x10010AD70..0x10010AD74` | `0x1086D8..0x1086DC` |

### 5.2 共同伪代码与提交顺序

```text
count = priorityOwner["count"].AsInteger32()
limit = signed32(uint32(count) - 2)       // 没有 count 正数 gate
while signed(rootCursor) < limit:
    if livePlayerEval < rootNextTime:
        break
    rootCursor = signed32(uint32(rootCursor) + 1)
    rootContent = priorityOwner[rootCursor]["content"]
    rootCurTime = rootNextTime
    rootNextTime = priorityOwner[signed32(uint32(rootCursor) + 1)]["time"]
```

与 tag 不同，forward root 在读取 count 后没有 `count >= 1` 或 `count != 0` gate。
所以 `INT_MIN-2` 回绕成 `INT_MAX-1`；从 cursor 0 出发仍会进入并读取 frame 1/2。
零、普通负值和其他畸形 count 也完全由回绕后的 signed limit 决定。

时间门仍是 ordered-LT break，NaN 继续。cursor 在任何 frame getter 前提交；成功进入
一轮后，可观察提交顺序为 cursor、root content、`rootCurTime = old rootNextTime`、
下一 frame 的 time。content getter 抛出时只保留新 cursor；next-time getter 抛出时
content 与 current time 已经提交。

## 6. rewind layer/tag phase

### 6.1 四端边界映射

| 边界 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| dynamic count | `0x6B6EB4` | `0x584872` | `0x10010D288` | `0x10ABE8` |
| `count != 0` gate | `0x6B6EB8` | `0x584876..0x584878` | `0x10010D28C` | `0x10ABEE..0x10ABF0` |
| initial live ordered-GT | `0x6B6EBC..0x6B6EC8` | `0x58487C..0x58488C` | `0x10010D290..0x10010D29C` | `0x10ABEC` / `0x10ADFE` loop |
| cursor decrement/store | `0x6B6EFC..0x6B6F04` | `0x5848DC..0x5848E2` | `0x10010D2D0..0x10010D2D8` | `0x10ABF6..0x10ABFE` |

### 6.2 共同伪代码

```text
count = tagOwner["count"].AsInteger32()
if count != 0 && layerCurTime > livePlayerEval:
    do:
        layerCursor = signed32(uint32(layerCursor) - 1)  // 先提交
        current = tagOwner[layerCursor]
        layerCurTime = current["time"].AsReal()
        layerNextTime = tagOwner[signed32(uint32(layerCursor) + 1)]["time"]
        if current["type"] == 1:
            run align, then sync, then action exactly as forward
    while layerCurTime > livePlayerEval
```

rewind 只检查 count 非零，负 count 也进入；它不用 count 限制游标。cursor 减一没有
零保护，零回绕为 `0xFFFFFFFF`，传入 TJS 数字属性 ABI 时同一位型被解释为 signed
`-1`。时间门使用 ordered `>`；NaN 使比较为 false，因此会停止而不是继续。
每次 current-time getter 和事件回调后，循环尾都重读 live Player time。

## 7. rewind root/priority phase

| 边界 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| priority owner copy | `0x6B7214` | `0x584A76` | `0x10010D4FC` | `0x10AE0C` |
| initial live ordered-GT | `0x6B7258..0x6B7264` | `0x584A92..0x584AA2` | `0x10010D51C..0x10010D528` | `0x10AE28..0x10AE38` |
| cursor decrement/store | `0x6B7280..0x6B7288` | `0x584AC2..0x584AC8` | `0x10010D544..0x10010D54C` | `0x10AE40..0x10AE48` |

```text
if rootCurTime > livePlayerEval:
    do:
        rootCursor = signed32(uint32(rootCursor) - 1)
        frame = priorityOwner[rootCursor]
        rootContent = frame["content"]
        rootNextTime = rootCurTime
        rootCurTime = frame["time"].AsReal()
    while rootCurTime > livePlayerEval
```

root rewind 完全不读取 count，但 aggregate 仍构造并持有 priority owner。其 ordered-GT
NaN 行为与 tag rewind 一致，cursor 零下溢也同样以 signed `-1` 进入数字 getter。
成功一轮的提交顺序为 cursor、content、next-time 的旧 current 值、current-time。
frame time getter 重入修改 evaluation 后，循环尾立即使用新值。

## 8. 本地实现修正

`cpp/plugins/motionplayer/PlayerFrameProgress.cpp` 现已：

- 为 tag 与 priority 源建立 aggregate-scope owner，并按原生先 tag、后 priority 的
  构造时点维持到四流函数尾部；
- 让四个 source-level tag/root phase 接受 owner 引用，不再重读可能已被 getter
  清空的 Player 持久字段；
- 去掉抽取 helper 的 target 快照，全部时间门重读 `_clampedEvalTime`；
- 分别恢复 forward tag 的 `count >= 1`、forward root 的无 gate、rewind tag 的
  `count != 0` 与 rewind root 的无 count 查询；
- 用显式 uint32 加、减与 `count-2` helper 保留寄存器回绕，并把位型桥接为 signed
  TJS 数字索引；
- 保留 forward ordered-LT/NaN-continue、rewind ordered-GT/NaN-stop，以及所有
  cursor/content/time/event 的异常前缀提交顺序。

`Player.h` 新增的 setup/run/observe 入口仅存在于测试 harness，不注册为 Motion.Player
脚本成员。

## 9. 回归与验证

新增 Catch2 用例覆盖以下旧实现最容易漏失的边界：

- tag align 把 evaluation 从 25 拉回 10 后，下一轮 live gate 立即停止，同时 action
  仍按 align 后顺序入队；
- forward evaluation 为 NaN 时 tag 与 root 都推进到 signed limit；
- tag owner 先构造，priority count getter 把 evaluation 15 改为 25，并清空 Player
  字段与两个最后外部 owner 后，root 仍通过局部 owner 完成；
- priority `count == INT_MIN` 的 `count-2` 回绕允许 root 读取 frame 1/2；
- 两个 aggregate owner 在 getter 内均不析构，到尾部严格以 priority、tag 逆序释放；
- rewind tag count 仅作非零测试，frame getter 重入后 cursor 0 继续下溢到 `-1`；
- rewind root 不查询 count，并在 live time 再次变化后同样读取 `-1` frame。

验证结果：

- 完整 motionplayer Catch2 翻译单元以 Web Debug 的真实 Emscripten 参数执行
  `-fsyntax-only` 成功，仅有仓库既有 `_tss` warning；
- `Web Debug Build` 完整 33-step 增量构建与最终 archive/index/Wasm 链接成功；
- 四份 recovery IDB 均写入 owner、wrap、live-time、commit 与 release 注释/书签，
  并分别取得 `idb_save ok=true`。

## 10. 相邻 node phase 已闭合

最后的 non-root node incremental phase 已继续以 fresh 四端证据独立闭合：node deque
使用 `1..<live size`，parameterized node 在两个方向调用同一共享 stepper；ordinary
forward 建立 count-only frame-list owner 并使用 live ordered-LT，rewind 不读 count、
不建 owner并使用 live ordered-GT。两方向的 selector、wrapping index、parse/action、
exact `flags=1`、physical merge 与异常提交前缀均已恢复；旧 source gate 的
`nodeType` 范围保护也由四端 direct shift 证明为端口杜撰并移除。完整字段布局、地址、
平台 shift 差异和回归见
`analysis/motionplayer_node_incremental_seek_four_binary_2026-08-15.md`。
