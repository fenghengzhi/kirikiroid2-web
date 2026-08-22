# Motion.LayerGetter：ClassInfo 发布、注册事务、facade/adaptor 生命周期四参考复原（2026-08-17）

## 1. 范围与结论

本轮以四个 `reference/binaries/` 当前参考为唯一二进制事实源，重新纵向闭合
`Motion.LayerGetter` 的下列层次：

- 独立 `ncbClassInfo<LayerGetter>::InfoT`、动态初始化 guard 与精确 ABI；
- Motion subclass wrapper、`ncbSubClassItem::Setup`、`RegistBegin/RegistEnd` 的注册事务；
- `CreateAdaptor` 的脚本 shell 创建、class-ID lookup、dispatch 引用交接和失败边界；
- 直接脚本构造的一指针 native facade、metadata attach 与失败回收；
- `getLayerGetter` / `getLayerGetterList` 的 facade 生产、deque 遍历、Void 保位；
- `CreateEmpty`、`Invalidate`、完整/删除析构的 sticky owner 语义；
- ClassInfo 卸载、Player/node-tree 生命周期和无同步读写形成的悬空边界。

四端共同结论是：`LayerGetter` 不是保存节点快照的 value object，也不是节点 owner。
它是一个只有 `MotionNode*` 的 native facade；脚本 adaptor 在 `sticky=false` 且 attach
成功后只拥有/删除这只 facade，不拥有其指向的 node。ClassInfo 同样只保存 borrowed
name/class-object 指针，不执行 AddRef/Release。

本轮没有发现需要改变 production 行为的新差异；`ncbind.hpp` 与现有生产路径已经保留
参考实现的失败泄漏和悬空边界。因此 portable 改动仅为三处无地址的证据/ownership 注释。

## 2. 对旧 LayerGetter 报告的命名纠偏

`analysis/motionplayer_layer_getter_ncb_surface_constructor_four_binary_2026-08-14.md`
关于 29 项 property、直接构造器、live getter 和 adaptor 布局的数据结论仍有效，但其中
两个表头把模板层次压扁了：

- Android arm64 `0x6FACE8` 不是独立的 “class registration wrapper”，而是
  `ncbSubClassItem<LayerGetter>::Setup`；该目标的 Motion wrapper 被更高层内联/采用了
  不同拆分；
- 四端原标为 “class-info initialization” 的 `0x6FAE58 / 0x5B65A4 /
  0x10014D928 / 0x14F588` 实际是 `RegistBegin`。真正的 `InfoT` 静态初始化属于 init-array
  中另一组极短函数。

本报告用 `staticInit → Motion wrapper → Setup → RegistBegin/RegistEnd → member registrar`
恢复模板层次，取代旧表的层级命名；旧报告的 member surface/getter 数据不被推翻。
`analysis/motionplayer_layer_getter_lifecycle_four_binary_2026-08-12.md` 中递归查询、列表顺序
和 live/dangling facade 结论也继续成立。

## 3. 独立 ClassInfo 状态

### 3.1 精确布局

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

### 3.2 四端地址

| 目标 | InfoT | guard | init-array static init |
|---|---:|---:|---:|
| Android arm64 | `0x1AB5720` | `0x1AB5740` | `0x42F024` |
| Android armv7 | `0x1111ABC` | `0x1111ACC` | `0x30150C` |
| iOS arm64 | `0x101ADF610` | `0x101ADF630` | `0x10014FA70` |
| iOS armv7 | `0x18317A4` | `0x18317B4` | `0x151AB8` |

恢复库统一命名为：

```text
g_ncbClassInfo_LayerGetter_guess
g_ncbClassInfo_LayerGetter_guard_guess
ncbClassInfo_LayerGetter_staticInit_guess
```

四端最终 entity readback 的 InfoT/guard 大小分别为 `0x20/0x08`、`0x10/0x04`、
`0x20/0x08`、`0x10/0x04`。按 `initialized/name/id/classObject/guard` 读取的 20 个
逻辑初值全部为零。

