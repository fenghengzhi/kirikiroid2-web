# Timeline `time/content` 独立 member-hint pair（四参考，2026-08-16）

## 结论

`EmoteEngine::initializeTimelineState_guess` 解码 timeline frame 时读取的 `"time"` 和
`"content"`，与 `MotionNodeFrameSlot`/`Player::skipToSync` 使用的同名 member 不共享 hint。

四个当前参考二进制都存在两组地址身份不同的连续 pair：

- timeline pair：`timelineTimeHint_guess` / `timelineContentHint_guess`，只有 timeline-state
  initializer 这一类语义 consumer；
- broader frame pair：`timeMemberHint_guess` / `contentMemberHint_guess`，由
  `MotionNodeFrameSlot::parse`、content merge 和 `Player::skipToSync` 使用。

因此当前源码把 timeline pair 保留在 `EmoteEngine.cpp` 的独立 family 是正确的。V166/V167
证明了若干同名 key 确实共享，但不能据此机械合并所有同名字符串；本纵切面记录的是同样
重要的四端负 identity 证据。

## 函数映射

| 参考 | `EmoteEngine_initializeTimelineState_guess` | 大小 |
|---|---:|---:|
| Android arm64 | `0x66D03C` | `0xBE4` |
| Android armv7 | `0x5590E8` | `0x4E6` |
| iOS arm64 | `0x1001ABD5C` | `0x5D0` |
| iOS armv7 | `0x1AB4B0` | `0x5B6` |

## 两组 backing word

| 参考 | timeline `time` | timeline `content` | broader `time` | broader `content` |
|---|---:|---:|---:|---:|
| Android arm64 | `0x1AB4F90` | `0x1AB4F94` | `0x1AB5120` | `0x1AB5128` |
| Android armv7 | `0x1111528` | `0x111152C` | `0x1111654` | `0x111165C` |
| iOS arm64 | `0x101B6A040` | `0x101B6A044` | `0x101B695E8` | `0x101B695F0` |
| iOS armv7 | `0x187DA60` | `0x187DA64` | `0x187D318` | `0x187D320` |

每一组内部的 `content` 都严格等于 `time + 4`。两组之间则不相邻：

- Android arm64：broader `time` 比 timeline `time` 高 `0x190`；
- Android armv7：高 `0x12C`；
- iOS arm64：timeline `time` 比 broader `time` 高 `0xA58`；
- iOS armv7：高 `0x748`。

这种四端 address identity/ordering 比 literal 拼写更强；不存在 linker 偶然折叠或单平台
布局巧合可以解释成同一变量。

## xref consumer 集合

### Timeline pair

代表性 call/data-xref head：

| 参考 | timeline `time` | timeline `content` |
|---|---:|---:|
| Android arm64 | `0x66D71C` / `0x66D730` | `0x66D78C` / `0x66D79C` |
| Android armv7 | `0x5593FC` / `0x559408` | `0x55945C` / `0x55946A` |
| iOS arm64 | `0x1001ABF14` | `0x1001AC180` |
| iOS armv7 | `0x1AB864` / `0x1AB86A` / `0x1AB870` | `0x1AB8DA` / `0x1AB8E0` / `0x1AB8E6` |

四端语义 consumer 都只有 `EmoteEngine_initializeTimelineState_guess`。Android armv7 的
额外 null-function xref 是 recovery function-chunk/materialization 边界，不形成第二个
源码 consumer。

### Broader pair

`timeMemberHint_guess` 的语义 consumers：

- `MotionNodeFrameSlot_parse_guess`；
- `Player_skipToSync_guess`。

`contentMemberHint_guess` 的语义 consumers：

- `MotionNodeFrameSlot_parse_guess`；
- `MotionNodeFrameSlot_mergeContent_guess`；
- `Player_skipToSync_guess`。

例如 Android arm64 broader `time` xrefs 位于 `0x68FBA4/0x68FBAC` 和
`0x6D09B4/0x6D09CC`；broader `content` 位于 `0x68FC2C/0x68FC34`、
`0x68FFAC/0x68FFB4`、`0x6D0A9C/0x6D0AAC`。其他三端得到相同语义集合。

## 数据流边界

Timeline initializer 的相关共同顺序保持为：

```text
raw frame Variant
 -> retained frameObject accessor
 -> GetValue<real>("time", flags=0, timelineTimeHint)
 -> GetValue<int>("type", flags=0, engineTypeHint)
 -> if type != 0:
      GetValue<Variant>("content", flags=0, timelineContentHint)
      -> retained contentObject accessor
      -> value/easing reads
```

这两个 timeline word 只改变 TJS dispatch 的缓存参数，不是 TimelineData/Frame 的持久
字段。typed helper 仍忽略 ordinary HRESULT、消费 getter 写出的 Variant，并在转换/复制后
析构临时；本轮不改变已闭合的 accessor owner 和 Track/Frame 渐进提交顺序。

## 源码与 IDB

源码行为不需要修正。`EmoteEngine.cpp` 的 family 注释已明确：timeline `time/content`
尽管拼写相同，四端都不复用 broader node-frame family，防止后续机械去重。

四个 recovery IDB 均完成：

- timeline pair 的 8 个地址重建为独立 size-4 `unsigned int` data item；
- 命名为 `timelineTimeHint_guess` / `timelineContentHint_guess`；
- 两个 data item、initializer 入口与两处代表性 call operand 写入 V168 注释；
- 增加 `V168 timeline time/content distinct hint pair` bookmark；
- 四端 initializer 强制重编译；
- decompiler 因 address-materialization 形式仍可能把实参渲染成 `MEMORY[address]`，但
  typed data entity、直接 xref target 与注释 readback 均指向新命名的精确独立边界；
- 四库均原位保存成功。

## 验证范围

本纵切面没有改变可执行语句或测试逻辑；现有 timeline nested-accessor probe 仍覆盖
flags=0、hint non-null、receiver、source owner 和普通失败 status 的消费行为。新的贡献是
四参考地址 identity、consumer 集合与 IDB data boundary，不把“hint 非空”误写成“两个
family 已证明共享”。

