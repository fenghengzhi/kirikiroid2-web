# TextRender `cgmode` 的 Variant 布尔转换四参考联合分析（2026-08-30）

## 现象与调用链

游戏 `KRkr高压_千恋万花.zip` 在 Web 调试版标题界面点击「鉴赏模式」后，
`cgmode.ks:8` 的 `[dialog name=cgmode]` 初始化失败：

```text
Cannot convert the variable type ((int)0 to Object)
at textrender.tjs(1)[(function) setDefaultFont]
```

Playwright 复现证明输入命中正确；TJS 寄存器中 `setDefaultFont` 的字典实参也是
`Dictionary`。原生诊断进一步证明 `TextRenderBase::setDefault` 收到
`tvtObject`，`ncbPropAccessor` 的 `AsObject()` 成功。异常发生在读取 `shadow`
以后、把其 `tvtInteger(0)` 转成 C++ `bool` 时。

## 本轮 IDB 状态

原 Android armv7 `.i64` 无法由当前 IDALib 打开，工作进程立即断开。增加宿主机
内存后仍复现，且无 OOM/内核杀进程记录。删除旧 IDB 后从原始 `.so` 重新自动分析：

- 自动分析约 18 秒完成，Hex-Rays 初始化成功；
- 保存得到新的 `.i64`，大小 `385527773` 字节；
- 首次保存后的 SHA-256：`6b9ac4491899f33e4fd25423483713cc5a431bfc6a3cc931167599a3af0b709f`；
- 完成重开、反编译验证后的最终 SHA-256：`16d454e06a8f9b714471d382a2f01670851e579dd30f2da170674b49c55f454d`；
- 按新 `.i64` 路径重开成功，并能反编译 `0x4E01B4`。

旧 IDB 已移入系统回收站，原始 `.so` 未改动。

## 四参考新定位

| 参考目标 | `TextRenderBase::setDefault` | `Variant -> bool` | NCB `void(Variant)` invoker |
|---|---:|---:|---:|
| Android arm64-v8a | `0x59E288` | 内联于 `setDefault` | `0x5A75C0` |
| Android armeabi-v7a | `0x4E01B4` | `0x496CC4` | `0x4E5EB0` |
| iOS arm64 | `0x1003F6204` | `0x100037640` | `0x1003FEF88` |
| iOS armv7 | `0x3DD694` | `0x3589C` | `0x3E6060` |

四个 NCB invoker 都先检查 `numparams >= 1`，再从 `params[0]` 复制
`tTJSVariant` 并调用成员函数；没有把缺参或整数零自动替换成字典的分支。

## 共同伪代码

四端 `setDefault` 在 `shadow`、`bold`、`edge` 等布尔键上的共同语义为：

```cpp
if (dict.PropGet(TJS_MEMBERMUSTEXIST, key, nullptr, &work, dict) >= 0)
    destination = VariantTruthy(work);
```

`VariantTruthy` 的共同语义为：

```cpp
switch (value.Type()) {
case tvtObject:  return value.Object != nullptr;
case tvtString:  return value.AsInteger() != 0;
case tvtOctet:   return value.Octet != nullptr;
case tvtInteger: return value.Integer != 0;
case tvtReal:    return value.Real != 0.0;
default:         return false;
}
```

这逐项对应本地 `tTJSVariant::operator bool()`，不对应
`operator iTJSDispatch2*()`/`AsObject()`。

## 平台差异

- Android arm64 把布尔 truthiness 的类型 `switch` 内联进 `setDefault`；
- Android armv7、iOS arm64、iOS armv7 分别调用上述三个 helper；
- 32/64 位 Variant 布局、对象/字符串/Octet 指针宽度及浮点比较指令不同；
- 共同源码行为一致，没有平台特有的 `int 0 -> Object` 转换或容错。

## Web 运行时分界证据

临时诊断日志按顺序得到：

```text
TRSETDEFAULT entry type=1
TRSETDEFAULT object holder ready
TRSETDEFAULT before shadow
TRSETDEFAULT raw shadow result=0 type=4
TRSETDEFAULT after shadow get has=true type=4
Cannot convert the variable type ((int)0 to Object)
```

其中 `1 == tvtObject`、`4 == tvtInteger`。带
`TJS_MEMBERMUSTEXIST | TJS_IGNOREPROP` 的原始读取也返回 `TJS_S_OK` 和整数零，
因此字典、成员查找与 NCB 传参均正常；失败边界就是本地 `(bool)v`。

## 本地比较与修复决策

`tTJSVariant` 同时声明 `operator bool()` 与 `operator iTJSDispatch2*()`。本地
TextRender 逆向移植代码使用 C 风格 `(bool)v`，而核心和其它已验证代码普遍用
`v.operator bool()`。当前 Web/Clang 产物在该调用点选择了对象指针转换，导致
整数零进入 `AsObject()`。

修复应把 TextRender 所有 Variant truthiness 调用点统一写成
`v.operator bool()`。这不是改变原版语义，而是消除 C++ 转换函数重载歧义，明确生成
四参考已经证明的逐类型 truthiness。临时 `TRSETDEFAULT` 日志和 raw-shadow 探针在
正式修复中全部删除。

## `render` RAW Process 的同源调用点

首次正式回归中，`setDefaultFont` 已通过，但稍后的 `textrender.tjs::render` 又在第 5
参数为整数零时抛出同一错误。本地 RAW Process 还保留了一处
`(bool)*param[4]`，它不匹配前述 `v` 文本检索。

本轮再次完成四端定位与新反编译：

| 参考目标 | `render` RAW Process | 第 5 参数转换 |
|---|---:|---|
| Android arm64-v8a | `0x5A0008` | 内联 Variant truthiness `switch` |
| Android armeabi-v7a | `0x4E0DB4` | 调 `0x496CC4` |
| iOS arm64 | `0x1003F7130` | 调 `0x100037640` |
| iOS armv7 | `0x3DE6F4` | 调 `0x3589C` |

Android arm64 的现有 IDB 恰好漏建此函数边界；前一函数结束于 `0x5A0008`、后一函数
始于 `0x5A02AC`，因此在不保存的会话中临时定义 `[0x5A0008, 0x5A02AC)` 后由
Hex-Rays 成功反编译。四端共同控制流均为：至少 3 个参数；第 4 参数存在时按 real
转换；第 5 参数存在时按同一个 Variant truthiness 转换；随后调用 `renderImpl`。

因此 RAW Process 应明确写成 `param[4]->operator bool()`。这与字典 setter 修复属于
同一根因和同一原版语义，不引入额外兼容分支。
