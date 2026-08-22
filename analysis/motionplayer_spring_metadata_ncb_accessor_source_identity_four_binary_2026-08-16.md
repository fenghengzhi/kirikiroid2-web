# MotionPlayer Simple/BustChain spring metadata `ncbPropAccessor` 源码身份四参考复原（2026-08-16）

## 结论

本轮重新纵切 `EmoteSpringState` 与 `EmoteBustChainSpring` 两个参数构造函数，纠正 portable
源码和 2026-08-11 文档仍以 `DispatchWrapper`/raw `motionPropGet*` 描述 metadata 读取的
过时结构。Android ARM64、Android ARMv7、iOS ARM64、iOS ARMv7 共同证明：两个构造函数
都先 copy-construct 输入 `tTJSVariant`，再构造一个真实、带 vptr 的 root
`ncbPropAccessor`；源 Variant 在 accessor 接管 dispatch reference 后立即析构，root accessor
持有独立引用到构造尾。

BustChain 的 `length`、`scale_x`、`scale_y` 进一步各自构造一个 sequential nested
`ncbPropAccessor`。每个 nested accessor 只读 index 0/1，随后立刻 Release，再开始下一项；
不是让三个返回 Variant 或三个 nested owner 一直存活到构造尾。

两个构造 family 共享 `gravity`、`scale_x`、`scale_y` 三个 member hint 槽；simple spring
另外拥有 `spring`、`friction` 两槽，BustChain 另有七槽标量 family，并复用 controller-state
全局 `length` hint。

## 构造函数映射

| 参考二进制 | Simple spring ctor | BustChain ctor |
| --- | ---: | ---: |
| Android ARM64 | `0x65F828` | `0x6662D8` |
| Android ARMv7 | `0x55176C` | `0x5554F0` |
| iOS ARM64 | `0x1001A18C4` | `0x1001A6104` |
| iOS ARMv7 | `0x1A099C` | `0x1A5710` |

Simple spring 沿用 `EmoteSpringState_ctor_guess`。本轮把四库旧的
`EmoteBustChainSpring_ctor` 统一更正为 `EmoteBustChainSpring_ctor_guess`。二进制均已
stripped，`_guess` 表示职责得到四端支持，但不声称恢复了作者符号。

## root accessor 的 copy/vptr/source-destroy/release 链

共同源码等价于：

```cpp
ncbPropAccessor object{tTJSVariant(dict)};
```

| 目标 | Simple：copy / vptr / source dtor / root Release | BustChain：copy / vptr / source dtor / root Release |
| --- | --- | --- |
| Android ARM64 | `0x65F890 / 0x65F8A8 / 0x65F8E4 / 0x65F9C8` | `0x666348 / 0x666360 / 0x66639C / 0x666824` |
| Android ARMv7 | `0x5517B2 / 0x5517BC / 0x5517C6 / 0x55187A` | `0x555534 / 0x555540 / 0x55554A / 0x555834` |
| iOS ARM64 | `0x1001A191C / 0x1001A192C / 0x1001A1940 / 0x1001A1A24` | `0x1001A6164 / 0x1001A6174 / 0x1001A6188 / 0x1001A6520` |
| iOS ARMv7 | `0x1A09F0 / 0x1A0A12 / 0x1A0A3A / 0x1A0B48` | `0x1A5782 / 0x1A57A2 / 0x1A57D2 / 0x1A5BAA` |

安装 vptr 后的 `AsObject`/AddRef 与 copied Variant dtor 排除了“临时 Variant 本身一直是
property owner”的解释。后续 typed helper 的 self 都指向 `{vptr,_obj}` accessor，虚调用从
`_obj` 发出，并把同一个 `_obj` 作为 `objthis`。

## hint family 与共享关系

### Simple spring 五槽 family

