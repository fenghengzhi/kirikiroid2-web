# Motionplayer buildVariableList owner / lookup / Array pipeline 四端复原（2026-08-15）

## 范围与结论

本纵切面重新反编译四个 `reference/binaries/` 的
`EmoteEngine_buildVariableList_guess`，闭合 variable metadata builder 从脚本输入到
`_variableLabels`、`_variableFrameLists`、`_variableRanges` 的完整对象生命周期。

fresh 四端证据纠正了源码中的三个重要偏差：

1. builder 每次调用都重建 `_variableLabels` 的 fresh Array，随后也重建并替换
   `_variableFrameLists` 的 fresh Dictionary；不是继续使用 metadata reset 早先建立的
   Dictionary；
2. 输入 `variableList` 和新 `_variableFrameLists` 都经
   Variant copy -> force Object -> `ncbPropAccessor` retain -> copied Variant early-destroy，
   两个 accessor 跨整个 builder 存活；不是借用 `AsObjectNoAddRef()`；
3. 每个 label 都在 frame Dictionary lookup **之前无条件创建 fresh candidate Array**。
   strict probe 命中后再执行第二次 flags-0 getter并丢弃 candidate；miss 才把 candidate发布。

每个 metadata item、`frameList` 和 frame 也各自建立 retained accessor，并在第一次成员读取
前销毁转换 Variant。源码现已恢复这些 owner 和 getter 次数，同时接回原本已声明但在本
调用漏传的 `engineFrameListHint_guess`。

## 四端映射

| 目标 | 入口 / IDA 函数大小 |
|---|---:|
| Android ARM64 | `0x667910` / `0xAE8` |
| Android ARMv7 | `0x555FC0` / `0x3FE` |
| iOS ARM64 | `0x1001A73C0` / `0x72C` |
| iOS ARMv7 | `0x1A693C` / `0x622` |

关键 Engine 成员偏移：

| 成员 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `_variableLabels` | `+1228` | `+652` | `+860` | `+464` |
| `_variableFrameLists` | `+1248` | `+664` | `+880` | `+476` |
| `_variableRanges` | `+1328` | `+704` | `+944` | `+508` |

函数名继续保留 `_guess`，因为四份 stripped 发布物不能证明原始 C++ 拼写。平台间函数大小
差异主要来自 Android ARM64 内联旧 libstdc++ unordered-map、deque/vector grow 与 ttstr
hash；共同源结构不硬编码这些 ABI 算术。

## 入口发布对象重建与外层 accessor

四端共同顺序：

```text
workingArray = createTJSArrayWithItems()
copyAssign(engine.variableLabels, workingArray.value)
labelsItems = workingArray.rawItems

dictionaryTemp = createFreshDictionaryObjectVariant()
copyAssign(engine.variableFrameLists, dictionaryTemp)
destroy dictionaryTemp

inputValue = copy(variableList)
inputValue.ToObject()
input = ncbPropAccessor(inputValue)       // retain dispatch
destroy inputValue                       // before count getter

dictValue = copy(engine.variableFrameLists)
dictValue.ToObject()
frameDictionary = ncbPropAccessor(dictValue)
destroy dictValue                        // before count getter

count = input.PropGet("count", flags=0) -> Integer
```

因此无论输入是否为空或是否合法，两个 fresh 发布对象都在输入 Object 转换和 `count` getter
之前替换旧成员。invalid/Void input 若在 `ToObject` 或 count conversion 抛异常，不会恢复旧
labels/Dictionary。

`workingArray.value` 是一个贯穿函数的可复用 owning Variant；`labelsItems` 单独保存最初
labels Array 的 borrowed native Items 指针。首次 metadata item 创建 candidate Array时，
`workingArray.value` 被覆盖，最初 labels Array只剩 Engine 成员（及可能的外部 Variant）
持有；raw `labelsItems` 仍按发布物直接使用，没有额外 native-instance retain。

创建顺序还说明 metadata reset 中的 `_variableFrameLists` 只是初始发布值。真正处理
`variableList` 时，builder 总会再换成另一份 fresh Dictionary；重复直接调用 builder 也不会
继承上次 Dictionary 的 label/frame 属性。

## metadata item accessor 与 range node

每个 outer index 的共同开头：

```text
itemValue = input.PropGetByNum(flags=0, index) // getter HRESULT ignored
itemValue.ToObject()
item = ncbPropAccessor(itemValue)              // retain dispatch
destroy itemValue                             // before label getter

label = item.PropGet("label", flags=0, engineLabelHint) -> ttstr
range = variableRanges.try_emplace(label, label).first
```

