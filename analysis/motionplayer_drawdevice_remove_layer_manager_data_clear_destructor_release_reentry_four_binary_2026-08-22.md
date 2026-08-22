# MotionPlayer `RemoveLayerManager` data-first 清槽、item 析构与 Release 重入四二进制审计（V277）

日期：2026-08-22  
范围：`DrawDeviceObjectBase::RemoveLayerManager(iTVPLayerManager *)`、共享
`tTVPDrawDevice::RemoveLayerManager`、base/software manager-item 析构链、parent tree detach、
vector erase 与异常/重入部分提交。  
参考：Android arm64-v8a、Android armeabi-v7a、iOS arm64、iOS armv7 四份当前
`reference/binaries/` canonical recovery database。  

## 1. 本轮结论

四份参考共同实现的 Remove 不是 Add 的事务逆操作，而是两个先后独立提交的 teardown：

```text
item = manager->GetDrawDeviceData()
if(item) {
    manager->SetDrawDeviceData(nullptr)    // first irreversible commit
    virtual deleting_destructor(item)
}
tTVPDrawDevice::RemoveLayerManager(manager)
    find first pointer-equal entry
    missing -> internal error at DrawDevice.cpp:146
    manager->Release()                     // vector still unchanged
    erase(saved iterator)                  // shift, then --end
```

关键边界是：

- data 先清零，item 后销毁；item destructor 终止/逃逸时 base vector 不会移除；
- base missing error 在 item 成功清槽/删除之后才发生；外来 manager 的 data 也可能先被破坏；
- base `Release` 调用期间 matching element 和 end 都仍公开；Release 返回后继续使用旧 iterator，
  只重新读取 live end；
- Release callback 可以令旧 iterator 错位或 dangling，甚至让 memmove 长度从负 pointer difference
  转为巨大无符号值；没有 generation、re-find、lock 或 rollback；
- item deleting destructor 只在 complete destructor 正常返回后 raw-delete allocation；
- manager item 正常析构仍不 Release `PrimaryOwner`，不清 `Parent`/`Manager`，只 detach tree nodes、
  释放 listener list nodes，software 类型另 Release 私有 cache texture。

本轮进一步确认 destructor EH 与 V276 ctor EH 具有相同平台二分：Android arm64/iOS armv7
有 compiler cleanup + terminate，Android armv7/iOS arm64 没有本地 cleanup landing。该差异只记录
在注释/分析，不用源级平台分支伪造。

## 2. 四端入口与 ABI

### 2.1 Remove 函数

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| 派生 Remove | `0x531824` (`0x6C`) | `0x49593C` (`0x3C`) | `0x100234228` (`0x6C`) | `0x232EE8` (`0x34`) |
| base Remove | `0xA72D20` (`0x1C0`) | `0x797104` (`0x96`) | `0x1002DC3B8` (`0xEC`) | `0x2DBD58` (`0x114`) |
| iOS armv7 missing-error cleanup | — | — | — | `0x2DBE6C` |

四库应用的恢复 prototype：

```cpp
void DrawDeviceObjectBase__RemoveLayerManager_guess(
    void *self, void *manager);
void tTVPDrawDevice__RemoveLayerManager(void *self, void *manager);
```

### 2.2 manager-item vtable 与析构入口

| 目标 | software vtable | base vtable | software complete/body | software deleting | base complete/body | base deleting |
|---|---:|---:|---:|---:|---:|---:|
| A64 | `0x19FABB0` | `0x19FAC18` | `0x532B14` | `0x532B6C` | `0x533144` | `0x5331F8` |
| A32 | `0x10AAFF4` | `0x10AB028` | `0x49665C` | `0x496694` | `0x496B18` | `0x496B6C` |
| I64 | `0x101AEE818` | `0x101AEE880` | thunk `0x1002351C8`, body `0x100235790` | `0x1002351CC` | thunk `0x100235754`, body `0x10023002C` | `0x100235758` |
| I32 | `0x1839048` | `0x183907C` | thunk `0x233ED4`, body `0x234524` | `0x233ED8` | thunk `0x2344F4`, body `0x22F128` | `0x2344F8` |

parent detach helper 为 A32 `0x4922A4`、I64 `0x100230088`、I32 `0x22F1E4`；A64 在
`0x533144` complete body 内联相同逻辑。A64/I32 的
`__cxa_begin_catch -> std::terminate` helper 分别为 `0x52138C` / `0x12BA8A0`。

## 3. 派生 Remove 的严格顺序

派生入口对 manager 没有 null guard。第一次 virtual `GetDrawDeviceData` 就会解引用 manager
vptr；它的返回值只快照一次：

