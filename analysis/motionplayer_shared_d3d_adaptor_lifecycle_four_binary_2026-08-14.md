# MotionPlayer process-shared D3DAdaptor 生命周期与 draw 顺序（四参考二进制，2026-08-14）

> 2026-08-16 V160 更正：本文关于 shared adaptor raw pointer、构造/publication、
> selected-target、`setSize -> visible -> render -> capture` 顺序和 cleanup 的结论继续有效；
> 但“shared-D3D 的 `visible` 拥有独立 member hint”已被四端完整 global xref 证明为过时。
> 该 call site 与 SLA assign、accurate SLA 和 calcViewParam 共用同一 `visible` 槽，详见
> `motionplayer_visible_setpos_opacity_hint_family_four_binary_2026-08-16.md`。
>
> 2026-08-17 V183 补充：`Player_draw_guess` 的 `setSize` 也不是无 hint 调用；它是四端同一
> `setSizeMemberHint_guess` 的 10 个直接调用之一，该共享槽横跨 SourceCache bake、SLA
> assign、Player render/materialization 与 draw。完整 xref 拓扑和 ABI 见
> `motionplayer_separate_layer_assign_double_read_set_size_shared_hint_boundary_four_binary_2026-08-17.md`。

## 1. 结论

四个 `reference/binaries/` 的普通 `Player.draw(Layer)` 在 sticky `useD3D=true`
时，共享同一个进程级 `D3DAdaptor *`。它不是函数静态 `unique_ptr`，也不是由
Player、Window 或 NCB wrapper 持有的智能指针：

1. slot 位于零初始化数据区，机器码只做普通 raw load/store；
2. 第一次需要时依次读取主窗口 width、height，分配 D3DAdaptor，再取主 Window
   owner；
3. centerX/centerY 是 signed int32 `/ 2` toward zero；
4. constructor 正常返回之后才把 raw pointer 写入共享 slot；
5. constructor 抛出时 new-expression landing path 释放 allocation，slot 仍为 null，
   下一次 draw 会重试；
6. 四端没有 guard variable、mutex、atomic、`__cxa_atexit` 或任何共享 slot teardown
   xref；成功对象连同 Window 引用、target texture 和 texture map 一直保留到进程结束；
7. 后续 Player 和后续 Window 尺寸变化都复用第一次成功创建时的 owner/dimensions；
8. target Layer 调用顺序固定为 `setSize -> visible=true -> renderFromPlayer ->
   captureCanvas`，之后只清理局部临时对象；没有 Layer `Update`。V246 又以四端 Player
   ctor/dtor 证明 `lastCanvas` 字段本身不存在；这两次 Layer 操作走 TJS dispatch，HRESULT
   都不参与分支；
9. target 若是 `SeparateLayerAdaptor`，原版不取其公开 `targetLayer`。它先交换
   active/retired 红黑树、把 sequence 清零，以无 payload resolver 解析 ordinal 0，依次
   清掉 retired 与 active/private 状态，然后凭局部 `tTJSVariant` 继续上述 TJS 调用。

当前 Web recovery 原先使用函数内 `static std::unique_ptr`，并在
`initialize_guess` 完成前就把对象放入 slot。该结构会注册退出析构，而且初始化抛出后
会留下一个非 null、部分初始化的粘滞对象；两点都与四个参考二进制相反。本轮改成
零初始化 raw process-global slot，并增加 native-shaped allocating constructor，使赋值
发生在 constructor 成功返回之后。

## 2. 共同控制流

四端共同伪代码如下：

