# MotionPlayer DrawDevice clearColor 首次使用、镜像指针与 Show 方法缓存（四参考）

日期：2026-08-15

范围：`reference/binaries/` 中 Android arm64、Android armv7、iOS arm64、iOS armv7。
本轮完全以四份当前参考重新反编译，不沿用旧 `libkrkr2.so` 注释。

## 结论

`DrawDeviceD3D` / `D3D` 根对象的 `ClearColor` 是一个构造时故意不写的 32 位字段。
getter 在第一次 setter 前直接读取这四个未初始化字节；参考实现没有隐含黑色、透明色或
ARGB 0 默认值。

公开 setter 也不是旧移植中的单纯成员 store。四端都严格执行：

```cpp
ClearColor = color;
if (ParentClearColor_guess)
    *ParentClearColor_guess = color;
```

相邻指针槽被当作 `uint32*` 直接解引用；把它命名为完整 root `Parent` 会产生错误的对象
模型。具体 root 构造将该槽清零。本轮对四端 39/42/44/44 个已命名 base helper 和全部
12 个 primary-vtable 方法重新批量反编译后，构造后的精确访问集合只有 `setClearColor`
与 `AddChild` 两个 reader，没有 writer。因而两个 concrete root 经所有插件内正常路径都
会让该槽终身保持 null，镜像 store 与 nested hook 分支均不可达；它们仍必须保留为基类
机器码中的结构和边界行为，不能因为具体实例走不到就从恢复源码删除。

`Show()` 对 clear method 的处理也比旧源码更窄、更稳定：第一次通过 Window gate 后，
不论 transition 是否 active，都会以两个 C++ guarded statics 缓存 `FillARGB` method 和
`color` 参数 ID。active 时每帧只发布一次 `ClearColor`，只从 FrontTarget 读取一次宽高，
然后用同一个矩形 Fill Front/Back。`AlphaBlend_SD` 与 `opacity` 是另一组只在第二次
active 分支内首次初始化的 guarded statics。

## 入口地址

| 入口 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| 主基类 ctor | `0x531274` | `0x495618` | `0x100233C88` | `0x23295C` |
| `getClearColor` | `0x52B7EC` | `0x492C56` | `0x100230D1C` | `0x22FC14` |
| `setClearColor` | `0x529614` | `0x491F54` | `0x10022FD28` | `0x22EE68` |
| `AddChild` | `0x529CFC` | `0x4923B0` | `0x100230234` | `0x22F2BE` |
| root `capture` | `0x531468` | `0x495778` | `0x100233FA8` | `0x232CA8` |
| root `Show` | `0x531890` | `0x495978` | `0x100234294` | `0x232F1C` |
| `StartBitmapCompletion` | `0x531E7C` | `0x495D10` | `0x100234690` | `0x2333D8` |
| manager-item ctor | `0x53287C` | `0x496480` | `0x100234FA8` | `0x233C14` |

setter 在 Android/iOS 的链接布局中早于 getter，且被 `DrawDeviceD3D` 与 `D3D` 两个
NCB member table 共同引用；不能因为 getter 与其他公开小函数连续排列，就猜出一个并不
存在的相邻 setter。

## 字段布局与构造遗漏

| 字段 | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| `ScriptOwner` | `+0x08` | `+0x04` | `+0x08` | `+0x04` |
| `ParentClearColor_guess` | `+0x10` | `+0x08` | `+0x10` | `+0x08` |
| `ClearColor` | `+0x18` | `+0x0C` | `+0x18` | `+0x0C` |
| next pointer-sized slot | `+0x20` | `+0x10` | `+0x20` | `+0x10` |

四个主基类 ctor 都写 owner、把 `ParentClearColor_guess` 清零、把 next pointer slot 清零，
但跨过 `ClearColor` 不写。派生 ctor / A64 内联 factory 只构造 `tTVPDrawDevice` 次基类并
初始化具体尾部，也没有补写这个字段。对象来自普通 `operator new`，factory 没有
value-initialize 或整对象 `memset`，所以不能把偶然得到的零页当作源码默认值。

在 C++ 抽象机层面，setter 前读取该未初始化标量没有定义良好的值；在四份现成机器码
层面，getter/active Show 都只是读取对象内当前四字节。复原源码应保留“没有 initializer”
这一事实，而不是为了测试稳定性擅自写 `= 0`。

