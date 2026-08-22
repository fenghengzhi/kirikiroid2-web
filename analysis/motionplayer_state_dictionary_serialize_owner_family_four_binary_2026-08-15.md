# Motionplayer state Dictionary serializer owner 家族四端复原（2026-08-15）

## 范围与结论

本纵切面在 Var / Angle controller serializer 之后，重新反编译四个
`reference/binaries/`，闭合状态保存链上仍被通用 helper 压平的四类 Dictionary owner：

- Timeline Array 中的每个 item Dictionary；
- Base child Dictionary；
- OuterForce child Dictionary；
- 顶层 Engine state Dictionary。

四类都使用同一个发布协议：
`ncbPropAccessor(TJSCreateDictionaryObject(), false)` 直接接管 factory reference；每个字段的
child Variant 只覆盖对应一次 `PropSet`，调用后立即销毁；所有 `PropSet` 状态被忽略；最后
以 `{dispatch,dispatch}` 建立 Object/ObjThis 两条引用，再让 accessor 释放 factory
reference。Timeline item 把 closure 直接 append 到输出 Array；另外三类把 closure 作为函数
返回值交给调用者。

这不是单纯的代码风格差异。此前源码由 `createTJSDictionary_guess()` 先构造 owning
Variant，再由 `setTJSProperty()` 借用其 dispatch。最终正常结果相同，但 factory reference
的 owner 身份、每个 child 临时量的存活区间、异常展开顺序，以及 Timeline item 发布时的
closure 构造点都与四端发布物不同。本轮把四类恢复为 native owner 结构。

## 四端函数映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| top-level Engine state | `0x673220` / `0x638` | `0x55BB70` / `0x1D6` | `0x1001AF774` / `0x294` | `0x1AEF30` / `0x2D0` |
| Timeline state Array | `0x673BC4` / `0x328` | `0x55C0E4` / `0x12C` | `0x1001AFE68` / `0x1B8` | `0x1AF5BC` / `0x1E6` |
| Base state | `0x674F88` / `0x280` | `0x55CC70` / `0x10C` | `0x1001B0DC4` / `0x170` | `0x1B073C` / `0x1C0` |
| OuterForce state | `0x675208` / `0x21C` | `0x55CDF0` / `0xE6` | `0x1001B0F98` / `0x13C` | `0x1B0980` / `0x184` |

表中每格为“入口 / IDA 函数大小”。函数语义名都继续保留 `_guess`；stripped 发布物没有
提供可恢复的原始 C++ 符号。

四端 Dictionary factory 调用分别显示为 Android ARM64 `sub_9C6D40(0)`、Android ARMv7
`sub_7384A8(0)`、iOS ARM64 `sub_1000A7A38(0)`、iOS ARMv7 `sub_A6900(0)`。返回 dispatch
直接写入带 accessor vptr 的栈对象，没有中间 owning Object Variant。

## 共同 factory / accessor / closure 生命周期

Base、OuterForce 和顶层返回 Dictionary 的共同骨架为：

```text
dispatch = TJSCreateDictionaryObject()       // factory reference = 1
dict = ncbPropAccessor(dispatch, false)      // direct owner; no AddRef here

for child in fixed schema order:
    childValue = serializeChild()            // owning local Variant
    dict.PropSet(MEMBERENSURE, key, hint, childValue, dict.dispatch)
    destroy childValue                       // regardless of PropSet HRESULT

AddRef(dispatch)                             // result.Object
AddRef(dispatch)                             // result.ObjThis
result.Type = Object
destroy dict                                 // Release factory reference
return result
```

四端 64-bit 结尾可直接看到两次同一 dispatch 的虚调用 AddRef、写入两个 8-byte closure
槽、写 Object type，随后 accessor vptr 切换并 Release。32-bit 两端执行同构的两次 AddRef、
写两个 4-byte closure 槽和 type。没有“把 factory reference 直接偷进返回 Variant”这一
条捷径。

`PropSet` 的 `objthis` 是 accessor 内的同一 Dictionary dispatch，flags 固定为
`TJS_MEMBERENSURE`。HRESULT 没有被测试；普通失败不改变控制流。child Variant 在下一次
serializer 调用之前销毁，不会把多棵 child state tree 同时暂存在 staging vector。

## Timeline item 的 Array publication

Timeline 先创建外层 fresh TJS Array，再按 `_activeTimelineLabels` 的 vector 顺序循环。每项
严格遵守：

```text
state = timelineStates[label]               // unordered_map::operator[]
item = accessor-owned fresh Dictionary

SetValue("label", label)
SetValue("flags", state.flags | 1)
SetValue("curTime", state.currentTime)
SetValue("blendRatioCtrl", serializeVar(state.blendController))
SetValue("stopWhenBlendDone", state.autoStop)

out.Items.emplace_back(item.dispatch, item.dispatch)
destroy item accessor
```

`operator[]` 明确发生在 Dictionary factory 之前。因此 stale active label 会先 materialize
默认 map node；之后 Dictionary 分配、blend-controller 解引用或字段写入抛异常，也不删除
该 node。

append 点构造的是 `{dispatch,dispatch}` Object closure，不是复制一份长期存活的 item
Variant。Array fast path 为同一 dispatch 建立两条引用并写 type；grow slow path保持相同
closure 语义。append 完成后 accessor Release，item 只由 Array element 拥有。

