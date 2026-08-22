# MotionPlayer `cameraOffset` / `modifyRoot` 四参考二进制对齐（2026-08-12）

## 1. 结论

四份当前参考二进制共同否定了本地原有的 `tTJSVariant`/Point 风格实现。`Motion.Player`
的三个入口应还原为：

```cpp
tTJSVariant Player::getCameraOffset() {
    ncbDictionaryAccessor result;
    result.SetValue(TJS_W("x"), _cameraOffsetX,
                    TJS_MEMBERENSURE, &xHint);
    result.SetValue(TJS_W("y"), _cameraOffsetY,
                    TJS_MEMBERENSURE, &yHint);
    return tTJSVariant(result.GetDispatch(), result.GetDispatch());
}

void Player::setCameraOffset(double x, double y) {
    _cameraOffsetX = static_cast<float>(x);
    _cameraOffsetY = static_cast<float>(y);
}

void Player::modifyRoot() {
    _nodes[0].delta.dirty = true;
}
```

其中 getter 每次都创建一个新 Dictionary；setter 是两个独立的普通 `double -> float`
转换；`modifyRoot` 不检查 deque 是否为空。`_cameraPosition` 是另一条 camera-node/property
状态，和这两个 float 完全无关。

## 2. 宽字符串和注册映射

普通 ASCII 搜索无法覆盖这些 TJS 宽字面量；以下地址来自 UTF-16LE 搜索。

| 目标 | `getCameraOffset` | `setCameraOffset` | `modifyRoot` |
| --- | ---: | ---: | ---: |
| Android ARM64 | `0x14D3F54` | `0x14D3F74` | `0x14D3F94` |
| Android ARMv7 | `0xD8490A` | `0xD8492A` | `0xD8494A` |
| iOS ARM64，Player | `0x10195CE7E` | `0x10195CE5E` | `0x10195CDD4` |
| iOS ARMv7，Player | `0x174F1E2` | `0x174F1C2` | `0x174F138` |

iOS 两端还各有一组同名的 `Motion.EmotePlayer` 宽字符串，不能和 Player registrar 混用。
Player 注册器和最终 callback 映射如下：

| 目标 | Player registrar | getter callback/body | setter callback | modify callback |
| --- | ---: | ---: | ---: | ---: |
| Android ARM64 | `0x6D3DA8` | shared tail `0x6CDE90` | `0x6D6E18` | `0x6CA490` |
| Android ARMv7 | `0x597EC8` | `0x59441C` | `0x599120` | `0x5926DE` |
| iOS ARM64 | `0x1001244F8` | `0x10011F6EC` | `0x100125940` | `0x10011D1DC` |
| iOS ARMv7 | `0x123848` | `0x11E220` | `0x124B36` | `0x11BB72` |

Android ARM64 有一个重要的链接器/优化器形状：`0x67F2D0` 是 EmotePlayer wrapper，先从
Engine 取 inner Player，再 tail-branch 到 `0x6CDE90`；Player NCB callback 则直接进入
`0x6CDE90`。IDA 把共享 tail chunk 归在 wrapper 函数下，所以不能把整个 `0x67F2D0`
错误命名成 Player getter。本轮将其命名为
`EmotePlayer_getCameraOffset_wrapper_guess`，并在共享 tail 入口单独记录 Player 语义。

## 3. getter：数据流、返回对象和生命周期

### 3.1 四端共同顺序

四端共同执行：

1. 用 Dictionary factory 创建一个新 dispatch；
2. 读取 camera-offset x 的 `float` 字段，转成 TJS real，按键 `x` 写入；
3. 读取 y 的 `float` 字段，转成 TJS real，按键 `y` 写入；
4. 两次属性写都带 `0x200`，即 `TJS_MEMBERENSURE`，并使用各自的进程级 member-hint；
5. 用同一个 Dictionary dispatch 构造返回 Variant 的 object 与 objectthis；
6. 在返回 Variant 建立拥有权后，释放 accessor/factory 留下的原始引用。

因此返回值有这些可观察性质：

- 每次调用的对象身份不同；
- 恰好写入 `x`、`y` 两个成员；
- 两个值的 TJS 类型都是 real，而不是 integer；
- 可观察数值是已量化 float 再提升到 double 的结果；
- getter 不返回 Point adaptor，也不返回 `_cameraPosition` 中保存的 Variant；
- 没有缓存、复用或 canonicalization。

本地源码把 float lvalue 直接传给 `ncbPropAccessor::SetValue`。这不仅数值等价，也和原生
模板实例一致：四端 property helper 的值参数都指向 Player 内的 float 字段。显式先转成
double 虽然输出数值相同，却会改变模板实例和调用点临时量结构，因此没有采用。

