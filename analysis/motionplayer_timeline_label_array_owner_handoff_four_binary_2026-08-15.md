# MotionPlayer timeline-label Array owner / return handoff 四参考二进制复原（2026-08-15）

## 结论

从四个当前参考二进制重新反编译 main/diff timeline-label 查询体与三端抽出的
String-Variant deque push helper后，确认两函数拥有完全相同的源结构，唯一差别是
输入 `vector<ttstr>` 成员。

每次调用创建一个 fresh TJS Array。局部 helper 同时保存 owning Array closure 与指向
`tTJSArrayNI::Items` 的 borrowed pointer；函数快照 source vector 的 begin/end，按原顺序
直接在 Items deque 末尾构造 String Variant。循环完成后，owning Array Variant 显式
CopyRef 到 ABI 隐藏返回对象，随后局部 helper 析构。返回 Array及其中每个 string handle
均独立拥有引用，不借用 Engine vector 的生命周期。

## 四端函数映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `EmoteEngine_getMainTimelineLabelList_guess` | `0x672334` / `0x16C` | `0x55B5C8` / `0x5C` | `0x1001AEF14` / `0x74` | `0x1AE6F4` / `0xA4` |
| `EmoteEngine_getDiffTimelineLabelList_guess` | `0x6724A0` / `0x16C` | `0x55B63C` / `0x5C` | `0x1001AEFA0` / `0x74` | `0x1AE7C8` / `0xA4` |
| `createTJSArrayWithItems_guess` | `0x702098` / `0xFC` | `0x5BAA70` / `0x8A` | `0x10029FF58` / `0xC4` | `0x2A4A80` / `0xE4` |
| `TJSArrayItems_pushBackString_guess` | inline in both bodies | `0x4EA126` | `0x1001024C4` | `0xFF7E0` |

Android ARMv7 与 iOS ARMv7 注册表保存 Thumb 奇地址，因此有效 data xref 分别指向
`0x55B5C9/0x55B63D` 与 `0x1AE6F5/0x1AE7C9`；偶地址查询没有 xref并不表示未注册。
四端注册 xref 均回到 `EmotePlayer_ncb_registerMembers_guess`，未发现普通业务 caller。

## 共同 owner / 数据流

共同伪代码是：

```cpp
Variant getTimelineLabelList(const vector<ttstr> &source) {
    ArrayWithItems local = createTJSArrayWithItems();
    auto current = source.begin();
    const auto endSnapshot = source.end();
    while (current != endSnapshot) {
        local.items->emplace_back(*current); // direct String Variant CopyRef
        ++current;
    }
    Variant returned(local.value);          // CopyRef into hidden return
    destroy(local);                         // release local Array closure
    return returned;
}
```

精确顺序为：

1. Array factory 返回 raw dispatch；
2. local owning Variant 以同一 dispatch 构造 Object/ObjThis closure；
3. factory raw reference被释放；
4. `NativeInstanceSupport(GETINSTANCE, ArrayClassID)` 只在严格 `TJS_S_OK` 时发布
   `&native->Items`，该指针本身不 AddRef；
5. source begin/end 各读取一次，循环期间不重新读取 Engine vector end；
6. 每个元素在 deque 尾槽直接写 type tag `2` 和源 string handle，再原子 AddRef handle；
7. 循环后 CopyConstruct local owning Variant 到 sret；
8. helper 析构并释放 local closure，sret 保留最终 Array owner。

因此它不是 Engine 内部 Array 的共享视图，也不是先构造 `std::vector<tTJSVariant>` 再
批量转换。Engine、源 vector和源 label 全部销毁后，返回值仍保持 Array及字符串有效。

## deque ABI 与块策略

| ABI | STL | Variant stride | 每块元素数 | block bytes |
|---|---|---:|---:|---:|
| Android ARM64 | old libstdc++ deque | `20` | `25` | `500` (`0x1F4`) |
| Android ARMv7 | old libstdc++ deque | `12` | `42` | `504` (`0x1F8`) |
| iOS ARM64 | libc++ deque | `20` | `204` (`0xCC`) | `4080` |
| iOS ARMv7 | libc++ deque | `12` | `341` (`0x155`) | `4092` |

Android ARM64 编译器把 push 完全内联进两个查询体；另外三端复用已恢复名的 helper。
这是 STL/ABI 代码生成差异，不是算法分支。本地继续使用自然
`std::deque<tTJSVariant>`，不把任一参考平台的 deque header或 block size硬编码进 Web
源码。

新槽只初始化当前 Variant 类型需要的字段：64 位写 handle `+0`、type `+16`，32 位写
handle `+0`、type `+8`。String Variant 的其余 payload 空间不参与该类型语义。null
`ttstr` handle原样写入且不 AddRef，故空 label 仍作为一个 String Variant element保留，
不会被当成 Void或跳过。

## 边界行为

- fresh Array 在读取 source vector 前创建；即使 source 为空也返回新的空 Array；
- 不过滤空 label，不去重，不排序；重复项各自 AddRef并各占一个 deque element；
- 不预先 reserve，不根据 source size 一次性分配；跨块时按对应 STL 的 map/block增长；
- begin/end 在进入循环前快照。正常实现中循环没有脚本调用或容器回调，故不会主动
  产生 reentrancy；若外部未同步修改 Engine vector，迭代顺序稳定；
- fresh Array 的 native lookup没有 caller 级 null guard。若 lookup失败且 source非空，
  首次 append会解引用 null Items；若 source为空则不会触及 borrowed pointer，仍走返回
  handoff。built-in Array factory成功是参考实现依赖的不变量；
- deque/map/block分配抛异常时，已构造的 Array owner与先前 String Variant由局部 helper
  unwind清理；尚未成功提交的尾槽不增加逻辑 size；
- 返回 CopyRef发生在完整循环之后，所以异常前缀不会作为部分 Array正常返回。

没有 HRESULT、fallback Array、脚本 `add` 调用或部分成功状态需要传播。

## 与本地实现对照

本地两个函数已经使用：

```cpp
detail::TJSArrayWithItems_guess result =
    detail::createTJSArrayWithItems_guess();
for (const ttstr &label : source)
    result.items->emplace_back(label);
return result.value;
```

C++ range-for同样在循环前取得 begin/end；`emplace_back(label)` 直接构造 String Variant；
返回成员 lvalue对 owning Variant执行 CopyRef，随后 `result` 析构。因此无需语义改写。
本轮只补充了 owner/return 注释，并新增 Engine析构后 escaped Array/string 仍有效的回归。

## IDB 回写与验证

四份 recovery IDB 的两个查询入口均补充：fresh Array owner、borrowed Items、String
CopyRef、deque块策略和 return CopyRef/owner-destroy顺序；并添加统一 bookmark。三端
抽出的 push helper已保留语义名和块策略注释。

验证项目：

- 单元测试 translation unit 使用真实 Emscripten response file执行 syntax-only并通过，
  唯一输出是仓库既有 `_tss` literal-operator弃用警告；
- Web Debug完成 3 个增量步骤并成功链接最终 `index.html`；
- 对 `EmoteEngine.cpp`、测试、旧 timeline-list总览、本文和 `plan.md` 执行定向
  `git diff --check`；
- 四份 recovery IDB 保存成功。
