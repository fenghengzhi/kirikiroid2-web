# motionplayer D3D texture、background/caption、listener 与 manager 容器总审计（四参考二进制，2026-08-27）

## 1. 范围与结论

本报告逐项闭合 `MP-C12`：D3D texture/background/caption/listener/manager 容器。

四端联合结论不是“五类对象各有一个容器”，而是：

1. D3DAdaptor有一棵 `std::map<source texture*, intrusive texture holder>`；
2. D3DLayerObject有一个允许duplicate的 `std::list<listener*>`；
3. D3D root有Front/Back两个`multiset`、Managed image `set`和Modules owning `map`；
4. secondary `tTVPDrawDevice` base有一个持有manager引用的 `std::vector<manager*>`；
5. mobile四参考中的background/caption API是兼容空操作，**不存在**对应native容器；
6. `unloadUnusedTextures`同样是空操作；唯一texture cache清理入口是`removeAllTextures`。

因此不能从旧Windows后端、接口名字或本地期望反向补造background/caption deque/map，也不能把
listener/root的borrowed pointer容器改成owning容器。当前本地实现与四端一致，本轮没有semantic
C++ edit。

本轮fresh覆盖72个独立函数实例、2933条完整指令；所有decompile成功，disassembly均未截断，
并读取xref。manager三函数通过四端 `tTVPDrawDevice` vtable的固定槽位和函数本体双重确认；
Android/iOS、LP64/ILP32的vector/list/tree lowering差异不改变共同C++容器类型。

## 2. fresh 四端函数分母

### 2.1 background/caption 明确缺失与 texture map

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `removeAllBg` | `0x6AACD0`；1 | `0x57CF7A`；1 | `0x100103D88`；1 | `0x101154`；1 |
| `removeAllCaption` | `0x6AACD4`；1 | `0x57CF7C`；1 | `0x100103D8C`；1 | `0x101156`；1 |
| `registerBg` | `0x6AACD8`；1 | `0x57CF7E`；1 | `0x100103D90`；1 | `0x101158`；1 |
| `registerCaption` | `0x6AACDC`；1 | `0x57CF80`；1 | `0x100103D94`；1 | `0x10115A`；1 |
| `unloadUnusedTextures` | `0x6AACE0`；1 | `0x57CF82`；1 | `0x100103D98`；1 | `0x10115C`；1 |
| `removeAllTextures` | `0x6AAC98`；14 | `0x57CF74`；2 | `0x100103D58`；12 | `0x101138`；11 |
| source texture getter | `0x6EE440`；160 | `0x5AC518`；157 | `0x10014019C`；118 | `0x1414C0`；196 |
| texture map emplace | `0x6EE778`；77 | `0x5AC700`；53 | `0x1001403FC`；57 | `0x141736`；54 |
| D3DAdaptor ctor | `0x6AAEF0`；48 | `0x57D0AC`；44 | `0x100103FA8`；46 | `0x10128C`；90 |
| D3DAdaptor dtor | `0x6AAFCC`；35 | `0x57D12E`；22 | `0x1001040A0`；31 | `0x1013BC`；71 |

前五行在每端都只有一条return指令。`registerBg/registerCaption`的typed NCB wrapper仍执行arity、
Variant、float和bool转换，但native target不读参数、不分配、不发布状态。完整D3DAdaptor ctor/dtor
又只构造/销毁software texture map、Window和target；不存在隐藏的background/caption member。

### 2.2 manager vector

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `tTVPDrawDevice` complete dtor | `0xA72970`；74 | `0x796E6C`；47 | `0x1002DC0F4`；46 | `0x2DBB0C`；80 |
| `AddLayerManager` | `0xA72C14`；67 | `0x7970B0`；34 | `0x1002DC360`；22 | `0x2DBD28`；21 |
| `RemoveLayerManager` | `0xA72D20`；111 | `0x797104`；63 | `0x1002DC3B8`；57 | `0x2DBD58`；98 |

四个vtable address point分别为 `0x1A304D8`、`0x10C5CB4`、`0x1019B35A0`、
`0x177AAA0`。`AddLayerManager`和`RemoveLayerManager`精确位于继承接口的第3/4个槽；四端函数已
统一确定性命名。

### 2.3 listener 与 root pointer containers

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| root primary dtor | `0x53244C`；77 | `0x49606C`；56 | `0x100233E1C`；64 | `0x232B14`；111 |
| Listener add | `0x531184`；17 | `0x495286`；14 | `0x1002336C8`；22 | `0x232572`；18 |
| Listener remove | `0x5311C8`；25 | `0x4952AC`；24 | `0x100233720`；11 | `0x23259A`；9 |
| LayerObject ctor/register view | `0x5333F0`；109 | `0x496990`；52 | `0x1002355B4`；37 | `0x2342B4`；80 |
| D3DImage complete dtor | `0x5336F4`；66 | `0x496F60`；32 | `0x100235CFC`；20 | `0x234AD8`；51 |

