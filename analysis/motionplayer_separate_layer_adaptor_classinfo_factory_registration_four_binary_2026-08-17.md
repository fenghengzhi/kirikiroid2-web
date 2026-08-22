# SeparateLayerAdaptor 独立 ClassInfo / Factory / adaptor owner gate 四参考恢复（V196）

## 1. 本轮结论

本轮只使用 `reference/binaries/` 的 Android arm64、Android armv7、iOS arm64、iOS armv7
四份当前参考，重新闭合 `Motion.SeparateLayerAdaptor` 的 NCB 模板层。既有三个 SLA 报告已经恢复
native 对象布局、owner/target/private-target Variant、两个 `std::map`、析构次序与 factory 的对象
构造；本轮补的是此前尚未完整成链的 ClassInfo、注册事务、Factory 描述符、空 shell adaptor、
attach、adaptor owner gate 与全局 producer topology。

共同结论如下：

1. `SeparateLayerAdaptor` 有自己的 `ncbClassInfo<T>::InfoT`、class ID、class object、Setup、
   `ncbInstanceAdaptor<T>` 和 `ncbSubClassItem<T>`；它不复用 Player、D3DAdaptor 或任何几何类身份；
2. `Factory(&SeparateLayerAdaptor::factory)` 是以运行时类名登记的 constructor/factory 描述符，
   不是公开的、字面名为 `Factory` 的脚本成员；
3. registrar 精确顺序仍是 `Factory -> absolute RW -> targetLayer RW -> clear -> assign raw`；
4. Factory 会把注册事务的 `constructorSeen` 标志置真；因此四端保留的 `-1002` dummy constructor
   代码正常情况下不会登记；
5. NativeClass 先通过 `CreateEmpty` 建立 `{native=null, sticky=false}` adaptor；生成的 factory bridge
   再构造 native，并用 SLA 自己的 class ID 从当前脚本 shell 取回 adaptor；
6. attach 成功只写 native 指针，不写 `sticky`；attach 任一失败都执行 native 完整析构和
   `operator delete`，返回 `TJS_E_NATIVECLASSCRASH`；
7. 四份 class-ID xref 的全量分类没有发现 existing-native `CreateAdaptor` producer。唯一 NCB native
   producer 是上述 factory，且它生成 non-sticky、由 adaptor owning 的实例；
8. Player 两个 class-ID consumer 只从脚本传入对象取 SLA native。Player 自身 persistent SLA 是
   另一条 raw owner 生命周期，未被包装成 sticky NCB shell；
9. adaptor 的 `sticky` 字段是 ncbind 模板结构能力，但当前 SLA 专用代码没有任何置 true 路径；
10. native 对象里的 `_owner` Variant 与 adaptor 的 `sticky` owner gate 是两层不同概念：前者保活
    脚本 owner/closure，后者只决定 adaptor teardown 是否删除 native。

## 2. 独立 ClassInfo ABI

### 2.1 InfoT 布局

LP64：

```text
+0x00  bool initialized
+0x01  padding[7]
+0x08  const tjs_char *name       // borrowed
+0x10  int32 classID
+0x14  padding[4]
+0x18  iTJSDispatch2 *classObject // borrowed
sizeof = 0x20
guard  = 0x08
```

ILP32：

```text
+0x00  bool initialized
+0x01  padding[3]
+0x04  const tjs_char *name       // borrowed
+0x08  int32 classID
+0x0c  iTJSDispatch2 *classObject // borrowed
sizeof = 0x10
guard  = 0x04
```

### 2.2 四端地址

| 目标 | SLA InfoT | guard | guarded static init |
|---|---:|---:|---:|
| Android arm64 | `0x1AB57E8` | `0x1AB5808` | `0x42F114` |
| Android armv7 | `0x1111B20` | `0x1111B30` | `0x3015FC` |
| iOS arm64 | `0x101ADF700` | `0x101ADF720` | `0x10014FB90` |
| iOS armv7 | `0x183181C` | `0x183182C` | `0x151BC0` |

