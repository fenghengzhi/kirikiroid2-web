# force-visible 的 `ncbPropAccessor` 源码结构与临时链：四参考二进制恢复记录

日期：2026-08-16

本纵切面在 setter 返回 ABI 复审之后继续向上恢复源码结构。四份参考并不是在 force-visible
块里维护一套私有 dispatch owner 与三个手写 setter；栈对象布局、虚表、Variant 转换、getter
返回临时和三个共享 setter 的函数体都与仓库现存 `ncbind.hpp::ncbPropAccessor` 一致。此前端口的
`RetainedVariantDispatch_guess` + `setForceGeometry*` 虽模拟了大部分运行时效果，却丢失了原始
类身份和模板调用链。

## 1. 栈对象身份

force-visible 块同时存活三个同构对象：base `emoteEdit`、`coord` 与 `mtx`。每个对象都是：

```text
64 位：+0 vptr，+8 iTJSDispatch2*      sizeof == 16
32 位：+0 vptr，+4 iTJSDispatch2*      sizeof == 8
```

对应虚表槽地址为：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|
| `0x19FA8E8` | `0x10AAE90` | `0x101ADF7D0` | `0x1830F2C` |

四份 recovery IDB 现统一命名为 `ncbPropAccessor_vtable_guess`。这与仓库
`cpp/core/plugin/ncbind.hpp` 中唯一匹配的类型逐项相同：`ncbPropAccessor` 有 virtual destructor，
唯一数据成员是 `_obj`；Variant constructor 调用 `var.AsObject()` 获得独立引用，destructor 对
非空 `_obj` 调用 `Release()`。

参考产物没有保留 `ncbPropAccessor` RTTI 字符串，所以虚表数据名仍保留 `_guess`；类身份来自
完整结构和模板函数体的交叉证明，不假装是未剥离符号。

## 2. base accessor 的临时 Variant

| 阶段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| force gate | `0x6BA768` | `0x587208` | `0x1001102C0` | `0x10D748` |
| copy node Variant / construct base | `0x6BA76C..0x6BA7C0` | `0x587212..0x587224` | `0x1001102CC..0x1001102E8` | `0x10D754..0x10D76A` |

四端共同顺序是：

1. copy-construct 一个 `tTJSVariant` 临时；
2. 把 `ncbPropAccessor` vptr 写入 base 栈槽；
3. 对临时调用 `AsObject()`，把返回的独立 dispatch 引用写入 accessor `_obj`；
4. 销毁 Variant 临时。

源码等价形状为：

```cpp
ncbPropAccessor object{tTJSVariant(emoteEditVariant)};
```

因此没有额外的安全 type/null gate。非 Object 仍在 `AsObject()` 转换处抛异常；临时复制只负责
在 accessor 取得自己的引用之前维持 owner。

## 3. `GetValue<tTJSVariant>` 与 nested accessor

named Variant getter 的 out-of-line 模板实例映射为：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|
| force 块内联 | `0x55218C` | `0x1000F1860` | `0xEDBF0` |

三份独立实例现命名为 `ncbPropAccessor_GetValueNamedVariant_guess`。Android arm64 在本调用点
完整内联同一模板：`0x6BA7C4..0x6BA800` 直接构造 Void Variant、调用 `PropGet`、copy-construct
返回 Variant，再销毁内部临时。

模板实例逐项对应 `ncbind.hpp`：

```cpp
template <typename TargetT>
TargetT ncbPropAccessor::GetValue(
    KeyT key, Tag<TargetT> const &tag, FlagsT flags, HintT hint) {
    VariantT value;
    _obj->PropGet(flags, key, hint, &value, _obj);
    return _toTarget(value, tag);
}
```

force-visible 以 `TargetT=tTJSVariant`、`flags=0`、`hint=nullptr` 调用；这个特化的 `_toTarget`
按值返回 Variant。返回码被忽略，failed nonthrowing read 因此返回仍为 Void 的 Variant。

`coord` 的 getter/临时→accessor 链分别位于：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|
| `0x6BA7C4..0x6BA84C` | `0x587242..0x587254` | `0x100110308..0x10011032C` | `0x10D788..0x10D79E` |

`mtx` 紧随两个 coord setter 重复同一链。源码等价形状是：