```text
if prepareRenderItems() succeeded and player.useD3D:
    adaptor = g_sharedD3DAdaptor
    if adaptor == null:
        width  = MainWindow.getWidth()
        height = MainWindow.getHeight()
        adaptor = new D3DAdaptor(
            MainWindow.getOwnerNoAddRef(),
            width,
            height,
            signed_int32(width)  / 2,
            signed_int32(height) / 2)
        g_sharedD3DAdaptor = adaptor

    selectedTarget = originalTarget
    if originalTarget is SeparateLayerAdaptor:
        originalTarget.active.swap(originalTarget.retired)
        originalTarget.assignSequence = 0
        selectedTarget = originalTarget.resolveLayerOrdinal(0)
        originalTarget.clearRetiredLayers()
        originalTarget.clear() // private target + active tree

    targetAccessor = ncbPropAccessor(selectedTarget)
    ignore targetAccessor.FuncCall(flags=0, "setSize",
                                   hint=&setSizeMemberHint_guess,
                                   args={adaptor.width, adaptor.height},
                                   result=null, objthis=selectedTarget)
    ignore targetAccessor.PropSet(MEMBERENSURE, "visible", Integer(1))
    adaptor.renderFromPlayer(player, preparedMainList)
    adaptor.captureCanvas(selectedTarget)
    release target temporaries
```

width/height 的读取在 allocation 之前；Window owner lookup 在 allocation 之后。
四端都直接使用主窗口全局，没有 null check。destination Layer 的尺寸不参与 shared
adaptor 创建；事实上共享构造 helper 没有 target 参数，target 也不会改变已经发布的
adaptor。

`new D3DAdaptor(...)` 的 publication 是单独的后继 store。allocation failure、owner
lookup 异常或 constructor 异常都到不了 store。constructor 已 AddRef Window、但 texture
factory 随后抛出时，compiler 只清理已构造的 STL map subobject/new allocation；raw Window
字段不是 RAII owner，D3DAdaptor destructor 不会在未完成构造上执行，因此 native 允许这条
异常路径泄漏 Window 引用。Web 的参数化 constructor 保留了这一 raw-owner 边界。

## 3. shared slot 与首次构造地址表

| 目标 | Player draw | shared slot | load | width / height | allocation | owner lookup | signed half | ctor | publish |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Android arm64 | `0x6D3398` | `0x1AB5588` | `0x6D34D4` | `0x6D34E8` / `0x6D34F4` | `0x6D3500` (`0x68`) | `0x6D3514` | `0x6D3520` / `0x6D352C` | `0x6D3540` | `0x6D3544` |
| Android armv7 | `0x597864` | `0x11119F0` | `0x59792C` | `0x59793E` / `0x59794C` | `0x597954` (`0x40`) | `0x597966` | `0x59796A` / `0x59796E` | `0x597980` | `0x597988` |
| iOS arm64 | `0x100123C84` | `0x101B69A28` | `0x100123DBC` | `0x100123DD0` / `0x100123DDC` | `0x100123DE8` (`0x50`) | `0x100123DFC` | `0x100123E08` / `0x100123E14` | `0x100123E28` | `0x100123E2C` |
| iOS armv7 | `0x122F28` | `0x187D6B0` | `0x123080` | `0x12309A` / `0x1230A8` | `0x1230B4` (`0x34`) | `0x1230C6` | `0x1230D2` / `0x1230DC` | `0x1230E4` | `0x1230F8` |

64/32 位之间以及 Android/iOS STL ABI 之间的 allocation size 不同，主要来自
D3DAdaptor 尾部 texture map 的实现布局；shared-slot protocol 与前缀字段语义相同。

### 3.1 signed half 的精确边界

AArch64 使用 `CMP; CINC value,value,LT; ASR #1`，AArch32 使用
`ADD value,value,value,LSR#31; ASRS #1`。二者都等价于 C++11 signed int32
`value / 2`：

| value | center |
|---:|---:|
| `5` | `2` |
| `4` | `2` |
| `-4` | `-2` |
| `-5` | `-2` |
| `INT32_MIN` | `-1073741824` |

旧源码中带单端旧地址名的 shift lambda 已删除，直接表达 `/ 2`。

## 4. slot xref 与退出生命周期

四个 recovery IDB 对 shared slot 的完整 xref 扫描结果为：

- Android arm64：`0x6D34D0/0x6D34D4` load address/value，`0x6D3544` store；
- Android armv7：`0x597928..0x59792C` load sequence，`0x597984..0x597988`
  store sequence；`0x597BC8/0x597BD8` 只是上述 PC-relative 引用的 literal-pool
  words；
