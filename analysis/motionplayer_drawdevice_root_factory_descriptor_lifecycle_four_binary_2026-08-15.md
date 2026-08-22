# motionplayer DrawDevice 根 factory descriptor 与创建生命周期（四参考二进制）

日期：2026-08-15

## 结论

`DrawDeviceD3D` 与 `D3D` 是两个公开表面相同、布局相同，但 concrete class ID、最终 vtable 和 NCBind adaptor 各自独立的根类。两者都用 NCBind 的 raw native-class factory，而不是 typed constructor。

一次普通脚本构造分成两个职责不同的阶段：

```text
TJS native class 先创建脚本对象 + 该 concrete class 的空 adaptor
  -> 调用以类名注册的 raw factory descriptor
     -> 业务 factory 检查 argc >= 2
     -> 分配完整 C++ 根对象
     -> 依次把 arg0/arg1 转成 tjs_int，额外参数不读
     -> 以 objthis 作为借用的 ScriptOwner 构造根对象
        -> 构造中另行注册 sticky D3DLayerBase adaptor
     -> raw descriptor 从 objthis 找 concrete class adaptor
     -> 把根对象指针写入 adaptor；该 non-sticky adaptor成为唯一所有者
```

raw descriptor 还有一个专供 `CreateAdaptor` 使用的特殊路径：当且仅当 `argc == 1` 且 `arg0.Type() == tvtVoid` 时，它立即返回成功，不调用业务 factory、不验证 receiver、不清空 `result`。这样可先生成“只有空 concrete adaptor 的脚本壳”，稍后再填入一个已有 native 指针。

这与 typed method 的边界显著不同：raw factory 在所有路径上都不清空或写入脚本 `result`；普通路径也不在进入业务 factory 前验证 `objthis`。

## 两个 concrete 根类

| 属性 | `DrawDeviceD3D` | `D3D` |
|---|---|---|
| 公开 member 表 | 相同 33 member + factory | 相同 33 member + factory |
| C++ 主/次基类布局 | 相同 | 相同 |
| concrete class ID | 独立 | 独立 |
| concrete NCBind adaptor | 独立、non-sticky、拥有根对象 | 独立、non-sticky、拥有根对象 |
| `D3DLayerBase` adaptor | 另行注册、sticky、仅借用 | 另行注册、sticky、仅借用 |
| 最终 primary/secondary vtable | `DrawDeviceD3D` 集合 | `D3D` 集合 |

两类不是脚本别名。尤其 factory wrapper 在 `objthis` 上查询的是各自 class ID；一个只有 `DrawDeviceD3D` adaptor 的对象壳不能接收 `D3D` native 指针，反之亦然。

## 注册入口与业务 factory

### `DrawDeviceD3D`

| 目标 | member registrar | factory 注册 helper | business factory |
|---|---:|---:|---:|
| A64 | `0x52A618` | 内联于 registrar 起始段 | `0x52B654` |
| A32 | `0x492790` | `0x492BD4` | `0x492BFC` |
| I64 | `0x10023070C` | `0x100230C34` | `0x100230C88` |
| I32 | `0x22F622` | `0x22FAFE` | `0x22FB28` |

### `D3D`

| 目标 | member registrar | factory 注册 helper | business factory |
|---|---:|---:|---:|
| A64 | `0x52BC18` | 内联于 registrar 起始段 | `0x52CC54` |
| A32 | `0x492F10` | `0x493354` | `0x49337C` |
| I64 | `0x100230FF0` | `0x100231518` | `0x10023156C` |
| I32 | `0x22FDFA` | `0x2302D6` | `0x230300` |

A64 把 descriptor 分配和构造内联进两个大 registrar；另三份保留了 register → Create → descriptor ctor 的函数链。这是优化差异，不是源码结构差异。

## descriptor 对象结构与构造

### 保留 helper 的三份参考

| 类 | 阶段 | A32 | I64 | I32 |
|---|---|---:|---:|---:|
| `DrawDeviceD3D` | descriptor Create | `0x498252` | `0x100236B74` | `0x23591C` |
| `DrawDeviceD3D` | descriptor ctor | `0x49836C` | `0x100236D18` | `0x235B44` |
| `D3D` | descriptor Create | `0x49C0EE` | `0x10023B6D0` | `0x23B208` |
| `D3D` | descriptor ctor | `0x49C208` | `0x10023B874` | `0x23B430` |

Create helper 只在注册开关为真时分配 descriptor；32 位对象大小 `0x20`，64 位对象大小 `0x38`。它返回的不是对象首地址，而是对象内嵌的 `iMethod` facade：32 位偏移 `+0x14`，64 位偏移 `+0x20`。

descriptor ctor 依次建立：

