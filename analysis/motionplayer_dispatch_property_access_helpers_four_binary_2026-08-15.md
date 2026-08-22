# MotionPlayer dispatch property-access helper 四端复原（2026-08-15）

## 结论

`MotionDispatch.h` 中的 `motionPropGet*` 并不是可以任意互相组合的便利函数。
四个 1.3.9 参考二进制共同保留了三组不同的源码级 helper：

1. 普通 named / numeric getter：忽略 `PropGet`/`PropGetByNum` 的 HRESULT，
   无条件使用调用后临时 `Variant` 的内容；
2. typed getter：直接在属性临时值上执行 real/integer/bool/string 转换，随后析构
   临时值，不经过一个“返回 Variant”helper；
3. strict probe：固定语义是先把 `TJS_MEMBERMUSTEXIST` 查询写入临时值，失败时
   丢弃临时值且不改调用者目标，成功时才构造中间副本并提交目标。

所有路径都把 holder 的同一个 dispatch 同时作为虚调用 receiver 和 `objthis`。
普通 named getter 转发 `flags/member/hint`；numeric getter 转发 `flags/index`；
`count` helper 则固定执行 `PropGet(0, L"count", nullptr, ..., dispatch)`，从未调用
`GetCount()`。

本专题还确认公共头中的六个 Android 旧地址不是四份当前参考的 helper 身份。
它们来自旧 `libkrkr2.so` 取证，不能继续作为当前源码注释：例如当前 Android ARM64
中的 `0x662668` 是 Eyebrow 构造异常清理内的 `operator delete` 调用，
`0x6635DC/0x6636D4` 位于 mouth restore，`0x6695BC` 位于 build-chain 的引用替换，
`0x56C694` 只是另一函数内部位置；旧 string 注释的 `0x529524` 也不是字符串 getter。

## 四端函数映射

### Named getter 与转换

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| real | `0x65FA48` | `0x4C779C` | `0x1000F1760` | `0xEDA64` |
| integer | `0x6609BC` | `0x496C5C` | `0x1000F17E4` | `0xEDB2C` |
| bool | `0x660AB4` | `0x552124` | `0x1000F3078` | `0xEF7F0` |
| returned `Variant` | Blink ctor 内联 `0x65FFA4` | `0x55218C` | `0x1000F1860` | `0xEDBF0` |
| returned `ttstr` | `0x529904` | `0x492100` | `0x1000F18DC` | `0xEDCB0` |
| integer `count` | `0x56CA74` | `0x4BEB84` | `0x1000F30F4` | `0xEF8B4` |

四端 named 虚槽一致：64 位对象 vtable `+0x20`，32 位对象 vtable `+0x10`。
参数顺序在 ABI 归一化后都是：

```text
dispatch->PropGet(flags, member, hint, &temporary, dispatch)
```

real、integer 与 bool helper 分别直接调用 `Variant::AsReal`、
`Variant::AsInteger` 与 `Variant::operator bool`。Android ARM64 将这些转换内联为
type-tag switch；其他目标更多保留独立 conversion call，但 Void/default、整数、实数
和字符串 coercion 的共同结果一致。调用者需要 float 时是在 helper 返回 double 后再
窄化，例如 simple-spring 构造的 gravity/spring/friction/scale 字段。

字符串 helper 先从属性临时 `Variant` 构造一个独立拥有引用的 `ttstr` 返回对象，
然后才析构属性临时值。因此它不是借用 `Variant` 内部字符串指针，返回字符串可安全
越过临时值生命周期。

### Numeric getter

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| returned `Variant` | buildTimeline 内联 `0x66CD3C` | `0x5334E0` | `0x1000691F8` | `0xED9A8` |
| real | `0x66699C` | `0x4C7734` | `0x1000F2FF8` | `0xEF66C` |
| integer | `0x660B9C` | `0x4C7970` | `0x100069180` | `0xEF730` |

numeric 虚槽为 64 位 vtable `+0x28`、32 位 vtable `+0x14`：

```text
dispatch->PropGetByNum(flags, index, &temporary, dispatch)
```

在 `EmoteEngine_buildTimelineControl_guess` 的 loop 中，四端还给出返回 Variant 的
精确所有权序列。Android ARM64 内联序列是 `PropGetByNum -> copy-construct source ->
destroy property temporary`；另外三端保留独立 helper，执行同一序列。这证明
`motionPropGetByNum` 不能依赖 named-return-value optimization 把属性临时值直接变成
调用者对象。

