# motionplayer D3DLayerObject borrowed adaptor / 四槽 NativeInstance 容器 / 构造重入生命周期四端恢复

日期：2026-08-17

## 1. 范围与结论

本轮只把 `reference/binaries/` 中四个当前参考二进制作为事实源，独立检查：

- Android arm64-v8a；
- Android armeabi-v7a；
- iOS arm64；
- iOS armv7。

目标是闭合此前仍未证明的 `D3DLayerObjectNativeInstance` 内部 class-ID、两字段
borrowed adaptor、`tTJSCustomObject::NativeInstanceSupport` 固定容器、重复注册、容量耗尽、
反向失效/析构，以及 `D3DLayer` raw factory descriptor 重入后的跨代数据流。

四端共同结论：

1. `D3DLayerObjectNativeInstance` 只有一个 process-global `tjs_int32` class-ID word；
2. adaptor 只有 `{vptr, D3DLayerObject *borrowed}` 两个字段，不拥有也不清空 payload；
3. `tTJSCustomObject` 固定持有四个 instance pointer 和四个 class ID，没有动态扩容；
4. REGISTER 从槽 0 向 3 找第一个 `classID == -1` 的空槽，不查重、不替换；满槽返回
   `TJS_E_FAIL (-1)`；
5. GET 同样从槽 0 向 3 查找，因此重复 class ID 返回最旧槽；缺失返回 `-1`；其他 flag
   返回 `TJS_E_NOTIMPL (-1002)`；
6. Finalize/Invalidate 和析构/Destruct 都按槽 `3 -> 2 -> 1 -> 0` 反向调用；
7. 普通 D3DLayer script shell 初始为 `slot0=concrete adaptor`、`slot1=first borrowed view`。
   raw descriptor 每次重入先追加 borrowed view，再把 slot0 的 concrete native pointer 直接改成
   新 layer；第一次、第二次重入占用 slot2/slot3，第三次起 borrowed REGISTER 失败但状态被
   忽略，刚分配的 adaptor 泄漏；
8. 因此 concrete D3DLayer 方法/属性看到最新 generation，而 D3D root 的 add/remove 始终
   通过 slot1 看到最旧 generation，形成可长期保持的 split-brain；
9. wrapper 覆盖 slot0 时还会泄漏旧 concrete layer。这和满槽时泄漏新的 borrowed adaptor
   是两个独立泄漏路径。

旧 `libkrkr2.so` 注释没有被用作证据；绝对地址只记录在本报告，不写进编译源码。

## 2. D3DLayerObject class-ID 与生产/消费调用链

| 目标 | class-ID word | ctor/producer | REGISTER call | root add | root remove |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x1AAF484` | 内联在 D3DLayer ctor `0x5333F0` | `0x53348C` | `0x52B82C` | `0x52B8B0` |
| Android armv7 | `0x110E0EC` | `0x496990` | `0x4969F4` | `0x492CA8` | `0x492D00` |
| iOS arm64 | `0x10256A0A4` | `0x1002355B4` | `0x100235634` | `0x100230D58` | `0x100230DBC` |
| iOS armv7 | `0x218E054` | `0x2342B4` | `0x23436C` | `0x22FC5E` | `0x22FC92` |

word 的生产者只有 `DrawDeviceD3D_PreRegist`：先加载 `emoteplayer.dll`，再直接调用
`TJSRegisterNativeClass("D3DLayerObjectNativeInstance")` 并写入该 word。没有完整 ClassInfo、
class object、global member、lazy find/register 或清理路径；这与 V208 的 PreRegist 结论一致。

构造器共同伪代码：

```cpp
D3DLayerObject::D3DLayerObject(iTJSDispatch2 *owner) {
    initialize_base_fields_and_empty_listener_list();
    if(owner) {
        auto *view = new BorrowedAdaptor;
        view->vptr = BorrowedAdaptorVtable;
        view->payload = this;
        iTJSNativeInstance *native = view;
        owner->NativeInstanceSupport(
            TJS_NIS_REGISTER, D3DLayerObjectClassID, &native);
        // status intentionally ignored
    }
}
```

allocation 明确发生在虚 REGISTER 之前；没有 unique_ptr、scope guard 或 status-failure delete。

root add/remove 的 lookup 均为：

```cpp
iTJSNativeInstance *native;
status = childDispatch->NativeInstanceSupport(
    TJS_NIS_GETINSTANCE, D3DLayerObjectClassID, &native);
