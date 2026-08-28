# D3D adaptor、listener、texture map 与 managed set 四端生命周期审计

## 结论

`MP-L13` 已闭合。四个参考二进制共同恢复出以下不能互换的 owner/borrow graph：

```text
concrete NCB adaptor
  owns -> DrawDevice D3D root / D3DLayer / D3DImage / D3DPicture / D3DEmotePlayer

DrawDevice D3D root
  borrows -> ScriptOwner dispatch
  owns intrusive refs -> FrontTarget, BackTarget, TransitionRuleTexture
  multiset nodes borrow -> D3DLayerObject* FrontItems
  multiset nodes borrow -> D3DLayerObject* BackItems
  set nodes borrow      -> D3DImage* ManagedObjects
  map nodes own         -> D3DModuleBase* mapped values

D3DLayerObject
  borrows -> root Parent
  list nodes borrow -> D3DLayerListener*
  TJS native shell keeps a separate borrowed-view adaptor -> this

D3DLayerListener base
  borrows -> D3DLayer owner
  ctor registers this in owner list
  dtor removes every matching list node

D3DImage
  borrows -> root Owner
  owns -> heap tTJSRefHolder<iTVPTexture2D>
  root ManagedObjects only borrows image pointer

D3DAdaptor
  raw-retains -> Window dispatch (manual AddRef/Release)
  owns intrusive ref -> target texture
  ordered map key borrows -> source texture identity
  ordered map value owns intrusive ref -> software texture copy
```

三种 root pointer tree 的 ownership完全不同：Front/Back/Managed 只拥有 node，析构绝不
delete pointee；Modules root destructor按 key order delete mapped module，再由 map destructor
释放 nodes。Listener list也只拥有 nodes，允许 duplicates，Layer destructor不通知 listeners。

因此 owner-first destruction是参考的尖锐边界：Layer先死会让后续 listener destructor通过
raw owner调用 `RemoveListener`；root先死会让后续 D3DImage destructor访问已销毁的
ManagedObjects tree。参考没有 owner invalidation、weak token、AddRef或observer fence，本地也
不能用 smart owner自动修复。

D3DAdaptor constructor/destructor/software-map已有独立四端完整切片；本轮重新 fresh读取其
ctor/dtor并与新恢复的 Listener/Managed/root tree证据合并。没有 semantic C++ edit。四个
IDB 已补充确定性命名、owner注释、书签并保存。

## 1. 本轮四端函数证据

下表全部 fresh decompile；完整 disassembly均为 `cursor.done=true`、`truncated=false`。

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| root primary dtor | `0x53244C` / 77 | `0x49606C` / 56 | `0x100233E1C` / 64 | `0x232B14` / 111 |
| `D3DLayerObject` ctor/register view | `0x5333F0` / 109 | `0x496990` / 52 | `0x1002355B4` / 37 | `0x2342B4` / 80 |
| `AddListener` | `0x531184` / 17 | `0x495286` / 14 | `0x1002336C8` / 22 | `0x232572` / 18 |
| `RemoveListener` | `0x5311C8` / 25 | `0x4952AC` / 24 | `0x100233720` / 11 | `0x23259A` / 9 |
| `D3DEmotePlayer` complete dtor | `0x533FE0` / 36 | `0x497870` / 12 | `0x100236374` / 12 | `0x235076` / 12 |
| `D3DImage` factory/ctor path | `0x52D98C` / 109 | `0x4939F8` / 71 | `0x100231BE8` / 53 | `0x2309DC` / 89 |
| `D3DImage` complete dtor | `0x5336F4` / 66 | `0x496F60` / 32 | `0x100235CFC` / 20 | `0x234AD8` / 51 |
| `D3DAdaptor` ctor | `0x6AAEF0` / 48 | `0x57D0AC` / 44 | `0x100103FA8` / 46 | `0x10128C` / 90 |
| `D3DAdaptor` dtor | `0x6AAFCC` / 35 | `0x57D12E` / 22 | `0x1001040A0` / 31 | `0x1013BC` / 71 |

Android arm64把 Listener base destructor内联在 D3DEmotePlayer complete destructor尾部；
其余三端保留独立 16/17/50 指令 helper（`0x497988`、`0x1002364C4`、`0x235164`）。四端
共同调用 owner vtable 的 `RemoveListener(this)`，不是直接 free一个 list node。

## 2. DrawDevice root 的四棵红黑树

共同 declaration order：

```text
multiset<D3DLayerObject*, FrontIndexComparator> FrontItems
multiset<D3DLayerObject*, BackIndexComparator>  BackItems
set<D3DImage*>                                  ManagedObjects
map<uint32_t, D3DModuleBase*>                   Modules
```

