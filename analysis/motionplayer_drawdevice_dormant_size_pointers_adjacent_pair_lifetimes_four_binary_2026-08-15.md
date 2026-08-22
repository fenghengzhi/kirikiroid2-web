# motionplayer DrawDevice dormant size pointers 与相邻 pair 生命周期（四参考二进制）

日期：2026-08-15

## 结论

`DrawDeviceObjectBase` 主基类头部的两个 pointer-sized 槽位已经可以从“未知尺寸组成员”
进一步收敛为当前四参考版本中的 dormant pointer slots：

```cpp
void *InitialSizePointer_guess;
int32_t InitialWidth_guess;
int32_t InitialHeight_guess;
void *ScreenSizePointer_guess;
int32_t ScreenWidth;
int32_t ScreenHeight;
```

四端构造都把两个 pointer 写成 null。对当前已经恢复的全部 root 成员函数、完整析构链、
公开 screen getter/setter，以及唯一跨对象 screen-size 业务消费者
`D3DLayer::TransformPoint` 逐一复核后，没有发现任一 pointer 的构造后读、写或清理。

相邻 pair 的生命周期彼此不同，不能把三个槽看成一个活跃复合对象：

- `InitialWidth/InitialHeight_guess` 只由构造写入，在当前完整插件范围也没有业务读取；
- `ScreenWidth/ScreenHeight` 是 live public state，会被公开 setter 改写、getter 读取，
  并被 `D3DLayer::TransformPoint` 用来计算屏幕中心；
- `ScreenSizePointer_guess` 即使紧邻 live pair，仍然没有参与任何一条上述数据流；
- 两枚 pointer 的析构均没有 null-check、release、delete 或单纯清零，因此当前二进制也
  不支持 owner、reference-counted object 或缓存对象等生命周期语义。

能确定的是槽位宽度、偏移、构造初值和 dormant 边界。原始字段名、指向类型以及旧版本
历史用途仍被剥离，源码必须继续保留 `_guess`。

## 构造写点与 ABI 布局

| 目标 | 主基类构造 | initial pointer null store | screen pointer null store |
|---|---:|---:|---:|
| Android arm64 | `0x531274` | `0x5312C4` | `0x5312C8` |
| Android armv7 | `0x495618` | `0x495648` | `0x49564C` |
| iOS arm64 | `0x100233C88` | `0x100233CC4` | `0x100233CCC` |
| iOS armv7 | `0x23295C` | `0x232998` | `0x2329A2` |

字段布局为：

| 字段 | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| `InitialSizePointer_guess` | `+0x20` | `+0x10` | `+0x20` | `+0x10` |
| `InitialWidth_guess` | `+0x28` | `+0x14` | `+0x28` | `+0x14` |
| `InitialHeight_guess` | `+0x2C` | `+0x18` | `+0x2C` | `+0x18` |
| `ScreenSizePointer_guess` | `+0x30` | `+0x1C` | `+0x30` | `+0x1C` |
| `ScreenWidth` | `+0x38` | `+0x20` | `+0x38` | `+0x20` |
| `ScreenHeight` | `+0x3C` | `+0x24` | `+0x3C` | `+0x24` |

构造数据流四端一致：

```text
InitialSizePointer_guess = null
InitialWidth_guess       = factory width
InitialHeight_guess      = factory height
ScreenSizePointer_guess  = null
ScreenWidth              = factory width
ScreenHeight             = factory height
```

32 位布局是决定性反证：每组总大小 `0x0C`，明确拆成一个 4-byte pointer 和两个 4-byte
signed integer，而不是 16-byte `tTVPRect`。64 位每组的自然布局则是 8-byte pointer 加两个
int32，总大小 `0x10`。

## 构造后引用审计

本轮重新反编译了四端所有当前已命名的 `DrawDeviceObjectBase` 方法，包括 root 构造/析构、
公开属性入口、child 管理、manager 管理、capture、Show、completion、target helpers、
`UpdateObjects`、`DrawFrontItems` 和 manager-setting 更新。按主基类相对偏移检查结果为：

- initial pointer：每端唯一引用都在主基类构造；
- screen pointer：每端唯一引用都在主基类构造；
- initial W/H：构造写入后没有业务 reader，也没有尺寸 setter 回写；
- screen W/H：存在明确的公开读写和跨对象几何 reader；
- 两枚 pointer：root 析构和 target release helper 均不读取、不清空、不释放。

