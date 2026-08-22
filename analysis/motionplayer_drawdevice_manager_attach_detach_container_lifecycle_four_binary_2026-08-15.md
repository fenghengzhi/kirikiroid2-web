# motionplayer DrawDevice manager 挂接、摘除、容器与生命周期：四参考二进制对照

日期：2026-08-15

范围：`reference/binaries/` 中 Android arm64、Android armv7、iOS arm64、iOS armv7 四份参考二进制。本笔记只记录四份当前参考共同支持的行为；旧 `libkrkr2.so` 注释仅作待验证线索，不能覆盖这里的四平台证据。

## 结论

插件根对象的 `AddLayerManager` / `RemoveLayerManager` 不是一套事务式、容错式注册表，而是在共享 `tTVPDrawDevice` 裸指针 vector 之上叠加一个由 manager 单槽反向指向的插件 item。原版明确保留下列边界：

- Add 先把 manager 追加到共享 vector 并 AddRef，随后才修改 `HoldAlpha`、分配 item、写回 manager data；后半段失败不会回滚前半段。
- Remove 先读取、清空并删除 manager data，最后才在共享 vector 中查找、Release 并移除首个匹配项；传入外来 manager 时也可能先破坏其 data，再抛“未注册”错误。
- vector 允许重复 manager。每次 Add 都追加并 AddRef；每次 base Remove 只删除首个匹配项。
- manager data 只有一个槽，重复 Add 会覆盖旧 item 指针而不删除旧 item，形成仍挂在根对象前/后树里的孤儿 item。
- primary-manager 索引不会在删除时修复。
- window 指针只是借用存储；和 manager 挂接完全独立。
- `DrawDeviceManagerItem` 构造要求 manager、primary layer、primary main image 均有效；原版没有对应的 null guard。

源码已据此把旧的 `SetDesiredLayerType(0)` 误认纠正为 `static_cast<tTVPLayerManager *>(manager)->SetHoldAlpha(false)`，并移除了 manager-item 构造器中原版不存在的 primary/main-image 防护。

## 四平台入口

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| 派生 `DrawDeviceObjectBase::AddLayerManager` | `0x531770` | `0x4958C4` | `0x100234174` | `0x232DCC` |
| 派生 `DrawDeviceObjectBase::RemoveLayerManager` | `0x531824` | `0x49593C` | `0x100234228` | `0x232EE8` |
| `tTVPDrawDevice` complete dtor | `0xA72970` | `0x796E6C` | `0x1002DC0F4` | `0x2DBB0C` |
| `tTVPDrawDevice::SetWindowInterface` | `0xA72C0C` | `0x7970AC` | `0x1002DC358` | `0x2DBD24` |
| `tTVPDrawDevice::AddLayerManager` | `0xA72C14` | `0x7970B0` | `0x1002DC360` | `0x2DBD28` |
| `tTVPDrawDevice::RemoveLayerManager` | `0xA72D20` | `0x797104` | `0x1002DC3B8` | `0x2DBD58` |
| `tTVPLayerManager::SetHoldAlpha` | `0x834170` | `0x64AB6C` | `0x10031B564` | `0x3205E0` |
| `DrawDeviceManagerItem` ctor | `0x53287C` | `0x496480` | `0x100234FA8` | `0x233C14` |
| primary owner 取值并 AddRef helper | `0x8333B4` | `0x64A290` | `0x100096FC8` | `0x95530` |

恢复 IDB 中上述共享函数已经命名；owner helper 暂命名为 `tTJSNI_BaseLayer_GetOwnerAddRef_guess`，因为其逻辑确定而剥离前源码符号仍未知。

## Add 的精确数据流

四份参考一致：

