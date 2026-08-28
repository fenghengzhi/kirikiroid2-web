# motionplayer AddRef/Release、native instance、borrowed pointer 与删除 thunk 总审计（四参考二进制，2026-08-27）

## 1. 范围与结论

本报告逐项闭合两个原始任务：

- `MP-L16`：AddRef/Release、TJS native instance、borrowed pointer 与 deleting destructor 总审计；
- `MP-B10`：构造失败、析构重入、double release、zero-ref 和删除 thunk。

这里的“总审计”不是把所有指针都归纳成同一种 owner。四个参考二进制共同使用至少五套
互不等价的生命周期协议：

1. `iTJSDispatch2` / `tTJSVariant` 的非原子引用计数；
2. `tTJSCustomObject` 固定四槽 native-instance 容器和 `ncbInstanceAdaptor<T>`；
3. `new/delete`、普通析构和 virtual deleting destructor 组成的 raw/single owner；
4. texture、bitmap、PSB raw node 等接口自己的 intrusive AddRef/Release；
5. `MotionNode`、D3D parent/listener/view、Array native Items 等不 retain 的 borrowed pointer。

把其中任意一类统一改成 `shared_ptr`、`unique_ptr`、weak handle 或 scope guard，都会改变构造
失败、重入、重复注册、双 owner、异常泄漏或删除顺序。当前本地实现已经保留四端共同语义，
本轮没有 semantic C++ edit。

本轮 fresh 请求 76 个四端入口，去除 Android armv7 三个共享 entry 落在同一 function range
的重复后，共 74 个独立函数范围、3323 条独立指令。全部 fresh decompile 成功，完整
disassembly 均未截断，并读取 xref。另对 Android/iOS armv7 的 AddRef/Release Thumb vtable
pointer 做了 raw-byte 全量搜索，用来补足 IDA 不识别低位 Thumb pointer 时缺失的 data xref。

## 2. fresh 四端证据

### 2.1 TJS 引用计数根

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `tTJSDispatch::AddRef` | `0x9F5660`；5 | `0x7533E0`；5 | `0x10005782C`；5 | `0x564C8`；5 |
| `tTJSDispatch::Release` | `0x9F5674`；29 | `0x7533EA`；26 | `0x100057840`；29 | `0x564D2`；26 |
| Array self-closure owner helper | `0x702098`；63 | `0x5BAA70`；60 | `0x10029FF58`；49 | `0x2A4A80`；91 |

四端 IDB 已把前两行确定性命名为 `tTJSDispatch_AddRef` 和
`tTJSDispatch_Release`。LP64 的 `BeforeDestruction` / deleting-destructor vcall offset分别为
`+232/+248`，ILP32 为 `+116/+124`；这是指针宽度带来的 ABI 差异，不是控制流差异。

IDA 的完整 xref计数为：Android arm64 `AddRef=593/Release=563`，iOS arm64
`573/562`。Android armv7 对 Thumb function pointer 的普通 xref 不完整；直接搜索低位已置 1
的 vtable pointer，`AddRef` 和 `Release` 各有 563 个 occurrence。iOS armv7 同法各有 562 个。

Array helper 是 motionplayer 中反复使用的 closure owner 原语。四端共同顺序是：

```text
array = TJSCreateArrayObject()                 // factory ref = 1
array.AddRef()                                 // closure.Object
array.AddRef()                                 // closure.ObjThis，允许与 Object 相同
local = Variant(Object=array, ObjThis=array)
array.Release()                                // 平衡 factory ref
status = array.NativeInstanceSupport(GETINSTANCE, ArrayClassID, &native)
result = copy(local)                           // 再持有完整 closure
result.Items = status == TJS_S_OK ? &native.Items : null
destroy local                                 // Object、ObjThis 各 Release 一次
```

