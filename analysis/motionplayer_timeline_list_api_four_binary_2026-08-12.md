# MotionPlayer timeline 列表 API 四端审计（2026-08-12）

## 结论

本纵切面从四份当前 `reference/binaries/` 的 `EmotePlayer` NCB 注册表反向定位
三个按值返回 `tTJSVariant` 的列表 API，不沿用源码名中的旧 `libkrkr2.so`
地址。旧地址在当前 Android ARM64 中已经属于完全不同的状态序列化函数：

- 旧 `0x674F54` 位于当前 `EmoteEngine_serializeSelectorState_guess`；
- 旧 `0x6750C0` 位于当前 `EmoteEngine_serializeBaseState_guess`；
- 旧 `0x6754C4` 位于当前 `EmoteEngine_unserializeState_guess`。

因此源码中的 `Like_0x674F54` / `Like_0x6750C0` / `Like_0x6754C4`
后缀已经失去证据意义。四端共同源码角色分别是主 timeline label 数组、diff
timeline label 数组，以及活动 timeline 的信息字典数组。

## 四端映射

| 源码角色 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `EmoteEngine_getMainTimelineLabelList_guess` | `0x672334` / `0x16C` | `0x55B5C8` / `0x5C` | `0x1001AEF14` / `0x74` | `0x1AE6F4` / `0xA4` |
| `EmoteEngine_getDiffTimelineLabelList_guess` | `0x6724A0` / `0x16C` | `0x55B63C` / `0x5C` | `0x1001AEFA0` / `0x74` | `0x1AE7C8` / `0xA4` |
| `EmoteEngine_getPlayingTimelineInfoList_guess` | `0x6728A4` | `0x55B788` | `0x1001AF104` | `0x1AE9D0` |

定位链为：宽字符串 `getMainTimelineLabelList`、
`getDiffTimelineLabelList`、`getPlayingTimelineInfoList` → NCB 注册函数 →
注册描述符中的实际回调地址。四端均重新失效并生成了 Hex-Rays 伪代码；不是用
某一端地址平移猜测另外三端。

## 内部 vector 布局

三个 API 分别遍历 Engine 中连续声明的三个 `vector<ttstr>`。自然 ABI 起止槽为：

| 参考 | main begin/end | diff begin/end | active begin/end |
|---|---:|---:|---:|
| Android ARM64 | `+992/+1000` | `+1016/+1024` | `+1040/+1048` |
| Android ARMv7 | `+496/+500` | `+508/+512` | `+520/+524` |
| iOS ARM64 | `+624/+632` | `+648/+656` | `+672/+680` |
| iOS ARMv7 | `+312/+316` | `+324/+328` | `+336/+340` |

这些偏移差异来自 libstdc++/libc++、指针宽度及前置成员自然布局，不代表平台条件
字段。共同源结构应继续使用三个 typed vector，不能把任何一端的 Engine 偏移写进
编译源码。

## main / diff label 数组

两个函数只有输入 vector 不同，共同行为是：

1. 每次调用创建一个新的 TJS Array；
2. 从 `begin` 到 `end` 按原顺序遍历 `ttstr`；
3. 为每个元素构造字符串 `tTJSVariant` 并追加到 Array 的原生 items 容器；
4. 按值返回持有 Array dispatch 的 `tTJSVariant`。

没有 HM3 查询、过滤、排序、去重或空字符串跳过。重复 label 和空 label 都原样
保留；多次调用返回不同的 Array 对象，Engine vector 不被修改。

四端反编译中看见的隐藏返回指针/寄存器差异只是 C++ 聚合按值返回 ABI：64 位端
使用隐藏的 sret 寄存器，32 位端把返回槽作为显式首参。它不是额外的源级参数。

2026-08-15 再次 fresh 检查四端 owner 与 deque append 后，进一步确认第 4 步是显式
CopyRef：局部 helper 先持有 fresh Array closure 与 borrowed `Items` 指针；循环结束后
把 owning Variant CopyRef 到隐藏返回对象，再销毁 helper。每个 append 直接在 deque
槽中构造 type=2 String Variant，并对源 `ttstr` handle AddRef；没有中间 string/vector
临时量。四种块策略和异常前缀详见
`analysis/motionplayer_timeline_label_array_owner_handoff_four_binary_2026-08-15.md`。

## playing info 数组

共同伪代码为：