Android old-libstdc++ tree header为 `0x30/0x18`，iOS libc++ tree header为
`0x18/0x0C`；四端 constructor 均按上面顺序建立四个空 tree。ABI header、sentinel与 node
link方向不同，不改变 source container选择。

### 2.1 Front/Back multiset

node只存 `D3DLayerObject*`。comparator不保存 integer key，而是在每次比较时解引用两个
pointee的 live `FrontIndex`/`BackIndex`。插入不拒绝 equivalent key，证明是 multiset而非
set/map。

共同 owner规则：

- tree owns only red-black node；stored pointer完全 borrowed；
- `AddChild`依次插入 Front再Back；第二次分配抛出会留下 Front-only partial membership；
- `SetParent`先发布 raw Parent再调用 AddChild；第一次插入抛出也留下 Parent已变、两个 set
  都缺失；
- `EraseFront`/`EraseBack`只在 equal range内删除第一个 pointer identity相等的 node；
- repeated AddChild允许同一 pointer出现多次；child destructor只各删一个，额外 duplicate
  node可成为 dangling pointer；
- setFrontIndex/setBackIndex先 erase、再改 live comparator key、再 insert；最后 insert抛出会
  留下新 index但不在对应 tree；
- tree iteration直接使用 live nodes，没有 snapshot/reentry guard；callback删除 current node
  后，iterator increment读取 freed node。

tree destructor只删除 nodes，不比较或解引用 stored pointer，因此 dangling pointer本身不妨碍
root teardown；但 root仍存活时任何查找/插入/遍历都可能通过 comparator或callback观察
悬空 pointee。

### 2.2 ManagedObjects set

这是 pointer-identity `std::set<D3DImage*>`，唯一 key，仍只拥有 node：

```text
D3DImage ctor:
    this.Owner = root                    // borrowed, no AddRef
    this.Picture = null
    root.ManagedObjects.insert(this)     // constructor内 publication

D3DImage dtor:
    ClearTextureHolder()                 // Release texture + delete holder
    if Owner: Owner.ManagedObjects.erase(this)
```

fresh image address在正常 constructor中唯一，因此 insert成功后 exactly one node。node
allocation抛出属于 D3DImage constructor failure：set尚未链接，outer new-expression释放
image storage，factory result尚未发布。它不同于 task L08 中 constructor完成后才 raw-emplace
的 leak window。

successful insert以后 factory才把 image pointer写 result。若外层 concrete adaptor lookup
失败，descriptor delete fresh image，complete destructor会先清 texture holder，再移除
managed node。

尖锐边界：

- root set不持有 image ref，root destructor不delete images；
- image不持有 root ref；root先死会让 `Owner`悬空；
- D3DImage factory re-entry覆盖 concrete adaptor native pointer但不delete old image，old image
  与 managed node一起泄漏；
- `ClearTextureHolder`先于 set erase。若 texture Release异常逃出，erase尚未发生，root保留
  将要销毁/terminate对象的 pointer；参考不提供 rollback；
- `load`每次 `new RefHolder`直接覆盖 Picture，旧 holder不delete，software copy的 factory
  ref也不平衡；这些 holder泄漏不改变 managed set ownership。

### 2.3 Modules owning map

Modules是四棵 tree中唯一真正拥有 pointee的容器：

```text
map<uint32_t, D3DModuleBase*> Modules
```

`SetParentModule(classId,module)`使用 `operator[]` 后直接覆盖 mapped raw pointer；同 key旧
module不会先delete，形成 replacement leak。root normal destructor在 tree仍完整时按 key order
遍历，对每个 non-null mapped value调用 virtual deleting destructor，然后 map member
destructor只清 nodes。

模块删除异常属于 destructor/noexcept terminate边界；后续 values和trees不会得到业务层
catch补偿。不能把 map改成 `unique_ptr`，否则 overwrite与构造失败行为会改变。

## 3. root 普通析构顺序

四端 primary destructor共同顺序：

```text
release FrontTarget; slot = null
release BackTarget; slot = null
release TransitionRuleTexture; slot = null

for Modules in key order:
    delete mapped module

destroy TransitionVariant
destroy Modules tree nodes
destroy ManagedObjects tree nodes          // does not delete images
destroy BackItems tree nodes               // does not delete layer objects
destroy FrontItems tree nodes              // does not delete layer objects
```

完整 root derived object还先析构 `tTVPDrawDevice` secondary base：它 snapshot/Release layer
managers，但不delete plugin tree pointers。随后才进入上面 primary phase。