1. item null：完全跳过 plugin item teardown，直接进入 base Remove；
2. item non-null：先调用 `SetDrawDeviceData(nullptr)`；
3. 从已快照 item 的当前 vptr 调 slot 1 deleting destructor；
4. deleting destructor 正常返回后才 tail-call base Remove。

四端派生 Remove 均没有 cleanup landing、owner local 或重新读取 data：

- GetData 逃逸：没有任何本函数提交；
- SetData 在 store 前/后逃逸：item 不删除、base 不调用，data 保持旧值/部分状态/null 取决于虚调用
  已做的工作；
- item deleting destructor 终止/逃逸：data 已是 null，manager 仍在 vector；
- item 正常删除、base missing error：data null 且 item 已释放，随后向调用方抛 internal error；
- 任一虚调用重入并改写 data，本函数都不会重采样；仍对最初 item pointer 执行 delete。

重复 Add 后第一次 Remove 仍只通过 manager 单槽取到最新 item B。旧 item A 已失去 manager authority；
删除 B 后 base 只去掉第一个 duplicate manager。第二次 Remove 的 data 已 null，只删除另一个 vector
项；A 永久保留为 root tree orphan。这一结论与旧报告一致。

## 4. base Remove 的 find、Release 和 erase

进入 base Remove 时读取 vector begin/end，线性查找第一个 pointer-equal 元素。Android A64 只是把
四指针比较展开；另外三端是普通紧循环/helper，语义相同。missing 时构造 source path 并调用
`TVPThrowInternalError`，line 恒为 146。

找到后，顺序固定为：

```text
saved = iterator
(*saved)->Release()
liveEnd = Managers.end()                 // Release 之后重读
bytes = liveEnd - (saved + 1)
if(bytes != 0) memmove(saved, saved + 1, bytes)
Managers.end() = liveEnd - sizeof(pointer)
```

iOS libc++ 的尾部 commit 被优化成带 alignment mask 的等价算式，normal path 仍是 live end 减一
个 pointer。没有调用 element destructor，因为元素只是裸 pointer；也不清原 last slot 的旧 bits。

`Release` call-site 四端都没有 rollback cleanup。iOS armv7 虽然整个函数注册 SJLJ frame，但
`Release` 前明确把 `call_site` 写成 `-1`；唯一有效 action 只覆盖 missing-error temporary path
string 的销毁。

## 5. Release 重入矩阵

Release 时旧 matching element 和旧 end 仍可见，外层持有 raw iterator。回调返回后的行为取决于
同一个 vector 被如何修改：

| callback 修改 | 返回后的 outer Remove |
|---|---|
| 无修改 | shift 原 match 后全部元素，`--end`，正确删首项 |
| append 且 capacity 足够 | saved iterator 仍有效；新 append 也进入 live tail shift，最终保留新项 |
| append 导致 reallocation | saved iterator 指向已释放 storage；比较、memmove destination/source 均 UAF |
| erase saved 之前的元素 | shift 使 saved address 现在代表后继元素；outer 可删除错误元素 |
| erase saved 自身 | saved address 代表已移入的后继或 end；outer 再删一次/越界 |
| clear 或 shrink 到 saved+1 之前 | `liveEnd-(saved+1)` 为负，传给 memmove 时转为巨大 `size_t` |
| Release 逃逸 | outer 不执行 erase；保留 callback 已造成的状态 |

`memmove` 自身没有 callback，因此正常进入后不会再次重入。它结束后代码再次读取 live end，并将
其减一。base Remove 不调整 `PrimaryLayerManagerIndex`；重入错删会让陈旧索引边界进一步扩大。

## 6. item complete/deleting destructor

### 6.1 base `D3DLayerObject` body

base manager item 的 destructor 等同 `D3DLayerObject` destructor：

1. 把 vptr 降级为 `D3DLayerObject` base vtable；
2. 读取 `Parent`；非空时从 root FrontItems 与 BackItems 各 erase 一个 matching pointer；
3. 任一 erase 成功才调用 base-stage `OnDetached()` 和 `Parent->OnItemsChanged()`；
4. 释放 listener list 的 node allocations；
5. 返回 item pointer/void ABI，供 deleting wrapper 继续 raw delete。

析构不：

- 把 `Parent` 写 null；
- Release `PrimaryOwner`；
- 清 `Manager`、`PrimaryLayer`、legacy texture tail；
- delete/Release listener payload pointer；
- 查询或清 borrowed NCB adaptor；
- 从 manager data 槽再次验证自己仍是 current item。

因此正常 Remove 的安全性完全依赖“先清 manager data，再删除 item”的外层顺序；借用 adaptor 和
任何外部 raw pointer 仍可在 item 释放后悬空。

### 6.2 software 派生 body

