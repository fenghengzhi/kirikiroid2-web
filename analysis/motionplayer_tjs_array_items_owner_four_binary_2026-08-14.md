# Motionplayer TJS Array/Items owner helper 四参考恢复

日期：2026-08-14

本记录只使用 `reference/binaries/` 四个当前目标的新反编译、反汇编和交叉引用。它替换
`RuntimeSupport` 中仍指向旧 `libkrkr2.so` 单目标地址的说明。

## 四端映射与共享范围

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `createTJSArrayWithItems_guess` | `0x702098` | `0x5BAA70` | `0x10029FF58` | `0x2A4A80` |
| `TJSCreateArrayObject` | `0x985FD4` | `0x71242C` | `0x100154778` | `0x1564A4` |
| `TJSGetArrayClassID` | `0x985FC8` | `0x712420` | `0x10015476C` | `0x156496` |

四个 helper 都恰好有 71 个直接 code xref。调用者覆盖 Engine metadata/status Array、Player
query/getter、ResourceManager、Bezier/mesh Layer 扩展和逆三角形结果，不是某个类的私有
工具。因此本地继续把它放在共享 `RuntimeSupport`。

## 返回对象 ABI

恢复源码的逻辑类型为：

```cpp
struct TJSArrayWithItems_guess {
    tTJSVariant value;
    std::deque<tTJSVariant> *items;
};
```

| ABI | `sizeof(tTJSVariant)`/后续对齐 | `items` 字段 | `tTJSArrayNI::Items` |
|---|---:|---:|---:|
| AArch64 Android/iOS | Variant payload 后补到 24 字节 | return object `+24` | native instance `+16` |
| AArch32 Android/iOS | 12 字节 | return object `+12` | native instance `+8` |

64 位 `tTJSArrayNI` 在 `Items` 前有两个 8 字节 polymorphic base/vptr 区域；32 位对应两个
4 字节区域。这与当前 `tTJSArrayNI : tTJSNativeInstance,
tTJSSaveStructuredDataCallback` 和紧随其后的 public `std::deque<tTJSVariant> Items` 一致。

`items` 只是内部 deque 的借用指针，不执行 AddRef，也不拥有 native instance。它的有效期完全
由同一返回 struct 中的 `value` 保持。

## 引用计数与构造顺序

四端共同伪代码：

```cpp
iTJSDispatch2 *dispatch = TJSCreateArrayObject(nullptr);
tTJSVariant temporary(dispatch, dispatch);
dispatch->Release();

tTJSArrayNI *native; // 不预清零
tjs_error status = dispatch->NativeInstanceSupport(
    TJS_NIS_GETINSTANCE, TJSGetArrayClassID(),
    reinterpret_cast<iTJSNativeInstance **>(&native));

out.value = temporary;
out.items = status == TJS_S_OK ? &native->Items : nullptr;
```

closure 的 `Object` 与 `ObjThis` 都是同一 Array dispatch，因此临时 Variant 对 dispatch 执行
两次 AddRef。紧接着释放 factory 返回的初始引用。把临时 Variant CopyRef 到返回对象后，临时
对象析构再释放自己的两条引用，最终由 `out.value` 的两个 closure slot 保持 Array/native
instance 存活。

这里不是 `tTJSVariant(dispatch)`：单参数构造会令 `ObjThis == nullptr`，与四端两个相同指针
和两次 AddRef 不符。

## native-instance 查询与失败边界

- `TJS_NIS_GETINSTANCE` 的数值为 2；class ID 来自 `TJSGetArrayClassID()`，参考 helper 没有把
  常量 class ID 硬编码进源码；
- 判断是 `status == 0`，不是一般的 `TJS_SUCCEEDED(status)`。只有精确
  `TJS_S_OK` 才发布 `native+16/+8`；任何非零状态都发布 null；
- native 输出 slot 没有预先清零，但非零状态路径用 conditional select/branch 直接输出 null，
  不读取失败后的 slot；
- 即便查询失败，返回的 `value` 仍是有效且拥有 Array 的 Variant；只有 `items` 为 null；
- helper 自身不检查 `TJSCreateArrayObject` 的 null 返回。虽然存在围绕两次 AddRef 的 null
  分支，后面仍无条件对 dispatch 调 Release/NativeInstanceSupport；因此不能在移植中增加
  “null 时返回空 struct”的新恢复路径；
- 当前 71 个原生调用点依赖 fresh built-in Array 必定返回 class ID 对应 native instance，通常
  直接解引用 `items`。本地调用者也没有 null fallback，保持同一 invariant。

## 异常清理编码

源级临时 Variant 的清理在四端相同，但编译器编码不同：

- Android arm64 有显式 landing pad，异常时析构临时 Variant 后 `_Unwind_Resume`；
- iOS armv7 通过 SJLJ call-site switch 清临时 Variant后继续 unwind；
- Android armv7 helper 没有局部 C++ landing pad；
- iOS arm64 只有 compact unwind/普通 epilogue，本函数体没有单独的 destructor landing pad。

这属于目标工具链差异，不能据此把 Android arm64/iOS armv7 的清理写成额外业务分支。共同
源码仍是自动临时 `tTJSVariant` 和返回对象 CopyRef。

## 与本地实现的对照

本地实现已经满足共同语义：

- 使用 `TJSCreateArrayObject()`；
- 以 `(dispatch, dispatch)` 构造 owning closure；
- 立即释放 factory reference；
- native pointer 不预清；
- 使用 `TJS_NIS_GETINSTANCE` 和 `TJSGetArrayClassID()`；
- 只在 `status == TJS_S_OK` 时返回 `&native->Items`，否则 null；
- 返回 struct 的 Variant 拥有对象，Items pointer 只是借用。

本纵切面因此不改执行代码，只删除旧 `sub_704CB8 @ 0x704CB8` 注释，并把双 closure ref、精确
zero-status gate、ABI offset、借用生命周期和无 graceful-null 边界写回源码。

四份 recovery IDB 还补齐了 `TJSCreateArrayObject`/`TJSGetArrayClassID` 名称和上述生命周期注释，
随后全部保存。

## 验证

- 完整 motionplayer 翻译单元语法检查；
- Web Debug 完整构建与最终链接；
- `git diff --check`；
- 上述验证与紧邻的 `BezierPatch` 纵切面共用同一轮最终检查；若继续发生源码改动，则在本纵切面
  结束时重新执行。
