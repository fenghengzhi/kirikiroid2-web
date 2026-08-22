# Motionplayer collection child serialize wrappers 四端复原（2026-08-15）

## 范围与结论

本纵切面重新反编译 `reference/binaries/` 的四个当前参考目标，闭合
Eye、Eyebrow、Mouth、Transition、Selector 五个 collection serializer，以及
Eye/Eyebrow 共用的 request-queue serializer。旧 `libkrkr2.so` 地址和本地旧注释不作为
证据。

五个 wrapper 都先创建一份新的原生 TJS Array，并按对应 Engine deque 的物理顺序输出
一个 Dictionary item，不筛选、不排序、不按 label 去重。最关键的新结论是 item 的所有权
不是统一的“局部 `tTJSVariant` 持有 Dictionary 到循环尾”：

- Eye、Eyebrow、Mouth、Selector 以及 request queue 直接用
  `ncbPropAccessor(TJSCreateDictionaryObject(), false)` 接管 Dictionary factory reference，
  写完字段后把 `{dispatch, dispatch}` Object closure 发布进 Array，再由 accessor 释放
  factory reference；
- Transition 先取得 Var-controller serializer 返回的 Object Variant，再让 accessor
  `AsObject`/AddRef，随后在写 `label` 之前销毁原 Variant；最后同样发布
  `{dispatch, dispatch}` closure。

所有 `PropSet` 都使用 `TJS_MEMBERENSURE` 和对应进程级 shared hint，并忽略 HRESULT。
因此普通失败不会跳过 item：缺字段或部分写入的 Dictionary 仍会被 append。异常则阻止
整个 Array 返回；已完成的前缀由局部 Array owner 清理，不会作为部分结果交给脚本。

## 四端函数映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| Eye collection | `0x673EEC` / `0x43C` | `0x55C290` / `0x1CC` | `0x1001B008C` / `0x2B0` | `0x1AF838` / `0x2F0` |
| Eyebrow collection | `0x674328` / `0x43C` | `0x55C500` / `0x1CC` | `0x1001B03A0` / `0x2B0` | `0x1AFBB4` / `0x2F0` |
| Mouth collection | `0x674764` / `0x338` | `0x55C770` / `0x1A2` | `0x1001B06B4` / `0x29C` | `0x1AFF30` / `0x2EA` |
| Transition collection | `0x674A9C` / `0x234` | `0x55C9A4` / `0xD2` | `0x1001B09A0` / `0x174` | `0x1B0294` / `0x188` |
| Selector collection | `0x674CD0` / `0x2B8` | `0x55CAD0` / `0x130` | `0x1001B0B6C` / `0x208` | `0x1B04A0` / `0x226` |
| request queue | `0x679474` / `0x20C` | `0x55F560` / `0xD2` | `0x1001B3250` / `0x17C` | `0x1B2D88` / `0x174` |

表中每格为“入口 / IDA 函数大小”。五个 collection 名和 request-queue 名都继续保留
`_guess`，因为 stripped 发布物没有可恢复的原始 C++ 符号。

## 固定输出 schema 与字段来源

字段插入顺序在四端完全一致：

| collection | Dictionary 写入顺序 | 值来源 |
|---|---|---|
| Eye | `label, phase, frame, v, target, length, lengthDone, exponent, speed, rq` | entry label；Blink `trackState, trackValue, trackDir, trackTarget, trackSpan, trackAccum, trackPow, trackInvDur`；secondary pair deque |
| Eyebrow | `label, phase, frame, v, target, length, lengthDone, exponent, speed, rq` | entry label；Eyebrow 同名语义字段；secondary pair deque |
| Mouth | `label, phase, mouth, frame, prev, target, tick, exponent, speed` | entry label；`state, beginFrame, currentValue, startVal, endVal, accum, powField, invDur` |
| Transition | `phase, tick, speed, exponent, frame, prev, target, label` | Var serializer 的七字段结果；entry label 最后补入 |
| Selector | `label, value, phase, speed, tick` | entry label；`selectedIndex, selState, invDuration, accum` |
| request pair | `p0, p1` | `std::pair<float,float>::first/second` |

Eye 与 Eyebrow 的 schema 相同，但 controller 的物理标量布局不同：Eyebrow 的
`trackAccum/trackSpan` 和 `trackPow/trackInvDur` 相对 Eye 对调。四端 serializer 都按
语义角色读取，当前 portable struct 也必须保留两种独立 class，而不能因输出 key 相同而
合并成一个共同尾布局。