software item 先安装 software vptr、读取末尾 private cache：cache 非空时调用 texture vslot 2
`Release`，不先清字段；随后进入同一个 base destructor。它也不 Release construction-time
`PrimaryOwner` owning ref。

deleting destructor 对 base/software 都只做：

```text
complete_destructor(item)
operator delete(item)          // only after normal return
```

所以 complete destructor 终止或越过 frame时，raw item allocation 不会释放。

## 7. destructor EH 平台矩阵

| 目标 | software cache Release escape | base detach/hook escape | deleting raw delete |
|---|---|---|---|
| Android arm64 | cleanup 调 base dtor，再 `clang_call_terminate` | cleanup 先 clear listener nodes，再 terminate | 不执行 |
| Android armv7 | 无本地 landing；base dtor 被跳过 | 无本地 landing | 不执行 |
| iOS arm64 | 无本地 landing；base dtor 被跳过 | 无本地 landing | 不执行 |
| iOS armv7 | SJLJ `0x2345B4` 调 base cleanup thunk，再 terminate | SJLJ `0x22F1BA` clear list nodes，再 terminate | 不执行 |

A64/I32 terminate helper 都是 `__cxa_begin_catch(exception); std::terminate();`。I32 landing 的 TBB
default `UDF` 是 unexpected selector，不是业务分支。base destructor landing 只在 detach call-site
有效；software landing 只在 cache Release call-site 有效。

A32/I64 二进制没有相应 local landing。若异常实际越过这些 frame，当前函数不会补 base/list/raw
cleanup；若 ABI/runtime 在别处终止，也不能据此声称本 frame 已完成清理。portable 源码保留普通
C++ destructor，让当前编译器按自己的 EH 模型生成代码，不用平台宏模拟旧编译器产物。

## 8. 失败后的对象图

| 失败点 | manager data | item allocation/tree | manager vector/ref |
|---|---|---|---|
| GetData | 未知/旧值 | 未触及 | 未触及 |
| SetData | 旧值/部分/null | 原 item 保留 | 未触及 |
| software cache Release，A64/I32 | 已 null | base detach 后 terminate；raw allocation 未删 | manager 仍在 vector |
| software cache Release，A32/I64 | 已 null | base cleanup 未执行；raw allocation 未删 | manager 仍在 vector |
| base detach escape，A64/I32 | 已 null | listener nodes 清理后 terminate；raw allocation 未删 | manager 仍在 vector |
| base detach escape，A32/I64 | 已 null | 部分 detach/list/allocation 保留 | manager 仍在 vector |
| base missing | 已 null | item 已正常删除 | vector 不变，抛 internal error |
| manager Release escape | 已 null | item 已正常删除 | matching entry 仍在 vector，ref side effect 取决于 Release 进度 |
| normal | null | current item 删除 | first matching entry 删除并 Release 一次 |

## 9. 源码和旧报告修订

- `cpp/plugins/DrawDeviceD3D.cpp`：在 `D3DLayerObject`/software item destructor 和派生 Remove
  增加四端 EH、data-first、无 rollback 与 base-tail 注释；执行语句不变；
- `cpp/core/visual/impl/DrawDevice.cpp`：在共享 base Remove 的 Release-before-erase 处记录 saved
  iterator/live end 重入边界；原 `find -> Release -> erase` 语句不变；
- `analysis/motionplayer_drawdevice_manager_attach_detach_container_lifecycle_four_binary_2026-08-15.md`：
  补充 V277 Remove/Release/析构 EH 小节；
- `analysis/motionplayer_d3dlayer_object_listener_container_lifecycle_four_binary_2026-08-15.md`：补充
  base/software destructor cleanup 平台矩阵。

源码和测试中本轮 native 入口地址匹配数为 0；平台绝对地址只保留在 `analysis/` 与 recovery IDB。

## 10. 构建、Wasm 与定向反汇编

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两种完整
  `motionplayer-dll.cpp` syntax-only 均 exit 0；只有既有 `_tss` deprecated warning；
- Web、Wasmtime main、Wasmtime guest 均成功重编/链接；随后三个目标均
  `ninja: no work to do`；
- 两个 CTest 目录 exit 0，并准确报告 `No tests were found!!!`；
- 三份 Wasm 均 `WebAssembly.validate=true` 并成功构造 Module，imports/exports 不变；
- Wasmtime main/guest plugin object 的派生 Remove 完全一致：GetData -> null gate -> SetData(0)
  -> item vslot 1 -> base Remove，整个序列没有 catch；
- shared core object 的 base Remove 只在 missing-error temporary 周围有 try/catch cleanup；manager
  vslot Release 位于该 try 之后，最后才调用 vector erase helper。

