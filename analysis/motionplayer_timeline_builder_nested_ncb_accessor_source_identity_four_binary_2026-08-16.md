# MotionPlayer timeline builder nested ncb accessor/source identity 四参考二进制复原（2026-08-16）

## 范围与结论

本纵切面重新从 `reference/binaries/` 的 Android arm64、Android armv7、iOS arm64、
iOS armv7 当前参考产物恢复 `EmoteEngine::buildTimelineControl_guess` 的完整对象访问层、
owner 区间、HRESULT 分叉和提交顺序。

既有 portable 容器结果大体正确，但仍把 native ncb 访问层写成了 raw
`motionPropGet*` / `motionTryPropGet`。四端共同实现实际是：

1. 先清 main/diff 两个 label vector；
2. 复制输入 Variant，建立覆盖整个循环的 root `ncbPropAccessor`；
3. Count 只快照一次；
4. 每项以 typed indexed `GetValue<tTJSVariant>` 取得并保留 raw source；
5. 再从该 source 的第二份 Variant 副本建立 nested element accessor；
6. element accessor 依次执行 `HasValue(diff)`、可选 typed bool read、typed label read；
7. label vector push 先提交，HM3 `operator[]` / `rawElement` owner 赋值后提交；
8. 每项依次释放 label、element accessor、raw source，循环结束后才释放 root accessor。

portable 已按这条 owner/source identity 改写；新增 probe 同时覆盖写值后失败 HRESULT、
MEMBERMUSTEXIST scratch 析构、可重入 owner drop 与 HM3 最终所有权。

## 四端函数映射

| 参考 | builder | 大小 |
|---|---:|---:|
| Android arm64 | `0x66CBEC` | `0x450` |
| Android armv7 | `0x558EB4` | `0x17E` |
| iOS arm64 | `0x1001ABA30` | `0x264` |
| iOS armv7 | `0x1AB18C` | `0x236` |

地址由本轮四端 `applyMetadata` caller 和函数边界 fresh 复核；绝对地址只保留在
`analysis/` 和 recovery IDB，不进入新的编译源码注释。

## 共同源级结构

四端可归一为：

```cpp
mainLabels.clear();
diffLabels.clear();

ncbPropAccessor root{tTJSVariant(timelineControl)};
const int count = root.GetArrayCount();
for (int index = 0; index < count; ++index) {
    const tTJSVariant source =
        root.GetValue(index, Tag<tTJSVariant>());
    ncbPropAccessor element{tTJSVariant(source)};

    const bool hasDiff =
        element.HasValue(L"diff", timelineDiffHint);
    vector<ttstr> &target =
        hasDiff && element.GetValue(
            L"diff", Tag<bool>(), 0, timelineDiffHint)
        ? diffLabels : mainLabels;

    const ttstr label = element.GetValue(
        L"label", Tag<ttstr>(), 0, engineLabelHint);
    target.push_back(label);
    timelineStates[label].rawElement = source;
}
```

`source` 不能被折叠成 element accessor 内的一份 borrowed dispatch：它在 HM3
`rawElement` 赋值时仍是独立的完整 Variant owner。element accessor 则保证 named
getter 内可重入地丢弃其他 owner 时，当前 dispatch 仍存活。

## root owner 与 indexed Variant getter

root 构造的精确生命周期为：

```text
copy caller Variant
  -> ncbPropAccessor converts/retains dispatch
  -> constructor temporary Variant dies
  -> Count
  -> all indexed items
  -> root accessor Release
```

Count 的底层 `PropGet` 参数为 flags=0、name=`count`、hint=null，receiver 与 objthis
均为 root 保存的 dispatch；HRESULT 不参与结果 gate。getter 若写出整数后返回失败，
Count 仍从写出的 Variant 转换。

typed indexed `GetValue<tTJSVariant>` 同样使用 flags=0、递增索引、receiver=objthis。
其 helper 流程是：初始化临时 Variant、`PropGetByNum`、忽略 HRESULT、把临时 Variant
copy-construct 到 caller return slot、销毁 getter 临时。因此 index getter 写出 element
后返回失败仍会进入后续 element accessor 与提交路径。

