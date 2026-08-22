# Motionplayer Var / Angle state serializer Dictionary 所有权四端复原（2026-08-15）

## 范围与结论

本纵切面重新反编译 `reference/binaries/` 的四个当前参考目标，只以四端发布物的共同
行为为依据，闭合 `EmoteVarController` 与 `EmoteAngleController` 的状态保存函数。旧
`libkrkr2.so` 地址和 recovery IDB 中既有注释均不作为证据。

两类 serializer 都不是先让局部 `tTJSVariant` 拥有 Dictionary，再通过通用 property
helper 写入。它们直接构造
`ncbPropAccessor(TJSCreateDictionaryObject(), false)`，由 accessor 接管 factory reference；
所有字段均以 `TJS_MEMBERENSURE` 和进程级共享 hint 写入，忽略 `PropSet` 返回值；最后在
accessor 析构之前以 `{dispatch, dispatch}` 构造返回 Object closure。返回 Variant 对同一
dispatch 建立两条引用，随后 accessor 只释放原 factory reference。

Var 的后三个字段是每次调用新建的三个 TJS Array；Angle 的后三个字段则是标量。两类固定
字段顺序分别为：

- Var：`phase, tick, speed, exponent, frame, prev, target`，其中后三项为 Array；
- Angle：`phase, tick, speed, exponent, frame, prev, target`，七项均为标量。

本轮同时发现并纠正一条四份 recovery IDB 都曾携带的旧注释：Var 的正确指针角色为
`frame=currentValue`、`prev=startValue`、`target=targetValue`。旧注释把后两者解释成
`prev=targetValue`、`target=startValue`，与四端当前反编译、step 语义和 restore 写回路径
都冲突。

## 四端函数映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| Var state serializer | `0x664A5C` / `0x460` | `0x554384` / `0x1B0` | `0x1001A4DCC` / `0x280` | `0x1A42A0` / `0x2B8` |
| Angle state serializer | `0x663C10` / `0x1E4` | `0x553D58` / `0x11C` | `0x1001A45B8` / `0x180` | `0x1A3A24` / `0x1F4` |

表中每格为“入口 / IDA 函数大小”。发布物已 stripped，两个恢复名称继续保留 `_guess`；
源码和 IDB 不把这些语义名冒充成原始 C++ 符号。

## 共同 Dictionary owner 与返回 closure 协议

两类函数的共同骨架为：

```text
dispatch = TJSCreateDictionaryObject()       // factory reference = 1
object = ncbPropAccessor(dispatch, false)    // directly owns that reference

object.SetValue("phase",   ..., MEMBERENSURE, sharedPhaseHint)
object.SetValue("tick",    ..., MEMBERENSURE, sharedTickHint)
object.SetValue("speed",   ..., MEMBERENSURE, sharedSpeedHint)
object.SetValue("exponent",..., MEMBERENSURE, sharedExponentHint)
object.SetValue("frame",   ..., MEMBERENSURE, sharedFrameHint)
object.SetValue("prev",    ..., MEMBERENSURE, sharedPrevHint)
object.SetValue("target",  ..., MEMBERENSURE, sharedTargetHint)
                                                // every HRESULT ignored

return tTJSVariant(dispatch, dispatch)         // Object + ObjThis AddRef
destroy object                                // releases factory reference
```

四端尾部都能看到返回值 type 被写为 Object，Object 与 ObjThis 两槽写入同一 dispatch，并在
accessor 析构前执行两次 AddRef。这个顺序保证 factory owner 退出后，调用者仍拥有完整
Object closure。若把 accessor 换成借用 raw dispatch 的 helper，或者直接返回只含 Object
不含 ObjThis 的 Variant，引用计数和调用上下文都会偏离发布物。

所有 `SetValue` 都复用 restore、Eye/Eyebrow/Mouth/Selector collection 以及 timeline
serializer 已确认的七个 shared member-hint 槽；不存在 Var-only 或 Angle-only 的第二组
同名 cache。各槽的四端地址已记录在
`analysis/motionplayer_angle_controller_lifecycle_four_binary_2026-08-11.md` 和
`analysis/motionplayer_controller_state_restore_family_four_binary_2026-08-15.md`，这里不再
复制一份可能漂移的地址表。

## Var：标量、三个 fresh Array 与角色校正

Var 首先写四个标量：

| key | controller 来源 |
|---|---|
| `phase` | `state` |
| `tick` | `phase` |
| `speed` | `invDuration` |
| `exponent` | `powCount` |

之后连续创建三份独立 Array，并按 channel index 从 `0` 到 `count - 1` append float：

| key | Array 元素来源 | 语义 |
|---|---|---|
| `frame` | `currentValue[index]` | 当前输出值 |
| `prev` | `startValue[index]` | 本次插值起点 |
| `target` | `targetValue[index]` | 本次插值目标 |

`count` 是有符号整数。四端都以“`count >= 1` 才进入循环”等价控制流实现，因此
`count == 0` 或负数时不会读取三个 channel 指针，而是仍创建并写入三份 fresh empty Array。
三个 Array 不共享 native instance，也不复用上一次 serialize 的结果。

