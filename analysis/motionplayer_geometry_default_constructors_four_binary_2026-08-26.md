# Geometry 默认构造器与 NCB 附着生命周期（四参考二进制，2026-08-26）

## 1. 范围与结论

本纵切面闭合 `Motion.Point`、`Motion.Circle`、`Motion.Rect`、`Motion.Quad`
四个零参数 NCB 构造器，包括：构造描述符的虚表入口、统一调用边界、原生记录
分配、字段初始化、ClassInfo/native-instance 查找、附着失败清理，以及脚本 adaptor
对原生记录的后续所有权。

四端共同结论是：每个构造器都分配一份完整的 `int32 type + double[15]`
记录，只写 `type = 0/1/2/3`，绝不初始化其余 15 个 `double`。它从当前脚本对象
按类 ID 取得已存在的非 sticky `ncbInstanceAdaptor<T>`，成功时把记录指针写入 adaptor；
查找失败、返回错误或返回空 native-instance 时，立即 scalar-delete 新记录并返回
`TJS_E_NATIVECLASSCRASH (-1008)`。

这证明四个公开 geometry 类应共享一个完整记录布局，而不是四种缩短的物理结构，
也证明不能给默认构造的坐标字段加零初始化、NaN 初始化或安全默认值。

## 2. 四端精确映射

### 2.1 NCB `FuncCall` 分派入口

| 类 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| Point | `Point_NCB_ctor_dispatch_guess@0x6DCBC4` | `...@0x59D858` | `...@0x10012CF5C` | `...@0x12BA08` |
| Circle | `Circle_NCB_ctor_dispatch_guess@0x6DD73C` | `...@0x59E38C` | `...@0x10012DD28` | `...@0x12C96C` |
| Rect | `Rect_NCB_ctor_dispatch_guess@0x6DE2FC` | `...@0x59EFBC` | `...@0x10012ECCC` | `...@0x12D8D4` |
| Quad | `Quad_NCB_ctor_dispatch_guess@0x6DEC88` | `...@0x59F98C` | `...@0x10012F904` | `...@0x12E5A8` |

Android arm64 的四个入口直接来自各构造描述符虚表 slot 2：
`0x1A17828/0x1A17CA8/0x1A18248/0x1A186C8 + 0x10`。Android armv7、
iOS arm64、iOS armv7 的对应虚表分别是：

- Android armv7：`0x10B9620`、`0x10B9860`、`0x10B9B30`、`0x10B9D70`；
- iOS arm64：`0x101ADF920`、`0x101ADFDA0`、`0x101AE0340`、`0x101AE07C0`；
- iOS armv7：`0x1831920`、`0x1831B60`、`0x1831E30`、`0x1832070`。

32 位虚表保存 Thumb 指针，表中函数地址已去掉最低位状态位。四端所有 16 个入口
均在本轮 fresh decompile，并写入语义名、函数签名和 IDB 注释。

### 2.2 原生分配与附着主体

| 类 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| Point | `Point_NCB_construct_and_attach_guess@0x6DCC98` | `...@0x59D8E8` | `...@0x10012CFFC` | `...@0x12BAD8` |
| Circle | `Circle_NCB_construct_and_attach_guess@0x6DD810` | `...@0x59E41C` | `...@0x10012DDC8` | `...@0x12C9D8` |
| Rect | `Rect_NCB_construct_and_attach_guess@0x6DE3D0` | `...@0x59F04C` | `...@0x10012ED6C` | `...@0x12D940` |
| Quad | `Quad_NCB_construct_and_attach_guess@0x6DED5C` | `...@0x59FA1C` | `...@0x10012F9A4` | `...@0x12E614` |

这些主体也全部 fresh decompile。类 ID/Info 存储分别成四组连续但独立的静态槽：

| 类 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Point | `0x1AB5690` | `0x1111A74` | `0x101ADF580` | `0x183175C` |
| Circle | `0x1AB56B8` | `0x1111A88` | `0x101ADF5A8` | `0x1831770` |
| Rect | `0x1AB56E0` | `0x1111A9C` | `0x101ADF5D0` | `0x1831784` |
| Quad | `0x1AB5708` | `0x1111AB0` | `0x101ADF5F8` | `0x1831798` |

每类使用自己的 ClassInfo/native class ID；物理邻接不表示共享 class identity。

## 3. 共同源码伪代码

四类只有 `shapeType` 和 ClassInfo 模板实参不同：

```text
CtorFuncCall(membername, result, numparams, params, objthis):
    if membername != null:
        return TJS_E_MEMBERNOTFOUND                  // -1001

    if numparams == 1 and params[0].type == Void:
        return TJS_S_OK                              // 不清 result，不构造

    context = { result, numparams, params, objthis, class-command }
    if result != null:
        result.Clear()

    if numparams < 0:                                // ArgsCount == 0
        return TJS_E_BADPARAMCOUNT                   // -1004

    native = operator new(sizeof(HitData))
    native.type = shapeType                          // 唯一一次字段写入

    adaptor = null
    if objthis != null and
       objthis.NativeInstanceSupport(
           TJS_NIS_GETINSTANCE, ClassInfo<T>.id, &adaptor) >= 0 and
       adaptor != null:
        adaptor.instance = native
        return TJS_S_OK

    operator delete(native)
    return TJS_E_NATIVECLASSCRASH                    // -1008
```