### 3.2 ABI 字段和 helper

| 目标 | Dictionary factory | x 字段 | y 字段 | x/y property helper |
| --- | ---: | ---: | ---: | ---: |
| Android ARM64 | `0x9C6D40` | Player `+144` | `+148` | `0x5A652C` |
| Android ARMv7 | `0x7384A8` | Player `+112` | `+116` | `0x4E52E4` |
| iOS ARM64 | `0x1000A7A38` | Player `+120` | `+124` | `0x10011F804` |
| iOS ARMv7 | `0xA6900` | Player `+96` | `+100` | `0x11E394` |

64 位返回 Variant 为 24 字节，32 位为 12 字节；编译器都通过隐藏 sret 指针返回。对
getter 应理解为源级 `tTJSVariant getCameraOffset()`，不能把反编译器显示出的首个
`retstr` 参数误写进 C++ 声明。

### 3.3 空 factory 边界

原生没有在两个 property helper 前增加“Dictionary 创建失败则返回 Void”的保护。本地
`ncbDictionaryAccessor::SetValue` 同样直接使用其 dispatch。因此不要添加失败回退；正常
运行依赖 Dictionary factory 成功，异常/损坏环境保留原有的直接访问边界。

## 4. setter：C++ 本体和 NCB 参数边界

### 4.1 四端本体

setter 的源级签名确定为：

```cpp
void Player::setCameraOffset(double x, double y);
```

| 目标 | callback | 存储次序 |
| --- | ---: | --- |
| Android ARM64 | `0x6D6E18` | 两次转换后 paired store x/y |
| Android ARMv7 | `0x599120` | 两次转换后依次 store x/y |
| iOS ARM64 | `0x100125940` | 转换/store x，再转换/store y |
| iOS ARMv7 | `0x124B36` | 转换/store x，再转换/store y |

本体没有：

- Variant 或 Point 参数；
- `x`/`y` 属性查找；
- 参数个数和类型检查；
- 相等早退；
- root dirty；
- `_cameraPosition` 写入；
- finite、范围、NaN 或 infinity 检查。

有限值溢出、`NaN`、正负 infinity、正负零都只服从目标平台普通 `double -> float`
转换。getter 随后把已存 float 提升回 TJS real。

### 4.2 Android ARMv7 的完整 NCB 特化链

Player registrar 的 setCameraOffset 项把 native member pointer 交给：

```text
0x5B2794  NCB_PlayerTwoDoubleMethod_createFunction_guess
    -> 0x5B27C8  NCB_PlayerTwoDoubleMethod_allocateFunction_guess
       -> 0x5B2804  NCB_PlayerTwoDoubleMethod_FunctionCtor_guess
          vtable -> 0x5B286C  NCB_PlayerTwoDoubleMethod_FuncCall_guess
```

outer FuncCall 的错误和顺序是：

- `membername != nullptr`：`TJS_E_MEMBERNOTFOUND`（`-1001`）；
- receiver 为空或 Player native-instance 解析失败：`TJS_E_NATIVECLASSCRASH`（`-1008`）；
- `argc < 2`：`TJS_E_BADPARAMCOUNT`（`-1004`）；
- `argc >= 2`：经 `0x5B1088 NCB_getPlayerNativeInstance_guess` 取 Player，再进
  `0x5B292C NCB_PlayerTwoDoubleMethod_invoke_guess`；
- `argc > 2` 的额外参数不读取。

invoke 的精确参数生命周期为：

1. copy-construct `param[0]` 到临时 Variant；
2. 对临时量执行 `tTJSVariant_AsReal`；
3. 立即析构 x 临时量；
4. copy-construct `param[1]` 到复用的临时 Variant；
5. 执行 `tTJSVariant_AsReal`；
6. 立即析构 y 临时量；
7. 仅在两次转换都完成后，用两个 double 调用保存的 native member pointer；
8. 返回 NCB 成功值。

invoke 中虽然还有 argc 缺失时构造 Void 的模板分支，但 outer FuncCall 已经要求
`argc >= 2`，所以这些分支对有效入口是死代码。x 转换抛出时 y 尚未读取；y 转换抛出时
native setter 尚未调用，因此脚本边界上两种转换失败都不会留下部分 float store。

## 5. `modifyRoot`：容器实现和无保护边界

四端都没有 `empty()`、size、root pointer 或 block pointer 检查。Android 构建的 deque
布局让 element zero 简化成根块指针直接加载；iOS libc++ deque 则保留 map/start-index
寻址算式。