if(status != TJS_S_OK)
    layer = nullptr;
else
    layer = static_cast<BorrowedAdaptor *>(native)->payload;
```

add 即使 lookup 失败仍把 null 传给 AddChild；remove 在 lookup 失败或 payload null 时直接
no-op。两者都没有改查 concrete D3DLayer ClassInfo ID，也没有选择最新重复项的逻辑。

## 3. borrowed adaptor 的精确布局与虚表

| 目标 | address point | Construct | Invalidate | Destruct | complete dtor | deleting dtor |
|---|---:|---:|---:|---:|---:|---:|
| Android arm64 | `0x19FAC80` | `0x524688` | `0x524690` | `0x524694` | `0x524818` | `0x533224` |
| Android armv7 | `0x10AB05C` | `0x48F930` | `0x48F934` | `0x48F936` | `0x48FA48` | `0x496B80` |
| iOS arm64 | `0x101AEE908` | `0x1000302F4` | `0x10003047C` | `0x100038D28` | `0x100235774` | `0x100235788` |
| iOS armv7 | `0x18390C0` | `0x2E944` | `0x2EA30` | `0x1296B0` | `0x23450C` | `0x23451C` |

布局在 LP64 为 `0x10`、ILP32 为 `0x08`：

```cpp
struct BorrowedAdaptor {
    void *vptr;
    D3DLayerObject *payload; // borrowed
};
```

虚槽语义四端一致：

- Construct 恒返 `TJS_S_OK`；
- Invalidate 是真正空函数；
- Destruct 对 non-null this 调 deleting-dtor 槽；
- complete dtor 只把 vptr 改回 `tTJSNativeInstance` 基类 vtable；
- deleting dtor 直接释放 adaptor 自身；
- 没有一个路径读取、删除或清零 payload。

所以 payload 可以在 adaptor 之前死亡。script shell Finalize 时空 Invalidate 不改变 dangling
值；稍后的 Destruct 仍只删除 adaptor，恰好不会解引用 dangling pointer。

## 4. tTJSCustomObject 固定四槽容器

### 4.1 地址映射

| 目标 | NativeInstanceSupport | Finalize/native Invalidate | dtor body/native Destruct | complete dtor | deleting dtor |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x9F9FC8` | `0x9F6548` | `0x9F62C0` | `0x9C6ED4` | `0x9C6ED8` |
| Android armv7 | `0x756074` | `0x753DEC` | `0x753CC4` | `0x7385C8` | `0x7385CC` |
| iOS arm64 | `0x10005B834` | `0x1000584B0` | `0x10005831C` | `0x1000A7B38` | `0x1000A7B3C` |
| iOS armv7 | `0x5A2C4` | `0x570D8` | `0x56E80` | `0xA6A6A` | `0xA6A6E` |

### 4.2 ABI 布局

| ABI | instance pointers | class IDs | fragment end |
|---|---:|---:|---:|
| LP64 | `this + 0x30`, 4 × 8 B | `this + 0x50`, 4 × 4 B | `0x60` |
| ILP32 | `this + 0x24`, 4 × 4 B | `this + 0x34`, 4 × 4 B | `0x44` |

Android/iOS 在同一 pointer width 下完全相同；平台 STL ABI 差异不影响这段核心布局。

### 4.3 精确状态机

```cpp
tjs_error NativeInstanceSupport(flag, classid, pointer) {
    if(flag == TJS_NIS_GETINSTANCE) {
        for(i = 0; i != 4; ++i) {
            if(ClassIDs[i] == classid) {
                *pointer = ClassInstances[i];
                return TJS_S_OK;
            }
        }
        return TJS_E_FAIL;
    }

    if(flag == TJS_NIS_REGISTER) {
        for(i = 0; i != 4; ++i) {
            if(ClassIDs[i] == -1) {
                ClassIDs[i] = classid;
                ClassInstances[i] = *pointer;
                return TJS_S_OK;
            }
        }
        return TJS_E_FAIL;
    }

    return TJS_E_NOTIMPL;
}
```

重要边界：

- REGISTER 不比较 `ClassIDs[i] == classid`，所以 equal ID 不是 replace，而是 append；
- full failure 发生在读取 `*pointer` 之前，不修改 output pointer 或已有 slot；
- GET 返回第一个 equal ID，不返回最后一个；
- slot 中 class ID 非 `-1` 但 pointer 为 null 时，GET 仍成功并写出 null；
- 析构/Finalize 也先看 class ID，再看 pointer；class ID 有效但 pointer null 时跳过虚调用；
- slot 永远不在 GET/REGISTER/Finalize/dtor 中清空或压缩。