### 2.2 native adaptor、borrowed view 与删除入口

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Player adaptor Invalidate | `0x6FB3DC`；17 | `0x5B6948`；共享 41 | `0x10014DDE8`；1 | `0x14FAF4`；1 |
| Player adaptor complete dtor | `0x6FB420`；22 | `0x5B694C`；同一范围 | `0x10014DDEC`；15 | `0x14FAF8`；15 |
| Player adaptor deleting dtor | `0x6FB480`；19 | `0x5B6974`；同一范围 | `0x10014DE30`；12 | `0x14FB24`；12 |
| `D3DLayerBase` GET/replace/REGISTER | `0x5322AC`；73 | `0x495F90`；85 | `0x100234964`；61 | `0x2336CA`；70 |
| root ctor + sticky base publication | `0x531274`；102 | `0x495618`；86 | `0x100233C88`；76 | `0x23295C`；132 |
| borrowed `D3DLayerObject` REGISTER | `0x5333F0`；109 | `0x496990`；52 | `0x1002355B4`；37 | `0x2342B4`；80 |
| borrowed view consumer `add` | `0x52B82C`；33 | `0x492CA8`；35 | `0x100230D58`；25 | `0x22FC5E`；22 |
| borrowed view consumer `remove` | `0x52B8B0`；50 | `0x492D00`；35 | `0x100230DBC`；25 | `0x22FC92`；22 |

### 2.3 producer failure、raw/intrusive owner 与 virtual object

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| LayerGetter `CreateAdaptor` | `0x6F2B1C`；92 | `0x5AFB24`；82 | `0x1001452D0`；65 | `0x145B88`；115 |
| ObjSource `CreateAdaptor` | `0x6E9504`；92 | `0x5A7A04`；82 | `0x10013A190`；65 | `0x13A274`；115 |
| D3DAdaptor ctor | `0x6AAEF0`；48 | `0x57D0AC`；44 | `0x100103FA8`；46 | `0x10128C`；90 |
| D3DAdaptor dtor | `0x6AAFCC`；35 | `0x57D12E`；22 | `0x1001040A0`；31 | `0x1013BC`；71 |
| D3DImage complete dtor | `0x5336F4`；66 | `0x496F60`；32 | `0x100235CFC`；20 | `0x234AD8`；51 |
| D3DEmotePlayer complete dtor | `0x533FE0`；36 | `0x497870`；12 | `0x100236374`；12 | `0x235076`；12 |
| D3DEmotePlayer deleting dtor | `0x534078`；9 | `0x497894`；13 | `0x1002363A8`；13 | `0x23509A`；13 |
| D3DEmotePlayer `getModule` | `0x52FF78`；61 | `0x494864`；35 | `0x100232B68`；43 | `0x2317C0`；42 |

最后一行在四个 IDB 中已确定性命名为 `D3DEmotePlayer_getModule`。它不是返回 Variant 的
D3D root stub，而是返回 `D3DEmoteModule *` 的 property native target。

## 3. `tTJSDispatch` 的 zero-ref 和析构重入状态机

四端共同源码不能简化成普通的 `if (--ref == 0) delete`：

```text
AddRef(self):
    return ++self.RefCount

Release(self):
    if self.RefCount == 1:
        if !self.BeforeDestructionCalled:
            self.BeforeDestructionCalled = true
            self.BeforeDestruction()              // 此时 RefCount 仍为 1
        if self.RefCount == 1:
            virtual deleting_destructor(self)
            return 0
    return --self.RefCount
```

引用数和两个状态位均为普通 load/store，没有 atomic、mutex、饱和或 underflow guard。边界为：

