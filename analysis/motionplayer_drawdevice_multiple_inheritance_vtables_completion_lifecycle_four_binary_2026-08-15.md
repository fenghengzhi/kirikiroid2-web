# motionplayer DrawDevice 多重继承、vtable、完成回调与生命周期（四参考二进制）

日期：2026-08-15

## 结论

四个参考二进制中的根对象不是“脚本对象内含一个 draw-device 适配器”，而是一个真实的 C++ 多重继承对象：

```cpp
class DrawDeviceObjectBase
    : public DrawDeviceObjectBasePrimary_guess,
      public tTVPDrawDevice {
    // concrete-root tail
};
```

主基类负责脚本所有者、父子关系、四个有序容器、转场、目标纹理和绘制设置；`tTVPDrawDevice` 是位于非零偏移处的次基类。具体根对象因此有两个 vptr，`interface` 属性返回次基类地址而不是一个成员对象地址。次 vtable 中由插件覆盖的入口都是 adjustor thunk：先把 `this` 从 `tTVPDrawDevice *` 调回完整对象，再进入主实现。

析构链同样证明了继承关系：具体根析构先析构非零偏移处的 `tTVPDrawDevice`，再析构主基类。通过次 vtable 析构时，thunk 执行相同顺序并恢复完整对象地址。

原生实例所有权也由两个不同适配器分工：

1. 具体 `DrawDeviceD3D`、`D3D`、`D3DLayer` 的普通 NCBind 适配器保持默认 non-sticky，是真实对象的唯一所有者。
2. 根对象另行注册的 `D3DLayerBase` 适配器具有 `instance + sticky`，构造后被设为 sticky，只提供借用查找视图。
3. 每个 `D3DLayerObject` 另行注册的适配器更小，只有 `vptr + borrowed pointer`；其 `Invalidate` 为空，`Destruct` 只删除适配器自身，从不删除或清空所指对象。

因此旧移植中三个工厂手工 `setSticky()`、`D3DLayer` 析构主动 detach、以及自定义适配器 `Destruct()` 删除业务对象的组合并非原版结构，且会把真实所有权从具体 NCBind 适配器错误转移到借用适配器。

## 参考目标

本文只采用 `reference/binaries/` 中四个参考二进制的交叉证据：

| 简称 | 平台 / ABI | C++ 标准库特征 |
|---|---|---|
| A64 | Android arm64 | libstdc++ 风格红黑树 / list |
| A32 | Android armv7 Thumb | libstdc++ 风格红黑树 / list |
| I64 | iOS arm64 | libc++ 风格 tree / list |
| I32 | iOS armv7 Thumb | libc++ 风格 tree / list |

地址仅用于复核反编译数据库。源码中的恢复名称在原符号被剥离时保留 `_guess`。

## 具体根对象尺寸与两基类边界

| 目标 | 完整对象大小 | 主基类大小 / 次基类起点 | 具体尾部起点 | 关键构造证据 |
|---|---:|---:|---:|---|
| A64 | `0x200` | `0x178` | `0x1E0` | 工厂 `0x52B654`；主基类构造 `0x531274`；派生构造被内联 |
| A32 | `0x13C` | `0xD4` | `0x124` | 工厂 `0x492BFC`；派生构造 `0x4955C4`；主基类构造 `0x495618` |
| I64 | `0x1A0` | `0x118` | `0x180` | 工厂 `0x100230C88`；派生构造 `0x100233C10`；主基类构造 `0x100233C88` |
| I32 | `0x10C` | `0xA4` | `0xF4` | 工厂 `0x22FB28`；派生构造 `0x23287C`；主基类构造 `0x23295C` |

具体尾部在四个 ABI 上都有同一字段顺序：

```cpp
tjs_int PrimaryWidth;
tjs_int PrimaryHeight;
tjs_int ScreenLeft;
tjs_int ScreenTop;
void *TailPointer_guess; // 两个 concrete ctor 清零；无后续读、写或析构清理
tjs_int UpdateState;
```

