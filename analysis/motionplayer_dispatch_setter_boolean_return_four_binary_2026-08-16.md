# dispatch setter 的 exact-zero Boolean 返回 ABI：四参考二进制恢复记录

日期：2026-08-16

本纵切面复审三个被 force-visible 几何镜像调用、同时也被插件其他路径共享的 TJS dispatch
setter wrapper。旧本地 helper 已经恢复了 Variant 类型、虚调用参数和写入顺序，但错误地声明成
`void`；四份当前参考二进制都明确 materialize 一个 Boolean 返回值，而且判断条件不是
`TJS_SUCCEEDED(error)`，而是 `error == TJS_S_OK`。

## 1. 四端函数映射

| wrapper 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| unsigned-byte → named Integer property | `0x5A2540` | `0x4E2568` | `0x100102BD0` | `0xFFFF8` |
| named Real property | `0x671290` | `0x55B0E4` | `0x100113810` | `0x1111E8` |
| numeric-index Real property | `0x6BE0E8` | `0x58A39C` | `0x100113758` | `0x1110F0` |

recovery IDB 最初使用的语义名在后续源码身份纵切面中进一步收紧为：

- `ncbPropAccessor_SetValueNamedIntegerByte_guess`
- `ncbPropAccessor_SetValueNamedReal_guess`
- `ncbPropAccessor_SetValueArrayReal_guess`

名称保留 `_guess`：剥离二进制没有提供原始 C++ 标识符。三个实现表现为可跨翻译单元合并的
共享 wrapper，而不只是 force-visible 块的局部函数。

## 2. Variant 构造与虚调用 ABI

四端共同数据流是：

```cpp
bool setIntegerByte(accessor, name, const unsigned char *value,
                    unsigned flags, hint) {
    tTJSVariant temporary(static_cast<tjs_int>(*value));
    const tjs_error error = accessor.dispatch->PropSet(
        flags, name, hint, &temporary, accessor.dispatch);
    temporary.~tTJSVariant();
    return error == TJS_S_OK;
}

bool setReal(accessor, name, const double *value,
             unsigned flags, hint) {
    tTJSVariant temporary(*value);
    const tjs_error error = accessor.dispatch->PropSet(
        flags, name, hint, &temporary, accessor.dispatch);
    temporary.~tTJSVariant();
    return error == TJS_S_OK;
}

bool setArrayRealAt(accessor, unsigned index, const double *value,
                    unsigned flags) {
    tTJSVariant temporary(*value);
    const tjs_error error = accessor.dispatch->PropSetByNum(
        flags, index, &temporary, accessor.dispatch);
    temporary.~tTJSVariant();
    return error == TJS_S_OK;
}
```

这里的 `accessor` 在本纵切面完成时只表达反编译观察到的“小包装对象内取 dispatch 指针”结构。
紧随其后的四端源码身份复审已经用 vptr+dispatch 布局、`GetValue<tTJSVariant>` 和三份
`SetValue<T>` 函数体把它闭合为仓库现存的 `ncbPropAccessor`；详见
`analysis/motionplayer_force_visible_ncb_prop_accessor_source_identity_four_binary_2026-08-16.md`。

named setter 使用 vtable `+48`（32 位为 `+24`）的 `PropSet`；numeric setter 使用
vtable `+56`（32 位为 `+28`）的 `PropSetByNum`。三个 wrapper 都把目标 dispatch 自身传作
`objthis`。byte wrapper 以无符号字节读取输入后构造 `tvtInteger`；另两个构造 `tvtReal`。
异常不在 wrapper 内捕获，Variant 临时对象由正常或展开清理路径销毁。

## 3. exact-zero 的指令级证据

| wrapper | Android arm64 compare | Android armv7 compare | iOS arm64 compare | iOS armv7 compare |
|---|---:|---:|---:|---:|
| Integer byte | `0x5A25C0` | `0x4E25C2` | `0x100102C40` | `0x100092` |
| named Real | `0x671314` | `0x55B148` | `0x100113888` | `0x11128A` |
| array Real | `0x6BE164` | `0x58A3F8` | `0x1001137C4` | `0x11118C` |

AArch64 三个实现都是 `CMP W0,#0` 后 `CSET W...,EQ`；ARMv7 三个实现都是先把结果寄存器置
0，再 `CMP saved_error,#0`，最后以 `MOVEQ` 置 1。因而：

