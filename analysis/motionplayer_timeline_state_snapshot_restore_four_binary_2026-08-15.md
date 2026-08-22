# Timeline state snapshot / restore 四端复原（2026-08-15）

## 范围与结论

本纵切面闭合 `EmoteEngine_serializeTimelineState_guess` 与
`EmoteEngine_restoreTimelineState_guess`，包括 active-label/map 数据流、TJS Array item
生命周期、五字段 schema、member-hint 复用、按项恢复顺序和异常前缀。

Timeline restore 不是无副作用的“可选数组解析”：它无条件先执行
`stopTimeline_guess("")`，之后才检查输入是不是原生 Array。每个有效 item 又按
`flags/curTime -> play -> inclusive window -> stopWhenBlendDone -> blendRatioCtrl`
顺序即时提交，任何后续异常都不回滚前面的 stop、play 或时间窗口变更。

另一个容易遗漏的边界是 `blendRatioCtrl` strict probe 复用并覆盖
`restoreTimelineState` 的 by-value 输入 Variant 槽；不是写入独立的本地 `field`。源码本次
已恢复这个槽身份，同时为每个 Array item 建立 retained accessor。

## 四端入口映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| serialize timeline state | `0x673BC4` | `0x55C0E4` | `0x1001AFE68` | `0x1AF5BC` |
| restore timeline state | `0x675834` | `0x55D184` | `0x1001B1410` | `0x1B0EB0` |

四端都调用前一纵切面已恢复的 native Array Items extractor；`label` 复用 strict named
`ttstr` probe，`blendRatioCtrl` 则走 strict named Variant probe。Android ARM64 将部分
accessor/Variant 生命周期内联，其余目标复用非内联 helper。

## 五字段 schema、字面量与 hint

item 的发布顺序固定为：

```text
label
flags                 // serialized value is state.flags | 1
curTime
blendRatioCtrl
stopWhenBlendDone
```

普通 string search 对后三项全部返回空，反编译只显示 `c/b/s`。UTF-8、UTF-16LE、
UTF-32LE raw search 与 xref 筛选确认 state 字面量是下列 UTF-16LE 项；UTF-32LE 均无
命中。

| key | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `label` | `0x14BF74C` | `0xD772AC` | `0x10195FD00` | `0x1752064` |
| `flags` | `0x14D3AE0` | `0xD84512` | `0x10195FF2A` | `0x175228E` |
| `curTime` | `0x14D3B74` | `0xD845A6` | `0x10195FFBE` | `0x1752322` |
| `blendRatioCtrl` | `0x14D3B84` | `0xD845B6` | `0x10195FFCE` | `0x1752332` |
| `stopWhenBlendDone` | `0x14D3BA2` | `0xD845D4` | `0x10195FFEC` | `0x1752350` |

同一字段的 serialize 与 restore 共享同一 mutable hint：

| key | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `label` | `0x1AB4F18` | `0x11114B0` | `0x101B69FC8` | `0x187D9E8` |
| `flags` | `0x1AB4F9C` | `0x1111534` | `0x101B6A04C` | `0x187DA6C` |
| `curTime` | `0x1AB4FC0` | `0x1111558` | `0x101B6A070` | `0x187DA90` |
| `blendRatioCtrl` | `0x1AB4FC4` | `0x111155C` | `0x101B6A074` | `0x187DA94` |
| `stopWhenBlendDone` | `0x1AB4FC8` | `0x1111560` | `0x101B6A078` | `0x187DA98` |

`label` 是 Engine builders、controller collections、timeline 与 Player 初始化共用的全局
槽；`flags` 还与 `getPlayingTimelineInfoList` 复用。后三项只由 timeline snapshot 的
serialize/restore 使用，因此源码新增三个 timeline-state `_guess` 槽，继续复用既有
`engineLabelHint_guess` 和 `timelineInfoFlagsHint_guess`。

## serialize 数据流与所有权

四端共同伪代码：

```text
out = fresh TJS Array closure + borrowed native Items pointer
for label in activeTimelineLabels, preserving vector order:
    state = timelineStates[label]               // unordered_map::operator[]
    item = fresh TJS Dictionary
    item.PropSet(MEMBERENSURE, "label", label)
    item.PropSet(MEMBERENSURE, "flags", state.flags | 1)
    item.PropSet(MEMBERENSURE, "curTime", state.currentTime)
    item.PropSet(MEMBERENSURE, "blendRatioCtrl",
                 serializeVarController(state.blendController))
    item.PropSet(MEMBERENSURE, "stopWhenBlendDone", state.autoStop)
    out.Items.push_back(item closure)
return owning Array Variant
```

