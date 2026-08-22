# motionplayer `D3DImage` ClassInfo、Factory、ManagedObjects、adaptor owner 四端恢复

日期：2026-08-17
纵切面：V204

## 范围

本纵切面只使用 `reference/binaries/` 中四个当前参考发布物：

- Android arm64-v8a；
- Android armeabi-v7a；
- iOS arm64；
- iOS armv7。

此前 D3DImage 专题已经闭合 native 三字段布局、texture holder、`load`、root
`ManagedObjects` set 与 D3DPicture raw borrowing。本轮从该 native core 向外继续恢复：

- `global.D3DImage` 独立 NCB ClassInfo 和整宽 guard；
- generated registrar 的 Regist/Unregist 事务与 no-unload；
- raw factory 的 root unbox、surplus、object/set allocation 异常路径；
- descriptor 的 exact-one-Void、concrete attach、失败 rollback 和 populated re-entry；
- standard concrete adaptor 的 owner/sticky 语义；
- concrete D3DImage existing-native producer 是否存在；
- ClassInfo、root set、holder、D3DPicture borrower 的完整相对寿命图。

四份二进制均 stripped，无法由发布物证明的私有拼写继续使用 `_guess`。旧
`libkrkr2.so` 地址和注释不作为证据。

## 结论摘要

1. D3DImage 有独立 ClassInfo，LP64 `0x20`、ILP32 `0x10`；它不是 raw factory arg0
   使用的 `D3DLayerBase` root class ID。
2. ClassInfo guard 与 V203 D3DLayer 一样是整宽 `8/4/8/4`。初始化器只测试低位，但写入
   64/32-bit `1`。
3. raw factory 只消费 arg0：无参数为 `TJS_E_BADPARAMCOUNT`；非 object、root adaptor
   query 失败或 root payload null 均为 `TJS_E_INVALIDTYPE`；surplus 不读取。
4. factory 创建 `0x18/0x0C` D3DImage，并在返回前把 `this` 插入 borrowed root 的
   non-owning `std::set<D3DImage *> ManagedObjects`。
5. set insertion 抛异常时，Android arm64 与两份 iOS 会删除已分配的 raw image storage；
   Android armv7 没有对应 unwind cleanup，会泄漏 `0x0C` storage。四端都没有已 commit 的
   set node需要回滚。
6. descriptor 的恰好一个 Void 是 empty-adaptor sentinel。正常结果 raw-attaches 到 concrete
   non-sticky adaptor；错误 receiver/concrete adaptor 会删除 fresh image，因而同时释放 holder、
   擦除刚插入的 ManagedObjects node，再返回 `TJS_E_NATIVECLASSCRASH`。
7. populated receiver re-entry 直接覆盖 concrete adaptor payload，不删除旧 image。旧 image
   继续留在 root set，旧 holder/texture ref 和所有 D3DPicture borrower 也继续存在。
8. concrete adaptor 是 D3DImage 唯一 owner；root set 只拥有 node，D3DPicture 只借用 raw pointer。
   全四端没有 `CreateAdaptor(existing D3DImage *)` producer，正常 populated shell 的唯一 producer
   是 raw factory wrapper。
9. generated Unregist 虚槽四端都存在，但 integrated DrawDeviceD3D loader 无 unload/registered-set
   erase caller；不能假设 runtime class/adaptor teardown。

## 独立 ClassInfo ABI

### 四端定位

| 目标 | ClassInfo | concrete class ID | guard | static init |
|---|---:|---:|---:|---:|
| Android arm64 | `0x1AAF648` | `0x1AAF658` | `0x1AAF668` | `0x42CAB8` |
| Android armv7 | `0x110E1F8` | `0x110E200` | `0x110E208` | `0x2FEF74` |
| iOS arm64 | `0x101AEE468` | `0x101AEE478` | `0x101AEE488` | `0x10024C9E0` |
| iOS armv7 | `0x1838E74` | `0x1838E7C` | `0x1838E84` | `0x24E5D0` |

### 布局

LP64：

```cpp
struct D3DImage_NCB_ClassInfo_guess {
    bool initialized;                 // +0x00
    unsigned char padding0[7];
    const tjs_char *className;        // +0x08
    tjs_int32 classID;                // +0x10
    unsigned char padding1[4];
    iTJSDispatch2 *classObject;       // +0x18
};                                    // 0x20
```

ILP32：

```cpp
struct D3DImage_NCB_ClassInfo_guess {
    bool initialized;                 // +0x00
    unsigned char padding0[3];
    const tjs_char *className;        // +0x04
    tjs_int32 classID;                // +0x08
    iTJSDispatch2 *classObject;       // +0x0C
};                                    // 0x10
```