- `TJS_S_OK == 0` → `true`；
- `TJS_S_TRUE == 1` 和 `TJS_S_FALSE == 2` → `false`；
- 负错误码（例如 `TJS_E_FAIL == -1`）→ `false`。

这排除了 `TJS_SUCCEEDED(error)`。后者会把正状态当作成功，与四端条件码不一致。Android arm64
IDB 中 Integer/Real 两个函数的旧返回类型仍是 `__int64`；本轮已依据 W 寄存器的 0/1
materialization 改为 `bool`，其余十个函数原型此前已是 `bool`。

## 4. 调用者审计与 force-visible 数据流

完整 code-xref 计数如下；平台间数量差异来自链接/合并结果，不改变 wrapper 本体：

| wrapper | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Integer byte | 11 | 21 | 21 | 23 |
| named Real | 43 | 43 | 43 | 43 |
| array Real | 13 | 13 | 13 | 13 |

xref 调用者覆盖 TextRender、ResourceManager source 查询、Player modified/vertex/bounds/calcView、
Bezier bounds、timeline/variable/Quad/ObjSource 序列化和 command-list 等路径。逐一检查可见 caller
context 后，调用都以 statement 形式出现：调用后立即准备下一次属性操作、释放临时 owner 或进入
下一计算块，没有分支、比较或保存返回 Boolean 的代码。本结论只说明当前四个链接产物中的可见
调用者都忽略结果；wrapper 自身仍必须保持 Boolean ABI，因为它是共享且可被其他链接上下文复用的
实现边界。

force-visible 顶点块的第一次数值写调用点分别是：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|
| `0x6BA860` | `0x587264` | `0x100110340` | `0x10D7B0` |

四端都连续调用 6 次 array setter（`coord[0..1]`、`mtx[0..3]`）、8 次 named Real setter 和
2 次 Integer-byte setter。每次返回值都被丢弃；非抛异常的失败不会提前退出，后续写入继续进行。
这与异常边界并不冲突：脚本 callback 若直接抛异常，展开立即开始，后续写入不执行，也不回滚已经
完成的增量写入。

## 5. 本地恢复

本纵切面先把三个 force-visible setter helper 恢复为返回 `bool`，直接使用：

```cpp
return dispatch->PropSet...(args...) == TJS_S_OK;
```

这里故意不用 `TJS_SUCCEEDED`。后续源码身份纵切面已删除这三个手写复制 helper，改为直接
调用 `ncbPropAccessor::SetValue<T>`；ncbind 模板本身就是相同的 exact-zero 表达式。
`mirrorForceVisibleGeometry_guess` 的 16 个调用仍全部以显式 `(void)` 丢弃返回值，保留四端的
无 early-out、有序增量写入。

`motionplayer-dll.cpp` 新增记录型 dispatch 回归，分别覆盖：

- `TJS_S_OK` 为 true，`TJS_S_TRUE` 和 `TJS_E_FAIL` 都为 false；
- `TJS_MEMBERENSURE` flags；
- numeric index、named member、hint 指针和 `objthis == dispatch`；
- array/named Real 的 `tvtReal`；
- byte Boolean 输入写成值为 0/1 的 `tvtInteger`。

## 6. IDB 与验证

四份 recovery IDB 已在三个 compare 点加入 exact-zero 说明，在 force-visible 第一个调用点加入
“忽略 Boolean、继续增量写入”说明，并加入同名书签
`dispatch setter exact-zero bool return (2026-08-16)`。四库均已原位保存。

验证结果：

- 普通 Web 与 `KRKR2_WASMTIME_HEADLESS=1` 两种 motionplayer 完整测试翻译单元 syntax-only
  通过，仅有仓库既有 `_tss` 弃用警告；
- Web Debug 与 Wasmtime Headless Debug 增量构建通过；
- 定向 `git diff --check` 无新增内容级 whitespace error，仅有仓库换行转换提示。

## 7. 保留边界

三个共享 wrapper 的 accessor/template 归属已由紧随其后的四端纵切面闭合为
`ncbPropAccessor::SetValue<T>`，force-visible 的 raw-dispatch 复制 helper 也已删除。当前保留的
边界只是不把所有跨插件调用点一次性机械重写；它们的当前产物 caller context 已证明忽略 bool，
但各业务路径的完整对象生命周期仍由各自纵切面负责。
