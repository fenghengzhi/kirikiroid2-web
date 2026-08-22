# MotionPlayer DrawDevice getPrimaryLayers native Array、manager snapshot 与 owner ref leak 四参考闭环（V271）

## 1. 结论

V271 审计 root `primaryLayers` getter，发现 V270 `getChildren` 的 native Array 偏差在这里也存在，
并额外恢复一条四端一致的永久引用泄漏：

- 构造 `ncbArrayAccessor`，忽略 fresh Array `NativeInstanceSupport` 普通 status，直接取得
  `tTJSArrayNI::Items`；
- 在入口 snapshot `Managers` vector 的 begin/end，按保存的 raw range遍历；
- 每项严格调用 `manager->GetPrimaryLayer()`，不检查 manager/layer null；
- 调用旧 `tTJSNI_BaseLayer` owner getter：读取 owner field，非 null时先 AddRef一次；
- 把这个 cached owner pointer作为 Object/ObjThis直接 emplace到 native deque；deque Variant再
  AddRef两次；
- owning getter 的第一份 ref从不 Release：无论正常返回还是后续 append抛出，每个已经成功取得
  owner 的 manager都净泄漏一 ref；
- 返回 Array closure仍遵循两次 AddRef、accessor消费factory ref的协议。

旧 portable 用 `GetOwnerNoAddRef` + temporary Variant + `PropSetByNum`，既缺少原生 getter leak，
又引入不存在的 script numeric setter/error surface。两处都已修正。

## 2. 四端函数地图

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| root `getPrimaryLayers` | `0x52A2B4` | `0x4925AC` | `0x1002304FC` | `0x22F430` |
| owning BaseLayer owner getter | `0x8333B4` | `0x64A290` | `0x100096FC8` | `0x95530` |
| accessor + Items acquisition | caller 内联 | `0x491E84` | `0x10021AB44` | `0x219A2C` |
| deque emplace | inline/`0x534454` slow | `0x491EFC`/`0x497D08` | `0x10022FC34` | `0x22EDB0` |
| getter unwind | caller landing | zero-cost EH | `0x1002305D8` | `0x22F52A` SJLJ |

accessor/deque helpers已在 V270闭合；本轮新证据是 Managers producer、strict primary-layer chain、
owning getter及raw getter ref不在cleanup中的事实。

## 3. 共同 source shape

```cpp
tTJSVariant getPrimaryLayers() const {
    ncbArrayAccessor array;
    tTJSArrayNI *native;
    (void)array.GetDispatch()->NativeInstanceSupport(
        TJS_NIS_GETINSTANCE,
        TJSGetArrayClassID(),
        reinterpret_cast<iTJSNativeInstance **>(&native));

    for(iTVPLayerManager *manager : Managers) {
        tTJSNI_BaseLayer *layer = manager->GetPrimaryLayer();
        iTJSDispatch2 *owner = layer->GetOwnerNoAddRef();
        if(owner)
            owner->AddRef();       // old owning getter ref: deliberately leaked
        native->Items.emplace_back(owner, owner);
    }

    iTJSDispatch2 *dispatch = array.GetDispatch();
    return tTJSVariant(dispatch, dispatch);
}
```

portable 使用现有 `GetOwnerNoAddRef` 加显式 AddRef来表达旧二进制中的 owning getter。不能在 append
后补 Release：四端均不存在该操作，且异常 dispatcher也不认识 raw owner local。

## 4. Managers vector snapshot 与 mutation

四端在首个 manager callback前一次性加载 begin/end：

| 目标 | vector begin | vector end | element |
|---|---:|---:|---:|
| Android arm64 | root `+0x190` | root `+0x198` | 8-byte manager pointer |
| Android armv7 | root `+0xE0` | root `+0xE4` | 4-byte manager pointer |
| iOS arm64 | root `+0x130` | root `+0x138` | 8-byte manager pointer |
| iOS armv7 | root `+0xB0` | root `+0xB4` | 4-byte manager pointer |

等价于 C++ range-for 的 iterator/end snapshot，而不是每轮重新读取 `Managers.end()`：

- callback在capacity内修改尚未访问的现有slot，后续raw slot load可见新 pointer；
- append且不reallocate：新元素位于保存end之后，本轮不可见；
- append触发reallocation：保存begin/current/end全部悬空，返回后UAF；
- erase/clear/reallocation同样使current/end失效；
- 删除或销毁当前manager后，当前调用已取得的layer/owner副作用不回滚；
- 没有 manager null guard、snapshot copy、generation check、reentrancy guard或deferred mutation。