### 3.3 静态初始化与 guard

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

这里是 vague-linkage/template static 的进程级动态初始化 guard，不是 function-local
static 的 `__cxa_guard` 协议。四端都没有 `__cxa_guard_acquire/release`、mutex、atomic、
TLS 或异常恢复；bit test 与最后的 `guard=1` 都是普通 load/store。

LayerGetter 的状态在语义上紧随 Point/Circle/Rect/Quad 成为第五个独立 ClassInfo 模板实例；
但与 V191 一样，跨链接器身份必须依靠 init、Setup、RegistBegin、constructor 和
CreateAdaptor 的 xref，不能用 Android 的物理邻接外推 iOS 源码声明顺序。

## 4. 注册调用链

### 4.1 地址映射

| 层次 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Motion subclass wrapper | 内联/不同拆分 | `0x5996B0` | `0x100125F74` | `0x1250BC` |
| `ncbSubClassItem::Setup` | `0x6FACE8` | `0x5B64E4` | `0x10014D890` | `0x14F494` |
| `RegistBegin` | `0x6FAE58` | `0x5B65A4` | `0x10014D928` | `0x14F588` |
| `RegistEnd` | Setup 内联 | `0x5B6578` | `0x10014DB1C` | `0x14F770` |
| member registrar | `0x698730` | `0x574628` | `0x1000F81AC` | `0xF4FF8` |

恢复名分别统一为：

```text
Motion_registerSubclass_LayerGetter_guess
ncbSubClassItem_LayerGetter_Setup_guess
ncbRegistSubClass_LayerGetter_RegistBegin_guess
ncbRegistSubClass_LayerGetter_RegistEnd_guess
NCB_registerMembers_LayerGetter_guess
```

Android arm64 不创建虚假的 wrapper 符号；`0x6FACE8` 只保留 Setup 身份。

### 4.2 Motion wrapper

有独立 wrapper 的三端共同执行：

1. 调用 `Setup(name, isRegist)`；
2. Setup 返回 false 时进入框架注册失败路径；
3. 注册模式下分配小型 subclass item，并把 LayerGetter class object 发布到 Motion 的
   registration context；
4. 非注册模式不分配该 item。

Motion 最终 registrar 中 LayerGetter 仍是 Point/Circle/Rect/Quad 后、Player 前的第五个
`NCB_SUBCLASS` row；本轮没有改变 11 项 subclass 顺序。

### 4.3 Setup 事务

四端共同控制流可归纳为：

```cpp
if (isRegist && ClassInfo::GetClassObject() != nullptr)
    return false;

RegistrationContext ctx(name, isRegist);
if (isRegist)
    RegistBegin(ctx);
registerLayerGetterMembers(ctx);
RegistEnd(ctx);
return !isRegist || ClassInfo::GetClassObject() != nullptr;
```

重复注册 gate 检查 `classObject`，不是 `initialized`。这两个字段没有原子一致性；
CreateAdaptor 同时读取 `classObject` 与 `id`，故并发注册/卸载可以观察混合代际状态。

### 4.4 RegistBegin 与发布顺序

共同顺序是：

1. 由 name 创建 native class，并安装 LayerGetter `CreateEmpty` adaptor factory；
2. 注册/取得 native class ID；
3. 若 `InfoT.initialized` 已真，进入 “already registered” 异常路径；
4. 发布 `name → id → classObject → initialized=true`；
5. 把 class ID 写入 native class；
6. 注册 `finalize`；
7. Setup 随后注册 typed constructor 与 29 项 getter-only property。

第 4 步保存的是 borrowed 指针：没有 AddRef，也没有 owner transfer。发布发生在
SetClassID、finalize 和 29 项 descriptor 之前；后续任一步抛出都没有自动 Clear，所以
可以留下“InfoT 已发布、class 尚未完整”的部分提交状态。

Android 保留独立 ClassInfo leaf：