四个 initializer 都只在 guard 尚未置位时把完整 tuple 清零，再置 guard。unregister 只 Clear InfoT，
不复位 guard，因此后续 register 依赖 Clear 后的 tuple，而不是再次运行静态初始化。

### 2.3 Android 可见 leaves

| 语义 | Android arm64 | Android armv7 |
|---|---:|---:|
| GetName | `0x6A92D8` | `0x57C538` |
| GetID | `0x6A92E8` | `0x57C544` |
| GetClassObject | `0x6A92F8` | `0x57C550` |
| IsSubClass | `0x6A9308` | `0x57C55C` |
| Set | `0x6A9310` | `0x57C560` |
| Clear | `0x6A9348` | `0x57C588` |
| InfoT zero ctor | `0x6A9364` | `0x57C59C` |

iOS 把同一组操作折叠进使用点。共同语义为：

```cpp
bool Set(const tjs_char *name, int32_t id, iTJSDispatch2 *classObject) {
    if (initialized) return false;
    this->name = name;
    this->classID = id;
    this->classObject = classObject;
    initialized = true; // publication marker last
    return true;
}

void Clear() {
    name = nullptr;
    classID = 0;
    classObject = nullptr;
    initialized = false;
}
```

两个指针都是 borrowed。Set 不 AddRef，Clear 不 Release；没有锁、原子发布、代际号或读侧同步。
RegistBegin 在 finalize 和五个 member row 之前发布 InfoT，故之后若抛异常，不存在自动回滚已发布
tuple 的事务保证。

## 3. Motion publication 与注册事务

### 3.1 四端函数映射

| 阶段 | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| Motion wrapper | 根 registrar 内联，callsite `0x6D74AC` | `0x599804` | `0x1001261CC` | `0x125224` |
| Setup | `0x6FC2C4` | `0x5B7224` | `0x10014EA30` | `0x150878` |
| RegistBegin | `0x6FC428` | `0x5B72E4` | `0x10014EAC8` | `0x15096C` |
| member registrar | `0x6A9378` | `0x57C5A8` | `0x100103080` | `0x1004A6` |
| RegistEnd/Clear | Setup 内联 | `0x5B72B8` | `0x10014ECCC` | `0x150B60` |
| AddDummy helper | Setup 内联 | `0x5B7460` | `0x10014ED1C` | `0x150C0C` |
| dummy callback | `0x6FC6A4` | `0x5B749C` | `0x10014ED80` | `0x150C42` |

共同控制流：

```cpp
bool Setup(const tjs_char *name, bool isRegister) {
    if (isRegister && Info.classObject)
        return false;

    RegistClass tx(name, isRegister);
    if (isRegister)
        RegistBegin(tx); // class, CreateEmpty, ID, InfoT, finalize

    RegisterFiveRows(tx); // Factory first; sets constructorSeen

    if (isRegister) {
        if (!tx.constructorSeen)
            AddDummyConstructor(tx); // dormant for SLA
    } else {
        Info.Clear();
    }

    return bool(Info.classObject) || !isRegister;
}
```

register 模式若同一 InfoT 已有 live class object，会在新 class 分配前返回 false。unregister 模式即使
当前 class object 已空也继续走 member unregister 和 Clear 逻辑。Clear 不负责 Release class dispatch；
外层 NativeClass/register infrastructure 持有其真正引用生命周期。

### 3.2 subclass item metadata

| 虚调用 | A64 | A32 | I64 | I32 | 行为 |
|---|---:|---:|---:|---:|---|
| GetDispatch | `0x6FC6AC` | `0x5B74A8` | `0x10014ED88` | `0x150C4C` | SLA 当前 classObject |
| GetFlags | `0x6FC6BC` | `0x5B74B4` | `0x10014ED98` | `0x150C5A` | `0x10000` / `TJS_STATICMEMBER` |
| GetType | `0x6FC6C4` | `0x5B74BA` | `0x10014EDA0` | `0x150C60` | `0` / `nitClass` |
| Release | `0x6FC6CC` | `0x5B74BE` | `0x10014EDA8` | `0x150C64` | delete item 本身 |