```text
DrawDeviceObjectBase::AddLayerManager(manager)
  ├─ tTVPDrawDevice::AddLayerManager(manager)
  │    ├─ Managers.push_back(manager)
  │    └─ manager->AddRef()
  ├─ static_cast<tTVPLayerManager *>(manager)->SetHoldAlpha(false)
  │    ├─ manager.HoldAlpha = false
  │    └─ 若 DrawBuffer 已存在：DrawBuffer->DestTexture.HoldAlpha = false
  ├─ TVPIsSoftwareRenderManager()
  ├─ software ? new SoftwareDrawDeviceManagerItem_guess(...)
  │           : new DrawDeviceManagerItem(...)
  └─ manager->SetDrawDeviceData(item)
```

`SetHoldAlpha` 的字段位置也在四份参考中成对一致：

| 位宽 | manager DrawBuffer | manager HoldAlpha | `tTVPDestTexture` HoldAlpha |
|---|---:|---:|---:|
| 64 位 | `+40` | `+230` | `+96` |
| 32 位 | `+20` | `+138` | `+68` |

所以这个调用不是“设置 desired layer type”。它会即时改变已经创建的 destination texture 的 alpha 复制行为。当前差分测试先创建 draw buffer：Add 前 `CopyRect` 保留目标 alpha，Add 后同一操作复制源 alpha，从运行时可观察面验证了这条路径。

### Add 的异常和空指针边界

- base vector 扩容失败时，manager 尚未追加，也尚未 AddRef。
- `push_back` 成功后，代码立即调用 `manager->AddRef()`；null manager 会先进入 vector，再在 AddRef 处崩溃。
- base Add 成功后，`SetHoldAlpha`、render-manager 查询、`new`、item 构造中的任一步失败，都没有回滚 vector 项或 AddRef。
- 没有 duplicate 检查。
- 没有把派生后半段包成 try/catch，也没有 scope guard。
- 直到 item 完整构造完成后才写 `SetDrawDeviceData(item)`；构造失败时 data 槽仍是旧值或原值。

### V276：item `new` 的平台异常清理差异

V276 对四份当前 canonical recovery database 重新读取了派生 Add、base Add、`SetHoldAlpha`
和 item 构造器的完整 EH/landing 区间。源码层仍是一条普通 `new` expression，但四份已发布
构建的编译器清理边界不相同：

| 目标 | item ctor 内部 escape | Add 外层 `new` escape |
|---|---|---|
| Android arm64 | guard initializer 先 `__cxa_guard_abort`（若适用），随后调用 base item/D3DLayerObject 析构 | raw `operator delete(allocation)` 后 resume |
| Android armv7 | 无 cleanup landing；不调用 guard-abort 或 base dtor | 无 cleanup landing；不 raw-delete allocation |
| iOS arm64 | 无 cleanup landing；不调用 guard-abort 或 base dtor | 无 cleanup landing；不 raw-delete allocation |
| iOS armv7 | SJLJ landing 对两个 guard 分别 abort，随后调用 base item/D3DLayerObject 析构 | 两个 constructor call-site 分别选择已保存的软件/base allocation，raw delete 后 SJLJ resume |

即使存在 constructor cleanup，它也不归还 `PrimaryOwner` 的 owning AddRef，因为正常 base item
析构本来就不 Release 该字段；已经发生的 main-image Fill 也不可回滚。若 parent attach 已完成，
base 析构会按正常 detach 路径尝试从 front/back 树移除。

Android armv7/iOS arm64 没有本地 cleanup edge：异常若越过这些 frame，不会在本地析构或释放；
若运行时把无 landing 的 escape 终止，则终止本身同样不会产生上述 cleanup。尤其 guarded static
initializer 的业务调用逃逸时，没有编译器生成的 `__cxa_guard_abort`。

四端最终 `manager->SetDrawDeviceData(item)` 都位于完整 item 构造之后，且没有 owner local、
landing 或 delete。该虚调用若逃逸，完整 item 及其 tree membership 保留；data 槽是旧值、部分
写入值还是新值只取决于 manager 虚调用抛出前已经完成的工作。普通 concrete manager 的实现只
做单槽 store，因此正常返回时才由该槽发布新 item；重复 Add 覆盖旧 pointer 而不 delete 旧 item。

