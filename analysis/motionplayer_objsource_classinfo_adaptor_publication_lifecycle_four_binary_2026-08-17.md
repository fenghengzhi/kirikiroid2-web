# Motion.ObjSource：ClassInfo、注册事务与 adaptor 发布/所有权四参考复原（2026-08-17）

## 1. 范围与结论

本轮以 `reference/binaries/` 中四个当前参考为唯一二进制事实源，纵向闭合
`Motion.ObjSource` 的下列模板层和对象生命周期：

- 独立 `ncbClassInfo<ObjSource>::InfoT`、静态初始化 guard 与 ABI；
- Motion subclass wrapper、`ncbSubClassItem::Setup`、`RegistBegin/RegistEnd`；
- typed zero-argument constructor 的 native 分配、metadata attach 与失败回收；
- `ncbInstanceAdaptor<ObjSource>::CreateAdaptor` 的 script shell 创建、class-ID 查询、
  `error`/`sticky` 边界和 native 发布；
- `ResourceManager::findSource` 预构造 facade 后的成功 owner transfer，以及 null、异常、
  incompatible metadata 的泄漏边界；
- `CreateEmpty`、`finalize`、`Invalidate`、完整析构与 deleting destructor；
- ObjSource 的 `PSBRawNode` owner、raw node、lazy texture 三槽与析构顺序。

四端共同结论是：ObjSource 的 native facade 和 script shell 是两个可分离的生命周期。
`CreateAdaptor` 先创建 shell，再尝试由 ObjSource class ID 取得 adaptor metadata；只有 metadata
成功且返回非空 adaptor，才把调用者预构造的 ObjSource 写入 adaptor。默认 `error=false` 时，
metadata 类型不兼容不会撤销已经创建的 shell，而是把 shell 原样返回；调用者的 ObjSource
没有被挂接，也没有被回收。

这条边界对 ObjSource 比 LayerGetter 更重：`ResourceManager::findSource` 在调用
`CreateAdaptor` 前已经构造 `ObjSource(iconEntry)`，因此已经保有 native allocation 和一份
PSB owner retain。发布失败会同时泄漏两者。相反，脚本直接构造器的 metadata attach 失败
会执行完整 ObjSource 析构并 `operator delete`，不会走相同泄漏路径。

本轮没有发现需要改变 portable 控制流的新差异。现有实现已经保留参考的默认 non-sticky
owner transfer 和失败泄漏；代码改动仅补充三处无绝对地址的 ClassInfo/ownership 注释。

## 2. 对旧 ObjSource 报告的层级纠偏

下列既有报告的数据结论继续有效：

- `analysis/motionplayer_objsource_ncb_surface_constructor_four_binary_2026-08-14.md`：
  zero-argument constructor、五项只读 property 和 `drawLayer`；
- `analysis/motionplayer_objsource_texture_owner_lifecycle_four_binary_2026-08-14.md`：
  raw owner/node/texture 布局、lazy texture retain/release 和析构顺序。

但旧 surface 报告把部分 ncbind 模板层压扁为一个“class registration”层。本轮重新由
static-init、Setup、RegistBegin、constructor、CreateAdaptor 和 state xref 交叉确认：

- Android arm64 `0x6FB9F0` 是
  `ncbSubClassItem<ObjSource>::Setup`，不是独立 Motion wrapper；该目标把 ObjSource wrapper
  逻辑内联进 Motion 根 registrar；
- `RegistBegin` 是创建 TJS native class 并发布 ClassInfo 的事务步骤，不是真正的 InfoT
  静态零初始化；
- `CreateAdaptor` 返回的非空 dispatch 不等于 supplied native 已挂接；script shell 与
  native adaptor metadata 必须分开判断；
- adaptor 的 conditional native destroy、完整析构和 deleting destructor 是三个 ABI 入口，
  不能都压成一个“adaptor destroy”标签。

本报告用
`staticInit → Motion wrapper → Setup → RegistBegin/RegistEnd → members → constructor/CreateAdaptor`
替换旧层级命名；旧报告的 member surface、texture decoder 和 raw-node读取结论不被推翻。