### 4.4 反向生命周期顺序

Finalize 展开/循环的共同顺序：

```text
slot 3: if ID != -1 && instance != null -> instance->Invalidate()
slot 2: if ID != -1 && instance != null -> instance->Invalidate()
slot 1: if ID != -1 && instance != null -> instance->Invalidate()
slot 0: if ID != -1 && instance != null -> instance->Invalidate()
```

custom-object dtor 使用完全相同的 `3 -> 0` 顺序，但调用 `Destruct()`。这解释了普通
D3DLayer shell 的 teardown：borrowed slot3/2/1 先收到空 Invalidate，slot0 concrete adaptor
最后失效并删除最新 layer；随后 dtor 反向 Destruct，borrowed adaptor 只删除自己。

## 5. D3DLayer raw descriptor 重入：四代时间线

普通 `D3DLayerClass.CreateNew(root)` 的 NCBind 顺序为：先在新 script object 的 slot0 安装
concrete adaptor，再调用 raw factory。raw factory 构造 layer 时注册第一份 borrowed view：

| 时点 | slot0 | slot1 | slot2 | slot3 | concrete dispatch | root add/remove lookup |
|---|---|---|---|---|---|---|
| 初次 CreateNew | concrete -> G0 | borrowed -> G0 | empty | empty | G0 | G0 |
| descriptor 重入 1 | concrete -> G1 | borrowed -> G0 | borrowed -> G1 | empty | G1 | G0 |
| descriptor 重入 2 | concrete -> G2 | borrowed -> G0 | borrowed -> G1 | borrowed -> G2 | G2 | G0 |
| descriptor 重入 3 | concrete -> G3 | borrowed -> G0 | borrowed -> G1 | borrowed -> G2 | G3 | G0 |

重入 3 的 G3 borrowed adaptor 没进容器：REGISTER 返回 `-1`，构造器丢弃 status 和唯一裸
pointer，故 adaptor 泄漏。之后每次重入都重复这个泄漏。外层 raw wrapper 仍能找到 slot0
concrete adaptor，把它的 native pointer 改为新 layer，最后返回成功。

每代构造器还会经 parent setter 把新 layer 插入 root 的 front/back multisets；旧 concrete
layer 没有被 wrapper 删除，也没有被自动从 root 分离。结果：

- D3DLayer property/method 通过 slot0 操作 G3；
- `root.remove(scriptShell)` 通过 slot1 只移除 G0；
- 第二次 `remove(scriptShell)` 仍得到 G0，找不到已删除节点，G1/G2/G3 均不可由该 shell
  的 public remove 到达；
- `root.add(scriptShell)` 也会重复插入 G0，而不是当前 G3；
- shell 销毁只由当前 slot0 concrete adaptor 删除 G3；G0/G1/G2 是 wrapper overwrite 泄漏，
  borrowed adaptors 不承担回收责任。

这不是容器 bug 的推断，而是四端 REGISTER/GET、ctor producer、root consumer 和 raw wrapper
顺序的组合结果。

## 6. status failure 与异常边界

确定的普通 failure 路径：

```text
new BorrowedAdaptor succeeds
NativeInstanceSupport REGISTER returns -1 because all four slots are occupied
ctor ignores -1
raw wrapper overwrites concrete slot0 and reports success
BorrowedAdaptor leaks
```

异常路径也没有 adaptor RAII。iOS armv7 的 ctor 使用显式 SjLj 表：

- REGISTER call-site 为 2；
- landing `0x23438E` 调 D3DLayerObject/base cleanup `0x2325B0` 后 resume；
- landing 不读取保存 adaptor pointer，也不调用 adaptor dtor/operator delete。

因此自定义 owner 的 NativeInstanceSupport 若抛异常，已分配 adaptor 仍泄漏；base/list 成员按
构造展开阶段清理。其余三端的 ctor/landing 也没有 adaptor delete 路径；四端共同的普通
`TJS_E_FAIL` 泄漏则不依赖异常实现。

## 7. 源码与测试对齐

### 7.1 `cpp/core/tjs2/tjsObject.cpp`

算法原本已与参考一致，本轮没有“安全化”它。只补充证据注释，明确：

- 固定四槽；
- GET oldest-first；
- REGISTER first-empty/no-dedupe；
- full failure 不改状态；
- Finalize/dtor reverse order。