`CurrentTarget`不是 root destructor owner：capture/Show把它当临时 live slot，正常尾部Release/
clear；callback/operation异常若跳过尾部，root destructor也不补释放。这是独立 temporary-owner
边界，不能并入 Front/Back target ownership。

`EnsureTargets`先Release两个旧 target，再创建/发布 Front，最后创建/发布 Back。Back创建
抛出会保留 fresh Front和 null Back；普通 root dtor最终Release该 Front，不做即时事务回滚。

## 4. D3DLayer listener list

`D3DLayerObject` logical layout在 scalar fields之后拥有：

```text
std::list<D3DLayerListener*> Listeners
```

Android list sentinel不保存 cached size；iOS libc++ list带 size。共同 payload仍是一个 borrowed
pointer。

### 4.1 Listener constructor/publication

```text
Listener(owner):
    vptr = Listener vtable
    _d3dLayerOwner = owner               // borrowed
    _stretchType = 8
    _bicubicParam = -0.5f
    if owner: owner.AddListener(this)
```

AddListener行为：

- null是 no-op；
- non-null分配一个 list node；
- payload与link fields在 detached node中完整初始化后才link到sentinel；
- iOS size只在link以后增加；
- allocation是唯一 throwing operation，失败时 owner list byte-for-byte不变；
- 不查 duplicate，同一 listener pointer可有多个 nodes。

因此 listener base constructor在 node allocation失败时没有 half-registration；derived
constructor unwind也无需 RemoveListener。注册成功后若更晚的 derived member constructor
抛出，正常 C++ base unwind会调用 Listener destructor并移除全部 matches。

### 4.2 Remove/destructor

RemoveListener是 `std::list::remove(listener)`：null no-op，non-null扫描并删除**所有**相等
payload。Android边扫描边 unlink/delete；iOS把匹配 runs转入 temporary list后统一释放并维护
cached size。source行为一致。

D3DEmotePlayer normal destructor顺序：

```text
delete secondary EmoteObject
delete primary EmoteObject
Listener base dtor -> owner.RemoveListener(this)
```

D3DPicture derived fields中 Image与TransformLayer是 raw borrows；normal destruction释放
ImageRanges vector，然后 Listener base unregister，既不访问也不释放 D3DImage。

Layer/list没有双向 invalidation：

- D3DLayer destructor只让 list member清nodes，不遍历 listener pointees或清其 owner field；
- layer先死、listener后死会通过悬空 `_d3dLayerOwner` virtual-call；
- listener正常先死则Remove all duplicates；
- Draw/OnUpdate/matrix notification直接遍历live list；current callback自删使迭代器失效，
  删除future node/追加tail在同一pass可见，异常立即中止剩余 listeners。

## 5. borrowed D3DLayerObject native view

每个 `D3DLayerObject` construction还创建独立 adaptor：

```text
adaptor = new {borrowedAdaptorVptr, this}
owner.NativeInstanceSupport(REGISTER, borrowedClassId, &adaptor)
ignore status
```

它不是 concrete class adaptor，也不拥有 D3DLayerObject。REGISTER允许同 class ID重复占用
最多四个 native slots；满槽失败被忽略，fresh adaptor泄漏。GET从最早slot开始，root add/
remove可继续看到oldest generation。

borrowed adaptor Invalidate/destructor从不delete或清 object pointer。具体 D3DLayerObject死后，
TJS shell可永久保留 dangling view；不能借 listener unregister顺手detach它。该生命周期已经由
DrawDevice dependency-root切片完成独立 vtable/xref闭包，本报告将它纳入对象图。

## 6. D3DAdaptor 与 software texture map

共同 source container：

```text
std::map<iTVPTexture2D*, tTJSRefHolder<iTVPTexture2D>>
```

- key只借用 source texture pointer identity，不AddRef/Release、不解引用；
- mapped holder在unique insertion时AddRef software copy；
- hit返回 `GetObjectNoAddRef()` borrow，不增加ref；
- `removeAllTextures()`/destructor逐 mapped holder Release；
- Android libstdc++先分配candidate node并AddRef、再查 duplicate；duplicate会Release/delete
  candidate；
- iOS libc++先查，只在miss时分配/AddRef；
- CreateTexture2D返回的factory reference不是 map holder reference；caller不释放首个 factory
  ref，map clear后它仍可独立存活；
- factory成功后map node allocation抛出时，raw copy无RAII owner并泄漏，map保持未插入；
- borrowed key对应source texture先死会留下dangling numeric identity；tree比较只比较pointer
  value，但地址复用/业务lookup仍是原始风险。

D3DAdaptor本体 owner：

```text
raw _window + manual AddRef/Release
raw _targetTexture + one intrusive creation/ref ownership
software map mapped holder refs
```