## 3. 独立 ClassInfo 状态

### 3.1 ABI 布局

LP64：

```text
+0x00  bool initialized
+0x01  padding[7]
+0x08  const tjs_char *name       // borrowed
+0x10  int32 classID
+0x14  padding[4]
+0x18  iTJSDispatch2 *classObject // borrowed
sizeof InfoT = 0x20
guard        = 0x08
```

ILP32：

```text
+0x00  bool initialized
+0x01  padding[3]
+0x04  const tjs_char *name       // borrowed
+0x08  int32 classID
+0x0c  iTJSDispatch2 *classObject // borrowed
sizeof InfoT = 0x10
guard        = 0x04
```

### 3.2 四端映射

| 目标 | InfoT | guard | init-array static init |
|---|---:|---:|---:|
| Android arm64 | `0x1AB5748` | `0x1AB5768` | `0x42F054` |
| Android armv7 | `0x1111AD0` | `0x1111AE0` | `0x30153C` |
| iOS arm64 | `0x101ADF638` | `0x101ADF658` | `0x10014FAA0` |
| iOS armv7 | `0x18317B8` | `0x18317C8` | `0x151AE4` |

恢复库统一命名为：

```text
g_ncbClassInfo_ObjSource_guess
g_ncbClassInfo_ObjSource_guard_guess
ncbClassInfo_ObjSource_staticInit_guess
```

四端按 `initialized/name/id/classObject/guard` 读取的 20 个逻辑初值全部为零。InfoT 与 guard
分别按 LP64 `0x20/0x08`、ILP32 `0x10/0x04` 回读；没有把 Android 的物理邻接当作跨链接器
身份依据。

### 3.3 静态初始化

共同伪代码：

```cpp
if ((guard & 1) == 0) {
    info.initialized = false;
    info.name = nullptr;
    info.id = 0;
    info.classObject = nullptr;
    guard = 1;
}
```

这是模板静态存储的进程级零初始化 guard，不会注册 TJS class，也不取得 dispatch owner。
四端均没有 mutex、atomic、TLS、`__cxa_guard_acquire/release` 或异常回滚。ObjSource 的
InfoT 身份由 static init、Setup、RegistBegin、direct constructor 和 CreateAdaptor 的共同
xref 闭合；不能因它在 Android BSS 中位于 LayerGetter 状态之后就推导 iOS 的物理顺序。

## 4. 注册事务

### 4.1 地址映射

| 层次 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Motion subclass wrapper | 根 registrar 内联 | `0x59977C` | `0x1001260DC` | `0x125194` |
| `ncbSubClassItem::Setup` | `0x6FB9F0` | `0x5B6CDC` | `0x10014E328` | `0x150088` |
| `RegistBegin` | `0x6FBB54` | `0x5B6D9C` | `0x10014E3C0` | `0x15017C` |
| `RegistEnd` | Setup 内联 | `0x5B6D70` | `0x10014E5C4` | `0x150370` |
| dummy-constructor helper | 内联/不同拆分 | `0x5B6F18` | `0x10014E614` | `0x15041C` |
| member registrar | `0x69A098` | `0x575028` | `0x1000F8D30` | `0xF5C48` |

恢复名：

```text
Motion_registerSubclass_ObjSource_guess
ncbSubClassItem_ObjSource_Setup_guess
ncbRegistSubClass_ObjSource_RegistBegin_guess
ncbRegistSubClass_ObjSource_RegistEnd_guess
ncbRegistSubClass_ObjSource_ensureDummyConstructor_guess
NCB_registerMembers_ObjSource_guess
```

Android arm64 不制造不存在的 wrapper 函数名；恢复库只在 Motion 根 registrar 的内联 call
site 写入说明，并保留 `0x6FB9F0` 的 Setup 身份。

### 4.2 Motion wrapper 与 Setup

有独立 wrapper 的三端共同执行：