| slot | member | Simple 使用 | BustChain 使用 |
| ---: | --- | :---: | :---: |
| 0 | `gravity` | 是 | 是 |
| 1 | `spring` | 是 | 否 |
| 2 | `friction` | 是 | 否 |
| 3 | `scale_x` | 是（real） | 是（Variant） |
| 4 | `scale_y` | 是（real） | 是（Variant） |

| 参考二进制 | slot 0 基址 | IDB 表达 |
| --- | ---: | --- |
| Android ARM64 | `0x1AB4E8C` | `g_EmoteSpringMetadataHints_guess` |
| Android ARMv7 | `0x1111424` | `g_EmoteSpringMetadataHints_guess` |
| iOS ARM64 | `0x101B69F3C` | 位于宽泛 `qword_101B69A20` 数据项内，精确位置已加注释 |
| iOS ARMv7 | `0x187D95C` | `g_EmoteSpringMetadataHints_guess` |

四端全量 xref 都显示 `gravity/scale_x/scale_y` 同时进入两个 ctor；`spring/friction` 只进入
simple ctor。共享 hint 不绑定返回类型：同一个 `scale_x` 缓存槽在 simple ctor 的
`GetValue<tjs_real>` 与 BustChain ctor 的 `GetValue<tTJSVariant>` 中复用。

### BustChain 七槽私有 family

| slot | member |
| ---: | --- |
| 0 | `friction_x` |
| 1 | `friction_y` |
| 2 | `b_rate` |
| 3 | `v_bound` |
| 4 | `ud_eft` |
| 5 | `bend_spd` |
| 6 | `bend_vol` |

| 参考二进制 | slot 0 基址 | IDB 表达 |
| --- | ---: | --- |
| Android ARM64 | `0x1AB4EFC` | `g_EmoteBustChainMetadataHints_guess` |
| Android ARMv7 | `0x1111494` | `g_EmoteBustChainMetadataHints_guess` |
| iOS ARM64 | `0x101B69FAC` | 位于宽泛数据项内，精确位置已加注释 |
| iOS ARMv7 | `0x187D9CC` | `g_EmoteBustChainMetadataHints_guess` |

这七槽的四端 xref 只落在 BustChain ctor。`length` 不属于此 family，而是继续复用此前
controller-state 纵切面已恢复的 `controllerLengthHint_guess`：Android ARM64
`0x1AB4ED0`、Android ARMv7 `0x1111468`、iOS ARM64 `0x101B69F80`、iOS ARMv7
`0x187D9A0`。

## typed helper 映射与边界

| helper | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| named real | `0x65FA48` | `0x4C779C` | `0x1000F1760` | `0xEDA64` |
| named integer | `0x6609BC` | `0x496C5C` | `0x1000F17E4` | `0xEDB2C` |
| named Variant | caller 内联 | `0x55218C` | `0x1000F1860` | `0xEDBF0` |
| indexed real | `0x66699C` | `0x4C7734` | `0x1000F2FF8` | `0xEF66C` |

fresh helper/caller 复核共同证明：

1. named 与 indexed 调用 flags 都是 0，named 调用传对应全局 hint；
2. dispatch receiver 与 `objthis` 都是 accessor retained `_obj`；
3. `PropGet`/`PropGetByNum` HRESULT 被忽略；临时 Variant 即使由失败调用写入，仍继续转
   `tjs_real`/`tjs_int` 或 copy 成返回 Variant；
4. 临时 Variant 在转换或 return-value copy 后析构；
5. real helper 返回真正的 TJS `double`，controller 随后执行数值 `double -> float` 收窄，
   不是 raw bits reinterpret；
6. 没有 `HasValue` probe、默认值双读或 `GetArrayCount`。常规 miss 若未写 result，零初始化
   Void 临时值按 TJS 转换得到 0；失败但写值则保留 callee 写入的数据。

## BustChain nested owner 数据流

共同源码结构是：