| helper | Android arm64 | Android armv7 |
|---|---:|---:|
| GetName | `0x698690` | `0x5745B8` |
| GetID | `0x6986A0` | `0x5745C4` |
| GetClassObject | `0x6986B0` | `0x5745D0` |
| IsSubClass | `0x6986C0` | `0x5745DC` |
| Set | `0x6986C8` | `0x5745E0` |
| Clear | `0x698700` | `0x574608` |
| InfoT constructor | `0x69871C` | `0x57461C` |

`IsSubClass` 恒 false；LayerGetter 没有 ClassInfo parent。iOS 将这些 leaf 内联，但字段和值流一致。

### 4.5 RegistEnd / 卸载

Android armv7 与 iOS 两端的独立 RegistEnd 均为：

```cpp
if (ctx.active)
    frameworkRegistEnd(ctx.owner);
else {
    info.name = nullptr;
    info.id = 0;
    info.classObject = nullptr;
    info.initialized = false;
}
```

Android arm64 把同一语义并入 Setup。Clear 不 Release class object，guard 也不复位；下一次
Setup 仍使用已经初始化过的 InfoT 存储。全链没有 synchronization，因此卸载与
CreateAdaptor/constructor 并发属于 data race，可能用旧 class object 配零/新 ID，或用已清空
的 class ID 查询仍存活的 script shell。

## 5. 直接脚本构造的一指针 facade

### 5.1 allocate + attach 地址

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|
| `0x6E0080` | `0x5A0C4C` | `0x100131314` | `0x1301D8` |

恢复名为 `LayerGetter_ncbConstructor_allocateAttach_guess`。

共同状态机：

```cpp
LayerGetter *native = new LayerGetter; // one pointer, node = nullptr
Adaptor *adaptor = objthis->NativeInstanceSupport(GETINSTANCE,
                                                   ClassInfo::GetID());
if (adaptor != nullptr) {
    adaptor->native = native;
    return TJS_S_OK;
}
delete native;
return TJS_E_NATIVECLASSCRASH; // -1008
```

LP64 facade 为 8 B，32-bit facade 为 4 B。metadata attach 失败会回收 facade；成功却不会
给 node 赋有效地址。外层 typed constructor 的既有边界保持不变：一个且仅一个 Void 参数
是 CreateAdaptor 使用的空 shell 哨兵；普通零参数或 surplus 非负参数会建立真实的 node-null
facade。29 个 getter 均没有 null guard，所以直接脚本构造后调用任一 getter 会自然进入
null-dereference 边界。

## 6. `ncbInstanceAdaptor<LayerGetter>`

### 6.1 地址与 ABI

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| CreateAdaptor | `0x6F2B1C` | `0x5AFB24` | `0x1001452D0` | `0x145B88` |
| CreateEmpty | `0x6FAFAC` | `0x5B668C` | `0x10014DA40` | `0x14F6E4` |
| Invalidate | `0x6FAFE0` | `0x5B66B0` | `0x10014DA74` | `0x14F708` |
| complete dtor | `0x6FB018` | `0x5B66CC` | `0x10014DAAC` → `0x10014DAC4` | `0x14F722` → `0x14F736` |
| deleting dtor | `0x6FB06C` | `0x5B6704` | `0x10014DAB0` | `0x14F726` |

布局：

```text
LP64, sizeof 0x18:
  +0x00 vptr
  +0x08 LayerGetter *native
  +0x10 bool sticky

ILP32, sizeof 0x0c:
  +0x00 vptr
  +0x04 LayerGetter *native
  +0x08 bool sticky
```

CreateEmpty 把 native 和 sticky 都置零。Invalidate、完整析构与删除析构共享：

```cpp
if (native != nullptr && !sticky)
    delete native;
native = nullptr;
sticky = false;
```

删除 facade 只释放一个 pointer-sized allocation；其内 `MotionNode*` 是 borrowed address，
不会调用 MotionNode destructor，也不会 delete node。`sticky=true` 反而阻止 adaptor 删除 facade；
Player 两个生产者固定用 `sticky=false`。

### 6.2 CreateAdaptor 精确数据流

共同伪代码：