`GetPrimaryLayer` 是 manager vtable LP64 `+0x28`、ILP32 `+0x14` 的虚调用。manager null在vptr load
崩溃；返回layer null时，下一 owning getter在layer field offset处崩溃。

## 5. owning owner getter

四端 helper body完全同构：

```text
owner = layer.Owner                 // LP64 +0x18, ILP32 +0x0C
if owner != null:
    owner.AddRef()
return owner
```

它不是当前 portable API名称所暗示的 `GetOwnerNoAddRef`：reference helper确实调用 owner vtable
AddRef一次。caller把返回 pointer写入一个raw stack local；没有 `tTJSVariant`、smart pointer、scope
guard或尾部 Release来拥有这份 ref。

null边界：

- layer null：strict dereference崩溃，不产生Array element；
- layer非null、owner null：helper返回null且不AddRef，仍继续 append；
- null owner element仍写 `tvtObject` tag，Object/ObjThis都是null；
- non-null owner在append前已经净增一ref。

owner pointer在 helper返回后缓存到stack；这与 V270 `getChildren` forwarding live field不同。后续
side effect改变layer owner field不会改变本element使用的cached pointer。

## 6. direct native deque 与 refcount ledger

V270 已确认的 direct Items emplace在这里原样复用：无 `PropSetByNum`、numeric index、flag、hint、
script setter或setter `tjs_error`。每个 non-null owner 的正常ledger：

| 阶段 | owner ref delta |
|---|---:|
| owning BaseLayer getter | `+1` |
| deque `tTJSVariant(owner,owner)` Object AddRef | `+1` |
| deque Variant ObjThis AddRef | `+1` |
| returned Array后续销毁该element | `-2` |
| 永久净变化 | `+1` |

Array自身ledger独立：factory `+1`由accessor接管；返回closure对Array `+2`；accessor destructor
`-1`；returned Variant最终持有并归还两份closure ref。

所以每次读取 `primaryLayers` 都按当时snapshot中的每个non-null owner永久泄漏一ref。重复 manager
产生重复Array elements，也产生重复leak；调用者是否保存/清空返回Array不影响这份getter leak。

## 7. publication 与异常边界

每个 manager 的操作顺序：

```text
manager = *cursor
layer = manager.GetPrimaryLayer()
owner = layer.GetOwner_AddRef()          // +1 raw ref
Items.emplace_back(owner, owner)         // +2, then element publication
++cursor
```

deque内部仍是 V270 的 allocation/map-grow → 读取cached local → 双AddRef → raw pointer/tag store →
finish/size commit。若 allocation在owning getter成功后抛出，element未发布、Array无该closure refs，
但getter `+1` 已泄漏。

body异常 cleanup只做：

```text
accessor.~ncbArrayAccessor()
    -> Array.Release()
       -> destroy already committed Items (-2 each)
resume original exception
```

raw owner local不在 cleanup中。iOS armv7 SJLJ最明确：

| operation | call_site | outcome on throw |
|---|---:|---|
| returned Array Object AddRef | 1 | Array cleanup |
| returned Array ObjThis AddRef | 2 | Array cleanup |
| manager GetPrimaryLayer | 3 | Array cleanup |
| owning owner getter | 4 | Array cleanup；getter未返回则无新raw ref |
| deque emplace | 5 | Array cleanup；已返回的getter `+1` 泄漏 |
| normal accessor Release | 6 | Release再抛进入terminate |
| accessor Release during unwind | 7 | Release再抛进入terminate |

dispatcher `0x22F52A` 的call-sites 1..5只读取accessor Array dispatch；没有读取cached owner slot。
iOS arm64 split landing `0x1002305D8`同样只Release Array。Android A64 landing也没有owner Release。

## 8. portable修复与回归

`cpp/plugins/DrawDeviceD3D.cpp`：

- `getPrimaryLayers`从 `TJSCreateArrayObject + PropSetByNum` 改为
  `ncbArrayAccessor + native Items.emplace_back`；
- 保留 fresh Array NativeInstanceSupport status忽略和无fallback output；
- 保留 manager range snapshot的range-for形状；
- primary layer继续strict dereference；
- 用 `GetOwnerNoAddRef` 后显式 `AddRef` 模拟旧owning getter；
- 故意不Release该ref，恢复每manager每call `+1` leak；
- null owner仍直接生成tagged-null Object closure。

`tests/unit-tests/plugins/motionplayer-dll.cpp`：