### Strict optional probe

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| integer destination | `0x6617FC` | `0x552AEC` | `0x1001A2F04` | `0x1A2180` |
| real/float destination | `0x661904` | `0x552B60` | `0x1001A2F90` | `0x1A2258` |
| `Variant` destination | `0x661A1C` | `0x552BDC` | `0x1001A3020` | `0x1A2338` |

三种 strict helper 都先执行：

```text
hr = dispatch->PropGet(
    TJS_MEMBERMUSTEXIST, member, hint, &probeTemporary, dispatch)
```

边界不是“把调用者 destination 直接传给 getter”：

- `hr < 0`：返回 false，getter 即使写过 `probeTemporary` 也会被丢弃，调用者
  destination 保持逐 bit/逐引用语义不变；
- `hr >= 0`：integer/real helper 转换后才写 destination；Variant helper 先
  copy-construct 一个中间 `Variant`，再 copy-assign destination；
- Variant 成功出口的正常析构顺序是中间副本、`probeTemporary`；失败出口只析构
  `probeTemporary`。

这组两阶段提交是本次发现的真实源码偏差。旧 Web 封装把 destination 直接传给
`PropGet`，失败 getter 若先写结果再返回 `TJS_E_MEMBERNOTFOUND`，会错误覆盖调用者旧值。

## 共同数据流与边界行为

### 普通 typed getter

共同伪代码为：

```cpp
T getTyped(holder, key_or_index, flags, hint) {
    Variant temporary;                    // starts as Void
    Dispatch *dispatch = strictUnwrap(holder);
    (void)dispatch->PropGet...(
        flags, key_or_index, hint_if_named,
        &temporary, dispatch);
    T result = convert<T>(temporary);
    destroy(temporary);
    return result;
}
```

HRESULT 被忽略的精确含义是“使用调用后的临时值”，而不是“失败总返回默认值”：

- getter 失败且未写 result：临时值仍为 Void，real/integer/bool 为零/false，string
  为 canonical empty；
- getter 先写 result 再返回失败：写入值仍然参与转换并成为返回值；
- conversion 自身的异常继续向外传播，临时值由相应 unwind cleanup 析构。

### 返回 Variant 的 getter

named 与 numeric 的保留序列都是：

```cpp
Variant temporary;
ignore_hr(property_read(&temporary));
Variant returned(temporary);  // observable AddRef for object/string owners
destroy(temporary);           // matching Release before helper returns
return returned;
```

因此公共源码现在显式构造 `result(value)`。返回对象可以被 ABI/NRVO 直接放进调用者
return slot，但从属性临时值到该返回对象的 copy construction 仍不可删除；对带自定义
dispatch 的 Object Variant，这一对 AddRef/Release 是可观察生命周期。

### Holder、receiver 与 hint

- holder 走严格 object conversion；非 Object Variant 在虚调用前抛 conversion error；
- helper 不增加一个长期 holder dispatch 引用，`AsObjectNoAddRef` 只借用；
- Object Variant 若携带 null dispatch，没有额外 graceful-null guard，随后虚调用解引用
  保留 malformed-input 崩溃边界；
- receiver 与 `objthis` 必须是同一 dispatch，不使用 null 或外部上下文；
- named helper 原样转发 hint 指针，脚本 getter 的重入可更新同一个进程级 hint 槽；
- `count` 是例外：固定 flags 0、null hint 与 literal `count`。

### 异常清理 ABI

共同源语义是所有已经构造的 `Variant`/`ttstr` 按逆序清理。平台编码不同：

- Android ARM64 的 cleanup 与主函数 landing path 合并；
- Android ARMv7 由 EHABI 表覆盖这些短 helper；
- iOS ARM64 在许多 helper 后紧邻 `0x14` 字节 cold cleanup/resume sibling，例如
  `0x1000F17D0`、`0x1000F184C`、`0x1000F18C8`；
- iOS ARMv7 使用 SjLj cleanup sibling，例如 `0xEDB00`、`0xEDBC4`、`0xEDC86`、
  `0xEF704`、`0xEF7C4`。

这些 ABI 差异不改变成功/失败提交顺序，但说明不能用“函数短小”推断转换或
copy-assignment 抛出时不会清理。

## 源码修复

`cpp/plugins/motionplayer/MotionDispatch.h` 已完成：

1. 删除最后一段把六个旧 Android 地址当作当前 helper 身份的注释，改为无地址的
   四端共同语义；
2. `motionTryPropGet` 改为 `probe temporary -> HRESULT test -> intermediate copy ->
   destination assignment`，失败不再污染 destination；
