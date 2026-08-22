# MotionPlayer `SeparateLayerAdaptor` 对象生命周期与 NCB 工厂四参考复原

日期：2026-08-13

> **后续更正（2026-08-17 / V185）**：本文关于 adaptor 成员布局、resolver 参数和
> `Layer(owner, targetLayer)` 的结论继续成立；第 9–10 节曾保留的 normal-builder 入口
> scratch owner/parent 已被 fresh 四参考证据进一步推翻。group composed Layer 确实仍需要
> owner/parent 两个 Variant，但它们只在每个 `composedLayer == Void` 门内通过
> `Window.mainWindow` expression 与 exact-hint `primaryLayer` 读取按需物化，不是入口级 raw
> scratch。见
> `motionplayer_build_render_commands_primary_layer_on_demand_hint_lifecycle_four_binary_2026-08-17.md`。

## 1. 本轮结论

四份当前参考二进制共同证明，`SeparateLayerAdaptor` 的源级成员声明顺序是：

```cpp
tTJSVariant owner;
tTJSVariant targetLayer;
tTJSVariant privateTarget;
std::map<tjs_uint32, SeparateLayerPayload> activeLayers;
std::map<tjs_uint32, SeparateLayerPayload> retiredLayers;
tjs_int absolute;
tjs_int assignSequence;
```

其中构造器只显式把 `absolute` 写成零；`assignSequence` 没有构造器初始化。它在三个会
进入 resolver 的顶层入口——`assign`、normal builder、accurate renderer——交换 map 后
各自归零。因此，本地字段声明上的 `_assignSequence = 0` 虽然通常不改变已知正常路径，
却抹掉了 native 的构造边界行为，必须去掉。

构造器并不宽容非对象 target。它复制传入 Variant，然后按对象转换规则取得 dispatch，
再以该对象同时作为调用对象和 `objthis` 读取 `window`。转换失败会沿 TJS Variant 的原生
失败路径退出；本地原先“类型不对就把 owner 留成 Void”的分支过于友好。

resolver 创建新 Layer 时，也不使用 resolver 调用者的 `objthis` 或 Player 临时查到的
`Window.mainWindow`。四端都把 adaptor 自身保存的两个 Variant 的地址直接作为两个
构造实参：

```cpp
Layer(owner, targetLayer)
```

并且 CreateNew 的接收者是 `TVPGetScriptDispatch()` 返回的全局脚本 dispatch，成员名为
`Layer`；不是先取 Layer class dispatch 再对 class object 调用匿名 CreateNew。本地旧
`objthis` 数据流和 class-dispatch helper 均与四参考不符。

析构器的显式函数体只有 `clear()`。之后编译器按逆声明序自动析构 retired map、active
map、private target、target Layer、owner。NCB instance adaptor 在“保存了 native 指针且
sticky 标志为 false”时调用该析构器并 `operator delete`；随后总会清空指针和 sticky
标志。它不是 GC 借用对象，也没有另外一条隐式 owner 回收链。

最后，四个 NCB registrar 都只注册以下五项，且顺序相同：Factory、`absolute`、
`targetLayer`、`clear`、`assign`。本地额外添加的 hidden
`layerTreeOwnerInterface` callback 不存在于任一当前参考的 `SeparateLayerAdaptor`
member registrar，必须移除。

本文中的绝对地址只作为分析坐标。编译源码不嵌入这些地址，也不继续沿用旧
`libkrkr2.so` 的单二进制注释。

## 2. 数据库和函数映射

本轮证据来自四份 recovery IDB，对应：

- `Kirikiroid2_1.3.9_Android_arm64-v8a`；
- `Kirikiroid2_1.3.9_Android_armabi-v7a`；
- `Kirikiroid2_1.3.9_iOS_arm64`；
- `Kirikiroid2_1.3.9_iOS_armv7`。

使用原生 `mcp__idalib__*` fresh decompile、disasm 和 xref 交叉验证。函数映射如下：

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| SLA constructor | `0x6C3DB4` | `0x58DBDC` | `0x1001298C4` | `0x128890` |
| NCB factory / create shell | `0x6EBFA4` | `0x5AA258` | `0x10013D48C` | `0x13DE80` |
| allocate-and-construct helper | `0x6EC0BC` | inline | inline | `0x13DFC8` |
| SLA destructor | `0x6CD398` | `0x593E98` | `0x10012A644` | `0x1291F8` |
| NCB instance-adaptor cleanup | `0x6FC5B0` | `0x5B73F0` | `0x10014EC90` | `0x150B40` |
| NCB member registrar | `0x6A9378` | `0x57C5A8` | `0x100103080` | `0x1004A6` |
| resolver | `0x6C3F28` | `0x58DCD4` | `0x100117E88` | `0x115B34` |
| standalone Layer-create helper | inline | `0x57AC1C` | `0x1001008A8` | `0xFDA14` |