item 不以 owning Variant 保持到 outer iteration 末尾；独立 accessor 是跨 label、frame-list、
inner frame loop 的 receiver owner。其目的不仅是 refcount 形状：property getter可以重入并
修改原输入 Array，accessor 仍使当前 item dispatch 存活。

`_variableRanges` 不在 builder 入口 clear。miss 构造带 key 和 mapped label-copy 的新 node；
hit 复用旧 node以及其中的 `frameMin/frameMax`。这与每次 fresh frame Dictionary 的行为不
对称，是发布版原样语义。

## 每个 label 的 eager candidate 与双 getter

完成 range lookup/insert 后，四端无条件执行：

```text
candidateTemp = createTJSArrayWithItems()
copyAssign(workingArray.value, candidateTemp.value)
candidateItems = candidateTemp.rawItems
destroy candidateTemp

probe = Void
hr = frameDictionary.PropGet(MEMBERMUSTEXIST,
                             label, label.GetHint(), probe)
destroy probe

if SUCCEEDED(hr):
    existing = frameDictionary.PropGet(flags=0,
                                       label, label.GetHint())
    copyAssign(workingArray.value, existing) // destroys eager candidate
    destroy existing
    frameItems = nativeArray(workingArray.value).Items
else:
    labelsItems.emplace_back(label)
    frameDictionary.PropSet(MEMBERENSURE,
                            label, label.GetHint(), workingArray.value)
    frameItems = candidateItems
```

命中路径不是复用 strict probe 返回的 Variant：第一次 getter 只决定 HRESULT并立即销毁
probe，第二次 flags-0 getter才取真正 Array。这会让脚本 getter 被调用两次，而且第二次可以
返回与第一次不同的对象或抛异常。源码此前只调用一次，现已恢复双 getter。

candidate 也不是 miss-only lazy allocation。即使 label 已存在，也先创建新 Array；成功取得
既有 Array时，assign 会释放 candidate。重复 label 因此每次都具有 candidate allocation/
destruction 和两次 Dictionary getter 的可观察轨迹。

miss 路径先向 labels Array append 字符串，再调用 Dictionary PropSet；HRESULT 被忽略。
因此 PropSet 普通失败时，label 已公开在 `_variableLabels`，frames 仍会写入 candidate 的 raw
Items，但 Dictionary 可能没有对应属性。candidate 至少由 `workingArray.value` 持有到下一
outer item覆盖或函数退出；失败不触发 rollback。

命中路径把 `workingArray.value` 替换成第二次 getter结果并通过 native-instance support取得
Array Items；没有 Object/null/Array 类型的 wrapper-level guard。脚本第二次返回错误类型会
沿原生转换/native-instance 边界失败。

## frameList 与每个 frame 的 owner

frame list 读取固定为：

```text
frameListValue = item.PropGet("frameList", flags=0,
                              engineFrameListHint)
frameListValue.ToObject()
frameList = ncbPropAccessor(frameListValue)
destroy frameListValue
frameCount = frameList.PropGet("count", flags=0) -> Integer
```

`engineFrameListHint_guess` 与 timeline builder 读取同名 `frameList` 共用；四端地址身份已在
`analysis/motionplayer_timeline_internal_four_binary_2026-08-12.md` 记录。builder 源码此前
漏传该已存在 hint，本轮补回。

每个 frame 则保留一份 original Variant供 Array publication，另建 accessor copy：

```text
frame = frameList.PropGetByNum(flags=0, frameIndex)
frameObjectValue = copy(frame)
frameObjectValue.ToObject()
frameObject = ncbPropAccessor(frameObjectValue)
destroy frameObjectValue

value = frameObject.PropGet("frame", flags=0,
                            controllerFrameHint) -> Real
range.frameMin = range.frameMin < value ? range.frameMin : value
range.frameMax = value < range.frameMax ? range.frameMax : value
frameItems.push_back(frame)                 // copy original Variant

destroy frameObject
destroy frame
```

numeric getter和 named real getter都忽略普通 HRESULT；conversion异常仍传播。range extrema
继续保留“相等或 unordered 时选择新值”的已确认 signed-zero/NaN 边界，也继续读取构造器
未初始化的 `frameMin/frameMax`。

push 的是原始 frame Variant完整值，不要求它一定是 Object；accessor copy 用于读取
`frame`，而 Array element复制原始 closure/type。frame accessor 在 original frame析构前
释放。

## owner 释放顺序与异常前缀