```text
root.GetValue<Variant>("length", controllerLengthHint)
  -> length accessor -> GetValue<real>(0), GetValue<real>(1) -> Release
root.GetValue<Variant>("scale_x", sharedScaleXHint)
  -> scaleX accessor -> GetValue<real>(0), GetValue<real>(1) -> Release
root.GetValue<Variant>("scale_y", sharedScaleYHint)
  -> scaleY accessor -> GetValue<real>(0), GetValue<real>(1) -> Release
root accessor -> Release at ctor tail
```

正常路径的三个 nested Release 位置：

| 参考二进制 | length | scale_x | scale_y |
| --- | ---: | ---: | ---: |
| Android ARM64 | `0x6665BC` | `0x6666A0` | `0x666784` |
| Android ARMv7 | `0x5556C0` | `0x555730` | `0x5557A0` |
| iOS ARM64 | `0x1001A6358` | `0x1001A63EC` | `0x1001A6480` |
| iOS ARMv7 | `0x1A59E6` | `0x1A5A72` | `0x1A5AFE` |

返回 Variant 在 nested accessor 建成后立即析构。Android ARM64 因该编译器的返回值/按值
参数展开可见额外 copy 与两个临时 dtor；其他 ABI 更直接地复用 sret storage。这是编译器
投影差异，共同源码仍是 `ncbPropAccessor nested{root.GetValue<Variant>(...)}`。

异常路径也按当前 live owner 反向清理：Android ARM64 的 landing blocks 最终进入
`_Unwind_Resume`，iOS ARMv7 显式注册 SjLj frame；若 indexed conversion 抛出，当前 nested
先释放，再释放 root。已经正常结束作用域的前一个 nested 不会在后续异常时再次参与清理。

## portable 源码与回归

本轮更新：

- `EmoteSpring.cpp` 的两个 ctor 直接构造 root `ncbPropAccessor`；
- Simple 五次 scalar read 与 BustChain 八次 scalar read 都恢复成 typed named
  `GetValue`；
- BustChain 的 `length/scale_x/scale_y` 用三个独立块作用域表达顺序 nested owner；
- `MotionDispatch.h`/`RuntimeSupport.cpp` 增加五个 simple/shared hint 和七个 BustChain-only
  hint；`length` 继续复用既有 controller-state 槽；
- `tests/unit-tests/plugins/motionplayer-dll.cpp` 增加 spring metadata named/indexed probe。

probe 的所有 getter 都先写入有效值，再故意返回 `TJS_E_FAIL`。测试核对两个 ctor 的全部
字段、root member 顺序、flags=0、准确 hint 地址、`objthis == receiver`，以及三个 nested
array 的 `{index 0,index 1}` 读取。由此同时锁定 ignored-HRESULT、double→float、共享 hint
和 nested accessor 数据流。

## Recovery IDB 写回

四份 recovery IDB 已完成：

- BustChain ctor 名补 `_guess`；
- Android ARM64、Android ARMv7、iOS ARMv7 的 simple/shared 与 BustChain-only family
  数据命名；iOS ARM64 的两个精确基址加行注释；
- simple ctor、BustChain ctor、两组 hint base、indexed-real helper 的源码身份、共享关系、
  ignored-HRESULT 与 owner-lifetime 注释；
- 两个 constructor 入口书签；
- 两 constructor 加 indexed-real helper 共 12 个函数的强制重新反编译与 readback。

## 验证

完成以下验证：

1. 普通和 `KRKR2_WASMTIME_HEADLESS=1` 两种完整 motionplayer test TU syntax-only 均通过，
   只有仓库既有 `_tss` warning；
2. Web Debug 完整增量构建 `34/34` 成功；
3. Wasmtime Headless Debug 完整增量构建 `66/66` 成功；
4. 两个最终 wasm 均可由 `llvm-objdump -h` 正常解析；
5. `EmoteSpring.cpp` 的旧 `motionPropGet*` 定向扫描为零，编译源码的旧 `libkrkr2`/绝对
   地址扫描为零；
6. 本专题源码、测试、文档和 `plan.md` 的限定 `git diff --check` 通过。
