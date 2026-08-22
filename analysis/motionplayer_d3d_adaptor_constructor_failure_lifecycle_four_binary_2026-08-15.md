# Motion.D3DAdaptor 构造、new-expression 回收与失败生命周期（四参考二进制，2026-08-15）

## 1. 结论

本纵切面重新从 `reference/binaries/` 的四个当前参考二进制核对
`D3DAdaptor` 参数构造、NCB factory 和 `Player.draw` 共享实例创建。共同的源级所有权模型是：

- `Window` 是一个手工 `AddRef` / `Release` 的 raw pointer，不是 RAII holder；
- 宽、高和两个中心值在调用 `Window::AddRef` 之前已经写入对象；
- 红黑树/map subobject 也在 AddRef 之前完成空容器构造；
- AddRef 之后才取得当前 render manager，并创建 RGBA target texture；
- target factory 返回值不检查 null，直接写入成员后把构造视为成功；
- AddRef、manager lookup 或 target factory 抛出时，不执行完整 `D3DAdaptor` destructor，
  因此不会补偿已经成功的 Window AddRef；
- 外层 NCB factory 和 shared-adaptor new-expression 会回收尚未发布的对象 storage，但这个
  `operator delete` 不能回收已经泄漏的 Window 引用；
- 成功对象只在构造返回后发布。NCB 写入 `out_native`，shared 路径写入进程级 raw slot。

这修正了本地参数构造原来的可观察顺序：旧 helper 先 AddRef，之后才写宽高/中心；现在参数
constructor 用 member initializer 安装四个整数和 raw Window，随后在 body 中 AddRef 和创建
target。测试辅助 `initialize_guess` 也改为先写四个整数再 AddRef。

## 2. 参数构造的精确顺序

| 阶段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| constructor | `0x6AAEF0` | `0x57D0AC` | `0x100103FA8` | `0x10128C` |
| raw Window store | `0x6AAF34` | `0x57D0E4` | `0x100103FFC` | `0x1012EA` |
| width/height store | `0x6AAF38` | `0x57D0E6` | `0x100104000` | `0x1012EE..0x1012F2` |
| centerX/centerY store | `0x6AAF40` | `0x57D0EA` | `0x100104004` | `0x1012F4..0x1012FA` |
| conditional Window AddRef | `0x6AAF54` | `0x57D0F6` | `0x100104018` | `0x10132E` |
| current-manager lookup | `0x6AAF58` | `0x57D0F8` | `0x10010401C` | `0x101334` |
| `CreateTexture2D` call | `0x6AAF7C` | `0x57D110` | `0x100104040` | `0x101352` |
| target member publication | `0x6AAF80` | `0x57D112` | `0x100104044` | `0x101356` |

四端在这些外部调用之前还共同完成：

1. 四个尺寸/中心字段、独立 dormant int、五个布尔值、clear color 和 target-null 初始化；
2. 空 map header/sentinel 初始化；
3. raw Window pointer 与四个传入整数的保存。

非 null Window 才执行 AddRef；null Window 会跳过 AddRef，但仍继续 manager lookup 和 target
创建。脚本 factory 和 shared draw 都会在调用 constructor 前提供经过验证/取得的非 null
Window；null 行为仍是 native constructor 自身的真实边界。

纹理创建参数四端一致：

```text
CreateTexture2D(null, 0, width, height, RGBA, 0)
```

width/height 没有正数检查。返回 null 也不是构造失败：null 会写入 target 字段，constructor
正常返回，外层随后发布完整对象。只有 C++ 异常会进入 unwind 路径。

## 3. constructor 内部 unwind

| 目标 | constructor-local cleanup | 行为 |
|---|---:|---|
| Android arm64 | `0x6AAF94..0x6AAFA8` | 取得 map root，调用树清理 helper，再 `_Unwind_Resume` |
| Android armv7 | `0x57D11E..0x57D12A` | 主函数边界外冷块调用 map-subobject destructor，再 `_Unwind_Resume` |
| iOS arm64 | `0x100104060` | 独立冷函数清理 map tree，再 `_Unwind_Resume` |
| iOS armv7 | `0x101376`，树清理 `0x10138E` | SjLj selectors 0..2 清空 map tree，再 resume |

四端的显式路径都只处理已经构造的 map subobject。它们没有：

- 调用完整 `D3DAdaptor` destructor；
- Release raw Window；
- Release target texture（发生异常时 target 返回值尚未写入成员）；
- 把 raw Window 槽或其他前缀重新清零。

A32 cleanup 和 NCB/shared factory 一样位于 IDA 主函数边界外，I64 则拆成独立冷函数；只看
constructor 主函数 decompile 会漏掉它们。四端都没有 Window rollback。

因此异常边界为：

- Window AddRef 本身抛出：map subobject 按 ABI 清理；Window callee 已产生多少引用副作用由
  callee 决定，caller 不调用 Release；