| 目标 | Player deque 关键字段 | node stride | dirty 相对 node |
| --- | --- | ---: | ---: |
| Android ARM64 | root pointer `+200` | 已简化为直接 root | `+1584` |
| Android ARMv7 | root pointer `+160` | 已简化为直接 root | `+1344` |
| iOS ARM64 | map `+168`，start `+192` | `2648` | `+1600` |
| iOS ARMv7 | map `+140`，start `+152` | `2228` | `+1312` |

正常 Player 构造链始终建立 root，因此公开方法通常安全；但是空/损坏 deque 的行为仍是
无效访问，而不是无操作。旧本地 `if(!_nodes.empty())` 改变了这个边界，现已移除。

## 6. 本地旧实现的失配

本轮前本地代码有四个相互强化的错误：

1. `getCameraOffset()` 直接返回 `_cameraPosition`，对象身份和数值来源均错误；
2. `setCameraOffset(tTJSVariant)` 先覆盖 `_cameraPosition`，再把参数当 Point 按属性读
   `x/y`；缺成员时还会造成部分更新，而参考实现根本没有这条路径；
3. 为 EmotePlayer 另造了带旧地址的 `setCameraOffsetXY_*` helper，错误地把 Player 和
   EmotePlayer 判断成两套不同参数模型；
4. `modifyRoot()` 对空 deque 静默返回。

同步后：

- Player 声明改成两个 double；NCB 宏自动实例化正确的二参数转换器；
- getter 使用 fresh `ncbDictionaryAccessor` 和共享的 x/y member hints；
- setter 只写两个 float；
- EmotePlayer wrapper 调用同一个 Player setter，保留其独立脚本入口但不复制错误数据流；
- cameraPosition 仍由原有 cameraPosition property 独立持有；
- `modifyRoot` 和既有 `getRootModified_guess` 一样直接索引 element zero；
- 删除这条 vertical 里指向旧 `libkrkr2.so` 的源码地址注释；地址证据只保留在本文。

## 7. 回归覆盖

`tests/unit-tests/plugins/motionplayer-dll.cpp` 新增/加强：

- 正常 root 的 dirty 位先人工清零，再由 `modifyRoot` 置一；
- 连续两次 getter 返回不同 Dictionary 对象；
- 每个 Dictionary 恰好两个成员且包含 `x/y`；
- 初值为 real `0.0/0.0`；
- 任意 double 经 setter 后按 float 量化，再以 real 读回；
- 设置 cameraOffset 不改变 `_cameraPosition`；
- `DBL_MAX -> +float infinity` 和负 infinity 的符号保持；
- NCB 只有一个参数时返回 `TJS_E_BADPARAMCOUNT` 且两个字段不变；
- NCB 三个参数时成功，第三参数被忽略，只使用前两个。

没有通过构造空 `_nodes` 去运行 `modifyRoot`，因为参考边界是 C++ 无效访问，执行这种
用例只会把预期 UB 变成测试进程崩溃；源码和四端反汇编已经直接锁定无 guard 行为。

## 8. IDB 同步

四份 IDB 均已更新并保存：

- getter、setter、modifyRoot 统一命名并补充 prototype；
- getter 的 Dictionary factory、x/y `TJS_MEMBERENSURE`、返回 Variant AddRef/Release
  平衡均加了函数或行注释；
- setter 的 double-to-float 次序、字段 store 和“无 dirty/validation/cameraPosition”边界
  均加注释；
- modifyRoot 的 deque element-zero 算式和无 guard dirty store 已标注；
- Android ARM64 特别保留 EmotePlayer wrapper 与 Player 共享 tail body 的函数边界；
- Android ARMv7 的六段 NCB 两-double Function 模板链已命名、注释并重新反编译；
- 所有改名/类型/注释完成后均强制清除 Hex-Rays cache 并 fresh-decompile；四份 IDB
  `idb_save` 均成功。

## 9. 验证

本轮完成后执行：

- 用 Web `PlayerCore.cpp` 的真实 Emscripten compile command 对完整
  `tests/unit-tests/plugins/motionplayer-dll.cpp` 做 `-fsyntax-only`：成功；唯一输出为仓库
  既有 `_tss` literal-operator deprecated warning；
- `cmake --build out/web/debug --target motionplayer --parallel`：成功；
- `cmake --build out/wasmtime/debug --target motionplayer --parallel`：成功；
- `cmake --build --preset "Web Debug Build" --parallel`：成功链接 `index.html` 与 Wasm，
  并同步 shell prealloc memory；
- `cmake --build out/wasmtime/debug --target krkr2_wasmtime_guest --parallel`：成功链接
  guest Wasm 并完成 exnref exception 转换；
- 上述四个 target 立即各重跑一次：全部 `ninja: no work to do.`；
- `git diff --check`：退出码 0；只有工作区既有 LF/CRLF 提示，无 whitespace error。