正常 outer iteration 结尾依次为：frameList accessor Release、label owner Release、item
accessor Release。函数末尾依次释放 frame-Dictionary accessor、input accessor、最后释放
`workingArray.value`；Engine成员继续拥有已发布 Array/Dictionary。

关键异常前缀：

- fresh labels 或 fresh frame Dictionary 创建/成员赋值失败时，只保留已经完成的成员替换；
  没有 old-value rollback。
- input/frame-Dictionary Object转换失败发生在两个成员都已替换之后。
- item getter/转换、label转换、range node分配异常会阻止当前 label candidate 创建，保留
  此前 items/ranges。
- candidate 创建后 strict probe抛异常时，candidate由 working owner清理；此前 range node
  不回滚。
- strict miss 后 labels append 成功、PropSet 抛异常时，label prefix 保留在 Engine Array；
  Dictionary item不保证存在。
- frameList/frame getter或转换抛异常时，已写入的 range extrema和 frame Array prefix保留；
  accessor RAII释放当前 receiver。
- Array push grow抛异常时，当次 extrema更新已经发生，不回滚；原始 frame Variant与
  accessor仍按栈展开释放。
- `count <= 0` 时不进入 outer loop，但 fresh outputs 和两个 retained accessor的建立/释放
  仍发生。

## 本地恢复、IDB 回写与验证

- builder 入口新增 fresh `_variableFrameLists` replacement；输入和新 Dictionary恢复
  copy/force/accessor-retain/early-clear，并使用 retained dispatch进行 count/numeric/named
  getter。
- `MotionDispatch.h` 新增 retained-dispatch 版本的 Variant、numeric Variant、Real 和 String
  getter；只供已由外层 accessor固定 receiver寿命的路径使用。
- metadata item、frameList、frame 都恢复独立 retained accessor；frame原始 Variant仍用于
  Array push。
- 每个 label 恢复 eager candidate Array、strict probe temporary early-destroy、hit第二次
  flags-0 getter、miss label-before-PropSet 和 ignored HRESULT。
- `frameList` getter 接回 `engineFrameListHint_guess`；其它 label/frame hints保持既有共享槽。
- 四份 recovery IDB 的 builder入口均追加完整 owner、双 getter、eager candidate和异常前缀
  注释，并建立 owner-specific bookmark；最终四库原位保存。
- 真实 Emscripten response-file syntax-only 通过，仅有既有 `_tss` warning。
- `cmake --build --preset "Web Debug Build"` 完成 34 个增量步骤；由于
  `MotionDispatch.h` 新增 retained-dispatch overload，相关 motionplayer translation units
  全部重编，静态库与最终 `index.html` 成功链接。输出仅含仓库既有 `_tss`、imagepacker、
  pthread memory-growth、JSPI 与 JS library warnings。
- 定向 `git diff --check` 覆盖 `EmoteEngine.cpp`、`MotionDispatch.h`、计划、变量容器语义页
  和本页并通过；换行转换提示不是 whitespace error。

本页闭合 variable-list metadata builder 的对象结构；unordered container ABI、range node
字段语义和 reset 生命周期分别见
`analysis/motionplayer_variable_container_tail_semantic_names_four_binary_2026-08-15.md`、
`analysis/motionplayer_variable_range_hm5_four_binary_2026-08-11.md` 与
`analysis/motionplayer_variable_publication_variant_reset_lifecycle_four_binary_2026-08-15.md`。
这仍不代表整个 motionplayer 已达到 100% 复原。

## 2026-08-16 V144 fresh addendum：direct ncb source identity

本页当时关于candidate Array、strict probe、hit双getter、miss发布顺序和owner层级的结论经
四端重新反编译仍成立；但“源码已恢复这些getter”的表述对source identity过强。V144开始时
`EmoteEngine.cpp`的本函数仍通过 `detail::motionPropGet*` wrapper执行Count和普通getter，
没有直接写出发布物的typed `ncbPropAccessor::GetValue/GetArrayCount` 形状。

V144已将该残留闭合为5个显式accessor、2次`GetArrayCount`、6次typed `GetValue`、1次
`HasValue`和1次`SetValue`，本函数raw helper归零；新增四层failure-after-write/reentrant
owner probe与duplicate-label candidate复用probe。四库各回写1条function comment、24条地址
注释和bookmark，强制重编译回读24/24后保存。完整证据和本轮验证见
`analysis/motionplayer_build_variable_list_nested_ncb_accessor_source_identity_four_binary_2026-08-16.md`。
