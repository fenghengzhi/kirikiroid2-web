# textrender 四参考二进制联合取证基线（2026-08-10）

## 结论状态

本文只记录本轮已通过 Codex 原生 `mcp__idalib__*` fresh 取证的结论。旧
`libkrkr2.so` 单文件分析、`TextRender.cpp` 中尚未迁移的裸地址、历史提交里的
“50/50”或“完全 1:1”措辞都不是四文件证据。

四个目标二进制和四个配套 `.i64` 均存在且可读；四个会话的 `module`、
`input_path`、imagebase、自动分析状态和 Hex-Rays 状态已逐一核对通过。

## 输入与会话

| 二进制 | IDA 会话 | imagebase | Hex-Rays |
|---|---|---:|---|
| `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | `tr_android_arm64` | `0x0` | ready |
| `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | `tr_android_armv7` | `0x0` | ready |
| `Kirikiroid2_1.3.9_iOS_arm64` | `tr_ios_arm64` | `0x100000000` | ready |
| `Kirikiroid2_1.3.9_iOS_armv7` | `tr_ios_armv7` | `0x4000` | ready |

`TextRender.dll` 与 `TextRenderBase` 的普通 IDA string 查询在四个会话中均为空；
UTF-16LE 原始字节搜索分别命中，UTF-8/UTF-32LE 均未命中。随后读取相邻原始字节，
确认两个字符串边界、NUL 终止符以及紧随其后的成员字符串表。

| 二进制 | `TextRender.dll` | `TextRenderBase` | 编码 |
|---|---:|---:|---|
| Android arm64 | `0x14C6350` | `0x14C636E` | UTF-16LE |
| Android armv7 | `0xD7AFAE` | `0xD7AFCC` | UTF-16LE |
| iOS arm64 | `0x10197CEB8` | `0x10197CED6` | UTF-16LE |
| iOS armv7 | `0x176F26A` | `0x176F288` | UTF-16LE |

## 模块、类注册与生命周期根节点

| 二进制 | 静态注册节点 | `Regist` / `Unregist` | 成员注册器 | constructor factory | 真构造函数 | 销毁路径 | `operator new` 大小 |
|---|---|---|---|---|---|---|---:|
| Android arm64 | `sub_42D3FC@0x42D3FC` | `TextRenderBase_NCB_AutoRegister_Regist@0x5A6A30` / `TextRenderBase_NCB_AutoRegister_Unregist@0x5A6B94` | `TextRenderBase_ncb_members@0x59C0AC` | `TextRenderBase_NCB_FactoryCallback@0x59D540` | `TextRenderBase_ctor@0x5A14FC` | adaptor `Invalidate@0x5A6E74` → `TextRenderBase_dtor@0x5A6F68` → delete | `0x250` |
| Android armv7 | `sub_2FF878@0x2FF878` | `TextRenderBase_NCB_AutoRegister_Regist@0x4E572C` / `TextRenderBase_NCB_AutoRegister_Unregist@0x4E57B0` | `TextRenderBase_ncb_members@0x4DF4AC` | `TextRenderBase_NCB_FactoryCallback@0x4DFBFC` | `TextRenderBase_ctor@0x4E1A00` | adaptor lifecycle cluster `0x4E5938..0x4E59A5` → `TextRenderBase_dtor@0x4E59A6` → delete | `0x1B0` |
| iOS arm64 | `InitFunc_169@0x100402100` | `TextRenderBase_NCB_AutoRegister_Regist@0x1003FE588` / `TextRenderBase_NCB_AutoRegister_Unregist@0x1003FE5F0` | `TextRenderBase_ncb_members@0x1003F52F4` | `TextRenderBase_NCB_FactoryCallback@0x1003F5AEC` | `TextRenderBase_ctor@0x1003F80B4` | adaptor `Invalidate@0x1003FE798` → `_deleteInstance@0x1003FE814`（对象析构内联并 delete） | `0x220` |
| iOS armv7 | `InitFunc_169@0x3E99FC` | `TextRenderBase_NCB_AutoRegister_Regist@0x3E5454` / `TextRenderBase_NCB_AutoRegister_Unregist@0x3E5508` | `TextRenderBase_ncb_members@0x3DC778` | `TextRenderBase_NCB_FactoryCallback@0x3DCEDC` | `TextRenderBase_ctor@0x3DF6DC` | adaptor `Invalidate@0x3E573A` → `_deleteInstance@0x3E5792`（对象析构内联并 delete） | `0x198` |

Android arm64 旧注释里的模块/成员/factory/ctor/dtor 地址分别为
`0x42D01C/0x59BCCC/0x59D160/0x5A111C/0x5A6B88`，与当前参考文件均不对应；
当前映射整体位于其后 `0x3E0`。旧地址不得继续传播到新分析或代码注释。

### NCB 静态节点、24 个 hint 与装卸顺序

四份静态初始化函数都只构造一个
`ncbNativeClassAutoRegister<TextRenderBase>` 节点：按 ABI 指针宽度依次写入
vptr、`TextRender.dll`、旧 `ncbAutoRegister::_top[ClassRegist]`、
`TextRenderBase`，再把该节点写回 `ClassRegist` 链表头。`ClassRegist`
的枚举值是 1，这是注册链所在的 line，不是“注册/注销 phase”。

| 二进制 | init-array 槽 | registrar 对象 | 对象大小 | 24 个 `uint32` hint 区间 |
|---|---:|---:|---:|---:|
| Android arm64 | `0x19E7028` | `0x1AB2170` | `0x20` | `0x1AB2190..0x1AB21EF` |
| Android armv7 | `0x10A102C` | `0x110F9B8` | `0x10` | `0x110F9C8..0x110FA27` |
| iOS arm64 | `0x1019AA7B8` | `0x101B97430` | `0x20` | `0x101B97450..0x101B974AF` |
| iOS armv7 | `0x1775D58` | `0x18A62CC` | `0x10` | `0x18A62DC..0x18A633B` |

hint 块紧跟 registrar 对象，全部由 BSS 零初始化，四文件的顺序均为：

`face, bold, italic, onFontChange, onGetTextWidth, onEval, pos, time, add,
graph, text, x, y, cw, size, color, shadow, edge, shadowColor, shadowDiff,
edgeColor, ruby, vertical, delay`。

这些是跨调用共享的 TJS 成员名缓存，不是每对象字段。静态初始化不调用
`__cxa_atexit`，因此 registrar 没有进程退出析构登记；hint 也不会在
`Unregist` 时清零，其存活期是整个进程/镜像装载期。

`Regist` 构造 `ncbRegistNativeClass<TextRenderBase>` delegate 和 RAII 的
`ncbRegistClass`：`RegistBegin` 创建 native class、登记 class ID/
class info/空 `finalize`；成员注册器再按固定顺序加入 factory、16 method
和 33 property；`RegistEnd` 补 dummy constructor（本类已有真 constructor，
所以不会补）、取 global、将 class object 装箱为 variant、释放原始
class object 引用、`PropSet(TextRenderBase)`、释放 global，最后析构 variant。

`Unregist` 使用同一个 50 项成员注册器，但 factory/method/property
`Create(false)` 都不分配成员对象；更重要的是本类的 delegate
`ncbRegistNativeClass<T>` 未覆盖 `UnregistItem`，所以逐项注销是空操作。
真正的注销全在 `UnregistEnd`：从 global 删除 `TextRenderBase`、释放
global，并清除 `ncbClassInfo<TextRenderBase>`。`ncbind.hpp` 中会逐成员
`DeleteMember` 的是另一个 `ncbAttachTJS2Class` delegate，不能套到本插件。

异常边上，Android arm64 的 `Regist/Unregist` 都有分阶段 landing pad，
会先执行 RAII `End` 再 resume；Android armv7 和 iOS arm64 未生成局部
landing pad；iOS armv7 的 SjLj landing 会先调用 wrapper 析构/
`End` 再重抛。四份静态初始化本身只写指针，没有可抛出操作。

### NCB delegate、factory item 与 adaptor 的物理边界

`ncbind.hpp` 的源级模板边界不能直接按一个平台的函数划分搬到另一个平台。fresh
反编译确认的 delegate 主体如下；`inline` 表示该源级方法被并入调用者，并不表示流程
缺失：

| 源级方法 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `ncbRegistNativeClass<TextRenderBase>::RegistBegin` | `0x5A6CEC` | `0x4E582C` | `0x1003FE64C` | `0x3E55B8` |
| `RegistEnd` | `0x5A70DC` | `0x4E5A1C` | `0x1003FE8F4` | `0x3E58B4` |
| `UnregistEnd` | inline in `AutoRegister_Unregist` | `0x4E5ADC` | `0x1003FE9F0` | `0x3E59E8` |
| `RegistItem` | `0x5A7244` | `0x4E5B94` | `0x1003FEB20` | `0x3E5B24` |
| `_AddDummyConstructor` | inline in `RegistEnd` | `0x4E5B1C` | `0x1003FEA60` | `0x3E5A28` |
| `NotImplCallback` | `0x5A723C` | `0x4E5B58` | `0x1003FEAC4` | `0x3E5A5E` |
| `EmptyCallback` (`finalize`) | `0x5A6E6C` | `0x4E5934` | `0x1003FE790` | `0x3E5734` |

iOS 两份还保留 `ncbRegistClass::End` 的独立 selector
`0x1003FE8E0/0x3E58A4` 及调用它的 wrapper 析构
`0x1003FE8B8/0x3E5814`；Android 优化后没有同样的独立边界。`RegistItem`
先比较 `name == className` 并维护 `_hasCtor`，重复时写
`Multiple constructors.(...)` 日志；随后仅对非空 item 注册 dispatch/type/flags，
最后 `Release` item。这个顺序解释了注销遍历中 null item 不产生逐成员动作。

factory 注册本身也分三层，不能与 `TextRenderBase::factory` callback 或 factory
item 的 `FuncCall` 混为一谈：

| 源级方法 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `ncbRegistClass::Factory(raw callback)` | inline | `0x4DFBD4` | `0x1003F5A98` | `0x3DCEB2` |
| `ncbNativeClassFactory<TextRenderBase>::Create` | inline | `0x4E5B62` | `0x1003FEACC` | `0x3E5A68` |
| `ncbNativeClassFactory<TextRenderBase>` ctor | inline | `0x4E5CA4` | `0x1003FEC70` | `0x3E5C90` |

`Create(false)` 直接返回 null；注册态才分配 factory item。64-bit factory 对象为 56B，
嵌入式 `iMethod` 在 `+32`、factory callback 在 `+48`；32-bit 对象为 32B，对应偏移为
`+20/+28`。其构造器写入 `ncbNativeClassMethodBase(nitMethod)`、名字 `Function`、外层与
`iMethod` 的 back-pointer 及 raw callback，callback 为空时抛 `No factory pointer.`。
Android arm64 把 allocation/ctor 全部内联进成员注册器，并复用该函数尾的
delete/resume 清理。iOS armv7 的 `Create` SjLj landing pad 会在 ctor 抛出时 delete 外层
allocation，ctor 自己另有 base-dtor + resume；iOS arm64 与 Android armv7 的独立
factory `Create/ctor` 则没有同类本地 unwind cleanup。这与普通 method 对象在 iOS arm64
上有两级 landing pad 的结果不同，是实在的目标差异。

native instance 的实际 holder 是源码中的
`ncbInstanceAdaptor<TextRenderBase>`，不是插件自定义 holder。它的三个字段是 vptr、
`TextRenderBase* _instance` 和 `bool _sticky`；`CreateEmptyAdaptor` 将后两者置空/false。

| adaptor 边界 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `CreateEmptyAdaptor` | `0x5A6E40` | `0x4E5914` | `0x1003FE764` | `0x3E5714` |
| `Invalidate` | `0x5A6E74`（内联 `_deleteInstance`） | `0x4E5938` | `0x1003FE798` thunk | `0x3E573A` thunk |
| `_deleteInstance` | inline | shared tail `0x4E5988` | `0x1003FE814` | `0x3E5792` |
| complete / deleting dtor | `0x5A6EB8/0x5A6F18` | internal entries `0x4E593C/0x4E5964` | `0x1003FE79C/0x1003FE7E0` | `0x3E5740/0x3E576C` |

Android armv7 的 vtable 存的是 Thumb 指针 `0x4E5939/0x4E593D/0x4E5965`；IDA
把 `Invalidate`、complete dtor、deleting dtor 和共同尾块合成一个
`0x4E5938..0x4E59A5` 函数。它们仍是三个源级入口，IDB 已在两个内部入口和共同尾块
逐行标注。所有平台的 `_deleteInstance` 都只在 `_instance != null && !_sticky` 时
析构并 delete `TextRenderBase`，之后无条件把 instance 清零、sticky 复位。基类
`Construct`/`Destruct` vtable 槽沿用共享的 `tTJSNativeInstance` 实现，不属于
textrender 私有函数。

## NCB 公开面

四个成员注册器都按相同顺序注册 1 个 constructor、16 个 method 和 33 个
property。部分宽字符串被 IDA 错标成首字符；名字通过 UTF-16LE 原始字节边界还原，
不能按反编译器显示的 `"s"`、`"r"`、`"d"` 等单字符读取。

Methods：

`setOption`, `setDefault`, `setRenderSize`, `clear`, `resetFont`, `resetStyle`,
`setFont`, `setStyle`, `render`, `newline`, `done`, `onEval`, `getKeyWait`,
`calcLineOffset`, `calcShowCount`, `getCharacters`。

RW properties（22）：

`vertical`, `timeScale`, `fontScale`, `defaultFace`, `defaultFontSize`,
`defaultBigFontSize`, `defaultSmallFontSize`, `defaultLineSize`,
`defaultLineSpacing`, `defaultPitch`, `defaultAlign`, `defaultValign`,
`defaultRubySize`, `defaultRubyOffset`, `defaultChColor`, `defaultShadow`,
`defaultShadowColor`, `defaultShadowDiff`, `defaultEdge`, `defaultEdgeColor`,
`defaultBold`, `defaultItalic`。

RO properties（11）：

`renderOver`, `renderLines`, `renderCount`, `renderDelay`, `renderLeft`,
`renderTop`, `renderRight`, `renderBottom`, `renderText`, `maxScrollOffset`,
`maxScrollLine`。

### 普通 method 的 NCB invoker 类型

除专用 raw callback `render` 外，15 个普通 method 实际只实例化八种
`ncbNativeClassMethod<InvokeCommand<...>>`。以下地址是各类型 vtable 的 `FuncCall`
槽；Android arm64 将若干注册 helper 内联，但 vtable/调用体仍可独立定位：

| C++ 形状 / 使用者 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `void(variant)`：setOption/setDefault/setFont/setStyle | `0x5A75C0` | `0x4E5EB0` | `0x1003FEF88` | `0x3E6060` |
| `void(float,float)`：setRenderSize | `0x5A77FC` | `0x4E61C8` | `0x1003FF33C` | `0x3E64F4` |
| `void()`：clear/resetFont/resetStyle/newline/done | `0x5A7ACC` | `0x4E6430` | `0x1003FF654` | `0x3E68A8` |
| `variant(ttstr)`：onEval | `0x5A7CE4` | `0x4E672C` | `0x1003FFA50` | `0x3E6D78` |
| `variant()`：getKeyWait | `0x5A8020` | `0x4E6A68` | `0x1003FFE68` | `0x3E726C` |
| `double(int)`：calcLineOffset | `0x5A81F8` | `0x4E6C78` | `0x10040010C` | `0x3E75CC` |
| `int(int)`：calcShowCount | `0x5A8478` | `0x4E6EC4` | `0x100400414` | `0x3E7960` |
| `variant(int,int)`：getCharacters | `0x5A86EC` | `0x4E7100` | `0x100400710` | `0x3E7CEC` |

四平台共同的可观察封送顺序是：非空 member name 先返回 `-1001`；空 objthis 返回
`-1008`；清空非空 result；按上表检查最少参数数，不足返回 `-1004`；再以
`NativeInstanceSupport(GETINSTANCE)` 取得实例，失败返回 `-1008`；最后从左到右转换
参数并调用成员。多余参数被忽略。variant 参数按值复制；float 参数先走 TJS real
转换再窄化；ttstr 走字符串转换；int 走 TJS integer 转换。void 返回保持 result 为
void，variant 返回保持成员生成的类型，`calcLineOffset` 写 type 5 real，`calcShowCount`
写 type 4 integer。四个 IDB 已把这八类入口统一命名并保存。

`render` 不走上述模板：共享 raw-method 包装先取得实例，之后才进入专用 Process 检查
最少三个参数，所以它在“实例损坏且参数不足”时的错误优先级与普通 method 不同。
该 `ncbRawCallbackMethod<TextRenderBase>::FuncCall` vtable 槽在 Android arm64/armv7、
iOS arm64/armv7 分别为 `0x5A7BD4/0x4E659C/0x1003FF81C/0x3E6AF8`；此前四份 IDB
均把它留作匿名函数，本轮已按真实模板层命名，不能把它与 textrender 自己的 raw
`render` callback 合并。

