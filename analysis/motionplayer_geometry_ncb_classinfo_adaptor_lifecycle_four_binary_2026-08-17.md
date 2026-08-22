# Motion.Point / Circle / Rect / Quad：NCB ClassInfo、注册调用链与 adaptor 生命周期四参考复原（2026-08-17）

## 1. 范围与结论

本轮延伸 `analysis/motionplayer_geometry_four_binary_2026-08-11.md` 已恢复的
几何记录、getter、`contains` 和整记录装箱，专门闭合此前未完整描述的：

- 四类各自的 `ncbClassInfo<T>::InfoT` 静态状态及动态初始化 guard；
- `Motion` registrar → subclass wrapper → `ncbSubClassItem<T>::Setup` →
  `ncbRegistSubClass<T>::RegistBegin/RegistEnd` 的注册/卸载链；
- `ncbInstanceAdaptor<T>` 的 ABI 布局、native owner、sticky 语义、失效和析构；
- `LayerGetter.shape` 的整记录复制、单 Void 构造哨兵、dispatch 引用交接和失败泄漏；
- 默认脚本构造、`NativeInstanceSupport` 绑定失败、异常回滚和无同步边界。

四端共同结论是：Point、Circle、Rect、Quad 虽共享同一种完整 `HitData`
记录布局，却不共享 NCB class state。每一类都有独立的 `InfoT`、独立动态初始化
guard、独立 native-class object、class ID 和 adaptor specialization。

本地 `cpp/core/plugin/ncbind.hpp` 保留了参考实现使用的模板结构；生产算法无需
改写。本轮只把新证据同步到 `main.cpp`、`PlayerLayerQuery.cpp` 和
`SourceCache.h` 的注释中，避免继续把注册包装器、ClassInfo 和 native owner
混为同一层。

## 2. 重要纠偏：物理相邻不是跨链接器的类型身份

从 V190 selector cluster 的尾部继续查看 BSS 时，Android 两端的物理后继确实是
Point/Circle/Rect/Quad 的 `ncbClassInfo<T>::InfoT + guard`。但 iOS 链接器把同一批
vague-linkage 模板静态量 coalesce/重排到了另一个 BSS cluster：

- iOS arm64 的语义状态从 `0x101ADF570` 开始，而不是物理后继
  `0x101B69B20`；
- iOS armv7 的语义状态从 `0x1831754` 开始，而不是物理后继
  `0x187D72C`。

后两个“物理后继”是无关的聚合 BSS。它们没有几何注册、构造或 adaptor 的数据
xref，因此没有被命名为 Point 状态。身份判定以四类 Setup、RegistBegin、
CreateAdaptor、构造器和 init-array 的实际引用为准。这个纠偏说明：Android 的
邻接只能作为该目标的布局事实，不能外推成 iOS 的源码声明顺序。

## 3. `ncbClassInfo<T>::InfoT` 精确 ABI

### 3.1 LP64

```text
+0x00  bool initialized
+0x01  ABI padding[7]
+0x08  const tjs_char *name
+0x10  int32 id
+0x14  ABI padding[4]
+0x18  classObject pointer
sizeof = 0x20
紧随其后的动态初始化 guard = 0x08 bytes
每个 class state + guard 的跨度 = 0x28
```

### 3.2 32-bit

```text
+0x00  bool initialized
+0x01  ABI padding[3]
+0x04  const tjs_char *name
+0x08  int32 id
+0x0c  classObject pointer
sizeof = 0x10
紧随其后的动态初始化 guard = 0x04 bytes
每个 class state + guard 的跨度 = 0x14
```

四个文件中，四类的 16 个字段记录和 16 个 guard 在文件/BSS 初值均为零。本轮按
`initialized/name/id/classObject/guard` 每类五次读取，共完成 80 次逐字段零值
readback。

### 3.3 四端状态地址