```cpp
ncbPropAccessor coord{object.GetValue(
    L"coord", ncbTypedefs::Tag<tTJSVariant>(), 0, nullptr)};
ncbPropAccessor matrix{object.GetValue(
    L"mtx", ncbTypedefs::Tag<tTJSVariant>(), 0, nullptr)};
```

nested accessor 在返回 Variant 临时销毁之前执行 `AsObject()` 并取得自己的引用。这样 callback
后续删除/替换 dictionary member 时，当前 coord/mtx dispatch 仍活到 accessor destructor。

## 4. 三个 `SetValue<T>` 实例

setter 映射和新 recovery 名为：

| 模板语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `SetValue` named Integer-byte | `0x5A2540` | `0x4E2568` | `0x100102BD0` | `0xFFFF8` |
| `SetValue` named Real | `0x671290` | `0x55B0E4` | `0x100113810` | `0x1111E8` |
| `SetValue` numeric Real | `0x6BE0E8` | `0x58A39C` | `0x100113758` | `0x1110F0` |

分别统一命名为：

- `ncbPropAccessor_SetValueNamedIntegerByte_guess`
- `ncbPropAccessor_SetValueNamedReal_guess`
- `ncbPropAccessor_SetValueArrayReal_guess`

三者都从 accessor 的第二个 pointer-sized 槽取 `_obj`，用 `_toVariant` 构造临时 Variant，把
同一 `_obj` 传作 `objthis`，并以 `PropSet/PropSetByNum(...) == TJS_S_OK` 返回 bool。这个函数体
不是仅仅“像”一个普通 wrapper：它逐项匹配仓库 `ncbind.hpp:740..763` 的四个模板 overload。

force-visible 因此直接使用：

```cpp
(void)coord.SetValue(index, real, TJS_MEMBERENSURE);
(void)object.SetValue(name, realOrBool, TJS_MEMBERENSURE, hint);
```

索引 0 在 C++ 中显式转为 `tjs_int32`，避免整数零同时匹配 pointer overload；这只解决当前
ncbind 头的重载歧义，不改变生成的 numeric-index ABI。

## 5. 生命周期、失败和逆序清理

三个 accessor 按 base、coord、matrix 的构造顺序存活，正常尾部严格按 matrix、coord、base
逆序析构。四端的 landing-pad/SjLj 清理状态也以“已成功构造到第几个 accessor”为界：

- base 构造失败：只销毁尚存 Variant 临时；
- coord getter/转换失败：销毁 base；
- matrix getter/转换失败：销毁 coord、base；
- 任一 setter 抛异常：销毁已经构造的 matrix、coord、base；
- setter 仅返回非零状态而不抛异常：返回 bool 被 caller 丢弃，继续后续增量写入。

`GetValue` 忽略 PropGet 返回码，所以 missing `coord`/`mtx` 不是静默 skip，而是在随后 Void
Variant → accessor Object 转换时走普通异常。已经完成的写入不回滚。

## 6. 本地恢复与验证

`PlayerUpdateLayersInternal.h` 已删除 force-visible 专用的三个 setter wrapper，以及只被这一路
使用的 `RetainedVariantDispatch_guess::fromPropertyResult`。镜像路径现在直接构造三个
`ncbPropAccessor`、调用 `GetValue<tTJSVariant>` 和 `SetValue<T>`。ground-correction 仍使用其
独立审计过的 retained-dispatch helper，没有被本轮不相关地改写。

`motionplayer-dll.cpp` 的 exact-zero 测试改为直接覆盖 `ncbPropAccessor::SetValue`，并增加
`sizeof(accessor) == 2*sizeof(void*)` 的布局断言；既有镜像测试继续覆盖 coord/mtx 原地更新、
Variant 类型、缺成员异常和反向 owner 生命周期。

四份 recovery IDB 已写入 accessor 虚表/模板实例命名、base 和 nested 临时链注释以及
`force-visible ncbPropAccessor source identity (2026-08-16)` 书签，并全部原位保存。

验证结果：

- 普通 Web 与 `KRKR2_WASMTIME_HEADLESS=1` 两种完整 motionplayer 测试翻译单元
  syntax-only 通过，仅有仓库既有 `_tss` 弃用警告；
- Web Debug 与 Wasmtime Headless Debug 增量构建通过；
- 定向 `git diff --check` 无新增内容级 whitespace error，仅有换行转换提示。