成员注册器到上述 invoker 对象之间还存在一层源自 `ncbind.hpp` 的
`ncbRegistClass::Method/RawCallback` 模板。它先按 `_isRegist` 调用对应
`ncbNativeClassMethod::Create`，再把返回的 `iMethod` 交给 `DoItem/RegistItem`；注销态
`Create(false)` 返回 null，`DoItem` 转走 `UnregistItem`。这一层不能与 invoker 的
`FuncCall` 槽合并。四目标抽取边界如下（`inline` 表示 Android arm64 直接展开在
`TextRenderBase_ncb_members`）：

| 注册模板形状 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `Method void(variant)` | `0x59D598` | `0x4E5DD8` | `0x1003FEE38` | `0x3E5E78` |
| `Method void(float,float)` | inline | `0x4E60F0` | `0x1003FF1EC` | `0x3E630C` |
| `Method void()` | `0x59EF58` | `0x4E6358` | `0x1003FF504` | `0x3E66C0` |
| raw callback `render` | inline | `0x4E0D80` | `0x1003F70DC` | `0x3DE6CC` |
| `Method variant(ttstr)` | inline | `0x4E6654` | `0x1003FF900` | `0x3E6B90` |
| `Method variant()` | inline | `0x4E6990` | `0x1003FFD18` | `0x3E7084` |
| `Method double(int)` | inline | `0x4E6BA0` | `0x1003FFFBC` | `0x3E73E4` |
| `Method int(int)` | inline | `0x4E6DEC` | `0x1004002C4` | `0x3E7778` |
| `Method variant(int,int)` | inline | `0x4E7028` | `0x1004005C0` | `0x3E7B04` |

Android arm64 的两个保留 wrapper 已把 `Create` 与 method-object 构造完全内联；其它
三个目标仍保留 `wrapper -> Create -> ctor -> RegistItem` 的物理调用链。四个 IDB 已按
上述 C++ 形状统一命名，不再把它们误作 textrender 自己的业务方法。

继续下钻后，独立 `Create/ctor` 入口如下；斜线两侧分别是 `Create / ctor`：

| method-object 形状 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| `void(variant)` | inline in `0x59D598` | `0x4E5E0C / 0x4E5E48` | `0x1003FEE8C / 0x1003FEEF0` | `0x3E5EA0 / 0x3E5F60` |
| `void(float,float)` | inline in members | `0x4E6124 / 0x4E6160` | `0x1003FF240 / 0x1003FF2A4` | `0x3E6334 / 0x3E63F4` |
| `void()` | inline in `0x59EF58` | `0x4E638C / 0x4E63C8` | `0x1003FF558 / 0x1003FF5BC` | `0x3E66E8 / 0x3E67A8` |
| raw `render` | inline in members | `0x4E64F4 / 0x4E6530` | `0x1003FF728 / 0x1003FF78C` | `0x3E6940 / 0x3E6A00` |
| `variant(ttstr)` | inline in members | `0x4E6688 / 0x4E66C4` | `0x1003FF954 / 0x1003FF9B8` | `0x3E6BB8 / 0x3E6C78` |
| `variant()` | inline in members | `0x4E69C4 / 0x4E6A00` | `0x1003FFD6C / 0x1003FFDD0` | `0x3E70AC / 0x3E716C` |
| `double(int)` | inline in members | `0x4E6BD4 / 0x4E6C10` | `0x100400010 / 0x100400074` | `0x3E740C / 0x3E74CC` |
| `int(int)` | inline in members | `0x4E6E20 / 0x4E6E5C` | `0x100400318 / 0x10040037C` | `0x3E77A0 / 0x3E7860` |
| `variant(int,int)` | inline in members | `0x4E705C / 0x4E7098` | `0x100400614 / 0x100400678` | `0x3E7B2C / 0x3E7BEC` |

`Create(false)` 在分配前返回 null。注册态的 64-bit method/raw 对象为 64B，嵌入式
`iMethod` 在 `+32`；32-bit 对象为 36B，`iMethod` 在 `+20`。构造器先建立
`ncbNativeClassMethodBase(nitMethod)`，名字为 `Function`，令 `iMethod` 的 back-pointer
指回外层对象，再写 specialization vtable 与成员函数指针；普通 method 的成员指针在
64/32-bit 分别为 16/8B。全零成员指针抛 `No method pointer.`；raw 对象改存 callback 与
flags，callback 为空抛 `No callback pointer.`。

异常清理也有真实平台差异。iOS arm64 每个 `Create` 后有独立
`operator delete + _Unwind_Resume` landing pad，每个 ctor 后另有 base-dtor + resume
landing pad；iOS armv7 把同样两级清理写进两个函数各自的 SjLj 状态机。Android arm64
的两个保留 wrapper 把 base 析构、delete、resume 合入函数尾，成员注册器中其余展开的
new-expression 共用尾部 delete/resume；Android armv7 的独立 `Create/ctor` 不含本地
unwind cleanup。正常注册传入的函数指针均非空，因此差异只在异常/非法直接调用边可见。

### Property 的 NCB invoker 类型

33 个 property 按 getter/setter 的 C++ 形状只实例化八种
`ncbNativeClassProperty<PropertyCommand<...>>`。下表列出每种对象 vtable 的
`PropGet/PropSet` 槽；只读类型也有 PropSet 入口，但构造出来的 property 对象把
setter member pointer 保存为零。

| C++ 形状 / 使用者 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| RW `bool`：vertical/defaultShadow/defaultEdge/defaultBold/defaultItalic | `0x5A8A18/0x5A8B3C` | `0x4E735C/0x4E73E8` | `0x100400A28/0x100400AA4` | `0x3E8040/0x3E80A6` |
| RW `float`：timeScale/fontScale/defaultFontSize/defaultBigFontSize/defaultSmallFontSize/defaultLineSize/defaultLineSpacing/defaultPitch/defaultRubySize/defaultRubyOffset | `0x5A8D64/0x5A8E94` | `0x4E764C/0x4E76D8` | `0x100400DA4/0x100400E20` | `0x3E8454/0x3E84BA` |
| RW `ttstr`：defaultFace | `0x5A9048/0x5A9134` | `0x4E7954/0x4E79E0` | `0x100401130/0x1004011AC` | `0x3E887C/0x3E88E2` |
| RW `tjs_int`：defaultAlign/defaultValign/defaultChColor/defaultShadowColor/defaultShadowDiff/defaultEdgeColor | `0x5A951C/0x5A9640` | `0x4E7D4C/0x4E7DD8` | `0x1004015D8/0x100401654` | `0x3E8DF8/0x3E8E5E` |
| RO `bool`：renderOver | `0x5A97E8/0x5A990C` | `0x4E8038/0x4E80C4` | `0x100401958/0x1004019D4` | `0x3E920C/0x3E9272` |
| RO `tjs_int`：renderLines/renderCount | `0x5A9A10/0x5A9B34` | `0x4E8220/0x4E82AC` | `0x100401B78/0x100401BF4` | `0x3E9454/0x3E94BA` |
| RO `float`：renderDelay/renderLeft/renderTop/renderRight/renderBottom/maxScrollOffset/maxScrollLine | `0x5A9C38/0x5A9D68` | `0x4E8408/0x4E8494` | `0x100401D98/0x100401E14` | `0x3E969C/0x3E9702` |
| RO `ttstr`：renderText | `0x5A9E6C/0x5A9F58` | `0x4E85F0/0x4E867C` | `0x100401FB8/0x100402034` | `0x3E98E4/0x3E994A` |

四平台共同的 PropGet 顺序是：非空 member name 返回
`TJS_E_MEMBERNOTFOUND(-1001)`；空 getter member pointer 返回
`TJS_E_ACCESSDENYED(-1007)`；空 objthis 返回
`TJS_E_NATIVECLASSCRASH(-1008)`；若 result 非空则先 Clear；再取得 native
instance，失败返回 `-1008`；最后调用 getter。result 允许为空，此时 getter 仍会被
调用，`ttstr` 返回临时对象也照常执行 AddRef/Release。装箱规则为：bool 变成值为 0/1
的 type 4 integer，tjs_int 变成 type 4 integer，float 先提升为 double 后变成 type 5
real，ttstr 变成引用计数的 string variant。

PropSet 的共同顺序是：非空 member name 返回 `-1001`；空 setter member pointer 返回
`-1007`；空 objthis 返回 `-1008`；空 param 返回 `TJS_E_FAIL(-1)`；随后取得 native
instance、转换单个参数并调用 setter。因 setter 指针检查早于 objthis/param，正常构造
的 RO property 对任何写入都稳定返回 `-1007`。bool 使用 TJS variant 真值转换；float
先转 TJS real 再窄化为 float；ttstr 构造并析构临时引用；tjs_int 使用 TJS integer
转换。四个 IDB 的 64 个 PropGet/PropSet 入口均已按上述八类统一命名并保存。

Property 注册同样另有一层 `ncbRegistClass::Property`，按 getter/setter 的 C++ 类型只
生成八个 wrapper；Android arm64 全部内联，其余三目标保留独立函数：

| Property 注册模板 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|
| RW `bool/bool` | `0x4E72A8` | `0x100400920` | `0x3E7EE4` |
| RW `float/float` | `0x4E7598` | `0x100400C9C` | `0x3E82F8` |
| RW `ttstr/ttstr` | `0x4E78A0` | `0x100401028` | `0x3E8720` |
| RW `int/int` | `0x4E7C98` | `0x1004014D0` | `0x3E8C9C` |
| RO `bool` | `0x4E7F84` | `0x100401850` | `0x3E90B0` |
| RO `int` | `0x4E816C` | `0x100401A70` | `0x3E92F8` |
| RO `float` | `0x4E8354` | `0x100401C90` | `0x3E9540` |
| RO `ttstr` | `0x4E853C` | `0x100401EB0` | `0x3E9788` |

这些 wrapper 只负责 `Create + DoItem`；真正的访问顺序和错误码仍由上一张
PropGet/PropSet invoker 表中的 vtable 槽决定。

property ctor 在四个优化目标里都并入 `Create`；独立 Create 入口为：

| property-object 形状 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---:|---:|---:|
| RW `bool/bool` | inline in members | `0x4E72EC` | `0x10040097C` | `0x3E7F28` |
| RW `float/float` | inline in members | `0x4E75DC` | `0x100400CF8` | `0x3E833C` |
| RW `ttstr/ttstr` | inline in members | `0x4E78E4` | `0x100401084` | `0x3E8764` |
| RW `int/int` | inline in members | `0x4E7CDC` | `0x10040152C` | `0x3E8CE0` |
| RO `bool` | inline in members | `0x4E7FC8` | `0x1004018AC` | `0x3E90F4` |
| RO `int` | inline in members | `0x4E81B0` | `0x100401ACC` | `0x3E933C` |
| RO `float` | inline in members | `0x4E8398` | `0x100401CEC` | `0x3E9584` |
| RO `ttstr` | inline in members | `0x4E8580` | `0x100401F0C` | `0x3E97CC` |

64-bit property 对象为 80B、嵌入式 `iMethod` 在 `+32`；32-bit 为 44B、`iMethod`
在 `+20`。`Create(false)` 同样不分配；注册态内联构造
`ncbNativeClassMethodBase(nitProperty)`，名字为 `Property`，再保存 getter/setter 两个
成员指针。RO specialization 的 setter 是全零成员指针；构造本身不拒绝它，因为访问
拒绝发生在 PropSet invoker。Android arm64 fresh 指令计数还确认成员注册器直接进行
1 次 56B factory、7 次 64B method/raw 和 33 次 80B property 分配；另两种 method
通过 `0x59D598/0x59EF58` 完成，合计与公开面的 1+16+33 注册顺序一致。

### NCB factory/method/property dispatch 的所有权与销毁

`Create` 返回的并不是额外堆对象，而是外层 dispatch 内嵌的
`ncbNativeClassMethodBase::iMethod` facade。其四个 vtable 槽已 fresh 对齐：

| `iMethod` 槽 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `GetDispatch` | `0x535184` | `0x498568` | `0x100042E18` | `0x401F4` |
| `GetFlags` | `0x53518C` | `0x49856C` | `0x100042E20` | `0x12BAC8` |
| `GetType` | `0x53519C` | `0x498576` | `0x100042E30` | `0x4A01C` |
| `Release` | `0x5351AC` | `0x498580` | `0x100042E40` | `0x401F8` |

这四个入口属于 source-level `ncbNativeClassMethodBase::iMethod`，factory、普通 method、
raw method 和 property 共用，不是某个 method specialization。`GetDispatch` 返回
back-pointer；另外两个 getter 通过外层 vtable 读取 flags/type；
`iMethod::Release()` 是真正的空函数。因此 `RegistItem` 尾部对 item 调用 Release 不改变
引用计数，堆对象的所有权只经返回的 `iTJSDispatch2*` 转移。

四份 `RegistItem` 随后共同进入 `tTJSNativeClass::RegisterNCM`，其共享边界及 dispatch
引用计数入口为：

| 共享边界 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `tTJSNativeClass::RegisterNCM` | `0x9F43F4` | `0x752AAC` | `0x10004E990` | `0x4DB4C` |
| `tTJSDispatch::AddRef` | `0x9F5660` | `0x7533E0` | `0x10005782C` | `0x564C8` |
| `tTJSDispatch::Release` | `0x9F5674` | `0x7533EA` | `0x100057840` | `0x564D2` |
| `tTJSDispatch` ctor / dtor | `0x9F562C / 0x9FA4A4` | `0x7533B8 / 0x7562E8` | `0x1000577B0 / 0x1000577D0` | `0x5646E / 0x56486` |

新 dispatch 的 refcount 初值为 1。`RegisterNCM` 映射名字、可选写 debug object type，
把 dispatch 装入局部 variant，使用 `MEMBERENSURE|IGNOREPROP|flags` 先调用
`PropSetByVS`（仅在 `E_NOTIMPL` 时 fallback 到 `PropSet`），随后显式 Release 传入
dispatch，再析构局部 variant。正常 PropSet 保存的 class-member variant 成为唯一长期
owner；`RegisterNCM` 的其它错误码不向上传递，因为接口返回 void。

卸载时 `UnregistEnd` 先从 global 删除 class member，再把 `ncbClassInfo` 的 name/id/object
裸字段清零；`Clear` 自身不 Release class object。因此若脚本没有额外 class 引用，删除
global member 会最终析构 class object，并由其 member variants 逐一 Release 这 50 个
factory/method/property dispatch；若仍有外部 class 引用，它们及这些 dispatch 会延寿到
最后一个 class 引用释放之后，但静态 `ncbClassInfo` 已经失效。旧对象没有额外 generation
或 unload guard。

`tTJSDispatch::Release` 在 refcount 为 1 时只调用一次 `BeforeDestruction`，之后重读
refcount；hook 若 AddRef 则只减掉当前引用并保留对象，否则走 vtable deleting dtor。
这些 NCB 对象沿用空 `BeforeDestruction`，成员函数指针/callback 也都是 POD，所以所有
specialization 的 deleting dtor 都只调用共享 base dtor 再 `operator delete`。Factory
本身的 slot 31 deleting dtor / slot 33 `GetFlags` 为：

| factory 槽 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| deleting dtor | `0x5A759C` | `0x4E5DC8` | `0x1003FEE24` | `0x3E5E68` |
| `GetFlags`（返回 0） | `0x53516C` | `0x498552` | `0x10004BA24` | `0xEB99C` |

iOS arm64/armv7 的相邻 slot 30 另有完整析构 thunk
`0x1003FEE20/0x3E5E64`，只转发到共享 `tTJSDispatch` dtor；两份 Android vtable 的 slot
30 直接指向共享 dtor。下表继续列出 method/raw 的 slot 31 deleting dtor：

| 形状 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `void(variant)` | `0x5A76F0` | `0x4E5F48` | `0x1003FF090` | `0x3E60CE` |
| `void(float,float)` | `0x5A787C` | `0x4E6274` | `0x1003FF400` | `0x3E6574` |
| `void()` | `0x5A7BA8` | `0x4E64E0` | `0x1003FF70C` | `0x3E692C` |
| raw `render` | `0x5A7CB8` | `0x4E6640` | `0x1003FF8E4` | `0x3E6B7C` |
| `variant(ttstr)` | `0x5A7DD4` | `0x4E67D8` | `0x1003FFB14` | `0x3E6DF8` |
| `variant()` | `0x5A810C` | `0x4E6B00` | `0x1003FFF00` | `0x3E72DA` |
| `double(int)` | `0x5A82E8` | `0x4E6D24` | `0x1004001D0` | `0x3E764C` |
| `int(int)` | `0x5A8568` | `0x4E6F70` | `0x1004004D8` | `0x3E79E0` |
| `variant(int,int)` | `0x5A87DC` | `0x4E71AC` | `0x1004007D4` | `0x3E7D6C` |

Property specialization 的同类入口为：

