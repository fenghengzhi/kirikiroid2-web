# MotionPlayer D3DAdaptor 状态消费者四参考恢复（2026-08-15）

## 结论

`D3DAdaptor` 前缀中的五个布尔值和一个颜色整数并不是六个都参与渲染。四份参考二进制的
完整 adaptor/public-wrapper/Player/raw-renderer consumer surface 给出以下精确分类：

| 状态 | 默认值 | 脚本访问面 | 构造后的 native consumer | 实际语义 |
|---|---:|---|---|---|
| `visible` | `false` | getter + setter | 无 | 纯脚本回显；不是渲染门 |
| `canvasCaptureEnabled` | `false` | getter + setter | `renderFromPlayer` | Player-to-target 渲染总门 |
| `clearEnabled` | `true` | getter + setter | `clearTargetTexture(color)` | 显式 `FillARGB` 清屏门 |
| `resizable` | `false` | setter only | 无 | setter-retained 状态；不触发 resize/recreate |
| `alphaOpAdd` | `false` | getter + setter | 无 | 纯脚本回显；共享 renderer 固定传 `true` |
| `clearColor` | `0` | `setClearColor` only | 无 | setter-retained 状态；清屏使用调用参数 `color` |

加上此前闭合的 `dormantState_guess`，完整前缀状态中只有
`canvasCaptureEnabled` 和 `clearEnabled` 在构造后进入原生执行控制流。不能根据属性名字给
`visible`、`alphaOpAdd`、`resizable` 或 `clearColor` 补造“合理”的 runtime 行为。

## 四端入口与真实读点

### 脚本 getter/setter

| 成员 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `visible` get/set | `0x6AACE4` / `0x6AACEC` | `0x57CF84` / `0x57CF88` | `0x100103D9C` / `0x100103DA4` | `0x10115E` / `0x101162` |
| `alphaOpAdd` get/set | `0x6AACF8` / `0x6AAD00` | `0x57CF8C` / `0x57CF90` | `0x100103DAC` / `0x100103DB4` | `0x101166` / `0x10116A` |
| `canvasCaptureEnabled` get/set | `0x6AAEC8` / `0x6AAED0` | `0x57D09C` / `0x57D0A0` | `0x100103F88` / `0x100103F90` | `0x10127C` / `0x101280` |
| `clearEnabled` get/set | `0x6AAEDC` / `0x6AAEE4` | `0x57D0A4` / `0x57D0A8` | `0x100103F98` / `0x100103FA0` | `0x101284` / `0x101288` |
| `setResizable` | `0x6AAB98` | `0x57CF70` | `0x100103D50` | `0x101134` |
| `setClearColor` | `0x6AAB90` | `0x57CF6C` | `0x100103D48` | `0x101130` |

四端注册表都把 `resizable` 和 `clearColor` 发布成 setter method，而非可读 property；native
也确实没有对应 getter。`visible` 和 `alphaOpAdd` 的 getter 只原样读回各自字节，setter 只
按 Boolean conversion 后的值覆写字节。

### 两个真正的控制流 consumer

| consumer | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `renderFromPlayer` | `0x6AB204`（读 `0x6AB238`） | `0x57D2CC`（读 `0x57D2EA`） | `0x100104284`（读 `0x1001042B8`） | `0x101680`（读 `0x1016E0`） |
| `clearTargetTexture` | `0x6AB08C`（读 `0x6AB0B8`） | `0x57D184`（读 `0x57D1A0`） | `0x100104130`（读 `0x100104150`） | `0x10149C`（读 `0x1014EA`） |

共同伪代码为：

```text
renderFromPlayer(adaptor, player, preparedItems):
    if adaptor.canvasCaptureEnabled == false:
        return
    bind(adaptor.targetTexture)
    renderPreparedItems(
        adaptor.targetTexture,
        adaptor.width,
        adaptor.height,
        sourceGetter{adaptor, player},
        targetGetter{adaptor},
        preparedItems,
        player,
        0.5,
        0.5)

clearTargetTexture(adaptor, color):
    if adaptor.clearEnabled == false:
        return
    method = cached GetRenderMethod("FillARGB")
    method.SetParameterColor4B(cachedColorId, color)
    rect = [0, 0, adaptor.targetTexture.width, adaptor.targetTexture.height]
    OperateRect(method, adaptor.targetTexture, adaptor.targetTexture, rect, {})
```

`canvasCaptureEnabled` 的名字容易造成另一种误读：它不 gate 公开的 `captureCanvas` 成员；
它只 gate Player 把 prepared items 画入 target texture 的 helper。直接调用 `captureCanvas`
仍会执行 software row copy 或 GPU texture ownership exchange。`clearEnabled=false` 则在取得
`FillARGB` 方法及其函数局部静态缓存之前就返回。

## `alphaOpAdd` 没有进入 renderer

四端共享 raw renderer 与方法选择器都已单独锁定：

| 目标 | raw renderer | 唯一 selector call | selector | 第四形参准备 |
|---|---:|---:|---:|---|
| Android arm64 | `0x6AB39C` | `0x6AB5C0` | `0x6D8E3C` | `0x6AB5BC: MOV W3,#1` |
| Android armv7 | `0x57D3DC` | `0x57D5B4` | `0x59AA38` | `0x57D5B2: MOVS R3,#1` |
| iOS arm64 | `0x100104450` | `0x100104658` | `0x1001287BC` | `0x100104654: MOV W3,#1` |
| iOS armv7 | `0x101850` | `0x101E7A` | `0x127AF6` | `0x101E78: MOVS R3,#1` |