这里故意使用 `unordered_map::operator[]`，不是 `find`：active-label vector 若残留一个 map
中不存在的 label，会先 materialize 默认 `EmoteTimelineState`，随后 serializer 继续解引用
其 blend-controller owner；不会静默跳过。这个副作用发生在 item 完整发布之前，后续
异常不删除新插入的 map node。

每个 child Variant 在下一字段之前销毁；item Dictionary 在 push 到 Array 后由 Array
closure 持有，局部 accessor/Variant 随后释放。字段构建和 push 均逐项完成，没有 staging
或整批回滚。

## restore 数据流、item accessor 与提交顺序

四端共同流程：

```text
stopTimeline("")                                  // before every type gate
if input.Type != Object: return
items = force Object + query native Array Items
if items == null: return

for rawItem in items order:
    if rawItem.Type != Object: continue
    copy rawItem closure
    force Object; accessor AddRef dispatch
    destroy copied Variant before first property probe

    strict get-and-convert label directly into ttstr; if missing: continue
    statePtr = timelineStates.find(label)
    if missing: continue

    flags = 0; strict scalar get flags if present
    curTime = 0; strict scalar get curTime if present
    playTimeline(label, flags)
    applyTimelineWindow(statePtr, inclusive=true, curTime)
    strict scalar get stopWhenBlendDone into state.autoStop if present
    strict Variant get blendRatioCtrl into reusable input Variant slot
    if present: restoreVarController(state.blendController, inputSlot)

    accessor Release on normal/exceptional item exit
```

item 不是直接借用 Array 中的 Variant dispatch：native accessor 明确 retain Object，且临时
Variant 在第一次 getter 前销毁。这在 getter 脚本可重入、修改原 Array 或抛异常时固定了
item dispatch 的寿命；源码此前只用 `AsObjectNoAddRef` 借用，现已恢复 retained accessor。

`label` 缺失、label 不在 `_timelineStates`、或 item 非 Object 都只跳过该 item。找到 state
后则不再事务化：`playTimeline` 可能改变 active-label/vector 状态，inclusive window 又会
推进 cursor/controller；之后 `stopWhenBlendDone` 或 `blendRatioCtrl` getter/转换抛错时，
这些前缀保留。`blendRatioCtrl` 缺失时 strict probe 不覆盖输入 Variant 槽，也不恢复 blend
controller。

## Array Items 的 ABI 容器差异

源码保持 portable `tTJSArrayNI::Items` 迭代，但四端块布局可从循环边界直接恢复：

| 目标 | Variant 大小 | block payload / 元素数 | 实现族 |
|---|---:|---:|---|
| Android ARM64 | 20B | 500B / 25 | 旧 libstdc++ deque，`512 / sizeof(T)` |
| Android ARMv7 | 12B | 504B / 42 | 旧 libstdc++ deque，`512 / sizeof(T)` |
| iOS ARM64 | 20B | 4080B / 204 | libc++ deque，约 4KiB block |
| iOS ARMv7 | 12B | 4092B / 341 | libc++ deque，约 4KiB block |

这些 block/index 公式是 ABI 细节，不应硬编码进 Web 源码。共同可移植语义是顺序迭代、
按 item 即时提交和 accessor 的 dispatch retain。

## 本地修正、IDB 回写与验证

- Timeline serializer 的五个 `PropSet` 全部接回真实共享 hint；字段顺序与
  `flags | 1` 保持不变。
- restore 参数改为 by-value Variant；每个 Object item 恢复 copy/force/accessor retain/
  temporary early-destroy，`blendRatioCtrl` strict probe 复用输入槽。
- `flags/curTime/stopWhenBlendDone` 使用 accessor scalar probe；`label` 使用成功后才提交的
  strict `ttstr` 输出 probe，`blendRatioCtrl` 使用 strict Variant 输出 probe，所有调用传入
  对应 hint。
- 四份 recovery IDB 将 `curTime/blendRatioCtrl/stopWhenBlendDone` 宽字面量恢复为完整
  `_utf16_guess` 名；iOS 两端的三个独立 hint 恢复语义名。两端入口、五个 literal 与
  五个 hint 均补注释和 bookmark，四份 IDB 已原位保存。
- 真实 response-file `motionplayer-dll.cpp -fsyntax-only` 通过，仅有既有 `_tss`
  warning。
- `cmake --build --preset "Web Debug Build"` 完成 10 个增量步骤，受
  `EmoteEngine.h` 影响的引用者重新编译，成功生成静态库并链接最终 `index.html`；仅有
  既有 `_tss`、imagepacker attribute 与 Emscripten/JSPI warnings。
- 定向 `git diff --check` 通过；换行转换提示不属于 whitespace error。

本页闭合 timeline snapshot 的序列化/恢复路径；timeline 正常播放、loop/pass 与 seek
状态机由其它纵切面分别审计。