| 形状 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| RW `bool/bool` | `0x5A8C24` | `0x4E747C` | `0x100400BC4` | `0x3E8118` |
| RW `float/float` | `0x5A901C` | `0x4E776C` | `0x100400F40` | `0x3E852C` |
| RW `ttstr/ttstr` | `0x5A921C` | `0x4E7A74` | `0x1004012BC` | `0x3E8954` |
| RW `int/int` | `0x5A97BC` | `0x4E7E6C` | `0x100401774` | `0x3E8ED0` |
| RO `bool` | `0x5A99E4` | `0x4E8158` | `0x100401A54` | `0x3E92E4` |
| RO `int` | `0x5A9C0C` | `0x4E8340` | `0x100401C74` | `0x3E952C` |
| RO `float` | `0x5A9E40` | `0x4E8528` | `0x100401E94` | `0x3E9774` |
| RO `ttstr` | `0x5AA040` | `0x4E8710` | `0x1004020B4` | `0x3E99BC` |

factory 与普通 method/property 的 `GetFlags` specialization 均返回编译期 0；raw
method 对象的同一槽读取对象内 flags 字段。上述 144 个 factory/method/property
deleting-dtor/GetFlags 入口、iOS 的两个 factory 完整析构 thunk，以及共享
ctor/dtor/AddRef/Release/`iMethod` facade 均已写回四份 IDB。

method/property invoker 的大部分模板在各入口内联，但优化器仍在部分目标抽出两个共享
实例。第一个通过 `NativeInstanceSupport(GETINSTANCE, classID)` 获取 adaptor，再读取
`_instance`，任何缺失都形成 `TJS_E_NATIVECLASSCRASH(-1008)`；第二个是
`MethodCaller` 的 `void(bool)` 调用实例，负责复制/真值转换参数、销毁临时 variant、调用
setter：

| 共享 helper | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| native instance lookup | inline | `0x4E5F5C` | `0x1003FF0AC` | `0x3E60E2` |
| `Invoke void(bool)` | `0x5A8C50` | `0x4E750C` | inline | `0x3E8204` |

是否抽取完全由目标/优化器决定；这些地址不是额外的插件 API，也不能据此人为给共享
C++ 源码增加 wrapper。

### Factory constructor invoker

注册项使用的是 `Factory(&TextRenderBase::factory)`，不是 typed `Constructor()`。
外层 `ncbNativeClassFactory<TextRenderBase>::FuncCall` 与 textrender 自己的 raw callback
是两个独立层次：

| 层次 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| factory `FuncCall` | `0x5A74A4` | `0x4E5D24` | `0x1003FECF0` | `0x3E5D84` |
| `TextRenderBase::factory` callback | `0x59D540` | `0x4DFBFC` | `0x1003F5AEC` | `0x3DCEDC` |

四份外层共同顺序为：非空 member name 返回 `TJS_E_MEMBERNOTFOUND(-1001)`；如果
`numparams == 1` 且 `param[0]` 的类型为 void，则立即返回成功，不检查 objthis、不调用
callback、也不安装 native instance。该特例直接解引用 `param[0]`，自身不补空指针
保护。其他情况先把局部 instance 置空，再调用 raw callback；callback 的非零错误码
原样返回。它不会读取或清空传入的 result。

textrender callback 忽略 numparams/param，以各 ABI 对应的对象大小执行
`new TextRenderBase(objthis)`，写入 `*result` 并固定返回成功；因此除单 void 特例外，
多余参数和任意参数类型都不影响构造。callback 返回成功后，外层以
`NativeInstanceSupport(GETINSTANCE)` 取得 adaptor，并直接把新对象写入 adaptor 的
instance 槽。objthis 为空、GETINSTANCE 失败或 adaptor 为空时，外层会完整析构并
delete 刚创建的对象，再返回 `TJS_E_NATIVECLASSCRASH(-1008)`。四个 IDB 的 wrapper 与
callback 已分别统一命名为 `TextRenderBase_NCB_NativeClassFactory_FuncCall` 和
`TextRenderBase_NCB_FactoryCallback` 并保存。

### Property accessor 入口

以下入口由每个宽字符串的 fresh xref 回到成员注册器，再读取同一注册项里的 getter/
setter member pointer 得到；没有按 Android 地址差值平移。`G/S` 表示 getter/setter，
只写一个地址的是只读 getter：

| property | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| `vertical` | `5A1154/5A115C` | `4E1790/4E1794` | `1003F7DBC/1003F7DC4` | `3DF418/3DF41C` |
| `timeScale` | `5A1168/5A1170` | `4E1798/4E179E` | `1003F7DCC/1003F7DD4` | `3DF420/3DF426` |
| `fontScale` | `5A1178/5A1180` | `4E17A4/4E17AA` | `1003F7DDC/1003F7DE4` | `3DF42C/3DF432` |
| `defaultFace` | `5A1188/5A11EC` | `4E17B0/4E17BC` | `1003F7DEC/1003F7DF4` | `3DF438/3DF444` |
| `defaultFontSize` | `5A128C/5A1294` | `4E1828/4E182C` | `1003F7E60/1003F7E68` | `3DF508/3DF50C` |
| `defaultBigFontSize` | `5A129C/5A12A4` | `4E1830/4E1836` | `1003F7E70/1003F7E78` | `3DF510/3DF516` |
| `defaultSmallFontSize` | `5A12AC/5A12B4` | `4E183C/4E1842` | `1003F7E80/1003F7E88` | `3DF51C/3DF522` |
| `defaultLineSize` | `5A12BC/5A12C4` | `4E1848/4E184E` | `1003F7E90/1003F7E98` | `3DF528/3DF52E` |
| `defaultLineSpacing` | `5A12CC/5A12D4` | `4E1854/4E185A` | `1003F7EA0/1003F7EA8` | `3DF534/3DF53A` |
| `defaultPitch` | `5A12DC/5A12E4` | `4E1860/4E1866` | `1003F7EB0/1003F7EB8` | `3DF540/3DF546` |
| `defaultAlign` | `5A12EC/5A12F4` | `4E186C/4E1870` | `1003F7EC0/1003F7EC8` | `3DF54C/3DF550` |
| `defaultValign` | `5A12FC/5A1304` | `4E1874/4E1878` | `1003F7ED0/1003F7ED8` | `3DF554/3DF558` |
| `defaultRubySize` | `5A130C/5A1314` | `4E187C/4E1882` | `1003F7EE0/1003F7EE8` | `3DF55C/3DF562` |
| `defaultRubyOffset` | `5A131C/5A1324` | `4E1888/4E188E` | `1003F7EF0/1003F7EF8` | `3DF568/3DF56E` |
| `defaultChColor` | `5A132C/5A1334` | `4E1894/4E189A` | `1003F7F00/1003F7F08` | `3DF574/3DF57A` |
| `defaultShadow` | `5A133C/5A1344` | `4E18A0/4E18A6` | `1003F7F10/1003F7F18` | `3DF580/3DF586` |
| `defaultShadowColor` | `5A1350/5A1358` | `4E18AC/4E18B2` | `1003F7F20/1003F7F28` | `3DF58C/3DF592` |
| `defaultShadowDiff` | `5A1360/5A1368` | `4E18B8/4E18BE` | `1003F7F30/1003F7F38` | `3DF598/3DF59E` |
| `defaultEdge` | `5A1370/5A1378` | `4E18C4/4E18CA` | `1003F7F40/1003F7F48` | `3DF5A4/3DF5AA` |
| `defaultEdgeColor` | `5A1384/5A138C` | `4E18D0/4E18D6` | `1003F7F50/1003F7F58` | `3DF5B0/3DF5B6` |
| `defaultBold` | `5A1394/5A139C` | `4E18DC/4E18E2` | `1003F7F60/1003F7F68` | `3DF5BC/3DF5C2` |
| `defaultItalic` | `5A13A8/5A13B0` | `4E18E8/4E18EE` | `1003F7F70/1003F7F78` | `3DF5C8/3DF5CE` |
| `renderOver` | `5A13BC` | `4E18F4` | `1003F7F80` | `3DF5D4` |
| `renderLines` | `5A13C4` | `4E18FA` | `1003F7F88` | `3DF5DA` |
| `renderCount` | `5A13E0` | `4E190E` | `1003F7FA4` | `3DF5F2` |
| `renderDelay` | `5A13E8` | `4E1912` | `1003F7FAC` | `3DF5F6` |
| `renderLeft` | `5A13F8` | `4E1924` | `1003F7FBC` | `3DF608` |
| `renderTop` | `5A1400` | `4E192A` | `1003F7FC4` | `3DF60E` |
| `renderRight` | `5A1408` | `4E1930` | `1003F7FCC` | `3DF614` |
| `renderBottom` | `5A1410` | `4E1936` | `1003F7FD4` | `3DF61A` |
| `renderText` | `5A1418` | `4E193C` | `1003F7FDC` | `3DF620` |
| `maxScrollOffset` | `5A1438` | `4E195A` | `1003F7FFC` | `3DF63E` |
| `maxScrollLine` | `5A1460` | `4E1984` | `1003F8020` | `3DF65E` |

除 `defaultFace` 和末尾计算型 getter 外，RW 项都是相应标量字段的直接读写；
`defaultFace` getter 做无符号范围检查并返回空串后备，setter 复制 ttstr 临时后调用
`resolveFaceIndex`。颜色与 align/valign 都是 32-bit integer property，不是 float；bool
项读写单字节。`renderLines` 按各平台 `Line` stride 计算 vector size，`renderDelay`
返回 `renderDelayAccum*timeScale`，`renderText` 返回时 AddRef。四份映射已在各 IDB 中
统一重命名并保存。

两个计算型滚动 getter 的 fresh 四文件伪代码完全一致：

```text
maxScrollOffset:
    vertical ? renderSizeW-renderLeft : renderSizeH-renderBottom

maxScrollLine:
    count = narrow lineList.size() to int
    if count < 1: return 0.0
    remaining = vertical ? renderSizeW : renderSizeH
    offset = 0
    for line from last to first:
        remaining -= line.lineHeight
        if remaining < 0: break
        --offset
        if count + offset <= 0: return 1.0
    return offset != 0 ? float(count + offset) : 0.0
```

这里没有 clamp、有限数检查或尺寸规范化：`maxScrollOffset` 可以返回负值/NaN；
`remaining==0` 仍算完整容纳，NaN 因 `<0` 为假也继续计入。因而“所有行都能放下”固定
返回 `1.0`，“最后一行都放不下”返回 `0.0`。32-bit 反编译器把浮点位型显示为 integer
返回只是软/硬浮点 ABI 表象，property invoker 四端都按 `float` 装成 TJS real。

## 构造数据流与容器顺序

四个真构造函数的共同伪代码：

```text
ctor(this, objthis):
    this.objthis = objthis                 // borrowed；不 AddRef
    following/leading/begin/end = 内置 UTF-16 禁则字符串
    renderText = empty
    初始化相同的 option/style/default/geometry 标量
    construct charList
    construct pendingLine
    construct lineList
    construct faceTable
    construct keyWaitList
    construct accumBuf
    construct curRubyText
    default-construct faceHash
    defaultFaceIndex = intern("normal")
```

四组 `following/leading/begin/end` 常量已直接读取 UTF-16LE 原始字节并逐字节比较，
内容完全一致。需要注意 Android armv7 把 `leading` 的 19 码点常量内联进构造器
literal pool，其余三目标以及另外三个字符串位于只读数据区；这只是链接布局差异。

| 常量 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 | 码点数 |
|---|---:|---:|---:|---:|---:|
| `following` | `0x14C6838` | `0xD7B402` | `0x10197D3CE` | `0x176F780` | 68 |
| `leading` | `0x14C68C2` | `0x4E1BF4` | `0x10197D458` | `0x176F80A` | 19 |
| `begin` | `0x14C68EA` | `0xD7B48C` | `0x10197D480` | `0x176F832` | 10 |
| `end` | `0x14C6900` | `0xD7B4A2` | `0x10197D496` | `0x176F848` | 10 |

### 构造器暴露的三个漏声明槽

四平台的标量区都证明当前旧移植版曾漏掉三个 4B 成员：

| 语义推定 | Android 64/iOS 64 | Android 32/iOS 32 | 构造器 | 已观察访问 |
|---|---:|---:|---|---|
| vestigial current big font size | `+120` | `+96` | 不初始化 | 无 |
| vestigial current small font size | `+124` | `+100` | 不初始化 | 无 |
| vestigial ruby bbox bottom | `+276` | `+252` | 不初始化 | 无 |

前两个槽夹在 `curFontSize` 与 `curRubySize` 之间，并与后面的
`defaultFontSize/defaultBigFontSize/defaultSmallFontSize/defaultRubySize` 顺序对应；第三个
槽补齐 `rubyLeft/rubyTop/rubyRight/rubyBottom` 四元组。四目标插件代码区对相应直接
offset 的 fresh 指令扫描均未发现读写，构造器也刻意跳过它们；因此共享源码已补回
两个 current big/small float 和一个 ruby bottom float，但不添加初始化器。这样既恢复
成员序列，又保留其 vestigial/uninitialized 边界，而不是用 padding 或零值伪造语义。

### 构造后的未初始化边界

四个构造器并没有把整个标量区清零。共同被显式初始化的业务标量只有：全部 option
bytes、dead `+61`、四个 default style bool、default align/valign、kinsoku max、
`curFontSize=-1`、`curRubySize=-1`、所有 default 字号/间距/颜色、time/font scale、
`charDelayStep=1`、render size `0/0`，以及末尾的 default face index。容器只执行各自
默认构造；pending `Line` 的 deque 被构造，但其八个行状态标量尚未初始化。

下列字段在 factory 返回后、第一次 `setRenderSize()->clear()` 之前保持分配器残值：
`renderOver`、全部 current style bool/index/align、render count 与内部状态、current
ruby/line 参数、render delay、current colors、pen/bounds/ruby bbox/render position，
以及 pending Line 的 metric/run/space 状态。`clear()`/`resetFont()` 会建立正常运行态，
但如果脚本在此之前直接读取 `renderCount/renderDelay/renderLeft...` 等 property，参考
实现本来就会暴露未指定值。旧移植版的类内 `=0` 初值把这条边界错误地确定化；本轮已
仅对四构造器明确未写的字段移除初值，保留原始生命周期语义。

四个析构链按上述成员的逆序销毁，均不触碰 `objthis`。Android 保留独立
`TextRenderBase` 对象析构 helper；iOS 将同一析构序列内联进
`ncbInstanceAdaptor<TextRenderBase>::_deleteInstance`。对象大小和容器控制块偏移是
ABI/标准库产物，不应写入 C++ padding 或 `offsetof` 断言。

优化器/标准库对外层成员析构的抽取边界如下；`inline` 只表示序列在对象析构或 adaptor
cleanup 中展开：

| 析构语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `TextRenderBase` object | `0x5A6F68` | `0x4E59A6` | inline in `0x1003FE814` | inline in `0x3E5792` |
| `faceHash` | inline | `0x4E208C` | `0x1003F8968` | `0x3DFFE4` |
| `vector<KeyWaitItem>` | inline | inline | `0x1003F89DC` | `0x3E0022` |
| `vector<Line>` | inline | `0x4E1C64` | `0x1003F8A2C` | `0x3E0050` |
| pending `deque<CharItem>` | `0x5A1F04` | `0x4E20EC` | `0x1003F8A84` | `0x3E007A` |
| `vector<CharItem*>` | inline | inline | `0x1003F8C48` | `0x3E016C` |
| `vector<RubyItem>` member helper | inline | `0x4E2230` | `0x1003F8BF0` | `0x3E0142` |

`vector<CharItem*>` 只释放指针数组，不 delete 指向 line/deque 内元素的非 owning 指针；
`vector<KeyWaitItem>` 是 8B POD record，也没有逐元素析构。`vector<Line>` 则必须逐个
析构每个 Line 的 deque，再释放外层 buffer。face hash 先 Release key/删除 node，再释放
bucket array。以上差异应由正常 C++ 成员和各目标 STL 自然产生，不能手写平台分支。

| 语义字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `charList` | `+296` | `+268` | `+296` | `+268` |
| `pendingLine` | `+320` | `+280` | `+320` | `+280` |
| `lineList` | `+432` | `+352` | `+400` | `+336` |
| `faceTable` | `+456` | `+364` | `+424` | `+348` |
| `keyWaitList` | `+480` | `+376` | `+448` | `+360` |
| `accumBuf` | `+504` | `+388` | `+472` | `+372` |
| `curRubyText` | `+528` | `+400` | `+496` | `+384` |
| `faceHash` | `+536` | `+404` | `+504` | `+388` |

元素和嵌套容器的 ABI 展开同样不能从 Android arm64 推广到其它目标：

| 项目 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `RubyItem` stride | `20` | `16` | `20` | `16` |
| `CharItem` stride | `80` | `64` | `80` | `64` |
| `deque<CharItem>` 控制块 | `80` | `40` | `48` | `24` |
| 每个 deque node 的 CharItem 数 | `6` | `8` | `51` | `64` |
| `Line` stride | `112` | `72` | `80` | `56` |