## 3. software texture map

共同源码容器是：

```text
std::map<iTVPTexture2D*, tTJSRefHolder<iTVPTexture2D>> softwareTextureCopies
```

- key是source texture的borrowed raw identity；不AddRef、不Release，也不解引用；
- mapped holder在unique insertion时对software copy AddRef；
- map hit返回holder内raw pointer，不增加ref；
- duplicate key不替换旧holder；
- `removeAllTextures`销毁整棵树，逐holder Release，再复位为空树；
- D3DAdaptor destructor也清同一map，随后Release target和Window；
- Player shared D3DAdaptor无exit destructor时，这棵map事实成为process-lifetime cache。

Android libstdc++先构造候选node并AddRef mapped texture，之后再查duplicate；duplicate路径
Release并delete候选。iOS libc++先查找，只在miss时分配/AddRef。共同source仍是同一个
`std::map::emplace`。

CreateTexture2D factory reference与map holder reference是两份owner。首个miss成功插入后caller
不释放factory ref，所以clear map只释放holder ref；factory ref仍可让copy存活。node allocation
抛出时raw factory ref没有RAII owner，copy泄漏而map仍miss。borrowed key先死则留下dangling数值
identity，地址复用可能错误命中；容器没有weak token或generation。

## 4. background/caption 和 unused texture 的 ABSENT disposition

五个legacy入口必须按两层解释：

```text
typed NCB wrapper:
    validate receiver/result/arity
    convert arguments
    invoke native target

native target:
    return immediately
```

所以“空操作”不等于脚本调用没有conversion异常；它只意味着conversion完成后没有native容器
副作用。四端同时满足：

- 没有bg/caption node allocation；
- 没有插入、查找、erase、clear或析构helper；
- D3DAdaptor完整ctor/dtor没有对应member construction/destruction；
- `removeAllBg/removeAllCaption/unloadUnusedTextures`不转调`removeAllTextures`；
- `registerBg/registerCaption`不保留传入Variant或texture。

因此这三类“容器”的正确恢复结果是ABSENT，而不是“尚未实现”。

## 5. listener list

共同容器是：

```text
std::list<D3DLayerListener*> Listeners
```

list node只借用listener pointer。Android旧libstdc++ sentinel不缓存size；iOS libc++ list缓存
size。共同操作为：

```text
AddListener(p):
    if p == null: return
    allocate detached node(p)
    link before end sentinel
    // iOS link后 size++

RemoveListener(p):
    if p == null: return
    erase every node whose payload == p
```

Add不查重；同一listener可出现多个node。allocation失败发生在link前，list保持原样。Remove是
`list::remove`，删除全部匹配，不是只删第一个。Android边扫描边unlink/delete；iOS可把匹配run
转入temporary list再释放，source语义一致。

listener base ctor先保存borrowed owner，再向list注册。注册后若derived ctor抛异常，base unwind
会Remove全部matches。正常D3DEmotePlayer/D3DPicture dtor最后由listener base摘链，但layer dtor
只清list nodes，不遍历pointee、也不清listener的owner字段。layer先死、listener后死会通过悬空
owner调用Remove；参考没有双向invalidation。

Draw/OnUpdate/matrix notification直接遍历live list，无snapshot或reentry fence：

- current callback自删使iterator失效；
- 删除future node改变余下遍历；
- append tail是否在本pass可见取决于live结构推进；
- callback抛出立即停止，先前副作用不回滚。

## 6. root 四棵 tree

D3D primary root按声明顺序构造：

```text
multiset<D3DLayerObject*, FrontIndexComparator> FrontItems
multiset<D3DLayerObject*, BackIndexComparator>  BackItems
set<D3DImage*>                                  ManagedObjects
map<uint32_t, D3DModuleBase*>                   Modules
```

前三棵树只own node、借用pointee；Modules map是唯一own mapped module的tree，root dtor按key顺序
virtual delete每个non-null value，再由member dtor清nodes。

Front/Back comparator每次解引用live child index，不保存独立integer key；是multiset而非set。
AddChild先Front后Back，第二次allocation失败留下Front-only membership。index更新先erase、改live
field、再insert，reinsert失败留下新index但不在tree。duplicate child允许；child dtor各只删除一个
identity node，多余node可悬空。

ManagedObjects只按D3DImage pointer identity唯一插入。image ctor内发布node；node allocation失败由
new-expression释放pending image。image dtor先清texture holder，再从borrowed root set erase；root
先死使image.Owner悬空。