```text
tTJSDispatch base
native-item kind = method
label = "Function"
embedded iMethod vtable
embedded iMethod -> outer descriptor back-pointer
class-specific raw-factory descriptor vtable
business factory function pointer
```

若传入空 business factory pointer，ctor 抛出 `"No factory pointer."`。两个根类的 descriptor vtable 不同，因为模板参数 `ClassT` 不同，最终查询的 native class ID 也不同。

### descriptor vtable 与 `FuncCall`

| 类 | 目标 | descriptor address point | `FuncCall` |
|---|---|---:|---:|
| `DrawDeviceD3D` | A64 | `0x19FB058` | `0x534F78` |
|  | A32 | `0x10AB248` | `0x4983CC` |
|  | I64 | `0x101AEED50` | `0x100236D98` |
|  | I32 | `0x18392E0` | `0x235C38` |
| `D3D` | A64 | `0x19FC9A0` | `0x539374` |
|  | A32 | `0x10ABEEC` | `0x49C268` |
|  | I64 | `0x101AF0668` | `0x10023B8F4` |
|  | I32 | `0x1839F6C` | `0x23B524` |

四平台、两 concrete class 共八个 `FuncCall` 实例的控制流完全一致，差异仅在指针宽度、class ID 全局量和异常展开格式。

## raw factory `FuncCall` 的精确顺序

`FuncCall(flag, membername, hint, result, argc, argv, objthis)` 的真实顺序是：

```text
1. membername != nullptr
      -> TJS_E_MEMBERNOTFOUND (-1001)
      -> result 保持原值

2. argc == 1 && argv[0].Type() == tvtVoid
      -> TJS_S_OK
      -> 不检查 objthis
      -> 不调用 business factory
      -> result 保持原值

3. native = nullptr
   error = businessFactory(&native, argc, argv, objthis)
      -> error != TJS_S_OK 时原样返回
      -> result 保持原值

4. 从 objthis 获取本 concrete class 的既有 adaptor
      -> objthis 非空、NativeInstanceSupport 成功且 adaptor 非空：
           adaptor->instance = native
           return TJS_S_OK

5. 第 4 步失败
      -> native 非空时调用其 deleting destructor
      -> return TJS_E_NATIVECLASSCRASH (-1008)
      -> result 保持原值
```

`flag`、`hint` 和 `result` 均不参与该流程。它没有 typed method 的 receiver precheck，也没有 invoke-command 造成的 `result.Clear()`。

### 特殊 one-Void 分支的优先级

优先级可观察为：

| 条件 | 返回 | 是否需要 receiver | 是否创建 native | result |
|---|---:|---:|---:|---|
| 非空 `membername`，即使参数为 one-Void | `-1001` | 否 | 否 | 保留 |
| 空 membername + exact one-Void | `0` | 否 | 否 | 保留 |
| 空 membername + zero args | business factory 的 `-1004` | 否 | 否 | 保留 |
| 空 membername + one non-Void | business factory 的 `-1004` | 否 | 否 | 保留 |
| 空 membername + two args + 正确空 adaptor shell | `0` | 是 | 是并安装 | 保留 |
| 空 membername + two args + 错误 concrete adaptor | `-1008` | 是 | 是，随后删除 | 保留 |

exact one-Void 不是“缺省 width=0,height=0”。它完全跳过业务 factory。两个 Void 参数则不匹配 sentinel，会走普通路径，并分别按整数 0 转换。

## business factory 的精确顺序

两个 concrete class、四平台共同的源码结构为：

```cpp
static tjs_error factory(Class **out, tjs_int argc,
                         tTJSVariant **argv, iTJSDispatch2 *objthis) {
    if(argc < 2)
        return TJS_E_BADPARAMCOUNT;
    *out = new Class(static_cast<tjs_int>(*argv[0]),
                     static_cast<tjs_int>(*argv[1]), objthis);
    return TJS_S_OK;
}
```

编译后的精确次序是：

1. `argc < 2` 时直接 `-1004`，不分配、不读 `argv`、不使用 `objthis`。
2. `argc >= 2` 时先 `operator new` 完整 concrete object。
3. 转换 `argv[0]` 为 `tjs_int`。
4. 转换 `argv[1]` 为 `tjs_int`。
5. 构造主基类、次基类和 concrete tail。
6. 安装该 concrete class 的最终两张 vtable。
7. 将 native 指针写入 `*out`，返回 0。

额外参数完全不读取。A32/I64/I32 明确调用共享 `tTJSVariant::AsInteger`；A64 把相同的 variant type switch 内联两次。A64 的 switch 也确认 Void/未匹配类型落到整数 0，Real 使用向零截断的浮点转整数，String/Octet 等遵循共享 Variant 转换路径。

### 对象大小与最终 vtable 差异