两份 64-bit 的 Ruby vector 都以 20B 步长寻址并允许相邻元素中的 ttstr 指针落在
4-byte 边界，两份 32-bit 都是 16B；这是“有效最大对齐为 4”的编译产物证据。仅凭
strip 后的四个文件仍不能唯一判定这一对齐来自 RubyItem 周围的源级声明、参考版本
TJS `ttstr` 类型自身的对齐规则，还是目标工程的编译设置；三者生成的成员访问与 vector
stride 可以相同。RubyItem 不是平台无关的序列化 POD，故不能为了 64-bit 的 20B 数字在
共享实现中臆造 `#pragma pack`/padding/sizeof 断言。源码只恢复共同可证的
`ttstr + float x/y/span` 字段序列；20/16 的精确 ABI 事实留在本表，并把“源 token 的
对齐来源”保留为不可唯一判定项。

`RubyItem` 含一个引用计数 `ttstr` 和三个 float；`CharItem` 还含
`vector<RubyItem>`，二者都不是 POD。CharItem 拷贝会 AddRef `text`、复制所有标量并
深拷贝 ruby vector；销毁逆序 Release 每个 ruby text、释放 ruby allocation，再
Release CharItem text。Android/libstdc++ deque node 以约 512B block 切分，iOS/libc++
以约 4096B block 切分，因而出现 `6/8` 对 `51/64` 的容量差；这是容器实现差异，
共享源码仍是 `deque<CharItem>`。

### `accumBuf` 的 UTF-16 push/扩容边界

`appendChar` 的第一步在四份中都是向 `vector<tjs_char>` 追加一个 2B 码元。满容量时的
入口如下；Android arm64 将整段直接内联在 `appendChar` 中：

| 入口 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `appendChar` | `0x5A3C60` | `0x4E3580` | `0x1003FA37C` | `0x3E176C` |
| reallocate + push | inline | `0x4E4658` | `0x10006573C` | `0x63130` |

Android/libstdc++ 取 `size + max(size,1)`，即首次容量 1、之后通常翻倍。它先分配新
buffer，在 `new + old_size` 写入新码元，以 `memmove` 复制全部旧码元，最后 delete 旧
buffer 并提交 begin/end/cap。Android armv7 的具名模板实例化执行相同顺序。

iOS/libc++ 取 `max(size+1,2*capacity)`，在 split buffer 的尾端先写新码元，再用
`memcpy` 把旧区间搬到它前面，最后交换三指针。arm64 使用 DWARF landing pad，armv7
使用 SjLj；长度/分配异常会析构临时 split buffer 并继续抛出。这里的元素构造、搬迁与
析构均是平凡操作，没有 ttstr 引用计数。四份都在旧存储提交前完成唯一可能抛出的
allocation，所以失败时旧 `accumBuf` 不变。

### `vector<RubyItem>` resize/扩容、引用计数与异常边

`appendChar` 并不 `push_back` 一个临时 RubyItem，而是
`ruby.resize(ruby.size()+1)` 后取得 `back()`。四份都把新增的整个元素值初始化为零，
再由调用者按 `x -> y -> text -> span` 顺序写槽位；其中 text 赋值 AddRef 当前 ruby
字符串，随后清空当前 ruby 字段时再 Release 持有者引用。

| 入口 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| resize wrapper | call default-append directly | `0x4E4170` | `0x1003FC050` | `0x3E3298` |
| default append/growth | `0x5A5754` | `0x4E4710` | `0x1003FC77C` | `0x3E3828` |
| copy old elements + swap | inline | inline | `0x1003FC898` | `0x3E3954` |
| split-buffer construct/destroy | — | — | `0x1003FC930/0x1003FC9F8` | `0x3E39D4/0x3E3AE8` |

Android arm64 已把 `resize(old_size+1)` 优化为 `_M_default_append(1)`；armv7 先在 resize
wrapper 中计算 additional count，再尾跳到 `0x4E4710`。后者被 IDA 错并入前一个
`vector<char16_t>` allocator 的函数范围，完整指令流仍是独立的 RubyItem default-append。
libstdc++ 的新容量为 `old_size + max(old_size,additional)`；64/32-bit 最大元素数分别受
20B/16B stride 限制为 `0xCCCCCCCCCCCCCCC` 与 `0x0FFFFFFF`。扩容先分配新 buffer，
从前向后 copy 每个旧 RubyItem：复制 ttstr 指针并 AddRef，再复制三个 float；然后清零
新增区间，Release/析构旧元素、delete 旧 buffer，最后提交三指针。

iOS/libc++ 的容量为 `max(old_size+additional,2*capacity)`，最大元素数同样是 64-bit
`0xCCCCCCCCCCCCCCC`、32-bit `0x0FFFFFFF`。split buffer 先清零新增区间，再从尾向头
copy 既有 RubyItem；每个非空 ttstr 都明确 AddRef，最后交换 vector/split-buffer 控制块。
临时 split buffer 随后析构原存储并 Release 原元素，因此这不是窃取 ttstr 指针的 move，
与 iOS `vector<Line>` 对既有 Line 的 move/swap 策略不同。

四份 Ruby 扩容在分配成功后只执行原子 AddRef、标量复制、清零、Release 和 delete，
这些路径不再抛异常；长度错误或 allocation 失败发生在旧 vector 变化之前。iOS 两份仍
保留 DWARF/SjLj 临时 split-buffer 清理 landing pad。Android arm64/armv7 没有对应的
局部 landing pad，但在其不抛出的复制区间里不需要已复制前缀回滚；旧 vector 同样在
最终提交前保持有效。上述 helper 已在四个 IDB 中命名保存，armv7 的 `0x4E4710` 另加
行注释标明 IDA 函数边界误并。

### `deque<CharItem>` node/map、push/pop/clear 与异常边

textrender 对这一 deque 有三类业务操作：`kinsoku` 共同落字尾部向 pending Line
`push_back`；
kinsoku 把 pending 尾项 copy 到临时 deque 的 `push_front`，再 `pop_back`；Line clear/
析构销毁区间。元素 copy 的共同语义是先 AddRef CharItem text、复制全部标量，再深拷贝
`vector<RubyItem>`；元素销毁反向释放 ruby vector 和 text。

| 入口 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `kinsoku` | `0x5A4E5C` | `0x4E41B4` | `0x1003FC0C8` | `0x3E32D0` |
| CharItem copy | `0x5A4C18` | front `0x4E493A`, back `0x4E4B86` | inline in push | inline in push |
| push-front | aux `0x5A5928` | `0x4E48B8`, aux `0x4E499A` | `0x1003FCAAC` | `0x3E3B74` |
| pop-back | inline + dtor `0x5A5B40` | `0x4E48E6`, aux `0x4E4B02` | `0x1003FCBA8` | `0x3E3CB0` |
| push-back | inline, aux `0x5A5BAC` | `0x4E490E`, aux `0x4E4BE6` | `0x1003FCC64` | `0x3E3D10` |
| map/node grow | map `0x5A59F8` | map `0x4E4A26` | front `0x1003FCD6C`, back `0x1003FD244` | front `0x3E3E5C`, back `0x3E4254` |
| deque dtor | `0x5A1F04` | `0x4E20EC` | `0x1003F8A84` | `0x3E007A` |
| 源码级 `Line::clear` | `0x5A2248` 完整保留 | 标量尾内联；调用 deque clear `0x4E229C -> 0x4E22E4` | 标量尾内联；调用 deque clear `0x1003F8ACC` | 标量尾内联；调用 deque clear `0x3E00A2` |
| Char deque 分段 destroy-range | `0x5A2030` | `0x4E219C`（同 node 子段 `0x4E220C`） | 内联在 deque clear | 内联在 deque clear |
| Ruby vector dtor | 内联在 CharItem 销毁 | `0x4E2230` | `0x1003F8BF0` | `0x3E0142` |

Line 深拷贝内部的整段 deque range copy 也保留了可辨认的标准库边界：Android arm64/
armv7 分别为 `0x5A4AA0/0x4E3E3C`，iOS arm64/armv7 分别为
`0x1003FAEAC/0x3E234C`。Android 的空 deque 初始化另外抽出 node allocation loop
`0x5A1A90/0x4E1D2A`，每次分别分配 `0x1E0/0x200`；iOS 的 copy range 则先调用
`__add_back_capacity(n)`（`0x1003FB068/0x3E2510`），再跨 4080/4096-byte node 复制。

iOS 两份把 deque map 实现为 `__split_buffer<CharItem*>`，其内部 helper 一一对应：

| libc++ map helper | iOS arm64 | iOS armv7 |
|---|---:|---:|
| map storage dtor | `0x1003F832C` | `0x3DFA00` |
| split-buffer ctor | `0x1003FBCB8` | `0x3E2E64` |
| map `emplace_back` / `emplace_front` | `0x1003FB718 / 0x1003FB87C` | `0x3E2AD4 / 0x3E2BB8` |
| temporary split-buffer `push_back/push_front` | `0x1003FB9E8 / 0x1003FBB4C` | `0x3E2C9E / 0x3E2D80` |
| map `push_front(existing pointer)` | `0x1003FD0D8` | `0x3E416C` |

最后一行与 `emplace_front` 的机器码近似但不是同一源模板入口：它用于把已有的对侧
spare node 旋转到 map 前端，而 `emplace_front` 接收新分配 node。旧注释曾把 iOS32
`0x3DFA00` 猜成外层 `faceTable` vector 析构；fresh xref 已证明它只在 deque dtor 释放
全部 node 后销毁 map 控制数组，现已纠正。

Android/libstdc++ 的 deque 控制块是 `map/map_size + start iterator + finish iterator`。
空 deque 构造即分配至少 8 个 node-pointer 的 map，并在中央放一个 node：arm64 node
480B/6 项，armv7 node 512B/8 项。push-front 在 start node 没有前置槽时分配前一 node，
把新 CharItem 构造在该 node 最后一项；push-back 在 finish 到达最后一个可构造槽时先
准备后一 node，在旧 finish 槽构造元素，再把 finish 切到新 node 的 begin。

map 空间不足时，若现有 map 足够宽，`reallocate_map` 只用 `memmove` 把当前 node-pointer
区间重新居中；否则新 map 长度为
`old_map_size + max(old_map_size,additional_nodes) + 2`，在新 map 中居中复制 node
指针、delete 旧 map，再重建 start/finish 的 node 指针。元素所在 node 从不因 map
增长而搬迁。pop-back 跨 node 时释放空的尾 node；clear 销毁全部 CharItem，释放 start
之后的所有 node，把 finish 复位为 start，但保留 map allocation 和 start node。

iOS/libc++ 的控制块是 node-pointer split-buffer 加 `start_index/size`，空 deque 全零，
第一次 push 才分配 node。arm64 node 4080B/51 项，armv7 node 4096B/64 项。front/back
grow 优先把对侧 spare node 的指针旋转到所需一端并调整 start index；否则在现有 map
内重心化 pointer 区间，或以空 map 1、后续通常翻倍的容量分配新 split-buffer，只复制
node 指针。push-front 在 CharItem copy 完成后才 `--start_index, ++size`；push-back 也在
copy 完成后才 `++size`。pop-back 销毁元素并减 size，尾部至少空出两个整 node 时释放
一个。clear 销毁全部元素、令 size=0，并从前端删 node 直到最多剩两个；剩两个 node 时
start index 设为 51/64，剩一个时设为 25/32，map allocation 仍保留。

Android armv7 还保留了 `deque<CharItem>::iterator` 的跨 node 有符号位移 helper
`0x4E4B38`。它以每 node 8 个 CharItem 为单位把正负元素差拆成 node 差与 node 内偏移，
在 kinsoku 回看中承担“从 end 向前 N 项”的标准库迭代器运算；它不是独立业务边界，
也不应被恢复成手写的环形/分块索引算法。

这里必须区分源码边界和标准库 helper 边界。fresh caller 指令核对证实，只有 Android
arm64 `0x5A2248` 同时执行 deque clear 与 Line 的 6 float + int + bool 清零，因而保留了
完整的 `Line::clear`。Android armv7 的调用者在 `0x4E229C` 返回后紧接
`__aeabi_memclr4(...,29)`；iOS arm64 调用者在 `0x1003F8ACC` 返回后以 8/16-byte store
清零 29-byte 标量尾；iOS armv7 也在 `0x3E00A2` 返回后以 NEON 32/8-bit store 清同一
29-byte 尾。后三个被旧 IDB 误名为 `Line_clear` 的入口本体完全不触碰这些标量，只是
`std::deque<CharItem>::clear`，说明三个编译器把源码级 `Line::clear` 展开到调用者、但
仍调用标准库 deque helper。三个 IDB 已就地纠正为 `CharDeque_clear`；共享源码继续
保留普通 `Line::clear()`，不能反向拆成平台专用业务函数。

异常回滚必须按目标区分：

- Android arm64 的 CharItem copy landing pad 在 ruby allocation 失败时 Release 已 AddRef
  的外层 text；push-front catch 恢复 start iterator、delete 新 node 后 rethrow，
  push-back catch delete 新 node 后 rethrow。map 可能已合法重排/扩容，但逻辑内容不变。
- Android armv7 的两份 CharItem copy、front/back aux 和 map grow 都没有本地 landing
  pad。ruby allocation 失败会发生在外层 text AddRef 之后；参考指令流没有配对 Release。
  front aux 已先提交 start 到新 node，back aux 则已把新 node pointer 写进 map但尚未提交
  finish，均不存在 arm64 的 node 回滚序列。这是参考实现的真实异常弱点。
- iOS arm64 的 front/back push 在 copy 成功后才提交 index/size，所以逻辑区间不增大；
  但 push、Ruby vector copy 和 front/back grow 均无本地 landing pad。copy 的 ruby
  allocation 失败时已 AddRef 的外层 text 没有可见 Release；需要先建临时 map、再建
  node 的 grow 分支也没有第二次 allocation 失败后的临时 map 清理。
- iOS armv7 的 push 有 SjLj landing pad，copy 失败时 Release 外层 text；front/back grow
  的 SjLj 清理还会 delete 已分配 node 和临时 map buffer，再 resume。start/size 只在
  copy 成功后提交，因此四份里这一目标的 libc++ deque 增长回滚最完整。

先前四个 deque dtor 被暂命名成 `LineItem_dtor`，但 fresh 函数体和 kinsoku 栈对象
共同证明它只接收/析构首成员的 deque 控制块；Line 尾部 metric 都不在其访问范围内。
四份 IDB 现已纠正为 `TextRenderBase_CharDeque_dtor`。它既可作用于 Line 首成员，也可
作用于独立的临时 deque，不等于一个完整 Line/LineItem 析构函数。

这些 specialization 已在四个 IDB 中按 copy/push/pop/map-grow 语义命名并保存。共享
源码仍应保持 `std::deque<CharItem>`；手写 node 大小、map 旋转或异常缺陷会把某一 ABI
错误固化到 wasm 目标。

### `vector<CharItem*>` / `vector<KeyWaitItem>` 增长与指针生命周期

`done()` 先把 charList 逻辑长度清零，再按 line 顺序、deque 内顺序 push 每个 CharItem
地址；`\k` 则向 keyWaitList push 一个 8B `{renderCount,0}` 记录。两者都是平凡元素，
扩容只复制原始 pointer/8B record，没有构造析构或引用计数。

| 入口 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `done` | `0x5A02C4` | `0x4E0EA0` | `0x1003F7250` | `0x3DE844` |
| charList reallocate + push | inline in `done` | `0x4E4D94` | `0x1003FD7C8` | `0x3E47DC` |
| render 主体 | `0x5A266C` | `0x4E2610` | `0x1003F8EF4` | `0x3E0410` |
| keyWait reallocate + push | `0x5A5C54` | `0x4E4C54` | `0x1003FD598` | `0x3E4548` |

32-bit Android 的 libstdc++ 额外保留 `_M_check_len + allocate` 两级 helper：KeyWait 为
`0x4E4CD8/0x4E4D18`，CharItem pointer 为 `0x4E4E00/0x4E4E40`；arm64 将相同计算内联。
iOS 的 slow path 则是 `__split_buffer` 构造后调用 vector
`__swap_out_circular_buffer`：KeyWait 在 arm64/armv7 为
`0x1003FD714 -> 0x1003FD69C` / `0x3E46C8 -> 0x3E468C`，CharItem pointer 为
`0x1003FD94C -> 0x1003FD8D4` / `0x3E4958 -> 0x3E491C`。这些临时 buffer helper
只搬运 POD/pointer，不取得 CharItem 所有权。

Android/libstdc++ 两类 vector 都取 `size+max(size,1)`；char pointer 的 64/32-bit
`max_size` 分别为 `0x1FFFFFFFFFFFFFFF/0x3FFFFFFF`，8B KeyWait 的 64/32-bit
`max_size` 为 `0x1FFFFFFFFFFFFFFF/0x1FFFFFFF`。新元素先写到新 buffer 的旧 end，旧
区间用 memmove/展开的整数复制搬迁，delete 旧 allocation 后提交三指针。Android
arm64 的 charList 增长直接内联在 done，另外三项保留独立模板 helper。

