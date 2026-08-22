# Motionplayer variable publication Variant reset 生命周期四端复原（2026-08-15）

## 范围与结论

本纵切面重新反编译四个 `reference/binaries/` 的
`EmoteEngine_resetVariableContainers_guess`，补齐 metadata reset 尾部三份脚本发布
Variant 的精确 owner 顺序：

1. 创建 fresh Array 临时量；
2. copy-assign 到 `_variableLabelsBase`；
3. **立即析构 Array 临时 owner**；
4. 再从 `_variableLabelsBase` copy-assign `_variableLabels`；
5. 创建 fresh Dictionary Object Variant 临时量；
6. copy-assign 到 `_variableFrameLists`，立即析构 Dictionary 临时量；
7. 随后才 clear `_instantVariableLabels` 和 `_variableRanges`。

源码的 Dictionary full-expression 原本已符合第 5–6 步；Array helper 局部量却一直活到
整个 `resetMetadataState()` 结束，额外保留 Object/ObjThis 两条引用跨过 current-label
alias、Dictionary 重建和两个 unordered 容器 clear。本轮只缩短 Array 临时量作用域，恢复
四端共同的引用计数边界，不改变发布对象内容和 clear 顺序。

## 四端函数与字段映射

| 目标 | reset helper 入口 / 大小 | `_variableLabelsBase` | `_variableLabels` | `_variableFrameLists` |
|---|---:|---:|---:|---:|
| Android ARM64 | `0x666B78` / `0x190` | `+1208` | `+1228` | `+1248` |
| Android ARMv7 | `0x555A04` / `0x7C` | `+640` | `+652` | `+664` |
| iOS ARM64 | `0x1001A66AC` / `0x7C` | `+840` | `+860` | `+880` |
| iOS ARMv7 | `0x1A5D88` / `0xC6` | `+452` | `+464` | `+476` |

64-bit Variant 宽 20 字节，32-bit Variant 宽 12 字节；三成员紧邻。Android ARM64 的
reset helper 还内联了后续 HM4/HM5 clear，另外三端调用已命名 clear helper；这只是优化与
STL ABI 差异，三份 Variant 的源级步骤一致。

函数名继续保留 `_guess`，因为 stripped 发布物不能证明原始 C++ 拼写。

## Array temporary 与两个发布 alias

四端共同伪代码：

```text
arrayTemp = createTJSArrayWithItems()
copyAssign(engine.variableLabelsBase, arrayTemp.value)
destroy arrayTemp.value

copyAssign(engine.variableLabels, engine.variableLabelsBase)
```

`createTJSArrayWithItems` 返回 owning Array Object Variant；Object 和 ObjThis 都是同一个
Array dispatch。copy-assign base 成员后，成员取得自己的两条 closure 引用；紧接着销毁
临时量的两条引用。随后 current 成员从 base 成员 CopyRef，再取得 Object/ObjThis 两条
引用。因此第二次赋值开始时，fresh Array 只由 base 成员拥有，不再由 factory helper
临时量额外保活。

完成第二次赋值后，`_variableLabelsBase` 与 `_variableLabels` 指向同一 Array native
instance，但它们是两份独立 owning Variant，不是一个 raw pointer alias。以后 selector
同步替换 `_variableLabels` 时，base 仍持有初始 label Array；脚本已经取得的旧 Variant 也按
各自 closure reference 继续存活。

原生在 reset 中不保留 `createTJSArrayWithItems` 返回的 raw `Items *`；该指针只用于 factory
helper 内部返回结构，reset helper 从不 append element。新 Array 保持为空。

## Dictionary temporary 与 frame-list publication

下一段为：

```text
dictionaryTemp = createFreshDictionaryObjectVariant()
copyAssign(engine.variableFrameLists, dictionaryTemp)
destroy dictionaryTemp
```

Dictionary factory reference 先被 Object/ObjThis closure接管；赋值成员后临时 Variant立即
释放自己的两槽，最终只由 `_variableFrameLists` 持有。这里不创建
`ncbPropAccessor`，也不写字段；后续 `buildVariableList_guess` 才按变量 label 在这份长期
Dictionary 中查询/创建 frame Array。

源码的 `createTJSDictionary_guess()` 返回值直接作为赋值右值，其临时量在分号处析构，已经
符合四端；本轮没有把它误改成 accessor-owned factory 协议。

## old-value release、clear 时机与异常前缀

三次 `tTJSVariant` copy assignment 会在各自步骤释放成员旧值并取得新 closure：

- `_variableLabelsBase` 的旧 Array先被替换；临时 fresh Array随后析构；
- `_variableLabels` 的旧 alias 后被替换；
- `_variableFrameLists` 的旧 Dictionary最后被替换。

只有这三步全部正常返回后，函数才 clear `_instantVariableLabels`，再 clear
`_variableRanges`。`_variableValues` 刻意不清理。

该序列没有事务回滚：

- fresh Array factory 或首个 copy assignment 抛异常时，三成员和两个 unordered clear 的
  可见前缀由实际完成点决定；
- base 成员替换成功后，current 成员 copy assignment 抛异常，不会恢复旧 base；此时 native
  Array factory 临时量已经按原顺序销毁；
- current alias 成功但 Dictionary factory/assignment 抛异常时，两个 Array 成员保持新值，
  frame-list Dictionary和 HM4/HM5 维持当时状态；
- HM4 clear 抛异常或 allocator/element destructor 异常边界不回滚三份 Variant，也不继续
  保证 HM5 clear；正常库析构通常不抛，但源结构没有事务层；
- 旧 Array/Dictionary 若已被脚本 Variant引用，成员替换只降低引用计数，不强制使外部
  closure 失效。

临时 Array owner 是否仍跨 `_variableLabels` assignment 存活，会改变异常点的 dispatch
引用数和 reentrant destruction 时机，因此不能以“最终两个成员仍指向同一 Array”为理由
省略这个 scope 边界。

## 本地恢复、IDB 回写与验证

- `resetMetadataState()` 中 fresh `TJSArrayWithItems_guess` 局部量被限制在只覆盖
  `_variableLabelsBase` copy assignment 的内层 scope；scope 结束后才执行
  `_variableLabels = _variableLabelsBase`。
- `_variableFrameLists = createTJSDictionary_guess()` 保持 full-expression temporary 赋值，
  三成员赋值以及 HM4/HM5 clear 的先后不变。
- 四份 recovery IDB 的 reset-variable-container helper 都追加临时 owner、成员 CopyRef、
  Dictionary temporary 和 clear 顺序注释，并建立 owner-specific bookmark；最终四库原位
  保存。
- 真实 Emscripten response-file syntax-only 通过，仅有既有 `_tss` warning。
- `cmake --build --preset "Web Debug Build"` 完成 3 个增量步骤，重新编译
  `EmoteEngine.cpp`、生成 motionplayer 静态库并成功链接最终 `index.html`；输出仅含仓库
  既有 `_tss`、pthread memory-growth、JSPI 与 JS library warnings。
- 定向 `git diff --check` 覆盖源码、计划、变量容器语义页和本页并通过；换行转换提示不是
  whitespace error。

本页补充
`analysis/motionplayer_variable_container_tail_semantic_names_four_binary_2026-08-15.md` 的发布
Variant 生命周期；HM4–HM7 的语义、node ABI 与 clear 行为继续以该页为准。这一局部闭合
不代表整个 motionplayer 已达到 100% 复原。