| 类 | Android arm64 state / guard | Android armv7 state / guard | iOS arm64 state / guard | iOS armv7 state / guard |
|---|---:|---:|---:|---:|
| Point | `0x1AB5680` / `0x1AB56A0` | `0x1111A6C` / `0x1111A7C` | `0x101ADF570` / `0x101ADF590` | `0x1831754` / `0x1831764` |
| Circle | `0x1AB56A8` / `0x1AB56C8` | `0x1111A80` / `0x1111A90` | `0x101ADF598` / `0x101ADF5B8` | `0x1831768` / `0x1831778` |
| Rect | `0x1AB56D0` / `0x1AB56F0` | `0x1111A94` / `0x1111AA4` | `0x101ADF5C0` / `0x101ADF5E0` | `0x183177C` / `0x183178C` |
| Quad | `0x1AB56F8` / `0x1AB5718` | `0x1111AA8` / `0x1111AB8` | `0x101ADF5E8` / `0x101ADF608` | `0x1831790` / `0x18317A0` |

恢复库统一使用类型 `ncbClassInfoState_guess`，并把四类状态命名为
`g_ncbClassInfo_<Class>_guess`，guard 命名为
`g_ncbClassInfo_<Class>_guard_guess`。

## 4. 静态初始化

### 4.1 init-array 函数

| 类 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Point | `0x42EF64` | `0x30144C` | `0x10014F9B0` | `0x151A08` |
| Circle | `0x42EF94` | `0x30147C` | `0x10014F9E0` | `0x151A34` |
| Rect | `0x42EFC4` | `0x3014AC` | `0x10014FA10` | `0x151A60` |
| Quad | `0x42EFF4` | `0x3014DC` | `0x10014FA40` | `0x151A8C` |

共同伪代码为：

```cpp
if ((guard & 1) == 0) {
    info.initialized = false;
    info.name = nullptr;
    info.id = 0;
    info.classObject = nullptr;
    guard = 1;
}
```

这里的 guard 是 namespace/template static 的动态初始化标志，不是 function-local
static 的 Itanium `__cxa_guard`。四端均无 `__cxa_guard_acquire/release`、atomic、
lock 或 TLS；guard 最后以普通 store 发布。`InfoT` 构造不抛异常，且四个字段均为
trivial/non-owning 值，因此没有 atexit destructor。

## 5. 注册调用链

### 5.1 Motion subclass wrapper

| 类 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Point | registrar 内联/不同拆分 | `0x5995A0` | `0x100125D94` | `0x124F9C` |
| Circle | registrar 内联/不同拆分 | `0x5995E4` | `0x100125E0C` | `0x124FE4` |
| Rect | registrar 内联/不同拆分 | `0x599628` | `0x100125E84` | `0x12502C` |
| Quad | registrar 内联/不同拆分 | `0x59966C` | `0x100125EFC` | `0x125074` |

这些包装层调用相应 `Setup(name, isRegist)`。注册成功后把
`ClassInfo::GetClassObject()` 作为 static `nitClass` 成员发布到 Motion；卸载时，
Setup/UnregistEnd 先 Clear ClassInfo，随后父 Motion 成员生命周期完成移除。

### 5.2 `ncbSubClassItem<T>::Setup`

| 类 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Point | `0x6F9AC8` | `0x5B599C` | `0x10014C970` | `0x14E378` |
| Circle | `0x6FA118` | `0x5B5D1C` | `0x10014CE34` | `0x14E8D0` |
| Rect | `0x6FA508` | `0x5B5FB4` | `0x10014D1A8` | `0x14ECBC` |
| Quad | `0x6FA8F8` | `0x5B624C` | `0x10014D51C` | `0x14F0A8` |

共同控制流：

```cpp
if (isRegist && ClassInfo::GetClassObject())
    return false;

ncbRegistSubClass<T> delegate(name);
ncbRegistClass<ncbRegistSubClass<T>> registration(delegate, isRegist);
registration.Regist();
return !isRegist || ClassInfo::GetClassObject() != nullptr;
```

重复注册的早期 gate 测试 `classObject`，不是 `initialized`。这两个字段没有原子
一致性，故并发 Set/Clear 时理论上可以出现 gate 与其余字段不一致的状态。

### 5.3 `RegistBegin`

| 类 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Point | `0x6F9C2C` | `0x5B5A5C` | `0x10014CA08` | `0x14E46C` |
| Circle | `0x6FA27C` | `0x5B5DDC` | `0x10014CECC` | `0x14E9C4` |
| Rect | `0x6FA66C` | `0x5B6074` | `0x10014D240` | `0x14EDB0` |
| Quad | `0x6FAA5C` | `0x5B630C` | `0x10014D5B4` | `0x14F19C` |

源码级顺序为：