Selector 只保存五个标量。它的 command deque、option vector、entry gate byte 和 dormant
targets vector 均不进入 snapshot。Transition 则复用完整 Var-controller serializer；
`label` 的插入时机确实在七个 Var 字段之后。

## 普通 item 的 factory/accessor/Array 所有权

Eye、Eyebrow、Mouth、Selector 和 request pair 共同伪代码为：

```text
arrayClosure, rawItems = createTJSArrayWithItems()
for entry in sourceDeque order:
    dispatch = TJSCreateDictionaryObject()            // factory ref = 1
    accessor = ncbPropAccessor(dispatch, addref=false) // directly owns ref

    accessor.SetValue(key0, value0, MEMBERENSURE, hint0)
    ...                                                // ignore every bool/HRESULT
    accessor.SetValue(keyN, valueN, MEMBERENSURE, hintN)

    rawItems.emplace_back(dispatch, dispatch)          // two closure AddRefs
    destroy accessor                                   // one factory Release
return copy(arrayClosure); destroy local arrayClosure
```

`ncbPropAccessor` 的栈对象在四端都能直接识别为 vptr 加一个 dispatch；Dictionary factory
调用后没有额外 AddRef，析构只做一次 Release。Array item 的 native
`tTJSVariant` 则同时保存 Object 与 ObjThis，两槽都是同一个 Dictionary dispatch；fast
push path 在四端都能看到两次 AddRef，然后写 type=Object。这样 accessor 析构后仍由
Array item 的两条 closure 引用拥有 Dictionary。

原生不是先建立一份拥有 Dictionary 的 Object Variant、再借用 dispatch 写字段。当前
源码因此恢复为 accessor 直接接管 factory reference，并用
`items.emplace_back(dispatch, dispatch)` 发布 closure。

## Transition 的 Variant-to-accessor handoff

Transition 是五类中唯一不新建空 Dictionary 的 wrapper。共同顺序为：

```text
stateVariant = serializeVarControllerState(controller)
force stateVariant to Object if necessary
itemAccessor = ncbPropAccessor(stateVariant)  // AsObject AddRef
destroy stateVariant                         // before label PropSet

itemAccessor.SetValue("label", entry.label,
                      MEMBERENSURE, engineLabelHint)
rawItems.emplace_back(itemAccessor.dispatch,
                      itemAccessor.dispatch)
destroy itemAccessor
```

Var serializer 正常返回的 Dictionary closure 中 Object 与 ObjThis 相同，因此 accessor
提前接管一条引用后销毁 Variant不会销毁对象。发布新 closure 又建立 Object/ObjThis 两条
引用，最后 accessor Release。这个顺序影响异常清理和可观察的 AddRef/Release 边界，不能
简化成“Variant 一直活到 push 完成”。

四端 Transition 的 `label` PropSet 都使用与 builders、timeline 和其它 collection state
共用的 `engineLabel` hint：`0x1AB4F18 / 0x11114B0 / 0x101B69FC8 /
0x187D9E8`。旧本地调用漏传 hint，本轮已接回同一 portable slot。

## request queue 的嵌套 Array

Eye/Eyebrow 在写 `rq` 前调用共同 request-queue serializer。该 helper 自己创建另一份新
Array，按 secondary deque 顺序为每个 pair 建 Dictionary，严格先写 `p0`、再写 `p1`，
然后用同一 accessor-owned factory-ref 协议发布 Object closure。

外层 Eye/Eyebrow `SetValue("rq", returnedVariant, ...)` 会再构造用于 PropSet 的局部
Variant 副本；返回的 request-queue Variant 在调用结束后释放。`rq` Dictionary 属性持有
自己的 Array closure 引用，因此嵌套 Array 在外层 item 发布后继续存活。request queue
为空时仍写入一份新的空 Array，不省略 `rq`。

`p0/p1` 使用 restore 侧已经确认的共享 hint：

| key | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `p0` | `0x1AB4EE4` | `0x111147C` | `0x101B69F94` | `0x187D9B4` |
| `p1` | `0x1AB4EE8` | `0x1111480` | `0x101B69F98` | `0x187D9B8` |

其它 controller key 继续复用
`analysis/motionplayer_angle_controller_lifecycle_four_binary_2026-08-11.md` 和
`analysis/motionplayer_controller_state_restore_family_four_binary_2026-08-15.md` 已建立的
`phase/frame/v/target/length/lengthDone/exponent/speed/rq/mouth/prev/tick/value` 槽；
serialize 与 restore 没有各自独立的同名 cache。

## deque 与 Array Items ABI

collection wrapper 直接遍历 Engine 内部 deque，没有生成 vector snapshot。四端 record
宽度和 block 容量为：