| 字段 | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| `PrimaryWidth` | `+0x1E0` | `+0x124` | `+0x180` | `+0xF4` |
| `PrimaryHeight` | `+0x1E4` | `+0x128` | `+0x184` | `+0xF8` |
| `ScreenLeft` | `+0x1E8` | `+0x12C` | `+0x188` | `+0xFC` |
| `ScreenTop` | `+0x1EC` | `+0x130` | `+0x18C` | `+0x100` |
| `TailPointer_guess` | `+0x1F0` | `+0x134` | `+0x190` | `+0x104` |
| `UpdateState` | `+0x1F8` | `+0x138` | `+0x198` | `+0x108` |

64 位对象在 `UpdateState` 后还有自然对齐 padding。

`TailPointer_guess` 不是 64 位 alignment padding：它在 32 位对象里仍占恰好一个 4 字节
pointer-sized 槽，而且两个 concrete root 共享的完整构造函数会显式清零它。四端完整插件
listing 只有这个构造写点；紧随其后的 `UpdateState` 则另有公开 `update` 写入、`Show` 读取
并清零。完整析构链先销毁 `tTVPDrawDevice` 次基类再销毁主基类，从不检查该 pointer。
因此槽位必须保留，但原始指针类型/字段名和历史用途没有证据，继续使用 `_guess`。

## 主基类字段布局

四架构共同的源码字段顺序如下：

```text
primary vptr
ScriptOwner
ParentClearColor_guess              // 构造清零；setter 按 uint32* 解引用
ClearColor                         // 故意未初始化
InitialSizePointer_guess = null
InitialWidth_guess, InitialHeight_guess
ScreenSizePointer_guess = null
ScreenWidth, ScreenHeight
three zero state bytes
RenderTextureDirty_guess = false       // writers recovered; no plugin-range read
FrontItems
BackItems
ManagedObjects
Modules
TransitionActive = false
TransitionMethod                  // 故意未初始化
TransitionState = 0
TransitionVague                   // 故意未初始化
tTJSVariant TransitionVariant_guess
TransitionRuleTexture = null
TransitionPointer0_guess = null
TransitionPointer1_guess = null
FrontTarget = null
BackTarget = null
CurrentTarget                     // 故意未初始化
OffsetX, OffsetY                  // float
StretchType = 2
BicubicParam = -0.5f
ForceRenderTexture = false
```

2026-08-15 对 `clearColor` setter 的四端重新反编译纠正了这里原先的 `Parent` 命名。
setter 在写本地 `ClearColor` 后，若相邻指针非空，直接执行 `*pointer = color`；它没有
对象字段偏移、vtable 调用或完整 root 解引用。因此该槽不能继续声明成
`DrawDeviceObjectBase *Parent`，本地保守恢复为 `tjs_uint32 *ParentClearColor_guess`。
`AddChild` 只把同一槽的非空状态当作 nested marker， concrete root 构造都将其清零。
对四端全部已命名 base helper 和 primary-vtable 12 槽复核后，构造是唯一 writer，后续
只有 clearColor setter 与 AddChild 两个 reader；两个当前 concrete root 的正常生命周期
因此始终保持 null。该槽与 `D3DLayerObject` 中经过 SetParent/析构/索引树调用链验证的
真实 `DrawDeviceObjectBase *Parent` 是两个不同字段。

### 32 位证据否定了两个 `tTVPRect` 的旧假设

64 位构造中两个组都表现为“一个 64 位零值后跟 width/height”，仅看 A64/I64 容易误判成两个矩形。A32/I32 把每组明确拆成：

```text
pointer-sized zero slot + int32 width + int32 height
```

所以恢复源码不能把这两个组写成 `tTVPRect`。进一步对四端全部已恢复 root 方法、析构链
和 `D3DLayer::TransformPoint` 复核后，两枚 pointer 都只有构造清零，没有后续读、写或
析构清理。第一组的 width/height 也只有构造写入；第二组则必须拆开理解：pointer dormant，
但 width/height 是公开 `screenWidth/screenHeight` 与坐标变换所使用的 live state。邻接不
代表共同生命周期，更不能据此把 pointer 恢复成 size object、cache 或 owner。

### 跨 ABI 偏移

