# Motion.EmotePlayer `initPhysics` typed binding / Variant owner 四参考复原（2026-08-15）

## 结论

四个当前参考二进制的 Primary 成员 #4 `initPhysics` 都不是一个
`EmotePlayer::initPhysics` 转发函数。注册器把两字成员指针的代码字段直接设为
`EmoteEngine_applyMetadata_guess`，调整字段为零；`draw`、`initPhysics`、
`unserialize` 三项又共用同一个“一枚 `tTJSVariant` 按值参数、`void` 返回”的
typed NCBind 模板实例。

因此源级结构应是：脚本名仍叫 `initPhysics`，但注册表达式直接引用 metadata
总装配成员；该成员的源级参数是 `tTJSVariant` **按值**，不是
`const tTJSVariant &`。typed 适配器先拥有首个脚本 Variant，核心在清空旧
metadata 状态之后再复制一次并转为对象。原本地的 const-ref 核心签名加派生
facade 转发层会产生相同的大部分业务结果，却不等于参考成员指针、模板实例和
引用计数生命周期。

## 四端映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| UTF-16LE `initPhysics` | `0x14D3DF4` | `0xD8478E` | `0x1019602E8` | `0x175264C` |
| registrar 中直接成员代码指针 | `0x67D038` | `0x56136A` | `0x1001B51E8` | `0x1B4E86` |
| `EmoteEngine_applyMetadata_guess` | `0x67A8B0` | `0x560020` | `0x1001B4468` | `0x1B3F58` |
| 一 Variant / void 工厂 | registrar 内联 | `0x56A718` | `0x1001C64D0` | `0x1C3890` |
| typed `FuncCall` | `0x68A24C` | `0x56A7F0` | `0x1001C6620` | `0x1C3A78` |
| 首 Variant 按值复制 helper | `0x689F40` | `0x56A3F4` | `0x1001C60E0` | `0x1C33E4` |
| member-pointer invoke helper | `FuncCall` 内联 | `0x56A89C` | `0x1001C66A0` | `0x1C3AFC` |

普通 IDA 字符串搜索在四端都返回零；UTF-16LE 字节检索各得到唯一命中，再由
xref 回到 `EmotePlayer_ncb_registerMembers_guess`。四个 registrar 在相邻三项
中表现一致：

```text
draw          -> 一 Variant / void typed 工厂 -> draw wrapper
initPhysics   -> 同一 typed 工厂             -> applyMetadata core
unserialize   -> 同一 typed 工厂             -> unserialize core
```

Android ARM64 把这一工厂的分配和构造内联进 registrar；另三端保留独立工厂。
这不改变模板身份或脚本边界。

## Function dispatch 与成员指针布局

| ABI | 完整 Function 对象 | embedded `ncbIMethodObject` | 存储成员指针 |
|---|---:|---:|---:|
| 64-bit | `0x40` bytes | object `+0x20` | code `+0x30`, adjustment `+0x38` |
| 32-bit | `0x24` bytes | object `+0x14` | code `+0x1C`, adjustment `+0x20` |

`initPhysics` 的 adjustment 在四端均为零。invoke helper 仍实现完整 Itanium
两字成员指针协议：先把 adjustment 右移一位加到 native payload；低位为 1 时
经调整后对象的 vtable 取虚函数，否则直接调用 code 字段。本项走后者。这说明
注册目标不是通过脚本 facade 再取 `engine()`，而是在同一个零偏移 Engine
payload 上直接落到 metadata core。

## typed `FuncCall` 边界顺序

忽略平台寄存器和异常展开后，四端共同顺序是：