iOS/libc++ 两类 vector 都取 `max(size+1,2*capacity)`；相应最大元素数与上述 stride
一致。split buffer 先写新元素，再 memcpy/整数复制旧区间并交换控制块；arm64 的 DWARF
和 armv7 的 SjLj landing pad 会在长度/分配异常时释放临时 allocation。四份单次 push
都在唯一可能抛出的 allocation 成功后才提交，因而失败时该 vector 的既有内容不变。
但 `done()` 是逐项 push，不是一次 reserve/事务：第 N 项扩容失败时，clear 后已经成功
写入的 N-1 个指针仍构成 charList 的部分前缀，keyWait 回填和 sort 尚未执行。

两个 `clear()` 都只令 end=begin，保留 allocation/capacity；容量历史会跨 render/done
复用。`\k` 的 fast path 在 64-bit 目标用一次零扩展 64-bit store，在 32-bit 目标等价地
写低字并清高字，因此新 KeyWait.time 必为 0，不是未初始化 padding。

charList 不拥有 CharItem，只保存 lineList 中各 Line.deque 元素的地址。done 完成后这些
地址能跨同一 deque 的端点增长保持稳定；但 lineList 自身扩容时，Android 会深拷贝既有
Line 并销毁旧 deque，旧 charList 指针立即失效，而 iOS move/swap 既有 Line 的 deque
控制块，原 node 地址仍保留。更普遍的共同边界是 render 的 `flag&1==0` 入口先 clear
lineList/keyWaitList，却不 clear charList；直到随后调用 done 重建前，charList 仍保存已
销毁 Line 的悬空指针。clear() 方法虽也是先 lineList 后 charList，但两句之间没有回调。
`calcShowCount/getCharacters` 若在上述窗口被脚本调用，参考实现没有代际检查或防护。

done 在 sort 前用 KeyWait.index 直接索引 charList 并回填 renderPos bits，仍无边界检查；
随后 sort 只重排 pointer vector，不移动 CharItem。本轮只恢复并记录这些生命周期边界，
没有给共享源码增加 reserve、提前 clear 或索引校验。上述 helper 已在四个 IDB 中命名保存。

### `vector<Line>` push/扩容与异常边

`finishLine` 的源码操作是 `_lineList.push_back(_pendingLine)`；四份都先深拷贝新的
pending Line，区别只出现在 capacity 不足时怎样迁移旧元素：

| 入口 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| reallocate + push | `0x5A47C8` | `0x4E3CA4` | `0x1003FACC8` | `0x3E211C` |
| Line 的 deque 深拷贝构造 | `0x5A4968` | `0x4E3D88` | `0x1003FADB8` | `0x3E2254` |
| 迁移既有 Line | copy `0x5A4DAC` | copy `0x4E40FC` | move/swap `0x1003FBE74` | move/swap `0x3E30A6` |

Android armv7 还抽出 `vector<Line>::_M_check_len@0x4E4090` 与 72-byte 元素 allocator
`0x4E40D6`；Android arm64 内联同一逻辑。iOS 的 Line slow path 把临时
`__split_buffer<Line>` 构造/析构分别保留在
`0x1003FBF3C/0x1003FC004` 与 `0x3E3150/0x3E3274`。Ruby range copy 的 exact-size
`vector::__vallocate` 则是 `0x1003FBE24/0x3E3080`；Android armv7 对应的 Ruby vector
copy/allocate 为 `0x4E3F68/0x4E402A`，容量检查另存为 `0x4E4814`；arm64 将它们并入
CharItem copy/增长路径。

Android/libstdc++ 的新容量为 `size + max(size,1)`，即首次 1、之后通常翻倍，并受
`max_size` 限制；64/32-bit Line stride 不同使最大元素数分别为
`0x249249249249249` 与 `0x38E38E3`。它先在新 buffer 的旧 end 位置深拷贝 pending
Line，再从头到尾深拷贝每个旧 Line；全部成功后才析构旧元素、释放旧 buffer并提交
三指针。这里既有 Line 也会深拷贝 deque，而不是偷控制块。

iOS/libc++ 使用 `max(size+1,2*capacity)`（超过半个 max_size 时直接取 max_size），
64/32-bit 最大元素数分别为 `0x333333333333333` 与 `0x4924924`。新 pending Line 同样
深拷贝，但既有 Line 从尾到头迁入 split buffer：逐字段窃取 deque/map 控制块与行标量，
把源容器指针清零，最后交换 vector/split-buffer 三指针。因此 iOS 扩容不对既有行的
CharItem 再做 AddRef/深拷贝。

异常边也不是四平台完全相同。Android arm64 的 reallocate helper 有显式 catch：旧行
复制中途失败时，内层先析构已复制前缀；外层再析构已构造的 pending Line、释放新
buffer 并 rethrow，旧 vector 到 commit 前保持不变。iOS 两份通过 split-buffer RAII
和 DWARF/SjLj landing pad 在异常时销毁临时 buffer 后 resume，旧 vector 同样在 swap
前保持不变。Android armv7 的 `0x4E3CA4/0x4E40FC` 整段没有本地 landing pad；参考
文件中不存在与 arm64 相同的已复制前缀/新 buffer 回滚序列，不能把 arm64 的异常保证
写成四平台共同事实。上述 helper 已在四个 IDB 中按 copy 与 move/swap 语义命名保存。

## faceHash：首个已修正的单文件误归因

逐文件 fresh 映射：

| 二进制 | `intern(name)` |
|---|---|
| Android arm64 | `sub_5A18BC@0x5A18BC` |
| Android armv7 | `sub_4E1C2C@0x4E1C2C` |
| iOS arm64 | `sub_1003F82CC@0x1003F82CC` |
| iOS armv7 | `sub_3DF9C4@0x3DF9C4` |

共同伪代码：

```text
intern(name):
    it = faceHash.find(name)
    if it != faceHash.end():
        return it->second
    index = faceTable.size()
    faceHash[name] = index
    return index
```

Android 两个 libstdc++ 构造器中，默认哈希表构造展开为 bucket hint `10` 和对应
bucket 初始化；iOS 两个 libc++ 构造器只建立空 bucket 状态并设置
`max_load_factor = 1`，没有 `10` 参数或预分配。四个 `intern` 的业务控制流一致。

因此共享源码结构应是成员的默认构造，而不是显式 `_faceHash(10)`。后者是把 Android
标准库默认实现误写成源码参数，并会让 wasm/libc++ 产生 iOS 原版不存在的预分配。
本轮已把本地构造器改为默认构造；容器类型、hash functor 和 `intern` 数据流不变。

四文件的 hash 计算完全一致：空 ttstr 指针直接返回 0；非空 UTF-16 内容逐码元执行
`h=(1025*(h+ch)) ^ ((1025*(h+ch))>>6)`，尾声再做 `9*h`、`^>>11`、`*32769`，
最终 0 改成 `0xFFFFFFFF`。节点均缓存 hash：64-bit node 为 32B、32-bit 为 16B。
Android/libstdc++ 节点顺序是 `next,key,value,hash`，iOS/libc++ 是
`next,hash,key,value`；查找和插入仍是同一 `unordered_map<ttstr,int>` 源码操作。
iOS 对 2 的幂 bucket 数使用位与、其它 bucket 数使用取模，并按 load factor 触发
rehash；Android 使用其 libstdc++ rehash policy。上述差异不应手写进共享源码。

这段 hash 不是 textrender 私有算法，也不是会访问字符串 Hint 缓存的
`TJS::ttstr_hasher`。Android armv7 `0x497AFA` 有 155 个代码引用，iOS arm64
`0x100039AEC` 和 iOS armv7 `0x3798C` 各有 160 个，函数体逐句对应
`tTJSHashFunc<ttstr>::Make`；textrender 的 find/subscript 只是这些众多核心调用点中的
两个。Android arm64 在 `resolveFaceIndex` 内联同一函数。四份路径均没有
`GetHint()`、读已有 hint 或写回 hint 的指令。因此共享源码的 unordered_map wrapper
现已直接调用 `tTJSHashFunc<ttstr>::Make(s)`，删除先前手写的整套 `FaceNameHash`
算法体，同时刻意不替换成语义不同的 `ttstr_hasher`。

本轮又把 miss/rehash/clear 边界逐层拆开。`operator[]` miss 都先分配 node、AddRef
ttstr key、把 mapped int 值初始化为 0；随后按 `max_load_factor=1` 决定是否 rehash，
链接 node、增加 size，最后由 `resolveFaceIndex` 覆盖 mapped value。命中路径不分配、
不 AddRef。关键 specialization 入口如下：

| 入口 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| subscript/miss insert | `0x5A1BFC` | `0x4E1E5C` | `0x1003F837C` | `0x3DFA2C` |
| rehash | `0x5A1E10` | `0x4E1FE4` | `0x1003F8768` | `0x3DFDC4` |
| clear | inlined in `clear()` | `0x4E20AA` | `0x1003F8CF8` | `0x3E01E4` |
| destroy node chain | inlined in `clear()` | `0x4E20CC` | `0x1003F89A0` | `0x3E0000` |

Android armv7 对默认构造和链表式 bucket 操作还额外抽出五个 libstdc++ helper：
`FaceHash_ctor@0x4E1D7A`、`find_before_node@0x4E1E02`、
`rehash_aux@0x4E1F70`、`insert_bucket_begin@0x4E1F9A`，以及上表的主 rehash
`0x4E1FE4`。`find_before_node` 同时比较缓存 hash 与 ttstr 相等性；
`insert_bucket_begin` 修复 bucket 指向“首 node 的前驱”这一 libstdc++ 表示。构造器收到
默认 policy 展开的 hint 10，但这仍不是源码中的显式 `_faceHash(10)`。

Android 默认 bucket hint 10 先走 prime policy；增长门限按 `(size+1)` 与
`next_resize` 比较，新 bucket 请求取满足负载率的数量与旧 bucket 两倍的较大者，再选
不小于它的 prime。iOS 从 0 bucket 开始，首次 miss 把请求 1 规范化为 2；后续候选按
旧 bucket 约两倍和 `ceil((size+1)/load_factor)` 取大值，2 的幂可直接保留，否则取
next-prime。四份 rehash 都先检查最大 allocation、分配新 bucket array，成功后才替换
旧 array；node 本身不重建，只按缓存 hash 重新串链，因此 key 引用计数不变。

`clear()` 与析构不同：它逐 node Release key 并 delete node，把每个现有 bucket 清零，
把链头和 size 置零，但不缩 bucket count、不释放 bucket allocation，也不改变
max-load/growth policy；随后 `clear()` 再 intern 保存下来的 default face。四个调用图
中，find/subscript 只从 `resolveFaceIndex` 到达，clear/node destruction 只从插件
clear/析构到达；不存在按 key/range 调用 `erase` 的业务路径，所以没有可复原的
textrender erase 边界。

异常回滚同样有 ABI 差异：Android arm64 的 insert catch 会先恢复 rehash policy 的
`next_resize`，再 Release 新 node 的 key、delete node 并 rethrow；iOS armv7 的 SjLj
landing pad也释放 key/node。Android armv7 与 iOS arm64 的对应 subscript/insert 函数
没有本地 landing pad，参考文件里看不到 node rollback；不过 rehash 自身都在 bucket
allocation 成功前不改旧表。上述 specialization 已在四个 IDB 中统一命名并保存。

`resolveFaceIndex` 四者还有一个必须保留的退化边界：miss 时取 `faceTable.size()` 写入
hash value，但没有向 faceTable push。仓库中不存在其它 faceTable push 路径，因此表
保持空、所有新 face 的 index 都是 0；`defaultFace` getter 和 `onFontChange` 的 face
字段通常回退为空串。这看似不合理，但四参考文件一致，不能擅自补表。

## 状态复位链：`clear/resetFont/resetStyle`

| 二进制 | `clear` | `resetFont` | `resetStyle` |
|---|---|---|---|
| Android arm64 | `TextRenderBase_clear@0x59F04C` | `TextRenderBase_resetFont@0x59F2C0` | `TextRenderBase_resetStyle@0x59F39C` |
| Android armv7 | `TextRenderBase_clear@0x4E066C` | `TextRenderBase_resetFont@0x4E0818` | `TextRenderBase_resetStyle@0x4E08CC` |
| iOS arm64 | `TextRenderBase_clear@0x1003F6800` | `TextRenderBase_resetFont@0x1003F69FC` | `TextRenderBase_resetStyle@0x1003F6AD8` |
| iOS armv7 | `TextRenderBase_clear@0x3DDD1C` | `TextRenderBase_resetFont@0x3DDF5C` | `TextRenderBase_resetStyle@0x3DE008` |

iOS armv7 `resetFont` 的 Hex-Rays 在 `0x3DDFB4` 失败；本轮补读了该函数全部
57 条指令（`0x3DDF5C..0x3DE006`），覆盖所有条件分支、字段读写和返回，未用其它
文件代替。

`clear` 四者共同控制流：

```text
pendingLine.clear()
if vertical:
    penX = renderLeft = renderRight = renderSizeW
    pendingLine.bboxLeft = pendingLine.bboxRight = renderSizeW
else:
    penX = renderLeft = renderRight = 0
penY = renderTop = renderBottom = 0
kinsokuUsed = 0
release(curRubyText); curRubyText = null
charBufCountdown = lineStartX = 0
accumBuf.clear()
resetFont()
curPitch/curLineSize/curLineSpacing/curAlign/curValign = defaults
lineList.clear()
charList.clear()
renderPos/renderPosSnap/state288/renderDelayAccum/charDelayStep/state92/renderCount = 0
renderOver = false
keyWaitList.clear()
release(renderText); renderText = null
defFaceName = defaultFaceIndex 合法时 faceTable[index]，否则空串
faceHash.clear()
faceTable.clear()
defaultFaceIndex = intern(defFaceName)
```

差异均来自容器内联展开：Android/libstdc++ 的 `deque/vector/unordered_map::clear`
与 iOS/libc++ 的控制块、元素 stride 和 erase 方向不同；源码层操作顺序和边界条件
一致。

### 共享的 `getFaceName(uint32)`

此前本地把 face index 查询在四个调用点分别展开，但三份目标保留了同一源码 helper：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|
| 全内联 | `0x4E2254` | `0x1003F8C98` | `0x3E0198` |

helper 接收 32-bit unsigned index；`index >= faceTable.size()` 时从 UTF-16 `L""`
构造返回 `ttstr`，否则 AddRef 并返回 `faceTable[index]`。因此负的 face int 转入后会
零扩展成大 unsigned 值并走空串后备。iOS 两份有四个直接调用者：`clear`、
`getCharacters`、`defaultFace` getter、`onStyleChanged`；Android armv7 仅 getter 保留
调用，其余点内联，Android arm64 四处全内联。这个调用分布是优化差异，不应把 helper
误判为 iOS 私有函数。三份独立边界现已统一命名为 `TextRenderBase_getFaceName`，本地
四个调用点也已恢复为公共 `getFaceName(tjs_uint32)`。

三个保留 helper 的越界块都实际调用对应平台的 `ttstr(const tjs_char*)`/字符串分配
入口并传入空字面量，再写隐藏返回槽；不是默认构造直接写 null。核心分配器最终可把
空串规范化为 null 不改变这个源码调用边界，因此共享实现必须保留
`return ttstr(TJS_W(""))`，不能以结果等价为由改成 `return ttstr()`。

`clear` 的 face 收尾还有精确的双引用生命期：先由 helper 保存 `defFaceName`，再清
faceHash 和 faceTable；调用按值的 `resolveFaceIndex` 时又 AddRef 一个参数副本。正常
返回先 Release 参数副本，再 Release 保存值。四个 `clear` 都没有包围这两者的本地 EH
landing/SjLj cleanup；若 intern allocation 异常越过此层，face 容器已被清空且不会做
clear-level 回滚。`getCharacters` 在 faceIndex 改变时同样以 helper 的短返回值赋给全
函数唯一的 face variant cache，随即 Release 短返回值。

`resetFont` 四者共同控制流：

```text
if curFace != defaultFace || curBold != defaultBold ||
   curItalic != defaultItalic || curFontSize != defaultFontSize:
    curFontSize = defaultFontSize
    curFace = defaultFace
    curBold = defaultBold
    curItalic = defaultItalic
    onStyleChanged()
if curRubySize < 0 || curRubySize != defaultRubySize:
    curRubySize = defaultRubySize
curRubyOffset = defaultRubyOffset
curShadow = defaultShadow
curEdge = defaultEdge
复制 default 的 ch/shadow/shadowDiff/edge 四个颜色字段到 current
```

`resetStyle` 四者都只复制 `lineSpacing/pitch/lineSize/align/valign` 五个默认字段，
不调用 `resetFont` 或 `onStyleChanged`。当前本地两函数均与四文件共同控制流一致。

上述已确认函数已按 NCB 成员字符串和调用链在四个 IDB 中重命名，并分别
`idb_save`；iOS 的对象析构被内联进 holder cleanup，未强行创建虚假的独立 dtor 名称。

## 字典入口：`setOption`

| 二进制 | 函数 |
|---|---|
| Android arm64 | `TextRenderBase_setOption@0x59D68C` |
| Android armv7 | `TextRenderBase_setOption@0x4DFC30` |
| iOS arm64 | `TextRenderBase_setOption@0x1003F5B44` |
| iOS armv7 | `TextRenderBase_setOption@0x3DCF8C` |

