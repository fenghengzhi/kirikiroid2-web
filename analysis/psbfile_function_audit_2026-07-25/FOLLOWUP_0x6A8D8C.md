# Follow-up：`ResourceManager_loadResource@0x6A8D8C` 的 label 数据流与临时量生命周期

日期：`2026-07-26`。本函数不属于 psbfile 114-address MANIFEST；它是
`PSBFile::LoadStorage/GetRoot/Transfer` 与 raw-node getter 的真实 motionplayer consumer，
因此只作为跨模块 caller follow-up 记录，不改变 114 个 emitted 入口的统计分母。

## fresh Android 证据

本轮 fresh 调用 IDA MCP `decompile(addr="0x6A8D8C")`，完整取得
`ResourceManager_loadResource`；另 fresh 调用 `decompile(addr="0xA13878")` 确认其
narrow `ttstr` helper 的 null/empty 边界，并结合 fresh EH cleanup 证据核对临时对象
析构顺序。

- `0x6A90EC`：严格读取 root dictionary 的 `"label"`；
- `0x6A9100..0x6A910C`：`PSBRawNode_GetString_guess` 返回 borrowed `const char *`，
  raw-node 临时量仍存活；返回值未经 `CBZ/CSEL` 或空字面量替换，直接作为 X0
  传给 `sub_A13878` 构造 `ttstr`；
- `0x6A911C`：生成的 `ttstr` 直接传给精确异常文本
  `L"motion file '%1' has not adaptive spec. export psb again."`；
- throw landing pad `0x6A93C4` 先清理该 `ttstr`，随后才经 `0x6A93EC`、
  `0x6A9468..0x6A9484` 清理 raw-node 临时量；这证明 raw-node 并非在
  `GetString()` 后立即销毁，而是活过 narrow `ttstr` 构造与 throwing call；
- `sub_A13878@0xA13878` 自身先执行 `if (!p || !*p) return empty`，否则分配并转换
  narrow 字符串。因此 caller 不需要、也没有先把 null 归一化为 `""`。

## Android 伪代码（8 行）

```text
if spec is neither "krkr" nor "win":
    labelNode = root.GetDictionaryValueStrict("label")
    label = labelNode.GetString()
    messageArg = ttstr_from_narrow(label)       // 此时 labelNode 仍存活
    Throw(L"motion file '%1' has not adaptive spec. export psb again.", messageArg)
    on throw: destroy(messageArg)
    on throw: destroy(labelNode)
ttstr_from_narrow(p): if !p or !*p return empty; else allocate_and_convert(p)
```

## 本地逐行对照与闭环

- `cpp/plugins/motionplayer/ResourceManager.cpp:302-379` 是目标完整本地函数；
- `:340-347` 将 strict lookup、`GetString()`、narrow `ttstr` 构造和精确错误文本保留在
  同一个 throwing full-expression，对应 Android `0x6A90EC..0x6A911C`；
- 修复前先以 `const char *label = ...GetString();` 结束一个独立语句，raw-node 临时量会在
  分号处销毁；下一语句才构造 `ttstr`，与 `0x6A9100..0x6A910C` 及 EH cleanup 顺序冲突；
- 修复前的 `ttstr(label != nullptr ? label : "")` 还在 caller 增加了 Android 不存在的
  null→empty 归一化。虽然 `sub_A13878` 让 null/empty 输入结果相同，但它改变了源码
  数据流和 caller 的判断职责；
- 当前 `:344-347` 的
  `ttstr(root.GetDictionaryValueStrict("label").GetString())` 让 borrowed pointer 原样进入
  narrow constructor，并让 raw-node 临时量保持到整个 throwing full-expression 结束；
  异常展开时先析构 `ttstr`、再析构 raw-node，复刻 `0x6A93C4` 后的 cleanup 次序。

## 六维影响

| 维度 | 结论 |
| --- | --- |
| 源代码结构 | 删除独立 `label` 语句与 caller 自造的 null-normalization，恢复链式 getter → narrow `ttstr` constructor 的单一 full-expression。 |
| 数据流 | `GetString()` 的原始 borrowed pointer 不再被 caller 改写，也不会跨 raw-node 临时量的语句边界悬空。 |
| 调用链 | strict raw getter → string getter → `sub_A13878` → throw helper 保持连续，和 Android 调用次序一致。 |
| 对象生命周期 | 已纠正旧结论：raw-node 在 `ttstr` 构造及 throwing call 期间存活；异常路径先清 `ttstr`、再清 raw-node，对齐 `0x6A93C4`、`0x6A93EC`、`0x6A9468..0x6A9484`。 |
| 内部容器实现 | `N/A`；本闭环不触碰 map、packed array 或 TJS collection。 |
| 边界行为 | null/empty 仍都由 `sub_A13878` 产生 empty ttstr；职责位置和首个处理边界与 Android 一致。 |

## 验证

- `motionplayer-ttstr-hash-test`：`109 assertions in 24 test cases`；
- `motionplayer-dll`：`1386 assertions in 21 test cases`；
- `psbfile-dll`：`577 assertions in 10 test cases`。

现有原生 fixture 未触发 invalid-spec throw；该分支的生命周期结论来自 fresh Android
反编译与 EH cleanup 证据。最终当前源码的 Web Debug 与 Wasmtime guest 均已重编、链接
通过；m2logo/yuzulogo 完整捕获 25/63 帧并保持汇总记录的 trace hash，structural
comparator 仍复现既有 31/21 mismatch。健康 fixture 不触发本分支，这些结果只作
非回归守护；完整结果见 [SUMMARY.md](SUMMARY.md)。