| 字段 / 组 | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| vptr / owner / parent / clear | `0/8/16/24` | `0/4/8/12` | `0/8/16/24` | `0/4/8/12` |
| initial ptr / W / H | `32/40/44` | `16/20/24` | `32/40/44` | `16/20/24` |
| screen ptr / W / H | `48/56/60` | `28/32/36` | `48/56/60` | `28/32/36` |
| state group / write-only guessed invalidation byte | `64 / 67` | `40 / 43` | `64 / 67` | `40 / 43` |
| `FrontItems` | `72` | `44` | `72` | `44` |
| `BackItems` | `120` | `68` | `96` | `56` |
| `ManagedObjects` | `168` | `92` | `120` | `68` |
| `Modules` | `216` | `116` | `144` | `80` |
| active / method / state / vague | `264/268/272/276` | `140/144/148/152` | `168/172/176/180` | `92/96/100/104` |
| transition `tTJSVariant` | `280` | `156` | `184` | `108` |
| variant type discriminator | `296` | `164` | `200` | `116` |
| rule | `304` | `168` | `208` | `120` |
| two unknown pointers | `312/320` | `172/176` | `216/224` | `124/128` |
| front / back / current target | `328/336/344` | `180/184/188` | `232/240/248` | `132/136/140` |
| offset X / Y | `352/356` | `192/196` | `256/260` | `144/148` |
| stretch / bicubic / force | `360/364/368` | `200/204/208` | `264/268/272` | `152/156/160` |

Android 的四个树容器各占 `0x30`/`0x18`，iOS libc++ 的对应容器各占 `0x18`/`0x0C`，解释了相同后续语义字段在两套标准库间的偏移差。

`TransitionVariant_guess` 的类型由构造时只把 discriminator 写成 `tvtVoid`，以及主基类
析构中精确进入 `tTJSVariant` 析构函数共同确认。默认构造没有清零它的 payload；该析构是
插件完整代码范围内唯一已证实消费者，不能擅自恢复语义名称。紧随 rule 的两个未知指针
只在构造时清零，没有后续读取、写入或清理。

三个 target 中只有 `CurrentTarget` 不在构造写集合内。四端都在 BackTarget 之后留下整整一
个 pointer-sized 空洞，下一次写已是 OffsetX/Y；所以这里不是反编译器漏显，而是与源码
默认初始化规则一致的 indeterminate pointer。正常 `capture`/`Show` 会在调用任何 child
`Draw` 之前发布它，正常尾部再写 null；`ReleaseTargets`、根析构和
`StartBitmapCompletion` 均不读取或清空它。异常路径仍会保留最后发布的 target。

第四个 state byte 的命名也需要同样保守。四端完整插件代码范围只找到四个写点：
`setForceRenderTexture`、`setScreenRect`、`setScreenWidth`、`setScreenHeight`；全部写入 1，
没有任何读取。它与 target 释放/force 属性写入的共现支持“invalidation/dirty”语义推断，
但当前四份二进制没有消费者能确认原始字段名或实际消费时机，因此源码名必须保留
`RenderTextureDirty_guess`，不能再写成已恢复的确定名称。

## 两个最终 vtable address point

| 目标 | primary vtable | secondary `tTVPDrawDevice` vtable |
|---|---:|---:|
| A64 | `0x19FA908` | `0x19FA978` |
| A32 | `0x10AAEA0` | `0x10AAED8` |
| I64 | `0x101AEE568` | `0x101AEE5D8` |
| I32 | `0x1838EF4` | `0x1838F2C` |

### Primary vtable 的 12 个槽

