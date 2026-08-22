# MotionPlayer `doAlphaMaskOperation` Variant ABI、脚本派发与生命周期四参考复核（2026-08-16）

## 1. 结论

8 月 11 日对裁剪、mode/op 和像素公式的复原是正确的，但当时把 Layer 的可观察
TJS 调用链过早降成了原生 `tTJSNI_BaseLayer` 调用。本轮重新从四个参考二进制的
完整 callee、三个生产调用点和严格 Variant-to-Layer helper 对照后，恢复出以下
共同源级结构：

1. 第 1、4 参数是**按值传入的 `tTJSVariant`**，不是裸 `iTJSDispatch2 *`。
2. callee 再复制一次目标 Variant，并通过 `ncbPropAccessor` 的 `AsObject()` 获得一份
   独立 AddRef；这个目标对象持有一直活到函数尾。
3. `clipLeft`、`clipTop`、`clipWidth`、`clipHeight` 按该顺序通过属性 accessor 读取；
   每项先以 `TJS_MEMBERMUSTEXIST` 探测，再以 flags 0 取值，缺失时沿 accessor 的
   Integer 0 默认行为。
4. 裁剪和空交集判断发生在任何源 Layer native-instance 转换之前。空交集绝不检查
   源 Variant；`op == 1` 时通过 retained destination dispatch 调脚本 `fillRect`。
5. 非空交集先严格转换源 Variant，再严格转换目标 Variant。转换 helper 要求 Variant
   类型为 Object，只读取 Variant 保存的 Object dispatch，并只查询一次 Layer native
   instance；没有 ObjThis 重试、空对象恢复或兼容 adaptor。
6. 四个外部清零条带也是脚本可见的 `fillRect`；非空路径的尾调用是脚本可见的
   `update`。它们使用同一个非空 result Variant，并把 retained destination 作为
   `objthis`。
7. callee 是源级 `void`。调用者不消费返回值；32 位伪返回来自栈保护差值，64 位尾部
   伪返回来自最后一次 Release 的寄存器残值。

当前源码已据此恢复。内部 compositor helper 也已删除端口专用的
`motionPath/frameTime/dstNode/srcNode` trace sidecar，收敛为参考可见的 11 参数 `void`
调用边界。这样不仅恢复了调用链结构，也避免为日志额外计算 requested rect 而改变极值
输入的整数边界。函数名仍保留 `_guess`，不把地址关联误当成原始 C++ 拼写证据。

## 2. 四参考函数与三个生产调用点

| 目标 | `doAlphaMaskOperation` | common caller | Canvas caller | accurate caller |
| --- | ---: | ---: | ---: | ---: |
| Android arm64 | `0x6AC4E4` | `0x6C3734` | `0x6C5770` | `0x6C8144` |
| Android armv7 | `0x57E1E8` | `0x58D786` | `0x58EFD2` | `0x591260` |
| iOS arm64 | `0x100104E68` | `0x100117890` | `0x100119338` | `0x10011B7F0` |
| iOS armv7 | `0x10243C` | `0x11532C` | `0x117924` | `0x119D1A` |

十二个调用点具有相同的栈对象形状：调用前分别 copy-construct destination/source
`tTJSVariant` 临时对象，把临时对象地址作为第 1/4 参数传给 callee；void 调用后按
source、destination 的顺序析构两个临时对象。调用点没有 `Type()==Object` 筛选、
裸 dispatch 抽取或 null 恢复。这同时解释了为什么空交集能接受一个完全不是 Object
的 source Variant：source 的按值 copy 会发生，但其严格 Layer 转换不会发生。

## 3. 目标对象的两层所有权与属性读取

| 目标 | callee 内 destination owner acquisition | 第一项 `clipLeft` 读取 |
| --- | ---: | ---: |
| Android arm64 | copy `0x6AC538` | `0x6AC5BC` |
| Android armv7 | `AsObject` `0x57E21E` | `0x57E234` |
| iOS arm64 | `AsObject` `0x100104EDC` | `0x100104EFC` |
| iOS armv7 | `AsObject` `0x1024C4` | `0x1024E2` |