Android arm64 把 Layer-create 逻辑内联进 resolver；另外三端保留了可辨认的共享 helper。
这是内联决策差异，不是源语义差异。Android arm64 与 iOS armv7 还把分配和构造拆成
小 helper；Android armv7 与 iOS arm64 则把相同步骤内联进 factory shell。

2026-08-15 的跨调用者 xref 复核进一步修正了这里的作用域描述：三份 standalone helper
并非 SLA 私有 helper，而是 SourceCache ctor/load、command-builder composed Layer、full-payload
resolver、payload-free ordinal resolver 和 Player workspace primary/work 共七个调用点共享的
source-level factory；Android arm64 在对应调用者内联。完整七调用点与异常矩阵见
`motionplayer_shared_layer_factory_exception_lifecycle_four_binary_2026-08-15.md`。

同一复核还恢复了第二条 payload-free resolver：`0x6C90C4` / `0x591DEC` /
`0x10011C628` / `0x11AE24`。它只搬移 Layer Variant，写 absolute/hitThreshold 且不递增
sequence；accurate renderer 在先调用 builder 的 full-payload resolver 后，还会直接调用这条
overload。所以下表“三位调用者”仍准确描述 full-payload resolver，但不能再解释成 SLA 只有
一条内部 resolver。

resolver 在四端都恰好有三位正式调用者：

| 调用者 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| assign | `0x6A993C` | `0x57C8A6` | `0x100103540` | `0x100930` |
| normal builder | `0x6C270C` | `0x58CABE` | `0x100116B30` | `0x114666` |
| accurate renderer | `0x6C7490` | `0x590912` | `0x10011AD70` | `0x119284` |

该 xref 集合也说明 resolver 不是公开 NCB 成员；它是插件 C++ 内部 helper。

## 3. 对象布局：同一声明，不同 STL ABI

### 3.1 四端完整布局

| 成员 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `owner` | `+0`, 20 B | `+0`, 12 B | `+0`, 20 B | `+0`, 12 B |
| `targetLayer` | `+20`, 20 B | `+12`, 12 B | `+20`, 20 B | `+12`, 12 B |
| `privateTarget` | `+40`, 20 B | `+24`, 12 B | `+40`, 20 B | `+24`, 12 B |
| alignment gap | `+60..63` | none | `+60..63` | none |
| `activeLayers` | `+64`, 48 B | `+36`, 24 B | `+64`, 24 B | `+36`, 12 B |
| `retiredLayers` | `+112`, 48 B | `+60`, 24 B | `+88`, 24 B | `+48`, 12 B |
| `absolute` | `+160`, 4 B | `+84`, 4 B | `+112`, 4 B | `+60`, 4 B |
| `assignSequence` | `+164`, 4 B | `+88`, 4 B | `+116`, 4 B | `+64`, 4 B |
| 分配大小 | `0xA8` / 168 B | `0x5C` / 92 B | `0x78` / 120 B | `0x44` / 68 B |

Android 参考使用 libstdc++ 的 `std::map` 对象布局；iOS 参考使用 libc++。因此同为
64 位时 Android map 是 48 字节、iOS map 是 24 字节；同为 32 位时分别是 24 与
12 字节。这些尺寸差异完整解释了四个对象大小，不需要假设 Android 或 iOS 独有字段。

### 3.2 构造器实际写入

四端构造器都按下列顺序执行：

```cpp
SeparateLayerAdaptor::SeparateLayerAdaptor(tTJSVariant target) {
    tTJSVariant targetCopy(target);
    iTJSDispatch2 *targetObject = targetCopy.AsObjectNoAddRef();
    targetObject->PropGet(0, TJS_W("window"), nullptr,
                          &owner, targetObject);
    targetLayer = target;
    privateTarget = tTJSVariant();
    activeLayers = {};
    retiredLayers = {};
    absolute = 0;
    // assignSequence 没有构造器写入
}
```

这里的伪代码表达数据流，不表示成员先默认构造、再逐项赋值的实际优化形态。反编译中
可见编译器把 Variant 复制、对象转换和成员构造穿插安排；但成员完成构造的顺序仍遵守
声明顺序。

四端 constructor disasm 都有 `absolute` 对应偏移的显式零写入，同时在整个构造函数
范围内都没有 `assignSequence` 偏移写入：

