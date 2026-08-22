# Motion.Player.getLayerNames 四参考二进制复原（2026-08-12）

## 结论

四个当前参考二进制一致证明，`Motion.Player.getLayerNames` 的源级形状应为：

```cpp
tTJSVariant Player::getLayerNames(ttstr filter) {
    auto result = createTJSArrayWithItems_guess();
    for (const auto &entry : nodeLabelMap) {
        if (filter.IsEmpty() || entry.first.IndexOf(filter, 0) >= 0)
            result.items->emplace_back(entry.first);
    }
    return result.value;
}
```

并通过普通的 typed NCB 方法注册：

```cpp
NCB_METHOD(getLayerNames);
```

它不是本地旧实现所描述的“可选第一个参数 raw callback”。脚本调用至少必须提供一个参数；缺参返回 `TJS_E_BADPARAMCOUNT`。传 `Void` 或空 String 时，普通 `Variant -> ttstr` 转换产生空的 `ttstr` 句柄，原生方法因此枚举全部标签。

本轮据此删除了 `getLayerNamesCompat`、`collectLayerNames` 和无参 `getLayerNames()` 旁路，将接口复原为 `getLayerNames(ttstr)`。

## 定位链

普通 IDA string 搜索无法找到该宽字符串。以精确 UTF-16LE 字节 `67 00 65 00 74 00 4c 00 61 00 79 00 65 00 72 00 4e 00 61 00 6d 00 65 00 73 00 00 00` 搜索后，四端定位如下：

| 目标 | UTF-16 字符串 | 注册点 | 原生成员函数 |
|---|---:|---:|---:|
| Android arm64 | `0x14D65F6` | `0x6D5CA8` | `0x6CE4C0` |
| Android armv7 | `0xD85EF4` | `0x59866C` | `0x594798` |
| iOS arm64 | `0x10195CE1C` | `0x100125054` | `0x10011FE88` |
| iOS armv7 | `0x174F180` | `0x1242AC` | `0x11EB7C` |

Android armv7/iOS 两端注册点明确调用一参数 typed method 模板。Android arm64 在 registrar 内联构造相同方法对象；其 vtable `0x1A1DF38` 的 FuncCall 槽指向 `0x6F7178`，同样证明它不是 raw callback。

旧源码注释使用的 Android arm64 `0x6D10E0` 不属于当前参考二进制中的这个函数。该地址来自已经换下的 `libkrkr2.so` 分析，不能继续当作当前四参考基线。

## typed NCB 调用链

| 阶段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| typed FuncCall | `0x6F7178` | `0x5B3834` | `0x100149E8C` | `0x14AF5C` |
| method invoke | `0x6F7294` | `0x5B38F4` | `0x100149F6C` | `0x14AFF0` |
| arg0 `Variant -> ttstr` | `0x6F739C` | `0x5B39A8` | `0x10014A03C` | `0x14B10C` |

四端 FuncCall 的共同边界为：

1. `membername != nullptr` 时走 member-not-found 路径。
2. `objthis == nullptr` 时返回 `TJS_E_INVALIDOBJECT`（`-1008`）。
3. 非空 `result` 在参数数量检查前被清为 Void；这是 `ncbNativeClassMethodBase::doInvokeBase` 的行为。
4. `numparams < 1` 返回 `TJS_E_BADPARAMCOUNT`（`-1004`）。
5. 只转换 `param[0]`；额外参数不被读取或转换。
6. 获取 `Player` native instance 后进入 method invoke。

四端 method invoke 的共同生命周期为：

1. 把 `param[0]` 转为一个拥有引用的临时 `ttstr`。
2. 调用 `Player::getLayerNames(ttstr)`，无条件构造原生返回 Variant。
3. 再 CopyRef 一份返回 Variant。
4. 仅当脚本 `result != nullptr` 时把该副本赋给 result。
5. 逆序析构复制返回值、原生返回值和临时 `ttstr`。

因此 `result == nullptr` 不会短路原生方法：Array 仍被创建、填充并在 wrapper 内正常销毁。旧 raw callback 中的 `if (result) self->collectLayerNames(...)` 跳过了整个调用，这与四端生命周期均不一致。

## 参数转换与边界行为

转换器使用普通的 `tTJSVariant::AsString()` / `ttstr(const tTJSVariant&)` 语义：

| 传入 Variant | 转换结果 | `getLayerNames` 行为 |
|---|---|---|
| 缺参 | 不转换 | `TJS_E_BADPARAMCOUNT` |
| Void | null `ttstr` handle | 枚举全部标签 |
| String `""` | null `ttstr` handle | 枚举全部标签 |
| 非空 String | 同一字符串的拥有引用 | case-sensitive UTF-16 substring 过滤 |
| Integer | 十进制字符串 | 按格式化结果过滤 |
| Real | TJS real 格式化字符串 | 按格式化结果过滤 |
| Object | `TJSObjectToString` | 按对象字符串过滤；对象转换异常原样传播 |
| Octet | 转换异常 | 原生方法不执行 |