四个函数都先把参数转换为 object，取得 dispatch 的强引用；每次 `PropGet` 都使用
flag `1024`、空 hint，失败表示“键不存在”并保持字段原值；函数尾释放 dispatch。
逐文件 UTF-16LE 原始字节搜索和调用点 xref 确认的查询顺序为：

```text
following, leading, begin, end,
vertical, kinsoku_max, word_break,
ignore_color, ignore_size, ignore_delay,
ignore_over, ignore_overy, ignore_overx,
width_time_scale, ignore_ruby, ignore_type, ignore_face, ignore_style
```

共同边界行为：

- 前四个键只接受 string 或 void；void 写空串，其它类型抛 String 转换错误。
- 其余键按 TJS 真值规则转换；`kinsoku_max` 最终写入 32-bit 的 `0/1`。
- `ignore_over` 和紧随其后的 `ignore_overy` 写同一个 Y 字段，故两者同时存在时
  `ignore_overy` 覆盖前者；`ignore_overx` 写独立 X 字段。这不是反编译命名误差。
- 四次字符串查询并不各自构造一个工作 variant；四次与后续十四次 bool 查询共用
  同一个 `tTJSVariant`。每次 string 分支只另建一个很短命的 `ttstr` 赋值临时。
- 当前本地实现的查询顺序、转换和覆盖关系均与四文件一致；本轮把 helper 改为显式
  复用外层 variant，删除了源码结构中并不存在的四个 helper-local variant 生命周期。

## 默认样式入口：`setDefault`

| 二进制 | 函数 |
|---|---|
| Android arm64 | `TextRenderBase_setDefault@0x59E288` |
| Android armv7 | `TextRenderBase_setDefault@0x4E01B4` |
| iOS arm64 | `TextRenderBase_setDefault@0x1003F6204` |
| iOS armv7 | `TextRenderBase_setDefault@0x3DD694` |

逐文件 UTF-16LE 原始字节与 xref 确认的键集和控制流一致：

```text
face, bold, fontsize, bigfontsize, smallfontsize, rubysize, rubyoffset,
color, shadow, shadowcolor, shadowdiff, edge, edgecolor,
linespacing, pitch, linesize, align, valign
```

除字号族和 `linesize` 外，每个 present 键按目标字段转换后直接覆盖；absent 键保持
原值。`face` 与 `setOption` 的字符串边界一致，并经 `resolveFaceIndex` intern。
字号族的共同伪代码具有一个刻意保留的非直观分支：

```text
if PropGet("fontsize") succeeds:
    defaultFontSize = real(value)
    if PropGet("bigfontsize") fails:   defaultBigFontSize = defaultFontSize
    if PropGet("smallfontsize") fails: defaultSmallFontSize = defaultFontSize
    if PropGet("rubysize") fails:      defaultRubySize = defaultFontSize
    // 三个 PropGet 成功时只阻止 fallback，不读取其值，字段保持旧值
else:
    if PropGet("bigfontsize") succeeds:   defaultBigFontSize = real(value)
    if PropGet("smallfontsize") succeeds: defaultSmallFontSize = real(value)
    if PropGet("rubysize") succeeds:      defaultRubySize = real(value)

if PropGet("linesize") succeeds || PropGet("fontsize") succeeds:
    defaultLineSize = real(value_of_the_successful_query)
```

因此 `{fontsize: 20, bigfontsize: 30}` 不会把 big size 写成 30：它会更新普通字号，
同时保留此前的 big size；而没有 `fontsize` 时 `{bigfontsize: 30}` 才写入 30。四个
目标均有这一控制流，本地实现也一致，不能按直觉改写成“只查询一次并应用所有值”。
`linesize` 优先，缺失时再次查询 `fontsize` 回退。三个 fallback existence probe 也复用
全函数唯一的工作 variant，二进制中没有原源码曾声明的第二个 `tmp` variant；本轮已
删除这个多余对象而保持上述非直观语义不变。

## 当前样式入口：`setFont/setStyle/onStyleChanged`

| 二进制 | `setFont` | `setStyle` | `onStyleChanged` |
|---|---|---|---|
| Android arm64 | `0x59F3B8` | `0x59FB8C` | `0x5A2308` |
| Android armv7 | `0x4E08E8` | `0x4E0BF4` | `0x4E2368` |
| iOS arm64 | `0x1003F6AF4` | `0x1003F6EE8` | `0x1003F8D50` |
| iOS armv7 | `0x3DE024` | `0x3DE474` | `0x3E0210` |

`setFont` 的共同查询顺序是：

```text
face, bold, fontsize, rubysize, rubyoffset, color, shadow,
shadowcolor, shadowdiff, edge, edgecolor
```

四者共同使用一个初值为 false 的 `changed` 标志。只有以下三种实际变化置位：

```text
face present 且 intern 后的 index != curFaceIndex
bold present 且 bool(value) != curBold
fontsize present 且 curFontSize < 0 或 real(value) != curFontSize
```

末尾仅在 `changed` 为 true 时调用一次 `onStyleChanged`。`rubysize` 同样有
`curRubySize < 0 || value != current` 的写门控，但不置 `changed`；`rubyoffset`、颜色、
shadow/edge 及其颜色在 present 时直接写入，也不触发回调。`setFont` 不查询
`italic`；italic 由其它状态路径和 render tag 控制。absent 键都保持字段原值。

`setStyle` 四者都按 `linespacing, pitch, linesize-or-fontsize, align, valign` 查询；
`linesize` 成功时短路，不再查询 `fontsize`，否则以 `fontsize` 回退。它不做相等比较，
也不调用 `onStyleChanged`。

四个 setter 的共同局部对象拓扑进一步约束了接近原源码的声明顺序：先从 object
variant 取得 dispatch 并交给 `ncbPropAccessor` 风格的强引用 holder；供转换使用的
输入拷贝随后已无用途并立即析构。holder 之后只默认构造一个工作
`tTJSVariant`，它贯穿该函数全部 `PropGet`；正常尾声固定先析构工作 variant、再由
holder `Release` dispatch。`setDefault/setFont` 的 `faceName` 是更短的内层 `ttstr`，
在 `resolveFaceIndex` 后立即 Release；`setOption` 则有四个独立的字符串赋值临时。

四份 `setOption` 都把 following/leading/begin/end 的 PropGet、type 分发、字段赋值和
短 `ttstr` 析构直接编在主体内，没有 `setOptionStr` 函数边界。尤其四个 string 分支
分别形成 AddRef 输入字符串、赋值字段、立即 Release 临时的独立窗口；共享源码已删除
额外 helper 并展开四段，未把它们合并成跨后续查询存活的单一字符串局部。

异常元数据的工具链差异也在四个入口全部重复：Android arm64 有本地 LSDA，按
短 `ttstr`（若已构造）→工作 variant→holder 的逆构造序清理，清理函数自身抛异常时
进入 `terminate`；Android armv7 与 iOS arm64 没有本地 landing；iOS armv7 的 SjLj
call-site 表按构造阶段选择短临时，再汇入工作 variant→holder 的共同清理，清理失败
同样 `terminate`。这组证据排除了裸 dispatch 加函数尾手工 `Release` 的源码形状；
当前本地四个 setter 均已改用 `ncbPropAccessor` holder。

### 字典查询与三种值转换

四文件每次查询的实参完全一致：dispatch 自身、flag `1024`
(`TJS_MEMBERMUSTEXIST`)、UTF-16 key、null hint、同一个工作 variant 的地址、ObjThis=同一
dispatch。present 判据是返回码按有符号数 `>= 0`（AArch64 表现为 sign bit 未置），不是
只接受 `TJS_S_OK`；相邻查询之间没有额外 `Clear`。这正好是仓库 ncbind.hpp 已有的
`ncbPropAccessor::checkVariant(key, work)` 内联体，仓库其它 ncbind 插件也采用同一写法。
当前 textrender 已删除旧移植额外发明的 `dictGet` wrapper，直接恢复该 API 形状。

三种转换在 Android arm64 setter 内联；其余三个目标保留可识别的公共函数边界：

| 转换 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `tTJSVariant::operator bool` | 内联 | `0x496CC4` | `0x100037640` | `0x3589C` |
| `tTJSVariant::AsInteger` | 内联 | `0x491994` | `0x10001E130` | `0x1E1A0` |
| `tTJSVariant::AsReal` | 内联 | `0x498E60` | `0x10001E1C8` | `0x1001E0` |

共同类型边界为：

- bool：void=false；object/octet 按内部指针非空；string 先 `AsInteger` 再比较零；integer
  比较 64-bit 零；real 在 TJS FPU 设置后比较 `0.0`，所以 NaN 为 true。
- integer：void=0；object/octet 抛 Integer 转换错误；string 解析整数；integer 原样；
  real 转 64-bit integer。setter 最后窄化/存入 32-bit 字段，保留低 32 位。
- real：void=0；object/octet 抛 Real 转换错误；string 解析数值；integer 转 double；
  real 原样。textrender 再把 double 窄化为目标 float 字段。

这些 helper 已在三份保留独立边界的 IDB 中按真实 TJS 名称重命名。它们是 TJS 核心
variant 转换，不是 textrender 私有边界；Android arm64 还把它们直接内联进调用点。
共享源码因此也已删除额外发明的 `boolCoerce/intCoerce/realCoerce` 薄包装：setter 和
`render` invoker 现在直接写 `(bool)v`、`v.AsInteger()`、`v.AsReal()`，异常从核心转换
直接进入上一节所列的展开路径。

`onStyleChanged` 四者的共同数据流：

```text
ncbDictionaryAccessor dict                 // owns initial Dictionary reference
faceName = curFaceIndex 在 faceTable 范围内 ? faceTable[curFaceIndex] : empty
dict.SetValue(face, faceName)               // MEMBERENSURE / flag 512
dict.SetValue(bold, curBold ? 1 : 0)
dict.SetValue(italic, curItalic ? 1 : 0)
vDict = Object(dict, ObjThis=null)           // AddRef; holder still owns its reference
objthis.onFontChange(vDict)                  // result=null, argc=1, context=objthis
destroy vDict -> faceName -> dict holder
```

四个目标均直接解引用 `objthis`，没有 null guard；字典作为 Object variant 传参时其
ObjThis 槽为 null。每个 `SetValue` 内部各自默认构造一个 variant，完成类型转换和
`PropSet` 后立即析构；三个属性 variant 不会一直活到回调返回。原有本地实现使用裸
dispatch、把三个 variant 保留到函数尾，并在回调前提前释放字典的初始引用，结构和
引用计数窗口都不符；现已恢复 accessor holder 和逐次 `SetValue` 的生命周期。

## 其余公开方法入口表

以下地址均从各自 NCB 成员注册器重新提取，不是由旧 Android 地址平移：

| 方法 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `setRenderSize` | `0x59EF50` | `0x4E0664` | `0x1003F67F4` | `0x3DDD10` |
| `render` Process | `0x5A0008` | `0x4E0DB4` | `0x1003F7130` | `0x3DE6F4` |
| `newline` | `0x5A02AC` | `0x4E0E84` | `0x1003F7234` | `0x3DE830` |
| `done` | `0x5A02C4` | `0x4E0EA0` | `0x1003F7250` | `0x3DE844` |
| `onEval` | `0x5A0674` | `0x4E109C` | `0x1003F756C` | `0x3DEA94` |
| `getKeyWait` | `0x5A06BC` | `0x4E10C0` | `0x1003F75B4` | `0x3DEB3C` |
| `calcLineOffset` | `0x5A09DC` | `0x4E12BC` | `0x1003F7878` | `0x3DEE10` |
| `calcShowCount` | `0x5A0A24` | `0x4E12F2` | `0x1003F78B8` | `0x3DEE4A` |
| `getCharacters` | `0x5A0A74` | `0x4E1330` | `0x1003F7908` | `0x3DEE8C` |

Android armv7 的两个 `calc*` 注册目标原先只是 IDA 的未定义 `loc_` 标签。本轮由相邻
注册指针、各自独立返回指令及无重叠边界确认后，补定义为 `[0x4E12BC,0x4E12F2)` 与
`[0x4E12F2,0x4E1330)` 两个函数；这不是把其它平台函数强套到该文件。

## 小型公开方法与 `done` 收束链

四文件共同语义：

- `setRenderSize(w,h)` 依次写 render width/height，然后无条件调用 `clear()`。
- `newline()` 仅当 pending line 非空时调用 `finishLine()`；Android 以 deque
  begin/end cursor 比较，iOS 以 deque size 判断，属于标准库展开差异。四者都是
  void 方法并直接丢弃 `finishLine` 返回值；若 over-Y 失败，它向脚本面静默返回，
  pending 已清，但 `finishLine` 成功尾声中的 kinsoku/ruby/accum 清理尚未执行。
- `onEval(expr)` 先把结果 variant 置 void，再以 `objthis` 为上下文执行表达式。
- `calcLineOffset(i)`：以无符号 size 比较索引；合法时读取该行的 float
  bottom/offset 字段，否则读取全局 float `renderBottom`。负数经无符号转换后也走
  越界回退。四目标随后都执行 `float -> double` 提升（AArch64 `FCVT D0,S0`，ARMv7
  `VCVT.F64.F32`），所以源码返回类型是 `double`，不是旧注释一度声称的 float。
- `calcShowCount(width)`：字符数小于 2 时返回 0；否则从最后一个 CharItem 逆扫，比较
  `char.renderPos * timeScale > float(width)`，直到命中或退到头部。

`done()` 的共同阶段顺序为：

```text
if pendingLine nonempty: finishLine()
遍历 lineList 聚合全局 left/top/right/bottom
if !vertical:
    valign==0 -> delta=int((renderHeight-bottom)*0.5)
    valign==1 -> delta=int(renderHeight-bottom)
    其它值 -> delta=0
    每个 CharItem.y += delta
    globalTop/globalBottom += delta
charList.clear()，再按 line 顺序、行内顺序铺入每个 CharItem* 指针
对每个 KeyWait：timeBits = charList[index].renderPos 的 32-bit float 位型
std::sort(charList, CharItem.renderPos 升序)
```

四份 KeyWait 回填都是主体内的一次 32-bit load/store，没有插件级
`reinterpretFloatBits` 调用边界。共享源码已删除该人造 helper，并在循环内用
`memcpy` 表达同一严格别名安全的位复制；不会触发数值 float-to-int 转换。

KeyWait 回填索引没有边界检查；依赖 render 阶段产生的 index 落在 flatten 后的
charList 内。Android 的 deque block stride 与 iOS 不同，但阶段和字段含义一致。

四个 `done()` 都直接调用 `finishLine()` 后丢弃 bool 返回值，没有条件跳转。因此
over-Y 且未忽略时，`finishLine` 清掉 pending 并返回 false，`done` 仍继续对此前已经
提交的 lineList 做 bbox、flatten、KeyWait 回填和排序。`done` 自身在四个目标中均无
局部 EH 清理/事务回滚；各阶段是增量提交：

- bbox 与 valign 的标量/CharItem.y 写入一旦发生就保留；
- `charList.clear()` 先提交 size=0、保留 capacity，随后逐指针 push。扩容 allocation
  失败时 vector helper 保证本次扩容前的 prefix 仍有效，但 `done` 不会恢复旧的完整
  charList，也不会继续 KeyWait/sort；
- KeyWait 回填发生在排序前，故 index 解释的是 line/deque flatten 顺序；无效 index
  会直接越界解引用，而不是返回错误；
- 最后的 sort 只原地交换非 owning `CharItem*`，不会移动 deque 中的 CharItem，也不
  分配内存。

四文件保留了两套标准库排序实现：

| ABI | sort 根 | 策略 |
|---|---|---|
| Android arm64 | `0x5A5DC8` + `0x5A6014` | libstdc++ introsort，depth=`2*lg(n)`，最后以 16 项阈值做 final insertion |
| Android armv7 | `0x4E4E5C` + `0x4E4EF4` | 同一 libstdc++ 策略，pointer stride 改为 4B |
| iOS arm64 | `0x1003FDA00` | libc++ sort；0..5 专门路径、6..30 insertion、31..999 median-of-3、>=1000 Tukey ninther |
| iOS armv7 | `0x3E4A6C` | 同一 libc++ 策略，阈值按 4B pointer 等比例编码 |

depth-limit/小分区子 helper 也已逐个核对。libstdc++ ARMv7 保留
`heap_select@0x4E4F68 -> sort_heap@0x4E4FC2`，其下是
`make_heap@0x4E4FFE`、`adjust_heap@0x4E5048`；常规 pivot/partition/final insertion
分别使用 `move_median_to_first@0x4E50F8`、`unguarded_partition@0x4E5170`、
`insertion_sort@0x4E51B8`。Android arm64 除 `heap_select@0x5A61F4` 外均内联进两个
sort 根。libc++ 两份保留完全同构的 `__sort3/__sort4/__sort5`、insertion sort 与
incomplete insertion sort：arm64 为 `0x1003FDDCC/0x1003FDE6C/0x1003FDF18/`
`0x1003FDFF8/0x1003FE0A4`，armv7 为 `0x3E4D16/0x3E4D9E/0x3E4E16/0x3E4EB6/`
`0x3E4F24`。最后一个 helper 在累计 8 次插入移动后返回 false，这是 libc++ 对近似有序
partition 的短路判据，不是 textrender 自己的排序阈值。