| 目标 | object size | `DrawDeviceD3D` primary / secondary | `D3D` primary / secondary |
|---|---:|---:|---:|
| A64 | `0x200` | `0x19FA908` / `0x19FA978` | `0x19FACB8` / `0x19FAD28` |
| A32 | `0x13C` | `0x10AAEA0` / `0x10AAED8` | `0x10AB078` / `0x10AB0B0` |
| I64 | `0x1A0` | `0x101AEE568` / `0x101AEE5D8` | `0x101AEE9A8` / `0x101AEEA18` |
| I32 | `0x10C` | `0x1838EF4` / `0x1838F2C` | `0x1839110` / `0x1839148` |

A32/I64/I32 的 `D3D` factory 复用已恢复的共享基类 ctor 后显式覆盖两张最终 vtable；A64 优化器把完整派生构造尾部内联。两种形态仍证明它们是两个 concrete 类型。

## receiver、`D3DLayerBase` 与失败清理

raw descriptor 没有普通 receiver precheck，`objthis` 会先传进业务 factory。根对象主基类构造期间又执行：

```text
SetAdaptorWithNativeInstance(objthis, root, non-sticky initially)
GetAdaptor(objthis)->setSticky()
```

对应主基类 ctor：A64 `0x531274`、A32 `0x495618`、I64 `0x100233C88`、I32 `0x23295C`。四者在注册后都再次查询 `D3DLayerBase` adaptor，并无条件写 sticky byte。查询失败时反编译都形成对零基址偏移字段的写入。因此：

V208 补足了“注册失败”的精确限定：helper 的 false 被忽略；只有 owner 在 REGISTER 处为空，
或随后 GET 失败/返回 null，才形成确定崩溃。若 existing attachment 在重复 REGISTER 失败后仍
可 GET，sticky 晋升仍会完成。helper 对 native-null existing adaptor 还会保留旧 sticky。内部
ClassInfo/PreRegist/三态证据见
`motionplayer_d3dlayerbase_classinfo_preregist_adaptor_sticky_failure_four_binary_2026-08-17.md`。

- `argc < 2` 时 null `objthis` 安全，因为业务 factory 在构造前返回 `-1004`；
- exact one-Void 时 null `objthis` 安全，因为 descriptor 在业务 factory 前返回成功；
- `argc >= 2` 时不能把 null `objthis` 当成普通、可恢复的 `-1008` 边界；root ctor 的严格 `D3DLayerBase` 注册会更早失败；
- wrapper 中“factory 成功但 concrete adaptor 查找失败后 delete”的分支，应以一个可接受 `D3DLayerBase` 注册、但没有目标 concrete class adaptor 的非空 shell 来观察。

当后一个分支发生时，调用的是新根对象 primary vtable 的 deleting destructor：先执行完整的 `DrawDeviceD3D`/`D3D` 析构链，再释放对象存储，最后返回 `-1008`。该清理发生在 wrapper 内部，不把失败对象泄漏给调用者。

构造过程中注册的 `D3DLayerBase` adaptor 是 sticky 借用视图，不拥有根对象；concrete adaptor 才是唯一所有者。错误 concrete shell 在自身销毁时只销毁这个 sticky adaptor，不会再次删除已由 wrapper 清理的根对象。

## 异常与发布边界

> **2026-08-17 / V206–V207 订正：**上一个版本把 A64 的 landing-pad 形态外推成“四端都表达同一 new-expression 清理语义”，这个结论不成立。V206 逐端闭合 `D3D`，V207 又不依赖模板外推、单独逐端闭合 `DrawDeviceD3D`；两类最终具有同一目标矩阵，但这是八个 factory 实例/边界分别验证后的结果。

四端共同的只有发布顺序：先分配，再按 arg0、arg1 顺序转换并构造；只有完整构造成功后才写 `*out`，也只有 business factory 返回 0 后 raw descriptor 才尝试 concrete-adaptor attach。尚未发布分配在异常时如何处理，必须按实际目标分别描述：

| 两个 root factory 的目标 | 转换异常 | 根构造异常 | 证据边界 |
|---|---|---|---|
| A64 | raw delete | 若 primary base 已构造，则先调用 root-base 析构再 delete；更早阶段只 delete | factory 内有两个分阶段 landing pad |
| A32 | 泄漏尚未发布的 `0x13C` allocation | 同样泄漏 | 整个短 factory 没有 landing pad/EH cleanup |
| I64 | 泄漏尚未发布的 `0x1A0` allocation | 同样泄漏 | 整个短 factory 没有 landing pad/EH cleanup |
| I32 | SJLJ call-site 1/2 都 raw-delete `0x10C` allocation 后 resume | call-site 3 进入 terminate/trap，而非普通 cleanup | landing jump table `02 02 02 00` 与三个 call-site 状态 |