1. `TJSCreateNativeClassForPlugin(name, CreateEmptyAdaptor)` 创建 class object；
2. `TJSRegisterNativeClass(name)` 取得 class ID；
3. `ClassInfo::Set(name, id, classObject)`；
4. `TJSNativeClassSetClassID(classObject, id)`；
5. 注册空 `finalize` method；
6. 注册各类的 constructor/property/method rows；
7. subclass `RegistEnd` 仅在无 constructor 时补 dummy constructor。

`ClassInfo::Set` 在 `initialized` 已真时返回 false，调用者随后抛出
`Already registerd class.`。Set 的源码语义是 name → id → classObject →
initialized=true，Clear 是 name → id → classObject → initialized=false。Android
保留独立 Set/Clear leaf；iOS 将它们内联。优化器在某些内联 RegistBegin 中可以重排
最前面的普通 store，因此该顺序不能当作并发 publication barrier。

`InfoT` 不持有 class object 引用，Set/Clear 均不 AddRef/Release。所有权由 native-class
注册和父 Motion 的 class member 图管理。

### 5.4 部分提交和异常边界

- 在 Set 之前失败，ClassInfo 仍为零；已创建的 class object/ID 是否被上层回收取决于
  具体抛出点，InfoT 自身不提供回滚。
- Set 因重复注册返回 false 时，刚创建的 class object/ID 尚未被 InfoT 接管；该路径可
  留下新注册状态泄漏。
- Set 成功后，`SetClassID`、finalize/member registration、dummy constructor 或父 Motion
  member publication 任一步失败，都没有自动 Clear；InfoT 可保持部分注册状态。
- 卸载调用 subclass `UnregistEnd`，按 name/id/classObject/initialized 清零；Clear 不释放
  class object。iOS 的独立 RegistEnd 地址为：Point `0x10014CBFC/0x14E654`、Circle
  `0x10014D0C0/0x14EBAC`、Rect `0x10014D434/0x14EF98`、Quad
  `0x10014D7A8/0x14F384`。Android 相同模板语义被不同程度内联/拆分。
- 全链无 mutex、atomic 或 TLS。并发注册、CreateAdaptor 和卸载属于 data race；读者可
  观察新 classObject 配旧/零 ID，或在 ID 已清零时仍暂时读到旧 classObject。

## 6. 默认 native 构造

真正分配几何记录、只写 type 的 leaf：

| 类 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Point | `0x6DCC98` | `0x59D8E8` | `0x10012CFFC` | `0x12BAD8` |
| Circle | `0x6DD810` | `0x59E41C` | `0x10012DDC8` | `0x12C9D8` |
| Rect | `0x6DE3D0` | `0x59F04C` | `0x10012ED6C` | `0x12D940` |
| Quad | `0x6DED5C` | `0x59FA1C` | `0x10012F9A4` | `0x12E614` |

LP64 与 Android armv7 分配 `0x80`，iOS armv7 因 double 对齐分配 `0x7c`。
构造只写 type 0/1/2/3，不清 15 个 double。普通脚本 `new Motion.<Shape>()`
因此保留这些槽位的分配器旧内容。

NCB constructor wrapper 随后用当前 `ClassInfo.id` 调
`NativeInstanceSupport(TJS_NIS_GETINSTANCE, id, &adaptor)`：

- 成功时把 allocation 写入 adaptor native slot；默认 adaptor 非 sticky，成为 owner；
- lookup/attach 失败时 delete allocation，返回 `TJS_E_NATIVECLASSCRASH`；
- constructor wrapper 抛异常时 landing cleanup 同样 delete 尚未交接的 allocation；
- 一个且仅一个 Void 参数属于特殊哨兵：factory 直接返回 `TJS_S_OK`，不再创建 native
  allocation。这正是 `CreateAdaptor` 用来装箱已有 shape copy 的路径。

## 7. `ncbInstanceAdaptor<T>` ABI 与析构

### 7.1 布局

```text
LP64, sizeof 0x18:
  +0x00 vptr
  +0x08 NativeClassT *_instance
  +0x10 bool _sticky

32-bit, sizeof 0x0c:
  +0x00 vptr
  +0x04 NativeClassT *_instance
  +0x08 bool _sticky
```

CreateEmpty 初始化 native=null、sticky=false。`Invalidate()` 与 destructor 都调用同一
`_deleteInstance()`：