constructor先发布参数和Window slot，再Window.AddRef，再创建target；target创建异常只析构空
map，不Release已AddRef Window。normal destructor先clear map，再Release/null target，再Release
Window但不清slot，最后map member destructor检查空树。

Player另有process-global raw shared D3DAdaptor：无mutex/static guard、成功后永久保存、无exit
destructor。并发首建和process-lifetime Window/target retention属于MP-L15/B09的global总审计，
本任务只标出该对象不是普通NCB adaptor owner。

## 7. publication/异常矩阵

| 操作 | throw前已提交 | 不回滚结果 |
|---|---|---|
| Listener list node allocation | 无 | list原样 |
| Front set insert成功、Back insert失败 | Parent与Front membership | Back缺失 |
| SetParent后Front首次insert失败 | raw Parent | 两个sets缺失 |
| setFront/backIndex erase后reinsert失败 | erase + new live index | 对应set缺失 |
| D3DImage managed node allocation失败 | Owner/Picture只在pending object | set原样，new-expression释放object storage |
| D3DImage Picture release失败 | holder clear正在执行 | managed node尚未erase |
| Modules `operator[]`后pointer overwrite | new mapped pointer | old module泄漏 |
| D3DAdaptor map node allocation失败 | raw factory texture created | raw factory ref泄漏，map miss保持 |
| target replacement/second create失败 | old targets已Release；可能fresh Front已发布 | no immediate rollback |
| callback遍历中抛出 | earlier side effects | remaining listeners/items不访问 |

## 8. 本地映射

| 参考语义 | 本地位置 | 结果 |
|---|---|---|
| D3DLayerObject/listener declarations | `cpp/plugins/DrawDeviceD3DIntf.h:28` | 匹配 |
| Front/Back/Managed/Modules typed trees | `cpp/plugins/DrawDeviceD3D.cpp:190` | 匹配 |
| AddChild/erase partial membership | `cpp/plugins/DrawDeviceD3D.cpp:337` | 匹配 |
| D3DLayerObject ctor/view registration | `cpp/plugins/DrawDeviceD3D.cpp:744` | 匹配 |
| Listener ctor/dtor/list add/remove | `cpp/plugins/DrawDeviceD3D.cpp:786` | 匹配 |
| root target/module/tree teardown | `cpp/plugins/DrawDeviceD3D.cpp:1062` | 匹配 |
| D3DImage managed insertion/removal | `cpp/plugins/DrawDeviceD3D.cpp:1415` | 匹配 |
| D3DAdaptor owner/map declaration | `cpp/plugins/motionplayer/D3DAdaptor.h:22` | 匹配 |
| D3DAdaptor ctor/dtor/map clear | `cpp/plugins/motionplayer/D3DAdaptor.cpp:21` | 匹配 |
| source texture map lookup/emplace | `cpp/plugins/motionplayer/D3DAdaptor.cpp:199` | 匹配 |

已有 tests覆盖 borrowed native slots、oldest generation、layer/image factory、Front/Back add/remove、
listener duplicates、managed set、D3DAdaptor target/map holder，主要位于
`tests/unit-tests/plugins/motionplayer-dll.cpp:8781`、`:9660`、`:9943`、`:10170`、
`:11277`。正式test binary运行仍由MP-V07/V08跟踪。

本任务无 C++ semantic edit。IDB中新恢复的 root dtor、LayerObject ctor、Listener add/remove、
D3DImage factory/dtor、Listener dtor已确定性命名；D3DAdaptor沿用前序确定性名称。

## 9. Disposition

| 观察项 | disposition |
|---|---|
| Front/Back/Managed只拥有tree nodes | 共同borrowed source语义 |
| Modules mapped raw pointer由root delete | 共同owning map语义 |
| Listener list允许duplicates并只借用pointee | 共同std::list语义 |
| Layer/root先死导致late dtor悬空owner | 共同尖锐生命周期边界；保留 |
| borrowed native view在object死后不detach | 共同NCBind历史边界；保留 |
| D3DImage managed insert是constructor内publication | allocation失败安全回滚；不同于post-ctor raw handoff |
| D3DImage load覆盖旧holder | 共同holder leak边界；保留 |
| D3DAdaptor map raw key + intrusive value | 共同source container |
| Android/iOS tree/list node策略不同 | libstdc++/libc++ ABI/STL lowering |
| Player global shared D3DAdaptor无exit dtor | process-global边；转MP-L15/B09总审计 |

`MP-L13` task-local 静态缺口为零；所有D3D background/caption/manager容器总账、global cache
与跨对象AddRef/Release总审计仍由 `MP-C12`、`MP-L15`、`MP-L16` 独立跟踪。