每个 Array 先由 `createTJSArrayWithItems_guess` 返回局部 owning Variant 和 raw
`tTJSArrayNI::Items *`，循环直接向 native vector 追加 Real Variant。用于 Dictionary
`PropSet` 的 Array Variant 是一份局部副本；属性写入调用返回后，局部 Array owner 立即
释放，而 Dictionary 成功写入时持有自己的 closure reference。即使 `PropSet` 返回失败，
serializer 仍继续创建下一个字段，不作字段级 rollback。

正确的四端 Var 布局是：

| 字段 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `count` / `state` | `+80 / +84` | `+40 / +44` | `+48 / +52` | `+24 / +28` |
| `currentValue` | `+88` | `+48` | `+56` | `+32` |
| `startValue` | `+96` | `+52` | `+64` | `+36` |
| `targetValue` | `+104` | `+56` | `+72` | `+40` |
| `powCount` / `phase` / `invDuration` | `+112 / +116 / +120` | `+60 / +64 / +68` | `+80 / +84 / +88` | `+44 / +48 / +52` |

这里的角色不是仅凭 serializer key 猜测：四端 Var step 都以 `startValue` 作为插值起点、
`targetValue` 作为终点，四端 restore 又分别把 `prev` 写回 `startValue`、把 `target` 写回
`targetValue`。三条独立证据共同排除了旧注释的反向解释。

## Angle：七个标量的固定顺序

Angle 不创建 channel Array，完整映射为：

| key | controller 来源 |
|---|---|
| `phase` | `state` |
| `tick` | `phase` |
| `speed` | `invDuration` |
| `exponent` | `powCount` |
| `frame` | `currentRad` |
| `prev` | `startRad` |
| `target` | `targetRad` |

四端对象布局为：

| 字段顺序 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `state/currentRad/targetRad/startRad/invDuration/powCount/phase` | `+80/+84/+88/+92/+96/+100/+104` | `+40/+44/+48/+52/+56/+60/+64` | `+48/+52/+56/+60/+64/+68/+72` | `+24/+28/+32/+36/+40/+44/+48` |

保存侧确实把 `target` 读取自 `targetRad`。这与发布版恢复侧的已确认 bug 不对称：restore
会把 `prev` 和 `target` 都写入 `startRad`，从不恢复 `targetRad`。serializer 不应为了制造
表面对称而复制这个 restore bug。

## 边界、失败与异常行为

- 两个入口都不检查 controller 指针；null 会在首个字段读取时沿原生未防御边界失败。
- Dictionary factory 返回 null 时 accessor 没有额外友好 guard；首次 `SetValue` 进入原生
  null-dispatch 边界。
- 所有 `PropSet` 使用 `TJS_MEMBERENSURE`，但返回状态均被忽略。普通失败只会留下缺字段或
  部分字段 Dictionary；函数继续后续字段并最终返回该对象。
- 字段值不做 finite、范围或状态正规化；NaN、Infinity、负 phase/count 之外的原始标量
  都按当前内存值构造 Variant。
- Var 不检查三个 channel 指针。仅当有符号 `count > 0` 时才解引用；正数 count 配 null 或
  短缓冲区仍按发布物的未防御边界访问。
- Array native-instance 或 raw `Items *` 获取失败没有显式 guard；第一个 append 直接使用
  raw pointer。
- `PropSet` 抛异常时不返回部分 Dictionary。当前字段的临时 Variant/Array owner、先前
  已写入的属性和 accessor-owned Dictionary 按栈展开释放；此前对外没有发布返回 closure。
- 返回 closure 构造本身若在 AddRef 路径抛出，accessor 仍负责释放 factory reference；
  没有额外 raw owner 转移点。

## 本地恢复、IDB 回写与验证

- `serializeVarControllerState_guess` 和 `serializeAngleControllerState_guess` 已改为直接以
  `ncbPropAccessor(TJSCreateDictionaryObject(), false)` 持有 Dictionary，逐字段
  `SetValue(..., TJS_MEMBERENSURE, sharedHint)`，最后返回 `{dispatch,dispatch}` closure。
- Var 保留三份 fresh Array、signed-count 顺序遍历以及
  `currentValue/startValue/targetValue` 的正确角色；没有把 native vector grow 算术硬编码进
  portable 源码。
- 四份 recovery IDB 的两个入口均已写入 owner、schema、字段角色和平台布局注释及
  bookmark。由于旧 Var 说明位于非重复函数注释槽，本轮通过 IDAPython 同时替换
  non-repeatable 与 repeatable function comment，再重新打开四库逐一核验；错误的
  `prev/target` 反向说明已不再出现。
- 使用真实 Emscripten response file 的 `motionplayer-dll.cpp -fsyntax-only` 通过，仅有既有
  `_tss` warning。
- `cmake --build --preset "Web Debug Build"` 完成 3 个增量步骤，重新编译
  `EmoteEngine.cpp`、生成 motionplayer 静态库并成功链接最终 `index.html`；输出仅含仓库
  既有 `_tss`、pthread memory-growth、JSPI 与 JS library warnings。
- 定向 `git diff --check` 覆盖源码、计划和本页并通过；换行转换提示不是 whitespace error。

本页闭合 Var / Angle controller 的保存侧 Dictionary ownership，与
`analysis/motionplayer_controller_state_restore_family_four_binary_2026-08-15.md` 以及
`analysis/motionplayer_angle_controller_lifecycle_four_binary_2026-08-11.md` 的恢复/执行侧共同
约束字段角色；这仍不代表 motionplayer 全体已达到 100% 复原。