| 容器 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---|---|---|---|
| Eye/Eyebrow entry | `16B × 32 = 512B` | `8B × 64 = 512B` | `16B × 256 = 4096B` | `8B × 512 = 4096B` |
| Mouth/Transition entry | `24B × 21 = 504B` | `12B × 42 = 504B` | `24B × 170 = 4080B` | `12B × 341 = 4092B` |
| Selector entry | `48B × 10 = 480B` | `24B × 21 = 504B` | `48B × 85 = 4080B` | `24B × 170 = 4080B` |
| request pair | `8B × 64 = 512B` | `8B × 64 = 512B` | `8B × 512 = 4096B` | `8B × 512 = 4096B` |

输出 Array 的 `tTJSArrayNI::Items` 则是 `std::vector<tTJSVariant>`：

| 目标 | Variant 宽度 | native vector grow block/capacity 证据 |
|---|---:|---:|
| Android ARM64 | `20B` | `500B / 25 items` |
| Android ARMv7 | `12B` | `504B / 42 items` |
| iOS ARM64 | `20B` | `4080B / 204 items` |
| iOS ARMv7 | `12B` | `4092B / 341 items` |

这些数字解释反编译中的 block crossing 和 push fast path，是 libstdc++/libc++ ABI 细节；
portable 源码继续使用标准 deque/vector，不能硬编码 native block 算术。

## 边界与异常行为

- 源 deque 为空时仍返回新的空 Array；连续两次 serialize 不共享 Array owner。
- wrapper 不检查 entry controller owner 是否为空；读取字段时自然解引用。Transition 的
  Var serializer同样不接受 null controller。
- `createTJSArrayWithItems` 的 raw `Items *` 不单独持有 native instance。wrapper 不检查
  它是否为空；native-instance 获取失败后的首次 append 会按原生未防御边界失败。
- Dictionary factory 返回 null 时 accessor 也没有友好 guard；第一次 `SetValue` 走原生
  null-dispatch 边界。
- 每个 `SetValue` 都忽略返回值。一个成员写失败但没有抛异常时，后续成员仍继续写，item
  仍 append；没有字段级 rollback。
- item 只有在所有字段写入步骤完成后才 append。某字段转换/分配抛异常时，当前 accessor
  释放 Dictionary；外层 Array owner 再释放已 append 的前缀，调用者得不到部分结果。
- Array grow 在 closure 构造或容器扩容时抛异常，也由当前 accessor 与 Array owner 清理；
  没有额外 raw Dictionary leak。
- serializer 不正规化未初始化/非有限 controller 标量；只要调用发生，就按当时内存中的
  float/int 位型构造 Variant。Eye/Eyebrow 首次动画 setup 前的尾部初始化边界因此保持。
- 输出顺序是 deque 顺序，不是 label 字典序。重复 label、空 label 都逐项保留。

## 本地恢复、IDB 回写与验证

- `serializeRequestQueue_guess` 改为 accessor 直接拥有 Dictionary factory reference，按
  `p0/p1` 顺序 `SetValue` 后发布 `{dispatch,dispatch}` closure。
- Eye/Eyebrow/Mouth/Selector 的 controller-item helper 改为向已有 retained accessor 写入；
  五个 wrapper 中四类直接创建 accessor-owned Dictionary 并发布 closure。
- Transition 恢复 Var-state Variant -> force Object -> accessor retain -> Variant early Clear
  -> shared-hint label PropSet -> closure publish 的独立生命周期。
- `EmoteEngine.h` 补充五类 collection serializer 的共同所有权、顺序与异常返回边界；
  compiled-source 注释不含任何单目标绝对地址。
- 四份 recovery IDB 的六个入口均补入字段顺序、accessor ownership、closure publication、
  平台 deque/block ABI 注释和 bookmark；最终四库已原位保存。
- 使用真实 Emscripten response file 的 `motionplayer-dll.cpp -fsyntax-only` 通过，仅有既有
  `_tss` warning。
- `cmake --build --preset "Web Debug Build"` 完成 10 个增量步骤，重新编译相关
  motionplayer/NCB 对象，成功生成静态库并链接最终 `index.html`；输出仅含仓库既有
  `_tss`、imagepacker、pthread memory-growth、JSPI 与 JS library warnings。
- 定向 `git diff --check` 通过；换行转换提示不是 whitespace error。

本页闭合 collection 保存侧，与
`analysis/motionplayer_collection_child_restore_wrappers_four_binary_2026-08-15.md` 的恢复侧
共同覆盖五类 controller Array 的往返生命周期；这仍不代表整个 motionplayer 已达到
100% 复原。
