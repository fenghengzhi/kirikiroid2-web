# EmoteEngine direct owner 与构造失败回滚：四参考二进制复原

日期：2026-08-13

## 1. 结论

`EmoteEngine` 中连续的 Player 与七个 direct controller 字段不是旧本地注释所称的
“raw pointer + 手动 delete”。四份当前参考共同指向同一源码结构：八个字段都是单指针
宽度的 `std::unique_ptr<T>` owner，声明次序为：

1. Player；
2. position VarController；
3. scale VarController；
4. color VarController；
5. angle controller；
6. bust outer-force VarController；
7. hair outer-force VarController；
8. parts outer-force VarController。

这个结论不依赖 RTTI 或残留模板名，而来自三组可同时解释的机器级行为：正常析构的
owner-slot specialization、不同标准库的 slot 清零次序，以及构造失败时按已完成 member
前缀自动回滚。

本文绝对地址只作证据索引，不进入编译源码注释。

## 2. 四端映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Engine ctor | `0x67B76C` | `0x560948` | `0x1001B7FB0` | `0x1B7788` |
| Engine normal dtor | `0x67C898` | `0x5610E8` | `0x1001B8B4C` | `0x1B814E` |
| 独立 ctor-unwind body | ctor 尾部内联 | 未独立保留 | `0x1001B829C` | `0x1B7B02` |
| VarController owner-slot dtor | 内联 | `0x56351C` | 内联 | 内联 |
| Angle owner-slot dtor | 内联 | `0x563C44` | 内联 | 内联 |
| Player owner-slot dtor | 内联 | `0x563C5E` | 内联 | 内联 |

字段在各 ABI 中仍只占一个 pointer：

| 目标 | Player slot | controller slots |
|---|---:|---:|
| Android arm64 | `+1064` | `+1072..+1120` |
| Android armv7 | `+532` | `+536..+560` |
| iOS arm64 | `+696` | `+704..+752` |
| iOS armv7 | `+348` | `+352..+376` |

`unique_ptr` 不增加对象内 storage，因而这个纠正不改变此前闭合的 Engine ABI 布局。

## 3. 正常析构为何能识别 owning wrapper

Android armv7 的三个 helper 都接收 `T **ownerSlot`：

```text
owned = *slot
if owned:
    owned.~T()
    operator delete(owned)
*slot = null
```

同一个 VarController helper连续用于六个相同 specialization；Angle 和 Player 分别保留自己的
specialization。Android arm64 将它们内联，但顺序相同。

iOS 两端的内联形态不同：

```text
owned = *slot
*slot = null
if owned:
    owned.~T()
    operator delete(owned)
```

这不是产品语义差异，而是两套标准库实现 `unique_ptr` reset/destructor 的差异：Android
libstdc++ 生成 delete-then-null，iOS libc++ 生成 exchange-null-before-delete。字段宽度、逆
声明析构次序和 deleter 行为相同。普通 raw pointer member 的自动析构不会生成任何一组
owner-slot helper，也无法解释这种随标准库稳定变化的 slot 写回次序。

## 4. 构造失败的 prefix unwind

构造共同按 Player、六个 VarController 和 Angle controller的声明次序逐个执行：

```text
temporary = operator new(sizeof(T))
temporary.T(...)
ownerSlot = temporary
```

如果 `T` 的构造在写入 ownerSlot 前抛出，landing pad 先仅对 temporary 调用
`operator delete`；它不能调用尚未完整构造对象的析构。之后语言级 member unwind 会从最后
一个已完成 owner 开始逆序销毁，最终销毁 Player，再销毁更早声明的 containers，并 resume
原异常。

iOS arm64 的 cleanup body是多入口阶梯，而不是“先删一个 pending pointer、再总是从 parts
开始”的单一路径。后置 member 或四个 seed setter失败时，八个 owner都已完成，入口才从
parts→hair→bust→angle→color→scale→position→Player开始；某个 controller constructor失败
则先只 delete当前 pending allocation，随后直接从它前一个已完成 owner开始（例如 parts ctor
失败后从 hair开始）；该 controller的 allocation自身失败时没有 pending pointer可删，同样从
前一个 owner开始。iOS armv7 的 SjLj switch以独立 allocation/constructor state逐项编码同一
frontier。Android arm64 的 ctor 尾部包含同形 landing blocks；Android armv7 没有保留同形独立
Hex-Rays body，但其 normal-dtor owner-slot specializations和完全相同的字段/构造顺序支持同一
共同源码类型。另一个 ABI差异是 Angle：Android eager libstdc++ deque initialization可抛，存在
pending-delete landing；iOS lazy libc++ empty deque只生成零写，因此只有 allocation-failure
frontier。完整逐项证据由 V262补录。

完整 `EmoteEngine::~EmoteEngine()` 不会在 ctor 失败时运行。特别是 wind-first 的正常析构
规则不能套到 partial object；只析构已经完成构造的 member 前缀。

## 5. 本地偏差与修复

修复前八个字段都是裸 `T *`，构造函数逐个 `new` 后直接赋值。若后续分配、Player ctor 或
controller ctor 抛出，C++ 不会调用失败对象的 `EmoteEngine::~EmoteEngine()`，已写入字段的
Player/controller 会泄漏；也没有参考中的 owner-prefix unwind。

当前本地恢复为：

- 八个字段均为 `std::unique_ptr<T>`；
- 八个 `new T(...)` 都位于 declaration-order member initializer中；每个 new-expression完整
  成功后才构造/发布对应 `std::unique_ptr` member，后续 member failure自然回滚已完成前缀；
- 所有 controller helper 参数通过 `.get()` 形成非 owning borrow，不转移所有权；
- 正常析构仍在已闭合的准确阶段按 parts -> hair -> bust -> angle -> color -> scale ->
  position -> Player 调用 `reset()`；
- 构造异常则由 C++ member unwind 自动释放已完成的 owning prefix。

源码仍保留显式正常析构阶段，是因为更早的 deque element owning pointer 尚需逐 builder 复核
push/emplace 异常边界；这不影响本专题的八个 direct owner 已恢复为准确类型。

## 6. IDB 与验证

- 四份 recovery IDB 的 ctor/normal-dtor 已追加 direct-owner/unique_ptr 语义注释并保存；
- iOS 两端 ctor-unwind 已有语义名；Android arm64 cleanup 是 ctor 内部 block，不强拆伪函数；
- `cmake --build out/web/debug -j 8`：通过，motionplayer 静态库与最终 Web 产物重新链接；
- `git diff --check`：无 whitespace error，仅有工作树既存 LF/CRLF warning。