1. 调用 `Setup(name, isRegist)`；
2. Setup false 时进入框架注册失败路径；
3. register 模式分配一个小型 subclass item，并发布到 Motion registration context；
4. unregister 模式不分配该 item。

Setup 共同控制流：

```cpp
if (isRegist && info.classObject != nullptr)
    return false;

RegistrationContext ctx(name, isRegist);
if (isRegist)
    RegistBegin(ctx);
registerObjSourceMembers(ctx);
RegistEnd(ctx);
return !isRegist || info.classObject != nullptr;
```

重复注册 gate 检查 `classObject`，不是 `initialized`。ObjSource 仍是 Motion 的第八个
in-flow subclass row，位于 SourceCache 后、ResourceManager 前；本轮没有改变 11 项 row 顺序。

### 4.3 RegistBegin 发布顺序

共同顺序：

1. 分配 TJS native class：LP64 编译形态为 `0xB0` B，ILP32 为 `0x70` B；
2. 安装 ObjSource `CreateEmpty` adaptor factory；
3. 获取 class ID；
4. `info.initialized` 已真时进入 already-registered 异常；
5. 按 `name → id → classObject → initialized=true` 发布 InfoT；
6. 把 class ID 写入 native class；
7. 注册 `finalize`；
8. Setup 再注册 typed constructor、五项只读 property 和 `drawLayer`。

InfoT 的 name/classObject 都是 borrowed；Set/Clear 没有 AddRef/Release。发布先于
SetClassID、finalize 和成员注册，因此后续步骤抛出时没有事务回滚，可留下“InfoT 已发布但
class 未完全注册”的中间状态。

Android 未内联的 ClassInfo leaf：

| helper | Android arm64 | Android armv7 |
|---|---:|---:|
| GetName | `0x699FF8` | `0x574FB8` |
| GetID | `0x69A008` | `0x574FC4` |
| GetClassObject | `0x69A018` | `0x574FD0` |
| IsSubClass | `0x69A028` | `0x574FDC` |
| Set | `0x69A030` | `0x574FE0` |
| Clear | `0x69A068` | `0x575008` |
| InfoT constructor | `0x69A084` | `0x57501C` |

`IsSubClass` 恒 false；ObjSource 没有 ClassInfo parent。iOS 将这些小 leaf 内联，但字段和值流
一致。

### 4.4 RegistEnd 与卸载

共同语义：

```cpp
if (ctx.active) {
    if (!ctx.hasConstructor)
        registerDummyConstructor(ctx);
} else {
    info.name = nullptr;
    info.id = 0;
    info.classObject = nullptr;
    info.initialized = false;
}
```

ObjSource 已注册真实 zero-argument constructor，正常 active 路径不会用 dummy 覆盖它。
unregister 清空 InfoT 但不 Release class object，也不复位 static-init guard。全链没有同步；
卸载与 CreateAdaptor/direct constructor 并发会形成普通 data race，并可能读取混合代际的
classObject 和 class ID。

## 5. native ObjSource 布局与直接构造

### 5.1 facade ABI

LP64：

```text
+0x00  PSBRawOwner *sourceOwner // retained by PSBRawNode
+0x08  RawNode *sourceNode      // borrowed within owner storage
+0x10  iTVPTexture2D *texture   // retained lazy texture
sizeof ObjSource = 0x18
```

ILP32：

```text
+0x00  PSBRawOwner *sourceOwner
+0x04  RawNode *sourceNode
+0x08  iTVPTexture2D *texture
sizeof ObjSource = 0x0c
```

成员析构顺序是：ObjSource 显式析构体先 Release `texture`，随后 `PSBRawNode` 成员析构
Release `sourceOwner`。raw node 不单独拥有引用；它依赖 owner 存活。

### 5.2 direct constructor 地址与状态机

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|
| `0x6E12DC` | `0x5A1E3C` | `0x10013291C` | `0x131974` |

恢复名为 `ObjSource_ncbConstructor_allocateAttach_guess`。

共同伪代码：