```cpp
iTJSDispatch2 *klass = ClassInfo::GetClassObject();
if (!klass) {
    if (throwOnError) throw ...;
    return nullptr;
}

iTJSDispatch2 *global = TVPGetScriptDispatch();
tTJSVariant voidArg;
iTJSDispatch2 *instance = nullptr;
tjs_error hr = klass->CreateNew(..., &instance, 1, &voidArg, global);
global->Release();

if (TJS_SUCCEEDED(hr) && instance) {
    Adaptor *adaptor = getAdaptor(instance, ClassInfo::GetID());
    if (adaptor) {
        adaptor->native = suppliedFacade;
        if (sticky) adaptor->sticky = true;
    }
    return instance; // even when adaptor == nullptr
}

if (throwOnError) throw ...;
return nullptr;
```

脚本 shell 使用一个 Void 参数，故 typed constructor 不分配第二只 node-null facade。正常
CreateNew 返回后会 Release 临时 global dispatch；返回的 instance 保持 CreateNew 引用，随后
portable builder 用 `tTJSVariant(dispatch, dispatch)` retain，再以 local `Release()` 平衡。

`throwOnError` 只覆盖 ClassInfo 缺失和 CreateNew 失败/null；metadata 不兼容时不抛出，仍返回
已经创建的 instance。

### 6.3 失败矩阵

| 边界 | script 返回/异常 | supplied facade | 临时 global dispatch |
|---|---|---|---|
| classObject 为空，error=false | null → caller 的 Void | 泄漏 | 尚未取得 |
| classObject 为空，error=true | 抛出 | 泄漏 | 尚未取得 |
| CreateNew 正常失败/null，error=false | null → Void | 泄漏 | 正常 Release |
| CreateNew 正常失败/null，error=true | 抛出 | 泄漏 | 正常 Release |
| CreateNew 自身抛出 | 异常传播 | 泄漏 | Release 语句未到达，保留引用 |
| instance 有兼容 adaptor | instance | adaptor 接管；non-sticky 时析构删除 | 正常 Release |
| instance 无兼容 adaptor | **仍返回 instance** | 泄漏 | 正常 Release |

最后一行是旧 LayerGetter 报告没有完全展开的新精度：不能把所有 metadata attach 失败都归纳成
“CreateAdaptor 返回 null”。`buildLayerGetterVariant` 的非空分支会把这个不兼容 script object
保留下来；只有真正的 null 才转成 Void。

## 7. Player 生产者与内部容器

### 7.1 地址

| 函数 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `getLayerGetter` | `0x6D0CD4` | `0x595EF4` | `0x100121D64` | `0x120B2C` |
| `getLayerGetterList` | `0x6D2368` | `0x596CD4` | `0x100122DC0` | `0x121E18` |

### 7.2 单项查询

`getLayerGetter(label)`：

1. 按 raw label 做递归 node lookup；
2. miss 直接返回 Void，不分配 facade；
3. hit 分配 pointer-sized `LayerGetter`，保存 borrowed node address；
4. 调 `CreateAdaptor(facade, sticky=false, throwOnError=false)`；
5. compatible 成功后 script adaptor 成为 facade owner；真正 null 时返回 Void 且 facade 泄漏；
6. incompatible metadata 时返回新 script object，但 facade 泄漏。

### 7.3 列表与 deque

`getLayerGetterList()` 每次创建新 TJS Array，并按 `_nodes` segmented deque 的逻辑索引
`[1, size)` 遍历：root index 0 被跳过；没有 tree DFS、label map lookup 或去重。每个 node
都创建独立 facade 并追加一个 Variant。

iOS armv7 的 fresh decompile 直接显示 deque block-map/offset 运算和 `0x8B4` node stride；
这证明列表是在平坦 node deque 上按 index 前进，不是沿 parent/child 指针遍历。四端优化形态
不同，但逻辑顺序、跳 root 和重复保留一致。

CreateAdaptor 真正返回 null 时，当前位置仍追加 Void，数组长度与 node 数量保持对应；失败
facade 泄漏。metadata 不兼容则该位置追加异常 shell object，而不是 Void。

