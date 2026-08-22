# MotionPlayer instantVariableList / timelineControl 构建链四端复原（2026-08-12）

## 范围与结论

本轮针对 `reference/binaries/` 中 Android arm64、Android armv7、iOS arm64、
iOS armv7 四份当前参考产物，重新反编译了 instant-variable set builder、timeline
metadata builder、HM3 `operator[]` 和 raw `tTJSVariant` 赋值 helper，并把它们与
本轮已新鲜核对的 metadata reset 串成同一条数据流。

当前源码的总体容器模型正确，但发现两个真实遗漏：

1. `buildTimelineControl` 对 `diff` 的 MEMBERMUSTEXIST 探测和第二次布尔读取在
   四端都共用同一个进程级 TJS hint；本地原来两次都传空 hint。
2. 同一 builder 的 `label` 读取使用 Engine 共享 label hint；本地原来同样传空
   hint。

源码现已恢复这两个 hint 身份，并删除本纵向编译源码中旧 `libkrkr2.so` 单地址
注释。函数精确地址、ABI 偏移和容器节点尺寸只保留在本文与 IDB。

## 四端函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `buildInstantVariableList_guess` | `0x66CA2C` | `0x558DBC` | `0x1001AB6E4` | `0x1AAE18` |
| `buildTimelineControl_guess` | `0x66CBEC` | `0x558EB4` | `0x1001ABA30` | `0x1AB18C` |
| `EmoteTimelineMap_subscript_guess` | `0x685060` | `0x5669AC` | `0x1001A6938` | `0x1A6074` |
| raw Variant copy-assign | `0xA0E464` | `0x760440` | `0x100319E14` | `0x31F1C0` |
| HM4 set insert | `0x686B40` | `0x5680F4` | builder 内联 | builder 内联 |
| `resetMetadataState_guess` | `0x666D08` | `0x555AD8` | `0x1001A67BC` | `0x1A5F4C` |

所有 builder 地址都从四端 `applyMetadata` 的相同调用位置重新确认，而不是把旧
Android `libkrkr2.so` 偏移平移到新文件。

## Engine 容器布局

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| HM3 timeline map | `+936` | `+468` | `+584` | `+292` |
| main label vector | `+992` | `+496` | `+624` | `+312` |
| diff label vector | `+1016` | `+508` | `+648` | `+324` |
| active label vector | `+1040` | `+520` | `+672` | `+336` |
| HM4 instant set | `+1272` | `+676` | `+904` | `+488` |

三个 label vector 是彼此独立的 `vector<ttstr>`。Timeline builder 只清前两个；
active vector 从入口到返回完全不读写。HM3 和 HM4 都是 `ttstr` 自定义哈希容器，
但 HM4 只有 key，HM3 还拥有完整 timeline state。

## instantVariableList 数据流

### 2026-08-16 V141 accessor/source-identity 补充

本节早期用 `propGetCount` / `propGetByNum` 表达的只是值流，不是原生对象访问层的精确
实现。本轮重新 fresh decompile 四端后，已确认实际 source identity 是：先复制输入
Variant，以该副本构造覆盖整个循环的 root `ncbPropAccessor`；Count 只通过 accessor
读取一次，每项用同一 accessor 的 typed indexed `GetValue<ttstr>` 转换。构造临时在
Count 前析构，root accessor 在循环后析构，因此可重入清除 caller owner 不会提前销毁
dispatch。完整 helper 拓扑、HRESULT-after-write 边界、IDB readback 和 probe 见
`analysis/motionplayer_instant_variable_builder_ncb_accessor_indexed_ttstr_four_binary_2026-08-16.md`。

以下伪代码继续用于描述容器值流；其中 raw property helper 名不再表示编译源码选择。

四端共同源级伪代码为：

```cpp
count = propGetCount(instantVariableList);
for (index = 0; index < count; ++index) {
    raw = propGetByNum(instantVariableList, index);
    key = ttstr(raw);
    instantSet.insert(key);
}
```

关键边界：

- builder 自己不清 HM4；重复调用得到集合并集。正常 `applyMetadata` 路径依赖更早
  的 `resetMetadataState` 清空 HM4。
- 不检查 `enabled`、元素类型或空字符串。空值按普通 Variant-to-ttstr 规则形成空
  key；重复 key 不增加 size。
- `count <= 0` 时完全不改容器；不存在为“空输入”做额外 clear 的分支。
- 每个成功插入的 set 节点拥有一个 ttstr 引用并保存缓存 hash。64 位节点是 24
  字节，32 位节点是 12 字节。