```cpp
if (membername != nullptr)
    return BaseDispatch::FuncCall(...);       // reference 中为 MEMBERNOTFOUND 路径

if (objthis == nullptr)
    return TJS_E_NATIVECLASSCRASH;            // result 尚未清空

if (result != nullptr)
    result->Clear();                          // void typed 调用的 eager clear

if (numparams < 1)
    return TJS_E_BADPARAMCOUNT;

native = unwrapEmotePlayerPayload(objthis);
if (native == nullptr)
    return TJS_E_NATIVECLASSCRASH;

tTJSVariant adapterOwned = copyByValue(*param[0]);
(native->*storedMemberPointer)(adapterOwned);
destroy(adapterOwned);
return TJS_S_OK;
```

由此得到以下边界：

- null receiver 的优先级高于 arity，而且不会清空 `result`；
- 非空但错误类型的 receiver 在 `argc == 0` 时先得到
  `TJS_E_BADPARAMCOUNT`，在 `argc >= 1` 时才得到
  `TJS_E_NATIVECLASSCRASH`；
- `result` 在非空 receiver 下早于 arity/native unwrap 被清空，成功的 void
  调用不会重新写入它；
- 只要求首参，多余参数完全忽略；`argc` 覆盖的 `param[0]` 没有额外 null-slot
  防护；
- method-pointer 调用后的 `TJS_S_OK` 是 typed adapter 的返回，不是 core 写入
  的脚本值；C++ 异常仍按 NCBind hook 路径传播。

## Variant 复制与所有权链

四端“首 Variant 复制 helper”不是 ctor 专用。它同时被 EmotePlayer 构造模板
和本 typed method 模板调用；先前 IDB 中的
`EmotePlayer_normalizeFirstCtorArg_guess` 因而是过时窄命名。本轮统一改为
`NCB_copyFirstVariantArgByValue_guess`。

旧 NCBind 模板展开可见多段临时对象，而不是借用 `param[0]`：

1. 从 `*param[0]` copy-construct 临时 Variant；
2. direct-copy/assign 到参数 functor 的本地槽；
3. 经返回值临时再 copy-construct 最终按值实参；
4. 销毁中间临时；
5. core 返回后销毁 adapter-owned 实参。

`EmoteEngine_applyMetadata_guess` 随后还保持原有内部顺序：

```cpp
void applyMetadata_guess(tTJSVariant metadata) {
    resetMetadataState();
    tTJSVariant metadataValue(metadata); // 第二层 owner copy
    metadataValue.ToObject();
    ncbPropAccessor accessor(metadataValue);
    metadataValue.Clear();
    // property reads / controller rebuild ...
}
```

所以“reset 后才 copy”描述的是 core 的第二次复制；首层按值实参已在 typed
adapter 中持有 dispatch。即使脚本首参别名到即将被 reset 的 Engine Variant
成员，reset 也不会使源 dispatch 在第二次复制之前失效。

## 源码、测试与 IDB 调整

- `EmoteEngine::applyMetadata_guess` 改为按值 `tTJSVariant` 参数，保留 core
  内部第二次 copy；
- 删除仅为本地宏注册存在的 `EmotePlayer::initPhysics` 转发声明/定义；
- registrar 改为脚本名 `initPhysics` 直接绑定
  `&EmoteEngine::applyMetadata_guess`；NCBind 的 `RefClassT` 仍为
  `EmotePlayer`，所以 receiver 以 Primary class id 解包，再零偏移上转为 Engine；
- Catch2 新增 null receiver / eager result clear / arity-before-unwrap / surplus
  argument 边界以及按值成员签名静态断言；
- 四个 recovery IDB 写入 direct-target 注释、typed FuncCall 原型、按值 core
  landing 原型、工厂/invoke/复制 helper 语义名及两个导航书签。

## 验证

- motionplayer 单测翻译单元 Emscripten `-fsyntax-only`：通过；
- `Web Debug Build`：10-step 增量编译与最终 `index.html` 链接通过；仅出现仓库
  既有 `_tss`、`nodiscard`、pthread memory growth、JSPI 与 JS library 警告；
- 四个 recovery IDB：均已成功保存到各自 `out/ida-recovery/*` 路径。
