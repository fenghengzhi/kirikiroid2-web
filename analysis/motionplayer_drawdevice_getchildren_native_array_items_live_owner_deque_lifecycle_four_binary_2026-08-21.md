# MotionPlayer DrawDevice getChildren native Array Items、live owner 与 deque 生命周期四参考闭环（V270）

## 1. 结论

V270 沿 V269 的 `FrontItems` live tree 审计只读属性 `children` 的业务体。四份当前
`reference/binaries/` 一致证明，参考实现不是逐项调用 script Array setter，而是：

1. 构造一个 owning `ncbArrayAccessor`；
2. 对 fresh Array 调 `NativeInstanceSupport(TJS_NIS_GETINSTANCE, TJSGetArrayClassID(), ...)`；
3. 取得 borrowed `tTJSArrayNI::Items`；
4. 按 live `FrontItems` 顺序检查 child 的 script owner；
5. 仅当 `owner != nullptr && owner->IsValid(...) == TJS_S_TRUE` 时，直接执行
   `Items.emplace_back(child->ScriptOwner, child->ScriptOwner)`；
6. 构造 `tTJSVariant(array,array)` 返回，再由 accessor destructor 释放 factory ref。

旧 portable 用 `TJSCreateArrayObject` 加 `PropSetByNum`。那会引入 reference 中不存在的 script
numeric setter、flag/index/error-code、可能的 overridden dispatch 与不同异常表面。本轮已恢复成
native deque 路径，并锁定 duplicate closure 的 Object/ObjThis identity。

## 2. 四端函数地图

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| root `getChildren` | `0x529418` | `0x491DA0` | `0x10022FAC0` | `0x22EC24` |
| accessor + native Items acquisition | caller 内联 | `0x491E84` | `0x10021AB44` | `0x219A2C` |
| deque fast/general emplace | caller 内联 | `0x491EFC` | `0x10022FC34` | `0x22EDB0` |
| deque slow block emplace | `0x534454` | `0x497D08` | general helper 内 | general helper 内 |
| getChildren unwind cleanup | caller landing | zero-cost EH | `0x10022FBFC` | `0x22ED4E` SJLJ |
| accessor acquisition unwind | caller landing | zero-cost EH | zero-cost EH | `0x219AD8` SJLJ |

`getChildren` 是按 ABI 返回 20/12-byte packed `tTJSVariant` 的函数；LP64 调用约定使用隐藏
result register/slot，ILP32 把 result pointer 作为首 machine argument。IDB 的
`tTJSVariant_guess` 只表达已证实的 `{Object,ObjThis,Type}` 布局，不声称恢复全部原始类型名。

## 3. 共同 source shape

四端共同语义可归纳为：

```cpp
tTJSVariant getChildren() const {
    ncbArrayAccessor array;
    tTJSArrayNI *native; // no fallback initialization
    (void)array.GetDispatch()->NativeInstanceSupport(
        TJS_NIS_GETINSTANCE,
        TJSGetArrayClassID(),
        reinterpret_cast<iTJSNativeInstance **>(&native));

    for(D3DLayerObject *child : FrontItems) {
        if(!child->ScriptOwner ||
           child->ScriptOwner->IsValid(
               0, nullptr, nullptr, child->ScriptOwner) != TJS_S_TRUE)
            continue;
        native->Items.emplace_back(
            child->ScriptOwner, child->ScriptOwner);
    }

    iTJSDispatch2 *dispatch = array.GetDispatch();
    return tTJSVariant(dispatch, dispatch);
}
```

这里保留 `ScriptOwner` field lvalue 非偶然：optimizer 给 deque helper 的不是提前缓存的两个 pointer
值，而是同一个 field 地址两次。后续章节说明其重入边界。

## 4. fresh Array 与 borrowed Items 获取

四端 accessor acquisition 都执行：

```text
dispatch = TJSCreateArrayObject(nullptr)
accessor.vptr = ncbArrayAccessor/ncbPropAccessor vptr
accessor._obj = dispatch                // addref=false, takes factory ref
nativeSlot = uninitialized
dispatch->NativeInstanceSupport(
    2, TJSGetArrayClassID(), &nativeSlot)
items = &nativeSlot->Items
```

关键边界：

- 没有 fresh Array null guard；null 会在 vtable load 自然崩溃；
- `NativeInstanceSupport` 的普通 `tjs_error` 返回值完全忽略；
- output slot 不先写 null，也不按成功/失败选择 fallback；非成功且未写 output 时，随后 Items
  pointer 来自未初始化值；