guard 最终 data item：

| 目标 | size | 测试 | 写入 |
|---|---:|---|---|
| Android arm64 | `0x08` | low bit | qword `1` |
| Android armv7 | `0x04` | low byte/bit | dword `1` |
| iOS arm64 | `0x08` | low bit | qword `1` |
| iOS armv7 | `0x04` | low byte/bit | dword `1` |

Android 两端保留独立 ClassInfo leaves：

| 语义 | Android arm64 | Android armv7 |
|---|---:|---:|
| GetName | `0x52D6C8` | `0x4938E0` |
| GetID | `0x52D6D8` | `0x4938EC` |
| GetClassObject | `0x52D6E8` | `0x4938F8` |
| IsSubClass | `0x52D6F8` | `0x493904` |
| Set | `0x52D700` | `0x493908` |
| Clear | `0x52D738` | `0x493930` |
| zero ctor | `0x52D754` | `0x493944` |

`Set` 仍是 first-publication-wins：initialized 后拒绝替换 name/ID/classObject。iOS 优化器把
这些 leaves 内联，但 static init、Regist/Unregist、factory wrapper、member receiver resolver
都访问同一 tuple，因此布局和语义不依赖符号存在。

## registrar 与 no-unload

公开 member table 顺序固定为：

```text
Factory
width RO
height RO
load
```

| 目标 | member registrar | Regist | Unregist | vtable Regist / Unregist |
|---|---:|---:|---:|---:|
| Android arm64 | `0x52D768` | `0x53DA14` | `0x53DAA4` | `0x19FE858` / `0x19FE860` |
| Android armv7 | `0x493950` | `0x49FE6C` | `0x49FEF0` | `0x10ACE48` / `0x10ACE4C` |
| iOS arm64 | `0x100231AFC` | `0x100240BC8` | `0x100240C30` | `0x101AF2520` / `0x101AF2528` |
| iOS armv7 | `0x230932` | `0x2406A0` | `0x240754` | `0x183AEC8` / `0x183AECC` |

Android armv7 vtable 存 Thumb `entry + 1`，字节搜索 `0x49FE6D` / `0x49FEF1` 才在
`0x10ACE48` / `0x10ACE4C` 闭合两个槽；普通 even-entry xref 为空不是函数不可达证据。

共同事务：

```text
Regist:
    registration state.registering = true
    begin class publication
    replay Factory/width/height/load
    RegistEnd

Unregist:
    registration state.registering = false
    replay Factory/width/height/load
    UnregistEnd / clear ClassInfo
```

iOS 两端和 Android armv7 显式保留 RAII state dtor/flag dispatcher；Android arm64 把部分
unregister-end/clear 折叠进 virtual body。该编译器形状差异不改变源级事务。

全插件 loader/xref 搜索只找到 integrated load/publication。没有卸载 DrawDeviceD3D.dll、从
registered plugin set 擦除名称、或通过 registrar vtable 调 Unregist 的生产调用链。因此
Unregist 是 generated dormant path，不是 runtime 生命周期事件。

## raw factory 与 root identity

### 四端入口

| 目标 | raw factory | native size | constructor shape |
|---|---:|---:|---|
| Android arm64 | `0x52D98C` | `0x18` | 内联 |
| Android armv7 | `0x4939F8` | `0x0C` | 内联 |
| iOS arm64 | `0x100231BE8` | `0x18` | 内联 |
| iOS armv7 | `0x2309DC` | `0x0C` | 调 `0x234948` |

factory arg0 与 wrapper attach 使用两个不同 class ID：

| 目标 | D3DLayerBase/root ID | concrete D3DImage ID |
|---|---:|---:|
| Android arm64 | `0x1AAF6F8` | `0x1AAF658` |
| Android armv7 | `0x110E250` | `0x110E200` |
| iOS arm64 | `0x101AEE518` | `0x101AEE478` |
| iOS armv7 | `0x1838ECC` | `0x1838E7C` |

共同伪代码：

```cpp
tjs_error D3DImage_factory_guess(
    D3DImage **out,
    tjs_int argc,
    tTJSVariant **argv,
    iTJSDispatch2 *) {
    if(argc < 1)
        return TJS_E_BADPARAMCOUNT;
    if(argv[0]->Type() != tvtObject)
        return TJS_E_INVALIDTYPE;

    RootAdaptor *rootAdaptor =
        query(argv[0]->AsObjectNoAddRef(), D3DLayerBaseClassID);
    if(!rootAdaptor || !rootAdaptor->instance)
        return TJS_E_INVALIDTYPE;

    *out = new D3DImage(rootAdaptor->instance);
    return TJS_S_OK;
}
```