## 7. manager vector

共同容器是：

```text
std::vector<iTVPLayerManager*> Managers
size_t PrimaryLayerManagerIndex
```

它与上述borrowed pointer容器不同：vector element是raw pointer，但每个成功Add拥有一份manager
引用。

### 7.1 Add

```text
Managers.push_back(manager)       // full时先grow/copy/publish新storage
manager.AddRef()
```

不查duplicate；同一manager可占多个元素并分别AddRef。grow allocation失败使vector不变且不AddRef。
append发布后才调用AddRef；若异常/非标准实现让AddRef逃出，vector element已提交但引用未建立。

### 7.2 Remove

```text
i = find first pointer-equal element
if i == end: throw internal error at DrawDevice.cpp:146
(*i).Release()
Managers.erase(i)
```

只删除第一个duplicate。Release发生在erase之前，saved iterator和live end仍公开；Release callback
若重入修改vector，原iterator不重新验证：

- 无reallocation的append可能被后续memmove tail shift一起纳入；
- reallocation或erase current/future element可使saved iterator悬空；
- Release抛出时element尚未erase，vector保留可能已改变引用状态的pointer。

### 7.3 destructor snapshot

```text
backup = Managers                    // 只复制raw pointers，不AddRef
for manager in fixed backup:
    manager.Release()
destroy backup storage
destroy then-current Managers storage // 不再逐元素Release
```

Release callback可以修改live Managers：新append不在backup中，不会被本次dtor Release；移除/释放
另一名已snapshot manager可让later backup entry悬空。backup allocation失败发生在任何Release前；
析构异常按ABI landing/noexcept路径处理，不提供业务rollback。

完整D3D root先析构secondary `tTVPDrawDevice` base，所以manager Release callback发生时primary
Front/Back/Managed/Modules trees仍存在；之后才进入primary root dtor。不能交换这两个阶段。

## 8. owner与异常矩阵

| 容器 | element owner | duplicate | 失败/重入边界 |
|---|---|---|---|
| software texture map | key borrow；value intrusive own | unique key，duplicate不替换 | candidate策略按STL；allocation失败泄漏raw factory ref |
| bg/caption | ABSENT | 不适用 | wrapper conversion仍可失败；native无提交 |
| listener list | node own；listener borrow | 允许；Remove全删 | link前allocation安全；live iteration reentry失效 |
| Front/Back multiset | node own；layer borrow | 允许 | 两树partial membership、live comparator、dangling duplicate |
| ManagedObjects set | node own；image borrow | pointer unique | ctor内publication；root/image互相不retain |
| Modules map | node和mapped module own | same key overwrite不delete旧value | replacement leak；getModule另造double owner |
| Managers vector | pointer + one AddRef owner/element | 允许；Remove首个 | Add先publish后AddRef；Remove先Release后erase；dtor raw snapshot |

## 9. 本地映射与 disposition

| 参考语义 | 本地位置 | 结果 |
|---|---|---|
| no-op background/caption/unused API | `cpp/plugins/motionplayer/D3DAdaptor.h:71-78` | 匹配；容器ABSENT |
| software texture map与clear | `cpp/plugins/motionplayer/D3DAdaptor.h:129`、`D3DAdaptor.cpp:199` | 匹配 |
| source getter/map emplace | `cpp/plugins/motionplayer/D3DAdaptor.cpp:139-230` | 匹配 |
| root四树 | `cpp/plugins/DrawDeviceD3D.cpp:190-270` | 匹配 |
| listener list与borrowed owner | `cpp/plugins/DrawDeviceD3DIntf.h:28-100`、`DrawDeviceD3D.cpp:744-810` | 匹配 |
| image managed set | `cpp/plugins/DrawDeviceD3D.cpp:1415-1459` | 匹配 |
| manager vector、Add/Remove/dtor snapshot | `cpp/core/visual/impl/DrawDevice.h:530-560`、`DrawDevice.cpp:52-82`、`:166-190` | 匹配 |

本轮没有语义代码修改。四端72个入口已追加task comment和manager bookmark；前三个此前未命名
的目标各自确定性命名为`tTVPDrawDevice__complete_dtor`、`tTVPDrawDevice__AddLayerManager`、
`tTVPDrawDevice__RemoveLayerManager`；iOS armv7沿用已有确定性名称；四份IDB均保存。

`MP-C12` 的task-local静态缺口为零。跨全部容器的empty/duplicate/erase/EH总审计、STL差异总
分类和formal build仍分别由`MP-C15`、`MP-C16`与`MP-V`任务跟踪。