具体地，A64 `DrawDeviceD3D` 的两个 phase 位于 `0x52B7C8`/`0x52B7D8`，`D3D` 位于 `0x52CDC8`/`0x52CDD8`；I32 两类的 call-site 均为 1/2/3，两个 jump table 也都为 `02 02 02 00`。A32 与 I64 的两个短 factory 都没有自身 landing pad/EH cleanup。

因此，raw descriptor 在“business factory 已成功、但 concrete adaptor attach 失败”时调用 deleting destructor 的回滚，仍然是四端、两类共同且完整的；但它不能补救 business factory 自己尚未返回时的转换/构造异常。不能再从任意一个实例或单一目标向其他实例外推。`D3D` 的 ClassInfo/adaptor/容器闭环见 V206 报告，`DrawDeviceD3D` 的独立注册与异常闭环见 V207 报告。

## 可执行测试

`tests/unit-tests/plugins/motionplayer-dll.cpp` 的根表面测试现在覆盖：

1. 分别通过 `TJS_IGNOREPROP` 取得 `D3D` 与 `DrawDeviceD3D` 类名下的 raw factory descriptor；
2. 两类都验证 nested member 优先返回 `TJS_E_MEMBERNOTFOUND` 且保留 result；
3. 两类都验证 exact one-Void 在 null receiver 上成功且保留 result；
4. 两类都验证 zero args 与 one non-Void 返回 `TJS_E_BADPARAMCOUNT` 且保留 result；
5. 两类的 one-Void empty shell 在填充前调用实例属性都返回 `TJS_E_NATIVECLASSCRASH`，并遵循 typed-property result-clear；
6. 用 `DrawDeviceD3D` shell 作为错误 concrete receiver 调 `D3D` factory，验证 `-1008`、fresh-root rollback 与 result 保持；
7. 反向用 `D3D` shell 调 `DrawDeviceD3D` factory，验证两类 concrete class ID/adaptor 确实独立；
8. 两类都用各自正确 shell 和三个参数直接调 descriptor，验证只使用前两个参数并成功安装 native；
9. 两类填充后的 shell 都读回 `primaryWidth/primaryHeight == 320/240`。

测试刻意不对 two-argument ordinary path 使用 null receiver，因为四参考 root ctor 证明这不是可恢复错误边界。

## IDB 固化状态

四份 recovery IDB 均已保存：

- A64 新命名 `D3D` business factory 与两个 raw factory `FuncCall`；
- A32/I64/I32 额外命名两个类各自的 register helper、descriptor Create、descriptor ctor；
- business factory、descriptor 创建链和 wrapper 均附有参数门、结果保持、class-id 查找、失败 delete 与 null receiver 严格性的注释；
- 每库新增三个 factory 书签，覆盖两个 concrete wrapper 和 `D3D` business factory。

V206 又为独立 `D3D` ClassInfo/guard、注册与注销、concrete adaptor、四棵 root tree、具体析构体和上述异常矩阵补充类型、语义名、注释与书签，并重新保存四份 recovery IDB；详见 `analysis/motionplayer_d3d_classinfo_raw_factory_root_adaptor_containers_lifecycle_four_binary_2026-08-17.md`。

V207 对 `DrawDeviceD3D` 重复同样的四端证据门槛，补齐其独立 ClassInfo/adaptor/registration lifecycle，并把异常矩阵从“仅 D3D 已闭合”升级为两类各自闭合；详见 `analysis/motionplayer_drawdeviced3d_classinfo_raw_factory_adaptor_exception_destructor_four_binary_2026-08-17.md`。

## 被否定的旧假设

| 旧假设 | 四参考结论 |
|---|---|
| 根类用普通 typed constructor | 使用 `ncbNativeClassFactory<ClassT>` raw descriptor。 |
| one-Void 等价于 `(0, 0)` 构造 | one-Void 完全跳过 business factory，只留下空 adaptor。 |
| factory 会像 typed method 一样先验证 receiver/清 result | 不预检普通 receiver；所有路径都保留 result。 |
| 参数必须恰好两个 | 只要求至少两个；额外参数忽略。 |
| concrete adaptor 在 C++ ctor 内创建 | TJS native class 先创建空 adaptor，wrapper 在 ctor 成功后写入 native 指针。 |
| sticky `D3DLayerBase` adaptor 拥有根对象 | 它只是借用视图；non-sticky concrete adaptor 是唯一所有者。 |
| `DrawDeviceD3D` 和 `D3D` 只是同一类的两个名字 | class ID、descriptor vtable、factory、最终 vtable 和 concrete adaptor 全部独立。 |
| two-argument null receiver 一定返回 `-1008` | root ctor 会在 wrapper 的 adaptor-failure 分支之前严格访问 `D3DLayerBase` adaptor；不能当成安全错误返回。 |