### libstdc++ / libc++ 插入顺序差异

Android 两端的 libstdc++ helper 在查重之前就分配候选节点并 CopyRef key：

```text
allocate candidate -> retain key -> compute/cache hash -> lookup
  miss: link candidate
  hit : release candidate key -> delete candidate
```

iOS 两端的 libc++ 展开则先计算 hash、遍历 bucket，只有 miss 才分配并链接节点：

```text
compute/cache hash -> lookup
  hit : return
  miss: allocate -> retain key -> maybe rehash -> link
```

因此重复插入时 Android 仍可能在临时候选节点分配处抛出，而 iOS 不需要该次节点
分配。这是标准库实现带来的真实平台边界；本地使用目标平台的标准容器，不能用
手写统一节点流程伪装成四端同一 ABI。

## timelineControl 共同数据流

### 2026-08-16 V142 nested accessor/source-identity 补充

本节早期用 raw `propGet*` 表达的只是值流。四端 fresh decompile 已闭合精确访问层：
复制输入构造 loop-wide root `ncbPropAccessor`，一次快照 Count；每项 typed indexed
`GetValue<tTJSVariant>` 保留 raw source，再从第二份 Variant 副本构造 nested element
accessor。element accessor 执行 HRESULT-gated `HasValue(diff)`，其 scratch 在 bool
reread 前析构；随后的 bool/label typed reads 都消费写出值而不以 HRESULT 为 gate。
label push 后才做 HM3 raw owner 赋值，每项 element accessor 先于 source Variant 释放，
root 在整个循环之后释放。portable、probe、helper codegen 和 IDB readback 详见
`analysis/motionplayer_timeline_builder_nested_ncb_accessor_source_identity_four_binary_2026-08-16.md`。

以下伪代码继续描述容器结果与提交顺序；raw helper 名不再代表编译源码选择。

四端可以归一为：

```cpp
mainLabels.clear();
diffLabels.clear();

count = propGetCount(timelineControl);
for (index = 0; index < count; ++index) {
    elem = propGetByNum(timelineControl, index);

    probe = void;
    hasDiff = elem.PropGet(
        MEMBERMUSTEXIST, "diff", diffHint, &probe).succeeded();
    destroy(probe);

    target = hasDiff && propGetBool(elem, "diff", diffHint)
        ? diffLabels : mainLabels;
    label = propGetString(elem, "label", engineLabelHint);
    target.push_back(label);
    HM3[label].rawElement = elem;
}
```

实际顺序中的几点不能合并：

1. 先 clear main，再 clear diff；两者都释放现有 ttstr 元素、把 end 重置到 begin，
   但保留 vector capacity。
2. `diff` 存在性探测用 `TJS_MEMBERMUSTEXIST`，探测结果 Variant 在第二次读取前
   已析构。
3. 属性存在时才进行第二次普通 PropGet/布尔转换。因此脚本 getter 可观察两次
   独立调用；第一次成功不保证第二次返回同一值或不抛异常。
4. 缺少 `diff` 与存在但转换为 false 都进入 main；只有存在且第二次读取为 true
   才进入 diff。
5. label 先追加进目标 vector，随后才执行 HM3 `operator[]` 和 rawElement 赋值。
6. 不过滤空 label，不去重，也不清 HM3。

## TJS hint 身份

| hint | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| timeline `diff` | `0x1AB4F7C` | `0x1111514` | `0x101B6A02C` | `0x187DA4C` |
| Engine shared `label` | `0x1AB4F18` | `0x11114B0` | `0x101B69FC8` | `0x187D9E8` |

同一 `diff` slot 同时出现在 MEMBERMUSTEXIST probe 和第二次 bool helper 调用中。
`label` slot 则与 variable/eye/eyebrow/mouth/transition/selector/timeline 内部构建和
playing-info 路径共享。它们都是进程级可变 hint，不是函数栈临时缓存。

## 重复 label 与 HM3 `operator[]`

四端 `operator[]` 都先按缓存 hash 和 ttstr 内容查找，只有 miss 才分配节点。

| ABI | HM3 节点大小 | mapped value 起点 | `rawElement` 相对 mapped value |
|---|---:|---:|---:|
| Android arm64 | `0x88` | node `+0x10` | `+0x14` |
| Android armv7 | `0x70` | node `+0x10` | `+0x0C` |
| iOS arm64 | `0x88` | node `+0x18` | `+0x14` |
| iOS armv7 | `0x60` | node `+0x0C` | `+0x0C` |