- native instance 不单独 AddRef，Items 只在 accessor/返回 closure 保持 Array 存活时有效；
- Items subobject 相对 native instance 是 LP64 `+0x10`、ILP32 `+0x08`；
- iOS armv7 constructor helper在 accessor 已构造后为 class-ID getter 和 NativeInstanceSupport
  设置 SJLJ call-site 1/2；任一抛出都调用 `ncbPropAccessor` destructor Release Array 后 resume。

fresh core Array 的正常路径必然提供正确 native instance，但原版仍保留上述 sharp failure 行为，
portable 不能擅自改成 `status == TJS_S_OK ? Items : nullptr` 的安全协议。

## 5. owner snapshot、live field 与 exact IsValid

每个 tree node 的顺序是：

```text
child = current.payload
ownerSnapshot = child.ScriptOwner
if ownerSnapshot != null:
    status = ownerSnapshot.IsValid(0,null,null,null,ownerSnapshot)
    if status == 1:
        Items.emplace_back(&child.ScriptOwner, &child.ScriptOwner)
current = tree_successor(current)
```

四端都只接受 exact `TJS_S_TRUE == 1`：`TJS_S_OK == 0`、其他 positive status 和任何 failure
都跳过。`IsValid` 的四个 call arguments/slots都是零/null，`objthis` 是调用前 snapshot。

但 append 不是用 snapshot：

- Android arm64 在 `0x529498` 保留 `child+0x08` 地址，`0x5294D8` 回调后重读；
- Android armv7 在 `0x491DC8` 保留 `child+0x04`，`0x491EFC` helper 再解引用；
- iOS arm64 在 `0x10022FAF4` 保留 `child+0x08`，`0x10022FCD0` 再解引用；
- iOS armv7 在 `0x22EC80` 保留 `child+0x04`，`0x22EE30` 再解引用。

所以若 native/reentrant side effect 在 `IsValid` 返回 1 前把 field 从 P 改成 Q，Array 追加的是未经
`IsValid` 验证的 Q/Q closure；改成 null 则仍追加 tag 为 `tvtObject`、两个 pointer 都为 null 的
closure。若 side effect 销毁 child，append 重读 field 本身就已经 UAF。

## 6. live tree successor 与 mutation

与 V269 一致，successor 只在 `IsValid` 和可能的 deque append 完成后计算。容器布局仍是：

| 目标 | first | sentinel | payload |
|---|---:|---:|---:|
| Android arm64 | root `+0x60` | root `+0x50` | node `+0x20` |
| Android armv7 | root `+0x38` | root `+0x30` | node `+0x10` |
| iOS arm64 | root `+0x48` | root `+0x50` | node `+0x20` |
| iOS armv7 | root `+0x2C` | root `+0x30` | node `+0x10` |

因此：

- duplicate node 不去重，每个 valid node 都追加一次 closure；
- `IsValid` 删除 future node 或插入尚未经过位置会改变本轮后续路径；
- `IsValid` 删除 current node后，post-callback successor 以 freed node 为输入，UAF/UB；
- `IsValid` 改 current front index时公开 setter会 erase/reinsert current，同样使旧 cursor 悬空；
- root/tree 被销毁时 current/sentinel 全失效；
- 没有 snapshot vector、next prefetch、mutation generation、reentrancy guard 或 deferred erase。

## 7. native deque 内部实现与 publication

Array `Items` 在四份 reference 中都是 `std::deque<tTJSVariant>`；Android 是 libstdc++ lowering，
iOS 是 libc++ lowering。实际 element 是 packed 20/12 bytes：

| 目标族 | element | elements/block | block bytes | size commit |
|---|---:|---:|---:|---|
| Android LP64 libstdc++ | `0x14` | 25 | 500 | finish cursor `+0x14` / slow node switch |
| Android ILP32 libstdc++ | `0x0C` | 42 | 504 | finish cursor `+0x0C` / slow node switch |
| iOS LP64 libc++ | `0x14` | 204 | 4080 payload | deque count `+1` |
| iOS ILP32 libc++ | `0x0C` | 341 | 4092 payload | deque count `+1` |

共同 element construction 顺序：

```text
ensure map/block capacity if needed
dst = next raw element slot
object  = *forwardedObjectField
objthis = *forwardedObjThisField
if object:  object.AddRef()
if objthis: objthis.AddRef()
dst.Type = tvtObject
dst.Object = object
dst.ObjThis = objthis
commit deque finish/size
```