| 初始状态 / 回调动作 | 共同结果 |
|---|---|
| `RefCount > 1` | 直接减一；不调用 finalize |
| `RefCount == 1`，回调不改 ref | deleting destructor；返回 0；不会先把字段写成 0 |
| 回调 `AddRef` 一次 | 回调后 count 为 2，外层再减为 1；对象复活，销毁前回调不再运行 |
| 回调 `AddRef` 多次 | 外层只减一次；保留其余新 owner |
| 回调不先 AddRef、直接重入 `Release` | nested Release 看到 `BeforeDestructionCalled=true/count=1`，立即删除；outer callback/Release 随后继续访问已释放对象，形成原版 UAF/double-delete 边界 |
| `BeforeDestruction` 抛异常 | 标志已经置 true、count仍为1，异常直接逃出；之后再次 Release 会跳过回调并删除，不重试 finalize |
| 对仍存活但 count 已是 0 的损坏对象调用 Release | 无 zero guard，unsigned count 下溢；对已经删除的对象调用则本身就是 UAF |
| 多线程 AddRef/Release | plain read-modify-write data race；不是线程安全 refcount |

`tTJSCustomObject::_Finalize` 只用 `IsInvalidating` 吸收递归 invalidation。virtual `Finalize` 和
native adaptor 的 Invalidate 执行期间，`IsInvalidated` 仍是 false，所以 ordinary TJS/native
method reentry 仍可发生。异常路径只清 `IsInvalidating`；上层已经置位的
`BeforeDestructionCalled` 不回退。

## 4. Variant closure 的双引用，不是“一对象一引用”

Object Variant 同时保存 `Object` 和 `ObjThis` 两个独立引用。即使两者指向同一 dispatch，
构造/copy也各 AddRef 一次，Clear 也各 Release 一次：

```text
copy closure:
    AddRef(new.Object)
    AddRef(new.ObjThis)
    Release(old content)
    publish both pointers and tvtObject

clear closure:
    oldType = type
    type = Void                         // release 回调前先消除本 Variant 的重入 owner
    Release(Object)
    Release(ObjThis)
```

因此正常 self-closure 的两次 Release 并不是 double-release bug；它们平衡两个真实引用。
`CopyRef` 对 Object 先持有新 closure、再释放旧内容，并特判 `this == &ref`，防止 alias
assignment 过早销毁。`AsObject()` 返回一个新增引用，`AsObjectNoAddRef()` 只借用；
`tTJSVariantClosure` 本身不是 RAII owner，只有显式 `AddRef/Release` helper。

如果外部手工伪造一个未执行两次 AddRef 的 self-closure，再让 Variant Clear，第二次 Release
会落到已删除对象；参考实现没有检测这种不变量破坏。

`ncbPropAccessor` 的三种来源也不能混淆：raw dispatch默认 AddRef；Array/Dictionary factory
通过 `addref=false` 接管 factory owner；Variant constructor使用 `AsObject()`取得新 owner。
accessor析构只 Release `_obj` 一次，内部每个临时 Variant再独立释放其内容。

## 5. native-instance 四槽与 adaptor 状态机

`tTJSCustomObject` 的 native container 固定为四槽：

- `GETINSTANCE` 从 slot 0 向上扫描，返回第一个/最老的相同 class ID；
- `REGISTER` 写第一个 classID为 `-1` 的空槽，不查重、不替换相同 ID；
- 四槽已满时返回失败，原 slots 和 caller的 adaptor pointer均不变；
- Finalize/Invalidate 从 slot 3 向 slot 0 调 `Invalidate`；
- custom-object destructor 同样从 slot 3 向 slot 0 调 `Destruct`；
- native Invalidate 完成后才删除 script members，因此 native destructor callback仍可看到成员。

通用 `ncbInstanceAdaptor<T>` 是 `{vptr, native*, sticky}`：

```text
DeleteInstance:
    if native != null and !sticky:
        ordinary/native destructor + scalar delete native
    native = null
    sticky = false

Invalidate:       DeleteInstance
complete dtor:    DeleteInstance + base-vptr restoration
deleting dtor:    complete destruction + scalar delete adaptor shell
```

native pointer在 pointee destructor和scalar delete全部结束后才清零。析构回调中的普通
`GetNativeInstance` 因而仍可发现旧地址；外层 custom-object guard只吸收另一次 Invalidate，
不屏蔽 ordinary method reentry。