- Android arm64：写 `+160`，不写 `+164`；
- Android armv7：写 `+84`，不写 `+88`；
- iOS arm64：写 `+112`，不写 `+116`；
- iOS armv7：写 `+60`，不写 `+64`。

这不是“反编译器漏显示了连续 64 位清零”：对应原始 store 宽度只覆盖
`absolute` 的四字节槽，后一个四字节仍保持分配器交付时的内容。

### 3.3 所有合法 resolver 入口负责重置 sequence

三个顶层入口的 reset 位置如下：

| 入口 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| assign | `0x6A98B0` / `0x6A9928` | `0x57C83A` / `0x57C898` | `0x1001034C0` / `0x10010352C` | `0x1008BE` |
| normal builder | `0x6C22EC` | `0x58C800` | `0x100116820` | `0x11419C` |
| accurate renderer | `0x6C7238` | `0x590550` | `0x10011AAF4` | `0x118EE8` |

部分 assign 地址有两处，是 Android 两端和 iOS arm64 控制流合并后的两个等价入口块；
不是一帧重置两次。各指令都写同一后成员槽。resolver 随后读取
`absolute + assignSequence`，写 Layer 的 `absolute`，递增 sequence，再写
`hitThreshold = 256`。

因此源级边界是“未初始化的私有计数器，只允许从会先 reset 的内部入口使用”，而不是
“对象一创建就可安全直接调用 resolver”。本地公开的 `_guess` helper 仍应只由这些已知
内部路径调用。

## 4. target 到 owner 的严格构造数据流

四个 constructor 都先复制 target Variant，再做对象转换，随后执行：

```cpp
targetObject->PropGet(
    0,
    TJS_W("window"),
    memberHint,
    &owner,
    targetObject);
```

共同边界行为：

1. 零参数 factory 产生 Void target，然后仍进入相同 constructor；Variant-to-object
   转换决定最终异常/错误边界，factory 不预先替换一个安全对象。
2. 非对象参数没有 `Type() != tvtObject` 的早退分支。
3. 对象 Variant 的 `Object` 指针是 PropGet 的 receiver，也是 `objthis`。
4. PropGet 返回值没有被转成“失败则 Void”的兼容结果；异常和错误按 TJS 调用链传播。
5. 读取结果直接成为第一个 owning Variant 成员；target 自身成为第二个成员。

本地旧 helper 在 1、2、4 三点上都过于宽容，改变了脚本用 Void、整数、字符串或无效
对象构造 SLA 时的可观察边界。

## 5. Layer 创建参数和脚本 dispatch 所有权

### 5.1 两个参数是成员 Variant 本身

四端 resolver 的新建分支都等价于：

```cpp
tTJSVariant *args[2] = { &owner, &targetLayer };
global->CreateNew(0, TJS_W("Layer"), &layerHint,
                  &created, 2, args, global);
```

逐端证据：

- Android arm64 resolver 内联构造 `args[0] = this + 0`、
  `args[1] = this + 20`；
- Android armv7 resolver 以 `this`、`this + 12` 调用 `0x57AC1C`，helper 原样写入
  两个参数槽；
- iOS arm64 resolver 以 `this`、`this + 20` 调用 `0x1001008A8`；
- iOS armv7 resolver 以 `this`、`this + 12` 调用 `0xFDA14`。

这两个地址分别就是前两项 Variant 成员，而不是临时构造出来的 dispatch Variant。
参数指针在同步 CreateNew 调用期间借用成员存储，不发生先验复制。

### 5.2 接收者是 global，不是 Layer class object

三个独立 helper 与 Android arm64 内联体都先取得全局脚本 dispatch，再在 global 上以
成员名 `Layer` 调用 CreateNew，最后 Release global。创建出的 raw dispatch 被包装成
`tTJSVariant(created, created)`，随后 Release raw `created`，由 Variant 保留最终强引用。

四端都没有本地旧实现中的这些步骤：

- 不读取 `Window.mainWindow` 或 `primaryLayer`；
- 不把 resolver 调用者 `objthis` 包成 owner 参数；
- 不先查询 `Layer` class dispatch；
- 不对 class object 做 `CreateNew(0, nullptr, ...)`；
- 不把 CreateNew 失败转换成空 Variant 后继续安全返回。

因此 resolver 的源级签名无需 `objthis` 参数。normal builder 和 accurate renderer
原先传入的 scratch owner / SLA script object 是本地移植产生的额外数据流。

## 6. NCB factory 与 native 实例绑定

四端 factory 的共同参数策略：