源级生命周期可表达为：

```cpp
void doAlphaMaskOperation(tTJSVariant dst, ..., tTJSVariant src, ...) {
    tTJSVariant destinationCopy(dst);
    ncbPropAccessor destination(destinationCopy); // AsObject() retains dispatch
    destinationCopy.Clear();                     // accessor owns independently

    const tjs_int clipLeft   = destination.getIntValue(TJS_W("clipLeft"));
    const tjs_int clipTop    = destination.getIntValue(TJS_W("clipTop"));
    const tjs_int clipWidth  = destination.getIntValue(TJS_W("clipWidth"));
    const tjs_int clipHeight = destination.getIntValue(TJS_W("clipHeight"));
    tTJSVariant result;
    // ...
} // result dies, then accessor releases retained destination
```

这里存在三层不同的持有期：caller 的 destination 参数临时、callee 的按值 destination
参数，以及 accessor 的独立 dispatch AddRef。不能把 accessor 简化为对参数 Variant
内部裸指针的借用，否则 early return、脚本回调重入和函数尾的 Release 顺序都会变化。

`getIntValue` 的访问模式也是可观察边界：四个属性严格依照 left/top/width/height 顺序，
每个属性执行一次 `TJS_MEMBERMUSTEXIST` probe 和一次 flags 0 read，`objthis` 都是 retained
destination。源码测试把这 8 次 `PropGet` 固定为契约。

## 4. 空交集：lazy source 与脚本 `fillRect`

| 目标 | 空交集 `op == 1` 的 `fillRect` 派发 |
| --- | ---: |
| Android arm64 | `0x6AC928` |
| Android armv7 | `0x57E342` |
| iOS arm64 | `0x100105140` |
| iOS armv7 | `0x102612` |

裁剪得到 `width <= 0 || height <= 0` 后：

- source Variant 不做类型检查，不取 Object，不查询 native Layer；
- `op != 1` 直接返回；
- `op == 1` 通过 retained destination 的 `FuncCall` 调 `fillRect`；
- 五个参数全部是 Integer Variant，顺序为
  `[clipLeft, clipTop, clipWidth, clipHeight, 0]`；
- `objthis` 是 retained destination，result 参数非空；
- 调用之后走函数作用域清理，不执行 `update`。

这条路径说明“先把两个 Variant 解析成 native layer，再做裁剪”的实现即使正常输入像素
结果相同，也不具备参考的类型错误边界和脚本可观察调用链。

## 5. 非空交集：严格转换和自然失败

| 目标 | source strict conversion | destination strict conversion | Variant-to-Layer helper |
| --- | ---: | ---: | ---: |
| Android arm64 | `0x6AC7BC` | `0x6AC7C8` | `0xA7959C` |
| Android armv7 | `0x57E37A` | `0x57E382` | `0x79AFCE` |
| iOS arm64 | `0x100104FD4` | `0x100104FE0` | `0x10035FF10` |
| iOS armv7 | `0x10264E` | `0x10265A` | `0x36366C` |

严格 helper 的四份实现都先强制 Variant 为 Object；非 Object 进入 Variant conversion
error。随后只使用 Variant 的 Object dispatch 查询 Layer native instance 一次。不存在：

- `Type()!=Object` 时返回 null；
- Object dispatch 失败后尝试 ObjThis；
- 查不到 native instance 时返回友好失败；
- native layer 或 main image 为 null 时静默跳过；
- 为兼容端口对象而制造 adaptor。

因此当前实现只把裁剪和空交集放在转换之前；一旦 overlap 非空，就依次严格转换 source、
destination，随后信任 native layer 与 main-image 指针，让不满足前置条件的状态沿引擎既有
异常/自然失败路径暴露。

## 6. `fillRect`/`update` 的 TJS 调用 ABI

非空 `maskMode` 为 0/1 且 `op == 1` 时，overlap 外四个非空条带各产生一次脚本
`fillRect(left, top, width, height, 0)`。每个参数都是单独的 Integer Variant；成员名是
`fillRect`，`objthis` 为 retained destination，所有调用共用函数作用域的 result Variant。