两个 forwarded address 相同，但 helper 仍独立解引用、独立 AddRef。allocation/map grow发生在重读
owner 与 AddRef 之前；allocation failure不发布 element、不增加 size。fast path只有两次 AddRef
之后的 raw stores。Android arm64 slow path还明确在新 block element construction 抛出时 delete
该 block并 rethrow，finish node不提交。

不存在：

- `PropSetByNum` / `FuncCall("add")`；
- numeric index 或 `TJS_MEMBERENSURE`；
- setter hint/cache；
- setter override/script callback；
- 对 setter `tjs_error` 的忽略或检查；
- per-element temporary Variant 的独立 copy/dtor。

## 8. 返回 closure 与引用计数

正常路径的引用计数协议：

1. `TJSCreateArrayObject` 交出一个 factory ref；
2. `ncbArrayAccessor(..., addref=false)` 接管该 ref，不增加；
3. 每个 admitted child element 对 live owner 执行 Object/ObjThis 两次 AddRef；
4. 返回 `tTJSVariant(array,array)` 对 Array执行两次 AddRef；
5. accessor destructor 对 Array执行一次 Release，消费 factory ref；
6. caller 得到持有两个 closure ref 的 `tvtObject` Variant。

Array为空时返回的仍是有效 Array Object closure；没有 null/void special case。duplicate tree node使同一个
owner重复两次 element construction，每个 element各自持有两 refs。Array销毁时 native deque 按元素
析构，归还这些 child refs。

## 9. 异常矩阵

accessor 成功构造后，`IsValid`、deque allocation/append、返回 closure AddRef 中任一步抛出，都会：

```text
destroy ncbArrayAccessor
    -> Release fresh Array
       -> destroy every already committed Items Variant
resume original exception
```

iOS armv7 提供最显式的 SJLJ 证明：

| source operation | call_site | dispatcher |
|---|---:|---|
| returned Array Object AddRef | 1 | accessor cleanup |
| returned Array ObjThis AddRef | 2 | accessor cleanup |
| child owner IsValid | 3 | accessor cleanup |
| deque emplace | 4 | accessor cleanup |
| normal accessor Release | 5 | terminate case if it throws |
| accessor Release while unwinding | 6 | terminate case if it throws |

call-sites 1..4 在 `0x22ED7E` 汇合 release Array，随后 `0x22ED96` resume；5/6 进入 terminate
helper。iOS arm64 的 split landing `0x10022FBFC` 做同一 Release/resume。Android 采用 zero-cost EH；
A64 listing 同样可见 Array Release landing及 release-while-unwinding terminate thunk。

已完成的 Items append 会随 Array destruction回滚其 refcount/element lifetime；`IsValid` 或其他
外部 callback 已产生的副作用不回滚。普通非 1 `tjs_error` 只是过滤结果，不走异常 cleanup。

## 10. portable 修复与回归

`cpp/plugins/DrawDeviceD3D.cpp`：

- 删除 fresh Array 上逐项 `PropSetByNum(TJS_MEMBERENSURE,index,...)`；
- 改用 `ncbArrayAccessor`；
- 忽略 fresh Array `NativeInstanceSupport` 普通 status并直接取得 native `Items`；
- 直接 `Items.emplace_back(child->ScriptOwner,child->ScriptOwner)`，保留 field-lvalue重读；
- 补 owner snapshot/live field、exact true、异常 owner与无 fallback的精确注释。

`tests/unit-tests/plugins/motionplayer-dll.cpp`：

- 在 duplicate FrontItems 场景读取 `children`；
- 直接取得返回 Array native `Items`；
- 断言 size 为 2，两个元素都为 `tvtObject`；
- 断言每个元素的 Object 与 ObjThis 都是同一个 live D3DLayer script owner。

## 11. Recovery IDB 写回

四库合计：

- 131 条 function/line comment；
- 14 个 bookmark；
- 13 个 `_guess`/语义 function rename；
- 9 个 function type/prototype update；
- 13 次定向 force-recompile/readback；
- iOS armv7 新增一个 packed `tTJSVariant_guess` local type declaration。

| 目标 | comments | bookmarks | renames/types | force readback | final bytes | final SHA-256 |
|---|---:|---:|---:|---:|---:|---|
| Android arm64 | 33 | 3 | 2 / 2 | 2 | 368547676 | `F3E2AE3F6959EEF834AD61507F1B420D420DD0735D4640BBF244F77A1D1ABA7A` |
| Android armv7 | 29 | 3 | 3 / 3 | 3 | 346744394 | `469BEBBE7ED8D657937525B0851E1EE3E8DC941C04C2159DD2D0CB3EE4CA3FDC` |
| iOS arm64 | 29 | 3 | 3 / 2 | 3 | 336228262 | `F8EFC7BD980726A8F373B7B2D8455FA0B1A4AA7968B42D5E4005C1D68BBEF9E4` |
| iOS armv7 | 40 | 5 | 5 / 2 | 5 | 376967070 | `AFE79C5B25018C27AF60356BFA8F51AA9A92067274959B6615C5434B7DA5E495` |