```cpp
if (_instance && !_sticky)
    delete _instance;
_instance = nullptr;
_sticky = false;
```

所以重复 Invalidate、随后 destructor、或析构变体之间不会 double-delete。geometry 的
普通构造和 shape boxing 都使用 sticky=false；成功 attach 后 adaptor 是唯一 native owner。

### 7.2 CreateEmpty 地址

| 类 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Point | `0x6F9D80` | `0x5B5B44` | `0x10014CB20` | `0x14E5C8` |
| Circle | `0x6FA3D0` | `0x5B5EC4` | `0x10014CFE4` | `0x14EB20` |
| Rect | `0x6FA7C0` | `0x5B615C` | `0x10014D358` | `0x14EF0C` |
| Quad | `0x6FABB0` | `0x5B63F4` | `0x10014D6CC` | `0x14F2F8` |

### 7.3 Invalidate 地址

| 类 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Point | `0x6F9DB4` | `0x5B5B68` | `0x10014CB54` | `0x14E5EC` |
| Circle | `0x6FA404` | `0x5B5EE8` | `0x10014D018` | `0x14EB44` |
| Rect | `0x6FA7F4` | `0x5B6180` | `0x10014D38C` | `0x14EF30` |
| Quad | `0x6FABE4` | `0x5B6418` | `0x10014D700` | `0x14F31C` |

### 7.4 complete destructor 地址

| 类 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Point | `0x6F9DEC` | `0x5B5B84` | `0x10014CBA4` | `0x14E61A` |
| Circle | `0x6FA43C` | `0x5B5F04` | `0x10014D068` | `0x14EB72` |
| Rect | `0x6FA82C` | `0x5B619C` | `0x10014D3DC` | `0x14EF5E` |
| Quad | `0x6FAC1C` | `0x5B6434` | `0x10014D750` | `0x14F34A` |

### 7.5 deleting destructor 地址

| 类 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Point | `0x6F9E40` | `0x5B5BBC` | `0x10014CB90` | `0x14E60A` |
| Circle | `0x6FA490` | `0x5B5F3C` | `0x10014D054` | `0x14EB62` |
| Rect | `0x6FA880` | `0x5B61D4` | `0x10014D3C8` | `0x14EF4E` |
| Quad | `0x6FAC70` | `0x5B646C` | `0x10014D73C` | `0x14F33A` |

## 8. `LayerGetter.shape` 整记录装箱和 CreateAdaptor

### 8.1 boxer 与 class-specific helper

| 功能 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| full-record boxer | `0x68F2C0` | `0x56E914` | `0x1000F0DC4` | `0xECF54` |
| Point CreateAdaptor | boxer 内联 | `0x56E9A4` | `0x1000F0ED4` | `0xECFE4` |
| Circle CreateAdaptor | boxer 内联 | `0x56EA8C` | `0x1000F0FFC` | `0xED13C` |
| Rect CreateAdaptor | boxer 内联 | `0x56EB74` | `0x1000F1124` | `0xED294` |
| Quad CreateAdaptor | boxer 内联 | `0x56EC5C` | `0x1000F124C` | `0xED3EC` |

boxer 对 type 0..3 先分配完整记录，再复制所有字节：LP64/Android armv7 为
`0x80`，iOS armv7 为 `0x7c`。它不只复制当前 shape 可见的坐标槽。未知 type
直接返回 Void，不分配。

### 8.2 CreateAdaptor 精确 owner 交接

```cpp
classObject = ClassInfo::GetClassObject();
if (!classObject)
    return nullptr;

global = TVPGetScriptDispatch();
dummy = Void;
r = classObject->CreateNew(..., &obj, 1, &dummy, global);
if (global)
    global->Release();

if (TJS_FAILED(r) || !obj)
    return nullptr;

adaptor = GetAdaptor(obj, err);
if (adaptor) {
    adaptor->_instance = preallocatedShape;
    if (sticky)
        adaptor->_sticky = true;
}
return obj;
```

当前 shape getter 传 `sticky=false, err=false`。可观察边界：

- shape copy 在读取 ClassInfo 前已分配；classObject 为 null 时直接泄漏；
- `CreateNew` 返回失败或 null 时，shape copy 泄漏；
- `CreateNew` 抛异常时，没有 native-shape RAII；shape copy 泄漏，且已取得的 global
  dispatch 也越过正常 Release 点；
