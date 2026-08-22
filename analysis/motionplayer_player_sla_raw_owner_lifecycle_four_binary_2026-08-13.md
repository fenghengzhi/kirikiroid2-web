# MotionPlayer `Player` → `SeparateLayerAdaptor` raw-owner 生命周期（四参考）

日期：2026-08-13

> **后续更正（2026-08-17 / V185）**：本文的 raw-owner 身份、ctor-success 后发布与
> destructor 顺序继续成立；第 3 节旧伪代码把 target 来源抽象成了友好
> `resolveMainWindowPrimaryLayer()`，不再代表当前四参考结论。builder 实际只在 lazy SLA
> 门内重新求值 `Window.mainWindow`，以 strict accessor、flags 0、exact shared hint 和非空
> Variant result 读取 `primaryLayer`；而且 new-expression allocation 先于该属性读取。完整
> 证据和 group 的第二条独立按需链见
> `motionplayer_build_render_commands_primary_layer_on_demand_hint_lifecycle_four_binary_2026-08-17.md`。

## 1. 结论

`Player` 内部保存一个 persistent `SeparateLayerAdaptor *`。四份当前参考共同显示它是
手写单 raw owner，而不是默认销毁的 `std::unique_ptr`：

1. `Player` constructor 把 owner slot 写成 null；
2. normal render-command builder 只在槽为空时分配 adaptor；
3. `SeparateLayerAdaptor` constructor 完整成功后才把 raw pointer 发布到 `Player`；
4. 发布后立即对该对象执行当前 pass 的 map swap/reset；
5. `Player` destructor 的显式函数体先完成 ramp/parameter/node-tree teardown，再调用
   adaptor destructor、`operator delete`，最后才清 owner slot；
6. 随后才进入其余成员的自动逆序析构。

iOS arm64 和 iOS armv7 的 native 顺序尤其有区分力：两端都是
`pointee destructor -> operator delete -> slot = null`。同一批 iOS 二进制中已经闭合的
libc++ `unique_ptr` owner 使用 exchange-first/reset 形态，即先把 owner slot 换成 null，
再销毁旧对象。这里没有那种形态，因此把该字段美化为 `unique_ptr` 会改变可观察的槽位
时序，也会掩盖它位于 `Player` 显式 destructor body 的事实。

本文只在分析记录中保留绝对地址。编译源码使用语义注释，不再沿用旧
`libkrkr2.so` 的单目标偏移。

## 2. 四端映射与 ABI 差异

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `Player` constructor | `0x6CC110` | `0x5935C4` | `0x10011EC04` | `0x11D488` |
| owner slot | `+760` | `+500` | `+648` | `+436` |
| constructor 零槽 store | `0x6CC5DC` | `0x5938DA` | `0x10011EF44` | `0x11D9CA` |
| render-command builder | `0x6C2208` | `0x58C7C4` | `0x1001167BC` | `0x114118` |
| adaptor allocation | `0x6C2400` | `0x58C92E` | `0x100116918` | `0x114438` |
| 分配大小 | `0xA8` / 168 B | `0x5C` / 92 B | `0x78` / 120 B | `0x44` / 68 B |
| adaptor constructor | `0x6C3DB4` | `0x58DBDC` | `0x1001298C4` | `0x128890` |
| builder 内 ctor call | `0x6C2458` | `0x58C95C` | `0x100116950` | `0x114480` |
| builder 内 slot publish | `0x6C2460` | `0x58C964` | `0x100116954` | `0x11448A` |
| `Player` destructor | `0x6CCEBC` | `0x593C24` | `0x10011F2A0` | `0x11DCC4` |
| adaptor destructor | `0x6CD398` | `0x593E98` | `0x10012A644` | `0x1291F8` |
| owner destroy/delete/clear block | `0x6CCF28` | `0x593C64` | `0x10011F2F0` | `0x11DD54` |

Android 参考使用 libstdc++ map 布局，iOS 参考使用 libc++ map 布局；因此 adaptor 的
分配大小并不只由指针宽度决定。同为 64 位时是 168 B 对 120 B，同为 32 位时是
92 B 对 68 B。对应的 `SeparateLayerAdaptor` 完整成员布局已经在
`motionplayer_separate_layer_adaptor_object_lifecycle_four_binary_2026-08-13.md` 闭合。

本轮也修正了旧注释对 `Player` field offset 的误用：只有 Android arm64 是 `+760`；
Android armv7、iOS arm64、iOS armv7 分别是 `+500`、`+648`、`+436`。跨 ABI 直接复用
单一偏移会把完全不同的成员误认成该 owner。

## 3. 构造、发布与正常使用数据流

四端共同的源级数据流等价于：