## setter 与 guessed pointer 的精确语义

四端 setter 只有以下顺序差异，语义一致：Android A64/A32 先 load 指针再写本地字段；
iOS A64/A32 先写本地字段再 load 指针。之后都以 null check 保护一次无偏移 32 位 store。

```text
p = this->ParentClearColor_guess
this->ClearColor = uint32(value)
if p != null:
    *p = uint32(value)
```

它没有：

- target Release/recreate；
- Window `RequestUpdate` / `NotifySrcResize`；
- transition state 修改；
- 第四个 write-only root-state byte 写入；
- 对镜像目标的 AddRef、生命周期管理或异常保护。

`AddChild` 对同一指针槽只做非空判断；非空时在插入 child 前调用 child 的
`OnParentHasParent` 虚槽。这里提供了 “nested state” 关联，但不能证明 stripped 源码的
原始字段名。`ParentClearColor_guess` 因而保留 `_guess`。它必须与 `D3DLayerObject` 中有
独立构造、SetParent、索引树更新和析构证据的真实 `DrawDeviceObjectBase *Parent` 区分。

### 完整 root 访问集合

本轮把刚恢复的完整主基类/root ABI 类型应用到 ctor、getter、setter 与 `AddChild` 后，
又对四端全部当前已命名 `DrawDeviceObjectBase*` 函数和 primary vtable 的 12 个槽执行成员
偏移审计。`ParentClearColor_guess` 的集合严格为：

| 操作 | A64 | A32 | I64 | I32 | 语义 |
|---|---:|---:|---:|---:|---|
| ctor null store | `0x5312C0` | `0x495646` | `0x100233CC0` | `0x232992` | 唯一 writer |
| setter pointer load | `0x529614` | `0x491F54` | `0x10022FD2C` | `0x22EE6A` | null-check 前读取 |
| setter indirect scalar store | `0x529620` | `0x491F5A` | `0x10022FD34` | `0x22EE6E` | `*pointer = color` |
| `AddChild` marker read | `0x529D30` | `0x4923D4` | `0x100230268` | `0x22F2D8` | 只判断 null/non-null |

没有 direct pointer setter、copy/move、attach 写入、clearColor getter 写入、Show/capture 写入，
也没有析构清零或释放。四个 concrete root 工厂都进入同一个主基类构造语义；完整派生构造
只继续构造 `tTVPDrawDevice` 次基类与 concrete tail，不回填该槽。因此在不计任意内存破坏
或调试器外部改写时，当前插件产生的 `DrawDeviceD3D` 与 `D3D` 实例永远满足：

```text
ParentClearColor_guess == null
setClearColor: only writes local ClearColor
AddChild: never calls child.OnParentHasParent through this marker
```

这里的“不可达”只描述两个当前 concrete roots；基类函数体本身仍真实包含两个分支。为了
一比一保留潜在旧版/未实例化派生类的 ABI 和崩溃边界，源码不能常量折叠它们，也不能把
字段降格为 alignment padding。

公开 NCB 类型是 `tjs_uint32`。TJS integer 经 unsigned 32 位转换后进入 setter，因此
负数按模 `2^32` 截断；getter 再把 unsigned 结果物化成 TJS integer。本地回归覆盖了
`0x89ABCDEF` 与 `-1 -> 0xFFFFFFFF`。

## `Show` 中 FillARGB 的首次使用与调用次序

四端完整 `Show` 的相关顺序一致：

```text
UpdateObjects(UpdateState)
UpdateState = 0
if Window == null: return

for manager in Managers: UpdateSettings if manager data is non-null
renderManager = private OpenGL render manager
ensure FrontTarget/BackTarget

static fillMethod = renderManager.GetRenderMethod("FillARGB")
static colorId = fillMethod.EnumParameterID("color")

if TransitionActive:
    fillMethod.SetParameterColor4B(colorId, ClearColor)       // 一次
    fillRect = [0, 0, FrontTarget.width, FrontTarget.height] // 一次
    OperateRect(fillMethod, FrontTarget, FrontTarget, fillRect, [])
    OperateRect(fillMethod, BackTarget,  BackTarget,  fillRect, [])

    draw front plane into FrontTarget
    draw back plane into BackTarget

    if TransitionActive:                                    // 真实第二次读取
        static blendMethod = renderManager.GetRenderMethod("AlphaBlend_SD")
        static opacityId = blendMethod.EnumParameterID("opacity")
        blendMethod.SetParameterFloat(opacityId, TransitionState)
        blend FrontTarget over BackTarget
else:
    draw front plane directly into BackTarget
```