- 新 script object 存在但 `GetAdaptor` 不兼容时，err=false 不抛异常，函数仍返回 script
  object，但不 attach native pointer；shape copy 泄漏；
- compatible adaptor 成功时写入 native slot，非 sticky adaptor 从此拥有 shape copy；
- `makeShapeVariant` 用 `(dispatch, dispatch)` 构造 Variant，Variant retain dispatch，随后
  对 CreateNew 返回的本地引用执行一次 Release。最终 script object → adaptor → native
  shape 形成 owner 链。

这解释了为什么不能把本地 helper 改成 `std::unique_ptr<Shape>` 再无条件回滚：那会消除
参考实现对脚本可观察的 allocation/lifetime 边界。

## 9. 本地源码同步

- `cpp/plugins/motionplayer/main.cpp`
  - 明确四个 delayed class 各有独立、non-owning、unsynchronized ClassInfo；
  - 明确 delayed macro 只实例化状态/成员，实际 Setup 由 Motion 内的 NCB_SUBCLASS 触发；
  - 明确卸载时 Clear 先于父 Motion member 生命周期完成。
- `cpp/plugins/motionplayer/PlayerLayerQuery.cpp`
  - 记录已有 native copy、单 Void constructor sentinel、Variant/dispatch 引用交接；
  - 记录 class missing、CreateNew failure/throw 和 incompatible adaptor 的泄漏；
  - 记录 incompatible adaptor 仍可返回新 script object 的边界。
- `cpp/plugins/motionplayer/SourceCache.h`
  - 明确 non-sticky adaptor 只在成功 attach 后拥有 heap facade；
  - 明确 per-type ClassInfo 是独立的静态 non-owning lookup。

这些都是注释同步，没有改变生产控制流。

## 10. IDB 固化

四个 recovery IDB 本轮完成：

- 4 个 `ncbClassInfoState_guess` ABI type；
- 32 个 typed data item（16 个 InfoT + 16 个 guard）；
- 80 次字段/guard 原始零值 readback；
- 200 个函数语义重命名：Android arm64 56、Android armv7 64、iOS arm64 40、
  iOS armv7 40；
- 180 条状态、guard、初始化、Setup、RegistBegin/End、CreateAdaptor、boxer、构造和
  析构注释；
- 4 个 V191 bookmark；
- 132 个函数 force-recompile，并逐库对 static init、Motion wrapper、Setup、
  RegistBegin/End、CreateAdaptor、boxer 和 destructor 做 fresh decompile readback；
- 四库均已原位保存。

读回确认未把 iOS 的无关物理邻接 BSS 命名为几何状态，也未留下旧巨大聚合 data item
覆盖 class/guard 边界。

## 11. 验证

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两套 motionplayer unit-test TU syntax-only
  均 exit 0；只有仓库既有 `_tss` deprecated warning；
- Web Debug 和 Wasmtime Headless Debug 均完成最终构建/链接；
- Node `WebAssembly.Module` 成功 parse 两份当前 `index.wasm`；
- `llvm-objdump -h` 成功读取两份完整 section table；
- Web/Headless CTest 均 exit 0，但两个 build tree 当前都是 `No tests were found`，所以本轮
  只声明双模式 test TU 编译覆盖，不虚报 runtime unit-test 执行；
- scoped `git diff --check` 通过；只有工作树既有 LF/CRLF 提示。

V191 artifact：

| 配置 | 总大小 | imports / exports | FUNCTION | GLOBAL | CODE | DATA | name |
|---|---:|---:|---:|---:|---:|---:|---:|
| Web Debug | 85,654,197 B | 539 / 69 | `0x1BD23` | `0xD5B2` | `0x1A4219A` | `0x5A3FB7` | `0x31848C0` |
| Wasmtime Headless Debug | 85,001,338 B | 538 / 69 | `0x1BA42` | `0xD5DA` | `0x19EA148` | `0x5A1207` | `0x3140756` |

本轮 portable 只增加注释，因此两份 artifact 相对 V190 总大小、全部 section、imports 和
exports 均精确零变化。headless 当前产物仍为 `out/wasmtime/debug/index.wasm`；旧时间戳的
`krkr2_wasmtime_guest.wasm` 不参与本轮 parse、section 或尺寸结论。