## 共享 manager vector 的实现

`tTVPDrawDevice` 保存的是 `std::vector<iTVPLayerManager *>` 风格的三个连续指针：

| 位宽 | begin | end | capacity-end |
|---|---:|---:|---:|
| 64 位 | 次基类 `+24` | `+32` | `+40` |
| 32 位 | 次基类 `+12` | `+16` | `+20` |

根对象中的次基类偏移及 vector 起点为：

| 目标 | `tTVPDrawDevice` 次基类偏移 | 根对象中的 Managers begin |
|---|---:|---:|
| Android arm64 | `+0x178` | `+0x190` |
| Android armv7 | `+0xD4` | `+0xE0` |
| iOS arm64 | `+0x118` | `+0x130` |
| iOS armv7 | `+0xA4` | `+0xB0` |

增长策略是标准倍增：空容量首次扩到 1，否则选择足以容纳 `size + 1` 的 `max(size + 1, 2 * capacity)` 等价容量。指针元素上限为 64 位 `0x1FFFFFFFFFFFFFFF`、32 位 `0x3FFFFFFF`；超过上限走 `length_error`。

V271 又确认 `getPrimaryLayers` 在入口把该 vector 的 begin/end 各加载一次并用raw cursor遍历，
不是每轮重读end。每项严格走 manager `GetPrimaryLayer`，再用本报告已确认的owner AddRef helper
取得cached owner；helper的`+1`从不Release，随后native Array Items closure另持两refs。因此每个
non-null owner、每次getter调用永久泄漏1 ref。callback内append不reallocate时新项在saved end后
本轮不可见；reallocate/erase/clear则使saved cursors悬空。完整Array/refcount/异常证据见
`motionplayer_drawdevice_getprimarylayers_native_array_manager_snapshot_owner_ref_leak_four_binary_2026-08-21.md`。

辅助/内联证据：A64 的扩容直接内联在 `0xA72C14`；A32 重分配/容量 helper 为 `0x64D314` / `0x64D394`；I64 为 `0x100288DBC`；I32 为 `0x28B654`。

## manager item 构造的严格前置条件

四份 `DrawDeviceManagerItem` 构造器都按以下顺序执行：

```text
Manager = manager
PrimaryLayer = manager->GetPrimaryLayer()          // manager 无 null guard
PrimaryOwner = GetOwnerAddRef(PrimaryLayer)        // PrimaryLayer 无 null guard
UpdateSettings()
mainImage = PrimaryLayer->GetMainImage()           // 无 null guard
mainImage->Fill(full image rect, ARGB 0)            // mainImage 无 null guard
SetParent_guess(owner)
```

owner helper 自身只对 owner 指针做检查：

```cpp
owner = primaryLayer->Owner;
if(owner) owner->AddRef();
return owner;
```

因此边界必须区分：

- owner helper 本身允许 `PrimaryOwner == nullptr`，第一次 AddRef 是条件式的；但构造器紧接着进入 `UpdateSettings`，其中 `ncbPropAccessor(PrimaryOwner)` 会无条件 AddRef。因此完整 manager-item 构造并不允许 owner 为空，null owner 会在属性读取之前崩溃。
- `manager == nullptr`、`PrimaryLayer == nullptr` 或 main image 为 null 不被允许，会进入原版崩溃边界。
- owner 的 AddRef 没有在 base item 或 software item 析构中配对 Release；这是四参考共同存在的生命周期泄漏，不能擅自修正。
- ARGB 0 填充发生在 parent attach 之前；attach 失败或后续异常不能撤销填充及 owner AddRef。

## Remove 的精确数据流