```cpp
result = new TJS Array;
for (label : activeTimelineLabels) {
    state = compoundHM3.find(label);  // 非插入查询
    if (state == end)
        continue;

    item = new TJS Dictionary;
    item["label"] = label;
    item["flags"] = int32(state.flags);
    item["blendRatio"] = double(state.blendWeight);
    result.push(item);
}
return result;
```

边界行为：

- 遍历顺序完全跟随 active vector；
- HM3 miss 被跳过，而且不会物化默认节点；
- active vector 中相同 label 出现多次时，每次都会重新查询并创建一个独立 Dictionary；
- Dictionary 恰好按 `label`、`flags`、`blendRatio` 顺序写入三个成员；
- `label` 是字符串 Variant，`flags` 从 32 位字段构造整数 Variant，
  `blendRatio` 从原生 `float` 提升为 TJS real；
- 三次 `PropSet` 都带 `TJS_MEMBERENSURE`，且每个 key 使用独立的进程级
  `tjs_uint32` member-hint 槽；
- fresh Dictionary 先由原始 Object Variant 持有；函数复制该 closure、强制 Object、让
  `ncbPropAccessor` 单独 retain dispatch，并在首个字段前销毁复制临时量。Array append
  复制的是原始 Dictionary Variant；随后 accessor 与原 Variant 依次释放，Array element
  保有自己的 Object/ObjThis 引用，没有泄漏或借用悬空。四端 handoff 与异常矩阵的 fresh
  补证见
  `analysis/motionplayer_playing_timeline_info_dictionary_handoff_four_binary_2026-08-15.md`。

旧本地实现的标签/flags/blend 数值与 miss-skip 流程已经正确，但三个 `PropSet`
传入了空 hint。四端均明确传入三个不同的静态 hint 地址，因此本轮补回
`timelineInfoLabelHint_guess`、`timelineInfoFlagsHint_guess` 和
`timelineInfoBlendRatioHint_guess`。

## IDB 改进

四个 IDB 已完成：

- 三个函数统一命名为上述 `_guess` 名称；
- 每个函数写入共同数据流、容器和边界注释；
- 修复 `flags` 与 `blendRatio` 两个相邻 UTF-16 key 的数据边界。原先三份 IDB
  把它们显示成单字符 `"f"` / `"b"`；现在均为 12 字节和 22 字节的完整数组；
- 修复三个 NCB 注册名的完整 UTF-16 数据边界，分别恢复为 50、50、54 字节；
- 强制刷新 playing-info 伪代码，确认四端都引用修复后的完整 key 数据符号。

随后再次刷新四端 NCB 注册函数，确认三组完整注册名与三个新命名回调正确配对，
并将四个 IDB 原位保存成功。

## 源码与测试

源码变更：

- 删除三个旧地址后缀，改为 ABI 无关的 `_guess` 名；
- 移除函数体中的旧单端地址行尾注释；
- 补回 playing-info 三个静态 TJS member-hint 槽；
- playing-info item 恢复 owning Dictionary Variant -> copied/forced Object -> retained
  accessor -> copied temporary early-destroy -> original Variant Array publication 的完整 owner
  handoff；三个字段 Variant 改回逐调用构造/销毁；
- main/diff getter 补充 fresh Array owner、borrowed Items 和 return CopyRef 注释，并新增
  Engine 析构后返回 Array/string 仍独立存活的回归；
- 保持 vector/HM3/Array/Dictionary 的自然 C++ 所有权。

新增测试覆盖：

- main/diff 数组保留顺序、重复 label 与空 label；
- 每次调用创建新 Array；
- playing info 对 HM3 miss 非插入并跳过；
- 重复 active label 生成两个不同 Dictionary；
- 每个 Dictionary 只含 `label`、`flags`、`blendRatio` 三项且值类型/数值正确。

验证结果：

- Web `motionplayer` 静态库目标通过；
- Wasmtime `motionplayer` 静态库目标通过；
- `tests/unit-tests/plugins/motionplayer-dll.cpp` 使用 Web Debug 的真实
  Emscripten 参数执行完整 `-fsyntax-only` 通过，仅有仓库既有的 `_tss`
  literal-operator 弃用警告；
- Web 完整 `index.html` 链接通过；
- Wasmtime 完整 `krkr2_wasmtime_guest.wasm` 链接及 exnref 转换通过；
- `git diff --check` 通过，仅报告工作树中既有的 LF/CRLF 转换提示；
- 本轮环境没有可直接运行的 Catch2 motionplayer 测试目标，因此这里只声明编译、
  链接和语法检查结果，不声明 Catch2 运行时通过。
