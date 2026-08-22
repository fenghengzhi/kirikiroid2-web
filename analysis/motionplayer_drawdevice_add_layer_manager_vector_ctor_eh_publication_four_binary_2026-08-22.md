# MotionPlayer `AddLayerManager` vector 提交、item 构造 EH 与发布边界四二进制审计（V276）

日期：2026-08-22  
范围：`DrawDeviceObjectBase::AddLayerManager(iTVPLayerManager *)`、共享
`tTVPDrawDevice::AddLayerManager`、`tTVPLayerManager::SetHoldAlpha`、
`DrawDeviceManagerItem` 构造器，以及编译器为 item `new` 生成或省略的异常清理。  
参考：Android arm64-v8a、Android armeabi-v7a、iOS arm64、iOS armv7 四份当前
`reference/binaries/` canonical recovery database。  

## 1. 本轮结论

四份参考共同确认 `AddLayerManager` 是按阶段提交而不是事务：

```text
base Managers vector append + manager AddRef
  -> concrete manager SetHoldAlpha(false)
  -> software-renderer predicate
  -> allocate and construct software/base manager item
  -> manager SetDrawDeviceData(item)
```

任何后续失败都不撤销先前阶段。尤其：

- vector 项与对应 AddRef 在 `SetHoldAlpha` 之前就已提交；
- item 构造期间取得的 `PrimaryOwner` owning ref、main-image Fill 及已发生的属性副作用没有
  事务回滚；
- 完整 item 可以先挂入 root 的 front/back 两棵树，最后才写 manager data 单槽；
- 最终 `SetDrawDeviceData(item)` 四端都没有 cleanup。该虚调用若逃逸，item 可以已挂树但没有
  可由 manager 取回的权威指针；
- 同一 manager 可重复追加，每次追加都 AddRef。data 单槽只覆盖、不 delete 旧 item。

本轮还补出了旧报告没有记录的真实平台差异。源码层是一条普通 `new` expression，但发布出来的
四个 native 构建具有两组 EH 形态：

- Android arm64 与 iOS armv7：构造器逃逸时，构造器 landing 先清理已构造的
  `D3DLayerObject` 基部，外层 `new` landing 再 raw-delete allocation；
- Android armv7 与 iOS arm64：构造器和外层 `new` 都没有本地 cleanup landing；
- 四端最终 data-slot 虚调用均处于 cleanup 覆盖区之外。

这是编译器/EH 模型差异，不是需要用平台条件编译伪造的源级分支。当前 portable 源码继续保留
一个普通 `new` expression，只在相邻注释中记录 shipped boundary。

## 2. 四端函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| 派生 Add | `0x531770` (`0xB4`) | `0x4958C4` (`0x64`) | `0x100234174` (`0x9C`) | `0x232DCC` (`0xE8`) |
| base Add | `0xA72C14` | `0x7970B0` | `0x1002DC360` | `0x2DBD28` |
| `SetHoldAlpha` | `0x834170` | `0x64AB6C` | `0x10031B564` | `0x3205E0` |
| common item ctor | `0x53287C` | `0x496480` | `0x100234FA8` | `0x233C14` |
| Add 外层 SJLJ cleanup | — | — | — | `0x232EB4` |
| ctor 内部 SJLJ cleanup | — | — | — | `0x233E6E` |

四端共同使用的恢复名称为：

```cpp
void DrawDeviceObjectBase__AddLayerManager_guess(void *self, void *manager);
void tTVPDrawDevice__AddLayerManager(void *self, void *manager);
void tTVPLayerManager__SetHoldAlpha(void *manager, bool holdAlpha);
void *DrawDeviceManagerItem__ctor_guess(
    void *item, void *owner, void *manager);
```

iOS armv7 的两个编译器 landing 另以保守名称
`DrawDeviceObjectBase__AddLayerManager_SJLJ_cleanup_guess` 和
`DrawDeviceManagerItem__ctor_SJLJ_cleanup_guess` 标出。landing 的反编译参数是 SJLJ frame
恢复结果，不应被误当成历史源码函数签名。

## 3. base vector 的提交点

`tTVPDrawDevice` 的 manager 容器是三个连续指针组成的
`std::vector<iTVPLayerManager *>`：

| ABI | begin | end | capacity-end |
|---|---:|---:|---:|
| LP64 | 次基类 `+24` | `+32` | `+40` |
| ILP32 | 次基类 `+12` | `+16` | `+20` |

根对象中的次基类/vector 起点为：

| 目标 | `tTVPDrawDevice` 次基类 | Managers begin |
|---|---:|---:|
| Android arm64 | `+0x178` | `+0x190` |
| Android armv7 | `+0xD4` | `+0xE0` |
| iOS arm64 | `+0x118` | `+0x130` |
| iOS armv7 | `+0xA4` | `+0xB0` |