- iOS arm64：`0x100123DB8/0x100123DBC` load，`0x100123E2C` store；
- iOS armv7：`0x123076..0x123080` load，`0x1230EE..0x1230F8` store。

所有语义 xref 都属于同一个 `Player_draw_guess`。没有 dtor、module unload、Window
replacement 或 app shutdown 代码读写该 slot。这个事实排除了 `unique_ptr`、静态对象
destructor 和显式 plugin-unload cleanup。

因此成功创建后：

- shared D3DAdaptor destructor 在正常进程生命周期中不执行；
- retained Window owner 不 Release；
- target texture 不 Release；
- software texture map 中后来加入的 intrusive holders 也不清空；
- MainWindow 改尺寸、替换 owner 或创建新 Player 都不触发重建。

## 5. constructor failure 与 publication

| 目标 | constructor-failure delete | 被删除的 allocation |
|---|---:|---|
| Android arm64 | `0x6D3898` | `X20` |
| Android armv7 | `0x597B50` | `R10` |
| iOS arm64 | `0x10012410C` | `X21` |
| iOS armv7 | `0x1233F8` SjLj selectors 4/5 | saved new-expression pointer |

这些 delete 都位于异常 landing path；正常路径没有 shared-adaptor delete。store 总在 ctor
call 之后，所以失败路径不会发布 dangling/partial pointer。

iOS armv7 caller 写入的 call-site 比 handler selector 大一：allocation 的 4 映射 selector
3，不做 delete；owner lookup 的 5 和 constructor 的 6 分别映射 selector 4/5，两者都到
`0x1233F8`。因此 constructor 异常同样会删除未发布 storage，不应直接用 handler 的 case
数字反推 caller call-site。

native constructor 内部先初始化 scalar/Boolean/map sentinel，再写 raw Window、width/
height/center；四个整数全部安装后，非 null Window 执行一次 AddRef，最后创建 RGBA target
texture并存入字段。
AArch64 Android 的 constructor exception tail 只销毁已经构造的 map tree；它不会调用完整
D3DAdaptor destructor，也不会释放 raw Window 字段。这与 allocating-constructor 的 C++
对象生命周期一致。

### 5.1 脚本工厂也是 allocating-constructor

注册给 NCB 的脚本侧 `D3DAdaptor` 工厂与 shared draw 使用同一种对象生命周期边界。四端
都先验证参数 0 是 `Window`，随后调用 `operator new`，再严格按参数 1、2、3、4 的顺序
执行四次 `AsInteger`，最后调用五参数 constructor。只有 constructor 正常返回后才把 native
pointer 写进 NCB result：

| 目标 | factory | allocation | 首次转换 | ctor | result publication | failure delete |
|---|---:|---:|---:|---:|---:|---:|
| Android arm64 | `0x6AA8F8` | `0x6AA978` | `0x6AA97C` | `0x6AAB44` | `0x6AAB4C` | `0x6AAB78` |
| Android armv7 | `0x57CEBC` | `0x57CF08` | `0x57CF10` | `0x57CF34` | `0x57CF3A` | `0x57CF50` |
| iOS arm64 | `0x100103C30` | `0x100103CB4` | `0x100103CC0` | `0x100103CFC` | `0x100103D04` | `0x100103D30` |
| iOS armv7 | `0x100FD4` | `0x101076` | `0x101084` | `0x1010C2` | `0x1010CC` | `0x10110E` |

因此 factory 不能恢复成 `new default-constructor -> initialize_guess -> publish`：任一
numeric conversion 或 texture constructor 抛出时，该写法会绕开 new-expression 的自动
deallocation。当前实现直接写成 `new D3DAdaptor(window, width, height, centerX, centerY)`；
allocation 在参数求值前完成，转换/构造异常走未发布 allocation 的回收路径，成功后才赋给
result。四个 recovery IDB 已同时标注 factory 的上述六个阶段。

## 6. target Variant 选择与 Layer dispatch 顺序