五个 `PropSet` 的状态全部忽略。因此某字段普通失败后，后续字段仍写入；五步结束后即使
Dictionary 缺字段或只有部分字段，也仍 append 到 Array。只有抛异常才阻止当前 item
publication。此前已 append 的 Array 前缀由局部 Array owner 在异常展开中清理，不会作为
部分结果返回。

## Base 与 OuterForce

Base 固定逐项执行：

```text
serializeVar(position) -> coord
serializeVar(scale)    -> scale
serializeVar(color)    -> color
serializeAngle(angle)  -> rotate
```

OuterForce 固定执行：

```text
serializeVar(bustOuterForce)  -> bust
serializeVar(hairOuterForce)  -> hair
serializeVar(partsOuterForce) -> parts
```

每个 controller serializer 返回 `{dispatch,dispatch}` child closure。外层 `PropSet` 取得自己的
属性引用后，child 临时量立即析构；下一项才开始 serialize。controller owner 没有 null
guard：null 会在 child serializer 首次取字段时失败，外层 accessor 随异常释放尚未发布的
Dictionary factory reference。

普通 `PropSet` 失败不阻止下一项，最终仍返回部分 Dictionary。异常则不发布外层 closure；
已经成功写入的 child 属性随着外层 Dictionary 一起在 unwind 中释放。

## 顶层 Engine state Dictionary

顶层函数在创建 Dictionary 之前完成既有零时间预刷新：

1. `preProgress_guess(true, 0.0)`；
2. Eye、Eyebrow、Mouth、Selector、Transition controller 以 `dt=0` step，并写回变量表；
3. `stepRootControllers_guess(0.0)`；
4. 之后才调用 Dictionary factory。

Dictionary 的固定 child 顺序保持：

```text
timeline -> eye -> eyebrow -> mouth -> transition -> selector -> base -> outerforce
```

八个 child 都按“serialize、立即 PropSet、立即销毁临时量”的顺序执行，复用 serialize / restore
已经确认的同一组 shared hint。`mouth` 继续复用 controller-level `mouth` hint；其它七项用
各自顶层槽。

普通 `PropSet` 失败不停止后续 child，函数最终可以返回部分顶层 Dictionary；这点比旧文档
仅描述“失败时清理前缀”更精确。只有异常才不返回结果。无论何种失败，Dictionary 写入阶段
都不会回滚已经发生的零时间 step 和变量表更新。

## 边界与异常矩阵

- 四类 Dictionary factory 返回 null 时没有额外 guard；第一次 `SetValue`/`PropSet` 沿
  native null-dispatch 边界失败。
- child serializer 返回非 Object Variant 时，外层 `PropSet` 仍按 Variant 原类型写入；
  wrapper 不额外验证 schema。正常内置 child 路径都返回 Object/Array。
- `PropSet` failed HRESULT 与异常不同：前者被忽略并继续，后者立即 unwind。
- Timeline source Array 为空时返回 fresh empty Array，完全不会调用 item Dictionary
  factory。
- Timeline item 只有五次字段构建全部正常返回后才 append；当前 item 的任一步抛异常都
  不会留下半个 Array element。
- Base、OuterForce、top-level 返回 closure 只在全部 child 调用正常返回后构造；在此之前
  外部没有获得 Dictionary 引用。
- return closure 的两次 AddRef 完成后，accessor 才 Release factory reference；没有窗口让
  正常返回的 Dictionary 在 result 获得所有权前归零。
- 所有 child 状态、flags 和时间值按当前内存原样序列化，不执行 finite/range/schema
  正规化。

## 本地恢复、IDB 回写与验证

- Timeline item、Base、OuterForce、top-level Engine state 都改为 accessor 直接拥有
  Dictionary factory reference，并以 `SetValue(..., TJS_MEMBERENSURE, sharedHint)` 写字段。
- Timeline item 改为直接向 `tTJSArrayNI::Items` 发布 `{dispatch,dispatch}`；另外三类在
  accessor 析构前构造同形返回 Variant。
- 字段顺序、map `operator[]` 时机、顶层 preflush、shared hint、ignored HRESULT 和 child
  temporary 的逐项存活边界保持不变。
- 四份 recovery IDB 的四个入口均追加本轮 fresh owner 审计注释，并建立 owner-specific
  bookmark；最终四库原位保存。
- 真实 Emscripten response-file syntax-only 通过，仅有既有 `_tss` warning。
- `cmake --build --preset "Web Debug Build"` 完成 3 个增量步骤，重新编译
  `EmoteEngine.cpp`、生成 motionplayer 静态库并成功链接最终 `index.html`；输出仅含仓库
  既有 `_tss`、pthread memory-growth、JSPI 与 JS library warnings。
- 定向 `git diff --check` 覆盖源码、计划和本页并通过；换行转换提示不是 whitespace error。

本页补精确的是状态 Dictionary 的 owner/publication 结构；字段语义、restore 提交顺序和
hint 地址仍分别以
`analysis/motionplayer_timeline_state_snapshot_restore_four_binary_2026-08-15.md`、
`analysis/motionplayer_base_outerforce_state_snapshot_restore_four_binary_2026-08-15.md` 和
`analysis/motionplayer_engine_state_pipeline_four_binary_2026-08-15.md` 为准。这仍不代表整个
motionplayer 已达到 100% 复原。
