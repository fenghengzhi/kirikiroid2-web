# MotionPlayer instant-variable builder root ncb accessor / indexed ttstr 四参考二进制复原（2026-08-16）

## 范围与结论

本纵切面只重新核对 `EmoteEngine::buildInstantVariableList_guess` 的脚本对象访问层与
临时 owner 生命周期。HM4 `unordered_set<ttstr>` 的节点 ABI、rehash 和平台 STL 差异
仍以既有 HM4 纵切面为准。

四份 `reference/binaries/` 当前参考产物共同证明：builder 并不是对传入 Variant 反复
调用便携层 raw property helper，而是先复制输入 Variant、用该副本构造一个覆盖整个
循环的 `ncbPropAccessor`，只读取一次 `Count`，再用同一 accessor 的 typed indexed
`GetValue<ttstr>` 依次读取元素。这个 owner 形状已经写回 portable 源码，并由可重入
owner-drop probe 锁定。

## 四端函数映射

| 参考 | builder | 大小 | indexed-string 相关 helper |
|---|---:|---:|---:|
| Android arm64 | `0x66CA2C` | `0x1C0` | wrapper 内联；`0x683AD0` 是共享 Variant→`ttstr` 叶子 |
| Android armv7 | `0x558DBC` | `0xA6` | `0x52E2C4` |
| iOS arm64 | `0x1001AB6E4` | `0x2D0` | `0x100108690` |
| iOS armv7 | `0x1AAE18` | `0x2DE` | `0x105DAC` |

builder 地址由本轮四端 `applyMetadata` 调用位置重新确认。表中的绝对地址仅用于 IDB
对应和证据复查，不进入编译源码注释。

## 共同源级结构

四端可以归一为：

```cpp
void buildInstantVariableList(const tTJSVariant &input) {
    tTJSVariant copiedInput(input);
    ncbPropAccessor root(copiedInput);
    destroy(copiedInputTemporary);

    const int count = root.GetArrayCount();
    for (int index = 0; index < count; ++index) {
        const ttstr value = root.GetValue(index, Tag<ttstr>());
        instantVariableLabels.insert(value);
    }
    destroy(root);
}
```

反编译器在四个 ABI 上对栈临时变量、对象指针拆分和 STL insert 展开的表达不同，
但以下次序完全一致：

1. 从 caller 提供的 const Variant 复制出本地 owner；
2. 用复制值构造 loop-wide `ncbPropAccessor`；
3. accessor 已 retain dispatch 后，构造临时 Variant 在 Count 前析构；
4. `Count` 只读取一次，结果快照为循环上界；
5. 每个索引进行一次 flags=0、hint=null 的 numeric property read；
6. getter 写出的 Variant 直接转换为 `ttstr`，再插入既有 HM4；
7. 每轮字符串/Variant 临时量先释放，root accessor 在整个循环之后最后释放。

因此脚本 getter 在第一次 indexed read 中可重入地清除 caller 的最后一个输入 owner，
但不能销毁当前 dispatch：root accessor 自己仍持有引用。反过来，builder 返回后不再
保留脚本数组，只保留插入 HM4 的 `ttstr` key owner。

## TJS 调用边界

`GetArrayCount()` 的底层调用是：

- member name `count`；
- flags `0`；
- hint `nullptr`；
- receiver 与 objthis 均为 accessor 保存的同一 dispatch。

indexed `GetValue<ttstr>` 的底层调用是：

- 数字索引严格按 `0 .. count-1` 递增；
- flags `0`；
- receiver 与 objthis 同一；
- 没有 named-property hint；
- property call 写出可用 Variant 后即使返回失败 HRESULT，typed 转换仍消费写出的值。

最后一点不是 portable 层额外容错，而是 ncb typed accessor 的实际返回值路径：它未以
HRESULT 作为该次转换的 gate。测试 probe 刻意在写出 Count/字符串后返回
`TJS_E_FAIL`，用来区分这一行为与 raw helper 的成功检查。

## helper 身份与跨调用者复用

本轮 fresh decompile/xref 修正了 recovery IDB 中遗留的局部来源标签：

- Android armv7、iOS arm64、iOS armv7 的三个函数都是通用
  `ncbPropAccessor::GetValue<ttstr>(index)` 模板实例；每端有 6 个调用者，覆盖 Mirror、
  InstantVariable、motion 初始化、NodeTree、particle update 和另一 typed caller。它们
  已统一改名为 `ncbPropAccessor_GetValueArrayString_guess`。