有剩余容量时，base Add 先 raw-store manager、再推进 end，随后才通过 manager vtable 调用
`AddRef`。容量耗尽时先完成新 storage 分配、旧 pointer 搬移和三个 vector pointer 的发布，最后
同样调用 `AddRef`。因此：

- 扩容/`length_error` 在新 vector 状态发布前失败，旧 vector 保持不变；
- append 一旦完成，后续 `AddRef` 逃逸也不会回退 end 或移除 pointer；
- null manager 也会先进入 vector，然后在 `AddRef` 虚调用处进入原版崩溃边界；
- 不检查 duplicate；同一 pointer 可以有多个 vector 元素和多次 AddRef；
- 派生 Add 后续所有失败都保留已提交的 vector/refcount 状态。

增长策略及 helper 映射与既有 manager 生命周期报告一致：空容量先扩到 1，之后采用满足
`size + 1` 的倍增容量；32 位/64 位 pointer 元素上限分别为 `0x3FFFFFFF` 和
`0x1FFFFFFFFFFFFFFF`。

## 4. `SetHoldAlpha(false)` 与 predicate 顺序

base Add 正常返回后，派生函数直接把 interface pointer 当具体 `tTVPLayerManager *` 调用
`SetHoldAlpha(false)`，不是旧注释中的 `SetDesiredLayerType(0)`。该函数：

1. 把 manager 的 HoldAlpha byte 写为 0；
2. 若 draw buffer 已存在，把其中 destination texture 的 HoldAlpha byte 也写为 0。

| ABI | manager DrawBuffer | manager HoldAlpha | destination HoldAlpha |
|---|---:|---:|---:|
| LP64 | `+40` | `+230` | `+96` |
| ILP32 | `+20` | `+138` | `+68` |

直到这两个状态写入之后才调用 `TVPIsSoftwareRenderManager()`。predicate 抛出或重入不会撤销
vector/AddRef 或 HoldAlpha；predicate 的结果只决定 item 的动态类型和 allocation size。

## 5. item allocation、构造和最终发布

各 ABI 的 allocation 大小为：

| 目标 | software item | base item | software cache pointer |
|---|---:|---:|---:|
| Android arm64 | `0x68` | `0x60` | `+0x60` |
| Android armv7 | `0x3C` | `0x38` | `+0x38` |
| iOS arm64 | `0x70` | `0x68` | `+0x68` |
| iOS armv7 | `0x40` | `0x3C` | `+0x3C` |

software 分支先调用 common item ctor，再安装 software derived vptr 并把 cache pointer 清零；
base 分支只有 common item。common ctor 的业务顺序为：

```text
construct D3DLayerObject/base state
store Manager
PrimaryLayer = manager->GetPrimaryLayer()
PrimaryOwner = GetOwnerAddRef(PrimaryLayer)
UpdateSettings()
mainImage = PrimaryLayer->GetMainImage()
mainImage->Fill(full image rect, ARGB 0)
SetParent_guess(owner)       // may link into both root trees
return fully constructed item
```

manager、primary layer 和 main image 都没有 null guard。owner getter 自身只在 owner 非空时
AddRef，但 `UpdateSettings` 的属性访问随后要求有效 owner。已经取得的 `PrimaryOwner` ref 不会由
item base destructor Release；这是现存 reference 行为，而不是本轮应修正的泄漏。

只有 ctor 正常返回后，外层才调用：

```text
manager->SetDrawDeviceData(item)
```

普通 concrete manager 的实现是单 pointer store。没有旧 item delete，也没有 compare/exchange、
owner local 或 scope guard。若虚调用重入后再逃逸，data 槽的最终内容仅由该虚调用在逃逸前已经
执行的 store 决定；Add frame 本身不检查或回滚。

## 6. 四端 EH 矩阵

| 目标 | ctor 内部 landing | 外层 `new` landing | final data-slot call |
|---|---|---|---|
| Android arm64 | 有；active guarded static 按需 `__cxa_guard_abort`，再调用已构造 base 的 destructor | 有；software/base 两条路径都对各自保存的 allocation 调用 raw delete 后 resume | 不在 cleanup 覆盖内 |
| Android armv7 | 无本地 cleanup；无 guard-abort、base dtor edge | 无 raw-delete landing | 不在 cleanup 覆盖内 |
| iOS arm64 | 无本地 cleanup；无 guard-abort、base dtor edge | 无 raw-delete landing | 不在 cleanup 覆盖内 |
| iOS armv7 | `0x233E6E` SJLJ switch；相关 selector abort 两个 guarded static，汇合后 base dtor、resume | `0x232EB4` SJLJ switch；两个 action arm 分别取 software/base saved allocation、raw delete、resume | call-site 先重置为 `-1`，无本地 cleanup |

