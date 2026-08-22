# Motionplayer playing-timeline info Dictionary handoff 四端复原（2026-08-15）

## 范围与关键结论

本纵切面重新反编译四个 `reference/binaries/` 的
`EmoteEngine_getPlayingTimelineInfoList_guess`，补齐
`analysis/motionplayer_timeline_list_api_four_binary_2026-08-12.md` 尚未精确恢复的 item
Dictionary owner/handoff。

该 API 不使用状态 serializer 的“accessor 直接接管 Dictionary factory reference”协议。
四端共同执行另一条独立链：

1. 先把 fresh Dictionary factory reference 变成一份 owning Object Variant；
2. 复制该 Variant、强制 Object、让 `ncbPropAccessor` 单独 retain dispatch；
3. 在首个字段前销毁复制临时量；
4. 通过 accessor 写 `label`、`flags`、`blendRatio`；
5. Array append 的是原始 Dictionary Variant 的副本，不是 accessor raw dispatch；
6. append 后先释放 accessor，再析构原始 Dictionary Variant。

当前源码原先只用 `AsObjectNoAddRef()` 借用原 Variant 的 dispatch，而且提前构造三份字段
Variant并让它们同时活到循环尾。正常单线程结果往往相同，但与原生的独立 retained
accessor、字段临时量区间和异常清理顺序不同。本轮已恢复四端一致的 handoff。

## 四端映射

| 目标 | 入口 / IDA 函数大小 |
|---|---:|
| Android ARM64 | `0x6728A4` / `0x358` |
| Android ARMv7 | `0x55B788` / `0x10A` |
| iOS ARM64 | `0x1001AF104` / `0x17C` |
| iOS ARMv7 | `0x1AE9D0` / `0x18E` |

函数名继续保留 `_guess`；stripped 发布物不提供原始 C++ 符号。

## lookup、过滤与分配时机

外层先创建 fresh TJS Array，然后按 `_activeTimelineLabels` 的 vector 物理顺序遍历。每个
label 都对 `_timelineStates` 执行非插入 lookup：

```text
for label in activeTimelineLabels order:
    found = timelineStates.find(label)
    if found == end:
        continue
    // only now allocate item Dictionary
```

HM3 miss 不调用 Dictionary factory，也不 materialize 默认 map node。重复 active label
每次都会重新 lookup 并创建独立 item；空 label 只要 map 中存在，也照常序列化。

这与 timeline state snapshot 的 `operator[]` 行为不同：后者会 materialize stale entry，
本 API 明确跳过。两条路径不能共享一个会插入的 lookup helper。

## owning Variant -> retained accessor -> original Variant publication

四端共同源级伪代码为：

```text
dictionary = createFreshDictionaryObjectVariant()
// dictionary owns Object and ObjThis references

objectValue = copy(dictionary)
objectValue.ToObject()
item = ncbPropAccessor(objectValue)      // AsObject/AddRef one dispatch ref
destroy objectValue                     // before first PropSet

item.SetValue("label", label, MEMBERENSURE, labelHint)
item.SetValue("flags", int32(state.flags), MEMBERENSURE, flagsHint)
item.SetValue("blendRatio", double(state.blendWeight),
              MEMBERENSURE, blendRatioHint)

out.Items.push_back(dictionary)         // copy original full closure
destroy item                            // Release accessor retain
destroy dictionary                      // Release original Object + ObjThis
```

Android ARM64 展开最完整：Dictionary factory 后对同一 dispatch 执行两次 AddRef，构造
Object/ObjThis 双槽并释放 factory ref；接着 copy-construct 第二份 closure，`ToObject` 后再
对 Object dispatch 做一次 AddRef 交给 accessor，随即释放复制 closure。写完三字段后，Array
以 copy constructor 从最初的 Dictionary closure 建 element；最后 accessor Release，原始
closure 再释放两槽。

Android ARMv7、iOS ARM64、iOS ARMv7 虽将 factory-to-Variant、Variant copy 和 Array push
折叠进 helper，栈对象与析构顺序相同：原始 Dictionary Variant、复制临时 Variant、
accessor 三者是不同 owner；复制临时在第一字段前销毁，Array append读取原始 Variant。