## nested element owner 与 diff 分叉

每项同时存在两条 owner：

```text
root indexed getter result
  -> source Variant -------------------------------> HM3 rawElement
       \-> copied Variant -> element accessor -> diff/label reads
```

element accessor 的构造副本在 accessor retain dispatch 后立即销毁，但 `source` 本身一直
保留到该项 HM3 赋值完成。named read 的 receiver 和 objthis 均为 element dispatch。

### `HasValue(diff)`

`HasValue` 是唯一以 HRESULT 判定分支的访问：

- flags=`TJS_MEMBERMUSTEXIST`；
- 使用 timeline `diff` 共享 hint；
- scratch Variant 无论成功或失败都会在 helper 返回前析构；
- 任一非负 TJS status 视为存在；负值视为不存在；
- `type` 参数为 null，因此不发布 scratch type。

即使失败 getter 写出了值，`hasDiff` 仍为 false，而且写出值只在 scratch 析构时释放，
不会触发第二次 bool read。

### typed bool 与 label read

仅当 `HasValue` 成功时才进行第二次 `diff` read。它使用 flags=0、同一个 diff hint，
但和 typed indexed getter 一样忽略 HRESULT：写出 truthy 值后返回失败，仍选择 diff
vector。

label read随后无条件发生，使用 flags=0 和共享 Engine label hint。它也忽略 HRESULT，
把写出的 Variant 转换成独立拥有 backing string 的 `ttstr`，再销毁临时 Variant。
因此 MEMBERMUSTEXIST probe、bool read 和 label read 是三个可分别被脚本观察的调用；
不能缓存第一次 diff 值，也不能把 probe 和 bool read 合并。

## hint 身份

| hint | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| timeline `diff` | `0x1AB4F7C` | `0x1111514` | `0x101B6A02C` | `0x187DA4C` |
| Engine shared `label` | `0x1AB4F18` | `0x11114B0` | `0x101B69FC8` | `0x187D9E8` |

同一 diff slot 供 `HasValue` probe 和第二次 bool read 共用。label slot 继续与其他
controller builder、list/state 路径共享；它不是 timeline-local hint。

## helper codegen 拓扑

Android armv7、iOS arm64、iOS armv7 保留四个通用 ncb helper 实例：

| helper | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|
| indexed `GetValue<tTJSVariant>` | `0x5334E0` | `0x1000691F8` | `0xED9A8` |
| named `HasValue` | `0x496BE8` | `0x10010905C` | `0x10686C` |
| named `GetValue<bool>` | `0x552124` | `0x1000F3078` | `0xEF7F0` |
| named `GetValue<ttstr>` | `0x492100` | `0x1000F18DC` | `0xEDCB0` |

本轮 fresh decompile 逐个确认其 HRESULT、objthis 和临时析构行为。Android arm64 的
编译器形状不同：indexed Variant getter 与 `HasValue` 展开在 builder 内；bool 仍调用
typed helper；label 的 PropGet 展开后调用共享 Variant→`ttstr` 叶子 `0x683AD0`。后者
在 V141 已确认有 24 个 caller，不能按 Timeline 或 indexed getter 命名。

这些差异只属于优化和模板实例化边界，不改变共同 source-level owner 模型。

## 容器提交顺序与重复 label

main vector 先 clear，diff vector 后 clear；两者析构旧 ttstr 元素并把 end 退到 begin，
但保留 capacity。active label vector 和 HM3 从入口到循环均不做整体 clear。

每项提交严格为：

```text
typed label acquired
  -> target vector push_back(label)
  -> HM3 operator[](label)
  -> mapped.rawElement = source
  -> release local label
  -> release element accessor
  -> release source Variant
```

所以：

