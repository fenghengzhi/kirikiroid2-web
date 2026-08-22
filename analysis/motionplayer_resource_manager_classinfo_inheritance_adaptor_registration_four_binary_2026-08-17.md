# Motion ResourceManager ClassInfo、继承边界、注册事务与 adaptor 生命周期四端恢复（V195）

日期：2026-08-17  
事实源：`reference/binaries/` 当前 Android arm64、Android armv7、iOS arm64、iOS armv7 四个参考二进制

## 1. 结论

本轮补齐了旧 `ResourceManager` NCB surface/constructor 报告尚未分离的模板层，并纠正了一个
很容易由 C++ 继承关系推出、但二进制并不支持的脚本层推断：

1. `ResourceManager` 在原生 C++ 对象中确实把 `SourceCache` 放在首基类、偏移零；
2. `ResourceManager` 与 `SourceCache` 的 NCB 身份却完全独立：它们有不同的
   `ncbClassInfo<T>::InfoT`、class ID、class object、adaptor 类型与 Setup/teardown 事务；
3. `ncbSubClassCheck<ResourceManager>::IsSubClass == true` 只选择“把一个 class dispatch 作为
   `TJS_STATICMEMBER` 挂到 `Motion`”的注册路径，不包含 `SourceCache` parent dispatch、父 class ID、
   base offset 或向上/向下转换 metadata；
4. `ResourceManager` registrar 重新登记的 `loadSource`、`clearCache`、`bufLayer` 是同一组
   `SourceCache` callback，member adjustment 均为零；这反映原生首基类布局，不会合并 ClassInfo；
5. 四端都有 public `ncbInstanceAdaptor<ResourceManager>::CreateAdaptor`，唯一 existing-native
   producer 是 `EmoteObject` constructor，调用参数固定为 `sticky=true, error=false`；
6. 这个 adaptor 只借用 `EmoteObject` 持有的 raw `ResourceManager`。CreateAdaptor 的任意失败、
   不兼容或异常边界都不会替调用方回收传入的 native；成功创建 shell 但拿不到 adaptor 时，
   函数还会返回该 shell；
7. direct script constructor 走另一条 owner 路径：native 被挂到 non-sticky CreateEmpty adaptor，
   attach 失败立即完整析构/free，成功后由 adaptor Invalidate/destructor 删除。

因此当前 portable 的 `class ResourceManager : public SourceCache` 是原生布局/直接 callback 复用
的恢复；不能据此写成脚本层 `ResourceManager extends SourceCache`，也不能让两类复用同一 adaptor
或 ClassInfo。

## 2. 独立 ClassInfo 状态

### 2.1 InfoT ABI

四端仍是 ncbind 的同一逻辑结构。

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

| 目标 | ResourceManager InfoT | guard | guarded static init |
|---|---:|---:|---:|
| Android arm64 | `0x1AB5088` | `0x1AB50A8` | `0x42EEB0` |
| Android armv7 | `0x11115F4` | `0x1111604` | `0x30138C` |
| iOS arm64 | `0x101ADF6D8` | `0x101ADF6F8` | `0x10014FB60`, `0x1001CADF0` |
| iOS armv7 | `0x1831808` | `0x1831818` | `0x151B94`, `0x1C8E86` |

Android 两端各保留一个可见的 guarded initializer。iOS 两端各保留两个逐指令同义的 initializer：
两份都先读同一个 guard，只有 guard bit 尚未设置时才把同一个 InfoT 清零并置 guard。由代码区位置
可推断，一份随 ResourceManager 注册模板实例出现，另一份随 EmoteObject/CreateAdaptor 消费模板
实例出现；“属于哪个源 TU”是位置推断，但“两份函数共享同一 tuple 与同一 guard”是直接二进制事实。

第二份 initializer 不代表第二个 ClassInfo。两个函数无论执行顺序如何，最多只有第一份实际清零，
后者观察到 guard 后直接返回。

### 2.3 Android 可见的 ClassInfo leaves

| 语义 | Android arm64 | Android armv7 |
|---|---:|---:|
| GetName | `0x6A8BFC` | `0x57C338` |
| GetID | `0x6A8C0C` | `0x57C344` |
| GetClassObject | `0x6A8C1C` | `0x57C350` |
| IsSubClass | `0x6A8C2C` | `0x57C35C` |
| Set | `0x6A8C34` | `0x57C360` |
| Clear | `0x6A8C6C` | `0x57C388` |