这是“当前插件代码没有消费者”的结论，而不是“原始源码一定把它们声明成 `void *`”。
`void *` 只是剥离类型信息后的最保守 ABI 占位；若以后从另一版本符号、源包或新的调用点
找到类型证据，应替换 `_guess`，但不能在没有证据时先验恢复成 rectangle、size cache、
parent link 或 texture pointer。

## live screen pair 的独立数据流

公开入口地址：

| 方法 | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| `setScreenRect` | `0x52BA98` | `0x492E0C` | `0x100230F38` | `0x22FD80` |
| `getScreenWidth` | `0x52BB30` | `0x492E6E` | `0x100230FA8` | `0x22FDC8` |
| `setScreenWidth` | `0x529A94` | `0x49222C` | `0x10022FF68` | `0x22F0BE` |
| `getScreenHeight` | `0x52BB38` | `0x492E72` | `0x100230FB0` | `0x22FDCC` |
| `setScreenHeight` | `0x529AF8` | `0x492268` | `0x10022FFF0` | `0x22F108` |

这些入口都直接访问 `ScreenWidth/ScreenHeight`。尺寸变化路径会释放 Front/Back target 并写
第四个 root state byte，但不读写 `ScreenSizePointer_guess`。因此 target invalidation 也属于
screen integer pair 的数据流，而不是 pointer 的隐藏生命周期。

`D3DLayer::TransformPoint` 的四端入口：

| A64 | A32 | I64 | I32 |
|---:|---:|---:|---:|
| `0x533688` | `0x496EFC` | `0x100235B10` | `0x2348E0` |

四端分别从 parent `+0x38/+0x3C`（64 位）或 `+0x20/+0x24`（32 位）读取 signed int32，
也就是 `ScreenWidth/ScreenHeight`：

```cpp
x = matrix.m12 + float(parent->ScreenWidth / 2) + x * matrix.m0;
y = matrix.m13 + float(parent->ScreenHeight / 2) + y * matrix.m5;
```

前一枚 screen pointer 在 `+0x30`/`+0x1C`，没有 load。整数除法先执行，再转 float；负奇数
遵循 C++ signed division 向零截断。这一调用链同时确认 pair 的 signed int32 类型和
pointer/pair 生命周期的严格分离。

## 析构负证据

主基类析构入口为 A64 `0x53244C`、A32 `0x49606C`、I64 `0x100233E1C`、I32
`0x232B14`。四端都只处理：

1. Front/Back target；
2. transition rule texture；
3. module value；
4. transition `tTJSVariant`；
5. 四个标准库有序容器。

两个 size pointer 不在析构读取集合里。这不能证明它们永远不会指向资源，但明确规定了
当前参考版本的边界：即使通过未恢复的外部手段改变其值，root 析构也不会替调用者释放。
移植版不应凭安全性直觉添加 release/delete/null，也不应让 screen setter 隐式维护它们。

## IDB 与源码落点

四份 recovery IDB 新增了完整主基类 ABI 类型：

- `DrawDeviceObjectBasePrimaryFull_android64_guess`，大小 `0x178`；
- `DrawDeviceObjectBasePrimaryFull_android32_guess`，大小 `0xD4`；
- `DrawDeviceObjectBasePrimaryFull_ios64_guess`，大小 `0x118`；
- `DrawDeviceObjectBasePrimaryFull_ios32_guess`，大小 `0xA4`。

同时新增对应完整 root 类型，大小分别为 `0x200/0x13C/0x1A0/0x10C`。构造、析构、
`setScreenRect`、screen getter/setter 已应用完整类型；构造 null stores、TransformPoint 和
析构负证据均加入注释与书签。`TransformPoint` 的 Hex-Rays 局部变量自动类型覆盖未能稳定
落盘，因此保留函数注释和明确 parent offset 证据，不把一次失败的 local-type 操作当作成果。

源码 `cpp/plugins/DrawDeviceD3D.cpp` 保留两枚 `void *_guess` ABI 槽位，并把注释收紧为：
两枚 pointer 均 constructor-null/dormant；initial pair constructor-only；screen pair 独立保持
live。源码注释不写绝对地址，复核地址只保存在本文与 recovery IDB。