iOS armv7 按 pre-backup → candidate → pre `idat -A` → edits/force/fresh decompile → post
`idat -A` → canonical publish → canonical fresh readback 保存。最后四个 canonical 依次再次通过
`C:\IDA\idat.exe -A`；candidate从最终 canonical重同步，size/hash逐字节一致。

## 12. 构建与 Wasm 证据

验证结果：

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两种完整 `motionplayer-dll.cpp` syntax-only
  均 exit 0，只有既有 `_tss` warning；
- Web 3/3、Wasmtime 4/4、Wasmtime guest 1/1 成功；
- 三目标随后顺序复跑均为 `ninja: no work to do`；
- Web/Wasmtime 的 `ctest --output-on-failure` 都 exit 0，但准确报告
  `No tests were found!!!`，所以新增 Catch2 assertions 只能称 syntax-validated；
- 定向 `llvm-nm` 保留 `DrawDeviceObjectBase::getChildren() const`；
- Wasmtime `llvm-objdump --disassemble-symbols` 显示 fresh accessor/NativeInstanceSupport、exact
  IsValid比较、把 `child+4` 同一 field address 两次传给 deque helper、catch中 accessor cleanup；
  不再有 `PropSetByNum` call path。
- `git diff --check` exit 0，仅有工作树既有 LF→CRLF conversion warning；
- 本轮 compiled source/test 行范围中的 reference absolute-address 扫描为 0 命中；
- `mcp__idalib__idb_list` 为 `sessions=[]`, `count=0`，也没有残留
  `ida`/`ida64`/`idat`/`idat64`/`idalib-worker` 进程。

最终 Wasm：

| 产物 | bytes | SHA-256 |
|---|---:|---|
| Web `index.wasm` | 85655068 | `C3E6C208C560E2987E565DE9E3A9B38ECB66DEFD10F9B1935EAFCB48AA26C6B0` |
| Wasmtime `index.wasm` | 85002209 | `E9A1A10E78DF73BADB708D871F108C034E9D895349AFF61C7B3AF898C4168618` |
| Wasmtime guest | 151508078 | `91A00983F2925F18FE6876F3E742C789A08972DB5A43AC238BA9C94AD54A84EC` |

相对 V269：

| 产物 | total | FUNCTION | CODE | name | 其他变化 |
|---|---:|---:|---:|---:|---|
| Web | `-194` | `-1` | `-0x97` | `-0x2A` | DATA及其余payload size不变 |
| Wasmtime | `-194` | `-1` | `-0x97` | `-0x2A` | DATA及其余payload size不变 |
| guest | `+29044` | `+2` | `+0x26` | `+0x85` | GLOBAL `+8`、ELEM `+4`、debug合计扩大 |

guest 的 `+29044` 恰由 objdump 已列 section delta组成：`.debug_abbrev +32`、
`.debug_info +14098`、`.debug_ranges +952`、`.debug_str +200`、`.debug_line +13577`、
FUNCTION/GLOBAL/ELEM/CODE/name 合计 `+185`，`.debug_loc` 不变。两个主产物对称减少 194 bytes，
其中 CODE 各减 151 bytes，与删除 script numeric setter路径一致，是有意 executable fidelity修复。

## 13. 闭合范围

V270 已闭合 `getChildren` 的 fresh Array factory ref、native instance/Items acquisition、exact validity
过滤、live owner lvalue identity、duplicate behavior、deque ABI/block geometry、element publication、返回
closure refcount、tree mutation/UAF 与异常 cleanup。

这不表示 motionplayer 全目标完成；下一纵切面继续沿尚未闭合的调用链、容器、owner 与边界行为推进。

同一root的第二个Array producer `getPrimaryLayers` 在V271确认复用native Items路径，但producer
是snapshot Managers vector与strict primary-layer chain；其old owning owner getter还额外AddRef一次
且永不Release，形成每manager每call `+1` leak。完整证据见
`motionplayer_drawdevice_getprimarylayers_native_array_manager_snapshot_owner_ref_leak_four_binary_2026-08-21.md`。