```text
DrawDeviceObjectBase::RemoveLayerManager(manager)
  ├─ item = manager->GetDrawDeviceData()
  ├─ if(item != nullptr)
  │    ├─ manager->SetDrawDeviceData(nullptr)
  │    └─ delete item
  └─ tTVPDrawDevice::RemoveLayerManager(manager)
       ├─ linear find first pointer-equal entry
       ├─ missing -> internal error (DrawDevice.cpp:146)
       ├─ manager->Release()
       ├─ shift tail left by one pointer
       └─ --end
```

关键顺序是先删除插件 item，后验证 base vector 成员资格。由此得到：

- null manager 在派生入口的 `GetDrawDeviceData` 即崩溃，根本到不了 base 查找。
- 外来、未注册 manager 若 data 槽非空，会先被清空并把该指针当 `DrawDeviceManagerItem` 删除，之后 base 才抛未注册错误。
- data 先清零再调用 deleting destructor；item 析构或 detach 过程异常时，manager 已失去该 item 指针。
- base 对匹配 manager 先 Release，后移动 vector 尾部。Release 可能触发回调时，待删除项仍暂时位于 vector 中。
- base 只删首个匹配项。
- 删除前后均不调整 `PrimaryLayerManagerIndex`；删除位于 primary 之前或正是 primary 的节点都会留下陈旧索引。

### V277：Remove 的异常覆盖和 Release 重入 iterator

V277 对四端派生/base Remove、base/software item deleting destructor 和
`D3DLayerObject` detach/list teardown 做了 fresh 指令复核。派生 Remove 四端都没有 cleanup
landing：`GetDrawDeviceData` 只快照一次；item 非空时先
`SetDrawDeviceData(nullptr)`，再通过 item vtable slot 1 调 deleting destructor，最后才 tail-call
base Remove。因此 SetData 逃逸会跳过 delete/base Remove；delete 终止或逃逸会保留已清 data 和仍在
vector 中的 manager；base missing error 则发生在 item 已经成功删除之后。

base Remove 的 `find` 保存的是进入函数时的 begin/end 和第一个 matching iterator。它在
`manager->Release()` 前不移动元素、不递减 end；Release 正常返回后重新加载 live end，却继续使用
旧 iterator：

- callback 只 append 且不 reallocate：新元素被纳入随后 tail shift，原 match 仍被删；
- callback 在旧 iterator 之前 erase/shift：旧地址现在指向别的元素，外层可错删；
- callback 导致 reallocation：旧 iterator 成为 dangling，后续比较/memmove 是 UAF；
- callback 把 live end 缩到 `iterator + 1` 之前：`liveEnd - (iterator + 1)` 变成负值并作为无符号
  memmove 长度使用；
- Release 逃逸：四端都没有覆盖该 call-site 的 rollback，vector 保持 callback/逃逸时状态。

iOS armv7 base Remove 的 SJLJ `call_site=1` 只保护 missing-manager internal-error 路径上的临时
path string；Release 前已把 call-site 重置为 `-1`。该 landing 不能被误读成 vector erase rollback。

item 析构的 compiler EH 分成与 Add ctor 相同的两组：

| 目标 | software cache `Release` 逃逸 | base detach 逃逸 | raw item delete |
|---|---|---|---|
| Android arm64 | 先跑 base dtor，再 `std::terminate` | 先清 listener-list nodes，再 terminate | 仅 complete dtor 正常返回后执行 |
| Android armv7 | 无本地 cleanup landing，base dtor 可被跳过 | 无本地 cleanup landing | 仅正常返回后执行 |
| iOS arm64 | 无本地 cleanup landing，base dtor 可被跳过 | 无本地 cleanup landing | 仅正常返回后执行 |
| iOS armv7 | SJLJ cleanup 跑 base dtor，再 terminate | SJLJ cleanup 清 list nodes，再 terminate | 仅正常返回后执行 |