比较器在四者中都只是 `lhs->renderPos < rhs->renderPos`。排序不稳定，renderPos 相等时
不能承诺保持 flatten 顺序；若存在 NaN，这个比较也不再满足严格弱序，实际排列会受
两套标准库实现影响，源码不得额外稳定化或规范化。

`done` 也不是一般意义上的幂等操作，因为它不在入口重置聚合 bounds。尤其横排
center valign（值 0）会把 `delta=int((renderHeight-currentBottom)*0.5)` 再次加到所有
CharItem.y；重复调用会继续折半逼近 renderHeight，直到整数截断令 delta 为 0。bottom
valign（值 1）第一次通常已把 bottom 推到 renderHeight，后续 delta 才为 0。这个边界
来自四份相同的阶段顺序，不应在移植层添加“already done”保护。

## 查询对象形状：`getKeyWait/getCharacters`

`getKeyWait()` 四者都返回 Array，每个元素是 `{pos, time}`。一个反直觉但四文件一致的
边界是：两个属性都读取 KeyWait 的低 32-bit `index`，并不读取 `done()` 刚写入的高
32-bit `timeBits`。因此公开结果当前是 `pos == time == index`；高半字回填可能只供其它
内部/历史路径使用，不能凭字段名把查询改成高半字。

`getCharacters(start,count)` 的共同范围规则：`count==0` 时先改成
`renderCount-start`；若 `start+count` 超过 charList 长度则钳到 `length-start`；最终
count 小于 1 返回空数组。没有单独拒绝负 `start`，合法性依赖调用者不变量。

逐 CharItem 产生的属性顺序与类型为：

```text
graph:int, text:string, x:real, y:real, cw:real, size:real, face:string,
color:uint32->TJS integer, bold:int, italic:int, shadow:int, edge:int,
shadowColor:uint32->TJS integer, shadowDiff:int32->TJS integer,
edgeColor:uint32->TJS integer, [ruby:Array if nonempty], vertical:int, delay:real
```

face name 只在 faceIndex 改变时刷新函数级 variant 缓存；索引无效时缓存空字符串。
主数组通过数值 index 写入 dictionary dispatch，返回 Object variant 的 Object/ObjThis
两槽都指向数组。`getKeyWait` 的返回 variant 则只有 Object 槽，ObjThis 为 null。

ruby helper 的四文件入口为 Android arm64 `0x5A6620`、Android armv7 `0x4E54A4`、
iOS arm64 `0x1003FE240`、iOS armv7 `0x3E5034`。它逐项返回
`{text:string,x:real,y:real,size:real}`，其中 size 来自 RubyItem 的第四字段；子数组
同样令 Object/ObjThis 两槽都指向数组。

三条查询构造链的源码级局部顺序也由四文件共同锁定：

```text
getKeyWait:
  array accessor -> NativeInstanceSupport -> snapshot count
  each: dictionary accessor -> read index -> pos/time SetValue
        -> named Object(dict,null) -> by-value FuncCall parameter copy
        -> array.add(copy) -> destroy copy/named variant/dictionary
  construct return Object(array,null) -> destroy array accessor

getCharacters:
  array accessor -> NativeInstanceSupport -> range clamp -> faceName Variant(void)
  each: dictionary accessor -> read CharItem -> optional faceName refresh
        -> fixed properties -> optional buildRubyArray result + ruby SetValue
        -> vertical/delay -> array.SetValue(index,dict) -> destroy dictionary
  construct return Object(array,array) -> destroy faceName -> array accessor

buildRubyArray:
  array accessor -> NativeInstanceSupport
  each: dictionary accessor -> read RubyItem -> text/x/y/size SetValue
        -> array.SetValue(index,dict) -> destroy dictionary
  construct return Object(array,array) -> destroy array accessor
```

这里的“先建 dictionary 再读元素/刷新 face”不是反编译器随意重排：四个目标的
`TJSCreateDictionaryObject` 都位于当轮 CharItem/KeyWait 读取之前。旧本地源码把
`getCharacters` 的字典构造放在 face 刷新之后，使 face 字符串赋值抛出时少了一个
已构造 holder；现已恢复上述顺序。

所有 `SetValue`、`FuncCall("add")` 和 `NativeInstanceSupport` 的错误码都被忽略；若
dispatch 只返回失败码而不抛 C++ 异常，循环仍继续，最终可能返回缺属性或缺元素的
部分对象，不会回滚。异常展开则存在明确平台差异：

- Android arm64 三个查询函数都有按构造阶段分流的 landing；活动的参数 variant、
  ruby result、dictionary、faceName 和 array holder 会逆序清理。数值 index 的
  dispatch 写 helper `0x5A6930` 也会在重抛前清理自己的临时 variant。
- Android armv7 与 iOS arm64 的三个查询函数、array 构造 outline helper 和数值
  SetValue helper 都没有本地 personality/landing；C++ 异常跨过时看不到相应 holder
  清理，部分数组/字典和字符串引用可滞留。
- iOS armv7 的 array 构造与 `NativeInstanceSupport` 被 outline 到 `0x219A2C`，且三个
  caller 都在调用它之后才注册 SjLj，因此该 helper 抛出时 caller 尚无清理帧。注册
  之后，face/属性/ruby 等构造阶段会清理已经存活的局部；但直接 array
  `FuncCall/SetValue` 及 holder 析构所对应的后段 call-site 被 LSDA 映射到
  `__cxa_begin_catch; std::terminate()`。其中 `getKeyWait` 的 call-site 7--10、
  `buildRubyArray` 的 8--11、`getCharacters` 的 25--28 都属于 terminate 组。
  iOS armv7 数值 SetValue helper `0x3E52C0` 自己会先析构临时 variant 再重抛，随后
  caller 仍可能按上述 call-site 终止。

这些差异来自同一源码在不同编译器/标准库 ABI 下的 EH 生成；共享实现只恢复局部
声明和 holder 拓扑，不手写某个平台的泄漏或 terminate 行为。

## `render` Process 的保留但未消费参数

四个 Process 都要求至少 3 个参数；第 4 参数存在时强制转换为 real/float，第 5 参数
存在时按真值转换为续写标志，然后把五个值一起传给主体：

| 二进制 | 主体入口 |
|---|---:|
| Android arm64 | `0x5A266C` |
| Android armv7 | `0x4E2610` |
| iOS arm64 | `0x1003F8EF4` |
| iOS armv7 | `0x3E0410` |

32-bit 两主体的反编译签名明确保留 `sizeBits` 与续写标志两个相邻形参，正文只读取
续写标志；64-bit 两主体同样读取通用寄存器中的标志，不读取浮点寄存器中的 size。
因此 size 对输出没有作用，但原始源码数据流是“转换并传入未使用形参”，不是包装层
提前丢弃。四份入口与全部后续读点还共同证明：第一个整数只作 begin/end 配对状态机的
零/非零门控；第二个整数写入初始 charDelayStep，并作为 `%d/%D` 的默认或比例基数。
二进制没有参数名元数据，源码采用
`pairMode_guess/baseDelay_guess/continueRender_guess` 作为语义别名，
不把它们误写成坐标，也不声称恢复了原作者拼写。未使用的 float 参数仍按原数据流传递。

四个 Process 的局部顺序还一致地约束了源码作用域：

```text
check numparams
convert optional size, then optional continueRender_guess
construct text from param[0]
convert pairMode_guess from param[1]
convert baseDelay_guess from param[2]
ok = renderImpl(text, pairMode_guess, baseDelay_guess, size, continueRender_guess)
destroy text
if result != null: box ok as TJS integer
return TJS_S_OK
```

关键是 `text` 在结果装箱之前析构，不是留到函数尾。当 result 与某个输入
variant 别名，或析构/装箱抛出时，这个顺序可观察。原本地实现把 `text`
放在 Process 整个函数作用域，源码生命期不符；现已用内层作用域恢复。

Android arm64、Android armv7 和 iOS arm64 的 Process 没有局部 EH landing；
`text` 构造完成后的两个整数转换或 `renderImpl` 若以 C++ 异常越过此层，参考
文件中看不到它的 Release。iOS armv7 先以 call-site `-1` 执行可选参数转换和
`text` 构造（未构造完时无需清理），再对两个整数和 `renderImpl` 设置分阶段
SjLj call-site；这些 landing 都先析构 `text` 再 resume。

## `render` 主状态机与直属 helper

主体和从主体直接识别出的源码级 helper 映射如下。表中“内联”表示该平台保留相同
控制流但没有独立函数边界，不代表功能缺失：

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `renderImpl` | `0x5A266C` | `0x4E2610` | `0x1003F8EF4` | `0x3E0410` |
| `finishLine` | `0x5A3898` | `0x4E32CC` | `0x1003F9F30` | `0x3E133C` |
| `appendChar` | `0x5A3C60` | `0x4E3580` | `0x1003FA37C` | `0x3E176C` |
| `scanTagUntil` | `0x5A40C4` | `0x4E3820` | `0x1003FA694` | `0x3E1AC0` |
| `scanDigits` | `0x5A42F8` | `0x4E38F8` | `0x1003FA80C` | `0x3E1C40` |
| `evalDollarTag` | `0x5A4528` | `0x4E39D4` | `0x1003FA984` | `0x3E1DC4` |
| hex 颜色纯解析 | 内联 | `0x4E3ABC` | `0x1003FAA88` | `0x3E1F00` |
| begin/end 字符索引 | 内联 | `0x4E3B20` | `0x1003FAB08` | `0x3E1F48` |
| TJS2 核心 `TJS_atoi`（非插件私有 helper） | `0x9AFA1C` | `0x729BBC` | `0x100363C24` | `0x366BDC` |

四个地址的新鲜反编译都与 `cpp/core/tjs2/tjsConfig.cpp` 中的 `TJS_atoi` 完全对应，
而不是 textrender 自己实现的十进制解析器。每个二进制里该函数都恰有 14 个代码引用：
9 个来自 `TextRenderBase::renderImpl`，另 5 个来自 TJS2/引擎核心调用点。其边界语义为：

- 跳过所有 UTF-16 code unit `<= 0x20` 的前导字符；
- 接受一个可选的 `'-'`，之后再次跳过 `<= 0x20`；不接受 `'+'`；
- 只累积十进制数字并在第一个非数字处停止；空串或首个有效字符非数字时返回 0；
- 累加器和符号应用保持 TJS 核心的 `tjs_int`/32 位行为，不额外做插件侧饱和或报错。

因此源码已删除先前臆造的 `parseInt10(const ttstr&)` 私有 helper，九个渲染调用点均直接
调用 `TJS_atoi(tagAccum.c_str())`。这也保留了 `%数字` 的非空且正数门控、`%d/%a/%p`
的非空门控、`%w` 的非空门控，以及 `%l/%t` 和 `%D` 相应路径即使标签为空也会调用
核心解析器的差异。

颜色解析与字符索引的归属不同。`parseHexColor` 在 Android armv7、iOS arm64、iOS
armv7 各只有一个代码引用，均来自本模块的 `renderImpl`；Android arm64 把同一控制流
完全内联在 `%f` 分支。其 `0x`/`0X` 可选前缀、逐位左移四位并在首个非十六进制字符
停止的外层循环属于 textrender，但 digit 解码的三个范围分支与 TJS2 核心
`TJSHexNum` 一致：Android armv7/iOS armv7 展开成 `0-9/A-F/a-f` 三段比较，两个
arm64 优化成有效位 mask/减值表。源码因此保留插件侧 `parseHexColor`，但删除了臆造的
第二层 `hexDigitValue`，改为直接调用 `TJSHexNum`；优化后仍对应参考文件中的内联体。

相邻的 `scanCharIndex` 在保留边界的三个目标中各恰有三个代码引用，全部是
`renderImpl` 的 begin、end、配对 begin 二次查询，Android arm64 则三处内联；没有
来自 TJS2 其他模块的引用。它接收 `ttstr` 本体并返回字符索引或 `-1`，而不是只返回
`TJS_strchr` 指针，因此仍归为 textrender 私有 helper，不能因内部是线性扫描就错误改名
为核心 `TJS_strchr`。

begin/end 平衡控制流本身则没有额外 helper 边界：四份都把“查 begin → appendChar →
更新起点/深度，或查 end → 深度递减 → 长度与配对索引校验”直接编入 `renderImpl`；
armv7 与两份 iOS 只调用上述 `scanCharIndex`，Android arm64 连索引扫描也内联。共享源码
原先额外抽出的 `renderBalancedChar` 会制造第五个目标均未保留的类成员边界，现已删除并
把该段恢复到主状态机中。此结论不把“所有编译器都可能恰好内联”误说成数学不可能，而是
以四目标共同保留其它相邻 helper、却共同不保留此边界作为最接近原源码结构的选择。

入口共同执行：续写 flag bit0 为 0 时析构并清空 lineList、清零 delay 累积、清空
keyWaitList；随后总是清零当前 renderPos，快照当前字号供 `\\w` 使用，并以第三个
整数参数初始化 charDelayStep。空文本也会调用一次 `finishLine()`，不是直接成功返回。

一级字符分派在四者中一致：`#`、`$`、`%`、`&`、`[`、`\\`、裸 LF 和普通字符。
`%` 子码集合也一致：数字、`; B C D L R S a b d e f i l p r s t w`。其中有意保留的
非常规边界包括：

- `%C/%R/%L` 的控制流在四者都逐级 fall-through，未忽略 style 时最终 align 都写成
  `-1`；之后还继续执行 `%B` 的 big-font-size 路径。四份 CFG 都是 C 写 0 后落入
  R 写 1、再落入 L 写 -1、最后落入 B；这比“每个 case 手工重复同样赋值并调用一个
  helper”更强地约束了原源码 switch 形状。共享源码现已恢复直接 fall-through，并删除
  二进制中均无边界的 `applyFontSize/applyBigFontSizeTag/applySmallFontSizeTag` 人造成员。
- `%l/%t` 在未忽略 delay 时即使标签为空也调用十进制解析；`%w` 只有标签非空才调。
  `%D` 紧随 `$` 并不执行 eval：四个目标都只有顶层 `$` 分派会调用
  `evalDollarTag`。`%Dfoo;` 从 `D` 后扫描并无条件调用 `TJS_atoi`；`%D$foo;` 只多跳过
  `$`，并仅在未 ignore-delay 时调用同一解析器；两路都丢弃结果。`%D` 恰好位于文本尾
  时，对下一码元的读取命中 `c_str()` 的 NUL terminator，随后按非 `$` 空标签路径走。
- `scanTagUntil` 消费终止符；终止符缺失时消费到尾。`scanDigits` 在遇到首个非数字时
  也已经把 cursor 向前推进，因此该非数字被吞掉。
- `\\k` 先压入 `{renderCount,0}`；`\\t` 走 `appendChar(9)`；`\\x` 只把 begin-run
  标志清零，不终止 render。
- begin/end 模式先调用 `appendChar`，再更新嵌套深度；不匹配的 end 也会使深度递减，
  没有下界保护。只有 begin/end 串等长、起始 begin 与归零 end 的索引相同才把
  lineStartX 复位为 0。

三个保留独立函数的目标都证明 hex helper 只接收 UTF-16 指针并返回 RGB 累积值；
调用者再 OR `0xFF000000` 并写当前颜色。Android arm64 把同一循环内联。本地此前让
helper 直接写对象成员，虽输出相同但源结构和数据流不符；现已恢复为纯解析函数。
`evalDollarTag` 四者都把返回 type 3/4/5 与 object type 1 作为转 string 错误，string
type 2 返回其内容，void/其它类型返回空串。后一条与 `getFaceName` 的越界空串源码边界
不同：四份 `evalDollarTag` 都直接把隐藏 ttstr 返回槽写为 null，不调用空字面量字符串
构造入口，因此本地保持 `return ttstr()`。

### 标签临时 `ttstr` 和扫描 buffer 的生命周期

四份 `renderImpl` 的源码级局部拓扑一致。入口只构造一个活到函数返回的
`tagAccum` 空 `ttstr`。标签循环内共有 17 类短生命值：14 个
`scanTagUntil` 返回值、1 个 `scanDigits` 返回值、1 个
`evalDollarTag` 返回值，以及 `%f` 把 `tagAccum` 按值传给
`resolveFaceIndex` 时的参数副本。前 16 个都遵循同一正常路径：

```text
helper constructs short result
AddRef(short)
Release(old tagAccum)
tagAccum = short
Release(short)
```

这要求共享源码写成 `tagAccum = evalDollarTag(tagAccum)`：返回值在赋值 full-expression
结束时立即析构。先声明具名 `evalResult` 再赋值会把该引用额外保留到整个 `$` 分支结束，
覆盖字符 append 循环并改变失败/异常路径的 Release 时点；现已删除这个生命周期偏差。