item 只有 vptr，没有 parent dispatch、parent class ID、native offset、cast thunk、Player 指针或
D3DAdaptor 指针。`IsSubClass==true` 只选择 Motion 下的静态发布路径，不表达 C++ 或脚本继承。

## 4. Factory 不是公开成员

四端 member registrar 的精确顺序：

```text
1. Factory(&SeparateLayerAdaptor::factory)
2. absolute RW
3. targetLayer RW
4. clear
5. assign raw/native
```

第一行的内部 descriptor 名来自当前注册事务的类名 `SeparateLayerAdaptor`。通用 member registrar
看到“当前 member 名等于 class 名”时先设置 `constructorSeen=true`，再把 factory dispatch 交给
NativeClass。因此脚本面没有一个额外的 `SeparateLayerAdaptor.Factory` 方法。

这也解释了为什么 `AddDummyConstructor` 与返回 `-1002/TJS_E_NOTIMPL` 的 callback 四端都存在，
但正常路径不会执行实际 RegisterNCM：它们是同一 ncbind 模板的保留 fallback，而不是 SLA 的第二
constructor。portable 的 `Factory(...)` macro 正确表达了这个 constructor descriptor；注释不应把
它数成 public method。

## 5. shell、factory 与 attach 数据流

### 5.1 四端映射

| 阶段 | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| generated factory FuncCall | `0x6EBECC` | `0x5AA1C8` | `0x10013D3E8` | `0x13DE14` |
| native factory + attach bridge | `0x6EBFA4` | `0x5AA258` | `0x10013D48C` | `0x13DE80` |
| allocate/construct helper | `0x6EC0BC` | 在 bridge 内联 | 在 bridge 内联 | `0x13DFC8` |
| native SLA ctor | `0x6C3DB4` | `0x58DBDC` | `0x1001298C4` | `0x128890` |
| CreateEmpty adaptor | `0x6FC57C` | `0x5B73CC` | `0x10014EBE0` | `0x150AC8` |
| finalize no-op | `0x6FC5A8` | `0x5B73EC` | `0x10014EC0C` | `0x150AE8` |

generated FuncCall 的共同外壳：

1. 非 null member name 返回 `TJS_E_MEMBERNOTFOUND/-1001`；
2. 单个 Void sentinel 直接成功，这是 ncbind 创建空 adaptor 时的 constructor probe；
3. 有 result 时先 Clear；
4. 没有参数 slot 返回 `TJS_E_BADPARAMCOUNT/-1004`；
5. 否则进入 native factory/attach bridge。

bridge 中的 typed factory 逻辑仍是既有 portable 源码的：逻辑用户参数数为零时使用 Void Variant，
大于等于一时只复制 `param[0]`，后续参数完全忽略。外层 sentinel/slot 协议与 typed callback 的逻辑
参数协议是 ncbind 两层 ABI，不能把前者误写成 SLA native ctor 自己强制要求参数。

### 5.2 attach 伪代码

```cpp
SeparateLayerAdaptor *native = new SeparateLayerAdaptor(arg0OrVoid);
if (!native)
    ThrowNativeClassInstanceCreationFailed();

ncbInstanceAdaptor<SeparateLayerAdaptor> *adaptor = nullptr;
if (objthis &&
    TJS_SUCCEEDED(objthis->NativeInstanceSupport(
        TJS_NIS_GETINSTANCE, Info.classID, &adaptor)) &&
    adaptor) {
    adaptor->native = native;
    // adaptor->sticky remains false
    return TJS_S_OK;
}

native->~SeparateLayerAdaptor();
operator delete(native);
return TJS_E_NATIVECLASSCRASH; // -1008
```

边界行为：

- attach 前 native 没有被任何 Variant/smart pointer 接管；
- 缺 receiver、NativeInstanceSupport 失败、返回 null adaptor 都走同一完整 rollback；
- attach 成功不写 result Variant；构造结果由当前脚本 shell 表达；
- attach 直接覆盖 adaptor native slot，没有先清理旧 slot。正常 constructor 流保证 slot 为空；若外部
  非法重入同一 shell，旧 native 可能泄漏，bridge 本身没有防护；