iOS armv7 外层 landing 的 TBB switch 默认分支是 unexpected-selector `UDF`，不是第三条业务构造
路径。两个有效 action 对应两次 ctor call-site。ctor landing 同理，selector 分流用于判断当前
active guard；最终构造失败路径都汇合到已构造 base 的析构和 unwind resume。

Android arm64 具有与 iOS armv7 等价的 DWARF landing 语义，但不是 SJLJ switch。Android armv7
与 iOS arm64 的函数范围、LSDA/调用点和反编译 CFG 都没有对应清理 edge。若这些构建的异常实际
越过 frame，本地对象/分配不会由这里清理；若目标运行时在无 landing 的 escape 上终止，终止本身
也不会补做本地 cleanup。

即使在有 cleanup 的两端，清理也不是事务回滚：

- base destructor 不归还 `PrimaryOwner` ref；
- 已完成的 main-image Fill 不可撤销；
- 属性读取/写入副作用不可撤销；
- 若 parent attach 已完成，base destructor 会走普通 detach，尝试从 front/back 树删除当前 item；
- vector append、manager AddRef 和 HoldAlpha 仍保持提交状态。

## 7. 失败阶段矩阵

| 失败点 | vector/AddRef | HoldAlpha | allocation/base | root tree | manager data |
|---|---|---|---|---|---|
| vector growth | 未提交 | 未执行 | 无 | 无 | 旧值 |
| manager AddRef | append 已提交；ref 是否增加取决于虚调用进度 | 未执行 | 无 | 无 | 旧值 |
| `SetHoldAlpha` | 已提交 | 可部分提交 | 无 | 无 | 旧值 |
| software predicate | 已提交 | 已提交 | 无 | 无 | 旧值 |
| `operator new` | 已提交 | 已提交 | 分配失败 | 无 | 旧值 |
| ctor escape，A64/I32 | 已提交 | 已提交 | base cleanup + raw delete；外部业务副作用仍在 | attach 已完成时尝试 detach | 旧值 |
| ctor escape，A32/I64 | 已提交 | 已提交 | 无本地 base cleanup/raw delete | 可能保留部分链接 | 旧值 |
| final `SetDrawDeviceData` escape | 已提交 | 已提交 | 完整 item 保留 | 可能已经完整挂树 | 旧值/部分写入/新值 |
| 正常返回 | 已提交 | 已提交 | 完整 item | 已挂树 | 新 item；旧 pointer 被覆盖 |

这个矩阵说明不能把 Add 复原成强异常保证容器，也不能在 portable 版本里补 duplicate check、
自动 delete 旧 data、统一 rollback 或 null 容错。

## 8. 源码与旧报告落点

`cpp/plugins/DrawDeviceD3D.cpp` 保持四端共同的源级顺序：base Add、concrete HoldAlpha、predicate、
普通 `new`、final data-slot virtual。新增注释只记录 compiler-emitted EH split；没有加入会改变
portable 行为的 `try/catch`、手写 delete、平台宏或 scope guard。

既有
`analysis/motionplayer_drawdevice_manager_attach_detach_container_lifecycle_four_binary_2026-08-15.md`
已同步补充本轮平台矩阵、guard-abort/base-dtor/raw-delete 差异，以及 final publication 的无
cleanup 边界。原报告的公共 Add/Remove、duplicate、vector 和 manager-data 结论继续有效。

## 9. Wasm 验证

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两种完整
  `motionplayer-dll.cpp` syntax-only 均 exit 0；只有既有 `_tss` deprecated warning；
- Web、Wasmtime main、Wasmtime guest 均成功重编/链接，之后三个目标均
  `ninja: no work to do`；
- Web/Wasmtime 两个 CTest 目录均 exit 0，并准确报告 `No tests were found!!!`；
- 三份 Wasm 都 `WebAssembly.validate=true` 且成功构造 `WebAssembly.Module`；
- Wasmtime main 与 guest 的 `DrawDeviceObjectBase::AddLayerManager` object 反汇编完全一致：
  先 base Add，再 HoldAlpha，再 predicate；两条 allocation/ctor 路径各有当前 Wasm 编译器生成的
  raw-delete `catch_all`，final `SetDrawDeviceData` indirect call 位于两个 try 之外。

本轮只增加源码注释，因此 Web 与 Wasmtime main 的 stripped payload/hash 不变；guest debug build
的总大小和各 section 长度也不变，只有 debug metadata 内容导致 hash 改变。