factory 不读 argv[1..]，也不使用 objthis。source-level constructor：

```cpp
D3DImage::D3DImage(DrawDeviceObjectBase *owner)
    : Owner(owner), Picture(nullptr) {
    Owner->ManagedObjects.insert(this);
}
```

factory 已证明 owner 非 null，因此 constructor 无 null guard。Owner 只是 borrowed pointer；
插入的 set node 也不拥有 image。

## set-insertion 异常矩阵

object storage 分配完成、set node allocation/insert helper 随后抛出时：

| 目标 | cleanup landing | raw object storage | set node |
|---|---:|---|---|
| Android arm64 | `0x52DB34` | `operator delete` | 未 commit |
| Android armv7 | 无 | 泄漏 `0x0C` | 未 commit |
| iOS arm64 | `0x100231CC4` | `operator delete` | 未 commit |
| iOS armv7 | `0x230AC8` | `operator delete` | 未 commit |

这不是普通 complete destructor：D3DImage construction 尚未成功，所以 cleanup 只释放 raw storage，
不调用 virtual holder clear，也不从 set erase。STL insertion 的异常保证使 node 在抛出时尚未成为
root tree 的 live member。

Android armv7 factory 没有 LSDA/landing cleanup；其 set helper 抛异常会越过已分配 image，导致
raw storage leak。这一差异不能用其它三端的 C++ new-expression cleanup 反向覆盖。

## descriptor wrapper

| 目标 | NativeClassFactory FuncCall |
|---|---:|
| Android arm64 | `0x53E1F8` |
| Android armv7 | `0x4A03C0` |
| iOS arm64 | `0x1002412FC` |
| iOS armv7 | `0x240F78` |

共同状态机：

```cpp
if(membername != nullptr)
    return TJS_E_MEMBERNOTFOUND;

if(argc == 1 && argv[0]->Type() == tvtVoid)
    return TJS_S_OK;                  // concrete adaptor remains empty

D3DImage *fresh = nullptr;
tjs_error er = rawFactory(&fresh, argc, argv, objthis);
if(TJS_FAILED(er))
    return er;

ConcreteD3DImageAdaptor *adaptor =
    query(objthis, D3DImageConcreteClassID);
if(!objthis || !adaptor) {
    delete fresh;
    return TJS_E_NATIVECLASSCRASH;
}

adaptor->instance = fresh;            // no deletion of previous instance
return TJS_S_OK;
```

### 边界矩阵

| 情形 | 返回 | ManagedObjects / native 结果 |
|---|---|---|
| named call | `TJS_E_MEMBERNOTFOUND` | 不调用 factory |
| exactly one Void | `TJS_S_OK` | empty concrete adaptor |
| argc 0 | `TJS_E_BADPARAMCOUNT` | 无 image/node |
| arg0 non-object | `TJS_E_INVALIDTYPE` | 无 image/node |
| object 无 root adaptor | `TJS_E_INVALIDTYPE` | 无 image/node |
| root adaptor payload null | `TJS_E_INVALIDTYPE` | 无 image/node |
| valid root + surplus | `TJS_S_OK` | fresh image + one root-set node |
| fresh image，objthis/concrete adaptor 缺失 | `TJS_E_NATIVECLASSCRASH` | deleting dtor 擦 fresh node并 delete image |
| populated receiver re-entry | `TJS_S_OK` | old image/node 泄漏；new image/node attach |

`Void + surplus` 不命中 sentinel，会进入 raw factory，因 arg0 non-object 返回
`TJS_E_INVALIDTYPE`。

### attach failure 的完整 rollback

raw factory 成功返回时，fresh image 已在 root set 中。wrapper concrete-adaptor lookup 失败后调用
image deleting dtor：

```text
clear holder (fresh image normally null)
erase fresh pointer from borrowed Owner.ManagedObjects
operator delete(image)
return TJS_E_NATIVECLASSCRASH
```

因此这条失败路径不会留下 managed-set dangling node。它与 constructor set-insertion throw 是
两条不同异常/失败阶段，不能合并。

### populated re-entry 的扩散泄漏

wrapper success 只写 adaptor payload slot。旧 image 不被 delete，所以：

- root ManagedObjects 仍含旧 image pointer；
- 旧 Picture holder 与 texture AddRef 保留；
- 旧 D3DPicture raw borrowers 仍指向旧 image；
- 新 image 另插一枚 root set node并成为 adaptor 新 payload。