```cpp
ObjSource *native = operator new(sizeof(ObjSource));
native->sourceOwner = nullptr;
native->sourceNode = nullptr;
native->texture = nullptr;

Adaptor *adaptor = objthis->NativeInstanceSupport(GETINSTANCE, info.id);
if (adaptor != nullptr) {
    adaptor->native = native;
    return TJS_S_OK;
}

native->~ObjSource();
operator delete(native);
return TJS_E_NATIVECLASSCRASH; // -1008
```

这里与 producer publication 不同：metadata attach 失败一定执行 ObjSource 析构并释放存储。
attach 成功后的对象仍持有 null source 三槽；property/drawLayer 的 raw-node 边界继续由旧 surface
报告记录，本轮没有添加一个参考中不存在的默认 source。

## 6. `CreateAdaptor`：script shell 与 native 发布分离

### 6.1 地址映射

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| CreateAdaptor | `0x6E9504` | `0x5A7A04` | `0x10013A190` | `0x13A274` |
| GetAdaptor helper | 内联 | `0x5A1F14` | `0x1001329E4` | `0x131AA0` |

恢复名：

```text
ncbInstanceAdaptor_ObjSource_CreateAdaptor_guess
ncbInstanceAdaptor_ObjSource_GetAdaptor_guess
```

### 6.2 共同状态机

归纳后的控制流：

```cpp
iTJSDispatch2 *klass = info.classObject;
if (!klass) {
    if (error) throwNoClass();
    return nullptr;
}

iTJSDispatch2 *shell = nullptr;
tjs_error hr = klass->CreateNew(/* one Void sentinel */, &shell, tempGlobal);
tempGlobal->Release(); // 仅普通返回后显式执行
if (TJS_FAILED(hr) || !shell) {
    if (error) throwCantCreateInstance();
    return nullptr;
}

Adaptor *adaptor = GetAdaptor(shell, error);
if (adaptor) {
    adaptor->native = suppliedNative;
    if (sticky == true)
        adaptor->sticky = true;
}
return shell;
```

`GetAdaptor` 使用 ObjSource class ID 调用 `NativeInstanceSupport(GETINSTANCE)`：

- instance 为 null：`error=true` 抛 `No instance.`，否则返回 null；
- support 返回负值：`error=true` 抛 `Invalid instance type.`，否则返回 null；
- support 成功：直接返回 out slot，即使 out slot 自身仍为 null。

因此必须区分两种“metadata 不可用”：

- negative type + `error=true` 会抛出，不返回 shell；
- negative type + `error=false`，或 support 成功但 out slot 为 null，会保留并返回 shell，
  supplied native 仍未挂接。

四端都没有在 `CreateAdaptor` 内回收 supplied native。普通 CreateNew 返回后会显式 Release
临时 global/this dispatch；在该显式 Release 前发生的异常没有本地 RAII owner。错误路径也
没有把已经发布的 ClassInfo 回滚。

### 6.3 失败矩阵

| 阶段 | `error=false` | `error=true` | supplied ObjSource |
|---|---|---|---|
| classObject 缺失 | null | 抛错 | 不回收 |
| CreateNew 失败/null | null | `Can't create instance` | 不回收 |
| shell 类型不兼容 | 返回 shell | `Invalid instance type` | 不回收 |
| support 成功但 adaptor slot null | 返回 shell | 返回 shell | 不回收 |
| compatible adaptor | 返回 shell | 返回 shell | 写入 adaptor |

`sticky` 只在 compatible adaptor 非空时写入；默认 `sticky=false`。它不是对失败路径的 guard，
也不会在 shell 创建后替调用者托管尚未挂接的 native。

## 7. `ResourceManager::findSource` 的 owner transfer 与泄漏

四端 `src/group/icon` 命中后的共同 producer 顺序：