每个 selector 全局都只有这一个 code xref。其五参数数据流是：

```text
selectMethod(batch,
             item.blendMode & 0xF,
             packedColor,
             true,                         // alphaOpAdd
             item.stencilMaskRef != 0)     // alphaTest
```

selector 又把第四参数保存进 batch key，并原样交给
`PrivateMotionGLL_selectRenderMethod` / `selectAlphaTestRenderMethod`，因此第四参数的身份不是
推测；它就是 `alphaOpAdd`。四端调用点都没有 adaptor pointer，也没有从 adaptor 的
`alphaOpAdd` 字节加载后再常量折叠的路径。设置脚本属性只改变后续 getter 的返回值，不能
改变 method cache key、selector 名称或 blend 行为。

## `clearColor` 与显式清屏参数

给 `clearTargetTexture` 补上 `(D3DAdaptor_layout_guess *self, int color)` 原型后，四端伪代码都
直接把第二形参交给 `SetParameterColor4B`：

- Android arm64 `0x6AB178` 使用 `color`；
- Android armv7 `0x57D228` 使用 `color`；
- iOS arm64 `0x100104210` 使用保存的第二形参；
- iOS armv7 `0x1015E6` 使用 `color`。

函数只从 adaptor 读取 `clearEnabled` 和 `targetTexture`，没有读取 `clearColor`。后者的完整
当前生命周期是 ctor 写 0、`setClearColor` 覆写、dtor 忽略。`Player.clear` 的 D3D 快路径把
本次 fill Variant 转出的颜色直接作为 helper 第二参数，故 `setClearColor(x)` 不会影响随后
以另一个颜色参数执行的 clear。

`method` raw pointer 与 `colorId` 还是两个声明顺序相邻、各有独立 guard 的函数局部静态。
第一次 enabled clear 先初始化 `method = TVPGetRenderManager()->GetRenderMethod("FillARGB")`，
再初始化 `colorId = method->EnumParameterID("color")`；以后直接复用两者。raw pointer 没有
`AddRef`、退出析构或 renderer 变更后的 refresh。任一初始化表达式抛异常时，标准 local-static
guard abort 使该槽在后续调用重试，而已成功发布的前一个槽保持已初始化。`clearEnabled=false`
在两个 guard 之前返回，因此不会预热任一缓存。执行 `OperateRect` 前还会独立再次取得当前
render manager，所以第一次成功清屏调用 manager getter 两次（method 初始化一次、operate
一次），后续调用一次；不能把首次 initializer 与末尾 operate 的 manager lookup 合并。

## 排除其他隐藏 consumer

本轮使用统一 `D3DAdaptor_layout_guess *self` 重编译了四端完整 core surface，并继续检查：

- 15 个公开成员及 factory/ctor/dtor；
- `captureCanvas`、target release、software source-copy map；
- `Player_renderToD3DAdaptor` 与 shared-adaptor wrapper；
- `renderFromPlayer` 建立的 source callback 和 target/reference callback；
- 完整 raw renderer 与唯一 method selector xref；
- NCB wrapper 的 Variant/Boolean conversion 与成员调用。

`renderFromPlayer` 除 capture 字节外只读取 width、height、target texture；target callback 在
64 位只取 adaptor `+0x30`、32 位只取 `+0x24` 的 target texture；Player wrapper 只执行
prepare、projection 和 helper call。未出现 `visible`、`alphaOpAdd`、`resizable` 或
`clearColor` 的隐藏读取。

由此得到以下可观察边界：

- `visible=false` 且 `canvasCaptureEnabled=true` 时仍进入完整 target render；
- 改变 `alphaOpAdd` 只改变脚本 getter，native batch 始终使用 `true`；
- `setResizable` 不比较尺寸、不重建纹理，也不改变 `setSize` 的纯字段写行为；
- `setClearColor` 不预配置 `FillARGB`，也不为无参 clear 提供 fallback；
- `canvasCaptureEnabled=false` 只跳过 render helper，不禁止直接 `captureCanvas`；
- `clearEnabled=false` 只禁止显式 FillARGB helper，不清除或改变保存的 clearColor。

## 源码与恢复 IDB 落点

本地源码保留全部脚本-visible storage，但增加 consumer 注释，明确只有 capture/clear 两个
状态参与控制流；共享 renderer 的 `selectMethod_guess(..., true, ...)` 保持不变并标明常量
来源。清屏路径从每次查询恢复为两个独立 guard 的 function-local static，旧 D3DAdaptor 总
文档中“渲染携带 adaptor alphaOpAdd”的过时描述也已纠正。

四份 recovery IDB 在两个真实门控读点、四个无 consumer setter/getter 边界以及 raw renderer
的 literal-true selector call 写入语义注释和书签后保存。地址只保留在本证据文档与 IDB，
不写回可编译源码注释。

验证：`Web Debug Build` 完整增量构建通过；完整 motionplayer test TU 的 Emscripten
`-fsyntax-only` 通过，仅保留仓库既有 `_tss` 弃用警告；`git diff --check` 通过且两份相关
未跟踪分析文档没有行尾空白。