- Android arm64 编译器将 indexed accessor wrapper 内联到各 caller；`0x683AD0` 只承担
  Variant-to-`ttstr` 的共享转换/引用计数叶子，fresh xref 有 24 个调用者。它既不是
  indexed getter，也不应因任一 caller 命名，故保留 `sub_683AD0`。

旧的 `_NodeTree_guess` 和“stencil-composite post-pass”文字都是先前沿单个调用点推断
出的过时来源标签。三个保留模板 helper 的 function comment 目前保留历史行，并追加
V141 correction，明确其通用身份；函数名、反编译调用点和新注释均已 readback。

## HM4 提交与边界行为

这次 owner/accessor 修正不改变已恢复的 HM4 语义：

- builder 不 clear，重复调用与入口既有 key 做集合并集；
- 不过滤空字符串、非字符串 Variant 或重复 key；转换结果原样插入；
- `count <= 0` 时不触碰 HM4；
- Count 是入口时的一次快照；getter 修改脚本对象的后续 count 不改变循环上界；
- 任一 indexed read、转换或 insert 抛出时，已成功插入的前缀不回滚；
- Android old-libstdc++ 在 duplicate lookup 前分配候选节点，duplicate 也可能在 allocation
  处失败；iOS libc++ 先查重，duplicate hit 不分配节点。这一差异继续由目标平台 STL
  自然提供。

## portable 源码与测试落地

`EmoteEngine.cpp` 的实现现为：

```cpp
ncbPropAccessor controlObject{tTJSVariant(instantVariableList)};
const int count = static_cast<int>(controlObject.GetArrayCount());
for(int index = 0; index < count; ++index) {
    const ttstr value = controlObject.GetValue(
        index, ncbTypedefs::Tag<ttstr>());
    _instantVariableLabels.insert(value);
}
```

新增 Catch2 probe 覆盖：

- 入口 HM4 的 preexisting key 保留；
- 三项 `a, a, b` 形成两个新 key，重复项折叠；
- Count 只读一次，numeric index 精确为 `0,1,2`；
- Count/numeric flags 全为 0，Count hint 为 null，objthis 精确等于 dispatch；
- Count 与三个 numeric getter 都在写值后返回失败 HRESULT，值仍被消费；
- index 0 getter 可重入清除 caller 的输入 Variant，root accessor 仍使 dispatch 存活；
- helper 返回后 root dispatch 恰好析构一次，没有悬空 owner 或额外 retain。

## IDB 落地

四个 recovery IDB 都写入 V141 builder comment/bookmark，force-recompile 后重新
decompile 并完成 comment readback：

| 参考 | builder comment readback | helper 处理 |
|---|---:|---|
| Android arm64 | 6 处 | 确认 `0x683AD0` 是 24-caller 转换叶子，不误命名 |
| Android armv7 | 5 处 | helper rename、comment、force-recompile、1 处 readback |
| iOS arm64 | 5 处 | helper rename、comment、force-recompile、1 处 readback |
| iOS armv7 | 5 处 | helper rename、comment、force-recompile、1 处 readback |

四个 IDB 均已在 readback 后原位保存。

## 验证

- 普通与 `KRKR2_WASMTIME_HEADLESS=1` 两套完整 test TU `-fsyntax-only` 均通过；只有仓库
  既有 `_tss` literal-operator 弃用 warning。
- Web Debug 完整增量构建和最终 wasm 链接通过。
- Wasmtime Headless Debug 完整增量构建和最终 guest wasm 链接通过；为排除并行加载
  Emscripten 环境产生的临时文件竞态，随后串行复跑收敛为 `ninja: no work to do.`。
- `out/web/debug/index.wasm` 与
  `out/wasmtime/debug/krkr2_wasmtime_guest.wasm` 均由 Node
  `WebAssembly.Module` 成功解析。
- 定向实现范围扫描：旧 `motionPropGet*` 为 0；`ncbPropAccessor`、`GetArrayCount`、
  typed indexed `GetValue` 各 1 处。
- 限定 `git diff --check` 通过；仅有工作树既有 LF/CRLF 转换提示。

这些 Ninja 步数和 wasm parse 只证明当前 portable 构建闭合，不代替四端行为证据；
owner、调用参数和失败后写值边界由上述 fresh decompile 与 probe 共同锁定。