iOS 把相同操作按使用点内联。四端共同语义为：

```cpp
bool Set(name, classID, classObject) {
    if (initialized) return false;
    this->name = name;
    this->classID = classID;
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

`name` 和 `classObject` 都是 borrowed；Set 没有 AddRef，Clear 没有 Release。Set/Clear 也没有锁、
原子 publication、代际编号或读侧同步。static guard 只保护最初的零初始化，unregister 不重置 guard。

## 3. Motion publication 不是 SourceCache inheritance

### 3.1 wrapper 与 Setup

| 阶段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Motion wrapper | 根 registrar 内联，callsite `0x6D7444` | `0x5997C0` | `0x100126154` | `0x1251DC` |
| `ncbSubClassItem<ResourceManager>::Setup` | `0x6FBEA4` | `0x5B6F80` | `0x10014E6AC` | `0x150480` |
| RegistBegin | `0x6FC014` | `0x5B7040` | `0x10014E744` | `0x150574` |
| member registrar | `0x6A8C9C` | `0x57C3A8` | `0x100102E88` | `0x1002FC` |
| RegistEnd/UnregistEnd dispatcher | Setup 内联 | `0x5B7014` | `0x10014E948` | `0x150768` |
| AddDummyConstructor helper | Setup 内联 | `0x5B71BC` | `0x10014E998` | `0x150814` |
| dummy callback | `0x6FC290` | `0x5B71F8` | `0x10014E9FC` | `0x15084A` |

旧 A64 名称把 `0x6FBEA4` 叫作 ResourceManager wrapper；四端对齐后应纠正为 Setup。A64 的
wrapper 与 `ncbSubClassItem` allocation/DoItem 已被编译器直接内联进 Motion 根 registrar。

Motion 的相关 publication 顺序可见为：

```text
SourceCache -> ObjSource -> ResourceManager -> SeparateLayerAdaptor -> D3DAdaptor
```

每一项都先调用自己的 Setup，再各自 new 一个 `ncbSubClassItem<T>`，以当前名字登记到 Motion。
相邻顺序不构成父子关系；尤其 ResourceManager item 不引用 SourceCache item。

### 3.2 ncbSubClassItem 的实际 metadata

ResourceManager item 的四个虚调用在四端完全一致：

| 语义 | A64 | A32 | I64 | I32 | 返回/动作 |
|---|---:|---:|---:|---:|---|
| GetDispatch | `0x6FC298` | `0x5B7204` | `0x10014EA04` | `0x150854` | ResourceManager 当前 classObject |
| GetFlags | `0x6FC2A8` | `0x5B7210` | `0x10014EA14` | `0x150862` | `0x10000` / `TJS_STATICMEMBER` |
| GetType | `0x6FC2B0` | `0x5B7216` | `0x10014EA1C` | `0x150868` | `0` / `nitClass` |
| Release | `0x6FC2B8` | `0x5B721A` | `0x10014EA24` | `0x15086C` | delete item 本身 |

这个 item 没有 payload 字段，只有 vptr；没有 parent dispatch、base class ID、offset、cast thunk 或
SourceCache 指针。`GetDispatch` 每次从 ResourceManager 自己的 InfoT 取 classObject，Release 只删除
item，并不 Release borrowed class dispatch。

### 3.3 `IsSubClass == true` 的精确含义

Android 两个 leaf 直接返回 `1`，iOS 在使用点折叠为常量。该 bit 说明此 NCB 类型由
`NCB_REGISTER_SUBCLASS` 路径发布到外层 class；它不是 C++ RTTI 结果，也不表达“ResourceManager
是 SourceCache 的脚本子类”。

## 4. 注册与卸载事务

共同伪代码：

```cpp
bool Setup(name, isRegister) {
    if (isRegister && ResourceManagerClassInfo.classObject)
        return false;

    RegistSubClass delegate(name);
    RegistClass transaction(delegate, isRegister);
    transaction.Regist();

    return !isRegister || ResourceManagerClassInfo.classObject != nullptr;
}
```

register 模式的关键顺序：

1. 用传入的 `ResourceManager` 名字构造 native-class dispatch；
2. 把 `CreateEmptyAdaptor` 写成 native instance factory；
3. 生成 ResourceManager 自己的 TJS native class ID；
4. `Set(name,id,classObject)` 发布 ResourceManager InfoT，`initialized` 最后置一；
5. 把 class ID 写入 native-class dispatch；
6. 注册 no-op `finalize`；
7. 运行一条 typed `(Variant,int32)` constructor 和 12 个成员的 registrar；
8. RegistEnd 只在没有真实 constructor 时补 `TJS_E_ACCESSDENY` dummy。ResourceManager 已有 typed
   constructor，所以正常注册不会安装 dummy；
9. Motion wrapper new ResourceManager subclass item，并以 `TJS_STATICMEMBER` 挂入 Motion。

InfoT 在 finalize/member/item publication 之前已经可见。后续任何 exception 都没有自动 Clear/rollback，
因此与其他 NCB subclass 一样，存在“ClassInfo 已发布、table 或 Motion item 尚未完成”的中间状态。

unregister 模式不创建 class dispatch；它遍历同一 member table 做 removal，然后 Clear ResourceManager
InfoT。Clear 不 Release dispatch，也不重置 static guard。SourceCache InfoT 完全不受影响。

## 5. 两条 native publication 路径

### 5.1 direct script constructor

旧 surface 报告已经恢复：class-object CreateNew 先用 one-Void sentinel 创建 shell/empty adaptor，真实
`ResourceManager(Variant,int32)` constructor 随后分配并完整构造 native，再以 ResourceManager class
ID 对 `objthis` 做 GETINSTANCE attach。

| attach helper | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| construct + attach | `0x6E9B70` | `0x5A7E50` | `0x10013A6E8` | `0x13A79C` |

attach 使用的 class ID 正是本轮 InfoT 的 ID 字段，而不是 SourceCache ID。成功时把 native 写到
adaptor `+8/+4`，sticky 保持 false；失败则立即运行 ResourceManager derived destructor、SourceCache
base teardown 和 `operator delete`，返回 `TJS_E_NATIVECLASSCRASH`。

### 5.2 public CreateAdaptor

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| CreateAdaptor | `0x67B5EC` | `0x56083C` | `0x1001B4E40` | `0x1B49E0` |
| GetAdaptor | CreateAdaptor 内联 | `0x56A09C` | `0x1001C5CC4` | `0x1C2E98` |

共同伪代码：

```cpp
iTJSDispatch2 *CreateAdaptor(native, sticky=false, error=false) {
    classObject = ResourceManagerClassInfo.classObject;
    if (!classObject) {
        if (error) throw "No class object.";
        return nullptr;
    }

    global = TVPGetScriptDispatch();
    dummy = Void;
    status = classObject->CreateNew(..., &object, 1, &dummy, global);
    if (global) global->Release();

    if (status < 0 || !object) {
        if (error) throw "Can't create instance";
        return nullptr;
    }

    adaptor = GetAdaptor(object, error);
    if (adaptor) {
        adaptor->native = native;
        if (sticky) adaptor->sticky = true;
    }
    return object;
}
```

需要保留的非直觉边界：

- 传入的 `native` 从不被 CreateAdaptor 删除；
- `CreateNew` 返回 negative status 但同时给出 non-null object 时，函数返回 null，却没有 Release 该
  object；
- GETINSTANCE 返回失败且 `error=false` 时，GetAdaptor 返回 null，CreateAdaptor 仍返回已创建 shell；
- GETINSTANCE 返回成功但输出 adaptor 仍为 null 时，即使 `error=true` 也不抛，仍返回 shell；
- GETINSTANCE 失败且 `error=true` 时会抛；raw object/native 没有 RAII owner 替该模板回收；
- sticky 只在 adaptor 非空时置一。返回无 adaptor shell 时既没有 native slot，也没有 sticky owner
  语义。

### 5.3 EmoteObject 是唯一 existing-native producer

| 目标 | EmoteObject init | CreateAdaptor callsite | native 大小 |
|---|---:|---:|---:|
| Android arm64 | `0x67AF8C` | `0x67B028` | `0xE8` |
| Android armv7 | `0x5604B8` | `0x560514` | `0x80` |
| iOS arm64 | `0x1001B4984` | `0x1001B4A08` | `0xC8` |
| iOS armv7 | `0x1B4500` | `0x1B45B6` | `0x70` |

四端调用参数都为：

```cpp
rm = new ResourceManager(global.kag, 20_MiB);
self->resourceManagerRawOwner = rm;
dispatch = CreateAdaptor(rm, /*sticky=*/true, /*error=*/false);
engine = new EmoteEngine(temporaryVariant(dispatch));
```

raw owner 在 CreateAdaptor 之前已经写进 EmoteObject。正常路径中 temporary Variant 只存在于
constructor stack；Engine/Player 建立自己的 retained Variant 后，temporary 被销毁。EmoteObject 正常
析构仍是 Engine -> raw ResourceManager -> paths；sticky adaptor 即使先收到 Invalidate，也只清 slot，
不删除 raw ResourceManager。

CreateAdaptor 返回 null 时传给 Engine 的 Variant 为 Void；返回无 adaptor shell 时传给 Engine 的是
没有 ResourceManager native slot 的对象。两种情况都不回收 `self->resourceManagerRawOwner`。

## 6. adaptor ABI 与 teardown

### 6.1 layout

LP64：

```text
+0x00 vptr
+0x08 ResourceManager *native
+0x10 bool sticky
sizeof = 0x18
```

ILP32：

```text
+0x00 vptr
+0x04 ResourceManager *native
+0x08 bool sticky
sizeof = 0x0c
```

### 6.2 四端函数映射

| 语义 | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| CreateEmptyAdaptor | `0x6FC168` | `0x5B7128` | `0x10014E85C` | `0x1506D0` |
| finalize no-op | `0x6FC194` | `0x5B7148` | `0x10014E888` | `0x1506F0` |
| Invalidate | `0x6FC19C` | cluster `0x5B714C` | thunk `0x10014E890` | thunk `0x1506F4` |
| complete destructor | `0x6FC1E0` | entry `0x5B7150` | `0x10014E894` | `0x1506F8` |
| deleting destructor | `0x6FC240` | entry `0x5B7178` | `0x10014E8D8` | `0x150724` |
| shared native teardown | Invalidate 内联 | entry `0x5B719C` | `0x10014E90C` | `0x150748` |

Android armv7 的四个 Thumb ABI entry 被 IDA 合并成从 `0x5B714C` 开始的一个 function cluster。本轮
保留 function boundary，不做破坏性 split；cluster 注释与本报告都列出四个真实 entry。

共同 teardown：

```cpp
if (native && !sticky) {
    native->~ResourceManager();
    operator delete(native);
}
native = nullptr;
sticky = false;
```

non-sticky direct script instance 因而由 adaptor 删除；sticky EmoteObject instance 只清借用。完整/
删除析构与 Invalidate 的 owner gate 相同，deleting destructor 最后再 free adaptor。

ResourceManager destructor 的 derived/base 顺序仍由旧报告覆盖：先销毁 module/resource maps 与 derived
Variants/set/scalars，再销毁偏移零的 SourceCache base；adaptor 不调用 public `unloadAll` 或
`clearCache`，SourceCache cached Layer 也不会因此收到额外脚本 Invalidate。

## 7. C++ 首基类证据与 NCB 独立身份

四端支持 `ResourceManager : public SourceCache` 的原生证据为：

1. ResourceManager constructor 对同一个 native 起始地址执行 SourceCache construction；
2. ResourceManager destructor 完成 derived teardown 后，以同一个地址进入 SourceCache base teardown；
3. ResourceManager member table 直接绑定 SourceCache 的 `loadSource`、`clearCache`、`getBufLayer`，
   没有 forwarding shim；
4. property setter 与 callback adjustment slots 为零；
5. native 尺寸包含一份 SourceCache base，不存在第二份 cache topology。

脚本身份独立的证据为：

1. 两类 InfoT 地址完全不同；
2. 两类 Setup/RegistBegin/class ID/dispatch/CreateEmpty vtable 完全不同；
3. direct constructor 各自用自己的 class ID attach；
4. ResourceManager ncbSubClassItem 只返回 ResourceManager classObject；
5. item metadata 只有 static/class/type/release 四项，没有 SourceCache parent；
6. Motion 根 registrar 把 SourceCache 与 ResourceManager 作为两个平级静态成员分别发布。

所以“首基类、offset zero、callback 无 adjustment”与“脚本父类 metadata”必须分开建模。

## 8. portable 对齐

本轮不改运行时代码，只迁移/补强四端无地址注释：

- `cpp/plugins/motionplayer/main.cpp`：ResourceManager 独立 ClassInfo、平级 Motion publication、
  IsSubClass trait 不携带 SourceCache parent；
- `cpp/plugins/motionplayer/ResourceManager.h`：原生 offset-zero 首基类证据与独立 NCB 身份分离；
- `cpp/plugins/motionplayer/EmotePlayer.cpp`：唯一 producer 固定 `sticky=true,error=false`，失败不回收
  raw owner，null adaptor 仍可能返回 shell；
- `cpp/plugins/motionplayer/EmotePlayer.h`：同一 owner 边界的对象模型摘要。

`ncbInstanceAdaptor<ResourceManager>::CreateAdaptor(_rm, true)` 的默认第三参数就是 false，因此无需为了
注释显式化而制造代码差异。

## 9. recovery IDB 写回

四库完成：

- 8 个 typed data items：4 组 ResourceManager InfoT + guard；
- 82 个 function entry semantic names，全部 lookup readback；
- 78 个核心 ClassInfo/Setup/CreateAdaptor/adaptor/item function signatures；
- 93 条成功 data/function/callsite comments；
- 4 个 ResourceManager ClassInfo/Setup/adaptor bookmarks；
- 82 个 targeted force-recompile，全数成功；
- 20 个 initialized/name/id/classObject/guard logical-zero reads；
- 四个 recovery IDB 原位保存。

Android armv7 adaptor lifecycle cluster 只命名 function head 并在注释列出三个额外 ABI entry；未伪造
不存在的 IDA function boundary。

## 10. 验证

验证结果：

- ordinary/headless motionplayer syntax-only：通过；
- Web/wasmtime full build：通过；
- 两个 Wasm 的 Node `WebAssembly.Module` parse：通过；
- 两个 Wasm 的 llvm-objdump section parse：通过；
- Web/wasmtime CTest：命令成功，当前两个 build tree 均为 `No tests were found`；
- `git diff --check`：通过，仅有工作树既有 LF/CRLF 提示。

相对 V194 精确零变化：

| 指标 | Web | wasmtime/headless |
|---|---:|---:|
| file size | `85,654,197` B | `85,001,338` B |
| imports | `539` | `538` |
| exports | `69` | `69` |
| FUNCTION | `0x1BD23` | `0x1BA42` |
| GLOBAL | `0xD5B2` | `0xD5DA` |
| CODE | `0x1A4219A` | `0x19EA148` |
| DATA | `0x5A3FB7` | `0x5A1207` |
| name | `0x31848C0` | `0x3140756` |

另记录当前 artifact SHA-256，供后续纵切面直接比较：

- Web：`7346616D20C76D17D6FF0B11B82DB0BB3D37C3A2CAC36846BDE625A07F4951E4`；
- wasmtime/headless：`FAD194A44C87C8475E39BC35FDDCD2ADE7C6295915C9E1D0C581134072C4E3CC`。

编译只报告仓库既有 `_tss`、`nodiscard`、pthread memory-growth、JSPI 与 JS library warning；
没有本轮新增的错误或 warning 类型。

## 11. 尚未扩大结论的边界

- 本轮证明 ResourceManager 不包含 SourceCache parent metadata，但没有声称 TJS 引擎绝不允许脚本
  自己以 prototype/delegate 方式建立别的关系；这里只恢复插件注册器实际发布的内容；
- iOS 两个 guarded initializer 的源 TU 归属是按代码区与消费点推断，函数体、共享 tuple 与共享 guard
  本身已由二进制直接证明；
- CreateAdaptor 的 rare failure matrix 是模板边界恢复；实际唯一 producer 固定 error=false，正常产品
  数据通常只走“取得 adaptor、设 native、置 sticky”路径；
- 这一个纵切面闭合不表示整个 motionplayer 已经 100% 一比一，剩余对象和旧单目标注释仍需继续按
  四端门槛推进。