- 在真实registered primary manager场景读取 `primaryLayers`；
- 断言native Items size=1、element为`tvtObject`，Object/ObjThis都等于primary script owner；
- 清空返回Array后重新探测owner refcount，断言净增1；
- test cleanup额外Release第三次，分别回收ordinary object ref、既有manager-item leak和V271
  getPrimaryLayers getter leak。

## 9. Recovery IDB 写回

四库合计：

- 98 条 function/line comment；
- 12 个 bookmark；
- 10 个 `_guess`/语义 function rename；
- 8 个 function type/prototype update；
- 10 次定向 force-recompile/readback。

| 目标 | comments | bookmarks | renames/types | force readback | final bytes | final SHA-256 |
|---|---:|---:|---:|---:|---:|---|
| Android arm64 | 29 | 3 | 2 / 2 | 2 | 368547676 | `BE55EC7ADDE727156E836A1797E69AEA4E17B2C0888980B75A1E695E6B46C96C` |
| Android armv7 | 18 | 3 | 2 / 2 | 2 | 346744394 | `6239466F24E522E3FC77044E6C4792F235878BCC86E594D2533BEFBB2B65E400` |
| iOS arm64 | 24 | 3 | 3 / 2 | 3 | 336228262 | `6ED9F12E66796436730E0C203B4DE5B9B42E40BE6EB57F9C8E7B34CD3CF54DBC` |
| iOS armv7 | 27 | 3 | 3 / 2 | 3 | 376983454 | `3E44C1EED983FAF5BF2FF9FB0729B6092F931B9149C2C3838F7525C4EB903C81` |

iOS armv7 继续使用 prebackup/candidate/pre-post `idat -A`/canonical publish/fresh decompile路径。
最后四个canonical再次顺序通过`C:\IDA\idat.exe -A`；candidate从最终canonical同步，size/hash一致。

## 10. 构建与 Wasm 证据

验证结果：

- ordinary与`KRKR2_WASMTIME_HEADLESS=1`两种`motionplayer-dll.cpp` syntax-only exit 0；
- Web 3/3、Wasmtime 4/4、guest 1/1成功；三目标no-work复跑通过；
- 两个CTest均exit 0但准确报告`No tests were found!!!`，新增refcount断言仅syntax-validated；
- Wasmtime定向反汇编保留`DrawDeviceObjectBase::getPrimaryLayers() const`，显示manager range
  snapshot、GetPrimaryLayer虚调、owner field load和一次独立AddRef、cached stack owner地址两次传入
  deque helper，以及catch中只析构accessor；没有`PropSetByNum`路径。
- `git diff --check` exit 0，仅有工作树既有LF→CRLF conversion warning；
- 本轮compiled source/test精确行范围中的reference absolute-address扫描为0命中；
- `mcp__idalib__idb_list`为`sessions=[]`, `count=0`，也没有残留
  `ida`/`ida64`/`idat`/`idat64`/`idalib-worker`进程。

最终Wasm：

| 产物 | bytes | SHA-256 |
|---|---:|---|
| Web `index.wasm` | 85654999 | `17DF56EE37E070E6C333593BC5CCBC52BEF6E5706696BF4BAB41F1BA6DC73E6C` |
| Wasmtime `index.wasm` | 85002140 | `61EA065DE068695E3C76386F2B14D54AAD575E315978EB5320AA13070DDC391C` |
| Wasmtime guest | 151507941 | `13D4747FA69BBDDBF82118DFFAC1809DA20341ACC884D71800BA543FBC3A4A4B` |

相对V270：

| 产物 | total | CODE | 其他section delta |
|---|---:|---:|---|
| Web | `-69` | `-0x45` | 全部其他payload size不变 |
| Wasmtime | `-69` | `-0x45` | 全部其他payload size不变 |
| guest | `-137` | `-0x39` | `.debug_info -8`, `.debug_ranges -48`, `.debug_line -24` |

guest四项delta之和正好`-137`；name/DATA/debug_loc等其余payload size不变。两主产物对称减少
69-byte CODE，与删除第二条script numeric setter路径并加入更直接native path一致。

## 11. 闭合范围

V271 已闭合 `getPrimaryLayers` 的 manager vector snapshot、strict primary-layer chain、owning owner
getter、cached owner closure、direct native deque、null行为、per-call owner ref leak、返回Array owner与
成功/异常cleanup。

这不表示 motionplayer 全目标完成；下一纵切面继续沿尚未闭合的调用链、容器、owner与边界推进。