### 6.1 `SeparateLayerAdaptor` 不是普通 targetLayer unwrap

四端在共享 adaptor 已成功取得之后，再对原始参数做第二次 SLA class-id probe。命中时的
控制流如下；Android arm64 把红黑树交换内联，其余三端调用同一个 ABI-specific map-header
swap helper：

| 目标 | active/retired swap | sequence=0 | ordinal-0 resolver | local Variant copy | clear retired | clear private+active |
|---|---:|---:|---:|---:|---:|---:|
| Android arm64 | `0x6D3598..0x6D3834` | `0x6D382C` | `0x6D3844` | `0x6D3850` | `0x6D3860` | `0x6D3868` |
| Android armv7 | `0x5979D4` | `0x5979D8` | `0x5979E2` | `0x5979EA` | `0x5979F6` | `0x5979FC` |
| iOS arm64 | `0x100123E88` | `0x100123E8C` | `0x100123E9C` | `0x100123EA8` | `0x100123EB8` | `0x100123EC0` |
| iOS armv7 | `0x123146` | `0x12314C` | `0x123158` | `0x123166` | `0x12317A` | `0x123188` |

非 SLA 参数才直接复制原始 target Variant：Android arm64 `0x6D3638`、Android armv7
`0x597A42`、iOS arm64 `0x100123F28`、iOS armv7 `0x1231EA`。

这里有一个不能按“更合理行为”修饰掉的边界：`clear()` 会 Invalidate private target，并把
刚解析 ordinal 0 所在的 active payload 复制到临时量、Invalidate、销毁整棵 active tree；
但 Player 已在此之前把 Layer Variant CopyRef 到局部量，随后仍从这个局部 closure 构造
`ncbPropAccessor`。因此 portable recovery 也必须保留“先 Invalidate、后派发”的顺序，不能
把它改成 `sla->getTargetLayer()`，不能为了保持 Layer 有效而省略 `clear()`，也不能把清理
推迟到 capture 之后。

三份非内联 tree-header helper 已在 recovery IDB 统一命名为
`SeparateLayerOrderedMap_swap_guess`：Android armv7 `0x59B6E8`、iOS arm64
`0x100129858`、iOS armv7 `0x128848`。

### 6.2 TJS 调用顺序