正常路径没有读取任何参数。因此零参数构造器不仅接受 0 个参数，也接受任意正数的
非 Void/混合冗余参数；只有“恰好一个 Void”被 ncbind 当成“不安装原生实例”的
特殊哨兵而提前成功返回。`numparams < 0` 虽不是普通 TJS 调用会产生的状态，仍是
二进制保留的明确边界。

`result.Clear()` 位于 Void 哨兵之后、最少参数检查之前。四端被调用的 helper 都会
把 Variant 类型设为 Void，并按原类型释放对象、字符串或 octet 所有权；所以不能把
它误读为 AddRef 或返回值复制。

## 4. 布局与初始化

| 项目 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| 分配字节数 | `0x80` | `0x80` | `0x80` | `0x7c` |
| `type` 偏移 | `+0` | `+0` | `+0` | `+0` |
| `values[0]` 偏移 | `+8` | `+8` | `+8` | `+4` |
| 写入范围 | 仅首 4 字节 | 仅首 4 字节 | 仅首 4 字节 | 仅首 4 字节 |

iOS armv7 的 `double` 自然对齐为 4，因此 `4 + 15*8 = 0x7c`；另外三端从 `+8`
开始 `double[15]`，并带自然尾部对齐，合计 `0x80`。这是同一个 portable 源声明
在 ABI 下的布局差异，不是 iOS armv7 缺字段，也不应靠手写 padding 复刻。

构造后未初始化值是对象存储中原有字节。对这些值调用 `x/y/r/l/t/w/h/p/contains`
会读取未初始化标量，属于原版边界而不是安全 API；本地实现不得通过成员默认初值
“修复”它。

## 5. 对象生命周期与 owner 边

1. 脚本类创建对象时已经安装了类专属、默认 `_instance=null`、`_sticky=false` 的
   `ncbInstanceAdaptor<T>`；本构造器只取得它，不新建 adaptor。
2. 原生 geometry 记录先分配，再查 adaptor。
3. 附着成功后，adaptor 成为唯一 owner；脚本对象 Invalidate 或 adaptor 析构时，
   非 sticky 路径 scalar-delete geometry 记录，然后清空 `_instance` 和 sticky 标志。
4. 附着失败时，构造器立即 scalar-delete 记录，adaptor 不取得所有权。
5. 四个 geometry 类型均是平凡析构记录，因此当前失败路径表现为直接
   `operator delete`；源层仍应表达为 `delete inst`，让模板保持通用生命周期语义。

## 6. 四端差异

- 64 位 `tTJSVariant` 类型 tag 位于 `+16`，32 位位于 `+8`，所以 Void 检查的
  机器偏移不同，但源语义完全相同。
- 64 位 `NativeInstanceSupport` 虚表偏移为 `+200`，32 位为 `+100`；adaptor 的
  native pointer 分别写 `+8` / `+4`。
- iOS armv7 为保存 VFP callee-saved 寄存器产生大量反编译噪声；实际构造写仍只有
  `type`。
- Android arm64 函数带 stack canary，其他当前反编译形态不同；不影响源结构。

没有发现平台条件分支、不同默认值、不同错误码或不同 owner 策略。

## 7. 本地逐行对照

- `cpp/plugins/motionplayer/main.cpp` 对四类均使用 `NCB_CONSTRUCTOR(())`，精确选择
  上述 `ncbNativeClassConstructor` 零参数实例；成员注册顺序已在注册表面报告闭合。
- `cpp/plugins/motionplayer/SourceCache.h` 的 `GeometryShapeBase_guess(shapeType)` 只执行
  `type = shapeType`；`HitData::values` 没有默认成员初始化。
- Point/Circle/Rect/Quad 默认构造分别传入 `0/1/2/3`。
- `HitTestInternal.h` 以自然布局声明 `int32_t + std::array<double,15>`，并用
  `alignof(double)` 验证 `0x80/0x7c` 两类 ABI 结果，没有嵌入平台 padding。
- 仓库内 `ncbind.hpp` 的 `doInvokeBase`、constructor `CallInvoke`、
  `ncbInstanceAdaptor::SetNativeInstance/_deleteInstance` 与四端机器码逐项一致。

本纵切面不需要修改运行 C++。只补充本地注释，明确 Void 哨兵、冗余参数、结果
清理和失败删除边界。

## 8. 验证与剩余限制

四端 16 个 descriptor-dispatch 函数和 16 个 construct/attach 主体均 fresh
decompile；构造虚表 slot、ClassInfo 槽、分配尺寸和唯一字段写已互相交叉验证。四个
IDB 已保存语义名、签名和注释。

当前机器仍缺 CMake/Emscripten，无法重新运行正式 motionplayer unit/Web build。
默认读取未初始化 `double` 本身也不适合编写会触发 C++ 未定义行为的本地单元测试；
因此验证应聚焦结构、构造机器码和可安全观察的脚本边界，不伪造确定坐标值。