`SetNativeInstance` 只覆盖 slot，不删除旧 native。typed/raw constructor descriptor在同一 receiver
上重入时，会泄漏旧 payload；D3DLayer因此可出现“concrete adaptor看最新 generation，borrowed
class-ID view仍看最老 generation”的分裂。

`SetAdaptorWithNativeInstance` / D3DLayerBase特化的边界是：

- 已有且 live adaptor：仅 non-sticky旧 payload被删除，随后清 slot/sticky；
- 已有但 native==null：旧 sticky bit故意保留；
- 无 adaptor：先 raw `new` 一个空 adaptor；
- 新 native先写入 adaptor，再执行 REGISTER；
- REGISTER失败且不抛时，fresh/reused adaptor与新 native保持部分发布并泄漏；抛出路径也没有
  自动回收它们。

D3D root的concrete adaptor是non-sticky唯一 owner；独立 `D3DLayerBase` adaptor随后置sticky，
只提供borrowed base view。`D3DLayerObjectNativeInstance`更尖锐：Invalidate是no-op，deleting
destructor只释放view shell，永远不读、清空或删除borrowed payload。重复REGISTER会追加
generation；满槽失败被忽略并泄漏fresh view；具体layer析构后旧view可以永久悬空。

## 6. CreateAdaptor、constructor/factory 与失败 publication

### 6.1 `CreateAdaptor(native, sticky, err)`

四端 LayerGetter/ObjSource 特化共同证明：

1. class object缺失：返回 null或抛；输入native仍是无 owner raw pointer；
2. 取得owning global dispatch，构造exact-one-Void参数；
3. `CreateNew`正常返回后才Release global；若CreateNew抛，global ref和输入native都泄漏；
4. status失败或result null返回null；若result被非标准地写成non-null，也没有补Release；
5. 创建shell成功但`GetAdaptor`失败：仍返回non-null shell；输入native未附着并泄漏；
6. compatible adaptor才写native，按需置sticky；
7. 输入native在成功附着前没有临时RAII owner。

因此 LayerGetter facade、geometry shape、ObjSource facade 和 Player child producer 的普通/异常
失败泄漏是共同模板边界，不应局部“修安全”。

### 6.2 generated typed constructor/factory

typed wrapper先建立pending native，`SetNativeInstance`失败时delete pending native并返回
`TJS_E_NATIVECLASSCRASH`；异常路径也delete仍在local中的pending pointer。exact-one-Void在
分配前返回成功empty adaptor。需要保留两个细节：

- `SetNativeInstance`成功后local pointer没有清零，但正常路径不会再delete；所有权已经转入
  adaptor；
- 若失败分支里的native destructor本身抛出，外围catch会再次执行 `delete inst`，这是潜在
  double-delete/terminate边界，参考没有catch阶段标志。

raw `ncbNativeClassFactory` 没有typed wrapper的catch：factory返回非零status时，即使它已经把
non-null pointer写入out，也不delete；factory抛出且已在内部完成allocation时也由factory自己
负责。只有exact `TJS_S_OK` 后attach失败才delete返回的native。

### 6.3 raw aggregate constructor和clone

- EmoteObject在 `_rm` / `_engine` 发布后发生后续异常，不运行完整aggregate destructor，已发布
  raw owners泄漏；
- EmoteObject clone在完整copy建立后serialize/unserialize抛出，copy泄漏；
- D3DEmotePlayer clone先完成listener注册，再进入inner clone；inner失败使完整shell和已注册
  listener一起泄漏；
- D3DImage constructor在managed set insert前由new-expression管理pending storage，insert成功后
  才由outer adaptor接管；attach失败会delete image并erase刚发布的set node；
- LayerGetter/ObjSource直接把raw pointer交给CreateAdaptor，无相同的pending cleanup。

## 7. pointer boxing 与真实 double owner

NCBind native-object boxing按C++返回类型选择owner：

| 返回类型 | native payload | adaptor sticky | 语义 |
|---|---|---:|---|
| `T` | `new T(copy)` | false | adaptor owns copy |
| `T&` / `const T&` | 原地址 | true | adaptor borrows |
| `T*` / `const T*` | 原地址 | false | adaptor owns pointer |