| 槽 | 语义 | A64 | A32 | I64 | I32 |
|---:|---|---:|---:|---:|---:|
| 0 | complete dtor | `0x531410` | `0x495744` | `0x100233F54` | `0x232C74` |
| 1 | deleting dtor | `0x531438` | `0x49575C` | `0x100233F7C` | `0x232C8C` |
| 2 | `capture` | `0x531468` | `0x495778` | `0x100233FA8` | `0x232CA8` |
| 3 | items-changed no-op | `0x531698` | `0x495836` | `0x1002340C0` | `0x232D6C` |
| 4 | `GetCursorPos` | `0x53169C` | `0x495838` | `0x1002340C4` | `0x232D6E` |
| 5 | `SetCursorPos` | `0x531700` | `0x495870` | `0x100234124` | `0x232D9C` |
| 6 | `AddLayerManager` | `0x531770` | `0x4958C4` | `0x100234174` | `0x232DCC` |
| 7 | `RemoveLayerManager` | `0x531824` | `0x49593C` | `0x100234228` | `0x232EE8` |
| 8 | `Show` | `0x531890` | `0x495978` | `0x100234294` | `0x232F1C` |
| 9 | `StartBitmapCompletion` | `0x531E7C` | `0x495D10` | `0x100234690` | `0x2333D8` |
| 10 | `NotifyBitmapCompleted` no-op | `0x5320F4` | `0x495F0C` | `0x1002348D8` | `0x233674` |
| 11 | `EndBitmapCompletion` no-op | `0x5320F8` | `0x495F0E` | `0x1002348DC` | `0x233676` |

主 vtable 把插件自己的 `capture` 与 changed hook 放在 draw-device 虚函数之前，证明它不是 `tTVPDrawDevice` 的 vtable，也证明主基类本身有虚函数。

### Primary-base-only vtable

| 目标 | address point | complete dtor | deleting dtor | pure `capture` | changed no-op |
|---|---:|---:|---:|---:|---:|
| A64 | `0x19FAB48` | `0x53244C` | `0x532584` | `0x1452CDC` | `0x531698` |
| A32 | `0x10AAFC0` | `0x49606D`（函数起点 `0x49606C`） | `0x49613D`（起点 `0x49613C`） | `0xD33F91` | `0x495837` |
| I64 | `0x101AEE7A8` | `0x100234B08` thunk → `0x100233E1C` | `0x100234B0C` | `0x10274FED0` | `0x1002340C0` |
| I32 | `0x1839014` | `0x2337B9` thunk → `0x232B14` | `0x2337BD` | `0x236DEE0` | `0x232D6D` |

A64 的 deleting dtor 入口是 `BRK #1`，A32 对应入口是 UDF trap；两份 iOS 参考则有正常的 delete wrapper。这是编译器/构建差异，不能据此把主基类在源码层恢复成“Android 没有虚析构”。四者一致的源码级事实仍是：完整派生对象的 deleting dtor 可用，主基类 vtable 有析构槽，而 `capture` 保持 pure virtual。

## Secondary vtable 与 adjustor thunk

在完整 `tTVPDrawDevice` vtable 中，插件相关槽位是：

| vtable index | 语义 |
|---:|---|
| 2 | `AddLayerManager` |
| 3 | `RemoveLayerManager` |
| 31 | `GetCursorPos` |
| 32 | `SetCursorPos` |
| 44 | `Show` |
| 45 | `StartBitmapCompletion` |
| 46 | `NotifyBitmapCompleted` |
| 47 | `EndBitmapCompletion` |
| 50 | `Clear` |
| 54 | complete dtor |
| 55 | deleting dtor |

每个插件覆盖入口的 `this` 调整量等于次基类偏移：A64 `-0x178`、A32 `-0xD4`、I64 `-0x118`、I32 `-0xA4`。

| 目标 | Add / Remove | cursor get / set | Show | Start / Notify / End | Clear | secondary dtors |
|---|---|---|---|---|---|---|
| A64 | `0x5320FC` / `0x532104` | `0x532170` / `0x5321D0` | `0x532240` | `0x532248` / `0x532250` / `0x532254` | `0x532258` | `0x53225C` / `0x532280` |
| A32 | `0x495F10` / `0x495F16` | `0x495F1C` / `0x495F4E` | `0x495F54` | `0x495F5A` / `0x495F60` / `0x495F62` | `0x495F64` | `0x495F66` / `0x495F78` |
| I64 | `0x1002348E0` / `0x1002348E8` | `0x1002348F0` / `0x1002348F8` | `0x100234900` | `0x100234908` / `0x100234910` / `0x100234914` | `0x1001CF970` | `0x100234918` / `0x10023493C` |
| I32 | `0x233678` / `0x23367E` | `0x233684` / `0x23368A` | `0x233690` | `0x233696` / `0x23369C` / `0x23369E` | `0x2336A0` | `0x2336A2` / `0x2336B4` |