本轮只有注释变化，因此 stripped Web/main payload 与 V276 相同；guest 的总大小和 section 长度也
相同，仅 debug metadata 内容改变 hash。

| 产物 | bytes | imports/exports | SHA-256 |
|---|---:|---:|---|
| Web `index.wasm` | 85655133 | `539 / 69` | `98CF2BCB5DCB94D07B545F73DAA64C45AF4C70C1E25AA95A22B41476E40DC479` |
| Wasmtime `index.wasm` | 85002274 | `538 / 69` | `65B1F0FC0B0730D2A6634C2C2A2F0823B7488D744B7D1428F74B70C45426C65D` |
| Wasmtime guest | 151508371 | `445 / 87` | `C2AAF9D1630175AD5AA7D75CE4F173F522B2F2BB81A657E3D9D9E0324D333508` |

| 产物 | FUNCTION | GLOBAL | CODE | DATA | `.debug_str` |
|---|---:|---:|---:|---:|---:|
| Web | `0x1BD30` | `0xD5C2` | `0x1A40FFF` | `0x5A3E40` | — |
| Wasmtime main | `0x1BA4F` | `0xD5EA` | `0x19E8FAD` | `0x5A1090` | — |
| guest | `0x16190` | `0xB1C3` | `0x13D7E10` | `0x4D1630` | `0x1530816` |

相对 V276，三份总大小均为 `+0` bytes。

## 11. recovery IDB 写回与发布

本轮写回：

| 目标 | comments | bookmarks | renames | function types | write-session readbacks |
|---|---:|---:|---:|---:|---:|
| Android arm64 | 19 | 5 | 7 | 7 | 7 |
| Android armv7 | 17 | 5 | 7 | 7 | 7 |
| iOS arm64 | 19 | 5 | 9 | 9 | 9 |
| iOS armv7 | 24 | 7 | 14 | 11 | 14 |
| 合计 | 79 | 22 | 37 | 34 | 37 |

iOS armv7 另对 candidate 做 14 函数 fresh readback，发布后 canonical 独立
`run_auto_analysis=true -> save -> close`；最终四库又各自 cold-open，共回读 37 个函数。candidate
只从本轮开始时的 authoritative canonical 复制 `.i64/.id0/.id1/.nam/.til`；save 删除未使用的
loose `.til` 后，从未修改的 canonical 恢复该组件。发布时 packed candidate/canonical 均为
`E78CE43083BCC1B72C31175C86FF20F8710851C5C73BBD95373184C4DCFC0A28`；最终 canonical 独立
auto-save 后 metadata/hash 不同，但全部语义 readback 一致。

最终 canonical IDB：

| 目标 | bytes | SHA-256 |
|---|---:|---|
| Android arm64 | 368547291 | `690A7B48E5D1522A5C67D26E4FE6583049F666920DB1694FFD857B0B5AB96875` |
| Android armv7 | 346739012 | `A3058CFB6DB5C15435A6CFBEE889E92C88C6DF5FE830183F92F11E407976307B` |
| iOS arm64 | 336228210 | `C605FAEA624F2BFDC6AA63950FD16905CB1468712C2FF6D6087DDDB21F8944EA` |
| iOS armv7 | 377376618 | `C3259F7ADB87DC8061BE31613D99D9F473F44EBF5A6965FF39A839FCBC50952E` |

pre-V277 backups 与 iOS candidate：

`out/idb-recovery/v277-remove-layer-manager-release-destructor/`

| 目标 | pre-V277 bytes | pre-V277 SHA-256 |
|---|---:|---|
| Android arm64 | 368547291 | `50D9676D8E137EBEA6B0A497D582A86A752BA4B983632CDFBC4C10C0C51C0453` |
| Android armv7 | 346739012 | `09B307F30FA596CE23E7DAD61E8C0EA101E4F12E9453A4D91AD8AE154A9D90BE` |
| iOS arm64 | 336228210 | `490D2745A169BA31174CA0A1339BDBF4C9781B7E010537053900E4B2C4A3C6EB` |
| iOS armv7 | 377171818 | `2EA104D9F5425482FFF4D2423C4FFB9EB66911631D0A05A469297EF2C325C233` |

最终四库均 `auto_analysis_ready=true`、`hexrays_ready=true`；收尾 session/worker 和独立
IDA GUI/batch process 审计为零。

## 12. 本轮未扩张范围

本轮没有假设任意自定义 manager 的 `Get/SetDrawDeviceData` 或 `Release` 必然不抛/不重入；报告
刻意保留虚调用可观察边界。manager 最终 Release 是否销毁其具体 draw buffer、window 在什么外层
顺序触发 Remove，以及 vector 已损坏后的任意 UB 结果，属于 owner/manager teardown 的后续纵切面，
不由本函数反推。