空 String 与 Void 最终相同不是 wrapper 特判：TJS 的空字符串由 `TJSAllocVariantString` 规范化为 null string pointer，而 `ttstr::IsEmpty()` 精确检查内部 `Ptr == nullptr`。

原生函数判断的是传入 `ttstr` 的内部 handle 是否为零，然后才决定是否调用 `IndexOf(key, filter, 0)`。过滤是从位置 0 开始、大小写敏感的 UTF-16 code-unit substring 匹配。找不到时返回负值，该项被跳过。

## label map 数据流

查询读取构建节点树时形成的 `NodeLabelMap`：

```cpp
std::map<ttstr, int, ttstr_utf16_less>
```

共同源级行为：

- 迭代顺序是 RB-tree 的中序顺序，即 `ttstr_utf16_less` 的 UTF-16 code-unit 升序。
- 只读取 key（PSB 原始 `label`）；mapped node index 完全不参与结果。
- 同名标签已在 map 构建阶段合并，所以一个 raw label 最多产生一个结果。
- 不检查 node type、visible、active、mesh、draw flag。
- 不沿 type-3/type-4 child-player Variant 递归。
- 每次调用创建新的 TJS Array，即使 map 为空也返回一个新的空 Array，而不是 Void。

STL 实现造成的 ABI 布局差异如下。这些只能用于 IDB 结构标注，不能硬编码进共用 C++：

| 目标 | 树实现特征 | root | header/end | key in node |
|---|---|---:|---:|---:|
| Android arm64 | libstdc++ `_Rb_tree_increment` helper | adjusted map `+0x30` | `+0x20` | `+0x20` |
| Android armv7 | libstdc++ `_Rb_tree_increment` helper | `+0x18` | `+0x10` | `+0x10` |
| iOS arm64 | libc++ successor inline | `+0x18` | `+0x20` | `+0x1C` |
| iOS armv7 | libc++ successor inline | `+0x0C` | `+0x10` | `+0x10` |

Android arm64 反编译里的 `a1` 已是 member pointer 调整后的 map 地址，不能直接把这些偏移解释为完整 `Player` 的源字段偏移。

## 返回 Array 与内部容器

Array 构造 helper：

| 目标 | create Array helper | String push |
|---|---:|---:|
| Android arm64 | `0x702098` | 在 `0x6CE4C0` 内联 |
| Android armv7 | `0x5BAA70` | `0x4EA126` |
| iOS arm64 | `0x10029FF58` | `0x1001024C4` |
| iOS armv7 | `0x2A4A80` | `0xFF7E0` |

四端 create helper 均执行同一源级工作：

1. `TJSCreateArrayObject()`。
2. 把 dispatch/objthis 放入拥有对象引用的 `tTJSVariant`。
3. 以 Array class id `2` 请求 native instance。
4. 保存一个借用的 `tTJSArrayNI::Items` 指针；其生命周期由拥有 Array 的 Variant 保证。

项目核心 `tTJSArrayNI::Items` 的声明确认为 `std::deque<tTJSVariant>`。String append 会构造 type tag `2` 的 Variant，复制 map-key 的 string handle 并原子 AddRef。deque 的实现细节是平台 STL 的 ABI 差异：

| 目标 | Variant stride | deque node/block |
|---|---:|---:|
| Android arm64 | 20 bytes | libstdc++ 分配 `0x1F4=500` bytes，即 25 项 |
| Android armv7 | 12 bytes | libstdc++ 分配 `0x1F8=504` bytes，即 42 项 |
| iOS arm64 | 20 bytes | libc++ 每块 `0xCC=204` 项 |
| iOS armv7 | 12 bytes | libc++ 每块 `0x155=341` 项 |

这也解释了为何 Android arm64 native body 显得特别大：字符串 `emplace_back` 和 libstdc++ deque 扩容被完整内联，而其余目标更多调用独立 helper。

## Android arm64 IDB 边界修复

当前 Android arm64 IDB 原先把 `0x6CE4C0..0x6CE6FC` 错误并入前一个 `Player_getProcessedMeshVerticesNum_guess@0x6CE3F8`，导致后者大小为 `0x304`，且没有独立的 `getLayerNames` 函数。

本轮依据 `0x6CE4C0` 的独立 AArch64 prologue 与下一函数 `0x6CE6FC` 的边界执行了：