3. `motionPropGet` 与 `motionPropGetByNum` 显式从属性临时值 copy-construct 返回对象；
4. real/int/bool/string/count 以及 numeric real/int helper 改为各自直接调用 dispatch，
   避免通过 Variant-return helper 引入额外的引用计数副作用；
5. string helper 在销毁属性临时值前构造独立 `ttstr` 返回 owner。

`tests/unit-tests/plugins/motionplayer-dll.cpp` 新增 probe 回归测试：失败 dispatch
故意写入 `failed-probe-write` 再返回 `TJS_E_MEMBERNOTFOUND`，验证调用者原值仍为
`retained`；随后成功读取才提交 `committed`。测试同时核对 flags、member、hint 和
`objthis == receiver`。

## Recovery IDB 写回

四份 recovery IDB 已统一补入 `_guess` 语义名：

- `VariantObject_get{Real,Int,Bool,Variant,String}ByName_guess`；
- `VariantObject_get{Real,Int,Variant}ByIndex_guess`；
- `VariantObject_tryGet{Real,Int,Variant}ByName_guess`。

2026-08-16 NodeTree follow-up：原列表中的 `VariantObject_*` 是早期按行为命名，并非已证实
的源码类名。NodeTree fresh 四端 vptr/receiver 证据现已把该路径实际调用到的 helper 纠正为
`ncbPropAccessor_GetArrayCount_guess`、`ncbPropAccessor_GetValueNamed{String,Bool,Real}_guess`、
`ncbPropAccessor_GetValueArray{Variant,Integer}_guess` 及
`ncbPropAccessor_getIntValueArray_NodeTree_guess`。其他路径只有在各自四端纵切面闭合后才应
迁移，不能从 NodeTree 证据无条件外推。详见
`analysis/motionplayer_node_tree_ncb_accessor_source_identity_four_binary_2026-08-16.md`。

2026-08-16 Emote controller follow-up：Blink、Eyebrow、Mouth 三条构造路径现也已由 fresh
四端 copy/vptr/dtor、nested owner 与 hint-family xref 闭合，并迁移为真实
`ncbPropAccessor`。其中 indexed integer emitted clone 与 NodeTree 相同，故保守命名调整为
`ncbPropAccessor_GetValueArrayInteger_NodeTreeEmote_guess`；其他地址上的同模板 clone 仍不从
这两个 family 外推。详见
`analysis/motionplayer_emote_controller_metadata_ncb_accessor_source_identity_four_binary_2026-08-16.md`。

2026-08-16 spring follow-up：Simple/BustChain 两个参数 constructor 也已由 fresh 四端
copy/vptr/dtor、hint xref 与 nested owner 纵切面迁移为真实 `ncbPropAccessor`。BustChain 的
`length/scale_x/scale_y` 是三个顺序 accessor，各自读完两个 indexed real 即 Release；
named/indexed getter 均忽略 HRESULT。该 family 的
`ncbPropAccessor_GetValueArrayReal_guess` 只在本纵切面确认，仍不外推其他 raw helper caller。
详见
`analysis/motionplayer_spring_metadata_ncb_accessor_source_identity_four_binary_2026-08-16.md`。

Android ARM64 的两个内联 Variant 路径保留在 caller 内，以行注释和书签标出；
其余 standalone helper 均补了 receiver/objthis、HRESULT、转换、copy/commit 与临时
析构语义。每个平台还增加 named、string、numeric、strict、count 和 numeric-Variant
入口书签。

## 验证

完成以下验证：

1. `cmake --build --preset "Web Debug Build"`：成功，34 个增量步骤完成；只有仓库
   既有 `_tss`、imagepacker `nodiscard`、Emscripten pthread/memory-growth、JSPI 与
   JS library 警告；
2. 使用 `out/syntax-check/motionplayer_test_args.rsp` 对完整
   `tests/unit-tests/plugins/motionplayer-dll.cpp` 执行 `em++` 语法检查：成功，只有既有
   `_tss` warning；新增 strict-probe 回归测试已进入同一完整 TU；
3. `MotionDispatch.h` 的五位以上十六进制地址与 `libkrkr2` 定向扫描：零命中；
4. 对本专题源码、测试、专题文档、迁移台账和 `plan.md` 执行
   `git diff --check`：成功，仅报告工作区既有 LF/CRLF 转换提示；
5. Android ARM64、Android ARMv7、iOS ARM64、iOS ARMv7 四份 recovery IDB 均
   `idb_save ok=true`。

Web preset 不提供 motionplayer Catch2 可执行 target，因此这里按仓库现行门槛采用完整
测试 TU 语法检查；没有把“测试源已编译”误记为运行时 Catch2 已执行。
