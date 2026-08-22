# MotionPlayer DrawDevice dormant transition tail 与 CurrentTarget 初态（四参考）

日期：2026-08-15

本纵切面只采用 `reference/binaries/` 的 Android arm64、Android armv7、iOS arm64、
iOS armv7 四份当前参考。它专门闭合 transition variant、rule 后两枚未知指针，以及
`CurrentTarget` 构造初态；旧 `libkrkr2.so` 注释不作证据。

## 结论

四份二进制共同给出三个容易被“安全初始化”掩盖的源码边界：

1. transition 中间的成员确实是 `tTJSVariant`，但默认构造只写 `tvtVoid`
   discriminator，payload 保持未初始化；插件内唯一确认的后续消费是根析构。
2. rule 后的两个 pointer-sized 成员都构造为 null，但在完整插件代码范围内没有后续
   读取、写入或析构清理。只能保守命名为 `TransitionPointer0/1_guess`。
3. `FrontTarget` 与 `BackTarget` 构造为 null，紧随其后的 `CurrentTarget` 却没有任何构造
   写入。它在首次 `capture`/`Show` 发布前是 indeterminate；正常绘制链在 child `Draw`
   前覆盖它，正常尾部清 null，异常则保留最后发布值。

这三点都是对象字节级状态，而不只是反编译显示风格。恢复源码不得把 variant payload
整体清零，也不得给 `CurrentTarget` 补默认 null 初始化。

## 四端布局

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| transition variant 起点 | `+0x118` | `+0x9C` | `+0xB8` | `+0x6C` |
| variant discriminator | `+0x128` | `+0xA4` | `+0xC8` | `+0x74` |
| rule texture | `+0x130` | `+0xA8` | `+0xD0` | `+0x78` |
| unknown pointer 0 | `+0x138` | `+0xAC` | `+0xD8` | `+0x7C` |
| unknown pointer 1 | `+0x140` | `+0xB0` | `+0xE0` | `+0x80` |
| FrontTarget | `+0x148` | `+0xB4` | `+0xE8` | `+0x84` |
| BackTarget | `+0x150` | `+0xB8` | `+0xF0` | `+0x88` |
| CurrentTarget | `+0x158` | `+0xBC` | `+0xF8` | `+0x8C` |
| OffsetX / OffsetY | `+0x160/+0x164` | `+0xC0/+0xC4` | `+0x100/+0x104` | `+0x90/+0x94` |

Android 使用 libstdc++ 风格的 24/12 字节 variant，iOS 当前产物的 ABI 大小相同；
64 位 discriminator 位于 variant 起点后 16 字节，32 位则位于起点后 8 字节。

## 构造写集合：CurrentTarget 空洞是直接证据

主基类构造入口为 A64 `0x531274`、A32 `0x495618`、I64 `0x100233C88`、
I32 `0x23295C`。各端相关写入如下：

- A64：`0x531328` 清 variant discriminator；`0x531330` 的两个 16 字节 store 清
  rule、两未知指针和 FrontTarget；`0x53132C` 清 BackTarget；下一写
  `0x531334` 已到 `+0x160` 的 OffsetX/Y。`+0x158` 完全跳过。
- A32：`0x4956A0` 的 `memset(+0xA4, 0, 0x18)` 清 discriminator、rule、两未知
  指针和 Front/Back；`0x49569C` 清 `+0xC0/+0xC4`。中间 `+0xBC` 未写。
- I64：`0x100233D20` 清 discriminator；`0x100233D30` 清 rule，
  `0x100233D2C` 清两未知指针，`0x100233D28` 清 Front/Back；
  `0x100233D24` 清 `+0x100` 的 OffsetX/Y。`+0xF8` 未写。
- I32：`0x232A20` 清 discriminator/rule 和两未知指针，`0x232A2A`、
  `0x232A26` 清 Front/Back，`0x232A1C` 清 OffsetX/Y；`+0x8C` 未写。

四端的 store 合并和调度顺序不同，却都留下同一个源码成员位置。这排除了“某端优化器
省略显式 null，因为等价”的解释：未初始化 pointer 的初值不可观察为确定 null。

## variant 与两枚未知指针的生命周期

根析构入口为 A64 `0x53244C`、A32 `0x49606C`、I64 `0x100233E1C`、
I32 `0x232B14`。四端分别把 variant 地址 `this+0x118/+0x9C/+0xB8/+0x6C`
交给各自 `tTJSVariant` 析构 helper；由于 discriminator 是 `tvtVoid`，未初始化 payload
不会被解释为有效引用。完整 start/stop/capture/Show/target-helper/析构扫描没有找到该
成员的业务读写。

两枚未知指针的唯一确认写点就是上述构造清零。根析构释放 FrontTarget、BackTarget、
rule texture、Modules 与容器节点，却不检查或清理这两槽。没有所有权行为可支持把它们
恢复成智能指针、texture 或 dispatch；源码保留 `void *..._guess` 是当前证据上限。

## CurrentTarget 的完整读写链

四端 `capture` 首次发布/正常清空位置分别为：

| 目标 | 发布 | 正常清空 |
|---|---:|---:|
| A64 | `0x531534` | `0x531648` |
| A32 | `0x4957AE` | `0x495828` |
| I64 | `0x100233FF8` | `0x1002340A4` |
| I32 | `0x232CE2` | `0x232D5E` |

四端 `Show` 的 transition front、transition back/普通 back 发布点及共同正常清空点为：

| 目标 | front 发布 | back 发布点 | 正常清空 |
|---|---:|---:|---:|
| A64 | `0x531B58` | `0x531BC4` | `0x531D5C` |
| A32 | `0x495B18` | `0x495B28` | `0x495C18` |
| I64 | `0x10023449C` | `0x1002344B0` | `0x1002345E4` |
| I32 | `0x233188` | `0x23319C` | `0x233308` |

两个确认消费者是 manager item `Draw` 与 `D3DLayer::Draw`；它们由上述 root 循环在发布后
调用。`ReleaseTargets` 只 release/null FrontTarget、BackTarget；根析构沿用该边界。
`StartBitmapCompletion` 也不读取 `CurrentTarget`：它从 manager 的 draw buffer 出发，取得
bitmap render manager、render target/reference texture 后执行 `OperateRect`。

因此普通 root 调用链不会先读未初始化值，但它仍不是一个可在任意时刻查询的稳定字段。
若 child `Draw`、严格 Layer 转换、texture update/assignment 或 render 操作抛异常，函数
没有 finally/RAII 清理，`CurrentTarget` 会保留最后发布的 target；`capture` 还会同时泄漏
其新建 texture。这一异常边界不能用 scope guard 擅自“修好”。

## 恢复源码与测试边界

本地字段恢复为无默认初始化的 `iTVPTexture2D *CurrentTarget;`。variant 继续依赖
`tTJSVariant` 自己只写 discriminator 的默认构造；两未知指针继续显式初始化为 null。

不存在安全、公开且不引入额外 ABI 的方式观察“首次发布前的 indeterminate pointer”，
因此不为它伪造测试 introspection。现有 capture/Show 测试覆盖可观察的发布、Draw、正常
清空路径；字节级构造差异由四端指令证据和 Web 构建验证约束。