无论 mode/op 是否受支持，只要 overlap 非空，尾部都会产生一次脚本
`update(dstX, dstY, width, height)`，四个参数同样全是 Integer Variant，且继续复用同一
result Variant：

| 目标 | 非空尾部 `update` |
| --- | ---: |
| Android arm64 | `0x6AD97C` |
| Android armv7 | `0x57F03A` |
| iOS arm64 | `0x1001061C4` |
| iOS armv7 | `0x10359A` |

这不是 `tTJSNI_BaseLayer::FillRect`/`UpdateByScript` 的等价优化：脚本层 override、参数
Variant 类型、member hint、result 写入、`objthis` 和回调重入都可观察。8 月 11 日文档
第 5～6 节关于 native Layer API 的实施方案因此被本轮结论取代；该文档的裁剪和像素
数学部分不受影响。

## 7. 返回值与析构顺序

| 目标 | result Variant 析构 | retained destination Release |
| --- | ---: | ---: |
| Android arm64 | `0x6AD9A4` | `0x6AD9C0..0x6AD9CC` |
| Android armv7 | `0x57F054` | `0x57F06A..0x57F070` |
| iOS arm64 | `0x1001061E8` | `0x100106204..0x100106210` |
| iOS armv7 | `0x1035B4` | `0x1035C0..0x1035CA` |

正常非空尾部先析构共用 result Variant，再释放 accessor 独立持有的目标 dispatch；按值
source/destination 参数由 ABI 生成的 callee/caller 清理继续收尾。三个 caller 都不读取
函数结果。四份控制流综合证明源级函数是 `void`，不能把反编译器在不同 ABI 尾部选择的
最后一个寄存器值提升为有意义返回值。

## 8. 源码修正与回归约束

本轮修正覆盖：

- `PlayerRenderInternal.h/.cpp`：helper 改为 `void` 和两个按值 Variant 参数；恢复目标
  accessor 独立所有权、顺序属性读取、lazy strict conversion、脚本 `fillRect/update`
  及共用 result Variant；移除 null/native/image 的友好恢复。
- `PlayerRenderExecute.cpp`、`PlayerRenderTargets.cpp`：common、Canvas、accurate 三条生产
  路径直接传 Variant，不再在 caller 提前抽取裸 dispatch。
- `main.cpp`：Motion namespace 注册 wrapper 接受 11 个参考形状参数，其中 Layer 参数
  为按值 Variant，返回 `void`。
- 内部 compositor 删除四个端口 trace 参数与函数尾日志，三个生产 caller 和 namespace
  wrapper 都恢复为十二个参考调用点共同证明的 11 参数边界；日志专用 requested rect
  计算也随之删除，不再给无日志参考路径添加额外 signed-addition 边界。
- `tests/unit-tests/plugins/motionplayer-dll.cpp`：新增空交集测试，以 Integer 99 作为非法
  source，证明该路径不触发严格转换；同时核对 8 次属性访问和一次五 Integer 参数的
  `fillRect` 派发。

验证结果：

- 普通 Emscripten 单 TU 语法检查通过；
- `KRKR2_WASMTIME_HEADLESS=1` 单 TU 语法检查通过；
- Web Debug `motionplayer` 静态库增量构建通过；
- Wasmtime Headless Debug `motionplayer` 静态库增量构建通过；
- 删除 trace sidecar 后，Web Debug 完整目标重新链接成功并同步 shell prealloc；输出只有
  仓库既有的 pthread/memory-growth、JSPI 和 JS-library warnings；
- 当前 CMake 配置没有暴露 unit-test 可执行 target，因此新增用例获得普通/headless
  两套编译覆盖，但不把它声称为已运行的 runtime test；
- 四个 recovery IDB 均已写入 callee、owner acquisition、首个属性读取、lazy strict
  conversion、empty fill、nonempty update 与十二个 caller call-site 注释，并保存成功。

IDB 中所有恢复命名继续保留 `_guess`；地址只记录在本分析文档，不写入编译源码注释。