因此这里不能机械套用 Timeline snapshot item 的
`ncbPropAccessor(TJSCreateDictionaryObject(), false)` + raw `{dispatch,dispatch}` append。
两者最终 Array element 形状相同，但 intermediate ownership 和异常边界不同。

## 三字段与临时量

字段固定为：

| 顺序 | key | 值来源 / TJS 类型 |
|---:|---|---|
| 1 | `label` | active vector 中的 `ttstr` / String |
| 2 | `flags` | `state.flags` / Integer，未 OR `1` |
| 3 | `blendRatio` | `state.blendWeight` 的 float-to-double 提升 / Real |

三次写入都用 `TJS_MEMBERENSURE`，复用既有
`engineLabelHint_guess`、`timelineInfoFlagsHint_guess`、
`timelineInfoBlendRatioHint_guess`，并忽略 HRESULT。

字段 Variant 不是在 Dictionary 创建后一次性并列构造。每个 typed `SetValue` 调用各自建立
所需临时 Variant，在该调用完整表达式结束时销毁；`label` 临时结束后才构造 flags，flags
结束后才构造 blendRatio。脚本型 `PropSet` 的重入或异常因此看不到两份无关字段临时量仍
占有额外对象。

## 普通失败、异常和 Array publication

- `PropSet` 返回 failed HRESULT 时继续下一字段。三次调用正常返回后，无论成功了几项，
  原始 Dictionary Variant仍被 append，因此 Array 中可以出现部分 Dictionary。
- 任一字段构造、类型转换或 `PropSet` 抛异常时，当前 item 尚未 append；accessor 和原始
  Dictionary Variant 都 unwind，当前 item 不进入 Array。
- Array push copy/grow 抛异常时，当前 accessor 和原始 Dictionary 仍被清理；已完成的 Array
  前缀随后由外层 Array owner 清理，调用者得不到部分结果。
- factory 返回 null 或 Object conversion/retained-accessor 建立失败没有 wrapper-level
  友好 fallback；异常按上述 owner 链展开。
- append 成功后即使 accessor 随后 Release，Array element 已通过复制原始 closure取得自己
  的 Object/ObjThis 引用；不存在 borrowed dangling dispatch。
- source active vector 或 map 不因字段失败而回滚/改写。该查询除可能触发 TJS 对象分配及
  property 回调外，不修改 timeline state。

## 本地恢复、IDB 回写与验证

- 源码保留 `createTJSDictionary_guess()` 生成原始 owning Variant；新增
  `copy -> ToObject -> ncbPropAccessor retain -> copied Variant early Clear`。
- 三个 raw `dispatch->PropSet` 和跨循环尾存活的手工 field Variant 改为 accessor typed
  `SetValue`，保持 MEMBERENSURE、hint、字段顺序和 ignored HRESULT。
- Array 继续 `push_back(dictionary)`，刻意复制原始完整 closure；没有改成 raw dispatch
  emplace。
- 四份 recovery IDB 的入口均追加 handoff、字段临时量与 cleanup 注释，并建立
  owner-specific bookmark；最终四库原位保存。
- 真实 Emscripten response-file syntax-only 通过，仅有既有 `_tss` warning。
- `cmake --build --preset "Web Debug Build"` 完成 3 个增量步骤，重新编译
  `EmoteEngine.cpp`、生成 motionplayer 静态库并成功链接最终 `index.html`；输出仅含仓库
  既有 `_tss`、pthread memory-growth、JSPI 与 JS library warnings。
- 定向 `git diff --check` 覆盖源码、计划、旧列表证据页和本页并通过；换行转换提示不是
  whitespace error。

本页只补正 playing-info item 的中间 owner 结构；vector/HM3 布局、三字段 schema、NCB
注册与列表 API 测试仍以
`analysis/motionplayer_timeline_list_api_four_binary_2026-08-12.md` 为准。该局部闭合不代表
整个 motionplayer 已达到 100% 复原。