- `0x6CE3F8..0x6CE4C0`：重新逐指令定义为 `Player_getProcessedMeshVerticesNum_guess`，大小 `0xC8`。
- `0x6CE4C0..0x6CE6FC`：重新逐指令定义为 `Player_getLayerNames_guess`，大小 `0x23C`。

修复后 fresh decompile 验证：

- 前一函数重新得到“读取本地计数、递归访问 child player、返回累加值”的完整伪代码，不再是 `JUMPOUT`。
- 后一函数得到完整 Array 构造、map 遍历、filter、deque append 和返回析构链。

## IDB 改进

四个 IDB 均写入并保存了以下 `_guess` 名称：

- `Player_getLayerNames_guess`
- `NCB_Player_getLayerNames_FuncCall_guess`
- `NCB_Player_getLayerNames_invoke_guess`
- `NCB_Player_getLayerNames_convertArg0ToTtstr_guess`
- `createTJSArrayWithItems_guess`
- 三个非内联目标的 `TJSArrayItems_pushBackString_guess`

函数与注册点注释记录了 typed arity、result-null 生命周期、参数转换、map/deque 布局及 STL ABI 差异。保存结果：

- `Kirikiroid2_1.3.9_Android_arm64-v8a.so.i64`
- `Kirikiroid2_1.3.9_Android_armabi-v7a.so.i64`
- `Kirikiroid2_1.3.9_iOS_arm64.i64`
- `Kirikiroid2_1.3.9_iOS_armv7.i64`

## 本地实现与测试修正

修改文件：

- `cpp/plugins/motionplayer/Player.h`
- `cpp/plugins/motionplayer/PlayerLayerQuery.cpp`
- `cpp/plugins/motionplayer/main.cpp`
- `tests/unit-tests/plugins/motionplayer-dll.cpp`

新增/更新测试覆盖：

- C++ 直接调用必须显式传 `ttstr()`。
- 非空 substring filter 的每个返回 key 都实际包含 filter。
- 脚本缺参返回 `TJS_E_BADPARAMCOUNT`，且普通 NCB 已把 result 清为 Void。
- Void 与空 String 都枚举全部标签，并各自返回 fresh Array。
- 额外 Octet 参数被忽略，不触发转换。
- Octet 作为第一个参数触发普通 Variant-to-string 异常。
- `result == nullptr` 时调用仍成功并完整执行原生返回对象生命周期。
- 既有“label map 合并重复标签，而 getter list 遍历所有非根节点”行为继续验证。

## 验证

2026-08-12 在当前工作树完成了以下验证：

- `cmake --build out/web/debug --target motionplayer -j 10`：成功；重新编译 motionplayer 的 30 个对象并链接静态库。
- `cmake --build out/web/debug -j 10`：成功；完成 Web Debug 的 `index.html`、`index.js`、`index.wasm`、资源复制与 shell-memory 同步。
- `tests/unit-tests/plugins/motionplayer-dll.cpp`：使用 `out/web/debug` 中 `PlayerLayerQuery.cpp` 的真实 Emscripten 编译参数执行完整 `-fsyntax-only`，成功。
- `cmake --build out/wasmtime/debug --target motionplayer -j 10`：成功。
- `cmake --build out/wasmtime/debug --target krkr2_wasmtime_guest -j 10`：成功；包括最终 guest 链接和 exnref 转换。

上述构建只出现项目已有警告：`_tss` 字面量弃用、`imagepacker.h` 的 `nodiscard`、以及 Emscripten pthread memory-growth/JSPI 提示；没有本轮修改引入的新错误。

另尝试使用已安装的 Visual Studio 2022 Build Tools 配置 `out/windows/debug`，以便实际运行 Catch2 单元测试。配置在 vcpkg overlay 构建 `cocos2dx:x64-windows` 时提前失败：上游 cocos2d-x `platform/win32/CCStdC.h` 对 `snprintf` 的宏定义与 MSVC 14.44 标准库声明冲突，报 `C1189`。失败发生在项目源码开始编译之前，故不能把它解释为本轮 motionplayer 修改的测试失败，但本轮也因此不能声称 Catch2 运行时测试已经通过。

最后执行接口残留扫描和 `git diff --check`，用于确认旧的无参/compat 路径已清除且补丁没有空白错误；结果记录在本轮交付说明中。

## 证据置信度

- typed 方法、至少一个参数、Void/空 String 行为：四端直接证据，高。
- map 只取 key、中序遍历、substring 过滤、fresh Array：四端直接证据，高。
- result-null 仍构造/销毁返回值：四端 invoke helper 直接证据，高。
- `std::map` / `std::deque` 源类型：反编译布局与项目核心声明互证，高。
- 原始未剥离 C++ 符号拼写不可得；IDB 名称因此遵循项目规则保留 `_guess`。