| 目标 | `setSize` | `visible=true` | `renderFromPlayer` | `captureCanvas` |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6D36E0` | `0x6D3740` | `0x6D3758` | `0x6D3770` |
| Android armv7 | `0x597AA6` | `0x597ACC` | `0x597AD6` | `0x597AE6` |
| iOS arm64 | `0x100123FB8` | `0x100123FF0` | `0x100124000` | `0x100124018` |
| iOS armv7 | `0x12326E` | `0x1232A6` | `0x1232B4` | `0x1232CC` |

`setSize` 无 positive-size guard，直接把 shared adaptor 保存的两个 signed int32 包装成
TJS Integer 参数，严格按 width、height 排列。它不是 native `tTJSNI_Layer::SetSize`；
四端都对 selected Variant 的 dispatch 执行
`FuncCall(flags=0,"setSize",hint=&setSizeMemberHint_guess,result=null,argc=2,argv,objthis=selectedTarget)`
并忽略 HRESULT。该 hint 与 SourceCache bake、SLA assign、Player render/materialization
路径复用，是每端同一共享 word 的 10 个直接调用之一。`visible=true` 同样是
`PropSet(TJS_MEMBERENSURE)`，并复用 SLA assign、accurate
SLA 与 calcViewParam 的进程级 `visible` member hint；它写的是 selected Layer 属性而不是
D3DAdaptor 的 `visible` byte，HRESULT 也不参与分支。
四端 capture 后只做 Variant/dispatch/vector cleanup；不存在下面这些旧 Web side effect：

- render 先于 target resize/visible；
- width/height 必须同时大于零才 resize；
- `targetLayer->Update(false)`；
- `_lastCanvas = target`；
- 每帧 `sharedAdaptor->setVisible(true)`。

本轮按四端顺序删除/重排了这些行为。

## 7. 反直觉但四端一致的 canvasCaptureEnabled gate

D3DAdaptor Boolean prefix 在四端都为：

| offset | property | ctor default |
|---:|---|---:|
| `+0x14` | `visible` | `false` |
| `+0x15` | `canvasCaptureEnabled` | `false` |
| `+0x16` | `clearEnabled` | `true` |
| `+0x17` | `resizable` | `false` |
| `+0x18` | `alphaOpAdd` | `false` |

`D3DAdaptor_renderFromPlayer_guess` 的第一条语义检查却固定读取 `+0x15`：

| 目标 | gate |
|---|---:|
| Android arm64 | `0x6AB238` |
| Android armv7 | `0x57D2EA` |
| iOS arm64 | `0x1001042B8` |
| iOS armv7 | `0x1016E0` |

shared draw 没有把 `canvasCaptureEnabled` 改成 true，也没有把 adaptor `visible` 改成
true。因此 fresh process 的 shared fallback 会调用 `renderFromPlayer`，但该调用立刻
返回，不会把 prepared items 提交到 target texture；随后仍然执行 `captureCanvas`。
捕获的是 target texture 当时已有的内容，不能假设一定清零。这里没有按“看起来更合理”
添加隐式 enable。

## 8. 并发边界

slot load/store 是普通非原子指令，没有 static-local guard 或锁。若两个线程同时观察到
null，它们都可采样窗口、分配和构造；随后两个普通 store 竞争，最后一次 store 留在 slot。
另一对象即使完成本次调用，也失去 process-global owner，之后泄漏。以 C++ memory model
描述这是 data race/UB；以当前四个机器码描述则至少可以确认不存在 native synchronization，
不能在 Web 端擅自用 thread-safe local-static initialization 串行化首次构造。

## 9. 本轮落地

- `D3DAdaptor` 增加接受 raw Window owner 与五个 native ctor 参数的 allocating-
  constructor 路径；原测试辅助 `initialize_guess` 复用同一 raw initializer；
- shared owner 改为 namespace-scope、零初始化、永不析构的 raw pointer；
- 删除 `TVPMainWindow`/owner null guard、旧地址 half lambda 和每帧 adaptor visible 写；
- 保留 `width -> height -> allocation/owner -> ctor -> publish` 数据流；
- target selection 移到 shared construction 之后，共享 constructor helper 删除了无意义的
  target 参数；
- SLA target 恢复为 active/retired swap、payload-free ordinal 0、retired clear、完整
  SLA clear；普通 target 才直接复制原始 Variant；
- 删除 native Layer unwrap/安全失败分支，恢复以共享 `setSizeMemberHint_guess` 无条件执行
  TJS `setSize`，随后按 `visible -> render -> capture` 排列；后续 V160 又把当时误建的 shared-D3D 独立
  `visible` 变量校正为四端真实的跨调用链共享槽；
- 删除 shared path 额外的 `Update(false)` 与 `_lastCanvas` publication；
- 四个 recovery IDB 把 slot 命名为 `g_sharedD3DAdaptor_guess`，标注完整构造、异常、
  property gate、SLA ordinal-0 target rotation 和 target call order，并全部保存；三份
  非内联红黑树交换 helper 也已统一语义命名。
- 后续 constructor-failure 纵切面又补充了 AddRef 前字段 publication、Window rollback 缺失、
  A32/I64 冷 landing 与 I32 call-site/selector 映射；详见
  `motionplayer_d3d_adaptor_constructor_failure_lifecycle_four_binary_2026-08-15.md`。

## 10. 验证

- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` Emscripten `-fsyntax-only` 通过，
  仅有仓库既有 `_tss` deprecated warning；
- `Web Debug Build` 在 SLA target 复原后重编 38 个步骤并成功链接最终 Wasm/HTML；
- 新增 native-shaped D3DAdaptor constructor 的 width/height/center、Window retention、
  Boolean defaults 与 target texture 回归断言；
- `git diff --check` 通过，仅报告工作树既有 LF/CRLF conversion warning。