I64 的 `Clear` 指向共享 `nullsub_135`，其余三份也都是空函数。`NotifyBitmapCompleted` 与 `EndBitmapCompletion` 同样为空，不能把像素上传或帧提交逻辑放进这两个回调。

## 析构调用链

完整对象的正常 complete dtor：

```text
DrawDeviceD3D::~DrawDeviceD3D
  ├─ tTVPDrawDevice::~tTVPDrawDevice(complete + secondaryOffset)
  │    └─ 释放 tTVPDrawDevice 自己的 manager vector / window-side 状态
  └─ DrawDeviceObjectBasePrimary_guess::~...
       ├─ ReleaseTargets: release FrontTarget，再 release BackTarget
       │    （不读取、不清空 CurrentTarget）
       ├─ release TransitionRuleTexture
       ├─ 遍历 Modules，调用每个非空 module 的 deleting/owning release 槽
       ├─ 析构 TransitionVariant_guess
       └─ 依源码逆序销毁 Modules、ManagedObjects、BackItems、FrontItems 的树节点
```

四份完整派生析构入口分别是 A64 `0x531410`、A32 `0x495744`、I64 `0x100233F54`、I32 `0x232C74`。次 vtable complete-dtor thunk 分别是 A64 `0x53225C`、A32 `0x495F66`、I64 `0x100234918`、I32 `0x2336A2`。

两个重要边界：

- `ManagedObjects` 是原始指针集合；根析构只释放树节点，不删除 `D3DImage`，也不清空图像对象里的 owner 指针。原版要求图像先于根对象销毁。
- manager item 的 `PrimaryOwner` 获取路径会 AddRef，但 base/软件派生 manager item 析构都没有对应 Release。这是四参考二进制共同保留的生命周期泄漏，移植不能在没有额外证据时“修正”掉。

## changed hook 的精确触发

主基类 vslot 3 当前是 no-op，但调用边界必须保留：

- `AddChild`：可选执行 child attach 与 front/back 两次插入后，无条件调用 changed hook；即便 child 是 null 也调用。入口证据 A64 `0x529CFC`、A32 `0x4923B0`、I64 `0x100230234`、I32 `0x22F2BE`。
- remove / detach：仅当 front 或 back 至少一次 erase 成功时，先调用 child `OnDetached`，再调用 changed hook。相关 helper：A64 `0x529F78` / `0x52A18C`，A32 `0x4922A4`，I64 `0x100230088`，I32 `0x22F1E4`。
- `SetParent_guess` 在 A64 被内联；A32 `0x492388`、I64 `0x1002301E8`、I32 `0x22F298` 保留 helper。

即使当前派生类未覆盖 changed hook，也不能因为它是 no-op 就删除调用点；那会改变未来派生类及异常/插桩边界。

## 光标虚函数

`GetCursorPos` 忽略传入 manager：直接调用 `Window->GetCursorPos`，再 `TransformToPrimary`；变换失败时把 x/y 都写成零。`SetCursorPos` 同样忽略 manager，先 `TransformFromPrimary`，成功后调用 `Window->SetCursorPos`。四份参考都没有 `Window == nullptr` 防护。

## `D3DPoint_guess` 与 offset ABI

`OffsetX`/`OffsetY` 是两个 `float`，不是 `tjs_int`。属性注册在 A32/I32 上走与 `bicubicParam` 相同的 float-property 适配器。回调入口：

| 目标 | getX | setX | getY | setY | setOffset |
|---|---:|---:|---:|---:|---:|
| A64 | `0x52B9E4` | `0x52B9EC` | `0x52B9F4` | `0x52B9FC` | `0x52BA04` |
| A32 | `0x492D92` | `0x492D98` | `0x492D9E` | `0x492DA4` | `0x492DAA` |
| I64 | `0x100230E8C` | `0x100230E94` | `0x100230E9C` | `0x100230EA4` | `0x100230EAC` |
| I32 | `0x22FD00` | `0x22FD06` | `0x22FD0C` | `0x22FD12` | `0x22FD18` |