## 8. 完整对象生命周期

```text
进程 init-array
  -> InfoT/guard 初始化为零
  -> Motion 注册 LayerGetter
     -> Setup -> RegistBegin -> ClassInfo 发布 -> 29 members -> RegistEnd

Player query/list
  -> new one-pointer facade(borrowed MotionNode*)
  -> CreateAdaptor(one Void shell)
     -> compatible adaptor.native = facade, sticky=false
     -> script Variant retain dispatch

script object Invalidate/dtor
  -> delete facade only
  -> MotionNode remains Player/node-tree owned

node-tree rebuild / Player destruction
  -> surviving script facade may dangle

Motion unload
  -> RegistEnd Clear InfoT without Release
  -> guard remains initialized
```

这条链没有 generation、weak token、Player retain、node retain 或 validity probe。facade 是 live
view：node 存活期间会观察后续字段变化；node 被替换/销毁后则可悬空。不能为了“安全”加入
shared ownership 或 null/dangling guard，否则会偏离原版边界。

## 9. 四端 recovery IDB 写回

本轮四库完成：

- 1 个 LayerGetter InfoT 与 1 个 guard/目标，共 8 个 typed data items；
- 70 个 LayerGetter 相关函数 entry 的统一语义名/最终 entity readback；
- 78 个 function/data 注释；
- 4 个 ClassInfo/adaptor ownership bookmark；
- 70 个 function force-recompile，全部成功；
- 20 个逻辑静态字段/guard 零值读取；
- 四份 recovery IDB 逐一原位保存。

Android stripped leaf 统一保留 `_guess`；iOS 已有 `InitFunc_38` 也改为跨端一致的
`ncbClassInfo_LayerGetter_staticInit_guess`。没有给下一个物理 init-array/BSS 邻居强行赋
LayerGetter 身份。

## 10. portable 对齐

本轮只更新：

- `cpp/plugins/motionplayer/main.cpp`：说明 LayerGetter 是第五个独立 delayed-subclass
  ClassInfo，及直接构造与 Player producer 的分工；
- `cpp/plugins/motionplayer/PlayerLayerQuery.cpp`：展开 Void shell、dispatch retain/Release、
  facade owner、null failure 与 incompatible-object-return 的差异；
- `cpp/plugins/motionplayer/SourceCache.h`：记录 ClassInfo non-owning、adaptor 只拥有 facade、
  直接构造 null dereference 与 node-tree replacement dangling 边界。

编译单元注释不包含四参考的绝对地址；地址仅保留在本分析映射表中。算法、布局和 ABI surface
均未改动。

## 11. 验证

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两套 motionplayer test TU syntax-only 均 exit 0；
  只有仓库既有 `_tss` deprecated warning；
- Web Debug 与 Wasmtime Headless Debug 均完成最终链接；
- Node `WebAssembly.Module` 成功 parse 两份当前 `index.wasm`；
- `llvm-objdump -h` 成功读取两份完整 section table；
- Web/Headless CTest 均 exit 0，但两个 build tree 都报告 `No tests were found`；本轮只声明
  双模式编译/链接覆盖，不虚报 runtime unit-test；
- scoped `git diff --check` exit 0；只有工作树既有 LF/CRLF 提示。

V192 artifact：

| 配置 | 总大小 | imports / exports | FUNCTION | GLOBAL | CODE | DATA | name |
|---|---:|---:|---:|---:|---:|---:|---:|
| Web Debug | 85,654,197 B | 539 / 69 | `0x1BD23` | `0xD5B2` | `0x1A4219A` | `0x5A3FB7` | `0x31848C0` |
| Wasmtime Headless Debug | 85,001,338 B | 538 / 69 | `0x1BA42` | `0xD5DA` | `0x19EA148` | `0x5A1207` | `0x3140756` |

两份产物相对 V191 的总大小、imports/exports 和全部 section 精确零变化，符合本轮仅修改
portable 注释的预期。