1. 从 module raw root 导航到 `source[group].icon[icon]`；
2. 构造 `ObjSource(iconEntry)`；
3. PSBRawNode copy 在这一步已经 retain owner；texture 仍为 null；
4. 调用默认 `CreateAdaptor(src)`，即 `sticky=false, error=false`；
5. compatible adaptor 时 native pointer 写入 adaptor，script dispatch 交给 Variant；
6. script Invalidate/析构时 adaptor 销毁 ObjSource，先释放 texture，再释放 PSB owner。

失败时没有 producer-side `delete src`：

- class 缺失或 CreateNew 失败：返回 Void，泄漏 ObjSource allocation 和 PSB owner retain；
- incompatible metadata：`CreateAdaptor` 返回非空 script shell，`findSource` 把该 shell 返回给
  脚本，同时泄漏相同 native state；
- CreateNew/metadata 异常：没有 native owner guard，unwind 同样不回收 preconstructed ObjSource。

这不是 portable 代码应“顺手修复”的普通资源管理疏漏，而是四参考一致的可观察边界。
现有 `ResourceManager.cpp` 保留 raw allocation、默认 CreateAdaptor 与 null 分支不回收行为；
本轮只把“retained PSB owner 也随 facade 泄漏”写清。

## 8. adaptor ABI 与销毁入口

### 8.1 adaptor 布局

LP64：

```text
+0x00  vptr
+0x08  ObjSource *native
+0x10  bool sticky
sizeof adaptor = 0x18
```

ILP32：

```text
+0x00  vptr
+0x04  ObjSource *native
+0x08  bool sticky
sizeof adaptor = 0x0c
```

`CreateEmpty` 总是构造 `native=null, sticky=false`。`finalize` native method 是返回成功的空
stub，不改变 native owner。

### 8.2 四端入口

| 入口 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| CreateEmpty | `0x6FBCA8` | `0x5B6E84` | `0x10014E4D8` | `0x1502D8` |
| finalize stub | `0x6FBCD4` | `0x5B6EA4` | `0x10014E504` | `0x1502F8` |
| Invalidate/thunk | `0x6FBCDC` | `0x5B6EA8` | `0x10014E50C` | `0x1502FC` |
| complete destructor | `0x6FBD70` | `0x5B6EAC` | `0x10014E510` | `0x150300` |
| deleting destructor | `0x6FBDD0` | `0x5B6ED4` | `0x10014E554` | `0x15032C` |
| shared native destroy | 各入口内联 | `0x5B6EF8` | `0x10014E588` | `0x150350` |

Android armv7 的 IDA 自动分析把 `0x5B6EA8` thunk、两个 destructor entry 和
`0x5B6EF8` shared body 合并为一个带多个入口的 Thumb function。恢复库没有虚构错误的
独立函数边界，而把函数名标为 `Invalidate_thunk_cluster_guess`，并在 cluster 注释中记录
四个 ABI 入口。其他三端按 IDA 已确认的函数边界分别命名。

### 8.3 conditional native destroy

共同伪代码：

```cpp
if (adaptor->native != nullptr && !adaptor->sticky) {
    adaptor->native->~ObjSource();
    operator delete(adaptor->native);
}
adaptor->native = nullptr;
adaptor->sticky = false;
```

性质：

- `Invalidate` 对 non-sticky native 拥有并删除 ObjSource；
- ObjSource 析构先 Release lazy texture，再由 PSBRawNode Release owner；
- sticky native 不被析构，但 adaptor 槽仍清零，因此 native owner 留给外部；
- 重复 Invalidate 因 native 已清零而幂等；
- complete destructor 先应用同一 conditional destroy，再进入 base adaptor 析构；
- deleting destructor 再释放 adaptor 本身。

## 9. 并发、部分提交与边界行为

本纵切面没有任何本地同步：

- static guard、InfoT Set/Clear 和 CreateAdaptor reads 都是普通内存访问；
- Setup 先看 classObject，RegistBegin 再看 initialized，两者不是原子事务；
- InfoT publication 先于 finalize/member registration，异常不回滚；
- unregister 清空 borrowed 字段但不等待已创建 shell/adaptor；
- CreateAdaptor 分别读取 classObject 和 class ID，可在并发卸载/重注册时观察混合状态；
- adaptor sticky/native 写入与 Invalidate 也没有锁。