`capture` 和 `Show` 都把这两个字段的 32 位位模式原样组成 8 字节栈对象，再以 const-reference 传给 `Draw`；不存在整数到浮点的转换指令。原始类型名被剥离，当前保守恢复为：

```cpp
struct D3DPoint_guess {
    float x;
    float y;
};
```

## manager item 的真实基类 / 派生类分支

`AddLayerManager` 的顺序是：

1. 先调用次基类 `tTVPDrawDevice::AddLayerManager`。
2. `static_cast<tTVPLayerManager *>(manager)->SetHoldAlpha(false)`；先前写成 `SetDesiredLayerType(0)` 是沿用旧 `libkrkr2.so` 注释造成的误认，已由四份当前参考二进制纠正。
3. 软件 draw buffer 构造带私有缓存纹理的派生 item；其他路径构造 base item。
4. `manager->SetDrawDeviceData(item)`。

| 目标 | software/base 大小 | software vtable | base vtable |
|---|---:|---:|---:|
| A64 | `0x68 / 0x60` | `0x19FABB0` | `0x19FAC18` |
| A32 | `0x3C / 0x38` | `0x10AAFF4` | `0x10AB028` |
| I64 | `0x70 / 0x68` | `0x101AEE818` | `0x101AEE880` |
| I32 | `0x40 / 0x3C` | `0x1839048` | `0x183907C` |

两个类型共享 vtable slots 2..9；slot 3 是 Draw，slot 10 是“取得源纹理”：

| 目标 | Draw slot | software slot 10 | base slot 10 |
|---|---:|---:|---:|
| A64 | `0x532CD4` | `0x532EFC` | `0x53321C` |
| A32 | `0x496738` | `0x4968D4` | `0x496B7C` |
| I64 | `0x10023527C` | `0x1002354B0` | `0x10023576C` |
| I32 | `0x233FC8` | `0x23421C` | `0x234508` |

base slot 10 返回 bitmap 自有 texture。software slot 10 维护派生类末尾的 OpenGL texture：尺寸相同则 update，尺寸变化则 release/recreate；参考实现没有 bitmap-null 或 pitch 防护。

Draw 的精确 guard 只有三层：

```text
Parent != null
manager->GetDrawBuffer() != null
render method != null
```

它不检查 `Manager` 字段、`Parent->CurrentTarget`、primary-owner 属性访问结果或 slot-10 返回的源纹理。额外补 guard 会改变原版崩溃边界。

base item 构造会先无条件解引用 `manager->GetPrimaryLayer()`，再通过 helper 取得 primary owner 并在非空时 AddRef；A64 对应 helper `0x8333B4`，I32 对应 helper `0x95530`。随后它无条件取得 primary layer 的 main image 并以 ARGB 0 填满；primary layer 或 main image 为空都会落入原版崩溃边界。base dtor 直接等同/跳转到 `D3DLayerObject` dtor；software dtor 只额外 release 缓存纹理。两者均不 Release `PrimaryOwner`。

## `StartBitmapCompletion` 的完整顺序

四份参考一致：

1. `manager->GetDrawBuffer()`；结果为 null 时立即返回。没有 manager-null guard。
2. 取得 render manager。
3. 通过函数局部 guarded statics 缓存 `FillARGB` 与 color 参数 ID。
4. color 固定为 0；不读取根对象 `ClearColor`。
5. 调用 software-renderer 判定；target/reference 都在判定之后、各自的分支内取得，完全不读取根对象 `CurrentTarget`。
6. software 分支先从具体 `tTVPLayerManager` 的 completion update-region 快照 `Count/Head`，再从 draw-buffer bitmap 直接读取当前 texture 作为 reference，最后调用 `bitmap->GetTextureForRender(false, nullptr)` 取得 target。即使 region 为空，reference 快照与 target 转换仍会发生；没有 bitmap texture/target-null guard。
7. software buffer 遍历 update regions；若 `targetWidth < (unsigned)rect.right || targetHeight < (unsigned)rect.bottom`，是 `break` 整个遍历，不是跳过单个 rect；没有 left/top 负数检查。target/reference/render manager/method 在循环外固定，target 的直接宽高字段则在每个 rect 前重新读取。
8. GPU 分支先从 bitmap 依次读取宽、高构造完整 rect，再调用 `GetTextureForRender(false, nullptr)` 取得 target，随后才直接读取 bitmap 当前 texture 作为 reference。
9. `NotifyBitmapCompleted` 与 `EndBitmapCompletion` 都是 no-op。