```cpp
Player::Player(...) : renderSeparateLayerAdaptor(nullptr) {
    // 其余成员构造
}

if(renderSeparateLayerAdaptor == nullptr) {
    tTJSVariant owner;
    TVPExecuteExpression(TJS_W("Window.mainWindow"), &owner);
    ncbPropAccessor ownerAccessor{tTJSVariant(owner)};
    renderSeparateLayerAdaptor =
        new SeparateLayerAdaptor(
            ownerAccessor.GetValue(
                TJS_W("primaryLayer"),
                ncbTypedefs::Tag<tTJSVariant>(), 0,
                &primaryLayerMemberHint_guess));
    // 销毁 primaryLayer result 临时量
    renderSeparateLayerAdaptor->beginLayerPass_guess();
    // ownerAccessor Release；owner Variant 析构
}
```

这段伪代码强调以下边界：

- `Window.mainWindow` expression 与 strict accessor 建立在 allocation 之前；
- new-expression 的 allocation 发生在 `primaryLayer` 参数 `GetValue` 之前；
- allocation、属性读取或 adaptor constructor 失败时 member 都仍为 null；new-expression 对
  已取得但尚未发布的 storage 保持相应释放边界；
- member store 发生在 constructor 返回之后，primary result 临时量清理和 begin-pass 之前；
- begin-pass 后才释放 accessor 并析构 mainWindow owner Variant。

因此对象不存在“发布了半构造 adaptor”的状态。发布后的对象是跨帧 persistent；正常
builder 入口如果看到已有对象，只执行 pass 入口交换/reset，不做 replacement。当前四端
没有发现除 `Player` destructor 之外的独立 reset/delete API。

普通 `primaryLayer` PropGet HRESULT 被忽略；若失败且不写结果，Void 会进入严格 adaptor
constructor，而不是被友好 resolver 转换为 null raw dispatch。发布后临时 Variant 清理或
begin-pass 若异常，owner 已经保存在 `Player`，不会由 builder landing pad 回滚。这一点与
persistent cache 的职责一致；它不是 pending local owner。

## 4. 析构阶段和 raw-owner 判定

四端 `Player` destructor 都先执行：

```cpp
purgeParameterRampMap_guess();
parameterEntries.clear();
variableLabelScopes.clear();
resetAndReleaseOldNodeTree_guess();
```

随后立刻执行 owner teardown：

```cpp
if(renderSeparateLayerAdaptor != nullptr) {
    renderSeparateLayerAdaptor->~SeparateLayerAdaptor();
    operator delete(renderSeparateLayerAdaptor);
    renderSeparateLayerAdaptor = nullptr;
}
```

之后才出现 scratch-state cleanup、variable-label deque destructor、label map、tags/
meshline/outline Variant 和更早成员的自动析构。这一位置说明 owner teardown 属于显式
destructor body，而不是由字段声明顺序自然插入的 member destructor。

仅凭 `new`、字段 store 和正常 delete，raw pointer 与某些显式使用的 RAII wrapper 可能
生成相似代码；但四端的联合证据进一步收窄了来源：

- Android 与 iOS 全部保持 delete 后清槽；
- iOS 两端同版本 libc++ `unique_ptr::reset()` 在其他已闭合 owner 上先 exchange/清槽，
  再 delete 旧指针；
- 此 owner 的清槽位于手写 teardown 序列中，而非自动 member destruction phase；
- lazy builder 直接把构造完成的 raw pointer 发布到原槽，没有额外控制块或 deleter state。

最小且能保持全部顺序的源结构就是 `SeparateLayerAdaptor *` 加显式 `delete`/null。把它改成
`unique_ptr` 即使正常结果相同，也不能忠实表达 native 的 slot timing。

## 5. 本地修改与边界保留

本地原本已经使用 raw pointer 和手工 delete/null，因此没有改动运行语义。本轮做的是：

- 用四参考结果替换 `Player.h` 中旧 `libkrkr2.so player+760`/单地址注释；
- 在 destructor 处明确记录 destroy/free 后清槽的顺序；
- 在 lazy builder 处记录 ctor-success 后发布、发布后立即 begin-pass；
- 后续 V185 删除 builder 入口 raw owner/parent 预取，恢复 expression/accessor/exact-hint 的
  按需求值以及 allocation-before-PropGet 顺序；
- 保留 `delete` 后 `_renderSeparateLayerAdaptor = nullptr`，没有改用 RAII；
- 四端 IDB 为 ctor、builder、dtor 加入该 owner 协议注释并保存。

构建检查只能验证移植代码内部一致性；本文件前四节的 fresh 四端 disasm/decompile 才是
raw-owner 身份、字段偏移、对象大小和时序结论的依据。