root set 析构只删 node，不删 image，所以 root 后续析构也不会补偿该泄漏。若旧 image之后被其它
raw 路径 delete，它依赖 root 仍存活来擦除自己的 node。

## concrete adaptor owner

standard adaptor 布局：

```text
LP64: vptr + D3DImage *instance + bool sticky + padding = 0x18
ILP32: vptr + D3DImage *instance + bool sticky + padding = 0x0C
```

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| CreateEmptyAdaptor | `0x53DD14` | `0x4A0054` | `0x100240DA4` | `0x240960` |
| Invalidate | `0x53DD48` | `0x4A0078` | `0x100240DD8` | `0x240984` |
| complete dtor | `0x53DD88` | `0x4A0094` | `0x100240E18` | `0x2409A0` |
| deleting dtor | `0x53DDE4` | `0x4A00D0` | `0x100240E78` | `0x2409DA` |

ordinary Factory attach 从不置 sticky。adaptor Invalidate/dtor 在 payload non-null 且 non-sticky 时调用
D3DImage deleting dtor，因此 concrete adaptor 是 image 的唯一正常 owner。

四端对 concrete ClassInfo ID 的所有 xref 分类为：

- static/init/Get/Set/Clear；
- Regist/Unregist transaction；
- factory attach；
- width/height/load receiver payload resolver；
- D3DPicture typed D3DImage converter。

没有 `ClassInfo.classObject.CreateNew(Void) -> attach supplied D3DImage *` 的
`CreateAdaptor(existing-native)` producer。root set 和 D3DPicture 均只保存 raw pointer，不创建 TJS
shell。

## native object、holder 和 load

对象布局：

| 字段 | LP64 | ILP32 | 所有权 |
|---|---:|---:|---|
| vptr | `+0` | `+0` | ABI |
| Owner | `+8` | `+4` | borrowed root |
| Picture | `+16` | `+8` | owning heap `tTJSRefHolder<iTVPTexture2D> *` |
| sizeof | `0x18` | `0x0C` |  |

vtable 第三槽在两个 destructor slot 之后，执行：

```cpp
delete Picture;      // holder dtor virtual Release(texture)
Picture = nullptr;
```

完整析构顺序：holder release/null -> Owner null check ->
`Owner.ManagedObjects.erase(this)` -> object storage delete。它不访问/分离任何 D3DPicture。

`load` 仍保持前序专题确认的边界：

- `FromVariant -> GetMainImage -> GetTexture` 无额外 null fallback；
- software 分支从 source pixels 通过私有 opengl manager 新建 texture；
- new holder 对 texture AddRef；
- `Picture = newHolder` 直接覆盖，不 delete旧 holder；
- software-created texture 的初始引用不 Release；
- repeated load 泄漏旧 holder 和旧 texture ref。

本轮没有用 ClassInfo/adaptor 结论去“修复”这些 native leak。

## root ManagedObjects 与相对寿命

容器：

```cpp
std::set<D3DImage *> ManagedObjects;
```

| 目标 | root field offset | insertion | erase helper |
|---|---:|---:|---:|
| Android arm64 | `+0xA8`（header `+0xB0`） | factory inline | `0x533868` |
| Android armv7 | `+0x5C` | `0x496FEC` | `0x4970F4` |
| iOS arm64 | `+0x78` | `0x100235BD8` | `0x100235D54` |
| iOS armv7 | `+0x44` | `0x234A38` | `0x234B88` |

key 只是 D3DImage pointer address。root destructor 只释放红黑树 nodes，不 delete payload，也不把
`image.Owner` 清零。因此：

```text
D3DImage destruction must precede root destruction
```

若 root 先死，后续 concrete adaptor invalidation 会让 image dtor 沿 dangling Owner 操作已析构 set。
Owner-null guard不解决这个问题，因为 root 从不写 null。

## D3DPicture raw borrower

D3DPicture constructor 只保存 `D3DImage *`，不 AddRef image、不复制 Picture holder、不注册反向
borrower。destructor 从 D3DLayer listener list 注销并释放 ranges，但不访问 image。Draw 才走：

```text
D3DPicture.Image
  -> D3DImage.Picture
  -> holder.texture
```

因此相对寿命至少为：

```text
root outlives image destruction
image outlives every picture that may Draw
concrete adaptor owns image
root set and pictures borrow image
holder owns one texture reference
```

提前销毁 image 会让 picture dangling；提前销毁 root 会让 image destructor dangling。参考没有
backpointer clearing、shared ownership 或 observer invalidation。

## 源码与测试同步

`cpp/plugins/DrawDeviceD3D.cpp` 增强地址无关注释：