- bridge 不写 `sticky=true`，也不调用 generic existing-native `CreateAdaptor`；
- native `new` 的异常路径由 C++/unwind 处理；A64/I32 保留显式 null 检查及友好错误文本，不应据此
  推导出所有平台使用 nothrow allocation。

## 6. adaptor ABI 与 owner gate

LP64：

```text
+0x00 vptr
+0x08 SeparateLayerAdaptor *native
+0x10 bool sticky
sizeof = 0x18
```

ILP32：

```text
+0x00 vptr
+0x04 SeparateLayerAdaptor *native
+0x08 bool sticky
sizeof = 0x0c
```

### 6.1 生命周期映射

| 语义 | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| Invalidate | `0x6FC5B0` | entry `0x5B73F0` | thunk `0x10014EC14` | thunk `0x150AEC` |
| complete destructor | `0x6FC5F4` | entry `0x5B73F4` | `0x10014EC18` | `0x150AF0` |
| deleting destructor | `0x6FC654` | entry `0x5B741C` | `0x10014EC5C` | `0x150B1C` |
| shared native teardown | Invalidate 内联 | entry `0x5B7440` | `0x10014EC90` | `0x150B40` |

Android armv7 的四个 Thumb ABI entry 被 IDA 合并为从 `0x5B73F0` 开始的 cluster。本轮保留该
function boundary，只在入口行补语义注释，不做重叠函数的破坏性 split。

共同 teardown：

```cpp
if (native && !sticky) {
    native->~SeparateLayerAdaptor();
    operator delete(native);
}
native = nullptr;
sticky = false;
```

Invalidate 与两类析构共享完全相同的 owner gate；deleting destructor 最后再 free adaptor。Factory
实例从 CreateEmpty 开始即 `sticky=false`，所以 shell Invalidate/析构负责删除 native。当前 SLA
代码没有 existing-native producer，因而 `sticky=true` 分支只是 template ABI 能力，四端都没有
SLA 专用写 true callsite。

native `_owner` Variant 的生命周期保持旧报告结论：它由 targetLayer 解出并持有脚本 dispatch，
析构时在两个 map 和其余 Variant 的反序中释放。该 Variant 的“owner”不改变 adaptor sticky，也不
决定 native memory 的 delete 权限。

## 7. producer / consumer topology

对 InfoT/class ID 的四端全量 data xref 分类得到三组：

1. 注册/静态初始化/InfoT leaves；
2. factory 与五个 member wrapper，用 class ID 从 receiver shell 取 native；
3. Player 的 `drawToLayerRecursive` 与 `draw`，从脚本提供的 SLA 对象取 native。

Player consumer：

| 语义 | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| drawToLayerRecursive | `0x6D0160` | `0x595720` | `0x10012139C` | `0x120168` |
| draw | `0x6D3398` | `0x597864` | `0x100123C84` | `0x122F28` |

没有第四组 existing-native producer：没有 `CreateNew -> GetAdaptor -> native=existing -> sticky=true`，
也没有把 Player persistent raw SLA 发布成脚本 shell 的路径。故当前 owner 图为：

```text
script new Motion.SeparateLayerAdaptor(...)
    -> NativeClass CreateEmpty shell adaptor (non-sticky)
    -> typed factory allocates native
    -> attach by SLA class ID
    -> shell adaptor owns native
    -> Invalidate/dtor destroys native

Player persistent SLA
    -> Player raw slot owns native directly
    -> never attached to NCB shell

Player draw consumers
    -> borrow native from script-supplied SLA shell
    -> never transfer ownership or set sticky
```

## 8. portable 源码核对

本轮不需要改 executable behavior。现有：