`%f` 副本则在 `resolveFaceIndex` 返回后立即 Release。`[` ruby 分支在
`!ignoreRuby` 时确实对 `tagAccum` 做一次 AddRef 后紧接一次 Release，但优化后
没有独立栈对象，也没有数据消费者。所有成功、`appendChar` 失败、
`finishLine` 失败与空文本返回都在离开前 Release `tagAccum`。

四份调用图均给出恰好 14 个 `scanTagUntil` xref，另有各 1 个
`scanDigits/evalDollarTag` xref。`%f -> resolveFaceIndex` 的调用点分别为
Android arm64 `0x5A2F90`、Android armv7 `0x4E2E9A`、iOS arm64
`0x1003F97E0`、iOS armv7 `0x3E0B14`。这些副本引用计数触碰是
可观察生命周期，不能因为数值净效果为零而从源码删掉。

`scanTagUntil` 与 `scanDigits` 自身都有一个局部
`vector<tjs_char>`：扫描时逐码元 push；空 buffer 直接在隐藏返回槽写空
`ttstr`；非空 buffer 再 push NUL，由 `data()` 构造返回 `ttstr`，然后销毁
vector。Android/libstdc++ 增长容量是 `old + max(old,1)`；iOS/libc++ 走
`__recommend`/分离 buffer 路径。扩容都在分配成功后才提交新 buffer，而返回
`ttstr` 保有自己的字符串存储，不借用 vector 内存。

异常展开是平台差异，不是四套源码结构：

- Android arm64 `renderImpl` 的 LSDA 区分“短临时未完成”和“短临时已完成”；
  后者先 Release 当前短临时，再 Release `tagAccum`，然后 resume，前者只清
  `tagAccum`。正常路径上 `ttstr` 析构作为 `noexcept` cleanup，如果它自己抛出则
  转 `__cxa_begin_catch; std::terminate()`。两个扫描 helper 也有本地 landing，会
  delete 当前 vector buffer 再 resume。
- Android armv7 和 iOS arm64 的 `renderImpl`、`scanTagUntil`、`scanDigits`
  都没有本地 EH landing/personality。异常若跨过这些层，目标文件中看不到
  `tagAccum`/短临时的 Release 或扫描 vector buffer 的 delete。
- iOS armv7 `renderImpl` 的 SjLj 表有 78 个 call-site 值；其中 17 个分支
  分别清理 17 个短临时栈槽，再与其余分支汇合到 `tagAccum` 清理和
  `__Unwind_SjLj_Resume`。两个扫描 helper 各有 3 个 call-site 值，都汇合到
  vector buffer 清理后 resume。

因此共享 C++ 保持普通 `ttstr`/`vector` RAII 和局部声明作用域；不手写
某 ABI 的泄漏、landing 或 terminate 分支。

## 脚本回调的局部对象顺序与异常边

本轮重新定位的四组入口如下；这些地址只属于对应文件：

| 函数 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `onEval` | `0x5A0674` | `0x4E109C` | `0x1003F756C` | `0x3DEA94` |
| `onGetTextWidth` | `0x5A464C` | `0x4E3BC0` | `0x1003FAB98` | `0x3E1FA4` |
| `onStyleChanged` | `0x5A2308` | `0x4E2368` | `0x1003F8D50` | `0x3E0210` |
| `evalDollarTag` | `0x5A4528` | `0x4E39D4` | `0x1003FA984` | `0x3E1DC4` |

`onEval` 四者都直接在 ABI 隐藏返回槽构造 void variant，再把该槽交给
`TVPExecuteExpression(expr,objthis,result)`；没有额外局部结果拷贝。四个函数体均无
本地异常清理块，返回对象的继续填充完全由表达式执行函数承担。

`onGetTextWidth` 的源码级构造顺序四者一致：`result(void) -> text(string, AddRef) ->
size(real)`；回调后对 result 执行 `AsReal`，正常析构顺序为 `size -> text -> result`。
Android armv7 与两份 iOS 都保留对核心 `tTJSVariant::AsReal` 的直接调用，Android
arm64 把它的 type switch 内联；void 落到核心转换的默认 `0.0`。因此插件源码没有
`if(result.Type()==tvtVoid)` 预检，现已删除这条结果等价但多余的人造分支，直接写
`return (float)result.AsReal()`。
Android arm64 的 `0x5A47A4..0x5A47C4` landing 与 iOS armv7 的
`0x3E20DA..0x3E210E` SjLj landing 会按已完成的构造阶段逆序清理；Android armv7 和
iOS arm64 函数体没有 personality/landing，回调或转换若以 C++ 异常越过该层，看不到
本地 variant 清理。这是目标编译器/ABI 的真实差异，不应在共享源码中人为制造泄漏。

`evalDollarTag` 的构造顺序也是四者一致，但与旧本地声明相反：

```text
result = Variant(void)
arg = Variant(content)              // string AddRef
objthis.onEval(arg, &result)
类型分发/可选 throw/构造返回 ttstr
destroy arg -> result
```

Android arm64 的 `0x5A4630..0x5A4648` landing 和 iOS armv7 的
`0x3E1EDE..0x3E1EF4` SjLj landing 同样按 `arg -> result` 清理；Android armv7、
iOS arm64 无本地 landing。现源码已把声明顺序改成 `result` 后 `arg`，从而恢复正常
路径及支持展开目标上的精确 AddRef/Release 顺序。

`onStyleChanged` 正常路径的局部顺序为 `dictionary accessor -> faceName -> vDict`，
所以回调后依次析构 `vDict -> faceName -> accessor`。Android arm64 landing 会根据
构造阶段清理这三个对象；Android armv7 与 iOS arm64 没有本地 landing。iOS armv7
的 SjLj 表会为 face/属性构造失败清理已构造对象，但直接 `onFontChange` 虚调用所用的
call-site 6 映射到 `__cxa_begin_catch; std::terminate()`，不是普通 cleanup；holder
析构的 call-site 7 也走 terminate。这一处尤其说明四文件不能被单个平台的“更安全”
异常路径替代。

## `appendChar`、over 判定与 kinsoku

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `onGetTextWidth` | `0x5A464C` | `0x4E3BC0` | `0x1003FAB98` | `0x3E1FA4` |
| `isOver` | 内联 | `0x4E4854` | `0x1003FCA44` | `0x3E3B0C` |
| `kinsoku` | `0x5A4E5C` | `0x4E41B4` | `0x1003FC0C8` | `0x3E32D0` |
| 源码级 `Line::clear` | `0x5A2248` 完整保留 | 内联 + deque clear `0x4E229C` | 内联 + deque clear `0x1003F8ACC` | 内联 + deque clear `0x3E00A2` |

`appendChar` 四者先向 UTF-16 accum vector 追加字符。`charBufCountdown-1 >= 0` 时回写
倒计数并立即成功；否则 accum 长度必须恰为 1，长度不是 1 直接失败。成功成字时会
为度量参数和 CharItem.text 分别构造两个 ttstr，调用
`onGetTextWidth(text,fontScale*curFontSize)`，再快照当前 face/font/color/效果字段。

四个栈帧还共同约束了 `CharItem` 的构造边界：第二个单字符 `ttstr` 直接落在蓝图的
`text` 字段，没有临时对象的 AddRef/Release；随后只把 `graph` 写成 0，并默认构造
`ruby` vector。`cw/size` 与当前样式字段由 `appendChar` 紧接着赋值，`x/y/renderPos`
则在这时仍未初始化，直到 `kinsoku` 的共同落字尾部才写入。共享源码因此使用接收
`tjs_char` 的轻量构造器，只初始化 `text/graph/ruby`，而不以成员内零值抹平这段
分阶段生命周期。蓝图在落字前不会读取三个未初始化坐标/时间字段。

横排且 curRubyText 非空时，再度量 ruby，向 CharItem.ruby 扩大一个默认构造元素并
就地写 `{text,x,y,span}`，随后消费并清空 curRubyText、更新 ruby bbox。最后清空
accum vector 的逻辑长度并把栈上 CharItem 交给 `kinsoku`；返回途中析构其 ruby vector
和两个局部 ttstr。四文件的 AddRef/Release 与异常清理边对应这条生命周期。

这里第一个单字符度量 `ttstr` 是具名函数局部，不是仅活到 `onGetTextWidth` 调用结束的
实参临时。四份正常尾声都在 `kinsoku` 返回后依次销毁 CharItem.ruby、释放
CharItem.text，最后才释放度量字符串；源码保留 `measureText` 跨越蓝图构造、ruby 处理和
`kinsoku`。这与 `renderImpl` 的 `evalDollarTag` 返回值恰好相反，后者必须在赋值
full-expression 后立即析构。

`curRubyText` 的消费、`clear` 和 `finishLine` 成功尾声都直接执行 ttstr 的
Release-and-null 语义，四目标没有 `releaseCurRubyText` 成员边界。共享源码现直接调用
`ttstr::Clear()`；`renderText` 在 `clear` 中也使用同一核心 API，避免为一次 Release/null
制造默认临时或插件私有薄包装。

`isOver` 在 Android armv7 和两份 iOS 中是独立函数，Android arm64 内联；共同条件为：

```text
horizontal: renderSizeW > 0 && renderSizeW <= penX + char.cw && !ignoreOverX
vertical:   renderSizeH > 0 && renderSizeH <= penY + char.size && !ignoreOverY
```

因此“刚好等于边界”也算 over。当前本地原先把判断内联在 `kinsoku`，行为相同但源码
结构没有反映 3/4 目标的函数边界；现已恢复独立 `isOver`，Android arm64 可由优化器
自然内联。

`kinsoku` 的共同阶段为：未 over 直接落字；over 时创建临时 deque，按 word-break、
following/leading 集和 `kinsokuUsed/kinsokuMax` 从 pending 尾部回退字符；需要时先
`finishLine`，再递归把临时 deque 从前到后重新落到新行。临时 deque 在最终落当前字
之前销毁。落字写 x/y/renderPos，更新 delay 最大值，深拷贝 CharItem 到 pending deque，
更新空格 run、renderCount/renderPos 和横/竖 pen。四者的分支条件、`<=` over 边界、
回退次序和失败传播与本地代码一致。

最终落字、word-break 状态更新、renderPos 计算和横/竖 pen 推进在四个目标里都是
`kinsoku` 的共同尾部，未保留 `placeChar/updateWordBreakState/advanceLineVertical`
任一额外函数边界；这与 3/4 目标明确保留的 `isOver` 边界不同。共享源码现已把这三层
人为抽取并回 `kinsoku`，同时保持 over 路径的临时 deque 在进入共同尾部前析构这一
可观察顺序。

这个临时对象在 Android/libstdc++ 上默认构造就分配 map 和一个空 node；在两份
iOS/libc++ 上只是全零 lazy 控制块，第一次 `push_front` 才分配。显式控制流的三类出口
都先调用 deque dtor：`finishLine`/递归失败后再返回 false、正常重排完成后再落当前字、
following 的直落分支也先析构再落字。四个 kinsoku 函数本身却都没有包围该作用域的
本地 EH landing；所以若 construction 之后的 helper 以 C++ 异常越过 kinsoku，指令流
不会经过这次正常 deque dtor。iOS armv7 的下层 push/grow helper 虽有自己的 SjLj
回滚，也不会替代外层 kinsoku 对已存在临时 deque 的析构。

## `finishLine` 收束

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|
| `0x5A3898` | `0x4E32CC` | `0x1003F9F30` | `0x3E133C` |

四文件共同逻辑是：竖排只进入末尾清理；横排先取 pending 内最大字号并与
curLineSize 取 max。若 `renderSizeH > 0 && renderSizeH < penY+lineHeight`，置
renderOver；未忽略 over-Y 时立刻清 pending 并失败。注意这里是严格 `<`，与
`isOver` 的 `<=` 不同。

随后按 curAlign 计算平移：`1 -> width-penX`、`0 -> (width-penX)/2`、其它 -> 0。
非空行还用 U+3000 的回调宽度（返回 0 时退回 curFontSize）计算向 renderText 前置的
缩进字符数。每个 CharItem 的 x 加对齐量、y 加 lineHeight，并按原次序把 text 追加到
renderText；同时更新行 bbox/lineBottom。整行深拷贝进 lineList 后清 pending，追加 LF，
penX 恢复 lineStartX，penY 增加 lineSpacing。共同尾声总会清 kinsokuUsed、释放
curRubyText 并把 accum vector 逻辑长度归零；这里的“共同尾声”只指成功横排路径与
竖排路径。over-Y 且未忽略时会先 clear pending 后直接返回 false，不执行成功尾声。

四个栈帧都只有一个非平凡短临时：为宽度回调构造的 U+3000 `ttstr` 实参。正常路径
在 `onGetTextWidth` 返回后立即 Release，再进入缩进循环；四个 finishLine 都没有本地
EH landing/SjLj cleanup。后续状态也按指令顺序增量提交而无外层事务回滚：缩进和每个
字符文本逐次追加到 renderText，字符 x/y 在相应文本追加前已改写；Line vector push
失败时 pending 的坐标和已经追加的文本仍保留。Line push 成功后先 clear pending，
再追加 LF；若 LF 追加抛出，lineList 已提交且 pending 已清，但 pen 尚未复位。共享
源码的普通 RAII/语句顺序自然表达正常路径，不能凭“更安全”直觉添加回滚。

## 收口状态与继续审计规则

`render` 一级分派和直属扫描/eval helper 已完成 fresh 四文件核对；`finishLine`、
`appendChar`、kinsoku/word-break 和 CharItem/RubyItem 基本生命周期已完成 fresh 核对；
`lineList` 扩容搬移/回滚、faceHash rehash/clear/无-erase、RubyItem/accum vector 和
CharItem deque map/node、charList 与 keyWaitList 的增长/异常边界已完成 fresh 四文件
核对；容器增长路径目前已收口。`onEval/onGetTextWidth/onStyleChanged/evalDollarTag`
的临时 variant/dictionary 正常与异常清理链也已完成 fresh 四文件核对。全局
24 个 hint、NCB registrar 的静态链表插入、Regist/Unregist 流程、无静态析构登记和
跨装卸 hint 持续性也已收口。`renderImpl` 的 17 个短 `ttstr`、函数级 tag accumulator、
两个扫描 vector 及 raw Process 的局部作用域/异常边也已收口；四个字典 setter 的
holder、唯一工作 variant 和短字符串临时亦已完成 fresh 四文件核对。
`finishLine`/kinsoku 的局部对象作用域、显式析构点与无外层 EH 回滚边已经收口；
`done` 的忽略返回、部分 flatten、KeyWait-before-sort 和 libstdc++/libc++ 排序边界也已
补齐，连同 deque map split-buffer、Line/Ruby/KeyWait/CharPtr vector slow path 与 sort
子 helper 均已写回四份 IDB。共享 `TJS_atoi`、`TJSHexNum`、
`tTJSHashFunc<ttstr>::Make` 与插件私有 helper 的
边界已重新划分并落到源码/IDB。实现文件中的旧 `libkrkr2.so` 裸地址、`sub_*`、
`LABEL_*`、IDA `vNN` 局部名和单一 Android arm64 `+offset` 字段锚点已全部移出；三个
把偏移写进名字的未观察读取字段也改成 `_unusedOptionFlag_guess/
_unusedResetStateA_guess/_unusedResetStateB_guess`，既不凭单端布局暗示业务语义，也以
`_guess` 明示二进制没有原始字段名证据。逐平台地址、字段偏移和容器尺寸只
保留在本基线。本轮已继续逐句检查其余控制流；在不改变异常与部分提交语义的约束下，
目前没有发现还需要恢复的可信插件私有 C++ 边界或运行语义。
普通 method 的八种 invoker、property 的八种 invoker、factory constructor invoker 与
raw render 的错误优先级也已完成核对；`ncbRegistClass::Method/Property/RawCallback`
的 17 种注册 wrapper 也已按类型恢复，Android arm64 的内联差异已单列。随后继续下钻
又补齐了先前匿名的 raw-method `FuncCall`、factory 的 `Create/ctor/FuncCall`、78 个
独立 method `Create/ctor` 与 property `Create`、144 个 factory/method/property
deleting-dtor/GetFlags、iOS 的两个 factory 完整析构 thunk，以及共享
`ncbNativeClassMethodBase::iMethod` facade、`RegisterNCM` 和 dispatch 引用计数链。排除
这些新确认入口后，核心范围剩余匿名函数已
逐个 fresh 分类：iOS arm64 均为 DWARF landing pad、临时 allocation/split-buffer
清理或 terminate thunk；Android/iOS armv7 剩余项是有大量插件外引用的通用
dispatch/variant helper、无引用 cleanup thunk 或空 allocator ABI artifact；Android
arm64 剩余项同样是引擎通用 PropGet/PropSet/holder helper 与空 variadic allocator
artifact。目前没有再发现未命名的 textrender 私有业务 helper。未来若出现新候选函数，
仍必须先补齐四文件地址映射、共同伪代码、差异与本地逐行对照，之后才能修改运行语义。