所以并发注册、卸载、CreateAdaptor 或 Invalidate 属于 native data race；portable 不应通过
mutex、atomic、AddRef/Release、unique_ptr guard 或自动 rollback 悄悄改变参考语义。

## 10. recovery IDB 写回

四库统一写回：

- 8 个 typed data items：4 组 InfoT + guard；
- 70 个 function entry semantic names，并全部 lookup readback；
- 80 个成功写入的 data/function/ABI-cluster comments；
- 4 个 ObjSource ClassInfo/lifecycle bookmarks；
- 70 个 targeted force-recompile，全数成功；
- 20 个 logical zero-field reads；
- 四个 recovery IDB 均原位保存。

主要恢复名使用 `_guess`，因为原始 C++ 符号已剥离；`ncbClassInfo`、`ncbSubClassItem`、
`ncbRegistSubClass` 和 `ncbInstanceAdaptor` 的层级来自四端共同模板形态，不冒充原始符号。

## 11. portable 对齐

本轮仅修改注释：

- `cpp/plugins/motionplayer/main.cpp`：ObjSource 有独立 delayed ClassInfo tuple，Setup
  publish/unregister clear；
- `cpp/plugins/motionplayer/SourceCache.h`：compatible non-sticky adaptor ownership、
  failed/incompatible publication leak，以及 direct constructor 的失败回收差异；
- `cpp/plugins/motionplayer/ResourceManager.cpp`：泄漏不仅包括 facade allocation，也包括
  已 retain 的 PSB owner。

没有加入自动 delete、RAII rollback、ClassInfo AddRef/Release、同步或错误 shell 过滤；这些都会
修正四参考共同存在的边界，而不是一比一复原。

## 12. 验证

验证结果：

- ordinary motionplayer syntax-only：通过；
- `KRKR2_WASMTIME_HEADLESS=1` syntax-only：通过；
- `cmake --build out/web/debug`：通过；
- `cmake --build out/wasmtime/debug`：通过；
- Node `WebAssembly.Module` parse：两个产物均通过；
- llvm-objdump section parse：两个产物均通过；
- Web/Headless CTest：命令成功，当前两个 build tree 都报告 `No tests were found`；
- scoped `git diff --check`：通过，仅有工作树既有 LF/CRLF 提示。

产物与 V192 精确零变化：

| 指标 | Web | Headless |
|---|---:|---:|
| file size | `85,654,197` B | `85,001,338` B |
| imports | `539` | `538` |
| exports | `69` | `69` |
| FUNCTION | `0x1BD23` | `0x1BA42` |
| GLOBAL | `0xD5B2` | `0xD5DA` |
| CODE | `0x1A4219A` | `0x19EA148` |
| DATA | `0x5A3FB7` | `0x5A1207` |
| name | `0x31848C0` | `0x3140756` |

这与本轮仅修改注释的预期一致。编译输出保留既有 `_tss`、`nodiscard`、pthread memory-growth、
JSPI 和 JS library warning，没有出现本轮新增错误或警告类型。

## 13. 置信度与未闭合项

高置信度：

- InfoT/guard 地址、布局、静态初值与 Set/Clear 非 owning；
- wrapper/Setup/RegistBegin/RegistEnd 层级；
- direct constructor attach failure 完整回收；
- CreateAdaptor 的 shell-first、default-error=false 和 native 不回收边界；
- incompatible metadata 返回 shell、producer retained-owner leak；
- adaptor 布局、sticky 条件、texture-before-owner 析构顺序。

保留 `_guess`：

- stripped 模板 helper 的原始源码拼写；
- Android armv7 自动分析合并的 Thumb destructor cluster 的原始编译器符号划分；
- temporary CreateNew receiver/global dispatch 在原始 ncbind 源码中的变量名。

这些命名不确定性不影响已闭合的字段、控制流、owner transfer 或边界行为。