```cpp
NCB_REGISTER_SUBCLASS_DELAY(SeparateLayerAdaptor) {
    Factory(&SeparateLayerAdaptor::factory);
    NCB_PROPERTY(absolute, getAbsolute, setAbsolute);
    NCB_PROPERTY(targetLayer, getTargetLayer, setTargetLayer);
    NCB_METHOD(clear);
    RawCallback(TJS_W("assign"), &SeparateLayerAdaptor::assignCompat, 0);
}
```

以及 `factory` 的 optional arg0 / surplus-ignore 行为已经匹配四端。只更新注释，明确：

- 独立 ClassInfo 和 vptr-only static subclass publication；
- Factory 的 constructor descriptor 身份与 dummy suppression；
- CreateEmpty -> non-sticky -> attach -> rollback；
- 没有 existing-native/sticky producer；
- Player raw owner 与 class-ID consumer 不得混成 NCB producer。

这轮不把 reference 绝对地址写进 compiled source；地址只保留在本报告和 recovery IDB。

## 9. IDB 写回

四份 recovery IDB 本轮写回：

- 8 个 typed data items：4 组 SLA InfoT + guard；
- 4 组带显式 padding 的 LP64/ILP32 InfoT layout type；
- 85 次核心 ClassInfo/Setup/Factory/adaptor/item 语义 rename；
- 85 个核心 function signature；
- 77 个 data/function/entry comments（含 A32 两个 cluster interior line comments）；
- 4 个 V196 bookmarks；
- 98 个相关函数 force-recompile/readback target。

命名仍保留 `_guess`，因为参考目标无符号；对象/模板身份由四端结构、xref、vtable 与调用图共同支持，
但并非原始符号恢复。

## 10. 验证

验证全部通过：

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两种 `em++ -fsyntax-only` 均通过，只保留既有
  `_tss` warning；
- `cmake --build out/web/debug` 与 `cmake --build out/wasmtime/debug` 均完成最终链接；
- Node `WebAssembly.Module` 解析、import/export 计数以及 `llvm-objdump` section 解析均通过；
- `ctest --test-dir out/web/debug` 与 `ctest --test-dir out/wasmtime/debug` 命令均成功，但两个
  配置仍显示 `No tests were found!!!`；
- scoped 与全 worktree `git diff --check` 均无 whitespace error，只显示工作树中既有的
  LF/CRLF warning。

本轮只修改注释和分析文件，两份 wasm 与 V195 字节级完全相同：

| target | size | SHA-256 | imports / exports |
|---|---:|---|---:|
| Web | 85,654,197 | `7346616D20C76D17D6FF0B11B82DB0BB3D37C3A2CAC36846BDE625A07F4951E4` | 539 / 69 |
| Wasmtime/headless | 85,001,338 | `FAD194A44C87C8475E39BC35FDDCD2ADE7C6295915C9E1D0C581134072C4E3CC` | 538 / 69 |

结构指标也逐项保持 V195：Web 的 FUNCTION/GLOBAL/CODE/DATA/name 为
`0x1BD23/0xD5B2/0x1A4219A/0x5A3FB7/0x31848C0`；Wasmtime/headless 为
`0x1BA42/0xD5DA/0x19EA148/0x5A1207/0x3140756`。因此本轮 ClassInfo/Factory 恢复与
portable 注释补强没有改变 executable bytes 或 Wasm ABI 表面。

## 11. 与既有 SLA 报告的分工

本报告不替代下列已闭合纵切面：

- `motionplayer_separate_layer_adaptor_four_binary_2026-08-13.md`：native 字段、member callback 与
  render-facing 行为；
- `motionplayer_separate_layer_adaptor_object_lifecycle_four_binary_2026-08-13.md`：native ctor/dtor、
  owner/target/private target 与 map 释放顺序；
- `motionplayer_separate_layer_payload_map_node_abi_four_binary_2026-08-15.md`：两个 ordered map 的
  object/header/node/value ABI 和边界行为。

本轮新增的是它们外层的 NCB class/shell/registration/ownership 拓扑；后续纵切面应继续把 native
行为和这个 shell 生命周期分层描述，避免再把 `_owner`、Player raw owner 和 adaptor `sticky` 混同。