| 产物 | bytes | imports/exports | SHA-256 |
|---|---:|---:|---|
| Web `index.wasm` | 85655133 | `539 / 69` | `98CF2BCB5DCB94D07B545F73DAA64C45AF4C70C1E25AA95A22B41476E40DC479` |
| Wasmtime `index.wasm` | 85002274 | `538 / 69` | `65B1F0FC0B0730D2A6634C2C2A2F0823B7488D744B7D1428F74B70C45426C65D` |
| Wasmtime guest | 151508371 | `445 / 87` | `0D73E602CBCF5B59B4D62DF92690CD841C20178A737410A32A98FD3309D5C771` |

关键 section payload：

| 产物 | FUNCTION | GLOBAL | CODE | DATA | `.debug_str` |
|---|---:|---:|---:|---:|---:|
| Web | `0x1BD30` | `0xD5C2` | `0x1A40FFF` | `0x5A3E40` | — |
| Wasmtime main | `0x1BA4F` | `0xD5EA` | `0x19E8FAD` | `0x5A1090` | — |
| guest | `0x16190` | `0xB1C3` | `0x13D7E10` | `0x4D1630` | `0x1530816` |

相对 V275，三份 Wasm 总大小均为 `+0` bytes。

## 10. recovery IDB 写回、发布和冷读

Android arm64、Android armv7、iOS arm64 各写入 11 条注释、3 个 bookmarks、4 个函数名和
4 个函数 prototype。iOS armv7 另外显式恢复两个 SJLJ landing，共写入 13 条注释、5 个
bookmarks、6 个函数名和 4 个业务函数 prototype。合计：

- comments：46；
- bookmarks：14；
- renames：18；
- function types：16；
- 写入会话强制 decompile readback：18 个业务/landing 函数；
- 最终 canonical `run_auto_analysis=true` fresh readback：18 个函数。

iOS armv7 继续采用 different-path candidate：从本轮开始时的 canonical loose/packed components
复制 `candidate-v276`，只在 candidate 写入；完成 save、close 和 fresh readback 后才显式发布
`.i64/.id0/.id1/.nam/.til`。fresh save 删除了未使用的 loose `.til`，发布前从未修改的 canonical
恢复该组件；`.id2` 在本轮开始时本来就不存在，因此没有伪造新组件。candidate 与 canonical
发布时 packed `.i64` hash 均为
`19CA566D53D8326A4BDE227EF3699437849F33D211FC1B6E60EACCC66CAC0EEA`。canonical 随后独立执行
auto-analysis/save，所以最终 packed metadata/hash 与 candidate 不同，但六个恢复目标的语义
fresh readback 全部一致。

最终 canonical IDB：

| 目标 | bytes | SHA-256 |
|---|---:|---|
| Android arm64 | 368547291 | `50D9676D8E137EBEA6B0A497D582A86A752BA4B983632CDFBC4C10C0C51C0453` |
| Android armv7 | 346739012 | `09B307F30FA596CE23E7DAD61E8C0EA101E4F12E9453A4D91AD8AE154A9D90BE` |
| iOS arm64 | 336228210 | `490D2745A169BA31174CA0A1339BDBF4C9781B7E010537053900E4B2C4A3C6EB` |
| iOS armv7 | 377171818 | `2EA104D9F5425482FFF4D2423C4FFB9EB66911631D0A05A469297EF2C325C233` |

pre-V276 backups 与 iOS candidate 位于：

`out/idb-recovery/v276-add-layer-manager-eh-publication/`

pre-backup：

| 目标 | bytes | SHA-256 |
|---|---:|---|
| Android arm64 | 368547291 | `BA542D8D037E0C82ABCD6AB1511902BD40C58869288597F8684BCD53EE5FFDF8` |
| Android armv7 | 346739012 | `E960A708E01D8CB68C9415DA38E27068681D85F7DECB58870CBD935913560C8A` |
| iOS arm64 | 336228210 | `6EE958930E15D91F854AED6300D3D8ED724C1C8BB23A75A40D316A588A1E42EE` |
| iOS armv7 | 377098090 | `DB289635FE0F48712D66F681A324301577A6A42D5575864FB45B59196BEB85BD` |

最终四库 health 均为 `auto_analysis_ready=true`、`hexrays_ready=true`；关闭后的 IDALib
session/worker 数量在收尾审计中为零。

## 11. 本轮未扩张范围

本轮闭合的是 Add 的阶段提交、vector ABI、item `new`/ctor EH 和 final data-slot publication。
`UpdateSettings` 的两个脚本属性访问、树重排和 `Show` 消费已经由既有独立报告覆盖；
`RemoveLayerManager` 的 data-first 删除、base-vector 首项删除与 stale primary index 也继续由旧报告
覆盖。本轮没有从某一端的编译器 landing 反推所有目标都必须共享相同异常实现。