新节点的 mapped value 共同默认状态为：pointer/flags/Variant/numeric fields 清零，
`blendWeight = 1.0f`，frame-cursor vector 为空。已有 key 则原样返回 mapped value，
builder 只替换 `rawElement`，不会重置：

- decoded `timelineData` owner；
- blend controller owner；
- flags；
- loop begin/end、last/current time；
- blend weight、auto-stop；
- frame cursor vector。

所以重复 label 的精确结果是：每次出现都保留在对应 main/diff vector 中，最后一个
元素的 raw metadata owner 胜出，但 HM3 的其余运行时状态继续存活。若直接调用
builder，旧而未再声明的 HM3 key 也继续存在。正常 metadata 替换之所以不会留下
这些状态，是因为调用链在进入 builder 前已经清过整个 HM3。

## rawElement 引用计数生命周期

四端 Variant copy-assign helper 的类型分支一致。对 object Variant：

1. 若非完全同址赋值，先分别 AddRef source object dispatch 与 source objthis；
2. 再清理 destination 的旧 Variant，Release 旧 owner；
3. 最后复制两个指针和 type tag。

对 string Variant 同样先增加 source 字符串引用，再释放 destination 旧值。因而
重复 label 覆盖不会在新旧值共享同一 dispatch 时制造瞬时零引用；循环局部 `elem`
和 accessor 随后析构，HM3 的 `rawElement` 保留完整 owner。HM3 clear/dtor 最终按
mapped-value 析构路径释放 raw Variant、frame cursors、timeline data 和 blend
controller。

## reset、直接调用与异常部分状态

`applyMetadata` 的常规链是：

```text
resetMetadataState
  -> clear HM3
  -> recreate variable TJS containers
  -> clear HM4/HM5
...
optional buildInstantVariableList
required buildTimelineControl
syncSelectorControls
```

active timeline vector 不由 metadata reset 清理；紧随 reset 的完整 controller reset
会通过 HM3 `operator[]` 为仍 active、但刚被清掉的 label 重新物化默认节点。这条
既有边界与本轮 builder 不清 active vector 的行为一致。

异常或手工直接调用时，部分状态按真实执行顺序保留：

- Timeline 输入转 object、取 count 或第一个元素就抛异常时，main/diff 已经清空，
  HM3 与 active vector 仍保持入口状态。
- 某元素在 diff/label 读取时抛异常，该元素尚未 push，先前元素已提交。
- label vector push 成功后，HM3 查找/节点分配若抛异常，vector 会多出该 label，
  HM3 却尚未拥有对应 rawElement。
- HM3 rawElement 赋值发生在 vector push 之后；不会回滚前面的列表变化。
- Instant builder 从不事务化：每个已插入 key 保留。Android 在重复 key 的候选
  节点分配点也可能抛异常，iOS 重复命中路径不会做节点分配。

这些路径没有本地补偿、预校验、去重或 rollback。

## 源码、测试与 IDB 落地

- `EmoteEngine.cpp` 新增 `timelineDiffHint_guess`，两次 `diff` 访问共用该 slot，
  `label` 读取改用 `engineLabelHint_guess`。
- 两个 builder 的旧单二进制地址型注释已替换成四端共同源级说明。
- `EmoteEngine.h` 明确 HM4 builder 不清、timeline builder 不清 HM3/active、重复
  label 最后 raw element 胜出且旧 HM3-only key 保留。
- 新增定向 Catch2 用例覆盖 HM4 union/去重/空 key，以及 timeline 的 absent/
  false/true diff 分类、列表清空、active 保留、stale HM3 保留、重复 label 状态
  保留与 last-raw-element ownership。
- 四个 IDB 中统一 builder / HM3 subscript / Android set-insert 命名、原型和
  source-level 注释；四个 `diff` hint 全局同步命名并保存。

## 验证

- Web Debug `motionplayer` 静态库编译通过。
- Wasmtime Headless Debug `motionplayer` 静态库编译通过。
- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` 使用 Web Debug 真实
  Emscripten 参数执行 `-fsyntax-only` 通过；只有仓库既有 `_tss` literal-operator
  弃用警告。
- Web `index.html` 完整链接通过；诊断只有既有 pthread/memory-growth、JSPI 和
  JS library warning。
- Wasmtime `krkr2_wasmtime_guest` 完整 wasm 链接及 exnref 转换通过。
- 两个完整目标复跑都收敛到 `ninja: no work to do.`。
- 定向 `git diff --check` 通过；只报告工作树既有 LF/CRLF 转换提示。
- 四个 IDB 均已重新反编译本纵向函数并原位保存。