boxing随后构造Object+ObjThis Variant并Release factory adaptor ref。CreateAdaptor返回null时没有
null guard，后续Variant构造/Release也是原版崩溃边界。

`D3DEmotePlayer::getModule()` 是 motionplayer闭包里的真实双owner特例：

```text
root.Modules[classId] ---------------- raw owning map ----------------> module
script result -> non-sticky ncbInstanceAdaptor<D3DEmoteModule> ------> same module
```

四端 `getModule` 都先按class ID查root owning map；miss时 `new D3DEmoteModule`，再写map，
最后以raw pointer返回。pointer boxing使用sticky=false，因此：

- wrapper先释放：adaptor删除module，map保留non-null dangling value；root析构再次删除；
- root先释放：map删除module，wrapper保留dangling native；wrapper析构再次删除；
- module没有map back-pointer，任何一边都不会主动摘除另一边；
- map同key覆盖本身也不delete旧value，形成另一条replacement leak。

这是四端共同原版bug。本地注释明确保留它，不能把返回类型改成reference、sticky adaptor、
shared owner或map-aware wrapper。

## 8. raw、intrusive 和 borrowed owner 总表

| 对象/边 | 协议 | 发布、销毁与边界 |
|---|---|---|
| script dispatch / Object Variant | refcount owner | Object/ObjThis各一份；Release可复活、重入删除、异常poison；非线程安全 |
| `ncbInstanceAdaptor<T> -> T` | conditional raw owner | non-sticky delete，sticky borrow；native析构后才清slot |
| EmoteObject -> RM/Engine | raw single owner | Engine先删、RM后删；构造后半异常泄漏；析构中字段不预清零 |
| EmoteEngine -> Player/controllers | single-pointer owner | prefix构造失败逆序清理；normal reverse teardown；Player非多态、无自身deleting dtor |
| D3DEmotePlayer -> primary/secondary | raw single owner | secondary先删、primary后删，两个delete完成后才成对清零；listener最后注销 |
| D3D root -> Modules | raw owning map | key顺序virtual delete；overwrite不delete旧value；getModule再产生第二owner |
| D3D root Front/Back/Managed | borrowed pointer trees | tree只own node；不delete layer/image；owner-first留下悬空反向指针 |
| D3DLayerListener -> layer | borrowed subscription | ctor注册；dtor通过live owner移除所有匹配；layer不为listener清owner |
| D3DImage -> root | borrowed | dtor先Release/delete texture holder，再erase root set；root先死则悬空 |
| D3DPicture -> image/layer | borrowed | Draw时解引用；dtor只由listener base摘链，不retain两个pointee |
| D3DAdaptor -> Window | manual raw ref | ctor AddRef，dtor Release；constructor中后段失败保留已取得Window ref的原版边界 |
| D3DAdaptor target / texture map values | intrusive owner | holder/map valueRelease；map key只借用source identity |
| mesh submit source | manual intrusive ref | 入口AddRef，普通false/成功路径显式Release；异常路径故意不补Release |
| ObjSource PSB raw node / lazy texture | raw/intrusive owners | adaptor成功后由ObjSource dtor释放；CreateAdaptor失败前无owner而泄漏 |
| LayerGetter facade -> MotionNode | borrowed | adaptor只own facade；无Player ref、index、generation或失效token |
| Array helper `Items*` | borrowed interior pointer | 只由外层Array Variant维持native活性；GET失败时null，consumer常无guard |

## 9. deleting destructor 分层

“删除 thunk”必须按层级区分：

1. `Player`、EmoteObject、EmoteEngine和多数controller payload非多态；owner site直接调用
   ordinary destructor再scalar delete，没有payload deleting-destructor pair。
2. `ncbInstanceAdaptor<Player>`等shell有complete/deleting destructor；前者可删除non-sticky
   payload但不释放shell storage，后者再释放shell。二者不是Player的虚析构。