普通析构仍不 Release `PrimaryOwner`，也不清 `Parent`/`Manager`/cache field。正常 base dtor 只从
front/back multiset 各删一个 matching node；任一成功才调用 base `OnDetached` 和 root
`OnItemsChanged`，随后只释放 list nodes，不 delete listener payload。

## 重复 Add/Remove 的真实结果

对同一 manager 连续 Add 两次：

1. vector 得到两个相同裸指针，并分别 AddRef。
2. 第一次构造 item A，并把 manager data 写成 A。
3. 第二次构造 item B；B 也挂到根对象的 front/back 树中，然后 manager data 从 A 覆盖为 B，没有删除 A。
4. A、B 都仍可由根对象绘制，但只有 B 能从 manager data 槽取回。

第一次 Remove：

- 清槽并删除 B；
- base 删除 vector 的首个匹配项并 Release 一次；
- A 仍是挂在根对象树中的孤儿，但 manager data 已为空。

第二次 Remove：

- data 为空，不删除 item；
- base 删除剩余匹配项并再 Release 一次；
- A 继续孤儿化，仍保存 manager/root 指针。

这是原版可观察到的不安全边界，不应在一比一复原代码中用去重或自动回收“优化”。

## window 与 manager 是两条独立状态链

`tTVPDrawDevice::SetWindowInterface` 仅把传入指针写到成员：64 位次基类 `+8`，32 位次基类 `+4`。它不：

- AddRef 或 Release window；
- 遍历/通知 Managers；
- 挂接或摘除 layer manager；
- 修复 primary index；
- 验证 null。

根对象通过最终次 vtable 的 slot 1 原样继承该行为。window 生命周期由外部保证；manager 注册不能被视为 window attach 的副作用。

## `tTVPDrawDevice` 析构

四份 complete destructor 的共有结构是：

1. 安装 `tTVPDrawDevice` 自己的 vtable。
2. 复制整个 Managers vector 到临时 vector。
3. 遍历临时 vector，对每个指针调用 Release；重复指针会重复 Release。
4. 释放临时 vector 存储。
5. 释放原 Managers vector 存储。

先复制再 Release 很重要：Release 可能间接触发回调或注销，临时快照避免遍历游标直接依赖被重入修改的原 vector。

析构不会：

- 清 manager 的 DrawDeviceData；
- 删除插件 `DrawDeviceManagerItem`；
- 从根对象 front/back 树中逐个摘除 item；
- 修复 `PrimaryLayerManagerIndex`；
- Release window。

这也解释了为什么“只依赖 base 析构完成摘除”不能等价于逐个调用派生 Remove。

### V278：snapshot 只稳定 storage，不稳定 pointee 生命周期

V278 fresh 四端复核确认临时 vector 是裸 pointer 的 exact-capacity 副本，复制时不 AddRef。循环
只遍历临时副本的固定 begin/end；原 `Managers` 可被 Release callback 修改，但这只保护 iterator，
不保护 pointer 指向对象：

- callback append 的 manager 不在 snapshot 中，析构尾部只 free live vector storage，不对新项
  Release，故其 AddRef 不会在本轮配对；
- callback Remove/Release 另一个尚未遍历的 manager 可把对应对象销毁，后续 snapshot Release 对
  dangling pointer 调虚函数；
- callback Remove 自己会在 snapshot Release 外再执行一次 Release；copy 本身没有持有额外 ref；
- callback 清空/重分配原 vector 不破坏 snapshot cursor，但改变最终被 raw-delete 的 original
  storage；析构不会根据新 live 内容补做第二轮 Release。

vector copy 的 allocation capacity 使用第一次 source begin/end size；allocation 后四端又重新读取
source begin/end决定 memcpy 长度和backup end。通常 operator new 不重入，但若全局 allocation hook
重入增长同一 Managers，第二长度可超过第一次 exact capacity，造成 copy overflow；缩短则只复制
新的较短前缀。