这说明 completion 回调在 draw-buffer bitmap 自身的 render target/reference 之间执行清零操作，而不是使用根对象 `CurrentTarget`，也不是被 `NotifyBitmapCompleted` 推送像素。software 与 GPU 的 reference 快照分列 target 转换前后；`GetTextureForRender` 若重入并替换 bitmap texture，两条路径会刻意观察到不同版本。

## 两类独立原生实例适配器

### 根对象的 `D3DLayerBase` sticky 借用适配器

注册 helper：A64 `0x5322AC`、A32 `0x495F90`、I64 `0x100234964`、I32 `0x2336CA`。

它复现标准 `ncbInstanceAdaptor<T>` 的三字段布局和更新语义：

```cpp
vptr
T *instance
bool sticky
```

大小为 A64/I64 `0x18`，A32/I32 `0x0C`。新建时 instance/sticky 为零；已有 adaptor 且旧 instance 非空时，先按旧 sticky 条件删除旧实例并把 instance/sticky 清零；若旧 instance 本来为空，则保留旧 sticky。随后写入新实例并再次注册。

| 目标 | vtable address point | Construct | Invalidate | Destruct | complete dtor | deleting dtor |
|---|---:|---:|---:|---:|---:|---:|
| A64 | `0x19FAB78` | `0x524688` | `0x532588` | `0x524694` | `0x5325C8` | `0x532624` |
| A32 | `0x10AAFD8` | `0x48F931` | `0x49613F` | `0x48F937` | `0x49615D` | `0x496199` |
| I64 | `0x101AEE7D8` | `0x1000302F4` | `0x100234B20` | `0x100038D28` | `0x100234B60` | `0x100234BC0` |
| I32 | `0x183902C` | `0x2E945` | `0x2337CD` | `0x1296B1` | `0x2337EB` | `0x233825` |

`Invalidate` 与两个析构入口都只在 `instance != null && sticky == false` 时调用业务对象 vtable 的 deleting-dtor 槽，然后清空状态。主基类构造在注册后重新查询这个 adaptor，并把 sticky 字节写成 1：A64 `+0x10`，其余 32 位 `+0x08`，I64 `+0x10`。查询/注册失败后的空指针写入没有保护，是原始边界。

### `D3DLayerObject` 的两字段 borrowed adaptor

构造注册入口：A64 `0x5333F0` 内联段、A32 `0x496990`、I64 `0x1002355B4`、I32 `0x2342B4`。只有 owner 非空才分配并注册；注册返回值被忽略。

大小为 A64/I64 `0x10`，A32/I32 `0x08`：

```cpp
vptr
D3DLayerObject *borrowed_instance
```

| 目标 | vtable address point | Construct | Invalidate | Destruct | complete dtor | deleting dtor |
|---|---:|---:|---:|---:|---:|---:|
| A64 | `0x19FAC80` | `0x524688` | `0x524690` | `0x524694` | `0x524818` | `0x533224` |
| A32 | `0x10AB05C` | `0x48F931` | `0x48F935` | `0x48F937` | `0x48FA49` | `0x496B81` |
| I64 | `0x101AEE908` | `0x1000302F4` | `0x10003047C` | `0x100038D28` | `0x100235774` | `0x100235788` |
| I32 | `0x18390C0` | `0x2E945` | `0x2EA31` | `0x1296B1` | `0x23450D` | `0x23451D` |

`Invalidate` 是共享空函数。`Destruct` 只在 `this != null` 时跳到自身 vtable slot 4；两字段派生 dtor不访问 `borrowed_instance`，deleting dtor最终只执行 `operator delete(adaptor)`。所以它既不拥有、也不 detach 业务对象。

