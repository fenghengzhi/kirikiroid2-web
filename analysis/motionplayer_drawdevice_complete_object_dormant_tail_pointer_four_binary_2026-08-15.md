# MotionPlayer DrawDevice complete-object dormant tail pointer（四参考）

日期：2026-08-15

## 结论

`DrawDeviceD3D` 与 `D3D` 两个 concrete root 在 `tTVPDrawDevice` 次基类之后共享同一段
派生尾部：primary size、screen left/top、一枚 pointer-sized dormant 成员、`UpdateState`。
四份当前参考都显式把该 pointer 构造为 null；完整插件代码范围内没有构造后的读取、写入
或析构清理。它不是 padding，也不是 `UpdateState` 的一部分，但 stripped binary 无法恢复
原始指针类型和字段拼写，源码必须保留 `void *TailPointer_guess`。

## 完整对象布局与分配尺寸

| 项目 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| primary base 大小 | `0x178` | `0xD4` | `0x118` | `0xA4` |
| `tTVPDrawDevice` 次基类大小 | `0x68` | `0x50` | `0x68` | `0x50` |
| `PrimaryWidth` | `+0x1E0` | `+0x124` | `+0x180` | `+0xF4` |
| `PrimaryHeight` | `+0x1E4` | `+0x128` | `+0x184` | `+0xF8` |
| `ScreenLeft` | `+0x1E8` | `+0x12C` | `+0x188` | `+0xFC` |
| `ScreenTop` | `+0x1EC` | `+0x130` | `+0x18C` | `+0x100` |
| dormant pointer | `+0x1F0` | `+0x134` | `+0x190` | `+0x104` |
| `UpdateState` | `+0x1F8` | `+0x138` | `+0x198` | `+0x108` |
| `operator new` 大小 | `0x200` | `0x13C` | `0x1A0` | `0x10C` |

64 位对象在 `UpdateState` 后有 4 字节 complete-object tail padding；32 位对象恰好在
`UpdateState` 后结束。反过来，dormant 槽在 32/64 位都按指针宽度缩放，且位于两个可读写
int32 字段之间，不能解释成自然对齐空洞。

## 两个 concrete 类型共用同一构造

A32 `0x4955C4`、I64 `0x100233C10`、I32 `0x23287C` 是由 `DrawDeviceD3D` 与 `D3D`
两个工厂共同调用的完整构造函数；各自均有两个 code xref。A64 把完整构造尾部直接内联进
两个 raw 工厂 `0x52B654` 与 `0x52CC54`。两种 concrete vtable 地址不同，但字段初始化
完全一致，所以本地把尾部放在共同的 `DrawDeviceObjectBase`，而不是复制到两个 final class。

四端构造相关写入为：

- A64：两个工厂都先调用 primary-base ctor、构造 `this+0x178` 的次基类，再写
  width/height；`0x52B7A4` 或 `0x52CDA4` 清 ScreenLeft/Top 和整个 dormant pointer，
  `0x52B7A0` 或 `0x52CDA0` 单独清 UpdateState。
- A32：`0x4955E2` 写 width/height，`0x4955E8` 清 ScreenLeft/Top，`0x4955EC`
  用一条双寄存器 store 同时清 dormant pointer 与 UpdateState。
- I64：`0x100233C50/0x100233C54` 写 width/height，`0x100233C5C` 的成对零
  store 清 ScreenLeft/Top 与 dormant pointer，`0x100233C58` 单独清 UpdateState。
- I32：`0x2328FE/0x232906` 写 width/height，`0x23290E` 的两个 64 位零 store
  分别清 ScreenLeft/Top，以及 dormant pointer/UpdateState。

## 与 UpdateState 的负证据对照

逐指令扫描不是仅凭“反编译没显示”下结论。相邻 `UpdateState` 在每端都有清晰的完整链：

| 行为 | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| public `update(state)` 写入 | `0x52B98C` | `0x492D60` | `0x100230E30` | `0x22FCCC` |
| `Show` 读取 | `0x5318C0` | `0x495990` | `0x1002342BC` | `0x232F4C` |
| `Show` 清零 | `0x531934` | `0x49599E` | `0x1002342C4` | `0x232F5E` |

同一 complete-object displacement 扫描对 dormant pointer 只命中构造零 store；没有对应
load、非零 store 或 address publication。四端 complete destructor
`0x531410/0x495744/0x100233F54/0x232C74` 只依序调用 `tTVPDrawDevice` 与 primary-base
析构，也不触碰派生尾部。

这证明两槽不能合并：`UpdateState` 是一帧消费的脚本状态，dormant pointer 则在这四份构建
中终身保持构造 null。没有消费者能证明它是 texture、dispatch、window 或任一拥有型资源；
为它增加 Release/delete，或把它删除成 padding，都会超出参考行为。

## 恢复结果

源码继续保留显式 `void *TailPointer_guess = nullptr;`，并把注释收紧为“构造后无读写或
清理”。没有公开可观察行为可为该 dormant 槽增加单元测试；完整对象大小、构造 store 集、
相邻 UpdateState 的正向消费者链和四端析构负证据共同承担回归约束。