重要边界：

- Window 为 null 时，Fill statics 不初始化，也不读 `ClearColor`；
- Window 非 null 且 transition inactive 时，Fill statics 初始化，但不读 `ClearColor`；
- 第一次 active Show 若脚本从未写 `clearColor`，会读取构造遗留的四字节；
- 每个 active Show 只调用一次 `SetParameterColor4B`，不是每个 target 一次；
- BackTarget 的 Fill 复用 FrontTarget 矩形，不重新读取 BackTarget 宽高；
- Fill method/ID 是进程级函数局部静态缓存，不随 root 析构、target resize 或 renderer
  状态变化而清空；
- blend method/ID 使用另一组静态存储，并且只有到达第二次 active 分支才初始化。

## guarded-static 存储

| 存储 | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| Fill method | `0x1AAF760` | `0x110E284` | `0x101AEE938` | `0x18390D8` |
| Fill guard | `0x1AAF768` | `0x110E288` | `0x101AEE940` | `0x18390DC` |
| color ID | `0x1AAF770` | `0x110E28C` | `0x101AEE948` | `0x18390E0` |
| color-ID guard | `0x1AAF778` | `0x110E290` | `0x101AEE950` | `0x18390E4` |
| blend method | `0x1AAF780` | `0x110E294` | `0x101AEE958` | `0x18390E8` |
| blend guard | `0x1AAF788` | `0x110E298` | `0x101AEE960` | `0x18390EC` |
| opacity ID | `0x1AAF790` | `0x110E29C` | `0x101AEE968` | `0x18390F0` |
| opacity-ID guard | `0x1AAF798` | `0x110E2A0` | `0x101AEE970` | `0x18390F4` |

四端都分别保护 method 与 parameter ID，而不是用一个 guard 包住两个初始化。若 method
lookup 或 ID lookup 抛异常，标准 guarded-static 语义决定相应 guard 不发布完成，后续
调用会重试；本地使用两个独立函数局部 statics 保留该边界。

## 其他 FillARGB 路径与 ClearColor 的隔离

`FillARGB` 字符串全部交叉引用再次确认四类不同消费者：

- root `Show`：只有 transition active 才把根 `ClearColor` 发布给 method；
- root `capture`：每次创建独立 target，但不调用 `FillARGB`，也不读根 `ClearColor`；
- `StartBitmapCompletion`：有自己独立的一组 guarded statics，颜色固定为 ARGB 0；
- `DrawDeviceManagerItem` ctor：清 primary main image 时同样使用固定 ARGB 0；
- `D3DAdaptor_clearTargetTexture`：属于另一对象布局，使用调用参数/其独立 clear 状态，
  不是这个 root 字段。

因此不能把 completion/manager-item 的 0 当作根 ClearColor 默认值，也不能因为 capture
产出透明/黑色像素就假设它经过了 root 清屏。

## 落地

`cpp/plugins/DrawDeviceD3D.cpp` 已：

- 将旧 `DrawDeviceObjectBase *Parent` 字段纠正为
  `tjs_uint32 *ParentClearColor_guess`；
- 恢复 setter 的条件镜像写；
- 保留 `ClearColor` 无 initializer；
- 删除旧的“每 target lookup/set/rect” Fill helper 形状；
- 在 `Show` 中恢复 active 判断前的 Fill method/ID statics、单次设色和共享 Front rect；
- 把 blend method/ID 恢复为第二次 active 分支内的独立 statics。

`tests/unit-tests/plugins/motionplayer-dll.cpp` 不读取构造态 clearColor，只验证明确写入后的
unsigned 32 位 round-trip 与负数截断。

四份 recovery IDB 已统一命名 `setClearColor`、`DrawDeviceObjectBase_AddChild_guess`、
Fill/blend method 与 parameter-ID 静态存储；完整 root 类型已应用到 clearColor getter/
setter 与 `AddChild`，并在 ctor 唯一 writer、两个 reader、capture、Show、
StartBitmapCompletion 写入注释和首次使用书签。