四个 `D3DLayer` complete dtor 进一步排除了主动 detach：

| 目标 | complete dtor | 实际工作 |
|---|---:|---|
| A64 | `0x5335AC` | 写派生 vptr，析构 `Mat4 @ +0x4C`，尾调 `D3DLayerObject` dtor |
| A32 | `0x496E6C` | 写派生 vptr，析构 `Mat4 @ +0x34`，尾调 base dtor |
| I64 | `0x100235A38` | 写派生 vptr，`Mat4 @ +0x54` 空析构，尾调 base dtor |
| I32 | `0x234858` | 写派生 vptr，`Mat4 @ +0x38` 空析构，尾调 base dtor |

没有一份在析构中查询 native instance、清空 borrowed pointer 或修改 sticky。

### 具体 NCBind adaptor 才是所有者

三个工厂本体：

- `DrawDeviceD3D`: A64 `0x52B654`（另三份对应恢复工厂同样无 sticky 写入）
- `D3D`: A64 `0x52CC54`
- `D3DLayer`: A64 `0x52D308`、A32 `0x49361C`、I64 `0x1002317E8`、I32 `0x230594`

这些工厂本体只校验参数、分配、构造并写回结果；没有 concrete adaptor 查询，也没有 sticky 字节写入。2026-08-17 的 D3DLayer ClassInfo/Factory 四端纵切面进一步直接反编译了四份 descriptor wrapper，确认返回后会按 concrete D3DLayer class ID 查询 adaptor，并把指针原样写入默认 non-sticky payload 槽；查询失败则删除刚构造的 layer 并返回 `TJS_E_NATIVECLASSCRASH`。因此这里不再只是由本地 `ncbind.hpp` 外推。该直接证据也解释了为何 root 的 base adaptor 必须 sticky、为何 `D3DLayerObject` adaptor 必须 borrowed：否则同一个脚本对象会有两个业务对象所有者。

## 已恢复到源码的约束

- 删除成员式 `DrawDeviceAdapter`，恢复主基类 + `tTVPDrawDevice` 双继承。
- `interface` 返回 `static_cast<iTVPDrawDevice *>(this)`，由 C++ 自动产生非零基址调整。
- 主/次虚函数由同一完整对象实现；通知、结束和 clear 保持空函数。
- 根字段顺序按四 ABI 交叉结果排列；两个尺寸组恢复成 pointer + width + height，而非矩形。
- `OffsetX/Y` 与 `D3DPoint_guess` 都恢复为 float。
- manager item 恢复 base/software 两类及软件纹理缓存。
- changed hook 的无条件/有条件触发位置被保留。
- 三个具体工厂不再手工 `setSticky()`。
- `D3DLayerBaseNativeInstance` 恢复标准 sticky 清理语义；其 `Destruct` 继承“delete adaptor itself”。
- `D3DLayerObjectNativeInstance` 恢复为纯 borrowed 两字段适配器；`D3DLayer` 析构不 detach。

## 仍需保持 `_guess` 或继续追踪的点

- 两个尺寸组的 pointer 字段、根状态前三个字节、转场 variant、rule 后两个 pointer 和具体尾部 pointer 的原始源码名称均被剥离；虽然布局已确定，语义名仍不能强造。
- `TransitionVariant_guess` 的类型已确定，但业务用途未找到。
- manager item 中 owner 后的三个槽已由四端构造写入、完整虚函数面和历史
  `DrawDeviceD3D::LayerManagerInfo` 源码交叉恢复为 dormant
  `textureBuffer/texturePitch/lastOK`；四份当前二进制均没有构造后的消费者，详见
  `motionplayer_drawdevice_manager_legacy_texture_lock_tail_four_binary_2026-08-15.md`。
- Android 主基类 deleting-dtor trap 与 iOS 正常 wrapper 的源码/编译选项成因尚不能从剥离二进制唯一确定。
- `D3DLayerObject` borrowed adaptor 在业务对象先销毁后会保留悬空值；参考析构从不清空它。这是已确认边界，不应在移植中擅自“安全化”。