完整 root destructor 的直接基类顺序是先 `tTVPDrawDevice`、再 primary root base。manager Release
因此发生时 root 的front/back树与item仍存在，但vptr已经降为base draw-device；经虚接口重入的
`RemoveLayerManager`只能调用base版本，不会执行plugin data-clear/item-delete。manager phase结束后
primary destructor只释放tree nodes而不delete stored item pointers，故base析构从来不是plugin item
的owner teardown。

异常形态仍分两组：A64/I32在copy/Release逃逸时清backup（若已构造）和live vector storage后
terminate；A32/I64无本地cleanup landing，escape可越过两份storage清理及随后primary root析构。

### V279：concrete LayerManager 最终 Release 与 destructor ownership

V279 已把此前未闭合的具体 manager 生命周期补齐，完整四端证据见
`motionplayer_layer_manager_release_destructor_publication_owner_boundary_four_binary_2026-08-22.md`。
共同结论是：manager `Release(1)` 保持 refcount 为 1，经虚表 deleting destructor 删除；析构只删除
owned `DrawBuffer` 并让 `UpdateRegion`/三个 vector control block 正常析构，不清
`DrawDeviceData`、raw owner/layer pointers 或 vector pointees。normal BaseLayer 失效顺序为
`DetachPrimary -> owner Unregister -> manager Release -> BaseLayer.Manager=null`，所以最终析构时
BaseLayer 字段仍发布旧 manager pointer。该结论确认 V277 的 derived Remove 才是 plugin item/data
teardown owner；base/root 或 concrete manager destructor 都不是 fallback。

## 当前源码与测试落点

- `cpp/plugins/DrawDeviceD3D.cpp`：Add 的 exact order、`SetHoldAlpha(false)`、software/base item 分支、Remove 的清槽/删除/base-remove 顺序。
- `cpp/plugins/DrawDeviceD3D.cpp`：manager-item 构造不再保护 primary layer 或 main image；owner 仍条件式 AddRef。
- `tests/unit-tests/plugins/motionplayer-dll.cpp`：通过公开 `interface` 取得调整后的 `iTVPDrawDevice *`，使用带真实 primary layer 的真实 `tTVPLayerManager` 和预创建 draw buffer，验证 HoldAlpha 传播及 data 槽挂接/清除。
- 测试清理显式补掉 reference item 析构遗漏的 `PrimaryOwner` Release，避免把原版泄漏扩散到单测进程；这只是 fixture 清理，不改变插件源码行为。

## 恢复 IDB 状态

四份恢复库均已：

- 命名共享 draw-device complete destructor、window setter、base Add、base Remove 与 `SetHoldAlpha`；
- 标注派生 Add/Remove 的精确顺序、vector 的重复/异常边界和析构快照释放；
- 标注 manager-item 对 primary/main-image 的严格解引用边界；
- 将 owner helper 命名为带 `_guess` 的 `tTJSNI_BaseLayer_GetOwnerAddRef_guess` 并记录条件式 AddRef；
- 保存到对应 `out/ida-recovery/{android-arm64,android-armv7,ios-arm64,ios-armv7}` 恢复数据库。

## 后续审计状态

本条链之后的两项审计均已完成：

- `PrimaryLayerManagerIndex` 的 setter、三个坐标变换、slots 7–42 全部消费者和陈旧索引后果见 `motionplayer_drawdevice_primary_manager_index_consumers_stale_boundaries_four_binary_2026-08-15.md`；
- `DrawDeviceManagerItem::UpdateSettings` 的双 PropGet、默认值、串行应用、front/back 树更新与 `Show` Window gate 见 `motionplayer_drawdevice_manager_settings_property_access_show_gate_four_binary_2026-08-15.md`。
- manager item 尾部 `null/0/true` 三槽已恢复为历史 texture-lock 遗留的
  `textureBuffer/texturePitch/lastOK`；当前四份构建没有后续消费者，见
  `motionplayer_drawdevice_manager_legacy_texture_lock_tail_four_binary_2026-08-15.md`。