```cpp
tTJSVariant target;
if(numparams >= 1)
    target = *param[0];
// 多余参数忽略

auto *native = new SeparateLayerAdaptor(target);
auto *adaptor = objthis->NativeInstanceSupport(
    TJS_NIS_GETINSTANCE, classId, nullptr);
if(!adaptor) {
    native->~SeparateLayerAdaptor();
    operator delete(native);
    return TJS_E_INVALIDOBJECT;
}
adaptor->SetNativeInstance(native);
return TJS_S_OK;
```

这里把不同 ncbind 模板层次折成共同语义。具体二进制可能在 factory shell 外层创建 NCB
class instance adaptor，再把它传进 allocator helper；但四端最终都遵守相同的：

- `numparams < 1` 时构造一个 Void Variant；
- 有参数时只复制第一个 Variant；
- 多余参数不报错；
- native 对象只在成功绑定 adaptor 后转移所有权；
- 绑定失败走完整析构加 `operator delete`，返回 `TJS_E_INVALIDOBJECT`；
- Android arm64 的 class-instance 创建空指针路径还抛出/报告
  `NativeClassInstance creation faild.`；其他端模板包装层表达等价失败。

本地 factory 中 `param[0]` 和 `result` 的额外空指针防护不是 native 脚本调用边界；ncbind
调用契约保证这些槽在相应计数/返回路径有效。为了复原源结构，factory 应直接解引用首参
和输出槽。

## 7. 析构顺序与 NCB adaptor 所有权

四端 SLA destructor 的共同源结构是：

```cpp
SeparateLayerAdaptor::~SeparateLayerAdaptor() {
    clear();
}
// 随后隐式：
// retiredLayers.~map();
// activeLayers.~map();
// privateTarget.~tTJSVariant();
// targetLayer.~tTJSVariant();
// owner.~tTJSVariant();
```

`clear()` 的语义已在前一份双-map 分析中恢复：先 Invalidate private target，再对 active
map 的每项复制完整 payload、通过副本 Invalidate Layer，最后销毁 active tree。

析构体本身不显式清 retired map，也不显式 Clear 三个 Variant。retired map 的隐式
destructor 只释放 tree 节点持有的 Variant，不额外对 Layer 调用 Invalidate；这正是
`clear()` 与 map destructor 的不同边界。当前本地 ordered-map wrapper 的 destructor
使用 `clear(false)`，可表达这一隐式 map 回收行为。

四端 NCB adaptor cleanup 都等价于：

```cpp
if(instance && !sticky) {
    instance->~SeparateLayerAdaptor();
    operator delete(instance);
}
instance = nullptr;
sticky = false;
```

所以 native SLA 的通常生命周期由 script NCB instance adaptor 唯一拥有；`sticky`
只抑制这次 delete，不改变清空 adaptor 指针/标志的尾部状态。factory 绑定失败时对象尚未
交付 adaptor，因而由 factory 自己采用相同“析构再 delete”路径回滚。

## 8. Registrar 的精确成员集

四个 member registrar 的调用序列都只有：

1. `Factory(&SeparateLayerAdaptor::factory)`；
2. read/write property `absolute`；
3. read/write property `targetLayer`；
4. method `clear`；
5. raw/native method `assign`。

四端在 `assign` descriptor 注册完成后直接进入 registrar epilogue。没有第六个 descriptor，
也没有 `layerTreeOwnerInterface` 字符串/hidden flags 传给该 registrar。即使同名字符串在
别的 Layer/Window 逻辑中存在，也不能据此把它挂到 SLA class。

因此本地：

```cpp
RawCallback(TJS_W("layerTreeOwnerInterface"),
            &SeparateLayerAdaptor::getLayerTreeOwnerInterfaceCompat,
            0, TJS_HIDDENMEMBER);
```

是早期兼容性猜测，不属于四份当前参考的源成员表；callback 实现也应随注册项一起删除。

## 9. 本地逐项对比与修改决策