- 区分 concrete D3DImage ClassInfo 与 root ID；
- 记录 surplus、non-sticky attach、attach-failure set rollback；
- 记录 populated re-entry 的 image/root-set 扩散泄漏；
- 记录 set non-owning 与 root/image/picture 相对寿命；
- 记录 generated Unregist 但 integrated no-unload。

`tests/unit-tests/plugins/motionplayer-dll.cpp` 扩充注册级回归：

- exactly-one Void 产生 empty D3DImage concrete shell；
- `Void + surplus` 不命中 sentinel，返回 `TJS_E_INVALIDTYPE`；
- empty shell 的 `width` getter 因 null concrete payload 返回 `TJS_E_NATIVECLASSCRASH`；
- valid root + surplus 成功，后续 `width` getter 正常返回 0，证明 concrete payload 已 attach。

`analysis/motionplayer_d3dimage_holder_managedset_lifecycle_four_binary_2026-08-15.md`
修订 constructor exception 叙事，加入 Android arm64 cleanup 和 Android armv7 no-cleanup 差异。

## recovery IDB 写回

四份 `out/ida-recovery/` 数据库已原地保存并关闭：

- 8 个最终 typed data item：4 个 ClassInfo + 4 个整宽 guard；
- 4 个 `D3DImage_NCB_ClassInfo_guess` ABI 类型；
- 80 个从 stripped/default 名迁移的 `_guess` 语义函数名；
- 35 个函数签名应用；
- 69 个 ClassInfo、factory、exception、adaptor、vtable、managed-set、owner 注释地址；
- 4 个 V204 bookmark，均在 factory attach/ManagedObjects rollback 边界；
- 95 次成功 Hex-Rays cache invalidate/readback 请求。

最终 readback 确认：

- ClassInfo size `0x20/0x10/0x20/0x10`；
- guard size `0x08/0x04/0x08/0x04`；
- raw factory 使用 root ID；descriptor 使用 concrete ID；
- descriptor pseudocode 直接显示 non-sticky raw payload overwrite；
- Android armv7 Thumb Regist/Unregist vtable pair 已按 `+1` 指针闭合；
- 四库 save 成功且无遗留 session。

## 验证

本轮验证结果：

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两套完整 motionplayer unit-test TU
  `em++ -fsyntax-only` 均通过；首次检查准确发现私有 `D3DImage` C++ 类型不能从测试 TU 直接
  引用，回归改成上述 public script ABI getter 检查后两套都成功，输出只有既有 `_tss`
  literal-operator warning；
- `cmake --build out/web/debug` 与 `cmake --build out/wasmtime/debug` 都完成最终链接，只有既有
  `_tss`、pthread/memory-growth、JSPI 与 JS-library warning；
- Node `WebAssembly.Module` 对两份 `index.wasm` 解析成功；Web imports/exports `539/69`，
  headless `538/69`；
- 最终 artifact：

| 产物 | 文件大小 | SHA-256 | FUNCTION | GLOBAL | CODE | DATA | name |
|---|---:|---|---:|---:|---:|---:|---:|
| Web | 85,661,395 B | `45920E7ACCDFC78C173BB877E443E9D94A4284BCEB0F013515DC6E92D54CB219` | `0x1BD2F` | `0xD5B2` | `0x1A427D0` | `0x5A4017` | `0x3185E3C` |
| Headless | 85,008,536 B | `39984FEE7AB8FD687F4F53066A3E920BB3F29E0892304B55666D9871AB7D5B43` | `0x1BA4E` | `0xD5DA` | `0x19EA77E` | `0x5A1267` | `0x3141CD2` |

两份大小、hash、imports/exports 和表列 section 与 V203/V202 精确一致，证明本轮没有改变最终
插件机器码。两套 `ctest --output-on-failure` 均 0 退出但报告 `No tests were found!!!`；因此
新增回归当前只有两套完整 TU 编译和最终双链接保障，不表述为已运行 Catch2 runner。

## 未外推边界

- 不从 D3DImage 的 standard adaptor 推断 D3DPicture 必然相同；后者有 typed two-argument factory、
  D3DLayer listener owner 和 ranges vector，需要独立 V205；
- 不用三端 cleanup 强行修复 Android armv7 constructor-exception leak；
- 不用 root set 的存在推断 root 拥有 image；析构机器码明确否定；
- 不发明 runtime Unregist/unload；
- 不修复 repeated load、software initial-ref、populated re-entry、dangling root/picture borrower 等四端
  可观察边界；
- 私有 helper 原名不可证，继续保留 `_guess`。