- 重复 label 在 main/diff declaration vector 中全部保留；
- HM3 existing key 不重置 decoded data、controller、flags、times 或 frame cursors；
- 每次只替换 `rawElement`，最后一次同名声明的 raw owner 胜出；
- 旧而未再声明的 HM3 key 和 active vector 均保留；
- HM3 raw owner 在 element/source/root 三组循环局部都析构后继续保活脚本对象。

## 异常与边界行为

- 两个 declaration vector 在任何输入转换/Count 异常之前已经 clear；HM3/active 保持
  入口状态。
- `count <= 0` 时无 item read，root 随后释放；main/diff 已清空。
- indexed getter 写值后返回失败仍生成 source；若根本未写值，默认 Void Variant 继续
  进入 object conversion 的原生边界。
- element object conversion 抛出时，本项尚未 push；此前项已提交，source 按 unwind
  释放。
- `HasValue` 负 HRESULT 不读取 bool；scratch 即使拥有 object/string 也先释放。
- bool/label getter 写值后负 HRESULT 不阻止转换；转换本身仍可抛出。
- vector push 失败时 HM3 尚未被查询/修改；之前项保留。
- HM3 lookup/materialization 失败时 vector 已多出 label，但 rawElement 尚未提交；无
  rollback。
- rawElement 赋值先 retain 新 object/objthis，再释放旧 owner；共享 dispatch 不出现瞬时
  零引用。

## portable probe

新增 `timeline builder retains root, element and probe owners through ncb reads` 覆盖：

- main/diff 入口旧 label 在 builder 开头被清掉；
- root Count 写出 1 后返回失败，仍恰好读一次；
- index 0 写出 element 后返回失败，仍继续构造 element accessor；
- index getter 可重入清除 caller 的 root Variant，root accessor 仍保活 dispatch；
- root 内部 element storage 在写出结果后清除，returned/source owner 仍保活 element；
- `HasValue(diff)` 返回成功并交出唯一 scratch owner；scratch storage 清除后仍存活，
  但在第二次 bool read 前已经析构且恰好析构一次；
- bool 与 label getter 都写值后返回失败，结果仍分别选择 diff vector 和形成 label；
- named read 顺序、flags、两个 diff hint 同一性、label hint 独立性、objthis 和 root
  存活状态精确匹配；
- loop 结束时 root 恰好析构一次，而 element 由 HM3 rawElement 继续保活；erase map
  entry 后 element 恰好析构一次，且此时 root 已不存活。

## IDB 落地

四个 recovery IDB 都追加 V142 function comment、逐指令 source/lifetime 注释和 builder
bookmark，随后 force-recompile/decompile：

| 参考 | function comment readback | disasm line comment readback |
|---|---:|---:|
| Android arm64 | 1 | 12 |
| Android armv7 | 1 | 11 |
| iOS arm64 | 1 | 11 |
| iOS armv7 | 1 | 11 |

四个 IDB 均在 readback 后原位保存。

## 验证

- 普通与 `KRKR2_WASMTIME_HEADLESS=1` 两套完整 test TU `-fsyntax-only` 通过；只有既有
  `_tss` literal-operator 弃用 warning。
- Web Debug 完整增量构建 `3/3`，包括最终 `index.html`/wasm 链接。
- Wasmtime Headless Debug 完整增量构建 `4/4`，包括 guest wasm 链接。
- `out/web/debug/index.wasm` 与
  `out/wasmtime/debug/krkr2_wasmtime_guest.wasm` 均由 Node
  `WebAssembly.Module` 成功解析。
- 定向实现范围：`motionPropGet*` 与 `motionTryPropGet` 均为 0；两个 accessor、一次
  Count、一次 indexed Variant read、一次 `HasValue`、一次 bool read、一次 string read
  均存在。
- 限定 `git diff --check` 通过；只有工作树既有 LF/CRLF 转换提示。

构建成功只证明 portable 代码闭合；owner/source identity、TJS 参数与 HRESULT 分叉来自
本轮四端 fresh decompile，并由可重入 probe 锁定。