- manager lookup 抛出：一次已经成功的 Window AddRef 泄漏；
- `CreateTexture2D` 抛出：同样泄漏 Window AddRef；target 尚未发布；
- `CreateTexture2D` 返回 null：没有异常、没有 rollback，null target 对象会正常发布。

## 4. NCB factory 的外层 new-expression

NCB factory 四端共同保持：验证 Window → 分配 storage → 按 `param[1]..param[4]` 递增顺序
转换整数 → constructor → 成功后写 `out_native`。失败 delete 地址如下：

| 目标 | allocation | ctor | result publication | failure delete |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6AA978` | `0x6AAB44` | `0x6AAB4C` | `0x6AAB78` |
| Android armv7 | `0x57CF08` | `0x57CF34` | `0x57CF3A` | `0x57CF50` |
| iOS arm64 | `0x100103CB0` | `0x100103CFC` | `0x100103D04` | `0x100103D30` |
| iOS armv7 | `0x101072` | `0x1010C2` | `0x1010CC` | `0x10110E` |

A32 的 `0x57CF4C..0x57CF56` 是 IDA 主函数边界之外的冷 landing block；I64 则把相同逻辑
拆成独立 `D3DAdaptor_ncb_createInstance_cleanup_guess`。只检查主函数 decompile 会误判为
“没有 delete”。

这些 landing pad 对整数转换异常和 constructor 异常都回收 raw storage，但不会运行未完成
对象的完整 destructor。故 constructor 内部已经成功的 Window AddRef 仍然泄漏。

## 5. Player shared-adaptor 的外层 new-expression

| 目标 | allocation | owner lookup | ctor | slot publication | failure delete |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x6D3500` | `0x6D3514` | `0x6D3540` | `0x6D3544` | `0x6D3898` |
| Android armv7 | `0x597954` | `0x597966` | `0x597980` | `0x597988` | `0x597B50` |
| iOS arm64 | `0x100123DE8` | `0x100123DFC` | `0x100123E28` | `0x100123E2C` | `0x10012410C` |
| iOS armv7 | `0x1230B4` | `0x1230C6` | `0x1230E4` | `0x1230F8` | `0x1233F8` |

A32 的 delete 同样位于主函数边界外冷块；I64 位于新命名的
`Player_drawCompat_cleanup_guess`。成功 publication 之前都没有 shared-slot store，所以异常后
slot 仍为 null，下次 draw 会重新尝试创建。

I32 SjLj 需要区分 caller 写入的 call-site 和 handler selector：

- `operator new` 使用 call-site 4，映射 selector 3；分配本身抛出时没有 storage 可删；
- owner lookup 使用 call-site 5，映射 selector 4；
- constructor 使用 call-site 6，映射 selector 5；
- handler 的 selector 4/5 都到 `0x1233F8` delete saved storage。

因此 I32 与另外三端一样，owner lookup 或 constructor 异常都会回收外层 raw allocation。

## 6. 对象状态与可重试性

异常发生后的状态分成两层：

```text
process/NCB publication:
    未发生；slot/out_native 保持原值

new-expression storage:
    外层 landing pad 已 operator delete

constructor internal owners:
    map 已清理或仍是编译器已知空状态
    raw Window AddRef 没有补偿，可能泄漏
    target pointer 尚未写入（异常）
```

shared draw 下次会因 slot 仍为 null 而重试，可能每次都新增一次 Window 引用泄漏。NCB
factory 异常则直接向脚本传播，不写 half-constructed native pointer。

## 7. 本轮恢复落地

- 参数 constructor 改为 member-initializer 形式，在 AddRef 前安装 width/height/centers 和 raw
  Window；辅助 initializer 采用相同可观察顺序；
- 保留 raw Window owner，不添加 scope guard/catch/Release rollback；
- 保留单一 `new D3DAdaptor(...)` 表达式，让转换/constructor 异常仍走 new-expression
  deallocation；
- 四份 IDB 新增 AddRef 前置状态、constructor internal unwind、外层 new-expression cleanup
  注释与书签；
- I64 constructor/NCB/shared 三个冷 cleanup 函数和 I32 两个 SjLj handler 使用保守
  `_guess` 名称；
- 修正 I32 shared handler 注释，明确 call-site 到 selector 的减一映射。

## 8. 验证

- `Web Debug Build`：成功重编 `D3DAdaptor.cpp`、静态库并链接最终 Wasm/HTML；
- 完整 `motionplayer-dll.cpp` Emscripten `-fsyntax-only`：通过，仅有仓库既有 `_tss`
  deprecated warning；
- `git diff --check`：通过，仅报告工作树既有 LF/CRLF conversion warning；
- 本纵切面新增/更新文档无 trailing whitespace；
- 四份 recovery IDB：已保存。