3. `D3DLayerBaseNativeInstance` deleting destructor删除adaptor shell；其non-sticky Instance通过
   pointee自己的virtual deleting destructor删除。
4. borrowed `D3DLayerObjectNativeInstance` deleting destructor只释放view storage，绝不触碰
   borrowed pointer。
5. D3DEmotePlayer complete destructor先清两个raw EmoteObject owners、再注销listener；deleting
   destructor最后scalar delete shell。
6. D3DImage complete destructor先清texture holder、再从borrowed root set erase；deleting入口
   才释放image allocation。
7. `tTJSDispatch::Release` 在最终确认count仍为1后调用virtual deleting destructor，而不是
   手工普通析构；回调期间count保持1是复活和nested-release边界的根因。

destructor body抛异常时，C++ destructor/noexcept和ABI cleanup最终可能terminate；参考没有事务
rollback、继续删除剩余owners或把所有slot预先清空的业务层补偿。

## 10. 本地逐项对照

| 参考语义 | 本地实现 | 结果 |
|---|---|---|
| AddRef、Release-before-zero、复活与删除入口 | `cpp/core/tjs2/tjsObject.cpp:85-130` | 匹配 |
| reverse native Invalidate/Destruct、重入guard、四槽GET/REGISTER | `cpp/core/tjs2/tjsObject.cpp:359-420`、`:1951-1980` | 匹配 |
| Object/ObjThis双ref、Clear先置Void、CopyRef先acquire | `cpp/core/tjs2/tjsVariant.cpp:320-337`、`:378-402`、`:476-546` | 匹配 |
| generic adaptor sticky/non-sticky、attach/CreateAdaptor | `cpp/core/plugin/ncbind.hpp:121-234` | 匹配 |
| value/reference/pointer boxing owner选择 | `cpp/core/plugin/ncbind.hpp:512-555` | 匹配 |
| typed constructor/factory cleanup | `cpp/core/plugin/ncbind.hpp:1125-1159` | 匹配 |
| raw factory sentinel/attach cleanup | `cpp/core/plugin/ncbind.hpp:1406-1445` | 匹配 |
| D3D sticky base与borrowed view | `cpp/plugins/DrawDeviceD3D.cpp:77-170` | 匹配 |
| D3DImage borrowed root和holder-first dtor | `cpp/plugins/DrawDeviceD3D.cpp:1415-1459` | 匹配 |
| getModule双owner | `cpp/plugins/motionplayer/EmotePlayer.cpp:190-205` | 匹配 |

本轮没有语义代码修改。新增的IDB改进为：四端各命名 `tTJSDispatch_AddRef`、
`tTJSDispatch_Release`、`D3DEmotePlayer_getModule`；76个请求入口追加task comment；每端为Release
和getModule增加bookmark；四份IDB均已保存。

## 11. disposition

| 审计项 | 四端结论 |
|---|---|
| 漏配 AddRef/Release | 未发现本地偏差；closure双ref、accessor、persistent Variant和manual interface ref均匹配 |
| native instance owner | generic adaptor、sticky base、borrowed view三种拓扑均匹配，不能合并 |
| 构造失败 | raw leak、typed cleanup、partial publication和REGISTER失败均已入账 |
| 析构重入 | refcount复活/nested Release、native旧slot可见、raw owner未预清零和listener晚注销均已入账 |
| double release | 正常self-closure两次Release有两个真实ref；getModule是实际双owner/double-delete bug |
| zero-ref | Release-before-zero、无underflow guard、无atomic及callback异常poison均已入账 |
| 删除 thunk | payload ordinary dtor、adaptor complete/deleting、virtual D3D deleting三层已分离 |
| borrowed pointer | MotionNode、D3D parent/listener/view/set、Array Items和texture key均标明失效边界 |

`MP-L16` 与 `MP-B10` 的 task-local静态缺口为零。formal native/Web build、unit运行和运行时
差分仍由独立 `MP-V` 任务跟踪，不反向改变这里已经闭合的四端静态生命周期语义。