### 7.2 `cpp/plugins/DrawDeviceD3D.cpp`

- 加入 `TJS_MAX_NATIVE_CLASS == 4` 静态断言；
- 记录 borrowed adaptor 精确两字段/非所有权；
- 记录 allocation-before-REGISTER、status ignored、full-slot leak；
- 记录 GET oldest-first 与 concrete/borrowed split-brain；
- 扩充 D3DLayer factory re-entry 注释，写明 slot0..3 时间线与两种独立泄漏。

没有加入 rollback、replace、dedupe、detach 或 capacity growth，因为这些都会偏离四份参考。

### 7.3 `tests/unit-tests/plugins/motionplayer-dll.cpp`

新增 `D3DLayer constructor re-entry keeps the oldest borrowed four-slot view`：

1. 创建真实 D3D root 和 D3DLayer shell；
2. 保存初始 concrete layer 和 borrowed adaptor；
3. 对同一 receiver 连续调用 raw D3DLayer factory descriptor 三次；
4. 每次断言 concrete pointer 改成新 generation，但 borrowed GET 始终返回最初 adaptor；
5. 第二次重入后直接 REGISTER 断言四槽已满并返回 `TJS_E_FAIL`；
6. 第三次重入仍断言 raw factory 成功；
7. root children 从 1 增至 4；第一次 remove 只删 G0，第二次 remove 仍无法删除 G1..G3；
8. 测试尾部显式回收 wrapper 泄漏的旧 concrete generations，保留产品真实控制流。

## 8. recovery IDB 写回

四份 recovery IDB 已顺序打开、写回、保存并关闭；最终没有遗留 session。共加入：

- 8 个 ABI 类型：LP64/ILP32 borrowed adaptor 与 native-slot fragment，各库两项；
- 57 项 function/data 语义命名；所有 stripped/private 名称保留 `_guess`；
- 20 项 global/function type application；
- 37 条 producer/consumer/container/order/failure/lifetime 证据注释；
- 16 个 class-ID、ctor、NativeInstanceSupport、borrowed-vtable 书签；
- iOS armv7 额外命名并注释 ctor SjLj exception landing。

## 9. 验证

- ordinary Web syntax-only：通过；
- `KRKR2_WASMTIME_HEADLESS=1` syntax-only：通过；
- 两者仅保留既有 `_tss` literal-operator warning；
- `cmake --build out/web/debug`：通过；
- `cmake --build out/wasmtime/debug`：通过；
- 两个产物均 `WebAssembly.validate=true`；
- imports/exports：Web `539/69`，Wasmtime `538/69`；
- 两个 CTest tree 均 exit 0，并明确报告 `No tests were found`；新增 test TU 已由两种 syntax-only
  配置完整编译检查；
- `git diff --check` 通过，仅有工作树既有 LF→CRLF warning。

本轮只有注释、静态断言和未链接到产品 Wasm 的回归 TU 变化；两个产品产物与 V208 字节级
一致：

| 产物 | size | SHA-256 |
|---|---:|---|
| Web `out/web/debug/index.wasm` | 85,660,946 | `1706B037BCE4DC375992B2D2C63039CBAD3620C7C9BDA4993AE15F38CAAEBA9E` |
| Wasmtime `out/wasmtime/debug/index.wasm` | 85,008,087 | `139430AA13C51C78B16B957E22140E22D485288A2C05B8C10C137E5D835BFDBA` |

section 也与 V208 完全一致：

| section | Web | Wasmtime |
|---|---:|---:|
| FUNCTION | `0x1BD30` | `0x1BA4F` |
| GLOBAL | `0xD5C2` | `0xD5EA` |
| CODE | `0x1A42556` | `0x19EA504` |
| DATA | `0x5A4017` | `0x5A1267` |
| name | `0x3185EE4` | `0x3141D7A` |

## 10. 本纵切面剩余不确定性

- 私有 C++ 原名已剥离，恢复名继续使用 `_guess`；
- custom owner 抛异常属于非常规扩展边界；普通四槽耗尽和 status-ignore 行为已完全闭合；
- leaked generation/adaptor 的地址与可达性依 allocator、后续脚本行为而变，但泄漏发生条件、
  容器状态和 public lookup 目标均已由四端闭合；
- 本轮闭合的是 D3DLayerObject borrowed view 与 core native-slot 容器，不代表整个 motionplayer
  已完成；总目标继续进行。