| 本地当前实现 | 四参考共同事实 | 决策 |
|---|---|---|
| constructor owner helper 对非对象返回 Void | 直接 Variant-to-object，再读 `window` | 移除 type/null 宽容分支 |
| `_assignSequence = 0` 字段初始化 | constructor 不写该槽，三类入口负责 reset | 去掉 in-class initializer |
| resolver 接收 caller `objthis` | resolver 只读 SLA 保存的 owner/target | 从公开/internal/assign 调用链删除该参数 |
| `Layer(objthis, targetLayer)` | `Layer(owner, targetLayer)`，参数为成员 Variant 地址 | 改为两个保存成员 |
| 先获取 Layer class dispatch | 在 global dispatch 上命名 CreateNew `Layer` | 改为 `TVPGetScriptDispatch()` 路径 |
| CreateNew 失败安全返回空 | native 随后直接包装/Release created | 不引入本地成功检查分支 |
| destructor 显式清 map 和三个 Variants | 显式体只有 `clear()`，其余为隐式成员析构 | 删除手工尾部清理 |
| hidden `layerTreeOwnerInterface` member | 四个 registrar 均无此项 | 删除注册、声明和实现 |
| accurate helper 因 resolver 而要求 `slaObject` | resolver 不需要 script objthis | 从 accurate 内部 helper 删除该参数 |
| normal leaf 因 resolver 选择 scratch parent/owner | SLA resolver 只读 adaptor 保存成员；group 则在自己的 Void 门内按需求值 owner/primary Variant | leaf helper 删除两个参数；builder 入口 scratch 整体删除 |
| factory 检查 `param[0]`、`result` 是否为空 | ncbind 模板直接使用有效槽 | 直接首参复制和输出赋值 |

本文原轮次只切断了 scratch owner/parent 到 SLA leaf resolver 的错误边。后续 V185 已由
四端进一步证明 builder 入口的 scratch 可以且必须整体删除：group composed Layer 保留的是
两个 Variant 的语义实参，但仅在其 Void 门内重新执行 expression/accessor/GetValue 后产生，
不会复用函数入口的 raw dispatch。

## 10. 实现与验证结果

本轮源码已完成以下修改：

- constructor owner 提取改为严格 Variant-to-object，再读取 `window`；
- `_assignSequence` 去掉 in-class initializer；
- resolver 的公开/internal/assign 调用链全部删除 caller `objthis`；
- Layer 创建改为在 global dispatch 上命名 CreateNew，两个实参直接借用保存的
  `_owner`、`_targetLayer` Variant；
- destructor 显式体缩回单独 `clear()`，retired map 与三个 Variant 留给成员析构；
- NCB registrar 和类实现删除不存在的 hidden `layerTreeOwnerInterface` callback；
- accurate helper 不再要求 `slaObject`；normal leaf helper 不再接收 scratch owner/parent；
  后续 V185 又删除 normal builder 入口 scratch，composed-Layer 路径改为 Void 门内独立物化
  owner/primary Variant 后传给共享 factory；
- factory 恢复首参/结果槽的 ncbind 调用契约边界。

后续 2026-08-15 shared-factory 纵切又将三份 file-private factory 复制体收拢为唯一
`detail::createLayerVariant_guess`，让 composed Layer 也走同一 native helper，并恢复
`resolveLayerOrdinal_guess`；accurate renderer 不再构造/复制该 overload 原生不存在的 payload。
2026-08-17 V185 再恢复 composed Layer factory 两个实参的精确来源与生命周期：它们来自
该 group 门内的 `Window.mainWindow` owner Variant 和 exact-hint `primaryLayer` result Variant。

验证结果：

1. `rg` 对 `getLayerTreeOwnerInterfaceCompat`、`clearNativeListsForDtor`、
   `createLayerNodeObject_guess` 和先前已删除的 `trackManagedTarget_guess` 均为零命中。
2. `cmake --build out/web/debug -j 8` 成功完成全部 C++ 编译、静态库链接和最终
   `index.html/index.wasm` 链接。第一次最终链接遇到一次瞬时
   `index.wasm: permission denied`；确认没有残留构建进程后原命令重试成功，故不是源码
   或链接符号错误。
3. 从 `out/web/debug/compile_commands.json` 读取实际
   `SeparateLayerAdaptor.cpp` Emscripten 编译命令，替换为
   `tests/unit-tests/plugins/motionplayer-dll.cpp -fsyntax-only` 并加入现有
   `out/syntax-check` test-config/Catch2 include；检查成功。唯一输出是仓库既有
   `_tss` literal-operator deprecated warning。
4. 全仓 `git diff --check` 返回成功；输出只有工作树 LF 将来可能转 CRLF 的提示，无
   whitespace error。
5. 四份 recovery IDB 已加入 ctor、dtor、factory、registrar、NCB cleanup 的语义名和
   生命周期注释；三份非内联实现还加入 Layer-create helper 语义名，Android arm64 在
   resolver 注释中标明同一内联数据流。四次 `idb_save` 均返回 `ok: true`，保存后再按
   地址 lookup，所有新函数名均可读回。

构建结果只验证移植代码内部一致性；四端 fresh decompile/disasm/xref 和本文件前九节才是
对象布局、未初始化边界、参数所有权与 registrar 集合的语义依据。
